/** @file
 * @brief Verify TextRank summarization, VADER sentiment analysis, MCP JSON-RPC 2.0 dispatch, and
 * natural-language agent tool routing.
 */
#include "chat/ai/agent_tools.hpp"
#include "chat/ai/ai_engine.hpp"
#include <gtest/gtest.h>
#include <tuple>
#include <vector>

namespace {
using chat::ai::AgentController;
using chat::ai::AiEngine;
using chat::ai::LlmEndpointOptions;
using chat::ai::McpDispatcher;
using chat::client::CommandKind;
using chat::client::DirectDelivery;
using chat::client::GroupSnapshot;
using chat::client::RoomModel;
using chat::client::UserSnapshot;

/// @brief VADER-style sentiment distinguishes polarity, negation inversion, and booster emphasis.
TEST(AiEngineTest, VaderSentimentClassifiesPositiveNegativeNeutralAndNegation) {
    const auto positive = AiEngine::analyze_sentiment("The TLS socket build is great and working!");
    EXPECT_EQ(positive.label, "Positive");
    EXPECT_GT(positive.compound, 0.3);
    EXPECT_GT(positive.positive, positive.negative);

    const auto boosted =
        AiEngine::analyze_sentiment("The TLS socket build is EXTREMELY GREAT and WORKING!!!");
    EXPECT_GT(boosted.compound, positive.compound);

    const auto negative =
        AiEngine::analyze_sentiment("Connection crashed with a terrible timeout failure.");
    EXPECT_EQ(negative.label, "Negative");
    EXPECT_LT(negative.compound, -0.3);
    EXPECT_GT(negative.negative, negative.positive);

    const auto negated = AiEngine::analyze_sentiment("The build is not good at all.");
    EXPECT_EQ(negated.label, "Negative");
    EXPECT_LT(negated.compound, -0.05);

    const auto neutral = AiEngine::analyze_sentiment("Port 5555 uses TCP frames.");
    EXPECT_EQ(neutral.label, "Neutral");
    EXPECT_NEAR(neutral.compound, 0.0, 0.049);
    EXPECT_EQ(AiEngine::analyze_sentiment("").label, "Neutral");
}

/// @brief TextRank extracts central transcript lines in chronological room order with citation.
TEST(AiEngineTest, TextRankSummarizerExtractsCentralMessagesInChronologicalOrder) {
    RoomModel model;
    model.reset("alice");
    model.apply(UserSnapshot{{"alice", "bob"}});
    model.open_direct("bob");
    const auto empty_summary =
        AiEngine::summarize_lines(model.current()->title, model.current()->lines, 2);
    EXPECT_EQ(empty_summary.message_count, 0U);
    EXPECT_TRUE(empty_summary.key_points.empty());
    EXPECT_NE(empty_summary.model_id.find("TextRank"), std::string::npos);

    model.apply(DirectDelivery{"alice", "bob", "direct-1",
                               "We need to finalize the TLS 1.3 socket framing benchmark today.",
                               "m1", 1, 1});
    model.apply(DirectDelivery{"bob", "alice", "direct-1", "Random unrelated coffee break.", "m2",
                               2, 2});
    model.apply(DirectDelivery{
        "alice", "bob", "direct-1",
        "The TLS 1.3 socket framing tests passed in debug and release builds.", "m3", 3, 3});
    model.apply(DirectDelivery{
        "bob", "alice", "direct-1",
        "Great, let us include the TLS 1.3 socket framing benchmark in the demo slides!", "m4", 4,
        4});

    const auto summary =
        AiEngine::summarize_lines(model.current()->title, model.current()->lines, 2);
    EXPECT_EQ(summary.message_count, 4U);
    ASSERT_EQ(summary.key_points.size(), 2U);
    EXPECT_EQ(summary.overall_sentiment.label, "Positive");
    for (const auto& point : summary.key_points) {
        EXPECT_NE(point.find("TLS 1.3 socket framing"), std::string::npos);
    }
    EXPECT_NE(summary.formatted.find("AI Summary (Direct: bob, 4 msgs"), std::string::npos);
}

/// @brief MCP JSON-RPC 2.0 dispatcher validates framing, initialization, tools/list, and errors.
TEST(McpProtocolTest, McpDispatcherHandlesInitializePingToolsListAndMalformedJson) {
    RoomModel model;
    model.reset("alice");
    AgentController controller(
        model, [] { return true; },
        [](CommandKind, const std::string&, const std::string&) { return std::uint64_t{1}; });

    EXPECT_FALSE(McpDispatcher::handle_line("   ", controller).has_value());
    EXPECT_FALSE(McpDispatcher::handle_line(
                     R"({"jsonrpc":"2.0","method":"notifications/initialized"})", controller)
                     .has_value());

    const auto init = McpDispatcher::handle_line(
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})", controller);
    ASSERT_TRUE(init.has_value());
    EXPECT_NE(init->find("\"protocolVersion\":\"2024-11-05\""), std::string::npos);
    EXPECT_NE(init->find("\"socket-chat-mcp\""), std::string::npos);

