// The local-exec TLS relocations without overflow checks of their low 12
// bits, and initial exec, which relaxes to local exec, with a 32-byte
// aligned TLS block.
	.text
	.globl	_start
	.type	_start, %function
_start:
	mrs	x0, tpidr_el0
	add	x1, x0, #:tprel_hi12:tls_a, lsl #12
	add	x1, x1, #:tprel_lo12_nc:tls_a
	add	x2, x0, :tprel_lo12_nc:tls_b
	strb	w3, [x0, :tprel_lo12_nc:tls_byte]
	ldrh	w3, [x0, :tprel_lo12_nc:tls_half]
	str	w3, [x0, :tprel_lo12_nc:tls_word]
	ldr	x3, [x0, :tprel_lo12_nc:tls_zero]
	movz	x4, #:tprel_g2:tls_zero
	movk	x4, #:tprel_g1_nc:tls_zero
	movk	x4, #:tprel_g0_nc:tls_zero
	movz	x5, #:tprel_g1:tls_b
	movk	x5, #:tprel_g0_nc:tls_b
	movz	x6, #:tprel_g0:tls_a
	adrp	x7, :gottprel:tls_other
	ldr	x7, [x7, :gottprel_lo12:tls_other]
	adrp	x8, :gottprel:tls_zero
	ldr	x8, [x8, :gottprel_lo12:tls_zero]
	ret

	.section .tdata,"awT",@progbits
	.p2align	5
	.globl	tls_a
tls_a:
	.xword	1
	.globl	tls_b
tls_b:
	.xword	2
	.globl	tls_byte
tls_byte:
	.byte	3
	.p2align	1
	.globl	tls_half
tls_half:
	.hword	4
	.p2align	2
	.globl	tls_word
tls_word:
	.word	5
	.p2align	4
	.globl	tls_quad
tls_quad:
	.xword	6, 7

	.section .tbss,"awT",@nobits
	.p2align	3
	.globl	tls_other
	.globl	tls_zero
tls_zero:
	.zero	16
tls_other:
	.zero	8
