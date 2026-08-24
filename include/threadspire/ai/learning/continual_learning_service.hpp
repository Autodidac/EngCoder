#pragma once

#include "threadspire/ai/learning/candidate_registry.hpp"
#include "threadspire/ai/learning/expert_registry.hpp"
#include "threadspire/ai/learning/knowledge_classifier.hpp"
#include "threadspire/ai/memory/episode_store.hpp"
#include "threadspire/ai/resource/resource_governor.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

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
        std::stop_token stop_token);
    [[nodiscard]] bool run_lora_update(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::stop_token stop_token);
    [[nodiscard]] bool run_consolidation(
        const LearningSettings& settings,
        ExpertModule& active_module,
        LearningJobStatus& status,
        std::optional<std::string> resume_candidate_id,
        std::stop_token stop_token);
    void queue_request(LearningRequest request);
    void recover_interrupted_candidates();
    void update_status(const LearningJobStatus& status);
    void persist_status(const LearningJobStatus& status) const;
    void commit_module(const ExpertModule& module);
    void commit_candidate(const CandidateRecord& candidate);
    void publish_control_files() const;

    LearningSettings settings_;
    memory::EpisodeStore memory_store_;
    ExpertRegistry expert_registry_;
    CandidateRegistry candidate_registry_;
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
