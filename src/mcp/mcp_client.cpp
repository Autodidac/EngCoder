#include "threadspire/ai/mcp/mcp_client.hpp"

#include "threadspire/ai/backend/http_client.hpp"
#include "threadspire/ai/core/ini.hpp"
#include "threadspire/ai/core/json.hpp"
#include "threadspire/ai/memory/episode_store.hpp"
#include "threadspire/ai/platform/process.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace threadspire::ai::mcp {
namespace {

constexpr std::string_view preferred_protocol = "2025-11-25";

[[nodiscard]] std::vector<std::string> split_arguments(const std::string_view value) {
    std::vector<std::string> result;
    std::string current;
    bool quoted = false;
    bool escaped = false;
    for (const char character : value) {
        if (escaped) {
            current.push_back(character);
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (!quoted && (character == ' ' || character == '\t')) {
            if (!current.empty()) {
                result.push_back(std::move(current));
                current.clear();
            }
        } else {
            current.push_back(character);
        }
    }
    if (!current.empty()) {
        result.push_back(std::move(current));
    }
    return result;
}

[[nodiscard]] std::string join_arguments(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            stream << ' ';
        }
        const auto& value = values[index];
        const auto quote = value.find_first_of(" \t\"") != std::string::npos;
        if (quote) {
            stream << '"';
        }
        for (const char character : value) {
            if (character == '"' || character == '\\') {
                stream << '\\';
            }
            stream << character;
        }
        if (quote) {
            stream << '"';
        }
    }
    return stream.str();
}

[[nodiscard]] std::vector<std::string> split_csv(const std::string_view value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(',', begin);
        auto item = std::string(value.substr(
            begin,
            end == std::string_view::npos ? value.size() - begin : end - begin));
        const auto first = item.find_first_not_of(" \t\r\n");
        const auto last = item.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            item = item.substr(first, last - first + 1U);
            if (!item.empty()) {
                result.push_back(std::move(item));
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return result;
}

[[nodiscard]] std::string json_rpc_request(
    const int id,
    const std::string_view method,
    core::json::Value parameters) {
    return core::json::stringify(core::json::Value(core::json::Value::Object{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method},
        {"params", std::move(parameters)},
    }));
}

[[nodiscard]] std::string extract_sse_json(const std::string_view body) {
    std::istringstream stream{std::string(body)};
    std::string line;
    std::string latest;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.starts_with("data:")) {
            continue;
        }
        auto payload = std::string_view(line).substr(5U);
        while (!payload.empty() && payload.front() == ' ') {
            payload.remove_prefix(1U);
        }
        if (payload != "[DONE]") {
            latest.assign(payload);
        }
    }
    return latest;
}

struct HttpSession final {
    std::string protocol_version;
    std::string session_id;
};

[[nodiscard]] backend::HttpResponse mcp_post(
    const ServerProfile& server,
    const std::string_view body,
    const HttpSession& session) {
    std::vector<backend::HttpHeader> headers{
        {.name = "Content-Type", .value = "application/json"},
        {.name = "Accept", .value = "application/json, text/event-stream"},
    };
    if (!session.protocol_version.empty()) {
        headers.push_back({.name = "MCP-Protocol-Version", .value = session.protocol_version});
    }
    if (!session.session_id.empty()) {
        headers.push_back({.name = "Mcp-Session-Id", .value = session.session_id});
    }
    return backend::http_request(backend::HttpRequest{
        .method = "POST",
        .url = server.endpoint,
        .body = std::string(body),
        .headers = std::move(headers),
        .timeout = std::chrono::minutes(2),
    });
}

[[nodiscard]] std::optional<core::json::Value> response_document(
    const backend::HttpResponse& response,
    std::string& error) {
    auto body = response.body;
    if (const auto content_type = response.header("Content-Type");
        content_type.has_value() && content_type->find("text/event-stream") != std::string::npos) {
        body = extract_sse_json(body);
    }
    return core::json::try_parse(body, error);
}

