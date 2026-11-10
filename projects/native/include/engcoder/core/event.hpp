#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace engcoder::core {
using json = nlohmann::json;

struct Event {
    std::uint64_t sequence{};
    std::chrono::system_clock::time_point timestamp{};
    std::string project_id;
    std::string session_id;
    std::string task_id;
    std::string type;
    json data = json::object();
};

class EventBus {
public:
    using Sink = std::function<void(const Event&)>;
    std::uint64_t subscribe(Sink sink);
    void unsubscribe(std::uint64_t id);
    void publish(const Event& event) const;
private:
    mutable std::mutex mutex_;
    mutable std::uint64_t next_id_{1};
    std::unordered_map<std::uint64_t, Sink> sinks_;
};

} // namespace engcoder::core
