// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Vectors whose lanes are all constants, set lane by lane or computed lane
// by lane from such vectors, load as literals. test_entry returns zero when
// every lane read through a variable index has its expected value; it runs
// with -fno-eval-calls, so that the calls run.

typedef u8 u8x16 [[vector_size(16)]];
typedef u16 u16x8 [[vector_size(16)]];
typedef i32 i32x4 [[vector_size(16)]];
typedef u64 u64x2 [[vector_size(16)]];
typedef f32 f32x4 [[vector_size(16)]];
typedef f64 f64x2 [[vector_size(16)]];
typedef u32 u32x8 [[vector_size(32)]];

[[noinline]] global u32 bytes(in uptr k) {
    u8x16 v = 0u8;
    for (u32 lane = 1u32; lane < 16u32; ++lane) v[lane] = (u8)(lane * 17u32);
    u8x16 w = (v + 200u8) ^ 15u8;
    return w[k];
}

[[noinline]] global u32 halves(in uptr k) {
    u16x8 v = 65535u16;
    v[0] = 1u16;
    v[3] = 300u16;
    v[7] = 32768u16;
    u16x8 w = v - 2u16;
    return w[k];
}

[[noinline]] global i32 words(in uptr k) {
    i32x4 v = -1;
    v[1] = 7;
    v[2] = -2147483647 - 1;
    i32x4 w = v & 2147483647;
    return w[k];
}

[[noinline]] global u64 quads(in uptr k) {
    u64x2 v = 1u64;
    v[1] = 18446744073709551615u64;
    u64x2 w = v | 9223372036854775808u64;
    return w[k];
}

[[noinline]] global f32 singles(in uptr k) {
    f32x4 v = 1.5f32;
    v[2] = -0.25f32;
    v[3] = 3.0e38f32;
    return v[k];
}

[[noinline]] global f64 doubles(in uptr k) {
    f64x2 v = 2.5;
    v[1] = -1.0e300;
    return v[k];
}

[[noinline]] global u32 octets(in uptr k) {
    u32x8 v = 0u32;
    for (u32 lane = 0u32; lane < 8u32; ++lane) v[lane] = lane;
    u32x8 step = 8u32;
    u32x8 w = v + step + step;
    return w[k];
}

// A const vector local reads its lanes as values of the element type.
[[noinline]] global f64x2 pair(in f64 first, in f64 second) {
    f64x2 v = first;
    v[1] = second;
    return v;
}

[[noinline]] global f64 const_doubles(in uptr k) {
    const f64x2 v = pair(0.5, -3.0);
    return v[0] + v[k];
}

[[noinline]] global i32 const_words(in uptr k) {
    const i32x4 v = 5;
    return v[k] + v[3];
}

global u32 test_entry() {
    for (uptr k = 0; k < 16; ++k) {
        const u32 lane = (u32)k;
        if (bytes(k) != ((((lane * 17u32) + 200u32) & 255u32) ^ 15u32)) {
            return 1u32;
        }
    }
    const u32 halves_expected[8] = {65535u32, 65533u32, 65533u32, 298u32,
                                    65533u32, 65533u32, 65533u32, 32766u32};
    for (uptr k = 0; k < 8; ++k) {
        if (halves(k) != halves_expected[k]) return 2u32;
    }
    if (words(0) != 2147483647 || words(1) != 7 || words(2) != 0 ||
        words(3) != 2147483647) {
        return 3u32;
    }
    if (quads(0) != 9223372036854775809u64 ||
        quads(1) != 18446744073709551615u64) {
        return 4u32;
    }
    if (singles(0) != 1.5f32 || singles(1) != 1.5f32 ||
        singles(2) != -0.25f32 || singles(3) != 3.0e38f32) {
        return 5u32;
    }
    if (doubles(0) != 2.5 || doubles(1) != -1.0e300) return 6u32;
    for (uptr k = 0; k < 8; ++k) {
        if (octets(k) != (u32)k + 16u32) return 7u32;
    }
    if (const_doubles(0) != 1.0 || const_doubles(1) != -2.5) return 8u32;
    for (uptr k = 0; k < 4; ++k) {
        if (const_words(k) != 10) return 9u32;
    }
    return 0u32;
}
