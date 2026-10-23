// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Bit intrinsics and rotations at 8, 16, 32, and 64 bits, through clz,
// rbit, rev, and ror where the widths have them and MIR's inline sequences
// elsewhere, against bit-by-bit loops. test_main returns 0, or the number
// of the failed check.

volatile u64 inputs[8] = {0u64, 1u64, 0x80u64, 0x8000000000000000u64, 0xffffffffffffffffu64,
                          0x0123456789abcdefu64, 0x00000000f0000000u64, 0x5555aaaa3333ccccu64};

[[noinline]] static u32 clz8(in u8 x) { return $::clz(x); }
[[noinline]] static u32 clz16(in u16 x) { return $::clz(x); }
[[noinline]] static u32 clz32(in u32 x) { return $::clz(x); }
[[noinline]] static u32 clz64(in u64 x) { return $::clz(x); }
[[noinline]] static u32 ctz16(in u16 x) { return $::ctz(x); }
[[noinline]] static u32 ctz32(in u32 x) { return $::ctz(x); }
[[noinline]] static u32 ctz64(in u64 x) { return $::ctz(x); }
[[noinline]] static u32 popcount32(in u32 x) { return $::popcount(x); }
[[noinline]] static u32 popcount64(in u64 x) { return $::popcount(x); }
[[noinline]] static u16 bswap16(in u16 x) { return $::bswap(x); }
[[noinline]] static u32 bswap32(in u32 x) { return $::bswap(x); }
[[noinline]] static u64 bswap64(in u64 x) { return $::bswap(x); }
[[noinline]] static u32 rotl32(in u32 x, in u32 n) { return $::rotl(x, n); }
[[noinline]] static u32 rotr32(in u32 x, in u32 n) { return $::rotr(x, n); }
[[noinline]] static u64 rotl64(in u64 x, in u32 n) { return $::rotl(x, n); }
[[noinline]] static u64 rotr64(in u64 x, in u32 n) { return $::rotr(x, n); }
[[noinline]] static u8 rotl8(in u8 x, in u32 n) { return $::rotl(x, n); }
[[noinline]] static u32 rotl32_by_5(in u32 x) { return $::rotl(x, 5u32); }
[[noinline]] static u64 rotr64_by_9(in u64 x) { return $::rotr(x, 9u32); }

static u32 leading(in u64 x, in u32 bits) {
    u32 count = 0u32;
    for (u32 i = bits; i > 0u32; i = i - 1u32) {
        if (((x >> (i - 1u32)) & 1u64) != 0u64) break;
        count = count + 1u32;
    }
    return count;
}

static u32 trailing(in u64 x, in u32 bits) {
    u32 count = 0u32;
    for (u32 i = 0u32; i < bits; i = i + 1u32) {
        if (((x >> i) & 1u64) != 0u64) break;
        count = count + 1u32;
    }
    return count;
}

static u32 ones(in u64 x) {
    u32 count = 0u32;
    for (u32 i = 0u32; i < 64u32; i = i + 1u32) count = count + (u32)((x >> i) & 1u64);
    return count;
}

static u64 swapped(in u64 x, in u32 bytes) {
    u64 result = 0u64;
    for (u32 i = 0u32; i < bytes; i = i + 1u32) {
        result = (result << 8) | ((x >> (i * 8u32)) & 0xffu64);
    }
    return result;
}

static u64 rotated_left(in u64 x, in u32 n, in u32 bits) {
    const u64 mask = bits == 64u32 ? 0xffffffffffffffffu64 : (1u64 << bits) - 1u64;
    u64 value = x & mask;
    for (u32 i = 0u32; i < n % bits; i = i + 1u32) {
        value = ((value << 1) | (value >> (bits - 1u32))) & mask;
    }
    return value;
}

global u32 test_main() {
    for (u32 index = 0u32; index < 8u32; index = index + 1u32) {
        const u64 x = inputs[index];
        if (clz8(x) != leading(x & 0xffu64, 8u32)) return 1u32;
        if (clz16(x) != leading(x & 0xffffu64, 16u32)) return 2u32;
        if (clz32(x) != leading(x & 0xffffffffu64, 32u32)) return 3u32;
        if (clz64(x) != leading(x, 64u32)) return 4u32;
        if (ctz16(x) != trailing(x, 16u32)) return 5u32;
        if (ctz32(x) != trailing(x, 32u32)) return 6u32;
        if (ctz64(x) != trailing(x, 64u32)) return 7u32;
        if (popcount32(x) != ones(x & 0xffffffffu64)) return 8u32;
        if (popcount64(x) != ones(x)) return 9u32;
        if (bswap16(x) != swapped(x, 2u32)) return 10u32;
        if (bswap32(x) != swapped(x, 4u32)) return 11u32;
        if (bswap64(x) != swapped(x, 8u32)) return 12u32;
        for (u32 n = 0u32; n < 70u32; n = n + 13u32) {
            if (rotl32(x, n) != rotated_left(x, n, 32u32)) return 13u32;
            if (rotr32(x, n) != rotated_left(x, 32u32 - n % 32u32, 32u32)) return 14u32;
            if (rotl64(x, n) != rotated_left(x, n, 64u32)) return 15u32;
            if (rotr64(x, n) != rotated_left(x, 64u32 - n % 64u32, 64u32)) return 16u32;
            if (rotl8(x, n) != rotated_left(x, n, 8u32)) return 17u32;
        }
        if (rotl32_by_5(x) != rotated_left(x, 5u32, 32u32)) return 18u32;
        if (rotr64_by_9(x) != rotated_left(x, 55u32, 64u32)) return 19u32;
    }
    return 0u32;
}
