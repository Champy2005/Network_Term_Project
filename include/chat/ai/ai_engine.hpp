/** @file
 * @brief Provide deterministic TextRank room summarization, VADER-style sentiment analysis, and
 * optional HTTP(S) LLM completion.
 */
#pragma once
#include "chat/client/room_model.hpp"
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chat::ai {
/// @brief Normalized valence polarity in [-1, 1] with positive/neutral/negative proportions.
struct SentimentScore {
    /// @brief Normalized VADER-style compound score in [-1.0, 1.0].
    double compound = 0.0;
    /// @brief Proportion of positive valence weight in [0.0, 1.0].
    double positive = 0.0;
    /// @brief Proportion of neutral tokens in [0.0, 1.0].
    double neutral = 1.0;
    /// @brief Proportion of negative valence weight in [0.0, 1.0].
    double negative = 0.0;
    /// @brief Human-readable polarity classification ("Positive", "Neutral", or "Negative").
    std::string label = "Neutral";
};
/// @brief Extractive TextRank room summary paired with aggregate conversation sentiment.
struct RoomSummary {
    /// @brief Display title or room identifier summarized.
    std::string room_title;
    /// @brief Total room lines analyzed.
    std::size_t message_count = 0;
    /// @brief Top-ranked representative lines in chronological room order.
    std::vector<std::string> key_points;
    /// @brief Aggregate valence across all analyzed room messages.
    SentimentScore overall_sentiment;
    /// @brief Single-string summary suitable for UI/CLI/MCP display.
    std::string formatted;
    /// @brief Model citation used to produce the summary.
    std::string model_id;
};
/// @brief Structured action items, decisions, and open questions extracted from a room transcript.
struct ActionItemsResult {
    /// @brief Display title or room identifier analyzed.
    std::string room_title;
    /// @brief Extracted action items, tasks, or commitments.
    std::vector<std::string> action_items;
    /// @brief Extracted decisions or agreements.
    std::vector<std::string> decisions;
    /// @brief Extracted unanswered or open questions.
    std::vector<std::string> open_questions;
    /// @brief Formatted single-string report suitable for UI/CLI/MCP display.
    std::string formatted;
};
/// @brief Optional OpenAI-compatible HTTP(S) endpoint settings; disabled uses the local C++ engine.
struct LlmEndpointOptions {
    /// @brief False keeps all inference local and deterministic without network calls.
    bool enabled = false;
    /// @brief Remote or local host (for example, 127.0.0.1 for Ollama or api.openai.com).
    std::string host;
    /// @brief Target TCP port (for example, 11434 for Ollama or 443 for HTTPS).
    std::uint16_t port = 443;
    /// @brief HTTP request target path.
    std::string path = "/v1/chat/completions";
    /// @brief Model identifier passed in the JSON request payload.
    std::string model = "gpt-4o-mini";
    /// @brief Optional bearer API key; never logged or persisted.
    std::string api_key;
    /// @brief True uses OpenSSL TLS; false permits loopback HTTP (such as local Ollama).
    bool tls = true;
    /// @brief Bounded connect/request/response deadline.
    std::chrono::milliseconds timeout{3000};

    /// @brief Auto-detect Gemini, OpenAI, or Ollama endpoint settings from environment or `.env`.
    [[nodiscard]] static LlmEndpointOptions from_environment();
};
/// @brief GUI-independent NLP and hybrid LLM engine for room summarization and sentiment analysis.
class AiEngine {
  public:
    /// @brief Academic model citation for the built-in extractive summarizer and sentiment analyzer.
    [[nodiscard]] static constexpr std::string_view model_reference() noexcept {
        return "TextRank (Mihalcea & Tarau, EMNLP 2004) + VADER Lexicon (Hutto & Gilbert, ICWSM "
               "2014)";
    }
    /// @brief Compute VADER-inspired valence sentiment with negation and booster rules.
    [[nodiscard]] static SentimentScore analyze_sentiment(std::string_view text);
    /// @brief Rank transcript lines by TF-IDF cosine TextRank centrality and format a room summary.
    [[nodiscard]] static RoomSummary summarize_lines(std::string_view room_title,
                                                     const std::deque<client::ChatLine>& lines,
                                                     std::size_t max_sentences = 3);
    /// @brief Extract action items, decisions, and open questions from a room transcript.
    [[nodiscard]] static ActionItemsResult
    extract_action_items(std::string_view room_title, const std::deque<client::ChatLine>& lines);
    /// @brief Rewrite and polish a message draft using an optional LLM or deterministic C++ rules.
    [[nodiscard]] static std::string polish_draft(std::string_view draft,
                                                  const LlmEndpointOptions& llm = {});
    /// @brief Suggest a context-aware reply to the latest conversation in a room.
    [[nodiscard]] static std::string
    suggest_smart_reply(std::string_view self_username, std::string_view room_title,
                        const std::deque<client::ChatLine>& lines,
                        const LlmEndpointOptions& llm = {});
    /// @brief Query an optional OpenAI-compatible endpoint; returns nullopt when disabled or unreachable.
    [[nodiscard]] static std::optional<std::string>
    query_llm_endpoint(const LlmEndpointOptions& options, std::string_view system_prompt,
                       std::string_view user_prompt);
};
} // namespace chat::ai
