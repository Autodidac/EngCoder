#pragma once

#include "threadspire/ai/models/model_registry.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace threadspire::ai::models {

struct LocalServerProfile final {
    std::string id;
    std::string display_name;
    std::string base_url;
    std::string api_key;
    bool enabled{true};
    bool teacher{true};
    bool verifier{false};
    bool coding_agent{true};
};

struct DiscoveredLocalModel final {
    std::string server_id;
    std::string server_name;
    std::string base_url;
    std::string api_key;
    std::string model_id;
    bool teacher{true};
    bool verifier{false};
    bool coding_agent{true};
};

struct LocalServerStatus final {
    LocalServerProfile server;
    bool reachable{false};
    std::string message;
    std::vector<DiscoveredLocalModel> models;
};

struct LocalDiscoveryReport final {
    std::vector<LocalServerStatus> servers;
    std::vector<DiscoveredLocalModel> models;
};

class LocalServerHub final {
public:
    [[nodiscard]] static LocalServerHub load(const std::filesystem::path& path);
    [[nodiscard]] static LocalServerHub defaults();

    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<LocalServerProfile>& servers() const noexcept;
    [[nodiscard]] std::vector<LocalServerProfile>& servers() noexcept;

    [[nodiscard]] LocalDiscoveryReport discover(
        const std::function<bool()>& should_stop = {}) const;

    [[nodiscard]] static ModelProfile make_model_profile(
        const DiscoveredLocalModel& discovered,
        std::size_t ordinal);

private:
    std::vector<LocalServerProfile> servers_;
};

[[nodiscard]] std::string normalize_local_base_url(std::string_view url);
[[nodiscard]] std::string openai_models_url(std::string_view base_url);
[[nodiscard]] std::string openai_chat_url(std::string_view base_url);

} // namespace threadspire::ai::models
