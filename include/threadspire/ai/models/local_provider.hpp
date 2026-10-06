#pragma once

#include "threadspire/ai/models/model_registry.hpp"

#include <chrono>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::models {

struct ProviderDiscoveryReport final {
    std::vector<ModelProfile> models;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool found_any() const noexcept {
        return !models.empty();
    }
};

struct LocalModelEndpointResponse final {
    int status_code{0};
    std::string body;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty() && status_code >= 200 && status_code < 300;
    }
};

using LocalModelEndpointRequest = std::function<LocalModelEndpointResponse(
    std::string_view method,
    std::string_view url,
    std::string_view body,
    std::chrono::milliseconds timeout)>;

struct LocalModelActivationResult final {
    bool ready{false};
    bool loaded_by_engcoder{false};
    std::string message;
};

[[nodiscard]] ProviderDiscoveryReport discover_local_models(
    std::chrono::milliseconds timeout_per_endpoint = std::chrono::milliseconds(700),
    std::stop_token stop_token = {});

[[nodiscard]] bool merge_discovered_models(
    ModelRegistry& registry,
    const std::vector<ModelProfile>& discovered);

[[nodiscard]] bool is_managed_lm_studio_teacher(
    const ModelProfile& model);

[[nodiscard]] LocalModelActivationResult ensure_managed_lm_studio_teacher_ready(
    const ModelProfile& model,
    std::chrono::milliseconds timeout = std::chrono::minutes(2),
    std::stop_token stop_token = {},
    LocalModelEndpointRequest request = {});

[[nodiscard]] ModelProfile make_manual_local_model(
    std::string endpoint,
    std::string model_name,
    std::string protocol,
    bool coding_agent,
    bool teacher);

[[nodiscard]] bool is_internal_model(const ModelProfile& model) noexcept;

} // namespace threadspire::ai::models
