#pragma once

#include <atomic>
#include <condition_variable>
#include <expected>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>

#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/agent/context_manager.hpp"
#include "engcoder/controller/architect_controller.hpp"
#include "engcoder/core/config.hpp"
#include "engcoder/core/event.hpp"
#include "engcoder/learning/learning_service.hpp"
#include "engcoder/model/model_provider.hpp"
#include "engcoder/models/model_registry.hpp"
#include "engcoder/persistence/session_store.hpp"
#include "engcoder/residency/model_residency_manager.hpp"
#include "engcoder/tools/tool_registry.hpp"
#include "engcoder/training/training_service.hpp"

namespace engcoder::agent {

enum class TaskPhase { created, preparing, reasoning, executing_tool, observing, verifying, completed, failed, cancelled, awaiting_approval };

struct TaskRequest {
    std::string project_id;
    std::string session_id;
    std::string task_id;
    std::string user_message;
    std::string foundation_model;
    std::string simple_model; // optional external lightweight response model
    std::string retry_model; // exact prior response model, for an explicit retry only
    std::string review_model; // optional external middle-tier reviewer (no tools)
    std::string review_mode{"auto"}; // off / auto / deep
    std::string thinking_mode{"auto"}; // auto / fast / deep (prompt policy, not a backend switch)
    std::string controller_model;
    std::string forced_addon;
    core::json addon_models = core::json::object();
    std::string session_instructions;
    double temperature{0.15};
    int max_tokens{-1};
    bool include_history{true};
    std::size_t max_history_turns{0};
    bool auto_route{true};
    bool addons_enabled{true};
    bool wait_for_approval{false};
    bool self_iteration{false};
};

struct TaskResult {
    std::string final_text;
    TaskPhase phase{TaskPhase::completed};
};

class AgentRuntime {
public:
    AgentRuntime(
        core::Config config,
        model::IModelProvider& model,
        addons::AddonRegistry& addons,
        controller::ArchitectController& controller,
        models::ModelRegistry& models,
        residency::ModelResidencyManager& residency,
        training::TrainingService& training,
        learning::LearningService& learning,
        tools::ToolRegistry& tools,
        persistence::SessionStore& store,
        core::EventBus& events);

    [[nodiscard]] std::expected<TaskResult, core::Error> run(const TaskRequest&, std::stop_token = {});
    [[nodiscard]] bool resolve_approval(std::string_view task_id, std::string_view call_id, bool approved);

private:
    void emit(const TaskRequest&, std::string type, core::json data = core::json::object());
    [[nodiscard]] std::string system_prompt() const;

    core::Config config_;
    model::IModelProvider& model_;
    addons::AddonRegistry& addons_;
    controller::ArchitectController& controller_;
    models::ModelRegistry& models_;
    residency::ModelResidencyManager& residency_;
    training::TrainingService& training_;
    learning::LearningService& learning_;
    tools::ToolRegistry& tools_;
    persistence::SessionStore& store_;
    core::EventBus& events_;
    ContextManager context_;
    struct PendingApproval {
        std::string call_id;
        bool decided{false};
        bool approved{false};
    };

    std::atomic_uint64_t sequence_{0};
    std::mutex execution_mutex_;
    std::mutex approval_mutex_;
    std::condition_variable_any approval_cv_;
    std::unordered_map<std::string, PendingApproval> pending_approvals_;
};

} // namespace engcoder::agent
