// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// $::sqrt, $::fabs, $::copysign, $::fmin, and $::fmax on f32 and f64: the
// correctly rounded square root, sign-bit operations that keep NaN payloads,
// and minNum/maxNum, whose NaN left operand yields the right operand and
// whose equal operands yield the left one. Run-time results match exactly
// computed bits, and translation-time evaluation agrees with them. A target
// without an inline square root, such as the R3000, reports it through
// $::has_intrinsic.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };

static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }
static u32 bits_of32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 bits_of64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }
static bool nan32(in u32 bits) { return (bits & 0x7fffffffu32) > 0x7f800000u32; }
static bool nan64(in u64 bits) {
    return (bits & 0x7fffffffffffffffu64) > 0x7ff0000000000000u64;
}

#if $::has_intrinsic($::sqrt)
[[noinline]] global f32 sqrt32(in f32 x) { return $::sqrt(x); }
[[noinline]] global f64 sqrt64(in f64 x) { return $::sqrt(x); }
#endif
[[noinline]] global f32 fabs32(in f32 x) { return $::fabs(x); }
[[noinline]] global f64 fabs64(in f64 x) { return $::fabs(x); }
[[noinline]] global f32 copysign32(in f32 x, in f32 y) { return $::copysign(x, y); }
[[noinline]] global f64 copysign64(in f64 x, in f64 y) { return $::copysign(x, y); }
[[noinline]] global f32 fmin32(in f32 x, in f32 y) { return $::fmin(x, y); }
[[noinline]] global f64 fmin64(in f64 x, in f64 y) { return $::fmin(x, y); }
[[noinline]] global f32 fmax32(in f32 x, in f32 y) { return $::fmax(x, y); }
[[noinline]] global f64 fmax64(in f64 x, in f64 y) { return $::fmax(x, y); }

// Input and correctly rounded root; any NaN result is accepted for a NaN.
global volatile u32 sqrt32_cases[18][2] = {
    {0x40000000u32, 0x3fb504f3u32},
    {0x40800000u32, 0x40000000u32},
    {0x3f800000u32, 0x3f800000u32},
    {0x00000001u32, 0x1a3504f3u32},
    {0x007fffffu32, 0x1fffffffu32},
    {0x00800000u32, 0x20000000u32},
    {0x7f7fffffu32, 0x5f7fffffu32},
    {0x80000000u32, 0x80000000u32},
    {0x00000000u32, 0x00000000u32},
    {0x7f800000u32, 0x7f800000u32},
    {0x3fc00000u32, 0x3f9cc471u32},
    {0x40490fdbu32, 0x3fe2dfc5u32},
    {0x2e9a5b7cu32, 0x370c8fefu32},
    {0xbf800000u32, 0x7fc00000u32},
    {0xff800000u32, 0x7fc00000u32},
    {0x7fc00000u32, 0x7fc00000u32},
    {0xffc00001u32, 0x7fc00000u32},
    {0x80000001u32, 0x7fc00000u32},
};
global volatile u64 sqrt64_cases[17][2] = {
    {0x4000000000000000u64, 0x3ff6a09e667f3bcdu64},
    {0x3ff0000000000000u64, 0x3ff0000000000000u64},
    {0x0000000000000001u64, 0x1e60000000000000u64},
    {0x000fffffffffffffu64, 0x1fffffffffffffffu64},
    {0x0010000000000000u64, 0x2000000000000000u64},
    {0x7fefffffffffffffu64, 0x5fefffffffffffffu64},
    {0x8000000000000000u64, 0x8000000000000000u64},
    {0x0000000000000000u64, 0x0000000000000000u64},
    {0x7ff0000000000000u64, 0x7ff0000000000000u64},
    {0x400921fb54442d18u64, 0x3ffc5bf891b4ef6au64},
    {0x3fb999999999999au64, 0x3fd43d136248490fu64},
    {0x4341c37937e08000u64, 0x4197d78400000000u64},
    {0xbff0000000000000u64, 0x7ff8000000000000u64},
    {0xfff0000000000000u64, 0x7ff8000000000000u64},
    {0x7ff8000000000000u64, 0x7ff8000000000000u64},
    {0xfff8000000000001u64, 0x7ff8000000000000u64},
    {0x8000000000000001u64, 0x7ff8000000000000u64},
};
// x, y, fmin(x, y), fmax(x, y), copysign(x, y).
global volatile u32 pair32_cases[15][5] = {
    {0x3f800000u32, 0x40000000u32, 0x3f800000u32, 0x40000000u32, 0x3f800000u32},
    {0x40000000u32, 0x3f800000u32, 0x3f800000u32, 0x40000000u32, 0x40000000u32},
    {0x00000000u32, 0x80000000u32, 0x00000000u32, 0x00000000u32, 0x80000000u32},
    {0x80000000u32, 0x00000000u32, 0x80000000u32, 0x80000000u32, 0x00000000u32},
    {0x7fc12345u32, 0x3fc00000u32, 0x3fc00000u32, 0x3fc00000u32, 0x7fc12345u32},
    {0x3fc00000u32, 0x7fc12345u32, 0x3fc00000u32, 0x3fc00000u32, 0x3fc00000u32},
    {0xffc00001u32, 0xff800000u32, 0xff800000u32, 0xff800000u32, 0xffc00001u32},
    {0x7fc00001u32, 0x7fc00002u32, 0x7fc00002u32, 0x7fc00002u32, 0x7fc00001u32},
    {0xff800000u32, 0x7f800000u32, 0xff800000u32, 0x7f800000u32, 0x7f800000u32},
    {0x00000001u32, 0x00000002u32, 0x00000001u32, 0x00000002u32, 0x00000001u32},
    {0x80000001u32, 0x00000000u32, 0x80000001u32, 0x00000000u32, 0x00000001u32},
    {0x7f800001u32, 0x40400000u32, 0x40400000u32, 0x40400000u32, 0x7f800001u32},
    {0x40400000u32, 0x7f800001u32, 0x40400000u32, 0x40400000u32, 0x40400000u32},
    {0x40a00000u32, 0x40a00000u32, 0x40a00000u32, 0x40a00000u32, 0x40a00000u32},
    {0xc0200000u32, 0xbf000000u32, 0xc0200000u32, 0xbf000000u32, 0xc0200000u32},
};
global volatile u64 pair64_cases[15][5] = {
    {0x3ff0000000000000u64, 0x4000000000000000u64, 0x3ff0000000000000u64, 0x4000000000000000u64, 0x3ff0000000000000u64},
    {0x4000000000000000u64, 0x3ff0000000000000u64, 0x3ff0000000000000u64, 0x4000000000000000u64, 0x4000000000000000u64},
    {0x0000000000000000u64, 0x8000000000000000u64, 0x0000000000000000u64, 0x0000000000000000u64, 0x8000000000000000u64},
    {0x8000000000000000u64, 0x0000000000000000u64, 0x8000000000000000u64, 0x8000000000000000u64, 0x0000000000000000u64},
    {0x7ff8000000012345u64, 0x3ff8000000000000u64, 0x3ff8000000000000u64, 0x3ff8000000000000u64, 0x7ff8000000012345u64},
    {0x3ff8000000000000u64, 0x7ff8000000012345u64, 0x3ff8000000000000u64, 0x3ff8000000000000u64, 0x3ff8000000000000u64},
    {0xfff8000000000001u64, 0xfff0000000000000u64, 0xfff0000000000000u64, 0xfff0000000000000u64, 0xfff8000000000001u64},
    {0x7ff8000000000001u64, 0x7ff8000000000002u64, 0x7ff8000000000002u64, 0x7ff8000000000002u64, 0x7ff8000000000001u64},
    {0xfff0000000000000u64, 0x7ff0000000000000u64, 0xfff0000000000000u64, 0x7ff0000000000000u64, 0x7ff0000000000000u64},
    {0x0000000000000001u64, 0x0000000000000002u64, 0x0000000000000001u64, 0x0000000000000002u64, 0x0000000000000001u64},
    {0x8000000000000001u64, 0x0000000000000000u64, 0x8000000000000001u64, 0x0000000000000000u64, 0x0000000000000001u64},
    {0x7ff0000000000001u64, 0x4008000000000000u64, 0x4008000000000000u64, 0x4008000000000000u64, 0x7ff0000000000001u64},
    {0x4008000000000000u64, 0x7ff0000000000001u64, 0x4008000000000000u64, 0x4008000000000000u64, 0x4008000000000000u64},
    {0x4014000000000000u64, 0x4014000000000000u64, 0x4014000000000000u64, 0x4014000000000000u64, 0x4014000000000000u64},
    {0xc004000000000000u64, 0xbfe0000000000000u64, 0xc004000000000000u64, 0xbfe0000000000000u64, 0xc004000000000000u64},
};

