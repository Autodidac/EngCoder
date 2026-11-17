#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "engcoder/agent/agent_runtime.hpp"
#include "engcoder/core/config.hpp"
#include "engcoder/core/event.hpp"
#include "engcoder/decision/decision_engine.hpp"
#include "engcoder/model/model_provider.hpp"
#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/controller/architect_controller.hpp"
#include "engcoder/models/model_registry.hpp"
#include "engcoder/residency/model_residency_manager.hpp"
#include "engcoder/training/training_service.hpp"
#include "engcoder/learning/learning_service.hpp"
#include "engcoder/persistence/session_store.hpp"
#include "engcoder/tools/tool_registry.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

class IterationModel final : public engcoder::model::IModelProvider {
public:
    unsigned turns{};
    bool boundary{};
    bool expect_memory{};
    bool stronger_available{true};
    std::vector<std::string> models;
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        for (const auto& tool : request.tools) {
            CHECK(tool.at("function").at("name") != "process.run");
        }
        const auto turn = turns++;
        models.push_back(request.model);
        if (turn == 0 && boundary) {
            CHECK((request.messages.front().content.find("Previously verified C++ boundary lesson") != std::string::npos)
                == expect_memory);
        }
        if (turn == 2) {
            CHECK(request.messages.back().content.find(boundary ? "remaining capacity" : "missing fixture symbol") != std::string::npos);
        }
        if (turn % 2 == 0) {
            const std::string prefix = turn == 0 ? "original" : "edited " + std::to_string(turn / 2);
            return engcoder::model::ModelResponse{.tool_calls = {
                {"unexpected-" + std::to_string(turn), "process.run", {{"executable", "ctest"}}},
                {"protected-" + std::to_string(turn), "workspace.patch",
                    {{"path", boundary ? "tests.cpp" : "build/test.cpp"}, {"expected", "old"}, {"replacement", "new"}}},
                {"overwrite-" + std::to_string(turn), "workspace.write",
                    {{"path", "fixture.cpp"}, {"content", "unreviewed whole-file overwrite"}}},
                {"empty-" + std::to_string(turn), "workspace.patch",
                    {{"path", "fixture.cpp"}, {"expected", ""}, {"replacement", "unused"}}},
                {"missing-" + std::to_string(turn), "workspace.patch",
                    {{"path", "fixture.cpp"}, {"expected", "missing before approval"}, {"replacement", "unused"}}},
                {"ambiguous-" + std::to_string(turn), "workspace.patch",
                    {{"path", "fixture.cpp"}, {"expected", "aaa"}, {"replacement", "unused"}}},
                {"unchanged-" + std::to_string(turn), "workspace.patch",
                    {{"path", "fixture.cpp"}, {"expected", "aaaa\nfooter"}, {"replacement", "aaaa\r\nfooter"}}},
                {"edit-" + std::to_string(turn), "workspace.patch",
                    {{"path", "fixture.cpp"}, {"expected", prefix + "\naaaa\nfooter"},
                        {"replacement_lines", engcoder::core::json::array({
                            "edited " + std::to_string(turn / 2 + 1), "aaaa", "footer"})}}},
            }};
        }
        return engcoder::model::ModelResponse{.text = "Candidate change summary"};
    }

    engcoder::model::ProviderCapabilities capabilities() const noexcept override {
        return {.custom_tools = true};
    }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}, {"models", engcoder::model::json::array({
            {{"id", "stronger-fixture"}, {"loaded", stronger_available}}
        })}};
    }
};

class FakeModelProvider final : public engcoder::model::IModelProvider {
public:
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        last_request = request;
        return engcoder::model::ModelResponse{
            .text = "runtime-ok",
            .tool_calls = {},
            .raw = engcoder::model::json::object(),
            .model = "fake-physical-model",
        };
    }

    std::optional<engcoder::model::ModelRequest> last_request;

    engcoder::model::ProviderCapabilities capabilities() const noexcept override {
        return {
            .streaming = true,
            .custom_tools = true,
            .stateful_chat = false,
            .remote_mcp = false,
            .native_model_management = false,
        };
    }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{
            {"reachable", true},
            {"backend", "fake"},
            {"selected_model", "fake-model"},
        };
    }
};


class AssignedModelProvider final : public engcoder::model::IModelProvider {
public:
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        last_request = request;
        return engcoder::model::ModelResponse{
            .text = "assigned-model-ok",
            .tool_calls = {},
            .raw = engcoder::model::json::object(),
            .model = request.model,
        };
    }

    std::expected<engcoder::model::json, engcoder::core::Error> prepare(std::string_view model) override {
        requested_prepare_model = std::string{model};
        if (prepare_error) return std::unexpected(*prepare_error);
        return engcoder::model::json{
            {"model", "qwenflash://native-test"},
            {"effective_context_tokens", 81920},
            {"context_reserve_tokens", 4096},
            {"auto_compact", true},
            {"compact_trigger_ratio", 0.97},
            {"compact_target_ratio", 0.72},
        };
    }

    engcoder::model::ProviderCapabilities capabilities() const noexcept override {
        return {
            .streaming = true,
            .custom_tools = true,
            .stateful_chat = false,
            .remote_mcp = false,
            .native_model_management = true,
        };
    }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}};
    }

    std::optional<engcoder::model::ModelRequest> last_request;
    std::optional<engcoder::core::Error> prepare_error;
    std::string requested_prepare_model;
};
class ToolCallingModelProvider final : public engcoder::model::IModelProvider {
public:
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest&) override {
        if (call_count_++ == 0) {
            return engcoder::model::ModelResponse{
                .text = {},
                .tool_calls = {{
                    .id = "call-1",
                    .name = "workspace.read",
                    .arguments = {{"path", "README.md"}},
                }},
                .raw = engcoder::model::json::object(),
            };
        }

        return engcoder::model::ModelResponse{
            .text = "tool-cycle-ok",
            .tool_calls = {},
            .raw = engcoder::model::json::object(),
        };
    }

    engcoder::model::ProviderCapabilities capabilities() const noexcept override {
        return {
            .streaming = true,
            .custom_tools = true,
            .stateful_chat = false,
            .remote_mcp = false,
            .native_model_management = false,
        };
    }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}, {"backend", "fake-tools"}};
    }

