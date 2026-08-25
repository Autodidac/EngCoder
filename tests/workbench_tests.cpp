#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

import threadspire.learning;
import threadspire.platform;
import threadspire.providers;
import threadspire.scheduler;
import threadspire.storage;

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::filesystem::path temporary_root() {
    static std::atomic_uint64_t serial{1U};
    const auto root = std::filesystem::temp_directory_path() /
        ("threadspire-workbench-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(serial.fetch_add(1U, std::memory_order_relaxed)));
    std::filesystem::create_directories(root);
    return root;
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
    require(static_cast<bool>(stream), "failed to write " + path.string());
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "failed to read " + path.string());
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
}

class RecordingProvider final : public threadspire::ai::backend::TaskProvider {
public:
    RecordingProvider(
        std::string provider_id,
        threadspire::ai::backend::ProviderCapabilities capabilities,
        const threadspire::ai::backend::ArtifactKind artifact_kind =
            threadspire::ai::backend::ArtifactKind::file,
        const bool wait_for_cancel = false)
        : id_(std::move(provider_id)),
          capabilities_(capabilities),
          artifact_kind_(artifact_kind),
          wait_for_cancel_(wait_for_cancel) {
    }

    [[nodiscard]] std::string_view id() const noexcept override {
        return id_;
    }

    [[nodiscard]] threadspire::ai::backend::ProviderCapabilities capabilities() const noexcept override {
        return capabilities_;
    }

