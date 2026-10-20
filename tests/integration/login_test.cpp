/** @file
 * @brief Verify active-name uniqueness and deny chat from unauthenticated connections.
 */
#include "chat/client/ping_client.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"

#include <barrier>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {
using boost::asio::ip::tcp;
using chat::test::encode;
using chat::test::Peer;
using chat::test::ping;

chat::protocol::Envelope login(const std::string& name, const std::string& id = "login-request") {
    chat::protocol::Envelope message;
    message.set_protocol_version(1);
    message.set_request_id(id);
    message.mutable_login_request()->set_username(name);
    return message;
}

class LoginTest : public testing::Test {
  protected:
    LoginTest() : server(context, {boost::asio::ip::address_v4::loopback(), 0}) {
        server.start();
        for (auto& worker : workers) {
            worker = std::thread([this] { context.run(); });
        }
    }
    ~LoginTest() override {
        server.stop();
        for (auto& worker : workers) {
            worker.join();
        }
    }

    chat::client::LoginResult claim(Peer& peer, const std::string& name) {
        peer.send(encode(login(name)));
        const auto result = peer.receive(1);
        if (result.messages.size() != 1 || !result.messages[0].has_login_response()) {
            throw std::runtime_error("expected LoginResponse");
        }
        EXPECT_EQ(result.messages[0].protocol_version(), 1U);
        EXPECT_EQ(result.messages[0].request_id(), "login-request");
        const auto& reply = result.messages[0].login_response();
        return {reply.accepted(), reply.username(), reply.reason(), reply.resume_token()};
    }

    boost::asio::io_context context;
    chat::server::PingServer server;
    std::array<std::thread, 2> workers;
};

/// @brief Use the public login API and retain normal Ping exchanges.
TEST_F(LoginTest, HeadlessClientLogsInAndContinuesPinging) {
    chat::client::PingClient client;
    client.connect("127.0.0.1", server.port());
    const auto result = client.login("Alice_01");
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.username, "Alice_01");
    EXPECT_TRUE(result.reason.empty());
    EXPECT_EQ(client.ping(123), 123U);
}

