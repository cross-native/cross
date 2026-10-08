// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace BlockAssertions {
[[macro]] static $::meta::tokens copy(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens statement(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Statement : statement { prefix "assertion_copy"; match body:stmt; expand statement; }
syntax Statement;

[[noinline]] static T identity<T, u32 N>(in T value) {
    T local;
    T array[N];
    T arithmetic[N + 1u32];
    u8 layout[sizeof(T)];
    $::static_assert(sizeof(value) == sizeof(T), "parameter type");
    $::static_assert(sizeof(local) == sizeof(T), "local type");
    assertion_copy $::static_assert(sizeof(array) == sizeof(T) * (uptr)N, "captured generic array");
    $::static_assert(sizeof(arithmetic) == sizeof(T) * (uptr)(N + 1u32), "generic arithmetic extent");
    $::static_assert(sizeof(layout) == sizeof(T), "layout extent");
    {
        u8 local;
        copy!($::static_assert(sizeof(local) == 1uptr, "inner shadow");)
    }
    $::static_assert(sizeof(local) == sizeof(T), "restored outer type");
    struct Local { T first; T second; } record;
    $::static_assert(sizeof(record) == sizeof(T) * 2uptr, "generic nominal layout");
    $::static_assert($::alignof(record) == $::alignof(T), "generic nominal alignment");
    $::static_assert(sizeof(point) == sizeof(label), "lexical label owner");
    point: ;
    return value;
}

// Not being called must not make a declaration-time assertion disappear.
static u32 unused(in u64 parameter) {
    f64 local;
    if (0u32) $::static_assert(sizeof(local) == 8uptr, "untaken declaration");
    for (uptr index = 0uptr; index < 1uptr; ++index) {
        $::static_assert(sizeof(index) == sizeof(uptr), "for scope");
    }
    return 0u32;
    $::static_assert(sizeof(parameter) == 8uptr, "unreachable declaration");
    $::static_assert(1.5f64, "scalar floating assertion");
}
static T unused_generic<T>() {
    $::static_assert(0u32, "unused generic must not instantiate");
    return (T)0u32;
}

static u32 expansion_helper(in u32 value) {
    u16 local;
    $::static_assert(sizeof(value) == 4uptr && sizeof(local) == 2uptr,
                    "ordinary helper declaration context");
    return value + 1u32;
}
[[macro]] static $::meta::tokens generated(in $::meta::tokens input) {
    if (expansion_helper(6u32) != 7u32) return $::quote { 0u32 };
    return $::quote { 7u32 };
}

static u32 run() {
    if (generated!() != 7u32) return 0u32;
    return identity<u32, 3u32>(97u32) == 97u32 && identity<u16, 5u32>(11u16) == 11u16
        ? 97u32 : 0u32;
}
}
