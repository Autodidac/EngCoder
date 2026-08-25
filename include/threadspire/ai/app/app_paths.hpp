#pragma once

#include <filesystem>

namespace threadspire::ai::app {

struct AppPaths final {
    std::filesystem::path executable;
    std::filesystem::path root;
    std::filesystem::path packs;
    std::filesystem::path outputs;
    std::filesystem::path cache;
    std::filesystem::path candidates;
    std::filesystem::path checkpoints;
    std::filesystem::path runtime;
    std::filesystem::path python;
    std::filesystem::path worker;
    std::filesystem::path internal_model_module;
    std::filesystem::path curriculum_guard_module;
    std::filesystem::path model_registry;
    std::filesystem::path local_server_registry;
    std::filesystem::path mcp_registry;
    std::filesystem::path ready_file;
    std::filesystem::path jobs;
    std::filesystem::path logs;
    std::filesystem::path experts;
    std::filesystem::path memory;
    std::filesystem::path data;
    std::filesystem::path bootstrap_dataset;
    std::filesystem::path bootstrap_anchors;
    std::filesystem::path custom_training_template;
    std::filesystem::path trajectories;
};

[[nodiscard]] AppPaths make_app_paths();
void initialize_base_resources(const AppPaths& paths);

} // namespace threadspire::ai::app
