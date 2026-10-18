// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// 128-bit transport and inline sequences that int128.cmake checks in the
// assembly.

global u128 add(in u128 a, in u128 b) {
    return a + b;
}

global u64 product_high(in u64 a, in u64 b) {
    return (u64)(((u128)a * (u128)b) >> 64u32);
}

// Returns the words 1 to 4 from the most significant one.
global u128 words() {
    return 0x00000001000000020000000300000004u128;
}

// The 128-bit argument starts at an even register under n64.
global u64 low_half(in u32 a, in u128 b) {
    return (u64)b + (u64)a;
}
