/** @file
 * @brief Generate isolated short-lived test CAs and SAN certificates without committed private
 * keys.
 */
#pragma once
#include "chat/client/chat_client.hpp"
#include <filesystem>
#include <memory>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace chat::test {
/// @brief Temporary certificate corpus for trust, identity, validity, signature, and key tests.
class TlsCertificates {
    using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
    static void check(bool okay) {
        if (!okay) {
            throw std::runtime_error("test certificate generation failed");
        }
    }
    static Key key() {
        Key result(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
        check(result != nullptr);
        return result;
    }
    static void extension(X509* certificate, X509* issuer, int nid, const char* value) {
        X509V3_CTX context{};
        X509V3_set_ctx(&context, issuer, certificate, nullptr, nullptr, 0);
        std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> field(
            X509V3_EXT_conf_nid(nullptr, &context, nid, value), X509_EXTENSION_free);
        check(field != nullptr && X509_add_ext(certificate, field.get(), -1) == 1);
    }
    Cert certificate(EVP_PKEY* subject, X509* issuer, EVP_PKEY* signing, bool ca, long before = -60,
                     long after = 86400, bool san = true) {
        Cert result(X509_new(), X509_free);
        check(result != nullptr);
        check(X509_set_version(result.get(), 2) == 1);
        check(ASN1_INTEGER_set(X509_get_serialNumber(result.get()), ++serial_) == 1);
        check(X509_gmtime_adj(X509_getm_notBefore(result.get()), before) != nullptr);
        check(X509_gmtime_adj(X509_getm_notAfter(result.get()), after) != nullptr);
        check(X509_set_pubkey(result.get(), subject) == 1);
        auto* name = X509_get_subject_name(result.get());
        const std::string common = ca ? "Temporary Chat Test CA" : "localhost";
        check(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                         reinterpret_cast<const unsigned char*>(common.c_str()), -1,
                                         -1, 0) == 1);
        check(X509_set_issuer_name(result.get(), issuer ? X509_get_subject_name(issuer) : name) ==
              1);
        extension(result.get(), issuer, NID_basic_constraints,
                  ca ? "critical,CA:TRUE" : "critical,CA:FALSE");
        extension(result.get(), issuer, NID_key_usage,
                  ca ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature");
        if (!ca) {
            extension(result.get(), issuer, NID_ext_key_usage, "serverAuth");
            if (san) {
                extension(result.get(), issuer, NID_subject_alt_name,
                          "DNS:localhost,IP:127.0.0.1,IP:::1");
            }
        }
        check(X509_sign(result.get(), signing, EVP_sha256()) > 0);
        return result;
    }
    void save(const std::string& name, X509* certificate) {
        std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new_file(file(name).c_str(), "w"),
                                                         BIO_free);
        check(output != nullptr && PEM_write_bio_X509(output.get(), certificate) == 1);
    }
    void save(const std::string& name, EVP_PKEY* key) {
        std::unique_ptr<BIO, decltype(&BIO_free)> output(BIO_new_file(file(name).c_str(), "w"),
                                                         BIO_free);
        check(output != nullptr && PEM_write_bio_PrivateKey(output.get(), key, nullptr, nullptr, 0,
                                                            nullptr, nullptr) == 1);
    }

  public:
    TlsCertificates() {
        root_ = std::filesystem::absolute("build/test-data").lexically_normal();
        directory_ = root_ / ("tls-" + client::ChatClient::new_message_key());
        std::filesystem::create_directories(directory_);
        auto ca_key = key(), leaf_key = key(), other_key = key();
        auto ca = certificate(ca_key.get(), nullptr, ca_key.get(), true);
        auto other = certificate(other_key.get(), nullptr, other_key.get(), true);
        auto leaf = certificate(leaf_key.get(), ca.get(), ca_key.get(), false);
        auto expired = certificate(leaf_key.get(), ca.get(), ca_key.get(), false, -3600, -60);
        auto future = certificate(leaf_key.get(), ca.get(), ca_key.get(), false, 3600, 86400);
        auto no_san = certificate(leaf_key.get(), ca.get(), ca_key.get(), false, -60, 86400, false);
        auto bad_signature = certificate(leaf_key.get(), ca.get(), other_key.get(), false);
        save("ca.pem", ca.get());
        save("other-ca.pem", other.get());
        save("server.pem", leaf.get());
        save("expired.pem", expired.get());
        save("future.pem", future.get());
        save("no-san.pem", no_san.get());
        save("bad-signature.pem", bad_signature.get());
        save("server.key", leaf_key.get());
        save("other.key", other_key.get());
    }
    ~TlsCertificates() {
        std::error_code ignored;
        if (directory_.parent_path() == root_ &&
            directory_.filename().string().starts_with("tls-")) {
            std::filesystem::remove_all(directory_, ignored);
        }
    }
    [[nodiscard]] std::string file(const std::string& name) const {
        return (directory_ / name).string();
    }
    [[nodiscard]] transport::ServerTlsOptions server(const std::string& cert = "server.pem") const {
        return {true, file(cert), file("server.key"), std::chrono::milliseconds{3000}};
    }
    [[nodiscard]] transport::ClientTlsOptions client(const std::string& expected = {}) const {
        return {true, file("ca.pem"), expected};
    }

  private:
    std::filesystem::path root_, directory_;
    long serial_ = 0;
};
} // namespace chat::test
