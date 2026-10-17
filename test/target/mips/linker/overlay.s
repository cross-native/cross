# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Code and data for overlay.ld: two overlaid code sections, an overlaid
# data section, and a table of the __load_start_ and __load_stop_ symbols.

	.set noreorder
	.text
	.globl _start
_start:	jal ov_a_fn
	nop
	jal ov_b_fn
	nop

	.section .ov_a,"ax",@progbits
	.globl ov_a_fn
ov_a_fn: jr $31
	li $2, 1

	.section .ov_b,"ax",@progbits
	.globl ov_b_fn
ov_b_fn: jr $31
	li $2, 2
	nop
	nop

	.section .ov_c,"aw",@progbits
	.globl ov_c_data
ov_c_data: .word 0x12345678

	.data
	.word __load_start_ova, __load_stop_ova, __load_start_ovb, __load_stop_ovb
	.word __load_start_ovc, __load_stop_ovc, after_overlay
