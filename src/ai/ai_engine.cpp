/** @file
 * @brief Implement TextRank extractive summarization, VADER-inspired sentiment scoring, and
 * optional bounded HTTP(S) LLM completion.
 */
#include "chat/ai/ai_engine.hpp"
#include <algorithm>
#include <array>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <numeric>
#include <openssl/ssl.h>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace chat::ai {
namespace {
struct AnalyzedToken {
    std::string lower;
    bool all_caps = false;
};
bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_ascii_alnum(char c) { return is_ascii_alpha(c) || (c >= '0' && c <= '9'); }
std::vector<AnalyzedToken> tokenize_for_sentiment(std::string_view text) {
    std::vector<AnalyzedToken> tokens;
    std::string current;
    bool has_upper = false;
    bool has_lower = false;
    const auto flush = [&] {
        if (!current.empty()) {
            tokens.push_back({std::move(current), has_upper && !has_lower && current.size() >= 2});
            current.clear();
            has_upper = false;
            has_lower = false;
        }
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto ch = static_cast<unsigned char>(text[i]);
        if (is_ascii_alnum(static_cast<char>(ch)) || ch == '\'' || ch >= 0x80U) {
            if (ch >= 'A' && ch <= 'Z') {
                has_upper = true;
                current.push_back(static_cast<char>(ch - 'A' + 'a'));
            } else {
                if (ch >= 'a' && ch <= 'z') {
                    has_lower = true;
                }
                current.push_back(static_cast<char>(ch));
            }
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}
const std::unordered_map<std::string_view, double>& sentiment_lexicon() {
    static const std::unordered_map<std::string_view, double> kLexicon{
        {"excellent", 3.2},   {"awesome", 3.1},    {"fantastic", 3.0},   {"wonderful", 2.9},
        {"brilliant", 2.9},   {"love", 2.8},       {"perfect", 2.8},     {"great", 2.6},
        {"best", 2.5},        {"happy", 2.4},      {"excited", 2.3},     {"glad", 2.1},
        {"good", 1.9},        {"helpful", 1.9},    {"success", 2.2},     {"successful", 2.2},
        {"passed", 1.8},      {"pass", 1.6},       {"working", 1.5},     {"works", 1.6},
        {"fixed", 1.8},       {"resolved", 1.9},   {"secure", 1.7},      {"reliable", 1.8},
        {"fast", 1.5},        {"clean", 1.5},      {"smooth", 1.6},      {"thanks", 1.9},
        {"thank", 1.8},       {"appreciate", 2.0}, {"ready", 1.4},       {"nice", 1.8},
        {"cool", 1.5},        {"agree", 1.4},      {"approved", 1.8},    {"approve", 1.7},
        {"clear", 1.3},       {"easy", 1.5},       {"welcome", 1.7},     {"yay", 2.2},
        {"\xF0\x9F\x91\x8D", 2.3},                 // 👍
        {"\xE2\x9C\x85", 2.0},                     // ✅
        {"\xF0\x9F\x8E\x89", 2.5},                 // 🎉
        {"worst", -3.2},      {"terrible", -3.0},  {"horrible", -3.0},   {"awful", -2.9},
        {"hate", -2.8},       {"broken", -2.5},    {"crashed", -2.6},    {"crash", -2.5},
        {"failed", -2.4},     {"failure", -2.4},   {"fail", -2.2},       {"error", -2.1},
        {"bug", -2.0},        {"bad", -2.2},       {"sad", -2.1},        {"angry", -2.4},
        {"rejected", -2.0},   {"denied", -2.0},    {"timeout", -1.8},    {"slow", -1.5},
        {"wrong", -1.9},      {"issue", -1.5},     {"problem", -1.8},    {"stuck", -1.7},
        {"annoying", -2.1},   {"frustrating", -2.3}, {"confusing", -1.6}, {"missing", -1.5},
        {"lost", -1.6},       {"disconnected", -1.5}, {"unsafe", -2.2},  {"vulnerable", -2.3},
        {"\xF0\x9F\x91\x8E", -2.3},                // 👎
        {"\xE2\x9D\x8C", -2.1}                     // ❌
    };
    return kLexicon;
}
bool is_negation(std::string_view token) {
    static const std::unordered_set<std::string_view> kNegations{
        "not",    "no",      "never",   "neither", "cannot", "cant",    "can't",
        "dont",   "don't",   "doesnt",  "doesn't", "didnt",  "didn't",  "wont",
        "won't",  "isnt",    "isn't",   "arent",   "aren't", "wasnt",   "wasn't",
        "werent", "weren't", "without", "hardly",  "barely", "shouldnt", "shouldn't"};
    return kNegations.contains(token);
}
double booster_delta(std::string_view token) {
    static const std::unordered_map<std::string_view, double> kBoosters{
        {"very", 0.293},       {"extremely", 0.35}, {"really", 0.293}, {"super", 0.293},
        {"highly", 0.293},     {"completely", 0.293}, {"absolutely", 0.35}, {"totally", 0.293},
        {"so", 0.25},          {"slightly", -0.293}, {"somewhat", -0.25}, {"kinda", -0.25}};
    const auto found = kBoosters.find(token);
    return found == kBoosters.end() ? 0.0 : found->second;
}
bool is_stop_word(std::string_view token) {
    static const std::unordered_set<std::string_view> kStopWords{
        "a",    "an",   "and",  "are",  "as",   "at",   "be",   "by",   "for",  "from",
        "has",  "he",   "in",   "is",   "it",   "its",  "of",   "on",   "or",   "that",
        "the",  "to",   "was",  "were", "will", "with", "i",    "you",  "we",   "they",
        "this", "but",  "have", "had",  "do",   "does", "did",  "so",   "if",   "can"};
    return token.size() <= 1 || kStopWords.contains(token);
}
std::vector<std::string> content_terms(std::string_view text) {
    std::vector<std::string> terms;
    for (auto& token : tokenize_for_sentiment(text)) {
        if (!is_stop_word(token.lower)) {
            terms.push_back(std::move(token.lower));
        }
    }
    return terms;
}
std::string escape_json_string(std::string_view input) {
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
            if (ch >= 32) {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}
} // namespace

SentimentScore AiEngine::analyze_sentiment(std::string_view text) {
    const auto tokens = tokenize_for_sentiment(text);
    if (tokens.empty()) {
        return {};
    }
    const auto& lexicon = sentiment_lexicon();
    std::vector<double> sentiments;
    sentiments.reserve(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const auto found = lexicon.find(tokens[i].lower);
        if (found == lexicon.end()) {
            sentiments.push_back(0.0);
            continue;
        }
        double valence = found->second;
        if (tokens[i].all_caps) {
            valence += (valence > 0.0 ? 0.733 : -0.733);
        }
        const std::size_t window_start = i >= 3 ? i - 3 : 0;
        bool negated = false;
        for (std::size_t back = window_start; back < i; ++back) {
            const double boost = booster_delta(tokens[back].lower);
            if (boost != 0.0) {
                const double scale = (i - back == 1) ? 1.0 : (i - back == 2 ? 0.95 : 0.9);
                valence += (valence > 0.0 ? boost : -boost) * scale;
            }
            if (is_negation(tokens[back].lower)) {
                negated = !negated;
            }
        }
        if (negated) {
            valence *= -0.74;
        }
        sentiments.push_back(valence);
    }
    double sum = std::accumulate(sentiments.begin(), sentiments.end(), 0.0);
    const auto exclamations =
        std::min<std::size_t>(4, static_cast<std::size_t>(std::count(text.begin(), text.end(), '!')));
    if (exclamations > 0 && sum != 0.0) {
        const double emphasis = static_cast<double>(exclamations) * 0.292;
        sum += (sum > 0.0 ? emphasis : -emphasis);
    }
    double pos_sum = 0.0;
    double neg_sum = 0.0;
    std::size_t neu_count = 0;
    for (const double v : sentiments) {
        if (v > 0.0) {
            pos_sum += v + 1.0;
        } else if (v < 0.0) {
            neg_sum += std::abs(v) + 1.0;
        } else {
            ++neu_count;
        }
    }
    const double total = pos_sum + neg_sum + static_cast<double>(neu_count);
    SentimentScore result;
    result.compound = std::clamp(sum / std::sqrt(sum * sum + 15.0), -1.0, 1.0);
    if (total > 0.0) {
        result.positive = pos_sum / total;
        result.negative = neg_sum / total;
        result.neutral = static_cast<double>(neu_count) / total;
    }
    result.label = result.compound >= 0.05    ? "Positive"
                   : result.compound <= -0.05 ? "Negative"
                                              : "Neutral";
    return result;
}

RoomSummary AiEngine::summarize_lines(std::string_view room_title,
                                      const std::deque<client::ChatLine>& lines,
                                      std::size_t max_sentences) {
    RoomSummary summary;
    summary.room_title = std::string(room_title.empty() ? "Room" : room_title);
    summary.message_count = lines.size();
    summary.model_id = std::string(model_reference());
    max_sentences = std::clamp<std::size_t>(max_sentences, 1, 10);
    if (lines.empty()) {
        summary.formatted =
            "AI Summary (" + summary.room_title + "): No messages available to summarize.";
        return summary;
    }
    std::string combined_text;
    const std::size_t n = lines.size();
    std::vector<std::map<std::string, double>> tf(n);
    std::map<std::string, std::size_t> df;
    for (std::size_t i = 0; i < n; ++i) {
        combined_text += lines[i].text;
        combined_text.push_back(' ');
        const auto terms = content_terms(lines[i].text);
        std::set<std::string> unique_in_line;
        for (const auto& term : terms) {
            tf[i][term] += 1.0;
            unique_in_line.insert(term);
        }
        for (const auto& term : unique_in_line) {
            df[term] += 1;
        }
    }
    summary.overall_sentiment = analyze_sentiment(combined_text);
    std::vector<std::map<std::string, double>> tfidf(n);
    std::vector<double> norms(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        double sq_sum = 0.0;
        for (const auto& [term, count] : tf[i]) {
            const double idf =
                std::log(1.0 + static_cast<double>(n) / (1.0 + static_cast<double>(df[term]))) +
                1.0;
            const double weight = count * idf;
            tfidf[i][term] = weight;
            sq_sum += weight * weight;
        }
        norms[i] = std::sqrt(sq_sum);
    }
    std::vector<std::vector<double>> sim(n, std::vector<double>(n, 0.0));
    std::vector<double> row_sums(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (norms[i] == 0.0 || norms[j] == 0.0) {
                continue;
            }
            double dot = 0.0;
            for (const auto& [term, w_i] : tfidf[i]) {
                const auto it = tfidf[j].find(term);
                if (it != tfidf[j].end()) {
                    dot += w_i * it->second;
                }
            }
            const double cosine = dot / (norms[i] * norms[j]);
            sim[i][j] = cosine;
            sim[j][i] = cosine;
            row_sums[i] += cosine;
            row_sums[j] += cosine;
        }
    }
    constexpr double kDamping = 0.85;
    std::vector<double> ranks(n, 1.0 / static_cast<double>(n));
    std::vector<double> next_ranks(n, 0.0);
    for (int iter = 0; iter < 30; ++iter) {
        for (std::size_t i = 0; i < n; ++i) {
            double incoming = 0.0;
            for (std::size_t j = 0; j < n; ++j) {
                if (i != j && row_sums[j] > 1e-9) {
                    incoming += (sim[j][i] / row_sums[j]) * ranks[j];
                }
            }
            next_ranks[i] = (1.0 - kDamping) / static_cast<double>(n) + kDamping * incoming;
        }
        ranks = next_ranks;
    }
    std::vector<std::size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        if (std::abs(ranks[a] - ranks[b]) > 1e-7) {
            return ranks[a] > ranks[b];
        }
        if (std::abs(norms[a] - norms[b]) > 1e-7) {
            return norms[a] > norms[b];
        }
        return a < b;
    });
    indices.resize(std::min(max_sentences, n));
    std::sort(indices.begin(), indices.end());
    for (const std::size_t idx : indices) {
        summary.key_points.push_back(lines[idx].sender + ": " + lines[idx].text);
    }
    std::ostringstream out;
    out << "AI Summary (" << summary.room_title << ", " << summary.message_count
        << (summary.message_count == 1 ? " msg" : " msgs")
        << ", sentiment=" << summary.overall_sentiment.label << " [" << std::fixed
        << std::setprecision(2) << summary.overall_sentiment.compound << "]): ";
    for (std::size_t i = 0; i < summary.key_points.size(); ++i) {
        if (i > 0) {
            out << " | ";
        }
        out << "(" << (i + 1) << ") " << summary.key_points[i];
    }
    summary.formatted = out.str();
    return summary;
}

