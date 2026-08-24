#include "threadspire/ai/runtime/internal_model_state.hpp"

#include "threadspire/ai/core/json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

namespace threadspire::ai::runtime {
namespace {

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool safe_generation_name(const std::string& generation) noexcept {
    if (generation.empty() || generation == "." || generation == "..") {
        return false;
    }
    const std::filesystem::path path(generation);
    return path.filename() == path &&
           generation.find('/') == std::string::npos &&
           generation.find('\\') == std::string::npos;
}

[[nodiscard]] std::size_t size_or(
    const core::json::Value* value,
    const std::size_t fallback = 0U) noexcept {
    const auto number = core::json::number_or(value, static_cast<double>(fallback));
    if (!std::isfinite(number) || number <= 0.0) {
        return number == 0.0 ? 0U : fallback;
    }
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::size_t>::max());
    return static_cast<std::size_t>(std::min(number, maximum));
}

[[nodiscard]] std::uint64_t uint64_or(
    const core::json::Value* value,
    const std::uint64_t fallback = 0U) noexcept {
    const auto number = core::json::number_or(value, static_cast<double>(fallback));
    if (!std::isfinite(number) || number <= 0.0) {
        return number == 0.0 ? 0U : fallback;
    }
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::uint64_t>::max());
    return static_cast<std::uint64_t>(std::min(number, maximum));
}

[[nodiscard]] std::int64_t int64_or(
    const core::json::Value* value,
    const std::int64_t fallback = 0) noexcept {
    const auto number = core::json::number_or(value, static_cast<double>(fallback));
    if (!std::isfinite(number)) {
        return fallback;
    }
    constexpr auto minimum = static_cast<double>(std::numeric_limits<std::int64_t>::min());
    constexpr auto maximum = static_cast<double>(std::numeric_limits<std::int64_t>::max());
    return static_cast<std::int64_t>(std::clamp(number, minimum, maximum));
}

[[nodiscard]] int int_or(
    const core::json::Value* value,
    const int fallback = 0) noexcept {
    const auto number = core::json::number_or(value, static_cast<double>(fallback));
    if (!std::isfinite(number)) {
        return fallback;
    }
    return static_cast<int>(std::clamp(
        number,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max())));
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

std::size_t default_bootstrap_target(const InternalModelState& state) noexcept {
    if (state.bootstrap_target_steps > 0U) {
        return state.bootstrap_target_steps;
    }
    const auto profile = lowercase(state.profile_name);
    if (profile.find("large") != std::string::npos) {
        return 2500U;
    }
    if (profile.find("standard") != std::string::npos) {
        return 1800U;
    }
    return 1200U;
}

InternalModelState inspect_internal_model_state(const app::AppPaths& paths) noexcept {
    InternalModelState state;
    try {
        const auto pointer_text = read_text(
            paths.root / "models" / "internal" / "active.json");
        if (pointer_text.empty()) {
            state.error = "internal model has not been initialized";
            return state;
        }
        state.exists = true;

        std::string parse_error;
        const auto pointer = core::json::try_parse(pointer_text, parse_error);
        if (!pointer.has_value()) {
            state.error = "active generation pointer is unreadable: " + parse_error;
            return state;
        }
        state.generation = core::json::string_or(pointer->find("generation"));
        if (!safe_generation_name(state.generation)) {
            state.error = "active generation pointer contains an invalid generation name";
            return state;
        }

        const auto manifest_path =
            paths.root / "models" / "internal" / "generations" /
            state.generation / "manifest.json";
        const auto manifest_text = read_text(manifest_path);
        if (manifest_text.empty()) {
            state.error = "active generation manifest is missing";
            return state;
        }
        const auto manifest = core::json::try_parse(manifest_text, parse_error);
        if (!manifest.has_value()) {
            state.error = "active generation manifest is unreadable: " + parse_error;
            return state;
        }

        state.manifest_valid = true;
        state.status = core::json::string_or(manifest->find("status"), "unknown");
        state.profile_name = core::json::string_or(manifest->find("name"));
        state.parameter_count = uint64_or(manifest->find("parameter_count"));
        state.tokens_seen = uint64_or(manifest->find("tokens_seen"));
        state.steps = size_or(manifest->find("steps"));
        state.bootstrap_target_steps = size_or(manifest->find("bootstrap_target_steps"));
        state.trained = state.status == "trained";
        state.full_backbone_trained = core::json::bool_or(
            manifest->find("full_backbone_trained"), false);
        state.generation_ready = core::json::bool_or(
            manifest->find("generation_ready"), false);
        state.bootstrap_complete =
            core::json::bool_or(manifest->find("bootstrap_complete"), false) &&
            state.trained && state.full_backbone_trained && state.generation_ready;
        return state;
    } catch (const std::exception& exception) {
        state.error = exception.what();
        return state;
    } catch (...) {
        state.error = "unknown error while reading the internal-model state";
        return state;
    }
}

