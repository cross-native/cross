// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_transform.hpp"

#include "common/control_flow.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
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

Dominance block_dominance(const machine::Function& function) {
    return Dominance(
        function.blocks.size(), function.entry.value,
        [&](std::uint32_t block) -> const std::vector<machine::BlockId>& {
            return function.blocks[block].successors;
        },
        [&](std::uint32_t block) -> const std::vector<machine::BlockId>& {
            return function.blocks[block].predecessors;
        });
}

// Natural loops, one per header, holding every block that reaches one of
// the header's back edges without passing through the header.
struct NaturalLoop {
    std::uint32_t header{};
    std::vector<bool> members;
    std::size_t size{};
};

std::vector<NaturalLoop> natural_loops(const machine::Function& function,
                                       const Dominance& dominance) {
    const auto count = function.blocks.size();
    std::vector<NaturalLoop> loops;
    std::vector<std::optional<std::size_t>> header_loop(count);
    for (std::uint32_t source = 0; source < count; ++source) {
        if (!dominance.reachable(source)) continue;
        for (const auto successor : function.blocks[source].successors) {
            const auto header = successor.value;
            if (header >= count || !dominance.dominates(header, source)) {
                continue;
            }
            if (!header_loop[header]) {
                header_loop[header] = loops.size();
                loops.push_back({header, std::vector<bool>(count), 1});
                loops.back().members[header] = true;
            }
            auto& loop = loops[*header_loop[header]];
            std::vector<std::uint32_t> pending{source};
            while (!pending.empty()) {
                const auto block = pending.back();
                pending.pop_back();
                if (loop.members[block]) continue;
                loop.members[block] = true;
                ++loop.size;
                for (const auto predecessor :
                     function.blocks[block].predecessors) {
                    if (dominance.reachable(predecessor.value)) {
                        pending.push_back(predecessor.value);
                    }
                }
            }
        }
    }
    return loops;
}

// The block targets of a branch, in operand order.
std::vector<machine::BlockId> branch_targets(
    const machine::Instruction& instruction) {
    std::vector<machine::BlockId> targets;
    for (const auto& operand : instruction.operands) {
        if (const auto* block = std::get_if<machine::BlockOperand>(&operand)) {
            targets.push_back(block->target);
        }
    }
    return targets;
}

} // namespace

