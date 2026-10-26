/** @file
 * @brief Declare the TCP acceptor facade whose sessions share one hub strand.
 */
#pragma once

#include "chat/server/client_session.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace chat::server {

/// TCP chat server (retains its original API name). Hub and sessions share one strand.
/// start()/stop() are thread safe; the caller runs and drains the context.
class PingServer {
  public:
    /// @brief Open optional history storage, then bind/listen; empty path is ephemeral.
    /// Port zero selects an ephemeral port. Storage failure prevents listening.
    /// SessionLimits expires silent peers after 30 seconds by default; zero disables expiry.
    PingServer(boost::asio::io_context& context, const boost::asio::ip::tcp::endpoint& endpoint,
               const std::string& database_path = {}, SessionLimits session_limits = {},
               const transport::ServerTlsOptions& tls = {});
    /// @brief Schedule cancellation; the caller must drain the context afterward.
    ~PingServer();
    /// @brief Bind/listen immediately; port zero selects an ephemeral port.
    PingServer(const PingServer&) = delete;
    PingServer& operator=(const PingServer&) = delete;

    /// @brief Return the actual bound port, including an OS-assigned ephemeral port.
    [[nodiscard]] std::uint16_t port() const;
    /// @brief Begin accepting connections once.
    void start();
    /// @brief Cancel acceptance and all sessions; repeated calls are harmless.
    void stop();

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace chat::server
