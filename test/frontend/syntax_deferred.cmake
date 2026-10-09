# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(discard "[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) { return $::quote { ; }; }")
set(copy "[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) { return $::meta::tokens($::syntax::node(input, \"body\")); }")
set(owner "syntax Owner : statement { prefix \"owner\"; match body:stmt; expand discard; }")

function(reject case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES ":[0-9]+:[0-9]+: error:" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: (error|note):")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

function(reject_expansion case expected source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: note:")
        message(FATAL_ERROR "${case} expansion was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

function(accept case source)
    set(input "${OUTPUT}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S "${input}" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${case} failed\n${out}\n${err}")
    endif()
endfunction()

set(deferred_expression_source [=[
[[macro]] static $::meta::tokens define_type(in $::meta::tokens input) {
    return $::quote { typedef uptr $::unquote(input); };
}
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Inner : statement { prefix "inner"; match value:expr ";"; expand drop; }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    $::meta::syntax block = $::meta::child($::meta::child(body, 0uptr), 0uptr);
    if ($::meta::is_kind(block, "deferred"))
        $::syntax::error($::syntax::span(input), "block was deferred");
    if (!$::meta::is_production(block, "compound_statement"))
        $::syntax::error($::syntax::span(input), "block was not compound");
    $::meta::syntax inner = body;
    for (uptr at = 0uptr; at < $::meta::child_count(block); ++at) {
        $::meta::syntax statement = $::meta::child(block, at);
        if ($::meta::is_extension(statement, "Inner")) inner = statement;
        for (uptr nested = 0uptr; nested < $::meta::child_count(statement); ++nested) {
            $::meta::syntax child = $::meta::child(statement, nested);
            if ($::meta::is_extension(child, "Inner")) inner = child;
            for (uptr leaf = 0uptr; leaf < $::meta::child_count(child); ++leaf) {
                $::meta::syntax candidate = $::meta::child(child, leaf);
                if ($::meta::is_extension(candidate, "Inner")) inner = candidate;
            }
        }
    }
    if (!$::meta::is_extension(inner, "Inner"))
        $::syntax::error($::syntax::span(input), "inner extension not found");
    $::meta::syntax value = $::syntax::node($::meta::extension_match(inner), "value");
    if (!$::meta::is_kind(value, "deferred"))
        $::syntax::error($::syntax::span(input), "expression was not deferred");
    // Keep the captured brace identity and its original leading macro.
    return $::quote {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        $::unquote($::meta::child(block, 1uptr))
        if ($::unquote(value) != 5uptr) return 0u32;
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
    };
}
syntax Owner : statement { prefix "owner"; match name:ident body:stmt; expand expand; }
global u32 entry() {
    syntax Owner, Inner;
    owner Later { define_type!(Later); inner (Later)5uptr; }
    return 1u32;
}
]=])
accept(deferred_expression_splice "${deferred_expression_source}")
string(REPLACE "typedef uptr $::unquote(input);"
    "typedef uptr $::unquote(input); u32 additional = 0u32;"
    deferred_multiple_items_source "${deferred_expression_source}")
reject_expansion(structured_macro_multiple_items "structured statement splice must contain one complete statement"
    "${deferred_multiple_items_source}")
string(REPLACE "$::unquote($::meta::child(block, 1uptr))"
    "$::unquote($::meta::tokens($::meta::child(block, 1uptr)))"
    deferred_multiple_items_source "${deferred_multiple_items_source}")
accept(deferred_expression_after_textual_items "${deferred_multiple_items_source}")

# Keep the original block identity in both placements, but discard its
# declaration in the second. Deferred lookup must not borrow the first copy's
# alias, tag, local or enumerator, even though the lexical token IDs agree.
string(REPLACE
    "return $::quote {\n        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))"
    "$::meta::tokens first = $::quote {\n        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))"
    missing_copied_binding "${deferred_expression_source}")
string(REPLACE "    };\n}\nsyntax Owner" [=[    };
    $::meta::tokens missing = $::quote {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        if ($::unquote(value) != 5uptr) return 0u32;
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
    };
    return $::quote { { $::unquote(first) $::unquote(missing) } };
}
syntax Owner]=] missing_copied_binding "${missing_copied_binding}")
foreach(kind alias value enumerator tag)
    set(missing "${missing_copied_binding}")
    set(expected "expected Cross type|expected '\\)'|structured expression splice must contain one complete expression")
    if(kind STREQUAL "value" OR kind STREQUAL "enumerator")
        if(kind STREQUAL "value")
            set(declaration "uptr $::unquote(input) = 5uptr;")
        else()
            set(declaration "enum { $::unquote(input) = 5uptr };")
        endif()
        string(REPLACE "typedef uptr $::unquote(input);" "${declaration}" missing "${missing}")
        string(REPLACE "inner (Later)5uptr;" "inner Later;" missing "${missing}")
        set(expected "unresolved name 'Later'")
    elseif(kind STREQUAL "tag")
        string(REPLACE "typedef uptr $::unquote(input);" "struct $::unquote(input) { uptr field; };"
            missing "${missing}")
        string(REPLACE "inner (Later)5uptr;" "inner sizeof(struct Later);" missing "${missing}")
        set(expected "sizeof requires a complete object type")
    endif()
    string(REPLACE "$::unquote(first) $::unquote(missing)" "$::unquote(first) $::unquote(first)"
        present "${missing}")
    accept(copied_${kind}_present "${present}")
    reject_expansion(copied_${kind}_missing "${expected}" "${missing}")
    string(REPLACE "$::unquote(value)" "$::unquote($::meta::tokens(value))"
        projected_missing "${missing}")
    reject_expansion(copied_${kind}_missing_projected "${expected}" "${projected_missing}")
endforeach()

