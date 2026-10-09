# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC ERROR_SOURCE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(capture option output_name)
    execute_process(
        COMMAND "${CC}" "${option}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${option} failed\n${error}")
    endif()
    set("${output_name}" "${output}" PARENT_SCOPE)
endfunction()

capture("--version" version)
if(NOT version MATCHES "language 0\\.8")
    message(FATAL_ERROR "version output does not identify Cross 0.8\n${version}")
endif()

capture("--print-keywords" keywords)
foreach(keyword bool global i32 in namespace static u128 volatile while)
    if(NOT keywords MATCHES "(^|\n)${keyword}(\r?\n|$)")
        message(FATAL_ERROR "keyword registry is missing '${keyword}'\n${keywords}")
    endif()
endforeach()
foreach(non_keyword asm auto char double extern float int long)
    if(keywords MATCHES "(^|\n)${non_keyword}(\r?\n|$)")
        message(FATAL_ERROR "'${non_keyword}' must not be reserved\n${keywords}")
    endif()
endforeach()

capture("--print-builtins" builtins)
foreach(name
        "$::eval translation-time requirement"
        "$::runtime staged-evaluation barrier"
        "$::has_feature query"
        "$::atomic_compare_exchange atomic intrinsic"
        "$::_movabs instruction")
    string(FIND "${builtins}" "${name}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "built-in registry is missing '${name}'\n${builtins}")
    endif()
endforeach()

capture("--print-attributes" attributes)
foreach(name abi aligned macro naked operator section)
    if(NOT attributes MATCHES "(^|\n)${name}(\r?\n|$)")
        message(FATAL_ERROR "attribute registry is missing '${name}'\n${attributes}")
    endif()
endforeach()
# Generic parameters are spelled only as an angle list after the name.
if(attributes MATCHES "(^|\n)generic(\r?\n|$)")
    message(FATAL_ERROR "'generic' must not be an attribute\n${attributes}")
endif()

capture("--print-features" features)
foreach(feature evaluation automatic_evaluation generics procedural_macros fixed_vectors atomics variadics
        contextual_attributes external_models operator_binding)
    string(FIND "${features}" "$::feature::${feature}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "feature registry is missing '${feature}'\n${features}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -emit-llvm "${ERROR_SOURCE}"
            -o "${CMAKE_CURRENT_BINARY_DIR}/language-surface-errors.ll"
    RESULT_VARIABLE diagnostic_status
    OUTPUT_VARIABLE diagnostic_output
    ERROR_VARIABLE diagnostic_error
)
if(diagnostic_status EQUAL 0)
    message(FATAL_ERROR "familiar C spellings were unexpectedly accepted")
endif()
foreach(message
        "'extern' is not Cross syntax"
        "Cross integers use exact names such as i32"
        "no visible Cross type named 'double'; use f64")
    string(FIND "${diagnostic_error}" "${message}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "language diagnostic is missing '${message}'\n${diagnostic_error}")
    endif()
endforeach()
