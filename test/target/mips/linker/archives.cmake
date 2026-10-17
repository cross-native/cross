# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Archives: members are linked when they define a symbol a strong reference
# needs, in any command-line order; weak references and unused members pull
# nothing; -L/-l and -l:NAME search; diagnostics name archive members.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
find_program(LLVM_AR NAMES llvm-ar)
if(NOT LLVM_AR)
    message(STATUS "skipping archive test: llvm-ar is required")
    return()
endif()

function(assemble name text)
    file(WRITE "${OUTPUT}/${name}.s" ".set noreorder\n${text}")
    run_checked("assemble ${name}" "${AS}" -target mips-unknown-elf "${OUTPUT}/${name}.s"
                -o "${OUTPUT}/${name}.o")
endfunction()
assemble(main ".section .text.boot,\"ax\",@progbits\n.globl _start\n.weak gamma\n_start:\njal alpha\nnop\nlui $2, %hi(gamma)\naddiu $2, $2, %lo(gamma)\n")
assemble(alpha ".text\n.globl alpha\nalpha:\nj beta\nnop\n")
assemble(beta ".text\n.globl beta\nbeta:\njr $31\nnop\n")
assemble(gamma ".text\n.globl gamma\ngamma:\njr $31\nnop\n")
assemble(delta ".text\n.globl delta\ndelta:\nj missing\nnop\n")
run_checked("archive" "${LLVM_AR}" rcs "${OUTPUT}/libparts.a" "${OUTPUT}/alpha.o" "${OUTPUT}/beta.o"
            "${OUTPUT}/gamma.o" "${OUTPUT}/delta.o")
set(script "${OUTPUT}/archive.ld")
file(WRITE "${script}" "ENTRY(_start)\nSECTIONS {\n  . = 0x80100000;\n  .text : { *(.text.boot) *(.text) }\n  /DISCARD/ : { *(*) }\n}\n")

foreach(order before after named)
    if(order STREQUAL "before")
        set(inputs -L "${OUTPUT}" -lparts "${OUTPUT}/main.o")
    elseif(order STREQUAL "after")
        set(inputs "${OUTPUT}/main.o" -L "${OUTPUT}" -lparts)
    else()
        set(inputs "${OUTPUT}/main.o" -L "${OUTPUT}" -l:libparts.a)
    endif()
    run_checked("${order}" "${LD}" -T "${script}" ${inputs} -o "${OUTPUT}/${order}.elf")
    run_checked("symbols" "${LLVM_NM}" --format=posix "${OUTPUT}/${order}.elf")
    expect("${order}" "${run_stdout}" "(^|\n)_start T 80100000" "(^|\n)alpha T 80100010"
           "(^|\n)beta T 80100020")
    # Neither the unneeded member nor the weak reference (as in GNU ld)
    # reaches the symbol table.
    if(run_stdout MATCHES "(^|\n)(delta|gamma) ")
        message(FATAL_ERROR "${order}: unexpected symbols\n${run_stdout}")
    endif()
    # The weak reference resolves to zero.
    section_words("${OUTPUT}/${order}.elf" .text big words)
    list(GET words 2 high)
    list(GET words 3 low)
    if(NOT high STREQUAL "3c020000" OR NOT low STREQUAL "24420000")
        message(FATAL_ERROR "${order}: the weak reference is ${high} ${low}")
    endif()
endforeach()
if(GNU_LD)
    run_checked("GNU ld" "${GNU_LD}" -T "${script}" "${OUTPUT}/main.o" -L "${OUTPUT}" -lparts
                -o "${OUTPUT}/gnu.elf")
    section_hex("${OUTPUT}/gnu.elf" .text gnu)
    section_hex("${OUTPUT}/after.elf" .text cross)
    if(NOT gnu STREQUAL cross)
        message(FATAL_ERROR "GNU ld linked ${gnu}, Cross ld ${cross}")
    endif()
endif()

# -u pulls a member; its undefined reference names the member.
run_failing("member reference" "${LD}" -T "${script}" -u delta "${OUTPUT}/main.o" -L "${OUTPUT}"
            -lparts -o "${OUTPUT}/delta.elf")
expect("member reference" "${run_stderr}"
       "libparts[.]a[(]delta[.]o[)]:[(][.]text[+]0x0[)]: undefined reference to 'missing'")
run_failing("missing library" "${LD}" -T "${script}" "${OUTPUT}/main.o" -lmissing -o "${OUTPUT}/x.elf")
expect("missing library" "${run_stderr}" "ld: error: cannot find -lmissing")
run_checked("index-less archive" "${LLVM_AR}" rcS "${OUTPUT}/libplain.a" "${OUTPUT}/alpha.o")
run_failing("index-less archive" "${LD}" -T "${script}" "${OUTPUT}/main.o" "${OUTPUT}/libplain.a"
            -o "${OUTPUT}/x.elf")
expect("index-less archive" "${run_stderr}" "libplain[.]a: archive has no symbol index")
