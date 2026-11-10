#include "engcoder/agent/task_scheduler.hpp"

#include <algorithm>
#include <utility>

namespace engcoder::agent {

TaskScheduler::TaskScheduler(AgentRuntime& runtime, std::size_t workers)
    : runtime_(runtime) {
    workers = std::max<std::size_t>(1, workers);
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        workers_.emplace_back([this](std::stop_token stop) { worker(stop); });
    }
}

TaskScheduler::~TaskScheduler() {
    {
        std::scoped_lock lock(mutex_);
        for (auto& [_, source] : active_) {
            source.request_stop();
        }
    }

    for (auto& worker_thread : workers_) {
        worker_thread.request_stop();
    }
    cv_.notify_all();
}

std::expected<void, core::Error> TaskScheduler::submit(TaskRequest request) {
    std::scoped_lock lock(mutex_);
    if (active_.contains(request.task_id)) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::invalid_argument),
            "duplicate task id",
        });
    }

    active_.emplace(request.task_id, std::stop_source{});
    queue_.push_back(std::move(request));
    cv_.notify_one();
    return {};
}

bool TaskScheduler::cancel(std::string_view task_id) {
    std::scoped_lock lock(mutex_);
    const auto it = active_.find(std::string{task_id});
    if (it == active_.end()) return false;
    it->second.request_stop();
    cv_.notify_all();
    return true;
}

bool TaskScheduler::resolve_approval(
    std::string_view task_id,
    std::string_view call_id,
    bool approved) {
    {
        std::scoped_lock lock(mutex_);
        if (!active_.contains(std::string{task_id})) return false;
    }
    return runtime_.resolve_approval(task_id, call_id, approved);
}

void TaskScheduler::worker(std::stop_token scheduler_stop) {
    while (!scheduler_stop.stop_requested()) {
        TaskRequest task;
        std::stop_token task_stop;

        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, scheduler_stop, [&] { return !queue_.empty(); });
            if (scheduler_stop.stop_requested()) break;

            task = std::move(queue_.front());
            queue_.pop_front();

            const auto active = active_.find(task.task_id);
            if (active == active_.end()) {
                continue;
            }
            task_stop = active->second.get_token();
        }

        (void)runtime_.run(task, task_stop);

        {
            std::scoped_lock lock(mutex_);
            active_.erase(task.task_id);
        }
    }
}

} // namespace engcoder::agent
