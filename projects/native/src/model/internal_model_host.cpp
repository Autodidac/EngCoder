#include "engcoder/model/internal_model_host.hpp"

#include "engcoder/process/process.hpp"

#include <chrono>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>

namespace engcoder::model {
namespace {

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return nlohmann::json::object();
    try {
        return nlohmann::json::parse(input);
    } catch (...) {
        return nlohmann::json::object();
    }
}

[[nodiscard]] bool manifest_ready_for_inference(const nlohmann::json& manifest) {
    if (!manifest.is_object()) return false;
    if (manifest.contains("inference_ready") && manifest["inference_ready"].is_boolean()) {
        return manifest["inference_ready"].get<bool>();
    }
    return manifest.value("status", std::string{}) == "trained" &&
        manifest.value("bootstrap_complete", false) &&
        manifest.value("full_backbone_trained", false) &&
        manifest.value("generation_ready", false);
}

} // namespace

InternalModelHost::InternalModelHost(const core::Config& config, std::filesystem::path state_root)
    : config_(config),
      state_root_(std::move(state_root)),
      ready_file_(state_root_ / "state" / "server.ready"),
      control_root_(state_root_ / "control") {}

InternalModelHost::~InternalModelHost() {
    stop();
}

void InternalModelHost::set_last_error(std::string message) {
    std::scoped_lock lock(error_mutex_);
    last_error_ = std::move(message);
}

std::string InternalModelHost::last_error() const {
    std::scoped_lock lock(error_mutex_);
    return last_error_;
}

bool InternalModelHost::internal_generation_available() const {
    const auto root = state_root_ / "models" / "internal";
    const auto active = read_json(root / "active.json");
    const auto generation = active.value("generation", active.value("active_generation", std::string{}));
    if (generation.empty()) return false;
    return manifest_ready_for_inference(read_json(root / "generations" / generation / "manifest.json"));
}

std::expected<int, core::Error> InternalModelHost::wait_for_ready() {
    for (int attempt = 0; attempt < 200; ++attempt) {
        std::ifstream input(ready_file_, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.starts_with("port=")) continue;
            try {
                const auto port = std::stoi(line.substr(5));
                if (port > 0 && port <= 65535) return port;
            } catch (...) {}
        }
        if (worker_exited_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    const auto detail = last_error();
    return std::unexpected(core::Error{
        core::make_error_code(core::Errc::model_unavailable),
        detail.empty() ? "internal model worker did not become ready" : detail,
    });
}

std::expected<std::string, core::Error> InternalModelHost::ensure_started() {
    std::scoped_lock lock(mutex_);
    if (running() && port_.load(std::memory_order_acquire) > 0) return url();
    if (!internal_generation_available()) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "no inference-ready internal EngCoder generation exists yet; initialize/train and promote it from the Training view",
        });
    }
    if (!std::filesystem::is_regular_file(config_.training_backend)) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            "internal model backend not found: " + config_.training_backend.string(),
        });
    }

    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    std::error_code error;
    std::filesystem::create_directories(ready_file_.parent_path(), error);
    std::filesystem::create_directories(control_root_, error);
    std::filesystem::remove(ready_file_, error);
    port_.store(0, std::memory_order_release);
    set_last_error({});
    worker_exited_.store(false, std::memory_order_release);

    worker_ = std::jthread([this](std::stop_token stop) {
        process::Request request;
        request.executable = config_.python_executable;
        request.arguments = {
            config_.training_backend.string(),
            "serve",
            "--port", "0",
            "--ready-file", ready_file_.string(),
            "--state-root", state_root_.string(),
            "--control-dir", control_root_.string(),
            "--max-loaded-models", "2",
            "--max-resident-addons", std::to_string(config_.max_resident_addons),
            "--vram-budget-gib", std::to_string(config_.vram_budget_gib),
        };
        request.working_directory = config_.training_backend.parent_path();
        request.timeout = std::chrono::hours{24 * 365};
        request.max_output_bytes = 1024 * 1024;
        auto result = process::run(request, stop);
        if (!result) {
            set_last_error(result.error().message);
        } else if (!result->cancelled && result->exit_code != 0) {
            set_last_error(!result->stderr_text.empty() ? result->stderr_text : result->stdout_text);
        }
        running_.store(false, std::memory_order_release);
        worker_exited_.store(true, std::memory_order_release);
    });

    auto ready = wait_for_ready();
    if (!ready) {
        worker_.request_stop();
        if (worker_.joinable()) worker_.join();
        return std::unexpected(ready.error());
    }
    port_.store(*ready, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    return url();
}

