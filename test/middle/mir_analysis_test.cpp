// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_analysis.hpp"
#include "middle/mir_pass.hpp"
#include "middle/mir_transform.hpp"

#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

bool expect(bool condition, std::string_view message) {
    if (condition) return true;
    std::cerr << "mir analysis test: " << message << '\n';
    return false;
}

cross::mir::ManagedFunction loop_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.entry = {0};
    function.values.resize(3);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
    }
    function.values[0].kind = ValueKind::ConstantInteger;
    function.values[1].kind = ValueKind::Phi;
    function.values[1].incoming = {{{0}, {0}}, {{2}, {2}}};
    function.values[2].kind = ValueKind::Binary;
    function.values[2].operands = {{1}, {0}};

    function.blocks.resize(4);
    for (std::uint32_t index = 0; index < function.blocks.size(); ++index) {
        function.blocks[index].id = {index};
    }
    function.blocks[0].values = {{0}};
    function.blocks[0].terminator.kind = TerminatorKind::Branch;
    function.blocks[0].terminator.successors = {{1}};

    function.blocks[1].values = {{1}};
    function.blocks[1].predecessors = {{0}, {2}};
    function.blocks[1].terminator.kind =
        TerminatorKind::ConditionalBranch;
    function.blocks[1].terminator.value = ValueId{1};
    function.blocks[1].terminator.successors = {{2}, {3}};

    function.blocks[2].values = {{2}};
    function.blocks[2].predecessors = {{1}};
    function.blocks[2].terminator.kind = TerminatorKind::Branch;
    function.blocks[2].terminator.successors = {{1}};

    function.blocks[3].predecessors = {{1}};
    function.blocks[3].terminator.kind = TerminatorKind::Return;
    return function;
}

cross::hir::Module transform_hir() {
    using namespace cross;
    hir::Module module;
    hir::Type boolean;
    boolean.kind = hir::Type::Kind::Builtin;
    boolean.builtin = BuiltinType::Bool;
    module.types.push_back(boolean);
    hir::Type integer;
    integer.kind = hir::Type::Kind::Builtin;
    integer.builtin = BuiltinType::U64;
    module.types.push_back(integer);
    hir::Type signed_integer;
    signed_integer.kind = hir::Type::Kind::Builtin;
    signed_integer.builtin = BuiltinType::I64;
    module.types.push_back(signed_integer);
    hir::Type narrow_integer;
    narrow_integer.kind = hir::Type::Kind::Builtin;
    narrow_integer.builtin = BuiltinType::U32;
    module.types.push_back(narrow_integer);
    hir::Function source;
    source.id = {0};
    source.result_type = {1};
    source.ownership = hir::BodyOwnership::ManagedMir;
    hir::Parameter condition;
    condition.type = {0};
    condition.mode = ParameterMode::In;
    source.parameters.push_back(condition);
    module.functions.push_back(std::move(source));
    return module;
}

cross::mir::ManagedFunction rotate_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(7);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::Parameter;
    function.values[1].kind = ValueKind::Parameter;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 64;
    function.values[3].kind = ValueKind::Binary;
    function.values[3].binary = BinaryOperation::Subtract;
    function.values[3].operands = {{2}, {1}};
    function.values[4].kind = ValueKind::Binary;
    function.values[4].binary = BinaryOperation::ShiftLeft;
    function.values[4].operands = {{0}, {1}};
    function.values[5].kind = ValueKind::Binary;
    function.values[5].binary = BinaryOperation::ShiftRightLogical;
    function.values[5].operands = {{0}, {3}};
    function.values[6].kind = ValueKind::Binary;
    function.values[6].binary = BinaryOperation::BitOr;
    function.values[6].operands = {{4}, {5}};
    function.parameters = {{0}, {1}};
    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}}};
    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}, {2}, {3}, {4}, {5}, {6}};
    entry.terminator = {TerminatorKind::Return, {}, ValueId{6}, {}, {0}};
    function.blocks = {std::move(entry)};
    return function;
}

cross::mir::ManagedFunction forwarding_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(5);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::Parameter;
    function.values[0].type = {0};
    function.values[1].kind = ValueKind::ConstantInteger;
    function.values[1].integer = 10;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 20;
    function.values[3].kind = ValueKind::Phi;
    function.values[3].incoming = {{{1}, {1}}, {{2}, {2}}};
    function.values[4].kind = ValueKind::Phi;
    function.values[4].incoming = {{{3}, {3}}};
    function.parameters = {{0}};

    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}};
    entry.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{0}, {{1}, {2}}, {0}};
    ManagedBlock left;
    left.id = {1};
    left.effect = {1};
    left.predecessors = {{0}};
    left.values = {{1}};
    left.terminator = {TerminatorKind::Branch, {}, std::nullopt, {{3}}, {1}};
    ManagedBlock right;
    right.id = {2};
    right.effect = {2};
    right.predecessors = {{0}};
    right.values = {{2}};
    right.terminator = {TerminatorKind::Branch, {}, std::nullopt, {{3}}, {2}};
    ManagedBlock forwarding;
    forwarding.id = {3};
    forwarding.effect = {3};
    forwarding.predecessors = {{1}, {2}};
    forwarding.values = {{3}};
    forwarding.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{4}}, {3}};
    ManagedBlock result;
    result.id = {4};
    result.effect = {4};
    result.predecessors = {{3}};
    result.values = {{4}};
    result.terminator = {TerminatorKind::Return, {}, ValueId{4}, {}, {4}};
    function.blocks = {entry, left, right, forwarding, result};
    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}},
        {EffectId{1},
         {},
         EffectKind::Phi,
         std::nullopt,
         std::nullopt,
         {{{0}, {0}}}},
        {EffectId{2},
         {},
         EffectKind::Phi,
         std::nullopt,
         std::nullopt,
         {{{0}, {0}}}},
        {EffectId{3},
         {},
         EffectKind::Phi,
         std::nullopt,
         std::nullopt,
         {{{1}, {1}}, {{2}, {2}}}},
        {EffectId{4},
         {},
         EffectKind::Phi,
         std::nullopt,
         std::nullopt,
         {{{3}, {3}}}},
    };
    return function;
}

