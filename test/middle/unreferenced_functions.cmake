# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Functions that nothing emitted references are absent from native assembly
# and LLVM text; every kind of root stays.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(absent inlined_step never_called called_only_by_unused group_never_called)
set(present kept_used kept_retained kept_by_address kept_in_table
            kept_label_owner unreferenced_entry)

function(check name)
    foreach(level O0 O2)
        set(output "${OUTPUT}-${name}-${level}")
        execute_process(
            COMMAND "${CC}" ${ARGN} -${level} "${SOURCE}" -o "${output}"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${name} -${level} failed\n${stdout}\n${stderr}")
        endif()
        file(READ "${output}" text)
        foreach(symbol IN LISTS absent)
            if(text MATCHES "${symbol}")
                message(FATAL_ERROR "${name} -${level} emitted ${symbol}")
            endif()
        endforeach()
        foreach(symbol IN LISTS present)
            if(NOT text MATCHES "${symbol}")
                message(FATAL_ERROR "${name} -${level} lacks ${symbol}")
            endif()
        endforeach()
    endforeach()
endfunction()

check(x86_64 -S -target x86_64-unknown-linux-gnu)
check(vr4300 -S -mprofile=vr4300-o32)
if(LLVM_TEXT)
    check(llvm -emit-llvm -target x86_64-unknown-linux-gnu)
endif()
