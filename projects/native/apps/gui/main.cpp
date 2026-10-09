#include "engcoder/runtime/runtime_host.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

int run_native_ui();

namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::string json_string_or(const nlohmann::json& value, std::string_view key, std::string fallback) {
    const auto it = value.find(std::string{key});
    if (it != value.end() && it->is_string()) return it->get<std::string>();
    return fallback;
}

struct ChatMessage {
    std::string role;
    std::string text;
};

enum class UpdateKind {
    event,
    failed,
    worker_finished,
    health,
    native_library,
    native_action_complete,
    native_action_failed,
};

struct Update {
    UpdateKind kind{};
    std::string text;
    std::string detail;
    std::optional<engcoder::core::Event> event;
    std::optional<engcoder::runtime::RunOutcome> outcome;
    nlohmann::json payload = nlohmann::json::object();
};

class GuiController {
public:
    GuiController()
        : config_(engcoder::core::Config::from_environment()),
          runtime_(std::make_unique<engcoder::runtime::RuntimeHost>(config_)),
          session_id_(engcoder::runtime::make_session_id("gui")),
          token_source_(config_.lm_studio_token.empty() ? "not configured" : "environment") {
        start_health_worker();
    }

    ~GuiController() {
        stop_workers();
        runtime_->stop_server();
    }

    GuiController(const GuiController&) = delete;
    GuiController& operator=(const GuiController&) = delete;