    const auto ping =
        McpDispatcher::handle_line(R"({"jsonrpc":"2.0","id":"p-1","method":"ping"})", controller);
    ASSERT_TRUE(ping.has_value());
    EXPECT_NE(ping->find("\"id\":\"p-1\""), std::string::npos);

    const auto tools =
        McpDispatcher::handle_line(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})", controller);
    ASSERT_TRUE(tools.has_value());
    for (const auto* expected :
         {"get_status", "list_users", "list_groups", "list_rooms", "open_room",
          "get_room_messages", "load_room_history", "create_group", "join_group",
          "send_direct_message", "send_group_message", "summarize_room", "extract_action_items",
          "suggest_smart_reply", "polish_draft", "analyze_sentiment", "run_agent_prompt"}) {
        EXPECT_NE(tools->find(expected), std::string::npos) << expected;
    }

    const auto parse_err = McpDispatcher::handle_line("not-json", controller);
    ASSERT_TRUE(parse_err.has_value());
    EXPECT_NE(parse_err->find("-32700"), std::string::npos);

    const auto invalid_req = McpDispatcher::handle_line(R"({"id":3})", controller);
    ASSERT_TRUE(invalid_req.has_value());
    EXPECT_NE(invalid_req->find("-32600"), std::string::npos);

    const auto unknown_method = McpDispatcher::handle_line(
        R"({"jsonrpc":"2.0","id":4,"method":"unknown/method"})", controller);
    ASSERT_TRUE(unknown_method.has_value());
    EXPECT_NE(unknown_method->find("-32601"), std::string::npos);

    const auto oversized =
        McpDispatcher::handle_line(std::string(McpDispatcher::kMaxLineBytes + 1, 'x'), controller);
    ASSERT_TRUE(oversized.has_value());
    EXPECT_NE(oversized->find("-32600"), std::string::npos);
}

