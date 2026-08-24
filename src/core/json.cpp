#include "threadspire/ai/core/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace threadspire::ai::core::json {
namespace {

class Parser final {
public:
    explicit Parser(const std::string_view input) noexcept : input_(input) {}

    [[nodiscard]] Value parse_document() {
        skip_whitespace();
        auto result = parse_value();
        skip_whitespace();
        if (position_ != input_.size()) {
            fail("unexpected trailing characters");
        }
        return result;
    }

private:
    [[nodiscard]] Value parse_value() {
        if (position_ >= input_.size()) {
            fail("unexpected end of input");
        }
        switch (input_[position_]) {
        case 'n':
            expect_literal("null");
            return Value{};
        case 't':
            expect_literal("true");
            return Value{true};
        case 'f':
            expect_literal("false");
            return Value{false};
        case '"':
            return Value{parse_string()};
        case '[':
            return Value{parse_array()};
        case '{':
            return Value{parse_object()};
        default:
            if (input_[position_] == '-' ||
                (input_[position_] >= '0' && input_[position_] <= '9')) {
                return Value{parse_number()};
            }
            fail("unexpected character");
        }
    }

    [[nodiscard]] Value::Array parse_array() {
        consume('[');
        skip_whitespace();
        Value::Array result;
        if (consume_if(']')) {
            return result;
        }
        while (true) {
            skip_whitespace();
            result.push_back(parse_value());
            skip_whitespace();
            if (consume_if(']')) {
                return result;
            }
            consume(',');
        }
    }

    [[nodiscard]] Value::Object parse_object() {
        consume('{');
        skip_whitespace();
        Value::Object result;
        if (consume_if('}')) {
            return result;
        }
        while (true) {
            skip_whitespace();
            if (position_ >= input_.size() || input_[position_] != '"') {
                fail("object key must be a string");
            }
            auto key = parse_string();
            skip_whitespace();
            consume(':');
            skip_whitespace();
            result.insert_or_assign(std::move(key), parse_value());
            skip_whitespace();
            if (consume_if('}')) {
                return result;
            }
            consume(',');
        }
    }

    [[nodiscard]] std::string parse_string() {
        consume('"');
        std::string result;
        while (position_ < input_.size()) {
            const auto character = input_[position_++];
            if (character == '"') {
                return result;
            }
            if (character == '\\') {
                if (position_ >= input_.size()) {
                    fail("unterminated escape sequence");
                }
                const auto escaped = input_[position_++];
                switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': append_unicode_escape(result); break;
                default: fail("unsupported escape sequence");
                }
                continue;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                fail("unescaped control character in string");
            }
            result.push_back(character);
        }
        fail("unterminated string");
    }

