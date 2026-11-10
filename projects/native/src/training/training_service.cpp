#include "engcoder/training/training_service.hpp"

#include "engcoder/process/process.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace engcoder::training {
namespace {

[[nodiscard]] json parse_last_json(std::string_view text) {
    const auto first = text.find('{');
    const auto last = text.rfind('}');
    if (first == std::string_view::npos || last == std::string_view::npos || last < first) {
        return json{{"output", text}};
    }
    try {
        return json::parse(text.substr(first, last - first + 1));
    } catch (...) {
        return json{{"output", text}};
    }
}

void copy_if_missing(const std::filesystem::path& source, const std::filesystem::path& target) {
    if (std::filesystem::exists(target) || !std::filesystem::is_regular_file(source)) return;
    std::filesystem::create_directories(target.parent_path());
    std::error_code error;
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, error);
}

[[nodiscard]] std::string component_for_scope(std::string_view scope) {
    if (scope == "keymaker-foundation" || scope == "english-foundation" || scope == "english-conversation") return "keymaker";
    if (scope == "architect-controller" || scope == "architect" || scope == "controller") return "architect";
    if (scope == "cpp23-core" || scope == "cpp23-debugging" || scope == "cpp") return "cpp";
    if (scope == "documentation" || scope == "docs") return "docs";
    if (scope == "planning" || scope == "planner") return "planner";
    if (scope == "tool-use" || scope == "tools") return "tools";
    if (scope == "reranking" || scope == "reranker") return "reranker";
    return std::string{scope};
}

} // namespace

TrainingService::TrainingService(
    const core::Config& config,
    std::filesystem::path state_root,
    models::ModelRegistry& models,
    residency::ModelResidencyManager& residency)
    : config_(config),
      state_root_(std::move(state_root)),
      control_root_(state_root_ / "control"),
      models_(models),
      residency_(residency) {
    std::filesystem::create_directories(control_root_);
    ensure_training_data();
}

TrainingService::~TrainingService() {
    cancel();
    if (worker_.joinable()) worker_.join();
}

void TrainingService::ensure_training_data() const {
    const auto resources = config_.training_backend.parent_path();
    const auto data = state_root_ / "data";
    copy_if_missing(resources / "bootstrap.jsonl", data / "bootstrap.jsonl");
    copy_if_missing(resources / "bootstrap_anchors.jsonl", data / "bootstrap_anchors.jsonl");
    copy_if_missing(resources / "anchors.jsonl", data / "anchors.jsonl");
    copy_if_missing(resources / "seed.jsonl", data / "seed.jsonl");
}

std::expected<json, core::Error> TrainingService::run_worker(
    const std::vector<std::string>& arguments,
    std::stop_token stop) const {
    if (!std::filesystem::is_regular_file(config_.training_backend)) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            "training backend not found: " + config_.training_backend.string(),
        });
    }

    process::Request request;
    request.executable = config_.python_executable;
    request.arguments.reserve(arguments.size() + 1);
    request.arguments.push_back(config_.training_backend.string());
    request.arguments.insert(request.arguments.end(), arguments.begin(), arguments.end());
    request.working_directory = config_.training_backend.parent_path();
    request.timeout = std::chrono::hours{24};
    request.max_output_bytes = 4 * 1024 * 1024;

    auto result = process::run(request, stop);
    if (!result) return std::unexpected(result.error());
    if (result->cancelled) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::cancelled), "training worker cancelled"});
    }
    if (result->timed_out || (result->exit_code != 0 && result->exit_code != 75)) {
        const auto detail = !result->stderr_text.empty() ? result->stderr_text : result->stdout_text;
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::process_failed),
            "training worker exited with code " + std::to_string(result->exit_code) + ": " + detail,
        });
    }
    auto payload = parse_last_json(result->stdout_text);
    payload["_exit_code"] = result->exit_code;
    if (result->exit_code == 75) payload["status"] = "preempted";
    return payload;
}

std::expected<json, core::Error> TrainingService::doctor() const {
    return run_worker({"doctor"});
}

std::expected<json, core::Error> TrainingService::initialize(std::string_view profile, bool force) {
    if (running()) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::stalled), "training is already running"});
    }
    std::vector<std::string> args{
        "init-internal",
        "--state-root", state_root_.string(),
        "--profile", std::string{profile},
    };
    if (force) args.push_back("--force");
    auto result = run_worker(args);
    if (result) {
        models_.refresh();
        residency_.sync(models_);
    }
    return result;
}

std::expected<void, core::Error> TrainingService::start(TrainingRequest request) {
    if (!config_.training_enabled) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::tool_denied), "training is disabled"});
    }
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::stalled), "a training job is already active"});
    }
    if (worker_.joinable()) worker_.join();
    {
        std::scoped_lock lock(mutex_);
        last_status_ = json{{"state", "queued"}, {"scope", request.scope}, {"max_steps", request.max_steps}};
    }
    worker_ = std::jthread([this, request = std::move(request)](std::stop_token stop) mutable {
        run_training(stop, std::move(request));
    });
    return {};
}

