# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

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
    set(stem "${OUTPUT}-${level}")
    execute_process(COMMAND "${CC}" -mabi=${ABI} -${level} -fno-eval-calls
        -c "${SOURCE}" -o "${stem}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "token sequence compile failed (${level})\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${HOST_CXX}" "${DRIVER}" "${stem}.o"
        -o "${stem}.exe"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "token sequence link failed (${level})\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${stem}.exe" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "token sequence runtime mismatch (${level}): ${status}")
    endif()
endforeach()

set(limit_input "${OUTPUT}-limits.x")
file(WRITE "${limit_input}"
    "[[macro]] static $::meta::tokens emit(in $::meta::tokens input) { return $::quote { global u32 emitted; }; }\n"
    "emit! {}\n")
foreach(case byte memory steps)
    if(case STREQUAL byte)
        set(option eval-byte-limit)
        set(low 1)
        set(high 4096)
        set(expected "token construction budget exceeded 1 bytes")
    elseif(case STREQUAL memory)
        set(option eval-memory-limit)
        set(low 1)
        set(high 4096)
        set(expected "meta memory budget exceeded 1 bytes")
    else()
        set(option eval-step-limit)
        set(low 1)
        set(high 100)
        set(expected "instruction budget exceeded 1")
    endif()
    execute_process(COMMAND "${CC}" -S "-f${option}=${low}"
        "${limit_input}" -o "${OUTPUT}-${case}-low.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "procedural ${case} limit was not enforced\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${CC}" -S "-f${option}=${high}"
        "${limit_input}" -o "${OUTPUT}-${case}-high.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "procedural ${case} override failed\n${out}\n${err}")
    endif()
endforeach()

foreach(target mips-unknown-elf mipsel-unknown-elf mips64-unknown-elf)
    set(flags -target "${target}")
    if(target STREQUAL mips64-unknown-elf)
        list(APPEND flags -mabi=n64)
    else()
        list(APPEND flags -mabi=o32)
    endif()
    execute_process(COMMAND "${CC}" -S ${flags} "${SOURCE}"
        -o "${OUTPUT}-${target}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "token sequence target compile failed (${target})\n${out}\n${err}")
    endif()
endforeach()

foreach(case bad_at bad_slice bad_concat)
    if(case STREQUAL bad_at)
        set(operation "$::meta::at(input, 2u32)")
        set(expected "at index is outside the token sequence")
    elseif(case STREQUAL bad_slice)
        set(operation "$::meta::slice(input, 1u32, 2u32)")
        set(expected "slice length is outside the token sequence")
    else()
        set(operation "$::meta::concat(input, 3u32)")
        set(expected "concat requires token values")
    endif()
    set(input "${OUTPUT}-${case}.x")
    file(WRITE "${input}"
        "[[macro]] static $::meta::tokens bad(in $::meta::tokens input) { return ${operation}; }\n"
        "global u32 value() { return bad! { (1u32 + 2u32) 3u32 }; }\n")
    execute_process(COMMAND "${CC}" -S "${input}"
        -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${case} was not diagnosed\n${out}\n${err}")
    endif()
endforeach()
