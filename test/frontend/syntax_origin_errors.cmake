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
