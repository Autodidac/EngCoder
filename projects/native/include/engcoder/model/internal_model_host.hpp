#pragma once

#include <atomic>
#include <expected>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"

namespace engcoder::model {

class InternalModelHost {
public:
    InternalModelHost(const core::Config& config, std::filesystem::path state_root);
    ~InternalModelHost();

    InternalModelHost(const InternalModelHost&) = delete;
    InternalModelHost& operator=(const InternalModelHost&) = delete;

    [[nodiscard]] std::expected<std::string, core::Error> ensure_started();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] std::string url() const;
    [[nodiscard]] nlohmann::json describe() const;
    [[nodiscard]] std::expected<nlohmann::json, core::Error> residency_status(bool start_if_needed = false);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> prepare_model(std::string_view model_identifier);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> release_idle_addons();

private:
    [[nodiscard]] bool internal_generation_available() const;
    [[nodiscard]] std::expected<int, core::Error> wait_for_ready();
    void set_last_error(std::string message);
    [[nodiscard]] std::string last_error() const;

    const core::Config& config_;
    std::filesystem::path state_root_;
    std::filesystem::path ready_file_;
    std::filesystem::path control_root_;
    mutable std::mutex mutex_;
    mutable std::mutex error_mutex_;
    std::jthread worker_;
    std::atomic_bool running_{false};
    std::atomic_bool worker_exited_{true};
    std::atomic_int port_{0};
    std::string last_error_;
};

} // namespace engcoder::model
