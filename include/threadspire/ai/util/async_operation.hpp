#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace threadspire::ai::util {

enum class OperationState {
    idle,
    running,
    succeeded,
    failed,
    cancelled,
};

struct OperationResult final {
    OperationState state{OperationState::idle};
    int exit_code{-1};
    std::string message;
};

class AsyncOperation final {
public:
    using Task = std::function<OperationResult(std::stop_token)>;

    AsyncOperation() = default;
    ~AsyncOperation();

    AsyncOperation(const AsyncOperation&) = delete;
    AsyncOperation& operator=(const AsyncOperation&) = delete;
    AsyncOperation(AsyncOperation&&) = delete;
    AsyncOperation& operator=(AsyncOperation&&) = delete;

    [[nodiscard]] bool start(Task task, std::string initial_message);
    void cancel();
    void wait();
    [[nodiscard]] bool running() const;
    [[nodiscard]] OperationResult snapshot() const;
    void set_message(std::string message);

private:
    mutable std::mutex mutex_;
    OperationResult result_{};
    std::jthread worker_{};
};

[[nodiscard]] std::string_view to_string(OperationState state) noexcept;

} // namespace threadspire::ai::util
