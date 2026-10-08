// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The registration grants more work, not greater depth or a larger host stack.
#define CROSS_TYPE_TEN item item item item item item item item item item
#define CROSS_TYPE_120 CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN CROSS_TYPE_TEN
#define CROSS_TYPE_240 CROSS_TYPE_120 CROSS_TYPE_120

namespace DeepTypeExpansion {
    [[syntax_expander]] static $::meta::tokens bound(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
        uptr count = $::meta::len(body);
        if (count == 0uptr) return $::quote { (u32)sizeof(uptr) };
        $::meta::tokens rest = $::meta::slice(body, 1uptr, count - 1uptr);
        $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
        return $::quote { 1u32 + (u32)sizeof(u8[$::unquote(prefix)($::unquote(rest))]) };
    }
    [[syntax_expander]] static $::meta::tokens mixed(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
        uptr count = $::meta::len(body);
        if (count == 0uptr) return $::quote { 1u32 };
        $::meta::tokens rest = $::meta::slice(body, 1uptr, count - 1uptr);
        $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
        if (count % 6uptr == 0uptr)
            return $::quote { (u32)sizeof(u8[$::unquote(prefix)($::unquote(rest))]) };
        if (count % 6uptr == 1uptr)
            return $::quote { (u32)sizeof(struct { u8 cell[$::unquote(prefix)($::unquote(rest))]; }) };
        if (count % 6uptr == 2uptr)
            return $::quote {
                (u32)sizeof(u8 (*)(in u8 cell[$::unquote(prefix)($::unquote(rest))])) / (u32)sizeof(u8 *)
            };
        if (count % 6uptr == 3uptr)
            return $::quote { (u32)sizeof(u8 [[ext_vector_type($::unquote(prefix)($::unquote(rest)))]]) };
        if (count % 6uptr == 4uptr)
            return $::quote { (u32)sizeof(enum [[underlying(u8)]] { Value = $::unquote(prefix)($::unquote(rest)) }) };
        $::meta::syntax type = $::meta::parse("type",
            $::quote { u8[$::unquote(prefix)($::unquote(rest))] }, $::syntax::context(input));
        $::static_assert($::meta::is_production(type, "type_name"), "structured type root");
        return $::quote { (u32)sizeof($::unquote(type)) };
    }
    syntax Bound : expression { prefix "deep_type_bound"; match body:paren; expand bound; }
    syntax Mixed : expression { prefix "deep_type_mixed"; match body:paren; expand mixed; }
    syntax Bound, Mixed;

#ifdef CUSTOM_SYNTAX_ABI
    [[noinline, abi("stack_result_abi")]] static u32 stack_identity(in u32 value) { return value; }
    [[noinline, abi("memory_result_abi")]] static u32 memory_identity(in u32 value) { return value; }
#endif
    [[noinline]] static u32 run(in u32 value) {
        u32 result = deep_type_bound(CROSS_TYPE_240) + value;
        if (deep_type_mixed(CROSS_TYPE_240) != 1u32) return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
        return memory_identity(stack_identity(result));
#else
        return result;
#endif
    }
    $::static_assert($::eval(run(0u32)) == 240u32 + (u32)sizeof(uptr), "nested type-bound evaluation");
}

#undef CROSS_TYPE_TEN
#undef CROSS_TYPE_120
#undef CROSS_TYPE_240

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if ($::runtime(DeepTypeExpansion::run(7u32)) != 247u32 + (u32)sizeof(uptr)) return 0u32;
    return 61u32;
}
