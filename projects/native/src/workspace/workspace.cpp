#include "engcoder/workspace/workspace.hpp"
#include "engcoder/core/ids.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif

namespace engcoder::workspace {
namespace {
bool is_ignored(const std::filesystem::path& p) {
    for (const auto& part : p) {
        const auto s = [](std::string piece) {
            if constexpr (std::filesystem::path::preferred_separator != '/') {
                for (char& c : piece) {
                    if (c >= 'A' && c <= 'Z') {
                        c = static_cast<char>(c - 'A' + 'a');
                    }
                }
            }
            return piece;
        }(part.string());
        if (s == ".git" || s == ".engcoder" || s == "out" || s == "build" || s == "node_modules" || s == "vcpkg_installed") return true;
    }
    return false;
}

bool admit_enumerated_entry(const Workspace& workspace,
                            std::filesystem::recursive_directory_iterator& entry,
                            std::filesystem::path& relative) {
    std::error_code error;
    const auto status = entry->symlink_status(error);
    relative = entry->path().lexically_relative(workspace.root());
    if (!error && !std::filesystem::is_symlink(status) && !relative.empty() &&
        !is_ignored(relative)) {
        const auto admitted = workspace.resolve(relative.generic_string(), true);
        // A directory alias may be reported as an ordinary directory by the
        // platform. Do not enumerate a second path to the same files.
        if (admitted && admitted->lexically_normal() == entry->path().lexically_normal())
            return true;
    }
    entry.disable_recursion_pending();
    return false;
}

bool has_uniform_crlf(std::string_view text) {
    auto newline = text.find_first_of("\r\n");
    if (newline == std::string_view::npos) return false;

    while (newline != std::string_view::npos) {
        if (text.substr(newline, 2) != "\r\n") return false;
        newline = text.find_first_of("\r\n", newline + 2);
    }
    return true;
}

std::expected<std::string, core::Error> patch_text_as_crlf(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char byte = text[index];
        if (byte == '\r' && (index + 1 == text.size() || text[index + 1] != '\n')) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "patch text contains a standalone carriage return in a CRLF file",
            });
        }
        if (byte == '\n' && (index == 0 || text[index - 1] != '\r')) result.push_back('\r');
        result.push_back(byte);
    }
    return result;
}

std::unexpected<core::Error> invalid_patch(std::string message) {
    return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), std::move(message)});
}

std::optional<std::size_t> line_offset(std::string_view text, std::size_t start_line) {
    if (start_line == 0) return std::nullopt;
    std::size_t start = 0;
    for (std::size_t line = 1; line < start_line; ++line) {
        const auto newline = text.find('\n', start);
        if (newline == std::string_view::npos || newline + 1 == text.size()) return std::nullopt;
        start = newline + 1;
    }
    return start;
}
}

std::expected<TextPatch, core::Error> plan_text_patch(
    std::string_view source, std::string_view expected, std::string_view replacement,
    std::optional<std::size_t> start_line) {
    if (expected.empty()) return invalid_patch("expected block must not be empty");

    std::string adjusted_expected;
    std::string adjusted_replacement;
    if (has_uniform_crlf(source)) {
        auto expected_crlf = patch_text_as_crlf(expected);
        if (!expected_crlf) return std::unexpected(expected_crlf.error());
        auto replacement_crlf = patch_text_as_crlf(replacement);
        if (!replacement_crlf) return std::unexpected(replacement_crlf.error());
        adjusted_expected = std::move(*expected_crlf);
        adjusted_replacement = std::move(*replacement_crlf);
        expected = adjusted_expected;
        replacement = adjusted_replacement;
    }

    if (start_line) {
        const auto start = line_offset(source, *start_line);
        if (!start || !source.substr(*start).starts_with(expected))
            return invalid_patch("expected block no longer matches the referenced line");
        const auto rest = source.substr(*start + expected.size());
        if (!rest.empty() && !rest.starts_with('\n') && !rest.starts_with("\r\n"))
            return invalid_patch("expected block no longer matches the complete referenced lines");
        if (expected == replacement) return invalid_patch("replacement does not change the expected block");
        return TextPatch{*start, expected.size(), std::string{replacement}};
    }
    const auto first = source.find(expected);
    if (first == std::string_view::npos) return invalid_patch("expected block not found");
    if (source.find(expected, first + 1) != std::string_view::npos) {
        return invalid_patch("expected block is ambiguous");
    }
    if (expected == replacement) return invalid_patch("replacement does not change the expected block");
    return TextPatch{first, expected.size(), std::string{replacement}};
}

Workspace::Workspace(std::filesystem::path root) : root_(std::filesystem::weakly_canonical(std::move(root))) {
    if (!std::filesystem::is_directory(root_)) throw std::runtime_error("workspace is not a directory: " + root_.string());
}

std::expected<std::filesystem::path, core::Error> Workspace::resolve(std::string_view relative, bool require_existing) const {
    std::error_code ec;
    auto input = std::filesystem::path(relative);
    if (input.is_absolute()) return std::unexpected(core::Error{core::make_error_code(core::Errc::workspace_violation), "absolute paths are not accepted"});
    auto candidate = std::filesystem::weakly_canonical(root_ / input, ec);
    if (ec) {
        auto parent = std::filesystem::weakly_canonical((root_ / input).parent_path(), ec);
        if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
        candidate = parent / input.filename();
    }
    auto r = root_.lexically_normal(); auto c = candidate.lexically_normal();
    auto ri = r.begin(), ci = c.begin();
    for (; ri != r.end() && ci != c.end(); ++ri, ++ci) if (*ri != *ci) return std::unexpected(core::Error{core::make_error_code(core::Errc::workspace_violation), "path escapes workspace"});
    if (ri != r.end()) return std::unexpected(core::Error{core::make_error_code(core::Errc::workspace_violation), "path escapes workspace"});
    if (require_existing && !std::filesystem::exists(candidate)) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "path does not exist"});
    return candidate;
}

