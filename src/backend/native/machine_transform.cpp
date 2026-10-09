// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_transform.hpp"

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::native {
namespace {

using ReplacementMap = std::unordered_map<std::uint32_t, machine::Register>;

machine::Register resolve_replacement(machine::Register value,
                                      const ReplacementMap& replacements) {
    std::unordered_set<std::uint32_t> seen;
    while (value.kind == machine::RegisterKind::Virtual &&
           seen.insert(value.id).second) {
        const auto found = replacements.find(value.id);
        if (found == replacements.end())
            break;
        value = found->second;
    }
    return value;
}

void apply_replacements(machine::Function& function,
                        const ReplacementMap& replacements) {
    for (auto& block : function.blocks) {
        for (auto& instruction : block.instructions) {
            for (auto& use : instruction.uses) {
                use = resolve_replacement(use, replacements);
            }
            for (auto& live : instruction.live_across_call) {
                live = resolve_replacement(live, replacements);
            }
            for (auto& operand : instruction.operands) {
                if (auto* reg =
                        std::get_if<machine::RegisterOperand>(&operand)) {
                    reg->value = resolve_replacement(reg->value, replacements);
                }
            }
        }
    }
}

bool erase_replaced_definitions(
    machine::Function& function,
    const std::unordered_set<std::uint32_t>& removed,
    const MachineInstructionPredicate& is_eligible) {
    bool changed = false;
    for (auto& block : function.blocks) {
        const auto before = block.instructions.size();
        std::erase_if(block.instructions,
                      [&](const machine::Instruction& instruction) {
                          return is_eligible(instruction) &&
                                 instruction.defs.size() == 1 &&
                                 instruction.defs.front().kind ==
                                     machine::RegisterKind::Virtual &&
                                 removed.contains(instruction.defs.front().id);
                      });
        changed = changed || block.instructions.size() != before;
    }
    return changed;
}

bool structurally_pure_single_definition(
    const machine::Instruction& instruction) {
    return instruction.kind == machine::InstructionKind::Target &&
           instruction.defs.size() == 1 &&
           instruction.defs.front().kind == machine::RegisterKind::Virtual &&
           !instruction.patch && !instruction.has_side_effects &&
           !instruction.may_load && !instruction.may_store;
}

bool same_selected_expression(const machine::Instruction& left,
                              const machine::Instruction& right) {
    return left.opcode == right.opcode &&
           left.defs.front().mode == right.defs.front().mode &&
           left.uses == right.uses && left.operands == right.operands;
}

bool eliminate_redundancy(machine::Function& function,
                          const MachineInstructionPredicate& is_eligible,
                          bool loads) {
    ReplacementMap replacements;
    std::unordered_set<std::uint32_t> removed;
    for (const auto& block : function.blocks) {
        std::vector<const machine::Instruction*> available;
        for (const auto& instruction : block.instructions) {
            if (loads &&
                (instruction.kind != machine::InstructionKind::Target ||
                 instruction.may_store || instruction.has_side_effects ||
                 instruction.patch)) {
                available.clear();
            }
            if (!is_eligible(instruction))
                continue;
            const auto duplicate = std::find_if(
                available.begin(), available.end(),
                [&](const machine::Instruction* candidate) {
                    return same_selected_expression(*candidate, instruction);
                });
            if (duplicate == available.end()) {
                available.push_back(&instruction);
                continue;
            }
            replacements[instruction.defs.front().id] =
                (*duplicate)->defs.front();
            removed.insert(instruction.defs.front().id);
        }
    }
    if (removed.empty())
        return false;
    apply_replacements(function, replacements);
    return erase_replaced_definitions(function, removed, is_eligible);
}

} // namespace

bool schedule_block_layout(
    machine::Function& function,
    const MachineBlockPredicate& is_addressable) {
    if (function.layout.size() < 2) return false;

    const auto original = function.layout;
    std::unordered_set<std::uint32_t> placed;
    std::vector<machine::BlockId> layout;
    layout.reserve(original.size());

    // Compute ordinary block dominance once for trace selection. A path from
    // a nested-loop exit can eventually return to an inner header through the
    // enclosing loop, but it is not the inner loop's continuing path.
    const auto block_count = function.blocks.size();
    std::vector<std::vector<bool>> dominates(
        block_count, std::vector<bool>(block_count, true));
    if (function.entry.value < block_count) {
        std::fill(dominates[function.entry.value].begin(),
                  dominates[function.entry.value].end(), false);
        dominates[function.entry.value][function.entry.value] = true;
    }
    bool dominance_changed = true;
    while (dominance_changed) {
        dominance_changed = false;
        for (const auto& candidate : function.blocks) {
            if (candidate.id == function.entry ||
                candidate.id.value >= block_count) {
                continue;
            }
            std::vector<bool> next(block_count, true);
            if (candidate.predecessors.empty()) {
                std::fill(next.begin(), next.end(), false);
            } else {
                for (const auto predecessor : candidate.predecessors) {
                    if (predecessor.value >= block_count) continue;
                    for (std::size_t index = 0; index < block_count;
                         ++index) {
                        next[index] = next[index] &&
                            dominates[predecessor.value][index];
                    }
                }
            }
            next[candidate.id.value] = true;
            if (next != dominates[candidate.id.value]) {
                dominates[candidate.id.value] = std::move(next);
                dominance_changed = true;
            }
        }
    }

    const auto distance_to = [&](machine::BlockId source,
                                 machine::BlockId target)
        -> std::optional<std::size_t> {
        std::vector<std::pair<machine::BlockId, std::size_t>> pending{
            {source, 0}};
        std::unordered_set<std::uint32_t> visited;
        for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
            const auto [item, distance] = pending[cursor];
            if (item == target) return distance;
            if (!visited.insert(item.value).second ||
                item.value >= block_count) {
                continue;
            }
            for (const auto successor :
                 function.blocks[item.value].successors) {
                pending.emplace_back(successor, distance + 1);
            }
        }
        return std::nullopt;
    };
    const auto natural_back_distance =
        [&](machine::BlockId source, machine::BlockId header)
        -> std::optional<std::size_t> {
        if (source.value >= block_count || header.value >= block_count ||
            !dominates[source.value][header.value]) {
            return std::nullopt;
        }
        std::vector<std::pair<machine::BlockId, std::size_t>> pending{
            {source, 0}};
        std::unordered_set<std::uint32_t> visited;
        for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
            const auto [item, distance] = pending[cursor];
            if (item == header) return distance;
            if (!visited.insert(item.value).second ||
                item.value >= block_count) {
                continue;
            }
            for (const auto successor :
                 function.blocks[item.value].successors) {
                if (successor == header ||
                    (successor.value < block_count &&
                     dominates[successor.value][header.value])) {
                    pending.emplace_back(successor, distance + 1);
                }
            }
        }
        return std::nullopt;
    };
    const auto append_trace = [&](machine::BlockId seed) {
        auto current = seed;
        while (!placed.contains(current.value) &&
               current.value < block_count) {
            placed.insert(current.value);
            layout.push_back(current);
            const auto& owner = function.blocks[current.value];
            std::optional<machine::BlockId> successor;
            if (owner.successors.size() == 1) {
                if (!placed.contains(owner.successors.front().value)) {
                    successor = owner.successors.front();
                }
            } else {
                // A loop's continuing edge is normally much hotter than its
                // exit. Prefer it before the ordinary false-edge heuristic so
                // the body becomes fallthrough.
                const auto ready = [&](machine::BlockId candidate) {
                    if (candidate.value >= block_count) return false;
                    const auto& destination =
                        function.blocks[candidate.value];
                    return destination.predecessors.size() <= 1 ||
                        std::all_of(
                            destination.predecessors.begin(),
                            destination.predecessors.end(),
                            [&](machine::BlockId predecessor) {
                                return predecessor == current ||
                                    placed.contains(predecessor.value);
                            });
                };
                std::optional<std::size_t> shortest_back_path;
                for (auto item = owner.successors.rbegin();
                     item != owner.successors.rend(); ++item) {
                    if (placed.contains(item->value) || !ready(*item)) {
                        continue;
                    }
                    const auto distance =
                        natural_back_distance(*item, current);
                    if (distance &&
                        (!shortest_back_path ||
                         *distance < *shortest_back_path)) {
                        successor = *item;
                        shortest_back_path = *distance;
                    }
                }
                if (!successor) {
                    for (auto item = owner.successors.rbegin();
                         item != owner.successors.rend(); ++item) {
                        if (!placed.contains(item->value) && ready(*item) &&
                            !distance_to(*item, current)) {
                            successor = *item;
                            break;
                        }
                    }
                }
            }
            if (!successor) break;
            current = *successor;
        }
    };

    append_trace(function.entry);
    for (const auto seed : original) append_trace(seed);
    if (layout.size() != original.size()) return false;

    const auto addressable = [&](machine::BlockId id) {
        return is_addressable && is_addressable(id);
    };

    // A shared latch reached by several arms belongs immediately before its
    // dominated header so the backedge becomes fallthrough.
    for (const auto& header : function.blocks) {
        std::optional<machine::BlockId> latch;
        std::size_t latch_inputs{};
        for (const auto& candidate : function.blocks) {
            if (candidate.id == function.entry ||
                candidate.predecessors.size() < 2 ||
                candidate.successors.size() != 1 ||
                candidate.successors.front() != header.id ||
                addressable(candidate.id) ||
                candidate.id.value >= block_count ||
                header.id.value >= block_count ||
                !dominates[candidate.id.value][header.id.value]) {
                continue;
            }
            if (!latch || candidate.predecessors.size() > latch_inputs) {
                latch = candidate.id;
                latch_inputs = candidate.predecessors.size();
            }
        }
        if (!latch) continue;
        auto header_position =
            std::find(layout.begin(), layout.end(), header.id);
        auto latch_position = std::find(layout.begin(), layout.end(), *latch);
        if (header_position == layout.end() ||
            latch_position == layout.end() ||
            latch_position + 1 == header_position) {
            continue;
        }
        const auto latch_id = *latch_position;
        layout.erase(latch_position);
        header_position = std::find(layout.begin(), layout.end(), header.id);
        layout.insert(header_position, latch_id);
    }

    // Rotate a test-first, single-body loop in layout only. The external
    // predecessor jumps to the test once; thereafter the body falls through
    // to the test and its conditional branch forms the backedge.
    for (std::size_t index = 0; index + 1 < layout.size(); ++index) {
        const auto header_id = layout[index];
        if (header_id == function.entry || header_id.value >= block_count) {
            continue;
        }
        const auto& header = function.blocks[header_id.value];
        if (header.successors.size() != 2) continue;
        const auto body_id = layout[index + 1];
        if (body_id.value >= block_count ||
            std::find(header.successors.begin(), header.successors.end(),
                      body_id) == header.successors.end()) {
            continue;
        }
        const auto& body = function.blocks[body_id.value];
        const bool has_dedicated_preheader = std::any_of(
            header.predecessors.begin(), header.predecessors.end(),
            [&](machine::BlockId predecessor) {
                if (predecessor == body_id ||
                    predecessor.value >= block_count) {
                    return false;
                }
                const auto& owner = function.blocks[predecessor.value];
                return owner.successors.size() == 1 &&
                    owner.successors.front() == header_id;
            });
        if (body.predecessors.size() != 1 ||
            body.predecessors.front() != header_id ||
            body.successors.size() != 1 ||
            body.successors.front() != header_id || addressable(body_id)) {
            continue;
        }
        if (!has_dedicated_preheader) continue;
        std::swap(layout[index], layout[index + 1]);
        ++index;
    }

    // Keep the non-continuing successor next to the rotated test when it is a
    // movable single-predecessor block.
    for (std::size_t index = 1; index + 1 < layout.size(); ++index) {
        const auto body_id = layout[index - 1];
        const auto header_id = layout[index];
        if (body_id.value >= block_count || header_id.value >= block_count) {
            continue;
        }
        const auto& body = function.blocks[body_id.value];
        const auto& header = function.blocks[header_id.value];
        if (body.successors.size() != 1 ||
            body.successors.front() != header_id ||
            header.successors.size() != 2 ||
            std::find(header.successors.begin(), header.successors.end(),
                      body_id) == header.successors.end()) {
            continue;
        }
        const auto exit = header.successors.front() == body_id
            ? header.successors.back()
            : header.successors.front();
        if (layout[index + 1] == exit || addressable(exit) ||
            exit.value >= block_count) {
            continue;
        }
        const auto& exit_block = function.blocks[exit.value];
        if (exit_block.predecessors.size() != 1 ||
            exit_block.predecessors.front() != header_id) {
            continue;
        }
        const auto exit_position =
            std::find(layout.begin() +
                          static_cast<std::ptrdiff_t>(index + 1),
                      layout.end(), exit);
        if (exit_position == layout.end()) continue;
        const auto exit_id = *exit_position;
        layout.erase(exit_position);
        layout.insert(layout.begin() +
                          static_cast<std::ptrdiff_t>(index + 1),
                      exit_id);
    }

    if (layout == original) return false;
    function.layout = std::move(layout);
    return true;
}

