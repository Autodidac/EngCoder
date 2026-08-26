#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

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

struct StorageSnapshot final {
    std::filesystem::path root;
    std::uintmax_t total_bytes{0};
    std::uintmax_t free_disk_bytes{0};
    std::intmax_t growth_bytes{0};
    std::size_t files{0};
    std::uintmax_t training_data_bytes{0};
    std::size_t training_data_files{0};
    std::uintmax_t active_model_bytes{0};
    std::size_t active_model_files{0};
    std::uintmax_t active_weights_bytes{0};
    std::uint64_t active_parameter_count{0};
    std::uint64_t active_tokens_seen{0};
    std::size_t active_training_steps{0};
    std::size_t active_context_length{0};
    std::size_t active_layer_count{0};
    std::string active_training_scope;
    std::string active_training_data_scope;
    std::string active_origin;
    std::vector<std::string> active_skills;
    std::uintmax_t all_generation_bytes{0};
    std::uintmax_t inactive_generation_bytes{0};
    std::uintmax_t runtime_bytes{0};
    std::uintmax_t training_pack_bytes{0};
    std::uintmax_t pytorch_bytes{0};
    std::uintmax_t pytorch_native_libraries_bytes{0};
    std::uintmax_t pytorch_code_bytes{0};
    std::uintmax_t private_runtime_other_bytes{0};
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
