
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

import threadspire.core;
import threadspire.learning;
import threadspire.mcp;
import threadspire.models;
import threadspire.platform;
import threadspire.providers;
import threadspire.scheduler;
import threadspire.storage;
import threadspire.tasks;

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::filesystem::path temporary_root() {
    const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
    auto root = std::filesystem::temp_directory_path() /
                ("threadspire-ai-tests-" + std::to_string(value));
    std::filesystem::create_directories(root);
    return root;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.is_open(), "failed to open " + path.string());
    const std::string text{
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
    return text;
}

void test_task_analyzer_fast_path() {
    threadspire::ai::routing::TaskAnalyzer analyzer;
    const auto analysis = analyzer.analyze("What is RAII?");
    require(analysis.tier == threadspire::ai::routing::ComplexityTier::fast,
            "short explanation should avoid deep planning");
    require(analysis.kind == threadspire::ai::routing::TaskKind::explanation,
            "RAII prompt should be an explanation");
}

void test_task_analyzer_long_horizon() {
    threadspire::ai::routing::TaskAnalyzer analyzer;
    const auto analysis = analyzer.analyze(
        "Inspect the repository, refactor the C++23 renderer ownership model, build it, run tests, "
        "diagnose failures, and keep iterating until the full project passes.");
    require(analysis.tier == threadspire::ai::routing::ComplexityTier::deep,
            "repository loop should be deep");
    require(analysis.needs_repository && analysis.needs_tools,
            "repository loop should require repository and tools");
}

void test_router_uses_fast_model_for_simple_work() {
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    auto* fast = registry.find("fast");
    auto* coder = registry.find("coder");
    auto* thinker = registry.find("thinker");
    require(fast != nullptr && coder != nullptr && thinker != nullptr,
            "default internal lanes must exist");
    fast->tokens_per_second = 1.0;
    fast->quality = 0.10;
    fast->reliability = 0.30;
    coder->tokens_per_second = 500.0;
    coder->quality = 0.99;
    coder->reliability = 0.99;
    thinker->quality = 0.99;
    thinker->reliability = 0.99;
    threadspire::ai::routing::TaskAnalyzer analyzer;
    threadspire::ai::routing::AdaptiveRouter router;
    const auto plan = router.plan(analyzer.analyze("hello"), registry);
    require(plan.mode == threadspire::ai::routing::RouteMode::direct,
            "short chat should use direct route");
    require(plan.stages.size() == 1 && plan.stages.front().model_id == "fast",
            "direct route should select the hot fast model");
}

void test_router_builds_agentic_code_loop() {
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    threadspire::ai::routing::TaskAnalyzer analyzer;
    threadspire::ai::routing::AdaptiveRouter router;
    const auto plan = router.plan(analyzer.analyze(
        "Refactor this C++23 repository architecture, compile every target, run the tests, and repair all failures."),
        registry);
    require(plan.mode == threadspire::ai::routing::RouteMode::agentic,
            "deep repository task should use agentic route");
    require(plan.stages.size() >= 2,
            "agentic route should split planning/execution/review");
}

void test_model_registry_round_trip() {
    const auto root = temporary_root();
    const auto path = root / "models.ini";
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    registry.save(path);
    auto loaded = threadspire::ai::models::ModelRegistry::load(path);
    require(loaded.models().size() == registry.models().size(),
            "model registry count should round trip");
    require(loaded.find("coder") != nullptr && loaded.find("coder")->supports_tools,
            "coder capabilities should persist");
    std::filesystem::remove_all(root);
}

void test_immediate_memory_retrieval() {
    const auto root = temporary_root();
    threadspire::ai::memory::EpisodeStore store(root / "episodes.jsonl");
    store.append({
        .id = "episode-1",
        .skill = "cpp23-debugging",
        .prompt = "Fix a dangling std::string_view in C++23",
        .response = "Give the referenced string an owning lifetime or return std::string.",
        .model_id = "coder",
        .score = 0.98,
        .compiler_passed = true,
        .tests_passed = true,
        .human_accepted = true,
        .human_rejected = false,
        .deterministic_verified = true,
        .reference_verified = false,
        .self_generated = false,
        .eligible_for_training = true,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = "test-fixture",
        .knowledge_destination = "lora-incubation",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = {},
        .created_unix_ms = threadspire::ai::memory::now_unix_ms(),
    });
    const auto recalled = store.retrieve("Why is this string_view dangling?", 3);
    require(!recalled.empty(), "accepted memory should be retrievable without weight training");
    require(recalled.front().episode.id == "episode-1", "relevant memory should rank first");
    const auto exact = store.exact_verified("  FIX a dangling std::string_view in C++23!!! ");
    require(exact.has_value() && exact->episode.response == recalled.front().episode.response,
            "exact verified lookup must ignore harmless case, spacing, and punctuation changes");
    require(!store.exact_verified("Fix a different lifetime bug").has_value(),
            "verified lookup must not guess from a merely similar prompt");
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    threadspire::ai::backend::AgentOrchestrator orchestrator(registry, {}, store);
    const auto direct = orchestrator.execute(
        {},
        "Fix a dangling std::string_view in C++23.");
    require(direct.succeeded && direct.models_used == std::vector<std::string>{"verified-memory"},
            "orchestration must answer exact verified knowledge without contacting an endpoint");

    const auto append_conversation = [&store](
                                         std::string id,
                                         std::string prompt,
                                         std::string response) {
        store.append({
            .id = std::move(id),
            .skill = "english-conversation",
            .prompt = std::move(prompt),
            .response = std::move(response),
            .model_id = "human-approved-memory",
            .score = 0.92,
            .compiler_passed = false,
            .tests_passed = false,
            .human_accepted = true,
            .human_rejected = false,
            .deterministic_verified = false,
            .reference_verified = false,
            .self_generated = false,
            .eligible_for_training = false,
            .verifier_model_id = "human",
            .verification_notes = "accepted in test",
            .provenance = "human-accepted",
            .knowledge_destination = "retrieval-memory",
            .content_hash = {},
            .correction = {},
            .tool_trace_hash = {},
            .created_unix_ms = threadspire::ai::memory::now_unix_ms(),
        });
    };
    append_conversation(
        "conversation-1",
        "What's up?",
        "Not much. What is on your mind?");
    const auto spelling_match = store.verified_match(
        "want's up?",
        "english-conversation");
    require(spelling_match.has_value() &&
                spelling_match->episode.response == "Not much. What is on your mind?" &&
                spelling_match->relevance < 1.0,
            "short conversation memory should tolerate one conservative spelling error");
    require(!store.verified_match("want's up?", "cpp23-generation").has_value(),
            "coding skills must never use fuzzy conversation memory");

    append_conversation("conversation-2", "What is ready?", "The first item is ready.");
    append_conversation("conversation-3", "When is ready?", "It is ready tomorrow.");
    require(!store.verified_match("Whan is ready?", "english-conversation").has_value(),
            "equally close approved prompts must be rejected as ambiguous");
    require(store.human_acceptance_count_for_skill("english-conversation") == 3U,
            "human approval counts must be separate from reference and deterministic evidence");
    std::filesystem::remove_all(root);
}
void test_manual_model_choice_disables_fallback() {
    const auto root = temporary_root();
    threadspire::ai::memory::EpisodeStore store(root / "memory");

    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    threadspire::ai::backend::AgentOrchestrator orchestrator(registry, {}, store);
    const auto result = orchestrator.execute(
        {},
        "Say hello without using stored memory.",
        threadspire::ai::backend::AgentExecutionOptions{
            .allow_automatic_fallback = false,
        });

    require(!result.succeeded, "an unavailable manually selected model must report failure");
    require(result.models_used.size() == 1U,
            "a locked model request must attempt exactly one model");
    require(result.plan.stages.size() == 1U &&
                result.plan.stages.front().role != "automatic-fallback",
            "a locked model request must never add an automatic fallback stage");
    require(result.error.find("No other AI was used") != std::string::npos,
            "the locked-model failure must clearly say that ThreadSpire did not switch AIs");

    std::filesystem::remove_all(root);
}

void test_expert_lifecycle_thresholds() {
    const auto root = temporary_root();
    threadspire::ai::learning::ExpertRegistry registry(root / "registry");
    auto& module = registry.ensure("cpp23-debugging", "local-base");
    threadspire::ai::learning::PromotionPolicy policy;
    module.episodes_since_update = policy.lora_microbatch;
    require(registry.ready_for_lora(module, policy),
            "LoRA should queue only after a validated microbatch");
    module.state = threadspire::ai::learning::ExpertState::lora_active;
    module.accepted_episodes = policy.consolidate_after_episodes;
    module.lora_generations = policy.consolidate_after_generations;
    module.validation_score = policy.minimum_validation_score;
    module.regression_score = policy.maximum_regression;
    module.compiler_verified_episodes = policy.minimum_compiler_verified_for_code;
    module.test_verified_episodes = policy.minimum_test_verified_for_code;
    require(registry.ready_for_consolidation(module, policy),
            "mature validated LoRA should become eligible for full-rank consolidation");
    std::filesystem::remove_all(root);
}

void test_learning_service_stores_before_training() {
    const auto root = temporary_root();
    threadspire::ai::learning::LearningSettings settings{
        .python_executable = root / "missing-python.exe",
        .worker_script = root / "missing-worker.py",
        .state_root = root,
        .seed_dataset = {},
        .anchor_dataset = {},
        .base_model = "local-base",
        .policy = {},
        .lora_rank = 16,
        .lora_alpha = 32,
        .micro_steps = 12,
        .learning_rate = 1.0e-4,
        .require_cuda = true,
        .automatic_consolidation = true,
    };
    settings.policy.lora_microbatch = 100;
    threadspire::ai::learning::ContinualLearningService service(std::move(settings));
    const auto accepted = service.submit(threadspire::ai::memory::Episode{
        .id = {},
        .skill = "cpp23-generation",
        .prompt = "Write a C++23 scope guard",
        .response = "Use a move-only callable owner whose destructor invokes the callable.",
        .model_id = "coder",
        .score = 0.95,
        .compiler_passed = false,
        .tests_passed = false,
        .human_accepted = true,
        .human_rejected = false,
        .deterministic_verified = false,
        .reference_verified = false,
        .self_generated = true,
        .eligible_for_training = false,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = "test-fixture",
        .knowledge_destination = "episodic-memory",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = {},
        .created_unix_ms = 0,
    });
    require(accepted, "high-quality accepted episode should pass the gate");
    require(service.memory_store().count_for_skill("cpp23-generation") == 1,
            "memory must update immediately before any LoRA job");
    const auto experts = service.expert_snapshot();
    require(experts.size() == 1 && experts.front().accepted_episodes == 1,
            "expert evidence counter should update immediately");
    service.request_stop();
    std::filesystem::remove_all(root);
}

