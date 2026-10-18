# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# o32 startup for abicalls and thread-local code. The thread's TLS block is
# a copy of the TLS template that abicalls.ld delimits, and the thread
# pointer lies 0x7000 past its start: in UserLocal, which `rdhwr $3,$29`
# reads on Release 2 CPUs, and in thread_pointer, from which an exception
# handler emulates the instruction on earlier ones, as systems do.
# runtime_check is called through $t9, as abicalls code is, and one digit
# is printed per round. scramble_gp returns its argument with $gp changed.

        .set noreorder
        .set noat
        .set mips3
        .section .text.boot,"ax",@progbits
        .globl _start
_start:
        lui $sp,0x8080
        addiu $sp,$sp,-64
        # Enable CP1 and take exceptions in kernel mode at the RAM vector:
        # clear Status.BEV, ERL, EXL, and IE.
        mfc0 $t0,$12
        lui $t1,0x2000
        or $t0,$t0,$t1
        lui $t1,0xffbf
        ori $t1,$t1,0xfff8
        and $t0,$t0,$t1
        mtc0 $t0,$12
        nop
        nop
        # The handler, copied to the general exception vector.
        lui $t0,%hi(exception)
        addiu $t0,$t0,%lo(exception)
        lui $t1,%hi(exception_end)
        addiu $t1,$t1,%lo(exception_end)
        lui $t2,0x8000
        ori $t2,$t2,0x0180
.Lcopy_handler:
        lw $t3,0($t0)
        addiu $t0,$t0,4
        sw $t3,0($t2)
        bne $t0,$t1,.Lcopy_handler
        addiu $t2,$t2,4
        # The TLS block: the bytes of .tdata, then zeros for .tbss at the
        # same offset from the block as from the template.
        lui $t0,%hi(__tls_data)
        addiu $t0,$t0,%lo(__tls_data)
        lui $t1,%hi(__tls_data_size)
        addiu $t1,$t1,%lo(__tls_data_size)
        addu $t1,$t0,$t1
        lui $t2,%hi(tls_block)
        addiu $t2,$t2,%lo(tls_block)
        move $t3,$t2
.Lcopy_data:
        beq $t0,$t1,.Lzero_start
        nop
        lbu $t4,0($t0)
        addiu $t0,$t0,1
        sb $t4,0($t3)
        b .Lcopy_data
        addiu $t3,$t3,1
.Lzero_start:
        lui $t0,%hi(__tls_zero)
        addiu $t0,$t0,%lo(__tls_zero)
        lui $t1,%hi(__tls_data)
        addiu $t1,$t1,%lo(__tls_data)
        subu $t0,$t0,$t1
        addu $t3,$t2,$t0
        lui $t1,%hi(__tls_zero_size)
        addiu $t1,$t1,%lo(__tls_zero_size)
        addu $t1,$t3,$t1
        subu $t4,$t1,$t2
        sltiu $t4,$t4,257
        beq $t4,$zero,unexpected
        nop
.Lzero:
        beq $t3,$t1,.Lthread_pointer
        nop
        sb $zero,0($t3)
        b .Lzero
        addiu $t3,$t3,1
.Lthread_pointer:
        addiu $t0,$t2,0x7000
        lui $t1,%hi(thread_pointer)
        sw $t0,%lo(thread_pointer)($t1)
        # Only MIPS32 and MIPS64 CPUs, whose PRId names a company, have
        # selectable CP0 registers such as UserLocal.
        mfc0 $t1,$15
        lui $t3,0x00ff
        and $t1,$t1,$t3
        beq $t1,$zero,.Lrounds
        nop
        .set push
        .set mips32r2
        mtc0 $t0,$4,2
        .set pop
.Lrounds:
        move $s0,$zero
.Lround:
        lui $t9,%hi(runtime_check)
        addiu $t9,$t9,%lo(runtime_check)
        jalr $t9
        move $a0,$s0
        bal print_digit
        move $a0,$v0
        addiu $s0,$s0,1
        sltiu $t0,$s0,3
        bne $t0,$zero,.Lround
        nop
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $t1,$zero,10
        sb $t1,0($t0)
        move $a0,$zero
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
halt:   b halt
        nop
print_digit:
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $a0,$a0,48
        sb $a0,0($t0)
        jr $ra
        nop
# Prints E and exits with status 1.
unexpected:
        lui $t0,0xbf00
        ori $t0,$t0,0x0900
        addiu $t1,$zero,69
        sb $t1,0($t0)
        addiu $t1,$zero,10
        sb $t1,0($t0)
        addiu $a0,$zero,1
        addiu $v0,$zero,1
        addiu $t9,$zero,1
        .word 0x7000007f
.Lhalt_unexpected:
        b .Lhalt_unexpected
        nop

# The exception handler: it emulates `rdhwr $3,$29` outside a delay slot
# and reports any other exception.
exception:
        mfc0 $k0,$13
        nop
        bltz $k0,.Lunexpected
        andi $k0,$k0,0x7c
        xori $k0,$k0,0x28
        bne $k0,$zero,.Lunexpected
        nop
        mfc0 $k0,$14
        nop
        lw $k1,0($k0)
        lui $k0,0x7c03
        ori $k0,$k0,0xe83b
        bne $k0,$k1,.Lunexpected
        nop
        lui $k1,%hi(thread_pointer)
        lw $v1,%lo(thread_pointer)($k1)
        mfc0 $k0,$14
        nop
        addiu $k0,$k0,4
        mtc0 $k0,$14
        nop
        nop
        eret
.Lunexpected:
        lui $k0,%hi(unexpected)
        addiu $k0,$k0,%lo(unexpected)
        jr $k0
        nop
exception_end:

        .text
        .globl scramble_gp
scramble_gp:
        lui $gp,0x5a5a
        jr $ra
        move $v0,$a0

        .bss
        .align 4
thread_pointer:
        .space 16
tls_block:
        .space 256
