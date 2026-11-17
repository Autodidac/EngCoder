#include "engcoder/tools/builtin_tools.hpp"

#include "engcoder/process/process.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engcoder::tools {
namespace {

[[nodiscard]] json string_schema() {
    return json{{"type", "string"}};
}

[[nodiscard]] json boolean_schema() {
    return json{{"type", "boolean"}};
}

[[nodiscard]] json integer_schema(const std::uint64_t minimum, const std::uint64_t maximum) {
    return json{
        {"type", "integer"},
        {"minimum", minimum},
        {"maximum", maximum},
    };
}

[[nodiscard]] json string_array_schema() {
    return json{
        {"type", "array"},
        {"items", string_schema()},
    };
}

[[nodiscard]] json object_schema(
    std::initializer_list<std::pair<std::string_view, json>> properties,
    std::initializer_list<std::string_view> required = {}) {

    json property_object = json::object();
    for (const auto& [name, schema] : properties) {
        property_object[std::string{name}] = schema;
    }

    json schema{
        {"type", "object"},
        {"properties", std::move(property_object)},
        {"additionalProperties", false},
    };

    if (required.size() != 0) {
        auto required_array = json::array();
        for (const auto name : required) {
            required_array.push_back(std::string{name});
        }
        schema["required"] = std::move(required_array);
    }

    return schema;
}

[[nodiscard]] Tool make_tool(
    std::string name,
    std::string description,
    json parameters,
    Handler handler) {

    return Tool{
        .definition = Definition{
            .name = std::move(name),
            .description = std::move(description),
            .parameters = std::move(parameters),
        },
        .handler = std::move(handler),
    };
}

[[nodiscard]] std::expected<json, core::Error> process_result_json(
    const std::expected<process::Result, core::Error>& result) {

    if (!result) {
        return std::unexpected(result.error());
    }

    return json{
        {"exit_code", result->exit_code},
        {"timed_out", result->timed_out},
        {"cancelled", result->cancelled},
        {"stdout", result->stdout_text},
        {"stderr", result->stderr_text},
    };
}

} // namespace

std::expected<std::string, core::Error> workspace_patch_replacement(const json& arguments) {
    const auto invalid = [](std::string message) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), std::move(message)});
    };
    const auto text = arguments.find("replacement");
    const auto lines = arguments.find("replacement_lines");
    if (text != arguments.end() && text->is_string() && lines == arguments.end())
        return text->get<std::string>();
    if (text != arguments.end() || lines == arguments.end() || !lines->is_array())
        return invalid("Supply exactly one of replacement (text) or replacement_lines (array of lines).");

    std::string result;
    bool first = true;
    for (const auto& line : *lines) {
        if (!line.is_string()) return invalid("Each replacement_lines entry must be a string.");
        const auto& value = line.get_ref<const std::string&>();
        if (value.find_first_of("\r\n") != std::string::npos)
            return invalid("Use a separate replacement_lines entry for each physical line.");
        const std::size_t separator = first ? 0 : 1;
        if (separator + value.size() > workspace::max_text_file_bytes - result.size())
            return invalid("Replacement lines exceed the workspace text limit.");
        if (!first) result.push_back('\n');
        result += value;
        first = false;
    }
    return result;
}

