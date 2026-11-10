#include "engcoder/tools/tool_registry.hpp"

#include <algorithm>

namespace engcoder::tools {

ToolRegistry::ToolRegistry(decision::IDecisionEngine& decisions)
    : decisions_(decisions) {}

void ToolRegistry::add(Tool tool) {
    tools_.insert_or_assign(tool.definition.name, std::move(tool));
}

std::vector<json> ToolRegistry::model_definitions() const {
    std::vector<const Tool*> ordered;
    ordered.reserve(tools_.size());
    for (const auto& [_, tool] : tools_) {
        ordered.push_back(&tool);
    }

    std::ranges::sort(ordered, {}, [](const Tool* tool) {
        return tool->definition.name;
    });

    std::vector<json> out;
    out.reserve(ordered.size());
    for (const auto* tool : ordered) {
        out.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool->definition.name},
                {"description", tool->definition.description},
                {"parameters", tool->definition.parameters},
            }},
        });
    }
    return out;
}

std::expected<decision::ToolReview, core::Error> ToolRegistry::review(
    std::string_view name,
    const json& arguments,
    const Context& context) const {
    if (!tools_.contains(std::string{name})) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::tool_not_found),
            "unknown tool: " + std::string{name},
        });
    }

    return decisions_.review_tool({
        std::string{name},
        arguments,
        context.workspace_root,
    });
}

std::expected<json, core::Error> ToolRegistry::invoke(
    std::string_view name,
    const json& arguments,
    const Context& context) const {
    const auto it = tools_.find(std::string{name});
    if (it == tools_.end()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::tool_not_found),
            "unknown tool: " + std::string{name},
        });
    }
    return it->second.handler(arguments, context);
}

std::expected<Execution, core::Error> ToolRegistry::execute(
    std::string_view name,
    const json& arguments,
    const Context& context) const {
    auto reviewed = review(name, arguments, context);
    if (!reviewed) {
        return std::unexpected(reviewed.error());
    }

    if (reviewed->disposition == decision::ToolDisposition::deny) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::tool_denied),
            reviewed->reason,
        });
    }
    if (reviewed->disposition == decision::ToolDisposition::require_approval) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::approval_required),
            reviewed->reason,
        });
    }

    auto value = invoke(name, arguments, context);
    if (!value) {
        return std::unexpected(value.error());
    }
    return Execution{std::move(*value), std::move(*reviewed)};
}

} // namespace engcoder::tools
