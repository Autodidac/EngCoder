#include "engcoder/model/lm_studio_provider.hpp"
#include "engcoder/core/ids.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>

namespace engcoder::model {
namespace {

void ensure_curl_initialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* text = static_cast<std::string*>(userdata);
    text->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string join_url(std::string base, std::string_view path) {
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + std::string(path);
}

[[nodiscard]] bool is_loopback_url(std::string_view url) noexcept {
    constexpr std::string_view http_ipv4 = "http://127.0.0.1";
    constexpr std::string_view https_ipv4 = "https://127.0.0.1";
    constexpr std::string_view http_localhost = "http://localhost";
    constexpr std::string_view https_localhost = "https://localhost";
    constexpr std::string_view http_ipv6 = "http://[::1]";
    constexpr std::string_view https_ipv6 = "https://[::1]";
    return url.starts_with(http_ipv4) || url.starts_with(https_ipv4) ||
           url.starts_with(http_localhost) || url.starts_with(https_localhost) ||
           url.starts_with(http_ipv6) || url.starts_with(https_ipv6);
}

void configure_local_transport(CURL* curl, std::string_view url) {
    // libcurl honors proxy environment variables on Windows and Unix. Local
    // model servers must never be routed through a corporate/VPN/system proxy.
    if (is_loopback_url(url)) {
        curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

struct CurlStopContext {
    std::stop_token stop;
};

int transfer_progress_cb(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    const auto* context = static_cast<const CurlStopContext*>(userdata);
    return context != nullptr && context->stop.stop_requested() ? 1 : 0;
}

void configure_cancellation(CURL* curl, CurlStopContext& context) {
    if (!context.stop.stop_possible()) return;
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &context);
}

[[nodiscard]] core::Error cancelled_error() {
    return core::Error{core::make_error_code(core::Errc::cancelled), "cancelled"};
}

[[nodiscard]] bool deadline_expired(std::chrono::steady_clock::time_point deadline) noexcept {
    return deadline != std::chrono::steady_clock::time_point::max() &&
           std::chrono::steady_clock::now() >= deadline;
}

[[nodiscard]] long remaining_timeout_ms(
    std::chrono::steady_clock::time_point deadline,
    std::chrono::milliseconds fallback) noexcept {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        const auto count = fallback.count();
        return static_cast<long>(std::min<std::int64_t>(count, std::numeric_limits<long>::max()));
    }
    const auto remaining = inference_budget_remaining(deadline).count();
    if (remaining <= 0) return 0L;
    return static_cast<long>(std::min<std::int64_t>(remaining, std::numeric_limits<long>::max()));
}

[[nodiscard]] core::Error timeout_error(std::string_view phase) {
    return core::Error{
        core::make_error_code(core::Errc::model_timeout),
        std::string{phase} + " exceeded the inference deadline"
    };
}

[[nodiscard]] std::string lower_copy(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] bool looks_like_context_overflow(std::string_view text) {
    const auto lower = lower_copy(std::string{text});
    return lower.find("context") != std::string::npos &&
           (lower.find("overflow") != std::string::npos ||
            lower.find("too long") != std::string::npos ||
            lower.find("maximum context") != std::string::npos ||
            lower.find("context length") != std::string::npos ||
            lower.find("n_ctx") != std::string::npos);
}

[[nodiscard]] core::Error curl_failure(CURLcode rc, std::string_view phase) {
    if (rc == CURLE_OPERATION_TIMEDOUT) return timeout_error(phase);
    if (rc == CURLE_RECV_ERROR || rc == CURLE_SEND_ERROR || rc == CURLE_GOT_NOTHING ||
        rc == CURLE_PARTIAL_FILE) {
        return core::Error{
            core::make_error_code(core::Errc::model_connection_reset),
            std::string{phase} + " connection reset: " + curl_easy_strerror(rc)
        };
    }
    if (rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST) {
        return core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            std::string{phase} + " could not reach the model server: " + curl_easy_strerror(rc)
        };
    }
    return core::Error{
        core::make_error_code(core::Errc::model_generation_error),
        std::string{phase} + " transport failed: " + curl_easy_strerror(rc)
    };
}

[[nodiscard]] core::Error http_failure(long status, std::string_view response, std::string_view phase) {
    if (status == 401 || status == 403) {
        return core::Error{
            core::make_error_code(core::Errc::authentication_error),
            std::string{phase} + " authentication failed (HTTP " + std::to_string(status) + ")"
        };
    }
    if (status == 408 || status == 504) return timeout_error(phase);
    if (status == 400 || status == 413 || status == 422) {
        if (looks_like_context_overflow(response)) {
            return core::Error{
                core::make_error_code(core::Errc::model_context_overflow),
                std::string{phase} + " exceeded the model context window"
            };
        }
        return core::Error{
            core::make_error_code(core::Errc::invalid_argument),
            std::string{phase} + " rejected the request parameters (HTTP " + std::to_string(status) + ")"
        };
    }
    if (status == 404 || status == 409 || status == 429 || status == 503) {
        return core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            std::string{phase} + " model service is unavailable (HTTP " + std::to_string(status) + ")"
        };
    }
    return core::Error{
        core::make_error_code(core::Errc::model_generation_error),
        std::string{phase} + " failed upstream (HTTP " + std::to_string(status) + ")"
    };
}

