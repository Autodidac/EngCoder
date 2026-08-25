module;
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

export module threadspire.learning;

export import :state_machine;
export import :promotion;
export import threadspire.models;
export import threadspire.platform;
export import threadspire.storage;

export {
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
    std::uint64_t training_episode_count{0};
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

namespace threadspire::ai::learning {

struct LearningSettings final {
    std::filesystem::path python_executable;
    std::filesystem::path worker_script;
    std::filesystem::path state_root;
    std::filesystem::path seed_dataset;
    std::filesystem::path anchor_dataset;
    std::string base_model{"threadspire://coder"};
    PromotionPolicy policy;
    std::size_t lora_rank{16};
    std::size_t lora_alpha{32};
    std::size_t micro_steps{12};
    std::size_t maximum_sequence_tokens{2048};
    std::size_t checkpoint_steps{3};
    std::chrono::milliseconds inference_preemption_grace{1500};
    double learning_rate{1.0e-4};
    std::size_t full_rank_steps{6};
    std::size_t full_rank_layers_per_pass{1};
    double full_rank_learning_rate{5.0e-6};
    bool require_cuda{true};
    bool automatic_consolidation{true};
    bool resume_interrupted_candidates{true};
    resource::ResourcePolicy resource_policy{};
};

enum class LearningJobState {
    queued,
    waiting_for_resources,
    training_lora,
    training_internal,
    validating,
    consolidating,
    succeeded,
    failed,
    cancelled,
    interrupted,
};

struct LearningJobStatus final {
    std::string id;
    std::string skill;
    std::string candidate_id;
    LearningJobState state{LearningJobState::queued};
    std::string message;
    std::filesystem::path log_path;
    std::filesystem::path progress_path;
    int exit_code{-1};
    std::int64_t updated_unix_ms{0};
};

struct LearningPipelineSnapshot final {
    std::size_t queued_updates{0};
    std::optional<LearningJobStatus> active_job;
};

class ContinualLearningService final {
public:
    explicit ContinualLearningService(LearningSettings settings);
    ~ContinualLearningService();

    ContinualLearningService(const ContinualLearningService&) = delete;
    ContinualLearningService& operator=(const ContinualLearningService&) = delete;

    [[nodiscard]] bool submit(memory::Episode episode);
    void request_skill_update(std::string skill);
    void pause(bool value);
    void set_interactive_busy(bool value);
    void request_stop();
    void set_python_executable(std::filesystem::path path);
    void set_base_model(std::string model);
    [[nodiscard]] bool rollback_skill(std::string_view skill);

    [[nodiscard]] bool paused() const noexcept;
    [[nodiscard]] bool interactive_busy() const noexcept;
    [[nodiscard]] std::vector<LearningJobStatus> statuses() const;
    [[nodiscard]] LearningPipelineSnapshot pipeline_snapshot() const;
    [[nodiscard]] std::vector<ExpertModule> expert_snapshot() const;
    [[nodiscard]] std::vector<CandidateRecord> candidate_snapshot() const;
    [[nodiscard]] LearningSettings settings_snapshot() const;
    [[nodiscard]] const memory::EpisodeStore& memory_store() const noexcept;

private:
    struct LearningRequest final {
        std::string job_id;
        std::string skill;
        std::optional<std::string> candidate_id;
        CandidateKind kind{CandidateKind::lora_overlay};
    };

    void worker_loop(std::stop_token stop_token);
    void process_request(const LearningRequest& request, std::stop_token stop_token);
    [[nodiscard]] bool run_internal_update(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::string_view durable_job_id,
        std::stop_token stop_token);
    [[nodiscard]] bool run_lora_update(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::string_view durable_job_id,
        std::stop_token stop_token);
    [[nodiscard]] bool run_consolidation(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::string_view durable_job_id,
        std::stop_token stop_token);
    void queue_request(LearningRequest request);
    void recover_persisted_requests();
    void recover_interrupted_candidates();
    void transition_durable_phase(
        std::string_view job_id,
        TrainingState expected,
        TrainingState next,
        const std::filesystem::path& checkpoint_path,
        std::string message) noexcept;
    void update_status(const LearningJobStatus& status);
    void persist_status(const LearningJobStatus& status) const;
    void commit_module(const ExpertModule& module);
    void commit_candidate(const CandidateRecord& candidate);
    void publish_control_files() const;

    LearningSettings settings_;
    memory::EpisodeStore memory_store_;
    ExpertRegistry expert_registry_;
    CandidateRegistry candidate_registry_;
    PersistentTrainingQueue persistent_queue_;
    KnowledgeClassifier knowledge_classifier_;
    resource::ResourceGovernor resource_governor_;
    mutable std::mutex promotion_mutex_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::deque<LearningRequest> queue_;
    std::vector<LearningJobStatus> statuses_;
    std::atomic_bool paused_{false};
    std::atomic_uint32_t interactive_leases_{0};
    std::jthread worker_;
};

[[nodiscard]] std::string_view to_string(LearningJobState state) noexcept;

} // namespace threadspire::ai::learning

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
}