$::static_assert(bits_of32($::sqrt(2.0f32)) == 0x3fb504f3u32, "f32 square root");
$::static_assert(bits_of64($::sqrt(2.0)) == 0x3ff6a09e667f3bcdu64, "f64 square root");
$::static_assert(bits_of64($::sqrt(f64_of(1u64))) == 0x1e60000000000000u64, "subnormal root");
$::static_assert(bits_of64($::sqrt(-0.0)) == 0x8000000000000000u64, "root of -0");
$::static_assert(nan64(bits_of64($::sqrt(-1.0))), "root of a negative");
$::static_assert(bits_of32($::fabs(f32_of(0xffc12345u32))) == 0x7fc12345u32, "fabs keeps a payload");
$::static_assert(bits_of64($::copysign(f64_of(0x7ff8000000012345u64), -1.0)) ==
                 0xfff8000000012345u64, "copysign onto a NaN");
$::static_assert(bits_of64($::copysign(1.5, -0.0)) == 0xbff8000000000000u64, "copysign of -0");
$::static_assert(bits_of32($::fmin(0.0f32, -0.0f32)) == 0u32 &&
                 bits_of32($::fmin(-0.0f32, 0.0f32)) == 0x80000000u32, "equal zeros");
$::static_assert($::fmin(f64_of(0x7ff8000000012345u64), 1.5) == 1.5 &&
                 $::fmax(1.5, f64_of(0x7ff8000000012345u64)) == 1.5, "a NaN operand");
$::static_assert(bits_of64($::fmax(f64_of(0x7ff8000000000001u64),
                                   f64_of(0x7ff8000000000002u64))) == 0x7ff8000000000002u64,
                 "two NaNs");
$::static_assert($::fmin(-2.5, -0.5) == -2.5 && $::fmax(-2.5, -0.5) == -0.5, "ordering");

static bool same32(in u32 actual, in u32 expected) {
    return nan32(expected) ? nan32(actual) : actual == expected;
}
static bool same64(in u64 actual, in u64 expected) {
    return nan64(expected) ? nan64(actual) : actual == expected;
}

global u32 test_entry() {
    for (u32 i = 0u32; i < 18u32; ++i) {
        const u32 x = sqrt32_cases[i][0];
#if $::has_intrinsic($::sqrt)
        if (!same32(bits_of32(sqrt32(f32_of(x))), sqrt32_cases[i][1])) return 1u32 + i;
#endif
        if (bits_of32(fabs32(f32_of(x))) != (x & 0x7fffffffu32)) return 20u32 + i;
    }
    for (u32 i = 0u32; i < 17u32; ++i) {
        const u64 x = sqrt64_cases[i][0];
#if $::has_intrinsic($::sqrt)
        if (!same64(bits_of64(sqrt64(f64_of(x))), sqrt64_cases[i][1])) return 40u32 + i;
#endif
        if (bits_of64(fabs64(f64_of(x))) != (x & 0x7fffffffffffffffu64)) return 60u32 + i;
    }
    for (u32 i = 0u32; i < 15u32; ++i) {
        const f32 x = f32_of(pair32_cases[i][0]);
        const f32 y = f32_of(pair32_cases[i][1]);
        if (bits_of32(fmin32(x, y)) != pair32_cases[i][2]) return 80u32 + i;
        if (bits_of32(fmax32(x, y)) != pair32_cases[i][3]) return 100u32 + i;
        if (bits_of32(copysign32(x, y)) != pair32_cases[i][4]) return 120u32 + i;
    }
    for (u32 i = 0u32; i < 15u32; ++i) {
        const f64 x = f64_of(pair64_cases[i][0]);
        const f64 y = f64_of(pair64_cases[i][1]);
        if (bits_of64(fmin64(x, y)) != pair64_cases[i][2]) return 140u32 + i;
        if (bits_of64(fmax64(x, y)) != pair64_cases[i][3]) return 160u32 + i;
        if (bits_of64(copysign64(x, y)) != pair64_cases[i][4]) return 180u32 + i;
    }
#if $::has_intrinsic($::sqrt)
    // Translation-time results equal the run-time ones.
    const f64 three = f64_of(sqrt64_cases[0][0] + 0x0008000000000000u64);
    if (bits_of64($::eval($::sqrt(3.0))) != bits_of64(sqrt64(three))) return 200u32;
#endif
    if (bits_of32($::eval($::fmin(f32_of(0x7fc12345u32), -0.0f32))) !=
        bits_of32(fmin32(f32_of(pair32_cases[4][0]), -0.0f32)))
        return 201u32;
    return 0u32;
}
