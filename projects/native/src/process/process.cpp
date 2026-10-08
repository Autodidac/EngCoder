#include "engcoder/process/process.hpp"
namespace engcoder::process {
std::expected<Result, core::Error> run(const Request& request, std::stop_token stop) {
    if (request.executable.empty()) return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), "executable is empty"});
#ifdef _WIN32
    return detail::run_windows(request, stop);
#else
    return detail::run_posix(request, stop);
#endif
}
}
