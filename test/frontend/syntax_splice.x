// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens multiply(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    // A splice is one token tree through copying, slicing and concatenation.
    $::meta::tokens head = $::quote { $::unquote(value) };
    head = $::meta::slice(head, 0uptr, 1uptr);
    return $::meta::concat(head, $::quote { * 3u32 });
}
syntax Multiply : expression {
    prefix "multiplied"; match "(" value:expr ")"; expand multiply;
}

[[syntax_expander]] static $::meta::tokens parse_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    return $::quote { $::unquote(parsed) + 1u32 };
}
syntax ParseThenSplice : expression {
    prefix "parse_splice"; match "(" value:expr ")"; expand parse_then_splice;
}

[[syntax_expander]] static $::meta::tokens project_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    return $::meta::concat($::meta::tokens(parsed), $::quote { + 1u32 });
}
syntax ProjectThenSplice : expression {
    prefix "project_splice"; match "(" value:expr ")"; expand project_then_splice;
}

[[syntax_expander]] static $::meta::tokens inspect_without_expanding(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) + 1u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    // The nested invocation belongs to the discarded node, not this owner.
    return $::quote { 9u32 };
}
syntax InspectWithoutExpanding : expression {
    prefix "inspect_splice"; match "(" value:expr ")"; expand inspect_without_expanding;
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    syntax Multiply;
    syntax ParseThenSplice;
    syntax ProjectThenSplice;
    syntax InspectWithoutExpanding;
    u32 amount = 4u32;
    u32 __cross_syntax_splice = 2u32; // The marker spelling alone is ordinary source.
    // Structured grouping: (4 + 1) * 3; explicit projection: 4 + 1 * 3.
    if (multiplied (amount + 1u32) != 15u32 ||
        parse_splice (amount + 1u32) != 16u32 ||
        project_splice (amount + 1u32) != 8u32 ||
        inspect_splice (missing_macro! { 3u32 }) != 9u32 ||
        __cross_syntax_splice != 2u32)
        return 0u32;
    return 61u32;
}
