// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[macro]] static $::meta::tokens part(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens object(in $::meta::syntax_match input) {
    $::meta::tokens declaration = $::quote {
        $::unquote($::syntax::node(input, "type")) $::unquote($::syntax::capture(input, "name"))
            = $::unquote($::syntax::node(input, "value"));
    };
    $::meta::syntax node = $::meta::parse("declaration", declaration, $::syntax::context(input));
    $::static_assert($::meta::is_production(node, "declaration"), "leading type declaration root");
    return $::quote { $::unquote(node) };
}
[[syntax_expander]] static $::meta::tokens array(in $::meta::syntax_match input) {
    return $::quote {
        $::unquote($::syntax::node(input, "type")) $::unquote($::syntax::capture(input, "name"))
            [$::unquote($::syntax::node(input, "count"))] = {};
    };
}
[[syntax_expander]] static $::meta::tokens function(in $::meta::syntax_match input) {
    $::meta::tokens header = $::quote {
        $::unquote($::syntax::node(input, "type")) $::unquote($::syntax::capture(input, "name"))
            $::unquote($::syntax::capture(input, "parameters"))
    };
    $::meta::tokens body = $::syntax::capture(input, "body");
    $::meta::syntax_match mode = $::syntax::at(input, "mode", 0uptr);
    if ($::syntax::is_variant(mode, "header")) {
        $::meta::syntax node = $::meta::parse("function_header", header, $::syntax::context(input));
        $::static_assert($::meta::is_production(node, "function_header"), "leading type header root");
        return $::quote { $::unquote(node) $::unquote(body) };
    }
    $::meta::tokens definition = $::meta::concat(header, body);
    if ($::syntax::is_variant(mode, "parsed")) {
        $::meta::syntax node = $::meta::parse("function_def", definition, $::syntax::context(input));
        $::static_assert($::meta::is_production(node, "function_definition"), "leading type function root");
        return $::quote { $::unquote(node) };
    }
    return definition;
}
[[syntax_expander]] static $::meta::tokens prototype(in $::meta::syntax_match input) {
    $::meta::tokens declaration = $::quote {
        $::unquote($::syntax::node(input, "type")) $::unquote($::syntax::capture(input, "name"))
            $::unquote($::syntax::capture(input, "parameters"));
    };
    $::meta::syntax node = $::meta::parse("function_decl", declaration, $::syntax::context(input));
    $::static_assert($::meta::is_production(node, "declaration"), "leading type prototype root");
    return $::quote { $::unquote(node) };
}
syntax Object : item {
    prefix "object"; match "(" type:type ")" name:ident "=" value:expr ";"; expand object;
}
syntax Array : item {
    prefix "array"; match "(" type:type ")" name:ident "[" count:expr "]" ";"; expand array;
}
syntax Function : item {
    prefix "function";
    match mode:choice(direct:("direct") | parsed:("parsed") | header:("header"))
        "(" type:type ")" name:ident parameters:paren body:block;
    expand function;
}
syntax Prototype : item {
    prefix "prototype"; match "(" type:type ")" name:ident parameters:paren ";"; expand prototype;
}
syntax Object, Array, Function, Prototype;
namespace Created {
    object (u32) value = 17u32;
    object (part!(const u16)) narrow = 301u16;
    array (u16 [2]) matrix[3uptr];
    prototype (u32) identity(in u32 value);
    function direct (u32) identity(in u32 value) { return value + 3u32; }
    function parsed (part!(u32)) parsed(in u32 value) { return value + 5u32; }
    function header (u32) composed(in u32 value) { return value + 7u32; }
    function parsed (u32 (*)(in u32)) callback() { return identity; }
}

#ifdef CUSTOM_NULL_ABI
struct Packet { u64 first; u64 second; };
[[abi("stack_result_abi"), noinline]] static uptr stack_result(in u32 value) { return (uptr)value + 1uptr; }
[[abi("memory_result_abi"), noinline]] static struct Packet memory_result(in u32 value) {
    struct Packet packet = { (u64)value, 43u64 };
    return packet;
}
function parsed (uptr (*)(in u32) [[abi("stack_result_abi")]]) stack_callback() { return stack_result; }
function header (struct Packet (*)(in u32) [[abi("memory_result_abi")]]) memory_callback() { return memory_result; }
#endif

#ifdef CUSTOM_NULL_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    if (Created::value != 17u32 || Created::narrow != 301u16) return 1u32;
    if (sizeof(Created::matrix) != 6uptr * sizeof(u16) || sizeof(Created::matrix[0]) != 2uptr * sizeof(u16)) return 2u32;
    Created::matrix[2][1] = 509u16;
    if (Created::matrix[2][1] != 509u16 || Created::matrix[0][0] != 0u16) return 3u32;
    if ($::runtime(Created::identity(19u32)) != 22u32 || $::runtime(Created::parsed(23u32)) != 28u32 ||
        $::runtime(Created::composed(29u32)) != 36u32 || $::runtime(Created::callback()(31u32)) != 34u32) return 4u32;
#ifdef CUSTOM_NULL_ABI
    if ($::runtime(stack_callback()(37u32)) != 38uptr) return 5u32;
    struct Packet packet = $::runtime(memory_callback()(41u32));
    if (packet.first != 41u64 || packet.second != 43u64) return 6u32;
#endif
    return 61u32;
}
