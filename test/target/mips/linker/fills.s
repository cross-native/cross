# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Input sections for fills.ld: code with an aligned second part, sections
# of different alignments, and data that output section types change.

	.set noreorder
	.text
	.globl _start
_start:	jr $31
	nop
	.section .text.b,"ax",@progbits
	.balign 16
	.globl b
b:	jr $31
	nop
	.globl end_b
end_b:
	.section .a4,"a",@progbits
	.word 4
	.section .a64,"a",@progbits
	.balign 64
	.word 64
	.section .stuff,"aw",@progbits
	.word 5
	.section .ro,"aw",@progbits
	.word 6
	.section .info,"",@progbits
	.word 7
	.data
	.word 8
	.bss
	.space 16
