// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Braces around one expression: the initializer of a scalar and a string that
// initializes a u8 array, in static and automatic objects, members, compound
// literals, generic code, and translation-time evaluation.

struct pair { u32 a; u32 b; };
struct named { u8 name[6]; u16 count; const u8 *text; };
struct bits { u32 low : 3; u32 high : 5; u8 tag; };
union word { u32 bits; f32 real; };
enum colour { red = 1, green = 2, blue = 4 };

static u32 twice(in u32 value) { return value * 2u32; }

static T doubled<T>(in T value) {
    T copy = { value };
    T *cell = &(T){ value };
    return copy + *cell;
}

static u32 sum_bytes(in const u8 *text, in uptr count) {
    u32 total = 0u32;
    for (uptr i = 0uptr; i < count; ++i) total += text[i];
    return total;
}

// Static objects.
global u32 scalar = { 5u32 };
global struct pair members = { { 1u32 }, { 2u32 } };
global u8 inferred[] = { "abc" };
global u8 bounded[8] = { "hi" };
global struct named record = { { "abcd" }, { 7u16 }, { "text" } };
global u8 rows[][4] = { { "ab" }, "cd", { "e" } };
global u32 from_literal = (u32){ 9u32 };
global u32 *literal_address = &(u32){ 11u32 };
// A scalar literal of the object's type is its brace list, relocations kept.
global u32 *literal_pointer = (u32 *){ &scalar };
global u32 folded = { 2u32 + 3u32 * 4u32 };
global uptr layout = { sizeof(u64) + 1uptr };
global struct bits fields = { { 5u32 }, { 17u32 }, { 9u8 } };
global union word overlay = { .real = { 1.0f32 } };
global enum colour hue = { blue };
global u32 (*callback)(in u32) = { twice };

// Translation-time evaluation, compared with the same calls at runtime.
static u32 evaluated(in u32 x) {
    u32 y = { x * 2u32 };
    struct pair p = { { y }, 1u32 };
    u8 text[] = { "xyz" };
    struct named n = { { "q" }, { 3u16 }, { text } };
    u32 *cell = &(u32){ x };
    *cell += 1u32;
    return p.a + p.b + text[1] + (u32)sizeof(text) + n.name[0] + n.count + n.text[2] + *cell +
        ((u8[]){ "pq" })[1] + (u32)sizeof((u8[]){ "four" }) + doubled(x);
}

global u32 evaluated_value = evaluated(4u32);

$::static_assert((u32){ 5u32 } == 5u32, "scalar literal");
$::static_assert(sizeof((u8[]){ "abc" }) == 4uptr, "braced string literal size");
$::static_assert(((u8[]){ "abc" })[2] == 'c', "braced string literal value");
$::static_assert(doubled(3u32) == 6u32, "generic braces");

[[noinline]] static u32 automatic(in u32 x) {
    u32 y = { x };
    f64 f = { 2.5 };
    u8 *none = { (u8 *)0 };
    struct pair p = { { x }, { y + 1u32 } };
    u8 text[] = { "hello" };
    u8 padded[10] = { "ab" };
    struct named n = { { "q" }, { 3u16 }, { text } };
    u8 variable[x] = { "vl" };
    struct bits b = { { 6u32 }, { 30u32 }, { 1u8 } };
    union word w = { { 0x3f800000u32 } };
    enum colour c = { green };
    u32 (*fn)(in u32) = { twice };
    i8 narrow = { 300 };
    if (none != (u8 *)0 || f != 2.5 || sizeof(text) != 6uptr || text[4] != 'o') return 0u32;
    if (padded[1] != 'b' || padded[2] != 0u8 || padded[9] != 0u8) return 0u32;
    if (n.name[0] != 'q' || n.name[1] != 0u8 || n.count != 3u16 || n.text[1] != 'e') return 0u32;
    if (variable[1] != 'l' || variable[2] != 0u8) return 0u32;
    if (b.low != 6u32 || b.high != 30u32 || b.tag != 1u8 || w.real != 1.0f32) return 0u32;
    if (c != green || fn(21u32) != 42u32 || narrow != (i8)44) return 0u32;
    return y + p.a + p.b;
}

[[noinline]] static u32 counted(in u32 step) {
    static u32 counter = { 100u32 };
    static u8 text[] = { "static" };
    static struct pair p = { { 4u32 }, 5u32 };
    counter += step;
    return counter + (u32)sizeof(text) + text[5] + p.a + p.b;
}

[[noinline]] static u32 literals(in u32 x) {
    u32 *p = &(u32){ x };
    *p += 1u32;
    u8 *text = (u8[]){ "lit" };
    u8 *padded = (u8[8]){ "ab" };
    return *p + text[2] + padded[1] + padded[7] + (struct pair){ { 6u32 }, 7u32 }.a +
        (u32)sizeof((u8[]){ "four" }) + sum_bytes((u8[]){ "xy" }, 3uptr);
}

[[link_name("braced_initializers_entry")]]
global i32 braced_initializers_entry() {
    if (scalar != 5u32 || members.a != 1u32 || members.b != 2u32) return 2;
    if (sizeof(inferred) != 4uptr || inferred[2] != 'c' || inferred[3] != 0u8) return 3;
    if (bounded[1] != 'i' || bounded[2] != 0u8 || bounded[7] != 0u8) return 4;
    if (record.name[3] != 'd' || record.name[4] != 0u8 || record.count != 7u16 || record.text[2] != 'x')
        return 5;
    if (sizeof(rows) != 12uptr || rows[0][1] != 'b' || rows[1][0] != 'c' || rows[2][0] != 'e' ||
        rows[2][1] != 0u8) return 6;
    if (from_literal != 9u32 || *literal_address != 11u32 || folded != 14u32 || layout != 9uptr ||
        literal_pointer != &scalar) return 7;
    if (fields.low != 5u32 || fields.high != 17u32 || fields.tag != 9u8) return 8;
    if (overlay.bits != 0x3f800000u32 || hue != blue || callback(4u32) != 8u32) return 9;
    if (evaluated_value != $::runtime(evaluated(4u32)) || evaluated_value != 8u32 + 1u32 + 'y' + 4u32 +
        'q' + 3u32 + 'z' + 5u32 + 'q' + 5u32 + 8u32) return 10;
    if (automatic(5u32) != 5u32 + 5u32 + 6u32) return 11;
    if (counted(1u32) != 101u32 + 7u32 + 'c' + 9u32 || counted(2u32) != 103u32 + 7u32 + 'c' + 9u32) return 12;
    if (literals(3u32) != 4u32 + 't' + 'b' + 6u32 + 5u32 + 'x' + 'y') return 13;
    if (doubled(7u64) != 14u64 || $::runtime(doubled(3u32)) != 6u32) return 14;
    *literal_address += 1u32;
    if (*literal_address != 12u32) return 15;
    return 1;
}
