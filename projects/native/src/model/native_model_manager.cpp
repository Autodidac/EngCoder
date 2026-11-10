#include "engcoder/model/native_model_manager.hpp"

#include "engcoder/process/process.hpp"
#include "engcoder/core/ids.hpp"
#include "engcoder/core/file_replace.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <ranges>
#include <regex>
#include <stdexcept>
#include <system_error>

namespace engcoder::model {
namespace {

[[nodiscard]] bool path_is_within(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    auto candidate_it = candidate.begin();
    auto root_it = root.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *candidate_it != *root_it) return false;
    }
    return candidate_it != candidate.end();
}


[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return nlohmann::json::object();
    try { return nlohmann::json::parse(input); }
    catch (...) { return nlohmann::json::object(); }
}

// Profiles written by early 0.6.5 builds could point their speculative
// draft at the full main model. Repair JSON *before* strict validation so a
// user's tuned GPU/context settings survive rather than replacing the entire
// model profile with factory defaults or silently hiding custom models.
[[nodiscard]] bool repair_legacy_self_draft(nlohmann::json& manifest) {
    if (!manifest.is_object()) return false;
    const auto revision = manifest.value("profile_revision", 1U);
    if (revision >= 8U) return false;
    const auto string_field = [&](std::string_view key) -> std::string {
        auto it = manifest.find(std::string{key});
        return it != manifest.end() && it->is_string() ? it->get<std::string>() : std::string{};
    };
    const auto repo = string_field("repo");
    if (repo.empty() || string_field("speculative_repo") != repo || !string_field("speculative_model").empty()) {
        return false;
    }
    manifest["speculative_enabled"] = false;
    manifest["speculative_repo"] = nullptr;
    return true;
}

// Revision 9 changes the scale, not the physical meaning of old saved values:
// old 18% -> new 30%; old 55% -> new 92%; old >=60% -> new 100%.
// Keep the old load approximately unchanged for customized profiles. The
// recognizable factory built-in preset instead uses the current default.
[[nodiscard]] std::uint32_t rescale_legacy_gpu_percent(std::uint32_t old_percent) noexcept {
    return std::min<std::uint32_t>(100U,
        (old_percent * 100U + gpu_offload_legacy_ceiling_percent / 2U) /
            gpu_offload_legacy_ceiling_percent);
}

[[nodiscard]] bool migrate_legacy_gpu_scale(nlohmann::json& manifest) {
    if (!manifest.is_object() || manifest.value("profile_revision", 1U) >= runtime_profile_revision) {
        return false;
    }
    const auto old_percent = manifest.value("gpu_offload_percent", 36U);
    if (old_percent > 100U) return false; // Validation will reject it.
    manifest["gpu_offload_percent"] = rescale_legacy_gpu_percent(old_percent);
    manifest["profile_revision"] = runtime_profile_revision;
    return true;
}

[[nodiscard]] std::string strip_scheme(std::string_view value) {
    if (value.starts_with("qwenflash://")) value.remove_prefix(12);
    return std::string{value};
}

[[nodiscard]] std::string trim_path_text(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }
    return value;
}

[[nodiscard]] std::expected<std::uint64_t, std::string> local_gguf_set_bytes(
    const std::filesystem::path& first_shard) {
    static const std::regex split_pattern{R"((.*)-(\d{5})-of-(\d{5})\.gguf$)", std::regex::icase};
    std::smatch match;
    const auto filename = first_shard.filename().string();
    std::error_code ec;
    if (!std::regex_match(filename, match, split_pattern) || match.size() != 4) {
        const auto bytes = std::filesystem::file_size(first_shard, ec);
        if (ec || bytes == 0) return std::unexpected("Local GGUF is empty or unreadable: " + first_shard.string());
        return bytes;
    }

    const auto total = static_cast<unsigned>(std::stoul(match[3].str()));
    const auto prefix = match[1].str();
    std::uint64_t total_bytes = 0;
    for (unsigned index = 1; index <= total; ++index) {
        char suffix[32]{};
        std::snprintf(suffix, sizeof(suffix), "-%05u-of-%05u.gguf", index, total);
        const auto shard = first_shard.parent_path() / (prefix + suffix);
        ec.clear();
        if (!std::filesystem::is_regular_file(shard, ec) || ec) {
            return std::unexpected("Missing GGUF shard: " + shard.string());
        }
        ec.clear();
        const auto bytes = std::filesystem::file_size(shard, ec);
        if (ec || bytes == 0) {
            return std::unexpected("GGUF shard is empty or unreadable: " + shard.string());
        }
        total_bytes += bytes;
    }
    return total_bytes;
}

[[nodiscard]] std::expected<std::filesystem::path, std::string> normalize_local_gguf_path(
    const std::filesystem::path& raw_path) {
    const auto cleaned = trim_path_text(raw_path.string());
    if (cleaned.empty()) return std::unexpected("Local GGUF path is empty");

    std::filesystem::path path{cleaned};
    const auto filename_lower = [&] {
        auto value = path.filename().string();
        std::ranges::transform(value, value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }();
    if (filename_lower.starts_with("mmproj-") || filename_lower == "mmproj.gguf") {
        return std::unexpected(
            "The selected file is an mmproj vision projector, not the Qwen model. "
            "Choose the first model shard ending in -00001-of-0000N.gguf.");
    }
    if (!filename_lower.ends_with(".gguf")) {
        return std::unexpected("Local model override must point to a .gguf file");
    }

    static const std::regex split_pattern{R"((.*)-(\d{5})-of-(\d{5})\.gguf$)", std::regex::icase};
    std::smatch match;
    const auto filename = path.filename().string();
    if (std::regex_match(filename, match, split_pattern) && match.size() == 4) {
        const auto total = static_cast<unsigned>(std::stoul(match[3].str()));
        if (total == 0) return std::unexpected("Invalid split GGUF shard count");
        char suffix[32]{};
        std::snprintf(suffix, sizeof(suffix), "-%05u-of-%05u.gguf", 1U, total);
        path = path.parent_path() / (match[1].str() + suffix);
    }

    std::error_code ec;
    auto normalized = std::filesystem::absolute(path, ec);
    if (ec) normalized = path;
    normalized = normalized.lexically_normal();
    if (!std::filesystem::is_regular_file(normalized, ec) || ec) {
        return std::unexpected("Local GGUF does not exist: " + normalized.string());
    }
    ec.clear();
    if (std::filesystem::file_size(normalized, ec) == 0 || ec) {
        return std::unexpected("Local GGUF is empty or unreadable: " + normalized.string());
    }

    // Match the original QwenFlashRuntime behavior: a split-model override
    // points at shard 1 and is accepted only when every sibling shard exists
    // and is non-empty.
    if (auto bytes = local_gguf_set_bytes(normalized); !bytes) {
        return std::unexpected(bytes.error());
    }

    return normalized;
}

} // namespace

