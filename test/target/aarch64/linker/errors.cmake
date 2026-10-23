# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AArch64 link errors: values out of range and misaligned, undefined
# symbols, unsupported relocations and objects, and what a position-
# independent executable cannot hold, each reported at its file, section,
# and offset with the relocation and its symbol.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()

# link_fails(NAME SOURCE LD_OPTIONS PATTERN...): SOURCE assembled by
# Cross as does not link with the options, and ld reports each pattern.
function(link_fails name source options)
    file(WRITE "${OUTPUT}/${name}.s" "${source}")
    assemble(cross "${OUTPUT}/${name}.s" "${OUTPUT}/${name}.o")
    run_failing("ld ${name}" "${LD}" ${options} "${OUTPUT}/${name}.o" -o "${OUTPUT}/${name}.elf")
    expect("${name}" "${run_stderr}" ${ARGN})
    if(EXISTS "${OUTPUT}/${name}.elf")
        message(FATAL_ERROR "${name}: ld wrote an image despite the error")
    endif()
endfunction()

set(text "-Ttext=0x40080000")
set(far "--section-start=.far=")
set(head "\t.text\n\t.globl _start\n_start:\n")

# Out of range, each just past its limit.
link_fails(condbr19 "${head}\tnop\n\tb.eq target\n\t.section .far,\"ax\"\n\t.globl target\ntarget:\n\tret\n"
           "${text};${far}0x40180004"
           "condbr19\\.o:\\(\\.text\\+0x4\\): R_AARCH64_CONDBR19 against 'target' out of range: 1048576 is not in \\[-1048576, 1048575\\]")
link_fails(tstbr14 "${head}\ttbz x0, #3, target\n\t.section .far,\"ax\"\n\t.globl target\ntarget:\n\tret\n"
           "${text};${far}0x40088000"
           "tstbr14\\.o:\\(\\.text\\+0x0\\): R_AARCH64_TSTBR14 against 'target' out of range: 32768 is not in \\[-32768, 32767\\]")
link_fails(ld_prel_lo19 "${head}\tldr x0, target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.xword 0\n"
           "${text};${far}0x40180000"
           "R_AARCH64_LD_PREL_LO19 against 'target' out of range: 1048576 is not in \\[-1048576, 1048575\\]")
link_fails(adr_prel_lo21 "${head}\tadr x0, target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0x40180000"
           "adr_prel_lo21\\.o:\\(\\.text\\+0x0\\): R_AARCH64_ADR_PREL_LO21 against 'target' out of range: 1048576 is not in \\[-1048576, 1048575\\]")
link_fails(adr_prel_pg_hi21 "${head}\tadrp x0, target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0x140080000"
           "R_AARCH64_ADR_PREL_PG_HI21 against 'target' out of range: 4294967296 is not in \\[-4294967296, 4294967295\\]")
link_fails(abs32 "${head}\tret\n\t.data\n\t.word target + 4\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0xfffffffc"
           "abs32\\.o:\\(\\.data\\+0x0\\): R_AARCH64_ABS32 against 'target' out of range: 4294967296 is not in \\[-2147483648, 4294967295\\]")
link_fails(abs16 "${head}\tret\n\t.data\n\t.hword target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0x10000"
           "R_AARCH64_ABS16 against 'target' out of range: 65536 is not in \\[-32768, 65535\\]")
link_fails(prel32 "${head}\tret\n\t.data\n\t.word target - .\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};--section-start=.data=0x40100000;${far}0xc0100000"
           "R_AARCH64_PREL32 against 'target' out of range: 2147483648 is not in \\[-2147483648, 2147483647\\]")
link_fails(movw_uabs_g0 "${head}\tmovz x0, #:abs_g0:target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0x10000"
           "R_AARCH64_MOVW_UABS_G0 against 'target' out of range: 65536 is not in \\[0, 65535\\]")
