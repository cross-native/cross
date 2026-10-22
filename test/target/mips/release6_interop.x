// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Floating arguments and results between Cross and GCC (release6_interop.c)
// on Release 6, whose FPU has only 64-bit registers: compiled with -mabi=o32
// or -mabi=n64, the C ABI passes them in FPRs and on the stack. test_entry
// returns zero when every call in both directions gives its result.

global f64 c_scale(in f64 x, in f32 y, in f64 z, in i32 n, in f64 w);
global f32 c_mix(in f32 a, in f64 b, in f32 c, in i32 d, in f32 e);
global u32 c_calls_cross();

[[noinline, runtime_only]]
global f64 cross_scale(in f64 x, in f32 y, in f64 z, in i32 n, in f64 w) {
    return x * (f64) y + z * (f64) n - w;
}

[[noinline, runtime_only]]
global f32 cross_mix(in f32 a, in f64 b, in f32 c, in i32 d, in f32 e) {
    return a - (f32) b * c + (f32) d / e;
}

[[noinline, runtime_only]]
global u32 cross_select(in f64 a, in f64 b, in u32 yes, in u32 no) {
    return a < b ? yes : no;
}

global u32 test_entry() {
    u32 wrong = c_calls_cross();
    if (c_scale(1.5, 2.0f32, -0.25, 8i32, 3.0) != 1.5 * 2.0 - 0.25 * 8.0 - 3.0) {
        wrong = wrong + 10u32;
    }
    if (c_mix(10.0f32, 0.5, 3.0f32, 9i32, 2.0f32) != 10.0f32 - 0.5f32 * 3.0f32 + 4.5f32) {
        wrong = wrong + 20u32;
    }
    return wrong;
}
