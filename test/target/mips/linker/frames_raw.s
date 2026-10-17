# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# A hand-written .eh_frame with a personality routine and LSDAs: a zPLR
# CIE and FDEs for `caught`, which eh_frame.cmake keeps, and `dropped`,
# which --gc-sections collects with its LSDA.

	.set noreorder
	.section .text.caught,"ax",@progbits
	.globl caught
caught:	jal first
	nop
	jal second
	nop
	.section .text.dropped,"ax",@progbits
	.globl dropped
dropped: jr $31
	nop
	.section .text.personality,"ax",@progbits
	.globl personality
personality: jr $31
	nop
	.section .gcc_except_table.caught,"a",@progbits
lsda_caught: .byte 0xff, 0x9b, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00
	.section .gcc_except_table.dropped,"a",@progbits
lsda_dropped: .byte 0xff, 0x9b, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00

	.section .eh_frame,"a",@progbits
cie:	.4byte cie_end - cie_start
cie_start:
	.4byte 0
	.byte 1
	.asciz "zPLR"
	.uleb128 1
	.sleb128 -4
	.byte 31
	.uleb128 7
	.byte 0x00
	.4byte personality
	.byte 0x00
	.byte 0x0b
	.byte 0x0c, 29, 0
	.balign 4
cie_end:
fde_caught:
	.4byte fde_caught_end - fde_caught_start
fde_caught_start:
	.4byte fde_caught_start - cie
	.4byte caught
	.4byte 16
	.uleb128 4
	.4byte lsda_caught
	.balign 4
fde_caught_end:
fde_dropped:
	.4byte fde_dropped_end - fde_dropped_start
fde_dropped_start:
	.4byte fde_dropped_start - cie
	.4byte dropped
	.4byte 8
	.uleb128 4
	.4byte lsda_dropped
	.byte 0, 0, 0, 0, 0, 0, 0, 0
	.balign 4
fde_dropped_end:
