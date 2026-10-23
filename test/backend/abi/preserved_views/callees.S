# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

.text
.globl upper_clobber
upper_clobber:
    vxorps %xmm0, %xmm0, %xmm0
    vinsertf128 $1, %xmm0, %ymm6, %ymm6
#ifdef WIDE512
    # An EVEX write also clears bits 256-511 of ZMM6.
    vinserti64x4 $1, %ymm0, %zmm6, %zmm6
#endif
    xorl %eax, %eax
    ret

.globl explicit_clobber
explicit_clobber:
    vxorps %xmm3, %xmm3, %xmm3
    jmp custom_clobber

.globl manual_clobber
manual_clobber:
    # The caller has already overwritten XMM3 with the manual argument.
    jmp custom_clobber

.globl custom_clobber
custom_clobber:
    vxorps %xmm0, %xmm0, %xmm0
    vinsertf128 $1, %xmm0, %ymm3, %ymm3
    vinsertf128 $1, %xmm0, %ymm10, %ymm10
    vxorps %xmm1, %xmm1, %xmm1
    vxorps %xmm2, %xmm2, %xmm2
    vxorps %xmm4, %xmm4, %xmm4
    vxorps %xmm5, %xmm5, %xmm5
    vxorps %xmm6, %xmm6, %xmm6
    vxorps %xmm7, %xmm7, %xmm7
    vxorps %xmm8, %xmm8, %xmm8
    vxorps %xmm9, %xmm9, %xmm9
    vxorps %xmm12, %xmm12, %xmm12
    vxorps %xmm13, %xmm13, %xmm13
    vxorps %xmm14, %xmm14, %xmm14
    vxorps %xmm15, %xmm15, %xmm15
    movabsq $0xdeadbeef00000000, %r11
    movl %eax, %eax
    orq %r11, %rax
    xorl %ebx, %ebx
    xorl %ecx, %ecx
    xorl %edx, %edx
    xorl %esi, %esi
    xorl %edi, %edi
    xorl %r8d, %r8d
    xorl %r9d, %r9d
    xorl %r10d, %r10d
    xorl %r11d, %r11d
    xorl %r12d, %r12d
    xorl %r13d, %r13d
    xorl %r14d, %r14d
    xorl %r15d, %r15d
    ret
.section .note.GNU-stack,"",@progbits
