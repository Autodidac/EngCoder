#pragma once

#include "threadspire/ai/app/app_paths.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace threadspire::ai::runtime {

struct InternalModelState final {
    bool exists{false};
    bool manifest_valid{false};
    bool trained{false};
    bool bootstrap_complete{false};
    bool full_backbone_trained{false};
    bool generation_ready{false};
    bool bootstrap_policy_current{false};
    std::string generation;
    std::string bootstrap_policy;
    std::string status;
    std::string profile_name;
    std::string error;
    std::uint64_t parameter_count{0};
    std::uint64_t tokens_seen{0};
    std::size_t steps{0};
    std::size_t bootstrap_target_steps{0};
};

struct InternalModelProgress final {
    bool available{false};
    bool stale{false};
    bool resumable{false};
    bool bootstrap_complete{false};
    bool full_backbone_trained{false};
    bool generation_ready{false};
    std::string phase;
    std::string message;
    std::string error;
    std::string generation;
    std::size_t step{0};
    std::size_t maximum_steps{0};
    std::size_t prior_total_steps{0};
    std::size_t total_steps{0};
    std::size_t bootstrap_target_steps{0};
    std::uint64_t tokens_seen{0};
    double percent{0.0};
    double elapsed_seconds{0.0};
    std::optional<double> eta_seconds;
    std::int64_t heartbeat_unix_ms{0};
    int process_id{0};
};

[[nodiscard]] InternalModelState inspect_internal_model_state(
    const app::AppPaths& paths) noexcept;

[[nodiscard]] InternalModelProgress read_internal_model_progress(
    const app::AppPaths& paths) noexcept;

[[nodiscard]] std::size_t default_bootstrap_target(
    const InternalModelState& state) noexcept;

} // namespace threadspire::ai::runtime
