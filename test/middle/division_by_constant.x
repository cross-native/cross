// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Every quotient and remainder by a constant divisor must equal the same
// operation on a divisor read from a volatile object, which the compiler
// cannot see and therefore divides with the target's divide instruction or
// inline divide sequence.

#define U8_DIVISORS(X) \
    X(u8, u8, 1u8, 101) X(u8, u8, 2u8, 102) X(u8, u8, 3u8, 103) \
    X(u8, u8, 7u8, 104) X(u8, u8, 10u8, 105) X(u8, u8, 16u8, 106) \
    X(u8, u8, 100u8, 107) X(u8, u8, 127u8, 108) X(u8, u8, 128u8, 109) \
    X(u8, u8, 200u8, 110) X(u8, u8, 255u8, 111)

#define I8_DIVISORS(X) \
    X(i8, u8, 1i8, 201) X(i8, u8, -1i8, 202) X(i8, u8, 2i8, 203) \
    X(i8, u8, 3i8, 204) X(i8, u8, -3i8, 205) X(i8, u8, 7i8, 206) \
    X(i8, u8, -7i8, 207) X(i8, u8, 10i8, 208) X(i8, u8, 16i8, 209) \
    X(i8, u8, -16i8, 210) X(i8, u8, 100i8, 211) X(i8, u8, 127i8, 212) \
    X(i8, u8, -127i8, 213) X(i8, u8, (i8)0x80u8, 214)

#define U16_DIVISORS(X) \
    X(u16, u16, 1u16, 301) X(u16, u16, 3u16, 302) X(u16, u16, 7u16, 303) \
    X(u16, u16, 10u16, 304) X(u16, u16, 100u16, 305) \
    X(u16, u16, 1000u16, 306) X(u16, u16, 1024u16, 307) \
    X(u16, u16, 32768u16, 308) X(u16, u16, 65535u16, 309)

#define I16_DIVISORS(X) \
    X(i16, u16, 1i16, 401) X(i16, u16, -1i16, 402) X(i16, u16, 3i16, 403) \
    X(i16, u16, -7i16, 404) X(i16, u16, 10i16, 405) \
    X(i16, u16, 1000i16, 406) X(i16, u16, -1000i16, 407) \
    X(i16, u16, 32767i16, 408) X(i16, u16, (i16)0x8000u16, 409)

#define U32_DIVISORS(X) \
    X(u32, u32, 1u32, 501) X(u32, u32, 2u32, 502) X(u32, u32, 3u32, 503) \
    X(u32, u32, 5u32, 504) X(u32, u32, 6u32, 505) X(u32, u32, 7u32, 506) \
    X(u32, u32, 9u32, 507) X(u32, u32, 10u32, 508) X(u32, u32, 11u32, 509) \
    X(u32, u32, 12u32, 510) X(u32, u32, 13u32, 511) X(u32, u32, 25u32, 512) \
    X(u32, u32, 60u32, 513) X(u32, u32, 100u32, 514) \
    X(u32, u32, 125u32, 515) X(u32, u32, 641u32, 516) \
    X(u32, u32, 1000u32, 517) X(u32, u32, 1024u32, 518) \
    X(u32, u32, 3600u32, 519) X(u32, u32, 65535u32, 520) \
    X(u32, u32, 65536u32, 521) X(u32, u32, 65537u32, 522) \
    X(u32, u32, 1000000u32, 523) X(u32, u32, 0x55555555u32, 524) \
    X(u32, u32, 0x7fffffffu32, 525) X(u32, u32, 0x80000000u32, 526) \
    X(u32, u32, 0x80000001u32, 527) X(u32, u32, 0xfffffffeu32, 528) \
    X(u32, u32, 0xffffffffu32, 529)

#define I32_DIVISORS(X) \
    X(i32, u32, 1i32, 601) X(i32, u32, -1i32, 602) X(i32, u32, 2i32, 603) \
    X(i32, u32, -2i32, 604) X(i32, u32, 3i32, 605) X(i32, u32, -3i32, 606) \
    X(i32, u32, 5i32, 607) X(i32, u32, 6i32, 608) X(i32, u32, 7i32, 609) \
    X(i32, u32, -7i32, 610) X(i32, u32, 9i32, 611) X(i32, u32, 10i32, 612) \
    X(i32, u32, -10i32, 613) X(i32, u32, 11i32, 614) \
    X(i32, u32, 12i32, 615) X(i32, u32, 13i32, 616) \
    X(i32, u32, 25i32, 617) X(i32, u32, 60i32, 618) \
    X(i32, u32, -60i32, 619) X(i32, u32, 100i32, 620) \
    X(i32, u32, 125i32, 621) X(i32, u32, 641i32, 622) \
    X(i32, u32, 1000i32, 623) X(i32, u32, 1024i32, 624) \
    X(i32, u32, -1024i32, 625) X(i32, u32, 65536i32, 626) \
    X(i32, u32, 1000000i32, 627) X(i32, u32, 0x55555555i32, 628) \
    X(i32, u32, 0x7fffffffi32, 629) X(i32, u32, -0x7fffffffi32, 630) \
    X(i32, u32, (i32)0x80000000u32, 631)

