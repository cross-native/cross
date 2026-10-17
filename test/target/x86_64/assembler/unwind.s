# Win64 SEH unwind directives produce .pdata and .xdata that must match
# llvm-mc's byte-for-byte.
	.text
	.globl seh_func
	.def seh_func; .scl 2; .type 32; .endef
	.seh_proc seh_func
seh_func:
	pushq %rbp
	.seh_pushreg %rbp
	pushq %rbx
	.seh_pushreg %rbx
	subq $64, %rsp
	.seh_stackalloc 64
	leaq 64(%rsp), %rbp
	.seh_setframe %rbp, 64
	movaps %xmm6, 32(%rsp)
	.seh_savexmm %xmm6, 32
	movq %rsi, 24(%rsp)
	.seh_savereg %rsi, 24
	.seh_endprologue
	xorl %eax, %eax
	addq $64, %rsp
	popq %rbx
	popq %rbp
	retq
	.seh_endproc
