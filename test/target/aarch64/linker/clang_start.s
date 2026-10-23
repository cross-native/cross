// Startup for QEMU's virt machine: apply the image's relative relocations
// (a position-independent image is linked at 0), set the stack, enable
// floating point, set the thread pointer before the TLS block (variant 1),
// run .init_array, call main, and exit through semihosting with its result.
	.section .text.start,"ax",@progbits
	.globl	_start
_start:
	// The load base of an image linked at 0 (a PIE); a static image
	// has no relative relocations.
	adr	x20, _start
	adrp	x1, stack_top
	add	x1, x1, :lo12:stack_top
	mov	sp, x1
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0
	isb
	adrp	x2, __rela_start
	add	x2, x2, :lo12:__rela_start
	adrp	x3, __rela_end
	add	x3, x3, :lo12:__rela_end
1:	cmp	x2, x3
	b.hs	2f
	ldp	x5, x6, [x2]
	ldr	x7, [x2, #16]
	add	x7, x7, x20
	str	x7, [x5, x20]
	add	x2, x2, #24
	b	1b
2:	adrp	x0, __tls_start
	add	x0, x0, :lo12:__tls_start
	sub	x0, x0, #16
	msr	tpidr_el0, x0
	adrp	x19, __init_array_start
	add	x19, x19, :lo12:__init_array_start
	adrp	x21, __init_array_end
	add	x21, x21, :lo12:__init_array_end
3:	cmp	x19, x21
	b.hs	4f
	ldr	x0, [x19], #8
	blr	x0
	b	3b
4:	bl	main
	adr	x1, exit_block
	str	x0, [x1, #8]
	mov	w0, #0x18
	hlt	#0xf000
5:	b	5b
	.p2align 3
exit_block:
	.xword	0x20026, 0
