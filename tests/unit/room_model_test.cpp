/** @file
 * @brief Verify independent room state, drafts, reconnect generations, and retention bounds.
 */
#include "chat/client/room_model.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

namespace {
class RoomModelTest : public testing::Test {
  protected:
    RoomModelTest() {
        model.reset("alice");
        model.apply(chat::client::UserSnapshot{{"alice", "bob", "charlie"}});
    }
    chat::client::RoomModel model;
};
/// @brief Route sender echoes and incoming text to the same pair view, keeping other pairs
/// separate.
TEST_F(RoomModelTest, DirectParticipantsShareOneViewAndOtherPairsStaySeparate) {
    model.open_direct("bob");
    const auto key = model.current()->key;
    model.apply(chat::client::DirectDelivery{"alice", "bob", "direct-1", "first"});
    model.apply(chat::client::DirectDelivery{"bob", "alice", "direct-1", "reply"});
    model.apply(chat::client::DirectDelivery{"charlie", "alice", "direct-2", "separate"});
    ASSERT_EQ(model.rooms().size(), 2);
    EXPECT_EQ(model.current()->key, key);
    ASSERT_EQ(model.current()->lines.size(), 2);
    EXPECT_EQ(model.current()->lines[1].text, "reply");
    model.open_direct("charlie");
    ASSERT_EQ(model.current()->lines.size(), 1);
    EXPECT_EQ(model.current()->lines.front().text, "separate");
}
/// @brief Distinct IDs keep same-named groups and their transcripts independent.
TEST_F(RoomModelTest, GroupDisplayNamesDoNotMergeRoomIdentity) {
    model.apply(
        chat::client::GroupSnapshot{{{"g1", "Study", {"alice"}}, {"g2", "Study", {"bob"}}}});
    model.apply(chat::client::GroupDelivery{"alice", "g1", "private to members"});
    model.open_group("g2");
    EXPECT_FALSE(model.current()->joined);
    EXPECT_TRUE(model.current()->lines.empty());
    model.open_group("g1");
    EXPECT_TRUE(model.current()->joined);
    EXPECT_EQ(model.current()->lines.size(), 1);
}
/// @brief Navigation preserves an independent unsent draft for every room.
TEST_F(RoomModelTest, DraftsSurviveNavigationAndSnapshots) {
    model.open_direct("bob");
    model.draft("draft for bob");
    model.open_direct("charlie");
    model.draft("draft for charlie");
    model.apply(chat::client::UserSnapshot{{"alice", "bob", "charlie"}});
    EXPECT_EQ(model.current()->draft, "draft for charlie");
    model.open_direct("bob");
    EXPECT_EQ(model.current()->draft, "draft for bob");
}
/// @brief An ACK clears only its submitted draft and never duplicates the server echo.
TEST_F(RoomModelTest, AcceptanceDoesNotEraseNewTypingOrAddOptimisticLines) {
    model.open_direct("bob");
    const auto key = model.current()->key;
    model.draft("submitted");
    model.draft("new typing");
    model.sent(key, "submitted");
    EXPECT_EQ(model.current()->draft, "new typing");
    EXPECT_TRUE(model.current()->lines.empty());
    model.sent(key, "new typing");
    EXPECT_TRUE(model.current()->draft.empty());
}
/// @brief An offline/reclaimed peer gets a fresh room view, preserving the old transcript
/// separately.
TEST_F(RoomModelTest, ReclaimedPeerCannotInheritTheOldTranscript) {
    model.open_direct("bob");
    model.apply(chat::client::DirectDelivery{"bob", "alice", "old", "old text"});
    const auto old = model.current()->key;
    model.apply(chat::client::UserSnapshot{{"alice"}});
    EXPECT_FALSE(model.current()->available);
    model.apply(chat::client::UserSnapshot{{"alice", "bob"}});
    model.open_direct("bob");
    EXPECT_NE(model.current()->key, old);
    model.apply(chat::client::DirectDelivery{"bob", "alice", "new", "new text"});
    ASSERT_EQ(model.current()->lines.size(), 1);
    EXPECT_EQ(model.current()->lines.front().text, "new text");
    model.select(old);
    EXPECT_EQ(model.current()->lines.front().text, "old text");
}
/// @brief Group snapshots replace joined-state without losing local transcripts or drafts.
TEST_F(RoomModelTest, GroupSnapshotsReplaceMembershipAndAvailability) {
    model.apply(chat::client::GroupSnapshot{{{"g", "Study", {"alice", "bob"}}}});
    model.open_group("g");
    model.draft("unsent");
    model.apply(chat::client::GroupDelivery{"bob", "g", "hello"});
    model.apply(chat::client::GroupSnapshot{{{"g", "Study", {"bob"}}}});
    EXPECT_FALSE(model.current()->joined);
    EXPECT_EQ(model.current()->draft, "unsent");
    model.apply(chat::client::GroupSnapshot{});
    EXPECT_FALSE(model.current()->available);
    EXPECT_EQ(model.current()->lines.size(), 1);
}
/// @brief Disconnect preserves local drafts/history; a new local login clears the previous
/// identity.
TEST_F(RoomModelTest, DisconnectAndNewLoginHaveExplicitRetentionRules) {
    model.open_direct("bob");
    model.draft("unsent");
    model.apply(chat::client::DirectDelivery{"bob", "alice", "r", "hello"});
    model.disconnected();
    EXPECT_FALSE(model.current()->available);
    EXPECT_EQ(model.current()->draft, "unsent");
    EXPECT_EQ(model.current()->lines.size(), 1);
    model.reset("other");
    EXPECT_TRUE(model.rooms().empty());
    EXPECT_EQ(model.current(), nullptr);
}
/// @brief Retain only the newest 100 lines in a busy room.
TEST_F(RoomModelTest, PerRoomHistoryIsBounded) {
    model.open_direct("bob");
    for (int i = 0; i < 150; ++i) {
        model.apply(chat::client::DirectDelivery{"bob", "alice", "r", std::to_string(i)});
    }
    ASSERT_EQ(model.current()->lines.size(), 100);
    EXPECT_EQ(model.current()->lines.front().text, "50");
}
/// @brief Evict globally oldest lines when many rooms exceed the shared string-byte budget.
TEST_F(RoomModelTest, GlobalHistoryByteBudgetIsBounded) {
    for (int room = 0; room < 8; ++room) {
        for (int i = 0; i < 100; ++i) {
            model.apply(chat::client::DirectDelivery{"bob", "alice", "r" + std::to_string(room),
                                                     std::string(4096, 'x')});
        }
    }
    std::size_t bytes = 0;
    for (const auto& room : model.rooms()) {
        for (const auto& line : room.lines) {
            bytes += line.sender.size() + line.text.size();
        }
    }
    EXPECT_LE(bytes, 2U * 1024U * 1024U);
    EXPECT_TRUE(model.rooms().front().lines.empty());
}
/// @brief Repeated peer generations cannot grow the room collection without a limit.
TEST_F(RoomModelTest, RoomCountIsBoundedAcrossManyGenerations) {
    model.apply(chat::client::GroupSnapshot{{{"g", "Study", {"alice", "bob"}}}});
    for (int i = 0; i < 300; ++i) {
        model.apply(chat::client::DirectDelivery{"bob", "alice", "r" + std::to_string(i), "hello"});
    }
    EXPECT_EQ(model.rooms().size(), 256);
    model.apply(chat::client::GroupDelivery{"bob", "g", "still available"});
    model.open_group("g");
    EXPECT_EQ(model.current()->lines.front().text, "still available");
}
/// @brief Bad navigation, unrelated direct deliveries, oversized drafts, and empty lines fail
/// explicitly.
TEST_F(RoomModelTest, InvalidStateDoesNotCreateUnrelatedTranscripts) {
    EXPECT_THROW(model.open_direct("offline"), std::invalid_argument);
    EXPECT_THROW(model.open_group("missing"), std::invalid_argument);
    EXPECT_THROW(model.apply(chat::client::DirectDelivery{"bob", "charlie", "r", "unrelated"}),
                 std::invalid_argument);
    model.open_direct("bob");
    EXPECT_THROW(model.draft(std::string(16385, 'x')), std::invalid_argument);
    EXPECT_THROW(model.apply(chat::client::DirectDelivery{"bob", "alice", "r", ""}),
                 std::invalid_argument);
    EXPECT_TRUE(model.current()->lines.empty());
}
/// @brief Replayed live sequences are suppressed even after retention eviction and disconnect.
TEST_F(RoomModelTest, SequenceCursorSurvivesEvictionAndSuppressesReplayDuplicates) {
    model.open_direct("bob");
    for (std::uint64_t sequence = 1; sequence <= 105; ++sequence) {
        model.apply(chat::client::DirectDelivery{
            "bob", "alice", "r", "text", "message-" + std::to_string(sequence), sequence, 1});
    }
    ASSERT_EQ(model.current()->lines.size(), 100);
    EXPECT_EQ(model.current()->last_sequence, 105);
    model.draft("unsent");
    model.disconnected();
    model.apply(chat::client::DirectDelivery{"bob", "alice", "r", "text", "message-1", 1, 1});
    EXPECT_EQ(model.current()->lines.size(), 100);
    model.apply(chat::client::DirectDelivery{"bob", "alice", "r", "new", "message-106", 106, 1});
    EXPECT_EQ(model.current()->last_sequence, 106);
    EXPECT_EQ(model.current()->draft, "unsent");
    EXPECT_EQ(model.current()->lines.back().text, "new");
}
/// @brief Original-peer proof restores an existing draft; a fresh namesake cannot activate it.
TEST_F(RoomModelTest, DirectRecoveryUsesServerRoomIdentityRatherThanPresenceNames) {
    model.open_direct("bob");
    model.apply(chat::client::DirectDelivery{"bob", "alice", "direct-1", "old", "message-1", 1, 1});
    const auto original = model.current()->key;
    model.draft("retained draft");
    model.disconnected();
    model.apply(chat::client::UserSnapshot{{"alice", "bob"}});
    model.restore_direct({"direct-1", "bob", 1, false});
    EXPECT_FALSE(model.current()->available);
    model.restore_direct({"direct-1", "bob", 1, true});
    EXPECT_TRUE(model.current()->available);
    EXPECT_EQ(model.current()->key, original);
    EXPECT_EQ(model.current()->draft, "retained draft");
    EXPECT_EQ(model.current()->last_sequence, 1);
    model.restore_direct({"direct-2", "bob", 3, true});
    model.open_direct("bob");
    EXPECT_NE(model.current()->key, original);
    EXPECT_TRUE(model.current()->lines.empty());
    EXPECT_EQ(model.current()->last_sequence,
              0); // Directory counters are not applied-message cursors.
}
/// @brief On-demand history pages merge earlier records in sequence order without duplicating live
/// lines.
TEST_F(RoomModelTest, ApplyHistoryMergesEarlierRecordsInOrderWithoutDuplicates) {
    model.apply(chat::client::GroupSnapshot{{{"group-1", "Study", {"alice", "bob"}}}});
    model.open_group("group-1");
    model.apply(chat::client::GroupDelivery{"bob", "group-1", "live third", "m3", 3, 30});
    chat::client::HistoryPage page;
    page.accepted = true;
    page.group = true;
    page.room_id = "group-1";
    page.records = {{1, "alice", "group-1", "stored first", "m1", 1, 10},
                    {2, "bob", "group-1", "stored second", "m2", 2, 20},
                    {3, "bob", "group-1", "live third", "m3", 3, 30}};
    model.apply_history(page);
    ASSERT_EQ(model.current()->lines.size(), 3U);
    EXPECT_EQ(model.current()->lines[0].text, "stored first");
    EXPECT_EQ(model.current()->lines[1].text, "stored second");
    EXPECT_EQ(model.current()->lines[2].text, "live third");
    EXPECT_EQ(model.current()->last_sequence, 3U);
}
} // namespace
