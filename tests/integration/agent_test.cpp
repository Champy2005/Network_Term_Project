/** @file
 * @brief Verify on-demand worker history, AI summarization/sentiment, and MCP/loopback external
 * agent control over verified TLS 1.3 sockets.
 */
#include "chat/server/ping_server.hpp"
#include "chat_console.hpp"
#include "tls_certificates.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <gtest/gtest.h>
#include <sstream>
#include <thread>

namespace {
using namespace std::chrono_literals;
template <class Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    do {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < end);
    return false;
}

class AgentIntegrationTest : public testing::Test {
  protected:
    AgentIntegrationTest()
        : server(context, {boost::asio::ip::address_v4::loopback(), 0}, ":memory:", {},
                 certificates.server()),
          network([this] { context.run(); }) {
        server.start();
    }
    ~AgentIntegrationTest() override {
        server.stop();
        guard.reset();
        network.join();
    }
    void start(chat::cli::ChatConsole& console, const std::string& name) {
        ASSERT_TRUE(console.start("127.0.0.1", server.port(), name));
        ASSERT_TRUE(until([&] {
            console.poll();
            return console.ready();
        }));
    }
    void stop(chat::cli::ChatConsole& console) {
        console.request_exit();
        ASSERT_TRUE(until([&] {
            console.poll();
            return console.finished();
        }));
    }
    chat::test::TlsCertificates certificates;
    boost::asio::io_context context;
    chat::server::PingServer server;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard{
        context.get_executor()};
    std::thread network;
};

/// @brief On-demand CLI history and AI summarization honor server membership authorization.
TEST_F(AgentIntegrationTest, CliHistorySummarizeAndSentimentRespectServerAuthorization) {
    std::ostringstream a, b, c;
    chat::cli::ChatConsole alice(a, {}, certificates.client()), bob(b, {}, certificates.client()),
        charlie(c, {}, certificates.client());
    start(alice, "alice");
    start(bob, "bob");
    start(charlie, "charlie");

    alice.execute({"/create AI Study"});
    ASSERT_TRUE(until([&] {
        alice.poll();
        return a.str().find("OK 1 create room=\"") != std::string::npos;
    }));
    const auto out_a = a.str();
    const auto pos = out_a.find("OK 1 create room=\"") + std::string_view("OK 1 create room=\"").size();
    const auto group_id = out_a.substr(pos, out_a.find('"', pos) - pos);

    alice.execute({"/group " + group_id + " TLS 1.3 socket framing works great and fast!"});
    alice.execute({"/group " + group_id + " SQLite WAL history passed all reliability tests!"});
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return a.str().find("OK 3 group") != std::string::npos &&
               b.str().find(group_id) != std::string::npos;
    }));

    // Nonmember Charlie is denied history by the server.
    charlie.execute({"/history " + group_id});
    ASSERT_TRUE(until([&] {
        charlie.poll();
        return c.str().find("REJECTED 1 history code=9") != std::string::npos;
    }));
    EXPECT_EQ(c.str().find("TLS 1.3 socket framing"), std::string::npos);

    // Bob explicitly joins, loads stored history from before his join, and summarizes the room.
    bob.execute({"/join " + group_id});
    ASSERT_TRUE(until([&] {
        bob.poll();
        return b.str().find("OK 1 join") != std::string::npos;
    }));
    bob.execute({"/history " + group_id});
    ASSERT_TRUE(until([&] {
        bob.poll();
        return b.str().find("OK 2 history") != std::string::npos &&
               b.str().find("HISTORY_MESSAGE \"" + group_id + "\" seq=2") != std::string::npos;
    }));
    bob.execute({"/summarize " + group_id});
    EXPECT_NE(b.str().find("AI_SUMMARY"), std::string::npos);
    EXPECT_NE(b.str().find("sentiment=Positive"), std::string::npos);

    bob.execute({"/sentiment Everything is working wonderfully!"});
    EXPECT_NE(b.str().find("AI_SENTIMENT"), std::string::npos);

    bob.execute({"/ai status"});
    EXPECT_NE(b.str().find("AI_AGENT \"Status: connected as bob"), std::string::npos);

    stop(alice);
    stop(bob);
    stop(charlie);
}

