#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace threadspire::ai::platform {

struct ProcessSpec final {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::filesystem::path log_file;
    bool append_log{false};
};

struct ProcessResult final {
    bool launched{false};
    bool cancelled{false};
    int exit_code{-1};
    std::chrono::milliseconds duration{0};
    std::string error;
};

using StopPredicate = std::function<bool()>;

[[nodiscard]] ProcessResult run_process(
    const ProcessSpec& spec,
    StopPredicate should_stop = {});

[[nodiscard]] bool launch_detached(
    const ProcessSpec& spec,
    std::string& error);

class ManagedProcess final {
public:
    ManagedProcess();
    ~ManagedProcess();

    ManagedProcess(const ManagedProcess&) = delete;
    ManagedProcess& operator=(const ManagedProcess&) = delete;
    ManagedProcess(ManagedProcess&&) noexcept;
    ManagedProcess& operator=(ManagedProcess&&) noexcept;

    [[nodiscard]] bool start(const ProcessSpec& spec, std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] int exit_code() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadspire::ai::platform
