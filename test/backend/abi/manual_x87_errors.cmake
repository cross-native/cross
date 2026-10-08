# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -emit-llvm "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0)
    message(FATAL_ERROR "invalid x87 source unexpectedly compiled")
endif()
foreach(pattern
        "x87 input endpoints must form a dense prefix from st0"
        "duplicate x87 input endpoint 'st0'"
        "ordinary result overlaps x87 output endpoint 'st0'"
        "does not match parameter type 'i32'"
        "does not match parameter type 'f80'"
        "indirect endpoint requires a 64-bit integer address register"
        "target register 'st0' cannot carry a managed hard-bound scalar")
    if(NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "missing x87 diagnostic: ${pattern}\n${stderr}")
    endif()
endforeach()

# Source operand constraints precede ABI lowering. Keep this rejection separate
# so it cannot hide the independent endpoint diagnostics above.
execute_process(
    COMMAND "${CC}" -emit-llvm -DTEST_REMAINDER "${SOURCE}" -o "${OUTPUT}.remainder"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 1 OR NOT stderr MATCHES "operator requires integer operands" OR
   NOT stderr MATCHES "manual_x87_errors.x:[0-9]+:[0-9]+: error:")
    message(FATAL_ERROR "missing source-level floating remainder diagnostic\n${stdout}\n${stderr}")
endif()
