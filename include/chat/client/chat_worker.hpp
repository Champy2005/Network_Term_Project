/** @file
 * @brief Provide a bounded asynchronous command/event bridge for GUI callers.
 */
#pragma once
#include "chat/client/ping_client.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace chat::client {
/// @brief Outbound action whose identity/authorization is still checked by the server.
enum class CommandKind { direct, create_group, join_group, group, history };
/// @brief Worker and operation time budgets plus command/update admission bounds.
struct WorkerLimits {
    std::chrono::milliseconds operation_timeout{3000}, poll_timeout{25};
    /// @brief Periodic Ping cadence, default 10 seconds; 0 disables, maximum 24 hours.
    std::chrono::milliseconds heartbeat_interval{10000};
    std::size_t commands = 64, updates = 256, update_bytes = std::size_t{2} * 1024U * 1024U;
    /// @brief Durable recovery only; zero attempts disables it. Fresh/ephemeral logins never fall
    /// back.
    std::size_t reconnect_attempts = 16, recovery_rooms = 256;
    /// @brief Stop-aware exponential retry delay, capped at reconnect_max_delay.
    std::chrono::milliseconds reconnect_delay{250}, reconnect_max_delay{2500};
};
/// @brief Login completion delivered on the UI polling path.
struct WorkerConnected {
    std::string username;
    /// @brief True preserves the same identity's local drafts/transcripts after catch-up.
    bool resumed = false;
};
/// @brief Temporary offline state; queued message attempts and their keys remain retained.
struct WorkerReconnecting {
    /// @brief Bounded transport failure text, without resume capabilities or message content.
    std::string reason;
    /// @brief One-based consecutive recovery attempt number.
    std::size_t attempt = 0;
};
/// @brief Authorized durable room discovery binds drafts and original-peer availability on resume.
struct WorkerDirectRoom {
    /// @brief Private directory entry authorized by the server for this resumed identity.
    DirectRoomInfo room;
};
/// @brief Authorized room history page loaded on demand by a client command.
struct WorkerHistory {
    /// @brief Ascending stored messages validated by the headless client.
    HistoryPage page;
};
/// @brief Terminal reason, latched separately so queue overflow is never silent.
struct WorkerDisconnected {
    std::string reason;
};
/// @brief Correlated operation result; no optimistic message line is generated.
struct WorkerCompleted {
    std::uint64_t id = 0;
    CommandKind kind = CommandKind::direct;
    std::string target;
    OperationResult result;
};
using WorkerUpdate = std::variant<WorkerConnected, WorkerDisconnected, WorkerCompleted, ClientEvent,
                                  WorkerReconnecting, WorkerDirectRoom, WorkerHistory>;
/// @brief One networking thread owns ChatClient; the UI submits and polls plain values.
/// Start/poll are called by one UI thread. disconnect() only requests stop;
/// destruction joins after at most the current bounded operation/poll deadline.
/// Heartbeat Ping/Pong runs on this same thread after login. A durable login's
/// transport failure triggers bounded identity resume and sequence replay; protocol,
/// authorization, queue, or exhausted recovery failures publish WorkerDisconnected.
class ChatWorker {
  public:
    explicit ChatWorker(WorkerLimits limits = {}, transport::ClientTlsOptions tls = {});
    ~ChatWorker();
    ChatWorker(const ChatWorker&) = delete;
    ChatWorker& operator=(const ChatWorker&) = delete;
    /// @brief Start a fresh connection/login only when the previous worker has ended.
    [[nodiscard]] bool start(std::string host, std::uint16_t port, std::string username);
    /// @brief Queue a bounded operation; nullopt means disconnected, invalid, or full.
    [[nodiscard]] std::optional<std::uint64_t> submit(CommandKind kind, std::string target,
                                                      std::string text = {});
    /// @brief Request cancellation; socket access remains on its owning thread.
    void disconnect();
    /// @brief Drain bounded updates and any separately latched terminal reason.
    [[nodiscard]] std::vector<WorkerUpdate> poll();
    [[nodiscard]] bool running() const { return running_.load(); }

  private:
    struct Command {
        std::uint64_t id;
        CommandKind kind;
        std::string target, text;
        std::string message_key;
    };
    bool emit(WorkerUpdate update);
    void run(const std::stop_token& stop, const std::string& host, std::uint16_t port,
             const std::string& username);
    WorkerLimits limits_;
    transport::ClientTlsOptions tls_;
    std::jthread thread_;
    std::atomic<bool> running_{false}, connected_{false};
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<Command> commands_;
    std::vector<WorkerUpdate> updates_;
    std::optional<WorkerDisconnected> terminal_;
    std::size_t bytes_ = 0;
    std::uint64_t next_command_ = 0;
};
} // namespace chat::client
