module;
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.platform;

export import :reset;
export {
namespace threadspire::ai::app {

struct AppPaths final {
    std::filesystem::path executable;
    std::filesystem::path root;
    std::filesystem::path packs;
    std::filesystem::path outputs;
    std::filesystem::path cache;
    std::filesystem::path candidates;
    std::filesystem::path checkpoints;
    std::filesystem::path runtime;
    std::filesystem::path python;
    std::filesystem::path worker;
    std::filesystem::path internal_model_module;
    std::filesystem::path curriculum_guard_module;
    std::filesystem::path model_registry;
    std::filesystem::path local_server_registry;
    std::filesystem::path mcp_registry;
    std::filesystem::path ready_file;
    std::filesystem::path jobs;
    std::filesystem::path logs;
    std::filesystem::path experts;
    std::filesystem::path memory;
    std::filesystem::path data;
    std::filesystem::path bootstrap_dataset;
    std::filesystem::path bootstrap_anchors;
    std::filesystem::path custom_training_template;
    std::filesystem::path trajectories;
};

[[nodiscard]] AppPaths make_app_paths();
void initialize_base_resources(const AppPaths& paths);

} // namespace threadspire::ai::app

namespace threadspire::ai::platform {

struct ProcessSpec final {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::filesystem::path log_file;
    bool append_log{false};
};

struct ProcessResult final {
    bool launched{false};
    bool cancelled{false};
    int exit_code{-1};
    std::chrono::milliseconds duration{0};
    std::string error;
};

using StopPredicate = std::function<bool()>;

[[nodiscard]] ProcessResult run_process(
    const ProcessSpec& spec,
    StopPredicate should_stop = {});

[[nodiscard]] bool launch_detached(
    const ProcessSpec& spec,
    std::string& error);

class ManagedProcess final {
public:
    ManagedProcess();
    ~ManagedProcess();

    ManagedProcess(const ManagedProcess&) = delete;
    ManagedProcess& operator=(const ManagedProcess&) = delete;
    ManagedProcess(ManagedProcess&&) noexcept;
    ManagedProcess& operator=(ManagedProcess&&) noexcept;

    [[nodiscard]] bool start(const ProcessSpec& spec, std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] int exit_code() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadspire::ai::platform

namespace threadspire::ai::backend {

struct HttpHeader final {
    std::string name;
    std::string value;
};

struct HttpRequest final {
    std::string method{"GET"};
    std::string url;
    std::string body;
    std::vector<HttpHeader> headers;
    std::chrono::milliseconds timeout{std::chrono::seconds(30)};
};

struct HttpResponse final {
    int status_code{0};
    std::string body;
    std::string error;
    std::vector<HttpHeader> headers;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty() && status_code >= 200 && status_code < 300;
    }

    [[nodiscard]] std::optional<std::string> header(std::string_view name) const;
};

using HttpChunkCallback = std::function<bool(std::string_view)>;

[[nodiscard]] HttpResponse http_request(
    const HttpRequest& request,
    HttpChunkCallback on_chunk = {});

[[nodiscard]] HttpResponse http_get(
    std::string_view url,
    std::chrono::milliseconds timeout = std::chrono::seconds(30));

[[nodiscard]] HttpResponse http_post_json(
    std::string_view url,
    std::string_view body,
    std::chrono::milliseconds timeout = std::chrono::minutes(10));

} // namespace threadspire::ai::backend

namespace threadspire::ai::resource {

struct ResourceSnapshot final {
    double cpu_utilization{0.0};
    double memory_available_gib{0.0};
    double gpu_utilization{-1.0};
    double gpu_memory_available_gib{-1.0};
    bool on_ac_power{true};
    bool thermal_pressure{false};
    std::chrono::steady_clock::time_point captured_at{std::chrono::steady_clock::now()};
};

struct ResourcePolicy final {
    double maximum_background_cpu{0.75};
    double maximum_background_gpu{0.82};
    double minimum_available_memory_gib{3.0};
    double minimum_available_gpu_memory_gib{2.5};
    bool require_ac_power{false};
    bool stop_on_thermal_pressure{true};
};

struct TrainingBudget final {
    bool permitted{false};
    std::size_t max_steps{1};
    std::size_t maximum_sequence_tokens{512};
    std::size_t lora_rank{4};
    std::chrono::milliseconds retry_after{1000};
    std::string reason;
};

class ResourceGovernor final {
public:
    explicit ResourceGovernor(ResourcePolicy policy = {});

    [[nodiscard]] TrainingBudget plan_training(
        const ResourceSnapshot& snapshot,
        bool inference_active,
        std::size_t requested_steps,
        std::size_t requested_sequence_tokens,
        std::size_t requested_rank) const;
    [[nodiscard]] const ResourcePolicy& policy() const noexcept;

private:
    ResourcePolicy policy_;
};

[[nodiscard]] ResourceSnapshot sample_system_resources() noexcept;

} // namespace threadspire::ai::resource
}
