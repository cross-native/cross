# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(reject label source expected)
    execute_process(
        COMMAND "${CC}" -S -O0 ${ARGN} "${SOURCE_DIR}/${source}"
                -o "${OUTPUT}-${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

reject(cast-pointer expression_cast_pointer_error.x
       "pointer casts require an integer at least as wide")
reject(cast-float-pointer expression_cast_float_pointer_error.x
       "explicit cast cannot convert between a pointer and a non-integer type")
reject(sizeof-void expression_sizeof_void_error.x
       "sizeof requires a complete object type with fixed size")
reject(pointer-void expression_pointer_void_error.x
       "pointer arithmetic requires a complete pointed-to object type")
reject(static-assert expression_static_assert_error.x
       "[$]::static_assert failed: layout mismatch")
reject(enum-overflow expression_enum_overflow_error.x
       "implicit enumerator value is not representable")
reject(string-overflow expression_string_overflow_error.x
       "string initializer does not fit in the u8 array")
reject(local-string-overflow expression_local_string_overflow_error.x
       "string initializer does not fit in the u8 array")
reject(pointer-void-mips expression_pointer_void_error.x
       "pointer arithmetic requires a complete pointed-to object type"
       -mprofile=mips64-n64)
