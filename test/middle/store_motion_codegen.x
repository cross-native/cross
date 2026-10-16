// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loops whose stores to fixed locations leave them for the loop exits, and
// loops whose read of the location, call, store through a pointer that may
// alias, or volatile access keeps their stores inside.

global u32 total;
global u64 wide_total;
global f64 mean;
global u32 remaining;
global u32 accumulated;
global u32 counted;
global u32 seen;
global u32 aliased;
global volatile u32 device;
global u32 near_device;

[[noinline]] global u32 sum_stored(in u32 n) {
    u32 sum = 0u32;
    for (u32 k = 0u32; k < n; ++k) {
        sum = sum + k * k;
        total = sum;
    }
    return sum;
}

[[noinline]] global void wide_stored(in u32 n) {
    u64 state = 7u64;
    f64 x = 0.0f64;
    for (u32 k = 0u32; k < n; ++k) {
        state = state * 3u64 + (u64)k;
        x = x + 0.5f64;
        wide_total = state;
        mean = x;
    }
}

// A bottom-tested loop leaves after its store without rotation.
[[noinline]] global void countdown(in u32 n) {
    u32 k = n;
    do {
        k = k - 1u32;
        remaining = k * 5u32;
    } while (k > 1u32);
}

// Each pass reads what the previous one stored, and nothing tells the
// first pass the value, so the read and the store stay.
[[noinline]] global void load_kept(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) accumulated = accumulated + k;
}

[[noinline]] global void note() { seen = seen + counted; }

[[noinline]] global void call_kept(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        counted = k;
        note();
    }
}

[[noinline]] global void alias_kept(in u32 *p, in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        aliased = k;
        *p = k + 1u32;
    }
}

[[noinline]] global void volatile_kept(in u32 n) {
    for (u32 k = 0u32; k < n; ++k) {
        near_device = k;
        device = k;
    }
}
