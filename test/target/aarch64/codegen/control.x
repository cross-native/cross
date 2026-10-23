// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Control flow: conditional branches on every comparison, loops whose
// values rotate through PHIs on critical edges, chains of tests, computed
// goto through label values, and an exported label. test_main returns 0,
// or the number of the failed check.

volatile u64 zero = 0u64;

[[noinline]] static u32 classify(in i64 value) {
    if (value < -1000i64) return 1u32;
    if (value <= -1i64) return 2u32;
    if (value == 0i64) return 3u32;
    if (value < 4096i64) return 4u32;
    if (value >= 0x100000000i64) return 6u32;
    return 5u32;
}

[[noinline]] static u32 unsigned_bits(in u32 a, in u32 b) {
    u32 bits = 0u32;
    if (a < b) bits = bits | 1u32;
    if (a <= b) bits = bits | 2u32;
    if (a > b) bits = bits | 4u32;
    if (a >= b) bits = bits | 8u32;
    if (a == b) bits = bits | 16u32;
    if (a != b) bits = bits | 32u32;
    return bits;
}

[[noinline]] static u32 signed_narrow_bits(in i8 a, in i16 b) {
    u32 bits = 0u32;
    if (a < -3i8) bits = bits | 1u32;
    if (b > 300i16) bits = bits | 2u32;
    if (a >= 0i8) bits = bits | 4u32;
    if (b <= -300i16) bits = bits | 8u32;
    return bits;
}

// Three values rotate each iteration: the back edge's copies form a cycle.
[[noinline]] static u32 rotate(in u32 a, in u32 b, in u32 c, in u32 n) {
    u32 x = a;
    u32 y = b;
    u32 z = c;
    for (u32 i = 0u32; i < n; i = i + 1u32) {
        u32 t = x;
        x = y;
        y = z;
        z = t;
    }
    return x * 100u32 + y * 10u32 + z;
}

// Two values swap on the back edge of a loop that may run zero times.
[[noinline]] static u64 swap_loop(in u64 a, in u64 b, in u32 n) {
    u64 x = a;
    u64 y = b;
    u32 i = 0u32;
    while (i < n) {
        u64 t = x + 1u64;
        x = y;
        y = t;
        i = i + 1u32;
    }
    return (x << 32) | y;
}

[[noinline]] static u32 dispatch(in u32 selector) {
    label targets[4] = {dispatch::first, dispatch::second, dispatch::third, dispatch::fourth};
    goto targets[selector & 3u32];
first:
    return 11u32;
second:
    return 22u32;
third:
    return 33u32;
fourth:
    return 44u32;
}

// Only labels whose address is taken are computed-goto targets, so `join`,
// a direct goto's target, merges no value from the computed goto.
[[noinline]] static u32 narrowed(in u32 selector, in u32 seed) {
    u32 kept = seed * 3u32;
    label to = selector == 0u32 ? narrowed::first : narrowed::second;
    goto to;
join:
    kept = kept + 100u32;
    return kept;
first:
    kept = kept + 1u32;
    goto join;
second:
    return kept + 2u32;
}

[[noinline]] static u32 collatz(in u32 start) {
    u32 steps = 0u32;
    u32 n = start;
    while (n != 1u32) {
        if ((n & 1u32) == 0u32) n = n >> 1;
        else n = n * 3u32 + 1u32;
        steps = steps + 1u32;
    }
    return steps;
}

[[noinline]] global u32 exported_entry(in u32 x) {
    u32 y = x + 1u32;
global label control_exported:
    return y * 2u32;
}

[[noinline]] static bool both(in bool p, in bool q) { return p && q; }

global u32 test_main() {
    const u64 z = zero;
    const i64 sz = z;
    if (classify(-5000i64 + sz) != 1u32 || classify(-1i64 + sz) != 2u32 ||
        classify(sz) != 3u32 || classify(4095i64 + sz) != 4u32 ||
        classify(4096i64 + sz) != 5u32 || classify(0x100000000i64 + sz) != 6u32) {
        return 1u32;
    }
    if (unsigned_bits(1u32 + (u32)z, 2u32) != 35u32 ||
        unsigned_bits(0xffffffffu32 + (u32)z, 1u32) != 44u32 ||
        unsigned_bits(7u32 + (u32)z, 7u32) != 26u32) {
        return 2u32;
    }
    if (signed_narrow_bits(-4i8 + (i8)z, 301i16) != 3u32 ||
        signed_narrow_bits(5i8 + (i8)z, -300i16) != 12u32) {
        return 3u32;
    }
    if (rotate(1u32, 2u32, 3u32, 5u32 + (u32)z) != 312u32) return 4u32;
    if (rotate(1u32, 2u32, 3u32, (u32)z) != 123u32) return 5u32;
    if (swap_loop(10u64, 20u64, 3u32 + (u32)z) != (21u64 << 32 | 12u64)) return 6u32;
    if (swap_loop(10u64, 20u64, (u32)z) != (10u64 << 32 | 20u64)) return 7u32;
    if (dispatch(0u32 + (u32)z) != 11u32 || dispatch(1u32 + (u32)z) != 22u32 ||
        dispatch(2u32 + (u32)z) != 33u32 || dispatch(7u32 + (u32)z) != 44u32) {
        return 8u32;
    }
    if (collatz(27u32 + (u32)z) != 111u32) return 9u32;
    if (exported_entry(20u32 + (u32)z) != 42u32) return 10u32;
    if ((uptr)exported_entry::control_exported == 0uptr) return 11u32;
    bool p = z == 0u64;
    bool q = z != 0u64;
    if (!both(p, p) || both(p, q)) return 12u32;
    if (narrowed(0u32 + (u32)z, 5u32) != 116u32 ||
        narrowed(1u32 + (u32)z, 5u32) != 17u32) return 13u32;
    return 0u32;
}
