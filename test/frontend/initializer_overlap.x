// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// An initializer entry may not initialize an object, or part of one, that an
// earlier entry of the same initializer initialized. Each error case is
// diagnosed at the later entry in every context: runtime and static
// initializers, translation-time evaluation, and code that never runs.

struct entity { i32 x; i32 y; };
struct actor { u8 flags; struct entity e; u32 last; };
union cell { struct entity pair; i64 whole; };
struct holder { u8 tag; union cell value; };
struct mixed { u8 tag; union { i32 i; f32 f; }; };
struct wrapper { struct entity; u32 extra; };

#if defined(CONTAINER_AFTER_MEMBER)
global i32 entry() {
    struct actor b = { .e.y = 4, .e = { .x = 3 } };
    return b.e.y;
}
#elif defined(MEMBER_AFTER_CONTAINER)
global i32 entry() {
    struct actor b = { .e = { .x = 3 }, .e.y = 4 };
    return b.e.y;
}
#elif defined(STATIC)
global struct actor b = { .e.y = 4, .e = { .x = 3 } };
#elif defined(EVALUATED)
static i32 make() {
    struct actor b = { .e.y = 4, .e = { .x = 3 } };
    return b.e.y;
}
global i32 made = make();
#elif defined(UNCALLED)
static i32 unused() {
    struct actor b = { .e.y = 4, .e = { .x = 3 } };
    return b.e.y;
}
#elif defined(UNTAKEN)
global i32 entry(in i32 v) {
    if (v == 12345) {
        struct actor b = { .e.y = 4, .e = { .x = 3 } };
        return b.e.y;
    }
    return 0;
}
#elif defined(PROMOTED)
global i32 entry() {
    struct wrapper w = { .y = 4, .entity = { .x = 3 } };
    return w.y;
}
#elif defined(ARRAY)
global i32 entry() {
    struct entity points[2] = { [1] = { 1, 2 }, [1].y = 5 };
    return points[1].y;
}
#elif defined(UNION)
global i64 entry() {
    struct holder h = { .value.pair.x = 1, .value.whole = 2 };
    return h.value.whole;
}
#elif defined(ANONYMOUS_UNION)
global i32 entry() {
    struct mixed m = { .i = 1, .f = 2.0f32 };
    return m.i;
}
#elif defined(POSITIONAL)
global i32 entry() {
    struct actor b = { .e.y = 4, .flags = 1u8, { 1, 2 } };
    return b.e.y;
}
#elif defined(NESTED_DUPLICATE)
struct pair { struct actor a; struct actor b; };
global i32 entry() {
    struct pair p = { .a = { .e = { .x = 1, .y = 2, .x = 3 } }, .b.e.x = 4 };
    return p.a.e.x;
}
#elif defined(NESTED_OVERLAP)
struct pair { struct actor a; struct actor b; };
global i32 entry() {
    struct pair p = { .a = { .e = { .x = 1 } }, .b = { .e.y = 2 }, .a.flags = 3u8 };
    return p.a.e.y;
}
#else
// Distinct destinations, including members of one union member, are valid.
static struct actor nested = { .e = { .x = 3, .y = 4 }, .last = 5u32 };
static struct actor members = { .e.x = 6, .e.y = 7, .flags = 8u8 };
static struct holder chosen = { .value.pair.x = 9, .value.pair.y = 10 };

static i32 sum(in struct actor b) { return (i32)b.flags + b.e.x + b.e.y + (i32)b.last; }

static i32 evaluated() {
    struct entity points[3] = { [2] = { 11, 12 }, [0].y = 13, [1] = { .x = 14 } };
    struct wrapper w = { .x = 15, .y = 16, .extra = 17u32 };
    return points[0].y + points[1].x + points[2].x + points[2].y + w.x + w.y + (i32)w.extra;
}

global i32 folded = evaluated();

global i32 test_entry() {
    struct actor local = { .e = { .y = 2 }, .flags = 1u8, .last = 3u32 };
    struct holder runtime_cell = { .tag = 1u8, .value.pair = { .y = 20 } };
    if (sum(nested) != 12 || sum(members) != 21 || sum(local) != 6) return 1;
    if (chosen.value.pair.x != 9 || chosen.value.pair.y != 10) return 2;
    if (runtime_cell.value.pair.x != 0 || runtime_cell.value.pair.y != 20) return 3;
    if (folded != 98 || $::runtime(evaluated()) != 98) return 4;
    return 0;
}
#endif
