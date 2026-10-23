// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loop phis that exchange and rotate 64-bit values, which MIPS I and II keep
// in word pairs: the copies on the back edge read every value before they
// write one. test_entry returns zero when each checksum matches the one an
// independent C version of the functions computes, or the number of the
// first that differs. It runs with -fno-eval-calls, so that the calls run.

[[noinline]] global u32 opaque(in u32 value) { return value; }

static u64 mix(in u64 sum, in u64 value) { return sum * 31u64 + value; }

// Two exchanges on one edge.
[[noinline]] global u64 exchanged(in u32 n, in u64 x, in u64 y, in u64 z,
                                  in u64 w) {
    u64 a = x;
    u64 b = y;
    u64 c = z;
    u64 d = w;
    for (u32 k = 0u32; k < n; ++k) {
        u64 t = a;
        a = b;
        b = t;
        t = c;
        c = d;
        d = t;
    }
    return mix(mix(mix(mix(0u64, a), b), c), d);
}

// A rotation of three, and a copy of one of them.
[[noinline]] global u64 rotated(in u32 n, in u64 x, in u64 y, in u64 z) {
    u64 a = x;
    u64 b = y;
    u64 c = z;
    u64 last = 0u64;
    for (u32 k = 0u32; k < n; ++k) {
        const u64 t = a;
        last = a;
        a = b;
        b = c;
        c = t;
    }
    return mix(mix(mix(mix(0u64, a), b), c), last);
}

global u32 test_entry() {
    const u64 x = 0x0123456789abcdefu64;
    const u64 y = 0xfedcba9876543210u64;
    const u64 z = 0x00000001ffffffffu64;
    const u64 w = 0x8000000000000001u64;
    u64 sum = 0u64;
    for (u32 n = 0u32; n < opaque(7u32); ++n) {
        sum = mix(sum, $::runtime(exchanged(n, x, y, z, w)));
    }
    if (sum != 0x78b9969af3aa23f5u64) return 1u32;
    sum = 0u64;
    for (u32 n = 0u32; n < opaque(7u32); ++n) {
        sum = mix(sum, $::runtime(rotated(n, x, y, z)));
    }
    if (sum != 0xd602cfeb0b2db022u64) return 2u32;
    return 0u32;
}
