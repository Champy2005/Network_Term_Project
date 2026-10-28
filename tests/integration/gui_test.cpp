/** @file
 * @brief Exercise real hidden Windows controls against sockets without manual UI acceptance.
 */
#include "chat/client/chat_client.hpp"
#include "chat/server/ping_server.hpp"
#include "recovery_peer.hpp"
#include "test_peer.hpp"
#include "tls_certificates.hpp"
#include "window.hpp"
#include <gtest/gtest.h>
#include <thread>

namespace {
using namespace std::chrono_literals;
template <class Predicate> bool until(chat::gui::Window& window, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        window.pump();
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
void input(chat::gui::Window& window, int id, const std::wstring& value) {
    SetWindowTextW(GetDlgItem(window.handle(), id), value.c_str());
}
void click(chat::gui::Window& window, int id) {
    SendMessageW(window.handle(), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
}
void select(chat::gui::Window& window, int id, std::size_t index) {
    SendMessageW(GetDlgItem(window.handle(), id), LB_SETCURSEL, index, 0);
    SendMessageW(window.handle(), WM_COMMAND, MAKEWPARAM(id, LBN_SELCHANGE), 0);
}
std::wstring caption(chat::gui::Window& window, int id) {
    const auto control = GetDlgItem(window.handle(), id);
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    const auto read = GetWindowTextW(control, value.data(), static_cast<int>(value.size()));
    value.resize(static_cast<std::size_t>(read));
    return value;
}
class GuiTest : public testing::Test {
  protected:
    GuiTest()
        : server(context, {boost::asio::ip::address_v4::loopback(), 0}, ":memory:"),
          network([this] { context.run(); }) {
        server.start();
    }
    ~GuiTest() override {
        server.stop();
        guard.reset();
        network.join();
    }
    void connect(chat::gui::Window& window, const std::wstring& name) {
        ASSERT_NE(window.create(GetModuleHandleW(nullptr), false), nullptr);
        input(window, chat::gui::port, std::to_wstring(server.port()));
        input(window, chat::gui::username, name);
        click(window, chat::gui::connect);
    }
    boost::asio::io_context context;
    chat::server::PingServer server;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{
        context.get_executor()};
    std::thread network;
};
/// @brief Real controls preserve separate drafts/views, Unicode delivery, creator-only membership,
/// and third-client privacy.
TEST_F(GuiTest, ControlsCompleteDirectAndVoluntaryGroupFlowsWithoutCrossRoomText) {
    chat::client::ChatClient bob, charlie;
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    charlie.connect("127.0.0.1", server.port());
    ASSERT_TRUE(charlie.login("charlie").accepted);
    chat::gui::Window window(false);
    connect(window, L"alice");
    ASSERT_TRUE(until(window, [&] { return window.model().users().size() == 3; }));
    EXPECT_EQ(SendMessageW(GetDlgItem(window.handle(), chat::gui::users), LB_GETCOUNT, 0, 0), 3);
    select(window, chat::gui::users, 1);
    ASSERT_NE(window.model().current(), nullptr);
    EXPECT_EQ(window.model().current()->target, "bob");
    input(window, chat::gui::message, L"hello \u0E44\u0E17\u0E22");
    click(window, chat::gui::send);
    ASSERT_TRUE(until(window, [&] {
        return window.model().current()->lines.size() == 1 &&
               caption(window, chat::gui::message).empty();
    }));
    EXPECT_EQ(caption(window, chat::gui::transcript), L"alice: hello \u0E44\u0E17\u0E22\r\n");
    bool delivered = false;
    ASSERT_TRUE(until(window, [&] {
        if (auto event = bob.next_event(10ms)) {
            if (auto* direct = std::get_if<chat::client::DirectDelivery>(&*event)) {
                delivered = direct->text == "hello ไทย";
            }
        }
        return delivered;
    }));
    ASSERT_TRUE(bob.send_direct("alice", std::string("before\0after", 12)).accepted);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->lines.size() == 2; }));
    EXPECT_NE(caption(window, chat::gui::transcript).find(L"before␀after"), std::wstring::npos);
    input(window, chat::gui::message, L"unsent draft for Bob");
    input(window, chat::gui::group_name, L"Study");
    click(window, chat::gui::create_group);
    ASSERT_TRUE(until(window, [&] { return window.model().groups().size() == 1; }));
    ASSERT_EQ(window.model().groups()[0].members, std::vector<std::string>{"alice"});
    const auto group = window.model().groups()[0].group_id;
    select(window, chat::gui::groups, 0);
    EXPECT_TRUE(caption(window, chat::gui::transcript).empty());
    EXPECT_TRUE(caption(window, chat::gui::message).empty());
    ASSERT_TRUE(bob.join_group(group).accepted);
    ASSERT_TRUE(until(window, [&] { return window.model().groups()[0].members.size() == 2; }));
    input(window, chat::gui::message, L"group text");
    click(window, chat::gui::send);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->lines.size() == 1; }));
    delivered = false;
    ASSERT_TRUE(until(window, [&] {
        if (auto event = bob.next_event(10ms)) {
            if (auto* message = std::get_if<chat::client::GroupDelivery>(&*event)) {
                delivered = message->text == "group text";
            }
        }
        return delivered;
    }));
    select(window, chat::gui::users, 1);
    EXPECT_EQ(caption(window, chat::gui::message), L"unsent draft for Bob");
    EXPECT_EQ(window.model().current()->lines.size(), 2);
    EXPECT_EQ(caption(window, chat::gui::transcript).find(L"group text"), std::wstring::npos);
    const auto end = std::chrono::steady_clock::now() + 100ms;
    do {
        if (auto event = charlie.next_event(10ms)) {
            EXPECT_FALSE(std::holds_alternative<chat::client::DirectDelivery>(*event));
            EXPECT_FALSE(std::holds_alternative<chat::client::GroupDelivery>(*event));
        }
    } while (std::chrono::steady_clock::now() < end);
    click(window, chat::gui::disconnect);
    ASSERT_TRUE(until(
        window, [&] { return IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::connect)); }));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
}
/// @brief Explicit GUI join enables sending; oversized Unicode drafts survive presence updates.
TEST_F(GuiTest, NonmemberControlsRequireAnExplicitJoinAndKeepOversizedDrafts) {
    chat::client::ChatClient creator;
    creator.connect("127.0.0.1", server.port());
    ASSERT_TRUE(creator.login("alice").accepted);
    ASSERT_TRUE(creator.create_group("Study").accepted);
    chat::gui::Window window(false);
    connect(window, L"bob");
    ASSERT_TRUE(until(window, [&] { return window.model().groups().size() == 1; }));
    select(window, chat::gui::groups, 0);
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::join_group)));
    EXPECT_EQ(window.model().groups()[0].members, std::vector<std::string>{"alice"});
    click(window, chat::gui::join_group);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->joined; }));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    const std::wstring oversized(2000, L'ไ');
    input(window, chat::gui::message, oversized);
    click(window, chat::gui::send);
    EXPECT_NE(caption(window, chat::gui::status).find(L"Cannot queue"), std::wstring::npos);
    EXPECT_EQ(caption(window, chat::gui::message), oversized);
    EXPECT_TRUE(window.model().current()->lines.empty());
    chat::client::ChatClient observer;
    observer.connect("127.0.0.1", server.port());
    ASSERT_TRUE(observer.login("charlie").accepted);
    ASSERT_TRUE(until(window, [&] { return window.model().users().size() == 3; }));
    EXPECT_EQ(caption(window, chat::gui::message), oversized);
    input(window, chat::gui::message, L"joined from GUI");
    click(window, chat::gui::send);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->lines.size() == 1; }));
    EXPECT_EQ(window.model().current()->lines.front().text, "joined from GUI");
}
/// @brief Duplicate-name errors appear in the real status control without enabling chat.
TEST_F(GuiTest, RejectedLoginLeavesConnectionControlsUsable) {
    chat::client::ChatClient owner;
    owner.connect("127.0.0.1", server.port());
    ASSERT_TRUE(owner.login("alice").accepted);
    chat::gui::Window window(false);
    connect(window, L"alice");
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"Login rejected") != std::wstring::npos;
    }));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::connect)));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    EXPECT_TRUE(window.model().users().empty());
    input(window, chat::gui::username, L"different");
    click(window, chat::gui::connect);
    ASSERT_TRUE(until(window, [&] { return window.model().username() == "different"; }));
}
/// @brief A silent server times out on the worker while window messages continue to execute.
TEST_F(GuiTest, WindowStaysResponsiveWhileLoginWaitsAndTimesOut) {
    boost::asio::io_context silent_context;
    boost::asio::ip::tcp::acceptor acceptor(silent_context,
                                            {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::WorkerLimits limits;
    limits.operation_timeout = 200ms;
    chat::gui::Window window(false, limits);
    ASSERT_NE(window.create(GetModuleHandleW(nullptr), false), nullptr);
    input(window, chat::gui::port, std::to_wstring(acceptor.local_endpoint().port()));
    input(window, chat::gui::username, L"alice");
    const auto before = std::chrono::steady_clock::now();
    click(window, chat::gui::connect);
    EXPECT_LT(std::chrono::steady_clock::now() - before, 100ms);
    boost::asio::ip::tcp::socket socket(silent_context);
    acceptor.accept(socket);
    SendMessageW(window.handle(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(1000, 720));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 150ms);
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"timed out") != std::wstring::npos;
    }));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::connect)));
}
/// @brief A missing heartbeat reply appears in status while reconnect controls remain usable.
TEST(GuiHeartbeatTest, MissingPongShowsFailureAndKeepsWindowResponsive) {
    chat::test::Peer fake;
    boost::asio::ip::tcp::acceptor acceptor(fake.context,
                                            {boost::asio::ip::address_v4::loopback(), 0});
    chat::client::WorkerLimits limits;
    limits.heartbeat_interval = 20ms;
    limits.poll_timeout = 500ms;
    limits.operation_timeout = 200ms;
    chat::gui::Window window(false, limits);
    ASSERT_NE(window.create(GetModuleHandleW(nullptr), false), nullptr);
    input(window, chat::gui::port, std::to_wstring(acceptor.local_endpoint().port()));
    input(window, chat::gui::username, L"alice");
    click(window, chat::gui::connect);
    acceptor.accept(fake.socket);
    const auto login = fake.receive(1);
    ASSERT_EQ(login.messages.size(), 1);
    ASSERT_TRUE(login.messages[0].has_login_request());
    auto reply = login.messages[0];
    reply.mutable_login_response()->set_accepted(true);
    reply.mutable_login_response()->set_username("alice");
    fake.send(chat::test::encode(reply));
    ASSERT_TRUE(until(window, [&] { return window.model().username() == "alice"; }));
    const auto ping = fake.receive(1);
    ASSERT_EQ(ping.messages.size(), 1);
    ASSERT_TRUE(ping.messages[0].has_ping());
    const auto before = std::chrono::steady_clock::now();
    SendMessageW(window.handle(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(1000, 720));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 100ms);
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"Heartbeat failed:") != std::wstring::npos;
    }));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::connect)));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    EXPECT_TRUE(window.model().users().empty());
}
/// @brief Hidden controls preserve the selected room, draft, and transcript during automatic
/// recovery.
TEST_F(GuiTest, AutomaticRecoveryPreservesDraftAndCatchesUpWithoutDuplicateLines) {
    chat::client::ChatClient bob;
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    const auto group = bob.create_group("Study").resource_id;
    chat::test::RecoveryProxy proxy(server.port());
    chat::client::WorkerLimits limits;
    limits.operation_timeout = 400ms;
    limits.poll_timeout = 5ms;
    limits.heartbeat_interval = 100ms;
    limits.reconnect_delay = 40ms;
    limits.reconnect_max_delay = 40ms;
    chat::gui::Window window(false, limits);
    ASSERT_NE(window.create(GetModuleHandleW(nullptr), false), nullptr);
    input(window, chat::gui::port, std::to_wstring(proxy.port()));
    input(window, chat::gui::username, L"alice");
    click(window, chat::gui::connect);
    ASSERT_TRUE(until(window, [&] { return window.model().groups().size() == 1; }));
    select(window, chat::gui::groups, 0);
    click(window, chat::gui::join_group);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->joined; }));
    ASSERT_TRUE(bob.send_group(group, "before interruption").accepted);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->lines.size() == 1; }));
    const auto key = window.model().current()->key;
    input(window, chat::gui::message, L"preserve unsent draft");
    proxy.interrupt();
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"Reconnecting") != std::wstring::npos;
    }));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::disconnect)));
    ASSERT_TRUE(bob.send_group(group, "while offline").accepted);
    proxy.paused = false;
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"Reconnected as alice") !=
               std::wstring::npos;
    }));
    EXPECT_EQ(window.model().current()->key, key);
    EXPECT_EQ(window.model().current()->lines.size(), 2);
    EXPECT_EQ(caption(window, chat::gui::message), L"preserve unsent draft");
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
    EXPECT_EQ(caption(window, chat::gui::transcript),
              L"bob: before interruption\r\nbob: while offline\r\n");
}
/// @brief Dedicated TLS listener for actual hidden-control secure login and failure tests.
class GuiTlsTest : public testing::Test {
  protected:
    GuiTlsTest()
        : server(context, {boost::asio::ip::address_v4::loopback(), 0}, ":memory:", {},
                 certificates.server()) {
        server.start();
        network = std::thread([this] { context.run(); });
    }
    ~GuiTlsTest() override {
        server.stop();
        guard.reset();
        network.join();
    }
    void connect(chat::gui::Window& window) {
        ASSERT_NE(window.create(GetModuleHandleW(nullptr), false), nullptr);
        input(window, chat::gui::port, std::to_wstring(server.port()));
        input(window, chat::gui::username, L"alice");
        click(window, chat::gui::connect);
    }
    chat::test::TlsCertificates certificates;
    boost::asio::io_context context;
    chat::server::PingServer server;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{
        context.get_executor()};
    std::thread network;
};
/// @brief The native room adapter carries the configured CA/name into worker login and encrypted
/// sends.
TEST_F(GuiTlsTest, HiddenControlsConnectAndSendOverVerifiedTls) {
    chat::client::ChatClient bob(certificates.client());
    bob.connect("127.0.0.1", server.port());
    ASSERT_TRUE(bob.login("bob").accepted);
    chat::gui::Window window(false, {}, certificates.client("localhost"));
    connect(window);
    ASSERT_TRUE(until(window, [&] { return window.model().users().size() == 2; }));
    select(window, chat::gui::users, 1);
    input(window, chat::gui::message, L"encrypted GUI text");
    click(window, chat::gui::send);
    ASSERT_TRUE(until(window, [&] { return window.model().current()->lines.size() == 1; }));
    EXPECT_EQ(caption(window, chat::gui::transcript), L"alice: encrypted GUI text\r\n");
    bool delivered = false;
    ASSERT_TRUE(until(window, [&] {
        if (auto event = bob.next_event(10ms)) {
            if (const auto* message = std::get_if<chat::client::DirectDelivery>(&*event)) {
                delivered = message->text == "encrypted GUI text";
            }
        }
        return delivered;
    }));
}
/// @brief A wrong certificate identity is displayed as terminal failure while window controls
/// respond.
TEST_F(GuiTlsTest, CertificateFailureShowsStatusWithoutConnectingOrDowngrading) {
    chat::gui::Window window(false, {}, certificates.client("wrong.example"));
    connect(window);
    const auto before = std::chrono::steady_clock::now();
    SendMessageW(window.handle(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(1000, 720));
    EXPECT_LT(std::chrono::steady_clock::now() - before, 100ms);
    ASSERT_TRUE(until(window, [&] {
        return caption(window, chat::gui::status).find(L"TLS") != std::wstring::npos;
    }));
    EXPECT_TRUE(window.model().users().empty());
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::connect)));
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::send)));
}
/// @brief Native history button, AI summarize/sentiment controls, and loopback MCP agent control
/// work over verified TLS.
TEST_F(GuiTlsTest, HistoryAiAndLoopbackAgentControlsWorkOverVerifiedTls) {
    chat::client::ChatClient creator(certificates.client());
    creator.connect("127.0.0.1", server.port());
    ASSERT_TRUE(creator.login("bob").accepted);
    const auto created = creator.create_group("AI Room");
    ASSERT_TRUE(created.accepted);
    ASSERT_TRUE(
        creator.send_group(created.resource_id, "TLS 1.3 socket framing is great and fast!")
            .accepted);

    chat::gui::Window window(false, {}, certificates.client("localhost"), std::uint16_t{0});
    ASSERT_GT(window.agent_port(), 0U);
    connect(window);
    ASSERT_TRUE(until(window, [&] {
        return window.model().users().size() == 2 && window.model().groups().size() == 1;
    }));
    select(window, chat::gui::groups, 0);
    EXPECT_FALSE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::load_history)));
    click(window, chat::gui::join_group);
    ASSERT_TRUE(until(window, [&] {
        return window.model().current() && window.model().current()->joined;
    }));
    EXPECT_TRUE(IsWindowEnabled(GetDlgItem(window.handle(), chat::gui::load_history)));
    click(window, chat::gui::load_history);
    ASSERT_TRUE(until(window, [&] {
        return window.model().current()->lines.size() == 1 &&
               caption(window, chat::gui::transcript).find(L"TLS 1.3 socket framing") !=
                   std::wstring::npos;
    }));

    click(window, chat::gui::ai_summarize);
    EXPECT_NE(caption(window, chat::gui::ai_output).find(L"AI Summary"), std::wstring::npos);

    click(window, chat::gui::ai_actions);
    EXPECT_NE(caption(window, chat::gui::ai_output).find(L"AI Insights"), std::wstring::npos);

    click(window, chat::gui::ai_smart_reply);
    EXPECT_FALSE(caption(window, chat::gui::message).empty());

    input(window, chat::gui::message, L"pls check the build thx");
    click(window, chat::gui::ai_polish);
    EXPECT_NE(caption(window, chat::gui::message).find(L"Please check"), std::wstring::npos);

    input(window, chat::gui::message, L"@ai who is online?");
    click(window, chat::gui::send);
    EXPECT_NE(caption(window, chat::gui::ai_output).find(L"Connected users"), std::wstring::npos);

    input(window, chat::gui::ai_prompt, L"Everything is working wonderfully!");
    click(window, chat::gui::ai_sentiment);
    EXPECT_NE(caption(window, chat::gui::ai_output).find(L"Positive"), std::wstring::npos);

    boost::asio::io_context mcp_io;
    boost::asio::ip::tcp::socket mcp_socket(mcp_io);
    mcp_socket.connect({boost::asio::ip::address_v4::loopback(), window.agent_port()});
    const std::string request =
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"run_agent_prompt","arguments":{"prompt":"dm bob Hello from GUI MCP connector"}}})"
        "\n";
    boost::asio::write(mcp_socket, boost::asio::buffer(request));
    bool bob_received = false;
    ASSERT_TRUE(until(window, [&] {
        if (auto event = creator.next_event(10ms)) {
            if (const auto* dm = std::get_if<chat::client::DirectDelivery>(&*event)) {
                bob_received = dm->text == "Hello from GUI MCP connector";
            }
        }
        return bob_received;
    }));
    mcp_socket.close();
}
} // namespace
