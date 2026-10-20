/** @file
 * @brief Verify TLS 1.3 trust/SAN/validity, framing, privacy, deadlines, cancellation, and
 * recovery.
 */
#include "chat/client/chat_worker.hpp"
#include "chat/client/room_model.hpp"
#include "chat/server/ping_server.hpp"
#include "recovery_peer.hpp"
#include "tls_certificates.hpp"
#include <atomic>
#include <boost/asio/ssl/error.hpp>
#include <gtest/gtest.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
using chat::client::ChatClient;
using chat::test::tcp;
using chat::transport::TlsError;
template <class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 4s;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}
/// @brief Independent OpenSSL client with deadline-bound encrypted reads for framing/version tests.
class TlsPeer {
  public:
    TlsPeer(std::uint16_t port, const std::string& ca, bool old_version = false)
        : ssl(boost::asio::ssl::context::tls_client), stream(context, ssl) {
        ssl.load_verify_file(ca);
        stream.set_verify_mode(boost::asio::ssl::verify_peer);
        const auto version = old_version ? TLS1_2_VERSION : TLS1_3_VERSION;
        if (SSL_set_min_proto_version(stream.native_handle(), version) != 1 ||
            SSL_set_max_proto_version(stream.native_handle(), version) != 1 ||
            X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(stream.native_handle()), "127.0.0.1") !=
                1) {
            throw std::runtime_error("test TLS configuration failed");
        }
        stream.next_layer().connect({boost::asio::ip::address_v4::loopback(), port});
        stream.handshake(boost::asio::ssl::stream_base::client);
    }
    void send(const std::vector<std::uint8_t>& bytes) {
        boost::asio::write(stream, boost::asio::buffer(bytes));
    }
    chat::test::Peer::Result receive(std::size_t expected, std::chrono::milliseconds timeout = 1s) {
        context.restart();
        chat::test::Peer::Result result;
        boost::asio::steady_timer timer(context, timeout);
        timer.async_wait([&](const boost::system::error_code& error) {
            if (!error) {
                result.timed_out = true;
                boost::system::error_code ignored;
                static_cast<void>(stream.next_layer().cancel(ignored));
            }
        });
        std::array<std::uint8_t, 8192> input{};
        std::function<void()> read;
        read = [&] {
            stream.async_read_some(
                boost::asio::buffer(input),
                [&](const boost::system::error_code& error, std::size_t count) {
                    if (error) {
                        result.error = error;
                        timer.cancel();
                        return;
                    }
                    for (const auto& frame :
                         decoder.feed(std::span<const std::uint8_t>(input.data(), count))) {
                        chat::protocol::Envelope message;
                        if (!message.ParseFromArray(frame.data(), static_cast<int>(frame.size()))) {
                            throw std::runtime_error("bad test TLS payload");
                        }
                        result.messages.push_back(std::move(message));
                    }
                    if (result.messages.size() >= expected) {
                        timer.cancel();
                    } else {
                        read();
                    }
                });
        };
        read();
        context.run();
        return result;
    }
    boost::asio::io_context context;
    boost::asio::ssl::context ssl;
    boost::asio::ssl::stream<tcp::socket> stream;
    chat::protocol::FrameDecoder decoder;
};
class TlsTest : public testing::Test {
  protected:
    TlsTest() { start(); }
    ~TlsTest() override { stop(); }
    void start(const std::string& certificate = "server.pem",
               std::chrono::milliseconds handshake = 3s, std::chrono::milliseconds idle = 30s) {
        context.restart();
        auto options = certificates.server(certificate);
        options.handshake_timeout = handshake;
        chat::server::SessionLimits limits;
        limits.idle_timeout = idle;
        server = std::make_unique<chat::server::PingServer>(
            context, tcp::endpoint{boost::asio::ip::address_v4::loopback(), port},
            ":memory:", limits, options);
        port = server->port();
        server->start();
        for (auto& worker : network) {
            worker = std::thread([this] { context.run(); });
        }
    }
    void stop() {
        if (!server) {
            return;
        }
        server->stop();
        for (auto& worker : network) {
            worker.join();
        }
        server.reset();
        context.restart();
        context.poll();
    }
    void login(ChatClient& client, const std::string& name) {
        client.connect("127.0.0.1", port);
        if (!client.login(name).accepted) {
            throw std::runtime_error("test TLS login failed");
        }
    }
    template <class Message> Message delivery(ChatClient& client) {
        std::optional<Message> result;
        until([&] {
            if (auto event = client.next_event(10ms)) {
                if (const auto* message = std::get_if<Message>(&*event)) {
                    result = *message;
                }
            }
            return result.has_value();
        });
        if (!result) {
            throw std::runtime_error("test TLS delivery missing");
        }
        return *result;
    }
    void quiet(ChatClient& client) {
        const auto end = std::chrono::steady_clock::now() + 60ms;
        do {
            if (auto event = client.next_event(5ms)) {
                EXPECT_FALSE(std::holds_alternative<chat::client::DirectDelivery>(*event));
                EXPECT_FALSE(std::holds_alternative<chat::client::GroupDelivery>(*event));
            }
        } while (std::chrono::steady_clock::now() < end);
    }
    chat::test::TlsCertificates certificates;
    boost::asio::io_context context;
    std::unique_ptr<chat::server::PingServer> server;
    std::array<std::thread, 2> network;
    std::uint16_t port = 0;
};

