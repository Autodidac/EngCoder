#include "threadspire/ai/routing/router.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <sstream>

namespace threadspire::ai::routing {
namespace {

[[nodiscard]] double expected_latency(
    const models::ModelProfile& model,
    const std::size_t output_tokens) noexcept {
    const auto generation_ms =
        static_cast<double>(output_tokens) / std::max(1.0, model.tokens_per_second) * 1000.0;
    const auto cold_penalty = model.keep_hot ? 0.0 : 350.0;
    return model.first_token_ms + generation_ms + cold_penalty;
}

[[nodiscard]] double required_quality(const TaskAnalysis& analysis) noexcept {
    switch (analysis.tier) {
    case ComplexityTier::instant:
        return 0.48;
    case ComplexityTier::fast:
        return 0.58;
    case ComplexityTier::standard:
        return 0.70;
    case ComplexityTier::deep:
        return 0.80;
    }
    return 0.60;
}

[[nodiscard]] bool is_coding_task(const TaskAnalysis& analysis) noexcept {
    return analysis.kind == TaskKind::code_generation ||
           analysis.kind == TaskKind::code_debugging ||
           analysis.kind == TaskKind::repository_task;
}

[[nodiscard]] bool satisfies_required_capabilities(
    const models::ModelProfile& model,
    const TaskAnalysis& analysis) {
    if (analysis.needs_tools && !model.supports_tools) {
        return false;
    }
    return std::ranges::all_of(
        analysis.required_capabilities,
        [&model](const std::string& capability) {
            if (capability == "current-information") {
                return model.supports_tools;
            }
            return capability == "english" || models::has_capability(model, capability);
        });
}

[[nodiscard]] const models::ModelProfile* strongest_matching(
    const models::ModelRegistry& registry,
    const auto& predicate) noexcept {
    const models::ModelProfile* best = nullptr;
    auto best_score = -std::numeric_limits<double>::infinity();
    for (const auto& model : registry.models()) {
        if (!model.enabled || !predicate(model)) {
            continue;
        }
        const auto score = model.quality * 0.62 + model.reliability * 0.28 +
                           std::min(1.0, model.tokens_per_second / 100.0) * 0.05 +
                           static_cast<double>(model.priority) / 2000.0;
        if (score > best_score) {
            best = &model;
            best_score = score;
        }
    }
    return best;
}

[[nodiscard]] std::size_t stage_tokens(
    const TaskAnalysis& analysis,
    const double multiplier) noexcept {
    return std::max<std::size_t>(64U, static_cast<std::size_t>(
        std::ceil(static_cast<double>(analysis.estimated_output_tokens) * multiplier)));
}

} // namespace

std::string_view to_string(const RouteMode mode) noexcept {
    switch (mode) {
    case RouteMode::direct:
        return "direct";
    case RouteMode::cascade:
        return "cascade";
    case RouteMode::hedged:
        return "hedged";
    case RouteMode::agentic:
        return "agentic";
    }
    return "direct";
}

const models::ModelProfile* AdaptiveRouter::fastest_capable(
    const TaskAnalysis& analysis,
    const models::ModelRegistry& registry,
    const double minimum_quality,
    const std::string_view preferred_capability) const {
    const models::ModelProfile* best = nullptr;
    auto best_latency = std::numeric_limits<double>::max();

    for (const auto& model : registry.models()) {
        if (!model.enabled || model.quality < minimum_quality || model.reliability < 0.55) {
            continue;
        }
        if (!preferred_capability.empty() && !models::has_capability(model, preferred_capability)) {
            continue;
        }
        if (is_coding_task(analysis) && !model.coding_agent &&
            !models::has_capability(model, "cpp23")) {
            continue;
        }
        if (!satisfies_required_capabilities(model, analysis)) {
            continue;
        }
        const auto latency = expected_latency(model, analysis.estimated_output_tokens) -
                             static_cast<double>(model.priority) * 0.25;
        if (latency < best_latency) {
            best = &model;
            best_latency = latency;
        }
    }

    if (best != nullptr) {
        return best;
    }

    for (const auto& model : registry.models()) {
        if (!model.enabled || !satisfies_required_capabilities(model, analysis)) {
            continue;
        }
        if (is_coding_task(analysis) && !model.coding_agent &&
            !models::has_capability(model, "cpp23")) {
            continue;
        }
        if (best == nullptr ||
            model.quality * model.reliability > best->quality * best->reliability) {
            best = &model;
        }
    }
    return best;
}

RoutingPlan AdaptiveRouter::plan(
    const TaskAnalysis& analysis,
    const models::ModelRegistry& registry) const {
    RoutingPlan result;
    result.analysis = analysis;

    const auto minimum_quality = required_quality(analysis);
    const auto preferred = is_coding_task(analysis) ? std::string_view{"cpp23"} : std::string_view{};
    const auto* fast = fastest_capable(analysis, registry, minimum_quality, preferred);
    const auto* coder = strongest_matching(registry, [](const models::ModelProfile& model) {
        return model.coding_agent || models::has_capability(model, "cpp23") ||
               models::has_capability(model, "repository") ||
               models::has_capability(model, "debugging");
    });
    const auto* thinker = strongest_matching(registry, [](const models::ModelProfile& model) {
        return models::has_capability(model, "planning");
    });
    const auto* reviewer = strongest_matching(registry, [](const models::ModelProfile& model) {
        return model.verifier || models::has_capability(model, "review");
    });

    if (fast == nullptr) {
        result.rationale = "No enabled model satisfies this task's capabilities.";
        return result;
    }

    const auto add_stage = [&result](
                               std::string role,
                               const models::ModelProfile& model,
                               const std::size_t max_tokens,
                               const bool parallel = false) {
        result.stages.push_back(RouteStage{
            .role = std::move(role),
            .model_id = model.id,
            .max_output_tokens = max_tokens,
            .parallel = parallel,
        });
        result.expected_latency_ms += expected_latency(model, max_tokens);
    };

    if (analysis.tier == ComplexityTier::instant ||
        (analysis.tier == ComplexityTier::fast && analysis.risk < 0.30 && analysis.ambiguity < 0.25)) {
        result.mode = RouteMode::direct;
        add_stage("answer", *fast, analysis.estimated_output_tokens);
        result.rationale =
            "Low-risk bounded task: use the fastest measured capable model and skip unnecessary planning.";
        return result;
    }

    if (analysis.tier == ComplexityTier::fast ||
        (analysis.tier == ComplexityTier::standard && analysis.risk < 0.45 && analysis.ambiguity < 0.35)) {
        result.mode = RouteMode::cascade;
        add_stage("draft", *fast, analysis.estimated_output_tokens);
        if (reviewer != nullptr && reviewer->id != fast->id) {
            add_stage("verify", *reviewer, stage_tokens(analysis, 0.25));
        }
        if (coder != nullptr && coder->id != fast->id) {
            add_stage("fallback", *coder, analysis.estimated_output_tokens);
        }
        result.rationale =
            "Fast-first cascade: keep the cheap answer when verification passes and use the specialist only on failure.";
        return result;
    }

    if (analysis.tier == ComplexityTier::standard && analysis.ambiguity >= 0.35 &&
        coder != nullptr && coder->id != fast->id) {
        result.mode = RouteMode::hedged;
        add_stage("fast-candidate", *fast, analysis.estimated_output_tokens, true);
        add_stage("specialist-candidate", *coder, analysis.estimated_output_tokens, true);
        if (reviewer != nullptr) {
            add_stage("select", *reviewer, stage_tokens(analysis, 0.30));
        }
        result.expected_latency_ms *= 0.58;
        result.rationale =
            "Ambiguous medium task: run fast and specialist candidates concurrently, then select only when useful.";
        return result;
    }

    result.mode = RouteMode::agentic;
    if (thinker != nullptr) {
        add_stage("plan", *thinker, stage_tokens(analysis, 0.30));
    }
    if (coder != nullptr) {
        add_stage("execute", *coder, analysis.estimated_output_tokens);
    } else {
        add_stage("execute", *fast, analysis.estimated_output_tokens);
    }
    if (reviewer != nullptr &&
        (coder == nullptr || reviewer->id != coder->id)) {
        add_stage("review", *reviewer, stage_tokens(analysis, 0.35));
    }
    result.rationale =
        "Deep or high-risk task: separate planning, coding execution, tool use, and verification while keeping each lane minimal.";
    return result;
}

} // namespace threadspire::ai::routing
