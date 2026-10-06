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
#include <string_view>
#include <thread>
#include <vector>

import threadspire.learning;
import threadspire.platform;
import threadspire.providers;
import threadspire.scheduler;
import threadspire.storage;
import threadspire.tasks;

namespace {

std::filesystem::path workbench_executable;

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
        input_file_count_.store(request.input_files.size(), std::memory_order_release);
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

    [[nodiscard]] std::size_t input_file_count() const noexcept {
        return input_file_count_.load(std::memory_order_acquire);
    }

private:
    std::string id_;
    threadspire::ai::backend::ProviderCapabilities capabilities_;
    threadspire::ai::backend::ArtifactKind artifact_kind_;
    bool wait_for_cancel_{false};
    std::atomic_bool started_{false};
    std::atomic_size_t input_file_count_{0U};
};

void test_unified_routing_profiles_multimodal_and_artifacts() {
    using namespace threadspire::ai::backend;
    UnifiedProviderRouter router;
    auto text_provider = std::make_shared<RecordingProvider>(
        "text-and-tools",
        ProviderCapabilities{
            .interactive_text = true,
            .coding = true,
            .tools = true,
            .streaming = true,
        });
    router.register_provider(text_provider);
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
        .input_files = {"brief.txt", "code.cpp"},
        .on_event = [&streamed](const TaskEvent&) {
            ++streamed;
            return true;
        },
    });
    require(text.succeeded && text.provider_id == "text-and-tools" && streamed == 1U &&
                text_provider->input_file_count() == 2U,
            "interactive text and selected file inputs must stream through the unified provider contract");

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

