// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 (*register_result)(in i32 value "rdi") -> "rdx"
    [[abi("sysv_abi"), clobber("memory")]];
typedef i32 (*stack_result)(in i32 value "rdi") -> "stack+0"
    [[abi("sysv_abi"), stack_cleanup("caller")]];

global register_result register_callback;
global stack_result stack_callback;
