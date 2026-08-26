module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

module threadspire.storage;

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

[[nodiscard]] std::string normalized_prompt(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    bool pending_space = false;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0 || character == '+' || character == '_') {
            if (pending_space && !result.empty()) {
                result.push_back(' ');
            }
            result.push_back(static_cast<char>(std::tolower(character)));
            pending_space = false;
        } else if (!result.empty()) {
            pending_space = true;
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> prompt_words(const std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const auto end = value.find(' ', begin);
        result.push_back(value.substr(
            begin,
            end == std::string_view::npos ? value.size() - begin : end - begin));
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] bool safe_conversation_lookup(const std::string_view original) {
    if (original.size() < 4U || original.size() > 56U) {
        return false;
    }
    for (const unsigned char character : original) {
        if (std::isdigit(character) != 0 ||
            std::string_view("{}[]();=<>\\/" "\x60" "_+").find(static_cast<char>(character)) !=
                std::string_view::npos) {
            return false;
        }
        if (character >= 0x80U) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t bounded_edit_distance(
    const std::string_view left,
    const std::string_view right,
    const std::size_t maximum) {
    if (left.size() > right.size() + maximum || right.size() > left.size() + maximum) {
        return maximum + 1U;
    }
    std::vector<std::size_t> previous(right.size() + 1U);
    std::vector<std::size_t> current(right.size() + 1U);
    for (std::size_t column = 0; column <= right.size(); ++column) {
        previous[column] = column;
    }
    for (std::size_t row = 1; row <= left.size(); ++row) {
        current[0] = row;
        auto row_minimum = current[0];
        for (std::size_t column = 1; column <= right.size(); ++column) {
            const auto substitution = previous[column - 1U] +
                (left[row - 1U] == right[column - 1U] ? 0U : 1U);
            current[column] = std::min({
                previous[column] + 1U,
                current[column - 1U] + 1U,
                substitution,
            });
            row_minimum = std::min(row_minimum, current[column]);
        }
        if (row_minimum > maximum) {
            return maximum + 1U;
        }
        previous.swap(current);
    }
    return previous.back();
}

[[nodiscard]] bool compatible_verified_skill(
    const std::string_view candidate,
    const std::string_view expected) noexcept {
    return candidate == expected ||
           (candidate.starts_with("cpp23-") && expected.starts_with("cpp23-"));
}

[[nodiscard]] std::string prompt_key(const Episode& episode) {
    return normalized_prompt(episode.skill) + '\n' + normalized_prompt(episode.prompt);
}

[[nodiscard]] UserMemoryItem parse_user_memory(const std::string_view line) {
    return UserMemoryItem{
        .id = extract_json_string(line, "id"),
        .text = extract_json_string(line, "text"),
        .source = extract_json_string(line, "source"),
        .pinned = extract_json_bool(line, "pinned"),
        .created_unix_ms = static_cast<std::int64_t>(extract_json_number(line, "created_unix_ms")),
        .updated_unix_ms = static_cast<std::int64_t>(extract_json_number(line, "updated_unix_ms")),
    };
}

[[nodiscard]] std::string trim_copy(const std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

[[nodiscard]] std::optional<std::string> explicit_memory_text(const std::string_view prompt) {
    auto cleaned = trim_copy(prompt);
    if (cleaned.size() < 4U || cleaned.size() > 2048U) {
        return std::nullopt;
    }
    auto lowered = cleaned;
    std::ranges::transform(lowered, lowered.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    for (const auto marker : {
             "password", "passcode", "api key", "secret key", "access token",
             "credit card", "social security", "private key", "recovery phrase"}) {
        if (lowered.find(marker) != std::string::npos) {
            return std::nullopt;
        }
    }
    for (const auto prefix : {
             std::string_view{"remember that "}, std::string_view{"remember: "},
             std::string_view{"please remember "}, std::string_view{"my name is "},
             std::string_view{"call me "}, std::string_view{"i prefer "},
             std::string_view{"i always want "}, std::string_view{"i don't want "},
             std::string_view{"i do not want "}}) {
        if (lowered.starts_with(prefix)) {
            if (prefix == "remember that " || prefix == "remember: " ||
                prefix == "please remember ") {
                const auto remove_that =
                    prefix == "please remember " &&
                    lowered.substr(prefix.size()).starts_with("that ");
                cleaned.erase(0U, prefix.size() + (remove_that ? 5U : 0U));
            }
            while (!cleaned.empty() &&
                   std::string_view(" .!\t\r\n").find(cleaned.back()) != std::string_view::npos) {
                cleaned.pop_back();
            }
            return cleaned.empty() ? std::nullopt : std::optional<std::string>{std::move(cleaned)};
        }
    }
    return std::nullopt;
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
    provisional_prompt_keys_.clear();
    positive_prompt_keys_.clear();
    retrievable_episodes_.clear();
    exact_prompt_index_.clear();
    term_postings_.clear();
    accepted_counts_.clear();
    human_accepted_counts_.clear();
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
    const auto hard_correction = episode.human_rejected && !episode.correction.empty() &&
        episode.provenance == "interactive-hard-correction";
    const auto independently_validated = episode.human_accepted || episode.deterministic_verified ||
                                         episode.reference_verified;
    const auto key = prompt_key(episode);
    const auto normalized_episode_skill = normalized_prompt(episode.skill);
    const auto normalized_episode_prompt = normalized_prompt(episode.prompt);
    const auto already_human_positive = std::ranges::any_of(
        retrievable_episodes_,
        [&](const Episode& existing) {
            return existing.human_accepted &&
                   normalized_prompt(existing.skill) == normalized_episode_skill &&
                   normalized_prompt(existing.prompt) == normalized_episode_prompt;
        });
    const auto already_positive = positive_prompt_keys_.contains(key);
    if (!independently_validated && !episode.human_rejected && !episode.prompt.empty()) {
        provisional_prompt_keys_.insert(key);
    }
    if (independently_validated && !episode.human_rejected) {
        positive_prompt_keys_.insert(key);
        ++accepted_counts_[episode.skill];
        if (episode.human_accepted) {
            ++human_accepted_counts_[episode.skill];
        }
    }
    if (hard_correction) {
        positive_prompt_keys_.insert(key);
        if (!already_positive) {
            ++accepted_counts_[episode.skill];
        }
        if (!already_human_positive) {
            ++human_accepted_counts_[episode.skill];
        }
    }
    if (episode.eligible_for_training && (!episode.human_rejected || hard_correction)) {
        ++training_counts_[episode.skill];
    }
    if ((!independently_validated && !hard_correction) || episode.prompt.empty() ||
        (episode.response.empty() && !hard_correction)) {
        return;
    }

    auto retrievable = episode;
    if (hard_correction) {
        retrievable.response = episode.correction;
        retrievable.human_accepted = true;
        retrievable.human_rejected = false;
        retrievable.score = 1.0;
        retrievable.knowledge_destination = "corrected-retrieval-memory";
    }
    const auto index = retrievable_episodes_.size();
    retrievable_episodes_.push_back(std::move(retrievable));
    const auto& indexed = retrievable_episodes_.back();
    const auto exact_key = normalized_prompt(indexed.prompt);
    if (!exact_key.empty()) {
        const auto existing = exact_prompt_index_.find(exact_key);
        if (hard_correction || existing == exact_prompt_index_.end() ||
            retrievable_episodes_[existing->second].score < indexed.score) {
            exact_prompt_index_[exact_key] = index;
        }
    }
    for (const auto& term : terms(indexed.prompt + ' ' + indexed.response)) {
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
    const auto independently_validated = episode.human_accepted || episode.deterministic_verified ||
                                         episode.reference_verified;
    if (independently_validated && !episode.human_rejected &&
        positive_prompt_keys_.contains(prompt_key(episode))) {
        return false;
    }
    if (!independently_validated && !episode.human_rejected &&
        provisional_prompt_keys_.contains(prompt_key(episode))) {
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

bool EpisodeStore::contains_positive_prompt(
    const std::string_view skill,
    const std::string_view prompt) const {
    std::lock_guard lock(mutex_);
    return positive_prompt_keys_.contains(
        normalized_prompt(skill) + '\n' + normalized_prompt(prompt));
}

bool EpisodeStore::contains_human_accepted_prompt(
    const std::string_view skill,
    const std::string_view prompt) const {
    std::lock_guard lock(mutex_);
    const auto normalized_skill = normalized_prompt(skill);
    const auto normalized_value = normalized_prompt(prompt);
    return std::ranges::any_of(retrievable_episodes_, [&](const Episode& episode) {
        return episode.human_accepted &&
               normalized_prompt(episode.skill) == normalized_skill &&
               normalized_prompt(episode.prompt) == normalized_value;
    });
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
        const auto authoritative = exact_prompt_index_.find(normalized_prompt(episode.prompt));
        if (authoritative != exact_prompt_index_.end() &&
            authoritative->second != index &&
            authoritative->second < retrievable_episodes_.size() &&
            retrievable_episodes_[authoritative->second].provenance ==
                "interactive-hard-correction") {
            continue;
        }
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

std::optional<RetrievedEpisode> EpisodeStore::exact_verified(
    const std::string_view query) const {
    std::lock_guard lock(mutex_);
    const auto iterator = exact_prompt_index_.find(normalized_prompt(query));
    if (iterator == exact_prompt_index_.end() ||
        iterator->second >= retrievable_episodes_.size()) {
        return std::nullopt;
    }
    return RetrievedEpisode{
        .episode = retrievable_episodes_[iterator->second],
        .relevance = 1.0,
    };
}

std::optional<RetrievedEpisode> EpisodeStore::verified_match(
    const std::string_view query,
    const std::string_view expected_skill) const {
    std::lock_guard lock(mutex_);
    const auto normalized_query = normalized_prompt(query);
    if (normalized_query.empty()) {
        return std::nullopt;
    }

    const Episode* exact = nullptr;
    for (const auto& episode : retrievable_episodes_) {
        if (compatible_verified_skill(episode.skill, expected_skill) &&
            normalized_prompt(episode.prompt) == normalized_query &&
            (exact == nullptr || exact->score < episode.score)) {
            exact = &episode;
        }
    }
    if (exact != nullptr) {
        return RetrievedEpisode{.episode = *exact, .relevance = 1.0};
    }

    if (expected_skill != "english-conversation" || !safe_conversation_lookup(query)) {
        return std::nullopt;
    }
    const auto query_words = prompt_words(normalized_query);
    if (query_words.empty() || query_words.size() > 6U) {
        return std::nullopt;
    }

    const Episode* best = nullptr;
    std::string best_prompt;
    std::size_t best_distance = 3U;
    bool ambiguous = false;
    for (const auto& episode : retrievable_episodes_) {
        if (episode.skill != expected_skill) {
            continue;
        }
        const auto candidate_prompt = normalized_prompt(episode.prompt);
        if (candidate_prompt == normalized_query) {
            continue;
        }
        const auto candidate_words = prompt_words(candidate_prompt);
        if (candidate_words.size() != query_words.size()) {
            continue;
        }
        std::size_t differing_words = 0U;
        std::size_t distance = 0U;
        bool has_anchor = query_words.size() == 1U;
        for (std::size_t index = 0; index < query_words.size(); ++index) {
            if (query_words[index] == candidate_words[index]) {
                has_anchor = has_anchor || query_words[index].size() >= 2U;
                continue;
            }
            ++differing_words;
            if (differing_words > 1U || query_words[index].size() < 4U ||
                candidate_words[index].size() < 4U) {
                distance = 3U;
                break;
            }
            const auto word_limit = query_words.size() == 1U ? 1U : 2U;
            distance = bounded_edit_distance(query_words[index], candidate_words[index], word_limit);
            if (distance > word_limit) {
                break;
            }
        }
        if (differing_words != 1U || distance == 0U || distance > 2U || !has_anchor) {
            continue;
        }
        if (distance < best_distance) {
            best = &episode;
            best_prompt = candidate_prompt;
            best_distance = distance;
            ambiguous = false;
        } else if (distance == best_distance && candidate_prompt != best_prompt) {
            ambiguous = true;
        } else if (distance == best_distance && candidate_prompt == best_prompt &&
                   best != nullptr && best->score < episode.score) {
            best = &episode;
        }
    }
    if (best == nullptr || ambiguous) {
        return std::nullopt;
    }
    return RetrievedEpisode{
        .episode = *best,
        .relevance = best_distance == 1U ? 0.94 : 0.88,
    };
}

std::size_t EpisodeStore::count_for_skill(const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    const auto iterator = accepted_counts_.find(std::string(skill));
    return iterator == accepted_counts_.end() ? 0U : iterator->second;
}

std::size_t EpisodeStore::human_acceptance_count_for_skill(
    const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    const auto iterator = human_accepted_counts_.find(std::string(skill));
    return iterator == human_accepted_counts_.end() ? 0U : iterator->second;
}

std::size_t EpisodeStore::training_count_for_skill(const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    const auto iterator = training_counts_.find(std::string(skill));
    return iterator == training_counts_.end() ? 0U : iterator->second;
}

std::filesystem::path EpisodeStore::path() const {
    return path_;
}

UserMemoryStore::UserMemoryStore(std::filesystem::path path) : path_(std::move(path)) {
    if (!path_.parent_path().empty()) {
        std::filesystem::create_directories(path_.parent_path());
    }
    std::lock_guard lock(mutex_);
    load_unlocked();
}

void UserMemoryStore::load_unlocked() {
    items_.clear();
    auto source = path_;
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    if (!std::filesystem::is_regular_file(source) && std::filesystem::is_regular_file(backup)) {
        source = backup;
    }
    if (!std::filesystem::is_regular_file(source)) {
        return;
    }
    std::ifstream stream(source, std::ios::binary);
    std::string line;
    while (std::getline(stream, line)) {
        auto item = parse_user_memory(line);
        if (!item.id.empty() && !item.text.empty()) {
            items_.push_back(std::move(item));
        }
    }
    std::ranges::sort(items_, std::greater{}, &UserMemoryItem::updated_unix_ms);
}

void UserMemoryStore::persist_unlocked() const {
    const auto temporary = std::filesystem::path(path_.string() + ".tmp");
    const auto backup = std::filesystem::path(path_.string() + ".bak");
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to write permanent user memory: " + path_.string());
    }
    for (const auto& item : items_) {
        stream << '{'
               << "\"id\":\"" << escape_json(item.id) << "\","
               << "\"text\":\"" << escape_json(item.text) << "\","
               << "\"source\":\"" << escape_json(item.source) << "\","
               << "\"pinned\":" << (item.pinned ? "true" : "false") << ','
               << "\"created_unix_ms\":" << item.created_unix_ms << ','
               << "\"updated_unix_ms\":" << item.updated_unix_ms
               << "}\n";
    }
    stream.flush();
    if (!stream) {
        throw std::runtime_error("unable to flush permanent user memory: " + path_.string());
    }
    stream.close();

    std::error_code error;
    std::filesystem::remove(backup, error);
    error.clear();
    if (std::filesystem::exists(path_)) {
        std::filesystem::rename(path_, backup, error);
        if (error) {
            throw std::runtime_error("unable to protect previous user memory: " + error.message());
        }
    }
    error.clear();
    std::filesystem::rename(temporary, path_, error);
    if (error) {
        std::error_code rollback_error;
        if (std::filesystem::is_regular_file(backup)) {
            std::filesystem::rename(backup, path_, rollback_error);
        }
        throw std::runtime_error("unable to publish permanent user memory: " + error.message());
    }
    std::filesystem::remove(backup, error);
}

std::vector<UserMemoryItem> UserMemoryStore::items() const {
    std::lock_guard lock(mutex_);
    return items_;
}

UserMemoryItem UserMemoryStore::upsert(UserMemoryItem item) {
    item.text = trim_copy(item.text);
    if (item.text.empty() || item.text.size() > 2048U) {
        throw std::invalid_argument("user memory must contain between 1 and 2048 characters");
    }
    std::lock_guard lock(mutex_);
    const auto normalized = normalized_prompt(item.text);
    auto existing = item.id.empty()
        ? items_.end()
        : std::ranges::find(items_, item.id, &UserMemoryItem::id);
    if (existing == items_.end()) {
        existing = std::ranges::find_if(items_, [&](const UserMemoryItem& candidate) {
            return normalized_prompt(candidate.text) == normalized;
        });
    }
    const auto timestamp = now_unix_ms();
    if (existing != items_.end()) {
        existing->text = std::move(item.text);
        existing->source = item.source.empty() ? existing->source : std::move(item.source);
        existing->pinned = item.pinned || existing->pinned;
        existing->updated_unix_ms = timestamp;
        item = *existing;
    } else {
        item.id = "user-memory-" + make_content_hash("user-memory", normalized, "").substr(0U, 16U);
        item.source = item.source.empty() ? "user-memory-editor" : std::move(item.source);
        item.created_unix_ms = timestamp;
        item.updated_unix_ms = timestamp;
        items_.push_back(item);
    }
    std::ranges::sort(items_, std::greater{}, &UserMemoryItem::updated_unix_ms);
    persist_unlocked();
    return item;
}

std::optional<UserMemoryItem> UserMemoryStore::remember_from_prompt(
    const std::string_view prompt) {
    const auto text = explicit_memory_text(prompt);
    if (!text.has_value()) {
        return std::nullopt;
    }
    return upsert(UserMemoryItem{
        .text = *text,
        .source = "automatic-explicit-memory",
        .pinned = false,
    });
}

bool UserMemoryStore::forget(const std::string_view id) {
    std::lock_guard lock(mutex_);
    const auto before = items_.size();
    std::erase_if(items_, [id](const UserMemoryItem& item) { return item.id == id; });
    if (items_.size() == before) {
        return false;
    }
    persist_unlocked();
    return true;
}

std::string UserMemoryStore::context_for(
    const std::string_view query,
    const std::size_t limit) const {
    std::lock_guard lock(mutex_);
    if (limit == 0U || items_.empty()) {
        return {};
    }
    const auto query_terms = terms(query);
    struct Ranked final {
        const UserMemoryItem* item;
        std::size_t overlap;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(items_.size());
    for (const auto& item : items_) {
        const auto item_terms = terms(item.text);
        const auto overlap = static_cast<std::size_t>(std::ranges::count_if(
            query_terms,
            [&item_terms](const std::string& term) { return item_terms.contains(term); }));
        ranked.push_back({.item = &item, .overlap = overlap});
    }
    std::ranges::stable_sort(ranked, [](const Ranked& left, const Ranked& right) {
        if (left.item->pinned != right.item->pinned) {
            return left.item->pinned;
        }
        if (left.overlap != right.overlap) {
            return left.overlap > right.overlap;
        }
        return left.item->updated_unix_ms > right.item->updated_unix_ms;
    });
    if (ranked.size() > limit) {
        ranked.resize(limit);
    }
    std::ostringstream output;
    output << "Permanent user memory (user-managed context; never model-weight training):\n";
    for (const auto& entry : ranked) {
        output << "- " << entry.item->text << '\n';
    }
    output << "Use only when relevant. Current instructions override memory.";
    return output.str();
}

std::filesystem::path UserMemoryStore::path() const {
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
