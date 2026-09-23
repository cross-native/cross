// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i64 native_mix(in i64 left, in i64 right) {
    return ((left << 2) ^ right) + left / right;
}

global i64 native_loop(in i64 limit) {
    i64 total = 0;
    for (i64 index = 0; index < limit; ++index) {
        if (index == 2) {
            continue;
        }
        if (index == 6) {
            break;
        }
        total += index * 3;
    }
    return total;
}

global u64 native_patch() {
    return $::patch(7u64) + 5u64;
}

global u128 native_wide(in u128 value) {
    return value + 0x10000000000000000000000000000000u128;
}

global u64 native_divrem(in u64 left, in u64 right) {
    return left / right + left % right;
}

global u64 native_rotate(in u64 value, in u64 count) {
    return (value << count) | (value >> (64u64 - count));
}

global void native_adjust(inout i64 value) {
    value += 2;
}

global i64 native_copyout() {
    i64 value = 5;
    native_adjust(value);
    return value;
}

global i64 native_entry() {
    return native_loop(8) + native_mix(20, 3) + native_patch() +
           native_copyout() + native_divrem(100u64, 7u64) +
           native_rotate(1u64, 8u64);
}