namespace {
std::string trim_text(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}
std::string to_lower_copy(std::string_view input) {
    std::string out(input);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}
std::string read_env_var(const char* name) {
#if defined(_WIN32)
    char* buf = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        std::string val(buf);
        std::free(buf);
        return trim_text(val);
    }
    return {};
#else
    const char* raw = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
    return raw ? trim_text(raw) : std::string{};
#endif
}
std::unordered_map<std::string, std::string> read_dotenv_file() {
    std::unordered_map<std::string, std::string> env;
    std::ifstream in(".env");
    if (!in) {
        return env;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim_text(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        std::string key = trim_text(trimmed.substr(0, eq));
        std::string val = trim_text(trimmed.substr(eq + 1));
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }
        env[std::move(key)] = std::move(val);
    }
    return env;
}
} // namespace

LlmEndpointOptions LlmEndpointOptions::from_environment() {
    const auto dotenv = read_dotenv_file();
    auto lookup = [&](const char* key) -> std::string {
        std::string v = read_env_var(key);
        if (!v.empty()) {
            return v;
        }
        const auto it = dotenv.find(key);
        return it == dotenv.end() ? std::string{} : it->second;
    };
    LlmEndpointOptions opts;
    const std::string gemini_key = lookup("GEMINI_API_KEY");
    const std::string openai_key = lookup("OPENAI_API_KEY");
    const std::string chat_key = lookup("CHAT_AI_KEY");
    const std::string ollama_host = lookup("OLLAMA_HOST");
    const std::string custom_model = lookup("CHAT_AI_MODEL");

    if (!gemini_key.empty() || (chat_key.starts_with("AIza"))) {
        opts.enabled = true;
        opts.host = "generativelanguage.googleapis.com";
        opts.port = 443;
        opts.path = "/v1beta/openai/chat/completions";
        opts.model = custom_model.empty() ? "gemini-2.5-flash" : custom_model;
        opts.api_key = !gemini_key.empty() ? gemini_key : chat_key;
        opts.tls = true;
        return opts;
    }
    if (!openai_key.empty() || !chat_key.empty()) {
        opts.enabled = true;
        opts.host = "api.openai.com";
        opts.port = 443;
        opts.path = "/v1/chat/completions";
        opts.model = custom_model.empty() ? "gpt-4o-mini" : custom_model;
        opts.api_key = !openai_key.empty() ? openai_key : chat_key;
        opts.tls = true;
        return opts;
    }
    if (!ollama_host.empty()) {
        opts.enabled = true;
        opts.host = ollama_host;
        opts.port = 11434;
        opts.path = "/v1/chat/completions";
        opts.model = custom_model.empty() ? "llama3.2" : custom_model;
        opts.tls = false;
    }
    return opts;
}

