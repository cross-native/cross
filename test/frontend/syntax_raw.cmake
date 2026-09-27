# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(directory "${OUTPUT}.cases")
file(MAKE_DIRECTORY "${directory}")

foreach(target mips-unknown-linux-gnu mipsel-unknown-linux-gnu
               mips64-unknown-linux-gnu mips64el-unknown-linux-gnu)
    execute_process(COMMAND "${CC}" -S -O0 -fno-eval-calls -target "${target}"
        "${SOURCE}" -o "${directory}/${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${target} raw syntax compilation failed\n${out}\n${err}")
    endif()
    file(READ "${directory}/${target}.s" assembly)
    if(target MATCHES "^mips64")
        set(width 8)
    else()
        set(width 4)
    endif()
    if(NOT assembly MATCHES "syntax_width:\n[^\n]*[.](word|dword|long|quad) ${width}")
        message(FATAL_ERROR "${target} syntax expander used the wrong target width")
    endif()
    if(assembly MATCHES "flow.*(copy_body|target_size|returning)|syntax_match|syntax_expander")
        message(FATAL_ERROR "${target} translation-only syntax machinery escaped to assembly")
    endif()
endforeach()

set(expander "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 1u32 }; }\n")
set(definition "syntax Value : expression { prefix \"value\"; match body:paren; expand expand; }\n")
function(reject case expected source)
    set(input "${directory}/${case}.x")
    file(WRITE "${input}" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${input}" -o "${directory}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case} was not diagnosed correctly\n${out}\n${err}")
    endif()
endfunction()

reject(reserved "ordinary nonreserved identifier"
    "${expander}syntax Bad : expression { prefix \"if\"; match body:paren; expand expand; }\n")
reject(terminal "exactly one existing token"
    "${expander}syntax Bad : expression { prefix \"two words\"; match body:paren; expand expand; }\n")
reject(balance "balanced|balance delimiters"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" field:ident; expand expand; }\n")
reject(fields "duplicate syntax capture field"
    "${expander}syntax Bad : expression { prefix \"bad\"; match a:ident a:ident; expand expand; }\n")
reject(clause "requires prefix, match, and expand in order"
    "${expander}syntax Bad : expression { match body:paren; prefix \"bad\"; expand expand; }\n")
reject(parsed_capture "capture 'expr' is not implemented yet"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" value:expr \")\"; expand expand; }\n")
reject(fence "followed immediately by terminal ';'"
    "${expander}syntax Bad : expression { prefix \"bad\"; match value:tokens_until(\";\"); expand expand; }\n")
reject(match "syntax-match error for active prefix"
    "${expander}${definition}syntax Value; global u32 entry() { u32 value = 1u32; return value + 1u32; }\n")
reject(function_raw_requires_definition "syntax-match error for active prefix"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function_raw; expand drop; } syntax Fn; fn static u32 value;\n")
reject(function_raw_header "expected ';' or function body"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function; expand drop; } syntax Fn; fn static u32 bad(in u32 value) alien { foreign words; }\n")
reject(function_raw_direct "requires a direct core function header"
    "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Fn : item { prefix \"fn\"; match body:function_raw; expand drop; } syntax Fn; fn struct Record { u32 value; }\n")
reject(nullable_repeat "repetition body may be nullable"
    "${expander}syntax Bad : expression { prefix \"bad\"; match parts:repeat0(item:optional(value:literal)); expand expand; }\n")
reject(nullable_optional "optional/repetition body may be nullable"
    "${expander}syntax Bad : expression { prefix \"bad\"; match part:optional(inner:optional(\"x\")); expand expand; }\n")
reject(nullable_rule_repeat "repetition body may be nullable"
    "${expander}syntax Empty : rule { match item:optional(value:literal); } syntax Bad : expression { prefix \"bad\"; match parts:repeat0(rule(Empty)); expand expand; } syntax Bad;\n")