[[nodiscard]] std::vector<Tool> parse_tools(
    const std::string_view server_id,
    const core::json::Value& document) {
    std::vector<Tool> tools;
    const auto* result = document.find("result");
    const auto* list = result == nullptr ? nullptr : result->find("tools");
    const auto* array = list == nullptr ? nullptr : list->as_array();
    if (array == nullptr) {
        return tools;
    }
    for (const auto& item : *array) {
        const auto name = core::json::string_or(item.find("name"));
        if (name.empty()) {
            continue;
        }
        const auto* schema = item.find("inputSchema");
        tools.push_back(Tool{
            .server_id = std::string(server_id),
            .name = name,
            .description = core::json::string_or(item.find("description")),
            .input_schema_json = schema == nullptr
                ? "{}"
                : core::json::stringify(*schema),
        });
    }
    return tools;
}

[[nodiscard]] ConnectionResult connect_http(const ServerProfile& server) {
    ConnectionResult result;
    HttpSession session;
    const auto initialize = json_rpc_request(
        1,
        "initialize",
        core::json::Value::Object{
            {"protocolVersion", std::string(preferred_protocol)},
            {"capabilities", core::json::Value::Object{}},
            {"clientInfo", core::json::Value::Object{
                {"name", "ThreadSpire"},
                {"version", "0.3.0"},
            }},
        });
    auto response = mcp_post(server, initialize, session);
    if (!response.succeeded()) {
        result.message = response.error.empty()
            ? "MCP initialize returned HTTP " + std::to_string(response.status_code)
            : response.error;
        return result;
    }
    std::string parse_error;
    const auto document = response_document(response, parse_error);
    if (!document.has_value()) {
        result.message = "Invalid MCP initialize response: " + parse_error;
        return result;
    }
    const auto* error = document->find("error");
    if (error != nullptr) {
        result.message = core::json::string_or(error->find("message"), "MCP initialization failed");
        return result;
    }
    const auto* initialize_result = document->find("result");
    session.protocol_version = core::json::string_or(
        initialize_result == nullptr ? nullptr : initialize_result->find("protocolVersion"),
        preferred_protocol);
    session.session_id = response.header("Mcp-Session-Id").value_or("");

    const auto initialized = core::json::stringify(core::json::Value(core::json::Value::Object{
        {"jsonrpc", "2.0"},
        {"method", "notifications/initialized"},
        {"params", core::json::Value::Object{}},
    }));
    response = mcp_post(server, initialized, session);
    if (!response.succeeded() && response.status_code != 202) {
        result.message = "MCP initialized notification failed";
        return result;
    }

    response = mcp_post(
        server,
        json_rpc_request(2, "tools/list", core::json::Value::Object{}),
        session);
    if (!response.succeeded()) {
        result.message = response.error.empty()
            ? "MCP tools/list returned HTTP " + std::to_string(response.status_code)
            : response.error;
        return result;
    }
    const auto tools_document = response_document(response, parse_error);
    if (!tools_document.has_value()) {
        result.message = "Invalid MCP tools/list response: " + parse_error;
        return result;
    }
    result.protocol_version = session.protocol_version;
    result.session_id = session.session_id;
    result.tools = parse_tools(server.id, *tools_document);
    result.succeeded = true;
    result.message = std::to_string(result.tools.size()) + " MCP tool(s) available";
    return result;
}

