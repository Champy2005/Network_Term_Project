/** @file
 * @brief Verify private sequence replay, watermark paging, lost ACKs, and bounded recovery.
 */
#include "chat/client/chat_client.hpp"
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include "chat/server/ping_server.hpp"
#include "recovery_peer.hpp"
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <sqlite3.h>

namespace {
using namespace std::chrono_literals;
using chat::client::ChatClient;
using chat::client::ChatWorker;
using chat::client::CommandKind;
using chat::client::RequestError;
using chat::client::WorkerCompleted;
using chat::client::WorkerConnected;
using chat::client::WorkerDisconnected;
using chat::client::WorkerReconnecting;
template <class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 4s;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
chat::client::WorkerLimits limits() {
    chat::client::WorkerLimits result;
    result.operation_timeout = 400ms;
    result.poll_timeout = 5ms;
    result.heartbeat_interval = 100ms;
    result.reconnect_delay = 20ms;
    result.reconnect_max_delay = 40ms;
    result.reconnect_attempts = 64;
    return result;
}
/// @brief Apply the actual UI update order and retain correlated completions for assertions.
struct Observer {
    chat::client::RoomModel model;
    std::vector<WorkerCompleted> completed;
    std::vector<chat::client::ClientEvent> events;
    std::string terminal;
    bool connected = false;
    std::size_t reconnecting = 0, resumed = 0;
    void poll(ChatWorker& worker) {
        for (const auto& update : worker.poll()) {
            if (const auto* ready = std::get_if<WorkerConnected>(&update)) {
                if (!ready->resumed) {
                    model.reset(ready->username);
                } else {
                    ++resumed;
                }
                connected = true;
            } else if (const auto* offline = std::get_if<WorkerDisconnected>(&update)) {
                terminal = offline->reason;
                connected = false;
                model.disconnected();
            } else if (std::holds_alternative<WorkerReconnecting>(update)) {
                ++reconnecting;
                connected = false;
                model.disconnected();
            } else if (const auto* event = std::get_if<chat::client::ClientEvent>(&update)) {
                model.apply(*event);
                events.push_back(*event);
            } else if (const auto* room = std::get_if<chat::client::WorkerDirectRoom>(&update)) {
                model.restore_direct(room->room);
            } else {
                completed.push_back(std::get<WorkerCompleted>(update));
            }
        }
    }
    const WorkerCompleted* result(std::uint64_t id) const {
        for (const auto& value : completed) {
            if (value.id == id) {
                return &value;
            }
        }
        return nullptr;
    }
};
class RecoveryTest : public testing::Test {
  protected:
    RecoveryTest() {
        root = std::filesystem::absolute("build/test-data").lexically_normal();
        directory = root / ("recovery-" + ChatClient::new_message_key());
        std::filesystem::create_directories(directory);
        start();
    }
    ~RecoveryTest() override {
        stop();
        std::error_code ignored;
        if (directory.parent_path() == root &&
            directory.filename().string().starts_with("recovery-")) {
            std::filesystem::remove_all(directory, ignored);
        }
    }
    std::string path() const { return (directory / "chat.sqlite3").string(); }
    void start() {
        context.restart();
        server = std::make_unique<chat::server::PingServer>(
            context, chat::test::tcp::endpoint{boost::asio::ip::address_v4::loopback(), port},
            path());
        port = server->port();
        server->start();
        network = std::thread([this] { context.run(); });
    }
    void stop() {
        if (!server) {
            return;
        }
        server->stop();
        network.join();
        server.reset();
        context.restart();
        context.poll();
    }
    chat::client::LoginResult login(ChatClient& client, const std::string& name,
                                    const std::string& token = {}) {
        client.connect("127.0.0.1", port);
        const auto result = token.empty() ? client.login(name) : client.resume_login(name, token);
        if (!result.accepted) {
            throw std::runtime_error("test login rejected");
        }
        return result;
    }
    void worker(ChatWorker& client, Observer& observer, std::uint16_t target) {
        ASSERT_TRUE(client.start("127.0.0.1", target, "alice"));
        ASSERT_TRUE(until([&] {
            observer.poll(client);
            return observer.connected;
        }));
    }
    std::filesystem::path root, directory;
    boost::asio::io_context context;
    std::unique_ptr<chat::server::PingServer> server;
    std::thread network;
    std::uint16_t port = 0;
};

/// @brief A captured watermark excludes later live writes while page boundaries stay contiguous.
TEST_F(RecoveryTest, SequencePagesKeepOneWatermarkAndDoNotChaseNewWrites) {
    ChatClient alice;
    login(alice, "alice");
    const auto group = alice.create_group("Study").resource_id;
    for (int n = 0; n < 7; ++n) {
        ASSERT_TRUE(alice.send_group(group, std::to_string(n)).accepted);
    }
    const auto first = alice.sync(group, 0, 0, 2);
    ASSERT_TRUE(first.accepted);
    ASSERT_EQ(first.records.size(), 2);
    EXPECT_EQ(first.through_sequence, 7);
    EXPECT_EQ(first.next_sequence, 2);
    EXPECT_TRUE(first.has_more);
    ASSERT_TRUE(alice.send_group(group, "later live write").accepted);
    auto cursor = first.next_sequence;
    bool more = true;
    while (more) {
        const auto page = alice.sync(group, cursor, first.through_sequence, 2);
        ASSERT_TRUE(page.accepted);
        for (const auto& record : page.records) {
            EXPECT_EQ(record.room_sequence, ++cursor);
        }
        EXPECT_EQ(page.through_sequence, 7);
        more = page.has_more;
    }
    EXPECT_EQ(cursor, 7);
    const auto last = alice.sync(group, cursor);
    ASSERT_EQ(last.records.size(), 1);
    EXPECT_EQ(last.records.front().text, "later live write");
    const auto empty = alice.sync(group, 8);
    EXPECT_TRUE(empty.accepted);
    EXPECT_TRUE(empty.records.empty());
    EXPECT_FALSE(empty.has_more);
    EXPECT_EQ(empty.next_sequence, 8);
}

/// @brief Direct room discovery is principal scoped, paginated, and valid with an offline peer.
TEST_F(RecoveryTest, DirectDiscoveryAndSyncDoNotLeakToFreshSameNameOrThirdUser) {
    ChatClient alice, bob, charlie;
    const auto identity = login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto ab = alice.send_direct("bob", "private");
    ASSERT_TRUE(ab.accepted);
    const auto ac = alice.send_direct("charlie", "other");
    ASSERT_TRUE(ac.accepted);
    auto first = alice.direct_rooms(0, 1);
    ASSERT_EQ(first.rooms.size(), 1);
    EXPECT_EQ(first.rooms.front().room_id, ab.resource_id);
    EXPECT_EQ(first.rooms.front().peer, "bob");
    EXPECT_TRUE(first.rooms.front().peer_connected);
    EXPECT_TRUE(first.has_more);
    auto second = alice.direct_rooms(first.next_room_id, 1);
    ASSERT_EQ(second.rooms.size(), 1);
    EXPECT_EQ(second.rooms.front().room_id, ac.resource_id);
    EXPECT_FALSE(second.has_more);
    EXPECT_EQ(charlie.sync(ab.resource_id).error, RequestError::history_forbidden);
    EXPECT_EQ(bob.direct_rooms().rooms.size(), 1);
    alice.close();
    ASSERT_TRUE(until([&] {
        ChatClient replacement;
        replacement.connect("127.0.0.1", port);
        const auto claimed = replacement.login("alice");
        if (!claimed.accepted) {
            return false;
        }
        EXPECT_TRUE(replacement.direct_rooms().rooms.empty());
        EXPECT_EQ(replacement.sync(ab.resource_id).error, RequestError::history_forbidden);
        return true;
    }));
    ASSERT_TRUE(until([&] {
        alice.connect("127.0.0.1", port);
        return alice.resume_login("alice", identity.resume_token).accepted;
    }));
    bob.close();
    const auto page = alice.sync(ab.resource_id);
    ASSERT_EQ(page.records.size(), 1);
    EXPECT_EQ(page.records.front().text, "private");
    EXPECT_EQ(alice.direct_rooms().rooms.size(), 2);
    ASSERT_TRUE(until([&] { return !alice.direct_rooms().rooms.front().peer_connected; }));
}

/// @brief Old admission audit rows confer no current group replay access after identity resume.
TEST_F(RecoveryTest, GroupSyncRequiresCurrentMembershipAfterReconnect) {
    ChatClient alice, bob;
    const auto identity = login(alice, "alice");
    login(bob, "bob");
    const auto group = alice.create_group("Study").resource_id;
    ASSERT_TRUE(alice.send_group(group, "old member text").accepted);
    EXPECT_EQ(bob.sync(group).error, RequestError::history_forbidden);
    alice.close();
    ASSERT_TRUE(until([&] {
        alice.connect("127.0.0.1", port);
        return alice.resume_login("alice", identity.resume_token).accepted;
    }));
    EXPECT_EQ(alice.sync(group).error, RequestError::history_forbidden);
    ASSERT_TRUE(alice.join_group(group).accepted);
    ASSERT_EQ(alice.sync(group).records.size(), 1);
    EXPECT_EQ(alice.sync(group, 2).error, RequestError::sync_gap);
    EXPECT_EQ(alice.sync(group, 0, 2).error, RequestError::sync_gap);
}

/// @brief Missing retained rows are reported as an explicit gap rather than advancing a cursor.
TEST_F(RecoveryTest, DeletedSequenceReportsGapInsteadOfSkippingText) {
    ChatClient alice;
    login(alice, "alice");
    const auto group = alice.create_group("Study").resource_id;
    for (int n = 0; n < 3; ++n) {
        ASSERT_TRUE(alice.send_group(group, "text").accepted);
    }
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path().c_str(), &db), SQLITE_OK);
    const auto changed =
        sqlite3_exec(db, "DELETE FROM messages WHERE sequence=2", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    ASSERT_EQ(changed, SQLITE_OK);
    EXPECT_EQ(alice.sync(group).error, RequestError::sync_gap);
    const auto first = alice.sync(group, 0, 0, 1);
    ASSERT_TRUE(first.accepted);
    EXPECT_EQ(alice.sync(group, first.next_sequence, first.through_sequence, 1).error,
              RequestError::sync_gap);
}

