module;
#include "module_prelude.hpp"
#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

module threadspire.providers;
import threadspire.core;
import threadspire.models;
import threadspire.platform;

namespace threadspire::ai::backend {
namespace {

[[nodiscard]] const core::json::Value* first_choice(const core::json::Value& document) noexcept {
    const auto* choices = document.find("choices");
    return choices == nullptr ? nullptr : choices->at(0U);
}

[[nodiscard]] std::string response_error(const core::json::Value& document) {
    const auto* error = document.find("error");
    if (error == nullptr) {
        return {};
    }
    if (error->is_string()) {
        return std::string(error->as_string());
    }
    return core::json::string_or(error->find("message"), "local model server returned an error");
}

void append_tool_calls(
    const core::json::Value* calls_value,
    std::map<std::size_t, ToolCall>& calls) {
    const auto* array = calls_value == nullptr ? nullptr : calls_value->as_array();
    if (array == nullptr) {
        return;
    }
    for (std::size_t position = 0; position < array->size(); ++position) {
        const auto& item = (*array)[position];
        const auto index = static_cast<std::size_t>(std::max(
            0.0,
            core::json::number_or(item.find("index"), static_cast<double>(position))));
        auto& call = calls[index];
        const auto id = core::json::string_or(item.find("id"));
        if (!id.empty()) {
            call.id = id;
        }
        const auto* function = item.find("function");
        if (function == nullptr) {
            continue;
        }
        const auto name = core::json::string_or(function->find("name"));
        if (!name.empty()) {
            call.name = name;
        }
        const auto* arguments = function->find("arguments");
        if (arguments == nullptr) {
            continue;
        }
        if (arguments->is_string()) {
            call.arguments_json += arguments->as_string();
        } else {
            call.arguments_json = core::json::stringify(*arguments);
        }
    }
}

[[nodiscard]] core::json::Value::Array build_messages(const CompletionRequest& request) {
    core::json::Value::Array messages;
    if (!request.messages.empty()) {
        for (const auto& message : request.messages) {
            core::json::Value::Object item;
            item.emplace("role", message.role);
            item.emplace("content", message.content);
            if (!message.name.empty()) {
                item.emplace("name", message.name);
            }
            if (!message.tool_call_id.empty()) {
                item.emplace("tool_call_id", message.tool_call_id);
            }
            if (!message.tool_calls_json.empty()) {
                std::string error;
                auto calls = core::json::try_parse(message.tool_calls_json, error);
                if (!calls.has_value() || !calls->is_array()) {
                    throw std::runtime_error("tool_calls_json must contain a JSON array: " + error);
                }
                item.emplace("tool_calls", std::move(*calls));
            }
            messages.emplace_back(std::move(item));
        }
        return messages;
    }
    if (!request.system_prompt.empty()) {
        messages.emplace_back(core::json::Value::Object{
            {"role", "system"},
            {"content", request.system_prompt},
        });
    }
    messages.emplace_back(core::json::Value::Object{
        {"role", "user"},
        {"content", request.prompt},
    });
    return messages;
}

[[nodiscard]] std::optional<core::json::Value> parse_tools(const CompletionRequest& request) {
    if (request.tools_json.empty()) {
        return std::nullopt;
    }
    std::string error;
    auto tools = core::json::try_parse(request.tools_json, error);
    if (!tools.has_value() || !tools->is_array()) {
        throw std::runtime_error("tools_json must contain a valid JSON array: " + error);
    }
    return tools;
}

[[nodiscard]] std::string build_openai_payload(
    const models::ModelProfile& model,
    const CompletionRequest& request) {
    core::json::Value::Object root;
    root.emplace("model", model.model_name);
    root.emplace("messages", build_messages(request));
    root.emplace("max_tokens", static_cast<double>(request.max_output_tokens));
    root.emplace("temperature", request.temperature);
    root.emplace("stream", request.stream);
    if (!request.adapter_path.empty()) {
        root.emplace("adapter_path", request.adapter_path.string());
    }
    if (!request.module_path.empty()) {
        root.emplace("module_path", request.module_path.string());
    }
    if (auto tools = parse_tools(request); tools.has_value()) {
        root.emplace("tools", std::move(*tools));
        root.emplace("tool_choice", "auto");
    }
    return core::json::stringify(core::json::Value(std::move(root)));
}

[[nodiscard]] std::string build_ollama_payload(
    const models::ModelProfile& model,
    const CompletionRequest& request) {
    core::json::Value::Object root;
    root.emplace("model", model.model_name);
    root.emplace("messages", build_messages(request));
    root.emplace("stream", request.stream);
    root.emplace("options", core::json::Value::Object{
        {"temperature", request.temperature},
        {"num_predict", static_cast<double>(request.max_output_tokens)},
    });
    if (auto tools = parse_tools(request); tools.has_value()) {
        root.emplace("tools", std::move(*tools));
    }
    return core::json::stringify(core::json::Value(std::move(root)));
}

void parse_openai_non_stream_response(const std::string_view body, CompletionResult& result) {
    std::string error;
    const auto document = core::json::try_parse(body, error);
    if (!document.has_value()) {
        result.error = "invalid JSON from local model server: " + error;
        return;
    }
    if (const auto server_error = response_error(*document); !server_error.empty()) {
        result.error = server_error;
        return;
    }
    const auto* choice = first_choice(*document);
    if (choice != nullptr) {
        const auto* message = choice->find("message");
        if (message != nullptr) {
            result.content = core::json::string_or(message->find("content"));
            result.hidden_reasoning_only =
                !core::json::string_or(message->find("reasoning_content")).empty();
            std::map<std::size_t, ToolCall> calls;
            append_tool_calls(message->find("tool_calls"), calls);
            for (auto& [index, call] : calls) {
                static_cast<void>(index);
                result.tool_calls.push_back(std::move(call));
            }
        }
        if (result.content.empty()) {
            result.content = core::json::string_or(choice->find("text"));
        }
        result.finish_reason = core::json::string_or(choice->find("finish_reason"));
    }
    if (result.content.empty()) {
        result.content = core::json::string_or(document->find("content"));
    }
    if (result.content.empty()) {
        result.content = core::json::string_or(document->find("response"));
    }
    if (result.content.empty()) {
        result.content = core::json::string_or(document->find("text"));
    }
    if (const auto* usage = document->find("usage"); usage != nullptr) {
        result.prompt_tokens = static_cast<std::size_t>(std::max(
            0.0,
            core::json::number_or(usage->find("prompt_tokens"))));
        result.completion_tokens = static_cast<std::size_t>(std::max(
            0.0,
            core::json::number_or(usage->find("completion_tokens"))));
    }
    result.prompt_tokens = static_cast<std::size_t>(std::max(
        static_cast<double>(result.prompt_tokens),
        core::json::number_or(document->find("prompt_tokens"))));
    result.completion_tokens = static_cast<std::size_t>(std::max(
        static_cast<double>(result.completion_tokens),
        core::json::number_or(document->find("completion_tokens"))));
    result.tokens_per_second = core::json::number_or(document->find("tokens_per_second"));
}

void parse_ollama_document(
    const core::json::Value& document,
    CompletionResult& result,
    std::map<std::size_t, ToolCall>& calls,
    const CompletionRequest& request,
    std::chrono::steady_clock::time_point& first_token_at) {
    if (const auto server_error = response_error(document); !server_error.empty()) {
        result.error = server_error;
        return;
    }
    const auto* message = document.find("message");
    if (message != nullptr) {
        result.hidden_reasoning_only = result.hidden_reasoning_only ||
            !core::json::string_or(message->find("thinking")).empty();
        const auto token = core::json::string_or(message->find("content"));
        if (!token.empty()) {
            if (first_token_at == std::chrono::steady_clock::time_point{}) {
                first_token_at = std::chrono::steady_clock::now();
            }
            result.content += token;
            if (request.on_token && !request.on_token(token)) {
                result.error = "generation cancelled";
                return;
            }
        }
        append_tool_calls(message->find("tool_calls"), calls);
    }
    result.prompt_tokens = static_cast<std::size_t>(std::max(
        static_cast<double>(result.prompt_tokens),
        core::json::number_or(document.find("prompt_eval_count"))));
    result.completion_tokens = static_cast<std::size_t>(std::max(
        static_cast<double>(result.completion_tokens),
        core::json::number_or(document.find("eval_count"))));
    const auto duration_ns = core::json::number_or(document.find("eval_duration"));
    if (duration_ns > 0.0 && result.completion_tokens > 0U) {
        result.tokens_per_second = static_cast<double>(result.completion_tokens) /
                                   (duration_ns / 1'000'000'000.0);
    }
    if (core::json::bool_or(document.find("done"), false)) {
        result.finish_reason = core::json::string_or(document.find("done_reason"), "stop");
    }
}

class JsonLineDecoder final {
public:
    using Callback = std::function<bool(std::string_view)>;

    [[nodiscard]] bool feed(const std::string_view chunk, const Callback& callback) {
        buffer_.append(chunk);
        return consume(callback, false);
    }

    [[nodiscard]] bool finish(const Callback& callback) {
        return consume(callback, true);
    }

private:
    [[nodiscard]] bool consume(const Callback& callback, const bool flush) {
        while (true) {
            const auto newline = buffer_.find('\n');
            if (newline == std::string::npos) {
                if (flush && !buffer_.empty()) {
                    auto line = std::move(buffer_);
                    buffer_.clear();
                    return line.empty() || callback(line);
                }
                return true;
            }
            auto line = buffer_.substr(0U, newline);
            buffer_.erase(0U, newline + 1U);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty() && !callback(line)) {
                return false;
            }
        }
    }

    std::string buffer_;
};

[[nodiscard]] CompletionResult complete_ollama(
    const std::string_view endpoint,
    const models::ModelProfile& model,
    const CompletionRequest& request,
    std::string payload,
    std::vector<HttpHeader> headers) {
    static_cast<void>(model);
    CompletionResult result;
    const auto started = std::chrono::steady_clock::now();
    auto first_token_at = std::chrono::steady_clock::time_point{};
    std::map<std::size_t, ToolCall> calls;
    JsonLineDecoder decoder;

    const auto process = [&](const std::string_view line) {
        if (request.should_stop && request.should_stop()) {
            result.error = "generation cancelled";
            return false;
        }
        std::string error;
        const auto document = core::json::try_parse(line, error);
        if (!document.has_value()) {
            result.error = "invalid JSON from Ollama: " + error;
            return false;
        }
        parse_ollama_document(
            *document,
            result,
            calls,
            request,
            first_token_at);
        return result.error.empty();
    };

    const auto response = http_request(
        HttpRequest{
            .method = "POST",
            .url = models::normalize_local_base_url(endpoint) + "/api/chat",
            .body = std::move(payload),
            .headers = std::move(headers),
            .timeout = std::chrono::minutes(20),
        },
        request.stream
            ? HttpChunkCallback{[&](const std::string_view chunk) {
                return decoder.feed(chunk, process);
            }}
            : HttpChunkCallback{});

    if (request.stream && result.error.empty()) {
        static_cast<void>(decoder.finish(process));
    }
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (first_token_at != std::chrono::steady_clock::time_point{}) {
        result.first_token_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            first_token_at - started);
    }
    if (!response.succeeded()) {
        if (result.error.empty()) {
            result.error = response.error.empty()
                ? "Ollama returned HTTP " + std::to_string(response.status_code) +
                      (response.body.empty() ? std::string{} : ": " + response.body)
                : response.error;
        }
        return result;
    }
    if (!request.stream) {
        std::string error;
        const auto document = core::json::try_parse(response.body, error);
        if (!document.has_value()) {
            result.error = "invalid JSON from Ollama: " + error;
            return result;
        }
        parse_ollama_document(
            *document,
            result,
            calls,
            request,
            first_token_at);
    }
    for (auto& [index, call] : calls) {
        static_cast<void>(index);
        result.tool_calls.push_back(std::move(call));
    }
    if (result.completion_tokens == 0U && !result.content.empty()) {
        result.completion_tokens = static_cast<std::size_t>(std::count(
            result.content.begin(), result.content.end(), ' ')) + 1U;
    }
    if (result.tokens_per_second <= 0.0 && result.elapsed.count() > 0) {
        result.tokens_per_second = static_cast<double>(result.completion_tokens) /
                                   static_cast<double>(result.elapsed.count()) * 1000.0;
    }
    result.hidden_reasoning_only = result.hidden_reasoning_only && result.content.empty();
    result.succeeded = result.error.empty() &&
                       (!result.content.empty() || !result.tool_calls.empty());
    if (!result.succeeded && result.error.empty()) {
        result.error = result.hidden_reasoning_only
            ? "local AI used its first answer budget for private reasoning and returned no final answer"
            : "Ollama returned no generated content";
    }
    return result;
}

} // namespace

