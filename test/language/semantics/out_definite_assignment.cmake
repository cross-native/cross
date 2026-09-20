# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(target IN ITEMS x86 mipsel)
    set(target_flags)
    if(target STREQUAL mipsel)
        set(target_flags -target mipsel-unknown-elf -march=r3000 -mabi=o32)
    endif()
    foreach(level IN ITEMS O0 O2)
        execute_process(
            COMMAND "${CC}" ${target_flags} -${level} -S
                "${SOURCE_DIR}/out_valid.x"
                -o "${OUTPUT_DIR}/out-valid-${target}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "valid out-cell flow rejected (${target}/${level})\n${out}\n${err}")
        endif()
        foreach(name IN ITEMS out_read_before out_partial_branch
                              out_compound_before out_loop_gap out_goto_gap
                              out_pointer_read out_partial_member
                              out_member_read_before out_partial_bitfield)
            execute_process(
                COMMAND "${CC}" ${target_flags} -${level} -S
                    "${SOURCE_DIR}/${name}.x"
                    -o "${OUTPUT_DIR}/${name}-${target}-${level}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
            if(status EQUAL 0)
                message(FATAL_ERROR "invalid out-cell flow accepted: ${name} (${target}/${level})")
            endif()
            if(name STREQUAL out_read_before OR
               name STREQUAL out_compound_before OR
               name STREQUAL out_pointer_read OR
               name STREQUAL out_member_read_before)
                set(expected "read of 'out' parameter")
            else()
                set(expected "normal return leaves 'out' parameter")
            endif()
            if(NOT err MATCHES "${expected}")
                message(FATAL_ERROR "missing ${name} diagnostic (${target}/${level})\n${err}")
            endif()
            if(NOT err MATCHES "${name}\\.x:[0-9]+:[0-9]+")
                message(FATAL_ERROR "diagnostic for ${name} lacks a source location\n${err}")
            endif()
        endforeach()
    endforeach()
endforeach()
