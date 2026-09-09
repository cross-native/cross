# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(case signature count mode abi object global)
    execute_process(COMMAND "${CC}" -O2 -S
        "${CMAKE_CURRENT_LIST_DIR}/bad_${case}.x" -o "${OUTPUT}-${case}.s"
        RESULT_VARIABLE status ERROR_VARIABLE error)
    if(status EQUAL 0)
        message(FATAL_ERROR "invalid function pointer accepted: ${case}")
    endif()
    if(case STREQUAL count)
        set(expected "requires 1 arguments")
    elseif(case STREQUAL object OR case STREQUAL global)
        set(expected "incompatible signature or ABI")
    else()
        set(expected "incompatible pointee types")
    endif()
    if(NOT error MATCHES "${expected}" OR NOT error MATCHES "bad_${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "missing source-located diagnostic: ${case}\n${error}")
    endif()
endforeach()
