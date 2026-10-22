// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Typedefs whose `aligned` request lowers the alignment: layout, objects and
// members at any offset, accesses through pointers at every misalignment in
// both byte orders, record copies, and translation-time evaluation.

typedef u32 u32_any [[aligned(1), may_alias]];
typedef u16 u16_any [[aligned(1), may_alias]];
typedef u64 u64_any [[aligned(1), may_alias]];
typedef i32 i32_half [[aligned(2), may_alias]];

struct word { u8 tag; u32_any value; };
struct pair { u32 x; u32 y; };
typedef struct pair pair_any [[aligned(1), may_alias]];
struct framed { u8 tag; pair_any pair; u8 tail; };

$::static_assert(sizeof(u32_any) == 4uptr && $::alignof(u32_any) == 1uptr, "lowered scalar");
$::static_assert($::alignof(i32_half) == 2uptr && $::alignof(u64_any) == 1uptr, "requests");
$::static_assert(sizeof(struct word) == 5uptr && $::offsetof(struct word, value) == 1uptr, "member");
$::static_assert(sizeof(u32_any[3]) == 12uptr && $::alignof(u32_any[3]) == 1uptr, "array");
$::static_assert(sizeof(struct framed) == 10uptr && $::offsetof(struct framed, tail) == 9uptr,
                 "lowered record member");

static u8 bytes[40];
global struct word global_word = {7u8, 0x11223344u32};
// An object of a lowered type may follow a byte at any address.
global u8 lead_byte = 1u8;
global u32_any after_byte = 0x55667788u32;

[[noinline]] static void store32(in u8 *p, in u32 v) { *(u32_any *)p = v; }
[[noinline]] static u32 load32(in const u8 *p) { return *(const u32_any *)p; }
[[noinline]] static void store16(in u8 *p, in u16 v) { *(u16_any *)p = v; }
[[noinline]] static u16 load16(in const u8 *p) { return *(const u16_any *)p; }
[[noinline]] static void store64(in u8 *p, in u64 v) { *(u64_any *)p = v; }
[[noinline]] static u64 load64(in const u8 *p) { return *(const u64_any *)p; }
[[noinline]] static void store_half(in u8 *p, in i32 v) { *(i32_half *)p = v; }
[[noinline]] static i32 load_half(in const u8 *p) { return *(const i32_half *)p; }

[[noinline]] static u32 sum_words(in const u32_any *p, in uptr n) {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < n; ++i) total += p[i];
    return total;
}

[[noinline]] static void set_pair(in pair_any *p, in u32 x, in u32 y) {
    p->x = x;
    p->y = y;
}

[[noinline]] static u32 pair_total(in const pair_any *p) { return p->x * 10u32 + p->y; }

[[noinline]] static struct pair copy_pair(in const pair_any *p) {
    pair_any copy = *p;
    return copy;
}

static u32 little_endian() {
    u16 probe = 1u16;
    return *(u8 *)&probe;
}

static void clear() {
    for (uptr i = 0uptr; i < sizeof(bytes); ++i) bytes[i] = 0u8;
}

// Whether `p` holds the bytes of `value` in memory order.
static u32 stored(in const u8 *p, in u64 value, in uptr size) {
    for (uptr i = 0uptr; i < size; ++i) {
        const uptr shift = little_endian() != 0u32 ? i : size - 1uptr - i;
        if (p[i] != (u8)(value >> (shift * 8uptr))) return 0u32;
    }
    return 1u32;
}

static u32 evaluated_load() {
    u8 local[8] = {0u8, 1u8, 2u8, 3u8, 4u8, 5u8, 6u8, 7u8};
    return *(u32_any *)&local[1];
}

global u32 evaluated_word = evaluated_load();

[[link_name("underaligned_typedef_entry")]]
global i32 underaligned_typedef_entry() {
    for (uptr offset = 0uptr; offset < 8uptr; ++offset) {
        clear();
        store32(&bytes[offset + 1uptr], 0x01020304u32);
        if (!stored(&bytes[offset + 1uptr], 0x01020304u64, 4uptr) || bytes[offset] != 0u8 ||
            bytes[offset + 5uptr] != 0u8) return 2;
        if (load32(&bytes[offset + 1uptr]) != 0x01020304u32) return 3;
        clear();
        store16(&bytes[offset + 1uptr], 0xa1b2u16);
        if (!stored(&bytes[offset + 1uptr], 0xa1b2u64, 2uptr) || bytes[offset + 3uptr] != 0u8 ||
            load16(&bytes[offset + 1uptr]) != 0xa1b2u16) return 4;
        clear();
        store64(&bytes[offset + 1uptr], 0x0102030405060708u64);
        if (!stored(&bytes[offset + 1uptr], 0x0102030405060708u64, 8uptr) ||
            bytes[offset + 9uptr] != 0u8 || load64(&bytes[offset + 1uptr]) != 0x0102030405060708u64)
            return 5;
    }
    clear();
    store_half(&bytes[2], -5);
    if (load_half(&bytes[2]) != -5 || bytes[1] != 0u8 || bytes[6] != 0u8) return 6;
    clear();
    for (uptr i = 0uptr; i < 3uptr; ++i) store32(&bytes[3uptr + 4uptr * i], (u32)i + 1u32);
    if (sum_words((const u32_any *)&bytes[3], 3uptr) != 6u32) return 7;
    clear();
    set_pair((pair_any *)&bytes[5], 3u32, 4u32);
    const struct pair copied = copy_pair((const pair_any *)&bytes[5]);
    if (pair_total((const pair_any *)&bytes[5]) != 34u32 || copied.x != 3u32 || copied.y != 4u32 ||
        !stored(&bytes[5], 3u64, 4uptr) || bytes[4] != 0u8 || bytes[13] != 0u8) return 8;
    struct word w;
    w.tag = 9u8;
    w.value = 0x0a0b0c0du32;
    u32_any *member = &w.value;
    *member += 1u32;
    if (w.value != 0x0a0b0c0eu32 || w.tag != 9u8 || ((uptr)member - (uptr)&w) != 1uptr) return 9;
    struct framed f;
    f.tag = 1u8;
    f.pair.x = 5u32;
    f.pair.y = 6u32;
    f.tail = 2u8;
    struct framed g = f;
    g.pair.y += 1u32;
    if (g.tag != 1u8 || g.pair.x != 5u32 || g.pair.y != 7u32 || g.tail != 2u8 || f.pair.y != 6u32)
        return 10;
    global_word.value ^= 0xffu32;
    if (global_word.value != 0x112233bbu32 || global_word.tag != 7u8) return 11;
    after_byte += 1u32;
    if (after_byte != 0x55667789u32 || lead_byte != 1u8) return 14;
    u32_any local = 41u32;
    local += 1u32;
    if (local != 42u32) return 12;
    const u32 expected = little_endian() != 0u32 ? 0x04030201u32 : 0x01020304u32;
    if (evaluated_word != expected || $::runtime(evaluated_load()) != expected) return 13;
    return 1;
}