std::string json_escape(const std::string_view value) {
    return core::json::escape(value);
}

bool OpenAiSseDecoder::feed(
    const std::string_view chunk,
    const EventCallback& callback) {
    buffer_.append(chunk);
    return consume_lines(callback, false);
}

bool OpenAiSseDecoder::finish(const EventCallback& callback) {
    return consume_lines(callback, true);
}

bool OpenAiSseDecoder::consume_lines(
    const EventCallback& callback,
    const bool flush) {
    while (true) {
        const auto newline = buffer_.find('\n');
        if (newline == std::string::npos) {
            if (flush && !buffer_.empty()) {
                auto line = std::move(buffer_);
                buffer_.clear();
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.starts_with("data:")) {
                    auto payload = std::string_view(line).substr(5U);
                    while (!payload.empty() && payload.front() == ' ') {
                        payload.remove_prefix(1U);
                    }
                    return callback(payload);
                }
            }
            return true;
        }
        auto line = buffer_.substr(0U, newline);
        buffer_.erase(0U, newline + 1U);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.starts_with("data:")) {
            continue;
        }
        auto payload = std::string_view(line).substr(5U);
        while (!payload.empty() && payload.front() == ' ') {
            payload.remove_prefix(1U);
        }
        if (!callback(payload)) {
            return false;
        }
    }
}

