#include <catch2/catch_test_macros.hpp>
#include "engcoder/core/ids.hpp"
#include "engcoder/tools/builtin_tools.hpp"
#include "engcoder/workspace/workspace.hpp"
#include "engcoder/workspace/checkpoint_store.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
TEST_CASE("workspace blocks parent escape") {const auto root=std::filesystem::temp_directory_path()/"engcoder-test-workspace";std::filesystem::create_directories(root);engcoder::workspace::Workspace w(root);REQUIRE_FALSE(w.resolve("../escape.txt").has_value());std::filesystem::remove_all(root);}
TEST_CASE("workspace round trip") {const auto root=std::filesystem::temp_directory_path()/"engcoder-test-workspace2";std::filesystem::create_directories(root);engcoder::workspace::Workspace w(root);REQUIRE(w.write_text("a/b.txt","hello").has_value());auto x=w.read_text("a/b.txt");REQUIRE(x);REQUIRE(*x=="hello");std::filesystem::remove_all(root);}

TEST_CASE("workspace tree distinguishes files from empty directories") {
    const auto root = std::filesystem::temp_directory_path() / engcoder::core::make_id("workspace-tree-target");
    std::filesystem::create_directories(root / "empty");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    const engcoder::workspace::Workspace workspace(root);
    REQUIRE(workspace.write_text("src/ordinary.txt", "example\n"));
    const auto empty = workspace.tree("empty");
    REQUIRE(empty);
    CHECK(empty->empty());
    const auto populated = workspace.tree("src");
    REQUIRE(populated);
    CHECK(*populated == std::vector<std::string>{"src/ordinary.txt"});
    const auto file = workspace.tree("src/ordinary.txt");
    REQUIRE_FALSE(file);
    CHECK(file.error().code == engcoder::core::make_error_code(engcoder::core::Errc::invalid_argument));
}

TEST_CASE("workspace tree and search enumerate canonical source files once") {
    const auto root = std::filesystem::temp_directory_path() / engcoder::core::make_id("workspace-enumeration");
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    const engcoder::workspace::Workspace workspace(root);
    REQUIRE(workspace.write_text("src/ordinary.txt", "example marker\n"));
    REQUIRE(workspace.write_text("build/generated.txt", "example marker\n"));
#ifdef _WIN32
    REQUIRE(workspace.write_text("OuT/generated.txt", "example marker\n"));
#endif

    const auto assert_normal_view = [&] {
        const auto tree = workspace.tree(".");
        REQUIRE(tree);
        CHECK(tree->size() == 2);
        CHECK(std::ranges::find(*tree, "src/ordinary.txt") != tree->end());
        const auto matches = workspace.search_text("example marker");
        REQUIRE(matches);
        REQUIRE(matches->size() == 1);
        CHECK(matches->front() == "src/ordinary.txt:1:example marker");
    };
    assert_normal_view();

    std::error_code error;
    std::filesystem::create_symlink(root / "src/ordinary.txt", root / "alias.txt", error);
    if (error) SKIP("Symbolic links unavailable: " + error.message());
    std::filesystem::create_directory_symlink(root / "src", root / "src-alias", error);
    REQUIRE_FALSE(error);
    assert_normal_view();
}

TEST_CASE("create-only workspace writes never replace an existing file") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-test-create-only";
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    engcoder::workspace::Workspace workspace(root);
    REQUIRE(workspace.write_text("new.txt", "original", false));
    REQUIRE_FALSE(workspace.write_text("new.txt", "replacement", false));
    CHECK(workspace.read_text("new.txt").value_or("") == "original");
}

