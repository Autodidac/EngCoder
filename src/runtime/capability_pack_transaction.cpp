module;
#include "module_prelude.hpp"

module threadspire.scheduler;

namespace threadspire::ai::runtime {
namespace {

void remove_tree_no_follow(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::exists(status)) {
        return;
    }
    if (std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
        std::filesystem::remove(path, error);
        return;
    }
    std::filesystem::directory_iterator iterator(
        path, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const auto child = iterator->path();
        iterator.increment(error);
        error.clear();
        remove_tree_no_follow(child);
    }
    std::filesystem::remove(path, error);
}

[[nodiscard]] std::uintmax_t copy_payload(
    const std::filesystem::path& source,
    const std::filesystem::path& staging,
    const std::stop_token stop_token) {
    std::error_code error;
    const auto source_status = std::filesystem::symlink_status(source, error);
    if (error || !std::filesystem::is_directory(source_status) ||
        std::filesystem::is_symlink(source_status)) {
        throw std::runtime_error("capability-pack source must be a real directory");
    }
    std::filesystem::create_directories(staging);
    std::uintmax_t bytes = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        source, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("capability-pack installation cancelled");
        }
        const auto status = iterator->symlink_status(error);
        if (error) {
            throw std::runtime_error("unable to inspect capability-pack payload: " + error.message());
        }
        if (std::filesystem::is_symlink(status)) {
            throw std::runtime_error("capability-pack payload contains a symbolic link");
        }
        const auto relative = iterator->path().lexically_relative(source);
        if (relative.empty() || relative.is_absolute() ||
            std::ranges::find(relative, "..") != relative.end()) {
            throw std::runtime_error("capability-pack payload escaped its source root");
        }
        const auto destination = staging / relative;
        if (std::filesystem::is_directory(status)) {
            std::filesystem::create_directories(destination);
        } else if (std::filesystem::is_regular_file(status)) {
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(
                iterator->path(), destination,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                throw std::runtime_error("unable to copy capability-pack payload: " + error.message());
            }
            bytes += std::filesystem::file_size(destination, error);
            if (error) {
                throw std::runtime_error("unable to measure capability-pack payload");
            }
        } else {
            throw std::runtime_error("capability-pack payload contains an unsupported entry");
        }
        iterator.increment(error);
        if (error) {
            throw std::runtime_error("unable to enumerate capability-pack payload: " + error.message());
        }
    }
    return bytes;
}

void write_manifest(
    const std::filesystem::path& staging,
    const CapabilityPackDescriptor& descriptor,
    const std::uintmax_t bytes) {
    core::json::Value::Object manifest{
        {"schema_version", 1.0},
        {"id", descriptor.id},
        {"name", descriptor.name},
        {"version", descriptor.version},
        {"kind", std::string(to_string(descriptor.kind))},
        {"installed_bytes", static_cast<double>(bytes)},
        {"installed_unix_ms", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())},
        {"transactional", true},
    };
    const auto destination = staging / "pack.json";
    auto temporary = destination;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << core::json::stringify(core::json::Value(std::move(manifest))) << '\n';
        stream.flush();
        if (!stream) {
            throw std::runtime_error("unable to write capability-pack manifest");
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        throw std::runtime_error("unable to publish capability-pack manifest: " + error.message());
    }
}

[[nodiscard]] bool continue_install(
    const CapabilityPackInstallRequest& request,
    const std::string_view stage,
    const std::stop_token stop_token) {
    return !stop_token.stop_requested() &&
        (!request.continue_stage || request.continue_stage(stage));
}

} // namespace

CapabilityPackInstallResult CapabilityPackManager::install_transactional(
    const CapabilityPackInstallRequest& request,
    const std::stop_token stop_token) {
    CapabilityPackInstallResult result;
    bool target_moved = false;
    bool cancelled = false;
    std::filesystem::path target;
    std::filesystem::path transaction;
    std::filesystem::path staging;
    std::filesystem::path backup;
    try {
        target = pack_path(request.descriptor.id);
        for (const auto& dependency : request.descriptor.dependencies) {
            if (!installed(dependency)) {
                throw std::runtime_error("required capability pack is not installed: " + dependency);
            }
        }
        static std::atomic_uint64_t serial{1U};
        const auto transaction_id = request.descriptor.id + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(serial.fetch_add(1U, std::memory_order_relaxed));
        transaction = packs_root_ / ".transactions" / transaction_id;
        staging = transaction / "staging";
        backup = transaction / "previous";
        std::filesystem::create_directories(transaction);
        if (!continue_install(request, "preparing", stop_token)) {
            cancelled = true;
            throw std::runtime_error("capability-pack installation cancelled before copy");
        }
        auto bytes = copy_payload(request.source_directory, staging, stop_token);
        for (const auto& required : request.descriptor.required_files) {
            const std::filesystem::path relative(required);
            if (relative.empty() || relative.is_absolute() ||
                std::ranges::find(relative, "..") != relative.end() ||
                !std::filesystem::is_regular_file(staging / relative)) {
                throw std::runtime_error("capability-pack payload is missing required file: " + required);
            }
        }
        write_manifest(staging, request.descriptor, bytes);
        bytes += std::filesystem::file_size(staging / "pack.json");
        if (!continue_install(request, "before-commit", stop_token)) {
            cancelled = true;
            throw std::runtime_error("capability-pack installation cancelled before commit");
        }
        std::error_code error;
        if (std::filesystem::exists(target, error)) {
            const auto status = std::filesystem::symlink_status(target, error);
            if (error || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_directory(status)) {
                throw std::runtime_error("existing capability-pack target is unsafe");
            }
            std::filesystem::rename(target, backup, error);
            if (error) {
                throw std::runtime_error("unable to stage previous capability pack: " + error.message());
            }
            target_moved = true;
        }
        if (!continue_install(request, "commit", stop_token)) {
            cancelled = true;
            throw std::runtime_error("capability-pack installation cancelled during commit");
        }
        std::filesystem::rename(staging, target, error);
        if (error) {
            throw std::runtime_error("unable to commit capability pack: " + error.message());
        }
        target_moved = false;
        remove_tree_no_follow(backup);
        remove_tree_no_follow(transaction);
        result.succeeded = true;
        result.install_path = target;
        result.installed_bytes = bytes;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        result.cancelled = cancelled || stop_token.stop_requested();
        std::error_code error;
        if (target_moved && !std::filesystem::exists(target, error) &&
            std::filesystem::exists(backup, error)) {
            std::filesystem::rename(backup, target, error);
            result.previous_install_preserved = !error;
        }
        remove_tree_no_follow(staging);
        if (!target_moved || result.previous_install_preserved) {
            remove_tree_no_follow(transaction);
        }
        return result;
    }
}

} // namespace threadspire::ai::runtime