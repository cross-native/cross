// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI uint32_t gimple_udiv32_7(uint32_t);
extern CROSS_MS_ABI uint32_t gimple_udiv32_10(uint32_t);
extern CROSS_MS_ABI uint32_t gimple_urem32_10(uint32_t);
extern CROSS_MS_ABI int32_t gimple_sdiv32_3(int32_t);
extern CROSS_MS_ABI int32_t gimple_sdiv32_7(int32_t);
extern CROSS_MS_ABI int32_t gimple_sdiv32_m7(int32_t);
extern CROSS_MS_ABI int32_t gimple_srem32_7(int32_t);
extern CROSS_MS_ABI uint64_t gimple_udiv64_7(uint64_t);
extern CROSS_MS_ABI uint64_t gimple_udiv64_10(uint64_t);
extern CROSS_MS_ABI uint64_t gimple_urem64_1000(uint64_t);
extern CROSS_MS_ABI int64_t gimple_sdiv64_7(int64_t);
extern CROSS_MS_ABI int64_t gimple_sdiv64_m10(int64_t);
extern CROSS_MS_ABI int64_t gimple_srem64_7(int64_t);
extern CROSS_MS_ABI uint32_t gimple_udiv16_10(uint32_t);

// Volatile divisors keep GCC's reference results on its divide instruction.
static volatile uint32_t u32_7 = 7, u32_10 = 10;
static volatile int32_t i32_3 = 3, i32_7 = 7, i32_m7 = -7;
static volatile uint64_t u64_7 = 7, u64_10 = 10, u64_1000 = 1000;
static volatile int64_t i64_7 = 7, i64_m10 = -10;

static int check(uint64_t value) {
    const uint32_t u32 = (uint32_t)value;
    const int32_t i32 = (int32_t)u32;
    const int64_t i64 = (int64_t)value;
    if (gimple_udiv32_7(u32) != u32 / u32_7) return 1;
    if (gimple_udiv32_10(u32) != u32 / u32_10) return 2;
    if (gimple_urem32_10(u32) != u32 % u32_10) return 3;
    if (gimple_sdiv32_3(i32) != i32 / i32_3) return 4;
    if (gimple_sdiv32_7(i32) != i32 / i32_7) return 5;
    if (gimple_sdiv32_m7(i32) != i32 / i32_m7) return 6;
    if (gimple_srem32_7(i32) != i32 % i32_7) return 7;
    if (gimple_udiv64_7(value) != value / u64_7) return 8;
    if (gimple_udiv64_10(value) != value / u64_10) return 9;
    if (gimple_urem64_1000(value) != value % u64_1000) return 10;
    if (gimple_sdiv64_7(i64) != i64 / i64_7) return 11;
    if (gimple_sdiv64_m10(i64) != i64 / i64_m10) return 12;
    if (gimple_srem64_7(i64) != i64 % i64_7) return 13;
    if (gimple_udiv16_10(u32) != (uint16_t)u32 / u32_10) return 14;
    return 0;
}

int main(void) {
    static const uint64_t edges[] = {
        0, 1, 6, 7, 8, 9, 10, 11, 13, 14, 999, 1000, 1001,
        0x7ffffffe, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffff9,
        0xfffffffa, 0xfffffffe, 0xffffffff, 0x100000000,
        0x7ffffffffffffffe, 0x7fffffffffffffff, 0x8000000000000000,
        0x8000000000000001, 0xfffffffffffffff9, 0xfffffffffffffffe,
        0xffffffffffffffff,
    };
    for (unsigned index = 0; index < sizeof edges / sizeof edges[0]; ++index) {
        const int failed = check(edges[index]);
        if (failed) return failed;
    }
    uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
    for (unsigned index = 0; index < 100000; ++index) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        const int failed = check(state);
        if (failed) return failed;
    }
    return 0;
}
