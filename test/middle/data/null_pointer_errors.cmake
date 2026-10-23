# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
file(MAKE_DIRECTORY "${OUTPUT}")
# A nonzero integer, a floating value, and numeric code-label bits are not
# null pointer constants.
foreach(shape scalar aggregate)
    foreach(case "1u32|implicit conversion from integer 'u32' to pointer 'u32 [*]' requires an explicit cast in static initializer"
                 "0.0f64|conversion cannot convert between a pointer and a floating type in static initializer"
                 "zero_label()|implicit conversion from label to pointer 'u32 [*]' is not allowed in static initializer")
        string(FIND "${case}" "|" split)
        string(SUBSTRING "${case}" 0 ${split} value)
        math(EXPR split "${split} + 1")
        string(SUBSTRING "${case}" ${split} -1 expected)
        set(stem "${OUTPUT}/${shape}-${value}")
        if(shape STREQUAL scalar)
            set(source "static u32 *value = ${value};")
        else()
            set(source "static struct Cell {u32 *pointer;} value = {${value}};")
        endif()
        file(WRITE "${stem}.x" "[[eval_only]] static label zero_label() {return (label)0uptr;}\n${source}\n")
        execute_process(COMMAND "${CC}" -S -fno-eval-calls "${stem}.x" -o "${stem}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
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
