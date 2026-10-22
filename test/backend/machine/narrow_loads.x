// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[noinline]] global uptr prefix(in const u8 *a, in const u8 *b, in uptr lim) {
    uptr l = 0;
    while (l < lim && a[l] == b[l]) ++l;
    return l;
}

[[noinline]] global uptr prefix_nonzero(in const u8 *a, in const u8 *b, in uptr lim) {
    uptr l = 0;
    while (l < lim && a[l] != (u8)0 && a[l] == b[l]) ++l;
    return l;
}

[[noinline]] global uptr prefix16(in const u16 *a, in const u16 *b, in uptr lim) {
    uptr l = 0;
    while (l < lim && a[l] == b[l]) ++l;
    return l;
}

[[noinline]] global bool less8(in const u8 *a, in const u8 *b) { return *a < *b; }
[[noinline]] global bool less16(in const i16 *a, in const i16 *b) { return *a < *b; }
[[noinline]] global bool below200(in const u8 *a) { return *a < 200i32; }
[[noinline]] global bool negative16(in const i16 *a) { return *a < 0i32; }
[[noinline]] global bool equal8(in const u8 *a, in const u8 *b) { return *a == *b; }
[[noinline]] global bool volatile_equal(in const volatile u8 *a, in const volatile u8 *b) {
    return *a == *b;
}
[[noinline]] global bool changed(in u8 *a, in const u8 *b) {
    const u8 first = *b;
    *a = (u8)17;
    return *a == first;
}

[[noinline]] global u32 load_u8_32(in const u8 *a, in uptr i) { return (u32)a[i]; }
[[noinline]] global u64 load_u8_64(in const u8 *a, in uptr i) { return (u64)a[i]; }
[[noinline]] global u32 load_u16_32(in const u16 *a, in uptr i) { return (u32)a[i]; }
[[noinline]] global u64 load_u16_64(in const u16 *a, in uptr i) { return (u64)a[i]; }
[[noinline]] global i32 load_i8_32(in const i8 *a, in uptr i) { return (i32)a[i]; }
[[noinline]] global i64 load_i8_64(in const i8 *a, in uptr i) { return (i64)a[i]; }
[[noinline]] global i32 load_i16_32(in const i16 *a, in uptr i) { return (i32)a[i]; }
[[noinline]] global i64 load_i16_64(in const i16 *a, in uptr i) { return (i64)a[i]; }

static u16 global_word;
[[noinline]] global u64 global_widen() { return (u64)global_word; }

