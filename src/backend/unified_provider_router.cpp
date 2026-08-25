module;
#include "module_prelude.hpp"

module threadspire.providers;

namespace threadspire::ai::backend {
namespace {

[[nodiscard]] std::string make_task_id() {
    static std::atomic_uint64_t next{1U};
    return "task-" + std::to_string(next.fetch_add(1U, std::memory_order_relaxed));
}

} // namespace

bool ProviderCapabilities::supports(
    const WorkModality modality,
    const ExecutionProfile profile) const noexcept {
    if (profile == ExecutionProfile::long_horizon) {
        return modality == WorkModality::repository_work && repository_work;
    }
    if (modality == WorkModality::repository_work) {
        return false;
    }
    if (profile == ExecutionProfile::coding_agent && !coding) {
        return false;
    }
    switch (modality) {
    case WorkModality::interactive_text:
        return profile == ExecutionProfile::coding_agent ? coding : interactive_text;
    case WorkModality::image_generation:
        return profile == ExecutionProfile::interactive && image_generation;
    case WorkModality::audio_generation:
        return profile == ExecutionProfile::interactive && audio_generation;
    case WorkModality::tool_action:
        return tools && (profile == ExecutionProfile::interactive || coding);
    case WorkModality::repository_work:
        return false;
    }
    return false;
}

std::string_view to_string(const WorkModality modality) noexcept {
    switch (modality) {
    case WorkModality::interactive_text: return "interactive-text";
    case WorkModality::image_generation: return "image-generation";
    case WorkModality::audio_generation: return "audio-generation";
    case WorkModality::tool_action: return "tool-action";
    case WorkModality::repository_work: return "repository-work";
    }
    return "interactive-text";
}

std::string_view to_string(const ExecutionProfile profile) noexcept {
    switch (profile) {
    case ExecutionProfile::interactive: return "interactive";
    case ExecutionProfile::coding_agent: return "coding-agent";
    case ExecutionProfile::long_horizon: return "long-horizon";
    }
    return "interactive";
}

std::string_view to_string(const ArtifactKind kind) noexcept {
    switch (kind) {
    case ArtifactKind::file: return "file";
    case ArtifactKind::image: return "image";
    case ArtifactKind::audio: return "audio";
    case ArtifactKind::patch: return "patch";
    case ArtifactKind::log: return "log";
    }
    return "file";
}

void UnifiedProviderRouter::register_provider(std::shared_ptr<TaskProvider> provider) {
    if (!provider || provider->id().empty()) {
        throw std::invalid_argument("provider id must not be empty");
    }
    std::lock_guard lock(mutex_);
    const auto duplicate = std::ranges::find_if(
        providers_, [&provider](const std::shared_ptr<TaskProvider>& existing) {
            return existing->id() == provider->id();
        });
    if (duplicate != providers_.end()) {
        *duplicate = std::move(provider);
    } else {
        providers_.push_back(std::move(provider));
    }
}

std::vector<std::string> UnifiedProviderRouter::provider_ids() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    result.reserve(providers_.size());
    for (const auto& provider : providers_) {
        result.emplace_back(provider->id());
    }
    return result;
}

std::shared_ptr<TaskProvider> UnifiedProviderRouter::select_provider(
    const UnifiedTaskRequest& request) const {
    std::lock_guard lock(mutex_);
    if (!request.preferred_provider_id.empty()) {
        const auto preferred = std::ranges::find_if(
            providers_, [&request](const std::shared_ptr<TaskProvider>& provider) {
                return provider->id() == request.preferred_provider_id;
            });
        if (preferred == providers_.end() ||
            !(*preferred)->capabilities().supports(request.modality, request.profile)) {
            return {};
        }
        return *preferred;
    }
    const auto provider = std::ranges::find_if(
        providers_, [&request](const std::shared_ptr<TaskProvider>& candidate) {
            return candidate->capabilities().supports(request.modality, request.profile);
        });
    return provider == providers_.end() ? std::shared_ptr<TaskProvider>{} : *provider;
}

UnifiedTaskResult UnifiedProviderRouter::execute(
    UnifiedTaskRequest request,
    const std::stop_token stop_token) {
    if (request.task_id.empty()) {
        request.task_id = make_task_id();
    }
    UnifiedTaskResult result{
        .task_id = request.task_id,
    };
    const auto provider = select_provider(request);
    if (!provider) {
        result.error = "no provider supports " + std::string(to_string(request.modality)) +
            " with the " + std::string(to_string(request.profile)) + " profile";
        return result;
    }
    result.provider_id = std::string(provider->id());

    std::stop_source cancellation;
    {
        std::lock_guard lock(mutex_);
        if (active_tasks_.contains(request.task_id)) {
            result.error = "task id is already active";
            return result;
        }
        active_tasks_.emplace(request.task_id, cancellation);
    }
    std::stop_callback external_stop(stop_token, [&cancellation] {
        static_cast<void>(cancellation.request_stop());
    });
    if (stop_token.stop_requested()) {
        static_cast<void>(cancellation.request_stop());
    }

    try {
        result = provider->execute(request, cancellation.get_token());
        result.task_id = request.task_id;
        result.provider_id = std::string(provider->id());
    } catch (const std::exception& exception) {
        result.error = exception.what();
    } catch (...) {
        result.error = "provider failed with an unknown exception";
    }
    result.cancelled = result.cancelled || cancellation.stop_requested();
    if (result.cancelled && result.error.empty() && !result.succeeded) {
        result.error = "task cancelled";
    }
    {
        std::lock_guard lock(mutex_);
        active_tasks_.erase(request.task_id);
    }
    return result;
}

bool UnifiedProviderRouter::cancel(const std::string_view task_id) noexcept {
    std::stop_source source;
    {
        std::lock_guard lock(mutex_);
        const auto task = active_tasks_.find(std::string(task_id));
        if (task == active_tasks_.end()) {
            return false;
        }
        source = task->second;
    }
    return source.request_stop();
}

std::size_t UnifiedProviderRouter::active_tasks() const noexcept {
    std::lock_guard lock(mutex_);
    return active_tasks_.size();
}

} // namespace threadspire::ai::backend