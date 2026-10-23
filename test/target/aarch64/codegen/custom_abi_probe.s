// custom_abi_probe(a, b, c) under aapcs64: calls pinned_sum(a, b, c) under
// the model custom_abi.xm with canaries in x9 and x10, and returns its
// result, or 109 or 110 when the call changed x9 or x10.
	.text
	.globl	custom_abi_probe
	.type	custom_abi_probe, %function
custom_abi_probe:
	stp	x29, x30, [sp, #-48]!
	mov	x29, sp
	stp	x19, x20, [sp, #16]
	str	x21, [sp, #32]
	mov	x20, x0
	mov	x21, x1
	mov	x1, x2
	mov	x9, #0x909
	mov	x10, #0xa0a
	bl	pinned_sum
	mov	x0, #109
	cmp	x9, #0x909
	b.ne	1f
	mov	x0, #110
	cmp	x10, #0xa0a
	b.ne	1f
	sxtw	x0, w19
1:	ldp	x19, x20, [sp, #16]
	ldr	x21, [sp, #32]
	ldp	x29, x30, [sp], #48
	ret
