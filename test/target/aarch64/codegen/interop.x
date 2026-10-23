// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls between Cross and C under aapcs64, with interop.c compiled by
// Clang: register and stack arguments of every integer width, an inout
// channel as a pointer, function pointers, and records in registers, by
// reference, and through x8, both ways. test_main returns 0, or the number
// of the failed check.

volatile u32 zero = 0u32;

[[abi("aapcs64")]] typedef u32 (*c_binary)(in u32 x, in u32 y);

struct pair16 {
    i64 first;
    i32 second;
};

struct trio {
    u32 values[3];
};

struct big40 {
    u64 values[5];
};

[[abi("aapcs64")]] {
    global u64 c_mix(in u64 a, in i32 b, in u16 c, in i8 d, inout u64 e, in u32 f, in i16 g,
                     in u8 h, in i64 i, in u32 j, in i8 k, in u64 l);
    global u32 c_apply(in c_binary f, in u32 x, in u32 y);
    global i32 c_checks();

    global u64 cross_mix(in u64 a, in i32 b, in u16 c, in i8 d, inout u64 e, in u32 f,
                         in i16 g, in u8 h, in i64 i, in u32 j, in i8 k, in u64 l) {
        e = e + 1u64;
        i64 sb = b;
        i64 sd = d;
        i64 sg = g;
        i64 sk = k;
        return a + (u64)sb * 3u64 + c * 5u64 + (u64)sd * 7u64 + e * 11u64 + f * 13u64 +
               (u64)sg * 17u64 + h * 19u64 + (u64)i * 23u64 + j * 29u64 + (u64)sk * 31u64 +
               l * 37u64;
    }

    global u32 cross_sum(in const u32 *values, in u32 count) {
        u32 total = 0u32;
        for (u32 index = 0u32; index < count; index = index + 1u32) {
            total = total + values[index];
        }
        return total;
    }

    static u32 difference(in u32 x, in u32 y) { return x - y; }

    global struct pair16 c_make_pair(in i64 first, in i32 second);
    global u64 c_sum_records(in struct big40 big, in struct trio trio, in struct pair16 pair);
    global struct big40 c_make_big(in u32 seed);

    global struct pair16 cross_make_pair(in i64 first, in i32 second) {
        struct pair16 result;
        result.first = first * 2i64;
        result.second = second - 1;
        return result;
    }

    global u64 cross_sum_records(in struct big40 big, in struct trio trio,
                                 in struct pair16 pair) {
        u64 total = (u64)pair.first + (u64)(i64)pair.second;
        for (u32 index = 0u32; index < 5u32; index = index + 1u32) {
            total = total + big.values[index] * (index + 1u32);
        }
        for (u32 index = 0u32; index < 3u32; index = index + 1u32) {
            total = total + (trio.values[index] << index);
        }
        return total;
    }

    global struct big40 cross_make_big(in u32 seed) {
        struct big40 result;
        for (u32 index = 0u32; index < 5u32; index = index + 1u32) {
            result.values[index] = (u64)seed * index + 3u64;
        }
        return result;
    }
}

global u32 test_main() {
    const u32 z = zero;
    if (c_checks() != 0) return 1u32;
    u64 cell = 100u64 + z;
    const u64 mixed = c_mix(1u64 + z, -2, 60000u16, -4i8, cell, 0x80000000u32, -300i16, 200u8,
                            -5i64, 9u32, -127i8 - 1i8, 1234u64);
    if (cell != 101u64) return 2u32;
    cell = 100u64 + z;
    if (cross_mix(1u64 + z, -2, 60000u16, -4i8, cell, 0x80000000u32, -300i16, 200u8, -5i64,
                  9u32, -127i8 - 1i8, 1234u64) != mixed) {
        return 3u32;
    }
    if (c_apply(difference, 50u32 + z, 8u32) != 43u32) return 4u32;
    struct pair16 pair = c_make_pair(-21i64 + (i64)z, 8);
    if (pair.first != -42i64 || pair.second != 7) return 5u32;
    struct big40 big = c_make_big(7u32 + z);
    if (big.values[4] != 31u64) return 6u32;
    struct trio trio;
    for (u32 index = 0u32; index < 3u32; index = index + 1u32) {
        trio.values[index] = index * 11u32 + 1u32 + z;
    }
    if (c_sum_records(big, trio, pair) != cross_sum_records(big, trio, pair)) return 7u32;
    return 0u32;
}
