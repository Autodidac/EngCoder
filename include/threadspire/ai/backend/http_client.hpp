#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