void test_explicit_trainer_install_keeps_native_serving_pack() {
    using namespace threadspire::ai;
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    const auto packs = root / "packs";
    const auto native_source = root / "native-source";
    const auto trainer_source = root / "trainer-source";

    const auto catalog = CapabilityPackManager::catalog();
    const auto native_descriptor = std::ranges::find(
        catalog,
        std::string_view{"compact-native-inference"},
        &CapabilityPackDescriptor::id);
    const auto trainer_descriptor = std::ranges::find(
        catalog,
        std::string_view{"internal-pytorch-training"},
        &CapabilityPackDescriptor::id);
    require(native_descriptor != catalog.end() && trainer_descriptor != catalog.end(),
            "native serving and internal training pack fixtures must exist");

    for (const auto& relative : native_descriptor->required_files) {
        write_text(native_source / relative, "native-host-online");
    }
    for (const auto& relative : trainer_descriptor->required_files) {
        write_text(trainer_source / relative, "trainer-fixture");
    }

    CapabilityPackManager manager(packs);
    require(manager.install_transactional({
                .descriptor = *native_descriptor,
                .source_directory = native_source,
            }).succeeded,
            "the native serving fixture must install transactionally");
    const auto native_host = manager.pack_path(native_descriptor->id) /
        native_descriptor->required_files.front();
    const auto native_manifest_before = read_text(
        manager.pack_path(native_descriptor->id) / "pack.json");
    std::ifstream active_native_session(native_host, std::ios::binary);
    require(static_cast<bool>(active_native_session),
            "the native host fixture must remain open as the simulated serving session");

    const auto trainer_install = manager.install_transactional({
        .descriptor = *trainer_descriptor,
        .source_directory = trainer_source,
    });

    app::AppPaths paths;
    paths.root = root;
    paths.packs = packs;
    const auto trainer = manager.pack_path(trainer_descriptor->id);
    paths.python = trainer / "runtime" / "Scripts" / "python.exe";
    paths.worker = trainer / "backend" / "model_worker.py";
    paths.internal_model_module = trainer / "backend" / "internal_model.py";
    paths.curriculum_guard_module = trainer / "backend" / "curriculum_guard.py";
    require(trainer_install.succeeded && backend_files_present(paths),
            "an explicit transaction must install and expose the separate trainer pack");
    std::string live_native_reply;
    active_native_session >> live_native_reply;
    require(live_native_reply == "native-host-online" &&
                manager.installed(native_descriptor->id) &&
                read_text(manager.pack_path(native_descriptor->id) / "pack.json") ==
                    native_manifest_before,
            "trainer installation must leave the active native serving pack untouched");

    const auto ui = read_text(
        std::filesystem::path(THREADSPIRE_SOURCE_DIR) / "src" / "app" / "main_window.cpp");
    const auto install_begin = ui.find("void MainWindow::install_backend(const bool automatic)");
    const auto install_end = ui.find("void MainWindow::verify_backend", install_begin);
    const auto verify_end = ui.find("void MainWindow::start_backend", install_end);
    require(install_begin != std::string::npos && install_end > install_begin &&
                verify_end > install_end,
            "the automatic trainer install and verification lifecycle must remain inspectable");
    const auto install_body = ui.substr(install_begin, install_end - install_begin);
    const auto verify_body = ui.substr(install_end, verify_end - install_end);
    require(install_body.find("if (automatic)") != std::string::npos &&
                install_body.find("installed explicitly from Settings > AI & Tools") != std::string::npos &&
                install_body.find("backend_server_.stop()") == std::string::npos &&
                verify_body.find("backend_server_.running()") == std::string::npos &&
                verify_body.find("runtime::verify_backend") != std::string::npos,
            "the trainer must require explicit installation without stopping native serving or blocking verification");

    const auto lifecycle_begin = ui.find("void MainWindow::drive_automatic_lifecycle()");
    const auto lifecycle_end = ui.find(
        "void MainWindow::update_internal_model_lifecycle_ui()", lifecycle_begin);
    require(lifecycle_begin != std::string::npos && lifecycle_end > lifecycle_begin,
            "the automatic lifecycle coordinator must remain inspectable");
    const auto lifecycle_body = ui.substr(lifecycle_begin, lifecycle_end - lifecycle_begin);
    const auto missing_trainer_begin = lifecycle_body.find(
        "if (!runtime::backend_files_present(selected_paths))");
    const auto missing_trainer_end = lifecycle_body.find(
        "if (backend_repair_required_)", missing_trainer_begin);
    const auto missing_trainer_body =
        missing_trainer_begin == std::string::npos ||
            missing_trainer_end == std::string::npos
        ? std::string{}
        : lifecycle_body.substr(
            missing_trainer_begin,
            missing_trainer_end - missing_trainer_begin);
    const auto lifecycle_ui_end = ui.find(
        "void MainWindow::update_global_activity()", lifecycle_end);
    require(lifecycle_ui_end > lifecycle_end,
            "the visible trainer lifecycle status must remain inspectable");
    const auto lifecycle_ui = ui.substr(lifecycle_end, lifecycle_ui_end - lifecycle_end);
    const auto activity_end = ui.find(
        "void MainWindow::update_periodic_state()", lifecycle_ui_end);
    require(activity_end > lifecycle_ui_end,
            "the always-visible activity status must remain inspectable");
    const auto activity_ui = ui.substr(lifecycle_ui_end, activity_end - lifecycle_ui_end);
    const auto idle_ready_begin = activity_ui.find("READY — No work is running");
    const auto idle_ready_end = activity_ui.find(
        "if (storage_measurement_running)", idle_ready_begin);
    const auto idle_ready_ui = idle_ready_begin == std::string::npos ||
            idle_ready_end == std::string::npos
        ? std::string{}
        : activity_ui.substr(idle_ready_begin, idle_ready_end - idle_ready_begin);
    const auto automation_toggle_begin = ui.find("case id::models_automation_toggle:");
    const auto automation_toggle_end = ui.find(
        "case id::models_advanced_header:", automation_toggle_begin);
    const auto automation_toggle_body =
        automation_toggle_begin == std::string::npos ||
            automation_toggle_end == std::string::npos
        ? std::string{}
        : ui.substr(
            automation_toggle_begin,
            automation_toggle_end - automation_toggle_begin);
    require(lifecycle_body.find("install_backend(true)") == std::string::npos &&
                missing_trainer_body.find(
                    "if (learning_pipeline.queued_updates > 0U)") !=
                    std::string::npos &&
                lifecycle_ui.find("Install selected package") != std::string::npos &&
                lifecycle_ui.find(
                    "Internal PyTorch Training is available but not installed") ==
                    std::string::npos &&
                lifecycle_ui.find(
                    "Internal PyTorch Training is optional and not installed") ==
                    std::string::npos &&
                lifecycle_ui.find(
                    "Native answers remain available. No neural learning work is waiting.") !=
                    std::string::npos &&
                !automation_toggle_body.empty() &&
                automation_toggle_body.find("Install selected package") ==
                    std::string::npos &&
                activity_ui.find("ATTENTION — Internal PyTorch Training required") != std::string::npos &&
                activity_ui.find("backend_repair_required_ && !internal_training_pack_ready") != std::string::npos &&
                activity_ui.find("missing_training_requirements_text(training_paths)") != std::string::npos &&
                !idle_ready_ui.empty() &&
                idle_ready_ui.find("Internal PyTorch") == std::string::npos &&
                idle_ready_ui.find("not installed") == std::string::npos,
            "automatic coordination must keep an absent optional trainer silent until queued neural work is blocked, then show the exact durable manual action without installing it");

    const auto create_begin = ui.find("void MainWindow::create_internal_model(");
    const auto create_end = ui.find("void MainWindow::install_backend", create_begin);
    require(create_begin != std::string::npos && create_end > create_begin,
            "the isolated candidate-training lifecycle must remain inspectable");
    const auto create_body = ui.substr(create_begin, create_end - create_begin);
    const auto preservation_gate = create_body.find(
        "const bool preserve_serving_generation = bootstrap && backend_server_.running()");
    const auto destructive_stop = create_body.find("if (!bootstrap)", preservation_gate);
    const auto promoted_generation = create_body.find(
        "trained_state.manifest_valid || !trained_state.bootstrap_complete",
        destructive_stop);
    const auto promotion_stop = create_body.find(
        "if (preserve_serving_generation)", promoted_generation);
    const auto replacement_start = create_body.find(
        "backend_server_.start(", promotion_stop);
    require(preservation_gate != std::string::npos &&
                destructive_stop > preservation_gate &&
                promoted_generation > destructive_stop &&
                promotion_stop > promoted_generation &&
                replacement_start > promotion_stop,
            "candidate training must preserve the working host and switch it only after a ready generation is promoted");

    active_native_session.close();
    std::filesystem::remove_all(root);
}

