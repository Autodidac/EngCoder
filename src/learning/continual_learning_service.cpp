module;
#include "module_prelude.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>

module threadspire.learning;
import threadspire.platform;

namespace threadspire::ai::learning {
namespace {

[[nodiscard]] std::string make_job_id(const std::string_view skill) {
    return "learn-" + normalize_skill(skill) + '-' + std::to_string(memory::now_unix_ms());
}

void sanitize_learning_episode(memory::Episode& episode) {
    episode.prompt = sanitize_training_text(episode.prompt);
    episode.response = sanitize_training_response(episode.response, episode.model_id);
    episode.correction = sanitize_training_response(episode.correction, episode.model_id);
}

void set_marker(const std::filesystem::path& path, const bool enabled) {
    std::error_code error;
    if (enabled) {
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream stream(path, std::ios::trunc);
        stream << "1\n";
    } else {
        std::filesystem::remove(path, error);
    }
}

[[nodiscard]] double read_metric(
    const std::filesystem::path& path,
    const std::string_view key,
    const double fallback) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return fallback;
    }
    const std::string contents{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    const auto marker = '"' + std::string(key) + '"';
    auto position = contents.find(marker);
    if (position == std::string::npos) {
        return fallback;
    }
    position = contents.find(':', position + marker.size());
    if (position == std::string::npos) {
        return fallback;
    }
    try {
        return std::stod(contents.substr(position + 1U));
    } catch (...) {
        return fallback;
    }
}

[[nodiscard]] std::string read_string_metric(
    const std::filesystem::path& path,
    const std::string_view key) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    const std::string contents{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    const auto marker = '"' + std::string(key) + '"';
    auto position = contents.find(marker);
    if (position == std::string::npos) {
        return {};
    }
    position = contents.find(':', position + marker.size());
    if (position == std::string::npos) {
        return {};
    }
    position = contents.find('"', position + 1U);
    if (position == std::string::npos) {
        return {};
    }
    const auto end = contents.find('"', position + 1U);
    if (end == std::string::npos) {
        return {};
    }
    return contents.substr(position + 1U, end - position - 1U);
}

[[nodiscard]] std::filesystem::path candidate_root(
    const LearningSettings& settings,
    const std::string_view candidate_id) {
    return settings.state_root / "candidates" / std::string(candidate_id);
}

void clear_training_markers(const std::filesystem::path& state_root) noexcept {
    std::error_code error;
    std::filesystem::remove(state_root / "control" / "training.active", error);
    error.clear();
    std::filesystem::remove(state_root / "control" / "training.requested", error);
}

constexpr int preempted_exit_code = 75;

[[nodiscard]] bool terminal_state(const LearningJobState state) noexcept {
    switch (state) {
    case LearningJobState::succeeded:
    case LearningJobState::failed:
    case LearningJobState::cancelled:
    case LearningJobState::interrupted:
        return true;
    case LearningJobState::queued:
    case LearningJobState::waiting_for_resources:
    case LearningJobState::training_lora:
    case LearningJobState::training_internal:
    case LearningJobState::validating:
    case LearningJobState::consolidating:
        return false;
    }
    return false;
}

[[nodiscard]] std::filesystem::path internal_generation_path(
    const std::filesystem::path& state_root,
    const std::string_view generation) {
    return state_root / "models" / "internal" / "generations" / std::string(generation);
}

[[nodiscard]] std::string internal_generation_name(const std::filesystem::path& path) {
    const auto value = path.filename().string();
    return value.starts_with('g') ? value : std::string{};
}

[[nodiscard]] std::string internal_training_scope(const std::string_view skill) {
    const auto normalized = normalize_skill(skill);
    if (normalized == "cpp23-debugging" || normalized.starts_with("cpp23-debug") ||
        normalized.find("compiler") != std::string::npos ||
        normalized.find("repair") != std::string::npos) {
        return "cpp23-debugging";
    }
    if (normalized == "planning" || normalized.find("planning") != std::string::npos ||
        normalized.find("architecture") != std::string::npos ||
        normalized.find("long-horizon") != std::string::npos ||
        normalized == "tool-use") {
        return "planning";
    }
    if (normalized == "cpp23-core" || normalized.starts_with("cpp23-") ||
        normalized == "build-systems") {
        return "cpp23-core";
    }
    return "english-core";
}

[[nodiscard]] int ready_port(const std::filesystem::path& ready_file) noexcept {
    try {
        std::ifstream stream(ready_file);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.starts_with("port=")) {
                return std::stoi(line.substr(5U));
            }
        }
    } catch (...) {
    }
    return 0;
}

[[nodiscard]] CandidateHostChecks verify_internal_candidate_host(
    const LearningSettings& settings,
    const CandidateRecord& candidate,
    const std::string_view generation,
    const std::filesystem::path& metrics_path,
    const std::stop_token stop_token) {
    CandidateHostChecks checks;
    const auto ready_file = candidate.job_directory / "candidate-host.ready";
    const auto control_directory = candidate.job_directory / "candidate-host-control";
    std::error_code filesystem_error;
    std::filesystem::remove(ready_file, filesystem_error);
    std::filesystem::create_directories(control_directory, filesystem_error);
    if (filesystem_error) {
        checks.diagnostics = "unable to create isolated candidate-host control directory: " +
            filesystem_error.message();
        return checks;
    }

    platform::ManagedProcess process;
    std::string process_error;
    if (!process.start(
            platform::ProcessSpec{
                .executable = settings.python_executable,
                .arguments = {
                    settings.worker_script.string(),
                    "serve",
                    "--port", "0",
                    "--ready-file", ready_file.string(),
                    "--state-root", settings.state_root.string(),
                    "--generation", std::string(generation),
                    "--control-dir", control_directory.string(),
                    "--max-loaded-models", "1",
                },
                .working_directory = settings.state_root,
                .log_file = candidate.log_path,
                .append_log = true,
            },
            process_error)) {
        checks.diagnostics = "isolated candidate host did not launch: " + process_error;
        return checks;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    auto port = 0;
    while (!stop_token.stop_requested() && process.running() &&
           std::chrono::steady_clock::now() < deadline) {
        port = ready_port(ready_file);
        if (port > 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (stop_token.stop_requested()) {
        process.stop();
        checks.diagnostics = "candidate-host verification cancelled; active model remains online";
        return checks;
    }
    if (port <= 0) {
        const auto exit_code = process.exit_code();
        process.stop();
        checks.diagnostics = "candidate host did not become ready within the bounded startup window (exit " +
            std::to_string(exit_code) + ')';
        return checks;
    }

    const auto base_url = "http://127.0.0.1:" + std::to_string(port);
    const auto health = backend::http_get(base_url + "/health", std::chrono::seconds(10));
    checks.candidate_host_started = health.succeeded() &&
        health.body.find(std::string(generation)) != std::string::npos &&
        health.body.find("\"shadow_candidate\": true") != std::string::npos;
    if (!checks.candidate_host_started) {
        process.stop();
        checks.diagnostics = health.error.empty()
            ? "candidate health response did not identify the isolated shadow generation"
            : "candidate health check failed: " + health.error;
        return checks;
    }
    // The worker validates the generation manifest and every declared checksum before
    // publishing its ready file. A later canary failure is therefore a recoverable
    // host/inference failure, not evidence that the persisted artifact is corrupt.
    checks.artifact_checksum_valid = true;

    const auto canary = backend::http_post_json(
        base_url + "/v1/chat/completions",
        R"({"model":"threadspire://fast","messages":[{"role":"user","content":"Reply briefly in clear English: what is two plus two?"}],"max_tokens":48,"temperature":0.0,"stream":false})",
        std::chrono::minutes(2));
    process.stop();
    checks.inference_canary_passed = canary.succeeded() &&
        canary.body.find(std::string(generation)) != std::string::npos &&
        canary.body.find("low_confidence") == std::string::npos &&
        canary.body.find("\"content\": \"\"") == std::string::npos;
    checks.deterministic_validation_passed =
        read_metric(metrics_path, "passed_evaluation", 0.0) > 0.5 &&
        read_metric(metrics_path, "generation_ready", 0.0) > 0.5;
    checks.regression_gate_passed =
        candidate.validation_score >= settings.policy.minimum_validation_score &&
        candidate.regression_score <= settings.policy.maximum_regression;

    if (!checks.artifact_checksum_valid) {
        checks.diagnostics = canary.error.empty()
            ? "candidate artifact failed checksum/load validation in the shadow host"
            : "candidate host could not load its artifact: " + canary.error;
    } else if (!checks.inference_canary_passed) {
        checks.diagnostics = "candidate host returned an invalid or low-confidence inference canary";
    } else if (!checks.deterministic_validation_passed) {
        checks.diagnostics = "candidate failed deterministic held-out validation";
    } else if (!checks.regression_gate_passed) {
        checks.diagnostics = "candidate failed the independent regression gate";
    } else {
        checks.diagnostics = "isolated candidate host passed checksum, health, inference, deterministic, and regression checks";
    }
    return checks;
}
void publish_file(
    const std::filesystem::path& destination,
    const std::string_view contents) {
    std::filesystem::create_directories(destination.parent_path());
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << contents;
        stream.flush();
        if (!stream) {
            throw std::runtime_error("unable to write state file: " + destination.string());
        }
    }
    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::runtime_error("unable to publish state file: " + error.message());
    }
}

} // namespace

