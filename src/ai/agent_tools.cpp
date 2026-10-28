/** @file
 * @brief Implement the MCP JSON-RPC 2.0 tool registry, natural-language intent router, and
 * non-blocking loopback agent server.
 */
#include "chat/ai/agent_tools.hpp"
#include <algorithm>
#include <array>
#include <boost/asio/ip/tcp.hpp>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace chat::ai {
namespace {
std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}
std::string to_lower_ascii(std::string_view input) {
    std::string out(input);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}
std::string unquote(std::string_view input) {
    std::string s = trim(input);
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        return trim(std::string_view(s.data() + 1, s.size() - 2));
    }
    return s;
}
std::string strip_polite_prefix(std::string_view input) {
    std::string current = trim(input);
    constexpr std::array<std::string_view, 12> kPrefixes = {
        "please ",       "can you ",      "could you ", "would you ",
        "hey ai ",       "heyai ",        "ai, ",       "ai: ",
        "ai ",           "i want to ",    "i'd like to ", "let's "};
    bool changed = true;
    while (changed) {
        changed = false;
        const std::string lower = to_lower_ascii(current);
        for (const auto prefix : kPrefixes) {
            if (lower.starts_with(prefix)) {
                current = trim(std::string_view(current.data() + prefix.size(),
                                                current.size() - prefix.size()));
                changed = true;
                break;
            }
        }
    }
    return current;
}
std::string strip_trailing_punct(std::string_view input) {
    std::string s = trim(input);
    while (!s.empty() && (s.back() == '?' || s.back() == '.' || s.back() == '!')) {
        s.pop_back();
    }
    return trim(s);
}
std::optional<std::string> resolve_username(const client::RoomModel& model,
                                            std::string_view candidate) {
    const std::string key = unquote(candidate);
    if (key.empty()) {
        return std::nullopt;
    }
    for (const auto& u : model.users()) {
        if (u == key) {
            return u;
        }
    }
    const std::string key_lower = to_lower_ascii(key);
    for (const auto& u : model.users()) {
        if (to_lower_ascii(u) == key_lower) {
            return u;
        }
    }
    return std::nullopt;
}
std::optional<client::GroupInfo> resolve_group(const client::RoomModel& model,
                                               std::string_view candidate) {
    std::string key = unquote(candidate);
    const std::string lower_raw = to_lower_ascii(key);
    if (lower_raw.starts_with("group ") && key.size() > 6) {
        key = unquote(std::string_view(key.data() + 6, key.size() - 6));
    }
    if (key.empty()) {
        return std::nullopt;
    }
    for (const auto& g : model.groups()) {
        if (g.group_id == key || g.name == key) {
            return g;
        }
    }
    const std::string key_lower = to_lower_ascii(key);
    for (const auto& g : model.groups()) {
        if (to_lower_ascii(g.group_id) == key_lower || to_lower_ascii(g.name) == key_lower) {
            return g;
        }
    }
    return std::nullopt;
}
std::string json_escape(std::string_view input) {
    constexpr std::string_view kHex = "0123456789abcdef";
    std::string out;
    out.reserve(input.size() + 8);
    for (const unsigned char ch : input) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20U) {
                out += "\\u00";
                out.push_back(kHex[ch >> 4]);
                out.push_back(kHex[ch & 0x0FU]);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}
std::optional<std::size_t> find_json_key_value(std::string_view json, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(pattern, pos)) != std::string_view::npos) {
        std::size_t after = pos + pattern.size();
        while (after < json.size() &&
               (json[after] == ' ' || json[after] == '\t' || json[after] == '\r' ||
                json[after] == '\n')) {
            ++after;
        }
        if (after < json.size() && json[after] == ':') {
            ++after;
            while (after < json.size() &&
                   (json[after] == ' ' || json[after] == '\t' || json[after] == '\r' ||
                    json[after] == '\n')) {
                ++after;
            }
            return after;
        }
        pos += pattern.size();
    }
    return std::nullopt;
}
std::optional<std::string> extract_json_string(std::string_view json, std::string_view key) {
    const auto start = find_json_key_value(json, key);
    if (!start || *start >= json.size() || json[*start] != '"') {
        return std::nullopt;
    }
    std::string out;
    for (std::size_t i = *start + 1; i < json.size(); ++i) {
        if (json[i] == '\\') {
            if (i + 1 >= json.size()) {
                return std::nullopt;
            }
            ++i;
            switch (json[i]) {
            case '"':
            case '\\':
            case '/':
                out.push_back(json[i]);
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            default:
                out.push_back(json[i]);
                break;
            }
        } else if (json[i] == '"') {
            return out;
        } else {
            out.push_back(json[i]);
        }
    }
    return std::nullopt;
}
std::optional<std::string> extract_json_raw(std::string_view json, std::string_view key) {
    const auto start = find_json_key_value(json, key);
    if (!start || *start >= json.size()) {
        return std::nullopt;
    }
    const std::size_t idx = *start;
    if (json[idx] == '"') {
        for (std::size_t i = idx + 1; i < json.size(); ++i) {
            if (json[i] == '\\') {
                ++i;
            } else if (json[i] == '"') {
                return std::string(json.substr(idx, i - idx + 1));
            }
        }
        return std::nullopt;
    }
    if (json[idx] == '{' || json[idx] == '[') {
        const char open = json[idx];
        const char close = open == '{' ? '}' : ']';
        int depth = 0;
        bool in_string = false;
        for (std::size_t i = idx; i < json.size(); ++i) {
            if (in_string) {
                if (json[i] == '\\') {
                    ++i;
                } else if (json[i] == '"') {
                    in_string = false;
                }
            } else {
                if (json[i] == '"') {
                    in_string = true;
                } else if (json[i] == open) {
                    ++depth;
                } else if (json[i] == close) {
                    --depth;
                    if (depth == 0) {
                        return std::string(json.substr(idx, i - idx + 1));
                    }
                }
            }
        }
        return std::nullopt;
    }
    std::size_t end = idx;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' &&
           json[end] != ' ' && json[end] != '\t' && json[end] != '\r' && json[end] != '\n') {
        ++end;
    }
    if (end == idx) {
        return std::nullopt;
    }
    return std::string(json.substr(idx, end - idx));
}
std::optional<std::size_t> extract_json_uint(std::string_view json, std::string_view key) {
    const auto raw = extract_json_raw(json, key);
    if (!raw || raw->empty()) {
        return std::nullopt;
    }
    std::size_t value = 0;
    const auto res = std::from_chars(raw->data(), raw->data() + raw->size(), value);
    if (res.ec != std::errc{} || res.ptr != raw->data() + raw->size()) {
        return std::nullopt;
    }
    return value;
}
} // namespace

AgentController::AgentController(client::RoomModel& model, ConnectedFn is_connected,
                                 SubmitFn submit, OutputFn on_output, LlmEndpointOptions llm)
    : model_(model), is_connected_(std::move(is_connected)), submit_(std::move(submit)),
      on_output_(std::move(on_output)), llm_(std::move(llm)) {}

void AgentController::notify(const std::string& text) const {
    if (on_output_) {
        on_output_(text);
    }
}

void AgentController::notify_draft(const std::string& text) {
    model_.draft(text);
    if (on_draft_) {
        on_draft_(text);
    }
}

const client::RoomView* AgentController::find_room(std::string_view selector) const {
    const std::string key = unquote(selector);
    if (key.empty() || to_lower_ascii(key) == "this room" ||
        to_lower_ascii(key) == "current room" || to_lower_ascii(key) == "room" ||
        to_lower_ascii(key) == "chat" || to_lower_ascii(key) == "this chat") {
        return model_.current();
    }
    for (const auto& room : model_.rooms()) {
        if (room.key == key || room.wire_id == key || room.target == key || room.title == key) {
            return &room;
        }
    }
    const std::string lower_key = to_lower_ascii(key);
    if (const auto grp = resolve_group(model_, key)) {
        for (const auto& room : model_.rooms()) {
            if (room.wire_id == grp->group_id || room.target == grp->group_id) {
                return &room;
            }
        }
    }
    for (const auto& room : model_.rooms()) {
        if (to_lower_ascii(room.key) == lower_key || to_lower_ascii(room.wire_id) == lower_key ||
            to_lower_ascii(room.target) == lower_key || to_lower_ascii(room.title) == lower_key ||
            to_lower_ascii(room.title).find(lower_key) != std::string::npos) {
            return &room;
        }
    }
    return nullptr;
}

