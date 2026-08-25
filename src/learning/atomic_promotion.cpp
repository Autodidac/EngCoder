module;
#include "module_prelude.hpp"
#ifdef _WIN32
#include <Windows.h>
#endif

module threadspire.learning;
import threadspire.core;

namespace threadspire::ai::learning {
namespace {

[[nodiscard]] bool safe_generation(const std::string_view generation) noexcept {
    if (generation.empty() || generation == "." || generation == "..") {
        return false;
    }
    const std::filesystem::path path(generation);
    return path.filename() == path && generation.find('/') == std::string_view::npos &&
        generation.find('\\') == std::string_view::npos;
}

[[nodiscard]] std::string active_generation(
    const std::filesystem::path& pointer,
    std::string& error) {
    std::ifstream stream(pointer, std::ios::binary);
    if (!stream) {
        error = "active generation pointer is missing";
        return {};
    }
    const std::string text{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto document = core::json::try_parse(text, error);
    if (!document.has_value()) {
        error = "active generation pointer is unreadable: " + error;
        return {};
    }
    const auto generation = core::json::string_or(document->find("generation"));
    if (!safe_generation(generation)) {
        error = "active generation pointer is unsafe";
        return {};
    }
    return generation;
}

void publish_active_pointer(
    const std::filesystem::path& pointer,
    const std::string_view candidate,
    const std::string_view parent) {
    std::filesystem::create_directories(pointer.parent_path());
    auto temporary = pointer;
    temporary += ".candidate.tmp";
    core::json::Value::Object document{
        {"generation", std::string(candidate)},
        {"parent_generation", std::string(parent)},
        {"promoted_unix_ms", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())},
        {"candidate_host_verified", true},
    };
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << core::json::stringify(core::json::Value(std::move(document))) << '\n';
        stream.flush();
        if (!stream) {
            throw std::runtime_error("unable to write candidate active pointer");
        }
    }
#ifdef _WIN32
    if (::MoveFileExW(
            temporary.c_str(), pointer.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::system_error(
            static_cast<int>(::GetLastError()), std::system_category(),
            "unable to atomically promote candidate");
    }
#else
    std::filesystem::rename(temporary, pointer);
#endif
}

} // namespace

bool CandidateHostChecks::passed() const noexcept {
    return artifact_checksum_valid && candidate_host_started &&
        inference_canary_passed && deterministic_validation_passed &&
        regression_gate_passed;
}

AtomicPromotionResult AtomicPromotionCoordinator::promote(
    const AtomicPromotionRequest& request,
    const CandidateHostVerifier& verify_candidate_host,
    const CandidatePromotionCommit& commit_candidate) const noexcept {
    AtomicPromotionResult result{
        .candidate_generation = request.candidate_generation,
    };
    try {
        std::string error;
        result.active_generation = active_generation(request.active_pointer, error);
        if (result.active_generation.empty()) {
            result.outcome = PromotionOutcome::recoverable_failed;
            result.diagnostics = std::move(error);
            return result;
        }
        if (!safe_generation(request.candidate_generation) ||
            !safe_generation(request.expected_parent_generation)) {
            result.outcome = PromotionOutcome::rejected;
            result.diagnostics = "candidate or parent generation name is unsafe";
            return result;
        }
        if (result.active_generation != request.expected_parent_generation) {
            result.outcome = PromotionOutcome::stale_parent;
            result.diagnostics = "active model changed while the shadow candidate trained";
            return result;
        }
        std::error_code filesystem_error;
        const auto directory_status = std::filesystem::symlink_status(
            request.candidate_directory, filesystem_error);
        if (filesystem_error || !std::filesystem::is_directory(directory_status) ||
            std::filesystem::is_symlink(directory_status) ||
            !std::filesystem::is_regular_file(
                request.candidate_directory / "manifest.json", filesystem_error)) {
            result.outcome = PromotionOutcome::rejected;
            result.diagnostics = "candidate artifact directory or manifest is invalid";
            return result;
        }
        if (!verify_candidate_host) {
            result.outcome = PromotionOutcome::recoverable_failed;
            result.diagnostics = "candidate host verifier was not provided";
            return result;
        }

        // The active pointer is intentionally untouched while the isolated
        // candidate host starts and runs all checks.
        const auto checks = verify_candidate_host(request.candidate_directory);
        result.diagnostics = checks.diagnostics;
        if (!checks.artifact_checksum_valid) {
            result.outcome = PromotionOutcome::rejected;
            if (result.diagnostics.empty()) {
                result.diagnostics = "candidate checksum validation failed";
            }
            return result;
        }
        if (!checks.candidate_host_started || !checks.inference_canary_passed) {
            result.outcome = PromotionOutcome::recoverable_failed;
            if (result.diagnostics.empty()) {
                result.diagnostics = "candidate host failed; active model remains online";
            }
            return result;
        }
        if (!checks.deterministic_validation_passed || !checks.regression_gate_passed) {
            result.outcome = PromotionOutcome::rejected;
            if (result.diagnostics.empty()) {
                result.diagnostics = "candidate failed deterministic evaluation or regression gates";
            }
            return result;
        }
        std::string current_error;
        const auto current_generation = active_generation(
            request.active_pointer, current_error);
        if (current_generation != request.expected_parent_generation) {
            result.outcome = PromotionOutcome::stale_parent;
            result.active_generation = current_generation;
            result.diagnostics = current_error.empty()
                ? "active model changed while the isolated candidate host was checked"
                : std::move(current_error);
            return result;
        }
        if (commit_candidate) {
            const auto committed = commit_candidate();
            if (!committed.succeeded) {
                result.outcome = PromotionOutcome::recoverable_failed;
                result.diagnostics = committed.diagnostics.empty()
                    ? "candidate commit failed; active model remains online"
                    : committed.diagnostics;
                return result;
            }
            std::string published_error;
            const auto published_generation = active_generation(
                request.active_pointer, published_error);
            if (published_generation != request.candidate_generation) {
                result.outcome = PromotionOutcome::recoverable_failed;
                result.active_generation = published_generation;
                result.diagnostics = published_error.empty()
                    ? "candidate commit did not publish the verified generation"
                    : std::move(published_error);
                return result;
            }
        } else {
            publish_active_pointer(
                request.active_pointer,
                request.candidate_generation,
                request.expected_parent_generation);
        }
        result.outcome = PromotionOutcome::promoted;
        result.active_generation = request.candidate_generation;
        if (result.diagnostics.empty()) {
            result.diagnostics = "candidate host passed all checks and was atomically promoted";
        }
        return result;
    } catch (const std::exception& exception) {
        result.outcome = PromotionOutcome::recoverable_failed;
        result.diagnostics = exception.what();
        return result;
    } catch (...) {
        result.outcome = PromotionOutcome::recoverable_failed;
        result.diagnostics = "unknown candidate-host promotion failure";
        return result;
    }
}

std::string_view to_string(const PromotionOutcome outcome) noexcept {
    switch (outcome) {
    case PromotionOutcome::promoted: return "promoted";
    case PromotionOutcome::rejected: return "rejected";
    case PromotionOutcome::recoverable_failed: return "recoverable-failed";
    case PromotionOutcome::stale_parent: return "stale-parent";
    }
    return "recoverable-failed";
}

} // namespace threadspire::ai::learning