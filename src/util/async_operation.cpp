#include "threadspire/ai/util/async_operation.hpp"

#include <exception>
#include <utility>

namespace threadspire::ai::util {

AsyncOperation::~AsyncOperation() {
    cancel();
    wait();
}

bool AsyncOperation::start(Task task, std::string initial_message) {
    std::lock_guard lock(mutex_);
    if (result_.state == OperationState::running) {
        return false;
    }

    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }

    result_ = OperationResult{
        .state = OperationState::running,
        .exit_code = -1,
        .message = std::move(initial_message),
    };

    worker_ = std::jthread([this, task = std::move(task)](const std::stop_token stop_token) mutable {
        OperationResult completed;
        try {
            completed = task(stop_token);
            if (stop_token.stop_requested() && completed.state == OperationState::running) {
                completed.state = OperationState::cancelled;
                completed.message = "operation cancelled";
            }
        } catch (const std::exception& exception) {
            completed = OperationResult{
                .state = OperationState::failed,
                .exit_code = -1,
                .message = exception.what(),
            };
        } catch (...) {
            completed = OperationResult{
                .state = OperationState::failed,
                .exit_code = -1,
                .message = "unknown operation failure",
            };
        }

        std::lock_guard result_lock(mutex_);
        result_ = std::move(completed);
    });
    return true;
}

void AsyncOperation::cancel() {
    if (worker_.joinable()) {
        worker_.request_stop();
    }
}

void AsyncOperation::wait() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool AsyncOperation::running() const {
    std::lock_guard lock(mutex_);
    return result_.state == OperationState::running;
}

OperationResult AsyncOperation::snapshot() const {
    std::lock_guard lock(mutex_);
    return result_;
}

void AsyncOperation::set_message(std::string message) {
    std::lock_guard lock(mutex_);
    if (result_.state == OperationState::running) {
        result_.message = std::move(message);
    }
}

std::string_view to_string(const OperationState state) noexcept {
    switch (state) {
    case OperationState::idle: return "Idle";
    case OperationState::running: return "Running";
    case OperationState::succeeded: return "Succeeded";
    case OperationState::failed: return "Failed";
    case OperationState::cancelled: return "Cancelled";
    }
    return "Unknown";
}

} // namespace threadspire::ai::util
