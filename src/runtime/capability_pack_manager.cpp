module;
#include "module_prelude.hpp"

module threadspire.scheduler;

namespace threadspire::ai::runtime {
namespace {

constexpr std::uintmax_t mib = 1024ULL * 1024ULL;
constexpr std::uintmax_t gib = 1024ULL * mib;

[[nodiscard]] bool safe_pack_id(const std::string_view id) noexcept {
    return !id.empty() && id != "." && id != ".." &&
        std::ranges::all_of(id, [](const unsigned char character) {
            return std::isalnum(character) != 0 || character == '-' || character == '_';
        });
}

[[nodiscard]] std::uintmax_t tree_size(const std::filesystem::path& root) noexcept {
    std::error_code error;
    std::uintmax_t total = 0U;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        const auto status = iterator->symlink_status(error);
        if (!error && std::filesystem::is_symlink(status)) {
            iterator.disable_recursion_pending();
        } else if (!error && std::filesystem::is_regular_file(status)) {
            total += iterator->file_size(error);
        }
        error.clear();
        iterator.increment(error);
        error.clear();
    }
    return total;
}

} // namespace

CapabilityPackManager::CapabilityPackManager(std::filesystem::path packs_root)
    : packs_root_(std::move(packs_root)) {
}

std::vector<CapabilityPackDescriptor> CapabilityPackManager::catalog() {
    return {
        {
            .id = "compact-native-inference",
            .name = "Compact Native Inference",
            .version = "1",
            .kind = CapabilityPackKind::compact_native_inference,
            .projected_download_bytes = 8U * mib,
            .projected_install_bytes = 16U * mib,
            .required_files = {"bin/threadspire-native-host.exe"},
        },
        {
            .id = "internal-pytorch-training",
            .name = "Internal PyTorch Training",
            .version = "1",
            .kind = CapabilityPackKind::internal_pytorch_training,
            .projected_download_bytes = 2700U * mib,
            .projected_install_bytes = 4300U * mib,
            .required_files = {"runtime/Scripts/python.exe", "backend/model_worker.py", "backend/internal_model.py", "backend/curriculum_guard.py"},
        },
        {
            .id = "transformers-peft-finetuning",
            .name = "Advanced Fine-tuning (Transformers/PEFT)",
            .version = "1",
            .kind = CapabilityPackKind::transformers_peft_finetuning,
            .projected_download_bytes = 900U * mib,
            .projected_install_bytes = 1500U * mib,
            .required_files = {"pack.ready"},
            .dependencies = {"internal-pytorch-training"},
        },
        {
            .id = "image-engine",
            .name = "Image Generation",
            .version = "1",
            .kind = CapabilityPackKind::image_engine,
            .projected_download_bytes = 3500U * mib,
            .projected_install_bytes = 7U * gib,
            .required_files = {"pack.ready"},
        },
        {
            .id = "audio-engine",
            .name = "Audio Generation",
            .version = "1",
            .kind = CapabilityPackKind::audio_engine,
            .projected_download_bytes = 1200U * mib,
            .projected_install_bytes = 2500U * mib,
            .required_files = {"pack.ready"},
        },
    };
}

std::filesystem::path CapabilityPackManager::pack_path(const std::string_view id) const {
    if (!safe_pack_id(id)) {
        throw std::invalid_argument("invalid capability-pack id");
    }
    return packs_root_ / std::string(id);
}

bool CapabilityPackManager::installed(const std::string_view id) const noexcept {
    try {
        std::error_code error;
        return std::filesystem::is_regular_file(pack_path(id) / "pack.json", error) && !error;
    } catch (...) {
        return false;
    }
}

std::vector<CapabilityPackRecord> CapabilityPackManager::inspect() const {
    auto descriptors = catalog();
    std::vector<CapabilityPackRecord> records;
    records.reserve(descriptors.size());
    for (auto& descriptor : descriptors) {
        const auto path = pack_path(descriptor.id);
        const auto is_installed = installed(descriptor.id);
        records.push_back({
            .descriptor = std::move(descriptor),
            .state = is_installed ? CapabilityPackState::installed : CapabilityPackState::available,
            .install_path = path,
            .installed_bytes = is_installed ? tree_size(path) : 0U,
        });
    }
    return records;
}

std::string_view to_string(const CapabilityPackKind kind) noexcept {
    switch (kind) {
    case CapabilityPackKind::compact_native_inference: return "compact-native-inference";
    case CapabilityPackKind::internal_pytorch_training: return "internal-pytorch-training";
    case CapabilityPackKind::transformers_peft_finetuning: return "transformers-peft-finetuning";
    case CapabilityPackKind::image_engine: return "image-engine";
    case CapabilityPackKind::audio_engine: return "audio-engine";
    }
    return "compact-native-inference";
}

std::string_view to_string(const CapabilityPackState state) noexcept {
    switch (state) {
    case CapabilityPackState::available: return "available";
    case CapabilityPackState::installing: return "installing";
    case CapabilityPackState::installed: return "installed";
    case CapabilityPackState::failed: return "failed";
    }
    return "available";
}

} // namespace threadspire::ai::runtime