void register_builtin_tools(
    ToolRegistry& registry,
    workspace::Workspace& workspace,
    workspace::CheckpointStore& checkpoints,
    persistence::SessionStore& sessions,
    model::LmStudioProvider& lm_studio,
    addons::AddonRegistry& addons,
    const core::Config& config) {


    // Register optional addon tools once. AgentRuntime advertises them only
    // when global or session-assigned subagent models actually exist.
    {
        registry.add(make_tool(
            "addon.list",
            "List configured EngCoder capability addons. This is a read-only local operation.",
            object_schema({}),
            [&addons](const json&, const Context&) -> std::expected<json, core::Error> {
                return addons.describe();
            }));

        registry.add(make_tool(
            "addon.call",
            addons.model_tool_description(),
            object_schema(
                {
                    {"addon", string_schema()},
                    {"prompt", string_schema()},
                    {"context", string_schema()},
                },
                {"addon", "prompt"}),
            [&addons](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
                if (context.stop.stop_requested()) {
                    return std::unexpected(core::Error{
                        core::make_error_code(core::Errc::cancelled),
                        "cancelled",
                    });
                }

                const auto id = arguments.at("addon").get<std::string>();
                const auto assigned = context.addon_models.is_object() && context.addon_models.contains(id) &&
                    context.addon_models.at(id).is_string()
                        ? context.addon_models.at(id).get<std::string>() : std::string{};
                auto result = addons.invoke(id,
                    arguments.at("prompt").get<std::string>(),
                    arguments.value("context", std::string{}), assigned, context.stop);
                if (!result) return std::unexpected(result.error());

                return json{
                    {"addon", result->addon_id},
                    {"model", result->model},
                    {"text", result->text},
                    {"advisory", true},
                };
            }));

        // 0.2.x compatibility aliases. New prompts and UIs expose addon.* only.
        registry.add(make_tool(
            "expert.list",
            "Compatibility alias for addon.list. Read-only; prefer addon.list.",
            object_schema({}),
            [&addons](const json&, const Context&) -> std::expected<json, core::Error> {
                auto result = addons.describe();
                result["deprecated_alias"] = "expert.list";
                result["replacement"] = "addon.list";
                return result;
            }));

        registry.add(make_tool(
            "expert.call",
            "Compatibility alias for addon.call. Prefer addon.call.",
            object_schema(
                {
                    {"expert", string_schema()},
                    {"addon", string_schema()},
                    {"prompt", string_schema()},
                    {"context", string_schema()},
                },
                {"prompt"}),
            [&addons](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
                if (context.stop.stop_requested()) {
                    return std::unexpected(core::Error{
                        core::make_error_code(core::Errc::cancelled),
                        "cancelled",
                    });
                }
                const auto id = arguments.value("addon", arguments.value("expert", std::string{}));
                if (id.empty()) {
                    return std::unexpected(core::Error{
                        core::make_error_code(core::Errc::invalid_argument),
                        "expert.call requires an expert/addon id",
                    });
                }
                const auto assigned = context.addon_models.is_object() && context.addon_models.contains(id) &&
                    context.addon_models.at(id).is_string()
                        ? context.addon_models.at(id).get<std::string>() : std::string{};
                auto result = addons.invoke(id,
                    arguments.at("prompt").get<std::string>(),
                    arguments.value("context", std::string{}), assigned, context.stop);
                if (!result) return std::unexpected(result.error());
                return json{
                    {"addon", result->addon_id},
                    {"model", result->model},
                    {"text", result->text},
                    {"advisory", true},
                    {"deprecated_alias", "expert.call"},
                    {"replacement", "addon.call"},
                };
            }));
    }

    registry.add(make_tool(
        "workspace.tree",
        "List project files recursively and report the workspace root. Use this for repository/base-directory discovery. Ignores common generated/vendor directories.",
        object_schema({
            {"path", string_schema()},
            {"max_entries", integer_schema(1, 5000)},
        }),
        [&workspace](const json& arguments, const Context&) -> std::expected<json, core::Error> {
            const auto result = workspace.tree(
                arguments.value("path", std::string{"."}),
                arguments.value("max_entries", std::size_t{2000}));

            if (!result) {
                return std::unexpected(result.error());
            }

            return json{{"root", workspace.root().string()}, {"entries", *result}};
        }));

    registry.add(make_tool(
        "workspace.read",
        "Read a UTF-8/text project file inside the workspace. In candidate mode or with line arguments, "
        "return a bounded page of complete lines. Use the one-based start_line from next_line to continue. "
        "In candidate mode, read just the lines to replace and use the returned source_ref for workspace.patch.",
        object_schema(
            {
                {"path", string_schema()},
                {"start_line", integer_schema(1, workspace::max_text_file_bytes)},
                {"max_lines", integer_schema(1, workspace::max_text_page_lines)},
            },
            {"path"}),
        [&workspace](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
            const auto& path = arguments.at("path").get_ref<const std::string&>();
            if (context.candidate_mode || arguments.contains("start_line") || arguments.contains("max_lines")) {
                if ((arguments.contains("start_line") && !arguments.at("start_line").is_number_integer()) ||
                    (arguments.contains("max_lines") && !arguments.at("max_lines").is_number_integer())) {
                    return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument),
                        "start_line and max_lines must be positive integers"});
                }
                const auto start_line = arguments.value("start_line", std::uint64_t{1});
                const auto max_lines = arguments.value("max_lines", std::uint64_t{workspace::default_text_page_lines});
                if (start_line > workspace::max_text_file_bytes || max_lines > workspace::max_text_page_lines) {
                    return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument),
                        "workspace read page is outside the line limits"});
                }
                auto page = workspace.read_text_page(path, static_cast<std::size_t>(start_line), static_cast<std::size_t>(max_lines));
                if (!page) return std::unexpected(page.error());
                return json{{"content", std::move(page->content)}, {"start_line", start_line},
                    {"next_line", page->next_line ? json(*page->next_line) : json(nullptr)}};
            }
            const auto result = workspace.read_text(path);
            if (!result) {
                return std::unexpected(result.error());
            }
            return json{{"content", *result}};
        }));

    registry.add(make_tool(
        "workspace.search",
        "Search text in project files and return file:line matches.",
        object_schema(
            {
                {"query", string_schema()},
                {"max_results", integer_schema(1, 500)},
            },
            {"query"}),
        [&workspace](const json& arguments, const Context&) -> std::expected<json, core::Error> {
            const auto result = workspace.search_text(
                arguments.at("query").get<std::string>(),
                arguments.value("max_results", std::size_t{200}));

            if (!result) {
                return std::unexpected(result.error());
            }

            return json{{"matches", *result}};
        }));

    registry.add(make_tool(
        "workspace.write",
        "Create or replace a text file through a same-directory temporary file. Existing content is checkpointed first.",
        object_schema(
            {
                {"path", string_schema()},
                {"content", string_schema()},
            },
            {"path", "content"}),
        [&workspace, &checkpoints](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
            const auto path = arguments.at("path").get<std::string>();

            const auto checkpoint = checkpoints.capture(path);
            if (!checkpoint) {
                return std::unexpected(checkpoint.error());
            }

            const auto write_result = workspace.write_text(
                path,
                arguments.at("content").get<std::string>(), !context.candidate_mode);

            if (!write_result) {
                return std::unexpected(write_result.error());
            }

            return json{
                {"ok", true},
                {"checkpoint_id", *checkpoint},
            };
        }));

    registry.add(make_tool(
        "workspace.patch",
        "Replace one exact text block. Supply expected text or, in candidate mode, source_ref from a bounded workspace.read of the same path. "
        "Use replacement_lines (one JSON string per physical line) for multiline C++, or replacement for exact text. "
        "Uniform CRLF files retain CRLF. Missing or ambiguous matches fail.",
        object_schema(
            {
                {"path", string_schema()},
                {"expected", string_schema()},
                {"source_ref", string_schema()},
                {"replacement", string_schema()},
                {"replacement_lines", string_array_schema()},
            },
            {"path"}),
        [&workspace, &checkpoints](const json& arguments, const Context&) -> std::expected<json, core::Error> {
            if (!arguments.contains("expected") || !arguments.at("expected").is_string() ||
                arguments.contains("source_ref")) {
                return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument),
                    "An exact expected block is required. A source_ref must be resolved by the candidate host."});
            }
            const auto path = arguments.at("path").get<std::string>();
            const auto& expected = arguments.at("expected").get_ref<const std::string&>();
            const auto replacement = workspace_patch_replacement(arguments);
            if (!replacement) return std::unexpected(replacement.error());

            auto text = workspace.read_text(path);
            if (!text) {
                return std::unexpected(text.error());
            }

            const auto patch = workspace::plan_text_patch(*text, expected, *replacement);
            if (!patch) return std::unexpected(patch.error());

            const auto checkpoint = checkpoints.capture(path);
            if (!checkpoint) {
                return std::unexpected(checkpoint.error());
            }

            text->replace(patch->offset, patch->length, patch->replacement);
            const auto write_result = workspace.write_text(path, *text);
            if (!write_result) {
                return std::unexpected(write_result.error());
            }

            return json{
                {"ok", true},
                {"checkpoint_id", *checkpoint},
            };
        }));

    registry.add(make_tool(
        "process.run",
        "Run one executable directly with a structured argument array in the workspace. Reserve this for configure/build/test or a specific binary when no fixed tool fits; use workspace.tree/read/search for repository inspection and git.status/git.diff for Git inspection. No shell interpretation occurs.",
        object_schema(
            {
                {"executable", string_schema()},
                {"arguments", string_array_schema()},
                {"cwd", string_schema()},
                {"timeout_ms", integer_schema(1, 1'800'000)},
            },
            {"executable"}),
        [&workspace](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
            const auto working_directory = workspace.resolve(arguments.value("cwd", std::string{"."}), true);
            if (!working_directory) {
                return std::unexpected(working_directory.error());
            }

            process::Request request;
            request.executable = arguments.at("executable").get<std::string>();
            request.arguments = arguments.value("arguments", std::vector<std::string>{});
            request.working_directory = *working_directory;
            request.timeout = std::chrono::milliseconds{
                arguments.value("timeout_ms", std::int64_t{300'000})};

            return process_result_json(process::run(request, context.stop));
        }));

    registry.add(make_tool(
        "git.status",
        "Get repository status using a fixed read-only Git invocation.",
        object_schema({}),
        [&workspace](const json&, const Context& context) -> std::expected<json, core::Error> {
            process::Request request;
            request.executable = "git";
            request.arguments = {"status", "--porcelain=v1", "--branch"};
            request.working_directory = workspace.root();
            return process_result_json(process::run(request, context.stop));
        }));

    registry.add(make_tool(
        "git.diff",
        "Get a read-only Git diff. Optional path is resolved inside the workspace and passed after --.",
        object_schema({
            {"staged", boolean_schema()},
            {"path", string_schema()},
        }),
        [&workspace](const json& arguments, const Context& context) -> std::expected<json, core::Error> {
            process::Request request;
            request.executable = "git";
            request.arguments = {"--no-pager", "diff", "--no-ext-diff"};
            request.working_directory = workspace.root();

            if (arguments.value("staged", false)) {
                request.arguments.emplace_back("--cached");
            }

            if (arguments.contains("path")) {
                const auto path = arguments.at("path").get<std::string>();
                if (!path.empty()) {
                    const auto resolved = workspace.resolve(path, false);
                    if (!resolved) {
                        return std::unexpected(resolved.error());
                    }

                    std::error_code error;
                    const auto relative = std::filesystem::relative(*resolved, workspace.root(), error);
                    if (error) {
                        return std::unexpected(core::Error{
                            core::make_error_code(core::Errc::io_error),
                            "failed to make Git path relative to workspace: " + error.message(),
                        });
                    }

                    request.arguments.emplace_back("--");
                    request.arguments.push_back(relative.generic_string());
                }
            }

            return process_result_json(process::run(request, context.stop));
        }));

    registry.add(make_tool(
        "lmstudio.integration",
        "Delegate a focused task to an MCP integration configured in LM Studio's server. Only explicitly configured integrations are callable and unattended execution requires auto-allow.",
        object_schema(
            {
                {"integration", string_schema()},
                {"prompt", string_schema()},
            },
            {"integration", "prompt"}),
        [&lm_studio, &config](const json& arguments, const Context&) -> std::expected<json, core::Error> {
            const auto integration = arguments.at("integration").get<std::string>();

            if (std::ranges::find(config.lm_studio_integrations, integration) == config.lm_studio_integrations.end()) {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::tool_denied),
                    "LM Studio integration is not configured for this harness",
                });
            }

            if (std::ranges::find(config.lm_studio_auto_allow_integrations, integration) == config.lm_studio_auto_allow_integrations.end()) {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::approval_required),
                    "LM Studio integration is configured but not auto-authorized",
                });
            }

            auto result = lm_studio.native_chat(
                arguments.at("prompt").get<std::string>(),
                {integration});

            if (!result) {
                return std::unexpected(result.error());
            }

            return *result;
        }));

    registry.add(make_tool(
        "session.search",
        "Search persisted session transcripts/events for details lost from active context or from other sessions.",
        object_schema(
            {
                {"query", string_schema()},
                {"limit", integer_schema(1, 100)},
            },
            {"query"}),
        [&sessions](const json& arguments, const Context&) -> std::expected<json, core::Error> {
            const auto result = sessions.search_sessions(
                arguments.at("query").get<std::string>(),
                arguments.value("limit", std::size_t{50}));

            if (!result) {
                return std::unexpected(result.error());
            }

            return json{{"results", *result}};
        }));
}

} // namespace engcoder::tools