cross::mir::ManagedFunction guarded_loop_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(6);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::ConstantInteger;
    function.values[1].kind = ValueKind::ConstantInteger;
    function.values[1].integer = 3;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 1;
    function.values[3].kind = ValueKind::Phi;
    function.values[3].incoming = {{{0}, {0}}, {{3}, {5}}};
    function.values[4].kind = ValueKind::Binary;
    function.values[4].type = {0};
    function.values[4].binary = BinaryOperation::UnsignedLess;
    function.values[4].operands = {{3}, {1}};
    function.values[5].kind = ValueKind::Binary;
    function.values[5].binary = BinaryOperation::Add;
    function.values[5].operands = {{3}, {2}};

    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}},
        {EffectId{1}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}, {{3}, {3}}}},
        {EffectId{2}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
        {EffectId{3}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{2}, {2}}}},
        {EffectId{4}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
    };

    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}, {2}};
    entry.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {0}};
    ManagedBlock header;
    header.id = {1};
    header.effect = {1};
    header.predecessors = {{0}, {3}};
    header.values = {{3}, {4}};
    header.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{4}, {{2}, {4}}, {1}};
    ManagedBlock body;
    body.id = {2};
    body.effect = {2};
    body.predecessors = {{1}};
    body.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{3}}, {2}};
    ManagedBlock latch;
    latch.id = {3};
    latch.effect = {3};
    latch.predecessors = {{2}};
    latch.values = {{5}};
    latch.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {3}};
    ManagedBlock exit;
    exit.id = {4};
    exit.effect = {4};
    exit.predecessors = {{1}};
    exit.terminator = {
        TerminatorKind::Return, {}, ValueId{3}, {}, {4}};
    function.blocks = {entry, header, body, latch, exit};
    return function;
}

cross::mir::ManagedFunction cross_recurrence_loop_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(6);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::ConstantInteger;
    function.values[1].kind = ValueKind::ConstantInteger;
    function.values[1].integer = 1;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 3;
    function.values[3].kind = ValueKind::Phi;
    function.values[3].incoming = {{{0}, {0}}, {{3}, {4}}};
    function.values[4].kind = ValueKind::Phi;
    function.values[4].incoming = {{{0}, {1}}, {{3}, {3}}};
    function.values[5].kind = ValueKind::Binary;
    function.values[5].type = {0};
    function.values[5].binary = BinaryOperation::UnsignedLess;
    function.values[5].operands = {{3}, {2}};

    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}},
        {EffectId{1}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}, {{3}, {3}}}},
        {EffectId{2}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
        {EffectId{3}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{2}, {2}}}},
        {EffectId{4}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
    };

    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}, {2}};
    entry.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {0}};
    ManagedBlock header;
    header.id = {1};
    header.effect = {1};
    header.predecessors = {{0}, {3}};
    header.values = {{3}, {4}, {5}};
    header.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{5}, {{2}, {4}}, {1}};
    ManagedBlock body;
    body.id = {2};
    body.effect = {2};
    body.predecessors = {{1}};
    body.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{3}}, {2}};
    ManagedBlock latch;
    latch.id = {3};
    latch.effect = {3};
    latch.predecessors = {{2}};
    latch.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {3}};
    ManagedBlock exit;
    exit.id = {4};
    exit.effect = {4};
    exit.predecessors = {{1}};
    exit.terminator = {
        TerminatorKind::Return, {}, ValueId{3}, {}, {4}};
    function.blocks = {entry, header, body, latch, exit};
    return function;
}

cross::mir::ManagedFunction unit_recurrence_add_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(9);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::ConstantInteger;
    function.values[1].kind = ValueKind::ConstantInteger;
    function.values[1].integer = 1;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 16;
    function.values[3].kind = ValueKind::Phi;
    function.values[3].incoming = {{{0}, {0}}, {{2}, {7}}};
    function.values[4].kind = ValueKind::Phi;
    function.values[4].incoming = {{{0}, {0}}, {{2}, {8}}};
    function.values[5].kind = ValueKind::Binary;
    function.values[5].type = {0};
    function.values[5].binary = BinaryOperation::UnsignedLess;
    function.values[5].operands = {{3}, {2}};
    function.values[6].kind = ValueKind::Binary;
    function.values[6].binary = BinaryOperation::Add;
    function.values[6].operands = {{4}, {3}};
    function.values[7].kind = ValueKind::Binary;
    function.values[7].binary = BinaryOperation::Add;
    function.values[7].operands = {{3}, {1}};
    function.values[8].kind = ValueKind::Binary;
    function.values[8].binary = BinaryOperation::Add;
    function.values[8].operands = {{6}, {1}};

    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}},
        {EffectId{1}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}, {{2}, {2}}}},
        {EffectId{2}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
        {EffectId{3}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}}},
    };

    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}, {2}};
    entry.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {0}};
    ManagedBlock header;
    header.id = {1};
    header.effect = {1};
    header.predecessors = {{0}, {2}};
    header.values = {{3}, {4}, {5}};
    header.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{5}, {{2}, {3}}, {1}};
    ManagedBlock latch;
    latch.id = {2};
    latch.effect = {2};
    latch.predecessors = {{1}};
    // Keep the recurrence update after the candidate to exercise pure-value
    // scheduling as well as the algebraic rewrite.
    latch.values = {{6}, {8}, {7}};
    latch.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{1}}, {2}};
    ManagedBlock exit;
    exit.id = {3};
    exit.effect = {3};
    exit.predecessors = {{1}};
    exit.terminator = {
        TerminatorKind::Return, {}, ValueId{4}, {}, {3}};
    function.blocks = {entry, header, latch, exit};
    return function;
}

