# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Input for expressions.cmake: code, empty sections, data, and zero-filled
# data.

	.text
	.globl _start
_start:	nop
	.section .empty,"a",@progbits
	.section .empty2,"a",@progbits
	.data
	.word 1
	.bss
	.balign 4
	.space 4