/// @brief AgentController routes MCP tools and natural-language prompts through the room model and
/// worker callback.
TEST(AgentControllerTest, AgentControllerExecutesToolsAndNaturalLanguagePrompts) {
    RoomModel model;
    model.reset("alice");
    model.apply(UserSnapshot{{"alice", "bob"}});
    model.apply(GroupSnapshot{{{"group-1", "Networks", {"alice"}}}});
    model.apply(DirectDelivery{"bob", "alice", "direct-1",
                               "Socket programming project is ready and working great!", "m1", 1,
                               1});
    bool connected = true;
    std::vector<std::tuple<CommandKind, std::string, std::string>> submitted;
    std::string last_output;
    AgentController controller(
        model, [&] { return connected; },
        [&](CommandKind kind, const std::string& target,
            const std::string& text) -> std::optional<std::uint64_t> {
            if (target.empty()) {
                return std::nullopt;
            }
            submitted.emplace_back(kind, target, text);
            return static_cast<std::uint64_t>(submitted.size());
        },
        [&](const std::string& out) { last_output = out; });

    EXPECT_TRUE(controller.execute_prompt("open bob").ok);
    ASSERT_NE(model.current(), nullptr);
    EXPECT_EQ(model.current()->target, "bob");

    const auto sum = controller.execute_prompt("summarize");
    EXPECT_TRUE(sum.ok);
    EXPECT_NE(sum.message.find("Socket programming project"), std::string::npos);
    EXPECT_EQ(last_output, sum.message);

    const auto sent = controller.execute_prompt("sentiment This feature is awesome!");
    EXPECT_TRUE(sent.ok);
    EXPECT_NE(sent.message.find("Positive"), std::string::npos);

    EXPECT_TRUE(controller.execute_prompt("create group AI Lab").ok);
    EXPECT_TRUE(controller.execute_prompt("join group group-1").ok);
    EXPECT_TRUE(controller.execute_prompt("dm bob hello from agent").ok);
    EXPECT_TRUE(controller.execute_prompt("group group-1 hello group").ok);
    EXPECT_TRUE(controller.execute_prompt("load history direct-1").ok);
    EXPECT_TRUE(controller.execute_prompt("please tell Bob that I am on my way").ok);
    EXPECT_TRUE(controller.execute_prompt("send \"Meeting at 5pm\" to Networks").ok);
    EXPECT_TRUE(controller.execute_prompt("who is online?").ok);
    EXPECT_TRUE(controller.execute_prompt("who is in Networks?").ok);
    EXPECT_TRUE(controller.execute_prompt("what did Bob say?").ok);
    EXPECT_TRUE(controller.execute_prompt("search for Socket").ok);
    EXPECT_TRUE(controller.execute_prompt("how is the mood?").ok);
    EXPECT_TRUE(controller.execute_prompt("tldr").ok);
    EXPECT_TRUE(controller.execute_prompt("smart reply").ok);
    EXPECT_TRUE(controller.execute_prompt("action items").ok);
    const auto polished = controller.execute_prompt("polish: hey u free tmrw pls lmk");
    EXPECT_TRUE(polished.ok);
    EXPECT_NE(polished.message.find("Hey you free"), std::string::npos);

    // Multi-step autonomous tool chaining
    const auto chained =
        controller.execute_prompt("create group DevTeam and tell bob Join DevTeam!");
    EXPECT_TRUE(chained.ok);
    EXPECT_NE(chained.message.find("Step 1:"), std::string::npos);
    EXPECT_NE(chained.message.find("Step 2:"), std::string::npos);

    ASSERT_EQ(submitted.size(), 9U);
    EXPECT_EQ(std::get<0>(submitted[0]), CommandKind::create_group);
    EXPECT_EQ(std::get<1>(submitted[0]), "AI Lab");
    EXPECT_EQ(std::get<0>(submitted[1]), CommandKind::join_group);
    EXPECT_EQ(std::get<0>(submitted[2]), CommandKind::direct);
    EXPECT_EQ(std::get<2>(submitted[2]), "hello from agent");
    EXPECT_EQ(std::get<0>(submitted[3]), CommandKind::group);
    EXPECT_EQ(std::get<0>(submitted[4]), CommandKind::history);
    EXPECT_EQ(std::get<1>(submitted[4]), "direct-1");
    EXPECT_EQ(std::get<0>(submitted[7]), CommandKind::create_group);
    EXPECT_EQ(std::get<1>(submitted[7]), "DevTeam");
    EXPECT_EQ(std::get<0>(submitted[8]), CommandKind::direct);
    EXPECT_EQ(std::get<1>(submitted[8]), "bob");
    EXPECT_EQ(std::get<2>(submitted[8]), "Join DevTeam!");

    connected = false;
    EXPECT_FALSE(controller.execute_prompt("dm bob offline attempt").ok);
}

/// @brief Action items, decisions, open questions, and draft polishing work deterministically.
TEST(AiEngineTest, ActionItemsDecisionsAndPolishDraftWorkDeterministically) {
    RoomModel model;
    model.reset("alice");
    model.apply(UserSnapshot{{"alice", "bob"}});
    model.open_direct("bob");
    model.apply(DirectDelivery{"bob", "alice", "direct-1",
                               "We need to finish the slide deck by tomorrow.", "m1", 1, 1});
    model.apply(DirectDelivery{"alice", "bob", "direct-1",
                               "Agreed, we will use TLS 1.3 for all sockets.", "m2", 2, 2});
    model.apply(DirectDelivery{"bob", "alice", "direct-1",
                               "What time should we run the rehearsal?", "m3", 3, 3});

    const auto insights =
        AiEngine::extract_action_items(model.current()->title, model.current()->lines);
    EXPECT_FALSE(insights.action_items.empty());
    EXPECT_FALSE(insights.decisions.empty());
    EXPECT_FALSE(insights.open_questions.empty());
    EXPECT_NE(insights.formatted.find("Actions("), std::string::npos);

    const std::string polished = AiEngine::polish_draft("can u pls check the build thx");
    EXPECT_EQ(polished, "Can you please check the build thanks?");

    const std::string reply =
        AiEngine::suggest_smart_reply("alice", model.current()->title, model.current()->lines);
    EXPECT_NE(reply.find("bob"), std::string::npos);
}

/// @brief Disabled or unreachable optional LLM endpoints return nullopt cleanly within deadline.
TEST(AiEngineTest, LlmEndpointReturnsNulloptWhenDisabledOrUnreachable) {
    LlmEndpointOptions options;
    EXPECT_FALSE(AiEngine::query_llm_endpoint(options, "sys", "user").has_value());
    options.enabled = true;
    options.tls = false;
    options.host = "127.0.0.1";
    options.port = 1;
    options.timeout = std::chrono::milliseconds(100);
    EXPECT_FALSE(AiEngine::query_llm_endpoint(options, "sys", "user").has_value());
}
} // namespace