void TrainingService::run_training(std::stop_token stop, TrainingRequest request) {
    const auto training_component = component_for_scope(request.scope);
    residency_.mark_training(training_component, true);
    {
        std::scoped_lock lock(mutex_);
        last_status_ = json{{"state", "running"}, {"scope", request.scope}, {"max_steps", request.max_steps}};
    }

    ensure_training_data();
    std::vector<std::string> args{
        "train-internal",
        "--state-root", state_root_.string(),
        "--seed-dataset", (state_root_ / "data" / "bootstrap.jsonl").string(),
        "--anchors", (state_root_ / "data" / "anchors.jsonl").string(),
        "--scope", request.scope,
        "--max-steps", std::to_string(std::max<std::uint32_t>(1, request.max_steps)),
        "--checkpoint-steps", std::to_string(std::max<std::uint32_t>(1, request.checkpoint_steps)),
        "--control-dir", control_root_.string(),
    };
    const auto verified_evidence = state_root_ / "learning" / "verified-evidence.jsonl";
    if (std::filesystem::is_regular_file(verified_evidence)) {
        args.push_back("--episodes");
        args.push_back(verified_evidence.string());
    }
    if (request.require_cuda) args.push_back("--require-cuda");

    std::expected<json, core::Error> result = json::object();
    while (!stop.stop_requested()) {
        result = run_worker(args, stop);
        if (!result) break;
        const auto preempted = result->value("_exit_code", 0) == 75 ||
            result->value("status", std::string{}) == "preempted";
        if (!preempted) break;

        const bool user_paused = std::filesystem::exists(control_root_ / "training.paused");
        {
            std::scoped_lock lock(mutex_);
            last_status_ = *result;
            last_status_["state"] = user_paused ? "paused" : "paused-for-inference";
            last_status_["resumable"] = true;
        }

        // The Python worker has checkpointed and relinquished CUDA. Wait for
        // both explicit user pause and foreground inference blockers to clear,
        // then launch the same job again. The worker restores its exact
        // checkpoint rather than restarting the training run.
        while (!stop.stop_requested() &&
               (std::filesystem::exists(control_root_ / "training.paused") ||
                inference_users_.load(std::memory_order_acquire) != 0 ||
                std::filesystem::exists(control_root_ / "inference.priority"))) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        if (!stop.stop_requested()) {
            std::scoped_lock lock(mutex_);
            last_status_["state"] = "running";
            last_status_["resumed"] = true;
        }
    }

    if (stop.stop_requested()) {
        std::scoped_lock lock(mutex_);
        last_status_ = json{{"state", "cancelled"}, {"scope", request.scope}};
    } else if (result) {
        std::scoped_lock lock(mutex_);
        last_status_ = *result;
        last_status_.erase("_exit_code");
        last_status_["state"] = "completed";
    } else {
        std::scoped_lock lock(mutex_);
        last_status_ = json{
            {"state", result.error().code == core::make_error_code(core::Errc::cancelled) ? "cancelled" : "failed"},
            {"error", result.error().message},
            {"scope", request.scope},
        };
    }
    residency_.mark_training(training_component, false);
    models_.refresh();
    residency_.sync(models_);
    running_.store(false, std::memory_order_release);
}

void TrainingService::cancel() {
    if (worker_.joinable()) worker_.request_stop();
}

void TrainingService::write_marker(const std::filesystem::path& path, bool present) const {
    std::error_code error;
    if (present) {
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream marker(path, std::ios::binary | std::ios::trunc);
        marker << "1\n";
    } else {
        std::filesystem::remove(path, error);
    }
}

void TrainingService::set_paused(bool paused) {
    write_marker(control_root_ / "training.paused", paused);
}

void TrainingService::begin_inference() {
    if (!config_.training_preempt_for_inference) return;
    if (inference_users_.fetch_add(1, std::memory_order_acq_rel) == 0) {
        write_marker(control_root_ / "inference.priority", true);
    }
}

void TrainingService::end_inference() {
    if (!config_.training_preempt_for_inference) return;
    const auto prior = inference_users_.load(std::memory_order_acquire);
    if (prior == 0) return;
    if (inference_users_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        write_marker(control_root_ / "inference.priority", false);
    }
}

json TrainingService::status() const {
    json result;
    {
        std::scoped_lock lock(mutex_);
        result = last_status_;
    }
    result["enabled"] = config_.training_enabled;
    result["running"] = running();
    result["preempt_for_inference"] = config_.training_preempt_for_inference;
    result["backend"] = config_.training_backend.string();
    result["control_root"] = control_root_.string();
    result["inference_users"] = inference_users_.load(std::memory_order_acquire);

    const auto progress_path = state_root_ / "state" / "internal-model-progress.json";
    std::ifstream progress(progress_path, std::ios::binary);
    if (progress) {
        try { result["progress"] = json::parse(progress); } catch (...) {}
    }
    return result;
}

} // namespace engcoder::training
