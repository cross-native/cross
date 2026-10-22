// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/mips/bit_operations.hpp"
#include "target/mips/features.hpp"
#include "target/mips/instructions.hpp"
#include "target/subtarget.hpp"

#include <vector>

namespace cross::mips {

// The instructions MIPS selects with under the conditional-move feature:
// MOVN for an integer of at most 32 bits; for an f32 or f64, which MIPS IV,
// MIPS32, and MIPS64 FPUs select, MOVT.fmt after a C.cond.fmt comparison of
// the selected type or MOVN.fmt on a boolean. Release 6 selects an integer
// as wide as its registers with SELEQZ, SELNEZ, and OR, and a floating
// value with SEL.fmt on a comparison's result or, after MTC1, a boolean,
// and MOV.fmt. None where it branches.
inline std::vector<InstructionCostId> select_instructions(
    const Subtarget& subtarget, const SelectCostQuery& query) {
    using I = Instruction;
    if (release6(subtarget)) {
        const bool single = query.bits == 32;
        if (!query.floating) {
            const auto bits = subtarget.has_feature(Feature::Mips3) ? 64U : 32U;
            if (query.bits == 0 || query.bits > bits) return {};
            return {I::Seleqz, I::Selnez, I::Or};
        }
        if ((!single && query.bits != 64) || !subtarget.has_feature(Feature::HardFloat) ||
            (!single && subtarget.has_feature(Feature::SingleFloat))) {
            return {};
        }
        if (query.floating_comparison) {
            return {single ? I::SelS : I::SelD, single ? I::MovS : I::MovD};
        }
        return {I::Mtc1, single ? I::SelS : I::SelD, single ? I::MovS : I::MovD};
    }
    if (!subtarget.has_feature(Feature::CondMove)) return {};
    if (!query.floating) {
        if (query.bits == 0 || query.bits > 32) return {};
        return {I::Movn};
    }
    const bool single = query.bits == 32;
    if ((!single && query.bits != 64) ||
        !subtarget.has_feature(Feature::HardFloat) ||
        (!single && subtarget.has_feature(Feature::SingleFloat)) ||
        (!subtarget.has_feature(Feature::Mips4) &&
         !subtarget.has_feature(Feature::Mips32) &&
         !subtarget.has_feature(Feature::Mips64))) {
        return {};
    }
    if (query.floating_comparison) return {single ? I::MovtS : I::MovtD};
    return {single ? I::MovnS : I::MovnD};
}

} // namespace cross::mips
