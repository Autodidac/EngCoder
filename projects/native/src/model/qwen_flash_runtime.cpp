#include "engcoder/model/qwen_flash_runtime.hpp"

#include "engcoder/process/process.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <httplib.h>
#include <set>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/sysinfo.h>
#endif

namespace engcoder::model {
namespace {

constexpr std::string_view kQwenBackendCommit = "167742d9dfa01306786e7d3cfd31fcb8a8b185dc";
constexpr std::string_view kQwenBackendShortCommit = "167742d9dfa0";
constexpr std::string_view kQwenRuntimeRecipe = "engcoder-qwenflash-2026.10.06-q2pad1";
constexpr std::string_view kLegacyQfrRecipe = "qfr-vcpkg-2026.07.29";
constexpr std::string_view kQ2PaddingFeature = "qwen38-q2-padded-down-v1";

class ScopeExit {
public:
    explicit ScopeExit(std::function<void()> callback) : callback_(std::move(callback)) {}
    ~ScopeExit() { if (callback_) callback_(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    std::function<void()> callback_;
};

[[nodiscard]] std::string environment_value(std::string_view name) {
    const std::string key{name};
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key.c_str()) != 0 || value == nullptr) return {};
    std::string result;
    if (length > 1 && *value != '\0') result.assign(value);
    std::free(value);
    return result;
#else
    if (const char* value = std::getenv(key.c_str()); value != nullptr && *value != '\0') return value;
    return {};
#endif
}

[[nodiscard]] std::string lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

[[nodiscard]] bool model_size_is_plausible(const RuntimeModelProfile& profile, std::uint64_t bytes) {
    if (bytes == 0) return false;
    if (profile.estimated_bytes == 0) return true;

    // Built-in estimates are approximate, so leave 10% tolerance for upstream
    // repacks while still rejecting interrupted/manual copies that merely have
    // every split filename present. The stock Qwen3.8 Flash UD-Q2_K_XL set is
    // about 78.9 GB, making a 20-30 GB partial cache unmistakably incomplete.
    const auto minimum = profile.estimated_bytes - (profile.estimated_bytes / 10U);
    return bytes >= minimum;
}

struct LocalGgufInspection {
    bool complete{false};
    std::uint64_t bytes{};
    std::size_t shard_count{};
    std::size_t expected_shards{};
    std::string error;
};

[[nodiscard]] LocalGgufInspection inspect_local_gguf(const std::filesystem::path& first_shard) {
    LocalGgufInspection result;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(first_shard, ec) || ec) {
        result.error = "Local GGUF does not exist: " + first_shard.string();
        return result;
    }

    static const std::regex split_pattern{R"((.*)-(\d{5})-of-(\d{5})\.gguf$)", std::regex::icase};
    std::smatch match;
    const auto filename = first_shard.filename().string();
    if (!std::regex_match(filename, match, split_pattern) || match.size() != 4) {
        const auto bytes = std::filesystem::file_size(first_shard, ec);
        if (ec || bytes == 0) {
            result.error = "Local GGUF is empty or unreadable: " + first_shard.string();
            return result;
        }
        result.complete = true;
        result.bytes = bytes;
        result.shard_count = 1;
        result.expected_shards = 1;
        return result;
    }

    const auto prefix = match[1].str();
    const auto total = static_cast<unsigned>(std::stoul(match[3].str()));
    result.expected_shards = total;
    for (unsigned index = 1; index <= total; ++index) {
        char suffix[32]{};
        std::snprintf(suffix, sizeof(suffix), "-%05u-of-%05u.gguf", index, total);
        const auto shard = first_shard.parent_path() / (prefix + suffix);
        ec.clear();
        if (!std::filesystem::is_regular_file(shard, ec) || ec) {
            result.error = "Missing GGUF shard: " + shard.string();
            return result;
        }
        ec.clear();
        const auto bytes = std::filesystem::file_size(shard, ec);
        if (ec || bytes == 0) {
            result.error = "GGUF shard is empty or unreadable: " + shard.string();
            return result;
        }
        result.bytes += bytes;
        ++result.shard_count;
    }
    result.complete = result.shard_count == result.expected_shards;
    return result;
}

void append_unique_cache_root(
    std::vector<std::filesystem::path>& roots,
    std::filesystem::path candidate) {
    if (candidate.empty()) return;
    candidate = candidate.lexically_normal();
    const auto already_present = std::ranges::any_of(roots, [&](const auto& existing) {
#ifdef _WIN32
        return lower(existing.string()) == lower(candidate.string());
#else
        return existing == candidate;
#endif
    });
    if (!already_present) roots.push_back(std::move(candidate));
}

[[nodiscard]] std::vector<std::filesystem::path> candidate_model_cache_roots(
    const std::filesystem::path& configured_root) {
    std::vector<std::filesystem::path> roots;
    roots.reserve(12);
    append_unique_cache_root(roots, configured_root);

    if (const auto value = environment_value("LLAMA_CACHE"); !value.empty()) append_unique_cache_root(roots, value);
    if (const auto value = environment_value("HF_HUB_CACHE"); !value.empty()) append_unique_cache_root(roots, value);
    if (const auto value = environment_value("HUGGINGFACE_HUB_CACHE"); !value.empty()) append_unique_cache_root(roots, value);
    if (const auto value = environment_value("HF_HOME"); !value.empty()) append_unique_cache_root(roots, std::filesystem::path{value} / "hub");
    if (const auto value = environment_value("XDG_CACHE_HOME"); !value.empty()) {
        append_unique_cache_root(roots, std::filesystem::path{value} / "huggingface" / "hub");
    }

#ifdef _WIN32
    const auto home = environment_value("USERPROFILE");
    const auto local = environment_value("LOCALAPPDATA");
    if (!home.empty()) {
        append_unique_cache_root(roots, std::filesystem::path{home} / ".cache" / "huggingface" / "hub");
    }
    if (!local.empty()) {
        // Older llama.cpp/Qwen runtime builds have used LOCALAPPDATA-backed
        // caches. Checking their repo-shaped subdirectories is cheap and lets
        // EngCoder adopt existing weights without scanning arbitrary folders.
        append_unique_cache_root(roots, std::filesystem::path{local} / "llama.cpp");
        append_unique_cache_root(roots, std::filesystem::path{local} / "QwenFlashRuntime" / "model-cache");
        append_unique_cache_root(roots, std::filesystem::path{local} / "QwenFlashRuntime" / "cache");
        append_unique_cache_root(roots, std::filesystem::path{local} / "QwenFlashRuntime");
    }
#else
    const auto home = environment_value("HOME");
    if (!home.empty()) {
        append_unique_cache_root(roots, std::filesystem::path{home} / ".cache" / "huggingface" / "hub");
        append_unique_cache_root(roots, std::filesystem::path{home} / ".cache" / "llama.cpp");
        append_unique_cache_root(roots, std::filesystem::path{home} / ".cache" / "qwen-flash-runtime");
    }
#endif

    return roots;
}

[[nodiscard]] std::filesystem::path repo_root_for_cache(
    const std::filesystem::path& cache_root,
    const RuntimeModelProfile& profile) {
    if (cache_root.empty() || profile.repo.empty()) return {};
    const auto slash = profile.repo.find('/');
    std::string folder;
    if (slash == std::string::npos) {
        folder = "models--" + profile.repo;
    } else {
        folder = "models--" + profile.repo.substr(0, slash) + "--" + profile.repo.substr(slash + 1);
    }
    return cache_root / folder;
}

[[nodiscard]] std::filesystem::path first_existing(const std::vector<std::filesystem::path>& candidates) {
    std::error_code ec;
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate, ec) && !ec) {
            return std::filesystem::absolute(candidate, ec).lexically_normal();
        }
        ec.clear();
    }
    return {};
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) {
    if (candidate.empty() || root.empty()) return false;

    std::error_code candidate_ec;
    std::error_code root_ec;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, candidate_ec).lexically_normal();
    const auto canonical_root = std::filesystem::weakly_canonical(root, root_ec).lexically_normal();
    if (candidate_ec || root_ec || canonical_candidate.empty() || canonical_root.empty()) return false;

    auto root_it = canonical_root.begin();
    auto candidate_it = canonical_candidate.begin();
    for (; root_it != canonical_root.end() && candidate_it != canonical_candidate.end(); ++root_it, ++candidate_it) {
#ifdef _WIN32
        if (lower(root_it->string()) != lower(candidate_it->string())) return false;
#else
        if (*root_it != *candidate_it) return false;
#endif
    }
    return root_it == canonical_root.end();
}

