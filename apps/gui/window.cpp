/** @file
 * @brief Implement native Unicode room controls while keeping every socket call off the UI thread.
 */
#include "window.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace chat::gui {
namespace {
constexpr wchar_t kClass[] = L"SocketChatRoomWindow";
std::wstring wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (count == 0) {
        throw std::runtime_error("invalid UTF-8 text");
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), count);
    // Protobuf text may contain NUL; render it visibly instead of truncating native controls.
    std::replace(result.begin(), result.end(), L'\0', L'␀');
    return result;
}
std::string text(HWND control) {
    const auto count = GetWindowTextLengthW(control);
    std::wstring buffer(static_cast<std::size_t>(count) + 1, L'\0');
    const auto read = GetWindowTextW(control, buffer.data(), count + 1);
    if (read == 0) {
        return {};
    }
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), read,
                                          nullptr, 0, nullptr, nullptr);
    if (size == 0) {
        throw std::runtime_error("invalid Unicode input");
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, buffer.data(), read, result.data(), size,
                        nullptr, nullptr);
    return result;
}
void set_text(HWND control, const std::string& value) {
    SetWindowTextW(control, wide(value).c_str());
}
void list(HWND control, const std::vector<std::string>& values, int selection = -1) {
    SendMessageW(control, WM_SETREDRAW, FALSE, 0);
    SendMessageW(control, LB_RESETCONTENT, 0, 0);
    for (const auto& value : values) {
        const auto label = wide(value);
        SendMessageW(control, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(control, LB_SETCURSEL, static_cast<WPARAM>(selection), 0);
    SendMessageW(control, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(control, nullptr, TRUE);
}
} // namespace
Window::Window(bool quit_on_destroy, client::WorkerLimits limits, transport::ClientTlsOptions tls)
    : worker_(limits, std::move(tls)), quit_on_destroy_(quit_on_destroy) {}
Window::~Window() {
    worker_.disconnect();
    if (window_) {
        DestroyWindow(window_);
    }
}
HWND Window::create(HINSTANCE instance, bool visible) {
    instance_ = instance;
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = procedure;
    description.hInstance = instance;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    description.lpszClassName = kClass;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("could not register GUI window");
    }
    const auto result =
        CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Socket Chat", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 1080, 760, nullptr, nullptr, instance, this);
    if (!result) {
        throw std::runtime_error("could not create GUI window");
    }
    if (visible) {
        ShowWindow(result, SW_SHOW);
        UpdateWindow(result);
    }
    return result;
}
HWND Window::control(int id) const { return GetDlgItem(window_, id); }
void Window::say(const std::string& value) { set_text(control(status), value); }
void Window::controls() {
    const auto add = [&](const wchar_t* kind, const wchar_t* caption, int id, DWORD style) {
        const auto child =
            CreateWindowExW(0, kind, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, window_,
                            // Win32 encodes child control IDs in its HMENU parameter.
                            // NOLINTNEXTLINE(performance-no-int-to-ptr)
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!child) {
            throw std::runtime_error("could not create GUI control");
        }
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                     TRUE);
    };
    add(L"STATIC", L"Host", 200, 0);
    add(L"EDIT", L"127.0.0.1", host, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"STATIC", L"Port", 201, 0);
    add(L"EDIT", L"5555", port, WS_BORDER | WS_TABSTOP | ES_NUMBER);
    add(L"STATIC", L"Username", 202, 0);
    add(L"EDIT", L"", username, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"BUTTON", L"Connect", connect, WS_TABSTOP);
    add(L"BUTTON", L"Disconnect", disconnect, WS_TABSTOP);
    add(L"STATIC", L"Connected users (select to open a direct room)", 203, 0);
    add(L"LISTBOX", L"", users, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);
    add(L"STATIC", L"Groups (all groups and their members)", 204, 0);
    add(L"LISTBOX", L"", groups, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);
    add(L"STATIC", L"Select a group to see its members.", members, 0);
    add(L"STATIC", L"Group name", 205, 0);
    add(L"EDIT", L"", group_name, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"BUTTON", L"Create group", create_group, WS_TABSTOP);
    add(L"BUTTON", L"Join selected group", join_group, WS_TABSTOP);
    add(L"STATIC", L"Your room views", 206, 0);
    add(L"LISTBOX", L"", rooms, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);
    add(L"STATIC", L"Select a user or group to open a room.", room_title, 0);
    add(L"EDIT", L"", transcript,
        WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
    add(L"EDIT", L"", message,
        WS_BORDER | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN);
    add(L"BUTTON", L"Send", send, WS_TABSTOP);
    add(L"STATIC", L"Choose a username and connect. Transcripts are local to this window.", status,
        0);
    SendMessageW(control(host), EM_SETLIMITTEXT, 255, 0);
    SendMessageW(control(username), EM_SETLIMITTEXT, 32, 0);
    SendMessageW(control(port), EM_SETLIMITTEXT, 5, 0);
    SendMessageW(control(group_name), EM_SETLIMITTEXT, 64, 0);
    SendMessageW(control(message), EM_SETLIMITTEXT, 4096, 0);
    SetTimer(window_, 1, 40, nullptr);
    refresh();
}
void Window::layout(int width, int height) {
    const auto move = [&](int id, int x, int y, int w, int h) {
        MoveWindow(control(id), x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    move(200, 12, 10, 40, 20);
    move(host, 55, 8, 190, 26);
    move(201, 258, 10, 35, 20);
    move(port, 296, 8, 65, 26);
    move(202, 374, 10, 70, 20);
    move(username, 448, 8, 155, 26);
    move(connect, 615, 8, 90, 26);
    move(disconnect, 714, 8, 95, 26);
    const int left = 300, right = width - left - 36;
    move(203, 12, 48, left, 28);
    move(users, 12, 78, left, 100);
    move(204, 12, 190, left, 25);
    move(groups, 12, 219, left, 105);
    move(members, 12, 330, left, 60);
    move(205, 12, 397, left, 20);
    move(group_name, 12, 420, left, 26);
    move(create_group, 12, 453, 125, 28);
    move(join_group, 146, 453, 166, 28);
    move(206, 12, 496, left, 22);
    move(rooms, 12, 522, left, height - 579);
    move(room_title, left + 24, 48, right, 34);
    move(transcript, left + 24, 88, right, height - 251);
    move(message, left + 24, height - 151, right, 70);
    move(send, width - 112, height - 73, 100, 28);
    move(status, 12, height - 38, width - 24, 34);
}
void Window::save_draft() { model_.draft(text(control(message))); }
void Window::submit(client::CommandKind kind, const std::string& target, const std::string& body) {
    if (pending_.size() >= 64) {
        say("Wait for pending requests to finish.");
        return;
    }
    const auto id = worker_.submit(kind, target, body);
    if (!id) {
        say("Cannot queue this request: check the text size/target or wait for pending work.");
        return;
    }
    pending_.emplace(*id, Pending{kind, model_.current() ? model_.current()->key : "", body});
    say("Request pending...");
}
void Window::command(int id, int notification) {
    if (!IsWindowEnabled(control(id))) {
        return;
    }
    if (id == connect && notification == BN_CLICKED) {
        const auto value = text(control(port));
        unsigned int number = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || number == 0 ||
            number > 65535) {
            say("Port must be between 1 and 65535.");
            return;
        }
        if (worker_.start(text(control(host)), static_cast<std::uint16_t>(number),
                          text(control(username)))) {
            say("Connecting...");
            refresh();
        } else {
            say("Enter a host and username, or wait for disconnection to finish.");
        }
    } else if (id == disconnect && notification == BN_CLICKED) {
        save_draft();
        worker_.disconnect();
        say("Disconnecting...");
    } else if (id == users && notification == LBN_SELCHANGE) {
        save_draft();
        const auto selected = SendMessageW(control(users), LB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<std::size_t>(selected) < model_.users().size()) {
            model_.open_direct(model_.users()[static_cast<std::size_t>(selected)]);
        }
        refresh();
    } else if (id == groups && notification == LBN_SELCHANGE) {
        save_draft();
        const auto selected = SendMessageW(control(groups), LB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<std::size_t>(selected) < model_.groups().size()) {
            model_.open_group(model_.groups()[static_cast<std::size_t>(selected)].group_id);
        }
        refresh();
    } else if (id == rooms && notification == LBN_SELCHANGE) {
        save_draft();
        const auto selected = SendMessageW(control(rooms), LB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<std::size_t>(selected) < model_.rooms().size()) {
            model_.select(model_.rooms()[static_cast<std::size_t>(selected)].key);
        }
        refresh();
    } else if (id == create_group && notification == BN_CLICKED) {
        submit(client::CommandKind::create_group, text(control(group_name)));
    } else if (id == join_group && notification == BN_CLICKED && model_.current() &&
               model_.current()->group) {
        submit(client::CommandKind::join_group, model_.current()->target);
    } else if (id == send && notification == BN_CLICKED && model_.current()) {
        save_draft();
        submit(model_.current()->group ? client::CommandKind::group : client::CommandKind::direct,
               model_.current()->target, model_.current()->draft);
    }
}
void Window::pump() {
    const auto updates = worker_.poll();
    if (updates.empty()) {
        return;
    }
    try {
        save_draft();
    } catch (const std::exception& error) {
        // Invalid input must not discard already drained network updates.
        say(error.what());
    }
    for (const auto& update : updates) {
        if (const auto* connected = std::get_if<client::WorkerConnected>(&update)) {
            if (!connected->resumed) {
                model_.reset(connected->username);
                displayed_key_.clear();
            }
            connected_ = true;
            say(std::string(connected->resumed ? "Reconnected as " : "Connected as ") +
                connected->username);
        } else if (const auto* recovering = std::get_if<client::WorkerReconnecting>(&update)) {
            connected_ = false;
            model_.disconnected();
            say("Reconnecting (attempt " + std::to_string(recovering->attempt) +
                "): " + recovering->reason);
        } else if (const auto* disconnected = std::get_if<client::WorkerDisconnected>(&update)) {
            connected_ = false;
            model_.disconnected();
            pending_.clear();
            say(disconnected->reason);
        } else if (const auto* event = std::get_if<client::ClientEvent>(&update)) {
            model_.apply(*event);
        } else if (const auto* room = std::get_if<client::WorkerDirectRoom>(&update)) {
            model_.restore_direct(room->room);
        } else {
            const auto& completed = std::get<client::WorkerCompleted>(update);
            const auto pending = pending_.find(completed.id);
            if (completed.result.accepted) {
                if (pending != pending_.end() &&
                    (pending->second.kind == client::CommandKind::direct ||
                     pending->second.kind == client::CommandKind::group)) {
                    model_.sent(pending->second.key, pending->second.text);
                }
                say("Accepted: " + completed.result.resource_id);
            } else {
                say("Rejected: " + completed.result.reason);
            }
            if (pending != pending_.end()) {
                pending_.erase(pending);
            }
        }
    }
    refresh();
}
void Window::refresh() {
    const auto* current = model_.current();
    std::vector<std::string> labels;
    labels.reserve(model_.groups().size());
    int group_selection = -1;
    for (const auto& group : model_.groups()) {
        if (current && current->group && current->target == group.group_id) {
            group_selection = static_cast<int>(labels.size());
        }
        labels.push_back(group.name + " (" + group.group_id + ")");
    }
    list(control(groups), labels, group_selection);
    list(control(users), model_.users());
    labels.clear();
    labels.reserve(model_.rooms().size());
    int room_selection = -1;
    for (const auto& room : model_.rooms()) {
        if (current && current->key == room.key) {
            room_selection = static_cast<int>(labels.size());
        }
        labels.push_back(room.title + (room.wire_id.empty() ? "" : " [" + room.wire_id + "]") +
                         (room.available ? "" : " (offline)"));
    }
    list(control(rooms), labels, room_selection);
    std::string membership = "Select a group to see its members.";
    if (current && current->group) {
        for (const auto& group : model_.groups()) {
            if (group.group_id == current->target) {
                membership = "Members: ";
                for (const auto& member : group.members) {
                    membership += member + " ";
                }
                if (group.members.empty()) {
                    membership += "(none)";
                }
            }
        }
    }
    set_text(control(members), membership);
    set_text(control(room_title),
             current
                 ? current->title + (current->group && !current->joined ? " — join to send" : "")
                 : "Select a user or group to open a room.");
    std::string history;
    if (current) {
        for (const auto& line : current->lines) {
            history += line.sender + ": " + line.text + "\r\n";
        }
    }
    if (history != displayed_transcript_) {
        set_text(control(transcript), history);
        displayed_transcript_ = history;
        SendMessageW(control(transcript), EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(control(transcript), EM_SCROLLCARET, 0, 0);
    }
    // Preserve caret/typing through snapshots; only replace text on navigation/ACK.
    const auto draft = current ? current->draft : "";
    if (displayed_key_ != (current ? current->key : "") || text(control(message)) != draft) {
        set_text(control(message), draft);
    }
    displayed_key_ = current ? current->key : "";
    const bool can_send =
        connected_ && current && current->available && (!current->group || current->joined);
    EnableWindow(control(send), can_send);
    EnableWindow(control(message), can_send);
    EnableWindow(control(join_group),
                 connected_ && current && current->group && current->available && !current->joined);
    EnableWindow(control(create_group), connected_);
    EnableWindow(control(group_name), connected_);
    EnableWindow(control(connect), !worker_.running());
    EnableWindow(control(disconnect), worker_.running());
    for (const auto id : {host, port, username}) {
        EnableWindow(control(id), !worker_.running());
    }
}
LRESULT CALLBACK Window::procedure(HWND window, UINT message_id, WPARAM wparam, LPARAM lparam) {
    // Win32 stores application pointers in pointer-sized integer message slots.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message_id == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
        return DefWindowProcW(window, message_id, wparam, lparam);
    }
    try {
        switch (message_id) {
        case WM_CREATE:
            self->controls();
            return 0;
        case WM_SIZE:
            self->layout(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_GETMINMAXINFO: {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {850, 700};
            return 0;
        }
        case WM_COMMAND:
            self->command(LOWORD(wparam), HIWORD(wparam));
            return 0;
        case WM_TIMER:
            self->pump();
            return 0;
        case WM_DESTROY:
            self->worker_.disconnect();
            self->window_ = nullptr;
            if (self->quit_on_destroy_) {
                PostQuitMessage(0);
            }
            return 0;
        }
    } catch (const std::exception& error) {
        if (message_id == WM_CREATE) {
            return -1;
        }
        self->say(error.what());
    }
    return DefWindowProcW(window, message_id, wparam, lparam);
}
} // namespace chat::gui
