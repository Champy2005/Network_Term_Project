/** @file
 * @brief Bind the chat server and drain socket cancellation after termination signals.
 */
#include "chat/server/ping_server.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/asio/signal_set.hpp>
#include <charconv>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

int main(int argc, char* argv[]) {
    try {
        chat::transport::ServerTlsOptions tls;
        int first = 1;
        if (argc >= 2 && std::string_view(argv[1]) == "--plaintext-dev") {
            first = 2;
        } else if (argc >= 4 && std::string_view(argv[1]) == "--tls") {
            tls.enabled = true;
            tls.certificate_chain_file = argv[2];
            tls.private_key_file = argv[3];
            first = 4;
        } else {
            throw std::invalid_argument(
                "choose --tls <certificate-chain> <private-key> or --plaintext-dev");
        }
        std::vector<std::string_view> operands;
        for (int index = first; index < argc; ++index) {
            if (std::string_view(argv[index]).starts_with("--")) {
                throw std::invalid_argument("unknown or mixed transport options");
            }
            operands.emplace_back(argv[index]);
        }
        if (operands.size() > 4) {
            throw std::invalid_argument("usage: chat_server <transport-mode> [bind-address] [port] "
                                        "[database-path] [idle-timeout-ms]");
        }
        unsigned int port = 5555;
        if (operands.size() >= 2) {
            const auto value = operands[1];
            const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
            if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
                port > 65535) {
                throw std::invalid_argument("port must be between 0 and 65535");
            }
        }
        const auto address = boost::asio::ip::make_address(
            operands.empty() ? "127.0.0.1" : std::string(operands[0]));
        boost::asio::io_context context;
        const std::string database =
            operands.size() >= 3 ? std::string(operands[2]) : "data/chat.sqlite3";
        if (operands.size() < 3) {
            std::filesystem::create_directories("data");
        }
        chat::server::SessionLimits limits;
        if (operands.size() == 4) {
            unsigned int milliseconds = 0;
            const auto value = operands[3];
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), milliseconds);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                milliseconds > 86400000) {
                throw std::invalid_argument(
                    "idle timeout must be 0..86400000 milliseconds; zero disables it");
            }
            limits.idle_timeout = std::chrono::milliseconds(milliseconds);
        }
        chat::server::PingServer server(context, {address, static_cast<std::uint16_t>(port)},
                                        database, limits, tls);
        boost::asio::signal_set signals(context, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code& error, int) {
            if (!error) {
                server.stop();
            }
        });
        server.start();
        std::cout << "Ping server listening on " << address.to_string() << ':' << server.port()
                  << (tls.enabled ? " TLS1.3" : " plaintext-development-loopback")
                  << " (Ctrl+C to stop)\n"
                  << std::flush;
        context.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Server: " << error.what() << '\n';
        return 1;
    }
}
