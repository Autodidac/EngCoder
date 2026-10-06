module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

module threadspire.scheduler;
import threadspire.core;
import threadspire.platform;

namespace threadspire::ai::runtime {
namespace {

[[nodiscard]] std::string structured_log_detail(const std::string& tail) {
    auto position = tail.rfind("\n{");
    while (position != std::string::npos) {
        const auto candidate = tail.substr(position + 1U);
        std::string parse_error;
        const auto document = core::json::try_parse(candidate, parse_error);
        if (document.has_value()) {
            const auto status = core::json::string_or(document->find("status"));
            if (status == "rejected") {
                const auto generation = core::json::string_or(document->find("generation"), "unknown");
                const auto previous = core::json::number_or(document->find("previous_loss"), NAN);
                const auto validation = core::json::number_or(document->find("validation_loss"), NAN);
                const auto readiness = core::json::number_or(
                    document->find("generation_readiness_score"), 0.0);
                std::ostringstream detail;
                detail << "candidate " << generation << " was not promoted";
                if (std::isfinite(previous) && std::isfinite(validation)) {
                    detail << " (held-out loss " << std::fixed << std::setprecision(4)
                           << validation << " vs " << previous
                           << "; canary score " << readiness << ')';
                }
                detail << "; the strongest saved candidate remains available for repair";
                return detail.str();
            }
            const auto error = core::json::string_or(document->find("error"));
            if (!error.empty()) {
                return error;
            }
        }
        position = position == 0U ? std::string::npos : tail.rfind("\n{", position - 1U);
    }
    return {};
}

[[nodiscard]] std::string last_log_detail(const std::filesystem::path& log_path) {
    std::ifstream stream(log_path, std::ios::binary);
    if (!stream) {
        return {};
    }
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end <= 0) {
        return {};
    }
    constexpr std::streamoff maximum_tail = 16 * 1024;
    const auto start = std::max<std::streamoff>(0, static_cast<std::streamoff>(end) - maximum_tail);
    stream.seekg(start, std::ios::beg);
    std::string tail{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    if (const auto detail = structured_log_detail(tail); !detail.empty()) {
        return detail;
    }
    while (!tail.empty() && (tail.back() == '\r' || tail.back() == '\n' || tail.back() == ' ' || tail.back() == '\t')) {
        tail.pop_back();
    }
    const auto separator = tail.find_last_of("\r\n");
    auto line = separator == std::string::npos ? tail : tail.substr(separator + 1U);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.erase(line.begin());
    }
    constexpr std::size_t maximum_line = 700U;
    if (line.size() > maximum_line) {
        line = line.substr(line.size() - maximum_line);
    }
    return line;
}

void rotate_log_if_needed(const std::filesystem::path& log_path, const bool append) noexcept {
    if (!append) {
        return;
    }
    std::error_code error;
    constexpr std::uintmax_t maximum_log_bytes = 16U * 1024U * 1024U;
    if (!std::filesystem::is_regular_file(log_path, error) ||
        std::filesystem::file_size(log_path, error) <= maximum_log_bytes || error) {
        return;
    }
    auto previous = log_path;
    previous += ".previous";
    std::filesystem::remove(previous, error);
    error.clear();
    std::filesystem::rename(log_path, previous, error);
}

[[nodiscard]] util::OperationResult run_step(
    const std::filesystem::path& executable,
    std::vector<std::string> arguments,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& log_path,
    const bool append,
    const std::stop_token stop_token,
    std::string step_name) {
    rotate_log_if_needed(log_path, append);
    const auto result = platform::run_process(
        platform::ProcessSpec{
            .executable = executable,
            .arguments = std::move(arguments),
            .working_directory = working_directory,
            .log_file = log_path,
            .append_log = append,
        },
        [stop_token] { return stop_token.stop_requested(); });

    if (result.cancelled || stop_token.stop_requested()) {
        return {
            .state = util::OperationState::cancelled,
            .exit_code = result.exit_code,
            .message = std::move(step_name) + " was cancelled",
        };
    }
    if (!result.launched) {
        return {
            .state = util::OperationState::failed,
            .exit_code = result.exit_code,
            .message = std::move(step_name) + ": " + result.error,
        };
    }
    if (result.exit_code != 0) {
        const auto detail = last_log_detail(log_path);
        return {
            .state = util::OperationState::failed,
            .exit_code = result.exit_code,
            .message = std::move(step_name) + " failed" +
                (detail.empty() ? "; inspect the backend log" : ": " + detail),
        };
    }
    return {
        .state = util::OperationState::succeeded,
        .exit_code = 0,
        .message = std::move(step_name) + " completed",
    };
}

} // namespace

std::vector<std::string> missing_backend_files(const app::AppPaths& paths) {
    const auto pack_id = std::string{"internal-pytorch-training"};
    const auto pack_root = paths.packs / pack_id;
    std::vector<std::string> required{"pack.json"};
    const auto catalog = CapabilityPackManager::catalog();
    const auto descriptor = std::ranges::find(
        catalog,
        std::string_view{pack_id},
        &CapabilityPackDescriptor::id);
    if (descriptor != catalog.end()) {
        required.insert(
            required.end(),
            descriptor->required_files.begin(),
            descriptor->required_files.end());
    }

    std::vector<std::string> missing;
    for (const auto& relative : required) {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(pack_root / relative, error);
        if (error || !std::filesystem::is_regular_file(status) ||
            std::filesystem::is_symlink(status)) {
            missing.push_back(pack_id + "/" + relative);
        }
    }
    return missing;
}