/// @brief Correct explicit roots and DNS/IP SANs establish TLS 1.3 before any application exchange.
TEST_F(TlsTest, ValidDnsAndIpIdentitiesConnectUsingTls13) {
    ChatClient dns(certificates.client("localhost")), ip(certificates.client());
    dns.connect("127.0.0.1", port);
    EXPECT_EQ(dns.ping(42), 42);
    ip.connect("127.0.0.1", port);
    EXPECT_EQ(ip.ping(43), 43);
    TlsPeer peer(port, certificates.file("ca.pem"));
    EXPECT_EQ(SSL_version(peer.stream.native_handle()), TLS1_3_VERSION);
    EXPECT_EQ(SSL_get_verify_result(peer.stream.native_handle()), X509_V_OK);
}

/// @brief Wrong DNS or literal IP SAN is a terminal verification error, never a plaintext retry.
TEST_F(TlsTest, WrongDnsAndIpIdentitiesFailClosed) {
    for (const auto& name : {"wrong.example", "127.0.0.2"}) {
        ChatClient client(certificates.client(name));
        EXPECT_THROW(client.connect("127.0.0.1", port, 400ms), TlsError);
        EXPECT_THROW(static_cast<void>(client.login("alice")), std::runtime_error);
    }
    ChatClient healthy(certificates.client());
    login(healthy, "alice");
}

/// @brief An unrelated trust root cannot authorize this server even when its SAN is correct.
TEST_F(TlsTest, UntrustedCaFailsBeforeLogin) {
    auto options = certificates.client();
    options.ca_file = certificates.file("other-ca.pem");
    ChatClient client(options);
    EXPECT_THROW(client.connect("127.0.0.1", port, 400ms), TlsError);
}

/// @brief Expired/future validity, missing SAN, and invalid signatures all fail native chain
/// checks.
TEST_F(TlsTest, InvalidCertificatesFailWithoutNameOnlyFallback) {
    for (const auto& invalid : {"expired.pem", "future.pem", "no-san.pem", "bad-signature.pem"}) {
        stop();
        start(invalid);
        ChatClient client(certificates.client("localhost"));
        EXPECT_THROW(client.connect("127.0.0.1", port, 400ms), TlsError) << invalid;
    }
}

/// @brief Missing/mismatched/malformed server credentials fail before opening a listener.
TEST_F(TlsTest, InvalidServerConfigurationCannotListen) {
    auto options = certificates.server();
    options.private_key_file = certificates.file("other.key");
    EXPECT_THROW(chat::server::PingServer(context,
                                          tcp::endpoint{boost::asio::ip::address_v4::loopback(), 0},
                                          {}, {}, options),
                 TlsError);
    options.private_key_file = certificates.file("missing.key");
    EXPECT_THROW(static_cast<void>(chat::transport::server_tls_context(options)), TlsError);
    options = certificates.server();
    options.certificate_chain_file = certificates.file("server.key");
    EXPECT_THROW(static_cast<void>(chat::transport::server_tls_context(options)), TlsError);
    options = certificates.server();
    options.handshake_timeout = 0ms;
    EXPECT_THROW(static_cast<void>(chat::transport::server_tls_context(options)),
                 std::invalid_argument);
}

