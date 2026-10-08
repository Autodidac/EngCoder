#include "engcoder/residency/model_residency_manager.hpp"

#include <algorithm>
#include <cmath>

namespace engcoder::residency {
namespace {
constexpr double gib = 1024.0 * 1024.0 * 1024.0;

[[nodiscard]] bool consumes_vram(ResidencyState state) noexcept {
    return state == ResidencyState::loading || state == ResidencyState::vram ||
           state == ResidencyState::in_use || state == ResidencyState::training;
}
}

std::string_view to_string(ResidencyState state) noexcept {
    switch (state) {
        case ResidencyState::disk: return "disk";
        case ResidencyState::ram: return "ram";
        case ResidencyState::loading: return "loading";
        case ResidencyState::vram: return "vram";
        case ResidencyState::in_use: return "in-use";
        case ResidencyState::evicting: return "evicting";
        case ResidencyState::training: return "training";
        case ResidencyState::failed: return "failed";
    }
    return "disk";
}

ModelResidencyManager::ModelResidencyManager(double vram_budget_gib)
    : budget_bytes_(static_cast<std::uint64_t>(std::max(0.5, vram_budget_gib) * gib)) {}

void ModelResidencyManager::sync(const models::ModelRegistry& registry) {
    std::scoped_lock lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    std::unordered_map<std::string, ResidencyEntry> next;
    for (const auto& component : registry.components()) {
        auto entry = entries_.contains(component.id) ? entries_.at(component.id) : ResidencyEntry{};
        entry.component_id = component.id;
        entry.estimated_bytes = component.estimated_bytes;
        entry.pinned = component.pinned;
        if (entry.last_used.time_since_epoch().count() == 0) entry.last_used = now;
        if (component.pinned && entry.state == ResidencyState::disk) entry.state = ResidencyState::ram;
        next.emplace(component.id, std::move(entry));
    }
    entries_ = std::move(next);
    enforce_budget_locked();
}

void ModelResidencyManager::touch(std::string_view component_id, ResidencyState desired) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string{component_id});
    if (it == entries_.end()) return;
    it->second.last_used = std::chrono::steady_clock::now();
    if (it->second.state != ResidencyState::training && it->second.state != ResidencyState::in_use) {
        it->second.state = desired;
    }
    enforce_budget_locked();
}

void ModelResidencyManager::begin_use(std::string_view component_id) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string{component_id});
    if (it == entries_.end()) return;
    ++it->second.use_count;
    it->second.last_used = std::chrono::steady_clock::now();
    it->second.state = ResidencyState::in_use;
    enforce_budget_locked();
}

void ModelResidencyManager::end_use(std::string_view component_id) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string{component_id});
    if (it == entries_.end()) return;
    if (it->second.use_count > 0) --it->second.use_count;
    it->second.last_used = std::chrono::steady_clock::now();
    if (it->second.use_count == 0 && it->second.state == ResidencyState::in_use) {
        it->second.state = ResidencyState::vram;
    }
    enforce_budget_locked();
}

void ModelResidencyManager::mark_training(std::string_view component_id, bool active) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string{component_id});
    if (it == entries_.end()) return;
    it->second.state = active ? ResidencyState::training : (it->second.pinned ? ResidencyState::vram : ResidencyState::ram);
    it->second.last_used = std::chrono::steady_clock::now();
    enforce_budget_locked();
}

void ModelResidencyManager::mark_failed(std::string_view component_id) {
    std::scoped_lock lock(mutex_);
    const auto it = entries_.find(std::string{component_id});
    if (it == entries_.end()) return;
    it->second.state = ResidencyState::failed;
    it->second.use_count = 0;
}

std::uint64_t ModelResidencyManager::resident_bytes_locked() const {
    std::uint64_t total = 0;
    for (const auto& [_, entry] : entries_) {
        if (consumes_vram(entry.state)) total += entry.estimated_bytes;
    }
    return total;
}

void ModelResidencyManager::enforce_budget_locked() {
    auto resident = resident_bytes_locked();
    if (resident <= budget_bytes_) return;

    std::vector<ResidencyEntry*> candidates;
    for (auto& [_, entry] : entries_) {
        if (entry.pinned || entry.use_count != 0 || entry.state == ResidencyState::training) continue;
        if (entry.state == ResidencyState::vram || entry.state == ResidencyState::loading) {
            candidates.push_back(&entry);
        }
    }
    std::ranges::sort(candidates, {}, &ResidencyEntry::last_used);
    for (auto* entry : candidates) {
        if (resident <= budget_bytes_) break;
        entry->state = ResidencyState::ram;
        resident = resident >= entry->estimated_bytes ? resident - entry->estimated_bytes : 0;
    }
}

json ModelResidencyManager::describe() const {
    std::scoped_lock lock(mutex_);
    std::vector<json> items;
    items.reserve(entries_.size());
    for (const auto& [_, entry] : entries_) {
        items.push_back({
            {"component_id", entry.component_id},
            {"state", to_string(entry.state)},
            {"estimated_bytes", entry.estimated_bytes},
            {"pinned", entry.pinned},
            {"use_count", entry.use_count},
        });
    }
    std::ranges::sort(items, [](const json& a, const json& b) {
        return a.value("component_id", std::string{}) < b.value("component_id", std::string{});
    });
    return json{
        {"policy", "pinned-controller-foundation+lru-addons"},
        {"vram_budget_bytes", budget_bytes_},
        {"resident_bytes", resident_bytes_locked()},
        {"entries", std::move(items)},
    };
}

} // namespace engcoder::residency
