// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/floating_semantics.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cross::floating {
namespace {

class BigUnsigned {
public:
    explicit BigUnsigned(UInt128 value = {}) {
        if (value.low) {
            words_.push_back(static_cast<std::uint32_t>(value.low));
            words_.push_back(static_cast<std::uint32_t>(value.low >> 32));
        }
        if (value.high) {
            if (words_.empty()) words_.resize(2);
            words_.push_back(static_cast<std::uint32_t>(value.high));
            words_.push_back(static_cast<std::uint32_t>(value.high >> 32));
        }
        trim();
    }

    [[nodiscard]] bool zero() const { return words_.empty(); }
    [[nodiscard]] unsigned bits() const {
        return words_.empty() ? 0U
            : static_cast<unsigned>((words_.size() - 1) * 32) +
                  (32U - static_cast<unsigned>(std::countl_zero(words_.back())));
    }
    [[nodiscard]] UInt128 small() const {
        UInt128 result;
        if (!words_.empty()) result.low = words_[0];
        if (words_.size() > 1) result.low |= std::uint64_t{words_[1]} << 32;
        if (words_.size() > 2) result.high = words_[2];
        if (words_.size() > 3) result.high |= std::uint64_t{words_[3]} << 32;
        return result;
    }
    [[nodiscard]] int compare(const BigUnsigned& other) const {
        if (words_.size() != other.words_.size())
            return words_.size() < other.words_.size() ? -1 : 1;
        for (std::size_t index = words_.size(); index-- > 0;) {
            if (words_[index] != other.words_[index])
                return words_[index] < other.words_[index] ? -1 : 1;
        }
        return 0;
    }
    void add(const BigUnsigned& other) {
        words_.resize(std::max(words_.size(), other.words_.size()), 0);
        std::uint64_t carry{};
        for (std::size_t i = 0; i < words_.size(); ++i) {
            const auto sum = std::uint64_t{words_[i]} +
                (i < other.words_.size() ? other.words_[i] : 0U) + carry;
            words_[i] = static_cast<std::uint32_t>(sum);
            carry = sum >> 32;
        }
        if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
    }
    void add_small(std::uint32_t value) { add(BigUnsigned{UInt128{value}}); }
    void subtract(const BigUnsigned& other) {
        std::uint64_t borrow{};
        for (std::size_t i = 0; i < words_.size(); ++i) {
            const auto subtrahend =
                (i < other.words_.size() ? std::uint64_t{other.words_[i]} : 0) + borrow;
            const auto current = std::uint64_t{words_[i]};
            words_[i] = static_cast<std::uint32_t>(current - subtrahend);
            borrow = current < subtrahend;
        }
        trim();
    }
    void multiply_small(std::uint32_t factor) {
        std::uint64_t carry{};
        for (auto& word : words_) {
            const auto product = std::uint64_t{word} * factor + carry;
            word = static_cast<std::uint32_t>(product);
            carry = product >> 32;
        }
        if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
        trim();
    }
    [[nodiscard]] BigUnsigned multiply(const BigUnsigned& other) const {
        BigUnsigned result;
        result.words_.assign(words_.size() + other.words_.size(), 0);
        for (std::size_t i = 0; i < words_.size(); ++i) {
            std::uint64_t carry{};
            for (std::size_t j = 0; j < other.words_.size(); ++j) {
                const auto product = std::uint64_t{words_[i]} * other.words_[j] +
                    result.words_[i + j] + carry;
                result.words_[i + j] = static_cast<std::uint32_t>(product);
                carry = product >> 32;
            }
            result.words_[i + other.words_.size()] = static_cast<std::uint32_t>(carry);
        }
        result.trim();
        return result;
    }
    [[nodiscard]] BigUnsigned shifted(unsigned count) const {
        if (zero()) return BigUnsigned{};
        BigUnsigned result;
        result.words_.assign(count / 32, 0);
        std::uint64_t carry{};
        for (auto word : words_) {
            const auto part = (std::uint64_t{word} << (count % 32)) | carry;
            result.words_.push_back(static_cast<std::uint32_t>(part));
            carry = part >> 32;
        }
        if (carry) result.words_.push_back(static_cast<std::uint32_t>(carry));
        return result;
    }
    [[nodiscard]] BigUnsigned right_shifted(unsigned count) const {
        if (count / 32 >= words_.size()) return BigUnsigned{};
        BigUnsigned result;
        result.words_.assign(words_.begin() + count / 32, words_.end());
        const auto rem = count % 32;
        if (rem) {
            std::uint32_t carry{};
            for (std::size_t i = result.words_.size(); i-- > 0;) {
                const auto next = result.words_[i] << (32 - rem);
                result.words_[i] = (result.words_[i] >> rem) | carry;
                carry = next;
            }
        }
        result.trim();
        return result;
    }
private:
    void trim() {
        while (!words_.empty() && words_.back() == 0) words_.pop_back();
    }
    std::vector<std::uint32_t> words_;
};

struct Spec {
    unsigned precision;
    unsigned exponent_bits;
    int bias;
    bool explicit_bit;
    [[nodiscard]] unsigned fraction_bits() const {
        return precision - (explicit_bit ? 0U : 1U);
    }
    [[nodiscard]] unsigned sign_bit() const {
        return fraction_bits() + exponent_bits;
    }
    [[nodiscard]] unsigned max_field() const { return (1U << exponent_bits) - 1; }
};

Spec spec(Format format) {
    switch (format) {
    case Format::Binary32: return {24, 8, 127, false};
    case Format::Binary64: return {53, 11, 1023, false};
    case Format::Extended80: return {64, 15, 16383, true};
    case Format::Binary128: return {113, 15, 16383, false};
    }
    return {53, 11, 1023, false};
}

enum class Class { Zero, Finite, Infinity, NaN };
struct Number {
    Class kind{Class::Zero};
    bool negative{};
    BigUnsigned significand;
    int scale{};
};

Value encoded(Format format, bool negative, unsigned exponent,
              UInt128 fraction = {}) {
    const auto info = spec(format);
    auto bits = bit_or(mask_to(fraction, info.fraction_bits()),
                       shift_left(UInt128{exponent}, info.fraction_bits()));
    if (negative) bits = bit_or(bits, shift_left(UInt128{1}, info.sign_bit()));
    return {bits, format};
}

Value infinity(Format format, bool negative) {
    return encoded(format, negative, spec(format).max_field(),
                   spec(format).explicit_bit ? shift_left(UInt128{1}, 63) : UInt128{});
}

Value nan(Format format) {
    const auto info = spec(format);
    return encoded(format, false, info.max_field(),
                   bit_or(shift_left(UInt128{1}, info.precision - 2),
                          info.explicit_bit ? shift_left(UInt128{1}, 63) : UInt128{}));
}

Number decode(Value value) {
    const auto info = spec(value.format);
    const auto field = static_cast<unsigned>(
        shift_right(value.bits, info.fraction_bits()).low & info.max_field());
    const auto fraction = mask_to(value.bits, info.fraction_bits());
    Number result;
    result.negative = bit(value.bits, info.sign_bit());
    if (field == info.max_field()) {
        const auto infinity_fraction = info.explicit_bit
            ? shift_left(UInt128{1}, 63) : UInt128{};
        result.kind = fraction == infinity_fraction ? Class::Infinity : Class::NaN;
        return result;
    }
    if (field == 0 && fraction == UInt128{}) return result;
    result.kind = Class::Finite;
    auto significand = fraction;
    if (field != 0 && !info.explicit_bit)
        significand = bit_or(significand, shift_left(UInt128{1}, info.precision - 1));
    result.significand = BigUnsigned{significand};
    result.scale = (field ? static_cast<int>(field) - info.bias : 1 - info.bias)
                   - static_cast<int>(info.precision - 1);
    return result;
}

UInt128 rounded_quotient(BigUnsigned numerator, BigUnsigned denominator) {
    UInt128 quotient;
    auto shift = static_cast<int>(numerator.bits()) - static_cast<int>(denominator.bits());
    for (; shift >= 0; --shift) {
        const auto trial = denominator.shifted(static_cast<unsigned>(shift));
        if (numerator.compare(trial) < 0) continue;
        numerator.subtract(trial);
        if (shift < 128) quotient = bit_or(quotient, shift_left(UInt128{1}, static_cast<unsigned>(shift)));
    }
    const auto halfway = numerator.shifted(1).compare(denominator);
    if (halfway > 0 || (halfway == 0 && bit(quotient, 0)))
        quotient = add(quotient, {1});
    return quotient;
}

Value round_rational(Format format, bool negative, BigUnsigned numerator,
                     BigUnsigned denominator, int binary_scale = 0) {
    const auto info = spec(format);
    if (numerator.zero()) return encoded(format, negative, 0);
    auto exponent = static_cast<int>(numerator.bits()) -
                    static_cast<int>(denominator.bits());
    if (exponent >= 0) {
        if (numerator.compare(denominator.shifted(static_cast<unsigned>(exponent))) < 0)
            --exponent;
    } else if (numerator.shifted(static_cast<unsigned>(-exponent)).compare(denominator) < 0) {
        --exponent;
    }
    exponent += binary_scale;
    if (exponent > info.bias) return infinity(format, negative);
    const auto normal = exponent >= 1 - info.bias;
    const int target_exponent = normal ? exponent : 1 - info.bias;
    const int shift = static_cast<int>(info.precision - 1) - target_exponent + binary_scale;
    if (shift >= 0) numerator = numerator.shifted(static_cast<unsigned>(shift));
    else denominator = denominator.shifted(static_cast<unsigned>(-shift));
    auto rounded = rounded_quotient(std::move(numerator), std::move(denominator));
    const auto hidden = shift_left(UInt128{1}, info.precision - 1);
    if (normal) {
        if (bit(rounded, info.precision)) {
            rounded = hidden;
            ++exponent;
            if (exponent > info.bias) return infinity(format, negative);
        }
        if (!info.explicit_bit) rounded = subtract(rounded, hidden);
        return encoded(format, negative, static_cast<unsigned>(exponent + info.bias), rounded);
    }
    if (!(rounded < hidden)) {
        return encoded(format, negative, 1, info.explicit_bit ? hidden : UInt128{});
    }
    return encoded(format, negative, 0, rounded);
}

bool magnitude_less(const Number& a, const Number& b) {
    const auto scale = std::min(a.scale, b.scale);
    return a.significand.shifted(static_cast<unsigned>(a.scale - scale)).compare(
        b.significand.shifted(static_cast<unsigned>(b.scale - scale))) < 0;
}

} // namespace

