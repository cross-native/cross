# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# u64 clobbers_xmm6(u64 x): Win64 convention, returns x + 1, writes xmm6 as
# its declaration allows.
.text
.globl clobbers_xmm6
clobbers_xmm6:
    leaq 1(%rcx), %rax
    vpcmpeqd %xmm6, %xmm6, %xmm6
    ret
.section .note.GNU-stack,"",@progbits
.text
.globl m_clobbers
m_clobbers:
    leaq 1(%r10), %rax
    vpcmpeqd %xmm6, %xmm6, %xmm6
    ret
.section .note.GNU-stack,"",@progbits

.text
.globl hostile
hostile:
    leaq 1(%rcx), %rax
    vzeroupper
    ret
.globl hostile_sysv
hostile_sysv:
    leaq 1(%rdi), %rax
.irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
    vxorps %xmm\n, %xmm\n, %xmm\n
.endr
    ret
