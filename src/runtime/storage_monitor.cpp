module;
#include "module_prelude.hpp"

module threadspire.storage;
import threadspire.core;
import threadspire.scheduler;

namespace threadspire::ai::runtime {
namespace {

struct Measure final {
    std::uintmax_t bytes{0};
    std::size_t files{0};
};

void remember_large_file(
    StorageSnapshot& snapshot,
    const std::filesystem::path& path,
    const std::uintmax_t bytes,
    const std::size_t maximum) {
    if (maximum == 0U) {
        return;
    }
    auto relative = path.lexically_relative(snapshot.root);
    if (relative.empty() || relative.is_absolute()) {
        relative = path.filename();
    }
    snapshot.largest_files.push_back({.relative_path = std::move(relative), .bytes = bytes});
    std::ranges::sort(snapshot.largest_files, std::greater{}, &StorageFile::bytes);
    if (snapshot.largest_files.size() > maximum) {
        snapshot.largest_files.resize(maximum);
    }
}

[[nodiscard]] Measure measure_tree(
    const std::filesystem::path& item,
    StorageSnapshot* const snapshot,
    const std::size_t largest_file_count,
    const std::stop_token stop_token) {
    Measure result;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(item, error);
    if (error || std::filesystem::is_symlink(status)) {
        return result;
    }
    if (std::filesystem::is_regular_file(status)) {
        result.bytes = std::filesystem::file_size(item, error);
        result.files = error ? 0U : 1U;
        if (!error && snapshot != nullptr) {
            remember_large_file(*snapshot, item, result.bytes, largest_file_count);
        }
        return result;
    }
    if (!std::filesystem::is_directory(status)) {
        return result;
    }
    std::filesystem::recursive_directory_iterator iterator(
        item, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end && !stop_token.stop_requested()) {
        const auto entry_status = iterator->symlink_status(error);
        if (!error && std::filesystem::is_symlink(entry_status)) {
            iterator.disable_recursion_pending();
        } else if (!error && std::filesystem::is_regular_file(entry_status)) {
            const auto bytes = iterator->file_size(error);
            if (!error) {
                result.bytes += bytes;
                ++result.files;
                if (snapshot != nullptr) {
                    remember_large_file(*snapshot, iterator->path(), bytes, largest_file_count);
                }
            }
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return result;
}

[[nodiscard]] std::size_t directory_count(const std::filesystem::path& path) noexcept {
    std::error_code error;
    std::size_t count = 0U;
    std::filesystem::directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const auto status = iterator->symlink_status(error);
        count += static_cast<std::size_t>(
            !error && std::filesystem::is_directory(status) && !std::filesystem::is_symlink(status));
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return count;
}

[[nodiscard]] std::string read_active_generation(const std::filesystem::path& root) {
    std::ifstream stream(root / "models" / "internal" / "active.json", std::ios::binary);
    if (!stream) {
        return {};
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::string error;
    const auto document = core::json::try_parse(text, error);
    if (!document.has_value()) {
        return {};
    }
    const auto generation = core::json::string_or(document->find("generation"));
    const std::filesystem::path component(generation);
    if (generation.empty() || component.filename() != component ||
        generation.find('/') != std::string::npos || generation.find('\\') != std::string::npos) {
        return {};
    }
    return generation;
}

[[nodiscard]] const StorageCategory* category(
    const StorageSnapshot& snapshot,
    const std::string_view name) noexcept {
    const auto found = std::ranges::find(snapshot.categories, name, &StorageCategory::name);
    return found == snapshot.categories.end() ? nullptr : &*found;
}

[[nodiscard]] std::uintmax_t category_bytes(
    const StorageSnapshot& snapshot,
    const std::string_view name) noexcept {
    const auto* value = category(snapshot, name);
    return value == nullptr ? 0U : value->bytes;
}

[[nodiscard]] std::size_t category_files(
    const StorageSnapshot& snapshot,
    const std::string_view name) noexcept {
    const auto* value = category(snapshot, name);
    return value == nullptr ? 0U : value->files;
}

} // namespace

std::vector<StorageQuota> default_storage_quotas() {
    constexpr std::uintmax_t mib = 1024ULL * 1024ULL;
    constexpr std::uintmax_t gib = 1024ULL * mib;
    return {
        {.name = "logs", .relative_path = "logs", .maximum_bytes = 256U * mib, .maximum_age_days = 30U, .minimum_entries_to_keep = 10U},
        {.name = "candidates", .relative_path = "candidates", .maximum_bytes = 2U * gib, .maximum_age_days = 14U, .minimum_entries_to_keep = 2U},
        {.name = "checkpoints", .relative_path = "models/internal/checkpoints", .maximum_bytes = 4U * gib, .maximum_age_days = 7U, .minimum_entries_to_keep = 2U},
        {.name = "outputs", .relative_path = "outputs", .maximum_bytes = 2U * gib, .maximum_age_days = 30U, .minimum_entries_to_keep = 20U},
        {.name = "cache", .relative_path = "cache", .maximum_bytes = 1U * gib, .maximum_age_days = 7U, .minimum_entries_to_keep = 0U},
    };
}

StorageSnapshot inspect_storage(
    const std::filesystem::path& root,
    const std::size_t largest_file_count,
    const std::stop_token stop_token) noexcept {
    return inspect_storage_with_growth(root, std::nullopt, largest_file_count, 8U, stop_token);
}

StorageSnapshot inspect_storage_with_growth(
    const std::filesystem::path& root,
    const std::optional<std::uintmax_t> previous_total_bytes,
    const std::size_t largest_file_count,
    const std::size_t largest_directory_count,
    const std::stop_token stop_token) noexcept {
    StorageSnapshot snapshot;
    snapshot.root = root.lexically_normal();
    snapshot.sampled_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    snapshot.quotas = default_storage_quotas();
    try {
        std::error_code error;
        const auto root_status = std::filesystem::symlink_status(snapshot.root, error);
        if (error || std::filesystem::is_symlink(root_status)) {
            return snapshot;
        }
        const auto space = std::filesystem::space(snapshot.root, error);
        if (!error) {
            snapshot.free_disk_bytes = space.available;
        }
        error.clear();
        std::filesystem::directory_iterator iterator(
            snapshot.root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::directory_iterator end;
        while (iterator != end && !stop_token.stop_requested()) {
            const auto status = iterator->symlink_status(error);
            if (!error && !std::filesystem::is_symlink(status)) {
                StorageCategory item{
                    .name = iterator->path().filename().string(),
                    .path = iterator->path(),
                };
                const auto measured = measure_tree(
                    iterator->path(), &snapshot, largest_file_count, stop_token);
                item.bytes = measured.bytes;
                item.files = measured.files;
                snapshot.total_bytes += item.bytes;
                snapshot.files += item.files;
                snapshot.largest_directories.push_back({
                    .relative_path = iterator->path().filename(),
                    .bytes = item.bytes,
                    .files = item.files,
                });
                snapshot.categories.push_back(std::move(item));
            }
            error.clear();
            iterator.increment(error);
            error.clear();
        }
        std::ranges::sort(snapshot.categories, std::greater{}, &StorageCategory::bytes);
        std::ranges::sort(snapshot.largest_directories, std::greater{}, &StorageDirectory::bytes);
        if (snapshot.largest_directories.size() > largest_directory_count) {
            snapshot.largest_directories.resize(largest_directory_count);
        }
        snapshot.growth_bytes = previous_total_bytes.has_value()
            ? static_cast<std::intmax_t>(snapshot.total_bytes) -
                static_cast<std::intmax_t>(*previous_total_bytes)
            : 0;
        snapshot.generation_directories = directory_count(
            snapshot.root / "models" / "internal" / "generations");
        snapshot.trajectory_directories = directory_count(snapshot.root / "trajectories");
        const auto data = measure_tree(snapshot.root / "data", nullptr, 0U, stop_token);
        const auto episodes = measure_tree(
            snapshot.root / "memory" / "episodes.jsonl", nullptr, 0U, stop_token);
        snapshot.training_data_bytes = data.bytes + episodes.bytes;
        snapshot.training_data_files = data.files + episodes.files;
        snapshot.active_generation = read_active_generation(snapshot.root);
        if (!snapshot.active_generation.empty()) {
            const auto active = measure_tree(
                snapshot.root / "models" / "internal" / "generations" /
                    snapshot.active_generation,
                nullptr, 0U, stop_token);
            snapshot.active_model_bytes = active.bytes;
            snapshot.active_model_files = active.files;
        }
        const auto legacy_runtime = measure_tree(
            snapshot.root / "runtime", nullptr, 0U, stop_token);
        const auto pack_runtime = measure_tree(
            snapshot.root / "packs" / "internal-pytorch-training" / "runtime",
            nullptr, 0U, stop_token);
        snapshot.runtime_bytes = legacy_runtime.bytes + pack_runtime.bytes;
        const auto packs_bytes = category_bytes(snapshot, "packs");
        const auto models_bytes = category_bytes(snapshot, "models");
        const auto outputs_bytes = category_bytes(snapshot, "outputs") +
            category_bytes(snapshot, "cache");
        const auto outputs_files = category_files(snapshot, "outputs") +
            category_files(snapshot, "cache");
        snapshot.cards = {
            {.key = "current-model", .label = "Current model", .bytes = snapshot.active_model_bytes, .files = snapshot.active_model_files},
            {.key = "all-models", .label = "All models", .bytes = models_bytes, .files = category_files(snapshot, "models")},
            {.key = "accepted-training-data", .label = "Accepted training data", .bytes = snapshot.training_data_bytes, .files = snapshot.training_data_files},
            {.key = "installed-packs", .label = "Installed packs", .bytes = packs_bytes, .files = category_files(snapshot, "packs")},
            {.key = "outputs-cache", .label = "Outputs and cache", .bytes = outputs_bytes, .files = outputs_files},
            {.key = "total-usage", .label = "Total usage", .bytes = snapshot.total_bytes, .growth_bytes = snapshot.growth_bytes, .files = snapshot.files},
            {.key = "free-disk", .label = "Free disk", .bytes = snapshot.free_disk_bytes},
        };
        for (const auto& descriptor : CapabilityPackManager::catalog()) {
            snapshot.pack_projections.push_back({
                .id = descriptor.id,
                .name = descriptor.name,
                .download_bytes = descriptor.projected_download_bytes,
                .install_bytes = descriptor.projected_install_bytes,
                .installed = std::filesystem::is_regular_file(
                    snapshot.root / "packs" / descriptor.id / "pack.json", error),
            });
            error.clear();
        }
    } catch (...) {
    }
    return snapshot;
}

} // namespace threadspire::ai::runtime