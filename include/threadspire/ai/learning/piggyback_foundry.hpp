#pragma once

#include "threadspire/ai/learning/continual_learning_service.hpp"
#include "threadspire/ai/models/model_registry.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace threadspire::ai::learning {

struct PiggybackCompletionResponse final {
    bool succeeded{false};
    std::string content;
    std::string error;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
};

using PiggybackCompletion = std::function<PiggybackCompletionResponse(
    const models::ModelProfile& model,
    std::string_view system_prompt,
    std::string_view prompt,
    std::size_t maximum_output_tokens,
    double temperature,
    std::stop_token stop_token)>;

struct PiggybackRequest final {
    std::filesystem::path seed_dataset;
    std::string target_skill{"english-foundation"};
    bool restrict_to_target_domain{true};
    bool generate_teacher_variations{true};
    bool untrusted_live_examples{false};
    bool measured_gap_analysis{false};
    bool wrap_seed_dataset{true};
    std::string internal_base_url;
    models::ModelProfile teacher;
    std::optional<models::ModelProfile> verifier;
    std::vector<models::ModelProfile> teachers;
    std::vector<models::ModelProfile> verifiers;
    PiggybackCompletion completion;
    std::filesystem::path audit_log;
    std::filesystem::path measured_gap_dataset;
    std::size_t maximum_parallel_per_teacher{4};
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
    std::size_t revised{0};
    std::size_t needs_evidence{0};
    std::size_t retry_pending{0};
    std::size_t excluded{0};
    std::size_t coverage_gaps{0};
    std::size_t deep_gap_verified{0};
    std::vector<std::string> accepted_source_models;
    std::vector<std::string> rejected_source_models;
    std::size_t stored{0};
    std::size_t rejected{0};
    std::size_t attempted{0};
    std::size_t teacher_models{0};
    std::size_t maximum_parallel{0};
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
