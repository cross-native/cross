# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Both trampolines use the SysV convention. They load every register the
# callee's ABI preserves with a canary, call the function, and return a
# poisoned result if a canary changed.

        .text
        .globl sw_canary_call
# u64 sw_canary_call(function, a, b, c) calls a SysV function(a, b, c).
sw_canary_call:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        subq $8, %rsp
        movq %rdi, %r11
        movq %rsi, %rdi
        movq %rdx, %rsi
        movq %rcx, %rdx
        movabsq $0x0101010101010101, %rbx
        movabsq $0x0202020202020202, %rbp
        movabsq $0x0303030303030303, %r12
        movabsq $0x0404040404040404, %r13
        movabsq $0x0505050505050505, %r14
        movabsq $0x0606060606060606, %r15
        xorl %eax, %eax
        callq *%r11
        movabsq $0x0101010101010101, %rcx
        cmpq %rcx, %rbx
        jne .Lsysv_clobbered
        movabsq $0x0202020202020202, %rcx
        cmpq %rcx, %rbp
        jne .Lsysv_clobbered
        movabsq $0x0303030303030303, %rcx
        cmpq %rcx, %r12
        jne .Lsysv_clobbered
        movabsq $0x0404040404040404, %rcx
        cmpq %rcx, %r13
        jne .Lsysv_clobbered
        movabsq $0x0505050505050505, %rcx
        cmpq %rcx, %r14
        jne .Lsysv_clobbered
        movabsq $0x0606060606060606, %rcx
        cmpq %rcx, %r15
        je .Lsysv_done
.Lsysv_clobbered:
        movabsq $0x0badc0de0badc0de, %rax
.Lsysv_done:
        addq $8, %rsp
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        retq

        .globl sw_canary_call_ms
# f64 sw_canary_call_ms(function, x, n) calls a Microsoft x64 function(x, n).
sw_canary_call_ms:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        subq $40, %rsp
        movq %rdi, %r11
        movq %rsi, %rdx
        movabsq $0x1111111111111111, %rbx
        movabsq $0x2222222222222222, %rbp
        movabsq $0x3333333333333333, %rdi
        movabsq $0x4444444444444444, %rsi
        movabsq $0x5555555555555555, %r12
        movabsq $0x6666666666666666, %r13
        movabsq $0x7777777777777777, %r14
        movabsq $0x1212121212121212, %r15
        movabsq $0x1313131313131313, %rax
        movq %rax, %xmm6
        movabsq $0x1414141414141414, %rax
        movq %rax, %xmm7
        movabsq $0x1515151515151515, %rax
        movq %rax, %xmm15
        callq *%r11
        movabsq $0x1111111111111111, %rcx
        cmpq %rcx, %rbx
        jne .Lms_clobbered
        movabsq $0x2222222222222222, %rcx
        cmpq %rcx, %rbp
        jne .Lms_clobbered
        movabsq $0x3333333333333333, %rcx
        cmpq %rcx, %rdi
        jne .Lms_clobbered
        movabsq $0x4444444444444444, %rcx
        cmpq %rcx, %rsi
        jne .Lms_clobbered
        movabsq $0x5555555555555555, %rcx
        cmpq %rcx, %r12
        jne .Lms_clobbered
        movabsq $0x6666666666666666, %rcx
        cmpq %rcx, %r13
        jne .Lms_clobbered
        movabsq $0x7777777777777777, %rcx
        cmpq %rcx, %r14
        jne .Lms_clobbered
        movabsq $0x1212121212121212, %rcx
        cmpq %rcx, %r15
        jne .Lms_clobbered
        movq %xmm6, %rax
        movabsq $0x1313131313131313, %rcx
        cmpq %rcx, %rax
        jne .Lms_clobbered
        movq %xmm7, %rax
        movabsq $0x1414141414141414, %rcx
        cmpq %rcx, %rax
        jne .Lms_clobbered
        movq %xmm15, %rax
        movabsq $0x1515151515151515, %rcx
        cmpq %rcx, %rax
        je .Lms_done
.Lms_clobbered:
        movabsq $0x7ff8badc0de0bad0, %rax
        movq %rax, %xmm0
.Lms_done:
        addq $40, %rsp
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        retq
