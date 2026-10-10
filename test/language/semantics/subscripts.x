// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A subscript extends an index narrower than an address by the index type's
// signedness and keeps the low address bits of a wider one, like pointer
// arithmetic, both in translation-time evaluation and at run time.

global u32 table8[8] = {1u32, 2u32, 4u32, 8u32, 16u32, 32u32, 64u32, 128u32};
global u8 bytes[0x10010];
global u32 cells[8];
global u32 wide[300];
global u32 scaled[300];
global volatile i32 minus_one = -1i32;
global volatile i64 three = 3i64;
global volatile uptr bias = 0x80000000uptr;

[[noinline]] global u32 read_i8(in const u32 *p, in i8 x) { return p[x]; }
[[noinline]] global u32 read_i16(in const u32 *p, in i16 x) { return p[x]; }
[[noinline]] global u32 read_i32(in const u32 *p, in i32 x) { return p[x]; }
[[noinline]] global u32 read_i64(in const u32 *p, in i64 x) { return p[x]; }
[[noinline]] global u8 read_u8(in const u8 *p, in u8 x) { return p[x]; }
[[noinline]] global u8 read_u16(in const u8 *p, in u16 x) { return p[x]; }
[[noinline]] global u8 read_u32(in const u8 *p, in u32 x) { return p[x]; }
[[noinline]] global void write_i8(in u32 *p, in i8 x, in u32 value) { p[x] = value; }
[[noinline]] global void add_i16(in u32 *p, in i16 x) { p[x] += 1u32; }
[[noinline]] global const u32 *address_i32(in const u32 *p, in i32 x) { return &p[x]; }
[[noinline]] global u32 table_i64(in i64 x) { return table8[x]; }

// Narrow counters that wrap select elements below 256 and 65536.
[[noinline]] global u32 wrap_sum8(in u32 count) {
    u32 sum = 0u32;
    u8 i = 0u8;
    for (u32 n = 0u32; n < count; ++n) {
        sum += (u32)bytes[i];
        ++i;
    }
    return sum;
}
[[noinline]] global u32 wrap_sum16(in u16 start, in u32 count) {
    u32 sum = 0u32;
    u16 i = start;
    for (u32 n = 0u32; n < count; ++n) {
        sum += (u32)bytes[i];
        ++i;
    }
    return sum;
}
[[noinline]] global u32 signed_pairs(in const u32 *p, in i32 count) {
    u32 sum = 0u32;
    for (i32 i = 0i32; i < count; ++i) sum += p[i] + p[i + 1i32];
    return sum;
}
[[noinline]] global u32 unsigned_sum(in const u32 *p, in u32 count) {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < count; ++i) sum += p[i];
    return sum;
}
[[noinline]] global void unsigned_scale(in u32 *destination, in const u32 *source,
                                        in u32 count) {
    for (u32 i = 0u32; i < count; ++i) destination[i] = source[i] * 3u32;
}
[[noinline]] global u32 unsigned_find(in const u32 *p, in u32 count, in u32 key) {
    for (u32 i = 0u32; i < count; ++i)
        if (p[i] == key) return i;
    return count;
}
[[noinline]] global u32 narrow_sum16(in const u32 *p, in u16 count) {
    u32 sum = 0u32;
    for (u16 i = 0u16; i < count; ++i) sum += p[i];
    return sum;
}
[[noinline]] global u32 narrow_sum8(in const u32 *p, in u8 count) {
    u32 sum = 0u32;
    for (u8 i = 0u8; i < count; ++i) sum += p[i];
    return sum;
}
[[noinline]] global u16 narrow_find(in const u8 *p, in u16 count, in u8 key) {
    for (u16 i = 0u16; i < count; ++i)
        if (p[i] == key) return i;
    return count;
}

global u32 local_i8(in i8 x) {
    u32 local[8] = {1u32, 2u32, 4u32, 8u32, 16u32, 32u32, 64u32, 128u32};
    const u32 *p = &local[4];
    return p[x];
}
global u32 local_u8(in u8 x) {
    u32 local[8] = {1u32, 2u32, 4u32, 8u32, 16u32, 32u32, 64u32, 128u32};
    u8 i = x;
    ++i;
    return local[i];
}
global u32 local_i64(in i64 x) {
    u32 local[8] = {1u32, 2u32, 4u32, 8u32, 16u32, 32u32, 64u32, 128u32};
    const u32 *p = &local[4];
    return p[x];
}