void InternalModelHost::stop() {
    std::scoped_lock lock(mutex_);
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
    running_.store(false, std::memory_order_release);
    port_.store(0, std::memory_order_release);
    worker_exited_.store(true, std::memory_order_release);
}

std::string InternalModelHost::url() const {
    const auto port = port_.load(std::memory_order_acquire);
    return port > 0 ? "http://127.0.0.1:" + std::to_string(port) : std::string{};
}

std::expected<nlohmann::json, core::Error> InternalModelHost::residency_status(bool start_if_needed) {
    if (start_if_needed) {
        auto endpoint = ensure_started();
        if (!endpoint) return std::unexpected(endpoint.error());
    } else if (!running() || port_.load(std::memory_order_acquire) <= 0) {
        return nlohmann::json{
            {"format", "engcoder.residency.v1"},
            {"loaded", false},
            {"available", internal_generation_available()},
            {"policy", "pinned-keymaker+architect;ram-warm-addons;usage-lru-vram"},
            {"components", nlohmann::json::array()},
        };
    }

    httplib::Client client("127.0.0.1", port_.load(std::memory_order_acquire));
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);
    auto result = client.Get("/residency/status");
    if (!result || result->status < 200 || result->status >= 300) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "EngCoder internal residency status is unavailable",
        });
    }
    try {
        return nlohmann::json::parse(result->body);
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            std::string{"invalid EngCoder residency response: "} + error.what(),
        });
    }
}

std::expected<nlohmann::json, core::Error> InternalModelHost::prepare_model(std::string_view model_identifier) {
    auto endpoint = ensure_started();
    if (!endpoint) return std::unexpected(endpoint.error());
    std::string lane{model_identifier};
    if (const auto scheme = lane.find("://"); scheme != std::string::npos) lane.erase(0, scheme + 3);
    if (const auto query = lane.find('?'); query != std::string::npos) lane.erase(query);
    if (lane.empty()) lane = "keymaker";

    httplib::Client client("127.0.0.1", port_.load(std::memory_order_acquire));
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(30, 0);
    const auto body = nlohmann::json{{"lane", lane}}.dump();
    auto result = client.Post("/residency/activate", body, "application/json");
    if (!result || result->status < 200 || result->status >= 300) {
        const auto detail = result ? result->body : std::string{"worker unavailable"};
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "unable to load EngCoder model lane into GPU residency: " + detail,
        });
    }
    try {
        return nlohmann::json::parse(result->body);
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            std::string{"invalid EngCoder residency activation response: "} + error.what(),
        });
    }
}

std::expected<nlohmann::json, core::Error> InternalModelHost::release_idle_addons() {
    if (!running() || port_.load(std::memory_order_acquire) <= 0) return residency_status(false);
    httplib::Client client("127.0.0.1", port_.load(std::memory_order_acquire));
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(10, 0);
    auto result = client.Post("/residency/release-addons", "{}", "application/json");
    if (!result || result->status < 200 || result->status >= 300) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "unable to release idle EngCoder addons from VRAM",
        });
    }
    try {
        return nlohmann::json::parse(result->body);
    } catch (const std::exception& error) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::io_error),
            std::string{"invalid EngCoder residency release response: "} + error.what(),
        });
    }
}

nlohmann::json InternalModelHost::describe() const {
    std::scoped_lock lock(mutex_);
    const auto error = last_error();
    return nlohmann::json{
        {"available", internal_generation_available()},
        {"running", running()},
        {"url", port_.load(std::memory_order_acquire) > 0 ? nlohmann::json(url()) : nlohmann::json(nullptr)},
        {"state_root", state_root_.string()},
        {"backend", config_.training_backend.string()},
        {"last_error", error.empty() ? nlohmann::json(nullptr) : nlohmann::json(error)},
    };
}

} // namespace engcoder::model
