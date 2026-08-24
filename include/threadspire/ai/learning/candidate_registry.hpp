#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::learning {

enum class CandidateKind {
    lora_overlay,
    full_rank_skill,
    routing_policy,
    prompt_policy,
};

enum class CandidateState {
    staged,
    training,
    evaluating,
    approved,
    rejected,
    promoted,
    interrupted,
};

struct CandidateRecord final {
    std::string id;
    std::string skill;
    std::string base_model;
    CandidateKind kind{CandidateKind::lora_overlay};
    CandidateState state{CandidateState::staged};
    std::uint64_t parent_generation{0};
    std::filesystem::path artifact_path;
    std::filesystem::path job_directory;
    std::filesystem::path log_path;
    double validation_score{0.0};
    double regression_score{1.0};
    std::string message;
    std::int64_t created_unix_ms{0};
    std::int64_t updated_unix_ms{0};
};

class CandidateRegistry final {
public:
    explicit CandidateRegistry(std::filesystem::path directory);

    void load();
    [[nodiscard]] CandidateRecord create(
        std::string_view skill,
        std::string_view base_model,
        CandidateKind kind,
        std::uint64_t parent_generation,
        const std::filesystem::path& artifact_path,
        const std::filesystem::path& job_directory,
        const std::filesystem::path& log_path);
    void save(const CandidateRecord& candidate) const;

    [[nodiscard]] CandidateRecord* find(std::string_view id) noexcept;
    [[nodiscard]] const CandidateRecord* find(std::string_view id) const noexcept;
    [[nodiscard]] std::optional<CandidateRecord> resumable_for(
        std::string_view skill,
        CandidateKind kind) const;
    [[nodiscard]] std::vector<CandidateRecord> mark_inflight_interrupted();
    [[nodiscard]] const std::vector<CandidateRecord>& candidates() const noexcept;

private:
    std::filesystem::path directory_;
    std::vector<CandidateRecord> candidates_;
};

[[nodiscard]] std::string_view to_string(CandidateKind kind) noexcept;
[[nodiscard]] std::string_view to_string(CandidateState state) noexcept;
[[nodiscard]] CandidateKind candidate_kind_from_string(std::string_view value) noexcept;
[[nodiscard]] CandidateState candidate_state_from_string(std::string_view value) noexcept;
[[nodiscard]] std::string make_candidate_id(std::string_view skill);

} // namespace threadspire::ai::learning