void test_interactive_priority_uses_nested_leases() {
    const auto root = temporary_root();
    threadspire::ai::learning::LearningSettings settings{
        .python_executable = root / "missing-python.exe",
        .worker_script = root / "missing-worker.py",
        .state_root = root,
        .seed_dataset = {},
        .anchor_dataset = {},
        .base_model = "local-base",
        .policy = {},
        .lora_rank = 16,
        .lora_alpha = 32,
        .micro_steps = 12,
        .learning_rate = 1.0e-4,
        .require_cuda = true,
        .automatic_consolidation = true,
        .initial_interactive_priority = true,
    };
    threadspire::ai::learning::ContinualLearningService service(std::move(settings));
    require(service.interactive_busy() && service.interactive_lease_count() == 1U,
            "startup priority must exist before the recovered worker can begin");
    service.set_interactive_busy(false);
    require(!service.interactive_busy(),
            "startup priority must release exactly once after a model becomes ready");
    service.set_interactive_busy(true);
    service.set_interactive_busy(true);
    require(service.interactive_busy(),
            "overlapping foreground and teacher operations must hold inference priority");
    service.set_interactive_busy(false);
    require(service.interactive_busy(),
            "releasing one operation must not resume training while another is active");
    service.set_interactive_busy(false);
    require(!service.interactive_busy(),
            "training may resume only after every interactive lease is released");
    service.request_stop();
    std::filesystem::remove_all(root);
}

void test_episode_dedup_and_negative_evidence() {
    const auto root = temporary_root();
    threadspire::ai::memory::EpisodeStore store(root / "episodes.jsonl");
    threadspire::ai::memory::Episode accepted{
        .id = "accepted",
        .skill = "cpp23-debugging",
        .prompt = "Repair a dangling span",
        .response = "Preserve the owner's lifetime.",
        .model_id = "coder",
        .score = 0.95,
        .compiler_passed = true,
        .tests_passed = true,
        .human_accepted = false,
        .human_rejected = false,
        .deterministic_verified = true,
        .reference_verified = false,
        .self_generated = true,
        .eligible_for_training = true,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = "compiler-test",
        .knowledge_destination = "lora-incubation",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = {},
        .created_unix_ms = threadspire::ai::memory::now_unix_ms(),
    };
    require(store.append_if_new(accepted), "first deterministic episode should be stored");
    require(!store.append_if_new(accepted), "duplicate evidence must be rejected");
    auto competing_approval = accepted;
    competing_approval.id = "accepted-competing-answer";
    competing_approval.response = "Use a different approved answer for the same prompt.";
    competing_approval.content_hash.clear();
    require(!store.append_if_new(competing_approval),
            "a second positive answer for one normalized skill and prompt must not add weight");


    auto provisional = accepted;
    provisional.id = "provisional-a";
    provisional.prompt = "Explain a compact ownership rule";
    provisional.response = "First unverified teacher wording.";
    provisional.deterministic_verified = false;
    provisional.compiler_passed = false;
    provisional.tests_passed = false;
    provisional.eligible_for_training = false;
    provisional.content_hash.clear();
    require(store.append_if_new(provisional), "the first provisional prompt may be quarantined");
    provisional.id = "provisional-b";
    provisional.response = "Different unverified wording for the same prompt.";
    provisional.content_hash.clear();
    require(!store.append_if_new(provisional), "unverified teacher rewrites must not multiply per prompt");
    auto rejected = accepted;
    rejected.id = "rejected";
    rejected.prompt = "Suggest a raw owning pointer";
    rejected.response = "Use a raw pointer as the sole owner.";
    rejected.human_rejected = true;
    rejected.deterministic_verified = false;
    rejected.compiler_passed = false;
    rejected.tests_passed = false;
    rejected.eligible_for_training = false;
    rejected.content_hash.clear();
    require(store.append_if_new(rejected), "negative evidence should be retained");

    const auto recalled = store.retrieve("dangling span owner lifetime", 5);
    require(recalled.size() == 1 && recalled.front().episode.id == "accepted",
            "retrieval should use independently validated positive evidence only");
    std::filesystem::remove_all(root);
}
void test_learning_preview_blocks_errors_and_repetition() {
    const auto root = temporary_root();
    threadspire::ai::learning::LearningSettings settings{
        .python_executable = root / "missing-python.exe",
        .worker_script = root / "missing-worker.py",
        .state_root = root,
        .seed_dataset = {},
        .anchor_dataset = {},
        .base_model = "local-base",
        .policy = {},
        .lora_rank = 16,
        .lora_alpha = 32,
        .micro_steps = 12,
        .learning_rate = 1.0e-4,
        .require_cuda = true,
        .automatic_consolidation = true,
    };
    settings.policy.lora_microbatch = 100;
    threadspire::ai::learning::ContinualLearningService service(std::move(settings));

    const auto accepted_episode = [](const int index) {
        threadspire::ai::memory::Episode episode{};
        episode.skill = "conversation";
        episode.prompt = "Unique approved message " + std::to_string(index);
        episode.response = "Safe useful reply " + std::to_string(index);
        episode.model_id = "internal-fast";
        episode.score = 0.92;
        episode.human_accepted = true;
        episode.self_generated = true;
        episode.provenance = "per-reply-test";
        return episode;
    };

    auto unsafe = accepted_episode(0);
    unsafe.response = "ERROR | MODEL REQUEST\nEvery eligible local model failed";
    const auto unsafe_preview = service.preview(unsafe);
    require(!unsafe_preview.would_store &&
                unsafe_preview.accepted_skill_examples == 0U &&
                unsafe_preview.destination ==
                    threadspire::ai::learning::KnowledgeDestination::reject,
            "visible model errors must never be accepted as learning material");
    require(!service.submit(unsafe) && service.memory_store().count_for_skill("conversation") == 0U,
            "blocked model errors must not touch persistent memory");

    for (int index = 1; index <= 4; ++index) {
        auto episode = accepted_episode(index);
        const auto preview = service.preview(episode);
        require(preview.would_store, "each unique approved reply should be stored once");
        require(preview.eligible_for_weight_training == (index == 4),
                "human approvals must stay memory-only until four unique prompts exist");
        require(service.submit(std::move(episode)),
                "a unique safe per-reply approval should pass the learning gate");
    }

    auto competing = accepted_episode(4);
    competing.response = "A competing answer for the fourth prompt";
    const auto duplicate_preview = service.preview(competing);
    require(duplicate_preview.duplicate && !duplicate_preview.would_store &&
                !service.submit(std::move(competing)),
            "another answer for one approved prompt must be coalesced before storage");
    require(service.memory_store().count_for_skill("conversation") == 4U &&
                service.memory_store().training_count_for_skill("conversation") == 1U,
            "four unique approvals should yield four memory records but only the gated weight record");
    service.request_stop();
    std::filesystem::remove_all(root);
}


void test_episode_index_recovers_after_restart() {
    const auto root = temporary_root();
    const auto path = root / "episodes.jsonl";
    const auto expected_hash = threadspire::ai::memory::make_content_hash(
        "cpp23-debugging",
        "Explain why this owner is invalid",
        "The referenced object has already been destroyed.");
    {
        threadspire::ai::memory::EpisodeStore store(path);
        require(store.append_if_new({
            .id = "restart-episode",
            .skill = "cpp23-debugging",
            .prompt = "Explain why this owner is invalid",
            .response = "The referenced object has already been destroyed.",
            .model_id = "coder",
            .score = 0.97,
            .compiler_passed = true,
            .tests_passed = true,
            .human_accepted = false,
            .human_rejected = false,
            .deterministic_verified = true,
            .reference_verified = false,
            .self_generated = true,
            .eligible_for_training = true,
            .verifier_model_id = {},
            .verification_notes = {},
            .provenance = "compiler-test",
            .knowledge_destination = "lora-incubation",
            .content_hash = expected_hash,
            .correction = {},
            .tool_trace_hash = "trace-1",
            .created_unix_ms = threadspire::ai::memory::now_unix_ms(),
        }), "restart fixture should be appended");
    }
    {
        threadspire::ai::memory::EpisodeStore recovered(path);
        require(recovered.contains_hash(expected_hash),
                "content-hash index must rebuild after restart");
        require(recovered.count_for_skill("cpp23-debugging") == 1,
                "accepted evidence counts must rebuild after restart");
        require(recovered.training_count_for_skill("cpp23-debugging") == 1,
                "training counts must rebuild after restart");
        const auto recalled = recovered.retrieve("destroyed object owner", 2);
        require(recalled.size() == 1 && recalled.front().episode.id == "restart-episode",
                "retrieval postings must rebuild after restart");
        require(!recovered.append_if_new({
            .id = "duplicate-after-restart",
            .skill = "cpp23-debugging",
            .prompt = "Explain why this owner is invalid",
            .response = "The referenced object has already been destroyed.",
            .model_id = "coder",
            .score = 0.97,
            .compiler_passed = true,
            .tests_passed = true,
            .human_accepted = false,
            .human_rejected = false,
            .deterministic_verified = true,
            .reference_verified = false,
            .self_generated = true,
            .eligible_for_training = true,
            .verifier_model_id = {},
            .verification_notes = {},
            .provenance = "compiler-test",
            .knowledge_destination = "lora-incubation",
            .content_hash = expected_hash,
            .correction = {},
            .tool_trace_hash = "trace-1",
            .created_unix_ms = threadspire::ai::memory::now_unix_ms(),
        }), "restart must not weaken deduplication");
    }
    std::filesystem::remove_all(root);
}

