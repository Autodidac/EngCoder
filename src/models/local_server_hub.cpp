module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

module threadspire.models;
import threadspire.core;
import threadspire.platform;

namespace threadspire::ai::models {
namespace {

[[nodiscard]] std::vector<std::string> split_csv(const std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(',', begin);
        auto item = std::string(value.substr(
            begin,
            end == std::string_view::npos ? value.size() - begin : end - begin));
        const auto first = item.find_first_not_of(" \t\r\n");
        const auto last = item.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            item = item.substr(first, last - first + 1U);
            if (!item.empty()) {
                result.push_back(std::move(item));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] std::string join_csv(const std::vector<LocalServerProfile>& servers) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < servers.size(); ++index) {
        if (index != 0U) {
            stream << ',';
        }
        stream << servers[index].id;
    }
    return stream.str();
}

[[nodiscard]] std::vector<std::string> parse_openai_models(const std::string_view body) {
    std::string error;
    const auto document = core::json::try_parse(body, error);
    if (!document.has_value()) {
        return {};
    }
    const auto* data = document->find("data");
    const auto* array = data == nullptr ? nullptr : data->as_array();
    if (array == nullptr) {
        return {};
    }
    std::vector<std::string> result;
    for (const auto& item : *array) {
        const auto id = core::json::string_or(item.find("id"));
        if (!id.empty()) {
            result.push_back(id);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string> parse_ollama_models(const std::string_view body) {
    std::string error;
    const auto document = core::json::try_parse(body, error);
    if (!document.has_value()) {
        return {};
    }
    const auto* models = document->find("models");
    const auto* array = models == nullptr ? nullptr : models->as_array();
    if (array == nullptr) {
        return {};
    }
    std::vector<std::string> result;
    for (const auto& item : *array) {
        auto name = core::json::string_or(item.find("name"));
        if (name.empty()) {
            name = core::json::string_or(item.find("model"));
        }
        if (!name.empty()) {
            result.push_back(std::move(name));
        }
    }
    return result;
}

[[nodiscard]] std::string sanitize_id(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(std::tolower(character)));
        } else if (!result.empty() && result.back() != '-') {
            result.push_back('-');
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "model" : result;
}

[[nodiscard]] std::vector<std::string> inferred_capabilities(const std::string_view model_id) {
    auto lower = std::string(model_id);
    std::ranges::transform(lower, lower.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::vector<std::string> result{"english", "chat", "explanation"};
    if (lower.find("code") != std::string::npos ||
        lower.find("coder") != std::string::npos ||
        lower.find("qwen") != std::string::npos ||
        lower.find("deepseek") != std::string::npos) {
        result.insert(result.end(), {"cpp23", "debugging", "repository"});
    }
    if (lower.find("reason") != std::string::npos ||
        lower.find("think") != std::string::npos ||
        lower.find("r1") != std::string::npos ||
        lower.find("gpt-oss") != std::string::npos) {
        result.push_back("planning");
    }
    return result;
}

} // namespace

std::string normalize_local_base_url(const std::string_view url) {
    auto result = std::string(url);
    const auto first = result.find_first_not_of(" \t\r\n");
    const auto last = result.find_last_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    result = result.substr(first, last - first + 1U);
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    if (!result.starts_with("http://") && !result.starts_with("https://")) {
        result = "http://" + result;
    }
    return result;
}

std::string openai_models_url(const std::string_view base_url) {
    auto result = normalize_local_base_url(base_url);
    return result.ends_with("/v1") ? result + "/models" : result + "/v1/models";
}

std::string openai_chat_url(const std::string_view base_url) {
    auto result = normalize_local_base_url(base_url);
    return result.ends_with("/v1") ? result + "/chat/completions" : result + "/v1/chat/completions";
}

LocalServerHub LocalServerHub::load(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        auto result = defaults();
        result.save(path);
        return result;
    }
    const auto ini = core::IniFile::load(path);
    const auto identifiers = split_csv(ini.get_string("servers", "ids", ""));
    if (identifiers.empty()) {
        return defaults();
    }
    LocalServerHub result;
    for (const auto& id : identifiers) {
        const auto section = "server." + id;
        LocalServerProfile server;
        server.id = id;
        server.display_name = ini.get_string(section, "display_name", id);
        server.base_url = normalize_local_base_url(ini.get_string(section, "base_url", ""));
        server.api_key = ini.get_string(section, "api_key", "");
        server.enabled = ini.get_bool(section, "enabled", true);
        server.teacher = ini.get_bool(section, "teacher", true);
        server.verifier = ini.get_bool(section, "verifier", false);
        server.coding_agent = ini.get_bool(section, "coding_agent", true);
        if (!server.base_url.empty()) {
            result.servers_.push_back(std::move(server));
        }
    }
    return result.servers_.empty() ? defaults() : result;
}

LocalServerHub LocalServerHub::defaults() {
    LocalServerHub result;
    result.servers_ = {
        LocalServerProfile{
            .id = "lm-studio",
            .display_name = "LM Studio",
            .base_url = "http://127.0.0.1:1234",
            .api_key = {},
            .enabled = true,
            .teacher = true,
            .verifier = true,
            .coding_agent = true,
        },
        LocalServerProfile{
            .id = "ollama",
            .display_name = "Ollama",
            .base_url = "http://127.0.0.1:11434",
            .api_key = {},
            .enabled = true,
            .teacher = true,
            .verifier = false,
            .coding_agent = true,
        },
        LocalServerProfile{
            .id = "llama-cpp",
            .display_name = "llama.cpp server",
            .base_url = "http://127.0.0.1:8080",
            .api_key = {},
            .enabled = true,
            .teacher = true,
            .verifier = false,
            .coding_agent = true,
        },
        LocalServerProfile{
            .id = "openai-local",
            .display_name = "Generic local OpenAI server",
            .base_url = "http://127.0.0.1:8000",
            .api_key = {},
            .enabled = true,
            .teacher = true,
            .verifier = false,
            .coding_agent = true,
        },
    };
    return result;
}

void LocalServerHub::save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to write local server registry: " + path.string());
    }
    stream << "[servers]\nids=" << join_csv(servers_) << "\n\n";
    for (const auto& server : servers_) {
        stream << "[server." << server.id << "]\n"
               << "display_name=" << server.display_name << '\n'
               << "base_url=" << server.base_url << '\n'
               << "api_key=" << server.api_key << '\n'
               << "enabled=" << (server.enabled ? "true" : "false") << '\n'
               << "teacher=" << (server.teacher ? "true" : "false") << '\n'
               << "verifier=" << (server.verifier ? "true" : "false") << '\n'
               << "coding_agent=" << (server.coding_agent ? "true" : "false") << "\n\n";
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("unable to flush local server registry");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("unable to publish local server registry: " + error.message());
    }
}

