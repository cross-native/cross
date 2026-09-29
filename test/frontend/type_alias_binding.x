// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace AliasBindingTests {
    typedef u16 NamespaceAlias;
    namespace Selection { typedef u16 First; typedef u32 Second; }
    [[syntax_expander]] static $::meta::tokens shadow(in $::meta::syntax_match input) {
        // The captured alias use becomes a new binder. That must not rebind
        // other uses of the original alias in the projected expression.
        $::meta::tokens name = $::meta::tokens($::syntax::node(input, "type"));
        $::meta::tokens fresh_use = $::meta::call_site(name);
        return $::quote { {
            typedef u8 $::unquote(name);
            if ($::unquote($::meta::tokens($::syntax::node(input, "value"))) !=
                $::unquote($::syntax::node(input, "expected"))) return 0u32;
            if (sizeof($::unquote(fresh_use)) != 1uptr) return 0u32;
        } };
    }
    syntax Shadow : statement {
        prefix "alias_shadow"; match type:type "," value:expr "," expected:expr ";"; expand shadow;
    }
    [[syntax_expander]] static $::meta::tokens statement(in $::meta::syntax_match input) {
        return $::quote { {
            typedef u8 $::unquote($::meta::tokens($::syntax::node(input, "type")));
            $::unquote($::meta::tokens($::syntax::node(input, "body")))
        } };
    }
    syntax Statement : statement {
        prefix "alias_statement"; match type:type "," body:stmt; expand statement;
    }
    [[syntax_expander]] static $::meta::tokens value_shadow(in $::meta::syntax_match input) {
        return $::quote { {
            u8 $::unquote($::meta::tokens($::syntax::node(input, "type"))) = 0u8;
            $::unquote($::meta::tokens($::syntax::node(input, "body")))
        } };
    }
    syntax ValueShadow : statement {
        prefix "alias_value_shadow"; match type:type "," body:stmt; expand value_shadow;
    }
    [[syntax_expander]] static $::meta::tokens edit(in $::meta::syntax_match input) {
        // Input starts with { typedef u32 Changed; ... }. Edit the base type,
        // retaining the copied declaration/use identities in all other tokens.
        $::meta::syntax compound = $::meta::child($::meta::child($::syntax::node(input, "body"), 0uptr), 0uptr);
        $::meta::tokens body = $::quote {};
        for (uptr i = 1uptr; i + 1uptr < $::meta::child_count(compound); ++i)
            body = $::meta::concat(body, $::meta::tokens($::meta::child(compound, i)));
        body = $::meta::concat($::meta::slice(body, 0uptr, 1uptr),
            $::meta::concat($::quote { u16 }, $::meta::slice(body, 2uptr, $::meta::len(body) - 2uptr)));
        return $::quote { { { $::unquote(body) } { $::unquote(body) } } };
    }
    syntax Edit : statement { prefix "alias_edit"; match body:stmt; expand edit; }
    [[syntax_expander]] static $::meta::tokens reset(in $::meta::syntax_match input) {
        // Explicit parsing deliberately replaces token binding provenance.
        $::meta::tokens input_tokens = $::quote { {
            typedef u8 $::unquote($::meta::tokens($::syntax::node(input, "type")));
            if ($::unquote($::meta::tokens($::syntax::node(input, "value"))) != 1uptr) return 0u32;
        } };
        return $::quote { $::unquote($::meta::parse("stmt", input_tokens, $::syntax::context(input))) };
    }
    syntax Reset : statement { prefix "alias_reset"; match type:type "," value:expr ";"; expand reset; }
    [[syntax_expander]] static $::meta::tokens changed_name(in $::meta::syntax_match input) {
        $::meta::tokens type = $::meta::tokens($::syntax::node(input, "type"));
        // Replacing a qualified suffix invalidates the old full-name binding.
        type = $::meta::concat($::meta::slice(type, 0uptr, $::meta::len(type) - 1uptr),
                              $::meta::parse("Second"));
        return $::quote { { if (sizeof($::unquote(type)) != 4uptr) return 0u32; } };
    }
    syntax Changed : statement { prefix "alias_changed"; match type:type ";"; expand changed_name; }

    [[noinline]] static u32 run(in u32 amount) {
        syntax Shadow, Statement, ValueShadow, Edit, Reset, Changed;
        typedef u32 Alias;
        alias_shadow Alias, sizeof(Alias), 4uptr;
        alias_shadow Alias, sizeof(Alias *) + sizeof(Alias), sizeof(uptr) + 4uptr;
        alias_shadow NamespaceAlias, sizeof(NamespaceAlias), 2uptr;
        alias_changed AliasBindingTests::Selection::First;
        alias_statement Alias, {
            Alias value = amount;
            if (sizeof(value) != 4uptr || value != amount) return 0u32;
        }
        alias_value_shadow Alias, {
            Alias value = amount;
            if (sizeof(value) != 4uptr || value != amount || sizeof(Alias) != 4uptr) return 0u32;
        }
        typedef u16 Pair[2];
        alias_statement Pair, {
            Pair pair = {7u16, 9u16};
            if (sizeof(pair) != 4uptr || pair[0] + pair[1] != 16u16) return 0u32;
        }
        alias_edit {
            typedef u32 Changed;
            typedef Changed Derived;
            Changed first = 7u16;
            Derived second = first;
            if (sizeof(first) != 2uptr || sizeof(second) != 2uptr || second != 7u16) return 0u32;
        }
        alias_reset Alias, sizeof(Alias);
        if (sizeof(Alias) != 4uptr) return 0u32;
        return amount;
    }
    $::static_assert(run(65549u32) == 65549u32, "captured and copied typedef bindings");

#ifdef CUSTOM_SYNTAX_ABI
    struct Result { u64 first; u64 second; };
    [[noinline, abi("stack_result_abi")]] static u32 stack_result(in u32 amount) { return amount + 1u32; }
    [[noinline, abi("memory_result_abi")]] static struct Result memory_result(in u32 amount) {
        struct Result result = {(u64)amount, (u64)amount + 2u64};
        return result;
    }
    [[noinline]] static u32 custom(in u32 amount) {
        syntax Statement;
        typedef u32 (*Stack)(in u32 amount) [[abi("stack_result_abi")]];
        typedef struct Result (*Memory)(in u32 amount) [[abi("memory_result_abi")]];
        alias_statement Stack, {
            Stack callable = &stack_result;
            if (callable(amount) != amount + 1u32) return 0u32;
        }
        alias_statement Memory, {
            Memory callable = &memory_result;
            struct Result result = callable(amount);
            if (result.first != (u64)amount || result.second != (u64)amount + 2u64) return 0u32;
        }
        return amount;
    }
#endif
}