std::string_view to_string(const LearningJobState state) noexcept {
    switch (state) {
    case LearningJobState::queued:
        return "queued";
    case LearningJobState::waiting_for_resources:
        return "waiting-for-resources";
    case LearningJobState::training_lora:
        return "training-lora";
    case LearningJobState::training_internal:
        return "training-internal";
    case LearningJobState::validating:
        return "validating";
    case LearningJobState::consolidating:
        return "consolidating";
    case LearningJobState::succeeded:
        return "succeeded";
    case LearningJobState::failed:
        return "failed";
    case LearningJobState::cancelled:
        return "cancelled";
    case LearningJobState::interrupted:
        return "interrupted";
    }
    return "queued";
}

std::string_view to_string(const ImprovementSessionPhase phase) noexcept {
    switch (phase) {
    case ImprovementSessionPhase::running:
        return "running";
    case ImprovementSessionPhase::paused:
        return "paused";
    case ImprovementSessionPhase::ended:
        return "ended";
    case ImprovementSessionPhase::time_limit_reached:
        return "time-limit-reached";
    }
    return "ended";
}

ContinualLearningService::ContinualLearningService(LearningSettings settings)
    : settings_(std::move(settings)),
      memory_store_(settings_.state_root / "memory" / "episodes.jsonl"),
      expert_registry_(settings_.state_root / "experts" / "registry"),
      candidate_registry_(settings_.state_root / "candidates" / "registry"),
      persistent_queue_(settings_.state_root / "jobs" / "training-state.json"),
      resource_governor_(settings_.resource_policy),
      interactive_leases_(settings_.initial_interactive_priority ? 1U : 0U) {
    std::filesystem::create_directories(settings_.state_root / "jobs");
    std::filesystem::create_directories(settings_.state_root / "control");
    load_improvement_session();
    expert_registry_.load();
    candidate_registry_.load();
    try {
        persistent_queue_.load();
        static_cast<void>(persistent_queue_.recover_inflight(
            "process stopped while a shadow candidate was active; exact checkpoint retained"));
    } catch (...) {
        // A malformed queue must not prevent the last promoted model from serving.
    }
    recover_persisted_requests();
    recover_interrupted_candidates();
    persistent_queue_.set_interactive_busy(interactive_busy());
    publish_control_files();
    worker_ = std::jthread([this](const std::stop_token stop_token) {
        worker_loop(stop_token);
    });
}

ContinualLearningService::~ContinualLearningService() {
    request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    clear_training_markers(settings_.state_root);
}

LearningSubmissionPreview ContinualLearningService::preview(memory::Episode episode) const {
    LearningSettings settings;
    {
        std::lock_guard lock(mutex_);
        settings = settings_;
    }

    LearningSubmissionPreview result;
    const auto original_prompt = episode.prompt;
    const auto original_response = episode.response;
    const auto original_correction = episode.correction;
    sanitize_learning_episode(episode);
    result.stored_prompt = episode.prompt;
    result.stored_response =
        episode.human_rejected && !episode.correction.empty()
            ? episode.correction
            : (episode.response.empty() ? episode.correction : episode.response);
    result.training_text_adjusted =
        episode.prompt != original_prompt ||
        episode.response != original_response ||
        episode.correction != original_correction;
    if (episode.prompt.empty() || (episode.response.empty() && !episode.human_rejected)) {
        result.reason = "a learning record needs the exact user message and a completed reply";
        return result;
    }

    episode.skill = normalize_skill(episode.skill);
    episode.deterministic_verified = episode.deterministic_verified ||
                                     (episode.compiler_passed && episode.tests_passed);
    const auto hash_response = episode.human_rejected && !episode.correction.empty()
        ? episode.response + "\nAPPROVED CORRECTION\n" + episode.correction
        : (episode.response.empty() ? episode.correction : episode.response);
    result.content_hash = memory::make_content_hash(
        episode.skill,
        episode.prompt,
        hash_response);
    const auto current_accepted_examples =
        memory_store_.human_acceptance_count_for_skill(episode.skill);
    const auto hard_correction = episode.human_rejected && !episode.correction.empty() &&
        episode.provenance == "interactive-hard-correction";
    const auto independently_validated =
        episode.human_accepted || episode.deterministic_verified || episode.reference_verified;
    const auto exact_duplicate = memory_store_.contains_hash(result.content_hash);
    const auto prompt_already_positive =
        memory_store_.contains_positive_prompt(episode.skill, episode.prompt);
    const auto prompt_already_human_accepted =
        memory_store_.contains_human_accepted_prompt(episode.skill, episode.prompt);
    const auto positive_prompt_duplicate =
        independently_validated && !episode.human_rejected && prompt_already_positive;
    const auto projected_accepted_examples =
        current_accepted_examples +
        (((episode.human_accepted && !episode.human_rejected) ||
          (hard_correction && !prompt_already_human_accepted)) &&
         !exact_duplicate && !positive_prompt_duplicate ? 1U : 0U);

    auto decision = knowledge_classifier_.classify(
        episode,
        projected_accepted_examples);
    if (decision.destination != KnowledgeDestination::reject &&
        episode.score < settings.policy.minimum_episode_score) {
        decision.eligible_for_weight_training = false;
        if (decision.destination == KnowledgeDestination::lora_incubation) {
            decision.destination = KnowledgeDestination::retrieval_memory;
        }
        decision.reason = "saved for memory only because its score is below the weight-training threshold";
    }

    result.destination = decision.destination;
    result.eligible_for_weight_training = decision.eligible_for_weight_training;
    result.confidence = decision.confidence;
    result.accepted_skill_examples =
        decision.destination == KnowledgeDestination::reject ||
                exact_duplicate || positive_prompt_duplicate
            ? current_accepted_examples
            : projected_accepted_examples;
    result.reason = decision.reason;
    if (decision.destination == KnowledgeDestination::reject) {
        return result;
    }

    if (exact_duplicate) {
        result.duplicate = true;
        result.reason = "this exact message and reply are already stored; repeated approval adds no weight";
        return result;
    }

    if (positive_prompt_duplicate) {
        result.duplicate = true;
        result.reason =
            "this skill and message already have an approved reply; another approval would overweight one prompt";
        return result;
    }

    result.would_store = true;
    return result;
}

bool ContinualLearningService::submit(memory::Episode episode) {
    sanitize_learning_episode(episode);
    LearningSettings settings;
    {
        std::lock_guard lock(mutex_);
        settings = settings_;
    }
    const auto inspection = preview(episode);
    if (!inspection.would_store) {
        return false;
    }
    if (episode.prompt.empty() || (episode.response.empty() && !episode.human_rejected)) {
        return false;
    }
    episode.skill = normalize_skill(episode.skill);
    if (episode.id.empty()) {
        episode.id = memory::make_episode_id();
    }
    if (episode.created_unix_ms == 0) {
        episode.created_unix_ms = memory::now_unix_ms();
    }
    episode.deterministic_verified = episode.deterministic_verified ||
                                     (episode.compiler_passed && episode.tests_passed);
    const auto hash_response = episode.human_rejected && !episode.correction.empty()
        ? episode.response + "\nAPPROVED CORRECTION\n" + episode.correction
        : (episode.response.empty() ? episode.correction : episode.response);
    episode.content_hash = memory::make_content_hash(
        episode.skill,
        episode.prompt,
        hash_response);
    episode.eligible_for_training = inspection.eligible_for_weight_training;
    episode.knowledge_destination = std::string(to_string(inspection.destination));
    if (!memory_store_.append_if_new(episode)) {
        return false;
    }

    std::string skill_to_queue;
    const auto hard_correction = episode.human_rejected && !episode.correction.empty() &&
        episode.provenance == "interactive-hard-correction";
    if (episode.human_accepted || episode.deterministic_verified ||
        episode.reference_verified || hard_correction) {
        std::lock_guard lock(mutex_);
        auto& module = expert_registry_.ensure(episode.skill, settings_.base_model);
        ++module.accepted_episodes;
        if (episode.eligible_for_training) {
            ++module.episodes_since_update;
        }
        if (episode.compiler_passed) {
            ++module.compiler_verified_episodes;
        }
        if (episode.tests_passed) {
            ++module.test_verified_episodes;
        }
        module.updated_unix_ms = memory::now_unix_ms();
        expert_registry_.save(module);
        if (expert_registry_.ready_for_lora(module, settings.policy)) {
            skill_to_queue = module.skill;
        }
    }
    if (!skill_to_queue.empty()) {
        request_skill_update(std::move(skill_to_queue));
    }
    return true;
}

