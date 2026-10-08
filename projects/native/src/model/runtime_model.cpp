#include "engcoder/model/runtime_model.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace engcoder::model {

std::string_view to_string(RuntimeKind value) noexcept {
    switch (value) {
        case RuntimeKind::internal_keymaker: return "internal-keymaker";
        case RuntimeKind::qwen_flash: return "qwen-flash";
        case RuntimeKind::external_openai: return "external-openai";
    }
    return "qwen-flash";
}

std::string_view to_string(ModelSourceKind value) noexcept {
    switch (value) {
        case ModelSourceKind::internal_generation: return "internal-generation";
        case ModelSourceKind::hugging_face: return "hugging-face";
        case ModelSourceKind::local_gguf: return "local-gguf";
        case ModelSourceKind::external_provider: return "external-provider";
    }
    return "hugging-face";
}

std::string runtime_model_uri(const RuntimeModelProfile& profile) {
    switch (profile.runtime) {
        case RuntimeKind::qwen_flash: return "qwenflash://" + profile.id;
        case RuntimeKind::internal_keymaker: return "engcoder://" + profile.id;
        case RuntimeKind::external_openai: return profile.id;
    }
    return profile.id;
}

std::uint32_t effective_context_tokens(const RuntimeModelProfile& profile) noexcept {
    return profile.context_tokens / std::max<std::uint32_t>(1, profile.parallel_slots);
}

std::string effective_gpu_layers(const RuntimeModelProfile& profile) {
    const auto layer_hint = std::max<std::uint32_t>(1U, profile.gpu_layer_count_hint);
    const auto ceiling = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(layer_hint) * gpu_offload_legacy_ceiling_percent + 50ULL) / 100ULL);
    if (!profile.gpu_layers.empty() && profile.gpu_layers != "auto") {
        if (profile.gpu_layers == "all") return std::to_string(ceiling);
        std::uint32_t requested = 0;
        const auto [end, ec] = std::from_chars(
            profile.gpu_layers.data(), profile.gpu_layers.data() + profile.gpu_layers.size(), requested);
        if (ec == std::errc{} && end == profile.gpu_layers.data() + profile.gpu_layers.size()) {
            return std::to_string(std::min(requested, ceiling));
        }
        // Keep invalid manual input observable to the backend rather than
        // silently interpreting it as a GPU percentage.
        return profile.gpu_layers;
    }
    if (profile.gpu_offload_percent == 0U) return "0";
    const auto layers = std::max<std::uint32_t>(
        1U,
        static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(layer_hint) * gpu_offload_legacy_ceiling_percent *
                std::min(profile.gpu_offload_percent, 100U) + 5000ULL) / 10000ULL));
    return std::to_string(std::min(layers, ceiling));
}

nlohmann::json to_json(const RuntimeModelProfile& profile) {
    return {
        {"format", "engcoder.runtime-model.v4"},
        {"id", profile.id},
        {"display_name", profile.display_name},
        {"runtime", std::string{to_string(profile.runtime)}},
        {"source", std::string{to_string(profile.source)}},
        {"repo", profile.repo.empty() ? nlohmann::json(nullptr) : nlohmann::json(profile.repo)},
        {"quant", profile.quant.empty() ? nlohmann::json(nullptr) : nlohmann::json(profile.quant)},
        {"local_path", profile.local_path.empty() ? nlohmann::json(nullptr) : nlohmann::json(profile.local_path.string())},
        {"estimated_bytes", profile.estimated_bytes},
        {"profile_revision", profile.profile_revision},
        {"runtime_policy", profile.runtime_policy},
        {"gpu_offload_percent", profile.gpu_offload_percent},
        {"gpu_layer_count_hint", profile.gpu_layer_count_hint},
        {"context_tokens", profile.context_tokens},
        {"parallel_slots", profile.parallel_slots},
        {"effective_context_tokens", effective_context_tokens(profile)},
        {"context_reserve_tokens", profile.context_reserve_tokens},
        {"auto_compact", profile.auto_compact},
        {"compact_trigger_ratio", profile.compact_trigger_ratio},
        {"compact_target_ratio", profile.compact_target_ratio},
        {"threads", profile.threads},
        {"batch_threads", profile.batch_threads},
        {"batch_size", profile.batch_size},
        {"ubatch_size", profile.ubatch_size},
        {"gpu_layers", profile.gpu_layers},
        {"flash_attention", profile.flash_attention},
        {"kv_type_k", profile.kv_type_k},
        {"kv_type_v", profile.kv_type_v},
        {"kv_gpu_layers", profile.kv_gpu_layers},
        {"kv_offload", profile.kv_offload},
        {"kv_cpu_pinned", profile.kv_cpu_pinned},
        {"phase_aware_workspace", profile.phase_aware_workspace},
        {"load_mode", profile.load_mode},
        {"lazy_mode", profile.lazy_mode},
        {"tensor_override", profile.tensor_override},
        {"prompt_cache_mib", profile.prompt_cache_mib},
        {"context_shift", profile.context_shift},
        {"reasoning_preserve", profile.reasoning_preserve},
        {"expert_cache_mode", profile.expert_cache_mode},
        {"expert_cache_mib", profile.expert_cache_mib},
        {"pinned_host_mib", profile.pinned_host_mib},
        {"early_router", profile.early_router},
        {"speculative_enabled", profile.speculative_enabled},
        {"speculative_type", profile.speculative_type},
        {"speculative_repo", profile.speculative_repo.empty() ? nlohmann::json(nullptr) : nlohmann::json(profile.speculative_repo)},
        {"speculative_model", profile.speculative_model.empty() ? nlohmann::json(nullptr) : nlohmann::json(profile.speculative_model.string())},
        {"speculative_tokens", profile.speculative_tokens},
        {"enabled", profile.enabled},
        {"provider_model", runtime_model_uri(profile)},
        {"kind", "foundation"},
    };
}

