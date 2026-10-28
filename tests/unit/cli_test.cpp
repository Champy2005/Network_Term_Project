/** @file
 * @brief Verify console syntax, byte budgets, incremental input, and safe remote text display.
 */
#include "chat_console.hpp"
#include <gtest/gtest.h>
using chat::cli::Action;
using chat::cli::parse_command;

/// @brief Commands retain spaced names and message text without shell interpolation.
TEST(CliSyntaxTest, CoreFlowsAndLiteralText) {
    EXPECT_EQ(parse_command(" /users ").action, Action::users);
    EXPECT_EQ(parse_command("/groups").action, Action::groups);
    EXPECT_EQ(parse_command("/members group-1").target, "group-1");
    EXPECT_EQ(parse_command("/join group-1").action, Action::join);
    EXPECT_EQ(parse_command("/create Study group").target, "Study group");
    const auto dm = parse_command("/dm bob hello $HOME; `literal`  ");
    EXPECT_EQ(dm.action, Action::direct);
    EXPECT_EQ(dm.target, "bob");
    EXPECT_EQ(dm.text, "hello $HOME; `literal`  ");
    EXPECT_EQ(parse_command("/group group-1 hello").action, Action::group);
    EXPECT_EQ(parse_command("/history group-1").action, Action::history);
    EXPECT_EQ(parse_command("/summarize").action, Action::summarize);
    EXPECT_EQ(parse_command("/summarize group-1").target, "group-1");
    EXPECT_EQ(parse_command("/sentiment great job!").action, Action::sentiment);
    EXPECT_EQ(parse_command("/ai summarize this room").action, Action::ai);
    EXPECT_EQ(parse_command("/help").action, Action::help);
    EXPECT_EQ(parse_command("/quit").action, Action::quit);
    EXPECT_EQ(parse_command(" \t").action, Action::empty);
}
/// @brief Invalid verbs, missing values, and extra local-command arguments never queue sends.
TEST(CliSyntaxTest, InvalidCommandsAreRejected) {
    for (const auto* line :
         {"hello", "/unknown", "/dm", "/dm bob", "/group group-1", "/join", "/history",
          "/sentiment", "/ai", "/summarize a b", "/create", "/users extra", "/quit extra",
          "/members a b"}) {
        EXPECT_THROW(static_cast<void>(parse_command(line)), std::invalid_argument) << line;
    }
}
/// @brief UTF-8 budgets count bytes and admit the exact server text/name boundaries.
TEST(CliSyntaxTest, ExactByteBounds) {
    EXPECT_EQ(parse_command("/dm bob " + std::string(4096, 'x')).text.size(), 4096);
    EXPECT_THROW(static_cast<void>(parse_command("/dm bob " + std::string(4097, 'x'))),
                 std::invalid_argument);
    EXPECT_EQ(parse_command("/create " + std::string(64, 'x')).target.size(), 64);
    EXPECT_THROW(static_cast<void>(parse_command("/create " + std::string(65, 'x'))),
                 std::invalid_argument);
    EXPECT_THROW(static_cast<void>(parse_command("/join " + std::string(65, 'x'))),
                 std::invalid_argument);
}
/// @brief Embedded NUL/escape/newline controls cannot forge commands or terminal status.
TEST(CliSyntaxTest, ControlCharactersAreRejected) {
    for (const char control : {'\0', '\x1b', '\r', '\n', '\x7f'}) {
        EXPECT_THROW(static_cast<void>(parse_command(std::string("/dm bob ") + control + "text")),
                     std::invalid_argument);
    }
}
/// @brief Remote text cannot inject ANSI sequences, extra lines, or quoted status delimiters.
TEST(CliDisplayTest, RemoteControlsAndQuotesAreEscaped) {
    EXPECT_EQ(chat::cli::escape_text(std::string("\x1b[31m\n\r\t\0", 9)),
              "\"\\x1b[31m\\x0a\\x0d\\x09\\x00\"");
    EXPECT_EQ(chat::cli::escape_text("\"\\"), "\"\\\"\\\\\"");
    EXPECT_EQ(chat::cli::escape_text("ไทย"), "\"ไทย\"");
}
/// @brief Split/coalesced lines and split CRLF delimiters preserve one command per line.
TEST(CliInputTest, FragmentedAndCoalescedLines) {
    chat::cli::LineInput input;
    EXPECT_TRUE(input.feed("/dm b").empty());
    const auto lines = input.feed("ob text\r");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0].text, "/dm bob text");
    const auto more = input.feed("\n/users\n/groups\r\n");
    ASSERT_EQ(more.size(), 2);
    EXPECT_EQ(more[0].text, "/users");
    EXPECT_EQ(more[1].text, "/groups");
    EXPECT_TRUE(input.finish().empty());
}
/// @brief Backspace removes a complete multibyte codepoint, including fragmented input.
TEST(CliInputTest, Utf8BackspacePreservesEarlierText) {
    chat::cli::LineInput input;
    EXPECT_TRUE(input.feed("/create ไทย").empty());
    const auto lines = input.feed("\bX\n");
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0].text, "/create ไทX");
    EXPECT_EQ(input.feed("\b\b/users\n")[0].text, "/users");
}
/// @brief Overlong lines discard their suffix through the delimiter and recover for later commands.
TEST(CliInputTest, OversizedLineCannotBecomeAnotherCommand) {
    chat::cli::LineInput input;
    for (int count = 0; count < 9; ++count) {
        EXPECT_TRUE(input.feed(std::string(1024, 'x')).empty());
    }
    const auto lines = input.feed("/dm bob unintended\n/users\n");
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines[0].oversized);
    EXPECT_TRUE(lines[0].text.empty());
    EXPECT_EQ(lines[1].text, "/users");
    EXPECT_FALSE(lines[1].oversized);
}
/// @brief EOF emits a final line once; caller chunk budgets prevent arbitrary retained backlogs.
TEST(CliInputTest, FinalLineAndChunkBounds) {
    chat::cli::LineInput input;
    EXPECT_TRUE(input.feed("/quit").empty());
    ASSERT_EQ(input.finish()[0].text, "/quit");
    EXPECT_TRUE(input.finish().empty());
    EXPECT_THROW(static_cast<void>(input.feed(std::string(1025, 'x'))), std::invalid_argument);
}
