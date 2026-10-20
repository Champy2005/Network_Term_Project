/** @file
 * @brief Interrupt a single real TCP client, optionally after a committed message ACK.
 */
#pragma once
#include "test_peer.hpp"
#include <atomic>
#include <boost/asio/post.hpp>
#include <memory>
#include <thread>

namespace chat::test {
/// @brief Test-only loopback proxy; each direction has one write and one owning context.
class RecoveryProxy {
    struct Session : std::enable_shared_from_this<Session> {
        explicit Session(RecoveryProxy& owner)
            : proxy(owner), front(owner.context), back(owner.context) {}
        void close() {
            boost::system::error_code ignored;
            static_cast<void>(front.close(ignored));
            static_cast<void>(back.close(ignored));
        }
        void read(bool upstream) {
            auto self = shared_from_this();
            auto& source = upstream ? front : back;
            auto& input = upstream ? client_bytes : server_bytes;
            source.async_read_some(
                boost::asio::buffer(input),
                [self, upstream](const boost::system::error_code& error, std::size_t count) {
                    if (error) {
                        self->close();
                        return;
                    }
                    std::vector<std::uint8_t> output;
                    if (upstream || self->proxy.opaque_) {
                        const auto& input = upstream ? self->client_bytes : self->server_bytes;
                        output.assign(input.begin(),
                                      input.begin() + static_cast<std::ptrdiff_t>(count));
                    } else {
                        for (const auto& frame : self->decoder.feed(
                                 std::span<const std::uint8_t>(self->server_bytes.data(), count))) {
                            protocol::Envelope message;
                            if (!message.ParseFromArray(frame.data(),
                                                        static_cast<int>(frame.size()))) {
                                self->close();
                                return;
                            }
                            if (message.has_ack() &&
                                ((!message.ack().message_id().empty() &&
                                  self->proxy.drop_ack.exchange(false)) ||
                                 (message.ack().message_id().empty() &&
                                  self->proxy.drop_group_ack.exchange(false)))) {
                                self->close();
                                self->proxy.dropped = true;
                                return;
                            }
                            if (message.has_group_message() &&
                                message.group_message().room_sequence() ==
                                    self->proxy.skip_sequence.load()) {
                                self->proxy.skip_sequence = 0;
                                continue;
                            }
                            const auto encoded = encode(message);
                            output.insert(output.end(), encoded.begin(), encoded.end());
                        }
                    }
                    if (output.empty()) {
                        self->read(upstream);
                        return;
                    }
                    auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(output));
                    auto& destination = upstream ? self->back : self->front;
                    boost::asio::async_write(
                        destination, boost::asio::buffer(*bytes),
                        [self, upstream, bytes](const boost::system::error_code& write_error,
                                                std::size_t) {
                            if (write_error) {
                                self->close();
                            } else {
                                self->read(upstream);
                            }
                        });
                });
        }
        RecoveryProxy& proxy;
        tcp::socket front, back;
        std::array<std::uint8_t, 8192> client_bytes{}, server_bytes{};
        protocol::FrameDecoder decoder;
    };

  public:
    explicit RecoveryProxy(std::uint16_t upstream, bool opaque = false)
        : acceptor(context, {boost::asio::ip::address_v4::loopback(), 0}), upstream_(upstream),
          opaque_(opaque) {
        accept();
        network = std::thread([this] { context.run(); });
    }
    ~RecoveryProxy() {
        boost::asio::post(context, [this] {
            boost::system::error_code ignored;
            static_cast<void>(acceptor.close(ignored));
            if (current) {
                current->close();
            }
        });
        network.join();
    }
    [[nodiscard]] std::uint16_t port() const { return acceptor.local_endpoint().port(); }
    /// @brief Pause admission and interrupt the current stream without stopping the chat server.
    void interrupt() {
        paused = true;
        boost::asio::post(context, [this] {
            if (current) {
                current->close();
            }
        });
    }
    std::atomic<bool> paused{false}, drop_ack{false}, drop_group_ack{false}, dropped{false};
    /// @brief Skip one live group frame to exercise sequence-gap repair on an intact stream.
    std::atomic<std::uint64_t> skip_sequence{0};

  private:
    void accept() {
        auto session = std::make_shared<Session>(*this);
        acceptor.async_accept(
            session->front, [this, session](const boost::system::error_code& error) {
                if (error) {
                    return;
                }
                current = session;
                if (paused) {
                    session->close();
                } else {
                    session->back.async_connect(
                        {boost::asio::ip::address_v4::loopback(), upstream_},
                        [session](const boost::system::error_code& connect_error) {
                            if (connect_error) {
                                session->close();
                            } else {
                                session->read(true);
                                session->read(false);
                            }
                        });
                }
                accept();
            });
    }
    boost::asio::io_context context;
    tcp::acceptor acceptor;
    std::uint16_t upstream_;
    bool opaque_;
    std::shared_ptr<Session> current;
    std::thread network;
};
} // namespace chat::test
