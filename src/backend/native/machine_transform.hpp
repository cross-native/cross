// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <functional>

namespace cross::native {

// Selection exposes copies, repeated encodable expressions, repeated loads,
// and dead definitions on every target. The common algorithms own SSA use
// rewriting and block-local availability; targets provide only instruction
// eligibility and storage constraints through typed predicates.
using MachineInstructionPredicate =
    std::function<bool(const machine::Instruction&)>;
using MachineRegisterPredicate = std::function<bool(const machine::Register&)>;

[[nodiscard]] bool
propagate_virtual_register_copies(machine::Function& function,
                                  const MachineInstructionPredicate& is_copy);

[[nodiscard]] bool
eliminate_redundant_expressions(machine::Function& function,
                                const MachineInstructionPredicate& is_eligible);

[[nodiscard]] bool
eliminate_redundant_loads(machine::Function& function,
                          const MachineInstructionPredicate& is_eligible);

// Some targets temporarily represent values through implicit storage not yet
// modeled as SSA uses (for example, chunked over-wide vectors). Such values
// are retained when `definition_is_observable` returns true.
[[nodiscard]] bool eliminate_dead_definitions(
    machine::Function& function,
    const MachineRegisterPredicate& definition_is_observable = {});

} // namespace cross::native
