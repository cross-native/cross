// R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC, which GNU as and GNU ld do not
// take, and the checked form, which ld.lld 21 does not take either: it
// encodes what the _NC form does.
	.section .extra,"ax",@progbits
	.globl	tls128
	.type	tls128, %function
tls128:
	ldr	q3, [x0, :tprel_lo12_nc:tls_quad]
	str	q4, [x1, :tprel_lo12_nc:tls_quad]
	ret
