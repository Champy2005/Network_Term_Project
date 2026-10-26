/** @file
 * @brief Require TLS 1.3 and explicit CA trust, valid server credentials, and DNS/IP SAN
 * verification.
 */
#include "chat/transport/socket_stream.hpp"
#include <boost/asio/ip/address.hpp>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

namespace chat::transport {
namespace {
void versions(boost::asio::ssl::context& context) {
    if (SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_3_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(context.native_handle(), TLS1_3_VERSION) != 1) {
        throw TlsError("TLS 1.3 is unavailable");
    }
}
bool file_name(const std::string& name) {
    return !name.empty() && name.find('\0') == std::string::npos;
}
} // namespace
std::shared_ptr<boost::asio::ssl::context> server_tls_context(const ServerTlsOptions& options) {
    if (options.handshake_timeout.count() <= 0 ||
        options.handshake_timeout > std::chrono::seconds(60)) {
        throw std::invalid_argument("TLS handshake timeout must be 1..60000 ms");
    }
    if (!options.enabled) {
        if (!options.certificate_chain_file.empty() || !options.private_key_file.empty()) {
            throw std::invalid_argument("TLS credentials require enabled TLS");
        }
        return {};
    }
    if (!file_name(options.certificate_chain_file) || !file_name(options.private_key_file)) {
        throw TlsError("TLS certificate and private key files are required");
    }
    try {
        auto result =
            std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_server);
        versions(*result);
        // Never prompt interactively for a key password inside startup or tests.
        result->set_password_callback(
            [](std::size_t, boost::asio::ssl::context::password_purpose) { return std::string{}; });
        result->use_certificate_chain_file(options.certificate_chain_file);
        result->use_private_key_file(options.private_key_file, boost::asio::ssl::context::pem);
        if (SSL_CTX_check_private_key(result->native_handle()) != 1) {
            throw TlsError("TLS private key does not match certificate");
        }
        return result;
    } catch (const boost::system::system_error&) {
        throw TlsError("TLS server certificate/key configuration failed");
    }
}
std::shared_ptr<boost::asio::ssl::context> client_tls_context(const ClientTlsOptions& options) {
    if (!options.enabled) {
        if (!options.ca_file.empty() || !options.server_name.empty()) {
            throw std::invalid_argument("TLS trust/name configuration requires enabled TLS");
        }
        return {};
    }
    if (!file_name(options.ca_file)) {
        throw TlsError("TLS CA bundle is required");
    }
    try {
        auto result =
            std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
        versions(*result);
        result->set_verify_mode(boost::asio::ssl::verify_peer);
        result->load_verify_file(options.ca_file);
        return result;
    } catch (const boost::system::system_error&) {
        throw TlsError("TLS CA configuration failed");
    }
}
void verify_tls_peer(SocketStream& stream, const std::string& name) {
    if (name.empty() || name.size() > 253 || name.find('\0') != std::string::npos) {
        throw TlsError("TLS expected DNS/IP name is invalid");
    }
    auto* ssl = stream.tls().native_handle();
    SSL_set_hostflags(ssl,
                      X509_CHECK_FLAG_NEVER_CHECK_SUBJECT | X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    boost::system::error_code error;
    static_cast<void>(boost::asio::ip::make_address(name, error));
    if (!error) {
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), name.c_str()) != 1) {
            throw TlsError("TLS IP verification setup failed");
        }
    } else if (SSL_set1_host(ssl, name.c_str()) != 1 ||
               SSL_set_tlsext_host_name(ssl, name.c_str()) != 1) {
        throw TlsError("TLS DNS verification setup failed");
    }
}
} // namespace chat::transport
