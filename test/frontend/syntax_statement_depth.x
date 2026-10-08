// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#define CROSS_DEPTH_TEN item item item item item item item item item item
#define CROSS_DEPTH_120 CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN

namespace DeepStatementExpansion {
    static u32 result;
    [[noinline]] static u32 identity(in u32 value) { return value; }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
        uptr count = $::meta::len(body);
        if (count == 0uptr) return $::quote { result = identity(61u32); };
        $::meta::tokens rest = $::meta::slice(body, 1uptr, count - 1uptr);
        $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
        if (count % 5uptr == 0uptr)
            return $::quote { { $::unquote(prefix) ($::unquote(rest)) } };
        if (count % 5uptr == 1uptr)
            return $::quote { if (1u32) $::unquote(prefix) ($::unquote(rest)) else result = 0u32; };
        if (count % 5uptr == 2uptr)
            return $::quote { do $::unquote(prefix) ($::unquote(rest)) while (0u32); };
        if (count % 5uptr == 3uptr)
            return $::quote { switch (0u32) { case 0u32: $::unquote(prefix) ($::unquote(rest)) break; } };
        $::meta::syntax parsed = $::meta::parse("stmt",
            $::quote { $::unquote(prefix) ($::unquote(rest)) }, $::syntax::context(input));
        return $::quote { $::unquote(parsed) };
    }
    syntax Deep : statement { prefix "deep_statement"; match body:paren; expand expand; }
    syntax Deep;
    [[noinline]] static u32 run() {
        result = 0u32;
        deep_statement (CROSS_DEPTH_120)
        return result;
    }
}

#undef CROSS_DEPTH_120
#undef CROSS_DEPTH_TEN

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return $::runtime(DeepStatementExpansion::run());
}
