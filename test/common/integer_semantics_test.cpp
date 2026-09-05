// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/integer_semantics.hpp"

#include <cstdint>

using cross::IntegerOperation;
using cross::IntegerType;
using cross::UInt128;

int main() {
    const UInt128 dividend{~std::uint64_t{}, ~std::uint64_t{}};
    const UInt128 divisor{1, std::uint64_t{1} << 63};
    const auto qr = cross::divide(dividend, divisor);
    if (qr.first != UInt128{1} ||
        qr.second != UInt128{std::uint64_t{0xfffffffffffffffe},
                             std::uint64_t{0x7fffffffffffffff}})
        return 1;

    const IntegerType i8{8, true, false};
    const IntegerType i32{32, true, false};
    const IntegerType u32{32, false, false};
    auto shift = cross::checked_integer_operation(
        IntegerOperation::ShiftRight, UInt128{0xfc}, UInt128{1}, i8);
    if (shift.error != cross::IntegerError::None || shift.value != UInt128{0xfe})
        return 2;
    auto mixed = cross::checked_integer_operation(
        IntegerOperation::Add, cross::convert_integer(UInt128{0xffffffff}, u32, i32),
        UInt128{1}, i32);
    if (mixed.error != cross::IntegerError::None || mixed.value != UInt128{})
        return 3;
    auto overflow = cross::checked_integer_operation(
        IntegerOperation::Add, UInt128{0x7fffffff}, UInt128{1}, i32);
    if (overflow.error != cross::IntegerError::Overflow)
        return 4;
    auto divzero = cross::checked_integer_operation(
        IntegerOperation::Divide, UInt128{1}, UInt128{}, i32);
    if (divzero.error != cross::IntegerError::DivisionByZero)
        return 5;
    auto badshift = cross::checked_integer_operation(
        IntegerOperation::ShiftLeft, UInt128{1}, UInt128{32}, i32);
    if (badshift.error != cross::IntegerError::ShiftCount)
        return 6;
    return 0;
}