AgentActionResult AgentController::summarize_room(std::string_view room_selector,
                                                  std::size_t max_sentences) {
    const auto* room = find_room(room_selector);
    if (room_selector.empty() && (!room || room->lines.empty())) {
        for (const auto& candidate : model_.rooms()) {
            if (!candidate.lines.empty()) {
                room = &candidate;
                break;
            }
        }
    }
    if (!room) {
        const std::string msg =
            room_selector.empty()
                ? "Select or open a room before requesting a summary."
                : "Room '" + std::string(room_selector) + "' is not open in this client.";
        notify(msg);
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    const auto summary = AiEngine::summarize_lines(room->title, room->lines, max_sentences);
    notify(summary.formatted);
    std::ostringstream json;
    json << "{\"room\":\"" << json_escape(summary.room_title)
         << "\",\"message_count\":" << summary.message_count << ",\"model\":\""
         << json_escape(summary.model_id) << "\",\"sentiment\":{\"label\":\""
         << json_escape(summary.overall_sentiment.label) << "\",\"compound\":" << std::fixed
         << std::setprecision(4) << summary.overall_sentiment.compound << "},\"key_points\":[";
    for (std::size_t i = 0; i < summary.key_points.size(); ++i) {
        if (i > 0) {
            json << ',';
        }
        json << '"' << json_escape(summary.key_points[i]) << '"';
    }
    json << "],\"formatted\":\"" << json_escape(summary.formatted) << "\"}";
    return {true, summary.formatted, json.str()};
}

AgentActionResult AgentController::analyze_sentiment(std::string_view text_or_empty) {
    std::string target_text = trim(text_or_empty);
    std::string subject = "input text";
    if (target_text.empty()) {
        const auto* room = model_.current();
        if (!room || room->lines.empty()) {
            for (const auto& candidate : model_.rooms()) {
                if (!candidate.lines.empty()) {
                    room = &candidate;
                    break;
                }
            }
        }
        if (!room || room->lines.empty()) {
            const std::string msg =
                "Provide text or open a room with messages to analyze sentiment.";
            notify(msg);
            return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
        }
        subject = room->title;
        for (const auto& line : room->lines) {
            target_text += line.text + " ";
        }
    }
    const auto score = AiEngine::analyze_sentiment(target_text);
    std::ostringstream formatted;
    formatted << "AI Sentiment (" << subject << "): " << score.label << " (compound=" << std::fixed
              << std::setprecision(2) << score.compound << ", pos=" << score.positive
              << ", neu=" << score.neutral << ", neg=" << score.negative << ")";
    const std::string text_out = formatted.str();
    notify(text_out);
    std::ostringstream json;
    json << "{\"subject\":\"" << json_escape(subject) << "\",\"label\":\""
         << json_escape(score.label) << "\",\"compound\":" << std::fixed << std::setprecision(4)
         << score.compound << ",\"positive\":" << score.positive << ",\"neutral\":" << score.neutral
         << ",\"negative\":" << score.negative << ",\"formatted\":\"" << json_escape(text_out)
         << "\"}";
    return {true, text_out, json.str()};
}

AgentActionResult AgentController::extract_action_items(std::string_view room_selector) {
    const auto* room = find_room(room_selector);
    if (room_selector.empty() && (!room || room->lines.empty())) {
        for (const auto& candidate : model_.rooms()) {
            if (!candidate.lines.empty()) {
                room = &candidate;
                break;
            }
        }
    }
    if (!room) {
        const std::string msg =
            room_selector.empty()
                ? "Select or open a room before extracting action items."
                : "Room '" + std::string(room_selector) + "' is not open in this client.";
        notify(msg);
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    const auto items = AiEngine::extract_action_items(room->title, room->lines);
    notify(items.formatted);
    std::ostringstream json;
    json << "{\"room\":\"" << json_escape(items.room_title) << "\",\"action_items\":[";
    for (std::size_t i = 0; i < items.action_items.size(); ++i) {
        if (i > 0) {
            json << ',';
        }
        json << '"' << json_escape(items.action_items[i]) << '"';
    }
    json << "],\"decisions\":[";
    for (std::size_t i = 0; i < items.decisions.size(); ++i) {
        if (i > 0) {
            json << ',';
        }
        json << '"' << json_escape(items.decisions[i]) << '"';
    }
    json << "],\"open_questions\":[";
    for (std::size_t i = 0; i < items.open_questions.size(); ++i) {
        if (i > 0) {
            json << ',';
        }
        json << '"' << json_escape(items.open_questions[i]) << '"';
    }
    json << "],\"formatted\":\"" << json_escape(items.formatted) << "\"}";
    return {true, items.formatted, json.str()};
}

AgentActionResult AgentController::smart_reply(bool auto_send) {
    const auto* room = model_.current();
    if (!room || room->lines.empty()) {
        for (const auto& candidate : model_.rooms()) {
            if (!candidate.lines.empty()) {
                room = &candidate;
                break;
            }
        }
    }
    if (!room || room->lines.empty()) {
        const std::string msg = "No messages in the active room to reply to.";
        notify(msg);
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    const std::string reply_text =
        AiEngine::suggest_smart_reply(model_.username(), room->title, room->lines, llm_);
    if (auto_send) {
        if (room->group) {
            return call_tool("send_group_message",
                             "{\"group_id\":\"" + json_escape(room->target) +
                                 "\",\"text\":\"" + json_escape(reply_text) + "\"}");
        }
        return call_tool("send_direct_message",
                         "{\"recipient\":\"" + json_escape(room->target) + "\",\"text\":\"" +
                             json_escape(reply_text) + "\"}");
    }
    notify_draft(reply_text);
    const std::string msg = "Smart reply staged in composer: \"" + reply_text + "\"";
    notify(msg);
    return {true, msg, "{\"suggested_reply\":\"" + json_escape(reply_text) + "\"}"};
}

AgentActionResult AgentController::polish_draft(std::string_view draft_text) {
    std::string input = trim(draft_text);
    if (input.empty() && model_.current()) {
        input = trim(model_.current()->draft);
    }
    if (input.empty()) {
        const std::string msg = "Type a draft message first (or pass 'polish: <text>') to polish it.";
        notify(msg);
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    const std::string polished = AiEngine::polish_draft(input, llm_);
    notify_draft(polished);
    const std::string msg = "Polished draft: \"" + polished + "\"";
    notify(msg);
    return {true, msg, "{\"polished\":\"" + json_escape(polished) + "\"}"};
}

AgentActionResult AgentController::configure_llm(std::string_view spec) {
    const std::string cleaned = trim(spec);
    const std::string lower = to_lower_ascii(cleaned);
    if (lower == "ai status" || lower == "llm status" || lower == "model status") {
        std::ostringstream out;
        if (llm_.enabled) {
            out << "AI Mode: Hybrid LLM enabled (" << llm_.model << " @ " << llm_.host << ":"
                << llm_.port << (llm_.tls ? " TLS" : " HTTP") << ") + " << AiEngine::model_reference();
        } else {
            out << "AI Mode: Local C++ Engine (" << AiEngine::model_reference()
                << "). Tip: run 'set key <GEMINI_OR_OPENAI_KEY>' or 'use ollama' for live LLM!";
        }
        const std::string msg = out.str();
        notify(msg);
        return {true, msg, "{\"enabled\":" + std::string(llm_.enabled ? "true" : "false") + "}"};
    }
    if (lower == "use local" || lower == "disable llm" || lower == "offline mode") {
        llm_.enabled = false;
        const std::string msg =
            "Switched to deterministic local C++ AI engine (" +
            std::string(AiEngine::model_reference()) + ").";
        notify(msg);
        return {true, msg, "{\"enabled\":false}"};
    }
    if (lower.starts_with("set key ") || lower.starts_with("api key ") ||
        lower.starts_with("key ")) {
        const auto sp = cleaned.find(' ');
        std::string key = sp == std::string::npos ? "" : trim(cleaned.substr(sp + 1));
        if (to_lower_ascii(key).starts_with("key ")) {
            key = unquote(key.substr(4));
        } else {
            key = unquote(key);
        }
        if (key.empty()) {
            const std::string msg = "Provide a non-empty API key after 'set key'.";
            notify(msg);
            return {false, msg, "{\"error\":\"empty key\"}"};
        }
        llm_.api_key = key;
        llm_.enabled = true;
        if (key.starts_with("AIza")) {
            llm_.host = "generativelanguage.googleapis.com";
            llm_.port = 443;
            llm_.path = "/v1beta/openai/chat/completions";
            llm_.model = "gemini-2.5-flash";
            llm_.tls = true;
        } else {
            llm_.host = "api.openai.com";
            llm_.port = 443;
            llm_.path = "/v1/chat/completions";
            llm_.model = "gpt-4o-mini";
            llm_.tls = true;
        }
        const std::string msg =
            "Configured live HTTPS LLM (" + llm_.model + " via " + llm_.host + ").";
        notify(msg);
        return {true, msg, "{\"enabled\":true,\"model\":\"" + json_escape(llm_.model) + "\"}"};
    }
    if (lower.starts_with("use gemini")) {
        const std::string model = trim(cleaned.substr(10));
        llm_.host = "generativelanguage.googleapis.com";
        llm_.port = 443;
        llm_.path = "/v1beta/openai/chat/completions";
        llm_.model = model.empty() ? "gemini-2.5-flash" : unquote(model);
        llm_.tls = true;
        llm_.enabled = !llm_.api_key.empty();
        const std::string msg =
            llm_.enabled
                ? ("Using Gemini model '" + llm_.model + "'.")
                : ("Selected Gemini model '" + llm_.model +
                   "' — run 'set key <GEMINI_API_KEY>' to activate.");
        notify(msg);
        return {true, msg, "{\"model\":\"" + json_escape(llm_.model) + "\"}"};
    }
    if (lower.starts_with("use openai")) {
        const std::string model = trim(cleaned.substr(10));
        llm_.host = "api.openai.com";
        llm_.port = 443;
        llm_.path = "/v1/chat/completions";
        llm_.model = model.empty() ? "gpt-4o-mini" : unquote(model);
        llm_.tls = true;
        llm_.enabled = !llm_.api_key.empty();
        const std::string msg =
            llm_.enabled
                ? ("Using OpenAI model '" + llm_.model + "'.")
                : ("Selected OpenAI model '" + llm_.model +
                   "' — run 'set key <OPENAI_API_KEY>' to activate.");
        notify(msg);
        return {true, msg, "{\"model\":\"" + json_escape(llm_.model) + "\"}"};
    }
    if (lower.starts_with("use ollama")) {
        const std::string model = trim(cleaned.substr(10));
        llm_.host = "127.0.0.1";
        llm_.port = 11434;
        llm_.path = "/v1/chat/completions";
        llm_.model = model.empty() ? "llama3.2" : unquote(model);
        llm_.tls = false;
        llm_.enabled = true;
        const std::string msg =
            "Enabled local Ollama LLM ('" + llm_.model + "' on 127.0.0.1:11434).";
        notify(msg);
        return {true, msg, "{\"enabled\":true,\"model\":\"" + json_escape(llm_.model) + "\"}"};
    }
    return {false, "Unrecognized LLM configuration command.", "{}"};
}

namespace {
std::vector<std::string> split_multi_step_prompt(std::string_view input) {
    constexpr std::array<std::string_view, 22> kActionStarters = {
        "create ",    "make ",       "join ",       "open ",       "switch to ",
        "go to ",     "send ",       "tell ",       "dm ",         "message ",
        "text ",      "whisper ",    "group ",      "load ",       "summarize",
        "tldr",       "sentiment",   "action",      "smart reply", "auto reply",
        "polish",     "search "};

    std::vector<std::string> steps;
    std::string current;
    char quote = '\0';
    const std::string s(input);
    const std::string lower = to_lower_ascii(s);

    auto starts_with_action = [&](std::size_t idx) -> bool {
        while (idx < lower.size() && (lower[idx] == ' ' || lower[idx] == '\t')) {
            ++idx;
        }
        const std::string_view rem(lower.data() + idx, lower.size() - idx);
        for (const auto starter : kActionStarters) {
            if (rem.starts_with(starter)) {
                return true;
            }
        }
        return false;
    };

    for (std::size_t i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            current.push_back(ch);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            current.push_back(ch);
            continue;
        }
        if (ch == ';') {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                steps.push_back(trimmed);
            }
            current.clear();
            continue;
        }
        const std::string_view rem_lower(lower.data() + i, lower.size() - i);
        if (rem_lower.starts_with(" and then ") && starts_with_action(i + 10)) {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                steps.push_back(trimmed);
            }
            current.clear();
            i += 9;
            continue;
        }
        if (rem_lower.starts_with(", then ") && starts_with_action(i + 7)) {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                steps.push_back(trimmed);
            }
            current.clear();
            i += 6;
            continue;
        }
        if (rem_lower.starts_with(" then ") && starts_with_action(i + 6)) {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                steps.push_back(trimmed);
            }
            current.clear();
            i += 5;
            continue;
        }
        if (rem_lower.starts_with(" and ") && starts_with_action(i + 5)) {
            const std::string trimmed = trim(current);
            if (!trimmed.empty()) {
                steps.push_back(trimmed);
            }
            current.clear();
            i += 4;
            continue;
        }
        current.push_back(ch);
    }
    const std::string tail = trim(current);
    if (!tail.empty()) {
        steps.push_back(tail);
    }
    return steps;
}
} // namespace