bool propagate_virtual_register_copies(
    machine::Function& function, const MachineInstructionPredicate& is_copy) {
    ReplacementMap replacements;
    std::unordered_set<std::uint32_t> removed;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (!is_copy(instruction) ||
                instruction.kind != machine::InstructionKind::Target ||
                instruction.defs.size() != 1 || instruction.uses.size() != 1 ||
                instruction.patch || instruction.has_side_effects ||
                instruction.may_load || instruction.may_store) {
                continue;
            }
            const auto target = instruction.defs.front();
            const auto source =
                resolve_replacement(instruction.uses.front(), replacements);
            if (target.kind != machine::RegisterKind::Virtual ||
                source.kind != machine::RegisterKind::Virtual ||
                target.mode != source.mode ||
                target.id >= function.virtual_register_classes.size() ||
                source.id >= function.virtual_register_classes.size() ||
                function.virtual_register_classes[target.id] !=
                    function.virtual_register_classes[source.id]) {
                continue;
            }
            replacements[target.id] = source;
            removed.insert(target.id);
        }
    }
    if (removed.empty())
        return false;
    apply_replacements(function, replacements);
    return erase_replaced_definitions(function, removed, is_copy);
}

bool eliminate_redundant_expressions(
    machine::Function& function,
    const MachineInstructionPredicate& is_eligible) {
    const auto eligible = [&](const machine::Instruction& instruction) {
        return structurally_pure_single_definition(instruction) &&
               is_eligible(instruction);
    };
    return eliminate_redundancy(function, eligible, false);
}

