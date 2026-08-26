module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

module threadspire.learning;

namespace threadspire::ai::learning {
namespace {

[[nodiscard]] std::string lower_ascii(const std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] bool contains_any(
    const std::string_view text,
    const std::initializer_list<std::string_view> markers) {
    return std::ranges::any_of(markers, [text](const std::string_view marker) {
        return text.find(marker) != std::string_view::npos;
    });
}

[[nodiscard]] bool unsafe_generated_response(const std::string_view value) {
    auto response = lower_ascii(value);
    const auto first = response.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return true;
    }
    response.erase(0U, first);
    if (response == "-1" || response == "null" || response == "none") {
        return true;
    }
    if (response.starts_with("error | model request") ||
        response.starts_with("error: local model request failed") ||
        response.starts_with("we need to respond as assistant") ||
        response.starts_with("the user said") ||
        contains_any(response, {
            "every eligible local model failed",
            "the selected ai could not produce a usable answer",
            "repetitive or structurally corrupt internal-model draft",
            "private reasoning and returned no final answer",
            "local model server returned no generated content",
            "generation cancelled",
        })) {
        return true;
    }
    return false;
}

struct Utf8Unit final {
    std::uint32_t codepoint{0};
    std::size_t offset{0};
    std::size_t size{0};
    bool valid{false};
};

[[nodiscard]] std::vector<Utf8Unit> decode_utf8(const std::string_view value) {
    std::vector<Utf8Unit> units;
    units.reserve(value.size());
    for (std::size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<unsigned char>(value[offset]);
        std::uint32_t codepoint = first;
        std::size_t size = 1U;
        bool valid = first < 0x80U;
        if (first >= 0xC2U && first <= 0xDFU && offset + 1U < value.size()) {
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            if ((second & 0xC0U) == 0x80U) {
                codepoint = ((first & 0x1FU) << 6U) | (second & 0x3FU);
                size = 2U;
                valid = true;
            }
        } else if (first >= 0xE0U && first <= 0xEFU && offset + 2U < value.size()) {
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            const auto valid_second =
                (second & 0xC0U) == 0x80U &&
                !(first == 0xE0U && second < 0xA0U) &&
                !(first == 0xEDU && second >= 0xA0U);
            if (valid_second && (third & 0xC0U) == 0x80U) {
                codepoint = ((first & 0x0FU) << 12U) |
                            ((second & 0x3FU) << 6U) |
                            (third & 0x3FU);
                size = 3U;
                valid = true;
            }
        } else if (first >= 0xF0U && first <= 0xF4U && offset + 3U < value.size()) {
            const auto second = static_cast<unsigned char>(value[offset + 1U]);
            const auto third = static_cast<unsigned char>(value[offset + 2U]);
            const auto fourth = static_cast<unsigned char>(value[offset + 3U]);
            const auto valid_second =
                (second & 0xC0U) == 0x80U &&
                !(first == 0xF0U && second < 0x90U) &&
                !(first == 0xF4U && second >= 0x90U);
            if (valid_second && (third & 0xC0U) == 0x80U &&
                (fourth & 0xC0U) == 0x80U) {
                codepoint = ((first & 0x07U) << 18U) |
                            ((second & 0x3FU) << 12U) |
                            ((third & 0x3FU) << 6U) |
                            (fourth & 0x3FU);
                size = 4U;
                valid = true;
            }
        }
        units.push_back(Utf8Unit{
            .codepoint = codepoint,
            .offset = offset,
            .size = size,
            .valid = valid,
        });
        offset += size;
    }
    return units;
}

[[nodiscard]] bool is_training_emoji(const std::uint32_t codepoint) noexcept {
    return
        (codepoint >= 0x1F000U && codepoint <= 0x1FFFFU) ||
        (codepoint >= 0x2600U && codepoint <= 0x27BFU) ||
        (codepoint >= 0xE0020U && codepoint <= 0xE007FU) ||
        codepoint == 0x231AU || codepoint == 0x231BU ||
        (codepoint >= 0x23E9U && codepoint <= 0x23F3U) ||
        (codepoint >= 0x23F8U && codepoint <= 0x23FAU) ||
        codepoint == 0x2B50U || codepoint == 0x2B55U ||
        codepoint == 0x3030U || codepoint == 0x303DU ||
        codepoint == 0x3297U || codepoint == 0x3299U;
}

[[nodiscard]] bool is_training_emoji_format(const std::uint32_t codepoint) noexcept {
    return codepoint == 0x200DU || codepoint == 0x20E3U ||
           codepoint == 0xFE0EU || codepoint == 0xFE0FU;
}

[[nodiscard]] bool is_ascii_space(const char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] std::string trim_ascii_space(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool keycap_at(
    const std::vector<Utf8Unit>& units,
    const std::size_t index,
    std::size_t& consumed) noexcept {
    const auto codepoint = units[index].codepoint;
    if (!(codepoint == '#' || codepoint == '*' ||
          (codepoint >= '0' && codepoint <= '9'))) {
        return false;
    }
    auto next = index + 1U;
    if (next < units.size() && units[next].codepoint == 0xFE0FU) {
        ++next;
    }
    if (next < units.size() && units[next].codepoint == 0x20E3U) {
        consumed = next - index + 1U;
        return true;
    }
    return false;
}

[[nodiscard]] bool ascii_word_character(const unsigned char value) noexcept {
    return std::isalnum(value) != 0 || value == '_';
}

void replace_ascii_word(
    std::string& value,
    const std::string_view needle,
    const std::string_view replacement) {
    if (needle.empty()) {
        return;
    }
    auto lowered = lower_ascii(value);
    const auto lowered_needle = lower_ascii(needle);
    for (auto position = lowered.find(lowered_needle);
         position != std::string::npos;
         position = lowered.find(lowered_needle, position + replacement.size())) {
        const auto left_boundary =
            position == 0U ||
            !ascii_word_character(static_cast<unsigned char>(lowered[position - 1U]));
        const auto after = position + lowered_needle.size();
        const auto right_boundary =
            after >= lowered.size() ||
            !ascii_word_character(static_cast<unsigned char>(lowered[after]));
        if (!left_boundary || !right_boundary) {
            continue;
        }
        value.replace(position, needle.size(), replacement);
        lowered.replace(position, lowered_needle.size(), lower_ascii(replacement));
    }
}

[[nodiscard]] std::vector<std::string> source_identity_aliases(
    const std::string_view source_model_id) {
    std::vector<std::string> aliases;
    std::string token;
    const auto keep = [&aliases](std::string candidate) {
        if (candidate.size() < 4U ||
            contains_any(candidate, {
                "local", "model", "studio", "lmstudio", "connected", "internal",
                "assistant", "chat", "coder", "fast", "lane", "server",
            })) {
            return;
        }
        const auto has_letter = std::ranges::any_of(candidate, [](const unsigned char value) {
            return std::isalpha(value) != 0;
        });
        if (has_letter) {
            aliases.push_back(std::move(candidate));
        }
    };
    for (const auto character : lower_ascii(source_model_id)) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            token.push_back(character);
        } else if (!token.empty()) {
            keep(std::move(token));
            token.clear();
        }
    }
    if (!token.empty()) {
        keep(std::move(token));
    }
    return aliases;
}

