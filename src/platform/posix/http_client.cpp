#include "threadspire/ai/backend/http_client.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>
#include <netdb.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace threadspire::ai::backend {

std::optional<std::string> HttpResponse::header(const std::string_view name) const {
    for (const auto& item : headers) {
        if (item.name.size() != name.size()) {
            continue;
        }
        const auto matches = std::equal(
            item.name.begin(),
            item.name.end(),
            name.begin(),
            [](const unsigned char left, const unsigned char right) {
                return std::tolower(left) == std::tolower(right);
            });
        if (matches) {
            return item.value;
        }
    }
    return std::nullopt;
}

namespace {

class SocketHandle final {
public:
    SocketHandle() = default;
    explicit SocketHandle(const int value) noexcept : value_(value) {}
    ~SocketHandle() {
        if (value_ >= 0) {
            static_cast<void>(::close(value_));
        }
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : value_(other.release()) {}
    SocketHandle& operator=(SocketHandle&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                static_cast<void>(::close(value_));
            }
            value_ = other.release();
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return value_; }

private:
    [[nodiscard]] int release() noexcept {
        const auto value = value_;
        value_ = -1;
        return value;
    }

    int value_{-1};
};

class AddressInfo final {
public:
    ~AddressInfo() {
        if (value_ != nullptr) {
            ::freeaddrinfo(value_);
        }
    }

    AddressInfo(const AddressInfo&) = delete;
    AddressInfo& operator=(const AddressInfo&) = delete;
    AddressInfo() = default;

    [[nodiscard]] addrinfo** output() noexcept { return &value_; }
    [[nodiscard]] addrinfo* get() const noexcept { return value_; }

private:
    addrinfo* value_{nullptr};
};

struct ParsedUrl final {
    std::string host;
    std::string port;
    std::string path;
};

[[nodiscard]] std::string lower_ascii(const std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string trim_ascii(std::string value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1U);
}

[[nodiscard]] bool parse_url(
    const std::string_view url,
    ParsedUrl& result,
    std::string& error) {
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        error = url.starts_with("https://")
            ? "HTTPS is not implemented by the POSIX validation transport"
            : "only absolute http:// URLs are supported";
        return false;
    }

    auto remainder = url.substr(scheme.size());
    const auto slash = remainder.find('/');
    auto authority = remainder.substr(0U, slash);
    result.path = slash == std::string_view::npos
        ? "/"
        : std::string(remainder.substr(slash));
    if (authority.empty()) {
        error = "URL has no host";
        return false;
    }

    if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string_view::npos) {
            error = "invalid bracketed IPv6 URL";
            return false;
        }
        result.host = std::string(authority.substr(1U, closing - 1U));
        if (closing + 1U < authority.size()) {
            if (authority[closing + 1U] != ':') {
                error = "invalid IPv6 authority";
                return false;
            }
            result.port = std::string(authority.substr(closing + 2U));
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon != std::string_view::npos && authority.find(':') == colon) {
            result.host = std::string(authority.substr(0U, colon));
            result.port = std::string(authority.substr(colon + 1U));
        } else {
            result.host = std::string(authority);
        }
    }
    if (result.host.empty()) {
        error = "URL has no host";
        return false;
    }
    if (result.port.empty()) {
        result.port = "80";
    }
    return true;
}

[[nodiscard]] SocketHandle connect_socket(
    const ParsedUrl& url,
    const std::chrono::milliseconds timeout,
    std::string& error) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    AddressInfo addresses;
    const auto lookup = ::getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, addresses.output());
    if (lookup != 0) {
        error = "getaddrinfo failed: " + std::string(::gai_strerror(lookup));
        return {};
    }

    for (auto* address = addresses.get(); address != nullptr; address = address->ai_next) {
        SocketHandle socket(::socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        if (socket.get() < 0) {
            continue;
        }
        const auto seconds = timeout.count() / 1000;
        const auto microseconds = (timeout.count() % 1000) * 1000;
        timeval value{
            .tv_sec = static_cast<time_t>(seconds),
            .tv_usec = static_cast<suseconds_t>(microseconds),
        };
        static_cast<void>(::setsockopt(
            socket.get(), SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)));
        static_cast<void>(::setsockopt(
            socket.get(), SOL_SOCKET, SO_SNDTIMEO, &value, sizeof(value)));
        if (::connect(socket.get(), address->ai_addr, address->ai_addrlen) == 0) {
            return socket;
        }
    }
    error = "unable to connect to " + url.host + ':' + url.port + ": " + std::strerror(errno);
    return {};
}

