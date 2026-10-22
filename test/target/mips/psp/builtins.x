// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Allegrex instructions through their built-ins, which the compiler emits as
// words. allegrex_builtins_entry returns zero when each result is right,
// else the failed checks.

global volatile u32 builtin_a = 0x12345678u32;
global volatile u32 builtin_b = 0x0000f00fu32;

[[noinline]]
static u32 extensions() {
    const u32 a = builtin_a;
    const u32 b = builtin_b;
    u32 high;
    u32 low;
    u32 reversed;
    u32 swapped;
    u32 count;
    $::_max(high, a, b);
    $::_min(low, a, b);
    $::_bitrev(reversed, a);
    $::_wsbw(swapped, a);
    $::_clz(count, b);
    return high == a && low == b && reversed == 0x1e6a2c48u32 &&
           swapped == 0x78563412u32 && count == 16u32;
}

[[noinline]]
static u32 release2_forms() {
    const u32 a = builtin_a;
    const u32 b = builtin_b;
    u32 field;
    u32 inserted = 0u32;
    u32 extended;
    u32 rotated;
    $::_ext(field, a, 8, 8);
    $::_ins(inserted, a, 4, 4);
    $::_seh(extended, b);
    $::_rotr(rotated, a, 4);
    return field == 0x56u32 && inserted == 0x80u32 &&
           extended == 0xfffff00fu32 && rotated == 0x81234567u32;
}

[[noinline]]
static u32 conditional_moves() {
    const u32 a = builtin_a;
    const u32 b = builtin_b;
    u32 kept = b;
    u32 moved = 0u32;
    $::_movz(kept, a, b);
    $::_movn(moved, a, b);
    return kept == b && moved == a;
}

// MADD accumulates into HI and LO.
[[noinline]]
static u32 accumulate() {
    const u32 a = builtin_a;
    const u32 b = builtin_b;
    u32 low;
    $::_mult(a, b);
    $::_madd(a, b);
    $::_mflo(low);
    return low == a * b * 2u32;
}

[[runtime_only, noinline, link_name("allegrex_builtins_entry")]]
global u32 allegrex_builtins_entry() {
    u32 failed = 0u32;
    if (!extensions()) failed |= 1u32;
    if (!release2_forms()) failed |= 2u32;
    if (!conditional_moves()) failed |= 4u32;
    if (!accumulate()) failed |= 8u32;
    return failed;
}
