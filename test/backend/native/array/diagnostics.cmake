# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_array_error source needle)
    execute_process(
        COMMAND "${CC}" -S "${SOURCE_DIR}/${source}.x"
                -o "${OUTPUT}-${source}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(status EQUAL 0)
        message(FATAL_ERROR "${source}.x unexpectedly compiled")
    endif()
    string(FIND "${stderr}" "${needle}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "${source}.x did not report '${needle}'\n${stdout}\n${stderr}")
    endif()
endfunction()

expect_array_error(zero_bound
    "fixed array bound must be a positive integer translation-time value")
expect_array_error(omitted_bound
    "an omitted array bound requires a u8 string initializer")
expect_array_error(initializer
    "aggregate initializer requires a brace list")
expect_array_error(vla_inner_bound
    "only the outermost array bound may be a runtime value")
expect_array_error(vla_wide_bound
    "variable-length array bound cannot exceed pointer width")
expect_array_error(vla_goto
    "direct goto would enter or change variable-length array storage state")
expect_array_error(restrict_nonpointer
    "restrict qualifier requires a pointer type")
expect_array_error(aligned_local_invalid
    "aligned argument must be a positive power-of-two integer constant")
expect_array_error(vla_nonempty_errors
    "duplicate destination in aggregate initializer")

execute_process(
    COMMAND "${CC}" -S "${SOURCE_DIR}/vla_nonempty_errors.x"
            -o "${OUTPUT}-vla-nonempty-errors.s"
    RESULT_VARIABLE vla_errors_status
    OUTPUT_VARIABLE vla_errors_stdout
    ERROR_VARIABLE vla_errors_stderr)
if(vla_errors_status EQUAL 0)
    message(FATAL_ERROR "invalid nonempty VLA initializers compiled")
endif()
foreach(message
        "array initializer designator is out of range"
        "string initializer does not fit in the u8 array"
        "aggregate initializer requires a brace list")
    string(FIND "${vla_errors_stderr}" "${message}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "VLA initializer diagnostics lack '${message}'\n"
            "${vla_errors_stdout}\n${vla_errors_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -target mips-unknown-elf
            "${SOURCE_DIR}/vla_nonempty_mips_index_error.x"
            -o "${OUTPUT}-vla-nonempty-mips-index.s"
    RESULT_VARIABLE mips_index_status
    OUTPUT_VARIABLE mips_index_stdout
    ERROR_VARIABLE mips_index_stderr)
if(mips_index_status EQUAL 0 OR NOT mips_index_stderr MATCHES
   "array initializer designator is out of range")
    message(FATAL_ERROR
        "MIPS oversized VLA initializer designator was not diagnosed\n"
        "${mips_index_stdout}\n${mips_index_stderr}")
endif()
