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

int main() {
    using namespace cross::mir;
    bool ok = true;
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

    auto hir = transform_hir();
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
