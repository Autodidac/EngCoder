#include "engcoder/models/model_registry.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>

namespace engcoder::models {
namespace {

[[nodiscard]] json read_json(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return json::object();
    try {
        return json::parse(input);
    } catch (...) {
        return json::object();
    }
}

[[nodiscard]] std::uint64_t estimate_bytes(const json& manifest) {
    if (manifest.contains("estimated_bytes") && manifest["estimated_bytes"].is_number_unsigned()) {
        return manifest["estimated_bytes"].get<std::uint64_t>();
    }
    const auto parameters = manifest.value("parameter_count", std::uint64_t{});
    const auto precision = manifest.value("precision", std::string{"bf16"});
    std::uint64_t bytes_per_parameter = 2;
    if (precision == "fp32") bytes_per_parameter = 4;
    else if (precision == "int8") bytes_per_parameter = 1;
    else if (precision == "int4") bytes_per_parameter = 1; // conservative byte accounting
    return parameters * bytes_per_parameter;
}

[[nodiscard]] std::string active_generation(const std::filesystem::path& root) {
    const auto active = read_json(root / "active.json");
    return active.value("generation", active.value("active_generation", std::string{}));
}


[[nodiscard]] bool is_managed_model(std::string_view model) noexcept {
    return model.starts_with("engcoder://") || model.starts_with("threadspire://") ||
        model.starts_with("internal://") || model.starts_with("qwenflash://");
}
[[nodiscard]] bool inference_ready(const json& manifest) {
    if (!manifest.is_object()) return false;
    if (manifest.contains("inference_ready") && manifest["inference_ready"].is_boolean()) {
        return manifest["inference_ready"].get<bool>();
    }

    // Mirrors the internal trainer's promotion contract for existing V2 manifests.
    // Future manifests should publish `inference_ready` explicitly so the runtime
    // does not need to understand training-policy details.
    return manifest.value("status", std::string{}) == "trained" &&
        manifest.value("bootstrap_complete", false) &&
        manifest.value("full_backbone_trained", false) &&
        manifest.value("generation_ready", false);
}

} // namespace

std::string_view to_string(ComponentKind kind) noexcept {
    switch (kind) {
        case ComponentKind::controller: return "controller";
        case ComponentKind::foundation: return "foundation";
        case ComponentKind::addon: return "addon";
        case ComponentKind::auxiliary: return "auxiliary";
    }
    return "addon";
}

std::string_view to_string(ComponentOrigin origin) noexcept {
    switch (origin) {
        case ComponentOrigin::internal: return "internal";
        case ComponentOrigin::native_runtime: return "native-runtime";
        case ComponentOrigin::external_provider: return "external-provider";
    }
    return "internal";
}

ModelRegistry::ModelRegistry(const core::Config& config, std::filesystem::path state_root)
    : config_(config), root_(std::move(state_root) / "models") {
    refresh();
}

void ModelRegistry::refresh() {
    std::unique_lock lock(mutex_);
    components_.clear();
    std::filesystem::create_directories(root_ / "controller" / "generations");
    std::filesystem::create_directories(root_ / "foundation" / "generations");
    std::filesystem::create_directories(root_ / "addons");
    std::filesystem::create_directories(root_ / "auxiliary");

    preferred_foundation_model_.clear();
    const auto selection = read_json(root_ / "selection.json");
    if (selection.is_object()) {
        preferred_foundation_model_ = selection.value("foundation_model", std::string{});
    }

    load_internal_component(root_ / "controller", ComponentKind::controller, "architect", "Architect", true, true);
    load_internal_component(root_ / "foundation", ComponentKind::foundation, "keymaker", "KeyMaker Foundation", true, true);

    // EngCoder internal generations are the canonical local foundation. V2
    // publishes detachable skill sidecars; V1 generations remain readable and
    // are projected into the same logical component registry.
    const auto internal_root = root_ / "internal";
    const auto internal_generation = active_generation(internal_root);
    if (!internal_generation.empty()) {
        const auto manifest_path = internal_root / "generations" / internal_generation / "manifest.json";
        const auto manifest = read_json(manifest_path);
        if (manifest.is_object()) {
            const auto format = manifest.value("format", std::string{"threadspire.internal.v1"});
            const auto component_abi = manifest.value("component_abi", format);
            const bool ready = inference_ready(manifest);
            const auto foundation = manifest.value("foundation_component", json::object());
            const auto foundation_parameters = foundation.value(
                "parameter_count", manifest.value("parameter_count", std::uint64_t{}));
            const auto foundation_bytes = foundation.value(
                "estimated_bytes_bf16", foundation_parameters * std::uint64_t{2});

            components_.push_back(ModelComponent{
                .id = "keymaker",
                .display_name = "KeyMaker Foundation",
                .kind = ComponentKind::foundation,
                .origin = ComponentOrigin::internal,
                .generation = internal_generation,
                .runtime_type = "internal-keymaker",
                .provider_model = "engcoder://keymaker",
                .foundation_family = "engcoder.foundation",
                .foundation_abi = component_abi,
                .weights_sha256 = manifest.value("weights_sha256", std::string{}),
                .manifest_path = manifest_path,
                .parameter_count = foundation_parameters,
                .estimated_bytes = foundation_bytes,
                .enabled = ready,
                .pinned = true,
                .trainable = true,
                .active = ready,
            });

            const auto addon_json = manifest.value("addon_components", json::object());
            auto skill_parameters = [&](std::string_view skill) -> std::uint64_t {
                if (addon_json.is_object() && addon_json.contains(std::string{skill})) {
                    return addon_json.at(std::string{skill}).value("parameter_count", std::uint64_t{});
                }
                return 0;
            };
            auto add_internal = [&](
                std::string id, std::string name, ComponentKind kind, std::string provider_model,
                std::uint64_t parameters, bool pinned) {
                const auto duplicate = std::ranges::find_if(components_, [&](const auto& item) { return item.id == id; });
                if (duplicate != components_.end()) return;
                components_.push_back(ModelComponent{
                    .id = std::move(id),
                    .display_name = std::move(name),
                    .kind = kind,
                    .origin = ComponentOrigin::internal,
                    .generation = internal_generation,
                    .runtime_type = "internal-keymaker",
                    .provider_model = std::move(provider_model),
                    .foundation_family = "engcoder.foundation",
                    .foundation_abi = component_abi,
                    .weights_sha256 = manifest.value("weights_sha256", std::string{}),
                    .manifest_path = manifest_path,
                    .parameter_count = parameters,
                    .estimated_bytes = parameters * std::uint64_t{2},
                    .enabled = ready,
                    .pinned = pinned,
                    .trainable = true,
                    .active = ready,
                });
            };

            const auto skills = manifest.value("skills", json::array());
            const auto has_skill = [&](std::string_view skill) {
                return std::ranges::any_of(skills, [&](const auto& value) {
                    return value.is_string() && value.template get<std::string>() == skill;
                });
            };

            if (has_skill("architect-controller")) {
                add_internal("architect", "Architect", ComponentKind::controller,
                    "engcoder://architect", skill_parameters("architect-controller"), true);
            }
            if (has_skill("cpp23-core") || has_skill("cpp23-debugging")) {
                add_internal("cpp", "C++23 Addon", ComponentKind::addon, "engcoder://cpp",
                    skill_parameters("cpp23-core") + skill_parameters("cpp23-debugging"), false);
            }
            if (has_skill("documentation")) {
                add_internal("docs", "Documentation Addon", ComponentKind::addon, "engcoder://docs",
                    skill_parameters("documentation"), false);
            }
            if (has_skill("planning")) {
                add_internal("planner", "Planning Addon", ComponentKind::addon, "engcoder://planning",
                    skill_parameters("planning"), false);
            }
            if (has_skill("tool-use")) {
                add_internal("tools", "Tool-Use Addon", ComponentKind::addon, "engcoder://tools",
                    skill_parameters("tool-use"), false);
            }
            if (has_skill("reranking")) {
                add_internal("reranker", "Context Reranker", ComponentKind::auxiliary, "engcoder://reranker",
                    skill_parameters("reranking"), false);
            }

            // V1 compatibility: these generations predate the V2 controller and
            // additional addon lanes. Preserve the trained KeyMaker/C++/planning
            // capabilities without inventing components that are not present.
            if (format == "threadspire.internal.v1") {
                if (has_skill("cpp23-core") || has_skill("cpp23-debugging")) {
                    if (const auto it = std::ranges::find_if(components_, [](const auto& item) { return item.id == "cpp"; }); it != components_.end()) {
                        it->provider_model = "engcoder://cpp";
                    }
                }
            }
        }
    }

    const auto addons_root = root_ / "addons";
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(addons_root, error)) {
        if (!entry.is_directory()) continue;
        const auto id = entry.path().filename().string();
        load_internal_component(entry.path(), ComponentKind::addon, id, id, false, true);
    }