bool is_native_runtime_model(std::string_view model) noexcept {
    return model.starts_with("qwenflash://");
}

NativeModelManager::NativeModelManager(const core::Config& config, std::filesystem::path state_root)
    : config_(config),
      state_root_(std::move(state_root)),
      catalog_root_(state_root_ / "models" / "runtimes"),
      qwen_flash_(config_, state_root_) {
    std::error_code ec;
    std::filesystem::create_directories(catalog_root_, ec);
    seed_defaults();
    std::scoped_lock lock(mutex_);
    load_profiles_locked();
}

NativeModelManager::~NativeModelManager() {
    if (install_worker_.joinable()) install_worker_.request_stop();
    if (runtime_worker_.joinable()) runtime_worker_.request_stop();

    // ensure_started() can be waiting for a large first-time download. Stop the
    // native runtime before joining workers so shutdown remains cancellable.
    qwen_flash_.stop();
    if (install_worker_.joinable()) install_worker_.join();
    if (runtime_worker_.joinable()) runtime_worker_.join();
}

std::string NativeModelManager::normalize_id(std::string_view value) const {
    auto id = strip_scheme(value);
    std::string out;
    out.reserve(id.size());
    bool dash = false;
    for (unsigned char c : id) {
        if (std::isalnum(c) || c == '.' || c == '_') {
            out.push_back(static_cast<char>(std::tolower(c)));
            dash = false;
        } else if (!dash) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

std::filesystem::path NativeModelManager::manifest_path(std::string_view id) const {
    return catalog_root_ / normalize_id(id) / "manifest.json";
}

void NativeModelManager::save_profile(const RuntimeModelProfile& profile) const {
    const auto path = manifest_path(profile.id);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) throw std::filesystem::filesystem_error("Could not create model profile directory", path.parent_path(), ec);

    // Never truncate the live manifest: interrupted writes must leave the last
    // complete profile loadable. A unique sibling also permits concurrent saves
    // without sharing one fixed .tmp filename.
    const auto temporary = path.parent_path() /
        (path.filename().string() + "." + core::make_id("tmp"));
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Could not open temporary model profile: " + temporary.string());
            output << to_json(profile).dump(2) << '\n';
            output.flush();
            if (!output) throw std::runtime_error("Could not write model profile: " + temporary.string());
        }
        // Only the platform layer knows the atomic replacement contract;
        // never remove the existing manifest before the replacement succeeds.
        if (const auto replace_error = core::replace_file(temporary, path); replace_error) {
            throw std::filesystem::filesystem_error(
                "Could not replace model profile", temporary, path, replace_error);
        }
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        throw;
    }
}

void NativeModelManager::seed_defaults() {
    const RuntimeModelProfile qwen{
        .id = "qwen3.8-flash-next-ud-q2-k-xl",
        .display_name = "Qwen3.8 Flash Next · UD-Q2_K_XL",
        .runtime = RuntimeKind::qwen_flash,
        .source = ModelSourceKind::hugging_face,
        .repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
        .quant = "UD-Q2_K_XL",
        .local_path = {},
        .estimated_bytes = 78'900'000'000ULL,
        .profile_revision = runtime_profile_revision,
        .runtime_policy = config_.qwen_flash_runtime_policy,
        .gpu_offload_percent = config_.qwen_flash_gpu_offload_percent,
        .gpu_layer_count_hint = config_.qwen_flash_gpu_layer_count_hint,
        .context_tokens = config_.qwen_flash_context_tokens,
        .parallel_slots = config_.qwen_flash_parallel_slots,
        .context_reserve_tokens = config_.qwen_flash_context_reserve_tokens,
        .auto_compact = config_.qwen_flash_auto_compact,
        .compact_trigger_ratio = config_.qwen_flash_compact_trigger_ratio,
        .compact_target_ratio = config_.qwen_flash_compact_target_ratio,
        .threads = config_.qwen_flash_threads,
        .batch_threads = config_.qwen_flash_batch_threads,
        .batch_size = config_.qwen_flash_batch_size,
        .ubatch_size = config_.qwen_flash_ubatch_size,
        .gpu_layers = config_.qwen_flash_gpu_layers,
        .flash_attention = config_.qwen_flash_flash_attention,
        .kv_type_k = config_.qwen_flash_kv_type_k,
        .kv_type_v = config_.qwen_flash_kv_type_v,
        .kv_gpu_layers = config_.qwen_flash_kv_gpu_layers,
        .kv_offload = config_.qwen_flash_kv_offload,
        .kv_cpu_pinned = config_.qwen_flash_kv_cpu_pinned,
        .phase_aware_workspace = config_.qwen_flash_phase_aware_workspace,
        .load_mode = config_.qwen_flash_load_mode,
        .lazy_mode = config_.qwen_flash_lazy_mode,
        .tensor_override = config_.qwen_flash_tensor_override,
        .prompt_cache_mib = config_.qwen_flash_prompt_cache_mib,
        .context_shift = config_.qwen_flash_context_shift,
        .reasoning_preserve = config_.qwen_flash_reasoning_preserve,
        .expert_cache_mode = config_.qwen_flash_expert_cache_mode,
        .expert_cache_mib = config_.qwen_flash_expert_cache_mib,
        .pinned_host_mib = config_.qwen_flash_pinned_host_mib,
        .early_router = config_.qwen_flash_early_router,
        .speculative_enabled = false,
        .speculative_type = "draft-mtp",
        .speculative_repo = {},
        .speculative_model = {},
        .speculative_tokens = 2,
        .enabled = true,
    };
    const auto builtin_manifest = manifest_path(qwen.id);
    if (!std::filesystem::exists(builtin_manifest)) {
        save_profile(qwen);
        return;
    }

    // 0.6.0 shipped a more aggressive 98K startup preset. Preserve any user
    // customization, but migrate the untouched built-in preset to the safer
    // 80K-class 5080/64 GiB recommendation that still provides >75K active
    // conversation tokens before compaction.
    auto existing_json = read_json(builtin_manifest);
    try {
        const bool draft_repaired = repair_legacy_self_draft(existing_json);
        auto existing = runtime_model_from_json(existing_json);
        const bool untouched_v060 =
            existing.profile_revision < 2U &&
            existing.context_tokens == 98'304U &&
            existing.parallel_slots == 1U &&
            existing.context_reserve_tokens == 8'192U &&
            std::abs(existing.compact_trigger_ratio - 0.90) < 0.0001 &&
            std::abs(existing.compact_target_ratio - 0.70) < 0.0001;
        if (untouched_v060) {
            existing.context_tokens = qwen.context_tokens;
            existing.context_reserve_tokens = qwen.context_reserve_tokens;
            existing.compact_trigger_ratio = qwen.compact_trigger_ratio;
            existing.compact_target_ratio = qwen.compact_target_ratio;
        }
        // 0.6.2 added explicit Recommended / Automatic / Manual runtime policy.
        // Existing profiles default to Recommended so their visible controls remain
        // intact while startup returns to the proven QFR launch semantics.
        if (existing.profile_revision < 3U) {
            existing.runtime_policy = "recommended";
            existing.gpu_offload_percent = 90U;
            existing.gpu_layer_count_hint = 48U;
        }
        // 0.6.5 moved the untouched built-in GPU offload default from 100%
        // to 90% to preserve VRAM headroom.
        if (existing.profile_revision < 5U && existing.runtime_policy == "recommended" &&
            existing.gpu_offload_percent == 100U && existing.gpu_layer_count_hint == 48U) {
            existing.gpu_offload_percent = 90U;
        }
        // 0.6.5 follow-up: 90% still crowds a 16 GiB desktop GPU when the
        // harness is used beside an editor/browser/engine. Migrate only the
        // recognizable untouched recommended preset; Manual/custom profiles
        // keep the user's explicit value.
        const bool untouched_gpu90 =
            existing.profile_revision < 6U &&
            existing.runtime_policy == "recommended" &&
            existing.gpu_offload_percent == 90U &&
            existing.gpu_layer_count_hint == 48U &&
            existing.gpu_layers == "auto" &&
            existing.threads == 12U && existing.batch_threads == 12U &&
            existing.batch_size == 1024U && existing.ubatch_size == 256U;
        if (untouched_gpu90) existing.gpu_offload_percent = 36U;
        // 0.6.5 follow-up: reduce the untouched built-in desktop default to 36%.
        // Only migrate the recognizable 70% preset; Manual/custom profiles retain
        // the user's explicit value.
        const bool untouched_gpu70 =
            existing.profile_revision < 7U &&
            existing.runtime_policy == "recommended" &&
            existing.gpu_offload_percent == 70U &&
            existing.gpu_layer_count_hint == 48U &&
            existing.gpu_layers == "auto" &&
            existing.threads == 12U && existing.batch_threads == 12U &&
            existing.batch_size == 1024U && existing.ubatch_size == 256U;
        if (untouched_gpu70) existing.gpu_offload_percent = 36U;
        // 0.6.4 makes the experimental MoE cache capability-qualified.
        // Existing untouched profiles migrate to auto rather than forcing
        // a cache path that crashes UD-Q2_K_XL on the older backend.
        if (existing.profile_revision < 4U) existing.expert_cache_mode = "auto";

        if (existing.profile_revision < runtime_profile_revision) {
            const bool untouched_builtin_gpu =
                existing.runtime_policy == "recommended" &&
                existing.gpu_offload_percent == 36U &&
                existing.gpu_layer_count_hint == 48U &&
                existing.gpu_layers == "auto" &&
                existing.threads == 12U && existing.batch_threads == 12U &&
                existing.batch_size == 1024U && existing.ubatch_size == 256U;
            existing.gpu_offload_percent = untouched_builtin_gpu
                ? gpu_offload_default_percent
                : rescale_legacy_gpu_percent(existing.gpu_offload_percent);
        }
        existing.profile_revision = runtime_profile_revision;
        // The old path reserialized the built-in profile on *every launch*.
        // Only commit actual migrations/normalization changes; this also uses
        // the [[nodiscard]] repair result instead of silently discarding it.
        if (draft_repaired || to_json(existing) != existing_json) save_profile(existing);
    } catch (const std::exception& error) {
        // Never overwrite a user's saved profile with defaults because it
        // fails parsing or migration. Keep the original for manual recovery.
        std::fprintf(stderr, "EngCoder: built-in model profile preserved after migration failure (%s): %s\n",
            builtin_manifest.string().c_str(), error.what());
    }
}

void NativeModelManager::load_profiles_locked() {
    profiles_.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(catalog_root_, ec) || ec) return;
    for (const auto& entry : std::filesystem::directory_iterator(catalog_root_, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) { ec.clear(); continue; }
        auto manifest = read_json(entry.path() / "manifest.json");
        if (!manifest.is_object() || !manifest.value("format", std::string{}).starts_with("engcoder.runtime-model.v")) continue;
        try {
            const bool repaired = repair_legacy_self_draft(manifest);
            const bool rescaled = migrate_legacy_gpu_scale(manifest);
            auto profile = runtime_model_from_json(manifest);
            if (repaired || rescaled) {
                profile.profile_revision = runtime_profile_revision;
                save_profile(profile);
            }
            profiles_.push_back(std::move(profile));
        } catch (...) {
            // Preserve an invalid user manifest on disk for manual recovery.
            // Do not discard or overwrite its existing settings here.
        }
    }
    std::ranges::sort(profiles_, {}, &RuntimeModelProfile::display_name);
}

std::vector<RuntimeModelProfile> NativeModelManager::models() const {
    std::scoped_lock lock(mutex_);
    return profiles_;
}

std::optional<RuntimeModelProfile> NativeModelManager::find_locked(std::string_view id_or_uri) const {
    const auto id = normalize_id(id_or_uri);
    const auto it = std::ranges::find_if(profiles_, [&](const RuntimeModelProfile& profile) {
        return normalize_id(profile.id) == id;
    });
    if (it == profiles_.end()) return std::nullopt;
    return *it;
}

std::optional<RuntimeModelProfile> NativeModelManager::find(std::string_view id_or_uri) const {
    std::scoped_lock lock(mutex_);
    return find_locked(id_or_uri);
}

std::expected<nlohmann::json, core::Error> NativeModelManager::register_model(const nlohmann::json& spec) {
    try {
        RuntimeModelProfile profile;
        profile.runtime = RuntimeKind::qwen_flash;
        const auto source = spec.value("source", std::string{"hugging-face"});
        profile.source = source == "local-gguf" ? ModelSourceKind::local_gguf : ModelSourceKind::hugging_face;
        profile.repo = spec.value("repo", std::string{});
        profile.quant = spec.value("quant", std::string{});
        profile.local_path = spec.value("local_path", std::string{});
        if (profile.source == ModelSourceKind::local_gguf && !profile.local_path.empty()) {
            auto normalized_local = normalize_local_gguf_path(profile.local_path);
            if (!normalized_local) throw std::invalid_argument(normalized_local.error());
            profile.local_path = *normalized_local;
        }

        auto raw_id = spec.value("id", std::string{});
        if (raw_id.empty()) raw_id = spec.value("display_name", std::string{});
        if (raw_id.empty() && profile.source == ModelSourceKind::hugging_face) {
            raw_id = profile.repo + (profile.quant.empty() ? std::string{} : "-" + profile.quant);
        }
        if (raw_id.empty() && profile.source == ModelSourceKind::local_gguf) {
            raw_id = profile.local_path.stem().string();
        }
        profile.id = normalize_id(raw_id);
        profile.display_name = spec.value("display_name", std::string{});
        if (profile.display_name.empty()) {
            if (profile.source == ModelSourceKind::hugging_face) {
                profile.display_name = profile.repo + (profile.quant.empty() ? std::string{} : " · " + profile.quant);
            } else {
                profile.display_name = profile.local_path.filename().string();
            }
        }
        profile.estimated_bytes = spec.value("estimated_bytes", std::uint64_t{});
        if (profile.estimated_bytes == 0) {
            const auto existing_id = spec.value("id", std::string{});
            if (!existing_id.empty()) {
                std::scoped_lock lock(mutex_);
                if (const auto existing = find_locked(existing_id); existing.has_value()) {
                    profile.estimated_bytes = existing->estimated_bytes;
                }
            }
        }
        profile.profile_revision = runtime_profile_revision;
        profile.runtime_policy = spec.value("runtime_policy", config_.qwen_flash_runtime_policy);
        profile.gpu_offload_percent = spec.value("gpu_offload_percent", config_.qwen_flash_gpu_offload_percent);
        profile.gpu_layer_count_hint = spec.value("gpu_layer_count_hint", config_.qwen_flash_gpu_layer_count_hint);
        profile.context_tokens = spec.value("context_tokens", config_.qwen_flash_context_tokens);
        profile.parallel_slots = std::max(1U, spec.value("parallel_slots", config_.qwen_flash_parallel_slots));
        profile.context_reserve_tokens = spec.value("context_reserve_tokens", config_.qwen_flash_context_reserve_tokens);
        profile.auto_compact = spec.value("auto_compact", config_.qwen_flash_auto_compact);
        profile.compact_trigger_ratio = std::clamp(spec.value("compact_trigger_ratio", config_.qwen_flash_compact_trigger_ratio), 0.50, 0.98);
        profile.compact_target_ratio = std::clamp(spec.value("compact_target_ratio", config_.qwen_flash_compact_target_ratio), 0.25, profile.compact_trigger_ratio - 0.02);
        profile.threads = spec.value("threads", config_.qwen_flash_threads);
        profile.batch_threads = spec.value("batch_threads", config_.qwen_flash_batch_threads);
        profile.batch_size = spec.value("batch_size", config_.qwen_flash_batch_size);
        profile.ubatch_size = spec.value("ubatch_size", config_.qwen_flash_ubatch_size);
        profile.gpu_layers = spec.value("gpu_layers", config_.qwen_flash_gpu_layers);
        profile.flash_attention = spec.value("flash_attention", config_.qwen_flash_flash_attention);
        profile.kv_type_k = spec.value("kv_type_k", config_.qwen_flash_kv_type_k);
        profile.kv_type_v = spec.value("kv_type_v", config_.qwen_flash_kv_type_v);
        profile.kv_gpu_layers = spec.value("kv_gpu_layers", config_.qwen_flash_kv_gpu_layers);
        profile.kv_offload = spec.value("kv_offload", config_.qwen_flash_kv_offload);
        profile.kv_cpu_pinned = spec.value("kv_cpu_pinned", config_.qwen_flash_kv_cpu_pinned);
        profile.phase_aware_workspace = spec.value("phase_aware_workspace", config_.qwen_flash_phase_aware_workspace);
        profile.load_mode = spec.value("load_mode", config_.qwen_flash_load_mode);
        profile.lazy_mode = spec.value("lazy_mode", config_.qwen_flash_lazy_mode);
        profile.tensor_override = spec.value("tensor_override", config_.qwen_flash_tensor_override);
        profile.prompt_cache_mib = spec.value("prompt_cache_mib", config_.qwen_flash_prompt_cache_mib);
        profile.context_shift = spec.value("context_shift", config_.qwen_flash_context_shift);
        profile.reasoning_preserve = spec.value("reasoning_preserve", config_.qwen_flash_reasoning_preserve);
        profile.expert_cache_mode = spec.value("expert_cache_mode", config_.qwen_flash_expert_cache_mode);
        profile.expert_cache_mib = spec.value("expert_cache_mib", config_.qwen_flash_expert_cache_mib);
        profile.pinned_host_mib = spec.value("pinned_host_mib", config_.qwen_flash_pinned_host_mib);
        profile.early_router = spec.value("early_router", config_.qwen_flash_early_router);
        profile.speculative_enabled = spec.value("speculative_enabled", false);
        profile.speculative_type = spec.value("speculative_type", std::string{"draft-mtp"});
        profile.speculative_repo = spec.value("speculative_repo", std::string{});
        profile.speculative_model = spec.value("speculative_model", std::string{});
        profile.speculative_tokens = spec.value("speculative_tokens", 2U);
        profile.enabled = true;
        if (profile.id.empty()) throw std::invalid_argument("model id is required");
        if (profile.source == ModelSourceKind::hugging_face && profile.repo.empty()) throw std::invalid_argument("Hugging Face repo is required");
        if (profile.source == ModelSourceKind::local_gguf && profile.local_path.empty()) throw std::invalid_argument("local GGUF path is required");
        if (profile.context_tokens > 262'144U) throw std::invalid_argument("Qwen Flash native context cannot exceed 262144 without an explicit extended-context backend profile");
        if (profile.parallel_slots > 4U) throw std::invalid_argument("parallel_slots must be between 1 and 4");
        if (effective_context_tokens(profile) < 4'096U) throw std::invalid_argument("effective per-slot context is too small");
        if (profile.compact_target_ratio >= profile.compact_trigger_ratio) throw std::invalid_argument("compact target must be below compact trigger");
        profile = runtime_model_from_json(to_json(profile));

        const bool runtime_reload_required =
            qwen_flash_.running() && qwen_flash_.active_model_id() == profile.id;

        // Runtime-profile edits are configuration changes, not lifecycle
        // commands. Persist them even while this model is loaded, but never
        // tear down/reload a live model implicitly. The active llama-server
        // keeps its current GPU/KV layout until the next explicit load.
        save_profile(profile);
        {
            std::scoped_lock lock(mutex_);
            load_profiles_locked();
        }
        auto payload = to_json(profile);
        payload["registered"] = true;
        payload["runtime_reload_required"] = runtime_reload_required;
        payload["active_runtime_unchanged"] = runtime_reload_required;
        return payload;
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), error.what()});
    }
}

