module;
#include "module_prelude.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <set>
#include <sstream>
#include <string_view>

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

[[nodiscard]] std::vector<std::string> parse_lm_studio_loaded_models(
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

    std::vector<std::string> result;
    for (const auto& model : *model_array) {
        if (core::json::string_or(model.find("type")) != "llm") {
            continue;
        }
        const auto model_key = core::json::string_or(model.find("key"));
        const auto* instances = model.find("loaded_instances");
        const auto* instance_array = instances == nullptr ? nullptr : instances->as_array();
        if (instance_array == nullptr) {
            continue;
        }
        for (const auto& instance : *instance_array) {
            auto identifier = core::json::string_or(instance.find("id"));
            if (identifier.empty()) {
                identifier = model_key;
            }
            if (!identifier.empty()) {
                result.push_back(std::move(identifier));
            }
        }
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
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
    std::string model_name) {
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
        .priority = coder ? 125 : 115,
    };
}

} // namespace

ProviderDiscoveryReport discover_local_models(const std::chrono::milliseconds timeout_per_endpoint) {
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
        auto response = backend::http_get(
            join_url(probe.endpoint, probe.models_path), timeout_per_endpoint);
        auto used_lm_studio_native_api = false;
        std::vector<std::string> model_names;
        if (probe.id == "lmstudio" && response.succeeded()) {
            used_lm_studio_native_api = true;
            model_names = parse_lm_studio_loaded_models(response.body);
        } else if (probe.id == "lmstudio") {
            response = backend::http_get(
                join_url(probe.endpoint, "/v1/models"),
                timeout_per_endpoint);
        }
        if (!response.succeeded()) {
            continue;
        }
        if (!used_lm_studio_native_api) {
            model_names = extract_string_values(response.body, probe.model_key);
        }
        if (model_names.empty()) {
            report.diagnostics.push_back(
                used_lm_studio_native_api
                    ? probe.display_name + " connected: no loaded LLM is ready"
                    : probe.display_name + " answered but exposed no text-generation model IDs");
            continue;
        }
        std::size_t added = 0;
        for (auto& model_name : model_names) {
            const auto lower = lower_ascii(model_name);
            if (lower.find("embed") != std::string::npos || lower.find("rerank") != std::string::npos) {
                continue;
            }
            auto profile = make_profile(probe, std::move(model_name));
            const auto identity = profile.protocol + '|' + profile.endpoint + '|' + profile.model_name;
            if (seen.insert(identity).second) {
                report.models.push_back(std::move(profile));
                ++added;
            }
        }
        report.diagnostics.push_back(
            probe.display_name + " connected: " + std::to_string(added) +
            (used_lm_studio_native_api ? " loaded LLM(s)" : " usable model(s)"));
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
        existing->priority = priority;
        changed = changed || changed_endpoint;
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
