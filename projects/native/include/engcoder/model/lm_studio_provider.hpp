#pragma once
#include "engcoder/core/config.hpp"
#include "engcoder/model/model_provider.hpp"

#include <atomic>
#include <chrono>
#include <mutex>

namespace engcoder::model {

class LmStudioProvider final : public IModelProvider {
public:
    explicit LmStudioProvider(core::Config config);
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete(const ModelRequest&) override;
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_stream(const ModelRequest&, const StreamCallback&) override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::expected<json, core::Error> discover() override;
    [[nodiscard]] std::expected<json, core::Error> native_chat(std::string_view input, const std::vector<std::string>& integrations);
    void set_token(std::string token);
    [[nodiscard]] bool token_configured() const;
    [[nodiscard]] const std::string& base_url() const noexcept { return config_.lm_studio_url; }
private:
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_chat(const ModelRequest&);
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_chat_stream(const ModelRequest&, const StreamCallback&);
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_responses(const ModelRequest&);
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_responses_stream(const ModelRequest&, const StreamCallback&);
    [[nodiscard]] std::expected<std::string, core::Error> effective_model(std::stop_token stop = {}, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
    [[nodiscard]] std::expected<std::string, core::Error> model_for_request(const ModelRequest& request);
    [[nodiscard]] std::expected<void, core::Error> ensure_model_loaded(std::string_view model, std::stop_token stop = {}, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
    [[nodiscard]] std::expected<json, core::Error> http_json(std::string_view path, const json& body, std::stop_token stop = {}, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) const;
    [[nodiscard]] std::expected<json, core::Error> http_get_json(std::string_view path, std::stop_token stop = {}, std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) const;
    [[nodiscard]] std::expected<json, core::Error> discover_with_deadline(std::stop_token stop, std::chrono::steady_clock::time_point deadline);
    [[nodiscard]] std::expected<void, core::Error> ensure_generation_slot_available(std::stop_token stop, std::chrono::steady_clock::time_point deadline);
    void mark_generation_termination_uncertain() noexcept;
    [[nodiscard]] std::string authorization_header() const;
    core::Config config_;
    mutable std::mutex auth_mutex_;
    std::mutex resolved_model_mutex_;
    std::mutex model_load_mutex_;
    std::atomic_bool generation_termination_uncertain_{false};
    std::string resolved_model_;
};

} // namespace engcoder::model