void ContinualLearningService::request_skill_update(std::string skill) {
    skill = normalize_skill(skill);
    {
        std::lock_guard lock(mutex_);
        auto& module = expert_registry_.ensure(skill, settings_.base_model);
        module.updated_unix_ms = memory::now_unix_ms();
        expert_registry_.save(module);
    }
    queue_request(LearningRequest{
        .skill = std::move(skill),
        .candidate_id = std::nullopt,
        .kind = CandidateKind::lora_overlay,
    });
}

void ContinualLearningService::queue_request(LearningRequest request) {
    try {
        const auto key = request.candidate_id.has_value()
            ? "candidate:" + *request.candidate_id
            : "skill:" + request.skill + ':' + std::string(to_string(request.kind));
        const auto durable = persistent_queue_.submit(
            key,
            request.skill,
            request.candidate_id.value_or(""));
        request.job_id = durable.job.id;
    } catch (...) {
        // Never launch an update that could not first be serialized.
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (request.candidate_id.has_value()) {
            std::erase_if(queue_, [&request](const LearningRequest& pending) {
                return pending.skill == request.skill &&
                       !pending.candidate_id.has_value();
            });
            const auto duplicate = std::ranges::any_of(
                queue_, [&request](const LearningRequest& pending) {
                    return pending.candidate_id == request.candidate_id;
                });
            if (!duplicate) {
                queue_.push_front(std::move(request));
            }
        } else {
            const auto skill_already_queued = std::ranges::any_of(
                queue_, [&request](const LearningRequest& pending) {
                    return pending.skill == request.skill;
                });
            if (!skill_already_queued) {
                queue_.push_back(std::move(request));
            }
        }
    }
    condition_.notify_all();
}

void ContinualLearningService::pause(const bool value) {
    if (value) {
        const auto phase = improvement_session_phase_.load(std::memory_order_acquire);
        if (phase == ImprovementSessionPhase::running) {
            const auto now = memory::now_unix_ms();
            const auto deadline = improvement_session_deadline_unix_ms_.load(std::memory_order_acquire);
            improvement_session_remaining_ms_.store(
                std::max<std::int64_t>(0, deadline - now),
                std::memory_order_release);
            improvement_session_phase_.store(ImprovementSessionPhase::paused, std::memory_order_release);
        }
        paused_.store(true, std::memory_order_release);
        persist_improvement_session();
        publish_control_files();
        condition_.notify_all();
        return;
    }
    static_cast<void>(resume_improvement_session());
}

void ContinualLearningService::start_improvement_session(const std::chrono::minutes budget) {
    const auto maximum = std::max(std::chrono::minutes(1), settings_.maximum_improvement_session);
    const auto bounded = std::clamp(budget, std::chrono::minutes(1), maximum);
    const auto now = memory::now_unix_ms();
    const auto budget_ms = std::chrono::duration_cast<std::chrono::milliseconds>(bounded).count();
    improvement_session_budget_ms_.store(budget_ms, std::memory_order_release);
    improvement_session_remaining_ms_.store(budget_ms, std::memory_order_release);
    improvement_session_started_unix_ms_.store(now, std::memory_order_release);
    improvement_session_deadline_unix_ms_.store(now + budget_ms, std::memory_order_release);
    improvement_session_phase_.store(ImprovementSessionPhase::running, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    persist_improvement_session();
    publish_control_files();
    condition_.notify_all();
}

bool ContinualLearningService::resume_improvement_session() {
    if (improvement_session_phase_.load(std::memory_order_acquire) !=
        ImprovementSessionPhase::paused) {
        return false;
    }
    const auto remaining = improvement_session_remaining_ms_.load(std::memory_order_acquire);
    if (remaining <= 0) {
        improvement_session_phase_.store(
            ImprovementSessionPhase::time_limit_reached,
            std::memory_order_release);
        paused_.store(true, std::memory_order_release);
        persist_improvement_session();
        publish_control_files();
        return false;
    }
    const auto now = memory::now_unix_ms();
    improvement_session_deadline_unix_ms_.store(now + remaining, std::memory_order_release);
    improvement_session_phase_.store(ImprovementSessionPhase::running, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    persist_improvement_session();
    publish_control_files();
    condition_.notify_all();
    return true;
}

void ContinualLearningService::cancel_improvement_session() {
    improvement_session_phase_.store(ImprovementSessionPhase::ended, std::memory_order_release);
    improvement_session_remaining_ms_.store(0, std::memory_order_release);
    improvement_session_deadline_unix_ms_.store(0, std::memory_order_release);
    paused_.store(true, std::memory_order_release);
    persist_improvement_session();
    publish_control_files();
    condition_.notify_all();
}

ImprovementSessionSnapshot ContinualLearningService::improvement_session_snapshot() {
    auto phase = improvement_session_phase_.load(std::memory_order_acquire);
    const auto now = memory::now_unix_ms();
    auto remaining = improvement_session_remaining_ms_.load(std::memory_order_acquire);
    const auto deadline = improvement_session_deadline_unix_ms_.load(std::memory_order_acquire);
    if (phase == ImprovementSessionPhase::running) {
        remaining = std::max<std::int64_t>(0, deadline - now);
        if (remaining == 0) {
            improvement_session_phase_.store(
                ImprovementSessionPhase::time_limit_reached,
                std::memory_order_release);
            improvement_session_remaining_ms_.store(0, std::memory_order_release);
            paused_.store(true, std::memory_order_release);
            phase = ImprovementSessionPhase::time_limit_reached;
            persist_improvement_session();
            publish_control_files();
            condition_.notify_all();
        }
    }
    return ImprovementSessionSnapshot{
        .phase = phase,
        .budget = std::chrono::milliseconds(
            std::max<std::int64_t>(0, improvement_session_budget_ms_.load(std::memory_order_acquire))),
        .remaining = std::chrono::milliseconds(std::max<std::int64_t>(0, remaining)),
        .started_unix_ms = improvement_session_started_unix_ms_.load(std::memory_order_acquire),
        .deadline_unix_ms = deadline,
    };
}

void ContinualLearningService::set_interactive_busy(const bool value) {
    if (value) {
        static_cast<void>(interactive_leases_.fetch_add(1U, std::memory_order_acq_rel));
    } else {
        auto current = interactive_leases_.load(std::memory_order_acquire);
        while (current != 0U &&
               !interactive_leases_.compare_exchange_weak(
                   current,
                   current - 1U,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
    }
    persistent_queue_.set_interactive_busy(interactive_busy());
    publish_control_files();
    condition_.notify_all();
}

void ContinualLearningService::request_stop() {
    worker_.request_stop();
    condition_.notify_all();
}

void ContinualLearningService::set_python_executable(std::filesystem::path path) {
    std::lock_guard lock(mutex_);
    settings_.python_executable = std::move(path);
}

void ContinualLearningService::set_base_model(std::string model) {
    std::lock_guard lock(mutex_);
    settings_.base_model = std::move(model);
}

bool ContinualLearningService::rollback_skill(const std::string_view skill) {
    std::lock_guard promotion_lock(promotion_mutex_);

    ExpertModule snapshot;
    LearningSettings settings;
    {
        std::lock_guard lock(mutex_);
        const auto* module = expert_registry_.find(skill);
        if (module == nullptr ||
            (module->previous_adapter_path.empty() && module->previous_module_path.empty() &&
             module->previous_state == ExpertState::memory_only)) {
            return false;
        }
        snapshot = *module;
        settings = settings_;
    }

    const auto internal = snapshot.base_model.starts_with("threadspire://");
    if (internal) {
        const auto target_generation = internal_generation_name(snapshot.previous_module_path);
        const auto current_generation = internal_generation_name(snapshot.module_path);
        if (target_generation.empty() || current_generation.empty() ||
            !std::filesystem::is_regular_file(settings.python_executable) ||
            !std::filesystem::is_regular_file(settings.worker_script)) {
            return false;
        }

        const auto rollback_result = platform::run_process(
            platform::ProcessSpec{
                .executable = settings.python_executable,
                .arguments = {
                    settings.worker_script.string(),
                    "activate-internal",
                    "--state-root", settings.state_root.string(),
                    "--generation", target_generation,
                    "--expected-current", current_generation,
                },
                .working_directory = settings.state_root,
                .log_file = settings.state_root / "logs" / "internal-rollback.log",
                .append_log = true,
            },
            [] { return false; });
        if (!rollback_result.launched || rollback_result.exit_code != 0) {
            return false;
        }
    }

    std::lock_guard lock(mutex_);
    auto* module = expert_registry_.find(skill);
    if (module == nullptr || module->generation != snapshot.generation ||
        module->state != snapshot.state ||
        module->adapter_path != snapshot.adapter_path ||
        module->module_path != snapshot.module_path ||
        module->previous_adapter_path != snapshot.previous_adapter_path ||
        module->previous_module_path != snapshot.previous_module_path) {
        return false;
    }

    const auto current_state = module->state;
    const auto current_adapter = module->adapter_path;
    const auto current_module = module->module_path;
    module->state = module->previous_state;
    module->adapter_path = module->previous_adapter_path;
    module->module_path = module->previous_module_path;
    module->previous_state = current_state;
    module->previous_adapter_path = current_adapter;
    module->previous_module_path = current_module;
    module->promoted_candidate_id = internal
        ? "manual-internal-generation-rollback"
        : "manual-rollback";
    ++module->generation;
    module->updated_unix_ms = memory::now_unix_ms();
    expert_registry_.save(*module);
    return true;
}

bool ContinualLearningService::paused() const noexcept {
    if (paused_.load(std::memory_order_acquire) ||
        improvement_session_phase_.load(std::memory_order_acquire) !=
            ImprovementSessionPhase::running) {
        return true;
    }
    const auto deadline = improvement_session_deadline_unix_ms_.load(std::memory_order_acquire);
    return deadline <= 0 || memory::now_unix_ms() >= deadline;
}

bool ContinualLearningService::interactive_busy() const noexcept {
    return interactive_leases_.load(std::memory_order_acquire) != 0U;
}

std::uint32_t ContinualLearningService::interactive_lease_count() const noexcept {
    return interactive_leases_.load(std::memory_order_acquire);
}

std::vector<LearningJobStatus> ContinualLearningService::statuses() const {
    std::lock_guard lock(mutex_);
    return statuses_;
}

LearningPipelineSnapshot ContinualLearningService::pipeline_snapshot() const {
    std::lock_guard lock(mutex_);
    LearningPipelineSnapshot snapshot{
        .queued_updates = queue_.size(),
        .active_job = std::nullopt,
    };
    const auto active = std::ranges::find_if(
        statuses_.rbegin(),
        statuses_.rend(),
        [](const LearningJobStatus& status) {
            return !terminal_state(status.state);
        });
    if (active != statuses_.rend()) {
        snapshot.active_job = *active;
    }
    return snapshot;
}

std::vector<ExpertModule> ContinualLearningService::expert_snapshot() const {
    std::lock_guard lock(mutex_);
    return expert_registry_.modules();
}

std::vector<CandidateRecord> ContinualLearningService::candidate_snapshot() const {
    std::lock_guard lock(mutex_);
    return candidate_registry_.candidates();
}

LearningSettings ContinualLearningService::settings_snapshot() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

const memory::EpisodeStore& ContinualLearningService::memory_store() const noexcept {
    return memory_store_;
}

void ContinualLearningService::recover_persisted_requests() {
    for (const auto& job : persistent_queue_.snapshot()) {
        if (job.state != TrainingState::queued &&
            job.state != TrainingState::recoverable_failed) {
            continue;
        }
        LearningRequest request{
            .job_id = job.id,
            .skill = job.skill,
            .candidate_id = job.candidate_id.empty()
                ? std::nullopt
                : std::optional<std::string>(job.candidate_id),
            .kind = CandidateKind::lora_overlay,
        };
        if (request.candidate_id.has_value()) {
            if (const auto* candidate = candidate_registry_.find(*request.candidate_id);
                candidate != nullptr) {
                request.kind = candidate->kind;
            }
        } else if (const auto separator = job.coalesce_key.rfind(':');
                   separator != std::string::npos) {
            request.kind = candidate_kind_from_string(
                std::string_view(job.coalesce_key).substr(separator + 1U));
        }
        queue_.push_back(std::move(request));
    }
}

void ContinualLearningService::transition_durable_phase(
    const std::string_view job_id,
    const TrainingState expected,
    const TrainingState next,
    const std::filesystem::path& checkpoint_path,
    std::string message) noexcept {
    if (job_id.empty()) {
        return;
    }
    try {
        static_cast<void>(persistent_queue_.transition(
            job_id, expected, next, checkpoint_path, std::move(message)));
    } catch (...) {
        // Failure to publish state must not replace or stop the active model.
    }
}
void ContinualLearningService::recover_interrupted_candidates() {
    const auto interrupted = candidate_registry_.mark_inflight_interrupted();
    if (!settings_.resume_interrupted_candidates) {
        return;
    }
    for (auto candidate = interrupted.rbegin(); candidate != interrupted.rend(); ++candidate) {
        queue_request(LearningRequest{
            .skill = candidate->skill,
            .candidate_id = candidate->id,
            .kind = candidate->kind,
        });
    }
}

void ContinualLearningService::worker_loop(const std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        LearningRequest request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stop_token, [this] {
                return !queue_.empty() && !paused() && !interactive_busy();
            });
            if (stop_token.stop_requested()) {
                break;
            }
            request = std::move(queue_.front());
            queue_.pop_front();
        }
        try {
            if (!request.job_id.empty()) {
                const auto prepared = persistent_queue_.transition(
                    request.job_id, TrainingState::queued, TrainingState::preparing) ||
                    persistent_queue_.transition(
                        request.job_id, TrainingState::recoverable_failed, TrainingState::preparing);
                if (!prepared) {
                    continue;
                }
            }
            process_request(request, stop_token);
        } catch (const std::exception& error) {
            if (request.job_id.empty()) {
                continue;
            }
            const auto message = std::string{
                "serialized training worker recovered from an unexpected failure: "} + error.what();
            try {
                static_cast<void>(persistent_queue_.transition(
                    request.job_id, TrainingState::queued, TrainingState::preparing));
                for (const auto expected : {
                         TrainingState::preparing,
                         TrainingState::training,
                         TrainingState::evaluating,
                         TrainingState::promoting}) {
                    if (persistent_queue_.transition(
                            request.job_id,
                            expected,
                            TrainingState::recoverable_failed,
                            {},
                            message)) {
                        break;
                    }
                }
            } catch (...) {
                // The worker remains alive and the last promoted model keeps serving.
                // A later restart can recover any still-inflight durable record.
            }
        } catch (...) {
            if (!request.job_id.empty()) {
                try {
                    static_cast<void>(persistent_queue_.transition(
                        request.job_id,
                        TrainingState::training,
                        TrainingState::recoverable_failed,
                        {},
                        "serialized training worker recovered from a non-standard failure"));
                } catch (...) {
                }
            }
        }
    }
}

void ContinualLearningService::process_request(
    const LearningRequest& request,
    const std::stop_token stop_token) {
    ExpertModule active_module;
    LearningSettings settings;
    {
        std::lock_guard lock(mutex_);
        const auto* existing = expert_registry_.find(request.skill);
        if (existing == nullptr) {
            if (!request.job_id.empty()) {
                static_cast<void>(persistent_queue_.transition(
                    request.job_id,
                    TrainingState::preparing,
                    TrainingState::recoverable_failed,
                    {},
                    "skill registry entry disappeared before preparation"));
            }
            return;
        }
        active_module = *existing;
        settings = settings_;
    }

    const auto job_id = request.job_id.empty() ? make_job_id(request.skill) : request.job_id;
    LearningJobStatus status{
        .id = job_id,
        .skill = request.skill,
        .candidate_id = request.candidate_id.value_or(""),
        .state = LearningJobState::queued,
        .message = "candidate queued beside the active skill",
        .log_path = settings.state_root / "jobs" / (job_id + ".log"),
        .progress_path = settings.state_root / "jobs" / job_id / "progress.json",
        .exit_code = -1,
        .updated_unix_ms = memory::now_unix_ms(),
    };
    if (request.candidate_id.has_value()) {
        std::lock_guard lock(mutex_);
        if (const auto* candidate = candidate_registry_.find(*request.candidate_id);
            candidate != nullptr) {
            status.candidate_id = candidate->id;
            status.log_path = candidate->log_path;
            status.progress_path = candidate->job_directory / "progress.json";
        }
    }
    update_status(status);
    if (!request.job_id.empty() && !persistent_queue_.transition(
            request.job_id,
            TrainingState::preparing,
            TrainingState::training,
            status.progress_path,
            "training shadow candidate while the last promoted model keeps serving")) {
        return;
    }

    bool completed = false;
    if (request.kind == CandidateKind::full_rank_skill &&
        !active_module.base_model.starts_with("threadspire://")) {
        completed = run_consolidation(
            settings, active_module, status, request.candidate_id, request.job_id, stop_token);
    } else if (active_module.base_model.starts_with("threadspire://")) {
        completed = run_internal_update(
            settings, active_module, status, request.candidate_id, request.job_id, stop_token);
    } else {
        completed = run_lora_update(
            settings, active_module, status, request.candidate_id, request.job_id, stop_token);
        if (completed && settings.automatic_consolidation &&
            expert_registry_.ready_for_consolidation(active_module, settings.policy)) {
            completed = run_consolidation(
                settings, active_module, status, std::nullopt, request.job_id, stop_token);
        }
    }

    if (request.job_id.empty()) {
        return;
    }
    bool rejected = status.message.find("reject") != std::string::npos;
    if (!status.candidate_id.empty()) {
        std::lock_guard lock(mutex_);
        if (const auto* candidate = candidate_registry_.find(status.candidate_id);
            candidate != nullptr) {
            rejected = rejected || candidate->state == CandidateState::rejected;
        }
    }
    if (completed) {
        transition_durable_phase(
            request.job_id, TrainingState::training, TrainingState::evaluating,
            status.progress_path, "candidate training complete; evaluating held-out gates");
        transition_durable_phase(
            request.job_id, TrainingState::evaluating, TrainingState::promoting,
            status.progress_path, "candidate passed evaluation; verifying isolated candidate host");
        transition_durable_phase(
            request.job_id, TrainingState::promoting, TrainingState::completed,
            status.progress_path, "candidate completed without replacing a working model early");
    } else if (rejected) {
        transition_durable_phase(
            request.job_id, TrainingState::training, TrainingState::evaluating,
            status.progress_path, "candidate reached evaluation");
        transition_durable_phase(
            request.job_id, TrainingState::evaluating, TrainingState::rejected,
            status.progress_path, status.message);
        transition_durable_phase(
            request.job_id, TrainingState::promoting, TrainingState::rejected,
            status.progress_path, status.message);
    } else {
        const auto message = status.message.empty()
            ? "candidate failed recoverably; checkpoint retained"
            : status.message;
        transition_durable_phase(
            request.job_id, TrainingState::training, TrainingState::recoverable_failed,
            status.progress_path, message);
        transition_durable_phase(
            request.job_id, TrainingState::evaluating, TrainingState::recoverable_failed,
            status.progress_path, message);
        transition_durable_phase(
            request.job_id, TrainingState::promoting, TrainingState::recoverable_failed,
            status.progress_path, message);
    }
}
bool ContinualLearningService::run_internal_update(
    const LearningSettings& settings,
    ExpertModule& active_module,
    LearningJobStatus& status,
    const std::optional<std::string> resume_candidate_id,
    const std::string_view durable_job_id,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        status.state = LearningJobState::cancelled;
        status.message = "cancelled before internal candidate training";
        update_status(status);
        return false;
    }
    if (!std::filesystem::is_regular_file(settings.python_executable) ||
        !std::filesystem::is_regular_file(settings.worker_script)) {
        status.state = LearningJobState::failed;
        status.message = "Python/PyTorch backend is not installed or selected";
        update_status(status);
        return false;
    }

    CandidateRecord candidate;
    if (resume_candidate_id.has_value()) {
        std::optional<CandidateRecord> stored_candidate;
        {
            std::lock_guard lock(mutex_);
            if (const auto* stored = candidate_registry_.find(*resume_candidate_id);
                stored != nullptr) {
                stored_candidate = *stored;
            }
        }
        if (!stored_candidate.has_value()) {
            status.state = LearningJobState::failed;
            status.message = "resumable internal candidate record was not found";
            update_status(status);
            return false;
        }
        candidate = *stored_candidate;
    } else {
        {
            std::lock_guard lock(mutex_);
            candidate = candidate_registry_.create(
                active_module.skill,
                active_module.base_model,
                CandidateKind::full_rank_skill,
                active_module.generation,
                {},
                {},
                {});
        }
        candidate.training_episode_count = active_module.episodes_since_update;
        const auto root = candidate_root(settings, candidate.id);
        candidate.artifact_path = settings.state_root / "models" / "internal";
        candidate.job_directory = root / "job";
        candidate.log_path = root / "internal-training.log";
        candidate.message = "internal skill candidate staged beside the active generation";
        commit_candidate(candidate);
    }

    status.candidate_id = candidate.id;
    status.log_path = candidate.log_path;
    status.progress_path = candidate.job_directory / "progress.json";
    std::filesystem::create_directories(candidate.job_directory);

    resource::TrainingBudget budget;
    while (!stop_token.stop_requested()) {
        budget = resource_governor_.plan_training(
            resource::sample_system_resources(),
            interactive_busy(),
            settings.micro_steps,
            settings.maximum_sequence_tokens,
            settings.lora_rank);
        if (budget.permitted) {
            break;
        }
        status.state = LearningJobState::waiting_for_resources;
        status.message = budget.reason;
        update_status(status);
        std::this_thread::sleep_for(budget.retry_after);
    }
    if (stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = "internal candidate stopped while waiting for resources";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::training;
    candidate.message = "training a shadow full-weight internal skill module";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::training_internal;
    status.message = candidate.message;
    update_status(status);

    const auto metrics_path = candidate.job_directory / "metrics.json";
    const auto training_scope = internal_training_scope(active_module.skill);
    std::vector<std::string> arguments{
        settings.worker_script.string(),
        "train-internal",
        "--state-root", settings.state_root.string(),
        "--seed-dataset", settings.seed_dataset.string(),
        "--episodes", memory_store_.path().string(),
        "--anchors", settings.anchor_dataset.string(),
        "--scope", training_scope,
        "--max-steps", std::to_string(std::max<std::size_t>(4U, budget.max_steps)),
        "--learning-rate", std::to_string(settings.learning_rate),
        "--batch-size", "2",
        "--accumulation-steps", "2",
        "--checkpoint-steps", std::to_string(settings.checkpoint_steps),
        "--control-dir", (settings.state_root / "control").string(),
        "--metrics-output", metrics_path.string(),
        "--progress-file", status.progress_path.string(),
        "--defer-promotion",
    };
    if (settings.require_cuda) {
        arguments.emplace_back("--require-cuda");
    }

    std::optional<std::chrono::steady_clock::time_point> foreground_since;
    const auto result = platform::run_process(
        platform::ProcessSpec{
            .executable = settings.python_executable,
            .arguments = std::move(arguments),
            .working_directory = settings.state_root,
            .log_file = candidate.log_path,
            .append_log = resume_candidate_id.has_value(),
        },
        [this, stop_token, &foreground_since, &settings] {
            if (stop_token.stop_requested()) {
                return true;
            }
            const auto preemption_requested = interactive_busy() || paused();
            if (!preemption_requested) {
                foreground_since.reset();
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (!foreground_since.has_value()) {
                foreground_since = now;
                return false;
            }
            return now - *foreground_since >= settings.inference_preemption_grace;
        });
    clear_training_markers(settings.state_root);

    status.exit_code = result.exit_code;
    const auto resumable_preemption = !stop_token.stop_requested() &&
        (result.exit_code == preempted_exit_code ||
         (result.cancelled && (interactive_busy() || paused())));
    if (result.cancelled || result.exit_code == preempted_exit_code || stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = resumable_preemption
            ? "internal candidate preempted for foreground inference; checkpoint retained"
            : "internal candidate interrupted; active generation unchanged";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        if (resumable_preemption) {
            queue_request(LearningRequest{
                .skill = candidate.skill,
                .candidate_id = candidate.id,
                .kind = CandidateKind::full_rank_skill,
            });
        }
        return false;
    }
    if (!result.launched || result.exit_code != 0) {
        candidate.state = CandidateState::rejected;
        candidate.message = result.error.empty()
            ? "internal candidate training failed; active generation unchanged"
            : result.error;
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    transition_durable_phase(
        durable_job_id, TrainingState::training, TrainingState::evaluating,
        status.progress_path, "candidate training complete; evaluating held-out gates");
    candidate.validation_score = read_metric(metrics_path, "validation_score", 0.0);
    candidate.regression_score = read_metric(metrics_path, "regression_score", 1.0);
    const auto passed_evaluation = read_metric(
        metrics_path,
        "passed_evaluation",
        0.0) > 0.5;
    if (!passed_evaluation) {
        candidate.state = CandidateState::rejected;
        candidate.message = "internal candidate did not improve its held-out loss";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    const auto internal_generation = read_string_metric(metrics_path, "generation");
    const auto internal_parent = read_string_metric(metrics_path, "parent_generation");
    if (internal_generation.empty() || internal_parent.empty()) {
        candidate.state = CandidateState::rejected;
        candidate.message = "internal candidate metrics did not identify its generation";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.artifact_path = internal_generation_path(
        settings.state_root, internal_generation);
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    transition_durable_phase(
        durable_job_id, TrainingState::evaluating, TrainingState::promoting,
        status.progress_path, "candidate passed evaluation; verifying isolated candidate host");

    std::lock_guard promotion_lock(promotion_mutex_);
    auto registry_stale = false;
    {
        std::lock_guard lock(mutex_);
        auto* current = expert_registry_.find(candidate.skill);
        if (current == nullptr || current->generation != candidate.parent_generation) {
            candidate.state = CandidateState::rejected;
            candidate.message = "internal candidate became stale before registry promotion";
            candidate.updated_unix_ms = memory::now_unix_ms();
            candidate_registry_.save(candidate);
            status.state = LearningJobState::failed;
            status.message = candidate.message;
            registry_stale = true;
        }
    }
    if (registry_stale) {
        update_status(status);
        return false;
    }

    const AtomicPromotionCoordinator promotion_coordinator;
    const auto atomic_promotion = promotion_coordinator.promote(
        AtomicPromotionRequest{
            .active_pointer = settings.state_root / "models" / "internal" / "active.json",
            .candidate_directory = candidate.artifact_path,
            .candidate_generation = internal_generation,
            .expected_parent_generation = internal_parent,
        },
        [&settings, &candidate, &internal_generation, &metrics_path, stop_token](
            const std::filesystem::path&) {
            return verify_internal_candidate_host(
                settings, candidate, internal_generation, metrics_path, stop_token);
        },
        [&settings, &candidate, &internal_generation, &internal_parent] {
            const auto promotion_result = platform::run_process(
                platform::ProcessSpec{
                    .executable = settings.python_executable,
                    .arguments = {
                        settings.worker_script.string(),
                        "promote-internal",
                        "--state-root", settings.state_root.string(),
                        "--generation", internal_generation,
                        "--expected-parent", internal_parent,
                    },
                    .working_directory = settings.state_root,
                    .log_file = candidate.log_path,
                    .append_log = true,
                },
                [] { return false; });
            return CandidatePromotionCommitResult{
                .succeeded = promotion_result.launched && promotion_result.exit_code == 0,
                .diagnostics = promotion_result.error.empty()
                    ? (promotion_result.exit_code == 0
                        ? "candidate manifest and active pointer committed atomically"
                        : "candidate compare-and-swap promotion command failed")
                    : promotion_result.error,
            };
        });
    if (atomic_promotion.outcome != PromotionOutcome::promoted) {
        const auto recoverable =
            atomic_promotion.outcome == PromotionOutcome::recoverable_failed;
        candidate.state = recoverable
            ? CandidateState::interrupted
            : CandidateState::rejected;
        candidate.message = atomic_promotion.diagnostics.empty()
            ? (recoverable
                ? "candidate host failed recoverably; active model remains online"
                : "candidate was rejected before atomic promotion")
            : atomic_promotion.diagnostics;
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = recoverable
            ? LearningJobState::interrupted
            : LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }
    registry_stale = false;
    {
        std::lock_guard lock(mutex_);
        auto* current = expert_registry_.find(candidate.skill);
        if (current == nullptr || current->generation != candidate.parent_generation) {
            candidate.state = CandidateState::rejected;
            candidate.message = "internal candidate registry changed during atomic promotion";
            candidate.updated_unix_ms = memory::now_unix_ms();
            candidate_registry_.save(candidate);
            status.state = LearningJobState::failed;
            status.message = candidate.message;
            registry_stale = true;
        } else {
            current->previous_state = internal_parent == "g000000"
                ? current->state
                : ExpertState::full_rank_active;
            current->previous_module_path = internal_parent == "g000000"
                ? std::filesystem::path{}
                : internal_generation_path(settings.state_root, internal_parent);
            current->previous_adapter_path = current->adapter_path;
            current->state = ExpertState::full_rank_active;
            current->adapter_path.clear();
            current->module_path = internal_generation_path(
                settings.state_root, internal_generation);
            current->promoted_candidate_id = candidate.id;
            ++current->generation;
            ++current->lora_generations;
            current->episodes_since_update =
                current->episodes_since_update > candidate.training_episode_count
                ? current->episodes_since_update -
                    static_cast<std::size_t>(candidate.training_episode_count)
                : 0U;
            current->validation_score = candidate.validation_score;
            current->regression_score = candidate.regression_score;
            current->updated_unix_ms = memory::now_unix_ms();
            expert_registry_.save(*current);
            active_module = *current;
        }
    }
    if (registry_stale) {
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::promoted;
    candidate.message = "internal skill generation promoted; prior active weights remain rollback-ready";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::succeeded;
    status.message = candidate.message;
    update_status(status);
    return true;
}

bool ContinualLearningService::run_lora_update(
    const LearningSettings& settings,
    ExpertModule& active_module,
    LearningJobStatus& status,
    const std::optional<std::string> resume_candidate_id,
    const std::string_view durable_job_id,
    const std::stop_token stop_token) {
    if (stop_token.stop_requested()) {
        status.state = LearningJobState::cancelled;
        status.message = "cancelled before candidate training";
        update_status(status);
        return false;
    }
    if (!std::filesystem::is_regular_file(settings.python_executable) ||
        !std::filesystem::is_regular_file(settings.worker_script)) {
        status.state = LearningJobState::failed;
        status.message = "Python/PyTorch backend is not installed or selected";
        update_status(status);
        return false;
    }

    CandidateRecord candidate;
    if (resume_candidate_id.has_value()) {
        std::optional<CandidateRecord> stored_candidate;
        {
            std::lock_guard lock(mutex_);
            if (const auto* stored = candidate_registry_.find(*resume_candidate_id);
                stored != nullptr) {
                stored_candidate = *stored;
            }
        }
        if (!stored_candidate.has_value() ||
            stored_candidate->kind != CandidateKind::lora_overlay) {
            status.state = LearningJobState::failed;
            status.message = "resumable LoRA candidate record was not found";
            update_status(status);
            return false;
        }
        candidate = *stored_candidate;
    } else {
        {
            std::lock_guard lock(mutex_);
            candidate = candidate_registry_.create(
                active_module.skill,
                active_module.base_model,
                CandidateKind::lora_overlay,
                active_module.generation,
                {},
                {},
                {});
        }
        candidate.training_episode_count = active_module.episodes_since_update;
        const auto root = candidate_root(settings, candidate.id);
        candidate.artifact_path = root / "adapter";
        candidate.job_directory = root / "job";
        candidate.log_path = root / "training.log";
        candidate.message = "low-rank skill candidate staged; active skill remains untouched";
        commit_candidate(candidate);
    }

    status.candidate_id = candidate.id;
    status.log_path = candidate.log_path;
    std::filesystem::create_directories(candidate.job_directory);

    resource::TrainingBudget budget;
    while (!stop_token.stop_requested()) {
        budget = resource_governor_.plan_training(
            resource::sample_system_resources(),
            interactive_busy(),
            settings.micro_steps,
            settings.maximum_sequence_tokens,
            settings.lora_rank);
        if (budget.permitted) {
            break;
        }
        status.state = LearningJobState::waiting_for_resources;
        status.message = budget.reason;
        update_status(status);
        std::this_thread::sleep_for(budget.retry_after);
    }
    if (stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = "candidate stopped while waiting for resources";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::training;
    candidate.message = "training a low-rank skill candidate beside the active intelligence";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::training_lora;
    status.message = candidate.message;
    update_status(status);

    std::vector<std::string> arguments{
        settings.worker_script.string(),
        "train-lora",
        "--base-model", active_module.base_model,
        "--episodes", memory_store_.path().string(),
        "--skill", active_module.skill,
        "--output", candidate.artifact_path.string(),
        "--job-dir", candidate.job_directory.string(),
        "--control-dir", (settings.state_root / "control").string(),
        "--rank", std::to_string(budget.lora_rank),
        "--alpha", std::to_string(settings.lora_alpha),
        "--max-steps", std::to_string(budget.max_steps),
        "--max-sequence-tokens", std::to_string(budget.maximum_sequence_tokens),
        "--checkpoint-steps", std::to_string(settings.checkpoint_steps),
        "--learning-rate", std::to_string(settings.learning_rate),
    };
    if (!settings.seed_dataset.empty() &&
        std::filesystem::is_regular_file(settings.seed_dataset)) {
        arguments.emplace_back("--seed-dataset");
        arguments.push_back(settings.seed_dataset.string());
        arguments.emplace_back("--seed-replay-limit");
        arguments.push_back(active_module.generation == 0 ? "96" : "24");
    }
    if (!settings.anchor_dataset.empty() &&
        std::filesystem::is_regular_file(settings.anchor_dataset)) {
        arguments.emplace_back("--anchors");
        arguments.push_back(settings.anchor_dataset.string());
    }
    if (settings.require_cuda) {
        arguments.emplace_back("--require-cuda");
    }
    if (!active_module.module_path.empty()) {
        arguments.emplace_back("--module");
        arguments.push_back(active_module.module_path.string());
    }
    if (!active_module.adapter_path.empty()) {
        arguments.emplace_back("--resume-adapter");
        arguments.push_back(active_module.adapter_path.string());
    }

    std::optional<std::chrono::steady_clock::time_point> foreground_since;
    const auto result = platform::run_process(
        platform::ProcessSpec{
            .executable = settings.python_executable,
            .arguments = std::move(arguments),
            .working_directory = settings.state_root,
            .log_file = candidate.log_path,
            .append_log = resume_candidate_id.has_value(),
        },
        [this, stop_token, &foreground_since, &settings] {
            if (stop_token.stop_requested()) {
                return true;
            }
            const auto preemption_requested = interactive_busy() || paused();
            if (!preemption_requested) {
                foreground_since.reset();
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (!foreground_since.has_value()) {
                foreground_since = now;
                return false;
            }
            return now - *foreground_since >= settings.inference_preemption_grace;
        });
    clear_training_markers(settings.state_root);

    status.exit_code = result.exit_code;
    const auto resumable_preemption = !stop_token.stop_requested() &&
        (result.exit_code == preempted_exit_code ||
         (result.cancelled && (interactive_busy() || paused())));
    if (result.cancelled || result.exit_code == preempted_exit_code || stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = resumable_preemption
            ? "candidate preempted for foreground inference or pause; checkpoint retained and resume queued"
            : "candidate interrupted at a checkpoint; active skill was not changed";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        if (resumable_preemption) {
            queue_request(LearningRequest{
                .skill = candidate.skill,
                .candidate_id = candidate.id,
                .kind = CandidateKind::lora_overlay,
            });
        }
        return false;
    }
    if (!result.launched || result.exit_code != 0) {
        candidate.state = CandidateState::rejected;
        candidate.message = result.error.empty()
            ? "candidate training failed; active skill was not changed"
            : result.error;
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    transition_durable_phase(
        durable_job_id, TrainingState::training, TrainingState::evaluating,
        status.progress_path, "candidate training complete; evaluating held-out gates");
    candidate.state = CandidateState::evaluating;
    candidate.message = "evaluating skill improvement and held-out regression";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::validating;
    status.message = candidate.message;
    update_status(status);

    candidate.validation_score = read_metric(
        candidate.job_directory / "metrics.json", "validation_score", 0.0);
    candidate.regression_score = read_metric(
        candidate.job_directory / "metrics.json", "regression_score", 1.0);
    if (candidate.validation_score < settings.policy.minimum_validation_score ||
        candidate.regression_score > settings.policy.maximum_regression) {
        candidate.state = CandidateState::rejected;
        candidate.message = "candidate rejected by validation or regression gate";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        // Keep the accumulated evidence. The next independently verified episode may
        // trigger a new candidate instead of discarding the entire learning window.
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    transition_durable_phase(
        durable_job_id, TrainingState::evaluating, TrainingState::promoting,
        status.progress_path, "candidate passed evaluation; committing adapter with parent check");
    bool promoted = false;
    {
        std::lock_guard lock(mutex_);
        auto* current = expert_registry_.find(candidate.skill);
        if (current != nullptr && current->generation == candidate.parent_generation) {
            current->previous_state = current->state;
            current->previous_adapter_path = current->adapter_path;
            current->previous_module_path = current->module_path;
            current->adapter_path = candidate.artifact_path;
            current->state = ExpertState::lora_active;
            current->episodes_since_update =
                current->episodes_since_update > candidate.training_episode_count
                ? current->episodes_since_update -
                    static_cast<std::size_t>(candidate.training_episode_count)
                : 0U;
            ++current->lora_generations;
            ++current->generation;
            current->validation_score = candidate.validation_score;
            current->regression_score = candidate.regression_score;
            current->promoted_candidate_id = candidate.id;
            current->updated_unix_ms = memory::now_unix_ms();
            expert_registry_.save(*current);
            active_module = *current;
            promoted = true;
        }
    }
    if (!promoted) {
        candidate.state = CandidateState::rejected;
        candidate.message =
            "candidate became stale because the active skill changed during training; active intelligence was not modified";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::promoted;
    candidate.message = "candidate atomically promoted; prior active version is rollback-ready";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::succeeded;
    status.message = candidate.message;
    update_status(status);
    return true;
}

bool ContinualLearningService::run_consolidation(
    const LearningSettings& settings,
    ExpertModule& active_module,
    LearningJobStatus& status,
    const std::optional<std::string> resume_candidate_id,
    const std::string_view durable_job_id,
    const std::stop_token stop_token) {
    if (active_module.adapter_path.empty()) {
        return false;
    }

    CandidateRecord candidate;
    if (resume_candidate_id.has_value()) {
        std::optional<CandidateRecord> stored_candidate;
        {
            std::lock_guard lock(mutex_);
            if (const auto* stored = candidate_registry_.find(*resume_candidate_id);
                stored != nullptr) {
                stored_candidate = *stored;
            }
        }
        if (!stored_candidate.has_value() ||
            stored_candidate->kind != CandidateKind::full_rank_skill) {
            status.state = LearningJobState::failed;
            status.message = "resumable full-rank candidate record was not found";
            update_status(status);
            return false;
        }
        candidate = *stored_candidate;
    } else {
        {
            std::lock_guard lock(mutex_);
            candidate = candidate_registry_.create(
                active_module.skill,
                active_module.base_model,
                CandidateKind::full_rank_skill,
                active_module.generation,
                {},
                {},
                {});
        }
        candidate.training_episode_count = active_module.episodes_since_update;
        const auto root = candidate_root(settings, candidate.id);
        candidate.artifact_path = root / "skill-module";
        candidate.job_directory = root / "job";
        candidate.log_path = root / "consolidation.log";
        candidate.message = "full-rank skill candidate staged; active LoRA remains live";
        commit_candidate(candidate);
    }

    status.candidate_id = candidate.id;
    status.log_path = candidate.log_path;
    std::filesystem::create_directories(candidate.job_directory);

    resource::TrainingBudget budget;
    while (!stop_token.stop_requested()) {
        budget = resource_governor_.plan_training(
            resource::sample_system_resources(),
            interactive_busy(),
            settings.full_rank_steps,
            settings.maximum_sequence_tokens,
            settings.lora_rank);
        if (budget.permitted) {
            break;
        }
        status.state = LearningJobState::waiting_for_resources;
        status.message = budget.reason;
        update_status(status);
        std::this_thread::sleep_for(budget.retry_after);
    }
    if (stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = "full-rank candidate stopped while waiting for resources";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::training;
    candidate.message =
        "refining selected FFN matrices directly beside the active LoRA; active weights remain immutable";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::consolidating;
    status.message = candidate.message;
    update_status(status);

    std::vector<std::string> arguments{
        settings.worker_script.string(),
        "consolidate-module",
        "--base-model", active_module.base_model,
        "--adapter", active_module.adapter_path.string(),
        "--skill", active_module.skill,
        "--output", candidate.artifact_path.string(),
        "--job-dir", candidate.job_directory.string(),
        "--control-dir", (settings.state_root / "control").string(),
        "--episodes", memory_store_.path().string(),
        "--max-steps", std::to_string(std::max<std::size_t>(1U, budget.max_steps)),
        "--max-sequence-tokens", std::to_string(budget.maximum_sequence_tokens),
        "--checkpoint-steps", std::to_string(settings.checkpoint_steps),
        "--layers-per-pass", std::to_string(settings.full_rank_layers_per_pass),
        "--learning-rate", std::to_string(settings.full_rank_learning_rate),
    };
    if (!settings.seed_dataset.empty() &&
        std::filesystem::is_regular_file(settings.seed_dataset)) {
        arguments.emplace_back("--seed-dataset");
        arguments.push_back(settings.seed_dataset.string());
        arguments.emplace_back("--seed-replay-limit");
        arguments.push_back(active_module.generation == 0 ? "64" : "24");
    }
    if (!settings.anchor_dataset.empty() &&
        std::filesystem::is_regular_file(settings.anchor_dataset)) {
        arguments.emplace_back("--anchors");
        arguments.push_back(settings.anchor_dataset.string());
    }
    if (settings.require_cuda) {
        arguments.emplace_back("--require-cuda");
    }
    if (!active_module.module_path.empty()) {
        arguments.emplace_back("--prior-module");
        arguments.push_back(active_module.module_path.string());
    }

    std::optional<std::chrono::steady_clock::time_point> foreground_since;
    const auto result = platform::run_process(
        platform::ProcessSpec{
            .executable = settings.python_executable,
            .arguments = std::move(arguments),
            .working_directory = settings.state_root,
            .log_file = candidate.log_path,
            .append_log = resume_candidate_id.has_value(),
        },
        [this, stop_token, &foreground_since, &settings] {
            if (stop_token.stop_requested()) {
                return true;
            }
            const auto preemption_requested = interactive_busy() || paused();
            if (!preemption_requested) {
                foreground_since.reset();
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (!foreground_since.has_value()) {
                foreground_since = now;
                return false;
            }
            return now - *foreground_since >= settings.inference_preemption_grace;
        });
    clear_training_markers(settings.state_root);

    status.exit_code = result.exit_code;
    const auto resumable_preemption = !stop_token.stop_requested() &&
        (result.exit_code == preempted_exit_code ||
         (result.cancelled && (interactive_busy() || paused())));
    if (result.cancelled || result.exit_code == preempted_exit_code ||
        stop_token.stop_requested()) {
        candidate.state = CandidateState::interrupted;
        candidate.message = resumable_preemption
            ? "consolidation preempted for foreground inference or pause; resume queued and active LoRA unchanged"
            : "consolidation interrupted; active LoRA remains unchanged";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::interrupted;
        status.message = candidate.message;
        update_status(status);
        if (resumable_preemption) {
            queue_request(LearningRequest{
                .skill = candidate.skill,
                .candidate_id = candidate.id,
                .kind = CandidateKind::full_rank_skill,
            });
        }
        return false;
    }
    if (!result.launched || result.exit_code != 0 ||
        !std::filesystem::is_regular_file(candidate.artifact_path / "module.json")) {
        candidate.state = CandidateState::rejected;
        candidate.message = "consolidation failed structural validation; active LoRA remains unchanged";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    transition_durable_phase(
        durable_job_id, TrainingState::training, TrainingState::evaluating,
        status.progress_path, "candidate training complete; evaluating module integrity and retention");
    candidate.state = CandidateState::evaluating;
    candidate.message = "verifying module checksum, FFN-only tensor contract, and manifest counts";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::validating;
    status.message = candidate.message;
    update_status(status);

    const auto inspection = platform::run_process(
        platform::ProcessSpec{
            .executable = settings.python_executable,
            .arguments = {
                settings.worker_script.string(),
                "inspect-module",
                "--module", candidate.artifact_path.string(),
            },
            .working_directory = settings.state_root,
            .log_file = candidate.log_path,
            .append_log = true,
        },
        [stop_token] { return stop_token.stop_requested(); });
    if (!inspection.launched || inspection.cancelled || inspection.exit_code != 0) {
        candidate.state = inspection.cancelled
            ? CandidateState::interrupted
            : CandidateState::rejected;
        candidate.message = inspection.cancelled
            ? "module validation interrupted; active LoRA remains unchanged"
            : "module integrity validation failed; active LoRA remains unchanged";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = inspection.cancelled
            ? LearningJobState::interrupted
            : LearningJobState::failed;
        status.message = candidate.message;
        status.exit_code = inspection.exit_code;
        update_status(status);
        return false;
    }

    const auto retention_score = read_metric(
        candidate.job_directory / "metrics.json", "retention_score", 0.0);
    const auto measured_regression = read_metric(
        candidate.job_directory / "metrics.json", "regression_score", 1.0);
    candidate.validation_score = std::min(active_module.validation_score, retention_score);
    candidate.regression_score = std::max(active_module.regression_score, measured_regression);
    if (candidate.validation_score < settings.policy.minimum_validation_score ||
        candidate.regression_score > settings.policy.maximum_regression) {
        candidate.state = CandidateState::rejected;
        candidate.message =
            "full-rank candidate rejected by retention or regression evaluation; active LoRA remains unchanged";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    transition_durable_phase(
        durable_job_id, TrainingState::evaluating, TrainingState::promoting,
        status.progress_path, "candidate passed retention gates; committing module with parent check");
    bool promoted = false;
    {
        std::lock_guard lock(mutex_);
        auto* current = expert_registry_.find(candidate.skill);
        if (current != nullptr && current->generation == candidate.parent_generation &&
            current->adapter_path == active_module.adapter_path) {
            current->previous_state = current->state;
            current->previous_adapter_path = current->adapter_path;
            current->previous_module_path = current->module_path;
            current->module_path = candidate.artifact_path;
            current->adapter_path.clear();
            current->state = ExpertState::full_rank_active;
            ++current->generation;
            current->validation_score = candidate.validation_score;
            current->regression_score = candidate.regression_score;
            current->promoted_candidate_id = candidate.id;
            current->updated_unix_ms = memory::now_unix_ms();
            expert_registry_.save(*current);
            active_module = *current;
            promoted = true;
        }
    }
    if (!promoted) {
        candidate.state = CandidateState::rejected;
        candidate.message =
            "full-rank candidate became stale because the active skill changed; active intelligence was not modified";
        candidate.updated_unix_ms = memory::now_unix_ms();
        commit_candidate(candidate);
        status.state = LearningJobState::failed;
        status.message = candidate.message;
        update_status(status);
        return false;
    }

    candidate.state = CandidateState::promoted;
    candidate.message =
        "directly refined full-rank FFN skill atomically promoted; prior LoRA is rollback-ready";
    candidate.updated_unix_ms = memory::now_unix_ms();
    commit_candidate(candidate);
    status.state = LearningJobState::succeeded;
    status.message = candidate.message;
    update_status(status);
    return true;
}

void ContinualLearningService::update_status(const LearningJobStatus& incoming) {
    auto status = incoming;
    status.updated_unix_ms = memory::now_unix_ms();
    {
        std::lock_guard lock(mutex_);
        const auto iterator = std::ranges::find(statuses_, status.id, &LearningJobStatus::id);
        if (iterator == statuses_.end()) {
            statuses_.push_back(status);
        } else {
            *iterator = status;
        }
    }
    persist_status(status);
}

void ContinualLearningService::persist_status(const LearningJobStatus& status) const {
    std::ostringstream stream;
    stream << "[job]\n"
           << "id=" << status.id << '\n'
           << "skill=" << status.skill << '\n'
           << "candidate_id=" << status.candidate_id << '\n'
           << "state=" << to_string(status.state) << '\n'
           << "message=" << status.message << '\n'
           << "log=" << status.log_path.generic_string() << '\n'
           << "progress=" << status.progress_path.generic_string() << '\n'
           << "exit_code=" << status.exit_code << '\n'
           << "updated_unix_ms=" << status.updated_unix_ms << '\n';
    publish_file(settings_.state_root / "jobs" / status.id / "status.ini", stream.str());
}

void ContinualLearningService::commit_module(const ExpertModule& module) {
    std::lock_guard lock(mutex_);
    if (auto* existing = expert_registry_.find(module.skill); existing != nullptr) {
        *existing = module;
    } else {
        auto& created = expert_registry_.ensure(module.skill, module.base_model);
        created = module;
    }
    expert_registry_.save(module);
}

void ContinualLearningService::commit_candidate(const CandidateRecord& candidate) {
    std::lock_guard lock(mutex_);
    if (auto* existing = candidate_registry_.find(candidate.id); existing != nullptr) {
        *existing = candidate;
    }
    candidate_registry_.save(candidate);
}

void ContinualLearningService::load_improvement_session() {
    const auto path = settings_.state_root / "state" / "improvement-session.ini";
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        start_improvement_session(settings_.maximum_improvement_session);
        return;
    }

    auto phase = ImprovementSessionPhase::ended;
    std::int64_t budget_ms = 0;
    std::int64_t remaining_ms = 0;
    std::int64_t started_ms = 0;
    std::int64_t deadline_ms = 0;
    try {
        std::string line;
        while (std::getline(stream, line)) {
            const auto separator = line.find('=');
            if (separator == std::string::npos) {
                continue;
            }
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1U);
            if (key == "phase") {
                if (value == "running") {
                    phase = ImprovementSessionPhase::running;
                } else if (value == "paused") {
                    phase = ImprovementSessionPhase::paused;
                } else if (value == "time-limit-reached") {
                    phase = ImprovementSessionPhase::time_limit_reached;
                } else {
                    phase = ImprovementSessionPhase::ended;
                }
            } else if (key == "budget_ms") {
                budget_ms = std::stoll(value);
            } else if (key == "remaining_ms") {
                remaining_ms = std::stoll(value);
            } else if (key == "started_unix_ms") {
                started_ms = std::stoll(value);
            } else if (key == "deadline_unix_ms") {
                deadline_ms = std::stoll(value);
            }
        }
    } catch (...) {
        stream.close();
        start_improvement_session(settings_.maximum_improvement_session);
        return;
    }

    stream.close();
    const auto maximum_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(std::chrono::minutes(1), settings_.maximum_improvement_session)).count();
    budget_ms = std::clamp<std::int64_t>(budget_ms, 1, maximum_ms);
    remaining_ms = std::clamp<std::int64_t>(remaining_ms, 0, budget_ms);
    const auto now = memory::now_unix_ms();
    if (phase == ImprovementSessionPhase::running) {
        remaining_ms = std::max<std::int64_t>(0, deadline_ms - now);
        if (remaining_ms == 0) {
            phase = ImprovementSessionPhase::time_limit_reached;
        }
    }
    improvement_session_phase_.store(phase, std::memory_order_release);
    improvement_session_budget_ms_.store(budget_ms, std::memory_order_release);
    improvement_session_remaining_ms_.store(remaining_ms, std::memory_order_release);
    improvement_session_started_unix_ms_.store(started_ms, std::memory_order_release);
    improvement_session_deadline_unix_ms_.store(deadline_ms, std::memory_order_release);
    paused_.store(phase != ImprovementSessionPhase::running, std::memory_order_release);
    persist_improvement_session();
}

void ContinualLearningService::persist_improvement_session() const {
    std::ostringstream stream;
    stream << "phase="
           << to_string(improvement_session_phase_.load(std::memory_order_acquire)) << '\n'
           << "budget_ms=" << improvement_session_budget_ms_.load(std::memory_order_acquire) << '\n'
           << "remaining_ms=" << improvement_session_remaining_ms_.load(std::memory_order_acquire) << '\n'
           << "started_unix_ms=" << improvement_session_started_unix_ms_.load(std::memory_order_acquire) << '\n'
           << "deadline_unix_ms=" << improvement_session_deadline_unix_ms_.load(std::memory_order_acquire) << '\n';
    publish_file(settings_.state_root / "state" / "improvement-session.ini", stream.str());
}

void ContinualLearningService::publish_control_files() const {
    const auto control = settings_.state_root / "control";
    set_marker(control / "training.paused", paused());
    set_marker(control / "inference.priority", interactive_busy());
}

} // namespace threadspire::ai::learning
