#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace engcoder::client {

using json = nlohmann::json;

struct Event {
    std::uint64_t sequence{};
    std::string type;
    std::string task_id;
    json data = json::object();
};

struct Submission {
    std::string project_id;
    std::string session_id;
    std::string task_id;
};

enum class TaskTerminal {
    completed,
    failed,
    cancelled,
    awaiting_approval,
};

struct TaskOutcome {
    Submission submission;
    TaskTerminal terminal{TaskTerminal::completed};
    std::string final_text;
    json terminal_data = json::object();
};

using EventCallback = std::function<void(const Event&)>;

class AgentClient {
public:
    AgentClient();
    explicit AgentClient(std::string base_url);

    [[nodiscard]] const std::string& base_url() const noexcept { return base_url_; }

    [[nodiscard]] std::expected<json, std::string> health() const;
    [[nodiscard]] std::expected<json, std::string> model_discovery() const;
    [[nodiscard]] std::expected<Submission, std::string> submit(
        std::string_view project_id,
        std::string_view session_id,
        std::string_view message) const;
    [[nodiscard]] std::expected<std::vector<Event>, std::string> events_after(
        std::string_view session_id,
        std::uint64_t after) const;
    [[nodiscard]] std::expected<void, std::string> cancel(std::string_view task_id) const;

    [[nodiscard]] std::expected<TaskOutcome, std::string> run_task(
        std::string_view project_id,
        std::string_view session_id,
        std::string_view message,
        std::stop_token stop = {},
        EventCallback on_event = {},
        std::chrono::milliseconds poll_interval = std::chrono::milliseconds{75}) const;

    [[nodiscard]] static std::string make_session_id(std::string_view prefix = "session");

private:
    struct Endpoint {
        std::string host;
        int port{};
    };

    [[nodiscard]] Endpoint endpoint() const;

    std::string base_url_;
};

} // namespace engcoder::client
