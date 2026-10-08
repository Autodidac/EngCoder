#pragma once

#include <atomic>
#include <cstdint>

#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/agent/task_scheduler.hpp"
#include "engcoder/controller/architect_controller.hpp"
#include "engcoder/core/config.hpp"
#include "engcoder/learning/learning_service.hpp"
#include "engcoder/model/internal_model_host.hpp"
#include "engcoder/model/multiplex_provider.hpp"
#include "engcoder/model/native_model_manager.hpp"
#include "engcoder/models/model_registry.hpp"
#include "engcoder/persistence/session_store.hpp"
#include "engcoder/residency/model_residency_manager.hpp"
#include "engcoder/training/training_service.hpp"

namespace httplib { class Server; }

namespace engcoder::server {

class HttpServer {
public:
    HttpServer(
        core::Config config,
        agent::TaskScheduler& scheduler,
        persistence::SessionStore& store,
        model::MultiplexProvider& model,
        addons::AddonRegistry& addons,
        controller::ArchitectController& controller,
        models::ModelRegistry& models,
        residency::ModelResidencyManager& residency,
        training::TrainingService& training,
        learning::LearningService& learning,
        model::InternalModelHost& internal_host,
        model::NativeModelManager& native_models);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    [[nodiscard]] bool listen();
    void stop();
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] bool desktop_client_seen() const noexcept { return desktop_client_seen_.load(std::memory_order_acquire); }
    [[nodiscard]] bool desktop_client_open() const noexcept { return desktop_client_open_.load(std::memory_order_acquire); }

private:
    core::Config config_;
    agent::TaskScheduler& scheduler_;
    persistence::SessionStore& store_;
    model::MultiplexProvider& model_;
    addons::AddonRegistry& addons_;
    controller::ArchitectController& controller_;
    models::ModelRegistry& models_;
    residency::ModelResidencyManager& residency_;
    training::TrainingService& training_;
    learning::LearningService& learning_;
    model::InternalModelHost& internal_host_;
    model::NativeModelManager& native_models_;
    std::atomic<httplib::Server*> active_{nullptr};
    std::atomic_bool running_{false};
    std::atomic_bool remote_serving_{false};
    std::atomic_bool desktop_client_seen_{false};
    std::atomic_bool desktop_client_open_{false};
};

} // namespace engcoder::server
