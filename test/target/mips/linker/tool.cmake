# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The ld tool: help and version, the default output and layout, -e, -Ttext,
# --defsym, -Map and -M, --gc-sections with --print-gc-sections, -s,
# --fatal-warnings, and diagnostics that name files, sections, and symbols:
# undefined and duplicate symbols, relocation overflow, unpaired %hi,
# unsupported relocations, references into discarded sections, and objects
# whose ABI, floating-point ABI, class, or byte order disagree.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()

function(assemble name options text)
    separate_arguments(options UNIX_COMMAND "${options}")
    file(WRITE "${OUTPUT}/${name}.s" ".set noreorder\n${text}")
    run_checked("assemble ${name}" "${AS}" ${options} "${OUTPUT}/${name}.s" -o "${OUTPUT}/${name}.o")
endfunction()

run_checked("help" "${LD}" --help)
expect("help" "${run_stdout}" "Usage: ld [[]options[]] file" "--just-symbols=FILE" "--gc-sections")
run_checked("version" "${LD}" --version)
expect("version" "${run_stdout}" "Cross")
run_failing("no inputs" "${LD}")
expect("no inputs" "${run_stderr}" "^ld: error: no input files")
run_failing("unknown option" "${LD}" --bogus)
expect("unknown option" "${run_stderr}" "ld: error: unknown option '--bogus'")
run_failing("missing file" "${LD}" "${OUTPUT}/missing.o")
expect("missing file" "${run_stderr}" "ld: error: cannot open '[^']*missing[.]o'")

assemble(main "-target mips-unknown-elf"
         ".text\n.globl _start\n_start:\njal helper\nnop\n.section .text.unused,\"ax\",@progbits\n.globl unused\nunused:\njr $31\nnop\n.section .text.helper,\"ax\",@progbits\n.globl helper\nhelper:\nlui $2, %hi(value)\njr $31\nlw $2, %lo(value)($2)\n.data\n.globl value\nvalue:\n.word 7\n")

# Without a script: GNU ld's default addresses, a.out in the working directory.
execute_process(COMMAND "${LD}" main.o WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status
                ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT EXISTS "${OUTPUT}/a.out")
    message(FATAL_ERROR "default link failed\n${stderr}")
endif()
run_checked("default layout" "${LLVM_READELF}" -h -l -S "${OUTPUT}/a.out")
expect("default layout" "${run_stdout}" "Entry point address: +0x400030"
       "ABIFLAGS +0x001000 0x00400000 " "REGINFO +0x001018 0x00400018 "
       "LOAD +0x001000 0x00400000 0x00400000 0x0004c 0x0004c R E"
       "LOAD +0x001050 0x00401050 0x00401050 0x00004 0x00004 RW "
       "[.]text +PROGBITS +00400030 ")

run_checked("-Ttext" "${LD}" -Ttext=0x80010000 -e helper --defsym extra=0x1234 "${OUTPUT}/main.o"
            -Map "${OUTPUT}/main.map" -o "${OUTPUT}/ttext.elf")
run_checked("-Ttext headers" "${LLVM_READELF}" -h -S "${OUTPUT}/ttext.elf")
expect("-Ttext" "${run_stdout}" "[.]text +PROGBITS +80010000 " "Entry point address: +0x80010010")
symbol_value("${OUTPUT}/ttext.elf" extra extra)
if(NOT extra STREQUAL "1234")
    message(FATAL_ERROR "--defsym gave ${extra}")
endif()
file(READ "${OUTPUT}/main.map" map)
expect("map" "${map}" "\n[.]text +0x0+80010000 +0x1c\n"
       "\n [.]text +0x0+80010000 +0x8 [^\n]*main[.]o\n"
       "\n [.]text[.]helper +0x0+80010010 +0xc [^\n]*main[.]o\n"
       "\n +0x0+80010010 +helper\n" "\n +0x0+00001234 +extra\n")
run_checked("-M" "${LD}" -Ttext=0x80010000 -M "${OUTPUT}/main.o" -o "${OUTPUT}/m.elf")
expect("-M" "${run_stdout}" "Linker script and memory map")

run_checked("gc" "${LD}" -Ttext=0x80010000 --gc-sections --print-gc-sections "${OUTPUT}/main.o"
            -o "${OUTPUT}/gc.elf")
expect("gc" "${run_stderr}" "removing unused section '[.]text[.]unused' in file '[^']*main[.]o'")
run_checked("gc symbols" "${LLVM_NM}" "${OUTPUT}/gc.elf")
if(run_stdout MATCHES " unused\n" OR NOT run_stdout MATCHES " helper\n")
    message(FATAL_ERROR "--gc-sections kept the wrong sections\n${run_stdout}")
endif()
run_checked("strip" "${LD}" -s -Ttext=0x80010000 "${OUTPUT}/main.o" -o "${OUTPUT}/strip.elf")
run_checked("strip sections" "${LLVM_READELF}" -S "${OUTPUT}/strip.elf")
if(run_stdout MATCHES "[.]symtab")
    message(FATAL_ERROR "-s kept the symbol table")
endif()
run_failing("fatal warnings" "${LD}" --fatal-warnings -e nowhere "${OUTPUT}/main.o" -o "${OUTPUT}/w.elf")
expect("fatal warnings" "${run_stderr}" "ld: warning: cannot find entry symbol nowhere; defaulting to 0x")

# link_error(LABEL PATTERN ARGUMENTS...): the link fails with PATTERN.
function(link_error label pattern)
    run_failing("${label}" "${LD}" ${ARGN} -o "${OUTPUT}/${label}.elf")
    expect("${label}" "${run_stderr}" "${pattern}")
