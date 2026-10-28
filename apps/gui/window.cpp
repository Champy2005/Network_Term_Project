/** @file
 * @brief Implement native dark-theme Unicode room controls, double-buffered GDI chat bubbles, and
 * integrated AI Copilot controls while keeping every socket call off the UI thread.
 */
#include "window.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <string>
#include <vector>

namespace chat::gui {
namespace {
constexpr wchar_t kClass[] = L"SocketChatRoomWindow";
constexpr wchar_t kBubbleClass[] = L"SocketChatBubbleCanvas";

constexpr COLORREF kColBg = RGB(11, 15, 25);           // #0B0F19 Deep Slate
constexpr COLORREF kColSidebar = RGB(17, 24, 39);      // #111827 Charcoal Sidebar
constexpr COLORREF kColCanvas = RGB(13, 18, 30);       // #0D121E Chat Canvas
constexpr COLORREF kColInput = RGB(30, 41, 59);        // #1E293B Surface / Input
constexpr COLORREF kColBorder = RGB(45, 58, 82);       // #2D3A52 Subtle Border
constexpr COLORREF kColAiBg = RGB(23, 21, 58);         // #17153A AI Copilot Dock
constexpr COLORREF kColSelfBubble = RGB(79, 70, 229);  // #4F46E5 Indigo Accent
constexpr COLORREF kColSelfBorder = RGB(99, 102, 241); // #6366F1 Indigo Highlight
constexpr COLORREF kColPeerBubble = RGB(30, 41, 59);   // #1E293B Peer Slate Bubble
constexpr COLORREF kColPeerBorder = RGB(51, 65, 85);   // #334155 Peer Border
constexpr COLORREF kColTextMain = RGB(241, 245, 249);  // #F1F5F9 Primary Text
constexpr COLORREF kColTextMuted = RGB(148, 163, 184); // #94A3B8 Muted Label Text
constexpr COLORREF kColTextAi = RGB(199, 210, 254);    // #C7D2FE AI Output Text

COLORREF avatar_color_for(std::string_view sender) {
    static constexpr std::array<COLORREF, 8> kPalette = {
        RGB(16, 185, 129),  // Emerald
        RGB(245, 158, 11),  // Amber
        RGB(236, 72, 153),  // Pink
        RGB(14, 165, 233),  // Sky
        RGB(168, 85, 247),  // Purple
        RGB(244, 63, 94),   // Rose
        RGB(20, 184, 166),  // Teal
        RGB(59, 130, 246)   // Blue
    };
    std::size_t hash = 5381;
    for (const unsigned char ch : sender) {
        hash = ((hash << 5U) + hash) + ch;
    }
    return kPalette[hash % kPalette.size()];
}

void enable_dark_title_bar(HWND hwnd) {
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) {
        dwm = LoadLibraryW(L"dwmapi.dll");
    }
    if (!dwm) {
        return;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    // NOLINTNEXTLINE(clang-diagnostic-cast-function-type-strict)
    const auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
        GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (fn) {
        const BOOL dark = TRUE;
        static_cast<void>(fn(hwnd, 20, &dark, sizeof(dark)));
        static_cast<void>(fn(hwnd, 19, &dark, sizeof(dark)));
    }
}

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

Window::Window(bool quit_on_destroy, client::WorkerLimits limits, transport::ClientTlsOptions tls,
               std::optional<std::uint16_t> agent_port)
    : worker_(limits, std::move(tls)),
      agent_(
          model_, [this] { return connected_; },
          [this](client::CommandKind kind, const std::string& target, const std::string& body) {
              return submit(kind, target, body);
          },
          [this](const std::string& output) { say_ai(output); }),
      agent_server_(agent_port ? std::make_unique<ai::LoopbackAgentServer>(*agent_port) : nullptr),
      quit_on_destroy_(quit_on_destroy) {
    agent_.set_draft_callback([this](const std::string& draft_val) {
        if (window_ && control(message)) {
            set_text(control(message), draft_val);
        }
    });
}

Window::~Window() {
    worker_.disconnect();
    if (window_) {
        DestroyWindow(window_);
    }
    for (HFONT f : {font_ui_, font_bold_, font_title_, font_small_}) {
        if (f) {
            DeleteObject(f);
        }
    }
    for (HBRUSH b : {brush_bg_, brush_sidebar_, brush_input_, brush_ai_}) {
        if (b) {
            DeleteObject(b);
        }
    }
}

std::uint16_t Window::agent_port() const noexcept {
    return agent_server_ ? agent_server_->port() : 0;
}

HWND Window::create(HINSTANCE instance, bool visible) {
    instance_ = instance;
    if (!brush_bg_) {
        brush_bg_ = CreateSolidBrush(kColBg);
        brush_sidebar_ = CreateSolidBrush(kColSidebar);
        brush_input_ = CreateSolidBrush(kColInput);
        brush_ai_ = CreateSolidBrush(kColAiBg);
    }
    if (!font_ui_) {
        font_ui_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        font_bold_ = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        font_title_ = CreateFontW(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        font_small_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    WNDCLASSEXW bubble_cls{};
    bubble_cls.cbSize = sizeof(bubble_cls);
    bubble_cls.lpfnWndProc = bubble_procedure;
    bubble_cls.hInstance = instance;
    bubble_cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bubble_cls.hbrBackground = nullptr;
    bubble_cls.lpszClassName = kBubbleClass;
    if (!RegisterClassExW(&bubble_cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("could not register bubble canvas class");
    }

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = procedure;
    description.hInstance = instance;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hbrBackground = brush_bg_;
    description.lpszClassName = kClass;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("could not register GUI window");
    }
    const auto result = CreateWindowExW(
        WS_EX_CONTROLPARENT, kClass, L"Socket Chat — Modern Dark Edition + AI Copilot",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1160, 800, nullptr, nullptr, instance,
        this);
    if (!result) {
        throw std::runtime_error("could not create GUI window");
    }
    enable_dark_title_bar(result);
    if (visible) {
        ShowWindow(result, SW_SHOW);
        UpdateWindow(result);
    }
    return result;
}

HWND Window::control(int id) const { return GetDlgItem(window_, id); }
void Window::say(const std::string& value) { set_text(control(status), value); }
void Window::say_ai(const std::string& value) {
    ai_text_ = value;
    if (window_ && control(ai_output)) {
        set_text(control(ai_output), value);
    }
}

LRESULT CALLBACK Window::edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || !self->orig_edit_proc_) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
        const int id = GetDlgCtrlID(hwnd);
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (id == message && !shift) {
            self->command(send, BN_CLICKED);
            return 0;
        }
        if (id == ai_prompt) {
            self->command(ai_run, BN_CLICKED);
            return 0;
        }
        if (id == username) {
            self->command(connect, BN_CLICKED);
            return 0;
        }
        if (id == group_name) {
            self->command(create_group, BN_CLICKED);
            return 0;
        }
    }
    if (msg == WM_CHAR && wparam == '\r') {
        const int id = GetDlgCtrlID(hwnd);
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if ((id == message && !shift) || id == ai_prompt || id == username || id == group_name) {
            return 0;
        }
    }
    return CallWindowProcW(self->orig_edit_proc_, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK Window::bubble_procedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        self->paint_bubbles(hwnd);
        return 0;
    case WM_MOUSEWHEEL: {
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        self->scroll_offset_ =
            std::clamp(self->scroll_offset_ - (delta / 3), 0, std::max(0, self->max_scroll_));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Window::paint_bubbles(HWND canvas) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(canvas, &ps);
    RECT rc{};
    GetClientRect(canvas, &rc);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));

