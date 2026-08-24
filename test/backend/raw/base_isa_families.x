// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_base8"), clobber("flags")]]
global void raw_base8() {
    register u8 left "al";
    register u8 right "bl";
    register u8 result "cl";
    $::_mov(result, left);
    $::_add(result, right);
    $::_sub(result, 1);
    $::_xor(result, right);
    $::_and(result, 0x7fu8);
    $::_or(result, 0x80u8);
    $::_shl(result, 1);
    $::_shr(result, 1);
    $::_sar(result, 1);
    $::_rol(result, 1);
    $::_ror(result, 1);
    $::_test(result, right);
    $::_adc(result, 0);
    $::_sbb(result, 0);
    $::_inc(result);
    $::_dec(result);
    $::_neg(result);
    $::_not(result);
    $::_xchg(result, right);
    $::_xadd(result, right);
    $::_cmp(result, left);
    $::_sete(left);
    $::_setne(right);
    $::_ret();
}

[[naked, link_name("raw_base16"), clobber("flags")]]
global void raw_base16() {
    register u16 left "ax";
    register u16 right "bx";
    register u16 result "cx";
    $::_mov(result, left);
    $::_add(result, right);
    $::_imul(result, right);
    $::_bt(result, 3);
    $::_bts(result, 4);
    $::_btr(result, 5);
    $::_btc(result, 6);
    $::_cmp(result, left);
    $::_cmove(result, right);
    $::_cmovne(result, left);
    $::_ret();
}

[[naked, link_name("raw_base32"), clobber("flags")]]
global void raw_base32() {
    register u32 left "eax";
    register u32 right "ebx";
    register u32 result "ecx";
    $::_mov(result, left);
    $::_add(result, 0xffffffffu32);
    $::_sub(result, right);
    $::_imul(result, right);
    $::_bswap(result);
    $::_cmp(result, left);
    $::_cmovg(result, right);
    $::_cmovle(result, left);
    $::_ret();
}

[[naked, link_name("raw_base64"), clobber("flags")]]
global void raw_base64() {
    register u64 left "rax";
    register u64 right "rbx";
    register u64 result "rcx";
    $::_mov(result, left);
    $::_sar(result, 7);
    $::_rol(result, 11);
    $::_ror(result, 13);
    $::_bt(result, right);
    $::_bswap(result);
    $::_cmp(result, left);
    $::_cmova(result, right);
    $::_cmovb(result, left);
    $::_ret();
}

[[naked, link_name("raw_memory"), clobber("flags", "memory")]]
global void raw_memory(in volatile u64 *source "r9",
                       inout u64 *destination "r10",
                       inout u64 value "rax",
                       inout u64 exchange "rcx", in uptr index "r8") {
    $::_mov(value, *source);
    $::_mov(value, source[1]);
    $::_mov(*destination, value);
    $::_mov(destination[2], value);
    $::_mov(value, source[index]);
    $::_mov(destination[index], value);
    $::_add(value, *source);
    $::_sub(*destination, value);
    $::_xor(value, source[1]);
    $::_and(destination[2], value);
    $::_or(value, *source);
    $::_cmp(value, source[1]);
    $::_cmove(value, *source);
    $::_test(destination[2], value);
    $::_shl(*destination, 1);
    $::_not(destination[2]);
    $::_xchg(*destination, exchange);
    $::_xadd(destination[2], exchange);
    $::_bt(*source, 7);
    $::_bts(*destination, value);
    $::_ret();
}

[[naked, link_name("raw_cmpxchg"), clobber("flags", "memory")]]
global void raw_cmpxchg(inout u64 expected "rax",
                        inout u64 destination "r10",
                        inout u64 replacement "rcx") {
    $::_cmpxchg(destination, replacement);
    $::_ret();
}

[[naked, link_name("raw_atomic_memory"), clobber("flags", "memory")]]
global void raw_atomic_memory(inout u64 [[atomic]] *destination "r10",
                              inout u64 expected "rax",
                              inout u64 replacement "rcx") {
    $::_lock_cmpxchg(*destination, replacement);
    $::_xchg(*destination, replacement);
    $::_lock_xadd(*destination, replacement);
    $::_ret();
}

[[naked, link_name("raw_movbe"), clobber("memory")]]
global void raw_movbe(in u64 *source "r9", inout u64 *destination "r10",
                      inout u64 value "rax") {
    $::_movbe(value, *source);
    $::_movbe(*destination, value);
    $::_ret();
}

[[naked, link_name("raw_extensions"), clobber("flags", "memory")]]
global void raw_extensions(in u8 narrow "cl", in u16 half "r8w",
                           in u32 word "edx", in u8 *bytes "r9",
                           in u64 *words "r10", out u64 result "rax",
                           out u64 address "r11") {
    $::_movzx(result, narrow);
    $::_movsx(result, narrow);
    $::_movzx(result, half);
    $::_movsx(result, half);
    $::_movsx(result, word);
    $::_movzx(result, bytes[3]);
    $::_movsx(result, bytes[5]);
    $::_lea(address, bytes[7]);
    $::_bsf(result, address);
    $::_bsr(result, *words);
    $::_ret();
}

[[naked, link_name("raw_accumulator"),
  clobber("rax", "rdx", "flags")]]
global void raw_accumulator(inout u64 accumulator "rax",
                            inout u64 high "rdx", in u8 byte "cl",
                            in u16 half "r8w", in u32 word "r9d",
                            in u64 quad "r10") {
    $::_mul(byte);
    $::_mul(half);
    $::_mul(word);
    $::_mul(quad);
    $::_imul_full(quad);
    $::_cwd();
    $::_cdq();
    $::_cqo();
    $::_div(quad);
    $::_idiv(quad);
    $::_ret();
}

[[naked, link_name("raw_machine_state"),
  clobber("rax", "rbx", "rcx", "rdx", "r11", "flags", "memory")]]
global void raw_machine_state() {
    register u64 leaf "rax" = 0;
    register u64 subleaf "rcx" = 0;
    register u64 output_b "rbx";
    register u64 output_d "rdx";
    register u64 syscall_scratch "r11";
    $::_cpuid();
    $::_rdtsc();
    $::_clc();
    $::_cmc();
    $::_stc();
    $::_syscall();
    $::_nop();
    $::_ret();
}

[[naked, link_name("raw_ud2")]]
global void raw_ud2() {
    $::_ud2();
}

[[naked, link_name("raw_int3")]]
global void raw_int3() {
    $::_int3();
}