TEST_CASE("checkpoint restore rejects traversal and preserves a normal capture") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-checkpoint-boundary-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".engcoder");
    {
        engcoder::workspace::Workspace workspace(root);
        engcoder::workspace::CheckpointStore checkpoints(workspace, root / ".engcoder", 1'048'576);
        REQUIRE(workspace.write_text("nested/source.txt", "before"));
        const auto capture = checkpoints.capture("nested/source.txt");
        REQUIRE(capture);
        REQUIRE_FALSE(checkpoints.restore("../invalid", "nested/source.txt"));
        REQUIRE_FALSE(checkpoints.restore(*capture, "../escape.txt"));
        REQUIRE(workspace.write_text("nested/unrelated.txt", "leave-me"));
        REQUIRE_FALSE(checkpoints.restore(*capture, "nested/unrelated.txt"));
        CHECK(workspace.read_text("nested/unrelated.txt").value_or("") == "leave-me");
        const auto missing = checkpoints.capture("nested/no-such-file.txt");
        REQUIRE(missing);
        REQUIRE_FALSE(checkpoints.restore(*missing, "nested/unrelated.txt"));
        CHECK(workspace.read_text("nested/unrelated.txt").value_or("") == "leave-me");
        REQUIRE_FALSE(checkpoints.capture("../escape.txt"));
        REQUIRE(workspace.write_text("nested/source.txt", "after"));
        REQUIRE(checkpoints.restore(*capture, "nested/source.txt"));
        const auto restored = workspace.read_text("nested/source.txt");
        REQUIRE(restored);
        CHECK(*restored == "before");
    }
    std::filesystem::remove_all(root);
}

