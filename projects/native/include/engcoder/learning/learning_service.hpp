#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "engcoder/memory/episode_store.hpp"

namespace engcoder::learning {

using json = nlohmann::json;

struct EvidenceRecord {
    std::string id;
    std::string skill;
    std::string prompt;
    std::string response;
    std::string source;
    double confidence{1.0};
    bool verified{true};
    bool memory_only{false};
};

class LearningService {
public:
    explicit LearningService(std::filesystem::path state_root);

    [[nodiscard]] bool record(EvidenceRecord record);
    [[nodiscard]] bool remove(std::string_view content_hash);
    [[nodiscard]] json ban(std::string_view prompt, std::string_view response, std::string_view reason = "user-banned");
    [[nodiscard]] bool unban(std::string_view ban_id);
    [[nodiscard]] json bans() const;
    [[nodiscard]] bool is_banned(std::string_view prompt, std::string_view response) const;
    [[nodiscard]] json retrieve(
        std::string_view query,
        std::size_t limit = 3,
        std::string_view preferred_skill = {}) const;
    [[nodiscard]] std::string context_for(
        std::string_view query,
        std::size_t limit = 3,
        std::string_view preferred_skill = {}) const;
    [[nodiscard]] json recent(std::size_t limit = 20) const;
    [[nodiscard]] json describe() const;
    [[nodiscard]] const std::filesystem::path& evidence_path() const noexcept { return evidence_path_; }

private:
    std::filesystem::path root_;
    std::filesystem::path evidence_path_;
    std::filesystem::path denylist_path_;
    mutable std::mutex mutex_;
    memory::EpisodeStore episodes_;
    json denylist_ = json::array();
    std::unordered_set<std::string> banned_prompt_hashes_;
    std::unordered_set<std::string> banned_response_hashes_;
    void load_denylist();
    void save_denylist_locked() const;
};

} // namespace engcoder::learning