    [[nodiscard]] bool busy() const noexcept { return busy_.load(std::memory_order_relaxed); }
    [[nodiscard]] const std::string& session_id() const noexcept { return session_id_; }
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    [[nodiscard]] const std::string& backend() const noexcept { return backend_; }
    [[nodiscard]] const std::string& model() const noexcept { return model_; }
    [[nodiscard]] const std::string& service_message() const noexcept { return service_message_; }
    [[nodiscard]] const std::vector<ChatMessage>& messages() const noexcept { return messages_; }
    [[nodiscard]] const std::vector<engcoder::core::Event>& debug_events() const noexcept { return debug_events_; }
    [[nodiscard]] bool server_running() const noexcept { return runtime_->server_running(); }
    [[nodiscard]] std::string server_url() const { return runtime_->server_url(); }
    [[nodiscard]] std::string web_url() const { return runtime_->web_url(); }
    [[nodiscard]] std::string decision_mode() const { return runtime_->decision_mode(); }
    [[nodiscard]] std::string workspace() const { return runtime_->config().workspace.string(); }
    [[nodiscard]] bool token_configured() const noexcept { return !config_.lm_studio_token.empty(); }
    [[nodiscard]] const std::string& token_source() const noexcept { return token_source_; }
    [[nodiscard]] const nlohmann::json& native_library() const noexcept { return native_library_; }
    [[nodiscard]] bool native_busy() const noexcept { return native_busy_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::string foundation_model() const { return runtime_->foundation_model(); }

    void refresh_native_library() {
        enqueue(Update{
            .kind = UpdateKind::native_library,
            .text = {},
            .detail = {},
            .event = std::nullopt,
            .outcome = std::nullopt,
            .payload = runtime_->native_model_library(),
        });
    }

    void provision_native_runtime() {
        start_native_action("Installing Qwen Flash runtime", [this] { return runtime_->provision_native_runtime(); });
    }

    void install_native_model(std::string uri) {
        start_native_action("Starting native model download", [this, uri = std::move(uri)] {
            return runtime_->install_native_model(uri);
        });
    }

    void load_native_model(std::string uri, std::string local_path_override = {}) {
        start_native_action("Loading native model", [this, uri = std::move(uri), local_path_override = std::move(local_path_override)] {
            return runtime_->load_native_model(uri, local_path_override);
        });
    }

    void unload_native_model() {
        start_native_action("Unloading native model", [this] {
            runtime_->unload_native_model();
            return std::expected<nlohmann::json, engcoder::core::Error>{nlohmann::json{{"loaded", false}}};
        });
    }

    void clear_native_model_cache(std::string uri) {
        start_native_action("Clearing temporary native model cache", [this, uri = std::move(uri)] {
            return runtime_->clear_native_model_cache(uri);
        });
    }

    void delete_native_model_weights(std::string uri) {
        start_native_action("Deleting downloaded model weights", [this, uri = std::move(uri)] {
            return runtime_->delete_native_model_weights(uri);
        });
    }

    void reload_native_model(std::string uri, nlohmann::json spec, std::string local_path_override = {}) {
        start_native_action("Reloading native model with saved settings", [this, uri = std::move(uri), spec = std::move(spec), local_path_override = std::move(local_path_override)]() mutable {
            auto saved = runtime_->register_native_model(spec);
            if (!saved) return std::expected<nlohmann::json, engcoder::core::Error>{std::unexpected(saved.error())};
            runtime_->unload_native_model();
            return runtime_->load_native_model(uri, local_path_override);
        });
    }

    void set_foundation_model(std::string uri) {
        start_native_action("Selecting KeyMaker", [this, uri = std::move(uri)] {
            return runtime_->set_foundation_model(uri);
        });
    }

    void register_native_model(nlohmann::json spec) {
        start_native_action("Saving native model profile", [this, spec = std::move(spec)] {
            return runtime_->register_native_model(spec);
        });
    }

    bool apply_model_token(std::string token) {
        if (busy() || native_busy()) {
            service_message_ = "Stop the active task/model operation before changing the model token";
            return false;
        }

        while (!token.empty() && (token.front() == ' ' || token.front() == '\t' || token.front() == '\r' || token.front() == '\n')) token.erase(token.begin());
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t' || token.back() == '\r' || token.back() == '\n')) token.pop_back();
        if (token.size() >= 2 && token.front() == '<' && token.back() == '>') {
            token = token.substr(1, token.size() - 2);
        }
        if (token.empty()) {
            service_message_ = "Enter a Bionic/LM Studio API token first";
            return false;
        }

        return rebuild_runtime_with_token(std::move(token), "GUI session");
    }

    bool clear_model_token() {
        if (busy() || native_busy()) {
            service_message_ = "Stop the active task/model operation before changing the model token";
            return false;
        }
        return rebuild_runtime_with_token({}, "not configured");
    }

    void new_session() {
        if (busy()) return;
        session_id_ = engcoder::runtime::make_session_id("gui");
        messages_.clear();
        debug_events_.clear();
        status_ = "Ready";
        scroll_to_bottom_ = false;
        debug_scroll_to_bottom_ = false;
    }

    void send(std::string prompt) {
        if (prompt.empty() || busy()) return;
        if (task_worker_.joinable()) task_worker_.join();

        messages_.push_back({"user", prompt});
        messages_.push_back({"assistant", {}});
        status_ = "Thinking...";
        busy_.store(true, std::memory_order_relaxed);
        const auto session = session_id_;

        task_worker_ = std::jthread([this, session, prompt = std::move(prompt)](std::stop_token stop) {
            try {
                auto outcome = runtime_->run_task(
                    "native-gui",
                    session,
                    prompt,
                    stop,
                    [this](const engcoder::core::Event& event) {
                        enqueue(Update{.kind = UpdateKind::event, .text = {}, .detail = {}, .event = event, .outcome = std::nullopt, .payload = nlohmann::json::object()});
                    });

                if (!outcome) {
                    enqueue(Update{
                        .kind = UpdateKind::failed,
                        .text = "EngCoder task failed",
                        .detail = outcome.error().message,
                        .event = std::nullopt,
                        .outcome = std::nullopt,
                        .payload = nlohmann::json::object(),
                    });
                    return;
                }
                enqueue(Update{.kind = UpdateKind::worker_finished, .text = {}, .detail = {}, .event = std::nullopt, .outcome = std::move(*outcome), .payload = nlohmann::json::object()});
            } catch (const std::exception& error) {
                enqueue(Update{
                    .kind = UpdateKind::failed,
                    .text = "EngCoder task failed",
                    .detail = error.what(),
                    .event = std::nullopt,
                    .outcome = std::nullopt,
                    .payload = nlohmann::json::object(),
                });
            } catch (...) {
                enqueue(Update{
                    .kind = UpdateKind::failed,
                    .text = "EngCoder task failed",
                    .detail = "unexpected non-standard exception",
                    .event = std::nullopt,
                    .outcome = std::nullopt,
                    .payload = nlohmann::json::object(),
                });
            }
        });
    }

    void stop_task() {
        if (task_worker_.joinable() && busy()) {
            status_ = "Cancelling...";
            task_worker_.request_stop();
        }
    }

    bool start_server() {
        auto started = runtime_->start_server();
        if (!started) {
            service_message_ = started.error().message;
            return false;
        }
        service_message_ = "Server running at " + runtime_->server_url();
        return true;
    }

    void stop_server() {
        runtime_->stop_server();
        service_message_ = "Server stopped";
    }

    bool open_web() {
        if (!server_running() && !start_server()) return false;
        if (!SDL_OpenURL(runtime_->web_url().c_str())) {
            service_message_ = std::string{"Unable to open browser: "} + SDL_GetError();
            return false;
        }
        service_message_ = "Web UI opened; it is using this GUI's embedded runtime";
        return true;
    }

    void drain_updates() {
        std::deque<Update> local;
        {
            std::scoped_lock lock(update_mutex_);
            local.swap(updates_);
        }

        for (auto& update : local) {
            if (update.kind == UpdateKind::native_library) {
                native_library_ = std::move(update.payload);
                continue;
            }
            if (update.kind == UpdateKind::native_action_complete) {
                native_busy_.store(false, std::memory_order_relaxed);
                service_message_ = update.text.empty() ? "Native model operation completed" : update.text;
                native_library_ = runtime_->native_model_library();
                model_ = runtime_->foundation_model();
                continue;
            }
            if (update.kind == UpdateKind::native_action_failed) {
                native_busy_.store(false, std::memory_order_relaxed);
                service_message_ = update.text + (update.detail.empty() ? std::string{} : ": " + update.detail);
                native_library_ = runtime_->native_model_library();
                continue;
            }
            if (update.kind == UpdateKind::health) {
                backend_ = update.text;
                model_ = update.detail;
                continue;
            }
            if (update.kind == UpdateKind::failed) {
                ensure_assistant();
                if (messages_.back().text.empty()) {
                    messages_.back().text = "Error: " + update.text;
                    if (!update.detail.empty()) messages_.back().text += "\n\n" + update.detail;
                }
                status_ = "Error";
                busy_.store(false, std::memory_order_relaxed);
                scroll_to_bottom_ = true;
                continue;
            }
            if (update.kind == UpdateKind::worker_finished) {
                if (!update.outcome) continue;
                if (busy()) {
                    const auto& outcome = *update.outcome;
                    if (outcome.terminal == engcoder::runtime::RunTerminal::completed) {
                        ensure_assistant();
                        if (messages_.back().text.empty()) messages_.back().text = outcome.final_text;
                        status_ = "Ready";
                    } else if (outcome.terminal == engcoder::runtime::RunTerminal::cancelled) {
                        status_ = "Ready";
                    } else if (outcome.terminal == engcoder::runtime::RunTerminal::awaiting_approval) {
                        status_ = "Approval required";
                    } else {
                        status_ = "Error";
                    }
                    busy_.store(false, std::memory_order_relaxed);
                    scroll_to_bottom_ = true;
                }
                continue;
            }
            if (!update.event) continue;

            const auto& event = *update.event;
            debug_events_.push_back(event);
            if (debug_events_.size() > 2000) {
                debug_events_.erase(debug_events_.begin(), debug_events_.begin() + 500);
            }
            debug_scroll_to_bottom_ = true;

            if (event.type == "model.request") {
                status_ = "Thinking...";
            } else if (event.type == "model.text_delta") {
                ensure_assistant();
                messages_.back().text += event.data.value("delta", std::string{});
                status_ = "Generating...";
                scroll_to_bottom_ = true;
            } else if (event.type == "tool.requested") {
                status_ = "Tool: " + event.data.value("name", std::string{"unknown"});
            } else if (event.type == "tool.completed") {
                status_ = "Thinking...";
            } else if (event.type == "task.completed") {
                ensure_assistant();
                if (messages_.back().text.empty()) {
                    messages_.back().text = event.data.value("result", std::string{});
                }
                status_ = "Ready";
                busy_.store(false, std::memory_order_relaxed);
                scroll_to_bottom_ = true;
            } else if (event.type == "task.failed") {
                ensure_assistant();
                if (messages_.back().text.empty()) {
                    messages_.back().text = "Error: " + event.data.value("error", std::string{"Task failed"});
                }
                status_ = "Error";
                busy_.store(false, std::memory_order_relaxed);
            } else if (event.type == "task.awaiting_approval") {
                ensure_assistant();
                if (messages_.back().text.empty()) {
                    messages_.back().text = "This task requires approval before it can continue.";
                }
                status_ = "Approval required";
                busy_.store(false, std::memory_order_relaxed);
            } else if (event.type == "task.cancelled") {
                ensure_assistant();
                if (messages_.back().text.empty()) messages_.back().text = "Task cancelled.";
                status_ = "Ready";
                busy_.store(false, std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool consume_scroll_request() noexcept {
        return std::exchange(scroll_to_bottom_, false);
    }

    [[nodiscard]] bool consume_debug_scroll_request() noexcept {
        return std::exchange(debug_scroll_to_bottom_, false);
    }

    void clear_debug() {
        debug_events_.clear();
        debug_scroll_to_bottom_ = false;
    }

private:
    bool rebuild_runtime_with_token(std::string token, std::string source) {
        const bool restart_server = runtime_->server_running();
        stop_health_worker();
        if (task_worker_.joinable()) task_worker_.join();
        runtime_->stop_server();

        config_.lm_studio_token = std::move(token);
        runtime_ = std::make_unique<engcoder::runtime::RuntimeHost>(config_);
        token_source_ = std::move(source);
        backend_ = "detecting";
        model_ = "auto";

        if (restart_server) {
            auto started = runtime_->start_server();
            if (!started) {
                service_message_ = "Token applied, but the optional server could not restart: " + started.error().message;
                start_health_worker();
                return false;
            }
        }

        service_message_ = config_.lm_studio_token.empty()
            ? "Model token cleared; enter a token or use the CLI environment variable"
            : "Model token applied to the embedded runtime";
        start_health_worker();
        return true;
    }

    void stop_health_worker() {
        if (health_worker_.joinable()) {
            health_worker_.request_stop();
            health_worker_.join();
        }
    }

    void stop_workers() {
        if (task_worker_.joinable()) {
            task_worker_.request_stop();
            task_worker_.join();
        }
        if (native_worker_.joinable()) {
            native_worker_.request_stop();
            runtime_->unload_native_model();
            native_worker_.join();
        }
        stop_health_worker();
    }

    template <typename Fn>
    void start_native_action(std::string label, Fn&& action) {
        if (busy() || native_busy()) {
            service_message_ = busy() ? "Stop the active task before changing native models" : "A native model operation is already active";
            return;
        }
        if (native_worker_.joinable()) native_worker_.join();
        native_busy_.store(true, std::memory_order_relaxed);
        service_message_ = label + "...";
        native_worker_ = std::jthread([
            this,
            label = std::move(label),
            action = std::forward<Fn>(action)
        ](std::stop_token stop) mutable {
            if (stop.stop_requested()) return;
            try {
                auto result = action();
                if (!result) {
                    enqueue(Update{
                        .kind = UpdateKind::native_action_failed,
                        .text = label + " failed",
                        .detail = result.error().message,
                        .event = std::nullopt,
                        .outcome = std::nullopt,
                        .payload = nlohmann::json::object(),
                    });
                    return;
                }
                enqueue(Update{
                    .kind = UpdateKind::native_action_complete,
                    .text = label + " complete",
                    .detail = {},
                    .event = std::nullopt,
                    .outcome = std::nullopt,
                    .payload = std::move(*result),
                });
            } catch (const std::exception& error) {
                enqueue(Update{
                    .kind = UpdateKind::native_action_failed,
                    .text = label + " failed",
                    .detail = error.what(),
                    .event = std::nullopt,
                    .outcome = std::nullopt,
                    .payload = nlohmann::json::object(),
                });
            } catch (...) {
                enqueue(Update{
                    .kind = UpdateKind::native_action_failed,
                    .text = label + " failed",
                    .detail = "unexpected non-standard exception",
                    .event = std::nullopt,
                    .outcome = std::nullopt,
                    .payload = nlohmann::json::object(),
                });
            }
        });
    }

    void ensure_assistant() {
        if (messages_.empty() || messages_.back().role != "assistant") {
            messages_.push_back({"assistant", {}});
        }
    }

    void enqueue(Update update) {
        std::scoped_lock lock(update_mutex_);
        updates_.push_back(std::move(update));
    }

    void start_health_worker() {
        health_worker_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                try {
                    if (busy()) {
                        for (int i = 0; i < 10 && !stop.stop_requested(); ++i) {
                            std::this_thread::sleep_for(100ms);
                        }
                        continue;
                    }
                    if (auto discovery = runtime_->model_discovery()) {
                        enqueue(Update{
                            .kind = UpdateKind::health,
                            .text = json_string_or(*discovery, "backend", "connected"),
                            .detail = json_string_or(*discovery, "selected_model", "auto"),
                            .event = std::nullopt,
                            .outcome = std::nullopt,
                        });
                        enqueue(Update{
                            .kind = UpdateKind::native_library,
                            .text = {},
                            .detail = {},
                            .event = std::nullopt,
                            .outcome = std::nullopt,
                            .payload = runtime_->native_model_library(),
                        });
                    } else {
                        enqueue(Update{
                            .kind = UpdateKind::health,
                            .text = config_.lm_studio_token.empty() ? "model backend unavailable" : "model backend unavailable",
                            .detail = "-",
                            .event = std::nullopt,
                            .outcome = std::nullopt,
                        });
                    }
                } catch (const std::exception& error) {
                    enqueue(Update{
                        .kind = UpdateKind::health,
                        .text = "model health check failed",
                        .detail = error.what(),
                        .event = std::nullopt,
                        .outcome = std::nullopt,
                        .payload = nlohmann::json::object(),
                    });
                } catch (...) {
                    enqueue(Update{
                        .kind = UpdateKind::health,
                        .text = "model health check failed",
                        .detail = "unexpected non-standard exception",
                        .event = std::nullopt,
                        .outcome = std::nullopt,
                        .payload = nlohmann::json::object(),
                    });
                }
                for (int i = 0; i < 30 && !stop.stop_requested(); ++i) {
                    std::this_thread::sleep_for(100ms);
                }
            }
        });
    }

    engcoder::core::Config config_;
    std::unique_ptr<engcoder::runtime::RuntimeHost> runtime_;
    std::string session_id_;
    std::string token_source_;
    std::vector<ChatMessage> messages_;
    std::vector<engcoder::core::Event> debug_events_;
    std::string status_{"Ready"};
    std::string backend_{"detecting"};
    std::string model_{"auto"};
    std::string service_message_{"Native runtime is embedded; HTTP server is off"};
    nlohmann::json native_library_ = nlohmann::json::object();
    std::atomic_bool busy_{false};
    std::atomic_bool native_busy_{false};
    bool scroll_to_bottom_{false};
    bool debug_scroll_to_bottom_{false};

    std::mutex update_mutex_;
    std::deque<Update> updates_;
    std::jthread task_worker_;
    std::jthread health_worker_;
    std::jthread native_worker_;
};


struct GuiFonts {
    ImFont* ui{};
    ImFont* mono{};
};

[[nodiscard]] ImFont* load_first_font(ImGuiIO& io, const std::vector<std::filesystem::path>& candidates) {
    for (const auto& path : candidates) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) continue;
        if (auto* font = io.Fonts->AddFontFromFileTTF(path.string().c_str())) return font;
    }
    return nullptr;
}