[[nodiscard]] bool is_auto_model(std::string_view model) {
    return model.empty() || model == "auto" || model == "local-model";
}

struct StreamedToolCall {
    std::string id;
    std::string name;
    std::string arguments;
    std::size_t emitted_argument_bytes{};
    bool announced{};
};

void announce_tool(StreamedToolCall& call, std::size_t index, const StreamCallback& callback) {
    if (call.announced || call.name.empty() || call.id.empty()) {
        return;
    }
    call.announced = true;
    if (callback) {
        callback(StreamEvent{
            .kind = StreamEventKind::tool_call_started,
            .index = index,
            .tool_call_id = call.id,
            .tool_name = call.name,
            .delta = {},
        });
        if (call.emitted_argument_bytes < call.arguments.size()) {
            callback(StreamEvent{
                .kind = StreamEventKind::tool_argument_delta,
                .index = index,
                .tool_call_id = call.id,
                .tool_name = call.name,
                .delta = call.arguments.substr(call.emitted_argument_bytes),
            });
            call.emitted_argument_bytes = call.arguments.size();
        }
    }
}

struct ChatStreamState {
    StreamCallback callback;
    std::string pending;
    std::string text;
    std::string reasoning;
    std::map<std::size_t, StreamedToolCall> calls;
    std::string parser_error;
    json last_chunk;
};

void consume_chat_data(ChatStreamState& state, std::string_view payload) {
    if (payload.empty() || payload == "[DONE]") {
        return;
    }

    const auto chunk = json::parse(payload);
    state.last_chunk = chunk;
    if (!chunk.contains("choices") || chunk["choices"].empty()) {
        return;
    }

    const auto& delta = chunk["choices"][0].value("delta", json::object());
    if (delta.contains("content") && delta["content"].is_string()) {
        const auto fragment = delta["content"].get<std::string>();
        state.text += fragment;
        if (state.callback && !fragment.empty()) {
            state.callback(StreamEvent{
                .kind = StreamEventKind::text_delta,
                .index = 0,
                .tool_call_id = {},
                .tool_name = {},
                .delta = fragment,
            });
        }
    }

    const auto reasoning_fragment = delta.contains("reasoning_content") && delta["reasoning_content"].is_string()
        ? delta["reasoning_content"].get<std::string>()
        : delta.contains("reasoning") && delta["reasoning"].is_string()
            ? delta["reasoning"].get<std::string>()
            : std::string{};
    if (!reasoning_fragment.empty()) {
        state.reasoning += reasoning_fragment;
        if (state.callback) {
            state.callback(StreamEvent{
                .kind = StreamEventKind::reasoning_delta,
                .index = 0,
                .tool_call_id = {},
                .tool_name = {},
                .delta = reasoning_fragment,
            });
        }
    }

    for (const auto& raw_call : delta.value("tool_calls", json::array())) {
        const auto index = raw_call.value("index", 0U);
        auto& call = state.calls[index];
        if (raw_call.contains("id") && raw_call["id"].is_string()) {
            call.id += raw_call["id"].get<std::string>();
        }

        if (raw_call.contains("function") && raw_call["function"].is_object()) {
            const auto& function = raw_call["function"];
            if (function.contains("name") && function["name"].is_string()) {
                call.name += function["name"].get<std::string>();
            }
            announce_tool(call, index, state.callback);

            if (function.contains("arguments") && function["arguments"].is_string()) {
                const auto fragment = function["arguments"].get<std::string>();
                call.arguments += fragment;
                if (call.announced && state.callback && !fragment.empty()) {
                    state.callback(StreamEvent{
                        .kind = StreamEventKind::tool_argument_delta,
                        .index = index,
                        .tool_call_id = call.id,
                        .tool_name = call.name,
                        .delta = fragment,
                    });
                    call.emitted_argument_bytes = call.arguments.size();
                }
            }
        }
    }
}

template <class State, class Consume>
void consume_sse_lines(State& state, bool flush, Consume&& consume) {
    for (;;) {
        const auto newline = state.pending.find('\n');
        if (newline == std::string::npos) {
            break;
        }

        auto line = state.pending.substr(0, newline);
        state.pending.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.starts_with("data:")) {
            continue;
        }

        auto data = std::string_view(line).substr(5);
        while (!data.empty() && data.front() == ' ') {
            data.remove_prefix(1);
        }
        consume(state, data);
    }

    if (flush && !state.pending.empty()) {
        auto line = std::exchange(state.pending, {});
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("data:")) {
            auto data = std::string_view(line).substr(5);
            while (!data.empty() && data.front() == ' ') {
                data.remove_prefix(1);
            }
            consume(state, data);
        }
    }
}

size_t chat_stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept {
    auto* state = static_cast<ChatStreamState*>(userdata);
    const auto bytes = size * nmemb;
    try {
        state->pending.append(ptr, bytes);
        consume_sse_lines(*state, false, consume_chat_data);
        return bytes;
    } catch (const std::exception& error) {
        state->parser_error = error.what();
        return 0;
    }
}

