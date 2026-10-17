// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// n64 entry points that call the Cross-ABI definitions of a separately
// compiled unit (x64_callee.x). The startup calls these entries with
// n64 conventions and checks the registers n64 preserves around them.

struct x64_pair { u64 low; u64 high; };
struct x64_spread { u64 parts[5]; };

global u64 x64_weighted(
    in u64 x0, in u64 x1, in u64 x2, in u64 x3, in u64 x4, in u64 x5,
    in u64 x6, in u64 x7, in u64 x8, in u64 x9, in u64 x10, in u64 x11,
    in u64 x12, in u64 x13, in u64 x14, in u64 x15, in u64 x16, in u64 x17,
    in u64 x18, in u64 x19, in u64 x20, in u64 x21, in u64 x22, in u64 x23);
global f64 x64_mixed(in u32 count, in f64 scale, in f32 bias,
                     in i32 offset);
global f64 x64_float_weighted(
    in f64 y0, in f64 y1, in f64 y2, in f64 y3, in f64 y4, in f64 y5,
    in f64 y6, in f64 y7, in f64 y8);
global struct x64_pair x64_swap(in struct x64_pair value,
                                in u64 bias);
global struct x64_spread x64_spread(in u64 seed);
global void x64_divide(in u64 dividend, in u64 divisor,
                       out u64 quotient, out u64 remainder);
global u64 x64_leaf(in u64 value);
global u64 x64_keep(in u64 seed);

// On a mips64 triple the name `x` denotes x64, the 64-bit-register Cross ABI.
[[abi("x")]] typedef u64 (*x64_named)(in u64 value);
typedef u64 (*x64_unary)(in u64 value);
[[abi("n64")]] typedef u64 (*x64_platform)(in u64 value);

global x64_named x64_named_target = x64_leaf;
global x64_unary x64_default_target = x64_keep;

// Three values stay live across both calls.
[[abi("n64"), noinline]]
global u64 x64_entry_weighted(in u64 seed) {
    u64 kept0 = seed * 5u64 + 3u64;
    u64 kept1 = seed ^ 0x0123456789abcdefu64;
    u64 kept2 = seed << 9u64;
    u64 sum = x64_weighted(
        seed, seed + 1u64, seed + 2u64, seed + 3u64, seed + 4u64,
        seed + 5u64, seed + 6u64, seed + 7u64, seed + 8u64, seed + 9u64,
        seed + 10u64, seed + 11u64, seed + 12u64, seed + 13u64,
        seed + 14u64, seed + 15u64, seed + 16u64, seed + 17u64,
        seed + 18u64, seed + 19u64, seed + 20u64, seed + 21u64,
        seed + 22u64, seed + 23u64);
    u64 kept = x64_keep(sum);
    return sum + kept0 * 3u64 + (kept1 ^ kept) + kept2;
}

[[abi("n64"), noinline]]
global f64 x64_entry_floats(in f64 base) {
    f64 kept = base * 0.5f64;
    f64 mixed = x64_mixed(7u32, base, 0.25f32, -3);
    f64 weighted = x64_float_weighted(
        base, base + 1.0f64, base + 2.0f64, base + 3.0f64, base + 4.0f64,
        base + 5.0f64, base + 6.0f64, base + 7.0f64, base + 8.0f64);
    return mixed + weighted + kept;
}

[[abi("n64"), noinline]]
global u64 x64_entry_records(in u64 seed) {
    struct x64_pair input;
    input.low = seed;
    input.high = seed * 11u64;
    struct x64_pair swapped = x64_swap(input, 0x1000u64);
    struct x64_spread spread = x64_spread(swapped.low);
    u64 quotient;
    u64 remainder;
    x64_divide(spread.parts[4], 1000u64, quotient, remainder);
    return swapped.high + spread.parts[1] + spread.parts[3] * 7u64 +
           quotient * 13u64 + remainder;
}

// Indirect calls through the explicit `x`, the default, and an n64
// pointer type; the last converts a Cross function through a wrapper.
[[abi("n64"), noinline]]
global u64 x64_entry_indirect(in u64 seed) {
    x64_platform platform = x64_leaf;
    u64 first = x64_named_target(seed);
    u64 second = x64_default_target(first);
    u64 third = platform(second);
    return first + 3u64 * second + 5u64 * third;
}

// With private-ABI optimization this helper uses the 64-bit-address Cross
// convention, so its pointer must still arrive whole.
[[noinline]]
static u64 x64_private(in u64 *cells, in f64 scale, in u64 addend) {
    u64 loaded = cells[0] + cells[1];
    cells[2] = loaded + addend;
    if (scale > 2.0f64) return loaded + addend + 1u64;
    return loaded + addend;
}

[[abi("n64"), noinline]]
global u64 x64_entry_private(in u64 *cells, in u64 addend) {
    return x64_private(cells, 2.5f64, addend) + cells[2];
}

// The startup implements this Cross function in assembly. It returns
// 2 * value + 1 after clobbering every register the Cross contract permits,
// including gp and f24-f31, which this n64 entry must restore.
global u64 x64_clobber(in u64 value);

[[abi("n64"), noinline]]
global u64 x64_entry_bridge(in u64 seed, in f64 scale) {
    u64 kept0 = seed + 11u64;
    u64 kept1 = seed * 13u64;
    u64 first = x64_clobber(seed);
    u64 second = x64_clobber(first ^ kept0);
    u64 result = first + second + kept0 * 3u64 + kept1;
    if (scale > 1.0f64) result = result + 7u64;
    return result;
}
