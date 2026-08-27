module;
#include "module_prelude.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <random>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

module threadspire.tasks;
import threadspire.platform;
import threadspire.providers;
import threadspire.storage;

namespace threadspire::ai::long_horizon {
namespace {

[[nodiscard]] std::uintmax_t tree_size(const std::filesystem::path& root) noexcept {
    std::error_code error;
    std::uintmax_t bytes = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (iterator->is_regular_file(error) && !error) {
            bytes += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return bytes;
}

void prune_trajectory_history(const std::filesystem::path& root) noexcept {
    struct Entry final {
        std::filesystem::file_time_type modified;
        std::filesystem::path path;
        std::uintmax_t bytes;
    };
    std::error_code error;
    std::vector<Entry> entries;
    for (std::filesystem::directory_iterator iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error), end;
         iterator != end; iterator.increment(error), error.clear()) {
        if (iterator->is_directory(error) && !error) {
            entries.push_back({iterator->last_write_time(error), iterator->path(), tree_size(iterator->path())});
        }
        error.clear();
    }
    std::ranges::sort(entries, {}, &Entry::modified);
    std::uintmax_t total = 0U;
    for (const auto& entry : entries) {
        total += entry.bytes;
    }
    constexpr std::size_t maximum_runs = 20U;
    constexpr std::uintmax_t maximum_bytes = 2U * 1024U * 1024U * 1024U;
    while (!entries.empty() && (entries.size() >= maximum_runs || total > maximum_bytes)) {
        total -= std::min(total, entries.front().bytes);
        std::filesystem::remove_all(entries.front().path, error);
        error.clear();
        entries.erase(entries.begin());
    }
}

[[nodiscard]] std::string make_run_id() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::ostringstream stream;
    stream << "run-" << memory::now_unix_ms() << '-' << std::hex << generator();
    return stream.str();
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result.push_back(static_cast<char>(character)); break;
        }
    }
    return result;
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, const std::string_view value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << value;
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

[[nodiscard]] bool supported_text_file(const std::filesystem::path& path) {
    static constexpr std::array extensions{
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ixx", ".cppm",
        ".cmake", ".txt", ".json", ".toml", ".yaml", ".yml", ".md", ".bat", ".cmd",
        ".ps1", ".py", ".js", ".ts", ".html", ".css", ".xml", ".ini",
    };
    const auto extension = lower_ascii(path.extension().string());
    return std::ranges::find(extensions, extension) != extensions.end() ||
           path.filename() == "CMakeLists.txt" || path.filename() == "CMakePresets.json" ||
           path.filename() == "Makefile";
}

[[nodiscard]] platform::ProcessResult run_shell(
    const std::filesystem::path& repository,
    const std::string& command,
    const std::filesystem::path& log_path,
    const bool append,
    const std::stop_token stop_token) {
    if (command.empty()) {
        platform::ProcessResult result;
        result.error = "command is empty";
        return result;
    }
#ifdef _WIN32
    const platform::ProcessSpec spec{
        .executable = "cmd.exe",
        .arguments = {"/d", "/s", "/c", command},
        .working_directory = repository,
        .log_file = log_path,
        .append_log = append,
    };
#else
    const platform::ProcessSpec spec{
        .executable = "/bin/sh",
        .arguments = {"-lc", command},
        .working_directory = repository,
        .log_file = log_path,
        .append_log = append,
    };
#endif
    return platform::run_process(spec, [stop_token] { return stop_token.stop_requested(); });
}

[[nodiscard]] platform::ProcessResult run_git(
    const std::filesystem::path& repository,
    std::vector<std::string> arguments,
    const std::filesystem::path& log_path,
    const bool append,
    const std::stop_token stop_token) {
#ifdef _WIN32
    constexpr auto git_executable = "git.exe";
#else
    constexpr auto git_executable = "git";
#endif
    return platform::run_process(
        platform::ProcessSpec{
            .executable = git_executable,
            .arguments = std::move(arguments),
            .working_directory = repository,
            .log_file = log_path,
            .append_log = append,
        },
        [stop_token] { return stop_token.stop_requested(); });
}