bool duplicate_branch_blocks(machine::Function& function,
                             const BranchDuplicationPolicy& policy) {
    const auto count = function.blocks.size();
    if (count < 2 || function.layout.size() != count) return false;
    for (std::uint32_t block = 0; block < count; ++block) {
        if (function.blocks[block].id.value != block) return false;
    }
    const auto addressable = [&](std::uint32_t block) {
        return policy.is_addressable && policy.is_addressable({block});
    };
    // The block laid out after `block`, if any.
    const auto next = [&](std::uint32_t block) -> std::optional<std::uint32_t> {
        const auto at = std::find(function.layout.begin(),
                                  function.layout.end(),
                                  machine::BlockId{block});
        if (at == function.layout.end() || at + 1 == function.layout.end()) {
            return std::nullopt;
        }
        return (at + 1)->value;
    };
    const auto jump_target = [&](std::uint32_t block)
        -> std::optional<std::uint32_t> {
        const auto& instructions = function.blocks[block].instructions;
        if (instructions.empty() ||
            instructions.back().kind != machine::InstructionKind::Branch) {
            return std::nullopt;
        }
        const auto targets = branch_targets(instructions.back());
        if (targets.size() != 1) return std::nullopt;
        return targets.front().value;
    };
    // A block of PHIs and code-free instructions ending in a conditional
    // branch to two other blocks.
    const auto tests_only = [&](std::uint32_t block) {
        const auto& value = function.blocks[block];
        if (block == function.entry.value || addressable(block) ||
            function.frame.prologue_block == value.id ||
            value.instructions.empty() ||
            value.instructions.back().kind !=
                machine::InstructionKind::ConditionalBranch) {
            return false;
        }
        for (std::size_t index = 0; index + 1 < value.instructions.size();
             ++index) {
            const auto& instruction = value.instructions[index];
            if (!policy.is_phi(instruction) &&
                !policy.emits_nothing(instruction)) {
                return false;
            }
        }
        const auto targets = branch_targets(value.instructions.back());
        return targets.size() == 2 && targets[0] != targets[1] &&
            targets[0].value != block && targets[1].value != block &&
            targets[0].value < count && targets[1].value < count;
    };

    // Whether `source` reaches `header` by a back edge and `inside`, a
    // successor of the header, lies in that loop while its other successor
    // leaves it.
    const auto looping = [&](std::uint32_t header, std::uint32_t source,
                             std::uint32_t inside) {
        const auto dominance = block_dominance(function);
        if (!dominance.dominates(header, source)) return false;
        for (const auto& loop : natural_loops(function, dominance)) {
            if (loop.header != header) continue;
            const auto targets = function.blocks[header].successors;
            return std::all_of(targets.begin(), targets.end(),
                               [&](machine::BlockId block) {
                                   return loop.members[block.value] ==
                                       (block.value == inside);
                               });
        }
        return false;
    };

    bool changed = false;
    for (std::size_t round = 0; round < count; ++round) {
        bool duplicated = false;
        for (std::uint32_t tested = 0; tested < count && !duplicated;
             ++tested) {
            if (!tests_only(tested)) continue;
            const auto& branch = function.blocks[tested].instructions.back();
            const auto targets = branch_targets(branch);
            const auto target = [&](std::optional<std::uint32_t> block) {
                return block && (*block == targets[0].value ||
                                 *block == targets[1].value);
            };
            // Another predecessor keeps the tested block reachable.
            const auto predecessors = function.blocks[tested].predecessors;
            if (predecessors.size() < 2) continue;
            for (const auto source_id : predecessors) {
                const auto source = source_id.value;
                if (source >= count || source == tested ||
                    jump_target(source) != tested ||
                    !policy.copy_free(source_id, {tested})) {
                    continue;
                }
                // Only a loop test that falls through into its loop and
                // branches out of it wastes its delay slot on the loop's
                // path; one laid out after its loop already branches back
                // with a useful slot.
                const auto after_tested = next(tested);
                if (next(source) != tested || !target(after_tested) ||
                    !looping(tested, source, *after_tested)) {
                    continue;
                }
                // The tested block moves after another block that jumps to
                // it and is laid out before one of its targets, so that
                // block no longer jumps and the tested one still falls
                // through.
                std::optional<std::uint32_t> host;
                for (const auto other : predecessors) {
                    if (other.value != source && other.value < count &&
                        jump_target(other.value) == tested &&
                        next(other.value) != tested &&
                        target(next(other.value))) {
                        host = other.value;
                        break;
                    }
                }
                if (!host) continue;

                // The source passes its own incoming value where the tested
                // block's PHI result would flow.
                std::unordered_map<std::uint32_t, machine::Register> incoming;
                for (const auto& instruction :
                     function.blocks[tested].instructions) {
                    if (!policy.is_phi(instruction) ||
                        instruction.defs.size() != 1) {
                        continue;
                    }
                    for (std::size_t index = 0;
                         index + 1 < instruction.operands.size();
                         index += 2) {
                        const auto* from = std::get_if<machine::BlockOperand>(
                            &instruction.operands[index]);
                        const auto* value =
                            std::get_if<machine::RegisterOperand>(
                                &instruction.operands[index + 1]);
                        if (from && value && from->target.value == source) {
                            incoming.emplace(instruction.defs.front().id,
                                             value->value);
                        }
                    }
                }
                const auto rename = [&](machine::Register value) {
                    if (value.kind != machine::RegisterKind::Virtual) {
                        return value;
                    }
                    const auto found = incoming.find(value.id);
                    return found == incoming.end() ? value : found->second;
                };
                auto copy = branch;
                for (auto& use : copy.uses) use = rename(use);
                for (auto& operand : copy.operands) {
                    if (auto* value =
                            std::get_if<machine::RegisterOperand>(&operand)) {
                        value->value = rename(value->value);
                    }
                }
                auto& source_block = function.blocks[source];
                source_block.instructions.back() = std::move(copy);
                source_block.successors = targets;
                for (const auto successor : targets) {
                    auto& block = function.blocks[successor.value];
                    block.predecessors.push_back(source_id);
                    for (auto& instruction : block.instructions) {
                        if (!policy.is_phi(instruction)) continue;
                        for (std::size_t index = 0;
                             index + 1 < instruction.operands.size();
                             index += 2) {
                            const auto* from =
                                std::get_if<machine::BlockOperand>(
                                    &instruction.operands[index]);
                            const auto* value =
                                std::get_if<machine::RegisterOperand>(
                                    &instruction.operands[index + 1]);
                            if (!from || !value ||
                                from->target.value != tested) {
                                continue;
                            }
                            const machine::Operand edge =
                                machine::BlockOperand{source_id};
                            const machine::Operand passed =
                                machine::RegisterOperand{
                                    rename(value->value)};
                            instruction.operands.push_back(edge);
                            instruction.operands.push_back(passed);
                            break;
                        }
                    }
                }
                auto& tested_block = function.blocks[tested];
                std::erase(tested_block.predecessors, source_id);
                for (auto& instruction : tested_block.instructions) {
                    if (!policy.is_phi(instruction)) continue;
                    for (std::size_t index = 0;
                         index + 1 < instruction.operands.size();
                         index += 2) {
                        const auto* from = std::get_if<machine::BlockOperand>(
                            &instruction.operands[index]);
                        if (from && from->target.value == source) {
                            const auto at = instruction.operands.begin() +
                                static_cast<std::ptrdiff_t>(index);
                            instruction.operands.erase(at, at + 2);
                            break;
                        }
                    }
                }
                std::erase(function.layout, machine::BlockId{tested});
                const auto at = std::find(function.layout.begin(),
                                          function.layout.end(),
                                          machine::BlockId{*host});
                function.layout.insert(at + 1, machine::BlockId{tested});
                duplicated = true;
                changed = true;
                break;
            }
        }
        if (!duplicated) break;
    }
    return changed;
}

