#include "threadspire/ai/learning/piggyback_foundry.hpp"

#include "threadspire/ai/backend/completion_client.hpp"
#include "threadspire/ai/core/json.hpp"
#include "threadspire/ai/memory/episode_store.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace threadspire::ai::learning {
namespace {

struct SeedExample final {
    std::string domain;
    std::string declared_skill;
    std::string system;
    std::string prompt;
    std::string reference;
};

struct Verification final {
    bool accepted{false};
    double score{0.0};
    std::string reason;
};

[[nodiscard]] std::string normalize_skill_id(const std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    auto separator_pending = false;
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            if (separator_pending && !normalized.empty()) {
                normalized.push_back('-');
            }
            normalized.push_back(static_cast<char>(std::tolower(character)));
            separator_pending = false;
        } else {
            separator_pending = !normalized.empty();
        }
    }
    while (!normalized.empty() && normalized.back() == '-') {
        normalized.pop_back();
    }
    return normalized.empty() ? std::string{"english-foundation"} : normalized;
}

[[nodiscard]] std::string lower_ascii(const std::string_view value) {
    std::string lowered(value);
    std::ranges::transform(lowered, lowered.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return lowered;
}

[[nodiscard]] std::string skill_for_seed(const SeedExample& example) {
    if (!example.declared_skill.empty()) {
        return normalize_skill_id(example.declared_skill);
    }
    const auto lowered = lower_ascii(example.domain);
    if (lowered.find("build") != std::string::npos ||
        lowered.find("cmake") != std::string::npos ||
        lowered.find("msvc") != std::string::npos) {
        return "msvc-cmake";
    }
    if (lowered.find("cpp") != std::string::npos ||
        lowered.find("c++") != std::string::npos ||
        lowered.find("code") != std::string::npos) {
        return "cpp23-core";
    }
    return "english-foundation";
}

[[nodiscard]] bool seed_matches_target(
    const SeedExample& example,
    const std::string_view requested_target) {
    const auto target = normalize_skill_id(requested_target);
    const auto seed_skill = skill_for_seed(example);
    if (target.starts_with("english")) {
        return seed_skill.starts_with("english");
    }
    if (target.starts_with("cpp23")) {
        return seed_skill.starts_with("cpp23");
    }
    if (target == "msvc-cmake" || target == "build-systems") {
        return seed_skill == "msvc-cmake" || seed_skill == "build-systems";
    }
    if (!example.declared_skill.empty()) {
        return seed_skill == target;
    }
    const auto domain = normalize_skill_id(example.domain);
    return domain == target || domain.find(target) != std::string::npos;
}

[[nodiscard]] std::vector<SeedExample> load_seed_examples(
    const std::filesystem::path& path,
    const std::size_t maximum,
    const std::size_t offset,
    const std::string_view target_skill,
    const bool restrict_to_target_domain) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }

    std::vector<SeedExample> available;
    std::string line;
    while (std::getline(stream, line)) {
        std::string error;
        const auto document = core::json::try_parse(line, error);
        if (!document.has_value()) {
            continue;
        }
        SeedExample example;
        example.domain = core::json::string_or(document->find("domain"), "general");
        example.declared_skill = core::json::string_or(document->find("skill"));
        const auto* messages = document->find("messages");
        const auto* array = messages == nullptr ? nullptr : messages->as_array();
        if (array != nullptr) {
            for (const auto& message : *array) {
                const auto role = core::json::string_or(message.find("role"));
                const auto content = core::json::string_or(message.find("content"));
                if (role == "system") {
                    example.system = content;
                } else if (role == "user") {
                    example.prompt = content;
                } else if (role == "assistant") {
                    example.reference = content;
                }
            }
        }
        if (example.prompt.empty()) {
            example.prompt = core::json::string_or(document->find("prompt"));
        }
        if (example.reference.empty()) {
            example.reference = core::json::string_or(document->find("response"));
        }
        if (example.reference.empty()) {
            example.reference = core::json::string_or(document->find("answer"));
        }
        if (example.system.empty()) {
            example.system = core::json::string_or(document->find("system"));
        }
        if (!example.prompt.empty() && !example.reference.empty() &&
            (!restrict_to_target_domain || seed_matches_target(example, target_skill))) {
            available.push_back(std::move(example));
        }
    }

    if (available.empty() || maximum == 0U) {
        return {};
    }

    const auto count = std::min(maximum, available.size());
    const auto begin = offset % available.size();
    std::vector<SeedExample> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(available[(begin + index) % available.size()]);
    }
    return result;
}

[[nodiscard]] std::string target_skill_for(
    const PiggybackRequest& request,
    const SeedExample& example) {
    return request.target_skill.empty()
        ? skill_for_seed(example)
        : normalize_skill_id(request.target_skill);
}

