# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Script inputs: INPUT and GROUP with files, -lNAME archives, and
# AS_NEEDED; an implicit linker script on the command line, with EXTERN,
# PROVIDE, and an INPUT found next to it; INSERT AFTER and INSERT BEFORE
# into the default layout; and their diagnostics.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
find_program(LLVM_AR NAMES llvm-ar)
if(NOT LLVM_AR)
    message(STATUS "skipping script input test: llvm-ar is required")
    return()
endif()

function(assemble name text)
    file(WRITE "${OUTPUT}/${name}.s" ".set noreorder\n${text}")
    run_checked("assemble ${name}" "${AS}" -target mips-unknown-elf "${OUTPUT}/${name}.s"
                -o "${OUTPUT}/${name}.o")
endfunction()
file(MAKE_DIRECTORY "${OUTPUT}/scripts" "${OUTPUT}/lib")
assemble(main ".text\n.globl _start\n_start:\njal from_input\nnop\njal from_group\nnop\njal from_library\nnop\nlui $2, %hi(implicit_value)\naddiu $2, $2, %lo(implicit_value)\njal from_implicit\nnop\n")
assemble(input ".text\n.globl from_input\nfrom_input:\njr $31\nnop\n.section .text.kept,\"ax\",@progbits\n.globl kept_by_extern\nkept_by_extern:\njr $31\nnop\n")
assemble(group ".text\n.globl from_group\nfrom_group:\njr $31\nnop\n")
assemble(library ".text\n.globl from_library\nfrom_library:\njr $31\nnop\n")
assemble(needed ".text\n.globl from_needed\nfrom_needed:\njr $31\nnop\n")
assemble(implicit ".text\n.globl from_implicit\nfrom_implicit:\njr $31\nnop\n")
file(RENAME "${OUTPUT}/implicit.o" "${OUTPUT}/scripts/implicit.o")
run_checked("archive group" "${LLVM_AR}" rcs "${OUTPUT}/lib/libgroup.a" "${OUTPUT}/group.o")
run_checked("archive library" "${LLVM_AR}" rcs "${OUTPUT}/lib/liblibrary.a" "${OUTPUT}/library.o")
# The main script names input.o in the current directory and finds the
# archives through -L; the implicit script adds the object next to it and
# keeps a symbol.
file(WRITE "${OUTPUT}/scripts/main.ld"
     "INPUT(input.o -llibrary)\nGROUP(libgroup.a AS_NEEDED(needed.o))\nENTRY(_start)\nSECTIONS {\n"
     "  . = 0x80100000;\n  .text : { *(.text .text.*) }\n"
     "  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }\n}\n")
file(WRITE "${OUTPUT}/scripts/implicit.x" "/* An implicit script. */\nEXTERN(kept_by_extern)\nPROVIDE(implicit_value = 0x1234);\nINPUT(implicit.o)\n")
# The script's files go where the script is, after main.o (GNU ld searches
# an archive where it appears).
execute_process(COMMAND "${LD}" --gc-sections -L lib main.o -T scripts/main.ld scripts/implicit.x -o inputs.elf
                WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "script inputs failed or warned\n${stderr}")
endif()
run_checked("symbols" "${LLVM_NM}" --format=posix "${OUTPUT}/inputs.elf")
expect("symbols" "${run_stdout}" "(^|\n)from_input T " "(^|\n)from_group T " "(^|\n)from_library T "
       "(^|\n)from_implicit T " "(^|\n)kept_by_extern T " "(^|\n)implicit_value A 1234 ")
if(run_stdout MATCHES "from_needed")
    message(FATAL_ERROR "an unreferenced AS_NEEDED file kept its code\n${run_stdout}")
endif()
if(GNU_LD)
    execute_process(COMMAND "${GNU_LD}" --gc-sections -L lib main.o -T scripts/main.ld scripts/implicit.x -o gnu.elf
                    WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GNU ld failed\n${stderr}")
    endif()
    same_as_gnu("inputs" "${OUTPUT}/inputs.elf" "${OUTPUT}/gnu.elf")
endif()
file(WRITE "${OUTPUT}/missing.ld" "INPUT(nothing.o)\n")
run_failing("missing input" "${LD}" -T "${OUTPUT}/missing.ld" "${OUTPUT}/main.o" -o "${OUTPUT}/missing.elf")
expect("missing input" "${run_stderr}" "missing[.]ld:1:7: error: cannot find nothing[.]o")

# INSERT: a script that adds output sections to the default layout.
assemble(insert ".text\n.globl _start\n_start:\nnop\n.section .extra,\"ax\",@progbits\nnop\n.section .early,\"aw\",@progbits\n.word 1\n.data\n.word 2\n")
file(WRITE "${OUTPUT}/insert.ld"
     "SECTIONS {\n  .extra : { *(.extra) }\n}\nINSERT AFTER .text;\nSECTIONS {\n  .early : { *(.early) }\n}\nINSERT BEFORE .data;\n")
run_checked("insert" "${LD}" -T "${OUTPUT}/insert.ld" "${OUTPUT}/insert.o" -o "${OUTPUT}/insert.elf")
run_checked("insert headers" "${LLVM_READELF}" -S -W "${OUTPUT}/insert.elf")
expect("insert headers" "${run_stdout}" "[.]text +PROGBITS[^\n]*\n[^\n]*[.]extra +PROGBITS"
       "[.]early +PROGBITS[^\n]*\n[^\n]*[.]data +PROGBITS")
if(GNU_LD)
    # GNU ld's default layout differs, but the inserted sections sit at the
    # same places in it.
    run_checked("GNU insert" "${GNU_LD}" -T "${OUTPUT}/insert.ld" "${OUTPUT}/insert.o" -o "${OUTPUT}/insert-gnu.elf")
    run_checked("GNU insert headers" "${LLVM_READELF}" -S -W "${OUTPUT}/insert-gnu.elf")
    expect("GNU insert headers" "${run_stdout}" "[.]text +PROGBITS[^\n]*\n[^\n]*[.]extra +PROGBITS"
           "[.]early +PROGBITS[^\n]*\n[^\n]*[.]data +PROGBITS")
endif()
file(WRITE "${OUTPUT}/bad_insert.ld" "SECTIONS {\n  .extra : { *(.extra) }\n}\nINSERT AFTER .nowhere;\n")
run_failing("bad insert" "${LD}" -T "${OUTPUT}/bad_insert.ld" "${OUTPUT}/insert.o" -o "${OUTPUT}/bad.elf")
expect("bad insert" "${run_stderr}" "bad_insert[.]ld:4:1: error: section '[.]nowhere' not found for INSERT")