[[nodiscard]] std::filesystem::path qfr_proven_server_candidate() {
#ifdef _WIN32
    const auto local = environment_value("LOCALAPPDATA");
    if (local.empty()) return {};
    const auto build = std::filesystem::path{local} / "QwenFlashRuntime" / "backend" /
        ("build-" + std::string{kQwenBackendShortCommit});
    constexpr const char* exe = "llama-server.exe";
#else
    const auto xdg = environment_value("XDG_CACHE_HOME");
    const auto home = environment_value("HOME");
    std::filesystem::path cache_base;
    if (const auto configured = environment_value("QFR_CACHE_ROOT"); !configured.empty()) {
        cache_base = configured;
    } else if (!xdg.empty()) {
        cache_base = std::filesystem::path{xdg} / "qwen-flash-runtime";
    } else if (!home.empty()) {
        cache_base = std::filesystem::path{home} / ".cache" / "qwen-flash-runtime";
    } else {
        return {};
    }
    const auto build = cache_base / "backend" / ("build-" + std::string{kQwenBackendShortCommit});
    constexpr const char* exe = "llama-server";
#endif

    const std::vector<std::filesystem::path> candidates{
        build / "bin" / "Release" / exe,
        build / "bin" / exe,
        build / "bin" / "RelWithDebInfo" / exe,
    };
    return first_existing(candidates);
}

[[nodiscard]] std::optional<std::string> runtime_manifest_recipe(const std::filesystem::path& runtime_root) {
    const auto manifest_path = runtime_root / "runtime.json";
    std::ifstream input(manifest_path);
    if (!input) return std::nullopt;
    try {
        const auto manifest = nlohmann::json::parse(input);
        if (manifest.value("backend_commit", std::string{}) != kQwenBackendCommit) return std::nullopt;
        const auto recipe = manifest.value("build_recipe", std::string{});
        if (recipe.empty()) return std::nullopt;
        return recipe;
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] bool runtime_manifest_matches(const std::filesystem::path& runtime_root) {
    const auto recipe = runtime_manifest_recipe(runtime_root);
    return recipe.has_value() && *recipe == kQwenRuntimeRecipe;
}

[[nodiscard]] bool runtime_manifest_is_known_baseline(const std::filesystem::path& runtime_root) {
    const auto recipe = runtime_manifest_recipe(runtime_root);
    return recipe.has_value() && (*recipe == kQwenRuntimeRecipe || *recipe == kLegacyQfrRecipe);
}

void write_runtime_manifest(const std::filesystem::path& runtime_root, std::string_view source) {
    std::error_code ec;
    std::filesystem::create_directories(runtime_root, ec);
    if (ec) return;
    std::ofstream output(runtime_root / "runtime.json", std::ios::trunc);
    if (!output) return;
    output << nlohmann::json{
        {"runtime", "qwen-flash"},
        {"backend_commit", kQwenBackendCommit},
        {"build_recipe", kQwenRuntimeRecipe},
        {"source", source},
        {"features", nlohmann::json::array({kQ2PaddingFeature})},
    }.dump(2) << '\n';
}

[[nodiscard]] std::string process_failure_detail(const process::Result& result) {
    constexpr std::size_t tail_limit = 32 * 1024;
    auto tail_of = [](const std::string& source) {
        constexpr std::size_t limit = 16 * 1024;
        return source.size() > limit ? source.substr(source.size() - limit) : source;
    };
    std::string tail;
    if (!result.stdout_text.empty()) tail += "[stdout]\n" + tail_of(result.stdout_text);
    if (!result.stderr_text.empty()) {
        if (!tail.empty()) tail += "\n";
        tail += "[stderr]\n" + tail_of(result.stderr_text);
    }
    if (tail.size() > tail_limit) tail.erase(0, tail.size() - tail_limit);
    while (!tail.empty() && (tail.front() == '\r' || tail.front() == '\n')) tail.erase(tail.begin());
    std::string detail = "llama-server exited with code " + std::to_string(result.exit_code);
#ifdef _WIN32
    if (result.exit_code == -1073741819) {
        detail += " (Windows 0xC0000005 access violation)";
    }
#endif
    if (!tail.empty()) detail += "\n" + tail;
    return detail;
}

} // namespace

QwenFlashRuntime::QwenFlashRuntime(const core::Config& config, std::filesystem::path state_root)
    : config_(config), state_root_(std::move(state_root)) {
    std::error_code ec;
    std::filesystem::create_directories(config_.model_cache_root, ec);
    std::filesystem::create_directories(state_root_ / "runtimes" / "qwen-flash", ec);
    adopt_existing_backend();
}

QwenFlashRuntime::~QwenFlashRuntime() {
    stop();
}

void QwenFlashRuntime::adopt_existing_backend() {
#ifdef _WIN32
    constexpr const char* exe = "llama-server.exe";
#else
    constexpr const char* exe = "llama-server";
#endif
    const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
    const auto target = runtime_root / "bin";
    std::error_code ec;
    if (std::filesystem::is_regular_file(target / exe, ec) && !ec) {
        // Keep an existing EngCoder runtime in place. A legacy 0.6.x/QFR build
        // remains usable as a stable no-cache baseline, but is deliberately not
        // relabelled as the patched expert-cache runtime.
        return;
    }

    const auto source_server = qfr_proven_server_candidate();
    if (source_server.empty()) return;

    const auto source = source_server.parent_path();
    const auto staging = runtime_root / "bin.adopt-staging";
    std::filesystem::remove_all(staging, ec);
    ec.clear();
    std::filesystem::create_directories(staging, ec);
    if (ec) return;
    for (const auto& entry : std::filesystem::directory_iterator(source, ec)) {
        if (ec) break;
        const auto destination = staging / entry.path().filename();
        if (entry.is_directory(ec)) {
            std::filesystem::copy(entry.path(), destination,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, ec);
        } else if (entry.is_regular_file(ec)) {
            std::filesystem::copy_file(entry.path(), destination,
                std::filesystem::copy_options::overwrite_existing, ec);
        }
        if (ec) break;
    }
    if (ec || !std::filesystem::is_regular_file(staging / exe, ec)) {
        std::filesystem::remove_all(staging, ec);
        return;
    }
    std::filesystem::create_directories(runtime_root, ec);
    std::filesystem::rename(staging, target, ec);
    if (ec) {
        std::filesystem::remove_all(staging, ec);
        return;
    }

    // Record provenance honestly. This binary is a compatibility baseline, not
    // the patched EngCoder cache runtime.
    std::ofstream output(runtime_root / "runtime.json", std::ios::trunc);
    if (output) {
        output << nlohmann::json{
            {"runtime", "qwen-flash"},
            {"backend_commit", kQwenBackendCommit},
            {"build_recipe", kLegacyQfrRecipe},
            {"source", "adopted-qfr-baseline"},
            {"features", nlohmann::json::array()},
        }.dump(2) << '\n';
    }
}

void QwenFlashRuntime::set_last_error(std::string message) {
    std::scoped_lock lock(error_mutex_);
    last_error_ = std::move(message);
}

std::string QwenFlashRuntime::last_error() const {
    std::scoped_lock lock(error_mutex_);
    return last_error_;
}

std::filesystem::path QwenFlashRuntime::discover_server() const {
#ifdef _WIN32
    constexpr const char* exe = "llama-server.exe";
#else
    constexpr const char* exe = "llama-server";
#endif

    std::vector<std::filesystem::path> candidates;
    if (!config_.qwen_flash_server.empty()) candidates.push_back(config_.qwen_flash_server);

    const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
    const auto engcoder_server = runtime_root / "bin" / exe;

    // Prefer an EngCoder-owned copy when its manifest proves it is either the
    // current patched backend or the exact pinned QFR compatibility baseline.
    // This keeps runtime ownership inside EngCoder instead of executing the
    // original %LOCALAPPDATA% QwenFlashRuntime binary after adoption.
    if (runtime_manifest_is_known_baseline(runtime_root)) candidates.push_back(engcoder_server);

    // The standalone QFR build remains a known-good compatibility anchor when
    // no manifested EngCoder-owned copy is available.
    if (auto qfr = qfr_proven_server_candidate(); !qfr.empty()) candidates.push_back(std::move(qfr));

    // Compatibility fallback for a pre-0.6.3/unmanifested EngCoder runtime.
    // A manifested runtime installer/adoption supersedes this path.
    candidates.push_back(engcoder_server);
    candidates.push_back(config_.workspace / "runtime" / "qwen-flash" / exe);
    candidates.push_back(config_.workspace / "backend" / exe);

    if (auto result = first_existing(candidates); !result.empty()) return result;

    // Build output discovery is deliberately bounded to EngCoder's own build
    // tree. This permits source builds to stage the native runtime without
    // coupling the model manager to a particular CMake generator name.
    const auto build_root = config_.workspace / "out" / "build";
    std::error_code ec;
    if (std::filesystem::is_directory(build_root, ec) && !ec) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 build_root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (entry.is_regular_file(ec) && !ec && entry.path().filename() == exe) return entry.path();
            ec.clear();
        }
    }
    return {};
}

bool QwenFlashRuntime::runtime_verified() const {
    if (!config_.qwen_flash_server.empty()) {
        std::error_code ec;
        return std::filesystem::is_regular_file(config_.qwen_flash_server, ec) && !ec;
    }
    const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
    return runtime_manifest_matches(runtime_root);
}

QwenFlashRuntime::RuntimeCapabilities QwenFlashRuntime::capabilities_for_server(
    const std::filesystem::path& server) const {
    RuntimeCapabilities result;
    const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
    const bool under_engcoder_runtime = path_is_within(server, runtime_root);

    if (!under_engcoder_runtime) {
        result.recipe = "external-or-qfr-baseline";
        return result;
    }

    std::ifstream input(runtime_root / "runtime.json");
    if (!input) {
        result.recipe = "legacy-unmanifested";
        return result;
    }
    try {
        const auto manifest = nlohmann::json::parse(input);
        result.recipe = manifest.value("build_recipe", std::string{"legacy"});
        const auto features = manifest.value("features", nlohmann::json::array());
        if (features.is_array()) {
            for (const auto& item : features) {
                if (item.is_string() && item.get<std::string>() == kQ2PaddingFeature) {
                    result.qwen38_q2_padded_down = true;
                }
            }
        }
        result.expert_cache_candidate = result.qwen38_q2_padded_down && result.recipe == kQwenRuntimeRecipe;
    } catch (...) {
        result.recipe = "invalid-manifest";
    }
    return result;
}

std::optional<bool> QwenFlashRuntime::expert_cache_qualification(
    const RuntimeModelProfile& profile,
    const RuntimeCapabilities& capabilities) const {
    if (!capabilities.expert_cache_candidate || capabilities.recipe.empty()) return false;
    const auto path = state_root_ / "runtimes" / "qwen-flash" / "qualifications.json";
    std::ifstream input(path);
    if (!input) return std::nullopt;
    try {
        const auto root = nlohmann::json::parse(input);
        const auto key = profile.id + "@" + capabilities.recipe;
        if (!root.contains(key) || !root.at(key).is_object()) return std::nullopt;
        const auto& record = root.at(key);
        if (!record.contains("expert_cache_supported") || !record.at("expert_cache_supported").is_boolean()) {
            return std::nullopt;
        }
        return record.at("expert_cache_supported").get<bool>();
    } catch (...) {
        return std::nullopt;
    }
}

void QwenFlashRuntime::record_expert_cache_qualification(
    const RuntimeModelProfile& profile,
    const RuntimeCapabilities& capabilities,
    bool supported,
    std::string_view reason) const {
    if (!capabilities.expert_cache_candidate || capabilities.recipe.empty()) return;
    const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
    std::error_code ec;
    std::filesystem::create_directories(runtime_root, ec);
    if (ec) return;
    const auto path = runtime_root / "qualifications.json";
    nlohmann::json root = nlohmann::json::object();
    {
        std::ifstream input(path);
        if (input) {
            try { root = nlohmann::json::parse(input); }
            catch (...) { root = nlohmann::json::object(); }
        }
    }
    const auto key = profile.id + "@" + capabilities.recipe;
    root[key] = {
        {"model", profile.id},
        {"quant", profile.quant},
        {"runtime_recipe", capabilities.recipe},
        {"expert_cache_supported", supported},
        {"reason", std::string{reason}},
    };
    const auto temp = path.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << root.dump(2) << '\n';
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temp, path, ec);
        if (ec) std::filesystem::remove(temp, ec);
    }
}

QwenFlashRuntime::HardwareSnapshot QwenFlashRuntime::probe_hardware() const {
    HardwareSnapshot hardware;
    hardware.logical_threads = std::max(1U, std::thread::hardware_concurrency());

#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        hardware.ram_gib = static_cast<double>(memory.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    }
#else
    struct sysinfo info {};
    if (::sysinfo(&info) == 0) {
        const long double bytes = static_cast<long double>(info.totalram) * static_cast<long double>(info.mem_unit);
        hardware.ram_gib = static_cast<double>(bytes / (1024.0L * 1024.0L * 1024.0L));
    }
#endif

    process::Request request;
#ifdef _WIN32
    const auto system_root = environment_value("SystemRoot");
    const auto system_smi = system_root.empty()
        ? std::filesystem::path{}
        : std::filesystem::path{system_root} / "System32" / "nvidia-smi.exe";
    request.executable = std::filesystem::is_regular_file(system_smi) ? system_smi.string() : "nvidia-smi.exe";
#else
    request.executable = "nvidia-smi";
#endif
    request.arguments = {"--query-gpu=name,memory.total", "--format=csv,noheader,nounits"};
    request.timeout = std::chrono::seconds{2};
    request.max_output_bytes = 4096;
    if (auto result = process::run(request); result && result->exit_code == 0) {
        std::istringstream input(result->stdout_text);
        std::string line;
        if (std::getline(input, line)) {
            const auto comma = line.rfind(',');
            if (comma != std::string::npos) {
                hardware.gpu_name = line.substr(0, comma);
                while (!hardware.gpu_name.empty() && std::isspace(static_cast<unsigned char>(hardware.gpu_name.back()))) {
                    hardware.gpu_name.pop_back();
                }
                const auto vram_mib_text = line.substr(comma + 1);
                try {
                    hardware.vram_mib = std::stoull(vram_mib_text);
                } catch (...) {
                    hardware.vram_mib = 0;
                }
            }
        }
    }
    if (hardware.vram_mib == 0) {
        hardware.vram_mib = static_cast<std::uint64_t>(std::max(0.5, config_.vram_budget_gib) * 1024.0);
    }
    return hardware;
}

