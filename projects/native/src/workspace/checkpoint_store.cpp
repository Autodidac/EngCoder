#include "engcoder/workspace/checkpoint_store.hpp"
#include "engcoder/core/ids.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace engcoder::workspace {
namespace {

[[nodiscard]] bool valid_checkpoint_id(std::string_view id) {
    if (!id.starts_with("cp_") || id.size() > 96 || id.size() < 8) return false;
    return std::ranges::all_of(id, [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-';
    });
}

[[nodiscard]] bool valid_file_relative(std::string_view raw) {
    if (raw.empty() || raw.size() > 4096) return false;
    const std::filesystem::path path(raw);
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) return false;
    for (const auto& component : path) {
        if (component == ".." || component == "." || component.empty()) return false;
    }
    // Control filenames belong to the checkpoint store, never user snapshots.
    const auto top = *path.begin();
    if (top == "__missing" || top == "__snapshot_path") return false;
    return true;
}

[[nodiscard]] bool is_below(const std::filesystem::path& parent, const std::filesystem::path& child) {
    auto a = parent.begin();
    auto b = child.begin();
    for (; a != parent.end() && b != child.end(); ++a, ++b) {
        if (*a != *b) return false;
    }
    return a == parent.end() && b != child.end();
}

[[nodiscard]] bool checkpoint_location_safe(
    const std::filesystem::path& state_root, const std::filesystem::path& file) {
    std::error_code ec;
    const auto checkpoint_root = std::filesystem::weakly_canonical(state_root / "checkpoints", ec);
    if (ec) return false;
    const auto resolved = std::filesystem::weakly_canonical(file, ec);
    if (ec || !is_below(checkpoint_root, resolved)) return false;
    // Neither a saved snapshot nor a missing marker may be a symlink.
    const auto status = std::filesystem::symlink_status(file, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) return false;
    return !std::filesystem::is_symlink(status);
}

[[nodiscard]] core::Error invalid_path() {
    return {core::make_error_code(core::Errc::workspace_violation), "unsafe checkpoint id or path"};
}

} // namespace

CheckpointStore::CheckpointStore(Workspace& workspace, std::filesystem::path state_root, std::size_t file_limit)
    : workspace_(workspace), state_root_(std::move(state_root)), file_limit_(file_limit) {
    std::filesystem::create_directories(state_root_ / "checkpoints");
}

std::expected<std::string, core::Error> CheckpointStore::capture(std::string_view relative) {
    if (!valid_file_relative(relative)) return std::unexpected(invalid_path());
    auto source = workspace_.resolve(relative, false);
    if (!source) return std::unexpected(source.error());
    const auto id = core::make_id("cp");
    const auto dir = state_root_ / "checkpoints" / id;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
    {
        std::ofstream binding(dir / "__snapshot_path", std::ios::binary | std::ios::trunc);
        if (!binding) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "unable to bind checkpoint to its workspace path"});
        binding.write(relative.data(), static_cast<std::streamsize>(relative.size()));
        binding.flush();
        if (!binding) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "unable to write checkpoint path binding"});
    }
    if (!std::filesystem::exists(*source)) {
        std::ofstream marker(dir / "__missing", std::ios::binary | std::ios::trunc);
        if (!marker) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "unable to save missing-file checkpoint"});
        return id;
    }
    if (!std::filesystem::is_regular_file(*source)) return std::unexpected(invalid_path());
    const auto size = std::filesystem::file_size(*source, ec);
    if (ec || size > file_limit_) return std::unexpected(core::Error{core::make_error_code(core::Errc::approval_required), "file exceeds automatic checkpoint limit"});
    const auto target = dir / std::filesystem::path(relative);
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec || !checkpoint_location_safe(state_root_, target)) return std::unexpected(invalid_path());
    std::filesystem::copy_file(*source, target, std::filesystem::copy_options::none, ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
    return id;
}

std::expected<void, core::Error> CheckpointStore::restore(std::string_view checkpoint_id, std::string_view relative) {
    if (!valid_checkpoint_id(checkpoint_id) || !valid_file_relative(relative)) return std::unexpected(invalid_path());
    auto target = workspace_.resolve(relative, false);
    if (!target) return std::unexpected(target.error());
    const auto dir = state_root_ / "checkpoints" / std::string(checkpoint_id);
    const auto marker = dir / "__missing";
    const auto source = dir / std::filesystem::path(relative);
    std::error_code ec;
    const auto binding = dir / "__snapshot_path";
    if (!std::filesystem::is_directory(dir) || !checkpoint_location_safe(state_root_, source) ||
        !checkpoint_location_safe(state_root_, marker) ||
        !checkpoint_location_safe(state_root_, binding)) return std::unexpected(invalid_path());
    // A checkpoint is tied to the exact path captured. An attacker cannot use
    // a missing-file marker to delete an unrelated workspace file.
    std::ifstream source_binding(binding, std::ios::binary);
    if (!source_binding) return std::unexpected(invalid_path());
    const std::string bound_path{std::istreambuf_iterator<char>(source_binding),
        std::istreambuf_iterator<char>{}};
    if (bound_path != relative) return std::unexpected(invalid_path());
    if (std::filesystem::exists(marker)) {
        std::filesystem::remove(*target, ec);
        if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
        return {};
    }
    if (!std::filesystem::is_regular_file(source)) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "checkpoint content missing"});
    std::filesystem::create_directories(target->parent_path(), ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
    std::filesystem::copy_file(source, *target, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
    return {};
}
} // namespace engcoder::workspace
