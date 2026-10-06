module;
#include "module_prelude.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

module threadspire.models;
import threadspire.core;
import threadspire.platform;

namespace threadspire::ai::models {
namespace {

struct Probe final {
    std::string id;
    std::string display_name;
    std::string endpoint;
    std::string protocol;
    std::string models_path;
    std::string model_key;
};

struct LmStudioCatalogModel final {
    std::string model_name;
    bool runtime_ready{false};
};

struct LmStudioCatalog final {
    bool valid{false};
    std::vector<LmStudioCatalogModel> models;
};

[[nodiscard]] std::string lower_ascii(const std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string sanitize_identifier(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool separator = false;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(std::tolower(character)));
            separator = false;
        } else if (!result.empty() && !separator) {
            result.push_back('-');
            separator = true;
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "local-model";
    }
    return result;
}

[[nodiscard]] std::string join_url(
    const std::string_view base,
    const std::string_view path) {
    std::string result(base);
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    if (!path.empty() && path.front() != '/') {
        result.push_back('/');
    }
    result.append(path);
    return result;
}

[[nodiscard]] int managed_teacher_family_name(
    const std::string_view model_name) {
    const auto lowered = lower_ascii(model_name);
    if (lowered == "qwen/qwen3.8-27b" || lowered == "qwen3.8-27b") {
        return 1;
    }
    if (lowered == "nvidia/nemotron-3-nano-4b" ||
        lowered == "nemotron-3-nano-4b") {
        return 2;
    }
    return 0;
}

[[nodiscard]] std::optional<std::string> loopback_management_base(
    const ModelProfile& model) {
    if (lower_ascii(model.protocol) != "openai") {
        return std::nullopt;
    }
    const auto endpoint = lower_ascii(model.endpoint);
    const auto scheme_end = endpoint.find("://");
    if (scheme_end == std::string::npos) {
        return std::nullopt;
    }
    const auto scheme = endpoint.substr(0U, scheme_end);
    if (scheme != "http" && scheme != "https") {
        return std::nullopt;
    }
    const auto authority_start = scheme_end + 3U;
    const auto authority_end = endpoint.find_first_of("/?#", authority_start);
    const auto authority = endpoint.substr(
        authority_start,
        authority_end == std::string::npos
            ? std::string::npos
            : authority_end - authority_start);
    if (authority.empty() || authority.find('@') != std::string::npos) {
        return std::nullopt;
    }

    std::string host;
    std::string port;
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            return std::nullopt;
        }
        host = authority.substr(0U, close + 1U);
        const auto remainder = authority.substr(close + 1U);
        if (!remainder.empty()) {
            if (!remainder.starts_with(':')) {
                return std::nullopt;
            }
            port = remainder.substr(1U);
        }
    } else {
        const auto colon = authority.find(':');
        host = authority.substr(0U, colon);
        if (colon != std::string::npos) {
            port = authority.substr(colon + 1U);
        }
    }
    if (host != "127.0.0.1" && host != "localhost" && host != "[::1]") {
        return std::nullopt;
    }
    if (authority.ends_with(':') && port.empty()) {
        return std::nullopt;
    }
    unsigned long port_value = 0UL;
    for (const auto character : port) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return std::nullopt;
        }
        port_value = port_value * 10UL +
            static_cast<unsigned long>(character - '0');
        if (port_value > 65535UL) {
            return std::nullopt;
        }
    }
    if (!port.empty() && port_value == 0UL) {
        return std::nullopt;
    }
    return scheme + "://" + authority;
}

