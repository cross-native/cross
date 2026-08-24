// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/machine_ir.hpp"

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
    return ok ? 0 : 1;
}
