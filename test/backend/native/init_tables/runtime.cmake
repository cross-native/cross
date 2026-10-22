# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# [[constructor]] and [[destructor]] on ELF with the system C runtime: GCC
# links tables.x, compiled to an object at -O0 and -O2, through GNU as from
# -S output, with per-entity sections and --gc-sections, and through the
# LLVM serializer when CLANG names a compiler for its output; glibc's
# startup and exit code walk the tables. The sections have the ELF types of
# their names.
foreach(required CC GCC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
find_program(READELF NAMES llvm-readelf readelf)
if(NOT READELF)
    message(FATAL_ERROR "llvm-readelf or readelf is required")
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
    set(stderr "${stderr}" PARENT_SCOPE)
endfunction()

# Lower priorities construct first and destroy last; entries of one
# priority, here plain() and explicit_default(), run in either order.
set(expected "^1oabs2r(ce|ec)\nd\nd200\nd101\n$")
set(variants O0 O2 assembly sections)
if(DEFINED CLANG AND NOT "${CLANG}" STREQUAL "")
    list(APPEND variants llvm)
endif()
foreach(variant IN LISTS variants)
    set(object "${OUTPUT}/${variant}.o")
    set(link_flags "")
    if(variant STREQUAL "assembly")
        run_checked(compile-${variant} "${CC}" -O2 -S "${SOURCE}" -o "${OUTPUT}/tables.s")
        run_checked(gnu-as "${GCC}" -c "${OUTPUT}/tables.s" -o "${object}")
        if(NOT stderr STREQUAL "")
            message(FATAL_ERROR "GNU as warned about the table sections\n${stderr}")
        endif()
    elseif(variant STREQUAL "llvm")
        run_checked(emit-llvm "${CC}" -O2 -emit-llvm "${SOURCE}" -o "${OUTPUT}/tables.ll")
        run_checked(clang "${CLANG}" -O2 -c "${OUTPUT}/tables.ll" -o "${object}")
    elseif(variant STREQUAL "sections")
        run_checked(compile-${variant} "${CC}" -O2 -ffunction-sections -fdata-sections
                    -c "${SOURCE}" -o "${object}")
        set(link_flags -Wl,--gc-sections)
    else()
        run_checked(compile-${variant} "${CC}" -${variant} -c "${SOURCE}" -o "${object}")
    endif()
    run_checked(link-${variant} "${GCC}" ${link_flags} "${object}" -o "${OUTPUT}/${variant}.exe")
    run_checked(run-${variant} "${OUTPUT}/${variant}.exe")
    if(NOT stdout MATCHES "${expected}")
        message(FATAL_ERROR "${variant}: the tables ran out of order\n${stdout}")
    endif()
endforeach()

run_checked(sections "${READELF}" -S -W "${OUTPUT}/O2.o")
foreach(section
        "[.]init_array +INIT_ARRAY .* WA " "[.]init_array[.]00101 +INIT_ARRAY .* WA "
        "[.]init_array[.]00130 +INIT_ARRAY .* WA " "[.]fini_array +FINI_ARRAY .* WA "
        "[.]fini_array[.]00200 +FINI_ARRAY .* WA ")
    if(NOT stdout MATCHES "${section}")
        message(FATAL_ERROR "no section matching '${section}'\n${stdout}")
    endif()
endforeach()