    HDC mem_dc = CreateCompatibleDC(hdc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    HBRUSH canvas_brush = CreateSolidBrush(kColCanvas);
    FillRect(mem_dc, &rc, canvas_brush);
    DeleteObject(canvas_brush);

    HPEN border_pen = CreatePen(PS_SOLID, 1, kColBorder);
    HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
    HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
    Rectangle(mem_dc, 0, 0, width, height);

    SetBkMode(mem_dc, TRANSPARENT);
    const auto* current = model_.current();
    const HFONT ui_font = font_ui_ ? font_ui_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const HFONT bold_font = font_bold_ ? font_bold_ : ui_font;
    const HFONT title_font = font_title_ ? font_title_ : bold_font;
    const HFONT small_font = font_small_ ? font_small_ : ui_font;

    if (!current || current->lines.empty()) {
        SelectObject(mem_dc, title_font);
        SetTextColor(mem_dc, kColTextMain);
        RECT title_rc{24, height / 2 - 44, width - 24, height / 2 - 12};
        const std::wstring headline =
            current ? wide("✨ " + current->title)
                    : L"💬 Welcome to Socket Chat";
        DrawTextW(mem_dc, headline.c_str(), -1, &title_rc,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

        SelectObject(mem_dc, ui_font);
        SetTextColor(mem_dc, kColTextMuted);
        RECT sub_rc{32, height / 2 - 4, width - 32, height / 2 + 56};
        const wchar_t* subtitle =
            current
                ? L"No messages in this room yet. Say hello below, click 'Load History', or type "
                  L"@ai <prompt>!"
                : L"Connect at the top bar, then select an online user or group in the left "
                  L"sidebar.\r\nTip: Type @ai <prompt> in the message box or use the AI Copilot "
                  L"dock below.";
        DrawTextW(mem_dc, subtitle, -1, &sub_rc, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    } else {
        struct BubbleLayout {
            const client::ChatLine* line = nullptr;
            bool is_self = false;
            int box_w = 0;
            int box_h = 0;
            int y = 0;
        };
        const int max_bubble_w = std::max(180, std::min(520, width - 110));
        std::vector<BubbleLayout> items;
        items.reserve(current->lines.size());
        int total_h = 16;

        for (const auto& line : current->lines) {
            const bool is_self = (!model_.username().empty() && line.sender == model_.username());
            const std::wstring body_w = wide(line.text);
            SelectObject(mem_dc, ui_font);
            RECT calc{0, 0, max_bubble_w - 28, 0};
            DrawTextW(mem_dc, body_w.c_str(), -1, &calc,
                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);

            std::string header_str = is_self ? "You" : line.sender;
            if (line.room_sequence > 0) {
                header_str += "  #" + std::to_string(line.room_sequence);
            }
            const std::wstring header_w = wide(header_str);
            SelectObject(mem_dc, small_font);
            RECT hdr_calc{0, 0, max_bubble_w - 28, 0};
            DrawTextW(mem_dc, header_w.c_str(), -1, &hdr_calc,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);

            const int content_w =
                std::max<int>(calc.right - calc.left, hdr_calc.right - hdr_calc.left);
            const int box_w = std::clamp(content_w + 28, 110, max_bubble_w);
            const int box_h = (calc.bottom - calc.top) + 34;
            items.push_back({&line, is_self, box_w, box_h, total_h});
            total_h += box_h + 10;
        }
        total_h += 8;
        max_scroll_ = std::max(0, total_h - height);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll_);

        HRGN clip = CreateRectRgn(2, 2, width - 2, height - 2);
        SelectClipRgn(mem_dc, clip);

        for (const auto& item : items) {
            const int draw_y = item.y - scroll_offset_;
            if (draw_y + item.box_h < 0 || draw_y > height) {
                continue;
            }
            const int draw_x = item.is_self ? (width - item.box_w - 18) : 56;

            if (!item.is_self) {
                // Draw circular user avatar with initial on the left
                const COLORREF av_col = avatar_color_for(item.line->sender);
                HBRUSH av_brush = CreateSolidBrush(av_col);
                HPEN av_pen = CreatePen(PS_SOLID, 1, av_col);
                SelectObject(mem_dc, av_brush);
                SelectObject(mem_dc, av_pen);
                Ellipse(mem_dc, 14, draw_y + 2, 46, draw_y + 34);
                SelectObject(mem_dc, bold_font);
                SetTextColor(mem_dc, RGB(255, 255, 255));
                wchar_t initial[2] = {L'?', L'\0'};
                if (!item.line->sender.empty()) {
                    const char c = item.line->sender.front();
                    initial[0] = static_cast<wchar_t>((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c);
                }
                RECT av_rc{14, draw_y + 2, 46, draw_y + 34};
                DrawTextW(mem_dc, initial, 1, &av_rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                DeleteObject(av_brush);
                DeleteObject(av_pen);
            }

            // Draw rounded message bubble
            const COLORREF fill_col = item.is_self ? kColSelfBubble : kColPeerBubble;
            const COLORREF line_col = item.is_self ? kColSelfBorder : kColPeerBorder;
            HBRUSH b_brush = CreateSolidBrush(fill_col);
            HPEN b_pen = CreatePen(PS_SOLID, 1, line_col);
            SelectObject(mem_dc, b_brush);
            SelectObject(mem_dc, b_pen);
            RoundRect(mem_dc, draw_x, draw_y, draw_x + item.box_w, draw_y + item.box_h, 14, 14);
            DeleteObject(b_brush);
            DeleteObject(b_pen);

            // Header: Sender name + sequence badge
            SelectObject(mem_dc, small_font);
            SetTextColor(mem_dc,
                         item.is_self ? RGB(199, 210, 254) : avatar_color_for(item.line->sender));
            std::string hdr = item.is_self ? "You" : item.line->sender;
            if (item.line->room_sequence > 0) {
                hdr += "  •  #" + std::to_string(item.line->room_sequence);
            }
            const std::wstring hdr_w = wide(hdr);
            RECT hdr_rc{draw_x + 14, draw_y + 6, draw_x + item.box_w - 12, draw_y + 22};
            DrawTextW(mem_dc, hdr_w.c_str(), -1, &hdr_rc,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            // Body text
            SelectObject(mem_dc, ui_font);
            SetTextColor(mem_dc, kColTextMain);
            const std::wstring body_w = wide(item.line->text);
            RECT body_rc{draw_x + 14, draw_y + 23, draw_x + item.box_w - 14,
                         draw_y + item.box_h - 6};
            DrawTextW(mem_dc, body_w.c_str(), -1, &body_rc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectClipRgn(mem_dc, nullptr);
        DeleteObject(clip);
    }

    SelectObject(mem_dc, old_pen);
    SelectObject(mem_dc, old_brush);
    DeleteObject(border_pen);

    BitBlt(hdc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    EndPaint(canvas, &ps);
}

void Window::controls() {
    const HFONT base_font =
        font_ui_ ? font_ui_ : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto add = [&](const wchar_t* kind, const wchar_t* caption, int id, DWORD style,
                         bool visible = true) {
        const DWORD vis_flag = visible ? WS_VISIBLE : 0;
        const auto child = CreateWindowExW(
            0, kind, caption, WS_CHILD | vis_flag | style, 0, 0, 1, 1, window_,
            // Win32 encodes child control IDs in its HMENU parameter.
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (!child) {
            throw std::runtime_error("could not create GUI control");
        }
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(base_font), TRUE);
        return child;
    };
    add(L"STATIC", L"Host", 200, 0);
    add(L"EDIT", L"127.0.0.1", host, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"STATIC", L"Port", 201, 0);
    add(L"EDIT", L"5555", port, WS_BORDER | WS_TABSTOP | ES_NUMBER);
    add(L"STATIC", L"Username", 202, 0);
    const HWND user_edit = add(L"EDIT", L"", username, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"BUTTON", L"Connect", connect, WS_TABSTOP);
    add(L"BUTTON", L"Disconnect", disconnect, WS_TABSTOP);
    add(L"STATIC", L"🟢 Online Users (click to DM)", 203, 0);
    add(L"LISTBOX", L"", users, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);
    add(L"STATIC", L"👥 Discoverable Groups", 204, 0);
    add(L"LISTBOX", L"", groups, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);
    add(L"STATIC", L"Select a group to see its members.", members, 0);
    add(L"STATIC", L"New Group Name", 205, 0);
    const HWND grp_edit = add(L"EDIT", L"", group_name, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"BUTTON", L"+ Create", create_group, WS_TABSTOP);
    add(L"BUTTON", L"Join Group", join_group, WS_TABSTOP);
    add(L"BUTTON", L"📜 History", load_history, WS_TABSTOP);
    add(L"STATIC", L"💬 Active Conversations", 206, 0);
    add(L"LISTBOX", L"", rooms, WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY);

    const HWND title_ctrl =
        add(L"STATIC", L"Select a user or group to open a room.", room_title, 0);
    if (font_title_) {
        SendMessageW(title_ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font_title_), TRUE);
    }

    // Backing transcript EDIT kept synchronized for automated test assertions
    add(L"EDIT", L"", transcript,
        WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, false);

    // Custom double-buffered GDI Chat Bubble Canvas
    const auto canvas = CreateWindowExW(
        0, kBubbleClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, window_,
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(bubble_canvas)), instance_, this);
    if (!canvas) {
        throw std::runtime_error("could not create bubble canvas");
    }

    const HWND msg_edit = add(
        L"EDIT", L"", message,
        WS_BORDER | WS_VSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN);
    add(L"BUTTON", L"Send ↵", send, WS_TABSTOP);
    add(L"STATIC", L"🤖 AI Copilot", 207, 0);
    const HWND ai_edit = add(L"EDIT", L"", ai_prompt, WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    add(L"BUTTON", L"Ask AI", ai_run, WS_TABSTOP);
    add(L"BUTTON", L"✨ Reply", ai_smart_reply, WS_TABSTOP);
    add(L"BUTTON", L"🪄 Polish", ai_polish, WS_TABSTOP);
    add(L"BUTTON", L"📋 Actions", ai_actions, WS_TABSTOP);
    add(L"BUTTON", L"📝 Summary", ai_summarize, WS_TABSTOP);
    add(L"BUTTON", L"🎭 Mood", ai_sentiment, WS_TABSTOP);
    add(L"EDIT",
        L"AI Copilot ready — try 'create group DevTeam and tell Bob hi', '✨ Reply', '🪄 Polish', "
        L"or type '@ai <question>' directly in chat!",
        ai_output, WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
    add(L"STATIC", L"Choose a username and connect. Transcripts are local to this window.", status,
        0);

    for (HWND edit_hwnd : {msg_edit, ai_edit, user_edit, grp_edit}) {
        SetWindowLongPtrW(edit_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const auto prev = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
            edit_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(edit_subclass_proc)));
        if (!orig_edit_proc_) {
            orig_edit_proc_ = prev;
        }
    }

    SendMessageW(control(host), EM_SETLIMITTEXT, 255, 0);
    SendMessageW(control(username), EM_SETLIMITTEXT, 32, 0);
    SendMessageW(control(port), EM_SETLIMITTEXT, 5, 0);
    SendMessageW(control(group_name), EM_SETLIMITTEXT, 64, 0);
    SendMessageW(control(message), EM_SETLIMITTEXT, 4096, 0);
    SendMessageW(control(ai_prompt), EM_SETLIMITTEXT, 1024, 0);
    SetTimer(window_, 1, 40, nullptr);
    refresh();
}

void Window::layout(int width, int height) {
    const auto move = [&](int id, int x, int y, int w, int h) {
        MoveWindow(control(id), x, y, std::max(1, w), std::max(1, h), TRUE);
    };
    move(200, 14, 12, 38, 22);
    move(host, 54, 9, 175, 26);
    move(201, 240, 12, 34, 22);
    move(port, 276, 9, 64, 26);
    move(202, 352, 12, 68, 22);
    move(username, 424, 9, 155, 26);
    move(connect, 590, 8, 92, 28);
    move(disconnect, 690, 8, 98, 28);

    const int left = 290;
    const int right_x = left + 26;
    const int right = std::max(320, width - right_x - 14);

    move(203, 14, 46, left, 22);
    move(users, 14, 70, left, 104);
    move(204, 14, 182, left, 22);
    move(groups, 14, 206, left, 104);
    move(members, 14, 316, left, 52);
    move(205, 14, 374, left, 20);
    move(group_name, 14, 396, left, 26);
    move(create_group, 14, 428, 88, 28);
    move(join_group, 108, 428, 98, 28);
    move(load_history, 212, 428, 92, 28);
    move(206, 14, 466, left, 22);
    move(rooms, 14, 490, left, std::max(60, height - 536));

    move(room_title, right_x, 46, right, 28);
    const int canvas_h = std::max(120, height - 322);
    move(transcript, right_x, 78, right, canvas_h);
    move(bubble_canvas, right_x, 78, right, canvas_h);

    const int comp_y = 78 + canvas_h + 10;
    move(message, right_x, comp_y, std::max(100, right - 102), 58);
    move(send, right_x + right - 94, comp_y, 94, 58);

    const int ai_y = comp_y + 68;
    move(207, right_x, ai_y + 3, 94, 22);
    const int btn_total_w = 486;
    const int prompt_w = std::max(80, right - 98 - btn_total_w);
    move(ai_prompt, right_x + 96, ai_y, prompt_w, 26);

    int bx = right_x + 96 + prompt_w + 6;
    move(ai_run, bx, ai_y, 68, 26);
    bx += 72;
    move(ai_smart_reply, bx, ai_y, 76, 26);
    bx += 80;
    move(ai_polish, bx, ai_y, 74, 26);
    bx += 78;
    move(ai_actions, bx, ai_y, 82, 26);
    bx += 86;
    move(ai_summarize, bx, ai_y, 88, 26);
    bx += 92;
    move(ai_sentiment, bx, ai_y, 74, 26);

    move(ai_output, right_x, ai_y + 32, right, 64);
    move(status, 14, height - 34, width - 28, 26);
}

void Window::save_draft() { model_.draft(text(control(message))); }

std::optional<std::uint64_t> Window::submit(client::CommandKind kind, const std::string& target,
                                            const std::string& body) {
    if (pending_.size() >= 64) {
        say("Wait for pending requests to finish.");
        return std::nullopt;
    }
    const auto id = worker_.submit(kind, target, body);
    if (!id) {
        say("Cannot queue this request: check the text size/target or wait for pending work.");
        return std::nullopt;
    }
    pending_.emplace(*id, Pending{kind, model_.current() ? model_.current()->key : "", body});
    say("Request pending...");
    return id;
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
        static_cast<void>(submit(client::CommandKind::create_group, text(control(group_name))));
    } else if (id == join_group && notification == BN_CLICKED && model_.current() &&
               model_.current()->group) {
        static_cast<void>(submit(client::CommandKind::join_group, model_.current()->target));
    } else if (id == load_history && notification == BN_CLICKED && model_.current()) {
        const auto room_id = !model_.current()->wire_id.empty() ? model_.current()->wire_id
                                                                : model_.current()->target;
        static_cast<void>(submit(client::CommandKind::history, room_id));
    } else if (id == send && notification == BN_CLICKED && model_.current()) {
        save_draft();
        const std::string draft_val = model_.current()->draft;
        if (draft_val.starts_with("@ai ") || draft_val.starts_with("/ai ") ||
            draft_val.starts_with("@AI ") || draft_val.starts_with("/AI ")) {
            model_.draft({});
            set_text(control(message), {});
            static_cast<void>(agent_.execute_prompt(draft_val.substr(4)));
            refresh();
            return;
        }
        static_cast<void>(
            submit(model_.current()->group ? client::CommandKind::group : client::CommandKind::direct,
                   model_.current()->target, draft_val));
    } else if (id == ai_smart_reply && notification == BN_CLICKED) {
        save_draft();
        static_cast<void>(agent_.smart_reply(false));
        refresh();
    } else if (id == ai_polish && notification == BN_CLICKED) {
        save_draft();
        std::string draft_to_polish = text(control(message));
        if (draft_to_polish.empty()) {
            draft_to_polish = text(control(ai_prompt));
        }
        static_cast<void>(agent_.polish_draft(draft_to_polish));
        refresh();
    } else if (id == ai_actions && notification == BN_CLICKED) {
        save_draft();
        static_cast<void>(agent_.extract_action_items());
        refresh();
    } else if (id == ai_summarize && notification == BN_CLICKED) {
        save_draft();
        static_cast<void>(agent_.summarize_room());
        refresh();
    } else if (id == ai_sentiment && notification == BN_CLICKED) {
        save_draft();
        static_cast<void>(agent_.analyze_sentiment(text(control(ai_prompt))));
        refresh();
    } else if (id == ai_run && notification == BN_CLICKED) {
        save_draft();
        static_cast<void>(agent_.execute_prompt(text(control(ai_prompt))));
        refresh();
    }
}

void Window::pump() {
    const auto prev_key = model_.current() ? model_.current()->key : std::string{};
    const auto prev_ai = ai_text_;
    const auto prev_pending = pending_.size();
    if (agent_server_) {
        agent_server_->poll(agent_);
    }
    const auto updates = worker_.poll();
    if (updates.empty()) {
        if (prev_key != (model_.current() ? model_.current()->key : std::string{}) ||
            prev_ai != ai_text_ || prev_pending != pending_.size()) {
            refresh();
        }
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
        } else if (const auto* history = std::get_if<client::WorkerHistory>(&update)) {
            model_.apply_history(history->page);
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
    if (history != displayed_transcript_ || displayed_key_ != (current ? current->key : "")) {
        set_text(control(transcript), history);
        displayed_transcript_ = history;
        scroll_offset_ = 999999; // Auto-scroll bubble canvas to newest message
        if (HWND canvas = control(bubble_canvas)) {
            InvalidateRect(canvas, nullptr, FALSE);
        }
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
    EnableWindow(control(load_history),
                 connected_ && current && (!current->wire_id.empty() || current->group) &&
                     (!current->group || current->joined));
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
        case WM_ERASEBKGND: {
            if (self->brush_bg_) {
                // NOLINTNEXTLINE(performance-no-int-to-ptr)
                HDC hdc = reinterpret_cast<HDC>(wparam);
                RECT rc{};
                GetClientRect(window, &rc);
                FillRect(hdc, &rc, self->brush_bg_);
                return 1;
            }
            break;
        }
        case WM_CTLCOLORSTATIC: {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HDC hdc = reinterpret_cast<HDC>(wparam);
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HWND ctrl = reinterpret_cast<HWND>(lparam);
            const int cid = GetDlgCtrlID(ctrl);
            if (cid == ai_output && self->brush_ai_) {
                SetTextColor(hdc, kColTextAi);
                SetBkColor(hdc, kColAiBg);
                return reinterpret_cast<LRESULT>(self->brush_ai_);
            }
            SetTextColor(hdc, cid == room_title ? kColTextMain : kColTextMuted);
            SetBkColor(hdc, kColBg);
            return reinterpret_cast<LRESULT>(self->brush_bg_);
        }
        case WM_CTLCOLOREDIT: {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HDC hdc = reinterpret_cast<HDC>(wparam);
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HWND ctrl = reinterpret_cast<HWND>(lparam);
            const int cid = GetDlgCtrlID(ctrl);
            if (cid == ai_output && self->brush_ai_) {
                SetTextColor(hdc, kColTextAi);
                SetBkColor(hdc, kColAiBg);
                return reinterpret_cast<LRESULT>(self->brush_ai_);
            }
            SetTextColor(hdc, kColTextMain);
            SetBkColor(hdc, kColInput);
            return reinterpret_cast<LRESULT>(self->brush_input_);
        }
        case WM_CTLCOLORLISTBOX: {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kColTextMain);
            SetBkColor(hdc, kColSidebar);
            return reinterpret_cast<LRESULT>(self->brush_sidebar_);
        }
        case WM_GETMINMAXINFO: {
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {920, 700};
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
