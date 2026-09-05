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
import threadspire.core;

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

[[nodiscard]] Episode parse_episode(const std::string_view line) {
    std::string error;
    const auto document = core::json::try_parse(line, error);
    if (!document.has_value()) {
        return {};
    }
    Episode episode;
    episode.id = core::json::string_or(document->find("id"));
    episode.skill = core::json::string_or(document->find("skill"));
    episode.prompt = core::json::string_or(document->find("prompt"));
    episode.response = core::json::string_or(document->find("response"));
    episode.model_id = core::json::string_or(document->find("model_id"));
    episode.score = core::json::number_or(document->find("score"));
    episode.compiler_passed = core::json::bool_or(document->find("compiler_passed"), false);
    episode.tests_passed = core::json::bool_or(document->find("tests_passed"), false);
    episode.human_accepted = core::json::bool_or(document->find("human_accepted"), false);
    episode.human_rejected = core::json::bool_or(document->find("human_rejected"), false);
    episode.deterministic_verified = core::json::bool_or(
        document->find("deterministic_verified"), false);
    episode.reference_verified = core::json::bool_or(
        document->find("reference_verified"), false);
    episode.self_generated = core::json::bool_or(document->find("self_generated"), false);
    episode.eligible_for_training = core::json::bool_or(
        document->find("eligible_for_training"), false);
    episode.verifier_model_id = core::json::string_or(document->find("verifier_model_id"));
    episode.verification_notes = core::json::string_or(document->find("verification_notes"));
    episode.provenance = core::json::string_or(document->find("provenance"));
    episode.knowledge_destination = core::json::string_or(
        document->find("knowledge_destination"));
    episode.content_hash = core::json::string_or(document->find("content_hash"));
    episode.correction = core::json::string_or(document->find("correction"));
    episode.tool_trace_hash = core::json::string_or(document->find("tool_trace_hash"));
    episode.created_unix_ms = static_cast<std::int64_t>(
        core::json::number_or(document->find("created_unix_ms")));
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

void append_expanded_token(
    std::vector<std::string>& result,
    std::string token) {
    const auto append_pair = [&result](
                                 const std::string_view first,
                                 const std::string_view second) {
        result.emplace_back(first);
        result.emplace_back(second);
    };
    if (token == "what's" || token == "whats") {
        append_pair("what", "is");
    } else if (token == "who's" || token == "whos") {
        append_pair("who", "is");
    } else if (token == "where's" || token == "wheres") {
        append_pair("where", "is");
    } else if (token == "when's" || token == "whens") {
        append_pair("when", "is");
    } else if (token == "why's" || token == "whys") {
        append_pair("why", "is");
    } else if (token == "how's" || token == "hows") {
        append_pair("how", "is");
    } else if (token == "i'm" || token == "im") {
        append_pair("i", "am");
    } else if (token == "you're" || token == "youre") {
        append_pair("you", "are");
    } else if (token == "we're") {
        append_pair("we", "are");
    } else if (token == "they're" || token == "theyre") {
        append_pair("they", "are");
    } else if (token == "it's") {
        append_pair("it", "is");
    } else if (token == "that's" || token == "thats") {
        append_pair("that", "is");
    } else if (token == "there's" || token == "theres") {
        append_pair("there", "is");
    } else if (token == "can't" || token == "cant") {
        result.emplace_back("cannot");
    } else if (token == "won't" || token == "wont") {
        append_pair("will", "not");
    } else if (token == "don't" || token == "dont") {
        append_pair("do", "not");
    } else if (token == "doesn't" || token == "doesnt") {
        append_pair("does", "not");
    } else if (token == "didn't" || token == "didnt") {
        append_pair("did", "not");
    } else if (token == "isn't" || token == "isnt") {
        append_pair("is", "not");
    } else if (token == "aren't" || token == "arent") {
        append_pair("are", "not");
    } else if (token == "shouldn't" || token == "shouldnt") {
        append_pair("should", "not");
    } else if (token == "wouldn't" || token == "wouldnt") {
        append_pair("would", "not");
    } else if (token == "couldn't" || token == "couldnt") {
        append_pair("could", "not");
    } else {
        std::erase(token, '\'');
        if (!token.empty()) {
            result.push_back(std::move(token));
        }
    }
}

[[nodiscard]] std::string preserved_literal(const std::string_view value) {
    auto result = std::string{"literal_"};
    bool pending_separator = false;
    for (const unsigned char character : value) {
        if (std::isalnum(character) != 0) {
            if (pending_separator && result.size() > 8U) {
                result.push_back('_');
            }
            result.push_back(static_cast<char>(std::tolower(character)));
            pending_separator = false;
        } else if (result.size() > 8U) {
            pending_separator = true;
        }
    }
    return result.size() == 8U ? std::string{"literal"} : result;
}

[[nodiscard]] std::vector<std::string> canonical_tokens(
    const std::string_view value,
    const bool replace_slots) {
    std::vector<std::string> result;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto character = static_cast<unsigned char>(value[position]);
        const auto embedded_apostrophe =
            character == static_cast<unsigned char>('\'') && position > 0U &&
            position + 1U < value.size() &&
            std::isalpha(static_cast<unsigned char>(value[position - 1U])) != 0 &&
            std::isalpha(static_cast<unsigned char>(value[position + 1U])) != 0;
        if (character == static_cast<unsigned char>('"') ||
            character == static_cast<unsigned char>('`') ||
            (character == static_cast<unsigned char>('\'') && !embedded_apostrophe)) {
            const auto closing = value.find(static_cast<char>(character), position + 1U);
            if (closing != std::string_view::npos) {
                result.push_back(replace_slots
                    ? std::string{"<literal>"}
                    : preserved_literal(value.substr(position + 1U, closing - position - 1U)));
                position = closing + 1U;
                continue;
            }
        }
        if (std::isdigit(character) != 0) {
            const auto begin = position;
            while (position < value.size()) {
                const auto current = static_cast<unsigned char>(value[position]);
                if (std::isalnum(current) == 0 && current != '.' && current != '_') {
                    break;
                }
                ++position;
            }
            if (replace_slots) {
                result.emplace_back("<number>");
            } else {
                auto number = std::string(value.substr(begin, position - begin));
                std::ranges::transform(number, number.begin(), [](const unsigned char current) {
                    return static_cast<char>(std::tolower(current));
                });
                result.push_back(std::move(number));
            }
            continue;
        }
        if (std::isalpha(character) != 0 || character == '_' || character == '+') {
            std::string token;
            while (position < value.size()) {
                const auto current = static_cast<unsigned char>(value[position]);
                const auto apostrophe =
                    current == static_cast<unsigned char>('\'') && position > 0U &&
                    position + 1U < value.size() &&
                    std::isalpha(static_cast<unsigned char>(value[position - 1U])) != 0 &&
                    std::isalpha(static_cast<unsigned char>(value[position + 1U])) != 0;
                if (std::isalpha(current) == 0 && current != '_' && current != '+' &&
                    !apostrophe) {
                    break;
                }
                token.push_back(static_cast<char>(std::tolower(current)));
                ++position;
            }
            append_expanded_token(result, std::move(token));
            continue;
        }
        ++position;
    }
    return result;
}