/// @brief Missing CA and invalid configuration fail locally without selecting development TCP.
TEST_F(TlsTest, InvalidClientConfigurationDoesNotDowngrade) {
    for (const auto& path :
         {std::string{}, certificates.file("missing.pem"), certificates.file("server.key")}) {
        ChatClient client({true, path, "localhost"});
        EXPECT_THROW(client.connect("127.0.0.1", port, 400ms), TlsError);
    }
    ChatClient bad_name(certificates.client(std::string("localhost\0ignored", 17)));
    EXPECT_THROW(bad_name.connect("127.0.0.1", port), TlsError);
    ChatClient disabled({false, certificates.file("ca.pem"), "localhost"});
    EXPECT_THROW(disabled.connect("127.0.0.1", port), std::invalid_argument);
}

/// @brief A TLS 1.2-only peer cannot negotiate or send an application request to this listener.
TEST_F(TlsTest, OlderTlsProtocolIsRejected) {
    EXPECT_THROW(TlsPeer(port, certificates.file("ca.pem"), true), boost::system::system_error);
    const auto client_context = chat::transport::client_tls_context(certificates.client());
    EXPECT_EQ(SSL_CTX_get_min_proto_version(client_context->native_handle()), TLS1_3_VERSION);
    EXPECT_EQ(SSL_CTX_get_max_proto_version(client_context->native_handle()), TLS1_3_VERSION);
    ChatClient healthy(certificates.client());
    login(healthy, "alice");
}

/// @brief Plaintext on a TLS listener is closed with no valid Protobuf/application response.
TEST_F(TlsTest, PlaintextIsNeverAutoDetectedOnTlsListener) {
    chat::test::Peer raw(port);
    raw.send(chat::test::encode(chat::test::ping(1)));
    const auto response = raw.receive(1, 400ms);
    EXPECT_TRUE(response.messages.empty());
    EXPECT_FALSE(response.timed_out);
    EXPECT_TRUE(response.error);
    ChatClient healthy(certificates.client());
    login(healthy, "alice");
}

/// @brief Independent encrypted writes retain byte-stream split/coalesced framing and correlation.
TEST_F(TlsTest, EncryptedSplitAndCoalescedFramesStayOrdered) {
    TlsPeer peer(port, certificates.file("ca.pem"));
    const auto first = chat::test::encode(chat::test::ping(42));
    for (const auto byte : first) {
        peer.send({byte});
    }
    auto response = peer.receive(1);
    ASSERT_EQ(response.messages.size(), 1);
    EXPECT_EQ(response.messages.front().pong().nonce(), 42);
    std::vector<std::uint8_t> batch;
    for (std::uint64_t nonce = 0; nonce < 50; ++nonce) {
        const auto frame = chat::test::encode(chat::test::ping(nonce));
        batch.insert(batch.end(), frame.begin(), frame.end());
    }
    peer.send(batch);
    response = peer.receive(50);
    ASSERT_EQ(response.messages.size(), 50);
    for (std::size_t index = 0; index < response.messages.size(); ++index) {
        EXPECT_EQ(response.messages[index].pong().nonce(), index);
    }
}

/// @brief The unchanged 1 MiB frame limit also applies to encrypted records spanning many reads.
TEST_F(TlsTest, MaximumEncryptedFrameIsAccepted) {
    TlsPeer peer(port, certificates.file("ca.pem"));
    auto request = chat::test::ping(7, std::string(chat::protocol::kMaxFrameSize - 12U, 'x'));
    const auto delta = static_cast<std::ptrdiff_t>(chat::protocol::kMaxFrameSize) -
                       static_cast<std::ptrdiff_t>(request.ByteSizeLong());
    request.mutable_request_id()->resize(
        static_cast<std::size_t>(static_cast<std::ptrdiff_t>(request.request_id().size()) + delta));
    ASSERT_EQ(request.ByteSizeLong(), chat::protocol::kMaxFrameSize);
    peer.send(chat::test::encode(request));
    const auto result = peer.receive(1, 2s);
    ASSERT_EQ(result.messages.size(), 1);
    EXPECT_EQ(result.messages.front().request_id(), request.request_id());
}