#define U64_DIVISORS(X) \
    X(u64, u64, 1u64, 701) X(u64, u64, 2u64, 702) X(u64, u64, 3u64, 703) \
    X(u64, u64, 5u64, 704) X(u64, u64, 6u64, 705) X(u64, u64, 7u64, 706) \
    X(u64, u64, 10u64, 707) X(u64, u64, 11u64, 708) \
    X(u64, u64, 12u64, 709) X(u64, u64, 100u64, 710) \
    X(u64, u64, 641u64, 711) X(u64, u64, 1000u64, 712) \
    X(u64, u64, 1024u64, 713) X(u64, u64, 3600u64, 714) \
    X(u64, u64, 274177u64, 715) X(u64, u64, 1000000u64, 716) \
    X(u64, u64, 0xffffffffu64, 717) X(u64, u64, 0x100000000u64, 718) \
    X(u64, u64, 0x100000001u64, 719) X(u64, u64, 1000000000000u64, 720) \
    X(u64, u64, 0x5555555555555555u64, 721) \
    X(u64, u64, 0x7fffffffffffffffu64, 722) \
    X(u64, u64, 0x8000000000000000u64, 723) \
    X(u64, u64, 0x8000000000000001u64, 724) \
    X(u64, u64, 0xfffffffffffffffeu64, 725) \
    X(u64, u64, 0xffffffffffffffffu64, 726)

#define I64_DIVISORS(X) \
    X(i64, u64, 1i64, 801) X(i64, u64, -1i64, 802) X(i64, u64, 2i64, 803) \
    X(i64, u64, -2i64, 804) X(i64, u64, 3i64, 805) X(i64, u64, -3i64, 806) \
    X(i64, u64, 5i64, 807) X(i64, u64, 7i64, 808) X(i64, u64, -7i64, 809) \
    X(i64, u64, 10i64, 810) X(i64, u64, -10i64, 811) \
    X(i64, u64, 12i64, 812) X(i64, u64, 100i64, 813) \
    X(i64, u64, 641i64, 814) X(i64, u64, 1000i64, 815) \
    X(i64, u64, 1024i64, 816) X(i64, u64, -1024i64, 817) \
    X(i64, u64, 0xffffffffi64, 818) X(i64, u64, 0x100000000i64, 819) \
    X(i64, u64, 1000000000000i64, 820) \
    X(i64, u64, -1000000000000i64, 821) \
    X(i64, u64, 0x5555555555555555i64, 822) \
    X(i64, u64, 0x7fffffffffffffffi64, 823) \
    X(i64, u64, -0x7fffffffffffffffi64, 824) \
    X(i64, u64, (i64)0x8000000000000000u64, 825)

#define DEFINE_DIVISION(T, UT, D, CODE) \
    [[noinline]] global T quotient_##CODE(in T x) { return x / (D); } \
    [[noinline]] global T remainder_##CODE(in T x) { return x % (D); }

U8_DIVISORS(DEFINE_DIVISION)
I8_DIVISORS(DEFINE_DIVISION)
U16_DIVISORS(DEFINE_DIVISION)
I16_DIVISORS(DEFINE_DIVISION)
U32_DIVISORS(DEFINE_DIVISION)
I32_DIVISORS(DEFINE_DIVISION)
U64_DIVISORS(DEFINE_DIVISION)
I64_DIVISORS(DEFINE_DIVISION)

// A mask, a logical shift, and a zero extension bound the dividend's
// significant bits.
[[noinline]] global u32 masked_quotient(in u32 x) {
    return (x & 0xffffu32) / 10u32;
}
[[noinline]] global u32 shifted_quotient(in u32 x) {
    return (x >> 8u32) / 7u32;
}
[[noinline]] global u64 widened_quotient(in u32 x) { return (u64)x / 10u64; }
[[noinline]] global u64 widened_remainder(in u32 x) {
    return (u64)x % 1000u64;
}