[[nodiscard]] GuiFonts load_gui_fonts(ImGuiIO& io) {
#if defined(_WIN32)
    const std::vector<std::filesystem::path> ui_candidates{
        R"(C:\Windows\Fonts\SegoeUIVF.ttf)",
        R"(C:\Windows\Fonts\segoeui.ttf)",
        R"(C:\Windows\Fonts\arial.ttf)",
    };
    const std::vector<std::filesystem::path> mono_candidates{
        R"(C:\Windows\Fonts\CascadiaCode.ttf)",
        R"(C:\Windows\Fonts\CascadiaMono.ttf)",
        R"(C:\Windows\Fonts\consola.ttf)",
    };
#elif defined(__APPLE__)
    const std::vector<std::filesystem::path> ui_candidates{
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    const std::vector<std::filesystem::path> mono_candidates{
        "/System/Library/Fonts/SFNSMono.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/System/Library/Fonts/Monaco.ttf",
    };
#else
    const std::vector<std::filesystem::path> ui_candidates{
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    };
    const std::vector<std::filesystem::path> mono_candidates{
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
    };
#endif

    GuiFonts fonts;
    fonts.ui = load_first_font(io, ui_candidates);
    if (fonts.ui == nullptr) fonts.ui = io.Fonts->AddFontDefaultVector();
    fonts.mono = load_first_font(io, mono_candidates);
    if (fonts.mono == nullptr) fonts.mono = fonts.ui;
    io.FontDefault = fonts.ui;
    return fonts;
}

// Dear ImGui's vcpkg build uses 16-bit ImWchar by default. Keep full UTF-8
// in persisted chat/history, but omit supplemental-plane emoji and their
// joiners from the native display so they never degrade into confusing ??
// artifacts. BMP punctuation/symbols remain intact with the system UI font.
[[nodiscard]] std::string native_display_text(std::string_view input) {
    std::string output;
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const auto first = static_cast<unsigned char>(input[i]);
        std::uint32_t codepoint{};
        std::size_t length{};
        if (first < 0x80U) { codepoint = first; length = 1; }
        else if ((first & 0xE0U) == 0xC0U && i + 1 < input.size()) {
            codepoint = (first & 0x1FU) << 6U;
            codepoint |= static_cast<unsigned char>(input[i + 1]) & 0x3FU;
            length = 2;
        } else if ((first & 0xF0U) == 0xE0U && i + 2 < input.size()) {
            codepoint = (first & 0x0FU) << 12U;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3FU) << 6U;
            codepoint |= static_cast<unsigned char>(input[i + 2]) & 0x3FU;
            length = 3;
        } else if ((first & 0xF8U) == 0xF0U && i + 3 < input.size()) {
            codepoint = (first & 0x07U) << 18U;
            codepoint |= (static_cast<unsigned char>(input[i + 1]) & 0x3FU) << 12U;
            codepoint |= (static_cast<unsigned char>(input[i + 2]) & 0x3FU) << 6U;
            codepoint |= static_cast<unsigned char>(input[i + 3]) & 0x3FU;
            length = 4;
        } else {
            output.push_back('?');
            ++i;
            continue;
        }

        const bool displayable = codepoint <= 0xFFFFU && codepoint != 0xFE0FU && codepoint != 0x200DU;
        if (displayable) output.append(input.substr(i, length));
        i += length;
    }
    return output;
}

enum class UiSizePreset : int {
    compact = 0,
    standard = 1,
    large = 2,
};

constexpr float kBaseFontSize = 18.0F;

[[nodiscard]] float display_content_scale(SDL_Window* window) {
    const float scale = SDL_GetWindowDisplayScale(window);
    return (std::isfinite(scale) && scale > 0.0F) ? scale : 1.0F;
}

[[nodiscard]] float high_resolution_readability_boost(SDL_Window* window, float content_scale) {
    if (content_scale >= 1.25F) return 1.0F;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    if (display == 0) return 1.0F;
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(display);
    if (mode == nullptr) return 1.0F;

    const float density = (std::isfinite(mode->pixel_density) && mode->pixel_density > 0.0F)
        ? mode->pixel_density
        : 1.0F;
    const float pixel_width = static_cast<float>(mode->w) * density;
    const float pixel_height = static_cast<float>(mode->h) * density;
    if (pixel_width >= 5000.0F || pixel_height >= 2800.0F) return 1.35F;
    if (pixel_width >= 3000.0F || pixel_height >= 1800.0F) return 1.20F;
    return 1.0F;
}

[[nodiscard]] float preset_multiplier(UiSizePreset preset) {
    // The previous Automatic preset is now the absolute maximum.
    // Compact and Standard step downward from that baseline so high-DPI
    // displays stay readable without turning the whole UI into oversized text.
    switch (preset) {
        case UiSizePreset::compact: return 0.70F;
        case UiSizePreset::standard: return 0.85F;
        case UiSizePreset::large: return 1.0F;
    }
    return 0.85F;
}

struct UiScaleState {
    UiSizePreset preset{UiSizePreset::standard};
    float content_scale{1.0F};
    float readability_boost{1.0F};
    float effective_scale{1.0F};

    void refresh(SDL_Window* window) {
        content_scale = display_content_scale(window);
        readability_boost = high_resolution_readability_boost(window, content_scale);
        effective_scale = std::clamp(
            content_scale * readability_boost * preset_multiplier(preset),
            0.65F,
            2.60F);
    }
};

template <std::size_t N>
void copy_text(std::array<char, N>& target, std::string_view value) {
    target.fill('\0');
    const auto count = std::min<std::size_t>(N - 1, value.size());
    std::copy_n(value.data(), count, target.data());
}

struct NativeProfileEditor {
    std::array<char, 160> id{};
    std::array<char, 192> display_name{};
    std::array<char, 320> repo{};
    std::array<char, 128> quant{};
    std::array<char, 768> local_path{};
    std::array<char, 32> gpu_layers{};
    std::array<char, 32> kv_type_k{};
    std::array<char, 32> kv_type_v{};
    std::array<char, 32> load_mode{};
    std::array<char, 32> lazy_mode{};
    std::array<char, 192> tensor_override{};
    std::array<char, 64> speculative_type{};
    std::array<char, 320> speculative_repo{};
    std::array<char, 768> speculative_model{};

