// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 early_unsigned_values[9];
global i64 early_signed_values[9];
global u32 early_u32_values[10];

[[noinline, link_name("early_exit_less")]]
static uptr early_exit_less(in const u64 *values, in uptr count,
                            in u64 limit) {
    uptr index = 0;
    while (index < count && values[index] < limit) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_not_equal")]]
static uptr early_exit_not_equal(in const u64 *values, in uptr count,
                                 in u64 sought) {
    uptr index = 0;
    while (index < count && values[index] != sought) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_reversed")]]
static uptr early_exit_reversed(in const u64 *values, in uptr count,
                                in u64 limit) {
    uptr index = 0;
    while (index < count && limit > values[index]) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_at_most")]]
static uptr early_exit_at_most(in const u64 *values, in uptr count,
                               in u64 limit) {
    uptr index = 0;
    while (index < count && values[index] <= limit) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_above")]]
static uptr early_exit_above(in const u64 *values, in uptr count,
                             in u64 floor) {
    uptr index = 0;
    while (index < count && values[index] > floor) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_u32")]]
static uptr early_exit_u32(in const u32 *values, in uptr count,
                           in u32 limit) {
    uptr index = 0;
    while (index < count && values[index] <= limit) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_signed")]]
static uptr early_exit_signed(in const i64 *values, in uptr count,
                              in i64 floor) {
    uptr index = 0;
    while (index < count && values[index] >= floor) {
        index = index + 1;
    }
    return index;
}

[[noinline, link_name("early_exit_volatile")]]
static uptr early_exit_volatile(in volatile u64 *values, in uptr count,
                                in u64 sought) {
    uptr index = 0;
    while (index < count && values[index] != sought) {
        index = index + 1;
    }
    return index;
}

global i32 early_exit_entry() {
    early_unsigned_values[0] = 1;
    early_unsigned_values[1] = 2;
    early_unsigned_values[2] = 3;
    early_unsigned_values[3] = 4;
    early_unsigned_values[4] = 9;
    early_unsigned_values[5] = 12;
    early_unsigned_values[6] = 17;
    early_unsigned_values[7] = 21;
    early_unsigned_values[8] = 30;

    early_signed_values[0] = 7;
    early_signed_values[1] = 5;
    early_signed_values[2] = 3;
    early_signed_values[3] = -1;
    early_signed_values[4] = -4;
    early_signed_values[5] = -8;
    early_signed_values[6] = -9;
    early_signed_values[7] = -12;
    early_signed_values[8] = -15;

    early_u32_values[0] = 1;
    early_u32_values[1] = 2;
    early_u32_values[2] = 3;
    early_u32_values[3] = 4;
    early_u32_values[4] = 5;
    early_u32_values[5] = 6;
    early_u32_values[6] = 7;
    early_u32_values[7] = 8;
    early_u32_values[8] = 9;
    early_u32_values[9] = 10;

    return (early_exit_less(early_unsigned_values, 0, 5) == 0) +
           (early_exit_less(early_unsigned_values, 3, 5) == 3) +
           (early_exit_less(early_unsigned_values, 9, 5) == 4) +
           (early_exit_less(early_unsigned_values, 9, 100) == 9) +
           (early_exit_not_equal(early_unsigned_values, 9, 1) == 0) +
           (early_exit_not_equal(early_unsigned_values, 9, 12) == 5) +
           (early_exit_not_equal(early_unsigned_values, 9, 99) == 9) +
           (early_exit_reversed(early_unsigned_values, 9, 5) == 4) +
           (early_exit_reversed(early_unsigned_values, 3, 5) == 3) +
           (early_exit_at_most(early_unsigned_values, 9, 4) == 4) +
           (early_exit_above(early_unsigned_values, 9, 2) == 0) +
           (early_exit_u32(early_u32_values, 10, 6) == 6) +
           (early_exit_signed(early_signed_values, 9, 0) == 3) +
           (early_exit_signed(early_signed_values, 2, 0) == 2) +
           (early_exit_signed(early_signed_values, 9, -20) == 9) +
           (early_exit_volatile(early_unsigned_values, 9, 17) == 6);
}