accept(discard_expression_operator_macro [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    if (!$::meta::is_kind($::syntax::node(input, "value"), "deferred"))
        $::syntax::error($::syntax::span(input), "operator fragment was not deferred");
    return $::quote { 7u32 };
}
syntax Discard : expression {
    prefix "discard"; match "(" value:expr ")"; expand discard;
}
syntax Discard;
global u32 entry() { return discard(1u32 unknown!(+) 2u32); }
]=])
accept(discard_qualified_type_fragment [=[
namespace names { typedef u32 Value; }
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    if (!$::meta::is_kind($::syntax::node(input, "value"), "deferred"))
        $::syntax::error($::syntax::span(input), "qualified type fragment was not deferred");
    return $::quote { ; };
}
syntax Discard : statement { prefix "discard"; match value:type ";"; expand discard; }
global u32 entry() {
    syntax Discard;
    discard names unknown!(::Value);
    return 1u32;
}
]=])
accept(type_probe_keeps_expression_owner_input [=[
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    while ($::meta::is_kind(node, "core") && $::meta::child_count(node) == 1uptr)
        node = $::meta::child(node, 0uptr);
    if (!$::meta::is_kind(node, "macro"))
        $::syntax::error($::syntax::span(input), "type probe expanded owner input");
    return $::quote { 7u32 };
}
syntax Inspect : expression { prefix "inspect"; match value:expr; expand inspect; }
global u32 entry() {
    syntax Inspect;
    return (inspect unknown!{foreign tokens}) + sizeof(inspect unknown!{foreign tokens});
}
]=])
accept(expression_prefix_precedes_type_probe [=[
namespace Types { typedef u64 T; }
using Types;
[[syntax_expander]] static $::meta::tokens value(in $::meta::syntax_match input) {
    return $::quote { 7u16 };
}
syntax Value : expression { prefix "T"; match "*"; expand value; }
syntax Value;
static T *forced_type(in T *value) { return value; }
$::static_assert(sizeof(T *) == sizeof(u16), "active prefix lost to alias");
$::static_assert($::alignof(T *) == $::alignof(u16), "active prefix lost to alignment probe");
$::static_assert((T *) + 3u16 == 10u16, "active prefix lost to cast probe");
$::static_assert(sizeof(Types::T *) == sizeof(void *), "qualified type was intercepted");
static u32 value_argument<u32 N>() { return N; }
$::static_assert(value_argument<T *>() == 7u32, "active prefix lost to generic argument probe");
static u32 expression_statement() { T *; return 1u32; }
$::static_assert(expression_statement() == 1u32, "active prefix lost to declaration probe");
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    $::meta::syntax leaf = node;
    while ($::meta::is_kind(leaf, "core") && $::meta::child_count(leaf) == 1uptr)
        leaf = $::meta::child(leaf, 0uptr);
    if (!$::meta::is_kind(leaf, "extension"))
        $::syntax::error($::syntax::span(input), "prefix was classified as a type in capture");
    return $::quote { $::unquote(node) };
}
syntax Copy : expression { prefix "copied"; match "(" value:expr ")"; expand copy; }
syntax Copy;
$::static_assert(copied(T *) == 7u16, "captured active prefix lost its identity");
[[syntax_expander]] static $::meta::tokens type_size(in $::meta::syntax_match input) {
    $::meta::syntax node = $::syntax::node(input, "value");
    if (!$::meta::is_production(node, "type_name"))
        $::syntax::error($::syntax::span(input), "forced type capture dispatched expression syntax");
    return $::quote { sizeof($::unquote(node)) };
}
syntax TypeSize : expression { prefix "type_size"; match "(" value:type ")"; expand type_size; }
syntax TypeSize;
$::static_assert(type_size(T *) == sizeof(void *), "type capture lost required-type classification");
[[macro]] static $::meta::tokens generic_name(in $::meta::tokens input) { return input; }
static T identity<generic_name!(T)>(in T value) [[aligned(sizeof(T *))]] { return value; }
$::static_assert(identity(11u32) == 11u32, "generic header changed expression dispatch");
]=])
reject(active_prefix_does_not_fall_back_to_type "syntax-match error" [=[
typedef u32 T;
[[syntax_expander]] static $::meta::tokens value(in $::meta::syntax_match input) {
    return $::quote { 7u16 };
}
syntax Value : expression { prefix "T"; match "*"; expand value; }
global u32 entry() { syntax Value; return sizeof(T); }
]=])
accept(statement_prefix_does_not_override_type_probe [=[
typedef u64 T;
[[syntax_expander]] static $::meta::tokens statement(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Statement : statement { prefix "T"; match "*"; expand statement; }
global u32 entry() {
    syntax Statement;
    return sizeof(T *) == sizeof(void *);
}
]=])
function(as_deferred_type result source)
    string(REPLACE "match value:expr" "match value:type" type_source "${source}")
    string(REPLACE "inner (Later)5uptr;" "inner Later;" type_source "${type_source}")
    string(REPLACE "if ($::unquote(value) != 5uptr) return 0u32;"
        "typedef $::unquote(value) Composed;\n        $::static_assert(sizeof(Composed) == sizeof(uptr), \"original type alias selected\");"
        type_source "${type_source}")
    string(REPLACE "$::static_assert(sizeof($::unquote(value)) == sizeof(uptr), \"original block alias selected\");"
        "typedef $::unquote(value) Composed;\n          $::static_assert(sizeof(Composed) == sizeof(uptr), \"original type alias selected\");"
        type_source "${type_source}")
    set(${result} "${type_source}" PARENT_SCOPE)
endfunction()
as_deferred_type(deferred_type_source "${deferred_expression_source}")
accept(deferred_type_original_block "${deferred_type_source}")
string(REPLACE "match value:expr \";\"" "match value:stmt"
    deferred_statement_source "${deferred_expression_source}")
string(REPLACE "inner (Later)5uptr;" "inner Later object = 5uptr;"
    deferred_statement_source "${deferred_statement_source}")
string(REPLACE "if ($::unquote(value) != 5uptr) return 0u32;"
    "$::unquote(value)\n        if ($::unquote($::meta::call_site($::quote { object })) != 5uptr) return 0u32;"
    deferred_statement_source "${deferred_statement_source}")
accept(deferred_statement_original_block "${deferred_statement_source}")
string(REPLACE "match value:expr \";\"" "match value:declaration"
    deferred_declaration_source "${deferred_expression_source}")
string(REPLACE "inner (Later)5uptr;" "inner Later object = 5uptr;"
    deferred_declaration_source "${deferred_declaration_source}")
string(REPLACE "if ($::unquote(value) != 5uptr) return 0u32;"
    "$::unquote(value)\n        if ($::unquote($::meta::call_site($::quote { object })) != 5uptr) return 0u32;"
    deferred_declaration_source "${deferred_declaration_source}")
accept(deferred_declaration_original_block "${deferred_declaration_source}")
accept(discard_nested_declarator_macro [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Drop : statement { prefix "drop"; match body:declaration; expand discard; }
global u32 entry() {
    syntax Drop;
    drop u32 discarded missing_suffix!();
    return 1u32;
}
]=])
foreach(label "plain:" "label local:" "global label exported:"
              "[[vendor::hint]] global label decorated:" "case (NewType)1u32:" "default:")
    string(MD5 label_case "${label}")
    accept(deferred_label_${label_case} "
${discard}
syntax Inner : statement { prefix \"inner\"; match body:stmt; expand discard; }
syntax Outer : statement { prefix \"outer\"; match body:stmt; expand discard; }
global u32 entry() {
    syntax Inner, Outer;
    outer { unknown!{}; inner ${label} if ((NewType)1u32) NewType first; else NewType second; }
    return 1u32;
}")
endforeach()
accept(survive_nested_declarator_macro [=[
[[macro]] static $::meta::tokens array_suffix(in $::meta::tokens ignored) {
    return $::quote { [2] };
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Copy : statement { prefix "copy"; match body:declaration; expand copy; }
global u32 entry() {
    syntax Copy;
    copy u32 values array_suffix!() = { 2u32, 3u32 };
    return values[1];
}
]=])
accept(discard_nested_function_parameter_macro [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax DropDecl : item { prefix "drop_decl"; match body:function_decl; expand discard; }
syntax DropDef : item { prefix "drop_def"; match body:function_def; expand discard; }
syntax DropHeader : item {
    prefix "drop_header"; match head:function_header body:block; expand discard;
}
syntax DropDecl, DropDef, DropHeader;
drop_decl static u32 one(missing_parameter!());
drop_decl static u32 (*callback(missing_parameter!()))(in u32 value);
drop_def static u32 two(missing_parameter!()) { return 2u32; }
drop_header static u32 three(missing_parameter!()) { return 3u32; }
drop_def static struct Inline { u32 value; } four(missing_parameter!()) {
    return { 4u32 };
}
global u32 entry() { return 1u32; }
]=])
accept(survive_nested_function_parameter_macro [=[
[[macro]] static $::meta::tokens parameter(in $::meta::tokens name) {
    return $::quote { in u32 $::unquote(name) };
}
[[syntax_expander]] static $::meta::tokens copy_def(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    if (!$::meta::is_kind(body, "deferred"))
        $::syntax::error($::syntax::span(input), "function was parsed too early");
    return $::meta::tokens(body);
}
syntax CopyDef : item { prefix "copy_def"; match body:function_def; expand copy_def; }
syntax CopyDef;
copy_def static u32 helper(parameter!(value)) { return value + 1u32; }
global u32 entry() { return helper(4u32); }
]=])
reject(function_decl_cannot_capture_definition
    "syntax-match error for active prefix" [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Decl : item { prefix "decl"; match body:function_decl; expand discard; }
syntax Decl;
decl static u32 wrong(missing_parameter!()) { return 1u32; }
]=])
reject(function_def_cannot_capture_prototype
    "syntax-match error for active prefix" [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Def : item { prefix "def"; match body:function_def; expand discard; }
syntax Def;
def static u32 wrong(missing_parameter!());
]=])
reject(function_decl_cannot_capture_pointer_object
    "syntax-match error for active prefix" [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Decl : item { prefix "decl"; match body:function_decl; expand discard; }
syntax Decl;
decl static u32 (*object)(missing_parameter!());
]=])
reject(function_decl_cannot_guess_macro_shape
    "syntax-match error for active prefix" [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Decl : item { prefix "decl"; match body:function_decl; expand discard; }
syntax Decl;
decl static u32 unknown_declarator!();
]=])
reject(function_decl_cannot_capture_declarator_list
    "syntax-match error for active prefix" [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Decl : item { prefix "decl"; match body:function_decl; expand discard; }
syntax Decl;
decl static u32 one(missing_parameter!()), two();
]=])
accept(result_location_declarator_macro [=[
[[macro]] static $::meta::tokens result_location(in $::meta::tokens ignored) {
    return $::quote { -> "rax" };
}
global u64 entry() result_location!() { return 1u64; }
]=])
reject_expansion(nested_declarator_macro_multiple_statements
    "structured statement splice must contain one complete statement" [=[
[[macro]] static $::meta::tokens extra(in $::meta::tokens ignored) {
    return $::quote { [2]; u32 another = 1u32 };
}
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Copy : statement { prefix "copy"; match body:declaration; expand copy; }
global u32 entry() {
    syntax Copy;
    copy u32 values extra!();
    return 0u32;
}
]=])
reject_expansion(nested_declarator_macro_recursion
    "syntax/procedural expansion depth exceeded" [=[
[[macro]] static $::meta::tokens again(in $::meta::tokens ignored) {
    return $::quote { again!() };
}
[[syntax_expander]] static $::meta::tokens dummy(in $::meta::syntax_match input) {
    return $::quote { ; };
}
syntax Dummy : statement { prefix "dummy"; match ";"; expand dummy; }
global u32 entry() { syntax Dummy; u32 value again!(); return 0u32; }
]=])
function(as_deferred_statement_alias result source)
    string(REPLACE "match value:expr \";\"" "match value:stmt" statement_source "${source}")
    string(REPLACE "inner (Later)5uptr;" "inner typedef Later Composed;"
        statement_source "${statement_source}")
    string(REPLACE "if ($::unquote(value) != 5uptr) return 0u32;"
        "$::unquote(value)\n        $::static_assert(sizeof($::unquote($::meta::call_site($::quote { Composed }))) == sizeof(uptr), \"original statement alias selected\");"
        statement_source "${statement_source}")
    string(REPLACE "$::static_assert(sizeof($::unquote(value)) == sizeof(uptr), \"original block alias selected\");"
        "$::unquote(value)\n          $::static_assert(sizeof($::unquote($::meta::call_site($::quote { Composed }))) == sizeof(uptr), \"original statement alias selected\");"
        statement_source "${statement_source}")
    set(${result} "${statement_source}" PARENT_SCOPE)