RuntimeModelProfile QwenFlashRuntime::resolve_profile(
    const RuntimeModelProfile& profile,
    const HardwareSnapshot& hardware,
    const RuntimeCapabilities& capabilities) const {
    RuntimeModelProfile resolved = profile;

    const auto quant = lower(profile.quant);
    const bool q2_layout = quant.find("q2") != std::string::npos;
    const auto qualify_expert_cache = [&] {
        if (resolved.expert_cache_mode == "off") {
            resolved.expert_cache_mib = 0;
            resolved.pinned_host_mib = 0;
            resolved.early_router = false;
            return;
        }
        if (resolved.expert_cache_mode == "auto" && q2_layout) {
            const auto qualification = expert_cache_qualification(profile, capabilities);
            // Legacy runtimes never enter the experimental cache path. A patched
            // runtime is allowed one probe; after that the per-model/runtime
            // result is persisted and reused like a Bionic-style automatic
            // compatibility decision.
            if (!capabilities.expert_cache_candidate || (qualification && !*qualification)) {
                resolved.expert_cache_mib = 0;
                resolved.pinned_host_mib = 0;
                resolved.early_router = false;
            }
        }
    };

    if (profile.runtime_policy == "recommended") {
        // Reproduce the proven standalone QwenFlashRuntime policy. Keep model
        // identity/context choices, but do not add new memory-policy switches
        // that were not present in the working QFR launch.
        resolved.threads = 12;
        resolved.batch_threads = 12;
        resolved.batch_size = 1024;
        resolved.ubatch_size = 256;
        // EngCoder's GPU percentage is an explicit layer-offload target, not
        // a decorative profile value. Honor it even in Recommended mode.
        resolved.gpu_layers = effective_gpu_layers(resolved);
        resolved.flash_attention = true;
        resolved.kv_type_k = "q4_0";
        resolved.kv_type_v = "q4_0";
        resolved.kv_gpu_layers = 0;
        resolved.kv_offload = true; // backend default; intentionally not forced on the command line
        resolved.kv_cpu_pinned = true;
        resolved.phase_aware_workspace = true;
        resolved.load_mode = "mmap";
        resolved.lazy_mode = "on";
        resolved.tensor_override = "per_layer_token_embd.weight=CPU";
        resolved.expert_cache_mib = 6144;
        resolved.pinned_host_mib = 12288;
        resolved.early_router = false;
        qualify_expert_cache();
        return resolved;
    }

    if (profile.runtime_policy == "automatic") {
        const auto logical = std::max(1U, hardware.logical_threads);
        resolved.threads = std::clamp(logical / 2U, 4U, 16U);
        resolved.batch_threads = resolved.threads;
        const bool roomy = hardware.ram_gib >= 48.0 && hardware.vram_mib >= 12U * 1024U;
        resolved.batch_size = roomy ? 1024U : 512U;
        resolved.ubatch_size = roomy ? 256U : 128U;
        // Automatic mode owns batching/cache choices, but the user GPU target
        // remains a cap on model-layer residency.
        resolved.gpu_layers = effective_gpu_layers(resolved);
        resolved.flash_attention = true;
        resolved.kv_type_k = "q4_0";
        resolved.kv_type_v = "q4_0";
        resolved.kv_gpu_layers = 0;
        resolved.kv_offload = !(effective_context_tokens(resolved) >= 65'536U && hardware.vram_mib <= 24U * 1024U);
        resolved.kv_cpu_pinned = true;
        resolved.phase_aware_workspace = true;
        resolved.load_mode = "mmap";
        resolved.lazy_mode = "on";
        resolved.tensor_override = "per_layer_token_embd.weight=CPU";
        const auto auto_expert = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(
            hardware.vram_mib * 3ULL / 8ULL, 2048ULL, 6144ULL));
        resolved.expert_cache_mib = auto_expert;
        const auto host_target = hardware.ram_gib > 0.0
            ? static_cast<std::uint32_t>(std::clamp(hardware.ram_gib * 192.0, 4096.0, 12288.0))
            : 8192U;
        resolved.pinned_host_mib = host_target;
        resolved.prompt_cache_mib = hardware.ram_gib >= 48.0 ? 1024U : 512U;
        resolved.context_shift = false;
        resolved.reasoning_preserve = false;
        qualify_expert_cache();
        return resolved;
    }

    // Manual mode: "auto" in the exact-layer field means use the percentage
    // slider against the model's layer-count hint. A literal number or "all"
    // remains an exact llama.cpp override.
    resolved.gpu_layers = effective_gpu_layers(resolved);
    qualify_expert_cache();
    return resolved;
}

RuntimeModelProfile QwenFlashRuntime::conservative_profile(
    const RuntimeModelProfile& profile,
    const HardwareSnapshot& hardware,
    const RuntimeCapabilities& capabilities) const {
    RuntimeModelProfile fallback = profile;
    fallback.runtime_policy = "automatic";
    fallback.context_tokens = std::min<std::uint32_t>(profile.context_tokens, 77'824U);
    fallback.parallel_slots = 1U;
    fallback.context_reserve_tokens = std::min<std::uint32_t>(profile.context_reserve_tokens, 4'096U);
    fallback = resolve_profile(fallback, hardware, capabilities);
    fallback.batch_size = std::min<std::uint32_t>(fallback.batch_size, 512U);
    fallback.ubatch_size = std::min<std::uint32_t>(fallback.ubatch_size, 128U);
    fallback.expert_cache_mib = std::min<std::uint32_t>(fallback.expert_cache_mib, 4096U);
    fallback.pinned_host_mib = std::min<std::uint32_t>(fallback.pinned_host_mib, 8192U);
    fallback.prompt_cache_mib = 0U;
    fallback.kv_offload = false;
    fallback.kv_gpu_layers = 0U;
    fallback.kv_cpu_pinned = true;
    fallback.context_shift = false;
    fallback.reasoning_preserve = false;
    return fallback;
}

std::vector<std::string> QwenFlashRuntime::build_arguments(
    const RuntimeModelProfile& profile,
    bool minimal_compatibility) const {
    std::vector<std::string> args;
    auto add = [&](std::string key, std::string value) {
        args.push_back(std::move(key));
        args.push_back(std::move(value));
    };

    if (profile.source == ModelSourceKind::local_gguf && !profile.local_path.empty()) {
        add("-m", profile.local_path.string());
    } else if (const auto cached = cached_model_path(profile); !cached.empty()) {
        // Prefer an already-complete model from any recognized shared/legacy
        // cache. Launching the first shard directly prevents llama.cpp from
        // initiating a second download merely because EngCoder's configured
        // cache root differs from the runtime that originally fetched it.
        add("-m", cached.string());
    } else {
        auto spec = profile.repo;
        if (!profile.quant.empty()) spec += ":" + profile.quant;
        add("-hf", std::move(spec));
    }

    add("--host", "127.0.0.1");
    add("--port", std::to_string(config_.qwen_flash_port));
    add("-np", std::to_string(std::max<std::uint32_t>(1, profile.parallel_slots)));
    add("-c", std::to_string(profile.context_tokens));

    // Last-resort compatibility launch: preserve EngCoder's requested context
    // and ownership, but otherwise stay as close as possible to the bare
    // llama-server -hf launch that is proven to load this model. This is a
    // runtime isolation mode, not a separate provider: Architect/System 1,
    // tools, memory, training and compaction still run above the same native
    // Qwen endpoint.
    if (minimal_compatibility) {
        // Keep the user's configured GPU-offload ceiling even when omitting
        // advanced runtime switches. Otherwise a recovery retry could silently
        // start a much larger GPU load than the requested percentage.
        add("-ngl", profile.gpu_layers.empty() ? "auto" : profile.gpu_layers);
        return args;
    }

    add("-t", std::to_string(profile.threads));
    add("-tb", std::to_string(profile.batch_threads));
    add("-b", std::to_string(profile.batch_size));
    add("-ub", std::to_string(profile.ubatch_size));
    add("-ngl", profile.gpu_layers.empty() ? "auto" : profile.gpu_layers);
    add("-fa", profile.flash_attention ? "on" : "off");
    add("-ctk", profile.kv_type_k.empty() ? "q4_0" : profile.kv_type_k);
    add("-ctv", profile.kv_type_v.empty() ? "q4_0" : profile.kv_type_v);
    add("--kv-gpu-layers", std::to_string(profile.kv_gpu_layers));
    args.emplace_back(profile.kv_cpu_pinned ? "--kv-cpu-pinned" : "--no-kv-cpu-pinned");
    args.emplace_back(profile.phase_aware_workspace ? "--phase-aware-workspace" : "--no-phase-aware-workspace");
    add("-lm", profile.load_mode.empty() ? "mmap" : profile.load_mode);
    add("-lzm", profile.lazy_mode.empty() ? "on" : profile.lazy_mode);
    if (!profile.tensor_override.empty()) add("-ot", profile.tensor_override);
    if (profile.expert_cache_mib > 0) {
        add("--moe-expert-cache-mib", std::to_string(profile.expert_cache_mib));
        if (profile.pinned_host_mib > 0) {
            add("--moe-expert-cache-host-pinned-mb", std::to_string(profile.pinned_host_mib));
        }
        if (profile.early_router) args.emplace_back("--moe-early-router");
    }

    // Recommended intentionally mirrors the original proven QFR command. The
    // extra switches below are available in Automatic/Manual mode only.
    if (profile.runtime_policy != "recommended") {
        args.emplace_back(profile.kv_offload ? "--kv-offload" : "--no-kv-offload");
        add("--cache-ram", std::to_string(profile.prompt_cache_mib));
        args.emplace_back(profile.context_shift ? "--context-shift" : "--no-context-shift");
        args.emplace_back(profile.reasoning_preserve ? "--reasoning-preserve" : "--no-reasoning-preserve");
    }

    if (profile.speculative_enabled) {
        if (!profile.speculative_model.empty()) {
            add("--spec-draft-model", profile.speculative_model.string());
        } else if (!profile.speculative_repo.empty()) {
            add("--spec-draft-hf", profile.speculative_repo);
        }
        add("--spec-type", profile.speculative_type.empty() ? "draft-mtp" : profile.speculative_type);
        add("--spec-draft-n-max", std::to_string(std::max<std::uint32_t>(1, profile.speculative_tokens)));
    }
    return args;
}

std::string QwenFlashRuntime::command_string(
    const std::filesystem::path& server,
    const std::vector<std::string>& args) const {
    auto quote = [](std::string_view value) {
        if (value.find_first_of(" \t\"") == std::string_view::npos) return std::string{value};
        std::string out = "\"";
        for (const char ch : value) {
            if (ch == '\"') out += '\\';
            out += ch;
        }
        out += '\"';
        return out;
    };
    std::string command = quote(server.string());
    for (const auto& arg : args) {
        command += ' ';
        command += quote(arg);
    }
    return command;
}

std::expected<void, core::Error> QwenFlashRuntime::wait_until_ready(
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stop) {
    if (deadline == std::chrono::steady_clock::time_point::max()) {
        deadline = std::chrono::steady_clock::now() + std::chrono::hours{4};
    }

    for (;;) {
        if (stop.stop_requested()) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::cancelled),
                "Qwen Flash model load was cancelled",
            });
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_timeout),
                "Qwen Flash model loading exceeded the inference deadline",
            });
        }
        if (worker_exited_.load(std::memory_order_acquire)) {
            const auto detail = last_error();
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                detail.empty() ? "Qwen Flash runtime exited before becoming ready" : detail,
            });
        }

        httplib::Client client("127.0.0.1", config_.qwen_flash_port);
        client.set_connection_timeout(0, 250000);
        client.set_read_timeout(0, 500000);
        if (auto result = client.Get("/health"); result && result->status >= 200 && result->status < 300) {
            return {};
        }

        const auto now = std::chrono::steady_clock::now();
        const auto remaining = deadline - now;
        if (remaining <= std::chrono::milliseconds{0}) continue;
        std::this_thread::sleep_for(std::min(std::chrono::milliseconds{500},
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining)));
    }
}