/// @brief An external TCP MCP client on 127.0.0.1 controls a live session while preserving privacy.
TEST_F(AgentIntegrationTest, LoopbackAgentServerControlsLiveSessionAndEnforcesPrivacy) {
    std::ostringstream a, b, c;
    chat::cli::ChatConsole alice(a, {}, certificates.client(), 5s, std::uint16_t{0}),
        bob(b, {}, certificates.client()), charlie(c, {}, certificates.client());
    ASSERT_GT(alice.agent_port(), 0U);
    start(alice, "alice");
    start(bob, "bob");
    start(charlie, "charlie");
    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return a.str().find("\"charlie\"") != std::string::npos;
    }));

    boost::asio::io_context client_io;
    boost::asio::ip::tcp::socket mcp_socket(client_io);
    mcp_socket.connect({boost::asio::ip::address_v4::loopback(), alice.agent_port()});

    const auto call_mcp = [&](const std::string& json_line) -> std::string {
        const std::string payload = json_line + "\n";
        boost::asio::write(mcp_socket, boost::asio::buffer(payload));
        std::string reply;
        mcp_socket.non_blocking(true);
        std::array<char, 4096> buf{};
        EXPECT_TRUE(until([&] {
            alice.poll();
            bob.poll();
            charlie.poll();
            boost::system::error_code ec;
            const auto n = mcp_socket.read_some(boost::asio::buffer(buf), ec);
            if (n > 0) {
                reply.append(buf.data(), n);
            }
            return reply.find('\n') != std::string::npos;
        }));
        return reply;
    };

    const auto init_reply =
        call_mcp(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
    EXPECT_NE(init_reply.find("\"protocolVersion\":\"2024-11-05\""), std::string::npos);

    const auto dm_reply = call_mcp(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"send_direct_message","arguments":{"recipient":"bob","text":"Hello Bob, MCP agent is working great!"}}})");
    EXPECT_NE(dm_reply.find("\"isError\":false"), std::string::npos);

    ASSERT_TRUE(until([&] {
        alice.poll();
        bob.poll();
        charlie.poll();
        return b.str().find("MCP agent is working great!") != std::string::npos &&
               a.str().find("MCP agent is working great!") != std::string::npos;
    }));

    const auto sum_reply = call_mcp(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"summarize_room","arguments":{"room":"bob"}}})");
    EXPECT_NE(sum_reply.find("\"isError\":false"), std::string::npos);
    EXPECT_NE(sum_reply.find("MCP agent is working great!"), std::string::npos);
    EXPECT_EQ(c.str().find("MCP agent is working great!"), std::string::npos);

    mcp_socket.close();
    stop(alice);
    stop(bob);
    stop(charlie);
}

/// @brief MCP stdio mode emits pure JSON-RPC 2.0 lines without human status output.
TEST_F(AgentIntegrationTest, McpStdioModeEmitsPureJsonRpcWithoutHumanStatusNoise) {
    std::ostringstream mcp_out, bob_out;
    chat::cli::ChatConsole alice_mcp(mcp_out, {}, certificates.client(), 5s, std::nullopt, true),
        bob(bob_out, {}, certificates.client());
    start(alice_mcp, "alice");
    start(bob, "bob");
    ASSERT_TRUE(until([&] {
        alice_mcp.poll();
        bob.poll();
        return bob_out.str().find("\"alice\"") != std::string::npos;
    }));
    // No human CONNECTED or USERS lines may pollute stdio in MCP mode.
    EXPECT_TRUE(mcp_out.str().empty());

    alice_mcp.execute_mcp({R"({"jsonrpc":"2.0","id":10,"method":"tools/list"})"});
    alice_mcp.execute_mcp(
        {R"({"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"run_agent_prompt","arguments":{"prompt":"dm bob Pure MCP stdio message"}}})"});
    ASSERT_TRUE(until([&] {
        alice_mcp.poll();
        bob.poll();
        return bob_out.str().find("Pure MCP stdio message") != std::string::npos;
    }));
    std::istringstream lines(mcp_out.str());
    std::string line;
    int count = 0;
    while (std::getline(lines, line)) {
        ASSERT_FALSE(line.empty());
        EXPECT_EQ(line.front(), '{');
        EXPECT_NE(line.find("\"jsonrpc\":\"2.0\""), std::string::npos);
        ++count;
    }
    EXPECT_EQ(count, 2);
    stop(alice_mcp);
    stop(bob);
}
} // namespace