void NativeModelManager::set_install_state(nlohmann::json state) {
    std::scoped_lock lock(mutex_);
    install_state_ = std::move(state);
}

std::expected<nlohmann::json, core::Error> NativeModelManager::install(std::string_view id_or_uri) {
    RuntimeModelProfile profile;
    {
        std::scoped_lock lock(mutex_);
        auto found = find_locked(id_or_uri);
        if (!found) return std::unexpected(core::Error{core::make_error_code(core::Errc::model_unavailable), "Unknown native model: " + std::string{id_or_uri}});
        profile = *found;
        if (install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed), "Another native model download is already active"});
        }
        if (runtime_install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed), "Qwen Flash runtime installation is still in progress"});
        }
    }

    const auto cache = qwen_flash_.cache_status(profile);
    if (cache.value("installed", false)) {
        auto state = nlohmann::json{{"state", "installed"}, {"model", runtime_model_uri(profile)}, {"cache", cache}};
        set_install_state(state);
        return state;
    }
    if (profile.source == ModelSourceKind::local_gguf) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Local GGUF file is not available: " + profile.local_path.string(),
        });
    }

    if (profile.estimated_bytes > 0) {
        std::error_code space_error;
        const auto selected_cache_root = std::filesystem::path{
            cache.value("cache_root", config_.model_cache_root.string())};
        const auto capacity = std::filesystem::space(selected_cache_root, space_error);
        if (!space_error) {
            const auto cached = cache.value("cached_bytes", std::uint64_t{});
            const auto remaining = profile.estimated_bytes > cached ? profile.estimated_bytes - cached : std::uint64_t{};
            constexpr std::uint64_t reserve = 2ULL * 1024ULL * 1024ULL * 1024ULL;
            if (capacity.available < remaining + reserve) {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::process_failed),
                    "Insufficient free space in native model cache " + selected_cache_root.string() + ": need about " +
                        std::to_string((remaining + reserve) / (1024ULL * 1024ULL * 1024ULL)) +
                        " GiB, have " + std::to_string(capacity.available / (1024ULL * 1024ULL * 1024ULL)) + " GiB",
                });
            }
        }
    }
    if (qwen_flash_.running()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::process_failed),
            "Stop the active Qwen Flash model before downloading another model",
        });
    }

    if (install_worker_.joinable()) install_worker_.join();
    set_install_state({{"state", "installing"}, {"model", runtime_model_uri(profile)}, {"display_name", profile.display_name}});
    install_worker_ = std::jthread([this, profile](std::stop_token stop) {
        if (stop.stop_requested()) return;
        auto started = qwen_flash_.ensure_started(profile);
        if (!started) {
            set_install_state({
                {"state", "failed"},
                {"model", runtime_model_uri(profile)},
                {"error", started.error().message},
                {"cache", qwen_flash_.cache_status(profile)},
            });
            return;
        }
        qwen_flash_.stop();
        set_install_state({
            {"state", "installed"},
            {"model", runtime_model_uri(profile)},
            {"cache", qwen_flash_.cache_status(profile)},
        });
    });

    return nlohmann::json{{"state", "installing"}, {"model", runtime_model_uri(profile)}};
}

