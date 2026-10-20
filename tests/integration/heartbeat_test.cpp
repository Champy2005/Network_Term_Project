/** @file
 * @brief Verify periodic liveness, stale identity cleanup, and timer/socket lifetime cancellation.
 */
#include "chat/client/chat_client.hpp"
#include "chat/client/chat_worker.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"
#include <atomic>
#include <boost/asio/socket_base.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using boost::asio::ip::tcp;
using chat::client::ChatClient;
using chat::client::ChatWorker;
using chat::client::WorkerConnected;
using chat::client::WorkerDisconnected;
using chat::protocol::Envelope;
using chat::test::Peer;
template <class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
/// Server threads are always drained after stop; normal tests have a generous idle margin.
class RunningServer {
  public:
    explicit RunningServer(std::chrono::milliseconds timeout) {
        chat::server::SessionLimits limits;
        limits.idle_timeout = timeout;
        server = std::make_unique<chat::server::PingServer>(
            context, tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0}, ":memory:", limits);
        server->start();
        for (auto& thread : threads) {
            thread = std::thread([this] { context.run(); });
        }
    }
    ~RunningServer() {
        server->stop();
        for (auto& thread : threads) {
            thread.join();
        }
    }
    std::uint16_t port() const { return server->port(); }
    boost::asio::io_context context;
    std::unique_ptr<chat::server::PingServer> server;
    std::array<std::thread, 2> threads;
};
Envelope exchange(Peer& peer, const Envelope& request) {
    peer.send(chat::test::encode(request));
    const auto read = peer.receive(1);
    if (read.messages.size() != 1) {
        throw std::runtime_error("test response missing");
    }
    return read.messages[0];
}
void claim(Peer& peer, const std::string& username) {
    auto request = chat::test::ping(0, "login");
    request.mutable_login_request()->set_username(username);
    if (!exchange(peer, request).login_response().accepted()) {
        throw std::runtime_error("test login rejected");
    }
}
chat::client::WorkerLimits worker_limits() {
    chat::client::WorkerLimits limits;
    limits.heartbeat_interval = 20ms;
    limits.poll_timeout = 500ms; // Heartbeat scheduling must clamp long event polling.
    limits.operation_timeout = 200ms;
    return limits;
}
void start(ChatWorker& worker, std::uint16_t port, const std::string& username) {
    if (!worker.start("127.0.0.1", port, username)) {
        throw std::runtime_error("worker start failed");
    }
    if (!until([&] {
            for (const auto& update : worker.poll()) {
                if (std::holds_alternative<WorkerConnected>(update)) {
                    return true;
                }
            }
            return false;
        })) {
        throw std::runtime_error("worker login missing");
    }
}
void accept_login(Peer& fake) {
    const auto received = fake.receive(1);
    if (received.messages.size() != 1 || !received.messages[0].has_login_request()) {
        throw std::runtime_error("fake login missing");
    }
    auto reply = received.messages[0];
    reply.mutable_login_response()->set_accepted(true);
    reply.mutable_login_response()->set_username("alice");
    fake.send(chat::test::encode(reply));
}

