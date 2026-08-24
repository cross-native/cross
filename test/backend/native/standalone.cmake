# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

execute_process(
    COMMAND "${CC}" -S -O2 "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cc failed (${result})\n${stdout}\n${stderr}")
endif()

file(READ "${OUTPUT}" assembly)
if(NOT assembly MATCHES "entry")
    message(FATAL_ERROR "assembly does not contain the readable entry symbol")
endif()
if(assembly MATCHES "(memcpy|memset|fmod|fmodf|fmodl|__chkstk|__main|___main)")
    message(FATAL_ERROR "standalone assembly contains a hidden runtime reference")
endif()
