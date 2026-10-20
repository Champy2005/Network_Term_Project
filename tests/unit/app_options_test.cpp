/** @file
 * @brief Verify explicit executable modes and reject ambiguous or insecure option combinations.
 */
#include "chat/transport/app_options.hpp"
#include <gtest/gtest.h>

/// @brief Secure client operands preserve trust paths/names while keeping library types GUI
/// independent.
TEST(AppOptionsTest, TlsModeAndExpectedIdentityArePreserved) {
    const auto options = chat::transport::client_app_options(
        {"--tls", "C:/test certificates/ca.pem", "--server-name", "chat.example", "127.0.0.1",
         "5555", "42", "alice"});
    EXPECT_TRUE(options.tls.enabled);
    EXPECT_EQ(options.tls.ca_file, "C:/test certificates/ca.pem");
    EXPECT_EQ(options.tls.server_name, "chat.example");
    EXPECT_EQ(options.operands.size(), 4);
    const auto gui = chat::transport::client_app_options({"--tls", "ca.pem"});
    EXPECT_TRUE(gui.operands.empty());
    EXPECT_TRUE(gui.tls.server_name.empty());
}
/// @brief Development mode is opt-in and does not inherit TLS trust or name settings.
TEST(AppOptionsTest, PlaintextRequiresExplicitDevelopmentFlag) {
    const auto options =
        chat::transport::client_app_options({"--plaintext-dev", "localhost", "5555"});
    EXPECT_FALSE(options.tls.enabled);
    EXPECT_TRUE(options.tls.ca_file.empty());
    EXPECT_TRUE(options.tls.server_name.empty());
    EXPECT_EQ(options.operands.size(), 2);
}
/// @brief Missing modes/values, mixed modes, and unknown flags fail before any network action.
TEST(AppOptionsTest, InvalidAndMixedModesAreRejected) {
    for (const auto& args :
         std::vector<std::vector<std::string>>{{},
                                               {"localhost", "5555"},
                                               {"--tls"},
                                               {"--tls", ""},
                                               {"--tls", "ca.pem", "--server-name"},
                                               {"--tls", "ca.pem", "--server-name", ""},
                                               {"--tls", "ca.pem", "--plaintext-dev"},
                                               {"--plaintext-dev", "--tls", "ca.pem"},
                                               {"--tls", "ca.pem", "--unknown"},
                                               {"--plaintext-dev", "--server-name", "localhost"}}) {
        EXPECT_THROW(static_cast<void>(chat::transport::client_app_options(args)),
                     std::invalid_argument);
    }
}
