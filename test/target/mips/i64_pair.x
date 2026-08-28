// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u64 mips_pair_cell;

[[runtime_only, noinline]]
global u64 mips_pair_add(in u64 left, in u64 right) {
    return left + right;
}

[[runtime_only, noinline]]
global u64 mips_pair_sub(in u64 left, in u64 right) {
    return left - right;
}

[[runtime_only, noinline]]
global u64 mips_pair_mul(in u64 left, in u64 right) {
    return left * right;
}

[[runtime_only, noinline]]
global u64 mips_pair_div(in u64 left, in u64 right) {
    return left / right;
}

[[runtime_only, noinline]]
global u64 mips_pair_rem(in u64 left, in u64 right) {
    return left % right;
}

[[runtime_only, noinline]]
global i64 mips_pair_sdiv(in i64 left, in i64 right) {
    return left / right;
}

[[runtime_only, noinline]]
global i64 mips_pair_srem(in i64 left, in i64 right) {
    return left % right;
}

[[runtime_only, noinline]]
global u64 mips_pair_shl(in u64 value, in u64 count) {
    return value << count;
}

[[runtime_only, noinline]]
global u64 mips_pair_shr(in u64 value, in u64 count) {
    return value >> count;
}

[[runtime_only, noinline]]
global i64 mips_pair_sar(in i64 value, in i64 count) {
    return value >> count;
}

[[runtime_only, noinline]]
global u64 mips_pair_rotl(in u64 value, in u64 count) {
    return (value << count) | (value >> (64u64 - count));
}

[[runtime_only, noinline]]
global u64 mips_pair_rotr(in u64 value, in u64 count) {
    return (value >> count) | (value << (64u64 - count));
}

[[runtime_only, noinline]]
global u64 mips_pair_memory(in u64 value) {
    mips_pair_cell = value;
    return mips_pair_cell;
}

[[runtime_only, noinline]]
global u64 mips_pair_stack(in u64 first, in u64 second, in u64 third,
                           in u64 fourth, in u64 fifth, in u64 sixth,
                           in u64 seventh) {
    return first + second + third + fourth + fifth + sixth + seventh;
}

[[runtime_only, noinline]]
global void mips_pair_inout(inout u64 value) {
    value += 2u64;
}

[[runtime_only, noinline]]
global void mips_pair_out(out u64 value) {
    value = 0x89abcdef01234567u64;
}

[[runtime_only, noinline, link_name("mips_pair_entry")]]
global i32 mips_pair_test() {
    i32 passed = 0;
    if (mips_pair_add(0xffffffffu64, 2u64) == 0x100000001u64) passed |= 0x0001;
    if (mips_pair_sub(0x100000000u64, 1u64) == 0xffffffffu64) passed |= 0x0002;
    if (mips_pair_mul(0x100000003u64, 5u64) == 0x50000000fu64) passed |= 0x0004;
    if (mips_pair_div(0x100000005u64, 3u64) == 0x55555557u64) passed |= 0x0008;
    if (mips_pair_rem(0x100000006u64, 3u64) == 1u64) passed |= 0x0010;
    if (mips_pair_sdiv(-10000000000i64, 3i64) == -3333333333i64) passed |= 0x0020;
    if (mips_pair_srem(-10000000000i64, 3i64) == -1i64) passed |= 0x0040;
    if (mips_pair_shl(1u64, 0u64) == 1u64 &&
        mips_pair_shl(1u64, 31u64) == 0x80000000u64 &&
        mips_pair_shl(1u64, 32u64) == 0x100000000u64 &&
        mips_pair_shl(1u64, 33u64) == 0x200000000u64 &&
        mips_pair_shl(1u64, 63u64) == 0x8000000000000000u64) passed |= 0x0080;
    if (mips_pair_shr(0x8000000000000000u64, 0u64) ==
            0x8000000000000000u64 &&
        mips_pair_shr(0x8000000000000000u64, 31u64) == 0x100000000u64 &&
        mips_pair_shr(0x8000000000000000u64, 32u64) == 0x80000000u64 &&
        mips_pair_shr(0x8000000000000000u64, 33u64) == 0x40000000u64 &&
        mips_pair_shr(0x8000000000000000u64, 63u64) == 1u64) passed |= 0x0100;
    if (mips_pair_sar(-8i64, 0i64) == -8i64 &&
        mips_pair_sar(-8i64, 2i64) == -2i64 &&
        mips_pair_sar(-8i64, 31i64) == -1i64 &&
        mips_pair_sar(-8i64, 32i64) == -1i64 &&
        mips_pair_sar(-8i64, 63i64) == -1i64) passed |= 0x0200;
    if (mips_pair_rotl(1u64, 0u64) == 1u64 &&
        mips_pair_rotl(1u64, 31u64) == 0x80000000u64 &&
        mips_pair_rotl(1u64, 32u64) == 0x100000000u64 &&
        mips_pair_rotl(1u64, 33u64) == 0x200000000u64 &&
        mips_pair_rotl(1u64, 63u64) == 0x8000000000000000u64) passed |= 0x0400;
    if (mips_pair_rotr(1u64, 0u64) == 1u64 &&
        mips_pair_rotr(1u64, 1u64) == 0x8000000000000000u64 &&
        mips_pair_rotr(1u64, 31u64) == 0x200000000u64 &&
        mips_pair_rotr(1u64, 32u64) == 0x100000000u64 &&
        mips_pair_rotr(1u64, 33u64) == 0x80000000u64 &&
        mips_pair_rotr(1u64, 63u64) == 2u64) passed |= 0x0800;
    if ((0x100000000u64 > 0xffffffffu64) &&
        (-2i64 < -1i64) &&
        (0x8000000000000000u64 >= 0x7fffffffffffffffu64)) passed |= 0x1000;
    if (((~0xf0f0f0f00f0f0f0fu64) == 0x0f0f0f0ff0f0f0f0u64) &&
        ((0xaaaaaaaa55555555u64 ^ 0xffff0000ffff0000u64) ==
         0x5555aaaaaaaa5555u64)) passed |= 0x2000;
    if (mips_pair_memory(0x1122334455667788u64) ==
        0x1122334455667788u64) passed |= 0x4000;
    i64 extended = -7i32;
    u64 widened = 0xffffffffu32;
    if ((extended == -7i64) && (widened == 0xffffffffu64)) passed |= 0x8000;
    if (mips_pair_stack(0x100000000u64, 2u64, 3u64, 4u64, 5u64,
                        6u64, 7u64) == 0x10000001bu64) passed |= 0x10000;
    u64 changed = 0xffffffffu64;
    mips_pair_inout(changed);
    if (changed == 0x100000001u64) passed |= 0x20000;
    u64 produced = 0u64;
    mips_pair_out(produced);
    if (produced == 0x89abcdef01234567u64) passed |= 0x40000;
    return passed;
}
