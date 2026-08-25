module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <stdexcept>

module threadspire.learning;
import threadspire.core;
import threadspire.storage;

namespace threadspire::ai::learning {

std::string_view to_string(const ExpertState state) noexcept {
    switch (state) {
    case ExpertState::memory_only:
        return "memory-only";
    case ExpertState::lora_incubating:
        return "lora-incubating";
    case ExpertState::lora_active:
        return "lora-active";
    case ExpertState::consolidating:
        return "consolidating";
    case ExpertState::full_rank_active:
        return "full-rank-active";
    case ExpertState::rejected:
        return "rejected";
    }
    return "memory-only";
}

ExpertState expert_state_from_string(const std::string_view value) {
    if (value == "lora-incubating") {
        return ExpertState::lora_incubating;
    }
    if (value == "lora-active") {
        return ExpertState::lora_active;
    }
    if (value == "consolidating") {
        return ExpertState::consolidating;
    }
    if (value == "full-rank-active") {
        return ExpertState::full_rank_active;
    }
    if (value == "rejected") {
        return ExpertState::rejected;
    }
    return ExpertState::memory_only;
}

std::string normalize_skill(const std::string_view skill) {
    std::string result;
    result.reserve(skill.size());
    bool hyphen = false;
    for (const unsigned char character : skill) {
        if (std::isalnum(character) != 0) {
            result.push_back(static_cast<char>(std::tolower(character)));
            hyphen = false;
        } else if (!result.empty() && !hyphen) {
            result.push_back('-');
            hyphen = true;
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    return result.empty() ? "general" : result;
}

ExpertRegistry::ExpertRegistry(std::filesystem::path directory)
    : directory_(std::move(directory)) {
    std::filesystem::create_directories(directory_);
}

void ExpertRegistry::load() {
    modules_.clear();
    if (!std::filesystem::is_directory(directory_)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ini") {
            continue;
        }
        try {
            const auto ini = core::IniFile::load(entry.path());
            ExpertModule module;
            module.id = ini.get_string("expert", "id", entry.path().stem().string());
            module.skill = ini.get_string("expert", "skill", module.id);
            module.base_model = ini.get_string("expert", "base_model", "");
            module.state = expert_state_from_string(ini.get_string("expert", "state", "memory-only"));
            module.adapter_path = ini.get_string("expert", "adapter_path", "");
            module.module_path = ini.get_string("expert", "module_path", "");
            module.previous_state = expert_state_from_string(
                ini.get_string("rollback", "previous_state", "memory-only"));
            module.previous_adapter_path = ini.get_string("rollback", "previous_adapter_path", "");
            module.previous_module_path = ini.get_string("rollback", "previous_module_path", "");
            module.promoted_candidate_id = ini.get_string("expert", "promoted_candidate_id", "");
            module.generation = static_cast<std::uint64_t>(std::max<std::int64_t>(
                0, ini.get_integer("expert", "generation", 0)));
            module.accepted_episodes = static_cast<std::size_t>(std::max<std::int64_t>(
                0, ini.get_integer("metrics", "accepted_episodes", 0)));
            module.episodes_since_update = static_cast<std::size_t>(std::max<std::int64_t>(
                0, ini.get_integer("metrics", "episodes_since_update", 0)));
            module.lora_generations = static_cast<std::size_t>(std::max<std::int64_t>(
                0, ini.get_integer("metrics", "lora_generations", 0)));
            module.compiler_verified_episodes = static_cast<std::size_t>(std::max<std::int64_t>(
                0, ini.get_integer("metrics", "compiler_verified_episodes", 0)));
            module.test_verified_episodes = static_cast<std::size_t>(std::max<std::int64_t>(
                0, ini.get_integer("metrics", "test_verified_episodes", 0)));
            module.validation_score = ini.get_number("metrics", "validation_score", 0.0);
            module.regression_score = ini.get_number("metrics", "regression_score", 0.0);
            module.updated_unix_ms = ini.get_integer("metrics", "updated_unix_ms", 0);
            modules_.push_back(std::move(module));
        } catch (...) {
            continue;
        }
    }
    std::ranges::sort(modules_, {}, &ExpertModule::skill);
}

void ExpertRegistry::save(const ExpertModule& module) const {
    std::filesystem::create_directories(directory_);
    const auto path = directory_ / (normalize_skill(module.skill) + ".ini");
    const auto temporary = path.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to save expert registry entry: " + path.string());
    }
    stream << "[expert]\n"
           << "id=" << module.id << '\n'
           << "skill=" << module.skill << '\n'
           << "base_model=" << module.base_model << '\n'
           << "state=" << to_string(module.state) << '\n'
           << "adapter_path=" << module.adapter_path.generic_string() << '\n'
           << "module_path=" << module.module_path.generic_string() << '\n'
           << "promoted_candidate_id=" << module.promoted_candidate_id << '\n'
           << "generation=" << module.generation << "\n\n"
           << "[rollback]\n"
           << "previous_state=" << to_string(module.previous_state) << '\n'
           << "previous_adapter_path=" << module.previous_adapter_path.generic_string() << '\n'
           << "previous_module_path=" << module.previous_module_path.generic_string() << "\n\n"
           << "[metrics]\n"
           << "accepted_episodes=" << module.accepted_episodes << '\n'
           << "episodes_since_update=" << module.episodes_since_update << '\n'
           << "lora_generations=" << module.lora_generations << '\n'
           << "compiler_verified_episodes=" << module.compiler_verified_episodes << '\n'
           << "test_verified_episodes=" << module.test_verified_episodes << '\n'
           << "validation_score=" << module.validation_score << '\n'
           << "regression_score=" << module.regression_score << '\n'
           << "updated_unix_ms=" << module.updated_unix_ms << '\n';
    stream.close();
    if (!stream) {
        throw std::runtime_error("unable to flush expert registry entry: " + path.string());
    }
    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("unable to publish expert registry entry: " + error.message());
    }
}

