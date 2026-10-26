/** @file
 * @brief Run persistent worker-backed chat or preserve the original Ping/login diagnostics.
 */
#include "chat/client/ping_client.hpp"
#include "chat/transport/app_options.hpp"
#include "chat_console.hpp"
#include "console_input.hpp"

#include <charconv>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
} // namespace

int main(int argc, char* argv[]) {
    try {
        std::vector<std::string> arguments;
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }
        const auto options = chat::transport::client_app_options(arguments);
        auto operands = options.operands;
        const bool chat = !operands.empty() && operands[0] == "chat";
        if (chat) {
            operands.erase(operands.begin());
        }
        if (chat && operands.size() != 3) {
            throw std::invalid_argument(
                "usage: chat_cli <transport-mode> chat <host> <port> <username>");
        }
        if (operands.size() < 2 || operands.size() > 4) {
            throw std::invalid_argument(
                "usage: chat_cli <transport-mode> <host> <port> [nonce] [username]");
        }
        const auto parse = [](const std::string& value) {
            std::uint64_t number = 0;
            const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
                throw std::invalid_argument("port and nonce must be unsigned decimal integers");
            }
            return number;
        };
        const auto port = parse(operands[1]);
        if (port == 0 || port > 65535) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
        if (chat) {
            chat::cli::ConsoleInput input;
            chat::cli::LineInput lines;
            chat::cli::ChatConsole console(std::cout, {}, options.tls);
            if (!console.start(operands[0], static_cast<std::uint16_t>(port), operands[2])) {
                throw std::invalid_argument("invalid chat host, port, or username");
            }
            std::signal(SIGINT, interrupt);
            std::signal(SIGTERM, interrupt);
            bool logged_in = false, eof = false;
            while (!console.finished()) {
                console.poll();
                logged_in = logged_in || console.ready();
                if (interrupted) {
                    // Interrupt stops recovery/queued operations instead of waiting on stdin.
                    console.request_exit();
                    console.request_exit();
                    interrupted = 0;
                    eof = true;
                }
                if (logged_in && !eof && !console.exiting() && !console.finished()) {
                    const auto chunk = input.poll();
                    for (const auto& line : lines.feed(chunk.bytes)) {
                        console.execute(line);
                    }
                    if (chunk.eof) {
                        for (const auto& line : lines.finish()) {
                            console.execute(line);
                        }
                        if (!console.exiting()) {
                            console.request_exit();
                        }
                        eof = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return console.exit_code();
        }
        const auto nonce = operands.size() >= 3 ? parse(operands[2]) : 1;
        chat::client::PingClient client(options.tls);
        client.connect(operands[0], static_cast<std::uint16_t>(port));
        if (operands.size() == 4) {
            const auto login = client.login(operands[3]);
            if (!login.accepted) {
                std::cerr << "Login rejected: " << login.reason << '\n';
                return 1;
            }
            std::cout << "Logged in as " << login.username << '\n';
        }
        const auto pong = client.ping(nonce);
        std::cout << "Pong nonce=" << pong << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Client: " << error.what() << '\n';
        return 1;
    }
}