[[nodiscard]] std::vector<std::string> extract_string_values(
    const std::string_view json,
    const std::string_view key) {
    std::vector<std::string> values;
    const auto marker = '"' + std::string(key) + '"';
    std::size_t cursor = 0;
    while (cursor < json.size()) {
        auto position = json.find(marker, cursor);
        if (position == std::string_view::npos) {
            break;
        }
        position = json.find(':', position + marker.size());
        if (position == std::string_view::npos) {
            break;
        }
        position = json.find('"', position + 1U);
        if (position == std::string_view::npos) {
            break;
        }
        ++position;
        std::string value;
        bool escaped = false;
        for (; position < json.size(); ++position) {
            const auto character = json[position];
            if (escaped) {
                switch (character) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(character); break;
                }
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                break;
            } else {
                value.push_back(character);
            }
        }
        if (!value.empty()) {
            values.push_back(std::move(value));
        }
        cursor = position < json.size() ? position + 1U : json.size();
    }
    std::ranges::sort(values);
    values.erase(std::ranges::unique(values).begin(), values.end());
    return values;
}

[[nodiscard]] LmStudioCatalog parse_lm_studio_models(
    const std::string_view body) {
    std::string error;
    const auto document = core::json::try_parse(body, error);
    if (!document.has_value()) {
        return {};
    }
    const auto* models = document->find("models");
    const auto* model_array = models == nullptr ? nullptr : models->as_array();
    if (model_array == nullptr) {
        return {};
    }

    LmStudioCatalog result;
    result.valid = true;
    for (const auto& model : *model_array) {
        if (core::json::string_or(model.find("type")) != "llm") {
            continue;
        }
        const auto model_key = core::json::string_or(model.find("key"));
        const auto* instances = model.find("loaded_instances");
        const auto* instance_array = instances == nullptr ? nullptr : instances->as_array();
        if (managed_teacher_family_name(model_key) != 0) {
            result.models.push_back(LmStudioCatalogModel{
                .model_name = model_key,
                .runtime_ready = instance_array != nullptr && !instance_array->empty(),
            });
            continue;
        }
        if (instance_array == nullptr) {
            continue;
        }
        for (const auto& instance : *instance_array) {
            auto identifier = core::json::string_or(instance.find("id"));
            if (identifier.empty()) {
                identifier = model_key;
            }
            if (!identifier.empty()) {
                result.models.push_back(LmStudioCatalogModel{
                    .model_name = std::move(identifier),
                    .runtime_ready = true,
                });
            }
        }
    }
    std::ranges::sort(result.models, {}, &LmStudioCatalogModel::model_name);
    std::vector<LmStudioCatalogModel> merged;
    for (auto& model : result.models) {
        if (!merged.empty() && merged.back().model_name == model.model_name) {
            merged.back().runtime_ready =
                merged.back().runtime_ready || model.runtime_ready;
        } else {
            merged.push_back(std::move(model));
        }
    }
    result.models = std::move(merged);
    return result;
}

[[nodiscard]] std::optional<bool> catalog_model_ready(
    const LmStudioCatalog& catalog,
    const std::string_view model_name) {
    if (!catalog.valid) {
        return std::nullopt;
    }
    const auto target = lower_ascii(model_name);
    const auto found = std::ranges::find_if(
        catalog.models,
        [&target](const LmStudioCatalogModel& candidate) {
            return lower_ascii(candidate.model_name) == target;
        });
    return found == catalog.models.end()
        ? std::optional<bool>{}
        : std::optional<bool>{found->runtime_ready};
}

[[nodiscard]] std::string response_failure(
    const LocalModelEndpointResponse& response) {
    if (!response.error.empty()) {
        return response.error;
    }
    return "HTTP " + std::to_string(response.status_code);
}

[[nodiscard]] bool coding_model_name(const std::string_view model_name) {
    const auto lower = lower_ascii(model_name);
    constexpr std::array markers{
        std::string_view{"code"}, std::string_view{"coder"}, std::string_view{"devstral"},
        std::string_view{"starcoder"}, std::string_view{"codestral"}, std::string_view{"qwen3-coder"},
        std::string_view{"qwen2.5-coder"}, std::string_view{"deepseek-coder"},
        std::string_view{"granite-code"}, std::string_view{"phi-4"},
        std::string_view{"qwen"}, std::string_view{"deepseek"},
    };
    return std::ranges::any_of(markers, [&lower](const std::string_view marker) {
        return lower.find(marker) != std::string::npos;
    });
}

