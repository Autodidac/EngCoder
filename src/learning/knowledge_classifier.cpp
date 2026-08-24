#include "threadspire/ai/learning/knowledge_classifier.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <ranges>
#include <string>
#include <string_view>

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

    const auto strong_evidence = episode.deterministic_verified ||
                                 (episode.reference_verified && episode.score >= 0.86) ||
                                 (episode.human_accepted && episode.score >= 0.88);
    if (strong_evidence && accepted_skill_examples >= 1U) {
        return {
            .destination = KnowledgeDestination::lora_incubation,
            .eligible_for_weight_training = true,
            .confidence = episode.deterministic_verified ? 0.98 :
                          (episode.reference_verified ? 0.90 : 0.88),
            .reason = "stable behavior with independent or explicit human validation can incubate",
        };
    }

    return {
        .destination = KnowledgeDestination::retrieval_memory,
        .eligible_for_weight_training = false,
        .confidence = 0.78,
        .reason = "use immediately through retrieval until more verified evidence accumulates",
    };
}

} // namespace threadspire::ai::learning
