#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"
#include "engcoder/model/model_provider.hpp"
#include "engcoder/models/model_registry.hpp"

namespace engcoder::addons {

using json = nlohmann::json;

struct RouteDecision {
    std::optional<std::string> addon_id;
    std::size_t score{};
    std::string detail;
};

struct AddonResult {
    std::string addon_id;
    std::string model;
    std::string text;
};

class AddonRegistry {
public:
    AddonRegistry(const core::Config& config, model::IModelProvider& model, models::ModelRegistry& models);

    [[nodiscard]] std::size_t enabled_count() const;
    [[nodiscard]] bool has_enabled_addons() const;
    [[nodiscard]] RouteDecision route_deterministic(std::string_view input, const json& overrides = json::object()) const;
    [[nodiscard]] std::expected<AddonResult, core::Error> invoke(
        std::string_view addon_id,
        std::string_view prompt,
        std::string_view context = {},
        std::string_view model_override = {},
        std::stop_token stop = {});
    [[nodiscard]] json describe() const;
    [[nodiscard]] std::string model_tool_description() const;
    [[nodiscard]] const core::AddonConfig* find(std::string_view id) const noexcept;
    [[nodiscard]] std::string model_for(std::string_view id) const;
    [[nodiscard]] bool is_enabled(std::string_view id) const;

private:
    const core::Config& config_;
    model::IModelProvider& model_;
    models::ModelRegistry& models_;
};

} // namespace engcoder::addons
