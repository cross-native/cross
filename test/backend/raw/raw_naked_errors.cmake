# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(expect_failure source output)
    execute_process(
        COMMAND "${CC}" -emit-llvm "${source}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0)
        message(FATAL_ERROR "invalid raw source unexpectedly compiled: ${source}")
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

expect_failure("${INTERFACE_SOURCE}" "${OUTPUT}-interface.ll")
foreach(pattern
        "non-void result requires an explicit result location"
        "parameter 'value' has an automatic endpoint"
        "naked does not take arguments")
    if(NOT LAST_STDERR MATCHES "${pattern}")
        message(FATAL_ERROR "missing raw HIR diagnostic: ${pattern}\n${LAST_STDERR}")
    endif()
endforeach()

expect_failure("${CONTEXT_SOURCE}" "${OUTPUT}-context.ll")
if(NOT LAST_STDERR MATCHES "only available in a naked function")
    message(FATAL_ERROR "missing managed/raw ownership diagnostic\n${LAST_STDERR}")
endif()

expect_failure("${RECOVERY_SOURCE}" "${OUTPUT}-recovery.ll")
if(NOT LAST_STDERR MATCHES "expected ';' or function body")
    message(FATAL_ERROR "parser did not diagnose malformed result-location syntax\n${LAST_STDERR}")
endif()