private:
    std::uint32_t call_count_{};
};

class ApprovalToolModelProvider final : public engcoder::model::IModelProvider {
public:
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest&) override {
        if (const auto turn = call_count_++; turn < 2) {
            return engcoder::model::ModelResponse{
                .text = {},
                .tool_calls = {{
                    .id = "approval-call-1",
                    .name = "process.run",
                    .arguments = {
                        {"executable", "custom-check"},
                        {"arguments", engcoder::core::json::array({std::to_string(turn)})},
                        {"cwd", "."},
                    },
                }},
                .raw = engcoder::model::json::object(),
            };
        }

        return engcoder::model::ModelResponse{
            .text = "approval-cycle-ok",
            .tool_calls = {},
            .raw = engcoder::model::json::object(),
        };
    }

    engcoder::model::ProviderCapabilities capabilities() const noexcept override {
        return {
            .streaming = true,
            .custom_tools = true,
            .stateful_chat = false,
            .remote_mcp = false,
            .native_model_management = false,
        };
    }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}, {"backend", "fake-approval"}};
    }

private:
    std::uint32_t call_count_{};
};

[[nodiscard]] engcoder::tools::Tool fake_read_tool() {
    return {
        .definition = {
            .name = "workspace.read",
            .description = "test read",
            .parameters = {
                {"type", "object"},
                {"properties", {{"path", {{"type", "string"}}}}},
                {"required", {"path"}},
            },
        },
        .handler = [](const engcoder::tools::json&, const engcoder::tools::Context&)
            -> std::expected<engcoder::tools::json, engcoder::core::Error> {
            return engcoder::tools::json{{"content", "fake-read"}};
        },
    };
}

[[nodiscard]] engcoder::tools::Tool fake_process_tool(bool& invoked) {
    return {
        .definition = {
            .name = "process.run",
            .description = "test process",
            .parameters = {
                {"type", "object"},
                {"properties", {{"executable", {{"type", "string"}}}}},
                {"required", {"executable"}},
            },
        },
        .handler = [&invoked](const engcoder::tools::json&, const engcoder::tools::Context&)
            -> std::expected<engcoder::tools::json, engcoder::core::Error> {
            invoked = true;
            return engcoder::tools::json{{"exit_code", 0}};
        },
    };
}

} // namespace

TEST_CASE("agent runtime persists streamed text and terminal completion") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-agent-runtime-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");

    {
        engcoder::core::Config config;
        config.workspace = root;
        config.max_active_context_chars = 32'000;
        config.max_tool_output_chars = 8'000;

        config.data_root = root / ".engcoder";
        config.model = "fake-model";
        FakeModelProvider model;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, model, models);
        engcoder::controller::ArchitectController controller(config, model, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, model, addons, controller, models, residency, training, learning, tools, store, events);

        const engcoder::agent::TaskRequest request{
            .project_id = "test-project",
            .session_id = "test-session",
            .task_id = "test-task",
            .user_message = "hello",
            .foundation_model = {},
            .controller_model = {},
            .forced_addon = {},
            .session_instructions = "Keep the session answer concise.",
            .temperature = 0.42,
            .max_tokens = 123,
            .include_history = false,
            .max_history_turns = 0,
            .auto_route = false,
        };

        const auto result = runtime.run(request);
        REQUIRE(result);
        REQUIRE(result->final_text == "runtime-ok");
        REQUIRE(result->phase == engcoder::agent::TaskPhase::completed);
        REQUIRE(model.last_request.has_value());
        REQUIRE(model.last_request->temperature == 0.42);
        REQUIRE(model.last_request->max_tokens == 123);
        REQUIRE(model.last_request->messages.size() == 2);
        REQUIRE(model.last_request->messages.front().role == "system");
        REQUIRE(model.last_request->messages.front().content.find("Keep the session answer concise.") != std::string::npos);
        REQUIRE(model.last_request->messages.back().role == "user");

        const auto persisted = store.events_after(request.session_id, 0);
        REQUIRE(persisted);

        const auto has_delta = std::ranges::any_of(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "model.text_delta" && event.data.value("delta", std::string{}) == "runtime-ok";
        });
        const auto has_completion = std::ranges::any_of(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "task.completed" && event.data.value("result", std::string{}) == "runtime-ok";
        });
        const auto has_model_assignment = std::ranges::any_of(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "model.response" &&
                event.data.value("model", std::string{}) == "fake-physical-model" &&
                event.data.value("requested_model", std::string{}) == "fake-model";
        });
        const auto has_non_model_route = std::ranges::any_of(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "controller.decision" &&
                event.data.value("source", std::string{}) == "session-core-only" &&
                !event.data.value("model_attempted", true) &&
                !event.data.value("model_used", true);
        });

        REQUIRE(has_delta);
        REQUIRE(has_completion);
        REQUIRE(has_model_assignment);
        REQUIRE(has_non_model_route);
        // The disabled-router path is an intentional, single audit decision,
        // not a second fallback pass or an omitted controller event.
        REQUIRE(std::ranges::count_if(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "controller.decision";
        }) == 1);
    }

    std::filesystem::remove_all(root);
}

