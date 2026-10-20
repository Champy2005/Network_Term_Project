/** @file
 * @brief Validate identity and membership, route private text, and publish state snapshots.
 */
#include "chat/server/chat_hub.hpp"
#include "chat/protocol/message_identity.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace chat::server {
namespace {
constexpr std::size_t kMaxTextBytes = 4096;
constexpr std::size_t kMaxGroups = 128;

protocol::Envelope envelope(const std::string& request_id = {}) {
    protocol::Envelope message;
    message.set_protocol_version(1);
    message.set_request_id(request_id);
    return message;
}

bool valid_username(const std::string& name) {
    return !name.empty() && name.size() <= 32 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_';
           });
}

bool valid_text(const std::string& text) { return !text.empty() && text.size() <= kMaxTextBytes; }

bool valid_group_name(const std::string& name) {
    return !name.empty() && name.size() <= 64 && name.find_first_not_of(' ') != std::string::npos &&
           std::none_of(name.begin(), name.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}
} // namespace

ChatHub::ChatHub(const std::string& database_path) {
    message_namespace_ = protocol::new_message_key();
    if (!database_path.empty()) {
        storage_ = std::make_unique<HistoryStore>(database_path);
        for (const auto& group : storage_->groups()) {
            groups_.emplace(group.id, Group{group.name, {}});
        }
    }
}
void ChatHub::attach(std::shared_ptr<ClientSession> session) {
    auto* key = session.get();
    connections_.emplace(key, Connection{std::move(session), {}, ++next_connection_});
}

void ChatHub::stop() {
    stopping_ = true;
    std::vector<std::shared_ptr<ClientSession>> sessions;
    sessions.reserve(connections_.size());
    for (const auto& [key, connection] : connections_) {
        sessions.push_back(connection.session);
    }
    for (const auto& session : sessions) {
        session->stop();
    }
}

void ChatHub::remove(ClientSession& session) {
    const auto found = connections_.find(&session);
    if (found == connections_.end()) {
        return;
    }
    const auto id = found->second.id;
    const bool registered = !found->second.username.empty();
    usernames_.erase(found->second.username);
    bool changed_groups = false;
    for (auto& [group_id, group] : groups_) {
        changed_groups |= group.members.erase(&session) != 0;
    }
    std::erase_if(direct_rooms_, [id](const auto& room) {
        return room.first.first == id || room.first.second == id;
    });
    connections_.erase(found);
    if (!stopping_) {
        broadcast(registered, changed_groups);
    }
}

void ChatHub::error(ClientSession& session, const protocol::Envelope& request,
                    protocol::ErrorCode code, const std::string& reason) {
    auto reply = envelope(request.request_id());
    reply.mutable_error()->set_code(code);
    reply.mutable_error()->set_message(reason);
    static_cast<void>(session.send(reply));
}

void ChatHub::acknowledge(ClientSession& session, const protocol::Envelope& request,
                          const std::string& resource) {
    auto reply = envelope(request.request_id());
    reply.mutable_ack()->set_resource_id(resource);
    static_cast<void>(session.send(reply));
}

void ChatHub::handle(ClientSession& session, const protocol::Envelope& request) {
    if (stopping_ || !connections_.contains(&session)) {
        session.stop();
        return;
    }
    try {
        if (request.has_ping()) {
            auto reply = envelope(request.request_id());
            reply.mutable_pong()->set_nonce(request.ping().nonce());
            static_cast<void>(session.send(reply));
        } else if (request.has_login_request()) {
            login(session, request);
        } else if (request.has_direct_message() || request.has_group_message() ||
                   request.has_create_group_request() || request.has_join_group_request() ||
                   request.has_history_request() || request.has_sync_request() ||
                   request.has_direct_rooms_request()) {
            if (connections_.at(&session).username.empty()) {
                error(session, request, protocol::ERROR_CODE_NOT_LOGGED_IN, "login is required");
            } else if (request.has_direct_message()) {
                direct(session, request);
            } else if (request.has_create_group_request()) {
                create_group(session, request);
            } else if (request.has_join_group_request()) {
                join_group(session, request);
            } else if (request.has_history_request()) {
                history(session, request);
            } else if (request.has_sync_request()) {
                sync(session, request);
            } else if (request.has_direct_rooms_request()) {
                direct_rooms(session, request);
            } else {
                group_message(session, request);
            }
        } else {
            session.stop();
        }
    } catch (const MessageIdConflict&) {
        error(session, request, protocol::ERROR_CODE_MESSAGE_ID_CONFLICT,
              "message retry key conflicts with accepted content");
    } catch (const std::runtime_error&) {
        // Never leak database paths, SQL, stored text, or capabilities in errors.
        error(session, request, protocol::ERROR_CODE_INTERNAL_ERROR, "history storage unavailable");
    }
}