InternalModelProgress read_internal_model_progress(const app::AppPaths& paths) noexcept {
    InternalModelProgress progress;
    try {
        const auto text = read_text(paths.root / "state" / "internal-model-progress.json");
        if (text.empty()) {
            return progress;
        }
        std::string parse_error;
        const auto value = core::json::try_parse(text, parse_error);
        if (!value.has_value()) {
            progress.available = true;
            progress.stale = true;
            progress.phase = "unreadable";
            progress.message = "Internal-model progress file is unreadable";
            progress.error = std::move(parse_error);
            return progress;
        }

        progress.available = true;
        progress.phase = core::json::string_or(value->find("phase"), "unknown");
        progress.message = core::json::string_or(value->find("message"));
        progress.error = core::json::string_or(value->find("error"));
        progress.generation = core::json::string_or(value->find("generation"));
        progress.step = size_or(value->find("step"));
        progress.maximum_steps = size_or(value->find("maximum_steps"));
        progress.prior_total_steps = size_or(value->find("prior_total_steps"));
        progress.total_steps = size_or(value->find("total_steps"));
        progress.bootstrap_target_steps = size_or(value->find("bootstrap_target_steps"));
        progress.tokens_seen = uint64_or(value->find("tokens_seen"));
        progress.elapsed_seconds = std::max(
            0.0,
            core::json::number_or(value->find("elapsed_seconds"), 0.0));
        progress.heartbeat_unix_ms = int64_or(value->find("heartbeat_unix_ms"));
        progress.process_id = int_or(value->find("pid"));
        progress.resumable = core::json::bool_or(value->find("resumable"), false);
        progress.bootstrap_complete = core::json::bool_or(
            value->find("bootstrap_complete"), false);
        progress.full_backbone_trained = core::json::bool_or(
            value->find("full_backbone_trained"), false);
        progress.generation_ready = core::json::bool_or(
            value->find("generation_ready"), false);

        const auto explicit_percent = core::json::number_or(value->find("percent"), -1.0);
        if (progress.bootstrap_target_steps > 0U) {
            const auto completed = progress.total_steps > 0U
                ? progress.total_steps
                : progress.prior_total_steps + progress.step;
            progress.percent = 100.0 * static_cast<double>(completed) /
                static_cast<double>(progress.bootstrap_target_steps);
        } else if (explicit_percent >= 0.0) {
            progress.percent = explicit_percent;
        } else if (progress.maximum_steps > 0U) {
            progress.percent = 100.0 * static_cast<double>(progress.step) /
                static_cast<double>(progress.maximum_steps);
        }
        progress.percent = std::clamp(progress.percent, 0.0, 100.0);

        const auto eta = core::json::number_or(value->find("eta_seconds"), -1.0);
        if (std::isfinite(eta) && eta >= 0.0) {
            progress.eta_seconds = eta;
        }

        if (progress.heartbeat_unix_ms > 0) {
            const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            constexpr std::int64_t stale_after_ms = 30'000;
            progress.stale = now > progress.heartbeat_unix_ms + stale_after_ms;
        }
        return progress;
    } catch (const std::exception& exception) {
        progress.available = true;
        progress.stale = true;
        progress.phase = "unreadable";
        progress.message = "Unable to read internal-model progress";
        progress.error = exception.what();
        return progress;
    } catch (...) {
        progress.available = true;
        progress.stale = true;
        progress.phase = "unreadable";
        progress.message = "Unknown error while reading internal-model progress";
        return progress;
    }
}

} // namespace threadspire::ai::runtime
