// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loop-carried values that stay live across a Cross ABI call are kept in
// frame homes. A phi destination may share its home with another phi's
// source on the same edge, so edge copies must be ordered by home.

[[abi("cross"), noinline]]
global u64 phi_frame_mix(u64 x) {
    return (x ^ (x >> 29)) * 0xbf58476d1ce4e5b9u64;
}

global u64 phi_frame_iterations = 1000;

[[noinline]]
global u64 phi_frame_six(u64 n) {
    u64 a = 1; u64 b = 2; u64 c = 3; u64 d = 4; u64 e = 5; u64 f = 6;
    for (u64 i = 0; i < n; ++i) {
        u64 t = phi_frame_mix(a ^ i);
        a += t; b ^= a >> 3; c += b * 3; d ^= c >> 5; e += d * 7;
        f ^= e >> 11;
    }
    return a ^ b ^ c ^ d ^ e ^ f;
}

[[noinline]]
global u64 phi_frame_nine(u64 n) {
    u64 a = 1; u64 b = 2; u64 c = 3; u64 d = 4; u64 e = 5; u64 f = 6;
    u64 g = 7; u64 h = 8; u64 k = 9;
    for (u64 i = 0; i < n; ++i) {
        u64 t = phi_frame_mix(a ^ i);
        a += t; b ^= a >> 3; c += b * 3; d ^= c >> 5; e += d * 7;
        f ^= e >> 11; g += f * 13; h ^= g >> 17; k += h * 19;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ k;
}

global i32 phi_frame_entry() {
    if (phi_frame_six(phi_frame_iterations) != 0x0c23dd12c4c9ffffu64) return 1;
    if (phi_frame_nine(phi_frame_iterations) != 0x2b27f28cb1b1ff18u64) return 2;
    return 61;
}