ActionItemsResult AiEngine::extract_action_items(std::string_view room_title,
                                                 const std::deque<client::ChatLine>& lines) {
    ActionItemsResult res;
    res.room_title = std::string(room_title.empty() ? "Room" : room_title);
    if (lines.empty()) {
        res.formatted =
            "AI Action Items (" + res.room_title + "): No messages available to analyze.";
        return res;
    }
    constexpr std::array<std::string_view, 16> kActionMarkers = {
        "todo",     "to-do",     "action:",   "need to ",   "needs to ", "must ",
        "should ",  "will ",     "i'll ",     "we'll ",     "please ",   "by tomorrow",
        "by friday","deadline",  "assign",    "follow up"};
    constexpr std::array<std::string_view, 9> kDecisionMarkers = {
        "decided",  "agreed",    "approved",  "resolved",   "confirmed",
        "settled",  "let's go",  "we will use", "consensus"};

    for (const auto& line : lines) {
        const std::string text = trim_text(line.text);
        if (text.empty()) {
            continue;
        }
        const std::string lower = to_lower_copy(text);
        const std::string entry = line.sender + ": " + text;

        if (text.find('?') != std::string::npos && res.open_questions.size() < 5) {
            res.open_questions.push_back(entry);
        }
        for (const auto marker : kDecisionMarkers) {
            if (lower.find(marker) != std::string::npos && res.decisions.size() < 5) {
                res.decisions.push_back(entry);
                break;
            }
        }
        for (const auto marker : kActionMarkers) {
            if (lower.find(marker) != std::string::npos && res.action_items.size() < 6) {
                res.action_items.push_back(entry);
                break;
            }
        }
    }
    std::ostringstream out;
    out << "AI Insights (" << res.room_title << ") — ";
    out << "Actions(" << res.action_items.size() << "): ";
    if (res.action_items.empty()) {
        out << "none";
    } else {
        for (std::size_t i = 0; i < res.action_items.size(); ++i) {
            if (i > 0) {
                out << " ; ";
            }
            out << "[" << (i + 1) << "] " << res.action_items[i];
        }
    }
    if (!res.decisions.empty()) {
        out << " | Decisions(" << res.decisions.size() << "): ";
        for (std::size_t i = 0; i < res.decisions.size(); ++i) {
            if (i > 0) {
                out << " ; ";
            }
            out << res.decisions[i];
        }
    }
    if (!res.open_questions.empty()) {
        out << " | Open Questions(" << res.open_questions.size() << "): ";
        for (std::size_t i = 0; i < res.open_questions.size(); ++i) {
            if (i > 0) {
                out << " ; ";
            }
            out << res.open_questions[i];
        }
    }
    res.formatted = out.str();
    return res;
}