[[nodiscard]] bool identity_clause(
    const std::string_view lowered_clause,
    const std::vector<std::string>& source_aliases) {
    if (contains_any(lowered_clause, {
            "threadspire", "chatgpt", "openai", "claude", "anthropic", "gemini",
            "google ai", "qwen", "llama", "meta ai", "nemotron", "nvidia",
            "ornith", "deepseek", "mistral", "language model", "ai model",
            "artificial intelligence model",
        })) {
        return true;
    }
    return std::ranges::any_of(source_aliases, [lowered_clause](const std::string& alias) {
        return lowered_clause.find(alias) != std::string_view::npos;
    });
}

[[nodiscard]] std::string normalize_assistant_identity(
    std::string value,
    const std::string_view source_model_id) {
    replace_ascii_word(value, "ThreadSpire", "EngCoder");
    const auto aliases = source_identity_aliases(source_model_id);
    std::string normalized;
    normalized.reserve(value.size());
    std::size_t start = 0U;
    while (start < value.size()) {
        const auto end = value.find_first_of(".!?\n", start);
        const auto extent = end == std::string::npos ? value.size() : end;
        const auto segment = std::string_view(value).substr(start, extent - start);
        const auto lowered_segment = lower_ascii(segment);
        constexpr std::string_view prefixes[]{
            "i am ", "i'm ", "i’m ", "my name is ", "you can call me ",
            "this is ", "as a ", "as an ",
        };
        std::size_t identity_position = std::string::npos;
        bool as_clause = false;
        for (const auto prefix : prefixes) {
            const auto position = lowered_segment.find(prefix);
            if (position != std::string::npos &&
                (identity_position == std::string::npos || position < identity_position)) {
                identity_position = position;
                as_clause = prefix.starts_with("as ");
            }
        }
        if (identity_position != std::string::npos &&
            identity_clause(lowered_segment.substr(identity_position), aliases)) {
            normalized.append(segment.substr(0U, identity_position));
            normalized.append(as_clause ? "As EngCoder" : "I am EngCoder");
        } else {
            normalized.append(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        normalized.push_back(value[end]);
        start = end + 1U;
    }
    return trim_ascii_space(std::move(normalized));
}

} // namespace

std::string sanitize_training_text(const std::string_view value) {
    const auto units = decode_utf8(value);
    std::string result;
    result.reserve(value.size());
    bool pending_gap = false;
    for (std::size_t index = 0U; index < units.size();) {
        std::size_t consumed = 0U;
        const auto keycap = units[index].valid && keycap_at(units, index, consumed);
        const auto removed =
            keycap ||
            (units[index].valid &&
             (is_training_emoji(units[index].codepoint) ||
              is_training_emoji_format(units[index].codepoint)));
        if (removed) {
            if (!result.empty() && !is_ascii_space(result.back())) {
                pending_gap = true;
            }
            index += keycap ? consumed : 1U;
            continue;
        }
        const auto next_is_space =
            units[index].valid && units[index].codepoint < 0x80U &&
            is_ascii_space(static_cast<char>(units[index].codepoint));
        if (pending_gap && !next_is_space && !result.empty() &&
            !is_ascii_space(result.back())) {
            result.push_back(' ');
        }
        pending_gap = false;
        result.append(value.substr(units[index].offset, units[index].size));
        ++index;
    }
    return trim_ascii_space(std::move(result));
}

std::string sanitize_training_response(
    const std::string_view value,
    const std::string_view source_model_id) {
    return normalize_assistant_identity(
        sanitize_training_text(value),
        source_model_id);
}

std::string_view to_string(const KnowledgeDestination destination) noexcept {
    switch (destination) {
    case KnowledgeDestination::reject:
        return "reject";
    case KnowledgeDestination::episodic_memory:
        return "episodic-memory";
    case KnowledgeDestination::retrieval_memory:
        return "retrieval-memory";
    case KnowledgeDestination::lora_incubation:
        return "lora-incubation";
    }
    return "reject";
}

KnowledgeDecision KnowledgeClassifier::classify(
    const memory::Episode& episode,
    const std::size_t accepted_skill_examples) const {
    if (episode.human_rejected) {
        return {
            .destination = KnowledgeDestination::episodic_memory,
            .eligible_for_weight_training = false,
            .confidence = 1.0,
            .reason = "explicit rejection is retained as negative evidence but never positive training",
        };
    }
    if (unsafe_generated_response(episode.response)) {
        return {
            .destination = KnowledgeDestination::reject,
            .eligible_for_weight_training = false,
            .confidence = 1.0,
            .reason = "model errors, corrupt drafts, and private reasoning are never accepted as learning material",
        };
    }

    if (!episode.human_accepted && !episode.deterministic_verified &&
        !episode.reference_verified) {
        return {
            .destination = KnowledgeDestination::episodic_memory,
            .eligible_for_weight_training = false,
            .confidence = 0.98,
            .reason = "unverified model output remains an episode only",
        };
    }
    if (episode.score < 0.70) {
        return {
            .destination = KnowledgeDestination::episodic_memory,
            .eligible_for_weight_training = false,
            .confidence = 0.95,
            .reason = "evidence score is below the positive-learning threshold",
        };
    }

    const auto prompt = lower_ascii(episode.prompt);
    const auto volatile_fact = contains_any(prompt, {
        "today", "current", "latest", "right now", "price", "weather", "news",
        "this week", "this month", "version installed", "branch", "commit",
    });
    const auto personal_or_project_state = contains_any(prompt, {
        "my preference", "i prefer", "remember that", "this repository", "this project",
        "current build", "current file", "local path", "on my machine",
    });
    if (volatile_fact || personal_or_project_state) {
        return {
            .destination = KnowledgeDestination::retrieval_memory,
            .eligible_for_weight_training = false,
            .confidence = 0.90,
            .reason = "volatile or user/project-specific state belongs in retrieval, not weights",
        };
    }

    const auto skill = lower_ascii(episode.skill);
    const auto is_weight_skill =
        skill == "english-core" || skill == "english-foundation" ||
        skill == "english-conversation" ||
        skill == "cpp23-core" || skill == "cpp23-debugging" ||
        skill == "planning";
    if (!is_weight_skill) {
        return {
            .destination = KnowledgeDestination::retrieval_memory,
            .eligible_for_weight_training = false,
            .confidence = 1.0,
            .reason = "only shared English, C++23, debugging, and planning behavior may enter EngCoder weights",
        };
    }

    constexpr std::size_t minimum_diverse_human_approvals = 4U;
    const auto independently_verified = episode.deterministic_verified ||
                                        (episode.reference_verified && episode.score >= 0.86);
    const auto diverse_human_validation =
        episode.human_accepted && episode.score >= 0.88 &&
        accepted_skill_examples >= minimum_diverse_human_approvals;
    if (independently_verified || diverse_human_validation) {
        return {
            .destination = KnowledgeDestination::lora_incubation,
            .eligible_for_weight_training = true,
            .confidence = episode.deterministic_verified ? 0.98 :
                          (episode.reference_verified ? 0.90 : 0.88),
            .reason = "stable behavior has enough independent or diverse human validation to incubate once",
        };
    }

    return {
        .destination = KnowledgeDestination::retrieval_memory,
        .eligible_for_weight_training = false,
        .confidence = 0.78,
        .reason = "use immediately through memory; weight learning waits for four unique approvals or independent verification",
    };
}

} // namespace threadspire::ai::learning