AgentActionResult AgentController::execute_prompt(std::string_view prompt) {
    const std::string raw = trim(prompt);
    if (raw.empty() || raw.size() > 4096) {
        const std::string msg = "AI prompt must be 1..4096 UTF-8 bytes.";
        notify(msg);
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    const auto steps = split_multi_step_prompt(raw);
    if (steps.size() > 1 && steps.size() <= 6) {
        bool all_ok = true;
        std::ostringstream combined_msg;
        std::ostringstream combined_json;
        combined_json << "{\"steps\":[";
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const auto res = execute_single_prompt(steps[i]);
            all_ok = all_ok && res.ok;
            if (i > 0) {
                combined_msg << " | ";
                combined_json << ',';
            }
            combined_msg << "Step " << (i + 1) << ": " << res.message;
            combined_json << res.json_data;
        }
        combined_json << "]}";
        const std::string final_msg = combined_msg.str();
        notify(final_msg);
        return {all_ok, final_msg, combined_json.str()};
    }
    return execute_single_prompt(raw);
}

AgentActionResult AgentController::execute_single_prompt(std::string_view prompt) {
    std::string raw = trim(prompt);
    const std::string raw_lower = to_lower_ascii(raw);
    if (raw_lower.starts_with("@ai ") || raw_lower.starts_with("/ai ")) {
        raw = trim(raw.substr(4));
    }
    if (raw.empty()) {
        return execute_single_prompt("help");
    }
    const std::string cleaned = strip_polite_prefix(raw);
    const std::string lower = to_lower_ascii(cleaned);
    const std::string lower_nopunct = strip_trailing_punct(lower);

    // 0. Live LLM configuration commands
    if (lower_nopunct == "ai status" || lower_nopunct == "llm status" ||
        lower_nopunct == "model status" || lower_nopunct == "use local" ||
        lower_nopunct == "disable llm" || lower_nopunct == "offline mode" ||
        lower.starts_with("set key ") || lower.starts_with("api key ") ||
        lower.starts_with("use gemini") || lower.starts_with("use openai") ||
        lower.starts_with("use ollama")) {
        return configure_llm(cleaned);
    }

    // 1. Help & capabilities
    if (lower_nopunct == "help" || lower_nopunct == "/help" ||
        lower_nopunct == "what can you do" || lower_nopunct == "commands") {
        const std::string msg =
            "AI Copilot supports multi-step actions, smart compose & insights! Try: "
            "'create group DevTeam and tell Bob Join DevTeam!', 'action items', 'smart reply', "
            "'polish: hey u free tmrw', 'summarize', 'how is the mood?', 'who is online?', "
            "'what did Bob say?', or 'set key <GEMINI_OR_OPENAI_KEY>'.";
        notify(msg);
        return {true, msg, "{\"help\":\"" + json_escape(msg) + "\"}"};
    }

    // 2. Action items, decisions & open questions extraction
    if (lower_nopunct == "action items" || lower_nopunct == "actions" ||
        lower_nopunct == "tasks" || lower_nopunct == "todo" || lower_nopunct == "todos" ||
        lower_nopunct == "decisions" || lower_nopunct == "open questions" ||
        lower_nopunct == "extract action items" || lower_nopunct == "extract tasks" ||
        lower_nopunct.find("what are the action items") != std::string::npos ||
        lower_nopunct.find("any action items") != std::string::npos ||
        lower_nopunct.find("what was decided") != std::string::npos) {
        return extract_action_items();
    }
    for (const std::string_view prefix :
         {"action items in ", "action items for ", "extract action items from ", "tasks in "}) {
        if (lower.starts_with(prefix)) {
            return extract_action_items(strip_trailing_punct(cleaned.substr(prefix.size())));
        }
    }

    // 3. Polish / rewrite draft
    if (lower_nopunct == "polish" || lower_nopunct == "polish draft" ||
        lower_nopunct == "rewrite" || lower_nopunct == "rewrite draft" ||
        lower_nopunct == "fix grammar" || lower_nopunct == "improve draft") {
        return polish_draft();
    }
    for (const std::string_view prefix :
         {"polish: ", "polish draft: ", "polish ", "rewrite: ", "rewrite ", "fix grammar: "}) {
        if (lower.starts_with(prefix)) {
            return polish_draft(unquote(cleaned.substr(prefix.size())));
        }
    }

    // 4. Status / identity queries
    if (lower_nopunct == "status" || lower_nopunct == "get status" ||
        lower_nopunct == "my status" || lower_nopunct == "connection status" ||
        lower_nopunct == "who am i" || lower_nopunct == "what is my name" ||
        lower_nopunct == "what's my name" || lower_nopunct == "where am i" ||
        lower_nopunct == "current room") {
        const auto res = call_tool("get_status", "{}");
        notify(res.message);
        return res;
    }

    // 5. Connected users discovery
    if (lower_nopunct == "users" || lower_nopunct == "list users" ||
        lower_nopunct == "show users" || lower_nopunct == "online users" ||
        lower_nopunct == "connected users" || lower_nopunct.find("who is online") != std::string::npos ||
        lower_nopunct.find("who's online") != std::string::npos ||
        lower_nopunct.find("who is here") != std::string::npos ||
        lower_nopunct.find("who's here") != std::string::npos ||
        lower_nopunct.find("who is connected") != std::string::npos) {
        return call_tool("list_users", "{}");
    }

    // 4. Group members query ("who is in StudyRoom?", "members of group-2")
    for (const std::string_view prefix :
         {"who is in ", "who's in ", "members of ", "list members of ", "show members of "}) {
        if (lower_nopunct.starts_with(prefix)) {
            const std::string target =
                strip_trailing_punct(cleaned.substr(prefix.size()));
            if (const auto grp = resolve_group(model_, target)) {
                std::ostringstream out;
                out << "Group '" << grp->name << "' (" << grp->group_id << ") has "
                    << grp->members.size() << " member(s):";
                for (const auto& m : grp->members) {
                    out << ' ' << m;
                }
                const std::string msg = out.str();
                notify(msg);
                return {true, msg, "{\"group_id\":\"" + json_escape(grp->group_id) + "\"}"};
            }
        }
    }

    // 5. Groups & rooms discovery
    if (lower_nopunct == "groups" || lower_nopunct == "list groups" ||
        lower_nopunct == "show groups" || lower_nopunct == "what groups" ||
        lower_nopunct.find("what groups") != std::string::npos ||
        lower_nopunct.find("which groups") != std::string::npos ||
        lower_nopunct == "available groups") {
        return call_tool("list_groups", "{}");
    }
    if (lower_nopunct == "rooms" || lower_nopunct == "list rooms" ||
        lower_nopunct == "show rooms" || lower_nopunct == "my rooms") {
        const auto res = call_tool("list_rooms", "{}");
        notify(res.message);
        return res;
    }

    // 6. Group creation ("create a new group called X", "make group X", "create group X")
    for (const std::string_view prefix :
         {"create a new group called ", "create a new group named ", "create a group called ",
          "create a group named ",      "create new group ",         "create group ",
          "make a new group called ",   "make a group called ",      "make a group named ",
          "make group ",                "start a group called ",     "start group ",
          "new group "}) {
        if (lower.starts_with(prefix)) {
            const std::string name = unquote(strip_trailing_punct(cleaned.substr(prefix.size())));
            return call_tool("create_group", "{\"name\":\"" + json_escape(name) + "\"}");
        }
    }

    // 7. Group joining ("join group StudyRoom", "join StudyRoom", "enter group-1")
    for (const std::string_view prefix :
         {"join the group ", "join group ", "enter group ", "subscribe to group ", "join "}) {
        if (lower.starts_with(prefix)) {
            const std::string target = unquote(strip_trailing_punct(cleaned.substr(prefix.size())));
            const auto grp = resolve_group(model_, target);
            const std::string id = grp ? grp->group_id : target;
            return call_tool("join_group", "{\"group_id\":\"" + json_escape(id) + "\"}");
        }
    }

    // 8. History loading ("load history", "fetch old messages", "load history for StudyRoom")
    if (lower_nopunct == "history" || lower_nopunct == "load history" ||
        lower_nopunct == "fetch history" || lower_nopunct == "get history" ||
        lower_nopunct == "show history" || lower_nopunct == "load older messages" ||
        lower_nopunct == "show old messages" || lower_nopunct == "old messages") {
        return call_tool("load_room_history", "{}");
    }
    for (const std::string_view prefix :
         {"load history for ", "load history of ", "load history ", "history for ", "history of ",
          "history "}) {
        if (lower.starts_with(prefix)) {
            const std::string target = unquote(strip_trailing_punct(cleaned.substr(prefix.size())));
            const auto* room = find_room(target);
            const std::string room_id =
                (room && !room->wire_id.empty()) ? room->wire_id : target;
            return call_tool("load_room_history", "{\"room_id\":\"" + json_escape(room_id) + "\"}");
        }
    }

    // 9. Opening / switching room views ("open chat with Bob", "switch to StudyRoom")
    for (const std::string_view prefix :
         {"open chat with ", "open room ", "open direct ", "open group ", "switch to room ",
          "switch to ",      "go to room ", "go to ",       "select room ", "open "}) {
        if (lower.starts_with(prefix)) {
            const std::string target = unquote(strip_trailing_punct(cleaned.substr(prefix.size())));
            return call_tool("open_room", "{\"target\":\"" + json_escape(target) + "\"}");
        }
    }

    // 10. Smart / Auto Reply in current room ("auto reply", "smart reply", "suggest reply")
    if (lower_nopunct == "auto reply" || lower_nopunct == "autoreply" ||
        lower_nopunct == "smart reply" || lower_nopunct == "suggest reply" ||
        lower_nopunct == "suggest a reply" || lower_nopunct == "reply to last message") {
        const bool auto_send = (lower_nopunct == "auto reply" || lower_nopunct == "autoreply" ||
                                lower_nopunct == "reply to last message");
        return smart_reply(auto_send);
    }

    // 11. Conversational "send <text> to/in <target>" patterns
    for (const std::string_view prefix :
         {"send message ", "send a message ", "send ", "say ", "write ", "post "}) {
        if (lower.starts_with(prefix)) {
            const std::string body = trim(cleaned.substr(prefix.size()));
            const std::string body_lower = to_lower_ascii(body);
            for (const std::string_view sep : {" to group ", " in group ", " to ", " in "}) {
                const auto pos = body_lower.rfind(sep);
                if (pos != std::string::npos && pos > 0) {
                    const std::string text_part = unquote(body.substr(0, pos));
                    const std::string target_part =
                        unquote(strip_trailing_punct(body.substr(pos + sep.size())));
                    if (!text_part.empty() && !target_part.empty()) {
                        if (sep.find("group") != std::string_view::npos || sep == " in ") {
                            if (const auto grp = resolve_group(model_, target_part)) {
                                return call_tool("send_group_message",
                                                 "{\"group_id\":\"" + json_escape(grp->group_id) +
                                                     "\",\"text\":\"" + json_escape(text_part) +
                                                     "\"}");
                            }
                        }
                        if (const auto user = resolve_username(model_, target_part)) {
                            return call_tool("send_direct_message",
                                             "{\"recipient\":\"" + json_escape(*user) +
                                                 "\",\"text\":\"" + json_escape(text_part) + "\"}");
                        }
                        if (const auto grp = resolve_group(model_, target_part)) {
                            return call_tool("send_group_message",
                                             "{\"group_id\":\"" + json_escape(grp->group_id) +
                                                 "\",\"text\":\"" + json_escape(text_part) + "\"}");
                        }
                        return call_tool("send_direct_message",
                                         "{\"recipient\":\"" + json_escape(target_part) +
                                             "\",\"text\":\"" + json_escape(text_part) + "\"}");
                    }
                }
            }
        }
    }

    // 12. Conversational "tell/message/dm/text/whisper <target> <text>" & "group <target> <text>"
    auto split_target_and_text = [](std::string_view input) -> std::pair<std::string, std::string> {
        const std::string s = trim(input);
        const auto sp = s.find_first_of(" \t:");
        if (sp == std::string::npos) {
            return {};
        }
        std::string target = unquote(s.substr(0, sp));
        std::size_t msg_start = sp;
        while (msg_start < s.size() &&
               (s[msg_start] == ' ' || s[msg_start] == '\t' || s[msg_start] == ':')) {
            ++msg_start;
        }
        std::string text = trim(s.substr(msg_start));
        const std::string text_lower = to_lower_ascii(text);
        if (text_lower.starts_with("that ") && text.size() > 5) {
            text = trim(text.substr(5));
        } else if (text_lower.starts_with("saying ") && text.size() > 7) {
            text = trim(text.substr(7));
        }
        return {target, unquote(text)};
    };

    for (const std::string_view prefix :
         {"message group ", "tell group ", "post to group ", "group "}) {
        if (lower.starts_with(prefix)) {
            const auto [target, text] = split_target_and_text(cleaned.substr(prefix.size()));
            if (!target.empty() && !text.empty()) {
                const auto grp = resolve_group(model_, target);
                const std::string id = grp ? grp->group_id : target;
                return call_tool("send_group_message",
                                 "{\"group_id\":\"" + json_escape(id) + "\",\"text\":\"" +
                                     json_escape(text) + "\"}");
            }
        }
    }

    for (const std::string_view prefix :
         {"send dm to ", "send a dm to ", "direct message ", "private message ", "whisper to ",
          "whisper ",    "message to ",   "message ",        "msg ",             "tell ",
          "text ",       "pm ",           "dm "}) {
        if (lower.starts_with(prefix)) {
            const auto [target, text] = split_target_and_text(cleaned.substr(prefix.size()));
            if (!target.empty() && !text.empty()) {
                if (const auto user = resolve_username(model_, target)) {
                    return call_tool("send_direct_message",
                                     "{\"recipient\":\"" + json_escape(*user) + "\",\"text\":\"" +
                                         json_escape(text) + "\"}");
                }
                if (const auto grp = resolve_group(model_, target)) {
                    return call_tool("send_group_message",
                                     "{\"group_id\":\"" + json_escape(grp->group_id) +
                                         "\",\"text\":\"" + json_escape(text) + "\"}");
                }
                return call_tool("send_direct_message",
                                 "{\"recipient\":\"" + json_escape(target) + "\",\"text\":\"" +
                                     json_escape(text) + "\"}");
            }
        }
    }

    // 13. Reply / send directly to active room ("reply <text>", "say: <text>", "send: <text>")
    for (const std::string_view prefix : {"reply: ", "reply ", "say: ", "send: ", "post: "}) {
        if (lower.starts_with(prefix)) {
            const std::string text = unquote(cleaned.substr(prefix.size()));
            const auto* room = model_.current();
            if (!room) {
                const std::string msg = "Select a room first before replying.";
                notify(msg);
                return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
            }
            if (room->group) {
                return call_tool("send_group_message",
                                 "{\"group_id\":\"" + json_escape(room->target) +
                                     "\",\"text\":\"" + json_escape(text) + "\"}");
            }
            return call_tool("send_direct_message",
                             "{\"recipient\":\"" + json_escape(room->target) + "\",\"text\":\"" +
                                 json_escape(text) + "\"}");
        }
    }

    // 14. Transcript Q&A: "what did <user> say?", "messages from <user>"
    for (const std::string_view prefix :
         {"what did ", "what has ", "messages from ", "show messages from ", "last message from "}) {
        if (lower_nopunct.starts_with(prefix)) {
            std::string rest = strip_trailing_punct(cleaned.substr(prefix.size()));
            const std::string rest_lower = to_lower_ascii(rest);
            for (const std::string_view suffix : {" say in this room", " say", " said", " write", " send"}) {
                if (rest_lower.ends_with(suffix)) {
                    rest = trim(rest.substr(0, rest.size() - suffix.size()));
                    break;
                }
            }
            const std::string target_user = resolve_username(model_, rest).value_or(unquote(rest));
            const std::string target_lower = to_lower_ascii(target_user);
            std::vector<std::string> matches;
            auto collect_from_room = [&](const client::RoomView& r) {
                for (const auto& line : r.lines) {
                    if (to_lower_ascii(line.sender) == target_lower) {
                        matches.push_back("[" + r.title + "] " + line.sender + ": " + trim(line.text));
                    }
                }
            };
            if (model_.current()) {
                collect_from_room(*model_.current());
            }
            if (matches.empty()) {
                for (const auto& r : model_.rooms()) {
                    collect_from_room(r);
                }
            }
            if (matches.empty()) {
                const std::string msg = "No messages found from '" + target_user + "'.";
                notify(msg);
                return {true, msg, "{\"matches\":[]}"};
            }
            std::ostringstream out;
            out << "Found " << matches.size() << " message(s) from " << target_user << ": ";
            const std::size_t start_idx = matches.size() > 3 ? matches.size() - 3 : 0;
            for (std::size_t i = start_idx; i < matches.size(); ++i) {
                if (i > start_idx) {
                    out << " | ";
                }
                out << matches[i];
            }
            const std::string msg = out.str();
            notify(msg);
            return {true, msg, "{\"count\":" + std::to_string(matches.size()) + "}"};
        }
    }

    // 15. Transcript search ("search for <keyword>", "find <keyword>")
    for (const std::string_view prefix :
         {"search messages for ", "search for ", "search ", "find messages with ", "find "}) {
        if (lower.starts_with(prefix)) {
            const std::string query = unquote(strip_trailing_punct(cleaned.substr(prefix.size())));
            const std::string query_lower = to_lower_ascii(query);
            std::vector<std::string> hits;
            for (const auto& r : model_.rooms()) {
                for (const auto& line : r.lines) {
                    if (to_lower_ascii(line.text).find(query_lower) != std::string::npos ||
                        to_lower_ascii(line.sender).find(query_lower) != std::string::npos) {
                        hits.push_back("[" + r.title + "] " + line.sender + ": " + trim(line.text));
                    }
                }
            }
            if (hits.empty()) {
                const std::string msg = "No messages matching '" + query + "'.";
                notify(msg);
                return {true, msg, "{\"hits\":0}"};
            }
            std::ostringstream out;
            out << "Search '" << query << "' (" << hits.size() << " hit(s)): ";
            for (std::size_t i = 0; i < std::min<std::size_t>(3, hits.size()); ++i) {
                if (i > 0) {
                    out << " | ";
                }
                out << hits[i];
            }
            const std::string msg = out.str();
            notify(msg);
            return {true, msg, "{\"hits\":" + std::to_string(hits.size()) + "}"};
        }
    }

    // 16. Room stats / last message queries
    if (lower_nopunct == "last message" || lower_nopunct == "latest message" ||
        lower_nopunct == "what was the last message") {
        const auto* room = model_.current();
        if (!room || room->lines.empty()) {
            for (const auto& candidate : model_.rooms()) {
                if (!candidate.lines.empty()) {
                    room = &candidate;
                    break;
                }
            }
        }
        if (!room || room->lines.empty()) {
            const std::string msg = "No messages in the current room.";
            notify(msg);
            return {false, msg, "{\"error\":\"empty room\"}"};
        }
        const auto& last = room->lines.back();
        const std::string msg =
            "Last message in " + room->title + " — " + last.sender + ": " + trim(last.text);
        notify(msg);
        return {true, msg, "{\"sender\":\"" + json_escape(last.sender) + "\"}"};
    }
    if (lower_nopunct.find("how many messages") != std::string::npos ||
        lower_nopunct == "message count" || lower_nopunct == "room stats" ||
        lower_nopunct == "stats" || lower_nopunct == "statistics") {
        return summarize_room();
    }

    // 17. Summarization intents ("summarize", "tldr", "recap", "what happened", "catch me up")
    if (lower_nopunct == "summarize" || lower_nopunct == "summary" ||
        lower_nopunct == "summarize room" || lower_nopunct == "summarize this room" ||
        lower_nopunct == "summarize chat" || lower_nopunct == "tldr" ||
        lower_nopunct == "tl;dr" || lower_nopunct == "recap" ||
        lower_nopunct == "catch me up" || lower_nopunct == "what happened" ||
        lower_nopunct == "what are they talking about" || lower_nopunct == "key points") {
        return summarize_room();
    }
    for (const std::string_view prefix :
         {"summarize room ", "summarize chat with ", "summarize ", "summary of ", "recap of ",
          "recap ",          "tldr of ",             "tldr "}) {
        if (lower.starts_with(prefix)) {
            return summarize_room(strip_trailing_punct(cleaned.substr(prefix.size())));
        }
    }

    // 18. Sentiment / mood / vibe intents
    if (lower_nopunct == "sentiment" || lower_nopunct == "analyze sentiment" ||
        lower_nopunct == "room sentiment" || lower_nopunct == "vibe" ||
        lower_nopunct == "vibe check" ||
        lower_nopunct.find("how is the mood") != std::string::npos ||
        lower_nopunct.find("what is the mood") != std::string::npos ||
        lower_nopunct.find("what is the vibe") != std::string::npos ||
        lower_nopunct.find("what is the sentiment") != std::string::npos ||
        lower_nopunct.find("how do people feel") != std::string::npos ||
        lower_nopunct.find("positive or negative") != std::string::npos) {
        return analyze_sentiment();
    }
    for (const std::string_view prefix :
         {"analyze sentiment of ", "analyze sentiment: ", "analyze sentiment ",
          "sentiment of ",         "sentiment: ",         "sentiment "}) {
        if (lower.starts_with(prefix)) {
            return analyze_sentiment(unquote(cleaned.substr(prefix.size())));
        }
    }

    // 19. Grounded external LLM endpoint (tool-calling + room-context Q&A) if configured
    if (llm_.enabled) {
        std::ostringstream sys;
        sys << "You are the Socket Chat AI Copilot for user '" << model_.username() << "'. "
            << "Online users:";
        for (const auto& u : model_.users()) {
            sys << ' ' << u;
        }
        sys << ". Groups:";
        for (const auto& g : model_.groups()) {
            sys << " [" << g.group_id << " (" << g.name << ")]";
        }
        if (const auto* cur = model_.current()) {
            sys << ". Active room: " << cur->title << " (" << cur->lines.size() << " msgs). ";
            const std::size_t start = cur->lines.size() > 10 ? cur->lines.size() - 10 : 0;
            for (std::size_t i = start; i < cur->lines.size(); ++i) {
                sys << cur->lines[i].sender << ": " << cur->lines[i].text << " | ";
            }
        }
        sys << "\nIf the user wants to execute a chat action, respond ONLY with a JSON object: "
            << R"({"tool":"<send_direct_message|send_group_message|create_group|join_group|open_room|load_room_history|summarize_room|extract_action_items>","arguments":{...}})"
            << ". Otherwise answer the user's question concisely in 1-2 sentences.";
        if (auto llm_reply = AiEngine::query_llm_endpoint(llm_, sys.str(), cleaned)) {
            const std::string reply_trim = trim(*llm_reply);
            if (reply_trim.starts_with('{') && reply_trim.ends_with('}')) {
                if (const auto tool_name = extract_json_string(reply_trim, "tool")) {
                    const auto args = extract_json_raw(reply_trim, "arguments").value_or("{}");
                    return call_tool(*tool_name, args);
                }
            }
            const std::string msg = "AI Copilot (" + llm_.model + "): " + reply_trim;
            notify(msg);
            return {true, msg, "{\"reply\":\"" + json_escape(msg) + "\"}"};
        }
    }

    // 20. Fallback: if the prompt is a question and a room has messages, summarize + action items;
    // otherwise analyze sentiment of the provided text and hint 'help'.
    if (cleaned.back() == '?' && model_.current() && !model_.current()->lines.empty()) {
        return summarize_room();
    }
    return analyze_sentiment(cleaned);
}

