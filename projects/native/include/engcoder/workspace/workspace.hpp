#pragma once
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "engcoder/core/error.hpp"

namespace engcoder::workspace {

inline constexpr std::size_t max_text_file_bytes = 2 * 1024 * 1024;
inline constexpr std::size_t max_text_page_bytes = 3'072;
inline constexpr std::size_t default_text_page_lines = 40;
inline constexpr std::size_t max_text_page_lines = 80;

struct TextPage {
    std::string content;
    std::optional<std::size_t> next_line;
};

struct TextPatch {
    std::size_t offset;
    std::size_t length;
    std::string replacement;
};

[[nodiscard]] std::expected<TextPatch, core::Error> plan_text_patch(
    std::string_view source, std::string_view expected, std::string_view replacement,
    std::optional<std::size_t> start_line = std::nullopt);

class Workspace {
public:
    explicit Workspace(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::expected<std::filesystem::path, core::Error> resolve(std::string_view relative, bool require_existing = false) const;
    [[nodiscard]] std::expected<std::string, core::Error> read_text(
        std::string_view relative, std::size_t max_bytes = max_text_file_bytes) const;
    [[nodiscard]] std::expected<TextPage, core::Error> read_text_page(
        std::string_view relative, std::size_t start_line, std::size_t max_lines) const;
    [[nodiscard]] std::expected<void, core::Error> write_text(
        std::string_view relative, std::string_view content, bool replace_existing = true) const;
    [[nodiscard]] std::expected<std::vector<std::string>, core::Error> tree(std::string_view relative = ".", std::size_t max_entries = 2000) const;
    [[nodiscard]] std::expected<std::vector<std::string>, core::Error> search_text(std::string_view query, std::size_t max_results = 200) const;
private:
    std::filesystem::path root_;
};

} // namespace engcoder::workspace
