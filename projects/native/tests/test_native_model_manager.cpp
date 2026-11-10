#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "engcoder/core/config.hpp"
#include "engcoder/model/native_model_manager.hpp"
#include "engcoder/model/runtime_model.hpp"
#include "engcoder/models/model_registry.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::filesystem::path unique_temp_root(std::string_view stem) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        (std::string{stem} + "-" + std::to_string(stamp));
}

struct TempTree {
    std::filesystem::path root;
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};

} // namespace

TEST_CASE("runtime model profiles round-trip without collapsing runtime identity") {
    engcoder::model::RuntimeModelProfile profile{
        .id = "qwen-test",
        .display_name = "Qwen Test",
        .runtime = engcoder::model::RuntimeKind::qwen_flash,
        .source = engcoder::model::ModelSourceKind::hugging_face,
        .repo = "owner/repo",
        .quant = "Q4_K_M",
        .local_path = {},
        .estimated_bytes = 123456789ULL,
        .profile_revision = 4U,
        .runtime_policy = "manual",
        .gpu_offload_percent = 75U,
        .gpu_layer_count_hint = 48U,
        .context_tokens = 98304,
        .parallel_slots = 1,
        .context_reserve_tokens = 8192,
        .auto_compact = true,
        .compact_trigger_ratio = 0.82,
        .compact_target_ratio = 0.60,
        .kv_offload = false,
        .expert_cache_mode = "on",
        .expert_cache_mib = 4096,
        .pinned_host_mib = 8192,
        .speculative_repo = {},
        .speculative_model = {},
        .enabled = true,
    };

    const auto encoded = engcoder::model::to_json(profile);
    const auto decoded = engcoder::model::runtime_model_from_json(encoded);

    CHECK(decoded.id == profile.id);
    CHECK(decoded.runtime == engcoder::model::RuntimeKind::qwen_flash);
    CHECK(decoded.source == engcoder::model::ModelSourceKind::hugging_face);
    CHECK(decoded.repo == profile.repo);
    CHECK(decoded.quant == profile.quant);
    CHECK(decoded.runtime_policy == "manual");
    CHECK(decoded.gpu_offload_percent == 75U);
    CHECK(decoded.gpu_layer_count_hint == 48U);
    CHECK(decoded.context_tokens == profile.context_tokens);
    CHECK(decoded.parallel_slots == 1);
    CHECK(decoded.auto_compact);
    CHECK_FALSE(decoded.kv_offload);
    CHECK(decoded.expert_cache_mode == "on");
    CHECK(engcoder::model::effective_context_tokens(decoded) == 98304);
    CHECK(engcoder::model::runtime_model_uri(decoded) == "qwenflash://qwen-test");
    CHECK(engcoder::model::is_native_runtime_model("qwenflash://qwen-test"));
    CHECK_FALSE(engcoder::model::is_native_runtime_model("engcoder://keymaker"));

    CHECK(engcoder::model::effective_gpu_layers(decoded) == "22"); // 75% of the new 60%-capped range

    auto gpu_target = decoded;
    gpu_target.gpu_layers = "auto";
    gpu_target.gpu_offload_percent = 70U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "20");
    gpu_target.gpu_offload_percent = 60U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "17");
    gpu_target.gpu_offload_percent = 36U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "10");
    gpu_target.gpu_offload_percent = 0U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "0");
    gpu_target.gpu_offload_percent = 100U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "29");
    gpu_target.gpu_offload_percent = 95U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "27");
    gpu_target.gpu_offload_percent = 30U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "9");
    gpu_target.gpu_layers = "all";
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "29");
    gpu_target.gpu_layers = "48";
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "29");
    gpu_target.gpu_layers = "17";
    gpu_target.gpu_offload_percent = 70U;
    CHECK(engcoder::model::effective_gpu_layers(gpu_target) == "17");

    engcoder::model::RuntimeModelProfile local{
        .id = "local-test",
        .display_name = "Local Test",
        .runtime = engcoder::model::RuntimeKind::qwen_flash,
        .source = engcoder::model::ModelSourceKind::local_gguf,
        .repo = {},
        .quant = {},
        .local_path = "/tmp/local-test.gguf",
        .estimated_bytes = 42,
        .speculative_repo = {},
        .speculative_model = {},
    };
    const auto local_round_trip = engcoder::model::runtime_model_from_json(engcoder::model::to_json(local));
    CHECK(local_round_trip.source == engcoder::model::ModelSourceKind::local_gguf);
    CHECK(local_round_trip.repo.empty());
    CHECK(local_round_trip.local_path == local.local_path);
}

