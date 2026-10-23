# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Helpers shared by the AArch64 linker tests: AS and LD name Cross as and
# ld, OUTPUT the test's directory.

foreach(required AS LD OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(PYTHON NAMES python3 python)
find_program(LLVM_MC NAMES llvm-mc)
find_program(LLVM_READELF NAMES llvm-readelf)
if(NOT PYTHON OR NOT LLVM_MC OR NOT LLVM_READELF)
    message(STATUS "skipping: python, llvm-mc, and llvm-readelf are required")
    set(LINKER_TEST_SKIP TRUE)
    return()
endif()
find_program(LLD NAMES ld.lld)
find_program(GNU_AS NAMES aarch64-linux-gnu-as aarch64-none-elf-as)
find_program(GNU_LD NAMES aarch64-linux-gnu-ld aarch64-none-elf-ld)
find_program(QEMU NAMES qemu-system-aarch64)
set(sources "${CMAKE_CURRENT_LIST_DIR}")
set(images "${CMAKE_CURRENT_LIST_DIR}/images.py")
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    set(run_stdout "${stdout}" PARENT_SCOPE)
    set(run_stderr "${stderr}" PARENT_SCOPE)
endfunction()

# run_failing(LABEL COMMAND...): the command fails; its standard error is
# returned in run_stderr.
function(run_failing label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "${label} succeeded unexpectedly\n${stdout}\n${stderr}")
    endif()
    set(run_stderr "${stderr}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# assemble(ASSEMBLER SOURCE OBJECT): ASSEMBLER is cross, mc, or gnu.
function(assemble assembler source object)
    if(assembler STREQUAL "cross")
        run_checked("as ${source}" "${AS}" -target aarch64-none-elf "${source}" -o "${object}")
    elseif(assembler STREQUAL "mc")
        run_checked("llvm-mc ${source}" "${LLVM_MC}" --triple=aarch64 --filetype=obj "${source}" -o "${object}")
    else()
        run_checked("GNU as ${source}" "${GNU_AS}" "${source}" -o "${object}")
    endif()
endfunction()

# same_images(LABEL A B [SECTION...]): the allocated sections, or the named
# ones, agree in names, types, flags, addresses, sizes, and contents.
function(same_images label first second)
    execute_process(COMMAND "${PYTHON}" "${images}" compare "${first}" "${second}" ${ARGN}
                    RESULT_VARIABLE status OUTPUT_VARIABLE differences ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} differ:\n${differences}${errors}")
    endif()
endfunction()

# qemu_exits_zero(LABEL ARGUMENT...): QEMU's virt machine with a Cortex-A57
# and semihosting runs the image and exits with status 0.
function(qemu_exits_zero label)
    execute_process(COMMAND "${QEMU}" -M virt -cpu cortex-a57 -nographic -semihosting ${ARGN}
                    TIMEOUT 60 RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${label} failed check ${status}\n${out}${err}")
    endif()
endfunction()
