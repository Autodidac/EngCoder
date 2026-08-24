#include "threadspire/ai/runtime/storage_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <ranges>
#include <system_error>
#include <utility>

namespace threadspire::ai::runtime {
namespace {

void remember_large_file(
    StorageSnapshot& snapshot,
    const std::filesystem::path& path,
    const std::uintmax_t bytes,
    const std::size_t maximum) {
    if (maximum == 0U) {
        return;
    }
    std::error_code error;
    auto relative = std::filesystem::relative(path, snapshot.root, error);
    if (error) {
        relative = path.filename();
    }
    snapshot.largest_files.push_back({.relative_path = std::move(relative), .bytes = bytes});
    std::ranges::sort(snapshot.largest_files, std::greater{}, &StorageFile::bytes);
    if (snapshot.largest_files.size() > maximum) {
        snapshot.largest_files.resize(maximum);
    }
}

void inspect_item(
    StorageSnapshot& snapshot,
    StorageCategory& category,
    const std::filesystem::path& item,
    const std::size_t largest_file_count,
    const std::stop_token stop_token) {
    std::error_code error;
    if (std::filesystem::is_regular_file(item, error)) {
        const auto bytes = std::filesystem::file_size(item, error);
        if (!error) {
            category.bytes += bytes;
            ++category.files;
            remember_large_file(snapshot, item, bytes, largest_file_count);
        }
        return;
    }
    error.clear();
    if (!std::filesystem::is_directory(item, error)) {
        return;
    }

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::filesystem::recursive_directory_iterator iterator(item, options, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end && !stop_token.stop_requested()) {
        if (iterator->is_regular_file(error) && !error) {
            const auto bytes = iterator->file_size(error);
            if (!error) {
                category.bytes += bytes;
                ++category.files;
                remember_large_file(snapshot, iterator->path(), bytes, largest_file_count);
            }
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
}

[[nodiscard]] std::size_t directory_count(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::size_t count = 0U;
    std::filesystem::directory_iterator iterator(
        path,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        count += static_cast<std::size_t>(iterator->is_directory(error) && !error);
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return count;
}

} // namespace

StorageSnapshot inspect_storage(
    const std::filesystem::path& root,
    const std::size_t largest_file_count,
    const std::stop_token stop_token) noexcept {
    StorageSnapshot snapshot;
    snapshot.root = root;
    snapshot.sampled_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    try {
        std::error_code error;
        std::filesystem::directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::directory_iterator end;
        while (iterator != end && !stop_token.stop_requested()) {
            StorageCategory category{
                .name = iterator->path().filename().string(),
                .path = iterator->path(),
            };
            inspect_item(snapshot, category, iterator->path(), largest_file_count, stop_token);
            snapshot.total_bytes += category.bytes;
            snapshot.files += category.files;
            snapshot.categories.push_back(std::move(category));
            iterator.increment(error);
            error.clear();
        }
        std::ranges::sort(snapshot.categories, std::greater{}, &StorageCategory::bytes);
        snapshot.generation_directories = directory_count(
            root / "models" / "internal" / "generations");
        snapshot.trajectory_directories = directory_count(root / "trajectories");
    } catch (...) {
    }
    return snapshot;
}

} // namespace threadspire::ai::runtime
