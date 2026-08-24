#include "threadspire/ai/core/ini.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <stdexcept>

namespace threadspire::ai::core {
namespace {

[[nodiscard]] std::string trim(std::string value) {
    const auto not_space = [](const unsigned char character) {
        return std::isspace(character) == 0;
    };

    const auto first = std::find_if(value.begin(), value.end(), not_space);
    if (first == value.end()) {
        return {};
    }

    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return std::string(first, last);
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

IniFile IniFile::load(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("unable to open INI file: " + path.string());
    }

    IniFile result;
    std::string section;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        line = trim(std::move(line));
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                throw std::runtime_error(
                    "malformed section at " + path.string() + ':' + std::to_string(line_number));
            }
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        const auto delimiter = line.find('=');
        if (delimiter == std::string::npos) {
            throw std::runtime_error(
                "malformed key/value at " + path.string() + ':' + std::to_string(line_number));
        }

        auto key = trim(line.substr(0, delimiter));
        auto value = trim(line.substr(delimiter + 1));
        if (key.empty()) {
            throw std::runtime_error(
                "empty key at " + path.string() + ':' + std::to_string(line_number));
        }

        result.sections_[section].insert_or_assign(std::move(key), std::move(value));
    }

    return result;
}

bool IniFile::contains(const std::string_view section, const std::string_view key) const {
    const auto section_iterator = sections_.find(std::string(section));
    if (section_iterator == sections_.end()) {
        return false;
    }
    return section_iterator->second.contains(std::string(key));
}

std::string IniFile::get_string(
    const std::string_view section,
    const std::string_view key,
    const std::string_view fallback) const {
    const auto section_iterator = sections_.find(std::string(section));
    if (section_iterator == sections_.end()) {
        return std::string(fallback);
    }

    const auto value_iterator = section_iterator->second.find(std::string(key));
    if (value_iterator == section_iterator->second.end()) {
        return std::string(fallback);
    }

    return value_iterator->second;
}

bool IniFile::get_bool(
    const std::string_view section,
    const std::string_view key,
    const bool fallback) const {
    if (!contains(section, key)) {
        return fallback;
    }

    const auto value = lower_ascii(get_string(section, key));
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        return false;
    }

    throw std::runtime_error(
        "invalid boolean value for [" + std::string(section) + "] " + std::string(key));
}

std::int64_t IniFile::get_integer(
    const std::string_view section,
    const std::string_view key,
    const std::int64_t fallback) const {
    if (!contains(section, key)) {
        return fallback;
    }

    const auto text = get_string(section, key);
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::runtime_error(
            "invalid integer value for [" + std::string(section) + "] " + std::string(key));
    }
    return value;
}

double IniFile::get_number(
    const std::string_view section,
    const std::string_view key,
    const double fallback) const {
    if (!contains(section, key)) {
        return fallback;
    }

    const auto text = get_string(section, key);
    std::size_t consumed = 0;
    const auto value = std::stod(text, &consumed);
    if (consumed != text.size()) {
        throw std::runtime_error(
            "invalid numeric value for [" + std::string(section) + "] " + std::string(key));
    }
    return value;
}

std::filesystem::path resolve_from(
    const std::filesystem::path& base,
    const std::filesystem::path& candidate) {
    if (candidate.empty()) {
        return {};
    }
    if (candidate.is_absolute()) {
        return candidate.lexically_normal();
    }
    return (base / candidate).lexically_normal();
}

} // namespace threadspire::ai::core
