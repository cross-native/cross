// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// u64 and i64 values live across calls to narrow_callee, which the startup
// implements as a MIPS I callee does: it saves and restores s0-s7 with sw and
// lw, so only their low 32 bits survive the call.

global u32 narrow_callee(u32 value);

global u32 wide_across(u32 seed) {
    u64 a = (u64)seed * 0x100000001u64 + 0x0123456700000000u64;
    i64 b = (i64)seed * -0x10000001i64;
    u64 c = a ^ 0xfedcba9876543210u64;
    u32 first = narrow_callee(seed);
    a += first;
    u32 second = narrow_callee(first);
    b -= (i64)second;
    c ^= (u64)second << 32;
    u64 mixed = a ^ (u64)b ^ c;
    return (u32)(mixed ^ (mixed >> 32));
}

// One u64 lives across one call.
global u64 wide_one(u64 value) {
    u64 kept = value * 0x100000001u64;
    u32 first = narrow_callee((u32)value);
    return kept + first;
}
