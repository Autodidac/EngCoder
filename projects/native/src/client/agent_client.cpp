#include "engcoder/client/agent_client.hpp"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace engcoder::client {
namespace {

[[nodiscard]] std::string environment_value(const char* name, std::string fallback) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return fallback;
    }
    std::string result{value};
    std::free(value);
    return result.empty() ? fallback : result;
#else
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
#endif
}

[[nodiscard]] std::string request_error(
    std::string_view operation,
    const httplib::Result& result) {
    if (!result) {
        return std::string{operation} + " failed: " + httplib::to_string(result.error());
    }
    return std::string{operation} + " returned HTTP " + std::to_string(result->status) +
        (result->body.empty() ? std::string{} : ": " + result->body);
}

} // namespace

AgentClient::AgentClient()
    : AgentClient(environment_value("ENGCODER_AGENT_URL", "http://127.0.0.1:14321")) {}

AgentClient::AgentClient(std::string base_url)
    : base_url_(std::move(base_url)) {
    while (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

AgentClient::Endpoint AgentClient::endpoint() const {
    constexpr std::string_view http_prefix{"http://"};
    constexpr std::string_view https_prefix{"https://"};

    std::string_view value{base_url_};
    if (value.starts_with(http_prefix)) {
        value.remove_prefix(http_prefix.size());
    } else if (value.starts_with(https_prefix)) {
        throw std::runtime_error("HTTPS EngCoder endpoints are not enabled in the local client yet.");
    }

    const auto slash = value.find('/');
    const auto authority = value.substr(0, slash);
    const auto colon = authority.rfind(':');

    Endpoint result;
    if (colon == std::string_view::npos) {
        result.host = authority;
        result.port = 80;
    } else {
        result.host = authority.substr(0, colon);
        result.port = std::stoi(std::string{authority.substr(colon + 1)});
    }
    return result;
}

std::expected<json, std::string> AgentClient::health() const {
    try {
        const auto ep = endpoint();
        httplib::Client http(ep.host, ep.port);
        http.set_connection_timeout(2);
        http.set_read_timeout(10);
        const auto response = http.Get("/health");
        if (!response || response->status < 200 || response->status >= 300) {
            return std::unexpected(request_error("GET /health", response));
        }
        return json::parse(response->body);
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<json, std::string> AgentClient::model_discovery() const {
    try {
        const auto ep = endpoint();
        httplib::Client http(ep.host, ep.port);
        http.set_connection_timeout(2);
        http.set_read_timeout(15);
        const auto response = http.Get("/api/model/discovery");
        if (!response || response->status < 200 || response->status >= 300) {
            return std::unexpected(request_error("GET /api/model/discovery", response));
        }
        return json::parse(response->body);
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<Submission, std::string> AgentClient::submit(
    std::string_view project_id,
    std::string_view session_id,
    std::string_view message) const {
    try {
        const auto ep = endpoint();
        httplib::Client http(ep.host, ep.port);
        http.set_connection_timeout(3);
        http.set_read_timeout(10);
        http.set_write_timeout(10);

        const json body{
            {"project_id", project_id},
            {"session_id", session_id},
            {"message", message}
        };
        const auto response = http.Post("/api/tasks", body.dump(), "application/json");
        if (!response || response->status != 202) {
            return std::unexpected(request_error("POST /api/tasks", response));
        }

        const auto payload = json::parse(response->body);
        return Submission{
            .project_id = payload.value("project_id", std::string{project_id}),
            .session_id = payload.value("session_id", std::string{session_id}),
            .task_id = payload.value("task_id", std::string{})
        };
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<std::vector<Event>, std::string> AgentClient::events_after(
    std::string_view session_id,
    std::uint64_t after) const {
    try {
        const auto ep = endpoint();
        httplib::Client http(ep.host, ep.port);
        http.set_connection_timeout(2);
        http.set_read_timeout(10);

        std::ostringstream path;
        path << "/api/sessions/" << session_id << "/events?after=" << after;
        const auto response = http.Get(path.str());
        if (!response || response->status < 200 || response->status >= 300) {
            return std::unexpected(request_error("GET session events", response));
        }

        const auto payload = json::parse(response->body);
        std::vector<Event> result;
        for (const auto& item : payload.value("events", json::array())) {
            result.push_back(Event{
                .sequence = item.value("sequence", std::uint64_t{0}),
                .type = item.value("type", std::string{}),
                .task_id = item.value("task_id", std::string{}),
                .data = item.value("data", json::object())
            });
        }
        return result;
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<void, std::string> AgentClient::cancel(std::string_view task_id) const {
    try {
        const auto ep = endpoint();
        httplib::Client http(ep.host, ep.port);
        http.set_connection_timeout(2);
        http.set_read_timeout(10);
        const auto path = "/api/tasks/" + std::string{task_id} + "/cancel";
        const auto response = http.Post(path.c_str(), "", "application/json");
        if (!response || response->status < 200 || response->status >= 300) {
            return std::unexpected(request_error("POST task cancel", response));
        }
        return {};
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<TaskOutcome, std::string> AgentClient::run_task(
    std::string_view project_id,
    std::string_view session_id,
    std::string_view message,
    std::stop_token stop,
    EventCallback on_event,
    std::chrono::milliseconds poll_interval) const {
    std::uint64_t after = 0;
    if (auto existing = events_after(session_id, 0); existing && !existing->empty()) {
        after = existing->back().sequence;
    }

    auto submission = submit(project_id, session_id, message);
    if (!submission) {
        return std::unexpected(submission.error());
    }

    unsigned consecutive_poll_failures = 0;
    for (;;) {
        if (stop.stop_requested()) {
            (void)cancel(submission->task_id);
            return TaskOutcome{
                .submission = *submission,
                .terminal = TaskTerminal::cancelled,
                .final_text = {},
                .terminal_data = json{{"cancelled_locally", true}},
            };
        }

        auto events = events_after(submission->session_id, after);
        if (!events) {
            ++consecutive_poll_failures;
            if (consecutive_poll_failures >= 40U) {
                return std::unexpected(
                    "EngCoder event stream remained unavailable: " + events.error());
            }
            std::this_thread::sleep_for(poll_interval);
            continue;
        }
        consecutive_poll_failures = 0;

        for (const auto& event : *events) {
            after = std::max(after, event.sequence);
            if (event.task_id != submission->task_id) {
                continue;
            }

            if (on_event) {
                on_event(event);
            }

            if (event.type == "task.completed") {
                return TaskOutcome{
                    .submission = *submission,
                    .terminal = TaskTerminal::completed,
                    .final_text = event.data.value("result", std::string{}),
                    .terminal_data = event.data,
                };
            }
            if (event.type == "task.failed") {
                return TaskOutcome{
                    .submission = *submission,
                    .terminal = TaskTerminal::failed,
                    .final_text = {},
                    .terminal_data = event.data,
                };
            }
            if (event.type == "task.cancelled") {
                return TaskOutcome{
                    .submission = *submission,
                    .terminal = TaskTerminal::cancelled,
                    .final_text = {},
                    .terminal_data = event.data,
                };
            }
            if (event.type == "task.awaiting_approval") {
                return TaskOutcome{
                    .submission = *submission,
                    .terminal = TaskTerminal::awaiting_approval,
                    .final_text = {},
                    .terminal_data = event.data,
                };
            }
        }

        std::this_thread::sleep_for(poll_interval);
    }
}

std::string AgentClient::make_session_id(std::string_view prefix) {
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::random_device random;
    const auto entropy = (static_cast<std::uint64_t>(random()) << 32U) | random();

    std::ostringstream out;
    out << prefix << '-' << std::hex << now << '-' << entropy;
    return out.str();
}

} // namespace engcoder::client
