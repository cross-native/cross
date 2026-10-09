// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Objects of typedefs that request alignment: storage, frames above the
// stack alignment, members, arrays, parameter homes, record transport, and
// translation-time evaluation against the emitted layout.

typedef u32 wide_u32 [[aligned(16)]];
typedef u64 wide_u64 [[aligned(32)]];
typedef u8 line_u8 [[aligned(64)]];
typedef u16 page_u16 [[aligned(128)]];

struct holder { u8 first; wide_u32 value; u8 last; };
struct over { u32 a; wide_u32 b; };
struct small8 { line_u8 a; u8 b; };
struct packed_holder [[packed]] { u8 tag; wide_u32 value; };
struct pair { u32 x; u32 y; };
typedef struct pair pair16 [[aligned(16)]];

global wide_u32 initialized = 7u32;
global wide_u32 initialized_table[3] = {1u32, 2u32, 3u32};
global struct holder initialized_holder = {4u8, 5u32, 6u8};
static line_u8 lines[2];
global line_u8 text[3] = "ab";

[[noinline]] static u32 opaque(in u32 value) { return value * 3u32 + 1u32; }

[[noinline]] global u32 aligned_sum(in wide_u32 a, in u32 b, in wide_u32 c) {
    return a * 100u32 + b * 10u32 + c;
}

[[noinline]] static u32 aligned_home(in wide_u32 value) {
    wide_u32 *home = &value;
    return ((uptr)home & 15uptr) == 0uptr && *home == 41u32;
}

[[noinline]] static u32 walk(in wide_u32 *p, in uptr count) {
    u32 total = 0u32;
    for (uptr index = 0uptr; index < count; ++index) total += p[index];
    return total;
}

[[noinline]] global struct over bump_over(in struct over value, in u32 k) {
    struct over result = value;
    result.a += k;
    result.b += k * 2u32;
    return result;
}

[[noinline]] static struct small8 bump_small(in struct small8 value) {
    struct small8 result = value;
    result.a += 1u8;
    result.b += 2u8;
    return result;
}

[[noinline]] global pair16 bump_pair(in pair16 value, in u32 k) {
    pair16 result = value;
    result.x += k;
    result.y += k;
    return result;
}

// Values live across a call in a frame realigned beyond its ABI alignment.
[[noinline]] static u32 pressure(in u32 seed) {
    wide_u64 cell = (u64)seed;
    u32 a = seed + 1u32, b = seed * 3u32, c = seed ^ 5u32, d = seed + 7u32;
    u32 e = seed * 11u32, f = seed + 13u32, g = seed * 17u32, h = seed + 19u32;
    u32 i = seed ^ 23u32, j = seed + 29u32;
    const u32 r = opaque(a + b);
    if (((uptr)&cell & 31uptr) != 0uptr || cell != (u64)seed) return 0u32;
    return a + b + c + d + e + f + g + h + i + j + r;
}

static uptr find_byte(in u8 *bytes, in uptr size, in u8 marker) {
    for (uptr index = 0uptr; index < size; ++index)
        if (bytes[index] == marker) return index;
    return 999uptr;
}

// Member offsets and element strides observed through object bytes.
static uptr layout_digest() {
    struct holder h;
    u8 *raw = (u8 *)(void *)&h;
    for (uptr index = 0uptr; index < sizeof(h); ++index) raw[index] = 0u8;
    h.first = 1u8;
    h.value = 0x02020202u32;
    h.last = 3u8;
    wide_u32 values[3];
    u8 *cells = (u8 *)(void *)&values[0];
    for (uptr index = 0uptr; index < sizeof(values); ++index) cells[index] = 0u8;
    values[1] = 0x05050505u32;
    wide_u32 *p = &values[0];
    *(p + 2) = 0x06060606u32;
    return find_byte(raw, sizeof(h), 2u8) + find_byte(raw, sizeof(h), 3u8) * 100uptr +
           sizeof(h) * 10000uptr + find_byte(cells, sizeof(values), 5u8) * 1000000uptr +
           find_byte(cells, sizeof(values), 6u8) * 100000000uptr;
}

