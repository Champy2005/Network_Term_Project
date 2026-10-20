/** @file
 * @brief Own blocking network calls on one worker and bound both cross-thread queues.
 */
#include "chat/client/chat_worker.hpp"
#include "chat/client/chat_client.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace chat::client {
namespace {
std::size_t event_size(const ClientEvent& event) {
    return std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            std::size_t size = sizeof(T);
            if constexpr (std::is_same_v<T, DirectDelivery>) {
                size += value.sender.size() + value.recipient.size() + value.room_id.size() +
                        value.text.size() + value.message_id.size();
            } else if constexpr (std::is_same_v<T, GroupDelivery>) {
                size += value.sender.size() + value.group_id.size() + value.text.size() +
                        value.message_id.size();
            } else if constexpr (std::is_same_v<T, UserSnapshot>) {
                for (const auto& user : value.usernames) {
                    size += sizeof(std::string) + user.size();
                }
            } else {
                for (const auto& group : value.groups) {
                    size += sizeof(GroupInfo) + group.group_id.size() + group.name.size();
                    for (const auto& user : group.members) {
                        size += sizeof(std::string) + user.size();
                    }
                }
            }
            return size;
        },
        event);
}
std::size_t update_size(const WorkerUpdate& update) {
    return std::visit(
        [](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ClientEvent>) {
                return event_size(value);
            } else if constexpr (std::is_same_v<T, WorkerConnected>) {
                return sizeof(T) + value.username.size();
            } else if constexpr (std::is_same_v<T, WorkerDirectRoom>) {
                return sizeof(T) + value.room.room_id.size() + value.room.peer.size();
            } else if constexpr (std::is_same_v<T, WorkerDisconnected> ||
                                 std::is_same_v<T, WorkerReconnecting>) {
                return sizeof(T) + value.reason.size();
            } else {
                return sizeof(T) + value.target.size() + value.result.resource_id.size() +
                       value.result.reason.size() + value.result.message_id.size() +
                       value.result.client_message_id.size();
            }
        },
        update);
}
} // namespace
ChatWorker::ChatWorker(WorkerLimits limits, transport::ClientTlsOptions tls)
    : limits_(limits), tls_(std::move(tls)) {
    if (limits.operation_timeout.count() <= 0 || limits.poll_timeout.count() <= 0 ||
        limits.commands == 0 || limits.updates == 0 || limits.update_bytes == 0 ||
        limits.heartbeat_interval.count() < 0 ||
        limits.heartbeat_interval > std::chrono::hours(24) || limits.reconnect_attempts > 64 ||
        limits.recovery_rooms == 0 || limits.recovery_rooms > 256 ||
        limits.reconnect_delay.count() <= 0 ||
        limits.reconnect_max_delay < limits.reconnect_delay ||
        limits.reconnect_max_delay > std::chrono::seconds(60)) {
        throw std::invalid_argument(
            "invalid worker limits, heartbeat interval, or recovery bounds");
    }
}
ChatWorker::~ChatWorker() {
    disconnect();
    if (thread_.joinable()) {
        thread_.join();
    }
}
bool ChatWorker::start(std::string host, std::uint16_t port, std::string username) {
    if (host.empty() || host.size() > 255 || port == 0 || username.empty() ||
        username.size() > 32 || running_.exchange(true)) {
        return false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    {
        std::lock_guard lock(mutex_);
        commands_.clear();
        updates_.clear();
        terminal_.reset();
        bytes_ = 0;
    }
    try {
        thread_ =
            std::jthread([this, host = std::move(host), port, username = std::move(username)](
                             const std::stop_token& stop) { run(stop, host, port, username); });
    } catch (...) {
        running_ = false;
        throw;
    }
    return true;
}
void ChatWorker::disconnect() {
    if (thread_.joinable()) {
        thread_.request_stop();
    }
}
std::optional<std::uint64_t> ChatWorker::submit(CommandKind kind, std::string target,
                                                std::string text) {
    if (kind != CommandKind::direct && kind != CommandKind::group &&
        kind != CommandKind::create_group && kind != CommandKind::join_group) {
        return std::nullopt;
    }
    if (target.empty() || target.size() > 64 || text.size() > 4096 ||
        ((kind == CommandKind::direct || kind == CommandKind::group) && text.empty())) {
        return std::nullopt;
    }
    std::lock_guard lock(mutex_);
    if (!connected_ || thread_.get_stop_token().stop_requested() ||
        commands_.size() >= limits_.commands) {
        return std::nullopt;
    }
    const auto id = ++next_command_;
    const auto key = kind == CommandKind::direct || kind == CommandKind::group
                         ? ChatClient::new_message_key()
                         : std::string{};
    commands_.push_back({id, kind, std::move(target), std::move(text), key});
    return id;
}
std::vector<WorkerUpdate> ChatWorker::poll() {
    std::lock_guard lock(mutex_);
    auto result = std::move(updates_);
    updates_.clear();
    bytes_ = 0;
    if (terminal_) {
        result.push_back(std::move(*terminal_));
        terminal_.reset();
    }
    return result;
}
bool ChatWorker::emit(WorkerUpdate update) {
    const auto size = update_size(update);
    std::lock_guard lock(mutex_);
    if (updates_.size() >= limits_.updates || size > limits_.update_bytes - bytes_) {
        return false;
    }
    bytes_ += size;
    updates_.push_back(std::move(update));
    return true;
}
void ChatWorker::run(const std::stop_token& stop, const std::string& host, std::uint16_t port,
                     const std::string& username) {
    ChatClient client(tls_);
    std::string reason = "Disconnected";
    std::string token;
    struct Cursor {
        bool group = false;
        std::uint64_t sequence = 0;
    };
    std::map<std::string, Cursor> rooms;
    std::set<std::string> wanted_groups;
    std::optional<Command> active;
    const auto publish = [&](WorkerUpdate update) {
        if (!emit(std::move(update))) {
            throw std::runtime_error("UI update queue limit exceeded");
        }
    };
    const auto track = [&](const std::string& id, bool group) -> Cursor& {
        if (!rooms.contains(id) && rooms.size() >= limits_.recovery_rooms) {
            throw std::runtime_error("Recovery room limit exceeded");
        }
        return rooms.try_emplace(id, Cursor{group, 0}).first->second;
    };
    const auto replay = [&](const std::string& id, Cursor& cursor, std::uint64_t through = 0) {
        bool more = true;
        while (more && !stop.stop_requested()) {
            const auto page =
                client.sync(id, cursor.sequence, through, 50, limits_.operation_timeout);
            if (!page.accepted) {
                throw std::runtime_error("Recovery sync rejected: " + page.reason);
            }
            through = page.through_sequence;
            for (const auto& record : page.records) {
                if (stop.stop_requested()) {
                    return;
                }
                if (cursor.group) {
                    publish(ClientEvent{GroupDelivery{record.sender, id, record.text,
                                                      record.message_id, record.room_sequence,
                                                      record.server_timestamp_unix_ms}});
                } else {
                    if (record.sender != username && record.recipient != username) {
                        throw std::runtime_error("Recovery direct room identity mismatch");
                    }
                    publish(ClientEvent{DirectDelivery{
                        record.sender, record.recipient, id, record.text, record.message_id,
                        record.room_sequence, record.server_timestamp_unix_ms}});
                }
                // Advance only after the UI update has been admitted.
                cursor.sequence = record.room_sequence;
            }
            more = page.has_more;
        }
    };
    const auto dispatch = [&](ClientEvent event) {
        if (!token.empty()) {
            if (const auto* groups = std::get_if<GroupSnapshot>(&event)) {
                for (const auto& group : groups->groups) {
                    if (std::find(group.members.begin(), group.members.end(), username) !=
                        group.members.end()) {
                        wanted_groups.insert(group.group_id);
                        static_cast<void>(track(group.group_id, true));
                    }
                }
            }
            const auto* direct = std::get_if<DirectDelivery>(&event);
            const auto* group = std::get_if<GroupDelivery>(&event);
            if (direct || group) {
                const auto id = direct ? direct->room_id : group->group_id;
                const auto sequence = direct ? direct->room_sequence : group->room_sequence;
                auto& cursor = track(id, group != nullptr);
                if (sequence <= cursor.sequence) {
                    return;
                }
                if (sequence > cursor.sequence + 1) {
                    replay(id, cursor, sequence - 1);
                }
                if (stop.stop_requested()) {
                    return;
                }
                publish(std::move(event));
                cursor.sequence = sequence;
                return;
            }
        }
        publish(std::move(event));
    };
    const auto synchronize = [&] {
        std::uint64_t after = 0;
        bool more = true;
        while (more && !stop.stop_requested()) {
            const auto page = client.direct_rooms(after, 64, limits_.operation_timeout);
            if (!page.accepted) {
                throw std::runtime_error("Recovery room discovery rejected: " + page.reason);
            }
            for (const auto& room : page.rooms) {
                static_cast<void>(track(room.room_id, false));
                publish(WorkerDirectRoom{room});
            }
            after = page.next_room_id;
            more = page.has_more;
        }
        // Exchanges buffer the ordered login/join snapshots. Apply them before group replay.
        for (std::size_t n = 0; n < 256 && !stop.stop_requested(); ++n) {
            auto event = client.next_event(std::chrono::milliseconds{1});
            if (!event) {
                break;
            }
            dispatch(std::move(*event));
        }
        for (auto& [id, cursor] : rooms) {
            if (stop.stop_requested()) {
                break;
            }
            replay(id, cursor);
        }
    };
    try {
        client.connect(host, port, limits_.operation_timeout);
        if (!stop.stop_requested()) {
            const auto login = client.login(username, limits_.operation_timeout);
            if (!login.accepted) {
                throw std::runtime_error("Login rejected: " + login.reason);
            }
            token = login.resume_token;
            connected_ = true;
            publish(WorkerConnected{login.username});
        }
        auto next_heartbeat = std::chrono::steady_clock::now() + limits_.heartbeat_interval;
        std::uint64_t nonce = 0;
        std::size_t attempts = 0;
        auto delay = limits_.reconnect_delay;
        bool recovering = false;
        while (!stop.stop_requested()) {
            try {
                if (recovering) {
                    if (attempts == limits_.reconnect_attempts) {
                        throw std::runtime_error("Reconnect attempts exhausted");
                    }
                    publish(WorkerReconnecting{reason.substr(0, 256), ++attempts});
                    {
                        std::unique_lock lock(mutex_);
                        wake_.wait_for(lock, stop, delay, [] { return false; });
                    }
                    if (stop.stop_requested()) {
                        break;
                    }
                    delay = std::min(delay * 2, limits_.reconnect_max_delay);
                    client.connect(host, port, limits_.operation_timeout);
                    const auto login =
                        client.resume_login(username, token, limits_.operation_timeout);
                    if (!login.accepted) {
                        if (login.reason == "username is already connected") {
                            throw NetworkError(login.reason);
                        }
                        throw std::runtime_error("Resume rejected: " + login.reason);
                    }
                    for (const auto& id : wanted_groups) {
                        if (stop.stop_requested()) {
                            break;
                        }
                        const auto joined = client.join_group(id, limits_.operation_timeout);
                        if (!joined.accepted && joined.error != RequestError::already_member) {
                            throw std::runtime_error("Recovery join rejected: " + joined.reason);
                        }
                    }
                    synchronize();
                    if (stop.stop_requested()) {
                        break;
                    }
                    publish(WorkerConnected{username, true});
                    connected_ = true;
                    recovering = false;
                    attempts = 0;
                    delay = limits_.reconnect_delay;
                    next_heartbeat = std::chrono::steady_clock::now() + limits_.heartbeat_interval;
                }
                if (limits_.heartbeat_interval.count() != 0 &&
                    std::chrono::steady_clock::now() >= next_heartbeat) {
                    try {
                        static_cast<void>(client.ping(++nonce, limits_.operation_timeout));
                    } catch (const NetworkError& error) {
                        throw NetworkError(std::string("Heartbeat failed: ") + error.what());
                    } catch (const std::runtime_error& error) {
                        throw std::runtime_error(std::string("Heartbeat failed: ") + error.what());
                    }
                    next_heartbeat = std::chrono::steady_clock::now() + limits_.heartbeat_interval;
                }
                if (stop.stop_requested()) {
                    break;
                }
                if (!active) {
                    std::lock_guard lock(mutex_);
                    if (!commands_.empty()) {
                        active = std::move(commands_.front());
                        commands_.pop_front();
                    }
                }
                if (active && !stop.stop_requested()) {
                    OperationResult result;
                    switch (active->kind) {
                    case CommandKind::direct:
                        result = client.send_direct_with_key(active->target, active->text,
                                                             active->message_key,
                                                             limits_.operation_timeout);
                        break;
                    case CommandKind::group:
                        result = client.send_group_with_key(active->target, active->text,
                                                            active->message_key,
                                                            limits_.operation_timeout);
                        break;
                    case CommandKind::create_group:
                        result = client.create_group(active->target, limits_.operation_timeout);
                        break;
                    case CommandKind::join_group:
                        result = client.join_group(active->target, limits_.operation_timeout);
                        break;
                    }
                    if (result.accepted && !token.empty()) {
                        if (active->kind == CommandKind::create_group ||
                            active->kind == CommandKind::join_group) {
                            wanted_groups.insert(result.resource_id);
                            static_cast<void>(track(result.resource_id, true));
                        } else {
                            static_cast<void>(
                                track(result.resource_id, active->kind == CommandKind::group));
                        }
                    }
                    publish(WorkerCompleted{active->id, active->kind, active->target,
                                            std::move(result)});
                    active.reset();
                }
                if (stop.stop_requested()) {
                    break;
                }
                auto poll_timeout = limits_.poll_timeout;
                if (limits_.heartbeat_interval.count() != 0) {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        next_heartbeat - std::chrono::steady_clock::now());
                    poll_timeout =
                        std::min(poll_timeout, std::max(remaining, std::chrono::milliseconds{1}));
                }
                if (auto event = client.next_event(poll_timeout)) {
                    dispatch(std::move(*event));
                }
            } catch (const NetworkError& error) {
                if (token.empty() || limits_.reconnect_attempts == 0) {
                    throw;
                }
                reason = error.what();
                connected_ = false;
                client.close();
                // Only messages have durable retry keys. Never blindly duplicate group creation.
                if (active && (active->kind == CommandKind::create_group ||
                               active->kind == CommandKind::join_group)) {
                    if (active->kind == CommandKind::join_group) {
                        wanted_groups.insert(active->target);
                    }
                    OperationResult result;
                    result.reason = "Connection lost; operation outcome unknown";
                    publish(WorkerCompleted{active->id, active->kind, active->target,
                                            std::move(result)});
                    active.reset();
                }
                recovering = true;
            }
        }
        if (stop.stop_requested()) {
            reason = "Disconnected";
        }
    } catch (const std::exception& error) {
        reason = error.what();
    }
    client.close();
    connected_ = false;
    {
        std::lock_guard lock(mutex_);
        commands_.clear();
        // Reserve a separate single terminal slot: overflow cannot hide the reason.
        terminal_ = WorkerDisconnected{reason.substr(0, 256)};
        // Publish ended state before poll can observe the terminal update.
        running_ = false;
    }
}
} // namespace chat::client
