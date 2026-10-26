/** @file
 * @brief Preserve associated executors and one ordered write path over either TLS or development
 * TCP.
 */
#pragma once
#include "chat/transport/tls.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <memory>
#include <utility>

namespace chat::transport {
/// @brief Internal stream adapter; callers own cancellation and bind all handlers to one executor.
/// TLS context outlives its stream, including pending handlers retained by the session owner.
class SocketStream {
  public:
    using executor_type = boost::asio::ip::tcp::socket::executor_type;
    /// @brief Transfer a socket; a nonnull context makes all application I/O use TLS.
    explicit SocketStream(boost::asio::ip::tcp::socket socket,
                          std::shared_ptr<boost::asio::ssl::context> context = {})
        : context_(std::move(context)), plain_(socket.get_executor()) {
        if (context_) {
            tls_ = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
                std::move(socket), *context_);
        } else {
            plain_ = std::move(socket);
        }
    }
    /// @brief Expose executor for Asio composed writes without erasing handler executor bindings.
    [[nodiscard]] executor_type get_executor() { return socket().get_executor(); }
    /// @brief Lowest layer is used only for connect, cancel, close, and socket options.
    [[nodiscard]] boost::asio::ip::tcp::socket& socket() {
        return tls_ ? tls_->next_layer() : plain_;
    }
    [[nodiscard]] bool encrypted() const { return tls_ != nullptr; }
    /// @brief Handshake/configuration only; encrypted application bytes use the adapter methods.
    [[nodiscard]] auto& tls() { return *tls_; }
    /// @brief Retain buffers until completion; TLS handlers must share the owning strand.
    template <class Buffers, class Handler>
    void async_read_some(const Buffers& buffers, Handler&& handler) {
        if (tls_) {
            tls_->async_read_some(buffers, std::forward<Handler>(handler));
        } else {
            plain_.async_read_some(buffers, std::forward<Handler>(handler));
        }
    }
    /// @brief One composed writer calls this; preserve its associated executor/cancellation slot.
    template <class Buffers, class Handler>
    void async_write_some(const Buffers& buffers, Handler&& handler) {
        if (tls_) {
            tls_->async_write_some(buffers, std::forward<Handler>(handler));
        } else {
            plain_.async_write_some(buffers, std::forward<Handler>(handler));
        }
    }

  private:
    std::shared_ptr<boost::asio::ssl::context> context_;
    boost::asio::ip::tcp::socket plain_;
    std::unique_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> tls_;
};
/// @brief Validate server credentials/TLS version before listening; null means development mode.
[[nodiscard]] std::shared_ptr<boost::asio::ssl::context>
server_tls_context(const ServerTlsOptions& options);
/// @brief Load only the configured trust bundle; null means embedded development mode.
[[nodiscard]] std::shared_ptr<boost::asio::ssl::context>
client_tls_context(const ClientTlsOptions& options);
/// @brief Bind native certificate verification to the expected SAN and send SNI for DNS names.
void verify_tls_peer(SocketStream& stream, const std::string& name);
} // namespace chat::transport
