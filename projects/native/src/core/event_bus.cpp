#include "engcoder/core/event.hpp"

namespace engcoder::core {
std::uint64_t EventBus::subscribe(Sink sink) {
    std::scoped_lock lock(mutex_);
    const auto id = next_id_++;
    sinks_.emplace(id, std::move(sink));
    return id;
}
void EventBus::unsubscribe(std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    sinks_.erase(id);
}
void EventBus::publish(const Event& event) const {
    std::vector<Sink> copy;
    {
        std::scoped_lock lock(mutex_);
        copy.reserve(sinks_.size());
        for (const auto& [_, sink] : sinks_) copy.push_back(sink);
    }
    for (const auto& sink : copy) sink(event);
}

} // namespace engcoder::core