const std::vector<LocalServerProfile>& LocalServerHub::servers() const noexcept { return servers_; }
std::vector<LocalServerProfile>& LocalServerHub::servers() noexcept { return servers_; }

LocalDiscoveryReport LocalServerHub::discover(const std::function<bool()>& should_stop) const {
    LocalDiscoveryReport report;
    for (const auto& server : servers_) {
        if (!server.enabled || (should_stop && should_stop())) {
            continue;
        }
        LocalServerStatus status;
        status.server = server;
        auto response = backend::http_request(backend::HttpRequest{
            .method = "GET",
            .url = openai_models_url(server.base_url),
            .body = {},
            .headers = server.api_key.empty()
                ? std::vector<backend::HttpHeader>{}
                : std::vector<backend::HttpHeader>{{
                    .name = "Authorization",
                    .value = "Bearer " + server.api_key,
                }},
            .timeout = std::chrono::milliseconds(1400),
        });
        auto model_ids = response.succeeded() ? parse_openai_models(response.body) : std::vector<std::string>{};
        if (model_ids.empty() && server.id == "ollama") {
            response = backend::http_get(
                normalize_local_base_url(server.base_url) + "/api/tags",
                std::chrono::milliseconds(1400));
            if (response.succeeded()) {
                model_ids = parse_ollama_models(response.body);
            }
        }
        std::ranges::sort(model_ids);
        model_ids.erase(std::ranges::unique(model_ids).begin(), model_ids.end());
        status.reachable = response.succeeded();
        if (!status.reachable) {
            status.message = response.error.empty()
                ? "HTTP " + std::to_string(response.status_code)
                : response.error;
        } else if (model_ids.empty()) {
            status.message = "Server responded, but no loaded models were reported";
        } else {
            status.message = std::to_string(model_ids.size()) + " model(s) ready";
        }
        for (const auto& model_id : model_ids) {
            status.models.push_back(DiscoveredLocalModel{
                .server_id = server.id,
                .server_name = server.display_name,
                .base_url = server.base_url,
                .api_key = server.api_key,
                .model_id = model_id,
                .teacher = server.teacher,
                .verifier = server.verifier,
                .coding_agent = server.coding_agent,
            });
        }
        report.models.insert(report.models.end(), status.models.begin(), status.models.end());
        report.servers.push_back(std::move(status));
    }
    return report;
}

ModelProfile LocalServerHub::make_model_profile(
    const DiscoveredLocalModel& discovered,
    const std::size_t ordinal) {
    const auto capabilities = inferred_capabilities(discovered.model_id);
    const auto is_coder = std::ranges::find(capabilities, "cpp23") != capabilities.end();
    return ModelProfile{
        .id = "local-" + sanitize_id(discovered.server_id) + '-' +
              sanitize_id(discovered.model_id) + '-' + std::to_string(ordinal),
        .display_name = discovered.server_name + " — " + discovered.model_id,
        .model_name = discovered.model_id,
        .endpoint = normalize_local_base_url(discovered.base_url),
        .api_key = discovered.api_key,
        .protocol = "openai",
        .capabilities = capabilities,
        .tokens_per_second = is_coder ? 35.0 : 50.0,
        .first_token_ms = 200.0,
        .quality = is_coder ? 0.80 : 0.72,
        .reliability = 0.92,
        .context_window = 32768,
        .enabled = true,
        .keep_hot = true,
        .supports_tools = true,
        .supports_lora = false,
        .teacher = discovered.teacher,
        .verifier = discovered.verifier,
        .custom_model = false,
        .coding_agent = discovered.coding_agent,
        .auto_discovered = true,
        .priority = is_coder ? 120 : 110,
    };
}

} // namespace threadspire::ai::models
