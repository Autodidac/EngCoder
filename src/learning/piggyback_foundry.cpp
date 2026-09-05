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
    std::string candidate_id;
    std::string source_model_id;
    std::string tool_trace_hash;
    std::string gap_reason;
    bool needs_current_information{false};
    bool compiler_passed{false};
    bool tests_passed{false};
};

struct Verification final {
    bool accepted{false};
    bool revised{false};
    bool needs_evidence{false};
    bool retryable{false};
    bool excluded{false};
    bool coverage_gap{false};
    double score{0.0};
    std::string reason;
    std::string response;
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

[[nodiscard]] std::string canonical_model_identity(
    const models::ModelProfile& model) {
    auto endpoint = lower_ascii(model.endpoint);
    while (!endpoint.empty() && endpoint.back() == '/') {
        endpoint.pop_back();
    }
    auto model_name = lower_ascii(model.model_name);
    if (model_name.empty()) {
        model_name = lower_ascii(model.id);
    }
    return endpoint + '|' + model_name;
}

[[nodiscard]] bool same_model_identity(
    const models::ModelProfile& left,
    const models::ModelProfile& right) {
    return left.id == right.id ||
           canonical_model_identity(left) == canonical_model_identity(right);
}

[[nodiscard]] const models::ModelProfile* find_model_by_id(
    const PiggybackRequest& request,
    const std::string_view id) {
    const auto find = [id](const std::vector<models::ModelProfile>& models)
        -> const models::ModelProfile* {
        const auto found = std::ranges::find(models, id, &models::ModelProfile::id);
        return found == models.end() ? nullptr : &*found;
    };
    if (const auto* found = find(request.teachers); found != nullptr) {
        return found;
    }
    if (const auto* found = find(request.verifiers); found != nullptr) {
        return found;
    }
    if (request.teacher.id == id) {
        return &request.teacher;
    }
    if (request.verifier.has_value() && request.verifier->id == id) {
        return &*request.verifier;
    }
    return nullptr;
}

[[nodiscard]] PiggybackCompletionResponse complete_teacher_request(
    const PiggybackRequest& request,
    const models::ModelProfile& model,
    const std::string_view system_prompt,
    const std::string_view prompt,
    const std::size_t maximum_output_tokens,
    const double temperature,
    const std::stop_token stop_token) {
    if (request.completion) {
        return request.completion(
            model,
            system_prompt,
            prompt,
            maximum_output_tokens,
            temperature,
            stop_token);
    }
    backend::CompletionClient client;
    const auto result = client.complete(
        request.internal_base_url,
        model,
        backend::CompletionRequest{
            .system_prompt = std::string(system_prompt),
            .prompt = std::string(prompt),
            .messages = {},
            .max_output_tokens = maximum_output_tokens,
            .temperature = temperature,
            .adapter_path = {},
            .module_path = {},
            .tools_json = {},
            .stream = true,
            .on_token = {},
            .should_stop = [stop_token] { return stop_token.stop_requested(); },
        });
    return PiggybackCompletionResponse{
        .succeeded = result.succeeded,
        .content = result.content,
        .error = result.error,
        .prompt_tokens = result.prompt_tokens,
        .completion_tokens = result.completion_tokens,
    };
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
    const bool restrict_to_target_domain,
    const bool wrap_dataset) {
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
        example.candidate_id = core::json::string_or(document->find("candidate_id"));
        example.source_model_id = core::json::string_or(document->find("source_model_id"));
        example.tool_trace_hash = core::json::string_or(document->find("tool_trace_hash"));
        example.gap_reason = core::json::string_or(document->find("gap_reason"));
        example.needs_current_information = core::json::bool_or(
            document->find("needs_current_information"), false);
        example.compiler_passed = core::json::bool_or(document->find("compiler_passed"), false);
        example.tests_passed = core::json::bool_or(document->find("tests_passed"), false);
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

    if (!wrap_dataset && offset >= available.size()) {
        return {};
    }
    const auto begin = wrap_dataset ? offset % available.size() : offset;
    const auto count = std::min(
        maximum,
        wrap_dataset ? available.size() : available.size() - begin);
    std::vector<SeedExample> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        result.push_back(available[
            wrap_dataset ? (begin + index) % available.size() : begin + index]);
    }
    return result;
}

