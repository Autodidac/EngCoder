#include <catch2/catch_test_macros.hpp>

#include "engcoder/training/training_service.hpp"

#include <string>

TEST_CASE("training worker progress preserves the final rejected candidate metrics") {
    const auto result = engcoder::training::parse_training_worker_output(R"json(
{"event":"bootstrap-repair-resume","prior_total_steps":440}
{"event":"training-progress","step":1,"loss":2.02}
{
  "status": "rejected",
  "generation": "g000002",
  "promoted_bool": false,
  "anchor_examples_evaluated": 20,
  "generation_readiness_samples": [{"output":"brace } and escaped \"quote\" and slash \\"}]
}
)json");
    REQUIRE(result.has_value());
    CHECK(result->at("status") == "rejected");
    CHECK(result->at("generation") == "g000002");
    CHECK(result->at("promoted_bool") == false);
    CHECK(result->at("anchor_examples_evaluated") == 20);
    CHECK(result->at("generation_readiness_samples").size() == 1);
    CHECK_FALSE(result->contains("output"));
}

TEST_CASE("training worker parser accepts doctor and preemption terminal objects") {
    const auto doctor = engcoder::training::parse_training_worker_output("  {\"torch\":\"2.10\",\"cuda\":true}\r\n");
    REQUIRE(doctor.has_value());
    CHECK(doctor->at("cuda") == true);
    const auto paused = engcoder::training::parse_training_worker_output(
        "{\"event\":\"checkpoint\",\"step\":10}\n{\"status\":\"preempted\",\"exit_code\":75}");
    REQUIRE(paused.has_value());
    CHECK(paused->at("status") == "preempted");
}

TEST_CASE("training worker parser never treats malformed or partial output as success") {
    for (const std::string output : {
        "", "plain text", "[]", "null", "{\"status\":true}",
        "{\"event\":42}", "{\"event\":\"training-progress\",\"step\":10}",
        "{\"event\":\"training-progress\"}\n{\"status\":\"rejected\"",
        "{\"status\":\"promoted\"} garbage", "{\"status\":\"promoted\"}{broken}",
        "{\"status\":\"rejected\"}{\"status\":\"promoted\"}",
        "{\"nested\":[}]}", "{\"event\":\"bad\",}\n{\"status\":\"promoted\"}",
    }) {
        INFO(output);
        CHECK_FALSE(engcoder::training::parse_training_worker_output(output).has_value());
    }
}

TEST_CASE("training worker parser enforces bounded size and nesting") {
    CHECK_FALSE(engcoder::training::parse_training_worker_output(std::string(4U * 1024U * 1024U + 1U, ' ')).has_value());
    const auto deeply_nested = "{\"items\":" + std::string(128, '[') + "0" + std::string(128, ']') + "}";
    CHECK_FALSE(engcoder::training::parse_training_worker_output(deeply_nested).has_value());
}
