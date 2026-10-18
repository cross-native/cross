# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# n64 startup for abicalls and thread-local code, as abicalls_start.s is
# for o32: the thread's TLS block is a copy of the template that
# abicalls-n64.ld delimits, the thread pointer lies 0x7000 past it in
# UserLocal and in thread_pointer, from which an exception handler
# emulates `rdhwr $3,$29` on CPUs before Release 2. Three rounds of
# runtime_check, called through $25, must return 1 and preserve $gp, as
# n64 functions do; the failure code is the round's digit, and E reports
# an unexpected exception. scramble_gp changes $gp and restores it before
# returning its argument. GPRs are numeric, as in n64_runtime.s.

        .set    noreorder
        .set    noat
        .set    mips64

        .section .text.boot,"ax",@progbits
        .globl  _start
        .ent    _start
_start:
        lui     $29,0x8080
        daddiu  $29,$29,-64

        # Enable CU1 and FR for the n64 FPU, and take exceptions in kernel
        # mode at the RAM vector: clear Status.BEV, ERL, EXL, and IE.
        mfc0    $12,$12
        lui     $13,0x2400
        or      $12,$12,$13
        lui     $13,0xffbf
        ori     $13,$13,0xfff8
        and     $12,$12,$13
        mtc0    $12,$12
        nop
        nop

        # The handler, copied to the general exception vector.
        lui     $12,%hi(exception)
        daddiu  $12,$12,%lo(exception)
        lui     $13,%hi(exception_end)
        daddiu  $13,$13,%lo(exception_end)
        lui     $14,0x8000
        ori     $14,$14,0x0180
.Lcopy_handler:
        lw      $15,0($12)
        daddiu  $12,$12,4
        sw      $15,0($14)
        bne     $12,$13,.Lcopy_handler
        daddiu  $14,$14,4

        # The TLS block: the bytes of .tdata, then zeros for .tbss at the
        # same offset from the block as from the template.
        lui     $12,%hi(__tls_data)
        daddiu  $12,$12,%lo(__tls_data)
        lui     $13,%hi(__tls_data_size)
        daddiu  $13,$13,%lo(__tls_data_size)
        daddu   $13,$12,$13
        lui     $14,%hi(tls_block)
        daddiu  $14,$14,%lo(tls_block)
        move    $15,$14
.Lcopy_data:
        beq     $12,$13,.Lzero_start
        nop
        lbu     $24,0($12)
        daddiu  $12,$12,1
        sb      $24,0($15)
        b       .Lcopy_data
        daddiu  $15,$15,1
.Lzero_start:
        lui     $12,%hi(__tls_zero)
        daddiu  $12,$12,%lo(__tls_zero)
        lui     $13,%hi(__tls_data)
        daddiu  $13,$13,%lo(__tls_data)
        dsubu   $12,$12,$13
        daddu   $15,$14,$12
        lui     $13,%hi(__tls_zero_size)
        daddiu  $13,$13,%lo(__tls_zero_size)
        daddu   $13,$15,$13
        dsubu   $24,$13,$14
        sltiu   $24,$24,257
        beq     $24,$0,.Lunexpected_exception
        nop
.Lzero:
        beq     $15,$13,.Lthread_pointer
        nop
        sb      $0,0($15)
        b       .Lzero
        daddiu  $15,$15,1
.Lthread_pointer:
        daddiu  $12,$14,0x7000
        lui     $13,%hi(thread_pointer)
        sd      $12,%lo(thread_pointer)($13)
        # Only MIPS32 and MIPS64 CPUs, whose PRId names a company, have
        # selectable CP0 registers such as UserLocal.
        mfc0    $13,$15
        lui     $15,0x00ff
        and     $13,$13,$15
        beq     $13,$0,.Lrounds
        nop
        dmtc0   $12,$4,2
.Lrounds:
        move    $16,$0
        lui     $28,0xa5a5
.Lround:
        lui     $25,%hi(runtime_check)
        daddiu  $25,$25,%lo(runtime_check)
        jalr    $25
        move    $4,$16
        lui     $12,0xa5a5
        bne     $28,$12,.Lfail
        daddiu  $17,$16,'0'
        daddiu  $12,$0,1
        bne     $2,$12,.Lfail
        nop
        daddiu  $16,$16,1
        sltiu   $12,$16,3
        bne     $12,$0,.Lround
        nop

        daddiu  $4,$0,'P'
        b       .Lreport
        nop
.Lunexpected_exception:
        daddiu  $17,$0,'E'
.Lfail:
        move    $4,$17
.Lreport:
        xori    $14,$4,'P'
        sltu    $14,$0,$14
        lui     $12,0xbf00
        ori     $12,$12,0x0900
        sb      $4,0($12)
        daddiu  $4,$0,'\n'
        sb      $4,0($12)
        move    $4,$14
        daddiu  $2,$0,1
        daddiu  $25,$0,1
        .word   0x7000007f
.Lhalt:
        b       .Lhalt
        nop

# The exception handler: it emulates `rdhwr $3,$29` outside a delay slot
# and reports any other exception.
exception:
        mfc0    $26,$13
        nop
        bltz    $26,.Lunexpected
        andi    $26,$26,0x7c
        xori    $26,$26,0x28
        bne     $26,$0,.Lunexpected
        nop
        dmfc0   $26,$14
        nop
        lw      $27,0($26)
        lui     $26,0x7c03
        ori     $26,$26,0xe83b
        bne     $26,$27,.Lunexpected
        nop
        lui     $27,%hi(thread_pointer)
        ld      $3,%lo(thread_pointer)($27)
        dmfc0   $26,$14
        nop
        daddiu  $26,$26,4
        dmtc0   $26,$14
        nop
        nop
        eret
.Lunexpected:
        lui     $26,%hi(.Lunexpected_exception)
        daddiu  $26,$26,%lo(.Lunexpected_exception)
        jr      $26
        nop
exception_end:
        .end    _start

        .text
        .globl  scramble_gp
        .ent    scramble_gp
scramble_gp:
        move    $24,$28
        lui     $28,0x5a5a
        move    $2,$4
        jr      $31
        move    $28,$24
        .end    scramble_gp

        .bss
        .align  4
thread_pointer:
        .space  16
tls_block:
        .space  256
