// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A keyword never names a procedural macro, so `keyword !(...)` negates,
// with or without macros in the source unit.
#ifdef WITH_MACRO
[[macro]] static $::meta::tokens same(in $::meta::tokens input) { return input; }
#define SAME(x) same!(x)
#else
#define SAME(x) (x)
#endif

static bool even(in u32 off) {
    return !(off & 1u32);
}

static u32 negated_size(in u32 value) {
    return (u32)sizeof !(value) + SAME(0u32);
}

static u32 select(in u32 value) {
    switch (value) {
    case 0u32:
        return !(value) ? 7u32 : 8u32;
    default:
        return !(value + SAME(1u32)) ? 9u32 : 10u32;
    }
}

global bool even_two = even(2u32);
global bool even_three = even(3u32);
$::static_assert(even(2u32) && !even(3u32), "return !(x) negates");
$::static_assert(negated_size(5u32) == 1u32, "sizeof !(x) measures a bool");
$::static_assert(select(0u32) == 7u32 && select(4u32) == 10u32, "case and default");