cross::mir::ManagedFunction zero_extended_xor_chain_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(7);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = index == 1 || index == 2
            ? cross::hir::TypeId{3} : cross::hir::TypeId{1};
    }
    for (std::uint32_t index = 0; index < 3; ++index) {
        function.values[index].kind = ValueKind::Parameter;
        function.values[index].parameter_index = index;
    }
    function.values[3].kind = ValueKind::Cast;
    function.values[3].cast = CastOperation::ZeroExtend;
    function.values[3].operands = {{1}};
    function.values[4].kind = ValueKind::Binary;
    function.values[4].binary = BinaryOperation::BitXor;
    function.values[4].operands = {{0}, {3}};
    function.values[5].kind = ValueKind::Cast;
    function.values[5].cast = CastOperation::ZeroExtend;
    function.values[5].operands = {{2}};
    function.values[6].kind = ValueKind::Binary;
    function.values[6].binary = BinaryOperation::BitXor;
    function.values[6].operands = {{4}, {5}};
    function.parameters = {{0}, {1}, {2}};
    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}}};
    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}, {2}, {3}, {4}, {5}, {6}};
    entry.terminator = {
        TerminatorKind::Return, {}, ValueId{6}, {}, {0}};
    function.blocks = {std::move(entry)};
    return function;
}

cross::mir::ManagedFunction tail_factor_function() {
    using namespace cross::mir;
    ManagedFunction function;
    function.source = {0};
    function.result_type = {1};
    function.entry = {0};
    function.values.resize(9);
    for (std::uint32_t index = 0; index < function.values.size(); ++index) {
        function.values[index].id = {index};
        function.values[index].type = {1};
    }
    function.values[0].kind = ValueKind::Parameter;
    function.values[0].type = {0};
    function.values[0].parameter_index = 0;
    function.values[1].kind = ValueKind::ConstantInteger;
    function.values[1].integer = 100;
    function.values[2].kind = ValueKind::ConstantInteger;
    function.values[2].integer = 1;
    function.values[3].kind = ValueKind::Binary;
    function.values[3].binary = BinaryOperation::Add;
    function.values[3].operands = {{1}, {2}};
    function.values[4].kind = ValueKind::ConstantInteger;
    function.values[4].integer = 2;
    function.values[5].kind = ValueKind::Binary;
    function.values[5].binary = BinaryOperation::Add;
    function.values[5].operands = {{1}, {4}};
    function.values[6].kind = ValueKind::ConstantInteger;
    function.values[6].integer = 3;
    function.values[7].kind = ValueKind::Binary;
    function.values[7].binary = BinaryOperation::Add;
    function.values[7].operands = {{1}, {6}};
    function.values[8].kind = ValueKind::Phi;
    function.values[8].incoming = {
        {{1}, {3}}, {{3}, {5}}, {{4}, {7}}};
    function.parameters = {{0}};

    ManagedBlock entry;
    entry.id = {0};
    entry.effect = {0};
    entry.values = {{0}, {1}};
    entry.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{0},
        {{1}, {2}}, {0}};
    ManagedBlock first;
    first.id = {1};
    first.effect = {1};
    first.predecessors = {{0}};
    first.values = {{2}, {3}};
    first.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{5}}, {1}};
    ManagedBlock test;
    test.id = {2};
    test.effect = {2};
    test.predecessors = {{0}};
    test.terminator = {
        TerminatorKind::ConditionalBranch, {}, ValueId{0},
        {{3}, {4}}, {2}};
    ManagedBlock second;
    second.id = {3};
    second.effect = {3};
    second.predecessors = {{2}};
    second.values = {{4}, {5}};
    second.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{5}}, {3}};
    ManagedBlock third;
    third.id = {4};
    third.effect = {4};
    third.predecessors = {{2}};
    third.values = {{6}, {7}};
    third.terminator = {
        TerminatorKind::Branch, {}, std::nullopt, {{5}}, {4}};
    ManagedBlock join;
    join.id = {5};
    join.effect = {5};
    join.predecessors = {{1}, {3}, {4}};
    join.values = {{8}};
    join.terminator = {
        TerminatorKind::Return, {}, ValueId{8}, {}, {5}};
    function.blocks = {entry, first, test, second, third, join};
    function.effects = {
        {EffectId{0}, {}, EffectKind::Entry, std::nullopt, std::nullopt, {}},
        {EffectId{1}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}}},
        {EffectId{2}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{0}, {0}}}},
        {EffectId{3}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{2}, {2}}}},
        {EffectId{4}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{2}, {2}}}},
        {EffectId{5}, {}, EffectKind::Phi, std::nullopt, std::nullopt,
         {{{1}, {1}}, {{3}, {3}}, {{4}, {4}}}},
    };
    return function;
}

} // namespace

