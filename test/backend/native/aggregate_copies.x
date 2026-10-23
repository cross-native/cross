// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Record copies and initializers: assignment between objects of sizes and
// alignments that split into different units, copies onto the same object,
// records that a call returns or that a temporary or a literal holds copied
// over storage they overlap, volatile records, literals that read their
// destination, derived initializers, zero fill around partial entries and
// padding, bit-fields, packed records, and arrays of records. Every function
// that copies is [[noinline]], so the copies run in it.
// aggregate_copies_entry returns 1 when every check holds, and otherwise the
// number of the first that fails.

struct b1 { u8 a; };
struct b2 { u16 a; };
struct b3 { u8 a; u8 b; u8 c; };
struct b4 { u32 a; };
struct b7 { u8 a[7]; };
struct b8 { u32 a; u32 b; };
struct b12 { u32 a; u16 b; u8 c; u32 d; };
struct b16 { u64 a; u64 b; };
struct b24 { u32 a[6]; };
struct b33 { u8 a[33]; };
struct big { u32 v[64]; };
struct tight [[packed]] { u8 tag; u32 value; u16 half; };
struct flags { u32 lo : 3; u32 mid : 12; i32 neg : 5; u8 tail; };
struct motion { i32 walk; i32 dash; i32 jump; i32 fall; };
struct row { struct motion mot; i32 harm; i32 heal; u8 name[8]; };

global u8 pattern[512];

[[noinline]] static void fill(in u8 *bytes, in uptr count, in u8 seed) {
    for (uptr i = 0uptr; i < count; ++i) bytes[i] = (u8)(seed + i * 7u8);
}

[[noinline]] static bool same(in const u8 *left, in const u8 *right, in uptr count) {
    for (uptr i = 0uptr; i < count; ++i)
        if (left[i] != right[i]) return (bool)0;
    return (bool)1;
}

[[noinline]] static void copy1(in struct b1 *to, in const struct b1 *from) { *to = *from; }
[[noinline]] static void copy2(in struct b2 *to, in const struct b2 *from) { *to = *from; }
[[noinline]] static void copy3(in struct b3 *to, in const struct b3 *from) { *to = *from; }
[[noinline]] static void copy4(in struct b4 *to, in const struct b4 *from) { *to = *from; }
[[noinline]] static void copy7(in struct b7 *to, in const struct b7 *from) { *to = *from; }
[[noinline]] static void copy8(in struct b8 *to, in const struct b8 *from) { *to = *from; }
[[noinline]] static void copy12(in struct b12 *to, in const struct b12 *from) { *to = *from; }
[[noinline]] static void copy16(in struct b16 *to, in const struct b16 *from) { *to = *from; }
[[noinline]] static void copy24(in struct b24 *to, in const struct b24 *from) { *to = *from; }
[[noinline]] static void copy33(in struct b33 *to, in const struct b33 *from) { *to = *from; }
[[noinline]] static void copy_big(in struct big *to, in const struct big *from) { *to = *from; }
[[noinline]] static void copy_tight(in struct tight *to, in const struct tight *from) { *to = *from; }
[[noinline]] static void copy_flags(in struct flags *to, in const struct flags *from) { *to = *from; }
[[noinline]] static void copy_row(in struct row *to, in const struct row *from) { *to = *from; }

