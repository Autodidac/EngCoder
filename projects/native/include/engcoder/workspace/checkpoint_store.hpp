#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include "engcoder/core/error.hpp"
#include "engcoder/workspace/workspace.hpp"

namespace engcoder::workspace {

class CheckpointStore {
public:
    CheckpointStore(Workspace& workspace, std::filesystem::path state_root, std::size_t file_limit);
    [[nodiscard]] std::expected<std::string, core::Error> capture(std::string_view relative);
    [[nodiscard]] std::expected<void, core::Error> restore(std::string_view checkpoint_id, std::string_view relative);
private:
    Workspace& workspace_;
    std::filesystem::path state_root_;
    std::size_t file_limit_;
};

} // namespace engcoder::workspace
