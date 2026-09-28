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
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
// Share the explicit platform-ABI MIPS startup with syntax_raw.x.
global u32 syntax_raw_entry() {
    if (deferred_statements(5u32) != 14u32 || deferred_definition(7u32) != 9u32)
        return 0u32;
    return 61u32;
}