std::string AiEngine::polish_draft(std::string_view draft, const LlmEndpointOptions& llm) {
    const std::string cleaned = trim_text(draft);
    if (cleaned.empty()) {
        return {};
    }
    if (auto reply = query_llm_endpoint(
            llm,
            "Rewrite the user's chat message draft so it is clear, natural, polite, and concise. "
            "Return ONLY the rewritten message text without quotes.",
            cleaned)) {
        const std::string trimmed_reply = trim_text(*reply);
        if (!trimmed_reply.empty()) {
            return trimmed_reply;
        }
    }
    // Deterministic C++ grammar, shorthand expansion, capitalization, and punctuation polish
    static const std::vector<std::pair<std::string_view, std::string_view>> kReplacements = {
        {"pls", "please"},     {"plz", "please"},     {"thx", "thanks"},
        {"ty", "thank you"},   {"u", "you"},          {"ur", "your"},
        {"r", "are"},          {"btw", "by the way"}, {"imo", "in my opinion"},
        {"idk", "I'm not sure"},{"lmk", "let me know"},{"asap", "as soon as possible"},
        {"w/", "with"},        {"w/o", "without"},    {"im", "I'm"},
        {"ive", "I've"},       {"id", "I'd"},         {"dont", "don't"},
        {"cant", "can't"},     {"wont", "won't"},     {"isnt", "isn't"}};

    std::istringstream iss(cleaned);
    std::string word;
    std::string out;
    while (iss >> word) {
        std::string prefix_punct;
        std::string suffix_punct;
        while (!word.empty() && !is_ascii_alnum(word.front()) && word.front() != '\'') {
            prefix_punct.push_back(word.front());
            word.erase(word.begin());
        }
        while (!word.empty() && !is_ascii_alnum(word.back()) && word.back() != '\'') {
            suffix_punct.insert(suffix_punct.begin(), word.back());
            word.pop_back();
        }
        const std::string lower_w = to_lower_copy(word);
        std::string replaced = word;
        if (lower_w == "i") {
            replaced = "I";
        } else {
            for (const auto& [abbr, full] : kReplacements) {
                if (lower_w == abbr) {
                    replaced = std::string(full);
                    break;
                }
            }
        }
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += prefix_punct;
        out += replaced;
        out += suffix_punct;
    }
    if (!out.empty() && out.front() >= 'a' && out.front() <= 'z') {
        out.front() = static_cast<char>(out.front() - 'a' + 'A');
    }
    if (!out.empty()) {
        const char back = out.back();
        if (back != '.' && back != '!' && back != '?') {
            const std::string lower_out = to_lower_copy(out);
            const bool is_q = lower_out.starts_with("can ") || lower_out.starts_with("could ") ||
                              lower_out.starts_with("what ") || lower_out.starts_with("when ") ||
                              lower_out.starts_with("where ") || lower_out.starts_with("how ") ||
                              lower_out.starts_with("why ") || lower_out.starts_with("who ") ||
                              lower_out.starts_with("are ") || lower_out.starts_with("is ");
            out.push_back(is_q ? '?' : '.');
        }
    }
    return out;
}

