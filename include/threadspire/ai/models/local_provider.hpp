#pragma once

#include "threadspire/ai/models/model_registry.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace threadspire::ai::models {

struct ProviderDiscoveryReport final {
    std::vector<ModelProfile> models;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool found_any() const noexcept {
        return !models.empty();
    }
};

[[nodiscard]] ProviderDiscoveryReport discover_local_models(
    std::chrono::milliseconds timeout_per_endpoint = std::chrono::milliseconds(700));

[[nodiscard]] bool merge_discovered_models(
    ModelRegistry& registry,
    const std::vector<ModelProfile>& discovered);

[[nodiscard]] ModelProfile make_manual_local_model(
    std::string endpoint,
    std::string model_name,
    std::string protocol,
    bool coding_agent,
    bool teacher);

[[nodiscard]] bool is_internal_model(const ModelProfile& model) noexcept;

} // namespace threadspire::ai::models