endfunction()
string(REPLACE "$::unquote($::meta::child(block, 1uptr))" "typedef uptr Later;"
    definition_site_alias "${deferred_expression_source}")
reject_expansion(deferred_expression_hygiene
    "structured expression splice must contain one complete expression"
    "${definition_site_alias}")
string(REPLACE "$::unquote($::meta::tokens($::meta::child(block, 0uptr)))" "{"
    relocated_expression "${deferred_expression_source}")
string(REPLACE "$::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))" "}"
    relocated_expression "${relocated_expression}")
reject_expansion(deferred_expression_relocated
    "structured expression splice must contain one complete expression"
    "${relocated_expression}")
as_deferred_type(relocated_type "${relocated_expression}")
reject_expansion(deferred_type_relocated
    "expected Cross type|structured type splice must contain one complete type"
    "${relocated_type}")
string(REPLACE
    "return $::quote {\n        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))"
    "return $::quote {\n        {\n        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))"
    relocated_original "${deferred_expression_source}")
string(REPLACE
    "if ($::unquote(value) != 5uptr) return 0u32;\n        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))"
    "$::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))\n        { typedef u8 $::unquote($::syntax::capture(input, \"name\"));\n          $::static_assert(sizeof($::unquote(value)) == sizeof(uptr), \"original block alias selected\"); }\n        }"
    relocated_original "${relocated_original}")
