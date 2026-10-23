// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/aarch64/instructions.hpp"
#include "target/subtarget.hpp"

#include <vector>

namespace cross::aarch64 {

// The instructions AArch64 computes a bit operation with: clz, rbit and
// clz, rev, rev16 for 16 bits, and rorv, after a neg of the count for a
// left rotation; none for a population count, which base A64 has no
// general-register instruction for, and for other widths, which MIR widens
// or computes inline.
inline std::vector<InstructionCostId> bit_operation_instructions(
    const Subtarget&, CostedBitOperation operation, unsigned bits) {
    using I = Instruction;
    const bool native = bits == 32 || bits == 64;
    switch (operation) {
    case CostedBitOperation::CountLeadingZeros:
        if (native) return {I::Clz};
        return {};
    case CostedBitOperation::CountTrailingZeros:
        if (native) return {I::Rbit, I::Clz};
        return {};
    case CostedBitOperation::PopulationCount: return {};
    case CostedBitOperation::ByteSwap:
        if (bits == 16) return {I::Rev16};
        if (native) return {I::Rev};
        return {};
    case CostedBitOperation::RotateLeft:
        if (native) return {I::Neg, I::Rorv};
        return {};
    case CostedBitOperation::RotateRight:
        if (native) return {I::Rorv};
        return {};
    }
    return {};
}

// csel chooses between integers of at most 64 bits without a branch.
inline std::vector<InstructionCostId> select_instructions(const Subtarget&,
                                                          const SelectCostQuery& query) {
    if (query.floating || query.bits == 0 || query.bits > 64) return {};
    return {Instruction::Csel};
}

inline unsigned integer_register_bits(const Subtarget&) { return 64; }

} // namespace cross::aarch64
