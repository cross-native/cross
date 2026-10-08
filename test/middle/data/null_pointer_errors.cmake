# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
file(MAKE_DIRECTORY "${OUTPUT}")
foreach(shape scalar aggregate)
    foreach(value 1u32 0.0f64 "zero_label()")
        set(stem "${OUTPUT}/${shape}-${value}")
        if(shape STREQUAL scalar)
            set(source "static u32 *value = ${value};")
        else()
            set(source "static struct Cell {u32 *pointer;} value = {${value}};")
        endif()
        file(WRITE "${stem}.x" "[[eval_only]] static label zero_label() {return (label)0uptr;}\n${source}\n")
        execute_process(COMMAND "${CC}" -S -fno-eval-calls "${stem}.x" -o "${stem}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 1 OR NOT err MATCHES "global pointer/label initializer is not an address constant" OR
            NOT err MATCHES "[.]x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "invalid ${shape}/${value} initializer was not diagnosed\n${out}\n${err}")
        endif()
    endforeach()
endforeach()
# A source null proof must not be inferred from a runtime value or an
# effectful call. Other unsupported narrow casts retain their width error.
foreach(value 1u8 input "(u8)(input - input)" "runtime_zero()")
    string(SHA256 key "${value}")
    string(SUBSTRING "${key}" 0 12 key)
    set(stem "${OUTPUT}/narrow-cast-${key}")
    file(WRITE "${stem}.x" "typedef u32 (*Callback)();\nstatic u32 effects;\nstatic u8 runtime_zero() { ++effects; return 0u8; }\nglobal Callback entry(in u8 input) { return (Callback)${value}; }\n")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls
            "${stem}.x" -o "${stem}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 1 OR NOT err MATCHES "pointer casts require an integer at least as wide" OR
            NOT err MATCHES "[.]x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "unsupported narrow cast ${value}/${level} was not diagnosed\n${out}\n${err}")
        endif()
    endforeach()
endforeach()
