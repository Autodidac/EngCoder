#include "engcoder/core/config.hpp"

#include <algorithm>
#include <limits>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace engcoder::core {
namespace {

[[nodiscard]] std::string environment_value(std::string_view name, std::string fallback = {}) {
    const std::string key{name};

#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key.c_str()) != 0 || value == nullptr) {
        return fallback;
    }

    std::string result;
    if (length > 1 && *value != '\0') {
        result.assign(value);
    }
    std::free(value);
    return result.empty() ? fallback : result;
#else
    if (const char* value = std::getenv(key.c_str()); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
#endif
}


[[nodiscard]] std::string product_environment_value(std::string_view suffix, std::string fallback = {}) {
    return environment_value(std::string{"ENGCODER_"} + std::string{suffix}, std::move(fallback));
}

[[nodiscard]] std::filesystem::path default_data_root() {
#ifdef _WIN32
    const auto local = environment_value("LOCALAPPDATA");
    if (!local.empty()) return std::filesystem::path{local} / "EngCoder";
#else
    const auto xdg = environment_value("XDG_DATA_HOME");
    if (!xdg.empty()) return std::filesystem::path{xdg} / "EngCoder";
    const auto home = environment_value("HOME");
    if (!home.empty()) return std::filesystem::path{home} / ".local" / "share" / "EngCoder";
#endif
    return std::filesystem::current_path() / ".engcoder";
}

[[nodiscard]] std::filesystem::path default_model_cache_root(const std::filesystem::path& data_root) {
    if (const auto value = environment_value("LLAMA_CACHE"); !value.empty()) return value;
    if (const auto value = environment_value("HF_HUB_CACHE"); !value.empty()) return value;
    if (const auto value = environment_value("HUGGINGFACE_HUB_CACHE"); !value.empty()) return value;
    if (const auto value = environment_value("HF_HOME"); !value.empty()) return std::filesystem::path{value} / "hub";
    if (const auto value = environment_value("XDG_CACHE_HOME"); !value.empty()) {
        return std::filesystem::path{value} / "huggingface" / "hub";
    }

#ifdef _WIN32
    const auto home = environment_value("USERPROFILE", environment_value("HOME"));
#else
    const auto home = environment_value("HOME");
#endif
    if (!home.empty()) {
        const auto shared = std::filesystem::path{home} / ".cache" / "huggingface" / "hub";
        std::error_code ec;
        if (std::filesystem::is_directory(shared, ec) && !ec) return shared;
    }
    return data_root / "model-cache";
}

[[nodiscard]] bool truthy(std::string value) {
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES";
}

[[nodiscard]] std::vector<std::string> split_csv(std::string value) {
    std::vector<std::string> result;
    std::size_t start = 0;

    while (start < value.size()) {
        const auto end = value.find(',', start);
        auto item = value.substr(
            start,
            end == std::string::npos ? value.size() - start : end - start);

        if (!item.empty()) {
            result.push_back(std::move(item));
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return result;
}

[[nodiscard]] AddonConfig make_addon(
    std::string id,
    std::string display_name,
    std::string purpose,
    std::string recommended_size,
    std::string system_prompt,
    std::vector<std::string> triggers) {
    return AddonConfig{
        .id = std::move(id),
        .display_name = std::move(display_name),
        .model = {},
        .purpose = std::move(purpose),
        .recommended_size = std::move(recommended_size),
        .system_prompt = std::move(system_prompt),
        .triggers = std::move(triggers),
        .temperature = 0.10,
        .enabled = true,
        .auto_route = true,
    };
}

[[nodiscard]] std::vector<AddonConfig> default_addons() {
    std::vector<AddonConfig> addons;
    addons.push_back(make_addon(
        "cpp",
        "C++ Addon",
        "C++23 implementation, compiler diagnostics, systems code, API usage, and focused code review",
        "75M-250M",
        "You are the C++23 specialist inside EngCoder. Analyze only the focused problem you are given. "
        "Prefer modern, safe, efficient, cross-platform C++23. Identify exact compiler/runtime issues and return concise advisory guidance or a concrete implementation approach. "
        "Do not claim to have edited files or run tools.",
        {"c++", "cpp", "cmake", "compiler", "compile", "template", "constexpr", "std::", "msvc", "clang", "gcc", ".hpp", ".cpp"}));
    addons.push_back(make_addon(
        "docs",
        "Documentation Addon",
        "technical documentation, API contracts, specifications, and repository documentation extraction",
        "50M-150M",
        "You are the documentation specialist inside EngCoder. Extract constraints, APIs, contracts, and contradictions from the supplied material. "
        "Return only facts relevant to the core agent's task and clearly mark uncertainty. Do not invent undocumented behavior.",
        {"documentation", "docs", "readme", "api", "manual", "spec", "protocol", "reference", "agents.md"}));
    addons.push_back(make_addon(
        "planner",
        "Planning Addon",
        "task decomposition, architecture sequencing, implementation planning, and risk identification",
        "50M-150M",
        "You are the planning specialist inside EngCoder. Decompose the requested work into the fewest coherent implementation steps, identify dependencies and verification points, "
        "and return a compact plan to the core agent. Do not perform the work or invent repository facts.",
        {"plan", "architecture", "design", "refactor", "roadmap", "milestone", "sequence", "strategy", "restructure"}));
    addons.push_back(make_addon(
        "tools",
        "Tool-Use Addon",
        "tool selection, structured tool arguments, command construction, and constrained action planning",
        "50M-150M",
        "You are the structured tool-use specialist inside EngCoder. Given a task and available context, recommend the smallest safe tool action and exact structured arguments. "
        "Never claim the action ran. Avoid shell pipelines and hidden side effects.",
        {"tool", "json", "arguments", "command", "execute", "build", "test", "search", "patch", "workspace"}));
    addons.push_back(make_addon(
        "reranker",
        "Context Reranker",
        "ranking retrieved context and selecting the smallest evidence set worth keeping active",
        "20M-100M",
        "You are the context reranking specialist inside EngCoder. Rank the supplied evidence by relevance to the task and return only the smallest high-value subset with short reasons. "
        "Do not solve the task itself.",
        {"rerank", "retrieval", "context", "relevant", "evidence", "search results", "which files"}));
    return addons;
}

void set_addon_model(std::vector<AddonConfig>& addons, std::string_view id, std::string model) {
    const auto it = std::ranges::find_if(addons, [&](const auto& addon) {
        return addon.id == id;
    });
    if (it != addons.end()) {
        it->model = std::move(model);
        return;
    }

    addons.push_back(AddonConfig{
        .id = std::string{id},
        .display_name = std::string{id},
        .model = std::move(model),
        .purpose = "custom advisory specialist",
        .recommended_size = "task dependent",
        .system_prompt = "You are a narrow advisory specialist inside EngCoder. Return concise evidence to the core agent and do not claim tool execution.",
        .triggers = {std::string{id}},
    });
}

void apply_custom_addons(std::vector<AddonConfig>& addons, const std::string& value) {
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find(';', start);
        const auto entry = value.substr(start, end == std::string::npos ? value.size() - start : end - start);
        const auto equals = entry.find('=');
        if (equals != std::string::npos && equals != 0 && equals + 1 < entry.size()) {
            set_addon_model(addons, entry.substr(0, equals), entry.substr(equals + 1));
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

} // namespace

Config Config::from_environment() {
    Config config;

    config.workspace = product_environment_value(
        "WORKSPACE",
        std::filesystem::current_path().string());
    config.data_root = product_environment_value(
        "DATA_ROOT",
        default_data_root().string());
    config.web_root = product_environment_value(
        "WEB_ROOT",
        (config.workspace / "projects" / "web").string());
    config.iteration_candidate = product_environment_value("ITERATION_CANDIDATE");
    config.iteration_build_dir = product_environment_value("ITERATION_BUILD_DIR");
    config.iteration_escalation_model = product_environment_value("ITERATION_ESCALATION_MODEL");
    config.iteration_boundary_source = product_environment_value("ITERATION_BOUNDARY_SOURCE");
    config.iteration_boundary_rule = product_environment_value("ITERATION_BOUNDARY_RULE");
    config.lm_studio_url = product_environment_value(
        "MODEL_SERVER_URL",
        product_environment_value("LMSTUDIO_URL", config.lm_studio_url));
    config.model = product_environment_value("MODEL", config.model);
    config.controller_model = product_environment_value(
        "ARCHITECT_MODEL",
        product_environment_value("CONTROLLER_MODEL", product_environment_value("ROUTER_MODEL")));
    config.controller_enabled = truthy(product_environment_value("CONTROLLER_ENABLED", "true"));
    config.router_model = product_environment_value("ROUTER_MODEL", config.controller_model);
    config.addon_auto_route = truthy(product_environment_value(
        "ADDON_AUTO_ROUTE", product_environment_value("EXPERT_AUTO_ROUTE", "true")));
    const auto controller_confidence = product_environment_value("CONTROLLER_MIN_CONFIDENCE");
    if (!controller_confidence.empty()) {
        try { config.controller_min_confidence = std::clamp(std::stod(controller_confidence), 0.0, 1.0); } catch (...) {}
    }
    config.addons = default_addons();

    set_addon_model(config.addons, "cpp", product_environment_value("ADDON_CPP_MODEL", product_environment_value("EXPERT_CPP_MODEL")));
    set_addon_model(config.addons, "docs", product_environment_value("ADDON_DOCS_MODEL", product_environment_value("EXPERT_DOCS_MODEL")));
    set_addon_model(config.addons, "planner", product_environment_value("ADDON_PLANNER_MODEL", product_environment_value("EXPERT_PLANNER_MODEL")));
    set_addon_model(config.addons, "tools", product_environment_value("ADDON_TOOLS_MODEL", product_environment_value("EXPERT_TOOLS_MODEL")));
    set_addon_model(config.addons, "reranker", product_environment_value("ADDON_RERANKER_MODEL", product_environment_value("EXPERT_RERANKER_MODEL")));
    apply_custom_addons(config.addons, product_environment_value("ADDONS", product_environment_value("EXPERTS")));

    config.lm_studio_token = product_environment_value(
        "MODEL_SERVER_TOKEN",
        product_environment_value("LMSTUDIO_TOKEN", environment_value("LM_API_TOKEN")));
    config.lm_studio_integrations = split_csv(product_environment_value("LMSTUDIO_MCP_INTEGRATIONS"));
    config.lm_studio_auto_allow_integrations = split_csv(product_environment_value("LMSTUDIO_MCP_AUTO_ALLOW"));

    const auto api = product_environment_value("MODEL_API", "auto");
    if (api == "responses") {
        config.model_api = ModelApi::responses;
    } else if (api == "chat" || api == "chat_completions") {
        config.model_api = ModelApi::chat_completions;
    } else {
        config.model_api = ModelApi::auto_select;
    }

    config.model_auto_load = truthy(product_environment_value("MODEL_AUTO_LOAD", "true"));
    if (const auto value = product_environment_value("INFERENCE_TIMEOUT_SECONDS"); !value.empty()) {
        try {
            const auto parsed = std::stoull(value);
            if (parsed >= 30ULL) config.inference_timeout_seconds = std::min<std::uint64_t>(parsed, 365ULL * 24ULL * 60ULL * 60ULL);
        } catch (...) {}
    }

    config.qwen_flash_enabled = truthy(product_environment_value("QWEN_FLASH_ENABLED", "true"));
    config.qwen_flash_server = product_environment_value("QWEN_FLASH_SERVER");
    config.model_cache_root = product_environment_value(
        "MODEL_CACHE_ROOT",
        default_model_cache_root(config.data_root).string());

    auto parse_u16 = [&](const char* name, std::uint16_t& target) {
        const auto value = product_environment_value(name);
        if (value.empty()) return;
        try {
            const auto parsed = std::stoul(value);
            if (parsed > 0 && parsed <= 65535) target = static_cast<std::uint16_t>(parsed);
        } catch (...) {}
    };
    auto parse_u32 = [&](const char* name, std::uint32_t& target) {
        const auto value = product_environment_value(name);
        if (value.empty()) return;
        try {
            const auto parsed = std::stoull(value);
            if (parsed > 0 && parsed <= std::numeric_limits<std::uint32_t>::max()) target = static_cast<std::uint32_t>(parsed);
        } catch (...) {}
    };
    auto parse_u32_zero_ok = [&](const char* name, std::uint32_t& target) {
        const auto value = product_environment_value(name);
        if (value.empty()) return;
        try {
            const auto parsed = std::stoull(value);
            if (parsed <= std::numeric_limits<std::uint32_t>::max()) target = static_cast<std::uint32_t>(parsed);
        } catch (...) {}
    };

    parse_u16("QWEN_FLASH_PORT", config.qwen_flash_port);
    parse_u32("QWEN_FLASH_CONTEXT_TOKENS", config.qwen_flash_context_tokens);
    parse_u32("QWEN_FLASH_PARALLEL_SLOTS", config.qwen_flash_parallel_slots);
    parse_u32("QWEN_FLASH_CONTEXT_RESERVE_TOKENS", config.qwen_flash_context_reserve_tokens);
    config.qwen_flash_auto_compact = truthy(product_environment_value("QWEN_FLASH_AUTO_COMPACT", config.qwen_flash_auto_compact ? "true" : "false"));
    if (const auto value = product_environment_value("QWEN_FLASH_COMPACT_TRIGGER_RATIO"); !value.empty()) {
        try { config.qwen_flash_compact_trigger_ratio = std::clamp(std::stod(value), 0.50, 0.98); } catch (...) {}
    }
    if (const auto value = product_environment_value("QWEN_FLASH_COMPACT_TARGET_RATIO"); !value.empty()) {
        try { config.qwen_flash_compact_target_ratio = std::clamp(std::stod(value), 0.25, config.qwen_flash_compact_trigger_ratio - 0.02); } catch (...) {}
    }
    parse_u32("QWEN_FLASH_THREADS", config.qwen_flash_threads);
    parse_u32("QWEN_FLASH_BATCH_THREADS", config.qwen_flash_batch_threads);
    parse_u32("QWEN_FLASH_BATCH_SIZE", config.qwen_flash_batch_size);
    parse_u32("QWEN_FLASH_UBATCH_SIZE", config.qwen_flash_ubatch_size);
    config.qwen_flash_runtime_policy = product_environment_value("QWEN_FLASH_RUNTIME_POLICY", config.qwen_flash_runtime_policy);
    parse_u32_zero_ok("QWEN_FLASH_GPU_OFFLOAD_PERCENT", config.qwen_flash_gpu_offload_percent);
    parse_u32("QWEN_FLASH_GPU_LAYER_COUNT_HINT", config.qwen_flash_gpu_layer_count_hint);
    config.qwen_flash_gpu_layers = product_environment_value("QWEN_FLASH_GPU_LAYERS", config.qwen_flash_gpu_layers);
    config.qwen_flash_flash_attention = truthy(product_environment_value("QWEN_FLASH_FLASH_ATTENTION", config.qwen_flash_flash_attention ? "true" : "false"));
    config.qwen_flash_kv_type_k = product_environment_value("QWEN_FLASH_KV_TYPE_K", config.qwen_flash_kv_type_k);
    config.qwen_flash_kv_type_v = product_environment_value("QWEN_FLASH_KV_TYPE_V", config.qwen_flash_kv_type_v);
    parse_u32_zero_ok("QWEN_FLASH_KV_GPU_LAYERS", config.qwen_flash_kv_gpu_layers);
    config.qwen_flash_kv_offload = truthy(product_environment_value("QWEN_FLASH_KV_OFFLOAD", config.qwen_flash_kv_offload ? "true" : "false"));
    config.qwen_flash_kv_cpu_pinned = truthy(product_environment_value("QWEN_FLASH_KV_CPU_PINNED", config.qwen_flash_kv_cpu_pinned ? "true" : "false"));
    config.qwen_flash_phase_aware_workspace = truthy(product_environment_value("QWEN_FLASH_PHASE_AWARE_WORKSPACE", config.qwen_flash_phase_aware_workspace ? "true" : "false"));
    config.qwen_flash_load_mode = product_environment_value("QWEN_FLASH_LOAD_MODE", config.qwen_flash_load_mode);
    config.qwen_flash_lazy_mode = product_environment_value("QWEN_FLASH_LAZY_MODE", config.qwen_flash_lazy_mode);
    config.qwen_flash_tensor_override = product_environment_value("QWEN_FLASH_TENSOR_OVERRIDE", config.qwen_flash_tensor_override);
    parse_u32_zero_ok("QWEN_FLASH_PROMPT_CACHE_MIB", config.qwen_flash_prompt_cache_mib);
    config.qwen_flash_context_shift = truthy(product_environment_value("QWEN_FLASH_CONTEXT_SHIFT", config.qwen_flash_context_shift ? "true" : "false"));
    config.qwen_flash_reasoning_preserve = truthy(product_environment_value("QWEN_FLASH_REASONING_PRESERVE", config.qwen_flash_reasoning_preserve ? "true" : "false"));
    config.qwen_flash_expert_cache_mode = product_environment_value("QWEN_FLASH_EXPERT_CACHE_MODE", config.qwen_flash_expert_cache_mode);
    parse_u32_zero_ok("QWEN_FLASH_EXPERT_CACHE_MIB", config.qwen_flash_expert_cache_mib);
    parse_u32_zero_ok("QWEN_FLASH_PINNED_HOST_MIB", config.qwen_flash_pinned_host_mib);
    config.qwen_flash_early_router = truthy(product_environment_value("QWEN_FLASH_EARLY_ROUTER", config.qwen_flash_early_router ? "true" : "false"));

    // Keep environment overrides inside the native Qwen Flash contract. Invalid
    // values should degrade to safe bounds here instead of surfacing later as
    // opaque llama.cpp startup failures.
    if (config.qwen_flash_runtime_policy != "recommended" &&
        config.qwen_flash_runtime_policy != "automatic" &&
        config.qwen_flash_runtime_policy != "manual") {
        config.qwen_flash_runtime_policy = "recommended";
    }
    if (config.qwen_flash_expert_cache_mode != "auto" && config.qwen_flash_expert_cache_mode != "off" &&
        config.qwen_flash_expert_cache_mode != "on") {
        config.qwen_flash_expert_cache_mode = "auto";
    }
    config.qwen_flash_gpu_offload_percent = std::min(config.qwen_flash_gpu_offload_percent, 100U);
    config.qwen_flash_gpu_layer_count_hint = std::clamp(config.qwen_flash_gpu_layer_count_hint, 1U, 512U);
    config.qwen_flash_context_tokens = std::clamp(config.qwen_flash_context_tokens, 4'096U, 262'144U);
    config.qwen_flash_parallel_slots = std::clamp(config.qwen_flash_parallel_slots, 1U, 4U);
    const auto qwen_effective_context = config.qwen_flash_context_tokens / config.qwen_flash_parallel_slots;
    config.qwen_flash_context_reserve_tokens = std::clamp(
        config.qwen_flash_context_reserve_tokens,
        1'024U,
        std::max(1'024U, qwen_effective_context / 2U));
    config.qwen_flash_compact_trigger_ratio = std::clamp(config.qwen_flash_compact_trigger_ratio, 0.50, 0.98);
    config.qwen_flash_compact_target_ratio = std::clamp(
        config.qwen_flash_compact_target_ratio,
        0.25,
        config.qwen_flash_compact_trigger_ratio - 0.02);
    config.qwen_flash_threads = std::max(1U, config.qwen_flash_threads);
    config.qwen_flash_batch_threads = std::max(1U, config.qwen_flash_batch_threads);
    config.qwen_flash_batch_size = std::max(32U, config.qwen_flash_batch_size);
    config.qwen_flash_ubatch_size = std::clamp(config.qwen_flash_ubatch_size, 32U, config.qwen_flash_batch_size);

    config.model_root = product_environment_value(
        "MODEL_ROOT",
        (config.data_root / "models").string());
    config.learning_root = product_environment_value(
        "LEARNING_ROOT",
        (config.data_root / "learning").string());
    config.training_backend = product_environment_value(
        "TRAINING_BACKEND",
        (config.workspace / "projects" / "native" / "resources" / "training" / "model_worker.py").string());
    config.python_executable = product_environment_value("PYTHON", config.python_executable);
    config.training_enabled = truthy(product_environment_value("TRAINING_ENABLED", "true"));
    config.training_preempt_for_inference = truthy(product_environment_value("TRAINING_PREEMPT_FOR_INFERENCE", "true"));
    const auto vram_budget = product_environment_value("VRAM_BUDGET_GIB");
    if (!vram_budget.empty()) {
        try { config.vram_budget_gib = std::max(0.5, std::stod(vram_budget)); } catch (...) {}
    }
    const auto resident_addons = product_environment_value("MAX_RESIDENT_ADDONS");
    if (!resident_addons.empty()) {
        try { config.max_resident_addons = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::stoul(resident_addons))); } catch (...) {}
    }

    const auto port = product_environment_value("SERVER_PORT");
    if (!port.empty()) {
        try {
            std::size_t consumed = 0;
            const auto parsed = std::stoul(port, &consumed);
            if (consumed == port.size() && parsed >= 1UL && parsed <= 65'535UL) {
                config.server_port = static_cast<std::uint16_t>(parsed);
            }
        } catch (...) {
            // Keep the safe product default when the environment value is malformed.
        }
    }


    return config;
}

} // namespace engcoder::core
