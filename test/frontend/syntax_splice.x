// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[syntax_expander]] static $::meta::tokens multiply(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    // A splice is one token tree through copying, slicing and concatenation.
    $::meta::tokens head = $::quote { $::unquote(value) };
    head = $::meta::slice(head, 0uptr, 1uptr);
    return $::meta::concat(head, $::quote { * 3u32 });
}
syntax Multiply : expression {
    prefix "multiplied"; match "(" value:expr ")"; expand multiply;
}

[[syntax_expander]] static $::meta::tokens parse_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    return $::quote { $::unquote(parsed) + 1u32 };
}
syntax ParseThenSplice : expression {
    prefix "parse_splice"; match "(" value:expr ")"; expand parse_then_splice;
}

[[syntax_expander]] static $::meta::tokens project_then_splice(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) * 3u32 }, $::syntax::context(input));
    return $::meta::concat($::meta::tokens(parsed), $::quote { + 1u32 });
}
syntax ProjectThenSplice : expression {
    prefix "project_splice"; match "(" value:expr ")"; expand project_then_splice;
}

[[syntax_expander]] static $::meta::tokens inspect_without_expanding(in $::meta::syntax_match input) {
    $::meta::syntax value = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("expr",
        $::quote { $::unquote(value) + 1u32 }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "assignment_expression"))
        return $::quote { 0u32 };
    // The nested invocation belongs to the discarded node, not this owner.
    return $::quote { 9u32 };
}
syntax InspectWithoutExpanding : expression {
    prefix "inspect_splice"; match "(" value:expr ")"; expand inspect_without_expanding;
}

