# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A value that no rule of its MIPS ABI entry places is diagnosed once at its
# declaration or call, naming the entry and the value's type.
foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(triple mips-unknown-elf mipsel-unknown-elf)
    foreach(level O0 O2)
        execute_process(
            COMMAND "${CC}" -S -${level} -target ${triple} -march=vr4300
                    "--model=${MODEL}" "${SOURCE}"
                    -o "${OUTPUT}-${triple}-${level}.s"
            RESULT_VARIABLE status
            OUTPUT_VARIABLE stdout
            ERROR_VARIABLE stderr
        )
        if(status EQUAL 0)
            message(FATAL_ERROR "unplaceable interfaces were accepted for ${triple} -${level}")
        endif()
        foreach(expected
                "placement_errors.x:7:[0-9]+: error: ABI 'mips-regs' cannot place the result of type 'struct pair'"
                "placement_errors.x:18:[0-9]+: error: ABI 'mips-regs' cannot place the result of type 'struct pair'"
                "placement_errors.x:22:[0-9]+: error: ABI 'mips-regs' cannot place the result of type 'struct pair'"
                "placement_errors.x:26:42: error: ABI 'mips-regs-only' cannot place parameter 'value' of type 'struct pair'"
                "placement_errors.x:34:[0-9]+: error: ABI 'mips-regs-only' cannot place argument 1 of type 'struct pair'")
            if(NOT stderr MATCHES "${expected}")
                message(FATAL_ERROR "missing diagnostic for ${triple} -${level}: ${expected}\n${stderr}")
            endif()
        endforeach()
        string(REGEX MATCHALL "error:" errors "${stderr}")
        list(LENGTH errors count)
        if(NOT count EQUAL 5)
            message(FATAL_ERROR "expected 5 errors for ${triple} -${level}, got ${count}\n${stderr}")
        endif()
    endforeach()
endforeach()
