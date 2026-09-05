# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC NATIVE_SOURCE RAW_SOURCE DATA_SOURCE NAMESPACE_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_macho source stem)
    set(assembly "${OUTPUT}-${stem}.s")
    set(object "${OUTPUT}-${stem}.o")
    foreach(mode IN ITEMS assembly object)
        if(mode STREQUAL "assembly")
            set(arguments -S -O2)
            set(result "${assembly}")
        else()
            set(arguments -c -O2)
            set(result "${object}")
        endif()
        execute_process(
            COMMAND "${CC}" ${arguments} -funwind-tables -target x86_64-apple-darwin
                    "${source}" -o "${result}"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr
        )
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "Mach-O ${stem} ${mode} emission failed (${status})\n"
                "${stdout}\n${stderr}")
        endif()
    endforeach()
    file(READ "${object}" magic LIMIT 4 HEX)
    string(TOLOWER "${magic}" magic)
    if(NOT magic STREQUAL "cffaedfe")
        message(FATAL_ERROR
            "${stem} output is not a 64-bit little-endian Mach-O object: ${magic}")
    endif()
endfunction()

compile_macho("${NATIVE_SOURCE}" native)
compile_macho("${RAW_SOURCE}" raw)
compile_macho("${DATA_SOURCE}" data)
compile_macho("${NAMESPACE_SOURCE}" namespace)

file(READ "${OUTPUT}-native.s" native)
foreach(pattern
        ".section __TEXT,__text,regular,pure_instructions"
        ".cfi_startproc"
        ".Lcross.patch.value."
        ".subsections_via_symbols")
    string(FIND "${native}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "managed Mach-O assembly is missing '${pattern}'")
    endif()
endforeach()

file(READ "${OUTPUT}-namespace.s" namespace)
foreach(pattern
        ".globl \"math::twice\""
        ".globl \"app::entry\"")
    string(FIND "${namespace}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "Mach-O assembly did not preserve readable symbol '${pattern}'")
    endif()
endforeach()

file(READ "${OUTPUT}-raw.s" raw)
foreach(pattern
        ".section __TEXT,.boot,regular,pure_instructions"
        "raw_const:"
        "movabsq"
        ".subsections_via_symbols")
    string(FIND "${raw}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "raw Mach-O assembly is missing '${pattern}'")
    endif()
endforeach()

file(READ "${OUTPUT}-data.s" data)
foreach(pattern
        ".section __TEXT,__const"
        ".section __DATA,__data"
        ".section __DATA,.cross.data"
        ".section __DATA,__noinit,zerofill"
        ".no_dead_strip retained_object"
        ".quad addressed_object"
        ".quad addressed_function")
    string(FIND "${data}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "data Mach-O assembly is missing '${pattern}'")
    endif()
endforeach()

foreach(text IN ITEMS native raw data namespace)
    if("${${text}}" MATCHES "(^|\n)\\.(def|type|size|local) ")
        message(FATAL_ERROR
            "${text} Mach-O assembly contains ELF/COFF-only metadata")
    endif()
endforeach()