link_fails(movw_sabs_g1 "${head}\tmovz x0, #:abs_g1_s:target\n\t.section .far,\"a\"\n\t.globl target\ntarget:\n\t.byte 0\n"
           "${text};${far}0x100000000"
           "R_AARCH64_MOVW_SABS_G1 against 'target' out of range: 4294967296 is not in \\[-4294967296, 4294967295\\]")
link_fails(tprel_lo12 "${head}\tadd x0, x0, :tprel_lo12:last\n\t.section .tbss,\"awT\",@nobits\n\t.zero 0xff0\nlast:\n\t.zero 8\n"
           "${text}"
           "tprel_lo12\\.o:\\(\\.text\\+0x0\\): R_AARCH64_TLSLE_ADD_TPREL_LO12 against 'last' out of range: 4096 is not in \\[0, 4095\\]")
link_fails(tprel_hi12 "${head}\tadd x0, x0, #:tprel_hi12:last, lsl #12\n\t.section .tbss,\"awT\",@nobits\n\t.zero 0xffffff0\nlast:\n\t.zero 8\n"
           "${text}"
           "R_AARCH64_TLSLE_ADD_TPREL_HI12 against 'last' out of range: 268435456 is not in \\[0, 16777215\\]")

# Misaligned targets and offsets.
link_fails(ldst64 "${head}\tldr x0, [x1, :lo12:odd]\n\t.data\n\t.p2align 4\n\t.word 0\nodd:\n\t.xword 0\n" "${text}"
           "ldst64\\.o:\\(\\.text\\+0x0\\): R_AARCH64_LDST64_ABS_LO12_NC against '\\.data\\+0x4': 0x[0-9a-f]*4 is not a multiple of 8")
link_fails(ldst128 "${head}\tldr q0, [x1, :lo12:odd]\n\t.data\n\t.p2align 4\n\t.xword 0\n\t.globl odd\nodd:\n\t.xword 0, 0\n" "${text}"
           "R_AARCH64_LDST128_ABS_LO12_NC against 'odd': 0x[0-9a-f]*8 is not a multiple of 16")
link_fails(ldst16 "${head}\tldrh w0, [x1, :lo12:odd]\n\t.data\n\t.p2align 4\n\t.byte 0\n\t.globl odd\nodd:\n\t.hword 0\n" "${text}"
           "R_AARCH64_LDST16_ABS_LO12_NC against 'odd': 0x[0-9a-f]*1 is not a multiple of 2")
link_fails(branch_target "${head}\tbl odd\n\t.section .far,\"ax\"\n\t.p2align 4\n\t.byte 0, 0\n\t.globl odd\nodd:\n\tret\n" "${text}"
           "branch_target\\.o:\\(\\.text\\+0x0\\): R_AARCH64_CALL26 against 'odd': 0x[0-9a-f]+ is not a multiple of 4")
link_fails(tls_ldst64 "${head}\tldr x0, [x1, :tprel_lo12_nc:odd]\n\t.section .tdata,\"awT\",@progbits\n\t.word 0\n\t.globl odd\nodd:\n\t.xword 0\n"
           "${text}" "R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC against 'odd': 0x14 is not a multiple of 8")

# Undefined symbols, TLS without a TLS segment, and discarded sections.
link_fails(undefined "${head}\tnop\n\tbl missing\n\tadrp x0, :got:missing_data\n" "${text}"
           "undefined\\.o:\\(\\.text\\+0x4\\): undefined reference to 'missing'"
           "undefined\\.o:\\(\\.text\\+0x8\\): undefined reference to 'missing_data'")
link_fails(no_tls "${head}\tadd x0, x0, :tprel_lo12_nc:_start\n" "${text}"
           "no_tls\\.o:\\(\\.text\\+0x0\\): R_AARCH64_TLSLE_ADD_TPREL_LO12_NC against '_start' needs a TLS segment, which the link lacks")
file(WRITE "${OUTPUT}/discard.ld" "SECTIONS { .text 0x40080000 : { *(.text) } /DISCARD/ : { *(.gone) } }\n")
link_fails(discarded "${head}\tbl gone\n\t.section .gone,\"ax\"\n\t.globl gone\ngone:\n\tret\n" "-T;${OUTPUT}/discard.ld"
           "discarded\\.o:\\(\\.text\\+0x0\\): R_AARCH64_CALL26 refers to 'gone', which is in a discarded section")

