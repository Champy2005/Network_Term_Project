/** @file
 * @brief Declare the Windows room client and stable control IDs used by automated UI tests.
 */
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chat/ai/agent_tools.hpp"
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include <map>
#include <memory>
#include <optional>
#include <windows.h>

namespace chat::gui {
/// @brief Native controls for connection, discovery, rooms, AI tools, and message composition.
enum ControlId {
    host = 100,
    port,
    username,
    connect,
    disconnect,
    users = 110,
    groups,
    members,
    rooms,
    group_name = 120,
    create_group,
    join_group,
    load_history,
    room_title = 130,
    transcript,
    message,
    send,
    ai_prompt,
    ai_run,
    ai_summarize,
    ai_sentiment,
    ai_output,
    status = 140,
    ai_smart_reply = 141,
    ai_polish = 142,
    ai_actions = 143,
    bubble_canvas = 144
};
/// @brief UI-thread-only window; the owned worker is the only socket caller.
class Window {
  public:
    explicit Window(bool quit_on_destroy = true, client::WorkerLimits limits = {},
                    transport::ClientTlsOptions tls = {},
                    std::optional<std::uint16_t> agent_port = std::nullopt);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    /// @brief Create controls; hidden windows permit automated tests without a walkthrough.
    [[nodiscard]] HWND create(HINSTANCE instance, bool visible = true);
    /// @brief Drain worker and loopback agent values on the UI thread, with no blocking calls.
    void pump();
    [[nodiscard]] const client::RoomModel& model() const { return model_; }
    [[nodiscard]] HWND handle() const { return window_; }
    /// @brief Bound loopback MCP/JSON-RPC port, or zero when external agent attachment is disabled.
    [[nodiscard]] std::uint16_t agent_port() const noexcept;

  private:
    struct Pending {
        client::CommandKind kind;
        std::string key, text;
    };
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK bubble_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT message, WPARAM wparam,
                                               LPARAM lparam);
    void controls();
    void layout(int width, int height);
    void command(int id, int notification);
    void refresh();
    void paint_bubbles(HWND canvas);
    void save_draft();
    std::optional<std::uint64_t> submit(client::CommandKind kind, const std::string& target,
                                        const std::string& text = {});
    void say(const std::string& text);
    void say_ai(const std::string& text);
    HWND control(int id) const;
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ui_ = nullptr;
    HFONT font_bold_ = nullptr;
    HFONT font_title_ = nullptr;
    HFONT font_small_ = nullptr;
    HBRUSH brush_bg_ = nullptr;
    HBRUSH brush_sidebar_ = nullptr;
    HBRUSH brush_input_ = nullptr;
    HBRUSH brush_ai_ = nullptr;
    WNDPROC orig_edit_proc_ = nullptr;
    int scroll_offset_ = 0;
    int max_scroll_ = 0;
    client::ChatWorker worker_;
    client::RoomModel model_;
    ai::AgentController agent_;
    std::unique_ptr<ai::LoopbackAgentServer> agent_server_;
    std::map<std::uint64_t, Pending> pending_;
    bool connected_ = false, quit_on_destroy_ = true;
    std::string displayed_key_, displayed_transcript_, ai_text_;
};
} // namespace chat::gui
