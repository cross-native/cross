// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/mir_analysis.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace cross::mir {

enum class PassId : std::uint8_t {
    PromoteScalarSlots,
    DeadStoreElimination,
    CopyPropagation,
    ConstantFolding,
    IntegerSimplification,
    BitwiseCanonicalization,
    BranchFolding,
    BranchThreading,
    ForwardingBlockElimination,
    TailMerging,
    IfConversion,
    SelectFactoring,
    RedundantExpressionElimination,
    InductionCoalescing,
    LoopInvariantMotion,
    ReductionVectorization,
    EarlyExitVectorization,
    LoopUnrolling,
    RecurrenceRebalancing,
    SlpVectorization,
    FloatingSimplification,
    DeadCodeElimination,
};

[[nodiscard]] std::string_view pass_name(PassId pass);

struct PassResult {
    bool changed{};
    PreservedAnalyses preserved{PreservedAnalyses::all()};

    [[nodiscard]] static constexpr PassResult unchanged() { return {}; }
    [[nodiscard]] static constexpr PassResult changed_cfg() {
        return {true, PreservedAnalyses::none()};
    }
    [[nodiscard]] static constexpr PassResult changed_values() {
        return {true, PreservedAnalyses::cfg()};
    }
};

using FunctionPass =
    std::function<PassResult(ManagedFunction&, FunctionAnalysisManager&)>;
using PassObserver =
    std::function<void(PassId, const ManagedFunction&, const PassResult&)>;

class FunctionPassManager {
public:
    void add(PassId id, FunctionPass pass);
    [[nodiscard]] bool empty() const { return passes_.empty(); }
    [[nodiscard]] bool run(ManagedModule& module,
                           const PassObserver& observer = {}) const;

private:
    struct Entry {
        PassId id;
        FunctionPass run;
    };
    std::vector<Entry> passes_;
};

} // namespace cross::mir
