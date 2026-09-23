// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir.hpp"

#include <iostream>
#include <sstream>
#include <string_view>

namespace {

using namespace cross;

hir::Module hir_fixture() {
    hir::Module module;
    module.types.push_back({hir::Type::Kind::Builtin, BuiltinType::Void,
                            std::nullopt, false, false, {}, std::nullopt, 0,
                            false, false, std::nullopt});
    module.types.push_back({hir::Type::Kind::Builtin, BuiltinType::Bool,
                            std::nullopt, false, false, {}, std::nullopt, 0,
                            false, false, std::nullopt});
    module.types.push_back({hir::Type::Kind::Builtin, BuiltinType::I32,
                            std::nullopt, false, false, {}, std::nullopt, 0,
                            false, false, std::nullopt});

    hir::Function function;
    function.id = {0};
    function.result_type = {2};
    function.ownership = hir::BodyOwnership::ManagedMir;
    module.functions.push_back(std::move(function));
    hir::FunctionSignature callable;
    callable.result_type = {2};
    const auto signature = module.function_type(std::move(callable));
    (void)module.pointer_to(signature);
    return module;
}

mir::ManagedModule valid_module() {
    mir::ManagedValue constant;
    constant.id = {0};
    constant.type = {2};
    constant.kind = mir::ValueKind::ConstantInteger;
    constant.integer = 7;

    mir::ManagedBlock entry;
    entry.id = {0};
    entry.values = {{0}};
    entry.terminator.kind = mir::TerminatorKind::Return;
    entry.terminator.value = mir::ValueId{0};
    entry.terminator.effect = {0};
    entry.effect = {0};

    mir::ManagedFunction function;
    function.source = {0};
    function.result_type = {2};
    function.entry = {0};
    function.values = {constant};
    function.effects = {{mir::EffectId{0}, {}, mir::EffectKind::Entry,
                         std::nullopt, std::nullopt, {}}};
    function.blocks = {entry};

    mir::ManagedModule module;
    module.functions.push_back(std::move(function));
    module.definitions.insert(0);
    return module;
}

mir::ManagedModule indirect_module() {
    auto module = valid_module();
    auto& function = module.functions.front();
    auto& address = function.values.front();
    address.kind = mir::ValueKind::FunctionAddress;
    address.type = {4};
    address.callee = hir::FunctionId{0};
    mir::ManagedValue call;
    call.id = {1};
    call.kind = mir::ValueKind::Call;
    call.type = {2};
    call.call_signature = hir::TypeId{3};
    call.operands = {{0}};
    call.effect_input = mir::EffectId{0};
    call.effect_output = mir::EffectId{1};
    function.values.push_back(call);
    function.effects.push_back({{1},
                                {},
                                mir::EffectKind::Operation,
                                mir::EffectId{0},
                                mir::ValueId{1},
                                {}});
    function.blocks[0].values.push_back({1});
    function.blocks[0].terminator.value = mir::ValueId{1};
    function.blocks[0].terminator.effect = {1};
    return module;
}

mir::ManagedModule diamond_module() {
    auto module = valid_module();
    auto& function = module.functions.front();
    function.values.clear();
    function.blocks.clear();

    const auto add_constant = [&](std::uint32_t id, hir::TypeId type,
                                  std::uint64_t integer) {
        mir::ManagedValue value;
        value.id = {id};
        value.type = type;
        value.kind = mir::ValueKind::ConstantInteger;
        value.integer = integer;
        function.values.push_back(std::move(value));
    };
    add_constant(0, {1}, 1);
    add_constant(1, {2}, 10);
    add_constant(2, {2}, 20);
    mir::ManagedValue phi;
    phi.id = {3};
    phi.type = {2};
    phi.kind = mir::ValueKind::Phi;
    phi.incoming = {{{1}, {1}}, {{2}, {2}}};
    function.values.push_back(std::move(phi));

    mir::ManagedBlock entry;
    entry.id = {0};
    entry.values = {{0}};
    entry.terminator = {mir::TerminatorKind::ConditionalBranch, {},
                        mir::ValueId{0}, {{1}, {2}}, {0}};
    entry.effect = {0};
    mir::ManagedBlock left;
    left.id = {1};
    left.values = {{1}};
    left.predecessors = {{0}};
    left.terminator = {mir::TerminatorKind::Branch, {}, std::nullopt, {{3}}, {1}};
    left.effect = {1};
    mir::ManagedBlock right;
    right.id = {2};
    right.values = {{2}};
    right.predecessors = {{0}};
    right.terminator = {mir::TerminatorKind::Branch, {}, std::nullopt, {{3}}, {2}};
    right.effect = {2};
    mir::ManagedBlock merge;
    merge.id = {3};
    merge.values = {{3}};
    merge.predecessors = {{1}, {2}};
    merge.terminator = {mir::TerminatorKind::Return, {}, mir::ValueId{3}, {}, {3}};
    merge.effect = {3};
    function.blocks = {entry, left, right, merge};
    function.effects = {
        {mir::EffectId{0}, {}, mir::EffectKind::Entry,
         std::nullopt, std::nullopt, {}},
        {mir::EffectId{1}, {}, mir::EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}}},
        {mir::EffectId{2}, {}, mir::EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}}},
        {mir::EffectId{3}, {}, mir::EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}, {{2}, {2}}}},
    };
    return module;
}

