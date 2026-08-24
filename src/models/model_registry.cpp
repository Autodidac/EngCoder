#include "threadspire/ai/models/model_registry.hpp"

#include "threadspire/ai/core/ini.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

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

[[nodiscard]] std::string join_csv(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            stream << ',';
        }
        stream << values[index];
    }
    return stream.str();
}

[[nodiscard]] ModelProfile read_profile(
    const core::IniFile& ini,
    const std::string& id) {
    const auto section = "model." + id;
    ModelProfile profile;
    profile.id = id;
    profile.display_name = ini.get_string(section, "display_name", id);
    profile.model_name = ini.get_string(section, "model", id);
    profile.endpoint = ini.get_string(section, "endpoint", "internal");
    profile.api_key = ini.get_string(section, "api_key", "");
    profile.protocol = ini.get_string(
        section,
        "protocol",
        profile.endpoint == "internal" ? "internal" : "openai");
    profile.capabilities = split_csv(ini.get_string(section, "capabilities", "english,chat"));
    profile.tokens_per_second = ini.get_number(section, "tokens_per_second", 20.0);
    profile.first_token_ms = ini.get_number(section, "first_token_ms", 250.0);
    profile.quality = ini.get_number(section, "quality", 0.6);
    profile.reliability = ini.get_number(section, "reliability", 0.9);
    profile.context_window = static_cast<std::size_t>(std::max<std::int64_t>(
        256,
        ini.get_integer(section, "context_window", 4096)));
    profile.enabled = ini.get_bool(section, "enabled", true);
    profile.keep_hot = ini.get_bool(section, "keep_hot", false);
    profile.supports_tools = ini.get_bool(section, "supports_tools", false);
    profile.supports_lora = ini.get_bool(section, "supports_lora", true);
    profile.teacher = ini.get_bool(section, "teacher", false);
    profile.verifier = ini.get_bool(section, "verifier", false);
    profile.custom_model = ini.get_bool(section, "custom_model", false);
    profile.coding_agent = ini.get_bool(section, "coding_agent", false);
    profile.auto_discovered = ini.get_bool(section, "auto_discovered", false);
    profile.priority = static_cast<int>(ini.get_integer(section, "priority", 0));
    return profile;
}

} // namespace

ModelRegistry ModelRegistry::load(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        auto result = defaults();
        result.save(path);
        return result;
    }

    const auto ini = core::IniFile::load(path);
    auto identifiers = split_csv(ini.get_string("models", "ids", ""));
    if (identifiers.empty()) {
        return defaults();
    }

    ModelRegistry result;
    result.models_.reserve(identifiers.size());
    for (const auto& identifier : identifiers) {
        result.models_.push_back(read_profile(ini, identifier));
    }
    return result;
}

ModelRegistry ModelRegistry::defaults() {
    ModelRegistry result;
    result.models_ = {
        ModelProfile{
            .id = "fast",
            .display_name = "ThreadSpire Internal — fast lane",
            .model_name = "threadspire://fast",
            .endpoint = "internal",
            .api_key = {},
            .protocol = "internal",
            .capabilities = {"english", "chat", "explanation", "transformation", "translation", "cpp23"},
            .tokens_per_second = 0.0,
            .first_token_ms = 0.0,
            .quality = 0.20,
            .reliability = 0.50,
            .context_window = 1024,
            .enabled = true,
            .keep_hot = true,
            .supports_tools = false,
            .supports_lora = false,
            .teacher = false,
            .verifier = false,
            .custom_model = true,
            .coding_agent = false,
            .auto_discovered = false,
            .priority = 100,
        },
        ModelProfile{
            .id = "coder",
            .display_name = "ThreadSpire Internal — C++23 coding",
            .model_name = "threadspire://coder",
            .endpoint = "internal",
            .api_key = {},
            .protocol = "internal",
            .capabilities = {"english", "chat", "explanation", "cpp23", "debugging", "repository", "tools"},
            .tokens_per_second = 0.0,
            .first_token_ms = 0.0,
            .quality = 0.20,
            .reliability = 0.50,
            .context_window = 1024,
            .enabled = true,
            .keep_hot = true,
            .supports_tools = true,
            .supports_lora = false,
            .teacher = false,
            .verifier = false,
            .custom_model = true,
            .coding_agent = true,
            .auto_discovered = false,
            .priority = 95,
        },
        ModelProfile{
            .id = "thinker",
            .display_name = "ThreadSpire Internal — reasoning",
            .model_name = "threadspire://thinker",
            .endpoint = "internal",
            .api_key = {},
            .protocol = "internal",
            .capabilities = {"english", "explanation", "planning", "cpp23", "debugging", "repository", "tools"},
            .tokens_per_second = 0.0,
            .first_token_ms = 0.0,
            .quality = 0.20,
            .reliability = 0.50,
            .context_window = 1024,
            .enabled = true,
            .keep_hot = false,
            .supports_tools = true,
            .supports_lora = false,
            .teacher = false,
            .verifier = true,
            .custom_model = true,
            .coding_agent = true,
            .auto_discovered = false,
            .priority = 80,
        },
        ModelProfile{
            .id = "reviewer",
            .display_name = "ThreadSpire Internal — verifier",
            .model_name = "threadspire://reviewer",
            .endpoint = "internal",
            .api_key = {},
            .protocol = "internal",
            .capabilities = {"english", "review", "cpp23", "debugging"},
            .tokens_per_second = 0.0,
            .first_token_ms = 0.0,
            .quality = 0.20,
            .reliability = 0.50,
            .context_window = 1024,
            .enabled = true,
            .keep_hot = true,
            .supports_tools = false,
            .supports_lora = false,
            .teacher = false,
            .verifier = true,
            .custom_model = true,
            .coding_agent = false,
            .auto_discovered = false,
            .priority = 85,
        },
    };
    return result;
}

