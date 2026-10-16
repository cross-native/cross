// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// RAX and RCX as ordinary colors: loops with more live values than the other
// registers, global addresses formed in their own registers, values in homes
// staged through RAX/RCX, fixed RAX/RDX division and CL shift operands, and
// phi copies between homes. test_entry returns zero when every checksum
// matches the value an -O0 build, which allocates no registers, computes.

static i32 px[32];
static i32 py[32];
static i32 vx[32];
static i32 vy[32];

static i32 fixed_mul(in i32 a, in i32 b) { return (i32)(((i64)a * (i64)b) >> 16); }

[[noinline]] global u64 physics(in u64 state, in uptr iterations) {
    u32 seed = (u32)(state >> 32) ^ (u32)state;
    for (u32 k = 0u32; k < 32u32; ++k) {
        seed = seed * 1664525u32 + 1013904223u32;
        px[k] = (i32)(seed & 1048575u32) - 524288;
        py[k] = (i32)((seed >> 12) & 1048575u32);
        vx[k] = (i32)((seed >> 7) & 262143u32) - 131072;
        vy[k] = 0;
    }
    u32 hits = 0u32;
    for (uptr n = 0; n < iterations; ++n) {
        const uptr k = n & 31;
        i32 y_speed = vy[k] - 6554;
        i32 y = py[k] + y_speed;
        if (y < 0) {
            y = 0 - y;
            y_speed = fixed_mul(0 - y_speed, 52429);
            hits = hits + 1u32;
        }
        i32 x_speed = fixed_mul(vx[k], 65208);
        i32 x = px[k] + x_speed;
        if (x < -1048576 || x > 1048576) {
            x_speed = 0 - x_speed;
            x = px[k];
        }
        px[k] = x;
        py[k] = y;
        vx[k] = x_speed;
        vy[k] = y_speed;
    }
    u32 sum = hits;
    for (u32 k = 0u32; k < 32u32; ++k) {
        sum = sum * 31u32 + (u32)px[k];
        sum = sum * 31u32 + (u32)py[k];
    }
    return ((u64)hits << 32) | (u64)sum;
}

[[noinline]] global u64 opaque(in u64 value) { return value; }

// A call result stays live while sixteen other values circulate, divisions
// use RAX and RDX, and variable shifts use CL.
[[noinline]] global u64 crowded(in u64 a, in u64 b, in u32 rounds) {
    u64 kept = opaque(a ^ 0x5555u64);
    u64 s0 = a; u64 s1 = b; u64 s2 = a + b; u64 s3 = a ^ b;
    u64 s4 = a * 3u64; u64 s5 = b * 5u64; u64 s6 = a + 7u64;
    u64 s7 = b + 11u64; u64 s8 = a >> 3; u64 s9 = b >> 5;
    u64 s10 = a | 9u64; u64 s11 = b & 0xffffu64; u64 s12 = a - b;
    u64 s13 = b - a; u64 s14 = a << 2; u64 s15 = b << 4;
    for (u32 r = 0u32; r < rounds; ++r) {
        const u64 divisor = (s3 & 1023u64) + 1u64;
        s0 = s0 + s1 / divisor;
        s1 = s1 ^ (s2 << (u32)(s4 & 31u64));
        s2 = s2 + s3 % divisor;
        s3 = s3 * 2654435761u64 + s5;
        s4 = s4 + (s6 >> (u32)(s7 & 15u64));
        s5 = s5 ^ s8;
        s6 = s6 + s9 + kept;
        s7 = s7 ^ s10;
        s8 = s8 + s11;
        s9 = s9 ^ s12;
        s10 = s10 + s13;
        s11 = s11 ^ s14;
        s12 = s12 + s15;
        s13 = s13 ^ s0;
        s14 = s14 + s1;
        s15 = s15 ^ s2;
        if ((s0 & 3u64) == 0u64) kept = kept ^ s3;
    }
    return s0 ^ s1 ^ s2 ^ s3 ^ s4 ^ s5 ^ s6 ^ s7 ^ s8 ^ s9 ^ s10 ^ s11 ^
           s12 ^ s13 ^ s14 ^ s15 ^ kept;
}

// Loop-carried values rotate through phis while pressure keeps some of
// them in homes, so phi copies run between homes, registers, and both.
[[noinline]] global u32 rotate_phis(in u32 seed, in u32 rounds) {
    u32 a = seed; u32 b = seed + 1u32; u32 c = seed + 2u32; u32 d = seed + 3u32;
    u32 e = seed + 4u32; u32 f = seed + 5u32; u32 g = seed + 6u32;
    u32 h = seed + 7u32; u32 i = seed + 8u32; u32 j = seed + 9u32;
    u32 k = seed + 10u32; u32 l = seed + 11u32; u32 m = seed + 12u32;
    u32 n = seed + 13u32; u32 o = seed + 14u32; u32 p = seed + 15u32;
    for (u32 r = 0u32; r < rounds; ++r) {
        const u32 t = a * 33u32 + p;
        a = b; b = c; c = d; d = e; e = f; f = g; g = h; h = i;
        i = j; j = k; k = l; l = m; m = n; n = o; o = p ^ r; p = t;
    }
    return a ^ (b << 1) ^ (c << 2) ^ (d << 3) ^ (e << 4) ^ (f << 5) ^
           (g << 6) ^ (h << 7) ^ (i << 8) ^ (j << 9) ^ (k << 10) ^
           (l << 11) ^ (m << 12) ^ (n << 13) ^ (o << 14) ^ (p << 15);
}

global u32 test_entry() {
    if (physics(0x123456789abcdefu64, 5000) != 0x42bbda9d069u64) return 1u32;
    if (crowded(0x0123456789abcdefu64, 0xfedcba9876543210u64, 200u32) !=
        0x584e15cb7a2670d1u64) return 2u32;
    if (rotate_phis(77u32, 1000u32) != 2412016175u32) return 3u32;
    return 0u32;
}
