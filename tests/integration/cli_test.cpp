/** @file
 * @brief Exercise CLI command/event presentation against real TLS sockets with three-client
 * privacy.
 */
#include "chat/server/ping_server.hpp"
#include "chat_console.hpp"
#include "recovery_peer.hpp"
#include "tls_certificates.hpp"
#include <gtest/gtest.h>
#include <sstream>
#include <thread>

namespace {
using namespace std::chrono_literals;
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
class CliTest : public testing::Test {
  protected:
    CliTest()
        : server(context, {boost::asio::ip::address_v4::loopback(), 0}, ":memory:", {},
                 certificates.server()),
          network([this] { context.run(); }) {
        server.start();
    }
    ~CliTest() override {
        server.stop();
        guard.reset();
        network.join();
    }
    void start(chat::cli::ChatConsole& console, const std::string& name) {
        ASSERT_TRUE(console.start("127.0.0.1", server.port(), name));
        ASSERT_TRUE(until([&] {
            console.poll();
            return console.ready();
        }));
    }
    void stop(chat::cli::ChatConsole& console) {
        console.request_exit();
        ASSERT_TRUE(until([&] {
            console.poll();
            return console.finished();
        }));
    }
    std::string create(chat::cli::ChatConsole& console, std::ostringstream& output) {
        console.execute({"/create Study group"});
        EXPECT_TRUE(until([&] {
            console.poll();
            return output.str().find("OK 1 create room=\"") != std::string::npos;
        }));
        const auto text = output.str();
        const auto start = text.find("OK 1 create room=\"");
        if (start == std::string::npos) {
            return {};
        }
        const auto first = start + std::string_view("OK 1 create room=\"").size();
        return text.substr(first, text.find('"', first) - first);
    }
    chat::test::TlsCertificates certificates;
    boost::asio::io_context context;
    chat::server::PingServer server;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{
        context.get_executor()};
    std::thread network;
};
/// @brief CLI core DM and user-list flows deliver only to their participants over verified TLS.
TEST_F(CliTest, DirectMessagesAndSnapshotsPreserveThreeClientPrivacy) {
    std::ostringstream a, b, c;
    chat::cli::ChatConsole alice(a, {}, certificates.client()), bob(b, {}, certificates.client()),
        charlie(c, {}, certificates.client());
    start(alice, "alice");
    start(bob, "bob");
    start(charlie, "charlie");
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return a.str().find("\"charlie\"") != std::string::npos;
    }));
    alice.execute({"/users"});
    alice.execute({"/dm bob private_cli_text"});
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return a.str().find("OK 1 dm") != std::string::npos &&
               b.str().find("private_cli_text") != std::string::npos;
    }));
    const auto deadline = std::chrono::steady_clock::now() + 80ms;
    while (std::chrono::steady_clock::now() < deadline) {
        alice.poll();
        charlie.poll();
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_NE(a.str().find("private_cli_text"), std::string::npos);
    EXPECT_EQ(c.str().find("private_cli_text"), std::string::npos);
    stop(alice);
    stop(bob);
    stop(charlie);
    EXPECT_EQ(alice.exit_code(), 0);
}
/// @brief Group creation exposes creator-only membership and opaque IDs to all registered CLI
/// users.
TEST_F(CliTest, CreateDiscoveryAndMemberViews) {
    std::ostringstream a, b;
    chat::cli::ChatConsole alice(a, {}, certificates.client()), bob(b, {}, certificates.client());
    start(alice, "alice");
    start(bob, "bob");
    const auto group = create(alice, a);
    ASSERT_FALSE(group.empty());
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        return b.str().find("Study group") != std::string::npos;
    }));
    bob.execute({"/groups"});
    bob.execute({"/members " + group});
    EXPECT_NE(b.str().find("MEMBERS \"" + group + "\" \"alice\"\n"), std::string::npos);
    stop(alice);
    stop(bob);
}
/// @brief Joining is voluntary; the server rejects nonmember sends and keeps Charlie's CLI quiet.
TEST_F(CliTest, JoinAndGroupSendUseServerAuthorization) {
    std::ostringstream a, b, c;
    chat::cli::ChatConsole alice(a, {}, certificates.client()), bob(b, {}, certificates.client()),
        charlie(c, {}, certificates.client());
    start(alice, "alice");
    start(bob, "bob");
    start(charlie, "charlie");
    const auto group = create(alice, a);
    ASSERT_FALSE(group.empty());
    bob.execute({"/group " + group + " denied_cli_text"});
    ASSERT_TRUE(until([&] {
        bob.poll();
        return b.str().find("REJECTED 1 group code=7") != std::string::npos;
    }));
    bob.execute({"/join " + group});
    ASSERT_TRUE(until([&] {
        bob.poll();
        return b.str().find("OK 2 join") != std::string::npos;
    }));
    bob.execute({"/group " + group + " joined_cli_text"});
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return a.str().find("joined_cli_text") != std::string::npos &&
               b.str().find("OK 3 group") != std::string::npos;
    }));
    const auto deadline = std::chrono::steady_clock::now() + 80ms;
    while (std::chrono::steady_clock::now() < deadline) {
        charlie.poll();
        std::this_thread::sleep_for(1ms);
    }
    EXPECT_EQ(a.str().find("denied_cli_text"), std::string::npos);
    EXPECT_EQ(c.str().find("joined_cli_text"), std::string::npos);
    stop(alice);
    stop(bob);
    stop(charlie);
    EXPECT_EQ(bob.exit_code(), 1);
}
/// @brief Duplicate-name login terminates cleanly and never produces a connected status.
TEST_F(CliTest, LoginRejectionEndsWithoutInput) {
    std::ostringstream a, b;
    chat::cli::ChatConsole alice(a, {}, certificates.client()),
        duplicate(b, {}, certificates.client());
    start(alice, "alice");
    ASSERT_TRUE(duplicate.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] {
        duplicate.poll();
        return duplicate.finished();
    }));
    EXPECT_EQ(duplicate.exit_code(), 1);
    EXPECT_EQ(b.str().find("CONNECTED \"alice\""), std::string::npos);
    stop(alice);
}
/// @brief Invalid TLS identity terminates the CLI with no login and no development fallback.
TEST_F(CliTest, CertificateFailureIsTerminal) {
    std::ostringstream output;
    chat::cli::ChatConsole console(output, {}, certificates.client("wrong.example"));
    ASSERT_TRUE(console.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.finished();
    }));
    EXPECT_EQ(console.exit_code(), 1);
    EXPECT_NE(output.str().find("TLS"), std::string::npos);
    EXPECT_EQ(output.str().find("CONNECTED \"alice\""), std::string::npos);
}
/// @brief Quit drains submitted receipts and ignores later input rather than silently sending it.
TEST_F(CliTest, QuitDrainsAdmittedCommands) {
    std::ostringstream output;
    chat::cli::ChatConsole console(output, {}, certificates.client());
    start(console, "alice");
    console.execute({"/dm alice before_quit"});
    console.execute({"/quit"});
    console.execute({"/create after_quit"});
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.finished();
    }));
    EXPECT_NE(output.str().find("OK 1 dm"), std::string::npos);
    EXPECT_EQ(output.str().find("QUEUED 2"), std::string::npos);
    EXPECT_EQ(console.exit_code(), 0);
}
/// @brief Local malformed/oversized input reports bounded errors and later commands remain usable.
TEST_F(CliTest, InvalidInputRecoversWithoutNetworkSideEffects) {
    std::ostringstream output;
    chat::cli::ChatConsole console(output, {}, certificates.client());
    start(console, "alice");
    console.execute({"/dm alice unseen", true});
    console.execute({"/unknown"});
    console.execute({"/members missing"});
    console.execute({"/dm alice valid_after_error"});
    ASSERT_TRUE(until([&] {
        console.poll();
        return output.str().find("OK 1 dm") != std::string::npos;
    }));
    EXPECT_EQ(output.str().find("unseen"), std::string::npos);
    EXPECT_NE(output.str().find("ERROR"), std::string::npos);
    stop(console);
    EXPECT_EQ(console.exit_code(), 1);
    EXPECT_FALSE(console.start("127.0.0.1", server.port(), "fresh"));
}
/// @brief An out-of-range public enum cannot become an uninitialized worker action or a delivery.
TEST_F(CliTest, WorkerRejectsUnknownCommandKinds) {
    chat::client::ChatWorker worker({}, certificates.client());
    ASSERT_TRUE(worker.start("127.0.0.1", server.port(), "alice"));
    ASSERT_TRUE(until([&] {
        for (const auto& update : worker.poll()) {
            if (std::holds_alternative<chat::client::WorkerConnected>(update)) {
                return true;
            }
        }
        return false;
    }));
    EXPECT_FALSE(worker.submit(static_cast<chat::client::CommandKind>(255), "alice", "not sent"));
    ASSERT_TRUE(worker.submit(chat::client::CommandKind::direct, "alice", "valid"));
    ASSERT_TRUE(until([&] {
        for (const auto& update : worker.poll()) {
            if (const auto* done = std::get_if<chat::client::WorkerCompleted>(&update)) {
                return done->result.accepted;
            }
        }
        return false;
    }));
    worker.disconnect();
}
/// @brief TLS interruption replays missed group text into the CLI once under the same identity.
TEST_F(CliTest, EncryptedRecoveryReplaysIntoConsole) {
    chat::test::RecoveryProxy proxy(server.port(), true);
    std::ostringstream output;
    chat::client::WorkerLimits limits;
    limits.reconnect_delay = 20ms;
    limits.reconnect_max_delay = 40ms;
    chat::cli::ChatConsole console(output, limits, certificates.client());
    ASSERT_TRUE(console.start("127.0.0.1", proxy.port(), "alice"));
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.ready();
    }));
    const auto group = create(console, output);
    ASSERT_FALSE(group.empty());
    chat::client::ChatClient bob(certificates.client());
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    ASSERT_TRUE(bob.join_group(group).accepted);
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        console.poll();
        return output.str().find("RECONNECTING") != std::string::npos;
    }));
    ASSERT_TRUE(bob.send_group(group, "missed_cli_text").accepted);
    proxy.paused = false;
    ASSERT_TRUE(until([&] {
        console.poll();
        return output.str().find("RECONNECTED \"alice\"") != std::string::npos;
    }));
    const auto text = output.str();
    const auto first = text.find("missed_cli_text");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(text.find("missed_cli_text", first + 1), std::string::npos);
    console.execute({"/group " + group + " after_recovery"});
    ASSERT_TRUE(until([&] {
        console.poll();
        return output.str().find("OK 2 group") != std::string::npos;
    }));
    stop(console);
    EXPECT_EQ(console.exit_code(), 0);
}
/// @brief Quit interrupts recovery backoff promptly even while no input/network bytes are ready.
TEST_F(CliTest, QuitDuringRecoveryDoesNotWaitForStdinOrBackoff) {
    chat::test::RecoveryProxy proxy(server.port(), true);
    std::ostringstream output;
    chat::client::WorkerLimits limits;
    limits.reconnect_delay = 1500ms;
    limits.reconnect_max_delay = 1500ms;
    chat::cli::ChatConsole console(output, limits, certificates.client());
    ASSERT_TRUE(console.start("127.0.0.1", proxy.port(), "alice"));
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.ready();
    }));
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        console.poll();
        return output.str().find("RECONNECTING") != std::string::npos;
    }));
    const auto before = std::chrono::steady_clock::now();
    console.execute({"/quit"});
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.finished();
    }));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 700ms);
    EXPECT_EQ(console.exit_code(), 0);
}
} // namespace

