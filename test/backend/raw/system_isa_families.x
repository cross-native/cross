// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_extended_state"),
  clobber("rax", "rcx", "rdx", "r8", "r9", "r10", "memory")]]
global void raw_extended_state() {
    register u64 low "rax";
    register u64 selector "rcx";
    register u64 high "rdx";
    register u64 fs_base "r8";
    register u64 gs_base "r9";
    register u64 processor_id "r10";

    $::_rdtscp();
    $::_xgetbv();
    $::_xsetbv();
    $::_rdfsbase(fs_base);
    $::_rdgsbase(gs_base);
    $::_wrfsbase(fs_base);
    $::_wrgsbase(gs_base);
    $::_rdpid(processor_id);
    $::_rdpkru();
    $::_wrpkru();
    $::_ret();
}

[[naked, link_name("raw_state_memory"),
  clobber("rax", "rbx", "rcx", "rdx", "flags", "memory")]]
global void raw_state_memory(
    in volatile u8 *area "rsi", in volatile u8 *line "rdi",
    in volatile u32 *mxcsr "r10",
    in u64 [[atomic]] *word "r8",
    in u128 [[atomic]] *wide "r9",
    inout u64 low "rax", in u64 replacement_low "rbx",
    in u64 replacement_high "rcx", inout u64 high "rdx") {
    $::_prefetchnta(*line);
    $::_prefetcht0(*line);
    $::_prefetcht1(*line);
    $::_prefetcht2(*line);
    $::_prefetchw(*line);
    $::_clflush(*line);
    $::_clflushopt(*line);
    $::_clwb(*line);
    $::_fxsave(*area);
    $::_fxrstor(*area);
    $::_xsave(*area);
    $::_xrstor(*area);
    $::_xsaveopt(*area);
    $::_xsavec(*area);
    $::_xsaves(*area);
    $::_xrstors(*area);
    $::_stmxcsr(*mxcsr);
    $::_ldmxcsr(*mxcsr);
    $::_lock_cmpxchg8b(*word);
    $::_lock_cmpxchg16b(*wide);
    $::_ret();
}

[[naked, link_name("raw_wait_and_direct"),
  clobber("flags", "memory")]]
global void raw_wait_and_direct(
    in volatile u8 *source "rsi", in u64 *direct "rdi",
    in u128 *descriptor "r8", in u64 kind "rcx",
    in u64 destination "rax", in u64 payload "rbx",
    in u64 timeout_high "rdx", in u32 control "r10d") {
    $::_monitor();
    $::_mwait();
    $::_umonitor(destination);
    $::_umwait(control);
    $::_tpause(control);
    $::_invpcid(kind, *descriptor);
    $::_cldemote(*source);
    $::_movdiri(*direct, payload);
    $::_movdir64b(destination, *source);
    $::_enqcmd(destination, *source);
    $::_enqcmds(destination, *source);
    $::_wbnoinvd();
    $::_ret();
}
