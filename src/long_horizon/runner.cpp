#include "threadspire/ai/long_horizon/runner.hpp"

#include "threadspire/ai/backend/completion_client.hpp"
#include "threadspire/ai/memory/episode_store.hpp"
#include "threadspire/ai/platform/process.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <random>
#include <ranges>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

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
            .message = "Autonomous coding requires a Git worktree so patches can be validated and rolled back",
        };
        return false;
    }
    if (!request.require_clean_worktree) {
        return true;
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
                "You are Epoch's autonomous C++23 repository worker. Return only one complete unified diff. "
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
    if (!request.rollback_on_failure || !request.require_clean_worktree) {
        return;
    }
    const auto log = result.run_directory / "rollback.log";
    const auto reset = run_git(
        request.repository,
        {"reset", "--hard", "HEAD"},
        log,
        false,
        std::stop_token{});
    append_event(result, result.iterations, "rollback", reset, log);
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

    if (!clean_worktree(request, result, stop_token)) {
        return result;
    }

    const auto task_file = result.run_directory / "task.txt";
    write_text(task_file, request.task);
    const auto initial_diagnostics = result.run_directory / "initial-context.txt";
    write_text(initial_diagnostics, "Initial implementation pass. No compiler or test diagnostics exist yet.");
    const auto initial_patch = result.run_directory / "initial-implementation.diff";
    const auto initial_log = result.run_directory / "initial-implementation.log";
    const auto initial = generate_repair(
        request,
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
        return cancelled(std::move(result), request);
    }
    if (!process_succeeded(initial) ||
        !check_and_apply_patch(request, result, 0, initial_patch, initial_log, stop_token)) {
        rollback(request, result);
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
            return cancelled(std::move(result), request);
        }

        const auto iteration_directory = result.run_directory / ("iteration-" + std::to_string(iteration));
        std::filesystem::create_directories(iteration_directory);
        const auto build_log = iteration_directory / "build.log";
        const auto build = run_shell(
            request.repository,
            request.build_command,
            build_log,
            false,
            stop_token);
        append_event(result, iteration, "build", build, build_log);
        if (build.cancelled || stop_token.stop_requested()) {
            return cancelled(std::move(result), request);
        }
        result.compiler_passed = process_succeeded(build);
        if (!result.compiler_passed) {
            const auto patch = iteration_directory / "compile-repair.diff";
            const auto repair_log = iteration_directory / "compile-repair.log";
            const auto repair = generate_repair(
                request,
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
                return cancelled(std::move(result), request);
            }
            if (!process_succeeded(repair) ||
                !check_and_apply_patch(request, result, iteration, patch, repair_log, stop_token)) {
                rollback(request, result);
                if (result.operation.message.empty()) {
                    result.operation = {
                        .state = util::OperationState::failed,
                        .exit_code = repair.exit_code,
                        .message = repair.error.empty()
                            ? "Build failed and no safe repair could be applied"
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
                request.repository,
                request.test_command,
                test_log,
                false,
                stop_token);
            append_event(result, iteration, "tests", tests, test_log);
            if (tests.cancelled || stop_token.stop_requested()) {
                return cancelled(std::move(result), request);
            }
            result.tests_passed = process_succeeded(tests);
            if (!result.tests_passed) {
                const auto patch = iteration_directory / "test-repair.diff";
                const auto repair_log = iteration_directory / "test-repair.log";
                const auto repair = generate_repair(
                    request,
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
                    return cancelled(std::move(result), request);
                }
                if (!process_succeeded(repair) ||
                    !check_and_apply_patch(request, result, iteration, patch, repair_log, stop_token)) {
                    rollback(request, result);
                    if (result.operation.message.empty()) {
                        result.operation = {
                            .state = util::OperationState::failed,
                            .exit_code = repair.exit_code,
                            .message = repair.error.empty()
                                ? "Tests failed and no safe repair could be applied"
                                : repair.error,
                        };
                    }
                    return result;
                }
                continue;
            }
        }

        const auto diff_log = result.run_directory / "final.diff";
        const auto diff = run_git(
            request.repository,
            {"diff", "--cached", "--no-ext-diff", "--binary", "--"},
            diff_log,
            false,
            stop_token);
        append_event(result, iteration, "capture-final-diff", diff, diff_log);
        result.final_diff_path = diff_log;
        result.final_diff = read_text(diff_log);
        result.completed = true;
        result.operation = {
            .state = util::OperationState::succeeded,
            .exit_code = 0,
            .message = "Build and tests passed; generated changes remain staged for review",
        };
        return result;
    }

    result.operation = {
        .state = util::OperationState::failed,
        .exit_code = -1,
        .message = "Maximum long-horizon iterations reached",
    };
    rollback(request, result);
    return result;
}

} // namespace threadspire::ai::long_horizon