accept(deferred_expression_original_block_after_exit "${relocated_original}")
as_deferred_type(moved_type "${relocated_original}")
accept(deferred_type_original_block_after_exit "${moved_type}")
as_deferred_statement_alias(moved_statement "${relocated_original}")
accept(deferred_statement_original_block_after_exit "${moved_statement}")
string(REPLACE "owner Later { define_type!(Later); inner (Later)5uptr; }"
    "owner Later { define_type!(Other); inner (Later)5uptr; define_type!(Later); }"
    later_original_alias "${relocated_original}")
string(REPLACE "$::unquote($::meta::child(block, 1uptr))\n        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))"
    "$::unquote($::meta::child(block, 1uptr))\n        $::unquote($::meta::child(block, 3uptr))\n        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))"
    later_original_alias "${later_original_alias}")
reject_expansion(deferred_expression_later_original_alias
    "structured expression splice must contain one complete expression"
    "${later_original_alias}")
as_deferred_type(later_type "${later_original_alias}")
reject_expansion(deferred_type_later_original_alias
    "expected Cross type|structured type splice must contain one complete type"
    "${later_type}")
as_deferred_statement_alias(later_statement "${later_original_alias}")
reject_expansion(deferred_statement_later_original_alias
    "expected Cross type|structured statement splice must contain one complete statement"
    "${later_statement}")

