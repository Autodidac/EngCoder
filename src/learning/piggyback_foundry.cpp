module;
#include "module_prelude.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <future>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

module threadspire.learning;
import threadspire.core;
import threadspire.providers;
import threadspire.storage;

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

struct VariationAttempt final {
    std::size_t seed_index{0};
    SeedExample seed;
    models::ModelProfile teacher;
    std::optional<models::ModelProfile> verifier;
    std::string candidate_prompt;
    std::string candidate_response;
    std::string error;
    Verification verification;
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
            "Use complete grammatical English, natural conversation, instruction following, factual explanation, "
            "summarization, and precise wording. Do not introduce programming unless the task asks for it.";
    } else if (normalized.starts_with("cpp23")) {
        target_instruction =
            "Use modern C++23 correctness, safety, complete source-level answers, and buildable code.";
    } else if (normalized == "msvc-cmake" || normalized == "build-systems") {
        target_instruction =
            "Use correct CMake, MSVC, compiler-diagnostic, and reproducible-build behavior.";
    } else {
        target_instruction = "Stay inside the selected skill: " + normalized + ".";
    }
    return
        "Generate one genuinely different supervised example for a compact offline assistant. "
        "Return JSON only with this exact shape: {\"prompt\":\"new task\",\"answer\":\"standalone correct answer\"}. "
        "The new task must exercise the same reusable behavior as the trusted example without copying its wording. "
        "Do not add names, personal facts, current events, trivia, hidden reasoning, grading, or training commentary. " +
        target_instruction;
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
    const models::ModelProfile& verifier,
    const std::string_view candidate_prompt,
    const std::string_view candidate_response,
    const std::stop_token stop_token) {
    Verification verification;
    backend::CompletionClient client;
    const auto prompt =
        "Judge whether a new supervised example teaches the same reusable behavior as a trusted example. "
        "The new prompt must be genuinely different, and its answer must be correct for that new prompt, standalone, "
        "fluent, and free of personal facts, current facts, unsupported claims, or copied boilerplate. "
        "Return JSON only with this exact shape: "
        "{\"accepted\":true|false,\"score\":0.0,\"reason\":\"brief reason\"}.\n\n"
        "SOURCE TASK:\n" + example.prompt +
        "\n\nTRUSTED ANSWER:\n" + example.reference +
        "\n\nNEW TASK:\n" + std::string(candidate_prompt) +
        "\n\nNEW ANSWER:\n" + std::string(candidate_response);
    const auto result = client.complete(
        request.internal_base_url,
        verifier,
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
    if (verification.reason.empty()) {
        verification.reason = verification.accepted
            ? "Independent verifier accepted the diverse example."
            : "Independent verifier rejected the example.";
    }
    return verification;
}

[[nodiscard]] VariationAttempt generate_variation(
    const PiggybackRequest& request,
    const SeedExample& example,
    const std::size_t seed_index,
    const models::ModelProfile& teacher,
    const models::ModelProfile& verifier,
    const std::string_view target_skill,
    const std::stop_token stop_token) {
    VariationAttempt attempt{
        .seed_index = seed_index,
        .seed = example,
        .teacher = teacher,
        .verifier = verifier,
    };
    try {
        backend::CompletionClient client;
        const auto generated = client.complete(
            request.internal_base_url,
            teacher,
            backend::CompletionRequest{
                .system_prompt = teacher_system_prompt(target_skill),
                .prompt =
                    "SOURCE TASK:\n" + example.prompt +
                    "\n\nTRUSTED ANSWER:\n" + example.reference +
                    "\n\nCreate one new task and answer that teach the same behavior.",
                .messages = {},
                .max_output_tokens = request.maximum_output_tokens,
                .temperature = 0.45,
                .adapter_path = {},
                .module_path = {},
                .tools_json = {},
                .stream = true,
                .on_token = {},
                .should_stop = [stop_token] { return stop_token.stop_requested(); },
            });
        if (!generated.succeeded || generated.content.empty()) {
            attempt.error = generated.error.empty()
                ? "Teacher returned no content."
                : "Teacher failed: " + generated.error;
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        const auto document = extract_json_object(generated.content);
        if (!document.has_value()) {
            attempt.error = "Teacher returned malformed JSON.";
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        attempt.candidate_prompt = core::json::string_or(document->find("prompt"));
        attempt.candidate_response = core::json::string_or(document->find("answer"));
        if (attempt.candidate_prompt.empty() || attempt.candidate_response.empty()) {
            attempt.error = "Teacher JSON omitted the new prompt or answer.";
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        if (lower_ascii(attempt.candidate_prompt) == lower_ascii(example.prompt)) {
            attempt.error = "Teacher copied the trusted prompt instead of creating a diverse example.";
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        attempt.verification = verify_candidate(
            request,
            example,
            verifier,
            attempt.candidate_prompt,
            attempt.candidate_response,
            stop_token);
    } catch (const std::exception& error) {
        attempt.error = "Teacher attempt failed: " + std::string(error.what());
        attempt.verification.reason = attempt.error;
    }
    return attempt;
}

[[nodiscard]] bool append_teacher_audit(
    const PiggybackRequest& request,
    const VariationAttempt& attempt,
    const std::string_view target_skill) {
    if (request.audit_log.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(request.audit_log.parent_path(), error);
    if (error) {
        return false;
    }
    const auto created = memory::now_unix_ms();
    core::json::Value::Object record{
        {"id", "teacher-attempt-" + std::to_string(created) + "-" +
            std::to_string(attempt.seed_index) + "-" + attempt.teacher.id},
        {"skill", std::string(target_skill)},
        {"prompt", attempt.candidate_prompt.empty() ? attempt.seed.prompt : attempt.candidate_prompt},
        {"response", attempt.candidate_response},
        {"model_id", attempt.teacher.id},
        {"teacher_name", attempt.teacher.display_name},
        {"verifier_model_id", attempt.verifier.has_value() ? attempt.verifier->id : std::string{}},
        {"verifier_name", attempt.verifier.has_value() ? attempt.verifier->display_name : std::string{}},
        {"human_accepted", false},
        {"human_rejected", false},
        {"deterministic_verified", false},
        {"reference_verified", attempt.verification.accepted},
        {"eligible_for_training", false},
        {"score", attempt.verification.accepted ? attempt.verification.score : 0.0},
        {"verification_notes", attempt.verification.reason},
        {"generation_error", attempt.error},
        {"provenance", "parallel-local-teacher-audit"},
        {"knowledge_destination", attempt.verification.accepted ? "lora-incubation" : "quarantine"},
        {"source_prompt", attempt.seed.prompt},
        {"trusted_reference", attempt.seed.reference},
        {"created_unix_ms", static_cast<double>(created)},
    };
    std::ofstream stream(request.audit_log, std::ios::binary | std::ios::app);
    if (!stream) {
        return false;
    }
    stream << core::json::stringify(core::json::Value(std::move(record))) << '\n';
    stream.flush();
    return static_cast<bool>(stream);
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

    std::vector<models::ModelProfile> teachers = request.teachers;
    if (teachers.empty() && !request.teacher.model_name.empty()) {
        teachers.push_back(request.teacher);
    }
    std::vector<models::ModelProfile> unique_teachers;
    for (const auto& teacher : teachers) {
        if (std::ranges::none_of(unique_teachers, [&](const auto& existing) {
                return existing.id == teacher.id;
            })) {
            unique_teachers.push_back(teacher);
        }
    }

    std::vector<models::ModelProfile> verifiers = request.verifiers;
    if (request.verifier.has_value() &&
        std::ranges::none_of(verifiers, [&](const auto& existing) {
            return existing.id == request.verifier->id;
        })) {
        verifiers.push_back(*request.verifier);
    }

    std::vector<std::pair<models::ModelProfile, models::ModelProfile>> assignments;
    for (const auto& teacher : unique_teachers) {
        const auto verifier = std::ranges::find_if(verifiers, [&](const auto& candidate) {
            return candidate.id != teacher.id;
        });
        if (verifier != verifiers.end()) {
            assignments.emplace_back(teacher, *verifier);
        }
    }

    const auto per_teacher = std::clamp<std::size_t>(
        request.maximum_parallel_per_teacher, 1U, 4U);
    result.progress.teacher_models = assignments.size();
    result.progress.maximum_parallel = assignments.size() * per_teacher;
    const auto generate_verified_variations =
        request.generate_teacher_variations && !assignments.empty();


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

    for (const auto& example : examples) {
        if (stop_token.stop_requested()) {
            result.cancelled = true;
            result.progress.message = "Teacher-data generation cancelled.";
            publish_progress(on_progress, result.progress);
            return result;
        }
        ++result.progress.examined;
        result.progress.next_seed_offset = request.seed_offset + result.progress.examined;
        result.progress.current_prompt = example.prompt;
        result.progress.message = "Saving trusted " + result.progress.target_skill + " example " +
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
    }

    if (!generate_verified_variations) {
        result.succeeded = result.progress.examined > 0U;
        result.progress.message = request.generate_teacher_variations
            ? "Trusted examples saved. Diverse generation waits for a distinct verifier."
            : "Trusted dataset import complete.";
        publish_progress(on_progress, result.progress);
        return result;
    }

    const auto wave_capacity = std::max<std::size_t>(1U, result.progress.maximum_parallel);
    for (std::size_t wave_begin = 0; wave_begin < examples.size(); wave_begin += wave_capacity) {
        if (stop_token.stop_requested()) {
            result.cancelled = true;
            result.progress.message = "Teacher-data generation cancelled.";
            publish_progress(on_progress, result.progress);
            return result;
        }

        const auto wave_end = std::min(examples.size(), wave_begin + wave_capacity);
        std::vector<std::future<VariationAttempt>> futures;
        futures.reserve(wave_end - wave_begin);
        for (auto index = wave_begin; index < wave_end; ++index) {
            const auto assignment = assignments[index % assignments.size()];
            const auto example = examples[index];
            futures.push_back(std::async(
                std::launch::async,
                [request, example, index, assignment, target = result.progress.target_skill, stop_token] {
                    return generate_variation(
                        request,
                        example,
                        request.seed_offset + index,
                        assignment.first,
                        assignment.second,
                        target,
                        stop_token);
                }));
        }

        result.progress.message =
            "Running " + std::to_string(futures.size()) + " audited teacher attempts across " +
            std::to_string(assignments.size()) + " local models.";
        publish_progress(on_progress, result.progress);

        for (auto& future : futures) {
            auto attempt = future.get();
            ++result.progress.attempted;
            result.progress.current_prompt = attempt.candidate_prompt.empty()
                ? attempt.seed.prompt
                : attempt.candidate_prompt;

            if (!append_teacher_audit(request, attempt, result.progress.target_skill)) {
                result.error = "Could not append the teacher-attempt audit log.";
                result.progress.message = result.error;
                publish_progress(on_progress, result.progress);
                return result;
            }

            if (!attempt.candidate_prompt.empty() && !attempt.candidate_response.empty()) {
                ++result.progress.generated;
            }
            if (!attempt.verification.accepted) {
                ++result.progress.rejected;
                result.progress.message =
                    attempt.teacher.display_name + " attempt rejected and retained in the audit: " +
                    attempt.verification.reason;
                publish_progress(on_progress, result.progress);
                continue;
            }

            ++result.progress.verified;
            const auto stored = learning_service.submit(memory::Episode{
                .id = {},
                .skill = target_skill_for(request, attempt.seed),
                .prompt = attempt.candidate_prompt,
                .response = attempt.candidate_response,
                .model_id = attempt.teacher.id,
                .score = attempt.verification.score,
                .compiler_passed = false,
                .tests_passed = false,
                .human_accepted = false,
                .human_rejected = false,
                .deterministic_verified = false,
                .reference_verified = true,
                .self_generated = true,
                .eligible_for_training = false,
                .verifier_model_id = attempt.verifier.has_value()
                    ? attempt.verifier->id
                    : std::string{},
                .verification_notes = attempt.verification.reason,
                .provenance = "parallel-local-teacher",
                .knowledge_destination = "lora-incubation",
                .content_hash = {},
                .correction = {},
                .tool_trace_hash = {},
                .created_unix_ms = 0,
            });
            if (stored) {
                ++result.progress.stored;
            }
            result.progress.message =
                attempt.teacher.display_name + " produced an independently verified diverse example.";
            publish_progress(on_progress, result.progress);
        }
    }

    result.succeeded = result.progress.examined > 0U;
    result.progress.message = result.succeeded
        ? "Audited teacher-data batch complete: " +
            std::to_string(result.progress.verified) + " verified, " +
            std::to_string(result.progress.rejected) + " rejected."
        : "No teacher examples were processed.";
    if (!result.succeeded) {
        result.error = result.progress.message;
    }
    publish_progress(on_progress, result.progress);
    return result;
}
} // namespace threadspire::ai::learning
