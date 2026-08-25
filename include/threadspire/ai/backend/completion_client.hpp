#pragma once

#include "threadspire/ai/models/model_registry.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::backend {

struct ChatMessage final {
    std::string role;
    std::string content;
    std::string name;
    std::string tool_call_id;
    std::string tool_calls_json;
};

struct ToolCall final {
    std::string id;
    std::string name;
    std::string arguments_json;
};

using TokenCallback = std::function<bool(std::string_view)>;
using CompletionStopPredicate = std::function<bool()>;

struct CompletionRequest final {
    std::string system_prompt;
    std::string prompt;
    std::vector<ChatMessage> messages;
    std::size_t max_output_tokens{256};
    double temperature{0.2};
    std::filesystem::path adapter_path;
    std::filesystem::path module_path;
    std::string tools_json;
    bool stream{true};
    TokenCallback on_token;
    CompletionStopPredicate should_stop;
};

struct CompletionResult final {
    bool succeeded{false};
    std::string content;
    std::string error;
    std::string finish_reason;
    std::vector<ToolCall> tool_calls;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    double tokens_per_second{0.0};
    std::chrono::milliseconds first_token_latency{0};
    std::chrono::milliseconds elapsed{0};
    bool hidden_reasoning_only{false};
};

class CompletionClient final {
public:
    [[nodiscard]] CompletionResult complete(
        std::string_view internal_base_url,
        const models::ModelProfile& model,
        const CompletionRequest& request) const;
};

class OpenAiSseDecoder final {
public:
    using EventCallback = std::function<bool(std::string_view)>;

    [[nodiscard]] bool feed(std::string_view chunk, const EventCallback& callback);
    [[nodiscard]] bool finish(const EventCallback& callback);

private:
    [[nodiscard]] bool consume_lines(const EventCallback& callback, bool flush);
    std::string buffer_;
};

[[nodiscard]] std::string json_escape(std::string_view value);

} // namespace threadspire::ai::backend