    void append_unicode_escape(std::string& output) {
        auto code_point = parse_hex_quad();
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (position_ + 2U > input_.size() || input_[position_] != '\\' ||
                input_[position_ + 1U] != 'u') {
                fail("high surrogate is missing its low surrogate");
            }
            position_ += 2U;
            const auto low = parse_hex_quad();
            if (low < 0xDC00U || low > 0xDFFFU) {
                fail("invalid low surrogate");
            }
            code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            fail("unexpected low surrogate");
        }
        append_utf8(output, code_point);
    }

    [[nodiscard]] std::uint32_t parse_hex_quad() {
        if (position_ + 4U > input_.size()) {
            fail("truncated unicode escape");
        }
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index) {
            const auto character = input_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9') {
                value += static_cast<std::uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value += static_cast<std::uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                value += static_cast<std::uint32_t>(character - 'A' + 10);
            } else {
                fail("invalid hexadecimal digit");
            }
        }
        return value;
    }

    static void append_utf8(std::string& output, const std::uint32_t code_point) {
        if (code_point <= 0x7FU) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            output.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0xFFFFU) {
            output.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0x10FFFFU) {
            output.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            throw std::runtime_error("JSON unicode code point is out of range");
        }
    }

    [[nodiscard]] double parse_number() {
        const auto begin = position_;
        if (consume_if('-') && position_ >= input_.size()) {
            fail("truncated number");
        }
        if (consume_if('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
                fail("leading zero in number");
            }
        } else {
            consume_digits(true);
        }
        if (consume_if('.')) {
            consume_digits(true);
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            static_cast<void>(consume_if('+') || consume_if('-'));
            consume_digits(true);
        }
        const auto token = input_.substr(begin, position_ - begin);
        double result = 0.0;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), result);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
            !std::isfinite(result)) {
            fail("invalid number");
        }
        return result;
    }

    void consume_digits(const bool require_one) {
        const auto begin = position_;
        while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
            ++position_;
        }
        if (require_one && position_ == begin) {
            fail("expected a digit");
        }
    }

    void expect_literal(const std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) {
            fail("invalid literal");
        }
        position_ += literal.size();
    }

    void skip_whitespace() noexcept {
        while (position_ < input_.size()) {
            const auto character = input_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n') {
                break;
            }
            ++position_;
        }
    }

    void consume(const char expected) {
        if (!consume_if(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    [[nodiscard]] bool consume_if(const char expected) noexcept {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    [[noreturn]] void fail(const std::string_view message) const {
        throw std::runtime_error(
            "JSON parse error at byte " + std::to_string(position_) + ": " + std::string(message));
    }

    std::string_view input_;
    std::size_t position_{0};
};

void append_serialized(std::string& output, const Value& value) {
    if (value.is_null()) {
        output += "null";
        return;
    }
    if (value.is_bool()) {
        output += value.as_bool() ? "true" : "false";
        return;
    }
    if (value.is_number()) {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<double>::max_digits10)
               << value.as_number();
        output += stream.str();
        return;
    }
    if (value.is_string()) {
        output.push_back('"');
        output += escape(value.as_string());
        output.push_back('"');
        return;
    }
    if (const auto* array = value.as_array(); array != nullptr) {
        output.push_back('[');
        for (std::size_t index = 0; index < array->size(); ++index) {
            if (index != 0U) {
                output.push_back(',');
            }
            append_serialized(output, (*array)[index]);
        }
        output.push_back(']');
        return;
    }
    const auto* object = value.as_object();
    output.push_back('{');
    bool first = true;
    for (const auto& [key, item] : *object) {
        if (!first) {
            output.push_back(',');
        }
        first = false;
        output.push_back('"');
        output += escape(key);
        output += "\":";
        append_serialized(output, item);
    }
    output.push_back('}');
}

} // namespace

Value::Value() noexcept : storage_(nullptr) {}
Value::Value(std::nullptr_t) noexcept : storage_(nullptr) {}
Value::Value(const bool value) noexcept : storage_(value) {}
Value::Value(const double value) noexcept : storage_(value) {}
Value::Value(const int value) noexcept : storage_(static_cast<double>(value)) {}
Value::Value(std::string value) : storage_(std::move(value)) {}
Value::Value(const std::string_view value) : storage_(std::string(value)) {}
Value::Value(const char* value) : storage_(std::string(value == nullptr ? "" : value)) {}
Value::Value(Array value) : storage_(std::move(value)) {}
Value::Value(Object value) : storage_(std::move(value)) {}

bool Value::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(storage_); }
bool Value::is_bool() const noexcept { return std::holds_alternative<bool>(storage_); }
bool Value::is_number() const noexcept { return std::holds_alternative<double>(storage_); }
bool Value::is_string() const noexcept { return std::holds_alternative<std::string>(storage_); }
bool Value::is_array() const noexcept { return std::holds_alternative<Array>(storage_); }
bool Value::is_object() const noexcept { return std::holds_alternative<Object>(storage_); }

bool Value::as_bool(const bool fallback) const noexcept {
    const auto* value = std::get_if<bool>(&storage_);
    return value == nullptr ? fallback : *value;
}

double Value::as_number(const double fallback) const noexcept {
    const auto* value = std::get_if<double>(&storage_);
    return value == nullptr ? fallback : *value;
}

std::string_view Value::as_string(const std::string_view fallback) const noexcept {
    const auto* value = std::get_if<std::string>(&storage_);
    return value == nullptr ? fallback : std::string_view(*value);
}

const Value::Array* Value::as_array() const noexcept { return std::get_if<Array>(&storage_); }
Value::Array* Value::as_array() noexcept { return std::get_if<Array>(&storage_); }
const Value::Object* Value::as_object() const noexcept { return std::get_if<Object>(&storage_); }
Value::Object* Value::as_object() noexcept { return std::get_if<Object>(&storage_); }

const Value* Value::find(const std::string_view key) const noexcept {
    const auto* object = as_object();
    if (object == nullptr) {
        return nullptr;
    }
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

Value* Value::find(const std::string_view key) noexcept {
    auto* object = as_object();
    if (object == nullptr) {
        return nullptr;
    }
    const auto iterator = object->find(key);
    return iterator == object->end() ? nullptr : &iterator->second;
}

const Value* Value::at(const std::size_t index) const noexcept {
    const auto* array = as_array();
    return array == nullptr || index >= array->size() ? nullptr : &(*array)[index];
}

Value* Value::at(const std::size_t index) noexcept {
    auto* array = as_array();
    return array == nullptr || index >= array->size() ? nullptr : &(*array)[index];
}

Value parse(const std::string_view text) {
    return Parser(text).parse_document();
}

std::optional<Value> try_parse(const std::string_view text, std::string& error) noexcept {
    try {
        error.clear();
        return parse(text);
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

std::string stringify(const Value& value) {
    std::string result;
    append_serialized(result, value);
    return result;
}

std::string escape(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 0x20U) {
                std::ostringstream stream;
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character);
                result += stream.str();
            } else {
                result.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    return result;
}

std::string string_or(const Value* value, const std::string_view fallback) {
    return value == nullptr || !value->is_string()
        ? std::string(fallback)
        : std::string(value->as_string());
}

double number_or(const Value* value, const double fallback) noexcept {
    return value == nullptr ? fallback : value->as_number(fallback);
}

bool bool_or(const Value* value, const bool fallback) noexcept {
    return value == nullptr ? fallback : value->as_bool(fallback);
}

} // namespace threadspire::ai::core::json
