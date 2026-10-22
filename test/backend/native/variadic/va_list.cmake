# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Forwards unnamed arguments through the va_list state (va_list.x) to glibc
# and to GCC-compiled walkers (va_list_driver.c), at -O0 and -O2, and through
# the LLVM serializer when CLANG names a compiler for its output.
foreach(required CC GCC SOURCE DRIVER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(run_checked label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

run_checked(driver "${GCC}" -O2 -c "${DRIVER}" -o "${OUTPUT}/driver.o")
set(variants O0 O2)
if(DEFINED CLANG AND NOT "${CLANG}" STREQUAL "")
    list(APPEND variants llvm)
endif()
foreach(variant IN LISTS variants)
    set(object "${OUTPUT}/${variant}.o")
    if(variant STREQUAL "llvm")
        run_checked(emit-llvm "${CC}" -O2 -emit-llvm "${SOURCE}" -o "${OUTPUT}/forward.ll")
        run_checked(clang "${CLANG}" -O2 -c "${OUTPUT}/forward.ll" -o "${object}")
    else()
        run_checked(compile-${variant} "${CC}" -${variant} -c "${SOURCE}" -o "${object}")
    endif()
    run_checked(link-${variant} "${GCC}" "${OUTPUT}/driver.o" "${object}"
                -o "${OUTPUT}/${variant}.exe")
    run_checked(run-${variant} "${OUTPUT}/${variant}.exe")
    if(NOT stdout STREQUAL "passed\n")
        message(FATAL_ERROR "${variant} forwarding failed\n${stdout}")
    endif()
endforeach()
