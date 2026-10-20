/** @file
 * @brief Exercise committed message receipts, retry privacy, restart, migration, and client
 * validation.
 */
#include "chat/client/chat_client.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <sqlite3.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using chat::client::ChatClient;
using chat::client::DirectDelivery;
using chat::client::GroupDelivery;
using chat::client::RequestError;
using chat::protocol::Envelope;
using chat::test::Peer;
/// Small independent SQLite observer for failure injection and committed-state checks.
class Probe {
  public:
    explicit Probe(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("probe open failed");
        }
    }
    ~Probe() { static_cast<void>(sqlite3_close(db)); }
    void sql(const char* text) {
        if (sqlite3_exec(db, text, nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error("probe SQL failed");
        }
    }
    std::int64_t number(const char* text) {
        sqlite3_stmt* query = nullptr;
        if (sqlite3_prepare_v2(db, text, -1, &query, nullptr) != SQLITE_OK) {
            throw std::runtime_error("probe prepare failed");
        }
        const auto code = sqlite3_step(query);
        const auto result = sqlite3_column_int64(query, 0);
        static_cast<void>(sqlite3_finalize(query));
        if (code != SQLITE_ROW) {
            throw std::runtime_error("probe query failed");
        }
        return result;
    }
    sqlite3* db = nullptr;
};
template <class T> T delivery(ChatClient& client) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    do {
        if (auto event = client.next_event(20ms)) {
            if (const auto* message = std::get_if<T>(&*event)) {
                return *message;
            }
        }
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error("test delivery missing");
}
void quiet(ChatClient& client) {
    const auto end = std::chrono::steady_clock::now() + 80ms;
    do {
        if (const auto event = client.next_event(10ms)) {
            EXPECT_FALSE(std::holds_alternative<DirectDelivery>(*event));
            EXPECT_FALSE(std::holds_alternative<GroupDelivery>(*event));
        }
    } while (std::chrono::steady_clock::now() < end);
}
class ReliabilityTest : public testing::Test {
  protected:
    ReliabilityTest() {
        root = std::filesystem::absolute("build/test-data").lexically_normal();
        directory = root / ("reliability-" + ChatClient::new_message_key());
        std::filesystem::create_directories(directory);
        start(path());
    }
    ~ReliabilityTest() override {
        stop();
        std::error_code ignored;
        if (directory.parent_path() == root &&
            directory.filename().string().starts_with("reliability-")) {
            std::filesystem::remove_all(directory, ignored);
        }
    }
    std::string path() const { return (directory / "chat.sqlite3").string(); }
    void start(const std::string& database) {
        context.restart();
        server = std::make_unique<chat::server::PingServer>(
            context, boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0},
            database);
        server->start();
        for (auto& worker : workers) {
            worker = std::thread([this] { context.run(); });
        }
    }
    void stop() {
        if (server) {
            server->stop();
            for (auto& worker : workers) {
                worker.join();
            }
            server.reset();
            context.restart();
            context.poll();
        }
    }
    chat::client::LoginResult login(ChatClient& client, const std::string& name,
                                    const std::string& token = {}) {
        client.connect("127.0.0.1", server->port());
        auto result = token.empty() ? client.login(name) : client.resume_login(name, token);
        if (!result.accepted) {
            throw std::runtime_error("test login failed");
        }
        return result;
    }
    std::filesystem::path root, directory;
    boost::asio::io_context context;
    std::unique_ptr<chat::server::PingServer> server;
    std::array<std::thread, 2> workers;
};

