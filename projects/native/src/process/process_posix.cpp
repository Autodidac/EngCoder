#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "engcoder/process/process.hpp"
#ifndef _WIN32

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <thread>

extern char** environ;

namespace engcoder::process::detail {
namespace {

void close_fd(int& fd) noexcept {
    if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
    }
}

core::Error posix_error(std::string_view operation, int code = errno) {
    return {
        core::make_error_code(core::Errc::process_failed),
        std::string(operation) + ": " + std::strerror(code)
    };
}

void append_tail(std::string& output, const char* data, std::size_t size, std::size_t limit) {
    if (limit == 0 || size == 0) return;
    if (size >= limit) {
        output.assign(data + (size - limit), limit);
        return;
    }
    if (output.size() + size > limit) {
        output.erase(0, output.size() + size - limit);
    }
    output.append(data, size);
}

void read_fd(int fd, std::string& output, std::size_t limit) {
    char buffer[4096];
    for (;;) {
        const auto count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) return;
        if (count < 0) {
            if (errno == EINTR) continue;
            return;
        }
        append_tail(output, buffer, static_cast<std::size_t>(count), limit);
    }
}

} // namespace

std::expected<Result, core::Error> run_posix(const Request& request, std::stop_token stop) {
    int stdout_pipe[2]{-1, -1};
    int stderr_pipe[2]{-1, -1};

    if (::pipe(stdout_pipe) != 0) {
        return std::unexpected(posix_error("pipe(stdout)"));
    }
    if (::pipe(stderr_pipe) != 0) {
        const auto error = posix_error("pipe(stderr)");
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        return std::unexpected(error);
    }

    posix_spawn_file_actions_t actions{};
    int rc = posix_spawn_file_actions_init(&actions);
    if (rc != 0) {
        close_fd(stdout_pipe[0]); close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]); close_fd(stderr_pipe[1]);
        return std::unexpected(posix_error("posix_spawn_file_actions_init", rc));
    }

    auto add_action = [&](int action_rc) -> bool {
        if (action_rc == 0) {
            return true;
        }
        rc = action_rc;
        return false;
    };

    bool actions_ok =
        add_action(posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO)) &&
        add_action(posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO)) &&
        add_action(posix_spawn_file_actions_addclose(&actions, stdout_pipe[0])) &&
        add_action(posix_spawn_file_actions_addclose(&actions, stderr_pipe[0])) &&
        add_action(posix_spawn_file_actions_addclose(&actions, stdout_pipe[1])) &&
        add_action(posix_spawn_file_actions_addclose(&actions, stderr_pipe[1]));

#if defined(__APPLE__) || defined(__linux__)
    if (actions_ok && !request.working_directory.empty()) {
        actions_ok = add_action(posix_spawn_file_actions_addchdir_np(
            &actions, request.working_directory.c_str()));
    }
#endif

    if (!actions_ok) {
        posix_spawn_file_actions_destroy(&actions);
        close_fd(stdout_pipe[0]); close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]); close_fd(stderr_pipe[1]);
        return std::unexpected(posix_error("posix_spawn file action", rc));
    }

    std::vector<std::string> argument_storage;
    argument_storage.reserve(request.arguments.size() + 1);
    argument_storage.push_back(request.executable);
    argument_storage.insert(
        argument_storage.end(), request.arguments.begin(), request.arguments.end());

    std::vector<std::string> environment_storage;
    std::vector<char*> environment_argv;
    char** environment_ptr = environ;
    if (!request.environment.empty()) {
        for (char** current = environ; current != nullptr && *current != nullptr; ++current) {
            environment_storage.emplace_back(*current);
        }
        for (const auto& [key, value] : request.environment) {
            const auto prefix = key + "=";
            std::erase_if(environment_storage, [&](const std::string& item) {
                return item.starts_with(prefix);
            });
            environment_storage.push_back(prefix + value);
        }
        environment_argv.reserve(environment_storage.size() + 1);
        for (auto& item : environment_storage) environment_argv.push_back(item.data());
        environment_argv.push_back(nullptr);
        environment_ptr = environment_argv.data();
    }

    std::vector<char*> argv;
    argv.reserve(argument_storage.size() + 1);
    for (auto& argument : argument_storage) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    posix_spawnattr_t attributes{};
    rc = posix_spawnattr_init(&attributes);
    if (rc != 0) {
        posix_spawn_file_actions_destroy(&actions);
        close_fd(stdout_pipe[0]); close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]); close_fd(stderr_pipe[1]);
        return std::unexpected(posix_error("posix_spawnattr_init", rc));
    }

    rc = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    if (rc == 0) {
        rc = posix_spawnattr_setpgroup(&attributes, 0);
    }
    if (rc != 0) {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attributes);
        close_fd(stdout_pipe[0]); close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]); close_fd(stderr_pipe[1]);
        return std::unexpected(posix_error("posix_spawnattr process group", rc));
    }

    pid_t pid{};
    rc = posix_spawnp(
        &pid,
        request.executable.c_str(),
        &actions,
        &attributes,
        argv.data(),
        environment_ptr);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    if (rc != 0) {
        close_fd(stdout_pipe[0]);
        close_fd(stderr_pipe[0]);
        return std::unexpected(posix_error("posix_spawnp", rc));
    }

    Result result;
    std::jthread stdout_reader([&] {
        read_fd(stdout_pipe[0], result.stdout_text, request.max_output_bytes);
    });
    std::jthread stderr_reader([&] {
        read_fd(stderr_pipe[0], result.stderr_text, request.max_output_bytes);
    });

    const auto deadline = std::chrono::steady_clock::now() + request.timeout;
    int status = 0;
    bool wait_failed = false;
    int wait_error_code = 0;

    for (;;) {
        const auto waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            break;
        }
        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            wait_failed = true;
            wait_error_code = errno;
            break;
        }

        if (stop.stop_requested()) {
            result.cancelled = true;
            (void)::kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            (void)::kill(-pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            (void)::kill(-pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            (void)::kill(-pid, SIGKILL);
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }

    if (stdout_reader.joinable()) { stdout_reader.join(); }
    if (stderr_reader.joinable()) { stderr_reader.join(); }
    close_fd(stdout_pipe[0]);
    close_fd(stderr_pipe[0]);

    if (wait_failed) {
        return std::unexpected(posix_error("waitpid", wait_error_code));
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    return result;
}

} // namespace engcoder::process::detail
#endif