    [[nodiscard]] threadspire::ai::backend::UnifiedTaskResult execute(
        const threadspire::ai::backend::UnifiedTaskRequest& request,
        const std::stop_token stop_token) override {
        started_.store(true, std::memory_order_release);
        while (wait_for_cancel_ && !stop_token.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (stop_token.stop_requested()) {
            return {
                .cancelled = true,
                .task_id = request.task_id,
                .provider_id = id_,
                .error = "cancelled by test",
            };
        }
        threadspire::ai::backend::UnifiedTaskResult result{
            .succeeded = true,
            .task_id = request.task_id,
            .provider_id = id_,
            .content = request.prompt,
        };
        if (request.modality == threadspire::ai::backend::WorkModality::image_generation ||
            request.modality == threadspire::ai::backend::WorkModality::audio_generation ||
            request.modality == threadspire::ai::backend::WorkModality::repository_work) {
            result.artifacts.push_back({
                .kind = artifact_kind_,
                .path = request.output_root / "result.bin",
                .mime_type = artifact_kind_ == threadspire::ai::backend::ArtifactKind::image
                    ? "image/png"
                    : artifact_kind_ == threadspire::ai::backend::ArtifactKind::audio
                        ? "audio/wav"
                        : "text/x-diff",
                .label = "result",
            });
        }
        if (request.modality == threadspire::ai::backend::WorkModality::tool_action) {
            result.tool_events.push_back("tool:ok");
        }
        if (request.on_event) {
            static_cast<void>(request.on_event({
                .kind = threadspire::ai::backend::TaskEventKind::text_delta,
                .text = "done",
            }));
        }
        return result;
    }

    [[nodiscard]] bool started() const noexcept {
        return started_.load(std::memory_order_acquire);
    }

private:
    std::string id_;
    threadspire::ai::backend::ProviderCapabilities capabilities_;
    threadspire::ai::backend::ArtifactKind artifact_kind_;
    bool wait_for_cancel_{false};
    std::atomic_bool started_{false};
};

void test_unified_routing_profiles_multimodal_and_artifacts() {
    using namespace threadspire::ai::backend;
    UnifiedProviderRouter router;
    router.register_provider(std::make_shared<RecordingProvider>(
        "text-and-tools",
        ProviderCapabilities{
            .interactive_text = true,
            .coding = true,
            .tools = true,
            .streaming = true,
        }));
    router.register_provider(std::make_shared<RecordingProvider>(
        "image",
        ProviderCapabilities{
            .image_generation = true,
            .file_artifacts = true,
        },
        ArtifactKind::image));
    router.register_provider(std::make_shared<RecordingProvider>(
        "audio",
        ProviderCapabilities{
            .audio_generation = true,
            .file_artifacts = true,
        },
        ArtifactKind::audio));
    router.register_provider(std::make_shared<RecordingProvider>(
        "repository",
        ProviderCapabilities{
            .coding = true,
            .repository_work = true,
            .tools = true,
            .streaming = true,
            .file_artifacts = true,
        },
        ArtifactKind::patch));

    std::size_t streamed = 0U;
    auto text = router.execute({
        .modality = WorkModality::interactive_text,
        .profile = ExecutionProfile::interactive,
        .prompt = "hello",
        .on_event = [&streamed](const TaskEvent&) {
            ++streamed;
            return true;
        },
    });
    require(text.succeeded && text.provider_id == "text-and-tools" && streamed == 1U,
            "interactive text must stream through the unified provider contract");

    auto coding = router.execute({
        .modality = WorkModality::interactive_text,
        .profile = ExecutionProfile::coding_agent,
        .prompt = "write code",
    });
    require(coding.succeeded && coding.provider_id == "text-and-tools",
            "Coding Agent must be an execution profile on the unified contract");

    auto repository = router.execute({
        .modality = WorkModality::repository_work,
        .profile = ExecutionProfile::long_horizon,
        .prompt = "repair repository",
        .output_root = "outputs",
    });
    require(repository.succeeded && repository.provider_id == "repository" &&
                repository.artifacts.size() == 1U &&
                repository.artifacts.front().kind == ArtifactKind::patch,
            "Long Horizon must be a profile that returns file artifacts");

    auto image = router.execute({
        .modality = WorkModality::image_generation,
        .profile = ExecutionProfile::interactive,
        .prompt = "make an image",
        .output_root = "outputs",
    });
    auto audio = router.execute({
        .modality = WorkModality::audio_generation,
        .profile = ExecutionProfile::interactive,
        .prompt = "make audio",
        .output_root = "outputs",
    });
    require(image.succeeded && image.artifacts.front().kind == ArtifactKind::image &&
                audio.succeeded && audio.artifacts.front().kind == ArtifactKind::audio,
            "image and audio providers must use the same task/result/artifact interface");

    auto tools = router.execute({
        .modality = WorkModality::tool_action,
        .profile = ExecutionProfile::coding_agent,
        .prompt = "run tool",
    });
    require(tools.succeeded && tools.tool_events == std::vector<std::string>{"tool:ok"},
            "tool execution must share the unified result channel");
}

void test_unified_cancellation() {
    using namespace threadspire::ai::backend;
    UnifiedProviderRouter router;
    auto provider = std::make_shared<RecordingProvider>(
        "cancellable",
        ProviderCapabilities{.interactive_text = true, .cancellation = true},
        ArtifactKind::file,
        true);
    router.register_provider(provider);
    auto future = std::async(std::launch::async, [&router] {
        return router.execute({
            .task_id = "cancel-me",
            .modality = WorkModality::interactive_text,
            .profile = ExecutionProfile::interactive,
            .prompt = "wait",
        });
    });
    for (std::size_t attempt = 0U; attempt < 500U && !provider->started(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(provider->started() && router.cancel("cancel-me"),
            "active unified tasks must be cancellable by task id");
    const auto result = future.get();
    require(result.cancelled && !result.succeeded && router.active_tasks() == 0U,
            "cancellation must reach the provider and clear the active-task registry");
}

void test_pack_transaction_rollback_and_selection_isolation() {
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    const auto source_one = root / "source-one";
    const auto source_two = root / "source-two";
    write_text(source_one / "pack.ready", "version-one");
    write_text(source_two / "pack.ready", "version-two");
    const auto selected_model = root / "selected-model.txt";
    write_text(selected_model, "working-model-g42");

    CapabilityPackManager manager(root / "packs");
    CapabilityPackDescriptor descriptor{
        .id = "test-pack",
        .name = "Test Pack",
        .version = "1",
        .kind = CapabilityPackKind::image_engine,
        .required_files = {"pack.ready"},
    };
    const auto installed = manager.install_transactional({
        .descriptor = descriptor,
        .source_directory = source_one,
    });
    require(installed.succeeded && manager.installed("test-pack") &&
                read_text(root / "packs" / "test-pack" / "pack.ready") == "version-one",
            "a verified capability pack must commit transactionally");
    require(read_text(selected_model) == "working-model-g42",
            "pack installation must never change the selected model");

    descriptor.version = "2";
    const auto rolled_back = manager.install_transactional({
        .descriptor = descriptor,
        .source_directory = source_two,
        .continue_stage = [](const std::string_view stage) {
            return stage != "commit";
        },
    });
    require(!rolled_back.succeeded && rolled_back.cancelled &&
                rolled_back.previous_install_preserved &&
                read_text(root / "packs" / "test-pack" / "pack.ready") == "version-one",
            "cancelling during commit must roll back to the prior working pack");
    require(read_text(selected_model) == "working-model-g42",
            "failed or cancelled pack installation must not select a model or trigger training");
    std::filesystem::remove_all(root);
}

void test_persistent_serialized_training_resume_and_coalescing() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto state_file = root / "queue" / "jobs.json";
    const auto checkpoint = root / "checkpoints" / "job.bin";
    PersistentTrainingQueue queue(state_file);
    queue.load();
    const auto first = queue.submit("english:g2", "english-core", "candidate-g2", "g1");
    const auto duplicate = queue.submit("english:g2", "english-core", "other", "g1");
    require(!first.coalesced && duplicate.coalesced && duplicate.job.id == first.job.id &&
                queue.snapshot().size() == 1U,
            "duplicate nonterminal training jobs must coalesce durably");

    queue.set_interactive_busy(true);
    require(!queue.claim_next().has_value(),
            "interactive inference must prevent background training from claiming work");
    queue.set_interactive_busy(false);
    const auto claimed = queue.claim_next();
    require(claimed.has_value() && claimed->state == TrainingState::preparing &&
                claimed->attempts == 1U,
            "serialized training must claim exactly one queued job");
    require(queue.transition(claimed->id, TrainingState::preparing, TrainingState::training),
            "training transition must be persisted");
    std::string checkpoint_error;
    require(write_atomic_checkpoint(checkpoint, "checkpoint-v1", checkpoint_error) &&
                read_text(checkpoint) == "checkpoint-v1" &&
                !std::filesystem::exists(checkpoint.string() + ".tmp"),
            "training checkpoints must publish atomically without leftover temporary files");
    require(queue.transition(
                claimed->id,
                TrainingState::training,
                TrainingState::recoverable_failed,
                checkpoint,
                "host interrupted"),
            "an interrupted training job must become recoverable with its checkpoint");

    PersistentTrainingQueue restarted(state_file);
    restarted.load();
    const auto recovered = restarted.claim_next();
    require(recovered.has_value() && recovered->id == claimed->id &&
                recovered->state == TrainingState::preparing &&
                recovered->checkpoint_path == checkpoint && recovered->attempts == 2U,
            "a restarted queue must resume the exact persisted checkpoint");
    require(restarted.transition(
                recovered->id, TrainingState::preparing, TrainingState::training) &&
            restarted.transition(
                recovered->id, TrainingState::training, TrainingState::evaluating) &&
            restarted.transition(
                recovered->id, TrainingState::evaluating, TrainingState::promoting) &&
            restarted.transition(
                recovered->id, TrainingState::promoting, TrainingState::completed),
            "the durable state machine must traverse training, evaluation, promotion, and completion");
    std::filesystem::remove_all(root);
}

void test_service_recovers_serialized_job_before_candidate_creation() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    {
        ExpertRegistry registry(root / "experts" / "registry");
        registry.load();
        auto& module = registry.ensure("english-core", "threadspire://coder");
        registry.save(module);
        PersistentTrainingQueue queue(root / "jobs" / "training-state.json");
        queue.load();
        static_cast<void>(queue.submit(
            "skill:english-core:lora-overlay", "english-core", {}, {}));
    }

    LearningSettings settings;
    settings.python_executable = root / "missing-python.exe";
    settings.worker_script = root / "missing-worker.py";
    settings.state_root = root;
    settings.base_model = "threadspire://coder";
    settings.require_cuda = false;
    settings.automatic_consolidation = false;
    {
        ContinualLearningService service(std::move(settings));
        auto recovered = false;
        for (std::size_t attempt = 0U; attempt < 500U; ++attempt) {
            try {
                if (read_text(root / "jobs" / "training-state.json").find(
                        "recoverable-failed") != std::string::npos) {
                    recovered = true;
                    break;
                }
            } catch (const std::runtime_error&) {
                // Atomic replace may leave a sub-millisecond no-file observation.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(recovered,
                "a serialized job without a candidate record must be restored and advanced after restart");
        service.request_stop();
    }
    PersistentTrainingQueue reloaded(root / "jobs" / "training-state.json");
    reloaded.load();
    const auto jobs = reloaded.snapshot();
    require(jobs.size() == 1U && jobs.front().state == TrainingState::recoverable_failed,
            "restart recovery must leave a resumable terminal record instead of losing queued work");
    std::filesystem::remove_all(root);
}
void test_atomic_promotion_keeps_working_host_until_candidate_passes() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto pointer = root / "models" / "active.json";
    const auto candidate = root / "candidates" / "g2";
    write_text(pointer, R"({"generation":"g1"})");
    write_text(candidate / "manifest.json", R"({"generation":"g2"})");
    AtomicPromotionCoordinator coordinator;
    const AtomicPromotionRequest request{
        .active_pointer = pointer,
        .candidate_directory = candidate,
        .candidate_generation = "g2",
        .expected_parent_generation = "g1",
    };
    auto commit_called = false;
    const auto failed_host = coordinator.promote(
        request,
        [](const std::filesystem::path&) {
            return CandidateHostChecks{
                .artifact_checksum_valid = true,
                .candidate_host_started = false,
                .inference_canary_passed = false,
                .deterministic_validation_passed = true,
                .regression_gate_passed = true,
                .diagnostics = "candidate host failed to start",
            };
        },
        [&commit_called] {
            commit_called = true;
            return CandidatePromotionCommitResult{.succeeded = true};
        });
    require(failed_host.outcome == PromotionOutcome::recoverable_failed &&
                !commit_called &&
                read_text(pointer).find("g1") != std::string::npos &&
                read_text(pointer).find("g2") == std::string::npos,
            "a failed candidate host must leave the working model pointer untouched and skip commit");

    const auto promoted = coordinator.promote(
        request,
        [](const std::filesystem::path&) {
            return CandidateHostChecks{
                .artifact_checksum_valid = true,
                .candidate_host_started = true,
                .inference_canary_passed = true,
                .deterministic_validation_passed = true,
                .regression_gate_passed = true,
                .diagnostics = "independent checks passed",
            };
        },
        [&pointer] {
            write_text(pointer, R"({"generation":"g2"})");
            return CandidatePromotionCommitResult{
                .succeeded = true,
                .diagnostics = "manifest-aware compare-and-swap committed",
            };
        });
    require(promoted.outcome == PromotionOutcome::promoted &&
                read_text(pointer).find("g2") != std::string::npos,
            "the candidate must replace the active pointer only after every independent check passes");
    const auto stale = coordinator.promote(request, [](const std::filesystem::path&) {
        return CandidateHostChecks{
            .artifact_checksum_valid = true,
            .candidate_host_started = true,
            .inference_canary_passed = true,
            .deterministic_validation_passed = true,
            .regression_gate_passed = true,
        };
    });
    require(stale.outcome == PromotionOutcome::stale_parent,
            "compare-and-swap promotion must reject candidates trained against a stale parent");
    std::filesystem::remove_all(root);
}

void test_storage_cards_growth_projections_quotas_and_cleanup() {
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    write_text(root / "models" / "internal" / "active.json", R"({"generation":"g1"})");
    write_text(root / "models" / "internal" / "generations" / "g1" / "weights.bin", "model");
    write_text(root / "data" / "accepted.jsonl", "accepted");
    write_text(root / "packs" / "image-engine" / "pack.json", "{}");
    write_text(root / "outputs" / "image.png", "output");
    write_text(root / "cache" / "cache.bin", "cache");
    write_text(root / "logs" / "one.log", "0123456789");
    write_text(root / "logs" / "two.log", "abcdefghij");

    const auto snapshot = inspect_storage_with_growth(root, 0U, 8U, 8U);
    require(snapshot.cards.size() == 6U && snapshot.growth_bytes > 0 &&
                snapshot.cards.front().key == "current-model",
            "storage reporting must expose the six live cards and growth since the prior sample");
    require(snapshot.pack_projections.size() == CapabilityPackManager::catalog().size() &&
                std::ranges::any_of(snapshot.pack_projections, [](const PackSizeProjection& projection) {
                    return projection.id == "image-engine" && projection.installed &&
                        projection.download_bytes > 0U && projection.install_bytes > 0U;
                }),
            "storage reporting must include projected pack download/install sizes");
    require(!snapshot.largest_files.empty() && !snapshot.largest_directories.empty() &&
                snapshot.quotas.size() == 5U,
            "storage reporting must include largest files/directories and safe default quotas");

    const std::vector<StorageQuota> quota{{
        .name = "logs",
        .relative_path = "logs",
        .maximum_bytes = 5U,
    }};
    const auto dry_run = cleanup_storage(root, quota, true);
    require(dry_run.succeeded && dry_run.bytes_reclaimed == 20U &&
                std::filesystem::exists(root / "logs" / "one.log"),
            "quota cleanup must report reclaimable bytes without mutating during a dry run");
    const auto cleaned = cleanup_storage(root, quota, false);
    require(cleaned.succeeded && cleaned.bytes_reclaimed == 20U &&
                !std::filesystem::exists(root / "logs" / "one.log") &&
                !std::filesystem::exists(root / "logs" / "two.log"),
            "quota cleanup must remove only selected unprotected entries");
    const auto unsafe = cleanup_storage(root, {{
        .name = "unsafe",
        .relative_path = "../outside",
        .maximum_bytes = 1U,
    }}, true);
    require(!unsafe.succeeded && !unsafe.error.empty(),
            "storage cleanup must reject paths that escape the application root");
    std::filesystem::remove_all(root);
}

void test_safe_reset_path_validation() {
    using threadspire::ai::platform::validate_threadspire_reset_path;
    const auto root = temporary_root();
    const auto local = root / "AppData" / "Local";
    const auto exact = local / "ThreadSpire";
    std::filesystem::create_directories(exact);
    const auto valid = validate_threadspire_reset_path(exact, local);
    require(valid.valid && valid.resolved_target == valid.expected_target,
            "reset validation must accept only the exact LocalAppData/ThreadSpire directory");
    require(!validate_threadspire_reset_path(local, local).valid &&
                !validate_threadspire_reset_path(local / "ThreadSpire-old", local).valid &&
                !validate_threadspire_reset_path(root, local).valid,
            "reset validation must reject broad, sibling, and unrelated paths");
    std::filesystem::remove_all(root);
}

void test_named_module_and_native_base_contract() {
    const auto source = std::filesystem::path(THREADSPIRE_SOURCE_DIR);
    const auto cmake = read_text(source / "CMakeLists.txt");
    require(cmake.find("FILE_SET threadspire_modules") != std::string::npos &&
                cmake.find("TYPE CXX_MODULES") != std::string::npos,
            "CMake must build the public API through CXX_MODULES file sets");
    const std::vector<std::string> modules{
        "threadspire.core", "threadspire.platform", "threadspire.storage",
        "threadspire.models", "threadspire.providers", "threadspire.tasks",
        "threadspire.scheduler", "threadspire.learning", "threadspire.media",
        "threadspire.mcp", "threadspire.ui",
    };
    for (const auto& name : modules) {
        const auto leaf = name.substr(name.find('.') + 1U) + ".ixx";
        const auto contents = read_text(source / "modules" / "threadspire" / leaf);
        require(contents.find("export module " + name) != std::string::npos,
                "missing named module: " + name);
    }
    const auto resources = read_text(source / "resources" / "ThreadSpire.rc.in");
    const auto resource_ids = read_text(source / "resources" / "resource.h");
    const auto app_paths = read_text(source / "src" / "app" / "app_paths.cpp");
    const auto backend = read_text(source / "src" / "runtime" / "backend_manager.cpp");
    require(resources.find("model_worker.py") == std::string::npos &&
                resources.find("internal_model.py") == std::string::npos &&
                resources.find("curriculum_guard.py") == std::string::npos &&
                resource_ids.find("IDR_MODEL_WORKER") == std::string::npos &&
                app_paths.find("write_resource(IDR_MODEL_WORKER") == std::string::npos,
            "the base executable must not embed Python training programs");
    require(backend.find("pip install") == std::string::npos &&
                backend.find("Automatic Python/PyTorch installation was removed from the base application") != std::string::npos,
            "the base executable must not install Python, PyTorch, or private runtimes automatically");
    const auto reset_script = read_text(
        source / "tools" / "reset_threadspire_appdata.ps1");
    require(reset_script.find("Assert-ExactResetTarget") != std::string::npos &&
                reset_script.find("[IO.FileAttributes]::ReparsePoint") != std::string::npos &&
                reset_script.find("Get-TreeInventoryNoFollow") != std::string::npos &&
                reset_script.find("Remove-TreeNoFollow") != std::string::npos &&
                reset_script.find("Write-ManifestAtomically") != std::string::npos &&
                reset_script.find("DeletionCompleted = $false") != std::string::npos,
            "the release reset must validate the exact root, refuse link traversal, and publish its manifest before deletion");
}

} // namespace

int main() {
    try {
        test_unified_routing_profiles_multimodal_and_artifacts();
        test_unified_cancellation();
        test_pack_transaction_rollback_and_selection_isolation();
        test_persistent_serialized_training_resume_and_coalescing();
        test_service_recovers_serialized_job_before_candidate_creation();
        test_atomic_promotion_keeps_working_host_until_candidate_passes();
        test_storage_cards_growth_projections_quotas_and_cleanup();
        test_safe_reset_path_validation();
        test_named_module_and_native_base_contract();
        std::cout << "All ThreadSpire unified workbench tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Workbench test failure: " << exception.what() << '\n';
        return 1;
    }
}