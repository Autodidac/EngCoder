#include "engcoder/agent/handoff_contract.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace engcoder::agent {
namespace {
[[nodiscard]] bool contains_untrusted_instruction(std::string_view text) noexcept {
    // Provider text is untrusted. Check ASCII syntax/injection markers without
    // allowing capitalization to bypass the handoff contract.
    constexpr std::string_view blocked[] = {
        "```", "#include", "<think>", "</think>", "thinking process",
        "ignore previous", "ignore all previous", "disregard previous",
        "system prompt", "int main(", "\n", "\r"
    };
    return std::ranges::any_of(blocked, [text](std::string_view marker) {
        return std::search(text.begin(), text.end(), marker.begin(), marker.end(),
            [](unsigned char lhs, unsigned char rhs) {
                return std::tolower(lhs) == std::tolower(rhs);
            }) != text.end();
    });
}
}

HandoffValidation validate_handoff(const model::ModelResponse& response) {
    if (!response.tool_calls.empty()) return {{}, "tool calls are prohibited in advisory stages"};
    if (response.raw.is_object() && response.raw.contains("choices") &&
        response.raw["choices"].is_array() && !response.raw["choices"].empty() &&
        response.raw["choices"][0].is_object() &&
        response.raw["choices"][0].contains("finish_reason") &&
        response.raw["choices"][0]["finish_reason"].is_string() &&
        response.raw["choices"][0]["finish_reason"].get<std::string>() == "length") {
        return {{}, "model exhausted output budget before completing its handoff"};
    }
    if (response.text.empty() || response.text.size() > 1600) {
        return {{}, "no bounded visible handoff (reasoning-only output is not a handoff)"};
    }
    const auto parsed = model::json::parse(response.text, nullptr, false);
    if (!parsed.is_object() || parsed.size() != 1 || !parsed.contains("handoff") ||
        !parsed["handoff"].is_array() || parsed["handoff"].empty() || parsed["handoff"].size() > 5) {
        return {{}, "expected a JSON object containing only a handoff array"};
    }
    std::string notes;
    for (const auto& item : parsed["handoff"]) {
        if (!item.is_string()) return {{}, "handoff contains a non-text item"};
        const auto part = item.get<std::string>();
        if (part.size() < 8 || part.size() > 240 || contains_untrusted_instruction(part) ||
            std::ranges::any_of(part, [](unsigned char c) { return c < 32 || c == 127; })) {
            return {{}, "handoff item exceeds limits or contains raw code/instructions"};
        }
        if (!notes.empty()) notes += '\n';
        notes += "- ";
        notes += part;
    }
    return {std::move(notes), {}};
}
} // namespace engcoder::agent
