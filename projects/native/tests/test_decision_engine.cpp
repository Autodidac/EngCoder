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

TEST_CASE("structured cmake build is allowed") {
    engcoder::decision::DeterministicDecisionEngine decisions;
    auto result = decisions.review_tool({
        "process.run",
        {{"executable", "cmake"}, {"arguments", std::vector<std::string>{"--build", "out/build/windows-vs2022"}}},
        "."
    });
    REQUIRE(result);
    REQUIRE(result->disposition == engcoder::decision::ToolDisposition::allow);
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
