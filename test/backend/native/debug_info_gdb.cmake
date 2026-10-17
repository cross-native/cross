# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -g on MIPS, end to end: a program runs under QEMU's gdb stub (`-gdb stdio
# -S`, the pipe form of `-s -S`), gdb-multiarch stops at a file:line
# breakpoint, and the backtrace names the callers at their lines, an inlined
# caller among them.
foreach(required CC SOURCE START LINKER TRIPLE ABI QEMU_NAME QEMU_CPU MC_CPU
                 EMULATION OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be set")
    endif()
endforeach()
find_program(GDB NAMES gdb-multiarch)
find_program(QEMU NAMES ${QEMU_NAME})
find_program(LLVM_MC NAMES llvm-mc)
include("${CMAKE_CURRENT_LIST_DIR}/../../support/mips_linker.cmake")
if(NOT GDB OR NOT QEMU OR NOT LLVM_MC OR NOT LLD)
    message(STATUS
        "skipping the gdb test: gdb-multiarch, ${QEMU_NAME}, llvm-mc, and ld.lld are required")
    return()
endif()

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

run("startup" "${LLVM_MC}" --filetype=obj --triple=${TRIPLE} --mcpu=${MC_CPU}
    --mattr=+noabicalls "${START}" -o "${OUTPUT}/start.o")
foreach(level -O0 -O2)
    set(image "${OUTPUT}/program${level}.elf")
    run("cc ${level}" "${CC}" -c -g ${level} -target ${TRIPLE} -mabi=${ABI}
        "${SOURCE}" -o "${OUTPUT}/program${level}.o")
    run("link ${level}" "${LLD}" -m ${EMULATION} -T "${LINKER}"
        "${OUTPUT}/start.o" "${OUTPUT}/program${level}.o" -o "${image}")
    set(commands -ex "set confirm off" -ex "set pagination off"
        -ex "file '${image}'"
        -ex "target remote | exec '${QEMU}' -M malta -cpu ${QEMU_CPU} -m 64M -bios none -kernel '${image}' -display none -serial none -monitor none -no-reboot -S -gdb stdio"
        -ex "break debug_info_gdb.x:5" -ex "continue" -ex "bt")
    if(level STREQUAL "-O0")
        list(APPEND commands -ex "print x")
    endif()
    list(APPEND commands -ex "continue" -ex "continue" -ex "bt")
    execute_process(COMMAND "${GDB}" -batch -nx ${commands} -ex "kill"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr
        TIMEOUT 60)
    set(session "${stdout}\n${stderr}")
    # At -O2 the line's first instruction fills a delay slot, so gdb moves
    # the breakpoint to the branch, and doubled is inlined into entry.
    expect("gdb ${level}" "${session}"
        "Breakpoint 1, add_one [(]x="
        "\n#1 +0x[0-9a-f]+ in twice [(][^\n]*[)] at [^\n]*debug_info_gdb[.]x:10\n#2 +0x[0-9a-f]+ in entry [(][^\n]*[)] at [^\n]*debug_info_gdb[.]x:19\n"
        "\n#1 +0x[0-9a-f]+ in doubled [(][^\n]*[)] at [^\n]*debug_info_gdb[.]x:15\n#2 +(0x[0-9a-f]+ in )?entry [(][^\n]*[)] at [^\n]*debug_info_gdb[.]x:20\n")
    if(level STREQUAL "-O0")
        expect("gdb ${level}" "${session}"
            "Breakpoint 1, add_one [(]x=5[)] at [^\n]*debug_info_gdb[.]x:5\n"
            "\n[$]1 = 5\n")
    else()
        expect("gdb ${level}" "${session}" "\n#2 +entry [(]")
    endif()
endforeach()
