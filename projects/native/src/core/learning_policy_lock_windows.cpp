#include "engcoder/core/learning_policy_lock.hpp"
#ifdef _WIN32
#include <windows.h>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace engcoder::core {
LearningPolicyLock::LearningPolicyLock(const std::filesystem::path& state_root) {
    const auto path = state_root / "learning" / "policy.lock";
    std::filesystem::create_directories(path.parent_path());
    const auto file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open learning policy lock");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    for (;;) {
        OVERLAPPED position{};
        if (LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &position)) {
            handle_ = reinterpret_cast<std::intptr_t>(file);
            return;
        }
        const auto error = GetLastError();
        if (error != ERROR_LOCK_VIOLATION || std::chrono::steady_clock::now() >= deadline) {
            CloseHandle(file);
            throw std::runtime_error("Cannot acquire learning policy lock: " + std::to_string(error));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}
LearningPolicyLock::~LearningPolicyLock() {
    if (handle_ != -1) {
        const auto file = reinterpret_cast<HANDLE>(handle_);
        OVERLAPPED position{};
        UnlockFileEx(file, 0, 1, 0, &position);
        CloseHandle(file);
    }
}
}
#endif
