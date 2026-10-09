// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstddef>
#include <functional>
#include <optional>

namespace cross::native {

// Selection exposes copies, repeated encodable expressions, repeated loads,
// and dead definitions on every target. The common algorithms own SSA use
// rewriting and block-local availability; targets provide only instruction
// eligibility and storage constraints through typed predicates.
using MachineInstructionPredicate =
    std::function<bool(const machine::Instruction&)>;
using MachineRegisterPredicate = std::function<bool(const machine::Register&)>;
using MachineBlockPredicate = std::function<bool(machine::BlockId)>;

// Forms hot fallthrough traces and rotates canonical test-first loops in
// layout only. The CFG and SSA edges remain unchanged; targets retain control
// over blocks whose source-visible labels make movement undesirable.
[[nodiscard]] bool schedule_block_layout(
    machine::Function& function,
    const MachineBlockPredicate& is_addressable = {});

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

// Moves every instruction `is_capture` accepts to the start of the entry
// block, keeping their order, so no other entry code can overwrite an
// incoming register before its capture. Returns how many were moved; they
// form the entry block's prefix.
std::size_t hoist_entry_captures(machine::Function& function,
                                 const MachineInstructionPredicate& is_capture);

// Selection commonly gives every virtual register a conservative fallback
// home before target-independent cleanup has removed copies and dead values.
// Elide homes whose virtual register no longer occurs in Machine IR so every
// target can keep that early, simple construction without paying frame cost.
[[nodiscard]] bool
elide_unused_virtual_spill_slots(machine::Function& function);

// A quotient and a remainder of the same SSA operands in one block become one
// combined instruction at the first position, defining {quotient, remainder}.
struct DivisionKind {
    bool quotient{};
    bool is_signed{};
};
using DivisionClassifier =
    std::function<std::optional<DivisionKind>(const machine::Instruction&)>;
[[nodiscard]] bool fuse_division_results(
    machine::Function& function, const DivisionClassifier& classify,
    machine::TargetOpcodeId signed_combined,
    machine::TargetOpcodeId unsigned_combined);

} // namespace cross::native
