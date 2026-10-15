// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A select or branch that chooses between zero and one, a power of two, or
// all ones by a condition becomes the extended condition or its negation.
// test_entry returns zero when every result matches its expected value. It
// runs with -fno-eval-calls, so that the calls run.

[[noinline]] global i32 below(in i32 x) { return x < 10 ? 1 : 0; }
[[noinline]] global u32 at_least(in u32 x) { return x >= 1000u32 ? 1u32 : 0u32; }
[[noinline]] global u32 not_above(in u32 x, in u32 y) { return x > y ? 0u32 : 1u32; }
[[noinline]] global i32 flag(in i32 x, in i32 y) { return x == y ? 8 : 0; }
[[noinline]] global u64 wide(in u64 x) { return x != 0u64 ? 0u64 : 0x8000000000000000u64; }
[[noinline]] global i32 mask(in i32 x) { return x < 0 ? -1 : 0; }
[[noinline]] global u16 narrow(in u8 x) { return x > 7u8 ? 0u16 : 0xffffu16; }
// Selects of zero when an integer comparison holds negate the comparison.
[[noinline]] global i32 nonnegative(in i32 x) { return x < 0 ? 0 : 1; }
[[noinline]] global i32 small(in i32 x) { return x >= 5 ? 0 : 2; }
[[noinline]] global i32 not_positive(in i32 x) { return x <= 0 ? 0 : -1; }
[[noinline]] global i32 returned(in i32 x) {
    if (x > 3) return 4;
    return 0;
}
[[noinline]] global bool truth(in f64 x) { return x < 1.0 ? (bool)1u32 : (bool)0u32; }
// Floating comparisons negate with their truth value, not an inverted
// comparison, so NaN selects the other constant.
[[noinline]] global i32 unordered(in f64 x) { return x < 1.0 ? 0 : 2; }
[[noinline]] global i32 assigned(in i32 x) {
    i32 result;
    if (x != 0) result = 16;
    else result = 0;
    return result + 1;
}
// Other constants keep their branch or select.
[[noinline]] global i32 general(in i32 x) { return x < 10 ? 3 : 5; }

union bits64 { f64 value; u64 bits; };
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }

global u32 test_entry() {
    if (below(9) != 1 || below(10) != 0 || below(-2147483647 - 1) != 1 ||
        below(2147483647) != 0) return 1u32;
    if (at_least(999u32) != 0u32 || at_least(1000u32) != 1u32 ||
        at_least(4294967295u32) != 1u32) return 2u32;
    if (not_above(3u32, 4u32) != 1u32 || not_above(4u32, 4u32) != 1u32 ||
        not_above(5u32, 4u32) != 0u32) return 3u32;
    if (flag(3, 3) != 8 || flag(3, 4) != 0) return 4u32;
    if (wide(0u64) != 0x8000000000000000u64 || wide(1u64) != 0u64 ||
        wide(0x100000000u64) != 0u64) return 5u32;
    if (mask(-5) != -1 || mask(0) != 0 || mask(7) != 0) return 6u32;
    if (narrow(8u8) != 0u16 || narrow(7u8) != 0xffffu16) return 7u32;
    if (nonnegative(-1) != 0 || nonnegative(0) != 1 || nonnegative(1) != 1 ||
        small(4) != 2 || small(5) != 0 || not_positive(0) != 0 ||
        not_positive(1) != -1 || not_positive(-3) != 0) return 14u32;
    if (returned(4) != 4 || returned(3) != 0) return 8u32;
    if (!truth(0.5) || truth(1.0)) return 9u32;
    const f64 nan = f64_of(0x7ff8000000000001u64);
    if (truth(nan)) return 10u32;
    if (unordered(0.5) != 0 || unordered(2.0) != 2 || unordered(nan) != 2)
        return 11u32;
    if (assigned(5) != 17 || assigned(0) != 1) return 12u32;
    if (general(9) != 3 || general(10) != 5) return 13u32;
    return 0u32;
}
