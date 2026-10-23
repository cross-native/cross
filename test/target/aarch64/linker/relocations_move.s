// R_AARCH64_MOVW_UABS_G0 to G3 with the _NC forms, and MOVW_SABS_G0 to G2,
// which choose movz or movn by the sign.
	.text
	.globl	_start
	.type	_start, %function
_start:
	movz	x0, #:abs_g0:small_abs
	movk	x0, #:abs_g0_nc:far_data
	movz	x1, #:abs_g1:far_data
	movk	x1, #:abs_g1_nc:wide_abs
	movz	x2, #:abs_g2:far_data
	movk	x2, #:abs_g2_nc:wide_abs
	movz	x3, #:abs_g3:wide_abs
	movk	x3, #:abs_g3:far_data
	movz	w4, #:abs_g0:abs16
	movz	x5, #:abs_g0_s:small_abs
	movz	x5, #:abs_g0_s:small_neg
	movn	x6, #:abs_g1_s:mid_neg
	movz	x6, #:abs_g1_s:far_data
	movz	x7, #:abs_g2_s:big_neg
	movn	x7, #:abs_g2_s:far_data
	ret
