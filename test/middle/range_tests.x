// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Tests of one integer against constants joined by && or || become one
// comparison where the values that pass form one range. test_entry returns
// zero when every combined test agrees with its tests evaluated separately
// and combined behind a call; it runs with -fno-eval-calls, so that the
// calls run.

[[noinline]] global bool either(in bool a, in bool b) { return a || b; }
[[noinline]] global bool both(in bool a, in bool b) { return a && b; }

[[noinline]] global bool outside(in i32 x) {
    return x < -1048576 || x > 1048576;
}
[[noinline]] global bool inside(in i32 x) { return x >= -5 && x <= 7; }
[[noinline]] global bool mirrored(in i32 x) { return 10 < x && 20 > x; }
[[noinline]] global bool gap(in i32 x) { return x == 1 || x == 3; }
[[noinline]] global bool pair(in u8 x) { return x == 3u8 || x == 4u8; }
[[noinline]] global bool neither(in u32 x) { return x != 0u32 && x != 1u32; }
[[noinline]] global bool ends(in u32 x) {
    return x < 10u32 || x > 4294967280u32;
}
[[noinline]] global bool three(in u16 x) {
    return x == 1u16 || x == 2u16 || x == 3u16;
}
[[noinline]] global bool shifted(in u32 x) {
    return x - 10u32 < 5u32 || x == 15u32;
}
[[noinline]] global bool everything(in i16 x) { return x < 0i16 || x >= 0i16; }
[[noinline]] global bool nothing(in i64 x) { return x > 5i64 && x < 3i64; }
[[noinline]] global bool around_sign(in u64 x) {
    return x >= 9223372036854775800u64 && x <= 9223372036854775815u64;
}
[[noinline]] global bool extremes(in i64 x) {
    return x > 9223372036854775800i64 || x < -9223372036854775800i64;
}

static const i64 probes[] = {
    0i64, 1i64, 2i64, 3i64, 4i64, 5i64, 6i64, 7i64, 8i64, 9i64, 10i64,
    11i64, 14i64, 15i64, 16i64, 19i64, 20i64, 21i64, -1i64, -4i64, -5i64,
    -6i64, 1048575i64, 1048576i64, 1048577i64, -1048575i64, -1048576i64,
    -1048577i64, 32767i64, -32768i64, 65535i64, 2147483647i64,
    -2147483648i64, 4294967280i64, 4294967281i64, 4294967295i64,
    9223372036854775799i64, 9223372036854775800i64, 9223372036854775801i64,
    9223372036854775807i64, -9223372036854775807i64 - 1i64,
    -9223372036854775801i64, -9223372036854775800i64,
    -9223372036854775799i64,
};

global u32 test_entry() {
    u32 failures = 0u32;
    for (uptr k = 0; k < sizeof(probes) / sizeof(probes[0]); ++k) {
        const i64 wide = probes[k];
        const i32 x = (i32)wide;
        if (outside(x) != either(x < -1048576, x > 1048576)) ++failures;
        if (inside(x) != both(x >= -5, x <= 7)) ++failures;
        if (mirrored(x) != both(10 < x, 20 > x)) ++failures;
        if (gap(x) != either(x == 1, x == 3)) ++failures;
        const u8 small = (u8)wide;
        if (pair(small) != either(small == 3u8, small == 4u8)) ++failures;
        const u32 word = (u32)wide;
        if (neither(word) != both(word != 0u32, word != 1u32)) ++failures;
        if (ends(word) != either(word < 10u32, word > 4294967280u32)) {
            ++failures;
        }
        const u16 half = (u16)wide;
        if (three(half) !=
            either(either(half == 1u16, half == 2u16), half == 3u16)) {
            ++failures;
        }
        if (shifted(word) != either(word - 10u32 < 5u32, word == 15u32)) {
            ++failures;
        }
        if (!everything((i16)wide)) ++failures;
        if (nothing(wide)) ++failures;
        const u64 unsigned_wide = (u64)wide;
        if (around_sign(unsigned_wide) !=
            both(unsigned_wide >= 9223372036854775800u64,
                 unsigned_wide <= 9223372036854775815u64)) {
            ++failures;
        }
        if (extremes(wide) != either(wide > 9223372036854775800i64,
                                     wide < -9223372036854775800i64)) {
            ++failures;
        }
    }
    return failures;
}