mir::ManagedModule effectful_module() {
    auto module = valid_module();
    auto& function = module.functions.front();
    function.slots = {{{0}, {}, {2}, "value", std::nullopt, false,
                       false, false, 1, std::nullopt}};

    mir::ManagedValue start;
    start.id = {1};
    start.type = {0};
    start.kind = mir::ValueKind::LifetimeStart;
    start.slot = mir::SlotId{0};
    start.effect_input = mir::EffectId{0};
    start.effect_output = mir::EffectId{1};
    mir::ManagedValue store;
    store.id = {2};
    store.type = {0};
    store.kind = mir::ValueKind::Store;
    store.slot = mir::SlotId{0};
    store.effect_input = mir::EffectId{1};
    store.effect_output = mir::EffectId{2};
    store.operands = {{0}};
    mir::ManagedValue load;
    load.id = {3};
    load.type = {2};
    load.kind = mir::ValueKind::Load;
    load.slot = mir::SlotId{0};
    load.effect_input = mir::EffectId{2};
    load.effect_output = mir::EffectId{3};
    mir::ManagedValue finish;
    finish.id = {4};
    finish.type = {0};
    finish.kind = mir::ValueKind::LifetimeEnd;
    finish.slot = mir::SlotId{0};
    finish.effect_input = mir::EffectId{3};
    finish.effect_output = mir::EffectId{4};
    function.values.push_back(start);
    function.values.push_back(store);
    function.values.push_back(load);
    function.values.push_back(finish);
    function.blocks[0].values = {{0}, {1}, {2}, {3}, {4}};
    function.blocks[0].terminator.value = mir::ValueId{3};
    function.blocks[0].terminator.effect = {4};
    function.effects.push_back({{1}, {}, mir::EffectKind::Operation,
                                mir::EffectId{0}, mir::ValueId{1}, {}});
    function.effects.push_back({{2}, {}, mir::EffectKind::Operation,
                                mir::EffectId{1}, mir::ValueId{2}, {}});
    function.effects.push_back({{3}, {}, mir::EffectKind::Operation,
                                mir::EffectId{2}, mir::ValueId{3}, {}});
    function.effects.push_back({{4}, {}, mir::EffectKind::Operation,
                                mir::EffectId{3}, mir::ValueId{4}, {}});
    return module;
}

bool expect_invalid(mir::ManagedModule module, std::string_view expected) {
    auto hir = hir_fixture();
    std::ostringstream output;
    Diagnostics diagnostics(output);
    if (mir::verify(module, hir, diagnostics)) {
        std::cerr << "verifier accepted invalid MIR; expected: " << expected << '\n';
        return false;
    }
    if (output.str().find(expected) == std::string::npos) {
        std::cerr << "missing verifier diagnostic '" << expected << "':\n"
                  << output.str();
        return false;
    }
    return true;
}

} // namespace

