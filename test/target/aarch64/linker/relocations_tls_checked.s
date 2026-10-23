// The local-exec TLS relocations that check their low 12 bits, which
// ld.lld 21 does not take.
	.section .text.checked,"ax",@progbits
	.type	checked, %function
checked:
	mrs	x0, tpidr_el0
	add	x2, x0, :tprel_lo12:tls_b
	ldrb	w3, [x0, :tprel_lo12:tls_byte]
	ldrh	w3, [x0, :tprel_lo12:tls_half]
	ldr	w3, [x0, :tprel_lo12:tls_word]
	ldr	x3, [x0, :tprel_lo12:tls_zero]
	ret
