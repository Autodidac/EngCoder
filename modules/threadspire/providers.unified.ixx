module;
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module threadspire.providers:unified;

import threadspire.models;
import threadspire.tasks;

export {
namespace threadspire::ai::backend {

enum class WorkModality {
    interactive_text,
    image_generation,
    audio_generation,
    tool_action,
    repository_work,
};

enum class ExecutionProfile {
    interactive,
    coding_agent,
    long_horizon,
};

enum class ArtifactKind {
    file,
    image,
    audio,
    patch,
    log,
};

enum class TaskEventKind {
    status,
    text_delta,
    tool,
    artifact,
};

struct TaskArtifact final {
    ArtifactKind kind{ArtifactKind::file};
    std::filesystem::path path;
    std::string mime_type;
    std::string label;
};

struct TaskEvent final {
    TaskEventKind kind{TaskEventKind::status};
    std::string text;
    TaskArtifact artifact;
};

using TaskEventCallback = std::function<bool(const TaskEvent&)>;

struct UnifiedTaskRequest final {
    std::string task_id;
    WorkModality modality{WorkModality::interactive_text};
    ExecutionProfile profile{ExecutionProfile::interactive};
    std::string preferred_provider_id;
    std::string prompt;
    std::string analysis_prompt;
    std::string context;
    models::ModelProfile model;
    std::filesystem::path repository;
    std::filesystem::path output_root;
    std::string build_command;
    std::string test_command;
    std::filesystem::path adapter_path;
    std::filesystem::path module_path;
    std::string tools_json;
    std::size_t maximum_iterations{8};
    std::size_t maximum_output_tokens{512};
    bool stream{true};
    bool allow_automatic_fallback{true};
    bool require_clean_worktree{true};
    bool rollback_on_failure{true};
    TaskEventCallback on_event;
};

struct ProviderCapabilities final {
    bool interactive_text{false};
    bool coding{false};
    bool repository_work{false};
    bool image_generation{false};
    bool audio_generation{false};
    bool tools{false};
    bool streaming{false};
    bool cancellation{true};
    bool file_artifacts{false};

    [[nodiscard]] bool supports(
        WorkModality modality,
        ExecutionProfile profile) const noexcept;
};

struct UnifiedTaskResult final {
    bool succeeded{false};
    bool cancelled{false};
    bool compiler_passed{false};
    bool tests_passed{false};
    std::size_t iterations{0};
    std::filesystem::path run_directory;
    std::string task_id;
    std::string provider_id;
    std::string content;
    std::string error;
    routing::RoutingPlan plan;
    std::vector<std::string> models_used;
    std::vector<std::string> tool_events;
    std::vector<TaskArtifact> artifacts;
    double effective_tokens_per_second{0.0};
};

class TaskProvider {
public:
    virtual ~TaskProvider() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual ProviderCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual UnifiedTaskResult execute(
        const UnifiedTaskRequest& request,
        std::stop_token stop_token) = 0;
};

class UnifiedProviderRouter final {
public:
    void register_provider(std::shared_ptr<TaskProvider> provider);
    [[nodiscard]] std::vector<std::string> provider_ids() const;
    [[nodiscard]] UnifiedTaskResult execute(
        UnifiedTaskRequest request,
        std::stop_token stop_token = {});
    [[nodiscard]] bool cancel(std::string_view task_id) noexcept;
    [[nodiscard]] std::size_t active_tasks() const noexcept;

private:
    [[nodiscard]] std::shared_ptr<TaskProvider> select_provider(
        const UnifiedTaskRequest& request) const;

    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<TaskProvider>> providers_;
    std::unordered_map<std::string, std::stop_source> active_tasks_;
};

[[nodiscard]] std::string_view to_string(WorkModality modality) noexcept;
[[nodiscard]] std::string_view to_string(ExecutionProfile profile) noexcept;
[[nodiscard]] std::string_view to_string(ArtifactKind kind) noexcept;

} // namespace threadspire::ai::backend
}