void test_knowledge_classifier_routes_volatile_state_to_retrieval() {
    threadspire::ai::learning::KnowledgeClassifier classifier;
    const auto decision = classifier.classify(threadspire::ai::memory::Episode{
        .id = "volatile",
        .skill = "cpp23-repository",
        .prompt = "Remember the current branch and local path on my machine",
        .response = "The branch is feature/local-runtime.",
        .model_id = "coder",
        .score = 0.99,
        .compiler_passed = false,
        .tests_passed = false,
        .human_accepted = true,
        .human_rejected = false,
        .deterministic_verified = false,
        .reference_verified = false,
        .self_generated = false,
        .eligible_for_training = false,
        .verifier_model_id = {},
        .verification_notes = {},
        .provenance = "test",
        .knowledge_destination = {},
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = {},
        .created_unix_ms = 0,
    }, 12);
    require(decision.destination == threadspire::ai::learning::KnowledgeDestination::retrieval_memory,
            "volatile project state must remain outside weights");
    require(!decision.eligible_for_weight_training,
            "volatile state must never be weight-training input");
}

void test_resource_governor_prioritizes_inference() {
    threadspire::ai::resource::ResourceGovernor governor;
    threadspire::ai::resource::ResourceSnapshot idle{
        .cpu_utilization = 0.10,
        .memory_available_gib = 24.0,
        .gpu_utilization = 0.10,
        .gpu_memory_available_gib = 10.0,
        .on_ac_power = true,
        .thermal_pressure = false,
    };
    const auto blocked = governor.plan_training(idle, true, 20, 4096, 32);
    require(!blocked.permitted, "active inference must always block background training");

    idle.cpu_utilization = 0.65;
    idle.gpu_utilization = 0.70;
    const auto reduced = governor.plan_training(idle, false, 20, 4096, 32);
    require(reduced.permitted, "training should proceed when resources remain under policy limits");
    require(reduced.max_steps < 20 && reduced.maximum_sequence_tokens < 4096,
            "resource pressure should reduce the training budget instead of using a fixed percentage");
}

void test_candidate_registry_marks_inflight_work_resumable() {
    const auto root = temporary_root();
    {
        threadspire::ai::learning::CandidateRegistry registry(root / "registry");
        auto candidate = registry.create(
            "cpp23-debugging",
            "local-base",
            threadspire::ai::learning::CandidateKind::lora_overlay,
            7,
            root / "adapter",
            root / "job",
            root / "training.log");
        candidate.state = threadspire::ai::learning::CandidateState::training;
        registry.save(candidate);
    }
    threadspire::ai::learning::CandidateRegistry recovered(root / "registry");
    recovered.load();
    const auto interrupted = recovered.mark_inflight_interrupted();
    require(interrupted.size() == 1, "one in-flight candidate should be recovered");
    require(interrupted.front().state == threadspire::ai::learning::CandidateState::interrupted,
            "crash recovery must convert in-flight work into a resumable state");
    std::filesystem::remove_all(root);
}

void test_expert_registry_persists_generation_and_rollback() {
    const auto root = temporary_root();
    {
        threadspire::ai::learning::ExpertRegistry registry(root / "registry");
        auto& module = registry.ensure("cpp23-generation", "local-base");
        module.state = threadspire::ai::learning::ExpertState::full_rank_active;
        module.module_path = root / "generation-4";
        module.previous_state = threadspire::ai::learning::ExpertState::lora_active;
        module.previous_adapter_path = root / "generation-3-adapter";
        module.generation = 4;
        module.promoted_candidate_id = "candidate-4";
        registry.save(module);
    }
    threadspire::ai::learning::ExpertRegistry loaded(root / "registry");
    loaded.load();
    const auto* module = loaded.find("cpp23-generation");
    require(module != nullptr && module->generation == 4,
            "active generation must survive restart");
    require(module->previous_state == threadspire::ai::learning::ExpertState::lora_active &&
                module->previous_adapter_path.filename() == "generation-3-adapter",
            "rollback target must survive restart");
    require(module->promoted_candidate_id == "candidate-4",
            "promotion provenance must survive restart");
    std::filesystem::remove_all(root);
}

void test_model_feedback_changes_routing_quality() {
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    const auto before = registry.find("fast")->quality;
    registry.record_feedback("fast", false);
    const auto after_rejection = registry.find("fast")->quality;
    require(after_rejection < before, "rejected fast answers should reduce fast-path confidence");
    registry.record_feedback("fast", true);
    require(registry.find("fast")->quality > after_rejection,
            "accepted answers should recover model quality gradually");
}

void test_process_log_append() {
    const auto root = temporary_root();
    const auto log = root / "process.log";
#ifdef _WIN32
    const auto executable = std::filesystem::path("cmd.exe");
    const std::vector<std::string> first_arguments{"/d", "/s", "/c", "echo first"};
    const std::vector<std::string> second_arguments{"/d", "/s", "/c", "echo second"};
#else
    const auto executable = std::filesystem::path("/bin/sh");
    const std::vector<std::string> first_arguments{"-c", "printf 'first\\n'"};
    const std::vector<std::string> second_arguments{"-c", "printf 'second\\n'"};
#endif
    const auto first = threadspire::ai::platform::run_process({
        .executable = executable,
        .arguments = first_arguments,
        .working_directory = root,
        .log_file = log,
        .append_log = false,
    });
    const auto second = threadspire::ai::platform::run_process({
        .executable = executable,
        .arguments = second_arguments,
        .working_directory = root,
        .log_file = log,
        .append_log = true,
    });
    require(first.launched && first.exit_code == 0, "first process should run");
    require(second.launched && second.exit_code == 0, "second process should run");
    const auto contents = read_text_file(log);
    require(contents.find("first") != std::string::npos && contents.find("second") != std::string::npos,
            "append log should preserve prior process output");
    std::filesystem::remove_all(root);
}


void test_json_and_stream_protocols() {
    std::string error;
    const auto document = threadspire::ai::core::json::try_parse(
        R"({"name":"local","count":2,"ok":true})", error);
    require(document.has_value(), "JSON control messages must parse");
    require(threadspire::ai::core::json::string_or(document->find("name")) == "local",
            "JSON string fields must survive parsing");
    require(threadspire::ai::core::json::number_or(document->find("count")) == 2.0,
            "JSON numeric fields must survive parsing");

    threadspire::ai::backend::OpenAiSseDecoder decoder;
    std::vector<std::string> events;
    const auto collect = [&events](const std::string_view event) {
        events.emplace_back(event);
        return true;
    };
    require(decoder.feed("data: {\"token\":\"A\"}\n\nda", collect),
            "SSE decoder must accept split chunks");
    require(decoder.feed("ta: [DONE]\n\n", collect),
            "SSE decoder must finish the split event");
    require(decoder.finish(collect), "SSE decoder must flush cleanly");
    require(events.size() == 2U && events.front().find("token") != std::string::npos &&
                events.back() == "[DONE]",
            "SSE events must be framed without losing split tokens");
}

void test_mcp_tool_schema_conversion() {
    const std::vector<threadspire::ai::mcp::Tool> tools{
        threadspire::ai::mcp::Tool{
            .name = "compile_cpp",
            .description = "Compile a C++23 target",
            .input_schema_json = R"({"type":"object","properties":{"target":{"type":"string"}}})",
        },
    };
    const auto openai = threadspire::ai::mcp::tools_to_openai_json(tools);
    require(openai.find("compile_cpp") != std::string::npos &&
                openai.find("parameters") != std::string::npos,
            "MCP tools must be convertible to model tool schemas");
    const auto payload = threadspire::ai::mcp::extract_json_rpc_payload(
        "event: message\ndata: {\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n\n");
    require(payload.find("jsonrpc") != std::string::npos,
            "MCP Streamable HTTP SSE payloads must be extracted");
}

void test_manual_local_provider_and_role_router() {
    auto registry = threadspire::ai::models::ModelRegistry::defaults();
    for (auto& model : registry.models()) {
        model.enabled = false;
    }
    auto local = threadspire::ai::models::make_manual_local_model(
        "http://127.0.0.1:1234/",
        "my-cpp23-model",
        "openai",
        true,
        true);
    local.id = "local-specialist";
    registry.upsert(local);

    threadspire::ai::routing::TaskAnalyzer analyzer;
    threadspire::ai::routing::AdaptiveRouter router;
    const auto plan = router.plan(
        analyzer.analyze("Write a C++23 resource owner and explain its lifetime"),
        registry);
    require(!plan.stages.empty() && plan.stages.front().model_id == "local-specialist",
            "routing must select a capable local model without hard-coded model IDs");
    require(registry.find("local-specialist")->endpoint == "http://127.0.0.1:1234",
            "manual local provider endpoints must be normalized");
    require(registry.find("local-specialist")->teacher,
            "manual local providers may opt into teacher-data generation");
}

