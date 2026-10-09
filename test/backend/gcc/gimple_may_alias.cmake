# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# GIMPLE keeps may_alias on the accessed type, so GCC's type-based alias
# analysis does not reorder the accesses at -O2 and -O3.
foreach(required CC GCC SOURCE HARNESS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

foreach(level O2 O3)
    set(gimple_source "${OUTPUT}.${level}.gimple.c")
    set(executable "${OUTPUT}.${level}.exe")
    execute_process(
        COMMAND "${CC}" -emit-gimple -${level} "${SOURCE}" -o "${gimple_source}"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "Cross GIMPLE serialization failed:\n${err}")
    endif()
    execute_process(
        COMMAND "${GCC}" -c -${level} -fgimple -ffreestanding -fno-builtin
                "${gimple_source}" -o "${OUTPUT}.${level}.o"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GCC rejected Cross GIMPLE:\n${err}")
    endif()
    execute_process(
        COMMAND "${GCC}" -O2 "${HARNESS}" "${OUTPUT}.${level}.o" -o "${executable}"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GCC could not link the may_alias test:\n${err}")
    endif()
    execute_process(COMMAND "${executable}" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "may_alias accesses were reordered at -${level} (${status})")
    endif()
endforeach()