[[nodiscard]] double inferred_quality(const std::string_view name, const bool coder) {
    const auto lower = lower_ascii(name);
    double quality = coder ? 0.82 : 0.74;
    if (lower.find("0.5b") != std::string::npos || lower.find("1b") != std::string::npos) {
        quality -= 0.12;
    } else if (lower.find("1.5b") != std::string::npos || lower.find("3b") != std::string::npos) {
        quality -= 0.05;
    } else if (lower.find("7b") != std::string::npos || lower.find("8b") != std::string::npos) {
        quality += 0.04;
    } else if (lower.find("14b") != std::string::npos || lower.find("20b") != std::string::npos ||
               lower.find("32b") != std::string::npos) {
        quality += 0.08;
    }
    return std::clamp(quality, 0.50, 0.94);
}

[[nodiscard]] ModelProfile make_profile(
    const Probe& probe,
    std::string model_name,
    const bool runtime_ready = true) {
    const auto coder = coding_model_name(model_name);
    auto capabilities = std::vector<std::string>{
        "english", "chat", "explanation", "transformation", "translation", "planning", "review"};
    if (coder) {
        capabilities.insert(capabilities.end(), {"cpp23", "debugging", "repository", "tools"});
    }
    return ModelProfile{
        .id = sanitize_identifier(probe.id + '-' + model_name),
        .display_name = probe.display_name + " — " + model_name,
        .model_name = std::move(model_name),
        .endpoint = probe.endpoint,
        .api_key = {},
        .protocol = probe.protocol,
        .capabilities = std::move(capabilities),
        .tokens_per_second = coder ? 36.0 : 42.0,
        .first_token_ms = 250.0,
        .quality = inferred_quality(model_name, coder),
        .reliability = 0.90,
        .context_window = coder ? 32768U : 16384U,
        .enabled = true,
        .keep_hot = true,
        .supports_tools = coder,
        .supports_lora = false,
        .teacher = true,
        .verifier = false,
        .custom_model = false,
        .coding_agent = coder,
        .auto_discovered = true,
        .runtime_ready = runtime_ready,
        .priority = coder ? 125 : 115,
    };
}

} // namespace

bool is_managed_lm_studio_teacher(const ModelProfile& model) {
    return managed_teacher_family_name(model.model_name) != 0 &&
           loopback_management_base(model).has_value();
}