/// @brief Exit's drain deadline cancels uncertain work and joins only the bounded active exchange.
TEST(CliExitTest, SilentOperationHasBoundedQuitDrain) {
    boost::asio::io_context context;
    chat::test::tcp::acceptor acceptor(context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::test::Peer fake;
    std::ostringstream output;
    chat::client::WorkerLimits limits;
    limits.operation_timeout = 150ms;
    chat::cli::ChatConsole console(output, limits, {}, 20ms);
    ASSERT_TRUE(console.start("127.0.0.1", acceptor.local_endpoint().port(), "alice"));
    acceptor.accept(fake.socket);
    const auto login = fake.receive(1);
    ASSERT_EQ(login.messages.size(), 1);
    chat::protocol::Envelope reply;
    reply.set_protocol_version(1);
    reply.set_request_id(login.messages[0].request_id());
    reply.mutable_login_response()->set_accepted(true);
    reply.mutable_login_response()->set_username("alice");
    fake.send(chat::test::encode(reply));
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.ready();
    }));
    console.execute({"/create uncertain"});
    ASSERT_EQ(fake.receive(1).messages.size(), 1);
    const auto before = std::chrono::steady_clock::now();
    console.execute({"/quit"});
    ASSERT_TRUE(until([&] {
        console.poll();
        return console.finished();
    }));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 700ms);
    EXPECT_EQ(console.exit_code(), 1);
    EXPECT_NE(output.str().find("cancelled at exit deadline"), std::string::npos);
    EXPECT_EQ(output.str().find("OK 1 create"), std::string::npos);
}