TEST_CASE("learning provenance excludes completed candidate runs") {
    using namespace engcoder;
    const auto root = std::filesystem::temp_directory_path() / "engcoder-learning-provenance";
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    } cleanup{root};
    persistence::SessionStore store(root / "provenance.db");
    const auto append = [&](std::uint64_t seq, std::string task, std::string type, core::json data) {
        REQUIRE(store.append_event({.sequence = seq, .session_id = "shared",
            .task_id = std::move(task), .type = std::move(type), .data = std::move(data)}));
    };
    append(1, "normal", "task.completed", {{"result", "Normal reply"}});
    append(2, "candidate", "iteration.started", {{"repair_limit", 2}});
    append(3, "candidate", "task.completed", {{"result", "Candidate repair summary"}});
    append(4, "failed", "task.failed", {{"error", "build failed"}});
    const auto normal = store.learnable_reply("shared", "normal");
    REQUIRE(normal);
    CHECK(*normal == "Normal reply");
    for (const auto* task : {"candidate", "failed", "missing"}) {
        const auto result = store.learnable_reply("shared", task);
        REQUIRE(result);
        CHECK_FALSE(result->has_value());
    }
    const auto wrong_session = store.learnable_reply("other", "normal");
    REQUIRE(wrong_session);
    CHECK_FALSE(wrong_session->has_value());
}

TEST_CASE("only the task host reconciles interrupted task events once") {
    using namespace engcoder;
    const auto root = std::filesystem::temp_directory_path() / "engcoder-task-restart-recovery";
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    const auto path = root / "events.sqlite";
    const auto append = [&](persistence::SessionStore& store, std::uint64_t sequence,
        std::string task, std::string type) {
        REQUIRE(store.append_event({.sequence = sequence, .project_id = "default", .session_id = "session",
            .task_id = std::move(task), .type = std::move(type)}));
    };
    {
        persistence::SessionStore host(path);
        REQUIRE(host.claim_task_host());
        append(host, 1, "pending", "task.queued");
        append(host, 2, "pending", "task.created");
        append(host, 3, "pending", "task.awaiting_approval");
        append(host, 4, "never-started", "task.queued");
        append(host, 5, "legacy", "task.created");
        append(host, 6, "finished", "task.created");
        append(host, 7, "finished", "task.completed");
        append(host, 8, "cancelled", "task.queued");
        append(host, 9, "cancelled", "task.cancelled");
        append(host, 10, "failed", "task.queued");
        append(host, 11, "failed", "task.failed");

        persistence::SessionStore other(path);
        const auto refused = other.claim_task_host();
        REQUIRE_FALSE(refused);
        CHECK(refused.error().message.find("Another EngCoder task host") != std::string::npos);
        CHECK(other.max_event_sequence() == 11);
    }
    {
        persistence::SessionStore restarted(path);
        REQUIRE(restarted.claim_task_host());
        const auto events = restarted.events_after("session", 11);
        REQUIRE(events);
        REQUIRE(events->size() == 3);
        const std::vector<std::string> tasks{"pending", "never-started", "legacy"};
        for (std::size_t index = 0; index < events->size(); ++index) {
            const auto& event = events->at(index);
            CHECK(event.sequence == 12 + index);
            CHECK(event.task_id == tasks.at(index));
            CHECK(event.type == "task.failed");
            CHECK(event.data.at("reason") == "runtime_restart");
        }
    }
    persistence::SessionStore reopened(path);
    REQUIRE(reopened.claim_task_host());
    CHECK(reopened.max_event_sequence() == 14);
}