bool local_pointer_analysis_checks() {
    using namespace cross;
    using namespace cross::mir;
    bool ok = true;
    TargetInfo target;
    for (const unsigned width : {32U, 64U}) {
        auto module = transform_hir();
        module.address_bits = width;
        const auto pointer = module.pointer_to({1});
        const auto pointer_pointer = module.pointer_to(pointer);
        ManagedFunction function;
        function.entry = {0};
        function.slots.resize(3);
        for (std::uint32_t i = 0; i < 3; ++i) {
            function.slots[i].id = {i};
            function.slots[i].type = i == 2 ? pointer : hir::TypeId{1};
        }
        function.slots[2].address_taken = true;
        function.blocks.resize(5);
        for (std::uint32_t i = 0; i < 5; ++i) function.blocks[i].id = {i};
        function.blocks[0].terminator.successors = {{1}, {2}};
        function.blocks[1].predecessors = {{0}};
        function.blocks[2].predecessors = {{0}};
        function.blocks[1].terminator.successors = {{3}};
        function.blocks[2].terminator.successors = {{3}};
        // Block 4 is unreachable but deliberately contributes a different phi arm.
        function.blocks[3].predecessors = {{1}, {2}, {4}};
        function.blocks[4].terminator.successors = {{3}};
        const auto add = [&](unsigned block, ValueKind kind, hir::TypeId type,
                             std::vector<ValueId> operands = {}, std::optional<SlotId> slot = {}) {
            ManagedValue value;
            value.id = {static_cast<std::uint32_t>(function.values.size())};
            value.kind = kind;
            value.type = type;
            value.operands = std::move(operands);
            value.slot = slot;
            function.values.push_back(value);
            function.blocks[block].values.push_back(value.id);
            return value.id;
        };
        const auto x = add(0, ValueKind::SlotAddress, pointer, {}, SlotId{0});
        const auto other = add(0, ValueKind::SlotAddress, pointer, {}, SlotId{1});
        const auto cell = add(0, ValueKind::SlotAddress, pointer_pointer, {}, SlotId{2});
        add(1, ValueKind::Store, {1}, {x}, SlotId{2});
        const auto right_store = add(2, ValueKind::Store, {1}, {x}, SlotId{2});
        const auto loaded = add(3, ValueKind::Load, pointer, {}, SlotId{2});
        const auto phi = add(3, ValueKind::Phi, pointer);
        function.values[phi.value].incoming = {{{1}, x}, {{2}, x}, {{4}, other}};
        add(3, ValueKind::PointerStore, {1}, {cell, other});
        const auto replaced = add(3, ValueKind::PointerLoad, pointer, {cell});
        const auto one = add(3, ValueKind::ConstantInteger, {2});
        function.values[one.value].integer = 1;
        const auto scaled = add(3, ValueKind::IndexedAddress, pointer_pointer, {cell, one});
        const auto negative = add(3, ValueKind::Unary, {2}, {one});
        function.values[negative.value].unary = UnaryOperation::Negate;
        const auto back = add(3, ValueKind::IndexedAddress, pointer_pointer, {scaled, negative});
        add(3, ValueKind::Call, {1});
        const auto after_call = add(3, ValueKind::Load, pointer, {}, SlotId{2});
        LocalPointerAnalysis analysis(function, module, target);
        ok &= expect(analysis.targets(loaded).definite() == LocalAddress{SlotId{0}, 0},
                     "equal CFG incoming pointer cells should agree and retain load-time identity");
        ok &= expect(analysis.targets(phi).definite() == LocalAddress{SlotId{0}, 0},
                     "unreachable phi edges must not poison provenance");
        ok &= expect(analysis.targets(replaced).definite() == LocalAddress{SlotId{1}, 0},
                     "indirect pointer-cell store should update subsequent loads");
        ok &= expect(analysis.targets(scaled).definite() == LocalAddress{SlotId{2}, width / 8} &&
                     analysis.targets(back).definite() == LocalAddress{SlotId{2}, 0},
                     "signed pointer displacement must use model-selected pointer width");
        ok &= expect(!analysis.targets(after_call).definite() && analysis.targets(after_call).unknown,
                     "a call cannot preserve a must-alias fact for an addressable pointer cell");

        function.values[right_store.value].operands = {other};
        LocalPointerAnalysis ambiguous(function, module, target);
        ok &= expect(!ambiguous.targets(loaded).definite() && ambiguous.targets(loaded).addresses.size() == 2,
                     "different CFG pointer cells must retain both possible read targets");

        // A loop that keeps advancing its pointer must converge by losing the
        // displacement, not by forgetting the possible original local cell.
        function.blocks.resize(3);
        function.values.resize(3);
        for (auto& block : function.blocks) { block.values.clear(); block.predecessors.clear(); }
        function.blocks[0].values = {x, other, cell};
        function.blocks[0].terminator.successors = {{1}};
        function.blocks[1].predecessors = {{0}, {1}};
        function.blocks[1].terminator.successors = {{1}, {2}};
        function.blocks[2].predecessors = {{1}};
        function.blocks[2].terminator.successors.clear();
        add(0, ValueKind::Store, {1}, {x}, SlotId{2});
        const auto step = add(0, ValueKind::ConstantInteger, {2});
        function.values[step.value].integer = 1;
        const auto current = add(1, ValueKind::Load, pointer, {}, SlotId{2});
        const auto next = add(1, ValueKind::IndexedAddress, pointer, {current, step});
        add(1, ValueKind::Store, {1}, {next}, SlotId{2});
        LocalPointerAnalysis loop(function, module, target);
        ok &= expect(!loop.targets(current).definite() &&
                     loop.targets(current).addresses == std::vector<LocalAddress>{{SlotId{0}, std::nullopt}},
                     "loop widening must preserve may-alias identity and terminate");
    }
    return ok;
}

