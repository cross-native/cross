# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(expect_failure source output)
    execute_process(
        COMMAND "${CC}" -emit-llvm ${ARGN} "${source}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 1)
        message(FATAL_ERROR "expected diagnostic exit 1 for ${source}, received ${status}\n${stdout}\n${stderr}")
    endif()
    set(LAST_STDERR "${stderr}" PARENT_SCOPE)
endfunction()

expect_failure("${SOURCE}" "${OUTPUT}-mir.ll")
foreach(pattern
        "ordinary return is not permitted in a naked function"
        "ordinary calls are not permitted in a naked function"
        "ordinary automatic and stack objects are not permitted"
        "raw stack depth is 8 bytes at [$]::_ret; expected 0"
        "raw stack instruction reads above the entry stack baseline"
        "instruction after raw terminator is unreachable"
        "target instruction '[$]::_ret' requires 0 operands"
        "reachable end of naked function requires an explicit target control transfer")
    if(NOT LAST_STDERR MATCHES "${pattern}")
        message(FATAL_ERROR "missing raw MIR diagnostic: ${pattern}\n${LAST_STDERR}")
    endif()
endforeach()

set(error_case 0)
foreach(pattern
        "non-void result requires an explicit result location"
        "parameter 'value' has an automatic endpoint"
        "naked does not take arguments")
    expect_failure("${INTERFACE_SOURCE}" "${OUTPUT}-interface-${error_case}.ll"
        "-DRAW_INTERFACE_ERROR=${error_case}")
    if(NOT LAST_STDERR MATCHES "${pattern}" OR
       NOT LAST_STDERR MATCHES "raw_naked_interface_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "missing raw HIR diagnostic: ${pattern}\n${LAST_STDERR}")
    endif()
    math(EXPR error_case "${error_case} + 1")
endforeach()

expect_failure("${CONTEXT_SOURCE}" "${OUTPUT}-context.ll")
if(NOT LAST_STDERR MATCHES "only available in a naked function")
    message(FATAL_ERROR "missing managed/raw ownership diagnostic\n${LAST_STDERR}")
endif()

expect_failure("${RECOVERY_SOURCE}" "${OUTPUT}-recovery.ll")
if(NOT LAST_STDERR MATCHES "expected ';' or function body")
    message(FATAL_ERROR "parser did not diagnose malformed result-location syntax\n${LAST_STDERR}")
endif()
