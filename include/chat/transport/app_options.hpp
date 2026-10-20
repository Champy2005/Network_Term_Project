/** @file
 * @brief Parse explicit executable transport modes while keeping trust configuration out of GUI
 * controls.
 */
#pragma once
#include "chat/transport/tls.hpp"
#include <vector>

namespace chat::transport {
/// @brief Client mode plus remaining CLI operands; GUI callers require no remaining operands.
struct ClientAppOptions {
    ClientTlsOptions tls;
    std::vector<std::string> operands;
};
/// @brief Require exactly one leading --tls CA or --plaintext-dev; reject mixed/unknown options.
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
    for (; position < args.size(); ++position) {
        if (args[position].starts_with("--")) {
            throw std::invalid_argument("unknown or mixed transport options");
        }
        result.operands.push_back(args[position]);
    }
    return result;
}
} // namespace chat::transport
