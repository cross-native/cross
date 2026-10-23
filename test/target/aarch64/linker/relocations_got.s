// R_AARCH64_ADR_GOT_PAGE and LD64_GOT_LO12_NC against global, local,
// absolute, and undefined weak symbols; uses of a symbol share its entry.
	.text
	.globl	_start
	.type	_start, %function
	.weak	undefined_weak
_start:
	adrp	x0, :got:far_data
	ldr	x0, [x0, :got_lo12:far_data]
	adrp	x1, :got:local_data
	ldr	x1, [x1, :got_lo12:local_data]
	adrp	x2, :got:wide_abs
	ldr	x2, [x2, :got_lo12:wide_abs]
	adrp	x3, :got:undefined_weak
	ldr	x3, [x3, :got_lo12:undefined_weak]
	adrp	x4, :got:other_function
	ldr	x4, [x4, :got_lo12:other_function]
	adrp	x5, :got:far_data
	ldr	x5, [x5, :got_lo12:far_data]
	ret

	.section .data.local,"aw",@progbits
	.p2align	3
local_data:
	.xword	_GLOBAL_OFFSET_TABLE_
