// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[macro]] static $::meta::tokens introduce_type(in $::meta::tokens input) {
    return $::quote { typedef uptr $::unquote(input); };
}
[[macro]] static $::meta::tokens opaque_type(in $::meta::tokens input) {
    return $::quote { NotAType };
}
[[macro]] static $::meta::tokens word_type(in $::meta::tokens input) {
    return $::quote { u16 };
}
[[macro]] static $::meta::tokens pointer_type(in $::meta::tokens input) {
    return $::quote { u32 * };
}
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Drop : statement { prefix "drop"; match body:stmt; expand discard; }
[[syntax_expander]] static $::meta::tokens deferred_statement(in $::meta::syntax_match input) {
    $::meta::syntax root = $::syntax::node(input, "body");
    $::meta::syntax ordinary = $::meta::child(root, 0uptr);
    $::meta::syntax deferred = $::meta::child(ordinary, 0uptr);
    if (!$::meta::is_kind(deferred, "deferred") ||
        $::meta::is_production(deferred, "compound_statement") ||
        $::meta::child_count(deferred) != 0uptr)
        return $::quote { public_schema_failure(); };
    $::meta::syntax parsed = $::meta::parse("stmt",
        $::quote { $::unquote(root) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "statement") ||
        !$::meta::is_kind($::meta::child($::meta::child(parsed, 0uptr), 0uptr), "deferred"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(root) };
}
syntax Deferred : statement { prefix "deferred"; match body:stmt; expand deferred_statement; }
[[syntax_expander]] static $::meta::tokens introduce_alias(in $::meta::syntax_match input) {
    return $::quote { typedef uptr $::unquote($::syntax::capture(input, "name")); };
}
syntax Introduce : statement { prefix "introduce"; match name:ident ";"; expand introduce_alias; }
[[syntax_expander]] static $::meta::tokens use_type(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    if (!$::meta::is_production(type, "type_name"))
        return $::quote { public_schema_failure(); };
    return $::quote {
        if (sizeof($::unquote($::meta::tokens(type))) != sizeof(uptr)) return 0u32;
    };
}
syntax UseType : statement { prefix "use_type"; match value:type ";"; expand use_type; }
[[syntax_expander]] static $::meta::tokens inspect_deferred_type(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    if (!$::meta::is_kind(type, "deferred") || $::meta::child_count(type) != 0uptr)
        return $::quote { public_schema_failure(); };
    return $::quote { ; };
}
syntax UseDeferredType : statement {
    prefix "use_deferred_type"; match value:type ";"; expand inspect_deferred_type;
}
[[syntax_expander]] static $::meta::tokens survive_deferred_type(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    if (!$::meta::is_kind(type, "deferred"))
        return $::quote { public_schema_failure(); };
    return $::quote {
        typedef $::unquote(type)
            $::unquote($::syntax::capture(input, "name"));
    };
}
syntax SurviveDeferredType : statement {
    prefix "survive_deferred_type"; match name:ident value:type ";";
    expand survive_deferred_type;
}
[[syntax_expander]] static $::meta::tokens use_expression(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    if (!$::meta::is_production(value, "assignment_expression"))
        return $::quote { public_schema_failure(); };
    return $::quote { if ($::unquote($::meta::tokens(value)) != 5uptr) return 0u32; };
}
syntax UseExpr : statement { prefix "use_expr"; match value:expr ";"; expand use_expression; }
[[macro]] static $::meta::tokens deferred_expr_type(in $::meta::tokens input) {
    return $::quote { typedef uptr $::unquote(input); };
}
[[syntax_expander]] static $::meta::tokens deferred_expr_drop(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax DeferredExprInner : statement {
    prefix "deferred_expr_inner"; match value:expr ";"; expand deferred_expr_drop;
}
syntax DeferredTypeInner : statement {
    prefix "deferred_type_inner"; match value:type ";"; expand deferred_expr_drop;
}
syntax DeferredStmtInner : statement {
    prefix "deferred_stmt_inner"; match value:stmt; expand deferred_expr_drop;
}
[[syntax_expander]] static $::meta::tokens deferred_expr_owner(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
    $::meta::syntax inner = body;
    $::meta::syntax type_inner = body;
    $::meta::syntax stmt_inner = body;
    for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
        $::meta::syntax statement = $::meta::child(block, at);
        for (uptr nested = 0uptr; nested < $::meta::child_count(statement); ++nested) {
            $::meta::syntax child = $::meta::child(statement, nested);
            for (uptr leaf = 0uptr; leaf < $::meta::child_count(child); ++leaf) {
                $::meta::syntax candidate = $::meta::child(child, leaf);
                if ($::meta::is_extension(candidate, "DeferredExprInner")) inner = candidate;
                if ($::meta::is_extension(candidate, "DeferredTypeInner")) type_inner = candidate;
                if ($::meta::is_extension(candidate, "DeferredStmtInner")) stmt_inner = candidate;
            }
        }
    }
    if (!$::meta::is_extension(inner, "DeferredExprInner"))
        $::syntax::error($::syntax::span(input), "deferred expression owner lost nested extension");
    if (!$::meta::is_extension(type_inner, "DeferredTypeInner"))
        $::syntax::error($::syntax::span(input), "deferred type owner lost nested extension");
    if (!$::meta::is_extension(stmt_inner, "DeferredStmtInner"))
        $::syntax::error($::syntax::span(input), "deferred statement owner lost nested extension");
    $::meta::syntax value = $::syntax::node($::meta::extension_match(inner), "value");
    $::meta::syntax type_value = $::syntax::node($::meta::extension_match(type_inner), "value");
    $::meta::syntax stmt_value = $::syntax::node($::meta::extension_match(stmt_inner), "value");
    if (!$::meta::is_kind(value, "deferred"))
        $::syntax::error($::syntax::span(input), "expected deferred expression capture");
    if (!$::meta::is_kind(type_value, "deferred"))
        $::syntax::error($::syntax::span(input), "expected deferred type capture");
    if (!$::meta::is_kind(stmt_value, "deferred"))
        $::syntax::error($::syntax::span(input), "expected deferred statement capture");
    return $::quote {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        $::unquote($::meta::child(block, 1uptr))
        if ($::unquote(value) != 5uptr) return 0u32;
        typedef $::unquote(type_value) DeferredAlias;
        if (sizeof(DeferredAlias) != sizeof(uptr)) return 0u32;
        $::unquote(stmt_value)
        if ($::unquote($::meta::call_site($::quote { check })) != 5uptr) return 0u32;
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
    };
}
syntax DeferredExprOwner : statement {
    prefix "deferred_expr_owner"; match name:ident body:stmt; expand deferred_expr_owner;
}
[[noinline]] static u32 deferred_expression_splice() {
    syntax DeferredExprOwner, DeferredExprInner, DeferredTypeInner, DeferredStmtInner;
    deferred_expr_owner ExprWord {
        deferred_expr_type!(ExprWord);
        deferred_expr_inner (ExprWord)5uptr;
        deferred_type_inner ExprWord;
        deferred_stmt_inner ExprWord check = 5uptr;
    }
    return 1u32;
}
[[syntax_expander]] static $::meta::tokens use_statement(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    if (!$::meta::is_production(body, "statement"))
        return $::quote { public_schema_failure(); };
    return $::meta::tokens(body);
}
syntax UseStmt : statement { prefix "use_stmt"; match body:stmt; expand use_statement; }
[[syntax_expander]] static $::meta::tokens definition(in $::meta::syntax_match input) {
    $::meta::syntax root = $::syntax::node(input, "body");
    if (!$::meta::is_production(root, "function_definition")) return $::quote {};
    return $::meta::tokens(root);
}
syntax Definition : item { prefix "definition"; match body:function_def; expand definition; }
syntax Definition;
definition [[noinline]] static u32 deferred_definition(in u32 value) {
    introduce_type!(DefinitionWord);
    DefinitionWord result = value + 2u32;
    return result;
}
[[noinline]] static u32 deferred_statements(in u32 value) {
    syntax Deferred, Drop, Introduce, UseType, UseDeferredType, SurviveDeferredType, UseExpr, UseStmt;
    u32 total = 0u32;
    use_deferred_type opaque_type!();
    use_deferred_type const opaque_type!();
    survive_deferred_type DeferredWord word_type!();
    DeferredWord saved_word = 7u16;
    if (sizeof(DeferredWord) != 2uptr || saved_word != 7u16) return 0u32;
    typedef word_type!() DirectWord;
    if (sizeof(DirectWord) != 2uptr) return 0u32;
    const word_type!() qualified_word = 8u16;
    typedef pointer_type!() DirectPointer;
    u32 pointed = 9u32;
    DirectPointer pointer = &pointed;
    if (qualified_word != 8u16 || *pointer != 9u32) return 0u32;
    drop { never_executed!{}; NeverDefined discarded; }
    deferred {
        introduce_type!(LocalWord);
        use_type LocalWord;
        use_expr (LocalWord)5uptr;
        use_stmt if ((LocalWord)0uptr) { return 0u32; } else { total += 0u32; }
        LocalWord first = value + 1u32;
        if (sizeof(LocalWord) != sizeof(uptr)) return 0u32;
        total += first;
    }
    deferred {
        introduce ExtensionWord;
        use_type ExtensionWord;
        ExtensionWord second = value + 3u32;
        if (sizeof(ExtensionWord) != sizeof(uptr)) return 0u32;
        total += second;
    }
    return total;
}
namespace GeneratedDeferredOrder {
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Inner : statement { prefix "generated_inner"; match value:expr ";"; expand discard; }
    static $::meta::syntax find_inner(in $::meta::syntax value) {
        if ($::meta::is_extension(value, "GeneratedDeferredOrder::Inner")) return value;
        for (uptr at = 0uptr; at < $::meta::child_count(value); ++at) {
            $::meta::syntax found = find_inner($::meta::child(value, at));
            if ($::meta::is_extension(found, "GeneratedDeferredOrder::Inner")) return found;
        }
        return value;
    }
    [[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        $::meta::syntax value = $::syntax::node($::meta::extension_match(find_inner(body)), "value");
        $::static_assert($::meta::is_kind(value, "deferred"), "generated expression was classified early");
        return $::quote {
            {
                $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
                $::unquote($::meta::child(block, 1uptr))
                $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
                { typedef u8 $::unquote($::syntax::capture(input, "name"));
                  $::static_assert(sizeof($::unquote(value)) == sizeof(uptr), "original generated alias");
                  return $::unquote(value) + 7uptr; }
            }
        };
    }
    syntax Move : statement { prefix "generated_move"; match name:ident body:stmt; expand move; }
    syntax Inner, Move;
    [[macro]] static $::meta::tokens generate(in $::meta::tokens name) {
        return $::quote {
            [[noinline]] static uptr $::unquote(name)(in uptr seed) {
                generated_move Later { introduce_type!(Later); generated_inner (Later)seed; }
                return 0uptr;
            }
        };
    }
    generate!(first)
    generate!(second)
}

namespace DeferredBoundaries {
    [[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Inner : statement { prefix "boundary_inner"; match body:stmt; expand keep; }
    static u32 count_deferred(in $::meta::syntax value) {
        if ($::meta::is_extension(value, "DeferredBoundaries::Inner")) {
            $::static_assert($::meta::is_kind(
                $::syntax::node($::meta::extension_match(value), "body"), "deferred"),
                "name-dependent statement was classified before its owner");
            return 1u32;
        }
        u32 count = 0u32;
        for (uptr at = 0uptr; at < $::meta::child_count(value); ++at)
            count += count_deferred($::meta::child(value, at));
        return count;
    }
    [[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::static_assert(count_deferred(body) == 1u32, "deferred statement boundary was lost");
        return $::quote { $::unquote(body) };
    }
    syntax Inspect : statement { prefix "boundary_inspect"; match body:stmt; expand inspect; }
    syntax Inner, Inspect;

    [[syntax_expander]] static $::meta::tokens compose(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        return $::quote {
            boundary_inspect {
                introduce_type!(LateWord);
                boundary_inner if ((LateWord)1u32) $::unquote(body)
                    else $::unquote($::syntax::capture(input, "result")) += 97u32;
            }
        };
    }
    syntax Compose : statement { prefix "boundary_compose"; match result:ident body:stmt; expand compose; }

    [[noinline]] global u32 labeled(in u32 selector) {
        u32 result = 0u32;
        boundary_inspect {
            introduce_type!(LateWord);
            boundary_inner global label exported:
                if ((LateWord)selector) result += 7u32; else result += 11u32;
        }
        return result;
    }
    [[noinline]] static u32 spliced() {
        syntax Compose;
        u32 result = 13u32;
        // The outer else cannot attach to the if inside the retained subtree.
        boundary_compose result if (0u32) result += 99u32;
        return result;
    }
}

namespace CopiedDeferredBlocks {
    [[macro]] static $::meta::tokens declare_local(in $::meta::tokens input) {
        return $::quote { uptr $::unquote($::meta::call_site($::quote { local })) = 3uptr; };
    }
    [[macro]] static $::meta::tokens declare_static(in $::meta::tokens input) {
        return $::quote { static uptr $::unquote($::meta::call_site($::quote { local })) = 3uptr; };
    }
    [[macro]] static $::meta::tokens binder(in $::meta::tokens input) {
        return $::quote { $::unquote($::meta::call_site($::quote { local })) };
    }
    [[macro]] static $::meta::tokens statement_head(in $::meta::tokens input) {
        return $::quote { uptr $::unquote($::meta::call_site($::quote { local })) };
    }
    [[macro]] static $::meta::tokens return_head(in $::meta::tokens input) {
        return $::quote { return };
    }
    [[syntax_expander]] static $::meta::tokens repeat(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        return $::quote { { $::unquote(body) $::unquote(body) } };
    }
    syntax Repeat : statement { prefix "repeat_deferred"; match body:stmt; expand repeat; }
    [[syntax_expander]] static $::meta::tokens once(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Once : statement { prefix "once_deferred"; match body:stmt; expand once; }

    [[noinline]] static uptr automatic(in uptr seed) {
        syntax Repeat;
        uptr total = 0uptr;
        uptr local = seed;
        repeat_deferred { declare_local!(); local += seed; total += local; }
        return total + local;
    }
    [[noinline]] static uptr persistent() {
        syntax Repeat;
        uptr total = 0uptr;
        repeat_deferred { declare_static!(); local += 1uptr; total += local; }
        return total;
    }
    [[noinline]] static uptr declaration_list(in uptr seed) {
        syntax Repeat;
        uptr total = 0uptr;
        repeat_deferred { uptr binder!() = seed, next = local + 3uptr; total += next; }
        return total;
    }
    [[noinline]] static uptr statement_list(in uptr seed) {
        syntax Repeat;
        uptr total = 0uptr;
        repeat_deferred { statement_head!() = seed, next = local + 5uptr; total += next; }
        return total;
    }
    [[noinline]] static uptr returned(in uptr seed) {
        syntax Repeat;
        repeat_deferred return_head!() + seed;
    }
    [[noinline]] static uptr direct_declaration(in uptr seed) {
        syntax Once;
        once_deferred statement_head!() = seed, next = local + 7uptr;
        return next;
    }
}

namespace CopiedDeferredBindings {
    [[macro]] static $::meta::tokens declare(in $::meta::tokens name) {
        return $::quote {
            struct $::unquote(name) { uptr field; };
            typedef struct $::unquote(name) $::unquote(name);
            uptr $::unquote($::meta::call_site($::quote { local })) =
                ++$::unquote($::meta::call_site($::quote { seed }));
        };
    }
    [[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
        return $::quote { ; };
    }
    syntax Use : statement {
        prefix "use_bindings"; match alias:type "," tag:type "," value:expr ";"; expand discard;
    }
    [[syntax_expander]] static $::meta::tokens repeat(in $::meta::syntax_match input) {
        $::meta::syntax body = $::syntax::node(input, "body");
        $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
        $::meta::syntax inner = body;
        for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
            $::meta::syntax child = $::meta::child(block, at);
            while ($::meta::is_kind(child, "core") && $::meta::child_count(child) == 1uptr)
                child = $::meta::child(child, 0uptr);
            if ($::meta::is_extension(child, "CopiedDeferredBindings::Use")) inner = child;
        }
        $::meta::syntax_match captured = $::meta::extension_match(inner);
        $::meta::syntax alias = $::syntax::node(captured, "alias");
        $::meta::syntax tag = $::syntax::node(captured, "tag");
        $::meta::syntax value = $::syntax::node(captured, "value");
        $::static_assert($::meta::is_kind(alias, "deferred") &&
            $::meta::is_kind(tag, "deferred") && $::meta::is_kind(value, "deferred"),
            "copies must replay the original deferred nodes, not newly matched captures");
        $::meta::tokens alias_tokens = $::quote { $::unquote(alias) };
        $::meta::tokens tag_tokens = $::quote { $::unquote(tag) };
        $::meta::tokens value_tokens = $::quote { $::unquote(value) };
        if ($::syntax::is_variant($::syntax::at(input, "mode", 0uptr), "text")) {
            alias_tokens = $::meta::tokens(alias);
            tag_tokens = $::meta::tokens(tag);
            value_tokens = $::meta::tokens(value);
        }
        $::meta::tokens one = $::quote {
            $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
            $::unquote($::meta::tokens($::meta::child(block, 1uptr)))
            $::unquote(alias_tokens) object = { $::unquote(value_tokens) };
            $::unquote(tag_tokens) *pointer = &object;
            $::unquote($::syntax::capture(input, "total")) += pointer->field;
            $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
        };
        return $::quote { { $::unquote(one) $::unquote(one) } };
    }
    syntax Repeat : statement {
        prefix "repeat_bindings";
        match mode:choice(tree:("tree") | text:("text")) total:ident body:stmt;
        expand repeat;
    }
    [[noinline]] static uptr structured(in uptr initial) {
        syntax Use, Repeat;
        uptr seed = initial;
        uptr total = 0uptr;
        repeat_bindings tree total {
            declare!(Record); use_bindings Record, struct Record, ((Record *)0 ? 0uptr : local);
        }
        return total;
    }
    [[noinline]] static uptr projected(in uptr initial) {
        syntax Use, Repeat;
        uptr seed = initial;
        uptr total = 0uptr;
        repeat_bindings text total {
            declare!(Record); use_bindings Record, struct Record, ((Record *)0 ? 0uptr : local);
        }
        return total;
    }
}

#include "syntax_exited_scope.x"
#define ExitedDeferredCopies ProjectedExitedDeferredCopies
#define TEST_PROJECT_EXITED_ALL
#include "syntax_exited_scope.x"
#undef TEST_PROJECT_EXITED_ALL
#undef ExitedDeferredCopies
#include "syntax_implicit_tags.x"
#define ImplicitTagCopies ProjectedImplicitTagCopies
#define TEST_PROJECT_IMPLICIT
#include "syntax_implicit_tags.x"
#undef TEST_PROJECT_IMPLICIT
#undef ImplicitTagCopies
#define TEST_GENERATE_IMPLICIT
#define ImplicitTagCopies GeneratedImplicitTagCopies
#include "syntax_implicit_tags.x"
#undef ImplicitTagCopies
#define ImplicitTagCopies GeneratedProjectedImplicitTagCopies
#define TEST_PROJECT_IMPLICIT
#include "syntax_implicit_tags.x"
#undef TEST_PROJECT_IMPLICIT
#undef ImplicitTagCopies
#undef TEST_GENERATE_IMPLICIT
#include "syntax_ambiguous_scopes.x"

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
// Share the explicit platform-ABI MIPS startup with syntax_raw.x.
global u32 syntax_raw_entry() {
    if (deferred_statements(5u32) != 14u32 || deferred_definition(7u32) != 9u32 ||
        deferred_expression_splice() != 1u32 || DeferredBoundaries::labeled(0u32) != 11u32 ||
        DeferredBoundaries::labeled(1u32) != 7u32 || DeferredBoundaries::spliced() != 13u32 ||
        GeneratedDeferredOrder::first(300uptr) != 307uptr ||
        GeneratedDeferredOrder::second(600uptr) != 607uptr ||
        CopiedDeferredBlocks::automatic(300uptr) != 906uptr ||
        CopiedDeferredBlocks::automatic(600uptr) != 1806uptr ||
        CopiedDeferredBlocks::persistent() != 8uptr ||
        CopiedDeferredBlocks::persistent() != 10uptr ||
        CopiedDeferredBlocks::declaration_list(300uptr) != 606uptr ||
        CopiedDeferredBlocks::declaration_list(600uptr) != 1206uptr ||
        CopiedDeferredBlocks::statement_list(300uptr) != 610uptr ||
        CopiedDeferredBlocks::returned(600uptr) != 600uptr ||
        CopiedDeferredBlocks::direct_declaration(900uptr) != 907uptr ||
        CopiedDeferredBindings::structured(300uptr) != 603uptr ||
        CopiedDeferredBindings::projected(600uptr) != 1203uptr ||
        AmbiguousDeferredCopies::run() != 7uptr ||
        ExitedDeferredCopies::plain() != 14uptr ||
        ExitedDeferredCopies::instances() != 16uptr ||
        ExitedDeferredCopies::instances() != 18uptr ||
        ProjectedExitedDeferredCopies::plain() != 14uptr ||
        ProjectedExitedDeferredCopies::instances() != 16uptr ||
        ProjectedExitedDeferredCopies::instances() != 18uptr ||
        ImplicitTagCopies::plain() != 2uptr ||
        ImplicitTagCopies::instances() != 2uptr ||
        ImplicitTagCopies::instances() != 4uptr ||
        ProjectedImplicitTagCopies::plain() != 2uptr ||
        ProjectedImplicitTagCopies::instances() != 2uptr ||
        ProjectedImplicitTagCopies::instances() != 4uptr ||
        GeneratedImplicitTagCopies::plain() != 2uptr ||
        GeneratedImplicitTagCopies::instances() != 2uptr ||
        GeneratedImplicitTagCopies::instances() != 4uptr ||
        GeneratedProjectedImplicitTagCopies::plain() != 2uptr ||
        GeneratedProjectedImplicitTagCopies::instances() != 2uptr ||
        GeneratedProjectedImplicitTagCopies::instances() != 4uptr)
        return 0u32;
    return 61u32;
}