std::expected<nlohmann::json, core::Error> NativeModelManager::provision_runtime() {
    if (const auto server = qwen_flash_.discover_server(); !server.empty() && qwen_flash_.runtime_verified()) {
        std::scoped_lock lock(mutex_);
        runtime_install_state_ = {
            {"state", "installed"},
            {"runtime", "qwen-flash"},
            {"server", server.string()},
            {"verified", true},
        };
        return runtime_install_state_;
    }

    {
        std::scoped_lock lock(mutex_);
        if (runtime_install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::process_failed),
                "Qwen Flash runtime installation is already active",
            });
        }
        if (install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::process_failed),
                "Wait for the active model download to finish before installing the runtime",
            });
        }
    }

#ifdef _WIN32
    const auto script = config_.workspace / "runtime" / "qwen-flash" / "install-runtime.ps1";
    const std::string executable = "powershell.exe";
    const std::vector<std::string> arguments{
        "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script.string(),
        "-StateRoot", state_root_.string(),
    };
#else
    const auto script = config_.workspace / "runtime" / "qwen-flash" / "install-runtime.sh";
    const std::string executable = "/bin/bash";
    const std::vector<std::string> arguments{script.string(), state_root_.string()};
#endif
    if (!std::filesystem::is_regular_file(script)) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            "Qwen Flash runtime installer is missing: " + script.string(),
        });
    }

    if (runtime_worker_.joinable()) runtime_worker_.join();
    const auto runtime_progress_path = state_root_ / "runtime-install-qwen-flash.json";
    {
        std::error_code remove_error;
        std::filesystem::remove(runtime_progress_path, remove_error);
    }
    {
        std::scoped_lock lock(mutex_);
        runtime_install_state_ = {
            {"state", "installing"},
            {"runtime", "qwen-flash"},
            {"phase", "starting"},
            {"percent", 1},
            {"message", "Preparing Qwen runtime backend update"},
            {"detail", "This updates the llama.cpp/CUDA inference backend; model weights are unchanged."},
        };
    }

    runtime_worker_ = std::jthread([this, executable, arguments](std::stop_token stop) {
        process::Request request;
        request.executable = executable;
        request.arguments = arguments;
        request.working_directory = config_.workspace;
        request.environment.emplace_back("ENGCODER_AUTO_INSTALL_CUDA", "1");
        request.timeout = std::chrono::hours{4};
        request.max_output_bytes = 8 * 1024 * 1024;

        auto result = process::run(request, stop);
        nlohmann::json state{{"runtime", "qwen-flash"}};
        if (!result) {
            state["state"] = "failed";
            state["error"] = result.error().message;
        } else if (result->cancelled) {
            state["state"] = "cancelled";
        } else if (result->timed_out) {
            state["state"] = "failed";
            state["error"] = "Qwen Flash runtime build timed out";
        } else if (result->exit_code != 0) {
            state["state"] = "failed";
            state["error"] = !result->stderr_text.empty() ? result->stderr_text : result->stdout_text;
        } else if (const auto server = qwen_flash_.discover_server(); server.empty()) {
            state["state"] = "failed";
            state["error"] = "Runtime installer completed but llama-server was not staged";
        } else {
            state["state"] = "installed";
            state["server"] = server.string();
        }
        std::scoped_lock lock(mutex_);
        runtime_install_state_ = std::move(state);
    });

    return nlohmann::json{
        {"state", "installing"},
        {"runtime", "qwen-flash"},
    };
}

std::expected<std::string, core::Error> NativeModelManager::prepare(
    std::string_view id_or_uri,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stop) {
    RuntimeModelProfile profile;
    {
        std::scoped_lock lock(mutex_);
        auto found = find_locked(id_or_uri);
        if (!found) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Unknown native model: " + std::string{id_or_uri},
            });
        }
        profile = *found;
        if (install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Native model download is still in progress: " +
                    install_state_.value("model", runtime_model_uri(profile)),
            });
        }
        if (runtime_install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Qwen Flash runtime installation is still in progress",
            });
        }
    }
    return qwen_flash_.ensure_started(profile, deadline, stop);
}

std::expected<std::string, core::Error> NativeModelManager::prepare_with_local_override(
    std::string_view id_or_uri,
    const std::filesystem::path& local_path_override,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stop) {
    auto normalized_local = normalize_local_gguf_path(local_path_override);
    if (!normalized_local) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            normalized_local.error(),
        });
    }

    RuntimeModelProfile profile;
    {
        std::scoped_lock lock(mutex_);
        auto found = find_locked(id_or_uri);
        if (!found) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Unknown native model: " + std::string{id_or_uri},
            });
        }
        profile = *found;
        if (install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Native model download is still in progress",
            });
        }
        if (runtime_install_state_.value("state", std::string{"idle"}) == "installing") {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Qwen Flash runtime installation is still in progress",
            });
        }
    }

    if (profile.estimated_bytes > 0) {
        auto total_bytes = local_gguf_set_bytes(*normalized_local);
        if (!total_bytes) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                total_bytes.error(),
            });
        }
        const auto minimum = profile.estimated_bytes - (profile.estimated_bytes / 10U);
        if (*total_bytes < minimum) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                "Local GGUF override is incomplete/truncated: " +
                    std::to_string(*total_bytes / (1024ULL * 1024ULL * 1024ULL)) +
                    " GiB present, about " +
                    std::to_string(profile.estimated_bytes / (1024ULL * 1024ULL * 1024ULL)) +
                    " GiB expected for " + profile.display_name,
            });
        }
    }

    profile.source = ModelSourceKind::local_gguf;
    profile.local_path = *normalized_local;
    // A direct load override must never fall back to -hf or another cache.
    // Keeping repo/quant is useful only as profile metadata.
    return qwen_flash_.ensure_started(profile, deadline, stop);
}

