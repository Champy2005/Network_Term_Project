/** @file
 * @brief Verify committed history, restart capabilities, bounded reads, and storage failure
 * isolation.
 */
#include "chat/client/chat_client.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <sqlite3.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using chat::client::ChatClient;
using chat::client::RequestError;
using chat::protocol::Envelope;
using chat::test::Peer;
/// A disposable directory has an explicit root check before recursive cleanup.
class TemporaryDatabase {
  public:
    TemporaryDatabase() {
        static std::atomic<unsigned int> counter{0};
        root = std::filesystem::absolute("build/test-data").lexically_normal();
        directory =
            root / ("history-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                    "-" + std::to_string(++counter));
        std::filesystem::create_directories(directory);
    }
    ~TemporaryDatabase() {
        std::error_code ignored;
        if (directory.lexically_normal().parent_path() == root &&
            directory.filename().string().starts_with("history-")) {
            std::filesystem::remove_all(directory, ignored);
        }
    }
    std::string path() const { return (directory / "chat.sqlite3").string(); }
    std::filesystem::path root, directory;
};
/// Extra connection simulates contention/failed writes and inspects committed state.
class DatabaseProbe {
  public:
    explicit DatabaseProbe(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("probe open failed");
        }
    }
    ~DatabaseProbe() { static_cast<void>(sqlite3_close(db)); }
    void execute(const char* sql) {
        if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error("probe SQL failed");
        }
    }
    std::int64_t number(const char* sql) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            throw std::runtime_error("probe prepare failed");
        }
        const auto code = sqlite3_step(statement);
        const auto result = sqlite3_column_int64(statement, 0);
        static_cast<void>(sqlite3_finalize(statement));
        if (code != SQLITE_ROW) {
            throw std::runtime_error("probe result missing");
        }
        return result;
    }
    sqlite3* db = nullptr;
};
class HistoryTest : public testing::Test {
  protected:
    HistoryTest() { start(); }
    ~HistoryTest() override { stop(); }
    void start() {
        context.restart();
        server = std::make_unique<chat::server::PingServer>(
            context, boost::asio::ip::tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0},
            temporary.path());
        server->start();
        network = std::thread([this] { context.run(); });
    }
    void stop() {
        if (server) {
            server->stop();
            network.join();
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
            throw std::runtime_error("test login rejected");
        }
        return result;
    }
    void quiet(ChatClient& client) {
        const auto until = std::chrono::steady_clock::now() + 100ms;
        do {
            if (auto event = client.next_event(10ms)) {
                EXPECT_FALSE(std::holds_alternative<chat::client::DirectDelivery>(*event));
                EXPECT_FALSE(std::holds_alternative<chat::client::GroupDelivery>(*event));
            }
        } while (std::chrono::steady_clock::now() < until);
    }
    TemporaryDatabase temporary;
    boost::asio::io_context context;
    std::unique_ptr<chat::server::PingServer> server;
    std::thread network;
};

