#include "engcoder/model/multiplex_provider.hpp"

#include <chrono>

namespace engcoder::model {
namespace {
[[nodiscard]] bool is_internal_model(std::string_view model) {
    return model.starts_with("engcoder://") || model.starts_with("threadspire://") ||
           model.starts_with("internal://");
}

[[nodiscard]] std::string canonical_internal_model(std::string model) {
    if (model.starts_with("threadspire://")) model.replace(0, 14, "engcoder://");
    else if (model.starts_with("internal://")) model.replace(0, 11, "engcoder://");
    return model;
}

[[nodiscard]] core::Error native_transport_error(core::Error error) {
    constexpr std::string_view from = "LM Studio";
    constexpr std::string_view to = "EngCoder Qwen Flash runtime";
    std::size_t position = 0;
    while ((position = error.message.find(from, position)) != std::string::npos) {
        error.message.replace(position, from.size(), to);
        position += to.size();
    }
    return error;
}
} // namespace

MultiplexProvider::MultiplexProvider(
    core::Config config,
    InternalModelHost& internal_host,
    NativeModelManager& native_models)
    : config_(std::move(config)),
      internal_host_(internal_host),
      native_models_(native_models),
      external_(config_) {}

bool MultiplexProvider::internal_request(std::string_view model) const noexcept {
    return is_internal_model(model);
}

bool MultiplexProvider::native_request(std::string_view model) const noexcept {
    return is_native_runtime_model(model);
}

std::string MultiplexProvider::resolve_request_model(std::string_view requested) const {
    if (!requested.empty() && requested != "auto") {
        return std::string{requested};
    }

    if (const auto native = native_models_.active_model_uri(); native.has_value()) {
        return *native;
    }

    return requested.empty() ? std::string{"auto"} : std::string{requested};
}

std::string MultiplexProvider::resolve_model_hint(std::string_view requested) const {
    return resolve_request_model(requested);
}

std::expected<LmStudioProvider*, core::Error> MultiplexProvider::internal_provider() {
    auto endpoint = internal_host_.ensure_started();
    if (!endpoint) return std::unexpected(endpoint.error());
    if (!internal_ || internal_url_ != *endpoint) {
        auto internal_config = config_;
        internal_config.lm_studio_url = *endpoint;
        internal_config.model = "engcoder://keymaker";
        internal_config.model_api = core::ModelApi::chat_completions;
        internal_config.lm_studio_token.clear();
        internal_ = std::make_unique<LmStudioProvider>(std::move(internal_config));
        internal_url_ = *endpoint;
    }
    return internal_.get();
}

std::expected<LmStudioProvider*, core::Error> MultiplexProvider::native_provider(
    std::string_view model,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stop) {
    auto endpoint = native_models_.prepare(model, deadline, stop);
    if (!endpoint) return std::unexpected(endpoint.error());
    if (!native_transport_ || native_url_ != *endpoint || native_model_ != model) {
        auto native_config = config_;
        native_config.lm_studio_url = *endpoint;
        native_config.model = "auto";
        native_config.model_api = core::ModelApi::chat_completions;
        native_config.model_auto_load = false; // EngCoder owns native runtime lifecycle.
        native_config.lm_studio_token.clear();
        native_transport_ = std::make_unique<LmStudioProvider>(std::move(native_config));
        native_url_ = *endpoint;
        native_model_ = std::string{model};
    }
    return native_transport_.get();
}

std::expected<ModelResponse, core::Error> MultiplexProvider::complete(const ModelRequest& request) {
    auto translated = request;
    if (translated.deadline == std::chrono::steady_clock::time_point::max()) {
        translated.deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{config_.inference_timeout_seconds};
    }
    const auto resolved_model = resolve_request_model(translated.model);
    translated.model = resolved_model;

    if (native_request(resolved_model)) {
        // Native non-streaming OpenAI/MCP clients must still expose live Qwen
        // fragments to EngCoder's monitor. Only the wire response is buffered.
        return complete_stream(request, {});
    }

    if (!internal_request(resolved_model)) {
        return external_.complete(translated);
    }

    auto prepared = prepare(resolved_model);
    if (!prepared) return std::unexpected(prepared.error());
    auto provider = internal_provider();
    if (!provider) return std::unexpected(provider.error());
    translated.model = canonical_internal_model(resolved_model);
    return (*provider)->complete(translated);
}

std::expected<ModelResponse, core::Error> MultiplexProvider::complete_stream(
    const ModelRequest& request,
    const StreamCallback& callback) {
    auto translated = request;
    if (translated.deadline == std::chrono::steady_clock::time_point::max()) {
        translated.deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{config_.inference_timeout_seconds};
    }
    const auto resolved_model = resolve_request_model(translated.model);
    translated.model = resolved_model;

    if (native_request(resolved_model)) {
        auto provider = native_provider(resolved_model, translated.deadline, translated.stop);
        if (!provider) return std::unexpected(provider.error());
        const auto trace_id = qwen_output_.begin(resolved_model, request.request_id);
        translated.model = "auto";
        const auto forward = [&](const StreamEvent& event) {
            switch (event.kind) {
                case StreamEventKind::text_delta:
                    qwen_output_.append(trace_id, resolved_model, "text", event.delta);
                    break;
                case StreamEventKind::reasoning_delta:
                    qwen_output_.append(trace_id, resolved_model, "reasoning", event.delta);
                    break;
                case StreamEventKind::tool_argument_delta:
                    qwen_output_.append(trace_id, resolved_model, "arguments", event.delta);
                    break;
                case StreamEventKind::tool_call_started: break;
            }
            if (callback) callback(event);
        };
        auto result = (*provider)->complete_stream(translated, forward);
        qwen_output_.finish(trace_id, resolved_model);
        if (!result) return std::unexpected(native_transport_error(result.error()));
        return result;
    }

    if (!internal_request(resolved_model)) {
        return external_.complete_stream(translated, callback);
    }

    auto prepared = prepare(resolved_model);
    if (!prepared) return std::unexpected(prepared.error());
    auto provider = internal_provider();
    if (!provider) return std::unexpected(provider.error());
    translated.model = canonical_internal_model(resolved_model);
    return (*provider)->complete_stream(translated, callback);
}

ProviderCapabilities MultiplexProvider::capabilities() const noexcept {
    auto caps = external_.capabilities();
    caps.native_model_management = true;
    return caps;
}

std::expected<json, core::Error> MultiplexProvider::prepare(std::string_view model) {
    const auto resolved_model = resolve_request_model(model);

    if (is_native_runtime_model(resolved_model)) {
        auto profile = native_models_.find(resolved_model);
        if (!profile) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Unknown native model: " + resolved_model,
            });
        }
        auto endpoint = native_models_.prepare(resolved_model);
        if (!endpoint) return std::unexpected(endpoint.error());
        auto profile_json = to_json(*profile);
        const auto runtime_state = native_models_.qwen_flash().describe();
        const auto active_context = runtime_state.value("active_context_tokens", profile->context_tokens);
        const auto active_slots = std::max(1U, runtime_state.value("active_parallel_slots", profile->parallel_slots));
        const auto active_effective_context = active_context / active_slots;
        profile_json["active_context_tokens"] = active_context;
        profile_json["active_parallel_slots"] = active_slots;
        profile_json["safety_fallback"] = runtime_state.value("safety_fallback", false);
        profile_json["startup_note"] = runtime_state.value("startup_note", nlohmann::json(nullptr));
        return json{
            {"model", resolved_model},
            {"management", "engcoder-native-runtime"},
            {"runtime", "qwen-flash"},
            {"role", "system1-foundation"},
            {"endpoint", *endpoint},
            {"profile", profile_json},
            {"context_tokens", active_context},
            {"parallel_slots", active_slots},
            {"effective_context_tokens", active_effective_context},
            {"auto_compact", profile->auto_compact},
            {"context_reserve_tokens", profile->context_reserve_tokens},
            {"compact_trigger_ratio", profile->compact_trigger_ratio},
            {"compact_target_ratio", profile->compact_target_ratio},
            {"harness", "architect+keymaker+addons+tools+memory+verification"},
        };
    }
    if (!is_internal_model(resolved_model)) {
        const std::string requested{model};
        std::string resolved = resolved_model;
        if (resolved.empty() || resolved == "auto") {
            auto discovered = external_.discover();
            if (!discovered) return std::unexpected(discovered.error());
            resolved = discovered->value("selected_model", std::string{});
            if (resolved.empty() || resolved == "auto") {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::model_unavailable),
                    "No external model could be assigned to the request."
                });
            }
        }
        return json{
            {"model", resolved},
            {"requested_model", requested.empty() ? std::string{"auto"} : requested},
            {"management", "external-provider"}
        };
    }
    return internal_host_.prepare_model(canonical_internal_model(resolved_model));
}

