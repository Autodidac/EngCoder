module;
#include <filesystem>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

export module threadspire.providers:adapters;

import :unified;
import threadspire.learning;
import threadspire.mcp;
import threadspire.models;
import threadspire.storage;

export {
namespace threadspire::ai::backend {

class AgentTaskProvider final : public TaskProvider {
public:
    AgentTaskProvider(
        models::ModelRegistry& models,
        std::vector<learning::ExpertModule> experts,
        const memory::EpisodeStore& memory,
        std::string internal_base_url,
        mcp::Client* mcp_client = nullptr);

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;
    [[nodiscard]] UnifiedTaskResult execute(
        const UnifiedTaskRequest& request,
        std::stop_token stop_token) override;

private:
    models::ModelRegistry& models_;
    std::vector<learning::ExpertModule> experts_;
    const memory::EpisodeStore& memory_;
    std::string internal_base_url_;
    mcp::Client* mcp_client_{nullptr};
};

class RepositoryTaskProvider final : public TaskProvider {
public:
    RepositoryTaskProvider(
        std::filesystem::path state_root,
        std::string internal_base_url,
        models::ModelProfile default_model = {});

    [[nodiscard]] std::string_view id() const noexcept override;
    [[nodiscard]] ProviderCapabilities capabilities() const noexcept override;
    [[nodiscard]] UnifiedTaskResult execute(
        const UnifiedTaskRequest& request,
        std::stop_token stop_token) override;

private:
    std::filesystem::path state_root_;
    std::string internal_base_url_;
    models::ModelProfile default_model_;
};

} // namespace threadspire::ai::backend
}