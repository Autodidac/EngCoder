#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace engcoder::model {

// Slider 100% is deliberately limited to 60% of the former layer-offload
// range. This is a limit on model layers, not on total GPU memory usage.
inline constexpr std::uint32_t gpu_offload_legacy_ceiling_percent = 60U;
inline constexpr std::uint32_t gpu_offload_default_percent = 50U;
inline constexpr std::uint32_t runtime_profile_revision = 9U;

enum class RuntimeKind {
    internal_keymaker,
    qwen_flash,
    external_openai,
};

enum class ModelSourceKind {
    internal_generation,
    hugging_face,
    local_gguf,
    external_provider,
};

// Persisted first-class model/runtime profile. Fields intentionally describe
// model identity, runtime policy, and context policy separately so EngCoder can
// expose one coherent model library without coupling agent orchestration to a
// particular inference backend.
struct RuntimeModelProfile {
    std::string id;
    std::string display_name;
    RuntimeKind runtime{RuntimeKind::qwen_flash};
    ModelSourceKind source{ModelSourceKind::hugging_face};
    std::string repo;
    std::string quant;
    std::filesystem::path local_path;
    std::uint64_t estimated_bytes{};
    std::uint32_t profile_revision{runtime_profile_revision};

    // Runtime policy. Recommended keeps the proven QwenFlashRuntime
    // scheduling/memory defaults while honoring EngCoder's GPU layer-offload
    // target. Automatic derives safe memory/batching values from the current
    // machine while using the same GPU target. Manual exposes all controls.
    std::string runtime_policy{"recommended"};
    std::uint32_t gpu_offload_percent{gpu_offload_default_percent};
    std::uint32_t gpu_layer_count_hint{48};

    // Context / concurrency. Qwen3.8-Flash-Next is 262,144 native; EngCoder's
    // recommended 5080/64 GiB profile starts at 81,920 with one slot so the
    // entire window belongs to the active agent rather than being divided.
    std::uint32_t context_tokens{81'920};
    std::uint32_t parallel_slots{1};
    std::uint32_t context_reserve_tokens{4'096};
    bool auto_compact{true};
    double compact_trigger_ratio{0.97};
    double compact_target_ratio{0.72};

    // Runtime scheduling / memory policy.
    std::uint32_t threads{12};
    std::uint32_t batch_threads{12};
    std::uint32_t batch_size{1024};
    std::uint32_t ubatch_size{256};
    std::string gpu_layers{"auto"};
    bool flash_attention{true};
    std::string kv_type_k{"q4_0"};
    std::string kv_type_v{"q4_0"};
    std::uint32_t kv_gpu_layers{0};
    bool kv_offload{false}; // Manual/automatic override. Recommended preserves the proven QFR backend default.
    bool kv_cpu_pinned{true};
    bool phase_aware_workspace{true};
    std::string load_mode{"mmap"};
    std::string lazy_mode{"on"};
    std::string tensor_override{"per_layer_token_embd.weight=CPU"};
    std::uint32_t prompt_cache_mib{2048};
    bool context_shift{false};
    bool reasoning_preserve{false};

    // Sparse/MoE hot/cold residency policy. `auto` enables the cache only
    // when the selected runtime advertises compatibility with the model
    // layout; `off` is the safe baseline; `on` is an explicit manual force.
    std::string expert_cache_mode{"auto"};
    std::uint32_t expert_cache_mib{6144};
    std::uint32_t pinned_host_mib{12288};
    bool early_router{false};

    // Optional speculative path. Stable default remains off until an MTP/draft
    // sidecar is configured for the selected model.
    bool speculative_enabled{false};
    std::string speculative_type{"draft-mtp"};
    std::string speculative_repo;
    std::filesystem::path speculative_model;
    std::uint32_t speculative_tokens{2};

    bool enabled{true};
};

[[nodiscard]] std::string_view to_string(RuntimeKind value) noexcept;
[[nodiscard]] std::string_view to_string(ModelSourceKind value) noexcept;
[[nodiscard]] std::string runtime_model_uri(const RuntimeModelProfile& profile);
[[nodiscard]] std::uint32_t effective_context_tokens(const RuntimeModelProfile& profile) noexcept;
// Resolve llama.cpp -ngl. The 0..100 user scale covers only 0..60% of the
// former layer range; even literal overrides/all are capped at that ceiling.
[[nodiscard]] std::string effective_gpu_layers(const RuntimeModelProfile& profile);
[[nodiscard]] nlohmann::json to_json(const RuntimeModelProfile& profile);
[[nodiscard]] RuntimeModelProfile runtime_model_from_json(const nlohmann::json& value);

} // namespace engcoder::model
