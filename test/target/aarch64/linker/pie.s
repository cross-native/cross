// A position-independent program that applies its own relative dynamic
// relocations, as a static PIE's startup code does, and checks the
// addresses that the GOT, data, and TLS accesses give. It exits through
// semihosting with 0, or with the number of the first check that fails.
	.section .text.start,"ax",@progbits
	.globl	_start
	.type	_start, %function
_start:
	// The load address: _start is at address 0.
	adr	x20, _start
	adrp	x1, stack_top
	add	x1, x1, :lo12:stack_top
	mov	sp, x1
	// 1: the dynamic section names the relocations.
	mov	x19, #1
	adrp	x1, _DYNAMIC
	add	x1, x1, :lo12:_DYNAMIC
	mov	x2, #0
	mov	x3, #0
	mov	x4, #0
1:	ldp	x5, x6, [x1], #16
	cbz	x5, 4f
	cmp	x5, #7
	b.ne	2f
	add	x2, x6, x20
2:	cmp	x5, #8
	csel	x3, x6, x3, eq
	cmp	x5, #9
	csel	x4, x6, x4, eq
	b	1b
4:	cbz	x2, fail
	cmp	x4, #24
	b.ne	fail
	// 2: each one is R_AARCH64_RELATIVE: *(base + offset) = base + addend.
	mov	x19, #2
	cbz	x3, 6f
5:	ldp	x5, x6, [x2]
	ldr	x7, [x2, #16]
	cmp	x6, #0x403
	b.ne	fail
	add	x7, x7, x20
	str	x7, [x5, x20]
	add	x2, x2, x4
	subs	x3, x3, x4
	b.ne	5b
6:	bl	main
	b	exit
fail:
	mov	x0, x19
	b	exit

	.text
	.type	main, %function
main:
	stp	x29, x30, [sp, #-16]!
	// 3: the image runs at an address other than its link address.
	mov	x19, #3
	cbz	x20, fail
	// 4: pointers in data, with addends, and in read-only data after
	// relocation.
	mov	x19, #4
	adr	x1, target
	adrp	x2, pointers
	add	x2, x2, :lo12:pointers
	ldr	x3, [x2]
	cmp	x3, x1
	b.ne	fail
	ldr	x3, [x2, #8]
	sub	x3, x3, #8
	cmp	x3, x1
	b.ne	fail
	adrp	x4, relro_pointer
	ldr	x4, [x4, :lo12:relro_pointer]
	cmp	x4, x1
	b.ne	fail
	// 5: absolute and undefined weak symbols take no relocation.
	mov	x19, #5
	ldr	x3, [x2, #16]
	movz	x5, #0x5678
	movk	x5, #0x1234, lsl #16
	cmp	x3, x5
	b.ne	fail
	ldr	x3, [x2, #24]
	cbnz	x3, fail
	// 6: the GOT holds relocated addresses, absolute values, and zero for
	// an undefined weak symbol.
	mov	x19, #6
	adrp	x6, :got:target
	ldr	x6, [x6, :got_lo12:target]
	cmp	x6, x1
	b.ne	fail
	adrp	x6, :got:ext_abs
	ldr	x6, [x6, :got_lo12:ext_abs]
	cmp	x6, x5
	b.ne	fail
	adrp	x6, :got:undefined_weak
	ldr	x6, [x6, :got_lo12:undefined_weak]
	cbnz	x6, fail
	// 7: a call through a relocated function pointer.
	mov	x19, #7
	ldr	x7, [x2, #32]
	blr	x7
	cmp	x0, #77
	b.ne	fail
	// 8: TLS by initial exec and local exec, with the control block before
	// the block (variant 1).
	mov	x19, #8
	adrp	x8, tls_start
	add	x8, x8, :lo12:tls_start
	sub	x8, x8, #16
	msr	tpidr_el0, x8
	mrs	x9, tpidr_el0
	adrp	x10, :gottprel:counter
	ldr	x10, [x10, :gottprel_lo12:counter]
	ldr	x11, [x9, x10]
	cmp	x11, #9
	b.ne	fail
	add	x12, x9, #:tprel_hi12:counter, lsl #12
	add	x12, x12, #:tprel_lo12_nc:counter
	ldr	x13, [x12]
	cmp	x13, #9
	b.ne	fail
	mov	x0, #0
	ldp	x29, x30, [sp], #16
	ret

	.type	target, %function
target:
	mov	x0, #77
	ret

	// Semihosting SYS_EXIT with the code in x0.
	.type	exit, %function
exit:
	adr	x1, exit_block
	str	x0, [x1, #8]
	mov	w0, #0x18
	hlt	#0xf000
1:	b	1b

	.section .data.rel.ro,"aw",@progbits
	.p2align	3
relro_pointer:
	.xword	target

	.data
	.p2align	3
	.weak	undefined_weak
pointers:
	.xword	target
	.xword	target + 8
	.xword	ext_abs
	.xword	undefined_weak
	.xword	target
exit_block:
	.xword	0x20026, 0

	.section .tdata,"awT",@progbits
	.p2align	3
counter:
	.xword	9

	.bss
	.p2align	4
	.zero	0x800
stack_top:
