// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Stores into local unions and records whose addresses are taken: those that
// no load, call, or escaping address can observe disappear once memory
// forwarding has replaced the loads; the others keep their effect. test_entry
// returns zero when every result matches.

union bits { f32 value; u32 word; };
struct pair { u32 first; u32 second; };

[[noinline]] global u32 float_bits(in f32 value) {
    union bits view;
    view.value = value;
    return view.word;
}

[[noinline]] global u32 pair_sum(in u32 x, in u32 y) {
    struct pair p;
    p.first = x;
    p.second = y;
    const struct pair *q = &p;
    return q->first + q->second;
}

// The second store overwrites part of the union after the first is read.
[[noinline]] global u32 rewritten(in f32 value, in u32 word) {
    union bits view;
    view.value = value;
    const u32 before = view.word;
    view.word = word;
    return before ^ view.word;
}

[[noinline]] global u32 read_pair(in const struct pair *p) {
    return p->first * 3u32 + p->second;
}

// The address escapes into a call, which reads the stores.
[[noinline]] global u32 escaped(in u32 x, in u32 y) {
    struct pair p;
    p.first = x;
    p.second = y;
    return read_pair(&p);
}

// A loop stores a record element by element and reads it back through an
// index.
[[noinline]] global u32 indexed(in u32 n) {
    u32 cells[4];
    for (u32 i = 0u32; i < 4u32; ++i) cells[i] = i * n;
    u32 total = 0u32;
    for (u32 i = 0u32; i < 4u32; ++i) total = total + cells[(i + n) & 3u32];
    return total;
}

global u32 test_entry() {
    if (float_bits(1.0f32) != 1065353216u32) return 1u32;
    if (float_bits(-2.5f32) != 3223322624u32) return 2u32;
    if (pair_sum(7u32, 35u32) != 42u32) return 3u32;
    if (rewritten(1.0f32, 5u32) != (1065353216u32 ^ 5u32)) return 4u32;
    if (escaped(4u32, 5u32) != 17u32) return 5u32;
    if (indexed(3u32) != 18u32) return 6u32;
    return 0u32;
}
