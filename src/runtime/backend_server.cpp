module;
#include "module_prelude.hpp"
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

module threadspire.scheduler;
import threadspire.platform;

namespace threadspire::ai::runtime {
namespace {

template <typename Function>
class ScopeExit final {
public:
    explicit ScopeExit(Function function)
        : function_(std::move(function)) {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ~ScopeExit() {
        function_();
    }

private:
    Function function_;
};

[[nodiscard]] int read_port(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.starts_with("port=")) {
            try {
                return std::stoi(line.substr(5));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

[[nodiscard]] std::filesystem::path native_host_path(const app::AppPaths& paths) {
    return paths.packs / "compact-native-inference" / "bin" / "engcoder-native-host.exe";
}

[[nodiscard]] std::filesystem::path native_host_source(const app::AppPaths& paths) {
    return paths.executable.parent_path() / "optional-packs" /
        "compact-native-inference";
}

[[nodiscard]] bool regular_file_no_follow(const std::filesystem::path& path) noexcept {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) &&
        !std::filesystem::is_symlink(status);
}

[[nodiscard]] std::string installed_pack_version(
    const std::filesystem::path& pack_root) noexcept {
    try {
        std::ifstream stream(pack_root / "pack.json", std::ios::binary);
        if (!stream) {
            return {};
        }
        const std::string text{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        const auto manifest = core::json::parse(text);
        if (core::json::string_or(manifest.find("id")) != "compact-native-inference") {
            return {};
        }
        return core::json::string_or(manifest.find("version"));
    } catch (...) {
        return {};
    }
}
[[nodiscard]] std::filesystem::path resolve_native_host(
    const app::AppPaths& paths,
    std::string& message) {
    const auto installed = native_host_path(paths);
    const auto source = native_host_source(paths);
    const auto source_ready = regular_file_no_follow(
        source / "bin" / "engcoder-native-host.exe");
    const auto catalog = CapabilityPackManager::catalog();
    const auto descriptor = std::ranges::find(
        catalog,
        std::string_view{"compact-native-inference"},
        &CapabilityPackDescriptor::id);
    if (descriptor == catalog.end()) {
        message = "Compact native inference pack description is missing";
        return {};
    }
    if (regular_file_no_follow(installed) &&
        (!source_ready || installed_pack_version(installed.parent_path().parent_path()) ==
            descriptor->version)) {
        return installed;
    }
    if (!source_ready) {
        message = "Compact native inference pack is unavailable: " + source.string();
        return {};
    }
    // The release payload is already a verified, self-contained native host.
    // Starting it in place avoids silently publishing a capability pack into
    // application data. Installing any optional pack remains an explicit UI
    // action owned by CapabilityPackManager.
    return source / "bin" / "engcoder-native-host.exe";
}
} // namespace

bool native_inference_files_present(const app::AppPaths& paths) noexcept {
    return regular_file_no_follow(native_host_path(paths));
}

bool native_inference_source_available(const app::AppPaths& paths) noexcept {
    return native_inference_files_present(paths) ||
        regular_file_no_follow(native_host_source(paths) / "bin" / "engcoder-native-host.exe");
}
BackendServer::BackendServer(app::AppPaths paths)
    : paths_(std::move(paths)),
      log_path_(paths_.logs / "model-server.log") {}

BackendServer::~BackendServer() {
    stop();
}

bool BackendServer::start(
    const std::stop_token stop_token,
    std::string& message) {
    platform::ManagedProcess stale_process;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            message = "Native model server is stopping";
            return false;
        }
        if (starting_) {
            message = "Native model server startup is already in progress";
            return false;
        }
        if (process_.running() && !base_url_.empty()) {
            message = "Native model server is already running at " + base_url_;
            return true;
        }
        if (process_.running()) {
            stale_process = std::move(process_);
            process_ = platform::ManagedProcess{};
        }
        starting_ = true;
        stop_requested_ = false;
        base_url_.clear();
        serving_generation_.clear();
        degradation_message_.clear();
    }

    const ScopeExit finish_startup{[this] {
        {
            std::lock_guard lock(mutex_);
            starting_ = false;
        }
        lifecycle_cv_.notify_all();
    }};
    stale_process.stop();

    const auto cancellation_requested = [this, stop_token] {
        if (stop_token.stop_requested()) {
            return true;
        }
        std::lock_guard lock(mutex_);
        return stop_requested_ || stopping_;
    };

    if (cancellation_requested()) {
        message = "Native model server startup cancelled";
        return false;
    }

