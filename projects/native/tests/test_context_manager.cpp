#include <catch2/catch_test_macros.hpp>
#include "engcoder/agent/context_manager.hpp"

TEST_CASE("context retains system and newest messages") {
    engcoder::agent::ContextManager c(250, 80);
    std::vector<engcoder::model::Message> m{
        {"system", "SYS", {}, {}},
        {"user", std::string(200, 'a'), {}, {}},
        {"assistant", std::string(100, 'b'), {}, {}},
        {"user", "new", {}, {}}
    };
    auto out = c.prepare(m);
    REQUIRE(out.front().role == "system");
    REQUIRE(out.back().content == "new");
}

TEST_CASE("token-aware compaction preserves root and latest working set") {
    engcoder::agent::ContextManager c(1'000'000, 8'000);
    std::vector<engcoder::model::Message> messages{{"system", "ROOT", {}, {}}};
    for (int i = 0; i < 18; ++i) {
        messages.push_back({i % 2 == 0 ? "user" : "assistant", std::string(4'000, static_cast<char>('a' + (i % 20))), {}, {}});
    }
    messages.push_back({"user", "LATEST REQUEST", {}, {}});

    const engcoder::agent::ContextPolicy policy{
        .context_tokens = 16'384,
        .reserve_tokens = 2'048,
        .auto_compact = true,
        .trigger_ratio = 0.80,
        .target_ratio = 0.55,
    };
    const auto out = c.prepare_detailed(messages, policy);
    CHECK(out.compacted);
    CHECK(out.compacted_messages > 0);
    REQUIRE_FALSE(out.messages.empty());
    CHECK(out.messages.front().content == "ROOT");
    CHECK(out.messages.back().content == "LATEST REQUEST");
    CHECK(out.estimated_tokens_after < out.estimated_tokens_before);
    CHECK(out.messages.size() < messages.size());
}

TEST_CASE("automatic compaction can be disabled per model profile") {
    engcoder::agent::ContextManager c(1'000'000, 8'000);
    std::vector<engcoder::model::Message> messages{
        {"system", "ROOT", {}, {}},
        {"user", std::string(80'000, 'x'), {}, {}},
    };
    const auto out = c.prepare_detailed(messages, engcoder::agent::ContextPolicy{
        .context_tokens = 8'192,
        .reserve_tokens = 1'024,
        .auto_compact = false,
    });
    CHECK_FALSE(out.compacted);
    CHECK(out.messages.size() == messages.size());
}


TEST_CASE("recommended Qwen Flash profile keeps at least 75K active tokens before compaction") {
    engcoder::agent::ContextManager c(2'000'000, 96'000);
    std::vector<engcoder::model::Message> messages{{"system", "ROOT", {}, {}}};
    // ~72.5K estimated content tokens plus overhead: below the ~75.5K trigger
    // for 81,920 total context, 4,096 reserve, 97% compaction threshold.
    messages.push_back({"user", std::string(290'000, 'x'), {}, {}});
    const engcoder::agent::ContextPolicy policy{
        .context_tokens = 81'920,
        .reserve_tokens = 4'096,
        .auto_compact = true,
        .trigger_ratio = 0.97,
        .target_ratio = 0.72,
    };
    const auto below = c.prepare_detailed(messages, policy);
    CHECK_FALSE(below.compacted);

    messages.push_back({"assistant", std::string(24'000, 'y'), {}, {}});
    const auto above = c.prepare_detailed(messages, policy);
    CHECK(above.compacted);
    CHECK(above.estimated_tokens_after < above.estimated_tokens_before);
}

TEST_CASE("context policy normalizes malformed compaction ratios safely") {
    engcoder::agent::ContextManager c(1'000'000, 8'000);
    std::vector<engcoder::model::Message> messages{
        {"system", "ROOT", {}, {}},
        {"user", std::string(120'000, 'x'), {}, {}},
        {"assistant", std::string(120'000, 'y'), {}, {}},
    };
    const auto out = c.prepare_detailed(messages, engcoder::agent::ContextPolicy{
        .context_tokens = 32'768,
        .reserve_tokens = 4'096,
        .auto_compact = true,
        .trigger_ratio = -5.0,
        .target_ratio = 99.0,
    });
    CHECK(out.compacted);
    CHECK(out.estimated_tokens_after < out.estimated_tokens_before);
}