struct ResponsesStreamState {
    StreamCallback callback;
    std::string pending;
    std::string text;
    std::string reasoning;
    std::map<std::size_t, StreamedToolCall> calls;
    std::map<std::string, std::size_t> item_indices;
    std::string parser_error;
    json last_chunk;
};

std::size_t response_call_index(ResponsesStreamState& state, const json& event) {
    if (event.contains("output_index")) {
        return event.value("output_index", std::size_t{0});
    }
    const auto item_id = event.value("item_id", std::string{});
    if (!item_id.empty()) {
        if (const auto it = state.item_indices.find(item_id); it != state.item_indices.end()) {
            return it->second;
        }
    }
    return state.calls.size();
}

void consume_responses_data(ResponsesStreamState& state, std::string_view payload) {
    if (payload.empty() || payload == "[DONE]") {
        return;
    }

    const auto event = json::parse(payload);
    state.last_chunk = event;
    const auto type = event.value("type", std::string{});

    if (type == "response.output_text.delta") {
        const auto fragment = event.value("delta", std::string{});
        state.text += fragment;
        if (state.callback && !fragment.empty()) {
            state.callback(StreamEvent{
                .kind = StreamEventKind::text_delta,
                .index = event.value("output_index", 0U),
                .tool_call_id = {},
                .tool_name = {},
                .delta = fragment,
            });
        }
        return;
    }

    if (type == "response.reasoning_text.delta" || type == "response.reasoning_summary_text.delta") {
        const auto fragment = event.value("delta", std::string{});
        state.reasoning += fragment;
        if (state.callback && !fragment.empty()) {
            state.callback(StreamEvent{
                .kind = StreamEventKind::reasoning_delta,
                .index = event.value("output_index", 0U),
                .tool_call_id = {},
                .tool_name = {},
                .delta = fragment,
            });
        }
        return;
    }

    if (type == "response.output_item.added" || type == "response.output_item.done") {
        const auto item = event.value("item", json::object());
        if (item.value("type", std::string{}) != "function_call") {
            return;
        }

        const auto index = event.value("output_index", state.calls.size());
        auto& call = state.calls[index];
        call.id = item.value("call_id", item.value("id", call.id));
        call.name = item.value("name", call.name);
        if (item.contains("arguments") && item["arguments"].is_string()) {
            call.arguments = item["arguments"].get<std::string>();
        }
        const auto item_id = item.value("id", std::string{});
        if (!item_id.empty()) {
            state.item_indices[item_id] = index;
        }
        announce_tool(call, index, state.callback);
        return;
    }

    if (type == "response.function_call_arguments.delta") {
        const auto index = response_call_index(state, event);
        auto& call = state.calls[index];
        const auto fragment = event.value("delta", std::string{});
        call.arguments += fragment;
        if (call.announced && state.callback && !fragment.empty()) {
            state.callback(StreamEvent{
                .kind = StreamEventKind::tool_argument_delta,
                .index = index,
                .tool_call_id = call.id,
                .tool_name = call.name,
                .delta = fragment,
            });
            call.emitted_argument_bytes = call.arguments.size();
        }
        return;
    }

    if (type == "response.function_call_arguments.done") {
        const auto index = response_call_index(state, event);
        auto& call = state.calls[index];
        if (event.contains("arguments") && event["arguments"].is_string()) {
            call.arguments = event["arguments"].get<std::string>();
        }
    }
}

size_t responses_stream_cb(char* ptr, size_t size, size_t nmemb, void* userdata) noexcept {
    auto* state = static_cast<ResponsesStreamState*>(userdata);
    const auto bytes = size * nmemb;
    try {
        state->pending.append(ptr, bytes);
        consume_sse_lines(*state, false, consume_responses_data);
        return bytes;
    } catch (const std::exception& error) {
        state->parser_error = error.what();
        return 0;
    }
}

[[nodiscard]] json responses_tool_choice(const json& choice) {
    if (choice.is_null() || choice.is_string()) return choice;
    if (!choice.is_object()) return choice;
    if (choice.value("type", std::string{}) != "function") return choice;

    if (choice.contains("function") && choice["function"].is_object()) {
        const auto name = choice["function"].value("name", std::string{});
        return json{{"type", "function"}, {"name", name}};
    }
    return choice;
}

json to_chat_body(std::string_view model, const ModelRequest& request, bool stream) {
    json body{
        {"model", model},
        {"temperature", request.temperature},
        {"stream", stream}
    };
    if (request.parallel_tool_calls.has_value()) {
        body["parallel_tool_calls"] = *request.parallel_tool_calls;
    }

    body["messages"] = json::array();
    for (const auto& message : request.messages) {
        json item{{"role", message.role}, {"content", message.content}};
        if (!message.tool_calls.empty()) {
            item["tool_calls"] = message.tool_calls;
        }
        if (!message.tool_call_id.empty()) {
            item["tool_call_id"] = message.tool_call_id;
        }
        body["messages"].push_back(std::move(item));
    }

    if (request.max_tokens > 0) body["max_tokens"] = request.max_tokens;

    if (!request.tools.empty()) body["tools"] = request.tools;
    if (!request.tool_choice.is_null()) body["tool_choice"] = request.tool_choice;
    return body;
}