void test_backend_requirements_report_exact_missing_files() {
    using namespace threadspire::ai;
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    app::AppPaths paths;
    paths.root = root;
    paths.packs = root / "packs";

    const auto catalog = CapabilityPackManager::catalog();
    const auto descriptor = std::ranges::find(
        catalog,
        std::string_view{"internal-pytorch-training"},
        &CapabilityPackDescriptor::id);
    require(descriptor != catalog.end(),
            "the internal trainer capability descriptor must exist");

    auto missing = missing_backend_files(paths);
    require(missing.size() == descriptor->required_files.size() + 1U &&
                missing.front() == "internal-pytorch-training/pack.json" &&
                std::ranges::find(
                    missing,
                    std::string{"internal-pytorch-training/backend/curriculum_compiler.py"}) !=
                    missing.end(),
            "an absent trainer must report its manifest and every exact descriptor requirement");

    const auto pack = paths.packs / descriptor->id;
    write_text(pack / "pack.json", "{\"id\":\"internal-pytorch-training\"}\n");
    for (const auto& relative : descriptor->required_files) {
        write_text(pack / relative, "trainer-requirement");
    }
    require(backend_files_present(paths) && missing_backend_files(paths).empty(),
            "every descriptor requirement must satisfy the trainer presence check");

    std::filesystem::remove(pack / "backend" / "curriculum_compiler.py");
    missing = missing_backend_files(paths);
    require(!backend_files_present(paths) && missing.size() == 1U &&
                missing.front() ==
                    "internal-pytorch-training/backend/curriculum_compiler.py",
            "a partial trainer must identify curriculum_compiler.py by its exact pack path");
    const auto verification = verify_backend(paths, root / "verify.log", {});
    require(verification.state == util::OperationState::failed &&
                verification.message.find(
                    "internal-pytorch-training/backend/curriculum_compiler.py") !=
                    std::string::npos &&
                verification.message.find("nothing installs automatically") !=
                    std::string::npos,
            "a blocked trainer action must surface its exact missing path without installing it");
    std::filesystem::remove_all(root);
}

void test_native_host_launch_never_installs_bundled_pack() {
    using namespace threadspire::ai;
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    app::AppPaths paths;
    paths.executable = root / "release" / "EngCoder.exe";
    paths.root = root / "isolated-state";
    paths.packs = paths.root / "packs";
    paths.logs = paths.root / "logs";
    paths.ready_file = paths.root / "control" / "native.ready";
    write_text(
        paths.executable.parent_path() / "optional-packs" /
            "compact-native-inference" / "bin" / "engcoder-native-host.exe",
        "not-an-executable");

    BackendServer server(paths);
    std::string message;
    require(!server.start({}, message),
            "the invalid bundled-host fixture must fail to launch");
    require(!std::filesystem::exists(paths.packs / "compact-native-inference"),
            "starting the bundled native host must never publish an AppData pack");
    std::filesystem::remove_all(root);
}

void test_backend_startup_accessors_remain_responsive_while_cancelling() {
    using namespace threadspire::ai;
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    app::AppPaths paths;
    paths.executable = root / "release" / "EngCoder.exe";
    paths.root = root / "isolated-state";
    paths.packs = paths.root / "packs";
    paths.logs = paths.root / "logs";
    paths.ready_file = paths.root / "control" / "native.ready";
    std::filesystem::create_directories(paths.logs);
    std::filesystem::create_directories(paths.ready_file.parent_path());

    const auto host = paths.executable.parent_path() / "optional-packs" /
        "compact-native-inference" / "bin" / "engcoder-native-host.exe";
    std::filesystem::create_directories(host.parent_path());
    std::filesystem::copy_file(
        workbench_executable,
        host,
        std::filesystem::copy_options::overwrite_existing);
    std::error_code permission_error;
    std::filesystem::permissions(
        host,
        std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add,
        permission_error);

    BackendServer server(paths);
    std::string start_message;
    auto startup = std::async(std::launch::async, [&] {
        return server.start({}, start_message);
    });
    const auto launch_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(server.log_path()) &&
           startup.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
           std::chrono::steady_clock::now() < launch_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(startup.wait_for(std::chrono::milliseconds(0)) ==
                std::future_status::timeout,
            "the fake native host must still be waiting for readiness");

    const auto accessor_begin = std::chrono::steady_clock::now();
    const auto reported_running = server.running();
    const auto degradation = server.degradation_message();
    const auto accessor_elapsed =
        std::chrono::steady_clock::now() - accessor_begin;
    require(!reported_running && degradation.empty() &&
                accessor_elapsed < std::chrono::milliseconds(500),
            "running and degradation accessors must not wait behind native startup polling");

    server.stop();
    require(startup.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
                !startup.get() &&
                start_message.find("startup cancelled") != std::string::npos,
            "stop must promptly cancel and join a native startup that never becomes ready");
    require(!server.running() && server.base_url().empty(),
            "cancelled startup must leave no published native server state");
    std::filesystem::remove_all(root);
}

