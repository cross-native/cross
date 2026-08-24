# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()

function(expect_variadic_error source needle)
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

expect_variadic_error(no_named_parameter
    "requires at least one named parameter")
expect_variadic_error(non_input_parameter
    "fixed parameter of a variadic function must use 'in'")
expect_variadic_error(non_variadic_attribute
    "variadic attribute requires a variadic function definition")
expect_variadic_error(unknown_state
    "unknown variadic ABI state 'missing'")
expect_variadic_error(wrong_state_type
    "requires binding type 'u32'")
expect_variadic_error(declaration_binding
    "state bindings are allowed only on a definition")
expect_variadic_error(too_few_arguments
    "requires 1 or more arguments")