std::expected<std::string, core::Error> Workspace::read_text(std::string_view relative, std::size_t max_bytes) const {
    auto path = resolve(relative, true); if (!path) return std::unexpected(path.error());
    std::error_code ec; const auto size = std::filesystem::file_size(*path, ec);
    if (!ec && size > max_bytes) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "file exceeds read limit"});
    std::ifstream in(*path, std::ios::binary); if (!in) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "unable to open file"});
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

std::expected<TextPage, core::Error> Workspace::read_text_page(
    std::string_view relative, std::size_t start_line, std::size_t max_lines) const {
    if (start_line == 0 || start_line > max_text_file_bytes || max_lines == 0 || max_lines > max_text_page_lines) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::invalid_argument), "workspace read page is outside the line limits"});
    }
    const auto contents = read_text(relative);
    if (!contents) return std::unexpected(contents.error());

    const std::string_view text = *contents;
    const auto offset = line_offset(text, start_line);
    if (!offset) return std::unexpected(core::Error{
        core::make_error_code(core::Errc::invalid_argument), "start_line is past the end of the file"});
    const auto start = *offset;

    std::size_t end = start;
    std::size_t lines = 0;
    while (end < text.size() && lines < max_lines) {
        const auto newline = text.find('\n', end);
        const auto next = newline == std::string_view::npos ? text.size() : newline + 1;
        if (next - start > max_text_page_bytes) break;
        end = next;
        ++lines;
    }
    if (end == start && end < text.size()) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument),
            "Line " + std::to_string(start_line) + " exceeds the read page byte limit. Try start_line " +
            std::to_string(start_line + 1) + " to skip it."});
    }
    return TextPage{
        .content = std::string{text.substr(start, end - start)},
        .next_line = end < text.size() ? std::optional{start_line + lines} : std::nullopt,
    };
}

std::expected<void, core::Error> Workspace::write_text(
    std::string_view relative, std::string_view content, bool replace_existing) const {
    auto path = resolve(relative, false); if (!path) return std::unexpected(path.error());
    std::error_code ec; std::filesystem::create_directories(path->parent_path(), ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
    const auto temp = path->parent_path() / (path->filename().string() + ".engcoder." + core::make_id("tmp"));
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); if (!out) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "unable to open temporary file"}); out.write(content.data(), static_cast<std::streamsize>(content.size())); out.flush(); if (!out) { std::filesystem::remove(temp, ec); return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "write failed"}); } }
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace_existing ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temp.c_str(), path->c_str(), flags)) {
        const auto code = GetLastError();
        std::filesystem::remove(temp, ec);
        return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), "MoveFileExW failed: " + std::to_string(code)});
    }
#else
    if (replace_existing) std::filesystem::rename(temp, *path, ec);
    else std::filesystem::create_hard_link(temp, *path, ec);
    if (!replace_existing || ec) {
        std::error_code cleanup;
        std::filesystem::remove(temp, cleanup);
    }
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error), ec.message()});
#endif
    return {};
}

std::expected<std::vector<std::string>, core::Error> Workspace::tree(std::string_view relative, std::size_t max_entries) const {
    auto start = resolve(relative, true); if (!start) return std::unexpected(start.error());
    std::vector<std::string> out; std::error_code ec;
    if (!std::filesystem::is_directory(*start, ec)) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), "workspace tree target must be a directory"});
    }
    for (std::filesystem::recursive_directory_iterator it(*start, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && out.size() < max_entries; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::filesystem::path relative_path;
        if (!admit_enumerated_entry(*this, it, relative_path)) continue;
        const bool directory = it->is_directory(ec);
        if (ec) { ec.clear(); it.disable_recursion_pending(); continue; }
        out.push_back(relative_path.generic_string() + (directory ? "/" : ""));
    }
    return out;
}

std::expected<std::vector<std::string>, core::Error> Workspace::search_text(std::string_view query, std::size_t max_results) const {
    std::vector<std::string> out; std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && out.size() < max_results; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        std::filesystem::path relative_path;
        if (!admit_enumerated_entry(*this, it, relative_path)) continue;
        if (!it->is_regular_file(ec)) { ec.clear(); continue; }
        const auto size = it->file_size(ec); if (ec || size > 2 * 1024 * 1024) { ec.clear(); continue; }
        std::ifstream in(it->path()); if (!in) continue;
        std::string line; std::size_t n = 0;
        while (std::getline(in, line) && out.size() < max_results) {
            ++n;
            if (line.find(query) == std::string::npos) continue;
            auto end = std::min(line.size(), std::size_t{400});
            while (end > 0 && end < line.size() &&
                   (static_cast<unsigned char>(line[end]) & 0xC0U) == 0x80U) {
                --end;
            }
            out.push_back(relative_path.generic_string() + ":" + std::to_string(n) + ":" + line.substr(0, end));
        }
    }
    return out;
}
} // namespace engcoder::workspace
