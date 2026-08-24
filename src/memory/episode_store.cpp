#include "threadspire/ai/memory/episode_store.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <ranges>
#include <set>
#include <unordered_set>
#include <sstream>
#include <stdexcept>

namespace threadspire::ai::memory {
namespace {

[[nodiscard]] std::string escape_json(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 16);
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (character < 0x20U) {
                std::ostringstream stream;
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character);
                result += stream.str();
            } else {
                result.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return result;
}

[[nodiscard]] std::string extract_json_string(
    const std::string_view line,
    const std::string_view key) {
    const auto marker = '"' + std::string(key) + '"';
    auto position = line.find(marker);
    if (position == std::string_view::npos) {
        return {};
    }
    position = line.find(':', position + marker.size());
    if (position == std::string_view::npos) {
        return {};
    }
    position = line.find('"', position + 1);
    if (position == std::string_view::npos) {
        return {};
    }
    ++position;

    std::string result;
    bool escaped = false;
    for (; position < line.size(); ++position) {
        const auto character = line[position];
        if (escaped) {
            switch (character) {
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            default:
                result.push_back(character);
                break;
            }
            escaped = false;
            continue;
        }
        if (character == '\\') {
            escaped = true;
            continue;
        }
        if (character == '"') {
            return result;
        }
        result.push_back(character);
    }
    return {};
}

[[nodiscard]] double extract_json_number(
    const std::string_view line,
    const std::string_view key) {
    const auto marker = '"' + std::string(key) + '"';
    auto position = line.find(marker);
    if (position == std::string_view::npos) {
        return 0.0;
    }
    position = line.find(':', position + marker.size());
    if (position == std::string_view::npos) {
        return 0.0;
    }
    ++position;
    const auto end = line.find_first_of(",}", position);
    try {
        return std::stod(std::string(line.substr(position, end - position)));
    } catch (...) {
        return 0.0;
    }
}

[[nodiscard]] bool extract_json_bool(
    const std::string_view line,
    const std::string_view key) {
    const auto marker = '"' + std::string(key) + '"';
    auto position = line.find(marker);
    if (position == std::string_view::npos) {
        return false;
    }
    position = line.find(':', position + marker.size());
    if (position == std::string_view::npos) {
        return false;
    }
    ++position;
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
        ++position;
    }
    return line.substr(position, 4) == "true";
}

[[nodiscard]] Episode parse_episode(const std::string_view line) {
    Episode episode;
    episode.id = extract_json_string(line, "id");
    episode.skill = extract_json_string(line, "skill");
    episode.prompt = extract_json_string(line, "prompt");
    episode.response = extract_json_string(line, "response");
    episode.model_id = extract_json_string(line, "model_id");
    episode.score = extract_json_number(line, "score");
    episode.compiler_passed = extract_json_bool(line, "compiler_passed");
    episode.tests_passed = extract_json_bool(line, "tests_passed");
    episode.human_accepted = extract_json_bool(line, "human_accepted");
    episode.human_rejected = extract_json_bool(line, "human_rejected");
    episode.deterministic_verified = extract_json_bool(line, "deterministic_verified");
    episode.reference_verified = extract_json_bool(line, "reference_verified");
    episode.self_generated = extract_json_bool(line, "self_generated");
    episode.eligible_for_training = extract_json_bool(line, "eligible_for_training");
    episode.verifier_model_id = extract_json_string(line, "verifier_model_id");
    episode.verification_notes = extract_json_string(line, "verification_notes");
    episode.provenance = extract_json_string(line, "provenance");
    episode.knowledge_destination = extract_json_string(line, "knowledge_destination");
    episode.content_hash = extract_json_string(line, "content_hash");
    episode.correction = extract_json_string(line, "correction");
    episode.tool_trace_hash = extract_json_string(line, "tool_trace_hash");
    episode.created_unix_ms = static_cast<std::int64_t>(extract_json_number(line, "created_unix_ms"));
    return episode;
}

[[nodiscard]] std::set<std::string> terms(const std::string_view value) {
    std::set<std::string> result;
    std::string current;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0 || character == '_' || character == '+') {
            current.push_back(static_cast<char>(std::tolower(character)));
        } else if (current.size() >= 3) {
            result.insert(std::move(current));
            current.clear();
        } else {
            current.clear();
        }
    }
    if (current.size() >= 3) {
        result.insert(std::move(current));
    }
    return result;
}

[[nodiscard]] double similarity(
    const std::set<std::string>& query,
    const Episode& episode) {
    if (query.empty()) {
        return 0.0;
    }
    auto candidate = terms(episode.prompt + ' ' + episode.response);
    std::size_t common = 0;
    for (const auto& item : query) {
        common += candidate.contains(item) ? 1U : 0U;
    }
    const auto lexical = static_cast<double>(common) /
                         std::sqrt(static_cast<double>(query.size() * std::max<std::size_t>(1, candidate.size())));
    const auto quality = std::clamp(episode.score, 0.0, 1.0);
    return lexical * (0.65 + quality * 0.35);
}

} // namespace

