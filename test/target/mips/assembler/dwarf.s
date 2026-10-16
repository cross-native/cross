# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# .file and .loc build the DWARF 5 line table, and the .cfi directives the
# .debug_frame and .eh_frame call-frame tables.
        .file 0 "/work" "main.x"
        .file 1 "/work/include" "header.x"
        .cfi_sections .debug_frame, .eh_frame
        .set noreorder
        .text
        .globl f
f:
        .cfi_startproc
        .loc 0 3 5 prologue_end
        addiu $sp, $sp, -16
        .cfi_def_cfa_offset 16
        sw $ra, 8($sp)
        .cfi_offset 31, -8
        .loc 1 10 2
        jal g
        nop
        .loc 0 4 1 is_stmt 0
        lw $ra, 8($sp)
        .cfi_remember_state
        .cfi_restore 31
        .loc 0 4 1 basic_block
        .loc 0 5 1 epilogue_begin discriminator 2
        jr $ra
        addiu $sp, $sp, 16
        .cfi_restore_state
        .cfi_def_cfa_offset 0
        .cfi_endproc

        .section .text.other,"ax",@progbits
        .globl h
h:
        .cfi_startproc
        .loc 0 20 0
        nop
        .loc 0 2000 7
        .rept 300
        nop
        .endr
        .cfi_def_cfa_register 30
        .loc 0 21 0 isa 1
        jr $ra
        nop
        .cfi_endproc
