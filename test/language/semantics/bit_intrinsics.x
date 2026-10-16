// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// $::clz, $::ctz, $::popcount, $::rotl, $::rotr, and $::bswap at every
// integer width. Translation-time results are checked against literals, and
// run-time results against bit-by-bit loops over edge values, a
// pseudo-random sequence, and rotation counts of several types, negative and
// beyond the width included.

#if !$::has_intrinsic($::clz) || !$::has_intrinsic($::ctz) || \
    !$::has_intrinsic($::popcount) || !$::has_intrinsic($::rotl) || \
    !$::has_intrinsic($::rotr) || !$::has_builtin($::bswap)
#error every target has the bit intrinsics
#endif

// Zero gives the width; the counts are u32 at every width.
$::static_assert($::clz(0u8) == 8u32 && $::clz(0u16) == 16u32 &&
                 $::clz(0u32) == 32u32 && $::clz(0u64) == 64u32 &&
                 $::clz(0u128) == 128u32, "clz of zero");
$::static_assert($::ctz(0u8) == 8u32 && $::ctz(0u16) == 16u32 &&
                 $::ctz(0u32) == 32u32 && $::ctz(0u64) == 64u32 &&
                 $::ctz(0u128) == 128u32, "ctz of zero");
$::static_assert($::popcount(0u8) == 0u32 && $::popcount(0u128) == 0u32,
                 "popcount of zero");
$::static_assert($::clz((u8)0xff) == 0u32 && $::clz((i16)-1) == 0u32 &&
                 $::clz(0xffffffffu32) == 0u32 && $::clz((i64)-1) == 0u32 &&
                 $::clz((u128)-1) == 0u32, "clz of all ones");
$::static_assert($::ctz((i8)-1) == 0u32 && $::ctz((u128)-1) == 0u32,
                 "ctz of all ones");
$::static_assert($::popcount((u8)0xff) == 8u32 && $::popcount((i16)-1) == 16u32 &&
                 $::popcount(-1) == 32u32 && $::popcount((u64)-1) == 64u32 &&
                 $::popcount((i128)-1) == 128u32, "popcount of all ones");
// Single bits.
$::static_assert($::clz(1u8) == 7u32 && $::clz(0x80u8) == 0u32 &&
                 $::clz(1u16) == 15u32 && $::clz(1u32) == 31u32 &&
                 $::clz(0x80000000u32) == 0u32 && $::clz(1u64) == 63u32 &&
                 $::clz(0x100000000u64) == 31u32 && $::clz(1u128) == 127u32 &&
                 $::clz((u128)1 << 64) == 63u32 && $::clz((u128)1 << 127) == 0u32,
                 "clz of single bits");
$::static_assert($::ctz(0x80u8) == 7u32 && $::ctz(0x8000u16) == 15u32 &&
                 $::ctz(0x80000000u32) == 31u32 && $::ctz(0x100000000u64) == 32u32 &&
                 $::ctz(0x8000000000000000u64) == 63u32 &&
                 $::ctz((u128)1 << 64) == 64u32 && $::ctz((u128)1 << 127) == 127u32,
                 "ctz of single bits");
$::static_assert($::popcount(0x5555u16) == 8u32 &&
                 $::popcount(0x0123456789abcdefu64) == 32u32 &&
                 $::popcount(((u128)1 << 100) | 7u128) == 4u32, "popcount");
// Signed operands count their bits; a literal has its literal type.
$::static_assert($::clz((i8)1) == 7u32 && $::clz(1) == 31u32 &&
                 $::clz((i32)-2147483647 - 1) == 0u32, "clz of signed values");
// Rotations by the count modulo the width, a negative count the other way.
$::static_assert($::rotl(0x81u8, 1) == 0x03u8 && $::rotr(0x81u8, 1) == 0xc0u8 &&
                 $::rotl(0x81u8, 9) == 0x03u8 && $::rotl(0x81u8, -1) == 0xc0u8 &&
                 $::rotr(0x81u8, -1) == 0x03u8 && $::rotl(0x81u8, 8) == 0x81u8 &&
                 $::rotl(0x81u8, (u8)255) == 0xc0u8 && $::rotl(0x81u8, 0) == 0x81u8,
                 "rotations of u8");
