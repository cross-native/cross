# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(check_target name bytes padding)
    foreach(level O0 O2)
        set(output "${OUTPUT}-${name}-${level}.s")
        execute_process(
            COMMAND "${CC}" -S -${level} ${ARGN} "${SOURCE}" -o "${output}"
            RESULT_VARIABLE status
            ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "${name} bit-field compile failed at -${level}:\n${error}")
        endif()
        file(READ "${output}" assembly)
        string(REPLACE "\r\n" "\n" assembly "${assembly}")
        foreach(expected IN ITEMS
                "initialized_flags:\n\t.byte ${bytes}"
                "initialized_padding:\n\t.byte ${padding}")
            string(FIND "${assembly}" "${expected}" found)
            if(found EQUAL -1)
                message(FATAL_ERROR
                    "${name} bit-field bytes at -${level} do not contain "
                    "'${expected}'\n${assembly}")
            endif()
        endforeach()
    endforeach()
endfunction()

check_target(x86-64
    "11,0,0,0,61,1,0,0"
    "163"
    -target x86_64-unknown-linux-gnu)
check_target(mips32-big
    "208,0,0,0,232,0,0,0,144,0,0,0"
    "197"
    -target mips-unknown-elf -mabi=o32 -march=vr4300)
check_target(mips32-little
    "11,0,0,0,29,0,0,0,9,0,0,0"
    "163"
    -target mipsel-unknown-elf -mabi=o32 -march=vr4300)
check_target(mips64-big
    "208,0,0,0,232,0,0,0,144,0,0,0"
    "197"
    -mprofile=mips64-n64)
check_target(mips64-little
    "11,0,0,0,29,0,0,0,9,0,0,0"
    "163"
    -mprofile=mips64el-n64)