void test_discovery_reconciliation_preserves_runtime_telemetry() {
    auto registry = threadspire::ai::models::ModelRegistry::defaults();

    auto discovered = threadspire::ai::models::make_manual_local_model(
        "http://127.0.0.1:1234",
        "qwen3-coder",
        "openai",
        true,
        true);
    discovered.id = "auto-local";
    discovered.auto_discovered = true;
    require(threadspire::ai::models::merge_discovered_models(registry, {discovered}),
            "first discovery pass should add the local provider");

    auto* stored = registry.find("auto-local");
    require(stored != nullptr, "discovered provider should be present");
    stored->api_key = "local-secret";
    stored->tokens_per_second = 123.0;
    stored->first_token_ms = 44.0;
    stored->quality = 0.91;
    stored->reliability = 0.97;
    stored->enabled = false;
    stored->keep_hot = false;
    stored->supports_tools = false;
    stored->supports_lora = true;
    stored->teacher = false;
    stored->verifier = true;
    stored->custom_model = true;
    stored->coding_agent = false;
    stored->priority = 321;

    auto stale = discovered;
    stale.id = "stale-local";
    stale.model_name = "stale-model";
    registry.upsert(stale);

    static_cast<void>(threadspire::ai::models::merge_discovered_models(registry, {discovered}));
    stored = registry.find("auto-local");
    require(stored != nullptr, "live provider should survive reconciliation");
    require(stored->api_key == "local-secret",
            "rediscovery must preserve local provider credentials");
    require(stored->tokens_per_second == 123.0 && stored->first_token_ms == 44.0,
            "rediscovery must preserve measured token speed and first-token latency");
    require(stored->quality == 0.91 && stored->reliability == 0.97,
            "rediscovery must preserve learned quality and reliability");
    require(!stored->enabled && !stored->keep_hot && !stored->supports_tools &&
                stored->supports_lora && !stored->teacher && stored->verifier &&
                stored->custom_model && !stored->coding_agent && stored->priority == 321,
            "rediscovery must preserve user-configured provider roles and priority");
    require(registry.find("stale-local") == nullptr,
            "providers missing from the latest discovery pass should be removed");
    require(registry.find("fast") != nullptr,
            "reconciliation must not remove built-in or manually managed profiles");
}

void test_reference_verified_evidence_can_incubate() {
    threadspire::ai::learning::KnowledgeClassifier classifier;
    const auto decision = classifier.classify(threadspire::ai::memory::Episode{
        .id = "reference-verified",
        .skill = "cpp23-core",
        .prompt = "Explain std::unique_ptr ownership",
        .response = "It represents exclusive ownership and transfers through move operations.",
        .model_id = "local-teacher",
        .score = 0.91,
        .compiler_passed = false,
        .tests_passed = false,
        .human_accepted = false,
        .human_rejected = false,
        .deterministic_verified = false,
        .reference_verified = true,
        .self_generated = true,
        .eligible_for_training = false,
        .verifier_model_id = "independent-local-verifier",
        .verification_notes = "Equivalent to trusted seed reference",
        .provenance = "local-model-piggyback",
        .knowledge_destination = "episodic-memory",
        .content_hash = {},
        .correction = {},
        .tool_trace_hash = {},
        .created_unix_ms = 0,
    }, 0U);
    require(decision.eligible_for_weight_training &&
                decision.destination == threadspire::ai::learning::KnowledgeDestination::lora_incubation,
            "independently reference-verified local teacher data must reach LoRA incubation");
}

void test_targeted_and_custom_training_import() {
    const auto root = temporary_root();
    const auto dataset = root / "custom.jsonl";
    {
        std::ofstream stream(dataset, std::ios::binary | std::ios::trunc);
        stream
            << R"({"skill":"english-foundation","messages":[{"role":"user","content":"Define dependable."},{"role":"assistant","content":"Dependable means consistently worthy of trust or reliance."}]})"
            << '\n'
            << R"({"skill":"cpp23-core","prompt":"What is RAII?","response":"RAII binds resource lifetime to object lifetime."})"
            << '\n';
    }

    threadspire::ai::learning::LearningSettings settings;
    settings.python_executable = root / "missing-python";
    settings.worker_script = root / "missing-worker.py";
    settings.state_root = root;
    settings.seed_dataset = dataset;
    settings.base_model = "local-base";
    settings.policy.lora_microbatch = 100U;

    {
        threadspire::ai::learning::ContinualLearningService service(std::move(settings));
        threadspire::ai::learning::PiggybackRequest english_request;
        english_request.seed_dataset = dataset;
        english_request.target_skill = "english-foundation";
        english_request.restrict_to_target_domain = true;
        english_request.generate_teacher_variations = false;
        english_request.maximum_examples = 16U;
        const auto english = threadspire::ai::learning::run_piggyback_foundry(
            english_request,
            service);
        require(english.succeeded && english.progress.examined == 1U,
                "English-targeted import must exclude C++ records");
        require(service.memory_store().training_count_for_skill("english-foundation") == 1U &&
                    service.memory_store().training_count_for_skill("cpp23-core") == 0U,
                "English-first import must not silently feed the C++ skill");

        auto no_verifier_request = english_request;
        no_verifier_request.generate_teacher_variations = true;
        const auto safe_seed_pass = threadspire::ai::learning::run_piggyback_foundry(
            no_verifier_request,
            service);
        require(safe_seed_pass.succeeded && safe_seed_pass.progress.generated == 0U &&
                    safe_seed_pass.progress.verified == 0U,
                "automatic learning must keep trusted seeds and skip synthetic rewrites without a distinct verifier");
        threadspire::ai::learning::PiggybackRequest custom_request;
        custom_request.seed_dataset = dataset;
        custom_request.target_skill = "custom-domain";
        custom_request.restrict_to_target_domain = false;
        custom_request.generate_teacher_variations = false;
        custom_request.maximum_examples = 16U;
        const auto custom = threadspire::ai::learning::run_piggyback_foundry(
            custom_request,
            service);
        require(custom.succeeded && custom.progress.examined == 2U,
                "Trusted custom import must accept both messages and prompt/response JSONL records");
        require(service.memory_store().training_count_for_skill("custom-domain") == 2U,
                "A selected custom target must own every imported custom record");
        service.request_stop();
    }
    std::filesystem::remove_all(root);
}

void test_internal_model_lifecycle_state_and_progress() {
    const auto root = temporary_root();
    const auto generation = std::string{"g000004"};
    const auto generation_root =
        root / "models" / "internal" / "generations" / generation;
    std::filesystem::create_directories(generation_root);
    std::filesystem::create_directories(root / "state");
    {
        std::ofstream pointer(root / "models" / "internal" / "active.json");
        pointer << R"({"generation":"g000004"})";
    }
    {
        std::ofstream manifest(generation_root / "manifest.json");
        manifest << R"({
            "name":"ThreadSpire Core Micro",
            "status":"trained",
            "parameter_count":22527360,
            "tokens_seen":4961,
            "steps":4,
            "bootstrap_target_steps":1200,
            "bootstrap_complete":false
        })";
    }
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    {
        std::ofstream progress(root / "state" / "internal-model-progress.json");
        progress << "{\n"
                 << "  \"phase\": \"training\",\n"
                 << "  \"message\": \"resuming\",\n"
                 << "  \"bootstrap_policy\": \"verified-hybrid-conversation-v6\",\n"
                 << "  \"heartbeat_unix_ms\": " << now_ms << ",\n"
                 << "  \"step\": 6,\n"
                 << "  \"maximum_steps\": 1196,\n"
                 << "  \"prior_total_steps\": 4,\n"
                 << "  \"bootstrap_target_steps\": 1200\n"
                 << "}\n";
    }

    threadspire::ai::app::AppPaths paths;
    paths.root = root;
    const auto state = threadspire::ai::runtime::inspect_internal_model_state(paths);
    require(state.manifest_valid && state.trained && !state.bootstrap_complete,
            "a four-step legacy generation must be detected as incomplete bootstrap state");
    require(!state.bootstrap_policy_current &&
                threadspire::ai::runtime::default_bootstrap_target(state) == 24000U,
            "a stale bootstrap policy must upgrade to the stronger micro target");

    const auto progress = threadspire::ai::runtime::read_internal_model_progress(paths);
    require(progress.available && !progress.stale && progress.step == 6U &&
                progress.bootstrap_policy_current,
            "live progress heartbeat, policy, and exact checkpoint step must be readable");
    require(progress.percent > 0.8 && progress.percent < 0.9,
            "overall bootstrap percent must include prior promoted steps");
    std::filesystem::remove_all(root);
}

void test_storage_monitor_reports_exact_background_footprint() {
    const auto root = temporary_root();
    std::filesystem::create_directories(root / "runtime");
    std::filesystem::create_directories(
        root / "models" / "internal" / "generations" / "g000000");
    std::filesystem::create_directories(
        root / "models" / "internal" / "generations" / "g000001");
    std::filesystem::create_directories(root / "trajectories" / "run-1");
    std::filesystem::create_directories(root / "logs");
    std::filesystem::create_directories(root / "data");
    std::filesystem::create_directories(root / "memory");
    std::ofstream(root / "runtime" / "stable.bin", std::ios::binary)
        << std::string(1024U, 'r');
    std::ofstream(
        root / "models" / "internal" / "generations" / "g000000" / "weights.bin",
        std::ios::binary) << std::string(4096U, 'm');
    const std::string pointer_text{R"({"generation":"g000000"})"};
    std::ofstream(root / "models" / "internal" / "active.json", std::ios::binary)
        << pointer_text;
    std::ofstream(root / "logs" / "app.log", std::ios::binary)
        << std::string(16U, 'l');
    std::ofstream(root / "data" / "bootstrap.jsonl", std::ios::binary)
        << std::string(64U, 'd');
    std::ofstream(root / "memory" / "episodes.jsonl", std::ios::binary)
        << std::string(32U, 'e');

    const auto snapshot = threadspire::ai::runtime::inspect_storage(root, 3U);
    const auto expected_total = 5136U + pointer_text.size() + 64U + 32U;
    require(snapshot.total_bytes == expected_total && snapshot.files == 6U,
            "storage monitor must report exact recursive bytes and files");
    require(snapshot.generation_directories == 2U &&
                snapshot.trajectory_directories == 1U,
            "storage monitor must count model generations and repository trajectories");
    require(snapshot.training_data_bytes == 96U &&
                snapshot.training_data_files == 2U &&
                snapshot.active_generation == "g000000" &&
                snapshot.active_model_bytes == 4096U &&
                snapshot.active_model_files == 1U &&
                snapshot.runtime_bytes == 1024U,
            "storage monitor must surface training data, active model, and runtime sizes separately");
    require(!snapshot.largest_files.empty() &&
                snapshot.largest_files.front().bytes == 4096U,
            "storage monitor must identify the largest application-data files");
    require(!snapshot.categories.empty() &&
                snapshot.categories.front().name == "models",
            "storage categories must be sorted by descending disk usage");
    std::filesystem::remove_all(root);
}

