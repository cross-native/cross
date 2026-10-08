# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC SOURCE OUTPUT TARGET WIDTH)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
execute_process(COMMAND "${CC}" -S -O0 -fno-eval-calls -target "${TARGET}"
    "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "${TARGET} raw syntax compilation failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}" assembly)
if(NOT assembly MATCHES "syntax_width:\n[^\n]*[.](word|dword|long|quad) ${WIDTH}")
    message(FATAL_ERROR "${TARGET} syntax expander used the wrong target width")
endif()
if(assembly MATCHES "flow.*(copy_body|target_size|returning)|syntax_match|syntax_expander")
    message(FATAL_ERROR "${TARGET} translation-only syntax machinery escaped to assembly")
endif()
