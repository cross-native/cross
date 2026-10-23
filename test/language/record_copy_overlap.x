// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Record assignments during translation-time evaluation that are defined
// although their source and destination share storage: a copy onto the same
// object, and a copy from a literal, a temporary, or a returned record, each
// an object of its own. record_copy_overlap.cmake checks that a copy between
// partially overlapping objects is diagnosed.

struct pair { u32 a; u32 b; };
struct trio { struct pair low; u32 c; };
struct shifted { u32 skip; struct pair high; };
union view { struct trio t; struct shifted s; };

static struct pair low_of(in const union view *v) { return v->t.low; }

static u32 same_object() {
    struct pair cells[2] = { { 1u32, 2u32 }, { 3u32, 4u32 } };
    struct pair *p = &cells[1];
    *p = *p;
    cells[0] = cells[0];
    union view v = { .t = { { 5u32, 6u32 }, 7u32 } };
    v.t.low = v.t.low;
    return cells[0].b * 100u32 + p->a * 10u32 + v.t.low.b;
}
$::static_assert(same_object() == 236u32, "a copy onto the same object");

static u32 literal_source() {
    union view v = { .t = { { 1u32, 2u32 }, 3u32 } };
    v.s.high = (struct pair){ v.t.low };
    return v.s.skip * 100u32 + v.s.high.a * 10u32 + v.s.high.b;
}
$::static_assert(literal_source() == 112u32, "a literal is an object of its own");

static u32 temporary_source() {
    union view v = { .t = { { 1u32, 2u32 }, 3u32 } };
    struct pair held = v.t.low;
    v.s.high = held;
    return v.s.high.a * 10u32 + v.s.high.b;
}
$::static_assert(temporary_source() == 12u32, "a temporary is an object of its own");

static u32 returned_source() {
    union view v = { .t = { { 1u32, 2u32 }, 3u32 } };
    v.s.high = low_of(&v);
    return v.s.high.a * 10u32 + v.s.high.b;
}
$::static_assert(returned_source() == 12u32, "a returned record is a value");
