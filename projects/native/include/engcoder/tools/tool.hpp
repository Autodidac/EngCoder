#pragma once
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <nlohmann/json.hpp>
#include "engcoder/core/error.hpp"

namespace engcoder::tools {
using json = nlohmann::json;

struct Context {
    std::string project_id;
    std::string session_id;
    std::string task_id;
    std::string workspace_root;
    std::stop_token stop;
    json addon_models = json::object(); // per-session model assignments, never tool authority
    bool candidate_mode{false};
    std::optional<std::size_t> patch_start_line{}; // verified by the candidate host
};

struct Definition {
    std::string name;
    std::string description;
    json parameters;
};

using Handler = std::function<std::expected<json, core::Error>(const json&, const Context&)>;

struct Tool {
    Definition definition;
    Handler handler;
};

} // namespace engcoder::tools
