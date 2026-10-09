// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef BAD_RESULT
[[abi("sysv_abi")]] global u64 bad_result_callee(in u64 x) -> "rdx";
[[abi("sysv_abi")]] global u64 bad_result(in u64 x) -> "rax" {
    [[musttail]] return bad_result_callee(x);
}
#endif

#ifdef BAD_STACK_RESULT
[[abi("sysv_abi")]] global u64 bad_slot_callee(in u64 x "rdi") -> "stack";
[[abi("sysv_abi")]]
global u64 bad_slot(in u64 x "rdi", in u64 y "stack") -> "stack" {
    [[musttail]] return bad_slot_callee(x + y);
}
#endif

#ifdef BAD_STACK_ARGUMENT
[[abi("sysv_abi")]] global u64 bad_stack_callee(in u64 x "stack") -> "rdx";
[[abi("sysv_abi")]] global u64 bad_stack(in u64 x "stack") -> "rdx" {
    [[musttail]] return bad_stack_callee(x);
}
#endif

#ifdef BAD_ADDRESS
struct pair { u64 a; u64 b; };
[[abi("sysv_abi")]] global u64 bad_address_callee(in struct pair p "*rsi") -> "rdx";
[[abi("sysv_abi")]] global u64 bad_address(in struct pair p "*rsi") -> "rdx" {
    [[musttail]] return bad_address_callee(p);
}
#endif

#ifdef BAD_MIXED
[[abi("sysv_abi")]] global u64 bad_mixed_callee(in u64 x);
[[abi("sysv_abi")]] global u64 bad_mixed(in u64 x "rdi") {
    [[musttail]] return bad_mixed_callee(x);
}
#endif

#ifdef BAD_CLOBBER
[[abi("sysv_abi"), clobber("rbx")]] global u64 bad_clobber_callee(in u64 x "rdi") -> "rdx";
[[abi("sysv_abi")]] global u64 bad_clobber(in u64 x "rdi") -> "rdx" {
    [[musttail]] return bad_clobber_callee(x);
}
#endif

#ifdef BAD_PRESERVED
[[abi("sysv_abi")]] global u64 bad_preserved_callee(in u64 x "rbx") -> "rdx";
[[abi("sysv_abi")]] global u64 bad_preserved(in u64 x "rdi") -> "rdx" {
    [[musttail]] return bad_preserved_callee(x);
}
#endif

#ifdef BAD_CLEANUP
[[abi("sysv_abi"), stack_cleanup("callee")]] global u64 bad_cleanup_callee(in u64 x "stack") -> "rdx";
[[abi("sysv_abi")]] global u64 bad_cleanup(in u64 x "stack") -> "rdx" {
    [[musttail]] return bad_cleanup_callee(x);
}
#endif
