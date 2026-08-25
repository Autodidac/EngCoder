module;
#include <filesystem>
#include <stop_token>
#include <string>

export module threadspire.media;

export import threadspire.models;
export import threadspire.providers;

export {
namespace threadspire::ai::backend {

enum class MediaKind {
    image,
    speech,
};

struct MediaRequest final {
    MediaKind kind{MediaKind::image};
    models::ModelProfile model;
    std::string prompt;
    std::filesystem::path output_root;
    std::string image_size{"1024x1024"};
    std::string voice{"alloy"};
};

struct MediaResult final {
    bool succeeded{false};
    MediaKind kind{MediaKind::image};
    std::string model_id;
    std::filesystem::path output_path;
    std::string mime_type;
    std::string error;
};

class MediaClient final {
public:
    [[nodiscard]] MediaResult generate(
        const MediaRequest& request,
        std::stop_token stop_token = {}) const;
};

class MediaTaskProvider final : public TaskProvider {
public:
    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;
    [[nodiscard]] UnifiedTaskResult execute(
        const UnifiedTaskRequest& request,
        std::stop_token stop_token) override;

private:
    MediaClient client_;
};

} // namespace threadspire::ai::backend
}