std::expected<std::string, core::Error> QwenFlashRuntime::ensure_started(
    const RuntimeModelProfile& profile,
    std::chrono::steady_clock::time_point deadline,
    std::stop_token stop) {
    std::unique_lock lock(mutex_);
    if (stop.stop_requested()) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::cancelled), "Qwen Flash model load was cancelled"});
    }
    if (deadline != std::chrono::steady_clock::time_point::max() && std::chrono::steady_clock::now() >= deadline) {
        return std::unexpected(core::Error{core::make_error_code(core::Errc::model_timeout), "Qwen Flash model loading exceeded the inference deadline"});
    }
    if (!config_.qwen_flash_enabled) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Qwen Flash native runtime is disabled",
        });
    }

    bool waited_for_startup = false;
    while (startup_in_progress_) {
        waited_for_startup = true;
        if (stop.stop_requested()) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::cancelled),
                "Qwen Flash model load was cancelled while waiting for the active startup",
            });
        }
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_timeout),
                "Qwen Flash model loading exceeded the inference deadline while waiting for the active startup",
            });
        }
        startup_cv_.wait_for(lock, std::chrono::milliseconds{100});
    }

    if (running() && active_model_id_ == profile.id) return url();
    if (waited_for_startup && last_startup_model_id_ == profile.id && last_startup_error_.has_value()) {
        return std::unexpected(*last_startup_error_);
    }

    startup_in_progress_ = true;
    startup_model_id_ = profile.id;
    last_startup_model_id_.clear();
    last_startup_error_.reset();
    auto finish_startup = [&](std::optional<core::Error> error = std::nullopt) {
        last_startup_model_id_ = profile.id;
        last_startup_error_ = std::move(error);
        startup_in_progress_ = false;
        startup_model_id_.clear();
        startup_cv_.notify_all();
    };
    auto fail_startup = [&](core::Error error) -> std::expected<std::string, core::Error> {
        finish_startup(error);
        return std::unexpected(std::move(error));
    };
    ScopeExit startup_guard([&] {
        if (!startup_in_progress_ || startup_model_id_ != profile.id) return;
        if (!lock.owns_lock()) lock.lock();
        finish_startup(core::Error{
            core::make_error_code(core::Errc::model_loading_failed),
            "Qwen Flash startup aborted unexpectedly",
        });
    });

    // Reject mechanically-invalid speculation before touching a currently
    // healthy runtime. A bad edited profile must not turn a validation error
    // into downtime merely because the caller reached ensure_started directly.
    if (profile.speculative_enabled) {
        // A draft must be an independent local GGUF. Reject before stopping the
        // healthy backend; no cross-server/LM Studio MTP is supported.
        std::error_code draft_ec;
        if (profile.speculative_type != "draft-mtp" || !profile.speculative_repo.empty() ||
            profile.speculative_model.empty() ||
            !std::filesystem::is_regular_file(profile.speculative_model, draft_ec) || draft_ec) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "MTP requires a readable dedicated local Qwen draft GGUF; LM Studio is not an MTP backend",
            });
        }
        std::ifstream draft_file(profile.speculative_model, std::ios::binary);
        char magic[4]{};
        if (!draft_file.read(magic, 4) || std::string_view(magic, 4) != "GGUF") {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "MTP draft is not a valid GGUF header",
            });
        }
        if (!profile.local_path.empty()) {
            const auto draft = std::filesystem::weakly_canonical(profile.speculative_model, draft_ec);
            draft_ec.clear();
            const auto main = std::filesystem::weakly_canonical(profile.local_path, draft_ec);
            if (!draft_ec && draft == main) {
                return fail_startup(core::Error{
                    core::make_error_code(core::Errc::invalid_argument),
                    "Main GGUF cannot be its own draft model",
                });
            }
        }
        if (profile.speculative_model.empty() && profile.speculative_repo.empty()) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "Speculative/MTP decoding is enabled but no dedicated draft model is configured",
            });
        }
        if (profile.speculative_model.empty() && !profile.speculative_repo.empty() &&
            profile.speculative_repo == profile.repo) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::invalid_argument),
                "Speculative/MTP decoding cannot reuse the main Qwen repository as its draft model. "
                "Select a dedicated MTP/draft GGUF sidecar or disable Speculative / MTP.",
            });
        }
    }

    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }

    if (profile.source == ModelSourceKind::hugging_face) {
        const auto cache = cache_status(profile);
        const auto matching_files = cache.value("matching_gguf_files", std::size_t{});
        const bool split_complete = cache.value("split_complete", false);
        const bool size_plausible = cache.value("size_plausible", false);
        if (matching_files > 0 && split_complete && !size_plausible) {
            const auto cached = cache.value("cached_bytes", std::uint64_t{});
            const auto expected = cache.value("expected_bytes", profile.estimated_bytes);
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                "Cached Qwen model looks incomplete or truncated: " +
                    std::to_string(cached / (1024ULL * 1024ULL * 1024ULL)) + " GiB present, about " +
                    std::to_string(expected / (1024ULL * 1024ULL * 1024ULL)) +
                    " GiB expected. Finish copying/downloading every GGUF shard before loading; EngCoder will not treat filenames alone as a complete model.",
            });
        }
    }

    const auto server = discover_server();
    if (server.empty()) {
        return fail_startup(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Qwen Flash llama-server backend was not found. Build/stage the EngCoder qwen-flash runtime or set ENGCODER_QWEN_FLASH_SERVER.",
        });
    }

    if (profile.source == ModelSourceKind::local_gguf) {
        const auto inspection = inspect_local_gguf(profile.local_path);
        if (!inspection.complete) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                inspection.error.empty() ? "Local GGUF shard set is incomplete" : inspection.error,
            });
        }
        if (!model_size_is_plausible(profile, inspection.bytes)) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::model_loading_failed),
                "Local GGUF override is incomplete/truncated: " +
                    std::to_string(inspection.bytes / (1024ULL * 1024ULL * 1024ULL)) +
                    " GiB present, about " +
                    std::to_string(profile.estimated_bytes / (1024ULL * 1024ULL * 1024ULL)) +
                    " GiB expected for " + profile.display_name,
            });
        }
    }

    // Never silently attach to an unrelated process. EngCoder owns the native
    // runtime lifecycle, so a pre-existing healthy listener on its reserved
    // port is a configuration conflict rather than a provider to reuse.
    {
        httplib::Client existing("127.0.0.1", config_.qwen_flash_port);
        existing.set_connection_timeout(0, 150000);
        existing.set_read_timeout(0, 150000);
        if (auto result = existing.Get("/health"); result) {
            return fail_startup(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Qwen Flash runtime port " + std::to_string(config_.qwen_flash_port) +
                    " is already in use; stop the other server or change ENGCODER_QWEN_FLASH_PORT",
            });
        }
    }

    const auto hardware = probe_hardware();
    const auto capabilities = capabilities_for_server(server);
    last_hardware_ = {
        {"ram_gib", hardware.ram_gib},
        {"vram_mib", hardware.vram_mib},
        {"logical_threads", hardware.logical_threads},
        {"gpu_name", hardware.gpu_name.empty() ? nlohmann::json(nullptr) : nlohmann::json(hardware.gpu_name)},
        {"runtime_recipe", capabilities.recipe},
        {"qwen38_q2_padded_down", capabilities.qwen38_q2_padded_down},
        {"expert_cache_candidate", capabilities.expert_cache_candidate},
        {"expert_cache_qualified", expert_cache_qualification(profile, capabilities).value_or(false)},
    };

    auto launch_attempt = [&](const RuntimeModelProfile& candidate, bool safety_fallback, bool minimal_compatibility = false)
        -> std::expected<void, core::Error> {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }

        active_model_id_ = profile.id;
        active_context_tokens_ = candidate.context_tokens;
        active_parallel_slots_ = std::max<std::uint32_t>(1, candidate.parallel_slots);
        active_safety_fallback_ = safety_fallback;
        startup_note_.clear();
        set_last_error({});
        worker_exited_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);

        const auto args = build_arguments(candidate, minimal_compatibility);
        const auto cache = cache_status(candidate);
        const auto launch_cache_root = cache.value("cache_root", config_.model_cache_root.string());
        const auto command = command_string(server, args);
        last_command_ = command;
        last_effective_profile_ = to_json(candidate);
        last_effective_profile_["launch_profile"] = minimal_compatibility ? "minimal-compatibility" : "full";

        worker_ = std::jthread([this, server, args, command, launch_cache_root](std::stop_token stop) {
            process::Request request;
            request.executable = server.string();
            request.arguments = args;
            request.working_directory = server.parent_path();
            request.environment.emplace_back("LLAMA_CACHE", launch_cache_root);
            request.timeout = std::chrono::hours{24 * 365};
            request.max_output_bytes = 32 * 1024 * 1024;
            auto result = process::run(request, stop);
            if (!result) {
                set_last_error(result.error().message + "\nLaunch: " + command);
            } else if (!result->cancelled && result->exit_code != 0) {
                set_last_error(process_failure_detail(*result) + "\nLaunch: " + command);
            }
            running_.store(false, std::memory_order_release);
            worker_exited_.store(true, std::memory_order_release);
        });

        // Loading can include a large first-time mmap/page-in. Do not hold the
        // lifecycle mutex while polling readiness: unload/cancel must remain
        // able to request stop and terminate the child process.
        lock.unlock();
        auto ready = wait_until_ready(deadline, stop);
        lock.lock();

        if (!ready) {
            if (worker_.joinable()) {
                worker_.request_stop();
                worker_.join();
            }
            running_.store(false, std::memory_order_release);
            if (active_model_id_ != profile.id) {
                return std::unexpected(core::Error{
                    core::make_error_code(core::Errc::cancelled),
                    "Qwen Flash model load was cancelled",
                });
            }
            return std::unexpected(ready.error());
        }

        if (active_model_id_ != profile.id || worker_exited_.load(std::memory_order_acquire)) {
            return std::unexpected(core::Error{
                core::make_error_code(core::Errc::model_unavailable),
                "Qwen Flash model load was cancelled",
            });
        }

        running_.store(true, std::memory_order_release);
        if (safety_fallback) {
            startup_note_ = "Requested runtime policy could not initialize; EngCoder recovered with its hardware-aware safety profile (" +
                std::to_string(candidate.context_tokens) + " context / " +
                std::to_string(candidate.parallel_slots) + " slot).";
        } else if (candidate.runtime_policy == "automatic") {
            startup_note_ = "Automatic hardware tuning active.";
        } else if (candidate.runtime_policy == "recommended") {
            startup_note_ = candidate.expert_cache_mib > 0
                ? "Recommended Qwen Flash policy active with qualified MoE expert cache."
                : "Recommended Qwen Flash stable baseline active; expert cache is disabled until this runtime/model layout is qualified.";
        }
        return {};
    };

    const auto first_candidate = resolve_profile(profile, hardware, capabilities);
    auto first = launch_attempt(first_candidate, false);
    if (first) {
        if (profile.expert_cache_mode == "auto" && first_candidate.expert_cache_mib > 0) {
            record_expert_cache_qualification(profile, capabilities, true, "cache-enabled model initialization reached /health");
        }
        finish_startup();
        return "http://127.0.0.1:" + std::to_string(config_.qwen_flash_port);
    }

    const auto first_error = first.error();
    if (first_error.code == core::make_error_code(core::Errc::cancelled) ||
        first_error.code == core::make_error_code(core::Errc::model_timeout) ||
        profile.runtime_policy == "manual") {
        active_model_id_.clear();
        active_context_tokens_ = 0;
        active_parallel_slots_ = 0;
        active_safety_fallback_ = false;
        return fail_startup(first_error);
    }

    struct RecoveryAttempt {
        std::string label;
        RuntimeModelProfile profile;
        bool minimal_compatibility{false};
    };
    std::vector<RecoveryAttempt> recovery_profiles;

    auto add_recovery = [&](std::string label, RuntimeModelProfile candidate, bool minimal_compatibility = false) {
        const auto same_shape = [&](const RecoveryAttempt& existing) {
            return existing.profile.context_tokens == candidate.context_tokens &&
                   existing.profile.runtime_policy == candidate.runtime_policy &&
                   existing.profile.batch_size == candidate.batch_size &&
                   existing.profile.ubatch_size == candidate.ubatch_size &&
                   existing.profile.expert_cache_mib == candidate.expert_cache_mib &&
                   existing.profile.pinned_host_mib == candidate.pinned_host_mib &&
                   existing.minimal_compatibility == minimal_compatibility;
        };
        if (candidate.context_tokens == first_candidate.context_tokens &&
            candidate.runtime_policy == first_candidate.runtime_policy &&
            candidate.batch_size == first_candidate.batch_size &&
            candidate.ubatch_size == first_candidate.ubatch_size &&
            candidate.expert_cache_mib == first_candidate.expert_cache_mib &&
            candidate.pinned_host_mib == first_candidate.pinned_host_mib &&
            !minimal_compatibility) {
            return;
        }
        if (std::ranges::none_of(recovery_profiles, same_shape)) {
            recovery_profiles.push_back({std::move(label), std::move(candidate), minimal_compatibility});
        }
    };

    // The expert cache is an optimization, not a requirement for Qwen Flash.
    // If a cache-enabled attempt crashes during model initialization, retry the
    // exact same context/offload policy with the cache disabled before touching
    // context size. This keeps the EngCoder harness online and isolates backend
    // compatibility from agent policy.
    if (first_candidate.expert_cache_mib > 0 && profile.expert_cache_mode != "on") {
        auto no_cache = first_candidate;
        no_cache.expert_cache_mode = "off";
        no_cache.expert_cache_mib = 0;
        no_cache.pinned_host_mib = 0;
        no_cache.early_router = false;
        add_recovery("same-context stable baseline (expert cache off)", std::move(no_cache));
    }

    // If another advanced llama.cpp switch is incompatible with this exact
    // build/model pair, isolate it before sacrificing context. This keeps the
    // requested 80K-class window while reducing the backend command to the
    // proven Hugging Face loader + EngCoder-owned host/port/context controls.
    if (profile.expert_cache_mode != "on") {
        auto minimal = first_candidate;
        minimal.expert_cache_mode = "off";
        minimal.expert_cache_mib = 0;
        minimal.pinned_host_mib = 0;
        minimal.early_router = false;
        add_recovery("same-context minimal llama.cpp compatibility", std::move(minimal), true);
    }

    // First recovery stays above the user's ~75K hard-context target while
    // materially reducing startup pressure. Unlike 0.6.2, this actually lowers
    // context instead of retrying 81,920 with only different cache knobs.
    auto safe_76k = conservative_profile(profile, hardware, capabilities);
    safe_76k.context_tokens = std::min<std::uint32_t>(profile.context_tokens, 77'824U);
    add_recovery("76K hardware-aware", std::move(safe_76k));

    // If the 76K-class profile still cannot initialize, reduce only far enough
    // to distinguish context pressure from a backend/runtime defect.
    auto safe_64k = conservative_profile(profile, hardware, capabilities);
    safe_64k.context_tokens = std::min<std::uint32_t>(profile.context_tokens, 65'536U);
    safe_64k.context_reserve_tokens = std::min<std::uint32_t>(safe_64k.context_reserve_tokens, 4'096U);
    add_recovery("64K diagnostic", std::move(safe_64k));

    // Final compatibility anchor: this mirrors the original QFR policy at the
    // 32K context that was already proven end-to-end on the target machine.
    // If this fails too, the problem is not the new large-context policy.
    auto proven_32k = profile;
    proven_32k.runtime_policy = "recommended";
    proven_32k.context_tokens = std::min<std::uint32_t>(profile.context_tokens, 32'768U);
    proven_32k.parallel_slots = 1U;
    proven_32k.context_reserve_tokens = std::min<std::uint32_t>(proven_32k.context_reserve_tokens, 4'096U);
    proven_32k = resolve_profile(proven_32k, hardware, capabilities);
    add_recovery("32K proven-QFR compatibility", std::move(proven_32k));

    std::vector<std::pair<std::string, core::Error>> recovery_errors;
    for (const auto& recovery : recovery_profiles) {
        auto attempt = launch_attempt(recovery.profile, true, recovery.minimal_compatibility);
        if (attempt) {
            if (profile.expert_cache_mode == "auto" && first_candidate.expert_cache_mib > 0 &&
                    recovery.label == "same-context stable baseline (expert cache off)") {
                record_expert_cache_qualification(profile, capabilities, false,
                    "cache-enabled initialization failed; same-context cache-off initialization reached /health");
            }
            startup_note_ = "Recovered with " + recovery.label + " profile (" +
                std::to_string(recovery.profile.context_tokens) + " context / " +
                std::to_string(recovery.profile.parallel_slots) + " slot).";
            finish_startup();
            return "http://127.0.0.1:" + std::to_string(config_.qwen_flash_port);
        }
        if (attempt.error().code == core::make_error_code(core::Errc::cancelled) ||
            attempt.error().code == core::make_error_code(core::Errc::model_timeout)) {
            active_model_id_.clear();
            active_context_tokens_ = 0;
            active_parallel_slots_ = 0;
            active_safety_fallback_ = false;
            return fail_startup(attempt.error());
        }
        recovery_errors.emplace_back(recovery.label, attempt.error());
    }

    active_model_id_.clear();
    active_context_tokens_ = 0;
    active_parallel_slots_ = 0;
    active_safety_fallback_ = false;
    std::string combined =
        "Qwen Flash startup failed for the requested policy and every recovery profile.\n"
        "Requested policy: " + first_error.message;
    if (capabilities.recipe == "external-or-qfr-baseline") {
        combined += "\nBackend note: EngCoder is using the legacy standalone QwenFlashRuntime backend, not a verified EngCoder Qwen Runtime Backend. If the model cache is complete, install/update the EngCoder runtime backend and retry.";
    }
    for (const auto& [label, error] : recovery_errors) {
        combined += "\n" + label + ": " + error.message;
    }
    set_last_error(combined);
    return fail_startup(core::Error{
        core::make_error_code(core::Errc::model_unavailable),
        combined,
    });
}

void QwenFlashRuntime::stop() {
    std::scoped_lock lock(mutex_);
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    running_.store(false, std::memory_order_release);
    worker_exited_.store(true, std::memory_order_release);
    active_model_id_.clear();
    active_context_tokens_ = 0;
    active_parallel_slots_ = 0;
    active_safety_fallback_ = false;
    startup_note_.clear();
}

std::string QwenFlashRuntime::url() const {
    return running() ? "http://127.0.0.1:" + std::to_string(config_.qwen_flash_port) : std::string{};
}

std::string QwenFlashRuntime::active_model_id() const {
    std::scoped_lock lock(mutex_);
    return active_model_id_;
}

std::filesystem::path QwenFlashRuntime::cache_repo_root(const RuntimeModelProfile& profile) const {
    return repo_root_for_cache(config_.model_cache_root, profile);
}

std::filesystem::path QwenFlashRuntime::cached_model_path(const RuntimeModelProfile& profile) const {
    if (profile.source == ModelSourceKind::local_gguf) {
        const auto inspection = inspect_local_gguf(profile.local_path);
        return inspection.complete && model_size_is_plausible(profile, inspection.bytes)
            ? profile.local_path
            : std::filesystem::path{};
    }

    struct SplitCandidate {
        std::filesystem::path path;
        std::size_t index{};
        std::size_t total{};
        std::uint64_t bytes{};
    };

    const auto quant = lower(profile.quant);
    const std::regex split_pattern{R"(-(\d+)-of-(\d+)\.gguf$)", std::regex::icase};
    for (const auto& cache_root : candidate_model_cache_roots(config_.model_cache_root)) {
        const auto root = repo_root_for_cache(cache_root, profile);
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec) continue;

        std::vector<SplitCandidate> splits;
        std::vector<std::filesystem::path> single_files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
            const auto raw_name = entry.path().filename().string();
            const auto name = lower(raw_name);
            if (!name.ends_with(".gguf") || (!quant.empty() && name.find(quant) == std::string::npos)) continue;

            std::smatch match;
            if (std::regex_search(raw_name, match, split_pattern) && match.size() == 3) {
                try {
                    const auto bytes = entry.file_size(ec);
                    if (ec) { ec.clear(); continue; }
                    splits.push_back({
                        entry.path(),
                        static_cast<std::size_t>(std::stoull(match[1].str())),
                        static_cast<std::size_t>(std::stoull(match[2].str())),
                        bytes,
                    });
                } catch (...) {}
            } else {
                single_files.push_back(entry.path());
            }
        }

        if (!splits.empty()) {
            const auto expected = splits.front().total;
            std::set<std::size_t> indices;
            std::uint64_t split_bytes = 0;
            for (const auto& split : splits) {
                if (split.total == expected && indices.insert(split.index).second) {
                    split_bytes += split.bytes;
                }
            }
            if (expected > 0 && indices.size() >= expected && model_size_is_plausible(profile, split_bytes)) {
                const auto first = std::ranges::find_if(splits, [expected](const SplitCandidate& split) {
                    return split.total == expected && split.index == 1;
                });
                if (first != splits.end()) return first->path;
            }
        }

        if (!single_files.empty()) {
            std::ranges::sort(single_files);
            for (const auto& file : single_files) {
                const auto bytes = std::filesystem::file_size(file, ec);
                if (!ec && model_size_is_plausible(profile, bytes)) return file;
                ec.clear();
            }
        }
    }
    return {};
}

nlohmann::json QwenFlashRuntime::cache_status(const RuntimeModelProfile& profile) const {
    if (profile.source == ModelSourceKind::local_gguf) {
        const auto inspection = inspect_local_gguf(profile.local_path);
        const bool size_plausible = inspection.complete && model_size_is_plausible(profile, inspection.bytes);
        return {
            {"installed", size_plausible},
            {"cached_bytes", inspection.bytes},
            {"expected_bytes", profile.estimated_bytes},
            {"path", profile.local_path.string()},
            {"matching_gguf_files", inspection.shard_count},
            {"split_expected", inspection.expected_shards},
            {"split_complete", inspection.complete},
            {"size_plausible", size_plausible},
            {"integrity_error", inspection.error.empty() ? nlohmann::json(nullptr) : nlohmann::json(inspection.error)},
        };
    }

    const auto quant = lower(profile.quant);
    const std::regex split_pattern{R"(-(\d+)-of-(\d+)\.gguf$)", std::regex::icase};
    nlohmann::json best = {
        {"installed", false},
        {"cached_bytes", 0},
        {"storage_bytes", 0},
        {"gguf_files", 0},
        {"matching_gguf_files", 0},
        {"split_expected", 0},
        {"split_complete", false},
        {"size_plausible", false},
        {"expected_bytes", profile.estimated_bytes},
        {"path", cache_repo_root(profile).string()},
        {"configured_root", config_.model_cache_root.string()},
    };
    std::uint64_t best_cached_bytes = 0;
    bool best_installed = false;
    nlohmann::json roots = nlohmann::json::array();

    for (const auto& cache_root : candidate_model_cache_roots(config_.model_cache_root)) {
        const auto root = repo_root_for_cache(cache_root, profile);
        std::uint64_t model_bytes = 0;
        std::uint64_t storage_bytes = 0;
        std::set<std::pair<std::string, std::uint64_t>> unique_matching_files;
        std::size_t gguf_files = 0;
        std::size_t matching_gguf_files = 0;
        std::size_t split_expected = 0;
        std::set<std::size_t> split_indices;
        std::error_code ec;

        if (std::filesystem::is_directory(root, ec) && !ec) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
                const auto raw_name = entry.path().filename().string();
                const auto name = lower(raw_name);
                const auto size = entry.file_size(ec);
                if (ec) { ec.clear(); continue; }
                storage_bytes += size;
                if (name.ends_with(".gguf")) {
                    ++gguf_files;
                    const bool matches = quant.empty() || name.find(quant) != std::string::npos;
                    if (matches) {
                        const auto [_, inserted] = unique_matching_files.emplace(raw_name, size);
                        if (inserted) {
                            ++matching_gguf_files;
                            model_bytes += size;
                        }
                        std::smatch split;
                        if (std::regex_search(raw_name, split, split_pattern) && split.size() == 3) {
                            try {
                                const auto index = static_cast<std::size_t>(std::stoull(split[1].str()));
                                const auto total = static_cast<std::size_t>(std::stoull(split[2].str()));
                                split_indices.insert(index);
                                split_expected = std::max(split_expected, total);
                            } catch (...) {}
                        }
                    }
                }
                ec.clear();
            }
        }

        const bool split_complete = split_expected == 0 || split_indices.size() >= split_expected;
        const bool size_plausible = model_size_is_plausible(profile, model_bytes);
        const bool installed = matching_gguf_files > 0 && split_complete && size_plausible;
        roots.push_back({
            {"root", cache_root.string()},
            {"repo_path", root.string()},
            {"cached_bytes", model_bytes},
            {"expected_bytes", profile.estimated_bytes},
            {"split_complete", split_complete},
            {"size_plausible", size_plausible},
            {"installed", installed},
        });

        if ((installed && !best_installed) || (installed == best_installed && model_bytes > best_cached_bytes)) {
            best_installed = installed;
            best_cached_bytes = model_bytes;
            best = {
                {"installed", installed},
                {"cached_bytes", model_bytes},
                {"storage_bytes", storage_bytes},
                {"gguf_files", gguf_files},
                {"matching_gguf_files", matching_gguf_files},
                {"split_expected", split_expected},
                {"split_complete", split_complete},
                {"size_plausible", size_plausible},
                {"expected_bytes", profile.estimated_bytes},
                {"path", root.string()},
                {"cache_root", cache_root.string()},
                {"configured_root", config_.model_cache_root.string()},
            };
        }
    }

    best["candidate_roots"] = std::move(roots);
    return best;
}