[[nodiscard]] std::string join_tokens(const std::vector<std::string>& tokens) {
    std::string result;
    for (const auto& token : tokens) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += token;
    }
    return result;
}

[[nodiscard]] bool has_constraint(const std::vector<std::string>& tokens) {
    return std::ranges::any_of(tokens, [](const std::string& token) {
        return token == "must" || token == "required" || token == "require" ||
               token == "only" || token == "exactly" || token == "minimum" ||
               token == "maximum" || token == "least" || token == "most" ||
               token == "under" || token == "over" || token == "before" ||
               token == "after" || token == "without" || token == "never" ||
               token == "cannot" || token == "should" || token == "shall" ||
               token == "avoid" || token == "exclude" || token == "include" ||
               token == "forbidden" || token == "allowed";
    });
}

[[nodiscard]] std::string response_polarity(const std::vector<std::string>& tokens) {
    const auto negative = std::ranges::any_of(tokens, [](const std::string& token) {
        return token == "no" || token == "not" || token == "never" ||
               token == "cannot" || token == "false" || token == "literal_false" ||
               token == "incorrect" || token == "wrong" || token == "forbidden" ||
               token == "disallowed" || token == "without";
    });
    const auto affirmative = std::ranges::any_of(tokens, [](const std::string& token) {
        return token == "yes" || token == "true" || token == "literal_true" ||
               token == "correct" || token == "allowed" || token == "supported" ||
               token == "affirmative";
    });
    if (negative && affirmative) {
        return "mixed";
    }
    if (negative) {
        return "negative";
    }
    return affirmative ? "affirmative" : "neutral";
}