/// @brief A lost committed ACK is retried with its original key; replay displays exactly one echo.
TEST_F(RecoveryTest, LostAckReplaysOneMessageAndCompletesOneRetriedCommand) {
    ChatClient bob;
    login(bob, "bob");
    chat::test::RecoveryProxy proxy(port);
    ChatWorker alice(limits());
    Observer observer;
    worker(alice, observer, proxy.port());
    proxy.drop_ack = true;
    const auto command = alice.submit(CommandKind::direct, "bob", "lost ACK text");
    ASSERT_TRUE(command);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.resumed == 1 && observer.result(*command);
    }));
    ASSERT_TRUE(proxy.dropped);
    const auto* result = observer.result(*command);
    ASSERT_TRUE(result->result.accepted);
    EXPECT_EQ(observer.completed.size(), 1);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return !observer.model.rooms().empty() && observer.model.rooms().front().lines.size() == 1;
    }));
    EXPECT_EQ(observer.model.rooms().front().lines.front().text, "lost ACK text");
    EXPECT_EQ(observer.model.rooms().front().last_sequence, 1);
    const auto history = bob.history(result->result.resource_id);
    ASSERT_EQ(history.records.size(), 1);
    EXPECT_EQ(history.records.front().message_id, result->result.message_id);
    EXPECT_TRUE(observer.terminal.empty());
}

