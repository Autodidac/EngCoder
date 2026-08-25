module;
#include "module_prelude.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

module threadspire.platform;

namespace threadspire::ai::platform {
namespace {

[[nodiscard]] std::vector<char*> make_argv(
    std::string& executable,
    std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(executable.data());
    for (auto& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    return argv;
}

[[nodiscard]] int open_log(
    const std::filesystem::path& path,
    const bool append,
    std::string& error) {
    if (path.empty()) {
        return ::open("/dev/null", O_WRONLY);
    }

    std::error_code filesystem_error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
    }
    if (filesystem_error) {
        error = "unable to create log directory: " + filesystem_error.message();
        return -1;
    }

    const auto flags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
    const auto descriptor = ::open(path.c_str(), flags, 0644);
    if (descriptor < 0) {
        error = "unable to open log file: " + std::string(std::strerror(errno));
    }
    return descriptor;
}

} // namespace

ProcessResult run_process(const ProcessSpec& spec, StopPredicate should_stop) {
    ProcessResult result;
    const auto start = std::chrono::steady_clock::now();

    auto executable = spec.executable.string();
    auto arguments = spec.arguments;
    auto argv = make_argv(executable, arguments);

    std::string log_error;
    const auto log_descriptor = open_log(spec.log_file, spec.append_log, log_error);
    if (log_descriptor < 0) {
        result.error = std::move(log_error);
        return result;
    }

    const auto process_id = ::fork();
    if (process_id < 0) {
        result.error = "fork failed: " + std::string(std::strerror(errno));
        ::close(log_descriptor);
        return result;
    }

    if (process_id == 0) {
        ::setpgid(0, 0);
        if (!spec.working_directory.empty() && ::chdir(spec.working_directory.c_str()) != 0) {
            ::dprintf(log_descriptor, "unable to change working directory: %s\n", std::strerror(errno));
            ::_exit(126);
        }

        ::dup2(log_descriptor, STDOUT_FILENO);
        ::dup2(log_descriptor, STDERR_FILENO);
        ::close(log_descriptor);
        ::execvp(executable.c_str(), argv.data());
        ::_exit(127);
    }

    result.launched = true;
    ::close(log_descriptor);

    int status = 0;
    while (true) {
        const auto wait_result = ::waitpid(process_id, &status, WNOHANG);
        if (wait_result == process_id) {
            break;
        }
        if (wait_result < 0) {
            result.error = "waitpid failed: " + std::string(std::strerror(errno));
            break;
        }

        if (should_stop && should_stop()) {
            result.cancelled = true;
            ::kill(-process_id, SIGTERM);

            for (int attempt = 0; attempt < 20; ++attempt) {
                if (::waitpid(process_id, &status, WNOHANG) == process_id) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (::waitpid(process_id, &status, WNOHANG) == 0) {
                ::kill(-process_id, SIGKILL);
                ::waitpid(process_id, &status, 0);
            }
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

bool launch_detached(const ProcessSpec& spec, std::string& error) {
    auto executable = spec.executable.string();
    auto arguments = spec.arguments;
    auto argv = make_argv(executable, arguments);

    std::string log_error;
    const auto log_descriptor = open_log(spec.log_file, spec.append_log, log_error);
    if (log_descriptor < 0) {
        error = std::move(log_error);
        return false;
    }

    const auto first_child = ::fork();
    if (first_child < 0) {
        error = "fork failed: " + std::string(std::strerror(errno));
        ::close(log_descriptor);
        return false;
    }

    if (first_child == 0) {
        if (::setsid() < 0) {
            ::_exit(126);
        }

        const auto second_child = ::fork();
        if (second_child < 0) {
            ::_exit(126);
        }
        if (second_child > 0) {
            ::_exit(0);
        }

        if (!spec.working_directory.empty() && ::chdir(spec.working_directory.c_str()) != 0) {
            ::_exit(126);
        }
        ::dup2(log_descriptor, STDOUT_FILENO);
        ::dup2(log_descriptor, STDERR_FILENO);
        ::close(log_descriptor);
        ::execvp(executable.c_str(), argv.data());
        ::_exit(127);
    }

    ::close(log_descriptor);
    int status = 0;
    if (::waitpid(first_child, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "unable to launch detached process";
        return false;
    }
    return true;
}

} // namespace threadspire::ai::platform

namespace threadspire::ai::platform {

struct ManagedProcess::Impl final {
    pid_t process_id{-1};
    int last_exit_code{-1};
};

ManagedProcess::ManagedProcess() : impl_(std::make_unique<Impl>()) {}
ManagedProcess::~ManagedProcess() { stop(); }
ManagedProcess::ManagedProcess(ManagedProcess&&) noexcept = default;
ManagedProcess& ManagedProcess::operator=(ManagedProcess&&) noexcept = default;

bool ManagedProcess::start(const ProcessSpec& spec, std::string& error) {
    stop();
    auto executable = spec.executable.string();
    auto arguments = spec.arguments;
    auto argv = make_argv(executable, arguments);

    std::string log_error;
    const auto log_descriptor = open_log(spec.log_file, spec.append_log, log_error);
    if (log_descriptor < 0) {
        error = std::move(log_error);
        return false;
    }

    const auto process_id = ::fork();
    if (process_id < 0) {
        error = "fork failed: " + std::string(std::strerror(errno));
        ::close(log_descriptor);
        return false;
    }
    if (process_id == 0) {
        ::setpgid(0, 0);
        if (!spec.working_directory.empty() && ::chdir(spec.working_directory.c_str()) != 0) {
            ::_exit(126);
        }
        ::dup2(log_descriptor, STDOUT_FILENO);
        ::dup2(log_descriptor, STDERR_FILENO);
        ::close(log_descriptor);
        ::execvp(executable.c_str(), argv.data());
        ::_exit(127);
    }

    ::close(log_descriptor);
    impl_->process_id = process_id;
    impl_->last_exit_code = -1;
    return true;
}

void ManagedProcess::stop() noexcept {
    if (!impl_ || impl_->process_id <= 0) {
        return;
    }
    int status = 0;
    const auto waited = ::waitpid(impl_->process_id, &status, WNOHANG);
    if (waited == 0) {
        ::kill(-impl_->process_id, SIGTERM);
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (::waitpid(impl_->process_id, &status, WNOHANG) == impl_->process_id) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (::waitpid(impl_->process_id, &status, WNOHANG) == 0) {
            ::kill(-impl_->process_id, SIGKILL);
            ::waitpid(impl_->process_id, &status, 0);
        }
    }
    if (WIFEXITED(status)) {
        impl_->last_exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        impl_->last_exit_code = 128 + WTERMSIG(status);
    }
    impl_->process_id = -1;
}

bool ManagedProcess::running() const noexcept {
    if (!impl_ || impl_->process_id <= 0) {
        return false;
    }
    int status = 0;
    return ::waitpid(impl_->process_id, &status, WNOHANG) == 0;
}

int ManagedProcess::exit_code() const noexcept {
    return impl_ ? impl_->last_exit_code : -1;
}

} // namespace threadspire::ai::platform
