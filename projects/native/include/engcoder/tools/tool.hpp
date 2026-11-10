#pragma once
#include <expected>
#include <functional>
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
    bool create_files_only{false};
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
