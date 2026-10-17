// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loop-carried values that stay live across a Cross ABI call are kept in
// frame homes. A phi destination may share its home with another phi's
// source on the same edge, so edge copies must be ordered by home.

[[abi("x"), noinline]]
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

// Swapped pairs form copy cycles. Under pressure a cycle runs through 8-,
// 16-, or 32-bit homes, and each home is written at its own width.
[[noinline]]
global T phi_frame_swaps<T>(in T seed, in u32 rounds) {
    T a0 = seed; T b0 = seed ^ (T)1; T a1 = seed + (T)2; T b1 = seed ^ (T)3;
    T a2 = seed + (T)4; T b2 = seed ^ (T)5; T a3 = seed + (T)6;
    T b3 = seed ^ (T)7; T a4 = seed + (T)8; T b4 = seed ^ (T)9;
    T a5 = seed + (T)10; T b5 = seed ^ (T)11; T a6 = seed + (T)12;
    T b6 = seed ^ (T)13; T a7 = seed + (T)14; T b7 = seed ^ (T)15;
    T a8 = seed + (T)16; T b8 = seed ^ (T)17; T a9 = seed + (T)18;
    T b9 = seed ^ (T)19; T acc = seed;
    for (u32 r = 0u32; r < rounds; ++r) {
        acc = acc * (T)3 + (T)r;
        T t0 = a0 + acc; a0 = b0; b0 = t0; T t1 = a1 + acc; a1 = b1; b1 = t1;
        T t2 = a2 + acc; a2 = b2; b2 = t2; T t3 = a3 + acc; a3 = b3; b3 = t3;
        T t4 = a4 + acc; a4 = b4; b4 = t4; T t5 = a5 + acc; a5 = b5; b5 = t5;
        T t6 = a6 + acc; a6 = b6; b6 = t6; T t7 = a7 + acc; a7 = b7; b7 = t7;
        T t8 = a8 + acc; a8 = b8; b8 = t8; T t9 = a9 + acc; a9 = b9; b9 = t9;
    }
    return acc ^ a0 ^ (b0 << 1) ^ a1 ^ (b1 << 1) ^ a2 ^ (b2 << 1) ^ a3 ^
           (b3 << 1) ^ a4 ^ (b4 << 1) ^ a5 ^ (b5 << 1) ^ a6 ^ (b6 << 1) ^
           a7 ^ (b7 << 1) ^ a8 ^ (b8 << 1) ^ a9 ^ (b9 << 1);
}

global i32 phi_frame_entry() {
    if (phi_frame_six(phi_frame_iterations) != 0x0c23dd12c4c9ffffu64) return 1;
    if (phi_frame_nine(phi_frame_iterations) != 0x2b27f28cb1b1ff18u64) return 2;
    const u32 rounds = (u32)phi_frame_iterations + 1u32;
    if (phi_frame_swaps((u8)0x5a, rounds) != (u8)0x28) return 3;
    if (phi_frame_swaps((u16)0x1234, rounds) != (u16)0xc0b2) return 4;
    if (phi_frame_swaps(0x12345678u32, rounds) != 0xe3fdaa96u32) return 5;
    return 61;
}