reject(repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(\"x\") \"x\" \")\"; expand expand; }\n")
reject(separated_continuation "repetition start conflicts with continuation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:separated0(number:literal, \",\") \",\" \")\"; expand expand; }\n")
reject(rule_repeat_continuation "repetition start conflicts with continuation"
    "${expander}syntax X : rule { match \"x\"; } syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(rule(X)) \"x\" \")\"; expand expand; } syntax Bad;\n")
reject(nullable_choice "choice alternative must consume input"
    "${expander}syntax Bad : expression { prefix \"bad\"; match branch:choice(empty:(value:optional(\"x\")) | value:(\"y\")); expand expand; }\n")
reject(choice_duplicate "duplicate choice alternative label"
    "${expander}syntax Bad : expression { prefix \"bad\"; match branch:choice(one:(\"x\") | one:(\"y\")); expand expand; }\n")
reject(choice_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(\"x\") | two:(\"x\")) \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x); }\n")
reject(optional_ambiguous "ambiguous syntax invocation"
    "${expander}syntax Bad : expression { prefix \"bad\"; match maybe:optional(\"x\"); expand expand; } syntax Bad; global u32 entry() { return bad x; }\n")
reject(separated_trailing "malformed syntax item after committed separator"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" values:separated0(number:literal, \",\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (1u32,); }\n")
reject(malformed_repeat "malformed syntax repetition after committed start"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat0(\"x\" \"y\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x z); }\n")
reject(repeat_one_empty "syntax-match error for active prefix"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:repeat1(\"x\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (); }\n")
reject(separated_one_empty "syntax-match error for active prefix"
    "${expander}syntax Bad : expression { prefix \"bad\"; match \"(\" parts:separated1(value:literal, \",\") \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (); }\n")
reject(choice_bad_label "syntax choice has no variant named"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax_match branch = $::syntax::at(input, \"branch\", 0uptr); if ($::syntax::is_variant(branch, \"missing\")) return $::quote { 1u32 }; return $::quote { 0u32 }; } syntax Bad : expression { prefix \"bad\"; match \"(\" branch:choice(one:(\"x\") | two:(\"y\")) \")\"; expand expand; } syntax Bad; global u32 entry() { return bad (x); }\n")
reject(missing_expander "syntax expander is not visible"
    "${definition}syntax Value;\n")
reject(forward_expander "syntax expander is not visible"
    "${definition}syntax Value;\n${expander}")
reject(wrong_role "syntax expander is not visible"
    "[[macro]] static $::meta::tokens expand(in $::meta::tokens input) { return input; }\n${definition}syntax Value;\n")
reject(nonstatic "syntax_expander.*must be static"
    "[[syntax_expander]] $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote {}; }\n")
reject(parameter "requires exactly one.*syntax_match"
    "[[syntax_expander]] static $::meta::tokens expand(out $::meta::syntax_match input) { return $::quote {}; }\n")
reject(variadic "requires exactly one"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input, ...) { return $::quote {}; }\n")
reject(rule_activation "rule cannot be activated"
    "syntax Rule : rule { match value:literal; }\nsyntax Rule;\n")
reject(rule_kind "must denote a syntax rule"
    "${expander}${definition}syntax Other : expression { prefix \"other\"; match rule(Value); expand expand; } syntax Other;\n")
reject(rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax Rule : rule { match rule(Rule) \"x\"; } syntax Other : expression { prefix \"other\"; match rule(Rule); expand expand; } syntax Other;\n")
reject(mutual_rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax A : rule { match rule(B) \"x\"; } syntax B : rule { match rule(A) \"y\"; } syntax Other : expression { prefix \"other\"; match rule(A); expand expand; } syntax Other;\n")
reject(nullable_rule_cycle "left-recursive or nullable syntax rule cycle"
    "${expander}syntax A : rule { match maybe:optional(\"x\") rule(A); } syntax Other : expression { prefix \"other\"; match rule(A); expand expand; } syntax Other;\n")
reject(recursive_ambiguity "ambiguous syntax invocation"
    "${expander}syntax Chain : rule { match branch:choice(stop:(\"x\") | again:(\"x\" rule(Chain))); } syntax Other : expression { prefix \"other\"; match child:rule(Chain); expand expand; } syntax Other; global u32 entry() { return other x x; }\n")
string(REPEAT "(" 12 recursive_opens)
string(REPEAT ")" 12 recursive_closes)
reject(rule_depth "syntax pattern matching depth exceeded"
    "${expander}syntax Tree : rule { match branch:choice(leaf:(value:literal) | nested:(\"(\" child:rule(Tree) \")\")); } syntax Other : expression { prefix \"other\"; match root:rule(Tree); expand expand; } syntax Other; global u32 entry() { return other ${recursive_opens}1u32${recursive_closes}; }\n"
    -feval-depth-limit=12)
reject(bundle_cycle "cyclic syntax bundle"
    "syntax One : bundle { use Two; } syntax Two : bundle { use One; } syntax One;\n")
reject(bundle_alias "bundle cannot be aliased"
    "${expander}${definition}syntax Pack : bundle { use Value; } syntax Pack as alias;\n")
reject(empty_bundle "requires a nonempty use list"
    "syntax Empty : bundle {}\n")
reject(conflict "conflicting syntax prefixes in activation"
    "${expander}${definition}syntax Other : statement { prefix \"value\"; match body:block; expand expand; } syntax Value, Other;\n")
reject(inherited "conflicts with an inherited binding"
    "${expander}${definition}syntax Other : expression { prefix \"value\"; match body:paren; expand expand; } syntax Value; global u32 entry() { syntax Other; return 0u32; }\n")
reject(ambiguous "ambiguous syntax entity"
    "namespace one { ${expander}${definition} } namespace two { ${expander}${definition} } using one; using two; syntax Value;\n")
reject(qualified "syntax entity is not visible"
    "namespace one { ${expander}${definition} namespace two { syntax one::Value; } } namespace elsewhere { syntax one::two::Value; }\n")
reject(trailing "expected nonreserved syntax entity name"
    "${expander}${definition}syntax Value,;\n")
reject(local_definition "definitions are allowed only at item position"
    "global u32 entry() { syntax Local : rule { match value:literal; } return 1u32; }\n")
reject(local_region "regions are allowed only at item position"
    "${expander}${definition}global u32 entry() { syntax (Value) { } return 1u32; }\n")
reject(generated_registration "cannot introduce syntax registration"
    "[[macro]] static $::meta::tokens make(in $::meta::tokens input) { return $::quote { syntax X : rule { match value:literal; } }; } make!()\n")
reject(parsed_registration "cannot introduce syntax registration"
    "[[macro]] static $::meta::tokens make(in $::meta::tokens input) { return $::meta::parse(\"syntax X : rule { match value:literal; }\"); } make!()\n")
reject(syntax_registration "cannot introduce syntax registration"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { syntax Value; }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(expression_output "without a semicolon"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { 1u32; }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(statement_output "exactly one complete statement"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { ; ; }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {} return 1u32; }\n")
reject(nested_semicolon "exactly one complete statement"
    "[[macro]] static $::meta::tokens expr(in $::meta::tokens input) { return $::quote { 1u32 }; } [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { expr!() }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {}; return 1u32; }\n")
reject(adjacent "expected ';'"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::quote { return 1u32 }; } syntax Stmt : statement { prefix \"stmt\"; match body:block; expand expand; } syntax Stmt; global u32 entry() { stmt {}; }\n")
reject(unknown_field "no field named 'missing'"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::syntax::capture(input, \"missing\"); } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(record_index "record index is out of range"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { $::meta::syntax_match child = $::syntax::at(input, \"child\", 1uptr); return $::quote { 1u32 }; } syntax Rule : rule { match body:paren; } syntax Value : expression { prefix \"value\"; match child:rule(Rule); expand expand; } syntax Value; global u32 entry() { return value (); }\n")
reject(record_capture "requires a primitive token field"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return $::syntax::capture(input, \"child\"); } syntax Rule : rule { match body:paren; } syntax Value : expression { prefix \"value\"; match child:rule(Rule); expand expand; } syntax Value; global u32 entry() { return value (); }\n")
reject(no_runtime_size "layout query requires a runtime object type"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { uptr size = sizeof(input); return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(const_match "cannot write a const cell"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { const $::meta::syntax_match copy = input; copy = input; return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(volatile_match "without runtime storage qualifiers"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { volatile $::meta::syntax_match copy = input; return $::quote { 1u32 }; } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(host_io "unresolved call"
    "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { return read_file(input); } ${definition}syntax Value; global u32 entry() { return value (); }\n")
reject(cycle "expansion depth or invocation budget exceeded"
    "${expander}${definition}syntax Value; [[syntax_expander]] static $::meta::tokens looping(in $::meta::syntax_match input) { return $::syntax::input(input); } syntax Loop : expression { prefix \"loop\"; match body:paren; expand looping; } syntax Loop; global u32 entry() { return loop (); }\n")
reject(steps "matching work budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (); }\n" -feval-step-limit=1)
reject(bytes "byte or memory budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (1u32); }\n" -feval-byte-limit=256)
reject(memory "byte or memory budget exceeded"
    "${expander}${definition}syntax Value; global u32 entry() { return value (1u32); }\n" -feval-memory-limit=256)

# Definitions from one primary input must not leak into another registry.
file(WRITE "${directory}/primary-one.x" "${expander}${definition}")
file(WRITE "${directory}/primary-two.x" "syntax Value;\n")
execute_process(COMMAND "${CC}" -S "${directory}/primary-one.x" "${directory}/primary-two.x"
    -o "${directory}/primary.s" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "syntax entity is not visible")
    message(FATAL_ERROR "primary-input syntax registries were not independent\n${out}\n${err}")
endif()
