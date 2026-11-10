#include "engcoder/server/http_server.hpp"
#include "engcoder/core/ids.hpp"
#include "engcoder/core/file_replace.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <sstream>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>

#ifndef ENGCODER_VERSION_STRING
#define ENGCODER_VERSION_STRING "dev"
#endif

namespace engcoder::server {
namespace {
using json = nlohmann::json;

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void serve_file(
    httplib::Response& response,
    const std::filesystem::path& path,
    const char* content_type) {
    auto content = read_text_file(path);
    if (content.empty() && (!std::filesystem::exists(path) || std::filesystem::file_size(path) != 0)) {
        response.status = 404;
        response.set_content("Not found", "text/plain; charset=utf-8");
        return;
    }
    // UI files change between source releases. Never serve stale JavaScript or
    // HTML across a desktop restart or a remote browser refresh.
    response.set_header("Cache-Control", "no-store");
    response.set_content(std::move(content), content_type);
}

[[nodiscard]] bool is_loopback_client(std::string_view address) noexcept {
    return address == "127.0.0.1" || address == "::1" || address == "localhost" ||
           address == "::ffff:127.0.0.1";
}

// Constant work over both strings; used only on remote requests. Remote
// credentials must be configured explicitly before exposing an API listener.
[[nodiscard]] bool token_equal(std::string_view a, std::string_view b) noexcept {
    std::size_t difference = a.size() ^ b.size();
    const auto n = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        difference |= ca ^ cb;
    }
    return difference == 0;
}

[[nodiscard]] std::string remote_api_token() {
#if defined(_WIN32)
    // MSVC deprecates getenv: _dupenv_s also gives this request its own
    // stable copy instead of borrowing the CRT's environment storage.
    char* raw = nullptr;
    std::size_t length = 0;
    const auto status = _dupenv_s(&raw, &length, "ENGCODER_REMOTE_API_TOKEN");
    const std::unique_ptr<char, decltype(&std::free)> value{raw, &std::free};
    if (status != 0 || !value) return {};
    return std::string{value.get(), length > 0 ? length - 1 : 0};
#else
    const char* value = std::getenv("ENGCODER_REMOTE_API_TOKEN");
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

[[nodiscard]] bool safe_origin(const httplib::Request& request) {
    if (!request.has_header("Origin")) return true; // non-browser local clients
    const auto origin = request.get_header_value("Origin");
    const auto host = request.get_header_value("Host");
    if (host.empty()) return false;
    return origin == "http://" + host || origin == "https://" + host;
}

[[nodiscard]] std::string content_text(const json& content) {
    if (content.is_null()) return {};
    if (content.is_string()) return content.get<std::string>();
    if (!content.is_array()) {
        throw std::invalid_argument("message content must be a string or text-content array");
    }

    std::string out;
    for (const auto& part : content) {
        if (part.is_string()) {
            if (!out.empty()) out.push_back('\n');
            out += part.get<std::string>();
            continue;
        }
        if (!part.is_object()) continue;
        const auto type = part.value("type", std::string{});
        if (type == "text" || type == "input_text" || type == "output_text") {
            const auto text = part.value("text", std::string{});
            if (!text.empty()) {
                if (!out.empty()) out.push_back('\n');
                out += text;
            }
        }
    }
    return out;
}

[[nodiscard]] std::vector<json> normalized_tools(const json& tools) {
    std::vector<json> out;
    if (tools.is_null()) return out;
    if (!tools.is_array()) throw std::invalid_argument("tools must be an array");
    out.reserve(tools.size());

    for (const auto& tool : tools) {
        if (!tool.is_object() || tool.value("type", std::string{}) != "function") {
            throw std::invalid_argument("only function tools are supported on this inference route");
        }
        json function;
        if (tool.contains("function") && tool["function"].is_object()) {
            function = tool["function"];
        } else {
            function = tool;
            function.erase("type");
        }
        const auto name = function.value("name", std::string{});
        if (name.empty()) throw std::invalid_argument("function tool name is required");
        out.push_back({{"type", "function"}, {"function", std::move(function)}});
    }
    return out;
}

[[nodiscard]] json canonical_tool_choice(const json& raw, const std::vector<json>& tools) {
    if (raw.is_null()) return nullptr;
    if (raw.is_string()) {
        const auto choice = raw.get<std::string>();
        if (choice != "auto" && choice != "none" && choice != "required") {
            throw std::invalid_argument("tool_choice must be auto, none, required, or a named function");
        }
        if ((choice == "auto" || choice == "required") && tools.empty()) {
            throw std::invalid_argument("tool_choice requires a non-empty tools array");
        }
        return choice;
    }
    if (!raw.is_object() || raw.value("type", std::string{}) != "function") {
        throw std::invalid_argument("named tool_choice must select a function");
    }
    std::string name;
    if (raw.contains("function") && raw["function"].is_object()) name = raw["function"].value("name", std::string{});
    else name = raw.value("name", std::string{});
    if (name.empty()) throw std::invalid_argument("named tool_choice requires a function name");
    const bool exists = std::ranges::any_of(tools, [&](const json& tool) {
        return tool.contains("function") && tool["function"].value("name", std::string{}) == name;
    });
    if (!exists) throw std::invalid_argument("named tool_choice references a function not present in tools");
    return json{{"type", "function"}, {"function", json{{"name", name}}}};
}

[[nodiscard]] model::ModelRequest chat_model_request(
    const json& body,
    std::string model_name) {
    if (!body.contains("messages") || !body.at("messages").is_array()) {
        throw std::invalid_argument("messages array is required");
    }

    model::ModelRequest request;
    request.model = std::move(model_name);
    request.temperature = std::clamp(body.value("temperature", 0.15), 0.0, 2.0);
    request.max_tokens = std::clamp(
        body.value("max_tokens", body.value("max_completion_tokens", -1)),
        -1, 262144);
    if (body.contains("parallel_tool_calls")) request.parallel_tool_calls = body.at("parallel_tool_calls").get<bool>();
    request.tools = normalized_tools(body.value("tools", json::array()));
    request.tool_choice = canonical_tool_choice(body.value("tool_choice", json{}), request.tools);

    for (const auto& item : body.at("messages")) {
        if (!item.is_object()) continue;
        model::Message message;
        message.role = item.value("role", std::string{});
        if (message.role.empty()) throw std::invalid_argument("message role is required");
        message.content = content_text(item.value("content", json{}));
        message.tool_calls = item.value("tool_calls", json::array());
        message.tool_call_id = item.value("tool_call_id", std::string{});
        request.messages.push_back(std::move(message));
    }
    if (request.messages.empty()) throw std::invalid_argument("at least one message is required");
    return request;
}

[[nodiscard]] model::ModelRequest responses_model_request(
    const json& body,
    std::string model_name) {
    model::ModelRequest request;
    request.model = std::move(model_name);
    request.temperature = std::clamp(body.value("temperature", 0.15), 0.0, 2.0);
    request.max_tokens = std::clamp(body.value("max_output_tokens", -1), -1, 262144);
    if (body.contains("parallel_tool_calls")) request.parallel_tool_calls = body.at("parallel_tool_calls").get<bool>();
    request.tools = normalized_tools(body.value("tools", json::array()));
    request.tool_choice = canonical_tool_choice(body.value("tool_choice", json{}), request.tools);

    const auto instructions = body.value("instructions", std::string{});
    if (!instructions.empty()) request.messages.push_back({.role = "system", .content = instructions});

    if (!body.contains("input")) throw std::invalid_argument("input is required");
    const auto& input = body.at("input");
    if (input.is_string()) {
        request.messages.push_back({.role = "user", .content = input.get<std::string>()});
    } else if (input.is_array()) {
        for (const auto& item : input) {
            if (item.is_string()) {
                request.messages.push_back({.role = "user", .content = item.get<std::string>()});
                continue;
            }
            if (!item.is_object()) continue;
            const auto type = item.value("type", std::string{});
            if (type == "function_call_output") {
                request.messages.push_back({
                    .role = "tool",
                    .content = item.value("output", std::string{}),
                    .tool_call_id = item.value("call_id", std::string{})
                });
                continue;
            }
            const auto role = item.value("role", std::string{"user"});
            request.messages.push_back({.role = role, .content = content_text(item.value("content", json{}))});
        }
    } else {
        throw std::invalid_argument("input must be a string or array");
    }

    if (request.messages.empty()) throw std::invalid_argument("input did not contain any messages");
    return request;
}

[[nodiscard]] json openai_tool_calls(const std::vector<model::ToolCall>& calls) {
    json out = json::array();
    for (const auto& call : calls) {
        out.push_back({
            {"id", call.id},
            {"type", "function"},
            {"function", {
                {"name", call.name},
                {"arguments", call.arguments.dump()}
            }}
        });
    }
    return out;
}

struct FailureInfo {
    int status{500};
    std::string type{"server_error"};
    std::string code{"generation_failure"};
    bool retryable{};
    bool generation_may_still_be_running{};
};

[[nodiscard]] FailureInfo classify_failure(const core::Error& error) {
    using core::Errc;
    if (error.code == core::make_error_code(Errc::invalid_argument)) {
        return {400, "invalid_request_error", "invalid_request", false, false};
    }
    if (error.code == core::make_error_code(Errc::authentication_error)) {
        return {401, "authentication_error", "authentication_failed", false, false};
    }
    if (error.code == core::make_error_code(Errc::model_context_overflow)) {
        return {400, "invalid_request_error", "context_overflow", false, false};
    }
    if (error.code == core::make_error_code(Errc::model_timeout)) {
        return {504, "server_error", "upstream_timeout", false, true};
    }
    if (error.code == core::make_error_code(Errc::model_connection_reset)) {
        return {502, "server_error", "connection_reset", true, true};
    }
    if (error.code == core::make_error_code(Errc::model_loading_failed)) {
        return {503, "server_error", "loading_failure", true, false};
    }
    if (error.code == core::make_error_code(Errc::model_unavailable)) {
        const bool uncertain = error.message.find("may still be running") != std::string::npos ||
            error.message.find("still occupying") != std::string::npos;
        return {503, "server_error", uncertain ? "generation_state_uncertain" : "model_unavailable", !uncertain, uncertain};
    }
    if (error.code == core::make_error_code(Errc::model_protocol_error)) {
        return {502, "server_error", "protocol_failure", false, false};
    }
    if (error.code == core::make_error_code(Errc::model_generation_error)) {
        return {502, "server_error", "generation_failure", true, false};
    }
    if (error.code == core::make_error_code(Errc::cancelled)) {
        return {499, "server_error", "cancelled", false, true};
    }
    return {500, "server_error", "internal_error", false, false};
}

void set_openai_error(
    httplib::Response& response,
    const core::Error& error,
    std::string_view request_id,
    std::chrono::steady_clock::time_point started) {
    const auto info = classify_failure(error);
    response.status = info.status;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    json payload;
    payload["error"] = {
        {"message", error.message},
        {"type", info.type},
        {"param", nullptr},
        {"code", info.code},
        {"request_id", request_id},
        {"elapsed_ms", elapsed},
        {"retryable", info.retryable},
        {"generation_may_still_be_running", info.generation_may_still_be_running}
    };
    response.set_header("X-Request-ID", std::string{request_id});
    response.set_content(payload.dump(), "application/json");
}

void set_invalid_request(
    httplib::Response& response,
    std::string message,
    std::string_view request_id,
    std::chrono::steady_clock::time_point started,
    std::string code = "invalid_request") {
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    response.status = 400;
    response.set_header("X-Request-ID", std::string{request_id});
    response.set_content(json{{"error", {
        {"message", std::move(message)},
        {"type", "invalid_request_error"},
        {"param", nullptr},
        {"code", std::move(code)},
        {"request_id", request_id},
        {"elapsed_ms", elapsed},
        {"retryable", false},
        {"generation_may_still_be_running", false}
    }}}.dump(), "application/json");
}

[[nodiscard]] std::chrono::steady_clock::time_point inference_deadline(
    const httplib::Request& request,
    const json& body,
    const core::Config& config) {
    const auto max_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::hours{24 * 365}).count());
    std::uint64_t timeout_ms = std::min(config.inference_timeout_seconds, max_ms / 1000ULL) * 1000ULL;
    if (body.contains("timeout_ms")) {
        if (!body["timeout_ms"].is_number_unsigned() && !body["timeout_ms"].is_number_integer()) {
            throw std::invalid_argument("timeout_ms must be a positive integer");
        }
        const auto raw = body["timeout_ms"].get<std::int64_t>();
        if (raw <= 0) throw std::invalid_argument("timeout_ms must be positive");
        timeout_ms = static_cast<std::uint64_t>(raw);
    } else if (body.contains("timeout_seconds")) {
        if (!body["timeout_seconds"].is_number_unsigned() && !body["timeout_seconds"].is_number_integer()) {
            throw std::invalid_argument("timeout_seconds must be a positive integer");
        }
        const auto raw = body["timeout_seconds"].get<std::int64_t>();
        if (raw <= 0) throw std::invalid_argument("timeout_seconds must be positive");
        const auto seconds = static_cast<std::uint64_t>(raw);
        timeout_ms = seconds > max_ms / 1000ULL ? max_ms : seconds * 1000ULL;
    } else if (request.has_header("X-EngCoder-Timeout-Ms")) {
        try {
            const auto raw = std::stoull(request.get_header_value("X-EngCoder-Timeout-Ms"));
            if (raw == 0) throw std::invalid_argument("zero");
            timeout_ms = raw;
        } catch (...) {
            throw std::invalid_argument("X-EngCoder-Timeout-Ms must be a positive integer");
        }
    }
    timeout_ms = std::min(timeout_ms, max_ms);
    return std::chrono::steady_clock::now() + std::chrono::milliseconds{timeout_ms};
}

