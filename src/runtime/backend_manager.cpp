#include "threadspire/ai/runtime/backend_manager.hpp"

#include "threadspire/ai/platform/process.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace threadspire::ai::runtime {
namespace {

[[nodiscard]] std::string last_log_line(const std::filesystem::path& log_path) {
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

[[nodiscard]] util::OperationResult run_step(
    const std::filesystem::path& executable,
    std::vector<std::string> arguments,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& log_path,
    const bool append,
    const std::stop_token stop_token,
    std::string step_name) {
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
        const auto detail = last_log_line(log_path);
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

bool backend_files_present(const app::AppPaths& paths) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(paths.python, error) &&
           std::filesystem::is_regular_file(paths.worker, error) &&
           std::filesystem::is_regular_file(paths.internal_model_module, error) &&
           std::filesystem::is_regular_file(paths.curriculum_guard_module, error);
}

util::OperationResult install_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string pytorch_index_url,
    const std::stop_token stop_token) {
    std::filesystem::create_directories(log_path.parent_path());
    std::error_code error;
    std::filesystem::remove(log_path, error);

    auto python = paths.python;
    if (!std::filesystem::is_regular_file(python)) {
        auto result = run_step(
            "py.exe",
            {"-3.12", "-m", "venv", paths.runtime.string()},
            paths.root,
            log_path,
            false,
            stop_token,
            "Creating the private Python 3.12 runtime");
        if (result.state == util::OperationState::failed && !stop_token.stop_requested()) {
            result = run_step(
                "py.exe",
                {"-3", "-m", "venv", paths.runtime.string()},
                paths.root,
                log_path,
                true,
                stop_token,
                "Creating the private Python runtime");
        }
        if (result.state != util::OperationState::succeeded) {
            result.message += "; install 64-bit Python 3.12 if py.exe is unavailable";
            return result;
        }
        python = paths.runtime / "Scripts" / "python.exe";
    }

    auto result = run_step(
        python,
        {"-m", "pip", "install", "--upgrade", "pip", "setuptools<82", "wheel"},
        paths.root,
        log_path,
        true,
        stop_token,
        "Updating pip");
    if (result.state != util::OperationState::succeeded) {
        return result;
    }

    if (pytorch_index_url.empty()) {
        pytorch_index_url = "https://download.pytorch.org/whl/cu128";
    }
    result = run_step(
        python,
        {"-m", "pip", "install", "--upgrade", "torch==2.11.0", "--index-url", std::move(pytorch_index_url)},
        paths.root,
        log_path,
        true,
        stop_token,
        "Installing CUDA PyTorch");
    if (result.state != util::OperationState::succeeded) {
        return result;
    }

    result = run_step(
        python,
        {
            "-m", "pip", "install", "--upgrade",
            "accelerate==1.14.0",
            "bitsandbytes==0.50.1",
            "datasets==5.0.1",
            "peft==0.20.0",
            "safetensors==0.8.0",
            "transformers==5.15.0",
            "sentencepiece",
        },
        paths.root,
        log_path,
        true,
        stop_token,
        "Installing inference and continual-learning packages");
    if (result.state != util::OperationState::succeeded) {
        return result;
    }

    auto verified_paths = paths;
    verified_paths.python = std::move(python);
    return verify_backend(verified_paths, log_path, stop_token);
}

util::OperationResult verify_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    const std::stop_token stop_token) {
    if (!backend_files_present(paths)) {
        return {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "The private PyTorch backend is not installed",
        };
    }

    auto result = run_step(
        paths.python,
        {paths.worker.string(), "doctor"},
        paths.root,
        log_path,
        true,
        stop_token,
        "Checking PyTorch, CUDA, internal-model code, Transformers, PEFT, and safetensors");
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
            .message = "The private PyTorch runtime is not installed",
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
        "Creating ThreadSpire's random-initialized internal model");
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
            .message = "The private PyTorch runtime is not installed",
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
            .message = "The private PyTorch runtime is not installed",
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
