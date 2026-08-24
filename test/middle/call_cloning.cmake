# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly suffix)
    execute_process(
        COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "call-clone compile ${suffix} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

compile_assembly(O3 -O3)
compile_assembly(O2 -O2)
compile_assembly(disabled -O3 -fno-ipa-cp-clone)

file(READ "${OUTPUT}.O3.s" o3)
file(READ "${OUTPUT}.O2.s" o2)
file(READ "${OUTPUT}.disabled.s" disabled)

if(NOT o3 MATCHES "call[\t ]+__cross_clone_" OR
   NOT o3 MATCHES "call[\t ]+__cross_group_clone_target")
    message(FATAL_ERROR
        "O3 did not specialize only the constant surviving call\n${o3}")
endif()
if(o2 MATCHES "__cross_clone_" OR disabled MATCHES "__cross_clone_")
    message(FATAL_ERROR
        "call cloning ignored its O preset or explicit negative flag")
endif()

execute_process(
    COMMAND "${CC}" -c -O3 "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "specialized object failed\n${object_stdout}\n${object_stderr}")
endif()