bool eliminate_redundant_loads(machine::Function& function,
                               const MachineInstructionPredicate& is_eligible) {
    const auto eligible = [&](const machine::Instruction& instruction) {
        return instruction.kind == machine::InstructionKind::Target &&
               instruction.may_load && !instruction.may_store &&
               !instruction.has_side_effects && !instruction.patch &&
               instruction.defs.size() == 1 &&
               instruction.defs.front().kind ==
                   machine::RegisterKind::Virtual &&
               is_eligible(instruction);
    };
    return eliminate_redundancy(function, eligible, true);
}

bool eliminate_dead_definitions(
    machine::Function& function,
    const MachineRegisterPredicate& definition_is_observable) {
    const auto count = function.virtual_registers.size();
    bool any_changed = false;
    bool changed = true;
    while (changed) {
        changed = false;
        std::vector<unsigned> uses(count);
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& use : instruction.uses) {
                    if (use.kind == machine::RegisterKind::Virtual &&
                        use.id < uses.size()) {
                        ++uses[use.id];
                    }
                }
                for (const auto& live : instruction.live_across_call) {
                    if (live.kind == machine::RegisterKind::Virtual &&
                        live.id < uses.size()) {
                        ++uses[live.id];
                    }
                }
            }
        }
        for (auto& block : function.blocks) {
            const auto before = block.instructions.size();
            std::erase_if(
                block.instructions,
                [&](const machine::Instruction& instruction) {
                    if (instruction.kind != machine::InstructionKind::Target ||
                        instruction.may_load || instruction.may_store ||
                        instruction.has_side_effects || instruction.patch ||
                        instruction.defs.empty()) {
                        return false;
                    }
                    if (definition_is_observable &&
                        std::any_of(
                            instruction.defs.begin(), instruction.defs.end(),
                            [&](const machine::Register& definition) {
                                return definition_is_observable(definition);
                            })) {
                        return false;
                    }
                    return std::all_of(
                        instruction.defs.begin(), instruction.defs.end(),
                        [&](const machine::Register& definition) {
                            return definition.kind ==
                                       machine::RegisterKind::Virtual &&
                                   definition.id < uses.size() &&
                                   uses[definition.id] == 0;
                        });
                });
            const bool block_changed = block.instructions.size() != before;
            changed = changed || block_changed;
            any_changed = any_changed || block_changed;
        }
    }
    return any_changed;
}

