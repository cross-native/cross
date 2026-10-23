// Relocations of AArch64 ELF objects: every operator and field that the
// table's operand types take, against undefined, global, weak, local, and
// TLS symbols, in the same section and in others, and the data relocations;
// the operators that GNU as and llvm-mc both take.
	.text
	.globl	start
	.globl	global_function
	.weak	weak_function
	.type	start, %function
start:
	// Calls and branches: local targets in the section resolve in place.
	bl	external_function
	bl	global_function
	bl	weak_function
	bl	local_function
	bl	other_section_function
	b	external_function
	b	local_function
	b.ne	external_function
	b.lt	local_function
	cbz	x0, external_function
	cbnz	w1, local_function
	tbz	x2, #40, external_function
	tbnz	w3, #3, local_function
	// PC-relative addresses and literals.
	adr	x4, external_data
	adr	x5, local_data
	adr	x6, local_data + 12
	adrp	x7, external_data
	adrp	x8, local_data
	adrp	x9, :pg_hi21_nc:external_data
	ldr	x10, external_literal
	ldr	w11, local_literal
	ldrsw	x12, local_literal
	ldr	d13, external_literal
	prfm	pldl1keep, local_literal
	// Low 12 bits: add and every access size.
	add	x14, x7, :lo12:external_data
	add	w15, w7, #:lo12:external_data
	add	x16, x8, :lo12:local_data + 8
	ldrb	w17, [x7, :lo12:external_data]
	strh	w17, [x7, #:lo12:external_data]
	ldr	w17, [x7, :lo12:external_data]
	ldr	x17, [x7, :lo12:external_data + 16]
	str	q17, [x7, :lo12:external_data]
	ldr	s17, [x8, :lo12:local_data]
	ldrsb	x17, [x8, :lo12:local_data]
	prfm	pstl1strm, [x8, :lo12:local_data]
	// The GOT.
	adrp	x18, :got:external_data
	ldr	x18, [x18, :got_lo12:external_data]
	adrp	x19, :got:local_data
	ldr	x19, [x19, :got_lo12:local_data]
	// Absolute moves.
	movz	x20, #:abs_g3:external_data
	movk	x20, #:abs_g2_nc:external_data
	movk	x20, #:abs_g1_nc:external_data
	movk	x20, #:abs_g0_nc:external_data
	movz	x21, #:abs_g2:external_data
	movz	x21, #:abs_g1:external_data
	movz	w21, #:abs_g0:external_data
	movn	x22, #:abs_g2_s:external_data
	movz	x22, #:abs_g1_s:external_data
	movz	x22, #:abs_g0_s:external_data
	// TLS: initial exec, local exec through moves and adds, and
	// descriptors and general dynamic.
	mrs	x23, tpidr_el0
	adrp	x24, :gottprel:external_tls
	ldr	x24, [x24, #:gottprel_lo12:external_tls]
	movz	x25, #:gottprel_g1:external_tls
	movk	x25, #:gottprel_g0_nc:external_tls
	add	x26, x23, #:tprel_hi12:local_tls, lsl #12
	add	x26, x26, #:tprel_lo12_nc:local_tls
	add	x27, x23, :tprel_lo12:local_tls
	ldr	x27, [x23, :tprel_lo12_nc:local_tls]
	ldrb	w27, [x23, :tprel_lo12:local_tls]
	movz	x28, #:tprel_g2:external_tls
	movk	x28, #:tprel_g1_nc:external_tls
	movk	x28, #:tprel_g0_nc:external_tls
	movz	x28, #:tprel_g1:external_tls
	movz	x28, #:tprel_g0:external_tls
	adrp	x0, :tlsdesc:external_tls
	ldr	x1, [x0, :tlsdesc_lo12:external_tls]
	add	x0, x0, :tlsdesc_lo12:external_tls
	add	x0, x0, #:dtprel_hi12:local_tls, lsl #12
	add	x0, x0, :dtprel_lo12_nc:local_tls
	movz	x0, #:dtprel_g1:local_tls
	movk	x0, #:dtprel_g0_nc:local_tls
	ldr	x0, [x0, :dtprel_lo12:local_tls]
	// Values that only layout settles.
	add	x2, x2, #(local_function - start)
	ldr	x2, [x2, #(local_literal - local_function)]
global_function:
local_function:
	ret
weak_function:
	ret
	.p2align	3
local_literal:
	.word	0x12345678, 0
	.size	start, . - start

	.section .text.other,"ax",@progbits
other_section_function:
	ret

	.data
	.p2align	3
local_data:
	.xword	external_data
	.xword	external_data + 8
	.xword	local_data
	.xword	start
	.xword	global_function
	.word	external_data
	.word	local_data + 4
	.hword	external_data
	.word	external_data - .
	.xword	external_data - .
	.word	local_function - .
	.xword	start - local_data

	.section .tbss,"awT",@nobits
	.p2align	3
local_tls:
	.zero	16