void ChatHub::acknowledge(ClientSession& session, const protocol::Envelope& request,
                          const StoredMessage& receipt) {
    auto reply = envelope(request.request_id());
    auto* ack = reply.mutable_ack();
    ack->set_resource_id(receipt.room_id);
    ack->set_message_id(receipt.message_id);
    ack->set_room_sequence(receipt.sequence);
    static_cast<void>(session.send(reply));
}
std::optional<StoredMessage> ChatHub::retry(Connection& sender, const std::string& key, bool group,
                                            const std::string& target, const std::string& text) {
    if (storage_) {
        return storage_->retry(sender.principal, key, group, target, text);
    }
    if (!key.empty()) {
        for (const auto& prior : sender.recent) {
            if (prior.key == key) {
                if (prior.group != group || prior.target != target || prior.text != text) {
                    throw MessageIdConflict();
                }
                auto receipt = prior.receipt;
                receipt.duplicate = true;
                return receipt;
            }
        }
    }
    return std::nullopt;
}
void ChatHub::remember(Connection& sender, const std::string& key, bool group,
                       const std::string& target, const std::string& text,
                       const StoredMessage& receipt) {
    if (!storage_ && !key.empty()) {
        if (sender.recent.size() == 256) {
            sender.recent.pop_front();
        }
        sender.recent.push_back({key, target, text, group, receipt});
    }
}

void ChatHub::login(ClientSession& session, const protocol::Envelope& request) {
    auto& connection = connections_.at(&session);
    const auto& name = request.login_request().username();
    auto reply = envelope(request.request_id());
    auto* result = reply.mutable_login_response();
    if (!connection.username.empty()) {
        result->set_reason("connection is already logged in");
    } else if (!valid_username(name)) {
        result->set_reason("username must contain 1-32 ASCII letters, digits, or underscores");
    } else if (usernames_.contains(name)) {
        result->set_reason("username is already connected");
    } else {
        try {
            if (storage_) {
                const auto identity =
                    storage_->identity(name, request.login_request().resume_token());
                connection.principal = identity.id;
                result->set_resume_token(identity.resume_token);
            } else if (!request.login_request().resume_token().empty()) {
                throw std::invalid_argument("resume requires storage-enabled server");
            }
        } catch (const std::invalid_argument& error) {
            result->set_reason(error.what());
            static_cast<void>(session.send(reply));
            return;
        } catch (const std::runtime_error&) {
            result->set_reason("history storage unavailable");
            static_cast<void>(session.send(reply));
            return;
        }
        connection.username = name;
        usernames_.emplace(name, &session);
        result->set_accepted(true);
        result->set_username(name);
    }
    const bool accepted = result->accepted();
    static_cast<void>(session.send(reply));
    if (accepted) {
        broadcast(true, true);
    }
}

