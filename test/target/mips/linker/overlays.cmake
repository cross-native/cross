# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# OVERLAY: sections that share a run address and load one after another,
# the __load_start_ and __load_stop_ symbols, the location counter past the
# largest section, an OVERLAY in MEMORY regions, and NOCROSSREFS.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
run_checked("assemble" "${AS}" -target mips-unknown-elf "${sources}/overlay.s" -o "${OUTPUT}/overlay.o")
run_checked("link" "${LD}" -T "${sources}/overlay.ld" "${OUTPUT}/overlay.o" -o "${OUTPUT}/overlay.elf")
run_checked("headers" "${LLVM_READELF}" -S -l -W "${OUTPUT}/overlay.elf")
expect("headers" "${run_stdout}"
       "[.]ova +PROGBITS +80300000 [0-9a-f]+ 000008 "
       "[.]ovb +PROGBITS +80300000 [0-9a-f]+ 000020 "
       "[.]ovc +PROGBITS +80200000 [0-9a-f]+ 000004 "
       "[.]data +PROGBITS +80200004 "
       "LOAD +0x[0-9a-f]+ 0x80300000 0x80108000 0x00008 0x00008 R E"
       "LOAD +0x[0-9a-f]+ 0x80300000 0x80108008 0x00020 0x00020 R E"
       "LOAD +0x[0-9a-f]+ 0x80200000 0x80100010 0x00020 0x00020 RW ")
run_checked("symbols" "${LLVM_NM}" --format=posix "${OUTPUT}/overlay.elf")
expect("symbols" "${run_stdout}" "(^|\n)__load_start_ova A 80108000" "(^|\n)__load_stop_ova A 80108008"
       "(^|\n)__load_start_ovb A 80108008" "(^|\n)__load_stop_ovb A 80108028"
       "(^|\n)__load_start_ovc A 80100010" "(^|\n)__load_stop_ovc A 80100014"
       "(^|\n)after_overlay T 80300020")
# The fill pattern pads the alignment gap at the end of .ovb.
section_hex("${OUTPUT}/overlay.elf" .ovb ovb)
if(NOT ovb STREQUAL "03e0000824020002000000000000000011223344112233441122334411223344")
    message(FATAL_ERROR ".ovb is ${ovb}")
endif()
section_hex("${OUTPUT}/overlay.elf" .data data)
if(NOT data STREQUAL "80108000801080088010800880108028801000108010001480300020")
    message(FATAL_ERROR ".data is ${data}")
endif()
if(GNU_LD)
    run_checked("GNU ld" "${GNU_LD}" -T "${sources}/overlay.ld" "${OUTPUT}/overlay.o" -o "${OUTPUT}/gnu.elf")
    same_as_gnu("overlay" "${OUTPUT}/overlay.elf" "${OUTPUT}/gnu.elf")
endif()

# NOCROSSREFS: code in .ova may not call into .ovb.
file(WRITE "${OUTPUT}/cross.s" ".set noreorder\n.text\n.globl _start\n_start: nop\n"
     ".section .ov_a,\"ax\",@progbits\njal ov_b_fn\nnop\n"
     ".section .ov_b,\"ax\",@progbits\n.globl ov_b_fn\nov_b_fn: jr $31\nnop\n"
     ".section .ov_c,\"aw\",@progbits\n.word 0\n.data\n.word 0\n")
run_checked("assemble cross" "${AS}" -target mips-unknown-elf "${OUTPUT}/cross.s" -o "${OUTPUT}/cross.o")
file(READ "${sources}/overlay.ld" script)
string(REPLACE "after_overlay = .;" "" script "${script}")
file(WRITE "${OUTPUT}/cross.ld" "${script}")
run_failing("nocrossrefs" "${LD}" -T "${OUTPUT}/cross.ld" "${OUTPUT}/cross.o" -o "${OUTPUT}/cross.elf")
expect("nocrossrefs" "${run_stderr}"
       "cross[.]o:[(][.]ov_a[+]0x0[)]: prohibited cross reference from [.]ova to 'ov_b_fn' in [.]ovb")
# NOCROSSREFS_TO prohibits references to its first section only.
string(REPLACE "NOCROSSREFS AT" "AT" to "${script}")
file(WRITE "${OUTPUT}/to.ld" "NOCROSSREFS_TO(.ova .ovb)\n${to}")
run_checked("nocrossrefs_to" "${LD}" -T "${OUTPUT}/to.ld" "${OUTPUT}/cross.o" -o "${OUTPUT}/to.elf")
file(WRITE "${OUTPUT}/to2.ld" "NOCROSSREFS_TO(.ovb .ova)\n${to}")
run_failing("nocrossrefs_to reverse" "${LD}" -T "${OUTPUT}/to2.ld" "${OUTPUT}/cross.o" -o "${OUTPUT}/to2.elf")
expect("nocrossrefs_to reverse" "${run_stderr}" "prohibited cross reference from [.]ova to 'ov_b_fn' in [.]ovb")
