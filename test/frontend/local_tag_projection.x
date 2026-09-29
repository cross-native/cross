// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace LocalTagProjection {
    struct GlobalTag { u16 value; };
    struct Tag { u32 value; };
    namespace Selection {
        struct First { u16 value; };
        struct Second { u32 value; };
    }

    [[syntax_expander]] static $::meta::tokens project(in $::meta::syntax_match input) {
        // Every invocation below supplies one simple tag type: kind Name.
        $::meta::tokens type = $::meta::tokens($::syntax::node(input, "value"));
        $::meta::tokens name = $::meta::at(type, 1uptr);
        $::meta::tokens reset = $::meta::call_site(name);
        return $::quote { {
            // A copied use token may name a new declaration, but must not
            // retarget existing uses of the original type to that declaration.
            struct $::unquote(name) { u32 value; };
            if (sizeof($::unquote(type)) != 2uptr) return 0u32;
            if (sizeof(struct $::unquote(reset)) != 4uptr) return 0u32;
        } };
    }
    syntax Project : statement { prefix "tag_project"; match value:type ";"; expand project; }

    [[syntax_expander]] static $::meta::tokens expr_project(in $::meta::syntax_match input) {
        $::meta::tokens name = $::syntax::capture(input, "name");
        $::meta::tokens expression = $::meta::tokens($::syntax::node(input, "value"));
        return $::quote { {
            struct $::unquote(name) { u32 value; };
            if ($::unquote(expression) != 2uptr) return 0u32;
        } };
    }
    syntax ExprProject : statement { prefix "tag_expr"; match name:ident "," value:expr ";"; expand expr_project; }

    [[syntax_expander]] static $::meta::tokens recontext(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::meta::parse("expr",
            $::meta::tokens($::syntax::node(input, "value")), $::syntax::context(input))) };
    }
    syntax Context : expression { prefix "tag_here"; match "(" value:expr ")"; expand recontext; }
    // Definition-site prefix lookup for the quotation below.
    syntax Context;
    [[syntax_expander]] static $::meta::tokens explicit_context(in $::meta::syntax_match input) {
        $::meta::tokens type = $::meta::tokens($::syntax::node(input, "value"));
        return $::quote { {
            // This literal prefix supplies definition context, whose Tag is
            // the namespace-level u32 record, not the captured local u16 tag.
            if (tag_here(sizeof($::unquote(type))) != 4uptr) return 0u32;
        } };
    }
    syntax Explicit : statement { prefix "tag_explicit"; match value:type ";"; expand explicit_context; }

    [[syntax_expander]] static $::meta::tokens changed_name(in $::meta::syntax_match input) {
        $::meta::tokens type = $::meta::tokens($::syntax::node(input, "value"));
        // Keep the annotated first qualified-name token but replace its last
        // component. The old complete name binding must no longer apply.
        type = $::meta::concat($::meta::slice(type, 0uptr, $::meta::len(type) - 1uptr),
                              $::meta::parse("Second"));
        return $::quote { { if (sizeof($::unquote(type)) != 4uptr) return 0u32; } };
    }
    syntax Changed : statement { prefix "tag_changed"; match value:type ";"; expand changed_name; }

    [[noinline]] static u32 run(in u32 amount) {
        struct Tag { u16 value; };
        union UnionTag { u16 value; u8 byte; };
        enum EnumTag [[underlying(u16)]] { Value = 7u16 };
        syntax Project, ExprProject, Explicit, Changed;
        tag_project struct Tag;
        tag_project union UnionTag;
        tag_project enum EnumTag;
        tag_project struct GlobalTag;
        tag_expr Tag, sizeof(struct Tag);
        tag_expr UnionTag, sizeof(union UnionTag);
        tag_expr EnumTag, sizeof(enum EnumTag);
        tag_explicit struct Tag;
        tag_changed struct LocalTagProjection::Selection::First;
        return amount;
    }
    $::static_assert(run(11u32) == 11u32, "projected and explicitly rebound tags");
}