std::optional<Value> parse(std::string text, Format format) {
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    bool negative{};
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.erase(text.begin());
    }
    if (text.empty() || text.size() > 10000) return std::nullopt;
    const bool hex = text.starts_with("0x") || text.starts_with("0X");
    const auto marker = text.find_first_of(hex ? "pP" : "eE");
    std::int64_t exponent{};
    if (marker != std::string::npos) {
        auto part = std::string_view(text).substr(marker + 1);
        if (!part.empty() && part.front() == '+') part.remove_prefix(1);
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), exponent);
        if (part.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != part.data() + part.size()) return std::nullopt;
        text.resize(marker);
    }
    BigUnsigned numerator;
    BigUnsigned denominator{UInt128{1}};
    bool seen_digit{};
    bool point{};
    std::int64_t fractional_digits{};
    for (std::size_t i = hex ? 2U : 0U; i < text.size(); ++i) {
        const auto ch = text[i];
        if (ch == '.') {
            if (point) return std::nullopt;
            point = true;
            continue;
        }
        unsigned digit{};
        if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0');
        else if (hex && ch >= 'a' && ch <= 'f') digit = static_cast<unsigned>(ch - 'a' + 10);
        else if (hex && ch >= 'A' && ch <= 'F') digit = static_cast<unsigned>(ch - 'A' + 10);
        else return std::nullopt;
        if (digit >= (hex ? 16U : 10U)) return std::nullopt;
        numerator.multiply_small(hex ? 16U : 10U);
        numerator.add_small(digit);
        seen_digit = true;
        if (point) ++fractional_digits;
    }
    if (!seen_digit) return std::nullopt;
    if (numerator.zero()) return encoded(format, negative, 0);
    const auto adjustment = exponent - fractional_digits * (hex ? 4 : 1);
    if (adjustment > 100000 || adjustment < -100000) return std::nullopt;
    int binary_scale{};
    if (hex) binary_scale = static_cast<int>(adjustment);
    else if (adjustment >= 0) {
        for (std::int64_t i = 0; i < adjustment; ++i) numerator.multiply_small(10);
    } else {
        for (std::int64_t i = 0; i < -adjustment; ++i) denominator.multiply_small(10);
    }
    return round_rational(format, negative, std::move(numerator),
                          std::move(denominator), binary_scale);
}