int main() {
    using namespace cross::mir;
    bool ok = local_pointer_analysis_checks();
    auto function = loop_function();

    UseLists uses(function);
    ok &= expect(uses.uses({0}).size() == 2,
                 "constant should have phi and binary uses");
    ok &= expect(uses.uses({1}).size() == 2,
                 "phi should have binary and terminator uses");
    ok &= expect(uses.definition_block({2}) == BlockId{2},
                 "definition block should be recorded");

    DominatorTree dominators(function);
    ok &= expect(dominators.dominates({0}, {3}),
                 "entry should dominate exit");
    ok &= expect(dominators.dominates({1}, {2}),
                 "loop header should dominate latch");
    ok &= expect(!dominators.dominates({2}, {1}),
                 "latch should not dominate header");
    ok &= expect(dominators.immediate_dominator({2}) == BlockId{1},
                 "latch immediate dominator should be header");

    LoopForest loops(function, dominators);
    ok &= expect(loops.loops().size() == 1,
                 "one natural loop should be discovered");
    ok &= expect(loops.canonical_loops().size() == 1,
                 "dedicated preheader should make the loop canonical");
    if (!loops.canonical_loops().empty()) {
        const auto& loop = loops.canonical_loops().front();
        ok &= expect(loop.header == BlockId{1} &&
                         loop.preheader == BlockId{0} &&
                         loop.blocks.contains(1) && loop.blocks.contains(2),
                     "canonical loop shape should be preserved");
    }

    ManagedModule module;
    module.functions.push_back(std::move(function));
    FunctionPassManager manager;
    bool preserved_cfg_seen = false;
    bool invalidated_cfg_seen = false;
    manager.add(
        PassId::CopyPropagation,
        [](ManagedFunction&, FunctionAnalysisManager& analyses) {
            (void)analyses.uses();
            (void)analyses.dominators();
            (void)analyses.loops();
            return PassResult::changed_values();
        });
    manager.add(
        PassId::LoopInvariantMotion,
        [&](ManagedFunction&, FunctionAnalysisManager& analyses) {
            preserved_cfg_seen =
                !analyses.cached(AnalysisKind::Uses) &&
                analyses.cached(AnalysisKind::Dominators) &&
                analyses.cached(AnalysisKind::Loops);
            return PassResult::changed_cfg();
        });
    manager.add(
        PassId::BranchFolding,
        [&](ManagedFunction&, FunctionAnalysisManager& analyses) {
            invalidated_cfg_seen =
                !analyses.cached(AnalysisKind::Uses) &&
                !analyses.cached(AnalysisKind::Dominators) &&
                !analyses.cached(AnalysisKind::Loops);
            return PassResult::unchanged();
        });
    ok &= expect(manager.run(module), "changed passes should be reported");
    ok &= expect(preserved_cfg_seen,
                 "value-only pass should preserve CFG analyses");
    ok &= expect(invalidated_cfg_seen,
                 "CFG-changing pass should invalidate dependent analyses");
    ok &= expect(pass_name(PassId::LoopInvariantMotion) ==
                     "loop-invariant-motion",
                 "typed pass IDs should retain diagnostic names");
    ok &= expect(pass_name(PassId::TailMerging) == "tail-merging",
                 "tail merging should retain a typed pass name");
    ok &= expect(pass_name(PassId::LoopRotation) == "loop-rotation",
                 "loop rotation should retain a typed pass name");
    ok &= expect(pass_name(PassId::UnitRecurrenceReassociation) ==
                     "unit-recurrence-reassociation",
                 "unit recurrence reassociation should retain a pass name");

    auto hir = transform_hir();
    auto unit_recurrence = unit_recurrence_add_function();
    DominatorTree recurrence_dominators(unit_recurrence);
    LoopForest recurrence_loops(unit_recurrence, recurrence_dominators);
    UseLists recurrence_uses(unit_recurrence);
    ok &= expect(
        reassociate_unit_recurrence_adds(
            unit_recurrence, hir, recurrence_loops.canonical_loops(),
            recurrence_uses),
        "unit recurrence value should be reused by a neighboring add");
    ok &= expect(
        unit_recurrence.values[8].operands ==
                std::vector<ValueId>{{4}, {7}} &&
            unit_recurrence.blocks[2].values ==
                std::vector<ValueId>{{6}, {7}, {8}},
        "reassociation should reuse the update and preserve SSA order");
    ManagedModule reassociated;
    reassociated.functions.push_back(unit_recurrence);
    reassociated.definitions.insert(0);
    std::ostringstream reassociated_diagnostics_text;
    cross::Diagnostics reassociated_diagnostics(
        reassociated_diagnostics_text);
    ok &= expect(verify(reassociated, hir, reassociated_diagnostics),
                 "reassociated recurrence should satisfy MIR verification");
    if (!reassociated_diagnostics_text.str().empty()) {
        std::cerr << reassociated_diagnostics_text.str();
    }

    auto xor_chain = zero_extended_xor_chain_function();
    ok &= expect(factor_zero_extended_bitwise_chains(xor_chain, hir),
                 "equal narrow XOR operands should share one extension");
    const auto xor_result = *xor_chain.blocks.front().terminator.value;
    const auto& factored_xor = xor_chain.values[xor_result.value];
    const auto shared_extend = factored_xor.operands[1];
    const auto& extension = xor_chain.values[shared_extend.value];
    const auto& narrow_xor =
        xor_chain.values[extension.operands.front().value];
    ok &= expect(
        factored_xor.kind == ValueKind::Binary &&
            factored_xor.binary == BinaryOperation::BitXor &&
            factored_xor.operands.front() == ValueId{0} &&
            extension.kind == ValueKind::Cast &&
            extension.cast == CastOperation::ZeroExtend &&
            narrow_xor.kind == ValueKind::Binary &&
            narrow_xor.binary == BinaryOperation::BitXor &&
            narrow_xor.type == cross::hir::TypeId{3} &&
            narrow_xor.operands == std::vector<ValueId>{{1}, {2}},
        "wide XOR chain should become wide xor zext(narrow xor)");
    ManagedModule factored_xor_module;
    factored_xor_module.functions.push_back(xor_chain);
    factored_xor_module.definitions.insert(0);
    std::ostringstream xor_diagnostics_text;
    cross::Diagnostics xor_diagnostics(xor_diagnostics_text);
    ok &= expect(verify(factored_xor_module, hir, xor_diagnostics),
                 "factored extension chain should satisfy MIR verification");
    if (!xor_diagnostics_text.str().empty()) {
        std::cerr << xor_diagnostics_text.str();
    }

    auto nonunit_recurrence = unit_recurrence_add_function();
    nonunit_recurrence.values[1].integer = 2;
    DominatorTree nonunit_dominators(nonunit_recurrence);
    LoopForest nonunit_loops(nonunit_recurrence, nonunit_dominators);
    UseLists nonunit_uses(nonunit_recurrence);
    ok &= expect(
        !reassociate_unit_recurrence_adds(
            nonunit_recurrence, hir,
            nonunit_loops.canonical_loops(), nonunit_uses),
        "non-unit recurrence must not use the specialized reassociation");

    auto signed_recurrence = unit_recurrence_add_function();
    signed_recurrence.result_type = {2};
    for (auto& value : signed_recurrence.values) {
        if (value.type == cross::hir::TypeId{1}) value.type = {2};
    }
    DominatorTree signed_dominators(signed_recurrence);
    LoopForest signed_loops(signed_recurrence, signed_dominators);
    UseLists signed_uses(signed_recurrence);
    ok &= expect(
        !reassociate_unit_recurrence_adds(
            signed_recurrence, hir, signed_loops.canonical_loops(),
            signed_uses),
        "signed recurrence must retain source overflow evaluation order");

    auto rotate = rotate_function();
    ok &= expect(canonicalize_bitwise_operations(rotate, hir),
                 "shift/or funnel should canonicalize in MIR");
    ok &=
        expect(rotate.values[6].binary == BinaryOperation::RotateLeft &&
                   rotate.values[6].operands == std::vector<ValueId>{{0}, {1}},
               "rotate canonical form should retain value and count");

    auto forwarding = forwarding_function();
    ok &= expect(eliminate_forwarding_blocks(forwarding),
                 "phi-only forwarding block should be eliminated in MIR");
    ok &= expect(forwarding.blocks.size() == 4 &&
                     forwarding.blocks[1].terminator.successors ==
                         std::vector<BlockId>{{3}} &&
                     forwarding.blocks[2].terminator.successors ==
                         std::vector<BlockId>{{3}} &&
                     forwarding.blocks[3].predecessors ==
                         std::vector<BlockId>{{1}, {2}},
                 "forwarding CFG should be rewired and compacted");
    const auto result_id = forwarding.blocks.back().values.front();
    const auto& result = forwarding.values[result_id.value];
    ok &= expect(result.kind == ValueKind::Phi &&
                     result.incoming ==
                         std::vector<PhiIncoming>{{{1}, {1}}, {{2}, {2}}},
                 "destination phi should be translated through forwarding phi");
    ManagedModule transformed;
    transformed.functions.push_back(forwarding);
    transformed.definitions.insert(0);
    std::ostringstream diagnostics_text;
    cross::Diagnostics diagnostics(diagnostics_text);
    ok &= expect(verify(transformed, hir, diagnostics),
                 "rewritten forwarding CFG should satisfy MIR verification");
    if (!diagnostics_text.str().empty()) {
        std::cerr << diagnostics_text.str();
    }

    auto labeled_forwarding = forwarding_function();
    ManagedBlock isolated_label;
    isolated_label.id = {5};
    isolated_label.effect = {5};
    isolated_label.terminator = {TerminatorKind::Branch, {}, std::nullopt, {{6}}, {5}};
    ManagedBlock isolated_tail;
    isolated_tail.id = {6};
    isolated_tail.effect = {6};
    isolated_tail.predecessors = {{5}};
    isolated_tail.values = {{5}};
    isolated_tail.terminator = {TerminatorKind::Return, {}, ValueId{5}, {}, {6}};
    ManagedValue isolated_result;
    isolated_result.id = {5};
    isolated_result.type = {1};
    isolated_result.kind = ValueKind::ConstantInteger;
    isolated_result.integer = 42;
    labeled_forwarding.values.push_back(isolated_result);
    labeled_forwarding.blocks.push_back(isolated_label);
    labeled_forwarding.blocks.push_back(isolated_tail);
    labeled_forwarding.effects.push_back(
        {EffectId{5}, {}, EffectKind::Phi, std::nullopt, std::nullopt, {}});
    labeled_forwarding.effects.push_back(
        {EffectId{6}, {}, EffectKind::Phi, std::nullopt, std::nullopt, {{{5}, {5}}}});
    labeled_forwarding.labels.push_back({{0}, {5}});
    auto label_hir = hir;
    cross::hir::Label isolated_entity;
    isolated_entity.id = {0};
    isolated_entity.owner = {0};
    label_hir.labels.push_back(isolated_entity);
    label_hir.functions[0].labels.push_back({0});
    ok &= expect(eliminate_forwarding_blocks(labeled_forwarding) &&
                     labeled_forwarding.blocks.size() == 6 &&
                     labeled_forwarding.labels.front().block == BlockId{4} &&
                     labeled_forwarding.blocks[4].terminator.successors ==
                         std::vector<BlockId>{{5}},
                 "forwarding cleanup must preserve an isolated label and its continuation");
    ManagedModule labeled_module;
    labeled_module.functions.push_back(labeled_forwarding);
    labeled_module.definitions.insert(0);
    std::ostringstream labeled_diagnostics_text;
    cross::Diagnostics labeled_diagnostics(labeled_diagnostics_text);
    ok &= expect(verify(labeled_module, label_hir, labeled_diagnostics),
                 "isolated labels must not prevent forwarding CFG/value/effect repair");
    if (!labeled_diagnostics_text.str().empty()) std::cerr << labeled_diagnostics_text.str();

    auto guarded = guarded_loop_function();
    ok &= expect(rotate_guarded_loops(guarded),
                 "guarded loop should rotate to a bottom test");
    ok &= expect(
        guarded.blocks.size() == 5 &&
            guarded.blocks[1].predecessors == std::vector<BlockId>{{0}} &&
            guarded.blocks[2].predecessors ==
                std::vector<BlockId>{{1}, {3}} &&
            guarded.blocks[4].predecessors ==
                std::vector<BlockId>{{1}, {3}} &&
            guarded.blocks[2].terminator.successors ==
                std::vector<BlockId>{{3}} &&
            guarded.blocks[3].terminator.successors ==
                std::vector<BlockId>{{2}, {4}},
        "rotation should preserve an initial guard and reuse the latch test");
    if (guarded.blocks.size() == 5 &&
        !guarded.blocks[2].values.empty() &&
        !guarded.blocks[4].values.empty()) {
        const auto& body_phi =
            guarded.values[guarded.blocks[2].values.front().value];
        const auto& exit_phi =
            guarded.values[guarded.blocks[4].values.front().value];
        ok &= expect(
            body_phi.kind == ValueKind::Phi &&
                body_phi.incoming ==
                    std::vector<PhiIncoming>{{{1}, {3}}, {{3}, {5}}} &&
                exit_phi.kind == ValueKind::Phi &&
                exit_phi.incoming ==
                    std::vector<PhiIncoming>{{{1}, {3}}, {{3}, {5}}},
            "rotation should distinguish recurrence and live-out PHIs");
        ok &= expect(
            guarded.effects.size() == 5 &&
                guarded.effects[guarded.blocks[2].effect.value]
                        .incoming.back().predecessor == BlockId{3} &&
                guarded.effects[guarded.blocks[2].effect.value]
                        .incoming.back().effect == EffectId{3} &&
                guarded.effects[guarded.blocks[4].effect.value]
                        .incoming.back().predecessor == BlockId{3} &&
                guarded.effects[guarded.blocks[4].effect.value]
                        .incoming.back().effect == EffectId{3},
            "rotation should repair effect PHIs on both outgoing edges");
    }
    ManagedModule rotated_module;
    rotated_module.functions.push_back(guarded);
    rotated_module.definitions.insert(0);
    std::ostringstream rotated_diagnostics_text;
    cross::Diagnostics rotated_diagnostics(rotated_diagnostics_text);
    ok &= expect(verify(rotated_module, hir, rotated_diagnostics),
                 "rotated loop should preserve MIR value/effect SSA");
    ok &= expect(!rotate_guarded_loops(guarded),
                 "already rotated loop should remain stable");
    if (!rotated_diagnostics_text.str().empty()) {
        std::cerr << rotated_diagnostics_text.str();
    }

    auto header_invariant = guarded_loop_function();
    header_invariant.blocks[0].values = {{0}, {2}};
    header_invariant.blocks[1].values = {{3}, {1}, {4}};
    ok &= expect(rotate_guarded_loops(header_invariant),
                 "header-local guard invariants should remain reusable");
    if (header_invariant.blocks.size() == 5) {
        const auto& test = header_invariant.blocks[3];
        ok &= expect(
            test.values.size() == 2 &&
                header_invariant.values[test.values.back().value].kind ==
                    ValueKind::Binary &&
                header_invariant.values[test.values.back().value].operands[1] ==
                    ValueId{1},
            "the one-time guard should dominate invariant rotated uses");
    }
    ManagedModule header_invariant_module;
    header_invariant_module.functions.push_back(header_invariant);
    header_invariant_module.definitions.insert(0);
    std::ostringstream header_invariant_diagnostics_text;
    cross::Diagnostics header_invariant_diagnostics(
        header_invariant_diagnostics_text);
    ok &= expect(verify(header_invariant_module, hir,
                        header_invariant_diagnostics),
                 "reused header guard invariants should preserve MIR SSA");
    if (!header_invariant_diagnostics_text.str().empty()) {
        std::cerr << header_invariant_diagnostics_text.str();
    }

    auto body_invariant = guarded_loop_function();
    body_invariant.blocks[0].values = {{0}, {1}};
    body_invariant.blocks[1].values = {{3}, {2}, {4}};
    ok &= expect(rotate_guarded_loops(body_invariant),
                 "header-local body invariants should remain reusable");
    ManagedModule body_invariant_module;
    body_invariant_module.functions.push_back(body_invariant);
    body_invariant_module.definitions.insert(0);
    std::ostringstream body_invariant_diagnostics_text;
    cross::Diagnostics body_invariant_diagnostics(
        body_invariant_diagnostics_text);
    ok &= expect(verify(body_invariant_module, hir,
                        body_invariant_diagnostics),
                 "header body invariants should preserve MIR dominance");
    if (!body_invariant_diagnostics_text.str().empty()) {
        std::cerr << body_invariant_diagnostics_text.str();
    }

    auto cross_recurrence = cross_recurrence_loop_function();
    ok &= expect(rotate_guarded_loops(cross_recurrence),
                 "cross-state recurrence should rotate");
    if (cross_recurrence.blocks.size() == 5 &&
        cross_recurrence.blocks[2].values.size() >= 2) {
        const auto first = cross_recurrence.blocks[2].values[0];
        const auto second = cross_recurrence.blocks[2].values[1];
        const auto& first_phi = cross_recurrence.values[first.value];
        const auto& second_phi = cross_recurrence.values[second.value];
        ok &= expect(
            first_phi.incoming.size() == 2 &&
                first_phi.incoming[1] == PhiIncoming{{3}, second} &&
                second_phi.incoming.size() == 2 &&
                second_phi.incoming[1] == PhiIncoming{{3}, first},
            "rotation should map mutually dependent carried state to body PHIs");
    }
    ManagedModule cross_recurrence_module;
    cross_recurrence_module.functions.push_back(cross_recurrence);
    cross_recurrence_module.definitions.insert(0);
    std::ostringstream cross_recurrence_diagnostics_text;
    cross::Diagnostics cross_recurrence_diagnostics(
        cross_recurrence_diagnostics_text);
    ok &= expect(verify(cross_recurrence_module, hir,
                        cross_recurrence_diagnostics),
                 "rotated cross-state recurrence should preserve MIR SSA");
    if (!cross_recurrence_diagnostics_text.str().empty()) {
        std::cerr << cross_recurrence_diagnostics_text.str();
    }

    auto addressable = forwarding_function();
    addressable.labels.push_back({{0}, {3}});
    ok &= expect(!eliminate_forwarding_blocks(addressable),
                 "address-taken forwarding block must remain observable");

    auto unsafe_tails = tail_factor_function();
    unsafe_tails.values[3].effect_input = EffectId{1};
    ok &= expect(!factor_common_phi_tails(unsafe_tails, hir),
                 "effectful or two-member tails must not be factored");

    auto tails = tail_factor_function();
    ok &= expect(factor_common_phi_tails(tails, hir),
                 "three common integer phi tails should be factored");
    const auto& shared = tails.blocks.back();
    ok &= expect(tails.blocks.size() == 7 && shared.id == BlockId{6} &&
                     shared.predecessors ==
                         std::vector<BlockId>{{1}, {3}, {4}} &&
                     shared.terminator.successors ==
                         std::vector<BlockId>{{5}} &&
                     tails.blocks[5].predecessors ==
                         std::vector<BlockId>{{6}} &&
                     shared.values.size() == 2,
                 "factored tail should own the collapsed CFG edge");
    if (shared.values.size() == 2) {
        const auto& varying = tails.values[shared.values[0].value];
        const auto& factored_result =
            tails.values[shared.values[1].value];
        ok &= expect(
            varying.kind == ValueKind::Phi &&
                varying.incoming ==
                    std::vector<PhiIncoming>{{{1}, {2}}, {{3}, {3}},
                                             {{4}, {4}}} &&
                factored_result.kind == ValueKind::Binary &&
                factored_result.binary == BinaryOperation::Add &&
                factored_result.operands ==
                    std::vector<ValueId>{{1}, shared.values[0]},
            "factored tail should select only the varying operand");
    }
    ManagedModule factored;
    factored.functions.push_back(tails);
    factored.definitions.insert(0);
    std::ostringstream factored_diagnostics_text;
    cross::Diagnostics factored_diagnostics(factored_diagnostics_text);
    ok &= expect(verify(factored, hir, factored_diagnostics),
                 "factored tail should preserve MIR and effect SSA");
    if (!factored_diagnostics_text.str().empty()) {
        std::cerr << factored_diagnostics_text.str();
    }

    return ok ? 0 : 1;
}
