#pragma once

#include "threadspire/ai/learning/continual_learning_service.hpp"
#include "threadspire/ai/models/model_registry.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::learning {

enum class CurriculumRights { unknown, public_domain, permissive_license, user_owned };
enum class FoundationStage {
    idle, ingesting, mapping, generating, verifying, compiling,
    queued_training, paused, completed, failed,
};
enum class CurriculumTeacherRole { qwen_architect, nemotron_generator, independent_verifier };

struct CurriculumSourceInput final {
    std::filesystem::path path;
    std::string title;
    std::string origin;
    std::string permission;
    CurriculumRights rights{CurriculumRights::unknown};
    bool redistribution_permitted{false};
};

struct CurriculumSourceRecord final {
    std::string id;
    std::string title;
    std::string origin;
    std::string permission;
    CurriculumRights rights{CurriculumRights::unknown};
    std::string content_hash;
    std::int64_t ingested_unix_ms{0};
    std::size_t bytes{0};
    std::size_t sections{0};
    std::size_t chunks{0};
    std::size_t held_out_chunks{0};
    bool redistribution_permitted{false};
};

struct CurriculumChunk final {
    std::string id;
    std::string source_id;
    std::string section;
    std::string source_span;
    std::string content_hash;
    std::string text;
    bool held_out{false};
};

struct CurriculumPreparationResult final {
    bool succeeded{false};
    std::vector<CurriculumSourceRecord> sources;
    std::vector<CurriculumChunk> chunks;
    std::size_t duplicates_removed{0};
    std::string error;
};

struct CurriculumTeacherResponse final {
    bool succeeded{false};
    std::string content;
    std::string error;
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
};

using CurriculumCompletion = std::function<CurriculumTeacherResponse(
    CurriculumTeacherRole role,
    const models::ModelProfile& model,
    std::string_view system_prompt,
    std::string_view prompt,
    std::size_t maximum_output_tokens,
    double temperature,
    std::stop_token stop_token)>;

struct FoundationBuildRequest final {
    std::filesystem::path curriculum_root;
    std::filesystem::path base_anchor_dataset;
    std::vector<CurriculumSourceInput> sources;
    models::ModelProfile qwen;
    models::ModelProfile nemotron;
    std::string internal_base_url;
    CurriculumCompletion completion;
    std::size_t nemotron_parallelism{4};
    std::size_t qwen_parallelism{1};
    std::size_t variants_per_lesson{4};
    std::size_t maximum_source_chunks{0};
    double minimum_verifier_score{0.88};
    bool rebuild{false};
};

struct FoundationBuildProgress final {
    FoundationStage stage{FoundationStage::idle};
    std::string operation;
    std::string teacher;
    std::string verifier;
    std::string source;
    std::string checkpoint;
    std::string active_candidate;
    std::string promotion_result;
    std::size_t source_index{0};
    std::size_t source_count{0};
    std::size_t chunk_index{0};
    std::size_t chunk_count{0};
    std::size_t generated{0};
    std::size_t accepted{0};
    std::size_t revised{0};
    std::size_t rejected{0};
    std::size_t retry_pending{0};
    std::size_t held_out{0};
    std::size_t queued_weight_jobs{0};
    std::size_t prompt_tokens{0};
    std::size_t completion_tokens{0};
    std::uintmax_t model_bytes{0};
    std::string message;
};

struct FoundationBuildResult final {
    bool succeeded{false};
    bool cancelled{false};
    bool training_queued{false};
    FoundationBuildProgress progress;
    std::string error;
};

using FoundationProgressCallback = std::function<void(const FoundationBuildProgress&)>;

[[nodiscard]] std::string_view to_string(CurriculumRights rights) noexcept;
[[nodiscard]] std::string_view to_string(FoundationStage stage) noexcept;
[[nodiscard]] bool curriculum_rights_admitted(CurriculumRights rights) noexcept;
[[nodiscard]] CurriculumPreparationResult prepare_english_foundation_sources(
    const std::vector<CurriculumSourceInput>& sources,
    const std::filesystem::path& curriculum_root,
    bool rebuild = false);
[[nodiscard]] FoundationBuildProgress read_english_foundation_progress(
    const std::filesystem::path& curriculum_root);
[[nodiscard]] FoundationBuildResult run_english_foundation_build(
    const FoundationBuildRequest& request,
    ContinualLearningService& learning_service,
    const FoundationProgressCallback& on_progress = {},
    std::stop_token stop_token = {});

} // namespace threadspire::ai::learning
