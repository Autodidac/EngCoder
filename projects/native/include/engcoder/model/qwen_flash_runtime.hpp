#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <stop_token>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"
#include "engcoder/model/runtime_model.hpp"

namespace engcoder::model {

// Native Qwen runtime owned by EngCoder. The inference backend is only the
// execution engine: Architect/System 1, training, tools, memory and compaction
// remain EngCoder systems. Sparse expert caching is capability-qualified per
// runtime/model layout so an experimental optimization cannot take down the
// foundation model. The OpenAI-compatible HTTP surface is an implementation
// detail.
class QwenFlashRuntime {
public:
    QwenFlashRuntime(const core::Config& config, std::filesystem::path state_root);
    ~QwenFlashRuntime();

    QwenFlashRuntime(const QwenFlashRuntime&) = delete;
    QwenFlashRuntime& operator=(const QwenFlashRuntime&) = delete;

    [[nodiscard]] std::expected<std::string, core::Error> ensure_started(const RuntimeModelProfile& profile, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(), std::stop_token stop = {});
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] std::string url() const;
    [[nodiscard]] std::string active_model_id() const;
    [[nodiscard]] nlohmann::json describe() const;
    [[nodiscard]] nlohmann::json cache_status(const RuntimeModelProfile& profile) const;
    [[nodiscard]] std::filesystem::path discover_server() const;
    [[nodiscard]] bool runtime_verified() const;

private:
    struct RuntimeCapabilities {
        std::string recipe;
        bool qwen38_q2_padded_down{false};
        bool expert_cache_candidate{false};
    };

    struct HardwareSnapshot {
        double ram_gib{};
        std::uint64_t vram_mib{};
        std::uint32_t logical_threads{};
        std::string gpu_name;
    };

    [[nodiscard]] HardwareSnapshot probe_hardware() const;
    [[nodiscard]] RuntimeCapabilities capabilities_for_server(const std::filesystem::path& server) const;
    [[nodiscard]] std::optional<bool> expert_cache_qualification(
        const RuntimeModelProfile& profile, const RuntimeCapabilities& capabilities) const;
    void record_expert_cache_qualification(
        const RuntimeModelProfile& profile, const RuntimeCapabilities& capabilities, bool supported, std::string_view reason) const;
    [[nodiscard]] RuntimeModelProfile resolve_profile(
        const RuntimeModelProfile& profile, const HardwareSnapshot& hardware, const RuntimeCapabilities& capabilities) const;
    [[nodiscard]] RuntimeModelProfile conservative_profile(
        const RuntimeModelProfile& profile, const HardwareSnapshot& hardware, const RuntimeCapabilities& capabilities) const;
    [[nodiscard]] std::vector<std::string> build_arguments(
        const RuntimeModelProfile& profile, bool minimal_compatibility = false) const;
    [[nodiscard]] std::string command_string(const std::filesystem::path& server, const std::vector<std::string>& args) const;
    [[nodiscard]] std::expected<void, core::Error> wait_until_ready(std::chrono::steady_clock::time_point deadline, std::stop_token stop);
    [[nodiscard]] std::filesystem::path cache_repo_root(const RuntimeModelProfile& profile) const;
    [[nodiscard]] std::filesystem::path cached_model_path(const RuntimeModelProfile& profile) const;
    void adopt_existing_backend();
    void set_last_error(std::string message);
    [[nodiscard]] std::string last_error() const;

    const core::Config& config_;
    std::filesystem::path state_root_;
    mutable std::mutex mutex_;
    std::condition_variable startup_cv_;
    mutable std::mutex error_mutex_;
    std::jthread worker_;
    bool startup_in_progress_{false};
    std::string startup_model_id_;
    std::string last_startup_model_id_;
    std::optional<core::Error> last_startup_error_;
    std::atomic_bool running_{false};
    std::atomic_bool worker_exited_{true};
    std::string active_model_id_;
    std::uint32_t active_context_tokens_{0};
    std::uint32_t active_parallel_slots_{0};
    bool active_safety_fallback_{false};
    std::string startup_note_;
    std::string last_command_;
    nlohmann::json last_hardware_ = nlohmann::json::object();
    nlohmann::json last_effective_profile_ = nlohmann::json::object();
    std::string last_error_;
};

} // namespace engcoder::model