[[nodiscard]] std::string teacher_system_prompt(const std::string_view target_skill) {
    const auto normalized = normalize_skill_id(target_skill);
    std::string target_instruction;
    if (normalized.starts_with("english")) {
        target_instruction =
            "Prioritize plain English comprehension, instruction following, factual explanations, "
            "conversation, summarization, and precise wording. Do not introduce programming unless the task asks for it.";
    } else if (normalized.starts_with("cpp23")) {
        target_instruction =
            "Prioritize modern C++23 correctness, safety, performance, complete source-level answers, and buildable code.";
    } else if (normalized == "msvc-cmake" || normalized == "build-systems") {
        target_instruction =
            "Prioritize CMake, MSVC, Ninja, compiler diagnostics, reproducible builds, and Windows build-system correctness.";
    } else {
        target_instruction = "Train the selected skill only: " + normalized + ".";
    }
    return
        "You are a senior local teacher producing dense, correct supervised training material for a smaller "
        "offline assistant. Answer the user's task directly. Use clear English. " + target_instruction +
        " Do not mention that this is training data and do not grade yourself.";
}

[[nodiscard]] std::optional<core::json::Value> extract_json_object(const std::string_view text) {
    const auto begin = text.find('{');
    const auto end = text.rfind('}');
    if (begin == std::string_view::npos || end == std::string_view::npos || end < begin) {
        return std::nullopt;
    }
    std::string error;
    auto value = core::json::try_parse(text.substr(begin, end - begin + 1U), error);
    if (!value.has_value() || !value->is_object()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] Verification verify_candidate(
    const PiggybackRequest& request,
    const SeedExample& example,
    const std::string_view candidate,
    const std::stop_token stop_token) {
    Verification verification;
    if (!request.verifier.has_value() || request.verifier->id == request.teacher.id) {
        verification.reason = "No distinct verifier model was available; stored as provisional memory only.";
        return verification;
    }

    backend::CompletionClient client;
    const auto prompt =
        "Evaluate a candidate training answer against a trusted reference. The candidate may use different "
        "wording, but it must be correct, directly answer the task, preserve important constraints, and contain "
        "no unsupported claims. Return JSON only with this exact shape: "
        "{\"accepted\":true|false,\"score\":0.0,\"reason\":\"brief reason\"}.\n\n"
        "TASK:\n" + example.prompt +
        "\n\nTRUSTED REFERENCE:\n" + example.reference +
        "\n\nCANDIDATE:\n" + std::string(candidate);
    const auto result = client.complete(
        request.internal_base_url,
        *request.verifier,
        backend::CompletionRequest{
            .system_prompt = "You are an independent strict dataset verifier. Output valid JSON only.",
            .prompt = prompt,
            .messages = {},
            .max_output_tokens = 192,
            .temperature = 0.0,
            .adapter_path = {},
            .module_path = {},
            .tools_json = {},
            .stream = true,
            .on_token = {},
            .should_stop = [stop_token] { return stop_token.stop_requested(); },
        });
    if (!result.succeeded) {
        verification.reason = "Verifier failed: " + result.error;
        return verification;
    }
    const auto document = extract_json_object(result.content);
    if (!document.has_value()) {
        verification.reason = "Verifier returned malformed JSON.";
        return verification;
    }
    verification.accepted = core::json::bool_or(document->find("accepted"), false);
    verification.score = std::clamp(core::json::number_or(document->find("score")), 0.0, 1.0);
    verification.reason = core::json::string_or(document->find("reason"));
    verification.accepted = verification.accepted &&
                            verification.score >= request.minimum_verifier_score;
    return verification;
}

void publish_progress(
    const PiggybackProgressCallback& callback,
    const PiggybackProgress& progress) {
    if (callback) {
        callback(progress);
    }
}

} // namespace