    int source{}; // 0 = Hugging Face, 1 = local GGUF
    int runtime_policy{}; // 0 = Recommended, 1 = Automatic, 2 = Manual
    int gpu_offload_percent{50};
    int gpu_layer_count_hint{48};
    int context_tokens{81'920};
    int parallel_slots{1};
    int reserve_tokens{4'096};
    int threads{12};
    int batch_threads{12};
    int batch_size{1024};
    int ubatch_size{256};
    int kv_gpu_layers{};
    int prompt_cache_mib{2048};
    int expert_cache_mode{}; // 0 = Auto, 1 = Off, 2 = On
    int expert_cache_mib{6144};
    int pinned_host_mib{12288};
    int speculative_tokens{2};
    float compact_trigger{0.97F};
    float compact_target{0.72F};
    bool auto_compact{true};
    bool flash_attention{true};
    bool kv_offload{};
    bool kv_cpu_pinned{true};
    bool phase_aware_workspace{true};
    bool context_shift{};
    bool reasoning_preserve{};
    bool early_router{};
    bool speculative_enabled{};

    NativeProfileEditor() {
        copy_text(id, "qwen3.8-flash-next-ud-q2-k-xl");
        copy_text(display_name, "Qwen3.8 Flash Next · UD-Q2_K_XL");
        copy_text(repo, "unsloth/Qwen3.8-Flash-Next-GGUF");
        copy_text(quant, "UD-Q2_K_XL");
        copy_text(gpu_layers, "auto");
        copy_text(kv_type_k, "q4_0");
        copy_text(kv_type_v, "q4_0");
        copy_text(load_mode, "mmap");
        copy_text(lazy_mode, "on");
        copy_text(tensor_override, "per_layer_token_embd.weight=CPU");
        copy_text(speculative_type, "draft-mtp");
        copy_text(speculative_repo, "");
    }

    void reset_defaults(bool preserve_identity = true) {
        const auto keep_id = id;
        const auto keep_display = display_name;
        const auto keep_repo = repo;
        const auto keep_quant = quant;
        const auto keep_local = local_path;
        const auto keep_source = source;
        runtime_policy = 0;
        gpu_offload_percent = 50;
        gpu_layer_count_hint = 48;
        context_tokens = 81'920;
        parallel_slots = 1;
        reserve_tokens = 4'096;
        threads = 12;
        batch_threads = 12;
        batch_size = 1024;
        ubatch_size = 256;
        kv_gpu_layers = 0;
        prompt_cache_mib = 2048;
        expert_cache_mode = 0;
        expert_cache_mib = 6144;
        pinned_host_mib = 12288;
        speculative_tokens = 2;
        compact_trigger = 0.97F;
        compact_target = 0.72F;
        auto_compact = true;
        flash_attention = true;
        kv_offload = false;
        kv_cpu_pinned = true;
        phase_aware_workspace = true;
        context_shift = false;
        reasoning_preserve = false;
        early_router = false;
        speculative_enabled = false;
        copy_text(gpu_layers, "auto");
        copy_text(kv_type_k, "q4_0");
        copy_text(kv_type_v, "q4_0");
        copy_text(load_mode, "mmap");
        copy_text(lazy_mode, "on");
        copy_text(tensor_override, "per_layer_token_embd.weight=CPU");
        copy_text(speculative_type, "draft-mtp");
        copy_text(speculative_repo, "");
        copy_text(speculative_model, "");
        if (preserve_identity) {
            id = keep_id; display_name = keep_display; repo = keep_repo; quant = keep_quant; local_path = keep_local; source = keep_source;
        }
    }

    void load(const nlohmann::json& model) {
        copy_text(id, model.value("id", std::string{id.data()}));
        copy_text(display_name, model.value("display_name", std::string{display_name.data()}));
        copy_text(repo, json_string_or(model, "repo", std::string{repo.data()}));
        copy_text(quant, json_string_or(model, "quant", std::string{quant.data()}));
        copy_text(local_path, json_string_or(model, "local_path", {}));
        source = model.value("source", std::string{"hugging-face"}) == "local-gguf" ? 1 : 0;
        const auto policy = model.value("runtime_policy", std::string{"recommended"});
        runtime_policy = policy == "automatic" ? 1 : (policy == "manual" ? 2 : 0);
        gpu_offload_percent = static_cast<int>(model.value("gpu_offload_percent", 50U));
        gpu_layer_count_hint = static_cast<int>(model.value("gpu_layer_count_hint", 48U));
        context_tokens = static_cast<int>(model.value("context_tokens", 81'920U));
        parallel_slots = static_cast<int>(model.value("parallel_slots", 1U));
        reserve_tokens = static_cast<int>(model.value("context_reserve_tokens", 4'096U));
        auto_compact = model.value("auto_compact", true);
        compact_trigger = static_cast<float>(model.value("compact_trigger_ratio", 0.97));
        compact_target = static_cast<float>(model.value("compact_target_ratio", 0.72));
        threads = static_cast<int>(model.value("threads", 12U));
        batch_threads = static_cast<int>(model.value("batch_threads", 12U));
        batch_size = static_cast<int>(model.value("batch_size", 1024U));
        ubatch_size = static_cast<int>(model.value("ubatch_size", 256U));
        copy_text(gpu_layers, model.value("gpu_layers", std::string{"auto"}));
        flash_attention = model.value("flash_attention", true);
        copy_text(kv_type_k, model.value("kv_type_k", std::string{"q4_0"}));
        copy_text(kv_type_v, model.value("kv_type_v", std::string{"q4_0"}));
        kv_gpu_layers = static_cast<int>(model.value("kv_gpu_layers", 0U));
        kv_offload = model.value("kv_offload", false);
        kv_cpu_pinned = model.value("kv_cpu_pinned", true);
        phase_aware_workspace = model.value("phase_aware_workspace", true);
        copy_text(load_mode, model.value("load_mode", std::string{"mmap"}));
        copy_text(lazy_mode, model.value("lazy_mode", std::string{"on"}));
        copy_text(tensor_override, model.value("tensor_override", std::string{"per_layer_token_embd.weight=CPU"}));
        prompt_cache_mib = static_cast<int>(model.value("prompt_cache_mib", 2048U));
        context_shift = model.value("context_shift", false);
        reasoning_preserve = model.value("reasoning_preserve", false);
        const auto cache_mode = model.value("expert_cache_mode", std::string{"auto"});
        expert_cache_mode = cache_mode == "off" ? 1 : (cache_mode == "on" ? 2 : 0);
        expert_cache_mib = static_cast<int>(model.value("expert_cache_mib", 6144U));
        pinned_host_mib = static_cast<int>(model.value("pinned_host_mib", 12288U));
        early_router = model.value("early_router", false);
        speculative_enabled = model.value("speculative_enabled", false);
        copy_text(speculative_type, model.value("speculative_type", std::string{"draft-mtp"}));
        copy_text(speculative_repo, json_string_or(model, "speculative_repo", {}));
        copy_text(speculative_model, json_string_or(model, "speculative_model", {}));
        speculative_tokens = static_cast<int>(model.value("speculative_tokens", 2U));
    }

    [[nodiscard]] nlohmann::json json() const {
        return {
            {"id", std::string{id.data()}},
            {"display_name", std::string{display_name.data()}},
            {"source", source == 1 ? "local-gguf" : "hugging-face"},
            {"repo", std::string{repo.data()}},
            {"quant", std::string{quant.data()}},
            {"local_path", std::string{local_path.data()}},
            {"runtime_policy", runtime_policy == 1 ? "automatic" : (runtime_policy == 2 ? "manual" : "recommended")},
            {"gpu_offload_percent", std::clamp(gpu_offload_percent, 0, 100)},
            {"gpu_layer_count_hint", std::clamp(gpu_layer_count_hint, 1, 512)},
            {"context_tokens", std::max(4096, context_tokens)},
            {"parallel_slots", std::clamp(parallel_slots, 1, 4)},
            {"context_reserve_tokens", std::max(1024, reserve_tokens)},
            {"auto_compact", auto_compact},
            {"compact_trigger_ratio", std::clamp(compact_trigger, 0.50F, 0.98F)},
            {"compact_target_ratio", std::clamp(compact_target, 0.25F, std::max(0.26F, compact_trigger - 0.02F))},
            {"threads", std::max(1, threads)},
            {"batch_threads", std::max(1, batch_threads)},
            {"batch_size", std::max(32, batch_size)},
            {"ubatch_size", std::max(32, ubatch_size)},
            {"gpu_layers", std::string{gpu_layers.data()}},
            {"flash_attention", flash_attention},
            {"kv_type_k", std::string{kv_type_k.data()}},
            {"kv_type_v", std::string{kv_type_v.data()}},
            {"kv_gpu_layers", std::max(0, kv_gpu_layers)},
            {"kv_offload", kv_offload},
            {"kv_cpu_pinned", kv_cpu_pinned},
            {"phase_aware_workspace", phase_aware_workspace},
            {"load_mode", std::string{load_mode.data()}},
            {"lazy_mode", std::string{lazy_mode.data()}},
            {"tensor_override", std::string{tensor_override.data()}},
            {"prompt_cache_mib", std::max(0, prompt_cache_mib)},
            {"context_shift", context_shift},
            {"reasoning_preserve", reasoning_preserve},
            {"expert_cache_mode", expert_cache_mode == 1 ? "off" : (expert_cache_mode == 2 ? "on" : "auto")},
            {"expert_cache_mib", std::max(0, expert_cache_mib)},
            {"pinned_host_mib", std::max(0, pinned_host_mib)},
            {"early_router", early_router},
            {"speculative_enabled", speculative_enabled},
            {"speculative_type", std::string{speculative_type.data()}},
            {"speculative_repo", std::string{speculative_repo.data()}},
            {"speculative_model", std::string{speculative_model.data()}},
            {"speculative_tokens", std::max(1, speculative_tokens)},
        };
    }
};

enum class NativeTheme : int {
    engcoder_dark = 0,
    engcoder_light = 1,
    windows_system = 2,
    windows_dark = 3,
    windows_light = 4,
    matrix_green = 5,
};

[[nodiscard]] bool windows_system_uses_light_theme() {
#if defined(_WIN32)
    DWORD value = 0;
    DWORD size = sizeof(value);
    const auto status = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &size);
    return status == ERROR_SUCCESS && value != 0;
#else
    return false;
#endif
}

void configure_style(float scale, NativeTheme requested_theme) {
    auto& style = ImGui::GetStyle();
    style = ImGuiStyle{};

    NativeTheme theme = requested_theme;
    if (theme == NativeTheme::windows_system) {
        theme = windows_system_uses_light_theme() ? NativeTheme::windows_light : NativeTheme::windows_dark;
    }

    if (theme == NativeTheme::engcoder_light || theme == NativeTheme::windows_light) {
        ImGui::StyleColorsLight(&style);
    } else {
        ImGui::StyleColorsDark(&style);
    }

    auto& colors = style.Colors;
    if (theme == NativeTheme::engcoder_dark) {
        colors[ImGuiCol_WindowBg] = ImVec4(0.02F, 0.05F, 0.08F, 1.0F);
        colors[ImGuiCol_ChildBg] = ImVec4(0.035F, 0.075F, 0.11F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.055F, 0.11F, 0.16F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.075F, 0.20F, 0.29F, 1.0F);
        colors[ImGuiCol_Button] = ImVec4(0.08F, 0.22F, 0.32F, 1.0F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.12F, 0.31F, 0.43F, 1.0F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.35F, 0.68F, 0.92F, 1.0F);
    } else if (theme == NativeTheme::engcoder_light) {
        colors[ImGuiCol_WindowBg] = ImVec4(0.96F, 0.97F, 0.98F, 1.0F);
        colors[ImGuiCol_ChildBg] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.91F, 0.94F, 0.97F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.80F, 0.89F, 0.96F, 1.0F);
        colors[ImGuiCol_Button] = ImVec4(0.18F, 0.43F, 0.62F, 1.0F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.12F, 0.37F, 0.58F, 1.0F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.05F, 0.38F, 0.68F, 1.0F);
    } else if (theme == NativeTheme::windows_dark) {
        colors[ImGuiCol_WindowBg] = ImVec4(0.045F, 0.045F, 0.045F, 1.0F);
        colors[ImGuiCol_ChildBg] = ImVec4(0.075F, 0.075F, 0.075F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.10F, 0.10F, 0.10F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.06F, 0.31F, 0.52F, 1.0F);
        colors[ImGuiCol_Button] = ImVec4(0.06F, 0.31F, 0.52F, 1.0F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.08F, 0.40F, 0.66F, 1.0F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.28F, 0.63F, 0.88F, 1.0F);
    } else if (theme == NativeTheme::windows_light) {
        colors[ImGuiCol_WindowBg] = ImVec4(0.95F, 0.95F, 0.95F, 1.0F);
        colors[ImGuiCol_ChildBg] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.93F, 0.93F, 0.93F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.80F, 0.89F, 0.96F, 1.0F);
        colors[ImGuiCol_Button] = ImVec4(0.06F, 0.36F, 0.63F, 1.0F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.04F, 0.42F, 0.75F, 1.0F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.06F, 0.36F, 0.63F, 1.0F);
    } else if (theme == NativeTheme::matrix_green) {
        colors[ImGuiCol_Text] = ImVec4(0.72F, 1.0F, 0.78F, 1.0F);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.30F, 0.62F, 0.38F, 1.0F);
        colors[ImGuiCol_WindowBg] = ImVec4(0.0F, 0.018F, 0.004F, 1.0F);
        colors[ImGuiCol_ChildBg] = ImVec4(0.01F, 0.055F, 0.025F, 1.0F);
        colors[ImGuiCol_FrameBg] = ImVec4(0.02F, 0.10F, 0.045F, 1.0F);
        colors[ImGuiCol_Header] = ImVec4(0.03F, 0.20F, 0.08F, 1.0F);
        colors[ImGuiCol_Button] = ImVec4(0.04F, 0.28F, 0.11F, 1.0F);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.07F, 0.43F, 0.17F, 1.0F);
        colors[ImGuiCol_CheckMark] = ImVec4(0.13F, 0.82F, 0.37F, 1.0F);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.13F, 0.82F, 0.37F, 1.0F);
        colors[ImGuiCol_Border] = ImVec4(0.08F, 0.30F, 0.14F, 0.80F);
    }