Value convert(Value value, Format format) {
    if (value.format == format) return value;
    const auto number = decode(value);
    if (number.kind == Class::NaN) return nan(format);
    if (number.kind == Class::Infinity) return infinity(format, number.negative);
    return round_rational(format, number.negative, number.significand,
                          BigUnsigned{UInt128{1}}, number.scale);
}

Value negate(Value value) {
    const auto sign = spec(value.format).sign_bit();
    value.bits = bit_xor(value.bits, shift_left(UInt128{1}, sign));
    return value;
}

Value binary(Operation operation, Value left, Value right, Format format) {
    const auto a = decode(convert(left, format));
    auto b = decode(convert(right, format));
    if (operation == Operation::Subtract) b.negative = !b.negative;
    if (a.kind == Class::NaN || b.kind == Class::NaN) return nan(format);
    const bool product_sign = a.negative != b.negative;
    if (operation == Operation::Multiply || operation == Operation::Divide) {
        if ((a.kind == Class::Infinity && b.kind == Class::Zero) ||
            (a.kind == Class::Zero && b.kind == Class::Infinity) ||
            (operation == Operation::Divide && a.kind == b.kind &&
             (a.kind == Class::Zero || a.kind == Class::Infinity))) return nan(format);
        if (a.kind == Class::Infinity ||
            (operation == Operation::Multiply && b.kind == Class::Infinity) ||
            (operation == Operation::Divide && b.kind == Class::Zero))
            return infinity(format, product_sign);
        if (a.kind == Class::Zero ||
            (operation == Operation::Divide && b.kind == Class::Infinity) ||
            (operation == Operation::Multiply && b.kind == Class::Zero))
            return encoded(format, product_sign, 0);
        if (b.kind == Class::Infinity) return infinity(format, product_sign);
        if (operation == Operation::Multiply)
            return round_rational(format, product_sign,
                a.significand.multiply(b.significand), BigUnsigned{UInt128{1}},
                a.scale + b.scale);
        return round_rational(format, product_sign, a.significand,
                              b.significand, a.scale - b.scale);
    }
    if (a.kind == Class::Infinity || b.kind == Class::Infinity) {
        if (a.kind == b.kind && a.negative != b.negative) return nan(format);
        return infinity(format, a.kind == Class::Infinity ? a.negative : b.negative);
    }
    if (a.kind == Class::Zero && b.kind == Class::Zero)
        return encoded(format, a.negative && b.negative, 0);
    const auto scale = std::min(a.scale, b.scale);
    auto x = a.significand.shifted(static_cast<unsigned>(a.scale - scale));
    auto y = b.significand.shifted(static_cast<unsigned>(b.scale - scale));
    bool negative = a.negative;
    if (a.negative == b.negative) x.add(y);
    else if (x.compare(y) >= 0) x.subtract(y);
    else { y.subtract(x); x = std::move(y); negative = b.negative; }
    if (x.zero()) negative = false;
    return round_rational(format, negative, std::move(x),
                          BigUnsigned{UInt128{1}}, scale);
}

