# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid raw CFG source unexpectedly compiled")
endif()
foreach(pattern
        "inconsistent raw stack depth at label 'join'"
        "inconsistent raw stack depth at label 'loop'"
        "unknown same-function raw label 'raw_missing_label::missing'"
        "raw instruction reads undefined machine resource 'flags'")
    if(NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "missing raw CFG diagnostic: ${pattern}\n${stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${HIR_SOURCE}" -o "${OUTPUT}-hir.ll"
    RESULT_VARIABLE hir_status
    OUTPUT_VARIABLE hir_stdout
    ERROR_VARIABLE hir_stderr
)
if(hir_status EQUAL 0 OR NOT hir_stderr MATCHES "duplicate label 'again'")
    message(FATAL_ERROR "missing HIR label diagnostic\n${hir_stderr}")
endif()