[[nodiscard]] std::string target_skill_for(
    const PiggybackRequest& request,
    const SeedExample& example) {
    return request.untrusted_live_examples || request.target_skill.empty()
        ? skill_for_seed(example)
        : normalize_skill_id(request.target_skill);
}

[[nodiscard]] std::string teacher_system_prompt(
    const std::string_view target_skill,
    const bool measured_gap_analysis) {
    const auto normalized = normalize_skill_id(target_skill);
    std::string target_instruction;
    if (normalized.starts_with("english")) {
        target_instruction =
            "Use complete grammatical English, natural conversation, instruction following, factual explanation, "
            "summarization, and precise wording. If the task is genuinely ambiguous, teach one concise clarifying "
            "question instead of guessing. Do not introduce programming unless the task asks for it.";
    } else if (normalized.starts_with("cpp23")) {
        target_instruction =
            "Use modern C++23 correctness, safety, complete source-level answers, and buildable code.";
    } else if (normalized == "msvc-cmake" || normalized == "build-systems") {
        target_instruction =
            "Use correct CMake, MSVC, compiler-diagnostic, and reproducible-build behavior.";
    } else {
        target_instruction = "Stay inside the selected skill: " + normalized + ".";
    }
    if (measured_gap_analysis) {
        return
            "Repair one measured conversational coverage gap for a compact offline assistant. "
            "The supplied answer failed an independent review and is negative evidence, never a trusted answer. "
            "Return JSON only with this exact shape: {\"prompt\":\"clean reusable task\",\"answer\":\"correct standalone answer\"}. "
            "Preserve the useful intent, but correct the behavior and never copy the failed answer. "
            "Do not add names, personal facts, current events, trivia, hidden reasoning, grading, provider identity, or training commentary. " +
            target_instruction;
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
    const auto prompt =
        std::string(request.measured_gap_analysis
            ? "Judge whether this proposed repair correctly closes a measured conversational coverage gap. "
              "The prior answer failed review and is negative evidence, never a trusted reference. "
              "The repaired answer must be correct for the task, standalone, natural, and reusable. "
            : "Judge whether a new supervised example teaches the same reusable behavior as a trusted example. "
              "The new prompt must be genuinely different, and its answer must be correct for that new prompt, standalone, ") +
        "fluent, and free of personal facts, current facts, unsupported claims, or copied boilerplate. "
        "Return JSON only with this exact shape: "
        "{\"accepted\":true|false,\"score\":0.0,\"reason\":\"brief reason\"}.\n\n"
        "SOURCE TASK:\n" + example.prompt +
        (request.measured_gap_analysis
            ? "\n\nFAILED ANSWER (NEGATIVE EVIDENCE):\n"
            : "\n\nTRUSTED ANSWER:\n") + example.reference +
        "\n\nNEW TASK:\n" + std::string(candidate_prompt) +
        "\n\nNEW ANSWER:\n" + std::string(candidate_response);
    const auto result = complete_teacher_request(
        request,
        verifier,
        "You are an independent strict dataset verifier. Output valid JSON only.",
        prompt,
        192,
        0.0,
        stop_token);
    if (!result.succeeded) {
        verification.retryable = true;
        verification.reason = "Verifier failed: " + result.error;
        return verification;
    }
    const auto document = extract_json_object(result.content);
    if (!document.has_value()) {
        verification.retryable = true;
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

[[nodiscard]] bool valid_tool_trace_receipt(const std::string_view value) {
    constexpr std::string_view prefix{"tool-trace-v1:"};
    return value.starts_with(prefix) && value.size() == prefix.size() + 16U &&
        std::ranges::all_of(value.substr(prefix.size()), [](const unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

[[nodiscard]] bool evidence_constrained_live_example(const SeedExample& example) {
    const auto skill = normalize_skill_id(example.declared_skill);
    const auto coding = skill.starts_with("cpp23") || skill == "build-systems" ||
        skill == "msvc-cmake";
    return (example.needs_current_information &&
            !valid_tool_trace_receipt(example.tool_trace_hash)) ||
           (coding && !(example.compiler_passed && example.tests_passed));
}

[[nodiscard]] Verification review_live_example(
    const PiggybackRequest& request,
    const SeedExample& example,
    const models::ModelProfile& reviewer,
    const std::string_view response,
    const bool allow_revision,
    const std::stop_token stop_token) {
    Verification verification;
    verification.response = std::string(response);
    if (evidence_constrained_live_example(example)) {
        verification.needs_evidence = true;
        verification.reason = example.needs_current_information
            ? "Current information needs a cited research/tool result or the user's confirmation."
            : "Coding material needs compiler and test evidence before it can teach weights.";
        return verification;
    }

    const auto action_shape = allow_revision
        ? "accept|revise|reject|needs-evidence"
        : "accept|reject|needs-evidence";
    const auto prompt =
        "Review one live interaction proposed for a compact English-first assistant. "
        "Accept only reusable, natural, correct behavior that will remain useful across users. "
        "A good response may ask one concise clarifying question when the request is ambiguous. "
        "Reject personal or private information, obscure one-off facts, unsupported factual claims, "
        "current information without cited tool evidence, provider branding, emoji noise, private reasoning, "
        "error messages, copied boilerplate, and rigid phrase parroting. "
        "Do not infer truth merely because another model wrote it. " +
        (allow_revision
            ? std::string{"If the behavior is useful but the answer is weak, return a corrected standalone answer. "}
            : std::string{"Do not revise in this confirmation pass. "}) +
        "Set coverage_gap true only when rejection exposes a reusable conversational skill EngCoder should learn; "
        "keep it false for privacy, identity, current facts, unsupported knowledge, junk, or formatting noise. "
        "Return JSON only with this exact shape: "
        "{\"action\":\"" + action_shape +
        "\",\"score\":0.0,\"answer\":\"corrected answer or empty\",\"coverage_gap\":true|false,\"reason\":\"brief reason\"}.\n\n"
        "SKILL:\n" + skill_for_seed(example) +
        "\n\nUSER MESSAGE:\n" + example.prompt +
        "\n\nPROPOSED ANSWER:\n" + std::string(response) +
        "\n\nTOOL EVIDENCE HASH:\n" +
        (example.tool_trace_hash.empty() ? std::string{"none"} : example.tool_trace_hash);
    const auto result = complete_teacher_request(
        request,
        reviewer,
        "You are EngCoder's strict independent live-training reviewer. Output valid JSON only.",
        prompt,
        320,
        0.0,
        stop_token);
    if (!result.succeeded) {
        verification.retryable = true;
        verification.reason = "Reviewer failed: " + result.error;
        return verification;
    }
    const auto document = extract_json_object(result.content);
    if (!document.has_value()) {
        verification.retryable = true;
        verification.reason = "Reviewer returned malformed JSON.";
        return verification;
    }
    const auto action = lower_ascii(core::json::string_or(document->find("action")));
    verification.score = std::clamp(core::json::number_or(document->find("score")), 0.0, 1.0);
    verification.reason = core::json::string_or(document->find("reason"));
    if (action == "accept") {
        verification.accepted = verification.score >= request.minimum_verifier_score;
    } else if (allow_revision && action == "revise") {
        verification.response = core::json::string_or(document->find("answer"));
        verification.revised = !verification.response.empty();
    } else if (action == "needs-evidence") {
        verification.needs_evidence = true;
    }
    verification.coverage_gap = action == "reject" &&
        core::json::bool_or(document->find("coverage_gap"), false);
    if (verification.reason.empty()) {
        verification.reason = verification.accepted
            ? "Independent reviewer accepted the reusable live behavior."
            : (verification.needs_evidence
                ? "The interaction needs authoritative evidence before learning."
                : (verification.revised
                    ? "Independent reviewer proposed a corrected answer."
                    : "Independent reviewer rejected the interaction."));
    }
    return verification;
}

[[nodiscard]] bool append_live_training_audit(
    const PiggybackRequest& request,
    const SeedExample& example,
    const std::string_view reviewed_response,
    const models::ModelProfile* reviewer,
    const models::ModelProfile* confirmer,
    const Verification& verification) {
    if (request.audit_log.empty()) {
        return true;
    }
    std::error_code error;
    std::filesystem::create_directories(request.audit_log.parent_path(), error);
    if (error) {
        return false;
    }
    const auto created = memory::now_unix_ms();
    const auto action = verification.retryable
        ? "retry-pending"
        : verification.excluded
        ? "excluded"
        : verification.accepted
        ? (verification.revised ? "revised-and-accepted" : "accepted")
        : (verification.needs_evidence ? "needs-evidence" : "rejected");
    core::json::Value::Object record{
        {"id", example.candidate_id.empty()
            ? "live-attempt-" + std::to_string(created)
            : example.candidate_id},
        {"skill", skill_for_seed(example)},
        {"prompt", example.prompt},
        {"response", std::string(reviewed_response)},
        {"source_response", example.reference},
        {"model_id", example.source_model_id},
        {"teacher_name", example.source_model_id},
        {"verifier_model_id", reviewer == nullptr ? std::string{} : reviewer->id},
        {"verifier_name", reviewer == nullptr ? std::string{} : reviewer->display_name},
        {"confirmation_model_id", confirmer == nullptr ? std::string{} : confirmer->id},
        {"confirmation_name", confirmer == nullptr ? std::string{} : confirmer->display_name},
        {"auto_action", action},
        {"human_accepted", false},
        {"human_rejected", false},
        {"deterministic_verified", example.compiler_passed && example.tests_passed},
        {"reference_verified", verification.accepted},
        {"eligible_for_training", verification.accepted},
        {"score", verification.accepted ? verification.score : 0.0},
        {"verification_notes", verification.reason},
        {"generation_error", verification.retryable ? verification.reason : std::string{}},
        {"provenance", "live-auto-training"},
        {"knowledge_destination", verification.retryable
            ? "pending-review"
            : (verification.excluded
                ? "excluded"
            : (verification.accepted
                ? "lora-incubation"
                : (verification.needs_evidence ? "needs-evidence" : "quarantine")))},
        {"tool_trace_hash", example.tool_trace_hash},
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

[[nodiscard]] bool append_measured_gap_candidate(
    const PiggybackRequest& request,
    const SeedExample& example,
    const std::string_view reason) {
    if (request.measured_gap_dataset.empty()) {
        return true;
    }
    const auto prompt = sanitize_training_text(example.prompt);
    const auto failed_response = sanitize_training_response(
        example.reference,
        example.source_model_id);
    if (prompt.empty() || failed_response.empty()) {
        return false;
    }
    const auto content_hash = memory::make_content_hash(
        "measured-conversation-gap",
        prompt,
        failed_response);
    std::error_code error;
    std::filesystem::create_directories(
        request.measured_gap_dataset.parent_path(),
        error);
    if (error) {
        return false;
    }
    if (std::filesystem::is_regular_file(request.measured_gap_dataset)) {
        std::ifstream existing(request.measured_gap_dataset, std::ios::binary);
        const std::string contents{
            std::istreambuf_iterator<char>{existing},
            std::istreambuf_iterator<char>{}};
        if (contents.find(content_hash) != std::string::npos) {
            return true;
        }
    }
    core::json::Value::Object candidate{
        {"candidate_id", "measured-gap-" + content_hash},
        {"content_hash", content_hash},
        {"domain", "measured-conversation-gap"},
        {"skill", skill_for_seed(example)},
        {"prompt", prompt},
        {"response", failed_response},
        {"source_model_id", example.source_model_id},
        {"gap_reason", std::string(reason)},
        {"negative_evidence", true},
        {"created_unix_ms", static_cast<double>(memory::now_unix_ms())},
    };
    std::ofstream stream(
        request.measured_gap_dataset,
        std::ios::binary | std::ios::app);
    if (!stream) {
        return false;
    }
    stream << core::json::stringify(core::json::Value(std::move(candidate))) << '\n';
    stream.flush();
    return static_cast<bool>(stream);
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
        const auto generated = complete_teacher_request(
            request,
            teacher,
            teacher_system_prompt(target_skill, request.measured_gap_analysis),
            "SOURCE TASK:\n" + example.prompt +
                (request.measured_gap_analysis
                    ? "\n\nFAILED ANSWER (NEGATIVE EVIDENCE):\n"
                    : "\n\nTRUSTED ANSWER:\n") + example.reference +
                (example.gap_reason.empty()
                    ? std::string{}
                    : "\n\nREVIEWED GAP:\n" + example.gap_reason) +
                (request.measured_gap_analysis
                    ? "\n\nReturn a clean task and corrected answer that close this measured gap."
                    : "\n\nCreate one new task and answer that teach the same behavior."),
            request.maximum_output_tokens,
            0.45,
            stop_token);
        if (!generated.succeeded || generated.content.empty()) {
            attempt.error = generated.error.empty()
                ? "Teacher returned no content."
                : "Teacher failed: " + generated.error;
            attempt.verification.retryable = true;
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        const auto document = extract_json_object(generated.content);
        if (!document.has_value()) {
            attempt.error = "Teacher returned malformed JSON.";
            attempt.verification.retryable = true;
            attempt.verification.reason = attempt.error;
            return attempt;
        }
        attempt.candidate_prompt = core::json::string_or(document->find("prompt"));
        attempt.candidate_response = core::json::string_or(document->find("answer"));
        attempt.candidate_prompt = sanitize_training_text(attempt.candidate_prompt);
        attempt.candidate_response = sanitize_training_response(
            attempt.candidate_response,
            teacher.id);
        if (attempt.candidate_prompt.empty() || attempt.candidate_response.empty()) {
            attempt.error =
                "Teacher JSON omitted a usable prompt or answer after the training-only privacy, identity, and noise filter.";
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
        attempt.verification.retryable = true;
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
        {"provenance", request.measured_gap_analysis
            ? "measured-gap-qwen-audit"
            : "parallel-local-teacher-audit"},
        {"auto_action", attempt.verification.retryable
            ? "retry-pending"
            : (attempt.verification.excluded
                ? "excluded"
                : (attempt.verification.accepted ? "accepted" : "rejected"))},
        {"knowledge_destination", attempt.verification.retryable
            ? "pending-review"
            : (attempt.verification.excluded
                ? "excluded"
                : (attempt.verification.accepted ? "lora-incubation" : "quarantine"))},
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
                return same_model_identity(existing, teacher);
            })) {
            unique_teachers.push_back(teacher);
        }
    }

    std::vector<models::ModelProfile> verifiers = request.verifiers;
    if (request.verifier.has_value() &&
        std::ranges::none_of(verifiers, [&](const auto& existing) {
            return same_model_identity(existing, *request.verifier);
        })) {
        verifiers.push_back(*request.verifier);
    }

    std::vector<std::pair<models::ModelProfile, models::ModelProfile>> assignments;
    for (const auto& teacher : unique_teachers) {
        const auto verifier = std::ranges::find_if(verifiers, [&](const auto& candidate) {
            return !same_model_identity(candidate, teacher);
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
        request.restrict_to_target_domain,
        request.wrap_seed_dataset);
    if (examples.empty()) {
        result.error = "No usable seed prompts for target '" + result.progress.target_skill +
            "' were found in " + request.seed_dataset.string();
        return result;
    }

    std::vector<SeedExample> variation_examples;
    variation_examples.reserve(examples.size());
    for (const auto& example : examples) {
        if (stop_token.stop_requested()) {
            result.cancelled = true;
            result.progress.message = "Teacher-data generation cancelled.";
            publish_progress(on_progress, result.progress);
            return result;
        }
        ++result.progress.examined;
        if (!request.untrusted_live_examples) {
            result.progress.next_seed_offset = request.seed_offset + result.progress.examined;
        }
        result.progress.current_prompt = example.prompt;
        if (request.untrusted_live_examples) {
            result.progress.message = "Checking live interaction " +
                std::to_string(result.progress.examined) + " of " +
                std::to_string(examples.size()) + " with an independent local model.";
            publish_progress(on_progress, result.progress);

            const auto* source_model = find_model_by_id(request, example.source_model_id);
            const auto reviewer_it = std::ranges::find_if(verifiers, [&](const auto& candidate) {
                return candidate.id != example.source_model_id &&
                    (source_model == nullptr || !same_model_identity(candidate, *source_model));
            });
            Verification verification;
            const models::ModelProfile* reviewer = nullptr;
            const models::ModelProfile* confirmer = nullptr;
            auto reviewed_response = example.reference;
            if (reviewer_it == verifiers.end()) {
                verification.retryable = true;
                verification.reason =
                    "A distinct local reviewer is required; the source model cannot approve itself.";
            } else {
                reviewer = &*reviewer_it;
                verification = review_live_example(
                    request,
                    example,
                    *reviewer,
                    reviewed_response,
                    true,
                    stop_token);
                if (verification.revised) {
                    reviewed_response = sanitize_training_response(
                        verification.response,
                        reviewer->id);
                    if (reviewed_response.empty()) {
                        verification.accepted = false;
                        verification.revised = false;
                        verification.excluded = true;
                        verification.reason =
                            "The proposed revision was empty after the training-only privacy, identity, and noise filter.";
                    }
                }
                if (verification.revised) {
                    const auto confirmer_it = std::ranges::find_if(verifiers, [&](const auto& candidate) {
                        return !same_model_identity(candidate, *reviewer);
                    });
                    if (confirmer_it == verifiers.end()) {
                        verification.accepted = false;
                        verification.retryable = true;
                        verification.reason =
                            "A revised answer needs confirmation from a second distinct local model.";
                    } else {
                        confirmer = &*confirmer_it;
                        auto confirmation = review_live_example(
                            request,
                            example,
                            *confirmer,
                            reviewed_response,
                            false,
                            stop_token);
                        confirmation.revised = true;
                        if (confirmation.accepted) {
                            confirmation.response = reviewed_response;
                            verification = std::move(confirmation);
                            ++result.progress.revised;
                        } else {
                            verification.accepted = false;
                            verification.needs_evidence = confirmation.needs_evidence;
                            verification.retryable = confirmation.retryable;
                            verification.reason = "Revision was not independently confirmed: " +
                                confirmation.reason;
                        }
                    }
                }
            }

            std::optional<memory::Episode> admitted_episode;
            auto original_already_stored = false;
            if (verification.accepted) {
                memory::Episode candidate_episode{
                    .id = {},
                    .skill = target_skill_for(request, example),
                    .prompt = example.prompt,
                    .response = reviewed_response,
                    .model_id = example.source_model_id,
                    .score = verification.score,
                    .compiler_passed = example.compiler_passed,
                    .tests_passed = example.tests_passed,
                    .human_accepted = false,
                    .human_rejected = false,
                    .deterministic_verified = example.compiler_passed && example.tests_passed,
                    .reference_verified = true,
                    .self_generated = true,
                    .eligible_for_training = false,
                    .verifier_model_id = confirmer != nullptr ? confirmer->id : reviewer->id,
                    .verification_notes = verification.reason,
                    .provenance = verification.revised
                        ? "live-auto-training-revised"
                        : "live-auto-training",
                    .knowledge_destination = "lora-incubation",
                    .content_hash = {},
                    .correction = {},
                    .tool_trace_hash = example.tool_trace_hash,
                    .created_unix_ms = 0,
                };
                const auto admission = learning_service.preview(candidate_episode);
                if (admission.duplicate) {
                    original_already_stored = true;
                    candidate_episode.prompt = admission.stored_prompt;
                    candidate_episode.response = admission.stored_response;
                    reviewed_response = candidate_episode.response;
                } else if (!admission.would_store) {
                    verification.accepted = false;
                    verification.excluded = true;
                    verification.reason = "Admission gate excluded the reviewed interaction: " +
                        admission.reason;
                } else {
                    candidate_episode.prompt = admission.stored_prompt;
                    candidate_episode.response = admission.stored_response;
                    reviewed_response = candidate_episode.response;
                    admitted_episode = std::move(candidate_episode);
                }
            }

            if (verification.needs_evidence) {
                ++result.progress.needs_evidence;
            }
            if (!append_live_training_audit(
                    request,
                    example,
                    reviewed_response,
                    reviewer,
                    confirmer,
                    verification)) {
                result.error = "Could not append the live-training audit log.";
                result.progress.message = result.error;
                publish_progress(on_progress, result.progress);
                return result;
            }
            if (verification.retryable) {
                ++result.progress.retry_pending;
                result.progress.message =
                    "Live interaction retained for retry; no learning decision was made: " +
                    verification.reason;
                publish_progress(on_progress, result.progress);
                // The cursor intentionally remains on this record. Preserve
                // ledger order and retry after a backoff instead of skipping
                // later records around an unresolved review.
                break;
            }
            result.progress.next_seed_offset =
                request.seed_offset + result.progress.examined;
            if (!verification.accepted) {
                if (verification.coverage_gap &&
                    !append_measured_gap_candidate(
                        request,
                        example,
                        verification.reason)) {
                    result.error = "Could not append the measured conversation-gap ledger.";
                    result.progress.message = result.error;
                    publish_progress(on_progress, result.progress);
                    return result;
                }
                if (verification.coverage_gap) {
                    ++result.progress.coverage_gaps;
                }
                if (verification.excluded) {
                    ++result.progress.excluded;
                } else {
                    ++result.progress.rejected;
                }
                if (!verification.needs_evidence && !verification.excluded &&
                    !example.source_model_id.empty()) {
                    result.progress.rejected_source_models.push_back(example.source_model_id);
                }
                result.progress.message = verification.excluded
                    ? "Live interaction excluded by the final admission gate: " + verification.reason
                    : verification.needs_evidence
                    ? "Live interaction held for evidence or a user answer: " + verification.reason
                    : verification.coverage_gap
                    ? "Live interaction rejected, retained in the audit, and counted as a measured conversation gap: " + verification.reason
                    : "Live interaction rejected and retained in the audit: " + verification.reason;
                publish_progress(on_progress, result.progress);
                continue;
            }

            auto accepted_example = example;
            accepted_example.reference = reviewed_response;
            variation_examples.push_back(accepted_example);
            if (!original_already_stored) {
                ++result.progress.verified;
                if (!verification.revised && !accepted_example.source_model_id.empty()) {
                    result.progress.accepted_source_models.push_back(accepted_example.source_model_id);
                } else if (verification.revised && !accepted_example.source_model_id.empty()) {
                    result.progress.rejected_source_models.push_back(accepted_example.source_model_id);
                }
            }
            const auto stored = admitted_episode.has_value() &&
                learning_service.submit(std::move(*admitted_episode));
            if (stored) {
                ++result.progress.stored;
            }
            result.progress.message = original_already_stored
                ? "Previously accepted interaction was reused only to retry its derived example."
                : verification.revised
                ? "Live interaction was revised, cross-checked, and accepted once."
                : "Live interaction passed an independent check and was accepted once.";
            publish_progress(on_progress, result.progress);
            continue;
        }

        if (request.measured_gap_analysis) {
            // The seed answer is the failed output that exposed the gap. It is
            // negative evidence for Qwen's repair prompt and must never be
            // submitted to verified memory or the weight-training queue.
            result.progress.message = "Loading measured conversation gap " +
                std::to_string(result.progress.examined) + " of " +
                std::to_string(examples.size()) + " as negative evidence.";
            publish_progress(on_progress, result.progress);
            variation_examples.push_back(example);
            continue;
        }

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
        variation_examples.push_back(example);
    }

    if (!generate_verified_variations) {
        result.succeeded = result.progress.examined > 0U;
        result.progress.message = request.generate_teacher_variations
            ? (request.untrusted_live_examples
                ? "Live interactions checked. Diverse generation waits for a distinct verifier."
                : "Trusted examples saved. Diverse generation waits for a distinct verifier.")
            : "Trusted dataset import complete.";
        publish_progress(on_progress, result.progress);
        return result;
    }

    if (variation_examples.empty()) {
        result.succeeded = result.progress.examined > 0U;
        result.progress.message = "Live review batch complete: no interaction passed the evidence gate.";
        publish_progress(on_progress, result.progress);
        return result;
    }

    const auto wave_capacity = std::max<std::size_t>(1U, result.progress.maximum_parallel);
    auto retry_variation_batch = false;
    for (std::size_t wave_begin = 0; wave_begin < variation_examples.size();
         wave_begin += wave_capacity) {
        if (stop_token.stop_requested()) {
            result.cancelled = true;
            result.progress.message = "Teacher-data generation cancelled.";
            publish_progress(on_progress, result.progress);
            return result;
        }

        const auto wave_end = std::min(variation_examples.size(), wave_begin + wave_capacity);
        std::vector<std::future<VariationAttempt>> futures;
        futures.reserve(wave_end - wave_begin);
        for (auto index = wave_begin; index < wave_end; ++index) {
            const auto assignment = assignments[index % assignments.size()];
            const auto example = variation_examples[index];
            const auto example_target = target_skill_for(request, example);
            futures.push_back(std::async(
                std::launch::async,
                [request, example, index, assignment, target = example_target, stop_token] {
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

            std::optional<memory::Episode> admitted_episode;
            if (attempt.verification.accepted) {
                memory::Episode candidate_episode{
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
                    .provenance = request.measured_gap_analysis
                        ? "measured-gap-qwen-teacher"
                        : "parallel-local-teacher",
                    .knowledge_destination = "lora-incubation",
                    .content_hash = {},
                    .correction = {},
                    .tool_trace_hash = {},
                    .created_unix_ms = 0,
                };
                const auto admission = learning_service.preview(candidate_episode);
                if (!admission.would_store) {
                    attempt.verification.accepted = false;
                    attempt.verification.excluded = true;
                    attempt.verification.reason =
                        "Admission gate excluded the generated example: " + admission.reason;
                } else {
                    candidate_episode.prompt = admission.stored_prompt;
                    candidate_episode.response = admission.stored_response;
                    attempt.candidate_prompt = candidate_episode.prompt;
                    attempt.candidate_response = candidate_episode.response;
                    admitted_episode = std::move(candidate_episode);
                }
            }

            if (!append_teacher_audit(request, attempt, result.progress.target_skill)) {
                result.error = "Could not append the teacher-attempt audit log.";
                result.progress.message = result.error;
                publish_progress(on_progress, result.progress);
                return result;
            }

            if (!attempt.candidate_prompt.empty() && !attempt.candidate_response.empty()) {
                ++result.progress.generated;
            }
            if (attempt.verification.retryable) {
                ++result.progress.retry_pending;
                retry_variation_batch = true;
                result.progress.message =
                    attempt.teacher.display_name +
                    " attempt retained for retry; no negative decision was recorded: " +
                    attempt.verification.reason;
                publish_progress(on_progress, result.progress);
                continue;
            }
            if (!attempt.verification.accepted) {
                if (attempt.verification.excluded) {
                    ++result.progress.excluded;
                } else {
                    ++result.progress.rejected;
                }
                result.progress.message = attempt.teacher.display_name +
                    (attempt.verification.excluded
                        ? " attempt excluded by the final admission gate: "
                        : " attempt rejected and retained in the audit: ") +
                    attempt.verification.reason;
                publish_progress(on_progress, result.progress);
                continue;
            }

            ++result.progress.verified;
            const auto stored = admitted_episode.has_value() &&
                learning_service.submit(std::move(*admitted_episode));
            if (stored) {
                ++result.progress.stored;
                if (request.measured_gap_analysis) {
                    ++result.progress.deep_gap_verified;
                }
            }
            result.progress.message =
                attempt.teacher.display_name + " produced an independently verified diverse example.";
            publish_progress(on_progress, result.progress);
        }
    }

    if ((request.untrusted_live_examples || request.measured_gap_analysis) &&
        retry_variation_batch) {
        // Re-run the bounded source batch after backoff so a transient teacher
        // or verifier failure cannot silently lose its derived example. Exact
        // accepted episodes deduplicate on the retry.
        result.progress.next_seed_offset = request.seed_offset;
    }

    result.succeeded = result.progress.examined > 0U;
    result.progress.message = result.succeeded
        ? (request.untrusted_live_examples
            ? "Live Auto Training batch complete: " +
                std::to_string(result.progress.verified) + " verified, " +
                std::to_string(result.progress.revised) + " revised, " +
                std::to_string(result.progress.needs_evidence) + " need evidence, " +
                std::to_string(result.progress.retry_pending) + " retry pending, " +
                std::to_string(result.progress.excluded) + " excluded, " +
                std::to_string(result.progress.rejected) + " rejected."
            : "Audited teacher-data batch complete: " +
                std::to_string(result.progress.verified) + " verified, " +
                std::to_string(result.progress.rejected) + " rejected.")
        : "No teacher examples were processed.";
    if (!result.succeeded) {
        result.error = result.progress.message;
    }
    publish_progress(on_progress, result.progress);
    return result;
}
} // namespace threadspire::ai::learning