[[nodiscard]] ToolResult call_http(
    const ServerProfile& server,
    const std::string_view tool_name,
    const std::string_view arguments_json) {
    const auto connection = connect_http(server);
    if (!connection.succeeded) {
        return {.succeeded = false, .content = {}, .error = connection.message, .is_error = true};
    }
    std::string parse_error;
    auto arguments = core::json::try_parse(arguments_json.empty() ? "{}" : arguments_json, parse_error);
    if (!arguments.has_value() || !arguments->is_object()) {
        return {
            .succeeded = false,
            .content = {},
            .error = "Tool arguments must be a JSON object: " + parse_error,
            .is_error = true,
        };
    }
    const HttpSession session{
        .protocol_version = connection.protocol_version,
        .session_id = connection.session_id,
    };
    const auto response = mcp_post(
        server,
        json_rpc_request(
            3,
            "tools/call",
            core::json::Value::Object{
                {"name", std::string(tool_name)},
                {"arguments", std::move(*arguments)},
            }),
        session);
    if (!response.succeeded()) {
        return {
            .succeeded = false,
            .content = {},
            .error = response.error.empty()
                ? "MCP tools/call returned HTTP " + std::to_string(response.status_code)
                : response.error,
            .is_error = true,
        };
    }
    const auto document = response_document(response, parse_error);
    if (!document.has_value()) {
        return {.succeeded = false, .content = {}, .error = parse_error, .is_error = true};
    }
    const auto* result = document->find("result");
    if (result == nullptr) {
        const auto* error = document->find("error");
        return {
            .succeeded = false,
            .content = {},
            .error = core::json::string_or(
                error == nullptr ? nullptr : error->find("message"),
                "MCP tool returned no result"),
            .is_error = true,
        };
    }
    std::string content;
    const auto* content_value = result->find("content");
    if (const auto* array = content_value == nullptr ? nullptr : content_value->as_array();
        array != nullptr) {
        for (const auto& item : *array) {
            const auto text = core::json::string_or(item.find("text"));
            if (!text.empty()) {
                if (!content.empty()) {
                    content += '\n';
                }
                content += text;
            }
        }
    }
    if (content.empty()) {
        content = core::json::stringify(*result);
    }
    const auto is_error = core::json::bool_or(result->find("isError"), false);
    return {
        .succeeded = !is_error,
        .content = std::move(content),
        .error = is_error ? "MCP tool reported an error" : std::string{},
        .is_error = is_error,
    };
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::optional<core::json::Value> run_stdio_bridge(
    const ServerProfile& server,
    const std::string_view action,
    const std::string_view tool_name,
    const std::string_view arguments_json,
    const std::filesystem::path& python,
    const std::filesystem::path& worker,
    const std::filesystem::path& state_root,
    std::string& error) {
    if (!std::filesystem::is_regular_file(python) || !std::filesystem::is_regular_file(worker)) {
        error = "Python backend is required for stdio MCP servers";
        return std::nullopt;
    }
    const auto run_id = "mcp-" + std::to_string(memory::now_unix_ms());
    const auto directory = state_root / "mcp" / run_id;
    std::filesystem::create_directories(directory);
    const auto request_file = directory / "request.json";
    const auto output_file = directory / "response.json";
    const auto log_file = directory / "bridge.log";
    std::string parse_error;
    auto arguments = core::json::try_parse(arguments_json.empty() ? "{}" : arguments_json, parse_error);
    if (!arguments.has_value()) {
        error = "Invalid tool arguments JSON: " + parse_error;
        return std::nullopt;
    }
    core::json::Value::Array command_arguments;
    for (const auto& item : server.arguments) {
        command_arguments.emplace_back(item);
    }
    const auto request = core::json::Value(core::json::Value::Object{
        {"action", std::string(action)},
        {"server_id", server.id},
        {"executable", server.executable.string()},
        {"arguments", std::move(command_arguments)},
        {"tool_name", std::string(tool_name)},
        {"tool_arguments", std::move(*arguments)},
        {"output", output_file.string()},
    });
    {
        std::ofstream stream(request_file, std::ios::binary | std::ios::trunc);
        stream << core::json::stringify(request);
    }
    const auto process = platform::run_process(platform::ProcessSpec{
        .executable = python,
        .arguments = {worker.string(), "mcp-stdio", "--request", request_file.string()},
        .working_directory = state_root,
        .log_file = log_file,
        .append_log = false,
    });
    if (!process.launched || process.exit_code != 0) {
        error = "stdio MCP bridge failed; inspect " + log_file.string();
        return std::nullopt;
    }
    const auto response_text = read_file(output_file);
    return core::json::try_parse(response_text, error);
}

} // namespace

std::string_view to_string(const Transport transport) noexcept {
    return transport == Transport::stdio ? "stdio" : "streamable-http";
}

Transport transport_from_string(const std::string_view value) noexcept {
    return value == "stdio" ? Transport::stdio : Transport::streamable_http;
}

Registry Registry::load(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) {
        auto result = defaults();
        result.save(path);
        return result;
    }
    const auto ini = core::IniFile::load(path);
    const auto ids = split_csv(ini.get_string("mcp", "ids", ""));
    Registry result;
    for (const auto& id : ids) {
        const auto section = "mcp." + id;
        ServerProfile server;
        server.id = id;
        server.display_name = ini.get_string(section, "display_name", id);
        server.transport = transport_from_string(ini.get_string(section, "transport", "streamable-http"));
        server.endpoint = ini.get_string(section, "endpoint", "");
        server.executable = ini.get_string(section, "executable", "");
        server.arguments = split_arguments(ini.get_string(section, "arguments", ""));
        server.enabled = ini.get_bool(section, "enabled", true);
        server.allow_for_training = ini.get_bool(section, "allow_for_training", false);
        result.servers_.push_back(std::move(server));
    }
    return result;
}

