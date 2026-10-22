# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs c_forms.x against the C++ reference in c_forms_driver.cpp.
foreach(required CC HOST_CXX SOURCE DRIVER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(WIN32)
    set(ABI ms_abi)
else()
    set(ABI sysv_abi)
endif()
foreach(level O0 O2)
    foreach(mode normal noeval)
        set(flags -${level})
        if(mode STREQUAL noeval)
            list(APPEND flags -fno-eval-calls)
        endif()
        set(stem "${OUTPUT}-${level}-${mode}")
        execute_process(COMMAND "${CC}" -mabi=${ABI} ${flags} -c "${SOURCE}" -o "${stem}.o"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "Cross compile failed (${level}/${mode})\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${HOST_CXX}" -std=c++20 "${DRIVER}" "${stem}.o" -o "${stem}.exe"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "runtime link failed (${level}/${mode})\n${out}\n${err}")
        endif()
        execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status
            OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "C forms differ from C++ (${level}/${mode})\n${out}\n${err}")
        endif()
    endforeach()
endforeach()
# The variadic promotions are ABI-neutral; the MIPS o32 C ABI compiles them too.
foreach(target mips-unknown-elf mipsel-unknown-elf)
    execute_process(COMMAND "${CC}" -target ${target} -mprofile=r3000-o32 -mabi=o32 -O2
        -S "${SOURCE}" -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "C forms do not compile for ${target}\n${out}\n${err}")
    endif()
endforeach()