// Copies a record through one each of its sizes and checks every byte.
static i32 sizes() {
    u8 source[64] [[aligned(16)]];
    u8 target[64] [[aligned(16)]];
    fill(source, 64uptr, 3u8);
    fill(target, 64uptr, 100u8);
    copy1((struct b1 *)target, (const struct b1 *)source);
    if (!same(target, source, 1uptr) || target[1] != 107u8) return 20;
    copy2((struct b2 *)target, (const struct b2 *)source);
    if (!same(target, source, 2uptr) || target[2] != 114u8) return 21;
    copy3((struct b3 *)target, (const struct b3 *)source);
    if (!same(target, source, 3uptr) || target[3] != 121u8) return 22;
    copy4((struct b4 *)target, (const struct b4 *)source);
    if (!same(target, source, 4uptr) || target[4] != 128u8) return 23;
    copy7((struct b7 *)(target + 1), (const struct b7 *)(source + 1));
    if (!same(target, source, 8uptr) || target[8] != 156u8) return 24;
    copy8((struct b8 *)target, (const struct b8 *)source);
    if (!same(target, source, 8uptr)) return 25;
    copy12((struct b12 *)target, (const struct b12 *)source);
    if (!same(target, source, 12uptr) || target[12] != 184u8) return 26;
    copy16((struct b16 *)target, (const struct b16 *)source);
    if (!same(target, source, 16uptr) || target[16] != 212u8) return 27;
    copy24((struct b24 *)(target + 4), (const struct b24 *)(source + 4));
    if (!same(target, source, 28uptr) || target[28] != (u8)(100u8 + 28u8 * 7u8)) return 28;
    copy33((struct b33 *)(target + 3), (const struct b33 *)(source + 3));
    if (!same(target, source, 36uptr) || target[36] != (u8)(100u8 + 36u8 * 7u8)) return 29;
    return 0;
}

global struct big big_a;
global struct big big_b;

[[noinline]] static void copy_globals() { big_b = big_a; }

// Copies that a register budget does not cover, between distinct objects
// and through pointers that may overlap.
static i32 large() {
    for (u32 i = 0u32; i < 64u32; ++i) big_a.v[i] = i * 3u32 + 1u32;
    copy_globals();
    for (u32 i = 0u32; i < 64u32; ++i)
        if (big_b.v[i] != i * 3u32 + 1u32) return 30;
    struct big local;
    copy_big(&local, &big_a);
    for (u32 i = 0u32; i < 64u32; ++i)
        if (local.v[i] != big_a.v[i]) return 31;
    return 0;
}

global u32 words[80];

[[noinline]] static void self24(in struct b24 *p) { *p = *p; }

static struct b24 get24(in const struct b24 *from) { return *from; }
static struct big get_big(in const struct big *from) { return *from; }

[[noinline]] static void returned24(in struct b24 *to, in const struct b24 *from) { *to = get24(from); }
[[noinline]] static void returned_big(in struct big *to, in const struct big *from) { *to = get_big(from); }
[[noinline]] static void held_big(in struct big *to, in const struct big *from) {
    struct big held = *from;
    *to = held;
}
[[noinline]] static void literal_big(in struct big *to, in const struct big *from) {
    *to = (struct big){ *from };
}

// A copy onto the same object keeps it. A record that a call returns, or
// that a temporary or a literal holds, is an object of its own, which may
// be copied over storage it overlaps.
static i32 overlap() {
    for (u32 i = 0u32; i < 16u32; ++i) words[i] = i + 1u32;
    self24((struct b24 *)&words[5]);
    if (words[5] != 6u32 || words[10] != 11u32) return 40;
    for (u32 i = 0u32; i < 80u32; ++i) words[i] = i * 5u32;
    copy_big((struct big *)&words[4], (const struct big *)&words[4]);
    for (u32 i = 0u32; i < 80u32; ++i)
        if (words[i] != i * 5u32) return 41;
    returned24((struct b24 *)&words[2], (const struct b24 *)&words[0]);
    for (u32 i = 0u32; i < 6u32; ++i)
        if (words[2u32 + i] != i * 5u32) return 42;
    for (u32 i = 0u32; i < 80u32; ++i) words[i] = i * 5u32;
    returned_big((struct big *)&words[1], (const struct big *)&words[0]);
    for (u32 i = 0u32; i < 64u32; ++i)
        if (words[1u32 + i] != i * 5u32) return 43;
    for (u32 i = 0u32; i < 80u32; ++i) words[i] = i * 5u32;
    held_big((struct big *)&words[9], (const struct big *)&words[0]);
    for (u32 i = 0u32; i < 64u32; ++i)
        if (words[9u32 + i] != i * 5u32) return 44;
    for (u32 i = 0u32; i < 80u32; ++i) words[i] = i * 5u32;
    literal_big((struct big *)&words[3], (const struct big *)&words[0]);
    for (u32 i = 0u32; i < 64u32; ++i)
        if (words[3u32 + i] != i * 5u32) return 45;
    return 0;
}

global volatile struct b8 device;

