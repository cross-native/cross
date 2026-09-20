# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE POSITIVE_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(case
        "mips-unknown-elf,vr4300,o32,O0"
        "mipsel-unknown-elf,vr4300,o32,O3"
        "mips64-unknown-elf,mips64,n64,O0"
        "mips64el-unknown-elf,mips64,n64,O3")
    string(REPLACE "," ";" fields "${case}")
    list(GET fields 0 triple)
    list(GET fields 1 march)
    list(GET fields 2 abi)
    list(GET fields 3 level)
    execute_process(
        COMMAND "${CC}" -target "${triple}" "-march=${march}"
                "-mabi=${abi}" -DTEST_MIPS -${level} -c
                "${POSITIVE_SOURCE}" -o "${OUTPUT}.${triple}.o"
        RESULT_VARIABLE mips_status
        OUTPUT_VARIABLE mips_stdout
        ERROR_VARIABLE mips_stderr
    )
    if(NOT mips_status EQUAL 0)
        message(FATAL_ERROR "label generic failed on ${triple} ${level}\n${mips_stdout}\n${mips_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -c "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR
   NOT stderr MATCHES "generic label argument requires a visible global label")
    message(FATAL_ERROR "non-transportable generic label was not diagnosed\n${stdout}\n${stderr}")
endif()

foreach(level O0 O3)
    execute_process(
        COMMAND "${CC}" -target x86_64-unknown-linux-gnu
                -mabi=sysv_abi -${level} -c "${POSITIVE_SOURCE}"
                -o "${OUTPUT}.${level}.sysv.o"
        RESULT_VARIABLE sysv_status
        OUTPUT_VARIABLE sysv_stdout
        ERROR_VARIABLE sysv_stderr
    )
    if(NOT sysv_status EQUAL 0)
        message(FATAL_ERROR "custom-register label generic failed on SysV ${level}\n${sysv_stdout}\n${sysv_stderr}")
    endif()
endforeach()
