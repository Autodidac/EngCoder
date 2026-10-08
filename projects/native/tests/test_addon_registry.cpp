#include <catch2/catch_test_macros.hpp>

#include "engcoder/addons/addon_registry.hpp"
#include "engcoder/core/config.hpp"
#include "engcoder/model/model_provider.hpp"
#include "engcoder/models/model_registry.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class RecordingModel final : public engcoder::model::IModelProvider {
public:
    std::expected<engcoder::model::ModelResponse, engcoder::core::Error> complete(
        const engcoder::model::ModelRequest& request) override {
        last_model = request.model;
        last_messages = request.messages;
        return engcoder::model::ModelResponse{
            .text = response_text,
            .tool_calls = {},
            .raw = engcoder::model::json::object(),
        };
    }

    engcoder::model::ProviderCapabilities capabilities() const noexcept override { return {}; }

    std::expected<engcoder::model::json, engcoder::core::Error> discover() override {
        return engcoder::model::json{{"reachable", true}};
    }

    std::string response_text{"addon-output"};
    std::string last_model;
    std::vector<engcoder::model::Message> last_messages;
};

engcoder::core::AddonConfig cpp_addon(std::string model = {}) {
    return {
        .id = "cpp",
        .display_name = "C++23 Addon",
        .model = std::move(model),
        .purpose = "C++ specialist",
        .recommended_size = "75M-200M",
        .system_prompt = "C++ capability prompt",
        .triggers = {"c++", "compiler", ".cpp"},
        .temperature = 0.1,
        .enabled = true,
        .auto_route = true,
    };
}

void write_internal_manifest(const std::filesystem::path& state_root) {
    const auto generation_root = state_root / "models" / "internal" / "generations" / "g000000";
    std::filesystem::create_directories(generation_root);
    {
        std::ofstream active(state_root / "models" / "internal" / "active.json");
        active << R"({"generation":"g000000"})";
    }
    {
        std::ofstream manifest(generation_root / "manifest.json");
        manifest << R"({
  "format":"engcoder.internal.v2",
  "generation":"g000000",
  "component_abi":"engcoder.component.v2",
  "parameter_count":1000000,
  "weights_sha256":"test",
  "status":"trained",
  "bootstrap_complete":true,
  "full_backbone_trained":true,
  "generation_ready":true,
  "inference_ready":true,
  "foundation_component":{"parameter_count":700000,"estimated_bytes_bf16":1400000},
  "addon_components":{
    "architect-controller":{"parameter_count":50000},
    "cpp23-core":{"parameter_count":100000},
    "cpp23-debugging":{"parameter_count":50000}
  },
  "skills":["keymaker-foundation","architect-controller","cpp23-core","cpp23-debugging"]
})";
    }
}

} // namespace


TEST_CASE("model registry accepts a clean first-run state with no active manifests") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-first-run-model-registry-test";
    std::filesystem::remove_all(root);

    engcoder::core::Config config;
    config.model = "auto";
    engcoder::models::ModelRegistry models(config, root);

    REQUIRE(models.components().empty());
    REQUIRE(models.foundation_model() == "auto");
    REQUIRE(models.controller_model().empty());
    REQUIRE(models.describe().at("components").is_array());

    std::filesystem::remove_all(root);
}

TEST_CASE("addon registry routes focused C++ tasks with an explicit provider model") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-addon-route-test";
    std::filesystem::remove_all(root);

    engcoder::core::Config config;
    config.addons = {cpp_addon("tiny-cpp-model")};
    RecordingModel model;
    engcoder::models::ModelRegistry models(config, root);
    engcoder::addons::AddonRegistry registry(config, model, models);

    const auto route = registry.route_deterministic("Fix this C++ compiler error in renderer.cpp");
    REQUIRE(route.addon_id.has_value());
    REQUIRE(*route.addon_id == "cpp");

    const auto result = registry.invoke("cpp", "Review this function", "int main() {}");
    REQUIRE(result);
    REQUIRE(result->model == "tiny-cpp-model");
    REQUIRE(model.last_model == "tiny-cpp-model");
    REQUIRE(model.last_messages.size() == 2);

    std::filesystem::remove_all(root);
}

TEST_CASE("word-like addon triggers do not match arbitrary substrings") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-addon-boundary-test";
    std::filesystem::remove_all(root);

    engcoder::core::Config config;
    auto planner = cpp_addon("tiny-planner");
    planner.id = "planner";
    planner.triggers = {"plan"};
    config.addons = {planner};

    RecordingModel model;
    engcoder::models::ModelRegistry models(config, root);
    engcoder::addons::AddonRegistry registry(config, model, models);

    REQUIRE_FALSE(registry.route_deterministic("Describe this planet renderer").addon_id.has_value());
    std::filesystem::remove_all(root);
}

TEST_CASE("new internal generation becomes an addon source after registry refresh without restart") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-dynamic-generation-test";
    std::filesystem::remove_all(root);

    engcoder::core::Config config;
    config.addons = {cpp_addon()};
    RecordingModel model;
    engcoder::models::ModelRegistry models(config, root);
    engcoder::addons::AddonRegistry registry(config, model, models);

    REQUIRE(registry.model_for("cpp").empty());

    write_internal_manifest(root);
    models.refresh();

    REQUIRE(registry.model_for("cpp") == "engcoder://cpp");
    REQUIRE(registry.is_enabled("cpp"));

    std::filesystem::remove_all(root);
}


TEST_CASE("configured internal addon URI is unavailable until trained weights exist") {
    const auto root = std::filesystem::temp_directory_path() / "engcoder-untrained-managed-addon-test";
    std::filesystem::remove_all(root);

    engcoder::core::Config config;
    config.addons = {cpp_addon("engcoder://cpp")};
    RecordingModel model;
    engcoder::models::ModelRegistry models(config, root);
    engcoder::addons::AddonRegistry registry(config, model, models);

    CHECK(registry.model_for("cpp").empty());
    CHECK_FALSE(registry.is_enabled("cpp"));
    CHECK_FALSE(registry.route_deterministic("Fix this C++ compiler error").addon_id.has_value());

    std::filesystem::remove_all(root);
}
