/** @file
 * @brief Verify framed TCP Ping/Pong, malformed input, deadlines, and server shutdown.
 */
#include "chat.pb.h"
#include "chat/client/ping_client.hpp"
#include "chat/protocol/framing.hpp"
#include "chat/server/ping_server.hpp"
#include "test_peer.hpp"

#include <array>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using boost::asio::ip::tcp;
using namespace std::chrono_literals;

using chat::test::encode;
using chat::test::Peer;
using chat::test::ping;

class PingTest : public testing::Test {
  protected:
    PingTest() : server_(context_, {boost::asio::ip::address_v4::loopback(), 0}) {
        server_.start();
        worker_ = std::thread([this] { context_.run(); });
    }
    ~PingTest() override { stop(); }

    void stop() {
        if (worker_.joinable()) {
            boost::asio::post(context_, [this] { server_.stop(); });
            worker_.join(); // Drains accept/read/write cancellation handlers.
        }
    }

    void expect_rejected(const std::vector<std::uint8_t>& bytes) {
        Peer peer(server_.port());
        peer.send(bytes);
        peer.socket.shutdown(tcp::socket::shutdown_send);
        const auto result = peer.receive(1);
        EXPECT_FALSE(result.timed_out);
        EXPECT_TRUE(result.messages.empty());
        EXPECT_TRUE(result.error == boost::asio::error::eof ||
                    result.error == boost::asio::error::connection_reset);
        // Invalid input must affect only the offending connection.
        chat::client::PingClient healthy;
        healthy.connect("127.0.0.1", server_.port());
        EXPECT_EQ(healthy.ping(12), 12U);
    }

    boost::asio::io_context context_;
    chat::server::PingServer server_;
    std::thread worker_;
};

/// @brief Reuse two connections and correlate each independent Pong.
TEST_F(PingTest, TwoPersistentClientsExchangeIndependentNonces) {
    chat::client::PingClient alice;
    chat::client::PingClient bob;
    alice.connect("localhost", server_.port());
    bob.connect("127.0.0.1", server_.port());
    auto first = std::async(std::launch::async, [&] { return alice.ping(42); });
    EXPECT_EQ(bob.ping(87), 87U);
    EXPECT_EQ(first.get(), 42U);
    EXPECT_EQ(alice.ping(0), 0U);
    EXPECT_EQ(bob.ping(std::numeric_limits<std::uint64_t>::max()),
              std::numeric_limits<std::uint64_t>::max());
}

/// @brief Handle fragmented and coalesced TCP requests in one stream.
TEST_F(PingTest, SplitHeaderAndPayloadThenCoalescedRequestsPreserveOrder) {
    Peer peer(server_.port());
    const auto frame = encode(ping(123, "split"));
    for (const auto byte : frame) {
        peer.send({byte});
    }
    const auto split = peer.receive(1);
    ASSERT_FALSE(split.timed_out);
    ASSERT_EQ(split.messages.size(), 1U);
    EXPECT_EQ(split.messages[0].request_id(), "split");
    EXPECT_EQ(split.messages[0].pong().nonce(), 123U);
    std::vector<std::uint8_t> batch;
    for (std::uint64_t nonce = 0; nonce < 100; ++nonce) {
        const auto encoded = encode(ping(nonce, std::to_string(nonce)));
        batch.insert(batch.end(), encoded.begin(), encoded.end());
    }
    peer.send(batch);
    const auto coalesced = peer.receive(100);
    ASSERT_FALSE(coalesced.timed_out);
    ASSERT_EQ(coalesced.messages.size(), 100U);
    for (std::size_t index = 0; index < coalesced.messages.size(); ++index) {
        EXPECT_EQ(coalesced.messages[index].protocol_version(), 1U);
        EXPECT_TRUE(coalesced.messages[index].has_pong());
        EXPECT_EQ(coalesced.messages[index].pong().nonce(), index);
        EXPECT_EQ(coalesced.messages[index].request_id(), std::to_string(index));
    }
}