AgentActionResult AgentController::call_tool(std::string_view tool_name,
                                             std::string_view arguments_json) {
    const bool connected = is_connected_ && is_connected_();
    if (tool_name == "get_status") {
        const auto* current = model_.current();
        std::ostringstream json;
        json << "{\"connected\":" << (connected ? "true" : "false") << ",\"username\":\""
             << json_escape(model_.username()) << "\",\"current_room\":\""
             << json_escape(current ? current->title : "")
             << "\",\"user_count\":" << model_.users().size()
             << ",\"group_count\":" << model_.groups().size()
             << ",\"room_count\":" << model_.rooms().size() << ",\"ai_model\":\""
             << json_escape(AiEngine::model_reference()) << "\"}";
        const std::string msg =
            std::string("Status: ") +
            (connected ? "connected as " + model_.username() : "disconnected") +
            ", users=" + std::to_string(model_.users().size()) +
            ", groups=" + std::to_string(model_.groups().size());
        return {true, msg, json.str()};
    }
    if (tool_name == "list_users") {
        std::ostringstream json;
        std::ostringstream text;
        text << "Connected users (" << model_.users().size() << "):";
        json << "{\"users\":[";
        for (std::size_t i = 0; i < model_.users().size(); ++i) {
            if (i > 0) {
                json << ',';
            }
            json << '"' << json_escape(model_.users()[i]) << '"';
            text << ' ' << model_.users()[i];
        }
        json << "]}";
        const std::string out = text.str();
        notify(out);
        return {true, out, json.str()};
    }
    if (tool_name == "list_groups") {
        std::ostringstream json;
        std::ostringstream text;
        text << "Groups (" << model_.groups().size() << "):";
        json << "{\"groups\":[";
        for (std::size_t i = 0; i < model_.groups().size(); ++i) {
            const auto& g = model_.groups()[i];
            if (i > 0) {
                json << ',';
            }
            json << "{\"group_id\":\"" << json_escape(g.group_id) << "\",\"name\":\""
                 << json_escape(g.name) << "\",\"members\":[";
            for (std::size_t m = 0; m < g.members.size(); ++m) {
                if (m > 0) {
                    json << ',';
                }
                json << '"' << json_escape(g.members[m]) << '"';
            }
            json << "]}";
            text << " [" << g.group_id << " " << g.name << ": " << g.members.size() << " members]";
        }
        json << "]}";
        const std::string out = text.str();
        notify(out);
        return {true, out, json.str()};
    }
    if (tool_name == "list_rooms") {
        std::ostringstream json;
        json << "{\"rooms\":[";
        for (std::size_t i = 0; i < model_.rooms().size(); ++i) {
            const auto& r = model_.rooms()[i];
            if (i > 0) {
                json << ',';
            }
            json << "{\"key\":\"" << json_escape(r.key) << "\",\"wire_id\":\""
                 << json_escape(r.wire_id) << "\",\"target\":\"" << json_escape(r.target)
                 << "\",\"title\":\"" << json_escape(r.title)
                 << "\",\"group\":" << (r.group ? "true" : "false")
                 << ",\"joined\":" << (r.joined ? "true" : "false")
                 << ",\"available\":" << (r.available ? "true" : "false")
                 << ",\"line_count\":" << r.lines.size() << "}";
        }
        json << "]}";
        return {true, "Listed " + std::to_string(model_.rooms().size()) + " room views.",
                json.str()};
    }
    if (tool_name == "open_room") {
        const auto target = extract_json_string(arguments_json, "target");
        if (!target || target->empty()) {
            return {false, "open_room requires a non-empty 'target'.",
                    "{\"error\":\"missing target\"}"};
        }
        try {
            if (const auto user = resolve_username(model_, *target)) {
                model_.open_direct(*user);
            } else if (const auto grp = resolve_group(model_, *target)) {
                model_.open_group(grp->group_id);
            } else {
                model_.select(*target);
            }
            const auto* cur = model_.current();
            const std::string msg = "Opened room: " + (cur ? cur->title : *target);
            notify(msg);
            return {true, msg,
                    "{\"selected\":\"" + json_escape(cur ? cur->key : *target) + "\"}"};
        } catch (const std::exception& error) {
            return {false, error.what(), "{\"error\":\"" + json_escape(error.what()) + "\"}"};
        }
    }
    if (tool_name == "get_room_messages") {
        const auto selector = extract_json_string(arguments_json, "room").value_or("");
        const auto* room = find_room(selector);
        if (!room) {
            return {false, "Requested room is not available.",
                    "{\"error\":\"room not available\"}"};
        }
        std::ostringstream json;
        json << "{\"room\":\"" << json_escape(room->title) << "\",\"wire_id\":\""
             << json_escape(room->wire_id) << "\",\"messages\":[";
        for (std::size_t i = 0; i < room->lines.size(); ++i) {
            const auto& line = room->lines[i];
            if (i > 0) {
                json << ',';
            }
            json << "{\"sender\":\"" << json_escape(line.sender) << "\",\"text\":\""
                 << json_escape(line.text) << "\",\"sequence\":" << line.room_sequence << "}";
        }
        json << "]}";
        return {true,
                "Retrieved " + std::to_string(room->lines.size()) + " messages from " + room->title,
                json.str()};
    }
    if (tool_name == "summarize_room") {
        const auto selector = extract_json_string(arguments_json, "room").value_or("");
        const auto max_sentences =
            extract_json_uint(arguments_json, "max_sentences").value_or(std::size_t{3});
        return summarize_room(selector, max_sentences);
    }
    if (tool_name == "extract_action_items") {
        const auto selector = extract_json_string(arguments_json, "room").value_or("");
        return extract_action_items(selector);
    }
    if (tool_name == "suggest_smart_reply") {
        const auto auto_send_raw = extract_json_raw(arguments_json, "auto_send").value_or("false");
        return smart_reply(auto_send_raw == "true");
    }
    if (tool_name == "polish_draft") {
        const auto text = extract_json_string(arguments_json, "text").value_or("");
        return polish_draft(text);
    }
    if (tool_name == "analyze_sentiment") {
        const auto text = extract_json_string(arguments_json, "text").value_or("");
        return analyze_sentiment(text);
    }
    if (tool_name == "run_agent_prompt") {
        const auto prompt = extract_json_string(arguments_json, "prompt").value_or("");
        return execute_prompt(prompt);
    }
    if (!connected || !submit_) {
        const std::string msg = "Client is not connected to a server.";
        return {false, msg, "{\"error\":\"" + json_escape(msg) + "\"}"};
    }
    if (tool_name == "load_room_history") {
        std::string room_id = extract_json_string(arguments_json, "room_id").value_or("");
        if (!room_id.empty()) {
            if (const auto* resolved = find_room(room_id)) {
                room_id = !resolved->wire_id.empty() ? resolved->wire_id : resolved->target;
            } else if (const auto grp = resolve_group(model_, room_id)) {
                room_id = grp->group_id;
            }
        } else if (model_.current()) {
            room_id = !model_.current()->wire_id.empty() ? model_.current()->wire_id
                                                         : model_.current()->target;
        }
        if (room_id.empty()) {
            return {false, "Specify 'room_id' or select a room with a known server ID.",
                    "{\"error\":\"missing room_id\"}"};
        }
        const auto id = submit_(client::CommandKind::history, room_id, {});
        if (!id) {
            return {false, "Could not queue history request.",
                    "{\"error\":\"worker rejected history request\"}"};
        }
        const std::string msg = "Queued history request for " + room_id + " (id=" +
                                std::to_string(*id) + ").";
        notify(msg);
        return {true, msg,
                "{\"queued_id\":" + std::to_string(*id) + ",\"room_id\":\"" +
                    json_escape(room_id) + "\"}"};
    }
    if (tool_name == "create_group") {
        const auto name = extract_json_string(arguments_json, "name").value_or("");
        const auto id = submit_(client::CommandKind::create_group, name, {});
        if (!id) {
            return {false, "Invalid group name or command queue full.",
                    "{\"error\":\"create_group rejected\"}"};
        }
        const std::string msg =
            "Queued group creation '" + name + "' (id=" + std::to_string(*id) + ").";
        notify(msg);
        return {true, msg, "{\"queued_id\":" + std::to_string(*id) + "}"};
    }
    if (tool_name == "join_group") {
        const auto raw_id = extract_json_string(arguments_json, "group_id").value_or("");
        const auto grp = resolve_group(model_, raw_id);
        const std::string group_id = grp ? grp->group_id : raw_id;
        const auto id = submit_(client::CommandKind::join_group, group_id, {});
        if (!id) {
            return {false, "Invalid group_id or command queue full.",
                    "{\"error\":\"join_group rejected\"}"};
        }
        if (grp) {
            model_.open_group(group_id);
        }
        const std::string msg =
            "Queued join for group '" + group_id + "' (id=" + std::to_string(*id) + ").";
        notify(msg);
        return {true, msg, "{\"queued_id\":" + std::to_string(*id) + "}"};
    }
    if (tool_name == "send_direct_message") {
        const auto raw_recipient = extract_json_string(arguments_json, "recipient").value_or("");
        const auto resolved_user = resolve_username(model_, raw_recipient);
        const std::string recipient = resolved_user.value_or(raw_recipient);
        const auto text = extract_json_string(arguments_json, "text").value_or("");
        const auto id = submit_(client::CommandKind::direct, recipient, text);
        if (!id) {
            return {false, "Invalid recipient/text or command queue full.",
                    "{\"error\":\"send_direct_message rejected\"}"};
        }
        if (resolved_user) {
            model_.open_direct(*resolved_user);
        }
        const std::string msg =
            "Queued direct message to '" + recipient + "' (id=" + std::to_string(*id) + ").";
        notify(msg);
        return {true, msg, "{\"queued_id\":" + std::to_string(*id) + "}"};
    }
    if (tool_name == "send_group_message") {
        const auto raw_group = extract_json_string(arguments_json, "group_id").value_or("");
        const auto grp = resolve_group(model_, raw_group);
        const std::string group_id = grp ? grp->group_id : raw_group;
        const auto text = extract_json_string(arguments_json, "text").value_or("");
        const auto id = submit_(client::CommandKind::group, group_id, text);
        if (!id) {
            return {false, "Invalid group_id/text or command queue full.",
                    "{\"error\":\"send_group_message rejected\"}"};
        }
        if (grp) {
            model_.open_group(group_id);
        }
        const std::string msg =
            "Queued group message to '" + group_id + "' (id=" + std::to_string(*id) + ").";
        notify(msg);
        return {true, msg, "{\"queued_id\":" + std::to_string(*id) + "}"};
    }
    return {false, "Unknown tool: " + std::string(tool_name),
            "{\"error\":\"unknown tool\"}"};
}

