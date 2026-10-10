// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"
#include "middle/mir.hpp"
#include "middle/raw_mir.hpp"
#include "target/target.hpp"
#include "target/subtarget.hpp"
#include "target/x86_64/dynamic_abi_plan.hpp"
#include "target/x86_64/manual_abi_plan.hpp"

#include <string>

namespace cross::native {
class DebugInfo;
}

namespace cross::x86_64 {

// Legalize verified target-independent managed MIR into x86-64 Machine IR.
// Every virtual register receives a stable spill home as a correctness
// fallback. When f.register-allocation is enabled, liveness/interference
// allocation assigns
// eligible integer, floating, and fixed-vector values to caller-clobbered or
// ABI-preserved registers, with call splitting, cross-ABI bridge saves, and
// finalized unwind-aware frames.
machine::Module lower_managed_machine(const mir::ManagedModule& managed,
                                      const hir::Module& hir_module,
                                      const ManualAbiPlans& manual_plans,
                                      const DynamicAbiPlans& dynamic_plans,
                                      const Subtarget& subtarget,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics);

// Finalize frames and print GNU/LLVM integrated-assembler compatible AT&T
// syntax, one entry per function. The driver lays out these entries, raw
// naked functions, and data in emission order.
mir::ManagedAssembly emit_managed_machine_assembly(machine::Module& module,
                                          const hir::Module& hir_module,
                                          const ManualAbiPlans& manual_plans,
                                          const DynamicAbiPlans& dynamic_plans,
                                          const Subtarget& subtarget,
                                          const CompilerOptions& options,
                                          Diagnostics& diagnostics,
                                          native::DebugInfo* debug = nullptr);

} // namespace cross::x86_64
