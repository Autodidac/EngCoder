#pragma once
#include <cstdint>
#include <filesystem>

namespace engcoder::core {
// Matches the Python worker's learning_policy.policy_lock protocol.
// Never unlink the lock file: another process may already have it open.
class LearningPolicyLock final {
public:
    explicit LearningPolicyLock(const std::filesystem::path& state_root);
    ~LearningPolicyLock();
    LearningPolicyLock(const LearningPolicyLock&) = delete;
    LearningPolicyLock& operator=(const LearningPolicyLock&) = delete;
private:
    std::intptr_t handle_{-1};
};
}
