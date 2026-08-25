// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_transform.hpp"

#include <iostream>
#include <string_view>

namespace {

using namespace cross;

bool expect(bool condition, std::string_view message) {
    if (condition)
        return true;
    std::cerr << "machine transform test: " << message << '\n';
    return false;
}

machine::Register reg(std::uint32_t id) {
    return machine::Register::virtual_register({id}, machine::i64);
}

machine::Function function_with_registers(std::size_t count) {
    machine::Function function;
    function.entry = {0};
    function.virtual_registers.assign(count, machine::i64);
    function.virtual_register_classes.assign(
        count, machine::VirtualRegisterClass::Integer);
    machine::Block entry;
    entry.id = {0};
    entry.reachable = true;
    function.blocks.push_back(std::move(entry));
    function.layout.push_back({0});
    return function;
}

machine::Instruction target(std::uint32_t opcode,
                            std::initializer_list<machine::Register> defs,
                            std::initializer_list<machine::Register> uses) {
    machine::Instruction instruction;
    instruction.kind = machine::InstructionKind::Target;
    instruction.opcode = opcode;
    instruction.defs = defs;
    instruction.uses = uses;
    for (const auto use : uses) {
        instruction.operands.push_back(machine::RegisterOperand{use});
    }
    return instruction;
}

bool copy_propagation_test() {
    auto function = function_with_registers(4);
    auto source = target(10, {reg(0)}, {});
    source.has_side_effects = true;
    auto copy = target(1, {reg(1)}, {reg(0)});
    auto consumer = target(11, {reg(2)}, {reg(1)});
    consumer.live_across_call = {reg(1)};
    auto incompatible = target(1, {reg(3)}, {reg(0)});
    function.virtual_register_classes[3] =
        machine::VirtualRegisterClass::Floating;
    function.blocks[0].instructions = {source, copy, consumer, incompatible};

    const auto is_copy = [](const machine::Instruction& instruction) {
        return instruction.opcode == machine::TargetOpcodeId{1};
    };
    bool ok = true;
    ok &= expect(native::propagate_virtual_register_copies(function, is_copy),
                 "compatible copy should be propagated");
    const auto& instructions = function.blocks[0].instructions;
    ok &= expect(instructions.size() == 3,
                 "only the compatible copy should be removed");
    ok &= expect(
        instructions[1].uses == std::vector{reg(0)} &&
            instructions[1].live_across_call == std::vector{reg(0)} &&
            std::get<machine::RegisterOperand>(instructions[1].operands.front())
                    .value == reg(0),
        "all use-like copy references should be rewritten");
    ok &= expect(instructions.back().defs.front() == reg(3),
                 "cross-register-class copy must remain explicit");
    return ok;
}

bool expression_elimination_test() {
    auto function = function_with_registers(5);
    auto source = target(10, {reg(0)}, {});
    source.has_side_effects = true;
    auto first = target(2, {reg(1)}, {reg(0)});
    first.operands.push_back(
        machine::ImmediateOperand{7, 0, machine::i64, false});
    auto duplicate = target(2, {reg(2)}, {reg(0)});
    duplicate.operands.push_back(
        machine::ImmediateOperand{7, 0, machine::i64, false});
    auto distinct = target(2, {reg(3)}, {reg(0)});
    distinct.operands.push_back(
        machine::ImmediateOperand{8, 0, machine::i64, false});
    auto consumer = target(11, {reg(4)}, {reg(2), reg(3)});
    consumer.has_side_effects = true;
    function.blocks[0].instructions = {source, first, duplicate, distinct,
                                       consumer};

    const auto eligible = [](const machine::Instruction& instruction) {
        return instruction.opcode == machine::TargetOpcodeId{2};
    };
    bool ok = true;
    ok &= expect(native::eliminate_redundant_expressions(function, eligible),
                 "duplicate selected expression should be eliminated");
    const auto& instructions = function.blocks[0].instructions;
    ok &= expect(instructions.size() == 4,
                 "one exact expression duplicate should be removed");
    ok &= expect(instructions.back().uses == std::vector{reg(1), reg(3)},
                 "expression consumer should use the available definition");
    return ok;
}

bool load_elimination_test() {
    auto function = function_with_registers(4);
    const machine::StackSlotOperand address{{0}, 0, machine::i64};
    auto first = target(3, {reg(0)}, {});
    first.operands.push_back(address);
    first.may_load = true;
    auto duplicate = target(3, {reg(1)}, {});
    duplicate.operands.push_back(address);
    duplicate.may_load = true;
    auto store = target(4, {}, {});
    store.operands.push_back(address);
    store.may_store = true;
    auto after_store = target(3, {reg(2)}, {});
    after_store.operands.push_back(address);
    after_store.may_load = true;
    auto consumer = target(11, {reg(3)}, {reg(1), reg(2)});
    consumer.has_side_effects = true;
    function.blocks[0].instructions = {first, duplicate, store, after_store,
                                       consumer};

    const auto eligible = [](const machine::Instruction& instruction) {
        return instruction.opcode == machine::TargetOpcodeId{3};
    };
    bool ok = true;
    ok &= expect(native::eliminate_redundant_loads(function, eligible),
                 "repeated load should be eliminated before a store");
    const auto& instructions = function.blocks[0].instructions;
    ok &= expect(instructions.size() == 4,
                 "store must retain the following load");
    ok &= expect(instructions.back().uses == std::vector{reg(0), reg(2)},
                 "load consumer should use the first available value");
    return ok;
}

bool dead_definition_test() {
    auto function = function_with_registers(4);
    auto root = target(10, {reg(0)}, {});
    root.has_side_effects = true;
    auto first = target(5, {reg(1)}, {reg(0)});
    auto second = target(5, {reg(2)}, {reg(1)});
    auto implicit_storage = target(5, {reg(3)}, {});
    function.virtual_register_classes[3] =
        machine::VirtualRegisterClass::Memory;
    function.blocks[0].instructions = {root, first, second, implicit_storage};

    const auto observable = [&](const machine::Register& definition) {
        return definition.kind == machine::RegisterKind::Virtual &&
               function.virtual_register_classes[definition.id] ==
                   machine::VirtualRegisterClass::Memory;
    };
    bool ok = true;
    ok &= expect(native::eliminate_dead_definitions(function, observable),
                 "dead definition chain should be removed to a fixed point");
    const auto& instructions = function.blocks[0].instructions;
    ok &= expect(instructions.size() == 2 &&
                     instructions.front().defs.front() == reg(0) &&
                     instructions.back().defs.front() == reg(3),
                 "effect root and implicit target storage must remain");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= copy_propagation_test();
    ok &= expression_elimination_test();
    ok &= load_elimination_test();
    ok &= dead_definition_test();
    return ok ? 0 : 1;
}