[[noinline]] static void store_device(in const struct b8 *from) { device = *from; }
[[noinline]] static void load_device(in struct b8 *to) { *to = device; }

static i32 volatiles() {
    struct b8 value = { 0x11223344u32, 0x55667788u32 };
    store_device(&value);
    struct b8 back = { 0u32, 0u32 };
    load_device(&back);
    return back.a == 0x11223344u32 && back.b == 0x55667788u32 ? 0 : 50;
}

[[noinline]] static void swap_halves(in struct b8 *p) { *p = (struct b8){ .a = p->b, .b = p->a }; }
[[noinline]] static void update_harm(in struct row *r, in i32 harm) {
    *r = (struct row){ *r, .harm = harm };
}
[[noinline]] static void with_dash(in struct row *to, in const struct row *from, in i32 dash) {
    *to = (struct row){ *from, .mot.dash = dash };
}
[[noinline]] static i32 literal_local(in i32 x) {
    struct b8 local = { 9u32, 9u32 };
    local = (struct b8){ (u32)x, local.a };
    return (i32)(local.a * 100u32 + local.b);
}

// Literals whose entries read the destination see its old value.
static i32 literals() {
    struct b8 pair = { 1u32, 2u32 };
    swap_halves(&pair);
    if (pair.a != 2u32 || pair.b != 1u32) return 60;
    struct row r = { { 1, 2, 3, 4 }, 5, 6, { 'a', 'b', 'c', 0 } };
    update_harm(&r, 50);
    if (r.mot.walk != 1 || r.mot.dash != 2 || r.harm != 50 || r.heal != 6 || r.name[2] != 'c')
        return 61;
    struct row other;
    with_dash(&other, &r, 77);
    if (other.mot.dash != 77 || other.mot.fall != 4 || other.harm != 50 || other.name[1] != 'b')
        return 62;
    with_dash(&r, &r, 88);
    if (r.mot.dash != 88 || r.mot.jump != 3 || r.heal != 6) return 63;
    if (literal_local(7) != 709) return 64;
    return 0;
}

[[noinline]] static void derive(in struct row *to, in const struct row *from, in i32 heal) {
    struct row x = { *from, .heal = heal, .name = { 'z' } };
    *to = x;
}

[[noinline]] static void bytes_of(in u8 *to, in const u8 *from, in uptr count) {
    for (uptr i = 0uptr; i < count; ++i) to[i] = from[i];
}

[[noinline]] static void partial(in u8 *sink, in u16 b) {
    struct b12 x = { .b = b };
    bytes_of(sink, (const u8 *)&x, sizeof(x));
}

[[noinline]] static void zero_tail(in u8 *sink, in u32 a) {
    struct b33 x = { { (u8)a, 1u8, 2u8, [20] = 3u8 } };
    bytes_of(sink, (const u8 *)&x, sizeof(x));
}

[[noinline]] static void large_zero(in u32 *sink, in u32 value) {
    u32 x[64] = { [17] = value };
    for (u32 i = 0u32; i < 64u32; ++i) sink[i] = x[i];
}

[[noinline]] static void string_tail(in u8 *sink) {
    u8 name[24] = "abc";
    bytes_of(sink, name, 24uptr);
}

// Initializers zero what no entry writes, padding included.
static i32 initializers() {
    struct row r = { { 1, 2, 3, 4 }, 5, 6, { 'a', 'b', 0 } };
    struct row d;
    derive(&d, &r, 66);
    if (d.mot.walk != 1 || d.mot.fall != 4 || d.harm != 5 || d.heal != 66 ||
        d.name[0] != 'z' || d.name[1] != 0) return 70;
    u8 sink[64] [[aligned(16)]];
    fill(sink, 64uptr, 9u8);
    partial(sink, 0x1234u16);
    // The padding byte at 7 and the members that no entry names are zero.
    for (uptr i = 0uptr; i < 12uptr; ++i)
        if ((i < 4uptr || i >= 6uptr) && sink[i] != 0u8) return 71;
    if (*(const u16 *)(sink + 4) != 0x1234u16) return 72;
    if (sink[12] != (u8)(9u8 + 12u8 * 7u8)) return 73;
    fill(sink, 64uptr, 9u8);
    zero_tail(sink, 0x45u32);
    if (sink[0] != 0x45u8 || sink[1] != 1u8 || sink[2] != 2u8 || sink[3] != 0u8 ||
        sink[19] != 0u8 || sink[20] != 3u8 || sink[21] != 0u8 || sink[32] != 0u8 ||
        sink[33] != (u8)(9u8 + 33u8 * 7u8))
        return 74;
    u32 values[64];
    large_zero(values, 12u32);
    for (u32 i = 0u32; i < 64u32; ++i)
        if (values[i] != (i == 17u32 ? 12u32 : 0u32)) return 75;
    fill(sink, 64uptr, 9u8);
    string_tail(sink);
    if (sink[0] != 'a' || sink[2] != 'c' || sink[3] != 0u8 || sink[23] != 0u8 ||
        sink[24] != (u8)(9u8 + 24u8 * 7u8))
        return 76;
    return 0;
}