[[syntax_expander]] static $::meta::tokens transplant_statement(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax parsed = $::meta::parse("stmt",
        $::quote { $::unquote(body) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "statement"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(body) };
}
syntax Transplant : statement {
    prefix "transplant"; match body:stmt; expand transplant_statement;
}
[[syntax_expander]] static $::meta::tokens inner_statement(in $::meta::syntax_match input) {
    return $::quote { u32 $::unquote($::syntax::capture(input, "name")) = 13u32; };
}
syntax InnerStatement : statement { prefix "inner_statement"; match name:ident ";"; expand inner_statement; }
[[syntax_expander]] static $::meta::tokens transplant_inner(in $::meta::syntax_match input) {
    $::meta::syntax root = $::syntax::node(input, "body");
    $::meta::syntax ordinary = $::meta::child(root, 0uptr);
    $::meta::syntax extension = $::meta::child(ordinary, 0uptr);
    if (!$::meta::is_kind(extension, "extension"))
        return $::quote { public_schema_failure(); };
    $::meta::syntax parsed = $::meta::parse("stmt",
        $::quote { $::unquote(extension) }, $::syntax::context(input));
    if (!$::meta::is_production(parsed, "statement") ||
        !$::meta::is_kind($::meta::child(parsed, 0uptr), "extension"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(extension) };
}
syntax TransplantInner : statement {
    prefix "transplant_inner"; match body:stmt; expand transplant_inner;
}
[[syntax_expander]] static $::meta::tokens shadow_transplant(in $::meta::syntax_match input) {
    $::meta::tokens local = $::meta::call_site($::meta::parse("amount"));
    return $::quote { {
        u32 $::unquote(local) = 99u32;
        $::unquote($::syntax::node(input, "body"))
        if ($::unquote($::syntax::capture(input, "result")) != 4u32) return 0u32;
    } };
}
syntax ShadowTransplant : statement {
    prefix "shadow_transplant"; match result:ident body:stmt; expand shadow_transplant;
}
[[syntax_expander]] static $::meta::tokens transplant_type(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    $::meta::syntax parsed = $::meta::parse("type",
        $::quote { $::unquote(type) }, $::syntax::context(input));
    $::meta::syntax specifier = $::meta::child(
        $::meta::child($::meta::child(parsed, 0uptr), 0uptr), 0uptr);
    if (!$::meta::is_production(parsed, "type_name") ||
        !$::meta::is_production(specifier, "type_specifier") ||
        !$::meta::is_production($::meta::child(specifier, 0uptr), "type_name"))
        return $::quote { public_schema_failure(); };
    return $::quote { $::unquote(parsed) $::unquote($::syntax::capture(input, "name")); };
}
syntax TransplantType : statement {
    prefix "transplant_type"; match name:ident value:type ";"; expand transplant_type;
}
[[syntax_expander]] static $::meta::tokens shadow_type(in $::meta::syntax_match input) {
    $::meta::tokens alias = $::meta::call_site($::meta::parse("CapturedAlias"));
    $::meta::tokens name = $::syntax::capture(input, "name");
    return $::quote { {
        typedef u32 $::unquote(alias);
        $::unquote($::syntax::node(input, "value")) $::unquote(name);
        if (sizeof($::unquote(name)) != 2uptr) return 0u32;
    } };
}
syntax ShadowType : statement {
    prefix "shadow_type"; match name:ident value:type ";"; expand shadow_type;
}
[[syntax_expander]] static $::meta::tokens transplant_specifier(in $::meta::syntax_match input) {
    $::meta::syntax type = $::syntax::node(input, "value");
    $::meta::syntax specifier = $::meta::child(
        $::meta::child($::meta::child(type, 0uptr), 0uptr), 0uptr);
    return $::quote { $::unquote(specifier) $::unquote($::syntax::capture(input, "name")); };
}
syntax TransplantSpecifier : statement {
    prefix "transplant_specifier"; match name:ident value:type ";"; expand transplant_specifier;
}
[[syntax_expander]] static $::meta::tokens copy_declaration(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax CopyDeclaration : statement {
    prefix "copy_declaration"; match body:declaration; expand copy_declaration;
}
syntax CopyExternal : item {
    prefix "copy_external"; match body:declaration; expand copy_declaration;
}
syntax CopyExternal;
copy_external global u32 external_spliced_value = 17u32;
copy_external typedef u16 ExternalSplicedType;
[[syntax_expander]] static $::meta::tokens move_external_declaration(
    in $::meta::syntax_match input) {
    return $::quote { namespace Destination {
        $::unquote($::syntax::node(input, "body"))
    } };
}
syntax MoveExternal : item {
    prefix "move_external"; match body:declaration; expand move_external_declaration;
}
namespace Source {
    typedef u16 OriginType;
    namespace Destination { typedef u32 OriginType; }
    syntax MoveExternal;
    move_external global OriginType moved;
}
[[macro]] static $::meta::tokens declare_spliced(in $::meta::tokens name) {
    return $::quote { u32 $::unquote(name) = 11u32; };
}
[[macro]] static $::meta::tokens declarator_name(in $::meta::tokens name) {
    return $::quote { $::unquote(name) };
}
[[macro]] static $::meta::tokens declarator_pointer(in $::meta::tokens name) {
    return $::quote { *$::unquote(name) };
}
[[macro]] static $::meta::tokens declarator_array(in $::meta::tokens ignored) {
    return $::quote { [2] };
}
[[macro]] static $::meta::tokens declarator_function(in $::meta::tokens name) {
    return $::quote { (in u32 $::unquote(name)) };
}
[[noinline]] static u32 suffix_function declarator_function!(value) {
    return value + 3u32;
}

#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    syntax Multiply;
    syntax ParseThenSplice;
    syntax ProjectThenSplice;
    syntax InspectWithoutExpanding;
    syntax Transplant;
    syntax InnerStatement, TransplantInner;
    syntax ShadowTransplant;
    syntax TransplantType;
    syntax ShadowType;
    syntax TransplantSpecifier;
    syntax CopyDeclaration;
    u32 amount = 4u32;
    u32 declarator_name!(named_by_macro) = 5u32;
    u32 declarator_pointer!(pointer_by_macro) = &amount;
    u32 array_by_macro declarator_array!() = { 2u32, 3u32 };
    copy_declaration register u32 local_spliced_value = external_spliced_value;
    copy_declaration u32 copied_array declarator_array!() = { 1u32, 8u32 };
    copy_declaration typedef u16 LocalSplicedType;
    LocalSplicedType local_spliced_alias = 6u16;
    ExternalSplicedType external_spliced_alias = 7u16;
    transplant u32 moved = amount + 1u32;
    transplant typedef u32 MovedType;
    transplant struct SplicedRecord { u32 value; } record = { 7u32 };
    transplant enum SplicedMode { spliced_mode_value = 3 } mode = spliced_mode_value;
    transplant declare_spliced!(macro_moved);
    transplant_inner inner_statement inner_moved;
    shadow_transplant relocated u32 relocated = amount;
    transplant_type typed_value u32;
    typed_value = 7u32;
    transplant_type pointer_value u32 *;
    pointer_value = &amount;
    transplant_type array_value u32 [2];
    array_value[0] = 3u32;
    array_value[1] = 4u32;
    transplant_type record_value struct SplicedTypeTag { u32 field; };
    record_value.field = 12u32;
    struct SplicedTypeTag *record_pointer = &record_value;
    typedef u16 CapturedAlias;
    shadow_type alias_value CapturedAlias;
    transplant_specifier base_value u16;
    base_value = 8u16;
    MovedType checked = moved;
    struct SplicedRecord later = { 9u32 };
    enum SplicedMode later_mode = spliced_mode_value;
    u32 __cross_syntax_splice = 2u32; // The marker spelling alone is ordinary source.
    // Structured grouping: (4 + 1) * 3; explicit projection: 4 + 1 * 3.
    if (multiplied (amount + 1u32) != 15u32 ||
        parse_splice (amount + 1u32) != 16u32 ||
        project_splice (amount + 1u32) != 8u32 ||
        inspect_splice (missing_macro! { 3u32 }) != 9u32 ||
        __cross_syntax_splice != 2u32 || checked != 5u32 ||
        record.value != 7u32 || later.value != 9u32 ||
        mode != spliced_mode_value || later_mode != spliced_mode_value ||
        macro_moved != 11u32 || inner_moved != 13u32 ||
        typed_value != 7u32 || *pointer_value != amount ||
        array_value[0] + array_value[1] != 7u32 ||
        record_pointer->field != 12u32 || base_value != 8u16 ||
        local_spliced_value != 17u32 || local_spliced_alias != 6u16 ||
        external_spliced_alias != 7u16 || named_by_macro != 5u32 ||
        *pointer_by_macro != amount || array_by_macro[1] != 3u32 ||
        copied_array[1] != 8u32 ||
        suffix_function(5u32) != 8u32 ||
        sizeof(Source::Destination::moved) != 2uptr)
        return 0u32;
    return 61u32;
}
