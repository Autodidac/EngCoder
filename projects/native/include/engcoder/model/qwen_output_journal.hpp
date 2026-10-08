#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace engcoder::model {

// Bounded, process-local generated-output feed shared by task, OpenAI API and
// tool/MCP clients using EngCoder as their Qwen inference endpoint.
// Transport fragments are NOT individual tokenizer IDs.
struct QwenOutputEvent {
    std::uint64_t sequence{};
    std::string request_id;
    std::string model;
    std::string kind; // start, text, reasoning, arguments, finish
    std::string delta;
};
struct QwenOutputSnapshot {
    std::uint64_t cursor{};
    std::vector<QwenOutputEvent> events;
};

class QwenOutputJournal final {
public:
    [[nodiscard]] std::string begin(std::string_view model, std::string_view request_id = {}) {
        std::scoped_lock lock{mutex_};
        // Give internal task calls distinct identities without relying on the
        // caller having populated ModelRequest::request_id.
        const std::string id = request_id.empty()
            ? "qwen-" + std::to_string(++next_id_)
            : std::string{request_id};
        append_locked(id, model, "start", {});
        return id;
    }

    void append(std::string_view id, std::string_view model, std::string_view kind, std::string_view delta) {
        if (delta.empty()) return;
        std::scoped_lock lock{mutex_};
        // Split provider chunks to keep each response and retained event bounded.
        constexpr std::size_t chunk_size = 4096;
        for (std::size_t offset = 0; offset < delta.size(); offset += chunk_size) {
            append_locked(id, model, kind, delta.substr(offset, chunk_size));
        }
    }

    void finish(std::string_view id, std::string_view model) {
        std::scoped_lock lock{mutex_};
        append_locked(id, model, "finish", {});
    }

    [[nodiscard]] QwenOutputSnapshot after(std::uint64_t sequence) const {
        std::scoped_lock lock{mutex_};
        QwenOutputSnapshot snapshot;
        snapshot.cursor = next_sequence_;
        for (const auto& event : events_) {
            if (event.sequence > sequence) snapshot.events.push_back(event);
        }
        return snapshot;
    }

private:
    void append_locked(std::string_view id, std::string_view model, std::string_view kind, std::string_view delta) {
        events_.push_back(QwenOutputEvent{
            ++next_sequence_, std::string{id}, std::string{model}, std::string{kind}, std::string{delta}
        });
        stored_bytes_ += delta.size();
        // Never retain unbounded model output in process memory.
        while (events_.size() > 2048 || stored_bytes_ > 512 * 1024) {
            stored_bytes_ -= events_.front().delta.size();
            events_.pop_front();
        }
    }
    mutable std::mutex mutex_;
    std::deque<QwenOutputEvent> events_;
    std::uint64_t next_sequence_{};
    std::uint64_t next_id_{};
    std::size_t stored_bytes_{};
};

} // namespace engcoder::model
