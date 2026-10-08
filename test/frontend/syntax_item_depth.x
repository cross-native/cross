// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#define CROSS_DEPTH_TEN item item item item item item item item item item
#define CROSS_DEPTH_240 CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN CROSS_DEPTH_TEN

[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::tokens body = $::meta::children($::syntax::capture(input, "body"));
    uptr count = $::meta::len(body);
    if (count == 0uptr) {
#ifdef CUSTOM_SYNTAX_ABI
        return $::quote { [[abi(HOST_ABI), link_name("syntax_raw_entry")]] global u32 leaf() { return 61u32; } };
#else
        return $::quote { [[link_name("syntax_raw_entry")]] global u32 leaf() { return 61u32; } };
#endif
    }
    $::meta::tokens rest = $::meta::slice(body, 1uptr, count - 1uptr);
    $::meta::tokens prefix = $::meta::slice($::syntax::input(input), 0uptr, 1uptr);
    return $::quote { namespace N { $::unquote(prefix) ($::unquote(rest)) } };
}
syntax Deep : item { prefix "deep_item"; match body:paren; expand expand; }
syntax Deep;
deep_item (CROSS_DEPTH_240)