/// @brief ACK, sender echo, recipient delivery, and history share IDs and room order with privacy.
TEST_F(ReliabilityTest, DirectReceiptsMatchEchoDeliveryAndHistory) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto first = alice.send_direct("bob", "first");
    ASSERT_TRUE(first.accepted);
    EXPECT_EQ(first.room_sequence, 1);
    EXPECT_EQ(first.client_message_id.size(), 64);
    const auto echo = delivery<DirectDelivery>(alice);
    const auto received = delivery<DirectDelivery>(bob);
    EXPECT_EQ(first.message_id, echo.message_id);
    EXPECT_EQ(first.message_id, received.message_id);
    EXPECT_EQ(received.room_sequence, first.room_sequence);
    EXPECT_EQ(echo.server_timestamp_unix_ms, received.server_timestamp_unix_ms);
    EXPECT_GT(received.server_timestamp_unix_ms, 0);
    quiet(charlie);
    const auto second = bob.send_direct("alice", "reply");
    EXPECT_EQ(second.resource_id, first.resource_id);
    EXPECT_EQ(second.room_sequence, 2);
    EXPECT_NE(second.message_id, first.message_id);
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    const auto separate = alice.send_direct("charlie", "other room");
    EXPECT_EQ(separate.room_sequence, 1);
    EXPECT_NE(separate.resource_id, first.resource_id);
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(charlie);
    const auto page = alice.history(first.resource_id);
    ASSERT_EQ(page.records.size(), 2);
    EXPECT_EQ(page.records[0].message_id, first.message_id);
    EXPECT_EQ(page.records[1].message_id, second.message_id);
    EXPECT_EQ(page.records[1].room_sequence, 2);
    EXPECT_EQ(page.records[0].server_timestamp_unix_ms, echo.server_timestamp_unix_ms);
}

/// @brief Both group senders share one sequence; another group starts at one and excludes
/// outsiders.
TEST_F(ReliabilityTest, GroupOrderIsSharedAcrossSendersAndSeparateAcrossRooms) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto group = alice.create_group("Study").resource_id;
    ASSERT_TRUE(bob.join_group(group).accepted);
    const auto first = alice.send_group(group, "first");
    EXPECT_EQ(first.room_sequence, 1);
    EXPECT_EQ(delivery<GroupDelivery>(alice).message_id, first.message_id);
    EXPECT_EQ(delivery<GroupDelivery>(bob).message_id, first.message_id);
    const auto second = bob.send_group(group, "reply");
    EXPECT_EQ(second.room_sequence, 2);
    EXPECT_EQ(delivery<GroupDelivery>(alice).room_sequence, 2);
    delivery<GroupDelivery>(bob);
    const auto other = alice.create_group("Other").resource_id;
    EXPECT_EQ(alice.send_group(other, "separate").room_sequence, 1);
    delivery<GroupDelivery>(alice);
    quiet(bob);
    quiet(charlie);
    EXPECT_EQ(alice.history(group).records.size(), 2);
}

/// @brief Exact retries return the committed receipt without another stored row or any delivery.
TEST_F(ReliabilityTest, DirectRetryReturnsOriginalAckWithoutRedelivery) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const std::string text("before\0after", 12);
    const auto first = alice.send_direct_with_key("bob", text, "saved-key");
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    const auto again = alice.send_direct_with_key("bob", text, "saved-key");
    EXPECT_TRUE(again.accepted);
    EXPECT_EQ(again.message_id, first.message_id);
    EXPECT_EQ(again.room_sequence, first.room_sequence);
    quiet(alice);
    quiet(bob);
    quiet(charlie);
    Probe probe(path());
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 1);
    EXPECT_EQ(probe.number("SELECT max(last_sequence) FROM rooms"), 1);
    const auto next = alice.send_direct("bob", text);
    EXPECT_EQ(next.room_sequence, 2);
    EXPECT_NE(next.message_id, first.message_id);
}

/// @brief A saved key is scoped to the durable sender and rejects changed type, target, or text.
TEST_F(ReliabilityTest, RetryConflictsNeverStoreOrDeliverChangedContent) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto group = alice.create_group("Study").resource_id;
    const auto first = alice.send_direct_with_key("bob", "original", "key");
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    EXPECT_EQ(alice.send_direct_with_key("bob", "changed", "key").error,
              RequestError::message_id_conflict);
    EXPECT_EQ(alice.send_direct_with_key("charlie", "original", "key").error,
              RequestError::message_id_conflict);
    EXPECT_EQ(alice.send_group_with_key(group, "original", "key").error,
              RequestError::message_id_conflict);
    quiet(alice);
    quiet(bob);
    quiet(charlie);
    const auto independent = bob.send_direct_with_key("alice", "own key", "key");
    EXPECT_TRUE(independent.accepted);
    EXPECT_NE(independent.message_id, first.message_id);
    EXPECT_EQ(independent.room_sequence, 2);
    Probe probe(path());
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 2);
}

