#pragma once

#include "threadspire/ai/backend/completion_client.hpp"
#include "threadspire/ai/learning/expert_registry.hpp"
#include "threadspire/ai/memory/episode_store.hpp"
#include "threadspire/ai/models/model_registry.hpp"
#include "threadspire/ai/routing/router.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::mcp {
class Client;
}

namespace threadspire::ai::backend {

struct AgentExecutionOptions final {
    bool force_coding_agent{false};
    bool prefer_minimum_compute{true};
    std::string analysis_prompt;
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
    [[nodiscard]] std::string with_memory(
        std::string_view prompt,
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
