/** @file
 * @brief Exercise the GUI-independent worker against real servers, including privacy and overload.
 */
#include "chat/client/chat_client.hpp"
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"
#include <gtest/gtest.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using chat::client::ChatWorker;
using chat::client::CommandKind;
using chat::client::WorkerCompleted;
using chat::client::WorkerDisconnected;
using chat::client::WorkerUpdate;
template <class Predicate> bool until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
class WorkerTest : public testing::Test {
  protected:
    WorkerTest()
        : server(context, {boost::asio::ip::address_v4::loopback(), 0}),
          network([this] { context.run(); }) {
        server.start();
    }
    ~WorkerTest() override {
        server.stop();
        guard.reset();
        network.join();
    }
    void start(ChatWorker& worker, const std::string& name) {
        ASSERT_TRUE(worker.start("127.0.0.1", server.port(), name));
        ASSERT_TRUE(until([&] {
            for (const auto& update : worker.poll()) {
                if (std::holds_alternative<chat::client::WorkerConnected>(update)) {
                    return true;
                }
            }
            return false;
        }));
    }
    std::optional<WorkerCompleted> result(ChatWorker& worker, std::uint64_t id,
                                          chat::client::RoomModel* model = nullptr) {
        std::optional<WorkerCompleted> found;
        until([&] {
            for (const auto& update : worker.poll()) {
                if (const auto* completed = std::get_if<WorkerCompleted>(&update);
                    completed && completed->id == id) {
                    found = *completed;
                }
                if (model) {
                    if (const auto* event = std::get_if<chat::client::ClientEvent>(&update)) {
                        model->apply(*event);
                    }
                }
            }
            return found.has_value();
        });
        return found;
    }
    boost::asio::io_context context;
    chat::server::PingServer server;
    // Keep the context alive before starting the acceptor on the test thread.
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{
        context.get_executor()};
    std::thread network;
};
/// @brief Worker text reaches both participants; the third client receives no direct delivery.
TEST_F(WorkerTest, DirectCommandsAndEventsPreserveThreeClientPrivacy) {
    chat::client::ChatClient bob, charlie;
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    charlie.connect("127.0.0.1", server.port());
    ASSERT_TRUE(charlie.login("charlie").accepted);
    ChatWorker alice;
    start(alice, "alice");
    chat::client::RoomModel model;
    model.reset("alice");
    const auto id = alice.submit(CommandKind::direct, "bob", "worker text");
    ASSERT_TRUE(id);
    const auto accepted = result(alice, *id, &model);
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(accepted->result.accepted);
    ASSERT_TRUE(until([&] {
        for (const auto& update : alice.poll()) {
            if (const auto* event = std::get_if<chat::client::ClientEvent>(&update)) {
                model.apply(*event);
            }
        }
        return !model.rooms().empty() && !model.rooms().front().lines.empty();
    }));
    bool received = false;
    ASSERT_TRUE(until([&] {
        if (auto event = bob.next_event(20ms)) {
            if (auto* direct = std::get_if<chat::client::DirectDelivery>(&*event)) {
                received = direct->text == "worker text";
            }
        }
        return received;
    }));
    const auto end = std::chrono::steady_clock::now() + 100ms;
    do {
        if (auto event = charlie.next_event(10ms)) {
            EXPECT_FALSE(std::holds_alternative<chat::client::DirectDelivery>(*event));
        }
    } while (std::chrono::steady_clock::now() < end);
    EXPECT_EQ(model.rooms().front().lines.size(), 1);
}
/// @brief Worker operations expose discovery, creator membership, voluntary join, and server
/// rejection.
TEST_F(WorkerTest, GroupsRequireServerConfirmedMembership) {
    ChatWorker alice, bob;
    start(alice, "alice");
    start(bob, "bob");
    const auto create = alice.submit(CommandKind::create_group, "Study");
    ASSERT_TRUE(create);
    const auto created = result(alice, *create);
    ASSERT_TRUE(created);
    ASSERT_TRUE(created->result.accepted);
    const auto group = created->result.resource_id;
    const auto invalid = bob.submit(CommandKind::group, group, "not a member");
    ASSERT_TRUE(invalid);
    const auto denied = result(bob, *invalid);
    ASSERT_TRUE(denied);
    EXPECT_EQ(denied->result.error, chat::client::RequestError::not_group_member);
    const auto join = bob.submit(CommandKind::join_group, group);
    ASSERT_TRUE(join);
    const auto joined = result(bob, *join);
    ASSERT_TRUE(joined);
    EXPECT_TRUE(joined->result.accepted);
    const auto send = bob.submit(CommandKind::group, group, "joined text");
    ASSERT_TRUE(send);
    const auto sent = result(bob, *send);
    ASSERT_TRUE(sent);
    EXPECT_TRUE(sent->result.accepted);
}
/// @brief A rejected duplicate login ends cleanly and allows another explicit connection.
TEST_F(WorkerTest, LoginRejectionAndExplicitReconnectHaveTerminalUpdates) {
    ChatWorker first, second;
    start(first, "alice");
    ASSERT_TRUE(second.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] { return !second.running(); }));
    auto updates = second.poll();
    ASSERT_EQ(updates.size(), 1);
    EXPECT_NE(std::get<WorkerDisconnected>(updates.front()).reason.find("Login rejected"),
              std::string::npos);
    start(second, "bob");
    second.disconnect();
    ASSERT_TRUE(until([&] { return !second.running(); }));
    EXPECT_FALSE(second.submit(CommandKind::direct, "alice", "after stop"));
}
/// @brief Update count overflow closes the connection and preserves a terminal reason outside the
/// full queue.
TEST_F(WorkerTest, UpdateCountOverflowIsReportedWithoutSilentLoss) {
    chat::client::WorkerLimits limits;
    limits.updates = 1;
    ChatWorker worker(limits);
    ASSERT_TRUE(worker.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    const auto updates = worker.poll();
    ASSERT_EQ(updates.size(), 2);
    EXPECT_TRUE(std::holds_alternative<chat::client::WorkerConnected>(updates.front()));
    EXPECT_NE(std::get<WorkerDisconnected>(updates.back()).reason.find("queue limit"),
              std::string::npos);
}
/// @brief Update byte admission is enforced independently of the update count.
TEST_F(WorkerTest, UpdateByteOverflowIsReported) {
    chat::client::WorkerLimits limits;
    limits.update_bytes = 1;
    ChatWorker worker(limits);
    ASSERT_TRUE(worker.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    const auto updates = worker.poll();
    ASSERT_EQ(updates.size(), 1);
    EXPECT_NE(std::get<WorkerDisconnected>(updates.back()).reason.find("queue limit"),
              std::string::npos);
}
/// @brief Missing connections, zero limits, and oversized command text fail before admission.
TEST_F(WorkerTest, CommandValidationAndDisconnectedAdmissionAreBounded) {
    ChatWorker worker;
    EXPECT_FALSE(worker.submit(CommandKind::direct, "bob", "hello"));
    EXPECT_FALSE(worker.start("", server.port(), "alice"));
    chat::client::WorkerLimits limits;
    limits.commands = 0;
    EXPECT_THROW(ChatWorker invalid(limits), std::invalid_argument);
    start(worker, "alice");
    EXPECT_FALSE(worker.submit(CommandKind::direct, "bob", std::string(4097, 'x')));
    EXPECT_FALSE(worker.submit(CommandKind::direct, "bob", ""));
    EXPECT_FALSE(worker.submit(CommandKind::create_group, std::string(65, 'x')));
}
/// @brief Connect/timeout run off the caller thread and destruction drains pending operations
/// safely.
TEST_F(WorkerTest, SilentServerDoesNotBlockTheCallerAndStopHasABound) {
    boost::asio::io_context silent_context;
    boost::asio::ip::tcp::acceptor acceptor(silent_context,
                                            {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::WorkerLimits limits;
    limits.operation_timeout = 150ms;
    ChatWorker worker(limits);
    const auto before = std::chrono::steady_clock::now();
    ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 100ms);
    boost::asio::ip::tcp::socket socket(silent_context);
    acceptor.accept(socket);
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 1s);
    const auto updates = worker.poll();
    ASSERT_EQ(updates.size(), 1);
    EXPECT_NE(std::get<WorkerDisconnected>(updates.front()).reason.find("timed out"),
              std::string::npos);
}
/// @brief An in-flight exchange cannot bypass the queued command limit; stop stays bounded.
TEST_F(WorkerTest, PendingCommandCountIsBoundedDuringAnActiveExchange) {
    chat::test::Peer fake;
    boost::asio::ip::tcp::acceptor acceptor(fake.context,
                                            {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::WorkerLimits limits;
    limits.commands = 1;
    limits.operation_timeout = 250ms;
    ChatWorker worker(limits);
    ASSERT_TRUE(worker.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    const auto login = fake.receive(1);
    ASSERT_EQ(login.messages.size(), 1);
    chat::protocol::Envelope reply;
    reply.set_protocol_version(1);
    reply.set_request_id(login.messages[0].request_id());
    reply.mutable_login_response()->set_accepted(true);
    reply.mutable_login_response()->set_username("alice");
    fake.send(chat::test::encode(reply));
    bool connected = false;
    ASSERT_TRUE(until([&] {
        for (const auto& update : worker.poll()) {
            connected |= std::holds_alternative<chat::client::WorkerConnected>(update);
        }
        return connected;
    }));
    ASSERT_TRUE(worker.submit(CommandKind::create_group, "first"));
    ASSERT_EQ(fake.receive(1).messages.size(), 1);
    ASSERT_TRUE(worker.submit(CommandKind::create_group, "queued"));
    EXPECT_FALSE(worker.submit(CommandKind::create_group, "overflow"));
    worker.disconnect();
    ASSERT_TRUE(until([&] { return !worker.running(); }));
    const auto updates = worker.poll();
    ASSERT_FALSE(updates.empty());
    EXPECT_TRUE(std::holds_alternative<WorkerDisconnected>(updates.back()));
}
} // namespace
