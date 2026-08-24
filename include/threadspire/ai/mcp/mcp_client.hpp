#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::mcp {

enum class Transport {
    streamable_http,
    stdio,
};

struct ServerProfile final {
    std::string id;
    std::string display_name;
    Transport transport{Transport::streamable_http};
    std::string endpoint;
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    bool enabled{true};
    bool allow_for_training{false};
};

struct Tool final {
    std::string server_id;
    std::string name;
    std::string description;
    std::string input_schema_json;
};

struct ConnectionResult final {
    bool succeeded{false};
    std::string message;
    std::string protocol_version;
    std::string session_id;
    std::vector<Tool> tools;
};

struct ToolResult final {
    bool succeeded{false};
    std::string content;
    std::string error;
    bool is_error{false};
};

class Registry final {
public:
    [[nodiscard]] static Registry load(const std::filesystem::path& path);
    [[nodiscard]] static Registry defaults();
    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<ServerProfile>& servers() const noexcept;
    [[nodiscard]] std::vector<ServerProfile>& servers() noexcept;

private:
    std::vector<ServerProfile> servers_;
};

class Client final {
public:
    [[nodiscard]] ConnectionResult connect_and_list(
        const ServerProfile& server,
        const std::filesystem::path& python,
        const std::filesystem::path& worker,
        const std::filesystem::path& state_root) const;

    [[nodiscard]] ToolResult call_tool(
        const ServerProfile& server,
        std::string_view tool_name,
        std::string_view arguments_json,
        const std::filesystem::path& python,
        const std::filesystem::path& worker,
        const std::filesystem::path& state_root) const;
};

[[nodiscard]] std::string_view to_string(Transport transport) noexcept;
[[nodiscard]] Transport transport_from_string(std::string_view value) noexcept;

} // namespace threadspire::ai::mcp
