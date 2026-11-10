#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "engcoder/model/model_provider.hpp"

namespace engcoder::agent {

struct ContextPolicy {
    std::uint32_t context_tokens{};
    std::uint32_t reserve_tokens{8192};
    bool auto_compact{true};
    double trigger_ratio{0.90};
    double target_ratio{0.70};
};

struct ContextPreparation {
    std::vector<model::Message> messages;
    bool compacted{};
    std::size_t estimated_tokens_before{};
    std::size_t estimated_tokens_after{};
    std::size_t compacted_messages{};
};

class ContextManager {
public:
    ContextManager(std::size_t max_chars, std::size_t max_tool_output_chars);
    [[nodiscard]] std::vector<model::Message> prepare(const std::vector<model::Message>& messages) const;
    [[nodiscard]] std::vector<model::Message> prepare(const std::vector<model::Message>& messages, const ContextPolicy& policy) const;
    [[nodiscard]] ContextPreparation prepare_detailed(const std::vector<model::Message>& messages, const ContextPolicy& policy) const;
    [[nodiscard]] static std::size_t estimate_tokens(const std::vector<model::Message>& messages) noexcept;
private:
    [[nodiscard]] std::vector<model::Message> normalize_tools(const std::vector<model::Message>& messages) const;
    std::size_t max_chars_;
    std::size_t max_tool_output_chars_;
};

} // namespace engcoder::agent