[[nodiscard]] bool send_all(
    const int socket,
    const std::string_view data,
    std::string& error) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto remaining = data.size() - sent;
        const auto bounded = std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        const auto result = ::send(socket, data.data() + sent, bounded, MSG_NOSIGNAL);
        if (result < 0) {
            error = "send failed: " + std::string(std::strerror(errno));
            return false;
        }
        if (result == 0) {
            error = "connection closed while sending request";
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

[[nodiscard]] std::string build_request(
    const HttpRequest& request,
    const ParsedUrl& url) {
    std::ostringstream stream;
    stream << request.method << ' ' << url.path << " HTTP/1.1\r\n"
           << "Host: " << url.host;
    if (url.port != "80") {
        stream << ':' << url.port;
    }
    stream << "\r\nUser-Agent: ThreadSpire/0.5.6\r\n"
           << "Connection: close\r\n";

    bool has_content_type = false;
    bool has_accept_encoding = false;
    for (const auto& header : request.headers) {
        if (header.name.empty()) {
            continue;
        }
        const auto lower = lower_ascii(header.name);
        has_content_type = has_content_type || lower == "content-type";
        has_accept_encoding = has_accept_encoding || lower == "accept-encoding";
        stream << header.name << ": " << header.value << "\r\n";
    }
    if (!has_accept_encoding) {
        stream << "Accept-Encoding: identity\r\n";
    }
    if (!request.body.empty() && !has_content_type) {
        stream << "Content-Type: application/json; charset=utf-8\r\n";
    }
    if (!request.body.empty()) {
        stream << "Content-Length: " << request.body.size() << "\r\n";
    }
    stream << "\r\n" << request.body;
    return stream.str();
}

[[nodiscard]] bool parse_response_head(
    const std::string_view head,
    HttpResponse& response,
    bool& chunked,
    std::optional<std::size_t>& content_length) {
    const auto first_line_end = head.find("\r\n");
    if (first_line_end == std::string_view::npos) {
        response.error = "malformed HTTP response status line";
        return false;
    }
    const auto status_line = head.substr(0U, first_line_end);
    const auto first_space = status_line.find(' ');
    const auto second_space = first_space == std::string_view::npos
        ? std::string_view::npos
        : status_line.find(' ', first_space + 1U);
    if (first_space == std::string_view::npos) {
        response.error = "malformed HTTP response status";
        return false;
    }
    const auto code_text = status_line.substr(
        first_space + 1U,
        second_space == std::string_view::npos
            ? status_line.size() - first_space - 1U
            : second_space - first_space - 1U);
    int code = 0;
    const auto [position, conversion_error] = std::from_chars(
        code_text.data(), code_text.data() + code_text.size(), code);
    if (conversion_error != std::errc{} || position != code_text.data() + code_text.size()) {
        response.error = "invalid HTTP response status code";
        return false;
    }
    response.status_code = code;

    std::size_t cursor = first_line_end + 2U;
    while (cursor < head.size()) {
        const auto end = head.find("\r\n", cursor);
        const auto line = head.substr(
            cursor,
            end == std::string_view::npos ? head.size() - cursor : end - cursor);
        if (line.empty()) {
            break;
        }
        const auto separator = line.find(':');
        if (separator != std::string_view::npos) {
            auto name = trim_ascii(std::string(line.substr(0U, separator)));
            auto value = trim_ascii(std::string(line.substr(separator + 1U)));
            const auto lower_name = lower_ascii(name);
            const auto lower_value = lower_ascii(value);
            if (lower_name == "transfer-encoding" &&
                lower_value.find("chunked") != std::string::npos) {
                chunked = true;
            } else if (lower_name == "content-length") {
                std::size_t length = 0;
                const auto [end_position, length_error] = std::from_chars(
                    value.data(), value.data() + value.size(), length);
                if (length_error == std::errc{} && end_position == value.data() + value.size()) {
                    content_length = length;
                }
            }
            response.headers.push_back({.name = std::move(name), .value = std::move(value)});
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 2U;
    }
    return true;
}

class ChunkedDecoder final {
public:
    using Callback = std::function<bool(std::string_view)>;

    [[nodiscard]] bool feed(
        const std::string_view data,
        const Callback& callback,
        std::string& error) {
        buffer_.append(data);
        while (true) {
            if (state_ == State::size) {
                const auto line_end = buffer_.find("\r\n");
                if (line_end == std::string::npos) {
                    return true;
                }
                auto size_text = std::string_view(buffer_).substr(0U, line_end);
                const auto extension = size_text.find(';');
                if (extension != std::string_view::npos) {
                    size_text = size_text.substr(0U, extension);
                }
                std::size_t value = 0;
                const auto [position, conversion_error] = std::from_chars(
                    size_text.data(), size_text.data() + size_text.size(), value, 16);
                if (conversion_error != std::errc{} ||
                    position != size_text.data() + size_text.size()) {
                    error = "invalid HTTP chunk size";
                    return false;
                }
                buffer_.erase(0U, line_end + 2U);
                remaining_ = value;
                if (remaining_ == 0U) {
                    state_ = State::trailers;
                } else {
                    state_ = State::payload;
                }
            } else if (state_ == State::payload) {
                if (buffer_.size() < remaining_) {
                    return true;
                }
                if (remaining_ > 0U && !callback(std::string_view(buffer_).substr(0U, remaining_))) {
                    error = "HTTP stream cancelled by client";
                    return false;
                }
                buffer_.erase(0U, remaining_);
                remaining_ = 0U;
                state_ = State::payload_crlf;
            } else if (state_ == State::payload_crlf) {
                if (buffer_.size() < 2U) {
                    return true;
                }
                if (!buffer_.starts_with("\r\n")) {
                    error = "malformed HTTP chunk delimiter";
                    return false;
                }
                buffer_.erase(0U, 2U);
                state_ = State::size;
            } else if (state_ == State::trailers) {
                const auto trailers_end = buffer_.find("\r\n\r\n");
                if (trailers_end != std::string::npos) {
                    buffer_.erase(0U, trailers_end + 4U);
                    finished_ = true;
                    return true;
                }
                if (buffer_.starts_with("\r\n")) {
                    buffer_.erase(0U, 2U);
                    finished_ = true;
                    return true;
                }
                return true;
            }
        }
    }

    [[nodiscard]] bool finished() const noexcept { return finished_; }

private:
    enum class State {
        size,
        payload,
        payload_crlf,
        trailers,
    };

    State state_{State::size};
    std::string buffer_;
    std::size_t remaining_{0};
    bool finished_{false};
};

} // namespace

HttpResponse http_request(const HttpRequest& request, HttpChunkCallback on_chunk) {
    HttpResponse response;
    ParsedUrl parsed;
    if (!parse_url(request.url, parsed, response.error)) {
        return response;
    }
    auto socket = connect_socket(parsed, request.timeout, response.error);
    if (socket.get() < 0) {
        return response;
    }
    const auto wire_request = build_request(request, parsed);
    if (!send_all(socket.get(), wire_request, response.error)) {
        return response;
    }

    std::string pending;
    std::array<char, 32U * 1024U> buffer{};
    bool head_parsed = false;
    bool chunked = false;
    std::optional<std::size_t> content_length;
    std::size_t received_body = 0;
    ChunkedDecoder chunk_decoder;

    const auto deliver = [&](const std::string_view chunk) {
        response.body.append(chunk);
        received_body += chunk.size();
        return !on_chunk || on_chunk(chunk);
    };

    while (true) {
        const auto count = ::recv(socket.get(), buffer.data(), buffer.size(), 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            response.error = "recv failed: " + std::string(std::strerror(errno));
            return response;
        }
        if (count == 0) {
            break;
        }
        const auto chunk = std::string_view(buffer.data(), static_cast<std::size_t>(count));
        if (!head_parsed) {
            pending.append(chunk);
            const auto separator = pending.find("\r\n\r\n");
            if (separator == std::string::npos) {
                if (pending.size() > 1024U * 1024U) {
                    response.error = "HTTP response headers exceeded 1 MiB";
                    return response;
                }
                continue;
            }
            if (!parse_response_head(
                    std::string_view(pending).substr(0U, separator + 2U),
                    response,
                    chunked,
                    content_length)) {
                return response;
            }
            head_parsed = true;
            const auto body = std::string_view(pending).substr(separator + 4U);
            if (!body.empty()) {
                if (chunked) {
                    if (!chunk_decoder.feed(body, deliver, response.error)) {
                        return response;
                    }
                } else if (!deliver(body)) {
                    response.error = "HTTP stream cancelled by client";
                    return response;
                }
            }
            pending.clear();
        } else if (chunked) {
            if (!chunk_decoder.feed(chunk, deliver, response.error)) {
                return response;
            }
        } else if (!deliver(chunk)) {
            response.error = "HTTP stream cancelled by client";
            return response;
        }

        if ((content_length.has_value() && received_body >= *content_length) ||
            (chunked && chunk_decoder.finished())) {
            break;
        }
    }

    if (!head_parsed) {
        response.error = "connection closed before HTTP response headers arrived";
    } else if (content_length.has_value() && received_body < *content_length) {
        response.error = "connection closed before the declared response body completed";
    } else if (chunked && !chunk_decoder.finished()) {
        response.error = "connection closed before the chunked response completed";
    }
    return response;
}

HttpResponse http_get(
    const std::string_view url,
    const std::chrono::milliseconds timeout) {
    return http_request(HttpRequest{
        .method = "GET",
        .url = std::string(url),
        .body = {},
        .headers = {},
        .timeout = timeout,
    });
}

HttpResponse http_post_json(
    const std::string_view url,
    const std::string_view body,
    const std::chrono::milliseconds timeout) {
    return http_request(HttpRequest{
        .method = "POST",
        .url = std::string(url),
        .body = std::string(body),
        .headers = {{.name = "Content-Type", .value = "application/json; charset=utf-8"}},
        .timeout = timeout,
    });
}

} // namespace threadspire::ai::backend