void append_event(
    Result& result,
    const std::size_t iteration,
    std::string phase,
    const platform::ProcessResult& process,
    const std::filesystem::path& log_path) {
    result.events.push_back(Event{
        .iteration = iteration,
        .phase = phase,
        .exit_code = process.exit_code,
        .duration = process.duration,
        .log_path = log_path,
    });
    std::ofstream stream(result.trajectory_path, std::ios::app | std::ios::binary);
    stream << "{\"iteration\":" << iteration
           << ",\"phase\":\"" << json_escape(phase)
           << "\",\"launched\":" << (process.launched ? "true" : "false")
           << ",\"cancelled\":" << (process.cancelled ? "true" : "false")
           << ",\"exit_code\":" << process.exit_code
           << ",\"duration_ms\":" << process.duration.count()
           << ",\"log\":\"" << json_escape(log_path.string()) << "\"}\n";
}

[[nodiscard]] bool process_succeeded(const platform::ProcessResult& result) noexcept {
    return result.launched && !result.cancelled && result.exit_code == 0;
}

[[nodiscard]] std::string trim_ascii_whitespace(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) noexcept {
    std::error_code error;
    const auto resolved_candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return false;
    }
    const auto resolved_root = std::filesystem::weakly_canonical(root, error);
    if (error) {
        return false;
    }
    const auto relative = resolved_candidate.lexically_relative(resolved_root);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}

[[nodiscard]] bool publish_new_file(
    const std::filesystem::path& destination,
    const std::string_view contents) {
    auto temporary = destination;
    temporary += ".tmp";
    write_text(temporary, contents);
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

[[nodiscard]] std::string candidate_manifest(const Result& result) {
    std::ostringstream stream;
    stream << "{\n"
           << "  \"version\": 1,\n"
           << "  \"status\": \"verified-awaiting-approval\",\n"
           << "  \"source_repository\": \"" << json_escape(result.source_repository.generic_string()) << "\",\n"
           << "  \"source_head\": \"" << json_escape(result.source_head) << "\",\n"
           << "  \"staged_repository\": \"" << json_escape(result.staged_repository.generic_string()) << "\",\n"
           << "  \"candidate_commit\": \"" << json_escape(result.candidate_commit) << "\",\n"
           << "  \"patch\": \"" << json_escape(result.final_diff_path.generic_string()) << "\",\n"
           << "  \"compiler_passed\": " << (result.compiler_passed ? "true" : "false") << ",\n"
           << "  \"tests_passed\": " << (result.tests_passed ? "true" : "false") << "\n"
           << "}\n";
    return stream.str();
}

[[nodiscard]] bool prepare_isolated_repository(
    const Request& request,
    Result& result,
    const std::stop_token stop_token) {
    std::error_code error;
    result.source_repository = std::filesystem::weakly_canonical(request.repository, error);
    if (error) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "The selected repository path could not be resolved safely",
        };
        return false;
    }

    const auto head_log = result.run_directory / "source-head.txt";
    const auto head = run_git(
        result.source_repository,
        {"rev-parse", "--verify", "HEAD"},
        head_log,
        false,
        stop_token);
    append_event(result, 0, "capture-source-head", head, head_log);
    result.source_head = trim_ascii_whitespace(read_text(head_log));
    if (!process_succeeded(head) || result.source_head.empty()) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = head.exit_code,
            .message = "The selected repository has no verifiable Git HEAD",
        };
        return false;
    }

    result.staged_repository = result.run_directory / "staged-repository";
    const auto clone_log = result.run_directory / "stage-clone.log";
    const auto clone = run_git(
        result.run_directory,
        {"clone", "--no-hardlinks", "--quiet", result.source_repository.string(), result.staged_repository.string()},
        clone_log,
        false,
        stop_token);
    append_event(result, 0, "create-isolated-candidate", clone, clone_log);
    if (!process_succeeded(clone) ||
        !path_is_within(result.staged_repository, result.run_directory)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = clone.exit_code,
            .message = "EngCoder could not create a private staged repository",
        };
        return false;
    }

    const auto checkout = run_git(
        result.staged_repository,
        {"checkout", "--detach", "--quiet", result.source_head},
        clone_log,
        true,
        stop_token);
    append_event(result, 0, "pin-isolated-candidate", checkout, clone_log);
    if (!process_succeeded(checkout)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = checkout.exit_code,
            .message = "The private candidate could not be pinned to the selected source revision",
        };
        return false;
    }
    return true;
}
[[nodiscard]] bool clean_worktree(
    const Request& request,
    Result& result,
    const std::stop_token stop_token) {
    const auto log = result.run_directory / "worktree-check.log";
    const auto inside = run_git(
        request.repository,
        {"rev-parse", "--is-inside-work-tree"},
        log,
        false,
        stop_token);
    append_event(result, 0, "git-repository-check", inside, log);
    if (!process_succeeded(inside)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = inside.exit_code,
            .message = "Repository work requires Git so EngCoder can pin the source revision and validate a private candidate",
        };
        return false;
    }
    if (!request.require_clean_worktree) {
        return true;
    }
    const auto status_log = result.run_directory / "source-status.txt";
    const auto status = run_git(
        request.repository,
        {"status", "--porcelain=v1", "--untracked-files=normal"},
        status_log,
        false,
        stop_token);
    append_event(result, 0, "git-clean-status", status, status_log);
    if (!process_succeeded(status) || !trim_ascii_whitespace(read_text(status_log)).empty()) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = status.exit_code,
            .message = "Repository has uncommitted or untracked files. Commit or stash them before an autonomous run",
        };
        return false;
    }
    const auto unstaged = run_git(
        request.repository,
        {"diff", "--quiet", "--ignore-submodules", "--"},
        log,
        true,
        stop_token);
    append_event(result, 0, "git-unstaged-check", unstaged, log);
    const auto staged = run_git(
        request.repository,
        {"diff", "--cached", "--quiet", "--ignore-submodules", "--"},
        log,
        true,
        stop_token);
    append_event(result, 0, "git-staged-check", staged, log);
    if (!process_succeeded(unstaged) || !process_succeeded(staged)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = 1,
            .message = "Repository has uncommitted changes. Commit or stash them before an autonomous run",
        };
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::filesystem::path> tracked_files(
    const Request& request,
    Result& result,
    const std::stop_token stop_token) {
    const auto log = result.run_directory / "tracked-files.txt";
    const auto process = run_git(
        request.repository,
        {"ls-files"},
        log,
        false,
        stop_token);
    append_event(result, 0, "git-ls-files", process, log);
    if (!process_succeeded(process)) {
        return {};
    }
    std::vector<std::filesystem::path> files;
    std::istringstream stream(read_text(log));
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            const std::filesystem::path path(line);
            if (supported_text_file(path)) {
                files.push_back(path);
            }
        }
    }
    return files;
}

