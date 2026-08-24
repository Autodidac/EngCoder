#include "threadspire/ai/resource/resource_governor.hpp"

#include <algorithm>
#include <cmath>

namespace threadspire::ai::resource {

ResourceGovernor::ResourceGovernor(ResourcePolicy policy) : policy_(policy) {}

TrainingBudget ResourceGovernor::plan_training(
    const ResourceSnapshot& snapshot,
    const bool inference_active,
    const std::size_t requested_steps,
    const std::size_t requested_sequence_tokens,
    const std::size_t requested_rank) const {
    TrainingBudget budget{
        .permitted = false,
        .max_steps = std::max<std::size_t>(1, requested_steps),
        .maximum_sequence_tokens = std::max<std::size_t>(256, requested_sequence_tokens),
        .lora_rank = std::max<std::size_t>(2, requested_rank),
        .retry_after = std::chrono::milliseconds(750),
        .reason = {},
    };
    if (inference_active) {
        budget.reason = "inference owns resource priority";
        return budget;
    }
    if (policy_.require_ac_power && !snapshot.on_ac_power) {
        budget.reason = "background learning is disabled on battery power";
        budget.retry_after = std::chrono::seconds(10);
        return budget;
    }
    if (policy_.stop_on_thermal_pressure && snapshot.thermal_pressure) {
        budget.reason = "thermal pressure requires learning to yield";
        budget.retry_after = std::chrono::seconds(5);
        return budget;
    }
    if (snapshot.memory_available_gib > 0.0 &&
        snapshot.memory_available_gib < policy_.minimum_available_memory_gib) {
        budget.reason = "insufficient available system memory";
        budget.retry_after = std::chrono::seconds(2);
        return budget;
    }
    if (snapshot.gpu_memory_available_gib >= 0.0 &&
        snapshot.gpu_memory_available_gib < policy_.minimum_available_gpu_memory_gib) {
        budget.reason = "insufficient available GPU memory";
        return budget;
    }
    if (snapshot.cpu_utilization > policy_.maximum_background_cpu) {
        budget.reason = "CPU is busy with foreground work";
        return budget;
    }
    if (snapshot.gpu_utilization >= 0.0 &&
        snapshot.gpu_utilization > policy_.maximum_background_gpu) {
        budget.reason = "GPU is busy with foreground work";
        return budget;
    }

    const auto cpu_headroom = std::clamp(
        (policy_.maximum_background_cpu - snapshot.cpu_utilization) /
            std::max(0.05, policy_.maximum_background_cpu),
        0.15,
        1.0);
    const auto gpu_headroom = snapshot.gpu_utilization < 0.0
        ? 0.70
        : std::clamp(
              (policy_.maximum_background_gpu - snapshot.gpu_utilization) /
                  std::max(0.05, policy_.maximum_background_gpu),
              0.15,
              1.0);
    const auto headroom = std::min(cpu_headroom, gpu_headroom);
    budget.permitted = true;
    budget.max_steps = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::ceil(static_cast<double>(requested_steps) * headroom)));
    budget.maximum_sequence_tokens = std::max<std::size_t>(
        512,
        static_cast<std::size_t>(static_cast<double>(requested_sequence_tokens) *
                                 std::clamp(0.55 + headroom * 0.45, 0.55, 1.0)));
    budget.lora_rank = std::max<std::size_t>(
        4,
        static_cast<std::size_t>(static_cast<double>(requested_rank) *
                                 std::clamp(0.50 + headroom * 0.50, 0.50, 1.0)));
    budget.reason = headroom > 0.80
        ? "full background-learning budget available"
        : "reduced learning budget selected from current headroom";
    return budget;
}

const ResourcePolicy& ResourceGovernor::policy() const noexcept {
    return policy_;
}

} // namespace threadspire::ai::resource