TEST_CASE("native model manager seeds Qwen Flash and derives ids for user models") {
    TempTree temp{unique_temp_root("engcoder-native-model-manager")};
    std::filesystem::create_directories(temp.root);

    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = temp.root / "state";
    config.model_root = config.data_root / "models";
    config.model_cache_root = config.data_root / "model-cache";
    config.qwen_flash_enabled = false; // Catalog tests must not launch a backend.

    engcoder::model::NativeModelManager manager(config, config.data_root);
    const auto seeded = manager.find("qwenflash://qwen3.8-flash-next-ud-q2-k-xl");
    REQUIRE(seeded.has_value());
    CHECK(seeded->runtime == engcoder::model::RuntimeKind::qwen_flash);
    CHECK(seeded->repo == "unsloth/Qwen3.8-Flash-Next-GGUF");
    CHECK(seeded->runtime_policy == "recommended");
    CHECK(seeded->gpu_offload_percent == 50U);
    CHECK(seeded->gpu_layer_count_hint == 48U);
    CHECK(seeded->context_tokens == 81920);
    CHECK(seeded->parallel_slots == 1);
    CHECK(seeded->auto_compact);
    CHECK(seeded->expert_cache_mode == "auto");
    CHECK_FALSE(seeded->speculative_enabled);
    CHECK(seeded->speculative_repo.empty());
    CHECK(seeded->speculative_model.empty());
    CHECK(seeded->profile_revision == 9U);

    // 0.7.1 changes the default, not the safe GPU scale or a saved user value.
    CHECK(engcoder::model::gpu_offload_default_percent == 50U);
    CHECK(engcoder::model::gpu_offload_legacy_ceiling_percent == 60U);
    auto half_scale = *seeded;
    half_scale.gpu_layers = "auto";
    CHECK(engcoder::model::effective_gpu_layers(half_scale) == "14");


    auto registered = manager.register_model({
        {"source", "hugging-face"},
        {"repo", "example/Another-Model-GGUF"},
        {"quant", "Q4_K_M"},
    });
    REQUIRE(registered.has_value());
    CHECK(registered->value("id", std::string{}) == "example-another-model-gguf-q4_k_m");

    const auto user_model = manager.find("qwenflash://example-another-model-gguf-q4_k_m");
    REQUIRE(user_model.has_value());
    CHECK(user_model->display_name == "example/Another-Model-GGUF · Q4_K_M");

    engcoder::models::ModelRegistry registry(config, config.data_root);
    registry.refresh();
    const auto description = registry.describe();
    bool found_native = false;
    for (const auto& component : description.value("components", nlohmann::json::array())) {
        if (component.value("provider_model", std::string{}) ==
            "qwenflash://example-another-model-gguf-q4_k_m") {
            found_native = true;
            CHECK(component.value("origin", std::string{}) == "native-runtime");
            CHECK(component.value("runtime_type", std::string{}) == "qwen-flash");
        }
    }
    CHECK(found_native);

    REQUIRE(registry.set_foundation_model("qwenflash://example-another-model-gguf-q4_k_m"));
    CHECK(registry.foundation_model() == "qwenflash://example-another-model-gguf-q4_k_m");

    // Foundation selection is product state, not a browser-local preference.
    // A fresh registry instance must see the same native KeyMaker choice.
    engcoder::models::ModelRegistry reloaded(config, config.data_root);
    CHECK(reloaded.foundation_model() == "qwenflash://example-another-model-gguf-q4_k_m");
}