ExpertModule& ExpertRegistry::ensure(
    const std::string_view skill,
    const std::string_view base_model) {
    const auto normalized = normalize_skill(skill);
    if (auto* existing = find(normalized); existing != nullptr) {
        if (existing->base_model.empty()) {
            existing->base_model = std::string(base_model);
        }
        return *existing;
    }

    modules_.push_back(ExpertModule{
        .id = "expert-" + normalized,
        .skill = normalized,
        .base_model = std::string(base_model),
        .state = ExpertState::memory_only,
        .adapter_path = {},
        .module_path = {},
        .previous_state = ExpertState::memory_only,
        .previous_adapter_path = {},
        .previous_module_path = {},
        .promoted_candidate_id = {},
        .generation = 0,
        .accepted_episodes = 0,
        .episodes_since_update = 0,
        .lora_generations = 0,
        .compiler_verified_episodes = 0,
        .test_verified_episodes = 0,
        .validation_score = 0.0,
        .regression_score = 0.0,
        .updated_unix_ms = memory::now_unix_ms(),
    });
    return modules_.back();
}

ExpertModule* ExpertRegistry::find(const std::string_view skill) noexcept {
    const auto normalized = normalize_skill(skill);
    const auto iterator = std::ranges::find(modules_, normalized, &ExpertModule::skill);
    return iterator == modules_.end() ? nullptr : &*iterator;
}

const ExpertModule* ExpertRegistry::find(const std::string_view skill) const noexcept {
    const auto normalized = normalize_skill(skill);
    const auto iterator = std::ranges::find(modules_, normalized, &ExpertModule::skill);
    return iterator == modules_.end() ? nullptr : &*iterator;
}

const std::vector<ExpertModule>& ExpertRegistry::modules() const noexcept {
    return modules_;
}

bool ExpertRegistry::ready_for_lora(
    const ExpertModule& module,
    const PromotionPolicy& policy) const noexcept {
    return module.state != ExpertState::consolidating &&
           module.episodes_since_update >= policy.lora_microbatch;
}

bool ExpertRegistry::ready_for_consolidation(
    const ExpertModule& module,
    const PromotionPolicy& policy) const noexcept {
    const auto code_skill = module.skill.starts_with("cpp23-") || module.skill == "build-systems";
    const auto verification_ready = !code_skill ||
        (module.compiler_verified_episodes >= policy.minimum_compiler_verified_for_code &&
         module.test_verified_episodes >= policy.minimum_test_verified_for_code);
    return module.state == ExpertState::lora_active &&
           module.accepted_episodes >= policy.consolidate_after_episodes &&
           module.lora_generations >= policy.consolidate_after_generations &&
           module.validation_score >= policy.minimum_validation_score &&
           module.regression_score <= policy.maximum_regression &&
           verification_ready;
}

} // namespace threadspire::ai::learning
