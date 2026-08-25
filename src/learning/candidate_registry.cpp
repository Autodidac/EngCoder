module;
#include "module_prelude.hpp"
#include <algorithm>
#include <fstream>
#include <random>
#include <ranges>
#include <sstream>
#include <stdexcept>

module threadspire.learning;
import threadspire.core;
import threadspire.storage;

namespace threadspire::ai::learning {
namespace {

[[nodiscard]] std::filesystem::path record_path(
    const std::filesystem::path& directory,
    const std::string_view id) {
    return directory / (std::string(id) + ".ini");
}

[[nodiscard]] std::string quoted(const std::filesystem::path& path) {
    return path.generic_string();
}

} // namespace

std::string_view to_string(const CandidateKind kind) noexcept {
    switch (kind) {
    case CandidateKind::lora_overlay:
        return "lora-overlay";
    case CandidateKind::full_rank_skill:
        return "full-rank-skill";
    case CandidateKind::routing_policy:
        return "routing-policy";
    case CandidateKind::prompt_policy:
        return "prompt-policy";
    }
    return "lora-overlay";
}

std::string_view to_string(const CandidateState state) noexcept {
    switch (state) {
    case CandidateState::staged:
        return "staged";
    case CandidateState::training:
        return "training";
    case CandidateState::evaluating:
        return "evaluating";
    case CandidateState::approved:
        return "approved";
    case CandidateState::rejected:
        return "rejected";
    case CandidateState::promoted:
        return "promoted";
    case CandidateState::interrupted:
        return "interrupted";
    }
    return "staged";
}

CandidateKind candidate_kind_from_string(const std::string_view value) noexcept {
    if (value == "full-rank-skill") {
        return CandidateKind::full_rank_skill;
    }
    if (value == "routing-policy") {
        return CandidateKind::routing_policy;
    }
    if (value == "prompt-policy") {
        return CandidateKind::prompt_policy;
    }
    return CandidateKind::lora_overlay;
}

CandidateState candidate_state_from_string(const std::string_view value) noexcept {
    if (value == "training") {
        return CandidateState::training;
    }
    if (value == "evaluating") {
        return CandidateState::evaluating;
    }
    if (value == "approved") {
        return CandidateState::approved;
    }
    if (value == "rejected") {
        return CandidateState::rejected;
    }
    if (value == "promoted") {
        return CandidateState::promoted;
    }
    if (value == "interrupted") {
        return CandidateState::interrupted;
    }
    return CandidateState::staged;
}

std::string make_candidate_id(const std::string_view skill) {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::ostringstream stream;
    stream << "candidate-" << normalize_skill(skill) << '-' << memory::now_unix_ms()
           << '-' << std::hex << generator();
    return stream.str();
}

CandidateRegistry::CandidateRegistry(std::filesystem::path directory)
    : directory_(std::move(directory)) {
    std::filesystem::create_directories(directory_);
}

void CandidateRegistry::load() {
    candidates_.clear();
    if (!std::filesystem::is_directory(directory_)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ini") {
            continue;
        }
        try {
            const auto ini = core::IniFile::load(entry.path());
            CandidateRecord candidate;
            candidate.id = ini.get_string("candidate", "id", entry.path().stem().string());
            candidate.skill = ini.get_string("candidate", "skill", "general");
            candidate.base_model = ini.get_string("candidate", "base_model", "");
            candidate.kind = candidate_kind_from_string(
                ini.get_string("candidate", "kind", "lora-overlay"));
            candidate.state = candidate_state_from_string(
                ini.get_string("candidate", "state", "staged"));
            candidate.parent_generation = static_cast<std::uint64_t>(std::max<std::int64_t>(
                0, ini.get_integer("candidate", "parent_generation", 0)));
            candidate.artifact_path = ini.get_string("paths", "artifact", "");
            candidate.job_directory = ini.get_string("paths", "job_directory", "");
            candidate.log_path = ini.get_string("paths", "log", "");
            candidate.validation_score = ini.get_number("metrics", "validation_score", 0.0);
            candidate.regression_score = ini.get_number("metrics", "regression_score", 1.0);
            candidate.training_episode_count = static_cast<std::uint64_t>(
                std::max<std::int64_t>(
                    0, ini.get_integer("metrics", "training_episode_count", 0)));
            candidate.message = ini.get_string("candidate", "message", "");
            candidate.created_unix_ms = ini.get_integer("time", "created_unix_ms", 0);
            candidate.updated_unix_ms = ini.get_integer("time", "updated_unix_ms", 0);
            candidates_.push_back(std::move(candidate));
        } catch (...) {
            // A corrupt candidate record is ignored; active intelligence is stored separately.
        }
    }
    std::ranges::sort(candidates_, {}, &CandidateRecord::created_unix_ms);
}

