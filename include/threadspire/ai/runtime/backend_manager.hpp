#pragma once

#include "threadspire/ai/app/app_paths.hpp"
#include "threadspire/ai/util/async_operation.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

namespace threadspire::ai::runtime {

[[nodiscard]] bool backend_files_present(const app::AppPaths& paths) noexcept;

[[nodiscard]] util::OperationResult install_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string pytorch_index_url,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult verify_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult initialize_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string profile,
    bool force,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult train_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string scope,
    std::size_t maximum_steps,
    bool foreground_bootstrap,
    std::size_t bootstrap_target_steps,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult inspect_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::stop_token stop_token);

} // namespace threadspire::ai::runtime
