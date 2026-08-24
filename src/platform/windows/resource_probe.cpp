#include "threadspire/ai/resource/resource_governor.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <mutex>

namespace threadspire::ai::resource {
namespace {

[[nodiscard]] std::uint64_t ticks(const FILETIME value) noexcept {
    ULARGE_INTEGER converted{};
    converted.LowPart = value.dwLowDateTime;
    converted.HighPart = value.dwHighDateTime;
    return converted.QuadPart;
}

} // namespace

ResourceSnapshot sample_system_resources() noexcept {
    ResourceSnapshot snapshot;
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory) != FALSE) {
        snapshot.memory_available_gib = static_cast<double>(memory.ullAvailPhys) /
                                        (1024.0 * 1024.0 * 1024.0);
    }

    static std::mutex sample_mutex;
    static std::uint64_t previous_idle = 0;
    static std::uint64_t previous_total = 0;
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetSystemTimes(&idle, &kernel, &user) != FALSE) {
        std::lock_guard lock(sample_mutex);
        const auto current_idle = ticks(idle);
        const auto current_total = ticks(kernel) + ticks(user);
        if (previous_total != 0 && current_total > previous_total) {
            const auto total_delta = current_total - previous_total;
            const auto idle_delta = current_idle - previous_idle;
            snapshot.cpu_utilization = std::clamp(
                1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta),
                0.0,
                1.0);
        }
        previous_idle = current_idle;
        previous_total = current_total;
    }

    SYSTEM_POWER_STATUS power{};
    if (::GetSystemPowerStatus(&power) != FALSE) {
        snapshot.on_ac_power = power.ACLineStatus != 0;
    }
    snapshot.captured_at = std::chrono::steady_clock::now();
    return snapshot;
}

} // namespace threadspire::ai::resource