[[nodiscard]] std::string canonical_behavior_prompt(
    const std::string_view skill,
    const std::string_view prompt) {
    const auto slotted_tokens = canonical_tokens(prompt, true);
    auto canonical_prompt = join_tokens(slotted_tokens);
    if (canonical_prompt == "hi" || canonical_prompt == "hello" ||
        canonical_prompt == "hey" || canonical_prompt == "hey there" ||
        canonical_prompt == "howdy") {
        canonical_prompt = "intent greeting";
    } else if (canonical_prompt == "what is up" ||
               canonical_prompt == "what is going on" ||
               canonical_prompt == "how is it going" ||
               canonical_prompt == "how are you") {
        canonical_prompt = "intent casual status greeting";
    } else if (canonical_prompt == "thanks" || canonical_prompt == "thank you" ||
               canonical_prompt == "many thanks" ||
               canonical_prompt == "thank you very much") {
        canonical_prompt = "intent gratitude";
    } else if (canonical_prompt == "bye" || canonical_prompt == "goodbye" ||
               canonical_prompt == "farewell" || canonical_prompt == "see you" ||
               canonical_prompt == "talk to you later") {
        canonical_prompt = "intent farewell";
    }
    auto result = normalized_prompt(skill) + '\n' + canonical_prompt;
    return result;
}

void decrement_behavior(
    std::unordered_map<std::string, std::size_t>& refcounts,
    const std::string& behavior) {
    const auto existing = refcounts.find(behavior);
    if (existing != refcounts.end() && --existing->second == 0U) {
        refcounts.erase(existing);
    }
}