void ChatHub::direct(ClientSession& session, const protocol::Envelope& request) {
    const auto& message = request.direct_message();
    if (!valid_username(message.recipient()) || !valid_text(message.text()) ||
        !message.sender().empty() || !message.room_id().empty() || !message.message_id().empty() ||
        message.room_sequence() != 0 || message.server_timestamp_unix_ms() != 0 ||
        (!message.client_message_id().empty() &&
         !protocol::valid_message_key(message.client_message_id()))) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid direct message fields");
        return;
    }
    auto& sender = connections_.at(&session);
    if (const auto prior = retry(sender, message.client_message_id(), false, message.recipient(),
                                 message.text())) {
        acknowledge(session, request, *prior);
        return;
    }
    const auto target = usernames_.find(message.recipient());
    if (target == usernames_.end()) {
        error(session, request, protocol::ERROR_CODE_USER_NOT_FOUND, "recipient is not connected");
        return;
    }
    const auto& recipient = connections_.at(target->second);
    const Pair pair{std::min(sender.id, recipient.id), std::max(sender.id, recipient.id)};
    StoredMessage receipt;
    if (storage_) {
        receipt = storage_->direct(sender.principal, recipient.principal, sender.username,
                                   recipient.username, message.text(), message.client_message_id());
    } else {
        auto room = direct_rooms_.find(pair);
        if (room == direct_rooms_.end()) {
            room = direct_rooms_
                       .emplace(pair, DirectRoom{"direct-" + std::to_string(++next_direct_room_)})
                       .first;
        }
        receipt = {room->second.id, message_namespace_ + "-" + std::to_string(++next_message_),
                   ++room->second.sequence, protocol::message_timestamp(), false};
    }
    remember(sender, message.client_message_id(), false, message.recipient(), message.text(),
             receipt);
    if (receipt.duplicate) {
        acknowledge(session, request, receipt);
        return;
    }
    auto delivery = envelope();
    auto* direct_message = delivery.mutable_direct_message();
    direct_message->set_recipient(recipient.username);
    direct_message->set_sender(sender.username);
    direct_message->set_room_id(receipt.room_id);
    direct_message->set_text(message.text());
    direct_message->set_message_id(receipt.message_id);
    direct_message->set_room_sequence(receipt.sequence);
    direct_message->set_server_timestamp_unix_ms(receipt.timestamp);
    auto recipient_session = recipient.session;
    acknowledge(session, request, receipt);
    static_cast<void>(session.send(delivery));
    if (recipient_session.get() != &session) {
        static_cast<void>(recipient_session->send(delivery));
    }
}

void ChatHub::create_group(ClientSession& session, const protocol::Envelope& request) {
    const auto& name = request.create_group_request().name();
    if (!valid_group_name(name) || groups_.size() >= kMaxGroups) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid group name or group limit reached");
        return;
    }
    const auto id = storage_ ? storage_->create_group(name, connections_.at(&session).principal)
                             : "group-" + std::to_string(++next_group_);
    groups_.emplace(id, Group{name, {&session}});
    acknowledge(session, request, id);
    broadcast(false, true);
}

void ChatHub::join_group(ClientSession& session, const protocol::Envelope& request) {
    const auto& id = request.join_group_request().group_id();
    const auto found = groups_.find(id);
    if (found == groups_.end()) {
        error(session, request, protocol::ERROR_CODE_GROUP_NOT_FOUND, "group does not exist");
    } else if (found->second.members.contains(&session)) {
        error(session, request, protocol::ERROR_CODE_ALREADY_MEMBER, "already a group member");
    } else {
        if (storage_) {
            storage_->join_group(id, connections_.at(&session).principal);
        }
        found->second.members.insert(&session);
        acknowledge(session, request, id);
        broadcast(false, true);
    }
}

void ChatHub::group_message(ClientSession& session, const protocol::Envelope& request) {
    const auto& message = request.group_message();
    if (!valid_text(message.text()) || !message.sender().empty() || !message.message_id().empty() ||
        message.room_sequence() != 0 || message.server_timestamp_unix_ms() != 0 ||
        (!message.client_message_id().empty() &&
         !protocol::valid_message_key(message.client_message_id()))) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid group message fields");
        return;
    }
    const auto found = groups_.find(message.group_id());
    if (found == groups_.end()) {
        error(session, request, protocol::ERROR_CODE_GROUP_NOT_FOUND, "group does not exist");
        return;
    }
    if (!found->second.members.contains(&session)) {
        error(session, request, protocol::ERROR_CODE_NOT_GROUP_MEMBER,
              "group membership is required");
        return;
    }
    auto& sender = connections_.at(&session);
    if (const auto prior =
            retry(sender, message.client_message_id(), true, message.group_id(), message.text())) {
        acknowledge(session, request, *prior);
        return;
    }
    const auto receipt =
        storage_ ? storage_->group_message(found->first, sender.principal, sender.username,
                                           message.text(), message.client_message_id())
                 : StoredMessage{found->first,
                                 message_namespace_ + "-" + std::to_string(++next_message_),
                                 ++found->second.sequence, protocol::message_timestamp(), false};
    remember(sender, message.client_message_id(), true, message.group_id(), message.text(),
             receipt);
    if (receipt.duplicate) {
        acknowledge(session, request, receipt);
        return;
    }
    auto delivery = envelope();
    auto* group_message = delivery.mutable_group_message();
    group_message->set_group_id(found->first);
    group_message->set_sender(connections_.at(&session).username);
    group_message->set_text(message.text());
    group_message->set_message_id(receipt.message_id);
    group_message->set_room_sequence(receipt.sequence);
    group_message->set_server_timestamp_unix_ms(receipt.timestamp);
    std::vector<std::shared_ptr<ClientSession>> recipients;
    for (auto* member : found->second.members) {
        recipients.push_back(connections_.at(member).session);
    }
    acknowledge(session, request, receipt);
    for (const auto& recipient : recipients) {
        static_cast<void>(recipient->send(delivery));
    }
}

