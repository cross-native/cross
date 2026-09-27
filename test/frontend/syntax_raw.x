// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_attribute(syntax_expander) || !$::has_builtin($::syntax::capture)
#error implemented syntax operations must be discoverable
#endif
#if $::has_feature($::feature::syntax_extensions)
#error full public-tree syntax support is not implemented yet
#endif

[[macro]] static $::meta::tokens copied(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens forwarded(in $::meta::tokens input) {
    return $::quote { copied! { $::unquote(input) } };
}
namespace flow {
    [[syntax_expander]] static $::meta::tokens five(in $::meta::syntax_match input) {
        return $::quote { 5u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand five; }
    syntax Base;
    [[syntax_expander]] static $::meta::tokens chain(in $::meta::syntax_match input) {
        return $::quote { base () + $::unquote($::syntax::capture(input, "body")) };
    }
    syntax Chain : expression { prefix "chain"; match body:paren; expand chain; }
    [[syntax_expander]] static $::meta::tokens copy_body(in $::meta::syntax_match input) {
        return $::syntax::capture(input, "body");
    }
    syntax Run : statement { prefix "run"; match body:block; expand copy_body; }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Discard : statement { prefix "discard"; match body:group; expand discard; }
    [[syntax_expander]] static $::meta::tokens returning(in $::meta::syntax_match input) {
        return $::quote { return $::unquote($::syntax::capture(input, "value")); };
    }
    syntax Ret : statement { prefix "ret"; match value:tokens_until(";") ";"; expand returning; }
    syntax Pair : rule { match left:literal ":" right:literal; }
    [[syntax_expander]] static $::meta::tokens pair(in $::meta::syntax_match input) {
        if ($::syntax::count(input, "values") != 1uptr) return $::quote { 0u32 };
        $::meta::syntax_match values = $::syntax::at(input, "values", 0uptr);
        return $::quote {
            $::unquote($::syntax::capture(values, "left")) +
            $::unquote($::syntax::capture(values, "right"))
        };
    }
    syntax Sum : expression { prefix "sum"; match "(" values:rule(Pair) ")"; expand pair; }
    syntax Tools : bundle { use Run; use Discard; use Chain as plus; use Sum; use Ret; }
    [[syntax_expander]] static $::meta::tokens make(in $::meta::syntax_match input) {
        return $::quote {
            static u32 $::unquote($::syntax::capture(input, "name"))() {
                return $::unquote($::syntax::capture(input, "value"));
            }
        };
    }
    syntax Make : item { prefix "make"; match name:ident value:literal ";"; expand make; }
    syntax CopyFunction : item { prefix "copy_fn"; match body:function_raw; expand copy_body; }
    [[syntax_expander]] static $::meta::tokens target_size(in $::meta::syntax_match input) {
        if (sizeof(uptr) == 4uptr) return $::quote { 4u32 };
        return $::quote { 8u32 };
    }
    syntax Width : expression { prefix "width"; match body:paren; expand target_size; }
    [[syntax_expander]] static $::meta::tokens primitive(in $::meta::syntax_match input) {
        return $::syntax::capture(input, "body");
    }
    syntax Lit : expression { prefix "lit"; match body:literal; expand primitive; }
    syntax Name : expression { prefix "named"; match body:name; expand primitive; }
    syntax Bracket : expression { prefix "bracket"; match body:bracket; expand five; }
    syntax Bang : expression { prefix "bang"; match "!" body:literal; expand primitive; }
    [[syntax_expander]] static $::meta::tokens empty(in $::meta::syntax_match input) {
        return $::quote {};
    }
    syntax Drop : item { prefix "drop"; match body:block; expand empty; }
    syntax DropTokens : item { prefix "drop_tokens"; match body:tokens_until(";") ";"; expand empty; }
    syntax DropFunction : item { prefix "drop_fn"; match body:function; expand empty; }
    [[syntax_expander]] static $::meta::tokens greater(in $::meta::syntax_match input) {
        return $::quote { 3u32 > 2u32 ? 9u32 : 0u32 };
    }
    syntax Greater : expression { prefix "greater"; match body:paren; expand greater; }
}
namespace caller {
    global u32 number = 13u32;
    [[syntax_expander]] static $::meta::tokens hundred(in $::meta::syntax_match input) {
        return $::quote { 100u32 };
    }
    syntax Base : expression { prefix "base"; match body:paren; expand hundred; }
}
namespace forwarding {
    syntax flow::Discard;
    [[macro]] static $::meta::tokens opaque(in $::meta::tokens input) {
        return $::quote { discard { syntax is DSL; [[syntax_expander]] not_a_declaration; } };
    }
}

syntax(flow::Make, flow::Drop, flow::DropTokens) {
    make generated 7u32;
    drop { no_such_macro!(); [[syntax_expander]] not_a_declaration; }
    drop_tokens [[syntax_expander]] not_a_declaration;
}
typedef u32 RawResult;
syntax flow::CopyFunction, flow::DropFunction;
copy_fn [[noinline]] static RawResult copied_function(in u32 value) { return forwarded! (value) + 17u32; }
drop_fn static u32 discarded_function(in u32 value) { no_such_macro!(); this is a foreign body; }
namespace reopened {
    syntax flow::Chain;
    static u32 first() { return chain (2u32); }
}
namespace reopened {
    static u32 second() { u32 chain = 3u32; return chain + 1u32; }
}
namespace imported_one {
    syntax Choice : expression { prefix "choose"; match body:paren; expand flow::five; }
}
namespace imported_two {
    syntax Choice : expression { prefix "choose"; match body:paren; expand caller::hundred; }
}
using imported_one;
[[noinline]] static u32 imports() {
    syntax Choice;
    { using imported_two;
      syntax Choice as other;
      return choose () + other ();
    }
}
[[syntax_expander]] static $::meta::tokens stable(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Scalar : rule { match value:literal; }
namespace frozen {
    syntax Value : expression { prefix "hold"; match "(" child:rule(Scalar) ")"; expand stable; }
    syntax Value;
    syntax Scalar : rule { match word:ident; }
    [[syntax_expander]] static $::meta::tokens stable(in $::meta::syntax_match input) {
        return $::quote { 2u32 };
    }
    [[noinline]] static u32 result() { return hold (3u32); }
}
[[noinline]] static u32 dangling(in u32 value) {
    syntax flow::Run;
    if (value) run { if (value == 1u32) return 3u32; }
    else return 5u32;
    return 7u32;
}
[[noinline]] static u32 lexical(in u32 input) {
    u32 plus = input;
    u32 total = 0u32;
    { syntax flow::Tools, caller::Base;
      discard { no_such_macro!(); [[syntax_expander]] not_a_declaration; }
      discard [ ( { } ) ];
      forwarding::opaque!();
      run { total += plus (2u32) * 3u32; }
      total += plus (base ()) + copied! (2u32);
      total += sum (3u32 : 4u32);
    }
    return total + plus;
}
[[noinline]] static u32 returned() {
    syntax flow::Ret;
    ret copied! (3u32) + (2u32 * 4u32);
}
[[noinline]] static u32 primitives() {
    syntax flow::Lit, flow::Name, flow::Bracket, flow::Bang;
    return lit 3u32 + named caller::number + bracket [raw, (data)] +
        (lit 1.5f32 == 1.5f32 ? 1u32 : 0u32) + (lit 'a' == 97 ? 1u32 : 0u32) +
        (lit "hi"[0] == 104 ? 1u32 : 0u32) + bang ! 2u32;
}
[[noinline]] static u32 macro_statements() {
    u32 value = 1u32;
    copied! (value) += 2u32;
    forwarded! (value) += 3u32;
    forwarded! { value += 4u32; }
    forwarded! { return value; }
}
[[generic(u32 number), noinline]] static u32 actual() { return number; }
[[noinline]] static u32 angle_boundary() {
    syntax flow::Greater;
    return actual<greater ()>();
}
syntax flow::Width;
global uptr syntax_width = width ();
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
#ifndef SYNTAX_COMPILE_ONLY
    if (generated() != 7u32) return 0u32;
    if (copied_function(4u32) != 21u32) return 0u32;
    if (reopened::first() != 7u32 || reopened::second() != 4u32) return 0u32;
    if (dangling(0u32) != 5u32 || dangling(1u32) != 3u32 || dangling(2u32) != 7u32) return 0u32;
    if (lexical(1u32) != 136u32 || returned() != 11u32) return 0u32;
    if (imports() != 105u32 || frozen::result() != 1u32) return 0u32;
    if (primitives() != 26u32) return 0u32;
    if (macro_statements() != 10u32) return 0u32;
    if (angle_boundary() != 9u32) return 0u32;
    if (syntax_width != sizeof(uptr)) return 0u32;
#endif
    return 61u32;
}
