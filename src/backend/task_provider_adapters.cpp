module;
#include "module_prelude.hpp"

module threadspire.providers;
import threadspire.mcp;

namespace threadspire::ai::backend {
namespace {

[[nodiscard]] bool emit_event(
    const UnifiedTaskRequest& request,
    TaskEvent event) {
    return !request.on_event || request.on_event(event);
}

} // namespace

AgentTaskProvider::AgentTaskProvider(
    models::ModelRegistry& models,
    std::vector<learning::ExpertModule> experts,
    const memory::EpisodeStore& memory,
    std::string internal_base_url,
    mcp::Client* const mcp_client)
    : models_(models),
      experts_(std::move(experts)),
      memory_(memory),
      internal_base_url_(std::move(internal_base_url)),
      mcp_client_(mcp_client) {
}

std::string_view AgentTaskProvider::id() const noexcept {
    return "native-agent";
}

ProviderCapabilities AgentTaskProvider::capabilities() const noexcept {
    return {
        .interactive_text = true,
        .coding = true,
        .repository_work = false,
        .image_generation = false,
        .audio_generation = false,
        .tools = true,
        .streaming = true,
        .cancellation = true,
        .file_artifacts = false,
    };
}

UnifiedTaskResult AgentTaskProvider::execute(
    const UnifiedTaskRequest& request,
    const std::stop_token stop_token) {
    UnifiedTaskResult result{
        .task_id = request.task_id,
        .provider_id = std::string(id()),
    };
    if (!capabilities().supports(request.modality, request.profile)) {
        result.error = "native agent does not support the requested modality/profile";
        return result;
    }
    if (request.prompt.empty()) {
        result.error = "task prompt is empty";
        return result;
    }
    if (stop_token.stop_requested()) {
        result.cancelled = true;
        result.error = "task cancelled";
        return result;
    }
    static_cast<void>(emit_event(request, {
        .kind = TaskEventKind::status,
        .text = "routing " + std::string(to_string(request.profile)) + " request",
    }));

    AgentOrchestrator orchestrator(models_, experts_, memory_);
    const auto agent = orchestrator.execute(
        internal_base_url_,
        request.prompt,
        AgentExecutionOptions{
            .force_coding_agent = request.profile == ExecutionProfile::coding_agent,
            .prefer_minimum_compute = request.profile == ExecutionProfile::interactive,
            .analysis_prompt = request.analysis_prompt,
            .on_token = [&request, stop_token](const std::string_view token) {
                return !stop_token.stop_requested() && emit_event(request, {
                    .kind = TaskEventKind::text_delta,
                    .text = std::string(token),
                });
            },
            .should_stop = [stop_token] { return stop_token.stop_requested(); },
            .mcp_client = mcp_client_,
            .maximum_tool_rounds = request.modality == WorkModality::tool_action ? 8U : 4U,
        });
    result.succeeded = agent.succeeded;
    result.cancelled = stop_token.stop_requested();
    result.content = agent.response;
    result.error = agent.error;
    result.plan = agent.plan;
    result.models_used = agent.models_used;
    result.tool_events = agent.tool_events;
    result.effective_tokens_per_second = agent.effective_tokens_per_second;
    for (const auto& tool : result.tool_events) {
        if (!emit_event(request, {.kind = TaskEventKind::tool, .text = tool})) {
            result.cancelled = true;
            result.succeeded = false;
            result.error = "task cancelled by event consumer";
            break;
        }
    }
    return result;
}

RepositoryTaskProvider::RepositoryTaskProvider(
    std::filesystem::path state_root,
    std::string internal_base_url,
    models::ModelProfile default_model)
    : state_root_(std::move(state_root)),
      internal_base_url_(std::move(internal_base_url)),
      default_model_(std::move(default_model)) {
}

std::string_view RepositoryTaskProvider::id() const noexcept {
    return "native-repository";
}

ProviderCapabilities RepositoryTaskProvider::capabilities() const noexcept {
    return {
        .interactive_text = false,
        .coding = true,
        .repository_work = true,
        .image_generation = false,
        .audio_generation = false,
        .tools = true,
        .streaming = true,
        .cancellation = true,
        .file_artifacts = true,
    };
}

UnifiedTaskResult RepositoryTaskProvider::execute(
    const UnifiedTaskRequest& request,
    const std::stop_token stop_token) {
    UnifiedTaskResult result{
        .task_id = request.task_id,
        .provider_id = std::string(id()),
    };
    if (!capabilities().supports(request.modality, request.profile)) {
        result.error = "repository provider requires the long-horizon execution profile";
        return result;
    }
    if (request.repository.empty() || request.prompt.empty()) {
        result.error = "repository and task prompt are required";
        return result;
    }
    if (!emit_event(request, {
            .kind = TaskEventKind::status,
            .text = "starting serialized long-horizon repository execution",
        })) {
        result.cancelled = true;
        result.error = "task cancelled by event consumer";
        return result;
    }
    const auto repository = long_horizon::run(
        long_horizon::Request{
            .repository = request.repository,
            .state_root = state_root_,
            .backend_base_url = internal_base_url_,
            .repair_model = request.model.id.empty() ? default_model_ : request.model,
            .task = request.prompt,
            .build_command = request.build_command,
            .test_command = request.test_command,
            .adapter_path = request.adapter_path,
            .module_path = request.module_path,
            .maximum_iterations = request.maximum_iterations,
            .require_clean_worktree = request.require_clean_worktree,
            .rollback_on_failure = request.rollback_on_failure,
        },
        stop_token);
    result.cancelled = repository.operation.state == util::OperationState::cancelled ||
        stop_token.stop_requested();
    result.succeeded = repository.completed && repository.compiler_passed && repository.tests_passed;
    result.compiler_passed = repository.compiler_passed;
    result.tests_passed = repository.tests_passed;
    result.iterations = repository.iterations;
    result.run_directory = repository.run_directory;
    result.content = repository.final_diff;
    if (!result.succeeded) {
        result.error = repository.operation.message;
    }
    const auto add_artifact = [&request, &result](
        const ArtifactKind kind,
        const std::filesystem::path& path,
        std::string label) {
        if (path.empty()) {
            return;
        }
        TaskArtifact artifact{.kind = kind, .path = path, .label = std::move(label)};
        result.artifacts.push_back(artifact);
        static_cast<void>(emit_event(request, {
            .kind = TaskEventKind::artifact,
            .text = result.artifacts.back().label,
            .artifact = std::move(artifact),
        }));
    };
    add_artifact(ArtifactKind::patch, repository.final_diff_path, "validated repository patch");
    add_artifact(ArtifactKind::log, repository.trajectory_path, "repository trajectory");
    for (const auto& event : repository.events) {
        result.tool_events.push_back(event.phase + ": exit " + std::to_string(event.exit_code));
    }
    return result;
}

} // namespace threadspire::ai::backend