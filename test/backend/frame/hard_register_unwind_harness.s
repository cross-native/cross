# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# u64 hard_unwind_call(function, count) uses the SysV convention. It loads a
# canary into rbx, rsi, rdi, r12-r15, xmm6, and xmm15 and calls function with
# count in r10 (Cross ABI) and rcx (Win64).

        .text
        .globl hard_unwind_call
hard_unwind_call:
        pushq %rbx
        pushq %rbp
        pushq %r12
        pushq %r13
        pushq %r14
        pushq %r15
        subq $40, %rsp
        movq %rdi, %r11
        movq %rsi, %r10
        movq %rsi, %rcx
        movabsq $0x0101010101010101, %rbx
        movabsq $0x0303030303030303, %r12
        movabsq $0x0404040404040404, %r13
        movabsq $0x0505050505050505, %r14
        movabsq $0x0606060606060606, %r15
        movabsq $0x0707070707070707, %rsi
        movabsq $0x0808080808080808, %rdi
        movabsq $0x1010101010101010, %rax
        movq %rax, %xmm6
        movq %rax, %xmm15
        callq *%r11
        addq $40, %rsp
        popq %r15
        popq %r14
        popq %r13
        popq %r12
        popq %rbp
        popq %rbx
        retq