/// @brief Duplicate group sends do not fan out again; conflict rejection preserves room sequence.
TEST_F(ReliabilityTest, GroupRetryIsIdempotentAndChangedTargetsConflict) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto group = alice.create_group("Study").resource_id;
    const auto other = alice.create_group("Other").resource_id;
    ASSERT_TRUE(bob.join_group(group).accepted);
    const auto first = alice.send_group_with_key(group, "hello", "group-key");
    delivery<GroupDelivery>(alice);
    delivery<GroupDelivery>(bob);
    const auto again = alice.send_group_with_key(group, "hello", "group-key");
    EXPECT_EQ(again.message_id, first.message_id);
    EXPECT_EQ(again.room_sequence, 1);
    EXPECT_EQ(alice.send_group_with_key(other, "hello", "group-key").error,
              RequestError::message_id_conflict);
    EXPECT_EQ(charlie.send_group_with_key(group, "hello", "group-key").error,
              RequestError::not_group_member);
    quiet(alice);
    quiet(bob);
    quiet(charlie);
    EXPECT_EQ(alice.history(group).records.size(), 1);
}

/// @brief Restart/resume preserves receipts and sequences; fresh same-name claims cannot reuse
/// keys.
TEST_F(ReliabilityTest, RestartResumesDeduplicationAndSequencesWithoutOfflineRedelivery) {
    ChatClient alice, bob;
    const auto owner = login(alice, "alice");
    const auto recipient = login(bob, "bob");
    const auto first = alice.send_direct_with_key("bob", "hello", "persisted");
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    alice.close();
    bob.close();
    stop();
    start(path());
    login(alice, "alice", owner.resume_token);
    const auto again = alice.send_direct_with_key("bob", "hello", "persisted");
    EXPECT_TRUE(again.accepted);
    EXPECT_EQ(again.message_id, first.message_id);
    quiet(alice);
    login(bob, "bob", recipient.resume_token);
    quiet(bob);
    EXPECT_EQ(alice.send_direct("bob", "next").room_sequence, 2);
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    alice.close();
    bob.close();
    stop();
    start(path());
    login(alice, "alice");
    login(bob, "bob", recipient.resume_token);
    const auto fresh = alice.send_direct_with_key("bob", "hello", "persisted");
    EXPECT_TRUE(fresh.accepted);
    EXPECT_NE(fresh.resource_id, first.resource_id);
    EXPECT_NE(fresh.message_id, first.message_id);
    EXPECT_EQ(fresh.room_sequence, 1);
    EXPECT_EQ(alice.history(first.resource_id).error, RequestError::history_forbidden);
}

/// @brief A reclaimed recipient name never receives a prior retry; its next new message has a new
/// room.
TEST_F(ReliabilityTest, RetryPinsOriginalRecipientDespiteNameReplacement) {
    ChatClient alice, bob;
    const auto owner = login(alice, "alice");
    login(bob, "bob");
    const auto first = alice.send_direct_with_key("bob", "original", "pinned");
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    alice.close();
    bob.close();
    stop();
    start(path());
    login(alice, "alice", owner.resume_token);
    login(bob, "bob");
    EXPECT_EQ(alice.send_direct_with_key("bob", "original", "pinned").message_id, first.message_id);
    quiet(alice);
    quiet(bob);
    const auto fresh = alice.send_direct("bob", "for replacement");
    EXPECT_NE(fresh.resource_id, first.resource_id);
    EXPECT_EQ(fresh.room_sequence, 1);
}