/// @brief Offline group catch-up spans pages, preserves drafts, and never joins an unchosen group.
TEST_F(RecoveryTest, RejoinAndPagedCatchupPreserveDraftsAndGroupPrivacy) {
    ChatClient bob;
    login(bob, "bob");
    const auto chosen = bob.create_group("Chosen").resource_id;
    const auto other = bob.create_group("Not chosen").resource_id;
    chat::test::RecoveryProxy proxy(port);
    ChatWorker alice(limits());
    Observer observer;
    worker(alice, observer, proxy.port());
    const auto join = alice.submit(CommandKind::join_group, chosen);
    ASSERT_TRUE(join);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.result(*join) && observer.model.groups().size() == 2;
    }));
    observer.model.open_group(chosen);
    observer.model.draft("keep this unsent draft");
    ASSERT_TRUE(bob.send_group(chosen, "before loss").accepted);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.model.current()->last_sequence == 1;
    }));
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.reconnecting > 0;
    }));
    for (int n = 0; n < 120; ++n) {
        ASSERT_TRUE(bob.send_group(chosen, "missed " + std::to_string(n)).accepted);
    }
    ASSERT_TRUE(bob.send_group(other, "unrelated private text").accepted);
    proxy.paused = false;
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.resumed == 1;
    }));
    ASSERT_NE(observer.model.current(), nullptr);
    EXPECT_EQ(observer.model.current()->draft, "keep this unsent draft");
    EXPECT_EQ(observer.model.current()->last_sequence, 121);
    EXPECT_EQ(observer.model.current()->lines.size(), 100);
    EXPECT_EQ(observer.model.current()->lines.back().text, "missed 119");
    EXPECT_TRUE(observer.model.current()->joined);
    std::uint64_t applied = 0;
    for (const auto& event : observer.events) {
        if (const auto* message = std::get_if<chat::client::GroupDelivery>(&event)) {
            EXPECT_EQ(message->group_id, chosen);
            EXPECT_EQ(message->room_sequence, ++applied);
        }
    }
    EXPECT_EQ(applied, 121);
    observer.model.open_group(other);
    EXPECT_FALSE(observer.model.current()->joined);
    EXPECT_TRUE(observer.model.current()->lines.empty());
    EXPECT_TRUE(observer.terminal.empty());
}

