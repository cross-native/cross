// R_AARCH64_ABS64, ABS32, ABS16, PREL64, and PREL32 against global, local,
// section, absolute, and undefined weak symbols.
	.text
	.globl	_start
	.type	_start, %function
_start:
	ret

	.data
	.p2align	3
	.globl	global_data
	.weak	undefined_weak
global_data:
	.xword	global_data
	.xword	local_data + 8
	.xword	_start
	.xword	other_function + 4
	.xword	wide_abs
	.xword	undefined_weak
	.xword	undefined_weak + 16
	.word	far_words
	.word	local_data - 4
	.word	small_abs
	.word	undefined_weak
	.hword	abs16
	.hword	undefined_weak + 2
	.p2align	3
	.xword	far_data - .
	.xword	_start - .
	.word	local_data - .
	.word	other_function - .
	.word	undefined_weak - .
local_data:
	.xword	7, 8
