// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace RelocatedParameters {
    static $::meta::syntax find(in $::meta::syntax node, in const u8 *production) {
        if ($::meta::is_production(node, production)) return node;
        if ($::meta::is_kind(node, "core"))
            for (uptr i = 0uptr; i < $::meta::child_count(node); ++i) {
                $::meta::syntax result = find($::meta::child(node, i), production);
                if ($::meta::is_production(result, production)) return result;
            }
        return node;
    }
    [[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax function = $::syntax::node(input, "function");
        $::meta::tokens mode = $::syntax::capture(input, "mode");
        u8 mode_byte = $::meta::at($::meta::spelling(mode), 0uptr);
        $::meta::syntax binder = find(function, "parameter_declaration");
        if (mode_byte == 'g') binder = find(function, "generic_parameter");
        if (mode_byte == 'l') binder = find(function, "declaration");
        $::meta::tokens raw = $::meta::tokens(binder);
        uptr name_index = $::meta::len(raw) - 1uptr;
        if (mode_byte == 'l') name_index = 1uptr;
        $::meta::tokens name = $::meta::slice(raw, name_index, 1uptr);
        if (mode_byte == 'v') {
            $::meta::tokens attribute = $::meta::children($::meta::slice($::meta::tokens(function), 0uptr, 1uptr));
            $::meta::tokens states = $::meta::children($::meta::slice(attribute, 1uptr, 1uptr));
            name = $::meta::slice(states, 1uptr, 1uptr);
        }
        $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
        $::meta::syntax result = $::meta::child(body, $::meta::child_count(body) - 2uptr);
        return $::quote {
            [[noinline]] static u32 as_local() { u32 $::unquote(name) = 7u32; $::unquote(result) }
            [[noinline]] static u32 as_parameter(in u32 $::unquote(name)) { $::unquote(result) }
            [[noinline]] static u32 as_generic<u32 $::unquote(name)>() { $::unquote(result) }
            [[noinline]] static u32 as_attribute() [[generic(u32 $::unquote(name))]] { $::unquote(result) }
            [[noinline]] static u32 projected(in u32 $::unquote(name)) { $::unquote($::meta::tokens(result)) }
        };
    }
    syntax Move : item { prefix "move"; match mode:ident function:function_def; expand move; }
    syntax Move;
    namespace Ordinary { move ordinary static u32 captured(in u32 value) { return value + 2u32; } }
    namespace Generic { move generic static u32 captured<u32 value>() { return value + 2u32; } }
    namespace Local { move local static u32 captured() { u32 value = 99u32; return value + 2u32; } }
    // This captured state is never lowered: relocation is independent of the
    // selected target's available ABI states and physical transport.
    namespace Variadic {
        move variadic [[variadic(u32 value "source_state")]]
        static u32 captured(in u32 tag, ...) { return value + 2u32; }
    }

    [[syntax_expander]] static $::meta::tokens decorate(in $::meta::syntax_match input) {
        $::meta::syntax definition = $::meta::parse("function_def", $::quote {
            [[noinline, generic(u32 $::unquote($::syntax::capture(input, "name")))]]
            $::unquote($::syntax::node(input, "header"))
            $::unquote($::syntax::capture(input, "body"))
        }, $::syntax::context(input));
        return $::quote { $::unquote(definition) };
    }
    syntax Decorate : item { prefix "decorate"; match name:ident header:function_header body:block; expand decorate; }
    syntax Decorate;
    [[macro]] static $::meta::tokens forward(in $::meta::tokens input) { return input; }
    decorate N static u32 decorated(in u32 value) { return forward!(value) + forward!(N); }
    $::static_assert(decorated<7u32>(2u32) == 9u32, "new generic around a retained header");

    $::static_assert(Ordinary::as_local() == 9u32 && Ordinary::as_parameter(8u32) == 10u32 &&
        Ordinary::as_generic<9u32>() == 11u32 && Ordinary::as_attribute<10u32>() == 12u32 &&
        Ordinary::projected(11u32) == 13u32, "ordinary parameter role changes");
    $::static_assert(Generic::as_local() == 9u32 && Generic::as_parameter(8u32) == 10u32 &&
        Generic::as_generic<9u32>() == 11u32 && Generic::as_attribute<10u32>() == 12u32 &&
        Generic::projected(11u32) == 13u32, "value-generic parameter role changes");
    $::static_assert(Local::as_local() == 9u32 && Local::as_parameter(8u32) == 10u32 &&
        Local::as_generic<9u32>() == 11u32 && Local::as_attribute<10u32>() == 12u32 &&
        Local::projected(11u32) == 13u32, "local declaration role changes");
    $::static_assert(Variadic::as_local() == 9u32 && Variadic::as_parameter(8u32) == 10u32 &&
        Variadic::as_generic<9u32>() == 11u32 && Variadic::as_attribute<10u32>() == 12u32 &&
        Variadic::projected(11u32) == 13u32, "variadic-state role changes");

    static u32 run() {
        if (decorated<7u32>(2u32) != 9u32) return 0u32;
        if (Ordinary::as_local() != 9u32 || Ordinary::as_parameter(8u32) != 10u32 ||
            Ordinary::as_generic<9u32>() != 11u32 || Ordinary::as_attribute<10u32>() != 12u32 ||
            Ordinary::projected(11u32) != 13u32) return 0u32;
        if (Generic::as_local() != 9u32 || Generic::as_parameter(8u32) != 10u32 ||
            Generic::as_generic<9u32>() != 11u32 || Generic::as_attribute<10u32>() != 12u32 ||
            Generic::projected(11u32) != 13u32) return 0u32;
        if (Local::as_local() != 9u32 || Local::as_parameter(8u32) != 10u32 ||
            Local::as_generic<9u32>() != 11u32 || Local::as_attribute<10u32>() != 12u32 ||
            Local::projected(11u32) != 13u32) return 0u32;
        if (Variadic::as_local() != 9u32 || Variadic::as_parameter(8u32) != 10u32 ||
            Variadic::as_generic<9u32>() != 11u32 || Variadic::as_attribute<10u32>() != 12u32 ||
            Variadic::projected(11u32) != 13u32) return 0u32;
        return 1u32;
    }
}
