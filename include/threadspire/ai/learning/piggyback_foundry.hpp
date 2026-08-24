#pragma once

#include "threadspire/ai/learning/continual_learning_service.hpp"
#include "threadspire/ai/models/model_registry.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>

namespace threadspire::ai::learning {

struct PiggybackRequest final {
    std::filesystem::path seed_dataset;
    std::string target_skill{"english-foundation"};
    bool restrict_to_target_domain{true};
    bool generate_teacher_variations{true};
    std::string internal_base_url;
    models::ModelProfile teacher;
    std::optional<models::ModelProfile> verifier;
    std::size_t seed_offset{0};
    std::size_t maximum_examples{16};
    std::size_t maximum_output_tokens{768};
    double minimum_verifier_score{0.86};
};

struct PiggybackProgress final {
    std::string target_skill;
    std::size_t examined{0};
    std::size_t trusted_seeded{0};
    std::size_t generated{0};
    std::size_t verified{0};
    std::size_t stored{0};
    std::size_t rejected{0};
    std::size_t next_seed_offset{0};
    std::string current_prompt;
    std::string message;
};

struct PiggybackResult final {
    bool succeeded{false};
    bool cancelled{false};
    PiggybackProgress progress;
    std::string error;
};

using PiggybackProgressCallback = std::function<void(const PiggybackProgress&)>;

[[nodiscard]] PiggybackResult run_piggyback_foundry(
    const PiggybackRequest& request,
    ContinualLearningService& learning_service,
    const PiggybackProgressCallback& on_progress = {},
    std::stop_token stop_token = {});

} // namespace threadspire::ai::learning
