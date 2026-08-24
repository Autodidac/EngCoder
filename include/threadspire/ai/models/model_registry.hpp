#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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

private:
    std::vector<ModelProfile> models_;
};

[[nodiscard]] bool has_capability(
    const ModelProfile& model,
    std::string_view capability) noexcept;

} // namespace threadspire::ai::models
