// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The Release 2 selections: SEB and SEH sign extensions, WSBH, DSBH, and
// DSHD byte swaps, EXT and INS (DEXT and DINS with their M and U forms)
// bit fields, DROTR, DROTR32, and DROTRV rotates, and the MIPS32 MUL, with
// fields at either end of a word or doubleword. test_entry returns the
// number of wrong results; it runs with -fno-eval-calls.

struct fields {
    u32 a : 5;
    u32 b : 11;
    u32 c : 16;
};

[[noinline, runtime_only]] global i32 sext8(in u32 x) { return (i32) (i8) (u8) x; }
[[noinline, runtime_only]] global i32 sext16(in u32 x) {
    return (i32) (i16) (u16) x;
}
[[noinline, runtime_only]] global i64 sext8_64(in u64 x) {
    return (i64) (i8) (u8) x;
}
[[noinline, runtime_only]] global i64 sext16_64(in u64 x) {
    return (i64) (i16) (u16) x;
}
[[noinline, runtime_only]] global u16 swap16(in u16 x) { return $::bswap(x); }
[[noinline, runtime_only]] global u32 swap32(in u32 x) { return $::bswap(x); }
[[noinline, runtime_only]] global u64 swap64(in u64 x) { return $::bswap(x); }

[[noinline, runtime_only]] global u32 ext_5_10(in u32 x) { return (x >> 5u32) & 0x3ffu32; }
[[noinline, runtime_only]] global u32 ext_0_20(in u32 x) { return x & 0xfffffu32; }
[[noinline, runtime_only]] global u32 ext_27_5(in u32 x) { return (x >> 27u32) & 0x1fu32; }
[[noinline, runtime_only]] global u32 ext_31_1(in u32 x) { return (x >> 31u32) & 1u32; }
[[noinline, runtime_only]] global u32 ext_1_31(in u32 x) {
    return (x >> 1u32) & 0x7fffffffu32;
}
[[noinline, runtime_only]] global u32 ext_pair(in u32 x) { return (x << 20u32) >> 24u32; }
[[noinline, runtime_only]] global u64 dext_37_12(in u64 x) {
    return (x >> 37u32) & 0xfffu64;
}
[[noinline, runtime_only]] global u64 dext_3_20(in u64 x) {
    return (x >> 3u32) & 0xfffffu64;
}
[[noinline, runtime_only]] global u64 dext_0_40(in u64 x) { return x & 0xffffffffffu64; }
[[noinline, runtime_only]] global u64 dext_20_44(in u64 x) {
    return (x >> 20u32) & 0xfffffffffffu64;
}
[[noinline, runtime_only]] global u64 dext_63_1(in u64 x) { return (x >> 63u32) & 1u64; }
[[noinline, runtime_only]] global u64 dext_1_63(in u64 x) {
    return (x >> 1u32) & 0x7fffffffffffffffu64;
}

[[noinline, runtime_only]] global u32 ins_8_8(in u32 x, in u32 y) {
    return (x & ~(0xffu32 << 8u32)) | ((y & 0xffu32) << 8u32);
}
[[noinline, runtime_only]] global u32 ins_27_5(in u32 x, in u32 y) {
    return (x & ~(0x1fu32 << 27u32)) | (y << 27u32);
}
[[noinline, runtime_only]] global u32 ins_0_16(in u32 x, in u32 y) {
    return (x & 0xffff0000u32) | (y & 0xffffu32);
}
[[noinline, runtime_only]] global u32 ins_31_1(in u32 x, in u32 y) {
    return (x & 0x7fffffffu32) | (y << 31u32);
}
[[noinline, runtime_only]] global u32 ins_4_1(in u32 x, in u32 y) {
    return (x & ~(1u32 << 4u32)) | ((y & 1u32) << 4u32);
}
[[noinline, runtime_only]] global u64 dins_40_16(in u64 x, in u64 y) {
    return (x & ~(0xffffu64 << 40u32)) | ((y & 0xffffu64) << 40u32);
}
[[noinline, runtime_only]] global u64 dins_8_8(in u64 x, in u64 y) {
    return (x & ~(0xffu64 << 8u32)) | ((y & 0xffu64) << 8u32);
}
[[noinline, runtime_only]] global u64 dins_16_32(in u64 x, in u64 y) {
    return (x & ~(0xffffffffu64 << 16u32)) | ((y & 0xffffffffu64) << 16u32);
}
[[noinline, runtime_only]] global u64 dins_32_32(in u64 x, in u64 y) {
    return (x & 0xffffffffu64) | (y << 32u32);
}
[[noinline, runtime_only]] global u64 dins_60_4(in u64 x, in u64 y) {
    return (x & ~(0xfu64 << 60u32)) | (y << 60u32);
}
[[noinline, runtime_only]] global u64 dins_33_30(in u64 x, in u64 y) {
    return (x & ~(0x3fffffffu64 << 33u32)) | ((y & 0x3fffffffu64) << 33u32);
}