bool fuse_division_results(machine::Function& function,
                           const DivisionClassifier& classify,
                           machine::TargetOpcodeId signed_combined,
                           machine::TargetOpcodeId unsigned_combined) {
    bool changed = false;
    for (auto& block : function.blocks) {
        std::vector<bool> removed(block.instructions.size());
        // Pending candidates by signedness and operands, in block order.
        std::vector<std::size_t> pending;
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const auto& instruction = block.instructions[index];
            const auto kind = classify(instruction);
            if (!kind || instruction.uses.size() != 2 || instruction.defs.size() != 1)
                continue;
            const auto partner = std::find_if(pending.begin(), pending.end(),
                [&](std::size_t first) {
                    const auto& candidate = block.instructions[first];
                    const auto other = classify(candidate);
                    return other->quotient != kind->quotient &&
                           other->is_signed == kind->is_signed &&
                           candidate.uses == instruction.uses;
                });
            if (partner == pending.end()) {
                pending.push_back(index);
                continue;
            }
            auto& first = block.instructions[*partner];
            const auto quotient = kind->quotient ? instruction.defs.front() : first.defs.front();
            const auto remainder = kind->quotient ? first.defs.front() : instruction.defs.front();
            first.opcode = kind->is_signed ? signed_combined : unsigned_combined;
            first.defs = {quotient, remainder};
            removed[index] = true;
            pending.erase(partner);
            changed = true;
        }
        if (std::find(removed.begin(), removed.end(), true) == removed.end()) continue;
        std::vector<machine::Instruction> kept;
        kept.reserve(block.instructions.size());
        for (std::size_t index = 0; index < block.instructions.size(); ++index)
            if (!removed[index]) kept.push_back(std::move(block.instructions[index]));
        block.instructions = std::move(kept);
    }
    return changed;
}