LocalModelActivationResult ensure_managed_lm_studio_teacher_ready(
    const ModelProfile& model,
    const std::chrono::milliseconds timeout,
    const std::stop_token stop_token,
    LocalModelEndpointRequest request) {
    const auto management_base = loopback_management_base(model);
    if (managed_teacher_family_name(model.model_name) == 0 ||
        !management_base.has_value()) {
        return LocalModelActivationResult{
            .message = "Only the exact loopback Qwen and Nemotron teachers may be loaded automatically.",
        };
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }
    if (!request) {
        request = [api_key = model.api_key](
                      const std::string_view method,
                      const std::string_view url,
                      const std::string_view body,
                      const std::chrono::milliseconds request_timeout) {
            std::vector<backend::HttpHeader> headers;
            if (method == "POST") {
                headers.push_back(backend::HttpHeader{
                    .name = "Content-Type",
                    .value = "application/json; charset=utf-8",
                });
            }
            if (!api_key.empty()) {
                headers.push_back(backend::HttpHeader{
                    .name = "Authorization",
                    .value = "Bearer " + api_key,
                });
            }
            const auto response = backend::http_request(backend::HttpRequest{
                .method = std::string(method),
                .url = std::string(url),
                .body = std::string(body),
                .headers = std::move(headers),
                .timeout = request_timeout,
            });
            return LocalModelEndpointResponse{
                .status_code = response.status_code,
                .body = response.body,
                .error = response.error,
            };
        };
    }

    // Foundation generation fans out several variants. Serialize only this
    // lightweight readiness/load handshake so those workers cannot race the
    // same LM Studio model lifecycle endpoint.
    static std::timed_mutex activation_mutex;
    std::unique_lock activation_lock(activation_mutex, std::defer_lock);
    constexpr auto activation_poll = std::chrono::milliseconds(50);
    const auto activation_deadline = std::chrono::steady_clock::now() +
        std::max(timeout, std::chrono::milliseconds::zero());
    while (!activation_lock.try_lock_for(activation_poll)) {
        if (stop_token.stop_requested()) {
            return LocalModelActivationResult{.message = "Teacher activation cancelled."};
        }
        if (std::chrono::steady_clock::now() >= activation_deadline) {
            return LocalModelActivationResult{
                .message = "Timed out waiting for another teacher lifecycle check to finish.",
            };
        }
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }
    const auto catalog_timeout = std::min(timeout, std::chrono::milliseconds(5000));
    const auto catalog_url = join_url(*management_base, "/api/v1/models");
    auto catalog_response = request("GET", catalog_url, {}, catalog_timeout);
    if (!catalog_response.succeeded()) {
        return LocalModelActivationResult{
            .message = "LM Studio catalog check failed: " +
                response_failure(catalog_response),
        };
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }
    auto catalog = parse_lm_studio_models(catalog_response.body);
    if (!catalog.valid) {
        return LocalModelActivationResult{
            .message = "LM Studio returned a malformed model catalog.",
        };
    }
    auto ready = catalog_model_ready(catalog, model.model_name);
    if (!ready.has_value()) {
        return LocalModelActivationResult{
            .message = "The authorized teacher is not installed in LM Studio: " +
                model.model_name,
        };
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }
    if (*ready) {
        return LocalModelActivationResult{
            .ready = true,
            .message = "The authorized LM Studio teacher is already loaded.",
        };
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }

    const auto family = managed_teacher_family_name(model.model_name);
    // These bounds apply only when EngCoder creates a new instance. Existing
    // instances return above unchanged, so a model in use by another task is
    // never reloaded or reconfigured. Qwen uses the deeper curriculum window;
    // Nemotron uses the smaller routine window. Request concurrency remains
    // bounded separately by the foundation runner (Qwen 1, Nemotron 4), since
    // LM Studio's v1 load API does not document a parallel-load setting.
    const auto context_length = family == 1 ? 32768 : 8192;
    const auto payload = core::json::stringify(core::json::Value(
        core::json::Value::Object{
            {"context_length", context_length},
            {"echo_load_config", true},
            {"model", model.model_name},
        }));
    const auto load_response = request(
        "POST",
        join_url(*management_base, "/api/v1/models/load"),
        payload,
        timeout);
    // The platform HTTP request is synchronous once dispatched. Observe
    // cancellation immediately after it returns; never try to compensate by
    // unloading a model that another local task may already be using.
    if (!load_response.succeeded()) {
        return LocalModelActivationResult{
            .message = "LM Studio could not load the authorized teacher: " +
                response_failure(load_response),
        };
    }
    if (stop_token.stop_requested()) {
        return LocalModelActivationResult{.message = "Teacher activation cancelled."};
    }

    catalog_response = request("GET", catalog_url, {}, catalog_timeout);
    if (!catalog_response.succeeded()) {
        return LocalModelActivationResult{
            .message = "LM Studio loaded the teacher but its catalog could not be confirmed: " +
                response_failure(catalog_response),
        };
    }
    catalog = parse_lm_studio_models(catalog_response.body);
    ready = catalog_model_ready(catalog, model.model_name);
    if (!catalog.valid || !ready.value_or(false)) {
        return LocalModelActivationResult{
            .message = "LM Studio did not report the authorized teacher as loaded.",
        };
    }
    return LocalModelActivationResult{
        .ready = true,
        .loaded_by_engcoder = true,
        .message = "The authorized LM Studio teacher was loaded on demand.",
    };
}

