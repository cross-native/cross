// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Assembly checks for the built-ins that every instruction table entry
// gives: instruction_builtins.cmake compiles this file for several CPUs.

// A pure form is one instruction, which fills a delay slot.
global u32 pure_add(in u32 a, in u32 b) {
    u32 d;
    $::_addu(d, a, b);
    return d;
}

// Pure forms merge, and an unused one disappears.
global u32 merged(in u32 a, in u32 b) {
    u32 first;
    u32 second;
    u32 unused;
    $::_subu(first, a, b);
    $::_subu(second, a, b);
    $::_nor(unused, a, b);
    return first ^ second;
}

// ADD traps on overflow: it stays though nothing reads its result.
global void trapping_add(in u32 a, in u32 b) {
    u32 sum;
    $::_add(sum, a, b);
}

// Memory operands are lvalues, whose displacements fold into the form.
global u32 memory_forms(in u32 *words, in u8 *bytes) {
    u32 word;
    u32 byte;
    $::_lw(word, words[1]);
    $::_lbu(byte, bytes[2]);
    $::_sw(byte, words[3]);
    return word;
}

#if $::has_instruction($::_teq)
// A trap's handler may use memory: the store stays before it and the load
// after it.
global u32 ordered_trap(in u32 *cell, in u32 a, in u32 b) {
    cell[0] = 7u32;
    $::_teq(a, b);
    return cell[0];
}
#endif

// MOVN keeps its destination where the condition is zero; INS keeps the
// bits outside its field; DIVU's `$zero` is printed but not an operand.
#if $::has_instruction($::_movn)
global u32 kept_destination(in u32 condition, in u32 yes, in u32 no) {
    u32 result = no;
    $::_movn(result, yes, condition);
    return result;
}
#endif

#if $::has_instruction($::_ins)
global u32 insert_field(in u32 base, in u32 field) {
    u32 result = base;
    $::_ins(result, field, 4, 8);
    return result;
}
#endif

#if $::has_instruction($::_mflo)
global u32 quotient(in u32 a, in u32 b) {
    u32 q;
    $::_divu(a, b);
    $::_mflo(q);
    return q;
}
#endif

// LWL and LWR merge into their register: on MIPS I its next reader waits
// one instruction.
#if $::has_instruction($::_lwl)
global u32 unaligned_word(in u8 *bytes) {
    u32 *first = (u32 *)(void *)bytes;
    u32 *last = (u32 *)(void *)(bytes + 3);
    u32 value = 0u32;
#if $::target::byte_order == $::target::order_big
    $::_lwl(value, *first);
    $::_lwr(value, *last);
#else
    $::_lwl(value, *last);
    $::_lwr(value, *first);
#endif
    return value + 1u32;
}
#endif

#if $::has_instruction($::_max)
// Allegrex instructions are words, since the function's MIPS II level has
// none of them.
global u32 allegrex_forms(in u32 a, in u32 b) {
    u32 high;
    u32 low;
    u32 reversed;
    $::_max(high, a, b);
    $::_min(low, a, b);
    u32 mixed = high ^ low;
    $::_bitrev(reversed, mixed);
    return reversed;
}
#endif

#if $::has_instruction($::_lsa)
// Release 6 forms.
global u32 release6_forms(in u32 a, in u32 b) {
    u32 scaled;
    u32 swapped;
    $::_lsa(scaled, a, b, 2);
    $::_bitswap(swapped, scaled);
    return swapped;
}
#endif

// A naked handler: coprocessor 0, a jump, and a branch-likely form.
#if $::has_instruction($::_beqzl) && $::has_instruction($::_eret)
[[naked, clobber("k0", "k1")]]
global void vector() {
    register u32 cause "k0";
    register u32 code "k1";
    $::_mfc0(cause, 13);
    $::_andi(code, cause, 0x7c);
    $::_beqzl(code, interrupt);
    $::_j(done);
interrupt:
    $::_mtc0(cause, 13);
done:
    $::_eret();
}
#endif
