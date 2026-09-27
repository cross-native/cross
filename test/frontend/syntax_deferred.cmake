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
foreach(depth 1 2 3)
    reject(deferred_depth_${depth} "depth|syntax-match error for active prefix"
        "${discard}${owner} global u32 entry() { syntax Owner; owner { future!{}; NewType value; } return 0u32; }"
        -feval-depth-limit=${depth})
endforeach()
string(REPEAT " NewType value;" 16 storage_payload)
reject(deferred_storage "byte or memory budget|storage budget"
    "${discard}${owner} global u32 entry() { syntax Owner; owner { future!{};${storage_payload} } return 0u32; }"
    -feval-memory-limit=4096)
