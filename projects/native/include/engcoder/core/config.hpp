#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engcoder::core {

enum class ModelApi { auto_select, responses, chat_completions };

struct AddonConfig {
    std::string id;
    std::string display_name;
    std::string model;
    std::string purpose;
    std::string recommended_size;
    std::string system_prompt;
    std::vector<std::string> triggers;
    double temperature{0.10};
    bool enabled{true};
    bool auto_route{true};
};

struct Config {
    std::filesystem::path workspace = std::filesystem::current_path();
    std::filesystem::path data_root;
    std::filesystem::path web_root;
    std::filesystem::path iteration_candidate;
    std::string iteration_build_dir;
    std::string iteration_escalation_model;
    std::string iteration_boundary_source;
    std::string iteration_boundary_rule;
    std::string lm_studio_url = "http://127.0.0.1:1234";
    std::string model = "auto"; // KeyMaker Foundation model / fallback provider model.
    std::string controller_model; // Optional external provider model for Architect decisions.
    bool controller_enabled = true;
    double controller_min_confidence = 0.55;
    std::string router_model; // compatibility input for older addon-router configuration.
    std::vector<AddonConfig> addons;
    bool addon_auto_route = true;
    std::string lm_studio_token;
    std::vector<std::string> lm_studio_integrations;
    std::vector<std::string> lm_studio_auto_allow_integrations;
    ModelApi model_api = ModelApi::auto_select;
    bool model_auto_load = true;
    // Long-running coding inference has a separate budget from health/discovery.
    // Engine callers can override this per request without changing short probes.
    std::uint64_t inference_timeout_seconds = 3ULL * 60ULL * 60ULL;

    // Native downloadable model runtimes. Qwen Flash is a first-class EngCoder
    // runtime type, not an external OpenAI provider. The transport is reused
    // only after EngCoder has launched and owns the runtime process.
    bool qwen_flash_enabled = true;
    std::filesystem::path qwen_flash_server;
    std::filesystem::path model_cache_root;
    std::uint16_t qwen_flash_port = 8188;
    std::uint32_t qwen_flash_context_tokens = 81'920;
    std::uint32_t qwen_flash_parallel_slots = 1;
    std::uint32_t qwen_flash_context_reserve_tokens = 4'096;
    bool qwen_flash_auto_compact = true;
    double qwen_flash_compact_trigger_ratio = 0.97;
    double qwen_flash_compact_target_ratio = 0.72;
    std::uint32_t qwen_flash_threads = 12;
    std::uint32_t qwen_flash_batch_threads = 12;
    std::uint32_t qwen_flash_batch_size = 1024;
    std::uint32_t qwen_flash_ubatch_size = 256;
    std::string qwen_flash_runtime_policy = "recommended";
    std::uint32_t qwen_flash_gpu_offload_percent = 50;
    std::uint32_t qwen_flash_gpu_layer_count_hint = 48;
    std::string qwen_flash_gpu_layers = "auto";
    bool qwen_flash_flash_attention = true;
    std::string qwen_flash_kv_type_k = "q4_0";
    std::string qwen_flash_kv_type_v = "q4_0";
    std::uint32_t qwen_flash_kv_gpu_layers = 0;
    bool qwen_flash_kv_offload = false;
    bool qwen_flash_kv_cpu_pinned = true;
    bool qwen_flash_phase_aware_workspace = true;
    std::string qwen_flash_load_mode = "mmap";
    std::string qwen_flash_lazy_mode = "on";
    std::string qwen_flash_tensor_override = "per_layer_token_embd.weight=CPU";
    std::uint32_t qwen_flash_prompt_cache_mib = 2048;
    bool qwen_flash_context_shift = false;
    bool qwen_flash_reasoning_preserve = false;
    std::string qwen_flash_expert_cache_mode = "auto";
    std::uint32_t qwen_flash_expert_cache_mib = 6144;
    std::uint32_t qwen_flash_pinned_host_mib = 12288;
    bool qwen_flash_early_router = false;
    std::uint16_t server_port = 14321;
    std::string desktop_session_token; // Ephemeral token used only by the embedded Desktop/Web surface.
    std::filesystem::path model_root;
    std::filesystem::path learning_root;
    std::filesystem::path training_backend;
    std::string python_executable = "python";
    double vram_budget_gib = 13.5;
    std::uint32_t max_resident_addons = 3;
    bool training_enabled = true;
    bool training_preempt_for_inference = true;
    std::size_t max_active_context_chars = 240'000;
    std::size_t max_tool_output_chars = 96'000;
    std::size_t checkpoint_file_limit = 16 * 1024 * 1024;
    std::uint32_t max_model_turns = 48;
    std::uint32_t repeated_action_limit = 3;

    [[nodiscard]] static Config from_environment();
};

} // namespace engcoder::core