EpisodeStore::EpisodeStore(std::filesystem::path path) : path_(std::move(path)) {
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    std::lock_guard lock(mutex_);
    rebuild_index_unlocked();
}

void EpisodeStore::rebuild_index_unlocked() {
    content_hashes_.clear();
    retrievable_episodes_.clear();
    term_postings_.clear();
    accepted_counts_.clear();
    training_counts_.clear();
    if (!std::filesystem::is_regular_file(path_)) {
        return;
    }

    std::ifstream stream(path_, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        auto episode = parse_episode(line);
        if (!episode.id.empty() || !episode.content_hash.empty()) {
            index_episode_unlocked(episode);
        }
    }
}

void EpisodeStore::index_episode_unlocked(const Episode& episode) {
    if (!episode.content_hash.empty()) {
        content_hashes_.insert(episode.content_hash);
    }
    const auto independently_validated = episode.human_accepted || episode.deterministic_verified ||
                                         episode.reference_verified;
    if (independently_validated && !episode.human_rejected) {
        ++accepted_counts_[episode.skill];
    }
    if (episode.eligible_for_training && !episode.human_rejected) {
        ++training_counts_[episode.skill];
    }
    if (!independently_validated || episode.human_rejected || episode.prompt.empty() ||
        episode.response.empty()) {
        return;
    }

    const auto index = retrievable_episodes_.size();
    retrievable_episodes_.push_back(episode);
    for (const auto& term : terms(episode.prompt + ' ' + episode.response)) {
        term_postings_[term].push_back(index);
    }
}

void EpisodeStore::append(const Episode& episode) {
    std::lock_guard lock(mutex_);
    std::ofstream stream(path_, std::ios::app | std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to append episodic memory: " + path_.string());
    }
    stream << '{'
           << "\"id\":\"" << escape_json(episode.id) << "\","
           << "\"skill\":\"" << escape_json(episode.skill) << "\","
           << "\"prompt\":\"" << escape_json(episode.prompt) << "\","
           << "\"response\":\"" << escape_json(episode.response) << "\","
           << "\"model_id\":\"" << escape_json(episode.model_id) << "\","
           << "\"score\":" << std::clamp(episode.score, 0.0, 1.0) << ','
           << "\"compiler_passed\":" << (episode.compiler_passed ? "true" : "false") << ','
           << "\"tests_passed\":" << (episode.tests_passed ? "true" : "false") << ','
           << "\"human_accepted\":" << (episode.human_accepted ? "true" : "false") << ','
           << "\"human_rejected\":" << (episode.human_rejected ? "true" : "false") << ','
           << "\"deterministic_verified\":" << (episode.deterministic_verified ? "true" : "false") << ','
           << "\"reference_verified\":" << (episode.reference_verified ? "true" : "false") << ','
           << "\"self_generated\":" << (episode.self_generated ? "true" : "false") << ','
           << "\"eligible_for_training\":" << (episode.eligible_for_training ? "true" : "false") << ','
           << "\"verifier_model_id\":\"" << escape_json(episode.verifier_model_id) << "\","
           << "\"verification_notes\":\"" << escape_json(episode.verification_notes) << "\","
           << "\"provenance\":\"" << escape_json(episode.provenance) << "\","
           << "\"knowledge_destination\":\"" << escape_json(episode.knowledge_destination) << "\","
           << "\"content_hash\":\"" << escape_json(episode.content_hash) << "\","
           << "\"correction\":\"" << escape_json(episode.correction) << "\","
           << "\"tool_trace_hash\":\"" << escape_json(episode.tool_trace_hash) << "\","
           << "\"created_unix_ms\":" << episode.created_unix_ms
           << "}\n";
    stream.flush();
    if (!stream) {
        throw std::runtime_error("unable to flush episodic memory: " + path_.string());
    }
    index_episode_unlocked(episode);
}

