#pragma once

#include "threadspire/ai/memory/episode_store.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace threadspire::ai::learning {

enum class KnowledgeDestination {
    reject,
    episodic_memory,
    retrieval_memory,
    lora_incubation,
};

struct KnowledgeDecision final {
    KnowledgeDestination destination{KnowledgeDestination::reject};
    bool eligible_for_weight_training{false};
    double confidence{0.0};
    std::string reason;
};

class KnowledgeClassifier final {
public:
    [[nodiscard]] KnowledgeDecision classify(
        const memory::Episode& episode,
        std::size_t accepted_skill_examples) const;
};

[[nodiscard]] std::string_view to_string(KnowledgeDestination destination) noexcept;

} // namespace threadspire::ai::learning
