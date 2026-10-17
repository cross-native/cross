# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fill patterns (a long hexadecimal =FILL and FILL, each restarting in every
# gap), SUBALIGN, ONLY_IF_RO and ONLY_IF_RW, the READONLY, INFO, and TYPE
# section types, numbers assigned inside output sections as offsets, the
# sections of symbols assigned from `.` outside them, and DATA_SEGMENT_ALIGN
# with DATA_SEGMENT_RELRO_END and DATA_SEGMENT_END.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
run_checked("assemble" "${AS}" -target mips-unknown-elf "${sources}/fills.s" -o "${OUTPUT}/fills.o")
run_checked("link" "${LD}" -T "${sources}/fills.ld" "${OUTPUT}/fills.o" -o "${OUTPUT}/fills.elf")
# The `. = . + 3` gap repeats the long pattern from its start; FILL then
# pads the alignment gap before .text.b.
section_hex("${OUTPUT}/fills.elf" .text text)
if(NOT text STREQUAL "03e0000800000000010203aabbccddaa03e00008000000000000000000000000")
    message(FATAL_ERROR ".text is ${text}")
endif()
run_checked("headers" "${LLVM_READELF}" -S -W "${OUTPUT}/fills.elf")
expect("headers" "${run_stdout}"
       "[.]sub +PROGBITS +80100020 [0-9a-f]+ 000024 00 +A +0 +0 +32\n"
       "[.]stuff +PROGBITS +80100044 [0-9a-f]+ 000004 00 +WA "
       "[.]ro +PROGBITS +80100048 [0-9a-f]+ 000004 00 +A "
       "[.]info +PROGBITS +8010004c [0-9a-f]+ 000004 00 +0 "
       "[.]typed +PROGBITS +8010004c [0-9a-f]+ 000001 00 +A ")
section_hex("${OUTPUT}/fills.elf" .sub sub)
if(NOT sub STREQUAL "000000040000000000000000000000000000000000000000000000000000000000000040")
    message(FATAL_ERROR ".sub is ${sub}")
endif()
run_checked("symbols" "${LLVM_READELF}" -s -W "${OUTPUT}/fills.elf")
# A number inside an output section is an offset from its start; a symbol
# from `.` outside output sections belongs to the previous section, or after
# an assignment to `.` to the next one, but past `_end` to the previous.
expect("symbols" "${run_stdout}" "80100010 +0 NOTYPE +GLOBAL DEFAULT +1 text_offset"
       "80100020 +0 NOTYPE +GLOBAL DEFAULT +1 top_of_text"
       "80100050 +0 NOTYPE +GLOBAL DEFAULT +7 after_align"
       "80100058 +0 NOTYPE +GLOBAL DEFAULT +7 b_size"
       "80100064 +0 NOTYPE +GLOBAL DEFAULT +8 past_end")
if(GNU_LD)
    run_checked("GNU ld" "${GNU_LD}" -T "${sources}/fills.ld" "${OUTPUT}/fills.o" -o "${OUTPUT}/fills-gnu.elf")
    same_as_gnu("fills" "${OUTPUT}/fills.elf" "${OUTPUT}/fills-gnu.elf")
endif()

# DATA_SEGMENT_ALIGN saves a common page when the data's partial first and
# last pages fit in one; DATA_SEGMENT_RELRO_END without -z relro is its
# second operand.
foreach(gap 0x100 0xf00)
    file(WRITE "${OUTPUT}/segment-${gap}.ld"
         "SECTIONS {\n  . = 0x80100000;\n  .text : { *(.text) . = . + ${gap}; }\n"
         "  . = DATA_SEGMENT_ALIGN(0x10000, 0x1000);\n  .data : { *(.data) . = . + 0x200; }\n"
         "  . = DATA_SEGMENT_RELRO_END(0, .);\n  .bss : { *(.bss) }\n  . = DATA_SEGMENT_END(.);\n"
         "  /DISCARD/ : { *(.text.b) *(.a4) *(.a64) *(.stuff) *(.ro) *(.info) *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) }\n}\n")
    run_checked("segment ${gap}" "${LD}" -z max-page-size=0x10000 -T "${OUTPUT}/segment-${gap}.ld"
                "${OUTPUT}/fills.o" -o "${OUTPUT}/segment-${gap}.elf")
    if(GNU_LD)
        run_checked("segment ${gap} GNU ld" "${GNU_LD}" -z max-page-size=0x10000 -z common-page-size=0x1000
                    -T "${OUTPUT}/segment-${gap}.ld" "${OUTPUT}/fills.o" -o "${OUTPUT}/segment-${gap}-gnu.elf")
        same_as_gnu("segment ${gap}" "${OUTPUT}/segment-${gap}.elf" "${OUTPUT}/segment-${gap}-gnu.elf")
    endif()
endforeach()
run_checked("segment headers" "${LLVM_READELF}" -S -W "${OUTPUT}/segment-0x100.elf" "${OUTPUT}/segment-0xf00.elf")
expect("segment headers" "${run_stdout}" "[.]data +PROGBITS +80110108 .*[.]data +PROGBITS +80111000 ")

# Diagnostics.
file(WRITE "${OUTPUT}/segment_inside.ld" "SECTIONS {\n  .text : { . = DATA_SEGMENT_ALIGN(0x1000, 0x1000); *(.text) }\n}\n")
run_failing("segment inside" "${LD}" -T "${OUTPUT}/segment_inside.ld" "${OUTPUT}/fills.o" -o "${OUTPUT}/x.elf")
expect("segment inside" "${run_stderr}" "segment_inside[.]ld:2:17: error: DATA_SEGMENT_ALIGN is only allowed outside output sections")
file(WRITE "${OUTPUT}/bad_type.ld" "SECTIONS {\n  .text (TYPE = SHT_BOGUS) : { *(.text) }\n}\n")
run_failing("bad type" "${LD}" -T "${OUTPUT}/bad_type.ld" "${OUTPUT}/fills.o" -o "${OUTPUT}/x.elf")
expect("bad type" "${run_stderr}" "bad_type[.]ld:2:17: error: invalid type for output section '[.]text'")
