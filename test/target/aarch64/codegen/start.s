// Bare-metal startup for QEMU's virt machine: calls test_main with
// canaries in the registers that the AAPCS64 and Cross ABIs preserve, and
// exits through semihosting with test_main's result, or with 200 plus the
// number of the first register that the call changed.
	.section .text.start,"ax",@progbits
	.globl	_start
	.type	_start, %function
_start:
	adrp	x0, stack_top
	add	x0, x0, :lo12:stack_top
	mov	sp, x0
	movz	x0, #0x30, lsl #16
	msr	cpacr_el1, x0
	isb
	mov	x19, #0x1900
	mov	x20, #0x2000
	mov	x21, #0x2100
	mov	x22, #0x2200
	mov	x23, #0x2300
	mov	x24, #0x2400
	mov	x25, #0x2500
	mov	x26, #0x2600
	mov	x27, #0x2700
	mov	x28, #0x2800
	mov	x29, #0x2900
	adrp	x1, saved_sp
	mov	x2, sp
	str	x2, [x1, :lo12:saved_sp]
	bl	test_main
	mov	w0, w0
	adrp	x1, saved_sp
	ldr	x2, [x1, :lo12:saved_sp]
	mov	x3, sp
	cmp	x2, x3
	b.ne	1f
	mov	x1, #19
	mov	x2, #0x1900
	cmp	x19, x2
	b.ne	2f
	mov	x1, #20
	mov	x2, #0x2000
	cmp	x20, x2
	b.ne	2f
	mov	x1, #21
	mov	x2, #0x2100
	cmp	x21, x2
	b.ne	2f
	mov	x1, #22
	mov	x2, #0x2200
	cmp	x22, x2
	b.ne	2f
	mov	x1, #23
	mov	x2, #0x2300
	cmp	x23, x2
	b.ne	2f
	mov	x1, #24
	mov	x2, #0x2400
	cmp	x24, x2
	b.ne	2f
	mov	x1, #25
	mov	x2, #0x2500
	cmp	x25, x2
	b.ne	2f
	mov	x1, #26
	mov	x2, #0x2600
	cmp	x26, x2
	b.ne	2f
	mov	x1, #27
	mov	x2, #0x2700
	cmp	x27, x2
	b.ne	2f
	mov	x1, #28
	mov	x2, #0x2800
	cmp	x28, x2
	b.ne	2f
	mov	x1, #29
	mov	x2, #0x2900
	cmp	x29, x2
	b.ne	2f
	b	exit
1:	mov	x1, #31
2:	add	x0, x1, #200
	// Semihosting SYS_EXIT with the status in x0, also for compiled code.
	.globl	semihost_exit
	.type	semihost_exit, %function
semihost_exit:
exit:
	adrp	x1, exit_block
	add	x1, x1, :lo12:exit_block
	str	x0, [x1, #8]
	mov	w0, #0x18
	hlt	#0xf000
3:	b	3b

	.data
	.p2align	3
exit_block:
	.xword	0x20026, 0
saved_sp:
	.xword	0
