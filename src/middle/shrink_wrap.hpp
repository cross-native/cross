// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shrink-wrapping runs a function's prologue on entry to the block that first
// needs the frame instead of at function entry, so paths that never need it,
// such as an early exit, skip the saves and their restores. The placement
// rules are target-independent; targets own the frame layout, the prologue
// and epilogue code, and frame uses the IR does not show.

#include "middle/machine_ir.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace cross::machine {

using InstructionPredicate = std::function<bool(const Instruction&)>;

// Blocks, indexed by ID, whose code needs the frame or a register it saves:
// a call; a stack-slot operand; a reference to a callee-saved register or to
// a register the frame program saves or sets up, directly or through an
// assignment; or a virtual register left in its frame home. A PHI, whose
// operands are (predecessor block, incoming register) pairs, copies at the
// end of each predecessor and counts there.
[[nodiscard]] std::vector<bool> frame_blocks(
    const Function& function, const InstructionPredicate& is_phi);

// Whether the prologue may run on entry to `block`: it is not the entry, it
// lies on no cycle, and it dominates every block reachable from it. Each path
// then runs the prologue at most once and before any of those blocks, and
// every exit reachable from it runs the epilogue. Blocks must be numbered
// densely by position and the entry must have no predecessor.
[[nodiscard]] bool valid_prologue_block(const Function& function,
                                        BlockId block);

// The block for a shrink-wrapped prologue (Chow; GCC shrink-wrap.cc, LLVM
// ShrinkWrap): the nearest common dominator of `needs_frame`, raised along
// the dominator tree until valid_prologue_block holds. Returns nullopt, which
// keeps the prologue at function entry, when that is the entry block or when
// every return runs after it.
[[nodiscard]] std::optional<BlockId> place_prologue(
    const Function& function, const std::vector<bool>& needs_frame);

// Blocks, indexed by ID, that run with the frame established: every block
// without shrink-wrapping, otherwise the blocks reachable from the prologue
// block and those unreachable from the entry.
[[nodiscard]] std::vector<bool> framed_blocks(const Function& function);

// Before allocation: when the entry block makes no call and ends in a
// conditional branch, a parameter that lives across a call below one of its
// successors is copied into a new virtual register at the top of that
// successor, and the uses that successor dominates read the copy. The
// original then needs no call-preserved register and the entry block can
// stay outside a shrink-wrapped frame (GCC's prepare_shrink_wrap sinks the
// same copies after allocation). `splittable` selects the parameter
// definitions a target can copy; each copy's register takes a copy of the
// original's spill home. Returns whether the function changed.
bool split_entry_parameters(Function& function,
                            const InstructionPredicate& splittable,
                            const InstructionPredicate& is_phi);

} // namespace cross::machine