$::static_assert($::rotl(0x8001u16, 4) == 0x0018u16 &&
                 $::rotr(0x8001u16, 4) == 0x1800u16 &&
                 $::rotl(0x12345678u32, 8) == 0x34567812u32 &&
                 $::rotr(0x12345678u32, 8) == 0x78123456u32 &&
                 $::rotl(0x12345678u32, -8) == 0x78123456u32 &&
                 $::rotl(0x12345678u32, 40i64) == 0x34567812u32 &&
                 $::rotl(0x12345678u32, (i8)-128) == 0x12345678u32 &&
                 $::rotl(0x0123456789abcdefu64, 4) == 0x123456789abcdef0u64 &&
                 $::rotr(0x0123456789abcdefu64, 68) == 0xf0123456789abcdeu64 &&
                 $::rotl((u128)1, 127) == (u128)1 << 127 &&
                 $::rotr((u128)1, -1) == (u128)2 && $::rotl((u128)3, 64u128) == (u128)3 << 64,
                 "rotations");
// A rotation of a signed value keeps its type.
$::static_assert($::rotl((i8)-128, 1) == (i8)1 && $::rotr((i32)1, 1) == (i32)-2147483647 - 1,
                 "rotations of signed values");
$::static_assert($::bswap(0xabu8) == 0xabu8 && $::bswap(0x1234u16) == 0x3412u16 &&
                 $::bswap(0x11223344u32) == 0x44332211u32 &&
                 $::bswap(0x0102030405060708u64) == 0x0807060504030201u64 &&
                 $::bswap(((u128)0x0102030405060708u64 << 64) | 0x090a0b0c0d0e0f10u128) ==
                     (((u128)0x100f0e0d0c0b0a09u64 << 64) | 0x0807060504030201u128) &&
                 $::bswap((i16)0x7f80) == (i16)-32641, "byte swaps");
$::static_assert($::clz((uptr)1) == (u32)(sizeof(uptr) * 8 - 1) &&
                 $::bswap((uptr)1) == (uptr)1 << (sizeof(uptr) * 8 - 8),
                 "pointer-width integers");

enum bit_flags [[underlying(u16)]] { flag_low = 1, flag_high = 0x8000 };
$::static_assert($::clz(flag_low) == 15u32 && $::ctz(flag_high) == 15u32 &&
                 (u16)$::rotl(flag_high, 1) == 1u16, "enumeration operands");

// Bit-by-bit references over the low `bits` of a value.
static u64 width_mask(in u32 bits) {
    return bits == 64 ? ~0u64 : (1u64 << bits) - 1u64;
}

static u32 reference_clz(in u64 x, in u32 bits) {
    u32 count = 0;
    while (count < bits && ((x >> (bits - 1u32 - count)) & 1u64) == 0u64) ++count;
    return count;
}

static u32 reference_ctz(in u64 x, in u32 bits) {
    u32 count = 0;
    while (count < bits && ((x >> count) & 1u64) == 0u64) ++count;
    return count;
}

static u32 reference_popcount(in u64 x, in u32 bits) {
    u32 count = 0;
    for (u32 index = 0; index < bits; ++index) {
        if (((x >> index) & 1u64) != 0u64) ++count;
    }
    return count;
}

static u64 reference_bswap(in u64 x, in u32 bits) {
    u64 result = 0;
    for (u32 index = 0; index < bits; index = index + 8u32) {
        result = (result << 8) | ((x >> index) & 0xffu64);
    }
    return result;
}

static u64 reference_rotl(in u64 x, in i64 n, in u32 bits) {
    i64 amount = n % (i64)bits;
    if (amount < 0) amount = amount + (i64)bits;
    const u64 mask = width_mask(bits);
    u64 value = x & mask;
    for (i64 step = 0; step < amount; ++step) {
        value = ((value << 1) | (value >> (bits - 1u32))) & mask;
    }
    return value;
}