/// @brief Both original participants resume across restart; fresh name claims cannot inherit direct
/// history.
TEST_F(HistoryTest, DirectHistorySurvivesRestartAndRequiresOriginalCapability) {
    ChatClient alice, bob, charlie;
    const auto owner = login(alice, "alice");
    const auto recipient = login(bob, "bob");
    login(charlie, "charlie");
    ASSERT_EQ(owner.resume_token.size(), 64);
    ASSERT_EQ(recipient.resume_token.size(), 64);
    EXPECT_TRUE(owner.resume_token != recipient.resume_token);
    const std::string payload("Unicode ไทย\0suffix", 24);
    const auto sent = alice.send_direct("bob", payload);
    ASSERT_TRUE(sent.accepted);
    const auto initial = bob.history(sent.resource_id);
    ASSERT_TRUE(initial.accepted);
    ASSERT_EQ(initial.records.size(), 1);
    EXPECT_EQ(initial.records[0].text, payload);
    EXPECT_EQ(charlie.history(sent.resource_id).error, RequestError::history_forbidden);
    quiet(charlie);
    alice.close();
    bob.close();
    charlie.close();
    stop();
    start();
    ChatClient replacement;
    const auto fresh = login(replacement, "alice");
    EXPECT_TRUE(fresh.resume_token != owner.resume_token);
    EXPECT_EQ(replacement.history(sent.resource_id).error, RequestError::history_forbidden);
    replacement.close();
    // A ping on the new connection is not a cross-connection cleanup barrier; retry
    // only the explicit duplicate rejection until server EOF cleanup releases the name.
    alice.connect("127.0.0.1", server->port());
    chat::client::LoginResult resumed;
    for (int i = 0; i < 100 && !resumed.accepted; ++i) {
        resumed = alice.resume_login("alice", owner.resume_token);
        if (!resumed.accepted) {
            std::this_thread::sleep_for(5ms);
        }
    }
    ASSERT_TRUE(resumed.accepted);
    const auto restored = alice.history(sent.resource_id);
    ASSERT_TRUE(restored.accepted);
    ASSERT_EQ(restored.records.size(), 1);
    EXPECT_EQ(restored.records[0].text, payload);
    login(bob, "bob", recipient.resume_token);
    const auto reverse = bob.send_direct("alice", "reverse after restart");
    ASSERT_TRUE(reverse.accepted);
    EXPECT_EQ(reverse.resource_id, sent.resource_id);
    EXPECT_EQ(alice.history(sent.resource_id).records.size(), 2);
}

/// @brief Group discovery persists while active membership requires an explicit join after restart.
TEST_F(HistoryTest, GroupsPersistButHistoryRequiresCurrentVoluntaryMembership) {
    ChatClient alice, bob, charlie;
    const auto owner = login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto created = alice.create_group("Study");
    ASSERT_TRUE(created.accepted);
    ASSERT_TRUE(alice.send_group(created.resource_id, "old group message").accepted);
    EXPECT_EQ(bob.history(created.resource_id).error, RequestError::history_forbidden);
    EXPECT_EQ(charlie.history(created.resource_id).error, RequestError::history_forbidden);
    quiet(bob);
    quiet(charlie);
    ASSERT_TRUE(bob.join_group(created.resource_id).accepted);
    const auto past = bob.history(created.resource_id);
    ASSERT_TRUE(past.accepted);
    ASSERT_EQ(past.records.size(), 1);
    EXPECT_TRUE(past.group);
    EXPECT_TRUE(past.records[0].recipient.empty());
    alice.close();
    bob.close();
    charlie.close();
    stop();
    start();
    login(alice, "alice", owner.resume_token);
    EXPECT_EQ(alice.history(created.resource_id).error, RequestError::history_forbidden);
    EXPECT_EQ(alice.send_group(created.resource_id, "must rejoin").error,
              RequestError::not_group_member);
    bool found = false;
    for (int i = 0; i < 10 && !found; ++i) {
        if (auto event = alice.next_event(100ms)) {
            if (auto* groups = std::get_if<chat::client::GroupSnapshot>(&*event)) {
                ASSERT_EQ(groups->groups.size(), 1);
                EXPECT_EQ(groups->groups[0].group_id, created.resource_id);
                EXPECT_TRUE(groups->groups[0].members.empty());
                found = true;
            }
        }
    }
    EXPECT_TRUE(found);
    ASSERT_TRUE(alice.join_group(created.resource_id).accepted);
    EXPECT_EQ(alice.history(created.resource_id).records.size(), 1);
    const auto another = alice.create_group("Study");
    ASSERT_TRUE(another.accepted);
    EXPECT_NE(another.resource_id, created.resource_id);
}