TEST_CASE("workspace tools preserve exact patches and bound candidate reads") {
    const auto root = std::filesystem::temp_directory_path() / engcoder::core::make_id("engcoder-patch-test");
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    engcoder::core::Config config;
    engcoder::workspace::Workspace workspace(root);
    engcoder::workspace::CheckpointStore checkpoints(workspace, root / ".engcoder", 1'048'576);
    engcoder::persistence::SessionStore sessions(root / "session.sqlite");
    engcoder::model::LmStudioProvider provider(config);
    engcoder::models::ModelRegistry models(config, root / ".engcoder");
    engcoder::addons::AddonRegistry addons(config, provider, models);
    engcoder::decision::DeterministicDecisionEngine decisions;
    engcoder::tools::ToolRegistry tools(decisions);
    engcoder::tools::register_builtin_tools(tools, workspace, checkpoints, sessions, provider, addons, config);
    const auto patch = [&](const char* expected, const char* replacement) {
        return tools.invoke("workspace.patch", {{"path", "source.cpp"}, {"expected", expected}, {"replacement", replacement}}, {});
    };

    SECTION("direct mutation handlers protect configuration after path resolution") {
        constexpr auto protected_path = ".github/workflows/build.yml";
        REQUIRE(workspace.write_text(protected_path, "original"));
        const auto run = [&](const char* path) {
            CHECK_FALSE(tools.invoke("workspace.write", {{"path", path}, {"content", "new"}}, {}));
            CHECK_FALSE(tools.invoke("workspace.patch",
                {{"path", path}, {"expected", "original"}, {"replacement", "new"}}, {}));
            CHECK(workspace.read_text(protected_path) == "original");
            CHECK(std::filesystem::is_empty(root / ".engcoder" / "checkpoints"));
        };
        run(protected_path);
        std::error_code link_error;
        std::filesystem::create_symlink(root / protected_path, root / "workflow.txt", link_error);
        if (!link_error) run("workflow.txt");
    }

    SECTION("the host can patch the observed duplicate while model arguments cannot select a line") {
        constexpr auto original = "repeat\r\nkeep\r\nrepeat\r\n";
        REQUIRE(workspace.write_text("source.cpp", original));
        auto args = engcoder::tools::json{{"path", "source.cpp"}, {"expected", "repeat"},
            {"replacement", "changed"}, {"patch_start_line", 3}};
        CHECK_FALSE(tools.invoke("workspace.patch", args, {}));
        CHECK(workspace.read_text("source.cpp") == original);
        args.erase("patch_start_line");
        engcoder::tools::Context candidate;
        candidate.candidate_mode = true;
        candidate.patch_start_line = 3;
        REQUIRE(tools.invoke("workspace.patch", args, candidate));
        CHECK(workspace.read_text("source.cpp") == "repeat\r\nkeep\r\nchanged\r\n");
        REQUIRE(workspace.write_text("source.cpp", "repeat\r\nkeep\r\nrepeat-extra\r\n"));
        CHECK_FALSE(tools.invoke("workspace.patch", args, candidate));
        CHECK(workspace.read_text("source.cpp") == "repeat\r\nkeep\r\nrepeat-extra\r\n");
    }

    SECTION("converts LF arguments to CRLF and captures the original bytes") {
        constexpr auto original = "prefix\r\nold line\r\nsecond line\r\nsuffix";
        REQUIRE(workspace.write_text("source.cpp", original));
        const auto result = patch("old line\nsecond line", "first\r\nnext\nlast");
        REQUIRE(result);
        CHECK(workspace.read_text("source.cpp") == "prefix\r\nfirst\r\nnext\r\nlast\r\nsuffix");
        REQUIRE(checkpoints.restore(result->at("checkpoint_id").get<std::string>(), "source.cpp"));
        CHECK(workspace.read_text("source.cpp") == original);
    }

    SECTION("inherits CRLF even when the expected text fits on one line") {
        REQUIRE(workspace.write_text("source.cpp", "before\r\nafter\r\n"));
        REQUIRE(patch("before", "one\ntwo"));
        CHECK(workspace.read_text("source.cpp") == "one\r\ntwo\r\nafter\r\n");
    }

    SECTION("multiline arrays join physical lines and preserve source string escapes") {
        constexpr auto original = "before\r\nafter\r\n";
        REQUIRE(workspace.write_text("source.cpp", original));
        const auto result = tools.invoke("workspace.patch", {
            {"path", "source.cpp"}, {"expected", "before"},
            {"replacement_lines", engcoder::tools::json::array({"first", "", R"(const auto newline = "\n";)"})},
        }, {});
        REQUIRE(result);
        CHECK(workspace.read_text("source.cpp") == "first\r\n\r\nconst auto newline = \"\\n\";\r\nafter\r\n");
        REQUIRE(checkpoints.restore(result->at("checkpoint_id").get<std::string>(), "source.cpp"));
        CHECK(workspace.read_text("source.cpp") == original);
    }

    SECTION("ambiguous or malformed representations cannot apply a patch") {
        REQUIRE(workspace.write_text("source.cpp", "before\n"));
        engcoder::tools::json args{{"path", "source.cpp"}, {"expected", "before"},
            {"replacement", "first"}, {"replacement_lines", engcoder::tools::json::array({"second"})}};
        REQUIRE_FALSE(tools.invoke("workspace.patch", args, {}));
        args.erase("replacement");
        args["replacement_lines"] = engcoder::tools::json::array({"first\nsecond"});
        REQUIRE_FALSE(tools.invoke("workspace.patch", args, {}));
        args["replacement_lines"] = engcoder::tools::json::array({42});
        REQUIRE_FALSE(tools.invoke("workspace.patch", args, {}));
        CHECK(workspace.read_text("source.cpp") == "before\n");
    }

    SECTION("mixed endings retain exact matching and neighboring bytes") {
        REQUIRE(workspace.write_text("source.cpp", "before\r\nafter\n"));
        REQUIRE_FALSE(patch("before\nafter", "changed"));
        REQUIRE(patch("after", "one\ntwo"));
        CHECK(workspace.read_text("source.cpp") == "before\r\none\ntwo\n");
    }

    SECTION("overlapping matches are ambiguous") {
        REQUIRE(workspace.write_text("source.cpp", "aaaa\r\n"));
        const auto result = patch("aaa", "changed");
        REQUIRE_FALSE(result);
        CHECK(result.error().message == "expected block is ambiguous");
        CHECK(workspace.read_text("source.cpp") == "aaaa\r\n");
    }

    SECTION("compatibility cannot split a CRLF pair") {
        REQUIRE(workspace.write_text("source.cpp", "before\r\nafter\r\n"));
        REQUIRE_FALSE(patch("before\r", "one"));
        CHECK(workspace.read_text("source.cpp") == "before\r\nafter\r\n");
    }

    SECTION("candidate read uses bounded pages while an ordinary read stays compatible") {
        std::string original;
        for (int line = 1; line <= 200; ++line) original += "line " + std::to_string(line) + "\r\n";
        REQUIRE(workspace.write_text("source.cpp", original));
        engcoder::tools::Context candidate;
        candidate.candidate_mode = true;
        const auto page = tools.invoke("workspace.read", {{"path", "source.cpp"}}, candidate);
        REQUIRE(page);
        CHECK(page->at("content").get<std::string>().size() < original.size());
        CHECK(page->at("start_line") == 1);
        CHECK(page->at("next_line") == 41);
        const auto tail = tools.invoke("workspace.read", {{"path", "source.cpp"}, {"start_line", 199}, {"max_lines", 2}}, candidate);
        REQUIRE(tail);
        CHECK(tail->at("content") == "line 199\r\nline 200\r\n");
        CHECK(tail->at("next_line").is_null());
        const auto ordinary = tools.invoke("workspace.read", {{"path", "source.cpp"}}, {});
        REQUIRE(ordinary);
        CHECK(ordinary->at("content") == original);
    }

    SECTION("explicit pages respect byte budgets and leave long lines intact") {
        const std::string original = "one\n" + std::string(3'072, 'x') + "\nlast";
        REQUIRE(workspace.write_text("source.cpp", original));
        const auto page = tools.invoke("workspace.read", {{"path", "source.cpp"}, {"start_line", 1}}, {});
        REQUIRE(page);
        CHECK(page->at("content") == "one\n");
        CHECK(page->at("next_line") == 2);
        const auto long_line = tools.invoke("workspace.read", {{"path", "source.cpp"}, {"start_line", 2}}, {});
        REQUIRE_FALSE(long_line);
        CHECK(long_line.error().message.find("start_line 3") != std::string::npos);
        const auto last = tools.invoke("workspace.read", {{"path", "source.cpp"}, {"start_line", 3}}, {});
        REQUIRE(last);
        CHECK(last->at("content") == "last");
        CHECK(last->at("next_line").is_null());
        for (const auto invalid : {0, -1, 4}) {
            REQUIRE_FALSE(tools.invoke("workspace.read", {{"path", "source.cpp"}, {"start_line", invalid}}, {}));
        }
        REQUIRE_FALSE(tools.invoke("workspace.read", {{"path", "source.cpp"}, {"max_lines", 1.5}}, {}));
    }
}

TEST_CASE("workspace search previews preserve complete UTF-8 characters at the byte limit") {
    const auto root = std::filesystem::temp_directory_path() / engcoder::core::make_id("search-utf8");
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};
    engcoder::workspace::Workspace workspace(root);
    const std::string crossing = std::string(399, 'x') + "\xC3\xA9" + " remaining";
    const std::string aligned = std::string(397, 'y') + "\xE2\x82\xAC" + " remaining";
    const std::string ascii(401, 'z');
    REQUIRE(workspace.write_text("fixture.txt", crossing + "\n" + aligned + "\n" + ascii + "\n"));
    const auto hits = workspace.search_text("", 3);
    REQUIRE(hits);
    REQUIRE(hits->size() == 3);
    const auto no_matches = workspace.search_text("not present", 3);
    REQUIRE(no_matches);
    CHECK(no_matches->empty());
    CHECK(hits->at(0) == "fixture.txt:1:" + std::string(399, 'x'));
    CHECK(hits->at(1) == "fixture.txt:2:" + aligned.substr(0, 400));
    CHECK(hits->at(2) == "fixture.txt:3:" + ascii.substr(0, 400));
}
