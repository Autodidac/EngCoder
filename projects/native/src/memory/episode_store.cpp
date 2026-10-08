#include "engcoder/memory/episode_store.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <ranges>
#include <set>
#include <sstream>

namespace engcoder::memory {
namespace {

[[nodiscard]] std::vector<std::string> terms(std::string_view value) {
    std::set<std::string> unique;
    std::string current;
    current.reserve(32);

    const auto flush = [&] {
        if (current.size() >= 3) unique.insert(current);
        current.clear();
    };

    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '_' || ch == '+' || ch == '#') {
            current.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            flush();
        }
    }
    flush();
    return {unique.begin(), unique.end()};
}

[[nodiscard]] Episode parse_episode(const json& value) {
    Episode episode;
    episode.id = value.value("id", std::string{});
    episode.skill = value.value("skill", std::string{"keymaker-foundation"});
    episode.prompt = value.value("prompt", std::string{});
    episode.response = value.value("response", std::string{});
    episode.source = value.value("source", std::string{});
    episode.content_hash = value.value("content_hash", std::string{});
    episode.confidence = value.value("confidence", 1.0);
    episode.human_accepted = value.value("human_accepted", false);
    episode.deterministic_verified = value.value("deterministic_verified", false);
    episode.reference_verified = value.value("reference_verified", false);
    episode.eligible_for_training = value.value("eligible_for_training", false);
    episode.created_unix_ms = value.value("created_unix_ms", std::int64_t{});
    return episode;
}

[[nodiscard]] double relevance_score(
    const Episode& episode,
    const std::vector<std::string>& query_terms,
    std::string_view preferred_skill) {
    if (query_terms.empty()) return 0.0;

    const auto episode_terms = terms(episode.prompt + " " + episode.response);
    if (episode_terms.empty()) return 0.0;

    std::size_t overlap = 0;
    for (const auto& term : query_terms) {
        if (std::ranges::find(episode_terms, term) != episode_terms.end()) ++overlap;
    }

    if (overlap == 0) return 0.0;
    const auto precision = static_cast<double>(overlap) / static_cast<double>(query_terms.size());
    const auto coverage = static_cast<double>(overlap) / static_cast<double>(episode_terms.size());
    double score = precision * 0.82 + coverage * 0.18;
    if (!preferred_skill.empty() && episode.skill == preferred_skill) score += 0.12;
    if (episode.human_accepted) score += 0.08;
    if (episode.deterministic_verified) score += 0.06;
    if (episode.reference_verified) score += 0.04;
    return std::min(1.0, score);
}

} // namespace

EpisodeStore::EpisodeStore(std::filesystem::path path) : path_(std::move(path)) {
    std::filesystem::create_directories(path_.parent_path());
    load();
}

std::int64_t EpisodeStore::now_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string EpisodeStore::make_content_hash(
    std::string_view skill,
    std::string_view prompt,
    std::string_view response) {
    // Stable FNV-1a is sufficient for duplicate suppression; this is not a
    // cryptographic integrity checksum.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto add = [&](std::string_view part) {
        for (const unsigned char ch : part) {
            hash ^= static_cast<std::uint64_t>(ch);
            hash *= 1099511628211ULL;
        }
        hash ^= 0xFFU;
        hash *= 1099511628211ULL;
    };
    add(skill);
    add(prompt);
    add(response);

    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

json to_json(const Episode& episode) {
    return json{
        {"id", episode.id},
        {"skill", episode.skill},
        {"prompt", episode.prompt},
        {"response", episode.response},
        {"source", episode.source},
        {"content_hash", episode.content_hash},
        {"confidence", episode.confidence},
        {"verified", true},
        {"human_accepted", episode.human_accepted},
        {"deterministic_verified", episode.deterministic_verified},
        {"reference_verified", episode.reference_verified},
        {"eligible_for_training", episode.eligible_for_training},
        {"created_unix_ms", episode.created_unix_ms},
    };
}

void EpisodeStore::load() {
    std::scoped_lock lock(mutex_);
    episodes_.clear();
    hashes_.clear();
    postings_.clear();
    training_eligible_ = 0;

    std::ifstream input(path_, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        try {
            auto episode = parse_episode(json::parse(line));
            if (episode.prompt.empty() || episode.response.empty()) continue;
            if (episode.content_hash.empty()) {
                episode.content_hash = make_content_hash(episode.skill, episode.prompt, episode.response);
            }
            if (!hashes_.insert(episode.content_hash).second) continue;
            index_episode(episode);
            episodes_.push_back(std::move(episode));
        } catch (...) {
            // Corrupt individual records do not make the complete memory store unusable.
        }
    }
}

void EpisodeStore::index_episode(const Episode& episode) {
    const auto index = episodes_.size();
    std::unordered_set<std::string> unique;
    for (auto& term : terms(episode.prompt + " " + episode.response)) {
        if (unique.insert(term).second) postings_[term].push_back(index);
    }
    if (episode.eligible_for_training) ++training_eligible_;
}

