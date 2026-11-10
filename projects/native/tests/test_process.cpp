#include <catch2/catch_test_macros.hpp>

#include "engcoder/process/process.hpp"

#include <chrono>
#include <filesystem>
#include <string>

TEST_CASE("process environment overrides are visible only to the child") {
    engcoder::process::Request request;
#ifdef _WIN32
    request.executable = "cmd.exe";
    request.arguments = {"/d", "/c", "echo %ENGCODER_CHILD_ENV_TEST%"};
#else
    request.executable = "/bin/sh";
    request.arguments = {"-c", "printf '%s' \"$ENGCODER_CHILD_ENV_TEST\""};
#endif
    request.working_directory = std::filesystem::current_path();
    request.environment.emplace_back("ENGCODER_CHILD_ENV_TEST", "qwen-flash-runtime");
    request.timeout = std::chrono::seconds{10};

    const auto result = engcoder::process::run(request);
    REQUIRE(result.has_value());
    CHECK(result->exit_code == 0);
    CHECK(result->stdout_text.find("qwen-flash-runtime") != std::string::npos);
}
