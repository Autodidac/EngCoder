#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include "engcoder/core/error.hpp"

namespace engcoder::workspace {

class Workspace {
public:
    explicit Workspace(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::expected<std::filesystem::path, core::Error> resolve(std::string_view relative, bool require_existing = false) const;
    [[nodiscard]] std::expected<std::string, core::Error> read_text(std::string_view relative, std::size_t max_bytes = 2 * 1024 * 1024) const;
    [[nodiscard]] std::expected<void, core::Error> write_text(
        std::string_view relative, std::string_view content, bool replace_existing = true) const;
    [[nodiscard]] std::expected<std::vector<std::string>, core::Error> tree(std::string_view relative = ".", std::size_t max_entries = 2000) const;
    [[nodiscard]] std::expected<std::vector<std::string>, core::Error> search_text(std::string_view query, std::size_t max_results = 200) const;
private:
    std::filesystem::path root_;
};

} // namespace engcoder::workspace
