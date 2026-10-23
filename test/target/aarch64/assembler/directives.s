// Directives of AArch64 objects: sections, symbols, data, alignment, call
// frames, line tables, instruction words, and the mapping symbols that mark
// code ($x) and data ($d) in sections that hold code.
	.arch	armv8-a
	.file	1 "directives.c"
	.text
	.globl	main
	.p2align	2
	.type	main, %function
main:
	.cfi_startproc
	.loc	1 3 0
	stp	x29, x30, [sp, #-32]!
	.cfi_def_cfa_offset 32
	.cfi_offset w30, -24
	.cfi_offset w29, -32
	mov	x29, sp
	.cfi_def_cfa w29, 32
	str	d8, [sp, #16]
	.cfi_offset b8, -16
	.loc	1 4 7 prologue_end
	bl	helper
	.loc	1 5 0 is_stmt 0
	ldr	d8, [sp, #16]
	.cfi_restore b8
	ldp	x29, x30, [sp], #32
	.cfi_def_cfa wsp, 0
	.cfi_restore w30
	.cfi_restore w29
	ret
	.cfi_endproc
	.size	main, . - main

	.local	helper
	.type	helper, @function
helper:
	.cfi_startproc
	.cfi_remember_state
	nop
	.cfi_restore_state
	ret
	.cfi_endproc
	.size	helper, . - helper

	// Data in code, and alignment padding that is code.
	.word	0x12345678
	.byte	1, 2, 3
	.p2align	4
	.inst	0xd503201f, 0xd65f03c0
	.balign	8
	.xword	main
	add	x0, x1, #1
	.align	3

	.section .rodata.str1.1,"aMS",@progbits,1
.Lstring:
	.asciz	"hello"
	.string	"world"

	.section .rodata,"a",@progbits
	.p2align	3
table:
	.xword	.Lstring, main + 4
	.dword	helper
	.word	.Lstring - table
	.hword	0x1234, -1
	.2byte	7
	.4byte	8
	.8byte	9
	.quad	10
	.short	11
	.long	12
	.float	1.5
	.double	-2.25
	.uleb128	624485
	.sleb128	-123456
	.ascii	"abc"
	.zero	3
	.space	2, 0xff
	.fill	2, 4, 0x01020304
	.size	table, . - table
	.type	table, %object

	.data
	.globl	counter
	.p2align	2
counter:
	.word	42
	.weak	weak_data
weak_data:
	.word	0
	.hidden	hidden_data
	.globl	hidden_data
hidden_data:
	.word	1
	.set	alias_data, counter + 4
	.globl	alias_data

	.bss
	.p2align	4
buffer:
	.zero	64
	.comm	common_data, 32, 8
	.lcomm	local_common, 16

	// A section of data with code after it.
	.section .init_code,"aw",@progbits
	.p2align	2
	.word	3
	nop
	.word	4

	.section .text.unlikely,"ax",@progbits
	.cpu	cortex-a57+crc
	crc32cx	w0, w1, x2
	.arch_extension lse
	ldaddal	x0, x1, [x2]
	.arch	armv8.2-a+fp16
	fadd	h0, h1, h2