CompletionResult CompletionClient::complete(
    const std::string_view internal_base_url,
    const models::ModelProfile& model,
    const CompletionRequest& request) const {
    CompletionResult result;
    const auto endpoint = model.endpoint == "internal"
        ? std::string(internal_base_url)
        : model.endpoint;
    if (endpoint.empty()) {
        result.error = "model endpoint is empty";
        return result;
    }

    std::string payload;
    try {
        payload = model.protocol == "ollama"
            ? build_ollama_payload(model, request)
            : build_openai_payload(model, request);
    } catch (const std::exception& exception) {
        result.error = exception.what();
        return result;
    }

    std::vector<HttpHeader> headers{
        {.name = "Content-Type", .value = "application/json; charset=utf-8"},
        {.name = "Accept", .value = request.stream ? "text/event-stream, application/json" : "application/json"},
    };
    if (!model.api_key.empty()) {
        headers.push_back({.name = "Authorization", .value = "Bearer " + model.api_key});
    }
    if (model.protocol == "ollama") {
        return complete_ollama(endpoint, model, request, std::move(payload), std::move(headers));
    }

    const auto started = std::chrono::steady_clock::now();
    auto first_token_at = std::chrono::steady_clock::time_point{};
    std::map<std::size_t, ToolCall> streamed_calls;
    OpenAiSseDecoder decoder;
    bool saw_sse_event = false;

    const auto process_event = [&](const std::string_view event) {
        saw_sse_event = true;
        if (event == "[DONE]") {
            return true;
        }
        std::string parse_error;
        const auto document = core::json::try_parse(event, parse_error);
        if (!document.has_value()) {
            result.error = "invalid SSE JSON from local model server: " + parse_error;
            return false;
        }
        if (const auto server_error = response_error(*document); !server_error.empty()) {
            result.error = server_error;
            return false;
        }
        const auto* choice = first_choice(*document);
        if (choice == nullptr) {
            return true;
        }
        const auto* delta = choice->find("delta");
        std::string token;
        if (delta != nullptr) {
            result.hidden_reasoning_only = result.hidden_reasoning_only ||
                !core::json::string_or(delta->find("reasoning_content")).empty();
            token = core::json::string_or(delta->find("content"));
            if (token.empty()) {
                token = core::json::string_or(delta->find("text"));
            }
            append_tool_calls(delta->find("tool_calls"), streamed_calls);
        }
        if (token.empty()) {
            token = core::json::string_or(choice->find("text"));
        }
        if (token.empty()) {
            const auto* message = choice->find("message");
            if (message != nullptr) {
                result.hidden_reasoning_only = result.hidden_reasoning_only ||
                    !core::json::string_or(message->find("reasoning_content")).empty();
                token = core::json::string_or(message->find("content"));
            }
        }
        if (!token.empty()) {
            if (first_token_at == std::chrono::steady_clock::time_point{}) {
                first_token_at = std::chrono::steady_clock::now();
            }
            result.content += token;
            if (request.on_token && !request.on_token(token)) {
                result.error = "generation cancelled";
                return false;
            }
        }
        const auto finish = core::json::string_or(choice->find("finish_reason"));
        if (!finish.empty()) {
            result.finish_reason = finish;
        }
        return !(request.should_stop && request.should_stop());
    };

    const auto response = http_request(
        HttpRequest{
            .method = "POST",
            .url = models::openai_chat_url(endpoint),
            .body = std::move(payload),
            .headers = std::move(headers),
            .timeout = std::chrono::minutes(20),
        },
        request.stream
            ? HttpChunkCallback{[&](const std::string_view chunk) {
                if (request.should_stop && request.should_stop()) {
                    result.error = "generation cancelled";
                    return false;
                }
                return decoder.feed(chunk, process_event);
            }}
            : HttpChunkCallback{});

    if (request.stream && result.error.empty()) {
        static_cast<void>(decoder.finish(process_event));
    }
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    if (first_token_at != std::chrono::steady_clock::time_point{}) {
        result.first_token_latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            first_token_at - started);
    }

    if (!response.succeeded()) {
        if (result.error.empty()) {
            result.error = response.error.empty()
                ? "local model server returned HTTP " + std::to_string(response.status_code) +
                      (response.body.empty() ? std::string{} : ": " + response.body)
                : response.error;
        }
        return result;
    }

    if (!saw_sse_event) {
        parse_openai_non_stream_response(response.body, result);
        if (!result.content.empty() && request.on_token) {
            static_cast<void>(request.on_token(result.content));
        }
    } else {
        for (auto& [index, call] : streamed_calls) {
            static_cast<void>(index);
            result.tool_calls.push_back(std::move(call));
        }
    }

    if (result.finish_reason == "low_confidence") {
        result.content.clear();
        result.tool_calls.clear();
        result.error =
            "ThreadSpire rejected a repetitive or structurally corrupt internal-model draft before display";
    }
    if (result.completion_tokens == 0U && !result.content.empty()) {
        result.completion_tokens = static_cast<std::size_t>(std::count(
            result.content.begin(), result.content.end(), ' ')) + 1U;
    }
    if (result.tokens_per_second <= 0.0 && result.elapsed.count() > 0) {
        result.tokens_per_second = static_cast<double>(result.completion_tokens) /
                                   static_cast<double>(result.elapsed.count()) * 1000.0;
    }
    result.hidden_reasoning_only = result.hidden_reasoning_only && result.content.empty();
    result.succeeded = result.error.empty() &&
                       (!result.content.empty() || !result.tool_calls.empty());
    if (!result.succeeded && result.error.empty()) {
        result.error = result.hidden_reasoning_only
            ? "local AI used its first answer budget for private reasoning and returned no final answer"
            : "local model server returned no generated content";
    }
    return result;
}

} // namespace threadspire::ai::backend