    const auto runtimes_root = root_ / "runtimes";
    std::filesystem::create_directories(runtimes_root, error);
    for (const auto& entry : std::filesystem::directory_iterator(runtimes_root, error)) {
        if (error) break;
        if (!entry.is_directory()) continue;
        const auto manifest_path = entry.path() / "manifest.json";
        const auto manifest = read_json(manifest_path);
        if (!manifest.is_object() || !manifest.value("format", std::string{}).starts_with("engcoder.runtime-model.v")) continue;
        const auto runtime = manifest.value("runtime", std::string{});
        const auto id = manifest.value("id", entry.path().filename().string());
        const auto provider_model = manifest.value("provider_model", runtime == "qwen-flash" ? "qwenflash://" + id : id);
        components_.push_back(ModelComponent{
            .id = id,
            .display_name = manifest.value("display_name", id),
            .kind = ComponentKind::foundation,
            .origin = ComponentOrigin::native_runtime,
            .generation = {},
            .runtime_type = runtime,
            .provider_model = provider_model,
            .foundation_family = runtime == "qwen-flash" ? "qwen.sparse-moe" : "native.runtime",
            .foundation_abi = manifest.value("format", std::string{}),
            .weights_sha256 = {},
            .manifest_path = manifest_path,
            .parameter_count = 0,
            .estimated_bytes = manifest.value("estimated_bytes", std::uint64_t{}),
            .enabled = manifest.value("enabled", true),
            .pinned = false,
            .trainable = false,
            .active = false,
        });
    }