/// @brief Idle membership/presence is removed while a heartbeat worker remains healthy; the name
/// can be reclaimed.
TEST(HeartbeatTest, ExpiredMemberPublishesCleanupAndReleasesItsName) {
    RunningServer server(400ms);
    ChatWorker alice(worker_limits());
    start(alice, server.port(), "alice");
    Peer bob(server.port());
    claim(bob, "bob");
    auto request = chat::test::ping(0, "create");
    request.mutable_create_group_request()->set_name("Study");
    const auto created = exchange(bob, request);
    ASSERT_TRUE(created.has_ack());
    ASSERT_TRUE(alice.submit(chat::client::CommandKind::join_group, created.ack().resource_id()));
    bool accepted = false, gone = false, membership_clean = false;
    ASSERT_TRUE(until([&] {
        for (const auto& update : alice.poll()) {
            if (const auto* done = std::get_if<chat::client::WorkerCompleted>(&update)) {
                accepted |= done->result.accepted;
            }
            if (const auto* event = std::get_if<chat::client::ClientEvent>(&update)) {
                if (const auto* users = std::get_if<chat::client::UserSnapshot>(event)) {
                    gone = users->usernames == std::vector<std::string>{"alice"};
                }
                if (const auto* groups = std::get_if<chat::client::GroupSnapshot>(event)) {
                    membership_clean =
                        groups->groups.size() == 1 &&
                        groups->groups[0].members == std::vector<std::string>{"alice"};
                }
            }
        }
        return accepted && gone && membership_clean;
    }));
    EXPECT_TRUE(alice.running());
    const auto closed = bob.receive(1, 1s);
    EXPECT_FALSE(closed.timed_out);
    EXPECT_TRUE(closed.error);
    EXPECT_TRUE(closed.messages.empty());
    Peer replacement(server.port());
    claim(replacement, "bob");
    EXPECT_TRUE(exchange(replacement, chat::test::ping(42)).has_pong());
    ASSERT_TRUE(replacement.groups);
    ASSERT_EQ(replacement.groups->groups_size(), 1);
    EXPECT_EQ(replacement.groups->groups(0).members_size(), 1);
    EXPECT_EQ(replacement.groups->groups(0).members(0), "alice");
}

/// @brief Unregistered idle sockets cannot hold all 64 connection slots indefinitely.
TEST(HeartbeatTest, ExpiredUnregisteredConnectionsFreeCapacity) {
    RunningServer server(800ms);
    std::vector<std::unique_ptr<Peer>> peers;
    peers.reserve(64);
    for (int i = 0; i < 64; ++i) {
        peers.push_back(std::make_unique<Peer>(server.port()));
    }
    Peer overflow(server.port());
    const auto refused = overflow.receive(1, 500ms);
    EXPECT_FALSE(refused.timed_out);
    EXPECT_TRUE(refused.error);
    const auto expired = peers[0]->receive(1, 2s);
    EXPECT_FALSE(expired.timed_out);
    EXPECT_TRUE(expired.error);
    // Wait for the deferred registry removal before claiming a recycled slot.
    bool recycled = false;
    ASSERT_TRUE(until([&] {
        try {
            ChatClient replacement;
            replacement.connect("127.0.0.1", server.port(), 200ms);
            recycled = replacement.ping(42, 200ms) == 42;
        } catch (const std::runtime_error&) {
        }
        return recycled;
    }));
}

/// @brief A byte trickle expires before it can finish one otherwise valid frame.
TEST(HeartbeatTest, PartialFrameBytesDoNotRefreshLiveness) {
    RunningServer server(400ms);
    Peer peer(server.port());
    const auto frame = chat::test::encode(chat::test::ping(42, "slow-frame"));
    bool expired = false;
    std::size_t sent = 0;
    for (; sent < frame.size(); ++sent) {
        boost::system::error_code error;
        boost::asio::write(peer.socket, boost::asio::buffer(frame.data() + sent, 1), error);
        const auto read = peer.receive(1, 80ms);
        ASSERT_TRUE(read.messages.empty());
        if (error || read.error == boost::asio::error::eof ||
            read.error == boost::asio::error::connection_reset) {
            expired = true;
            break;
        }
    }
    EXPECT_TRUE(expired);
    EXPECT_LT(sent, frame.size());
}

/// @brief Complete Pings extend activity across several original deadlines, then silence expires.
TEST(HeartbeatTest, CompletePingsRefreshTheSessionDeadline) {
    RunningServer server(400ms);
    Peer peer(server.port());
    claim(peer, "alice");
    for (std::uint64_t i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(150ms);
        const auto reply = exchange(peer, chat::test::ping(i, std::to_string(i)));
        ASSERT_TRUE(reply.has_pong());
        EXPECT_EQ(reply.pong().nonce(), i);
    }
    const auto read = peer.receive(1, 1s);
    EXPECT_FALSE(read.timed_out);
    EXPECT_TRUE(read.error);
}

