module;
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module threadspire.storage;

export {
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

namespace threadspire::ai::runtime {

struct StorageCategory final {
    std::string name;
    std::filesystem::path path;
    std::uintmax_t bytes{0};
    std::size_t files{0};
};

struct StorageFile final {
    std::filesystem::path relative_path;
    std::uintmax_t bytes{0};
};

struct StorageDirectory final {
    std::filesystem::path relative_path;
    std::uintmax_t bytes{0};
    std::size_t files{0};
};

struct StorageCard final {
    std::string key;
    std::string label;
    std::uintmax_t bytes{0};
    std::intmax_t growth_bytes{0};
    std::size_t files{0};
};

struct PackSizeProjection final {
    std::string id;
    std::string name;
    std::uintmax_t download_bytes{0};
    std::uintmax_t install_bytes{0};
    bool installed{false};
};

struct StorageQuota final {
    std::string name;
    std::filesystem::path relative_path;
    std::uintmax_t maximum_bytes{0};
    std::size_t maximum_age_days{0};
    std::size_t minimum_entries_to_keep{0};
};

struct StorageCleanupItem final {
    std::filesystem::path relative_path;
    std::uintmax_t bytes{0};
};

struct StorageCleanupResult final {
    bool succeeded{false};
    bool dry_run{true};
    std::uintmax_t bytes_reclaimed{0};
    std::vector<StorageCleanupItem> removed;
    std::string error;
};

struct StorageSnapshot final {    std::filesystem::path root;
    std::uintmax_t total_bytes{0};
    std::uintmax_t free_disk_bytes{0};
    std::intmax_t growth_bytes{0};
    std::size_t files{0};
    std::uintmax_t training_data_bytes{0};
    std::size_t training_data_files{0};
    std::uintmax_t active_model_bytes{0};
    std::size_t active_model_files{0};
    std::uintmax_t runtime_bytes{0};
    std::string active_generation;
    std::size_t generation_directories{0};
    std::size_t trajectory_directories{0};
    std::int64_t sampled_unix_ms{0};
    std::vector<StorageCategory> categories;
    std::vector<StorageCard> cards;
    std::vector<StorageFile> largest_files;
    std::vector<StorageDirectory> largest_directories;
    std::vector<PackSizeProjection> pack_projections;
    std::vector<StorageQuota> quotas;
};

[[nodiscard]] StorageSnapshot inspect_storage(
    const std::filesystem::path& root,
    std::size_t largest_file_count = 8U,
    std::stop_token stop_token = {}) noexcept;
[[nodiscard]] StorageSnapshot inspect_storage_with_growth(
    const std::filesystem::path& root,
    std::optional<std::uintmax_t> previous_total_bytes,
    std::size_t largest_file_count = 8U,
    std::size_t largest_directory_count = 8U,
    std::stop_token stop_token = {}) noexcept;
[[nodiscard]] std::vector<StorageQuota> default_storage_quotas();
[[nodiscard]] StorageCleanupResult cleanup_storage(
    const std::filesystem::path& root,
    const std::vector<StorageQuota>& quotas,
    bool dry_run = true,
    std::stop_token stop_token = {}) noexcept;

} // namespace threadspire::ai::runtime
}
