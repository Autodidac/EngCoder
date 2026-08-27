#pragma once

#include "threadspire/ai/models/model_registry.hpp"
#include "threadspire/ai/util/async_operation.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace threadspire::ai::long_horizon {

struct Request final {
    std::filesystem::path repository;
    std::filesystem::path state_root;
    std::string backend_base_url;
    models::ModelProfile repair_model;
    std::string task;
    std::string build_command;
    std::string test_command;
    std::filesystem::path adapter_path;
    std::filesystem::path module_path;
    std::size_t maximum_iterations{8};
    bool require_clean_worktree{true};
    bool rollback_on_failure{true};
};

struct Event final {
    std::size_t iteration{0};
    std::string phase;
    int exit_code{-1};
    std::chrono::milliseconds duration{0};
    std::filesystem::path log_path;
};

struct Result final {
    util::OperationResult operation;
    bool completed{false};
    bool compiler_passed{false};
    bool tests_passed{false};
    std::size_t iterations{0};
    std::filesystem::path run_directory;
    std::filesystem::path trajectory_path;
    std::filesystem::path final_diff_path;
    std::filesystem::path source_repository;
    std::filesystem::path staged_repository;
    std::filesystem::path candidate_manifest_path;
    std::string source_head;
    std::string candidate_commit;
    std::string final_diff;
    std::vector<Event> events;
};

struct PromotionResult final {
    util::OperationResult operation;
    std::string branch_name;
    std::string candidate_commit;
};

[[nodiscard]] Result run(const Request& request, std::stop_token stop_token);
[[nodiscard]] PromotionResult promote_verified_candidate(
    const Result& candidate,
    std::stop_token stop_token = {});

} // namespace threadspire::ai::long_horizon
