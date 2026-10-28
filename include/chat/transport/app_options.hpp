/** @file
 * @brief Parse explicit executable transport modes while keeping trust configuration out of GUI
 * controls.
 */
#pragma once
#include "chat/transport/tls.hpp"
#include <charconv>
#include <cstdint>
#include <optional>
#include <vector>

namespace chat::transport {
/// @brief Client mode plus remaining CLI operands; GUI callers require no remaining operands.
struct ClientAppOptions {
    ClientTlsOptions tls;
    /// @brief Optional loopback-only (127.0.0.1) MCP/JSON-RPC control port (0 selects ephemeral).
    std::optional<std::uint16_t> agent_port;
    std::vector<std::string> operands;
};
/// @brief Require one leading --tls CA or --plaintext-dev; allow optional --agent-port; reject
/// mixed/unknown options.
inline ClientAppOptions client_app_options(const std::vector<std::string>& args) {
    ClientAppOptions result;
    std::size_t position = 0;
    if (!args.empty() && args[0] == "--plaintext-dev") {
        position = 1;
    } else if (args.size() >= 2 && args[0] == "--tls" && !args[1].empty() &&
               !args[1].starts_with("--")) {
        result.tls.enabled = true;
        result.tls.ca_file = args[1];
        position = 2;
        if (position < args.size() && args[position] == "--server-name") {
            ++position;
            if (position == args.size() || args[position].empty() ||
                args[position].starts_with("--")) {
                throw std::invalid_argument("--server-name requires an expected DNS/IP SAN");
            }
            result.tls.server_name = args[position++];
        }
    } else {
        throw std::invalid_argument(
            "choose --tls <CA-file> [--server-name <DNS/IP>] or --plaintext-dev");
    }
    if (position < args.size() && args[position] == "--agent-port") {
        ++position;
        if (position == args.size() || args[position].empty() || args[position].starts_with("--")) {
            throw std::invalid_argument("--agent-port requires a port in 0..65535");
        }
        unsigned int number = 0;
        const auto& raw = args[position++];
        const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size() || number > 65535) {
            throw std::invalid_argument("--agent-port must be an integer between 0 and 65535");
        }
        result.agent_port = static_cast<std::uint16_t>(number);
    }
    for (; position < args.size(); ++position) {
        if (args[position].starts_with("--")) {
            throw std::invalid_argument("unknown or mixed transport options");
        }
        result.operands.push_back(args[position]);
    }
    return result;
}
} // namespace chat::transport