/// @brief Keep rejection recoverable by choosing a free username.
TEST_F(LoginTest, DuplicateClaimRejectedAndConnectionCanChooseAnotherName) {
    Peer alice(server.port());
    Peer bob(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    const auto duplicate = claim(bob, "alice");
    EXPECT_FALSE(duplicate.accepted);
    EXPECT_TRUE(duplicate.username.empty());
    EXPECT_FALSE(duplicate.reason.empty());
    EXPECT_TRUE(claim(bob, "bob").accepted);
}

/// @brief Serialize competing claims even with two context workers.
TEST_F(LoginTest, SimultaneousClaimsHaveExactlyOneWinner) {
    chat::client::PingClient first;
    chat::client::PingClient second;
    first.connect("127.0.0.1", server.port());
    second.connect("127.0.0.1", server.port());
    std::barrier ready(3);
    auto a = std::async(std::launch::async, [&] {
        ready.arrive_and_wait();
        return first.login("same_name").accepted;
    });
    auto b = std::async(std::launch::async, [&] {
        ready.arrive_and_wait();
        return second.login("same_name").accepted;
    });
    ready.arrive_and_wait();
    const auto first_won = a.get();
    const auto second_won = b.get();
    EXPECT_NE(first_won, second_won);
}

/// @brief Verify username alphabet and byte-length boundaries.
TEST_F(LoginTest, InvalidNamesRejectedAndBoundaryNamesAccepted) {
    Peer peer(server.port());
    for (const auto& name :
         std::vector<std::string>{"", "has space", "bad-name", "bad\nname", std::string(33, 'a'),
                                  "\xc3\xa9", std::string("a\0b", 3)}) {
        SCOPED_TRACE(name);
        const auto result = claim(peer, name);
        EXPECT_FALSE(result.accepted);
        EXPECT_FALSE(result.reason.empty());
    }
    EXPECT_TRUE(claim(peer, std::string(32, 'a')).accepted);
    Peer one(server.port());
    EXPECT_TRUE(claim(one, "_").accepted);
}

/// @brief Allow distinct names that differ only by letter case.
TEST_F(LoginTest, UsernameComparisonIsCaseSensitive) {
    Peer first(server.port());
    Peer second(server.port());
    EXPECT_TRUE(claim(first, "Alice").accepted);
    EXPECT_TRUE(claim(second, "alice").accepted);
}

/// @brief Keep one immutable registered identity per connection.
TEST_F(LoginTest, LoggedInConnectionCannotRenameOrClaimASecondIdentity) {
    Peer alice(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    EXPECT_FALSE(claim(alice, "bob").accepted);
    Peer bob(server.port());
    EXPECT_TRUE(claim(bob, "bob").accepted);
    Peer duplicate(server.port());
    EXPECT_FALSE(claim(duplicate, "alice").accepted);
}

/// @brief Release an active name during normal disconnect cleanup.
TEST_F(LoginTest, CleanDisconnectReleasesNameBeforeServerEof) {
    Peer alice(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    alice.socket.shutdown(tcp::socket::shutdown_send);
    const auto closed = alice.receive(1);
    ASSERT_FALSE(closed.timed_out);
    ASSERT_EQ(closed.error, boost::asio::error::eof);
    Peer replacement(server.port());
    EXPECT_TRUE(claim(replacement, "alice").accepted);
}

/// @brief Release a name when malformed traffic closes its session.
TEST_F(LoginTest, ProtocolViolationReleasesRegisteredName) {
    Peer alice(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    alice.send({0, 0, 0, 0});
    const auto closed = alice.receive(1);
    ASSERT_FALSE(closed.timed_out);
    ASSERT_TRUE(closed.error);
    Peer replacement(server.port());
    EXPECT_TRUE(claim(replacement, "alice").accepted);
}

/// @brief Derive authorization from the connection rather than supplied sender fields.
TEST_F(LoginTest, AllChatOperationsRequireLoginEvenWithForgedSender) {
    Peer peer(server.port());
    for (int operation = 0; operation < 4; ++operation) {
        auto message = login("unused");
        switch (operation) {
        case 0:
            message.mutable_direct_message()->set_sender("alice");
            message.mutable_direct_message()->set_recipient("bob");
            message.mutable_direct_message()->set_text("blocked");
            break;
        case 1:
            message.mutable_group_message()->set_sender("alice");
            break;
        case 2:
            message.mutable_create_group_request()->set_name("group");
            break;
        case 3:
            message.mutable_join_group_request()->set_group_id("group");
            break;
        }
        peer.send(encode(message));
        const auto result = peer.receive(1);
        ASSERT_EQ(result.messages.size(), 1U);
        EXPECT_EQ(result.messages[0].request_id(), message.request_id());
        EXPECT_EQ(result.messages[0].error().code(), chat::protocol::ERROR_CODE_NOT_LOGGED_IN);
    }
    EXPECT_TRUE(claim(peer, "alice").accepted);
}

/// @brief Prevent a rejected claimant from impersonating the active owner.
TEST_F(LoginTest, RejectedDuplicateDoesNotGainLoginPrivileges) {
    Peer alice(server.port());
    Peer rejected(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    ASSERT_FALSE(claim(rejected, "alice").accepted);
    auto message = login("unused");
    message.mutable_direct_message()->set_sender("alice");
    rejected.send(encode(message));
    const auto result = rejected.receive(1);
    ASSERT_EQ(result.messages.size(), 1U);
    EXPECT_EQ(result.messages[0].error().code(), chat::protocol::ERROR_CODE_NOT_LOGGED_IN);
}

/// @brief Return an error without leaking a forged message to its target.
TEST_F(LoginTest, ForgedMetadataIsRejectedAndNeverDelivered) {
    Peer alice(server.port());
    Peer bob(server.port());
    ASSERT_TRUE(claim(alice, "alice").accepted);
    ASSERT_TRUE(claim(bob, "bob").accepted);
    auto message = login("unused");
    message.mutable_direct_message()->set_recipient("bob");
    message.mutable_direct_message()->set_text("not implemented");
    message.mutable_direct_message()->set_sender("bob");
    alice.send(encode(message));
    const auto result = alice.receive(1);
    ASSERT_EQ(result.messages.size(), 1U);
    EXPECT_EQ(result.messages[0].error().code(), chat::protocol::ERROR_CODE_INVALID_REQUEST);
    // A correlated Pong acts as a barrier on Bob's stream: any accidental
    // earlier delivery would appear before it rather than being hidden by a UI.
    bob.send(encode(ping(5, "barrier")));
    const auto bob_result = bob.receive(1);
    ASSERT_EQ(bob_result.messages.size(), 1U);
    EXPECT_TRUE(bob_result.messages[0].has_pong());
    EXPECT_EQ(bob_result.messages[0].request_id(), "barrier");
    const auto quiet = bob.receive(1, std::chrono::milliseconds(100));
    EXPECT_TRUE(quiet.timed_out);
    EXPECT_TRUE(quiet.messages.empty());
}

/// @brief Validate accepted identity against the requested username.
TEST_F(LoginTest, HeadlessClientRejectsAnAcceptedLoginForAnotherUsername) {
    boost::asio::io_context fake_context;
    tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::PingClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    auto responder = std::async(std::launch::async, [&] {
        const auto request = fake_server.receive(1);
        if (request.messages.size() != 1) {
            return false;
        }
        auto response = request.messages[0];
        response.mutable_login_response()->set_accepted(true);
        response.mutable_login_response()->set_username("someone_else");
        fake_server.send(encode(response));
        return true;
    });
    EXPECT_THROW(static_cast<void>(client.login("alice")), std::runtime_error);
    EXPECT_TRUE(responder.get());
    EXPECT_THROW(static_cast<void>(client.ping(1)), std::runtime_error);
}

} // namespace
