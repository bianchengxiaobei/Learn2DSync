#include "core/json.h"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace nsync::core {
namespace {

constexpr std::size_t kMaxJsonDepth = 64;
constexpr std::size_t kMaxJsonValues = 8'000'000;

bool is_space(char value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    bool parse(JsonValue& value, std::string* error) {
        if (!parse_value(value, 0)) {
            return finish(error);
        }
        skip_whitespace();
        if (position_ != text_.size()) {
            fail("trailing data after json value");
            return finish(error);
        }
        if (error) {
            error->clear();
        }
        return true;
    }

private:
    bool finish(std::string* error) {
        if (error) {
            *error = error_.empty() ? "invalid json" : error_;
        }
        return false;
    }

    bool fail(const char* message) {
        if (error_.empty()) {
            error_ = std::string(message) + " at byte " +
                     std::to_string(position_);
        }
        return false;
    }

    void skip_whitespace() {
        while (position_ < text_.size() && is_space(text_[position_])) {
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool parse_value(JsonValue& value, std::size_t depth) {
        if (depth > kMaxJsonDepth) {
            return fail("json nesting too deep");
        }
        if (++value_count_ > kMaxJsonValues) {
            return fail("json value count too large");
        }

        skip_whitespace();
        if (position_ >= text_.size()) {
            return fail("expected json value");
        }

        switch (text_[position_]) {
            case '{':
                return parse_object(value, depth + 1);
            case '[':
                return parse_array(value, depth + 1);
            case '"':
                value.type = JsonValue::Type::String;
                return parse_string(value.string_value);
            case 't':
                value.type = JsonValue::Type::Bool;
                value.bool_value = true;
                return parse_literal("true");
            case 'f':
                value.type = JsonValue::Type::Bool;
                value.bool_value = false;
                return parse_literal("false");
            case 'n':
                value.type = JsonValue::Type::Null;
                return parse_literal("null");
            default:
                return parse_number(value);
        }
    }

    bool parse_object(JsonValue& value, std::size_t depth) {
        value = JsonValue{};
        value.type = JsonValue::Type::Object;
        if (!consume('{')) {
            return fail("expected object");
        }
        skip_whitespace();
        if (consume('}')) {
            return true;
        }

        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("expected ':' after object key");
            }
            for (const auto& entry : value.object) {
                if (entry.first == key) {
                    return fail("duplicate object key");
                }
            }

            JsonValue child;
            if (!parse_value(child, depth)) {
                return false;
            }
            value.object.emplace_back(std::move(key), std::move(child));

            skip_whitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in object");
            }
            skip_whitespace();
        }
    }

    bool parse_array(JsonValue& value, std::size_t depth) {
        value = JsonValue{};
        value.type = JsonValue::Type::Array;
        if (!consume('[')) {
            return fail("expected array");
        }
        skip_whitespace();
        if (consume(']')) {
            return true;
        }

        for (;;) {
            JsonValue child;
            if (!parse_value(child, depth)) {
                return false;
            }
            value.array.push_back(std::move(child));

            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in array");
            }
            skip_whitespace();
        }
    }

    bool parse_literal(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal) {
            return fail("invalid json literal");
        }
        position_ += literal.size();
        return true;
    }

    bool parse_string(std::string& value) {
        skip_whitespace();
        if (!consume('"')) {
            return fail("expected string");
        }

        std::string result;
        while (position_ < text_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(text_[position_++]);
            if (character == '"') {
                value = std::move(result);
                return true;
            }
            if (character < 0x20U) {
                return fail("control character in string");
            }
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }

            if (position_ >= text_.size()) {
                return fail("truncated string escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = 0;
                    if (!parse_hex4(codepoint)) {
                        return false;
                    }
                    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                        if (position_ + 2 > text_.size() ||
                            text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') {
                            return fail("missing low surrogate");
                        }
                        position_ += 2;
                        std::uint32_t low = 0;
                        if (!parse_hex4(low) ||
                            low < 0xDC00U || low > 0xDFFFU) {
                            return fail("invalid low surrogate");
                        }
                        codepoint = 0x10000U +
                            ((codepoint - 0xD800U) << 10U) +
                            (low - 0xDC00U);
                    } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                        return fail("unexpected low surrogate");
                    }
                    append_utf8(result, codepoint);
                    break;
                }
                default:
                    return fail("unknown string escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_hex4(std::uint32_t& value) {
        value = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ >= text_.size()) {
                return fail("truncated unicode escape");
            }
            const char digit = text_[position_++];
            value <<= 4U;
            if (digit >= '0' && digit <= '9') {
                value |= static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                value |= static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                value |= static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                return fail("invalid unicode escape");
            }
        }
        return true;
    }

    static void append_utf8(std::string& result, std::uint32_t codepoint) {
        if (codepoint <= 0x7FU) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            result.push_back(
                static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            result.push_back(
                static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            result.push_back(
                static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

    bool parse_number(JsonValue& value) {
        const std::size_t start = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        if (position_ >= text_.size()) {
            return fail("expected number");
        }
        if (text_[position_] == '0') {
            ++position_;
        } else if (text_[position_] >= '1' && text_[position_] <= '9') {
            while (position_ < text_.size() && is_digit(text_[position_])) {
                ++position_;
            }
        } else {
            return fail("expected number");
        }

        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            if (position_ >= text_.size() || !is_digit(text_[position_])) {
                return fail("expected fraction digits");
            }
            while (position_ < text_.size() && is_digit(text_[position_])) {
                ++position_;
            }
        }

        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= text_.size() || !is_digit(text_[position_])) {
                return fail("expected exponent digits");
            }
            while (position_ < text_.size() && is_digit(text_[position_])) {
                ++position_;
            }
        }

        const std::string token(text_.substr(start, position_ - start));
        errno = 0;
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end == token.c_str() ||
            *end != '\0' || !std::isfinite(number)) {
            return fail("invalid number");
        }

        value = JsonValue{};
        value.type = JsonValue::Type::Number;
        value.number_value = number;
        return true;
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t value_count_ = 0;
    std::string error_;
};

} // namespace

const JsonValue* JsonValue::find(std::string_view key) const {
    for (const auto& entry : object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool parse_json(
    std::string_view text,
    JsonValue& value,
    std::string* error) {
    Parser parser(text);
    return parser.parse(value, error);
}

} // namespace nsync::core