ProviderDiscoveryReport discover_local_models(
    const std::chrono::milliseconds timeout_per_endpoint,
    const std::stop_token stop_token) {
    ProviderDiscoveryReport report;
    const std::array probes{
        Probe{"ollama", "Ollama", "http://127.0.0.1:11434", "ollama", "/api/tags", "name"},
        Probe{"lmstudio", "LM Studio", "http://127.0.0.1:1234", "openai", "/api/v1/models", "id"},
        Probe{"llamacpp", "llama.cpp / LocalAI", "http://127.0.0.1:8080", "openai", "/v1/models", "id"},
        Probe{"jan", "Jan", "http://127.0.0.1:1337", "openai", "/v1/models", "id"},
        Probe{"koboldcpp", "KoboldCpp", "http://127.0.0.1:5001", "openai", "/v1/models", "id"},
        Probe{"textgen", "Text generation web UI", "http://127.0.0.1:5000", "openai", "/v1/models", "id"},
    };

    std::set<std::string> seen;
    for (const auto& probe : probes) {
        if (stop_token.stop_requested()) {
            report.diagnostics.push_back("Local AI discovery cancelled");
            break;
        }
        auto response = backend::http_get(
            join_url(probe.endpoint, probe.models_path), timeout_per_endpoint);
        if (stop_token.stop_requested()) {
            report.diagnostics.push_back("Local AI discovery cancelled");
            break;
        }
        auto used_lm_studio_native_api = false;
        std::vector<LmStudioCatalogModel> catalog_models;
        if (probe.id == "lmstudio" && response.succeeded()) {
            used_lm_studio_native_api = true;
            auto catalog = parse_lm_studio_models(response.body);
            if (catalog.valid) {
                catalog_models = std::move(catalog.models);
            }
        } else if (probe.id == "lmstudio") {
            response = backend::http_get(
                join_url(probe.endpoint, "/v1/models"),
                timeout_per_endpoint);
            if (stop_token.stop_requested()) {
                report.diagnostics.push_back("Local AI discovery cancelled");
                break;
            }
        }
        if (!response.succeeded()) {
            continue;
        }
        if (!used_lm_studio_native_api) {
            for (auto& model_name : extract_string_values(response.body, probe.model_key)) {
                catalog_models.push_back(LmStudioCatalogModel{
                    .model_name = std::move(model_name),
                    .runtime_ready = true,
                });
            }
        }
        if (catalog_models.empty()) {
            report.diagnostics.push_back(
                used_lm_studio_native_api
                    ? probe.display_name + " connected: no loaded LLM or authorized teacher is available"
                    : probe.display_name + " answered but exposed no text-generation model IDs");
            continue;
        }
        std::size_t added = 0;
        std::size_t ready_count = 0;
        std::size_t loadable_teacher_count = 0;
        for (auto& catalog_model : catalog_models) {
            auto& model_name = catalog_model.model_name;
            const auto lower = lower_ascii(model_name);
            if (lower.find("embed") != std::string::npos || lower.find("rerank") != std::string::npos) {
                continue;
            }
            auto profile = make_profile(
                probe,
                std::move(model_name),
                catalog_model.runtime_ready);
            const auto identity = profile.protocol + '|' + profile.endpoint + '|' + profile.model_name;
            if (seen.insert(identity).second) {
                ready_count += profile.runtime_ready ? 1U : 0U;
                loadable_teacher_count +=
                    !profile.runtime_ready && is_managed_lm_studio_teacher(profile) ? 1U : 0U;
                report.models.push_back(std::move(profile));
                ++added;
            }
        }
        if (used_lm_studio_native_api) {
            report.diagnostics.push_back(
                probe.display_name + " connected: " + std::to_string(ready_count) +
                " loaded LLM(s), " + std::to_string(loadable_teacher_count) +
                " authorized teacher(s) available to load");
        } else {
            report.diagnostics.push_back(
                probe.display_name + " connected: " + std::to_string(added) +
                " usable model(s)");
        }
    }
    return report;
}

