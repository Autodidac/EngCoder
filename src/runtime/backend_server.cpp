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

} // namespace

BackendServer::BackendServer(app::AppPaths paths)
    : paths_(std::move(paths)),
      log_path_(paths_.logs / "model-server.log") {}

BackendServer::~BackendServer() {
    stop();
}

bool BackendServer::start(
    const std::filesystem::path& python,
    const std::stop_token stop_token,
    std::string& message) {
    std::lock_guard lock(mutex_);
    if (process_.running() && !base_url_.empty()) {
        message = "Model server is already running at " + base_url_;
        return true;
    }
    if (!std::filesystem::is_regular_file(python)) {
        message = "Python runtime not found: " + python.string();
        return false;
    }
    if (!std::filesystem::is_regular_file(paths_.worker)) {
        message = "Embedded model worker not found: " + paths_.worker.string();
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
                .executable = python,
                .arguments = {
                    paths_.worker.string(),
                    "serve",
                    "--port", "0",
                    "--ready-file", paths_.ready_file.string(),
                    "--state-root", paths_.root.string(),
                    "--max-loaded-models", "2",
                },
                .working_directory = paths_.root,
                .log_file = log_path_,
                .append_log = false,
            },
            launch_error)) {
        message = "Unable to start model server: " + launch_error;
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (!stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        if (!process_.running()) {
            message = "Model server exited during startup; inspect " + log_path_.string();
            return false;
        }
        const auto port = read_port(paths_.ready_file);
        if (port > 0) {
            base_url_ = "http://127.0.0.1:" + std::to_string(port);
            const auto response = backend::http_get(base_url_ + "/health", std::chrono::seconds(5));
            if (response.succeeded()) {
                message = "Model server ready at " + base_url_;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    if (stop_token.stop_requested()) {
        message = "Model server startup cancelled";
    } else {
        message = "Model server did not become ready; inspect " + log_path_.string();
    }
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
