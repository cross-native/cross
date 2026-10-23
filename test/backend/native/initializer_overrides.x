// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Brace lists that start from a value: a base of the list's own type, an
// aggregate member initialized from a value, and later entries that replace
// parts of either. Static data reads constant objects during translation.

struct entity { i32 hp; i32 x; };

struct receiver [[exhaustive]] {
    i32 (*harm)(struct entity *self, i32 amount);
    void (*push)(struct entity *self, i32 dx);
    i32 weight;
    const u8 *name;
};

static i32 hero_harm(struct entity *self, i32 amount) { self->hp -= amount; return self->hp; }
static i32 partner_harm(struct entity *self, i32 amount) { self->hp -= amount / 2; return self->hp; }
static void hero_push(struct entity *self, i32 dx) { self->x += dx; }

static const struct receiver hero = { .harm = hero_harm, .push = hero_push, .weight = 5, .name = "hero" };
static const struct receiver partner = { hero, .harm = partner_harm, .name = "partner" };
static const struct receiver heavy = { hero, .weight = hero.weight * 2 };

struct motion { i32 dash; i32 jump; i32 fall; };
struct row { i32 weight; struct motion motion; u8 tag; i32 big; };
union cell { i32 i; u32 u; u8 bytes[8]; };
struct holder { union cell value; i32 after; };
struct flags { u32 a : 3; u32 b : 5; u32 c : 7; i32 tail; };
struct pad { u8 a; u32 b; u8 c; };

static const struct row normal = { .weight = 4, .motion = { 3, 2, 1 }, .tag = 7, .big = 100 };
static const struct row hard = { normal, .motion.dash = 6, .big = normal.big + 1 };
static const struct row reset = { normal, .motion = { .jump = 9 } };
static const struct row member = { .motion = normal.motion, .motion.fall = 8 };
static const i32 table[6] = { 1, 2, 3, 4, 5, 6 };
static const i32 table_copy[] = { table, [2] = 30 };
static const struct holder holder = { .value.i = -1, .after = 3 };
static const struct holder holder_member = { holder, .value.bytes[0] = 5 };
static const struct flags flag_base = { .a = 5, .b = 17, .c = 99, .tail = -4 };
static const struct flags flag_copy = { flag_base, .b = 3 };
static const u8 word[8] = { "abc", [1] = 'X' };
struct point { i32 x; i32 y; };
struct segment { struct point from; struct point to; };
struct inner { i32 a; i32 b; };
struct wrapper { struct inner; i32 c; };
static const struct segment literal_part = { .from = (struct point){ 1, 2 }, .from.y = 5 };
static const struct wrapper promoted = { (struct inner){ 1, 2 }, .b = 9, .c = 3 };
static const i32 k = 5;
static const i32 k2 = k + 1;
static i32 bounded[k2];
// An array of the list's own type is a base, not a pointer to its first element.
static i32 slot_a;
static i32 slot_b;
static void *const slots[2] = { &slot_a, &slot_b };
static void *const slots_copy[2] = { slots, [0] = &slot_b };

static i32 counter;
static i32 order[4];

static i32 note(i32 value) {
    order[counter] = value;
    counter += 1;
    return value;
}

static struct row *pick(struct row *p) {
    note(100);
    return p;
}

[[noinline]] static struct row update(const struct row *p, i32 weight) {
    return (struct row){ *p, .weight = weight };
}

