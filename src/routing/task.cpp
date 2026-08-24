#include "threadspire/ai/routing/task.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <string>
#include <string_view>

namespace threadspire::ai::routing {
namespace {

[[nodiscard]] std::string lower_ascii(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] bool contains_any(
    const std::string_view text,
    const std::initializer_list<std::string_view> values) {
    return std::ranges::any_of(values, [text](const std::string_view value) {
        return text.find(value) != std::string_view::npos;
    });
}

void add_capability(TaskAnalysis& analysis, std::string capability) {
    if (std::ranges::find(analysis.required_capabilities, capability) ==
        analysis.required_capabilities.end()) {
        analysis.required_capabilities.push_back(std::move(capability));
    }
}

void add_signal(TaskAnalysis& analysis, std::string signal) {
    analysis.signals.push_back(std::move(signal));
}

[[nodiscard]] std::size_t approximate_tokens(const std::string_view text) noexcept {
    if (text.empty()) {
        return 0;
    }
    const auto bytes = static_cast<double>(text.size());
    return std::max<std::size_t>(1U, static_cast<std::size_t>(std::ceil(bytes / 3.7)));
}

} // namespace

std::string_view to_string(const TaskKind kind) noexcept {
    switch (kind) {
    case TaskKind::conversation:
        return "conversation";
    case TaskKind::explanation:
        return "explanation";
    case TaskKind::transformation:
        return "transformation";
    case TaskKind::translation:
        return "translation";
    case TaskKind::code_generation:
        return "code-generation";
    case TaskKind::code_debugging:
        return "code-debugging";
    case TaskKind::repository_task:
        return "repository-task";
    case TaskKind::planning:
        return "planning";
    case TaskKind::tool_action:
        return "tool-action";
    }
    return "conversation";
}

std::string_view to_string(const ComplexityTier tier) noexcept {
    switch (tier) {
    case ComplexityTier::instant:
        return "instant";
    case ComplexityTier::fast:
        return "fast";
    case ComplexityTier::standard:
        return "standard";
    case ComplexityTier::deep:
        return "deep";
    }
    return "fast";
}

TaskAnalysis TaskAnalyzer::analyze(const std::string_view prompt) const {
    TaskAnalysis result;
    result.estimated_input_tokens = approximate_tokens(prompt);
    result.estimated_output_tokens = 128;
    add_capability(result, "english");

    const auto lower = lower_ascii(prompt);
    const auto has_code_block = lower.find("```") != std::string::npos;
    const auto cpp_signal = contains_any(lower, {
        "c++", "cpp", "std::", "cmake", "msvc", "visual studio", ".hpp", ".cpp",
        "compiler", "linker", "undefined behavior", "template", "constexpr", "concept",
    });
    const auto debugging_signal = contains_any(lower, {
        "error ", "warning ", "fails", "failed", "crash", "bug", "debug", "diagnose",
        "stack trace", "assert", "sanitizer", "doesn't compile", "does not compile",
    });
    const auto repository_signal = contains_any(lower, {
        "repository", "repo", "project", "codebase", "all files", "build system",
        "implement the feature", "refactor", "pull request", "long horizon",
    });
    const auto planning_signal = contains_any(lower, {
        "architecture", "design", "roadmap", "strategy", "plan", "tradeoff", "trade-off",
        "compare approaches", "step by step", "multi-step",
    });
    const auto tool_signal = contains_any(lower, {
        "run ", "build ", "compile ", "test ", "search ", "open ", "inspect ", "execute ",
    });
    const auto translation_signal = contains_any(lower, {
        "translate", "translation", "in spanish", "in french", "in german", "to english",
    });
    const auto transformation_signal = contains_any(lower, {
        "rewrite", "rephrase", "shorten", "summarize", "format this", "convert this",
    });
    const auto explanation_signal = contains_any(lower, {
        "what is", "how does", "explain", "why ", "difference between", "define ",
    });
    const auto current_signal = contains_any(lower, {
        "latest", "today", "current", "right now", "news", "price", "weather",
    });
    const auto high_risk_signal = contains_any(lower, {
        "delete", "erase", "production", "deploy", "security", "credential", "financial",
        "medical", "legal", "kernel", "driver", "unsafe", "data loss",
    });

    if (translation_signal) {
        result.kind = TaskKind::translation;
        result.tier = ComplexityTier::fast;
        result.estimated_output_tokens = std::max<std::size_t>(96, result.estimated_input_tokens);
        add_capability(result, "translation");
        add_signal(result, "translation request");
    } else if (repository_signal && cpp_signal) {
        result.kind = TaskKind::repository_task;
        result.tier = ComplexityTier::deep;
        result.needs_repository = true;
        result.needs_tools = true;
        result.estimated_output_tokens = 1536;
        add_capability(result, "cpp23");
        add_capability(result, "repository");
        add_capability(result, "planning");
        add_signal(result, "repository-level C++ task");
    } else if (cpp_signal && debugging_signal) {
        result.kind = TaskKind::code_debugging;
        result.tier = has_code_block || result.estimated_input_tokens > 350
            ? ComplexityTier::deep
            : ComplexityTier::standard;
        result.needs_tools = tool_signal;
        result.estimated_output_tokens = result.tier == ComplexityTier::deep ? 1200U : 700U;
        add_capability(result, "cpp23");
        add_capability(result, "debugging");
        add_signal(result, "C++ diagnostics");
    } else if (cpp_signal || has_code_block) {
        result.kind = TaskKind::code_generation;
        result.tier = result.estimated_input_tokens > 350 || planning_signal
            ? ComplexityTier::deep
            : ComplexityTier::standard;
        result.estimated_output_tokens = result.tier == ComplexityTier::deep ? 1400U : 800U;
        add_capability(result, "cpp23");
        add_signal(result, "code request");
    } else if (planning_signal) {
        result.kind = TaskKind::planning;
        result.tier = result.estimated_input_tokens > 220
            ? ComplexityTier::deep
            : ComplexityTier::standard;
        result.estimated_output_tokens = result.tier == ComplexityTier::deep ? 1000U : 600U;
        add_capability(result, "planning");
        add_signal(result, "planning or architecture");
    } else if (transformation_signal) {
        result.kind = TaskKind::transformation;
        result.tier = ComplexityTier::fast;
        result.estimated_output_tokens = std::max<std::size_t>(96U, result.estimated_input_tokens);
        add_capability(result, "transformation");
        add_signal(result, "bounded text transformation");
    } else if (explanation_signal) {
        result.kind = TaskKind::explanation;
        result.tier = result.estimated_input_tokens < 80
            ? ComplexityTier::fast
            : ComplexityTier::standard;
        result.estimated_output_tokens = result.tier == ComplexityTier::fast ? 300U : 650U;
        add_capability(result, "explanation");
        add_signal(result, "explanation request");
    } else if (tool_signal) {
        result.kind = TaskKind::tool_action;
        result.tier = ComplexityTier::standard;
        result.needs_tools = true;
        result.estimated_output_tokens = 400;
        add_capability(result, "tools");
        add_signal(result, "tool action");
    } else if (result.estimated_input_tokens <= 18) {
        result.kind = TaskKind::conversation;
        result.tier = ComplexityTier::instant;
        result.estimated_output_tokens = 96;
        add_capability(result, "chat");
        add_signal(result, "short conversational prompt");
    } else {
        result.kind = TaskKind::conversation;
        result.tier = ComplexityTier::fast;
        result.estimated_output_tokens = 240;
        add_capability(result, "chat");
    }

    if (result.estimated_input_tokens > 900) {
        result.tier = ComplexityTier::deep;
        result.ambiguity = std::max(result.ambiguity, 0.45);
        add_signal(result, "large prompt");
    } else if (result.estimated_input_tokens > 300 && result.tier < ComplexityTier::standard) {
        result.tier = ComplexityTier::standard;
        add_signal(result, "moderate prompt size");
    }

    const auto question_marks = static_cast<double>(std::ranges::count(prompt, '?'));
    const auto alternatives = contains_any(lower, {" or ", "maybe", "perhaps", "not sure", "i think"});
    result.ambiguity = std::clamp(
        result.ambiguity + std::min(0.25, question_marks * 0.04) + (alternatives ? 0.18 : 0.0),
        0.0,
        1.0);

    result.risk = high_risk_signal ? 0.75 : 0.1;
    if (result.needs_tools || result.needs_repository) {
        result.risk = std::max(result.risk, 0.4);
    }
    if (debugging_signal) {
        result.risk = std::max(result.risk, 0.35);
    }
    if (current_signal) {
        result.needs_current_information = true;
        result.needs_tools = true;
        result.risk = std::max(result.risk, 0.3);
        add_capability(result, "current-information");
        add_signal(result, "time-sensitive information");
    }

    return result;
}

} // namespace threadspire::ai::routing