TEST_CASE("candidate iteration requires approved edits and native validation") {
    using namespace engcoder;
    const auto root = std::filesystem::temp_directory_path() / "engcoder-candidate-iteration";
    std::filesystem::create_directories(root / "build");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); }
    } cleanup{root};

    core::Config config;
    config.workspace = root;
    config.iteration_candidate = root;
    config.iteration_build_dir = "build";
    config.data_root = root / ".engcoder";
    config.model = "fixture-model";
    config.training_enabled = false;
    const bool foreign_source = GENERATE(false, true);
    const auto source = foreign_source ? root.parent_path() : root;
    std::ofstream(root / "build" / "CMakeCache.txt") << "CMAKE_HOME_DIRECTORY:INTERNAL=" << source.generic_string() << '\n';
    const bool deny = GENERATE(false, true);
    const bool keep_failing = GENERATE(false, true);
    const bool boundary = GENERATE(false, true);
    const bool stronger_available = GENERATE(false, true);
    const bool changed_while_pending = GENERATE(false, true);
    const auto reset_candidate = [&] {
        std::ofstream(root / "fixture.cpp", std::ios::binary) << "original\r\naaaa\r\nfooter\r\n";
    };
    reset_candidate();
    if (boundary) {
        config.iteration_boundary_source = "fixture.cpp";
        config.iteration_boundary_rule = "capacity";
        config.iteration_escalation_model = "stronger-fixture";
        std::ofstream(root / "tests.cpp") << "unchanged";
    }

    IterationModel model;
    model.boundary = boundary;
    model.stronger_available = stronger_available;
    decision::DeterministicDecisionEngine decisions;
    models::ModelRegistry models(config, config.data_root);
    residency::ModelResidencyManager residency;
    addons::AddonRegistry addons(config, model, models);
    controller::ArchitectController controller(config, model, addons, models, residency);
    training::TrainingService training(config, config.data_root, models, residency);
    learning::LearningService learning(config.data_root);
    tools::ToolRegistry tools(decisions);
    unsigned edits = 0;
    std::vector<std::string> checks;
    tools.add({{"workspace.patch", "fixture edit", core::json::object()},
        [&](const core::json& args, const tools::Context&) -> std::expected<core::json, core::Error> {
            ++edits;
            CHECK(args.at("path") == "fixture.cpp");
            std::ofstream(root / "fixture.cpp", std::ios::binary) << "edited " << edits << "\r\naaaa\r\nfooter\r\n";
            return core::json{{"ok", true}, {"checkpoint_id", "fixture"}};
        }});
    tools.add({{"process.run", "fixture process", core::json::object()},
        [&](const core::json& args, const tools::Context&) -> std::expected<core::json, core::Error> {
            const auto command = args.at("executable").get<std::string>();
            checks.push_back(command);
            if (command == "ctest") CHECK(args.at("arguments").back() == "--no-tests=error");
            if (boundary) {
                const bool fails = command == "ctest" && (checks.size() <= 4 || keep_failing);
                return core::json{{"exit_code", fails ? 8 : 0}, {"timed_out", false}, {"cancelled", false},
                    {"stdout", fails ? "Test #1: capacity .... ***Failed\nremaining capacity" : "passed"}};
            }
            return core::json{{"exit_code", checks.size() == 1 || keep_failing ? 1 : 0},
                {"timed_out", false}, {"cancelled", false}, {"stderr", "missing fixture symbol"}};
        }});
    persistence::SessionStore store(config.data_root / "iteration.db");
    core::EventBus events;
    agent::AgentRuntime runtime(config, model, addons, controller, models, residency, training, learning, tools, store, events);
    unsigned recalled = 0;
    const auto subscription = events.subscribe([&](const core::Event& event) {
        if (event.type == "task.awaiting_approval") {
            CHECK(event.data.at("tool") == "workspace.patch");
            CHECK(event.data.at("call_id").get<std::string>().starts_with("approval_"));
            CHECK(event.data.at("tool_call_id").get<std::string>().starts_with("edit-"));
            const auto& arguments = event.data.at("arguments");
            CHECK_FALSE(arguments.contains("replacement_lines"));
            CHECK(arguments.at("replacement").get<std::string>().ends_with("\naaaa\nfooter"));
            if (changed_while_pending) std::ofstream(root / "fixture.cpp") << "newer operator content";
            CHECK(runtime.resolve_approval(event.task_id, event.data.at("call_id").get<std::string>(), !deny));
        }
        if (event.type == "iteration.lesson_reused") ++recalled;
        if ((event.type == "tool.approved" || event.type == "tool.denied" || event.type == "tool.completed") &&
            event.data.value("name", std::string{}) == "workspace.patch") {
            CHECK(event.data.at("candidate_path") == "fixture.cpp");
        }
    });
    const auto result = runtime.run({.project_id = "candidate", .session_id = "iteration",
        .task_id = "iteration-task", .user_message = "Update the fixture source.",
        .include_history = false, .wait_for_approval = true, .self_iteration = true});
    if (foreign_source || deny) {
        CHECK_FALSE(result);
        CHECK(edits == 0);
        if (boundary && !foreign_source) CHECK(checks == std::vector<std::string>{"cmake", "ctest"});
        else CHECK(checks.empty());
        CHECK(model.turns == (foreign_source ? 0u : 2u));
    } else if (changed_while_pending) {
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::make_error_code(core::Errc::workspace_violation));
        CHECK(result.error().message.find("changed while approval was pending") != std::string::npos);
        CHECK(edits == 0);
        std::string content;
        std::getline(std::ifstream(root / "fixture.cpp"), content);
        CHECK(content == "newer operator content");
    } else if (keep_failing) {
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::make_error_code(core::Errc::iteration_limit));
        CHECK(edits == 3);
        if (boundary) CHECK(checks.size() == 8);
        else CHECK(checks == std::vector<std::string>{"cmake", "cmake", "cmake"});
    } else {
        REQUIRE(result);
        CHECK(result->final_text == "Candidate change summary");
        CHECK(edits == 2);
        if (boundary) {
            CHECK(checks == std::vector<std::string>{"cmake", "ctest", "cmake", "ctest", "cmake", "ctest"});
            const auto recent = learning.recent().at("items");
            REQUIRE(recent.size() == 1);
            CHECK(recent[0].at("deterministic_verified") == true);
            CHECK(recent[0].at("eligible_for_training") == false);
        } else CHECK(checks == std::vector<std::string>{"cmake", "cmake", "ctest"});
    }
    if (boundary && !foreign_source && !deny && !changed_while_pending) {
        REQUIRE(model.models.size() >= 4);
        CHECK(model.models[0] == "fixture-model");
        CHECK(model.models[1] == "fixture-model");
        CHECK(model.models[2] == (stronger_available ? "stronger-fixture" : "fixture-model"));
    }
    if (!result) CHECK(learning.recent().at("items").empty());
    if (result && boundary) {
        reset_candidate();
        model.turns = 0;
        model.expect_memory = true;
        edits = 0;
        checks.clear();
        const auto repeated = runtime.run({.project_id = "candidate", .session_id = "iteration-second",
            .task_id = "iteration-repeat", .user_message = "Check a second boundary fixture.",
            .include_history = false, .wait_for_approval = true, .self_iteration = true});
        REQUIRE(repeated);
        CHECK(recalled == 1);
        CHECK(learning.recent().at("items").size() == 1);
    }
    events.unsubscribe(subscription);
}

