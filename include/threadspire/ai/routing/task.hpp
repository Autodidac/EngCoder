#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

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
