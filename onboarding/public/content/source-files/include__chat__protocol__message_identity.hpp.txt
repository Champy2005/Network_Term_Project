/** @file
 * @brief Share bounded retry-key validation, random key creation, and message clock metadata.
 */
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

namespace chat::protocol {
/// @brief Retry keys fit 1-64 ASCII letters, digits, underscores, or hyphens.
inline bool valid_message_key(const std::string& key) {
    return !key.empty() && key.size() <= 64 &&
           std::all_of(key.begin(), key.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-';
           });
}
/// @brief Generate a random 256-bit key; callers may save it before attempting a send.
std::string new_message_key();
/// @brief Server wall-clock timestamp; sequence numbers, not clocks, establish room order.
inline std::uint64_t message_timestamp() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}
} // namespace chat::protocol
