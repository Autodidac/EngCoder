#include "threadspire/ai/backend/http_client.hpp"

#include <Windows.h>
#include <Winhttp.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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

class InternetHandle final {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) noexcept : value_(value) {}
    ~InternetHandle() {
        if (value_ != nullptr) {
            ::WinHttpCloseHandle(value_);
        }
    }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : value_(other.release()) {}
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            if (value_ != nullptr) {
                ::WinHttpCloseHandle(value_);
            }
            value_ = other.release();
        }
        return *this;
    }
    [[nodiscard]] HINTERNET get() const noexcept { return value_; }
    [[nodiscard]] HINTERNET release() noexcept {
        const auto value = value_;
        value_ = nullptr;
        return value;
    }

private:
    HINTERNET value_{nullptr};
};

[[nodiscard]] std::wstring widen(const std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const auto size = ::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    static_cast<void>(::MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size));
    return result;
}

[[nodiscard]] std::string narrow(const std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const auto size = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    static_cast<void>(::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr));
    return result;
}

[[nodiscard]] std::string windows_error(const DWORD code) {
    wchar_t* buffer = nullptr;
    const auto size = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    if (size == 0 || buffer == nullptr) {
        return "Windows error " + std::to_string(code);
    }
    auto result = narrow(std::wstring_view(buffer, size));
    ::LocalFree(buffer);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n')) {
        result.pop_back();
    }
    return result;
}

struct ParsedUrl final {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port{0};
    bool secure{false};
};

[[nodiscard]] bool parse_url(
    const std::string_view url,
    ParsedUrl& result,
    std::string& error) {
    const auto wide_url = widen(url);
    if (wide_url.empty()) {
        error = "invalid UTF-8 URL";
        return false;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (::WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components) == FALSE) {
        error = "WinHttpCrackUrl failed: " + windows_error(::GetLastError());
        return false;
    }

    result.host.assign(components.lpszHostName, components.dwHostNameLength);
    result.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
        result.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (result.path.empty()) {
        result.path = L"/";
    }
    result.port = components.nPort;
    result.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}

[[nodiscard]] std::wstring make_headers(const HttpRequest& request) {
    std::wstring result;
    bool has_content_type = false;
    for (const auto& header : request.headers) {
        if (header.name.empty()) {
            continue;
        }
        auto lower_name = header.name;
        std::ranges::transform(lower_name, lower_name.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        has_content_type = has_content_type || lower_name == "content-type";
        result += widen(header.name);
        result += L": ";
        result += widen(header.value);
        result += L"\r\n";
    }
    if (!request.body.empty() && !has_content_type) {
        result += L"Content-Type: application/json; charset=utf-8\r\n";
    }
    return result;
}

[[nodiscard]] std::vector<HttpHeader> query_response_headers(const HINTERNET request_handle) {
    DWORD size = 0;
    static_cast<void>(::WinHttpQueryHeaders(
        request_handle,
        WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX,
        nullptr,
        &size,
        WINHTTP_NO_HEADER_INDEX));
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0U) {
        return {};
    }
    std::wstring raw(static_cast<std::size_t>(size / sizeof(wchar_t)), L'\0');
    if (::WinHttpQueryHeaders(
            request_handle,
            WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX,
            raw.data(),
            &size,
            WINHTTP_NO_HEADER_INDEX) == FALSE) {
        return {};
    }
    raw.resize(size / sizeof(wchar_t));
    std::vector<HttpHeader> result;
    std::size_t begin = 0;
    while (begin < raw.size()) {
        const auto end = raw.find(L"\r\n", begin);
        const auto line = raw.substr(begin, end == std::wstring::npos ? raw.size() - begin : end - begin);
        const auto separator = line.find(L':');
        if (separator != std::wstring::npos) {
            auto name = narrow(std::wstring_view(line).substr(0, separator));
            auto value = narrow(std::wstring_view(line).substr(separator + 1U));
            const auto first = value.find_first_not_of(" \t");
            if (first != std::string::npos) {
                value.erase(0, first);
            } else {
                value.clear();
            }
            result.push_back({.name = std::move(name), .value = std::move(value)});
        }
        if (end == std::wstring::npos) {
            break;
        }
        begin = end + 2U;
    }
    return result;
}

} // namespace

HttpResponse http_request(const HttpRequest& request, HttpChunkCallback on_chunk) {
    HttpResponse result;
    ParsedUrl parsed;
    if (!parse_url(request.url, parsed, result.error)) {
        return result;
    }

    InternetHandle session(::WinHttpOpen(
        L"ThreadSpire/0.5.6",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (session.get() == nullptr) {
        result.error = "WinHttpOpen failed: " + windows_error(::GetLastError());
        return result;
    }

    const auto timeout_ms = static_cast<int>(std::clamp<long long>(
        request.timeout.count(),
        250,
        static_cast<long long>(std::numeric_limits<int>::max())));
    static_cast<void>(::WinHttpSetTimeouts(
        session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms));

    InternetHandle connection(::WinHttpConnect(
        session.get(),
        parsed.host.c_str(),
        parsed.port,
        0));
    if (connection.get() == nullptr) {
        result.error = "WinHttpConnect failed: " + windows_error(::GetLastError());
        return result;
    }

    const auto wide_method = widen(request.method);
    const auto flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request_handle(::WinHttpOpenRequest(
        connection.get(),
        wide_method.c_str(),
        parsed.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (request_handle.get() == nullptr) {
        result.error = "WinHttpOpenRequest failed: " + windows_error(::GetLastError());
        return result;
    }

    const auto headers = make_headers(request);
    auto* payload = request.body.empty()
        ? WINHTTP_NO_REQUEST_DATA
        : const_cast<char*>(request.body.data());
    const auto payload_size = static_cast<DWORD>(request.body.size());
    if (::WinHttpSendRequest(
            request_handle.get(),
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0U : static_cast<DWORD>(-1),
            payload,
            payload_size,
            payload_size,
            0) == FALSE) {
        result.error = "WinHttpSendRequest failed: " + windows_error(::GetLastError());
        return result;
    }
    if (::WinHttpReceiveResponse(request_handle.get(), nullptr) == FALSE) {
        result.error = "WinHttpReceiveResponse failed: " + windows_error(::GetLastError());
        return result;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (::WinHttpQueryHeaders(
            request_handle.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX) != FALSE) {
        result.status_code = static_cast<int>(status);
    }
    result.headers = query_response_headers(request_handle.get());

    while (true) {
        DWORD available = 0;
        if (::WinHttpQueryDataAvailable(request_handle.get(), &available) == FALSE) {
            result.error = "WinHttpQueryDataAvailable failed: " + windows_error(::GetLastError());
            return result;
        }
        if (available == 0U) {
            break;
        }
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (::WinHttpReadData(request_handle.get(), buffer.data(), available, &read) == FALSE) {
            result.error = "WinHttpReadData failed: " + windows_error(::GetLastError());
            return result;
        }
        if (read == 0U) {
            continue;
        }
        const std::string_view chunk(buffer.data(), read);
        result.body.append(chunk);
        if (on_chunk && !on_chunk(chunk)) {
            result.error = "HTTP stream cancelled by client";
            return result;
        }
    }
    return result;
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