global volatile u8 divisor_u8 = 1u8;
global volatile i8 divisor_i8 = 1i8;
global volatile u16 divisor_u16 = 1u16;
global volatile i16 divisor_i16 = 1i16;
global volatile u32 divisor_u32 = 1u32;
global volatile i32 divisor_i32 = 1i32;
global volatile u64 divisor_u64 = 1u64;
global volatile i64 divisor_i64 = 1i64;

// Truncation to each width turns these into that width's boundary values.
global u64 sample_table[40] = {
    0u64, 1u64, 2u64, 3u64, 7u64, 10u64, 99u64, 100u64, 101u64, 0x7eu64,
    0x7fu64, 0x80u64, 0x81u64, 0xfeu64, 0xffu64, 0x100u64, 1000u64,
    0x7ffeu64, 0x7fffu64, 0x8000u64, 0x8001u64, 0xfffeu64, 0xffffu64,
    0x10000u64, 0x7ffffffeu64, 0x7fffffffu64, 0x80000000u64,
    0x80000001u64, 0xfffffffeu64, 0xffffffffu64, 0x100000000u64,
    0x7ffffffffffffffeu64, 0x7fffffffffffffffu64, 0x8000000000000000u64,
    0x8000000000000001u64, 0xfffffffffffffffeu64, 0xffffffffffffffffu64,
    0xdeadbeefcafebabeu64, 0x0123456789abcdefu64, 0xfedcba9876543210u64,
};

global u64 random_state = 0x9e3779b97f4a7c15u64;

global u64 next_random() {
    u64 value = random_state;
    value ^= value << 13u32;
    value ^= value >> 7u32;
    value ^= value << 17u32;
    random_state = value;
    return value;
}

global u32 sample_count = 104u32;

// The table, values around multiples of the divisor's magnitude near zero
// and near both ends of the range, and a pseudo-random sweep. `divisor` is
// the divisor's bit pattern at width `bits`.
global u64 sample(in u32 index, in u64 divisor, in u32 bits,
                  in u32 is_signed) {
    u64 mask = bits == 64u32 ? 0xffffffffffffffffu64
                             : (1u64 << bits) - 1u64;
    if (index < 40u32) return sample_table[index] & mask;
    u64 magnitude = divisor;
    if (is_signed != 0u32 && (divisor >> (bits - 1u32)) != 0u64) {
        magnitude = (0u64 - divisor) & mask;
    }
    u64 limit = is_signed != 0u32 ? mask >> 1u32 : mask;
    u64 top = limit / magnitude * magnitude;
    u64 value = 0u64;
    switch (index - 40u32) {
    case 0: value = magnitude - 1u64; break;
    case 1: value = magnitude; break;
    case 2: value = magnitude + 1u64; break;
    case 3: value = 2u64 * magnitude - 1u64; break;
    case 4: value = 2u64 * magnitude; break;
    case 5: value = 2u64 * magnitude + 1u64; break;
    case 6: value = top - 1u64; break;
    case 7: value = top; break;
    case 8: value = limit; break;
    case 9: value = 0u64 - magnitude + 1u64; break;
    case 10: value = 0u64 - magnitude; break;
    case 11: value = 0u64 - magnitude - 1u64; break;
    case 12: value = 0u64 - top + 1u64; break;
    case 13: value = 0u64 - top; break;
    case 14: value = 0u64 - top - 1u64; break;
    case 15: value = 0u64 - 2u64 * magnitude; break;
    default: value = next_random(); break;
    }
    return value & mask;
}

