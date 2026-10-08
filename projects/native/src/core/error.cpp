#include "engcoder/core/error.hpp"

namespace engcoder::core {
std::string ErrorCategory::message(int value) const {
    switch (static_cast<Errc>(value)) {
        case Errc::ok: return "ok";
        case Errc::invalid_argument: return "invalid argument";
        case Errc::io_error: return "I/O error";
        case Errc::workspace_violation: return "workspace boundary violation";
        case Errc::model_unavailable: return "model unavailable";
        case Errc::model_loading_failed: return "model loading failed";
        case Errc::model_timeout: return "model timeout";
        case Errc::model_connection_reset: return "model connection reset";
        case Errc::model_context_overflow: return "model context overflow";
        case Errc::model_generation_error: return "model generation error";
        case Errc::authentication_error: return "authentication error";
        case Errc::model_protocol_error: return "model protocol error";
        case Errc::tool_not_found: return "tool not found";
        case Errc::tool_denied: return "tool denied";
        case Errc::approval_required: return "approval required";
        case Errc::process_failed: return "process failed";
        case Errc::persistence_error: return "persistence error";
        case Errc::cancelled: return "cancelled";
        case Errc::iteration_limit: return "iteration limit reached";
        case Errc::stalled: return "agent stalled";
    }
    return "unknown";
}
const std::error_category& error_category() noexcept { static ErrorCategory c; return c; }
std::error_code make_error_code(Errc value) noexcept { return {static_cast<int>(value), error_category()}; }
} // namespace engcoder::core
