// Values where GNU ld and ld.lld differ, which Cross ld takes as ld.lld
// and AAELF64 do: a branch to an undefined weak symbol reaches the next
// instruction (GNU ld: address 0), other PC-relative relocations of one
// reach the place, and R_AARCH64_ABS16 and ABS32 accept negative values
// (GNU ld: only unsigned ones).
	.text
	.globl	_start
	.type	_start, %function
	.weak	undefined_weak
_start:
	bl	undefined_weak
	b	undefined_weak
	b.ne	undefined_weak
	cbz	x0, undefined_weak
	tbz	x0, #1, undefined_weak
	ldr	x1, undefined_weak
	adr	x2, undefined_weak
	adrp	x3, undefined_weak
	add	x3, x3, :lo12:undefined_weak
	ldr	x4, [x3, :lo12:undefined_weak]
	movz	x5, #:abs_g1:undefined_weak
	movk	x5, #:abs_g0_nc:undefined_weak
	ret

	.data
	.p2align	2
	.hword	neg16
	.hword	small_neg
	.word	mid_neg
	.word	undefined_weak - 8
