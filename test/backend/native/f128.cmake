# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR
        "native f128 compilation failed\n${compile_stdout}\n${compile_stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
if(assembly MATCHES "__(add|sub|mul|div)tf3" OR
   NOT assembly MATCHES "btcq[\t ]+\\\$63" OR
   NOT assembly MATCHES "fcmp128")
    message(FATAL_ERROR
        "f128 constants/comparisons were not lowered inline\n${assembly}")
endif()
