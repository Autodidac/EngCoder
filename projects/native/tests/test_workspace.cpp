#include <catch2/catch_test_macros.hpp>
#include "engcoder/workspace/workspace.hpp"
#include "engcoder/workspace/checkpoint_store.hpp"
#include <filesystem>
#include <fstream>
TEST_CASE("workspace blocks parent escape") {const auto root=std::filesystem::temp_directory_path()/"engcoder-test-workspace";std::filesystem::create_directories(root);engcoder::workspace::Workspace w(root);REQUIRE_FALSE(w.resolve("../escape.txt").has_value());std::filesystem::remove_all(root);}
TEST_CASE("workspace round trip") {const auto root=std::filesystem::temp_directory_path()/"engcoder-test-workspace2";std::filesystem::create_directories(root);engcoder::workspace::Workspace w(root);REQUIRE(w.write_text("a/b.txt","hello").has_value());auto x=w.read_text("a/b.txt");REQUIRE(x);REQUIRE(*x=="hello");std::filesystem::remove_all(root);}

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
