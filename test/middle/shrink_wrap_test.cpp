// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/shrink_wrap.hpp"

#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace cross::machine;

constexpr TargetOpcodeId parameter_opcode{1};
constexpr TargetOpcodeId phi_opcode{2};
constexpr TargetOpcodeId add_opcode{3};

bool is_phi(const Instruction& instruction) {
    return instruction.opcode == phi_opcode;
}

bool is_parameter(const Instruction& instruction) {
    return instruction.opcode == parameter_opcode;
}

Register vreg(std::uint32_t id) {
    return Register::virtual_register({id}, i64);
}

// Builds a function whose blocks 0..n-1 have the given successors. A block
// without successors returns; the entry branches when it has two.
Function graph(std::initializer_list<std::vector<std::uint32_t>> edges) {
    Function function;
    function.source = {0};
    function.symbol = "shrink_wrap_test";
    function.abi = {0};
    function.entry = {0};
    std::uint32_t index = 0;
    for (const auto& successors : edges) {
        Block block;
        block.id = {index};
        block.reachable = true;
        for (const auto successor : successors) {
            block.successors.push_back({successor});
        }
        function.blocks.push_back(std::move(block));
        function.layout.push_back({index});
        ++index;
    }
    for (auto& block : function.blocks) {
        for (const auto successor : block.successors) {
            function.blocks[successor.value].predecessors.push_back(block.id);
        }
    }
    return function;
}

// Appends each block's terminator once its body is complete.
void terminate(Function& function) {
    for (auto& block : function.blocks) {
        Instruction terminator;
        if (block.successors.empty()) {
            terminator.kind = InstructionKind::Return;
        } else if (block.successors.size() == 1) {
            terminator.kind = InstructionKind::Branch;
            terminator.operands.push_back(BlockOperand{block.successors[0]});
        } else {
            terminator.kind = InstructionKind::ConditionalBranch;
            for (const auto successor : block.successors) {
                terminator.operands.push_back(BlockOperand{successor});
            }
        }
        block.instructions.push_back(std::move(terminator));
    }
}

void call(Function& function, std::uint32_t block,
          std::vector<Register> uses = {}) {
    Instruction instruction;
    instruction.kind = InstructionKind::Call;
    SymbolOperand callee;
    callee.name = "callee";
    instruction.operands.push_back(std::move(callee));
    instruction.uses = std::move(uses);
    function.blocks[block].instructions.push_back(std::move(instruction));
}

void use(Function& function, std::uint32_t block, Register value) {
    Instruction instruction;
    instruction.opcode = add_opcode;
    instruction.uses.push_back(value);
    function.blocks[block].instructions.push_back(std::move(instruction));
}

std::vector<bool> blocks(std::size_t count,
                         std::initializer_list<std::uint32_t> members) {
    std::vector<bool> result(count);
    for (const auto member : members) result[member] = true;
    return result;
}

bool expect(bool condition, std::string_view message) {
    if (condition) return true;
    std::cerr << "shrink-wrap test failed: " << message << '\n';
    return false;
}

// Whether verification rejects the function for its prologue placement.
bool rejected(const Function& function) {
    std::ostringstream text;
    cross::Diagnostics diagnostics(text);
    return !verify(function, diagnostics) &&
           text.str().find("shrink-wrapped prologue block") !=
               std::string::npos;
}

bool placed(const Function& function,
            std::initializer_list<std::uint32_t> needs,
            std::optional<std::uint32_t> expected) {
    const auto block =
        place_prologue(function, blocks(function.blocks.size(), needs));
    return expected ? block && block->value == *expected : !block;
}

} // namespace