class InferenceRegistry {
public:
    [[nodiscard]] bool acquire(std::size_t key) {
        std::scoped_lock lock(mutex_);
        return active_.insert(key).second;
    }
    void release(std::size_t key) noexcept {
        std::scoped_lock lock(mutex_);
        active_.erase(key);
    }
private:
    std::mutex mutex_;
    std::unordered_set<std::size_t> active_;
};

InferenceRegistry g_inference_registry;

struct InferenceLease {
    explicit InferenceLease(std::size_t key) : key(key) {}
    ~InferenceLease() { g_inference_registry.release(key); }
    std::size_t key{};
};

[[nodiscard]] std::size_t inference_fingerprint(const json& body) {
    auto canonical = body;
    canonical.erase("stream");
    canonical.erase("timeout_ms");
    canonical.erase("timeout_seconds");
    return std::hash<std::string>{}(canonical.dump());
}

[[nodiscard]] std::int64_t unix_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

HttpServer::HttpServer(
    core::Config config,
    agent::TaskScheduler& scheduler,
    persistence::SessionStore& store,
    model::MultiplexProvider& model,
    addons::AddonRegistry& addons,
    controller::ArchitectController& controller,
    models::ModelRegistry& models,
    residency::ModelResidencyManager& residency,
    training::TrainingService& training,
    learning::LearningService& learning,
    model::InternalModelHost& internal_host,
    model::NativeModelManager& native_models)
    : config_(std::move(config)),
      scheduler_(scheduler),
      store_(store),
      model_(model),
      addons_(addons),
      controller_(controller),
      models_(models),
      residency_(residency),
      training_(training),
      learning_(learning),
      internal_host_(internal_host),
      native_models_(native_models) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::listen() {
    if (running()) {
        return false;
    }

    httplib::Server http;
    // Do not allow a stale EngCoder instance to share this loopback port.
    // cpp-httplib enables address/port reuse by default on some platforms;
    // override that behavior so Start server either owns the port or fails.
    http.set_socket_options([](socket_t socket) {
#ifdef _WIN32
        (void)httplib::set_socket_opt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        (void)httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    active_.store(&http, std::memory_order_release);
    http.set_read_timeout(30, 0);
    http.set_write_timeout(30, 0);
    // The UI and API are same-origin. Bind once on all IPv4 interfaces, but
    // reject non-loopback clients until the user explicitly enables remote serving.
    // This keeps one runtime and one web app while making LAN exposure an opt-in.
    http.set_pre_routing_handler([this](const httplib::Request& request, httplib::Response& response) {
        // Same-origin check blocks malicious cross-site browser POSTs to loopback.
        if (!safe_origin(request)) {
            response.status = 403;
            response.set_content(json{{"error", "Cross-origin EngCoder request rejected"}}.dump(), "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }
        if (!is_loopback_client(request.remote_addr)) {
            // Static files may load so a remote browser can request credentials;
            // all dynamic API and model endpoints require a bearer secret.
            const bool public_asset = request.method == "GET" &&
                (request.path == "/" || request.path.starts_with("/ui") || request.path == "/health");
            if (!remote_serving_.load(std::memory_order_acquire)) {
                response.status = 403;
                response.set_content(json{{"error", "Remote serving is disabled"}}.dump(), "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (!public_asset) {
                const auto secret = remote_api_token();
                const auto authorization = request.get_header_value("Authorization");
                if (secret.size() < 32 || !token_equal(authorization, "Bearer " + secret)) {
                    response.status = 401;
                    response.set_header("WWW-Authenticate", "Bearer realm=EngCoder");
                    response.set_content(json{{"error", "Valid remote EngCoder bearer token required"}}.dump(), "application/json");
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    const auto web_root = config_.web_root;
    http.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_redirect("/ui/");
    });
    http.Get("/ui", [](const httplib::Request&, httplib::Response& response) {
        response.set_redirect("/ui/");
    });
    http.Get("/ui/", [web_root](const httplib::Request&, httplib::Response& response) {
        serve_file(response, web_root / "index.html", "text/html; charset=utf-8");
    });
    http.Get("/ui/index.html", [web_root](const httplib::Request&, httplib::Response& response) {
        serve_file(response, web_root / "index.html", "text/html; charset=utf-8");
    });
    http.Get("/ui/css/engcoder.css", [web_root](const httplib::Request&, httplib::Response& response) {
        serve_file(response, web_root / "css" / "engcoder.css", "text/css; charset=utf-8");
    });
    for (const auto* script : {"config.js", "state.js", "api.js", "math.js", "render.js", "voice.js", "ui.js", "mission_data.js", "missions.js", "main.js"}) {
        const std::string route = std::string{"/ui/js/"} + script;
        const auto path = web_root / "js" / script;
        http.Get(route, [path](const httplib::Request&, httplib::Response& response) {
            serve_file(response, path, "text/javascript; charset=utf-8");
        });
    }

    // Runtime liveness must stay fast and independent of model-server availability.
    http.Get("/health", [this](const httplib::Request&, httplib::Response& response) {
        response.set_content(json{
            {"ok", true},
            {"product", "EngCoder"},
            {"version", ENGCODER_VERSION_STRING},
            {"ui", "/ui/"},
            {"openai_base", "/v1"},
            {"chat_completions", "/v1/chat/completions"},
            {"responses", "/v1/responses"},
            {"agent_tasks", "/api/tasks"},
            {"mcp", nullptr},
            {"remote_serving", remote_serving_.load(std::memory_order_acquire)}
        }.dump(), "application/json");
    });

    // Shared appearance profile: Desktop Edge mode and served Web UI use this
    // same native runtime. Only the public theme choice is shared; message
    // formatting/font size remain browser-local. All API routes retain remote
    // bearer authentication from the pre-routing handler.
    const auto theme_path = config_.data_root / "ui-appearance.json";
    const auto theme_mutex = std::make_shared<std::mutex>();
    http.Get("/api/engcoder/appearance", [theme_path, theme_mutex](
        const httplib::Request&, httplib::Response& response) {
        std::scoped_lock guard(*theme_mutex);
        std::string theme;
        std::error_code ec;
        if (std::filesystem::is_regular_file(theme_path, ec) &&
            std::filesystem::file_size(theme_path, ec) <= 1024 && !ec) {
            try {
                const auto saved = json::parse(read_text_file(theme_path));
                if (saved.is_object() && saved.contains("theme") && saved["theme"].is_string())
                    theme = saved["theme"].get<std::string>();
            } catch (...) { /* malformed old preference does not break the UI */ }
        }
        response.set_content(json{{"theme", theme}}.dump(), "application/json");
    });
    http.Post("/api/engcoder/appearance", [theme_path, theme_mutex](
        const httplib::Request& request, httplib::Response& response) {
        try {
            if (request.body.size() > 1024) throw std::invalid_argument("appearance payload too large");
            const auto payload = json::parse(request.body);
            if (!payload.is_object() || !payload.contains("theme") || !payload["theme"].is_string())
                throw std::invalid_argument("theme must be a string");
            const auto theme = payload["theme"].get<std::string>();
            static constexpr std::string_view supported[]{
                "dark", "light", "windows-system", "windows-dark", "windows-light", "matrix"};
            if (std::ranges::find(supported, theme) == std::end(supported))
                throw std::invalid_argument("unsupported theme");
            std::scoped_lock guard(*theme_mutex);
            std::filesystem::create_directories(theme_path.parent_path());
            const auto staged = theme_path.parent_path() /
                (".ui-appearance-" + core::make_id("stage") + ".tmp");
            {
                std::ofstream output(staged, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("cannot stage appearance setting");
                output << json{{"theme", theme}}.dump();
                output.flush();
                if (!output) throw std::runtime_error("appearance setting write failed");
            }
            if (const auto error = core::replace_file(staged, theme_path); error) {
                std::error_code cleanup;
                std::filesystem::remove(staged, cleanup);
                throw std::runtime_error("appearance replacement failed: " + error.message());
            }
            response.set_content(json{{"theme", theme}}.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Get("/api/engcoder/serving", [this](const httplib::Request&, httplib::Response& response) {
        response.set_content(json{
            {"remote_enabled", remote_serving_.load(std::memory_order_acquire)},
            {"port", config_.server_port},
            {"local_url", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/ui/"},
            {"remote_url", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/ui/"},
            {"local_openai_base", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1"},
            {"local_chat_completions", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1/chat/completions"},
            {"local_responses", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1/responses"},
            {"local_agent_tasks", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/api/tasks"},
            {"local_agent_events", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/api/sessions/{session_id}/events?after=0"},
            {"remote_openai_base", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/v1"},
            {"remote_agent_tasks", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/api/tasks"},
            {"mcp_endpoint", nullptr}
        }.dump(), "application/json");
    });

    http.Post("/api/engcoder/serving", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Remote serving can only be changed from the local EngCoder UI."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const bool enable_remote = body.value("remote_enabled", false);
            if (enable_remote && !config_.iteration_candidate.empty()) {
                response.status = 409;
                response.set_content(json{{"error", "Candidate self-iteration uses a loopback-only server."}}.dump(), "application/json");
                return;
            }
            if (enable_remote && remote_api_token().size() < 32) {
                response.status = 409;
                response.set_content(json{{"error", "Set ENGCODER_REMOTE_API_TOKEN (32+ characters) and restart before enabling remote serving"}}.dump(), "application/json");
                return;
            }
            remote_serving_.store(enable_remote, std::memory_order_release);
            response.set_content(json{
                {"remote_enabled", remote_serving_.load(std::memory_order_acquire)},
                {"port", config_.server_port},
                {"local_url", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/ui/"},
                {"remote_url", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/ui/"},
                {"local_openai_base", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1"},
                {"local_chat_completions", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1/chat/completions"},
                {"local_responses", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/v1/responses"},
                {"local_agent_tasks", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/api/tasks"},
                {"local_agent_events", "http://127.0.0.1:" + std::to_string(config_.server_port) + "/api/sessions/{session_id}/events?after=0"},
                {"remote_openai_base", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/v1"},
                {"remote_agent_tasks", "http://<this-pc-lan-ip>:" + std::to_string(config_.server_port) + "/api/tasks"},
                {"mcp_endpoint", nullptr}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Get("/api/engcoder/provider", [this](const httplib::Request&, httplib::Response& response) {
        auto& provider = model_.external();
        auto discovery = provider.discover();
        json payload{
            {"provider", "LM Studio / OpenAI-compatible"},
            {"base_url", provider.base_url()},
            {"token_configured", provider.token_configured()},
            {"ok", discovery.has_value()}
        };
        if (discovery) payload["discovery"] = *discovery;
        else payload["error"] = discovery.error().message;
        response.set_content(payload.dump(), "application/json");
    });

    http.Post("/api/engcoder/provider", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Provider credentials can only be changed from the local EngCoder UI."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto& provider = model_.external();
            if (body.contains("token") && body["token"].is_string()) {
                provider.set_token(body["token"].get<std::string>());
            }
            auto discovery = provider.discover();
            json payload{
                {"provider", "LM Studio / OpenAI-compatible"},
                {"base_url", provider.base_url()},
                {"token_configured", provider.token_configured()},
                {"ok", discovery.has_value()}
            };
            if (discovery) payload["discovery"] = *discovery;
            else payload["error"] = discovery.error().message;
            response.set_content(payload.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    // Live native-Qwen output for this EngCoder host, including requests made
    // through its OpenAI-compatible endpoints by MCP/tool clients. The existing
    // remote bearer gate protects this host-wide administrator feed.
    http.Get("/api/engcoder/qwen-output", [this](const httplib::Request& request, httplib::Response& response) {
        std::uint64_t after = 0;
        if (request.has_param("after")) {
            try {
                const auto value = request.get_param_value("after");
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                    throw std::invalid_argument("Invalid cursor");
                }
                after = std::stoull(value);
            } catch (const std::exception&) {
                response.status = 400;
                response.set_content(json{{"error", "Invalid Qwen output cursor"}}.dump(), "application/json");
                return;
            }
        }
        const auto snapshot = model_.qwen_output_after(after);
        json events = json::array();
        for (const auto& item : snapshot.events) {
            events.push_back({
                {"sequence", item.sequence}, {"request_id", item.request_id},
                {"model", item.model}, {"kind", item.kind}, {"delta", item.delta}
            });
        }
        json payload = {{"cursor", snapshot.cursor}, {"events", std::move(events)}};
        // Full recovery text is needed on first load or after the event ring
        // pruned events the client never saw. Do not resend up to 96 KiB
        // every half-second while idle or during an ordinary live stream.
        const bool missed_events = !snapshot.events.empty() &&
            snapshot.events.front().sequence > after &&
            snapshot.events.front().sequence - after > 1;
        if (after == 0 || missed_events || after > snapshot.cursor) {
            payload["latest"] = {
                {"request_id", snapshot.latest.request_id},
                {"model", snapshot.latest.model},
                {"output", snapshot.latest.output},
                {"generating", snapshot.latest.generating},
                {"has_current_output", snapshot.latest.has_current_output}
            };
        }
        response.set_header("Cache-Control", "no-store");
        response.set_content(payload.dump(), "application/json");
    });

    http.Get("/api/model/discovery", [&](const httplib::Request&, httplib::Response& response) {
        auto discovery = model_.discover();
        if (!discovery) {
            response.status = 503;
            response.set_content(json{{"error", discovery.error().message}}.dump(), "application/json");
            return;
        }
        const auto configured_foundation = models_.foundation_model();
        const auto effective_foundation = model_.resolve_model_hint(configured_foundation);
        (*discovery)["selected_model"] = effective_foundation;
        (*discovery)["foundation_model"] = effective_foundation;
        (*discovery)["configured_foundation_model"] = configured_foundation;
        response.set_content(discovery->dump(), "application/json");
    });

    http.Get("/api/engcoder/model-library", [this](const httplib::Request&, httplib::Response& response) {
        response.set_content(native_models_.describe().dump(), "application/json");
    });

    http.Post("/api/engcoder/foundation", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Foundation model selection is local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto model = body.value("model", std::string{});
            if (!models_.set_foundation_model(model)) {
                response.status = 409;
                response.set_content(json{{"error", "Foundation model is not available: " + model}}.dump(), "application/json");
                return;
            }
            response.set_content(json{
                {"foundation_model", models_.foundation_model()},
                {"model_registry", models_.describe()},
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/register", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Model registration is local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto result = native_models_.register_model(body);
            if (!result) {
                response.status = 400;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            models_.refresh();
            residency_.sync(models_);
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/runtime/install", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Native runtime installation is local-only."}}.dump(), "application/json");
            return;
        }
        auto result = native_models_.provision_runtime();
        if (!result) {
            response.status = 409;
            response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
            return;
        }
        response.set_content(result->dump(), "application/json");
    });

    http.Post("/api/engcoder/model-library/install", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Model downloads are local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto model = body.value("model", body.value("id", std::string{}));
            auto result = native_models_.install(model);
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/load", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Native model lifecycle controls are local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto model = body.value("model", body.value("id", std::string{}));
            const auto local_override = body.value("local_path_override", std::string{});
            auto endpoint = local_override.empty()
                ? native_models_.prepare(model)
                : native_models_.prepare_with_local_override(model, std::filesystem::path{local_override});
            if (!endpoint) {
                response.status = 409;
                response.set_content(json{{"error", endpoint.error().message}}.dump(), "application/json");
                return;
            }
            response.set_content(json{
                {"loaded", true},
                {"model", model},
                {"endpoint", *endpoint},
                {"local_path_override", local_override.empty() ? json(nullptr) : json(local_override)},
                {"library", native_models_.describe()}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/unload", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Native model lifecycle controls are local-only."}}.dump(), "application/json");
            return;
        }
        native_models_.stop_active();
        response.set_content(json{{"loaded", false}, {"library", native_models_.describe()}}.dump(), "application/json");
    });

    http.Post("/api/engcoder/model-library/clear-cache", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Cache maintenance is local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto result = native_models_.clear_transient_cache(body.value("model", body.value("id", std::string{})));
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/delete-weights", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Model deletion is local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto result = native_models_.delete_downloaded_weights(body.value("model", body.value("id", std::string{})));
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            models_.refresh();
            residency_.sync(models_);
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/model-library/remove", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Model removal is local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto result = native_models_.remove(
                body.value("model", body.value("id", std::string{})),
                body.value("delete_cache", false));
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            models_.refresh();
            residency_.sync(models_);
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    const auto architecture_handler = [&](const httplib::Request&, httplib::Response& response) {
        models_.refresh();
        residency_.sync(models_);
        const auto addon_description = addons_.describe();
        const auto configured_foundation = models_.foundation_model();
        const auto effective_foundation = model_.resolve_model_hint(configured_foundation);
        auto physical_residency = internal_host_.residency_status(false);
        const auto physical_json = physical_residency
            ? *physical_residency
            : json{{"loaded", false}, {"error", physical_residency.error().message}};
        response.set_content(json{
            {"format", "engcoder.architecture.v4"},
            {"product", "EngCoder"},
            {"strategy", "architect+keymaker-foundation+on-demand-addons"},
            {"controller", controller_.describe()},
            {"foundation", {
                {"id", "keymaker"},
                {"role", "resident English language/reasoning foundation"},
                {"model", effective_foundation},
                {"configured_model", configured_foundation},
                {"auto_resolved", effective_foundation != configured_foundation},
                {"recommended_size", "250M-400M"},
                {"user_facing", true},
                {"pinned", true}
            }},
            {"addons", addon_description.value("addons", json::array())},
            {"model_registry", models_.describe()},
            {"residency", {
                {"logical", residency_.describe()},
                {"physical", physical_json}
            }},
            {"training", training_.status()},
            {"learning", learning_.describe()},
            {"internal_host", internal_host_.describe()},
            {"native_models", native_models_.describe()}
        }.dump(), "application/json");
    };
    http.Get("/api/engcoder/architecture", architecture_handler);

    const auto models_handler = [&](const httplib::Request&, httplib::Response& response) {
        models_.refresh();
        residency_.sync(models_);
        response.set_content(models_.describe().dump(), "application/json");
    };
    http.Get("/api/engcoder/models", models_handler);

    const auto residency_handler = [&](const httplib::Request& request, httplib::Response& response) {
        const bool load = request.has_param("load") && request.get_param_value("load") == "1";
        auto physical = internal_host_.residency_status(load);
        if (!physical) {
            response.status = 503;
            response.set_content(json{
                {"error", physical.error().message},
                {"logical", residency_.describe()}
            }.dump(), "application/json");
            return;
        }
        auto payload = *physical;
        payload["logical"] = residency_.describe();
        response.set_content(payload.dump(), "application/json");
    };
    http.Get("/api/engcoder/residency", residency_handler);

    const auto residency_activate_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto model = body.value("model", body.value("lane", std::string{"engcoder://keymaker"}));
            auto result = internal_host_.prepare_model(model);
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/residency/activate", residency_activate_handler);

    const auto residency_release_handler = [&](const httplib::Request&, httplib::Response& response) {
        auto result = internal_host_.release_idle_addons();
        if (!result) {
            response.status = 409;
            response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
            return;
        }
        response.set_content(result->dump(), "application/json");
    };
    http.Post("/api/engcoder/residency/release-addons", residency_release_handler);

    const auto training_status_handler = [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(training_.status().dump(), "application/json");
    };
    http.Get("/api/engcoder/training/status", training_status_handler);

    const auto training_doctor_handler = [&](const httplib::Request&, httplib::Response& response) {
        auto result = training_.doctor();
        if (!result) {
            response.status = 503;
            response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
            return;
        }
        response.set_content(result->dump(), "application/json");
    };
    http.Post("/api/engcoder/training/doctor", training_doctor_handler);

    const auto training_initialize_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            auto result = training_.initialize(body.value("profile", std::string{"desktop-16gb"}), body.value("force", false));
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            response.set_content(result->dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/training/initialize", training_initialize_handler);

    const auto training_start_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            training::TrainingRequest job;
            job.scope = body.value("scope", std::string{"keymaker-foundation"});
            job.max_steps = body.value("max_steps", std::uint32_t{500});
            job.checkpoint_steps = body.value("checkpoint_steps", std::uint32_t{50});
            job.require_cuda = body.value("require_cuda", true);
            auto result = training_.start(std::move(job));
            if (!result) {
                response.status = 409;
                response.set_content(json{{"error", result.error().message}}.dump(), "application/json");
                return;
            }
            response.status = 202;
            response.set_content(training_.status().dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/training/start", training_start_handler);

    const auto training_pause_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            training_.set_paused(body.value("paused", true));
            response.set_content(training_.status().dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/training/pause", training_pause_handler);

    const auto training_cancel_handler = [&](const httplib::Request&, httplib::Response& response) {
        training_.cancel();
        response.status = 202;
        response.set_content(training_.status().dump(), "application/json");
    };
    http.Post("/api/engcoder/training/cancel", training_cancel_handler);

    const auto learning_handler = [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(learning_.describe().dump(), "application/json");
    };
    http.Get("/api/engcoder/learning", learning_handler);

    const auto learning_evidence_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = json::parse(request.body);
            const auto session_id = body.value("session_id", std::string{});
            const auto task_id = body.value("task_id", std::string{});
            if (session_id.empty() || session_id.size() > 256 || task_id.empty() || task_id.size() > 256) {
                response.status = 400;
                response.set_content(json{{"error", "Learning requires the original session and task IDs."}}.dump(), "application/json");
                return;
            }
            const auto reply = store_.learnable_reply(session_id, task_id);
            if (!reply) {
                response.status = 503;
                response.set_content(json{{"error", "Unable to verify the reply's learning eligibility."}}.dump(), "application/json");
                return;
            }
            if (!*reply) {
                response.status = 409;
                response.set_content(json{{"error", "Candidate runs and replies without a completed native task cannot enter learning."}}.dump(), "application/json");
                return;
            }
            const auto trim = [](std::string_view value) -> std::string_view {
                const auto first = value.find_first_not_of(" \t\r\n\f\v");
                if (first == std::string_view::npos) return {};
                return value.substr(first, value.find_last_not_of(" \t\r\n\f\v") - first + 1);
            };
            const auto submitted = body.at("response").get<std::string>();
            if (trim(submitted) != trim(**reply)) {
                response.status = 409;
                response.set_content(json{{"error", "The response does not match the completed native task."}}.dump(), "application/json");
                return;
            }
            learning::EvidenceRecord record;
            record.skill = body.value("skill", std::string{"keymaker-foundation"});
            record.prompt = body.at("prompt").get<std::string>();
            record.response = trim(**reply);
            record.source = "user-accepted";
            record.confidence = body.value("confidence", 1.0);
            record.verified = body.value("verified", true);
            if (learning_.is_banned(record.prompt, record.response)) {
                response.status = 409;
                response.set_content(json{{"stored", false}, {"banned", true}, {"error", "This content is banned from EngCoder learning. Unban it in Learning & memory before saving it again."}}.dump(), "application/json");
                return;
            }
            const bool stored = learning_.record(std::move(record));
            response.status = stored ? 201 : 400;
            response.set_content(json{{"stored", stored}, {"banned", false}}.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/learning/evidence", learning_evidence_handler);

    http.Post("/api/engcoder/learning/remove", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Learning changes are local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto hash = body.value("content_hash", std::string{});
            if (hash.empty()) {
                response.status = 400;
                response.set_content(json{{"error", "content_hash is required"}}.dump(), "application/json");
                return;
            }
            const bool removed = learning_.remove(hash);
            response.set_content(json{{"removed", removed}, {"content_hash", hash}}.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/engcoder/learning/ban", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Learning changes are local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto prompt = body.value("prompt", std::string{});
            const auto answer = body.value("response", std::string{});
            auto result = learning_.ban(prompt, answer, body.value("reason", std::string{"user-banned"}));
            response.set_content(result.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Get("/api/engcoder/learning/bans", [this](const httplib::Request&, httplib::Response& response) {
        response.set_content(learning_.bans().dump(), "application/json");
    });

    http.Post("/api/engcoder/learning/unban", [this](const httplib::Request& request, httplib::Response& response) {
        if (!is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(json{{"error", "Learning changes are local-only."}}.dump(), "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto id = body.value("ban_id", std::string{});
            const bool removed = learning_.unban(id);
            response.set_content(json{{"unbanned", removed}, {"ban_id", id}}.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    const auto memory_query_handler = [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            const auto query = body.value("query", std::string{});
            if (query.empty()) {
                response.status = 400;
                response.set_content(json{{"error", "query is required"}}.dump(), "application/json");
                return;
            }
            const auto raw_limit = body.value("limit", std::size_t{3});
            const auto limit = std::clamp<std::size_t>(raw_limit, 1, 20);
            response.set_content(
                learning_.retrieve(query, limit, body.value("skill", std::string{})).dump(),
                "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    };
    http.Post("/api/engcoder/memory/query", memory_query_handler);

    const auto memory_recent_handler = [&](const httplib::Request& request, httplib::Response& response) {
        std::size_t limit = 20;
        if (request.has_param("limit")) {
            try { limit = std::clamp<std::size_t>(std::stoull(request.get_param_value("limit")), 1, 100); }
            catch (...) { limit = 20; }
        }
        response.set_content(learning_.recent(limit).dump(), "application/json");
    };
    http.Get("/api/engcoder/memory/recent", memory_recent_handler);

    // Engine-facing inference API. These routes intentionally bypass the full
    // agent loop and call the selected Foundation model directly. Use /api/tasks
    // when Architect routing, addons, tools, memory, approvals and verification
    // are required.
    http.Get("/v1/models", [this](const httplib::Request&, httplib::Response& response) {
        const auto started = std::chrono::steady_clock::now();
        const auto request_id = core::make_id("req");
        auto discovery = model_.discover();
        if (!discovery) {
            set_openai_error(response, discovery.error(), request_id, started);
            return;
        }

        json data = json::array();
        data.push_back({
            {"id", "engcoder-foundation"},
            {"object", "model"},
            {"created", 0},
            {"owned_by", "engcoder"},
            {"resolved_model", model_.resolve_model_hint(models_.foundation_model())}
        });

        std::vector<std::string> seen{"engcoder-foundation"};
        for (const auto& item : discovery->value("models", json::array())) {
            if (!item.is_object()) continue;
            const auto id = item.value("id", std::string{});
            if (id.empty() || std::ranges::find(seen, id) != seen.end()) continue;
            seen.push_back(id);
            data.push_back({
                {"id", id},
                {"object", "model"},
                {"created", 0},
                {"owned_by", item.value("internal", false) || item.value("native", false) ? "engcoder" : "provider"}
            });
        }
        response.set_header("X-Request-ID", request_id);
        response.set_content(json{{"object", "list"}, {"data", std::move(data)}}.dump(), "application/json");
    });

    http.Post("/v1/chat/completions", [this](const httplib::Request& request, httplib::Response& response) {
        const auto started = std::chrono::steady_clock::now();
        const auto request_id = core::make_id("req");
        try {
            const auto body = json::parse(request.body);
            auto requested_model = body.value("model", std::string{"engcoder-foundation"});
            if (requested_model.empty() || requested_model == "engcoder-foundation") {
                requested_model = model_.resolve_model_hint(models_.foundation_model());
            }

            auto model_request = chat_model_request(body, requested_model);
            model_request.deadline = inference_deadline(request, body, config_);
            model_request.request_id = request_id;

            const auto fingerprint = inference_fingerprint(body);
            if (!g_inference_registry.acquire(fingerprint)) {
                response.status = 409;
                response.set_header("X-Request-ID", request_id);
                response.set_content(json{{"error", {
                    {"message", "An identical inference request is already active; duplicate generation was blocked"},
                    {"type", "server_error"}, {"code", "duplicate_inference"},
                    {"request_id", request_id}, {"elapsed_ms", 0},
                    {"retryable", true}, {"generation_may_still_be_running", false}
                }}}.dump(), "application/json");
                return;
            }
            auto lease = std::make_shared<InferenceLease>(fingerprint);

            if (body.value("stream", false)) {
                response.status = 200;
                response.set_header("Cache-Control", "no-cache");
                response.set_header("X-Accel-Buffering", "no");
                response.set_header("X-Request-ID", request_id);
                const auto created = unix_seconds();
                const auto completion_id = core::make_id("chatcmpl");
                const auto is_connection_closed = request.is_connection_closed;
                auto executed = std::make_shared<std::atomic_bool>(false);

                response.set_chunked_content_provider(
                    "text/event-stream",
                    [this, model_request = std::move(model_request), requested_model = std::move(requested_model),
                     request_id, started, created, completion_id, is_connection_closed, lease, executed]
                    (std::size_t, httplib::DataSink& sink) mutable {
                        if (executed->exchange(true, std::memory_order_acq_rel)) return false;
                        std::stop_source stop_source;
                        model_request.stop = stop_source.get_token();

                        std::jthread disconnect_watcher([&](std::stop_token watcher_stop) {
                            while (!watcher_stop.stop_requested() && !stop_source.stop_requested()) {
                                if (is_connection_closed && is_connection_closed()) {
                                    stop_source.request_stop();
                                    return;
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds{50});
                            }
                        });

                        auto write_data = [&](const json& payload) -> bool {
                            if (!sink.is_writable()) {
                                stop_source.request_stop();
                                return false;
                            }
                            const auto wire = "data: " + payload.dump() + "\n\n";
                            if (!sink.write(wire.data(), wire.size())) {
                                stop_source.request_stop();
                                return false;
                            }
                            return true;
                        };
                        auto write_done = [&]() {
                            constexpr std::string_view done = "data: [DONE]\n\n";
                            (void)sink.write(done.data(), done.size());
                            sink.done();
                        };

                        if (!write_data(json{
                            {"id", completion_id}, {"object", "chat.completion.chunk"},
                            {"created", created}, {"model", requested_model},
                            {"choices", json::array({json{{"index", 0}, {"delta", json{{"role", "assistant"}}}, {"finish_reason", nullptr}}})}
                        })) {
                            disconnect_watcher.request_stop();
                            return false;
                        }

                        auto inference = model_.complete_stream(model_request, [&](const model::StreamEvent& event) {
                            json delta = json::object();
                            switch (event.kind) {
                                case model::StreamEventKind::text_delta:
                                    delta["content"] = event.delta;
                                    break;
                                case model::StreamEventKind::reasoning_delta:
                                    delta["reasoning_content"] = event.delta;
                                    break;
                                case model::StreamEventKind::tool_call_started:
                                    delta["tool_calls"] = json::array({json{
                                        {"index", event.index}, {"id", event.tool_call_id}, {"type", "function"},
                                        {"function", json{{"name", event.tool_name}, {"arguments", ""}}}
                                    }});
                                    break;
                                case model::StreamEventKind::tool_argument_delta:
                                    delta["tool_calls"] = json::array({json{
                                        {"index", event.index},
                                        {"function", json{{"arguments", event.delta}}}
                                    }});
                                    break;
                            }
                            if (!write_data(json{
                                {"id", completion_id}, {"object", "chat.completion.chunk"},
                                {"created", created}, {"model", requested_model},
                                {"choices", json::array({json{{"index", 0}, {"delta", std::move(delta)}, {"finish_reason", nullptr}}})}
                            })) {
                                stop_source.request_stop();
                            }
                        });
                        disconnect_watcher.request_stop();

                        if (!inference) {
                            if (!(is_connection_closed && is_connection_closed())) {
                                const auto info = classify_failure(inference.error());
                                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started).count();
                                (void)write_data(json{{"error", {
                                    {"message", inference.error().message}, {"type", info.type}, {"code", info.code},
                                    {"request_id", request_id}, {"elapsed_ms", elapsed}, {"retryable", info.retryable},
                                    {"generation_may_still_be_running", info.generation_may_still_be_running}
                                }}});
                                write_done();
                                return true;
                            }
                            return false;
                        }

                        const auto final_model = inference->model.empty() ? requested_model : inference->model;
                        (void)write_data(json{
                            {"id", completion_id}, {"object", "chat.completion.chunk"},
                            {"created", created}, {"model", final_model},
                            {"choices", json::array({json{
                                {"index", 0}, {"delta", json::object()},
                                {"finish_reason", inference->tool_calls.empty() ? "stop" : "tool_calls"}
                            }})}
                        });
                        write_done();
                        return true;
                    });
                return;
            }

            (void)lease; // RAII keeps the duplicate-request reservation active.
            std::stop_source stop_source;
            model_request.stop = stop_source.get_token();
            std::jthread disconnect_watcher([&](std::stop_token watcher_stop) {
                while (!watcher_stop.stop_requested() && !stop_source.stop_requested()) {
                    if (request.is_connection_closed()) {
                        stop_source.request_stop();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                }
            });

            auto inference = model_.complete(model_request);
            disconnect_watcher.request_stop();
            if (!inference) {
                set_openai_error(response, inference.error(), request_id, started);
                return;
            }

            json message{{"role", "assistant"}, {"content", inference->text}};
            if (!inference->reasoning.empty()) message["reasoning_content"] = inference->reasoning;
            if (!inference->tool_calls.empty()) message["tool_calls"] = openai_tool_calls(inference->tool_calls);
            const auto model_name = inference->model.empty() ? requested_model : inference->model;
            response.set_header("X-Request-ID", request_id);
            response.set_content(json{
                {"id", core::make_id("chatcmpl")}, {"object", "chat.completion"},
                {"created", unix_seconds()}, {"model", model_name},
                {"choices", json::array({json{
                    {"index", 0}, {"message", std::move(message)},
                    {"finish_reason", inference->tool_calls.empty() ? "stop" : "tool_calls"}
                }})}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            set_invalid_request(response, error.what(), request_id, started);
        }
    });

    http.Post("/v1/responses", [this](const httplib::Request& request, httplib::Response& response) {
        const auto started = std::chrono::steady_clock::now();
        const auto request_id = core::make_id("req");
        try {
            const auto body = json::parse(request.body);
            if (body.value("stream", false)) {
                set_invalid_request(response, "Responses streaming is a separate protocol and is not exposed by this route", request_id, started, "unsupported_parameter");
                return;
            }

            auto requested_model = body.value("model", std::string{"engcoder-foundation"});
            if (requested_model.empty() || requested_model == "engcoder-foundation") {
                requested_model = model_.resolve_model_hint(models_.foundation_model());
            }
            auto model_request = responses_model_request(body, requested_model);
            model_request.deadline = inference_deadline(request, body, config_);
            model_request.request_id = request_id;

            const auto fingerprint = inference_fingerprint(body);
            if (!g_inference_registry.acquire(fingerprint)) {
                response.status = 409;
                response.set_header("X-Request-ID", request_id);
                response.set_content(json{{"error", {
                    {"message", "An identical inference request is already active; duplicate generation was blocked"},
                    {"type", "server_error"}, {"code", "duplicate_inference"},
                    {"request_id", request_id}, {"elapsed_ms", 0},
                    {"retryable", true}, {"generation_may_still_be_running", false}
                }}}.dump(), "application/json");
                return;
            }
            auto lease = std::make_shared<InferenceLease>(fingerprint);
            (void)lease; // RAII keeps the duplicate-request reservation active.
            std::stop_source stop_source;
            model_request.stop = stop_source.get_token();

            std::jthread disconnect_watcher([&](std::stop_token watcher_stop) {
                while (!watcher_stop.stop_requested() && !stop_source.stop_requested()) {
                    if (request.is_connection_closed()) {
                        stop_source.request_stop();
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                }
            });

            auto inference = model_.complete(model_request);
            disconnect_watcher.request_stop();
            if (!inference) {
                set_openai_error(response, inference.error(), request_id, started);
                return;
            }

            json output = json::array();
            if (!inference->text.empty()) {
                output.push_back({
                    {"type", "message"}, {"id", core::make_id("msg")}, {"role", "assistant"}, {"status", "completed"},
                    {"content", json::array({json{{"type", "output_text"}, {"text", inference->text}}})}
                });
            }
            for (const auto& call : inference->tool_calls) {
                output.push_back({
                    {"type", "function_call"}, {"id", core::make_id("fc")}, {"call_id", call.id},
                    {"name", call.name}, {"arguments", call.arguments.dump()}, {"status", "completed"}
                });
            }

            const auto model_name = inference->model.empty() ? requested_model : inference->model;
            response.set_header("X-Request-ID", request_id);
            response.set_content(json{
                {"id", core::make_id("resp")}, {"object", "response"}, {"created_at", unix_seconds()},
                {"status", "completed"}, {"model", model_name}, {"output", std::move(output)},
                {"output_text", inference->text}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            set_invalid_request(response, error.what(), request_id, started);
        }
    });

    http.Post("/api/tasks", [&](const httplib::Request& request, httplib::Response& response) {
        try {
            const auto body = json::parse(request.body);
            agent::TaskRequest task;
            task.project_id = body.value("project_id", std::string{"default"});
            task.session_id = body.value("session_id", core::make_id("session"));
            task.task_id = core::make_id("task");
            task.user_message = body.at("message").get<std::string>();
            task.self_iteration = body.value("self_iteration", false);
            if (task.self_iteration && !is_loopback_client(request.remote_addr)) {
                response.status = 403;
                response.set_content(R"({"error":"Self-iteration requires a local client"})", "application/json");
                return;
            }
            task.foundation_model = body.value("foundation_model", std::string{});
            task.simple_model = body.value("simple_model", std::string{});
            task.retry_model = body.value("retry_model", std::string{});
            // Do not let an arbitrary client-selected model grant tool authority.
            // Retry can pin only the declared foundation or the external small
            // responder, whose tool set is always empty.
            if (task.retry_model.size() > 256 ||
                (!task.retry_model.empty() && task.retry_model != task.foundation_model &&
                 task.retry_model != task.simple_model)) {
                response.status = 400;
                response.set_content(json{{"error", "retry_model must match the current task's foundation or small responder"}}.dump(), "application/json");
                return;
            }
            // Small-response models must be explicitly selected from a remote
            // provider, not a model URI that could mutate native runtime state.
            if (task.simple_model.starts_with("qwenflash://") ||
                task.simple_model.starts_with("engcoder://") ||
                task.simple_model.starts_with("internal://") ||
                task.simple_model.starts_with("threadspire://") ||
                task.simple_model == "auto") {
                response.status = 400;
                response.set_content(json{{"error", "simple_model requires a discovered external model"}}.dump(), "application/json");
                return;
            }
            task.review_model = body.value("review_model", std::string{});
            if (!task.review_model.empty() && task.review_model == task.simple_model) {
                response.status = 400;
                response.set_content(json{{"error", "Middle reviewer must be a different model than the small responder"}}.dump(), "application/json");
                return;
            }
            if (!task.review_model.empty() && task.review_model == task.foundation_model) {
                response.status = 400;
                response.set_content(json{{"error", "Middle reviewer must be independent from the KeyMaker foundation"}}.dump(), "application/json");
                return;
            }
            task.review_mode = body.value("review_mode", std::string{"auto"});
            task.thinking_mode = body.value("thinking_mode", std::string{"auto"});
            if (task.thinking_mode != "auto" && task.thinking_mode != "fast" && task.thinking_mode != "deep") {
                response.status = 400;
                response.set_content(json{{"error", "thinking_mode must be auto, fast or deep"}}.dump(), "application/json");
                return;
            }
            if (task.review_mode != "off" && task.review_mode != "auto" && task.review_mode != "deep") {
                response.status = 400;
                response.set_content(json{{"error", "review_mode must be off, auto or deep"}}.dump(), "application/json");
                return;
            }
            if (task.review_model.size() > 256 || task.review_model == "auto" ||
                task.review_model.starts_with("qwenflash://") ||
                task.review_model.starts_with("engcoder://") ||
                task.review_model.starts_with("internal://") ||
                task.review_model.starts_with("threadspire://")) {
                response.status = 400;
                response.set_content(json{{"error", "review_model requires an independent external provider model"}}.dump(), "application/json");
                return;
            }
            task.controller_model = body.value("controller_model", std::string{});
            task.forced_addon = body.value("forced_addon", std::string{});
            task.session_instructions = body.value("session_instructions", std::string{});
            if (task.session_instructions.size() > 32768) task.session_instructions.resize(32768);
            task.temperature = std::clamp(body.value("temperature", 0.15), 0.0, 2.0);
            task.max_tokens = std::clamp(body.value("max_tokens", -1), -1, 262144);
            task.include_history = body.value("include_history", true);
            const auto raw_history_turns = std::clamp(body.value("max_history_turns", 0), 0, 200);
            task.max_history_turns = static_cast<std::size_t>(raw_history_turns);
            task.auto_route = body.value("auto_route", true);
            task.addons_enabled = body.value("addons_enabled", true);
            if (!task.addons_enabled) task.forced_addon.clear();
            task.wait_for_approval = true;
            if (task.addons_enabled && body.contains("addon_models") && body["addon_models"].is_object()) {
                const auto& assignments = body["addon_models"];
                if (assignments.size() > 16) {
                    response.status = 400;
                    response.set_content(json{{"error", "too many addon model assignments"}}.dump(), "application/json");
                    return;
                }
                for (auto iter = assignments.begin(); iter != assignments.end(); ++iter) {
                    if (!iter.value().is_string()) {
                        response.status = 400;
                        response.set_content(json{{"error", "addon model assignment must be a string"}}.dump(), "application/json");
                        return;
                    }
                    const auto assigned = iter.value().get<std::string>();
                    // UI-assigned specialists must be independent provider models;
                    // do not attach the main native foundation as its own advisor.
                    if (assigned.size() > 256 || assigned == "auto" ||
                        assigned.starts_with("qwenflash://") ||
                        assigned.starts_with("engcoder://") ||
                        assigned.starts_with("internal://") ||
                        assigned.starts_with("threadspire://")) {
                        response.status = 400;
                        response.set_content(json{{"error", "addon override requires an external provider model"}}.dump(), "application/json");
                        return;
                    }
                }
                task.addon_models = assignments;
            }
            auto queued = scheduler_.submit(task);
            if (!queued) {
                response.status = 409;
                response.set_content(json{{"error", queued.error().message}}.dump(), "application/json");
                return;
            }
            response.status = 202;
            response.set_content(json{
                {"project_id", task.project_id},
                {"session_id", task.session_id},
                {"task_id", task.task_id}
            }.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post(R"(/api/tasks/([A-Za-z0-9_-]+)/cancel)", [&](const httplib::Request& request, httplib::Response& response) {
        const bool ok = scheduler_.cancel(request.matches[1].str());
        response.status = ok ? 202 : 404;
        response.set_content(json{{"ok", ok}}.dump(), "application/json");
    });

    http.Post(R"(/api/tasks/([A-Za-z0-9_-]+)/approval)", [&](const httplib::Request& request, httplib::Response& response) {
        if (!config_.iteration_candidate.empty() && !is_loopback_client(request.remote_addr)) {
            response.status = 403;
            response.set_content(R"({"error":"Approvals on a candidate host require a local client"})", "application/json");
            return;
        }
        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            if (!body.contains("approved") || !body.at("approved").is_boolean()) {
                response.status = 400;
                response.set_content(json{{"error", "approved boolean is required"}}.dump(), "application/json");
                return;
            }

            const auto call_id = body.value("call_id", std::string{});
            if (call_id.empty()) {
                response.status = 400;
                response.set_content(json{{"error", "call_id is required"}}.dump(), "application/json");
                return;
            }

            const auto task_id = request.matches[1].str();
            const bool ok = scheduler_.resolve_approval(task_id, call_id, body.at("approved").get<bool>());
            response.status = ok ? 202 : 409;
            response.set_content(
                ok
                    ? json{{"ok", true}, {"task_id", task_id}, {"call_id", call_id}}.dump()
                    : json{{"ok", false}, {"error", "no matching pending approval"}}.dump(),
                "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Post("/api/desktop/state", [&](const httplib::Request& request, httplib::Response& response) {
        if (config_.desktop_session_token.empty()) {
            response.status = 404;
            response.set_content(json{{"error", "desktop lifecycle endpoint is inactive"}}.dump(), "application/json");
            return;
        }

        try {
            const auto body = request.body.empty() ? json::object() : json::parse(request.body);
            if (body.value("token", std::string{}) != config_.desktop_session_token) {
                response.status = 403;
                response.set_content(json{{"error", "invalid desktop session token"}}.dump(), "application/json");
                return;
            }
            if (!body.contains("open") || !body.at("open").is_boolean()) {
                response.status = 400;
                response.set_content(json{{"error", "open boolean is required"}}.dump(), "application/json");
                return;
            }

            desktop_client_open_.store(body.at("open").get<bool>(), std::memory_order_release);
            desktop_client_seen_.store(true, std::memory_order_release);
            response.set_content(json{{"ok", true}}.dump(), "application/json");
        } catch (const std::exception& error) {
            response.status = 400;
            response.set_content(json{{"error", error.what()}}.dump(), "application/json");
        }
    });

    http.Get(R"(/api/sessions/([A-Za-z0-9_-]+)/events)", [&](const httplib::Request& request, httplib::Response& response) {
        std::uint64_t after = 0;
        if (request.has_param("after")) {
            const auto value = request.get_param_value("after");
            try {
                std::size_t consumed = 0;
                after = std::stoull(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("trailing characters");
            } catch (const std::exception&) {
                response.status = 400;
                response.set_content(
                    json{{"error", "after must be an unsigned integer sequence number"}}.dump(),
                    "application/json");
                return;
            }
        }
        auto events = store_.events_after(request.matches[1].str(), after);
        if (!events) {
            response.status = 500;
            response.set_content(json{{"error", events.error().message}}.dump(), "application/json");
            return;
        }
        json array = json::array();
        for (const auto& event : *events) {
            const auto timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(event.timestamp.time_since_epoch()).count();
            array.push_back({
                {"sequence", event.sequence},
                {"timestamp_ms", timestamp_ms},
                {"type", event.type},
                {"task_id", event.task_id},
                {"data", event.data}
            });
        }
        response.set_content(json{{"events", array}}.dump(), "application/json");
    });

    http.Delete(R"(/api/sessions/([A-Za-z0-9_-]+))", [&](const httplib::Request& request, httplib::Response& response) {
        const auto session_id = request.matches[1].str();
        auto removed = store_.delete_session(session_id);
        if (!removed) {
            response.status = 500;
            response.set_content(json{{"error", removed.error().message}}.dump(), "application/json");
            return;
        }
        response.set_content(json{{"ok", true}, {"session_id", session_id}}.dump(), "application/json");
    });

    const char* bind_address = config_.iteration_candidate.empty() ? "0.0.0.0" : "127.0.0.1";
    if (!http.bind_to_port(bind_address, config_.server_port)) {
        active_.store(nullptr, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return false;
    }

    running_.store(true, std::memory_order_release);
    std::cout << "EngCoder local UI: http://127.0.0.1:" << config_.server_port << "/ui/\n";
    std::cout << "Remote serving: disabled (enable it in EngCoder Lab > Runtime)\n";
    const bool result = http.listen_after_bind();
    running_.store(false, std::memory_order_release);
    active_.store(nullptr, std::memory_order_release);
    return result;
}

void HttpServer::stop() {
    if (auto* server = active_.load(std::memory_order_acquire); server != nullptr) {
        server->stop();
    }
}

} // namespace engcoder::server
