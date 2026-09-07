// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/machine_ir.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace cross::machine {
namespace {

void append_register(std::vector<Register>& registers, Register value) {
    if (std::find(registers.begin(), registers.end(), value) == registers.end()) {
        registers.push_back(value);
    }
}

bool physical(Register value) {
    return value.kind == RegisterKind::Physical && value.mode.valid();
}

bool may_return(const Function& function) {
    std::unordered_map<std::uint32_t, const Block*> blocks;
    for (const auto& block : function.blocks) blocks.emplace(block.id.value, &block);
    std::unordered_set<std::uint32_t> visited;
    std::vector<BlockId> pending{function.entry};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!visited.insert(id.value).second) continue;
        const auto found = blocks.find(id.value);
        if (found == blocks.end() || found->second->instructions.empty()) return true;
        const auto& block = *found->second;
        switch (block.instructions.back().kind) {
        case InstructionKind::Return:
        case InstructionKind::IndirectBranch:
        case InstructionKind::Target:
        case InstructionKind::Copy:
        case InstructionKind::Call:
            return true;
        case InstructionKind::Branch:
        case InstructionKind::ConditionalBranch:
            pending.insert(pending.end(), block.successors.begin(), block.successors.end());
            break;
        case InstructionKind::Unreachable:
        case InstructionKind::Trap:
            break;
        }
    }
    return false;
}

struct SavedRegister {
    Register reg;
    std::int64_t address{};
    std::optional<StackSlotId> slot;
};

} // namespace

Instruction frame_instruction(TargetOpcodeId opcode, FrameEffect effect,
                              SourceLocation location) {
    Instruction result;
    result.opcode = opcode;
    result.location = location;
    result.has_side_effects = true;
    append_register(result.uses, effect.base);
    switch (effect.operation) {
    case FrameOperation::AdjustStack:
        append_register(result.defs, effect.base);
        break;
    case FrameOperation::CopyBase:
        if (effect.destination) append_register(result.defs, *effect.destination);
        break;
    case FrameOperation::Save:
    case FrameOperation::Restore:
        result.may_store = effect.operation == FrameOperation::Save;
        result.may_load = effect.operation == FrameOperation::Restore;
        for (const auto& transfer : effect.transfers) {
            append_register(result.may_load ? result.defs : result.uses, transfer.reg);
        }
        if (effect.stack_delta != 0) append_register(result.defs, effect.base);
        break;
    }
    result.frame_effect = std::move(effect);
    return result;
}