    style.FontSizeBase = kBaseFontSize;
    style.FontScaleDpi = scale;
    style.WindowRounding = 8.0F;
    style.ChildRounding = 8.0F;
    style.FrameRounding = 7.0F;
    style.PopupRounding = 8.0F;
    style.ScrollbarRounding = 8.0F;
    style.WindowBorderSize = 0.0F;
    style.ChildBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.ItemSpacing = ImVec2(10.0F, 8.0F);
    style.FramePadding = ImVec2(10.0F, 8.0F);
    style.ScaleAllSizes(scale);
}

void message_bubble(const ChatMessage& message, float scale, ImFont* ui_font) {
    const bool user = message.role == "user";
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = std::min(available * 0.86F, 980.0F * scale);
    if (user) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, available - width));
    }

    const auto& palette = ImGui::GetStyle().Colors;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, user ? palette[ImGuiCol_Header] : palette[ImGuiCol_ChildBg]);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0F * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0F * scale, 12.0F * scale));
    const auto id = "##message_" + std::to_string(reinterpret_cast<std::uintptr_t>(&message));
    ImGui::BeginChild(id.c_str(), ImVec2(width, 0.0F), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s", user ? "You" : "EngCoder");
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 30.0F * scale);
    const auto display = native_display_text(message.text.empty() ? std::string_view{"..."} : std::string_view{message.text});
    ImGui::PushFont(ui_font);
    ImGui::TextUnformatted(display.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0F, 8.0F * scale));
}

[[nodiscard]] bool debug_event_visible(const engcoder::core::Event& event, bool tools_only) {
    if (!tools_only) return true;
    return event.type.starts_with("tool.") || event.type.starts_with("model.tool_") ||
        event.type == "agent.stall_detected";
}

void debug_inspector(GuiController& controller, float height, bool& tools_only, ImFont* mono_font) {
    ImGui::BeginChild("DebugInspector", ImVec2(0.0F, height), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Runtime / tool events");
    ImGui::SameLine();
    ImGui::Checkbox("Tools only", &tools_only);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) controller.clear_debug();
    ImGui::Separator();

    for (const auto& event : controller.debug_events()) {
        if (!debug_event_visible(event, tools_only)) continue;
        std::string summary = std::to_string(event.sequence) + "  " + event.type;
        if (event.type == "tool.reviewed") {
            const auto probabilities = event.data.value("probabilities", engcoder::core::json::object());
            const auto allow = probabilities.value("allow", 0.0) * 100.0;
            const auto approval = probabilities.value("approval", 0.0) * 100.0;
            const auto deny = probabilities.value("deny", 0.0) * 100.0;
            summary += "  [" + event.data.value("reviewer", std::string{"policy"}) + "]";
            summary += "  A " + std::to_string(static_cast<int>(std::lround(allow))) + "%";
            summary += " / R " + std::to_string(static_cast<int>(std::lround(approval))) + "%";
            summary += " / D " + std::to_string(static_cast<int>(std::lround(deny))) + "%";
        }
        const auto label = summary + "##" + std::to_string(event.sequence);
        ImGui::PushFont(mono_font);
        const bool open = ImGui::TreeNode(label.c_str());
        ImGui::PopFont();
        if (open) {
            ImGui::TextDisabled("task %s", event.task_id.c_str());
            const auto payload = native_display_text(event.data.dump(2));
            ImGui::PushTextWrapPos(0.0F);
            ImGui::PushFont(mono_font);
            ImGui::TextUnformatted(payload.c_str());
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            ImGui::TreePop();
        }
    }
    if (controller.consume_debug_scroll_request()) ImGui::SetScrollHereY(1.0F);
    ImGui::EndChild();
}

#if defined(_WIN32)
[[nodiscard]] std::optional<std::filesystem::path> edge_executable() {
    constexpr std::wstring_view suffix = L"Microsoft\\Edge\\Application\\msedge.exe";
    const wchar_t* variables[] = {L"PROGRAMFILES(X86)", L"PROGRAMFILES", L"LOCALAPPDATA"};
    for (const auto* variable : variables) {
        wchar_t buffer[32768]{};
        const auto length = GetEnvironmentVariableW(variable, buffer, static_cast<DWORD>(std::size(buffer)));
        if (length == 0 || length >= std::size(buffer)) continue;
        std::filesystem::path candidate{buffer};
        candidate /= suffix;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return std::nullopt;
}

void resolve_desktop_source_paths(engcoder::core::Config& config) {
    std::error_code error;
    if (std::filesystem::is_regular_file(config.web_root / "index.html", error)) return;

    std::vector<std::filesystem::path> starts;
    starts.push_back(std::filesystem::current_path(error));

    wchar_t executable_buffer[32768]{};
    const auto executable_length = GetModuleFileNameW(
        nullptr, executable_buffer, static_cast<DWORD>(std::size(executable_buffer)));
    if (executable_length > 0 && executable_length < std::size(executable_buffer)) {
        starts.emplace_back(std::filesystem::path{executable_buffer}.parent_path());
    }

    for (auto start : starts) {
        for (int depth = 0; depth < 8 && !start.empty(); ++depth) {
            const auto candidate = start / "projects" / "web";
            error.clear();
            if (std::filesystem::is_regular_file(candidate / "index.html", error)) {
                config.workspace = start;
                config.web_root = candidate;
                return;
            }
            const auto parent = start.parent_path();
            if (parent == start) break;
            start = parent;
        }
    }
}

[[nodiscard]] std::wstring quote_windows(std::wstring value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back(L'"');
    for (const auto character : value) {
        if (character == L'"') escaped.push_back(L'\\');
        escaped.push_back(character);
    }
    escaped.push_back(L'"');
    return escaped;
}

[[nodiscard]] int run_shared_desktop() {
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    auto config = engcoder::core::Config::from_environment();
    resolve_desktop_source_paths(config);
    config.desktop_session_token = engcoder::runtime::make_session_id("desktop");

    std::unique_ptr<engcoder::runtime::RuntimeHost> runtime;
    std::string start_error;
    try {
        auto candidate = std::make_unique<engcoder::runtime::RuntimeHost>(config);
        auto started = candidate->start_server();
        if (started) {
            runtime = std::move(candidate);
        } else {
            start_error = started.error().message;
        }
    } catch (const std::exception& error) {
        start_error = error.what();
    }

    if (!runtime) {
        const auto text = std::wstring{L"EngCoder could not start its embedded runtime/web adapter.\n\n"} +
            std::wstring(start_error.begin(), start_error.end());
        (void)MessageBoxW(nullptr, text.c_str(), L"EngCoder startup failed", MB_OK | MB_ICONERROR);
        return 1;
    }

    const auto edge = edge_executable();
    if (!edge) {
        runtime->stop_server();
        (void)MessageBoxW(nullptr,
            L"Microsoft Edge was not found. EngCoder will open the diagnostic native UI instead.\n\n"
            L"The shared Desktop/Web interface requires an installed Edge runtime.",
            L"EngCoder desktop UI", MB_OK | MB_ICONWARNING);
        return run_native_ui();
    }

    // Keep a dedicated persistent browser profile so the desktop surface has
    // exactly the same local history/theme/settings persistence as Web mode.
    // It is isolated from the user's normal Edge profile, so normal browser
    // sessions cannot absorb the app-mode launch.
    wchar_t local_app_data[32768]{};
    const auto local_length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", local_app_data, static_cast<DWORD>(std::size(local_app_data)));
    std::filesystem::path profile_root;
    if (local_length > 0 && local_length < std::size(local_app_data)) {
        profile_root = local_app_data;
    } else {
        profile_root = std::filesystem::temp_directory_path();
    }
    profile_root /= "EngCoder";
    profile_root /= "DesktopProfile";
    std::error_code file_error;
    std::filesystem::create_directories(profile_root, file_error);

    const auto url_utf8 = runtime->web_url() + "?desktop=1&desktop_token=" + config.desktop_session_token;
    const std::wstring url{url_utf8.begin(), url_utf8.end()};
    std::wstring command = quote_windows(edge->wstring());
    command += L" --app=" + quote_windows(url);
    command += L" --user-data-dir=" + quote_windows(profile_root.wstring());
    command += L" --no-first-run --no-default-browser-check --disable-background-mode";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    const BOOL launched = CreateProcessW(
        edge->c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process);
    if (!launched) {
        runtime->stop_server();
        const auto error = GetLastError();
        const auto message = L"Unable to start the EngCoder desktop web surface (Edge error " +
            std::to_wstring(error) + L"). The diagnostic native UI will open instead.";
        (void)MessageBoxW(nullptr, message.c_str(), L"EngCoder desktop UI", MB_OK | MB_ICONWARNING);
        return run_native_ui();
    }

    CloseHandle(process.hThread);

    // Edge can keep its browser process alive after the app-mode window closes.
    // Keep the dedicated Edge process tree in a kill-on-close job and use the
    // desktop page lifecycle signal to stop the embedded HTTP server when the
    // actual EngCoder window goes away. A short grace period tolerates reloads.
    HANDLE edge_job = CreateJobObjectW(nullptr, nullptr);
    if (edge_job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                edge_job,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)) ||
            !AssignProcessToJobObject(edge_job, process.hProcess)) {
            CloseHandle(edge_job);
            edge_job = nullptr;
        }
    }

    bool desktop_seen = false;
    std::optional<std::chrono::steady_clock::time_point> closed_since;
    constexpr auto close_grace = 1500ms;

    for (;;) {
        const DWORD wait = WaitForSingleObject(process.hProcess, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_FAILED) break;

        if (!runtime->desktop_client_seen()) continue;
        desktop_seen = true;
        if (runtime->desktop_client_open()) {
            closed_since.reset();
            continue;
        }

        if (!closed_since) closed_since = std::chrono::steady_clock::now();
        if (desktop_seen && std::chrono::steady_clock::now() - *closed_since >= close_grace) break;
    }

    // Stop accepting API/UI traffic first, then tear down the dedicated Edge
    // tree so closing EngCoder never leaves its loopback server behind.
    runtime->stop_server();
    if (edge_job != nullptr) {
        (void)TerminateJobObject(edge_job, 0);
        CloseHandle(edge_job);
    } else if (WaitForSingleObject(process.hProcess, 250) == WAIT_TIMEOUT) {
        (void)TerminateProcess(process.hProcess, 0);
    }
    (void)WaitForSingleObject(process.hProcess, 2000);
    CloseHandle(process.hProcess);
    return 0;
}
#endif

} // namespace

