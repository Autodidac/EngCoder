#pragma once

#include <memory>

#include "engcoder/core/config.hpp"
#include "engcoder/model/internal_model_host.hpp"
#include "engcoder/model/lm_studio_provider.hpp"
#include "engcoder/model/model_provider.hpp"
#include "engcoder/model/qwen_output_journal.hpp"
#include "engcoder/model/native_model_manager.hpp"

namespace engcoder::model {

class MultiplexProvider final : public IModelProvider {
public:
    MultiplexProvider(core::Config config, InternalModelHost& internal_host, NativeModelManager& native_models);

    [[nodiscard]] std::expected<ModelResponse, core::Error> complete(const ModelRequest&) override;
    [[nodiscard]] std::expected<ModelResponse, core::Error> complete_stream(const ModelRequest&, const StreamCallback&) override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;
    [[nodiscard]] std::expected<json, core::Error> discover() override;
    [[nodiscard]] std::expected<json, core::Error> prepare(std::string_view model) override;
    [[nodiscard]] std::expected<json, core::Error> residency() override;
    [[nodiscard]] std::expected<json, core::Error> release_idle() override;

    // Resolves only already-known local state. It never starts a model or
    // performs provider discovery, so UI/status callers can use it cheaply.
    [[nodiscard]] std::string resolve_model_hint(std::string_view requested) const;

    [[nodiscard]] LmStudioProvider& external() noexcept { return external_; }
    [[nodiscard]] NativeModelManager& native_models() noexcept { return native_models_; }
    [[nodiscard]] QwenOutputSnapshot qwen_output_after(std::uint64_t sequence) const { return qwen_output_.after(sequence); }

private:
    [[nodiscard]] bool internal_request(std::string_view model) const noexcept;
    [[nodiscard]] bool native_request(std::string_view model) const noexcept;
    [[nodiscard]] std::string resolve_request_model(std::string_view requested) const;
    [[nodiscard]] std::expected<LmStudioProvider*, core::Error> internal_provider();
    [[nodiscard]] std::expected<LmStudioProvider*, core::Error> native_provider(std::string_view model, std::chrono::steady_clock::time_point deadline, std::stop_token stop);

    QwenOutputJournal qwen_output_;
    core::Config config_;
    InternalModelHost& internal_host_;
    NativeModelManager& native_models_;
    LmStudioProvider external_;
    std::unique_ptr<LmStudioProvider> internal_;
    std::unique_ptr<LmStudioProvider> native_transport_;
    std::string internal_url_;
    std::string native_url_;
    std::string native_model_;
};

} // namespace engcoder::model
