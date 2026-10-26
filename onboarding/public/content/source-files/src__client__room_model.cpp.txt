/** @file
 * @brief Route server events into independent bounded room views and preserve unsent drafts.
 */
#include "chat/client/room_model.hpp"
#include <algorithm>
#include <stdexcept>

namespace chat::client {
namespace {
constexpr std::size_t kMaxBytes = std::size_t{2} * 1024U * 1024U;
bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
} // namespace
void RoomModel::reset(std::string username) {
    username_ = std::move(username);
    selected_.clear();
    users_.clear();
    groups_.clear();
    rooms_.clear();
    bytes_ = 0;
}
void RoomModel::disconnected() {
    users_.clear();
    groups_.clear();
    for (auto& room : rooms_) {
        room.available = false;
        room.joined = false;
    }
}
RoomView& RoomModel::add(RoomView room) {
    if (rooms_.size() == 256) {
        // Preserve discovered groups (at most 128); evict old direct views first.
        auto oldest = std::find_if(rooms_.begin(), rooms_.end(),
                                   [](const RoomView& view) { return !view.group; });
        if (oldest == rooms_.end()) {
            oldest = rooms_.begin();
        }
        while (!oldest->lines.empty()) {
            trim_front(*oldest);
        }
        if (selected_ == oldest->key) {
            selected_.clear();
        }
        rooms_.erase(oldest);
    }
    rooms_.push_back(std::move(room));
    return rooms_.back();
}
void RoomModel::open_direct(const std::string& username) {
    if (!contains(users_, username)) {
        throw std::invalid_argument("user is not connected");
    }
    const auto found = std::find_if(rooms_.rbegin(), rooms_.rend(), [&](const RoomView& room) {
        return !room.group && room.target == username && room.available;
    });
    if (found != rooms_.rend()) {
        selected_ = found->key;
    } else {
        RoomView room;
        room.key = "direct-view-" + std::to_string(++next_room_);
        room.target = username;
        room.title = "Direct: " + username;
        room.available = true;
        selected_ = add(std::move(room)).key;
    }
}
void RoomModel::open_group(const std::string& id) { select("group:" + id); }
void RoomModel::restore_direct(const DirectRoomInfo& room) {
    auto found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& view) {
        return !view.group && view.wire_id == room.room_id;
    });
    if (found == rooms_.end()) {
        found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& view) {
            return room.peer_connected && !view.group && view.wire_id.empty() &&
                   view.target == room.peer;
        });
        if (found == rooms_.end()) {
            RoomView view;
            view.key = "direct-view-" + std::to_string(++next_room_);
            view.target = room.peer;
            view.title = "Direct: " + room.peer;
            add(std::move(view));
            found = std::prev(rooms_.end());
        }
        found->wire_id = room.room_id;
    }
    found->available = room.peer_connected;
}
void RoomModel::select(const std::string& key) {
    const auto found = std::find_if(rooms_.begin(), rooms_.end(),
                                    [&](const RoomView& room) { return room.key == key; });
    if (found == rooms_.end()) {
        throw std::invalid_argument("room is not available");
    }
    selected_ = key;
}
const RoomView* RoomModel::current() const {
    const auto found = std::find_if(rooms_.begin(), rooms_.end(),
                                    [&](const RoomView& room) { return room.key == selected_; });
    return found == rooms_.end() ? nullptr : &*found;
}
void RoomModel::draft(std::string text) {
    if (text.size() > 16384) {
        throw std::invalid_argument("draft must fit within 16384 UTF-8 bytes");
    }
    for (auto& room : rooms_) {
        if (room.key == selected_) {
            room.draft = std::move(text);
            return;
        }
    }
}
void RoomModel::sent(const std::string& key, const std::string& submitted) {
    for (auto& room : rooms_) {
        if (room.key == key && room.draft == submitted) {
            room.draft.clear();
        }
    }
}
void RoomModel::trim_front(RoomView& room) {
    bytes_ -= room.lines.front().sender.size() + room.lines.front().text.size() +
              room.lines.front().message_id.size();
    room.lines.pop_front();
}
void RoomModel::append(RoomView& room, const std::string& sender, const std::string& text,
                       const std::string& message_id, std::uint64_t sequence) {
    if (sender.size() > 32 || text.empty() || text.size() > 4096 || message_id.size() > 128) {
        throw std::invalid_argument("invalid delivery fields");
    }
    if (sequence != 0 && sequence <= room.last_sequence) {
        return;
    }
    bytes_ += sender.size() + text.size() + message_id.size();
    room.lines.push_back({sender, text, ++next_line_, message_id, sequence});
    room.last_sequence = std::max(room.last_sequence, sequence);
    if (room.lines.size() > 100) {
        trim_front(room);
    }
    while (bytes_ > kMaxBytes) {
        RoomView* oldest = nullptr;
        for (auto& candidate : rooms_) {
            if (!candidate.lines.empty() &&
                (!oldest || candidate.lines.front().order < oldest->lines.front().order)) {
                oldest = &candidate;
            }
        }
        trim_front(*oldest);
    }
}
void RoomModel::apply(const ClientEvent& event) {
    if (const auto* users = std::get_if<UserSnapshot>(&event)) {
        users_ = users->usernames;
        for (auto& room : rooms_) {
            if (!room.group && !contains(users_, room.target)) {
                room.available = false; // A reclaimed name must get a new view.
            }
        }
    } else if (const auto* groups = std::get_if<GroupSnapshot>(&event)) {
        groups_ = groups->groups;
        for (auto& room : rooms_) {
            if (room.group) {
                room.available = false;
                room.joined = false;
            }
        }
        for (const auto& group : groups_) {
            auto found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& room) {
                return room.group && room.target == group.group_id;
            });
            if (found == rooms_.end()) {
                RoomView room;
                room.key = "group:" + group.group_id;
                room.target = group.group_id;
                room.group = true;
                room.wire_id = group.group_id;
                add(std::move(room));
                found = std::prev(rooms_.end());
            }
            found->title = group.name + " (" + group.group_id + ")";
            found->available = true;
            found->joined = contains(group.members, username_);
        }
    } else if (const auto* direct = std::get_if<DirectDelivery>(&event)) {
        if (direct->sender != username_ && direct->recipient != username_) {
            throw std::invalid_argument("direct delivery does not identify this user");
        }
        const auto peer = direct->sender == username_ ? direct->recipient : direct->sender;
        auto found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& room) {
            return !room.group && room.wire_id == direct->room_id;
        });
        if (found == rooms_.end()) {
            found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& room) {
                return !room.group && room.target == peer && room.wire_id.empty() && room.available;
            });
            if (found == rooms_.end()) {
                RoomView room;
                room.key = "direct-view-" + std::to_string(++next_room_);
                room.target = peer;
                room.title = "Direct: " + peer;
                room.available = contains(users_, peer);
                add(std::move(room));
                found = std::prev(rooms_.end());
            }
            found->wire_id = direct->room_id;
        }
        append(*found, direct->sender, direct->text, direct->message_id, direct->room_sequence);
    } else {
        const auto& group = std::get<GroupDelivery>(event);
        const auto found = std::find_if(rooms_.begin(), rooms_.end(), [&](const RoomView& room) {
            return room.group && room.target == group.group_id;
        });
        if (found == rooms_.end()) {
            throw std::invalid_argument("group delivery precedes discovery");
        }
        append(*found, group.sender, group.text, group.message_id, group.room_sequence);
    }
}
} // namespace chat::client
