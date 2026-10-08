// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#define CROSS_DEPTH_TEN item item item item item item item item item item
#define CROSS_DEPTH_120 CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN

#ifdef CROSS_DEEP_GENERIC
#define CROSS_DEPTH_INPUT CROSS_DEPTH_120 CROSS_DEPTH_120
#define CROSS_DEPTH_RESULT 241u32
#define CROSS_DEPTH_RUNTIME_RESULT 248u32
#else
#define CROSS_DEPTH_INPUT CROSS_DEPTH_120
#define CROSS_DEPTH_RESULT 121u32
#define CROSS_DEPTH_RUNTIME_RESULT 128u32
#endif

namespace DeepExpressionExpansion {
    [[noinline]] static u32 plus_one(in u32 value) { return value + 1u32; }
#ifdef CUSTOM_SYNTAX_ABI
    [[noinline, abi("stack_result_abi")]] static u32 stack_identity(in u32 value) {
        return value;
    }
    [[noinline, abi("memory_result_abi")]] static u32 memory_identity(in u32 value) {
        return value;
    }
#endif
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
        uptr count = $::meta::len(body);
        if (count == 0uptr) return $::quote { 1u32 };
        $::meta::tokens rest = $::meta::slice(body, 1uptr, count - 1uptr);
        $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
        if (count % 4uptr == 0uptr)
            return $::quote { plus_one($::unquote(prefix) ($::unquote(rest))) };
        if (count % 4uptr == 1uptr)
            return $::quote { (u32)(1u32 ? $::unquote(prefix) ($::unquote(rest)) + 1u32 : 0u32) };
        if (count % 4uptr == 2uptr)
            return $::quote { 1u32 + $::unquote(prefix) ($::unquote(rest)) };
        $::meta::syntax parsed = $::meta::parse("expr",
            $::quote { $::unquote(prefix) ($::unquote(rest)) + 1u32 },
            $::syntax::context(input));
        return $::quote { $::unquote(parsed) };
    }
    syntax Deep : expression { prefix "deep_expression"; match body:paren; expand expand; }
    syntax Deep;
    [[noinline]] static u32 run(in u32 value) {
        u32 result = deep_expression (CROSS_DEPTH_INPUT) + value;
#ifdef CUSTOM_SYNTAX_ABI
        return memory_identity(stack_identity(result));
#else
        return result;
#endif
    }
    $::static_assert($::eval(run(0u32)) == CROSS_DEPTH_RESULT, "each shrinking expansion adds one");
}

#undef CROSS_DEPTH_120
#undef CROSS_DEPTH_TEN
#undef CROSS_DEPTH_INPUT
#undef CROSS_DEPTH_RESULT

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifdef CROSS_DEEP_GENERIC
    if ($::runtime(GenericArgumentExpansion::retained(42uptr)) != 42uptr) return 0u32;
#endif
    if ($::runtime(DeepExpressionExpansion::run(7u32)) != CROSS_DEPTH_RUNTIME_RESULT) return 0u32;
    return 61u32;
}

#undef CROSS_DEPTH_RUNTIME_RESULT