json to_responses_body(std::string_view model, const ModelRequest& request, bool stream) {
    json input = json::array();
    for (const auto& message : request.messages) {
        if (message.role == "tool") {
            input.push_back({
                {"type", "function_call_output"},
                {"call_id", message.tool_call_id},
                {"output", message.content}
            });
            continue;
        }

        input.push_back({{"role", message.role}, {"content", message.content}});
        if (!message.tool_calls.empty()) {
            for (const auto& raw_call : message.tool_calls) {
                if (!raw_call.contains("function")) {
                    continue;
                }
                const auto& function = raw_call["function"];
                input.push_back({
                    {"type", "function_call"},
                    {"call_id", raw_call.value("id", std::string{})},
                    {"name", function.value("name", std::string{})},
                    {"arguments", function.value("arguments", std::string{"{}"})}
                });
            }
        }
    }

    json body{
        {"model", model},
        {"input", input},
        {"temperature", request.temperature},
        {"stream", stream}
    };
    if (request.parallel_tool_calls.has_value()) {
        body["parallel_tool_calls"] = *request.parallel_tool_calls;
    }

    if (request.max_tokens > 0) body["max_output_tokens"] = request.max_tokens;

    if (!request.tools.empty()) {
        body["tools"] = json::array();
        for (const auto& tool : request.tools) {
            const auto& function = tool.at("function");
            json flattened = function;
            flattened["type"] = "function";
            body["tools"].push_back(std::move(flattened));
        }
    }
    if (!request.tool_choice.is_null()) {
        body["tool_choice"] = responses_tool_choice(request.tool_choice);
    }
    return body;
}

std::optional<std::string> select_native_model(const json& root) {
    if (!root.contains("models") || !root["models"].is_array()) {
        return std::nullopt;
    }

    const json* best_loaded_tool = nullptr;
    const json* best_loaded = nullptr;
    const json* best_tool = nullptr;
    const json* best_any = nullptr;

    for (const auto& model : root["models"]) {
        if (model.value("type", std::string{}) != "llm") {
            continue;
        }
        if (!best_any) {
            best_any = &model;
        }
        const bool loaded = model.contains("loaded_instances") && model["loaded_instances"].is_array() && !model["loaded_instances"].empty();
        const bool tool = model.value("capabilities", json::object()).value("trained_for_tool_use", false);
        if (loaded && tool && !best_loaded_tool) best_loaded_tool = &model;
        if (loaded && !best_loaded) best_loaded = &model;
        if (tool && !best_tool) best_tool = &model;
    }

    const json* selected = best_loaded_tool ? best_loaded_tool : best_loaded ? best_loaded : best_tool ? best_tool : best_any;
    if (!selected) {
        return std::nullopt;
    }
    const auto key = selected->value("key", std::string{});
    return key.empty() ? std::nullopt : std::optional<std::string>{key};
}

std::optional<std::string> select_openai_model(const json& root) {
    if (!root.contains("data") || !root["data"].is_array()) {
        return std::nullopt;
    }
    for (const auto& model : root["data"]) {
        const auto id = model.value("id", std::string{});
        if (!id.empty()) {
            return id;
        }
    }
    return std::nullopt;
}

json normalize_native_models(const json& root) {
    json models = json::array();
    for (const auto& model : root.value("models", json::array())) {
        if (model.value("type", std::string{}) != "llm") {
            continue;
        }
        const auto caps = model.value("capabilities", json::object());
        models.push_back({
            {"id", model.value("key", std::string{})},
            {"display_name", model.value("display_name", std::string{})},
            {"loaded", model.contains("loaded_instances") && model["loaded_instances"].is_array() && !model["loaded_instances"].empty()},
            {"tool_use", caps.value("trained_for_tool_use", false)},
            {"max_context_length", model.value("max_context_length", 0)},
            {"architecture", model.value("architecture", std::string{})},
            {"format", model.value("format", std::string{})}
        });
    }
    return models;
}

json normalize_openai_models(const json& root) {
    json models = json::array();
    for (const auto& model : root.value("data", json::array())) {
        const auto id = model.value("id", std::string{});
        if (!id.empty()) {
            models.push_back({{"id", id}});
        }
    }
    return models;
}

core::Error combined_error(std::string message, const std::vector<std::string>& details) {
    std::ostringstream out;
    out << message;
    for (const auto& detail : details) {
        out << "\n  - " << detail;
    }
    return core::Error{core::make_error_code(core::Errc::model_unavailable), out.str()};
}

} // namespace

LmStudioProvider::LmStudioProvider(core::Config config)
    : config_(std::move(config)) {
    ensure_curl_initialized();
}


void LmStudioProvider::set_token(std::string token) {
    std::scoped_lock lock(auth_mutex_);
    config_.lm_studio_token = std::move(token);
}

bool LmStudioProvider::token_configured() const {
    std::scoped_lock lock(auth_mutex_);
    return !config_.lm_studio_token.empty();
}

