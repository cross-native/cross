// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Selects between a decrement of x and another value on whether x is zero,
// which select by the decrement's borrow. test_entry returns zero when every
// result matches the selection made behind a call; it runs with
// -fno-eval-calls, so that the calls run.

[[noinline]] global u32 pick(in bool condition, in u32 truth, in u32 falsity) {
    return condition ? truth : falsity;
}
[[noinline]] global u64 pick64(in bool condition, in u64 truth, in u64 falsity) {
    return condition ? truth : falsity;
}

[[noinline]] global u32 wrap(in u32 x) { return x == 0u32 ? 54u32 : x - 1u32; }
[[noinline]] global u32 wrap_inverse(in u32 x) {
    return x != 0u32 ? x - 1u32 : 7u32;
}
[[noinline]] global u32 decrement_on_zero(in u32 x, in u32 y) {
    return x == 0u32 ? x - 1u32 : y;
}
[[noinline]] global u32 kept_unless_zero(in u32 x, in u32 y) {
    return x != 0u32 ? y : x - 1u32;
}
[[noinline]] global u64 wide(in u64 x, in u64 y) { return x == 0u64 ? y : x - 1u64; }
[[noinline]] global i32 signed_wrap(in i32 x) { return x == 0 ? -9 : x + -1; }

// A ring of counters stepping down, as a subtractive generator's indices.
[[noinline]] global u32 ring(in uptr steps) {
    u32 a = 0u32;
    u32 b = 31u32;
    u32 sum = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        a = a == 0u32 ? 54u32 : a - 1u32;
        b = b == 0u32 ? 54u32 : b - 1u32;
        sum = sum * 31u32 + (a ^ (b << 8));
    }
    return sum;
}

[[noinline]] global u32 ring_reference(in uptr steps) {
    u32 a = 0u32;
    u32 b = 31u32;
    u32 sum = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        a = pick(a == 0u32, 54u32, a - 1u32);
        b = pick(b == 0u32, 54u32, b - 1u32);
        sum = sum * 31u32 + (a ^ (b << 8));
    }
    return sum;
}

static const u64 probes[] = {
    0u64, 1u64, 2u64, 54u64, 4294967295u64, 4294967296u64,
    9223372036854775808u64, 18446744073709551615u64, 123456789012345u64,
};

global u32 test_entry() {
    for (uptr k = 0; k < sizeof(probes) / sizeof(probes[0]); ++k) {
        const u64 wide_x = probes[k];
        const u32 x = (u32)wide_x;
        if (wrap(x) != pick(x == 0u32, 54u32, x - 1u32)) return 1u32;
        if (wrap_inverse(x) != pick(x != 0u32, x - 1u32, 7u32)) return 2u32;
        if (decrement_on_zero(x, 99u32) != pick(x == 0u32, x - 1u32, 99u32)) {
            return 3u32;
        }
        if (kept_unless_zero(x, 99u32) != pick(x != 0u32, 99u32, x - 1u32)) {
            return 4u32;
        }
        if (wide(wide_x, 5u64) != pick64(wide_x == 0u64, 5u64, wide_x - 1u64)) {
            return 5u32;
        }
        const i32 signed_x = (i32)x;
        if (signed_wrap(signed_x) !=
            (i32)pick(signed_x == 0, (u32)-9, (u32)(signed_x + -1))) {
            return 6u32;
        }
    }
    if (ring(1000) != ring_reference(1000)) return 7u32;
    return 0u32;
}
