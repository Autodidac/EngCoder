#pragma once

#include <atomic>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"
#include "engcoder/models/model_registry.hpp"
#include "engcoder/residency/model_residency_manager.hpp"

namespace engcoder::training {

using json = nlohmann::json;

struct TrainingRequest {
    std::string scope{"keymaker-foundation"};
    std::uint32_t max_steps{500};
    std::uint32_t checkpoint_steps{50};
    bool require_cuda{true};
};

class TrainingService {
public:
    TrainingService(
        const core::Config& config,
        std::filesystem::path state_root,
        models::ModelRegistry& models,
        residency::ModelResidencyManager& residency);
    ~TrainingService();

    TrainingService(const TrainingService&) = delete;
    TrainingService& operator=(const TrainingService&) = delete;

    [[nodiscard]] std::expected<json, core::Error> doctor() const;
    [[nodiscard]] std::expected<json, core::Error> initialize(std::string_view profile = "micro", bool force = false);
    [[nodiscard]] std::expected<void, core::Error> start(TrainingRequest request);
    void cancel();
    void set_paused(bool paused);
    void begin_inference();
    void end_inference();

    [[nodiscard]] json status() const;
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }

private:
    [[nodiscard]] std::expected<json, core::Error> run_worker(const std::vector<std::string>& arguments, std::stop_token stop = {}) const;
    void ensure_training_data() const;
    void run_training(std::stop_token stop, TrainingRequest request);
    void write_marker(const std::filesystem::path& path, bool present) const;

    const core::Config& config_;
    std::filesystem::path state_root_;
    std::filesystem::path control_root_;
    models::ModelRegistry& models_;
    residency::ModelResidencyManager& residency_;
    mutable std::mutex mutex_;
    json last_status_ = json{{"state", "idle"}};
    std::jthread worker_;
    std::atomic_bool running_{false};
    std::atomic_uint32_t inference_users_{0};
};

} // namespace engcoder::training
