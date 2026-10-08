# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

foreach(target native mips mipsel mips64 mips64el)
    set(target_flags)
    if(target STREQUAL mips OR target STREQUAL mipsel)
        set(target_flags -target "${target}-unknown-elf" -march=vr4300 -mabi=o32)
    elseif(target MATCHES "^mips64")
        set(target_flags -target "${target}-unknown-elf" -march=mips64 -mabi=n64)
    endif()
    foreach(level O0 O2)
        foreach(evaluation normal noeval)
            set(evaluation_flags)
            if(evaluation STREQUAL noeval)
                set(evaluation_flags -fno-eval-calls)
            endif()
            foreach(mode RANGE 0 22)
                execute_process(COMMAND "${CC}" -S "-${level}" ${evaluation_flags}
                    ${target_flags} "-DMODE=${mode}" "${SOURCE}"
                    -o "${OUTPUT}-${target}-${level}-${evaluation}-${mode}.s"
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
                if(status EQUAL 0 OR NOT err MATCHES "fixed-vector lane index is out of range")
                    message(FATAL_ERROR "${target}/${level}/${evaluation}/${mode}: expected a lane-bound error\n${out}\n${err}")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()
