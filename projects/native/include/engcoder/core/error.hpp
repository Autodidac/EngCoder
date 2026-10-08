#pragma once
#include <string>
#include <system_error>

namespace engcoder::core {

enum class Errc {
    ok = 0,
    invalid_argument,
    io_error,
    workspace_violation,
    model_unavailable,
    model_loading_failed,
    model_timeout,
    model_connection_reset,
    model_context_overflow,
    model_generation_error,
    authentication_error,
    model_protocol_error,
    tool_not_found,
    tool_denied,
    approval_required,
    process_failed,
    persistence_error,
    cancelled,
    iteration_limit,
    stalled
};

class ErrorCategory final : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "engcoder"; }
    [[nodiscard]] std::string message(int value) const override;
};

[[nodiscard]] const std::error_category& error_category() noexcept;
[[nodiscard]] std::error_code make_error_code(Errc value) noexcept;

struct Error {
    std::error_code code;
    std::string message;
};

} // namespace engcoder::core

namespace std {
template<> struct is_error_code_enum<engcoder::core::Errc> : true_type {};
}
