# Integer, SSE, AVX, AVX-512, x87, and BMI encodings the x86-64 backend
# selects. The differential test checks these assemble identically to llvm-mc.
	.text
	.globl encodings
encodings:
	addl $5, %eax
	addq $-1, %rbx
	addw $0xffff, %cx
	movl $0xdeadbeef, %r9d
	movabsq $0x1122334455667788, %r11
	movq %rax, %rbx
	leaq 8(%rsp,%rcx,4), %rdx
	leaq sym(%rip), %rsi
	imull $300, %eax, %ecx
	imulq %rbx, %rcx
	shll %eax
	shlq $5, %rbx
	xchgl %eax, %ecx
	xchgq %r8, %r10
	movsbl %al, %eax
	movzwq %bx, %rcx
	incq (%rax)
	negl %r8d
	testb $1, %dil
	cmpq $0, (%rbx,%rdx,8)
	bsfq %rax, %rbx
	popcntq %rax, %rbx
	cmoveq %rax, %rbx
	setne %al
	movaps %xmm8, %xmm0
	movups (%rdi), %xmm3
	addsd %xmm1, %xmm2
	mulss %xmm4, %xmm5
	cvtsi2sdq %rax, %xmm0
	cvttsd2si %xmm0, %eax
	pxor %xmm1, %xmm1
	pshufd $0x1b, %xmm2, %xmm3
	movq %rax, %xmm0
	movd %xmm0, %eax
	vaddps %ymm1, %ymm0, %ymm3
	vaddps %xmm8, %xmm7, %xmm6
	vmovaps %xmm8, %xmm0
	vmulsd %xmm1, %xmm2, %xmm3
	vxorps %xmm8, %xmm8, %xmm8
	vpaddd %ymm1, %ymm0, %ymm2
	vfmadd213ps %ymm3, %ymm2, %ymm1
	vbroadcastss %xmm0, %ymm1
	vextractf128 $1, %ymm3, %xmm2
	vcmpps $1, %ymm5, %ymm4, %ymm6
	vaddps %zmm1, %zmm3, %zmm2
	vaddps (%rax){1to16}, %zmm4, %zmm3 {%k1} {z}
	vaddps {rn-sae}, %zmm5, %zmm4, %zmm3 {%k1} {z}
	vmovdqu64 64(%rsp), %zmm0
	vpbroadcastd %eax, %zmm0 {%k1} {z}
	vextractf32x4 $3, %zmm0, %xmm2
	vpcmpd $1, %zmm1, %zmm2, %k1
	kmovw %k1, %eax
	kshiftlw $3, %k2, %k1
	andnq %rax, %rbx, %rcx
	shlxq %rax, %rbx, %rcx
	rorxq $7, %rax, %rbx
	bzhiq %rax, %rbx, %rcx
	fldl (%rsp)
	faddp %st, %st(1)
	fmul %st(2), %st
	fstpt (%rsp)
	fcomip %st(1), %st
	fnstcw (%rax)
	retq
	.data
sym:	.quad 0
