# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs the separately compiled preserved_callee.x and preserved_caller.x at
# LEVEL with shrink-wrapping enabled and disabled, with and without a frame
# pointer, with unwind tables, and with the caller at -O0. At -O2 it also
# checks that DWARF CFI and Win64 unwind codes describe the preserved saves.
foreach(required CC HOST_CXX LEVEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
set(here "${CMAKE_CURRENT_LIST_DIR}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
endfunction()

run_checked("driver compilation" "${HOST_CXX}" -O1 -std=c++20 -c
    "${here}/preserved_driver.cpp" -o "${OUTPUT}-driver.o")
run_checked("harness assembly" "${HOST_CXX}" -c "${here}/preserved_harness.s"
    -o "${OUTPUT}-harness.o")

# Each entry: name|callee flags|caller flags, flags separated by commas.
set(builds
    "sw|-${LEVEL},-fshrink-wrap|-${LEVEL},-fshrink-wrap"
    "nosw|-${LEVEL},-fno-shrink-wrap|-${LEVEL},-fno-shrink-wrap"
    "fp|-${LEVEL},-fno-omit-frame-pointer|-${LEVEL},-fno-omit-frame-pointer"
    "unwind|-${LEVEL},-funwind-tables|-${LEVEL},-funwind-tables"
    "mixed|-${LEVEL}|-O0")
foreach(build IN LISTS builds)
    string(REPLACE "|" ";" parts "${build}")
    list(GET parts 0 name)
    list(GET parts 1 callee_flags)
    list(GET parts 2 caller_flags)
    string(REPLACE "," ";" callee_flags "${callee_flags}")
    string(REPLACE "," ";" caller_flags "${caller_flags}")
    set(run "${OUTPUT}-${name}")
    run_checked("callee compilation (${name})" "${CC}" ${callee_flags}
        -c "${here}/preserved_callee.x" -o "${run}-callee.o")
    run_checked("caller compilation (${name})" "${CC}" ${caller_flags}
        -c "${here}/preserved_caller.x" -o "${run}-caller.o")
    run_checked("link (${name})" "${HOST_CXX}" "${OUTPUT}-driver.o"
        "${OUTPUT}-harness.o" "${run}-callee.o" "${run}-caller.o"
        -o "${run}.exe")
    run_checked("preserved-register run (${name})" "${run}.exe")
endforeach()

if(NOT LEVEL STREQUAL "O2")
    return()
endif()

# Extracts one function's assembly up to its ELF .size or COFF .seh_endproc.
function(function_body text name variable)
    string(FIND "${text}" "${name}:" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "no assembly for '${name}'\n${text}")
    endif()
    string(SUBSTRING "${text}" ${begin} -1 tail)
    string(FIND "${tail}" ".size ${name}," end)
    if(end EQUAL -1)
        string(FIND "${tail}" ".seh_endproc" end)
    endif()
    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

foreach(target x86_64-unknown-linux-gnu x86_64-w64-windows-gnu)
    set(assembly "${OUTPUT}-${target}.s")
    run_checked("unwind assembly (${target})" "${CC}" -S -O2 -funwind-tables
        -target ${target} "${here}/preserved_callee.x" -o "${assembly}")
    file(READ "${assembly}" text)
    function_body("${text}" cross_lanes lanes)
    foreach(register rbx r12 r13 r14 r15)
        if(target MATCHES "windows")
            set(pattern "[.]seh_(savereg|pushreg) %${register}[,\n]")
        else()
            set(pattern "[.]cfi_offset %${register},")
        endif()
        if(NOT lanes MATCHES "${pattern}")
            message(FATAL_ERROR
                "${target}: the save of ${register} is not described\n${lanes}")
        endif()
    endforeach()
endforeach()