namespace {

[[nodiscard]] std::string missing_backend_message(const app::AppPaths& paths) {
    const auto missing = missing_backend_files(paths);
    std::string message = "Internal PyTorch Training is incomplete";
    if (!missing.empty()) {
        message += ". Missing: ";
        for (std::size_t index = 0U; index < missing.size(); ++index) {
            if (index > 0U) {
                message += "; ";
            }
            message += missing[index];
        }
    }
    message += ". Install the prepared capability pack explicitly; nothing installs automatically.";
    return message;
}

} // namespace

bool backend_files_present(const app::AppPaths& paths) noexcept {
    try {
        return missing_backend_files(paths).empty();
    } catch (...) {
        return false;
    }
}

util::OperationResult install_backend(
    const app::AppPaths&,
    const std::filesystem::path&,
    std::string,
    const std::stop_token) {
    return {
        .state = util::OperationState::failed,
        .exit_code = -1,
        .message = "Automatic Python/PyTorch installation was removed from the base application. Install the Internal PyTorch Training capability pack explicitly from a verified staged payload.",
    };
}
util::OperationResult verify_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    const std::stop_token stop_token) {
    if (!backend_files_present(paths)) {
        return {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = missing_backend_message(paths),
        };
    }

    auto result = run_step(
        paths.python,
        {paths.worker.string(), "doctor"},
        paths.root,
        log_path,
        true,
        stop_token,
        "Checking the explicitly installed internal-training capability pack");
    if (result.state == util::OperationState::succeeded) {
        result.message = "Backend ready for random-initialized internal models, inference, optional LoRA incubation, and expert consolidation";
    }
    return result;
}


util::OperationResult initialize_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string profile,
    const bool force,
    const std::stop_token stop_token) {
    if (!backend_files_present(paths)) {
        return {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = missing_backend_message(paths),
        };
    }
    if (profile != "micro" && profile != "standard" && profile != "large-local") {
        profile = "micro";
    }
    std::vector<std::string> arguments{
        paths.worker.string(),
        "init-internal",
        "--state-root", paths.root.string(),
        "--profile", std::move(profile),
        "--progress-file", (paths.root / "state" / "internal-model-progress.json").string(),
    };
    if (force) {
        arguments.emplace_back("--force");
    }
    auto result = run_step(
        paths.python,
        std::move(arguments),
        paths.root,
        log_path,
        true,
        stop_token,
        "Creating EngCoder's random-initialized internal model");
    if (result.state == util::OperationState::succeeded) {
        result.message = "Internal model initialized locally with no external base weights";
    }
    return result;
}

util::OperationResult train_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string scope,
    const std::size_t maximum_steps,
    const bool foreground_bootstrap,
    const std::size_t bootstrap_target_steps,
    const std::stop_token stop_token) {
    if (!backend_files_present(paths)) {
        return {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = missing_backend_message(paths),
        };
    }
    if (scope.empty()) {
        scope = "all";
    }
    std::vector<std::string> arguments{
        paths.worker.string(),
        "train-internal",
        "--state-root", paths.root.string(),
        "--seed-dataset", paths.bootstrap_dataset.string(),
        "--episodes", (paths.memory / "episodes.jsonl").string(),
        "--anchors", paths.bootstrap_anchors.string(),
        "--scope", std::move(scope),
        "--max-steps", std::to_string(std::max<std::size_t>(1U, maximum_steps)),
        "--learning-rate", "0.0003",
        "--batch-size", "4",
        "--accumulation-steps", "4",
        "--checkpoint-steps", "25",
        "--control-dir", (paths.root / "control").string(),
        "--progress-file", (paths.root / "state" / "internal-model-progress.json").string(),
        "--bootstrap-target-steps", std::to_string(bootstrap_target_steps),
        "--slot-timeout-seconds", "300",
        "--require-cuda",
    };
    if (foreground_bootstrap) {
        arguments.emplace_back("--foreground-bootstrap");
    }
    auto result = run_step(
        paths.python,
        std::move(arguments),
        paths.root,
        log_path,
        true,
        stop_token,
        "Training a shadow generation of the internal model");
    if (result.state == util::OperationState::succeeded) {
        result.message = "Internal candidate trained, evaluated, and promoted only if it improved";
    }
    return result;
}

util::OperationResult inspect_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    const std::stop_token stop_token) {
    if (!backend_files_present(paths)) {
        return {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = missing_backend_message(paths),
        };
    }
    auto result = run_step(
        paths.python,
        {
            paths.worker.string(),
            "internal-status",
            "--state-root", paths.root.string(),
        },
        paths.root,
        log_path,
        true,
        stop_token,
        "Inspecting the internal model");
    if (result.state == util::OperationState::succeeded) {
        result.message = "Internal model status written to the backend log";
    }
    return result;
}

} // namespace threadspire::ai::runtime
