# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# System V entry: u64 msabi_canary(void *fn, u64 arg, u64 *result)
# Calls fn(arg) under the Win64 convention with canaries in every Win64
# nonvolatile register: RBX, RBP, RDI, RSI, R12-R15 and the full 128 bits of
# XMM6-XMM15 (the upper YMM halves get other values, which the callee may
# change). Returns a bit mask of registers that did not survive: bits 0-7 for
# RBX, RBP, RDI, RSI, R12, R13, R14, R15, and bits 8-17 for XMM6-XMM15.
# Stores fn's RAX result to *result. Also checks RSP is restored.

.text
.globl msabi_canary
msabi_canary:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $56, %rsp                  # 16-byte aligned; 32-byte home area at 0(%rsp)
    movq %rdx, 40(%rsp)             # result pointer
    movq %rsp, 48(%rsp)             # expected RSP after the call
    movq %rdi, %rax
    movq %rsi, %rcx
    movq %rsi, %r10                 # manual probes take the argument in R10
    vmovdqu canary_x+0(%rip), %ymm6
    vmovdqu canary_x+32(%rip), %ymm7
    vmovdqu canary_x+64(%rip), %ymm8
    vmovdqu canary_x+96(%rip), %ymm9
    vmovdqu canary_x+128(%rip), %ymm10
    vmovdqu canary_x+160(%rip), %ymm11
    vmovdqu canary_x+192(%rip), %ymm12
    vmovdqu canary_x+224(%rip), %ymm13
    vmovdqu canary_x+256(%rip), %ymm14
    vmovdqu canary_x+288(%rip), %ymm15
    movabsq $0x1111111111111101, %rbx
    movabsq $0x2222222222222202, %rbp
    movabsq $0x3333333333333303, %rdi
    movabsq $0x4444444444444404, %rsi
    movabsq $0x5555555555555505, %r12
    movabsq $0x6666666666666606, %r13
    movabsq $0x7777777777777707, %r14
    movabsq $0x0888888888888808, %r15
    call *%rax
    movq %rax, %rdx                 # fn result
    xorl %eax, %eax
    cmpq 48(%rsp), %rsp
    je 1f
    ud2
1:
    movabsq $0x1111111111111101, %rcx
    cmpq %rcx, %rbx
    je 2f
    orl $1, %eax
2:  movabsq $0x2222222222222202, %rcx
    cmpq %rcx, %rbp
    je 2f
    orl $2, %eax
2:  movabsq $0x3333333333333303, %rcx
    cmpq %rcx, %rdi
    je 2f
    orl $4, %eax
2:  movabsq $0x4444444444444404, %rcx
    cmpq %rcx, %rsi
    je 2f
    orl $8, %eax
2:  movabsq $0x5555555555555505, %rcx
    cmpq %rcx, %r12
    je 2f
    orl $16, %eax
2:  movabsq $0x6666666666666606, %rcx
    cmpq %rcx, %r13
    je 2f
    orl $32, %eax
2:  movabsq $0x7777777777777707, %rcx
    cmpq %rcx, %r14
    je 2f
    orl $64, %eax
2:  movabsq $0x0888888888888808, %rcx
    cmpq %rcx, %r15
    je 2f
    orl $128, %eax
2:
.irp n,6,7,8,9,10,11,12,13,14,15
    vpcmpeqb canary_x+(\n-6)*32(%rip), %xmm\n, %xmm0
    vpmovmskb %xmm0, %ecx
    cmpl $0xffff, %ecx
    je 3f
    orl $(1 << (\n + 2)), %eax
3:
.endr
    movq 40(%rsp), %rcx
    movq %rdx, (%rcx)
    vzeroupper
    addq $56, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    ret

.section .rodata
.p2align 5
canary_x:
.irp n,6,7,8,9,10,11,12,13,14,15
    .quad 0x0123456789ab0000+\n, 0xfedcba98765400f0+\n
    .quad 0xa5a5a5a5a5a5a5a5, 0x5a5a5a5a5a5a5a5a
.endr
.section .note.GNU-stack,"",@progbits
