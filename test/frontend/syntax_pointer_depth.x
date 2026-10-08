// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef u32 Mutable0;
typedef const u32 Qualified0;
#define CROSS_POINTER_ROW(HEAD, TAIL) typedef Mutable##HEAD *Mutable##TAIL; typedef Qualified##HEAD *const Qualified##TAIL;
#include "syntax_pointer_rows.inc"
#undef CROSS_POINTER_ROW
typedef Qualified39 *Joined;

namespace SymbolicPointerProof {
    struct Data { u8 prefix; u32 values[4]; };
    static struct Data data = {3u8, {5u32, 11u32, 17u32, 23u32}};
    static u32 *selected() { return &data.values[2]; }
    static uptr observe(in u32 *pointer) { return sizeof(*pointer); }
    static u32 read<u32 *Pointer>(in u32 seed) { return *Pointer + seed; }
    [[syntax_expander]] static $::meta::tokens retain(in $::meta::syntax_match input) {
        if (observe(selected()) != sizeof(u32)) return $::quote { unexpected_pointer_layout };
        return $::quote { $::unquote($::syntax::node(input, "definition")) };
    }
    syntax Keep : item { prefix "pointer_proof"; match definition:function_def; expand retain; }
    syntax Keep;
    pointer_proof [[noinline]] static u32 run(in u32 seed) { return read<selected()>(seed); }
}

[[syntax_expander]] static $::meta::tokens retain_join(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::meta::syntax reparsed = $::meta::parse("expr", $::quote { $::unquote(node) }, $::syntax::context(input));
    return $::quote { $::unquote(reparsed) };
}
syntax DeepPointerJoin : expression { prefix "deep_join"; match "(" value:expr ")"; expand retain_join; }
syntax DeepPointerJoin;

[[noinline]] static Joined select_pointer(in u32 condition, in Mutable40 left, in Joined right) {
    return deep_join(condition ? left : right);
}
[[noinline]] static Joined reverse_pointer(in u32 condition, in Mutable40 left, in Joined right) {
    return deep_join(condition ? right : left);
}
#ifdef CUSTOM_SYNTAX_ABI
[[noinline, abi("stack_result_abi")]] static Joined stack_pointer(in u32 condition, in Mutable40 left, in Joined right) {
    return select_pointer(condition, left, right);
}
[[noinline, abi("memory_result_abi")]] static Joined memory_pointer(in u32 condition, in Mutable40 left, in Joined right) {
    return reverse_pointer(condition, left, right);
}
#endif

$::static_assert($::eval(select_pointer(1u32, 0u32, 0u32)) == 0u32, "deep required pointer join");
$::static_assert($::eval(reverse_pointer(0u32, 0u32, 0u32)) == 0u32, "reverse required pointer join");
static Mutable39 left_cell = 0u32;
static Qualified39 right_cell = 0u32;
static volatile u32 selector = 1u32;

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (SymbolicPointerProof::run(44u32) != 61u32) return 0u32;
    Mutable40 left = &left_cell;
    Joined right = &right_cell;
    Joined qualified = left;
    if (select_pointer(selector, left, right) != qualified ||
        select_pointer(selector - 1u32, left, right) != right ||
        reverse_pointer(selector, left, right) != right ||
        reverse_pointer(selector - 1u32, left, right) != qualified) return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
    if (stack_pointer(selector, left, right) != qualified ||
        memory_pointer(selector, left, right) != right) return 0u32;
#endif
    return 61u32;
}
