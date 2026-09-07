// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/machine_ir.hpp"

#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace {
using namespace cross::machine;

Function paired_frame() {
    Function function;
    function.source = {0};
    function.symbol = "paired_frame";
    function.abi = {0};
    function.entry = {0};
    function.layout = {{0}};
    function.frame.finalized = true;
    function.frame.stack_alignment = 16;
    function.frame.local_size = 16;
    Block block;
    block.id = {0};
    block.reachable = true;
    Instruction result;
    result.kind = InstructionKind::Return;
    block.instructions.push_back(result);
    function.blocks.push_back(block);
    FrameProgram program;
    program.stack_pointer = Register::physical_register({29}, i64);
    program.stack_size = 16;
    program.entry_alignment = 16;
    program.body_alignment = 16;
    FrameEffect save;
    save.operation = FrameOperation::Save;
    save.base = program.stack_pointer;
    save.stack_delta = -16;
    save.update = FrameUpdate::BeforeMemory;
    for (unsigned index = 0; index < 2; ++index) {
        StackSlot slot;
        slot.id = {index};
        slot.kind = StackSlotKind::CalleeSave;
        slot.size = 8;
        slot.alignment = 8;
        slot.frame_offset = static_cast<std::int32_t>(index * 8U);
        slot.saved_register = Register::physical_register({19U + index}, i64);
        function.stack_slots.push_back(slot);
        function.callee_saved_registers.push_back({19U + index});
        save.transfers.push_back({*slot.saved_register, *slot.frame_offset, slot.id});
    }
    program.prologue.push_back(frame_instruction({1}, save, {}));
    save.operation = FrameOperation::Restore;
    save.stack_delta = 16;
    save.update = FrameUpdate::AfterMemory;
    program.epilogue.push_back(frame_instruction({2}, save, {}));
    function.frame.program = std::move(program);
    return function;
}

bool accepted(const Function& function) {
    std::ostringstream text;
    cross::Diagnostics diagnostics(text);
    if (verify(function, diagnostics)) return true;
    std::cerr << text.str();
    return false;
}

bool rejected(const Function& function, std::string_view expected) {
    std::ostringstream text;
    cross::Diagnostics diagnostics(text);
    if (!verify(function, diagnostics) && text.str().find(expected) != std::string::npos) return true;
    std::cerr << "expected diagnostic: " << expected << '\n' << text.str();
    return false;
}

void rebuild(Instruction& instruction) {
    instruction = frame_instruction(instruction.opcode, *instruction.frame_effect, instruction.location);
}

} // namespace

int main() {
    bool ok = accepted(paired_frame());
    {
        auto function = paired_frame();
        function.frame.elide_incoming_saves = true;
        ok = rejected(function, "reachable return") && ok;
    }
    {
        auto function = paired_frame();
        function.frame.program->prologue.front().defs.clear();
        ok = rejected(function, "effects disagree") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->transfers[1].offset = 0;
        rebuild(save);
        ok = rejected(function, "saves overlap") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->transfers[1].reg = save.frame_effect->transfers[0].reg;
        rebuild(save);
        ok = rejected(function, "overlapping register") && ok;
    }
    {
        auto function = paired_frame();
        auto& load = function.frame.program->epilogue.front();
        load.frame_effect->transfers[1].offset = 0;
        rebuild(load);
        ok = rejected(function, "does not match") && ok;
    }
    {
        auto function = paired_frame();
        auto& load = function.frame.program->epilogue.front();
        load.frame_effect->update = FrameUpdate::BeforeMemory;
        rebuild(load);
        ok = rejected(function, "before restoring") && ok;
    }
    {
        auto function = paired_frame();
        function.frame.program->epilogue.clear();
        ok = rejected(function, "does not restore") && ok;
    }
    {
        auto function = paired_frame();
        function.frame.program->stack_size = 8;
        ok = rejected(function, "escapes its fixed allocation") && ok;
    }
    {
        auto function = paired_frame();
        function.frame.program->entry_stack_residue = 8;
        ok = rejected(function, "body stack state") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->base.id = 28;
        rebuild(save);
        ok = rejected(function, "declared stack pointer") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->transfers[0].reg.kind = RegisterKind::Virtual;
        rebuild(save);
        ok = rejected(function, "invalid or overlapping register") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->transfers[0].slot = StackSlotId{9};
        rebuild(save);
        ok = rejected(function, "unknown stack slot") && ok;
    }
    {
        auto function = paired_frame();
        function.callee_saved_registers.push_back({21});
        ok = rejected(function, "omits a required incoming") && ok;
    }
    {
        auto function = paired_frame();
        auto& save = function.frame.program->prologue.front();
        save.frame_effect->transfers[0].slot.reset();
        rebuild(save);
        ok = rejected(function, "requires a typed stack slot") && ok;
    }
    {
        auto function = paired_frame();
        StackSlot local;
        local.id = {2};
        local.size = 4;
        local.alignment = 4;
        local.frame_offset = 4;
        function.stack_slots.push_back(local);
        ok = rejected(function, "overlaps live frame storage") && ok;
        function.stack_slots.pop_back();
        ok = accepted(function) && ok;
    }
    {
        auto function = paired_frame();
        FrameEffect copy;
        copy.operation = FrameOperation::CopyBase;
        copy.base = function.frame.program->stack_pointer;
        copy.destination = Register::physical_register({19}, i64);
        function.frame.program->prologue.insert(function.frame.program->prologue.begin(),
            frame_instruction({3}, copy, {}));
        ok = rejected(function, "base before saving") && ok;
        ok = rejected(function, "after overwriting") && ok;
    }
    {
        auto function = paired_frame();
        function.frame.elide_incoming_saves = true;
        function.callee_saved_registers.clear();
        function.stack_slots.clear();
        auto& block = function.blocks.front();
        block.instructions.front().kind = InstructionKind::Branch;
        block.instructions.front().operands = {BlockOperand{{0}}};
        block.predecessors = {{0}};
        block.successors = {{0}};
        auto& program = *function.frame.program;
        program.prologue.clear();
        program.epilogue.clear();
        FrameEffect adjust;
        adjust.operation = FrameOperation::AdjustStack;
        adjust.base = program.stack_pointer;
        adjust.stack_delta = -16;
        program.prologue.push_back(frame_instruction({4}, adjust, {}));
        ok = accepted(function) && ok;
        program.epilogue.push_back(frame_instruction({4}, adjust, {}));
        ok = rejected(function, "or an epilogue") && ok;
    }
    return ok ? 0 : 1;
}
