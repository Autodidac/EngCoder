#pragma once

#include "threadspire/ai/models/model_registry.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

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

} // namespace threadspire::ai::backend
