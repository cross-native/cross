// R_AARCH64_ADR_PREL_LO21, ADR_PREL_PG_HI21 and its _NC form,
// ADD_ABS_LO12_NC, and the LDST8 to LDST128 _ABS_LO12_NC forms.
	.text
	.globl	_start
	.type	_start, %function
_start:
	adr	x0, near_data
	adr	x1, near_data + 3
	adr	x2, other_function
	adrp	x3, far_data
	adrp	x4, far_data + 0x1000
	adrp	x5, :pg_hi21_nc:far_quads
	adrp	x6, near_data
	add	x7, x3, :lo12:far_data
	add	x7, x3, :lo12:far_data + 1
	add	w7, w3, #:lo12:far_quads
	ldrb	w8, [x3, :lo12:far_bytes + 1]
	strb	w8, [x3, :lo12:far_bytes + 3]
	ldrh	w8, [x3, :lo12:far_halves + 2]
	ldrsh	x8, [x3, :lo12:far_halves + 6]
	ldr	w8, [x3, :lo12:far_words + 4]
	ldr	s8, [x3, :lo12:far_words + 8]
	ldr	x8, [x3, :lo12:far_xwords + 8]
	str	d8, [x3, :lo12:far_xwords]
	ldr	q8, [x3, :lo12:far_quads + 16]
	ldr	x8, [x6, :lo12:near_data + 8]
	ret

	.section .rodata,"a",@progbits
	.p2align	3
near_data:
	.xword	1, 2
