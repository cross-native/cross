// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Embedded members: promotion, conversions, casts, and initializers. Each
// check runs at translation time through $::static_assert and at runtime
// through the entry function.

struct entity { i32 x; i32 y; };
typedef struct entity entity_t;
struct tag { u32 kind; };
struct counters { u8 flag; u16 hits : 5; u16 misses : 7; };

struct enemy { struct entity; u32 health; };

// Several embedded members, an anonymous structure and union, and a
// typedef-named embedding.
struct actor {
    u8 flags;
    struct tag;
    entity_t;
    struct { i32 hp; i32 mp; };
    union { u32 bits; f32 ratio; };
    struct counters;
    u32 last;
};

// Depth: `deep` reaches entity through enemy; its own `x` hides the promoted
// one, and `y` is promoted from depth two.
struct deep { u32 x; struct enemy; };
struct deeper { struct deep; struct tag; };

static i32 entity_sum(in const struct entity *e) { return e->x + e->y; }
static struct entity *as_entity(struct enemy *e) { return e; }
static const struct tag *tag_of(const struct actor *a) { return a; }

static u32 check_promotion() {
    struct actor a = {};
    a.kind = 7;
    a.x = 3;
    a.y = 4;
    a.hp = 10;
    a.mp = 20;
    a.bits = 0x3f800000u32;
    a.hits = 9;
    a.misses = 100;
    a.last = 5;
    if (a.tag.kind != 7 || a.entity_t.x != 3 || a.entity_t.y != 4) return 2;
    if (a.hp + a.mp != 30 || a.ratio != 1.0f32) return 3;
    if (a.counters.hits != 9 || a.misses != 100 || a.last != 5) return 4;
    struct actor *p = &a;
    p->hits = p->hits + 1;
    if (p->hits != 10 || p->x * p->y != 12) return 5;
    struct deeper d = {};
    d.x = 1;
    d.deep.enemy.entity.x = 2;
    d.y = 3;
    d.health = 4;
    d.kind = 5;
    if (d.deep.x != 1 || d.deep.enemy.x != 2 || d.deep.enemy.entity.y != 3 ||
        d.deep.health != 4 || d.tag.kind != 5) return 6;
    return 1;
}

static u32 check_conversions() {
    struct enemy foe = { { 5, 6 }, 100 };
    struct entity *e = &foe;            // initialization, offset zero
    if (e->x != 5 || entity_sum(&foe) != 11) return 2;
    struct actor a = {};
    a.x = 8;
    a.y = 9;
    a.kind = 12;
    entity_t *t = &a;                    // nonzero offset
    if (t->x != 8 || t->y != 9 || (u8 *)(void *)t != (u8 *)(void *)&a + 8) return 3;
    const struct actor *c = &a;
    if (tag_of(c)->kind != 12) return 4; // argument and return
    struct actor *none = 0;
    struct entity *still_none = none;
    if (still_none != 0 || tag_of(none) != 0) return 5;
    struct deeper d = {};
    d.y = 21;
    struct entity *inner = &d;           // depth two
    if (inner->y != 21 || (u8 *)(void *)inner != (u8 *)(void *)&d + 4) return 6;
    e = as_entity(&foe);
    e->y = 60;
    if (foe.y != 60) return 7;
    return 1;
}

static u32 check_casts() {
    struct actor a = {};
    entity_t *t = &a;
    struct actor *back = (struct actor *)t;
    if (back != &a) return 2;
    struct tag *k = &a;
    if ((struct actor *)k != &a) return 3;
    entity_t *none = 0;
    if ((struct actor *)none != 0) return 4;
    struct deeper d = {};
    struct entity *inner = &d;
    struct deeper *outer = (struct deeper *)inner;
    if (outer != &d) return 5;
    struct enemy *middle = (struct enemy *)inner;
    if ((u8 *)(void *)middle != (u8 *)(void *)&d.deep.enemy) return 6;
    return 1;
}

static u32 check_initializers() {
    struct actor a = { 1, { 2 }, { 3, 4 }, { 5, 6 }, { 7 }, { 8, 9, 10 }, 11 };
    if (a.flags != 1 || a.kind != 2 || a.x != 3 || a.y != 4 || a.hp != 5 || a.mp != 6 ||
        a.bits != 7 || a.flag != 8 || a.hits != 9 || a.misses != 10 || a.last != 11) return 2;
    struct actor b = { .hp = 5, .kind = 2, .entity_t = { .x = 3 }, .last = 3 };
    if (b.flags != 0 || b.kind != 2 || b.x != 3 || b.y != 0 || b.hp != 5 || b.last != 3) return 3;
    struct enemy c = { .y = 2, .health = 7 };
    if (c.x != 0 || c.y != 2 || c.health != 7) return 4;
    struct deeper d = { .y = 6, .kind = 8 };
    if (d.y != 6 || d.kind != 8 || d.x != 0) return 5;
    struct actor copy = a;
    copy.x = 30;
    if (copy.hp != 5 || a.x != 3 || copy.x != 30) return 6;
    return 1;
}

// Records local to each instance of a generic function embed like others.
static T generic_pick<T>(in T a, in T b) {
    struct inner { T first; };
    struct outer { u8 tag; struct inner; T second; };
    struct outer o = { 1, { a }, b };
    struct inner *p = &o;
    struct outer *back = (struct outer *)p;
    return p->first + back->second + (T)o.tag;
}

static u32 check_generic() {
    if (generic_pick<u16>(3, 4) != 8 || generic_pick<i64>(-10, 2) != -7) return 2;
    return 1;
}

$::static_assert($::offsetof(struct actor, kind) == 4, "promoted offset");
$::static_assert($::offsetof(struct actor, entity_t) == 8, "embedded member offset");
$::static_assert($::offsetof(struct actor, y) == 12, "promoted through typedef name");
$::static_assert($::offsetof(struct actor, mp) == 20, "anonymous structure member");
$::static_assert($::offsetof(struct actor, ratio) == 24, "anonymous union member");
$::static_assert($::offsetof(struct deeper, y) == 8, "depth two");
$::static_assert($::offsetof(struct deeper, deep.enemy.y) == 8, "explicit path");
$::static_assert(sizeof(struct actor) == 36, "layout of ordinary members");

$::static_assert(check_promotion() == 1, "promotion at translation time");
$::static_assert(check_conversions() == 1, "conversions at translation time");
$::static_assert(check_casts() == 1, "casts at translation time");
$::static_assert(check_initializers() == 1, "initializers at translation time");
$::static_assert(check_generic() == 1, "generic instances at translation time");

global struct actor table = { .x = 1, .y = 2, .kind = 3 };
global struct entity *table_entity = &table;
global struct tag *table_tag = &table;
global struct actor *table_back = (struct actor *)&table.entity_t;

global u32 embedded_members_entry() {
    u32 result = $::runtime(check_promotion());
    if (result != 1) return 10 + result;
    result = $::runtime(check_conversions());
    if (result != 1) return 20 + result;
    result = $::runtime(check_casts());
    if (result != 1) return 30 + result;
    result = $::runtime(check_initializers());
    if (result != 1) return 40 + result;
    result = $::runtime(check_generic());
    if (result != 1) return 60 + result;
    if (table_entity->y != 2 || table_tag->kind != 3 || table_back != &table) return 50;
    if ((u8 *)(void *)table_entity != (u8 *)(void *)&table + 8) return 51;
    return 1;
}
