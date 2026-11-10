#include "engcoder/workspace/workspace.hpp"
#include "engcoder/core/ids.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace engcoder::workspace {
namespace {
bool is_ignored(const std::filesystem::path& p) {
    for (const auto& part : p) {
        const auto s = part.string();
        if (s == ".git" || s == ".engcoder" || s == "out" || s == "build" || s == "node_modules" || s == "vcpkg_installed") return true;
    }
    return false;
}
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
    for (std::filesystem::recursive_directory_iterator it(*start, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && out.size() < max_entries; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const auto rel = std::filesystem::relative(it->path(), root_, ec); if (ec) { ec.clear(); continue; }
        if (is_ignored(rel)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
        out.push_back(rel.generic_string() + (it->is_directory() ? "/" : ""));
    }
    return out;
}

std::expected<std::vector<std::string>, core::Error> Workspace::search_text(std::string_view query, std::size_t max_results) const {
    std::vector<std::string> out; std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root_, std::filesystem::directory_options::skip_permission_denied, ec), end; it != end && out.size() < max_results; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        const auto rel = std::filesystem::relative(it->path(), root_, ec); if (ec) { ec.clear(); continue; }
        if (is_ignored(rel)) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
        if (!it->is_regular_file()) continue;
        const auto size = it->file_size(ec); if (ec || size > 2 * 1024 * 1024) { ec.clear(); continue; }
        std::ifstream in(it->path()); if (!in) continue;
        std::string line; std::size_t n = 0;
        while (std::getline(in, line) && out.size() < max_results) { ++n; if (line.find(query) != std::string::npos) out.push_back(rel.generic_string() + ":" + std::to_string(n) + ":" + line.substr(0, 400)); }
    }
    return out;
}
} // namespace engcoder::workspace