std::string LmStudioProvider::authorization_header() const {
    std::scoped_lock lock(auth_mutex_);
    if (config_.lm_studio_token.empty()) return {};
    return "Authorization: Bearer " + config_.lm_studio_token;
}

ProviderCapabilities LmStudioProvider::capabilities() const noexcept {
    return {
        .streaming = true,
        .custom_tools = true,
        .stateful_chat = true,
        .remote_mcp = true,
        .native_model_management = true
    };
}

std::expected<json, core::Error> LmStudioProvider::http_json(
    std::string_view path,
    const json& body,
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) const {
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model request"));

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "curl initialization failed"
        });
    }

    std::string response;
    const auto url = join_url(config_.lm_studio_url, path);
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (const auto auth = authorization_header(); !auth.empty()) {
        headers = curl_slist_append(headers, auth.c_str());
    }

    const auto timeout_ms = remaining_timeout_ms(deadline, std::chrono::seconds{static_cast<std::int64_t>(config_.inference_timeout_seconds)});
    if (timeout_ms <= 0) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(timeout_error("model request"));
    }
    const auto connect_timeout_ms = std::min<long>(5000L, timeout_ms);

    const auto payload = body.dump();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    configure_local_transport(curl, url);
    CurlStopContext stop_context{stop};
    configure_cancellation(curl, stop_context);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    const auto rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (stop.stop_requested() || rc == CURLE_ABORTED_BY_CALLBACK) {
            return std::unexpected(cancelled_error());
        }
        return std::unexpected(curl_failure(rc, "model request"));
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(http_failure(status, response, "model request"));
    }

    try {
        return json::parse(response);
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            "Model server returned invalid JSON: " + std::string{error.what()}
        });
    }
}

std::expected<json, core::Error> LmStudioProvider::http_get_json(
    std::string_view path,
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) const {
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model discovery"));

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "curl initialization failed"
        });
    }

    std::string response;
    const auto url = join_url(config_.lm_studio_url, path);
    struct curl_slist* headers = nullptr;
    if (const auto auth = authorization_header(); !auth.empty()) {
        headers = curl_slist_append(headers, auth.c_str());
    }

    // Discovery/health stays short even though generation has a multi-hour budget.
    const auto timeout_ms = std::min<long>(10000L, remaining_timeout_ms(deadline, std::chrono::seconds{10}));
    if (timeout_ms <= 0) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(timeout_error("model discovery"));
    }
    const auto connect_timeout_ms = std::min<long>(3000L, timeout_ms);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    configure_local_transport(curl, url);
    CurlStopContext stop_context{stop};
    configure_cancellation(curl, stop_context);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    const auto rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (stop.stop_requested() || rc == CURLE_ABORTED_BY_CALLBACK) {
            return std::unexpected(cancelled_error());
        }
        return std::unexpected(curl_failure(rc, "model discovery"));
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(http_failure(status, response, "model discovery"));
    }

    try {
        return json::parse(response);
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            "Model discovery returned invalid JSON: " + std::string{error.what()}
        });
    }
}

std::expected<json, core::Error> LmStudioProvider::discover_with_deadline(
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) {
    std::vector<std::string> failures;
    const auto native = http_get_json("/api/v1/models", stop, deadline);
    if (!native) failures.push_back(native.error().message);
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model discovery"));

    const auto openai = http_get_json("/v1/models", stop, deadline);
    if (!openai) failures.push_back(openai.error().message);
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model discovery"));

    const auto legacy = (!native && !openai)
        ? http_get_json("/api/v0/models", stop, deadline)
        : std::expected<json, core::Error>{std::unexpected(core::Error{})};
    if (!native && !openai && !legacy) failures.push_back(legacy.error().message);

    if (!native && !openai && !legacy) {
        return std::unexpected(combined_error(
            "Local model API is not reachable at " + config_.lm_studio_url +
            ". Start the configured local model server or update EngCoder's provider endpoint.",
            failures));
    }

    json out{
        {"reachable", true},
        {"base_url", config_.lm_studio_url},
        {"native_v1", native.has_value()},
        {"openai_compatible", openai.has_value()},
        {"legacy_v0", legacy.has_value()}
    };

    std::optional<std::string> selected;
    if (native) {
        out["models"] = normalize_native_models(*native);
        out["native"] = *native;
        selected = select_native_model(*native);
    } else if (openai) {
        out["models"] = normalize_openai_models(*openai);
        selected = select_openai_model(*openai);
    } else if (legacy) {
        out["models"] = normalize_openai_models(*legacy);
        selected = select_openai_model(*legacy);
    }

    if (!is_auto_model(config_.model)) selected = config_.model;
    if (selected) {
        {
            std::scoped_lock lock(resolved_model_mutex_);
            resolved_model_ = *selected;
        }
        out["selected_model"] = *selected;
    } else {
        out["selected_model"] = nullptr;
    }

    out["backend"] = native && openai
        ? "lm-runtime-native+openai"
        : openai ? "bionic-or-openai-compatible" : "legacy-lm-runtime";
    out["preferred_agent_transport"] = openai ? "chat_completions" : native ? "native_chat_no_custom_tools" : "legacy";
    return out;
}

