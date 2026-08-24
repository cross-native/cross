// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"
#include "target/abi_lowering.hpp"
#include "target/x86_64/manual_abi_plan.hpp"

#include <optional>
#include <vector>

namespace cross::x86_64 {

// A dynamic plan is a private, whole-compilation call contract. It is built
// only after managed inlining has finished and is shared by the definition and
// every remaining direct call. Registered ABI models remain the fallback for
// every stable boundary.
struct DynamicAbiPlan {
    hir::FunctionId function;
    CallLayout call;
    std::optional<ReturnAssignment> result;
    std::vector<std::string> clobbers;
};

class DynamicAbiPlans {
public:
    [[nodiscard]] const DynamicAbiPlan* find(hir::FunctionId id) const;
    void add(DynamicAbiPlan plan);

    [[nodiscard]] const std::vector<DynamicAbiPlan>& entries() const {
        return entries_;
    }
    [[nodiscard]] std::vector<DynamicAbiPlan>& mutable_entries() {
        return entries_;
    }

private:
    std::vector<DynamicAbiPlan> entries_;
};

DynamicAbiPlans build_dynamic_abi_plans(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const ManualAbiPlans& manual_plans, const CompilerOptions& options,
    Diagnostics& diagnostics);

} // namespace cross::x86_64