/// @brief Resuming a group sender still requires current self-join before an accepted retry is
/// read.
TEST_F(ReliabilityTest, GroupRetryAfterRestartRequiresExplicitMembership) {
    ChatClient alice;
    const auto owner = login(alice, "alice");
    const auto group = alice.create_group("Study").resource_id;
    const auto first = alice.send_group_with_key(group, "hello", "group-resume");
    delivery<GroupDelivery>(alice);
    alice.close();
    stop();
    start(path());
    login(alice, "alice", owner.resume_token);
    EXPECT_EQ(alice.send_group_with_key(group, "hello", "group-resume").error,
              RequestError::not_group_member);
    ASSERT_TRUE(alice.join_group(group).accepted);
    EXPECT_EQ(alice.send_group_with_key(group, "hello", "group-resume").message_id,
              first.message_id);
    quiet(alice);
    EXPECT_EQ(alice.send_group(group, "next").room_sequence, 2);
}

/// @brief A failed post-insert sequence update rolls back both the message and its retry key.
TEST_F(ReliabilityTest, FailedTransactionDoesNotConsumeSequenceOrRetryKey) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    const auto first = alice.send_direct("bob", "first");
    delivery<DirectDelivery>(alice);
    delivery<DirectDelivery>(bob);
    Probe probe(path());
    probe.sql("CREATE TRIGGER fail_sequence BEFORE UPDATE OF last_sequence ON rooms BEGIN SELECT "
              "RAISE(ABORT,'blocked'); END;");
    EXPECT_EQ(alice.send_direct_with_key("bob", "second", "retry-failed").error,
              RequestError::internal_error);
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 1);
    EXPECT_EQ(probe.number("SELECT max(last_sequence) FROM rooms"), 1);
    quiet(alice);
    quiet(bob);
    probe.sql("DROP TRIGGER fail_sequence");
    const auto second = alice.send_direct_with_key("bob", "second", "retry-failed");
    EXPECT_TRUE(second.accepted);
    EXPECT_EQ(second.room_sequence, first.room_sequence + 1);
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 2);
}

/// @brief Two socket senders are serialized into one contiguous group sequence by the shared
/// strand.
TEST_F(ReliabilityTest, ConcurrentGroupSendersProduceContiguousSharedOrder) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    const auto group = alice.create_group("Study").resource_id;
    ASSERT_TRUE(bob.join_group(group).accepted);
    auto send = [&](ChatClient& client) {
        for (int i = 0; i < 12; ++i) {
            if (!client.send_group(group, "concurrent").accepted) {
                return false;
            }
        }
        return true;
    };
    auto a = std::async(std::launch::async, [&] { return send(alice); });
    auto b = std::async(std::launch::async, [&] { return send(bob); });
    EXPECT_TRUE(a.get());
    EXPECT_TRUE(b.get());
    for (std::uint64_t i = 1; i <= 24; ++i) {
        const auto left = delivery<GroupDelivery>(alice);
        const auto right = delivery<GroupDelivery>(bob);
        EXPECT_EQ(left.room_sequence, i);
        EXPECT_EQ(right.room_sequence, i);
        EXPECT_EQ(left.message_id, right.message_id);
    }
    const auto page = alice.history(group);
    ASSERT_EQ(page.records.size(), 24);
    EXPECT_EQ(page.records.back().room_sequence, 24);
}

/// @brief Legacy empty keys mean distinct sends; request correlation IDs alone never deduplicate.
TEST_F(ReliabilityTest, LegacyRequestsKeepCorrelationSeparateFromRetryIdentity) {
    Peer peer(server->port());
    auto request = chat::test::ping(1, "same-request-id");
    request.mutable_login_request()->set_username("alice");
    peer.send(chat::test::encode(request));
    ASSERT_EQ(peer.receive(1).messages.size(), 1);
    request.mutable_direct_message()->set_recipient("alice");
    request.mutable_direct_message()->set_text("legacy");
    for (std::uint64_t i = 1; i <= 2; ++i) {
        peer.send(chat::test::encode(request));
        const auto read = peer.receive(2);
        ASSERT_EQ(read.messages.size(), 2);
        EXPECT_EQ(read.messages[0].request_id(), "same-request-id");
        EXPECT_EQ(read.messages[0].ack().room_sequence(), i);
        EXPECT_EQ(read.messages[1].direct_message().room_sequence(), i);
    }
    request.mutable_direct_message()->set_client_message_id("raw-key");
    peer.send(chat::test::encode(request));
    const auto first = peer.receive(2);
    ASSERT_EQ(first.messages.size(), 2);
    request.set_request_id("different-request-id");
    peer.send(chat::test::encode(request));
    const auto retry = peer.receive(1);
    ASSERT_EQ(retry.messages.size(), 1);
    EXPECT_EQ(retry.messages[0].request_id(), "different-request-id");
    EXPECT_EQ(retry.messages[0].ack().message_id(), first.messages[0].ack().message_id());
    EXPECT_TRUE(peer.receive(1, 80ms).timed_out);
}