/// @brief Ordinary complete chat requests also keep a client alive without separate Pings.
TEST(HeartbeatTest, CompleteChatRequestsRefreshLiveness) {
    RunningServer server(400ms);
    ChatClient client;
    client.connect("127.0.0.1", server.port());
    ASSERT_TRUE(client.login("alice").accepted);
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(150ms);
        EXPECT_TRUE(client.send_direct("alice", "active request").accepted);
    }
}

/// @brief Receiving many outgoing deliveries cannot keep a peer alive when it sends no requests.
TEST(HeartbeatTest, OutboundTrafficDoesNotRefreshPeerActivity) {
    RunningServer server(400ms);
    Peer alice(server.port());
    claim(alice, "alice");
    ChatClient bob;
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    bool removed = false;
    int delivered = 0;
    ASSERT_TRUE(until([&] {
        const auto result = bob.send_direct("alice", "outbound traffic");
        if (!result.accepted) {
            removed = result.error == chat::client::RequestError::user_not_found;
            return removed;
        }
        const auto read = alice.receive(1, 100ms);
        if (!read.messages.empty()) {
            EXPECT_TRUE(read.messages[0].has_direct_message());
            ++delivered;
        }
        std::this_thread::sleep_for(50ms);
        return false;
    }));
    EXPECT_GE(delivered, 2);
    EXPECT_EQ(bob.ping(99), 99);
}

/// @brief Idle expiry cancels a pending large write/read and drains all shared timer ownership.
TEST(HeartbeatTest, ExpiryCancelsPendingSocketWorkAndReleasesSession) {
    boost::asio::io_context context;
    tcp::acceptor acceptor(context, {boost::asio::ip::address_v4::loopback(), 0});
    Peer peer(acceptor.local_endpoint().port());
    tcp::socket socket(context);
    acceptor.accept(socket);
    socket.set_option(boost::asio::socket_base::send_buffer_size(1024));
    peer.socket.set_option(boost::asio::socket_base::receive_buffer_size(1024));
    chat::server::SessionLimits limits;
    limits.idle_timeout = 80ms;
    std::atomic<int> closed{0};
    auto session = std::make_shared<chat::server::ClientSession>(
        std::move(socket), boost::asio::make_strand(context),
        [](chat::server::ClientSession&, const Envelope&) {},
        [&closed](chat::server::ClientSession&) { ++closed; }, limits);
    std::weak_ptr<chat::server::ClientSession> weak = session;
    session->start();
    auto large = chat::test::ping(1);
    large.mutable_error()->set_message(std::string(800000, 'x'));
    ASSERT_TRUE(session->send(large));
    session.reset();
    auto run = std::async(std::launch::async, [&] { context.run(); });
    const auto ready = run.wait_for(2s);
    EXPECT_EQ(ready, std::future_status::ready);
    if (auto retained = weak.lock()) {
        retained->stop();
    }
    run.get();
    EXPECT_EQ(closed.load(), 1);
    EXPECT_TRUE(weak.expired());
}

/// @brief Explicit stop cancels even a long idle timer exactly once without retaining the context.
TEST(HeartbeatTest, StopCancelsIdleTimerAndCloseCallbackRunsOnce) {
    boost::asio::io_context context;
    tcp::acceptor acceptor(context, {boost::asio::ip::address_v4::loopback(), 0});
    Peer peer(acceptor.local_endpoint().port());
    tcp::socket socket(context);
    acceptor.accept(socket);
    std::atomic<int> closed{0};
    auto session = std::make_shared<chat::server::ClientSession>(
        std::move(socket), boost::asio::make_strand(context),
        [](chat::server::ClientSession&, const Envelope&) {},
        [&closed](chat::server::ClientSession&) { ++closed; });
    std::weak_ptr<chat::server::ClientSession> weak = session;
    session->start();
    session->stop();
    session->stop();
    session.reset();
    auto run = std::async(std::launch::async, [&] { context.run(); });
    EXPECT_EQ(run.wait_for(1s), std::future_status::ready);
    run.get();
    EXPECT_EQ(closed.load(), 1);
    EXPECT_TRUE(weak.expired());
}

