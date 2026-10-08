#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"

namespace engcoder::models {

using json = nlohmann::json;

enum class ComponentKind {
    controller,
    foundation,
    addon,
    auxiliary,
};

enum class ComponentOrigin {
    internal,
    native_runtime,
    external_provider,
};

struct ModelComponent {
    std::string id;
    std::string display_name;
    ComponentKind kind{ComponentKind::addon};
    ComponentOrigin origin{ComponentOrigin::internal};
    std::string generation;
    std::string runtime_type;
    std::string provider_model;
    std::string foundation_family;
    std::string foundation_abi;
    std::string weights_sha256;
    std::filesystem::path manifest_path;
    std::uint64_t parameter_count{};
    std::uint64_t estimated_bytes{};
    bool enabled{true};
    bool pinned{};
    bool trainable{};
    bool active{};
};

class ModelRegistry {
public:
    ModelRegistry(const core::Config& config, std::filesystem::path state_root);

    void refresh();

    [[nodiscard]] std::vector<ModelComponent> components() const;
    [[nodiscard]] std::optional<ModelComponent> find(std::string_view id) const;
    [[nodiscard]] std::optional<ModelComponent> active(ComponentKind kind) const;
    [[nodiscard]] std::string foundation_model() const;
    [[nodiscard]] std::string controller_model() const;
    [[nodiscard]] bool set_foundation_model(std::string_view provider_model);
    [[nodiscard]] std::string selected_foundation_model() const;
    [[nodiscard]] json describe() const;

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    void load_internal_component(
        const std::filesystem::path& component_root,
        ComponentKind kind,
        std::string fallback_id,
        std::string fallback_name,
        bool pinned,
        bool trainable);
    void add_configured_components();

    const core::Config& config_;
    std::filesystem::path root_;
    mutable std::shared_mutex mutex_;
    std::vector<ModelComponent> components_;
    std::string preferred_foundation_model_;
};

[[nodiscard]] std::string_view to_string(ComponentKind kind) noexcept;
[[nodiscard]] std::string_view to_string(ComponentOrigin origin) noexcept;

} // namespace engcoder::models