nlohmann::json QwenFlashRuntime::describe() const {
    const auto server = discover_server();
    const auto capabilities = server.empty() ? RuntimeCapabilities{} : capabilities_for_server(server);
    std::string runtime_owner = "unavailable";
    if (!server.empty()) {
        const auto runtime_root = state_root_ / "runtimes" / "qwen-flash";
        if (path_is_within(server, runtime_root)) {
            runtime_owner = "EngCoder";
        } else if (!config_.qwen_flash_server.empty()) {
            runtime_owner = "configured-custom";
        } else if (path_is_within(server, config_.workspace)) {
            runtime_owner = "workspace-build";
        } else {
            runtime_owner = "external-compatibility";
        }
    }
    std::scoped_lock state_lock(mutex_);
    return {
        {"type", "qwen-flash"},
        {"native", true},
        {"sparse_moe", true},
        {"downloadable", true},
        {"runtime_owned_by", runtime_owner},
        {"running", running()},
        {"startup_in_progress", startup_in_progress_},
        {"startup_model", startup_model_id_.empty() ? nlohmann::json(nullptr) : nlohmann::json(startup_model_id_)},
        {"active_model", active_model_id_.empty() ? nlohmann::json(nullptr) : nlohmann::json(active_model_id_)},
        {"active_context_tokens", active_context_tokens_ == 0 ? nlohmann::json(nullptr) : nlohmann::json(active_context_tokens_)},
        {"active_parallel_slots", active_parallel_slots_ == 0 ? nlohmann::json(nullptr) : nlohmann::json(active_parallel_slots_)},
        {"safety_fallback", active_safety_fallback_},
        {"startup_note", startup_note_.empty() ? nlohmann::json(nullptr) : nlohmann::json(startup_note_)},
        {"last_command", last_command_.empty() ? nlohmann::json(nullptr) : nlohmann::json(last_command_)},
        {"hardware", last_hardware_},
        {"effective_profile", last_effective_profile_},
        {"endpoint", running() ? nlohmann::json(url()) : nlohmann::json(nullptr)},
        {"server", server.empty() ? nlohmann::json(nullptr) : nlohmann::json(server.string())},
        {"runtime_verified", runtime_verified()},
        {"selected_runtime_recipe", capabilities.recipe.empty() ? nlohmann::json(nullptr) : nlohmann::json(capabilities.recipe)},
        {"expert_cache_candidate", capabilities.expert_cache_candidate},
        {"expert_cache_qualified", last_hardware_.value("expert_cache_qualified", false)},
        {"qwen38_q2_padded_down", capabilities.qwen38_q2_padded_down},
        {"runtime_recipe", kQwenRuntimeRecipe},
        {"backend_commit", kQwenBackendCommit},
        {"cache_root", config_.model_cache_root.string()},
        {"cache_discovery", "configured+hf-env+standard-hf+legacy-qfr"},
        {"native_context_limit", 262144},
        {"recommended_context_tokens", config_.qwen_flash_context_tokens},
        {"recommended_parallel_slots", config_.qwen_flash_parallel_slots},
        {"automatic_context_compaction", config_.qwen_flash_auto_compact},
        {"runtime_policy_modes", nlohmann::json::array({"recommended", "automatic", "manual"})},
        {"expert_cache_modes", nlohmann::json::array({"auto", "off", "on"})},
        {"engcoder_harness", "architect+system1-foundation+addons+tools+memory+verification"},
        {"policy", "mmap+lazy;capability-qualified-moe-cache;engcoder-system1-context-compaction"},
        {"last_error", last_error().empty() ? nlohmann::json(nullptr) : nlohmann::json(last_error())},
    };
}

} // namespace engcoder::model
