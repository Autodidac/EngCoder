#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include "engcoder/agent/agent_runtime.hpp"

namespace engcoder::agent {

class TaskScheduler {
public:
    explicit TaskScheduler(AgentRuntime& runtime, std::size_t workers = 1);
    ~TaskScheduler();
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    [[nodiscard]] std::expected<void, core::Error> submit(TaskRequest request);
    [[nodiscard]] bool cancel(std::string_view task_id);
    [[nodiscard]] bool resolve_approval(std::string_view task_id, std::string_view call_id, bool approved);
private:
    void worker(std::stop_token stop);
    AgentRuntime& runtime_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<TaskRequest> queue_;
    std::unordered_map<std::string, std::stop_source> active_;
    std::vector<std::jthread> workers_;
};

} // namespace engcoder::agent