    add_configured_components();
}

void ModelRegistry::load_internal_component(
    const std::filesystem::path& component_root,
    ComponentKind kind,
    std::string fallback_id,
    std::string fallback_name,
    bool pinned,
    bool trainable) {
    const auto generation = active_generation(component_root);
    if (generation.empty()) return;

    const auto manifest_path = component_root / "generations" / generation / "manifest.json";
    const auto manifest = read_json(manifest_path);
    if (!manifest.is_object()) return;

    const auto format = manifest.value("format", std::string{});
    if (!format.empty() && format != "engcoder.model-component.v2" &&
        format != "engcoder.internal.v2" && format != "threadspire.internal.v1") {
        return;
    }

    ModelComponent component;
    component.id = manifest.value("id", std::move(fallback_id));
    component.display_name = manifest.value("display_name", manifest.value("name", std::move(fallback_name)));
    component.kind = kind;
    component.origin = ComponentOrigin::internal;
    component.generation = generation;
    component.runtime_type = "internal-keymaker";
    component.provider_model = manifest.value("provider_model", std::string{});
    component.foundation_family = manifest.value("foundation_family", std::string{});
    component.foundation_abi = manifest.value("foundation_abi", std::string{});
    component.weights_sha256 = manifest.value("weights_sha256", std::string{});
    component.manifest_path = manifest_path;
    component.parameter_count = manifest.value("parameter_count", std::uint64_t{});
    component.estimated_bytes = estimate_bytes(manifest);
    const bool has_training_readiness =
        manifest.contains("inference_ready") || manifest.contains("generation_ready") ||
        manifest.contains("bootstrap_complete") || manifest.contains("full_backbone_trained");
    const bool ready = has_training_readiness ? inference_ready(manifest) : manifest.value("active", true);
    component.enabled = manifest.value("enabled", true) && ready;
    component.pinned = pinned;
    component.trainable = trainable;
    component.active = ready;
    components_.push_back(std::move(component));
}

void ModelRegistry::add_configured_components() {
    auto add_external = [&](std::string id, std::string name, ComponentKind kind, std::string model, bool pinned) {
        if (model.empty() || model == "auto" || is_managed_model(model)) return;
        const auto duplicate = std::ranges::find_if(components_, [&](const auto& item) { return item.id == id; });
        if (duplicate != components_.end()) return;
        components_.push_back(ModelComponent{
            .id = std::move(id),
            .display_name = std::move(name),
            .kind = kind,
            .origin = ComponentOrigin::external_provider,
            .generation = {},
            .runtime_type = "external-openai",
            .provider_model = std::move(model),
            .foundation_family = {},
            .foundation_abi = {},
            .weights_sha256 = {},
            .manifest_path = {},
            .parameter_count = 0,
            .estimated_bytes = 0,
            .enabled = true,
            .pinned = pinned,
            .trainable = false,
            .active = true,
        });
    };

    add_external("architect", "Architect", ComponentKind::controller, config_.controller_model, true);
    add_external("keymaker", "KeyMaker Foundation", ComponentKind::foundation, config_.model, true);
    for (const auto& addon : config_.addons) {
        add_external(addon.id, addon.display_name, ComponentKind::addon, addon.model, false);
    }
}

