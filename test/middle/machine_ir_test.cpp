// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/machine_ir.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

cross::machine::Function valid_function() {
    using namespace cross;
    using namespace cross::machine;

    Function function;
    function.source = {0};
    function.symbol = "machine_test";
    function.abi = {0};
    function.entry = {0};
    function.layout = {{0}};

    Block entry;
    entry.id = {0};
    entry.label = "entry";
    entry.reachable = true;
    Instruction result;
    result.kind = InstructionKind::Return;
    entry.instructions.push_back(std::move(result));
    function.blocks.push_back(std::move(entry));
    return function;
}

bool rejected(cross::machine::Function function,
              std::string_view expected) {
    std::ostringstream text;
    cross::Diagnostics diagnostics(text);
    if (cross::machine::verify(function, diagnostics)) return false;
    return text.str().find(expected) != std::string::npos;
}

bool expect(bool condition, std::string_view message) {
    if (condition) return true;
    std::cerr << "Machine IR test failed: " << message << '\n';
    return false;
}

} // namespace

int main() {
    bool ok = true;
    {
        std::ostringstream text;
        cross::Diagnostics diagnostics(text);
        ok = expect(cross::machine::verify(valid_function(), diagnostics) &&
                        diagnostics.errors() == 0,
                    "minimal typed function was rejected") &&
             ok;
    }
    {
        auto function = valid_function();
        function.abi = {};
        ok = expect(rejected(std::move(function), "resolved ABI identity"),
                    "unresolved ABI identity was accepted") &&
             ok;
    }
    {
        auto function = valid_function();
        cross::machine::StackSlot slot;
        slot.id = {0};
        slot.size = 8;
        slot.alignment = 8;
        slot.name = "hard";
        slot.hard_register = cross::machine::TargetRegisterViewId{};
        function.stack_slots.push_back(std::move(slot));
        ok = expect(rejected(std::move(function), "invalid target view"),
                    "invalid hard-register view was accepted") &&
             ok;
    }
    {
        auto function = valid_function();
        cross::machine::Instruction target;
        target.kind = cross::machine::InstructionKind::Target;
        function.blocks.front().instructions.insert(
            function.blocks.front().instructions.begin(),
            std::move(target));
        ok = expect(rejected(std::move(function), "has no opcode"),
                    "empty target opcode was accepted") &&
             ok;
    }
    {
        auto function = valid_function();
        function.virtual_registers = {cross::machine::i64};
        function.virtual_register_assignments.resize(1);
        function.rematerialized_immediates.resize(1);
        function.virtual_register_classes = {
            cross::machine::VirtualRegisterClass::Integer};
        cross::machine::StackSlot slot;
        slot.id = {0};
        slot.kind = cross::machine::StackSlotKind::Spill;
        slot.size = 8;
        slot.alignment = 8;
        slot.spill_for = cross::machine::VirtualRegisterId{0};
        slot.elided = true;
        function.stack_slots.push_back(std::move(slot));
        std::ostringstream text;
        cross::Diagnostics diagnostics(text);
        ok = expect(cross::machine::verify(function, diagnostics),
                    "elided home for a removed virtual was rejected") &&
             ok;
    }
    {
        auto function = valid_function();
        function.virtual_registers = {cross::machine::i64};
        function.virtual_register_assignments.resize(1);
        function.rematerialized_immediates.resize(1);
        function.virtual_register_classes = {
            cross::machine::VirtualRegisterClass::Integer};
        cross::machine::StackSlot slot;
        slot.id = {0};
        slot.kind = cross::machine::StackSlotKind::Spill;
        slot.size = 8;
        slot.alignment = 8;
        slot.spill_for = cross::machine::VirtualRegisterId{0};
        slot.elided = true;
        function.stack_slots.push_back(std::move(slot));
        cross::machine::Instruction target;
        target.kind = cross::machine::InstructionKind::Target;
        target.opcode = cross::machine::TargetOpcodeId{1};
        target.defs.push_back(cross::machine::Register::virtual_register(
            {0}, cross::machine::i64));
        function.blocks.front().instructions.insert(
            function.blocks.front().instructions.begin(),
            std::move(target));
        ok = expect(rejected(std::move(function),
                             "may be elided only"),
                    "elided home for a referenced unassigned virtual was accepted") &&
             ok;
    }
    {
        auto function = valid_function();
        cross::machine::Instruction target;
        target.kind = cross::machine::InstructionKind::Target;
        target.opcode = cross::machine::TargetOpcodeId{1};
        target.defs.push_back(cross::machine::Register::physical_register(
            {2}, cross::machine::i32));
        target.input_projection = cross::machine::ValueProjection{0, 32};
        function.blocks.front().instructions.insert(
            function.blocks.front().instructions.begin(), target);
        std::ostringstream text;
        cross::Diagnostics diagnostics(text);
        ok = expect(cross::machine::verify(function, diagnostics),
                    "well-formed boundary projection was rejected") &&
             ok;

        target.input_projection = cross::machine::ValueProjection{0, 16};
        function.blocks.front().instructions.front() = std::move(target);
        ok = expect(rejected(std::move(function),
                             "input projection requires"),
                    "width-mismatched boundary projection was accepted") &&
             ok;
    }
    {
        auto function = valid_function();
        function.virtual_registers = {
            cross::machine::i64, cross::machine::i32};
        function.virtual_register_assignments.resize(2);
        function.rematerialized_immediates.resize(2);
        function.virtual_register_classes = {
            cross::machine::VirtualRegisterClass::Integer,
            cross::machine::VirtualRegisterClass::Integer};
        for (std::uint32_t id = 0; id < 2; ++id) {
            cross::machine::Instruction definition;
            definition.kind = cross::machine::InstructionKind::Target;
            definition.opcode = cross::machine::TargetOpcodeId{id + 1};
            definition.defs.push_back(
                cross::machine::Register::virtual_register(
                    {id}, function.virtual_registers[id]));
            function.blocks.front().instructions.insert(
                function.blocks.front().instructions.end() - 1,
                std::move(definition));
        }
        auto& result = function.blocks.front().instructions.back();
        result.uses = {
            cross::machine::Register::virtual_register(
                {0}, cross::machine::i64),
            cross::machine::Register::virtual_register(
                {1}, cross::machine::i32)};
        result.result_composition =
            cross::machine::ResultComposition::XorZeroExtend;
        std::ostringstream text;
        cross::Diagnostics diagnostics(text);
        ok = expect(cross::machine::verify(function, diagnostics),
                    "well-formed result composition was rejected") &&
             ok;

        result.result_extension = cross::machine::ExtensionKind::Zero;
        ok = expect(rejected(std::move(function),
                             "result composition requires"),
                    "composed and extended result was accepted") &&
             ok;
    }
    return ok ? 0 : 1;
}