/// @brief Ephemeral deduplication retains 256 keyed receipts, then deliberately evicts the oldest.
TEST_F(ReliabilityTest, EphemeralRetryWindowIsBoundedAndSequencesStillAdvance) {
    stop();
    start({});
    ChatClient alice;
    EXPECT_TRUE(login(alice, "alice").resume_token.empty());
    const auto first = alice.send_direct_with_key("alice", "same", "oldest");
    delivery<DirectDelivery>(alice);
    EXPECT_EQ(alice.send_direct_with_key("alice", "same", "oldest").message_id, first.message_id);
    quiet(alice);
    for (int i = 0; i < 256; ++i) {
        ASSERT_TRUE(
            alice.send_direct_with_key("alice", "same", "key-" + std::to_string(i)).accepted);
        delivery<DirectDelivery>(alice);
    }
    const auto evicted = alice.send_direct_with_key("alice", "same", "oldest");
    EXPECT_NE(evicted.message_id, first.message_id);
    EXPECT_EQ(evicted.room_sequence, 258);
    delivery<DirectDelivery>(alice);
}

/// @brief Server and client reject invalid keys without consuming a message or disconnecting peers.
TEST_F(ReliabilityTest, RetryKeyValidationIsBoundedAtBothEnds) {
    ChatClient client;
    login(client, "alice");
    Peer peer(server->port());
    auto request = chat::test::ping(0, "login");
    request.mutable_login_request()->set_username("bob");
    peer.send(chat::test::encode(request));
    ASSERT_EQ(peer.receive(1).messages.size(), 1);
    for (const auto& bad :
         {std::string{}, std::string(65, 'x'), std::string("bad key"), std::string("a\0b", 3)}) {
        EXPECT_THROW(static_cast<void>(client.send_direct_with_key("alice", "text", bad)),
                     std::invalid_argument);
        if (!bad.empty()) {
            auto outgoing = chat::test::ping(0, "invalid-key");
            outgoing.mutable_direct_message()->set_recipient("alice");
            outgoing.mutable_direct_message()->set_text("text");
            outgoing.mutable_direct_message()->set_client_message_id(bad);
            peer.send(chat::test::encode(outgoing));
            const auto result = peer.receive(1);
            ASSERT_EQ(result.messages.size(), 1);
            EXPECT_EQ(result.messages[0].error().code(),
                      chat::protocol::ERROR_CODE_INVALID_REQUEST);
        }
    }
    EXPECT_TRUE(client.send_direct_with_key("alice", "text", std::string(64, 'x')).accepted);
    delivery<DirectDelivery>(client);
    EXPECT_EQ(client.ping(1), 1);
    quiet(client);
}