[[noinline]] global u32 clz8(in u8 x) { return $::clz(x); }
[[noinline]] global u32 clz16(in u16 x) { return $::clz(x); }
[[noinline]] global u32 clz32(in u32 x) { return $::clz(x); }
[[noinline]] global u32 clz64(in u64 x) { return $::clz(x); }
[[noinline]] global u32 clz_signed16(in i16 x) { return $::clz(x); }
[[noinline]] global u32 clz_pointer(in uptr x) { return $::clz(x); }
// Leading ones, as MIPS CLO and DCLO count them.
[[noinline]] global u32 clz_complement32(in u32 x) { return $::clz(~x); }
[[noinline]] global u32 clz_complement64(in u64 x) { return $::clz(~x); }
[[noinline]] global u32 ctz8(in u8 x) { return $::ctz(x); }
[[noinline]] global u32 ctz16(in u16 x) { return $::ctz(x); }
[[noinline]] global u32 ctz32(in u32 x) { return $::ctz(x); }
[[noinline]] global u32 ctz64(in u64 x) { return $::ctz(x); }
[[noinline]] global u32 ctz_signed64(in i64 x) { return $::ctz(x); }
[[noinline]] global u32 popcount8(in u8 x) { return $::popcount(x); }
[[noinline]] global u32 popcount16(in u16 x) { return $::popcount(x); }
[[noinline]] global u32 popcount32(in u32 x) { return $::popcount(x); }
[[noinline]] global u32 popcount64(in u64 x) { return $::popcount(x); }
[[noinline]] global u32 popcount_signed8(in i8 x) { return $::popcount(x); }
[[noinline]] global u16 bswap16(in u16 x) { return $::bswap(x); }
[[noinline]] global u32 bswap32(in u32 x) { return $::bswap(x); }
[[noinline]] global u64 bswap64(in u64 x) { return $::bswap(x); }
[[noinline]] global i32 bswap_signed32(in i32 x) { return $::bswap(x); }
[[noinline]] global u8 rotl8(in u8 x, in i32 n) { return $::rotl(x, n); }
[[noinline]] global u8 rotr8(in u8 x, in u64 n) { return $::rotr(x, n); }
[[noinline]] global u16 rotl16(in u16 x, in u8 n) { return $::rotl(x, n); }
[[noinline]] global u16 rotr16(in u16 x, in i16 n) { return $::rotr(x, n); }
[[noinline]] global u32 rotl32(in u32 x, in i64 n) { return $::rotl(x, n); }
[[noinline]] global u32 rotr32(in u32 x, in u32 n) { return $::rotr(x, n); }
[[noinline]] global u64 rotl64(in u64 x, in u32 n) { return $::rotl(x, n); }
[[noinline]] global u64 rotr64(in u64 x, in i8 n) { return $::rotr(x, n); }
[[noinline]] global i64 rotl_signed64(in i64 x, in i64 n) { return $::rotl(x, n); }
// Constant counts select the immediate forms.
[[noinline]] global u32 rotl32_by_5(in u32 x) { return $::rotl(x, 5); }
[[noinline]] global u64 rotr64_by_40(in u64 x) { return $::rotr(x, 40); }

global volatile u64 edges[24] = {
    0u64, 1u64, 2u64, 3u64, 0x80u64, 0xffu64, 0x100u64, 0x8000u64,
    0xffffu64, 0x10000u64, 0x80000000u64, 0xffffffffu64, 0x100000000u64,
    0x8000000000000000u64, 0xffffffffffffffffu64, 0x7fffffffffffffffu64,
    0x5555555555555555u64, 0xaaaaaaaaaaaaaaaau64, 0x0123456789abcdefu64,
    0xfedcba9876543210u64, 0x00000000ffff0000u64, 0x0000ffff00000000u64,
    0x00ff000000000000u64, 0x0000000100000000u64,
};
global volatile i64 counts[22] = {
    0, 1, -1, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, -63,
    -65, 1000, -1001, 0x7fffffffffffffffi64,
};

static i32 check_counts(in u64 v) {
    if (clz8((u8)v) != reference_clz(v & 0xffu64, 8)) return 1;
    if (clz16((u16)v) != reference_clz(v & 0xffffu64, 16)) return 2;
    if (clz32((u32)v) != reference_clz(v & 0xffffffffu64, 32)) return 3;
    if (clz64(v) != reference_clz(v, 64)) return 4;
    if (clz_signed16((i16)v) != reference_clz(v & 0xffffu64, 16)) return 5;
    if (clz_pointer((uptr)v) != reference_clz(v & width_mask((u32)sizeof(uptr) * 8u32),
                                              (u32)sizeof(uptr) * 8u32)) return 6;
    if (clz_complement32((u32)v) != reference_clz(~v & 0xffffffffu64, 32)) return 7;
    if (clz_complement64(v) != reference_clz(~v, 64)) return 8;
    if (ctz8((u8)v) != reference_ctz(v & 0xffu64, 8)) return 11;
    if (ctz16((u16)v) != reference_ctz(v & 0xffffu64, 16)) return 12;
    if (ctz32((u32)v) != reference_ctz(v & 0xffffffffu64, 32)) return 13;
    if (ctz64(v) != reference_ctz(v, 64)) return 14;
    if (ctz_signed64((i64)v) != reference_ctz(v, 64)) return 15;
    if (popcount8((u8)v) != reference_popcount(v, 8)) return 21;
    if (popcount16((u16)v) != reference_popcount(v, 16)) return 22;
    if (popcount32((u32)v) != reference_popcount(v, 32)) return 23;
    if (popcount64(v) != reference_popcount(v, 64)) return 24;
    if (popcount_signed8((i8)v) != reference_popcount(v, 8)) return 25;
    if ((u64)bswap16((u16)v) != reference_bswap(v, 16)) return 31;
    if ((u64)bswap32((u32)v) != reference_bswap(v, 32)) return 32;
    if (bswap64(v) != reference_bswap(v, 64)) return 33;
    if ((u64)(u32)bswap_signed32((i32)v) != reference_bswap(v, 32)) return 34;
    if ((u64)rotl32_by_5((u32)v) != reference_rotl(v, 5, 32)) return 35;
    if (rotr64_by_40(v) != reference_rotl(v, -40, 64)) return 36;
    for (u32 index = 0; index < 22u32; ++index) {
        const i64 c = counts[index];
        if ((u64)rotl8((u8)v, (i32)c) != reference_rotl(v, (i64)(i32)c, 8)) return 41;
        if ((u64)rotr8((u8)v, (u64)c) != reference_rotl(v, -(i64)((u64)c % 8u64), 8))
            return 42;
        if ((u64)rotl16((u16)v, (u8)c) != reference_rotl(v, (i64)(u8)c, 16)) return 43;
        if ((u64)rotr16((u16)v, (i16)c) != reference_rotl(v, -(i64)(i16)c, 16)) return 44;
        if ((u64)rotl32((u32)v, c) != reference_rotl(v, c % 32, 32)) return 45;
        if ((u64)rotr32((u32)v, (u32)c) != reference_rotl(v, -(i64)((u32)c % 32u32), 32))
            return 46;
        if (rotl64(v, (u32)c) != reference_rotl(v, (i64)((u32)c % 64u32), 64)) return 47;
        if (rotr64(v, (i8)c) != reference_rotl(v, -(i64)(i8)c, 64)) return 48;
        if ((u64)rotl_signed64((i64)v, c) != reference_rotl(v, c % 64, 64)) return 49;
    }
    return 0;
}

