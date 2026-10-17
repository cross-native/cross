# Relocation forms per operand: PLT32 calls, RIP-relative data and GOT loads,
# absolute data, TLS, and symbol differences.
	.text
	.globl relocs
relocs:
	call callee@PLT
	movq gotvar@GOTPCREL(%rip), %rax
	leaq localsym(%rip), %rcx
	movl external(%rip), %edx
	movq tlsvar@GOTTPOFF(%rip), %r8
	leaq tlsle@TPOFF(%rax), %r9
	jmp localsym
	retq
localsym:
	retq
	.data
	.globl datarefs
datarefs:
	.quad external
	.long localsym - datarefs
	.quad 0
	.section .tdata,"awT",@progbits
tlsle:	.long 0
