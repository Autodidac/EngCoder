module;
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

export module threadspire.core;

export {
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

namespace threadspire::ai::util {

enum class OperationState {
    idle,
    running,
    succeeded,
    failed,
    cancelled,
};

struct OperationResult final {
    OperationState state{OperationState::idle};
    int exit_code{-1};
    std::string message;
};

class AsyncOperation final {
public:
    using Task = std::function<OperationResult(std::stop_token)>;

    AsyncOperation() = default;
    ~AsyncOperation();

    AsyncOperation(const AsyncOperation&) = delete;
    AsyncOperation& operator=(const AsyncOperation&) = delete;
    AsyncOperation(AsyncOperation&&) = delete;
    AsyncOperation& operator=(AsyncOperation&&) = delete;

    [[nodiscard]] bool start(Task task, std::string initial_message);
    void cancel();
    void wait();
    [[nodiscard]] bool running() const;
    [[nodiscard]] OperationResult snapshot() const;
    void set_message(std::string message);

private:
    mutable std::mutex mutex_;
    OperationResult result_{};
    std::jthread worker_{};
};

[[nodiscard]] std::string_view to_string(OperationState state) noexcept;

} // namespace threadspire::ai::util
}