std::optional<std::string> NativeModelManager::active_model_uri() const {
    if (!qwen_flash_.running()) return std::nullopt;

    const auto active_id = qwen_flash_.active_model_id();
    if (active_id.empty()) return std::nullopt;

    std::scoped_lock lock(mutex_);
    if (const auto profile = find_locked(active_id); profile.has_value()) {
        return runtime_model_uri(*profile);
    }

    // Preserve a usable route even if a catalog entry was removed while the
    // runtime is still winding down. This should be transient, but it is safer
    // than silently falling through to an unrelated external provider.
    return "qwenflash://" + active_id;
}

void NativeModelManager::stop_active() {
    qwen_flash_.stop();
}


std::expected<nlohmann::json, core::Error> NativeModelManager::clear_transient_cache(std::string_view id_or_uri) {
    auto profile = find(id_or_uri);
    if (!profile) return std::unexpected(core::Error{core::make_error_code(core::Errc::model_unavailable), "Unknown native model"});
    {
        std::scoped_lock lock(mutex_);
        if (install_state_.value("state", std::string{}) == "installing") {
            return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed),
                "Wait for the model download to finish before clearing temporary cache files"});
        }
    }
    if (qwen_flash_.running()) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed),
            "Unload the native model before clearing temporary cache files"});
    }

    const auto cache = qwen_flash_.cache_status(*profile);
    const auto root = std::filesystem::path{cache.value("path", std::string{})};
    if (root.empty()) return nlohmann::json{{"cleared", true}, {"files_removed", 0}, {"bytes_removed", 0}};

    std::error_code ec;
    const auto configured = std::filesystem::weakly_canonical(config_.model_cache_root, ec);
    ec.clear();
    const auto candidate = std::filesystem::weakly_canonical(root, ec);
    if (ec || configured.empty() || candidate.empty() || !path_is_within(candidate, configured)) {
        return nlohmann::json{{"cleared", true}, {"files_removed", 0}, {"bytes_removed", 0}, {"note", "External/local model paths are never cleared by Clear cache"}};
    }

    std::uint64_t bytes_removed = 0;
    std::size_t files_removed = 0;
    const auto transient = [](const std::filesystem::path& path) {
        auto name = path.filename().string();
        std::ranges::transform(name, name.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        const auto ext = path.extension().string();
        if (name.ends_with(".tmp") || name.ends_with(".part") || name.ends_with(".partial") ||
            name.ends_with(".lock") || name.ends_with(".aria2") || name.ends_with(".incomplete")) return true;
        return ext == ".tmp" || ext == ".part" || ext == ".partial";
    };
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             candidate, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
        if (!transient(entry.path())) continue;
        // Model weights are never cache-trash. Even a .part-like GGUF name is preserved.
        auto lower_name = entry.path().filename().string();
        std::ranges::transform(lower_name, lower_name.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (lower_name.find(".gguf") != std::string::npos) continue;
        const auto size = entry.file_size(ec); ec.clear();
        if (std::filesystem::remove(entry.path(), ec) && !ec) {
            bytes_removed += size;
            ++files_removed;
        }
        ec.clear();
    }
    return nlohmann::json{{"cleared", true}, {"files_removed", files_removed}, {"bytes_removed", bytes_removed}, {"weights_preserved", true}};
}

std::expected<nlohmann::json, core::Error> NativeModelManager::delete_downloaded_weights(std::string_view id_or_uri) {
    auto profile = find(id_or_uri);
    if (!profile) return std::unexpected(core::Error{core::make_error_code(core::Errc::model_unavailable), "Unknown native model"});
    {
        std::scoped_lock lock(mutex_);
        if (install_state_.value("state", std::string{}) == "installing") {
            return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed),
                "Wait for the model download to finish before deleting downloaded model weights"});
        }
    }
    if (qwen_flash_.running() && qwen_flash_.active_model_id() == profile->id) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed), "Unload the model before deleting downloaded weights"});
    }
    if (profile->source == ModelSourceKind::local_gguf) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), "EngCoder will not delete a Local GGUF file. Delete external model files manually if intended."});
    }

    const auto cache = qwen_flash_.cache_status(*profile);
    const auto path = std::filesystem::path{cache.value("path", std::string{})};
    if (path.empty()) return nlohmann::json{{"weights_deleted", false}, {"bytes_deleted", 0}};
    std::error_code ec;
    const auto configured = std::filesystem::weakly_canonical(config_.model_cache_root, ec);
    ec.clear();
    const auto candidate = std::filesystem::weakly_canonical(path, ec);
    if (ec || configured.empty() || candidate.empty() || !path_is_within(candidate, configured)) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::invalid_argument), "Refusing to delete model weights outside EngCoder's configured model cache"});
    }
    const auto bytes = cache.value("cached_bytes", std::uint64_t{});
    std::filesystem::remove_all(candidate, ec);
    if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed), "Could not delete downloaded model weights: " + ec.message()});
    return nlohmann::json{{"weights_deleted", true}, {"bytes_deleted", bytes}, {"model", runtime_model_uri(*profile)}, {"profile_preserved", true}};
}

std::expected<nlohmann::json, core::Error> NativeModelManager::remove(std::string_view id_or_uri, bool delete_cache) {
    auto profile = find(id_or_uri);
    if (!profile) return std::unexpected(core::Error{core::make_error_code(core::Errc::model_unavailable), "Unknown native model"});
    if (qwen_flash_.running() && qwen_flash_.active_model_id() == profile->id) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::process_failed), "Cannot remove the active model; unload it first"});
    }

    std::error_code ec;
    if (delete_cache) {
        auto deleted = delete_downloaded_weights(id_or_uri);
        if (!deleted) return std::unexpected(deleted.error());
    }
    // The built-in profile is deliberately non-removable, but its downloaded
    // weights may be deleted by explicit request. Report that distinction.
    const bool is_builtin = profile->id == "qwen3.8-flash-next-ud-q2-k-xl";
    if (!is_builtin) {
        std::filesystem::remove_all(manifest_path(profile->id).parent_path(), ec);
        if (ec) return std::unexpected(core::Error{core::make_error_code(core::Errc::io_error),
            "Could not remove model profile: " + ec.message()});
    }
    {
        std::scoped_lock lock(mutex_);
        load_profiles_locked();
        if (install_state_.value("model", std::string{}) == runtime_model_uri(*profile)) {
            install_state_ = nlohmann::json{{"state", "idle"}};
        }
    }
    return nlohmann::json{{"removed", !is_builtin}, {"profile_preserved", is_builtin},
        {"model", runtime_model_uri(*profile)}, {"cache_deleted", delete_cache}};
}

