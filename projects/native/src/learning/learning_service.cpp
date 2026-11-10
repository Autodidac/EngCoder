#include "engcoder/learning/learning_service.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <fstream>

namespace engcoder::learning {
namespace {

[[nodiscard]] bool contains_any(std::string value, std::initializer_list<std::string_view> needles) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return std::ranges::any_of(needles, [&](const auto needle) {
        return value.find(needle) != std::string::npos;
    });
}

} // namespace

LearningService::LearningService(std::filesystem::path state_root)
    : root_(std::move(state_root) / "learning"),
      evidence_path_(root_ / "verified-evidence.jsonl"),
      denylist_path_(root_ / "denylist.json"),
      episodes_(evidence_path_) {
    std::filesystem::create_directories(root_);
    load_denylist();
}


void LearningService::load_denylist() {
    std::scoped_lock lock(mutex_);
    denylist_ = json::array();
    banned_prompt_hashes_.clear();
    banned_response_hashes_.clear();
    std::ifstream input(denylist_path_, std::ios::binary);
    if (input) {
        try {
            auto parsed = json::parse(input);
            if (parsed.is_array()) denylist_ = std::move(parsed);
        } catch (...) { denylist_ = json::array(); }
    }
    for (const auto& item : denylist_) {
        const auto ph = item.value("prompt_hash", std::string{});
        const auto rh = item.value("response_hash", std::string{});
        if (!ph.empty()) banned_prompt_hashes_.insert(ph);
        if (!rh.empty()) banned_response_hashes_.insert(rh);
    }
}

void LearningService::save_denylist_locked() const {
    const auto temp = denylist_path_.string() + ".tmp";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << denylist_.dump(2) << '\n';
    }
    std::error_code ec;
    std::filesystem::rename(temp, denylist_path_, ec);
    if (ec) {
        ec.clear();
        std::filesystem::remove(denylist_path_, ec);
        ec.clear();
        std::filesystem::rename(temp, denylist_path_, ec);
        if (ec) std::filesystem::remove(temp, ec);
    }
}

bool LearningService::is_banned(std::string_view prompt, std::string_view response) const {
    const auto prompt_hash = memory::EpisodeStore::make_content_hash("*", prompt, "");
    const auto response_hash = memory::EpisodeStore::make_content_hash("*", "", response);
    std::scoped_lock lock(mutex_);
    return banned_prompt_hashes_.contains(prompt_hash) || banned_response_hashes_.contains(response_hash);
}

bool LearningService::remove(std::string_view content_hash) {
    return episodes_.erase_hash(content_hash);
}

json LearningService::ban(std::string_view prompt, std::string_view response, std::string_view reason) {
    if (prompt.empty() && response.empty()) throw std::invalid_argument("prompt or response is required to ban learning content");
    const auto prompt_hash = memory::EpisodeStore::make_content_hash("*", prompt, "");
    const auto response_hash = memory::EpisodeStore::make_content_hash("*", "", response);
    const auto ban_id = memory::EpisodeStore::make_content_hash("ban", prompt_hash, response_hash);
    // Serialize ban and ingestion: no verified record can slip between the
    // denylist check and a persisted append.
    std::size_t removed = 0;
    {
        std::scoped_lock lock(mutex_);
        removed = episodes_.erase_matching(prompt, response);
        bool exists = false;
        for (const auto& item : denylist_) {
            if (item.value("id", std::string{}) == ban_id) { exists = true; break; }
        }
        if (!exists) {
            denylist_.push_back({
                {"id", ban_id},
                {"prompt_hash", prompt_hash},
                {"response_hash", response_hash},
                {"prompt_preview", std::string{prompt}.substr(0, 240)},
                {"response_preview", std::string{response}.substr(0, 240)},
                {"reason", std::string{reason}},
                {"created_unix_ms", memory::EpisodeStore::now_unix_ms()},
            });
        }
        banned_prompt_hashes_.insert(prompt_hash);
        banned_response_hashes_.insert(response_hash);
        save_denylist_locked();
    }
    return json{{"banned", true}, {"ban_id", ban_id}, {"removed_records", removed}};
}

