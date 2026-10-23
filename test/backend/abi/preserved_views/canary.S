# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

.text
.globl check_ms
check_ms:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $40, %rsp
    vmovdqu .Lcanary(%rip), %ymm6
    vmovdqu .Lcanary(%rip), %ymm7
    vmovdqu .Lcanary(%rip), %ymm8
    vmovdqu .Lcanary(%rip), %ymm9
    vmovdqu .Lcanary(%rip), %ymm10
    vmovdqu .Lcanary(%rip), %ymm11
    vmovdqu .Lcanary(%rip), %ymm12
    vmovdqu .Lcanary(%rip), %ymm13
    vmovdqu .Lcanary(%rip), %ymm14
    vmovdqu .Lcanary(%rip), %ymm15
    movl $0x2468ace0, %eax
    call *%rdi
    vpcmpeqb .Lcanary(%rip), %xmm6, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm7, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm8, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm9, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm10, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm11, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm12, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm13, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm14, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    vpcmpeqb .Lcanary(%rip), %xmm15, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_ms
    movl $1, %eax
    jmp .Ldone_check_ms
.Lbad_check_ms:
    xorl %eax, %eax
.Ldone_check_ms:
    vzeroupper
    addq $40, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.globl check_custom
check_custom:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $40, %rsp
    vmovdqu .Lcanary(%rip), %ymm3
    vmovdqu .Lcanary(%rip), %ymm10
    vmovdqu .Lcanary(%rip), %ymm11
    movl $0x2468ace0, %eax
    call *%rdi
    cmpl $0x2468ace0, %eax
    jne .Lbad_check_custom
    vpcmpeqb .Lcanary(%rip), %xmm3, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_custom
    vpcmpeqb .Lcanary(%rip), %xmm10, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_custom
    vpcmpeqb .Lcanary(%rip), %ymm11, %ymm0
    vpmovmskb %ymm0, %eax
    cmpl $-1, %eax
    jne .Lbad_check_custom
    movl $1, %eax
    jmp .Ldone_check_custom
.Lbad_check_custom:
    xorl %eax, %eax
.Ldone_check_custom:
    vzeroupper
    addq $40, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.globl check_strong
check_strong:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $40, %rsp
    vmovdqu .Lcanary(%rip), %ymm3
    vmovdqu .Lcanary(%rip), %ymm10
    vmovdqu .Lcanary(%rip), %ymm11
    movl $0x2468ace0, %eax
    call *%rdi
    cmpl $0x2468ace0, %eax
    jne .Lbad_check_strong
    vpcmpeqb .Lcanary(%rip), %ymm3, %ymm0
    vpmovmskb %ymm0, %eax
    cmpl $-1, %eax
    jne .Lbad_check_strong
    vpcmpeqb .Lcanary(%rip), %xmm10, %xmm0
    vpmovmskb %xmm0, %eax
    cmpl $65535, %eax
    jne .Lbad_check_strong
    vpcmpeqb .Lcanary(%rip), %ymm11, %ymm0
    vpmovmskb %ymm0, %eax
    cmpl $-1, %eax
    jne .Lbad_check_strong
    movl $1, %eax
    jmp .Ldone_check_strong
.Lbad_check_strong:
    xorl %eax, %eax
.Ldone_check_strong:
    vzeroupper
    addq $40, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.section .rodata
.p2align 5
.Lcanary:
    .quad 0x0123456789abcdef, 0xfedcba9876543210
    .quad 0x1122334455667788, 0x8877665544332211
.section .note.GNU-stack,"",@progbits
