/** @file
 * @brief Expose Model Context Protocol (MCP) JSON-RPC 2.0 tools, natural-language agent routing,
 * and a non-blocking loopback control server for GUI and CLI sessions.
 */
#pragma once
#include "chat/ai/ai_engine.hpp"
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chat::ai {
/// @brief Result of an in-app or MCP agent tool/prompt invocation.
struct AgentActionResult {
    /// @brief True when the tool or natural-language intent succeeded.
    bool ok = false;
    /// @brief Human-readable outcome or error explanation.
    std::string message;
    /// @brief Structured JSON payload returned to MCP callers.
    std::string json_data = "{}";
};
/// @brief Bind a client's RoomModel and single-owner command submission path to AI/MCP tools.
class AgentController {
  public:
    /// @brief Callback used to check whether the client worker is currently logged in.
    using ConnectedFn = std::function<bool()>;
    /// @brief Callback used to queue a server-authorized command through the owning UI/CLI path.
    using SubmitFn = std::function<std::optional<std::uint64_t>(
        client::CommandKind, const std::string&, const std::string&)>;
    /// @brief Optional observer notified when a tool or AI prompt produces user-visible output.
    using OutputFn = std::function<void(const std::string&)>;
    /// @brief Optional callback used to populate or replace the user's message composer draft.
    using DraftFn = std::function<void(const std::string&)>;

    /// @brief Construct a controller bound to the caller's room model and submission callbacks.
    AgentController(client::RoomModel& model, ConnectedFn is_connected, SubmitFn submit,
                    OutputFn on_output = {}, LlmEndpointOptions llm = LlmEndpointOptions::from_environment());

    /// @brief Register an optional callback invoked when AI generates or polishes a composer draft.
    void set_draft_callback(DraftFn on_draft) { on_draft_ = std::move(on_draft); }
    /// @brief Execute a natural-language instruction (including multi-step chains) against client state.
    [[nodiscard]] AgentActionResult execute_prompt(std::string_view prompt);
    /// @brief Summarize the specified room (key, wire ID, or target) or the active room if empty.
    [[nodiscard]] AgentActionResult summarize_room(std::string_view room_selector = {},
                                                   std::size_t max_sentences = 3);
    /// @brief Analyze sentiment of explicit text, or of the active room transcript when empty.
    [[nodiscard]] AgentActionResult analyze_sentiment(std::string_view text_or_empty = {});
    /// @brief Extract action items, decisions, and open questions from a room transcript.
    [[nodiscard]] AgentActionResult extract_action_items(std::string_view room_selector = {});
    /// @brief Generate a context-aware smart reply and either stage it in the draft box or send it.
    [[nodiscard]] AgentActionResult smart_reply(bool auto_send = false);
    /// @brief Rewrite and polish the provided message draft (or the active room's current draft).
    [[nodiscard]] AgentActionResult polish_draft(std::string_view draft_text = {});
    /// @brief Configure the live LLM backend (`set key <KEY>`, `use gemini`, `use openai`, `use ollama`).
    [[nodiscard]] AgentActionResult configure_llm(std::string_view spec);
    /// @brief Invoke a named MCP tool with raw JSON arguments.
    [[nodiscard]] AgentActionResult call_tool(std::string_view tool_name,
                                              std::string_view arguments_json);
    /// @brief Access the bound room model.
    [[nodiscard]] const client::RoomModel& model() const { return model_; }
    /// @brief Access the active LLM endpoint configuration.
    [[nodiscard]] const LlmEndpointOptions& llm_options() const noexcept { return llm_; }

  private:
    AgentActionResult execute_single_prompt(std::string_view prompt);
    const client::RoomView* find_room(std::string_view selector) const;
    void notify(const std::string& text) const;
    void notify_draft(const std::string& text);
    client::RoomModel& model_;
    ConnectedFn is_connected_;
    SubmitFn submit_;
    OutputFn on_output_;
    DraftFn on_draft_;
    LlmEndpointOptions llm_;
};
/// @brief Stateless JSON-RPC 2.0 / Model Context Protocol (MCP) message handler.
class McpDispatcher {
  public:
    /// @brief Maximum byte length of a single newline-delimited JSON-RPC request line.
    static constexpr std::size_t kMaxLineBytes = 65536;
    /// @brief Process one JSON-RPC 2.0 request line; notifications return nullopt.
    [[nodiscard]] static std::optional<std::string> handle_line(std::string_view line,
                                                                AgentController& controller);
    /// @brief Return the JSON array of MCP tool definitions exposed by this client.
    [[nodiscard]] static std::string tools_list_json();
};
/// @brief Non-blocking loopback-only (127.0.0.1) TCP JSON-RPC/MCP server polled on the UI thread.
class LoopbackAgentServer {
  public:
    /// @brief Bind 127.0.0.1 on the requested port (0 selects an ephemeral loopback port).
    explicit LoopbackAgentServer(std::uint16_t port = 0);
    ~LoopbackAgentServer();
    LoopbackAgentServer(const LoopbackAgentServer&) = delete;
    LoopbackAgentServer& operator=(const LoopbackAgentServer&) = delete;

    /// @brief Bound loopback TCP port.
    [[nodiscard]] std::uint16_t port() const noexcept;
    /// @brief Accept connections and process available JSON-RPC lines without blocking.
    void poll(AgentController& controller);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace chat::ai
