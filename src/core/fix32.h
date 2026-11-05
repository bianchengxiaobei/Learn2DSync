#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace nsync::core {

class Fix32 {
public:
    using raw_type = std::int32_t;

    static constexpr int kFracBits = 16;
    static constexpr raw_type kOne = raw_type(1) << kFracBits;

    constexpr Fix32() = default;

    static constexpr Fix32 from_raw(raw_type raw) {
        Fix32 value;
        value.raw_ = raw;
        return value;
    }

    static constexpr Fix32 from_int(std::int32_t value) {
        return from_raw(value * kOne);
    }

    static constexpr Fix32 from_ratio(std::int32_t numerator, std::int32_t denominator) {
        if (denominator == 0) {
            return {};
        }
        return from_raw(static_cast<raw_type>((static_cast<std::int64_t>(numerator) * kOne) / denominator));
    }

    static constexpr Fix32 zero() { return {}; }
    static constexpr Fix32 one() { return from_raw(kOne); }

    constexpr raw_type raw() const { return raw_; }

    constexpr std::int32_t to_int_trunc() const {
        return raw_ / kOne;
    }

    constexpr std::int32_t to_int_round() const {
        return (raw_ >= 0) ? (raw_ + kOne / 2) / kOne : (raw_ - kOne / 2) / kOne;
    }

    static constexpr Fix32 abs(Fix32 value) {
        return from_raw(value.raw_ < 0 ? -value.raw_ : value.raw_);
    }

    static constexpr Fix32 min(Fix32 a, Fix32 b) { return a.raw_ < b.raw_ ? a : b; }
    static constexpr Fix32 max(Fix32 a, Fix32 b) { return a.raw_ > b.raw_ ? a : b; }

    static constexpr Fix32 sqrt(Fix32 value) {
        if (value.raw_ <= 0) {
            return {};
        }
        const std::uint64_t n = static_cast<std::uint64_t>(value.raw_) << kFracBits;
        return from_raw(static_cast<raw_type>(isqrt(n)));
    }

    static constexpr Fix32 lerp(Fix32 a, Fix32 b, Fix32 t) {
        return a + (b - a) * t;
    }

    constexpr Fix32 operator+() const { return *this; }
    constexpr Fix32 operator-() const { return from_raw(-raw_); }

    friend constexpr Fix32 operator+(Fix32 a, Fix32 b) { return from_raw(a.raw_ + b.raw_); }
    friend constexpr Fix32 operator-(Fix32 a, Fix32 b) { return from_raw(a.raw_ - b.raw_); }
    friend constexpr Fix32 operator*(Fix32 a, Fix32 b) {
        return from_raw(static_cast<raw_type>((static_cast<std::int64_t>(a.raw_) * b.raw_) / kOne));
    }
    friend constexpr Fix32 operator/(Fix32 a, Fix32 b) {
        if (b.raw_ == 0) {
            return {};
        }
        return from_raw(static_cast<raw_type>((static_cast<std::int64_t>(a.raw_) * kOne) / b.raw_));
    }

    constexpr Fix32& operator+=(Fix32 other) { raw_ += other.raw_; return *this; }
    constexpr Fix32& operator-=(Fix32 other) { raw_ -= other.raw_; return *this; }
    constexpr Fix32& operator*=(Fix32 other) { *this = *this * other; return *this; }
    constexpr Fix32& operator/=(Fix32 other) { *this = *this / other; return *this; }

    friend constexpr bool operator==(Fix32 a, Fix32 b) { return a.raw_ == b.raw_; }
    friend constexpr bool operator!=(Fix32 a, Fix32 b) { return a.raw_ != b.raw_; }
    friend constexpr bool operator<(Fix32 a, Fix32 b) { return a.raw_ < b.raw_; }
    friend constexpr bool operator<=(Fix32 a, Fix32 b) { return a.raw_ <= b.raw_; }
    friend constexpr bool operator>(Fix32 a, Fix32 b) { return a.raw_ > b.raw_; }
    friend constexpr bool operator>=(Fix32 a, Fix32 b) { return a.raw_ >= b.raw_; }

private:
    static constexpr std::uint64_t isqrt(std::uint64_t n) {
        std::uint64_t result = 0;
        std::uint64_t bit = std::uint64_t(1) << 62;
        while (bit > n) {
            bit >>= 2;
        }
        while (bit != 0) {
            if (n >= result + bit) {
                n -= result + bit;
                result = (result >> 1) + bit;
            } else {
                result >>= 1;
            }
            bit >>= 2;
        }
        return result;
    }