RuntimeModelProfile runtime_model_from_json(const nlohmann::json& value) {
    const auto string_value = [&](std::string_view key, std::string fallback = {}) {
        const auto it = value.find(std::string{key});
        return it != value.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
    };

    RuntimeModelProfile profile;
    profile.id = string_value("id");
    profile.display_name = string_value("display_name", profile.id);

    const auto runtime = string_value("runtime", "qwen-flash");
    if (runtime == "internal-keymaker") profile.runtime = RuntimeKind::internal_keymaker;
    else if (runtime == "external-openai") profile.runtime = RuntimeKind::external_openai;
    else profile.runtime = RuntimeKind::qwen_flash;

    const auto source = string_value("source", "hugging-face");
    if (source == "internal-generation") profile.source = ModelSourceKind::internal_generation;
    else if (source == "local-gguf") profile.source = ModelSourceKind::local_gguf;
    else if (source == "external-provider") profile.source = ModelSourceKind::external_provider;
    else profile.source = ModelSourceKind::hugging_face;

    profile.repo = string_value("repo");
    profile.quant = string_value("quant");
    profile.local_path = string_value("local_path");
    profile.estimated_bytes = value.value("estimated_bytes", std::uint64_t{});
    profile.profile_revision = value.value("profile_revision", 1U);
    profile.runtime_policy = string_value("runtime_policy", "recommended");
    profile.gpu_offload_percent = value.value("gpu_offload_percent", gpu_offload_default_percent);
    profile.gpu_layer_count_hint = value.value("gpu_layer_count_hint", 48U);
    profile.context_tokens = value.value("context_tokens", 81'920U);
    profile.parallel_slots = std::max(1U, value.value("parallel_slots", 1U));
    profile.context_reserve_tokens = value.value("context_reserve_tokens", 4'096U);
    profile.auto_compact = value.value("auto_compact", true);
    profile.compact_trigger_ratio = std::clamp(value.value("compact_trigger_ratio", 0.97), 0.50, 0.98);
    profile.compact_target_ratio = std::clamp(value.value("compact_target_ratio", 0.72), 0.25, profile.compact_trigger_ratio - 0.02);
    profile.threads = value.value("threads", 12U);
    profile.batch_threads = value.value("batch_threads", 12U);
    profile.batch_size = value.value("batch_size", 1024U);
    profile.ubatch_size = value.value("ubatch_size", 256U);
    profile.gpu_layers = string_value("gpu_layers", "auto");
    profile.flash_attention = value.value("flash_attention", true);
    profile.kv_type_k = string_value("kv_type_k", "q4_0");
    profile.kv_type_v = string_value("kv_type_v", "q4_0");
    profile.kv_gpu_layers = value.value("kv_gpu_layers", 0U);
    profile.kv_offload = value.value("kv_offload", false);
    profile.kv_cpu_pinned = value.value("kv_cpu_pinned", true);
    profile.phase_aware_workspace = value.value("phase_aware_workspace", true);
    profile.load_mode = string_value("load_mode", "mmap");
    profile.lazy_mode = string_value("lazy_mode", "on");
    profile.tensor_override = string_value("tensor_override", "per_layer_token_embd.weight=CPU");
    profile.prompt_cache_mib = value.value("prompt_cache_mib", 2048U);
    profile.context_shift = value.value("context_shift", false);
    profile.reasoning_preserve = value.value("reasoning_preserve", false);
    profile.expert_cache_mode = string_value("expert_cache_mode", "auto");
    profile.expert_cache_mib = value.value("expert_cache_mib", 6144U);
    profile.pinned_host_mib = value.value("pinned_host_mib", 12288U);
    profile.early_router = value.value("early_router", false);
    profile.speculative_enabled = value.value("speculative_enabled", false);
    profile.speculative_type = string_value("speculative_type", "draft-mtp");
    profile.speculative_repo = string_value("speculative_repo");
    profile.speculative_model = string_value("speculative_model");
    profile.speculative_tokens = value.value("speculative_tokens", 2U);
    profile.enabled = value.value("enabled", true);

    if (profile.id.empty()) throw std::invalid_argument("runtime model id is empty");
    if (profile.runtime_policy != "recommended" && profile.runtime_policy != "automatic" && profile.runtime_policy != "manual") {
        throw std::invalid_argument("runtime_policy must be recommended, automatic, or manual");
    }
    if (profile.expert_cache_mode != "auto" && profile.expert_cache_mode != "off" && profile.expert_cache_mode != "on") {
        throw std::invalid_argument("expert_cache_mode must be auto, off, or on");
    }
    if (profile.gpu_offload_percent > 100U) {
        throw std::invalid_argument("gpu_offload_percent must be between 0 and 100");
    }
    if (profile.gpu_layer_count_hint == 0U || profile.gpu_layer_count_hint > 512U) {
        throw std::invalid_argument("gpu_layer_count_hint must be between 1 and 512");
    }
    if (!profile.gpu_layers.empty() && profile.gpu_layers != "auto" && profile.gpu_layers != "all") {
        std::uint32_t layers{};
        const auto [end, ec] = std::from_chars(
            profile.gpu_layers.data(), profile.gpu_layers.data() + profile.gpu_layers.size(), layers);
        if (ec != std::errc{} || end != profile.gpu_layers.data() + profile.gpu_layers.size()) {
            throw std::invalid_argument("gpu_layers must be auto, all, or an unsigned layer count");
        }
    }
    if (profile.context_tokens < 4'096 || profile.context_tokens > 262'144) {
        throw std::invalid_argument("context_tokens must be between 4096 and 262144");
    }
    if (profile.parallel_slots < 1 || profile.parallel_slots > 4) {
        throw std::invalid_argument("parallel_slots must be between 1 and 4");
    }
    const auto effective_context = effective_context_tokens(profile);
    if (effective_context < 4'096) throw std::invalid_argument("effective per-slot context must be at least 4096");
    if (profile.context_reserve_tokens >= effective_context) {
        throw std::invalid_argument("context_reserve_tokens must be smaller than effective per-slot context");
    }
    if (profile.compact_target_ratio >= profile.compact_trigger_ratio) {
        throw std::invalid_argument("compact_target_ratio must be below compact_trigger_ratio");
    }
    if (profile.threads == 0 || profile.threads > 256 || profile.batch_threads == 0 || profile.batch_threads > 256) {
        throw std::invalid_argument("runtime thread counts must be between 1 and 256");
    }
    if (profile.batch_size == 0 || profile.batch_size > 65'536 || profile.ubatch_size == 0 || profile.ubatch_size > profile.batch_size) {
        throw std::invalid_argument("batch_size must be 1..65536 and ubatch_size must be 1..batch_size");
    }
    if (profile.expert_cache_mib > 65'536 || profile.pinned_host_mib > 262'144 || profile.prompt_cache_mib > 65'536) {
        throw std::invalid_argument("runtime cache values exceed supported safety bounds");
    }
    if (profile.speculative_tokens > 64) {
        throw std::invalid_argument("speculative_tokens must be between 0 and 64");
    }
    if (profile.runtime == RuntimeKind::qwen_flash && profile.source == ModelSourceKind::hugging_face && profile.repo.empty()) {
        throw std::invalid_argument("hugging-face runtime model requires repo");
    }
    if (profile.runtime == RuntimeKind::qwen_flash && profile.source == ModelSourceKind::local_gguf && profile.local_path.empty()) {
        throw std::invalid_argument("local GGUF runtime model requires local_path");
    }
    if (profile.speculative_enabled) {
        // Prefer a specific error for legacy self-referential MTP profiles.
        // Previously the missing-file guard always threw first, making this
        // rejection unreachable and breaking the unsafe-scheduling test.
        if (!profile.speculative_repo.empty() && profile.speculative_repo == profile.repo) {
            throw std::invalid_argument(
                "speculative decoding cannot use the main model repository as its draft model; "
                "select a dedicated MTP/draft GGUF sidecar instead");
        }
        if (profile.speculative_type != "draft-mtp" || !profile.speculative_repo.empty()) {
            throw std::invalid_argument("Only a local, dedicated Qwen draft-mtp model is qualified for this runtime");
        }
        if (profile.speculative_model.empty()) {
            throw std::invalid_argument(
                "Qwen MTP requires an independent local draft GGUF; external LM Studio models and repo guesses are not supported");
        }
        auto extension = profile.speculative_model.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (extension != ".gguf") throw std::invalid_argument("The draft-MTP sidecar must be a local .gguf file");
    }
    return profile;
}

} // namespace engcoder::model
