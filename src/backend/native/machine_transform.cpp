// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_transform.hpp"

#include <algorithm>
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

} // namespace cross::native
