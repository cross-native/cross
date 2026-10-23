// The relocation operators that GNU as takes and llvm-mc does not.
	.text
	adrp	x0, :pg_hi21:external_data + 0x1000
	adrp	x1, :tlsgd:external_tls
	add	x1, x1, :tlsgd_lo12:external_tls
	bl	__tls_get_addr