[[nodiscard]] std::string repository_context(
    const Request& request,
    Result& result,
    const std::string_view diagnostics,
    const std::stop_token stop_token) {
    struct Candidate final {
        int score{0};
        std::filesystem::path path;
    };
    const auto lowered_diagnostics = lower_ascii(std::string(diagnostics));
    std::vector<Candidate> candidates;
    for (const auto& path : tracked_files(request, result, stop_token)) {
        const auto normalized = lower_ascii(path.generic_string());
        const auto filename = lower_ascii(path.filename().string());
        int score = 0;
        if (lowered_diagnostics.find(normalized) != std::string::npos) {
            score += 100;
        }
        if (!filename.empty() && lowered_diagnostics.find(filename) != std::string::npos) {
            score += 30;
        }
        if (path.filename() == "CMakeLists.txt" || path.filename() == "CMakePresets.json") {
            score += 10;
        }
        const auto extension = lower_ascii(path.extension().string());
        if (extension == ".cpp" || extension == ".hpp" || extension == ".h" ||
            extension == ".ixx" || extension == ".cppm") {
            score += 5;
        }
        candidates.push_back({.score = score, .path = path});
    }
    std::ranges::sort(candidates, [](const Candidate& left, const Candidate& right) {
        return left.score != right.score
            ? left.score > right.score
            : left.path.generic_string() < right.path.generic_string();
    });

    constexpr std::size_t maximum_context = 180U * 1024U;
    std::ostringstream context;
    std::size_t used = 0;
    for (const auto& candidate : candidates) {
        const auto absolute = request.repository / candidate.path;
        auto text = read_text(absolute);
        if (text.empty()) {
            continue;
        }
        const auto header = "\n===== FILE: " + candidate.path.generic_string() + " =====\n";
        if (used + header.size() >= maximum_context) {
            break;
        }
        const auto available = maximum_context - used - header.size();
        if (text.size() > available) {
            text.resize(available);
        }
        context << header << text;
        used += header.size() + text.size();
        if (used >= maximum_context) {
            break;
        }
    }
    return context.str();
}