endfunction()
assemble(caller "-target mips-unknown-elf" ".text\n.globl caller\ncaller:\njal absent\nnop\n")
link_error(undefined "caller[.]o:[(][.]text[+]0x0[)]: undefined reference to 'absent'" "${OUTPUT}/caller.o")
assemble(other "-target mips-unknown-elf" ".data\n.globl value\nvalue:\n.word 8\n")
link_error(duplicate "other[.]o:[(][.]data[+]0x0[)]: multiple definition of 'value'; first defined in [^\n]*main[.]o:[(][.]data[+]0x0[)]"
           "${OUTPUT}/main.o" "${OUTPUT}/other.o")
assemble(gprel "-target mips-unknown-elf" ".text\n.globl _start\n_start:\nlw $2, %gp_rel(far)($28)\n.data\n.space 0x10000\n.globl far\nfar:\n.word 1\n")
file(WRITE "${OUTPUT}/gp.ld" "SECTIONS {\n  .text 0x80010000 : { *(.text) }\n  _gp = 0x80008000;\n  .data : { *(.data) }\n}\n")
link_error(gprel "gprel[.]o:[(][.]text[+]0x0[)]: R_MIPS_GPREL16 against 'far' out of range: [0-9]+ is not in [[]-32768, 32767[]] of _gp"
           -T "${OUTPUT}/gp.ld" "${OUTPUT}/gprel.o")
file(WRITE "${OUTPUT}/plain.ld" "SECTIONS {\n  .text 0x80010000 : { *(.text) }\n  .data : { *(.data) }\n}\n")
link_error(no-gp "R_MIPS_GPREL16 against 'far' needs _gp, which nothing defines" -T "${OUTPUT}/plain.ld"
           "${OUTPUT}/gprel.o")
assemble(branch "-target mips-unknown-elf" ".text\n.globl _start\n_start:\nb away\nnop\n")
link_error(branch "branch[.]o:[(][.]text[+]0x0[)]: R_MIPS_PC16 against 'away' out of range"
           --defsym away=0x90000000 -Ttext=0x80010000 "${OUTPUT}/branch.o")
assemble(unpaired "-target mips-unknown-elf" ".text\n.globl _start\n_start:\nlui $2, %hi(value)\n")
run_checked("unpaired" "${LD}" -Ttext=0x80010000 "${OUTPUT}/unpaired.o" "${OUTPUT}/other.o" -o "${OUTPUT}/unpaired.elf")
expect("unpaired" "${run_stderr}" "warning: [^\n]*unpaired[.]o:[(][.]text[+]0x0[)]: can't find matching R_MIPS_LO16 relocation for R_MIPS_HI16 against 'value'")
assemble(discarded "-target mips-unknown-elf" ".text\n.globl _start\n_start:\njal gone\nnop\n.section .text.gone,\"ax\",@progbits\ngone:\njr $31\nnop\n")
file(WRITE "${OUTPUT}/discard.ld" "SECTIONS {\n  .text : { *(.text) }\n  /DISCARD/ : { *(.text.gone) *(.reginfo) *(.MIPS.abiflags) }\n}\n")
link_error(discarded "discarded[.]o:[(][.]text[+]0x0[)]: R_MIPS_26 refers to '[.]text[.]gone', which is in a discarded section"
           -T "${OUTPUT}/discard.ld" "${OUTPUT}/discarded.o")
assemble(eabi "-target mips-unknown-elf -mabi=eabi" ".text\n.globl e\ne:\njr $31\nnop\n")
link_error(abi "eabi[.]o: ABI 'eabi32' is incompatible with ABI 'o32' of [^\n]*main[.]o" "${OUTPUT}/main.o" "${OUTPUT}/eabi.o")
assemble(soft "-target mips-unknown-elf -msoft-float" ".text\n.globl s\ns:\njr $31\nnop\n")
run_checked("fp" "${LD}" -Ttext=0x80010000 "${OUTPUT}/main.o" "${OUTPUT}/soft.o" -o "${OUTPUT}/fp.elf")
expect("fp" "${run_stderr}" "ld: warning: [^\n]*soft[.]o: floating-point ABI '-msoft-float' is incompatible with '-mdouble-float' of [^\n]*main[.]o")
assemble(wide "-target mips64-unknown-elf -mabi=64" ".text\n.globl w\nw:\njr $31\nnop\n")
link_error(class "wide[.]o: ELF64 big-endian machine 8 is incompatible with the ELF32 big-endian machine 8 output"
           "${OUTPUT}/main.o" "${OUTPUT}/wide.o")
assemble(little "-target mipsel-unknown-elf" ".text\n.globl l\nl:\njr $31\nnop\n")
link_error(order "little[.]o: ELF32 little-endian machine 8 is incompatible with the ELF32 big-endian machine 8 output"
           "${OUTPUT}/main.o" "${OUTPUT}/little.o")
link_error(emulation "ld: error: unsupported emulation 'elf_i386'" -m elf_i386 "${OUTPUT}/main.o")
find_program(LLVM_MC NAMES llvm-mc)
if(LLVM_MC)
    file(WRITE "${OUTPUT}/pic.s" ".text\n.globl _start\n_start:\nlw $2, %got(value)($28)\n")
    run_checked("PIC object" "${LLVM_MC}" --filetype=obj --triple=mips-unknown-elf "${OUTPUT}/pic.s"
                -o "${OUTPUT}/pic.o")
    link_error(pic "pic[.]o:[(][.]text[+]0x0[)]: unsupported R_MIPS_GOT16 against 'value' [(]PIC code is not supported[)]"
               "${OUTPUT}/pic.o" "${OUTPUT}/other.o")
endif()
