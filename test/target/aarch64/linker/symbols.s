// Symbols the relocation families refer to from another object: absolute
// values, data more than 1 MiB from the code, and a function in another
// section.
	.globl	small_abs, small_neg, mid_neg, big_neg, abs16, neg16, wide_abs
	.set	small_abs, 0x1234
	.set	small_neg, -0x1234
	.set	mid_neg, -0x12345678
	.set	big_neg, -0x123456789abc
	.set	abs16, 0xfedc
	.set	neg16, -2
	.set	wide_abs, 0x123456789abcdef0

	.section .text.other,"ax",@progbits
	.globl	other_function
	.type	other_function, %function
other_function:
	ret

	.data
	.p2align	4
	.globl	far_data, far_bytes, far_halves, far_words, far_xwords, far_quads
far_data:
far_bytes:
	.byte	1, 2, 3, 4
far_halves:
	.hword	5, 6, 7, 8
	.p2align	3
far_words:
	.word	9, 10, 11, 12
far_xwords:
	.xword	13, 14
	.p2align	4
far_quads:
	.xword	15, 16, 17, 18