TEST_CASE("runtime model validation rejects unsafe scheduling values") {
    engcoder::model::RuntimeModelProfile profile{
        .id = "unsafe-test",
        .display_name = "Unsafe Test",
        .runtime = engcoder::model::RuntimeKind::qwen_flash,
        .source = engcoder::model::ModelSourceKind::hugging_face,
        .repo = "owner/repo",
        .quant = "Q4_K_M",
        .local_path = {},
        .speculative_repo = {},
        .speculative_model = {},
    };
    auto encoded = engcoder::model::to_json(profile);
    encoded["threads"] = 0;
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["ubatch_size"] = 2048;
    encoded["batch_size"] = 1024;
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["runtime_policy"] = "mystery";
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["gpu_offload_percent"] = 101;
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["gpu_layers"] = "bad-value";
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["expert_cache_mode"] = "unsafe-magic";
    CHECK_THROWS_AS(engcoder::model::runtime_model_from_json(encoded), std::invalid_argument);

    encoded = engcoder::model::to_json(profile);
    encoded["speculative_enabled"] = true;
    encoded["speculative_repo"] = profile.repo;
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("cannot use the main model repository"));

    // A different remote repository is not a substitute for a local MTP GGUF.
    encoded["speculative_repo"] = "other/remote-draft";
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("Only a local, dedicated Qwen draft-mtp model"));

    // Without a remote repository, enabled MTP must name a real draft path.
    encoded["speculative_repo"] = nullptr;
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("independent local draft GGUF"));

    encoded["speculative_model"] = "draft.bin";
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("local .gguf file"));

    encoded["speculative_model"] = "independent-draft.GGUF";
    encoded["speculative_type"] = "unsupported-draft-type";
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("Only a local, dedicated Qwen draft-mtp model"));

    encoded["speculative_type"] = "draft-mtp";
    CHECK_NOTHROW(engcoder::model::runtime_model_from_json(encoded));

    // The unsafe remote repository must still be rejected when a valid local
    // draft file is also supplied (no fallthrough to the valid-file case).
    encoded["speculative_repo"] = profile.repo;
    CHECK_THROWS_WITH(
        engcoder::model::runtime_model_from_json(encoded),
        Catch::Matchers::ContainsSubstring("cannot use the main model repository"));

    // Disabled legacy MTP settings must remain loadable for safe migration.
    encoded["speculative_enabled"] = false;
    CHECK_NOTHROW(engcoder::model::runtime_model_from_json(encoded));
}

TEST_CASE("legacy self-referential speculative defaults migrate to a safe disabled state") {
    TempTree temp{unique_temp_root("engcoder-speculative-migration")};
    const auto data_root = temp.root / "state";
    const auto manifest_dir = data_root / "models" / "runtimes" / "qwen3.8-flash-next-ud-q2-k-xl";
    std::filesystem::create_directories(manifest_dir);

    engcoder::model::RuntimeModelProfile legacy{
        .id = "qwen3.8-flash-next-ud-q2-k-xl",
        .display_name = "Qwen3.8 Flash Next · UD-Q2_K_XL",
        .runtime = engcoder::model::RuntimeKind::qwen_flash,
        .source = engcoder::model::ModelSourceKind::hugging_face,
        .repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
        .quant = "UD-Q2_K_XL",
        .profile_revision = 7U,
        .speculative_enabled = true,
        .speculative_type = "draft-mtp",
        .speculative_repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
        .speculative_model = {},
        .speculative_tokens = 2U,
    };
    {
        std::ofstream output(manifest_dir / "manifest.json", std::ios::binary | std::ios::trunc);
        output << engcoder::model::to_json(legacy).dump(2) << '\n';
    }

    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = data_root;
    config.model_root = data_root / "models";
    config.model_cache_root = data_root / "model-cache";
    config.qwen_flash_enabled = false;

    engcoder::model::NativeModelManager manager(config, data_root);
    const auto migrated = manager.find("qwenflash://qwen3.8-flash-next-ud-q2-k-xl");
    REQUIRE(migrated.has_value());
    CHECK(migrated->profile_revision == 9U);
    CHECK_FALSE(migrated->speculative_enabled);
    CHECK(migrated->speculative_repo.empty());
    CHECK(migrated->speculative_model.empty());
}

