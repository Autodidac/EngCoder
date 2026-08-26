#pragma once

#include "threadspire/ai/app/app_paths.hpp"
#include "threadspire/ai/platform/process.hpp"

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
    [[nodiscard]] std::filesystem::path log_path() const;

private:
    app::AppPaths paths_;
    mutable std::mutex mutex_;
    platform::ManagedProcess process_;
    std::string base_url_;
    std::filesystem::path log_path_;
};

} // namespace threadspire::ai::runtime
