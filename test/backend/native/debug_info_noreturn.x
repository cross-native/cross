// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// spin never returns, so it skips the saves of the preserved registers it
// overwrites; its call-frame information marks their incoming values lost.
[[noinline]] global i32 step(i32 v) {
    return v + 1;
}

#if defined(O32)
// Calls change gp and $f20 to $f31, which o32 preserves.
global volatile i32 last;
[[abi("o32")]] global void spin(i32 v) {
    i32 total = v;
    for (;;) {
        last = total;
        total = step(total) + v;
    }
}
#elif defined(LARGE)
// A large local keeps the frame out of the frame program.
global void spin(i32 v) {
    volatile u8 buffer[8192];
    i32 total = v;
    for (;;) {
        buffer[total & 8191] = (u8)total;
        total = step(total) + v;
    }
}
#elif defined(MS_ABI)
// Calls change RSI, RDI, and XMM6 to XMM15, which ms_abi preserves.
[[abi("ms_abi")]] global void spin(i32 v, f64 scale) {
    i32 total = v;
    f64 sum = scale;
    for (;;) {
        total = step(total) + v;
        sum = sum * scale + (f64)total;
        if (sum > 1000.0) sum = scale;
    }
}
#else
[[noinline]] global void spin(i32 v) {
    i32 total = v;
    for (;;) {
        total = step(total) + v;
        if (total == 0) {
            total = step(v);
        }
    }
}

// A runtime argument keeps cc from evaluating the endless call.
global volatile i32 seed = 3;

global i32 spin_entry() {
    spin(seed);
    return 0;
}
#endif
