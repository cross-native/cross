// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls under the models of custom_abi.xm. Functions of "pinned" keep the
// x9 and x10 that the spill code changes, take arguments in the preserved
// x20 and x21, and leave their result in the preserved x19, which their
// frame therefore does not restore; custom_abi_probe.s calls pinned_sum
// with canaries in x9 and x10 and checks them. "halves" splits integers
// into 16-bit pieces. test_main returns 0, or the number of the failed
// check.

volatile u32 zero = 0u32;

[[abi("pinned"), noinline]]
global i32 pinned_sum(in i32 a, in i32 b, in u64 c) {
    u64 table[4];
    for (u32 i = 0u32; i < 4u32; i = i + 1u32) table[i] = c + i;
    return a * 3 + b + (i32)table[3];
}

[[abi("pinned"), noinline]]
static i32 pinned_twice(in i32 a) {
    return pinned_sum(a, a, 0u64) - 3;
}

// Under "halves", a u32 travels as two 16-bit pieces and a u64 as four,
// and the stack takes the pieces that find no register.
[[abi("halves"), noinline]]
static u64 halves_mix(in u32 a, in u64 b, in u16 c, in u32 d) {
    return ((u64)a << 32) + b * 3u64 + c + d;
}

// The probe's canary check: 0, or 100 plus the register that changed.
[[abi("aapcs64")]] global u64 custom_abi_probe(in i32 a, in i32 b, in u64 c);

global u32 test_main() {
    const i32 z = zero;
    if (pinned_sum(-7 + z, 5, 2u64) != -11) return 1u32;
    if (pinned_twice(10 + z) != 40) return 2u32;
    if (halves_mix(0x12345678u32 + (u32)z, 0x0102030405060708u64, 0xfedcu16,
                   0x80000001u32) != (0x12345678u64 << 32) + 0x0102030405060708u64 * 3u64 +
                                         0xfedcu64 + 0x80000001u64) {
        return 3u32;
    }
    const u64 probed = custom_abi_probe(4 + z, -1, 10u64);
    if (probed != 24u64) return (u32)probed;
    return 0u32;
}
