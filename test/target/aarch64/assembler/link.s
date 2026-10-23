// A bare-metal program for QEMU's virt machine that checks what the linker
// made of each relocation: it exits through semihosting with 0, or with the
// number of the first check that fails.
	.section .text.start,"ax",@progbits
	.globl	_start
	.type	_start, %function
_start:
	adrp	x0, stack_top
	add	x0, x0, :lo12:stack_top
	mov	sp, x0
	// Floating point at EL1, and the thread pointer of the static TLS
	// block (variant 1: a 16-byte control block before the data).
	mov	x0, #(3 << 20)
	msr	cpacr_el1, x0
	isb
	adrp	x0, tls_start
	add	x0, x0, :lo12:tls_start
	sub	x0, x0, #16
	msr	tpidr_el0, x0
	bl	main
	b	exit

	.text
	.globl	main
	.type	main, %function
main:
	stp	x29, x30, [sp, #-32]!
	mov	x29, sp
	// 1: a call to another section, and a tail of conditional branches.
	mov	w0, #1
	bl	answer
	cmp	x0, #42
	b.ne	fail
	cbz	x0, fail
	cbnz	xzr, fail
	tbz	x0, #1, fail
	tbnz	x0, #0, fail
	// 2: adrp and :lo12: of every access size.
	mov	w0, #2
	adrp	x1, numbers
	add	x1, x1, :lo12:numbers
	ldrb	w2, [x1]
	adrp	x3, numbers
	ldrb	w4, [x3, :lo12:numbers]
	cmp	w2, w4
	b.ne	fail
	ldrh	w4, [x3, :lo12:numbers + 2]
	ldr	w5, [x3, :lo12:numbers + 4]
	ldr	x6, [x3, :lo12:numbers + 8]
	ldr	q7, [x3, :lo12:numbers + 16]
	movz	w8, #0x0302
	cmp	w4, w8
	b.ne	fail
	movz	w8, #0x0706, lsl #16
	movk	w8, #0x0504
	cmp	w5, w8
	b.ne	fail
	fmov	x9, d7
	cmp	x6, x9
	b.ne	fail
	// 3: the GOT, adr, and a literal give the same address.
	mov	w0, #3
	adrp	x10, :got:numbers
	ldr	x10, [x10, :got_lo12:numbers]
	cmp	x10, x1
	b.ne	fail
	adr	x11, numbers
	cmp	x11, x1
	b.ne	fail
	ldr	x12, numbers_address
	cmp	x12, x1
	b.ne	fail
	adrp	x13, :pg_hi21_nc:numbers
	add	x13, x13, :lo12:numbers
	cmp	x13, x1
	b.ne	fail
	// 4: absolute moves.
	mov	w0, #4
	movz	x14, #:abs_g3:numbers
	movk	x14, #:abs_g2_nc:numbers
	movk	x14, #:abs_g1_nc:numbers
	movk	x14, #:abs_g0_nc:numbers
	cmp	x14, x1
	b.ne	fail
	movz	x14, #:abs_g1:numbers
	movk	x14, #:abs_g0_nc:numbers
	cmp	x14, x1
	b.ne	fail
	// 5: data relocations.
	mov	w0, #5
	adrp	x15, pointers
	add	x15, x15, :lo12:pointers
	ldr	x16, [x15]
	cmp	x16, x1
	b.ne	fail
	ldr	w16, [x15, #8]
	cmp	x16, x1
	b.ne	fail
	ldrsw	x16, [x15, #12]
	add	x16, x16, x15
	add	x16, x16, #12
	cmp	x16, x1
	b.ne	fail
	ldr	x16, [x15, #16]
	add	x16, x16, x15
	add	x16, x16, #16
	cmp	x16, x1
	b.ne	fail
	// 6: TLS through the GOT (initial exec) and by offset (local exec).
	mov	w0, #6
	mrs	x17, tpidr_el0
	adrp	x18, :gottprel:counter
	ldr	x18, [x18, #:gottprel_lo12:counter]
	ldr	x19, [x17, x18]
	cmp	x19, #7
	b.ne	fail
	add	x20, x17, #:tprel_hi12:counter, lsl #12
	add	x20, x20, #:tprel_lo12_nc:counter
	ldr	x21, [x20]
	cmp	x21, #7
	b.ne	fail
	movz	x22, #:tprel_g1:counter
	movk	x22, #:tprel_g0_nc:counter
	cmp	x22, x18
	b.ne	fail
	ldr	x23, [x17, #:tprel_lo12_nc:counter]
	cmp	x23, #7
	b.ne	fail
	// 7: arithmetic, bit-field, and conditional forms.
	mov	w0, #7
	mov	x1, #0x1234
	movk	x1, #0xabcd, lsl #32
	ubfx	x2, x1, #32, #16
	mov	x3, #0xabcd
	cmp	x2, x3
	b.ne	fail
	lsl	x3, x2, #4
	add	x3, x3, x2, lsr #4
	mov	x4, #0xc78c
	movk	x4, #0xa, lsl #16
	cmp	x3, x4
	b.ne	fail
	mov	x5, #100
	mov	x6, #7
	udiv	x7, x5, x6
	msub	x8, x7, x6, x5
	cmp	x7, #14
	ccmp	x8, #2, #0, eq
	b.ne	fail
	rev	w9, w1
	mov	w10, #0x34120000
	cmp	w9, w10
	b.ne	fail
	clz	x11, x1
	cmp	x11, #16
	cset	w12, eq
	cinc	w12, w12, eq
	cmp	w12, #2
	b.ne	fail
	and	x13, x1, #0xff00ff00ff00ff00
	mov	x14, #0xab0000000000
	movk	x14, #0x1200
	cmp	x13, x14
	b.ne	fail
	sxtb	x15, w1
	cmp	x15, #0x34
	b.ne	fail
	bfi	x16, xzr, #0, #64
	bfi	x16, x1, #8, #8
	mov	x17, #0x3400
	cmp	x16, x17
	b.ne	fail
	// 8: floating point.
	mov	w0, #8
	fmov	d0, #1.5
	fmov	d1, #-0.25
	fmul	d2, d0, d1
	fadd	d2, d2, d0
	fmov	d3, #2.0
	fmul	d2, d2, d3
	fcvtzs	x18, d2
	cmp	x18, #2
	b.ne	fail
	scvtf	s4, w18
	fmov	s5, #2.0
	fcmp	s4, s5
	b.ne	fail
	fcsel	d6, d0, d1, eq
	fcmp	d6, d0
	b.ne	fail
	// 9: exclusive and acquire-release accesses.
	mov	w0, #9
	adrp	x19, word
	add	x19, x19, :lo12:word
1:	ldaxr	w20, [x19]
	add	w20, w20, #1
	stlxr	w21, w20, [x19]
	cbnz	w21, 1b
	ldar	w22, [x19]
	cmp	w22, #101
	b.ne	fail
	mov	w0, #0
	ldp	x29, x30, [sp], #32
	ret

fail:
	b	exit

	.section .text.other,"ax",@progbits
	.type	answer, %function
answer:
	mov	x0, #42
	ret

	// Semihosting SYS_EXIT with the code in w0.
	.type	exit, %function
exit:
	adrp	x1, exit_block
	add	x1, x1, :lo12:exit_block
	str	x0, [x1, #8]
	mov	w0, #0x18
	hlt	#0xf000
1:	b	1b

	.data
	.p2align	4
numbers:
	.byte	1, 0, 2, 3, 4, 5, 6, 7
	.xword	0x1122334455667788
	.xword	0x1122334455667788, 0
numbers_address:
	.xword	numbers
pointers:
	.xword	numbers
	.word	numbers
	.word	numbers - .
	.xword	numbers - .
word:
	.word	100
	.p2align	3
exit_block:
	.xword	0x20026, 0

	.section .tdata,"awT",@progbits
	.p2align	3
counter:
	.xword	7