/// @brief Long event polls cannot suppress periodic Ping; replies remain internal to the worker.
TEST(HeartbeatWorkerTest, PeriodicPingsAreCorrelatedAndNotPublishedAsChatUpdates) {
    Peer fake;
    tcp::acceptor acceptor(fake.context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatWorker worker(worker_limits());
    ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    accept_login(fake);
    std::uint64_t nonce = 0;
    std::string last_request;
    for (int i = 0; i < 3; ++i) {
        const auto read = fake.receive(1, 400ms);
        ASSERT_EQ(read.messages.size(), 1);
        ASSERT_TRUE(read.messages[0].has_ping());
        EXPECT_GT(read.messages[0].ping().nonce(), nonce);
        nonce = read.messages[0].ping().nonce();
        EXPECT_NE(read.messages[0].request_id(), last_request);
        last_request = read.messages[0].request_id();
        auto reply = read.messages[0];
        reply.mutable_pong()->set_nonce(nonce);
        fake.send(chat::test::encode(reply));
        for (const auto& update : worker.poll()) {
            EXPECT_TRUE(std::holds_alternative<WorkerConnected>(update));
        }
    }
    worker.disconnect();
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    const auto updates = worker.poll();
    ASSERT_FALSE(updates.empty());
    EXPECT_TRUE(std::holds_alternative<WorkerDisconnected>(updates.back()));
}

/// @brief Stop during a silent heartbeat waits only for that operation and skips queued commands.
TEST(HeartbeatWorkerTest, DisconnectDuringHeartbeatDoesNotStartAnotherOperation) {
    Peer fake;
    tcp::acceptor acceptor(fake.context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatWorker worker(worker_limits());
    ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    accept_login(fake);
    const auto read = fake.receive(1, 400ms);
    ASSERT_EQ(read.messages.size(), 1);
    ASSERT_TRUE(read.messages[0].has_ping());
    ASSERT_TRUE(worker.submit(chat::client::CommandKind::create_group, "queued"));
    const auto before = std::chrono::steady_clock::now();
    worker.disconnect();
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 450ms);
    const auto trailing = fake.receive(1, 400ms);
    EXPECT_TRUE(trailing.messages.empty());
    EXPECT_FALSE(trailing.timed_out);
    for (const auto& update : worker.poll()) {
        EXPECT_FALSE(std::holds_alternative<chat::client::WorkerCompleted>(update));
    }
}

/// @brief A missing/mismatched Pong closes the worker and discards commands queued behind it.
TEST(HeartbeatWorkerTest, MissingAndWrongPongsPublishTerminalFailure) {
    for (bool wrong : {false, true}) {
        Peer fake;
        tcp::acceptor acceptor(fake.context, {boost::asio::ip::address_v4::loopback(), 0});
        ChatWorker worker(worker_limits());
        ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
        acceptor.accept(fake.socket);
        accept_login(fake);
        const auto read = fake.receive(1, 400ms);
        ASSERT_EQ(read.messages.size(), 1);
        ASSERT_TRUE(read.messages[0].has_ping());
        ASSERT_TRUE(worker.submit(chat::client::CommandKind::create_group, "queued"));
        if (wrong) {
            auto reply = read.messages[0];
            reply.mutable_pong()->set_nonce(read.messages[0].ping().nonce() + 1);
            fake.send(chat::test::encode(reply));
        }
        ASSERT_TRUE(until([&] { return !worker.running(); }));
        const auto updates = worker.poll();
        ASSERT_FALSE(updates.empty());
        const auto& reason = std::get<WorkerDisconnected>(updates.back()).reason;
        EXPECT_EQ(reason.find("Heartbeat failed:"), 0);
        EXPECT_NE(reason.find(wrong ? "Pong" : "timed out"), std::string::npos);
        EXPECT_FALSE(worker.submit(chat::client::CommandKind::create_group, "after failure"));
        for (const auto& update : updates) {
            EXPECT_FALSE(std::holds_alternative<chat::client::WorkerCompleted>(update));
        }
    }
}

/// @brief Sustained command/snapshot traffic does not postpone the periodic liveness exchange.
TEST(HeartbeatWorkerTest, CommandTrafficDoesNotStarveHeartbeats) {
    Peer fake;
    tcp::acceptor acceptor(fake.context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatWorker worker(worker_limits());
    ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    accept_login(fake);
    ASSERT_TRUE(until([&] {
        for (const auto& update : worker.poll()) {
            if (std::holds_alternative<WorkerConnected>(update)) {
                return true;
            }
        }
        return false;
    }));
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(worker.submit(chat::client::CommandKind::create_group, "queued"));
    }
    auto responder = std::async(std::launch::async, [&] {
        int done = 0, pings = 0;
        while (done < 20) {
            const auto read = fake.receive(1, 500ms);
            if (read.messages.size() != 1) {
                return -1;
            }
            auto reply = read.messages[0];
            if (reply.has_ping()) {
                ++pings;
                reply.mutable_pong()->set_nonce(read.messages[0].ping().nonce());
            } else {
                std::this_thread::sleep_for(10ms);
                ++done;
                reply.mutable_ack()->set_resource_id("group-" + std::to_string(done));
            }
            auto bytes = chat::test::encode(reply);
            Envelope snapshot;
            snapshot.set_protocol_version(1);
            snapshot.mutable_user_list_update()->add_usernames("alice");
            const auto event = chat::test::encode(snapshot);
            bytes.insert(bytes.end(), event.begin(), event.end());
            fake.send(bytes);
        }
        return pings;
    });
    int completed = 0;
    EXPECT_TRUE(until([&] {
        for (const auto& update : worker.poll()) {
            if (const auto* done = std::get_if<chat::client::WorkerCompleted>(&update)) {
                EXPECT_TRUE(done->result.accepted);
                ++completed;
            }
        }
        return completed == 20;
    }));
    EXPECT_GE(responder.get(), 3);
    worker.disconnect();
    EXPECT_TRUE(until([&] { return !worker.running(); }));
}

/// @brief Zero explicitly disables automatic heartbeat; zero server idle budget keeps silent peers
/// open.
TEST(HeartbeatWorkerTest, DisablingHeartbeatAndIdleExpiryIsExplicit) {
    RunningServer server(0ms);
    auto limits = worker_limits();
    limits.heartbeat_interval = 0ms;
    ChatWorker worker(limits);
    start(worker, server.port(), "alice");
    std::this_thread::sleep_for(450ms);
    EXPECT_TRUE(worker.running());
    Peer fake;
    tcp::acceptor acceptor(fake.context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatWorker no_ping(limits);
    ASSERT_TRUE(no_ping.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    accept_login(fake);
    EXPECT_TRUE(fake.receive(1, 120ms).timed_out);
    no_ping.disconnect();
    worker.disconnect();
    EXPECT_TRUE(until([&] { return !no_ping.running() && !worker.running(); }));
}

/// @brief Negative/overflow-prone timer budgets fail construction before accepting connections.
TEST(HeartbeatTest, InvalidTimerBudgetsAreRejected) {
    boost::asio::io_context context;
    for (const auto timeout : {-1ms, std::chrono::milliseconds(std::chrono::hours(25))}) {
        chat::server::SessionLimits limits;
        limits.idle_timeout = timeout;
        EXPECT_THROW(chat::server::PingServer invalid(
                         context, {boost::asio::ip::address_v4::loopback(), 0}, {}, limits),
                     std::invalid_argument);
        auto worker = worker_limits();
        worker.heartbeat_interval = timeout;
        EXPECT_THROW(ChatWorker invalid(worker), std::invalid_argument);
    }
}
} // namespace
