# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# MEMORY regions: sections without >REGION go to the first region whose
# attributes they match (GNU ld's section flags, so a 'w' region also takes
# read-only data), REGION_ALIAS, an AT> region without >REGION as the run
# region too, orphans in the region of the section they follow, and the
# diagnostics for a loadable section no region takes, an undeclared region,
# and an overflow.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
file(WRITE "${OUTPUT}/regions.s"
     ".set noreorder\n.text\n.globl _start\n_start: jr $31\nnop\n.section .rodata,\"a\"\n.word 1\n"
     ".data\n.word 2\n.section .orphan,\"aw\",@progbits\n.word 3\n.bss\n.space 8\n")
run_checked("assemble" "${AS}" -target mips-unknown-elf "${OUTPUT}/regions.s" -o "${OUTPUT}/regions.o")
set(discard "/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }")

# link_script(LABEL TEXT): links regions.o with the script TEXT, then
# compares the image with GNU ld's when it is installed; the headers are in
# run_stdout.
function(link_script label text)
    file(WRITE "${OUTPUT}/${label}.ld" "${text}")
    run_checked("${label}" "${LD}" -T "${OUTPUT}/${label}.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/${label}.elf")
    if(GNU_LD)
        run_checked("${label} GNU ld" "${GNU_LD}" -T "${OUTPUT}/${label}.ld" "${OUTPUT}/regions.o"
                    -o "${OUTPUT}/${label}-gnu.elf")
        same_as_gnu("${label}" "${OUTPUT}/${label}.elf" "${OUTPUT}/${label}-gnu.elf")
    endif()
    run_checked("${label} headers" "${LLVM_READELF}" -S -l -W "${OUTPUT}/${label}.elf")
    set(run_stdout "${run_stdout}" PARENT_SCOPE)
endfunction()

# Code and read-only data in rom, data and the zero-filled section in ram;
# .data loads in rom, and the orphan follows .data into ram and rom; .bss
# loads where it runs, since no section of ram keeps its load address.
link_script(attributes "MEMORY {\n  rom (rx) : ORIGIN = 0x80100000, LENGTH = 0x1000\n  ram (w!x) : ORIGIN = 0x80200000, LENGTH = 0x1000\n}\nREGION_ALIAS(\"REGION_DATA\", ram)\nSECTIONS {\n  .text : { *(.text) }\n  .rodata : { *(.rodata) }\n  .data : { *(.data) } > REGION_DATA AT > rom\n  .bss : { *(.bss) }\n  ${discard}\n}\n")
expect("attributes" "${run_stdout}" "[.]text +PROGBITS +80100000 " "[.]rodata +PROGBITS +80100008 "
       "[.]data +PROGBITS +80200000 " "[.]orphan +PROGBITS +80200004 " "[.]bss +NOBITS +80200008 "
       "LOAD +0x[0-9a-f]+ 0x80200000 0x8010000c 0x00008 0x00008 RW "
       "LOAD +0x[0-9a-f]+ 0x80200008 0x80200008 0x00000 0x00008 RW ")

# A 'w' region listed first also takes .rodata, which GNU ld counts as data.
link_script(writable_first "MEMORY {\n  ram (w) : ORIGIN = 0x80200000, LENGTH = 0x1000\n  rom (rx) : ORIGIN = 0x80100000, LENGTH = 0x1000\n}\nSECTIONS {\n  .text : { *(.text) }\n  .rodata : { *(.rodata) }\n  .data : { *(.data) }\n  .bss : { *(.bss) }\n  ${discard}\n}\n")
expect("writable_first" "${run_stdout}" "[.]text +PROGBITS +80100000 " "[.]rodata +PROGBITS +80200000 "
       "[.]data +PROGBITS +80200004 ")

# An AT> region without >REGION or an address is the run region too.
link_script(load_only "MEMORY {\n  rom : ORIGIN = 0x80100000, LENGTH = 0x1000\n  ram : ORIGIN = 0x80200000, LENGTH = 0x1000\n}\nSECTIONS {\n  .text : { *(.text) } > ram\n  .data : { *(.data) *(.orphan) *(.rodata) } AT > rom\n  .bss : { *(.bss) } > ram\n  ${discard}\n}\n")
expect("load_only" "${run_stdout}" "[.]text +PROGBITS +80200000 " "[.]data +PROGBITS +80100000 ")

# A loadable section that no region takes is an error.
file(WRITE "${OUTPUT}/no_region.ld" "MEMORY {\n  rom (rx) : ORIGIN = 0x80100000, LENGTH = 0x1000\n}\nSECTIONS {\n  .text : { *(.text) }\n  .data : { *(.data) *(.rodata) *(.orphan) *(.bss) }\n  ${discard}\n}\n")
run_failing("no region" "${LD}" -T "${OUTPUT}/no_region.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/no_region.elf")
expect("no region" "${run_stderr}" "no_region[.]ld:6:3: error: no memory region specified for loadable section '[.]data'")

# An undeclared region starts at zero, with a warning.
file(WRITE "${OUTPUT}/undeclared.ld" "SECTIONS {\n  .text : { *(.text) } > nowhere\n  .data : { *(.data) *(.rodata) *(.orphan) *(.bss) } > nowhere\n  ${discard}\n}\n")
run_checked("undeclared" "${LD}" -T "${OUTPUT}/undeclared.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/undeclared.elf")
expect("undeclared" "${run_stderr}" "undeclared[.]ld:2:3: warning: memory region 'nowhere' not declared")
run_checked("undeclared headers" "${LLVM_READELF}" -S -W "${OUTPUT}/undeclared.elf")
expect("undeclared headers" "${run_stdout}" "[.]text +PROGBITS +00000000 " "[.]data +PROGBITS +00000008 ")

# An overflowing region.
file(WRITE "${OUTPUT}/overflow.ld" "MEMORY {\n  rom (rx) : ORIGIN = 0x80100000, LENGTH = 4\n  ram (w) : ORIGIN = 0x80200000, LENGTH = 0x1000\n}\nSECTIONS {\n  .text : { *(.text) }\n  ${discard}\n}\n")
run_failing("overflow" "${LD}" -T "${OUTPUT}/overflow.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/overflow.elf")
expect("overflow" "${run_stderr}" "overflow[.]ld:6:3: error: section '[.]text' will not fit in region 'rom'")

# Diagnosed alias mistakes.
file(WRITE "${OUTPUT}/alias.ld" "MEMORY {\n  rom : ORIGIN = 0, LENGTH = 4\n}\nREGION_ALIAS(\"r\", nowhere)\n")
run_failing("alias" "${LD}" -T "${OUTPUT}/alias.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/alias.elf")
expect("alias" "${run_stderr}" "alias[.]ld:4:1: error: memory region 'nowhere' for alias 'r' does not exist")
file(WRITE "${OUTPUT}/attribute.ld" "MEMORY {\n  rom (rq) : ORIGIN = 0, LENGTH = 4\n}\n")
run_failing("attribute" "${LD}" -T "${OUTPUT}/attribute.ld" "${OUTPUT}/regions.o" -o "${OUTPUT}/attribute.elf")
expect("attribute" "${run_stderr}" "attribute[.]ld:2:9: error: invalid character 'q' in memory region attributes")
