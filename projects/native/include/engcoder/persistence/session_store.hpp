#pragma once
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "engcoder/core/error.hpp"
#include "engcoder/core/event.hpp"
#include "engcoder/model/model_provider.hpp"

struct sqlite3;

namespace engcoder::persistence {
using json = nlohmann::json;

class SessionStore {
public:
    explicit SessionStore(const std::filesystem::path& database_path);
    ~SessionStore();
    SessionStore(const SessionStore&) = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    [[nodiscard]] std::expected<void, core::Error> ensure_project(std::string_view project_id, std::string_view root);
    [[nodiscard]] std::expected<void, core::Error> ensure_session(std::string_view session_id, std::string_view project_id, std::string_view title = {});
    [[nodiscard]] std::expected<void, core::Error> append_message(std::string_view session_id, std::string_view parent_message_id, std::string_view message_id, const model::Message& message);
    [[nodiscard]] std::expected<void, core::Error> append_event(const core::Event& event);
    [[nodiscard]] std::expected<std::uint64_t, core::Error> max_event_sequence();
    [[nodiscard]] std::expected<std::vector<core::Event>, core::Error> events_after(std::string_view session_id, std::uint64_t sequence, std::size_t limit = 500);
    [[nodiscard]] std::expected<std::optional<std::string>, core::Error> learnable_reply(
        std::string_view session_id, std::string_view task_id);
    [[nodiscard]] std::expected<std::vector<model::Message>, core::Error> load_messages(std::string_view session_id, std::size_t limit = 256);
    [[nodiscard]] std::expected<void, core::Error> delete_session(std::string_view session_id);
    [[nodiscard]] std::expected<std::vector<json>, core::Error> search_sessions(std::string_view query, std::size_t limit = 50);
private:
    [[nodiscard]] std::expected<void, core::Error> exec_locked(std::string_view sql);
    sqlite3* db_{};
    mutable std::mutex mutex_;
};

} // namespace engcoder::persistence
