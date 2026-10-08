# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE CONFLICT_SOURCE GENERIC_SOURCE INLINE_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

foreach(case RANGE 0 1)
    execute_process(
        COMMAND "${CC}" -DCASE=${case} -emit-llvm "${SOURCE}" -o "${OUTPUT}-${case}.ll"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0)
        message(FATAL_ERROR "invalid patch value ${case} unexpectedly compiled")
    endif()
    if(case EQUAL 0)
        set(pattern "runtime local or parameter is not a translation-time value")
    else()
        set(pattern "no contiguous $::patch materializer for u128")
    endif()
    string(FIND "${stderr}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "patch diagnostics are missing '${pattern}'\n${stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${INLINE_SOURCE}"
            -o "${OUTPUT}-inline.ll"
    RESULT_VARIABLE inline_status
    OUTPUT_VARIABLE inline_stdout
    ERROR_VARIABLE inline_stderr
)
if(inline_status EQUAL 0)
    message(FATAL_ERROR "patch-bearing mandatory-inline functions compiled")
endif()
foreach(pattern
        "raw_inline function cannot contain a sink-bearing $::patch"
        "always_inline function cannot contain a sink-bearing $::patch")
    string(FIND "${inline_stderr}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "inline patch diagnostic is missing '${pattern}'\n${inline_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${CONFLICT_SOURCE}"
            -o "${OUTPUT}-conflict.ll"
    RESULT_VARIABLE conflict_status
    OUTPUT_VARIABLE conflict_stdout
    ERROR_VARIABLE conflict_stderr
)
if(conflict_status EQUAL 0)
    message(FATAL_ERROR "duplicated patch sink unexpectedly compiled")
endif()
string(FIND "${conflict_stderr}"
       "$::patch address sink is used by more than one site"
       conflict_position)
if(conflict_position EQUAL -1)
    message(FATAL_ERROR "patch sink conflict diagnostic is missing\n${conflict_stderr}")
endif()

execute_process(
    COMMAND "${CC}" -emit-llvm "${GENERIC_SOURCE}"
            -o "${OUTPUT}-generic.ll"
    RESULT_VARIABLE generic_status
    OUTPUT_VARIABLE generic_stdout
    ERROR_VARIABLE generic_stderr
)
if(generic_status EQUAL 0)
    message(FATAL_ERROR "generic patch sink reuse unexpectedly compiled")
endif()
string(FIND "${generic_stderr}"
       "$::patch address sink is used by more than one site"
       generic_position)
if(generic_position EQUAL -1)
    message(FATAL_ERROR "generic patch sink diagnostic is missing\n${generic_stderr}")
endif()
