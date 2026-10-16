// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Members read through a pointer feed integer and floating arithmetic as
// memory operands; a read stays before a store that may change it.
// memory_operands_entry returns zero when every result matches.

struct record {
    u32 count;
    u32 mask;
    f32 scale;
    f32 bias;
    f64 weight;
    u64 wide;
};

[[noinline]] global u32 mixed(in const struct record *r, in u32 x) {
    return ((x + r->count) & r->mask) ^ (x * r->count);
}

[[noinline]] global f32 affine(in const struct record *r, in f32 x) {
    return x * r->scale + r->bias;
}

[[noinline]] global f32 ordered(in const struct record *r, in f32 x) {
    return (x - r->bias) / r->scale;
}

[[noinline]] global f64 weighted(in const struct record *r, in f64 x) {
    return x * r->weight - r->weight;
}

[[noinline]] global u64 wide_or(in const struct record *r, in u64 x) {
    return (x | r->wide) + (u64)r->count;
}

[[noinline]] global u32 before_store(in struct record *r, in u32 x) {
    const u32 old = r->count;
    r->count = x;
    return old + x;
}

[[noinline]] global u32 aliased(in struct record *r, in u32 *p) {
    const u32 mask = r->mask;
    *p = 7u32;
    return mask + 1u32;
}

global u32 memory_operands_entry() {
    struct record r;
    r.count = 6u32;
    r.mask = 0xf0f0u32;
    r.scale = 0.5f32;
    r.bias = 3.0f32;
    r.weight = 1.25f64;
    r.wide = 0x100000001u64;
    if (mixed(&r, 1000u32) != ((1006u32 & 0xf0f0u32) ^ 6000u32)) return 1u32;
    if (affine(&r, 5.0f32) != 5.5f32) return 2u32;
    if (ordered(&r, 5.0f32) != 4.0f32) return 3u32;
    if (weighted(&r, 3.0f64) != 2.5f64) return 4u32;
    if (wide_or(&r, 0x10u64) != 0x100000017u64) return 5u32;
    if (before_store(&r, 9u32) != 15u32 || r.count != 9u32) return 6u32;
    if (aliased(&r, &r.mask) != 0xf0f1u32 || r.mask != 7u32) return 7u32;
    return 0u32;
}
