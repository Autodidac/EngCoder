#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/models/model_registry.hpp"

namespace engcoder::residency {

using json = nlohmann::json;

enum class ResidencyState {
    disk,
    ram,
    loading,
    vram,
    in_use,
    evicting,
    training,
    failed,
};

struct ResidencyEntry {
    std::string component_id;
    ResidencyState state{ResidencyState::disk};
    std::uint64_t estimated_bytes{};
    bool pinned{};
    std::uint32_t use_count{};
    std::chrono::steady_clock::time_point last_used{std::chrono::steady_clock::now()};
};

class ModelResidencyManager {
public:
    explicit ModelResidencyManager(double vram_budget_gib = 13.5);

    void sync(const models::ModelRegistry& registry);
    void touch(std::string_view component_id, ResidencyState desired = ResidencyState::vram);
    void begin_use(std::string_view component_id);
    void end_use(std::string_view component_id);
    void mark_training(std::string_view component_id, bool active);
    void mark_failed(std::string_view component_id);

    [[nodiscard]] json describe() const;
    [[nodiscard]] std::uint64_t budget_bytes() const noexcept { return budget_bytes_; }

private:
    void enforce_budget_locked();
    [[nodiscard]] std::uint64_t resident_bytes_locked() const;

    std::uint64_t budget_bytes_{};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, ResidencyEntry> entries_;
};

[[nodiscard]] std::string_view to_string(ResidencyState state) noexcept;

} // namespace engcoder::residency
