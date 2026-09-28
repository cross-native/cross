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
if(depth_status EQUAL 0 OR NOT depth_err MATCHES "expansion depth or invocation budget exceeded" OR
   NOT depth_err MATCHES "syntax 'A' defined here" OR
   NOT depth_err MATCHES "in expansion of syntax 'B'")
    message(FATAL_ERROR "replacement-depth owner lost\n${depth_out}\n${depth_err}")
endif()
