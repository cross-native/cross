// R_AARCH64_CALL26, JUMP26, CONDBR19, TSTBR14, and LD_PREL_LO19, forward,
// backward, and to another section.
	.text
	.globl	_start
	.type	_start, %function
_start:
	bl	target_a
	b	target_a
	b.eq	target_a
	b.lt	target_b
	cbz	x0, target_a
	cbnz	w1, target_b
	tbz	x2, #3, target_b
	tbnz	w3, #31, target_a
	ldr	x4, literal
	ldr	w5, literal + 4
	ldrsw	x6, literal
	ldr	d7, literal
	prfm	pldl1keep, literal
	bl	other_function
	b	other_function
	ret

	.section .text.targets,"ax",@progbits
	.globl	target_a, target_b
	.type	target_a, %function
target_a:
	bl	_start
	b	_start
	b.ne	_start
	cbz	x0, _start
	tbnz	x0, #0, _start
	ldr	x1, literal
	ret
	.type	target_b, %function
target_b:
	ret

	.section .rodata,"a",@progbits
	.p2align	3
	.globl	literal
literal:
	.xword	0x1122334455667788