std::string McpDispatcher::tools_list_json() {
    return "["
           "{\"name\":\"get_status\",\"description\":\"Inspect client connection state, active "
           "room, and AI model citation.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
           "{\"name\":\"list_users\",\"description\":\"List all currently connected usernames on "
           "the server.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
           "{\"name\":\"list_groups\",\"description\":\"List all discoverable chat groups and "
           "their current members.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
           "{\"name\":\"list_rooms\",\"description\":\"List all local room views in the client "
           "model.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
           "{\"name\":\"open_room\",\"description\":\"Select and display a direct user room or "
           "group room in the client UI.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"target\":{\"type\":\"string\"}},\"required\":[\"target\"]}},"
           "{\"name\":\"get_room_messages\",\"description\":\"Read the transcript lines in a room "
           "view.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"room\":{\"type\":"
           "\"string\"}}}},"
           "{\"name\":\"load_room_history\",\"description\":\"Request stored SQLite history from "
           "the server for a room.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"room_id\":{\"type\":\"string\"}}}},"
           "{\"name\":\"create_group\",\"description\":\"Create a new group with only this client "
           "as the initial member.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":"
           "{\"type\":\"string\"}},\"required\":[\"name\"]}},"
           "{\"name\":\"join_group\",\"description\":\"Explicitly self-join an existing group by "
           "group_id.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"group_id\":{"
           "\"type\":\"string\"}},\"required\":[\"group_id\"]}},"
           "{\"name\":\"send_direct_message\",\"description\":\"Send a private direct message to a "
           "connected user.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"recipient\":{"
           "\"type\":\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"recipient\","
           "\"text\"]}},"
           "{\"name\":\"send_group_message\",\"description\":\"Send a message to a joined "
           "group.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"group_id\":{\"type\":"
           "\"string\"},\"text\":{\"type\":\"string\"}},\"required\":[\"group_id\",\"text\"]}},"
           "{\"name\":\"summarize_room\",\"description\":\"Summarize a room transcript using "
           "TextRank centrality and VADER sentiment analysis.\",\"inputSchema\":{\"type\":"
           "\"object\",\"properties\":{\"room\":{\"type\":\"string\"},\"max_sentences\":{\"type\":"
           "\"integer\"}}}},"
           "{\"name\":\"extract_action_items\",\"description\":\"Extract tasks, decisions, and open "
           "questions from a room transcript.\",\"inputSchema\":{\"type\":\"object\",\"properties\":"
           "{\"room\":{\"type\":\"string\"}}}},"
           "{\"name\":\"suggest_smart_reply\",\"description\":\"Generate a context-aware smart "
           "reply and stage or send it.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"auto_send\":{\"type\":\"boolean\"}}}},"
           "{\"name\":\"polish_draft\",\"description\":\"Rewrite and polish a message draft for "
           "clarity and tone.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"text\":{"
           "\"type\":\"string\"}}}},"
           "{\"name\":\"analyze_sentiment\",\"description\":\"Analyze sentiment polarity of text "
           "or the active room transcript.\",\"inputSchema\":{\"type\":\"object\",\"properties\":{"
           "\"text\":{\"type\":\"string\"}}}},"
           "{\"name\":\"run_agent_prompt\",\"description\":\"Execute a natural-language command or "
           "multi-step AI query against the chat client.\",\"inputSchema\":{\"type\":\"object\","
           "\"properties\":{\"prompt\":{\"type\":\"string\"}},\"required\":[\"prompt\"]}}"
           "]";
}