std::expected<json, core::Error> MultiplexProvider::residency() {
    auto internal = internal_host_.residency_status(false);
    json out{
        {"native", native_models_.describe()},
    };
    if (internal) out["internal"] = *internal;
    else out["internal_error"] = internal.error().message;
    return out;
}

std::expected<json, core::Error> MultiplexProvider::release_idle() {
    auto internal = internal_host_.release_idle_addons();
    if (!internal) return std::unexpected(internal.error());
    return json{{"internal", *internal}, {"native", native_models_.describe()}};
}

std::expected<json, core::Error> MultiplexProvider::discover() {
    json out{
        {"provider", "engcoder-multiplex"},
        {"internal", internal_host_.describe()},
        {"native", native_models_.describe()},
        {"models", json::array()},
    };

    for (const auto& profile : native_models_.models()) {
        auto cache = native_models_.qwen_flash().cache_status(profile);
        out["models"].push_back(json{
            {"id", runtime_model_uri(profile)},
            {"display_name", profile.display_name},
            {"runtime", to_string(profile.runtime)},
            {"native", true},
            {"installed", cache.value("installed", false)},
            {"downloadable", profile.source == ModelSourceKind::hugging_face},
        });
    }

    auto external = external_.discover();
    if (external) {
        out["external"] = *external;
        for (const auto& item : external->value("models", json::array())) out["models"].push_back(item);
        if (const auto it = external->find("selected_model"); it != external->end() && it->is_string()) {
            out["selected_model"] = it->get<std::string>();
        } else {
            out["selected_model"] = config_.model.empty() ? std::string{"auto"} : config_.model;
        }
    } else {
        out["external_error"] = external.error().message;
        out["selected_model"] = config_.model;
    }

    if (internal_host_.describe().value("available", false)) {
        const json internal_models = json::array({
            json{{"id", "engcoder://keymaker"}, {"internal", true}, {"lane", "keymaker"}, {"role", "foundation"}},
            json{{"id", "engcoder://architect"}, {"internal", true}, {"lane", "architect"}, {"role", "controller"}},
            json{{"id", "engcoder://cpp"}, {"internal", true}, {"lane", "cpp23"}, {"role", "addon"}},
            json{{"id", "engcoder://docs"}, {"internal", true}, {"lane", "documentation"}, {"role", "addon"}},
            json{{"id", "engcoder://planning"}, {"internal", true}, {"lane", "planning"}, {"role", "addon"}},
            json{{"id", "engcoder://tools"}, {"internal", true}, {"lane", "tools"}, {"role", "addon"}},
            json{{"id", "engcoder://reranker"}, {"internal", true}, {"lane", "reranker"}, {"role", "auxiliary"}},
        });
        for (const auto& item : internal_models) out["models"].push_back(item);
        if (config_.model.empty() || config_.model == "auto") out["selected_model"] = "engcoder://keymaker";
    }
    return out;
}

} // namespace engcoder::model