# Relocations ld does not apply: general and local dynamic TLS,
# descriptors, and initial-exec moves.
link_fails(unsupported
           "${head}\tadrp x0, :tlsdesc:v\n\tldr x1, [x0, :tlsdesc_lo12:v]\n\tadd x2, x2, :dtprel_lo12:v\n\tmovz x3, #:gottprel_g1:v\n\t.section .tbss,\"awT\",@nobits\n\t.globl v\nv:\n\t.zero 8\n"
           "${text}"
           "unsupported\\.o:\\(\\.text\\+0x0\\): unsupported R_AARCH64_TLSDESC_ADR_PAGE21 against 'v'"
           "unsupported\\.o:\\(\\.text\\+0x4\\): unsupported R_AARCH64_TLSDESC_LD64_LO12 against 'v'"
           "unsupported\\.o:\\(\\.text\\+0x8\\): unsupported R_AARCH64_TLSLD_ADD_DTPREL_LO12 against 'v'"
           "unsupported\\.o:\\(\\.text\\+0xc\\): unsupported R_AARCH64_TLSIE_MOVW_GOTTPREL_G1 against 'v'")

# What a position-independent executable cannot hold.
link_fails(pie_text "${head}\tret\n\t.section .rodata,\"a\"\n\t.p2align 3\n\t.xword _start\n" "-pie"
           "pie_text\\.o:\\(\\.rodata\\+0x0\\): R_AARCH64_ABS64 against '_start' needs a dynamic relocation in read-only section '\\.rodata'")
link_fails(pie_abs32 "${head}\tret\n\t.data\n\t.word _start\n" "-pie"
           "pie_abs32\\.o:\\(\\.data\\+0x0\\): R_AARCH64_ABS32 against '_start' cannot be used in a position-independent executable")
link_fails(pie_movw "${head}\tmovz x0, #:abs_g1:_start\n" "-pie"
           "pie_movw\\.o:\\(\\.text\\+0x0\\): R_AARCH64_MOVW_UABS_G1 against '_start' cannot be used in a position-independent executable")
link_fails(pie_absolute "${head}\tadrp x0, fixed\n\tbl fixed\n" "-pie;--defsym;fixed=0x1000"
           "pie_absolute\\.o:\\(\\.text\\+0x0\\): R_AARCH64_ADR_PREL_PG_HI21 against 'fixed' refers to an absolute symbol"
           "pie_absolute\\.o:\\(\\.text\\+0x4\\): R_AARCH64_CALL26 against 'fixed' refers to an absolute symbol")
link_fails(pie_relocatable "${head}\tret\n" "-pie;-r" "-r and -pie may not be used together")

# Objects of another byte order or class.
file(WRITE "${OUTPUT}/order.s" "${head}\tret\n")
foreach(triple aarch64_be aarch64-linux-gnu_ilp32)
    run_checked("llvm-mc ${triple}" "${LLVM_MC}" --triple=${triple} --filetype=obj "${OUTPUT}/order.s"
                -o "${OUTPUT}/${triple}.o")
    run_failing("ld ${triple}" "${LD}" "${OUTPUT}/${triple}.o" -o "${OUTPUT}/${triple}.elf")
    expect("${triple}" "${run_stderr}" "${triple}\\.o: only little-endian ELF64 AArch64 objects are supported")
endforeach()
# PIE needs a target with relative relocations.
file(WRITE "${OUTPUT}/mips.s" "${head}\tjr $ra\n")
run_checked("as for MIPS" "${AS}" -target mips-unknown-elf "${OUTPUT}/mips.s" -o "${OUTPUT}/mips.o")
run_failing("ld -pie for MIPS" "${LD}" -pie "${OUTPUT}/mips.o" -o "${OUTPUT}/mips.elf")
expect("ld -pie for MIPS" "${run_stderr}" "position-independent executables are not supported for ELF machine 8")
