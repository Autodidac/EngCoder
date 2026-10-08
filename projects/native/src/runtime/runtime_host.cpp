#include "engcoder/runtime/runtime_host.hpp"

#include "engcoder/agent/agent_runtime.hpp"
#include "engcoder/agent/task_scheduler.hpp"
#include "engcoder/core/ids.hpp"
#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/controller/architect_controller.hpp"
#include "engcoder/decision/decision_engine.hpp"
#include "engcoder/learning/learning_service.hpp"
#include "engcoder/model/internal_model_host.hpp"
#include "engcoder/model/multiplex_provider.hpp"
#include "engcoder/model/native_model_manager.hpp"
#include "engcoder/models/model_registry.hpp"
#include "engcoder/residency/model_residency_manager.hpp"
#include "engcoder/training/training_service.hpp"
#include "engcoder/persistence/session_store.hpp"
#include "engcoder/server/http_server.hpp"
#include "engcoder/tools/builtin_tools.hpp"
#include "engcoder/tools/tool_registry.hpp"
#include "engcoder/workspace/checkpoint_store.hpp"
#include "engcoder/workspace/workspace.hpp"

#include <filesystem>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

namespace engcoder::runtime {
namespace {

[[nodiscard]] RunTerminal terminal_from_event(std::string_view type) {
    if (type == "task.completed") return RunTerminal::completed;
    if (type == "task.cancelled") return RunTerminal::cancelled;
    if (type == "task.awaiting_approval") return RunTerminal::awaiting_approval;
    return RunTerminal::failed;
}


} // namespace

struct RuntimeHost::Impl {
    explicit Impl(core::Config initial_config)
        : config(std::move(initial_config)),
          workspace(config.workspace),
          state_root(config.data_root),
          checkpoints(workspace, state_root, config.checkpoint_file_limit),
          store(state_root / "engcoder.db"),
          decisions(decision::DeterministicDecisionEngine{}, decision::WeightedDecisionEngine{}),
          models(config, state_root),
          residency(config.vram_budget_gib),
          internal_host(config, state_root),
          native_models(config, state_root),
          model(config, internal_host, native_models),
          addons(config, model, models),
          controller(config, model, addons, models, residency),
          learning(state_root),
          training(config, state_root, models, residency),
          tools(decisions),
          runtime(config, model, addons, controller, models, residency, training, learning, tools, store, events),
          scheduler(runtime, 1),
          server(config, scheduler, store, model, addons, controller, models, residency, training, learning, internal_host, native_models) {
        // Registry-backed controller/addon resolution allows a newly initialized
        // internal generation to become usable without restarting EngCoder.
        models.refresh();
        residency.sync(models);
        tools::register_builtin_tools(tools, workspace, checkpoints, store, model.external(), addons, config);
    }

