// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Accesses through a may_alias type may alias any effective type, with the
// same result at translation time and at runtime.
typedef u32 any_u32 [[may_alias]];
typedef u16 [[may_alias]] any_u16;
[[may_alias]] typedef u64 any_u64;

static u32 float_bits(in f32 value) {
    f32 copy = value;
    return *(any_u32 *)(void *)&copy;
}

// A write through the view leaves the object's declared type in force.
static f32 rebuilt(in u32 bits) {
    f32 copy = 0.0f32;
    *(any_u32 *)(void *)&copy = bits;
    return copy;
}

// The attribute as a qualifier in a cast's type name. Both halves are equal,
// so the result does not depend on byte order.
static u16 first_half(in u32 value) {
    u32 copy = value;
    return *(u16 [[may_alias]] *)(void *)&copy;
}

static u64 double_bits(in f64 value) {
    f64 copy = value;
    return *(any_u64 *)(void *)&copy;
}

// Members and copies of a may_alias record are accessed through it too.
struct Pair { u32 first; u32 second; };
typedef struct Pair any_pair [[may_alias]];

static u32 pair_sum(in u64 bits) {
    u64 copy = bits;
    any_pair *pair = (any_pair *)(void *)&copy;
    struct Pair whole = *pair;
    return pair->first + pair->second + whole.first + whole.second;
}

// Redeclarations may spell the type either way.
u32 shared;
any_u32 shared = 5u32;
u32 next(in u32 value);
u32 next(in any_u32 value) { return value + 1u32; }

$::static_assert(float_bits(1.0f32) == 0x3f800000u32, "may_alias read");
$::static_assert(rebuilt(0x40000000u32) == 2.0f32, "may_alias write");
$::static_assert(first_half(0x56785678u32) == 0x5678u16, "may_alias qualifier");
$::static_assert(double_bits(-2.0f64) == 0xc000000000000000u64, "may_alias leading attribute");
$::static_assert(pair_sum(0x0000000700000006u64) == 26u32, "may_alias record");

global u32 may_alias_entry() {
    any_u16 half = first_half(0xabcdabcdu32);
    return float_bits(1.0f32) == 0x3f800000u32 && rebuilt(0x40000000u32) == 2.0f32 &&
           half == 0xabcdu16 && double_bits(-2.0f64) == 0xc000000000000000u64 &&
           pair_sum(0x0000000700000006u64) == 26u32 && next(shared) == 6u32;
}