bool merge_discovered_models(
    ModelRegistry& registry,
    const std::vector<ModelProfile>& discovered) {
    bool changed = false;
    std::set<std::string> live_identifiers;
    for (const auto& candidate : discovered) {
        live_identifiers.insert(candidate.id);
        auto* existing = registry.find(candidate.id);
        if (existing == nullptr) {
            registry.models().push_back(candidate);
            changed = true;
            continue;
        }

        const auto api_key = existing->api_key;
        const auto tokens_per_second = existing->tokens_per_second;
        const auto first_token_ms = existing->first_token_ms;
        const auto quality = existing->quality;
        const auto reliability = existing->reliability;
        const auto enabled = existing->enabled;
        const auto keep_hot = existing->keep_hot;
        const auto supports_tools = existing->supports_tools;
        const auto supports_lora = existing->supports_lora;
        const auto teacher = existing->teacher;
        const auto verifier = existing->verifier;
        const auto custom_model = existing->custom_model;
        const auto coding_agent = existing->coding_agent;
        const auto priority = existing->priority;
        const auto runtime_ready = existing->runtime_ready;
        const auto validated_generation = existing->validated_generation;

        const auto changed_endpoint = existing->endpoint != candidate.endpoint ||
                                      existing->model_name != candidate.model_name ||
                                      existing->protocol != candidate.protocol;
        *existing = candidate;
        existing->api_key = api_key;
        existing->tokens_per_second = tokens_per_second > 0.0
            ? tokens_per_second
            : candidate.tokens_per_second;
        existing->first_token_ms = first_token_ms > 0.0
            ? first_token_ms
            : candidate.first_token_ms;
        existing->quality = quality > 0.0 ? quality : candidate.quality;
        existing->reliability = reliability > 0.0 ? reliability : candidate.reliability;
        existing->enabled = enabled;
        existing->keep_hot = keep_hot;
        existing->supports_tools = supports_tools;
        existing->supports_lora = supports_lora;
        existing->teacher = teacher;
        existing->verifier = verifier;
        existing->custom_model = custom_model;
        existing->coding_agent = coding_agent;
        existing->validated_generation = validated_generation;
        existing->priority = priority;
        changed = changed || changed_endpoint ||
            runtime_ready != candidate.runtime_ready;
    }

    for (auto& model : registry.models()) {
        if (!model.auto_discovered || live_identifiers.contains(model.id) ||
            !is_managed_lm_studio_teacher(model)) {
            continue;
        }
        changed = changed || model.runtime_ready;
        model.runtime_ready = false;
        live_identifiers.insert(model.id);
    }
    const auto old_size = registry.models().size();
    std::erase_if(registry.models(), [&live_identifiers](const ModelProfile& model) {
        return model.auto_discovered && !live_identifiers.contains(model.id);
    });
    return changed || registry.models().size() != old_size;
}

ModelProfile make_manual_local_model(
    std::string endpoint,
    std::string model_name,
    std::string protocol,
    const bool coding_agent,
    const bool teacher) {
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    const auto id = sanitize_identifier("manual-" + endpoint + '-' + model_name);
    auto capabilities = std::vector<std::string>{
        "english", "chat", "explanation", "transformation", "translation", "planning", "review"};
    if (coding_agent) {
        capabilities.insert(capabilities.end(), {"cpp23", "debugging", "repository", "tools"});
    }
    return ModelProfile{
        .id = id,
        .display_name = "Local endpoint — " + model_name,
        .model_name = std::move(model_name),
        .endpoint = std::move(endpoint),
        .api_key = {},
        .protocol = std::move(protocol),
        .capabilities = std::move(capabilities),
        .tokens_per_second = coding_agent ? 34.0 : 40.0,
        .first_token_ms = 250.0,
        .quality = coding_agent ? 0.82 : 0.74,
        .reliability = 0.90,
        .context_window = coding_agent ? 32768U : 16384U,
        .enabled = true,
        .keep_hot = true,
        .supports_tools = coding_agent,
        .supports_lora = false,
        .teacher = teacher,
        .verifier = false,
        .custom_model = false,
        .coding_agent = coding_agent,
        .auto_discovered = false,
        .priority = coding_agent ? 130 : 120,
    };
}

bool is_internal_model(const ModelProfile& model) noexcept {
    return model.protocol == "internal" || model.endpoint == "internal";
}

} // namespace threadspire::ai::models
