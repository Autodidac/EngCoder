module;
#include "module_prelude.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

module threadspire.tasks;

namespace threadspire::ai::routing {
namespace {

[[nodiscard]] std::string trim_ascii(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string lower_ascii(std::string value) {
    std::ranges::transform(value, value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

void replace_all(
    std::string& value,
    const std::string_view needle,
    const std::string_view replacement) {
    for (auto offset = value.find(needle);
         offset != std::string::npos;
         offset = value.find(needle, offset + replacement.size())) {
        value.replace(offset, needle.size(), replacement);
    }
}

[[nodiscard]] bool prefix_boundary(
    const std::string_view text,
    const std::string_view prefix) noexcept {
    if (!text.starts_with(prefix)) {
        return false;
    }
    return text.size() == prefix.size() ||
           std::isspace(static_cast<unsigned char>(text[prefix.size()])) != 0 ||
           text[prefix.size()] == ':';
}

class ArithmeticParser final {
public:
    explicit ArithmeticParser(const std::string_view expression)
        : expression_(expression) {}

    [[nodiscard]] bool parse(long double& value) {
        if (!parse_sum(value)) {
            return false;
        }
        skip_spaces();
        if (position_ != expression_.size()) {
            set_error("unexpected character at position " + std::to_string(position_ + 1U));
            return false;
        }
        if (!std::isfinite(value)) {
            set_error("the calculation did not produce a finite number");
            return false;
        }
        return true;
    }

    [[nodiscard]] const std::string& error() const noexcept {
        return error_;
    }

private:
    [[nodiscard]] bool parse_sum(long double& value) {
        if (!parse_product(value)) {
            return false;
        }
        while (true) {
            skip_spaces();
            const auto operation = peek();
            if (operation != '+' && operation != '-') {
                return true;
            }
            ++position_;
            long double right = 0.0L;
            if (!parse_product(right)) {
                return false;
            }
            value = operation == '+' ? value + right : value - right;
        }
    }

    [[nodiscard]] bool parse_product(long double& value) {
        if (!parse_unary(value)) {
            return false;
        }
        while (true) {
            skip_spaces();
            const auto operation = peek();
            if (operation != '*' && operation != '/') {
                return true;
            }
            ++position_;
            long double right = 0.0L;
            if (!parse_unary(right)) {
                return false;
            }
            if (operation == '/' && right == 0.0L) {
                set_error("division by zero is undefined");
                return false;
            }
            value = operation == '*' ? value * right : value / right;
        }
    }

    [[nodiscard]] bool parse_unary(long double& value) {
        skip_spaces();
        const auto operation = peek();
        if (operation == '+' || operation == '-') {
            ++position_;
            if (!parse_unary(value)) {
                return false;
            }
            if (operation == '-') {
                value = -value;
            }
            return true;
        }
        return parse_power(value);
    }

    [[nodiscard]] bool parse_power(long double& value) {
        if (!parse_primary(value)) {
            return false;
        }
        skip_spaces();
        if (peek() != '^') {
            return true;
        }
        ++position_;
        long double exponent = 0.0L;
        if (!parse_unary(exponent)) {
            return false;
        }
        value = std::pow(value, exponent);
        if (!std::isfinite(value)) {
            set_error("that exponent is outside the supported finite range");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool parse_primary(long double& value) {
        skip_spaces();
        if (peek() == '(') {
            if (++depth_ > 32U) {
                set_error("the expression is nested too deeply");
                return false;
            }
            ++position_;
            if (!parse_sum(value)) {
                return false;
            }
            skip_spaces();
            if (peek() != ')') {
                set_error("a closing parenthesis is missing");
                return false;
            }
            ++position_;
            --depth_;
            return true;
        }
        return parse_number(value);
    }

    [[nodiscard]] bool parse_number(long double& value) {
        skip_spaces();
        const auto start = position_;
        auto digits = false;
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            digits = true;
            ++position_;
        }
        if (peek() == '.') {
            ++position_;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                digits = true;
                ++position_;
            }
        }
        if (!digits) {
            set_error("a number was expected at position " + std::to_string(start + 1U));
            return false;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            const auto exponent_start = position_;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                ++position_;
            }
            if (position_ == exponent_start) {
                set_error("the scientific-notation exponent is incomplete");
                return false;
            }
        }

        double parsed = 0.0;
        const auto begin = expression_.data() + start;
        const auto end = expression_.data() + position_;
        const auto conversion = std::from_chars(begin, end, parsed, std::chars_format::general);
        if (conversion.ec != std::errc{} || conversion.ptr != end || !std::isfinite(parsed)) {
            set_error("the number is outside the supported finite range");
            return false;
        }
        value = static_cast<long double>(parsed);
        return true;
    }

    void skip_spaces() noexcept {
        while (position_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] char peek() const noexcept {
        return position_ < expression_.size() ? expression_[position_] : '\0';
    }

    void set_error(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }

    std::string_view expression_;
    std::size_t position_{0U};
    std::size_t depth_{0U};
    std::string error_;
};

[[nodiscard]] std::string format_number(long double value) {
    if (std::abs(value) < 1.0e-15L) {
        value = 0.0L;
    }
    const auto rounded = std::round(value);
    const auto tolerance = 1.0e-12L * std::max(1.0L, std::abs(value));
    std::ostringstream output;
    if (std::abs(value - rounded) <= tolerance && std::abs(rounded) <= 9.0e15L) {
        output << std::fixed << std::setprecision(0) << rounded;
    } else {
        output << std::setprecision(15) << std::defaultfloat << value;
    }
    return output.str();
}

} // namespace

CalculationResult evaluate_calculation(const std::string_view prompt) {
    CalculationResult result;
    auto expression = trim_ascii(std::string(prompt));
    if (expression.empty()) {
        return result;
    }

    const auto lowered = lower_ascii(expression);
    bool explicit_request = false;
    constexpr std::array strong_prefixes{
        std::string_view{"calculate"},
        std::string_view{"compute"},
        std::string_view{"evaluate"},
        std::string_view{"solve"},
    };
    constexpr std::array weak_prefixes{
        std::string_view{"what is"},
        std::string_view{"what's"},
        std::string_view{"whats"},
    };
    for (const auto prefix : strong_prefixes) {
        if (prefix_boundary(lowered, prefix)) {
            expression.erase(0U, prefix.size());
            explicit_request = true;
            break;
        }
    }
    if (!explicit_request) {
        for (const auto prefix : weak_prefixes) {
            if (prefix_boundary(lowered, prefix)) {
                expression.erase(0U, prefix.size());
                break;
            }
        }
    }

    expression = trim_ascii(std::move(expression));
    if (!expression.empty() && expression.front() == ':') {
        expression.erase(0U, 1U);
    }
    expression = trim_ascii(std::move(expression));
    while (!expression.empty() && (expression.back() == '?' || expression.back() == '=')) {
        expression.pop_back();
        expression = trim_ascii(std::move(expression));
    }
    replace_all(expression, "\xC3\x97", "*");
    replace_all(expression, "\xC3\xB7", "/");
    replace_all(expression, "\xE2\x88\x92", "-");

    const auto has_digit = std::ranges::any_of(expression, [](const unsigned char character) {
        return std::isdigit(character) != 0;
    });
    const auto has_operator = expression.find_first_of("+-*/^()") != std::string::npos;
    const auto supported_characters = std::ranges::all_of(
        expression,
        [](const unsigned char character) {
            return std::isdigit(character) != 0 ||
                   std::isspace(character) != 0 ||
                   character == '.' || character == 'e' || character == 'E' ||
                   character == '+' || character == '-' || character == '*' ||
                   character == '/' || character == '^' ||
                   character == '(' || character == ')';
        });

    if (!has_digit || !supported_characters || (!explicit_request && !has_operator)) {
        if (explicit_request) {
            result.recognized = true;
            result.error =
                "The local calculator accepts numeric arithmetic using +, -, *, /, ^, and parentheses.";
        }
        return result;
    }
    if (expression.size() > 256U) {
        result.recognized = true;
        result.error = "The arithmetic expression is longer than the 256-character safety limit.";
        return result;
    }

    result.recognized = true;
    result.expression = expression;
    ArithmeticParser parser(expression);
    long double value = 0.0L;
    if (!parser.parse(value)) {
        result.error = "The local calculator could not evaluate this expression: " + parser.error() + ".";
        return result;
    }
    result.succeeded = true;
    result.value = format_number(value);
    return result;
}

} // namespace threadspire::ai::routing
