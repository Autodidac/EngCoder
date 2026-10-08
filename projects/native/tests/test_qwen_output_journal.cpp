#include <catch2/catch_test_macros.hpp>
#include "engcoder/model/qwen_output_journal.hpp"
#include <string>

TEST_CASE("native Qwen stream journal preserves source output for API and tool clients", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto task = journal.begin("qwenflash://next", "external-openai-client");
    journal.append(task, "qwenflash://next", "reasoning", "draft");
    journal.append(task, "qwenflash://next", "text", "hello");
    journal.append(task, "qwenflash://next", "arguments", "{\"arg\":1}");
    journal.finish(task, "qwenflash://next");
    const auto all = journal.after(0);
    REQUIRE(all.events.size() == 5);
    CHECK(all.events[0].request_id == "external-openai-client");
    CHECK(all.events[1].delta == "draft");
    CHECK(all.events[2].delta == "hello");
    CHECK(all.events[3].delta == "{\"arg\":1}");
    CHECK(all.events[4].kind == "finish");
    CHECK(journal.after(all.cursor).events.empty());
}

TEST_CASE("Qwen source stream keeps bounded memory and splits large chunks", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto task = journal.begin("qwenflash://next");
    const std::string long_chunk(16 * 1024, 'A');
    journal.append(task, "qwenflash://next", "text", long_chunk);
    auto snapshot = journal.after(0);
    REQUIRE(snapshot.events.size() == 5);
    for (std::size_t i = 1; i < snapshot.events.size(); ++i) CHECK(snapshot.events[i].delta.size() <= 4096);
    for (int i = 0; i < 3000; ++i) journal.append(task, "qwenflash://next", "text", "x");
    snapshot = journal.after(0);
    CHECK(snapshot.events.size() <= 2048);
    CHECK(snapshot.events.back().sequence == snapshot.cursor);
}