bool EpisodeStore::append_if_new(Episode episode) {
    if (episode.content_hash.empty()) {
        episode.content_hash = make_content_hash(episode.skill, episode.prompt, episode.response);
    }
    std::lock_guard lock(mutex_);
    if (contains_hash_unlocked(episode.content_hash)) {
        return false;
    }
    std::ofstream stream(path_, std::ios::app | std::ios::binary);
    if (!stream) {
        throw std::runtime_error("unable to append episodic memory: " + path_.string());
    }
    stream << '{'
           << "\"id\":\"" << escape_json(episode.id) << "\","
           << "\"skill\":\"" << escape_json(episode.skill) << "\","
           << "\"prompt\":\"" << escape_json(episode.prompt) << "\","
           << "\"response\":\"" << escape_json(episode.response) << "\","
           << "\"model_id\":\"" << escape_json(episode.model_id) << "\","
           << "\"score\":" << std::clamp(episode.score, 0.0, 1.0) << ','
           << "\"compiler_passed\":" << (episode.compiler_passed ? "true" : "false") << ','
           << "\"tests_passed\":" << (episode.tests_passed ? "true" : "false") << ','
           << "\"human_accepted\":" << (episode.human_accepted ? "true" : "false") << ','
           << "\"human_rejected\":" << (episode.human_rejected ? "true" : "false") << ','
           << "\"deterministic_verified\":" << (episode.deterministic_verified ? "true" : "false") << ','
           << "\"reference_verified\":" << (episode.reference_verified ? "true" : "false") << ','
           << "\"self_generated\":" << (episode.self_generated ? "true" : "false") << ','
           << "\"eligible_for_training\":" << (episode.eligible_for_training ? "true" : "false") << ','
           << "\"verifier_model_id\":\"" << escape_json(episode.verifier_model_id) << "\","
           << "\"verification_notes\":\"" << escape_json(episode.verification_notes) << "\","
           << "\"provenance\":\"" << escape_json(episode.provenance) << "\","
           << "\"knowledge_destination\":\"" << escape_json(episode.knowledge_destination) << "\","
           << "\"content_hash\":\"" << escape_json(episode.content_hash) << "\","
           << "\"correction\":\"" << escape_json(episode.correction) << "\","
           << "\"tool_trace_hash\":\"" << escape_json(episode.tool_trace_hash) << "\","
           << "\"created_unix_ms\":" << episode.created_unix_ms
           << "}\n";
    stream.flush();
    if (!stream) {
        throw std::runtime_error("unable to flush episodic memory: " + path_.string());
    }
    index_episode_unlocked(episode);
    return true;
}

bool EpisodeStore::contains_hash_unlocked(const std::string_view content_hash) const {
    return !content_hash.empty() && content_hashes_.contains(std::string(content_hash));
}

bool EpisodeStore::contains_hash(const std::string_view content_hash) const {
    std::lock_guard lock(mutex_);
    return contains_hash_unlocked(content_hash);
}

std::vector<RetrievedEpisode> EpisodeStore::retrieve(
    const std::string_view query,
    const std::size_t limit) const {
    std::lock_guard lock(mutex_);
    std::vector<RetrievedEpisode> result;
    if (limit == 0 || retrievable_episodes_.empty()) {
        return result;
    }

    const auto query_terms = terms(query);
    std::unordered_set<std::size_t> candidates;
    for (const auto& term : query_terms) {
        const auto posting = term_postings_.find(term);
        if (posting == term_postings_.end()) {
            continue;
        }
        candidates.insert(posting->second.begin(), posting->second.end());
    }
    result.reserve(std::min(candidates.size(), limit * 8U));
    for (const auto index : candidates) {
        if (index >= retrievable_episodes_.size()) {
            continue;
        }
        const auto& episode = retrievable_episodes_[index];
        const auto relevance = similarity(query_terms, episode);
        if (relevance > 0.04) {
            result.push_back({.episode = episode, .relevance = relevance});
        }
    }

    std::ranges::sort(result, std::greater{}, &RetrievedEpisode::relevance);
    if (result.size() > limit) {
        result.resize(limit);
    }
    return result;
}

std::size_t EpisodeStore::count_for_skill(const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    const auto iterator = accepted_counts_.find(std::string(skill));
    return iterator == accepted_counts_.end() ? 0U : iterator->second;
}

std::size_t EpisodeStore::training_count_for_skill(const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    const auto iterator = training_counts_.find(std::string(skill));
    return iterator == training_counts_.end() ? 0U : iterator->second;
}

std::filesystem::path EpisodeStore::path() const {
    return path_;
}

std::int64_t now_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string make_episode_id() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    std::ostringstream stream;
    stream << "episode-" << now_unix_ms() << '-' << std::hex << generator();
    return stream.str();
}

std::string make_content_hash(
    const std::string_view skill,
    const std::string_view prompt,
    const std::string_view response) {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    auto hash = offset;
    const auto consume = [&hash](const std::string_view value) {
        for (const unsigned char character : value) {
            const auto normalized = std::isspace(character) != 0
                ? static_cast<unsigned char>(' ')
                : static_cast<unsigned char>(std::tolower(character));
            hash ^= normalized;
            hash *= prime;
        }
        hash ^= static_cast<unsigned char>(0xFFU);
        hash *= prime;
    };
    consume(skill);
    consume(prompt);
    consume(response);
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}

} // namespace threadspire::ai::memory
