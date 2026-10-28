/** @file
 * @brief Keep bounded room transcripts and drafts independently of GUI widgets.
 */
#pragma once
#include "chat/client/ping_client.hpp"
#include <deque>
#include <string>
#include <vector>

namespace chat::client {
/// @brief One server-confirmed line, with local retention order for eviction.
struct ChatLine {
    std::string sender, text;
    std::uint64_t order = 0;
    /// @brief Stable receipt and room cursor for live/replayed duplicate suppression.
    std::string message_id{};
    std::uint64_t room_sequence = 0;
};
/// @brief A separate room view; draft/transcript are local and never persisted.
struct RoomView {
    std::string key, target, title, wire_id, draft;
    bool group = false, joined = false, available = false;
    std::deque<ChatLine> lines;
    /// @brief Last applied room cursor, retained even after older lines are evicted.
    std::uint64_t last_sequence = 0;
};
/// @brief UI-thread-only state; server deliveries, not ACKs, populate transcripts.
/// At most 256 rooms, 100 lines per room, and 2 MiB of retained line strings.
class RoomModel {
  public:
    /// @brief Begin a new local login session, clearing previous identity/chat data.
    void reset(std::string username);
    /// @brief Mark rooms offline without discarding drafts or local transcripts.
    void disconnected();
    /// @brief Apply a complete snapshot or route a delivery to its own room.
    void apply(const ClientEvent& event);
    /// @brief Merge an authorized history page into its room without duplicating live lines.
    void apply_history(const HistoryPage& page);
    /// @brief Rebind a server-authorized durable room without trusting a peer's display name.
    void restore_direct(const DirectRoomInfo& room);
    /// @brief Select a connected peer's current room, creating a provisional view.
    void open_direct(const std::string& username);
    /// @brief Select a known group's room even before joining it.
    void open_group(const std::string& id);
    /// @brief Select an existing local room key.
    void select(const std::string& key);
    /// @brief Save a bounded draft for the selected room, preserving it across navigation.
    void draft(std::string text);
    /// @brief Clear a sent draft only if it still equals the acknowledged submission.
    void sent(const std::string& key, const std::string& submitted);
    [[nodiscard]] const RoomView* current() const;
    [[nodiscard]] const std::vector<RoomView>& rooms() const { return rooms_; }
    [[nodiscard]] const std::vector<std::string>& users() const { return users_; }
    [[nodiscard]] const std::vector<GroupInfo>& groups() const { return groups_; }
    [[nodiscard]] const std::string& username() const { return username_; }

  private:
    RoomView& add(RoomView room);
    void append(RoomView& room, const std::string& sender, const std::string& text,
                const std::string& message_id, std::uint64_t sequence);
    void trim_front(RoomView& room);
    std::string username_, selected_;
    std::vector<std::string> users_;
    std::vector<GroupInfo> groups_;
    std::vector<RoomView> rooms_;
    std::size_t bytes_ = 0;
    std::uint64_t next_room_ = 0, next_line_ = 0;
};
} // namespace chat::client