// Keep loop-carried words live across a narrow memory comparison.
[[noinline]] global u64 compare_pressure(in const u8 *left, in const u8 *right,
                            in uptr rounds, in u64 seed) {
    if (rounds == 0) return seed;
    u64 a = seed; u64 b = seed ^ 1; u64 c = seed + 2; u64 d = seed * 3;
    u64 e = seed ^ 4; u64 f = seed + 5; u64 g = seed * 7; u64 h = seed ^ 8;
    u64 i = seed + 9; u64 j = seed * 11; u64 k = seed ^ 12; u64 l = seed + 13;
    for (uptr r = 0; r < rounds && left[r] == right[r]; ++r) {
        a += b; d ^= a; d = (d << 32) | (d >> 32);
        c += d; b ^= c; b = (b << 24) | (b >> 40);
        e += f; h ^= e; h = (h << 32) | (h >> 32);
        g += h; f ^= g; f = (f << 24) | (f >> 40);
        i += j; l ^= i; l = (l << 32) | (l >> 32);
        k += l; j ^= k; j = (j << 24) | (j >> 40);
        a += f; l ^= a; c += h; j ^= c; e += l; b ^= e;
        g += j; d ^= g; i += b; h ^= i; k += d; f ^= k;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l;
}

// Dense dispatch must ignore bits outside the selector's declared width.
[[noinline]] global i32 switch8(in u8 x) {
    switch (x) {
    case 0: return 10i32;
    case 1: return 11i32;
    case 2: return 12i32;
    case 3: return 13i32;
    case 4: return 14i32;
    case 5: return 15i32;
    }
    return 99i32;
}
[[noinline]] global i32 switch16(in u16 x) {
    switch (x) {
    case 0: return 20i32;
    case 1: return 21i32;
    case 2: return 22i32;
    case 3: return 23i32;
    case 4: return 24i32;
    case 5: return 25i32;
    }
    return 99i32;
}
[[noinline]] global i32 chain8(in u8 x) {
    if (x == (u8)0) return 30i32;
    if (x == (u8)1) return 31i32;
    if (x == (u8)2) return 32i32;
    if (x == (u8)3) return 33i32;
    if (x == (u8)4) return 34i32;
    return 99i32;
}
[[noinline]] global i32 switch32_trunc(in u64 x) {
    switch ((u32)x) {
    case 0u32: return 40i32;
    case 1u32: return 41i32;
    case 2u32: return 42i32;
    case 3u32: return 43i32;
    case 4u32: return 44i32;
    case 5u32: return 45i32;
    }
    return 99i32;
}
[[noinline]] static u8 return8(in u64 x) { return (u8)x; }
[[noinline]] static u16 return16(in u64 x) { return (u16)x; }
[[noinline]] global i32 dispatch_high(in u64 x) {
    const u8 byte = return8(x);
    const u16 word = return16(x);
    const i32 expected8 = (x & 255) < 6 ? (i32)(x & 255) : -1i32;
    const i32 expected16 = (x & 65535) < 6 ? (i32)(x & 65535) : -1i32;
    if (switch8((u8)x) != (expected8 < 0 ? 99i32 : 10i32 + expected8)) return 1i32;
    if (switch8(byte) != (expected8 < 0 ? 99i32 : 10i32 + expected8)) return 2i32;
    if (chain8(byte) != (expected8 < 0 || expected8 == 5i32 ? 99i32 : 30i32 + expected8)) return 3i32;
    if (switch16((u16)x) != (expected16 < 0 ? 99i32 : 20i32 + expected16)) return 4i32;
    if (switch16(word) != (expected16 < 0 ? 99i32 : 20i32 + expected16)) return 5i32;
    return 0i32;
}

[[noinline]] global u32 hash8(in const u8 *a, in uptr n) {
    u32 h = 5381u32;
    for (uptr i = 0; i < n; ++i) h = h * 33u32 + (u32)a[i];
    return h;
}
[[noinline]] global u32 offset_load(in const u8 *a, in uptr i) {
    return (u32)a[i + 2];
}
[[noinline]] global u32 repeated_load(in const u8 *a, in uptr i) {
    return (u32)a[i] * 3u32 + (u32)a[i];
}

[[noinline]] global i32 test_entry() {
    for (u64 i = 0; i < 6; ++i) {
        if (dispatch_high(0x1234567800010000u64 + i) != 0i32) return 19i32;
        if (switch32_trunc(0x100000000u64 + i) != 40i32 + (i32)i) return 20i32;
    }
    if (dispatch_high(0xffffffffffffffffu64) != 0i32 ||
        switch32_trunc(0x100000006u64) != 99i32) return 21i32;
    u8 a[260];
    u8 b[260];
    u16 words[260];
    u16 other[260];
    i8 signed_bytes[260];
    i16 signed_words[260];
    for (uptr i = 0; i < 260; ++i) {
        a[i] = (u8)i;
        b[i] = (u8)i;
        words[i] = (u16)(i * 257);
        other[i] = words[i];
        signed_bytes[i] = (i8)((i32)(i & 255) - 128i32);
        signed_words[i] = (i16)((i32)(i * 251) - 32768i32);
    }
    u32 reference_hash = 5381u32;
    for (uptr i = 0; i < 258; ++i) {
        if (offset_load(a, i) != (u32)a[i + 2] ||
            repeated_load(a, i) != (u32)a[i] * 4u32 ||
            hash8(a, i) != reference_hash) return 22i32;
        reference_hash = reference_hash * 33u32 + (u32)a[i];
    }
    if (compare_pressure(a, b, 0, 12345u64) != 12345u64 ||
        compare_pressure(a, b, 1, 12345u64) != 0x6302d120f40002d2u64 ||
        compare_pressure(a, b, 9, 12345u64) != 0xa596c331c77ad728u64) return 17i32;
    b[1] = (u8)255;
    if (compare_pressure(a, b, 9, 12345u64) != 0x6302d120f40002d2u64) return 18i32;
    b[1] = a[1];
    if (prefix_nonzero(a, (const u8*)0, 259) != 0) return 16i32;
    if (prefix_nonzero(a + 1, b + 1, 259) != 255) return 12i32;
    if (prefix_nonzero((const u8*)0, (const u8*)0, 0) != 0) return 13i32;
    if (prefix((const u8*)0, (const u8*)0, 0) != 0) return 1i32;
    if (prefix16((const u16*)0, (const u16*)0, 0) != 0) return 2i32;
    for (uptr i = 0; i < 260; ++i) {
        if (prefix(a, b, i) != i || prefix16(words, other, i) != i) return 3i32;
        b[i] = (u8)(a[i] ^ (u8)255);
        other[i] = (u16)(words[i] ^ (u16)65535);
        if (prefix(a, b, 260) != i || prefix16(words, other, 260) != i) return 4i32;
        if (less8(a + i, b + i) != (a[i] < b[i])) return 5i32;
        if (below200(a + i) != (a[i] < 200i32)) return 14i32;
        if (negative16(signed_words + i) != (signed_words[i] < 0i32)) return 15i32;
        if (equal8(a + i, b + i)) return 6i32;
        if (volatile_equal(a + i, b + i)) return 7i32;
        b[i] = a[i];
        other[i] = words[i];
        if (load_u8_32(a, i) != (u32)a[i]) return 8i32;
        if (load_u8_64(a, i) != (u64)a[i]) return 8i32;
        if (load_u16_32(words, i) != (u32)words[i]) return 8i32;
        if (load_u16_64(words, i) != (u64)words[i]) return 8i32;
        if (load_i8_32(signed_bytes, i) != (i32)signed_bytes[i]) return 8i32;
        if (load_i8_64(signed_bytes, i) != (i64)signed_bytes[i]) return 8i32;
        if (load_i16_32(signed_words, i) != (i32)signed_words[i]) return 8i32;
        if (load_i16_64(signed_words, i) != (i64)signed_words[i]) return 8i32;
        if (less16(signed_words + i, signed_words + 259) !=
            (signed_words[i] < signed_words[259])) return 9i32;
    }
    a[0] = (u8)91;
    if (changed(a, a) || a[0] != (u8)17) return 10i32;
    global_word = (u16)65535;
    if (global_widen() != 65535u64) return 11i32;
    return 0i32;
}