void test_degraded_native_health_stays_labeled_as_foundation() {
    const auto source = std::filesystem::path(THREADSPIRE_SOURCE_DIR);
    const auto native_host = read_text(source / "src" / "native_host" / "main.cpp");
    const auto backend_server = read_text(
        source / "src" / "runtime" / "backend_server.cpp");
    const auto ui = read_text(source / "src" / "app" / "main_window.cpp");

    require(native_host.find("requested_generation") != std::string::npos &&
                native_host.find("fallback_reason") != std::string::npos &&
                native_host.find("\\\"degraded\\\":true") != std::string::npos &&
                backend_server.find("health->find(\"degraded\")") != std::string::npos &&
                backend_server.find("health->find(\"requested_generation\")") !=
                    std::string::npos &&
                backend_server.find("health->find(\"fallback_reason\")") !=
                    std::string::npos &&
                backend_server.find("serving the verified native foundation instead") !=
                    std::string::npos &&
                ui.find("backend_server_.degradation_message()") != std::string::npos &&
                ui.find("ENGCODER — NATIVE FOUNDATION") != std::string::npos &&
                ui.find("Requested neural generation was rejected") != std::string::npos,
            "a rejected requested generation must remain an explicit degraded foundation in native health, backend state, and visible UI labels");
}

void test_pack_audit_reports_exact_files_licenses_roles_and_weights() {
    using namespace threadspire::ai::runtime;
    const auto root = temporary_root();
    const auto dependency_source = root / "dependency-source";
    const auto source = root / "audit-source";
    write_text(dependency_source / "runtime" / "shared.dll", "identical-runtime-bytes");
    write_text(dependency_source / "pack.ready", "dependency-ready");
    write_text(source / "runtime" / "shared.dll", "identical-runtime-bytes");
    write_text(source / "pack.ready", "audit-ready");
    write_text(
        source / "runtime" / "Lib" / "site-packages" / "peft" / "__init__.py",
        "__version__ = '0.14.0'\n");
    write_text(
        source / "runtime" / "Lib" / "site-packages" /
            "peft-0.14.0.dist-info" / "METADATA",
        "Metadata-Version: 2.1\n"
        "Name: peft\n"
        "Version: 0.14.0\n"
        "License-Expression: Apache-2.0\n"
        "Home-page: https://github.com/huggingface/peft\n\n");
    write_text(
        source / "runtime" / "Lib" / "site-packages" /
            "peft-0.14.0.dist-info" / "RECORD",
        "peft/__init__.py,,\n"
        "peft-0.14.0.dist-info/METADATA,,\n"
        "peft-0.14.0.dist-info/RECORD,,\n");
    write_text(source / "models" / "adapter.safetensors", "weights");

    CapabilityPackManager manager(root / "packs");
    const CapabilityPackDescriptor dependency{
        .id = "audit-dependency",
        .name = "Audit Dependency",
        .version = "3",
        .kind = CapabilityPackKind::internal_pytorch_training,
        .required_files = {"pack.ready"},
    };
    require(manager.install_transactional({
                .descriptor = dependency,
                .source_directory = dependency_source,
            }).succeeded,
            "audit dependency fixture must install transactionally");
    const CapabilityPackDescriptor descriptor{
        .id = "audit-pack",
        .name = "Audit Pack",
        .version = "7",
        .kind = CapabilityPackKind::transformers_peft_finetuning,
        .required_files = {"pack.ready"},
        .dependencies = {"audit-dependency"},
        .purpose = "fixture purpose",
        .origin = "fixture origin",
        .license = "fixture wrapper license",
        .loading_condition = "fixture explicit job only",
        .safe_removal_rule = "fixture whole-pack removal only",
    };
    require(manager.install_transactional({
                .descriptor = descriptor,
                .source_directory = source,
            }).succeeded,
            "audited fixture must install transactionally");

    const auto audit = manager.audit("audit-pack");
    require(audit.state == CapabilityPackState::installed &&
                audit.descriptor.version == "7" && audit.descriptor.purpose == "fixture purpose" &&
                audit.file_count == 7U && audit.installed_bytes > 0U,
            "audit must report the installed manifest version, metadata, and exact complete tree");
    require(audit.model_weight_files == 1U && audit.model_weight_bytes == 7U,
            "audit must distinguish actual model-weight files from training infrastructure");
    const auto package = std::ranges::find_if(audit.packages, [](const auto& item) {
        return item.name == "peft";
    });
    require(package != audit.packages.end() && package->version == "0.14.0" &&
                package->license == "Apache-2.0" &&
                package->origin == "https://github.com/huggingface/peft" &&
                package->file_count == 3U,
            "installed METADATA and RECORD must attribute exact version, origin, license, and files");
    const auto program = std::ranges::find_if(audit.files, [](const auto& file) {
        return file.relative_path.generic_string().ends_with("peft/__init__.py");
    });
    require(program != audit.files.end() && program->purpose == "Python program code" &&
                program->owner == "peft 0.14.0",
            "every file must expose a concrete role and owning dependency version");
    const auto duplicate = std::ranges::find_if(audit.files, [](const auto& file) {
        return file.relative_path.generic_string() == "runtime/shared.dll";
    });
    require(duplicate != audit.files.end() &&
                duplicate->duplicate_of == "audit-dependency/runtime/shared.dll",
            "redundancy must be confirmed byte-for-byte against required installed packs");
    const auto missing = manager.audit("not-installed");
    require(missing.state == CapabilityPackState::available && missing.file_count == 0U &&
                missing.model_weight_files == 0U,
            "an uninstalled catalog/source must never be represented as installed files or weights");
    std::filesystem::remove_all(root);
}
void test_persistent_serialized_training_resume_and_coalescing() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto state_file = root / "queue" / "jobs.json";
    const auto checkpoint = root / "checkpoints" / "job.bin";
    PersistentTrainingQueue queue(state_file);
    queue.load();
    const auto first = queue.submit("english:g2", "english-core", {}, "g1");
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
    require(queue.bind_candidate(claimed->id, "candidate-g2", "g1"),
            "the durable job must bind to the exact shadow candidate before training can yield");
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
                recovered->checkpoint_path == checkpoint && recovered->attempts == 2U &&
                recovered->candidate_id == "candidate-g2" &&
                recovered->parent_generation == "g1",
            "a restarted queue must resume the exact persisted candidate and checkpoint");
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

