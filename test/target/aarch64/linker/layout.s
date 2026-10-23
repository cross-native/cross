// Sections of each kind for the default layout: code with unwind
// information, read-only data, data, zero-filled data, TLS, initializer
// tables, and a section that only --gc-sections removes.
	.text
	.globl	_start
	.type	_start, %function
_start:
	.cfi_startproc
	stp	x29, x30, [sp, #-16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	adrp	x0, read_only
	ldr	x0, [x0, :lo12:read_only]
	adrp	x1, data
	ldr	x1, [x1, :lo12:data]
	adrp	x2, zeros
	add	x2, x2, :lo12:zeros
	mrs	x3, tpidr_el0
	add	x3, x3, :tprel_lo12_nc:tls_data
	bl	helper
	ldp	x29, x30, [sp], #16
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc

	.section .text.helper,"ax",@progbits
	.type	helper, %function
helper:
	.cfi_startproc
	ret
	.cfi_endproc

	.section .text.unused,"ax",@progbits
	.globl	unused
	.type	unused, %function
unused:
	.cfi_startproc
	ret
	.cfi_endproc

	.section .rodata,"a",@progbits
	.p2align	3
read_only:
	.xword	1

	.section .init_array,"aw",@init_array
	.p2align	3
	.xword	helper

	.data
	.p2align	4
data:
	.xword	2, 3

	.bss
	.p2align	5
zeros:
	.zero	64

	.section .tdata,"awT",@progbits
	.p2align	4
tls_data:
	.xword	4

	.section .tbss,"awT",@nobits
	.p2align	3
	.zero	8

	.section .note.GNU-stack,"",@progbits