[[nodiscard]] std::string extract_unified_diff(const std::string_view generated) {
    auto candidate = std::string(generated);
    const auto fence = candidate.find("```");
    if (fence != std::string::npos) {
        const auto first_newline = candidate.find('\n', fence);
        const auto closing = first_newline == std::string::npos
            ? std::string::npos
            : candidate.find("```", first_newline + 1U);
        if (first_newline != std::string::npos && closing != std::string::npos) {
            candidate = candidate.substr(first_newline + 1U, closing - first_newline - 1U);
        }
    }
    std::size_t start = std::string::npos;
    for (const auto marker : {std::string_view("diff --git "), std::string_view("--- a/")}) {
        const auto position = candidate.find(marker);
        if (position != std::string::npos) {
            start = start == std::string::npos ? position : std::min(start, position);
        }
    }
    if (start == std::string::npos) {
        return {};
    }
    candidate.erase(0, start);
    while (!candidate.empty() && (candidate.back() == ' ' || candidate.back() == '\r' ||
                                   candidate.back() == '\n' || candidate.back() == '\t')) {
        candidate.pop_back();
    }
    if (candidate.find("\n+++ ") == std::string::npos ||
        candidate.find("\n@@ ") == std::string::npos) {
        return {};
    }
    candidate.push_back('\n');
    return candidate;
}

[[nodiscard]] platform::ProcessResult generate_repair(
    const Request& request,
    Result& result,
    const std::filesystem::path& task_file,
    const std::filesystem::path& diagnostics_path,
    const std::filesystem::path& output_diff,
    const std::filesystem::path& log_path,
    const std::string_view phase,
    const std::size_t iteration,
    const std::stop_token stop_token) {
    platform::ProcessResult process;
    process.launched = true;
    if (stop_token.stop_requested()) {
        process.cancelled = true;
        return process;
    }
    const auto diagnostics = read_text(diagnostics_path);
    const auto context = repository_context(request, result, diagnostics, stop_token);
    if (context.empty()) {
        process.exit_code = 1;
        process.error = "No tracked source context was available";
        write_text(log_path, process.error);
        return process;
    }

    std::ostringstream prompt;
    prompt << "Task:\n" << read_text(task_file)
           << "\n\nPhase: " << phase << "\nIteration: " << iteration
           << "\n\nCompiler or test diagnostics:\n" << diagnostics
           << "\n\nTracked repository context:\n" << context
           << "\n\nReturn the smallest complete repair as one unified diff rooted at the repository.";

    const backend::CompletionClient client;
    const auto completion = client.complete(
        request.backend_base_url,
        request.repair_model,
        backend::CompletionRequest{
            .system_prompt =
                "You are EngCoder's autonomous C++23 repository worker. Return only one complete unified diff. "
                "No markdown prose. Preserve project architecture, ownership rules, and public behavior unless "
                "the task requires a change. Never invent compiler or test results.",
            .prompt = prompt.str(),
            .messages = {},
            .max_output_tokens = 4096,
            .temperature = 0.05,
            .adapter_path = request.adapter_path,
            .module_path = request.module_path,
            .tools_json = {},
            .stream = false,
            .on_token = {},
            .should_stop = {},
        });
    process.duration = completion.elapsed;
    process.exit_code = completion.succeeded ? 0 : 1;
    process.error = completion.error;
    write_text(log_path, completion.succeeded ? completion.content : completion.error);
    if (!completion.succeeded) {
        return process;
    }
    const auto patch = extract_unified_diff(completion.content);
    if (patch.empty()) {
        process.exit_code = 1;
        process.error = "Model response did not contain a complete unified diff";
        return process;
    }
    write_text(output_diff, patch);
    return process;
}

