/** @file
 * @brief Generate random message retry keys in the transport library's crypto-enabled build.
 */
#include "chat/protocol/message_identity.hpp"
#include <array>
#include <openssl/rand.h>
#include <stdexcept>

namespace chat::protocol {
std::string new_message_key() {
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("message key generation unavailable");
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string key;
    key.reserve(64);
    for (const auto byte : bytes) {
        key += digits[byte >> 4U];
        key += digits[byte & 15U];
    }
    return key;
}
} // namespace chat::protocol
