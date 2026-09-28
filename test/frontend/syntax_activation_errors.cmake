# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(declaration_input "${OUTPUT}.declaration.x")
file(WRITE "${declaration_input}" [=[
syntax Broken : expression { prefix "broken"; match item:ident item:ident; expand expand; }
]=])
execute_process(COMMAND "${CC}" -S "${declaration_input}" -o "${OUTPUT}"
    RESULT_VARIABLE declaration_status OUTPUT_VARIABLE declaration_out ERROR_VARIABLE declaration_err)
if(declaration_status EQUAL 0 OR NOT declaration_err MATCHES "duplicate syntax capture field" OR
   NOT declaration_err MATCHES "while declaring syntax 'Broken'")
    message(FATAL_ERROR "definition diagnostic lost its owner\n${declaration_out}\n${declaration_err}")
endif()

set(missing_match_end "${OUTPUT}.missing-match-end.x")
file(WRITE "${missing_match_end}" "syntax Broken : rule { match value:literal\n")
execute_process(COMMAND "${CC}" -S "${missing_match_end}" -o "${OUTPUT}"
    RESULT_VARIABLE missing_status OUTPUT_VARIABLE missing_out ERROR_VARIABLE missing_err)
if(missing_status EQUAL 0 OR NOT missing_err MATCHES "expected ';' after syntax match" OR
   NOT missing_err MATCHES "while declaring syntax 'Broken'")
    message(FATAL_ERROR "unterminated match was not diagnosed\n${missing_out}\n${missing_err}")
endif()

set(cycle_input "${OUTPUT}.cycle.x")
file(WRITE "${cycle_input}" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Loop : rule { match maybe:optional("x") rule(Loop); }
syntax Owner : expression { prefix "owner"; match child:rule(Loop); expand expand; }
syntax Owner;
]=])
execute_process(COMMAND "${CC}" -S "${cycle_input}" -o "${OUTPUT}"
    RESULT_VARIABLE cycle_status OUTPUT_VARIABLE cycle_out ERROR_VARIABLE cycle_err)
if(cycle_status EQUAL 0 OR NOT cycle_err MATCHES "left-recursive or nullable syntax rule cycle" OR
   NOT cycle_err MATCHES "syntax rule 'Loop' participates in this cycle")
    message(FATAL_ERROR "rule-cycle ancestry lost\n${cycle_out}\n${cycle_err}")
endif()

set(bundle_input "${OUTPUT}.bundle.x")
file(WRITE "${bundle_input}" [=[
syntax Leaf : expression { prefix "leaf"; match body:paren; expand missing; }
syntax Pack : bundle { use Leaf; }
syntax Pack;
]=])
execute_process(COMMAND "${CC}" -S "${bundle_input}" -o "${OUTPUT}"
    RESULT_VARIABLE bundle_status OUTPUT_VARIABLE bundle_out ERROR_VARIABLE bundle_err)
foreach(expected "syntax expander is not visible"
                 "while activating syntax 'Leaf'"
                 "syntax 'Leaf' defined here"
                 "while activating syntax 'Pack'"
                 "syntax 'Pack' defined here")
    if(bundle_status EQUAL 0 OR NOT bundle_err MATCHES "${expected}")
        message(FATAL_ERROR "bundle activation lost '${expected}'\n${bundle_out}\n${bundle_err}")
    endif()
endforeach()

set(rule_input "${OUTPUT}.rule.x")
file(WRITE "${rule_input}" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax Child : rule { match body:rule(Missing); }
syntax Owner : expression { prefix "owner"; match child:rule(Child); expand expand; }
syntax Owner;
]=])
execute_process(COMMAND "${CC}" -S "${rule_input}" -o "${OUTPUT}"
    RESULT_VARIABLE rule_status OUTPUT_VARIABLE rule_out ERROR_VARIABLE rule_err)
foreach(expected "syntax entity is not visible: 'Missing'"
                 "while binding syntax 'Child'"
                 "while binding syntax 'Owner'"
                 "while activating syntax 'Owner'"
                 "syntax 'Owner' defined here")
    if(rule_status EQUAL 0 OR NOT rule_err MATCHES "${expected}")
        message(FATAL_ERROR "rule binding lost '${expected}'\n${rule_out}\n${rule_err}")
    endif()
endforeach()