PiggybackResult run_piggyback_foundry(
    const PiggybackRequest& request,
    ContinualLearningService& learning_service,
    const PiggybackProgressCallback& on_progress,
    const std::stop_token stop_token) {
    PiggybackResult result;
    result.progress.target_skill = normalize_skill_id(request.target_skill);
    if (request.generate_teacher_variations && request.teacher.model_name.empty()) {
        result.error = "No teacher model was selected for variation generation.";
        return result;
    }
    const auto examples = load_seed_examples(
        request.seed_dataset,
        request.maximum_examples,
        request.seed_offset,
        result.progress.target_skill,
        request.restrict_to_target_domain);
    if (examples.empty()) {
        result.error = "No usable seed prompts for target '" + result.progress.target_skill +
            "' were found in " + request.seed_dataset.string();
        return result;
    }

    backend::CompletionClient client;
    for (const auto& example : examples) {
        if (stop_token.stop_requested()) {
            result.cancelled = true;
            result.progress.message = "Piggyback generation cancelled.";
            publish_progress(on_progress, result.progress);
            return result;
        }
        ++result.progress.examined;
        result.progress.next_seed_offset = request.seed_offset + result.progress.examined;
        result.progress.current_prompt = example.prompt;
        result.progress.message = "Preparing " + result.progress.target_skill + " example " +
                                  std::to_string(result.progress.examined) + " of " +
                                  std::to_string(examples.size());
        publish_progress(on_progress, result.progress);

        const auto trusted_stored = learning_service.submit(memory::Episode{
            .id = {},
            .skill = target_skill_for(request, example),
            .prompt = example.prompt,
            .response = example.reference,
            .model_id = request.generate_teacher_variations
                ? "bundled-trusted-seed"
                : "trusted-jsonl-import",
            .score = 0.99,
            .compiler_passed = false,
            .tests_passed = false,
            .human_accepted = false,
            .human_rejected = false,
            .deterministic_verified = false,
            .reference_verified = true,
            .self_generated = false,
            .eligible_for_training = false,
            .verifier_model_id = request.generate_teacher_variations
                ? "bundled-reference"
                : "user-trusted-reference",
            .verification_notes = request.generate_teacher_variations
                ? "Bundled hand-reviewed reference example."
                : "User-selected trusted JSONL reference example.",
            .provenance = request.generate_teacher_variations
                ? "trusted-seed-bootstrap"
                : "trusted-custom-import",
            .knowledge_destination = "lora-incubation",
            .content_hash = {},
            .correction = {},
            .tool_trace_hash = {},
            .created_unix_ms = 0,
        });
        if (trusted_stored) {
            ++result.progress.trusted_seeded;
            ++result.progress.stored;
        }

        if (!request.generate_teacher_variations) {
            result.progress.message = "Trusted " + result.progress.target_skill +
                " example imported without synthetic variation.";
            publish_progress(on_progress, result.progress);
            continue;
        }

        result.progress.message = "Teacher generating " + result.progress.target_skill + " variation " +
                                  std::to_string(result.progress.examined) + " of " +
                                  std::to_string(examples.size());
        publish_progress(on_progress, result.progress);

        const auto teacher_system = teacher_system_prompt(result.progress.target_skill);
        const auto generated = client.complete(
            request.internal_base_url,
            request.teacher,
            backend::CompletionRequest{
                .system_prompt = teacher_system,
                .prompt = example.prompt,
                .messages = {},
                .max_output_tokens = request.maximum_output_tokens,
                .temperature = 0.35,
                .adapter_path = {},
                .module_path = {},
                .tools_json = {},
                .stream = true,
                .on_token = {},
                .should_stop = [stop_token] { return stop_token.stop_requested(); },
            });
        if (!generated.succeeded || generated.content.empty()) {
            ++result.progress.rejected;
            result.progress.message = "Teacher generation failed: " + generated.error;
            publish_progress(on_progress, result.progress);
            continue;
        }
        ++result.progress.generated;

        const auto verification = verify_candidate(request, example, generated.content, stop_token);
        if (verification.accepted) {
            ++result.progress.verified;
        } else if (request.verifier.has_value()) {
            ++result.progress.rejected;
        }

        const auto stored = learning_service.submit(memory::Episode{
            .id = {},
            .skill = target_skill_for(request, example),
            .prompt = example.prompt,
            .response = generated.content,
            .model_id = request.teacher.id,
            .score = verification.accepted ? verification.score : 0.72,
            .compiler_passed = false,
            .tests_passed = false,
            .human_accepted = false,
            .human_rejected = false,
            .deterministic_verified = false,
            .reference_verified = verification.accepted,
            .self_generated = true,
            .eligible_for_training = false,
            .verifier_model_id = request.verifier.has_value() ? request.verifier->id : std::string{},
            .verification_notes = verification.reason,
            .provenance = "local-model-piggyback",
            .knowledge_destination = verification.accepted ? "lora-incubation" : "episodic-memory",
            .content_hash = {},
            .correction = example.reference,
            .tool_trace_hash = {},
            .created_unix_ms = 0,
        });
        if (stored) {
            ++result.progress.stored;
        }
        result.progress.message = verification.accepted
            ? "Verified teacher example stored for the continual-learning pipeline."
            : "Teacher example quarantined as provisional memory: " + verification.reason;
        publish_progress(on_progress, result.progress);
    }

    result.succeeded = result.progress.stored > 0U;
    if (!result.succeeded) {
        result.error = "No unique teacher examples were stored.";
    }
    result.progress.message = result.succeeded
        ? (request.generate_teacher_variations
            ? "Verified teacher-data batch complete."
            : "Trusted dataset import complete.")
        : result.error;
    publish_progress(on_progress, result.progress);
    return result;
}

} // namespace threadspire::ai::learning
