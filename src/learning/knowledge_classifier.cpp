module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <ranges>
#include <string>
#include <string_view>

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

} // namespace

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
