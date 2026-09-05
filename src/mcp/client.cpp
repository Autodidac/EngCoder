module;
#include "module_prelude.hpp"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <utility>

module threadspire.mcp;
import threadspire.core;
import threadspire.platform;

namespace threadspire::ai::mcp {
namespace {

[[nodiscard]] std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1U));
}

[[nodiscard]] std::string normalize_endpoint(std::string endpoint) {
    endpoint = trim(endpoint);
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    if (!endpoint.starts_with("http://") && !endpoint.starts_with("https://")) {
        endpoint = "http://" + endpoint;
    }
    return endpoint;
}

[[nodiscard]] std::string rpc_error(const core::json::Value& document) {
    const auto* error = document.find("error");
    if (error == nullptr) {
        return {};
    }
    if (error->is_string()) {
        return std::string(error->as_string());
    }
    const auto message = core::json::string_or(error->find("message"));
    const auto code = core::json::number_or(error->find("code"));
    if (!message.empty()) {
        return message + (code == 0.0 ? std::string{} : " (code " + std::to_string(code) + ')');
    }
    return "MCP server returned a JSON-RPC error";
}

[[nodiscard]] std::vector<Tool> parse_tools(const core::json::Value& result) {
    std::vector<Tool> tools;
    const auto* values = result.find("tools");
    const auto* array = values == nullptr ? nullptr : values->as_array();
    if (array == nullptr) {
        return tools;
    }
    tools.reserve(array->size());
    for (const auto& item : *array) {
        const auto name = core::json::string_or(item.find("name"));
        if (name.empty()) {
            continue;
        }
        const auto* schema = item.find("inputSchema");
        if (schema == nullptr) {
            schema = item.find("input_schema");
        }
        tools.push_back(Tool{
            .name = name,
            .description = core::json::string_or(item.find("description")),
            .input_schema_json = schema == nullptr ? "{}" : core::json::stringify(*schema),
        });
    }
    return tools;
}

[[nodiscard]] std::string flatten_tool_content(const core::json::Value& result) {
    const auto* content = result.find("content");
    const auto* array = content == nullptr ? nullptr : content->as_array();
    if (array == nullptr) {
        return core::json::stringify(result);
    }
    std::ostringstream stream;
    bool first = true;
    for (const auto& item : *array) {
        auto text = core::json::string_or(item.find("text"));
        if (text.empty()) {
            text = core::json::string_or(item.find("data"));
        }
        if (text.empty()) {
            text = core::json::stringify(item);
        }
        if (!first) {
            stream << '\n';
        }
        first = false;
        stream << text;
    }
    return stream.str();
}

} // namespace