/// @brief Stored rows are visible to another SQLite reader as soon as an ACK returns.
TEST_F(HistoryTest, AcknowledgementsFollowCommitAndTokensAreStoredOnlyAsHashes) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    DatabaseProbe probe(temporary.path());
    EXPECT_EQ(probe.number("SELECT count(*) FROM pragma_journal_mode WHERE journal_mode='wal'"), 1);
    EXPECT_EQ(probe.number("PRAGMA user_version"), 2);
    ASSERT_TRUE(alice.send_direct("bob", "committed first").accepted);
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 1);
    EXPECT_EQ(probe.number("SELECT count(*) FROM users WHERE length(token_hash)=64"), 2);
    EXPECT_EQ(
        probe.number("SELECT count(*) FROM pragma_table_info('users') WHERE name='resume_token'"),
        0);
    ASSERT_TRUE(alice.create_group("Study").accepted);
    EXPECT_EQ(probe.number("SELECT count(*) FROM admissions"), 1);
}

/// @brief Pages remain bounded/ordered and preserve maximum-size and embedded-NUL text.
TEST_F(HistoryTest, PaginationPreservesRoomIsolationAndBoundedFrames) {
    ChatClient alice, bob, charlie;
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    std::string room;
    for (int i = 0; i < 101; ++i) {
        const auto result = alice.send_direct("bob", std::string(4096, 'x'));
        ASSERT_TRUE(result.accepted);
        room = result.resource_id;
    }
    const auto unrelated = alice.send_direct("charlie", "different room");
    ASSERT_TRUE(unrelated.accepted);
    const auto page = alice.history(room, 0, 100);
    ASSERT_TRUE(page.accepted);
    ASSERT_EQ(page.records.size(), 100);
    EXPECT_TRUE(page.has_more);
    EXPECT_FALSE(page.group);
    for (std::size_t i = 0; i < page.records.size(); ++i) {
        EXPECT_EQ(page.records[i].text.size(), 4096);
        if (i != 0) {
            EXPECT_GT(page.records[i].id, page.records[i - 1].id);
        }
    }
    const auto tail = bob.history(room, page.next_after_id, 100);
    ASSERT_TRUE(tail.accepted);
    ASSERT_EQ(tail.records.size(), 1);
    EXPECT_FALSE(tail.has_more);
    const auto empty = alice.history(room, tail.next_after_id);
    EXPECT_TRUE(empty.accepted);
    EXPECT_TRUE(empty.records.empty());
    EXPECT_EQ(empty.next_after_id, tail.next_after_id);
    EXPECT_EQ(alice.history(unrelated.resource_id).records.size(), 1);
    EXPECT_EQ(alice.history(room, 0, 0).records.size(), 50);
    EXPECT_THROW(static_cast<void>(alice.history(room, 0, 101)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(alice.history(room, std::numeric_limits<std::uint64_t>::max())),
                 std::invalid_argument);
    EXPECT_EQ(alice.ping(17), 17);
}

/// @brief A writer lock rejects sends without ACK/echo and leaves room creation rolled back.
TEST_F(HistoryTest, BusyStorageNeverAcknowledgesOrDeliversUncommittedText) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    DatabaseProbe probe(temporary.path());
    probe.execute("BEGIN IMMEDIATE");
    const auto before = std::chrono::steady_clock::now();
    EXPECT_EQ(alice.send_direct("bob", "must not escape").error, RequestError::internal_error);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);
    EXPECT_EQ(alice.create_group("must not appear").error, RequestError::internal_error);
    quiet(alice);
    quiet(bob);
    probe.execute("ROLLBACK");
    EXPECT_EQ(probe.number("SELECT count(*) FROM messages"), 0);
    EXPECT_EQ(probe.number("SELECT count(*) FROM rooms"), 0);
    const auto sent = alice.send_direct("bob", "after unlock");
    ASSERT_TRUE(sent.accepted);
    EXPECT_EQ(alice.history(sent.resource_id).records.size(), 1);
}

