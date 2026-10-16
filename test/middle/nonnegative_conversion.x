// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An unsigned integer whose top bit is known to be clear converts to
// floating as a signed one; one whose top bit may be set keeps the unsigned
// conversion. Rounding is the same either way. test_entry returns zero when
// every result matches. It runs with -fno-eval-calls, so that the calls run;
// TEST_U64 adds 64-bit sources.

[[noinline]] global f32 shifted(in u32 x) { return (f32)(x >> 1); }
[[noinline]] global f64 masked(in u32 x) { return (f64)(x & 2147483647u32); }
[[noinline]] global f32 remainder(in u32 x) { return (f32)(x % 1000u32); }
[[noinline]] global f32 quotient(in u32 x) { return (f32)(x / 3u32); }
[[noinline]] global f32 widened(in u16 x) { return (f32)(u32)x; }
[[noinline]] global f32 narrow(in u16 x) { return (f32)x; }
[[noinline]] global f32 full(in u32 x) { return (f32)x; }
[[noinline]] global f32 selected(in u32 x, in bool c) {
    return (f32)(c ? x >> 4 : x & 255u32);
}
#if defined(TEST_U64)
[[noinline]] global f64 wide(in u64 x) { return (f64)(x >> 11); }
[[noinline]] global f32 wide_full(in u64 x) { return (f32)x; }
#endif

global u32 test_entry() {
    if ($::runtime(shifted(4294967295u32)) != 2147483648.0f32) return 1u32;
    if ($::runtime(shifted(2147483649u32)) != 1073741824.0f32) return 2u32;
    if ($::runtime(masked(4294967295u32)) != 2147483647.0f64) return 3u32;
    if ($::runtime(remainder(4294967295u32)) != 295.0f32) return 4u32;
    if ($::runtime(quotient(4294967295u32)) != 1431655808.0f32) return 5u32;
    if ($::runtime(widened(65535u16)) != 65535.0f32) return 6u32;
    if ($::runtime(narrow(65535u16)) != 65535.0f32) return 7u32;
    if ($::runtime(full(4294967295u32)) != 4294967296.0f32 ||
        $::runtime(full(2147483648u32)) != 2147483648.0f32 ||
        $::runtime(full(16777217u32)) != 16777216.0f32) return 8u32;
    if ($::runtime(selected(4294967295u32, (bool)1u32)) != 268435456.0f32 ||
        $::runtime(selected(4294967295u32, (bool)0u32)) != 255.0f32) return 9u32;
#if defined(TEST_U64)
    if ($::runtime(wide(18446744073709551615u64)) != 9007199254740991.0f64)
        return 10u32;
    if ($::runtime(wide_full(18446744073709551615u64)) !=
        18446744073709551616.0f32) return 11u32;
#endif
    return 0u32;
}
