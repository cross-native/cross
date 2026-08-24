// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("unroll_power")]]
global u64 unroll_power(in uptr count) {
    u64 result = 2;
    uptr index = 0;
    while (index < count) {
        result = result * 3;
        index = index + 1;
    }
    return result;
}

[[runtime_only, noinline, link_name("unroll_sequence")]]
global u64 unroll_sequence(in uptr count) {
    u64 result = 2;
    uptr index = 0;
    u64 step = 0;
    while (index < count) {
        result = result * 3 + step;
        index = index + 1;
        step = step + 1;
    }
    return result;
}

[[runtime_only, noinline, link_name("unroll_read")]]
global u64 unroll_read(in const u64 *data, in uptr count) {
    u64 result = 2;
    uptr index = 0;
    while (index < count) {
        result = result * 3 + data[index];
        index = index + 1;
    }
    return result;
}

[[runtime_only, noinline, link_name("unroll_add_read")]]
global u64 unroll_add_read(in const u64 *data, in uptr count, in u64 bias) {
    u64 result = 2;
    uptr index = 0;
    while (index < count) {
        result = result + data[index] + bias;
        index = index + 1;
    }
    return result;
}

[[runtime_only, noinline, link_name("unroll_masked_read")]]
global u64 unroll_masked_read(in const u64 *data, in uptr count,
                              in u64 bias) {
    u64 result = 2;
    uptr index = 0;
    while (index < count) {
        u64 value = data[index] + bias;
        if ((value & 1u64) != 0u64) {
            result = result + value * 3u64 + 1u64;
        } else {
            result = result + value + 5u64;
        }
        index = index + 1;
    }
    return result;
}

[[runtime_only, noinline, link_name("unroll_store")]]
global u64 unroll_store(in const u64 *source, in u64 *destination,
                        in uptr count) {
    u64 result = 0;
    u64 carry = 1;
    uptr index = 0;
    while (index < count) {
        carry = carry + source[index];
        destination[index] = carry;
        result = result ^ (carry + index);
        index = index + 1;
    }
    return result;
}

global i32 loop_unrolling_entry() {
    u64 data[5];
    u64 stored[5];
    data[0] = 1;
    data[1] = 2;
    data[2] = 3;
    data[3] = 4;
    data[4] = 5;
    return (unroll_power(0) == 2) +
           (unroll_power(1) == 6) +
           (unroll_power(3) == 54) +
           (unroll_power(4) == 162) +
           (unroll_power(5) == 486) +
           (unroll_power(9) == 39366) +
           (unroll_sequence(0) == 2) +
           (unroll_sequence(1) == 6) +
           (unroll_sequence(3) == 59) +
           (unroll_sequence(4) == 180) +
           (unroll_sequence(5) == 544) +
           (unroll_sequence(9) == 44282) +
           (unroll_read(&data[0], 0) == 2) +
           (unroll_read(&data[0], 1) == 7) +
           (unroll_read(&data[0], 5) == 665) +
           (unroll_add_read(&data[0], 0, 7) == 2) +
           (unroll_add_read(&data[0], 1, 7) == 10) +
           (unroll_add_read(&data[0], 5, 7) == 52) +
           (unroll_masked_read(&data[0], 0, 7) == 2) +
           (unroll_masked_read(&data[0], 1, 7) == 15) +
           (unroll_masked_read(&data[0], 3, 7) == 58) +
           (unroll_masked_read(&data[0], 4, 7) == 92) +
           (unroll_masked_read(&data[0], 5, 7) == 109) +
           (unroll_store(&data[0], &stored[0], 5) == 20) +
           (stored[0] == 2) + (stored[1] == 4) +
           (stored[2] == 7) + (stored[3] == 11) +
           (stored[4] == 16);
}