global i32 initializer_overrides_entry() {
    struct entity e = { 100, 0 };
    if (partner.harm(&e, 10) != 95 || heavy.harm(&e, 5) != 90) return 2;
    partner.push(&e, 3);
    if (e.x != 3 || partner.weight != 5 || heavy.weight != 10) return 3;
    if (heavy.name != hero.name || partner.name[0] != 'p' || heavy.name[3] != 'o') return 4;
    if (hard.weight != 4 || hard.motion.dash != 6 || hard.motion.jump != 2 || hard.big != 101) return 5;
    if (reset.motion.dash != 0 || reset.motion.jump != 9 || reset.tag != 7) return 6;
    if (member.motion.dash != 3 || member.motion.fall != 8 || member.weight != 0) return 7;
    if (sizeof(table_copy) != sizeof(table) || table_copy[2] != 30 || table_copy[5] != 6) return 8;
    if (holder_member.value.bytes[0] != 5 || holder_member.after != 3) return 9;
    for (uptr i = 1; i < 8; ++i)
        if (holder_member.value.bytes[i] != 0) return 9;
    if (flag_copy.a != 5 || flag_copy.b != 3 || flag_copy.c != 99 || flag_copy.tail != -4) return 10;
    if (word[0] != 'a' || word[1] != 'X' || word[2] != 'c' || word[3] != 0 || word[7] != 0) return 11;
    if (k2 != 6 || sizeof(bounded) != 6 * sizeof(i32)) return 12;
    if (literal_part.from.x != 1 || literal_part.from.y != 5 || literal_part.to.x != 0) return 27;
    if (promoted.a != 1 || promoted.b != 9 || promoted.c != 3) return 28;

    // Automatic objects: the base first and once, then the other entries.
    struct row local = normal;
    counter = 0;
    struct row picked = { *pick(&local), .weight = note(1), .tag = (u8)note(2) };
    if (counter != 3 || order[0] != 100 || order[1] != 1 || order[2] != 2) return 13;
    if (picked.weight != 1 || picked.tag != 2 || picked.motion.fall != 1 || picked.big != 100) return 14;
    struct row path = { local, .motion.jump = 20 };
    if (path.motion.dash != 3 || path.motion.jump != 20 || path.motion.fall != 1) return 15;
    struct row whole = { .motion = local.motion, .motion.dash = 30 };
    if (whole.motion.dash != 30 || whole.motion.jump != 2 || whole.weight != 0) return 16;
    struct row updated = update(&local, 9);
    if (updated.weight != 9 || updated.big != 100 || updated.motion.jump != 2) return 17;
    i32 values[6] = { 6, 5, 4, 3, 2, 1 };
    i32 copied[] = { values, [0] = 60 };
    if (sizeof(copied) != sizeof(values) || copied[0] != 60 || copied[1] != 5 || copied[5] != 1) return 18;
    i32 grid[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
    i32 grid_copy[2][3] = { grid, [1][1] = 50 };
    if (grid_copy[0][2] != 3 || grid_copy[1][1] != 50 || grid_copy[1][2] != 6) return 19;
    struct holder h = { .value.u = 0x01020304u32, .after = 4 };
    struct holder kept = { h, .after = 5 };
    struct holder entered = { h, .value.bytes[1] = 9 };
    if (kept.value.u != 0x01020304u32 || kept.after != 5) return 20;
    if (entered.value.bytes[1] != 9 || entered.after != 4) return 21;
    for (uptr i = 0; i < 8; ++i)
        if (i != 1 && entered.value.bytes[i] != 0) return 21;
    struct flags f = flag_base;
    struct flags g = { f, .c = 1 };
    if (g.a != 5 || g.b != 17 || g.c != 1 || g.tail != -4) return 22;
    u8 raw[sizeof(struct pad)];
    for (uptr i = 0; i < sizeof(raw); ++i) raw[i] = 0xff;
    struct pad *junk = (struct pad *)raw;
    junk->a = 1;
    junk->b = 2;
    junk->c = 3;
    struct pad clean = { *junk, .c = 9 };
    const u8 *bytes = (const u8 *)&clean;
    if (clean.a != 1 || clean.b != 2 || clean.c != 9) return 23;
    for (uptr i = 1; i < $::offsetof(struct pad, b); ++i)
        if (bytes[i] != 0) return 24;
    for (uptr i = $::offsetof(struct pad, c) + 1; i < sizeof(clean); ++i)
        if (bytes[i] != 0) return 25;
    u8 text[6] = { "hey", [2] = '!' };
    if (text[0] != 'h' || text[2] != '!' || text[3] != 0) return 26;
    struct segment segment = { .from = (struct point){ 1, 2 }, .from.y = 8 };
    if (segment.from.x != 1 || segment.from.y != 8 || segment.to.y != 0) return 29;
    struct inner part = { 4, 5 };
    struct wrapper wrapped = { part, .b = 6 };
    if (wrapped.a != 4 || wrapped.b != 6 || wrapped.c != 0) return 30;
    void *pointers[2] = { &slot_a, &slot_b };
    void *pointers_copy[2] = { pointers };
    if (pointers_copy[0] != &slot_a || pointers_copy[1] != &slot_b) return 31;
    if (slots_copy[0] != &slot_b || slots_copy[1] != &slot_b) return 31;
    return 1;
}
