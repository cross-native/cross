// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Static data that starts from values: derived interface records, a nested
// path, arrays, unions, bit-fields, a u8 string, and copied addresses.

struct entity { i32 hp; i32 x; };
struct receiver {
    i32 (*harm)(struct entity *self, i32 amount);
    void (*push)(struct entity *self, i32 dx);
    i32 weight;
    const u8 *name;
};
static i32 hero_harm(struct entity *self, i32 amount) { return self->hp -= amount; }
static i32 partner_harm(struct entity *self, i32 amount) { return self->hp -= amount / 2; }
static void hero_push(struct entity *self, i32 dx) { self->x += dx; }
global const struct receiver hero = { .harm = hero_harm, .push = hero_push, .weight = 5, .name = "hero" };
global const struct receiver partner = { hero, .harm = partner_harm };
global const struct receiver heavy = { hero, .weight = hero.weight * 2, .push = hero.push };
struct motion { i32 dash; i32 jump; };
struct row { i16 weight; struct motion motion; };
global const struct row normal = { .weight = 4, .motion = { 3, 2 } };
global const struct row hard = { normal, .motion.dash = 0x66 };
global const struct row reset = { normal, .motion = { .jump = 9 } };
global const u16 table[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
global const u16 table_copy[] = { table, [2] = 0x7777 };
union cell { u32 word; u8 bytes[4]; };
struct holder { union cell value; u8 after; };
global const struct holder holder = { .value.word = 0xaabbccddu32, .after = 3 };
global const struct holder entered = { holder, .value.bytes[1] = 0x55 };
global const struct holder kept = { holder, .after = 4 };
struct flags { u8 a : 3; u8 b : 5; u8 tail; };
global const struct flags flag_base = { .a = 5, .b = 17, .tail = 0x44 };
global const struct flags flag_copy = { flag_base, .b = 3 };
global const i32 k = 5;
global const i32 *const pk = &k;
global const i32 *const pk2 = pk;
global const u8 word[6] = { "abc", [1] = 'X' };