std::expected<json, core::Error> LmStudioProvider::discover() {
    return discover_with_deadline({}, std::chrono::steady_clock::time_point::max());
}

std::expected<std::string, core::Error> LmStudioProvider::effective_model(
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) {
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model routing"));

    {
        std::scoped_lock lock(resolved_model_mutex_);
        if (!resolved_model_.empty()) return resolved_model_;
        if (!is_auto_model(config_.model)) {
            resolved_model_ = config_.model;
            return resolved_model_;
        }
    }

    auto discovery = discover_with_deadline(stop, deadline);
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model routing"));
    if (!discovery) return std::unexpected(discovery.error());

    std::scoped_lock lock(resolved_model_mutex_);
    if (resolved_model_.empty()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "The local model server is reachable, but no LLM model is available"
        });
    }
    return resolved_model_;
}

std::expected<std::string, core::Error> LmStudioProvider::model_for_request(const ModelRequest& request) {
    if (request.stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(request.deadline)) return std::unexpected(timeout_error("model routing"));
    if (!is_auto_model(request.model)) return request.model;
    return effective_model(request.stop, request.deadline);
}

std::expected<void, core::Error> LmStudioProvider::ensure_model_loaded(
    std::string_view model,
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) {
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model loading"));
    if (!config_.model_auto_load || model.empty() || is_auto_model(model)) return {};

    std::scoped_lock lock(model_load_mutex_);
    const auto models = http_get_json("/api/v1/models", stop, deadline);
    if (!models) {
        if (models.error().code == core::make_error_code(core::Errc::cancelled) ||
            models.error().code == core::make_error_code(core::Errc::model_timeout) ||
            models.error().code == core::make_error_code(core::Errc::authentication_error)) {
            return std::unexpected(models.error());
        }
        // Generic OpenAI-compatible providers may not expose model management.
        return {};
    }

    const auto models_it = models->find("models");
    if (!models->is_object() || models_it == models->end() || !models_it->is_array()) return {};

    bool installed = false;
    bool already_loaded = false;
    for (const auto& item : *models_it) {
        if (!item.is_object() || item.value("key", std::string{}) != model) continue;
        installed = true;
        const auto instances_it = item.find("loaded_instances");
        already_loaded = instances_it != item.end() && instances_it->is_array() && !instances_it->empty();
        break;
    }
    if (!installed || already_loaded) return {};
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("model loading"));

    auto loaded_result = http_json("/api/v1/models/load", json{{"model", model}}, stop, deadline);
    if (!loaded_result) {
        if (loaded_result.error().code == core::make_error_code(core::Errc::cancelled) ||
            loaded_result.error().code == core::make_error_code(core::Errc::model_timeout) ||
            loaded_result.error().code == core::make_error_code(core::Errc::authentication_error)) {
            return std::unexpected(loaded_result.error());
        }
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_loading_failed),
            "Model '" + std::string{model} + "' could not be loaded: " + loaded_result.error().message
        });
    }
    return {};
}

void LmStudioProvider::mark_generation_termination_uncertain() noexcept {
    generation_termination_uncertain_.store(true, std::memory_order_release);
}

std::expected<void, core::Error> LmStudioProvider::ensure_generation_slot_available(
    std::stop_token stop,
    std::chrono::steady_clock::time_point deadline) {
    if (!generation_termination_uncertain_.load(std::memory_order_acquire)) return {};
    if (stop.stop_requested()) return std::unexpected(cancelled_error());
    if (deadline_expired(deadline)) return std::unexpected(timeout_error("generation slot recovery"));

    // llama-server exposes /slots. If an upstream does not expose equivalent
    // state, EngCoder refuses to overlap a new request after an unconfirmed
    // timeout/cancellation instead of creating duplicate generation.
    const auto slots = http_get_json("/slots", stop, deadline);
    if (!slots || !slots->is_array()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Previous generation ended without confirmed upstream termination; generation may still be running. Wait for the upstream slot to become idle or restart the model runtime before retrying."
        });
    }

    const bool busy = std::ranges::any_of(*slots, [](const json& slot) {
        if (!slot.is_object()) return false;
        if (slot.value("is_processing", false)) return true;
        const auto state = lower_copy(slot.value("state", std::string{}));
        return state == "processing" || state == "generating" || state == "busy";
    });
    if (busy) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Previous generation is still occupying the upstream slot; duplicate inference was blocked"
        });
    }

    generation_termination_uncertain_.store(false, std::memory_order_release);
    return {};
}

