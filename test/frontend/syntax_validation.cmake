# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
        foreach(note IN LISTS ARGN)
            if(NOT err MATCHES "${note}")
                message(FATAL_ERROR "${name}/${level}: missing activation note '${note}'\n${out}\n${err}")
            endif()
        endforeach()
    endforeach()
endfunction()

check(graph_activation_ancestry "left-recursive or nullable syntax rule cycle" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Recursive : rule { match rule(Recursive); }
syntax Owner : expression { prefix "owner"; match rule(Recursive); expand expand; }
syntax Inner : bundle { use Owner; }
syntax Outer : bundle { use Inner; }
syntax Outer;
]=] "requested by syntax activation 'Owner'" "syntax 'Owner' defined here"
    "requested by syntax activation 'Inner'" "syntax 'Inner' defined here"
    "requested by syntax activation 'Outer'" "syntax 'Outer' defined here"
    "graph_activation_ancestry.x:8:[0-9]+: note: requested by syntax activation 'Outer'")

check(progress_activation_ancestry "syntax optional/repetition body may be nullable" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Empty : rule { match maybe:optional("x"); }
syntax Owner : expression { prefix "owner"; match values:repeat0(rule(Empty)); expand expand; }
syntax Pack : bundle { use Owner; }
syntax Pack;
]=] "requested by syntax activation 'Owner'" "syntax 'Owner' defined here"
    "requested by syntax activation 'Pack'" "syntax 'Pack' defined here")

check(forward_and_stable pass [=[
syntax Unused : expression { prefix "unused"; match rule(Missing); expand missing; }
syntax Cycle : rule { match rule(Cycle); }
syntax A : rule { match branch:choice(stop:("end") | next:("a" rule(B))); }
syntax Forward : expression { prefix "forward"; match rule(A); expand expand; }
syntax B : rule { match branch:choice(stop:("end") | next:("b" rule(A))); }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Forward;
$::static_assert(forward a b a end == 1u32, "productive mutual forward rules");
syntax Leaf : rule { match part:optional("outer"); }
namespace Near {
    namespace Deep {
        syntax Owner : expression {
            prefix "candidate"; match "(" parts:repeat0(rule(Leaf)) ")"; expand expand;
        }
    }
    syntax Leaf : rule { match "inner"; }
    [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
        return $::quote { 17u32 };
    }
}
syntax Near::Deep::Owner;
$::static_assert(candidate (inner inner) == 17u32, "activation selects closer definitions");
namespace Near {
    namespace Deep {
        syntax Leaf : rule { match "deep"; }
        [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
            return $::quote { 31u32 };
        }
    }
}
syntax Near::Deep::Owner as again;
$::static_assert(again (inner) == 17u32, "successful activation keeps stable identities");
global u32 entry() { return again (inner); }
]=])

