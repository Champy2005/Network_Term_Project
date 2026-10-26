/** @file
 * @brief Parse bounded console commands and present worker events without network types.
 */
#pragma once
#include "chat/client/chat_worker.hpp"
#include <ostream>
#include <set>
#include <string_view>

namespace chat::cli {
inline constexpr std::size_t kInputChunk = 1024, kLineBytes = 8192;
/// @brief Escape remote control characters and delimiters before terminal display.
[[nodiscard]] std::string escape_text(std::string_view text);
/// @brief One decoded command line; an oversized line is discarded through its delimiter.
struct InputLine {
    std::string text;
    bool oversized = false;
};
/// @brief Incremental bounded UTF-8 line input, CR/LF handling, and codepoint backspace.
class LineInput {
  public:
    /// @brief Feed at most kInputChunk bytes; return completed lines without retaining a backlog.
    [[nodiscard]] std::vector<InputLine> feed(std::string_view bytes);
    /// @brief Emit an unterminated final line once at EOF.
    [[nodiscard]] std::vector<InputLine> finish();

  private:
    std::string line_;
    bool oversized_ = false, after_cr_ = false;
};
/// @brief Parsed local view command or server-authorized worker action.
enum class Action { empty, help, users, groups, members, direct, create, join, group, quit };
struct Command {
    Action action = Action::empty;
    std::string target, text;
};
/// @brief Validate syntax and byte limits; text is the remaining line, with no shell expansion.
[[nodiscard]] Command parse_command(std::string_view line);
/// @brief Persistent CLI presentation; one caller owns output, snapshots, and worker polling.
/// No transcripts, tokens, or message keys are written to disk. Quit/EOF drains
/// admitted receipts for a bounded time before requesting worker cancellation.
class ChatConsole {
  public:
    explicit ChatConsole(std::ostream& output, client::WorkerLimits limits = {},
                         transport::ClientTlsOptions tls = {},
                         std::chrono::milliseconds drain_timeout = std::chrono::seconds(5));
    /// @brief Start one fresh login; connection failures arrive through poll().
    [[nodiscard]] bool start(std::string host, std::uint16_t port, std::string username);
    /// @brief Print available updates; unexpected terminal failure sets exit code one.
    void poll();
    /// @brief Process a local command or queue a bounded worker operation.
    void execute(const InputLine& line);
    /// @brief Drain admitted operations, then stop; a second request cancels immediately.
    void request_exit();
    [[nodiscard]] bool ready() const { return connected_ && !quitting_; }
    [[nodiscard]] bool exiting() const { return quitting_; }
    [[nodiscard]] bool finished() const { return finished_; }
    [[nodiscard]] int exit_code() const { return exit_code_; }

  private:
    void update(const client::WorkerUpdate& update);
    void event(const client::ClientEvent& event);
    void groups() const;
    std::ostream& output_;
    client::ChatWorker worker_;
    client::UserSnapshot users_;
    client::GroupSnapshot groups_;
    std::set<std::uint64_t> pending_;
    std::chrono::milliseconds drain_timeout_;
    std::chrono::steady_clock::time_point drain_until_;
    bool started_ = false, connected_ = false, quitting_ = false, finished_ = false;
    int exit_code_ = 0;
};
} // namespace chat::cli