/// @brief Accept a valid framed envelope at the wire size limit.
TEST_F(PingTest, MaximumSizeFrameIsAccepted) {
    Peer peer(server_.port());
    auto message = ping(7, std::string(chat::protocol::kMaxFrameSize - 12U, 'x'));
    // Tune the request ID to the exact serialized limit without hard-coding
    // Protobuf varint overhead.
    const auto delta = static_cast<std::ptrdiff_t>(chat::protocol::kMaxFrameSize) -
                       static_cast<std::ptrdiff_t>(message.ByteSizeLong());
    message.mutable_request_id()->resize(
        static_cast<std::size_t>(static_cast<std::ptrdiff_t>(message.request_id().size()) + delta));
    ASSERT_EQ(message.ByteSizeLong(), chat::protocol::kMaxFrameSize);
    peer.send(encode(message));
    const auto result = peer.receive(1);
    ASSERT_FALSE(result.timed_out);
    ASSERT_EQ(result.messages.size(), 1U);
    EXPECT_EQ(result.messages[0].request_id(), message.request_id());
    EXPECT_EQ(result.messages[0].pong().nonce(), 7U);
}

/// @brief Disconnect peers declaring zero or oversized frames.
TEST_F(PingTest, InvalidFrameLengthsAreRejected) {
    expect_rejected({0, 0, 0, 0});
    expect_rejected({0, 0x10, 0, 1});
}

/// @brief Disconnect on malformed serialized envelope bytes.
TEST_F(PingTest, MalformedProtobufIsRejected) {
    expect_rejected(chat::protocol::encode_frame(std::array<std::uint8_t, 1>{0x0a}));
}

/// @brief Reject missing payloads and incompatible protocol versions.
TEST_F(PingTest, UnsupportedOrMissingVersionAndPayloadAreRejected) {
    auto message = ping(1);
    message.set_protocol_version(2);
    expect_rejected(encode(message));
    message.set_protocol_version(0);
    expect_rejected(encode(message));
    message.set_protocol_version(1);
    message.clear_payload();
    expect_rejected(encode(message));
}

/// @brief Deny a client sending a server-response payload.
TEST_F(PingTest, ServerOnlyPongRequestsAreRejected) {
    auto message = ping(1);
    message.mutable_pong()->set_nonce(1);
    expect_rejected(encode(message));
}

/// @brief Reject a peer ending an incomplete frame.
TEST_F(PingTest, TruncatedHeaderAndPayloadAtEofAreRejected) {
    expect_rejected({0, 0});
    auto frame = encode(ping(1));
    frame.pop_back();
    expect_rejected(frame);
}

/// @brief Drain the last queued reply after a clean read half-close.
TEST_F(PingTest, PeerHalfCloseReceivesPendingResponseThenEof) {
    Peer peer(server_.port());
    peer.send(encode(ping(456)));
    peer.socket.shutdown(tcp::socket::shutdown_send);
    const auto response = peer.receive(1);
    ASSERT_EQ(response.messages.size(), 1U);
    EXPECT_EQ(response.messages[0].pong().nonce(), 456U);
    const auto closed = peer.receive(1);
    EXPECT_FALSE(closed.timed_out);
    EXPECT_EQ(closed.error, boost::asio::error::eof);
}

/// @brief Cancel idle and incomplete reads and release pending handlers.
TEST_F(PingTest, StopClosesIdleAndPartialConnectionsAndDrainsHandlers) {
    Peer idle(server_.port());
    Peer partial(server_.port());
    partial.send({0, 0});
    // A successful exchange ensures that the event loop has started.
    chat::client::PingClient live;
    live.connect("127.0.0.1", server_.port());
    EXPECT_EQ(live.ping(10), 10U);
    stop();
    for (Peer* peer : {&idle, &partial}) {
        const auto closed = peer->receive(1);
        EXPECT_FALSE(closed.timed_out);
        EXPECT_TRUE(closed.messages.empty());
        EXPECT_TRUE(closed.error == boost::asio::error::eof ||
                    closed.error == boost::asio::error::connection_reset);
    }
    EXPECT_THROW(static_cast<void>(live.ping(11)), std::runtime_error);
    server_.stop(); // Idempotent after the worker has drained.
}

/// @brief Expose refusal and allow an explicit subsequent connection.
TEST_F(PingTest, RefusedConnectionFailsAndClientCanReconnect) {
    boost::asio::io_context reservation_context;
    tcp::acceptor reservation(reservation_context);
    reservation.open(tcp::v4());
    reservation.bind({boost::asio::ip::address_v4::loopback(), 0});
    // Reserve without listening: avoids selecting a free port another test
    // could reuse, while the OS refuses incoming connections.
    chat::client::PingClient client;
    EXPECT_THROW(client.connect("127.0.0.1", reservation.local_endpoint().port()),
                 std::runtime_error);
    client.connect("127.0.0.1", server_.port());
    EXPECT_EQ(client.ping(8), 8U);
    client.close();
    EXPECT_THROW(static_cast<void>(client.ping(9)), std::runtime_error);
}