void test_windows_ui_source_contract() {
    const auto source = std::filesystem::path(THREADSPIRE_SOURCE_DIR);
    const auto ui = read_text_file(source / "src/app/main_window.cpp");
    require(ui.find("Button_GetCheck") == std::string::npos &&
                ui.find("Button_SetCheck") == std::string::npos,
            "Windows button state must not depend on Windowsx.h helper macros");
    require(ui.find("BM_GETCHECK") != std::string::npos &&
                ui.find("BM_SETCHECK") != std::string::npos,
            "Windows button state must use native button messages");
    require(ui.find("static_cast<int>(client.right - client.left)") != std::string::npos &&
                ui.find("static_cast<int>(client.bottom - client.top)") != std::string::npos,
            "Win32 LONG layout dimensions must be normalized before integer algorithms");
    require(ui.find("const auto measured_line_height = static_cast<int>(") != std::string::npos &&
                ui.find("metrics.tmHeight + metrics.tmExternalLeading") != std::string::npos &&
                ui.find("line_height = std::max(1, measured_line_height)") != std::string::npos &&
                ui.find("const auto formatting_height = static_cast<int>(") != std::string::npos &&
                ui.find("formatting.bottom - formatting.top") != std::string::npos &&
                ui.find("std::max(1, formatting_height / line_height)") != std::string::npos &&
                ui.find("std::max(1, metrics.tmHeight + metrics.tmExternalLeading)") == std::string::npos &&
                ui.find("std::max(1, (formatting.bottom - formatting.top) / line_height)") == std::string::npos,
            "Win32 text metrics must be normalized from LONG before std::max template deduction");
    require(ui.find("No connected AI is ready.") != std::string::npos &&
                ui.find("Start the model server on the Models page first") == std::string::npos &&
                ui.find("Start the model server before running an autonomous coding task") == std::string::npos,
            "local bridge inference must not be blocked by the custom PyTorch host");
    require(ui.find("L\"Chat\", Page::chat") != std::string::npos &&
                ui.find("L\"AI Setup\", Page::models") != std::string::npos &&
                ui.find("L\"Run Details\", Page::long_horizon") == std::string::npos &&
                ui.find("L\"Storage & Logs\", Page::logs") != std::string::npos &&
                ui.find("L\"Coding Agent\", Page::coding") == std::string::npos,
            "primary navigation must keep chat and creation clear while repository automation remains an execution profile instead of a duplicate console");
    require(ui.find("ThreadSpire — local evolving AI") != std::string::npos,
            "the user-facing application source must use the ThreadSpire product name");
    require(ui.find("L\"Use AIs from\"") != std::string::npos &&
                ui.find("L\"Anywhere — automatic backup (default)\"") != std::string::npos &&
                ui.find("L\"Other AI apps only\"") != std::string::npos &&
                ui.find("L\"ThreadSpire built-in only\"") != std::string::npos &&
                ui.find("L\"Choose AI\"") != std::string::npos &&
                ui.find("update_inference_source") != std::string::npos &&
                ui.find("preferred-source.txt") != std::string::npos &&
                ui.find("id::chat_stop") != std::string::npos &&
                ui.find("id::chat_clear") != std::string::npos,
            "the primary workspace must expose an explicit synchronized runtime source and complete conversation controls");
    require(ui.find("L\"Generate image\"") != std::string::npos &&
                ui.find("L\"Generate speech\"") != std::string::npos &&
                ui.find("L\"Use tools\"") != std::string::npos &&
                ui.find("backend::UnifiedProviderRouter") != std::string::npos &&
                ui.find("backend::ExecutionProfile::coding_agent") != std::string::npos &&
                ui.find("backend::ExecutionProfile::long_horizon") != std::string::npos &&
                ui.find("backend::MediaTaskProvider") != std::string::npos &&
                ui.find("CURRENT MODEL") != std::string::npos &&
                ui.find("ACCEPTED TRAINING DATA") != std::string::npos &&
                ui.find("INSTALLED PACKS") != std::string::npos &&
                ui.find("OUTPUTS / CACHE") != std::string::npos &&
                ui.find("TOTAL USAGE") != std::string::npos &&
                ui.find("FREE DISK") != std::string::npos &&
                ui.find("Legacy/private PyTorch runtime (not training data)") != std::string::npos,
            "the Task surface must route every capability through execution profiles and expose all six live storage cards");
    require(ui.find("recent_conversation_context") != std::string::npos &&
                ui.find("conversation_history_") != std::string::npos &&
                ui.find(".analysis_prompt = std::move(analysis_prompt)") != std::string::npos &&
                ui.find(".context = std::move(execution_context)") != std::string::npos &&
                ui.find("Current user message:") == std::string::npos,
            "the workspace must keep the exact current user message separate from bounded background context and route analysis");
    require(ui.find("L\"What will it learn?\"") != std::string::npos &&
                ui.find("L\"Include earlier chat\"") != std::string::npos &&
                ui.find("response_turns_.push_back") != std::string::npos &&
                ui.find("copy_selected_response") != std::string::npos &&
                ui.find("EARLIER CONVERSATION INCLUDED BY USER") != std::string::npos &&
                ui.find("Other replies, routing details, status text, and error messages: NOT INCLUDED") !=
                    std::string::npos,
            "each completed reply must have exact copy, review, context, and learning-preview controls");
    const auto learning_gate = read_text_file(source / "src/learning/knowledge_classifier.cpp");
    const auto learning_service = read_text_file(source / "src/learning/continual_learning_service.cpp");
    const auto episode_store = read_text_file(source / "src/memory/episode_store.cpp");
    require(learning_gate.find("model errors, corrupt drafts, and private reasoning") !=
                std::string::npos &&
                learning_gate.find("minimum_diverse_human_approvals = 4U") != std::string::npos &&
                learning_service.find("contains_positive_prompt") != std::string::npos &&
                episode_store.find("positive_prompt_keys_") != std::string::npos,
            "per-reply learning must reject error text and prevent repeated prompts from being overweighted");

    require(ui.find("normalize_edit_newlines") != std::string::npos &&
                ui.find("SS_LEFTNOWORDWRAP") != std::string::npos &&
                ui.find("SS_ENDELLIPSIS") != std::string::npos &&
                ui.find("YOU — CODING REQUEST\\r\\n\\r\\n") != std::string::npos &&
                ui.find("ASSISTANT — CONNECTED LOCAL LLM") != std::string::npos &&
                ui.find("Used: built-in AI — ") != std::string::npos,
            "Win32 labels and transcripts must preserve readable one-line labels and CRLF-separated turns");
    require(ui.find("MSFTEDIT_CLASS") != std::string::npos &&
                ui.find("EM_SETBKGNDCOLOR") != std::string::npos &&
                ui.find("CHARFORMAT2W") != std::string::npos &&
                ui.find("style_chat_transcript") != std::string::npos &&
                ui.find("transcript_user_color") != std::string::npos &&
                ui.find("transcript_internal_color") != std::string::npos &&
                ui.find("transcript_connected_color") != std::string::npos &&
                ui.find("transcript_error_color") != std::string::npos &&
                ui.find("LONG rich_position = 0") != std::string::npos &&
                ui.find("rich_position + static_cast<LONG>(line.size())") != std::string::npos &&
                ui.find("static_cast<LONG>(line_start)") == std::string::npos,
            "the conversation must stay dark and visibly distinguish user, internal, connected, and error blocks while streaming or resizing");
    require(ui.find("L\"How it will run\"") != std::string::npos &&
                ui.find("L\"Check choices\"") != std::string::npos &&
                ui.find("L\"AI choices\\r\\n\"") != std::string::npos &&
                ui.find("Approach: ") == std::string::npos &&
                ui.find("L\"Show plan\"") == std::string::npos &&
                ui.find("L\"Complexity: \"") == std::string::npos &&
                ui.find("L\"Output budget: \"") == std::string::npos &&
                ui.find("L\"Risk: \"") == std::string::npos,
            "visible request details must avoid routing jargon, scores, and token budgets");
    require(ui.find("stream_timer") != std::string::npos &&
                ui.find("capture_edit_viewport") != std::string::npos &&
                ui.find("EM_GETFIRSTVISIBLELINE") != std::string::npos &&
                ui.find("WM_SETREDRAW") != std::string::npos &&
                ui.find("PostMessageW(window_, stream_message") == std::string::npos,
            "streaming and live reports must batch repaints and preserve a user's scroll position instead of fighting it");
    require(ui.find("SystemParametersInfoForDpi") != std::string::npos &&
                ui.find("SPI_GETNONCLIENTMETRICS") != std::string::npos &&
                ui.find("Segoe UI Variable Text") == std::string::npos,
            "the GUI must use the DPI-aware Windows message font instead of a hard-coded oversized face");
    require(ui.find("page_host_subclass") != std::string::npos &&
                ui.find("WS_EX_CONTROLPARENT") != std::string::npos &&
                ui.find("SetWindowSubclass failed for a page host") != std::string::npos &&
                ui.find("case WM_COMMAND:") != std::string::npos,
            "tab page hosts must forward child-control notifications to the main window");
    require(ui.find("WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | visibility") ==
                std::string::npos &&
                ui.find("WS_CHILD | WS_CLIPCHILDREN | visibility") != std::string::npos,
            "page hosts must not be clipped completely behind the overlapping tab control");
    require(ui.find("TabCtrl_AdjustRect") != std::string::npos &&
                ui.find("MapWindowPoints") != std::string::npos &&
                ui.find("SWP_SHOWWINDOW") != std::string::npos &&
                ui.find("show_page(page_)") != std::string::npos,
            "tab pages must use the real display rectangle and explicitly restore active-page z-order");
    require(ui.find("has_available_teacher") != std::string::npos &&
                ui.find("if (interactive_model_verified_ &&") == std::string::npos &&
                ui.find("piggyback_operation_.cancel()") != std::string::npos &&
                ui.find("Answering now. Automatic learning is waiting.") != std::string::npos,
            "automatic learning must start from an available teacher and yield to foreground requests");
    require(ui.find("!models::is_internal_model(model)") != std::string::npos &&
                ui.find("settings.policy.lora_microbatch = internal_base ? 8U : 4U") != std::string::npos &&
                ui.find("pipeline_snapshot()") != std::string::npos &&
                ui.find("ACTIVE WEIGHT UPDATE") != std::string::npos &&
                ui.find("The current pass keeps its captured target") != std::string::npos &&
                ui.find("std::chrono::minutes(10)") != std::string::npos,
            "the internal model must never teach itself; evidence must coalesce without package-selection cancellation or rapid retrigger loops");
    require(ui.find("The selected AI is not ready.") != std::string::npos &&
                ui.find("The selected AI could not answer") != std::string::npos,
            "connection and generation failures must be actionable in the visible workspace");

    require(ui.find("const auto workspace_learning_y") != std::string::npos &&
                ui.find("const auto learning_y = workspace_content_top") == std::string::npos &&
                ui.find("auto learning_y = 0;") != std::string::npos,
            "workspace and Learning-tab layout coordinates must use distinct local names");

    const auto registry_source = read_text_file(source / "src/models/model_registry.cpp");
    require(registry_source.find("ThreadSpire Internal — C++23 coding") != std::string::npos &&
                ui.find("model.display_name.starts_with(\"Custom AI\")") != std::string::npos,
            "internal runtime labels must identify ThreadSpire internal models consistently, including existing user state");

    const auto internal_model = read_text_file(source / "resources/internal_model.py");
    const auto worker = read_text_file(source / "resources/model_worker.py");
    const auto default_models = read_text_file(source / "resources/models.ini");
    require(internal_model.find("class ThreadSpireModel") != std::string::npos &&
                internal_model.find("random-initialized-local-model") != std::string::npos &&
                internal_model.find("external_base_weights\": False") != std::string::npos &&
                internal_model.find("class ByteTokenizer") != std::string::npos &&
                internal_model.find("def promote_generation") != std::string::npos &&
                internal_model.find("def activate_generation") != std::string::npos &&
                internal_model.find("\"gradients\": gradients") != std::string::npos &&
                internal_model.find("\"accumulation_steps\": accumulation_steps") != std::string::npos &&
                internal_model.find("def blocked_no_repeat_tokens") != std::string::npos &&
                internal_model.find("def assess_generation_quality") != std::string::npos &&
                worker.find("internal.load_active_model") != std::string::npos &&
                worker.find("threadspire://") != std::string::npos &&
                worker.find("promote-internal") != std::string::npos &&
                worker.find("activate-internal") != std::string::npos &&
                worker.find("training.lock") != std::string::npos &&
                worker.find("text/event-stream; charset=utf-8") != std::string::npos &&
                worker.find("data: [DONE]") != std::string::npos &&
                worker.find("low_confidence") != std::string::npos &&
                default_models.find("Qwen/") == std::string::npos &&
                default_models.find("model=threadspire://coder") != std::string::npos,
            "the private runtime must own a random-initialized model, stream it directly, and promote or roll back generations atomically without external base weights");
    require(ui.find("Rebuild from scratch") != std::string::npos &&
                ui.find("Continue setup") != std::string::npos &&
                ui.find("uses only local model files") != std::string::npos,
            "plain-language backup controls must retain model repair and local provenance");

    const auto install_begin = ui.find("void MainWindow::install_backend(const bool automatic)");
    const auto install_end = ui.find("void MainWindow::verify_backend", install_begin);
    require(install_begin != std::string::npos && install_end > install_begin,
            "the optional built-in AI file transaction must remain inspectable");
    const auto install_body = ui.substr(install_begin, install_end - install_begin);
    require(ui.find("runtime::CapabilityPackManager") != std::string::npos &&
                ui.find("install_transactional") != std::string::npos &&
                ui.find("automatic_training_pack_source") != std::string::npos &&
                install_body.find("create_internal_model") == std::string::npos &&
                install_body.find("start_backend") == std::string::npos &&
                install_body.find("inference_source_") == std::string::npos,
            "automatic optional-file installation must stay transactional and isolated from training, startup, and model selection");
    require(ui.find("Backup and repair options") != std::string::npos &&
                ui.find("backup_model_controls") != std::string::npos &&
                ui.find("show_backup_controls ? SW_SHOW : SW_HIDE") != std::string::npos &&
                ui.find("id::models_manual_endpoint") != std::string::npos &&
                ui.find("L\"BUTTON\", L\"Install pack\"") == std::string::npos &&
                ui.find("L\"BUTTON\", L\"Verify\"") == std::string::npos &&
                ui.find("L\"BUTTON\", L\"Create + bootstrap train\"") == std::string::npos,
            "manual connection and repair fields must stay hidden as backup-only controls with plain labels");
    require(ui.find("automatic_lifecycle_enabled_ = lifecycle_setting != \"off\"") != std::string::npos &&
                ui.find("automatic_training_enabled_ = training_setting != \"off\"") != std::string::npos &&
                ui.find("case id::models_automation_toggle") != std::string::npos &&
                ui.find("install_backend(true)") != std::string::npos &&
                ui.find("verify_backend(true)") != std::string::npos &&
                ui.find("create_internal_model(true, true)") != std::string::npos &&
                ui.find("start_backend(true)") != std::string::npos &&
                ui.find("page == page_ ? WS_VISIBLE : 0U") != std::string::npos,
            "setup, checking, learning, recovery, and startup must default to one idle-safe automatic coordinator without forcing tab navigation");
    require(ui.find("Automatic setup starts with the smallest profile and its 24k quality target") != std::string::npos &&
                ui.find("AI: best available") != std::string::npos &&
                ui.find("ThreadSpire built-in: ") != std::string::npos &&
                ui.find("NOT READY — learning automatically") != std::string::npos &&
                ui.find("built_in_status = L\"READY\"") != std::string::npos &&
                ui.find("const auto progress_active = progress.available") != std::string::npos &&
                ui.find("progress_active && completed == 0U") != std::string::npos &&
                ui.find("stream << L\"starting\"") != std::string::npos &&
                ui.find("progress_has_saved_work") != std::string::npos &&
                ui.find("Built-in AI failed quality checks at ") != std::string::npos &&
                ui.find("Improving the built-in AI automatically from approved examples") != std::string::npos &&
                ui.find("Open Connections, verify the PyTorch runtime") == std::string::npos,
            "Chat and AI Setup must show the saved live or terminal built-in AI counter while new setups use the minimum 24k profile");
    require(ui.find("internal_host_startable") != std::string::npos &&
                ui.find("A stale internal-only preference must never hide a working connected model") == std::string::npos &&
                ui.find("AI group: ThreadSpire built-in only") != std::string::npos &&
                ui.find("set_status(runtime_summary(snapshot, backend_server_.running()))") != std::string::npos &&
                ui.find("NOT INSTALLED — required files are missing") != std::string::npos &&
                ui.find("No setup is running because the built-in AI package is not installed") != std::string::npos &&
                ui.find("waiting for optional setup files") == std::string::npos &&
                ui.find("show_progress ? SW_SHOW : SW_HIDE") != std::string::npos &&
                ui.find("automatic_learning_limit_reached") != std::string::npos &&
                ui.find("no unapproved model was activated") != std::string::npos,
            "an unavailable manually selected built-in AI must remain selected while automatic setup continues honestly");
    require(ui.find("internal_pool_selection_id") != std::string::npos &&
                ui.find("ThreadSpire built-in AIs only") != std::string::npos &&
                ui.find("apply_model_choice_filter(snapshot)") != std::string::npos &&
                ui.find("model.enabled = model.enabled && models::is_internal_model(model)") != std::string::npos &&
                ui.find("model.enabled = model.enabled && model.id == *selected") != std::string::npos &&
                ui.find("const auto allow_automatic_fallback = !selected_model_id().has_value()") != std::string::npos &&
                ui.find(".allow_automatic_fallback = allow_automatic_fallback") != std::string::npos &&
                ui.find("The selected AI is not responding. No other AI was used.") != std::string::npos &&
                ui.find("safe backup") == std::string::npos,
            "a manual source or exact AI choice must disable automatic switching while Automatic mode retains it");
    require(ui.find("DarkMode_Explorer") != std::string::npos &&
                ui.find("DwmSetWindowAttribute") != std::string::npos &&
                ui.find("RGB(22, 24, 29)") != std::string::npos,
            "the native Windows UI must default to a real dark surface and dark title bar");
    require(ui.find("English foundation — recommended first") != std::string::npos &&
                ui.find("training-target.txt") != std::string::npos &&
                ui.find("Automatic mode never changes the selected target") != std::string::npos &&
                ui.find("Add verified file") != std::string::npos &&
                ui.find("choose_training_dataset") != std::string::npos,
            "the training interface must default to English, persist target selection, and expose understandable custom JSONL import");
    require(ui.find("Training Library —") != std::string::npos &&
                ui.find("append_training_library_file") != std::string::npos &&
                ui.find("Approved result") != std::string::npos &&
                ui.find("Rejected results") != std::string::npos &&
                ui.find("Weight training: eligible") != std::string::npos &&
                ui.find("Held-out check") != std::string::npos &&
                ui.find("Held out from training; used only to check the candidate") != std::string::npos &&
                ui.find("No prompt or answer below is shortened") != std::string::npos &&
                ui.find("id::learning_library_filter") != std::string::npos &&
                ui.find("width >= scaled(980, dpi_)") != std::string::npos,
            "Learning must expose every full trainable or held-out record in a searchable, responsive, plain-language library");
    require(ui.find("TCS_OWNERDRAWFIXED") != std::string::npos &&
                ui.find("TCS_FIXEDWIDTH") != std::string::npos &&
                ui.find("CBS_OWNERDRAWFIXED") != std::string::npos &&
                ui.find("BS_OWNERDRAW") != std::string::npos &&
                ui.find("SS_OWNERDRAW") != std::string::npos &&
                ui.find("item.CtlType == ODT_STATIC") != std::string::npos &&
                ui.find("PBM_SETBKCOLOR") != std::string::npos &&
                ui.find("draw_dark_control_item") != std::string::npos,
            "tabs, selectors, push buttons, storage cards, and progress surfaces must use one coherent dark palette rather than white native islands");
    require(ui.find("SWP_NOCOPYBITS") != std::string::npos &&
                ui.find("case WM_EXITSIZEMOVE") != std::string::npos &&
                ui.find("RDW_ALLCHILDREN | RDW_UPDATENOW") != std::string::npos,
            "live resizing must repaint the complete dark child tree without stretching stale control pixels");

    const auto launcher = read_text_file(source / "BUILD_AND_RUN.cmd");
    require(launcher.find("call build.cmd") != std::string::npos &&
                launcher.find("ctest") == std::string::npos &&
                launcher.find(R"(start "ThreadSpire")") != std::string::npos,
            "the one-command launcher must reuse the validated build pipeline and start only its successful output");

    const auto orchestrator = read_text_file(source / "src/backend/agent_orchestrator.cpp");
    require(orchestrator.find("Interactive minimum-compute route") != std::string::npos &&
                orchestrator.find("automatic-fallback") != std::string::npos &&
                orchestrator.find("active_options_.allow_automatic_fallback") != std::string::npos &&
                orchestrator.find("No other AI was used") != std::string::npos &&
                orchestrator.find("model.model_name != \"threadspire://fast\"") != std::string::npos,
            "interactive routing may fail over only when the request explicitly allows automatic switching");
    require(orchestrator.find("memory_context(prompt, active_analysis_)") != std::string::npos &&
                orchestrator.find("Background context follows") != std::string::npos &&
                orchestrator.find("const auto augmented = prompt") != std::string::npos &&
                orchestrator.find("Never expose private reasoning") != std::string::npos,
            "conversation context must remain out of the current user turn and providers must forbid private reasoning output");
    require(orchestrator.find("memory_.verified_match(prompt, skill_for(active_analysis_))") != std::string::npos &&
                orchestrator.find("generation_temperature(active_analysis_)") != std::string::npos &&
                orchestrator.find("return 0.05") != std::string::npos &&
                orchestrator.find("0.48") != std::string::npos &&
                orchestrator.find("do not repeat the wording mechanically") != std::string::npos &&
                orchestrator.find("verified-memory") != std::string::npos &&
                orchestrator.find("completion.hidden_reasoning_only") != std::string::npos &&
                orchestrator.find("stage.max_output_tokens * 4U") != std::string::npos,
            "compact inference must use exact verified memory first and retry reasoning-only backups with a bounded budget");
    require(orchestrator.find("const auto task_needs_tools") != std::string::npos &&
                orchestrator.find("const auto use_tools = task_needs_tools") != std::string::npos,
            "connected MCP tools must not disable streaming or break ordinary conversation prompts");

    const auto providers = read_text_file(source / "src/models/local_provider.cpp");
    require(providers.find("/api/v1/models") != std::string::npos &&
                providers.find("loaded_instances") != std::string::npos &&
                providers.find("no loaded LLM is ready") != std::string::npos,
            "LM Studio discovery must select loaded LLM instances instead of every downloaded model");

    const auto piggyback = read_text_file(source / "src/learning/piggyback_foundry.cpp");
    require(piggyback.find("trusted-seed-bootstrap") != std::string::npos &&
                piggyback.find("Bundled hand-reviewed reference example") != std::string::npos &&
                piggyback.find("request.seed_offset") != std::string::npos &&
                piggyback.find("request.target_skill") != std::string::npos &&
                piggyback.find("request.restrict_to_target_domain") != std::string::npos &&
                piggyback.find("document->find(\"prompt\")") != std::string::npos &&
                piggyback.find("document->find(\"answer\")") != std::string::npos &&
                piggyback.find("generate_verified_variations") != std::string::npos &&
                piggyback.find("no unverified rewrites were created") != std::string::npos,
            "automatic and custom learning must target one selected skill, rotate independently, and accept practical JSONL record shapes");

    const auto custom_template = read_text_file(
        source / "resources/custom_training_template.jsonl");
    require(custom_template.find("\"skill\":\"custom-skill\"") != std::string::npos &&
                custom_template.find("cpp23-core") == std::string::npos &&
                custom_template.find("one complete JSON object per line") != std::string::npos,
            "the embedded custom-training template must demonstrate one coherent custom target instead of mixing English and C++ skills");
}

