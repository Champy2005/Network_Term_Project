/** @file
 * @brief Stress ordered write admission, queue bounds, and shared session lifetimes.
 */
#include "chat/server/client_session.hpp"
#include "test_peer.hpp"

#include <atomic>
#include <barrier>
#include <boost/asio/post.hpp>
#include <boost/asio/socket_base.hpp>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace {
using boost::asio::ip::tcp;
using chat::test::Peer;
using chat::test::ping;

class SessionPair {
  public:
    explicit SessionPair(chat::server::SessionLimits limits = {}) {
        tcp::acceptor acceptor(context, {boost::asio::ip::address_v4::loopback(), 0});
        peer.socket.open(tcp::v4());
        peer.socket.set_option(boost::asio::socket_base::receive_buffer_size(16384));
        peer.socket.connect(acceptor.local_endpoint());
        tcp::socket socket(context);
        acceptor.accept(socket);
        // Keep writes pending for cancellation tests, independent of large
        // default OS send buffers. The peer does not read until told to do so.
        socket.set_option(boost::asio::socket_base::send_buffer_size(1024));
        session = std::make_shared<chat::server::ClientSession>(
            std::move(socket), boost::asio::make_strand(context),
            [](chat::server::ClientSession&, const chat::protocol::Envelope&) {},
            [this](chat::server::ClientSession&) { ++closed; }, limits);
    }
    ~SessionPair() { stop(); }

    void start() {
        session->start();
        for (auto& worker : workers) {
            worker = std::thread([this] { context.run(); });
        }
    }

    void stop() {
        if (session) {
            session->stop();
        }
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        context.restart();
        context.run();
    }

    boost::asio::io_context context;
    Peer peer;
    std::atomic<int> closed = 0;
    std::shared_ptr<chat::server::ClientSession> session;
    std::array<std::thread, 2> workers;
};

/// @brief Stress concurrent admission without losing framing or producer order.
TEST(SessionTest, ConcurrentProducersPreserveFramesAndEachProducersOrder) {
    SessionPair pair;
    pair.start();
    std::barrier ready(5);
    std::array<std::future<bool>, 4> producers;
    for (std::size_t producer = 0; producer < producers.size(); ++producer) {
        producers[producer] = std::async(std::launch::async, [&, producer] {
            ready.arrive_and_wait();
            for (std::uint64_t index = 0; index < 50; ++index) {
                auto message = ping(index, std::to_string(producer));
                message.mutable_pong()->set_nonce(index);
                if (!pair.session->send(message)) {
                    return false;
                }
            }
            return true;
        });
    }
    ready.arrive_and_wait();
    const auto result = pair.peer.receive(200);
    for (auto& producer : producers) {
        EXPECT_TRUE(producer.get());
    }
    ASSERT_FALSE(result.timed_out);
    ASSERT_EQ(result.messages.size(), 200U);
    std::array<std::uint64_t, 4> next{};
    for (const auto& message : result.messages) {
        const auto producer = static_cast<std::size_t>(std::stoul(message.request_id()));
        ASSERT_LT(producer, next.size());
        EXPECT_TRUE(message.has_pong());
        EXPECT_EQ(message.pong().nonce(), next[producer]++);
    }
    for (const auto count : next) {
        EXPECT_EQ(count, 50U);
    }
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
}

/// @brief Preserve FIFO order across multiple queued socket writes.
TEST(SessionTest, SequentialAdmissionKeepsExactOrderAcrossQueuedWrites) {
    SessionPair pair;
    for (std::uint64_t index = 0; index < 200; ++index) {
        ASSERT_TRUE(pair.session->send(ping(index)));
    }
    pair.start();
    const auto result = pair.peer.receive(200);
    ASSERT_FALSE(result.timed_out);
    ASSERT_EQ(result.messages.size(), 200U);
    for (std::size_t index = 0; index < result.messages.size(); ++index) {
        EXPECT_EQ(result.messages[index].ping().nonce(), index);
    }
}

/// @brief Bound admission even when executor work has not run.
TEST(SessionTest, FrameCountLimitAppliesBeforeExecutorRuns) {
    SessionPair pair({1024, 2});
    ASSERT_TRUE(pair.session->send(ping(1)));
    ASSERT_TRUE(pair.session->send(ping(2)));
    EXPECT_FALSE(pair.session->send(ping(3)));
    EXPECT_FALSE(pair.session->send(ping(4)));
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
}

/// @brief Count framing overhead against the byte budget.
TEST(SessionTest, ByteLimitIncludesFourByteFrameHeader) {
    const auto frame_size = chat::test::encode(ping(1)).size();
    SessionPair pair({frame_size, 8});
    ASSERT_TRUE(pair.session->send(ping(1)));
    EXPECT_FALSE(pair.session->send(ping(2)));
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
}

/// @brief Reject oversized output without disturbing another session.
TEST(SessionTest, OversizedOutgoingFrameClosesOnlyItsSession) {
    SessionPair pair;
    EXPECT_FALSE(pair.session->send(ping(1, std::string(chat::protocol::kMaxFrameSize, 'x'))));
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
}

/// @brief Drain cancellation without releasing active buffers prematurely.
TEST(SessionTest, StopDuringActiveReadAndWriteKeepsBuffersAliveAndReleasesSession) {
    SessionPair pair;
    std::weak_ptr<chat::server::ClientSession> weak = pair.session;
    ASSERT_TRUE(pair.session->send(ping(1, std::string(900000, 'x'))));
    pair.start();
    // Receive one wire byte: the composed write has started, but a 900 KiB
    // frame cannot fit in the deliberately small send buffer without reads.
    std::array<std::uint8_t, 1> byte{};
    pair.peer.socket.read_some(boost::asio::buffer(byte));
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
    pair.session.reset();
    EXPECT_TRUE(weak.expired());
}

/// @brief Retain the active write in admission accounting.
TEST(SessionTest, ByteLimitCountsTheActiveWriteUntilItCompletes) {
    const auto message = ping(1, std::string(900000, 'x'));
    SessionPair pair({chat::test::encode(message).size(), 8});
    ASSERT_TRUE(pair.session->send(message));
    pair.start();
    std::array<std::uint8_t, 1> byte{};
    pair.peer.socket.read_some(boost::asio::buffer(byte));
    EXPECT_FALSE(pair.session->send(ping(2)));
    pair.stop();
    EXPECT_EQ(pair.closed, 1);
}

/// @brief Handle peer reset under load with one close callback.
TEST(SessionTest, ResetPeerDuringLoadCancelsReadWriteAndNotifiesOnce) {
    SessionPair pair;
    ASSERT_TRUE(pair.session->send(ping(1, std::string(900000, 'x'))));
    pair.start();
    std::array<std::uint8_t, 1> byte{};
    pair.peer.socket.read_some(boost::asio::buffer(byte));
    pair.peer.socket.set_option(boost::asio::socket_base::linger(true, 0));
    pair.peer.socket.close();
    for (auto& worker : pair.workers) {
        worker.join(); // EOF/reset cancels the other outstanding operation.
    }
    EXPECT_EQ(pair.closed, 1);
    EXPECT_FALSE(pair.session->send(ping(2)));
}

} // namespace
