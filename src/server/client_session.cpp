/** @file
 * @brief Serialize socket callbacks and retain write buffers through cancellation.
 */
#include "chat/server/client_session.hpp"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <span>
#include <stdexcept>
#include <utility>

namespace chat::server {

ClientSession::ClientSession(boost::asio::ip::tcp::socket socket, Executor executor,
                             OnMessage on_message, OnClose on_close, SessionLimits limits,
                             std::shared_ptr<boost::asio::ssl::context> tls_context,
                             std::chrono::milliseconds handshake_timeout)
    : stream_(std::move(socket), std::move(tls_context)), executor_(std::move(executor)),
      idle_timer_(stream_.get_executor()), on_message_(std::move(on_message)),
      on_close_(std::move(on_close)), limits_(limits), handshake_timeout_(handshake_timeout) {
    if (limits_.queued_bytes == 0 || limits_.queued_frames == 0 ||
        limits_.idle_timeout.count() < 0 || limits_.idle_timeout > std::chrono::hours(24) ||
        handshake_timeout_.count() <= 0 || handshake_timeout_ > std::chrono::seconds(60)) {
        throw std::invalid_argument(
            "positive queue limits and idle timeout in 0..24 hours required");
    }
}

void ClientSession::start() {
    boost::asio::dispatch(executor_, [self = shared_from_this()] {
        if (!self->started_ && !self->stopped_) {
            self->started_ = true;
            if (self->stream_.encrypted()) {
                self->idle_timer_.expires_after(self->handshake_timeout_);
                self->idle_timer_.async_wait(boost::asio::bind_executor(
                    self->executor_, [self](const boost::system::error_code& error) {
                        if (!error && !self->stopped_) {
                            self->stop_on_executor();
                        }
                    }));
                self->stream_.tls().async_handshake(
                    boost::asio::ssl::stream_base::server,
                    boost::asio::bind_executor(
                        self->executor_, [self](const boost::system::error_code& error) {
                            if (self->stopped_) {
                                return;
                            }
                            if (error) {
                                self->stop_on_executor();
                                return;
                            }
                            self->idle_timer_.cancel();
                            self->handshake_done_ = true;
                            self->last_activity_ = std::chrono::steady_clock::now();
                            self->wait_for_idle();
                            self->read();
                            self->pump_write();
                        }));
                return;
            }
            self->handshake_done_ = true;
            self->last_activity_ = std::chrono::steady_clock::now();
            self->wait_for_idle();
            self->read();
        }
    });
}

bool ClientSession::send(const protocol::Envelope& message) {
    Frame frame;
    try {
        const auto payload = message.SerializeAsString();
        frame = std::make_shared<const std::vector<std::uint8_t>>(
            protocol::encode_frame(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size())));
    } catch (const protocol::FrameError&) {
        stop();
        return false;
    }
    bool schedule = false;
    bool overflow = false;
    {
        std::lock_guard lock(queue_mutex_);
        if (!accepting_sends_) {
            return false;
        }
        overflow = queue_.size() >= limits_.queued_frames ||
                   frame->size() > limits_.queued_bytes - queued_bytes_;
        if (overflow) {
            accepting_sends_ = false;
        } else {
            queue_.push_back(frame);
            queued_bytes_ += frame->size();
            if (!pump_scheduled_) {
                pump_scheduled_ = true;
                schedule = true;
            }
        }
    }
    if (overflow) {
        stop();
        return false;
    }
    if (schedule) {
        boost::asio::post(executor_, [self = shared_from_this()] { self->pump_write(); });
    }
    return true;
}

void ClientSession::stop() {
    {
        std::lock_guard lock(queue_mutex_);
        accepting_sends_ = false;
    }
    boost::asio::dispatch(executor_, [self = shared_from_this()] { self->stop_on_executor(); });
}

