// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/mips/features.hpp"
#include "target/mips/instructions.hpp"
#include "target/subtarget.hpp"

#include <initializer_list>
#include <vector>

namespace cross::mips {

// MIPS32 or MIPS64 Release 2.
inline bool release2(const Subtarget& subtarget) {
    return subtarget.has_feature(Feature::Mips32r2) ||
           subtarget.has_feature(Feature::Mips64r2);
}

// Whether a Release 2 instruction replaces a sequence: the -march CPU has
// it and its -mtune latency is at most the sequence's.
inline bool favors(const Subtarget& subtarget, Instruction instruction,
                   std::initializer_list<Instruction> sequence) {
    unsigned cost{};
    for (const auto item : sequence) cost += subtarget.latency(item);
    return subtarget.has_instruction(instruction) &&
           subtarget.latency(instruction) <= cost;
}

// SEB and SEH sign-extend a byte and a halfword (Release 2, Allegrex) in
// place of SLL and SRA.
inline bool selects_sign_extension(const Subtarget& subtarget) {
    using I = Instruction;
    return (release2(subtarget) || subtarget.has_feature(Feature::Allegrex)) &&
           favors(subtarget, I::Seb, {I::Sll, I::Sra}) &&
           favors(subtarget, I::Seh, {I::Sll, I::Sra});
}

// EXT and INS extract and insert a bit field of a word (Release 2,
// Allegrex), and DEXT and DINS one of a doubleword (MIPS64 Release 2), in
// place of a shift and a mask, and of masks, a shift, and an OR.
inline bool selects_bit_fields(const Subtarget& subtarget, unsigned bits) {
    using I = Instruction;
    if (bits > 32) {
        return subtarget.has_feature(Feature::Mips64r2) &&
               favors(subtarget, I::Dext, {I::Dsrl, I::Andi}) &&
               favors(subtarget, I::Dextm, {I::Dsrl, I::Andi}) &&
               favors(subtarget, I::Dextu, {I::Dsrl, I::Andi}) &&
               favors(subtarget, I::Dins, {I::Dsll, I::And, I::Or}) &&
               favors(subtarget, I::Dinsm, {I::Dsll, I::And, I::Or}) &&
               favors(subtarget, I::Dinsu, {I::Dsll, I::And, I::Or});
    }
    return (release2(subtarget) || subtarget.has_feature(Feature::Allegrex)) &&
           favors(subtarget, I::Ext, {I::Srl, I::Andi}) &&
           favors(subtarget, I::Ins, {I::Sll, I::And, I::Or});
}

// MUL, the three-operand multiply of MIPS32 and MIPS64, in place of MULT
// and MFLO for a product of at most 32 bits.
inline bool selects_three_operand_multiply(const Subtarget& subtarget) {
    using I = Instruction;
    return (subtarget.has_feature(Feature::Mips32) ||
            subtarget.has_feature(Feature::Mips64)) &&
           favors(subtarget, I::Mul, {I::Mult, I::Mflo});
}

// DROTR, DROTR32, and DROTRV rotate a doubleword (MIPS64 Release 2 with
// the rotate feature).
inline bool selects_doubleword_rotate(const Subtarget& subtarget) {
    using I = Instruction;
    return subtarget.has_feature(Feature::Rotate) &&
           subtarget.has_feature(Feature::Mips64r2) &&
           subtarget.has_instruction(I::Drotr) &&
           subtarget.has_instruction(I::Drotr32) &&
           subtarget.has_instruction(I::Drotrv);
}

// The instructions MIPS computes a bit operation with, or none where MIR's
// inline sequence is the only lowering: CLZ (MIPS32, MIPS64, Allegrex) and
// DCLZ (MIPS64); WSBH (Release 2, Allegrex), followed by ANDI for 16 bits or
// by ROTR 16 for 32, Allegrex's WSBW, and DSBH and DSHD for 64 bits (MIPS64
// Release 2); ROTRV with the rotate feature, DROTRV for 64 bits on MIPS64
// Release 2, otherwise two shifts and an OR, and pair sequences for 64 bits
// before MIPS III.
inline std::vector<InstructionCostId> bit_operation_instructions(
    const Subtarget& subtarget, CostedBitOperation operation, unsigned bits) {
    using I = Instruction;
    const bool allegrex = subtarget.has_feature(Feature::Allegrex);
    const bool release2 = mips::release2(subtarget);
    switch (operation) {
    case CostedBitOperation::CountLeadingZeros:
        if (bits == 32 && (allegrex || subtarget.has_feature(Feature::Mips32) ||
                           subtarget.has_feature(Feature::Mips64)))
            return {I::Clz};
        if (bits == 64 && subtarget.has_feature(Feature::Mips64)) return {I::Dclz};
        return {};
    case CostedBitOperation::CountTrailingZeros:
    case CostedBitOperation::PopulationCount:
        return {};
    case CostedBitOperation::ByteSwap:
        if (bits == 16 && (release2 || allegrex)) return {I::Wsbh, I::Andi};
        if (bits == 32 && allegrex) return {I::Wsbw};
        if (bits == 32 && release2) return {I::Wsbh, I::Rotr};
        if (bits == 64 && subtarget.has_feature(Feature::Mips64r2)) {
            return {I::Dsbh, I::Dshd};
        }
        return {};
    case CostedBitOperation::RotateLeft:
    case CostedBitOperation::RotateRight: {
        const bool left = operation == CostedBitOperation::RotateLeft;
        if (bits == 32) {
            if (subtarget.has_feature(Feature::Rotate) &&
                subtarget.has_instruction(I::Rotrv)) {
                if (left) return {I::Subu, I::Rotrv};
                return {I::Rotrv};
            }
            return {I::Subu, I::Sllv, I::Srlv, I::Or};
        }
        if (bits != 64) return {};
        if (selects_doubleword_rotate(subtarget)) {
            if (left) return {I::Dsubu, I::Drotrv};
            return {I::Drotrv};
        }
        if (subtarget.has_feature(Feature::Mips3))
            return {I::Dsubu, I::Dsllv, I::Dsrlv, I::Or};
        return {I::Andi, I::Beq, I::Sltiu, I::Bne, I::Subu, I::Sllv,
                I::Srlv, I::Or, I::Sllv, I::Srlv, I::Or};
    }
    }
    return {};
}

// Whether the leading zeros of a complement select CLO or DCLO, which count
// leading ones, in place of NOR and CLZ.
inline bool counts_leading_ones(const Subtarget& subtarget, unsigned bits) {
    using I = Instruction;
    const auto count = bit_operation_instructions(
        subtarget, CostedBitOperation::CountLeadingZeros, bits);
    if (count.empty()) return false;
    const auto ones = bits == 64 ? I::Dclo : I::Clo;
    return subtarget.has_instruction(ones) &&
           subtarget.latency(ones) <=
               subtarget.latency(count.front()) + subtarget.latency(I::Nor);
}

} // namespace cross::mips
