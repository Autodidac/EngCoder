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

TEST_CASE("Qwen last output remains readable after finish, pruning, and refresh", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto first = journal.begin("qwenflash://next", "external-one");
    journal.append(first, "qwenflash://next", "text", "retained answer");
    journal.finish(first, "qwenflash://next");

    const auto completed = journal.after(0);
    CHECK(completed.latest.request_id == first);
    CHECK(completed.latest.model == "qwenflash://next");
    CHECK(completed.latest.output == "retained answer");
    CHECK_FALSE(completed.latest.generating);

    // No more events arrive after completion. A subsequent poll must still
    // return the last generation even with an up-to-date event cursor.
    const auto idle = journal.after(completed.cursor);
    CHECK(idle.events.empty());
    CHECK(idle.latest.output == "retained answer");
    CHECK_FALSE(idle.latest.generating);

    // Pruning the event ring does not delete the latest request snapshot.
    const std::string extra(4096, 'x');
    for (int i = 0; i < 160; ++i) {
        // Other traffic can age old events out but not this completed output.
        journal.append(first, "qwenflash://next", "text", extra);
    }
    CHECK(journal.after(0).latest.output == "retained answer");
    CHECK_FALSE(journal.after(0).latest.generating);

    const auto next = journal.begin("qwenflash://next", "external-two");
    // Opening a request does not make previous output vanish; only actual
    // generated fragments replace the retained response.
    CHECK(journal.after(0).latest.output == "retained answer");
    CHECK(journal.after(0).latest.generating);
    CHECK_FALSE(journal.after(0).latest.has_current_output);
    // Overlapping old requests are not allowed to overwrite the new output.
    journal.append(first, "qwenflash://next", "text", "old late fragment");
    journal.finish(first, "qwenflash://next");
    CHECK(journal.after(0).latest.generating);
    journal.append(next, "qwenflash://next", "text", "fresh reply");
    CHECK(journal.after(0).latest.has_current_output);
    journal.finish(next, "qwenflash://next");
    CHECK(journal.after(0).latest.output == "fresh reply");
    CHECK_FALSE(journal.after(0).latest.generating);
}

TEST_CASE("Qwen failed or empty next request does not erase last completed output", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto first = journal.begin("qwenflash://next", "first");
    journal.append(first, "qwenflash://next", "text", "Readable until a new answer arrives");
    journal.finish(first, "qwenflash://next");
    const auto empty = journal.begin("qwenflash://next", "empty");
    CHECK(journal.after(0).latest.output == "Readable until a new answer arrives");
    journal.finish(empty, "qwenflash://next");
    const auto snapshot = journal.after(0);
    CHECK_FALSE(snapshot.latest.generating);
    CHECK_FALSE(snapshot.latest.has_current_output);
    CHECK(snapshot.latest.output == "Readable until a new answer arrives");
}

TEST_CASE("Qwen snapshot bounds output independently of the event journal", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto id = journal.begin("qwenflash://next");
    const std::string prefix(80 * 1024, 'a');
    const std::string suffix(80 * 1024, 'b');
    journal.append(id, "qwenflash://next", "text", prefix);
    journal.append(id, "qwenflash://next", "text", suffix);
    journal.finish(id, "qwenflash://next");
    const auto snapshot = journal.after(0);
    REQUIRE(snapshot.latest.output.size() == 96 * 1024);
    CHECK(snapshot.latest.output.front() == 'a');
    CHECK(snapshot.latest.output.back() == 'b');
    CHECK_FALSE(snapshot.latest.generating);
}

TEST_CASE("Qwen output snapshot preserves complete UTF-8 at chunk and retention boundaries", "[qwen][output]") {
    engcoder::model::QwenOutputJournal journal;
    const auto id = journal.begin("qwenflash://next");
    const std::string glyph = "\xF0\x9F\x92\xA1"; // U+1F4A1, four bytes
    std::string long_text(4095, 'a');
    long_text += glyph;
    long_text += std::string(96 * 1024 + 8, 'z');
    journal.append(id, "qwenflash://next", "text", long_text);
    journal.finish(id, "qwenflash://next");
    const auto snapshot = journal.after(0);
    REQUIRE_FALSE(snapshot.events.empty());
    for (const auto& event : snapshot.events) {
        CHECK(event.delta.size() <= 4096);
        if (!event.delta.empty()) {
            CHECK((static_cast<unsigned char>(event.delta.front()) & 0xC0u) != 0x80u);
        }
    }
    CHECK(snapshot.latest.output.size() <= 96 * 1024);
    CHECK(snapshot.latest.output.back() == 'z');
}
