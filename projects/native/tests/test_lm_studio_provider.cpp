#include "engcoder/model/lm_studio_provider.hpp"

#include <catch2/catch_test_macros.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <ranges>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace {

struct ServerStopGuard {
    httplib::Server& server;
    ~ServerStopGuard() { server.stop(); }
};

} // namespace

TEST_CASE("LM Studio provider resolves auto and reloads an evicted model before inference") {
    using nlohmann::json;

    std::atomic_bool loaded{false};
    std::atomic_int load_count{0};
    std::atomic_int protocol_errors{0};
    std::atomic_int auth_errors{0};
    httplib::Server server;

    const auto authorized = [&](const httplib::Request& request, httplib::Response& response) {
        if (request.get_header_value("Authorization") == "Bearer test-token") return true;
        ++auth_errors;
        response.status = 401;
        response.set_content(R"({"error":"unauthorized"})", "application/json");
        return false;
    };

    server.Get("/api/v1/models", [&](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request, response)) return;
        json instances = json::array();
        if (loaded.load()) instances.push_back(json{{"id", "instance-1"}});
        response.set_content(json{{"models", json::array({
            json{
                {"type", "llm"},
                {"key", "test/model"},
                {"display_name", "Test Model"},
                {"loaded_instances", std::move(instances)},
                {"capabilities", json{{"trained_for_tool_use", true}}},
            }
        })}}.dump(), "application/json");
    });

    server.Post("/api/v1/models/load", [&](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request, response)) return;
        try {
            const auto body = json::parse(request.body);
            if (body.value("model", std::string{}) != "test/model") {
                ++protocol_errors;
                response.status = 400;
                response.set_content(R"({"error":"unexpected model"})", "application/json");
                return;
            }
        } catch (...) {
            ++protocol_errors;
            response.status = 400;
            response.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        loaded.store(true);
        ++load_count;
        response.set_content(json{{"type", "llm"}, {"status", "loaded"}, {"instance_id", "instance-1"}}.dump(), "application/json");
    });

    server.Post("/v1/chat/completions", [&](const httplib::Request& request, httplib::Response& response) {
        if (!authorized(request, response)) return;
        try {
            const auto body = json::parse(request.body);
            if (!loaded.load() || body.value("model", std::string{}) != "test/model") {
                ++protocol_errors;
                response.status = 409;
                response.set_content(R"({"error":"model not loaded or unexpected model"})", "application/json");
                return;
            }
        } catch (...) {
            ++protocol_errors;
            response.status = 400;
            response.set_content(R"({"error":"invalid json"})", "application/json");
            return;
        }
        response.set_content(json{
            {"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", "ok"}}}}})}
        }.dump(), "application/json");
    });

    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard stop_guard{server};
    (void)stop_guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "auto";
    config.model_api = engcoder::core::ModelApi::chat_completions;
    config.model_auto_load = true;
    engcoder::model::LmStudioProvider provider(config);
    REQUIRE_FALSE(provider.token_configured());
    provider.set_token("test-token");
    REQUIRE(provider.token_configured());

    engcoder::model::ModelRequest request;
    request.model = "auto";
    request.messages.push_back({"user", "hi", {}, {}});

    const auto first = provider.complete(request);
    const std::string first_error = first ? std::string{} : first.error().message;
    INFO(first_error);
    REQUIRE(first);
    REQUIRE(first->text == "ok");
    REQUIRE(load_count.load() == 1);

    loaded.store(false); // Simulate LM Studio TTL/manual eviction.
    const auto second = provider.complete(request);
    const std::string second_error = second ? std::string{} : second.error().message;
    INFO(second_error);
    REQUIRE(second);
    REQUIRE(second->text == "ok");
    REQUIRE(load_count.load() == 2);
    REQUIRE(protocol_errors.load() == 0);
    REQUIRE(auth_errors.load() == 0);
}

TEST_CASE("LM Studio provider rejects an already-cancelled request before model discovery") {
    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:1";
    config.model = "auto";
    config.model_api = engcoder::core::ModelApi::chat_completions;
    config.model_auto_load = true;
    engcoder::model::LmStudioProvider provider(config);

    std::stop_source stop;
    stop.request_stop();

    engcoder::model::ModelRequest request;
    request.model = "auto";
    request.stop = stop.get_token();
    request.messages.push_back({"user", "hi", {}, {}});

    const auto result = provider.complete(request);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == engcoder::core::make_error_code(engcoder::core::Errc::cancelled));
}

