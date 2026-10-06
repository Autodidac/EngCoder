#pragma once

#include "threadspire/ai/app/app_paths.hpp"
#include "threadspire/ai/platform/process.hpp"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>

namespace threadspire::ai::runtime {

class BackendServer final {
public:
    explicit BackendServer(app::AppPaths paths);
    ~BackendServer();

    BackendServer(const BackendServer&) = delete;
    BackendServer& operator=(const BackendServer&) = delete;

    [[nodiscard]] bool start(
        std::stop_token stop_token,
        std::string& message);
    void stop() noexcept;
    [[nodiscard]] bool healthy(std::string& message) const;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string base_url() const;
    [[nodiscard]] std::string serving_generation() const;
    [[nodiscard]] std::string degradation_message() const;
    [[nodiscard]] std::filesystem::path log_path() const;

private:
    app::AppPaths paths_;
    mutable std::mutex mutex_;
    std::condition_variable lifecycle_cv_;
    platform::ManagedProcess process_;
    bool starting_{false};
    bool stopping_{false};
    bool stop_requested_{false};
    std::string base_url_;
    std::string serving_generation_;
    std::string degradation_message_;
    std::filesystem::path log_path_;
};

} // namespace threadspire::ai::runtime
