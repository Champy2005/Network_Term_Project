/** @file
 * @brief Declare transactional SQLite history with durable participant capabilities.
 */
#pragma once
#include "chat.pb.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace chat::server {
/// @brief Durable participant ID and a secret issued only to its claiming connection.
struct StoredIdentity {
    std::int64_t id = 0;
    std::string resume_token;
};
/// @brief Persisted group discovery; active membership is rebuilt through explicit joins.
struct StoredGroup {
    std::string id, name;
};
/// @brief Committed receipt, shared by ACK, delivery, and authorized history.
struct StoredMessage {
    /// @brief Authoritative room containing this accepted message.
    std::string room_id;
    /// @brief Server-issued ID retained across durable restart/resume.
    std::string message_id;
    /// @brief Positive room counter advanced in the message transaction.
    std::uint64_t sequence = 0;
    /// @brief Original server timestamp; never regenerated for a retry.
    std::uint64_t timestamp = 0;
    /// @brief True means return this receipt without new insertion or delivery.
    bool duplicate = false;
};
/// @brief A retry key already names a different type, target, or text.
class MessageIdConflict : public std::runtime_error {
  public:
    MessageIdConflict() : std::runtime_error("message retry key conflicts with accepted content") {}
};
/// @brief One SQLite connection, used only by the hub strand after construction.
/// File stores use WAL/FULL. Commits precede ACK/routing. Busy waits are 100 ms.
/// Resume-token hashes are persisted; fresh claims of the same name get new identities.
class HistoryStore {
  public:
    /// @brief Open/create schema v2, atomically upgrading v1; reject unsupported schemas.
    explicit HistoryStore(const std::string& path);
    ~HistoryStore();
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;
    /// @brief Create a new capability or verify its token/name without disclosing existing tokens.
    [[nodiscard]] StoredIdentity identity(const std::string& name, const std::string& token);
    /// @brief Load durable groups with no implied active connection membership.
    [[nodiscard]] std::vector<StoredGroup> groups();
    /// @brief Atomically create a group and record its creator's admission.
    [[nodiscard]] std::string create_group(const std::string& name, std::int64_t principal);
    /// @brief Record a voluntary admission; historical admissions do not authorize current
    /// delivery.
    void join_group(const std::string& id, std::int64_t principal);
    /// @brief Atomically find/create a durable participant-pair room and commit one message.
    [[nodiscard]] StoredMessage direct(std::int64_t sender, std::int64_t recipient,
                                       const std::string& sender_name,
                                       const std::string& recipient_name, const std::string& text,
                                       const std::string& key);
    /// @brief Commit a group message after the hub has checked current membership.
    [[nodiscard]] StoredMessage group_message(const std::string& id, std::int64_t sender,
                                              const std::string& sender_name,
                                              const std::string& text, const std::string& key);
    /// @brief Find this principal's committed retry or reject mismatched content; no delivery.
    [[nodiscard]] std::optional<StoredMessage> retry(std::int64_t sender, const std::string& key,
                                                     bool group, const std::string& target,
                                                     const std::string& text);
    /// @brief Check durable direct-room participation; missing/unrelated rooms both return false.
    [[nodiscard]] bool direct_participant(const std::string& id, std::int64_t principal);
    /// @brief Read at most 100 ascending records after authorization by the caller/hub.
    [[nodiscard]] protocol::HistoryResponse history(const std::string& id, std::uint64_t after,
                                                    std::uint32_t limit);
    /// @brief Authorized caller's contiguous sequence page at a stable upper watermark.
    [[nodiscard]] protocol::SyncResponse sync(const std::string& id, std::uint64_t after,
                                              std::uint64_t through, std::uint32_t limit);
    /// @brief Enumerate only a principal's direct rooms, with bounded cursor pagination.
    [[nodiscard]] protocol::DirectRoomsResponse
    direct_rooms(std::int64_t principal, std::uint64_t after, std::uint32_t limit,
                 const std::vector<std::int64_t>& connected);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace chat::server
