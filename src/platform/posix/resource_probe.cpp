module;
#include "module_prelude.hpp"
#include <algorithm>
#include <sys/sysinfo.h>
#include <unistd.h>

module threadspire.platform;

namespace threadspire::ai::resource {

ResourceSnapshot sample_system_resources() noexcept {
    ResourceSnapshot snapshot;
    struct sysinfo information {};
    if (::sysinfo(&information) == 0) {
        const auto bytes = static_cast<long double>(information.freeram) *
                           static_cast<long double>(information.mem_unit);
        snapshot.memory_available_gib = static_cast<double>(bytes / (1024.0L * 1024.0L * 1024.0L));
    }
    double load = 0.0;
    if (::getloadavg(&load, 1) == 1) {
        const auto processors = std::max<long>(1, ::sysconf(_SC_NPROCESSORS_ONLN));
        snapshot.cpu_utilization = std::clamp(load / static_cast<double>(processors), 0.0, 1.0);
    }
    snapshot.captured_at = std::chrono::steady_clock::now();
    return snapshot;
}

} // namespace threadspire::ai::resource
