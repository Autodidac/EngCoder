module;
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.models;

export {
namespace threadspire::ai::models {

struct ModelProfile final {
    std::string id;
    std::string display_name;
    std::string model_name;
    std::string endpoint{"internal"};
    std::string api_key;
    std::string protocol{"internal"};
    std::vector<std::string> capabilities;
    double tokens_per_second{20.0};
    double first_token_ms{250.0};
    double quality{0.6};
    double reliability{0.9};
    std::size_t context_window{4096};
    bool enabled{true};
    bool keep_hot{false};
    bool supports_tools{false};
    bool supports_lora{true};
    bool teacher{false};
    bool verifier{false};
    bool custom_model{false};
    bool coding_agent{false};
    bool auto_discovered{false};
    // Transient readiness observed at runtime; intentionally not persisted.
    bool runtime_ready{true};
    // Last independently evaluated expert generation reflected in routing.
    std::uint64_t validated_generation{0};
    int priority{0};
};

class ModelRegistry final {
public:
    [[nodiscard]] static ModelRegistry load(const std::filesystem::path& path);
    [[nodiscard]] static ModelRegistry defaults();

    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<ModelProfile>& models() const noexcept;
    [[nodiscard]] std::vector<ModelProfile>& models() noexcept;
    [[nodiscard]] const ModelProfile* find(std::string_view id) const noexcept;
    [[nodiscard]] ModelProfile* find(std::string_view id) noexcept;

    void upsert(ModelProfile profile);
    void remove_auto_discovered();

    void record_observation(
        std::string_view id,
        double measured_tokens_per_second,
        bool success) noexcept;
    void record_feedback(std::string_view id, bool accepted) noexcept;
    [[nodiscard]] bool record_validated_promotion(
        std::string_view skill,
        std::uint64_t generation,
        std::size_t accepted_examples,
        double validation_score,
        double regression_score) noexcept;

private:
    std::vector<ModelProfile> models_;
};

[[nodiscard]] bool has_capability(
    const ModelProfile& model,
    std::string_view capability) noexcept;

} // namespace threadspire::ai::models

namespace threadspire::ai::models {

struct ProviderDiscoveryReport final {
    std::vector<ModelProfile> models;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool found_any() const noexcept {
        return !models.empty();
    }
};

struct LocalModelEndpointResponse final {
    int status_code{0};
    std::string body;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept {
        return error.empty() && status_code >= 200 && status_code < 300;
    }
};

using LocalModelEndpointRequest = std::function<LocalModelEndpointResponse(
    std::string_view method,
    std::string_view url,
    std::string_view body,
    std::chrono::milliseconds timeout)>;

struct LocalModelActivationResult final {
    bool ready{false};
    bool loaded_by_engcoder{false};
    std::string message;
};

[[nodiscard]] ProviderDiscoveryReport discover_local_models(
    std::chrono::milliseconds timeout_per_endpoint = std::chrono::milliseconds(700),
    std::stop_token stop_token = {});

[[nodiscard]] bool merge_discovered_models(
    ModelRegistry& registry,
    const std::vector<ModelProfile>& discovered);

[[nodiscard]] bool is_managed_lm_studio_teacher(
    const ModelProfile& model);

[[nodiscard]] LocalModelActivationResult ensure_managed_lm_studio_teacher_ready(
    const ModelProfile& model,
    std::chrono::milliseconds timeout = std::chrono::minutes(2),
    std::stop_token stop_token = {},
    LocalModelEndpointRequest request = {});

[[nodiscard]] ModelProfile make_manual_local_model(
    std::string endpoint,
    std::string model_name,
    std::string protocol,
    bool coding_agent,
    bool teacher);

[[nodiscard]] bool is_internal_model(const ModelProfile& model) noexcept;

} // namespace threadspire::ai::models

namespace threadspire::ai::models {

struct LocalServerProfile final {
    std::string id;
    std::string display_name;
    std::string base_url;
    std::string api_key;
    bool enabled{true};
    bool teacher{true};
    bool verifier{false};
    bool coding_agent{true};
};

struct DiscoveredLocalModel final {
    std::string server_id;
    std::string server_name;
    std::string base_url;
    std::string api_key;
    std::string model_id;
    bool teacher{true};
    bool verifier{false};
    bool coding_agent{true};
};

struct LocalServerStatus final {
    LocalServerProfile server;
    bool reachable{false};
    std::string message;
    std::vector<DiscoveredLocalModel> models;
};

struct LocalDiscoveryReport final {
    std::vector<LocalServerStatus> servers;
    std::vector<DiscoveredLocalModel> models;
};

class LocalServerHub final {
public:
    [[nodiscard]] static LocalServerHub load(const std::filesystem::path& path);
    [[nodiscard]] static LocalServerHub defaults();

    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<LocalServerProfile>& servers() const noexcept;
    [[nodiscard]] std::vector<LocalServerProfile>& servers() noexcept;

    [[nodiscard]] LocalDiscoveryReport discover(
        const std::function<bool()>& should_stop = {}) const;

    [[nodiscard]] static ModelProfile make_model_profile(
        const DiscoveredLocalModel& discovered,
        std::size_t ordinal);

private:
    std::vector<LocalServerProfile> servers_;
};

[[nodiscard]] std::string normalize_local_base_url(std::string_view url);
[[nodiscard]] std::string openai_models_url(std::string_view base_url);
[[nodiscard]] std::string openai_chat_url(std::string_view base_url);

} // namespace threadspire::ai::models
}
