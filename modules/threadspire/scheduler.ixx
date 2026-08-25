module;
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>

export module threadspire.scheduler;

export import :packs;
export import threadspire.core;
export import threadspire.platform;

export {
namespace threadspire::ai::runtime {

[[nodiscard]] bool backend_files_present(const app::AppPaths& paths) noexcept;

[[nodiscard]] util::OperationResult install_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string pytorch_index_url,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult verify_backend(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult initialize_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string profile,
    bool force,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult train_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::string scope,
    std::size_t maximum_steps,
    bool foreground_bootstrap,
    std::size_t bootstrap_target_steps,
    std::stop_token stop_token);

[[nodiscard]] util::OperationResult inspect_internal_model(
    const app::AppPaths& paths,
    const std::filesystem::path& log_path,
    std::stop_token stop_token);

} // namespace threadspire::ai::runtime

namespace threadspire::ai::runtime {

class BackendServer final {
public:
    explicit BackendServer(app::AppPaths paths);
    ~BackendServer();

    BackendServer(const BackendServer&) = delete;
    BackendServer& operator=(const BackendServer&) = delete;

    [[nodiscard]] bool start(
        const std::filesystem::path& python,
        std::stop_token stop_token,
        std::string& message);
    void stop() noexcept;
    [[nodiscard]] bool healthy(std::string& message) const;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string base_url() const;
    [[nodiscard]] std::filesystem::path log_path() const;

private:
    app::AppPaths paths_;
    mutable std::mutex mutex_;
    platform::ManagedProcess process_;
    std::string base_url_;
    std::filesystem::path log_path_;
};

} // namespace threadspire::ai::runtime

namespace threadspire::ai::runtime {

struct InternalModelState final {
    bool exists{false};
    bool manifest_valid{false};
    bool trained{false};
    bool bootstrap_complete{false};
    bool full_backbone_trained{false};
    bool generation_ready{false};
    bool bootstrap_policy_current{false};
    std::string generation;
    std::string bootstrap_policy;
    std::string status;
    std::string profile_name;
    std::string error;
    std::uint64_t parameter_count{0};
    std::uint64_t tokens_seen{0};
    std::size_t steps{0};
    std::size_t bootstrap_target_steps{0};
};

struct InternalModelProgress final {
    bool available{false};
    bool stale{false};
    bool resumable{false};
    bool bootstrap_complete{false};
    bool full_backbone_trained{false};
    bool generation_ready{false};
    bool bootstrap_policy_current{false};
    std::string phase;
    std::string message;
    std::string error;
    std::string generation;
    std::string bootstrap_policy;
    std::size_t step{0};
    std::size_t maximum_steps{0};
    std::size_t prior_total_steps{0};
    std::size_t total_steps{0};
    std::size_t bootstrap_target_steps{0};
    std::uint64_t tokens_seen{0};
    double percent{0.0};
    double elapsed_seconds{0.0};
    std::optional<double> eta_seconds;
    std::int64_t heartbeat_unix_ms{0};
    int process_id{0};
};

[[nodiscard]] InternalModelState inspect_internal_model_state(
    const app::AppPaths& paths) noexcept;

[[nodiscard]] InternalModelProgress read_internal_model_progress(
    const app::AppPaths& paths) noexcept;

[[nodiscard]] std::size_t default_bootstrap_target(
    const InternalModelState& state) noexcept;

} // namespace threadspire::ai::runtime
}
