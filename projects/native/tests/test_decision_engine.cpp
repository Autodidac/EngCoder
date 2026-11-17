#include <catch2/catch_test_macros.hpp>
#include "engcoder/decision/decision_engine.hpp"

TEST_CASE("shell interpreters are hard denied") {
    engcoder::decision::DeterministicDecisionEngine decisions;
    auto result = decisions.review_tool({
        "process.run",
        {{"executable", "powershell.exe"}, {"arguments", nlohmann::json::array()}},
        "."
    });
    REQUIRE(result);
    REQUIRE(result->disposition == engcoder::decision::ToolDisposition::deny);
}

TEST_CASE("generic git process requires approval") {
    engcoder::decision::DeterministicDecisionEngine decisions;
    auto result = decisions.review_tool({
        "process.run",
        {{"executable", "git"}, {"arguments", std::vector<std::string>{"status", "--short"}}},
        "."
    });
    REQUIRE(result);
    REQUIRE(result->disposition == engcoder::decision::ToolDisposition::require_approval);
}

TEST_CASE("fixed git inspection tool is allowed") {
    engcoder::decision::DeterministicDecisionEngine decisions;
    auto result = decisions.review_tool({"git.status", nlohmann::json::object(), "."});
    REQUIRE(result);
    REQUIRE(result->disposition == engcoder::decision::ToolDisposition::allow);
}

TEST_CASE("weighted review cannot waive required operator approval") {
    using namespace engcoder::decision;
    CompositeDecisionEngine decisions({}, {});
    auto result = decisions.review_tool({
        "process.run",
        {{"executable", "cmake"}, {"arguments", std::vector<std::string>{"--build", "out/build/windows-vs2022"}}},
        "."
    });
    REQUIRE(result);
    REQUIRE(result->disposition == ToolDisposition::require_approval);
    REQUIRE(result->probabilities.allow > 0.90);

    for (const auto& proposal : std::vector<ToolProposal>{
        {"process.run", {{"executable", "ctest"}}, "."},
        {"process.run", {{"executable", "where.exe"}}, "."},
        {"workspace.patch", {{"path", "src/main.cpp"}}, "."},
        {"lmstudio.integration", {{"integration", "fixture"}}, "."},
        {"workspace.readFutureMutation", json::object(), "."},
    }) {
        CAPTURE(proposal.name);
        const auto review = decisions.review_tool(proposal);
        REQUIRE(review);
        CHECK(review->disposition == ToolDisposition::require_approval);
    }
}

TEST_CASE("deterministic policy denies protected targets and unsafe process classes") {
    using namespace engcoder::decision;
    CompositeDecisionEngine decisions({}, {});
    for (const auto* path : {".git", "sub/.ssh/config", "sub/.CODEx/settings",
                            "src/.engcoder/index", "src/.github/workflows/build.yml",
                            "src/.vscode/./tasks.json", "src\\.vscode\\launch.json"}) {
        CAPTURE(path);
        const auto review = decisions.review_tool({"workspace.patch", {{"path", path}}, "."});
        REQUIRE(review);
        CHECK(review->disposition == ToolDisposition::deny);
    }
    CHECK_FALSE(protected_workspace_target("docs/.github/workflows-example.yml"));
#ifdef _WIN32
    CHECK(protected_workspace_target("src/.git. /config"));
    CHECK(protected_workspace_target("src/file.txt:stream"));
#endif
    for (const auto& proposal : std::vector<ToolProposal>{
        {"process.run", {{"executable", "bin/PYTHON.EXE"}}, "."},
        {"process.run", {{"executable", "curl"}}, "."},
        {"process.run", {{"executable", "cmake"}, {"arguments", {"-P", "fixture.cmake"}}}, "."},
        {"process.run", {{"executable", "cmake"}, {"arguments", {"-E"}}}, "."},
        {"process.run", {{"executable", "cmake"}, {"arguments", {"--install", "build"}}}, "."},
    }) {
        CAPTURE(proposal.arguments.dump());
        const auto review = decisions.review_tool(proposal);
        REQUIRE(review);
        CHECK(review->disposition == ToolDisposition::deny);
    }
    CHECK_FALSE(decisions.review_tool({"workspace.write", {{"path", 1}}, "."}));
    CHECK_FALSE(decisions.review_tool({"process.run", {{"executable", true}}, "."}));
    CHECK_FALSE(decisions.review_tool({"process.run", {{"executable", "cmake"}, {"arguments", {1}}}, "."}));
}

TEST_CASE("weighted reviewer assigns probabilities to checkpointed edits") {
    engcoder::decision::WeightedDecisionEngine decisions;
    auto result = decisions.review_tool({
        "workspace.patch",
        {{"path", "src/main.cpp"}},
        "."
    });
    REQUIRE(result);
    REQUIRE(result->disposition == engcoder::decision::ToolDisposition::allow);
    REQUIRE(result->probabilities.allow > 0.90);
    REQUIRE(result->reviewer == "local-weighted");
}