int main() {
    {
        auto hir = hir_fixture();
        auto module = indirect_module();
        std::ostringstream output;
        Diagnostics diagnostics(output);
        if (!mir::verify(module, hir, diagnostics)) {
            std::cerr << "verifier rejected typed indirect call:\n"
                      << output.str();
            return 1;
        }
    }
    {
        auto hir = hir_fixture();
        auto module = valid_module();
        std::ostringstream output;
        Diagnostics diagnostics(output);
        if (!mir::verify(module, hir, diagnostics)) {
            std::cerr << "verifier rejected valid MIR:\n" << output.str();
            return 1;
        }
    }
    {
        auto hir = hir_fixture();
        auto module = effectful_module();
        std::ostringstream output;
        Diagnostics diagnostics(output);
        if (!mir::verify(module, hir, diagnostics)) {
            std::cerr << "verifier rejected valid effectful MIR:\n" << output.str();
            return 1;
        }
    }

    unsigned failures = 0;
    {
        auto module = valid_module();
        module.functions[0].blocks[0].terminator = {};
        failures += !expect_invalid(std::move(module), "block has no terminator");
    }
    {
        auto module = valid_module();
        module.functions[0].blocks[0].terminator = {
            mir::TerminatorKind::Branch, {}, std::nullopt, {{99}}, {0}};
        failures += !expect_invalid(std::move(module), "successor is out of range");
    }
    {
        auto module = valid_module();
        module.functions[0].blocks[0].values.push_back({0});
        failures += !expect_invalid(std::move(module), "value has multiple definitions");
    }
    {
        auto module = diamond_module();
        module.functions[0].values[3].incoming[1] = {{1}, {1}};
        failures += !expect_invalid(std::move(module),
                                    "phi names a predecessor more than once");
    }
    {
        auto module = diamond_module();
        auto& function = module.functions[0];
        function.blocks.pop_back();
        function.values.pop_back();
        function.effects.pop_back();
        function.blocks[1].terminator = {
            mir::TerminatorKind::Return, {}, mir::ValueId{1}, {}, {1}};
        function.blocks[2].terminator = {
            mir::TerminatorKind::Return, {}, mir::ValueId{1}, {}, {2}};
        failures += !expect_invalid(std::move(module),
            "terminator value definition does not dominate its use");
    }
    {
        auto module = valid_module();
        module.definitions.clear();
        failures += !expect_invalid(std::move(module), "definition index omits function");
    }
    {
        auto module = valid_module();
        module.functions[0].blocks[0].terminator.effect = {99};
        failures += !expect_invalid(std::move(module), "terminator effect is out of range");
    }
    {
        auto module = diamond_module();
        module.functions[0].effects[3].incoming[1].effect = {1};
        failures += !expect_invalid(std::move(module),
                                    "effect phi input does not match edge");
    }
    {
        auto module = effectful_module();
        module.functions[0].values[2].effect_input = mir::EffectId{0};
        failures += !expect_invalid(std::move(module), "broken operation effect chain");
    }
    {
        auto module = indirect_module();
        module.functions[0].values[1].operands.clear();
        failures += !expect_invalid(std::move(module),
                                    "indirect call has no target operand");
    }
    {
        auto module = indirect_module();
        module.functions[0].values[1].callee = hir::FunctionId{0};
        failures +=
            !expect_invalid(std::move(module), "call has an invalid callee");
    }
    {
        auto module = indirect_module();
        module.functions[0].values[0].type = {2};
        failures += !expect_invalid(std::move(module),
                                    "invalid typed function address");
    }
    {
        auto module = indirect_module();
        module.functions[0].values[0].callee = hir::FunctionId{99};
        failures += !expect_invalid(std::move(module),
                                    "invalid typed function address");
    }
    return failures == 0 ? 0 : 1;
}