TEST_CASE("legacy invalid speculative setting preserves tuned built-in and custom profiles") {
    TempTree temp{unique_temp_root("engcoder-profile-migration-preserve")};
    const auto data_root = temp.root / "state";
    const auto catalog = data_root / "models" / "runtimes";
    const std::string built_id = "qwen3.8-flash-next-ud-q2-k-xl";
    const std::string custom_id = "user-custom-qwen";
    std::filesystem::create_directories(catalog / built_id);
    std::filesystem::create_directories(catalog / custom_id);

    auto write_legacy = [&](std::string_view id, std::string_view name) {
        engcoder::model::RuntimeModelProfile profile{
            .id = std::string{id},
            .display_name = std::string{name},
            .runtime = engcoder::model::RuntimeKind::qwen_flash,
            .source = engcoder::model::ModelSourceKind::hugging_face,
            .repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
            .quant = "UD-Q2_K_XL",
            .profile_revision = 7U,
            .runtime_policy = "manual",
            .gpu_offload_percent = 61U,
            .context_tokens = 65'536U,
            .speculative_enabled = true,
            .speculative_type = "draft-mtp",
            .speculative_repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
        };
        std::ofstream output(catalog / std::string{id} / "manifest.json", std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << engcoder::model::to_json(profile).dump(2) << '\n';
    };
    write_legacy(built_id, "Tuned built-in Qwen");
    write_legacy(custom_id, "Custom Qwen");

    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = data_root;
    config.model_root = data_root / "models";
    config.model_cache_root = data_root / "model-cache";
    config.qwen_flash_enabled = false;
    engcoder::model::NativeModelManager manager(config, data_root);
    for (std::string_view id : {std::string_view{built_id}, std::string_view{custom_id}}) {
        auto migrated = manager.find(std::string{"qwenflash://"} + std::string{id});
        REQUIRE(migrated.has_value());
        CHECK(migrated->runtime_policy == "manual");
        CHECK(migrated->gpu_offload_percent == 100U); // Old 61% exceeds new old-60% ceiling.
        CHECK(migrated->context_tokens == 65'536U);
        CHECK(migrated->profile_revision == 9U);
        CHECK_FALSE(migrated->speculative_enabled);
        CHECK(migrated->speculative_repo.empty());
        std::ifstream persisted(catalog / std::string{id} / "manifest.json");
        REQUIRE(persisted.good());
        const auto saved = nlohmann::json::parse(persisted);
        CHECK(saved.value("gpu_offload_percent", 0U) == 100U);
        CHECK(saved.value("speculative_enabled", true) == false);
    }
}

TEST_CASE("legacy GPU offload scale is migrated once without resetting custom context") {
    TempTree temp{unique_temp_root("engcoder-gpu-percent-v9-migration")};
    const auto data_root = temp.root / "state";
    const auto models_dir = data_root / "models" / "runtimes";
    const std::string builtin = "qwen3.8-flash-next-ud-q2-k-xl";
    const std::string custom = "custom-offload-55";
    for (const auto& id : {builtin, custom}) {
        std::filesystem::create_directories(models_dir / id);
        engcoder::model::RuntimeModelProfile profile{
            .id = id,
            .display_name = id,
            .runtime = engcoder::model::RuntimeKind::qwen_flash,
            .source = engcoder::model::ModelSourceKind::hugging_face,
            .repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
            .quant = "UD-Q2_K_XL",
            .profile_revision = 8U,
            .runtime_policy = id == builtin ? "recommended" : "manual",
            .gpu_offload_percent = id == builtin ? 36U : 55U,
            .gpu_layer_count_hint = 48U,
            .context_tokens = 65'536U,
        };
        std::ofstream output(models_dir / id / "manifest.json", std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << engcoder::model::to_json(profile).dump(2) << '\n';
    }
    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = data_root;
    config.model_root = data_root / "models";
    config.model_cache_root = data_root / "model-cache";
    config.qwen_flash_enabled = false;

    {
        engcoder::model::NativeModelManager manager(config, data_root);
        const auto default_model = manager.find("qwenflash://" + builtin);
        const auto tuned_model = manager.find("qwenflash://" + custom);
        REQUIRE(default_model.has_value());
        REQUIRE(tuned_model.has_value());
        CHECK(default_model->gpu_offload_percent == 50U); // Only old-format untouched presets migrate to the current default.
        CHECK(tuned_model->gpu_offload_percent == 92U); // 55% old ≈ 92% of the new 60% ceiling.
        CHECK(default_model->profile_revision == 9U);
        CHECK(tuned_model->profile_revision == 9U);
        CHECK(default_model->context_tokens == 65'536U);
        CHECK(tuned_model->context_tokens == 65'536U);
        CHECK(engcoder::model::effective_gpu_layers(*default_model) == "14");
        CHECK(engcoder::model::effective_gpu_layers(*tuned_model) == "26");
    }
    // Migration must not apply twice after an app restart.
    engcoder::model::NativeModelManager reopened(config, data_root);
    const auto default_again = reopened.find("qwenflash://" + builtin);
    const auto tuned_again = reopened.find("qwenflash://" + custom);
    REQUIRE(default_again.has_value());
    REQUIRE(tuned_again.has_value());
    CHECK(default_again->gpu_offload_percent == 50U);
    CHECK(tuned_again->gpu_offload_percent == 92U);
}

TEST_CASE("0.7.1 keeps saved Qwen GPU tuning when the default changes") {
    TempTree temp{unique_temp_root("engcoder-gpu-v071-existing")};
    const auto data_root = temp.root / "state";
    const auto id = std::string{"qwen3.8-flash-next-ud-q2-k-xl"};
    const auto catalog = data_root / "models" / "runtimes" / id;
    std::filesystem::create_directories(catalog);

    engcoder::model::RuntimeModelProfile saved{
        .id = id,
        .display_name = "Existing custom GPU model",
        .runtime = engcoder::model::RuntimeKind::qwen_flash,
        .source = engcoder::model::ModelSourceKind::hugging_face,
        .repo = "unsloth/Qwen3.8-Flash-Next-GGUF",
        .quant = "UD-Q2_K_XL",
        .profile_revision = engcoder::model::runtime_profile_revision,
        .runtime_policy = "manual",
        .gpu_offload_percent = 99U,
        .gpu_layer_count_hint = 48U,
        .context_tokens = 81'920U,
    };
    const auto manifest_path = catalog / "manifest.json";
    const auto saved_json = engcoder::model::to_json(saved);
    {
        std::ofstream output(manifest_path, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << saved_json.dump(2) << '\n';
    }
    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = data_root;
    config.model_root = data_root / "models";
    config.model_cache_root = data_root / "model-cache";
    config.qwen_flash_enabled = false;

    for (int startup = 0; startup < 2; ++startup) {
        engcoder::model::NativeModelManager manager(config, data_root);
        const auto loaded = manager.find("qwenflash://" + id);
        REQUIRE(loaded.has_value());
        CHECK(loaded->gpu_offload_percent == 99U);
        CHECK(engcoder::model::effective_gpu_layers(*loaded) == "29");
        std::ifstream persisted(manifest_path);
        REQUIRE(persisted.good());
        CHECK(nlohmann::json::parse(persisted) == saved_json);
    }
}

TEST_CASE("managed controller and foundation URIs are not invented as external models") {
    TempTree temp{unique_temp_root("engcoder-managed-model-availability")};
    std::filesystem::create_directories(temp.root);

    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = temp.root / "state";
    config.model = "engcoder://keymaker";
    config.controller_model = "engcoder://architect";

    engcoder::models::ModelRegistry registry(config, config.data_root);
    CHECK(registry.foundation_model() == "auto");
    CHECK(registry.controller_model().empty());

    for (const auto& component : registry.components()) {
        const bool managed_uri_exposed_as_external =
            component.origin == engcoder::models::ComponentOrigin::external_provider &&
            (component.provider_model == "engcoder://keymaker" ||
             component.provider_model == "engcoder://architect");
        CHECK_FALSE(managed_uri_exposed_as_external);
    }
}


TEST_CASE("local GGUF override normalizes quoted split paths and rejects projectors") {
    TempTree temp{unique_temp_root("engcoder-local-override")};
    std::filesystem::create_directories(temp.root);

    const auto model_dir = temp.root / "models";
    std::filesystem::create_directories(model_dir);
    const auto shard1 = model_dir / "Test-UD-Q2_K_XL-00001-of-00002.gguf";
    const auto shard2 = model_dir / "Test-UD-Q2_K_XL-00002-of-00002.gguf";
    {
        std::ofstream out1(shard1, std::ios::binary);
        std::ofstream out2(shard2, std::ios::binary);
        out1 << "first-shard";
        out2 << "second-shard";
    }

    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = temp.root / "state";
    config.model_root = config.data_root / "models";
    config.model_cache_root = config.data_root / "model-cache";
    config.qwen_flash_enabled = false;

    engcoder::model::NativeModelManager manager(config, config.data_root);
    const auto quoted_second = std::string{"\""} + shard2.string() + "\"";
    auto registered = manager.register_model({
        {"id", "local-override"},
        {"display_name", "Local Override"},
        {"source", "local-gguf"},
        {"local_path", quoted_second},
    });
    REQUIRE(registered.has_value());

    const auto profile = manager.find("qwenflash://local-override");
    REQUIRE(profile.has_value());
    CHECK(profile->source == engcoder::model::ModelSourceKind::local_gguf);
    CHECK(profile->local_path.lexically_normal() == std::filesystem::absolute(shard1).lexically_normal());

    const auto mmproj = model_dir / "mmproj-BF16.gguf";
    {
        std::ofstream output(mmproj, std::ios::binary);
        output << "projector";
    }
    auto rejected = manager.register_model({
        {"id", "not-a-model"},
        {"source", "local-gguf"},
        {"local_path", mmproj.string()},
    });
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().message.find("mmproj") != std::string::npos);
}

TEST_CASE("unchanged native model manifests do not get rewritten at startup") {
    TempTree temp{unique_temp_root("engcoder-profile-noop-startup")};
    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = temp.root / "state";
    config.model_root = config.data_root / "models";
    config.model_cache_root = config.data_root / "model-cache";
    config.qwen_flash_enabled = false;

    const auto manifest = config.data_root / "models" / "runtimes" /
        "qwen3.8-flash-next-ud-q2-k-xl" / "manifest.json";
    {
        engcoder::model::NativeModelManager manager(config, config.data_root);
        REQUIRE(manager.find("qwenflash://qwen3.8-flash-next-ud-q2-k-xl").has_value());
    }
    REQUIRE(std::filesystem::exists(manifest));
    // Deliberately set a marker older than the current filesystem clock. A
    // pointless startup rewrite would replace this timestamp with now.
    const auto sentinel = std::filesystem::file_time_type::clock::now() - std::chrono::hours(2);
    std::filesystem::last_write_time(manifest, sentinel);
    {
        engcoder::model::NativeModelManager reopened(config, config.data_root);
        REQUIRE(reopened.find("qwenflash://qwen3.8-flash-next-ud-q2-k-xl").has_value());
    }
    CHECK(std::filesystem::last_write_time(manifest) == sentinel);
}

TEST_CASE("broken built-in manifest is preserved instead of being overwritten with defaults") {
    TempTree temp{unique_temp_root("engcoder-builtin-profile-corruption")};
    engcoder::core::Config config;
    config.workspace = temp.root / "workspace";
    config.data_root = temp.root / "state";
    config.model_root = config.data_root / "models";
    config.model_cache_root = config.data_root / "model-cache";
    config.qwen_flash_enabled = false;

    const auto manifest = config.data_root / "models" / "runtimes" /
        "qwen3.8-flash-next-ud-q2-k-xl" / "manifest.json";
    std::filesystem::create_directories(manifest.parent_path());
    const std::string corrupted = "{invalid-but-must-be-preserved";
    {
        std::ofstream output(manifest, std::ios::binary);
        output << corrupted;
    }
    {
        engcoder::model::NativeModelManager manager(config, config.data_root);
        CHECK_FALSE(manager.find("qwenflash://qwen3.8-flash-next-ud-q2-k-xl").has_value());
    }
    std::ifstream persisted(manifest, std::ios::binary);
    REQUIRE(persisted.good());
    CHECK(std::string(std::istreambuf_iterator<char>(persisted), {}) == corrupted);
}