[[nodiscard]] bool check_and_apply_patch(
    const Request& request,
    Result& result,
    const std::size_t iteration,
    const std::filesystem::path& patch_path,
    const std::filesystem::path& log_path,
    const std::stop_token stop_token) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(patch_path, error) ||
        std::filesystem::file_size(patch_path, error) == 0U) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = 1,
            .message = "Model returned no patch",
        };
        return false;
    }
    const auto check = run_git(
        request.repository,
        {"apply", "--check", "--whitespace=error-all", patch_path.string()},
        log_path,
        true,
        stop_token);
    append_event(result, iteration, "patch-check", check, log_path);
    if (!process_succeeded(check)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = check.exit_code,
            .message = "Generated patch failed git apply --check",
        };
        return false;
    }
    const auto apply = run_git(
        request.repository,
        {"apply", "--index", "--whitespace=fix", patch_path.string()},
        log_path,
        true,
        stop_token);
    append_event(result, iteration, "patch-apply", apply, log_path);
    if (!process_succeeded(apply)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = apply.exit_code,
            .message = "Generated patch passed validation but could not be applied",
        };
        return false;
    }
    return true;
}

void rollback(const Request& request, Result& result) {
    if (!request.rollback_on_failure) {
        return;
    }
    const auto log = result.run_directory / "rollback.log";
    write_text(
        log,
        "No selected-repository files were changed. The private candidate is retained for audit and safe cleanup.\n");
    platform::ProcessResult isolated;
    isolated.launched = true;
    isolated.exit_code = 0;
    append_event(result, result.iterations, "selected-repository-unchanged", isolated, log);
}
[[nodiscard]] Result cancelled(Result result, const Request& request) {
    result.operation = {
        .state = util::OperationState::cancelled,
        .exit_code = -1,
        .message = "Long-horizon run cancelled",
    };
    rollback(request, result);
    return result;
}

} // namespace

