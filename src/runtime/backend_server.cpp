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
[[nodiscard]] std::filesystem::path ensure_native_host(
    const app::AppPaths& paths,
    const std::stop_token stop_token,
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
    CapabilityPackManager manager(paths.packs);
    const auto result = manager.install_transactional({
        .descriptor = *descriptor,
        .source_directory = source,
    }, stop_token);
    if (!result.succeeded || !regular_file_no_follow(installed)) {
        message = result.error.empty()
            ? "Compact native inference pack did not publish its host"
            : result.error;
        return {};
    }
    return installed;
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
    std::lock_guard lock(mutex_);
    if (process_.running() && !base_url_.empty()) {
        message = "Native model server is already running at " + base_url_;
        return true;
    }

    const auto host = ensure_native_host(paths_, stop_token, message);
    if (host.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::remove(paths_.ready_file, error);
    error.clear();
    std::filesystem::remove(paths_.root / "control" / "inference.server", error);
    error.clear();
    std::filesystem::remove(log_path_, error);

    std::string launch_error;
    if (!process_.start(
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
    while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        if (!process_.running()) {
            message = "Native model server exited during startup; inspect " + log_path_.string();
            return false;
        }
        const auto port = read_port(paths_.ready_file);
        if (port > 0) {
            base_url_ = "http://127.0.0.1:" + std::to_string(port);
            const auto response = backend::http_get(base_url_ + "/health", std::chrono::seconds(5));
            if (response.succeeded()) {
                message = "Compact native model server ready at " + base_url_;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    message = stop_token.stop_requested()
        ? "Native model server startup cancelled"
        : "Native model server did not become ready; inspect " + log_path_.string();
    process_.stop();
    base_url_.clear();
    return false;
}
void BackendServer::stop() noexcept {
    std::lock_guard lock(mutex_);
    if (!base_url_.empty()) {
        static_cast<void>(backend::http_post_json(
            base_url_ + "/shutdown",
            "{}",
            std::chrono::seconds(3)));
        for (int attempt = 0; attempt < 30 && process_.running(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    process_.stop();
    std::error_code error;
    std::filesystem::remove(paths_.root / "control" / "inference.server", error);
    base_url_.clear();
}

bool BackendServer::healthy(std::string& message) const {
    std::lock_guard lock(mutex_);
    if (!process_.running() || base_url_.empty()) {
        message = "Model server is not running";
        return false;
    }
    const auto response = backend::http_get(base_url_ + "/health", std::chrono::seconds(5));
    if (!response.succeeded()) {
        message = response.error.empty() ? response.body : response.error;
        return false;
    }
    message = response.body;
    return true;
}

bool BackendServer::running() const noexcept {
    std::lock_guard lock(mutex_);
    return process_.running();
}

std::string BackendServer::base_url() const {
    std::lock_guard lock(mutex_);
    return base_url_;
}

std::filesystem::path BackendServer::log_path() const {
    return log_path_;
}

} // namespace threadspire::ai::runtime
