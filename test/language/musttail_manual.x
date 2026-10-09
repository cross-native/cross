// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Mandatory tail transfers between manual interfaces whose result transports
// coincide: a custom result register, a forwarded result address, stack
// result slots, equal callee cleanup, and a call through a manual function
// pointer.

[[abi("sysv_abi"), noinline]]
global u64 musttail_rdx_callee(in u64 x) -> "rdx" { return x + 1u64; }

[[abi("sysv_abi"), noinline]]
global u64 musttail_rdx_caller(in u64 x) -> "rdx" {
    [[musttail]] return musttail_rdx_callee(x * 2u64);
}

struct musttail_big { u64 a; u64 b; u64 c; u64 d; };

[[abi("sysv_abi"), noinline]]
global struct musttail_big musttail_big_callee(in u64 x) -> "*rdi" {
    struct musttail_big result = { x, x + 1u64, x + 2u64, x + 3u64 };
    return result;
}

[[abi("sysv_abi"), noinline]]
global struct musttail_big musttail_big_caller(in u64 x) -> "*rdi" {
    [[musttail]] return musttail_big_callee(x + 10u64);
}

[[abi("sysv_abi"), noinline]]
global u64 musttail_stack_callee(in u64 x "rdi") -> "stack" {
    return x + 3u64;
}

[[abi("sysv_abi"), noinline]]
global u64 musttail_stack_caller(in u64 x "rdi") -> "stack" {
    [[musttail]] return musttail_stack_callee(x + 4u64);
}

// Microsoft x64 places the stack result slot above the 32-byte home area.
[[abi("ms_abi"), noinline]]
global u64 musttail_home_callee(in u64 x "rcx") -> "stack" {
    return x + 6u64;
}

[[abi("ms_abi"), noinline]]
global u64 musttail_home_caller(in u64 x "rcx") -> "stack" {
    [[musttail]] return musttail_home_callee(x * 3u64);
}

// Both sides pop the same 32-byte home area.
[[abi("ms_abi"), stack_cleanup("callee"), noinline]]
global u64 musttail_cleanup_callee(in u64 x "rcx") -> "rdx" {
    return x + 7u64;
}

[[abi("ms_abi"), stack_cleanup("callee"), noinline]]
global u64 musttail_cleanup_caller(in u64 x "rcx") -> "rdx" {
    [[musttail]] return musttail_cleanup_callee(x + 1u64);
}

typedef u64 (*musttail_manual_fn)(in u64 x "rdi") -> "rdx"
    [[abi("sysv_abi"), clobber("memory")]];

[[abi("sysv_abi"), clobber("memory"), noinline]]
global u64 musttail_pointer_target(in u64 x "rdi") -> "rdx" {
    return x * 5u64;
}

[[abi("sysv_abi"), clobber("memory"), noinline]]
global u64 musttail_pointer_caller(in u64 x "rdi",
                                   in musttail_manual_fn target "rsi")
    -> "rdx" {
    [[musttail]] return target(x + 1u64);
}

global i32 musttail_manual_entry() {
    u64 seed = $::runtime(5u64);
    struct musttail_big big = musttail_big_caller(seed);
    return (musttail_rdx_caller(seed) == 11u64) +
           (big.a == 15u64 && big.b == 16u64 && big.d == 18u64) +
           (musttail_stack_caller(seed) == 12u64) +
           (musttail_home_caller(seed) == 21u64) +
           (musttail_cleanup_caller(seed) == 13u64) +
           (musttail_pointer_caller(seed, &musttail_pointer_target) == 30u64);
}
