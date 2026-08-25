module;
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.providers;

export import :unified;
export import :adapters;
export import threadspire.learning;
export import threadspire.models;
export import threadspire.storage;
export import threadspire.tasks;

export {
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

namespace threadspire::ai::mcp {
class Client;
}

namespace threadspire::ai::backend {

struct AgentExecutionOptions final {
    bool force_coding_agent{false};
    bool prefer_minimum_compute{true};
    std::string analysis_prompt;
    std::string context;
    TokenCallback on_token;
    CompletionStopPredicate should_stop;
    mcp::Client* mcp_client{nullptr};
    std::size_t maximum_tool_rounds{4};
};

struct AgentResult final {
    bool succeeded{false};
    std::string response;
    std::string error;
    routing::RoutingPlan plan;
    std::vector<std::string> models_used;
    std::vector<std::string> tool_events;
    double effective_tokens_per_second{0.0};
};

class AgentOrchestrator final {
public:
    AgentOrchestrator(
        models::ModelRegistry& models,
        std::vector<learning::ExpertModule> experts,
        const memory::EpisodeStore& memory);

    [[nodiscard]] AgentResult execute(
        std::string_view internal_base_url,
        std::string prompt,
        AgentExecutionOptions options = {});

private:
    [[nodiscard]] CompletionResult run_stage(
        std::string_view internal_base_url,
        const routing::RouteStage& stage,
        std::string system_prompt,
        std::string prompt,
        bool expose_tokens = false);
    [[nodiscard]] std::string skill_for(const routing::TaskAnalysis& analysis) const;
    [[nodiscard]] std::string memory_context(
        std::string_view current_prompt,
        const routing::TaskAnalysis& analysis) const;

    models::ModelRegistry& models_;
    std::vector<learning::ExpertModule> experts_;
    const memory::EpisodeStore& memory_;
    routing::TaskAnalyzer analyzer_;
    routing::AdaptiveRouter router_;
    CompletionClient client_;
    routing::TaskAnalysis active_analysis_;
    AgentExecutionOptions active_options_;
    std::vector<std::string> active_tool_events_;
};

} // namespace threadspire::ai::backend
}