void ChatHub::history(ClientSession& session, const protocol::Envelope& request) {
    const auto& read = request.history_request();
    if (!storage_ || read.room_id().empty() || read.room_id().size() > 64 || read.limit() > 100 ||
        read.after_id() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid history request or storage disabled");
        return;
    }
    bool permitted = false;
    if (read.room_id().starts_with("group-")) {
        const auto group = groups_.find(read.room_id());
        permitted = group != groups_.end() && group->second.members.contains(&session);
    } else {
        permitted =
            storage_->direct_participant(read.room_id(), connections_.at(&session).principal);
    }
    if (!permitted) {
        error(session, request, protocol::ERROR_CODE_HISTORY_FORBIDDEN,
              "room history is not available to this connection");
        return;
    }
    auto response = envelope(request.request_id());
    *response.mutable_history_response() =
        storage_->history(read.room_id(), read.after_id(), read.limit() == 0 ? 50 : read.limit());
    static_cast<void>(session.send(response));
}

void ChatHub::sync(ClientSession& session, const protocol::Envelope& request) {
    const auto& query = request.sync_request();
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!storage_ || query.room_id().empty() || query.room_id().size() > 64 ||
        query.limit() > 100 || query.after_sequence() > maximum ||
        query.through_sequence() > maximum) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid sync request or storage disabled");
        return;
    }
    const auto group = groups_.find(query.room_id());
    const bool permitted =
        query.room_id().starts_with("group-")
            ? group != groups_.end() && group->second.members.contains(&session)
            : storage_->direct_participant(query.room_id(), connections_.at(&session).principal);
    if (!permitted) {
        error(session, request, protocol::ERROR_CODE_HISTORY_FORBIDDEN,
              "room is not available to this connection");
        return;
    }
    try {
        auto response = envelope(request.request_id());
        *response.mutable_sync_response() =
            storage_->sync(query.room_id(), query.after_sequence(), query.through_sequence(),
                           query.limit() == 0 ? 50 : query.limit());
        static_cast<void>(session.send(response));
    } catch (const std::invalid_argument&) {
        error(session, request, protocol::ERROR_CODE_SYNC_GAP,
              "room sequence cannot be recovered contiguously");
    }
}
void ChatHub::direct_rooms(ClientSession& session, const protocol::Envelope& request) {
    const auto& query = request.direct_rooms_request();
    if (!storage_ || query.limit() > 64 ||
        query.after_room_id() >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        error(session, request, protocol::ERROR_CODE_INVALID_REQUEST,
              "invalid room discovery or storage disabled");
        return;
    }
    auto response = envelope(request.request_id());
    std::vector<std::int64_t> connected;
    for (const auto& [client, connection] : connections_) {
        static_cast<void>(client);
        if (!connection.username.empty()) {
            connected.push_back(connection.principal);
        }
    }
    *response.mutable_direct_rooms_response() =
        storage_->direct_rooms(connections_.at(&session).principal, query.after_room_id(),
                               query.limit() == 0 ? 64 : query.limit(), connected);
    static_cast<void>(session.send(response));
}
void ChatHub::broadcast(bool users, bool groups) {
    auto users_update = envelope();
    auto groups_update = envelope();
    if (users) {
        auto* list = users_update.mutable_user_list_update();
        for (const auto& [name, client] : usernames_) {
            list->add_usernames(name);
        }
    }
    if (groups) {
        auto* list = groups_update.mutable_group_list_update();
        for (const auto& [id, group] : groups_) {
            auto* info = list->add_groups();
            info->set_group_id(id);
            info->set_name(group.name);
            std::vector<std::string> members;
            members.reserve(group.members.size());
            for (auto* member : group.members) {
                members.push_back(connections_.at(member).username);
            }
            std::sort(members.begin(), members.end());
            for (const auto& name : members) {
                info->add_members(name);
            }
        }
    }
    for (const auto& [name, client] : usernames_) {
        if (users) {
            static_cast<void>(client->send(users_update));
        }
        if (groups) {
            static_cast<void>(client->send(groups_update));
        }
    }
}

} // namespace chat::server