std::expected<json, core::Error> LmStudioProvider::native_chat(
    std::string_view input,
    const std::vector<std::string>& integrations) {
    auto model = effective_model();
    if (!model) return std::unexpected(model.error());
    auto loaded = ensure_model_loaded(*model);
    if (!loaded) return std::unexpected(loaded.error());

    json body{
        {"model", *model},
        {"input", input},
        {"store", false},
        {"temperature", 0.1}
    };
    if (!integrations.empty()) {
        body["integrations"] = integrations;
    }
    return http_json("/api/v1/chat", body);
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete(const ModelRequest& request) {
    auto bounded = request;
    if (bounded.deadline == std::chrono::steady_clock::time_point::max()) {
        bounded.deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{config_.inference_timeout_seconds};
    }
    if (deadline_expired(bounded.deadline)) return std::unexpected(timeout_error("inference"));
    if (auto slot = ensure_generation_slot_available(bounded.stop, bounded.deadline); !slot) {
        return std::unexpected(slot.error());
    }

    if (config_.model_api == core::ModelApi::responses) return complete_responses(bounded);
    // Exactly one generation. Never retry the same prompt through a different
    // protocol after a late transport/parser failure.
    return complete_chat(bounded);
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete_stream(
    const ModelRequest& request,
    const StreamCallback& callback) {
    auto bounded = request;
    if (bounded.deadline == std::chrono::steady_clock::time_point::max()) {
        bounded.deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds{config_.inference_timeout_seconds};
    }
    if (deadline_expired(bounded.deadline)) return std::unexpected(timeout_error("inference"));
    if (auto slot = ensure_generation_slot_available(bounded.stop, bounded.deadline); !slot) {
        return std::unexpected(slot.error());
    }

    if (config_.model_api == core::ModelApi::responses) return complete_responses_stream(bounded, callback);
    return complete_chat_stream(bounded, callback);
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete_chat(const ModelRequest& request) {
    auto model = model_for_request(request);
    if (!model) return std::unexpected(model.error());
    auto loaded = ensure_model_loaded(*model, request.stop, request.deadline);
    if (!loaded) return std::unexpected(loaded.error());

    auto response = http_json("/v1/chat/completions", to_chat_body(*model, request, false), request.stop, request.deadline);
    if (!response) {
        if (response.error().code == core::make_error_code(core::Errc::cancelled) ||
            response.error().code == core::make_error_code(core::Errc::model_timeout) ||
            response.error().code == core::make_error_code(core::Errc::model_connection_reset)) {
            mark_generation_termination_uncertain();
        }
        return std::unexpected(response.error());
    }

    try {
        const auto& message = response->at("choices").at(0).at("message");
        ModelResponse out;
        out.model = *model;
        out.raw = *response;
        out.text = message.value("content", std::string{});
        out.reasoning = message.value("reasoning_content", message.value("reasoning", std::string{}));

        for (const auto& raw_call : message.value("tool_calls", json::array())) {
            ToolCall call;
            call.id = raw_call.value("id", std::string{});
            if (call.id.empty()) {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::model_protocol_error),
                    "Upstream returned a tool call without an id"
                });
            }
            const auto& function = raw_call.at("function");
            call.name = function.at("name").get<std::string>();
            const auto arguments = function.value("arguments", json::object());
            call.arguments = arguments.is_string()
                ? json::parse(arguments.get<std::string>())
                : arguments;
            out.tool_calls.push_back(std::move(call));
        }
        return out;
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            error.what()
        });
    }
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete_chat_stream(
    const ModelRequest& request,
    const StreamCallback& callback) {
    auto model = model_for_request(request);
    if (!model) return std::unexpected(model.error());
    auto loaded = ensure_model_loaded(*model, request.stop, request.deadline);
    if (!loaded) return std::unexpected(loaded.error());

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "curl initialization failed"
        });
    }

    ChatStreamState state{};
    state.callback = callback;
    const auto url = join_url(config_.lm_studio_url, "/v1/chat/completions");
    const auto payload = to_chat_body(*model, request, true).dump();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (const auto auth = authorization_header(); !auth.empty()) {
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    configure_local_transport(curl, url);
    CurlStopContext stop_context{request.stop};
    configure_cancellation(curl, stop_context);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, chat_stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    const auto timeout_ms = remaining_timeout_ms(request.deadline, std::chrono::seconds{static_cast<std::int64_t>(config_.inference_timeout_seconds)});
    if (timeout_ms <= 0) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(timeout_error("chat generation"));
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min<long>(5000L, timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    const auto rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (request.stop.stop_requested() || rc == CURLE_ABORTED_BY_CALLBACK) {
        mark_generation_termination_uncertain();
        return std::unexpected(cancelled_error());
    }

    if (state.parser_error.empty()) {
        try {
            consume_sse_lines(state, true, consume_chat_data);
        } catch (const std::exception& error) {
            state.parser_error = error.what();
        }
    }

    if (!state.parser_error.empty()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            "LM Studio Chat Completions stream parse failed: " + state.parser_error
        });
    }
    if (rc != CURLE_OK) {
        mark_generation_termination_uncertain();
        return std::unexpected(curl_failure(rc, "chat generation"));
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(http_failure(status, {}, "chat generation"));
    }

    ModelResponse out;
    out.model = *model;
    out.text = std::move(state.text);
    out.reasoning = std::move(state.reasoning);
    out.raw = std::move(state.last_chunk);
    for (auto& [index, streamed] : state.calls) {
        (void)index;
        if (streamed.id.empty() || streamed.name.empty()) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_protocol_error),
                "Incomplete streamed tool call metadata"
            });
        }
        ToolCall call;
        call.id = std::move(streamed.id);
        call.name = std::move(streamed.name);
        try {
            call.arguments = streamed.arguments.empty()
                ? json::object()
                : json::parse(streamed.arguments);
        } catch (const std::exception& error) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_protocol_error),
                "Invalid streamed tool arguments for " + call.name + ": " + error.what()
            });
        }
        out.tool_calls.push_back(std::move(call));
    }
    return out;
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete_responses(const ModelRequest& request) {
    auto model = model_for_request(request);
    if (!model) return std::unexpected(model.error());
    auto loaded = ensure_model_loaded(*model, request.stop, request.deadline);
    if (!loaded) return std::unexpected(loaded.error());

    auto response = http_json("/v1/responses", to_responses_body(*model, request, false), request.stop, request.deadline);
    if (!response) {
        if (response.error().code == core::make_error_code(core::Errc::cancelled) ||
            response.error().code == core::make_error_code(core::Errc::model_timeout) ||
            response.error().code == core::make_error_code(core::Errc::model_connection_reset)) {
            mark_generation_termination_uncertain();
        }
        return std::unexpected(response.error());
    }

    try {
        ModelResponse out;
        out.model = *model;
        out.raw = *response;
        for (const auto& item : response->value("output", json::array())) {
            const auto type = item.value("type", std::string{});
            if (type == "message") {
                if (item.contains("content") && item["content"].is_array()) {
                    for (const auto& content : item["content"]) {
                        if (content.value("type", std::string{}) == "output_text") {
                            out.text += content.value("text", std::string{});
                        }
                    }
                } else {
                    out.text += item.value("content", std::string{});
                }
            } else if (type == "function_call") {
                ToolCall call;
                call.id = item.value("call_id", item.value("id", std::string{}));
                call.name = item.value("name", std::string{});
                if (call.id.empty() || call.name.empty()) {
                    return std::unexpected(core::Error{
                        core::make_error_code(core::Errc::model_protocol_error),
                        "Upstream returned an incomplete function call"
                    });
                }
                const auto arguments = item.value("arguments", json::object());
                call.arguments = arguments.is_string()
                    ? json::parse(arguments.get<std::string>())
                    : arguments;
                out.tool_calls.push_back(std::move(call));
            }
        }
        return out;
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            error.what()
        });
    }
}

