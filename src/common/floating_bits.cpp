// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/floating_bits.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace cross {
namespace {

class BigUnsigned {
public:
    explicit BigUnsigned(std::uint32_t value = 0) {
        if (value != 0) words_.push_back(value);
    }

    [[nodiscard]] bool zero() const { return words_.empty(); }

    [[nodiscard]] unsigned bit_length() const {
        if (words_.empty()) return 0;
        return static_cast<unsigned>((words_.size() - 1) * 32) +
               static_cast<unsigned>(32 - std::countl_zero(words_.back()));
    }

    void multiply(std::uint32_t factor) {
        std::uint64_t carry{};
        for (auto& word : words_) {
            const auto product =
                static_cast<std::uint64_t>(word) * factor + carry;
            word = static_cast<std::uint32_t>(product);
            carry = product >> 32;
        }
        if (carry != 0) words_.push_back(static_cast<std::uint32_t>(carry));
    }

    void add(std::uint32_t value) {
        std::uint64_t carry = value;
        for (auto& word : words_) {
            const auto sum = static_cast<std::uint64_t>(word) + carry;
            word = static_cast<std::uint32_t>(sum);
            carry = sum >> 32;
            if (carry == 0) return;
        }
        if (carry != 0) words_.push_back(static_cast<std::uint32_t>(carry));
    }

    [[nodiscard]] BigUnsigned shifted(unsigned bits) const {
        if (zero()) return BigUnsigned{};
        BigUnsigned result;
        result.words_.assign(bits / 32, 0);
        std::uint64_t carry{};
        for (const auto word : words_) {
            const auto next =
                (static_cast<std::uint64_t>(word) << (bits % 32)) | carry;
            result.words_.push_back(static_cast<std::uint32_t>(next));
            carry = next >> 32;
        }
        if (carry != 0) result.words_.push_back(static_cast<std::uint32_t>(carry));
        return result;
    }

    [[nodiscard]] int compare(const BigUnsigned& other) const {
        if (words_.size() != other.words_.size()) {
            return words_.size() < other.words_.size() ? -1 : 1;
        }
        for (std::size_t index = words_.size(); index != 0; --index) {
            if (words_[index - 1] == other.words_[index - 1]) continue;
            return words_[index - 1] < other.words_[index - 1] ? -1 : 1;
        }
        return 0;
    }

    void subtract(const BigUnsigned& other) {
        std::uint64_t borrow{};
        for (std::size_t index = 0; index < words_.size(); ++index) {
            const auto subtrahend =
                (index < other.words_.size() ? other.words_[index] : 0) +
                borrow;
            const auto current = static_cast<std::uint64_t>(words_[index]);
            words_[index] =
                static_cast<std::uint32_t>(current - subtrahend);
            borrow = current < subtrahend ? 1 : 0;
        }
        while (!words_.empty() && words_.back() == 0) words_.pop_back();
    }

private:
    std::vector<std::uint32_t> words_;
};

struct RoundedQuotient {
    std::uint64_t value{};
    bool overflow{};
};

RoundedQuotient rounded_quotient(BigUnsigned numerator,
                                 BigUnsigned denominator) {
    RoundedQuotient result;
    if (denominator.zero()) return {0, true};
    int shift = static_cast<int>(numerator.bit_length()) -
                static_cast<int>(denominator.bit_length());
    for (; shift >= 0; --shift) {
        const auto candidate =
            denominator.shifted(static_cast<unsigned>(shift));
        if (numerator.compare(candidate) < 0) continue;
        numerator.subtract(candidate);
        if (shift >= 64) {
            result.overflow = true;
        } else {
            result.value |=
                std::uint64_t{1} << static_cast<unsigned>(shift);
        }
    }
    const auto relation = numerator.shifted(1).compare(denominator);
    const bool round_up =
        relation > 0 || (relation == 0 && (result.value & 1U) != 0);
    if (round_up) {
        if (result.value == std::numeric_limits<std::uint64_t>::max()) {
            result.value = 0;
            result.overflow = true;
        } else {
            ++result.value;
        }
    }
    return result;
}

} // namespace

