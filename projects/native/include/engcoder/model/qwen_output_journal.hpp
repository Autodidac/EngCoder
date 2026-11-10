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
// The most recently started invocation is retained independently of the
// event ring. Clients can reopen the panel after completion or recover when
// event pruning removed its start marker. Memory is bounded to 96 KiB.
struct QwenOutputLatest {
    std::string request_id;
    std::string model;
    std::string output;
    bool generating{};
    // A new invocation does not own the displayed output until it has
    // emitted an actual provider fragment. This prevents empty, failed, or
    // merely preparing requests from erasing the previous completed output.
    bool has_current_output{};
};
struct QwenOutputSnapshot {
    std::uint64_t cursor{};
    std::vector<QwenOutputEvent> events;
    QwenOutputLatest latest;
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
        latest_ = QwenOutputLatest{id, std::string{model}, latest_.output, true, false};
        append_locked(id, model, "start", {});
        return id;
    }

    void append(std::string_view id, std::string_view model, std::string_view kind, std::string_view delta) {
        if (delta.empty()) return;
        std::scoped_lock lock{mutex_};
        // Only the most recently started invocation owns the displayed snapshot.
        // Older overlapping calls must not overwrite the new call's output.
        if (id == latest_.request_id && latest_.generating) {
            if (!latest_.has_current_output) {
                latest_.output.clear();
                latest_.has_current_output = true;
            }
            constexpr std::size_t output_limit = 96 * 1024;
            if (delta.size() >= output_limit) {
                latest_.output.assign(delta.substr(utf8_boundary_after(delta, delta.size() - output_limit)));
            } else {
                latest_.output.append(delta);
                if (latest_.output.size() > output_limit) {
                    latest_.output.erase(0, utf8_boundary_after(
                        latest_.output, latest_.output.size() - output_limit));
                }
            }
        }
        // Split provider chunks to keep each response and retained event bounded.
        constexpr std::size_t chunk_size = 4096;
        for (std::size_t offset = 0; offset < delta.size();) {
            std::size_t end = std::min(offset + chunk_size, delta.size());
            // Avoid splitting valid UTF-8 codepoints: nlohmann::json requires
            // strings in the response to contain complete UTF-8 sequences.
            if (end < delta.size()) {
                while (end > offset && is_utf8_continuation(delta[end])) --end;
            }
            if (end == offset) end = std::min(offset + chunk_size, delta.size());
            append_locked(id, model, kind, delta.substr(offset, end - offset));
            offset = end;
        }
    }

    void finish(std::string_view id, std::string_view model) {
        std::scoped_lock lock{mutex_};
        if (id == latest_.request_id) latest_.generating = false;
        append_locked(id, model, "finish", {});
    }

    [[nodiscard]] QwenOutputSnapshot after(std::uint64_t sequence) const {
        std::scoped_lock lock{mutex_};
        QwenOutputSnapshot snapshot;
        snapshot.cursor = next_sequence_;
        snapshot.latest = latest_;
        for (const auto& event : events_) {
            if (event.sequence > sequence) snapshot.events.push_back(event);
        }
        return snapshot;
    }

private:
    [[nodiscard]] static bool is_utf8_continuation(char byte) noexcept {
        return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
    }
    [[nodiscard]] static std::size_t utf8_boundary_after(std::string_view text, std::size_t offset) noexcept {
        while (offset < text.size() && is_utf8_continuation(text[offset])) ++offset;
        return offset;
    }

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
    QwenOutputLatest latest_;
    std::uint64_t next_sequence_{};
    std::uint64_t next_id_{};
    std::size_t stored_bytes_{};
};

} // namespace engcoder::model
