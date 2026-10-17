// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/machine_ir.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

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
// over blocks whose source-visible labels make movement undesirable. With
// `late_conditional_back_edges`, a conditional back edge joins only after
// every forward edge, so a loop's chain starts at its header and its latch
// branches back once; a target whose emitter places a non-fallthrough
// edge's PHI copies inline after the branch (MIPS) asks for it.
[[nodiscard]] bool schedule_block_layout(
    machine::Function& function,
    const MachineBlockPredicate& is_addressable = {},
    bool late_conditional_back_edges = false);

// What a target tells branch duplication: which instructions are PHIs
// (their uses arrive on predecessor edges) and which emit no code, whether
// an edge needs no PHI copies under the register assignment, and which
// blocks a source-visible label keeps in place.
struct BranchDuplicationPolicy {
    MachineInstructionPredicate is_phi;
    MachineInstructionPredicate emits_nothing;
    std::function<bool(machine::BlockId, machine::BlockId)> copy_free;
    MachineBlockPredicate is_addressable;
};

// Loop rotation by tail duplication after register allocation: a latch
// that falls through to its loop's header, where the header holds only
// PHIs and a conditional branch that falls through into the loop and
// branches out of it, and whose edge needs no PHI copies, ends with a copy
// of that branch instead, so the loop tests at its bottom. The header moves
// after another predecessor that jumps to it and is laid out before one of
// the branch's targets, so that predecessor falls through. The targets gain
// the latch as a predecessor with the values the header passed them.
[[nodiscard]] bool duplicate_branch_blocks(
    machine::Function& function, const BranchDuplicationPolicy& policy);

// The number of natural loops that contain each block, by block id.
[[nodiscard]] std::vector<unsigned>
loop_depths(const machine::Function& function);

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

// What a target tells loop-constant hoisting: which instructions
// materialize a constant (they have no register uses), which are PHIs
// (whose uses are live on predecessor edges), the register bank of a
// virtual register that takes an allocatable color, how many colors of
// each bank the values of one loop may occupy, and how many colors each
// bank has.
struct LoopConstantPolicy {
    MachineInstructionPredicate is_constant;
    MachineInstructionPredicate is_phi;
    std::function<std::optional<unsigned>(const machine::Register&)> bank;
    std::vector<unsigned> capacity;
    std::vector<unsigned> colors;
};

// Moves constant materializations out of natural loops to the end of the
// loop's only predecessor outside it, which dominates the loop, while every
// point of the loop and of the preheader's end keeps its live values and
// the hoisted constants within each bank's capacity, or within the
// function's peak pressure when that is higher but fits the bank's colors.
// A hoisted constant adds a value only where it was not already live. Out
// of a loop with calls, a constant that an operation other than a call
// reads leaves only into the call-preserved colors that the function's
// busiest call needs and the loop's calls leave free. Inner loops come
// first, so a constant leaves as many loops of a nest as the budget allows;
// equal constants hoisted into one block are merged.
[[nodiscard]] bool hoist_loop_constants(machine::Function& function,
                                        const LoopConstantPolicy& policy);

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