    core::Config config;
    workspace::Workspace workspace;
    std::filesystem::path state_root;
    workspace::CheckpointStore checkpoints;
    persistence::SessionStore store;
    decision::CompositeDecisionEngine decisions;
    models::ModelRegistry models;
    residency::ModelResidencyManager residency;
    model::InternalModelHost internal_host;
    model::NativeModelManager native_models;
    model::MultiplexProvider model;
    addons::AddonRegistry addons;
    controller::ArchitectController controller;
    learning::LearningService learning;
    training::TrainingService training;
    tools::ToolRegistry tools;
    core::EventBus events;
    agent::AgentRuntime runtime;
    agent::TaskScheduler scheduler;
    server::HttpServer server;
    std::jthread server_thread;
    std::atomic_bool server_launch_finished{false};
    std::atomic_bool server_launch_result{false};
};

RuntimeHost::RuntimeHost(core::Config config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

RuntimeHost::~RuntimeHost() {
    stop_server();
}

const core::Config& RuntimeHost::config() const noexcept {
    return impl_->config;
}

std::expected<json, core::Error> RuntimeHost::model_discovery() {
    auto discovery = impl_->model.discover();
    if (!discovery) return std::unexpected(discovery.error());
    const auto configured_foundation = impl_->models.foundation_model();
    const auto effective_foundation = impl_->model.resolve_model_hint(configured_foundation);
    (*discovery)["selected_model"] = effective_foundation;
    (*discovery)["foundation_model"] = effective_foundation;
    (*discovery)["configured_foundation_model"] = configured_foundation;
    return discovery;
}

json RuntimeHost::architecture() const {
    const auto addon_description = impl_->addons.describe();
    const auto configured_foundation = impl_->models.foundation_model();
    const auto effective_foundation = impl_->model.resolve_model_hint(configured_foundation);
    return json{
        {"format", "engcoder.architecture.v4"},
        {"strategy", "architect+keymaker-foundation+on-demand-addons"},
        {"controller", impl_->controller.describe()},
        {"foundation", {
            {"id", "keymaker"},
            {"role", "resident KeyMaker language/reasoning foundation"},
            {"model", effective_foundation},
            {"configured_model", configured_foundation},
            {"auto_resolved", effective_foundation != configured_foundation},
            {"recommended_size", "250M-400M"},
            {"user_facing", true},
        }},
        {"addons", addon_description.value("addons", json::array())},
        {"model_registry", impl_->models.describe()},
        {"residency", impl_->residency.describe()},
        {"training", impl_->training.status()},
        {"learning", impl_->learning.describe()},
        {"internal_host", impl_->internal_host.describe()},
        {"native_models", impl_->native_models.describe()},
        {"tool_policy", "deterministic C++ policy remains authoritative"},
    };
}

json RuntimeHost::native_model_library() const {
    return impl_->native_models.describe();
}

std::expected<json, core::Error> RuntimeHost::register_native_model(const json& spec) {
    auto result = impl_->native_models.register_model(spec);
    if (!result) return std::unexpected(result.error());
    impl_->models.refresh();
    impl_->residency.sync(impl_->models);
    return *result;
}

std::expected<json, core::Error> RuntimeHost::provision_native_runtime() {
    return impl_->native_models.provision_runtime();
}

std::expected<json, core::Error> RuntimeHost::install_native_model(std::string_view id_or_uri) {
    return impl_->native_models.install(id_or_uri);
}

std::expected<json, core::Error> RuntimeHost::load_native_model(
    std::string_view id_or_uri,
    std::string_view local_path_override) {
    auto endpoint = local_path_override.empty()
        ? impl_->native_models.prepare(id_or_uri)
        : impl_->native_models.prepare_with_local_override(id_or_uri, std::filesystem::path{local_path_override});
    if (!endpoint) return std::unexpected(endpoint.error());
    const auto configured_foundation = impl_->models.foundation_model();
    const auto effective_foundation = impl_->model.resolve_model_hint(configured_foundation);
    return json{
        {"loaded", true},
        {"model", std::string{id_or_uri}},
        {"endpoint", *endpoint},
        {"local_path_override", local_path_override.empty() ? json(nullptr) : json(local_path_override)},
        {"effective_foundation_model", effective_foundation},
        {"library", impl_->native_models.describe()},
    };
}

void RuntimeHost::unload_native_model() {
    impl_->native_models.stop_active();
}

std::expected<json, core::Error> RuntimeHost::remove_native_model(std::string_view id_or_uri, bool delete_cache) {
    auto result = impl_->native_models.remove(id_or_uri, delete_cache);
    if (!result) return std::unexpected(result.error());
    impl_->models.refresh();
    impl_->residency.sync(impl_->models);
    return *result;
}

std::expected<json, core::Error> RuntimeHost::clear_native_model_cache(std::string_view id_or_uri) {
    return impl_->native_models.clear_transient_cache(id_or_uri);
}

std::expected<json, core::Error> RuntimeHost::delete_native_model_weights(std::string_view id_or_uri) {
    auto result = impl_->native_models.delete_downloaded_weights(id_or_uri);
    if (!result) return std::unexpected(result.error());
    impl_->models.refresh();
    impl_->residency.sync(impl_->models);
    return *result;
}

std::expected<json, core::Error> RuntimeHost::set_foundation_model(std::string_view provider_model) {
    if (!impl_->models.set_foundation_model(provider_model)) {
        return std::unexpected(core::Error{
            core::make_error_code(core::Errc::model_unavailable),
            "Foundation model is not available: " + std::string{provider_model},
        });
    }
    return json{
        {"foundation_model", impl_->models.foundation_model()},
        {"registry", impl_->models.describe()},
    };
}

std::string RuntimeHost::foundation_model() const {
    return impl_->model.resolve_model_hint(impl_->models.foundation_model());
}

std::expected<RunOutcome, core::Error> RuntimeHost::run_task(
    std::string_view project_id,
    std::string_view session_id,
    std::string_view message,
    std::stop_token stop,
    EventSink on_event) {
    agent::TaskRequest request{
        .project_id = std::string{project_id},
        .session_id = std::string{session_id},
        .task_id = core::make_id("task"),
        .user_message = std::string{message},
        .foundation_model = {},
        .controller_model = {},
        .forced_addon = {},
        .addon_models = json::object(),
        .session_instructions = {},
        .temperature = 0.15,
        .max_tokens = -1,
        .include_history = true,
        .max_history_turns = 0,
        .auto_route = true,
    };

    std::string terminal_type;
    json terminal_data = json::object();
    const auto subscription = impl_->events.subscribe([&](const core::Event& event) {
        if (event.task_id != request.task_id) {
            return;
        }
        if (on_event) {
            on_event(event);
        }
        if (event.type == "task.completed" || event.type == "task.failed" ||
            event.type == "task.cancelled" || event.type == "task.awaiting_approval") {
            terminal_type = event.type;
            terminal_data = event.data;
        }
    });

    struct SubscriptionGuard {
        core::EventBus& bus;
        std::uint64_t id{};
        ~SubscriptionGuard() { bus.unsubscribe(id); }
    } guard{impl_->events, subscription};

    const auto result = impl_->runtime.run(request, stop);
    if (!result) {
        if (terminal_type.empty()) {
            terminal_type = result.error().code == core::make_error_code(core::Errc::cancelled)
                ? "task.cancelled"
                : result.error().code == core::make_error_code(core::Errc::approval_required)
                    ? "task.awaiting_approval"
                    : "task.failed";
            terminal_data = json{{"error", result.error().message}, {"code", result.error().code.value()}};
        }

        return RunOutcome{
            .project_id = request.project_id,
            .session_id = request.session_id,
            .task_id = request.task_id,
            .terminal = terminal_from_event(terminal_type),
            .final_text = {},
            .terminal_data = std::move(terminal_data),
        };
    }

    if (terminal_type.empty()) {
        terminal_type = "task.completed";
        terminal_data = json{{"result", result->final_text}};
    }

    return RunOutcome{
        .project_id = request.project_id,
        .session_id = request.session_id,
        .task_id = request.task_id,
        .terminal = terminal_from_event(terminal_type),
        .final_text = terminal_data.value("result", result->final_text),
        .terminal_data = std::move(terminal_data),
    };
}

std::expected<std::uint16_t, core::Error> RuntimeHost::start_server() {
    if (impl_->server.running()) {
        return impl_->config.server_port;
    }
    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }

    impl_->server_launch_finished.store(false, std::memory_order_release);
    impl_->server_launch_result.store(false, std::memory_order_release);
    impl_->server_thread = std::jthread([this](std::stop_token) {
        const bool result = impl_->server.listen();
        impl_->server_launch_result.store(result, std::memory_order_release);
        impl_->server_launch_finished.store(true, std::memory_order_release);
    });

    for (int attempt = 0; attempt < 60; ++attempt) {
        if (impl_->server.running()) {
            return impl_->config.server_port;
        }
        if (impl_->server_launch_finished.load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }

    impl_->server.stop();
    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }
    return std::unexpected(core::Error{
        core::make_error_code(core::Errc::io_error),
        "Unable to bind the EngCoder web server on port " + std::to_string(impl_->config.server_port)
    });
}

void RuntimeHost::stop_server() {
    impl_->server.stop();
    if (impl_->server_thread.joinable()) {
        impl_->server_thread.join();
    }
}

bool RuntimeHost::server_running() const noexcept {
    return impl_->server.running();
}

bool RuntimeHost::desktop_client_seen() const noexcept {
    return impl_->server.desktop_client_seen();
}

bool RuntimeHost::desktop_client_open() const noexcept {
    return impl_->server.desktop_client_open();
}

std::string RuntimeHost::server_url() const {
    return "http://127.0.0.1:" + std::to_string(impl_->config.server_port);
}

std::string RuntimeHost::web_url() const {
    return server_url() + "/ui/";
}

bool RuntimeHost::serve_forever() {
    return impl_->server.listen();
}

std::string RuntimeHost::decision_mode() const {
    return "deterministic policy + local Architect controller";
}

std::string make_session_id(std::string_view prefix) {
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::random_device random;
    const auto entropy = (static_cast<std::uint64_t>(random()) << 32U) | random();

    std::ostringstream out;
    out << prefix << '-' << std::hex << now << '-' << entropy;
    return out.str();
}

} // namespace engcoder::runtime
