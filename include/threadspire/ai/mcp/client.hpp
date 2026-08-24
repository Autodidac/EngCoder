#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::mcp {

struct Tool final {
    std::string name;
    std::string description;
    std::string input_schema_json{"{}"};
};

struct ConnectionResult final {
    bool succeeded{false};
    std::string server_name;
    std::string server_version;
    std::string protocol_version;
    std::vector<Tool> tools;
    std::string error;
};

struct ToolResult final {
    bool succeeded{false};
    std::string content;
    bool is_error{false};
    std::string error;
};

class Client final {
public:
    Client() = default;

    [[nodiscard]] ConnectionResult connect_http(std::string endpoint);
    void disconnect() noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::string endpoint() const;
    [[nodiscard]] std::vector<Tool> tools() const;
    [[nodiscard]] std::string openai_tools_json() const;

    [[nodiscard]] ToolResult call_tool(
        std::string_view name,
        std::string_view arguments_json);

private:
    struct RpcResult final {
        bool succeeded{false};
        std::string result_json;
        std::string error;
        std::string session_id;
    };

    [[nodiscard]] RpcResult rpc(
        std::string_view endpoint,
        std::string_view session_id,
        std::uint64_t request_id,
        std::string_view method,
        std::string_view params_json,
        bool notification = false) const;

    mutable std::mutex mutex_;
    std::string endpoint_;
    std::string session_id_;
    std::string protocol_version_{"2025-06-18"};
    std::string server_name_;
    std::string server_version_;
    std::vector<Tool> tools_;
    std::uint64_t next_request_id_{1};
    bool connected_{false};
};

[[nodiscard]] std::string extract_json_rpc_payload(std::string_view body);
[[nodiscard]] std::string tools_to_openai_json(const std::vector<Tool>& tools);

} // namespace threadspire::ai::mcp
