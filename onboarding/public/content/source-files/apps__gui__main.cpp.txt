/** @file
 * @brief Run the native Windows room GUI with a separate networking worker.
 */
#include "chat/transport/app_options.hpp"
#include "window.hpp"
#include <exception>
#include <memory>
#include <shellapi.h>

namespace {
std::string utf8(const wchar_t* value) {
    const auto count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (count == 0) {
        throw std::invalid_argument("invalid Unicode argument");
    }
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), count, nullptr,
                        nullptr);
    result.pop_back();
    return result;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        int argc = 0;
        auto* raw = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!raw) {
            throw std::runtime_error("could not parse command line");
        }
        const auto release = [](wchar_t** args) {
            static_cast<void>(LocalFree(static_cast<void*>(args)));
        };
        std::unique_ptr<wchar_t*, decltype(release)> argv(raw, release);
        std::vector<std::string> args;
        for (int index = 1; index < argc; ++index) {
            args.push_back(utf8(argv.get()[index]));
        }
        const auto options = chat::transport::client_app_options(args);
        if (!options.operands.empty()) {
            throw std::invalid_argument("unexpected GUI arguments");
        }
        chat::gui::Window application(true, {}, options.tls);
        const auto window = application.create(instance);
        MSG message{};
        int result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result == -1 ? 1 : 0;
    } catch (const std::exception&) {
        MessageBoxW(nullptr,
                    L"Start with --tls <CA-file> [--server-name <DNS/IP>] or --plaintext-dev.\n"
                    L"Check certificate paths and the TLS setup guide if startup fails.",
                    L"Socket Chat", MB_OK | MB_ICONERROR);
        return 1;
    }
}
