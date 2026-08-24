// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_pass.hpp"

#include <utility>

namespace cross::native {

void MachineFunctionPassManager::add(MachinePassInfo info,
                                     MachineFunctionPass pass) {
    passes_.push_back({info, std::move(pass)});
}

bool MachineFunctionPassManager::run(
    machine::Function& function,
    const MachinePassObserver& observer) const {
    bool any_changed = false;
    for (const auto& pass : passes_) {
        const bool changed = pass.run(function);
        any_changed = any_changed || changed;
        if (observer) observer(pass.info, function, changed);
    }
    return any_changed;
}

} // namespace cross::native