void ClientSession::stop_on_executor() {
    if (stopped_) {
        return;
    }
    stopped_ = true;
    idle_timer_.cancel();
    {
        std::lock_guard lock(queue_mutex_);
        accepting_sends_ = false;
        queue_.clear();
        queued_bytes_ = 0;
    }
    boost::system::error_code ignored;
    // Bounded abortive TLS close: never race a shutdown writer with admitted writes.
    static_cast<void>(stream_.socket().close(ignored));
    if (on_close_) {
        on_close_(*this); // Exactly once, on the hub executor.
    }
}

void ClientSession::wait_for_idle() {
    if (limits_.idle_timeout.count() == 0) {
        return;
    }
    // One wait per session: traffic updates a timestamp instead of posting a
    // cancellation/rearm pair for every frame, including coalesced reads.
    idle_timer_.expires_at(last_activity_ + limits_.idle_timeout);
    idle_timer_.async_wait(boost::asio::bind_executor(
        executor_, [self = shared_from_this()](const boost::system::error_code& error) {
            if (error || self->stopped_) {
                return;
            }
            if (std::chrono::steady_clock::now() >=
                self->last_activity_ + self->limits_.idle_timeout) {
                self->stop_on_executor();
            } else {
                self->wait_for_idle();
            }
        }));
}

void ClientSession::read() {
    stream_.async_read_some(
        boost::asio::buffer(input_),
        boost::asio::bind_executor(
            executor_,
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t count) {
                if (self->stopped_) {
                    return;
                }
                if (error) {
                    if (error == boost::asio::error::eof) {
                        try {
                            self->decoder_.finish();
                            self->read_closed_ = true;
                            bool empty = false;
                            {
                                std::lock_guard lock(self->queue_mutex_);
                                self->accepting_sends_ = false;
                                empty = self->queue_.empty();
                            }
                            if (!empty) {
                                return; // Drain replies before closing a clean half-close.
                            }
                        } catch (const protocol::FrameError&) {
                            self->stop_on_executor();
                            return;
                        }
                    }
                    self->stop_on_executor();
                    return;
                }
                try {
                    const auto frames = self->decoder_.feed(
                        std::span<const std::uint8_t>(self->input_.data(), count));
                    for (const auto& frame : frames) {
                        protocol::Envelope message;
                        if (!message.ParseFromArray(frame.data(), static_cast<int>(frame.size())) ||
                            message.protocol_version() != 1 ||
                            message.payload_case() == protocol::Envelope::PAYLOAD_NOT_SET) {
                            self->stop_on_executor();
                            return;
                        }
                        // Partial bytes and outbound broadcasts are not proof of peer liveness.
                        self->last_activity_ = std::chrono::steady_clock::now();
                        self->on_message_(*self, message);
                        if (self->stopped_) {
                            return;
                        }
                    }
                    self->read();
                } catch (const protocol::FrameError&) {
                    self->stop_on_executor();
                }
            }));
}

void ClientSession::pump_write() {
    if (stopped_ || writing_ || (stream_.encrypted() && !handshake_done_)) {
        return;
    }
    Frame frame;
    {
        std::lock_guard lock(queue_mutex_);
        if (queue_.empty()) {
            pump_scheduled_ = false;
        } else {
            frame = queue_.front();
        }
    }
    if (!frame) {
        if (read_closed_) {
            stop_on_executor();
        }
        return;
    }
    writing_ = true;
    // Keep the active buffer alive through stop(), queue clear, and cancellation.
    boost::asio::async_write(
        stream_, boost::asio::buffer(*frame),
        boost::asio::bind_executor(executor_,
                                   [self = shared_from_this(),
                                    frame](const boost::system::error_code& error, std::size_t) {
                                       self->writing_ = false;
                                       if (self->stopped_) {
                                           return;
                                       }
                                       if (error) {
                                           self->stop_on_executor();
                                           return;
                                       }
                                       {
                                           std::lock_guard lock(self->queue_mutex_);
                                           self->queued_bytes_ -= frame->size();
                                           self->queue_.pop_front();
                                       }
                                       self->pump_write();
                                   }));
}

} // namespace chat::server