int run_native_ui() {
#if defined(_WIN32)
    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    std::unique_ptr<GuiController> controller_storage;
    try {
        controller_storage = std::make_unique<GuiController>();
    } catch (const std::exception& error) {
        SDL_Log("EngCoder runtime startup failed: %s", error.what());
        (void)SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "EngCoder startup failed",
            error.what(),
            nullptr);
        SDL_Quit();
        return 1;
    } catch (...) {
        SDL_Log("EngCoder runtime startup failed with an unexpected exception");
        (void)SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR,
            "EngCoder startup failed",
            "Unexpected runtime initialization failure. See the build/run log for details.",
            nullptr);
        SDL_Quit();
        return 1;
    }
    GuiController& controller = *controller_storage;

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("EngCoder", 1440, 900, flags);
    if (window == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    (void)SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto fonts = load_gui_fonts(io);

    UiScaleState ui_scale{};
    ui_scale.refresh(window);
    NativeTheme native_theme = NativeTheme::engcoder_dark;
    bool system_theme_light = windows_system_uses_light_theme();
    auto last_system_theme_check = std::chrono::steady_clock::now();
    configure_style(ui_scale.effective_scale, native_theme);

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    std::array<char, 512> model_token{};
    std::array<char, 16 * 1024> composer{};
    NativeProfileEditor native_profile_editor{};
    int selected_native_model = 0;
    std::string native_profile_editor_id;
    bool done = false;
    bool scale_dirty = false;
    bool show_debug = true;
    bool tools_only = false;

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
            if ((event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                 event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) &&
                event.window.windowID == SDL_GetWindowID(window)) {
                scale_dirty = true;
            }
        }

        if (native_theme == NativeTheme::windows_system &&
            std::chrono::steady_clock::now() - last_system_theme_check > 750ms) {
            const bool current_light = windows_system_uses_light_theme();
            if (current_light != system_theme_light) {
                system_theme_light = current_light;
                scale_dirty = true;
            }
            last_system_theme_check = std::chrono::steady_clock::now();
        }

        if (scale_dirty) {
            ui_scale.refresh(window);
            configure_style(ui_scale.effective_scale, native_theme);
            scale_dirty = false;
        }

        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0U) {
            SDL_Delay(10);
            continue;
        }

        controller.drain_updates();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const auto viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin(
            "EngCoderRoot",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus);

        const float scale = ui_scale.effective_scale;
        const float sidebar_width = 300.0F * scale;
        ImGui::BeginChild("Sidebar", ImVec2(sidebar_width, 0.0F), ImGuiChildFlags_Borders);
        ImGui::PushFont(nullptr, kBaseFontSize * 1.22F);
        ImGui::TextUnformatted("EngCoder");
        ImGui::PopFont();
        ImGui::TextDisabled("%s", controller.status().c_str());
        ImGui::Spacing();

        if (ImGui::Button("New session", ImVec2(-1.0F, 0.0F))) controller.new_session();
        ImGui::Checkbox("Debug inspector", &show_debug);

        ImGui::Spacing();
        ImGui::SeparatorText("Runtime");
        ImGui::Text("Native: embedded");
        ImGui::TextWrapped("Decision: %s", controller.decision_mode().c_str());
        ImGui::TextDisabled("Workspace");
        ImGui::TextWrapped("%s", controller.workspace().c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Server / Web");
        if (controller.server_running()) {
            ImGui::Text("Server: running");
            ImGui::TextWrapped("%s", controller.server_url().c_str());
            if (ImGui::Button("Stop server", ImVec2(-1.0F, 0.0F))) controller.stop_server();
        } else {
            ImGui::TextDisabled("Server: off (not required by native GUI)");
            if (ImGui::Button("Start server", ImVec2(-1.0F, 0.0F))) (void)controller.start_server();
        }
        if (ImGui::Button("Open Web UI", ImVec2(-1.0F, 0.0F))) (void)controller.open_web();
        ImGui::TextWrapped("%s", controller.service_message().c_str());

        ImGui::Spacing();
        ImGui::SeparatorText("Display");
        int theme_index = static_cast<int>(native_theme);
        const char* themes[] = {"EngCoder Dark", "EngCoder Light", "Windows System", "Windows Dark", "Windows Light", "Matrix Green"};
        if (ImGui::Combo("Theme", &theme_index, themes, 6)) {
            native_theme = static_cast<NativeTheme>(theme_index);
            system_theme_light = windows_system_uses_light_theme();
            scale_dirty = true;
        }
        int preset = static_cast<int>(ui_scale.preset);
        const char* presets[] = {"Compact", "Standard", "Large"};
        if (ImGui::Combo("UI size", &preset, presets, 3)) {
            ui_scale.preset = static_cast<UiSizePreset>(preset);
            scale_dirty = true;
        }
        ImGui::TextDisabled("Monitor %.0f%%  Effective %.0f%%",
            ui_scale.content_scale * 100.0F,
            ui_scale.effective_scale * 100.0F);

        ImGui::Spacing();
        ImGui::SeparatorText("Model");
        ImGui::TextDisabled("API token");
        const bool token_entered = ImGui::InputTextWithHint(
            "##model_api_token",
            "Paste Bionic/LM Studio token",
            model_token.data(),
            model_token.size(),
            ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        const float token_button_width = std::max(80.0F * scale, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5F);
        if ((ImGui::Button("Apply token", ImVec2(token_button_width, 0.0F)) || token_entered) && !controller.busy()) {
            if (controller.apply_model_token(std::string{model_token.data()})) model_token.fill('\0');
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear token", ImVec2(-1.0F, 0.0F)) && !controller.busy()) {
            (void)controller.clear_model_token();
            model_token.fill('\0');
        }
        ImGui::TextDisabled("Token: %s", controller.token_configured() ? controller.token_source().c_str() : "not configured");
        ImGui::TextWrapped("GUI token stays in memory for this run. CLI/headless modes still use LM_API_TOKEN or ENGCODER_MODEL_SERVER_TOKEN.");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", controller.model().c_str());
        ImGui::TextDisabled("Backend");
        ImGui::TextWrapped("%s", controller.backend().c_str());
        ImGui::TextDisabled("Session");
        ImGui::TextWrapped("%s", controller.session_id().c_str());

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Native Model Library", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto& library = controller.native_library();
            const auto models = library.value("models", nlohmann::json::array());
            const auto runtime = library.value("qwen_flash", nlohmann::json::object());
            const auto runtime_install = library.value("runtime_install", nlohmann::json::object());
            const auto install = library.value("install", nlohmann::json::object());

            ImGui::TextWrapped("KeyMaker: %s", controller.foundation_model().c_str());
            ImGui::TextDisabled("Qwen Flash runtime");
            ImGui::TextWrapped("%s", runtime.value("running", false)
                ? "Running"
                : (runtime.contains("server") && !runtime["server"].is_null()
                    ? (runtime.contains("last_error") && !runtime["last_error"].is_null() ? "Available · last startup failed" : "Available · stopped")
                    : "Backend not installed"));
            if (runtime.contains("startup_note") && runtime["startup_note"].is_string()) {
                ImGui::TextDisabled("%s", runtime["startup_note"].get<std::string>().c_str());
            }
            if (runtime.contains("effective_profile") && runtime["effective_profile"].is_object() && !runtime["effective_profile"].empty()) {
                const auto& effective = runtime["effective_profile"];
                const auto loaded_percent = effective.value("gpu_offload_percent", 50U);
                const auto loaded_ngl = effective.value("gpu_layers", std::string{"auto"});
                ImGui::TextDisabled("Launch GPU offload: %u%% (old %.1f%%)  ·  -ngl %s",
                    loaded_percent, static_cast<double>(loaded_percent) * 0.6, loaded_ngl.c_str());
            }

            const bool native_locked = controller.busy() || controller.native_busy();
            const bool runtime_current = runtime.value("runtime_verified", false);
            ImGui::BeginDisabled(native_locked || runtime_current || runtime_install.value("state", std::string{}) == "installing");
            const char* runtime_action_label = runtime_current
                ? "Qwen Runtime Backend Current"
                : ((runtime.contains("server") && !runtime["server"].is_null())
                    ? "Update Qwen Runtime Backend"
                    : "Install Qwen Runtime Backend");
            if (ImGui::Button(runtime_action_label, ImVec2(-1.0F, 0.0F))) controller.provision_native_runtime();
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh models")) controller.refresh_native_library();

            if (runtime_install.value("state", std::string{}) == "installing") {
                const float runtime_progress = std::clamp(runtime_install.value("percent", 0.0F) / 100.0F, 0.0F, 1.0F);
                ImGui::TextWrapped("%s", runtime_install.value("message", std::string{"Updating Qwen Runtime Backend..."}).c_str());
                ImGui::ProgressBar(runtime_progress, ImVec2(-1.0F, 0.0F));
                if (runtime_install.contains("detail") && runtime_install["detail"].is_string()) {
                    ImGui::TextDisabled("%s", runtime_install["detail"].get<std::string>().c_str());
                }
            } else if (runtime_install.value("state", std::string{}) == "failed") {
                ImGui::TextWrapped("Runtime backend update failed: %s", runtime_install.value("error", std::string{"unknown error"}).c_str());
            }
            if (install.value("state", std::string{}) == "installing") {
                ImGui::TextWrapped("Downloading: %s", install.value("display_name", install.value("model", std::string{"native model"})).c_str());
            } else if (install.value("state", std::string{}) == "failed") {
                ImGui::TextWrapped("Download failed: %s", install.value("error", std::string{"unknown error"}).c_str());
            }

            if (models.empty()) {
                ImGui::TextDisabled("No native model profiles registered yet.");
            } else {
                selected_native_model = std::clamp(selected_native_model, 0, static_cast<int>(models.size()) - 1);
                const auto selected_name = models.at(static_cast<std::size_t>(selected_native_model)).value(
                    "display_name", models.at(static_cast<std::size_t>(selected_native_model)).value("id", std::string{"Native model"}));
                if (ImGui::BeginCombo("Model", selected_name.c_str())) {
                    for (std::size_t i = 0; i < models.size(); ++i) {
                        const auto name = models.at(i).value("display_name", models.at(i).value("id", std::string{"Native model"}));
                        const bool selected = static_cast<int>(i) == selected_native_model;
                        if (ImGui::Selectable(name.c_str(), selected)) {
                            selected_native_model = static_cast<int>(i);
                            native_profile_editor_id.clear();
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const auto& selected_model = models.at(static_cast<std::size_t>(selected_native_model));
                const auto model_id = selected_model.value("id", std::string{});
                const auto model_uri = selected_model.value("uri", std::string{"qwenflash://"} + model_id);
                if (native_profile_editor_id != model_id) {
                    native_profile_editor.load(selected_model);
                    native_profile_editor_id = model_id;
                }

                const auto cache = selected_model.value("cache", nlohmann::json::object());
                const bool installed = cache.value("installed", false);
                const bool active = selected_model.value("active", false);
                const auto cached_bytes = cache.value("cached_bytes", std::uint64_t{});
                ImGui::Text("State: %s", active ? "Loaded" : (installed ? "Installed" : "Available"));
                ImGui::TextDisabled("Cache: %.2f GiB", static_cast<double>(cached_bytes) / (1024.0 * 1024.0 * 1024.0));

                ImGui::BeginDisabled(native_locked || installed || install.value("state", std::string{}) == "installing");
                if (ImGui::Button("Download", ImVec2(-1.0F, 0.0F))) controller.install_native_model(model_uri);
                ImGui::EndDisabled();
                const std::string local_override{native_profile_editor.local_path.data()};
                const bool can_load = installed || !local_override.empty();
                ImGui::BeginDisabled(native_locked || active || !can_load);
                if (ImGui::Button("Load", ImVec2(-1.0F, 0.0F))) {
                    controller.load_native_model(model_uri, local_override);
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(native_locked || !active);
                if (ImGui::Button("Reload / Apply settings", ImVec2(-1.0F, 0.0F))) {
                    controller.reload_native_model(model_uri, native_profile_editor.json(), local_override);
                }
                if (ImGui::Button("Unload", ImVec2(-1.0F, 0.0F))) controller.unload_native_model();
                ImGui::EndDisabled();
                ImGui::BeginDisabled(native_locked || (!installed && !active));
                if (ImGui::Button("Use as KeyMaker", ImVec2(-1.0F, 0.0F))) controller.set_foundation_model(model_uri);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(native_locked || install.value("state", std::string{}) == "installing");
                if (ImGui::Button("Clear temp cache", ImVec2(-1.0F, 0.0F))) controller.clear_native_model_cache(model_uri);
                ImGui::EndDisabled();
                ImGui::BeginDisabled(native_locked || active || !installed || install.value("state", std::string{}) == "installing");
                if (ImGui::Button("Delete downloaded model...", ImVec2(-1.0F, 0.0F))) ImGui::OpenPopup("Confirm delete model weights");
                ImGui::EndDisabled();
                if (ImGui::BeginPopupModal("Confirm delete model weights", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("Delete the downloaded model weights for %s?", selected_name.c_str());
                    ImGui::TextWrapped("This cannot be undone. The model profile and settings will be preserved.");
                    if (ImGui::Button("Delete weights")) { controller.delete_native_model_weights(model_uri); ImGui::CloseCurrentPopup(); }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                if (ImGui::TreeNodeEx("Runtime profile", ImGuiTreeNodeFlags_DefaultOpen)) {
                    const auto default_button = [&](const char* id, auto&& reset) {
                        ImGui::SameLine();
                        ImGui::PushID(id);
                        if (ImGui::SmallButton("Default")) reset();
                        ImGui::PopID();
                    };
                    const char* source_names[] = {"Hugging Face", "Local GGUF"};
                    ImGui::Combo("Source", &native_profile_editor.source, source_names, 2); default_button("source", [&]{ native_profile_editor.source = 0; });
                    ImGui::InputText("Profile ID", native_profile_editor.id.data(), native_profile_editor.id.size());
                    ImGui::InputText("Display name", native_profile_editor.display_name.data(), native_profile_editor.display_name.size()); default_button("display", [&]{ copy_text(native_profile_editor.display_name, "Qwen3.8 Flash Next · UD-Q2_K_XL"); });
                    if (native_profile_editor.source == 0) {
                        ImGui::InputText("HF repo", native_profile_editor.repo.data(), native_profile_editor.repo.size()); default_button("repo", [&]{ copy_text(native_profile_editor.repo, "unsloth/Qwen3.8-Flash-Next-GGUF"); });
                        ImGui::InputText("Quant", native_profile_editor.quant.data(), native_profile_editor.quant.size()); default_button("quant", [&]{ copy_text(native_profile_editor.quant, "UD-Q2_K_XL"); });
                    } else {
                        ImGui::InputText("GGUF path", native_profile_editor.local_path.data(), native_profile_editor.local_path.size()); default_button("gguf", [&]{ copy_text(native_profile_editor.local_path, ""); });
                    }

                    const char* policy_names[] = {"Recommended · QFR tuned", "Automatic · hardware-aware", "Manual · full control"};
                    ImGui::Combo("Runtime tuning", &native_profile_editor.runtime_policy, policy_names, 3); default_button("policy", [&]{ native_profile_editor.runtime_policy = 0; });

                    const int context_presets[] = {81920, 98304, 131072, 196608, 262144};
                    const char* context_labels[] = {"80K Recommended", "96K High context", "128K", "192K", "256K Native Max"};
                    int preset_index = -1;
                    for (int i = 0; i < 5; ++i) {
                        if (native_profile_editor.context_tokens == context_presets[i]) preset_index = i;
                    }
                    if (ImGui::Combo("Context preset", &preset_index, context_labels, 5) && preset_index >= 0) {
                        native_profile_editor.context_tokens = context_presets[preset_index];
                    }
                    ImGui::InputInt("Context tokens", &native_profile_editor.context_tokens, 4096, 16384); default_button("context", [&]{ native_profile_editor.context_tokens = 81920; });
                    native_profile_editor.context_tokens = std::clamp(native_profile_editor.context_tokens, 4096, 262144);
                    ImGui::SliderInt("Parallel slots", &native_profile_editor.parallel_slots, 1, 4); default_button("slots", [&]{ native_profile_editor.parallel_slots = 1; });
                    const int effective_context = native_profile_editor.context_tokens / std::max(1, native_profile_editor.parallel_slots);
                    if (effective_context < 75'000) {
                        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F), "Effective / slot: %d · below 75K target", effective_context);
                    } else {
                        ImGui::Text("Effective / slot: %d tokens", effective_context);
                    }
                    ImGui::InputInt("Output/tool reserve", &native_profile_editor.reserve_tokens, 1024, 4096); default_button("reserve", [&]{ native_profile_editor.reserve_tokens = 4096; });
                    ImGui::Checkbox("Automatic compaction", &native_profile_editor.auto_compact); default_button("compact-auto", [&]{ native_profile_editor.auto_compact = true; });
                    ImGui::SliderFloat("Compact trigger", &native_profile_editor.compact_trigger, 0.50F, 0.98F, "%.2f"); default_button("compact-trigger", [&]{ native_profile_editor.compact_trigger = 0.97F; });
                    ImGui::SliderFloat("Compact target", &native_profile_editor.compact_target, 0.25F, 0.90F, "%.2f"); default_button("compact-target", [&]{ native_profile_editor.compact_target = 0.72F; });

                    const bool manual_runtime = native_profile_editor.runtime_policy == 2;
                    auto promote_manual = [&] {
                        if (native_profile_editor.runtime_policy != 2) native_profile_editor.runtime_policy = 2;
                    };
                    if (ImGui::InputInt("CPU threads", &native_profile_editor.threads, 1, 4)) promote_manual(); default_button("threads", [&]{ native_profile_editor.threads = 12; promote_manual(); });
                    if (ImGui::InputInt("Batch threads", &native_profile_editor.batch_threads, 1, 4)) promote_manual(); default_button("batch-threads", [&]{ native_profile_editor.batch_threads = 12; promote_manual(); });
                    if (ImGui::InputInt("Batch size", &native_profile_editor.batch_size, 32, 256)) promote_manual(); default_button("batch", [&]{ native_profile_editor.batch_size = 1024; promote_manual(); });
                    if (ImGui::InputInt("Micro-batch", &native_profile_editor.ubatch_size, 32, 128)) promote_manual(); default_button("ubatch", [&]{ native_profile_editor.ubatch_size = 256; promote_manual(); });
                    if (ImGui::SliderInt("GPU layer offload %", &native_profile_editor.gpu_offload_percent, 0, 100)) {
                        promote_manual();
                        copy_text(native_profile_editor.gpu_layers, "auto");
                    }
                    default_button("gpu-percent", [&]{ native_profile_editor.gpu_offload_percent = 50; copy_text(native_profile_editor.gpu_layers, "auto"); promote_manual(); });
                    ImGui::TextDisabled("50%% default; 100%% uses the safe Qwen GPU-layer budget (not total VRAM).");
                    if (ImGui::InputInt("Model layer hint", &native_profile_editor.gpu_layer_count_hint, 1, 4)) promote_manual(); default_button("gpu-hint", [&]{ native_profile_editor.gpu_layer_count_hint = 48; promote_manual(); });
                    if (ImGui::InputText("Exact GPU layers", native_profile_editor.gpu_layers.data(), native_profile_editor.gpu_layers.size())) promote_manual(); default_button("gpu-exact", [&]{ copy_text(native_profile_editor.gpu_layers, "auto"); promote_manual(); });
                    ImGui::Checkbox("FlashAttention", &native_profile_editor.flash_attention); default_button("flash", [&]{ native_profile_editor.flash_attention = true; promote_manual(); });
                    ImGui::InputText("KV K type", native_profile_editor.kv_type_k.data(), native_profile_editor.kv_type_k.size()); default_button("kv-k", [&]{ copy_text(native_profile_editor.kv_type_k, "q4_0"); promote_manual(); });
                    ImGui::InputText("KV V type", native_profile_editor.kv_type_v.data(), native_profile_editor.kv_type_v.size()); default_button("kv-v", [&]{ copy_text(native_profile_editor.kv_type_v, "q4_0"); promote_manual(); });
                    ImGui::Checkbox("KV GPU offload", &native_profile_editor.kv_offload); default_button("kv-offload", [&]{ native_profile_editor.kv_offload = false; promote_manual(); });
                    ImGui::InputInt("KV GPU layers", &native_profile_editor.kv_gpu_layers, 1, 4); default_button("kv-layers", [&]{ native_profile_editor.kv_gpu_layers = 0; promote_manual(); });
                    ImGui::Checkbox("Pinned CPU KV", &native_profile_editor.kv_cpu_pinned); default_button("kv-pinned", [&]{ native_profile_editor.kv_cpu_pinned = true; promote_manual(); });
                    ImGui::Checkbox("Phase-aware workspace", &native_profile_editor.phase_aware_workspace); default_button("phase", [&]{ native_profile_editor.phase_aware_workspace = true; promote_manual(); });
                    ImGui::InputText("Load mode", native_profile_editor.load_mode.data(), native_profile_editor.load_mode.size()); default_button("load-mode", [&]{ copy_text(native_profile_editor.load_mode, "mmap"); promote_manual(); });
                    ImGui::InputText("Lazy mode", native_profile_editor.lazy_mode.data(), native_profile_editor.lazy_mode.size()); default_button("lazy-mode", [&]{ copy_text(native_profile_editor.lazy_mode, "on"); promote_manual(); });
                    ImGui::InputText("Tensor override", native_profile_editor.tensor_override.data(), native_profile_editor.tensor_override.size()); default_button("tensor", [&]{ copy_text(native_profile_editor.tensor_override, "per_layer_token_embd.weight=CPU"); promote_manual(); });
                    ImGui::InputInt("Prompt cache MiB", &native_profile_editor.prompt_cache_mib, 256, 1024); default_button("prompt-cache", [&]{ native_profile_editor.prompt_cache_mib = 2048; promote_manual(); });
                    ImGui::Checkbox("llama context shift", &native_profile_editor.context_shift); default_button("ctx-shift", [&]{ native_profile_editor.context_shift = false; promote_manual(); });
                    ImGui::Checkbox("Preserve reasoning", &native_profile_editor.reasoning_preserve); default_button("reasoning", [&]{ native_profile_editor.reasoning_preserve = false; promote_manual(); });
                    const char* cache_mode_names[] = {"Auto · compatibility-qualified", "Off · stable baseline", "On · force experimental"};
                    ImGui::Combo("Expert cache mode", &native_profile_editor.expert_cache_mode, cache_mode_names, 3); default_button("expert-mode", [&]{ native_profile_editor.expert_cache_mode = 0; promote_manual(); });
                    ImGui::InputInt("MoE VRAM cache MiB", &native_profile_editor.expert_cache_mib, 512, 1024); default_button("expert-cache", [&]{ native_profile_editor.expert_cache_mib = 6144; promote_manual(); });
                    ImGui::InputInt("Pinned host MiB", &native_profile_editor.pinned_host_mib, 1024, 2048); default_button("pinned", [&]{ native_profile_editor.pinned_host_mib = 12288; promote_manual(); });
                    ImGui::Checkbox("Early expert router", &native_profile_editor.early_router); default_button("router", [&]{ native_profile_editor.early_router = false; promote_manual(); });
                    if (!manual_runtime) {
                        ImGui::TextDisabled("Editing a tuning value switches this profile to Manual. GPU layer % is honored by every policy.");
                    }
                    ImGui::TextDisabled("GPU layer offload is not total VRAM percentage; expert/KV/workspace memory is separate.");

                    ImGui::Checkbox("Speculative / MTP", &native_profile_editor.speculative_enabled); default_button("spec", [&]{ native_profile_editor.speculative_enabled = false; });
                    if (native_profile_editor.speculative_enabled) {
                        ImGui::InputText("Spec type", native_profile_editor.speculative_type.data(), native_profile_editor.speculative_type.size()); default_button("spec-type", [&]{ copy_text(native_profile_editor.speculative_type, "draft-mtp"); });
                        ImGui::InputText("Spec HF repo", native_profile_editor.speculative_repo.data(), native_profile_editor.speculative_repo.size()); default_button("spec-repo", [&]{ copy_text(native_profile_editor.speculative_repo, ""); });
                        ImGui::InputText("Spec model", native_profile_editor.speculative_model.data(), native_profile_editor.speculative_model.size()); default_button("spec-model", [&]{ copy_text(native_profile_editor.speculative_model, ""); });
                        ImGui::TextDisabled("Use a dedicated MTP/draft sidecar; never point the draft model at the main Qwen repository.");
                        ImGui::InputInt("Draft tokens", &native_profile_editor.speculative_tokens, 1, 2); default_button("spec-tokens", [&]{ native_profile_editor.speculative_tokens = 2; });
                    }

                    if (ImGui::Button("Reset all settings...", ImVec2(-1.0F, 0.0F))) ImGui::OpenPopup("Confirm reset native settings");
                    if (ImGui::BeginPopupModal("Confirm reset native settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::TextWrapped("Reset every runtime setting for this model to EngCoder defaults?");
                        ImGui::TextWrapped("Model identity, source path and downloaded weights are preserved.");
                        if (ImGui::Button("Reset settings")) { native_profile_editor.reset_defaults(true); ImGui::CloseCurrentPopup(); }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::BeginDisabled(native_locked || active);
                    if (ImGui::Button("Save profile", ImVec2(-1.0F, 0.0F))) {
                        controller.register_native_model(native_profile_editor.json());
                        native_profile_editor_id.clear();
                    }
                    ImGui::EndDisabled();
                    ImGui::TreePop();
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginGroup();
        const float composer_height = 138.0F * scale;
        const float debug_height = show_debug ? std::max(190.0F * scale, ImGui::GetContentRegionAvail().y * 0.33F) : 0.0F;
        const float main_height = ImGui::GetContentRegionAvail().y;
        const float conversation_height = std::max(
            140.0F * scale,
            main_height - composer_height - (show_debug ? debug_height + 8.0F * scale : 0.0F));

        ImGui::BeginChild(
            "Conversation",
            ImVec2(0.0F, conversation_height),
            ImGuiChildFlags_Borders,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (controller.messages().empty()) {
            const auto region = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + region.y * 0.35F);
            const char* title = "What are we building?";
            ImGui::PushFont(nullptr, kBaseFontSize * 1.35F);
            const auto text_width = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0F, (region.x - text_width) * 0.5F));
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::TextDisabled("Native runtime is in-process. Server and web are optional.");
        } else {
            for (const auto& message : controller.messages()) message_bubble(message, scale, fonts.ui);
        }
        if (controller.consume_scroll_request()) ImGui::SetScrollHereY(1.0F);
        ImGui::EndChild();

        if (show_debug) {
            ImGui::Spacing();
            debug_inspector(controller, debug_height, tools_only, fonts.mono);
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-108.0F * scale);
        ImGui::InputTextMultiline(
            "##composer",
            composer.data(),
            composer.size(),
            ImVec2(-108.0F * scale, 96.0F * scale),
            ImGuiInputTextFlags_AllowTabInput);
        ImGui::SameLine();

        if (controller.busy()) {
            if (ImGui::Button("Stop", ImVec2(94.0F * scale, 96.0F * scale))) controller.stop_task();
        } else if (ImGui::Button("Send", ImVec2(94.0F * scale, 96.0F * scale))) {
            const std::string prompt{composer.data()};
            if (!prompt.empty()) {
                controller.send(prompt);
                composer.fill('\0');
            }
        }
        ImGui::EndGroup();
        ImGui::End();

        ImGui::Render();
        (void)SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        const auto clear = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        (void)SDL_SetRenderDrawColorFloat(renderer, clear.x, clear.y, clear.z, clear.w);
        (void)SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}


int main(int argc, char** argv) {
#if defined(_WIN32)
    HANDLE desktop_mutex = CreateMutexW(nullptr, FALSE, L"Local\\EngCoder.Desktop.0.4");
    if (desktop_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        (void)MessageBoxW(nullptr, L"EngCoder is already running.", L"EngCoder", MB_OK | MB_ICONINFORMATION);
        CloseHandle(desktop_mutex);
        return 0;
    }
#endif

    [[maybe_unused]] bool native_ui = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--native-ui") {
            native_ui = true;
            break;
        }
    }
    int result = 0;
#if defined(_WIN32)
    result = native_ui ? run_native_ui() : run_shared_desktop();
    if (desktop_mutex != nullptr) CloseHandle(desktop_mutex);
#else
    result = run_native_ui();
#endif
    return result;
}
