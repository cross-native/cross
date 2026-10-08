// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/shrink_wrap.hpp"

#include "common/control_flow.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>

namespace cross::machine {
namespace {

bool dense(const Function& function) {
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        if (function.blocks[index].id.value != index) return false;
    }
    return function.entry.value < function.blocks.size();
}

std::size_t block_capacity(const Function& function) {
    std::size_t result = 0;
    for (const auto& block : function.blocks) {
        result = std::max<std::size_t>(result, block.id.value + 1U);
    }
    return result;
}

Dominance dominance(const Function& function) {
    return Dominance(
        function.blocks.size(), function.entry.value,
        [&](std::uint32_t block) -> const std::vector<BlockId>& {
            return function.blocks[block].successors;
        },
        [&](std::uint32_t block) -> const std::vector<BlockId>& {
            return function.blocks[block].predecessors;
        });
}

std::vector<bool> reachable_from(const Function& function,
                                 std::uint32_t start) {
    std::vector<bool> seen(function.blocks.size());
    std::vector<std::uint32_t> pending{start};
    seen[start] = true;
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        for (const auto successor : function.blocks[block].successors) {
            if (successor.value < seen.size() && !seen[successor.value]) {
                seen[successor.value] = true;
                pending.push_back(successor.value);
            }
        }
    }
    return seen;
}

// A block dominates everything reachable from it and lies on no cycle exactly
// when its dominance frontier is empty: a frontier block is either reachable
// without passing it or the block itself, reached again. Marks the blocks
// with a nonempty frontier (Cooper, Harvey, and Kennedy's frontier walk).
std::vector<bool> frontier_blocks(const Function& function,
                                  const Dominance& tree) {
    std::vector<bool> result(function.blocks.size());
    for (const auto& block : function.blocks) {
        const auto idom = tree.immediate_dominator(block.id.value);
        if (!idom || block.predecessors.size() < 2) continue;
        for (const auto predecessor : block.predecessors) {
            if (!tree.reachable(predecessor.value)) continue;
            for (auto runner = predecessor.value; runner != *idom;
                 runner = *tree.immediate_dominator(runner)) {
                result[runner] = true;
            }
        }
    }
    return result;
}

// Requires dense blocks and an entry without predecessors.
bool valid_at(const Function& function, const Dominance& tree,
              const std::vector<bool>& frontier, std::uint32_t block) {
    return block != function.entry.value && tree.reachable(block) &&
           !frontier[block];
}

bool placeable(const Function& function) {
    return dense(function) &&
           function.blocks[function.entry.value].predecessors.empty();
}

bool names(const Register& reg, std::uint32_t virtual_id) {
    return reg.kind == RegisterKind::Virtual && reg.id == virtual_id;
}

} // namespace

std::vector<bool> frame_blocks(const Function& function,
                               const InstructionPredicate& is_phi) {
    std::vector<bool> result(block_capacity(function));
    std::unordered_set<std::uint32_t> framed;
    for (const auto physical : function.callee_saved_registers) {
        framed.insert(physical.value);
    }
    if (function.frame.program) {
        for (const auto& instruction : function.frame.program->prologue) {
            for (const auto& definition : instruction.defs) {
                framed.insert(definition.id);
            }
            if (instruction.frame_effect) {
                for (const auto& transfer :
                     instruction.frame_effect->transfers) {
                    framed.insert(transfer.reg.id);
                }
            }
        }
    }
    const auto in_frame = [&](const Register& reg) {
        if (reg.kind == RegisterKind::Physical) return framed.contains(reg.id);
        if (reg.id >= function.virtual_register_assignments.size()) {
            return true;
        }
        if (const auto& color = function.virtual_register_assignments[reg.id]) {
            return framed.contains(color->value);
        }
        return reg.id >= function.rematerialized_immediates.size() ||
               !function.rematerialized_immediates[reg.id];
    };
    const auto any_in_frame = [&](const std::vector<Register>& registers) {
        return std::any_of(registers.begin(), registers.end(), in_frame);
    };
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (is_phi(instruction)) {
                const bool target = any_in_frame(instruction.defs);
                for (std::size_t index = 0;
                     index + 1 < instruction.operands.size(); index += 2) {
                    const auto* predecessor =
                        std::get_if<BlockOperand>(&instruction.operands[index]);
                    const auto* incoming = std::get_if<RegisterOperand>(
                        &instruction.operands[index + 1]);
                    if (predecessor &&
                        predecessor->target.value < result.size() &&
                        (target || !incoming || in_frame(incoming->value))) {
                        result[predecessor->target.value] = true;
                    }
                }
                continue;
            }
            const bool needs =
                instruction.kind == InstructionKind::Call ||
                any_in_frame(instruction.defs) ||
                any_in_frame(instruction.uses) ||
                any_in_frame(instruction.clobbers) ||
                any_in_frame(instruction.live_across_call) ||
                std::any_of(instruction.operands.begin(),
                            instruction.operands.end(),
                            [&](const Operand& operand) {
                                if (std::holds_alternative<StackSlotOperand>(
                                        operand)) {
                                    return true;
                                }
                                const auto* reg =
                                    std::get_if<RegisterOperand>(&operand);
                                return reg && in_frame(reg->value);
                            });
            if (needs) result[block.id.value] = true;
        }
    }
    return result;
}

