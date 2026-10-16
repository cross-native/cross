// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"
#include "target/x86_64/features.hpp"
#include "target/x86_64/instructions.hpp"

#include <algorithm>
#include <vector>

namespace cross::x86_64 {

// The instructions x86-64 selects with: CMOVNE for an integer; for an f32 or
// f64 a mask blend, VBLENDVPS/PD with AVX or BLENDVPS/PD with SSE4.1 where
// cheaper than ANDPS, ANDNPS, and ORPS. A comparison of the selected type
// forms the mask with CMPSS/CMPSD in place of the COMISS a branch needs; a
// boolean becomes the mask through NEG and MOVD. Between two constants a
// boolean selects their bit patterns in a general register instead,
// F ^ (-c & (T ^ F)), where that costs no more than the blend, which also
// loads both constants. The emitter selects the form this returns.
inline std::vector<InstructionCostId> select_instructions(
    const Subtarget& subtarget, const SelectCostQuery& query) {
    using I = Instruction;
    using List = std::vector<InstructionCostId>;
    if (!query.floating) {
        if (query.bits == 0 || query.bits > 64) return {};
        return {query.bits == 64 ? I::Cmovneq : I::Cmovnel};
    }
    if (query.bits != 32 && query.bits != 64) return {};
    const bool single = query.bits == 32;
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
    List blend;
    if (subtarget.has_feature(Feature::Avx)) {
        blend = {single ? I::Vblendvps : I::Vblendvpd};
    } else if (subtarget.has_feature(Feature::Sse41)) {
        blend = {single ? I::Blendvps : I::Blendvpd};
    }
    const List logic = single ? List{I::Andps, I::Andnps, I::Orps}
                              : List{I::Andpd, I::Andnpd, I::Orpd};
    const auto& chosen =
        usable(blend) && (!usable(logic) || cost(blend) <= cost(logic))
            ? blend : logic;
    if (!usable(chosen)) return {};
    List result;
    if (!query.floating_comparison) result = {single ? I::Negl : I::Negq, I::Movd};
    result.insert(result.end(), chosen.begin(), chosen.end());
    if (query.constant_operands && !query.floating_comparison) {
        const List integer{single ? I::Negl : I::Negq, I::Andl, I::Xorl,
                           I::Movd};
        if (usable(integer) && cost(integer) <= cost(result)) return integer;
    }
    return result;
}

} // namespace cross::x86_64
