/** @file
 * @brief Provide real-socket peers, bounded reads, and fake servers for integration tests.
 */
#pragma once
#include "chat.pb.h"
#include "chat/protocol/framing.hpp"
#include <array>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
namespace chat::test {
using boost::asio::ip::tcp;
using namespace std::chrono_literals;
inline std::vector<std::uint8_t> encode(const chat::protocol::Envelope& message) {
    const auto payload = message.SerializeAsString();
    return chat::protocol::encode_frame(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
}

inline chat::protocol::Envelope ping(const std::uint64_t nonce, const std::string& id = "request") {
    chat::protocol::Envelope message;
    message.set_protocol_version(1);
    message.set_request_id(id);
    message.mutable_ping()->set_nonce(nonce);
    return message;
}

// Real sockets with bounded reads, so a regression cannot hang the suite.
class Peer {
  public:
    Peer() = default;
    explicit Peer(const std::uint16_t port) {
        socket.connect({boost::asio::ip::address_v4::loopback(), port});
    }

    void send(const std::vector<std::uint8_t>& bytes) {
        boost::asio::write(socket, boost::asio::buffer(bytes));
    }

    struct Result {
        std::vector<chat::protocol::Envelope> messages;
        boost::system::error_code error;
        bool timed_out = false;
    };

    Result receive(const std::size_t expected, std::chrono::milliseconds timeout = 2s) {
        context.restart();
        Result result;
        const auto take_pending = [&] {
            while (!pending.empty() && result.messages.size() < expected) {
                result.messages.push_back(std::move(pending.front()));
                pending.pop_front();
            }
        };
        take_pending();
        if (result.messages.size() >= expected) {
            return result;
        }
        boost::asio::steady_timer timer(context, timeout);
        timer.async_wait([&](const boost::system::error_code& error) {
            if (!error) {
                result.timed_out = true;
                socket.cancel();
            }
        });
        std::array<std::uint8_t, 4096> input{};
        std::function<void()> read;
        read = [&] {
            socket.async_read_some(
                boost::asio::buffer(input),
                [&](const boost::system::error_code& error, std::size_t count) {
                    if (error) {
                        result.error = error;
                        timer.cancel();
                        return;
                    }
                    for (const auto& bytes :
                         decoder.feed(std::span<const std::uint8_t>(input.data(), count))) {
                        chat::protocol::Envelope message;
                        if (!message.ParseFromArray(bytes.data(), static_cast<int>(bytes.size()))) {
                            throw std::runtime_error("test received malformed Protobuf");
                        }
                        if (message.has_user_list_update()) {
                            users = message.user_list_update();
                        } else if (message.has_group_list_update()) {
                            groups = message.group_list_update();
                        } else {
                            pending.push_back(std::move(message));
                        }
                    }
                    take_pending();
                    if (result.messages.size() >= expected) {
                        timer.cancel();
                    } else {
                        read();
                    }
                });
        };
        read();
        context.run();
        return result;
    }

    boost::asio::io_context context;
    tcp::socket socket{context};
    chat::protocol::FrameDecoder decoder;
    std::deque<chat::protocol::Envelope> pending;
    std::optional<chat::protocol::UserListUpdate> users;
    std::optional<chat::protocol::GroupListUpdate> groups;
};

} // namespace chat::test
