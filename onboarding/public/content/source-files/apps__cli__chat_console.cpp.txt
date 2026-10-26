/** @file
 * @brief Keep command syntax, bounded input, and terminal-safe output separate from networking.
 */
#include "chat_console.hpp"
#include <algorithm>
#include <type_traits>

namespace chat::cli {
namespace {
constexpr std::string_view help =
    "Commands: /help /users /groups /members ID /dm USER TEXT /create NAME "
    "/join ID /group ID TEXT /quit";
std::string_view token(std::string_view& input) {
    const auto first = input.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        input = {};
        return {};
    }
    input.remove_prefix(first);
    const auto end = input.find_first_of(" \t");
    const auto result = input.substr(0, end);
    input.remove_prefix(result.size());
    const auto next = input.find_first_not_of(" \t");
    input = next == std::string_view::npos ? std::string_view{} : input.substr(next);
    return result;
}
std::string_view kind(client::CommandKind value) {
    switch (value) {
    case client::CommandKind::direct:
        return "dm";
    case client::CommandKind::group:
        return "group";
    case client::CommandKind::create_group:
        return "create";
    case client::CommandKind::join_group:
        return "join";
    }
    return "unknown";
}
} // namespace
std::string escape_text(std::string_view text) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char byte : text) {
        if (byte == '"' || byte == '\\') {
            result += '\\';
            result += static_cast<char>(byte);
        } else if (byte < 32 || byte == 127) {
            result += "\\x";
            result += hex[byte >> 4];
            result += hex[byte & 15];
        } else {
            result += static_cast<char>(byte);
        }
    }
    result += '"';
    return result;
}
std::vector<InputLine> LineInput::feed(std::string_view bytes) {
    if (bytes.size() > kInputChunk) {
        throw std::invalid_argument("input chunk exceeds limit");
    }
    std::vector<InputLine> result;
    for (const char byte : bytes) {
        if (byte == '\n' && after_cr_) {
            after_cr_ = false;
            continue;
        }
        after_cr_ = byte == '\r';
        if (byte == '\r' || byte == '\n') {
            result.push_back({std::move(line_), oversized_});
            line_.clear();
            oversized_ = false;
        } else if (!oversized_) {
            if (byte == '\b') {
                if (!line_.empty()) {
                    auto position = line_.size() - 1;
                    while (position > 0 &&
                           (static_cast<unsigned char>(line_[position]) & 0xc0U) == 0x80U) {
                        --position;
                    }
                    line_.resize(position);
                }
            } else if (line_.size() == kLineBytes) {
                oversized_ = true;
                line_.clear();
            } else {
                line_ += byte;
            }
        }
    }
    return result;
}
std::vector<InputLine> LineInput::finish() {
    if (line_.empty() && !oversized_) {
        return {};
    }
    std::vector<InputLine> result{{std::move(line_), oversized_}};
    line_.clear();
    oversized_ = false;
    after_cr_ = false;
    return result;
}
Command parse_command(std::string_view line) {
    if (line.size() > kLineBytes || std::any_of(line.begin(), line.end(), [](unsigned char c) {
            return (c < 32 && c != '\t') || c == 127;
        })) {
        throw std::invalid_argument("invalid command line or control character");
    }
    const auto verb = token(line);
    Command result;
    if (verb.empty()) {
        return result;
    }
    if (verb == "/help") {
        result.action = Action::help;
    } else if (verb == "/users") {
        result.action = Action::users;
    } else if (verb == "/groups") {
        result.action = Action::groups;
    } else if (verb == "/quit") {
        result.action = Action::quit;
    } else if (verb == "/members" || verb == "/join" || verb == "/dm" || verb == "/group") {
        result.action = verb == "/members" ? Action::members
                        : verb == "/join"  ? Action::join
                        : verb == "/dm"    ? Action::direct
                                           : Action::group;
        result.target = token(line);
        if (result.target.empty() || result.target.size() > 64) {
            throw std::invalid_argument("command requires a target of 1..64 bytes");
        }
        if (result.action == Action::direct || result.action == Action::group) {
            if (line.empty() || line.size() > 4096) {
                throw std::invalid_argument("message requires 1..4096 UTF-8 bytes");
            }
            result.text = line;
            line = {};
        }
    } else if (verb == "/create") {
        if (line.empty() || line.size() > 64) {
            throw std::invalid_argument("group name requires 1..64 UTF-8 bytes");
        }
        result.action = Action::create;
        result.target = line;
        line = {};
    } else {
        throw std::invalid_argument("unknown command; use /help");
    }
    if (!line.empty()) {
        throw std::invalid_argument("unexpected command arguments");
    }
    return result;
}
ChatConsole::ChatConsole(std::ostream& output, client::WorkerLimits limits,
                         transport::ClientTlsOptions tls, std::chrono::milliseconds drain_timeout)
    : output_(output), worker_(limits, std::move(tls)), drain_timeout_(drain_timeout) {
    if (drain_timeout.count() <= 0 || drain_timeout > std::chrono::seconds(60)) {
        throw std::invalid_argument("CLI drain timeout must be 1..60000 ms");
    }
}
bool ChatConsole::start(std::string host, std::uint16_t port, std::string username) {
    if (started_ || finished_ || quitting_) {
        return false;
    }
    started_ = worker_.start(std::move(host), port, std::move(username));
    return started_;
}
void ChatConsole::groups() const {
    output_ << "GROUPS " << groups_.groups.size() << '\n';
    for (const auto& group : groups_.groups) {
        output_ << "GROUP " << escape_text(group.group_id) << ' ' << escape_text(group.name)
                << " MEMBERS";
        for (const auto& member : group.members) {
            output_ << ' ' << escape_text(member);
        }
        output_ << '\n';
    }
}
void ChatConsole::event(const client::ClientEvent& event) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, client::UserSnapshot>) {
                users_ = value;
                output_ << "USERS";
                for (const auto& user : users_.usernames) {
                    output_ << ' ' << escape_text(user);
                }
                output_ << '\n';
            } else if constexpr (std::is_same_v<T, client::GroupSnapshot>) {
                groups_ = value;
                groups();
            } else if constexpr (std::is_same_v<T, client::DirectDelivery>) {
                output_ << "DM " << escape_text(value.room_id) << " seq=" << value.room_sequence
                        << ' ' << escape_text(value.sender) << " -> "
                        << escape_text(value.recipient) << ": " << escape_text(value.text) << '\n';
            } else {
                output_ << "GROUP_MESSAGE " << escape_text(value.group_id)
                        << " seq=" << value.room_sequence << ' ' << escape_text(value.sender)
                        << ": " << escape_text(value.text) << '\n';
            }
        },
        event);
}
void ChatConsole::update(const client::WorkerUpdate& update) {
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, client::WorkerConnected>) {
                connected_ = true;
                output_ << (value.resumed ? "RECONNECTED " : "CONNECTED ")
                        << escape_text(value.username) << '\n';
                if (!value.resumed) {
                    output_ << help << '\n';
                }
            } else if constexpr (std::is_same_v<T, client::WorkerReconnecting>) {
                connected_ = false;
                output_ << "RECONNECTING attempt=" << value.attempt << ' '
                        << escape_text(value.reason) << '\n';
            } else if constexpr (std::is_same_v<T, client::WorkerDisconnected>) {
                finished_ = true;
                connected_ = false;
                if (!quitting_ || !pending_.empty()) {
                    exit_code_ = 1;
                }
                output_ << "DISCONNECTED " << escape_text(value.reason) << '\n';
            } else if constexpr (std::is_same_v<T, client::WorkerCompleted>) {
                pending_.erase(value.id);
                if (value.result.accepted) {
                    output_ << "OK " << value.id << ' ' << kind(value.kind)
                            << " room=" << escape_text(value.result.resource_id)
                            << " seq=" << value.result.room_sequence << '\n';
                } else {
                    exit_code_ = 1;
                    output_ << "REJECTED " << value.id << ' ' << kind(value.kind)
                            << " code=" << static_cast<int>(value.result.error) << ' '
                            << escape_text(value.result.reason) << '\n';
                }
            } else if constexpr (std::is_same_v<T, client::WorkerDirectRoom>) {
                output_ << "DIRECT_ROOM " << escape_text(value.room.room_id)
                        << " peer=" << escape_text(value.room.peer)
                        << " online=" << value.room.peer_connected << '\n';
            } else {
                event(value);
            }
        },
        update);
}
void ChatConsole::poll() {
    for (const auto& value : worker_.poll()) {
        update(value);
    }
    if (quitting_ && !finished_) {
        if (pending_.empty()) {
            worker_.disconnect();
        } else if (std::chrono::steady_clock::now() >= drain_until_) {
            output_ << "ERROR pending operations cancelled at exit deadline\n";
            exit_code_ = 1;
            worker_.disconnect();
            // Avoid repeating the same deadline diagnostic on each poll.
            drain_until_ = std::chrono::steady_clock::time_point::max();
        }
    }
    output_.flush();
}
void ChatConsole::request_exit() {
    if (quitting_) {
        if (!pending_.empty()) {
            exit_code_ = 1;
        }
        worker_.disconnect();
    } else {
        quitting_ = true;
        drain_until_ = std::chrono::steady_clock::now() + drain_timeout_;
    }
}
void ChatConsole::execute(const InputLine& line) {
    if (quitting_ || finished_) {
        return;
    }
    try {
        if (line.oversized) {
            throw std::invalid_argument("input line exceeds 8192 bytes");
        }
        const auto command = parse_command(line.text);
        switch (command.action) {
        case Action::empty:
            return;
        case Action::help:
            output_ << help << '\n';
            return;
        case Action::quit:
            request_exit();
            return;
        case Action::users:
            output_ << "USERS";
            for (const auto& user : users_.usernames) {
                output_ << ' ' << escape_text(user);
            }
            output_ << '\n';
            return;
        case Action::groups:
            groups();
            return;
        case Action::members: {
            const auto found =
                std::find_if(groups_.groups.begin(), groups_.groups.end(),
                             [&](const auto& group) { return group.group_id == command.target; });
            if (found == groups_.groups.end()) {
                throw std::invalid_argument("group absent from snapshot");
            }
            output_ << "MEMBERS " << escape_text(found->group_id);
            for (const auto& user : found->members) {
                output_ << ' ' << escape_text(user);
            }
            output_ << '\n';
            return;
        }
        default:
            break;
        }
        if (!connected_ || pending_.size() >= 64) {
            throw std::invalid_argument("not connected or pending command limit reached");
        }
        const auto action = command.action == Action::direct   ? client::CommandKind::direct
                            : command.action == Action::group  ? client::CommandKind::group
                            : command.action == Action::create ? client::CommandKind::create_group
                                                               : client::CommandKind::join_group;
        const auto id = worker_.submit(action, command.target, command.text);
        if (!id) {
            throw std::invalid_argument("worker rejected command admission");
        }
        pending_.insert(*id);
        output_ << "QUEUED " << *id << ' ' << kind(action) << '\n';
    } catch (const std::invalid_argument& error) {
        exit_code_ = 1;
        output_ << "ERROR " << escape_text(error.what()) << '\n';
    }
    output_.flush();
}
} // namespace chat::cli