std::optional<std::string> McpDispatcher::handle_line(std::string_view line,
                                                      AgentController& controller) {
    if (line.size() > kMaxLineBytes) {
        return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Request exceeds 65536 bytes"}})";
    }
    const std::string body = trim(line);
    if (body.empty()) {
        return std::nullopt;
    }
    if (body.front() != '{' || body.back() != '}') {
        return R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}})";
    }
    const auto version = extract_json_string(body, "jsonrpc");
    const auto method = extract_json_string(body, "method");
    const auto id = extract_json_raw(body, "id");
    if (!version || *version != "2.0" || !method || method->empty()) {
        const std::string id_out = id.value_or("null");
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
               ",\"error\":{\"code\":-32600,\"message\":\"Invalid JSON-RPC 2.0 request\"}}";
    }
    if (!id && method->starts_with("notifications/")) {
        return std::nullopt;
    }
    const std::string id_out = id.value_or("null");
    if (*method == "initialize") {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
               ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
               "\"serverInfo\":{\"name\":\"socket-chat-mcp\",\"version\":\"0.1.0\",\"aiModel\":\"" +
               json_escape(AiEngine::model_reference()) + "\"}}}";
    }
    if (*method == "ping") {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out + ",\"result\":{}}";
    }
    if (*method == "tools/list") {
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
               ",\"result\":{\"tools\":" + tools_list_json() + "}}";
    }
    if (*method == "tools/call") {
        const auto params = extract_json_raw(body, "params").value_or("{}");
        const auto name = extract_json_string(params, "name");
        if (!name || name->empty()) {
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
                   ",\"error\":{\"code\":-32602,\"message\":\"Missing tool name\"}}";
        }
        const auto arguments = extract_json_raw(params, "arguments").value_or("{}");
        const auto outcome = controller.call_tool(*name, arguments);
        return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
               ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"" +
               json_escape(outcome.message) + "\"}],\"structured\":" + outcome.json_data +
               ",\"isError\":" + (outcome.ok ? "false" : "true") + "}}";
    }
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id_out +
           ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
}