/// @brief TLS leaves server-authoritative DM/group privacy, retry receipts, and sync unchanged.
TEST_F(TlsTest, EncryptedChatPreservesPrivacyMembershipAndStoredReceipts) {
    ChatClient alice(certificates.client()), bob(certificates.client()),
        charlie(certificates.client());
    login(alice, "alice");
    login(bob, "bob");
    login(charlie, "charlie");
    const auto key = ChatClient::new_message_key();
    const auto accepted = alice.send_direct_with_key("bob", "private TLS text", key);
    ASSERT_TRUE(accepted.accepted);
    EXPECT_EQ(delivery<chat::client::DirectDelivery>(alice).message_id, accepted.message_id);
    EXPECT_EQ(delivery<chat::client::DirectDelivery>(bob).message_id, accepted.message_id);
    quiet(charlie);
    EXPECT_EQ(alice.send_direct_with_key("bob", "private TLS text", key).message_id,
              accepted.message_id);
    quiet(alice);
    quiet(bob);
    EXPECT_EQ(charlie.sync(accepted.resource_id).error,
              chat::client::RequestError::history_forbidden);
    ASSERT_EQ(alice.sync(accepted.resource_id).records.size(), 1);
    const auto group = alice.create_group("Study").resource_id;
    EXPECT_EQ(bob.send_group(group, "denied").error, chat::client::RequestError::not_group_member);
    ASSERT_TRUE(bob.join_group(group).accepted);
    ASSERT_TRUE(bob.send_group(group, "members only TLS").accepted);
    EXPECT_EQ(delivery<chat::client::GroupDelivery>(alice).text, "members only TLS");
    EXPECT_EQ(delivery<chat::client::GroupDelivery>(bob).text, "members only TLS");
    quiet(charlie);
}

/// @brief Silent peers cannot hold prelogin TLS slots indefinitely, even with idle expiry disabled.
TEST_F(TlsTest, HandshakeTimeoutIsIndependentOfIdleExpiry) {
    stop();
    start("server.pem", 80ms, 0ms);
    chat::test::Peer silent(port);
    const auto result = silent.receive(1, 400ms);
    EXPECT_FALSE(result.timed_out);
    EXPECT_TRUE(result.error);
    EXPECT_TRUE(result.messages.empty());
    ChatClient healthy(certificates.client());
    login(healthy, "alice");
}

/// @brief The connect budget covers a silent handshake and closes rather than leaking callbacks.
TEST_F(TlsTest, ClientDeadlineIncludesTlsHandshake) {
    chat::test::Peer silent;
    tcp::acceptor listener(silent.context, {boost::asio::ip::address_v4::loopback(), 0});
    ChatClient client(certificates.client());
    const auto before = std::chrono::steady_clock::now();
    EXPECT_THROW(client.connect("127.0.0.1", listener.local_endpoint().port(), 80ms),
                 chat::client::NetworkError);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 400ms);
    client.connect("127.0.0.1", port);
    EXPECT_EQ(client.ping(1), 1);
}

/// @brief Pending handshake/timer/buffer ownership drains on stop and invokes close exactly once.
TEST_F(TlsTest, StoppingPendingHandshakeReleasesSessionOwnership) {
    boost::asio::io_context local;
    tcp::acceptor listener(local, {boost::asio::ip::address_v4::loopback(), 0});
    chat::test::Peer silent(listener.local_endpoint().port());
    tcp::socket socket(local);
    listener.accept(socket);
    std::atomic<int> closed{0};
    auto session = std::make_shared<chat::server::ClientSession>(
        std::move(socket), boost::asio::make_strand(local),
        [](chat::server::ClientSession&, const chat::protocol::Envelope&) {},
        [&](chat::server::ClientSession&) { ++closed; }, chat::server::SessionLimits{},
        chat::transport::server_tls_context(certificates.server()));
    std::weak_ptr<chat::server::ClientSession> weak = session;
    ASSERT_TRUE(session->send(chat::test::ping(1)));
    session->start();
    std::thread worker([&] { local.run(); });
    session->stop();
    session->stop();
    session.reset();
    worker.join();
    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(closed, 1);
}

/// @brief Development TCP is restricted to loopback at both the listener and resolved endpoints.
TEST_F(TlsTest, PlaintextDevelopmentCannotBindOrConnectOutsideLoopback) {
    EXPECT_THROW(
        chat::server::PingServer(context, tcp::endpoint{boost::asio::ip::address_v4::any(), 0}),
        std::invalid_argument);
    ChatClient client;
    EXPECT_THROW(client.connect("192.0.2.1", 5555, 400ms), std::runtime_error);
}