$::static_assert(local_i8(-1i8) == 8u32, "i8 index");
$::static_assert(local_u8(255u8) == 1u32, "wrapping u8 index");
$::static_assert(local_i64(-4i64) == 1u32, "i64 index");

global u32 test_entry() {
    const u32 *middle = &table8[4];
    const i32 m = minus_one;
    if (read_i8(middle, (i8)m) != 8u32) return 1u32;
    if (read_i16(middle, (i16)(m * 2i32)) != 4u32) return 2u32;
    if (read_i32(middle, m * 3i32) != 2u32) return 3u32;
    if (read_i64(middle, (i64)(m * 4i32)) != 1u32) return 4u32;
    if (read_i64(table8, three) != 8u32) return 5u32;
    if (table_i64(three) != 8u32) return 6u32;
    if (table8[(i64)three + 1i64] != 16u32) return 7u32;
    if (middle[m] != 8u32) return 8u32;
    if (local_i8((i8)m) != 8u32 || local_u8((u8)m) != 1u32) return 9u32;
    if (local_i64((i64)m * 4i64) != 1u32) return 10u32;

    // Elements past a wrapped index differ from those before it.
    for (u32 k = 0u32; k < 0x10010u32; ++k)
        bytes[k] = k < 256u32 ? (u8)k : k < 0x10000u32 ? 1u8 : 0xffu8;
    if (read_u8(bytes, 200u8) != 200u8) return 11u32;
    if (read_u16(bytes, 0x8005u16) != 1u8) return 12u32;
    if (read_u32((const u8 *)((uptr)&bytes[0] - bias), 0x80000007u32) != 7u8)
        return 13u32;
    if (wrap_sum8(257u32) != 32640u32) return 14u32;
    if (wrap_sum8(512u32) != 65280u32) return 15u32;
    if (wrap_sum16(0xfff8u16, 16u32) != 8u32 + 28u32) return 16u32;

    for (u32 k = 0u32; k < 8u32; ++k) cells[k] = k;
    u32 *cell = &cells[4];
    write_i8(cell, (i8)(m * 2i32), 40u32);
    if (cells[2] != 40u32) return 17u32;
    add_i16(cell, (i16)(m * 3i32));
    if (cells[1] != 2u32) return 18u32;
    if (address_i32(middle, m) != &table8[3]) return 19u32;

    if (signed_pairs(table8, 7i32) != 2u32 * 255u32 - 1u32 - 128u32) return 20u32;
    if (unsigned_sum(table8, 8u32) != 255u32) return 21u32;
    unsigned_scale(cells, table8, 8u32);
    for (u32 k = 0u32; k < 8u32; ++k)
        if (cells[k] != table8[k] * 3u32) return 22u32;
    if (unsigned_find(table8, 8u32, 64u32) != 6u32) return 23u32;
    if (narrow_find(bytes, 300u16, 0xffu8) != 255u16) return 24u32;

    // Long enough for the vectorized loops and their scalar remainders.
    u32 expected = 0u32;
    u32 expected16 = 0u32;
    u32 expected8 = 0u32;
    for (u64 k = 0u64; k < 300u64; ++k) {
        wide[k] = (u32)k * 7u32 + 1u32;
        if (k < 299u64) expected += wide[k];
        expected16 += wide[k];
        if (k < 250u64) expected8 += wide[k];
    }
    if (unsigned_sum(wide, 299u32) != expected) return 25u32;
    if (narrow_sum16(wide, 300u16) != expected16) return 26u32;
    if (narrow_sum8(wide, 250u8) != expected8) return 27u32;
    unsigned_scale(scaled, wide, 299u32);
    for (u64 k = 0u64; k < 299u64; ++k)
        if (scaled[k] != wide[k] * 3u32) return 28u32;
    if (scaled[299] != 0u32) return 29u32;
    if (unsigned_find(wide, 300u32, wide[203]) != 203u32) return 30u32;
    return 0u32;
}