/// @brief Insert failures roll back direct rooms and preserve group state for a later valid send.
TEST_F(HistoryTest, InsertFailureHasNoDeliveryAndNoPartialDirectRoom) {
    ChatClient alice, bob;
    login(alice, "alice");
    login(bob, "bob");
    const auto created = alice.create_group("Study");
    ASSERT_TRUE(created.accepted);
    ASSERT_TRUE(bob.join_group(created.resource_id).accepted);
    DatabaseProbe probe(temporary.path());
    probe.execute("CREATE TRIGGER reject_message BEFORE INSERT ON messages BEGIN SELECT "
                  "RAISE(ABORT,'test failure'); END");
    EXPECT_EQ(alice.send_direct("bob", "denied").error, RequestError::internal_error);
    EXPECT_EQ(alice.send_group(created.resource_id, "denied").error, RequestError::internal_error);
    quiet(alice);
    quiet(bob);
    EXPECT_EQ(probe.number("SELECT count(*) FROM rooms WHERE kind=0"), 0);
    EXPECT_TRUE(alice.history(created.resource_id).records.empty());
    probe.execute("DROP TRIGGER reject_message");
    ASSERT_TRUE(alice.send_group(created.resource_id, "valid").accepted);
    EXPECT_EQ(bob.history(created.resource_id).records.size(), 1);
    probe.execute("DROP TABLE messages");
    EXPECT_EQ(alice.history(created.resource_id).error, RequestError::internal_error);
    EXPECT_EQ(alice.ping(99), 99);
}

/// @brief Failed storage admission neither reserves a login name nor grants group membership.
TEST_F(HistoryTest, FailedAdmissionsLeaveLoginAndMembershipUnchanged) {
    ChatClient alice, bob;
    login(alice, "alice");
    const auto group = alice.create_group("Study");
    ASSERT_TRUE(group.accepted);
    bob.connect("127.0.0.1", server->port());
    DatabaseProbe probe(temporary.path());
    probe.execute("BEGIN IMMEDIATE");
    const auto denied = bob.login("bob");
    EXPECT_FALSE(denied.accepted);
    EXPECT_TRUE(denied.resume_token.empty());
    EXPECT_EQ(bob.ping(5), 5);
    probe.execute("ROLLBACK");
    ASSERT_TRUE(bob.login("bob").accepted);
    probe.execute("BEGIN IMMEDIATE");
    EXPECT_EQ(bob.join_group(group.resource_id).error, RequestError::internal_error);
    EXPECT_EQ(bob.history(group.resource_id).error, RequestError::history_forbidden);
    probe.execute("ROLLBACK");
    ASSERT_TRUE(bob.join_group(group.resource_id).accepted);
    EXPECT_TRUE(bob.history(group.resource_id).accepted);
    EXPECT_EQ(probe.number("SELECT count(*) FROM admissions"), 2);
}

/// @brief Wrong-name/unknown tokens reject login rather than falling back to a fresh identity.
TEST_F(HistoryTest, ResumeCapabilityIsBoundToItsNameAndActiveUniqueness) {
    ChatClient owner, stranger;
    const auto identity = login(owner, "alice");
    stranger.connect("127.0.0.1", server->port());
    EXPECT_FALSE(stranger.resume_login("bob", identity.resume_token).accepted);
    EXPECT_FALSE(stranger.resume_login("bob", std::string(64, '0')).accepted);
    EXPECT_FALSE(stranger.resume_login("alice", identity.resume_token).accepted);
    EXPECT_TRUE(stranger.login("bob").accepted);
    EXPECT_THROW(static_cast<void>(stranger.resume_login("x", "short")), std::invalid_argument);
}