check(unused_local_progress "syntax optional/repetition body may be nullable" [=[
syntax Bad : expression {
    prefix "bad"; match values:repeat0(value:optional("x")); expand missing;
}
]=])
check(unused_local_continuation "syntax repetition start conflicts with continuation" [=[
syntax Bad : rule { match values:repeat0("x") "x"; }
]=])
check(declaration_using_continuation "syntax repetition start conflicts with continuation" [=[
syntax Bad : rule { match values:repeat0(value:declaration) "using" ";"; }
]=])
check(rule_declaration_using_continuation "syntax repetition start conflicts with continuation" [=[
syntax Declaration : rule { match value:declaration; }
syntax Bad : expression {
    prefix "bad"; match values:repeat0(rule(Declaration)) "using" ";"; expand expand;
}
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Bad;
]=])
foreach(category stmt function_header function_decl function_def function_raw)
    check(non_declaration_using_continuation_${category} pass "
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Value : expression {
    prefix \"value\"; match \"(\" values:repeat0(value:${category}) \"using\" \";\" \")\"; expand expand;
}
syntax Value;
$::static_assert(value (using;) == 1u32, \"using is not a statement or function header\");")
endforeach()
check(repeated_using_declarations pass [=[
namespace Imported { typedef u32 Word; }
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    $::static_assert($::syntax::count(input, "values") == 2uptr, "using declaration count");
    for (uptr index = 0uptr; index < 2uptr; ++index) {
        $::meta::syntax_match part = $::syntax::at(input, "values", index);
        $::meta::syntax node = $::syntax::node(part, "value");
        $::static_assert($::meta::is_production(node, "using_declaration"), "using declaration root");
    }
    return $::quote { 1u32 };
}
syntax Value : expression {
    prefix "value"; match "(" values:repeat1(value:declaration) ")"; expand inspect;
}
syntax Value;
$::static_assert(value (using Imported; using Imported;) == 1u32, "using declarations");
]=])
check(repeated_using_commit "malformed syntax repetition after committed start" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Value : expression {
    prefix "value"; match "(" values:repeat0(value:declaration) ")"; expand expand;
}
syntax Value;
$::static_assert(value (using !) == 1u32, "must not end the repetition at malformed using");
]=])
# A structured node is one category-aware input element, not an identifier.
# Missing a suffix after that element must commit the same repetition as the
# equivalent written source. Exercise direct/rule starts and sibling choices.
foreach(category expr type stmt declaration function_header function_decl function_def function_raw)
    set(parse_category "${category}")
    set(contents "u32 f()")
    set(tail "")
    if(category STREQUAL expr)
        set(contents "1u32 + 2u32")
    elseif(category STREQUAL type)
        set(contents "const u32 *")
    elseif(category STREQUAL stmt)
        set(contents "{ u32 value = 1u32; }")
    elseif(category STREQUAL declaration)
        set(contents "typedef u32 Value;")
    elseif(category STREQUAL function_decl)
        set(contents "u32 f();")
    elseif(category STREQUAL function_def)
        set(contents "u32 f() { return 1u32; }")
    elseif(category STREQUAL function_raw)
        set(parse_category function_header)
        set(tail "{ opaque body; }")
    endif()
    set(prefix "
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Part : rule { match value:${category} \";\"; }
")
    foreach(pattern "parts:repeat0(value:${category} \";\")" "parts:repeat0(rule(Part))")
        string(MD5 key "${pattern}")
        foreach(suffix "" ";")
            set(expected "malformed syntax repetition after committed start")
            set(mode malformed)
            if(suffix STREQUAL ";")
                set(expected pass)
                set(mode valid)
            endif()
            check(splice_repeat_${category}_${key}_${mode} "${expected}" "${prefix}
syntax Inspect : expression { prefix \"inspect\"; match \"(\" ${pattern} \")\"; expand inspect; }
syntax Inspect;
[[syntax_expander]] static $::meta::tokens forward(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse(\"${parse_category}\", $::quote { ${contents} }, $::syntax::context(input));
    return $::quote { inspect ($::unquote(node) ${tail} ${suffix}) };
}
syntax Forward : expression { prefix \"forward\"; match \"(\" \")\"; expand forward; }
syntax Forward;
$::static_assert(forward() == 1u32, \"structured repetition\");")
        endforeach()
    endforeach()
    check(splice_repeat_${category}_sibling pass "${prefix}
syntax Inspect : expression {
    prefix \"inspect\";
    match \"(\" branch:choice(repeated:(parts:repeat0(rule(Part))) | other:(value:${category})) \")\";
    expand inspect;
}
syntax Inspect;
[[syntax_expander]] static $::meta::tokens forward(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse(\"${parse_category}\", $::quote { ${contents} }, $::syntax::context(input));
    return $::quote { inspect ($::unquote(node) ${tail}) };
}
syntax Forward : expression { prefix \"forward\"; match \"(\" \")\"; expand forward; }
syntax Forward;
$::static_assert(forward() == 1u32, \"independent alternative survives\");")
endforeach()
function(check_splice_start name category parse_category contents tail expected)
    set(suffix "")
    if(expected STREQUAL pass)
        set(suffix ";")
    endif()
    check(splice_start_${name} "${expected}" "
namespace Imported {}
[[syntax_expander]] static $::meta::tokens inspect(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Inspect : expression {
    prefix \"inspect\"; match \"(\" parts:repeat0(value:${category} \";\") \")\"; expand inspect;
}
syntax Inspect;
[[syntax_expander]] static $::meta::tokens forward(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse(\"${parse_category}\", $::quote { ${contents} }, $::syntax::context(input));
    return $::quote { inspect ($::unquote(node) ${tail} ${suffix}) };
}
syntax Forward : expression { prefix \"forward\"; match \"(\" \")\"; expand forward; }
syntax Forward;
$::static_assert(forward() == 1u32, \"structured start category\");")
endfunction()
foreach(expected pass "malformed syntax repetition after committed start")
    string(MD5 key "${expected}")
    check_splice_start(expr_statement_${key} stmt expr "1u32" ";" "${expected}")
    check_splice_start(type_statement_${key} stmt type "u32" "value;" "${expected}")
    check_splice_start(type_declaration_${key} declaration type "u32" "value;" "${expected}")
    check_splice_start(type_header_${key} function_header type "u32" "f()" "${expected}")
    check_splice_start(type_prototype_${key} function_decl type "u32" "f();" "${expected}")
    check_splice_start(type_definition_${key} function_def type "u32" "f() { return 1u32; }" "${expected}")
    check_splice_start(type_raw_${key} function_raw type "u32" "f() { opaque body; }" "${expected}")
    check_splice_start(header_declaration_${key} declaration function_header "u32 f()" ";" "${expected}")
    check_splice_start(header_prototype_${key} function_decl function_header "u32 f()" ";" "${expected}")
    check_splice_start(header_definition_${key} function_def function_header "u32 f()" "{ return 1u32; }" "${expected}")
    check_splice_start(declaration_statement_${key} stmt declaration "typedef u32 Word;" "" "${expected}")
    check_splice_start(using_declaration_${key} declaration declaration "using Imported;" "" "${expected}")
    check_splice_start(assert_declaration_${key} declaration declaration "$::static_assert(0, \"not executed\");" "" "${expected}")
    check_splice_start(assert_statement_${key} stmt declaration "$::static_assert(0, \"not executed\");" "" "${expected}")
    # Opaque macro input makes these parsed nodes deferred; commitment must
    # use their retained categories without executing or flattening them.
    check_splice_start(deferred_type_${key} type type "missing!()" "" "${expected}")
    check_splice_start(deferred_header_${key} function_header function_header "u32 f(missing!())" "" "${expected}")
    check_splice_start(deferred_prototype_${key} function_decl function_decl "u32 f(missing!());" "" "${expected}")
endforeach()
set(mismatch "syntax-match error for active prefix")
foreach(category expr type stmt function_header function_decl function_def function_raw)
    check_splice_start(using_not_${category} "${category}" declaration "using Imported;" "" "${mismatch}")
endforeach()
check_splice_start(type_not_expr expr type "u32" "" "${mismatch}")
check_splice_start(expr_not_type type expr "1u32" "" "${mismatch}")
check_splice_start(header_not_statement stmt function_header "u32 f()" "" "${mismatch}")
check(splice_committed_no_shorter_item "malformed syntax repetition after committed start" [=[
[[syntax_expander]] static $::meta::tokens empty(in $::meta::syntax_match input) {
    return $::quote {};
}
syntax Consume : item {
    prefix "consume"; match parts:repeat0(value:declaration ";"); expand empty;
}
syntax Consume;
[[syntax_expander]] static $::meta::tokens forward(in $::meta::syntax_match input) {
    $::meta::syntax node = $::meta::parse("declaration", $::quote { u32 value = 1u32; }, $::syntax::context(input));
    return $::quote { consume $::unquote(node) };
}
syntax Forward : item { prefix "forward"; match "(" ")"; expand forward; }
syntax Forward;
forward()
]=])
check(unused_local_fence "parsed expression/type capture requires" [=[
syntax Bad : rule { match value:expr "+"; }
]=])
check(activation_progress "syntax optional/repetition body may be nullable" [=[
syntax Owner : expression { prefix "owner"; match values:repeat0(rule(Leaf)); expand expand; }
syntax Leaf : rule { match value:optional("x"); }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Owner;
]=])
check(activation_cycle "left-recursive or nullable syntax rule cycle" [=[
syntax A : rule { match rule(B); }
syntax Owner : expression { prefix "owner"; match rule(A); expand expand; }
syntax B : rule { match rule(A); }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Owner;
]=])
check(activation_fence "parsed expression/type capture requires" [=[
syntax Leaf : rule { match value:expr; }
syntax Owner : expression { prefix "owner"; match rule(Leaf) "+"; expand expand; }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Owner;
]=])