void test_persistent_queue_survives_concurrent_readers() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto state_file = root / "jobs" / "training-state.json";
    PersistentTrainingQueue queue(state_file);
    queue.load();
    std::atomic_bool partial_read{false};
    {
        std::jthread reader([&](const std::stop_token stop_token) {
            while (!stop_token.stop_requested()) {
                std::ifstream stream(state_file, std::ios::binary);
                const std::string snapshot{
                    std::istreambuf_iterator<char>(stream),
                    std::istreambuf_iterator<char>()};
                const auto last = snapshot.find_last_not_of("\r\n\t ");
                if (!snapshot.empty() &&
                    (snapshot.front() != '[' || last == std::string::npos || snapshot[last] != ']')) {
                    partial_read.store(true, std::memory_order_release);
                    return;
                }
            }
        });
        for (auto index = 0U; index < 32U; ++index) {
            const auto submitted = queue.submit(
                "reader-stress:" + std::to_string(index),
                "english-core");
            const auto claimed = queue.claim_next();
            require(claimed.has_value() && claimed->id == submitted.job.id,
                    "concurrent status readers must not prevent serialized job claiming");
            require(queue.transition(
                        claimed->id,
                        TrainingState::preparing,
                        TrainingState::rejected,
                        {},
                        "intentional test terminal"),
                    "concurrent status readers must not prevent atomic terminal publication");
        }
        reader.request_stop();
        reader.join();
    }
    require(!partial_read.load(std::memory_order_acquire),
            "atomic queue publication must never expose a partial JSON document to readers");
    PersistentTrainingQueue reloaded(state_file);
    reloaded.load();
    require(reloaded.snapshot().size() == 32U,
            "all state transitions must remain durable while status readers are active");
    std::filesystem::remove_all(root);
}

void test_persistent_queue_outwaits_transient_windows_reader() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto state_file = root / "jobs" / "training-state.json";
    PersistentTrainingQueue queue(state_file);
    queue.load();
    static_cast<void>(queue.submit("initial", "english-core"));

#ifdef _WIN32
    // MSVC's std::ifstream does not grant FILE_SHARE_DELETE. Hold that exact
    // kind of status-reader handle beyond the former 500 ms writer budget.
    std::ifstream blocking_reader(state_file, std::ios::binary);
    require(static_cast<bool>(blocking_reader),
            "the transient-reader regression must hold the published state file open");
    std::promise<void> writer_entered;
    auto entered = writer_entered.get_future();
    auto writer = std::async(std::launch::async, [&] {
        writer_entered.set_value();
        try {
            static_cast<void>(queue.submit("after-reader", "english-core"));
            return std::string{};
        } catch (const std::exception& exception) {
            return std::string{exception.what()};
        }
    });
    entered.wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    const auto writer_waited =
        writer.wait_for(std::chrono::milliseconds::zero()) == std::future_status::timeout;
    blocking_reader.close();
    const auto writer_error = writer.get();
    require(writer_waited && writer_error.empty(),
            "atomic queue publication must outwait a transient Windows reader instead of failing after 500 ms: " +
                writer_error);
#else
    static_cast<void>(queue.submit("after-reader", "english-core"));
#endif

    PersistentTrainingQueue reloaded(state_file);
    reloaded.load();
    require(reloaded.snapshot().size() == 2U,
            "the state update delayed by a transient reader must remain durable");
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