// The most negative value divided by -1 overflows; skip that pair.
#define CHECK_DIVISION(T, UT, D, CODE) \
    divisor_##T = (D); \
    for (u32 index = 0u32; index < sample_count; index++) { \
        T d = divisor_##T; \
        u32 bits = (u32)sizeof(T) * 8u32; \
        bool is_signed = (T)(0u8 - 1u8) < (T)0u8; \
        T x = (T)sample(index, (u64)(UT)d, bits, (u32)is_signed); \
        UT minimum = (UT)1u8 << (bits - 1u32); \
        if (is_signed && (UT)x == minimum && d == (T)(0u8 - 1u8)) { \
            continue; \
        } \
        if (quotient_##CODE(x) != x / d) return CODE; \
        if (remainder_##CODE(x) != x % d) return CODE + 10000; \
    }

// One function per width keeps unoptimized frames small.
#define CHECK_WIDTH(NAME, DIVISORS) \
    [[noinline]] global i32 NAME() { DIVISORS(CHECK_DIVISION) return 0; }

CHECK_WIDTH(check_u8, U8_DIVISORS)
CHECK_WIDTH(check_i8, I8_DIVISORS)
CHECK_WIDTH(check_u16, U16_DIVISORS)
CHECK_WIDTH(check_i16, I16_DIVISORS)
CHECK_WIDTH(check_u32, U32_DIVISORS)
CHECK_WIDTH(check_i32, I32_DIVISORS)
CHECK_WIDTH(check_u64, U64_DIVISORS)
CHECK_WIDTH(check_i64, I64_DIVISORS)

// 128-bit operations have no divide instruction: shifts, comparisons, and a
// multiply for a dividend widened from 64 bits replace the software divide.
#if $::has_feature($::feature::integer128)
#define U128_DIVISORS(X) \
    X(u128, u128, 1u128, 1001) X(u128, u128, 2u128, 1002) \
    X(u128, u128, 10u128, 1003) \
    X(u128, u128, 0x10000000000000000u128, 1004) \
    X(u128, u128, 0x80000000000000000000000000000000u128, 1005) \
    X(u128, u128, 0xffffffffffffffffffffffffffffffffu128, 1006)

#define I128_DIVISORS(X) \
    X(i128, u128, 1i128, 1101) X(i128, u128, -1i128, 1102) \
    X(i128, u128, 4i128, 1103) X(i128, u128, -4i128, 1104) \
    X(i128, u128, 7i128, 1105) \
    X(i128, u128, (i128)0x80000000000000000000000000000000u128, 1106)

U128_DIVISORS(DEFINE_DIVISION)
I128_DIVISORS(DEFINE_DIVISION)

[[noinline]] global u128 widened_quotient128(in u64 x) {
    return (u128)x / 10u128;
}

global volatile u128 divisor_u128 = 1u128;
global volatile i128 divisor_i128 = 1i128;

global u128 samples128[8] = {
    0u128, 1u128, 9u128, 10u128, 0xffffffffffffffffu128,
    0x123456789abcdef0123456789abcdefu128,
    0x80000000000000000000000000000000u128,
    0xffffffffffffffffffffffffffffffffu128,
};

#define CHECK_DIVISION128(T, UT, D, CODE) \
    divisor_##T = (D); \
    for (u32 index = 0u32; index < 8u32; index++) { \
        T d = divisor_##T; \
        T x = (T)samples128[index]; \
        if ((T)(0u8 - 1u8) < (T)0u8 && d == (T)(0u8 - 1u8) && \
            (UT)x == (UT)1u8 << 127u32) { \
            continue; \
        } \
        if (quotient_##CODE(x) != x / d) return CODE; \
        if (remainder_##CODE(x) != x % d) return CODE + 10000; \
    }

[[noinline]] global i32 check_128() {
    U128_DIVISORS(CHECK_DIVISION128)
    I128_DIVISORS(CHECK_DIVISION128)
    divisor_u128 = 10u128;
    for (u32 index = 0u32; index < 8u32; index++) {
        u64 x = (u64)samples128[index];
        if (widened_quotient128(x) != (u128)x / divisor_u128) return 1200;
    }
    return 0;
}
#endif

global i32 division_entry() {
#if $::has_feature($::feature::integer128)
    if (check_128() != 0) return check_128();
#endif
    i32 failed = check_u8();
    if (failed == 0) failed = check_i8();
    if (failed == 0) failed = check_u16();
    if (failed == 0) failed = check_i16();
    if (failed == 0) failed = check_u32();
    if (failed == 0) failed = check_i32();
    if (failed == 0) failed = check_u64();
    if (failed == 0) failed = check_i64();
    if (failed != 0) return failed;
    divisor_u32 = 10u32;
    for (u32 index = 0u32; index < sample_count; index++) {
        u32 x = (u32)sample(index, 10u64, 32u32, 0u32);
        u32 d = divisor_u32;
        if (masked_quotient(x) != (x & 0xffffu32) / d) return 901;
        if (shifted_quotient(x) != (x >> 8u32) / (d - 3u32)) return 902;
        if (widened_quotient(x) != (u64)x / (u64)d) return 903;
        if (widened_remainder(x) != (u64)x % ((u64)d * 100u64)) return 904;
    }
    return 0;
}