/// @brief A v1 database upgrades atomically, keeping cursors/text/admissions and backfilling room
/// order.
TEST_F(ReliabilityTest, VersionOneMigrationPreservesHistoryAndContinuesSequences) {
    stop();
    const auto old = (directory / "v1.sqlite3").string();
    {
        Probe probe(old);
        probe.sql(
            "CREATE TABLE users(id INTEGER PRIMARY KEY AUTOINCREMENT,name TEXT NOT NULL,token_hash "
            "TEXT NOT NULL UNIQUE);"
            "CREATE TABLE rooms(id INTEGER PRIMARY KEY AUTOINCREMENT,kind INTEGER NOT NULL,name "
            "TEXT NOT NULL DEFAULT '',a INTEGER,b INTEGER,UNIQUE(a,b));"
            "CREATE TABLE admissions(room INTEGER NOT NULL,principal INTEGER NOT NULL,PRIMARY "
            "KEY(room,principal));"
            "CREATE TABLE messages(id INTEGER PRIMARY KEY AUTOINCREMENT,room INTEGER NOT "
            "NULL,principal INTEGER NOT NULL,sender TEXT NOT NULL,recipient TEXT NOT NULL,text "
            "TEXT NOT NULL);"
            "CREATE INDEX messages_room_cursor ON messages(room,id);"
            "INSERT INTO users VALUES(1,'legacy','hash'); INSERT INTO rooms(id,kind,name) "
            "VALUES(1,1,'Study'),(2,1,'Other');"
            "INSERT INTO admissions VALUES(1,1);"
            "INSERT INTO messages "
            "VALUES(1,1,1,'legacy','','one'),(2,2,1,'legacy','','other'),(3,1,1,'legacy','','two');"
            "PRAGMA user_version=1;");
    }
    start(old);
    ChatClient alice;
    login(alice, "alice");
    ASSERT_TRUE(alice.join_group("group-1").accepted);
    const auto page = alice.history("group-1");
    ASSERT_EQ(page.records.size(), 2);
    EXPECT_EQ(page.records[0].id, 1);
    EXPECT_EQ(page.records[1].id, 3);
    EXPECT_EQ(page.records[0].room_sequence, 1);
    EXPECT_EQ(page.records[1].room_sequence, 2);
    EXPECT_EQ(page.records[1].message_id, "message-3");
    EXPECT_EQ(page.records[1].server_timestamp_unix_ms, 0);
    EXPECT_EQ(alice.send_group("group-1", "new").room_sequence, 3);
    Probe probe(old);
    EXPECT_EQ(probe.number("PRAGMA user_version"), 2);
    EXPECT_EQ(probe.number("SELECT count(*) FROM admissions WHERE principal=1"), 1);
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages WHERE client_key IS NULL"), 3);
}

/// @brief Losing the sender's unread ACK still permits receipt recovery after restart without
/// fanout.
TEST_F(ReliabilityTest, UnreadAckCanBeRecoveredWithSavedKeyAndIdentity) {
    ChatClient bob;
    const auto recipient = login(bob, "bob");
    Peer raw(server->port());
    auto request = chat::test::ping(0, "login");
    request.mutable_login_request()->set_username("alice");
    raw.send(chat::test::encode(request));
    const auto logged = raw.receive(1);
    ASSERT_EQ(logged.messages.size(), 1);
    const auto token = logged.messages[0].login_response().resume_token();
    ASSERT_EQ(token.size(), 64);
    request.set_request_id("unread-ack");
    auto* dm = request.mutable_direct_message();
    dm->set_recipient("bob");
    dm->set_text("committed");
    dm->set_client_message_id("saved-before-send");
    raw.send(chat::test::encode(request));
    const auto received =
        delivery<DirectDelivery>(bob); // Commit and routing barrier; ACK is unread.
    raw.socket.close();
    bob.close();
    stop();
    start(path());
    ChatClient alice;
    login(alice, "alice", token);
    const auto recovered = alice.send_direct_with_key("bob", "committed", "saved-before-send");
    EXPECT_TRUE(recovered.accepted);
    EXPECT_EQ(recovered.message_id, received.message_id);
    EXPECT_EQ(recovered.room_sequence, received.room_sequence);
    login(bob, "bob", recipient.resume_token);
    quiet(alice);
    quiet(bob);
    Probe probe(path());
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 1);
}

