// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Floating operations on constants fold to the values the same operations
// compute at run time, and a division by a power of two whose reciprocal is
// normal becomes an exact multiplication. test_entry returns zero when every
// folded value matches the run-time operation in a function that sees only
// parameters, or a known bit pattern. It runs with -fno-eval-calls, so that
// the optimizer folds the operations.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };

static u32 b32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 b64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }
static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }

[[noinline]] global f32 add32(in f32 a, in f32 b) { return a + b; }
[[noinline]] global f32 sub32(in f32 a, in f32 b) { return a - b; }
[[noinline]] global f32 mul32(in f32 a, in f32 b) { return a * b; }
[[noinline]] global f32 div32(in f32 a, in f32 b) { return a / b; }
[[noinline]] global f64 add64(in f64 a, in f64 b) { return a + b; }
[[noinline]] global f64 mul64(in f64 a, in f64 b) { return a * b; }
[[noinline]] global f64 div64(in f64 a, in f64 b) { return a / b; }
[[noinline]] global f64 widen(in f32 a) { return (f64)a; }
[[noinline]] global f32 narrow(in f64 a) { return (f32)a; }
[[noinline]] global f32 from_i32(in i32 a) { return (f32)a; }
[[noinline]] global f64 from_u32(in u32 a) { return (f64)a; }
[[noinline]] global i32 to_i32(in f32 a) { return (i32)a; }
[[noinline]] global u32 to_u32(in f64 a) { return (u32)a; }
[[noinline]] global bool less64(in f64 a, in f64 b) { return a < b; }
[[noinline]] global bool equal64(in f64 a, in f64 b) { return a == b; }
[[noinline]] global bool zero32(in f32 a) { return !a; }
[[noinline]] global f64 root64(in f64 a) { return $::sqrt(a); }
[[noinline]] global f32 root32(in f32 a) { return $::sqrt(a); }
[[noinline]] global f32 minimum(in f32 a, in f32 b) { return $::fmin(a, b); }
[[noinline]] global f64 maximum(in f64 a, in f64 b) { return $::fmax(a, b); }

// Divisions by constants.
[[noinline]] global f32 eighth(in f32 a) { return a / 8.0f32; }
[[noinline]] global f32 doubled(in f32 a) { return a / 0.5f32; }
[[noinline]] global f64 scaled_down(in f64 a) { return a / 0x1p1023; }
[[noinline]] global f64 third(in f64 a) { return a / 3.0; }

global u32 test_entry() {
    // Arithmetic, including inexact, overflowing, and tiny results.
    if (b32(1.0f32 / 16777216.0f32) != b32($::runtime(div32(1.0f32, 16777216.0f32))))
        return 1u32;
    if (b32(0.1f32 + 0.2f32) != b32($::runtime(add32(0.1f32, 0.2f32)))) return 2u32;
    if (b32(1.0e38f32 * 10.0f32) != b32($::runtime(mul32(1.0e38f32, 10.0f32))))
        return 3u32;
    if (b32(1.0e-38f32 * 1.0e-5f32) != b32($::runtime(mul32(1.0e-38f32, 1.0e-5f32))))
        return 4u32;
    if (b32(1.0e-40f32 * 2.0f32) != 0x00022d84u32 ||
        b32($::runtime(mul32(1.0e-40f32, 2.0f32))) != 0x00022d84u32) return 5u32;
    if (b32(1.0f32 - 1.0f32) != b32($::runtime(sub32(1.0f32, 1.0f32)))) return 6u32;
    if (b32(-0.0f32 - 0.0f32) != b32($::runtime(sub32(-0.0f32, 0.0f32)))) return 7u32;
    if (b64(3.0 * 0.5 + 1.0) != b64($::runtime(add64($::runtime(mul64(3.0, 0.5)), 1.0))))
        return 8u32;
    if (b64(1.0 / 3.0) != b64($::runtime(div64(1.0, 3.0)))) return 9u32;
    if (b32(1.0f32 / 0.0f32) != b32($::runtime(div32(1.0f32, 0.0f32)))) return 10u32;
    // Conversions.
    if (b64((f64)0.1f32) != b64($::runtime(widen(0.1f32)))) return 11u32;
    if (b32((f32)0.1) != b32($::runtime(narrow(0.1)))) return 12u32;
    if (b32((f32)1.0e-310) != b32($::runtime(narrow(1.0e-310)))) return 13u32;
    if (b32((f32)16777217) != b32($::runtime(from_i32(16777217)))) return 14u32;
    if (b32((f32)-2147483647) != b32($::runtime(from_i32(-2147483647)))) return 15u32;
    if (b64((f64)4294967295u32) != b64($::runtime(from_u32(4294967295u32)))) return 16u32;
    if ((i32)-2.75f32 != $::runtime(to_i32(-2.75f32)) || (i32)-2.75f32 != -2) return 17u32;
    if ((u32)3.0e9 != $::runtime(to_u32(3.0e9)) || (u32)3.0e9 != 3000000000u32) return 18u32;
    // Comparisons and truth values.
    if ((0.1 + 0.2 == 0.3) != $::runtime(equal64(0.1 + 0.2, 0.3))) return 19u32;
    if (!(-0.0 == 0.0) || !$::runtime(equal64(-0.0, 0.0))) return 20u32;
    if (!(1.0 < 2.0) || (2.0 < 1.0) || $::runtime(less64(2.0, 1.0))) return 21u32;
    if (!!0.0f32 || !!-0.0f32 || !1.0e-45f32 || $::runtime(zero32(1.0e-45f32)))
        return 22u32;
    // Intrinsics: square roots, sign operations, and minNum/maxNum, whose
    // equal operands yield the first.
    if (b64($::sqrt(2.0)) != b64($::runtime(root64(2.0)))) return 23u32;
    if (b32($::sqrt(1.0e-40f32)) != b32($::runtime(root32(1.0e-40f32)))) return 24u32;
    if (b32($::fabs(-3.5f32)) != 0x40600000u32) return 25u32;
    if (b64($::copysign(2.0, -0.0)) != 0xc000000000000000u64) return 26u32;
    if (b32($::fmin(-0.0f32, 0.0f32)) != b32($::runtime(minimum(-0.0f32, 0.0f32))) ||
        b32($::fmin(-0.0f32, 0.0f32)) != 0x80000000u32) return 27u32;
    if (b64($::fmax(1.0, 2.0)) != b64($::runtime(maximum(1.0, 2.0)))) return 28u32;
    // Division by a power of two whose reciprocal is normal: exact on
    // denormal and finite operands, overflowing as the division does.
    if (b32($::runtime(eighth(f32_of(0x00000100u32)))) != 0x00000020u32) return 29u32;
    if (b32($::runtime(eighth(-3.0f32))) != 0xbec00000u32) return 30u32;
    if (b32($::runtime(eighth(f32_of(0x7f7fffffu32)))) != 0x7dffffffu32) return 31u32;
    if (b32($::runtime(doubled(f32_of(0x7f7fffffu32)))) != 0x7f800000u32) return 32u32;
    // 2^-1023 is denormal, and 1/3 is inexact: both stay divisions.
    if (b64($::runtime(scaled_down(1.0))) != 0x0008000000000000u64) return 33u32;
    if (b64($::runtime(third(1.0))) != 0x3fd5555555555555u64) return 34u32;
    return 0u32;
}