void test_worker_and_resource_contract() {
    const auto source = std::filesystem::path(THREADSPIRE_SOURCE_DIR);
    const auto worker = read_text_file(source / "resources/model_worker.py");
    require(worker.find("optional_version(\"transformers\")") != std::string::npos &&
                worker.find("optional_version(\"peft\")") != std::string::npos &&
                worker.find("\"internal_training_ready\": True") != std::string::npos &&
                worker.find("    import transformers") == std::string::npos &&
                worker.find("    import peft") == std::string::npos,
            "the internal PyTorch doctor must not require the separate optional Transformers/PEFT pack");
    require(worker.find("train-lora") != std::string::npos,
            "worker must implement low-rank incubation");
    require(worker.find("threadspire.external-skill-delta.v1") != std::string::npos,
            "worker must implement full-rank modular FFN packs");
    require(worker.find("lora-incubation-plus-full-rank-ffn-refinement") != std::string::npos &&
                worker.find("parameters[name].requires_grad_(True)") != std::string::npos &&
                worker.find("full-rank-checkpoint") != std::string::npos,
            "consolidation must directly optimize bounded FFN matrices and checkpoint them, not merely relabel a merged LoRA");
    require(worker.find("weights_sha256") != std::string::npos &&
                worker.find("module contains a non-FFN tensor") != std::string::npos,
            "durable skill modules must enforce checksums and an FFN-only ABI");
    require(worker.find("inference.priority") != std::string::npos &&
                worker.find("inference.server") != std::string::npos,
            "worker and model server must coordinate inference-priority leases");
    require(worker.find("training.active") != std::string::npos &&
                worker.find("/release-cache") != std::string::npos &&
                worker.find("PREEMPTED_EXIT_CODE = 75") != std::string::npos,
            "foreground inference must evict cached models and preempt background training safely");
    require(worker.find("get_last_checkpoint") != std::string::npos &&
                worker.find("resume_from_checkpoint") != std::string::npos,
            "interrupted training must resume from persisted checkpoints");
    require(worker.find("--seed-dataset") != std::string::npos &&
                worker.find("eligible_for_training") != std::string::npos,
            "worker must support seed bootstrapping while filtering live training evidence");
    require(worker.find("--foreground-bootstrap") != std::string::npos &&
                worker.find("--progress-file") != std::string::npos &&
                worker.find("--slot-timeout-seconds") != std::string::npos &&
                worker.find("_foreground_training_cleanup") != std::string::npos,
            "foreground bootstrap must clear stale blockers, publish heartbeats, and time out instead of waiting forever");
    require(worker.find("--generation") != std::string::npos &&
                worker.find("--control-dir") != std::string::npos &&
                worker.find("shadow_candidate") != std::string::npos &&
                worker.find("load_generation_model") != std::string::npos,
            "candidate verification must serve an exact unpromoted generation in an isolated shadow host");
    const auto internal_model = read_text_file(source / "resources/internal_model.py");
    const auto curriculum_guard = read_text_file(source / "resources/curriculum_guard.py");
    const auto lifecycle_ui = read_text_file(source / "src/app/main_window.cpp");
    require(internal_model.find("threadspire.internal-progress.v1") != std::string::npos &&
                internal_model.find("bootstrap_complete") != std::string::npos &&
                internal_model.find("full_backbone_trained") != std::string::npos &&
                internal_model.find("evaluate_generation_readiness") != std::string::npos &&
                internal_model.find("def verified_local_response") != std::string::npos &&
                internal_model.find("optimizer_scope = \"all\"") != std::string::npos &&
                internal_model.find("honor_inference_priority") != std::string::npos &&
                internal_model.find("bounded_bootstrap_learning_rate") != std::string::npos &&
                internal_model.find("return min(scheduled, previous_ceiling)") != std::string::npos &&
                internal_model.find("bootstrap-host-already-ready") != std::string::npos &&
                internal_model.find("if maximum_steps > 0 and checkpoint_path.is_file():") != std::string::npos,
            "internal training must use the full backbone, never raise the rate on an extended repair target, pass generation canaries, persist completion metadata, and report exact resumable progress");
    require(internal_model.find("filtered_logits[:, SEPARATOR_ID] = logits[:, SEPARATOR_ID]") != std::string::npos &&
                internal_model.find("token_id in {EOS_ID, SEPARATOR_ID}") != std::string::npos,
            "generation must honor the supervised end-of-answer separator instead of forcing correct answers to continue as garbage");
    require(lifecycle_ui.find("automatic_bootstrap_targets{24000U, 36000U, 50000U}") != std::string::npos &&
                lifecycle_ui.find("adaptive_bootstrap_target") != std::string::npos &&
                lifecycle_ui.find("saved_learning_steps(progress) >= automatic_bootstrap_targets.back()") != std::string::npos &&
                lifecycle_ui.find("training copy is not used") != std::string::npos &&
                lifecycle_ui.find("this training copy cannot answer until every check passes") != std::string::npos,
            "automatic bootstrap must advance through 24k, 36k, and 50k stages, visibly quarantine unproven work, and stop boundedly at the final cap");
    require(internal_model.find("resolve_bootstrap_repair_generation") != std::string::npos &&
                internal_model.find("resumed_from_repair_generation") != std::string::npos &&
                internal_model.find("_next_generation(state_root, parent)") != std::string::npos &&
                worker.find("A rejected shadow candidate is a recoverable training outcome") != std::string::npos &&
                lifecycle_ui.find("an explicit repair retry must continue from the saved candidate") != std::string::npos &&
                lifecycle_ui.find("create_internal_model(true, true)") != std::string::npos &&
                lifecycle_ui.find("bootstrap_priority_lease_") != std::string::npos &&
                lifecycle_ui.find("interactive_lease_count() > reserved_bootstrap_lease") != std::string::npos &&
                lifecycle_ui.find("yield_automatic_bootstrap_to_foreground") != std::string::npos &&
                lifecycle_ui.find("Saving built-in AI learning progress and yielding") != std::string::npos &&
                lifecycle_ui.find("backend_operation_.cancel()") != std::string::npos,
            "automatic and manual bootstrap repair must resume the strongest rejected lineage, yield saved training immediately to foreground work, and never start an unready host");
    require(worker.find("curriculum.enforce_namespace(parser.parse_args())") != std::string::npos &&
                curriculum_guard.find("argparse.ArgumentParser.parse_args=") == std::string::npos &&
                curriculum_guard.find("return path if isinstance(original, Path) else str(path)") != std::string::npos &&
                curriculum_guard.find("namespace.episodes = filter_dataset") != std::string::npos &&
                internal_model.find("canonical_training_scope") != std::string::npos &&
                internal_model.find("path_values = [Path(path_value) for path_value in paths]") != std::string::npos &&
                internal_model.find("_threadspire_curriculum_guard.english_record(record)") != std::string::npos &&
                internal_model.find("curriculum-state.json") != std::string::npos,
            "English-first training must preserve Path values, avoid global argparse mutation, filter every evidence path, reject code-shaped records at the loader, map the UI target to english-core, and persist the gate state");

    const auto continual = read_text_file(
        source / "src/learning/continual_learning_service.cpp");
    require(continual.find("--defer-promotion") != std::string::npos &&
                continual.find("--progress-file") != std::string::npos &&
                continual.find("pipeline_snapshot") != std::string::npos &&
                continual.find("promote-internal") != std::string::npos &&
                continual.find("activate-internal") != std::string::npos &&
                continual.find("internal_generation_path") != std::string::npos &&
                continual.find("verify_internal_candidate_host") != std::string::npos &&
                continual.find("AtomicPromotionCoordinator") != std::string::npos &&
                continual.find("CandidatePromotionCommitResult") != std::string::npos &&
                continual.find("recover_persisted_requests") != std::string::npos &&
                continual.find("transition_durable_phase") != std::string::npos &&
                continual.find("promotion_mutex_") != std::string::npos &&
                continual.find("std::erase_if(queue_") != std::string::npos &&
                continual.find("training_episode_count") != std::string::npos &&
                continual.find("status.candidate_id = candidate.id") != std::string::npos &&
                continual.find("status.id = candidate.id") == std::string::npos &&
                continual.find("status.id = candidate->id") == std::string::npos,
            "internal learning must keep a stable serialized job ID, coalesce resumable shadow generations, preserve new evidence, compare-and-swap promotion, and support atomic rollback");

    const auto completion = read_text_file(
        source / "src/backend/completion_client.cpp");
    const auto streaming_heartbeat = worker.find("heartbeat,");
    const auto streaming_dataset = worker.find(
        "trusted_dataset=state.state_root / \"data\" / \"bootstrap.jsonl\"",
        streaming_heartbeat);
    require(completion.find("result.finish_reason == \"low_confidence\"") != std::string::npos &&
                completion.find("repetitive or structurally corrupt") != std::string::npos &&
                completion.find("hidden_reasoning_only") != std::string::npos &&
                completion.find("result.content = core::json::string_or(message->find(\"reasoning_content\"))") == std::string::npos &&
                completion.find("result.content += core::json::string_or(message->find(\"thinking\"))") == std::string::npos &&
                completion.find("private reasoning and returned no final answer") != std::string::npos &&
                worker.find("verified_response=verified_content is not None") != std::string::npos &&
                worker.find("trusted_dataset=trusted_dataset") != std::string::npos &&
                streaming_heartbeat != std::string::npos &&
                streaming_dataset != std::string::npos &&
                streaming_dataset - streaming_heartbeat < 256U &&
                internal_model.find("verified-hybrid-conversation-v6") != std::string::npos &&
                internal_model.find("trusted_dataset=trusted_dataset") != std::string::npos &&
                internal_model.find("vague-question-repair") != std::string::npos &&
                internal_model.find("grounded-summary") != std::string::npos &&
                internal_model.find("safe-replacement-reasoning") != std::string::npos &&
                internal_model.find("passed_count == len(canaries)") != std::string::npos &&
                worker.find("reference_verified") != std::string::npos &&
                worker.find("_MINIMUM_DIVERSE_HUMAN_APPROVALS = 4") != std::string::npos &&
                internal_model.find("_human_approval_counts") != std::string::npos,
            "nonsense and terse internal drafts must be withheld from the user, trigger route fallback, and only an independently probed generation may become ready");

    const auto resources = read_text_file(source / "resources/ThreadSpire.rc.in");
    const auto cmake = read_text_file(source / "CMakeLists.txt");
    const auto resource_ids = read_text_file(source / "resources/resource.h");
    const auto app_paths_header = read_text_file(
        source / "include/threadspire/ai/app/app_paths.hpp");
    const auto app_paths_source = read_text_file(source / "src/app/app_paths.cpp");
    const auto backend_manager = read_text_file(
        source / "src/runtime/backend_manager.cpp");
    require(resources.find("RT_MANIFEST") == std::string::npos,
            "manifest must be supplied exactly once by CMake");
    require(cmake.find("THREADSPIRE_MODEL_WORKER") == std::string::npos &&
                cmake.find("THREADSPIRE_INTERNAL_MODEL") == std::string::npos &&
                cmake.find("THREADSPIRE_CURRICULUM_GUARD") == std::string::npos &&
                resources.find("IDR_MODEL_WORKER") == std::string::npos &&
                resources.find("IDR_INTERNAL_MODEL") == std::string::npos &&
                resources.find("IDR_CURRICULUM_GUARD") == std::string::npos &&
                resource_ids.find("IDR_MODEL_WORKER") == std::string::npos &&
                resource_ids.find("IDR_INTERNAL_MODEL") == std::string::npos &&
                resource_ids.find("IDR_CURRICULUM_GUARD") == std::string::npos,
            "the base executable must not embed Python training programs or resource IDs");
    require(app_paths_header.find("std::filesystem::path packs") != std::string::npos &&
                app_paths_header.find("curriculum_guard_module") != std::string::npos &&
                app_paths_source.find("paths.packs / \"internal-pytorch-training\"") != std::string::npos &&
                app_paths_source.find("training_pack / \"backend\" / \"curriculum_guard.py\"") != std::string::npos &&
                app_paths_source.find("write_resource(IDR_CURRICULUM_GUARD") == std::string::npos &&
                backend_manager.find("paths.packs / \"internal-pytorch-training\" / \"pack.json\"") != std::string::npos &&
                backend_manager.find("is_regular_file(paths.curriculum_guard_module") != std::string::npos,
            "heavy training files must exist only inside an explicitly installed, verified capability pack");
    require(backend_manager.find("setuptools") == std::string::npos &&
                backend_manager.find("pip") == std::string::npos &&
                backend_manager.find("Automatic Python/PyTorch installation was removed") != std::string::npos,
            "the base backend manager must never create a private runtime or install PyTorch automatically");
}

