// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loads and stores of every integer width and extension: frame slots near
// and far from sp, global objects, arrays with scaled indexes, record
// members, and pointers with positive and negative displacements; the
// global objects take the GOT in position-independent code.
// test_main returns 0, or the number of the failed check.

volatile u64 zero = 0u64;

u8 g_u8 = 200u8;
i8 g_i8 = -100i8;
u16 g_u16 = 60000u16;
i16 g_i16 = -30000i16;
global u32 g_u32 = 4000000000u32;
i32 g_i32 = -2000000000;
global u64 g_u64 = 0xfedcba9876543210u64;
i64 g_i64 = -5000000000000i64;
global u16 g_array[10] = {1u16, 2u16, 3u16, 4u16, 5u16, 6u16, 7u16, 8u16, 9u16, 10u16};
static u8 g_buffer[4096];

struct header {
    u8 kind;
    i16 delta;
    u32 length;
    u64 checksum;
    i8 flags[3];
};

static struct header g_header;

[[noinline]] static void fill(in struct header *h, in u32 seed) {
    h->kind = seed;
    h->delta = -(i16)(seed & 0xffu32);
    h->length = seed * 1000u32;
    h->checksum = (u64)seed << 40;
    h->flags[0] = -1i8;
    h->flags[1] = 2i8;
    h->flags[2] = -3i8;
}

[[noinline]] static u64 digest(in struct header *h) {
    i64 delta = h->delta;
    i64 flags = h->flags[0] + h->flags[1] + h->flags[2];
    return h->kind + (u64)delta + h->length + h->checksum + (u64)flags;
}

[[noinline]] static i64 widen_all(in u64 z) {
    i64 total = g_i8;
    total = total + g_u8 + g_i16 + g_u16 + g_i32 + (i64)(g_u32 + (u32)z);
    return total + g_i64 + (i64)(g_u64 >> 8);
}

[[noinline]] static void store_all(in u64 base) {
    g_u8 = base;
    g_i8 = base + 1u64;
    g_u16 = base + 2u64;
    g_i16 = base + 3u64;
    g_u32 = base + 4u64;
    g_i32 = base + 5u64;
    g_u64 = base + 6u64;
    g_i64 = base + 7u64;
}

[[noinline]] static u32 sum_window(in u32 *p, in u32 count) {
    u32 total = 0u32;
    for (u32 i = 0u32; i < count; i = i + 1u32) total = total + p[i] + p[-1];
    return total;
}

// Members past the reach of a byte's and a half's scaled offset.
struct distant {
    u8 pad[5000];
    u8 byte;
    u8 more[4000];
    u16 half;
    i8 signed_byte;
};

[[noinline]] static u64 far_frame(in u64 seed) {
    struct distant far;
    u8 bytes[5000];
    u16 halves[3000];
    u32 words[3000];
    u64 doubles[5000];
    i8 sbytes[4];
    for (u32 i = 0u32; i < 5000u32; i = i + 1u32) {
        bytes[i] = seed + i;
        doubles[i] = seed * i;
    }
    for (u32 i = 0u32; i < 3000u32; i = i + 1u32) {
        halves[i] = seed ^ i;
        words[i] = seed + i * 7u32;
    }
    sbytes[0] = -1i8;
    sbytes[3] = -127i8 - 1i8;
    far.byte = seed;
    far.half = seed * 1000u64;
    far.signed_byte = -(i8)seed;
    i64 s0 = sbytes[0];
    i64 s3 = sbytes[3];
    i64 s4 = far.signed_byte;
    return bytes[4999] + halves[2999] + words[2999] + doubles[4999] + (u64)(s0 + s3 + s4) +
           far.byte + far.half;
}

global u32 test_main() {
    const u64 z = zero;
    if (widen_all(z) != -100i64 + 200i64 - 30000i64 + 60000i64 - 2000000000i64 +
                            4000000000i64 - 5000000000000i64 + 0xfedcba98765432i64) {
        return 1u32;
    }
    store_all(0x123456789abcde00u64 + z);
    if (g_u8 != 0u8 || g_i8 != 1i8 || g_u16 != 0xde02u16 || g_i16 != (i16)0xde03u16) {
        return 2u32;
    }
    if (g_u32 != 0x9abcde04u32 || g_i32 != (i32)0x9abcde05u32 ||
        g_u64 != 0x123456789abcde06u64 || g_i64 != 0x123456789abcde07i64) {
        return 3u32;
    }
    u32 index = 9u32 + (u32)z;
    if (g_array[index] != 10u16 || g_array[index - 9u32] != 1u16) return 4u32;
    g_array[index - 4u32] = 60000u16;
    if (g_array[5] != 60000u16) return 5u32;
    g_buffer[4095u32 + (u32)z] = 77u8;
    if (g_buffer[4095] != 77u8) return 6u32;
    fill(&g_header, 0x1234u32 + (u32)z);
    if (digest(&g_header) != 0x34u64 - 0x34u64 + 0x1234u64 * 1000u64 + (0x1234u64 << 40) - 2u64) {
        return 7u32;
    }
    struct header local;
    fill(&local, 7u32 + (u32)z);
    if (digest(&local) != 7u64 - 7u64 + 7000u64 + (7u64 << 40) - 2u64) return 8u32;
    u32 words[8];
    for (u32 i = 0u32; i < 8u32; i = i + 1u32) words[i] = i * 3u32 + (u32)z;
    if (sum_window(&words[1], 7u32) != 84u32) return 9u32;
    if (far_frame(5u64 + z) != ((5u64 + 4999u64) & 0xffu64) + ((5u64 ^ 2999u64) & 0xffffu64) + 5u64 + 2999u64 * 7u64 +
                                   5u64 * 4999u64 - 134u64 + 5u64 + 5000u64) {
        return 10u32;
    }
    volatile u32 cell;
    cell = 5u32 + (u32)z;
    cell = cell * 3u32;
    if (cell != 15u32) return 11u32;
    return 0u32;
}