    raw_type raw_ = 0;
};

inline bool parse_fix32_decimal(std::string_view token, Fix32& value) {
    if (token.empty()) {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;
    if (token[index] == '-') {
        negative = true;
        ++index;
    } else if (token[index] == '+') {
        ++index;
    }
    if (index == token.size()) {
        return false;
    }

    auto is_digit = [](char character) {
        return character >= '0' && character <= '9';
    };

    std::uint64_t integer = 0;
    bool has_digits = false;
    while (index < token.size() && is_digit(token[index])) {
        has_digits = true;
        integer = integer * 10U +
                  static_cast<std::uint64_t>(token[index] - '0');
        if (integer > 32767U) {
            return false;
        }
        ++index;
    }

    std::uint64_t fraction = 0;
    std::uint64_t denominator = 1;
    if (index < token.size() && token[index] == '.') {
        ++index;
        std::size_t fraction_digits = 0;
        while (index < token.size() && is_digit(token[index])) {
            has_digits = true;
            if (fraction_digits < 9) {
                fraction = fraction * 10U +
                           static_cast<std::uint64_t>(token[index] - '0');
                denominator *= 10U;
            }
            ++fraction_digits;
            ++index;
        }
        if (fraction_digits == 0) {
            return false;
        }
    }

    if (!has_digits || index != token.size()) {
        return false;
    }

    const std::uint64_t raw_integer =
        integer * static_cast<std::uint64_t>(Fix32::kOne);
    const std::uint64_t raw_fraction =
        (fraction * static_cast<std::uint64_t>(Fix32::kOne)) / denominator;
    const std::uint64_t raw = raw_integer + raw_fraction;
    if (raw > static_cast<std::uint64_t>(
                  std::numeric_limits<std::int32_t>::max())) {
        return false;
    }

    const std::int32_t signed_raw = static_cast<std::int32_t>(raw);
    value = Fix32::from_raw(negative ? -signed_raw : signed_raw);
    return true;
}

struct Vec2 {
    Fix32 x{};
    Fix32 y{};

    constexpr Vec2() = default;
    constexpr Vec2(Fix32 x_, Fix32 y_) : x(x_), y(y_) {}

    friend constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr Vec2 operator*(Vec2 v, Fix32 s) { return {v.x * s, v.y * s}; }
    friend constexpr Vec2 operator*(Fix32 s, Vec2 v) { return v * s; }
    friend constexpr Vec2 operator/(Vec2 v, Fix32 s) { return {v.x / s, v.y / s}; }

    friend constexpr bool operator==(Vec2 a, Vec2 b) { return a.x == b.x && a.y == b.y; }
    friend constexpr bool operator!=(Vec2 a, Vec2 b) { return !(a == b); }

    constexpr Vec2& operator+=(Vec2 other) { x += other.x; y += other.y; return *this; }
    constexpr Vec2& operator-=(Vec2 other) { x -= other.x; y -= other.y; return *this; }
    constexpr Vec2& operator*=(Fix32 scalar) { x *= scalar; y *= scalar; return *this; }

    constexpr Fix32 dot(Vec2 other) const { return x * other.x + y * other.y; }
    constexpr Fix32 length_sq() const { return dot(*this); }
    constexpr Fix32 length() const { return Fix32::sqrt(length_sq()); }

    constexpr Vec2 normalized() const {
        const Fix32 len = length();
        if (len == Fix32::zero()) {
            return {};
        }
        return *this / len;
    }
};

} // namespace nsync::core