CandidateRecord CandidateRegistry::create(
    const std::string_view skill,
    const std::string_view base_model,
    const CandidateKind kind,
    const std::uint64_t parent_generation,
    const std::filesystem::path& artifact_path,
    const std::filesystem::path& job_directory,
    const std::filesystem::path& log_path) {
    CandidateRecord candidate{
        .id = make_candidate_id(skill),
        .skill = normalize_skill(skill),
        .base_model = std::string(base_model),
        .kind = kind,
        .state = CandidateState::staged,
        .parent_generation = parent_generation,
        .artifact_path = artifact_path,
        .job_directory = job_directory,
        .log_path = log_path,
        .validation_score = 0.0,
        .regression_score = 1.0,
        .training_episode_count = 0,
        .message = "candidate staged beside the active skill",
        .created_unix_ms = memory::now_unix_ms(),
        .updated_unix_ms = memory::now_unix_ms(),
    };
    candidates_.push_back(candidate);
    save(candidate);
    return candidate;
}

void CandidateRegistry::save(const CandidateRecord& candidate) const {
    std::filesystem::create_directories(directory_);
    const auto destination = record_path(directory_, candidate.id);
    const auto temporary = destination.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("unable to write candidate record: " + destination.string());
        }
        stream << "[candidate]\n"
               << "id=" << candidate.id << '\n'
               << "skill=" << candidate.skill << '\n'
               << "base_model=" << candidate.base_model << '\n'
               << "kind=" << to_string(candidate.kind) << '\n'
               << "state=" << to_string(candidate.state) << '\n'
               << "parent_generation=" << candidate.parent_generation << '\n'
               << "message=" << candidate.message << "\n\n"
               << "[paths]\n"
               << "artifact=" << quoted(candidate.artifact_path) << '\n'
               << "job_directory=" << quoted(candidate.job_directory) << '\n'
               << "log=" << quoted(candidate.log_path) << "\n\n"
               << "[metrics]\n"
               << "validation_score=" << candidate.validation_score << '\n'
               << "regression_score=" << candidate.regression_score << '\n'
               << "training_episode_count=" << candidate.training_episode_count << "\n\n"
               << "[time]\n"
               << "created_unix_ms=" << candidate.created_unix_ms << '\n'
               << "updated_unix_ms=" << candidate.updated_unix_ms << '\n';
    }
    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::runtime_error("unable to publish candidate record: " + error.message());
    }
}

CandidateRecord* CandidateRegistry::find(const std::string_view id) noexcept {
    const auto iterator = std::ranges::find(candidates_, id, &CandidateRecord::id);
    return iterator == candidates_.end() ? nullptr : &*iterator;
}

const CandidateRecord* CandidateRegistry::find(const std::string_view id) const noexcept {
    const auto iterator = std::ranges::find(candidates_, id, &CandidateRecord::id);
    return iterator == candidates_.end() ? nullptr : &*iterator;
}

std::optional<CandidateRecord> CandidateRegistry::resumable_for(
    const std::string_view skill,
    const CandidateKind kind) const {
    const auto normalized = normalize_skill(skill);
    for (auto iterator = candidates_.rbegin(); iterator != candidates_.rend(); ++iterator) {
        if (iterator->skill == normalized && iterator->kind == kind &&
            (iterator->state == CandidateState::interrupted ||
             iterator->state == CandidateState::staged ||
             iterator->state == CandidateState::training ||
             iterator->state == CandidateState::evaluating)) {
            return *iterator;
        }
    }
    return std::nullopt;
}

std::vector<CandidateRecord> CandidateRegistry::mark_inflight_interrupted() {
    std::vector<CandidateRecord> interrupted;
    for (auto& candidate : candidates_) {
        if (candidate.state == CandidateState::training ||
            candidate.state == CandidateState::evaluating) {
            candidate.state = CandidateState::interrupted;
            candidate.message = "application stopped while candidate was in flight; resumable";
            candidate.updated_unix_ms = memory::now_unix_ms();
            save(candidate);
            interrupted.push_back(candidate);
        }
    }
    return interrupted;
}

const std::vector<CandidateRecord>& CandidateRegistry::candidates() const noexcept {
    return candidates_;
}

} // namespace threadspire::ai::learning