Result run(const Request& request, const std::stop_token stop_token) {
    Result result;
    if (!std::filesystem::is_directory(request.repository)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "Repository directory does not exist",
        };
        return result;
    }
    if (request.backend_base_url.empty() || !request.repair_model.enabled) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "Start the model server and enable a C++ repair model first",
        };
        return result;
    }
    if (request.task.empty() || request.build_command.empty()) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "Task and build command are required",
        };
        return result;
    }

    const auto trajectories_root = request.state_root / "trajectories";
    prune_trajectory_history(trajectories_root);
    result.run_directory = trajectories_root / make_run_id();
    std::filesystem::create_directories(result.run_directory);
    result.trajectory_path = result.run_directory / "trajectory.jsonl";
    std::ofstream(result.trajectory_path, std::ios::trunc).close();

    if (!clean_worktree(request, result, stop_token) ||
        !prepare_isolated_repository(request, result, stop_token)) {
        return result;
    }

    auto staged_request = request;
    staged_request.repository = result.staged_repository;
    staged_request.require_clean_worktree = true;

    const auto task_file = result.run_directory / "task.txt";
    write_text(task_file, request.task);
    const auto initial_diagnostics = result.run_directory / "initial-context.txt";
    write_text(initial_diagnostics, "Initial implementation pass. No compiler or test diagnostics exist yet.");
    const auto initial_patch = result.run_directory / "initial-implementation.diff";
    const auto initial_log = result.run_directory / "initial-implementation.log";
    const auto initial = generate_repair(
        staged_request,
        result,
        task_file,
        initial_diagnostics,
        initial_patch,
        initial_log,
        "implementation",
        0,
        stop_token);
    append_event(result, 0, "implementation", initial, initial_log);
    if (initial.cancelled || stop_token.stop_requested()) {
        return cancelled(std::move(result), staged_request);
    }
    if (!process_succeeded(initial) ||
        !check_and_apply_patch(staged_request, result, 0, initial_patch, initial_log, stop_token)) {
        rollback(staged_request, result);
        if (result.operation.message.empty()) {
            result.operation = {
                .state = util::OperationState::failed,
                .exit_code = initial.exit_code,
                .message = initial.error.empty()
                    ? "The implementation model did not produce a safe applicable patch"
                    : initial.error,
            };
        }
        return result;
    }

    const auto maximum_iterations = std::clamp<std::size_t>(request.maximum_iterations, 1U, 32U);
    for (std::size_t iteration = 1; iteration <= maximum_iterations; ++iteration) {
        result.iterations = iteration;
        if (stop_token.stop_requested()) {
            return cancelled(std::move(result), staged_request);
        }

        const auto iteration_directory = result.run_directory / ("iteration-" + std::to_string(iteration));
        std::filesystem::create_directories(iteration_directory);
        const auto build_log = iteration_directory / "build.log";
        const auto build = run_shell(
            result.staged_repository,
            request.build_command,
            build_log,
            false,
            stop_token);
        append_event(result, iteration, "build-isolated-candidate", build, build_log);
        if (build.cancelled || stop_token.stop_requested()) {
            return cancelled(std::move(result), staged_request);
        }
        result.compiler_passed = process_succeeded(build);
        if (!result.compiler_passed) {
            const auto patch = iteration_directory / "compile-repair.diff";
            const auto repair_log = iteration_directory / "compile-repair.log";
            const auto repair = generate_repair(
                staged_request,
                result,
                task_file,
                build_log,
                patch,
                repair_log,
                "compile-repair",
                iteration,
                stop_token);
            append_event(result, iteration, "compile-repair", repair, repair_log);
            if (repair.cancelled || stop_token.stop_requested()) {
                return cancelled(std::move(result), staged_request);
            }
            if (!process_succeeded(repair) ||
                !check_and_apply_patch(staged_request, result, iteration, patch, repair_log, stop_token)) {
                rollback(staged_request, result);
                if (result.operation.message.empty()) {
                    result.operation = {
                        .state = util::OperationState::failed,
                        .exit_code = repair.exit_code,
                        .message = repair.error.empty()
                            ? "Build failed and no safe repair could be applied in the private candidate"
                            : repair.error,
                    };
                }
                return result;
            }
            continue;
        }

        if (request.test_command.empty()) {
            result.tests_passed = true;
        } else {
            const auto test_log = iteration_directory / "tests.log";
            const auto tests = run_shell(
                result.staged_repository,
                request.test_command,
                test_log,
                false,
                stop_token);
            append_event(result, iteration, "test-isolated-candidate", tests, test_log);
            if (tests.cancelled || stop_token.stop_requested()) {
                return cancelled(std::move(result), staged_request);
            }
            result.tests_passed = process_succeeded(tests);
            if (!result.tests_passed) {
                const auto patch = iteration_directory / "test-repair.diff";
                const auto repair_log = iteration_directory / "test-repair.log";
                const auto repair = generate_repair(
                    staged_request,
                    result,
                    task_file,
                    test_log,
                    patch,
                    repair_log,
                    "test-repair",
                    iteration,
                    stop_token);
                append_event(result, iteration, "test-repair", repair, repair_log);
                if (repair.cancelled || stop_token.stop_requested()) {
                    return cancelled(std::move(result), staged_request);
                }
                if (!process_succeeded(repair) ||
                    !check_and_apply_patch(staged_request, result, iteration, patch, repair_log, stop_token)) {
                    rollback(staged_request, result);
                    if (result.operation.message.empty()) {
                        result.operation = {
                            .state = util::OperationState::failed,
                            .exit_code = repair.exit_code,
                            .message = repair.error.empty()
                                ? "Tests failed and no safe repair could be applied in the private candidate"
                                : repair.error,
                        };
                    }
                    return result;
                }
                continue;
            }
        }

        result.final_diff_path = result.run_directory / "final.diff";
        const auto diff = run_git(
            result.staged_repository,
            {"diff", "--cached", "--no-ext-diff", "--binary", "--"},
            result.final_diff_path,
            false,
            stop_token);
        append_event(result, iteration, "capture-verified-candidate-diff", diff, result.final_diff_path);
        result.final_diff = read_text(result.final_diff_path);
        if (!process_succeeded(diff) || result.final_diff.empty()) {
            result.operation = {
                .state = util::OperationState::failed,
                .exit_code = diff.exit_code,
                .message = "Build and tests passed, but the candidate contained no source change",
            };
            return result;
        }

        const auto commit_log = result.run_directory / "candidate-commit.log";
        const auto commit = run_git(
            result.staged_repository,
            {"-c", "user.name=EngCoder", "-c", "user.email=engcoder@local.invalid",
             "commit", "--quiet", "-m", "EngCoder verified repository candidate"},
            commit_log,
            false,
            stop_token);
        append_event(result, iteration, "seal-verified-candidate", commit, commit_log);
        if (!process_succeeded(commit)) {
            result.operation = {
                .state = util::OperationState::failed,
                .exit_code = commit.exit_code,
                .message = "The verified staged change could not be sealed as an immutable candidate",
            };
            return result;
        }

        const auto candidate_head_log = result.run_directory / "candidate-head.txt";
        const auto candidate_head = run_git(
            result.staged_repository,
            {"rev-parse", "--verify", "HEAD"},
            candidate_head_log,
            false,
            stop_token);
        append_event(result, iteration, "capture-candidate-commit", candidate_head, candidate_head_log);
        result.candidate_commit = trim_ascii_whitespace(read_text(candidate_head_log));
        if (!process_succeeded(candidate_head) || result.candidate_commit.empty()) {
            result.operation = {
                .state = util::OperationState::failed,
                .exit_code = candidate_head.exit_code,
                .message = "The sealed candidate commit could not be verified",
            };
            return result;
        }

        result.candidate_manifest_path = result.run_directory / "candidate.json";
        if (!publish_new_file(result.candidate_manifest_path, candidate_manifest(result))) {
            result.operation = {
                .state = util::OperationState::failed,
                .exit_code = -1,
                .message = "The candidate manifest could not be published atomically",
            };
            return result;
        }

        result.completed = true;
        result.operation = {
            .state = util::OperationState::succeeded,
            .exit_code = 0,
            .message = "Build and tests passed in a private candidate. Review it before promotion; the selected repository is unchanged",
        };
        return result;
    }

    result.operation = {
        .state = util::OperationState::failed,
        .exit_code = -1,
        .message = "Maximum repository repair loops reached; the selected repository is unchanged",
    };
    rollback(staged_request, result);
    return result;
}

