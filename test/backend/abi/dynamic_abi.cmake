# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly suffix)
    execute_process(
        COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "dynamic ABI compile ${suffix} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

compile_assembly(dynamic)
compile_assembly(stable -fno-private-abi)

file(READ "${OUTPUT}.dynamic.s" dynamic)
file(READ "${OUTPUT}.stable.s" stable)

string(REGEX MATCH
    "__cross_group_private_identity:[^#]*retq"
    dynamic_identity "${dynamic}")
string(REGEX MATCH
    "__cross_group_private_choose:[^#]*retq"
    dynamic_choose "${dynamic}")
string(REGEX MATCH
    "__cross_group_private_identity:[^#]*retq"
    stable_identity "${stable}")
string(REGEX MATCH
    "__cross_group_private_choose:[^#]*retq"
    stable_choose "${stable}")

if(NOT dynamic_identity MATCHES "%r11" OR
   NOT dynamic_identity MATCHES "%rax")
    message(FATAL_ERROR
        "dynamic return affinity did not alter the private identity boundary\n${dynamic_identity}")
endif()
if(NOT dynamic_choose MATCHES "%al" OR
   NOT dynamic_choose MATCHES "%r11" OR
   NOT dynamic_choose MATCHES "%r10")
    message(FATAL_ERROR
        "dynamic private parameter placement is missing\n${dynamic_choose}")
endif()
if(NOT stable_identity MATCHES "%r10" OR
   NOT stable_identity MATCHES "%r9" OR
   NOT stable_choose MATCHES "%r8")
    message(FATAL_ERROR
        "disabling private ABI selection did not restore the selected Cross ABI\n${stable_identity}\n${stable_choose}")
endif()

execute_process(
    COMMAND "${CC}" -c -O2 "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
if(NOT object_status EQUAL 0)
    message(FATAL_ERROR
        "dynamic ABI object failed\n${object_stdout}\n${object_stderr}")
endif()