TEST_CASE("LM Studio provider preserves required and named tool choice") {
    using nlohmann::json;
    httplib::Server server;
    std::mutex bodies_mutex;
    std::vector<json> bodies;
    server.Post("/v1/chat/completions", [&](const httplib::Request& request, httplib::Response& response) {
        {
            std::scoped_lock lock(bodies_mutex);
            bodies.push_back(json::parse(request.body));
        }
        response.set_content(json{{"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", "ok"}}}}})}}.dump(), "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_api = engcoder::core::ModelApi::chat_completions;
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);

    const json tool = {{"type", "function"}, {"function", {{"name", "inspect"}, {"strict", true}, {"parameters", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}}}}};
    engcoder::model::ModelRequest required;
    required.model = "test-model";
    required.messages.push_back({"user", "inspect", {}, {}});
    required.tools = {tool};
    required.tool_choice = "required";
    required.parallel_tool_calls = false;
    REQUIRE(provider.complete(required));

    auto named = required;
    named.tool_choice = json{{"type", "function"}, {"function", {{"name", "inspect"}}}};
    named.parallel_tool_calls = true;
    REQUIRE(provider.complete(named));

    std::scoped_lock lock(bodies_mutex);
    REQUIRE(bodies.size() == 2);
    REQUIRE(bodies[0].at("tool_choice") == "required");
    REQUIRE(bodies[0].at("parallel_tool_calls") == false);
    REQUIRE(bodies[1].at("tool_choice").at("function").at("name") == "inspect");
    REQUIRE(bodies[1].at("parallel_tool_calls") == true);
    REQUIRE(bodies[1].at("tools").at(0).at("function").at("name") == "inspect");
    REQUIRE(bodies[1].at("tools").at(0).at("function").at("strict") == true);
    REQUIRE(bodies[1].at("tools").at(0).at("function").at("parameters").at("additionalProperties") == false);
}

TEST_CASE("LM Studio provider streams content reasoning and incremental tool arguments") {
    using nlohmann::json;
    httplib::Server server;
    std::atomic_bool saw_stream{false};
    server.Post("/v1/chat/completions", [&](const httplib::Request& request, httplib::Response& response) {
        const auto body = json::parse(request.body);
        saw_stream.store(body.value("stream", false));
        const std::string sse =
            "data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"think\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\"hello\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"call_1\",\"function\":{\"name\":\"inspect\",\"arguments\":\"{\\\"path\\\":\"}}]}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"index\":0,\"function\":{\"arguments\":\"\\\"src\\\"}\"}}]}}]}\n\n"
            "data: [DONE]\n\n";
        response.set_content(sse, "text/event-stream");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_api = engcoder::core::ModelApi::chat_completions;
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);

    engcoder::model::ModelRequest req;
    req.model = "test-model";
    req.messages.push_back({"user", "go", {}, {}});
    std::vector<engcoder::model::StreamEvent> events;
    const auto result = provider.complete_stream(req, [&](const auto& e) { events.push_back(e); });
    const std::string result_error = result ? std::string{} : result.error().message;
    INFO(result_error);
    REQUIRE(result.has_value());
    REQUIRE(saw_stream.load());
    REQUIRE(result->text == "hello");
    REQUIRE(result->reasoning == "think");
    REQUIRE(result->tool_calls.size() == 1);
    REQUIRE(result->tool_calls[0].id == "call_1");
    REQUIRE(result->tool_calls[0].name == "inspect");
    REQUIRE(result->tool_calls[0].arguments.at("path") == "src");
    REQUIRE(std::ranges::any_of(events, [](const auto& e) { return e.kind == engcoder::model::StreamEventKind::reasoning_delta; }));
    REQUIRE(std::ranges::any_of(events, [](const auto& e) { return e.kind == engcoder::model::StreamEventKind::tool_argument_delta; }));
}

TEST_CASE("Inference deadline budget supports generation beyond ten minutes without resetting") {
    using namespace std::chrono;
    engcoder::core::Config config;
    REQUIRE(config.inference_timeout_seconds == 3ULL * 60ULL * 60ULL);
    const auto now = steady_clock::now();
    const auto deadline = now + minutes{11};
    const auto remaining = engcoder::model::inference_budget_remaining(deadline, now);
    REQUIRE(remaining >= minutes{11});
    REQUIRE(remaining > minutes{10});
    const auto later = now + minutes{7};
    REQUIRE(engcoder::model::inference_budget_remaining(deadline, later) == minutes{4});
}

TEST_CASE("Timed out generation is not retried and duplicate slot remains blocked until idle") {
    using nlohmann::json;
    using namespace std::chrono_literals;
    httplib::Server server;
    std::atomic_int generations{0};
    std::atomic_bool busy{true};
    server.Post("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& response) {
        ++generations;
        std::this_thread::sleep_for(300ms);
        busy.store(false);
        response.set_content(json{{"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", "late"}}}}})}}.dump(), "application/json");
    });
    server.Get("/slots", [&](const httplib::Request&, httplib::Response& response) {
        response.set_content(json::array({json{{"is_processing", busy.load()}}}).dump(), "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);
    engcoder::model::ModelRequest req;
    req.model = "test-model";
    req.messages.push_back({"user", "slow", {}, {}});
    req.deadline = std::chrono::steady_clock::now() + 75ms;
    const auto first = provider.complete(req);
    REQUIRE_FALSE(first);
    REQUIRE(first.error().code == engcoder::core::make_error_code(engcoder::core::Errc::model_timeout));
    REQUIRE(generations.load() == 1);

    req.deadline = std::chrono::steady_clock::now() + 1s;
    const auto blocked = provider.complete(req);
    REQUIRE_FALSE(blocked);
    REQUIRE(generations.load() == 1);

    for (int i = 0; i < 20 && busy.load(); ++i) std::this_thread::sleep_for(25ms);
    REQUIRE_FALSE(busy.load());
    req.deadline = std::chrono::steady_clock::now() + 1s;
    const auto resumed = provider.complete(req);
    REQUIRE(resumed);
    REQUIRE(generations.load() == 2);
}

TEST_CASE("Provider classifies authentication and context overflow failures") {
    using nlohmann::json;
    httplib::Server server;
    std::atomic_int mode{0};
    server.Post("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& response) {
        if (mode.load() == 0) {
            response.status = 401;
            response.set_content(R"({"error":"unauthorized"})", "application/json");
        } else {
            response.status = 400;
            response.set_content(R"({"error":{"message":"maximum context length exceeded"}})", "application/json");
        }
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);
    engcoder::model::ModelRequest req;
    req.model = "test-model";
    req.messages.push_back({"user", "x", {}, {}});

    auto result = provider.complete(req);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == engcoder::core::make_error_code(engcoder::core::Errc::authentication_error));
    mode.store(1);
    result = provider.complete(req);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == engcoder::core::make_error_code(engcoder::core::Errc::model_context_overflow));
}

TEST_CASE("Provider cancellation aborts an active generation") {
    using nlohmann::json;
    using namespace std::chrono_literals;
    httplib::Server server;
    std::atomic_int generations{0};
    server.Post("/v1/chat/completions", [&](const httplib::Request&, httplib::Response& response) {
        ++generations;
        std::this_thread::sleep_for(750ms);
        response.set_content(json{{"choices", json::array({json{{"message", json{{"role", "assistant"}, {"content", "too late"}}}}})}}.dump(), "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);
    std::stop_source stop;
    engcoder::model::ModelRequest req;
    req.model = "test-model";
    req.stop = stop.get_token();
    req.deadline = std::chrono::steady_clock::now() + 5s;
    req.messages.push_back({"user", "cancel me", {}, {}});

    std::jthread cancel_thread([&] {
        std::this_thread::sleep_for(100ms);
        stop.request_stop();
    });
    const auto result = provider.complete(req);
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == engcoder::core::make_error_code(engcoder::core::Errc::cancelled));
    REQUIRE(generations.load() == 1);
}

TEST_CASE("Responses adapter preserves named tool choice separately from Chat Completions") {
    using nlohmann::json;
    httplib::Server server;
    json captured;
    server.Post("/v1/responses", [&](const httplib::Request& request, httplib::Response& response) {
        captured = json::parse(request.body);
        response.set_content(json{{"output", json::array({json{{"type", "message"}, {"content", json::array({json{{"type", "output_text"}, {"text", "ok"}}})}}})}}.dump(), "application/json");
    });
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::jthread server_thread([&] { server.listen_after_bind(); });
    ServerStopGuard guard{server}; (void)guard;

    engcoder::core::Config config;
    config.lm_studio_url = "http://127.0.0.1:" + std::to_string(port);
    config.model = "test-model";
    config.model_api = engcoder::core::ModelApi::responses;
    config.model_auto_load = false;
    engcoder::model::LmStudioProvider provider(config);

    engcoder::model::ModelRequest req;
    req.model = "test-model";
    req.messages.push_back({"user", "go", {}, {}});
    req.tools = {json{{"type", "function"}, {"function", {{"name", "inspect"}, {"parameters", {{"type", "object"}, {"properties", json::object()}}}}}}};
    req.tool_choice = json{{"type", "function"}, {"function", {{"name", "inspect"}}}};
    req.parallel_tool_calls = false;
    req.max_tokens = 321;
    const auto result = provider.complete(req);
    const std::string result_error = result ? std::string{} : result.error().message;
    INFO(result_error);
    REQUIRE(result.has_value());
    REQUIRE(result->text == "ok");
    REQUIRE(captured.at("tool_choice").at("type") == "function");
    REQUIRE(captured.at("tool_choice").at("name") == "inspect");
    REQUIRE(captured.at("parallel_tool_calls") == false);
    REQUIRE(captured.at("max_output_tokens") == 321);
}
