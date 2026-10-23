// R_AARCH64_TLSLE_LDST128_TPREL_LO12, which no reference linker takes.
	.section .extra.checked,"ax",@progbits
	.globl	tls128_checked
	.type	tls128_checked, %function
tls128_checked:
	ldr	q3, [x0, :tprel_lo12:tls_quad]
	str	q4, [x1, :tprel_lo12:tls_quad]
	ret
