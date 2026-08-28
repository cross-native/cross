// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/machine_ir.hpp"

#include <algorithm>
#include <bit>
#include <string>
#include <string_view>
#include <unordered_set>

namespace cross::machine {
namespace {

bool fail(Diagnostics& diagnostics, SourceLocation location, std::string_view message) {
    diagnostics.error(location, message);
    return false;
}

bool contains(const std::vector<BlockId>& ids, BlockId wanted) {
    return std::any_of(ids.begin(), ids.end(), [wanted](BlockId id) { return id == wanted; });
}

bool is_power_of_two(std::uint32_t value) {
    return value != 0 && std::has_single_bit(value);
}

bool is_terminator(InstructionKind kind) {
    return kind == InstructionKind::Branch || kind == InstructionKind::ConditionalBranch ||
           kind == InstructionKind::IndirectBranch ||
           kind == InstructionKind::Return || kind == InstructionKind::Unreachable ||
           kind == InstructionKind::Trap;
}

bool block_operand(const Operand& operand, BlockId& result) {
    if (const auto* value = std::get_if<BlockOperand>(&operand)) {
        result = value->target;
        return true;
    }
    return false;
}

std::vector<BlockId> block_operands(const Instruction& instruction) {
    std::vector<BlockId> result;
    for (const Operand& operand : instruction.operands) {
        BlockId target;
        if (block_operand(operand, target)) {
            result.push_back(target);
        }
    }
    return result;
}

bool register_operand(const Operand& operand) {
    return std::holds_alternative<RegisterOperand>(operand);
}

bool symbol_operand(const Operand& operand) {
    return std::holds_alternative<SymbolOperand>(operand);
}

bool verify_register(const Register& reg, const Function& function,
                     std::unordered_set<std::uint32_t>& defined_virtuals,
                     bool defining, Diagnostics& diagnostics, SourceLocation location) {
    bool ok = true;
    if (!reg.mode.valid()) {
        ok = fail(diagnostics, location, "machine register has an invalid integer mode");
    }
    if (reg.kind == RegisterKind::Virtual) {
        if (reg.id >= function.virtual_registers.size()) {
            return fail(diagnostics, location, "machine instruction references an unknown virtual register");
        }
        if (function.virtual_registers[reg.id] != reg.mode) {
            ok = fail(diagnostics, location, "machine virtual register mode disagrees with its declaration");
        }
        if (defining && !defined_virtuals.insert(reg.id).second) {
            ok = fail(diagnostics, location, "machine virtual register has more than one definition");
        }
    }
    return ok;
}

bool verify_instruction(const Instruction& instruction, const Function& function,
                        const std::vector<BlockId>& block_ids,
                        std::unordered_set<std::uint32_t>& defined_virtuals,
                        Diagnostics& diagnostics) {
    bool ok = true;
    if (instruction.kind == InstructionKind::Target && instruction.opcode.empty()) {
        ok = fail(diagnostics, instruction.location, "target machine instruction has no opcode");
    }
    if (instruction.kind != InstructionKind::Target && !instruction.opcode.empty()) {
        ok = fail(diagnostics, instruction.location, "generic machine instruction must not carry a target opcode");
    }
    if (instruction.kind != InstructionKind::ConditionalBranch &&
        instruction.kind != InstructionKind::Target &&
        !instruction.condition_predicate.empty()) {
        ok = fail(diagnostics, instruction.location,
                  "machine condition predicate appears on an unconditional "
                  "generic instruction");
    }
    for (const Register& reg : instruction.defs) {
        ok = verify_register(reg, function, defined_virtuals, true, diagnostics,
                             instruction.location) && ok;
    }
    for (const Register& reg : instruction.uses) {
        ok = verify_register(reg, function, defined_virtuals, false, diagnostics,
                             instruction.location) && ok;
    }
    for (const Register& reg : instruction.clobbers) {
        ok = verify_register(reg, function, defined_virtuals, false, diagnostics,
                             instruction.location) && ok;
    }
    for (const Register& reg : instruction.live_across_call) {
        if (reg.kind != RegisterKind::Virtual) {
            ok = fail(diagnostics, instruction.location,
                      "machine call-live value must be a virtual register");
        }
        ok = verify_register(reg, function, defined_virtuals, false,
                             diagnostics, instruction.location) && ok;
    }
    for (const Operand& operand : instruction.operands) {
        if (const auto* immediate = std::get_if<ImmediateOperand>(&operand)) {
            if (!immediate->mode.valid() || immediate->mode.bits > 128) {
                ok = fail(diagnostics, instruction.location,
                          "machine immediate must have an integer mode from 1 through 128 bits");
            } else if (immediate->mode.bits <= 64 && immediate->high != 0) {
                ok = fail(diagnostics, instruction.location,
                          "machine immediate has high bits outside its mode");
            }
        } else if (const auto* symbol = std::get_if<SymbolOperand>(&operand)) {
            if (symbol->name.empty()) {
                ok = fail(diagnostics, instruction.location, "machine symbol operand has an empty name");
            }
        } else if (const auto* block = std::get_if<BlockOperand>(&operand)) {
            if (!contains(block_ids, block->target)) {
                ok = fail(diagnostics, instruction.location, "machine instruction targets an unknown block");
            }
        } else if (const auto* stack = std::get_if<StackSlotOperand>(&operand)) {
            if (stack->slot.value >= function.stack_slots.size() || !stack->mode.valid()) {
                ok = fail(diagnostics, instruction.location,
                          "machine stack operand references an unknown slot or invalid mode");
            }
        }
    }

    const auto expect_block_count = [&](std::size_t count, std::string_view message) {
        std::size_t actual = 0;
        for (const Operand& operand : instruction.operands) {
            actual += std::holds_alternative<BlockOperand>(operand);
        }
        if (actual != count) {
            ok = fail(diagnostics, instruction.location, message);
        }
    };
    switch (instruction.kind) {
    case InstructionKind::Copy:
        if (instruction.defs.size() != 1 || instruction.uses.size() != 1) {
            ok = fail(diagnostics, instruction.location,
                      "machine copy requires exactly one definition and one use");
        }
        break;
    case InstructionKind::Call:
        if (instruction.operands.empty() ||
            (!register_operand(instruction.operands.front()) && !symbol_operand(instruction.operands.front()))) {
            ok = fail(diagnostics, instruction.location,
                      "machine call requires a symbol or register callee as its first operand");
        }
        if (instruction.direct_callee &&
            !symbol_operand(instruction.operands.front())) {
            ok = fail(diagnostics, instruction.location,
                      "direct machine call identity requires a symbol callee");
        }
        if (instruction.call_argument_types.size() + 1 !=
            instruction.operands.size()) {
            ok = fail(diagnostics, instruction.location,
                      "machine call argument type metadata disagrees with operands");
        }
        break;
    case InstructionKind::Branch:
        if (!instruction.defs.empty()) {
            ok = fail(diagnostics, instruction.location, "machine branch cannot define a register");
        }
        expect_block_count(1, "machine branch requires exactly one block target");
        break;
    case InstructionKind::ConditionalBranch:
        if (!instruction.defs.empty()) {
            ok = fail(diagnostics, instruction.location,
                      "machine conditional branch cannot define a register");
        }
        expect_block_count(2, "machine conditional branch requires exactly two block targets");
        break;
    case InstructionKind::IndirectBranch:
        if (!instruction.defs.empty() || instruction.uses.size() != 1 ||
            instruction.operands.empty() ||
            !register_operand(instruction.operands.front())) {
            ok = fail(diagnostics, instruction.location,
                      "machine indirect branch requires one address register");
        }
        if (block_operands(instruction).empty()) {
            ok = fail(diagnostics, instruction.location,
                      "machine indirect branch requires possible block targets");
        }
        break;
    case InstructionKind::Return:
    case InstructionKind::Unreachable:
    case InstructionKind::Trap:
        if (!instruction.defs.empty()) {
            ok = fail(diagnostics, instruction.location, "machine terminator cannot define a register");
        }
        break;
    case InstructionKind::Target:
        break;
    }
    if (instruction.kind != InstructionKind::Call &&
        !instruction.live_across_call.empty()) {
        ok = fail(diagnostics, instruction.location,
                  "call-live values may appear only on a machine call");
    }
    if (instruction.kind != InstructionKind::Call &&
        !instruction.call_argument_types.empty()) {
        ok = fail(diagnostics, instruction.location,
                  "call argument types may appear only on a machine call");
    }
    if (!instruction.variadic_state.empty() &&
        instruction.kind != InstructionKind::Target) {
        ok = fail(diagnostics, instruction.location,
                  "variadic state metadata requires its target operation");
    }
    if (instruction.patch) {
        if (instruction.kind != InstructionKind::Target ||
            instruction.patch->field_bits == 0 ||
            instruction.patch->field_bits > 64) {
            ok = fail(diagnostics, instruction.location,
                      "machine patch metadata requires a target instruction "
                      "with a 1-through-64-bit field");
        }
    }
    return ok;
}

} // namespace

bool verify(const Function& function, Diagnostics& diagnostics) {
    bool ok = true;
    if (function.blocks.empty()) {
        return fail(diagnostics, function.location, "machine function has no blocks");
    }
    if (function.symbol.empty()) {
        ok = fail(diagnostics, function.location, "machine function has an empty linkage symbol");
    }
    if (!function.abi.valid()) {
        ok = fail(diagnostics, function.location,
                  "machine function has no resolved ABI identity");
    }

    std::vector<BlockId> block_ids;
    block_ids.reserve(function.blocks.size());
    std::unordered_set<std::uint32_t> unique_blocks;
    for (const Block& block : function.blocks) {
        block_ids.push_back(block.id);
        if (!unique_blocks.insert(block.id.value).second) {
            ok = fail(diagnostics, block.location, "machine function contains duplicate block IDs");
        }
    }
    if (!contains(block_ids, function.entry)) {
        ok = fail(diagnostics, function.location, "machine function entry block does not exist");
    }
    std::unordered_set<std::uint32_t> local_labels;
    for (const auto& label : function.labels) {
        if (!contains(block_ids, label.block) ||
            !local_labels.insert(label.label.value).second) {
            ok = fail(diagnostics, function.location,
                      "machine local-label map is invalid");
        }
    }
    if (function.layout.size() != function.blocks.size()) {
        ok = fail(diagnostics, function.location, "machine function layout must contain every block exactly once");
    }
    std::unordered_set<std::uint32_t> layout_blocks;
    for (BlockId id : function.layout) {
        if (!contains(block_ids, id) || !layout_blocks.insert(id.value).second) {
            ok = fail(diagnostics, function.location,
                      "machine function layout contains an unknown or duplicate block");
        }
    }
    for (IntegerMode mode : function.virtual_registers) {
        if (!mode.valid()) {
            ok = fail(diagnostics, function.location, "machine function declares a virtual register with invalid mode");
        }
    }
    if (function.virtual_register_classes.size() !=
        function.virtual_registers.size()) {
        ok = fail(diagnostics, function.location,
                  "machine register-class table must cover every virtual register");
    }
    if (function.virtual_register_assignments.size() !=
        function.virtual_registers.size()) {
        ok = fail(diagnostics, function.location,
                  "machine physical-assignment table must cover every "
                  "virtual register");
    }
    if (function.rematerialized_immediates.size() !=
        function.virtual_registers.size()) {
        ok = fail(diagnostics, function.location,
                  "machine rematerialization table must cover every virtual "
                  "register");
    } else {
        for (std::size_t index = 0;
             index < function.rematerialized_immediates.size(); ++index) {
            const auto& immediate =
                function.rematerialized_immediates[index];
            if (!immediate) continue;
            const bool zero_vector_recipe =
                index < function.virtual_register_classes.size() &&
                function.virtual_register_classes[index] ==
                    VirtualRegisterClass::Vector &&
                immediate->mode == function.virtual_registers[index] &&
                immediate->value == 0 && immediate->high == 0;
            const bool scalar_recipe =
                immediate->mode.valid() && immediate->mode.bits <= 128 &&
                immediate->mode == function.virtual_registers[index] &&
                (immediate->mode.bits > 64 || immediate->high == 0);
            if (!zero_vector_recipe && !scalar_recipe) {
                ok = fail(
                    diagnostics, function.location,
                    "machine rematerialized immediate disagrees with its "
                    "virtual register mode");
            }
        }
    }
    if (!is_power_of_two(function.frame.stack_alignment)) {
        ok = fail(diagnostics, function.location, "machine frame stack alignment must be a power of two");
    }
    if (!is_power_of_two(function.frame.outgoing_argument_alignment)) {
        ok = fail(diagnostics, function.location,
                  "machine outgoing-argument alignment must be a power of two");
    }
    std::vector<bool> referenced_virtuals(function.virtual_registers.size());
    const auto mark_virtual = [&](const Register& value) {
        if (value.kind == RegisterKind::Virtual &&
            value.id < referenced_virtuals.size()) {
            referenced_virtuals[value.id] = true;
        }
    };
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            for (const auto& definition : instruction.defs)
                mark_virtual(definition);
            for (const auto& use : instruction.uses) mark_virtual(use);
            for (const auto& live : instruction.live_across_call)
                mark_virtual(live);
            for (const auto& operand : instruction.operands) {
                if (const auto* reg =
                        std::get_if<RegisterOperand>(&operand)) {
                    mark_virtual(reg->value);
                }
            }
        }
    }
    std::unordered_set<std::uint32_t> spill_homes;
    for (std::size_t index = 0; index < function.stack_slots.size(); ++index) {
        const StackSlot& slot = function.stack_slots[index];
        if (slot.id.value != index || slot.size == 0 || !is_power_of_two(slot.alignment)) {
            ok = fail(diagnostics, slot.location,
                      "machine stack slot IDs must be dense with nonzero size and power-of-two alignment");
        }
        if (slot.spill_for &&
            (slot.kind != StackSlotKind::Spill ||
             slot.spill_for->value >= function.virtual_registers.size() ||
             !spill_homes.insert(slot.spill_for->value).second)) {
            ok = fail(diagnostics, slot.location,
                      "machine virtual-register spill home is invalid or duplicated");
        }
        if (slot.hard_register && !slot.hard_register->valid()) {
            ok = fail(diagnostics, slot.location,
                      "machine hard-register slot has an invalid target view");
        }
        const bool assigned_spill =
            slot.spill_for &&
            slot.spill_for->value <
                function.virtual_register_assignments.size() &&
            function.virtual_register_assignments[slot.spill_for->value]
                .has_value();
        const bool rematerialized_spill =
            slot.spill_for &&
            slot.spill_for->value <
                function.rematerialized_immediates.size() &&
            function.rematerialized_immediates[slot.spill_for->value]
                .has_value();
        const bool unused_spill =
            slot.spill_for &&
            slot.spill_for->value < referenced_virtuals.size() &&
            !referenced_virtuals[slot.spill_for->value];
        if (slot.elided &&
            ((!assigned_spill && !rematerialized_spill && !unused_spill) ||
             slot.frame_offset)) {
            ok = fail(diagnostics, slot.location,
                      "machine stack slot may be elided only for an assigned "
                      "or rematerialized virtual register, or one removed "
                      "from Machine IR");
        }
        if (function.frame.finalized && !slot.elided &&
            !slot.frame_offset.has_value()) {
            ok = fail(diagnostics, slot.location, "finalized machine frame has an unassigned stack slot");
        }
        if (function.frame.finalized && !slot.elided &&
            slot.frame_offset.has_value() &&
            *slot.frame_offset % static_cast<std::int32_t>(slot.alignment) != 0) {
            ok = fail(diagnostics, slot.location,
                      "finalized machine stack slot offset violates its alignment");
        }
    }

    std::unordered_set<std::uint32_t> defined_virtuals;
    for (const Block& block : function.blocks) {
        bool saw_terminator = false;
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const Instruction& instruction = block.instructions[index];
            if (saw_terminator) {
                ok = fail(diagnostics, instruction.location,
                          "machine instruction follows a block terminator");
            }
            ok = verify_instruction(instruction, function, block_ids, defined_virtuals,
                                    diagnostics) && ok;
            saw_terminator = is_terminator(instruction.kind);
        }
        if (!block.instructions.empty() && !saw_terminator && !block.successors.empty()) {
            ok = fail(diagnostics, block.location,
                      "machine block with successors must end in an explicit terminator");
        }
        for (BlockId successor : block.successors) {
            if (!contains(block_ids, successor)) {
                ok = fail(diagnostics, block.location, "machine block has an unknown successor");
            }
        }
        for (BlockId predecessor : block.predecessors) {
            if (!contains(block_ids, predecessor)) {
                ok = fail(diagnostics, block.location, "machine block has an unknown predecessor");
            }
        }
        if (saw_terminator) {
            const Instruction& terminator = block.instructions.back();
            if ((terminator.kind == InstructionKind::Return ||
                 terminator.kind == InstructionKind::Unreachable ||
                 terminator.kind == InstructionKind::Trap) &&
                !block.successors.empty()) {
                ok = fail(diagnostics, terminator.location,
                          "machine return, unreachable, or trap block cannot have successors");
            }
            if (terminator.kind == InstructionKind::Branch ||
                terminator.kind == InstructionKind::ConditionalBranch ||
                terminator.kind == InstructionKind::IndirectBranch) {
                const std::vector<BlockId> targets = block_operands(terminator);
                if (targets != block.successors) {
                    ok = fail(diagnostics, terminator.location,
                              "machine branch successors must match its target operands in order");
                }
            }
        }
    }

    for (const Block& block : function.blocks) {
        for (BlockId successor : block.successors) {
            const auto successor_it = std::find_if(function.blocks.begin(), function.blocks.end(),
                                                   [successor](const Block& candidate) {
                                                       return candidate.id == successor;
                                                   });
            if (successor_it != function.blocks.end() &&
                !contains(successor_it->predecessors, block.id)) {
                ok = fail(diagnostics, block.location,
                          "machine CFG successor is missing its reciprocal predecessor");
            }
        }
        for (BlockId predecessor : block.predecessors) {
            const auto predecessor_it = std::find_if(function.blocks.begin(), function.blocks.end(),
                                                     [predecessor](const Block& candidate) {
                                                         return candidate.id == predecessor;
                                                     });
            if (predecessor_it != function.blocks.end() &&
                !contains(predecessor_it->successors, block.id)) {
                ok = fail(diagnostics, block.location,
                          "machine CFG predecessor is missing its reciprocal successor");
            }
        }
    }
    return ok;
}

bool verify(const Module& module, Diagnostics& diagnostics) {
    bool ok = true;
    std::unordered_set<std::uint32_t> functions;
    for (const Function& function : module.functions) {
        if (!functions.insert(function.source.value).second) {
            ok = fail(diagnostics, function.location,
                      "machine module contains more than one body for a HIR function");
        }
        ok = verify(function, diagnostics) && ok;
    }
    return ok;
}

} // namespace cross::machine