TEST_CASE("agent runtime emits weighted tool review telemetry before execution") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-agent-tool-review-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");

    {
        engcoder::core::Config config;
        config.workspace = root;
        config.max_active_context_chars = 32'000;
        config.max_tool_output_chars = 8'000;
        config.max_model_turns = 4;

        config.data_root = root / ".engcoder";
        config.model = "fake-model";
        ToolCallingModelProvider model;
        engcoder::decision::WeightedDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, model, models);
        engcoder::controller::ArchitectController controller(config, model, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        tools.add(fake_read_tool());
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, model, addons, controller, models, residency, training, learning, tools, store, events);

        const engcoder::agent::TaskRequest request{
            .project_id = "test-project",
            .session_id = "tool-review-session",
            .task_id = "tool-review-task",
            .user_message = "read the readme",
            .foundation_model = {},
            .controller_model = {},
            .forced_addon = {},
        };

        const auto result = runtime.run(request);
        REQUIRE(result);
        REQUIRE(result->final_text == "tool-cycle-ok");

        const auto persisted = store.events_after(request.session_id, 0);
        REQUIRE(persisted);

        const auto review_it = std::ranges::find_if(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "tool.reviewed";
        });
        REQUIRE(review_it != persisted->end());
        REQUIRE(review_it->data.value("name", std::string{}) == "workspace.read");
        REQUIRE(review_it->data.value("reviewer", std::string{}) == "local-weighted");
        REQUIRE(review_it->data.value("disposition", std::string{}) == "allow");
        REQUIRE(review_it->data.contains("probabilities"));
        REQUIRE(review_it->data.at("probabilities").value("allow", 0.0) > 0.5);

        const auto completed_it = std::ranges::find_if(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "tool.completed";
        });
        REQUIRE(completed_it != persisted->end());
        REQUIRE(review_it->sequence < completed_it->sequence);
    }

    std::filesystem::remove_all(root);
}


TEST_CASE("agent runtime executes only a timely interactive approval for the exact call") {
    const bool expire = GENERATE(false, true);
    const auto root = std::filesystem::temp_directory_path() / "engcoder-agent-approval-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};

    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "fake-model";
        config.addon_auto_route = false;
        config.max_model_turns = 4;
        if (expire) config.tool_approval_timeout = std::chrono::milliseconds{25};

        ApprovalToolModelProvider model;
        engcoder::decision::WeightedDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, model, models);
        engcoder::controller::ArchitectController controller(config, model, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        bool invoked = false;
        tools.add(fake_process_tool(invoked));
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, model, addons, controller, models, residency, training, learning, tools, store, events);

        std::vector<std::string> approval_ids;
        const auto subscription = events.subscribe([&](const engcoder::core::Event& event) {
            if (event.type != "task.awaiting_approval") return;
            const auto id = event.data.at("call_id").get<std::string>();
            CHECK(event.data.at("tool_call_id") == "approval-call-1");
            if (expire) {
                approval_ids.push_back(id);
                return;
            }
            CHECK_FALSE(runtime.resolve_approval(event.task_id, "approval-call-1", true));
            if (!approval_ids.empty()) {
                CHECK(id != approval_ids.front());
                CHECK_FALSE(runtime.resolve_approval(event.task_id, approval_ids.front(), true));
            }
            CHECK(runtime.resolve_approval(event.task_id, id, true));
            CHECK_FALSE(runtime.resolve_approval(event.task_id, id, true));
            approval_ids.push_back(id);
        });

        const engcoder::agent::TaskRequest request{
            .project_id = "test-project",
            .session_id = "approval-session",
            .task_id = "approval-task",
            .user_message = "run the check",
            .foundation_model = {},
            .controller_model = {},
            .forced_addon = {},
            .addon_models = engcoder::core::json::object(),
            .session_instructions = {},
            .temperature = 0.15,
            .max_tokens = -1,
            .include_history = false,
            .max_history_turns = 0,
            .auto_route = false,
            .wait_for_approval = true,
        };

        const auto result = runtime.run(request);
        events.unsubscribe(subscription);

        if (expire) {
            REQUIRE_FALSE(result);
            CHECK(result.error().message.find("Tool approval expired") != std::string::npos);
            REQUIRE(approval_ids.size() == 1);
            CHECK_FALSE(invoked);
            CHECK_FALSE(runtime.resolve_approval(request.task_id, approval_ids.front(), true));
            const auto receipts = store.events_after(request.session_id, 0);
            REQUIRE(receipts);
            REQUIRE_FALSE(receipts->empty());
            CHECK(receipts->back().type == "task.failed");
            CHECK(receipts->back().data.at("approval_id") == approval_ids.front());
            return;
        }

        REQUIRE(result);
        CHECK(result->final_text == "approval-cycle-ok");
        REQUIRE(approval_ids.size() == 2);
        CHECK(invoked);

        const auto persisted = store.events_after(request.session_id, 0);
        REQUIRE(persisted);
        const auto approved = std::ranges::find_if(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "tool.approved" && event.data.value("id", std::string{}) == "approval-call-1";
        });
        const auto completed = std::ranges::find_if(*persisted, [](const engcoder::core::Event& event) {
            return event.type == "tool.completed" && event.data.value("id", std::string{}) == "approval-call-1";
        });
        REQUIRE(approved != persisted->end());
        REQUIRE(completed != persisted->end());
        CHECK(approved->sequence < completed->sequence);
        CHECK(approved->data.at("approval_id") == approval_ids.front());
    }

}


