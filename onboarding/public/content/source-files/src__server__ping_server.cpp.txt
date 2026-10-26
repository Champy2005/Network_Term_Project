/** @file
 * @brief Accept bounded connections and defer hub cleanup to avoid routing reentrancy.
 */
#include "chat/server/ping_server.hpp"
#include "chat/server/chat_hub.hpp"
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <stdexcept>

namespace chat::server {
namespace {
using boost::asio::ip::tcp;
constexpr std::size_t kMaxConnections = 64;
} // namespace

class PingServer::Impl : public std::enable_shared_from_this<PingServer::Impl> {
  public:
    Impl(boost::asio::io_context& context, const tcp::endpoint& endpoint,
         const std::string& database_path, SessionLimits session_limits,
         const transport::ServerTlsOptions& tls)
        : executor_(boost::asio::make_strand(context)), acceptor_(context), hub_(database_path),
          session_limits_(session_limits), tls_context_(transport::server_tls_context(tls)),
          handshake_timeout_(tls.handshake_timeout) {
        if (!tls_context_ && !endpoint.address().is_loopback()) {
            throw std::invalid_argument(
                "plaintext development server must bind a loopback address");
        }
        if (session_limits_.queued_bytes == 0 || session_limits_.queued_frames == 0 ||
            session_limits_.idle_timeout.count() < 0 ||
            session_limits_.idle_timeout > std::chrono::hours(24)) {
            throw std::invalid_argument("invalid server session limits");
        }
        acceptor_.open(endpoint.protocol());
        acceptor_.bind(endpoint);
        acceptor_.listen();
        port_ = acceptor_.local_endpoint().port();
    }
    void start() {
        boost::asio::dispatch(executor_, [self = shared_from_this()] {
            if (!self->started_ && !self->stopped_) {
                self->started_ = true;
                self->accept();
            }
        });
    }
    void stop() {
        boost::asio::dispatch(executor_, [self = shared_from_this()] {
            if (!self->stopped_) {
                self->stopped_ = true;
                boost::system::error_code ignored;
                static_cast<void>(self->acceptor_.close(ignored));
                self->hub_.stop();
            }
        });
    }
    std::uint16_t port_ = 0;

  private:
    void accept() {
        acceptor_.async_accept(boost::asio::bind_executor(
            executor_, [self = shared_from_this()](const boost::system::error_code& error,
                                                   tcp::socket socket) {
                if (self->stopped_) {
                    return;
                }
                if (error) {
                    self->stop();
                    return;
                }
                if (self->hub_.size() < kMaxConnections) {
                    const auto owner = std::weak_ptr<Impl>(self);
                    auto session = std::make_shared<ClientSession>(
                        std::move(socket), self->executor_,
                        [owner](ClientSession& client, const protocol::Envelope& request) {
                            if (const auto server = owner.lock()) {
                                server->hub_.handle(client, request);
                            }
                        },
                        [owner](ClientSession& client) {
                            if (const auto server = owner.lock()) {
                                auto retained = client.shared_from_this();
                                // Avoid registry/broadcast reentrancy when send overflow closes a
                                // session.
                                boost::asio::post(server->executor_, [server, retained] {
                                    server->hub_.remove(*retained);
                                });
                            }
                        },
                        self->session_limits_, self->tls_context_, self->handshake_timeout_);
                    self->hub_.attach(session);
                    session->start();
                }
                self->accept();
            }));
    }
    ClientSession::Executor executor_;
    tcp::acceptor acceptor_;
    ChatHub hub_;
    SessionLimits session_limits_;
    std::shared_ptr<boost::asio::ssl::context> tls_context_;
    std::chrono::milliseconds handshake_timeout_;
    bool started_ = false;
    bool stopped_ = false;
};
PingServer::PingServer(boost::asio::io_context& context,
                       const boost::asio::ip::tcp::endpoint& endpoint,
                       const std::string& database_path, SessionLimits session_limits,
                       const transport::ServerTlsOptions& tls)
    : impl_(std::make_shared<Impl>(context, endpoint, database_path, session_limits,
                                   tls)) {}
PingServer::~PingServer() { impl_->stop(); }
std::uint16_t PingServer::port() const { return impl_->port_; }
void PingServer::start() { impl_->start(); }
void PingServer::stop() { impl_->stop(); }
} // namespace chat::server
