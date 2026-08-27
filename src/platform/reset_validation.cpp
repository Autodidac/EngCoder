module;
#include "module_prelude.hpp"

module threadspire.platform;

namespace threadspire::ai::platform {
namespace {

[[nodiscard]] std::string normalized_key(const std::filesystem::path& path) {
    auto value = std::filesystem::absolute(path).lexically_normal().generic_string();
#ifdef _WIN32
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    while (value.size() > 1U && value.ends_with('/')) {
        value.pop_back();
    }
    return value;
}

} // namespace

ResetPathValidation validate_engcoder_reset_path(
    const std::filesystem::path& candidate,
    const std::filesystem::path& local_app_data_root) noexcept {
    ResetPathValidation result;
    try {
        if (candidate.empty() || local_app_data_root.empty()) {
            result.error = "reset target and LocalAppData root are required";
            return result;
        }
        const auto app_data = std::filesystem::absolute(local_app_data_root).lexically_normal();
        result.expected_target = (app_data / "EngCoder").lexically_normal();
        result.resolved_target = std::filesystem::absolute(candidate).lexically_normal();
        if (normalized_key(result.resolved_target) != normalized_key(result.expected_target) ||
            result.resolved_target.filename() != "EngCoder" ||
            normalized_key(result.resolved_target.parent_path()) != normalized_key(app_data)) {
            result.error = "reset target must resolve to exactly LocalAppData/EngCoder";
            return result;
        }
        if (result.resolved_target == result.resolved_target.root_path() ||
            result.resolved_target.parent_path() == result.resolved_target.root_path()) {
            result.error = "reset target is too broad";
            return result;
        }
        std::error_code error;
        const auto app_data_status = std::filesystem::symlink_status(app_data, error);
        if (error || std::filesystem::is_symlink(app_data_status) ||
            !std::filesystem::is_directory(app_data_status)) {
            result.error = "LocalAppData root is missing, not a directory, or a link";
            return result;
        }
        error.clear();
        const auto target_status = std::filesystem::symlink_status(result.resolved_target, error);
        if (!error && std::filesystem::exists(target_status)) {
            if (std::filesystem::is_symlink(target_status) ||
                !std::filesystem::is_directory(target_status)) {
                result.error = "EngCoder data root is not a real directory";
                return result;
            }
            std::filesystem::recursive_directory_iterator iterator(
                result.resolved_target,
                std::filesystem::directory_options::skip_permission_denied,
                error);
            const std::filesystem::recursive_directory_iterator end;
            while (iterator != end) {
                const auto status = iterator->symlink_status(error);
                if (!error && std::filesystem::is_symlink(status)) {
                    iterator.disable_recursion_pending();
                    const auto relative = iterator->path().lexically_relative(result.resolved_target);
                    if (relative.empty() || relative.is_absolute()) {
                        result.error = "linked entry escaped the EngCoder data root";
                        return result;
                    }
                    result.linked_entries.push_back(relative);
                }
                error.clear();
                iterator.increment(error);
                error.clear();
            }
        } else if (error != std::errc::no_such_file_or_directory) {
            error.clear();
        }
        result.valid = true;
        return result;
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    } catch (...) {
        result.error = "unknown reset-path validation failure";
        return result;
    }
}

} // namespace threadspire::ai::platform