bool compare(Comparison comparison, Value left, Value right) {
    const auto a = decode(left);
    const auto b = decode(right);
    if (a.kind == Class::NaN || b.kind == Class::NaN)
        return comparison == Comparison::NotEqual;
    int ordering{};
    if (a.kind == Class::Zero && b.kind == Class::Zero) ordering = 0;
    else if (a.negative != b.negative) ordering = a.negative ? -1 : 1;
    else if (a.kind == Class::Infinity || b.kind == Class::Infinity) {
        ordering = a.kind == b.kind ? 0 : (a.kind == Class::Infinity ? 1 : -1);
        if (a.negative) ordering = -ordering;
    } else {
        ordering = magnitude_less(a, b) ? -1 : magnitude_less(b, a) ? 1 : 0;
        if (a.negative) ordering = -ordering;
    }
    switch (comparison) {
    case Comparison::Equal: return ordering == 0;
    case Comparison::NotEqual: return ordering != 0;
    case Comparison::Less: return ordering < 0;
    case Comparison::LessEqual: return ordering <= 0;
    case Comparison::Greater: return ordering > 0;
    case Comparison::GreaterEqual: return ordering >= 0;
    }
    return false;
}

bool nonzero(Value value) {
    return decode(value).kind != Class::Zero;
}

Value from_integer(UInt128 bits, unsigned width, bool is_signed, Format format) {
    bits = mask_to(bits, width);
    const bool negative = is_signed && width != 0 && bit(bits, width - 1);
    if (negative) bits = mask_to(cross::negate(bits), width);
    return round_rational(format, negative, BigUnsigned{bits},
                          BigUnsigned{UInt128{1}});
}

std::optional<UInt128> to_integer(Value value, unsigned width, bool is_signed) {
    const auto number = decode(value);
    if (number.kind == Class::Infinity || number.kind == Class::NaN)
        return std::nullopt;
    auto magnitude = number.significand;
    if (number.scale >= 0) magnitude = magnitude.shifted(static_cast<unsigned>(number.scale));
    else magnitude = magnitude.right_shifted(static_cast<unsigned>(-number.scale));
    if (magnitude.bits() > 128) return std::nullopt;
    const auto bits = magnitude.small();
    if (number.negative) {
        if (bits == UInt128{}) return bits;
        if (!is_signed || (width && shift_left(UInt128{1}, width - 1) < bits))
            return std::nullopt;
        return mask_to(cross::negate(bits), width);
    }
    if (is_signed ? !fits_signed_positive(bits, width) : !fits_unsigned(bits, width))
        return std::nullopt;
    return bits;
}

} // namespace cross::floating
