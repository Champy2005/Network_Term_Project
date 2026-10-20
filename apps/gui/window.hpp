/** @file
 * @brief Declare the Windows room client and stable control IDs used by automated UI tests.
 */
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include <map>
#include <windows.h>

namespace chat::gui {
/// @brief Native controls for connection, discovery, rooms, and message composition.
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
    room_title = 130,
    transcript,
    message,
    send,
    status = 140
};
/// @brief UI-thread-only window; the owned worker is the only socket caller.
class Window {
  public:
    explicit Window(bool quit_on_destroy = true, client::WorkerLimits limits = {},
                    transport::ClientTlsOptions tls = {});
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    /// @brief Create controls; hidden windows permit automated tests without a walkthrough.
    [[nodiscard]] HWND create(HINSTANCE instance, bool visible = true);
    /// @brief Drain worker values on the UI thread, with no blocking network calls.
    void pump();
    [[nodiscard]] const client::RoomModel& model() const { return model_; }
    [[nodiscard]] HWND handle() const { return window_; }

  private:
    struct Pending {
        client::CommandKind kind;
        std::string key, text;
    };
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void controls();
    void layout(int width, int height);
    void command(int id, int notification);
    void refresh();
    void save_draft();
    void submit(client::CommandKind kind, const std::string& target, const std::string& text = {});
    void say(const std::string& text);
    HWND control(int id) const;
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    client::ChatWorker worker_;
    client::RoomModel model_;
    std::map<std::uint64_t, Pending> pending_;
    bool connected_ = false, quit_on_destroy_ = true;
    std::string displayed_key_, displayed_transcript_;
};
} // namespace chat::gui
