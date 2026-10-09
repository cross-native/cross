# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE POSITIVE_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -c "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR
   NOT stderr MATCHES "generic value argument is not representable in parameter type 'enum generic_small'")
    message(FATAL_ERROR "out-of-range enum generic was not diagnosed\n${stdout}\n${stderr}")
endif()

foreach(case
        "UNKNOWN_ENUM|unknown enumeration type 'missing'"
        "DUPLICATE|duplicate generic parameter 'T'"
        "EMPTY|a generic parameter list cannot be empty"
        "FLOAT|generic value parameter requires an integer"
        "DEPENDENT_FLOAT|generic value parameter requires an integer"
        "MISSING_NAME|expected an unqualified generic parameter name")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 selector)
    list(GET fields 1 diagnostic)
    execute_process(
        COMMAND "${CC}" "-DTEST_${selector}" -c "${SOURCE}" -o "${OUTPUT}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
        TIMEOUT 10
    )
    if(status EQUAL 0 OR NOT stderr MATCHES "${diagnostic}")
        message(FATAL_ERROR "${selector} generic parameter was not diagnosed\n${stdout}\n${stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O0 "${POSITIVE_SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "enum generic assembly failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
string(REGEX MATCHALL "shade_identity::<[^\n]+>\":" definitions "${assembly}")
list(LENGTH definitions count)
if(NOT count EQUAL 2 OR
   NOT assembly MATCHES "shade_identity::<enum generic_shade=7u8>")
    message(FATAL_ERROR "enum generic instances did not retain normalized nominal identity\n${assembly}")
endif()

execute_process(
    COMMAND "${CC}" -c -O0 -mmangling=simple "${POSITIVE_SOURCE}"
            -o "${OUTPUT}.simple.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "enum generic with alternate mangler failed\n${stdout}\n${stderr}")
endif()

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
                "-mabi=${abi}" -${level} -c
                "${POSITIVE_SOURCE}" -o "${OUTPUT}.${triple}.o"
        RESULT_VARIABLE mips_status
        OUTPUT_VARIABLE mips_stdout
        ERROR_VARIABLE mips_stderr
    )
    if(NOT mips_status EQUAL 0)
        message(FATAL_ERROR "enum generic failed on ${triple} ${level}\n${mips_stdout}\n${mips_stderr}")
    endif()
endforeach()