TEST_CASE("agent runtime adopts the model assigned by provider preparation") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-agent-assigned-model-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");

    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "auto";
        config.addon_auto_route = false;

        AssignedModelProvider model;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, model, models);
        engcoder::controller::ArchitectController controller(config, model, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, model, addons, controller, models, residency, training, learning, tools, store, events);

        const engcoder::agent::TaskRequest request{
            .project_id = "test-project",
            .session_id = "assigned-model-session",
            .task_id = "assigned-model-task",
            .user_message = "hello native model",
            .foundation_model = {},
            .controller_model = {},
            .forced_addon = {},
            .addon_models = engcoder::core::json::object(),
            .session_instructions = {},
            .temperature = 0.15,
            .max_tokens = -1,
            .include_history = false,
            .max_history_turns = 0,
            .auto_route = false,
        };

        SECTION("uses the assigned model for generation") {
            const auto result = runtime.run(request);
            REQUIRE(result);
            REQUIRE(result->final_text == "assigned-model-ok");
            REQUIRE(model.requested_prepare_model == "auto");
            REQUIRE(model.last_request.has_value());
            CHECK(model.last_request->model == "qwenflash://native-test");

            const auto persisted = store.events_after(request.session_id, 0);
            REQUIRE(persisted);
            const auto request_event = std::ranges::find_if(*persisted, [](const engcoder::core::Event& event) {
                return event.type == "model.request";
            });
            REQUIRE(request_event != persisted->end());
            CHECK(request_event->data.value("model", std::string{}) == "qwenflash://native-test");
        }

        SECTION("cancellation before execution persists exactly one terminal event") {
            std::stop_source stopped;
            stopped.request_stop();
            const auto result = runtime.run(request, stopped.get_token());
            REQUIRE_FALSE(result);
            CHECK(result.error().code == engcoder::core::make_error_code(engcoder::core::Errc::cancelled));
            CHECK(model.requested_prepare_model.empty());
            CHECK_FALSE(model.last_request);
            const auto persisted = store.events_after(request.session_id, 0);
            REQUIRE(persisted);
            REQUIRE(persisted->size() == 1);
            CHECK(persisted->front().task_id == request.task_id);
            CHECK(persisted->front().type == "task.cancelled");
        }

        SECTION("preparation failure persists the original error and terminal event") {
            model.prepare_error = engcoder::core::Error{
                engcoder::core::make_error_code(engcoder::core::Errc::model_generation_error), "fixture cannot prepare model"};
            const auto result = runtime.run(request);
            REQUIRE_FALSE(result);
            CHECK(result.error().message == model.prepare_error->message);
            CHECK_FALSE(model.last_request);
            const auto persisted = store.events_after(request.session_id, 0);
            REQUIRE(persisted);
            REQUIRE(persisted->size() >= 2);
            CHECK((*persisted)[persisted->size() - 2].type == "residency.failed");
            CHECK(persisted->back().type == "task.failed");
            CHECK(persisted->back().data.value("error", std::string{}) == model.prepare_error->message);
            CHECK(std::ranges::count_if(*persisted, [](const engcoder::core::Event& event) {
                return event.type == "task.failed" || event.type == "task.cancelled" || event.type == "task.completed";
            }) == 1);
        }
    }

    std::filesystem::remove_all(root);
}


TEST_CASE("automatic small responder handles greetings without addon routing or tool authority") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-agent-simple-route-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "foundation-test";
        config.addon_auto_route = true;
        FakeModelProvider model;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, model, models);
        engcoder::controller::ArchitectController architect(config, model, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, model, addons, architect, models, residency, training, learning, tools, store, events);

        engcoder::agent::TaskRequest request{
            .project_id = "test-project",
            .session_id = "simple-route-session",
            .task_id = "simple-route-hi",
            .user_message = "hi",
            .foundation_model = "foundation-test",
            .simple_model = "lmstudio-small-test",
            .auto_route = true,
            .addons_enabled = false,
        };
        const auto greeting = runtime.run(request);
        REQUIRE(greeting);
        REQUIRE(model.last_request);
        CHECK(model.last_request->model == "lmstudio-small-test");
        CHECK(model.last_request->tools.empty());
        CHECK(model.last_request->tool_choice == "none");
        const auto events_after_greeting = store.events_after(request.session_id, 0);
        REQUIRE(events_after_greeting);
        CHECK(std::ranges::any_of(*events_after_greeting, [](const engcoder::core::Event& event) {
            return event.type == "controller.model_selected" &&
                event.data.value("role", std::string{}) == "small-response";
        }));

        // Disabling the advisor subsystem must not disable simple-response routing.
        // But coding must always remain with Foundation and its normal tool policy.
        request.task_id = "simple-route-code";
        request.user_message = "Refactor the Vulkan renderer and run tests";
        const auto coding = runtime.run(request);
        REQUIRE(coding);
        REQUIRE(model.last_request);
        CHECK(model.last_request->model == "foundation-test");
        const auto events_after_coding = store.events_after(request.session_id, 0);
        REQUIRE(events_after_coding);
        CHECK(std::ranges::any_of(*events_after_coding, [](const engcoder::core::Event& event) {
            return event.type == "controller.model_selected" &&
                event.data.value("role", std::string{}) == "keymaker-foundation";
        }));
    }
    std::filesystem::remove_all(root);
}


namespace {
class ThreeTierProvider final : public engcoder::model::IModelProvider {
public:
    std::vector<engcoder::model::ModelRequest> seen;
    bool reviewer_fails = false;
    bool small_returns_code = false;
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        seen.push_back(request);
        if (request.model == "reviewer-test" && reviewer_fails) {
            return std::unexpected(engcoder::core::Error{
                engcoder::core::make_error_code(engcoder::core::Errc::model_unavailable),
                "reviewer unavailable"});
        }
        const std::string result = request.model == "specialist-test" ?
            (small_returns_code ? "```cpp\nint main() { return 0; }\n```" :
                R"({"handoff":["specialist-advice: inspect existing ownership before changing concurrency"]})") :
            request.model == "reviewer-test" ?
                R"({"handoff":["middle-critique: confirm the ownership changes preserve thread safety"]})" : "qwen-final";
        return engcoder::model::ModelResponse{.text = result, .model = request.model};
    }
    engcoder::model::ProviderCapabilities capabilities() const noexcept override { return {}; }
    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}};
    }
};
} // namespace