bool EpisodeStore::append_if_new(Episode episode) {
    if (episode.prompt.empty() || episode.response.empty()) return false;
    if (episode.skill.empty()) episode.skill = "keymaker-foundation";
    if (episode.id.empty()) episode.id = "episode-" + std::to_string(now_unix_ms());
    if (episode.created_unix_ms == 0) episode.created_unix_ms = now_unix_ms();
    if (episode.content_hash.empty()) {
        episode.content_hash = make_content_hash(episode.skill, episode.prompt, episode.response);
    }

    std::scoped_lock lock(mutex_);
    if (!hashes_.insert(episode.content_hash).second) return false;

    std::ofstream output(path_, std::ios::binary | std::ios::app);
    if (!output) {
        hashes_.erase(episode.content_hash);
        return false;
    }
    output << to_json(episode).dump() << '\n';
    if (!output) {
        hashes_.erase(episode.content_hash);
        return false;
    }

    index_episode(episode);
    episodes_.push_back(std::move(episode));
    return true;
}


void EpisodeStore::rebuild_indexes_locked() {
    hashes_.clear();
    postings_.clear();
    training_eligible_ = 0;
    for (std::size_t i = 0; i < episodes_.size(); ++i) {
        auto& episode = episodes_[i];
        if (episode.content_hash.empty()) {
            episode.content_hash = make_content_hash(episode.skill, episode.prompt, episode.response);
        }
        hashes_.insert(episode.content_hash);
        std::unordered_set<std::string> unique;
        for (auto& term : terms(episode.prompt + " " + episode.response)) {
            if (unique.insert(term).second) postings_[term].push_back(i);
        }
        if (episode.eligible_for_training) ++training_eligible_;
    }
}

bool EpisodeStore::rewrite_locked() {
    const auto temp = path_.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        for (const auto& episode : episodes_) output << to_json(episode).dump() << '\n';
        if (!output) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temp, path_, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path_, ec);
        ec.clear();
        std::filesystem::rename(temp, path_, ec);
    }
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool EpisodeStore::erase_hash(std::string_view content_hash) {
    if (content_hash.empty()) return false;
    std::scoped_lock lock(mutex_);
    const auto old_size = episodes_.size();
    std::erase_if(episodes_, [&](const Episode& episode) { return episode.content_hash == content_hash; });
    if (episodes_.size() == old_size) return false;
    if (!rewrite_locked()) return false;
    rebuild_indexes_locked();
    return true;
}

std::size_t EpisodeStore::erase_matching(std::string_view prompt, std::string_view response) {
    if (prompt.empty() && response.empty()) return 0;
    std::scoped_lock lock(mutex_);
    const auto old_size = episodes_.size();
    std::erase_if(episodes_, [&](const Episode& episode) {
        return (!prompt.empty() && episode.prompt == prompt) ||
               (!response.empty() && episode.response == response);
    });
    const auto removed = old_size - episodes_.size();
    if (removed == 0) return 0;
    if (!rewrite_locked()) return 0;
    rebuild_indexes_locked();
    return removed;
}

std::vector<RetrievedEpisode> EpisodeStore::retrieve(
    std::string_view query,
    std::size_t limit,
    std::string_view preferred_skill) const {
    const auto query_terms = terms(query);
    if (query_terms.empty() || limit == 0) return {};

    std::scoped_lock lock(mutex_);
    std::unordered_set<std::size_t> candidates;
    for (const auto& term : query_terms) {
        const auto it = postings_.find(term);
        if (it == postings_.end()) continue;
        candidates.insert(it->second.begin(), it->second.end());
    }

    std::vector<RetrievedEpisode> result;
    result.reserve(std::min(limit, candidates.size()));
    for (const auto index : candidates) {
        if (index >= episodes_.size()) continue;
        const auto score = relevance_score(episodes_[index], query_terms, preferred_skill);
        if (score < 0.18) continue;
        result.push_back(RetrievedEpisode{episodes_[index], score});
    }

    std::ranges::sort(result, [](const auto& lhs, const auto& rhs) {
        if (lhs.relevance != rhs.relevance) return lhs.relevance > rhs.relevance;
        return lhs.episode.created_unix_ms > rhs.episode.created_unix_ms;
    });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::vector<Episode> EpisodeStore::recent(std::size_t limit) const {
    std::scoped_lock lock(mutex_);
    const auto count = std::min(limit, episodes_.size());
    std::vector<Episode> result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        result.push_back(episodes_[episodes_.size() - 1 - i]);
    }
    return result;
}

std::size_t EpisodeStore::size() const {
    std::scoped_lock lock(mutex_);
    return episodes_.size();
}

std::size_t EpisodeStore::training_eligible_count() const {
    std::scoped_lock lock(mutex_);
    return training_eligible_;
}

} // namespace engcoder::memory