#if $::has_feature($::feature::integer128)
[[noinline]] global u32 clz128(in u128 x) { return $::clz(x); }
[[noinline]] global u32 ctz128(in u128 x) { return $::ctz(x); }
[[noinline]] global u32 popcount128(in u128 x) { return $::popcount(x); }
[[noinline]] global u128 bswap128(in u128 x) { return $::bswap(x); }
[[noinline]] global u128 rotl128(in u128 x, in i32 n) { return $::rotl(x, n); }
[[noinline]] global i128 rotr128(in i128 x, in u8 n) { return $::rotr(x, n); }

static i32 check_wide(in u64 high, in u64 low) {
    const u128 x = ((u128)high << 64) | (u128)low;
    const u32 leading = high != 0u64 ? reference_clz(high, 64)
                                     : 64u32 + reference_clz(low, 64);
    const u32 trailing = low != 0u64 ? reference_ctz(low, 64)
                                     : 64u32 + reference_ctz(high, 64);
    if (clz128(x) != leading) return 51;
    if (ctz128(x) != trailing) return 52;
    if (popcount128(x) != reference_popcount(high, 64) + reference_popcount(low, 64))
        return 53;
    if (bswap128(x) != (((u128)reference_bswap(low, 64) << 64) |
                        (u128)reference_bswap(high, 64))) return 54;
    for (u32 index = 0; index < 22u32; ++index) {
        const i32 c = (i32)counts[index];
        // Rotating left by 64 + k is rotating the swapped halves by k.
        const i64 k = ((i64)c % 128 + 128) % 128;
        const u64 first = k >= 64 ? low : high;
        const u64 second = k >= 64 ? high : low;
        const u32 s = (u32)(k % 64);
        const u64 rotated_high = s == 0u32 ? first : (first << s) | (second >> (64u32 - s));
        const u64 rotated_low = s == 0u32 ? second : (second << s) | (first >> (64u32 - s));
        if (rotl128(x, c) != (((u128)rotated_high << 64) | (u128)rotated_low)) return 55;
        if ((u128)rotr128((i128)rotl128(x, c), (u8)c) != (u128)rotl128(x, c - (i32)(u8)c))
            return 56;
    }
    return 0;
}
#endif

global i32 test_entry() {
    for (u32 index = 0; index < 24u32; ++index) {
        const i32 failure = check_counts(edges[index]);
        if (failure != 0) return failure;
    }
    u64 state = 0x9e3779b97f4a7c15u64;
    for (u32 round = 0; round < 64u32; ++round) {
        state = state ^ (state << 13);
        state = state ^ (state >> 7);
        state = state ^ (state << 17);
        const i32 failure = check_counts(state);
        if (failure != 0) return failure;
#if $::has_feature($::feature::integer128)
        const i32 wide = check_wide(state, edges[round % 24u32]);
        if (wide != 0) return wide;
        const i32 swapped = check_wide(edges[round % 24u32], state);
        if (swapped != 0) return swapped;
#endif
    }
    return 0;
}
