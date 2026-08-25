module;
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.scheduler:packs;

export {
namespace threadspire::ai::runtime {

enum class CapabilityPackKind {
    compact_native_inference,
    internal_pytorch_training,
    transformers_peft_finetuning,
    image_engine,
    audio_engine,
};

enum class CapabilityPackState {
    available,
    installing,
    installed,
    failed,
};

struct CapabilityPackDescriptor final {
    std::string id;
    std::string name;
    std::string version;
    CapabilityPackKind kind{CapabilityPackKind::compact_native_inference};
    std::uintmax_t projected_download_bytes{0};
    std::uintmax_t projected_install_bytes{0};
    std::vector<std::string> required_files;
    std::vector<std::string> dependencies;
};

struct CapabilityPackRecord final {
    CapabilityPackDescriptor descriptor;
    CapabilityPackState state{CapabilityPackState::available};
    std::filesystem::path install_path;
    std::uintmax_t installed_bytes{0};
    std::string error;
};

using PackStageCallback = std::function<bool(std::string_view)>;

struct CapabilityPackInstallRequest final {
    CapabilityPackDescriptor descriptor;
    std::filesystem::path source_directory;
    PackStageCallback continue_stage;
};

struct CapabilityPackInstallResult final {
    bool succeeded{false};
    bool cancelled{false};
    bool previous_install_preserved{true};
    std::filesystem::path install_path;
    std::uintmax_t installed_bytes{0};
    std::string error;
};

class CapabilityPackManager final {
public:
    explicit CapabilityPackManager(std::filesystem::path packs_root);

    [[nodiscard]] static std::vector<CapabilityPackDescriptor> catalog();
    [[nodiscard]] std::vector<CapabilityPackRecord> inspect() const;
    [[nodiscard]] CapabilityPackInstallResult install_transactional(
        const CapabilityPackInstallRequest& request,
        std::stop_token stop_token = {});
    [[nodiscard]] std::filesystem::path pack_path(std::string_view id) const;
    [[nodiscard]] bool installed(std::string_view id) const noexcept;

private:
    std::filesystem::path packs_root_;
};

[[nodiscard]] std::string_view to_string(CapabilityPackKind kind) noexcept;
[[nodiscard]] std::string_view to_string(CapabilityPackState state) noexcept;

} // namespace threadspire::ai::runtime
}