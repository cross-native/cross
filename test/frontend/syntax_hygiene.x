// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens relocate(in $::meta::syntax_match input) {
    return $::quote {
        {
            u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
            return $::unquote($::meta::tokens($::syntax::node(input, "value")));
        }
    };
}
syntax Relocate : statement {
    prefix "relocate"; match name:ident "," value:expr ";"; expand relocate;
}
[[syntax_expander]] static $::meta::tokens surround(in $::meta::syntax_match input) {
    return $::quote {
        {
            u32 $::unquote($::syntax::capture(input, "name")) = 99u32;
            $::unquote($::meta::tokens($::syntax::node(input, "body")))
        }
    };
}
syntax Surround : statement {
    prefix "surround"; match name:ident "," body:stmt; expand surround;
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::meta::tokens($::syntax::node(input, "body"));
}
syntax Copy : statement { prefix "copied"; match body:stmt; expand copy; }

[[syntax_expander]] static $::meta::tokens choose_call_site(in $::meta::syntax_match input) {
    return $::meta::call_site($::meta::parse("outside"));
}
syntax ChooseCallSite : expression {
    prefix "callsite"; match body:paren; expand choose_call_site;
}
[[syntax_expander]] static $::meta::tokens choose_definition(in $::meta::syntax_match input) {
    return $::meta::parse("outside");
}
syntax ChooseDefinition : expression {
    prefix "defsite"; match body:paren; expand choose_definition;
}
[[macro]] static $::meta::tokens choose_macro_site(in $::meta::tokens input) {
    return $::meta::call_site($::meta::parse("outside"));
}

global u32 outside = 23u32;
[[noinline]] static u32 parameter(in u32 value) {
    syntax Relocate;
    relocate value, value;
}
[[noinline]] static u32 local() {
    syntax Relocate;
    u32 value = 31u32;
    relocate value, value;
}
[[noinline]] static u32 nonlocal() {
    syntax Relocate;
    relocate outside, outside;
}
[[noinline]] static u32 scoped() {
    syntax Relocate;
    u32 value = 7u32;
    {
        u32 value = 11u32;
        relocate value, value;
    }
}
[[noinline]] static u32 modified() {
    syntax Surround;
    u32 value = 4u32;
    surround value, value += 3u32;
    return value;
}
[[noinline]] static u32 copyout(inout u32 value) {
    syntax Surround;
    surround value, value += 2u32;
    return value;
}
[[noinline]] static u32 interior() {
    syntax Copy;
    u32 value = 3u32, result = 0u32;
    copied {
        u32 value = 13u32;
        result = value;
    }
    return result + value;
}
[[noinline]] static T generic<T, u32 amount>(in T value) {
    syntax Surround;
    surround value, value += amount;
    return value;
}
static u32 ordinary_shadow() {
    u32 value = 5u32, result = 0u32;
    { u32 value = 17u32; result = value; }
    return result + value;
}
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    syntax ChooseCallSite;
    syntax ChooseDefinition;
    u32 outside = 41u32;
    u32 value = 6u32;
    if (parameter(17u32) != 17u32 || local() != 31u32 || nonlocal() != 23u32 ||
        scoped() != 11u32 || modified() != 7u32 || copyout(value) != 8u32 ||
        value != 8u32 || interior() != 16u32 ||
        generic<u32, 3u32>(9u32) != 12u32 || ordinary_shadow() != 22u32 ||
        callsite () != 41u32 || defsite () != 23u32 ||
        choose_macro_site! {} != 41u32)
        return 0u32;
    return 61u32;
}
