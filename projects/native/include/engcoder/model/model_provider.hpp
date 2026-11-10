#pragma once
#include <chrono>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <stop_token>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>
#include "engcoder/core/error.hpp"

namespace engcoder::model {
using json = nlohmann::json;

[[nodiscard]] inline std::chrono::milliseconds inference_budget_remaining(
    std::chrono::steady_clock::time_point deadline,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) noexcept {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        return std::chrono::milliseconds::max();
    }
    if (deadline <= now) return std::chrono::milliseconds{0};
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
}

struct Message {
    std::string role;
    std::string content;
    json tool_calls = json::array();
    std::string tool_call_id;
};

struct ToolCall {
    std::string id;
    std::string name;
    json arguments = json::object();
};

struct ModelRequest {
    std::vector<Message> messages;
    std::vector<json> tools;
    // Canonical OpenAI Chat Completions tool choice. Named functions are stored
    // as {"type":"function","function":{"name":"..."}} and translated by
    // provider adapters when their wire protocol differs. null means the caller
    // did not specify a tool choice and must not be rewritten to "auto".
    json tool_choice = nullptr;
    double temperature = 0.15;
    int max_tokens = -1;
    std::optional<bool> parallel_tool_calls;
    std::string model;
    std::stop_token stop;
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    std::string request_id;
};

struct ModelResponse {
    std::string text;
    std::string reasoning;
    std::vector<ToolCall> tool_calls;
    json raw;
    std::string model;
};

enum class StreamEventKind {
    text_delta,
    reasoning_delta,
    tool_call_started,
    tool_argument_delta
};

struct StreamEvent {
    StreamEventKind kind{};
    std::size_t index{};
    std::string tool_call_id;
    std::string tool_name;
    std::string delta;
};

using StreamCallback = std::function<void(const StreamEvent&)>;

struct ProviderCapabilities {
    bool streaming{};
    bool custom_tools{};
    bool stateful_chat{};
    bool remote_mcp{};
    bool native_model_management{};
};

class IModelProvider {
public:
    virtual ~IModelProvider() = default;
    [[nodiscard]] virtual std::expected<ModelResponse, core::Error> complete(const ModelRequest&) = 0;
    [[nodiscard]] virtual std::expected<ModelResponse, core::Error> complete_stream(const ModelRequest& request, const StreamCallback& callback) {
        auto response = complete(request);
        if (response && callback && !response->text.empty()) {
            callback(StreamEvent{
                .kind = StreamEventKind::text_delta,
                .index = 0,
                .tool_call_id = {},
                .tool_name = {},
                .delta = response->text,
            });
        }
        return response;
    }
    [[nodiscard]] virtual ProviderCapabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::expected<json, core::Error> discover() = 0;
    [[nodiscard]] virtual std::expected<json, core::Error> prepare(std::string_view model) {
        return json{{"model", std::string{model}}, {"management", "provider-managed"}};
    }
    [[nodiscard]] virtual std::expected<json, core::Error> residency() {
        return json{{"management", "provider-managed"}};
    }
    [[nodiscard]] virtual std::expected<json, core::Error> release_idle() {
        return json{{"management", "provider-managed"}};
    }
};

} // namespace engcoder::model
