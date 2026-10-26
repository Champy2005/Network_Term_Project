/** @file
 * @brief Declare blocking chat operations and GUI-independent result/event types.
 */
#pragma once
#include "chat/transport/tls.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace chat::client {

/// @brief Accepted identity or server rejection reason; rejection permits retry.
struct LoginResult {
    bool accepted = false;
    std::string username;
    std::string reason;
    std::string resume_token; // Secret; persist securely if explicit identity resume is needed.
};

/// @brief GUI-independent codes matching the stable protocol error values.
enum class RequestError {
    none = 0,
    invalid_request = 1,
    not_logged_in = 2,
    username_taken = 3,
    user_not_found = 4,
    group_not_found = 5,
    already_member = 6,
    not_group_member = 7,
    internal_error = 8,
    history_forbidden = 9,
    message_id_conflict = 10,
    sync_gap = 11
};
/// @brief Acceptance/rejection; storage-enabled servers commit before ACK, not recipient receipt.
struct OperationResult {
    bool accepted = false;
    std::string resource_id;
    RequestError error = RequestError::none;
    std::string reason;
    /// @brief Stable server ID on message acceptance; empty for group create/join or failure.
    std::string message_id{};
    /// @brief Attempt key, including typed rejection; retain before sending to recover thrown
    /// failures.
    std::string client_message_id{};
    /// @brief Committed room position on message acceptance; zero for nonmessage operations.
    std::uint64_t room_sequence = 0;
};
/// @brief Server-authored delivery shared by sender and named recipient.
struct DirectDelivery {
    std::string sender, recipient, room_id, text;
    /// @brief Server-assigned message receipt, identical for sender and recipient.
    std::string message_id{};
    /// @brief Positive server position in this direct room.
    std::uint64_t room_sequence = 0;
    /// @brief Server wall-clock milliseconds; sequence determines order.
    std::uint64_t server_timestamp_unix_ms = 0;
};
/// @brief Server-authored delivery available only to current group members.
struct GroupDelivery {
    std::string sender, group_id, text;
    /// @brief Stable server ID, identical for all intended group recipients.
    std::string message_id{};
    /// @brief Positive shared position across all senders in this group.
    std::uint64_t room_sequence = 0;
    /// @brief Server wall-clock milliseconds; sequence determines order.
    std::uint64_t server_timestamp_unix_ms = 0;
};
/// @brief Complete replacement of connected names, including the receiving user.
struct UserSnapshot {
    std::vector<std::string> usernames;
};
/// @brief Opaque group ID, display name, and current connected members.
struct GroupInfo {
    std::string group_id, name;
    std::vector<std::string> members;
};
/// @brief Complete replacement of all discoverable groups and memberships.
struct GroupSnapshot {
    std::vector<GroupInfo> groups;
};
/// @brief Typed unsolicited delivery/update; callers dispatch using std::visit.
using ClientEvent = std::variant<DirectDelivery, GroupDelivery, UserSnapshot, GroupSnapshot>;

/// @brief One stored message with a database cursor, independent of live wire sequence fields.
struct HistoryRecord {
    std::uint64_t id = 0;
    std::string sender, recipient, text;
    /// @brief Same stable server ID as the original ACK/delivery.
    std::string message_id{};
    /// @brief Stored per-room position, independent of the global page cursor.
    std::uint64_t room_sequence = 0;
    /// @brief Original timestamp, or zero for rows upgraded from schema v1.
    std::uint64_t server_timestamp_unix_ms = 0;
};
/// @brief Bounded ascending history page or typed denial; no automatic replay into live events.
struct HistoryPage {
    bool accepted = false;
    std::string room_id;
    RequestError error = RequestError::none;
    std::string reason;
    std::vector<HistoryRecord> records;
    std::uint64_t next_after_id = 0;
    bool has_more = false, group = false;
};
/// @brief Transport failure eligible for recovery; malformed protocol responses remain
/// runtime_error.
class NetworkError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
/// @brief Contiguous authorized replay bounded by one captured room watermark.
struct SyncPage {
    bool accepted = false;
    std::string room_id;
    RequestError error = RequestError::none;
    std::string reason;
    std::vector<HistoryRecord> records;
    /// @brief Last returned room sequence; advance only after applying these records.
    std::uint64_t next_sequence = 0;
    /// @brief Fixed replay watermark to pass unchanged to following pages.
    std::uint64_t through_sequence = 0;
    bool has_more = false, group = false;
};
/// @brief One durable direct room belonging to the authenticated principal.
struct DirectRoomInfo {
    /// @brief Opaque durable pair room ID and the original peer's display name.
    std::string room_id, peer;
    /// @brief Informational server watermark; never advance an applied cursor from this field.
    std::uint64_t last_sequence = 0;
    /// @brief Server proof that the original peer is online, rather than a fresh namesake.
    bool peer_connected = false;
};
/// @brief Bounded private room discovery; groups still require explicit current membership.
struct DirectRoomsPage {
    bool accepted = false;
    RequestError error = RequestError::none;
    std::string reason;
    std::vector<DirectRoomInfo> rooms;
    /// @brief Last returned numeric room ID to pass to the next discovery request.
    std::uint64_t next_room_id = 0;
    /// @brief True requires another bounded discovery page.
    bool has_more = false;
};