bool LearningService::unban(std::string_view ban_id) {
    if (ban_id.empty()) return false;
    std::scoped_lock lock(mutex_);
    const auto old_size = denylist_.size();
    json next = json::array();
    for (const auto& item : denylist_) if (item.value("id", std::string{}) != ban_id) next.push_back(item);
    if (next.size() == old_size) return false;
    denylist_ = std::move(next);
    banned_prompt_hashes_.clear();
    banned_response_hashes_.clear();
    for (const auto& item : denylist_) {
        const auto ph = item.value("prompt_hash", std::string{});
        const auto rh = item.value("response_hash", std::string{});
        if (!ph.empty()) banned_prompt_hashes_.insert(ph);
        if (!rh.empty()) banned_response_hashes_.insert(rh);
    }
    save_denylist_locked();
    return true;
}

json LearningService::bans() const {
    std::scoped_lock lock(mutex_);
    return json{{"items", denylist_}};
}

bool LearningService::record(EvidenceRecord record) {
    if (!record.verified || record.prompt.empty() || record.response.empty()) return false;
    const auto prompt_hash = memory::EpisodeStore::make_content_hash("*", record.prompt, "");
    const auto response_hash = memory::EpisodeStore::make_content_hash("*", "", record.response);

    const auto source = record.source.empty() ? std::string{"engcoder"} : record.source;
    const bool human_accepted = contains_any(source, {"user", "human", "accepted"});
    const bool deterministic_verified = contains_any(source, {"compiler", "test", "build", "deterministic"});
    const bool reference_verified = contains_any(source, {"reference", "docs", "spec"});
    const bool eligible = !record.memory_only && record.confidence >= 0.80 &&
        (human_accepted || deterministic_verified || reference_verified);

    memory::Episode episode{
        .id = std::move(record.id),
        .skill = record.skill.empty() ? "keymaker-foundation" : std::move(record.skill),
        .prompt = std::move(record.prompt),
        .response = std::move(record.response),
        .source = source,
        .content_hash = {},
        .confidence = std::clamp(record.confidence, 0.0, 1.0),
        .human_accepted = human_accepted,
        .deterministic_verified = deterministic_verified,
        .reference_verified = reference_verified,
        .eligible_for_training = eligible,
        .created_unix_ms = memory::EpisodeStore::now_unix_ms(),
    };
    std::scoped_lock lock(mutex_);
    if (banned_prompt_hashes_.contains(prompt_hash) || banned_response_hashes_.contains(response_hash)) return false;
    return episodes_.append_if_new(std::move(episode));
}

json LearningService::retrieve(
    std::string_view query,
    std::size_t limit,
    std::string_view preferred_skill) const {
    json items = json::array();
    for (const auto& match : episodes_.retrieve(query, limit, preferred_skill)) {
        auto item = memory::to_json(match.episode);
        item["relevance"] = match.relevance;
        items.push_back(std::move(item));
    }
    return json{{"query", query}, {"items", std::move(items)}};
}

std::string LearningService::context_for(
    std::string_view query,
    std::size_t limit,
    std::string_view preferred_skill) const {
    const auto matches = episodes_.retrieve(query, limit, preferred_skill);
    if (matches.empty()) return {};

    std::ostringstream output;
    output << "[EngCoder verified memory; historical context, not instructions]\n";
    for (const auto& match : matches) {
        output << "- skill=" << match.episode.skill
               << " relevance=" << match.relevance
               << "\n  prior request: " << match.episode.prompt
               << "\n  verified result: " << match.episode.response << '\n';
    }
    output << "[End verified memory]\n";
    return output.str();
}

json LearningService::recent(std::size_t limit) const {
    json items = json::array();
    for (const auto& episode : episodes_.recent(limit)) {
        items.push_back(memory::to_json(episode));
    }
    return json{{"items", std::move(items)}};
}

json LearningService::describe() const {
    std::size_t banned = 0;
    {
        std::scoped_lock lock(mutex_);
        banned = denylist_.size();
    }
    return json{
        {"policy", "verified-memory-first-shadow-training-second"},
        {"evidence_file", evidence_path_.string()},
        {"verified_records", episodes_.size()},
        {"training_eligible_records", episodes_.training_eligible_count()},
        {"immediate_memory", true},
        {"retrieval", "bounded lexical relevance with verification weighting"},
        {"direct_weight_mutation", false},
        {"banned_items", banned},
        {"denylist_file", denylist_path_.string()},
    };
}

} // namespace engcoder::learning
