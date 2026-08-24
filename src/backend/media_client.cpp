#include "threadspire/ai/backend/media_client.hpp"

#include "threadspire/ai/backend/http_client.hpp"
#include "threadspire/ai/core/json.hpp"
#include "threadspire/ai/models/local_server_hub.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace threadspire::ai::backend {
namespace {

[[nodiscard]] std::string media_url(
    const std::string_view base_url,
    const MediaKind kind) {
    auto result = models::normalize_local_base_url(base_url);
    const auto suffix = kind == MediaKind::image
        ? "/images/generations"
        : "/audio/speech";
    return result.ends_with("/v1") ? result + suffix : result + "/v1" + suffix;
}

[[nodiscard]] std::optional<std::vector<std::byte>> decode_base64(
    const std::string_view encoded) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<int, 256> lookup{};
    lookup.fill(-1);
    for (std::size_t index = 0; index < alphabet.size(); ++index) {
        lookup[static_cast<unsigned char>(alphabet[index])] = static_cast<int>(index);
    }
    lookup[static_cast<unsigned char>('-')] = 62;
    lookup[static_cast<unsigned char>('_')] = 63;

    std::vector<std::byte> output;
    output.reserve(encoded.size() * 3U / 4U);
    std::uint32_t accumulator = 0U;
    int bits = 0;
    for (const unsigned char character : encoded) {
        if (character == '=') {
            break;
        }
        if (character == ' ' || character == '\r' || character == '\n' || character == '\t') {
            continue;
        }
        const auto value = lookup[character];
        if (value < 0) {
            return std::nullopt;
        }
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFU));
        }
    }
    return output;
}

void prune_generated_outputs(const std::filesystem::path& directory) noexcept {
    struct Entry final {
        std::filesystem::path path;
        std::filesystem::file_time_type modified;
        std::uintmax_t bytes{0};
    };
    try {
        std::vector<Entry> files;
        std::uintmax_t total = 0U;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(
                 directory,
                 std::filesystem::directory_options::skip_permission_denied,
                 error), end;
             iterator != end;
             iterator.increment(error)) {
            error.clear();
            if (!iterator->is_regular_file(error) || error ||
                !iterator->path().filename().string().starts_with("generated-")) {
                continue;
            }
            const auto bytes = iterator->file_size(error);
            if (error) {
                continue;
            }
            files.push_back({iterator->path(), iterator->last_write_time(error), bytes});
            total += bytes;
        }
        std::ranges::sort(files, std::ranges::less{}, &Entry::modified);
        constexpr std::size_t maximum_files = 100U;
        constexpr std::uintmax_t maximum_bytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
        auto index = 0U;
        while ((files.size() - index > maximum_files || total > maximum_bytes) &&
               index < files.size()) {
            if (std::filesystem::remove(files[index].path, error)) {
                total = total > files[index].bytes ? total - files[index].bytes : 0U;
            }
            error.clear();
            ++index;
        }
    } catch (...) {
    }
}

[[nodiscard]] std::filesystem::path output_path(
    const MediaRequest& request,
    const std::string_view extension) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const auto directory = request.output_root /
        (request.kind == MediaKind::image ? "images" : "audio");
    std::filesystem::create_directories(directory);
    prune_generated_outputs(directory);
    return directory / ("generated-" + std::to_string(now) + std::string(extension));
}