/// Blocking headless chat API, independent of any GUI. Use on a worker thread.
/// Each instance has one caller at a time and owns its private networking loop.
/// Connection and each complete exchange have a deadline (default: 3 seconds).
/// Transport failures throw NetworkError; malformed protocol responses throw
/// std::runtime_error. Both close the connection; headless callers reconnect explicitly.
class PingClient {
  public:
    /// @brief Create a disconnected client without a background thread.
    explicit PingClient(transport::ClientTlsOptions tls = {});
    /// @brief Release the owned socket and networking state.
    ~PingClient();
    PingClient(const PingClient&) = delete;
    PingClient& operator=(const PingClient&) = delete;

    /// @brief Replace the previous connection and resolve/connect within the deadline.
    void connect(const std::string& host, std::uint16_t port,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Exchange a correlated nonce before or after login.
    [[nodiscard]] std::uint64_t ping(std::uint64_t nonce,
                                     std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// A rejected claim leaves the connection open for a different username.
    [[nodiscard]] LoginResult login(const std::string& username,
                                    std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Prove an earlier durable participant identity; a fresh claim never inherits its
    /// history.
    [[nodiscard]] LoginResult
    resume_login(const std::string& username, const std::string& token,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Read an authorized room after a cursor; limit 0 selects 50, maximum 100.
    [[nodiscard]] HistoryPage history(const std::string& room_id, std::uint64_t after_id = 0,
                                      std::uint32_t limit = 50,
                                      std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Fetch contiguous sequences; keep the first response watermark for subsequent pages.
    [[nodiscard]] SyncPage sync(const std::string& room_id, std::uint64_t after_sequence = 0,
                                std::uint64_t through_sequence = 0, std::uint32_t limit = 50,
                                std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Discover this principal's direct rooms, including offline peers, at most 64 per page.
    [[nodiscard]] DirectRoomsPage
    direct_rooms(std::uint64_t after_room_id = 0, std::uint32_t limit = 64,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Send private text and return its pair room ID or a server error.
    [[nodiscard]] OperationResult
    send_direct(const std::string& recipient, const std::string& text,
                std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Save a key before send; retry the exact text/target under the same resumed identity.
    [[nodiscard]] OperationResult
    send_direct_with_key(const std::string& recipient, const std::string& text,
                         const std::string& key,
                         std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Create a random retry key without sending, for persistence before an attempt.
    [[nodiscard]] static std::string new_message_key();
    /// @brief Create a group with this connection as its only initial member.
    [[nodiscard]] OperationResult
    create_group(const std::string& name,
                 std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Request membership for this connection alone using an opaque group ID.
    [[nodiscard]] OperationResult
    join_group(const std::string& group_id,
               std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Send group text; the server checks this connection is a member.
    [[nodiscard]] OperationResult
    send_group(const std::string& group_id, const std::string& text,
               std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Retry group text with a saved key; current membership is required on every attempt.
    [[nodiscard]] OperationResult
    send_group_with_key(const std::string& group_id, const std::string& text,
                        const std::string& key,
                        std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// Reads on demand; no background network thread. A quiet deadline returns
    /// nullopt without disconnecting. Buffered events are bounded to 256/2 MiB.
    [[nodiscard]] std::optional<ClientEvent>
    next_event(std::chrono::milliseconds timeout = std::chrono::seconds(3));
    /// @brief Close the connection and discard partial frames and queued events.
    void close();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chat::client