void test_service_reconciles_one_durable_job_per_interrupted_candidate() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    std::string candidate_id;
    {
        ExpertRegistry experts(root / "experts" / "registry");
        experts.load();
        auto& module = experts.ensure("english-core", "threadspire://coder");
        experts.save(module);

        CandidateRegistry candidates(root / "candidates" / "registry");
        candidates.load();
        auto candidate = candidates.create(
            "english-core",
            "threadspire://coder",
            CandidateKind::full_rank_skill,
            module.generation,
            {},
            root / "candidates" / "job",
            root / "candidates" / "job.log");
        candidate.state = CandidateState::training;
        candidates.save(candidate);
        candidate_id = candidate.id;

        PersistentTrainingQueue queue(root / "jobs" / "training-state.json");
        queue.load();
        const auto job = queue.submit(
            "skill:english-core:full-rank-skill",
            "english-core",
            candidate_id,
            std::to_string(module.generation));
        require(queue.transition(job.job.id, TrainingState::queued, TrainingState::preparing) &&
                    queue.transition(job.job.id, TrainingState::preparing, TrainingState::training),
                "test setup must leave one candidate-bound durable job in flight");
    }

    LearningSettings settings;
    settings.python_executable = root / "missing-python.exe";
    settings.worker_script = root / "missing-worker.py";
    settings.state_root = root;
    settings.base_model = "threadspire://coder";
    settings.initial_interactive_priority = true;
    settings.require_cuda = false;
    settings.automatic_consolidation = false;
    {
        ContinualLearningService service(std::move(settings));
        require(service.pipeline_snapshot().queued_updates == 1U,
                "startup must reconcile the candidate registry with its existing durable job instead of duplicating it");
        const auto candidates = service.candidate_snapshot();
        const auto recovered = std::ranges::find(candidates, candidate_id, &CandidateRecord::id);
        require(recovered != candidates.end() && recovered->state == CandidateState::interrupted,
                "the one recovered candidate must remain visibly interrupted and resumable");
        service.request_stop();
    }

    PersistentTrainingQueue reloaded(root / "jobs" / "training-state.json");
    reloaded.load();
    require(reloaded.snapshot().size() == 1U,
            "restart reconciliation must never leave a duplicate candidate-keyed job behind");
    std::filesystem::remove_all(root);
}

void test_service_recovers_interrupted_internal_promotion_transaction() {
    using namespace threadspire::ai::learning;
    const auto root = temporary_root();
    const auto registry_file = root / "experts" / "registry" / "english-core.ini";
    std::string previous_registry;
    {
        ExpertRegistry registry(root / "experts" / "registry");
        registry.load();
        auto& module = registry.ensure("english-core", "threadspire://coder");
        registry.save(module);
        previous_registry = read_text(registry_file);
        module.state = ExpertState::full_rank_active;
        module.module_path = root / "models" / "internal" / "generations" / "g2";
        module.promoted_candidate_id = "candidate-g2";
        ++module.generation;
        registry.save(module);
    }
    const auto transaction = root / "state" / "internal-promotion-transaction";
    write_text(transaction / "expert-before.ini", previous_registry);
    write_text(
        transaction / "pending.json",
        R"({"candidate_generation":"g2","parent_generation":"g1","registry_file":"english-core.ini","had_registry":true})");
    write_text(root / "models" / "internal" / "active.json", R"({"generation":"g1"})");

    LearningSettings settings;
    settings.python_executable = root / "missing-python.exe";
    settings.worker_script = root / "missing-worker.py";
    settings.state_root = root;
    settings.base_model = "threadspire://coder";
    settings.initial_interactive_priority = true;
    settings.require_cuda = false;
    settings.automatic_consolidation = false;
    {
        ContinualLearningService service(std::move(settings));
        const auto experts = service.expert_snapshot();
        const auto recovered = std::ranges::find(experts, "english-core", &ExpertModule::skill);
        require(recovered != experts.end() &&
                    recovered->state == ExpertState::memory_only &&
                    recovered->generation == 0U &&
                    recovered->module_path.empty(),
                "startup must restore the prior registry when a crash occurs before pointer commit");
        require(!std::filesystem::exists(transaction),
                "a recovered internal promotion transaction must be cleared");
        service.request_stop();
    }
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
    require(snapshot.cards.size() == 7U && snapshot.growth_bytes > 0 &&
                snapshot.cards.front().key == "current-model" &&
                snapshot.cards[1].key == "all-models" &&
                snapshot.cards[1].bytes > snapshot.cards.front().bytes,
            "storage reporting must expose all seven disk cards, including every model generation, and growth since the prior sample");
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
    using threadspire::ai::platform::validate_engcoder_reset_path;
    const auto root = temporary_root();
    const auto local = root / "AppData" / "Local";
    const auto exact = local / "EngCoder";
    std::filesystem::create_directories(exact);
    const auto valid = validate_engcoder_reset_path(exact, local);
    require(valid.valid && valid.resolved_target == valid.expected_target,
            "reset validation must accept only the exact LocalAppData/EngCoder directory");
    require(!validate_engcoder_reset_path(local, local).valid &&
                !validate_engcoder_reset_path(local / "EngCoder-old", local).valid &&
                !validate_engcoder_reset_path(root, local).valid,
            "reset validation must reject broad, sibling, and unrelated paths");
    std::filesystem::remove_all(root);
}

