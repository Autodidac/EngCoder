#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace threadspire::ai::core::json {

class Value final {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;

    Value() noexcept;
    Value(std::nullptr_t) noexcept;
    Value(bool value) noexcept;
    Value(double value) noexcept;
    Value(int value) noexcept;
    Value(std::string value);
    Value(std::string_view value);
    Value(const char* value);
    Value(Array value);
    Value(Object value);

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] bool is_bool() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;

    [[nodiscard]] bool as_bool(bool fallback = false) const noexcept;
    [[nodiscard]] double as_number(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::string_view as_string(std::string_view fallback = {}) const noexcept;
    [[nodiscard]] const Array* as_array() const noexcept;
    [[nodiscard]] Array* as_array() noexcept;
    [[nodiscard]] const Object* as_object() const noexcept;
    [[nodiscard]] Object* as_object() noexcept;

    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
    [[nodiscard]] Value* find(std::string_view key) noexcept;
    [[nodiscard]] const Value* at(std::size_t index) const noexcept;
    [[nodiscard]] Value* at(std::size_t index) noexcept;

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage storage_;
};

[[nodiscard]] Value parse(std::string_view text);
[[nodiscard]] std::optional<Value> try_parse(std::string_view text, std::string& error) noexcept;
[[nodiscard]] std::string stringify(const Value& value);
[[nodiscard]] std::string escape(std::string_view value);

[[nodiscard]] std::string string_or(
    const Value* value,
    std::string_view fallback = {});
[[nodiscard]] double number_or(const Value* value, double fallback = 0.0) noexcept;
[[nodiscard]] bool bool_or(const Value* value, bool fallback = false) noexcept;

} // namespace threadspire::ai::core::json
