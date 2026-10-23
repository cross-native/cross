// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The C side of interop.x, compiled by Clang: AAPCS64 functions that Cross
// code calls with register and stack arguments of every integer width, and
// that call Cross functions and a Cross function pointer.

typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u32;
typedef int i32;
typedef unsigned short u16;
typedef short i16;
typedef unsigned char u8;
typedef signed char i8;

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

// The copy that Clang calls for records, which freestanding code supplies.
void *memcpy(void *to, const void *from, unsigned long size) {
    volatile unsigned char *destination = to;
    const volatile unsigned char *source = from;
    for (unsigned long index = 0; index < size; ++index) destination[index] = source[index];
    return to;
}

u64 cross_mix(u64 a, i32 b, u16 c, i8 d, u64 *e, u32 f, i16 g, u8 h, i64 i, u32 j,
              i8 k, u64 l);
u32 cross_sum(const u32 *values, u32 count);
struct pair16 cross_make_pair(i64 first, i32 second);
u64 cross_sum_records(struct big40 big, struct trio trio, struct pair16 pair);
struct big40 cross_make_big(u32 seed);

struct pair16 c_make_pair(i64 first, i32 second) {
    struct pair16 result;
    result.first = first * 2;
    result.second = second - 1;
    return result;
}

u64 c_sum_records(struct big40 big, struct trio trio, struct pair16 pair) {
    u64 total = (u64)pair.first + (u64)(i64)pair.second;
    for (u32 index = 0; index < 5; ++index) total += big.values[index] * (index + 1);
    for (u32 index = 0; index < 3; ++index) total += trio.values[index] << index;
    return total;
}

struct big40 c_make_big(u32 seed) {
    struct big40 result;
    for (u32 index = 0; index < 5; ++index) result.values[index] = (u64)seed * index + 3;
    return result;
}

u64 c_mix(u64 a, i32 b, u16 c, i8 d, u64 *e, u32 f, i16 g, u8 h, i64 i, u32 j, i8 k,
          u64 l) {
    *e += 1;
    return a + (u64)(i64)b * 3 + (u64)c * 5 + (u64)(i64)d * 7 + *e * 11 + (u64)f * 13 +
           (u64)(i64)g * 17 + (u64)h * 19 + (u64)i * 23 + (u64)j * 29 + (u64)(i64)k * 31 +
           l * 37;
}

u32 c_apply(u32 (*f)(u32, u32), u32 x, u32 y) { return f(x, y) + 1; }

int c_checks(void) {
    u64 cell = 100;
    u64 expected = c_mix(1, -2, 60000, -4, &cell, 0x80000000u, -300, 200, -5, 9, -128, 1234);
    cell = 100;
    if (cross_mix(1, -2, 60000, -4, &cell, 0x80000000u, -300, 200, -5, 9, -128, 1234) !=
        expected) {
        return 1;
    }
    if (cell != 101) return 2;
    u32 values[5];
    for (u32 index = 0; index < 5; ++index) values[index] = index + 1;
    if (cross_sum(values, 5) != 15) return 3;
    struct pair16 pair = cross_make_pair(-21, 8);
    struct pair16 own = c_make_pair(-21, 8);
    if (pair.first != own.first || pair.second != own.second) return 4;
    struct big40 big = cross_make_big(7);
    struct big40 own_big = c_make_big(7);
    for (u32 index = 0; index < 5; ++index) {
        if (big.values[index] != own_big.values[index]) return 5;
    }
    struct trio trio;
    for (u32 index = 0; index < 3; ++index) trio.values[index] = index * 11 + 1;
    if (cross_sum_records(big, trio, pair) != c_sum_records(big, trio, pair)) return 6;
    return 0;
}
