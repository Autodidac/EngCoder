#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
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

struct StorageSnapshot final {
    std::filesystem::path root;
    std::uintmax_t total_bytes{0};
    std::size_t files{0};
    std::size_t generation_directories{0};
    std::size_t trajectory_directories{0};
    std::int64_t sampled_unix_ms{0};
    std::vector<StorageCategory> categories;
    std::vector<StorageFile> largest_files;
};

[[nodiscard]] StorageSnapshot inspect_storage(
    const std::filesystem::path& root,
    std::size_t largest_file_count = 8U,
    std::stop_token stop_token = {}) noexcept;

} // namespace threadspire::ai::runtime
