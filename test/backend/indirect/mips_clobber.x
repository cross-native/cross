// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The startup implements this function in assembly; it overwrites s0 and s1,
// which o32 otherwise preserves.
[[clobber("s0", "s1")]]
global u32 clobber_s01(in u32 value);

typedef u32 (*clobber_fn)(in u32 value) [[clobber("s0", "s1")]];

clobber_fn saved = clobber_s01;

// Enough values stay live across the call to occupy every preserved register.
[[noinline]] u32 use_clobber(in clobber_fn callback, in u32 x) {
    u32 a = x + 1u32;
    u32 b = x * 3u32;
    u32 c = x ^ 5u32;
    u32 d = x + 7u32;
    u32 e = x * 11u32;
    u32 f = x ^ 13u32;
    u32 g = x + 17u32;
    u32 h = x * 19u32;
    u32 r = callback(x);
    r += callback(r);
    return r + a + b + c + d + e + f + g + h + x;
}

global u32 mips_clobber_entry(in u32 x) {
    return use_clobber(saved, x);
}