bool valid_prologue_block(const Function& function, BlockId block) {
    if (!placeable(function) || block.value >= function.blocks.size()) {
        return false;
    }
    const auto tree = dominance(function);
    return valid_at(function, tree, frontier_blocks(function, tree),
                    block.value);
}

std::optional<BlockId> place_prologue(const Function& function,
                                      const std::vector<bool>& needs_frame) {
    if (!placeable(function)) return std::nullopt;
    const auto entry = function.entry.value;
    const auto tree = dominance(function);
    std::optional<std::uint32_t> block;
    for (std::uint32_t candidate = 0; candidate < function.blocks.size();
         ++candidate) {
        if (candidate >= needs_frame.size() || !needs_frame[candidate] ||
            !tree.reachable(candidate)) {
            continue;
        }
        if (!block) {
            block = candidate;
            continue;
        }
        while (!tree.dominates(*block, candidate)) {
            block = *tree.immediate_dominator(*block);
        }
    }
    if (!block) return std::nullopt;
    const auto frontier = frontier_blocks(function, tree);
    while (*block != entry && !valid_at(function, tree, frontier, *block)) {
        block = *tree.immediate_dominator(*block);
    }
    if (*block == entry) return std::nullopt;
    // Profitable only when some return runs without the prologue.
    const auto region = reachable_from(function, *block);
    for (std::uint32_t other = 0; other < region.size(); ++other) {
        const auto& instructions = function.blocks[other].instructions;
        if (!region[other] && tree.reachable(other) && !instructions.empty() &&
            instructions.back().kind == InstructionKind::Return) {
            return BlockId{*block};
        }
    }
    return std::nullopt;
}

std::vector<bool> framed_blocks(const Function& function) {
    std::vector<bool> result(block_capacity(function), true);
    if (!function.frame.prologue_block || !placeable(function) ||
        function.frame.prologue_block->value >= function.blocks.size()) {
        return result;
    }
    const auto from_entry = reachable_from(function, function.entry.value);
    const auto region =
        reachable_from(function, function.frame.prologue_block->value);
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        result[index] = region[index] || !from_entry[index];
    }
    return result;
}

