module;
#include "module_prelude.hpp"

module threadspire.storage;

namespace threadspire::ai::runtime {
namespace {

struct CleanupEntry final {
    std::filesystem::path path;
    std::filesystem::file_time_type modified;
    std::uintmax_t bytes{0};
};

[[nodiscard]] bool safe_relative(const std::filesystem::path& relative) noexcept {
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == ".." || component == ".") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool no_link_path(
    const std::filesystem::path& root,
    const std::filesystem::path& relative) noexcept {
    if (!safe_relative(relative)) {
        return false;
    }
    std::error_code error;
    auto current = root;
    for (const auto& component : relative) {
        current /= component;
        const auto status = std::filesystem::symlink_status(current, error);
        if (!error && std::filesystem::is_symlink(status)) {
            return false;
        }
        error.clear();
    }
    return true;
}

[[nodiscard]] std::uintmax_t tree_size_no_follow(
    const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status)) {
        return 0U;
    }
    if (std::filesystem::is_regular_file(status)) {
        return std::filesystem::file_size(path, error);
    }
    if (!std::filesystem::is_directory(status)) {
        return 0U;
    }
    std::uintmax_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto child = iterator->symlink_status(error);
        if (!error && std::filesystem::is_symlink(child)) {
            iterator.disable_recursion_pending();
        } else if (!error && std::filesystem::is_regular_file(child)) {
            total += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return total;
}

void remove_no_follow(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error) {
        throw std::system_error(error, "unable to inspect cleanup target");
    }
    if (std::filesystem::is_symlink(status)) {
        throw std::runtime_error("cleanup target became a symbolic link");
    }
    if (!std::filesystem::is_directory(status)) {
        if (!std::filesystem::remove(path, error) || error) {
            throw std::system_error(error, "unable to remove cleanup file");
        }
        return;
    }
    std::filesystem::directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const auto child = iterator->path();
        iterator.increment(error);
        error.clear();
        remove_no_follow(child);
    }
    if (!std::filesystem::remove(path, error) || error) {
        throw std::system_error(error, "unable to remove cleanup directory");
    }
}

[[nodiscard]] bool protected_entry(const std::filesystem::path& path) noexcept {
    std::error_code error;
    if (std::filesystem::is_regular_file(path / ".active", error) ||
        std::filesystem::is_regular_file(path / "training.lock", error) ||
        std::filesystem::is_regular_file(path / "inference.lock", error)) {
        return true;
    }
    const auto name = path.filename().string();
    return name == "active" || name.ends_with(".active") || name.ends_with(".lock");
}

} // namespace

StorageCleanupResult cleanup_storage(
    const std::filesystem::path& root,
    const std::vector<StorageQuota>& quotas,
    const bool dry_run,
    const std::stop_token stop_token) noexcept {
    StorageCleanupResult result{.dry_run = dry_run};
    try {
        const auto normalized_root = std::filesystem::absolute(root).lexically_normal();
        std::error_code error;
        const auto root_status = std::filesystem::symlink_status(normalized_root, error);
        if (error || !std::filesystem::is_directory(root_status) ||
            std::filesystem::is_symlink(root_status)) {
            result.error = "storage cleanup root is missing, not a directory, or a link";
            return result;
        }
        for (const auto& quota : quotas) {
            if (stop_token.stop_requested()) {
                result.error = "storage cleanup cancelled";
                return result;
            }
            if (!no_link_path(normalized_root, quota.relative_path)) {
                result.error = "unsafe storage quota path: " + quota.relative_path.string();
                return result;
            }
            const auto target = (normalized_root / quota.relative_path).lexically_normal();
            const auto target_status = std::filesystem::symlink_status(target, error);
            if (error || !std::filesystem::exists(target_status)) {
                error.clear();
                continue;
            }
            if (!std::filesystem::is_directory(target_status) ||
                std::filesystem::is_symlink(target_status)) {
                result.error = "storage quota target is not a safe directory";
                return result;
            }
            std::vector<CleanupEntry> entries;
            std::uintmax_t total = 0U;
            std::filesystem::directory_iterator iterator(
                target, std::filesystem::directory_options::skip_permission_denied, error);
            const std::filesystem::directory_iterator end;
            while (iterator != end) {
                const auto status = iterator->symlink_status(error);
                if (!error && !std::filesystem::is_symlink(status) &&
                    !protected_entry(iterator->path())) {
                    const auto bytes = tree_size_no_follow(iterator->path());
                    entries.push_back({
                        .path = iterator->path(),
                        .modified = iterator->last_write_time(error),
                        .bytes = bytes,
                    });
                    total += bytes;
                }
                error.clear();
                iterator.increment(error);
                error.clear();
            }
            std::ranges::sort(entries, {}, &CleanupEntry::modified);
            const auto now = std::filesystem::file_time_type::clock::now();
            const auto maximum_age = std::chrono::hours(24U * quota.maximum_age_days);
            std::size_t remaining = entries.size();
            for (const auto& entry : entries) {
                if (stop_token.stop_requested()) {
                    result.error = "storage cleanup cancelled";
                    return result;
                }
                const auto over_quota = quota.maximum_bytes > 0U && total > quota.maximum_bytes;
                const auto too_old = quota.maximum_age_days > 0U &&
                    now > entry.modified && now - entry.modified > maximum_age;
                if ((!over_quota && !too_old) || remaining <= quota.minimum_entries_to_keep) {
                    --remaining;
                    continue;
                }
                const auto relative = entry.path.lexically_relative(normalized_root);
                if (!safe_relative(relative)) {
                    result.error = "cleanup entry escaped the storage root";
                    return result;
                }
                if (!dry_run) {
                    remove_no_follow(entry.path);
                }
                result.removed.push_back({.relative_path = relative, .bytes = entry.bytes});
                result.bytes_reclaimed += entry.bytes;
                total = total > entry.bytes ? total - entry.bytes : 0U;
                --remaining;
            }
        }
        result.succeeded = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    } catch (...) {
        result.error = "unknown storage cleanup failure";
        return result;
    }
}

} // namespace threadspire::ai::runtime