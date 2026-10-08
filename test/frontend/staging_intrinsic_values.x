// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static u32 effects;
[[noinline, runtime_only]] static u16 next() { ++effects; return 9u16; }
[[eval_only]] static u16 constant() { return 7u16; }
[[eval_only]] static void empty() {}
struct Pair { u16 low; u16 high; };
[[eval_only]] static struct Pair pair() { struct Pair value = {13u16, 17u16}; return value; }
static $::meta::tokens forward(in $::meta::tokens input) { return $::eval(input); }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (sizeof($::runtime(next())) != sizeof(u16)) return $::quote { 0u32 };
    return forward(input);
}
$::static_assert(sizeof($::eval(next())) == sizeof(u16), "eval result type");
$::static_assert($::alignof($::runtime(next())) == $::alignof(u16), "runtime result type");
$::static_assert(sizeof($::eval(pair())) == sizeof(struct Pair), "aggregate result type");

#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (sizeof($::eval(next())) != sizeof(u16) || effects != 0u32) return 1u32;
    u16 left = $::eval(constant());
    u16 right = $::runtime(next());
    if (left != 7u16 || right != 9u16 || effects != 1u32) return 2u32;
    struct Pair value = $::eval(pair());
    if (value.low != 13u16 || value.high != 17u16) return 3u32;
    $::eval(empty());
    return apply!(61u32);
}