bool split_entry_parameters(Function& function,
                            const InstructionPredicate& splittable,
                            const InstructionPredicate& is_phi) {
    if (!placeable(function)) return false;
    const auto entry = function.entry.value;
    const auto& head = function.blocks[entry];
    const auto is_call = [](const Instruction& instruction) {
        return instruction.kind == InstructionKind::Call;
    };
    if (head.instructions.empty() ||
        head.instructions.back().kind != InstructionKind::ConditionalBranch ||
        std::any_of(head.instructions.begin(), head.instructions.end(),
                    is_call)) {
        return false;
    }
    const auto tree = dominance(function);
    // A successor entered only from the entry owns the blocks it dominates.
    constexpr auto none = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> owner(function.blocks.size(), none);
    for (const auto successor : head.successors) {
        if (successor.value >= function.blocks.size() ||
            function.blocks[successor.value].predecessors.size() != 1) {
            continue;
        }
        for (std::uint32_t block = 0; block < owner.size(); ++block) {
            if (tree.dominates(successor.value, block)) {
                owner[block] = successor.value;
            }
        }
    }
    // Shrink-wrapping can avoid the frame on some path only if every call
    // lies below one successor; values cross calls only there.
    std::optional<std::uint32_t> calls;
    for (const auto& block : function.blocks) {
        if (!std::any_of(block.instructions.begin(), block.instructions.end(),
                         is_call)) {
            continue;
        }
        if (owner[block.id.value] == none ||
            (calls && *calls != owner[block.id.value])) {
            return false;
        }
        calls = owner[block.id.value];
    }
    if (!calls) return false;
    const auto below = [&](std::uint32_t block) {
        return block < owner.size() && owner[block] == *calls;
    };

    std::vector<Register> parameters;
    for (const auto& instruction : head.instructions) {
        if (splittable(instruction) && instruction.defs.size() == 1 &&
            instruction.defs.front().kind == RegisterKind::Virtual) {
            parameters.push_back(instruction.defs.front());
        }
    }
    bool changed = false;
    for (const auto parameter : parameters) {
        // Every use outside the entry must lie below one successor, since a
        // use in a join would keep the original live across the calls. A
        // PHI reads its incoming value at the end of the predecessor.
        std::vector<bool> used(function.blocks.size());
        std::vector<bool> phi_used(function.blocks.size());
        bool joined = false;
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (is_phi(instruction)) {
                    for (std::size_t index = 0;
                         index + 1 < instruction.operands.size(); index += 2) {
                        const auto* predecessor = std::get_if<BlockOperand>(
                            &instruction.operands[index]);
                        const auto* incoming = std::get_if<RegisterOperand>(
                            &instruction.operands[index + 1]);
                        if (!incoming ||
                            !names(incoming->value, parameter.id)) {
                            continue;
                        }
                        if (!predecessor ||
                            predecessor->target.value >= owner.size()) {
                            joined = true;
                        } else if (predecessor->target.value != entry) {
                            joined = joined ||
                                owner[predecessor->target.value] == none;
                            phi_used[predecessor->target.value] = true;
                        }
                    }
                    continue;
                }
                if (block.id.value == entry ||
                    std::none_of(instruction.uses.begin(),
                                 instruction.uses.end(),
                                 [&](const Register& use) {
                                     return names(use, parameter.id);
                                 })) {
                    continue;
                }
                joined = joined || owner[block.id.value] == none;
                used[block.id.value] = true;
            }
        }
        if (joined) continue;

        // Liveness below the calling successor, propagated backward from the
        // uses; nothing there redefines the parameter.
        std::vector<bool> live_out(function.blocks.size());
        std::vector<bool> live_in(function.blocks.size());
        std::vector<std::uint32_t> pending;
        for (std::uint32_t block = 0; block < function.blocks.size(); ++block) {
            live_out[block] = phi_used[block];
            live_in[block] = used[block] || phi_used[block];
            if (live_in[block]) pending.push_back(block);
        }
        while (!pending.empty()) {
            const auto block = pending.back();
            pending.pop_back();
            for (const auto predecessor : function.blocks[block].predecessors) {
                if (!below(predecessor.value) || live_out[predecessor.value]) {
                    continue;
                }
                live_out[predecessor.value] = true;
                if (!live_in[predecessor.value]) {
                    live_in[predecessor.value] = true;
                    pending.push_back(predecessor.value);
                }
            }
        }
        bool crosses = false;
        for (std::uint32_t block = 0;
             block < function.blocks.size() && !crosses; ++block) {
            if (!below(block)) continue;
            bool live = live_out[block];
            const auto& instructions = function.blocks[block].instructions;
            for (auto item = instructions.rbegin();
                 item != instructions.rend() && !crosses; ++item) {
                if (is_call(*item) && live) crosses = true;
                if (!is_phi(*item) &&
                    std::any_of(item->uses.begin(), item->uses.end(),
                                [&](const Register& use) {
                                    return names(use, parameter.id);
                                })) {
                    live = true;
                }
            }
        }
        if (!crosses) continue;

        const auto copy = Register::virtual_register(
            {static_cast<std::uint32_t>(function.virtual_registers.size())},
            parameter.mode);
        function.virtual_registers.push_back(parameter.mode);
        function.virtual_register_classes.push_back(
            function.virtual_register_classes[parameter.id]);
        function.virtual_register_assignments.push_back(std::nullopt);
        function.rematerialized_immediates.push_back(std::nullopt);
        const auto home = std::find_if(
            function.stack_slots.begin(), function.stack_slots.end(),
            [&](const StackSlot& slot) {
                return slot.spill_for && slot.spill_for->value == parameter.id;
            });
        if (home != function.stack_slots.end()) {
            StackSlot slot = *home;
            slot.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
            slot.name = "$v" + std::to_string(copy.id);
            slot.spill_for = VirtualRegisterId{copy.id};
            slot.frame_offset.reset();
            slot.frame_color.reset();
            slot.elided = false;
            function.stack_slots.push_back(std::move(slot));
        }

        const auto rename = [&](Register& reg) {
            if (names(reg, parameter.id)) reg = copy;
        };
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                if (is_phi(instruction)) {
                    instruction.uses.clear();
                    for (std::size_t index = 0;
                         index + 1 < instruction.operands.size(); index += 2) {
                        const auto* predecessor = std::get_if<BlockOperand>(
                            &instruction.operands[index]);
                        auto* incoming = std::get_if<RegisterOperand>(
                            &instruction.operands[index + 1]);
                        if (!incoming) continue;
                        if (predecessor && below(predecessor->target.value)) {
                            rename(incoming->value);
                        }
                        instruction.uses.push_back(incoming->value);
                    }
                    continue;
                }
                if (!below(block.id.value)) continue;
                for (auto& use : instruction.uses) rename(use);
                for (auto& live : instruction.live_across_call) rename(live);
                for (auto& operand : instruction.operands) {
                    if (auto* reg = std::get_if<RegisterOperand>(&operand)) {
                        rename(reg->value);
                    }
                }
            }
        }
        auto& top = function.blocks[*calls].instructions;
        auto position = top.begin();
        while (position != top.end() && is_phi(*position)) ++position;
        Instruction move;
        move.kind = InstructionKind::Copy;
        move.location = position != top.end() ? position->location
                                              : function.location;
        move.defs.push_back(copy);
        move.uses.push_back(parameter);
        top.insert(position, std::move(move));
        changed = true;
    }
    return changed;
}

} // namespace cross::machine