bool verify_frame_program(const Function& function, Diagnostics& diagnostics) {
    bool ok = true;
    const auto fail = [&](std::string_view message) {
        diagnostics.error(function.location, message);
        ok = false;
    };
    if (function.frame.elide_incoming_saves && may_return(function)) {
        fail("incoming frame saves cannot be elided with a reachable return or unknown exit");
    }
    if (!function.frame.program) return ok;
    const auto& program = *function.frame.program;
    if (!function.frame.finalized || !physical(program.stack_pointer) ||
        !std::has_single_bit(program.entry_alignment) ||
        !std::has_single_bit(program.body_alignment) ||
        program.entry_stack_residue >= program.entry_alignment ||
        program.body_alignment > program.entry_alignment) {
        fail("fixed machine frame program has an invalid stack or alignment contract");
        return false;
    }
    std::unordered_map<std::uint32_t, std::int64_t> bases;
    bases.emplace(program.stack_pointer.id, 0);
    std::unordered_map<std::uint32_t, SavedRegister> saved;
    std::unordered_set<std::uint32_t> overwritten_bases;
    std::unordered_set<std::uint32_t> restored;
    const auto stack_offset = [&]() { return bases.at(program.stack_pointer.id); };
    const auto adjust = [&](Register base, std::int32_t delta) {
        if (base != program.stack_pointer) {
            fail("frame writeback must update the declared stack pointer");
            return;
        }
        const auto next = stack_offset() + delta;
        if (next > 0 || next < -static_cast<std::int64_t>(program.stack_size)) {
            fail("machine frame stack adjustment escapes its fixed allocation");
        }
        for (const auto& [id, save] : saved) {
            if (!restored.contains(id) && save.address < next) {
                fail("machine frame releases an incoming register save before restoring it");
            }
        }
        bases[base.id] = next;
    };
    const auto execute = [&](const std::vector<Instruction>& instructions, bool entry) {
        for (const auto& instruction : instructions) {
            if (instruction.kind != InstructionKind::Target || instruction.opcode.empty() ||
                !instruction.frame_effect || !instruction.operands.empty()) {
                fail("machine frame program requires selected typed frame instructions");
                continue;
            }
            const auto& effect = *instruction.frame_effect;
            const auto expected = frame_instruction(instruction.opcode, effect, instruction.location);
            if (instruction.defs != expected.defs || instruction.uses != expected.uses ||
                instruction.may_load != expected.may_load ||
                instruction.may_store != expected.may_store || !instruction.has_side_effects) {
                fail("machine frame instruction register or memory effects disagree with its operation");
            }
            if (!physical(effect.base) || effect.base.mode != program.stack_pointer.mode ||
                !std::all_of(instruction.clobbers.begin(), instruction.clobbers.end(), physical)) {
                fail("machine frame instruction requires physical registers in valid modes");
                continue;
            }
            if (effect.operation == FrameOperation::AdjustStack) {
                if (effect.destination || !effect.transfers.empty() || effect.stack_delta == 0 ||
                    effect.update != FrameUpdate::None) {
                    fail("malformed machine frame stack adjustment");
                }
                adjust(effect.base, effect.stack_delta);
                continue;
            }
            if (effect.operation == FrameOperation::CopyBase) {
                if (!effect.destination || !physical(*effect.destination) ||
                    effect.destination->mode != effect.base.mode || effect.stack_delta != 0 ||
                    effect.update != FrameUpdate::None || !effect.transfers.empty()) {
                    fail("malformed machine frame base copy");
                    continue;
                }
                const auto found = bases.find(effect.base.id);
                if (found == bases.end()) {
                    fail("machine frame base copy reads an unknown stack origin");
                    continue;
                }
                if (*effect.destination == program.stack_pointer) {
                    const auto delta = found->second - stack_offset();
                    if (delta < INT32_MIN || delta > INT32_MAX) {
                        fail("machine frame base copy has an unrepresentable stack adjustment");
                    } else {
                        adjust(*effect.destination, static_cast<std::int32_t>(delta));
                    }
                } else {
                    if (entry && !function.frame.elide_incoming_saves &&
                        !saved.contains(effect.destination->id)) {
                        fail("machine frame overwrites an incoming base before saving it");
                    }
                    bases[effect.destination->id] = found->second;
                    overwritten_bases.insert(effect.destination->id);
                }
                continue;
            }
            const bool load = effect.operation == FrameOperation::Restore;
            if (effect.destination || effect.transfers.empty() ||
                ((effect.stack_delta == 0) != (effect.update == FrameUpdate::None)) ||
                (entry && load) || (!entry && !load)) {
                fail("malformed machine frame register transfer");
                continue;
            }
            if (effect.update == FrameUpdate::BeforeMemory) adjust(effect.base, effect.stack_delta);
            const auto base = bases.find(effect.base.id);
            if (base == bases.end()) {
                fail("machine frame transfer uses an unknown stack base");
                continue;
            }
            const auto origin = base->second;
            std::unordered_set<std::uint32_t> group;
            for (const auto& transfer : effect.transfers) {
                if (!physical(transfer.reg) || transfer.reg.mode.bits % 8 != 0 ||
                    transfer.reg.id == program.stack_pointer.id ||
                    !group.insert(transfer.reg.id).second ||
                    (effect.stack_delta != 0 && transfer.reg.id == effect.base.id)) {
                    fail("machine frame transfer has an invalid or overlapping register");
                    continue;
                }
                const auto bytes = static_cast<std::uint32_t>(transfer.reg.mode.bits / 8U);
                const auto address = origin + transfer.offset;
                if (address < stack_offset() || address + bytes > 0) {
                    fail("machine frame transfer lies outside allocated stack storage");
                }
                if (!transfer.slot) {
                    fail("machine frame transfer requires a typed stack slot");
                } else {
                    if (transfer.slot->value >= function.stack_slots.size()) {
                        fail("machine frame transfer references an unknown stack slot");
                    } else {
                        const auto& slot = function.stack_slots[transfer.slot->value];
                        if (slot.elided || slot.size != bytes ||
                            slot.saved_register != transfer.reg || !slot.frame_offset ||
                            *slot.frame_offset - static_cast<std::int64_t>(program.stack_size) !=
                                address) {
                            fail("machine frame transfer size disagrees with its stack slot");
                        }
                    }
                }
                if (!load) {
                    if (overwritten_bases.contains(transfer.reg.id)) {
                        fail("machine frame saves a base after overwriting its incoming value");
                    }
                    for (const auto& [id, other] : saved) {
                        (void)id;
                        if (address < other.address + other.reg.mode.bits / 8 &&
                            other.address < address + bytes) {
                            fail("machine frame incoming register saves overlap");
                        }
                    }
                    for (const auto& slot : function.stack_slots) {
                        if (slot.elided || !slot.frame_offset || slot.hard_register ||
                            slot.kind == StackSlotKind::CalleeSave ||
                            slot.kind == StackSlotKind::IncomingArgument) continue;
                        const auto begin = *slot.frame_offset -
                            static_cast<std::int64_t>(program.stack_size);
                        if (address < begin + slot.size && begin < address + bytes) {
                            fail("machine frame incoming save overlaps live frame storage");
                        }
                    }
                    if (!saved.emplace(transfer.reg.id,
                                       SavedRegister{transfer.reg, address, transfer.slot}).second) {
                        fail("machine frame saves an incoming register more than once");
                    }
                } else {
                    const auto found = saved.find(transfer.reg.id);
                    if (found == saved.end() || found->second.reg != transfer.reg ||
                        found->second.address != address || found->second.slot != transfer.slot ||
                        !restored.insert(transfer.reg.id).second) {
                        fail("machine frame restore does not match its incoming register save");
                    }
                    bases.erase(transfer.reg.id);
                    overwritten_bases.erase(transfer.reg.id);
                }
            }
            if (effect.update == FrameUpdate::AfterMemory) adjust(effect.base, effect.stack_delta);
        }
    };
    execute(program.prologue, true);
    if (stack_offset() != -static_cast<std::int64_t>(program.stack_size) ||
        (static_cast<std::int64_t>(program.entry_stack_residue) + stack_offset()) %
                program.body_alignment != 0) {
        fail("machine frame prologue does not establish its declared body stack state");
    }
    for (const auto reg : function.callee_saved_registers) {
        if (!saved.contains(reg.value)) {
            fail("machine frame program omits a required incoming register save");
        }
    }
    if (function.frame.elide_incoming_saves) {
        if (!saved.empty() || !program.epilogue.empty()) {
            fail("elided non-returning frame contains incoming saves or an epilogue");
        }
        return ok;
    }
    execute(program.epilogue, false);
    if (stack_offset() != 0 || restored.size() != saved.size() || !overwritten_bases.empty()) {
        fail("machine frame epilogue does not restore its incoming stack and register state");
    }
    return ok;
}

} // namespace cross::machine
