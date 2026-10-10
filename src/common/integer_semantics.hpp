// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/uint128.hpp"

namespace cross {

struct IntegerType {
    unsigned bits{};
    bool is_signed{};
    bool is_bool{};
};

inline IntegerType promote_integer(IntegerType type) {
    return type.bits < 32 ? IntegerType{32, true, false} : type;
}

inline IntegerType common_integer_type(IntegerType left, IntegerType right) {
    left = promote_integer(left);
    right = promote_integer(right);
    if (left.is_signed == right.is_signed)
        return left.bits >= right.bits ? left : right;
    const auto signed_type = left.is_signed ? left : right;
    const auto unsigned_type = left.is_signed ? right : left;
    return signed_type.bits > unsigned_type.bits ? signed_type : unsigned_type;
}

inline bool integer_negative(UInt128 value, IntegerType type) {
    return type.is_signed && type.bits != 0 && bit(value, type.bits - 1);
}

inline UInt128 convert_integer(UInt128 value, IntegerType from, IntegerType to) {
    value = mask_to(value, from.bits);
    if (to.is_bool) return UInt128{value != UInt128{}};
    if (from.bits < to.bits && integer_negative(value, from)) {
        value = bit_or(value, bit_not(mask_to(bit_not(UInt128{}), from.bits)));
    }
    return mask_to(value, to.bits);
}

enum class IntegerOperation {
    Add, Subtract, Multiply, Divide, Remainder, And, Or, Xor,
    ShiftLeft, ShiftRight, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual
};

enum class IntegerError { None, Overflow, DivisionByZero, ShiftCount };

struct IntegerResult {
    UInt128 value{};
    IntegerError error{};
};

// `wrap_signed` makes signed +, -, *, and << wrap modulo 2^N (-fwrapv);
// MIN / -1 and invalid shift counts remain errors.
inline IntegerResult checked_integer_operation(IntegerOperation operation,
                                              UInt128 left, UInt128 right,
                                              IntegerType type,
                                              bool wrap_signed = false) {
    const auto left_negative = integer_negative(left, type);
    const auto right_negative = integer_negative(right, type);
    const auto minimum = shift_left(UInt128{1}, type.bits - 1);
    const auto maximum = subtract(minimum, UInt128{1});
    const auto magnitude = [&](UInt128 value) {
        return integer_negative(value, type) ? mask_to(negate(value), type.bits)
                                             : value;
    };
    UInt128 result;
    switch (operation) {
    case IntegerOperation::Add:
    case IntegerOperation::Subtract: {
        const bool addition = operation == IntegerOperation::Add;
        result = mask_to(addition ? add(left, right) : subtract(left, right), type.bits);
        if (type.is_signed && !wrap_signed &&
            (addition ? left_negative == right_negative : left_negative != right_negative) &&
            integer_negative(result, type) != left_negative)
            return {{}, IntegerError::Overflow};
        break;
    }
    case IntegerOperation::Multiply:
        if (type.is_signed && !wrap_signed && right != UInt128{}) {
            const auto limit = left_negative != right_negative ? minimum : maximum;
            if (divide(limit, magnitude(right)).first < magnitude(left))
                return {{}, IntegerError::Overflow};
        }
        result = multiply(left, right);
        break;
    case IntegerOperation::Divide:
    case IntegerOperation::Remainder: {
        if (right == UInt128{}) return {{}, IntegerError::DivisionByZero};
        if (type.is_signed && left == minimum &&
            right == mask_to(bit_not(UInt128{}), type.bits))
            return {{}, IntegerError::Overflow};
        const auto [quotient, remainder] = divide(magnitude(left), magnitude(right));
        const bool division = operation == IntegerOperation::Divide;
        result = division ? quotient : remainder;
        if (division ? left_negative != right_negative : left_negative)
            result = negate(result);
        break;
    }
    case IntegerOperation::And: result = bit_and(left, right); break;
    case IntegerOperation::Or: result = bit_or(left, right); break;
    case IntegerOperation::Xor: result = bit_xor(left, right); break;
    case IntegerOperation::ShiftLeft:
    case IntegerOperation::ShiftRight: {
        if (right.high != 0 || right.low >= type.bits)
            return {{}, IntegerError::ShiftCount};
        const auto count = static_cast<unsigned>(right.low);
        if (operation == IntegerOperation::ShiftLeft) {
            if (type.is_signed && !wrap_signed &&
                (left_negative || shift_right(maximum, count) < left))
                return {{}, IntegerError::Overflow};
            result = shift_left(left, count);
        } else {
            result = shift_right(left, count);
            if (left_negative && count != 0)
                result = bit_or(result, shift_left(bit_not(UInt128{}), type.bits - count));
        }
        break;
    }
    default: {
        const bool less = left_negative != right_negative ? left_negative : left < right;
        const bool equal = left == right;
        const bool value = operation == IntegerOperation::Equal ? equal
                         : operation == IntegerOperation::NotEqual ? !equal
                         : operation == IntegerOperation::Less ? less
                         : operation == IntegerOperation::LessEqual ? less || equal
                         : operation == IntegerOperation::Greater ? !less && !equal
                         : !less;
        return {UInt128{value}, IntegerError::None};
    }
    }
    return {mask_to(result, type.bits), IntegerError::None};
}

} // namespace cross
