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
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
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
reject_expansion(type_macro_name "procedural type macro cannot declare a name"
    "[[macro]] static $::meta::tokens bad_type(in $::meta::tokens input) { return $::quote { u16 named }; } ${type_macro_owner} global u32 entry() { syntax CopyType; copy_type bad_type!(); return 0u32; }")
reject_expansion(type_macro_extra "procedural type macro must produce one complete type"
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
