/** @file
 * @brief Read bounded console events or ready pipe/file bytes while network updates keep flowing.
 */
#include "console_input.hpp"
#include "chat_console.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace chat::cli {
class ConsoleInput::Impl {
  public:
#ifdef _WIN32
    Impl() {
        if (!handle_ || handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("stdin handle is unavailable");
        }
        DWORD mode = 0;
        console_ = GetConsoleMode(handle_, &mode) != 0;
        output_console_ = GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) != 0;
        if (output_console_) {
            output_cp_ = GetConsoleOutputCP();
            if (!SetConsoleOutputCP(CP_UTF8)) {
                throw std::runtime_error("cannot select UTF-8 console output");
            }
        }
    }
    ~Impl() {
        if (output_console_) {
            static_cast<void>(SetConsoleOutputCP(output_cp_));
        }
    }
    InputChunk poll() {
        if (console_) {
            return console();
        }
        DWORD available = static_cast<DWORD>(kInputChunk);
        if (GetFileType(handle_) == FILE_TYPE_PIPE) {
            if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) {
                    return {{}, true};
                }
                throw std::runtime_error("cannot poll stdin pipe");
            }
            if (available == 0) {
                return {};
            }
        } else if (GetFileType(handle_) != FILE_TYPE_DISK) {
            throw std::runtime_error("unsupported stdin handle");
        }
        std::array<char, kInputChunk> buffer{};
        DWORD count = 0;
        const auto requested = (std::min)(available, static_cast<DWORD>(buffer.size()));
        if (!ReadFile(handle_, buffer.data(), requested, &count, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF) {
                return {{}, true};
            }
            throw std::runtime_error("cannot read stdin");
        }
        return {std::string(buffer.data(), count), count == 0};
    }

  private:
    InputChunk console() {
        DWORD available = 0;
        if (!GetNumberOfConsoleInputEvents(handle_, &available)) {
            throw std::runtime_error("cannot poll console input");
        }
        if (available == 0) {
            return {};
        }
        // One event per poll bounds even very large key-repeat counts or pasted input.
        INPUT_RECORD record{};
        DWORD count = 0;
        if (!ReadConsoleInputW(handle_, &record, 1, &count)) {
            throw std::runtime_error("cannot read console input");
        }
        if (count == 0 || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
            return {};
        }
        const auto character = record.Event.KeyEvent.uChar.UnicodeChar;
        if (character == 4 || character == 26) {
            return {{}, true};
        }
        if (character == L'\r') {
            high_surrogate_ = 0;
            std::cout << '\n' << std::flush;
            return {"\n", false};
        }
        if (character == L'\b') {
            high_surrogate_ = 0;
            std::cout << "\b \b" << std::flush;
            return {"\b", false};
        }
        if (character < 32 || character == 127) {
            return {};
        }
        if (character >= 0xd800 && character <= 0xdbff) {
            high_surrogate_ = character;
            return {};
        }
        std::array<wchar_t, 2> wide{};
        int length = 1;
        if (character >= 0xdc00 && character <= 0xdfff) {
            if (!high_surrogate_) {
                return {};
            }
            wide = {high_surrogate_, character};
            length = 2;
        } else {
            wide[0] = character;
        }
        high_surrogate_ = 0;
        std::array<char, 4> utf8{};
        const auto size =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), length, utf8.data(),
                                static_cast<int>(utf8.size()), nullptr, nullptr);
        if (size == 0) {
            throw std::runtime_error("invalid console Unicode input");
        }
        const std::string text(utf8.data(), static_cast<std::size_t>(size));
        std::cout << text << std::flush;
        return {text, false};
    }
    HANDLE handle_ = GetStdHandle(STD_INPUT_HANDLE);
    bool console_ = false, output_console_ = false;
    UINT output_cp_ = 0;
    wchar_t high_surrogate_ = 0;
#else
    InputChunk poll() {
        pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const auto ready = ::poll(&descriptor, 1, 0);
        if (ready < 0) {
            if (errno == EINTR) {
                return {};
            }
            throw std::runtime_error("cannot poll stdin");
        }
        if (ready == 0) {
            return {};
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            throw std::runtime_error("stdin polling failed");
        }
        std::array<char, kInputChunk> buffer{};
        const auto count = ::read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                return {};
            }
            throw std::runtime_error("cannot read stdin");
        }
        return {std::string(buffer.data(), static_cast<std::size_t>(count)), count == 0};
    }
#endif
};
ConsoleInput::ConsoleInput() : impl_(std::make_unique<Impl>()) {}
ConsoleInput::~ConsoleInput() = default;
InputChunk ConsoleInput::poll() { return impl_->poll(); }
} // namespace chat::cli
