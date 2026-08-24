#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace threadspire::ai::resource {

struct ResourceSnapshot final {
    double cpu_utilization{0.0};
    double memory_available_gib{0.0};
    double gpu_utilization{-1.0};
    double gpu_memory_available_gib{-1.0};
    bool on_ac_power{true};
    bool thermal_pressure{false};
    std::chrono::steady_clock::time_point captured_at{std::chrono::steady_clock::now()};
};

struct ResourcePolicy final {
    double maximum_background_cpu{0.75};
    double maximum_background_gpu{0.82};
    double minimum_available_memory_gib{3.0};
    double minimum_available_gpu_memory_gib{2.5};
    bool require_ac_power{false};
    bool stop_on_thermal_pressure{true};
};

struct TrainingBudget final {
    bool permitted{false};
    std::size_t max_steps{1};
    std::size_t maximum_sequence_tokens{512};
    std::size_t lora_rank{4};
    std::chrono::milliseconds retry_after{1000};
    std::string reason;
};

class ResourceGovernor final {
public:
    explicit ResourceGovernor(ResourcePolicy policy = {});

    [[nodiscard]] TrainingBudget plan_training(
        const ResourceSnapshot& snapshot,
        bool inference_active,
        std::size_t requested_steps,
        std::size_t requested_sequence_tokens,
        std::size_t requested_rank) const;
    [[nodiscard]] const ResourcePolicy& policy() const noexcept;

private:
    ResourcePolicy policy_;
};

[[nodiscard]] ResourceSnapshot sample_system_resources() noexcept;

} // namespace threadspire::ai::resource
