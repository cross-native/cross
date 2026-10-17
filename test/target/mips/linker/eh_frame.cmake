# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# .eh_frame editing: identical CIEs of different objects share one copy;
# --gc-sections removes the FDEs of collected functions and keeps the LSDAs
# and personality routines of the FDEs that stay, and an emptied .eh_frame
# stays; --eh-frame-hdr writes the .eh_frame_hdr search table and a
# PT_GNU_EH_FRAME header; trailing DW_CFA_nop padding shrinks and FDEs
# realign (8 bytes for n64); a section the linker cannot read stays whole
# with a warning.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump)
if(NOT LLVM_DWARFDUMP OR NOT DEFINED CC)
    message(STATUS "skipping .eh_frame test: llvm-dwarfdump and CC are required")
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
foreach(abi o32 n64)
    set(triple mips-unknown-elf)
    if(abi STREQUAL "n64")
        set(triple mips64-unknown-elf)
    endif()
    foreach(name frames_a frames_b)
        run_checked("compile ${name} ${abi}" "${CC}" -target ${triple} -O2 -funwind-tables -ffunction-sections
                    -c "${sources}/${name}.x" -o "${OUTPUT}/${name}-${abi}.o")
    endforeach()
endforeach()
run_checked("assemble raw" "${AS}" -target mips-unknown-elf "${sources}/frames_raw.s" -o "${OUTPUT}/raw.o")

set(script "${OUTPUT}/frames.ld")
file(WRITE "${script}" "ENTRY(caught)\nSECTIONS {\n  . = 0x80001000;\n  .text : { *(.text .text.*) }\n"
     "  .eh_frame_hdr : { *(.eh_frame_hdr) }\n  .eh_frame : { KEEP(*(.eh_frame)) }\n"
     "  .gcc_except_table : { *(.gcc_except_table .gcc_except_table.*) }\n  .data : { *(.data .data.*) }\n"
     "  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.MIPS.options) *(.pdr) *(.gnu.attributes) *(.comment) }\n}\n")
set(o32 "${OUTPUT}/frames_a-o32.o" "${OUTPUT}/frames_b-o32.o" "${OUTPUT}/raw.o")

# frames(LABEL OPTIONS...): links the o32 objects with the options; the
# CIE and FDE counts are in cies and fdes.
function(frames label)
    run_checked("${label}" "${LD}" ${ARGN} -T "${script}" ${o32} -o "${OUTPUT}/${label}.elf")
    run_checked("${label} frames" "${LLVM_DWARFDUMP}" --eh-frame "${OUTPUT}/${label}.elf")
    string(REGEX MATCHALL "\n[0-9a-f]+ [0-9a-f]+ [0-9a-f]+ CIE" cie_list "${run_stdout}")
    string(REGEX MATCHALL "\n[0-9a-f]+ [0-9a-f]+ [0-9a-f]+ FDE" fde_list "${run_stdout}")
    list(LENGTH cie_list cie_count)
    list(LENGTH fde_list fde_count)
    set(cies ${cie_count} PARENT_SCOPE)
    set(fdes ${fde_count} PARENT_SCOPE)
    set(frames_text "${run_stdout}" PARENT_SCOPE)
    if(GNU_LD)
        run_checked("${label} GNU ld" "${GNU_LD}" ${ARGN} -T "${script}" ${o32} -o "${OUTPUT}/${label}-gnu.elf")
        same_as_gnu("${label}" "${OUTPUT}/${label}.elf" "${OUTPUT}/${label}-gnu.elf")
    endif()
endfunction()

# The two compiled objects share one CIE; the raw object brings its own.
frames(whole)
if(NOT cies EQUAL 2 OR NOT fdes EQUAL 6)
    message(FATAL_ERROR "expected 2 CIEs and 6 FDEs, got ${cies} and ${fdes}\n${frames_text}")
endif()
frames(collected --gc-sections)
if(NOT cies EQUAL 2 OR NOT fdes EQUAL 4)
    message(FATAL_ERROR "expected 2 CIEs and 4 FDEs after --gc-sections, got ${cies} and ${fdes}\n${frames_text}")
endif()
expect("collected" "${frames_text}" "Personality Address: 0+800010")
if(frames_text MATCHES "pc=0+80001000[.][.][.]0+80001008")
    message(FATAL_ERROR "the FDE of dropped stayed\n${frames_text}")
endif()
run_checked("collected sections" "${LLVM_READELF}" -S -W "${OUTPUT}/collected.elf")
# Only the LSDA of `caught` stays.
expect("collected sections" "${run_stdout}" "[.]gcc_except_table +PROGBITS +[0-9a-f]+ [0-9a-f]+ 000008 ")