/// @brief A live delivery after a skipped sequence first replays the gap in order.
TEST_F(RecoveryTest, LiveSequenceGapIsFilledBeforePublishingTheLaterDelivery) {
    ChatClient bob;
    login(bob, "bob");
    const auto group = bob.create_group("Study").resource_id;
    chat::test::RecoveryProxy proxy(port);
    ChatWorker alice(limits());
    Observer observer;
    worker(alice, observer, proxy.port());
    const auto join = alice.submit(CommandKind::join_group, group);
    ASSERT_TRUE(join);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.result(*join);
    }));
    proxy.skip_sequence = 1;
    ASSERT_TRUE(bob.send_group(group, "skipped live frame").accepted);
    ASSERT_TRUE(bob.send_group(group, "later frame").accepted);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return !observer.model.rooms().empty() && observer.model.rooms().front().last_sequence == 2;
    }));
    const auto& lines = observer.model.rooms().front().lines;
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.front().text, "skipped live frame");
    EXPECT_EQ(lines.back().text, "later frame");
    EXPECT_EQ(observer.resumed, 0);
}

/// @brief Server restart keeps the worker's durable pair identity and existing local transcript.
TEST_F(RecoveryTest, ServerRestartResumesIdentityWithoutClearingExistingTranscript) {
    ChatClient bob;
    const auto identity = login(bob, "bob");
    auto config = limits();
    config.reconnect_delay = 100ms;
    config.reconnect_max_delay = 100ms;
    ChatWorker alice(config);
    Observer observer;
    worker(alice, observer, port);
    const auto first = alice.submit(CommandKind::direct, "bob", "before restart");
    ASSERT_TRUE(first);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.result(*first) && !observer.model.rooms().empty() &&
               observer.model.rooms().front().last_sequence == 1;
    }));
    const auto room = observer.result(*first)->result.resource_id;
    stop();
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.reconnecting > 0;
    }));
    start();
    login(bob, "bob", identity.resume_token);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.resumed == 1;
    }));
    ASSERT_TRUE(bob.send_direct("alice", "after restart").accepted);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.model.rooms().front().last_sequence == 2;
    }));
    EXPECT_EQ(observer.model.rooms().size(), 1);
    EXPECT_EQ(observer.model.rooms().front().wire_id, room);
    EXPECT_EQ(observer.model.rooms().front().lines.size(), 2);
}

/// @brief Retry exhaustion is terminal and stop interrupts a long backoff without waiting it out.
TEST_F(RecoveryTest, ReconnectIsBoundedAndBackoffIsCancellable) {
    chat::test::RecoveryProxy proxy(port);
    auto config = limits();
    config.reconnect_attempts = 2;
    ChatWorker alice(config);
    Observer observer;
    worker(alice, observer, proxy.port());
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return !alice.running();
    }));
    observer.poll(alice);
    EXPECT_EQ(observer.reconnecting, 2);
    EXPECT_NE(observer.terminal.find("exhausted"), std::string::npos);
    proxy.paused = false;
    config.reconnect_delay = 5s;
    config.reconnect_max_delay = 5s;
    ChatWorker second(config);
    Observer next;
    worker(second, next, proxy.port());
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        next.poll(second);
        return next.reconnecting > 0;
    }));
    const auto before = std::chrono::steady_clock::now();
    second.disconnect();
    ASSERT_TRUE(until([&] { return !second.running(); }));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 300ms);
}