void test_seed_material_counts() {
    const auto source = std::filesystem::path(THREADSPIRE_SOURCE_DIR);
    std::ifstream seed(source / "resources/seed.jsonl");
    std::ifstream anchors(source / "resources/anchors.jsonl");
    std::ifstream bootstrap(source / "resources/bootstrap.jsonl");
    std::ifstream bootstrap_anchors(source / "resources/bootstrap_anchors.jsonl");
    std::size_t seed_count = 0;
    std::size_t anchor_count = 0;
    std::size_t bootstrap_count = 0;
    std::size_t bootstrap_anchor_count = 0;
    std::string line;
    while (std::getline(seed, line)) {
        seed_count += !line.empty() ? 1U : 0U;
    }
    while (std::getline(anchors, line)) {
        anchor_count += !line.empty() ? 1U : 0U;
    }
    while (std::getline(bootstrap, line)) {
        bootstrap_count += !line.empty() ? 1U : 0U;
    }
    while (std::getline(bootstrap_anchors, line)) {
        bootstrap_anchor_count += !line.empty() ? 1U : 0U;
    }
    require(seed_count >= 190, "handcrafted seed corpus should contain at least 190 examples");
    require(anchor_count >= 10, "legacy regression anchors should be present");
    require(bootstrap_count >= 3'000, "internal model needs a substantial unique bootstrap curriculum");
    require(bootstrap_anchor_count >= 300, "internal model needs broad held-out anchors");
}

} // namespace

