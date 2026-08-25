module;
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.tasks;

export import threadspire.core;
export import threadspire.models;

export {
namespace threadspire::ai::routing {

enum class TaskKind {
    conversation,
    explanation,
    transformation,
    translation,
    code_generation,
    code_debugging,
    repository_task,
    planning,
    tool_action,
};

enum class ComplexityTier {
    instant,
    fast,
    standard,
    deep,
};

struct TaskAnalysis final {
    TaskKind kind{TaskKind::conversation};
    ComplexityTier tier{ComplexityTier::fast};
    std::size_t estimated_input_tokens{0};
    std::size_t estimated_output_tokens{128};
    double ambiguity{0.0};
    double risk{0.0};
    bool needs_tools{false};
    bool needs_repository{false};
    bool needs_current_information{false};
    std::vector<std::string> required_capabilities;
    std::vector<std::string> signals;
};

[[nodiscard]] std::string_view to_string(TaskKind kind) noexcept;
[[nodiscard]] std::string_view to_string(ComplexityTier tier) noexcept;

class TaskAnalyzer final {
public:
    [[nodiscard]] TaskAnalysis analyze(std::string_view prompt) const;
};

} // namespace threadspire::ai::routing

namespace threadspire::ai::routing {

enum class RouteMode {
    direct,
    cascade,
    hedged,
    agentic,
};

struct RouteStage final {
    std::string role;
    std::string model_id;
    std::size_t max_output_tokens{256};
    bool parallel{false};
};

struct RoutingPlan final {
    RouteMode mode{RouteMode::direct};
    TaskAnalysis analysis;
    std::vector<RouteStage> stages;
    double expected_latency_ms{0.0};
    std::string rationale;
};

[[nodiscard]] std::string_view to_string(RouteMode mode) noexcept;

class AdaptiveRouter final {
public:
    [[nodiscard]] RoutingPlan plan(
        const TaskAnalysis& analysis,
        const models::ModelRegistry& registry) const;

private:
    [[nodiscard]] const models::ModelProfile* fastest_capable(
        const TaskAnalysis& analysis,
        const models::ModelRegistry& registry,
        double minimum_quality,
        std::string_view preferred_capability = {}) const;
};

} // namespace threadspire::ai::routing

namespace threadspire::ai::long_horizon {

struct Request final {
    std::filesystem::path repository;
    std::filesystem::path state_root;
    std::string backend_base_url;
    models::ModelProfile repair_model;
    std::string task;
    std::string build_command;
    std::string test_command;
    std::filesystem::path adapter_path;
    std::filesystem::path module_path;
    std::size_t maximum_iterations{8};
    bool require_clean_worktree{true};
    bool rollback_on_failure{true};
};

struct Event final {
    std::size_t iteration{0};
    std::string phase;
    int exit_code{-1};
    std::chrono::milliseconds duration{0};
    std::filesystem::path log_path;
};

struct Result final {
    util::OperationResult operation;
    bool completed{false};
    bool compiler_passed{false};
    bool tests_passed{false};
    std::size_t iterations{0};
    std::filesystem::path run_directory;
    std::filesystem::path trajectory_path;
    std::filesystem::path final_diff_path;
    std::string final_diff;
    std::vector<Event> events;
};

[[nodiscard]] Result run(const Request& request, std::stop_token stop_token);

} // namespace threadspire::ai::long_horizon
}
