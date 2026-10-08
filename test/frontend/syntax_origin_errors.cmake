# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -S "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid generated syntax unexpectedly compiled")
endif()

foreach(expected
        "expected expression"
        "in expansion of syntax 'Inner'"
        "syntax 'Inner' defined here"
        "in expansion of syntax 'Outer'"
        "syntax 'Outer' defined here"
        "syntax expander defined here")
    string(FIND "${stderr}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "missing syntax origin '${expected}'\n${stdout}\n${stderr}")
    endif()
endforeach()

if(NOT stderr MATCHES "syntax_origin_errors\\.x:23:12: note: in expansion of syntax 'Outer'")
    message(FATAL_ERROR "outer invocation lost its input span\n${stdout}\n${stderr}")
endif()

# The parser may diagnose at the synthetic end token after a generated '+'.
# That token still inherits both owners even though its offset is text.size().
file(READ "${SOURCE}" eof_source)
string(REPLACE "1u32 + ; 2u32" "+" eof_source "${eof_source}")
set(eof_input "${OUTPUT}.eof.x")
file(WRITE "${eof_input}" "${eof_source}")
execute_process(COMMAND "${CC}" -S "${eof_input}" -o "${OUTPUT}"
    RESULT_VARIABLE eof_status OUTPUT_VARIABLE eof_out ERROR_VARIABLE eof_err)
if(eof_status EQUAL 0 OR NOT eof_err MATCHES "expected expression" OR
   NOT eof_err MATCHES "in expansion of syntax 'Inner'" OR
   NOT eof_err MATCHES "in expansion of syntax 'Outer'")
    message(FATAL_ERROR "end-token syntax ancestry lost\n${eof_out}\n${eof_err}")
endif()

# A match failure in generated input must identify both the nested rule and
# the syntax declaration that owns the invocation, then the outer expansion.
file(READ "${SOURCE}" original_source)
string(REPLACE "syntax Inner : expression {"
    "syntax NeedParen : rule { match body:paren; }\nsyntax Inner : expression {"
    rule_source "${original_source}")
string(REPLACE "match body:paren; expand inner_expander;"
    "match body:rule(NeedParen); expand inner_expander;" rule_source "${rule_source}")
string(REPLACE "return $::quote { inner () };"
    "return $::quote { inner 1u32 };" rule_source "${rule_source}")
set(rule_input "${OUTPUT}.rule.x")
file(WRITE "${rule_input}" "${rule_source}")
execute_process(COMMAND "${CC}" -S "${rule_input}" -o "${OUTPUT}"
    RESULT_VARIABLE rule_status OUTPUT_VARIABLE rule_out ERROR_VARIABLE rule_err)
if(rule_status EQUAL 0 OR NOT rule_err MATCHES "syntax-match error" OR
   NOT rule_err MATCHES "syntax 'Inner' defined here" OR
   NOT rule_err MATCHES "syntax rule 'NeedParen' defined here" OR
   NOT rule_err MATCHES "in expansion of syntax 'Outer'")
    message(FATAL_ERROR "nested match ancestry lost\n${rule_out}\n${rule_err}")
endif()

# EOF is still a match boundary: nested rules and combinators must retain their
# provenance even when no input token remains for the required primitive.
foreach(rule_pattern IN ITEMS
        "rule(NeedParen)"
        "choice(required:(rule(NeedParen)) | other:(\"x\"))"
        "repeat1(rule(NeedParen))"
        "separated1(rule(NeedParen), \",\")")
    string(REPLACE "match body:rule(NeedParen); expand inner_expander;"
        "match body:rule(Wrap); expand inner_expander;" rule_eof_source "${rule_source}")
    string(REPLACE "syntax Inner : expression {"
        "syntax Wrap : rule { match child:${rule_pattern}; }\nsyntax Inner : expression {"
        rule_eof_source "${rule_eof_source}")
    string(REPLACE "inner 1u32" "inner" rule_eof_source "${rule_eof_source}")
    set(rule_eof_input "${OUTPUT}.rule-eof.x")
    file(WRITE "${rule_eof_input}" "${rule_eof_source}")
    execute_process(COMMAND "${CC}" -S "${rule_eof_input}" -o "${OUTPUT}"
        RESULT_VARIABLE rule_eof_status OUTPUT_VARIABLE rule_eof_out ERROR_VARIABLE rule_eof_err)
    if(rule_eof_status EQUAL 0 OR NOT rule_eof_err MATCHES "syntax-match error" OR
       NOT rule_eof_err MATCHES "syntax 'Inner' defined here" OR
       NOT rule_eof_err MATCHES "while matching syntax rule 'Wrap'" OR
       NOT rule_eof_err MATCHES "syntax rule 'Wrap' defined here" OR
       NOT rule_eof_err MATCHES "while matching syntax rule 'NeedParen'" OR
       NOT rule_eof_err MATCHES "syntax rule 'NeedParen' defined here" OR
       NOT rule_eof_err MATCHES "in expansion of syntax 'Outer'")
        message(FATAL_ERROR "EOF match ancestry lost for ${rule_pattern}\n${rule_eof_out}\n${rule_eof_err}")
    endif()
endforeach()

# Constructing an oversized generated raw group fails before the inner owner is
# matched, so the diagnostic names the outer constructor/owner at that phase.
string(REPEAT "(" 33 group_opens)
string(REPEAT ")" 33 group_closes)
string(REPLACE "inner 1u32" "inner ${group_opens}x${group_closes}"
    group_source "${rule_source}")
set(group_input "${OUTPUT}.group-depth.x")
file(WRITE "${group_input}" "${group_source}")
execute_process(COMMAND "${CC}" -S -feval-depth-limit=32 "${group_input}" -o "${OUTPUT}"
    RESULT_VARIABLE group_status OUTPUT_VARIABLE group_out ERROR_VARIABLE group_err)
if(group_status EQUAL 0 OR NOT group_err MATCHES "token-tree nesting depth exceeded" OR
   NOT group_err MATCHES "while evaluating call to 'outer_expander'" OR
   NOT group_err MATCHES "syntax 'Outer' defined here" OR
   group_err MATCHES "syntax 'Inner' defined here")
    message(FATAL_ERROR "token construction resource-failure ancestry lost\n${group_out}\n${group_err}")
endif()

# A token macro can forward pre-existing opaque input without constructing or
# inspecting it. The reached raw matcher must still enforce its own depth and
# retain the nested rule plus generated-source ancestry.
set(forward_group_source "[[macro]] static $::meta::tokens supply(in $::meta::tokens input) { return input; }\n[[syntax_expander]] static $::meta::tokens inner_expander(in $::meta::syntax_match input) { return $::quote { 7u32 }; }\nsyntax NeedParen : rule { match body:paren; }\nsyntax Inner : expression { prefix \"inner\"; match body:rule(NeedParen); expand inner_expander; }\nsyntax Inner;\nglobal u32 entry() { return supply!(inner ${group_opens}x${group_closes}); }\n")
set(forward_group_input "${OUTPUT}.forward-group-depth.x")
file(WRITE "${forward_group_input}" "${forward_group_source}")
execute_process(COMMAND "${CC}" -S -feval-depth-limit=32 "${forward_group_input}" -o "${OUTPUT}"
    RESULT_VARIABLE forward_group_status OUTPUT_VARIABLE forward_group_out ERROR_VARIABLE forward_group_err)
if(forward_group_status EQUAL 0 OR NOT forward_group_err MATCHES "syntax raw-group nesting depth exceeded" OR
   NOT forward_group_err MATCHES "syntax 'Inner' defined here" OR
   NOT forward_group_err MATCHES "syntax rule 'NeedParen' defined here" OR
   NOT forward_group_err MATCHES "in expansion of procedural macro 'supply'")
    message(FATAL_ERROR "forwarded raw group resource-failure ancestry lost\n${forward_group_out}\n${forward_group_err}")
endif()

# A real speculative core parser (not a raw-group scan) can exhaust its tree
# limit after an independent alternative has matched. Preserve both nested
# rule references and the forwarding macro's ancestry on that fatal path.
set(parsed_limit_input "${OUTPUT}.parsed-limit.x")
file(WRITE "${parsed_limit_input}" [=[
[[macro]] static $::meta::tokens supply(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Expression : rule { match value:expr ";" "missing"; }
syntax Wrapped : rule { match rule(Expression); }
syntax Owner : expression {
    prefix "owner";
    match "[" branch:choice(raw:(value:group ";") | parsed:(rule(Wrapped))) "]";
    expand expand;
}
syntax Owner;
$::static_assert(supply!(owner [ ((((1u32)))); ]) == 1u32, "surviving raw alternative");
]=])
execute_process(COMMAND "${CC}" -S -feval-depth-limit=32 "${parsed_limit_input}" -o "${OUTPUT}"
    RESULT_VARIABLE parsed_limit_status OUTPUT_VARIABLE parsed_limit_out ERROR_VARIABLE parsed_limit_err TIMEOUT 20)
if(NOT parsed_limit_status EQUAL 1 OR
   NOT parsed_limit_err MATCHES "public syntax tree depth, work, or storage budget exceeded" OR
   NOT parsed_limit_err MATCHES "syntax 'Owner' defined here" OR
   NOT parsed_limit_err MATCHES "in expansion of procedural macro 'supply'")
    message(FATAL_ERROR "parsed capture resource-failure ancestry lost\n${parsed_limit_out}\n${parsed_limit_err}")
endif()
foreach(rule IN ITEMS Wrapped Expression)
    if(NOT parsed_limit_err MATCHES "while matching syntax rule '${rule}'" OR
       NOT parsed_limit_err MATCHES "syntax rule '${rule}' defined here")
        message(FATAL_ERROR "parsed capture resource rule ${rule} lost\n${parsed_limit_out}\n${parsed_limit_err}")
    endif()
endforeach()
execute_process(COMMAND "${CC}" -S -feval-depth-limit=256 "${parsed_limit_input}" -o "${OUTPUT}"
    RESULT_VARIABLE parsed_limit_status OUTPUT_VARIABLE parsed_limit_out ERROR_VARIABLE parsed_limit_err TIMEOUT 20)
if(NOT parsed_limit_status EQUAL 0)
    message(FATAL_ERROR "parsed capture ample-depth alternative failed\n${parsed_limit_out}\n${parsed_limit_err}")
endif()

# A deferred branch-local failure still needs the complete rule/owner ancestry
# when no alternative succeeds.
string(REPLACE "syntax NeedParen : rule { match body:paren; }"
    "syntax Pairs : rule { match pairs:repeat1(\"x\" \"y\"); }\nsyntax NeedParen : rule { match branch:choice(pairs:(rule(Pairs)) | other:(\"z\")); }"
    committed_source "${rule_source}")
string(REPLACE "inner 1u32" "inner x y x z" committed_source "${committed_source}")
set(committed_input "${OUTPUT}.committed.x")
file(WRITE "${committed_input}" "${committed_source}")
execute_process(COMMAND "${CC}" -S "${committed_input}" -o "${OUTPUT}"
    RESULT_VARIABLE committed_status OUTPUT_VARIABLE committed_out ERROR_VARIABLE committed_err)
if(committed_status EQUAL 0 OR NOT committed_err MATCHES "malformed syntax repetition after committed start" OR
   NOT committed_err MATCHES "syntax 'Inner' defined here" OR
   NOT committed_err MATCHES "syntax rule 'NeedParen' defined here" OR
   NOT committed_err MATCHES "syntax rule 'Pairs' defined here" OR
   NOT committed_err MATCHES "in expansion of syntax 'Outer'")
    message(FATAL_ERROR "deferred committed-failure ancestry lost\n${committed_out}\n${committed_err}")
endif()

# A committed repetition must retain the failing item's nested rule path, not
# just the rules still active after that item's recursive match has unwound.
# An unrelated failed choice arm may have reached farther into the same input.
foreach(list_kind IN ITEMS repeat1 separated1)
    if(list_kind STREQUAL repeat1)
        set(item_pattern "rule(Pair)")
        set(item_input "inner x () x nope")
        set(probe_pattern "\"x\" body:paren \"x\" value:ident \"tail\"")
        set(commit_error "malformed syntax repetition after committed start")
    else()
        set(item_pattern "rule(Pair), \",\"")
        set(item_input "inner x (), x nope")
        set(probe_pattern "\"x\" body:paren \",\" \"x\" value:ident \"tail\"")
        set(commit_error "malformed syntax item after committed separator")
    endif()
    foreach(branch_order IN ITEMS direct probe_first probe_last)
        if(branch_order STREQUAL direct)
            set(owner_pattern "body:rule(Pairs)")
        elseif(branch_order STREQUAL probe_first)
            set(owner_pattern "body:choice(probe:(rule(Unrelated)) | repeated:(rule(Pairs)))")
        else()
            set(owner_pattern "body:choice(repeated:(rule(Pairs)) | probe:(rule(Unrelated)))")
        endif()
        string(REPLACE "syntax NeedParen : rule { match body:paren; }"
            "syntax NeedParen : rule { match body:paren; }\nsyntax Pair : rule { match \"x\" value:rule(NeedParen); }\nsyntax Pairs : rule { match parts:${list_kind}(${item_pattern}); }\nsyntax Unrelated : rule { match ${probe_pattern}; }"
            item_source "${rule_source}")
        string(REPLACE "body:rule(NeedParen); expand inner_expander;"
            "${owner_pattern}; expand inner_expander;" item_source "${item_source}")
        string(REPLACE "inner 1u32" "${item_input}" item_source "${item_source}")
        set(item_file "${OUTPUT}.${list_kind}-${branch_order}.x")
        file(WRITE "${item_file}" "${item_source}")
        execute_process(COMMAND "${CC}" -S "${item_file}" -o "${OUTPUT}"
            RESULT_VARIABLE item_status OUTPUT_VARIABLE item_out ERROR_VARIABLE item_err)
        if(item_status EQUAL 0 OR NOT item_err MATCHES "${commit_error}" OR
           NOT item_err MATCHES "syntax 'Inner' defined here" OR
           NOT item_err MATCHES "in expansion of syntax 'Outer'" OR
           item_err MATCHES "syntax rule 'Unrelated' defined here")
            message(FATAL_ERROR "wrong committed-item ancestry for ${list_kind}/${branch_order}\n${item_out}\n${item_err}")
        endif()
        foreach(rule IN ITEMS Pairs Pair NeedParen)
            if(NOT item_err MATCHES "while matching syntax rule '${rule}'" OR
               NOT item_err MATCHES "syntax rule '${rule}' defined here")
                message(FATAL_ERROR "missing committed-item rule ${rule} for ${list_kind}/${branch_order}\n${item_out}\n${item_err}")
            endif()
        endforeach()
    endforeach()
endforeach()

# Nested commitments keep the inner failed item path even when the outer list
# subsequently also commits at its earlier start position.
string(REPLACE "syntax NeedParen : rule { match body:paren; }"
    "syntax Number : rule { match value:literal; }\nsyntax NeedParen : rule { match \"(\" items:separated1(rule(Number), \",\") \")\"; }"
    nested_item_source "${item_source}")
string(REPLACE "${item_input}" "inner x (1u32, nope)"
    nested_item_source "${nested_item_source}")
set(nested_item_file "${OUTPUT}.nested-committed-item.x")
file(WRITE "${nested_item_file}" "${nested_item_source}")
execute_process(COMMAND "${CC}" -S "${nested_item_file}" -o "${OUTPUT}"
    RESULT_VARIABLE nested_item_status OUTPUT_VARIABLE nested_item_out ERROR_VARIABLE nested_item_err)
if(nested_item_status EQUAL 0 OR
   NOT nested_item_err MATCHES "malformed syntax item after committed separator" OR
   NOT nested_item_err MATCHES "in expansion of syntax 'Outer'" OR
   nested_item_err MATCHES "syntax rule 'Unrelated' defined here")
    message(FATAL_ERROR "nested committed-item ancestry lost\n${nested_item_out}\n${nested_item_err}")
endif()
foreach(rule IN ITEMS Pairs Pair NeedParen Number)
    if(NOT nested_item_err MATCHES "while matching syntax rule '${rule}'" OR
       NOT nested_item_err MATCHES "syntax rule '${rule}' defined here")
        message(FATAL_ERROR "nested committed-item rule ${rule} lost\n${nested_item_out}\n${nested_item_err}")
    endif()
endforeach()

# Both complete derivations of an ambiguous rule must retain rule provenance.
string(REPLACE "syntax NeedParen : rule { match body:paren; }"
    "syntax NeedX : rule { match branch:choice(left:(\"x\") | right:(\"x\")); }"
    ambiguous_source "${rule_source}")
string(REPLACE "rule(NeedParen)" "rule(NeedX)" ambiguous_source "${ambiguous_source}")
string(REPLACE "inner 1u32" "inner x" ambiguous_source "${ambiguous_source}")
set(ambiguous_input "${OUTPUT}.ambiguous.x")
file(WRITE "${ambiguous_input}" "${ambiguous_source}")
execute_process(COMMAND "${CC}" -S "${ambiguous_input}" -o "${OUTPUT}"
    RESULT_VARIABLE ambiguous_status OUTPUT_VARIABLE ambiguous_out ERROR_VARIABLE ambiguous_err)
if(ambiguous_status EQUAL 0 OR NOT ambiguous_err MATCHES "ambiguous syntax invocation" OR
   NOT ambiguous_err MATCHES "syntax 'Inner' defined here" OR
   NOT ambiguous_err MATCHES "syntax rule 'NeedX' defined here" OR
   NOT ambiguous_err MATCHES "in expansion of syntax 'Outer'")
    message(FATAL_ERROR "ambiguous match ancestry lost\n${ambiguous_out}\n${ambiguous_err}")
endif()

# An evaluator failure happens before output exists, so the owner must be
# attached explicitly rather than relying on replacement-source metadata.
string(REPLACE "return $::quote { 1u32 + ; 2u32 };"
    "return $::syntax::capture(input, \"missing\");" evaluator_source "${original_source}")
set(evaluator_input "${OUTPUT}.evaluator.x")
file(WRITE "${evaluator_input}" "${evaluator_source}")
execute_process(COMMAND "${CC}" -S "${evaluator_input}" -o "${OUTPUT}"
    RESULT_VARIABLE evaluator_status OUTPUT_VARIABLE evaluator_out ERROR_VARIABLE evaluator_err)
if(evaluator_status EQUAL 0 OR NOT evaluator_err MATCHES "no field named 'missing'" OR
   NOT evaluator_err MATCHES "syntax 'Inner' defined here" OR
   NOT evaluator_err MATCHES "in expansion of syntax 'Outer'")
    message(FATAL_ERROR "pre-output evaluator ancestry lost\n${evaluator_out}\n${evaluator_err}")
endif()

# The replacement-depth gate runs before the next owner has generated output.
set(depth_input "${OUTPUT}.depth.x")
file(WRITE "${depth_input}" [=[
[[syntax_expander]] static $::meta::tokens make_a(in $::meta::syntax_match input) {
    return $::meta::concat($::meta::call_site($::meta::parse("b")), $::quote { () });
}
[[syntax_expander]] static $::meta::tokens make_b(in $::meta::syntax_match input) {
    return $::meta::concat($::meta::call_site($::meta::parse("a")), $::quote { () });
}
syntax A : expression { prefix "a"; match body:paren; expand make_a; }
syntax B : expression { prefix "b"; match body:paren; expand make_b; }
syntax A, B;
global u32 entry() { return a (); }
]=])
execute_process(COMMAND "${CC}" -S -feval-depth-limit=2 "${depth_input}" -o "${OUTPUT}"
    RESULT_VARIABLE depth_status OUTPUT_VARIABLE depth_out ERROR_VARIABLE depth_err)
if(depth_status EQUAL 0 OR NOT depth_err MATCHES "expansion depth exceeded" OR
   NOT depth_err MATCHES "syntax 'A' defined here" OR
   NOT depth_err MATCHES "in expansion of syntax 'B'")
    message(FATAL_ERROR "replacement-depth owner lost\n${depth_out}\n${depth_err}")
endif()