std::vector<unsigned> loop_depths(const machine::Function& function) {
    std::vector<unsigned> depths(function.blocks.size());
    if (function.entry.value >= function.blocks.size()) return depths;
    for (const auto& loop : natural_loops(function, block_dominance(function))) {
        for (std::size_t block = 0; block < depths.size(); ++block) {
            if (loop.members[block]) ++depths[block];
        }
    }
    return depths;
}

namespace {

// The live virtual registers of each bank at every point of a function,
// from a backward liveness solution in which a PHI's uses are live out of
// the predecessor they arrive from. A block's points are its end, each
// instruction (whose definitions occupy colors even when dead), and the
// point before each instruction. For each register of `tracked`, the
// points where it is live or being defined.
struct PressurePoints {
    std::vector<std::vector<unsigned>> counts;
    std::vector<std::uint32_t> block;
    // Each block's first point, its end; its terminator's two follow.
    std::vector<std::size_t> first;
    std::unordered_map<std::uint32_t, std::vector<bool>> live;
    // Each call's block and the values of each bank live across it.
    std::vector<std::pair<std::uint32_t, std::vector<unsigned>>> calls;
};

PressurePoints pressure_points(const machine::Function& function,
                               const LoopConstantPolicy& policy,
                               const std::unordered_set<std::uint32_t>&
                                   tracked_ids) {
    const auto count = function.blocks.size();
    const auto banks = policy.capacity.size();
    std::vector<std::optional<unsigned>> bank_of(
        function.virtual_registers.size());
    for (std::size_t id = 0; id < bank_of.size(); ++id) {
        bank_of[id] = policy.bank(machine::Register::virtual_register(
            {static_cast<std::uint32_t>(id)}, function.virtual_registers[id]));
        if (bank_of[id] && *bank_of[id] >= banks) bank_of[id].reset();
    }
    const auto tracked = [&](const machine::Register& value) {
        return value.kind == machine::RegisterKind::Virtual &&
               value.id < bank_of.size() && bank_of[value.id].has_value();
    };
    using LiveSet = std::unordered_set<std::uint32_t>;
    std::vector<LiveSet> uses(count), defs(count), live_in(count),
        live_out(count);
    // Values each block's PHIs take from a predecessor, by predecessor.
    std::vector<std::unordered_map<std::uint32_t, LiveSet>> edge_uses(count);
    for (std::uint32_t block = 0; block < count; ++block) {
        for (const auto& instruction : function.blocks[block].instructions) {
            if (policy.is_phi(instruction)) {
                for (std::size_t index = 0;
                     index + 1 < instruction.operands.size(); index += 2) {
                    const auto* from = std::get_if<machine::BlockOperand>(
                        &instruction.operands[index]);
                    const auto* value = std::get_if<machine::RegisterOperand>(
                        &instruction.operands[index + 1]);
                    if (from && value && tracked(value->value)) {
                        edge_uses[block][from->target.value].insert(
                            value->value.id);
                    }
                }
            } else {
                for (const auto& use : instruction.uses) {
                    if (tracked(use) && !defs[block].contains(use.id)) {
                        uses[block].insert(use.id);
                    }
                }
            }
            for (const auto& definition : instruction.defs) {
                if (tracked(definition)) defs[block].insert(definition.id);
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::uint32_t block = static_cast<std::uint32_t>(count);
             block-- > 0;) {
            LiveSet out;
            for (const auto successor : function.blocks[block].successors) {
                if (successor.value >= count) continue;
                out.insert(live_in[successor.value].begin(),
                           live_in[successor.value].end());
                const auto& from = edge_uses[successor.value];
                if (const auto found = from.find(block); found != from.end()) {
                    out.insert(found->second.begin(), found->second.end());
                }
            }
            LiveSet in = uses[block];
            for (const auto id : out) {
                if (!defs[block].contains(id)) in.insert(id);
            }
            if (out != live_out[block] || in != live_in[block]) {
                live_out[block] = std::move(out);
                live_in[block] = std::move(in);
                changed = true;
            }
        }
    }
    PressurePoints points;
    for (const auto id : tracked_ids) points.live[id];
    const auto record = [&](std::uint32_t block,
                            const std::vector<unsigned>& counts,
                            const LiveSet& live,
                            const machine::Instruction* defining) {
        points.counts.push_back(counts);
        points.block.push_back(block);
        for (auto& [id, at] : points.live) {
            bool present = live.contains(id);
            if (defining) {
                for (const auto& definition : defining->defs) {
                    present = present || (definition.kind ==
                                              machine::RegisterKind::Virtual &&
                                          definition.id == id);
                }
            }
            at.push_back(present);
        }
    };
    for (std::uint32_t block = 0; block < count; ++block) {
        auto live = live_out[block];
        std::vector<unsigned> current(banks);
        for (const auto id : live) ++current[*bank_of[id]];
        points.first.push_back(points.counts.size());
        record(block, current, live, nullptr);
        const auto& instructions = function.blocks[block].instructions;
        for (auto instruction = instructions.rbegin();
             instruction != instructions.rend(); ++instruction) {
            // Definitions occupy colors at their instruction even when dead.
            auto at = current;
            for (const auto& definition : instruction->defs) {
                if (tracked(definition) && !live.contains(definition.id)) {
                    ++at[*bank_of[definition.id]];
                }
            }
            record(block, at, live, &*instruction);
            for (const auto& definition : instruction->defs) {
                if (tracked(definition) && live.erase(definition.id)) {
                    --current[*bank_of[definition.id]];
                }
            }
            if (instruction->kind == machine::InstructionKind::Call) {
                points.calls.emplace_back(block, current);
            }
            if (!policy.is_phi(*instruction)) {
                for (const auto& use : instruction->uses) {
                    if (tracked(use) && live.insert(use.id).second) {
                        ++current[*bank_of[use.id]];
                    }
                }
            }
            record(block, current, live, nullptr);
        }
    }
    return points;
}

bool control_transfer(const machine::Instruction& instruction) {
    switch (instruction.kind) {
    case machine::InstructionKind::Branch:
    case machine::InstructionKind::ConditionalBranch:
    case machine::InstructionKind::IndirectBranch:
    case machine::InstructionKind::Return:
    case machine::InstructionKind::Unreachable:
    case machine::InstructionKind::Trap: return true;
    default: return false;
    }
}

} // namespace

bool hoist_loop_constants(machine::Function& function,
                          const LoopConstantPolicy& policy) {
    const auto count = function.blocks.size();
    if (count < 2 || function.entry.value >= count) return false;
    for (std::uint32_t block = 0; block < count; ++block) {
        if (function.blocks[block].id.value != block) return false;
    }
    const auto dominance = block_dominance(function);
    auto loops = natural_loops(function, dominance);
    std::stable_sort(loops.begin(), loops.end(),
                     [](const NaturalLoop& left, const NaturalLoop& right) {
                         return left.size < right.size;
                     });
    // A constant that only PHIs read still costs a copy on every edge, and
    // one that only calls read a copy into its argument register.
    std::vector<bool> operand_use(function.virtual_registers.size());
    std::vector<bool> operation_use(function.virtual_registers.size());
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (policy.is_phi(instruction)) continue;
            for (const auto& use : instruction.uses) {
                if (use.kind == machine::RegisterKind::Virtual &&
                    use.id < operand_use.size()) {
                    operand_use[use.id] = true;
                    operation_use[use.id] = operation_use[use.id] ||
                        instruction.kind != machine::InstructionKind::Call;
                }
            }
        }
    }
    const auto constant = [&](const machine::Instruction& instruction) {
        return structurally_pure_single_definition(instruction) &&
               instruction.uses.empty() && policy.is_constant(instruction) &&
               instruction.defs.front().id < operand_use.size() &&
               operand_use[instruction.defs.front().id] &&
               policy.bank(instruction.defs.front()).has_value();
    };
    std::unordered_set<std::uint32_t> constant_ids;
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (constant(instruction)) {
                constant_ids.insert(instruction.defs.front().id);
            }
        }
    }
    // A hoisted constant is live at every point of the loop it leaves and
    // at the end of the loop's preheader, so it adds a register at each of
    // them where it was not already live. Constants are hoisted while
    // every such point stays within the bank's limit: its capacity, or the
    // function's peak when that is higher but still within the bank's
    // colors, since a point that stays below the peak needs no new color.
    auto points = pressure_points(function, policy, constant_ids);
    std::vector<unsigned> limit = policy.capacity;
    for (std::size_t bank = 0; bank < limit.size(); ++bank) {
        unsigned peak = 0;
        for (const auto& at : points.counts) peak = std::max(peak, at[bank]);
        if (bank < policy.colors.size() && peak <= policy.colors[bank]) {
            limit[bank] = std::max(limit[bank], peak);
        }
    }
    bool changed = false;
    for (const auto& loop : loops) {
        std::optional<std::uint32_t> preheader;
        bool unique = true;
        for (const auto predecessor :
             function.blocks[loop.header].predecessors) {
            if (predecessor.value >= count ||
                loop.members[predecessor.value]) {
                continue;
            }
            if (preheader && *preheader != predecessor.value) unique = false;
            preheader = predecessor.value;
        }
        if (!preheader || !unique ||
            function.blocks[*preheader].instructions.empty() ||
            !control_transfer(function.blocks[*preheader].instructions.back())) {
            continue;
        }
        bool calls = false;
        bool candidates = false;
        for (std::uint32_t block = 0; block < count; ++block) {
            if (!loop.members[block]) continue;
            for (const auto& instruction : function.blocks[block].instructions) {
                calls = calls ||
                        instruction.kind == machine::InstructionKind::Call;
                candidates = candidates || constant(instruction);
            }
        }
        if (!candidates) continue;
        // A constant hoisted out of a loop with calls lives across them in
        // a call-preserved color. The function already preserves as many
        // as the values live across its busiest call; those that the
        // loop's calls leave free cost nothing more.
        std::vector<unsigned> preserved_free(limit.size());
        if (calls) {
            std::vector<unsigned> busiest(limit.size());
            std::vector<unsigned> busiest_in_loop(limit.size());
            for (const auto& [block, across] : points.calls) {
                for (std::size_t bank = 0; bank < across.size(); ++bank) {
                    busiest[bank] = std::max(busiest[bank], across[bank]);
                    if (loop.members[block]) {
                        busiest_in_loop[bank] =
                            std::max(busiest_in_loop[bank], across[bank]);
                    }
                }
            }
            for (std::size_t bank = 0; bank < limit.size(); ++bank) {
                preserved_free[bank] = busiest[bank] - busiest_in_loop[bank];
            }
        }

        // The loop's points, and the preheader's end and terminator, before
        // which the constants go.
        std::vector<std::size_t> region;
        for (std::size_t point = 0; point < points.block.size(); ++point) {
            if (loop.members[points.block[point]]) region.push_back(point);
        }
        for (std::size_t point = points.first[*preheader];
             point < points.first[*preheader] + 3; ++point) {
            region.push_back(point);
        }
        std::vector<std::vector<unsigned>> added(
            region.size(), std::vector<unsigned>(limit.size()));
        auto& target = function.blocks[*preheader].instructions;
        ReplacementMap replacements;
        std::vector<machine::Instruction> hoisted;
        for (std::uint32_t block = 0; block < count; ++block) {
            if (!loop.members[block]) continue;
            auto& instructions = function.blocks[block].instructions;
            for (auto instruction = instructions.begin();
                 instruction != instructions.end();) {
                if (!constant(*instruction)) {
                    ++instruction;
                    continue;
                }
                const auto equal = [&](const machine::Instruction& other) {
                    return same_selected_expression(other, *instruction) &&
                           policy.bank(other.defs.front()) ==
                               policy.bank(instruction->defs.front());
                };
                const auto merged = std::find_if(hoisted.begin(),
                                                 hoisted.end(), equal);
                if (merged != hoisted.end()) {
                    replacements[instruction->defs.front().id] =
                        merged->defs.front();
                    instruction = instructions.erase(instruction);
                    continue;
                }
                const auto bank = *policy.bank(instruction->defs.front());
                const auto& live =
                    points.live.at(instruction->defs.front().id);
                bool fits = !calls ||
                    (preserved_free[bank] != 0 &&
                     operation_use[instruction->defs.front().id]);
                for (std::size_t index = 0; index < region.size() && fits;
                     ++index) {
                    const auto point = region[index];
                    fits = points.counts[point][bank] + added[index][bank] +
                            (live[point] ? 0U : 1U) <= limit[bank];
                }
                if (!fits) {
                    ++instruction;
                    continue;
                }
                for (std::size_t index = 0; index < region.size(); ++index) {
                    if (!live[region[index]]) ++added[index][bank];
                }
                if (calls) --preserved_free[bank];
                hoisted.push_back(std::move(*instruction));
                instruction = instructions.erase(instruction);
            }
        }
        if (hoisted.empty() && replacements.empty()) continue;
        target.insert(target.end() - 1,
                      std::make_move_iterator(hoisted.begin()),
                      std::make_move_iterator(hoisted.end()));
        if (!replacements.empty()) apply_replacements(function, replacements);
        changed = true;
        // Outer loops see the constants in this loop's preheader.
        points = pressure_points(function, policy, constant_ids);
    }
    return changed;
}

