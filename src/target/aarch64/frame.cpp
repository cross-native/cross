// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/frame.hpp"

#include "target/aarch64/machine.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace cross::aarch64 {
namespace {

using machine::FrameEffect;
using machine::FrameOperation;
using machine::FrameUpdate;
using machine::Register;

constexpr std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

bool returns_twice_or_interrupt(const hir::Function& entity) {
    const auto special = [](const FunctionDecl* declaration) {
        return declaration &&
               (declaration->attribute("returns_twice") || declaration->attribute("interrupt"));
    };
    return special(entity.definition) ||
           std::any_of(entity.declarations.begin(), entity.declarations.end(), special);
}

// The bytes a frame-slot access of `opcode` transfers; zero for an address.
unsigned access_bytes(Opcode opcode) {
    switch (opcode) {
    case Opcode::LoadU8:
    case Opcode::LoadS8:
    case Opcode::Store8: return 1;
    case Opcode::LoadU16:
    case Opcode::LoadS16:
    case Opcode::Store16: return 2;
    case Opcode::Load32:
    case Opcode::LoadS32:
    case Opcode::Store32: return 4;
    case Opcode::Load64:
    case Opcode::Store64: return 8;
    default: return 0;
    }
}

class FrameLowering {
public:
    FrameLowering(SelectedFunction& selected, const hir::Function& entity, const AbiEntry& abi,
                  const CompilerOptions& options, Diagnostics& diagnostics)
        : selected_(selected), function_(selected.function), entity_(entity), abi_(abi),
          options_(options), diagnostics_(diagnostics) {}

