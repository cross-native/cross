# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# cross_canary_call uses the SysV convention. It loads a canary into every
# register the Cross ABI preserves, calls a Cross-ABI function with its first
# three integer arguments in r10, r9, and r8, and returns a poisoned result if
# a canary changed. cross_clobber is a Cross-ABI function that returns
# 2 * value + 1 after changing every other register the ABI lets it change.

        .text
        .globl cross_canary_call
# u64 cross_canary_call(function, a, b, c)
cross_canary_call:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        subq $8, %rsp
        movq %rdi, %r11
        movq %rsi, %r10
        movq %rdx, %r9
        movq %rcx, %r8
        movabsq $0x0101010101010101, %rbx
        movabsq $0x0202020202020202, %rbp
        movabsq $0x0303030303030303, %r12
        movabsq $0x0404040404040404, %r13
        movabsq $0x0505050505050505, %r14
        movabsq $0x0606060606060606, %r15
        callq *%r11
        movabsq $0x0101010101010101, %rcx
        cmpq %rcx, %rbx
        jne .Lclobbered
        movabsq $0x0202020202020202, %rcx
        cmpq %rcx, %rbp
        jne .Lclobbered
        movabsq $0x0303030303030303, %rcx
        cmpq %rcx, %r12
        jne .Lclobbered
        movabsq $0x0404040404040404, %rcx
        cmpq %rcx, %r13
        jne .Lclobbered
        movabsq $0x0505050505050505, %rcx
        cmpq %rcx, %r14
        jne .Lclobbered
        movabsq $0x0606060606060606, %rcx
        cmpq %rcx, %r15
        je .Ldone
.Lclobbered:
        movabsq $0x0badc0de0badc0de, %rax
.Ldone:
        addq $8, %rsp
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        retq

        .globl cross_clobber
# u64 cross_clobber(u64 value), Cross ABI: value in r10, result in rax.
cross_clobber:
        leaq 1(%r10,%r10), %rax
        movabsq $0x5a5a5a5a5a5a5a5a, %rcx
        movq %rcx, %rdx
        movq %rcx, %rsi
        movq %rcx, %rdi
        movq %rcx, %r8
        movq %rcx, %r9
        movq %rcx, %r10
        movq %rcx, %r11
        movq %rcx, %xmm0
        movq %rcx, %xmm1
        movq %rcx, %xmm2
        movq %rcx, %xmm3
        movq %rcx, %xmm4
        movq %rcx, %xmm5
        movq %rcx, %xmm6
        movq %rcx, %xmm7
        movq %rcx, %xmm8
        movq %rcx, %xmm9
        movq %rcx, %xmm10
        movq %rcx, %xmm11
        movq %rcx, %xmm12
        movq %rcx, %xmm13
        movq %rcx, %xmm14
        movq %rcx, %xmm15
        retq
