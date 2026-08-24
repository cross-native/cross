// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace cross {

// Host-independent unsigned 128-bit arithmetic used by the frontend and MIR.
// It deliberately does not depend on a C++ compiler extension such as
// __int128: the Cross compiler itself must remain buildable on MSVC hosts.
struct UInt128 {
    std::uint64_t low{};
    std::uint64_t high{};

    constexpr UInt128() = default;
    constexpr UInt128(std::uint64_t low_value, std::uint64_t high_value = 0)
        : low(low_value), high(high_value) {}

    friend bool operator==(UInt128, UInt128) = default;
    friend bool operator<(UInt128 left, UInt128 right) {
        return left.high < right.high ||
               (left.high == right.high && left.low < right.low);
    }
};

inline UInt128 bit_not(UInt128 value) {
    return {~value.low, ~value.high};
}

inline UInt128 bit_and(UInt128 left, UInt128 right) {
    return {left.low & right.low, left.high & right.high};
}

inline UInt128 bit_or(UInt128 left, UInt128 right) {
    return {left.low | right.low, left.high | right.high};
}

inline UInt128 bit_xor(UInt128 left, UInt128 right) {
    return {left.low ^ right.low, left.high ^ right.high};
}

inline UInt128 add(UInt128 left, UInt128 right, bool* overflow = nullptr) {
    const auto low = left.low + right.low;
    const auto carry = low < left.low ? std::uint64_t{1} : std::uint64_t{0};
    const auto intermediate = left.high + right.high;
    const bool first_overflow = intermediate < left.high;
    const auto high = intermediate + carry;
    if (overflow) *overflow = first_overflow || high < intermediate;
    return {low, high};
}

inline UInt128 negate(UInt128 value) {
    return add(bit_not(value), {1, 0});
}

inline UInt128 subtract(UInt128 left, UInt128 right) {
    return add(left, negate(right));
}

inline UInt128 shift_left(UInt128 value, unsigned count) {
    if (count >= 128) return {};
    if (count == 0) return value;
    if (count >= 64) return {0, value.low << (count - 64)};
    return {value.low << count,
            (value.high << count) | (value.low >> (64 - count))};
}

inline UInt128 shift_right(UInt128 value, unsigned count) {
    if (count >= 128) return {};
    if (count == 0) return value;
    if (count >= 64) return {value.high >> (count - 64), 0};
    return {(value.low >> count) | (value.high << (64 - count)),
            value.high >> count};
}

inline bool bit(UInt128 value, unsigned index) {
    return index < 64 ? ((value.low >> index) & 1U) != 0
                      : ((value.high >> (index - 64)) & 1U) != 0;
}

inline UInt128 multiply(UInt128 left, UInt128 right) {
    UInt128 result;
    for (unsigned index = 0; index < 128; ++index) {
        if (bit(right, index)) result = add(result, shift_left(left, index));
    }
    return result;
}

inline std::pair<UInt128, UInt128> divide(UInt128 dividend, UInt128 divisor) {
    if (divisor == UInt128{}) return {};
    UInt128 quotient;
    UInt128 remainder;
    for (unsigned index = 128; index-- > 0;) {
        remainder = shift_left(remainder, 1);
        if (bit(dividend, index)) remainder.low |= 1;
        if (remainder < divisor) continue;
        remainder = subtract(remainder, divisor);
        if (index < 64) quotient.low |= std::uint64_t{1} << index;
        else quotient.high |= std::uint64_t{1} << (index - 64);
    }
    return {quotient, remainder};
}

inline UInt128 mask_to(UInt128 value, unsigned bits) {
    if (bits >= 128) return value;
    if (bits == 0) return {};
    if (bits <= 64) {
        const auto low_mask = bits == 64 ? ~std::uint64_t{0}
                                         : (std::uint64_t{1} << bits) - 1;
        return {value.low & low_mask, 0};
    }
    const auto high_bits = bits - 64;
    const auto high_mask = high_bits == 64 ? ~std::uint64_t{0}
                                           : (std::uint64_t{1} << high_bits) - 1;
    return {value.low, value.high & high_mask};
}

inline bool fits_unsigned(UInt128 value, unsigned bits) {
    return mask_to(value, bits) == value;
}

inline bool fits_signed_positive(UInt128 value, unsigned bits) {
    return bits != 0 && fits_unsigned(value, bits - 1);
}

inline std::optional<UInt128> parse_uint128(std::string_view digits, unsigned base) {
    if (digits.empty() || base < 2 || base > 16) return std::nullopt;
    UInt128 value;
    for (const char character : digits) {
        unsigned digit_value;
        if (character >= '0' && character <= '9') {
            digit_value = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit_value = static_cast<unsigned>(character - 'a' + 10);
        } else if (character >= 'A' && character <= 'F') {
            digit_value = static_cast<unsigned>(character - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (digit_value >= base) return std::nullopt;
        UInt128 multiplied;
        for (unsigned index = 0; index < base; ++index) {
            bool overflow = false;
            multiplied = add(multiplied, value, &overflow);
            if (overflow) return std::nullopt;
        }
        bool overflow = false;
        value = add(multiplied, {digit_value, 0}, &overflow);
        if (overflow) return std::nullopt;
    }
    return value;
}

inline unsigned divide_small(UInt128& value, unsigned divisor) {
    unsigned remainder = 0;
    UInt128 quotient;
    for (unsigned index = 128; index-- > 0;) {
        remainder = remainder * 2 + (bit(value, index) ? 1U : 0U);
        if (remainder >= divisor) {
            remainder -= divisor;
            if (index < 64) quotient.low |= std::uint64_t{1} << index;
            else quotient.high |= std::uint64_t{1} << (index - 64);
        }
    }
    value = quotient;
    return remainder;
}

inline std::string to_decimal(UInt128 value) {
    if (value == UInt128{}) return "0";
    std::string result;
    while (!(value == UInt128{})) {
        result.push_back(static_cast<char>('0' + divide_small(value, 10)));
    }
    std::reverse(result.begin(), result.end());
    return result;
}

} // namespace cross
