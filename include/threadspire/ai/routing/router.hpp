#pragma once

#include "threadspire/ai/models/model_registry.hpp"
#include "threadspire/ai/routing/task.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::routing {

enum class RouteMode {
    direct,
    cascade,
    hedged,
    agentic,
};

struct RouteStage final {
    std::string role;
    std::string model_id;
    std::size_t max_output_tokens{256};
    bool parallel{false};
};

struct RoutingPlan final {
    RouteMode mode{RouteMode::direct};
    TaskAnalysis analysis;
    std::vector<RouteStage> stages;
    double expected_latency_ms{0.0};
    std::string rationale;
};

[[nodiscard]] std::string_view to_string(RouteMode mode) noexcept;

class AdaptiveRouter final {
public:
    [[nodiscard]] RoutingPlan plan(
        const TaskAnalysis& analysis,
        const models::ModelRegistry& registry) const;

private:
    [[nodiscard]] const models::ModelProfile* fastest_capable(
        const TaskAnalysis& analysis,
        const models::ModelRegistry& registry,
        double minimum_quality,
        std::string_view preferred_capability = {}) const;
};

} // namespace threadspire::ai::routing
