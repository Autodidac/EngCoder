module;
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

export module threadspire.learning:promotion;

export {
namespace threadspire::ai::learning {

struct CandidateHostChecks final {
    bool artifact_checksum_valid{false};
    bool candidate_host_started{false};
    bool inference_canary_passed{false};
    bool deterministic_validation_passed{false};
    bool regression_gate_passed{false};
    std::string diagnostics;

    [[nodiscard]] bool passed() const noexcept;
};

using CandidateHostVerifier = std::function<CandidateHostChecks(
    const std::filesystem::path& candidate_directory)>;

struct CandidatePromotionCommitResult final {
    bool succeeded{false};
    std::string diagnostics;
};

using CandidatePromotionCommit = std::function<CandidatePromotionCommitResult()>;

enum class PromotionOutcome {
    promoted,
    rejected,
    recoverable_failed,
    stale_parent,
};

struct AtomicPromotionRequest final {
    std::filesystem::path active_pointer;
    std::filesystem::path candidate_directory;
    std::string candidate_generation;
    std::string expected_parent_generation;
};

struct AtomicPromotionResult final {
    PromotionOutcome outcome{PromotionOutcome::recoverable_failed};
    std::string active_generation;
    std::string candidate_generation;
    std::string diagnostics;
};

class AtomicPromotionCoordinator final {
public:
    [[nodiscard]] AtomicPromotionResult promote(
        const AtomicPromotionRequest& request,
        const CandidateHostVerifier& verify_candidate_host,
        const CandidatePromotionCommit& commit_candidate = {}) const noexcept;
};

[[nodiscard]] std::string_view to_string(PromotionOutcome outcome) noexcept;

} // namespace threadspire::ai::learning
}