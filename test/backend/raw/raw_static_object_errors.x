// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Raw code reaches a static-duration object relative to the instruction
// pointer, or one at a fixed address through its sign-extended 32-bit
// absolute address, and takes no other address.

#if RAW_STATIC_ERROR == 0
[[thread_local]] static u32 per_thread;
[[naked, clobber("rax")]]
global u32 read_thread() -> "eax" {
    register u32 result "eax";
    result = per_thread;
    $::_ret();
}
#elif RAW_STATIC_ERROR == 1
// Preemptible under -fpic: it needs a GOT entry.
global u32 exported = 1u32;
[[naked, clobber("rax")]]
global u32 read_exported() -> "eax" {
    register u32 result "eax";
    result = exported;
    $::_ret();
}
#elif RAW_STATIC_ERROR == 2
[[naked, clobber("rax")]]
global u64 *register_address(in u64 value "rcx") -> "rax" {
    register u64 *result "rax";
    result = &value;
    $::_ret();
}
#elif RAW_STATIC_ERROR == 3
volatile u32 far_status [[address(0x123456789000)]];
[[naked, clobber("rax")]]
global u32 read_far() -> "eax" {
    register u32 result "eax";
    result = far_status;
    $::_ret();
}
#else
volatile u32 device_status [[address(0x10000000)]];
[[naked, clobber("rax")]]
global u32 read_status() -> "eax" {
    register u32 result "eax";
    result = device_status;
    $::_ret();
}
#endif