std::expected<ModelResponse, core::Error> LmStudioProvider::complete_responses_stream(
    const ModelRequest& request,
    const StreamCallback& callback) {
    auto model = model_for_request(request);
    if (!model) return std::unexpected(model.error());
    auto loaded = ensure_model_loaded(*model, request.stop, request.deadline);
    if (!loaded) return std::unexpected(loaded.error());

    CURL* curl = curl_easy_init();
    if (!curl) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "curl initialization failed"
        });
    }

    ResponsesStreamState state{};
    state.callback = callback;
    const auto url = join_url(config_.lm_studio_url, "/v1/responses");
    const auto payload = to_responses_body(*model, request, true).dump();
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    if (const auto auth = authorization_header(); !auth.empty()) {
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    configure_local_transport(curl, url);
    CurlStopContext stop_context{request.stop};
    configure_cancellation(curl, stop_context);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, responses_stream_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    const auto timeout_ms = remaining_timeout_ms(request.deadline, std::chrono::seconds{static_cast<std::int64_t>(config_.inference_timeout_seconds)});
    if (timeout_ms <= 0) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return std::unexpected(timeout_error("responses generation"));
    }
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min<long>(5000L, timeout_ms));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    const auto rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (request.stop.stop_requested() || rc == CURLE_ABORTED_BY_CALLBACK) {
        mark_generation_termination_uncertain();
        return std::unexpected(cancelled_error());
    }

    if (state.parser_error.empty()) {
        try {
            consume_sse_lines(state, true, consume_responses_data);
        } catch (const std::exception& error) {
            state.parser_error = error.what();
        }
    }

    if (!state.parser_error.empty()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_protocol_error),
            "LM Studio Responses stream parse failed: " + state.parser_error
        });
    }
    if (rc != CURLE_OK) {
        mark_generation_termination_uncertain();
        return std::unexpected(curl_failure(rc, "responses generation"));
    }
    if (status < 200 || status >= 300) {
        return std::unexpected(http_failure(status, {}, "responses generation"));
    }

    ModelResponse out;
    out.model = *model;
    out.text = std::move(state.text);
    out.reasoning = std::move(state.reasoning);
    out.raw = std::move(state.last_chunk);
    for (auto& [index, streamed] : state.calls) {
        (void)index;
        if (streamed.name.empty()) {
            continue;
        }
        if (streamed.id.empty() || streamed.name.empty()) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_protocol_error),
                "Incomplete streamed Responses function call metadata"
            });
        }
        ToolCall call;
        call.id = std::move(streamed.id);
        call.name = std::move(streamed.name);
        try {
            call.arguments = streamed.arguments.empty()
                ? json::object()
                : json::parse(streamed.arguments);
        } catch (const std::exception& error) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_protocol_error),
                "Invalid Responses tool arguments for " + call.name + ": " + error.what()
            });
        }
        out.tool_calls.push_back(std::move(call));
    }
    return out;
}

} // namespace engcoder::model
