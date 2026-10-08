#pragma once

#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/core/error.hpp"

namespace engcoder::decision {
using json = nlohmann::json;

enum class ToolDisposition { allow, deny, require_approval };

struct DecisionProbabilities {
    double allow{0.0};
    double approval{1.0};
    double deny{0.0};
};

struct ToolReview {
    ToolDisposition disposition{ToolDisposition::require_approval};
    double confidence{1.0};
    std::string reason;
    DecisionProbabilities probabilities{};
    std::string reviewer{"deterministic"};
};

struct ToolProposal {
    std::string name;
    json arguments;
    std::string workspace_root;
};

class IDecisionEngine {
public:
    virtual ~IDecisionEngine() = default;
    [[nodiscard]] virtual std::expected<ToolReview, core::Error> review_tool(const ToolProposal&) = 0;
};

class DeterministicDecisionEngine final : public IDecisionEngine {
public:
    [[nodiscard]] std::expected<ToolReview, core::Error> review_tool(const ToolProposal&) override;
};

// A local, fast Architect-side reviewer. It exposes explicit probabilities
// so the harness can threshold uncertainty without asking the generative model.
class WeightedDecisionEngine final : public IDecisionEngine {
public:
    [[nodiscard]] std::expected<ToolReview, core::Error> review_tool(const ToolProposal&) override;
};

class CompositeDecisionEngine final : public IDecisionEngine {
public:
    CompositeDecisionEngine(
        DeterministicDecisionEngine deterministic,
        WeightedDecisionEngine weighted);

    [[nodiscard]] std::expected<ToolReview, core::Error> review_tool(const ToolProposal&) override;

private:
    DeterministicDecisionEngine deterministic_;
    WeightedDecisionEngine weighted_;
};

} // namespace engcoder::decision
