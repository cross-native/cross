# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_READELF NAMES llvm-readelf REQUIRED)

foreach(profile mips64-n64 mips64el-n64 vr4300-o32)
    set(object "${OUTPUT}-${profile}.o")
    execute_process(
        COMMAND "${CC}" -c -O2 "-mprofile=${profile}" "${SOURCE}" -o "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${profile} floating constants failed\n${stdout}\n${stderr}")
    endif()
    execute_process(COMMAND "${LLVM_READELF}" -x .data "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE dump ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${profile} object inspection failed\n${stderr}")
    endif()
    if(profile STREQUAL "mips64-n64")
        foreach(pattern "3f800001 00000000 3fd55555 55555555"
                        "80000000 00000000 3ff00000 10000000")
            if(NOT dump MATCHES "${pattern}")
                message(FATAL_ERROR "${profile} lacks '${pattern}'\n${dump}")
            endif()
        endforeach()
    elseif(profile STREQUAL "mips64el-n64")
        foreach(pattern "0100803f 00000000 55555555 5555d53f"
                        "00000080 00000000 00000010 0000f03f")
            if(NOT dump MATCHES "${pattern}")
                message(FATAL_ERROR "${profile} lacks '${pattern}'\n${dump}")
            endif()
        endforeach()
    else()
        if(NOT dump MATCHES "80000000 3f800000")
            message(FATAL_ERROR "${profile} did not round fptr to binary32\n${dump}")
        endif()
    endif()
endforeach()
