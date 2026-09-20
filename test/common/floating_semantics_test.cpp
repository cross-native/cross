// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/floating_semantics.hpp"

#include <bit>
#include <cstdint>
#include <initializer_list>

using cross::UInt128;
using namespace cross::floating;

int main() {
    const auto f32 = Format::Binary32;
    const auto f64 = Format::Binary64;
    const auto f80 = Format::Extended80;
    const auto f128 = Format::Binary128;
    const auto one = *parse("1.0", f32);
    const auto half_ulp = *parse("0x1p-24", f32);
    const auto above_half = *parse("0x1.8p-24", f32);
    if (binary(Operation::Add, one, half_ulp, f32).bits != UInt128{0x3f800000}) return 1;
    if (binary(Operation::Add, one, above_half, f32).bits != UInt128{0x3f800001}) return 2;
    if (parse("1.000000059604644775390625", f32)->bits !=
        UInt128{0x3f800000}) return 2;
    if (parse("1.000000059604644775390626", f32)->bits !=
        UInt128{0x3f800001}) return 2;
    if (parse("0x1p-149", f32)->bits != UInt128{1}) return 3;
    if (parse("0x1p-150", f32)->bits != UInt128{}) return 4;
    if (parse("0x1.8p-150", f32)->bits != UInt128{1}) return 5;
    if (binary(Operation::Divide, one, *parse("3.0", f32), f32).bits !=
        UInt128{0x3eaaaaab}) return 6;
    if (parse("1.5", f80)->bits != UInt128{0xc000000000000000ULL, 0x3fff}) return 7;
    if (parse("1.5", f128)->bits != UInt128{0, 0x3fff800000000000ULL}) return 8;
    if (convert(*parse("1.5", f128), f64).bits != UInt128{0x3ff8000000000000ULL}) return 9;
    if (from_integer(UInt128{0xffffffff}, 32, true, f32).bits !=
        UInt128{0xbf800000}) return 10;
    if (to_integer(*parse("-1.5", f64), 32, true) != UInt128{0xffffffff}) return 11;
    if (to_integer(*parse("-0.5", f64), 32, false) != UInt128{}) return 12;
    const auto negative_zero = *parse("-0.0", f32);
    if (negative_zero.bits != UInt128{0x80000000} || nonzero(negative_zero)) return 13;
    if (!compare(Comparison::Equal, negative_zero, *parse("0.0", f32))) return 14;
    const auto infinity = binary(Operation::Divide, one, *parse("0.0", f32), f32);
    if (infinity.bits != UInt128{0x7f800000}) return 15;
    if (binary(Operation::Multiply, one, infinity, f32).bits !=
        UInt128{0x7f800000}) return 15;
    const auto invalid = binary(Operation::Subtract, infinity, infinity, f32);
    if (compare(Comparison::Equal, invalid, invalid) ||
        !compare(Comparison::NotEqual, invalid, invalid)) return 16;
    if (binary(Operation::Add, *parse("1.0", f128),
               *parse("0x1p-112", f128), f128).bits !=
        UInt128{1, 0x3fff000000000000ULL}) return 17;

    // Independent host IEEE operations cross-check thousands of exact target
    // results; exceptional NaN payloads are intentionally not specified.
    std::uint64_t state = 0x6a09e667f3bcc909ULL;
    const auto next = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int i = 0; i < 4000; ++i) {
        const auto bits_a = static_cast<std::uint32_t>(next());
        const auto bits_b = static_cast<std::uint32_t>(next());
        volatile float a = std::bit_cast<float>(bits_a);
        volatile float b = std::bit_cast<float>(bits_b);
        const Value soft_a{UInt128{bits_a}, f32};
        const Value soft_b{UInt128{bits_b}, f32};
        for (const auto op : {Operation::Add, Operation::Subtract,
                              Operation::Multiply, Operation::Divide}) {
            volatile float host = op == Operation::Add ? a + b
                : op == Operation::Subtract ? a - b
                : op == Operation::Multiply ? a * b : a / b;
            const auto actual = binary(op, soft_a, soft_b, f32).bits.low;
            const auto expected = std::bit_cast<std::uint32_t>(static_cast<float>(host));
            const bool both_nan = (actual & 0x7f800000U) == 0x7f800000U &&
                (actual & 0x007fffffU) != 0 &&
                (expected & 0x7f800000U) == 0x7f800000U &&
                (expected & 0x007fffffU) != 0;
            if (actual != expected && !both_nan) return 18;
        }
    }
    for (int i = 0; i < 2000; ++i) {
        const auto bits_a = next();
        const auto bits_b = next();
        volatile double a = std::bit_cast<double>(bits_a);
        volatile double b = std::bit_cast<double>(bits_b);
        const Value soft_a{UInt128{bits_a}, f64};
        const Value soft_b{UInt128{bits_b}, f64};
        for (const auto op : {Operation::Add, Operation::Subtract,
                              Operation::Multiply, Operation::Divide}) {
            volatile double host = op == Operation::Add ? a + b
                : op == Operation::Subtract ? a - b
                : op == Operation::Multiply ? a * b : a / b;
            const auto actual = binary(op, soft_a, soft_b, f64).bits.low;
            const auto expected = std::bit_cast<std::uint64_t>(static_cast<double>(host));
            const bool both_nan = (actual & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL &&
                (actual & 0x000fffffffffffffULL) != 0 &&
                (expected & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL &&
                (expected & 0x000fffffffffffffULL) != 0;
            if (actual != expected && !both_nan) return 19;
        }
    }
    return 0;
}