/// @brief Failed group sequence updates roll back text/key and permit a later exact retry.
TEST_F(ReliabilityTest, GroupCommitFailureDoesNotConsumeRetryOrSequence) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    const auto group = alice.create_group("Study").resource_id;
    ASSERT_TRUE(bob.join_group(group).accepted);
    Probe probe(path());
    probe.sql("CREATE TRIGGER fail_group BEFORE UPDATE OF last_sequence ON rooms BEGIN SELECT "
              "RAISE(ABORT,'blocked'); END;");
    EXPECT_EQ(alice.send_group_with_key(group, "text", "group-failure").error,
              RequestError::internal_error);
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 0);
    EXPECT_EQ(probe.number("SELECT max(last_sequence) FROM rooms"), 0);
    quiet(alice);
    quiet(bob);
    probe.sql("DROP TRIGGER fail_group");
    const auto accepted = alice.send_group_with_key(group, "text", "group-failure");
    EXPECT_TRUE(accepted.accepted);
    EXPECT_EQ(accepted.room_sequence, 1);
    EXPECT_EQ(delivery<GroupDelivery>(alice).message_id, accepted.message_id);
    EXPECT_EQ(delivery<GroupDelivery>(bob).message_id, accepted.message_id);
}

/// @brief A malformed v1 schema fails migration before listening and rolls back earlier ALTER work.
TEST_F(ReliabilityTest, FailedMigrationLeavesVersionAndExistingTablesUnchanged) {
    stop();
    const auto broken = (directory / "broken-v1.sqlite3").string();
    {
        Probe probe(broken);
        probe.sql("CREATE TABLE rooms(id INTEGER PRIMARY KEY,kind INTEGER,name TEXT);"
                  "CREATE TABLE messages(id INTEGER PRIMARY KEY,sequence INTEGER);"
                  "INSERT INTO messages VALUES(1,7); PRAGMA user_version=1;");
    }
    EXPECT_THROW(chat::server::PingServer invalid(
                     context,
                     (boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0}),
                     broken),
                 std::runtime_error);
    Probe probe(broken);
    EXPECT_EQ(probe.number("PRAGMA user_version"), 1);
    EXPECT_EQ(
        probe.number("SELECT count(*) FROM pragma_table_info('rooms') WHERE name='last_sequence'"),
        0);
    EXPECT_EQ(probe.number("SELECT sequence FROM messages WHERE id=1"), 7);
}

/// @brief Invalid metadata in ACKs or deliveries closes the headless connection.
TEST(ReliabilityClientTest, MalformedMessageMetadataAndCorrelationAreRejected) {
    for (int scenario = 0; scenario < 8; ++scenario) {
        Peer fake;
        boost::asio::ip::tcp::acceptor acceptor(fake.context,
                                                {boost::asio::ip::address_v4::loopback(), 0});
        ChatClient client;
        client.connect("127.0.0.1", acceptor.local_endpoint().port());
        acceptor.accept(fake.socket);
        auto responder = std::async(std::launch::async, [&] {
            const auto incoming = fake.receive(1);
            if (incoming.messages.size() != 1) {
                return false;
            }
            auto reply = incoming.messages[0];
            auto* ack = reply.mutable_ack();
            ack->set_resource_id("direct-1");
            ack->set_message_id("message-1");
            ack->set_room_sequence(1);
            switch (scenario) {
            case 0:
                ack->clear_message_id();
                break;
            case 1:
                ack->set_room_sequence(0);
                break;
            case 2:
                ack->set_message_id(std::string(129, 'x'));
                break;
            case 3:
                reply.set_request_id("wrong-correlation");
                break;
            case 4:
                ack->set_resource_id("group-1");
                break;
            default:
                reply.clear_request_id();
                auto* dm = reply.mutable_direct_message();
                dm->set_sender("alice");
                dm->set_recipient("bob");
                dm->set_room_id("direct-1");
                dm->set_text("text");
                dm->set_message_id("message-1");
                dm->set_room_sequence(1);
                dm->set_server_timestamp_unix_ms(1);
                if (scenario == 5) {
                    dm->set_client_message_id("private-retry-key");
                }
                if (scenario == 6) {
                    dm->set_room_sequence(0);
                }
                if (scenario == 7) {
                    dm->set_server_timestamp_unix_ms(0);
                }
                break;
            }
            fake.send(chat::test::encode(reply));
            return true;
        });
        EXPECT_THROW(static_cast<void>(client.send_direct_with_key("bob", "text", "key", 400ms)),
                     std::runtime_error);
        EXPECT_TRUE(responder.get());
        EXPECT_THROW(static_cast<void>(client.ping(1)), std::runtime_error);
    }
}
} // namespace