void test_repository_candidate_promotion_isolated_and_stale_safe() {
    namespace lh = threadspire::ai::long_horizon;
    namespace platform = threadspire::ai::platform;
    const auto root = temporary_root();
    const auto source = root / "source";
    const auto run_directory = root / "state" / "trajectories" / "run-isolation-test";
    const auto staged = run_directory / "staged-repository";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(run_directory);

#ifdef _WIN32
    const std::filesystem::path git = "git.exe";
#else
    const std::filesystem::path git = "git";
#endif
    std::size_t command_serial = 0U;
    const auto run_git = [&](
        const std::filesystem::path& working_directory,
        std::vector<std::string> arguments) {
        const auto log = root / ("git-" + std::to_string(++command_serial) + ".log");
        const auto result = platform::run_process({
            .executable = git,
            .arguments = std::move(arguments),
            .working_directory = working_directory,
            .log_file = log,
            .append_log = false,
        });
        require(result.launched && result.exit_code == 0,
            "Git fixture command failed; see " + log.string());
        return log;
    };
    const auto output = [](const std::filesystem::path& log) {
        auto value = read_text(log);
        while (!value.empty() &&
               (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) {
            value.pop_back();
        }
        return value;
    };

    static_cast<void>(run_git(source, {"init", "--quiet"}));
    static_cast<void>(run_git(source, {"config", "user.name", "EngCoder Test"}));
    static_cast<void>(run_git(source, {"config", "user.email", "engcoder-test@local.invalid"}));
    write_text(source / "answer.txt", "base\n");
    static_cast<void>(run_git(source, {"add", "answer.txt"}));
    static_cast<void>(run_git(source, {"commit", "--quiet", "-m", "base"}));
    const auto source_head = output(run_git(source, {"rev-parse", "HEAD"}));

    static_cast<void>(run_git(
        run_directory,
        {"clone", "--no-hardlinks", "--quiet", source.string(), staged.string()}));
    static_cast<void>(run_git(staged, {"config", "user.name", "EngCoder Test"}));
    static_cast<void>(run_git(staged, {"config", "user.email", "engcoder-test@local.invalid"}));
    write_text(staged / "answer.txt", "verified candidate\n");
    static_cast<void>(run_git(staged, {"add", "answer.txt"}));
    static_cast<void>(run_git(staged, {"commit", "--quiet", "-m", "candidate"}));
    const auto candidate_commit = output(run_git(staged, {"rev-parse", "HEAD"}));
    const auto final_diff = run_directory / "final.diff";
    const auto diff_log = run_git(staged, {"diff", "--binary", source_head, candidate_commit, "--"});
    write_text(final_diff, read_text(diff_log));
    const auto manifest = run_directory / "candidate.json";
    write_text(manifest, "{\"status\":\"verified-awaiting-approval\"}\n");

    const lh::Result candidate{
        .operation = {},
        .completed = true,
        .compiler_passed = true,
        .tests_passed = true,
        .iterations = 1,
        .run_directory = run_directory,
        .trajectory_path = run_directory / "trajectory.jsonl",
        .final_diff_path = final_diff,
        .source_repository = source,
        .staged_repository = staged,
        .candidate_manifest_path = manifest,
        .source_head = source_head,
        .candidate_commit = candidate_commit,
        .final_diff = read_text(final_diff),
        .events = {},
    };

    const auto promoted = lh::promote_verified_candidate(candidate);
    require(promoted.operation.state == threadspire::ai::util::OperationState::succeeded &&
                promoted.branch_name.starts_with("engcoder/candidate-") &&
                read_text(source / "answer.txt") == "base\n" &&
                output(run_git(source, {"rev-parse", "HEAD"})) == source_head &&
                output(run_git(source, {"rev-parse", "refs/heads/" + promoted.branch_name})) ==
                    candidate_commit,
            "promotion must atomically publish only a review branch and never change the current branch or files");

    write_text(source / "untracked.txt", "dirty\n");
    const auto dirty = lh::promote_verified_candidate(candidate);
    require(dirty.operation.state == threadspire::ai::util::OperationState::failed &&
                dirty.operation.message.find("uncommitted") != std::string::npos,
            "promotion must block when the source working tree changes");
    std::filesystem::remove(source / "untracked.txt");

    write_text(source / "source-only.txt", "new head\n");
    static_cast<void>(run_git(source, {"add", "source-only.txt"}));
    static_cast<void>(run_git(source, {"commit", "--quiet", "-m", "source moved"}));
    const auto stale = lh::promote_verified_candidate(candidate);
    require(stale.operation.state == threadspire::ai::util::OperationState::failed &&
                stale.operation.message.find("moved") != std::string::npos &&
                read_text(source / "answer.txt") == "base\n",
            "promotion must reject a candidate tested against a stale source revision");

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
    const auto resources = read_text(source / "resources" / "engcoder.rc.in");
    const auto resource_ids = read_text(source / "resources" / "resource.h");
    const auto app_paths = read_text(source / "src" / "app" / "app_paths.cpp");
    const auto backend = read_text(source / "src" / "runtime" / "backend_manager.cpp");
    const auto backend_server = read_text(source / "src" / "runtime" / "backend_server.cpp");
    const auto native_host = read_text(source / "src" / "native_host" / "main.cpp");
    require(resources.find("model_worker.py") == std::string::npos &&
                resources.find("internal_model.py") == std::string::npos &&
                resources.find("curriculum_guard.py") == std::string::npos &&
                resources.find("curriculum_compiler.py") == std::string::npos &&
                resource_ids.find("IDR_MODEL_WORKER") == std::string::npos &&
                app_paths.find("write_resource(IDR_MODEL_WORKER") == std::string::npos,
            "the base executable must not embed Python training programs");
    require(backend.find("pip install") == std::string::npos &&
                backend.find("Automatic Python/PyTorch installation was removed from the base application") != std::string::npos,
            "the base executable must not install Python, PyTorch, or private runtimes automatically");
    require(cmake.find("add_executable(EngCoderNativeHost") != std::string::npos &&
                cmake.find("optional-packs/compact-native-inference/bin") != std::string::npos &&
                cmake.find("engcoder_native_inference_contract") != std::string::npos &&
                backend_server.find(".executable = host") != std::string::npos &&
                backend_server.find(".executable = python") == std::string::npos &&
                backend_server.find("install_transactional") == std::string::npos &&
                backend_server.find("resolve_native_host") != std::string::npos &&
                backend_server.find("return source / \"bin\" / \"engcoder-native-host.exe\"") != std::string::npos &&
                backend_server.find("installed_pack_version") != std::string::npos &&
                backend_server.find("descriptor->version") != std::string::npos &&
                backend_server.find("degradation_message_") != std::string::npos &&
                native_host.find("--engine-contract-self-test") != std::string::npos &&
                native_host.find("generation_ = \"foundation\"") != std::string::npos &&
                native_host.find("approved_response(normalized)") != std::string::npos &&
                native_host.find("the native foundation has not learned this request yet") != std::string::npos &&
                native_host.find("/v1/chat/completions") != std::string::npos &&
                native_host.find("text/event-stream; charset=utf-8") != std::string::npos &&
                native_host.find("valid_utf8") != std::string::npos &&
                native_host.find("#include <Python") == std::string::npos &&
                native_host.find("#include <torch") == std::string::npos,
            "normal built-in answers must run the verified release host in place or use an explicitly installed native pack, with no Python or PyTorch dependency");
    require(cmake.find("resources/engcoder-eagle.ico") != std::string::npos &&
                cmake.find("OUTPUT_NAME \"EngCoder\"") != std::string::npos &&
                resources.find("IDI_ENGCODER ICON") != std::string::npos &&
                resources.find("VALUE \"OriginalFilename\", \"EngCoder.exe\"") != std::string::npos &&
                resource_ids.find("#define IDI_ENGCODER") != std::string::npos &&
                std::filesystem::file_size(source / "resources" / "engcoder-eagle.ico") > 0U &&
                std::filesystem::file_size(source / "resources" / "engcoder-eagle.png") > 0U,
            "EngCoder must ship its American-eagle icon as a real native application resource");
    const auto reset_script = read_text(
        source / "tools" / "reset_engcoder_appdata.ps1");
    require(reset_script.find("Assert-ExactResetTarget") != std::string::npos &&
                reset_script.find("[IO.FileAttributes]::ReparsePoint") != std::string::npos &&
                reset_script.find("Get-TreeInventoryNoFollow") != std::string::npos &&
                reset_script.find("Remove-TreeNoFollow") != std::string::npos &&
                reset_script.find("Write-ManifestAtomically") != std::string::npos &&
                reset_script.find("DeletionCompleted = $false") != std::string::npos,
            "the release reset must validate the exact root, refuse link traversal, and publish its manifest before deletion");
}

} // namespace

