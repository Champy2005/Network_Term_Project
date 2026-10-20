/** @file
 * @brief Implement deadline-bound exchanges and bounded unsolicited event buffering.
 */
#include "chat/client/ping_client.hpp"
#include "chat.pb.h"
#include "chat/protocol/framing.hpp"
#include "chat/protocol/message_identity.hpp"
#include "chat/transport/socket_stream.hpp"

#include <algorithm>
#include <array>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <charconv>
#include <deque>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>

namespace chat::client {
namespace {
bool valid_server_id(const std::string& id) {
    return !id.empty() && id.size() <= 128 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-' || c == '_';
           });
}
bool valid_sequence(std::uint64_t sequence) {
    return sequence > 0 &&
           sequence <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
bool valid_resume_token(const std::string& token) {
    return token.size() == 64 && std::all_of(token.begin(), token.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
} // namespace
class PingClient::Impl {
  public:
    using tcp = boost::asio::ip::tcp;
    explicit Impl(transport::ClientTlsOptions tls) : tls_options_(std::move(tls)) {}
    void close() {
        boost::system::error_code ignored;
        static_cast<void>(stream_->socket().close(ignored));
        decoder_ = protocol::FrameDecoder{};
        events_.clear();
        event_bytes_ = 0;
    }
    void arm(std::chrono::milliseconds timeout, bool soft = false) {
        if (timeout.count() <= 0) {
            throw std::invalid_argument("timeout must be positive");
        }
        context_.restart();
        failure_.clear();
        receive_timed_out_ = false;
        protocol_failure_ = false;
        tls_failure_ = false;
        timer_.expires_after(timeout);
        timer_.async_wait([this, soft](const boost::system::error_code& error) {
            if (!error) {
                if (soft) {
                    receive_timed_out_ = true;
                    boost::system::error_code ignored;
                    static_cast<void>(stream_->socket().cancel(ignored));
                } else {
                    failure_ = "operation timed out";
                    resolver_.cancel();
                    close();
                }
            }
        });
    }
    void complete(const boost::system::error_code& error) {
        if (error && failure_.empty() && !receive_timed_out_) {
            failure_ = error.message();
            tls_failure_ =
                stream_->encrypted() && error.category() == boost::asio::error::get_ssl_category();
        }
        timer_.cancel();
    }
    void check() {
        if (!failure_.empty()) {
            close();
            if (tls_failure_) {
                throw transport::TlsError("TLS handshake/verification/record failure: " + failure_);
            }
            if (protocol_failure_) {
                throw std::runtime_error(failure_);
            }
            throw NetworkError(failure_);
        }
    }
    void connect(const std::string& host, std::uint16_t port, std::chrono::milliseconds timeout) {
        close();
        // Recreate the SSL engine on every attempt. Never reuse a failed handshake or downgrade.
        stream_ = std::make_unique<transport::SocketStream>(
            tcp::socket(context_), transport::client_tls_context(tls_options_));
        if (stream_->encrypted()) {
            transport::verify_tls_peer(
                *stream_, tls_options_.server_name.empty() ? host : tls_options_.server_name);
        }
        arm(timeout);
        resolver_.async_resolve(
            host, std::to_string(port),
            [this](const boost::system::error_code& error,
                   const tcp::resolver::results_type& endpoints) {
                if (error || !failure_.empty()) {
                    complete(error);
                    return;
                }
                if (!stream_->encrypted() &&
                    std::any_of(endpoints.begin(), endpoints.end(), [](const auto& endpoint) {
                        return !endpoint.endpoint().address().is_loopback();
                    })) {
                    protocol_failure_ = true;
                    failure_ = "plaintext development client requires loopback endpoints";
                    complete({});
                    return;
                }
                boost::asio::async_connect(
                    stream_->socket(), endpoints,
                    [this](const boost::system::error_code& error, const tcp::endpoint&) {
                        if (error || !failure_.empty() || !stream_->encrypted()) {
                            complete(error);
                            return;
                        }
                        // The original deadline spans DNS, TCP, and TLS.
                        stream_->tls().async_handshake(
                            boost::asio::ssl::stream_base::client,
                            [this](const boost::system::error_code& handshake_error) {
                                complete(handshake_error);
                            });
                    });
            });
        context_.run();
        check();
    }
    protocol::Envelope request(const std::string& kind) {
        protocol::Envelope result;
        result.set_protocol_version(1);
        result.set_request_id(kind + "-" + std::to_string(++request_number_));
        return result;
    }
    std::uint64_t ping(std::uint64_t nonce, std::chrono::milliseconds timeout) {
        auto outgoing = request("ping");
        outgoing.mutable_ping()->set_nonce(nonce);
        const auto response = exchange(outgoing, timeout);
        if (!response.has_pong() || response.pong().nonce() != nonce) {
            close();
            throw std::runtime_error("invalid or mismatched Pong response");
        }
        return response.pong().nonce();
    }
    LoginResult login(const std::string& username, std::chrono::milliseconds timeout,
                      const std::string& token = {}) {
        auto outgoing = request("login");
        outgoing.mutable_login_request()->set_username(username);
        outgoing.mutable_login_request()->set_resume_token(token);
        const auto response = exchange(outgoing, timeout);
        if (!response.has_login_response() ||
            (response.login_response().accepted() &&
             response.login_response().username() != username) ||
            (!response.login_response().accepted() &&
             (response.login_response().reason().empty() ||
              !response.login_response().username().empty() ||
              !response.login_response().resume_token().empty())) ||
            (!response.login_response().resume_token().empty() &&
             !valid_resume_token(response.login_response().resume_token())) ||
            (response.login_response().accepted() && !token.empty() &&
             response.login_response().resume_token() != token)) {
            close();
            throw std::runtime_error("invalid Login response");
        }
        const auto& result = response.login_response();
        return {result.accepted(), result.username(), result.reason(), result.resume_token()};
    }
    OperationResult operation(const protocol::Envelope& outgoing,
                              std::chrono::milliseconds timeout) {
        const auto response = exchange(outgoing, timeout);
        const bool message = outgoing.has_direct_message() || outgoing.has_group_message();
        const auto key =
            outgoing.has_direct_message()  ? outgoing.direct_message().client_message_id()
            : outgoing.has_group_message() ? outgoing.group_message().client_message_id()
                                           : std::string{};
        if (response.has_ack() && !response.ack().resource_id().empty() &&
            response.ack().resource_id().size() <= 64 &&
            (message
                 ? valid_server_id(response.ack().message_id()) &&
                       valid_sequence(response.ack().room_sequence())
                 : response.ack().message_id().empty() && response.ack().room_sequence() == 0) &&
            (!outgoing.has_group_message() ||
             response.ack().resource_id() == outgoing.group_message().group_id()) &&
            (!outgoing.has_direct_message() ||
             response.ack().resource_id().starts_with("direct-"))) {
            return {true,
                    response.ack().resource_id(),
                    RequestError::none,
                    {},
                    response.ack().message_id(),
                    key,
                    response.ack().room_sequence()};
        }
        if (response.has_error() &&
            response.error().code() >= protocol::ERROR_CODE_INVALID_REQUEST &&
            response.error().code() <= protocol::ERROR_CODE_SYNC_GAP &&
            !response.error().message().empty()) {
            return {false,
                    {},
                    static_cast<RequestError>(response.error().code()),
                    response.error().message(),
                    {},
                    key,
                    0};
        }
        close();
        throw std::runtime_error("invalid operation response");
    }
    HistoryPage history(const std::string& room_id, std::uint64_t after, std::uint32_t limit,
                        std::chrono::milliseconds timeout) {
        if (room_id.empty() || room_id.size() > 64 || limit > 100 ||
            after > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            throw std::invalid_argument("invalid history page request");
        }
        auto outgoing = request("history");
        auto* query = outgoing.mutable_history_request();
        query->set_room_id(room_id);
        query->set_after_id(after);
        query->set_limit(limit);
        const auto response = exchange(outgoing, timeout);
        HistoryPage result;
        result.room_id = room_id;
        if (response.has_error() &&
            response.error().code() >= protocol::ERROR_CODE_INVALID_REQUEST &&
            response.error().code() <= protocol::ERROR_CODE_SYNC_GAP &&
            !response.error().message().empty()) {
            result.error = static_cast<RequestError>(response.error().code());
            result.reason = response.error().message();
            return result;
        }
        bool valid = response.has_history_response();
        const auto& page = response.history_response();
        valid &= page.room_id() == room_id &&
                 page.entries_size() <= static_cast<int>(limit == 0 ? 50 : limit);
        auto last = after;
        std::uint64_t last_sequence = 0;
        for (const auto& entry : page.entries()) {
            valid &= entry.id() > last &&
                     entry.id() <=
                         static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                     !entry.sender().empty() && entry.sender().size() <= 32 &&
                     !entry.text().empty() && entry.text().size() <= 4096 &&
                     valid_server_id(entry.message_id()) && valid_sequence(entry.room_sequence()) &&
                     entry.room_sequence() > last_sequence &&
                     (page.group() ? entry.recipient().empty()
                                   : !entry.recipient().empty() && entry.recipient().size() <= 32);
            result.records.push_back({entry.id(), entry.sender(), entry.recipient(), entry.text(),
                                      entry.message_id(), entry.room_sequence(),
                                      entry.server_timestamp_unix_ms()});
            last = entry.id();
            last_sequence = entry.room_sequence();
        }
        valid &= page.next_after_id() == last && (!page.has_more() || !page.entries().empty());
        if (!valid) {
            close();
            throw std::runtime_error("invalid history response");
        }
        result.accepted = true;
        result.next_after_id = last;
        result.has_more = page.has_more();
        result.group = page.group();
        return result;
    }
    // Validates and buffers unsolicited events while responses are in flight.
    SyncPage sync(const std::string& room, std::uint64_t after, std::uint64_t through,
                  std::uint32_t limit, std::chrono::milliseconds timeout) {
        const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (room.empty() || room.size() > 64 || after > maximum || through > maximum ||
            limit > 100) {
            throw std::invalid_argument("invalid sync bounds");
        }
        auto outgoing = request("sync");
        auto* query = outgoing.mutable_sync_request();
        query->set_room_id(room);
        query->set_after_sequence(after);
        query->set_through_sequence(through);
        query->set_limit(limit);
        const auto response = exchange(outgoing, timeout);
        SyncPage result;
        result.room_id = room;
        if (response.has_error() &&
            response.error().code() >= protocol::ERROR_CODE_INVALID_REQUEST &&
            response.error().code() <= protocol::ERROR_CODE_SYNC_GAP &&
            !response.error().message().empty()) {
            result.error = static_cast<RequestError>(response.error().code());
            result.reason = response.error().message();
            return result;
        }
        const auto& page = response.sync_response();
        bool valid = response.has_sync_response() && page.room_id() == room &&
                     page.group() == room.starts_with("group-") &&
                     page.entries_size() <= static_cast<int>(limit == 0 ? 50 : limit) &&
                     page.through_sequence() >= after && page.through_sequence() <= maximum &&
                     (through == 0 || page.through_sequence() == through);
        auto last = after;
        for (const auto& entry : page.entries()) {
            valid &= entry.room_sequence() == last + 1 && valid_sequence(entry.room_sequence()) &&
                     entry.room_sequence() <= page.through_sequence() &&
                     valid_server_id(entry.message_id()) && entry.id() > 0 &&
                     entry.id() <= maximum && !entry.sender().empty() &&
                     entry.sender().size() <= 32 && !entry.text().empty() &&
                     entry.text().size() <= 4096 &&
                     (page.group() ? entry.recipient().empty()
                                   : !entry.recipient().empty() && entry.recipient().size() <= 32);
            result.records.push_back({entry.id(), entry.sender(), entry.recipient(), entry.text(),
                                      entry.message_id(), entry.room_sequence(),
                                      entry.server_timestamp_unix_ms()});
            last = entry.room_sequence();
        }
        valid &= page.next_sequence() == last &&
                 page.has_more() == (last < page.through_sequence()) &&
                 (!page.has_more() || !page.entries().empty());
        if (!valid) {
            close();
            throw std::runtime_error("invalid sync response");
        }
        result.accepted = true;
        result.next_sequence = last;
        result.through_sequence = page.through_sequence();
        result.has_more = page.has_more();
        result.group = page.group();
        return result;
    }
    DirectRoomsPage direct_rooms(std::uint64_t after, std::uint32_t limit,
                                 std::chrono::milliseconds timeout) {
        if (after > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            limit > 64) {
            throw std::invalid_argument("invalid room discovery bounds");
        }
        auto outgoing = request("rooms");
        outgoing.mutable_direct_rooms_request()->set_after_room_id(after);
        outgoing.mutable_direct_rooms_request()->set_limit(limit);
        const auto response = exchange(outgoing, timeout);
        DirectRoomsPage result;
        if (response.has_error() &&
            response.error().code() >= protocol::ERROR_CODE_INVALID_REQUEST &&
            response.error().code() <= protocol::ERROR_CODE_SYNC_GAP &&
            !response.error().message().empty()) {
            result.error = static_cast<RequestError>(response.error().code());
            result.reason = response.error().message();
            return result;
        }
        const auto& page = response.direct_rooms_response();
        bool valid = response.has_direct_rooms_response() &&
                     page.rooms_size() <= static_cast<int>(limit == 0 ? 64 : limit);
        auto last = after;
        for (const auto& room : page.rooms()) {
            std::uint64_t number = 0;
            const auto id = std::string_view(room.room_id());
            if (!id.starts_with("direct-")) {
                valid = false;
                break;
            }
            const auto parsed = std::from_chars(id.data() + 7, id.data() + id.size(), number);
            valid &=
                parsed.ec == std::errc{} && parsed.ptr == id.data() + id.size() && number > last &&
                number <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                room.room_id() == "direct-" + std::to_string(number) && !room.peer().empty() &&
                room.peer().size() <= 32 &&
                room.last_sequence() <=
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
            result.rooms.push_back(
                {room.room_id(), room.peer(), room.last_sequence(), room.peer_connected()});
            last = number;
        }
        valid &= page.next_room_id() == last && (!page.has_more() || !page.rooms().empty());
        if (!valid) {
            close();
            throw std::runtime_error("invalid direct room response");
        }
        result.accepted = true;
        result.next_room_id = last;
        result.has_more = page.has_more();
        return result;
    }
    void event(protocol::Envelope message) {
        if (!message.request_id().empty()) {
            throw std::runtime_error("event has a request ID");
        }
        if (message.has_direct_message()) {
            const auto& dm = message.direct_message();
            if (dm.sender().empty() || dm.recipient().empty() || dm.room_id().empty() ||
                dm.text().empty() || dm.sender().size() > 32 || dm.recipient().size() > 32 ||
                dm.room_id().size() > 64 || dm.text().size() > 4096 ||
                !valid_server_id(dm.message_id()) || !valid_sequence(dm.room_sequence()) ||
                dm.server_timestamp_unix_ms() == 0 || !dm.client_message_id().empty()) {
                throw std::runtime_error("invalid direct delivery");
            }
        } else if (message.has_group_message()) {
            const auto& group = message.group_message();
            if (group.sender().empty() || group.group_id().empty() || group.text().empty() ||
                group.sender().size() > 32 || group.group_id().size() > 64 ||
                group.text().size() > 4096 || !valid_server_id(group.message_id()) ||
                !valid_sequence(group.room_sequence()) || group.server_timestamp_unix_ms() == 0 ||
                !group.client_message_id().empty()) {
                throw std::runtime_error("invalid group delivery");
            }
        } else if (!message.has_user_list_update() && !message.has_group_list_update()) {
            throw std::runtime_error("unexpected server response");
        }
        const auto size = message.ByteSizeLong() + 4;
        if (events_.size() >= 256 ||
            size > std::size_t{2} * protocol::kMaxFrameSize - event_bytes_) {
            throw std::runtime_error("client event queue limit exceeded");
        }
        event_bytes_ += size;
        events_.push_back(std::move(message));
    }
    void receive(std::optional<protocol::Envelope>& response, const protocol::Envelope* outgoing) {
        // This loop runs synchronously until every local-buffer callback drains.
        std::array<std::uint8_t, 4096> input{};
        std::function<void()> read;
        read = [&] {
            stream_->async_read_some(
                boost::asio::buffer(input),
                [&](const boost::system::error_code& error, std::size_t count) {
                    if (error) {
                        complete(error);
                        return;
                    }
                    try {
                        for (const auto& bytes :
                             decoder_.feed(std::span<const std::uint8_t>(input.data(), count))) {
                            protocol::Envelope message;
                            if (!message.ParseFromArray(bytes.data(),
                                                        static_cast<int>(bytes.size())) ||
                                message.protocol_version() != 1) {
                                throw std::runtime_error("invalid response envelope");
                            }
                            if (outgoing && message.request_id() == outgoing->request_id()) {
                                if (response) {
                                    throw std::runtime_error("duplicate operation response");
                                }
                                response = std::move(message);
                            } else {
                                event(std::move(message));
                            }
                        }
                        if (response || (!outgoing && !events_.empty()) || receive_timed_out_) {
                            complete({});
                        } else {
                            read();
                        }
                    } catch (const std::runtime_error& error) {
                        protocol_failure_ = true;
                        failure_ = error.what();
                        complete({});
                    }
                });
        };
        read();
        context_.run();
    }
    protocol::Envelope exchange(const protocol::Envelope& outgoing,
                                std::chrono::milliseconds timeout) {
        if (!stream_->socket().is_open()) {
            throw std::runtime_error("client is not connected");
        }
        const auto payload = outgoing.SerializeAsString();
        const auto frame = protocol::encode_frame(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
        arm(timeout);
        std::optional<protocol::Envelope> response;
        // Use a small initial run just for the write. The timer spans both runs.
        bool written = false;
        boost::asio::async_write(*stream_, boost::asio::buffer(frame),
                                 [&](const boost::system::error_code& error, std::size_t) {
                                     if (error) {
                                         complete(error);
                                     } else {
                                         written = true;
                                     }
                                 });
        // run_one() returns between handlers without cancelling the deadline.
        while (!written && failure_.empty()) {
            context_.run_one();
        }
        if (written && failure_.empty()) {
            receive(response, &outgoing);
        } else {
            context_.run();
        } // Drain the cancelled write and timer.
        check();
        if (!response) {
            close();
            throw std::runtime_error("missing operation response");
        }
        return std::move(*response);
    }
    std::optional<ClientEvent> next_event(std::chrono::milliseconds timeout) {
        if (timeout.count() <= 0) {
            throw std::invalid_argument("timeout must be positive");
        }
        if (!stream_->socket().is_open()) {
            throw std::runtime_error("client is not connected");
        }
        if (events_.empty()) {
            arm(timeout, true);
            std::optional<protocol::Envelope> response;
            receive(response, nullptr);
            check();
        }
        if (events_.empty()) {
            return std::nullopt;
        }
        auto message = std::move(events_.front());
        events_.pop_front();
        event_bytes_ -= message.ByteSizeLong() + 4;
        if (message.has_direct_message()) {
            const auto& dm = message.direct_message();
            return DirectDelivery{dm.sender(),
                                  dm.recipient(),
                                  dm.room_id(),
                                  dm.text(),
                                  dm.message_id(),
                                  dm.room_sequence(),
                                  dm.server_timestamp_unix_ms()};
        }
        if (message.has_group_message()) {
            const auto& group = message.group_message();
            return GroupDelivery{group.sender(),        group.group_id(),
                                 group.text(),          group.message_id(),
                                 group.room_sequence(), group.server_timestamp_unix_ms()};
        }
        if (message.has_user_list_update()) {
            const auto& names = message.user_list_update().usernames();
            return UserSnapshot{{names.begin(), names.end()}};
        }
        GroupSnapshot result;
        for (const auto& group : message.group_list_update().groups()) {
            result.groups.push_back(
                {group.group_id(), group.name(), {group.members().begin(), group.members().end()}});
        }
        return result;
    }

  private:
    boost::asio::io_context context_;
    std::unique_ptr<transport::SocketStream> stream_ =
        std::make_unique<transport::SocketStream>(tcp::socket(context_));
    tcp::resolver resolver_{context_};
    boost::asio::steady_timer timer_{context_};
    protocol::FrameDecoder decoder_;
    std::deque<protocol::Envelope> events_;
    std::size_t event_bytes_ = 0;
    std::uint64_t request_number_ = 0;
    std::string failure_;
    bool receive_timed_out_ = false;
    bool protocol_failure_ = false;
    bool tls_failure_ = false;
    transport::ClientTlsOptions tls_options_;
};
PingClient::PingClient(transport::ClientTlsOptions tls)
    : impl_(std::make_unique<Impl>(std::move(tls))) {}
PingClient::~PingClient() = default;
void PingClient::connect(const std::string& host, std::uint16_t port,
                         std::chrono::milliseconds timeout) {
    impl_->connect(host, port, timeout);
}
std::uint64_t PingClient::ping(std::uint64_t nonce, std::chrono::milliseconds timeout) {
    return impl_->ping(nonce, timeout);
}
LoginResult PingClient::login(const std::string& name, std::chrono::milliseconds timeout) {
    return impl_->login(name, timeout);
}
LoginResult PingClient::resume_login(const std::string& name, const std::string& token,
                                     std::chrono::milliseconds timeout) {
    if (!valid_resume_token(token)) {
        throw std::invalid_argument("invalid resume token");
    }
    return impl_->login(name, timeout, token);
}
HistoryPage PingClient::history(const std::string& room, std::uint64_t after, std::uint32_t limit,
                                std::chrono::milliseconds timeout) {
    return impl_->history(room, after, limit, timeout);
}
void PingClient::close() { impl_->close(); }
SyncPage PingClient::sync(const std::string& room, std::uint64_t after, std::uint64_t through,
                          std::uint32_t limit, std::chrono::milliseconds timeout) {
    return impl_->sync(room, after, through, limit, timeout);
}
DirectRoomsPage PingClient::direct_rooms(std::uint64_t after, std::uint32_t limit,
                                         std::chrono::milliseconds timeout) {
    return impl_->direct_rooms(after, limit, timeout);
}
OperationResult PingClient::send_direct(const std::string& recipient, const std::string& text,
                                        std::chrono::milliseconds timeout) {
    return send_direct_with_key(recipient, text, new_message_key(), timeout);
}
std::string PingClient::new_message_key() { return protocol::new_message_key(); }
OperationResult PingClient::send_direct_with_key(const std::string& recipient,
                                                 const std::string& text, const std::string& key,
                                                 std::chrono::milliseconds timeout) {
    if (!protocol::valid_message_key(key)) {
        throw std::invalid_argument("invalid message retry key");
    }
    auto outgoing = impl_->request("direct");
    outgoing.mutable_direct_message()->set_recipient(recipient);
    outgoing.mutable_direct_message()->set_text(text);
    outgoing.mutable_direct_message()->set_client_message_id(key);
    return impl_->operation(outgoing, timeout);
}
OperationResult PingClient::create_group(const std::string& name,
                                         std::chrono::milliseconds timeout) {
    auto outgoing = impl_->request("create");
    outgoing.mutable_create_group_request()->set_name(name);
    return impl_->operation(outgoing, timeout);
}
OperationResult PingClient::join_group(const std::string& group_id,
                                       std::chrono::milliseconds timeout) {
    auto outgoing = impl_->request("join");
    outgoing.mutable_join_group_request()->set_group_id(group_id);
    return impl_->operation(outgoing, timeout);
}
OperationResult PingClient::send_group(const std::string& group_id, const std::string& text,
                                       std::chrono::milliseconds timeout) {
    return send_group_with_key(group_id, text, new_message_key(), timeout);
}
OperationResult PingClient::send_group_with_key(const std::string& group_id,
                                                const std::string& text, const std::string& key,
                                                std::chrono::milliseconds timeout) {
    if (!protocol::valid_message_key(key)) {
        throw std::invalid_argument("invalid message retry key");
    }
    auto outgoing = impl_->request("group");
    outgoing.mutable_group_message()->set_group_id(group_id);
    outgoing.mutable_group_message()->set_text(text);
    outgoing.mutable_group_message()->set_client_message_id(key);
    return impl_->operation(outgoing, timeout);
}
std::optional<ClientEvent> PingClient::next_event(std::chrono::milliseconds timeout) {
    return impl_->next_event(timeout);
}
} // namespace chat::client
