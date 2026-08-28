// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_pass.hpp"

#include <utility>

namespace cross::mir {

std::string_view pass_name(PassId pass) {
    switch (pass) {
    case PassId::PromoteScalarSlots: return "promote-scalar-slots";
    case PassId::DeadStoreElimination: return "dead-store-elimination";
    case PassId::CopyPropagation: return "copy-propagation";
    case PassId::ConstantFolding: return "constant-folding";
    case PassId::IntegerSimplification: return "integer-simplification";
    case PassId::BitwiseCanonicalization:
        return "bitwise-canonicalization";
    case PassId::BranchFolding: return "branch-folding";
    case PassId::BranchThreading: return "branch-threading";
    case PassId::ForwardingBlockElimination:
        return "forwarding-block-elimination";
    case PassId::TailMerging: return "tail-merging";
    case PassId::IfConversion: return "if-conversion";
    case PassId::SelectFactoring: return "select-factoring";
    case PassId::RedundantExpressionElimination:
        return "redundant-expression-elimination";
    case PassId::InductionCoalescing: return "induction-coalescing";
    case PassId::AddressInductionStrengthReduction:
        return "address-induction-strength-reduction";
    case PassId::LoopInvariantMotion: return "loop-invariant-motion";
    case PassId::ReductionVectorization: return "reduction-vectorization";
    case PassId::EarlyExitVectorization:
        return "early-exit-vectorization";
    case PassId::LoopUnrolling: return "loop-unrolling";
    case PassId::RecurrenceRebalancing: return "recurrence-rebalancing";
    case PassId::SlpVectorization: return "slp-vectorization";
    case PassId::FloatingSimplification: return "floating-simplification";
    case PassId::DeadCodeElimination: return "dead-code-elimination";
    }
    return "unknown-mir-pass";
}

void FunctionPassManager::add(PassId id, FunctionPass pass) {
    passes_.push_back({id, std::move(pass)});
}

bool FunctionPassManager::run(ManagedModule& module,
                              const PassObserver& observer) const {
    bool changed = false;
    for (auto& function : module.functions) {
        FunctionAnalysisManager analyses(function);
        for (const auto& pass : passes_) {
            const auto result = pass.run(function, analyses);
            if (result.changed) {
                changed = true;
                analyses.invalidate(result.preserved);
            }
            if (observer) observer(pass.id, function, result);
        }
    }
    return changed;
}

} // namespace cross::mir