std::string AiEngine::suggest_smart_reply(std::string_view self_username,
                                          std::string_view room_title,
                                          const std::deque<client::ChatLine>& lines,
                                          const LlmEndpointOptions& llm) {
    if (lines.empty()) {
        return "Hey everyone! How's it going?";
    }
    if (llm.enabled) {
        std::ostringstream ctx;
        ctx << "Room: " << room_title << " (You are "
            << (self_username.empty() ? "User" : self_username) << ")\nRecent messages:\n";
        const std::size_t start = lines.size() > 8 ? lines.size() - 8 : 0;
        for (std::size_t i = start; i < lines.size(); ++i) {
            ctx << lines[i].sender << ": " << lines[i].text << "\n";
        }
        if (auto reply = query_llm_endpoint(
                llm,
                "Write a natural, helpful 1-sentence chat reply for the user based on the recent "
                "messages. Return ONLY the reply text without quotes or sender prefix.",
                ctx.str())) {
            const std::string trimmed = trim_text(*reply);
            if (!trimmed.empty()) {
                return trimmed;
            }
        }
    }
    // Find the last message not from self_username if possible
    const client::ChatLine* target = &lines.back();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it->sender != self_username) {
            target = &(*it);
            break;
        }
    }
    const std::string lower = to_lower_copy(target->text);
    const auto s = analyze_sentiment(target->text);
    if (lower.find("hello") != std::string::npos || lower.starts_with("hi") ||
        lower.starts_with("hey") || lower.find("welcome") != std::string::npos) {
        return "Hey " + target->sender + "! Great to connect here.";
    }
    if (target->text.find('?') != std::string::npos) {
        if (lower.find("when") != std::string::npos || lower.find("time") != std::string::npos) {
            return "Good question, " + target->sender +
                   " — I'm free right now if that works for you!";
        }
        if (lower.find("ready") != std::string::npos || lower.find("status") != std::string::npos) {
            return "Yes, everything looks ready on my side, " + target->sender + "!";
        }
        return "Good question, " + target->sender + " — let me check on that and follow up!";
    }
    if (s.label == "Negative") {
        return "Thanks for flagging that, " + target->sender +
               ". Let's look into it and get it fixed.";
    }
    if (lower.find("thanks") != std::string::npos || lower.find("thank") != std::string::npos) {
        return "You're very welcome, " + target->sender + "! Happy to help.";
    }
    return "Sounds good, " + target->sender + "! Thanks for the update.";
}

