// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"
#include "target/x86_64/features.hpp"
#include "target/x86_64/instructions.hpp"

#include <algorithm>
#include <vector>

namespace cross::x86_64 {

// The instructions x86-64 computes a bit operation with, or none where MIR's
// inline sequence is the only lowering: LZCNT, TZCNT, and POPCNT where the
// features allow them; BSR or BSF and a CMOVNE that keeps the result for
// zero (BSR's index then flips into a count with XOR); BSWAP, or ROLW for
// 16 bits; ROL and ROR. The selection emits the first instruction's form.
inline std::vector<InstructionCostId> bit_operation_instructions(
    const Subtarget& subtarget, CostedBitOperation operation, unsigned bits) {
    using I = Instruction;
    using List = std::vector<InstructionCostId>;
    const bool wide = bits == 64;
    const auto usable = [&](const List& list) {
        return !list.empty() &&
               std::all_of(list.begin(), list.end(), [&](InstructionCostId id) {
                   return subtarget.has_instruction(id);
               });
    };
    const auto cost = [&](const List& list) {
        unsigned total = 0;
        for (const auto id : list) total += subtarget.latency(id);
        return total;
    };
    switch (operation) {
    case CostedBitOperation::CountLeadingZeros:
    case CostedBitOperation::CountTrailingZeros: {
        if (bits != 32 && !wide) return {};
        const bool leading = operation == CostedBitOperation::CountLeadingZeros;
        List count;
        if (subtarget.has_feature(leading ? Feature::Lzcnt : Feature::Bmi)) {
            count = {leading ? (wide ? I::Lzcntq : I::Lzcntl)
                             : (wide ? I::Tzcntq : I::Tzcntl)};
        }
        List scan{leading ? (wide ? I::Bsrq : I::Bsrl)
                          : (wide ? I::Bsfq : I::Bsfl),
                  wide ? I::Cmovneq : I::Cmovnel};
        if (leading) scan.push_back(I::Xorl);
        if (usable(count) && (!usable(scan) || cost(count) <= cost(scan)))
            return count;
        return usable(scan) ? scan : List{};
    }
    case CostedBitOperation::PopulationCount:
        if ((bits != 32 && !wide) || !subtarget.has_feature(Feature::Popcnt))
            return {};
        return {wide ? I::Popcntq : I::Popcntl};
    case CostedBitOperation::ByteSwap:
        if (bits == 16) return {I::Rolw};
        if (bits == 32) return {I::Bswapl};
        if (wide) return {I::Bswapq};
        return {};
    case CostedBitOperation::RotateLeft:
    case CostedBitOperation::RotateRight: {
        const bool left = operation == CostedBitOperation::RotateLeft;
        switch (bits) {
        case 8: return {left ? I::Rolb : I::Rorb};
        case 16: return {left ? I::Rolw : I::Rorw};
        case 32: return {left ? I::Roll : I::Rorl};
        case 64: return {left ? I::Rolq : I::Rorq};
        default: return {};
        }
    }
    }
    return {};
}

} // namespace cross::x86_64