# The output sections that stay are decided before .eh_frame is edited, so
# a .eh_frame that collection empties stays, empty, with the labels in it.
run_checked("emptied" "${LD}" --gc-sections -e personality -T "${script}" "${OUTPUT}/raw.o"
            -o "${OUTPUT}/emptied.elf")
run_checked("emptied sections" "${LLVM_READELF}" -S -s -W "${OUTPUT}/emptied.elf")
expect("emptied sections" "${run_stdout}" "[.]eh_frame +PROGBITS +80001008 [0-9a-f]+ 000000 "
       "80001008 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ cie\n")
if(GNU_LD)
    run_checked("emptied GNU ld" "${GNU_LD}" --gc-sections -e personality -T "${script}" "${OUTPUT}/raw.o"
                -o "${OUTPUT}/emptied-gnu.elf")
    same_as_gnu("emptied" "${OUTPUT}/emptied.elf" "${OUTPUT}/emptied-gnu.elf")
endif()

# The search table: one entry per FDE, sorted, relative to the header.
frames(header --gc-sections --eh-frame-hdr)
run_checked("header unwind" "${LLVM_READELF}" -u -l -s -W "${OUTPUT}/header.elf")
expect("header unwind" "${run_stdout}" "fde_count: 4" "table_enc: 0x3b" "eh_frame_ptr_enc: 0x1b"
       "GNU_EH_FRAME +0x[0-9a-f]+ 0x[0-9a-f]+ 0x[0-9a-f]+ 0x0002c 0x0002c R +0x4"
       "LOCAL +DEFAULT +[0-9]+ __GNU_EH_FRAME_HDR")
string(REGEX MATCHALL "initial_location: 0x[0-9a-f]+" locations "${run_stdout}")
set(sorted ${locations})
list(SORT sorted)
if(NOT locations STREQUAL sorted)
    message(FATAL_ERROR "the search table is not sorted: ${locations}")
endif()

# n64 FDEs hold 8-byte addresses and align to 8.
run_checked("n64" "${LD}" --eh-frame-hdr -T "${script}" "${OUTPUT}/frames_a-n64.o" "${OUTPUT}/frames_b-n64.o"
            -e first -o "${OUTPUT}/n64.elf")
run_checked("n64 frames" "${LLVM_DWARFDUMP}" --eh-frame "${OUTPUT}/n64.elf")
string(REGEX MATCHALL "\n([0-9a-f]+) [0-9a-f]+ [0-9a-f]+ FDE" fde_offsets "${run_stdout}")
foreach(fde IN LISTS fde_offsets)
    string(REGEX REPLACE "\n([0-9a-f]+) .*" "\\1" offset "${fde}")
    math(EXPR low "0x${offset} % 8")
    if(NOT low EQUAL 0)
        message(FATAL_ERROR "an n64 FDE at ${offset} is not 8-byte aligned\n${run_stdout}")
    endif()
endforeach()
if(GNU_LD)
    run_checked("n64 GNU ld" "${GNU_LD}" --oformat=elf64-bigmips -z max-page-size=0x10000 --eh-frame-hdr
                -T "${script}" "${OUTPUT}/frames_a-n64.o" "${OUTPUT}/frames_b-n64.o" -e first
                -o "${OUTPUT}/n64-gnu.elf")
    same_as_gnu("n64" "${OUTPUT}/n64.elf" "${OUTPUT}/n64-gnu.elf")
endif()

# A CIE version the linker does not read leaves the section whole and the
# search table out.
file(WRITE "${OUTPUT}/odd.s" ".section .eh_frame,\"a\",@progbits\n.4byte 12\n.4byte 0\n.byte 2\n.asciz \"\"\n.byte 1, 0x7c, 31, 0, 0, 0\n")
run_checked("assemble odd" "${AS}" -target mips-unknown-elf "${OUTPUT}/odd.s" -o "${OUTPUT}/odd.o")
run_checked("odd" "${LD}" --eh-frame-hdr -T "${script}" ${o32} "${OUTPUT}/odd.o" -o "${OUTPUT}/odd.elf")
expect("odd" "${run_stderr}" "warning: [^\n]*odd[.]o:[(][.]eh_frame[+]0x0[)]: cannot edit [.]eh_frame")
section_hex("${OUTPUT}/odd.elf" .eh_frame_hdr header)
expect("odd header" "${header}" "^011bffff[0-9a-f]+$")