void ModelRegistry::save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to write model registry: " + path.string());
    }

    stream << "[models]\nids=";
    for (std::size_t index = 0; index < models_.size(); ++index) {
        if (index != 0U) {
            stream << ',';
        }
        stream << models_[index].id;
    }
    stream << "\n\n";

    for (const auto& model : models_) {
        stream << "[model." << model.id << "]\n"
               << "display_name=" << model.display_name << '\n'
               << "model=" << model.model_name << '\n'
               << "endpoint=" << model.endpoint << '\n'
               << "api_key=" << model.api_key << '\n'
               << "protocol=" << model.protocol << '\n'
               << "capabilities=" << join_csv(model.capabilities) << '\n'
               << "tokens_per_second=" << model.tokens_per_second << '\n'
               << "first_token_ms=" << model.first_token_ms << '\n'
               << "quality=" << model.quality << '\n'
               << "reliability=" << model.reliability << '\n'
               << "context_window=" << model.context_window << '\n'
               << "enabled=" << (model.enabled ? "true" : "false") << '\n'
               << "keep_hot=" << (model.keep_hot ? "true" : "false") << '\n'
               << "supports_tools=" << (model.supports_tools ? "true" : "false") << '\n'
               << "supports_lora=" << (model.supports_lora ? "true" : "false") << '\n'
               << "teacher=" << (model.teacher ? "true" : "false") << '\n'
               << "verifier=" << (model.verifier ? "true" : "false") << '\n'
               << "custom_model=" << (model.custom_model ? "true" : "false") << '\n'
               << "coding_agent=" << (model.coding_agent ? "true" : "false") << '\n'
               << "auto_discovered=" << (model.auto_discovered ? "true" : "false") << '\n'
               << "priority=" << model.priority << "\n\n";
    }
    stream.close();
    if (!stream) {
        throw std::runtime_error("unable to flush model registry: " + path.string());
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("unable to publish model registry: " + error.message());
    }
}

const std::vector<ModelProfile>& ModelRegistry::models() const noexcept { return models_; }
std::vector<ModelProfile>& ModelRegistry::models() noexcept { return models_; }

const ModelProfile* ModelRegistry::find(const std::string_view id) const noexcept {
    const auto iterator = std::ranges::find(models_, id, &ModelProfile::id);
    return iterator == models_.end() ? nullptr : &*iterator;
}

ModelProfile* ModelRegistry::find(const std::string_view id) noexcept {
    const auto iterator = std::ranges::find(models_, id, &ModelProfile::id);
    return iterator == models_.end() ? nullptr : &*iterator;
}

void ModelRegistry::upsert(ModelProfile profile) {
    if (auto* existing = find(profile.id); existing != nullptr) {
        *existing = std::move(profile);
        return;
    }
    models_.push_back(std::move(profile));
}

void ModelRegistry::remove_auto_discovered() {
    std::erase_if(models_, [](const ModelProfile& model) { return model.auto_discovered; });
}

void ModelRegistry::record_observation(
    const std::string_view id,
    const double measured_tokens_per_second,
    const bool success) noexcept {
    auto* model = find(id);
    if (model == nullptr) {
        return;
    }
    if (measured_tokens_per_second > 0.0) {
        constexpr double speed_alpha = 0.18;
        model->tokens_per_second =
            (1.0 - speed_alpha) * model->tokens_per_second + speed_alpha * measured_tokens_per_second;
    }
    constexpr double reliability_alpha = 0.08;
    const auto sample = success ? 1.0 : 0.0;
    model->reliability = std::clamp(
        (1.0 - reliability_alpha) * model->reliability + reliability_alpha * sample,
        0.0,
        1.0);
}

void ModelRegistry::record_feedback(const std::string_view id, const bool accepted) noexcept {
    auto* model = find(id);
    if (model == nullptr) {
        return;
    }
    const auto alpha = accepted ? 0.045 : 0.12;
    const auto sample = accepted ? 1.0 : 0.0;
    model->quality = std::clamp(
        (1.0 - alpha) * model->quality + alpha * sample,
        0.05,
        0.99);
}

bool has_capability(const ModelProfile& model, const std::string_view capability) noexcept {
    return std::ranges::find(model.capabilities, capability) != model.capabilities.end();
}

} // namespace threadspire::ai::models
