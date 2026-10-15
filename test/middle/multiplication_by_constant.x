// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Every product by a constant must equal the same product by a multiplier
// read from a volatile object, which the compiler multiplies with the
// target's multiply instruction or inline multiply sequence. Signed
// operands stay within the range in which the product does not overflow.

#define U32_MULTIPLIERS(X) \
    X(u32, u32, 0u32, 101) X(u32, u32, 1u32, 102) X(u32, u32, 2u32, 103) \
    X(u32, u32, 3u32, 104) X(u32, u32, 5u32, 105) X(u32, u32, 7u32, 106) \
    X(u32, u32, 10u32, 107) X(u32, u32, 15u32, 108) X(u32, u32, 17u32, 109) \
    X(u32, u32, 45u32, 110) X(u32, u32, 96u32, 111) \
    X(u32, u32, 1000u32, 112) X(u32, u32, 0x55555555u32, 113) \
    X(u32, u32, 0x7fffffffu32, 114) X(u32, u32, 0x80000000u32, 115) \
    X(u32, u32, 0x80000001u32, 116) X(u32, u32, 0xc0000001u32, 117) \
    X(u32, u32, 0xfffffff8u32, 118) X(u32, u32, 0xfffffff9u32, 119) \
    X(u32, u32, 0xfffffffeu32, 120) X(u32, u32, 0xffffffffu32, 121)

#define I32_MULTIPLIERS(X) \
    X(i32, u32, 1i32, 201) X(i32, u32, -1i32, 202) X(i32, u32, 2i32, 203) \
    X(i32, u32, -2i32, 204) X(i32, u32, 3i32, 205) X(i32, u32, -3i32, 206) \
    X(i32, u32, 5i32, 207) X(i32, u32, 7i32, 208) X(i32, u32, -7i32, 209) \
    X(i32, u32, -8i32, 210) X(i32, u32, 9i32, 211) X(i32, u32, 12i32, 212) \
    X(i32, u32, 17i32, 213) X(i32, u32, -17i32, 214) \
    X(i32, u32, 31i32, 215) X(i32, u32, 100i32, 216) \
    X(i32, u32, -100i32, 217) X(i32, u32, 1000i32, 218) \
    X(i32, u32, 0x7fffffffi32, 219) X(i32, u32, -0x7fffffffi32, 220)

#define U64_MULTIPLIERS(X) \
    X(u64, u64, 3u64, 301) X(u64, u64, 7u64, 302) X(u64, u64, 17u64, 303) \
    X(u64, u64, 100u64, 304) X(u64, u64, 0xffffffffu64, 305) \
    X(u64, u64, 0x100000001u64, 306) \
    X(u64, u64, 0x7fffffffffffffffu64, 307) \
    X(u64, u64, 0x8000000000000001u64, 308) \
    X(u64, u64, 0xc000000000000001u64, 309) \
    X(u64, u64, 0xfffffffffffffff8u64, 310) \
    X(u64, u64, 0xfffffffffffffff9u64, 311) \
    X(u64, u64, 0xffffffffffffffffu64, 312)

#define I64_MULTIPLIERS(X) \
    X(i64, u64, 3i64, 401) X(i64, u64, -3i64, 402) X(i64, u64, 7i64, 403) \
    X(i64, u64, -7i64, 404) X(i64, u64, -8i64, 405) \
    X(i64, u64, 17i64, 406) X(i64, u64, 100i64, 407) \
    X(i64, u64, -100i64, 408) X(i64, u64, 0x100000001i64, 409) \
    X(i64, u64, 0x7fffffffffffffffi64, 410)

#define DEFINE_PRODUCT(T, UT, C, CODE) \
    [[noinline]] global T product_##CODE(in T x) { return x * (C); }

U32_MULTIPLIERS(DEFINE_PRODUCT)
I32_MULTIPLIERS(DEFINE_PRODUCT)
U64_MULTIPLIERS(DEFINE_PRODUCT)
I64_MULTIPLIERS(DEFINE_PRODUCT)

global volatile u32 multiplier_u32 = 1u32;
global volatile i32 multiplier_i32 = 1i32;
global volatile u64 multiplier_u64 = 1u64;
global volatile i64 multiplier_i64 = 1i64;

global u64 random_state = 0x9e3779b97f4a7c15u64;

global u64 next_random() {
    u64 value = random_state;
    value ^= value << 13u32;
    value ^= value >> 7u32;
    value ^= value << 17u32;
    random_state = value;
    return value;
}

global u32 sample_count = 64u32;

// A sample of width `bits` whose signed product by `multiplier` does not
// overflow: values up to the largest such magnitude, both signs, the bounds
// themselves, and a pseudo-random sweep. Unsigned samples are unbounded.
global u64 sample(in u32 index, in u64 multiplier, in u32 bits,
                  in u32 is_signed) {
    u64 mask = bits == 64u32 ? 0xffffffffffffffffu64
                             : (1u64 << bits) - 1u64;
    u64 bound = mask;
    if (is_signed != 0u32) {
        u64 magnitude = multiplier;
        if ((multiplier >> (bits - 1u32)) != 0u64) {
            magnitude = (0u64 - multiplier) & mask;
        }
        bound = (mask >> 1u32) / magnitude;
    }
    u64 value = 0u64;
    switch (index) {
    case 0: value = 0u64; break;
    case 1: value = 1u64; break;
    case 2: value = bound; break;
    case 3: value = bound - 1u64; break;
    case 4: value = bound >> 1u32; break;
    default: value = bound == mask ? next_random()
                                   : next_random() % (bound + 1u64); break;
    }
    if (is_signed != 0u32 && (index & 1u32) != 0u32) value = 0u64 - value;
    return value & mask;
}

#define CHECK_PRODUCT(T, UT, C, CODE) \
    multiplier_##T = (C); \
    for (u32 index = 0u32; index < sample_count; index++) { \
        T c = multiplier_##T; \
        u32 bits = (u32)sizeof(T) * 8u32; \
        bool is_signed = (T)(0u8 - 1u8) < (T)0u8; \
        T x = (T)sample(index, (u64)(UT)c, bits, (u32)is_signed); \
        if (product_##CODE(x) != x * c) return CODE; \
    }

// One function per width keeps unoptimized frames small.
#define CHECK_WIDTH(NAME, MULTIPLIERS) \
    [[noinline]] global i32 NAME() { MULTIPLIERS(CHECK_PRODUCT) return 0; }

CHECK_WIDTH(check_u32, U32_MULTIPLIERS)
CHECK_WIDTH(check_i32, I32_MULTIPLIERS)
CHECK_WIDTH(check_u64, U64_MULTIPLIERS)
CHECK_WIDTH(check_i64, I64_MULTIPLIERS)

global i32 multiplication_entry() {
    i32 failed = check_u32();
    if (failed == 0) failed = check_i32();
    if (failed == 0) failed = check_u64();
    if (failed == 0) failed = check_i64();
    return failed;
}