/// @brief A rejected resume never falls back to a fresh identity that could send to a namesake.
TEST_F(RecoveryTest, InvalidatedResumeTokenIsTerminalWithoutFreshLoginFallback) {
    chat::test::RecoveryProxy proxy(port);
    ChatWorker alice(limits());
    Observer observer;
    worker(alice, observer, proxy.port());
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(path().c_str(), &db), SQLITE_OK);
    const auto changed = sqlite3_exec(
        db, "UPDATE users SET token_hash='invalid' WHERE name='alice'", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    ASSERT_EQ(changed, SQLITE_OK);
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.reconnecting > 0;
    }));
    proxy.paused = false;
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return !alice.running();
    }));
    observer.poll(alice);
    EXPECT_NE(observer.terminal.find("Resume rejected"), std::string::npos);
    ChatClient replacement;
    EXPECT_TRUE(login(replacement, "alice").accepted);
}

/// @brief A worker cannot silently forget older room cursors after hitting its tracking bound.
TEST_F(RecoveryTest, RecoveryRoomOverflowEndsExplicitly) {
    ChatClient bob;
    login(bob, "bob");
    auto config = limits();
    config.recovery_rooms = 1;
    ChatWorker alice(config);
    Observer observer;
    worker(alice, observer, port);
    const auto first = alice.submit(CommandKind::create_group, "first");
    ASSERT_TRUE(first);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.result(*first);
    }));
    ASSERT_TRUE(alice.submit(CommandKind::direct, "bob", "second room"));
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return !alice.running();
    }));
    observer.poll(alice);
    EXPECT_NE(observer.terminal.find("Recovery room limit"), std::string::npos);
    EXPECT_EQ(observer.reconnecting, 0);
}

/// @brief Unknown group creation is reported once; recovery never creates a second group or joins
/// it.
TEST_F(RecoveryTest, UncertainGroupCreationIsNotBlindlyRetried) {
    chat::test::RecoveryProxy proxy(port);
    ChatWorker alice(limits());
    Observer observer;
    worker(alice, observer, proxy.port());
    proxy.drop_group_ack = true;
    const auto command = alice.submit(CommandKind::create_group, "one creation");
    ASSERT_TRUE(command);
    ASSERT_TRUE(until([&] {
        observer.poll(alice);
        return observer.resumed == 1 && observer.result(*command);
    }));
    ASSERT_TRUE(proxy.dropped);
    EXPECT_FALSE(observer.result(*command)->result.accepted);
    EXPECT_NE(observer.result(*command)->result.reason.find("outcome unknown"), std::string::npos);
    EXPECT_EQ(observer.completed.size(), 1);
    ASSERT_EQ(observer.model.groups().size(), 1);
    EXPECT_TRUE(observer.model.groups().front().members.empty());
}

/// @brief Unregistered, malformed, oversized, and storage-disabled replay requests fail safely.
TEST_F(RecoveryTest, SyncAndDirectoryValidateAuthorizationAndWireBounds) {
    ChatClient unregistered;
    unregistered.connect("127.0.0.1", port);
    EXPECT_EQ(unregistered.sync("direct-1").error, RequestError::not_logged_in);
    EXPECT_EQ(unregistered.direct_rooms().error, RequestError::not_logged_in);
    EXPECT_THROW(static_cast<void>(unregistered.sync("direct-1", 0, 0, 101)),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(unregistered.direct_rooms(0, 65)), std::invalid_argument);
    EXPECT_EQ(unregistered.ping(12), 12);
    chat::test::Peer raw(port);
    auto request = chat::test::ping(1);
    request.mutable_login_request()->set_username("raw");
    raw.send(chat::test::encode(request));
    ASSERT_EQ(raw.receive(1).messages.size(), 1);
    for (int scenario = 0; scenario < 4; ++scenario) {
        request = chat::test::ping(1);
        if (scenario == 0) {
            request.mutable_sync_request()->set_room_id("direct-1");
            request.mutable_sync_request()->set_limit(101);
        } else if (scenario == 1) {
            request.mutable_sync_request()->set_room_id("direct-1");
            request.mutable_sync_request()->set_after_sequence(
                std::numeric_limits<std::uint64_t>::max());
        } else if (scenario == 2) {
            request.mutable_direct_rooms_request()->set_limit(65);
        } else {
            request.mutable_direct_rooms_request()->set_after_room_id(
                std::numeric_limits<std::uint64_t>::max());
        }
        raw.send(chat::test::encode(request));
        const auto response = raw.receive(1);
        ASSERT_EQ(response.messages.size(), 1);
        EXPECT_EQ(response.messages.front().error().code(),
                  chat::protocol::ERROR_CODE_INVALID_REQUEST);
    }
    boost::asio::io_context ephemeral_context;
    chat::server::PingServer ephemeral(ephemeral_context,
                                       {boost::asio::ip::address_v4::loopback(), 0});
    ephemeral.start();
    std::jthread network_thread([&] { ephemeral_context.run(); });
    struct StopServer {
        chat::server::PingServer& server;
        ~StopServer() { server.stop(); }
    } stopping{ephemeral};
    ChatClient client;
    client.connect("127.0.0.1", ephemeral.port());
    ASSERT_TRUE(client.login("alice").accepted);
    EXPECT_EQ(client.sync("direct-1").error, RequestError::invalid_request);
    EXPECT_EQ(client.direct_rooms().error, RequestError::invalid_request);
    ephemeral.stop();
}

