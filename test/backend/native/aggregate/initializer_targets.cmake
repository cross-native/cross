# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

function(check_target name point relocation text layout)
    foreach(level O0 O2)
        set(output "${OUTPUT}-${name}-${level}.s")
        execute_process(
            COMMAND "${CC}" -S -${level} ${ARGN} "${SOURCE}" -o "${output}"
            RESULT_VARIABLE status
            ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR
                "${name} aggregate initializer compile failed at -${level}:\n${error}")
        endif()
        file(READ "${output}" assembly)
        string(REPLACE "\r\n" "\n" assembly "${assembly}")
        foreach(expected IN ITEMS
                "initialized_point:\n\t.byte ${point}"
                "\t.${relocation} initialized_node"
                "initialized_text:\n\t.byte ${text}"
                "initialized_narrow:\n\t.byte 44"
                "initialized_layout:\n\t.byte ${layout}")
            string(FIND "${assembly}" "${expected}" found)
            if(found EQUAL -1)
                message(FATAL_ERROR
                    "${name} aggregate initializer output at -${level} "
                    "does not contain '${expected}'\n${assembly}")
            endif()
        endforeach()
    endforeach()
endfunction()

check_target(mips32-big
    "0,0,0,12,0,0,0,13" long
    "65,66,0,0,0,0,0,4" "0,0,0,8,0,0,0,4"
    -target mips-unknown-elf -mabi=o32 -march=vr4300)
check_target(mips32-little
    "12,0,0,0,13,0,0,0" long
    "65,66,0,0,4,0,0,0" "8,0,0,0,4,0,0,0"
    -target mipsel-unknown-elf -mabi=o32 -march=vr4300)
check_target(mips64-big
    "0,0,0,12,0,0,0,13" quad
    "65,66,0,0,0,0,0,4"
    "0,0,0,0,0,0,0,8,0,0,0,0,0,0,0,8"
    -mprofile=mips64-n64)
check_target(mips64-little
    "12,0,0,0,13,0,0,0" quad
    "65,66,0,0,4,0,0,0"
    "8,0,0,0,0,0,0,0,8,0,0,0,0,0,0,0"
    -mprofile=mips64el-n64)