std::optional<Extended80Bits> parse_extended80(std::string text) {
    if (text.ends_with("f80")) text.resize(text.size() - 3);
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    bool negative = false;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.erase(text.begin());
    }
    if (text.empty() || text.size() > 10000) return std::nullopt;

    BigUnsigned numerator;
    BigUnsigned denominator(1);
    const bool hexadecimal =
        text.starts_with("0x") || text.starts_with("0X");
    const auto exponent_at =
        text.find_first_of(hexadecimal ? "pP" : "eE");
    std::int64_t explicit_exponent{};
    if (exponent_at != std::string::npos) {
        auto exponent_text =
            std::string_view(text).substr(exponent_at + 1);
        if (!exponent_text.empty() && exponent_text.front() == '+') {
            exponent_text.remove_prefix(1);
        }
        if (exponent_text.empty()) return std::nullopt;
        const auto parsed =
            std::from_chars(exponent_text.data(),
                            exponent_text.data() + exponent_text.size(),
                            explicit_exponent);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != exponent_text.data() + exponent_text.size()) {
            return std::nullopt;
        }
        text.resize(exponent_at);
    }

    const std::size_t begin = hexadecimal ? 2U : 0U;
    bool seen_digit{};
    bool seen_point{};
    std::int64_t fractional_digits{};
    for (std::size_t index = begin; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '.') {
            if (seen_point) return std::nullopt;
            seen_point = true;
            continue;
        }
        unsigned digit{};
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (hexadecimal && ch >= 'a' && ch <= 'f') {
            digit = 10U + static_cast<unsigned>(ch - 'a');
        } else if (hexadecimal && ch >= 'A' && ch <= 'F') {
            digit = 10U + static_cast<unsigned>(ch - 'A');
        } else {
            return std::nullopt;
        }
        if (digit >= (hexadecimal ? 16U : 10U)) return std::nullopt;
        numerator.multiply(hexadecimal ? 16U : 10U);
        numerator.add(digit);
        seen_digit = true;
        if (seen_point) ++fractional_digits;
    }
    if (!seen_digit) return std::nullopt;
    if (numerator.zero()) {
        return Extended80Bits{
            0, static_cast<std::uint16_t>(negative ? 0x8000U : 0U)};
    }

    const auto adjustment =
        explicit_exponent -
        (hexadecimal ? 4 * fractional_digits : fractional_digits);
    if (adjustment > 100000 || adjustment < -100000) return std::nullopt;
    if (hexadecimal) {
        if (adjustment >= 0) {
            numerator =
                numerator.shifted(static_cast<unsigned>(adjustment));
        } else {
            denominator =
                denominator.shifted(static_cast<unsigned>(-adjustment));
        }
    } else if (adjustment >= 0) {
        for (std::int64_t index = 0; index < adjustment; ++index) {
            numerator.multiply(10);
        }
    } else {
        for (std::int64_t index = 0; index < -adjustment; ++index) {
            denominator.multiply(10);
        }
    }

    int exponent = static_cast<int>(numerator.bit_length()) -
                   static_cast<int>(denominator.bit_length());
    if (exponent >= 0) {
        if (numerator.compare(
                denominator.shifted(static_cast<unsigned>(exponent))) < 0) {
            --exponent;
        }
    } else if (numerator.shifted(static_cast<unsigned>(-exponent))
                   .compare(denominator) < 0) {
        --exponent;
    }

    std::uint16_t exponent_field{};
    std::uint64_t significand{};
    if (exponent > 16383) {
        exponent_field = 0x7fff;
        significand = std::uint64_t{1} << 63;
    } else if (exponent >= -16382) {
        const auto scale = 63 - exponent;
        auto scaled_numerator = numerator;
        auto scaled_denominator = denominator;
        if (scale >= 0) {
            scaled_numerator =
                scaled_numerator.shifted(static_cast<unsigned>(scale));
        } else {
            scaled_denominator =
                scaled_denominator.shifted(static_cast<unsigned>(-scale));
        }
        const auto rounded = rounded_quotient(
            std::move(scaled_numerator), std::move(scaled_denominator));
        if (rounded.overflow) {
            significand = std::uint64_t{1} << 63;
            ++exponent;
            exponent_field = exponent > 16383
                                 ? 0x7fff
                                 : static_cast<std::uint16_t>(
                                       exponent + 16383);
        } else {
            significand = rounded.value;
            exponent_field =
                static_cast<std::uint16_t>(exponent + 16383);
        }
    } else {
        const auto rounded =
            rounded_quotient(numerator.shifted(16445), denominator);
        significand = rounded.value;
        if (rounded.overflow ||
            significand >= (std::uint64_t{1} << 63)) {
            exponent_field = 1;
            significand = std::uint64_t{1} << 63;
        }
    }
    if (negative) {
        exponent_field =
            static_cast<std::uint16_t>(exponent_field | 0x8000U);
    }
    return Extended80Bits{significand, exponent_field};
}

} // namespace cross
