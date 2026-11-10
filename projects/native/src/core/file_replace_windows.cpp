#include "engcoder/core/file_replace.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace engcoder::core {
std::error_code replace_file(const std::filesystem::path& staged,
                             const std::filesystem::path& destination) noexcept {
    if (MoveFileExW(staged.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {};
    return {static_cast<int>(GetLastError()), std::system_category()};
}
} // namespace engcoder::core
#endif
