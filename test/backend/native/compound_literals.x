// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Compound literals: block-scope objects initialized at each evaluation and
// living to the end of their block, address-taking, arrays sized by their
// initializer, static file-scope objects, and translation-time evaluation.

struct point { i32 x; i32 y; };
struct segment { struct point from; struct point to; const i32 *weights; };
union bits { f32 f; u32 u; };

static i32 sum(in const i32 *values, in uptr count) {
    i32 total = 0;
    for (uptr i = 0uptr; i < count; ++i) total += values[i];
    return total;
}

static i32 norm1(in struct point p) { return p.x + p.y; }

[[noinline]] static i32 run_sum(in const i32 *values, in uptr count) { return sum(values, count); }

// A literal array passed to a parser, as C code passes one to strtol.
[[noinline]] static i64 parse_hex(in const u8 *s) {
    i64 value = 0;
    for (uptr i = 0uptr; s[i] != 0u8; ++i) {
        const u8 c = s[i];
        value = value * 16 + (c >= 'a' ? c - 'a' + 10 : c - '0');
    }
    return value;
}

[[noinline]] static i64 parse_word(in const u8 *p) {
    return parse_hex((u8[]){ p[0], p[1], p[2], p[3], 0 });
}

// Static objects outside a function body; a literal of the destination's own
// type is that destination's initializer.
global const i32 *table = (const i32[]){ 10, 20, 30 };
global struct point *origin = &(struct point){ 3, 4 };
global struct point corner = (struct point){ .y = 6, .x = 5 };
global struct segment line = {
    .from = (struct point){ 1, 2 },
    .to = { 3, 4 },
    .weights = (const i32[]){ 7, 8, 9 },
};
global const u8 *names[] = { (const u8[]){ 'a', 'b', 0 }, (const u8[]){ 'c', 0 } };
global u32 one_bits = (union bits){ .f = 1.0f32 }.u;
global u32 second = ((u32[]){ 4u32, 5u32, 6u32 })[1];
global uptr literal_size = sizeof((u8[]){ 1, 2, 3, 4, 5 });

static i32 evaluated() {
    i32 *p = (i32[]){ 1, 2, 3 };
    p[1] = 20;
    return sum(p, 3) + norm1((struct point){ 4, 5 }) + ((struct point){ 6, 7 }).y;
}

global i32 evaluated_total = evaluated();

// One object per literal and block activation, at runtime as in evaluation.
static i32 same_literal() {
    i32 *p = (i32 *)0;
    i32 *old = (i32 *)0;
    i32 i = 0;
    while ((p = (i32[]){ i++ }) != (i32 *)0 && i < 2) old = p;
    return p == old && *p == 1 ? 1 : 0;
}

static i32 same_nested() {
    const i32 *w = (const i32 *)0;
    const i32 *old = (const i32 *)0;
    i32 i = 0;
    while ((w = (struct segment){ .weights = (const i32[]){ i } }.weights) != (const i32 *)0 && ++i < 3)
        old = w;
    return w == old && *w == 2 ? 1 : 0;
}

static i32 depth_sum(in i32 n) {
    const i32 *here = (const i32[]){ n };
    return n == 0 ? *here : *here + depth_sum(n - 1);
}

static i32 literal_address() {
    struct point *p = &(struct point){ 4, 5 };
    i32 *y = &((struct point){ 6, 7 }).y;
    return p->x + *y;
}

static i32 parenthesized() {
    struct point local = ((struct point){ 11, 12 });
    return local.x + local.y;
}

global i32 same_folded = same_literal() + same_nested();
global i32 depth_folded = depth_sum(3);
global i32 address_folded = literal_address();
global i32 parenthesized_folded = parenthesized();
global struct point paren_corner = ((struct point){ 7, 8 });
global struct segment paren_line = { .from = ((struct point){ 1, 2 }), .weights = ((const i32[]){ 3 }) };

$::static_assert(sizeof((i32[]){ 1, 2, [5] = 6 }) == 6uptr * sizeof(i32), "inferred bound");
$::static_assert(((u32[]){ 4u32, 5u32, 6u32 })[2] == 6u32, "translation-time literal");
$::static_assert((&(struct point){ 1, 2 })->y == 2, "literal address");

static T first<T>(in const T *values) { return values[0]; }

[[link_name("compound_literals_entry")]]
global i32 compound_literals_entry() {
    if (run_sum((i32[]){ 1, 2, 3, 4 }, 4uptr) != 10) return 2;
    struct point *p = &(struct point){ .y = 2 };
    p->x += 5;
    if (p->x != 5 || p->y != 2 || norm1(*p) != 7) return 3;
    // Each evaluation reinitializes the same object of the enclosing block.
    i32 *saved[3];
    i32 total = 0;
    for (i32 i = 0; i < 3; ++i) {
        i32 *cells = (i32[]){ i, i * 2 };
        cells[0] += 10;
        total += cells[0] + cells[1];
        saved[i] = cells;
    }
    if (total != 39) return 4;
    struct point *previous = (struct point *)0;
    struct point *current = (struct point *)0;
    i32 j = 0;
again:
    previous = current;
    current = &(struct point){ j, 0 };
    j += 1;
    if (j < 2) goto again;
    if (current != previous || current->x != 1) return 5;
    if (((i32[]){ 5, 6, 7 })[2] + (struct point){ 8, 9 }.y != 16) return 6;
    if (parse_word((const u8 *)"1f2a") != 0x1f2a) return 7;
    if (table[2] != 30 || origin->x + origin->y != 7 || corner.x != 5 || corner.y != 6) return 8;
    if (line.from.y != 2 || line.to.x != 3 || line.weights[1] != 8) return 9;
    if (names[0][1] != 'b' || names[1][0] != 'c' || names[1][1] != 0) return 10;
    if (one_bits != 0x3f800000u32 || literal_size != 5uptr || second != 5u32) return 11;
    if (evaluated_total != 40 || $::runtime(evaluated()) != evaluated_total) return 12;
    if (first((u64[]){ 9u64, 8u64 }) != 9u64) return 13;
    const i32 *fixed = (const i32[]){ -1, -2 };
    if (fixed[0] + fixed[1] != -3 || sizeof((u8[]){ 1, 2, 3, 0 }) != 4uptr) return 14;
    origin->y = 40;
    if (origin->y != 40) return 15;
    if (same_folded != 2 || $::runtime(same_literal()) + $::runtime(same_nested()) != 2) return 16;
    if (depth_folded != 6 || $::runtime(depth_sum(3)) != 6) return 17;
    if (address_folded != 11 || $::runtime(literal_address()) != 11) return 18;
    if (parenthesized_folded != 23 || $::runtime(parenthesized()) != 23) return 19;
    if (paren_corner.y != 8 || paren_line.from.x != 1 || paren_line.weights[0] != 3) return 20;
    static struct point kept = ((struct point){ 9, 10 });
    if (kept.x + kept.y != 19) return 21;
    return 1;
}