int main() {
    try {
        test_task_analyzer_fast_path();
        test_task_analyzer_long_horizon();
        test_router_uses_fast_model_for_simple_work();
        test_router_builds_agentic_code_loop();
        test_model_registry_round_trip();
        test_immediate_memory_retrieval();
        test_manual_model_choice_disables_fallback();
        test_expert_lifecycle_thresholds();
        test_learning_service_stores_before_training();
        test_interactive_priority_uses_nested_leases();
        test_episode_dedup_and_negative_evidence();
        test_learning_preview_blocks_errors_and_repetition();
        test_episode_index_recovers_after_restart();
        test_knowledge_classifier_routes_volatile_state_to_retrieval();
        test_resource_governor_prioritizes_inference();
        test_candidate_registry_marks_inflight_work_resumable();
        test_expert_registry_persists_generation_and_rollback();
        test_model_feedback_changes_routing_quality();
        test_process_log_append();
        test_json_and_stream_protocols();
        test_mcp_tool_schema_conversion();
        test_manual_local_provider_and_role_router();
        test_discovery_reconciliation_preserves_runtime_telemetry();
        test_reference_verified_evidence_can_incubate();
        test_targeted_and_custom_training_import();
        test_internal_model_lifecycle_state_and_progress();
        test_storage_monitor_reports_exact_background_footprint();
        test_windows_ui_source_contract();
        test_worker_and_resource_contract();
        test_seed_material_counts();
        std::cout << "All ThreadSpire core tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return 1;
    }
}
