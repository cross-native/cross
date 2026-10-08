// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens multiply(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) * 3u32 };
}
syntax Multiply : expression { prefix "multiplied"; match "(" value:expr ")"; expand multiply; }

[[syntax_expander]] static $::meta::tokens parse_splice(in $::meta::syntax_match input) {
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote($::syntax::node(input, "value")) * 3u32 }, $::syntax::context(input));
    return $::quote { $::unquote(parsed) + 1u32 };
}
syntax ParseThenSplice : expression { prefix "parse_splice"; match "(" value:expr ")"; expand parse_splice; }

[[syntax_expander]] static $::meta::tokens project_splice(in $::meta::syntax_match input) {
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote($::syntax::node(input, "value")) * 3u32 }, $::syntax::context(input));
    return $::meta::concat($::meta::tokens(parsed), $::quote { + 1u32 });
}
syntax ProjectThenSplice : expression { prefix "project_splice"; match "(" value:expr ")"; expand project_splice; }

[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote { 9u32 };
}
syntax InspectWithoutExpanding : expression { prefix "inspect_splice"; match "(" value:expr ")"; expand discard; }

namespace UnaryCastOperands {
    typedef u32 Word;
    static u32 next(in u32 *value) { *value += 1u32; return *value; }
    [[macro]] static $::meta::tokens text(in $::meta::tokens input) { return input; }
    [[noinline]] static u32 run(in u32 value) {
        syntax Multiply, ParseThenSplice, ProjectThenSplice, InspectWithoutExpanding;
        u32 *pointer = &value;
        if (+(Word)value != value || -(i32)value != 0i32 - (i32)value ||
            ~(u32)value != 0xffffffffu32 - value || !(u32)value != (value == 0u32) ||
            *(u32 *)pointer != value || &*(u32 *)pointer != pointer) return 0u32;
        if (multiplied (-(i32)value + 10i32) != (10u32 - value) * 3u32 ||
            parse_splice (*(u32 *)pointer + 1u32) != (value + 1u32) * 3u32 + 1u32 ||
            project_splice (+(Word)value + 1u32) != value + 4u32 ||
            inspect_splice (-(i32)missing_macro! {}) != 9u32 ||
            text!(-(i32)value) + 10i32 != 10i32 - (i32)value) return 0u32;
        u32 before = value;
        if (-(i32)next(&value) != -(i32)(before + 1u32) || value != before + 1u32)
            return 0u32;
        return 1u32;
    }
}
$::static_assert(UnaryCastOperands::run(4u32) == 1u32, "unary cast composition");

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if ($::runtime(UnaryCastOperands::run(4u32)) != 1u32 ||
        $::runtime(UnaryCastOperands::run(0u32)) != 1u32) return 0u32;
    return 61u32;
}