/// @brief Raw malformed reads and unauthenticated probes return correlated errors without content.
TEST_F(HistoryTest, RawHistoryAdmissionAndAuthorizationAreCheckedByServer) {
    Peer peer(server->port());
    Envelope request;
    request.set_protocol_version(1);
    request.set_request_id("history-probe");
    request.mutable_history_request()->set_room_id("direct-1");
    peer.send(chat::test::encode(request));
    auto reply = peer.receive(1);
    ASSERT_EQ(reply.messages.size(), 1);
    EXPECT_EQ(reply.messages[0].error().code(), chat::protocol::ERROR_CODE_NOT_LOGGED_IN);
    request.mutable_login_request()->set_username("charlie");
    peer.send(chat::test::encode(request));
    ASSERT_TRUE(peer.receive(1).messages[0].login_response().accepted());
    auto* history = request.mutable_history_request();
    history->set_room_id("direct-1");
    history->set_limit(101);
    peer.send(chat::test::encode(request));
    reply = peer.receive(1);
    ASSERT_EQ(reply.messages.size(), 1);
    EXPECT_EQ(reply.messages[0].error().code(), chat::protocol::ERROR_CODE_INVALID_REQUEST);
    history->set_limit(1);
    history->set_after_id(std::numeric_limits<std::uint64_t>::max());
    peer.send(chat::test::encode(request));
    reply = peer.receive(1);
    ASSERT_EQ(reply.messages.size(), 1);
    EXPECT_EQ(reply.messages[0].error().code(), chat::protocol::ERROR_CODE_INVALID_REQUEST);
    history->set_after_id(0);
    history->set_room_id("direct-1 OR 1=1");
    peer.send(chat::test::encode(request));
    reply = peer.receive(1);
    ASSERT_EQ(reply.messages.size(), 1);
    EXPECT_EQ(reply.messages[0].error().code(), chat::protocol::ERROR_CODE_HISTORY_FORBIDDEN);
    EXPECT_EQ(reply.messages[0].request_id(), "history-probe");
}

/// @brief Missing directories, corrupt files, and future schema versions fail startup explicitly.
TEST(HistoryStartupTest, InvalidFilesAndUnsupportedSchemasFailBeforeListening) {
    TemporaryDatabase temporary;
    boost::asio::io_context context;
    const boost::asio::ip::tcp::endpoint endpoint{boost::asio::ip::address_v4::loopback(), 0};
    EXPECT_THROW(chat::server::PingServer invalid(
                     context, endpoint, (temporary.directory / "missing/chat.sqlite3").string()),
                 std::runtime_error);
    {
        std::ofstream corrupt(temporary.path(), std::ios::binary);
        corrupt << "not a database";
    }
    EXPECT_THROW(chat::server::PingServer invalid(context, endpoint, temporary.path()),
                 std::runtime_error);
    std::filesystem::remove(temporary.path());
    {
        DatabaseProbe probe(temporary.path());
        probe.execute("PRAGMA user_version=3");
    }
    EXPECT_THROW(chat::server::PingServer invalid(context, endpoint, temporary.path()),
                 std::runtime_error);
}

/// @brief Legacy ephemeral servers keep original login semantics and reject durable operations.
TEST(HistoryStartupTest, EphemeralServerExplicitlyRejectsHistoryAndResume) {
    boost::asio::io_context context;
    chat::server::PingServer server(context, {boost::asio::ip::address_v4::loopback(), 0});
    server.start();
    std::jthread network([&] { context.run(); });
    // Always stop before joining, including fatal assertion unwinding.
    struct Stop {
        chat::server::PingServer& server;
        ~Stop() { server.stop(); }
    } stop{server};
    ChatClient client;
    client.connect("127.0.0.1", server.port());
    EXPECT_FALSE(client.resume_login("alice", std::string(64, 'a')).accepted);
    const auto login = client.login("alice");
    EXPECT_TRUE(login.accepted);
    EXPECT_TRUE(login.resume_token.empty());
    EXPECT_EQ(client.history("direct-1").error, RequestError::invalid_request);
}

