/** @file
 * @brief Declare authoritative connection identities, rooms, groups, and routing state.
 */
#pragma once

#include "chat/server/client_session.hpp"
#include "chat/server/history_store.hpp"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace chat::server {

/// All methods run on the server's shared strand. Session close callbacks must
/// post remove() to that strand, preventing registry mutation inside send().
class ChatHub {
  public:
    /// @brief Optional persistent store; empty path preserves ephemeral session-only behavior.
    explicit ChatHub(const std::string& database_path = {});
    /// @brief Retain a newly accepted, initially unregistered connection.
    void attach(std::shared_ptr<ClientSession> session);
    /// @brief Release identity, pair rooms, and membership; publish replacement snapshots.
    void remove(ClientSession& session);
    /// @brief Validate direction/authorization and dispatch one parsed envelope.
    void handle(ClientSession& session, const protocol::Envelope& request);
    /// @brief Close all retained sessions without shutdown snapshot broadcasts.
    void stop();
    /// @brief Count accepted connections, including those awaiting login or cleanup.
    [[nodiscard]] std::size_t size() const { return connections_.size(); }

  private:
    struct RecentMessage {
        std::string key, target, text;
        bool group = false;
        StoredMessage receipt;
    };
    struct Connection {
        std::shared_ptr<ClientSession> session;
        std::string username;
        std::uint64_t id = 0;
        std::int64_t principal = 0;
        std::deque<RecentMessage> recent{};
    };
    struct Group {
        std::string name;
        std::set<ClientSession*> members;
        std::uint64_t sequence = 0;
    };
    struct DirectRoom {
        std::string id;
        std::uint64_t sequence = 0;
    };
    using Pair = std::pair<std::uint64_t, std::uint64_t>;
    void login(ClientSession& session, const protocol::Envelope& request);
    void direct(ClientSession& session, const protocol::Envelope& request);
    void create_group(ClientSession& session, const protocol::Envelope& request);
    void join_group(ClientSession& session, const protocol::Envelope& request);
    void group_message(ClientSession& session, const protocol::Envelope& request);
    void history(ClientSession& session, const protocol::Envelope& request);
    void sync(ClientSession& session, const protocol::Envelope& request);
    void direct_rooms(ClientSession& session, const protocol::Envelope& request);
    void error(ClientSession& session, const protocol::Envelope& request, protocol::ErrorCode code,
               const std::string& reason);
    void acknowledge(ClientSession& session, const protocol::Envelope& request,
                     const std::string& resource);
    void acknowledge(ClientSession& session, const protocol::Envelope& request,
                     const StoredMessage& receipt);
    std::optional<StoredMessage> retry(Connection& sender, const std::string& key, bool group,
                                       const std::string& target, const std::string& text);
    void remember(Connection& sender, const std::string& key, bool group, const std::string& target,
                  const std::string& text, const StoredMessage& receipt);
    void broadcast(bool users, bool groups);

    std::unordered_map<ClientSession*, Connection> connections_;
    std::map<std::string, ClientSession*> usernames_;
    std::map<Pair, DirectRoom> direct_rooms_;
    std::map<std::string, Group> groups_;
    std::uint64_t next_connection_ = 0;
    std::uint64_t next_direct_room_ = 0;
    std::uint64_t next_group_ = 0;
    std::uint64_t next_message_ = 0;
    std::string message_namespace_;
    bool stopping_ = false;
    std::unique_ptr<HistoryStore> storage_;
};

} // namespace chat::server