std::optional<std::string> AiEngine::query_llm_endpoint(const LlmEndpointOptions& options,
                                                        std::string_view system_prompt,
                                                        std::string_view user_prompt) {
    if (!options.enabled || options.host.empty() || options.port == 0 ||
        options.timeout.count() <= 0) {
        return std::nullopt;
    }
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver(io);
        boost::asio::steady_timer timer(io, options.timeout);
        std::ostringstream body;
        body << "{\"model\":\"" << escape_json_string(options.model) << "\",\"messages\":["
             << "{\"role\":\"system\",\"content\":\"" << escape_json_string(system_prompt) << "\"},"
             << "{\"role\":\"user\",\"content\":\"" << escape_json_string(user_prompt)
             << "\"}],\"max_tokens\":320}";
        const std::string payload = body.str();
        std::ostringstream req;
        req << "POST " << (options.path.empty() ? "/v1/chat/completions" : options.path)
            << " HTTP/1.1\r\n"
            << "Host: " << options.host << "\r\n"
            << "Content-Type: application/json\r\n"
            << "Connection: close\r\n";
        if (!options.api_key.empty()) {
            req << "Authorization: Bearer " << options.api_key << "\r\n";
        }
        req << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        const std::string raw_request = req.str();
        std::string response_text;
        const auto endpoints = resolver.resolve(options.host, std::to_string(options.port));

        if (options.tls) {
            boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tlsv12_client);
            ssl_ctx.set_default_verify_paths();
            boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream(io, ssl_ctx);
            static_cast<void>(
                SSL_set_tlsext_host_name(stream.native_handle(), options.host.c_str()));
            timer.async_wait([&](const boost::system::error_code& ec) {
                if (!ec) {
                    boost::system::error_code ignored;
                    static_cast<void>(stream.lowest_layer().close(ignored));
                }
            });
            boost::asio::connect(stream.lowest_layer(), endpoints);
            stream.handshake(boost::asio::ssl::stream_base::client);
            boost::asio::write(stream, boost::asio::buffer(raw_request));
            boost::system::error_code read_ec;
            std::array<char, 4096> buffer{};
            while (response_text.size() < 65536) {
                const auto n = stream.read_some(boost::asio::buffer(buffer), read_ec);
                if (n > 0) {
                    response_text.append(buffer.data(), n);
                }
                if (read_ec) {
                    break;
                }
            }
            timer.cancel();
        } else {
            boost::asio::ip::tcp::socket socket(io);
            timer.async_wait([&](const boost::system::error_code& ec) {
                if (!ec) {
                    boost::system::error_code ignored;
                    static_cast<void>(socket.close(ignored));
                }
            });
            boost::asio::connect(socket, endpoints);
            boost::asio::write(socket, boost::asio::buffer(raw_request));
            boost::system::error_code read_ec;
            std::array<char, 4096> buffer{};
            while (response_text.size() < 65536) {
                const auto n = socket.read_some(boost::asio::buffer(buffer), read_ec);
                if (n > 0) {
                    response_text.append(buffer.data(), n);
                }
                if (read_ec) {
                    break;
                }
            }
            timer.cancel();
        }
        const auto content_pos = response_text.find("\"content\"");
        if (content_pos == std::string::npos) {
            return std::nullopt;
        }
        const auto colon = response_text.find(':', content_pos);
        const auto quote_start =
            colon == std::string::npos ? std::string::npos : response_text.find('"', colon);
        if (quote_start == std::string::npos) {
            return std::nullopt;
        }
        std::string extracted;
        for (std::size_t i = quote_start + 1; i < response_text.size(); ++i) {
            if (response_text[i] == '\\' && i + 1 < response_text.size()) {
                ++i;
                extracted.push_back(response_text[i] == 'n' ? '\n' : response_text[i]);
            } else if (response_text[i] == '"') {
                return extracted;
            } else {
                extracted.push_back(response_text[i]);
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace chat::ai
