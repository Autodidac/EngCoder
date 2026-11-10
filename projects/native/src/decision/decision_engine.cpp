#include "engcoder/decision/decision_engine.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>


namespace engcoder::decision {
namespace {

[[nodiscard]] std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] std::string basename(std::string path) {
    const auto pos = path.find_last_of("/\\");
    if (pos != std::string::npos) path.erase(0, pos + 1);
    return lower(std::move(path));
}

[[nodiscard]] ToolReview make_review(
    ToolDisposition disposition,
    DecisionProbabilities probabilities,
    std::string reason,
    std::string reviewer) {
    const double confidence = std::max({probabilities.allow, probabilities.approval, probabilities.deny});
    return ToolReview{
        .disposition = disposition,
        .confidence = confidence,
        .reason = std::move(reason),
        .probabilities = probabilities,
        .reviewer = std::move(reviewer),
    };
}

[[nodiscard]] ToolDisposition disposition_for(const DecisionProbabilities& p) {
    if (p.deny >= 0.50) return ToolDisposition::deny;
    if (p.allow >= 0.90) return ToolDisposition::allow;
    return ToolDisposition::require_approval;
}

[[nodiscard]] DecisionProbabilities one_hot(ToolDisposition disposition, double confidence = 1.0) {
    const double remainder = std::max(0.0, 1.0 - confidence);
    switch (disposition) {
        case ToolDisposition::allow:
            return {confidence, remainder, 0.0};
        case ToolDisposition::deny:
            return {0.0, remainder, confidence};
        case ToolDisposition::require_approval:
            return {remainder * 0.5, confidence, remainder * 0.5};
    }
    return {0.0, 1.0, 0.0};
}




[[nodiscard]] bool is_shell(std::string_view executable) {
    static constexpr std::array<std::string_view, 8> shells{
        "cmd", "cmd.exe", "powershell", "powershell.exe", "pwsh", "bash", "sh", "zsh"
    };
    return std::ranges::find(shells, executable) != shells.end();
}

} // namespace

std::expected<ToolReview, core::Error> DeterministicDecisionEngine::review_tool(const ToolProposal& proposal) {
    if (proposal.name.starts_with("workspace.read") || proposal.name == "workspace.tree" ||
        proposal.name == "workspace.search" || proposal.name == "session.search" ||
        proposal.name == "git.status" || proposal.name == "git.diff" ||
        proposal.name == "expert.list" || proposal.name == "expert.call" ||
        proposal.name == "addon.list" || proposal.name == "addon.call") {
        return make_review(ToolDisposition::allow, one_hot(ToolDisposition::allow),
            "read-only workspace/session/model-advisory operation", "deterministic");
    }

    if (proposal.name == "workspace.write" || proposal.name == "workspace.patch") {
        return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
            "checkpointed workspace mutation delegated to weighted review", "deterministic");
    }

    if (proposal.name == "lmstudio.integration") {
        return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
            "integration request delegated to weighted review and configured allowlist", "deterministic");
    }

    if (proposal.name != "process.run") {
        return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
            "unknown side-effect class", "deterministic");
    }

    const auto executable = basename(proposal.arguments.value("executable", std::string{}));
    if (is_shell(executable)) {
        return make_review(ToolDisposition::deny, one_hot(ToolDisposition::deny),
            "shell interpreters are not accepted by the structured process tool", "deterministic");
    }

    if (executable == "git" || executable == "git.exe") {
        return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
            "use fixed git.status/git.diff tools for unattended Git inspection", "deterministic");
    }

    if (executable == "where" || executable == "where.exe" || executable == "which") {
        return make_review(ToolDisposition::allow, one_hot(ToolDisposition::allow, 0.99),
            "read-only executable discovery", "deterministic");
    }

    if (executable == "cmake" || executable == "cmake.exe") {
        const auto args = proposal.arguments.value("arguments", std::vector<std::string>{});
        if (std::ranges::find(args, "-P") != args.end() ||
            std::ranges::find(args, "-E") != args.end() ||
            std::ranges::find(args, "--install") != args.end()) {
            return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
                "cmake script/command/install modes require review", "deterministic");
        }
        return make_review(ToolDisposition::allow, one_hot(ToolDisposition::allow, 0.98),
            "CMake configure/build mode using structured arguments", "deterministic");
    }

    static const std::vector<std::string> build_tools{
        "ctest", "ctest.exe", "ninja", "ninja.exe", "msbuild", "msbuild.exe",
        "cl", "cl.exe", "clang", "clang++", "gcc", "g++"
    };
    if (std::ranges::find(build_tools, executable) != build_tools.end()) {
        return make_review(ToolDisposition::allow, one_hot(ToolDisposition::allow, 0.98),
            "recognized build/test tool using structured arguments", "deterministic");
    }

    return make_review(ToolDisposition::require_approval, one_hot(ToolDisposition::require_approval),
        "unrecognized process requires weighted review", "deterministic");
}

