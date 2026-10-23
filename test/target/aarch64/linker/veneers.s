// Calls and tail calls between code that a linker script places more than
// 128 MiB apart, in both directions, through range-extension veneers. The
// program exits through semihosting with 0, or with the number of the
// first check that fails.
	.text
	.globl	_start, near_begin, near_end
	.type	_start, %function
_start:
	adrp	x0, stack_top
	add	x0, x0, :lo12:stack_top
	mov	sp, x0
near_begin:
	// 1: BL forward; a second call from another section shares the veneer.
	mov	x19, #1
	bl	far_one
	cmp	x0, #1
	b.ne	fail
	bl	near_helper
	cmp	x0, #1
	b.ne	fail
	// 2: B forward, as a tail call with the return address set.
	mov	x19, #2
	adr	x30, 1f
	b	far_two
1:	cmp	x0, #2
	b.ne	fail
	// 3: BL forward to a local function, through a section symbol with an
	// addend.
	mov	x19, #3
	bl	far_local
	cmp	x0, #3
	b.ne	fail
	// 4: BL backward from far code.
	mov	x19, #4
	bl	far_calls_back
	cmp	x0, #4
	b.ne	fail
	// 5: B backward from far code, a tail call.
	mov	x19, #5
	bl	far_tail_back
	cmp	x0, #5
	b.ne	fail
	mov	x0, #0
	b	exit
fail:
	mov	x0, x19
	b	exit
near_end:

	.type	near_four, %function
near_four:
	mov	x0, #4
	ret
	.globl	near_five
	.type	near_five, %function
near_five:
	mov	x0, #5
	ret

	// Semihosting SYS_EXIT with the code in x0.
	.type	exit, %function
exit:
	adr	x1, exit_block
	str	x0, [x1, #8]
	mov	w0, #0x18
	hlt	#0xf000
1:	b	1b
	.p2align	3
exit_block:
	.xword	0x20026, 0

	.section .text.helper,"ax",@progbits
	.type	near_helper, %function
near_helper:
	b	far_one

	.section .far,"ax",@progbits
	.globl	far_begin, far_end, far_one, far_two, far_calls_back, far_tail_back
far_begin:
	.type	far_one, %function
far_one:
	mov	x0, #1
	ret
	.type	far_two, %function
far_two:
	mov	x0, #2
	ret
	.type	far_local, %function
far_local:
	mov	x0, #3
	ret
	.type	far_calls_back, %function
far_calls_back:
	stp	x29, x30, [sp, #-16]!
	bl	near_four
	ldp	x29, x30, [sp], #16
	ret
	.type	far_tail_back, %function
far_tail_back:
	b	near_five
far_end:
