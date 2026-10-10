// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The x87 f80 forms of the floating intrinsics and the binary128 sign
// operations on x86-64, checked against exactly computed encodings. The x87
// precision control must select a 64-bit significand.

struct words { u64 low; u64 high; };
union bits80 { f80 value; struct words parts; };
union bits128 { f128 value; struct words parts; };

static f80 f80_of(in u64 high, in u64 low) {
    union bits80 cell;
    cell.parts.low = low;
    cell.parts.high = high;
    return cell.value;
}
static bool f80_is(in f80 value, in u64 high, in u64 low) {
    union bits80 cell;
    cell.parts.low = 0u64;
    cell.parts.high = 0u64;
    cell.value = value;
    return (cell.parts.high & 0xffffu64) == high && cell.parts.low == low;
}
static bool f80_nan(in f80 value) { return value != value; }
static f128 f128_of(in u64 high, in u64 low) {
    union bits128 cell;
    cell.parts.low = low;
    cell.parts.high = high;
    return cell.value;
}
static bool f128_is(in f128 value, in u64 high, in u64 low) {
    union bits128 cell;
    cell.value = value;
    return cell.parts.high == high && cell.parts.low == low;
}

[[noinline]] global f80 sqrt80(in f80 x) { return $::sqrt(x); }
[[noinline]] global f80 fabs80(in f80 x) { return $::fabs(x); }
[[noinline]] global f80 copysign80(in f80 x, in f80 y) { return $::copysign(x, y); }
[[noinline]] global f80 fmin80(in f80 x, in f80 y) { return $::fmin(x, y); }
[[noinline]] global f80 fmax80(in f80 x, in f80 y) { return $::fmax(x, y); }
[[noinline]] global f128 fabs128(in f128 x) { return $::fabs(x); }
[[noinline]] global f128 copysign128(in f128 x, in f128 y) { return $::copysign(x, y); }

// Sign and exponent word, significand, and the correctly rounded root.
global volatile u64 sqrt80_cases[7][4] = {
    {0x4000u64, 0x8000000000000000u64, 0x3fffu64, 0xb504f333f9de6484u64},
    {0x3fffu64, 0x8000000000000000u64, 0x3fffu64, 0x8000000000000000u64},
    {0x4000u64, 0xc000000000000000u64, 0x3fffu64, 0xddb3d742c265539eu64},
    {0x0001u64, 0x8000000000000000u64, 0x2000u64, 0x8000000000000000u64},
    {0x0000u64, 0x0000000000000001u64, 0x1fe0u64, 0xb504f333f9de6484u64},
    {0x7ffeu64, 0xffffffffffffffffu64, 0x5ffeu64, 0xffffffffffffffffu64},
    {0x8000u64, 0x0000000000000000u64, 0x8000u64, 0x0000000000000000u64},
};
global volatile u64 nan80_high = 0xffffu64;
global volatile u64 nan80_low = 0xc000000000012345u64;

$::static_assert(f80_is($::sqrt(2.0f80), 0x3fffu64, 0xb504f333f9de6484u64), "f80 root");
$::static_assert(f80_is($::fabs(f80_of(0xffffu64, 0xc000000000012345u64)),
                        0x7fffu64, 0xc000000000012345u64), "f80 fabs keeps a payload");
$::static_assert(f80_is($::fmin(-0.0f80, 0.0f80), 0x8000u64, 0u64), "f80 equal zeros");
$::static_assert(f128_is($::copysign(1.5f128, -0.0f128), 0xbfff800000000000u64, 0u64),
                 "f128 copysign");

global u32 test_entry() {
    for (u32 i = 0u32; i < 7u32; ++i) {
        const f80 x = f80_of(sqrt80_cases[i][0], sqrt80_cases[i][1]);
        if (!f80_is(sqrt80(x), sqrt80_cases[i][2], sqrt80_cases[i][3])) return 1u32 + i;
        if (!f80_is(fabs80(x), sqrt80_cases[i][0] & 0x7fffu64, sqrt80_cases[i][1]))
            return 10u32 + i;
    }
    const f80 nan = f80_of(nan80_high, nan80_low);
    if (!f80_nan(sqrt80(nan)) || !f80_nan(sqrt80(-1.0f80))) return 20u32;
    if (!f80_is(fabs80(nan), 0x7fffu64, 0xc000000000012345u64)) return 21u32;
    if (!f80_is(copysign80(nan, 1.0f80), 0x7fffu64, 0xc000000000012345u64) ||
        !f80_is(copysign80(2.0f80, nan), 0xc000u64, 0x8000000000000000u64) ||
        !f80_is(copysign80(-2.0f80, 0.0f80), 0x4000u64, 0x8000000000000000u64))
        return 22u32;
    if (!f80_is(fmin80(nan, 1.5f80), 0x3fffu64, 0xc000000000000000u64) ||
        !f80_is(fmax80(1.5f80, nan), 0x3fffu64, 0xc000000000000000u64) ||
        !f80_is(fmin80(0.0f80, -0.0f80), 0u64, 0u64) ||
        !f80_is(fmax80(-0.0f80, 0.0f80), 0x8000u64, 0u64) ||
        !f80_is(fmin80(-3.0f80, 2.0f80), 0xc000u64, 0xc000000000000000u64) ||
        !f80_is(fmax80(-3.0f80, 2.0f80), 0x4000u64, 0x8000000000000000u64))
        return 23u32;
    if (!f80_is(fmax80(nan, nan), 0xffffu64, 0xc000000000012345u64)) return 24u32;
    const f128 wide_nan = f128_of(0xffff800000012345u64, 0x6789u64);
    if (!f128_is(fabs128(wide_nan), 0x7fff800000012345u64, 0x6789u64) ||
        !f128_is(copysign128(1.0f128, wide_nan), 0xbfff000000000000u64, 0u64) ||
        !f128_is(copysign128(wide_nan, 1.0f128), 0x7fff800000012345u64, 0x6789u64))
        return 25u32;
    if (!f80_is($::eval($::sqrt(3.0f80)), 0x3fffu64, 0xddb3d742c265539eu64) ||
        !f80_is(sqrt80(3.0f80), 0x3fffu64, 0xddb3d742c265539eu64))
        return 26u32;
    return 0u32;
}