/// @brief Enforce a hard exchange deadline without poisoning future connect.
TEST_F(PingTest, SilentServerTriggersDeadlineAndAllowsReconnect) {
    boost::asio::io_context silent_context;
    tcp::acceptor silent(silent_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::PingClient client;
    client.connect("127.0.0.1", silent.local_endpoint().port());
    tcp::socket socket(silent_context);
    silent.accept(socket);
    try {
        static_cast<void>(client.ping(1, 100ms));
        FAIL() << "silent server must time out";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "operation timed out");
    }
    EXPECT_THROW(static_cast<void>(client.ping(2)), std::runtime_error);
    client.connect("127.0.0.1", server_.port());
    EXPECT_EQ(client.ping(3), 3U);
}

/// @brief Reject malformed, uncorrelated, or semantically wrong replies.
TEST_F(PingTest, ClientRejectsInvalidResponses) {
    boost::asio::io_context fake_context;
    tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    for (int scenario = 0; scenario < 5; ++scenario) {
        SCOPED_TRACE(scenario);
        chat::client::PingClient client;
        client.connect("127.0.0.1", fake.local_endpoint().port());
        Peer bad_server;
        fake.accept(bad_server.socket);
        auto send_response = std::async(std::launch::async, [&] {
            const auto request = bad_server.receive(1);
            if (request.messages.size() != 1) {
                return false;
            }
            auto response = request.messages.front();
            response.mutable_pong()->set_nonce(1);
            switch (scenario) {
            case 0:
                response.mutable_pong()->set_nonce(999);
                break;
            case 1:
                response.set_request_id("unrelated");
                break;
            case 2:
                response.set_protocol_version(2);
                break;
            case 3:
                response.mutable_ping()->set_nonce(1);
                break;
            case 4:
                response.clear_payload();
                break;
            }
            bad_server.send(encode(response));
            return true;
        });
        EXPECT_THROW(static_cast<void>(client.ping(1)), std::runtime_error);
        EXPECT_TRUE(send_response.get());
        EXPECT_THROW(static_cast<void>(client.ping(2)), std::runtime_error);
    }
}

/// @brief Assemble a Pong arriving across multiple reads.
TEST_F(PingTest, ClientAcceptsFragmentedPong) {
    boost::asio::io_context fake_context;
    tcp::acceptor fake(fake_context, {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::PingClient client;
    client.connect("127.0.0.1", fake.local_endpoint().port());
    Peer fake_server;
    fake.accept(fake_server.socket);
    auto send_response = std::async(std::launch::async, [&] {
        const auto request = fake_server.receive(1);
        if (request.messages.size() != 1) {
            return false;
        }
        auto response = request.messages.front();
        response.mutable_pong()->set_nonce(123);
        for (const auto byte : encode(response)) {
            fake_server.send({byte});
        }
        return true;
    });
    EXPECT_EQ(client.ping(123), 123U);
    EXPECT_TRUE(send_response.get());
}

/// @brief Enforce the connection cap and reclaim a disconnected slot.
TEST_F(PingTest, ConnectionLimitRejectsExtraPeerAndDisconnectReleasesSlot) {
    std::vector<std::unique_ptr<Peer>> peers;
    for (std::size_t index = 0; index < 64; ++index) {
        auto peer = std::make_unique<Peer>(server_.port());
        peer->send(encode(ping(index)));
        ASSERT_EQ(peer->receive(1).messages.size(), 1U);
        peers.push_back(std::move(peer));
    }
    Peer excess(server_.port());
    const auto rejected = excess.receive(1);
    EXPECT_FALSE(rejected.timed_out);
    EXPECT_TRUE(rejected.messages.empty());
    EXPECT_TRUE(rejected.error == boost::asio::error::eof ||
                rejected.error == boost::asio::error::connection_reset);
    peers.front()->socket.shutdown(tcp::socket::shutdown_send);
    // Wait for server EOF, proving it removed this connection before reusing
    // capacity, rather than sleeping for an arbitrary cleanup delay.
    EXPECT_EQ(peers.front()->receive(1).error, boost::asio::error::eof);
    chat::client::PingClient replacement;
    replacement.connect("127.0.0.1", server_.port());
    EXPECT_EQ(replacement.ping(65), 65U);
}

} // namespace
