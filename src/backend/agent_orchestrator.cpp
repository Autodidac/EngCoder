module;
#include "module_prelude.hpp"
#include <algorithm>
#include <chrono>
#include <future>
#include <ranges>
#include <sstream>
#include <utility>

module threadspire.providers;
import threadspire.core;
import threadspire.mcp;

namespace threadspire::ai::backend {
namespace {

[[nodiscard]] const routing::RouteStage* find_stage(
    const routing::RoutingPlan& plan,
    const std::string_view role) noexcept {
    const auto iterator = std::ranges::find(plan.stages, role, &routing::RouteStage::role);
    return iterator == plan.stages.end() ? nullptr : &*iterator;
}

[[nodiscard]] std::string planner_system() {
    return
        "You are the planning lane in a local multi-model C++23 agent. Produce a compact, actionable plan. "
        "Do not solve the task yet. Identify constraints, files or tools needed, correctness checks, and the shortest safe path.";
}

[[nodiscard]] std::string answer_system(const routing::TaskAnalysis& analysis) {
    std::ostringstream stream;
    stream << "You are the execution lane in EngCoder. Answer the user's task directly. "
           << "Return only the user-facing answer. Never expose private reasoning, chain-of-thought, "
              "analysis, or instructions about how to respond. "
           << "Prefer correctness, low latency, and concise output. The task was classified as "
           << routing::to_string(analysis.kind) << " at " << routing::to_string(analysis.tier)
           << " complexity. ";
    if (analysis.kind == routing::TaskKind::code_generation ||
        analysis.kind == routing::TaskKind::code_debugging ||
        analysis.kind == routing::TaskKind::repository_task) {
        stream << "Act as the primary coding model. Use modern C++23, explicit ownership, composition, "
                  "platform-neutral/platform-specific separation, and complete source changes. ";
    }
    if (analysis.needs_tools) {
        stream << "Use connected tools when they provide evidence. Never invent tool results. ";
    }
    return stream.str();
}

[[nodiscard]] bool verifier_passed(const std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && text.substr(first, 4U) == "PASS";
}

[[nodiscard]] std::string_view fallback_skill(const std::string_view skill) noexcept {
    if (skill.starts_with("cpp23-")) {
        return "cpp23-core";
    }
    if (skill.starts_with("english-") || skill == "explanation" || skill == "planning" ||
        skill == "translation" || skill == "tool-use" || skill == "general") {
        return "english-core";
    }
    return {};
}

[[nodiscard]] bool coding_task(const routing::TaskAnalysis& analysis) noexcept {
    return analysis.kind == routing::TaskKind::code_generation ||
           analysis.kind == routing::TaskKind::code_debugging ||
           analysis.kind == routing::TaskKind::repository_task;
}

[[nodiscard]] double generation_temperature(
    const routing::TaskAnalysis& analysis) noexcept {
    if (coding_task(analysis) || analysis.kind == routing::TaskKind::tool_action) {
        return 0.05;
    }
    if (analysis.kind == routing::TaskKind::conversation) {
        return analysis.tier == routing::ComplexityTier::deep ? 0.38 : 0.48;
    }
    return analysis.tier == routing::ComplexityTier::deep ? 0.12 : 0.20;
}

[[nodiscard]] bool can_execute(
    const models::ModelProfile& model,
    const routing::TaskAnalysis& analysis) {
    if (!model.enabled || model.reliability < 0.20) {
        return false;
    }
    if (analysis.kind == routing::TaskKind::conversation &&
        models::is_internal_model(model) &&
        model.model_name != "threadspire://fast") {
        return false;
    }
    if (analysis.needs_tools && !model.supports_tools) {
        return false;
    }
    if (coding_task(analysis) &&
        !model.coding_agent &&
        !models::has_capability(model, "cpp23")) {
        return false;
    }
    for (const auto& capability : analysis.required_capabilities) {
        if (capability == "english") {
            continue;
        }
        if (capability == "current-information") {
            if (!model.supports_tools) {
                return false;
            }
            continue;
        }
        if (!models::has_capability(model, capability)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<const models::ModelProfile*> fallback_models(
    const models::ModelRegistry& registry,
    const routing::TaskAnalysis& analysis,
    const std::vector<std::string>& attempted) {
    std::vector<const models::ModelProfile*> result;
    for (const auto& model : registry.models()) {
        if (!can_execute(model, analysis) ||
            std::ranges::find(attempted, model.id) != attempted.end()) {
            continue;
        }
        result.push_back(&model);
    }
    std::ranges::sort(result, [](const auto* left, const auto* right) {
        const auto left_score =
            left->quality * left->reliability * 1000.0 +
            left->tokens_per_second * 2.0 -
            left->first_token_ms * 0.05 +
            static_cast<double>(left->priority);
        const auto right_score =
            right->quality * right->reliability * 1000.0 +
            right->tokens_per_second * 2.0 -
            right->first_token_ms * 0.05 +
            static_cast<double>(right->priority);
        return left_score > right_score;
    });
    return result;
}

[[nodiscard]] const learning::ExpertModule* compatible_expert(
    const std::vector<learning::ExpertModule>& experts,
    const std::string_view requested_skill,
    const std::string_view base_model) noexcept {
    const auto find_compatible = [&](const std::string_view skill) -> const learning::ExpertModule* {
        const auto iterator = std::ranges::find(experts, skill, &learning::ExpertModule::skill);
        if (iterator == experts.end() || iterator->base_model != base_model) {
            return nullptr;
        }
        return &*iterator;
    };

    if (const auto* exact = find_compatible(requested_skill); exact != nullptr) {
        return exact;
    }
    const auto fallback = fallback_skill(requested_skill);
    return fallback.empty() || fallback == requested_skill ? nullptr : find_compatible(fallback);
}

[[nodiscard]] std::string tool_calls_json(const std::vector<ToolCall>& calls) {
    core::json::Value::Array result;
    result.reserve(calls.size());
    for (const auto& call : calls) {
        std::string parse_error;
        auto arguments = core::json::try_parse(
            call.arguments_json.empty() ? "{}" : call.arguments_json,
            parse_error);
        if (!arguments.has_value()) {
            arguments = core::json::Value(core::json::Value::Object{});
        }
        result.emplace_back(core::json::Value::Object{
            {"id", call.id},
            {"type", "function"},
            {"function", core::json::Value::Object{
                {"name", call.name},
                {"arguments", core::json::stringify(*arguments)},
            }},
        });
    }
    return core::json::stringify(core::json::Value(std::move(result)));
}

} // namespace

AgentOrchestrator::AgentOrchestrator(
    models::ModelRegistry& models,
    std::vector<learning::ExpertModule> experts,
    const memory::EpisodeStore& memory)
    : models_(models), experts_(std::move(experts)), memory_(memory) {}

AgentResult AgentOrchestrator::execute(
    const std::string_view internal_base_url,
    std::string prompt,
    AgentExecutionOptions options) {
    AgentResult result;
    active_options_ = std::move(options);
    active_tool_events_.clear();
    const auto& analysis_prompt = active_options_.analysis_prompt.empty()
        ? prompt
        : active_options_.analysis_prompt;
    active_analysis_ = analyzer_.analyze(analysis_prompt);
    if (active_options_.force_coding_agent) {
        active_analysis_.kind = routing::TaskKind::code_generation;
        if (active_analysis_.tier == routing::ComplexityTier::instant) {
            active_analysis_.tier = routing::ComplexityTier::fast;
        }
        active_analysis_.risk = std::max(0.22, active_analysis_.risk);
        active_analysis_.estimated_output_tokens = std::max<std::size_t>(
            512U,
            active_analysis_.estimated_output_tokens);
        if (std::ranges::find(active_analysis_.required_capabilities, "cpp23") ==
            active_analysis_.required_capabilities.end()) {
            active_analysis_.required_capabilities.push_back("cpp23");
        }
        active_analysis_.signals.push_back("coding-agent-surface");
    }

    result.plan = router_.plan(active_analysis_, models_);
    if (result.plan.stages.empty()) {
        result.error = result.plan.rationale.empty()
            ? "No enabled model can execute this task"
            : result.plan.rationale;
        return result;
    }

    if (active_analysis_.kind == routing::TaskKind::calculation) {
        const auto calculation = routing::evaluate_calculation(analysis_prompt);
        result.models_used = {"deterministic-calculator"};
        if (!calculation.recognized || !calculation.succeeded) {
            result.error = calculation.error.empty()
                ? "The local calculator could not evaluate that expression."
                : calculation.error;
            return result;
        }
        result.response = calculation.value;
        result.succeeded = true;
        if (active_options_.on_token) {
            static_cast<void>(active_options_.on_token(result.response));
        }
        return result;
    }

    const auto minimum_compute_candidate =
        active_options_.prefer_minimum_compute &&
        (result.plan.mode == routing::RouteMode::cascade ||
         result.plan.mode == routing::RouteMode::hedged) &&
        active_analysis_.tier != routing::ComplexityTier::deep &&
        !active_analysis_.needs_repository &&
        !active_analysis_.needs_tools &&
        active_analysis_.risk < 0.58 &&
        active_analysis_.ambiguity < 0.48;
    if (minimum_compute_candidate) {
        auto stage = result.plan.stages.front();
        stage.role = "answer";
        stage.parallel = false;
        result.plan.mode = routing::RouteMode::direct;
        result.plan.stages = {std::move(stage)};
        result.plan.rationale =
            "Interactive minimum-compute route: stream the fastest capable model directly. "
            "Multi-model planning and review remain enabled for deep, ambiguous, tool-driven, "
            "or repository-scale requests.";
    }

    if (!active_options_.allow_automatic_fallback) {
        auto stage = result.plan.stages.front();
        stage.role = "answer";
        stage.parallel = false;
        result.plan.mode = routing::RouteMode::direct;
        result.plan.stages = {std::move(stage)};
        result.plan.rationale =
            "One selected AI answers once. No other AI is contacted.";
    }

    const auto built_in_memory_enabled = std::ranges::any_of(
        models_.models(),
        [](const models::ModelProfile& model) {
            return model.enabled && models::is_internal_model(model);
        });
    std::optional<memory::RetrievedEpisode> conversation_start;
    if (built_in_memory_enabled &&
        !active_options_.force_coding_agent &&
        !active_analysis_.needs_repository &&
        !active_analysis_.needs_tools) {
        if (const auto verified = memory_.verified_match(prompt, skill_for(active_analysis_));
            verified.has_value()) {
            if (active_analysis_.kind == routing::TaskKind::conversation &&
                verified->relevance < 1.0) {
                conversation_start = verified;
            } else {
                result.plan.mode = routing::RouteMode::direct;
                result.plan.stages = {
                    routing::RouteStage{
                        .role = "answer",
                        .model_id = "verified-memory",
                        .max_output_tokens = 0U,
                        .parallel = false,
                    },
                };
                result.plan.rationale =
                    "An exact independently verified local answer was already available.";
                result.models_used = {"verified-memory"};
                result.response = verified->episode.response;
                result.succeeded = !result.response.empty();
                if (result.succeeded && active_options_.on_token) {
                    static_cast<void>(active_options_.on_token(result.response));
                }
                return result;
            }
        }
    }

    if (conversation_start.has_value()) {
        if (!active_options_.context.empty()) {
            active_options_.context += "\n\n";
        }
        active_options_.context +=
            "Approved local conversation example. Use its intent and tone as guidance, "
            "but answer the current message naturally and do not repeat the wording mechanically.\n"
            "Example user: " + conversation_start->episode.prompt +
            "\nExample answer: " + conversation_start->episode.response;
        result.plan.rationale +=
            " A small approved conversation example is conditioning the chosen AI.";
    } else if (auto recalled = memory_context(prompt, active_analysis_); !recalled.empty()) {
        if (!active_options_.context.empty()) {
            active_options_.context += "\n\n";
        }
        active_options_.context += std::move(recalled);
    }
    const auto augmented = prompt;
    const auto started = std::chrono::steady_clock::now();
    std::size_t generated_tokens = 0;

    const auto consume = [&result, &generated_tokens](
                             const routing::RouteStage& stage,
                             const CompletionResult& completion) {
        result.models_used.push_back(stage.model_id);
        generated_tokens += completion.completion_tokens;
    };

    const auto run_answer_stage = [&](routing::RouteStage stage) {
        if (const auto* model = models_.find(stage.model_id);
            model != nullptr && !models::is_internal_model(*model)) {
            stage.max_output_tokens = std::clamp(
                std::max<std::size_t>(256U, stage.max_output_tokens),
                static_cast<std::size_t>(256U),
                static_cast<std::size_t>(1024U));
        }
        auto completion = run_stage(
            internal_base_url,
            stage,
            answer_system(active_analysis_),
            augmented,
            true);
        consume(stage, completion);
        return completion;
    };
    if (result.plan.mode == routing::RouteMode::direct) {
        const auto initial_stage = result.plan.stages.front();
        auto completion = run_answer_stage(initial_stage);

        std::vector<std::string> attempted{initial_stage.model_id};
        std::vector<std::string> errors;
        auto successful_model_id = initial_stage.model_id;
        if (!completion.succeeded) {
            errors.push_back(initial_stage.model_id + ": " + completion.error);
        }

        if (!completion.succeeded &&
            completion.content.empty() &&
            active_options_.allow_automatic_fallback &&
            !(active_options_.should_stop && active_options_.should_stop())) {
            auto alternatives = fallback_models(models_, active_analysis_, attempted);
            constexpr std::size_t maximum_fallbacks = 3U;
            for (std::size_t index = 0;
                 index < alternatives.size() && index < maximum_fallbacks;
                 ++index) {
                const auto* candidate = alternatives[index];
                routing::RouteStage fallback_stage{
                    .role = "automatic-fallback",
                    .model_id = candidate->id,
                    .max_output_tokens = initial_stage.max_output_tokens,
                    .parallel = false,
                };
                result.plan.stages.push_back(fallback_stage);
                attempted.push_back(candidate->id);
                completion = run_answer_stage(fallback_stage);
                if (completion.succeeded) {
                    successful_model_id = candidate->id;
                    result.plan.rationale +=
                        " The first endpoint failed, so the request automatically moved to " +
                        candidate->display_name + '.';
                    break;
                }
                errors.push_back(candidate->id + ": " + completion.error);
                if (active_options_.should_stop && active_options_.should_stop()) {
                    break;
                }
            }
        }

        if (!completion.succeeded) {
            if (std::ranges::any_of(errors, [](const std::string& error) {
                    return error.find("generation cancelled") != std::string::npos;
                })) {
                result.error = "generation cancelled";
                return result;
            }
            const auto corrupt_draft = std::ranges::any_of(
                errors,
                [](const std::string& error) {
                    return error.find("repetitive or structurally corrupt") != std::string::npos;
                });
            const auto empty_answer = std::ranges::any_of(
                errors,
                [](const std::string& error) {
                    return error.find("no generated content") != std::string::npos;
                });
            result.error = active_options_.allow_automatic_fallback
                ? "No available AI produced a usable answer."
                : "The selected AI could not produce a usable answer. No other AI was used.";
            if (corrupt_draft) {
                result.error += " Its draft was incomplete or corrupted.";
            } else if (empty_answer) {
                result.error += " It returned no answer.";
            }
            return result;
        }
        // Feedback belongs to the model that produced the accepted answer, not to failed
        // endpoints attempted before automatic fallback succeeded.
        result.models_used = {std::move(successful_model_id)};
        generated_tokens = completion.completion_tokens;
        result.response = std::move(completion.content);
    } else if (result.plan.mode == routing::RouteMode::cascade) {
        const auto* draft_stage = find_stage(result.plan, "draft");
        if (draft_stage == nullptr) {
            result.error = "Cascade route has no draft stage";
            return result;
        }
        auto draft = run_stage(
            internal_base_url,
            *draft_stage,
            answer_system(active_analysis_),
            augmented);
        consume(*draft_stage, draft);
        if (!draft.succeeded) {
            const auto* fallback = find_stage(result.plan, "fallback");
            if (fallback == nullptr) {
                result.error = draft.error;
                return result;
            }
            auto replacement = run_stage(
                internal_base_url,
                *fallback,
                answer_system(active_analysis_),
                augmented);
            consume(*fallback, replacement);
            if (!replacement.succeeded) {
                result.error = replacement.error;
                return result;
            }
            result.response = std::move(replacement.content);
        } else {
            const auto* verify_stage = find_stage(result.plan, "verify");
            if (verify_stage == nullptr) {
                result.response = std::move(draft.content);
            } else {
                const auto verification_prompt =
                    "User task:\n" + prompt + "\n\nCandidate answer:\n" + draft.content +
                    "\n\nReturn PASS on the first line only if the answer is correct, complete, and appropriately scoped. "
                    "Otherwise return FAIL followed by the precise defect.";
                auto verification = run_stage(
                    internal_base_url,
                    *verify_stage,
                    "You are a strict low-cost verifier. Do not rewrite correct answers.",
                    verification_prompt);
                consume(*verify_stage, verification);
                if (verification.succeeded && verifier_passed(verification.content)) {
                    result.response = std::move(draft.content);
                } else if (const auto* fallback = find_stage(result.plan, "fallback"); fallback != nullptr) {
                    auto replacement = run_stage(
                        internal_base_url,
                        *fallback,
                        answer_system(active_analysis_) +
                            "A faster draft was rejected by verification. Produce a corrected final answer.",
                        augmented + "\n\nRejected draft:\n" + draft.content +
                            "\n\nVerifier feedback:\n" + verification.content);
                    consume(*fallback, replacement);
                    if (!replacement.succeeded) {
                        result.error = replacement.error;
                        return result;
                    }
                    result.response = std::move(replacement.content);
                } else {
                    result.response = std::move(draft.content);
                }
            }
        }
    } else if (result.plan.mode == routing::RouteMode::hedged) {
        const auto* fast_stage = find_stage(result.plan, "fast-candidate");
        const auto* specialist_stage = find_stage(result.plan, "specialist-candidate");
        if (fast_stage == nullptr || specialist_stage == nullptr) {
            result.error = "Hedged route is incomplete";
            return result;
        }
        auto fast_future = std::async(std::launch::async, [&, fast_stage] {
            return run_stage(
                internal_base_url,
                *fast_stage,
                answer_system(active_analysis_),
                augmented);
        });
        auto specialist_future = std::async(std::launch::async, [&, specialist_stage] {
            return run_stage(
                internal_base_url,
                *specialist_stage,
                answer_system(active_analysis_),
                augmented);
        });
        auto fast_result = fast_future.get();
        auto specialist_result = specialist_future.get();
        consume(*fast_stage, fast_result);
        consume(*specialist_stage, specialist_result);
        if (!fast_result.succeeded && !specialist_result.succeeded) {
            result.error = fast_result.error + "; " + specialist_result.error;
            return result;
        }
        if (!fast_result.succeeded) {
            result.response = std::move(specialist_result.content);
        } else if (!specialist_result.succeeded) {
            result.response = std::move(fast_result.content);
        } else if (const auto* select_stage = find_stage(result.plan, "select"); select_stage != nullptr) {
            auto selection = run_stage(
                internal_base_url,
                *select_stage,
                "You are the final selector. Return a corrected final answer, not commentary about the candidates.",
                "User task:\n" + prompt + "\n\nCandidate A:\n" + fast_result.content +
                    "\n\nCandidate B:\n" + specialist_result.content +
                    "\n\nSelect the stronger candidate and repair any remaining defect.");
            consume(*select_stage, selection);
            result.response = selection.succeeded
                ? std::move(selection.content)
                : std::move(specialist_result.content);
        } else {
            result.response = specialist_result.content.size() >= fast_result.content.size()
                ? std::move(specialist_result.content)
                : std::move(fast_result.content);
        }
    } else {
        std::string plan_text;
        if (const auto* plan_stage = find_stage(result.plan, "plan"); plan_stage != nullptr) {
            auto plan = run_stage(
                internal_base_url,
                *plan_stage,
                planner_system(),
                augmented);
            consume(*plan_stage, plan);
            if (!plan.succeeded) {
                result.error = plan.error;
                return result;
            }
            plan_text = std::move(plan.content);
        }

        const auto* execute_stage = find_stage(result.plan, "execute");
        if (execute_stage == nullptr) {
            result.error = "Agentic route has no execution stage";
            return result;
        }
        auto execution = run_stage(
            internal_base_url,
            *execute_stage,
            answer_system(active_analysis_) +
                "Follow the supplied plan, but correct it if necessary. Return the complete answer.",
            augmented + "\n\nPlanning lane output:\n" + plan_text);
        consume(*execute_stage, execution);
        if (!execution.succeeded) {
            result.error = execution.error;
            return result;
        }

        if (const auto* review_stage = find_stage(result.plan, "review"); review_stage != nullptr) {
            auto review = run_stage(
                internal_base_url,
                *review_stage,
                "You are the final correctness lane. Return the improved final answer only. Preserve valid detail and fix omissions or errors.",
                "User task:\n" + prompt + "\n\nPlan:\n" + plan_text +
                    "\n\nExecution draft:\n" + execution.content);
            consume(*review_stage, review);
            result.response = review.succeeded ? std::move(review.content) : std::move(execution.content);
        } else {
            result.response = std::move(execution.content);
        }
    }

    if (result.plan.mode != routing::RouteMode::direct && active_options_.on_token &&
        !result.response.empty()) {
        static_cast<void>(active_options_.on_token(result.response));
    }
    result.tool_events = active_tool_events_;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (elapsed.count() > 0) {
        result.effective_tokens_per_second =
            static_cast<double>(generated_tokens) / static_cast<double>(elapsed.count()) * 1000.0;
    }
    result.succeeded = !result.response.empty();
    return result;
}

CompletionResult AgentOrchestrator::run_stage(
    const std::string_view internal_base_url,
    const routing::RouteStage& stage,
    std::string system_prompt,
    std::string prompt,
    const bool expose_tokens) {
    const auto* model = models_.find(stage.model_id);
    if (model == nullptr || !model->enabled) {
        return CompletionResult{
            .succeeded = false,
            .content = {},
            .error = "Routed model is unavailable: " + stage.model_id,
            .finish_reason = {},
            .tool_calls = {},
            .prompt_tokens = 0,
            .completion_tokens = 0,
            .tokens_per_second = 0.0,
            .first_token_latency = {},
            .elapsed = {},
        };
    }

    std::filesystem::path adapter;
    std::filesystem::path module;
    const auto skill = learning::normalize_skill(skill_for(active_analysis_));
    if (const auto* expert = compatible_expert(experts_, skill, model->model_name);
        expert != nullptr) {
        if (expert->state == learning::ExpertState::full_rank_active &&
            !expert->module_path.empty()) {
            module = expert->module_path;
        }
        if ((expert->state == learning::ExpertState::lora_active ||
             expert->state == learning::ExpertState::full_rank_active) &&
            !expert->adapter_path.empty()) {
            adapter = expert->adapter_path;
        }
        system_prompt += " A validated local skill module is active for " + expert->skill + '.';
    }

    const auto task_needs_tools =
        active_analysis_.needs_tools ||
        active_analysis_.kind == routing::TaskKind::tool_action ||
        active_analysis_.kind == routing::TaskKind::repository_task;
    const auto use_tools = task_needs_tools &&
                           active_options_.mcp_client != nullptr &&
                           active_options_.mcp_client->connected() &&
                           model->supports_tools;
    const auto tools_json = use_tools
        ? active_options_.mcp_client->openai_tools_json()
        : std::string{};
    if (!active_options_.context.empty()) {
        system_prompt +=
            "\n\nBackground context follows. Use it only when relevant; the current user message "
            "remains authoritative.\n" + active_options_.context;
    }
    std::vector<ChatMessage> messages{
        ChatMessage{
            .role = "system",
            .content = system_prompt,
            .name = {},
            .tool_call_id = {},
            .tool_calls_json = {},
        },
        ChatMessage{
            .role = "user",
            .content = prompt,
            .name = {},
            .tool_call_id = {},
            .tool_calls_json = {},
        },
    };

    const auto maximum_rounds = std::max<std::size_t>(1U, active_options_.maximum_tool_rounds + 1U);
    for (std::size_t round = 0; round < maximum_rounds; ++round) {
        const auto may_call_tools = use_tools && round < active_options_.maximum_tool_rounds;
        auto completion = client_.complete(
            internal_base_url,
            *model,
            CompletionRequest{
                .system_prompt = {},
                .prompt = {},
                .messages = messages,
                .max_output_tokens = stage.max_output_tokens,
                .temperature = generation_temperature(active_analysis_),
                .adapter_path = adapter,
                .module_path = module,
                .tools_json = may_call_tools ? tools_json : std::string{},
                .stream = expose_tokens && !may_call_tools,
                .on_token = expose_tokens ? active_options_.on_token : TokenCallback{},
                .should_stop = active_options_.should_stop,
            });
        models_.record_observation(model->id, completion.tokens_per_second, completion.succeeded);
        if (!completion.succeeded || completion.tool_calls.empty() || !may_call_tools) {
            return completion;
        }

        messages.push_back(ChatMessage{
            .role = "assistant",
            .content = completion.content,
            .name = {},
            .tool_call_id = {},
            .tool_calls_json = tool_calls_json(completion.tool_calls),
        });
        for (const auto& call : completion.tool_calls) {
            if (active_options_.should_stop && active_options_.should_stop()) {
                return CompletionResult{
                    .succeeded = false,
                    .content = {},
                    .error = "generation cancelled",
                    .finish_reason = {},
                    .tool_calls = {},
                    .prompt_tokens = 0,
                    .completion_tokens = 0,
                    .tokens_per_second = 0.0,
                    .first_token_latency = {},
                    .elapsed = {},
                };
            }
            const auto tool_result = active_options_.mcp_client->call_tool(
                call.name,
                call.arguments_json);
            active_tool_events_.push_back(
                call.name + (tool_result.succeeded ? ": success" : ": failed: " + tool_result.error));
            messages.push_back(ChatMessage{
                .role = "tool",
                .content = tool_result.succeeded ? tool_result.content : tool_result.error,
                .name = call.name,
                .tool_call_id = call.id,
                .tool_calls_json = {},
            });
        }
    }
    return CompletionResult{
        .succeeded = false,
        .content = {},
        .error = "MCP tool-call loop exceeded its safety limit",
        .finish_reason = {},
        .tool_calls = {},
        .prompt_tokens = 0,
        .completion_tokens = 0,
        .tokens_per_second = 0.0,
        .first_token_latency = {},
        .elapsed = {},
    };
}

std::string AgentOrchestrator::skill_for(const routing::TaskAnalysis& analysis) const {
    switch (analysis.kind) {
    case routing::TaskKind::code_debugging:
        return "cpp23-debugging";
    case routing::TaskKind::code_generation:
        return "cpp23-generation";
    case routing::TaskKind::repository_task:
        return "cpp23-repository";
    case routing::TaskKind::image_generation:
        return "image-generation";
    case routing::TaskKind::audio_generation:
        return "audio-generation";
    case routing::TaskKind::planning:
        return "planning";
    case routing::TaskKind::translation:
        return "translation";
    case routing::TaskKind::transformation:
        return "english-transformation";
    case routing::TaskKind::tool_action:
        return "tool-use";
    case routing::TaskKind::explanation:
        return "explanation";
    case routing::TaskKind::conversation:
        return "english-conversation";
    case routing::TaskKind::calculation:
        return "calculation";
    }
    return "general";
}

std::string AgentOrchestrator::memory_context(
    const std::string_view current_prompt,
    const routing::TaskAnalysis& analysis) const {
    const auto recalled = memory_.retrieve(current_prompt, 3U);
    if (recalled.empty()) {
        return {};
    }

    std::ostringstream stream;
    stream << "Locally validated relevant memories follow. Use them only when applicable; "
                     "the current request remains authoritative.\n";
    for (const auto& item : recalled) {
        stream << "\n--- memory " << item.episode.id << " [skill=" << item.episode.skill
               << ", score=" << item.episode.score << "] ---\n"
               << "Earlier task: " << item.episode.prompt << "\n"
               << "Accepted result: " << item.episode.response << '\n';
    }
    stream << "\nCurrent task class: " << routing::to_string(analysis.kind) << '.';
    return stream.str();
}

} // namespace threadspire::ai::backend
