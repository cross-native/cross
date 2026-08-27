# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

        # This section is flattened before linking, so the auxiliary LLVM MC
        # object contributes no ABI metadata to the EABI32 executable.
        # link.ld fixes this blob at 0x08808000 and the import stub at
        # 0x08804000; keep the pointer constants in sync with that layout.
        .section .rodata.sceModuleInfo,"a",@progbits
        .p2align 2

        # PspModuleInfo (52 bytes).
        .short  0
        .short  0x0100
        .ascii  "cross-allegrex-test"
        .zero   9
        .word   0
        .word   0x08808034
        .word   0x08808034
        .word   0x08808048
        .word   0x0880805c

        # Module name, function NID, and one 20-byte PspLibStubEntry.
        .asciz  "LoadExecForUser"
        .word   0x05572a5f
        .word   0x08808034
        .short  0
        .short  0
        .byte   5
        .byte   0
        .short  1
        .word   0x08808044
        .word   0x08804000
