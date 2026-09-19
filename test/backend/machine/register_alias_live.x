// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]]
static u32 choose_shift(in u64 min, in u64 max, in u32 ceiling) {
    if (min == max) return 0u32;
    return ceiling;
}

[[noinline]]
global u64 register_alias_probe(
    in u64 *a, in uptr lo, in uptr hi, in u64 min,
    in u64 max, in u32 ceiling) {
    uptr count[256];
    uptr i;
    uptr d;
    u32 shift;
    u64 sum;

    shift = choose_shift(min, max, ceiling);

    i = 0uptr;
    while (i < 256uptr) {
        count[i] = 0uptr;
        i++;
    }

    i = lo;
    while (i < hi) {
        d = (uptr)((a[i] >> shift) & 255u64);
        count[d]++;
        i++;
    }

    sum = 0u64;
    i = 0uptr;
    while (i < 256uptr) {
        sum += (u64)count[i] * (u64)(i + 1uptr);
        i++;
    }

    return sum;
}

global u64 register_alias_values[4] = {
    0x0100000000000001u64,
    0x0200000000000002u64,
    0x0300000000000003u64,
    0x0400000000000004u64,
};

global i32 register_alias_entry() {
    return register_alias_probe(register_alias_values, 0uptr, 4uptr,
                                register_alias_values[0],
                                register_alias_values[3], 56u32) == 14u64;
}
