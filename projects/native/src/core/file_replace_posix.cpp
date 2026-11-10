#include "engcoder/core/file_replace.hpp"

#ifndef _WIN32
namespace engcoder::core {
std::error_code replace_file(const std::filesystem::path& staged,
                             const std::filesystem::path& destination) noexcept {
    std::error_code ec;
    std::filesystem::rename(staged, destination, ec);
    return ec;
}
} // namespace engcoder::core
#endif