PromotionResult promote_verified_candidate(
    const Result& candidate,
    const std::stop_token stop_token) {
    PromotionResult result;
    result.candidate_commit = candidate.candidate_commit;
    if (!candidate.completed || !candidate.compiler_passed || !candidate.tests_passed ||
        candidate.source_repository.empty() || candidate.staged_repository.empty() ||
        candidate.source_head.empty() || candidate.candidate_commit.empty() ||
        !std::filesystem::is_regular_file(candidate.candidate_manifest_path) ||
        !path_is_within(candidate.staged_repository, candidate.run_directory)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "This repository candidate is incomplete or did not pass every required check",
        };
        return result;
    }

    auto branch_suffix = candidate.run_directory.filename().string();
    std::ranges::transform(branch_suffix, branch_suffix.begin(), [](const unsigned char character) {
        return std::isalnum(character) || character == '-' ? static_cast<char>(character) : '-';
    });
    result.branch_name = "engcoder/candidate-" + branch_suffix;
    const auto promotion_log = candidate.run_directory / "promotion.log";

    const auto candidate_head = run_git(
        candidate.staged_repository,
        {"rev-parse", "--verify", candidate.candidate_commit + "^{commit}"},
        promotion_log,
        false,
        stop_token);
    if (!process_succeeded(candidate_head) ||
        trim_ascii_whitespace(read_text(promotion_log)) != candidate.candidate_commit) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = candidate_head.exit_code,
            .message = "The staged candidate commit no longer matches its verified record",
        };
        return result;
    }

    const auto candidate_parent_log = candidate.run_directory / "promotion-candidate-parent.txt";
    const auto candidate_parent = run_git(
        candidate.staged_repository,
        {"rev-parse", "--verify", candidate.candidate_commit + "^"},
        candidate_parent_log,
        false,
        stop_token);
    if (!process_succeeded(candidate_parent) ||
        trim_ascii_whitespace(read_text(candidate_parent_log)) != candidate.source_head) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = candidate_parent.exit_code,
            .message = "The candidate is not a direct child of the exact source revision that was tested",
        };
        return result;
    }
    const auto source_head_log = candidate.run_directory / "promotion-source-head.txt";
    const auto source_head = run_git(
        candidate.source_repository,
        {"rev-parse", "--verify", "HEAD"},
        source_head_log,
        false,
        stop_token);
    if (!process_succeeded(source_head) ||
        trim_ascii_whitespace(read_text(source_head_log)) != candidate.source_head) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = source_head.exit_code,
            .message = "The repository moved since this candidate was tested. Run the task again against the current revision",
        };
        return result;
    }

    const auto status_log = candidate.run_directory / "promotion-source-status.txt";
    const auto status = run_git(
        candidate.source_repository,
        {"status", "--porcelain=v1", "--untracked-files=normal"},
        status_log,
        false,
        stop_token);
    if (!process_succeeded(status) || !trim_ascii_whitespace(read_text(status_log)).empty()) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = status.exit_code,
            .message = "The repository has uncommitted files. Promotion was blocked without changing anything",
        };
        return result;
    }

    const auto reference = "refs/heads/" + result.branch_name;
    const auto existing_log = candidate.run_directory / "promotion-existing-ref.txt";
    const auto existing = run_git(
        candidate.source_repository,
        {"rev-parse", "--verify", reference},
        existing_log,
        false,
        stop_token);
    if (process_succeeded(existing)) {
        if (trim_ascii_whitespace(read_text(existing_log)) == candidate.candidate_commit) {
            result.operation = {
                .state = util::OperationState::succeeded,
                .exit_code = 0,
                .message = "This verified candidate branch was already promoted; the current branch remains unchanged",
            };
            return result;
        }
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = existing.exit_code,
            .message = "The candidate branch name is already in use by different content",
        };
        return result;
    }

    const auto fetch = run_git(
        candidate.source_repository,
        {"fetch", "--no-tags", candidate.staged_repository.string(),
         candidate.candidate_commit + ":" + reference},
        promotion_log,
        false,
        stop_token);
    if (!process_succeeded(fetch)) {
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = fetch.exit_code,
            .message = "The verified candidate could not be published as a review branch",
        };
        return result;
    }

    const auto verify_log = candidate.run_directory / "promotion-verified-ref.txt";
    const auto verify = run_git(
        candidate.source_repository,
        {"rev-parse", "--verify", reference},
        verify_log,
        false,
        stop_token);
    if (!process_succeeded(verify) ||
        trim_ascii_whitespace(read_text(verify_log)) != candidate.candidate_commit) {
        static_cast<void>(run_git(
            candidate.source_repository,
            {"update-ref", "-d", reference, candidate.candidate_commit},
            promotion_log,
            true,
            std::stop_token{}));
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = verify.exit_code,
            .message = "Candidate publication could not be verified and was rolled back",
        };
        return result;
    }

    std::ostringstream receipt;
    receipt << "{\n"
            << "  \"version\": 1,\n"
            << "  \"status\": \"promoted-for-review\",\n"
            << "  \"branch\": \"" << json_escape(result.branch_name) << "\",\n"
            << "  \"candidate_commit\": \"" << json_escape(candidate.candidate_commit) << "\",\n"
            << "  \"source_head_unchanged\": \"" << json_escape(candidate.source_head) << "\"\n"
            << "}\n";
    const auto receipt_path = candidate.run_directory / "promotion.json";
    if (!std::filesystem::exists(receipt_path) && !publish_new_file(receipt_path, receipt.str())) {
        static_cast<void>(run_git(
            candidate.source_repository,
            {"update-ref", "-d", reference, candidate.candidate_commit},
            promotion_log,
            true,
            std::stop_token{}));
        result.operation = {
            .state = util::OperationState::failed,
            .exit_code = -1,
            .message = "The promotion receipt could not be published; the candidate branch was rolled back",
        };
        return result;
    }

    result.operation = {
        .state = util::OperationState::succeeded,
        .exit_code = 0,
        .message = "Verified candidate promoted to a review branch. The current branch and working files remain unchanged",
    };
    return result;
}

} // namespace threadspire::ai::long_horizon