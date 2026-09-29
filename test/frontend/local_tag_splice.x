// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace LocalTagSplice {
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Copy : statement { prefix "tag_copy"; match body:stmt; expand copy; }
    [[syntax_expander]] static $::meta::tokens type(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body"))
            $::unquote($::syntax::capture(input, "name")); };
    }
    syntax Type : statement { prefix "tag_type"; match name:ident "," body:type ";"; expand type; }
    [[syntax_expander]] static $::meta::tokens twice(in $::meta::syntax_match input) {
        return $::quote { { $::unquote($::syntax::node(input, "body"))
                           $::unquote($::syntax::node(input, "body")) } };
    }
    syntax Twice : statement { prefix "tag_twice"; match body:stmt; expand twice; }
    [[syntax_expander]] static $::meta::tokens text_twice(in $::meta::syntax_match input) {
        $::meta::tokens body = $::meta::tokens($::syntax::node(input, "body"));
        return $::quote { { $::unquote(body) $::unquote(body) } };
    }
    syntax TextTwice : statement { prefix "tag_text_twice"; match body:stmt; expand text_twice; }
    [[macro]] static $::meta::tokens declare(in $::meta::tokens name) {
        return $::quote { struct $::unquote(name) { u16 value; }; };
    }
    [[syntax_expander]] static $::meta::tokens shadow(in $::meta::syntax_match input) {
        $::meta::tokens tag = $::meta::call_site($::meta::parse("CapturedTag"));
        $::meta::tokens name = $::syntax::capture(input, "name");
        return $::quote { {
            struct $::unquote(tag) { u32 value; };
            $::unquote($::syntax::node(input, "body"))
            if (sizeof($::unquote(name)) != 2uptr || $::unquote(name).value != 9u16) return 0u32;
        } };
    }
    syntax Shadow : statement { prefix "tag_shadow"; match name:ident body:stmt; expand shadow; }

    [[noinline]] static u32 enum_pair<T>(in T left, in T right) {
        return (u32)left + (u32)right;
    }

    [[noinline]] static u32 run(in u32 amount) {
        syntax Copy, Type, Twice, TextTwice, Shadow;
        tag_copy struct Local { u16 value; };
        struct Local first = {(u16)amount};
        tag_copy enum E [[underlying(u16)]] { A = 3u16, B = A + 2u16 };
        enum E enumeration = B;
        if (sizeof(first) != 2uptr || first.value != (u16)amount ||
            (u32)enumeration != 5u32) return 0u32;
        tag_type type_enum, enum TypeEnum [[underlying(u16)]] { TypeA = 7u16 };
        type_enum = TypeA;
        if (sizeof(type_enum) != 2uptr || (u32)type_enum != 7u32) return 0u32;

        struct Forward;
        struct Forward *pointer;
        tag_copy struct Forward { u32 value; };
        struct Forward completed = {amount + 1u32};
        pointer = &completed;
        if (pointer->value != amount + 1u32) return 0u32;

        struct CapturedTag { u16 value; } original = {9u16};
        tag_shadow copied struct CapturedTag copied = original;
        tag_twice {
            struct RepeatedTag { u16 value; } repeated = {(u16)amount};
            if (repeated.value != (u16)amount) return 0u32;
            enum RepeatedEnum [[underlying(u16)]] { RepeatedA = 7u16 } repeated_enum = RepeatedA;
            if (enum_pair(repeated_enum, RepeatedA) != 14u32) return 0u32;
            tag_twice {
                enum NestedEnum [[underlying(u16)]] { NestedA = (u16)RepeatedA } nested_enum = NestedA;
                if (enum_pair(nested_enum, NestedA) != 14u32 ||
                    enum_pair(repeated_enum, RepeatedA) != 14u32) return 0u32;
            }
        }
        tag_text_twice {
            enum ProjectedEnum [[underlying(u16)]] { ProjectedA = 9u16 } projected_enum = ProjectedA;
            if (enum_pair(projected_enum, ProjectedA) != 18u32) return 0u32;
        }
        tag_copy {
            declare!(DeferredTag);
            tag_type deferred, struct DeferredTag;
            deferred.value = 13u16;
            if (sizeof(deferred) != 2uptr || deferred.value != 13u16) return 0u32;
        }
        return amount;
    }
}
