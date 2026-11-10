#include <catch2/catch_test_macros.hpp>

#include "engcoder/controller/task_intent.hpp"

#include <chrono>
#include <filesystem>
#include <string>

using engcoder::controller::TaskIntent;
using engcoder::controller::classify_task;
using engcoder::controller::PredictiveRouteShadow;

TEST_CASE("Architect conservatively routes straightforward text") {
    REQUIRE(classify_task("hi") == TaskIntent::simple);
    REQUIRE(classify_task("Hi.") == TaskIntent::simple);
    REQUIRE(classify_task("  HELLO!!  ") == TaskIntent::simple);
    REQUIRE(classify_task("HI!") == TaskIntent::simple);
    REQUIRE(classify_task("  hey?  ") == TaskIntent::simple);
    REQUIRE(classify_task("yo") == TaskIntent::simple);
    REQUIRE(classify_task("good morning") == TaskIntent::simple);
    REQUIRE(classify_task("hindsight") == TaskIntent::complex);
    REQUIRE(classify_task("What is a mutex?") == TaskIntent::simple);
    REQUIRE(classify_task("Thank you") == TaskIntent::simple);
    REQUIRE(classify_task("Fix the C++ compiler error in shader.cpp") == TaskIntent::coding);
    REQUIRE(classify_task("Analyze the sandbox threat model") == TaskIntent::complex);
    REQUIRE(classify_task("What's the square root of Michigan?") == TaskIntent::complex);
    REQUIRE(classify_task("19 * 27") == TaskIntent::complex);
    REQUIRE(classify_task("What is 12% of 180?") == TaskIntent::complex);
    REQUIRE(classify_task("What is 12 percent of 180?") == TaskIntent::complex);
    REQUIRE(classify_task("What is 18 - 4?") == TaskIntent::complex);
    REQUIRE(classify_task("What is 7 times 9?") == TaskIntent::complex);
    REQUIRE(classify_task("What's 11 squared?") == TaskIntent::complex);
    REQUIRE(classify_task("What is the average speed of a round trip?") == TaskIntent::complex);
    REQUIRE(classify_task("What is 2+2?") == TaskIntent::complex);
    REQUIRE(classify_task("What is a semaphore?") == TaskIntent::simple);
    REQUIRE(classify_task("Make a complete security review and then implement changes") == TaskIntent::coding);
    REQUIRE(classify_task("do it") == TaskIntent::complex); // ambiguity escalates
}

TEST_CASE("specialist preparation is for difficult work, not every coding prompt") {
    using engcoder::controller::warrants_specialist_handoff;
    CHECK(classify_task("show me a basic cpp23 app but don't write any files just display it") == TaskIntent::coding);
    CHECK_FALSE(warrants_specialist_handoff("show me a basic cpp23 app but don't write any files just display it"));
    CHECK_FALSE(warrants_specialist_handoff("Write a C++23 hello world example"));
    CHECK(warrants_specialist_handoff("Refactor the Vulkan renderer for multithreading"));
    CHECK(warrants_specialist_handoff("Review the entire source and redesign its architecture"));
    CHECK_FALSE(warrants_specialist_handoff("What is 12% of 180?"));
    CHECK_FALSE(warrants_specialist_handoff("A program starts with X=1; update (5X+3) mod 7"));
}

TEST_CASE("Trainable shadow replays numeric samples without becoming authoritative") {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
        ("engcoder-route-shadow-test-" + std::to_string(unique));
    std::filesystem::remove_all(root);
    {
        PredictiveRouteShadow learner{root};
        REQUIRE(learner.examples_seen() == 0);
        for (int i = 0; i < 12; ++i)
            learner.observe("Fix the C++ compiler warning", TaskIntent::coding);
        REQUIRE(learner.examples_seen() == 12);
        const auto [intent, confidence] = learner.predict("Fix the C++ compiler warning");
        REQUIRE(intent == TaskIntent::coding);
        REQUIRE(confidence > 0.333);
    }
    {
        PredictiveRouteShadow restored{root};
        REQUIRE(restored.examples_seen() == 12);
        REQUIRE(classify_task("Fix the C++ compiler warning") == TaskIntent::coding);
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("quantitative and symbolic problems bypass tiny preparatory models") {
    using engcoder::controller::requires_foundation_reasoning;
    CHECK(requires_foundation_reasoning("A program updates integer \\(X=1\\) by \\(X \\leftarrow (5X+3) \\pmod{7}\\) for 1,000,000 seconds"));
    CHECK(requires_foundation_reasoning("What is 12% of 180?"));
    CHECK(requires_foundation_reasoning("A car travels at 30 mph; find its average speed."));
    CHECK(requires_foundation_reasoning("Find the exact fraction after mixing 10 gallons."));
    CHECK(requires_foundation_reasoning("What is 2 + 2?"));
    CHECK_FALSE(requires_foundation_reasoning("Hi!"));
    CHECK_FALSE(requires_foundation_reasoning("Write a short greeting for a colleague."));
}