[[noinline]] static void make_flags(in struct flags *to, in u32 mid) {
    *to = (struct flags){ .lo = 5u32, .mid = mid, .neg = -3, .tail = 7u8 };
}

[[noinline]] static void make_tight(in struct tight *to, in u32 value) {
    struct tight t = { 1u8, value, 0xabcdu16 };
    *to = t;
}

static i32 layouts() {
    struct flags f;
    make_flags(&f, 0x123u32);
    struct flags g;
    copy_flags(&g, &f);
    if (g.lo != 5u32 || g.mid != 0x123u32 || g.neg != -3 || g.tail != 7u8) return 80;
    struct tight t;
    make_tight(&t, 0x12345678u32);
    u8 buffer[16] [[aligned(16)]];
    fill(buffer, 16uptr, 1u8);
    copy_tight((struct tight *)(buffer + 1), &t);
    if (buffer[0] != 1u8 || buffer[1] != 1u8 || buffer[8] != (u8)(1u8 + 8u8 * 7u8)) return 81;
    const struct tight *u = (const struct tight *)(buffer + 1);
    if (u->value != 0x12345678u32 || u->half != 0xabcdu16 || u->tag != 1u8) return 82;
    return 0;
}

global struct b12 table[6];

[[noinline]] static void move_element(in uptr to, in uptr from) { table[to] = table[from]; }
[[noinline]] static void reset_element(in uptr at, in u32 a) { table[at] = (struct b12){ a, 2u16, 3u8 }; }

static i32 arrays() {
    for (u32 i = 0u32; i < 6u32; ++i) table[i] = (struct b12){ i, (u16)(i + 10u32), (u8)(i + 20u32) };
    move_element(4uptr, 1uptr);
    if (table[4].a != 1u32 || table[4].b != 11u16 || table[4].c != 21u8 || table[5].a != 5u32) return 90;
    move_element(2uptr, 2uptr);
    if (table[2].b != 12u16) return 91;
    reset_element(0uptr, 77u32);
    if (table[0].a != 77u32 || table[0].b != 2u16 || table[0].c != 3u8 || table[1].a != 1u32) return 92;
    struct row rows[3];
    for (i32 i = 0; i < 3; ++i) rows[i] = (struct row){ .harm = i, .name = { 'r', (u8)('0' + i) } };
    copy_row(&rows[0], &rows[2]);
    if (rows[0].harm != 2 || rows[0].name[1] != '2' || rows[1].name[1] != '1') return 93;
    return 0;
}

static i32 checks(in bool packed) {
    i32 status = sizes();
    if (status == 0) status = large();
    if (status == 0) status = overlap();
    if (status == 0) status = volatiles();
    if (status == 0) status = literals();
    if (status == 0) status = initializers();
    if (status == 0 && packed) status = layouts();
    if (status == 0) status = arrays();
    return status == 0 ? 1 : status;
}

[[link_name("aggregate_copies_entry")]]
global i32 aggregate_copies_entry() { return checks((bool)1); }

// The checks without packed records, whose misaligned members fault on
// machines that trap misaligned accesses.
[[link_name("aggregate_copies_aligned_entry")]]
global i32 aggregate_copies_aligned_entry() { return checks((bool)0); }