global uptr evaluated_digest = layout_digest();

$::static_assert(sizeof(wide_u32) == 16uptr && $::alignof(wide_u32) == 16uptr, "scalar");
$::static_assert(sizeof(struct holder) == 48uptr && sizeof(struct over) == 32uptr, "records");
$::static_assert(sizeof(struct small8) == 128uptr && sizeof(pair16) == 16uptr, "padding");
$::static_assert(sizeof(struct packed_holder) == 32uptr, "packing keeps a requested alignment");

[[link_name("aligned_typedef_entry")]]
global i32 aligned_typedef_entry() {
    wide_u32 local = 5u32;
    wide_u64 wide = 9u64;
    line_u8 line = 3u8;
    page_u16 page = 11u16;
    wide_u32 table[4];
    struct holder h;
    struct packed_holder packed;
    for (u32 index = 0u32; index < 4u32; ++index) table[index] = index + 1u32;
    h.first = 1u8;
    h.value = 2u32;
    h.last = 3u8;
    packed.tag = 8u8;
    packed.value = 9u32;
    wide_u32 *p = &table[0];
    u32 plain = local;
    wide_u32 back = plain + 1u32;
    u32 *plain_pointer = p;
    if (((uptr)&local & 15uptr) != 0uptr || ((uptr)&wide & 31uptr) != 0uptr) return 2;
    if (((uptr)&line & 63uptr) != 0uptr || ((uptr)&page & 127uptr) != 0uptr) return 3;
    if ((uptr)&table[1] - (uptr)&table[0] != 16uptr || (uptr)(p + 1) - (uptr)p != 16uptr) return 4;
    if (((uptr)&h.value & 15uptr) != 0uptr || (uptr)&h.value - (uptr)&h != 16uptr ||
        (uptr)&h.last - (uptr)&h != 32uptr) return 5;
    if (((uptr)&packed.value & 15uptr) != 0uptr) return 6;
    if (((uptr)&initialized & 15uptr) != 0uptr || ((uptr)&initialized_table[1] & 15uptr) != 0uptr ||
        ((uptr)&initialized_holder & 15uptr) != 0uptr) return 7;
    if (((uptr)&lines[0] & 63uptr) != 0uptr || (uptr)&lines[1] - (uptr)&lines[0] != 64uptr) return 8;
    if (initialized != 7u32 || initialized_table[2] != 3u32 || initialized_holder.value != 5u32 ||
        initialized_holder.last != 6u8) return 9;
    if (walk(p, 4uptr) != 10u32 || walk(&initialized_table[0], 3uptr) != 6u32 ||
        *(plain_pointer + 4) != 2u32) return 10;
    if (aligned_sum(local, back, 3u32) != 563u32 || aligned_home(41u32) != 1u32) return 11;
    struct over o;
    o.a = 20u32;
    o.b = 30u32;
    const struct over r = bump_over(o, 5u32);
    struct small8 s;
    s.a = 40u8;
    s.b = 50u8;
    const struct small8 t = bump_small(s);
    pair16 q;
    q.x = 60u32;
    q.y = 70u32;
    const struct pair u = bump_pair(q, 7u32);
    if (r.a != 25u32 || r.b != 40u32 || t.a != 41u8 || t.b != 52u8 || u.x != 67u32 || u.y != 77u32)
        return 12;
    if (pressure(4u32) != 285u32) return 13;
    if (h.first != 1u8 || h.value != 2u32 || h.last != 3u8 || packed.tag != 8u8 ||
        packed.value != 9u32 || wide != 9u64 || line != 3u8 || page != 11u16) return 14;
    if (evaluated_digest != 3216483216uptr || $::runtime(layout_digest()) != evaluated_digest)
        return 15;
    if (text[0] != 97u8 || text[1] != 98u8 || text[2] != 0u8 ||
        (uptr)&text[1] - (uptr)&text[0] != 64uptr) return 16;
    return 1;
}
