// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/preprocessor_expression.hpp"

#include "frontend/ast.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>

namespace cross {
namespace {

enum class Kind {
    End, Invalid, Number, Identifier, Character, Open, Close,
    Plus, Minus, Not, Complement, Multiply, Divide, Remainder,
    Left, Right, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual,
    And, Xor, Or, LogicalAnd, LogicalOr,
};

struct Value {
    std::uint64_t bits{};
    bool is_unsigned{};
    bool negative() const { return !is_unsigned && (bits >> 63U) != 0; }
};

int precedence(Kind kind) {
    switch (kind) {
    case Kind::LogicalOr: return 1;
    case Kind::LogicalAnd: return 2;
    case Kind::Or: return 3;
    case Kind::Xor: return 4;
    case Kind::And: return 5;
    case Kind::Equal: case Kind::NotEqual: return 6;
    case Kind::Less: case Kind::LessEqual:
    case Kind::Greater: case Kind::GreaterEqual: return 7;
    case Kind::Left: case Kind::Right: return 8;
    case Kind::Plus: case Kind::Minus: return 9;
    case Kind::Multiply: case Kind::Divide: case Kind::Remainder: return 10;
    default: return 0;
    }
}

class ConditionParser {
public:
    ConditionParser(std::string_view text, SourceLocation location,
                    Diagnostics& diagnostics, unsigned address_bits)
        : text_(text), location_(location), diagnostics_(diagnostics),
          address_bits_(address_bits) { next(); }

    bool evaluate() {
        const auto result = expression(1, true);
        if (kind_ != Kind::End) fail("unexpected token in preprocessing condition");
        return !failed_ && result.bits != 0;
    }

private:
    void fail(std::string_view message) {
        if (!failed_) diagnostics_.error(location_, message);
        failed_ = true;
    }

    void next() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_;
        const auto begin = position_;
        if (begin == text_.size()) { kind_ = Kind::End; token_ = {}; return; }
        const auto ch = text_[position_++];
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$') {
            kind_ = std::isdigit(static_cast<unsigned char>(ch)) ? Kind::Number : Kind::Identifier;
            if (ch == '$') {
                if (text_.substr(position_, 2) != "::") kind_ = Kind::Invalid;
                else position_ += 2;
                if (position_ >= text_.size() ||
                    !(std::isalpha(static_cast<unsigned char>(text_[position_])) || text_[position_] == '_'))
                    kind_ = Kind::Invalid;
            }
            while (position_ < text_.size()) {
                const auto next = text_[position_];
                if (kind_ == Kind::Identifier && text_.substr(position_, 2) == "::" &&
                    position_ + 2 < text_.size() &&
                    (std::isalpha(static_cast<unsigned char>(text_[position_ + 2])) || text_[position_ + 2] == '_')) {
                    position_ += 2;
                    continue;
                }
                if (!std::isalnum(static_cast<unsigned char>(next)) && next != '_') break;
                ++position_;
            }
        } else if (ch == '\'') {
            kind_ = Kind::Character;
            while (position_ < text_.size() && text_[position_] != '\'') {
                if (text_[position_++] == '\\' && position_ < text_.size()) ++position_;
            }
            if (position_ == text_.size()) kind_ = Kind::Invalid;
            else ++position_;
        } else {
            const auto pair = [&](char second, Kind yes, Kind no) {
                if (position_ < text_.size() && text_[position_] == second) {
                    ++position_;
                    return yes;
                }
                return no;
            };
            switch (ch) {
            case '(': kind_ = Kind::Open; break;
            case ')': kind_ = Kind::Close; break;
            case '+': kind_ = pair('+', Kind::Invalid, Kind::Plus); break;
            case '-': kind_ = pair('-', Kind::Invalid, Kind::Minus); break;
            case '!': kind_ = pair('=', Kind::NotEqual, Kind::Not); break;
            case '~': kind_ = Kind::Complement; break;
            case '*': kind_ = Kind::Multiply; break;
            case '/': kind_ = Kind::Divide; break;
            case '%': kind_ = Kind::Remainder; break;
            case '^': kind_ = Kind::Xor; break;
            case '&': kind_ = pair('&', Kind::LogicalAnd, Kind::And); break;
            case '|': kind_ = pair('|', Kind::LogicalOr, Kind::Or); break;
            case '=': kind_ = pair('=', Kind::Equal, Kind::Invalid); break;
            case '<':
                kind_ = pair('<', Kind::Left, Kind::Less);
                if (kind_ == Kind::Less) kind_ = pair('=', Kind::LessEqual, Kind::Less);
                break;
            case '>':
                kind_ = pair('>', Kind::Right, Kind::Greater);
                if (kind_ == Kind::Greater) kind_ = pair('=', Kind::GreaterEqual, Kind::Greater);
                break;
            default: kind_ = Kind::Invalid; break;
            }
        }
        token_ = text_.substr(begin, position_ - begin);
    }

