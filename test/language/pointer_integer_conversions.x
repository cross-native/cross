// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Null pointer constants, an integer zero or one cast to `void *`, pointers
// as truth values, and explicit casts between pointers and integers, at run
// time and at translation time.
#define NULL ((void *)0)

typedef u32 (*callback)(in u32 value);
struct entry { u32 number; u8 *bytes; callback action; };

static u32 twice(in u32 value) { return value * 2u32; }

global u32 cell = 7u32;
global u8 *static_null = 0;
global struct entry static_entry = { 1u32, 0, 0 };
global u32 *static_device = (u32 *)4096;
global uptr static_address = (uptr)&cell;
global callback static_callback = NULL;
global callback static_table[3] = { twice, NULL, (void *)(1 - 1) };
global struct entry static_actions = { .bytes = NULL, .action = NULL };
global callback static_cast_null = (callback)NULL;

static u32 zero() { return 0u32; }
static u8 *keep(in u8 *p) { return p; }
static void clear(out u8 *p) { p = 0; }
static u32 *pick(in u32 select, in u32 *p) { return select != 0u32 ? p : 0; }
static u32 run(in callback action) { return action != NULL ? action(5u32) : 3u32; }
static callback choose(in u32 select) { return select != 0u32 ? twice : NULL; }

static u32 conversions(in u32 select) {
    u32 local = 5u32;
    u8 *p = 0;
    if (p != 0) return 1u32;
    p = zero();
    if (p) return 2u32;
    if (keep(0) != 0) return 3u32;
    p = (u8 *)&local;
    clear(p);
    if (p != 0) return 4u32;
    if (pick(select, &local) == 0) return 5u32;
    if (pick(0u32, &local) != 0) return 6u32;
    bool truth = &local;
    if (!truth) return 7u32;
    struct entry braced = { .number = 2u32 };
    if (braced.bytes != 0 || braced.action != 0) return 8u32;
    u32 *absolute = (u32 *)4096uptr;
    if ((uptr)absolute != 4096uptr) return 9u32;
    return 0u32;
}

static u32 function_nulls(in u32 select) {
    u32 local_cell = 4u32;
    callback action = NULL;
    if (action != 0 || NULL != action || run(action) != 3u32) return 1u32;
    action = twice;
    if (action == NULL || run(action) != 10u32) return 2u32;
    action = (void *)0;
    if (action) return 3u32;
    if (run(NULL) != 3u32 || run((void *)zero()) != 3u32) return 4u32;
    if (choose(select) == NULL || choose(0u32) != NULL) return 5u32;
    struct entry braced = { .action = NULL, .bytes = NULL };
    if (braced.action != 0 || braced.bytes != 0) return 6u32;
    u32 *selected = select != 0u32 ? &local_cell : NULL;
    if (*selected != 4u32) return 7u32;
    action = (callback)NULL;
    if (action != NULL) return 8u32;
    return 0u32;
}

// Only null function pointers: translation-time evaluation does not inspect
// the addresses of emitted functions.
static u32 null_callbacks(in u32 select) {
    callback action = NULL;
    if (action != NULL || NULL != action || action) return 1u32;
    action = (callback)NULL;
    if (action != 0) return 2u32;
    callback other = select != 0u32 ? NULL : twice;
    return other == NULL ? 0u32 : 3u32;
}

$::static_assert(conversions(1u32) == 0u32 && null_callbacks(1u32) == 0u32,
                 "conversions at translation time");

[[noinline]] global i32 pointer_integer_conversions_entry() {
    if (conversions(1u32) != 0u32 || function_nulls(1u32) != 0u32 || null_callbacks(1u32) != 0u32)
        return 0;
    if (static_null != 0 || static_entry.bytes != 0 || static_entry.action != 0) return 0;
    if (static_callback != NULL || static_table[1] != 0 || static_table[2] != NULL ||
        static_table[0](4u32) != 8u32 || static_actions.action || static_actions.bytes ||
        static_cast_null != NULL) return 0;
    if ((uptr)static_device != 4096uptr || static_address != (uptr)&cell) return 0;
    u8 *view = (u8 *)static_address;
    return *(u32 *)view == 7u32 ? 1 : 0;
}