    const auto host = resolve_native_host(paths_, message);
    if (host.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::remove(paths_.ready_file, error);
    error.clear();
    std::filesystem::remove(paths_.root / "control" / "inference.server", error);
    error.clear();
    std::filesystem::remove(log_path_, error);

    platform::ManagedProcess candidate_process;
    std::string launch_error;
    if (!candidate_process.start(
            platform::ProcessSpec{
                .executable = host,
                .arguments = {
                    "--port", "0",
                    "--ready-file", paths_.ready_file.string(),
                    "--state-root", paths_.root.string(),
                },
                .working_directory = paths_.root,
                .log_file = log_path_,
                .append_log = false,
            },
            launch_error)) {
        message = "Unable to start compact native model server: " + launch_error;
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (!cancellation_requested() && std::chrono::steady_clock::now() < deadline) {
        if (!candidate_process.running()) {
            message = "Native model server exited during startup; inspect " + log_path_.string();
            return false;
        }
        const auto port = read_port(paths_.ready_file);
        if (port > 0) {
            const auto candidate_base_url =
                "http://127.0.0.1:" + std::to_string(port);
            const auto response = backend::http_get(
                candidate_base_url + "/health",
                std::chrono::seconds(5));
            if (response.succeeded()) {
                std::string parse_error;
                const auto health = core::json::try_parse(response.body, parse_error);
                if (!health.has_value() || !health->is_object()) {
                    message = "Native model server returned malformed health metadata" +
                        (parse_error.empty() ? std::string{} : ": " + parse_error);
                    return false;
                }
                const auto* generation_value = health->find("generation");
                const auto* requested_value = health->find("requested_generation");
                const auto* degraded_value = health->find("degraded");
                const auto* reason_value = health->find("fallback_reason");
                if (generation_value == nullptr || !generation_value->is_string() ||
                    requested_value == nullptr || !requested_value->is_string() ||
                    degraded_value == nullptr || !degraded_value->is_bool() ||
                    reason_value == nullptr || !reason_value->is_string()) {
                    message = "Native model server returned incomplete generation health metadata";
                    return false;
                }
                auto candidate_generation = core::json::string_or(generation_value);
                std::string candidate_degradation;
                if (core::json::bool_or(degraded_value, false)) {
                    const auto requested = core::json::string_or(requested_value);
                    const auto reason = core::json::string_or(reason_value);
                    candidate_degradation =
                        "Requested generation " + requested +
                        " was rejected; serving the verified native foundation instead: " +
                        reason;
                }
                {
                    std::lock_guard lock(mutex_);
                    if (stop_token.stop_requested() || stop_requested_ || stopping_) {
                        message = "Native model server startup cancelled";
                        return false;
                    }
                    process_ = std::move(candidate_process);
                    base_url_ = candidate_base_url;
                    serving_generation_ = candidate_generation;
                    degradation_message_ = candidate_degradation;
                }
                message = candidate_degradation.empty()
                    ? "Compact native model server ready at " + candidate_base_url +
                        " (generation " + candidate_generation + ")"
                    : candidate_degradation;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    message = cancellation_requested()
        ? "Native model server startup cancelled"
        : "Native model server did not become ready; inspect " + log_path_.string();
    return false;
}
void BackendServer::stop() noexcept {
    platform::ManagedProcess process_to_stop;
    std::string shutdown_base_url;
    {
        std::unique_lock lock(mutex_);
        if (stopping_) {
            lifecycle_cv_.wait(lock, [this] { return !stopping_; });
            return;
        }
        stopping_ = true;
        stop_requested_ = true;
        shutdown_base_url = std::move(base_url_);
        base_url_.clear();
        serving_generation_.clear();
        degradation_message_.clear();
        lifecycle_cv_.wait(lock, [this] { return !starting_; });
        process_to_stop = std::move(process_);
        process_ = platform::ManagedProcess{};
    }

    if (!shutdown_base_url.empty()) {
        static_cast<void>(backend::http_post_json(
            shutdown_base_url + "/shutdown",
            "{}",
            std::chrono::seconds(3)));
        for (int attempt = 0; attempt < 30 && process_to_stop.running(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    process_to_stop.stop();
    std::error_code error;
    std::filesystem::remove(paths_.root / "control" / "inference.server", error);
    {
        std::lock_guard lock(mutex_);
        stopping_ = false;
        stop_requested_ = false;
    }
    lifecycle_cv_.notify_all();
}

bool BackendServer::healthy(std::string& message) const {
    std::string health_base_url;
    {
        std::lock_guard lock(mutex_);
        if (stopping_ || !process_.running() || base_url_.empty()) {
            message = "Model server is not running";
            return false;
        }
        health_base_url = base_url_;
    }
    const auto response = backend::http_get(
        health_base_url + "/health",
        std::chrono::seconds(5));
    if (!response.succeeded()) {
        message = response.error.empty() ? response.body : response.error;
        return false;
    }
    message = response.body;
    return true;
}

bool BackendServer::running() const noexcept {
    std::lock_guard lock(mutex_);
    return !stopping_ && !base_url_.empty() && process_.running();
}

std::string BackendServer::base_url() const {
    std::lock_guard lock(mutex_);
    return base_url_;
}

std::string BackendServer::serving_generation() const {
    std::lock_guard lock(mutex_);
    return serving_generation_;
}

std::string BackendServer::degradation_message() const {
    std::lock_guard lock(mutex_);
    return degradation_message_;
}

std::filesystem::path BackendServer::log_path() const {
    return log_path_;
}

} // namespace threadspire::ai::runtime
