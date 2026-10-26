/** @file
 * @brief Declare GUI-independent TLS configuration with explicit trust and no insecure fallback.
 */
#pragma once
#include <chrono>
#include <stdexcept>
#include <string>

namespace chat::transport {
/// @brief Certificate/negotiation/configuration failure; never eligible for plaintext fallback.
class TlsError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
/// @brief TLS 1.3 client trust; disabled defaults preserve embedded development/test callers.
struct ClientTlsOptions {
    /// @brief Enabled requires a CA file and always verifies chain, validity, and SAN identity.
    bool enabled = false;
    /// @brief PEM CA bundle; only these explicit roots are trusted.
    std::string ca_file;
    /// @brief Optional expected DNS/IP SAN; empty verifies the connect host itself.
    std::string server_name;
};
/// @brief TLS 1.3 server credentials, loaded before opening the listener.
struct ServerTlsOptions {
    /// @brief Disabled is embedded development mode; executables require an explicit mode flag.
    bool enabled = false;
    /// @brief PEM leaf followed by optional intermediate certificates.
    std::string certificate_chain_file;
    /// @brief Matching unencrypted PEM private key, kept out of source control.
    std::string private_key_file;
    /// @brief Independent handshake bound, even when application idle expiry is disabled.
    std::chrono::milliseconds handshake_timeout{3000};
};
} // namespace chat::transport