/// @brief Malformed history pages close the client: wrong room, cursor/order, bounds, or group
/// fields.
TEST(HistoryClientTest, InvalidPagesAreRejectedWithoutLeavingConnectionOpen) {
    for (int scenario = 0; scenario < 11; ++scenario) {
        boost::asio::io_context context;
        boost::asio::ip::tcp::acceptor acceptor(context,
                                                {boost::asio::ip::address_v4::loopback(), 0});
        ChatClient client;
        client.connect("127.0.0.1", acceptor.local_endpoint().port());
        Peer fake;
        acceptor.accept(fake.socket);
        auto responder = std::async(std::launch::async, [&] {
            const auto incoming = fake.receive(1);
            if (incoming.messages.size() != 1) {
                return false;
            }
            auto reply = incoming.messages[0];
            auto* page = reply.mutable_history_response();
            page->set_room_id("direct-1");
            auto* entry = page->add_entries();
            entry->set_id(1);
            entry->set_sender("alice");
            entry->set_recipient("bob");
            entry->set_text("hello");
            entry->set_message_id("message-1");
            entry->set_room_sequence(1);
            page->set_next_after_id(1);
            switch (scenario) {
            case 0:
                page->set_room_id("direct-2");
                break;
            case 1:
                entry->set_id(0);
                break;
            case 2:
                *page->add_entries() = *entry;
                break;
            case 3:
                entry->clear_text();
                break;
            case 4:
                entry->set_text(std::string(4097, 'x'));
                break;
            case 5:
                page->set_next_after_id(2);
                break;
            case 6:
                page->set_group(true);
                break;
            case 7:
                page->clear_entries();
                page->set_next_after_id(0);
                page->set_has_more(true);
                break;
            case 8:
                entry->clear_message_id();
                break;
            case 9:
                entry->set_room_sequence(0);
                break;
            case 10:
                *page->add_entries() = *entry;
                page->mutable_entries(1)->set_id(2);
                page->mutable_entries(1)->set_message_id("message-2");
                page->set_next_after_id(2); // Increasing cursor but repeated room sequence.
                break;
            }
            fake.send(chat::test::encode(reply));
            return true;
        });
        EXPECT_THROW(static_cast<void>(client.history("direct-1")), std::runtime_error);
        EXPECT_TRUE(responder.get());
        EXPECT_THROW(static_cast<void>(client.ping(1)), std::runtime_error);
    }
}

/// @brief A history response can be coalesced with a snapshot and a partial next event.
TEST(HistoryClientTest, HistoryExchangePreservesInterleavedAndPartialEvents) {
    boost::asio::io_context context;
    boost::asio::ip::tcp::acceptor acceptor(context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatClient client;
    client.connect("127.0.0.1", acceptor.local_endpoint().port());
    Peer fake;
    acceptor.accept(fake.socket);
    Envelope snapshot;
    snapshot.set_protocol_version(1);
    snapshot.mutable_user_list_update()->add_usernames("alice");
    Envelope event;
    event.set_protocol_version(1);
    event.mutable_group_list_update();
    const auto next = chat::test::encode(event);
    auto responder = std::async(std::launch::async, [&] {
        const auto incoming = fake.receive(1);
        if (incoming.messages.size() != 1) {
            return false;
        }
        auto reply = incoming.messages[0];
        reply.mutable_history_response()->set_room_id("direct-1");
        auto bytes = chat::test::encode(snapshot);
        const auto response = chat::test::encode(reply);
        bytes.insert(bytes.end(), response.begin(), response.end());
        bytes.insert(bytes.end(), next.begin(), next.begin() + 2);
        fake.send(bytes);
        return true;
    });
    const auto page = client.history("direct-1");
    EXPECT_TRUE(page.accepted);
    EXPECT_TRUE(page.records.empty());
    EXPECT_TRUE(responder.get());
    ASSERT_TRUE(std::holds_alternative<chat::client::UserSnapshot>(*client.next_event()));
    fake.send({next.begin() + 2, next.end()});
    EXPECT_TRUE(std::holds_alternative<chat::client::GroupSnapshot>(*client.next_event()));
}
} // namespace
