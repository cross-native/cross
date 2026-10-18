// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Code that the MIPS errata workarounds change: a multiply whose latency
// independent variable shifts could fill, a doubleword divide followed by
// a variable shift, and LL/SC loops.

global u32 [[atomic]] errata_counter;

[[noinline]]
global u32 errata_product(in u32 a, in u32 b, in u32 c, in u32 d) {
    return a * b + (c << d) + (c >> (d & 7u32));
}

[[noinline]]
global u64 errata_quotient(in u64 a, in u64 b, in u32 c) {
    return (a / b) << c;
}

[[noinline]]
global u32 errata_bump(in u32 value) {
    return $::atomic_fetch_add(&errata_counter, value, $::memory::seq_cst);
}

[[noinline]]
global bool errata_swap(in u32 expected, in u32 desired) {
    u32 seen = expected;
    return $::atomic_compare_exchange(&errata_counter, &seen, desired,
                                      $::memory::seq_cst, $::memory::seq_cst);
}