int main() {
    bool ok = true;

    // 0 -> {1 return, 2 call -> return}
    {
        auto function = graph({{1, 2}, {}, {}});
        call(function, 2);
        terminate(function);
        ok = expect(placed(function, {2}, 2), "early exit was not wrapped") &&
             ok;
        ok = expect(frame_blocks(function, is_phi) == blocks(3, {2}),
                    "a call did not need the frame") && ok;
        ok = expect(placed(function, {0, 2}, std::nullopt),
                    "a frame needed at entry was wrapped") && ok;
        function.frame.prologue_block = BlockId{2};
        ok = expect(framed_blocks(function) ==
                        std::vector<bool>{false, false, true},
                    "framed blocks differ") &&
             ok;
        {
            std::ostringstream text;
            cross::Diagnostics diagnostics(text);
            ok = expect(verify(function, diagnostics),
                        "valid placement was rejected") &&
                 ok;
        }
        function.frame.prologue_block = BlockId{0};
        ok = expect(rejected(function), "entry placement was accepted") && ok;
    }

    // A join reachable without the prologue raises it to the entry:
    // 0 -> {1 call, 2}; 1 -> 2 return.
    {
        auto function = graph({{1, 2}, {2}, {}});
        call(function, 1);
        terminate(function);
        ok = expect(placed(function, {1}, std::nullopt),
                    "a block reaching a join was wrapped") && ok;
        ok = expect(!valid_prologue_block(function, {1}),
                    "a non-dominating region was valid") && ok;
    }

    // The prologue leaves a loop for its preheader:
    // 0 -> {1 return, 2}; 2 -> 3; 3 -> {4, 5 return}; 4 call -> 3.
    {
        auto function = graph({{1, 2}, {}, {3}, {4, 5}, {3}, {}});
        call(function, 4);
        terminate(function);
        ok = expect(placed(function, {4}, 2),
                    "loop placement was not hoisted") &&
             ok;
        ok = expect(!valid_prologue_block(function, {3}) &&
                        !valid_prologue_block(function, {4}),
                    "a block on a cycle was valid") && ok;
        function.frame.prologue_block = BlockId{4};
        ok = expect(rejected(function), "loop placement was accepted") && ok;
    }

    // Several returns run the epilogue: 0 -> {1 return, 2 call}; 2 -> {3, 4}.
    {
        auto function = graph({{1, 2}, {}, {3, 4}, {}, {}});
        call(function, 2);
        terminate(function);
        ok = expect(placed(function, {2, 4}, 2),
                    "multiple returns were not wrapped") &&
             ok;
        function.frame.prologue_block = BlockId{2};
        ok = expect(framed_blocks(function) ==
                        std::vector<bool>{false, false, true, true, true},
                    "returns after the prologue were not framed") && ok;
    }

    // Wrapping is pointless when every return follows the prologue.
    {
        auto function = graph({{1}, {}});
        call(function, 1);
        terminate(function);
        ok = expect(placed(function, {1}, std::nullopt),
                    "an unavoidable prologue was moved") && ok;
    }

    // A PHI copies at the end of its predecessors: 0 -> {1, 2} -> 3.
    {
        auto function = graph({{1, 2}, {3}, {3}, {}});
        function.virtual_registers = {i64, i64, i64};
        function.virtual_register_classes.assign(
            3, VirtualRegisterClass::Integer);
        function.virtual_register_assignments = {PhysicalRegisterId{1},
                                                 PhysicalRegisterId{2},
                                                 PhysicalRegisterId{9}};
        function.rematerialized_immediates.resize(3);
        function.callee_saved_registers = {PhysicalRegisterId{9}};
        Instruction phi;
        phi.opcode = phi_opcode;
        phi.operands = {BlockOperand{{1}}, RegisterOperand{vreg(0)},
                        BlockOperand{{2}}, RegisterOperand{vreg(1)}};
        phi.uses = {vreg(0), vreg(1)};
        phi.defs = {vreg(2)};
        function.blocks[3].instructions.push_back(std::move(phi));
        terminate(function);
        ok = expect(frame_blocks(function, is_phi) == blocks(4, {1, 2}),
                    "a preserved PHI result did not need its predecessors") &&
             ok;
    }

    // A parameter live across a call below one branch is split there:
    // 0 -> {1 return v0, 2 call; use v0}.
    {
        auto function = graph({{1, 2}, {}, {}});
        function.virtual_registers = {i64};
        function.virtual_register_classes = {VirtualRegisterClass::Integer};
        function.virtual_register_assignments.resize(1);
        function.rematerialized_immediates.resize(1);
        StackSlot home;
        home.id = {0};
        home.kind = StackSlotKind::Spill;
        home.size = 8;
        home.alignment = 8;
        home.spill_for = VirtualRegisterId{0};
        function.stack_slots.push_back(home);
        Instruction parameter;
        parameter.opcode = parameter_opcode;
        parameter.defs = {vreg(0)};
        function.blocks[0].instructions.push_back(std::move(parameter));
        use(function, 1, vreg(0));
        call(function, 2);
        use(function, 2, vreg(0));
        terminate(function);
        ok = expect(split_entry_parameters(function, is_parameter, is_phi),
                    "a call-crossing parameter was not split") && ok;
        const auto& top = function.blocks[2].instructions.front();
        ok = expect(top.kind == InstructionKind::Copy &&
                        top.defs == std::vector{vreg(1)} &&
                        top.uses == std::vector{vreg(0)},
                    "the split copy is not at the top of its successor") && ok;
        ok = expect(function.blocks[2].instructions[2].uses ==
                            std::vector{vreg(1)} &&
                        function.blocks[1].instructions[0].uses ==
                            std::vector{vreg(0)},
                    "uses were not renamed below the copy only") && ok;
        ok = expect(function.stack_slots.size() == 2 &&
                        function.stack_slots[1].spill_for &&
                        function.stack_slots[1].spill_for->value == 1,
                    "the copy has no spill home") && ok;
    }

    // A use in a join keeps the parameter whole:
    // 0 -> {1 call, 2}; 1 -> 2; 2 uses v0.
    {
        auto function = graph({{1, 2}, {2}, {}});
        function.virtual_registers = {i64};
        function.virtual_register_classes = {VirtualRegisterClass::Integer};
        function.virtual_register_assignments.resize(1);
        function.rematerialized_immediates.resize(1);
        Instruction parameter;
        parameter.opcode = parameter_opcode;
        parameter.defs = {vreg(0)};
        function.blocks[0].instructions.push_back(std::move(parameter));
        call(function, 1);
        use(function, 2, vreg(0));
        terminate(function);
        ok = expect(!split_entry_parameters(function, is_parameter, is_phi),
                    "a parameter used in a join was split") && ok;
    }
    return ok ? 0 : 1;
}