std::expected<ToolReview, core::Error> WeightedDecisionEngine::review_tool(const ToolProposal& proposal) {
    DecisionProbabilities probabilities{0.04, 0.94, 0.02};
    std::string reason = "weighted local policy: ambiguous side effect";

    if (proposal.name.starts_with("workspace.read") || proposal.name == "workspace.tree" ||
        proposal.name == "workspace.search" || proposal.name == "session.search" ||
        proposal.name == "git.status" || proposal.name == "git.diff" ||
        proposal.name == "expert.list" || proposal.name == "expert.call" ||
        proposal.name == "addon.list" || proposal.name == "addon.call") {
        probabilities = {0.995, 0.005, 0.0};
        reason = "weighted local policy: read-only operation or model advisory";
    } else if (proposal.name == "workspace.write" || proposal.name == "workspace.patch") {
        probabilities = {0.972, 0.026, 0.002};
        reason = "weighted local policy: checkpointed, workspace-contained edit";
    } else if (proposal.name == "lmstudio.integration") {
        probabilities = {0.91, 0.085, 0.005};
        reason = "weighted local policy: integration remains constrained by configured allowlist";
    } else if (proposal.name == "process.run") {
        const auto executable = basename(proposal.arguments.value("executable", std::string{}));
        const auto args = proposal.arguments.value("arguments", std::vector<std::string>{});

        if (is_shell(executable)) {
            probabilities = {0.0, 0.001, 0.999};
            reason = "weighted local policy: unrestricted shell surface";
        } else if (executable == "cmake" || executable == "cmake.exe") {
            const bool script_mode = std::ranges::find(args, "-P") != args.end() ||
                std::ranges::find(args, "-E") != args.end() ||
                std::ranges::find(args, "--install") != args.end();
            probabilities = script_mode
                ? DecisionProbabilities{0.12, 0.86, 0.02}
                : DecisionProbabilities{0.985, 0.014, 0.001};
            reason = script_mode
                ? "weighted local policy: CMake command/script/install mode"
                : "weighted local policy: structured CMake configure/build";
        } else if (executable == "ctest" || executable == "ctest.exe" ||
                   executable == "ninja" || executable == "ninja.exe" ||
                   executable == "msbuild" || executable == "msbuild.exe" ||
                   executable == "cl" || executable == "cl.exe" ||
                   executable == "clang" || executable == "clang++" ||
                   executable == "gcc" || executable == "g++") {
            probabilities = {0.982, 0.017, 0.001};
            reason = "weighted local policy: recognized build/test executable";
        } else if (executable == "git" || executable == "git.exe") {
            probabilities = {0.08, 0.90, 0.02};
            reason = "weighted local policy: generic Git mutation surface";
        } else if (executable == "where" || executable == "where.exe" || executable == "which") {
            probabilities = {0.995, 0.005, 0.0};
            reason = "weighted local policy: read-only executable discovery";
        } else if (executable == "python" || executable == "python.exe" ||
                   executable == "python3" || executable == "node" ||
                   executable == "node.exe" || executable == "deno" || executable == "deno.exe") {
            probabilities = {0.10, 0.87, 0.03};
            reason = "weighted local policy: general-purpose script runtime";
        } else {
            probabilities = {0.06, 0.88, 0.06};
            reason = "weighted local policy: unrecognized executable";
        }
    }

    return make_review(disposition_for(probabilities), probabilities, std::move(reason), "local-weighted");
}

CompositeDecisionEngine::CompositeDecisionEngine(
    DeterministicDecisionEngine deterministic,
    WeightedDecisionEngine weighted)
    : deterministic_(std::move(deterministic)),
      weighted_(std::move(weighted)) {}

std::expected<ToolReview, core::Error> CompositeDecisionEngine::review_tool(const ToolProposal& proposal) {
    auto hard = deterministic_.review_tool(proposal);
    if (!hard) return hard;
    if (hard->disposition == ToolDisposition::deny) return hard;

    auto local = weighted_.review_tool(proposal);
    if (!local) return hard;

    // A deterministic allow acts as a safety floor for known fixed-function
    // build/read operations, but the weighted distribution remains visible.
    if (hard->disposition == ToolDisposition::allow && local->disposition != ToolDisposition::deny) {
        local->disposition = local->probabilities.allow >= 0.75
            ? ToolDisposition::allow
            : ToolDisposition::require_approval;
    }

    return local;
}

} // namespace engcoder::decision
