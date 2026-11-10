#pragma once

#include <chrono>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <stop_token>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/core/error.hpp"
#include "engcoder/model/qwen_flash_runtime.hpp"
#include "engcoder/model/runtime_model.hpp"

namespace engcoder::model {

// Owns EngCoder-native downloadable runtimes and their catalog. This layer is
// intentionally separate from both external providers and the agent loop.
class NativeModelManager {
public:
    NativeModelManager(const core::Config& config, std::filesystem::path state_root);
    ~NativeModelManager();

    NativeModelManager(const NativeModelManager&) = delete;
    NativeModelManager& operator=(const NativeModelManager&) = delete;

    [[nodiscard]] std::vector<RuntimeModelProfile> models() const;
    [[nodiscard]] std::optional<RuntimeModelProfile> find(std::string_view id_or_uri) const;
    [[nodiscard]] std::expected<nlohmann::json, core::Error> register_model(const nlohmann::json& spec);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> install(std::string_view id_or_uri);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> provision_runtime();
    [[nodiscard]] std::expected<nlohmann::json, core::Error> remove(std::string_view id_or_uri, bool delete_cache);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> clear_transient_cache(std::string_view id_or_uri);
    [[nodiscard]] std::expected<nlohmann::json, core::Error> delete_downloaded_weights(std::string_view id_or_uri);
    [[nodiscard]] std::expected<std::string, core::Error> prepare(std::string_view id_or_uri, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(), std::stop_token stop = {});
    [[nodiscard]] std::expected<std::string, core::Error> prepare_with_local_override(
        std::string_view id_or_uri,
        const std::filesystem::path& local_path_override,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
        std::stop_token stop = {});
    [[nodiscard]] std::optional<std::string> active_model_uri() const;
    void stop_active();

    [[nodiscard]] nlohmann::json describe() const;
    [[nodiscard]] nlohmann::json runtime_types() const;
    [[nodiscard]] QwenFlashRuntime& qwen_flash() noexcept { return qwen_flash_; }

private:
    void seed_defaults();
    void load_profiles_locked();
    [[nodiscard]] std::filesystem::path manifest_path(std::string_view id) const;
    [[nodiscard]] std::string normalize_id(std::string_view value) const;
    [[nodiscard]] std::optional<RuntimeModelProfile> find_locked(std::string_view id_or_uri) const;
    void save_profile(const RuntimeModelProfile& profile) const;
    void set_install_state(nlohmann::json state);

    const core::Config& config_;
    std::filesystem::path state_root_;
    std::filesystem::path catalog_root_;
    mutable std::mutex mutex_;
    std::vector<RuntimeModelProfile> profiles_;
    std::jthread install_worker_;
    std::jthread runtime_worker_;
    nlohmann::json install_state_ = nlohmann::json{{"state", "idle"}};
    nlohmann::json runtime_install_state_ = nlohmann::json{{"state", "idle"}};
    QwenFlashRuntime qwen_flash_;
};

[[nodiscard]] bool is_native_runtime_model(std::string_view model) noexcept;

} // namespace engcoder::model
