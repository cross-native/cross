// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_analysis.hpp"
#include "middle/mir_pass.hpp"

#include <iostream>
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

    return ok ? 0 : 1;
}
