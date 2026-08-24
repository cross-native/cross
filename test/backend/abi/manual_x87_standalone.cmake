# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(target IN ITEMS x86_64-w64-windows-gnu x86_64-unknown-linux-gnu)
    string(REPLACE "-" "_" suffix "${target}")
    set(output "${OUTPUT}-${suffix}.s")
    execute_process(
        COMMAND "${CC}" -S -O2 -target "${target}" "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "x87 ${target} compilation failed (${status})\n${stdout}\n${stderr}")
    endif()
    file(READ "${output}" assembly)
    if(NOT assembly MATCHES "fldt" OR NOT assembly MATCHES "fstpt")
        message(FATAL_ERROR "x87 ${target} assembly lacks extended load/store boundaries")
    endif()
    if(assembly MATCHES "(memcpy|memset|fmod|fmodf|fmodl|__chkstk|__main|___main)")
        message(FATAL_ERROR "x87 ${target} assembly contains a hidden runtime reference")
    endif()
endforeach()