class LoopbackAgentServer::Impl {
  public:
    struct Peer {
        explicit Peer(boost::asio::io_context& context) : socket(context) {}
        boost::asio::ip::tcp::socket socket;
        std::string rx_buffer;
        std::string tx_buffer;
    };

    explicit Impl(std::uint16_t port)
        : acceptor_(context_, {boost::asio::ip::address_v4::loopback(), port}) {
        acceptor_.non_blocking(true);
        port_ = acceptor_.local_endpoint().port();
    }

    std::uint16_t port() const noexcept { return port_; }

    void poll(AgentController& controller) {
        for (int i = 0; i < 4; ++i) {
            auto candidate = std::make_unique<Peer>(context_);
            boost::system::error_code ec;
            static_cast<void>(acceptor_.accept(candidate->socket, ec));
            if (ec) {
                break;
            }
            if (peers_.size() >= 4) {
                boost::system::error_code ignored;
                static_cast<void>(candidate->socket.close(ignored));
                continue;
            }
            static_cast<void>(candidate->socket.non_blocking(true, ec));
            if (!ec) {
                peers_.push_back(std::move(candidate));
            }
        }
        std::array<char, 4096> chunk{};
        for (auto it = peers_.begin(); it != peers_.end();) {
            auto& peer = **it;
            bool closed = false;
            for (int reads = 0; reads < 8; ++reads) {
                boost::system::error_code ec;
                const auto n = peer.socket.read_some(boost::asio::buffer(chunk), ec);
                if (n > 0) {
                    peer.rx_buffer.append(chunk.data(), n);
                    if (peer.rx_buffer.size() > McpDispatcher::kMaxLineBytes * 2) {
                        closed = true;
                        break;
                    }
                }
                if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                    break;
                }
                if (ec) {
                    closed = true;
                    break;
                }
            }
            std::size_t newline = std::string::npos;
            while (!closed && (newline = peer.rx_buffer.find('\n')) != std::string::npos) {
                std::string line = peer.rx_buffer.substr(0, newline);
                peer.rx_buffer.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (auto reply = McpDispatcher::handle_line(line, controller)) {
                    peer.tx_buffer += *reply;
                    peer.tx_buffer.push_back('\n');
                }
            }
            while (!peer.tx_buffer.empty()) {
                boost::system::error_code ec;
                const auto written =
                    peer.socket.write_some(boost::asio::buffer(peer.tx_buffer), ec);
                if (written > 0) {
                    peer.tx_buffer.erase(0, written);
                }
                if (ec == boost::asio::error::would_block || ec == boost::asio::error::try_again) {
                    break;
                }
                if (ec) {
                    closed = true;
                    break;
                }
            }
            if (closed && peer.tx_buffer.empty()) {
                boost::system::error_code ignored;
                static_cast<void>(peer.socket.close(ignored));
                it = peers_.erase(it);
            } else {
                ++it;
            }
        }
    }

  private:
    boost::asio::io_context context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::uint16_t port_ = 0;
    std::vector<std::unique_ptr<Peer>> peers_;
};

LoopbackAgentServer::LoopbackAgentServer(std::uint16_t port)
    : impl_(std::make_unique<Impl>(port)) {}
LoopbackAgentServer::~LoopbackAgentServer() = default;
std::uint16_t LoopbackAgentServer::port() const noexcept { return impl_->port(); }
void LoopbackAgentServer::poll(AgentController& controller) { impl_->poll(controller); }
} // namespace chat::ai
