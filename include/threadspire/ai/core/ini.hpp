#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace threadspire::ai::core {

class IniFile final {
public:
    [[nodiscard]] static IniFile load(const std::filesystem::path& path);

    [[nodiscard]] bool contains(std::string_view section, std::string_view key) const;
    [[nodiscard]] std::string get_string(
        std::string_view section,
        std::string_view key,
        std::string_view fallback = {}) const;
    [[nodiscard]] bool get_bool(
        std::string_view section,
        std::string_view key,
        bool fallback = false) const;
    [[nodiscard]] std::int64_t get_integer(
        std::string_view section,
        std::string_view key,
        std::int64_t fallback = 0) const;
    [[nodiscard]] double get_number(
        std::string_view section,
        std::string_view key,
        double fallback = 0.0) const;

private:
    using Section = std::unordered_map<std::string, std::string>;
    std::unordered_map<std::string, Section> sections_;
};

[[nodiscard]] std::filesystem::path resolve_from(
    const std::filesystem::path& base,
    const std::filesystem::path& candidate);

} // namespace threadspire::ai::core