    Value number() {
        auto spelling = token_;
        unsigned width = 64;
        std::optional<bool> is_unsigned;
        for (const auto prefix : {std::string_view("u"), std::string_view("i")}) {
            for (const auto suffix : {"8", "16", "32", "64", "128", "ptr"}) {
                const auto full = std::string(prefix) + suffix;
                if (!spelling.ends_with(full)) continue;
                spelling.remove_suffix(full.size());
                is_unsigned = prefix == "u";
                if (std::string_view(suffix) == "ptr") width = address_bits_;
                else std::from_chars(suffix, suffix + std::char_traits<char>::length(suffix), width);
                break;
            }
            if (is_unsigned.has_value()) break;
        }
        int base = 10;
        if (spelling.starts_with("0x") || spelling.starts_with("0X")) {
            base = 16; spelling.remove_prefix(2);
        } else if (spelling.starts_with("0b") || spelling.starts_with("0B")) {
            base = 2; spelling.remove_prefix(2);
        } else if (spelling.size() > 1 && spelling.front() == '0') {
            base = 8;
        }
        std::string digits;
        for (const auto ch : spelling) if (ch != '_') digits.push_back(ch);
        std::uint64_t bits{};
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), bits, base);
        if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
            fail("invalid or out-of-range integer in preprocessing condition");
            return {};
        }
        // Suffixes retain their range contract; all accepted values then use
        // i64/u64 evaluation. Wider literal values cannot silently truncate.
        const bool unsigned_value = is_unsigned.value_or(
            base != 10 && bits > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
        const auto value_bits = std::min(width, 64U) - static_cast<unsigned>(!unsigned_value);
        const auto maximum = value_bits >= 64 ? std::numeric_limits<std::uint64_t>::max()
                                             : (std::uint64_t{1} << value_bits) - 1;
        if (bits > maximum) fail("integer literal does not fit its preprocessing type");
        return {bits, unsigned_value};
    }

    Value unary(bool active) {
        if (++depth_ > 256) {
            fail("preprocessing condition nesting limit exceeded");
            kind_ = Kind::End;
            --depth_;
            return {};
        }
        const auto finish = [&](Value value) { --depth_; return value; };
        const auto operation = kind_;
        if (operation == Kind::Plus || operation == Kind::Minus ||
            operation == Kind::Not || operation == Kind::Complement) {
            next();
            auto value = unary(active);
            if (operation == Kind::Minus) value.bits = std::uint64_t{0} - value.bits;
            else if (operation == Kind::Complement) value.bits = ~value.bits;
            else if (operation == Kind::Not) value = {value.bits == 0, false};
            return finish(value);
        }
        if (operation == Kind::Open) {
            next();
            const auto value = expression(1, active);
            if (kind_ != Kind::Close) fail("expected ')' in preprocessing condition");
            else next();
            return finish(value);
        }
        Value value;
        if (kind_ == Kind::Number) value = number();
        else if (kind_ == Kind::Character) {
            const auto character = decode_character_literal(token_);
            if (!character) fail("invalid character in preprocessing condition");
            else value = {*character, true};
        } else if (kind_ != Kind::Identifier) {
            fail("expected integer expression in preprocessing condition");
        }
        next();
        return finish(value);
    }

    Value binary(Kind operation, Value left, Value right, bool active) {
        if (operation == Kind::LogicalAnd) return {left.bits != 0 && right.bits != 0, false};
        if (operation == Kind::LogicalOr) return {left.bits != 0 || right.bits != 0, false};
        if (operation == Kind::Left || operation == Kind::Right) {
            if (right.bits >= 64) {
                if (active) fail("shift count outside [0, 63] in preprocessing condition");
                return {};
            }
            const auto count = static_cast<unsigned>(right.bits);
            if (operation == Kind::Left) left.bits <<= count;
            else {
                const bool negative = left.negative();
                left.bits >>= count;
                if (negative && count != 0) left.bits |= ~std::uint64_t{0} << (64U - count);
            }
            return left;
        }
        const bool unsigned_result = left.is_unsigned || right.is_unsigned;
        const bool left_negative = !unsigned_result && left.negative();
        const bool right_negative = !unsigned_result && right.negative();
        const bool less = left_negative != right_negative ? left_negative : left.bits < right.bits;
        const bool equal = left.bits == right.bits;
        std::uint64_t bits{};
        switch (operation) {
        case Kind::Plus: bits = left.bits + right.bits; break;
        case Kind::Minus: bits = left.bits - right.bits; break;
        case Kind::Multiply: bits = left.bits * right.bits; break;
        case Kind::And: bits = left.bits & right.bits; break;
        case Kind::Xor: bits = left.bits ^ right.bits; break;
        case Kind::Or: bits = left.bits | right.bits; break;
        case Kind::Divide: case Kind::Remainder: {
            if (right.bits == 0) {
                if (active) fail("division by zero in preprocessing condition");
                return {};
            }
            const auto a = left_negative ? std::uint64_t{0} - left.bits : left.bits;
            const auto b = right_negative ? std::uint64_t{0} - right.bits : right.bits;
            bits = operation == Kind::Divide ? a / b : a % b;
            if (operation == Kind::Divide ? left_negative != right_negative : left_negative)
                bits = std::uint64_t{0} - bits;
            break;
        }
        case Kind::Less: return {less, false};
        case Kind::LessEqual: return {less || equal, false};
        case Kind::Greater: return {!less && !equal, false};
        case Kind::GreaterEqual: return {!less, false};
        case Kind::Equal: return {equal, false};
        case Kind::NotEqual: return {!equal, false};
        default: break;
        }
        return {bits, unsigned_result};
    }

    Value expression(int minimum, bool active) {
        auto left = unary(active);
        while (!failed_ && precedence(kind_) >= minimum) {
            const auto operation = kind_;
            next();
            const bool evaluate_right = active &&
                !(operation == Kind::LogicalAnd && left.bits == 0) &&
                !(operation == Kind::LogicalOr && left.bits != 0);
            const auto right = expression(precedence(operation) + 1, evaluate_right);
            left = binary(operation, left, right, active);
        }
        return left;
    }

    std::string_view text_;
    SourceLocation location_;
    Diagnostics& diagnostics_;
    unsigned address_bits_;
    std::size_t position_{};
    std::string_view token_;
    Kind kind_{Kind::End};
    unsigned depth_{};
    bool failed_{};
};

} // namespace

bool evaluate_preprocessing_condition(std::string_view text,
                                     SourceLocation location,
                                     Diagnostics& diagnostics,
                                     unsigned address_bits) {
    return ConditionParser(text, location, diagnostics, address_bits).evaluate();
}

} // namespace cross
