// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/frame_cfi.hpp"

#include <algorithm>

namespace cross::native {

CfiState FrameCfi::entry() const {
    return {stack_pointer_, entry_offset_, entry_offset_, std::nullopt, {}};
}

CfiState FrameCfi::after(CfiState state,
                         const machine::FrameEffect& effect) const {
    const auto distance = [&](unsigned reg) {
        return state.frame_register && state.frame_register->first == reg
            ? state.frame_register->second : state.stack_offset;
    };
    const auto base = number_(effect.base);
    const auto adjust = [&] { state.stack_offset -= effect.stack_delta; };
    const auto forget = [&](unsigned reg) {
        std::erase_if(state.saves, [&](const auto& save) { return save.first == reg; });
    };
    switch (effect.operation) {
    case machine::FrameOperation::AdjustStack:
        adjust();
        break;
    case machine::FrameOperation::CopyBase: {
        // The CFA follows the copy, so it never rests on a register that is
        // about to be reloaded.
        const auto destination = number_(*effect.destination);
        if (destination == stack_pointer_) {
            state.stack_offset = distance(base);
        } else {
            state.frame_register = std::pair{destination, distance(base)};
        }
        if (state.cfa_register == base) state.cfa_register = destination;
        break;
    }
    case machine::FrameOperation::Save:
        if (effect.update == machine::FrameUpdate::BeforeMemory) adjust();
        for (const auto& transfer : effect.transfers) {
            const auto reg = number_(transfer.reg);
            forget(reg);
            state.saves.emplace_back(reg, transfer.offset - distance(base));
        }
        if (effect.update == machine::FrameUpdate::AfterMemory) adjust();
        break;
    case machine::FrameOperation::Restore:
        if (effect.update == machine::FrameUpdate::BeforeMemory) adjust();
        for (const auto& transfer : effect.transfers) {
            const auto reg = number_(transfer.reg);
            forget(reg);
            if (state.cfa_register == reg) state.cfa_register = stack_pointer_;
            if (state.frame_register && state.frame_register->first == reg)
                state.frame_register.reset();
        }
        if (effect.update == machine::FrameUpdate::AfterMemory) adjust();
        break;
    }
    state.cfa_offset = distance(state.cfa_register);
    return state;
}

void FrameCfi::transition(std::ostream& output, CfiState& current,
                          const CfiState& target) const {
    if (target.cfa_register != current.cfa_register &&
        target.cfa_offset != current.cfa_offset) {
        output << ".cfi_def_cfa " << spelling_(target.cfa_register) << ", "
               << target.cfa_offset << '\n';
    } else if (target.cfa_register != current.cfa_register) {
        output << ".cfi_def_cfa_register " << spelling_(target.cfa_register) << '\n';
    } else if (target.cfa_offset != current.cfa_offset) {
        output << ".cfi_def_cfa_offset " << target.cfa_offset << '\n';
    }
    for (const auto& save : target.saves) {
        if (std::find(current.saves.begin(), current.saves.end(), save) ==
            current.saves.end()) {
            output << ".cfi_offset " << spelling_(save.first) << ", "
                   << save.second << '\n';
        }
    }
    for (const auto& save : current.saves) {
        if (std::none_of(target.saves.begin(), target.saves.end(),
                         [&](const auto& kept) { return kept.first == save.first; })) {
            output << ".cfi_restore " << spelling_(save.first) << '\n';
        }
    }
    current = target;
}

void FrameCfi::undefined(std::ostream& output, unsigned reg) const {
    output << ".cfi_undefined " << spelling_(reg) << '\n';
}

} // namespace cross::native