std::size_t hoist_entry_captures(machine::Function& function,
                                 const MachineInstructionPredicate& is_capture) {
    const auto entry = std::find_if(
        function.blocks.begin(), function.blocks.end(),
        [&](const machine::Block& block) {
            return block.id == function.entry;
        });
    if (entry == function.blocks.end()) return 0;
    std::vector<machine::Instruction> captures;
    for (auto& block : function.blocks) {
        auto write = block.instructions.begin();
        for (auto read = block.instructions.begin();
             read != block.instructions.end(); ++read) {
            if (is_capture(*read)) {
                captures.push_back(std::move(*read));
            } else {
                if (write != read) *write = std::move(*read);
                ++write;
            }
        }
        block.instructions.erase(write, block.instructions.end());
    }
    entry->instructions.insert(entry->instructions.begin(),
                               std::make_move_iterator(captures.begin()),
                               std::make_move_iterator(captures.end()));
    return captures.size();
}

bool elide_unused_virtual_spill_slots(machine::Function& function) {
    std::vector<bool> referenced(function.virtual_registers.size());
    const auto mark = [&](const machine::Register& value) {
        if (value.kind == machine::RegisterKind::Virtual &&
            value.id < referenced.size()) {
            referenced[value.id] = true;
        }
    };
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            for (const auto& definition : instruction.defs) mark(definition);
            for (const auto& use : instruction.uses) mark(use);
            for (const auto& live : instruction.live_across_call) mark(live);
            for (const auto& operand : instruction.operands) {
                if (const auto* reg =
                        std::get_if<machine::RegisterOperand>(&operand)) {
                    mark(reg->value);
                }
            }
        }
    }

    bool changed = false;
    for (auto& slot : function.stack_slots) {
        if (!slot.spill_for || slot.spill_for->value >= referenced.size() ||
            referenced[slot.spill_for->value]) {
            continue;
        }
        if (slot.elided && !slot.frame_offset) continue;
        slot.elided = true;
        slot.frame_offset.reset();
        changed = true;
    }
    return changed;
}

} // namespace cross::native