[[nodiscard]] bool write_binary(
    const std::filesystem::path& path,
    const std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

[[nodiscard]] bool write_binary(
    const std::filesystem::path& path,
    const std::vector<std::byte>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

[[nodiscard]] std::string server_error(const core::json::Value& document) {
    const auto* error = document.find("error");
    if (error == nullptr) {
        return {};
    }
    return error->is_string()
        ? std::string(error->as_string())
        : core::json::string_or(error->find("message"), "media model returned an error");
}

} // namespace

MediaResult MediaClient::generate(
    const MediaRequest& request,
    const std::stop_token stop_token) const {
    MediaResult result{.kind = request.kind, .model_id = request.model.id};
    if (request.prompt.empty()) {
        result.error = "media prompt is empty";
        return result;
    }
    if (request.model.endpoint.empty() || request.model.endpoint == "internal") {
        result.error = "the selected model has no compatible media endpoint";
        return result;
    }
    if (stop_token.stop_requested()) {
        result.error = "media generation cancelled";
        return result;
    }

    core::json::Value::Object payload{
        {"model", request.model.model_name},
    };
    if (request.kind == MediaKind::image) {
        payload.emplace("prompt", request.prompt);
        payload.emplace("n", 1.0);
        payload.emplace("size", request.image_size);
        payload.emplace("response_format", "b64_json");
    } else {
        payload.emplace("input", request.prompt);
        payload.emplace("voice", request.voice);
        payload.emplace("response_format", "wav");
    }
    std::vector<HttpHeader> headers{
        {.name = "Content-Type", .value = "application/json; charset=utf-8"},
        {.name = "Accept", .value = request.kind == MediaKind::image
            ? "application/json"
            : "audio/wav, audio/mpeg, application/octet-stream"},
    };
    if (!request.model.api_key.empty()) {
        headers.push_back({.name = "Authorization", .value = "Bearer " + request.model.api_key});
    }
    const auto response = http_request(HttpRequest{
        .method = "POST",
        .url = media_url(request.model.endpoint, request.kind),
        .body = core::json::stringify(core::json::Value(std::move(payload))),
        .headers = std::move(headers),
        .timeout = std::chrono::minutes(30),
    });
    if (!response.succeeded()) {
        result.error = response.error.empty()
            ? "media endpoint returned HTTP " + std::to_string(response.status_code) +
                  (response.body.empty() ? std::string{} : ": " + response.body)
            : response.error;
        return result;
    }
    if (stop_token.stop_requested()) {
        result.error = "media generation cancelled";
        return result;
    }

    if (request.kind == MediaKind::speech) {
        result.mime_type = response.header("Content-Type").value_or("audio/wav");
        if (result.mime_type.find("json") != std::string::npos) {
            std::string parse_error;
            const auto document = core::json::try_parse(response.body, parse_error);
            if (document.has_value()) {
                result.error = server_error(*document);
            }
            if (result.error.empty()) {
                result.error = document.has_value()
                    ? "speech endpoint returned JSON instead of audio"
                    : "speech endpoint returned invalid JSON: " + parse_error;
            }
            return result;
        }
        result.output_path = output_path(request, ".wav");
        result.succeeded = write_binary(result.output_path, response.body);
        if (!result.succeeded) {
            result.error = "unable to save generated audio";
        }
        return result;
    }

    std::string parse_error;
    const auto document = core::json::try_parse(response.body, parse_error);
    if (!document.has_value()) {
        result.error = "image endpoint returned invalid JSON: " + parse_error;
        return result;
    }
    if (const auto error = server_error(*document); !error.empty()) {
        result.error = error;
        return result;
    }
    const auto* first = document->find("data") == nullptr
        ? nullptr
        : document->find("data")->at(0U);
    if (first == nullptr) {
        result.error = "image endpoint returned no image";
        return result;
    }
    if (const auto encoded = core::json::string_or(first->find("b64_json")); !encoded.empty()) {
        const auto decoded = decode_base64(encoded);
        if (!decoded.has_value()) {
            result.error = "image endpoint returned invalid base64 data";
            return result;
        }
        result.output_path = output_path(request, ".png");
        result.succeeded = write_binary(result.output_path, *decoded);
    } else if (const auto url = core::json::string_or(first->find("url")); !url.empty()) {
        const auto download = http_get(url, std::chrono::minutes(10));
        if (!download.succeeded()) {
            result.error = download.error.empty() ? "unable to download generated image" : download.error;
            return result;
        }
        result.output_path = output_path(request, ".png");
        result.succeeded = write_binary(result.output_path, download.body);
    } else {
        result.error = "image endpoint returned neither b64_json nor url";
        return result;
    }
    result.mime_type = "image/png";
    if (!result.succeeded) {
        result.error = "unable to save generated image";
    }
    return result;
}

} // namespace threadspire::ai::backend
