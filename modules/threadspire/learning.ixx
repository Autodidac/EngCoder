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
[[nodiscard]] std::string sanitize_training_text(std::string_view value);
[[nodiscard]] std::string sanitize_training_response(
    std::string_view value,
    std::string_view source_model_id);
[[nodiscard]] bool private_or_nonpublic_training_text(std::string_view value);

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
    std::size_t foundation_bootstrap_steps{24'000};
    std::chrono::milliseconds inference_preemption_grace{1500};
    double learning_rate{1.0e-4};
    std::size_t full_rank_steps{6};
    std::size_t full_rank_layers_per_pass{1};
    double full_rank_learning_rate{5.0e-6};
    bool require_cuda{true};
    bool automatic_consolidation{true};
    bool resume_interrupted_candidates{true};
    bool initial_interactive_priority{false};
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

struct LearningSubmissionPreview final {
    bool would_store{false};
    bool duplicate{false};
    KnowledgeDestination destination{KnowledgeDestination::reject};
    bool eligible_for_weight_training{false};
    double confidence{0.0};
    std::size_t accepted_skill_examples{0};
    std::string content_hash;
    std::string reason;
    std::string stored_prompt;
    std::string stored_response;
    bool training_text_adjusted{false};
};


class ContinualLearningService final {
public:
    explicit ContinualLearningService(LearningSettings settings);
    ~ContinualLearningService();

    ContinualLearningService(const ContinualLearningService&) = delete;
    ContinualLearningService& operator=(const ContinualLearningService&) = delete;

    [[nodiscard]] LearningSubmissionPreview preview(memory::Episode episode) const;
    [[nodiscard]] bool submit(memory::Episode episode);
    void request_skill_update(std::string skill);
    void request_foundation_update(
        std::filesystem::path training_dataset,
        std::filesystem::path evaluation_dataset,
        std::size_t bootstrap_target_steps = 0);
    [[nodiscard]] std::size_t request_pending_updates();
    void set_interactive_busy(bool value);
    void request_stop();
    void set_python_executable(std::filesystem::path path);
    void set_base_model(std::string model);
    void set_anchor_dataset(std::filesystem::path path);
    [[nodiscard]] bool bind_untrained_skill_base(
        std::string_view skill,
        std::string_view base_model);
    [[nodiscard]] bool rollback_skill(std::string_view skill);

    [[nodiscard]] bool interactive_busy() const noexcept;
    [[nodiscard]] std::uint32_t interactive_lease_count() const noexcept;
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
        std::filesystem::path training_dataset;
        std::filesystem::path evaluation_dataset;
        std::size_t bootstrap_target_steps{0};
        bool initialize_internal_model{false};
    };

    void worker_loop(std::stop_token stop_token);
    void process_request(const LearningRequest& request, std::stop_token stop_token);
    [[nodiscard]] bool run_internal_update(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::string_view durable_job_id,
        const std::filesystem::path& training_dataset,
        const std::filesystem::path& evaluation_dataset,
        std::size_t bootstrap_target_steps,
        bool initialize_internal_model,
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
    std::atomic_uint32_t interactive_leases_{0};
    std::jthread worker_;
};

[[nodiscard]] std::string_view to_string(LearningJobState state) noexcept;

} // namespace threadspire::ai::learning

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

namespace threadspire::ai::learning {

enum class CurriculumRights { unknown, public_domain, permissive_license, user_owned };
enum class FoundationStage {
    idle, ingesting, mapping, generating, verifying, compiling,
    queued_training, paused, completed, failed,
};
enum class CurriculumTeacherRole { qwen_architect, nemotron_generator, independent_verifier };

struct CurriculumSourceInput final {
    std::filesystem::path path;
    std::string title;
    std::string origin;
    std::string permission;
    CurriculumRights rights{CurriculumRights::unknown};
    bool redistribution_permitted{false};
};
struct CurriculumSourceRecord final {
    std::string id;
    std::string title;
    std::string origin;
    std::string permission;
    CurriculumRights rights{CurriculumRights::unknown};
    std::string content_hash;
    std::int64_t ingested_unix_ms{0};
    std::size_t bytes{0};
    std::size_t sections{0};
    std::size_t chunks{0};
    std::size_t held_out_chunks{0};
    bool redistribution_permitted{false};
};
struct CurriculumChunk final {
    std::string id;
    std::string source_id;
    std::string section;
    std::string source_span;
    std::string content_hash;
    std::string text;
    bool held_out{false};
};
struct CurriculumPreparationResult final {
    bool succeeded{false};
    std::vector<CurriculumSourceRecord> sources;
    std::vector<CurriculumChunk> chunks;
    std::size_t duplicates_removed{0};
    std::string error;
};
struct CurriculumTeacherResponse final {
    bool succeeded{false};
    std::string content;
    std::string error;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
};
using CurriculumCompletion = std::function<CurriculumTeacherResponse(
    CurriculumTeacherRole role,
    const models::ModelProfile& model,
    std::string_view system_prompt,
    std::string_view prompt,
    std::size_t maximum_output_tokens,
    double temperature,
    std::stop_token stop_token)>;
struct FoundationBuildRequest final {
    std::filesystem::path curriculum_root;
    std::filesystem::path base_anchor_dataset;
    std::vector<CurriculumSourceInput> sources;
    models::ModelProfile qwen;
    models::ModelProfile nemotron;
    std::string internal_base_url;
    CurriculumCompletion completion;
    std::size_t nemotron_parallelism{4};
    std::size_t qwen_parallelism{1};
    std::size_t variants_per_lesson{4};
    std::size_t maximum_source_chunks{0};
    double minimum_verifier_score{0.88};
    bool rebuild{false};
};
struct FoundationBuildProgress final {
    FoundationStage stage{FoundationStage::idle};
    std::string operation;
    std::string teacher;
    std::string verifier;
    std::string source;
    std::string checkpoint;
    std::string active_candidate;
    std::string promotion_result;
    std::size_t source_index{0};
    std::size_t source_count{0};
    std::size_t chunk_index{0};
    std::size_t chunk_count{0};
    std::size_t generated{0};
    std::size_t accepted{0};
    std::size_t revised{0};
    std::size_t rejected{0};
    std::size_t retry_pending{0};
    std::size_t held_out{0};
    std::size_t queued_weight_jobs{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::uintmax_t model_bytes{0};
    std::string message;
};
struct FoundationBuildResult final {
    bool succeeded{false};
    bool cancelled{false};
    bool training_queued{false};
    FoundationBuildProgress progress;
    std::string error;
};
using FoundationProgressCallback = std::function<void(const FoundationBuildProgress&)>;

[[nodiscard]] std::string_view to_string(CurriculumRights rights) noexcept;
[[nodiscard]] std::string_view to_string(FoundationStage stage) noexcept;
[[nodiscard]] bool curriculum_rights_admitted(CurriculumRights rights) noexcept;
[[nodiscard]] CurriculumPreparationResult prepare_english_foundation_sources(
    const std::vector<CurriculumSourceInput>& sources,
    const std::filesystem::path& curriculum_root,
    bool rebuild = false);
[[nodiscard]] FoundationBuildProgress read_english_foundation_progress(
    const std::filesystem::path& curriculum_root);
[[nodiscard]] FoundationBuildResult run_english_foundation_build(
    const FoundationBuildRequest& request,
    ContinualLearningService& learning_service,
    const FoundationProgressCallback& on_progress = {},
    std::stop_token stop_token = {});

} // namespace threadspire::ai::learning
}