/// @brief Invalid sequence, watermark, type, and cursor responses close rather than reconnect.
TEST(RecoveryClientTest, MalformedSyncPagesAreProtocolFailures) {
    for (int scenario = 0; scenario < 6; ++scenario) {
        chat::test::Peer fake;
        chat::test::tcp::acceptor acceptor(fake.context,
                                           {boost::asio::ip::address_v4::loopback(), 0});
        ChatClient client;
        client.connect("127.0.0.1", acceptor.local_endpoint().port());
        acceptor.accept(fake.socket);
        auto responder = std::async(std::launch::async, [&] {
            const auto incoming = fake.receive(1);
            if (incoming.messages.size() != 1) {
                return false;
            }
            auto reply = incoming.messages.front();
            auto* page = reply.mutable_sync_response();
            page->set_room_id("direct-1");
            page->set_through_sequence(1);
            page->set_next_sequence(1);
            auto* record = page->add_entries();
            record->set_id(1);
            record->set_sender("alice");
            record->set_recipient("bob");
            record->set_text("text");
            record->set_message_id("message-1");
            record->set_room_sequence(1);
            switch (scenario) {
            case 0:
                record->set_room_sequence(2);
                break;
            case 1:
                page->set_next_sequence(0);
                break;
            case 2:
                page->set_through_sequence(0);
                break;
            case 3:
                page->set_has_more(true);
                break;
            case 4:
                page->set_group(true);
                break;
            case 5:
                page->set_room_id("direct-2");
                break;
            }
            fake.send(chat::test::encode(reply));
            return true;
        });
        try {
            static_cast<void>(client.sync("direct-1", 0, 0, 50, 400ms));
            ADD_FAILURE();
        } catch (const chat::client::NetworkError&) {
            ADD_FAILURE() << "protocol error was retryable";
        } catch (const std::runtime_error& error) {
            EXPECT_FALSE(std::string(error.what()).empty());
        }
        EXPECT_TRUE(responder.get());
        EXPECT_THROW(static_cast<void>(client.ping(1)), std::runtime_error);
    }
}

/// @brief Noncanonical, repeated, oversized, and stalled private directory pages are rejected.
TEST(RecoveryClientTest, MalformedDirectRoomPagesAreRejected) {
    for (int scenario = 0; scenario < 5; ++scenario) {
        chat::test::Peer fake;
        chat::test::tcp::acceptor acceptor(fake.context,
                                           {boost::asio::ip::address_v4::loopback(), 0});
        ChatClient client;
        client.connect("127.0.0.1", acceptor.local_endpoint().port());
        acceptor.accept(fake.socket);
        auto responder = std::async(std::launch::async, [&] {
            const auto incoming = fake.receive(1);
            if (incoming.messages.size() != 1) {
                return false;
            }
            auto reply = incoming.messages.front();
            auto* page = reply.mutable_direct_rooms_response();
            page->set_next_room_id(1);
            auto* room = page->add_rooms();
            room->set_room_id("direct-1");
            room->set_peer("bob");
            switch (scenario) {
            case 0:
                room->set_room_id("direct-01");
                break;
            case 1:
                room->set_peer("");
                break;
            case 2:
                page->set_next_room_id(2);
                break;
            case 3:
                *page->add_rooms() = *room;
                break;
            case 4:
                page->clear_rooms();
                page->set_next_room_id(0);
                page->set_has_more(true);
                break;
            }
            fake.send(chat::test::encode(reply));
            return true;
        });
        EXPECT_THROW(static_cast<void>(client.direct_rooms(0, 64, 400ms)), std::runtime_error);
        EXPECT_TRUE(responder.get());
    }
}
} // namespace