    bool run() {
        const auto saves = saved_registers();
        const bool calls = makes_calls();
        const bool slots = std::any_of(function_.stack_slots.begin(), function_.stack_slots.end(),
                                       [](const machine::StackSlot& slot) {
                                           return !slot.elided &&
                                                  slot.kind != machine::StackSlotKind::IncomingArgument;
                                       });
        function_.frame.elide_incoming_saves =
            options_.elide_noreturn_saves && options_.unwind_model == UnwindModel::None &&
            !options_.unwind_tables && !options_.asynchronous_unwind_tables &&
            !returns_twice_or_interrupt(entity_) && !selected_.returns;
        const bool record = !function_.frame.elide_incoming_saves && (calls || slots || !saves.empty());
        const auto locals = place_slots();
        if (locals > 0xffffffU) {
            diagnostics_.error(function_.location,
                               "the AArch64 frame of this function exceeds 16 MiB");
            return false;
        }
        const auto save_area = record ? 16U + align_up(8U * static_cast<std::uint32_t>(
                                                             saves.size()), 16U)
                                      : 0U;
        const auto size = locals + save_area;
        function_.frame.local_size = locals;
        function_.frame.has_frame_pointer = record;
        machine::FrameProgram program;
        program.stack_pointer = gpr(stack_pointer, 64);
        program.stack_size = size;
        program.entry_alignment = 16;
        program.body_alignment = 16;
        const auto sp = program.stack_pointer;
        if (record) {
            const auto fp = gpr(frame_pointer, 64);
            const auto lr = gpr(link_register, 64);
            const auto fp_slot = save_slot(fp, locals);
            const auto lr_slot = save_slot(lr, locals + 8U);
            std::vector<std::pair<Register, machine::StackSlotId>> pieces;
            for (std::size_t index = 0; index < saves.size(); ++index) {
                const auto reg = gpr(saves[index], 64);
                pieces.emplace_back(
                    reg, save_slot(reg, locals + 16U + 8U * static_cast<std::uint32_t>(index)));
                function_.callee_saved_registers.push_back({saves[index]});
            }
            // stp x29, x30, [sp, #-S]!; mov x29, sp; the saves above them.
            program.prologue.push_back(transfer(
                FrameOperation::Save, -static_cast<std::int32_t>(save_area),
                FrameUpdate::BeforeMemory, {{fp, 0, fp_slot}, {lr, 8, lr_slot}}));
            program.prologue.push_back(copy(fp, sp));
            for (const auto& group : pairs(pieces)) {
                program.prologue.push_back(transfer(FrameOperation::Save, 0, FrameUpdate::None, group));
            }
            adjust(program.prologue, -static_cast<std::int64_t>(locals));
            adjust(program.epilogue, locals);
            for (const auto& group : pairs(pieces)) {
                program.epilogue.push_back(
                    transfer(FrameOperation::Restore, 0, FrameUpdate::None, group));
            }
            program.epilogue.push_back(transfer(
                FrameOperation::Restore, static_cast<std::int32_t>(save_area),
                FrameUpdate::AfterMemory, {{fp, 0, fp_slot}, {lr, 8, lr_slot}}));
        } else if (locals != 0) {
            adjust(program.prologue, -static_cast<std::int64_t>(locals));
            if (!function_.frame.elide_incoming_saves) adjust(program.epilogue, locals);
        }
        for (const auto& [slot, offset] : selected_.incoming_arguments) {
            function_.stack_slots[slot.value].frame_offset =
                static_cast<std::int32_t>(size + offset);
        }
        function_.frame.program = std::move(program);
        function_.frame.finalized = true;
        mark_far_accesses();
        return true;
    }

private:
    // Registers that the function's ABI preserves and its body writes.
    std::vector<std::uint32_t> saved_registers() const {
        const auto named = [](const std::vector<std::string>& names, std::uint32_t number) {
            return std::any_of(names.begin(), names.end(), [&](const std::string& name) {
                return physical_register(name) == number;
            });
        };
        std::set<std::uint32_t> written;
        const auto add = [&](const Register& value) {
            if (value.kind == machine::RegisterKind::Physical) {
                written.insert(value.id);
            } else if (value.id < function_.virtual_register_assignments.size() &&
                       function_.virtual_register_assignments[value.id]) {
                written.insert(function_.virtual_register_assignments[value.id]->value);
            }
        };
        for (const auto& block : function_.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& value : instruction.defs) add(value);
                for (const auto& value : instruction.clobbers) add(value);
            }
        }
        std::vector<std::uint32_t> result;
        for (const auto number : written) {
            if (number >= frame_pointer) continue;
            if (named(abi_.call_clobbers, number) || named(entity_.clobbers, number) ||
                std::ranges::find(selected_.result_registers, number) !=
                    selected_.result_registers.end()) {
                continue;
            }
            result.push_back(number);
        }
        return result;
    }

    bool makes_calls() const {
        return std::any_of(function_.blocks.begin(), function_.blocks.end(),
                           [](const machine::Block& block) {
                               return std::any_of(block.instructions.begin(),
                                                  block.instructions.end(),
                                                  [](const machine::Instruction& instruction) {
                                                      return instruction.kind ==
                                                             machine::InstructionKind::Call;
                                                  });
                           });
    }

    // Frame offsets from sp for the outgoing area, then the spill homes and
    // locals by size; returns the 16-byte aligned size.
    std::uint32_t place_slots() {
        std::uint32_t offset = 0;
        if (selected_.outgoing_arguments) {
            auto& slot = function_.stack_slots[selected_.outgoing_arguments->value];
            slot.frame_offset = 0;
            offset = align_up(slot.size, 16);
        }
        std::vector<machine::StackSlot*> placed;
        for (auto& slot : function_.stack_slots) {
            if (slot.elided || slot.kind == machine::StackSlotKind::IncomingArgument ||
                slot.kind == machine::StackSlotKind::OutgoingArgument ||
                slot.kind == machine::StackSlotKind::CalleeSave) {
                continue;
            }
            placed.push_back(&slot);
        }
        // Small slots first, so that scalars stay within reach of a load's
        // scaled offset; large ones are mostly reached through addresses.
        std::stable_sort(placed.begin(), placed.end(), [](const auto* left, const auto* right) {
            return left->size < right->size;
        });
        for (auto* slot : placed) {
            offset = align_up(offset, slot->alignment);
            slot->frame_offset = static_cast<std::int32_t>(offset);
            offset += slot->size;
        }
        return align_up(offset, 16);
    }

    machine::StackSlotId save_slot(const Register& reg, std::uint32_t offset) {
        machine::StackSlot slot;
        slot.id = {static_cast<std::uint32_t>(function_.stack_slots.size())};
        slot.kind = machine::StackSlotKind::CalleeSave;
        slot.size = 8;
        slot.alignment = 8;
        slot.frame_offset = static_cast<std::int32_t>(offset);
        slot.location = function_.location;
        slot.name = "$save." + register_name(reg.id, 64);
        slot.saved_register = reg;
        function_.stack_slots.push_back(std::move(slot));
        return function_.stack_slots.back().id;
    }

    // Save transfers in pairs of consecutive slots, offsets from the save
    // area's base.
    std::vector<std::vector<machine::FrameTransfer>> pairs(
        const std::vector<std::pair<Register, machine::StackSlotId>>& pieces) const {
        std::vector<std::vector<machine::FrameTransfer>> result;
        for (std::size_t index = 0; index < pieces.size(); index += 2) {
            std::vector<machine::FrameTransfer> group;
            for (std::size_t item = index; item < std::min(index + 2, pieces.size()); ++item) {
                group.push_back({pieces[item].first, static_cast<std::int32_t>(16U + 8U * item),
                                 pieces[item].second});
            }
            result.push_back(std::move(group));
        }
        return result;
    }

    machine::Instruction transfer(FrameOperation operation, std::int32_t delta,
                                  FrameUpdate update,
                                  std::vector<machine::FrameTransfer> transfers) const {
        FrameEffect effect;
        effect.operation = operation;
        effect.base = gpr(stack_pointer, 64);
        effect.stack_delta = delta;
        effect.update = update;
        effect.transfers = std::move(transfers);
        return machine::frame_instruction(
            machine_opcode(operation == FrameOperation::Save ? Opcode::FrameSave
                                                             : Opcode::FrameRestore),
            std::move(effect), function_.location);
    }

    machine::Instruction copy(const Register& destination, const Register& source) const {
        FrameEffect effect;
        effect.operation = FrameOperation::CopyBase;
        effect.base = source;
        effect.destination = destination;
        return machine::frame_instruction(machine_opcode(Opcode::FrameCopy), std::move(effect),
                                          function_.location);
    }

    // Moves sp by `delta`: an add or subtract of 12 bits, shifted by 12,
    // and one of the low 12 bits.
    void adjust(std::vector<machine::Instruction>& program, std::int64_t delta) const {
        const auto magnitude = static_cast<std::uint32_t>(delta < 0 ? -delta : delta);
        for (const auto part : {magnitude & 0xfff000U, magnitude & 0xfffU}) {
            if (part == 0) continue;
            FrameEffect effect;
            effect.operation = FrameOperation::AdjustStack;
            effect.base = gpr(stack_pointer, 64);
            effect.stack_delta = delta < 0 ? -static_cast<std::int32_t>(part)
                                           : static_cast<std::int32_t>(part);
            program.push_back(machine::frame_instruction(machine_opcode(Opcode::FrameAdjust),
                                                         std::move(effect), function_.location));
        }
    }

    // A frame-slot access whose final offset no immediate holds computes
    // its address in the frame scratch register, which it then clobbers.
    void mark_far_accesses() {
        const auto scratch = gpr(frame_scratch, 64);
        for (auto& block : function_.blocks) {
            for (auto& instruction : block.instructions) {
                const auto bytes = access_bytes(decode_opcode(instruction.opcode));
                if (instruction.kind != machine::InstructionKind::Target || bytes == 0) continue;
                for (const auto& operand : instruction.operands) {
                    const auto* stack = std::get_if<machine::StackSlotOperand>(&operand);
                    if (!stack) continue;
                    const auto offset = static_cast<std::int64_t>(
                                            *function_.stack_slots[stack->slot.value].frame_offset) +
                                        stack->offset;
                    const bool near = (offset >= -256 && offset <= 255) ||
                                      (offset >= 0 && offset % bytes == 0 &&
                                       offset / bytes <= 4095);
                    if (!near && std::find(instruction.clobbers.begin(), instruction.clobbers.end(),
                                           scratch) == instruction.clobbers.end()) {
                        instruction.clobbers.push_back(scratch);
                    }
                }
            }
        }
    }

    SelectedFunction& selected_;
    machine::Function& function_;
    const hir::Function& entity_;
    const AbiEntry& abi_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
};

} // namespace

bool finalize_frame(SelectedFunction& selected, const hir::Function& entity, const AbiEntry& abi,
                    const CompilerOptions& options, Diagnostics& diagnostics) {
    return FrameLowering(selected, entity, abi, options, diagnostics).run();
}

} // namespace cross::aarch64
