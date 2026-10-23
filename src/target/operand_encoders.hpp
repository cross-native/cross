// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

// The general encoders of instruction operands: the field values of operand
// values that an instruction word does not hold as they are. Any
// architecture's operand types may use them.
namespace cross::encoders {

// `bitmask`: a value of `bits` (32 or 64) bits that repeats an element of
// 2, 4, 8, 16, 32, or 64 bits holding one rotated run of ones, as N:immr:imms
// (13 bits; N is 0 for 32 bits). None for zero, all ones, and other values.
[[nodiscard]] std::optional<std::uint32_t> bitmask(std::uint64_t value, unsigned bits);

// `fp8`: a floating value +-(16 + m) / 16 * 2^e with m from 0 to 15 and e from
// -3 to 4, as sign:exponent:mantissa in 8 bits. None for other values.
[[nodiscard]] std::optional<std::uint32_t> fp8(double value);

// `shifted`: a value of `bits` bits, or one shifted left by `shift` bits, as
// shift:value in bits + 1 bits; unshifted where both apply.
[[nodiscard]] std::optional<std::uint32_t> shifted(std::uint64_t value, unsigned bits,
                                                   unsigned shift);

// `wide`: a value of `bits` (32 or 64) bits whose set bits lie in one
// 16-bit piece at a multiple of 16, as hw:imm16 (18 bits; 17 for 32 bits),
// hw counting pieces. Zero is the piece at 0.
[[nodiscard]] std::optional<std::uint32_t> wide(std::uint64_t value, unsigned bits);

// `scaled`: a multiple of `scale` whose quotient lies from `minimum` to
// `maximum`, as the quotient.
[[nodiscard]] std::optional<std::int64_t> scaled(std::int64_t value, std::int64_t scale,
                                                 std::int64_t minimum, std::int64_t maximum);

// `page`: the distance in 4 KiB pages from the page of `place` to that of
// `target`.
[[nodiscard]] std::int64_t page(std::uint64_t target, std::uint64_t place);

} // namespace cross::encoders
