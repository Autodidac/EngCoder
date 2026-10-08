#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"
#include "engcoder/core/event.hpp"

namespace engcoder::runtime {

using json = nlohmann::json;
using EventSink = std::function<void(const core::Event&)>;

enum class RunTerminal {
    completed,
    failed,
    cancelled,
    awaiting_approval,
};

struct RunOutcome {
    std::string project_id;
    std::string session_id;
    std::string task_id;
    RunTerminal terminal{RunTerminal::completed};
    std::string final_text;
    json terminal_data = json::object();
};

class RuntimeHost {
public:
    explicit RuntimeHost(core::Config config = core::Config::from_environment());
    ~RuntimeHost();

    RuntimeHost(const RuntimeHost&) = delete;
    RuntimeHost& operator=(const RuntimeHost&) = delete;

    [[nodiscard]] const core::Config& config() const noexcept;
    [[nodiscard]] std::expected<json, core::Error> model_discovery();
    [[nodiscard]] json architecture() const;
    [[nodiscard]] json native_model_library() const;
    [[nodiscard]] std::expected<json, core::Error> register_native_model(const json& spec);
    [[nodiscard]] std::expected<json, core::Error> provision_native_runtime();
    [[nodiscard]] std::expected<json, core::Error> install_native_model(std::string_view id_or_uri);
    [[nodiscard]] std::expected<json, core::Error> load_native_model(
        std::string_view id_or_uri,
        std::string_view local_path_override = {});
    void unload_native_model();
    [[nodiscard]] std::expected<json, core::Error> remove_native_model(std::string_view id_or_uri, bool delete_cache);
    [[nodiscard]] std::expected<json, core::Error> clear_native_model_cache(std::string_view id_or_uri);
    [[nodiscard]] std::expected<json, core::Error> delete_native_model_weights(std::string_view id_or_uri);
    [[nodiscard]] std::expected<json, core::Error> set_foundation_model(std::string_view provider_model);
    [[nodiscard]] std::string foundation_model() const;

    [[nodiscard]] std::expected<RunOutcome, core::Error> run_task(
        std::string_view project_id,
        std::string_view session_id,
        std::string_view message,
        std::stop_token stop = {},
        EventSink on_event = {});

    [[nodiscard]] std::expected<std::uint16_t, core::Error> start_server();
    void stop_server();
    [[nodiscard]] bool server_running() const noexcept;
    [[nodiscard]] bool desktop_client_seen() const noexcept;
    [[nodiscard]] bool desktop_client_open() const noexcept;
    [[nodiscard]] std::string server_url() const;
    [[nodiscard]] std::string web_url() const;

    [[nodiscard]] bool serve_forever();
    [[nodiscard]] std::string decision_mode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string make_session_id(std::string_view prefix = "session");

} // namespace engcoder::runtime
