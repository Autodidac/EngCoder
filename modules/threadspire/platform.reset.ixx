module;
#include <filesystem>
#include <string>
#include <vector>

export module threadspire.platform:reset;

export {
namespace threadspire::ai::platform {

struct ResetPathValidation final {
    bool valid{false};
    std::filesystem::path resolved_target;
    std::filesystem::path expected_target;
    std::vector<std::filesystem::path> linked_entries;
    std::string error;
};

[[nodiscard]] ResetPathValidation validate_engcoder_reset_path(
    const std::filesystem::path& candidate,
    const std::filesystem::path& local_app_data_root) noexcept;

} // namespace threadspire::ai::platform
}