TEST_CASE("three-tier routing bounds specialists and reviewer before Qwen") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-three-tier-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "foundation-test";
        config.addon_auto_route = true;
        config.addons.push_back(engcoder::core::AddonConfig{
            .id = "cpp", .display_name = "C++", .model = "specialist-test",
            .purpose = "code review", .system_prompt = "review source", .triggers = {"c++"},
        });
        ThreeTierProvider provider;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, provider, models);
        engcoder::controller::ArchitectController architect(config, provider, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, provider, addons, architect, models, residency, training, learning, tools, store, events);
        engcoder::agent::TaskRequest request{
            .project_id = "three-tier-test", .session_id = "hierarchy-session",
            .task_id = "hierarchy-task-1", .user_message = "Review C++ mutex usage",
            .foundation_model = "foundation-test", .review_model = "reviewer-test",
            .review_mode = "auto", .forced_addon = "cpp", .addons_enabled = true,
        };
        const auto result = runtime.run(request);
        REQUIRE(result);
        CHECK(result->final_text == "qwen-final");
        REQUIRE(provider.seen.size() == 3);
        CHECK(provider.seen[0].model == "specialist-test");
        CHECK(provider.seen[0].max_tokens == 512);
        CHECK(provider.seen[0].tools.empty());
        CHECK(provider.seen[1].model == "reviewer-test");
        CHECK(provider.seen[1].max_tokens == 512);
        CHECK(provider.seen[1].tools.empty());
        CHECK(provider.seen[1].tool_choice == "none");
        CHECK(provider.seen[1].messages.back().content.find("specialist-advice") != std::string::npos);
        CHECK(provider.seen[2].model == "foundation-test");
        CHECK(provider.seen[2].messages.back().content.find("middle-critique") != std::string::npos);
        const auto entries = store.events_after(request.session_id, 0);
        REQUIRE(entries);
        CHECK(std::ranges::any_of(*entries, [](const auto& event) {
            return event.type == "review.completed";
        }));

        provider.seen.clear();
        request.task_id = "hierarchy-task-2";
        request.review_mode = "off";
        const auto without = runtime.run(request);
        REQUIRE(without);
        REQUIRE(provider.seen.size() == 2);
        CHECK(provider.seen[0].model == "specialist-test");
        CHECK(provider.seen[1].model == "foundation-test");

        provider.seen.clear();
        provider.reviewer_fails = true;
        request.task_id = "hierarchy-task-3";
        request.review_mode = "deep";
        const auto fallback = runtime.run(request);
        REQUIRE(fallback);
        CHECK(fallback->final_text == "qwen-final");
        REQUIRE(provider.seen.size() == 3);
        CHECK(provider.seen[1].max_tokens == 1024);
        CHECK(provider.seen[2].model == "foundation-test");
        const auto failures = store.events_after(request.session_id, 0);
        REQUIRE(failures);
        CHECK(std::ranges::any_of(*failures, [](const auto& event) {
            return event.type == "review.failed";
        }));

        // An algebra/modulo problem with symbolic TeX is not a checklist
        // assignment for the 9B model: KeyMaker is the only answering model.
        provider.seen.clear();
        request.task_id = "hierarchy-math-bypass";
        request.user_message = "An integer X=1 is repeatedly updated by (5X+3) mod 7 for 1,000,000 seconds";
        request.forced_addon.clear();
        request.auto_route = true;
        request.simple_model = "specialist-test";
        request.review_model = "reviewer-test";
        const auto math_result = runtime.run(request);
        REQUIRE(math_result);
        REQUIRE(provider.seen.size() == 1);
        CHECK(provider.seen.front().model == "foundation-test");
        const auto math_events = store.events_after(request.session_id, 0);
        REQUIRE(math_events);
        CHECK_FALSE(std::ranges::any_of(*math_events, [](const auto& event) {
            return event.task_id == "hierarchy-math-bypass" &&
                (event.type == "handoff.draft_requested" || event.type == "review.requested");
        }));
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("unassigned C++ specialist reuses small responder and middle reviews before KeyMaker") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-small-specialist-fallback-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "foundation-test";
        config.addon_auto_route = true;
        config.addons.push_back(engcoder::core::AddonConfig{
            .id = "cpp", .display_name = "C++ specialist", .model = {},
            .purpose = "limited checking", .triggers = {"not-a-match"},
        });
        ThreeTierProvider provider;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, provider, models);
        engcoder::controller::ArchitectController architect(config, provider, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, provider, addons, architect, models, residency, training, learning, tools, store, events);
        engcoder::agent::TaskRequest request{
            .project_id = "specialist-fallback", .session_id = "specialist-session",
            .task_id = "specialist-task", .user_message = "Refactor the Vulkan renderer for multithreading",
            .foundation_model = "foundation-test", .simple_model = "specialist-test",
            .review_model = "reviewer-test", .review_mode = "auto",
            .auto_route = true, .addons_enabled = true,
        };
        const auto answer = runtime.run(request);
        REQUIRE(answer);
        CHECK(answer->final_text == "qwen-final");
        REQUIRE(provider.seen.size() == 3);
        CHECK(provider.seen[0].model == "specialist-test");
        CHECK(provider.seen[0].tool_choice == "none");
        CHECK(provider.seen[0].tools.empty());
        CHECK(provider.seen[0].max_tokens == 256);
        CHECK(provider.seen[1].model == "reviewer-test");
        CHECK(provider.seen[1].messages.back().content.find("specialist-advice") != std::string::npos);
        CHECK(provider.seen[2].model == "foundation-test");
        CHECK(provider.seen[2].messages.back().content.find("middle-critique") != std::string::npos);
        const auto entries = store.events_after(request.session_id, 0);
        REQUIRE(entries);
        CHECK(std::ranges::any_of(*entries, [](const engcoder::core::Event& event) {
            return event.type == "addon.completed" &&
                event.data.value("mode", std::string{}) == "small-checklist-only";
        }));
        // Explicitly assigning the SAME small responder to the C++ addon
        // cannot bypass the strict preparatory-output contract.
        provider.seen.clear();
        request.task_id = "specialist-explicit-small";
        request.addon_models = {{"cpp", "specialist-test"}};
        const auto same_small = runtime.run(request);
        REQUIRE(same_small);
        REQUIRE(provider.seen.size() == 3);
        CHECK(provider.seen[0].max_tokens == 256);
        CHECK(provider.seen[0].tool_choice == "none");
        request.addon_models = engcoder::model::json::object();

        // A short, basic C++23 display request is NOT an excuse to run
        // specialist and reviewer models: KeyMaker handles it directly.
        provider.seen.clear();
        request.task_id = "specialist-basic-code";
        request.user_message = "show me a basic cpp23 app but don't write any files just display it";
        const auto basic = runtime.run(request);
        REQUIRE(basic);
        REQUIRE(provider.seen.size() == 1);
        CHECK(provider.seen.front().model == "foundation-test");
        CHECK(provider.seen.front().messages.back().content.find(request.user_message) != std::string::npos);

        // A malformed/preempted tiny result must never reach the middle model
        // or be rerun automatically for a second attempt.
        provider.seen.clear();
        provider.small_returns_code = true;
        request.task_id = "specialist-invalid-code";
        request.user_message = "Refactor the Vulkan renderer for multithreading";
        const auto invalid = runtime.run(request);
        REQUIRE(invalid);
        REQUIRE(provider.seen.size() == 2);
        CHECK(provider.seen[0].model == "specialist-test");
        CHECK(provider.seen[1].model == "foundation-test");
        CHECK(provider.seen[1].messages.back().content.find("int main()") == std::string::npos);
        CHECK(provider.seen[1].messages.back().content.find(request.user_message) != std::string::npos);
        const auto rejected_events = store.events_after(request.session_id, 0);
        REQUIRE(rejected_events);
        CHECK(std::ranges::any_of(*rejected_events, [](const auto& event) {
            return event.task_id == "specialist-invalid-code" && event.type == "addon.failed";
        }));
        provider.small_returns_code = false;
        request.user_message = "Refactor the Vulkan renderer for multithreading";

        // Disable addon consultation without disabling a configured small
        // *final response* model for simple text elsewhere in the application.
        provider.seen.clear();
        request.task_id = "specialist-disabled";
        request.addons_enabled = false;
        const auto direct = runtime.run(request);
        REQUIRE(direct);
        REQUIRE(provider.seen.size() == 1);
        CHECK(provider.seen.front().model == "foundation-test");
    }
    std::filesystem::remove_all(root);
}

