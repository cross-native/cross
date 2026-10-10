// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compiled with -fwrapv: signed +, -, *, and << wrap modulo 2^N, both in
// translation-time evaluation and at run time, so no optimization may assume
// that they do not overflow.

$::static_assert($::eval(2147483647i32 + 1i32) == -2147483647i32 - 1i32, "add wraps");
$::static_assert($::eval(-9223372036854775807i64 - 2i64) == 9223372036854775807i64, "sub wraps");
$::static_assert($::eval(65536i32 * 65536i32) == 0i32, "mul wraps");
$::static_assert($::eval(3i32 << 31) == -2147483647i32 - 1i32, "shl wraps");

global volatile i32 max32 = 2147483647i32;
global volatile i32 min32 = -2147483647i32 - 1i32;
global volatile i64 max64 = 9223372036854775807i64;
global volatile i64 min64 = -9223372036854775807i64 - 1i64;

[[noinline]] global bool add_greater32(in i32 x) { return x + 1i32 > x; }
[[noinline]] global bool add_greater64(in i64 x) { return x + 1i64 > x; }
[[noinline]] global bool less_add32(in i32 x) { return x < x + 1i32; }
[[noinline]] global bool difference_negative32(in i32 x, in i32 y) { return x - y < 0i32; }
[[noinline]] global bool difference_negative64(in i64 x, in i64 y) { return x - y < 0i64; }
[[noinline]] global bool add_positive32(in i32 x) { return x + 1i32 > 0i32; }
[[noinline]] global bool add_positive64(in i64 x) { return x + 1i64 > 0i64; }
[[noinline]] global bool negate_positive32(in i32 x) { return -x > 0i32; }
[[noinline]] global i32 double_half32(in i32 x) { return x * 2i32 / 2i32; }
[[noinline]] global i64 double_half64(in i64 x) { return x * 2i64 / 2i64; }
[[noinline]] global i32 shift_back32(in i32 x) { return (x << 1) >> 1; }
[[noinline]] global bool shift_negative32(in i32 x) { return (x << 1) < 0i32; }
[[noinline]] global bool triple_negative32(in i32 x) { return x * 3i32 < 0i32; }
[[noinline]] global i32 halve_next32(in i32 x) { return (x + 1i32) / 2i32; }
[[noinline]] global i64 widen_next32(in i32 x) { return (i64)(x + 1i32); }
[[noinline]] global i64 widen_scaled32(in i32 x) { return (i64)(x * 4i32); }
[[noinline]] global i32 magnitude32(in i32 x) { return x < 0i32 ? -x : x; }
[[noinline]] global bool same_offset32(in i32 x, in i32 y) { return x + 5i32 < y + 5i32; }
[[noinline]] global i32 sum_compare32(in i32 x, in i32 c) { return x + c > x ? 1i32 : 2i32; }

// The induction wraps past the maximum, which ends the loop.
[[noinline]] global u32 wrapping_count32(in i32 start, in i32 step) {
    u32 count = 0u32;
    for (i32 i = start; i >= start; i += step) ++count;
    return count;
}
[[noinline]] global u32 wrapping_count64(in i64 start, in i64 step) {
    u32 count = 0u32;
    for (i64 i = start; i >= start; i += step) ++count;
    return count;
}
// A sign-extended induction must not be widened as if it could not wrap.
[[noinline]] global i64 widened_sum32(in i32 start, in i32 end) {
    i64 sum = 0i64;
    for (i32 i = start; i != end; ++i) sum += (i64)i;
    return sum;
}
[[noinline]] global i32 product_sum32(in i32 x, in i32 n) {
    i32 sum = 0i32;
    for (i32 i = 0i32; i < n; ++i) sum += x * i;
    return sum;
}

global u32 test_entry() {
    const i32 imax = max32;
    const i32 imin = min32;
    const i64 lmax = max64;
    const i64 lmin = min64;
    if (add_greater32(imax)) return 1u32;
    if (add_greater64(lmax)) return 2u32;
    if (less_add32(imax)) return 3u32;
    if (difference_negative32(imin, 1i32)) return 4u32;
    if (difference_negative64(lmin, 1i64)) return 5u32;
    if (add_positive32(imax)) return 6u32;
    if (add_positive64(lmax)) return 7u32;
    if (negate_positive32(imin)) return 8u32;
    if (double_half32(0x40000000i32) != -0x40000000i32) return 9u32;
    if (double_half64(0x4000000000000000i64) != -0x4000000000000000i64) return 10u32;
    if (shift_back32(0x40000000i32) != -0x40000000i32) return 11u32;
    if (!shift_negative32(0x40000000i32)) return 12u32;
    if (!triple_negative32(0x2aaaaaabi32)) return 13u32;
    if (halve_next32(imax) != -0x40000000i32) return 14u32;
    if (widen_next32(imax) != -2147483648i64) return 15u32;
    if (widen_scaled32(0x40000000i32) != 0i64) return 16u32;
    if (magnitude32(imin) != imin) return 17u32;
    if (!same_offset32(imax, 0i32)) return 18u32;
    if (sum_compare32(imax, 1i32) != 2i32) return 19u32;
    if (wrapping_count32(imax - 10i32, 4i32) != 3u32) return 20u32;
    if (wrapping_count64(lmax - 10i64, 4i64) != 3u32) return 21u32;
    if (widened_sum32(imax - 1i32, imin + 1i32) != 2147483646i64 + 2147483647i64 - 2147483648i64)
        return 22u32;
    if (product_sum32(0x40000000i32, 4i32) != -0x40000000i32 * 2i32) return 23u32;
    // Constant operands fold with the same wrapping.
    i32 a = 2147483647i32;
    if (a + 1i32 > a) return 24u32;
    if (a * 2i32 != -2i32) return 25u32;
    if ((a << 1) != -2i32) return 26u32;
    i64 b = -9223372036854775807i64 - 1i64;
    if (b - 1i64 < b) return 27u32;
    if (-b != b) return 28u32;
    return 0u32;
}