bool schedule_block_layout(
    machine::Function& function,
    const MachineBlockPredicate& is_addressable,
    bool late_conditional_back_edges) {
    const auto count = function.blocks.size();
    const auto entry = function.entry.value;
    if (function.layout.size() < 2 || entry >= count) return false;
    const auto addressable = [&](std::uint32_t block) {
        return is_addressable && is_addressable({block});
    };
    std::vector<bool> placeable(count);
    std::vector<std::size_t> position(count, count);
    for (std::size_t index = 0; index < function.layout.size(); ++index) {
        const auto block = function.layout[index].value;
        if (block >= count) return false;
        placeable[block] = true;
        position[block] = index;
    }
    // Distinct successors in branch order. A block can fall through only
    // to the target of its branch or conditional branch.
    std::vector<std::vector<std::uint32_t>> successors(count);
    std::vector<bool> falls_through(count);
    std::vector<bool> traps(count);
    for (const auto& block : function.blocks) {
        const auto id = block.id.value;
        if (id >= count) return false;
        for (const auto successor : block.successors) {
            auto& list = successors[id];
            if (successor.value < count &&
                std::find(list.begin(), list.end(), successor.value) ==
                    list.end()) {
                list.push_back(successor.value);
            }
        }
        const auto* last = block.instructions.empty()
            ? nullptr : &block.instructions.back();
        falls_through[id] =
            successors[id].size() <= 2 &&
            (!last || last->kind != machine::InstructionKind::IndirectBranch);
        traps[id] = block.successors.empty() && last &&
            (last->kind == machine::InstructionKind::Trap ||
             last->kind == machine::InstructionKind::Unreachable);
    }

    const auto dominance = block_dominance(function);
    const auto back_edge = [&](std::uint32_t source, std::uint32_t target) {
        return dominance.dominates(target, source);
    };
    const auto loops = natural_loops(function, dominance);
    std::vector<std::optional<std::size_t>> header_loop(count);
    for (std::size_t index = 0; index < loops.size(); ++index) {
        header_loop[loops[index].header] = index;
    }
    std::vector<std::optional<std::size_t>> innermost(count);
    for (std::size_t index = 0; index < loops.size(); ++index) {
        for (std::uint32_t block = 0; block < count; ++block) {
            if (loops[index].members[block] &&
                (!innermost[block] ||
                 loops[index].size < loops[*innermost[block]].size)) {
                innermost[block] = index;
            }
        }
    }

    // Static branch probabilities (Ball and Larus; LLVM's
    // BranchProbabilityInfo) in units of 2^-20: an edge that stays in the
    // innermost loop is taken 31 times in 32 against one that leaves it, an
    // edge into a block that traps almost never, and other edges evenly.
    // Integers keep the layout independent of the host's floating point.
    constexpr std::uint64_t certain = std::uint64_t{1} << 20;
    constexpr std::uint64_t leave = certain / 32;
    constexpr std::uint64_t cold = 1;
    std::vector<std::vector<std::uint64_t>> probability(count);
    for (std::uint32_t block = 0; block < count; ++block) {
        const auto& list = successors[block];
        probability[block].assign(
            list.size(), list.empty() ? 0 : certain / list.size());
        if (list.size() != 2) continue;
        auto& odds = probability[block];
        if (traps[list[0]] != traps[list[1]]) {
            const auto unlikely = traps[list[0]] ? 0U : 1U;
            odds[unlikely] = cold;
            odds[1U - unlikely] = certain - cold;
        } else if (innermost[block]) {
            const auto& loop = loops[*innermost[block]];
            if (loop.members[list[0]] != loop.members[list[1]]) {
                const auto leaving = loop.members[list[0]] ? 1U : 0U;
                odds[leaving] = leave;
                odds[1U - leaving] = certain - leave;
            }
        }
    }
    const auto edge_probability = [&](std::uint32_t source,
                                      std::uint32_t target) {
        const auto& list = successors[source];
        const auto found = std::find(list.begin(), list.end(), target);
        return found == list.end()
            ? std::uint64_t{0}
            : probability[source][static_cast<std::size_t>(
                  found - list.begin())];
    };

    // Block frequencies in units of 2^-20 entries, in reverse postorder; a
    // loop header runs once per expected iteration of its loop. They
    // saturate at 2^43, so a frequency times a probability fits 64 bits.
    std::vector<std::uint32_t> order;
    {
        std::vector<bool> seen(count);
        std::vector<std::pair<std::uint32_t, std::size_t>> stack{{entry, 0}};
        seen[entry] = true;
        while (!stack.empty()) {
            auto& [block, next] = stack.back();
            if (next < successors[block].size()) {
                const auto successor = successors[block][next++];
                if (!seen[successor]) {
                    seen[successor] = true;
                    stack.push_back({successor, 0});
                }
                continue;
            }
            order.push_back(block);
            stack.pop_back();
        }
        std::reverse(order.begin(), order.end());
    }
    constexpr std::uint64_t saturated = std::uint64_t{1} << 43;
    const auto weight = [](std::uint64_t frequency, std::uint64_t odds) {
        return frequency * odds >> 20;
    };
    std::vector<std::uint64_t> frequency(count);
    for (const auto block : order) {
        std::uint64_t incoming = block == entry ? certain : 0;
        for (const auto predecessor : function.blocks[block].predecessors) {
            if (predecessor.value < count &&
                !back_edge(predecessor.value, block)) {
                incoming = std::min(
                    incoming + weight(frequency[predecessor.value],
                                      edge_probability(predecessor.value,
                                                       block)),
                    saturated);
            }
        }
        frequency[block] = std::min(
            header_loop[block] ? incoming * (certain / leave) : incoming,
            saturated);
    }

    // Bottom-up chain formation (Pettis and Hansen): visit fallthrough
    // candidates by decreasing weight and join a source that ends its chain
    // to a target that starts another. Among edges of equal weight, one that
    // would otherwise need an unconditional jump comes first, and back edges
    // come last so a loop's chain starts at its header. A late conditional
    // back edge comes after every forward edge: as a fallthrough it would
    // put its header in the middle of the loop's chain, so the loop's other
    // blocks would jump back to its top instead of the latch branching once.
    struct Edge {
        std::uint32_t source{};
        std::uint32_t target{};
        std::uint64_t weight{};
        bool conditional{};
        bool back{};
        std::size_t order{};
    };
    std::vector<Edge> edges;
    for (std::uint32_t source = 0; source < count; ++source) {
        if (!placeable[source] || !falls_through[source]) continue;
        for (std::size_t index = 0; index < successors[source].size();
             ++index) {
            const auto target = successors[source][index];
            if (target == source || target == entry || !placeable[target]) {
                continue;
            }
            edges.push_back(
                {source, target,
                 weight(frequency[source], probability[source][index]),
                 successors[source].size() > 1, back_edge(source, target),
                 position[source] * 4U + index});
        }
    }
    std::stable_sort(edges.begin(), edges.end(),
                     [&](const Edge& left, const Edge& right) {
                         const bool left_late = late_conditional_back_edges &&
                             left.back && left.conditional;
                         const bool right_late = late_conditional_back_edges &&
                             right.back && right.conditional;
                         if (left_late != right_late) return !left_late;
                         if (left.weight != right.weight) {
                             return left.weight > right.weight;
                         }
                         if (left.conditional != right.conditional) {
                             return !left.conditional;
                         }
                         if (left.back != right.back) return !left.back;
                         return left.order < right.order;
                     });
    std::vector<std::uint32_t> chain_of(count);
    std::vector<std::vector<std::uint32_t>> chains(count);
    for (std::uint32_t block = 0; block < count; ++block) {
        chain_of[block] = block;
        if (placeable[block]) chains[block] = {block};
    }
    // A block with a source-visible label keeps its original neighbors: it
    // falls through only to the block after it and is reached only from the
    // block before it.
    const auto original_neighbors = [&](const Edge& edge) {
        return position[edge.source] + 1 == position[edge.target];
    };
    const auto join_chains = [&] {
        for (const auto& edge : edges) {
            const auto first = chain_of[edge.source];
            const auto second = chain_of[edge.target];
            if (first == second || chains[first].back() != edge.source ||
                chains[second].front() != edge.target ||
                ((addressable(edge.source) || addressable(edge.target)) &&
                 !original_neighbors(edge))) {
                continue;
            }
            for (const auto block : chains[second]) chain_of[block] = first;
            chains[first].insert(chains[first].end(), chains[second].begin(),
                                 chains[second].end());
            chains[second].clear();
        }
    };
    join_chains();

    // A loop whose header tests and exits, and whose blocks form one run of
    // a chain with the header falling through to the loop, rotates that run
    // so the header comes last when the run's last block reaches its first
    // by a jump: by its only edge, or by the less likely one while its
    // conditional branch keeps the other. That edge becomes a fallthrough,
    // the header's branch to the loop forms the backedge, and the header can
    // fall through to its exit.
    std::vector<std::size_t> nest(loops.size());
    for (std::size_t index = 0; index < nest.size(); ++index) nest[index] = index;
    std::stable_sort(nest.begin(), nest.end(),
                     [&](std::size_t left, std::size_t right) {
                         return loops[left].size < loops[right].size;
                     });
    for (const auto index : nest) {
        const auto& loop = loops[index];
        const auto header = loop.header;
        const auto& exits = successors[header];
        if (!placeable[header] || exits.size() != 2 ||
            loop.members[exits[0]] == loop.members[exits[1]]) {
            continue;
        }
        const auto inside = loop.members[exits[0]] ? exits[0] : exits[1];
        auto& chain = chains[chain_of[header]];
        const auto at = static_cast<std::size_t>(
            std::find(chain.begin(), chain.end(), header) - chain.begin());
        auto first = at;
        while (first > 0 && loop.members[chain[first - 1]]) --first;
        auto last = at;
        while (last + 1 < chain.size() && loop.members[chain[last + 1]]) ++last;
        const auto begin = chain.begin() + static_cast<std::ptrdiff_t>(first);
        const auto end = chain.begin() + static_cast<std::ptrdiff_t>(last + 1);
        const auto& wrap = successors[chain[last]];
        if (at == last || chain[at + 1] != inside ||
            std::find(wrap.begin(), wrap.end(), chain[first]) == wrap.end() ||
            (wrap.size() > 1 &&
             edge_probability(chain[last], chain[first]) >= certain / 2) ||
            std::find(begin, end, entry) != end ||
            std::any_of(begin, end, addressable)) {
            continue;
        }
        std::rotate(begin, chain.begin() + static_cast<std::ptrdiff_t>(at + 1),
                    end);
    }
    // A rotated test can now fall through to its exit.
    join_chains();

    // The entry chain first, then the others by their earliest original
    // block, with chains that only lead to traps last.
    std::vector<std::uint32_t> placed;
    for (std::uint32_t chain = 0; chain < count; ++chain) {
        if (!chains[chain].empty()) placed.push_back(chain);
    }
    const auto earliest = [&](std::uint32_t chain) {
        std::size_t result = count;
        for (const auto block : chains[chain]) {
            result = std::min(result, position[block]);
        }
        return result;
    };
    const auto trapping = [&](std::uint32_t chain) {
        return std::all_of(chains[chain].begin(), chains[chain].end(),
                           [&](std::uint32_t block) {
                               return frequency[block] < cold;
                           });
    };
    std::stable_sort(placed.begin(), placed.end(),
                     [&](std::uint32_t left, std::uint32_t right) {
                         const bool left_entry = left == chain_of[entry];
                         const bool right_entry = right == chain_of[entry];
                         if (left_entry != right_entry) return left_entry;
                         const bool left_cold = trapping(left);
                         const bool right_cold = trapping(right);
                         if (left_cold != right_cold) return right_cold;
                         return earliest(left) < earliest(right);
                     });
    std::vector<machine::BlockId> layout;
    layout.reserve(function.layout.size());
    for (const auto chain : placed) {
        for (const auto block : chains[chain]) layout.push_back({block});
    }
    if (layout.size() != function.layout.size() || layout == function.layout) {
        return false;
    }
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