namespace {
class FailingSmallResponder final : public engcoder::model::IModelProvider {
public:
    bool emits_partial = false;
    engcoder::core::Errc small_failure = engcoder::core::Errc::model_unavailable;
    std::vector<std::string> invoked;
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        if (request.model == "small-test") {
            return std::unexpected(engcoder::core::Error{
                engcoder::core::make_error_code(small_failure), "small responder failed"});
        }
        return engcoder::model::ModelResponse{
            .text = "foundation-recovered", .model = request.model};
    }
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete_stream(
        const engcoder::model::ModelRequest& request,
        const engcoder::model::StreamCallback& callback) override {
        invoked.push_back(request.model);
        if (request.model == "small-test" && emits_partial && callback) {
            callback(engcoder::model::StreamEvent{
                .kind = engcoder::model::StreamEventKind::text_delta, .delta = "partial"});
        }
        return engcoder::model::IModelProvider::complete_stream(request, callback);
    }
    engcoder::model::ProviderCapabilities capabilities() const noexcept override { return {}; }
    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}};
    }
};
} // namespace

TEST_CASE("unavailable small responder retries once on foundation only before streaming") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-small-responder-recovery";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    {
        engcoder::core::Config config;
        config.workspace = root;
        config.data_root = root / ".engcoder";
        config.model = "foundation-test";
        FailingSmallResponder provider;
        engcoder::decision::DeterministicDecisionEngine decisions;
        engcoder::models::ModelRegistry models(config, config.data_root);
        engcoder::residency::ModelResidencyManager residency;
        residency.sync(models);
        engcoder::addons::AddonRegistry addons(config, provider, models);
        engcoder::controller::ArchitectController architect(config, provider, addons, models, residency);
        engcoder::training::TrainingService training(config, config.data_root, models, residency);
        engcoder::learning::LearningService learning(config.data_root);
        engcoder::tools::ToolRegistry tools(decisions);
        engcoder::persistence::SessionStore store(config.data_root / "engcoder.db");
        engcoder::core::EventBus events;
        engcoder::agent::AgentRuntime runtime(
            config, provider, addons, architect, models, residency, training, learning, tools, store, events);
        engcoder::agent::TaskRequest request{
            .project_id = "small-test", .session_id = "small-recovery-session",
            .task_id = "small-no-fragments", .user_message = "hi",
            .foundation_model = "foundation-test", .simple_model = "small-test",
            .auto_route = true, .addons_enabled = false,
        };
        const auto recovered = runtime.run(request);
        REQUIRE(recovered);
        CHECK(recovered->final_text == "foundation-recovered");
        REQUIRE(provider.invoked.size() == 2);
        CHECK(provider.invoked[0] == "small-test");
        CHECK(provider.invoked[1] == "foundation-test");
        provider.invoked.clear();
        provider.emits_partial = true;
        request.task_id = "small-partial";
        const auto refused_retry = runtime.run(request);
        CHECK_FALSE(refused_retry.has_value());
        REQUIRE(provider.invoked.size() == 1);
        CHECK(provider.invoked[0] == "small-test");

        provider.invoked.clear();
        provider.emits_partial = false;
        provider.small_failure = engcoder::core::Errc::model_output_limit;
        request.task_id = "small-output-limit";
        const auto capped = runtime.run(request);
        REQUIRE_FALSE(capped.has_value());
        CHECK(capped.error().code == engcoder::core::make_error_code(engcoder::core::Errc::model_output_limit));
        CHECK(provider.invoked == std::vector<std::string>{"small-test"});
    }
    std::filesystem::remove_all(root);
}
