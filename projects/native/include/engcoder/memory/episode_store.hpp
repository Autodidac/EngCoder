#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

namespace engcoder::memory {

using json = nlohmann::json;

struct Episode {
    std::string id;
    std::string skill;
    std::string prompt;
    std::string response;
    std::string source;
    std::string content_hash;
    double confidence{1.0};
    bool human_accepted{};
    bool deterministic_verified{};
    bool reference_verified{};
    bool eligible_for_training{};
    std::int64_t created_unix_ms{};
};

struct RetrievedEpisode {
    Episode episode;
    double relevance{};
};

class EpisodeStore {
public:
    explicit EpisodeStore(std::filesystem::path path);

    [[nodiscard]] bool append_if_new(Episode episode);
    [[nodiscard]] std::vector<RetrievedEpisode> retrieve(
        std::string_view query,
        std::size_t limit = 3,
        std::string_view preferred_skill = {}) const;
    [[nodiscard]] std::vector<Episode> recent(std::size_t limit = 20) const;
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::size_t training_eligible_count() const;
    [[nodiscard]] bool erase_hash(std::string_view content_hash);
    [[nodiscard]] std::size_t erase_matching(std::string_view prompt, std::string_view response);
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] static std::string make_content_hash(
        std::string_view skill,
        std::string_view prompt,
        std::string_view response);
    [[nodiscard]] static std::int64_t now_unix_ms();

private:
    void load();
    void index_episode(const Episode& episode);
    bool rewrite_locked();
    void rebuild_indexes_locked();

    std::filesystem::path path_;
    mutable std::mutex mutex_;
    std::vector<Episode> episodes_;
    std::unordered_set<std::string> hashes_;
    std::unordered_map<std::string, std::vector<std::size_t>> postings_;
    std::size_t training_eligible_{};
};

[[nodiscard]] json to_json(const Episode& episode);

} // namespace engcoder::memory
