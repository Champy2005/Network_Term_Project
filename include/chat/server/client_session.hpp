/** @file
 * @brief Declare socket ownership, cancellation, and bounded ordered write admission.
 */
#pragma once

#include "chat.pb.h"
#include "chat/protocol/framing.hpp"
#include "chat/transport/socket_stream.hpp"

#include <array>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace chat::server {

/// @brief Admission bounds include queued and currently active framed writes.
struct SessionLimits {
    std::size_t queued_bytes = std::size_t{2} * protocol::kMaxFrameSize;
    std::size_t queued_frames = 256;
    /// @brief Complete inbound envelopes refresh this budget; default 30s, 0 disables, max 24h.
    std::chrono::milliseconds idle_timeout{30000};
};

/// Socket operations and callbacks run on the supplied strand. Use the hub's
/// strand for all sessions so registry/routing state has one executor boundary.
/// Keep this object in shared_ptr. start(), send(), and stop() are thread safe;
/// send admission is bounded before any executor work is posted.
class ClientSession : public std::enable_shared_from_this<ClientSession> {
  public:
    using Executor = boost::asio::strand<boost::asio::io_context::executor_type>;
    using OnMessage = std::function<void(ClientSession&, const protocol::Envelope&)>;
    using OnClose = std::function<void(ClientSession&)>;

    /// @brief Transfer socket ownership and install strand-bound message/close callbacks.
    ClientSession(boost::asio::ip::tcp::socket socket, Executor executor, OnMessage on_message,
                  OnClose on_close, SessionLimits limits = {},
                  std::shared_ptr<boost::asio::ssl::context> tls_context = {},
                  std::chrono::milliseconds handshake_timeout = std::chrono::seconds(3));
    /// @brief Begin reading and idle expiry once; repeated calls are harmless.
    void start();
    /// True means admitted, not delivered. Concurrent sends are ordered by
    /// mutex admission, preserving each producer's order. Invalid/oversized
    /// frames or queue overflow return false and close this session.
    [[nodiscard]] bool send(const protocol::Envelope& message);
    /// @brief Cancel socket/timer work and notify close once; repeated calls are harmless.
    void stop();

  private:
    using Frame = std::shared_ptr<const std::vector<std::uint8_t>>;
    void read();
    void pump_write();
    void stop_on_executor();
    void wait_for_idle();

    transport::SocketStream stream_;
    Executor executor_;
    boost::asio::steady_timer idle_timer_;
    std::chrono::steady_clock::time_point last_activity_;
    OnMessage on_message_;
    OnClose on_close_;
    SessionLimits limits_;
    std::chrono::milliseconds handshake_timeout_;
    protocol::FrameDecoder decoder_;
    std::array<std::uint8_t, 4096> input_{};
    std::mutex queue_mutex_;
    std::deque<Frame> queue_;
    std::size_t queued_bytes_ = 0;
    bool accepting_sends_ = true;
    bool pump_scheduled_ = false;
    // Accessed only on executor_.
    bool started_ = false;
    bool stopped_ = false;
    bool read_closed_ = false;
    bool writing_ = false;
    bool handshake_done_ = false;
};

} // namespace chat::server
