#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engcoder/learning/learning_service.hpp"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("corrupt learning denylist never becomes an empty policy") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-learning-corrupt-policy-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "learning");
    const auto path = root / "learning" / "denylist.json";
    for (const auto* invalid : {"{", "{}", "[null]", "[{}]",
                               "[{\"id\":\"bad\",\"prompt_hash\":\"bad\",\"response_hash\":\"bad\"}]"}) {
        { std::ofstream output(path); output << invalid; }
        REQUIRE_THROWS_WITH(engcoder::learning::LearningService(root),
                           Catch::Matchers::ContainsSubstring("Learning denylist cannot be verified"));
        std::ifstream input(path);
        REQUIRE(std::string(std::istreambuf_iterator<char>(input), {}) == invalid);
    }
    { std::ofstream output(path); output << "[]"; }
    REQUIRE_NOTHROW(engcoder::learning::LearningService(root));
    std::filesystem::remove_all(root);
}

TEST_CASE("failed denylist persistence preserves prior bans and evidence") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-learning-ban-write-test";
    std::filesystem::remove_all(root);
    {
        engcoder::learning::LearningService learning(root);
        const auto first = learning.ban("existing forbidden prompt", "");
        const auto ban_id = first.at("ban_id").get<std::string>();
        const auto path = root / "learning" / "denylist.json";
        const auto read = [&] {
            std::ifstream input(path, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(input), {});
        };
        const auto before = read();
        const auto staged = root / "learning" / "denylist.json.tmp";
        std::filesystem::create_directory(staged);
        const engcoder::learning::EvidenceRecord evidence{
            .skill = "english", .prompt = "new prompt", .response = "new response",
        };
        REQUIRE(learning.record(evidence));
        REQUIRE_THROWS(learning.ban(evidence.prompt, evidence.response));
        REQUIRE_FALSE(learning.is_banned(evidence.prompt, evidence.response));
        REQUIRE(learning.recent(10).at("items").size() == 1);
        REQUIRE_THROWS(learning.unban(ban_id));
        REQUIRE(learning.is_banned("existing forbidden prompt", "anything"));
        REQUIRE(read() == before);
        engcoder::learning::LearningService reopened(root);
        REQUIRE(reopened.is_banned("existing forbidden prompt", "anything"));
        std::filesystem::remove(staged);
        REQUIRE(learning.unban(ban_id));
        REQUIRE(learning.ban(evidence.prompt, evidence.response).at("banned").get<bool>());
        REQUIRE(learning.recent(10).at("items").empty());
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("verified learning evidence is deduplicated and immediately retrievable") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-learning-service-test";
    std::filesystem::remove_all(root);

    {
        engcoder::learning::LearningService learning(root);
        const engcoder::learning::EvidenceRecord evidence{
            .id = {},
            .skill = "cpp23-core",
            .prompt = "Why does std::vector iterator invalidation matter after erase?",
            .response = "erase invalidates iterators at and after the erased position; use the iterator returned by erase.",
            .source = "compiler-test-user-accepted",
            .confidence = 0.98,
            .verified = true,
        };
        REQUIRE(learning.record(evidence));
        REQUIRE_FALSE(learning.record(evidence));

        const auto result = learning.retrieve("vector erase iterator invalidation", 3, "cpp23-core");
        REQUIRE(result.at("items").is_array());
        REQUIRE(result.at("items").size() == 1);
        REQUIRE(result.at("items").front().value("eligible_for_training", false));

        const auto context = learning.context_for("vector erase iterator invalidation", 3, "cpp23-core");
        REQUIRE(context.find("verified memory") != std::string::npos);
        REQUIRE(context.find("iterator") != std::string::npos);

        const auto status = learning.describe();
        REQUIRE(status.value("verified_records", 0U) == 1U);
        REQUIRE(status.value("training_eligible_records", 0U) == 1U);
        REQUIRE(status.value("direct_weight_mutation", true) == false);
    }

    std::filesystem::remove_all(root);
}


TEST_CASE("learning evidence can be removed and hard-banned from future ingestion") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-learning-ban-test";
    std::filesystem::remove_all(root);
    {
        engcoder::learning::LearningService learning(root);
        const engcoder::learning::EvidenceRecord evidence{
            .id = "ban-me",
            .skill = "cpp23-core",
            .prompt = "do not learn this prompt",
            .response = "do not learn this response",
            .source = "user-accepted",
            .confidence = 1.0,
            .verified = true,
        };
        REQUIRE(learning.record(evidence));
        const auto recent = learning.recent(10);
        REQUIRE(recent.at("items").size() == 1);
        const auto hash = recent.at("items").at(0).at("content_hash").get<std::string>();
        REQUIRE(learning.remove(hash));
        REQUIRE(learning.recent(10).at("items").empty());

        REQUIRE(learning.record(evidence));
        const auto banned = learning.ban(evidence.prompt, evidence.response);
        REQUIRE(banned.at("banned").get<bool>());
        REQUIRE(learning.recent(10).at("items").empty());
        REQUIRE_FALSE(learning.record(evidence));

        auto same_prompt = evidence;
        same_prompt.response = "a different response";
        REQUIRE_FALSE(learning.record(same_prompt));
        auto same_response = evidence;
        same_response.prompt = "a different prompt";
        REQUIRE_FALSE(learning.record(same_response));

        const auto ban_id = banned.at("ban_id").get<std::string>();
        REQUIRE(learning.unban(ban_id));
        REQUIRE(learning.record(evidence));
    }
    std::filesystem::remove_all(root);
}