std::vector<ModelComponent> ModelRegistry::components() const {
    std::shared_lock lock(mutex_);
    return components_;
}

std::optional<ModelComponent> ModelRegistry::find(std::string_view id) const {
    std::shared_lock lock(mutex_);
    const auto it = std::ranges::find_if(components_, [&](const auto& item) { return item.id == id; });
    if (it == components_.end()) return std::nullopt;
    return *it;
}

std::optional<ModelComponent> ModelRegistry::active(ComponentKind kind) const {
    std::shared_lock lock(mutex_);
    const auto it = std::ranges::find_if(components_, [&](const auto& item) {
        return item.kind == kind && item.active && item.enabled;
    });
    if (it == components_.end()) return std::nullopt;
    return *it;
}

std::string ModelRegistry::foundation_model() const {
    std::shared_lock lock(mutex_);

    if (!preferred_foundation_model_.empty()) {
        const bool managed = is_managed_model(preferred_foundation_model_);
        if (!managed) return preferred_foundation_model_;

        const auto selected = std::ranges::find_if(components_, [&](const ModelComponent& item) {
            return item.kind == ComponentKind::foundation && item.enabled &&
                item.provider_model == preferred_foundation_model_;
        });
        if (selected != components_.end()) return selected->provider_model;
    }

    const auto item = std::ranges::find_if(components_, [](const ModelComponent& component) {
        return component.kind == ComponentKind::foundation && component.active &&
            component.enabled && !component.provider_model.empty();
    });
    if (item != components_.end()) {
        return item->provider_model;
    }
    // A configured managed URI must correspond to a real enabled component.
    // Falling through with an unavailable engcoder:// URI makes the provider
    // repeatedly try to start a model that does not exist yet.
    return is_managed_model(config_.model) ? std::string{"auto"} : config_.model;
}

bool ModelRegistry::set_foundation_model(std::string_view provider_model) {
    if (provider_model.empty()) return false;

    std::unique_lock lock(mutex_);
    const std::string requested{provider_model};
    const bool managed = is_managed_model(requested);
    if (managed) {
        const auto item = std::ranges::find_if(components_, [&](const ModelComponent& component) {
            return component.kind == ComponentKind::foundation && component.enabled &&
                component.provider_model == requested;
        });
        if (item == components_.end()) return false;
    }

    const auto path = root_ / "selection.json";
    const auto temporary = root_ / "selection.json.tmp";
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec) return false;

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << json{{"format", "engcoder.model-selection.v1"}, {"foundation_model", requested}}.dump(2) << '\n';
        if (!output) return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    preferred_foundation_model_ = requested;
    return true;
}

std::string ModelRegistry::selected_foundation_model() const {
    std::shared_lock lock(mutex_);
    return preferred_foundation_model_;
}

std::string ModelRegistry::controller_model() const {
    if (const auto item = active(ComponentKind::controller); item.has_value() && !item->provider_model.empty()) {
        return item->provider_model;
    }

    // Managed controller URIs describe EngCoder-owned weights. If that
    // component is not active yet, do not pretend it is an external model and
    // pay a failed model-start attempt on every request. The deterministic
    // Architect fallback remains available at zero model-start cost.
    return is_managed_model(config_.controller_model) ? std::string{} : config_.controller_model;
}

json ModelRegistry::describe() const {
    json items = json::array();
    for (const auto& component : components()) {
        items.push_back({
            {"id", component.id},
            {"display_name", component.display_name},
            {"kind", to_string(component.kind)},
            {"origin", to_string(component.origin)},
            {"generation", component.generation.empty() ? json(nullptr) : json(component.generation)},
            {"runtime_type", component.runtime_type.empty() ? json(nullptr) : json(component.runtime_type)},
            {"provider_model", component.provider_model.empty() ? json(nullptr) : json(component.provider_model)},
            {"foundation_family", component.foundation_family.empty() ? json(nullptr) : json(component.foundation_family)},
            {"foundation_abi", component.foundation_abi.empty() ? json(nullptr) : json(component.foundation_abi)},
            {"parameter_count", component.parameter_count},
            {"estimated_bytes", component.estimated_bytes},
            {"enabled", component.enabled},
            {"pinned", component.pinned},
            {"trainable", component.trainable},
            {"active", component.active},
        });
    }
    return json{
        {"format", "engcoder.model-registry.v3"},
        {"root", root_.string()},
        {"selected_foundation_model", selected_foundation_model().empty()
            ? json(nullptr) : json(selected_foundation_model())},
        {"components", std::move(items)},
    };
}

} // namespace engcoder::models