[[nodiscard]] std::size_t behavior_count_for_skill(
    const std::unordered_map<std::string, std::size_t>& refcounts,
    const std::string_view skill) {
    const auto prefix = normalized_prompt(skill) + '\n';
    return static_cast<std::size_t>(std::ranges::count_if(
        refcounts,
        [&](const auto& item) {
            return item.first.starts_with(prefix);
        }));
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
    std::string error;
    const auto document = core::json::try_parse(line, error);
    if (!document.has_value()) {
        return {};
    }
    return UserMemoryItem{
        .id = core::json::string_or(document->find("id")),
        .text = core::json::string_or(document->find("text")),
        .source = core::json::string_or(document->find("source")),
        .pinned = core::json::bool_or(document->find("pinned"), false),
        .created_unix_ms = static_cast<std::int64_t>(
            core::json::number_or(document->find("created_unix_ms"))),
        .updated_unix_ms = static_cast<std::int64_t>(
            core::json::number_or(document->find("updated_unix_ms"))),
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

[[nodiscard]] std::string make_behavior_signature(
    const std::string_view skill,
    const std::string_view prompt,
    const std::string_view response) {
    auto result = canonical_behavior_prompt(skill, prompt);
    const auto prompt_tokens = canonical_tokens(prompt, true);
    if (has_constraint(prompt_tokens)) {
        result += "\nanswer-polarity:" + response_polarity(canonical_tokens(response, false));
    }
    return result;
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
    const auto effective_response = hard_correction ? std::string_view{episode.correction}
                                                    : std::string_view{episode.response};
    const auto behavior = make_behavior_signature(
        episode.skill,
        episode.prompt,
        effective_response);
    if (hard_correction) {
        const auto normalized_episode_prompt = normalized_prompt(episode.prompt);
        const auto same_prompt = [&](const Episode& existing) {
            return normalized_prompt(existing.skill) == normalized_episode_skill &&
                   normalized_prompt(existing.prompt) == normalized_episode_prompt;
        };
        const auto remove_superseded = [&](const Episode& existing) {
            const auto superseded_behavior = make_behavior_signature(
                existing.skill,
                existing.prompt,
                existing.response);
            decrement_behavior(accepted_counts_, superseded_behavior);
            if (existing.human_accepted) {
                decrement_behavior(human_accepted_counts_, superseded_behavior);
            }
            if (existing.eligible_for_training) {
                decrement_behavior(training_counts_, superseded_behavior);
            }
        };
        const auto previous_correction = std::ranges::find_if(
            retrievable_episodes_.rbegin(),
            retrievable_episodes_.rend(),
            [&](const Episode& existing) {
                return same_prompt(existing) &&
                       existing.provenance == "interactive-hard-correction";
            });
        if (previous_correction != retrievable_episodes_.rend()) {
            remove_superseded(*previous_correction);
        } else {
            for (const auto& existing : retrievable_episodes_) {
                if (same_prompt(existing)) {
                    remove_superseded(existing);
                }
            }
        }
    }
    if (!independently_validated && !episode.human_rejected && !episode.prompt.empty()) {
        provisional_prompt_keys_.insert(key);
    }
    if (independently_validated && !episode.human_rejected) {
        positive_prompt_keys_.insert(key);
        ++accepted_counts_[behavior];
        if (episode.human_accepted) {
            ++human_accepted_counts_[behavior];
        }
    }
    if (hard_correction) {
        positive_prompt_keys_.insert(key);
        ++accepted_counts_[behavior];
        ++human_accepted_counts_[behavior];
    }
    if (episode.eligible_for_training && (!episode.human_rejected || hard_correction)) {
        ++training_counts_[behavior];
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
    const auto behavior_prompt = canonical_behavior_prompt(skill, prompt);
    return std::ranges::any_of(retrievable_episodes_, [&](const Episode& episode) {
        return episode.human_accepted &&
               canonical_behavior_prompt(episode.skill, episode.prompt) == behavior_prompt;
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

    const auto authoritative = exact_prompt_index_.find(normalized_query);
    if (authoritative != exact_prompt_index_.end() &&
        authoritative->second < retrievable_episodes_.size()) {
        const auto& episode = retrievable_episodes_[authoritative->second];
        if (compatible_verified_skill(episode.skill, expected_skill)) {
            return RetrievedEpisode{.episode = episode, .relevance = 1.0};
        }
    }

    const Episode* exact = nullptr;
    for (const auto& episode : retrievable_episodes_) {
        const auto hard_correction = episode.provenance == "interactive-hard-correction";
        const auto exact_is_hard_correction = exact != nullptr &&
            exact->provenance == "interactive-hard-correction";
        if (compatible_verified_skill(episode.skill, expected_skill) &&
            normalized_prompt(episode.prompt) == normalized_query &&
            (exact == nullptr ||
             (hard_correction && !exact_is_hard_correction) ||
             (hard_correction == exact_is_hard_correction && exact->score <= episode.score))) {
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
        const auto candidate_authoritative = exact_prompt_index_.find(candidate_prompt);
        if (candidate_authoritative != exact_prompt_index_.end() &&
            candidate_authoritative->second < retrievable_episodes_.size() &&
            &retrievable_episodes_[candidate_authoritative->second] != &episode &&
            retrievable_episodes_[candidate_authoritative->second].provenance ==
                "interactive-hard-correction") {
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
                   best != nullptr) {
            const auto hard_correction =
                episode.provenance == "interactive-hard-correction";
            const auto best_is_hard_correction =
                best->provenance == "interactive-hard-correction";
            if ((hard_correction && !best_is_hard_correction) ||
                (hard_correction == best_is_hard_correction && best->score < episode.score)) {
                best = &episode;
            }
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
    return behavior_count_for_skill(accepted_counts_, skill);
}

std::size_t EpisodeStore::human_acceptance_count_for_skill(
    const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    return behavior_count_for_skill(human_accepted_counts_, skill);
}

std::size_t EpisodeStore::total_human_acceptance_count() const {
    std::lock_guard lock(mutex_);
    return human_accepted_counts_.size();
}

std::size_t EpisodeStore::training_count_for_skill(const std::string_view skill) const {
    std::lock_guard lock(mutex_);
    return behavior_count_for_skill(training_counts_, skill);
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
