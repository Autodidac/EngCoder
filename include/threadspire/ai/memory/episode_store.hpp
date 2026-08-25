#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace threadspire::ai::memory {

struct Episode final {
    std::string id;
    std::string skill;
    std::string prompt;
    std::string response;
    std::string model_id;
    double score{0.0};
    bool compiler_passed{false};
    bool tests_passed{false};
    bool human_accepted{false};
    bool human_rejected{false};
    bool deterministic_verified{false};
    bool reference_verified{false};
    bool self_generated{true};
    bool eligible_for_training{false};
    std::string verifier_model_id;
    std::string verification_notes;
    std::string provenance{"interactive"};
    std::string knowledge_destination{"episodic-memory"};
    std::string content_hash;
    std::string correction;
    std::string tool_trace_hash;
    std::int64_t created_unix_ms{0};
};

struct RetrievedEpisode final {
    Episode episode;
    double relevance{0.0};
};

class EpisodeStore final {
public:
    explicit EpisodeStore(std::filesystem::path path);

    void append(const Episode& episode);
    [[nodiscard]] bool append_if_new(Episode episode);
    [[nodiscard]] bool contains_hash(std::string_view content_hash) const;
    [[nodiscard]] bool contains_positive_prompt(
        std::string_view skill,
        std::string_view prompt) const;
    [[nodiscard]] std::vector<RetrievedEpisode> retrieve(
        std::string_view query,
        std::size_t limit = 3) const;
    [[nodiscard]] std::optional<RetrievedEpisode> exact_verified(
        std::string_view query) const;
    [[nodiscard]] std::optional<RetrievedEpisode> verified_match(
        std::string_view query,
        std::string_view expected_skill) const;
    [[nodiscard]] std::size_t count_for_skill(std::string_view skill) const;
    [[nodiscard]] std::size_t human_acceptance_count_for_skill(
        std::string_view skill) const;
    [[nodiscard]] std::size_t training_count_for_skill(std::string_view skill) const;
    [[nodiscard]] std::filesystem::path path() const;

private:
    void rebuild_index_unlocked();
    void index_episode_unlocked(const Episode& episode);
    [[nodiscard]] bool contains_hash_unlocked(std::string_view content_hash) const;

    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::unordered_set<std::string> content_hashes_;
    std::unordered_set<std::string> provisional_prompt_keys_;
    std::unordered_set<std::string> positive_prompt_keys_;
    std::vector<Episode> retrievable_episodes_;
    std::unordered_map<std::string, std::size_t> exact_prompt_index_;
    std::unordered_map<std::string, std::vector<std::size_t>> term_postings_;
    std::unordered_map<std::string, std::size_t> accepted_counts_;
    std::unordered_map<std::string, std::size_t> human_accepted_counts_;
    std::unordered_map<std::string, std::size_t> training_counts_;
};

[[nodiscard]] std::string make_episode_id();
[[nodiscard]] std::int64_t now_unix_ms();
[[nodiscard]] std::string make_content_hash(
    std::string_view skill,
    std::string_view prompt,
    std::string_view response);

} // namespace threadspire::ai::memory