[[noinline, runtime_only]] global u32 get_b(in struct fields* f) { return f->b; }
[[noinline, runtime_only]] global void set_b(in struct fields* f, in u32 v) { f->b = v; }
[[noinline, runtime_only]] global void set_a(in struct fields* f, in u32 v) { f->a = v; }

[[noinline, runtime_only]] global u64 rotr64(in u64 x, in u32 n) { return $::rotr(x, n); }
[[noinline, runtime_only]] global u64 rotl64(in u64 x, in u32 n) { return $::rotl(x, n); }
[[noinline, runtime_only]] global u64 rotr64_13(in u64 x) { return $::rotr(x, 13u32); }
[[noinline, runtime_only]] global u64 rotr64_32(in u64 x) { return $::rotr(x, 32u32); }
[[noinline, runtime_only]] global u64 rotr64_40(in u64 x) { return $::rotr(x, 40u32); }
[[noinline, runtime_only]] global u64 rotl64_1(in u64 x) { return $::rotl(x, 1u32); }

[[noinline, runtime_only]] global u32 mul32(in u32 x, in u32 y) { return x * y; }
[[noinline, runtime_only]] global i32 muli32(in i32 x, in i32 y) { return x * y; }
[[noinline, runtime_only]] global u32 accumulate(in u32 n, in u32 k) {
    u32 sum = 0u32;
    for (u32 i = 1u32; i <= n; i = i + 1u32) sum = sum + i * (i + k);
    return sum;
}

