// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/mips/features.hpp"
#include "target/mips/instructions.hpp"
#include "target/subtarget.hpp"

#include <vector>

namespace cross::mips {

// The instructions MIPS computes a bit operation with, or none where MIR's
// inline sequence is the only lowering: CLZ (MIPS32, MIPS64, Allegrex) and
// DCLZ (MIPS64); WSBH (MIPS32 Release 2, Allegrex), followed by ANDI for 16
// bits or by ROTR 16 for 32, and Allegrex's WSBW; ROTRV with the rotate
// feature, otherwise two shifts and an OR, and pair sequences for 64 bits
// before MIPS III.
inline std::vector<InstructionCostId> bit_operation_instructions(
    const Subtarget& subtarget, CostedBitOperation operation, unsigned bits) {
    using I = Instruction;
    const bool allegrex = subtarget.has_feature(Feature::Allegrex);
    const bool release2 = subtarget.has_feature(Feature::Mips32r2);
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
