#include "engcoder/core/learning_policy_lock.hpp"
#ifndef _WIN32
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace engcoder::core {
LearningPolicyLock::LearningPolicyLock(const std::filesystem::path& state_root) {
    const auto path = state_root / "learning" / "policy.lock";
    std::filesystem::create_directories(path.parent_path());
    const auto file = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (file < 0) throw std::runtime_error("Cannot open learning policy lock");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (::flock(file, LOCK_EX | LOCK_NB) != 0) {
        const auto error = errno;
        if ((error != EWOULDBLOCK && error != EAGAIN && error != EINTR) ||
            std::chrono::steady_clock::now() >= deadline) {
            ::close(file);
            throw std::runtime_error("Cannot acquire learning policy lock: " + std::to_string(error));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
    handle_ = file;
}
LearningPolicyLock::~LearningPolicyLock() {
    if (handle_ >= 0) {
        ::flock(static_cast<int>(handle_), LOCK_UN);
        ::close(static_cast<int>(handle_));
    }
}
}
#endif
