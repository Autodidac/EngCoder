#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::learning {

enum class ExpertState {
    memory_only,
    lora_incubating,
    lora_active,
    consolidating,
    full_rank_active,
    rejected,
};

struct ExpertModule final {
    std::string id;
    std::string skill;
    std::string base_model;
    ExpertState state{ExpertState::memory_only};
    std::filesystem::path adapter_path;
    std::filesystem::path module_path;
    ExpertState previous_state{ExpertState::memory_only};
    std::filesystem::path previous_adapter_path;
    std::filesystem::path previous_module_path;
    std::string promoted_candidate_id;
    std::uint64_t generation{0};
    std::size_t accepted_episodes{0};
    std::size_t episodes_since_update{0};
    std::size_t lora_generations{0};
    std::size_t compiler_verified_episodes{0};
    std::size_t test_verified_episodes{0};
    double validation_score{0.0};
    double regression_score{0.0};
    std::int64_t updated_unix_ms{0};
};

struct PromotionPolicy final {
    std::size_t lora_microbatch{4};
    std::size_t consolidate_after_episodes{64};
    std::size_t consolidate_after_generations{4};
    std::size_t minimum_compiler_verified_for_code{8};
    std::size_t minimum_test_verified_for_code{4};
    double minimum_episode_score{0.82};
    double minimum_validation_score{0.86};
    double maximum_regression{0.03};
};

class ExpertRegistry final {
public:
    explicit ExpertRegistry(std::filesystem::path directory);

    void load();
    void save(const ExpertModule& module) const;
    [[nodiscard]] ExpertModule& ensure(
        std::string_view skill,
        std::string_view base_model);
    [[nodiscard]] ExpertModule* find(std::string_view skill) noexcept;
    [[nodiscard]] const ExpertModule* find(std::string_view skill) const noexcept;
    [[nodiscard]] const std::vector<ExpertModule>& modules() const noexcept;

    [[nodiscard]] bool ready_for_lora(
        const ExpertModule& module,
        const PromotionPolicy& policy) const noexcept;
    [[nodiscard]] bool ready_for_consolidation(
        const ExpertModule& module,
        const PromotionPolicy& policy) const noexcept;

private:
    std::filesystem::path directory_;
    std::vector<ExpertModule> modules_;
};

[[nodiscard]] std::string_view to_string(ExpertState state) noexcept;
[[nodiscard]] ExpertState expert_state_from_string(std::string_view value);
[[nodiscard]] std::string normalize_skill(std::string_view skill);

} // namespace threadspire::ai::learning
