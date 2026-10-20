/** @file
 * @brief Expose the headless chat API under its application-facing ChatClient name.
 */
#pragma once

#include "chat/client/ping_client.hpp"

namespace chat::client {
/// Retains source compatibility with the original PingClient diagnostics.
using ChatClient = PingClient;
} // namespace chat::client
