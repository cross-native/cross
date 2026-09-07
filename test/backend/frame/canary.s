# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

.text
.globl checked_frame
checked_frame:
    pushq %rbx
    pushq %rbp
    pushq %rdi
    pushq %rsi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $88, %rsp
    movdqu %xmm6, 64(%rsp)
    movq 192(%rsp), %rax
    movq %rax, 32(%rsp)
    movq $101, %rbx
    movq $102, %rbp
    movq $103, %rdi
    movq $104, %rsi
    movq $105, %r12
    movq $106, %r13
    movq $107, %r14
    movq $108, %r15
    movq $109, %rax
    movq %rax, %xmm6
    call frame_roundtrip
    cmpq $101, %rbx
    jne .Lbad_frame
    cmpq $102, %rbp
    jne .Lbad_frame
    cmpq $103, %rdi
    jne .Lbad_frame
    cmpq $104, %rsi
    jne .Lbad_frame
    cmpq $105, %r12
    jne .Lbad_frame
    cmpq $106, %r13
    jne .Lbad_frame
    cmpq $107, %r14
    jne .Lbad_frame
    cmpq $108, %r15
    jne .Lbad_frame
    movq %xmm6, %r10
    cmpq $109, %r10
    je .Lframe_done
.Lbad_frame:
    notq %rax
.Lframe_done:
    movdqu 64(%rsp), %xmm6
    addq $88, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rsi
    popq %rdi
    popq %rbp
    popq %rbx
    ret
