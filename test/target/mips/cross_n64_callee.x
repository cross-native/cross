// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Cross-ABI definitions for the 64-bit MIPS address model. This unit is
// compiled separately from its callers: cross_n64_caller.x and the startup
// in cross_n64_runtime.s, which places every argument by hand.

struct cross_n64_pair { u64 low; u64 high; };
struct cross_n64_spread { u64 parts[5]; };

// Twenty-four arguments: a0-a3, t0-t9, s0-s7, then two packed stack slots.
[[noinline]]
global u64 cross_n64_weighted(
    in u64 x0, in u64 x1, in u64 x2, in u64 x3, in u64 x4, in u64 x5,
    in u64 x6, in u64 x7, in u64 x8, in u64 x9, in u64 x10, in u64 x11,
    in u64 x12, in u64 x13, in u64 x14, in u64 x15, in u64 x16, in u64 x17,
    in u64 x18, in u64 x19, in u64 x20, in u64 x21, in u64 x22, in u64 x23) {
    return x0 + 2u64 * x1 + 3u64 * x2 + 4u64 * x3 + 5u64 * x4 +
           6u64 * x5 + 7u64 * x6 + 8u64 * x7 + 9u64 * x8 + 10u64 * x9 +
           11u64 * x10 + 12u64 * x11 + 13u64 * x12 + 14u64 * x13 +
           15u64 * x14 + 16u64 * x15 + 17u64 * x16 + 18u64 * x17 +
           19u64 * x18 + 20u64 * x19 + 21u64 * x20 + 22u64 * x21 +
           23u64 * x22 + 24u64 * x23;
}

// Integer and floating arguments use independent cursors: count in a0,
// scale in f12, bias in f14, and offset in a1.
[[noinline]]
global f64 cross_n64_mixed(in u32 count, in f64 scale, in f32 bias,
                           in i32 offset) {
    f64 widened = bias;
    f64 counted = count;
    f64 shifted = offset;
    return scale * 4.0f64 + widened + counted + shifted;
}

// Nine doubles: f12, f14, f16, f18, f4, f6, f8, f10, then one stack slot.
[[noinline]]
global f64 cross_n64_float_weighted(
    in f64 y0, in f64 y1, in f64 y2, in f64 y3, in f64 y4, in f64 y5,
    in f64 y6, in f64 y7, in f64 y8) {
    return y0 + 2.0f64 * y1 + 3.0f64 * y2 + 4.0f64 * y3 + 5.0f64 * y4 +
           6.0f64 * y5 + 7.0f64 * y6 + 8.0f64 * y7 + 9.0f64 * y8;
}

// A two-doubleword record arrives in a0/a1 and returns in v0/v1.
[[noinline]]
global struct cross_n64_pair cross_n64_swap(in struct cross_n64_pair value,
                                            in u64 bias) {
    struct cross_n64_pair result;
    result.low = value.high + bias;
    result.high = value.low ^ bias;
    return result;
}

// A five-doubleword result returns through a hidden pointer in a0, so
// `seed` arrives in a1.
[[noinline]]
global struct cross_n64_spread cross_n64_spread(in u64 seed) {
    struct cross_n64_spread result;
    result.parts[0] = seed;
    result.parts[1] = seed * 2u64 + 1u64;
    result.parts[2] = seed * 3u64 + 2u64;
    result.parts[3] = seed * 4u64 + 3u64;
    result.parts[4] = seed * 5u64 + 4u64;
    return result;
}

// Output channels are whole 64-bit pointers in a2 and a3.
[[noinline]]
global void cross_n64_divide(in u64 dividend, in u64 divisor,
                             out u64 quotient, out u64 remainder) {
    quotient = dividend / divisor;
    remainder = dividend % divisor;
}

[[noinline]]
global u64 cross_n64_leaf(in u64 value) {
    return value * 3u64 + 1u64;
}

// Values live across the calls stay in s-registers, which this definition
// must in turn preserve for its own caller.
[[noinline]]
global u64 cross_n64_keep(in u64 seed) {
    u64 a = seed + 1u64;
    u64 b = seed * 3u64;
    u64 c = seed ^ 0x55u64;
    u64 d = seed << 7u64;
    u64 e = seed - 9u64;
    u64 f = cross_n64_leaf(seed);
    u64 g = cross_n64_leaf(f);
    return a + 2u64 * b + 3u64 * c + 4u64 * d + 5u64 * e + 6u64 * f +
           7u64 * g;
}