nlohmann::json NativeModelManager::runtime_types() const {
    const nlohmann::json recommended{
        {"repo", "unsloth/Qwen3.8-Flash-Next-GGUF"},
        {"quant", "UD-Q2_K_XL"},
        {"display_name", "Qwen3.8 Flash Next · UD-Q2_K_XL"},
        {"runtime_policy", config_.qwen_flash_runtime_policy},
        {"gpu_offload_percent", config_.qwen_flash_gpu_offload_percent},
        {"gpu_layer_count_hint", config_.qwen_flash_gpu_layer_count_hint},
        {"context_tokens", config_.qwen_flash_context_tokens},
        {"parallel_slots", config_.qwen_flash_parallel_slots},
        {"context_reserve_tokens", config_.qwen_flash_context_reserve_tokens},
        {"auto_compact", config_.qwen_flash_auto_compact},
        {"compact_trigger_ratio", config_.qwen_flash_compact_trigger_ratio},
        {"compact_target_ratio", config_.qwen_flash_compact_target_ratio},
        {"threads", config_.qwen_flash_threads},
        {"batch_threads", config_.qwen_flash_batch_threads},
        {"batch_size", config_.qwen_flash_batch_size},
        {"ubatch_size", config_.qwen_flash_ubatch_size},
        {"gpu_layers", config_.qwen_flash_gpu_layers},
        {"flash_attention", config_.qwen_flash_flash_attention},
        {"kv_type_k", config_.qwen_flash_kv_type_k},
        {"kv_type_v", config_.qwen_flash_kv_type_v},
        {"kv_gpu_layers", config_.qwen_flash_kv_gpu_layers},
        {"kv_offload", config_.qwen_flash_kv_offload},
        {"kv_cpu_pinned", config_.qwen_flash_kv_cpu_pinned},
        {"phase_aware_workspace", config_.qwen_flash_phase_aware_workspace},
        {"load_mode", config_.qwen_flash_load_mode},
        {"lazy_mode", config_.qwen_flash_lazy_mode},
        {"tensor_override", config_.qwen_flash_tensor_override},
        {"prompt_cache_mib", config_.qwen_flash_prompt_cache_mib},
        {"context_shift", config_.qwen_flash_context_shift},
        {"reasoning_preserve", config_.qwen_flash_reasoning_preserve},
        {"expert_cache_mode", config_.qwen_flash_expert_cache_mode},
        {"expert_cache_mib", config_.qwen_flash_expert_cache_mib},
        {"pinned_host_mib", config_.qwen_flash_pinned_host_mib},
        {"early_router", config_.qwen_flash_early_router},
        {"speculative_enabled", false},
        {"speculative_type", "draft-mtp"},
        {"speculative_repo", ""},
        {"speculative_model", ""},
        {"speculative_tokens", 2},
    };
    return nlohmann::json::array({
        nlohmann::json{
            {"id", "qwen-flash"},
            {"display_name", "Qwen Flash / Sparse MoE"},
            {"native", true},
            {"model_format", "GGUF"},
            {"sources", nlohmann::json::array({"hugging-face", "local-gguf"})},
            {"native_context_limit", 262144},
            {"recommended", recommended},
            {"context_presets", nlohmann::json::array({81920, 98304, 131072, 196608, 262144})},
            {"runtime_policies", nlohmann::json::array({"recommended", "automatic", "manual"})},
            {"capabilities", {
                {"download", true},
                {"import", true},
                {"load_unload", true},
                {"streaming", true},
                {"tools", true},
                {"engcoder_harness", true},
                {"architect_routing", true},
                {"episodic_memory", true},
                {"automatic_context_compaction", true},
                {"mmap_lazy", true},
                {"expert_cache", true},
                {"expert_cache_auto_qualification", true},
                {"gpu_offload_ratio", true},
                {"hardware_auto_tuning", true},
                {"speculative", "local-qwen-draft-mtp-only; GGUF/architecture qualification required"},
            }},
        }
    });
}

nlohmann::json NativeModelManager::describe() const {
    nlohmann::json items = nlohmann::json::array();
    for (const auto& profile : models()) {
        auto item = to_json(profile);
        item["uri"] = runtime_model_uri(profile);
        item["cache"] = qwen_flash_.cache_status(profile);
        item["active"] = qwen_flash_.running() && qwen_flash_.active_model_id() == profile.id;
        items.push_back(std::move(item));
    }
    nlohmann::json install;
    nlohmann::json runtime_install;
    {
        std::scoped_lock lock(mutex_);
        install = install_state_;
        runtime_install = runtime_install_state_;
    }
    if (runtime_install.value("state", std::string{"idle"}) == "installing") {
        const auto progress = read_json(state_root_ / "runtime-install-qwen-flash.json");
        if (progress.is_object()) {
            for (const auto* key : {"phase", "percent", "message", "detail", "updated_at"}) {
                if (progress.contains(key)) runtime_install[key] = progress.at(key);
            }
        }
    }
    std::error_code space_error;
    const auto space = std::filesystem::space(config_.model_cache_root, space_error);
    return {
        {"format", "engcoder.native-model-library.v1"},
        {"runtime_types", runtime_types()},
        {"models", std::move(items)},
        {"install", std::move(install)},
        {"runtime_install", std::move(runtime_install)},
        {"cache", {
            {"root", config_.model_cache_root.string()},
            {"available_bytes", space_error ? nlohmann::json(nullptr) : nlohmann::json(space.available)},
            {"capacity_bytes", space_error ? nlohmann::json(nullptr) : nlohmann::json(space.capacity)},
        }},
        {"qwen_flash", qwen_flash_.describe()},
    };
}

} // namespace engcoder::model