int main(const int argument_count, char** arguments) {
    if (argument_count > 1 && std::string_view(arguments[1]) == "--port") {
        while (true) {
            std::this_thread::sleep_for(std::chrono::hours(1));
        }
    }
    workbench_executable = std::filesystem::absolute(arguments[0]);
    try {
        test_unified_routing_profiles_multimodal_and_artifacts();
        test_unified_cancellation();
        test_pack_transaction_rollback_and_selection_isolation();
        test_explicit_trainer_install_keeps_native_serving_pack();
        test_backend_requirements_report_exact_missing_files();
        test_native_host_launch_never_installs_bundled_pack();
        test_backend_startup_accessors_remain_responsive_while_cancelling();
        test_degraded_native_health_stays_labeled_as_foundation();
        test_pack_audit_reports_exact_files_licenses_roles_and_weights();
        test_persistent_serialized_training_resume_and_coalescing();
        test_persistent_queue_survives_concurrent_readers();
        test_persistent_queue_outwaits_transient_windows_reader();
        test_service_recovers_serialized_job_before_candidate_creation();
        test_service_reconciles_one_durable_job_per_interrupted_candidate();
        test_service_recovers_interrupted_internal_promotion_transaction();
        test_atomic_promotion_keeps_working_host_until_candidate_passes();
        test_storage_cards_growth_projections_quotas_and_cleanup();
        test_safe_reset_path_validation();
        test_repository_candidate_promotion_isolated_and_stale_safe();
        test_named_module_and_native_base_contract();
        std::cout << "All EngCoder unified workbench tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Workbench test failure: " << exception.what() << '\n';
        return 1;
    }
}