function(as_generated_function result source)
    string(FIND "${source}" "global u32 entry()" entry)
    if(entry LESS 0)
        message(FATAL_ERROR "generated deferred fixture has no entry function")
    endif()
    string(SUBSTRING "${source}" 0 ${entry} definitions)
    string(SUBSTRING "${source}" ${entry} -1 body)
    string(REPLACE "syntax Owner, Inner;" "" body "${body}")
    set(${result} "${definitions}
syntax Owner, Inner;
[[macro]] static $::meta::tokens generate(in $::meta::tokens ignored) {
    return $::quote { ${body} };
}
generate!()
" PARENT_SCOPE)
endfunction()

# Fresh quote tokens share diagnostic spans, but their lexical output positions
# still distinguish declarations before/after a captured use and foreign blocks.
foreach(form relocated_original moved_type)
    as_generated_function(generated "${${form}}")
    accept(generated_${form} "${generated}")
endforeach()
string(REPLACE "$::meta::call_site($::quote { Composed })" "$::syntax::capture(input, \"alias\")"
    generated_statement "${moved_statement}")
string(REPLACE "match name:ident body:stmt" "match name:ident alias:ident body:stmt"
    generated_statement "${generated_statement}")
string(REPLACE "owner Later {" "owner Later Composed {"
    generated_statement "${generated_statement}")
# A generated alias is hygienic. Copy its identifier from the constructing
# fragment instead of manufacturing a public call-site identifier of that name.
as_generated_function(generated "${generated_statement}")
accept(generated_moved_statement "${generated}")
string(REPLACE "match value:stmt" "match value:declaration"
    moved_declaration "${generated_statement}")
as_generated_function(generated "${moved_declaration}")
accept(generated_moved_declaration "${generated}")
foreach(form later_original_alias later_type later_statement relocated_expression relocated_type)
    as_generated_function(generated "${${form}}")
    reject_expansion(generated_${form}
        "expected Cross type|structured (expression|type|statement) splice must contain one complete"
        "${generated}")
endforeach()

string(REPLACE "typedef uptr $::unquote(input);" "enum { $::unquote(input) = 5u32 };"
    moved_enumerator "${relocated_original}")
string(REPLACE "inner (Later)5uptr;" "inner Later;" moved_enumerator "${moved_enumerator}")
string(REPLACE "sizeof($::unquote(value)) == sizeof(uptr)" "$::unquote(value) == 5u32"
    moved_enumerator "${moved_enumerator}")
accept(deferred_enumerator_original_block_after_exit "${moved_enumerator}")
as_generated_function(generated "${moved_enumerator}")
accept(generated_moved_enumerator "${generated}")

foreach(kind struct union)
    string(REPLACE "typedef uptr $::unquote(input);"
        "${kind} $::unquote(input) { uptr field; };" moved_tag "${relocated_original}")
    string(REPLACE "inner (Later)5uptr;" "inner sizeof(${kind} Later);"
        moved_tag "${moved_tag}")
    string(REPLACE "typedef u8 $::unquote($::syntax::capture(input, \"name\"));"
        "${kind} $::unquote($::syntax::capture(input, \"name\")) { u8 field; };"
        moved_tag "${moved_tag}")
    string(REPLACE "sizeof($::unquote(value)) == sizeof(uptr)" "$::unquote(value) == sizeof(uptr)"
        moved_tag "${moved_tag}")
    as_generated_function(generated "${moved_tag}")
    accept(generated_moved_${kind} "${generated}")
endforeach()

string(REPLACE "return $::quote { typedef uptr $::unquote(input); };"
    "return $::quote { intermediate!($::unquote(input)) };"
    nested_generated "${relocated_original}")
set(nested_generated "[[macro]] static $::meta::tokens intermediate(in $::meta::tokens input) {
    return $::quote { typedef uptr $::unquote(input); };
}
${nested_generated}")
as_generated_function(generated "${nested_generated}")
accept(generated_nested_declaration "${generated}")

set(copied_deferred_blocks [=[
[[macro]] static $::meta::tokens declare(in $::meta::tokens input) {
    return $::quote { u32 $::unquote($::meta::call_site($::quote { local })) = 3u32; };
}
[[syntax_expander]] static $::meta::tokens twice(in $::meta::syntax_match input) {
    $::meta::syntax body = $::syntax::node(input, "body");
    return $::quote { { $::unquote(body) $::unquote(body) } };
}
syntax Twice : statement { prefix "twice"; match body:stmt; expand twice; }
static u32 run() {
    syntax Twice;
    u32 total = 0u32;
    twice { declare!(); local += 1u32; total += local; }
    return total;
}
$::static_assert($::eval(run()) == 8u32, "copied blocks own distinct declarations");
]=])
accept(copied_deferred_blocks "${copied_deferred_blocks}")
string(REPLACE "$::unquote(body) $::unquote(body)" "$::unquote(body)"
    single_deferred_block "${copied_deferred_blocks}")
string(REPLACE "== 8u32" "== 4u32" single_deferred_block "${single_deferred_block}")
accept(single_deferred_block "${single_deferred_block}")
string(REPLACE "u32 $::unquote($::meta::call_site($::quote { local })) = 3u32;"
    "$::unquote($::meta::call_site($::quote { local }))"
    deferred_declaration_list "${copied_deferred_blocks}")
string(REPLACE "declare!(); local += 1u32; total += local;"
    "u32 declare!() = 3u32, second = local + 1u32; total += second;"
    deferred_declaration_list "${deferred_declaration_list}")
accept(copied_deferred_declaration_list "${deferred_declaration_list}")
string(REPLACE "return $::quote { $::unquote($::meta::call_site($::quote { local })) };"
    "return $::quote { u32 $::unquote($::meta::call_site($::quote { local })) };"
    deferred_statement_head "${deferred_declaration_list}")
string(REPLACE "u32 declare!()" "declare!()"
    deferred_statement_head "${deferred_statement_head}")
accept(copied_deferred_statement_head "${deferred_statement_head}")
string(REPLACE "return $::quote { u32 $::unquote($::meta::call_site($::quote { local })) };"
    "return $::quote { u32 ignored = 9u32; u32 $::unquote($::meta::call_site($::quote { local })) };"
    deferred_statement_items "${deferred_statement_head}")
accept(deferred_statement_head_multiple_items "${deferred_statement_items}")
string(REPLACE "$::unquote(body) $::unquote(body)" "$::unquote(body)"
    direct_statement_head "${deferred_statement_head}")
string(REPLACE "twice { declare!() = 3u32, second = local + 1u32; total += second; }"
    "twice declare!() = 3u32, second = local + 1u32; total += second;"
    direct_statement_head "${direct_statement_head}")
string(REPLACE "return $::quote { { $::unquote(body) } };" "return $::quote { $::unquote(body) };"
    direct_statement_head "${direct_statement_head}")
string(REPLACE "== 8u32" "== 4u32" direct_statement_head "${direct_statement_head}")
accept(direct_deferred_statement_head "${direct_statement_head}")
string(REPLACE "return $::quote { u32 $::unquote($::meta::call_site($::quote { local })) };"
    "return $::quote { u32 ignored = 9u32; u32 $::unquote($::meta::call_site($::quote { local })) };"
    direct_statement_items "${direct_statement_head}")
reject_expansion(direct_deferred_statement_multiple_items
    "structured statement splice must contain one complete statement" "${direct_statement_items}")
accept(discard_statement_head_continuations "${discard}
syntax Drop : statement { prefix \"drop\"; match body:stmt; expand discard; }
global u32 entry() { syntax Drop;
    drop unknown!() = 3u32, another = local + 1u32;
    drop if (1u32) unknown!() + 3u32; else ;
    drop { unknown!()[3u32] = { 1u32, 2u32, 3u32 }; }
    return 1u32;
}")
string(REPLACE "$::unquote(body)" "$::unquote($::meta::tokens(body))"
    projected_deferred_blocks "${copied_deferred_blocks}")
accept(projected_deferred_blocks "${projected_deferred_blocks}")
string(REPLACE "[[macro]] static $::meta::tokens declare("
    "[[macro]] static $::meta::tokens inner(" nested_deferred_blocks "${copied_deferred_blocks}")
set(nested_deferred_blocks "[[macro]] static $::meta::tokens declare(in $::meta::tokens input) {
    return $::quote { inner!() };
}
${nested_deferred_blocks}")
accept(copied_nested_deferred_blocks "${nested_deferred_blocks}")
string(REPLACE "declare!(); local += 1u32;" "{ declare!(); } local += 1u32;"
    sibling_deferred_block "${copied_deferred_blocks}")
reject_expansion(deferred_sibling_local "unresolved name 'local'" "${sibling_deferred_block}")
string(REPLACE "declare!(); local += 1u32; total += local;"
    "local += 1u32; declare!(); total += local;"
    later_deferred_local "${copied_deferred_blocks}")
reject_expansion(deferred_later_local "unresolved name 'local'" "${later_deferred_local}")
string(REPLACE "{ $::unquote(body) $::unquote(body) }"
    "{ u32 $::unquote($::meta::call_site($::quote { local })) = 91u32; $::unquote(body) }"
    foreign_deferred_local "${later_deferred_local}")
reject_expansion(deferred_destination_local "unresolved name 'local'" "${foreign_deferred_local}")

file(READ "${CMAKE_CURRENT_LIST_DIR}/syntax_exited_scope.x" exited_scope_source)
accept(exited_scope_copies "${exited_scope_source}")
foreach(field alias tag value)
    string(REPLACE "$::unquote(${field})" "$::unquote($::meta::tokens(${field}))"
        projected "${exited_scope_source}")
    accept(exited_scope_${field}_projected "${projected}")
endforeach()
file(READ "${CMAKE_CURRENT_LIST_DIR}/syntax_implicit_tags.x" implicit_tags_source)
foreach(transport structured projected)
    if(transport STREQUAL "projected")
        set(implicit_transport "#define TEST_PROJECT_IMPLICIT\n${implicit_tags_source}")
    else()
        set(implicit_transport "${implicit_tags_source}")
    endif()
    accept(implicit_tags_${transport} "${implicit_transport}")
    string(REPLACE "struct " "union " implicit_union "${implicit_transport}")
    accept(implicit_union_tags_${transport} "${implicit_union}")
    string(REPLACE "$::unquote(first) *left = 0;"
        "struct $::unquote($::meta::call_site($::quote { Missing })) destination = { 7uptr }; $::unquote(first) *left = &destination;"
        implicit_destination "${implicit_transport}")
    reject_expansion(implicit_tags_destination_${transport}
        "incompatible|cannot.*pointer" "${implicit_destination}")
    string(REPLACE "struct $::unquote($::meta::call_site($::quote { Missing })) { uptr field; };"
        "" implicit_later_destination "${implicit_transport}")
    string(REPLACE "$::unquote(first) *left = 0;"
        "$::unquote(first) *left = 0; struct $::unquote($::meta::call_site($::quote { Missing })) { uptr field; };"
        implicit_later_destination "${implicit_later_destination}")
    string(REPLACE "$::unquote(second) *right = left;"
        "struct $::unquote($::meta::call_site($::quote { Missing })) *right = left;"
        implicit_later_destination "${implicit_later_destination}")
    reject_expansion(implicit_tags_later_destination_${transport}
        "incompatible|cannot.*pointer" "${implicit_later_destination}")
    set(implicit_block_end "$::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))")
    string(REPLACE "${implicit_block_end}" "" implicit_live "${implicit_transport}")
    string(REPLACE "struct $::unquote($::meta::call_site($::quote { Missing })) { uptr field; };"
        "" implicit_live "${implicit_live}")
    string(REPLACE "$::unquote(first) *left = 0;"
        "$::unquote(first) *left = 0; struct $::unquote($::meta::call_site($::quote { Missing })) { uptr field; };"
        implicit_live "${implicit_live}")
    string(REPLACE "$::unquote(additional)" "$::unquote(additional) ${implicit_block_end}"
        implicit_live "${implicit_live}")
    accept(implicit_tags_live_completion_${transport} "${implicit_live}")
    string(REGEX REPLACE "#ifdef TEST_PROJECT_IMPLICIT[\r\n]+([^#]*)#else[\r\n]+([^#]*)#endif"
        "\\2" implicit_generated_body "${implicit_transport}")
    if(transport STREQUAL "projected")
        string(REPLACE "$::meta::syntax first = first_node;" "$::meta::tokens first = $::meta::tokens(first_node);"
            implicit_generated_body "${implicit_generated_body}")
        string(REPLACE "$::meta::syntax second = second_node;" "$::meta::tokens second = $::meta::tokens(second_node);"
            implicit_generated_body "${implicit_generated_body}")
        string(REPLACE "#define TEST_PROJECT_IMPLICIT\n" "" implicit_generated_body "${implicit_generated_body}")
    endif()
    string(REPLACE "\"Hold\"" "\"ImplicitTagCopies::Hold\""
        implicit_generated_body "${implicit_generated_body}")
    string(FIND "${implicit_generated_body}" "    [[noinline]] static uptr plain()" implicit_functions)
    string(SUBSTRING "${implicit_generated_body}" 0 ${implicit_functions} implicit_definitions)
    string(SUBSTRING "${implicit_generated_body}" ${implicit_functions} -1 implicit_functions_body)
    string(REPLACE "syntax Hold, Move;" "" implicit_functions_body "${implicit_functions_body}")
    accept(implicit_tags_generated_${transport}
        "${implicit_definitions}\n}\nsyntax ImplicitTagCopies::Hold, ImplicitTagCopies::Move;\n[[macro]] static $::meta::tokens generate(in $::meta::tokens ignored) { return $::quote { namespace ImplicitTagCopies { ${implicit_functions_body} }; }\ngenerate!()")
endforeach()
file(READ "${CMAKE_CURRENT_LIST_DIR}/syntax_ambiguous_scopes.x" ambiguous_scope_source)
accept(ambiguous_scope_unaffected "${ambiguous_scope_source}")
string(REPLACE "$::unquote(selected)" "$::unquote($::meta::tokens(selected))"
    projected_unaffected "${ambiguous_scope_source}")
accept(ambiguous_scope_unaffected_projected "${projected_unaffected}")
string(REPLACE "use_detached free_name();" "use_detached absent;"
    unrelated_missing "${ambiguous_scope_source}")
reject_expansion(ambiguous_scope_unrelated_missing "unresolved name 'absent'" "${unrelated_missing}")
set(tag_only [=[struct $::unquote(name) { uptr field; };]=])
set(alias_only [=[typedef uptr $::unquote(name);]=])
set(generated_declarations [=[
            struct $::unquote(name) { uptr field; };
            typedef struct $::unquote(name) $::unquote(name);
            enum [[underlying(uptr)]] {
                $::unquote($::meta::call_site($::quote { number })) = 7uptr
            };
            uptr $::unquote($::meta::call_site($::quote { value })) = 7uptr;]=])
string(REPLACE "${generated_declarations}" "${tag_only}" tag_domain "${ambiguous_scope_source}")
string(REPLACE "use_detached free_name();" "use_detached sizeof(Record);" tag_domain "${tag_domain}")
accept(ambiguous_scope_tag_does_not_block_alias "${tag_domain}")
string(REPLACE "${generated_declarations}" "${alias_only}" ordinary_domain "${ambiguous_scope_source}")
string(REPLACE "use_detached free_name();" "use_detached sizeof(struct Record);" ordinary_domain "${ordinary_domain}")
accept(ambiguous_scope_alias_does_not_block_tag "${ordinary_domain}")
foreach(kind alias tag enumerator value)
    if(kind STREQUAL "alias")
        set(use "sizeof(Record)")
        set(name "Record")
    elseif(kind STREQUAL "tag")
        set(use "sizeof(struct Record)")
        set(name "Record")
    elseif(kind STREQUAL "enumerator")
        set(use "number")
        set(name "number")
    else()
        set(use "value")
        set(name "value")
    endif()
    string(REPLACE "use_detached free_name();" "use_detached ${use};"
        ambiguous "${ambiguous_scope_source}")
    reject_expansion(ambiguous_scope_${kind} "ambiguous lexical association for '${name}'" "${ambiguous}")
    string(REPLACE "$::unquote(selected)" "$::unquote($::meta::tokens(selected))"
        projected "${ambiguous}")
    reject_expansion(ambiguous_scope_${kind}_projected "ambiguous lexical association for '${name}'" "${projected}")
endforeach()
foreach(kind alias tag declaration statement)
    set(source "${ambiguous_scope_source}")
    string(REGEX REPLACE "\n[ \t]+repeat_bound[^\r\n]+" "" source "${source}")
    if(kind STREQUAL "alias" OR kind STREQUAL "tag")
        string(REPLACE "match value:expr \";\";" "match value:type \";\";" source "${source}")
        if(kind STREQUAL "alias")
            set(use "Record")
        else()
            set(use "struct Record")
        endif()
        set(output "$::unquote(selected) object;")
    else()
        string(REPLACE "match value:expr \";\";" "match value:${kind};" source "${source}")
        if(kind STREQUAL "declaration")
            set(use "Record object")
        else()
            string(REPLACE "match value:statement;" "match value:stmt;" source "${source}")
            set(use "sizeof(Record)")
        endif()
        set(output "$::unquote(selected)")
    endif()
    string(REPLACE "use_detached free_name();" "use_detached ${use};" source "${source}")
    string(REPLACE "if ($::unquote(selected) != 7uptr) return 0uptr;" "${output}" source "${source}")
    reject_expansion(ambiguous_scope_${kind}_node "ambiguous lexical association for 'Record'" "${source}")
endforeach()
set(original_parts [=[
                $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
                $::unquote($::meta::tokens($::meta::child(block, 1uptr)))
                $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))]=])
string(REPLACE "${original_parts}" "$::unquote($::meta::tokens(body))"
    complete_copies "${ambiguous_scope_source}")
accept(ambiguous_scope_complete_copies "${complete_copies}")
set(pending_declaration [=[
[[macro]] static $::meta::tokens pending(in $::meta::tokens input) {
    return $::quote { typedef uptr pending_type; };
}
]=])
foreach(kind alias tag enumerator value)
    if(kind STREQUAL "alias")
        set(use "sizeof(Record)")
    elseif(kind STREQUAL "tag")
        set(use "sizeof(struct Record)")
    elseif(kind STREQUAL "enumerator")
        set(use "number")
    else()
        set(use "value")
    endif()
    string(REPLACE "declare!(Record); use_detached free_name();"
        "pending!(); use_detached ${use}; declare!(Record);" late "${complete_copies}")
    accept(ambiguous_scope_${kind}_later "${pending_declaration}${late}")
endforeach()
set(exited_return [=[return $::quote { { $::unquote(one) $::unquote(one) } };]=])
set(empty_alias_return [=[return $::quote {
    { $::unquote(one) {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
        $::unquote(alias) missing;
    } }
};]=])
string(REPLACE "${exited_return}" "${empty_alias_return}" empty_alias_source "${exited_scope_source}")
reject_expansion(exited_scope_missing_alias "structured type splice must contain one complete type" "${empty_alias_source}")
string(REPLACE "$::unquote(alias) missing;"
    "typedef u8 $::unquote($::meta::call_site($::quote { Record })); $::unquote(alias) missing;"
    foreign_alias_source "${empty_alias_source}")
string(REPLACE "$::eval(plain()) == 14uptr" "$::eval(plain()) == 7uptr"
    foreign_alias_source "${foreign_alias_source}")
reject_expansion(exited_scope_foreign_alias "structured type splice must contain one complete type" "${foreign_alias_source}")
string(REPLACE "$::unquote(alias)" "$::unquote($::meta::tokens(alias))"
    foreign_alias_projected "${foreign_alias_source}")
reject_expansion(exited_scope_foreign_alias_projected "expected ';'|expected Cross type"
    "${foreign_alias_projected}")
set(empty_tag_return [=[return $::quote {
    { $::unquote(one) {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
        $::static_assert(sizeof($::unquote(tag)) == sizeof(uptr), "missing tag stays incomplete");
    } }
};]=])
string(REPLACE "${exited_return}" "${empty_tag_return}" empty_tag_source "${exited_scope_source}")
reject_expansion(exited_scope_missing_tag "complete object type" "${empty_tag_source}")
string(REPLACE "$::static_assert(sizeof($::unquote(tag))"
    "struct $::unquote($::meta::call_site($::quote { Record })) { uptr field; }; $::static_assert(sizeof($::unquote(tag))"
    foreign_tag_source "${empty_tag_source}")
string(REPLACE "$::eval(plain()) == 14uptr" "$::eval(plain()) == 7uptr"
    foreign_tag_source "${foreign_tag_source}")
reject_expansion(exited_scope_foreign_tag "complete object type" "${foreign_tag_source}")
string(REPLACE "$::unquote(tag)" "$::unquote($::meta::tokens(tag))"
    foreign_tag_projected "${foreign_tag_source}")
reject_expansion(exited_scope_foreign_tag_projected "complete object type" "${foreign_tag_projected}")
set(empty_value_return [=[
$::meta::tokens value_tokens = $::meta::children($::meta::tokens(value));
return $::quote {
    { $::unquote(one) {
        $::unquote($::meta::tokens($::meta::child(block, 0uptr)))
        $::unquote($::meta::tokens($::meta::child(block, $::meta::child_count(block) - 1uptr)))
        $::unquote($::syntax::capture(input, "total")) +=
            $::unquote($::meta::slice(value_tokens, $::meta::len(value_tokens) - 1uptr, 1uptr));
    } }
};]=])
string(REPLACE "${exited_return}" "${empty_value_return}" empty_value_source "${exited_scope_source}")
reject_expansion(exited_scope_missing_value "unresolved name 'number'" "${empty_value_source}")

reject(known_error "syntax-match error for active prefix"
    "${discard}${owner} global u32 entry() { syntax Owner; owner { u32 = ; future!{}; NewType value; } return 0u32; }")
reject(unbounded_group "syntax-match error|unterminated|bounded group"
    "${discard}${owner} global u32 entry() { syntax Owner; owner { future!{}; NewType value; ")
reject(ambiguous_generic_boundary "syntax-match error for active prefix"
    "${discard}${owner} syntax Inner : statement { prefix \"inner\"; match value:expr \";\"; expand discard; } global u32 entry() { syntax Owner, Inner; owner { future!{}; inner introduced<NewType, u32>(3u32); } return 0u32; }")
reject(incomplete_statement "syntax-match error for active prefix"
    "${discard}${owner} syntax Inner : statement { prefix \"inner\"; match body:stmt; expand discard; } global u32 entry() { syntax Owner, Inner; owner { future!{}; inner if (1u32) NewType value else other; } return 0u32; }")
reject(copied_assertion "static_assert failed: copied assertion must execute"
    "${copy} syntax Copy : statement { prefix \"copied\"; match body:stmt; expand copy; } static T checked<T>(in T value) { syntax Copy; copied { $::static_assert(0u32, \"copied assertion must execute\"); } return value; } global u32 entry() { return checked(1u32); }")
set(type_macro_owner "[[syntax_expander]] static $::meta::tokens copy_type(in $::meta::syntax_match input) { return $::quote { typedef $::unquote($::syntax::node(input, \"value\")) Alias; }; } syntax CopyType : statement { prefix \"copy_type\"; match value:type \";\"; expand copy_type; }")
reject_expansion(type_macro_name "structured type splice cannot declare a name"
    "[[macro]] static $::meta::tokens bad_type(in $::meta::tokens input) { return $::quote { u16 named }; } ${type_macro_owner} global u32 entry() { syntax CopyType; copy_type bad_type!(); return 0u32; }")
reject_expansion(type_macro_extra "structured type splice must contain one complete type"
    "[[macro]] static $::meta::tokens bad_type(in $::meta::tokens input) { return $::quote { u16 ; u32 }; } ${type_macro_owner} global u32 entry() { syntax CopyType; copy_type bad_type!(); return 0u32; }")
foreach(depth 1 2 3)
    reject(deferred_depth_${depth} "depth|syntax-match error for active prefix"
        "${discard}${owner} global u32 entry() { syntax Owner; owner { future!{}; NewType value; } return 0u32; }"
        -feval-depth-limit=${depth})
endforeach()
string(REPEAT " NewType value;" 16 storage_payload)
reject(deferred_storage "byte or memory budget|storage budget"
    "${discard}${owner} global u32 entry() { syntax Owner; owner { future!{};${storage_payload} } return 0u32; }"
    -feval-memory-limit=4096)
