// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Reduction loops whose bound is computed inside the loop: an expression of
// constants that only the header computes, a load, and a recurrence. The
// vectorizer must recompute an invariant bound before the loop and keep the
// scalar loop for any other.

global u32 table[16] = {1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32,
                        9u32, 10u32, 11u32, 12u32, 13u32, 14u32, 15u32, 16u32};
global uptr loaded_bound = 9uptr;
global volatile uptr divisor = 2uptr;

[[noinline]] global u32 constant_quotient() {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < 12uptr / 4uptr; ++i) total += table[i];
    return total;
}

[[noinline]] global u32 constant_remainder() {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < 69uptr % 16uptr; ++i) total += table[i];
    return total;
}

[[noinline]] global u32 constant_dividend(in uptr d) {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < 22uptr / d; ++i) total += table[i];
    return total;
}

[[noinline]] global u32 global_bound() {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < loaded_bound; ++i) total += table[i];
    return total;
}

[[noinline]] global u32 pointer_bound(in const uptr *bound) {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < *bound; ++i) total += table[i];
    return total;
}

[[noinline]] global u32 shrinking_bound(in uptr limit) {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < limit; ++i) {
        total += table[i];
        limit += ~0uptr;
    }
    return total;
}

[[noinline]] global u32 growing_bound(in uptr start) {
    u32 total = 0u32;
    uptr limit = start;
    for (uptr i = 0uptr; i < limit; ++i) {
        total += table[i];
        if (limit < 12uptr) limit += 1uptr;
    }
    return total;
}

[[noinline]] global u32 growing_from_zero() {
    u32 total = 0u32;
    uptr limit = 0uptr;
    for (uptr i = 0uptr; i < limit; ++i) {
        total += table[i];
        limit += 1uptr;
    }
    return total;
}

global u32 test_entry() {
    if (constant_quotient() != 6u32) return 1u32;
    if (constant_remainder() != 15u32) return 2u32;
    if (constant_dividend(divisor) != 66u32) return 3u32;
    if (global_bound() != 45u32) return 4u32;
    uptr bound = 13uptr;
    if (pointer_bound(&bound) != 91u32) return 5u32;
    if (shrinking_bound(6uptr) != 6u32) return 6u32;
    if (growing_bound(4uptr) != 78u32) return 7u32;
    if (growing_from_zero() != 0u32) return 8u32;
    return 0u32;
}
