// R_AARCH64_PREL16, which only llvm-mc and GNU as emit for 2-byte
// differences with a symbol of another object.
	.section .data.prel16,"aw",@progbits
	.p2align	1
	.hword	far_bytes - .
	.hword	far_halves + 4 - .