std::string extract_json_rpc_payload(const std::string_view body) {
    const auto stripped = trim(body);
    if (stripped.empty()) {
        return {};
    }
    if (stripped.front() == '{' || stripped.front() == '[') {
        return stripped;
    }

    std::string last_payload;
    std::size_t begin = 0;
    while (begin < body.size()) {
        const auto end = body.find('\n', begin);
        auto line = body.substr(begin, end == std::string_view::npos ? body.size() - begin : end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (line.starts_with("data:")) {
            line.remove_prefix(5U);
            while (!line.empty() && line.front() == ' ') {
                line.remove_prefix(1U);
            }
            if (!line.empty() && line != "[DONE]") {
                last_payload.assign(line);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1U;
    }
    return last_payload;
}

std::string tools_to_openai_json(const std::vector<Tool>& tools) {
    core::json::Value::Array result;
    result.reserve(tools.size());
    for (const auto& tool : tools) {
        std::string error;
        auto schema = core::json::try_parse(tool.input_schema_json, error);
        if (!schema.has_value() || !schema->is_object()) {
            schema = core::json::Value(core::json::Value::Object{});
        }
        result.emplace_back(core::json::Value::Object{
            {"type", "function"},
            {"function", core::json::Value::Object{
                {"name", tool.name},
                {"description", tool.description},
                {"parameters", std::move(*schema)},
            }},
        });
    }
    return core::json::stringify(core::json::Value(std::move(result)));
}

Client::RpcResult Client::rpc(
    const std::string_view endpoint,
    const std::string_view session_id,
    const std::uint64_t request_id,
    const std::string_view method,
    const std::string_view params_json,
    const bool notification) const {
    RpcResult result;
    std::string params_error;
    auto params = core::json::try_parse(params_json.empty() ? "{}" : params_json, params_error);
    if (!params.has_value()) {
        result.error = "invalid MCP params JSON: " + params_error;
        return result;
    }

    core::json::Value::Object root{
        {"jsonrpc", "2.0"},
        {"method", std::string(method)},
        {"params", std::move(*params)},
    };
    if (!notification) {
        root.emplace("id", static_cast<double>(request_id));
    }

    std::vector<backend::HttpHeader> headers{
        {.name = "Content-Type", .value = "application/json"},
        {.name = "Accept", .value = "application/json, text/event-stream"},
        {.name = "MCP-Protocol-Version", .value = protocol_version_},
    };
    if (!session_id.empty()) {
        headers.push_back({.name = "Mcp-Session-Id", .value = std::string(session_id)});
    }

    const auto response = backend::http_request(backend::HttpRequest{
        .method = "POST",
        .url = std::string(endpoint),
        .body = core::json::stringify(core::json::Value(std::move(root))),
        .headers = std::move(headers),
        .timeout = std::chrono::seconds(30),
    });
    result.session_id = response.header("Mcp-Session-Id").value_or(std::string(session_id));
    if (!response.succeeded()) {
        result.error = response.error.empty()
            ? "MCP server returned HTTP " + std::to_string(response.status_code) +
                  (response.body.empty() ? std::string{} : ": " + response.body)
            : response.error;
        return result;
    }
    if (notification) {
        result.succeeded = true;
        return result;
    }

    const auto payload = extract_json_rpc_payload(response.body);
    if (payload.empty()) {
        result.error = "MCP server returned no JSON-RPC payload";
        return result;
    }
    std::string parse_error;
    const auto document = core::json::try_parse(payload, parse_error);
    if (!document.has_value()) {
        result.error = "invalid MCP JSON-RPC response: " + parse_error;
        return result;
    }
    if (const auto error = rpc_error(*document); !error.empty()) {
        result.error = error;
        return result;
    }
    const auto* rpc_result = document->find("result");
    if (rpc_result == nullptr) {
        result.error = "MCP response is missing result";
        return result;
    }
    result.result_json = core::json::stringify(*rpc_result);
    result.succeeded = true;
    return result;
}

ConnectionResult Client::connect_http(std::string endpoint) {
    ConnectionResult output;
    endpoint = normalize_endpoint(std::move(endpoint));
    if (endpoint.empty()) {
        output.error = "MCP endpoint is empty";
        return output;
    }

    const auto initialize_params = core::json::stringify(core::json::Value(
        core::json::Value::Object{
            {"protocolVersion", protocol_version_},
            {"capabilities", core::json::Value::Object{}},
            {"clientInfo", core::json::Value::Object{
                {"name", "EngCoder"},
                {"version", "0.9.1"},
            }},
        }));
    auto initialized = rpc(endpoint, {}, 1U, "initialize", initialize_params);
    if (!initialized.succeeded) {
        output.error = initialized.error;
        return output;
    }

    std::string parse_error;
    const auto init_result = core::json::try_parse(initialized.result_json, parse_error);
    if (!init_result.has_value()) {
        output.error = "invalid initialize result: " + parse_error;
        return output;
    }
    output.protocol_version = core::json::string_or(
        init_result->find("protocolVersion"), protocol_version_);
    const auto* server_info = init_result->find("serverInfo");
    if (server_info != nullptr) {
        output.server_name = core::json::string_or(server_info->find("name"), "MCP server");
        output.server_version = core::json::string_or(server_info->find("version"));
    }

    const auto notification = rpc(
        endpoint,
        initialized.session_id,
        0U,
        "notifications/initialized",
        "{}",
        true);
    if (!notification.succeeded) {
        output.error = "MCP initialization notification failed: " + notification.error;
        return output;
    }

    const auto listed = rpc(endpoint, initialized.session_id, 2U, "tools/list", "{}");
    if (!listed.succeeded) {
        output.error = "MCP tools/list failed: " + listed.error;
        return output;
    }
    const auto tools_result = core::json::try_parse(listed.result_json, parse_error);
    if (!tools_result.has_value()) {
        output.error = "invalid MCP tools/list result: " + parse_error;
        return output;
    }
    output.tools = parse_tools(*tools_result);
    output.succeeded = true;

    {
        std::lock_guard lock(mutex_);
        endpoint_ = std::move(endpoint);
        session_id_ = listed.session_id.empty() ? initialized.session_id : listed.session_id;
        protocol_version_ = output.protocol_version.empty() ? "2025-06-18" : output.protocol_version;
        server_name_ = output.server_name;
        server_version_ = output.server_version;
        tools_ = output.tools;
        next_request_id_ = 3U;
        connected_ = true;
    }
    return output;
}

void Client::disconnect() noexcept {
    std::lock_guard lock(mutex_);
    endpoint_.clear();
    session_id_.clear();
    server_name_.clear();
    server_version_.clear();
    tools_.clear();
    next_request_id_ = 1U;
    connected_ = false;
}

bool Client::connected() const noexcept {
    std::lock_guard lock(mutex_);
    return connected_;
}

std::string Client::endpoint() const {
    std::lock_guard lock(mutex_);
    return endpoint_;
}

std::vector<Tool> Client::tools() const {
    std::lock_guard lock(mutex_);
    return tools_;
}

std::string Client::openai_tools_json() const {
    return tools_to_openai_json(tools());
}

ToolResult Client::call_tool(
    const std::string_view name,
    const std::string_view arguments_json) {
    std::string endpoint;
    std::string session;
    std::uint64_t id = 0;
    {
        std::lock_guard lock(mutex_);
        if (!connected_) {
            return ToolResult{.succeeded = false, .content = {}, .is_error = true, .error = "MCP client is not connected"};
        }
        endpoint = endpoint_;
        session = session_id_;
        id = next_request_id_++;
    }

    std::string parse_error;
    auto arguments = core::json::try_parse(arguments_json.empty() ? "{}" : arguments_json, parse_error);
    if (!arguments.has_value() || !arguments->is_object()) {
        return ToolResult{.succeeded = false, .content = {}, .is_error = true, .error = "invalid tool arguments JSON: " + parse_error};
    }
    const auto params = core::json::stringify(core::json::Value(core::json::Value::Object{
        {"name", std::string(name)},
        {"arguments", std::move(*arguments)},
    }));
    const auto response = rpc(endpoint, session, id, "tools/call", params);
    if (!response.succeeded) {
        return ToolResult{.succeeded = false, .content = {}, .is_error = true, .error = response.error};
    }
    const auto document = core::json::try_parse(response.result_json, parse_error);
    if (!document.has_value()) {
        return ToolResult{.succeeded = false, .content = {}, .is_error = true, .error = "invalid MCP tool result: " + parse_error};
    }
    const auto is_error = core::json::bool_or(document->find("isError"), false);
    return ToolResult{
        .succeeded = !is_error,
        .content = flatten_tool_content(*document),
        .is_error = is_error,
        .error = is_error ? "MCP tool reported an error" : std::string{},
    };
}

} // namespace threadspire::ai::mcp
