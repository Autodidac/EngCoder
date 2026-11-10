#include "engcoder/controller/architect_controller.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace engcoder::controller {
namespace {

[[nodiscard]] std::string extract_json_text(std::string text) {
    const auto begin = text.find('{');
    const auto end = text.rfind('}');
    if (begin == std::string::npos || end == std::string::npos || end < begin) return {};
    return text.substr(begin, end - begin + 1);
}

[[nodiscard]] bool likely_retrieval_request(std::string_view text) {
    std::string lower{text};
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::string_view markers[] = {
        "find ", "search ", "look up", "documentation", "docs", "repository", "file", "codebase", "readme", "agents.md",
        "remember", "previous", "last time", "before", "we discussed", "we talked", "you know", "again"
    };
    return std::ranges::any_of(markers, [&](std::string_view marker) { return lower.find(marker) != std::string::npos; });
}

} // namespace

ArchitectController::ArchitectController(
    const core::Config& config,
    model::IModelProvider& model,
    addons::AddonRegistry& addons,
    models::ModelRegistry& models,
    residency::ModelResidencyManager& residency)
    : config_(config), model_(model), addons_(addons), models_(models), residency_(residency), shadow_(config.data_root) {}

std::optional<ControllerDecision> ArchitectController::learned_decision(std::string_view request, std::string_view model_override, std::stop_token stop, const json& addon_models, std::string& failure) {
    if (stop.stop_requested()) { failure = "cancelled"; return std::nullopt; }
    const auto controller_model = model_override.empty() ? models_.controller_model() : std::string{model_override};
    if (!config_.controller_enabled || controller_model.empty() || controller_model == "auto") {
        failure = "controller not configured";
        return std::nullopt;
    }

    auto prepared = model_.prepare(controller_model);
    if (!prepared) { failure = "controller model unavailable (prepare failed)"; return std::nullopt; }

    residency_.begin_use("architect");
    struct Guard {
        residency::ModelResidencyManager& residency;
        ~Guard() { residency.end_use("architect"); }
    } guard{residency_};

    std::ostringstream catalog;
    for (const auto& addon : config_.addons) {
        const bool assigned = addon_models.is_object() && addon_models.contains(addon.id) &&
            addon_models.at(addon.id).is_string() && !addon_models.at(addon.id).get<std::string>().empty();
        if ((!addons_.is_enabled(addon.id) && !(addon.enabled && assigned)) || !addon.auto_route) continue;
        catalog << addon.id << ": " << addon.purpose << '\n';
    }

    model::ModelRequest model_request;
    model_request.model = controller_model;
    model_request.temperature = 0.0;
    model_request.max_tokens = 256;
    model_request.tool_choice = "none";
    model_request.parallel_tool_calls = false;
    model_request.stop = stop;
    model_request.messages = {
        {
            "system",
            "You are the Architect controller for a local AI. You do not answer the user. "
            "Select at most one capability addon and whether repository/context retrieval is useful. "
            "Return only JSON with keys addon, retrieve, confidence, reason. addon must be one catalog id or null. "
            "Keep reason under 12 words.\n\nCapability catalog:\n" + catalog.str(),
            {},
            {},
        },
        {"user", std::string{request}, {}, {}},
    };

    auto response = model_.complete(model_request);
    if (!response) { failure = "controller inference failed"; return std::nullopt; }

    try {
        const auto raw = extract_json_text(response->text);
        if (raw.empty()) { failure = "controller returned no JSON decision"; return std::nullopt; }
        const auto parsed = json::parse(raw);
        std::optional<std::string> addon_id;
        if (parsed.contains("addon") && parsed["addon"].is_string()) {
            const auto candidate = parsed["addon"].get<std::string>();
            const auto* addon = addons_.find(candidate);
            if (addon != nullptr && addon->auto_route &&
                (addons_.is_enabled(candidate) || (addon->enabled && addon_models.is_object() &&
                    addon_models.contains(candidate) && addon_models.at(candidate).is_string() &&
                    !addon_models.at(candidate).get<std::string>().empty()))) addon_id = candidate;
        }
        return ControllerDecision{
            .addon_id = std::move(addon_id),
            .confidence = std::clamp(parsed.value("confidence", 0.65), 0.0, 1.0),
            .retrieve_context = parsed.value("retrieve", false),
            .source = "architect-model",
            .reason = parsed.value("reason", std::string{"learned controller decision"}),
        };
    } catch (...) {
        failure = "controller returned invalid JSON decision";
        return std::nullopt;
    }
}

ControllerDecision ArchitectController::fallback_decision(std::string_view request, const json& addon_models) const {
    const auto route = addons_.route_deterministic(request, addon_models);
    const auto confidence = route.addon_id.has_value()
        ? std::min(0.92, 0.55 + static_cast<double>(route.score) * 0.10)
        : 0.85;
    return ControllerDecision{
        .addon_id = route.addon_id,
        .confidence = confidence,
        .retrieve_context = likely_retrieval_request(request),
        .source = "deterministic-fallback",
        .reason = route.detail,
    };
}

ControllerDecision ArchitectController::decide(std::string_view request, std::string_view model_override, std::stop_token stop, const json& addon_models) {
    const auto controller_model = model_override.empty() ? models_.controller_model() : std::string{model_override};
    const bool model_attempted = classify_task(request) != TaskIntent::simple && config_.controller_enabled && !controller_model.empty() && controller_model != "auto";

    // Observe conservative ground-truth heuristics in shadow mode. The learned
    // predictor never selects an unsafe small responder until externally evaluated.
    const auto intent = classify_task(request);
    const auto [shadow_intent, shadow_confidence] = shadow_.predict(request);
    shadow_.observe(request, intent);
    const auto decorate = [&](ControllerDecision& decision) {
        decision.task_intent = intent;
        decision.shadow_intent = std::string{to_string(shadow_intent)};
        decision.shadow_confidence = shadow_confidence;
    };
    std::string fallback_reason;
    if (intent != TaskIntent::simple) if (auto learned = learned_decision(request, model_override, stop, addon_models, fallback_reason); learned.has_value()) {
        decorate(*learned);
        learned->model = controller_model;
        learned->model_attempted = model_attempted;
        if (learned->confidence >= config_.controller_min_confidence) {
            learned->model_used = true;
            return *learned;
        }
        fallback_reason = "controller confidence below minimum threshold";
    }

    auto fallback = fallback_decision(request, addon_models);
    decorate(fallback);
    fallback.model = model_attempted ? controller_model : std::string{};
    fallback.model_attempted = model_attempted;
    fallback.model_used = false;
    fallback.model_fallback_reason = model_attempted ? std::move(fallback_reason) : std::string{};
    return fallback;
}

json ArchitectController::describe() const {
    return json{
        {"id", "architect"},
        {"role", "Architect low-latency controller"},
        {"model", models_.controller_model().empty() ? json(nullptr) : json(models_.controller_model())},
        {"enabled", config_.controller_enabled},
        {"recommended_size", "20M-50M"},
        {"minimum_confidence", config_.controller_min_confidence},
        {"fallback", "deterministic addon + safe task intent routing"},
        {"shadow_training", { {"type", "online-linear-latent-features-v1"},
            {"examples", shadow_.examples_seen()}, {"deployment", "observe-only"},
            {"labels", "policy-distillation; no verified labels yet"} }},
        {"user_facing", false},
    };
}

} // namespace engcoder::controller
