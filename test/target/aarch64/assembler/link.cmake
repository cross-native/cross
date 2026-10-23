# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The relocations of SOURCE, a bare-metal program, link as llvm-mc's do:
# as and llvm-mc give the same object, ld.lld and GNU ld (where installed)
# link each into the same image with SCRIPT, and with qemu-system-aarch64
# installed the images that as's object gives run on the virt machine and
# exit through semihosting with status 0.

foreach(required AS SOURCE SCRIPT OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
find_program(PYTHON NAMES python3 python)
if(NOT PYTHON)
    message(STATUS "skipping: python is required")
    return()
endif()
find_program(LLVM_MC NAMES llvm-mc)
if(NOT LLVM_MC)
    message(STATUS "skipping: llvm-mc is required")
    return()
endif()

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE out
                    ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${out}${err}")
    endif()
endfunction()

function(compare label first second)
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/compare.py" "${first}"
                            "${second}"
                    RESULT_VARIABLE status OUTPUT_VARIABLE differences)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} differ:\n${differences}")
    endif()
endfunction()

# The objects have one name, which GNU ld writes into the image.
file(MAKE_DIRECTORY "${OUTPUT}/cross" "${OUTPUT}/llvm-mc")
run_checked("as" "${AS}" -target aarch64-none-elf "${SOURCE}" -o "${OUTPUT}/cross/program.o")
run_checked("llvm-mc" "${LLVM_MC}" --triple=aarch64 --filetype=obj "${SOURCE}"
            -o "${OUTPUT}/llvm-mc/program.o")
compare("the objects of as and llvm-mc" "${OUTPUT}/cross/program.o"
        "${OUTPUT}/llvm-mc/program.o")

find_program(LLD NAMES ld.lld)
find_program(GNU_LD NAMES aarch64-linux-gnu-ld aarch64-none-elf-ld)
find_program(QEMU NAMES qemu-system-aarch64)
set(images "")
foreach(linker LLD GNU_LD)
    if(NOT ${linker})
        message(STATUS "${linker} not found")
        continue()
    endif()
    foreach(assembler cross llvm-mc)
        run_checked("${linker} of the ${assembler} object" "${${linker}}" -T "${SCRIPT}"
                    "${OUTPUT}/${assembler}/program.o" -o "${OUTPUT}/${assembler}/${linker}.elf")
    endforeach()
    compare("the ${linker} images" "${OUTPUT}/cross/${linker}.elf"
            "${OUTPUT}/llvm-mc/${linker}.elf")
    list(APPEND images "${OUTPUT}/cross/${linker}.elf")
endforeach()
if(NOT QEMU)
    message(STATUS "qemu-system-aarch64 not found; the images were not run")
    return()
endif()
foreach(image IN LISTS images)
    execute_process(COMMAND "${QEMU}" -M virt -cpu cortex-a57 -nographic -semihosting
                            -kernel "${image}"
                    TIMEOUT 60 RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "${image} failed check ${status}\n${out}${err}")
    endif()
endforeach()
list(LENGTH images count)
message(STATUS "${count} images ran")