Registry Registry::defaults() {
    Registry result;
    result.servers_ = {
        ServerProfile{
            .id = "local-http-tools",
            .display_name = "Local Streamable HTTP MCP",
            .transport = Transport::streamable_http,
            .endpoint = "http://127.0.0.1:3000/mcp",
            .executable = {},
            .arguments = {},
            .enabled = false,
            .allow_for_training = false,
        },
    };
    return result;
}

void Registry::save(const std::filesystem::path& path) const {
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("unable to write MCP registry");
    }
    stream << "[mcp]\nids=";
    for (std::size_t index = 0; index < servers_.size(); ++index) {
        if (index != 0U) {
            stream << ',';
        }
        stream << servers_[index].id;
    }
    stream << "\n\n";
    for (const auto& server : servers_) {
        stream << "[mcp." << server.id << "]\n"
               << "display_name=" << server.display_name << '\n'
               << "transport=" << to_string(server.transport) << '\n'
               << "endpoint=" << server.endpoint << '\n'
               << "executable=" << server.executable.string() << '\n'
               << "arguments=" << join_arguments(server.arguments) << '\n'
               << "enabled=" << (server.enabled ? "true" : "false") << '\n'
               << "allow_for_training=" << (server.allow_for_training ? "true" : "false")
               << "\n\n";
    }
    stream.close();
    std::error_code filesystem_error;
    std::filesystem::remove(path, filesystem_error);
    filesystem_error.clear();
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        throw std::runtime_error("unable to publish MCP registry: " + filesystem_error.message());
    }
}

const std::vector<ServerProfile>& Registry::servers() const noexcept { return servers_; }
std::vector<ServerProfile>& Registry::servers() noexcept { return servers_; }

ConnectionResult Client::connect_and_list(
    const ServerProfile& server,
    const std::filesystem::path& python,
    const std::filesystem::path& worker,
    const std::filesystem::path& state_root) const {
    if (server.transport == Transport::streamable_http) {
        return connect_http(server);
    }
    std::string error;
    const auto document = run_stdio_bridge(
        server,
        "list-tools",
        {},
        "{}",
        python,
        worker,
        state_root,
        error);
    if (!document.has_value()) {
        return {
            .succeeded = false,
            .message = std::move(error),
            .protocol_version = {},
            .session_id = {},
            .tools = {},
        };
    }
    ConnectionResult result;
    result.succeeded = core::json::bool_or(document->find("succeeded"), false);
    result.message = core::json::string_or(document->find("message"));
    result.protocol_version = core::json::string_or(document->find("protocol_version"));
    const auto* tools = document->find("tools");
    if (const auto* array = tools == nullptr ? nullptr : tools->as_array(); array != nullptr) {
        for (const auto& item : *array) {
            result.tools.push_back(Tool{
                .server_id = server.id,
                .name = core::json::string_or(item.find("name")),
                .description = core::json::string_or(item.find("description")),
                .input_schema_json = item.find("input_schema") == nullptr
                    ? "{}"
                    : core::json::stringify(*item.find("input_schema")),
            });
        }
    }
    return result;
}

ToolResult Client::call_tool(
    const ServerProfile& server,
    const std::string_view tool_name,
    const std::string_view arguments_json,
    const std::filesystem::path& python,
    const std::filesystem::path& worker,
    const std::filesystem::path& state_root) const {
    if (server.transport == Transport::streamable_http) {
        return call_http(server, tool_name, arguments_json);
    }
    std::string error;
    const auto document = run_stdio_bridge(
        server,
        "call-tool",
        tool_name,
        arguments_json,
        python,
        worker,
        state_root,
        error);
    if (!document.has_value()) {
        return {.succeeded = false, .content = {}, .error = std::move(error), .is_error = true};
    }
    return ToolResult{
        .succeeded = core::json::bool_or(document->find("succeeded"), false),
        .content = core::json::string_or(document->find("content")),
        .error = core::json::string_or(document->find("error")),
        .is_error = core::json::bool_or(document->find("is_error"), false),
    };
}

} // namespace threadspire::ai::mcp