global u32 test_entry() {
    u32 wrong = 0u32;
    if (sext8(0x1234a5u32) != -91i32) wrong = wrong + 1u32;
    if (sext8(0x7fu32) != 127i32) wrong = wrong + 1u32;
    if (sext16(0x18765u32) != -30875i32) wrong = wrong + 1u32;
    if (sext16(0x7fffu32) != 32767i32) wrong = wrong + 1u32;
    if (sext8_64(0xff80u64) != -128i64) wrong = wrong + 1u32;
    if (sext16_64(0x8001u64) != -32767i64) wrong = wrong + 1u32;
    if (swap16(0xa1b2u16) != 0xb2a1u16) wrong = wrong + 1u32;
    if (swap32(0x89abcdefu32) != 0xefcdab89u32) wrong = wrong + 1u32;
    if (swap64(0x0123456789abcdefu64) != 0xefcdab8967452301u64) wrong = wrong + 1u32;

    if (ext_5_10(0x89abcdefu32) != 0x26fu32) wrong = wrong + 1u32;
    if (ext_0_20(0x89abcdefu32) != 0xbcdefu32) wrong = wrong + 1u32;
    if (ext_27_5(0x89abcdefu32) != 0x11u32) wrong = wrong + 1u32;
    if (ext_31_1(0x89abcdefu32) != 1u32) wrong = wrong + 1u32;
    if (ext_1_31(0x89abcdefu32) != 0x44d5e6f7u32) wrong = wrong + 1u32;
    if (ext_pair(0x89abcdefu32) != 0xdeu32) wrong = wrong + 1u32;
    if (dext_37_12(0x0123456789abcdefu64) != 0xa2bu64) wrong = wrong + 1u32;
    if (dext_3_20(0x0123456789abcdefu64) != 0x579bdu64) wrong = wrong + 1u32;
    if (dext_0_40(0x0123456789abcdefu64) != 0x6789abcdefu64) wrong = wrong + 1u32;
    if (dext_20_44(0x0123456789abcdefu64) != 0x123456789au64) wrong = wrong + 1u32;
    if (dext_63_1(0xfedcba9876543210u64) != 1u64) wrong = wrong + 1u32;
    if (dext_1_63(0x0123456789abcdefu64) != 0x0091a2b3c4d5e6f7u64) wrong = wrong + 1u32;

    if (ins_8_8(0x89abcdefu32, 0x5a5a5a5bu32) != 0x89ab5befu32) wrong = wrong + 1u32;
    if (ins_27_5(0x89abcdefu32, 0x5a5a5a5bu32) != 0xd9abcdefu32) wrong = wrong + 1u32;
    if (ins_0_16(0x89abcdefu32, 0x5a5a5a5bu32) != 0x89ab5a5bu32) wrong = wrong + 1u32;
    if (ins_31_1(0x89abcdefu32, 0x5a5a5a5au32) != 0x09abcdefu32) wrong = wrong + 1u32;
    if (ins_4_1(0x89abcdefu32, 0x5a5a5a5bu32) != 0x89abcdffu32) wrong = wrong + 1u32;
    if (dins_40_16(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0x01321d6789abcdefu64) {
        wrong = wrong + 1u32;
    }
    if (dins_8_8(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0x0123456789ab1defu64) {
        wrong = wrong + 1u32;
    }
    if (dins_16_32(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0x01237654321dcdefu64) {
        wrong = wrong + 1u32;
    }
    if (dins_32_32(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0x7654321d89abcdefu64) {
        wrong = wrong + 1u32;
    }
    if (dins_60_4(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0xd123456789abcdefu64) {
        wrong = wrong + 1u32;
    }
    if (dins_33_30(0x0123456789abcdefu64, 0xfedcba987654321du64) !=
        0x6ca8643b89abcdefu64) {
        wrong = wrong + 1u32;
    }

    struct fields f;
    f.a = 0x15u32;
    f.b = 0x2aau32;
    f.c = 0x1234u32;
    if (get_b(&f) != 0x2aau32) wrong = wrong + 1u32;
    set_b(&f, 0xfffff955u32);
    set_a(&f, 0x3u32);
    if (f.a != 3u32 || f.b != 0x155u32 || f.c != 0x1234u32) wrong = wrong + 1u32;

    if (rotr64(0x0123456789abcdefu64, 13u32) != 0x6f78091a2b3c4d5eu64) {
        wrong = wrong + 1u32;
    }
    if (rotr64(0x0123456789abcdefu64, 0u32) != 0x0123456789abcdefu64) {
        wrong = wrong + 1u32;
    }
    if (rotr64(0x0123456789abcdefu64, 63u32) != 0x02468acf13579bdeu64) {
        wrong = wrong + 1u32;
    }
    if (rotl64(0x0123456789abcdefu64, 40u32) != 0xabcdef0123456789u64) {
        wrong = wrong + 1u32;
    }
    if (rotr64_13(0x0123456789abcdefu64) != 0x6f78091a2b3c4d5eu64) wrong = wrong + 1u32;
    if (rotr64_32(0x0123456789abcdefu64) != 0x89abcdef01234567u64) wrong = wrong + 1u32;
    if (rotr64_40(0x0123456789abcdefu64) != 0x6789abcdef012345u64) wrong = wrong + 1u32;
    if (rotl64_1(0x0123456789abcdefu64) != 0x02468acf13579bdeu64) wrong = wrong + 1u32;

    if (mul32(0x12345678u32, 0x9abcdef1u32) != 0x366176f8u32) wrong = wrong + 1u32;
    if (muli32(-7i32, 123456789i32) != -864197523i32) wrong = wrong + 1u32;
    if (accumulate(100u32, 0x9e3779b9u32) != 0x125c5518u32) wrong = wrong + 1u32;
    return wrong;
}