/// @brief Worker recovery retains TLS trust/identity and replays missing group text through
/// encryption.
TEST_F(TlsTest, WorkerReconnectKeepsTlsAndAppliesEachSequenceOnce) {
    ChatClient bob(certificates.client());
    login(bob, "bob");
    const auto group = bob.create_group("Study").resource_id;
    chat::test::RecoveryProxy proxy(port, true);
    chat::client::WorkerLimits limits;
    limits.reconnect_delay = 40ms;
    limits.reconnect_max_delay = 40ms;
    limits.poll_timeout = 5ms;
    limits.heartbeat_interval = 80ms;
    limits.operation_timeout = 400ms;
    chat::client::ChatWorker alice(limits, certificates.client("localhost"));
    chat::client::RoomModel model;
    bool connected = false;
    std::size_t resumed = 0, reconnecting = 0;
    std::vector<chat::client::WorkerCompleted> completed;
    std::string terminal;
    const auto poll = [&] {
        for (const auto& update : alice.poll()) {
            if (const auto* ready = std::get_if<chat::client::WorkerConnected>(&update)) {
                if (!ready->resumed) {
                    model.reset(ready->username);
                } else {
                    ++resumed;
                }
                connected = true;
            } else if (std::holds_alternative<chat::client::WorkerReconnecting>(update)) {
                ++reconnecting;
                model.disconnected();
            } else if (const auto* event = std::get_if<chat::client::ClientEvent>(&update)) {
                model.apply(*event);
            } else if (const auto* done = std::get_if<chat::client::WorkerCompleted>(&update)) {
                completed.push_back(*done);
            } else if (const auto* offline =
                           std::get_if<chat::client::WorkerDisconnected>(&update)) {
                terminal = offline->reason;
            }
        }
    };
    ASSERT_TRUE(alice.start("127.0.0.1", proxy.port(), "alice"));
    ASSERT_TRUE(until([&] {
        poll();
        return connected;
    }));
    ASSERT_TRUE(alice.submit(chat::client::CommandKind::join_group, group));
    ASSERT_TRUE(until([&] {
        poll();
        return !completed.empty() && !model.groups().empty();
    }));
    model.open_group(group);
    model.draft("unsent TLS draft");
    ASSERT_TRUE(bob.send_group(group, "before TLS loss").accepted);
    ASSERT_TRUE(until([&] {
        poll();
        return model.current()->last_sequence == 1;
    }));
    proxy.interrupt();
    ASSERT_TRUE(until([&] {
        poll();
        return reconnecting > 0;
    }));
    ASSERT_TRUE(bob.send_group(group, "while TLS offline").accepted);
    proxy.paused = false;
    ASSERT_TRUE(until([&] {
        poll();
        return resumed == 1;
    }));
    ASSERT_EQ(model.current()->lines.size(), 2);
    EXPECT_EQ(model.current()->last_sequence, 2);
    EXPECT_EQ(model.current()->draft, "unsent TLS draft");
    EXPECT_TRUE(terminal.empty());
}
/// @brief Reconnect revalidates credentials; an expired replacement ends without login/fallback.
TEST_F(TlsTest, CertificateFailureDuringRecoveryIsTerminal) {
    chat::client::WorkerLimits limits;
    limits.reconnect_delay = 100ms;
    limits.reconnect_max_delay = 100ms;
    limits.operation_timeout = 400ms;
    limits.poll_timeout = 5ms;
    chat::client::ChatWorker worker(limits, certificates.client());
    bool connected = false, reconnecting = false;
    std::size_t resumed = 0;
    std::string terminal;
    const auto poll = [&] {
        for (const auto& update : worker.poll()) {
            if (const auto* ready = std::get_if<chat::client::WorkerConnected>(&update)) {
                connected = true;
                if (ready->resumed) {
                    ++resumed;
                }
            } else if (std::holds_alternative<chat::client::WorkerReconnecting>(update)) {
                reconnecting = true;
            } else if (const auto* offline =
                           std::get_if<chat::client::WorkerDisconnected>(&update)) {
                terminal = offline->reason;
            }
        }
    };
    ASSERT_TRUE(worker.start("127.0.0.1", port, "alice"));
    ASSERT_TRUE(until([&] {
        poll();
        return connected;
    }));
    stop();
    ASSERT_TRUE(until([&] {
        poll();
        return reconnecting;
    }));
    start("expired.pem");
    ASSERT_TRUE(until([&] {
        poll();
        return !worker.running();
    }));
    poll();
    EXPECT_EQ(resumed, 0);
    EXPECT_NE(terminal.find("TLS"), std::string::npos);
}
} // namespace
