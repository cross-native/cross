// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"
#include "middle/mir.hpp"
#include "middle/raw_mir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace cross {

struct CompilerOptions;
class Diagnostics;
class Subtarget;
struct TargetInfo;

// Production code-generation boundary implemented once per architecture.
// The driver and middle end never name architecture ABI-plan or instruction
// selector types; a RISC backend can use the same HIR/MIR while supplying its
// own legalization, Machine IR, scheduling, allocation, and printer pipeline.
class TargetBackend {
public:
    virtual ~TargetBackend() = default;

    [[nodiscard]] virtual std::string_view architecture() const = 0;

    // Validate architecture-specific HIR contracts early enough that later
    // source/body diagnostics can still be accumulated in the same run.
    [[nodiscard]] virtual bool validate_hir(
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual mir::RawModule lower_raw(
        const hir::Module& hir_module, const Subtarget& subtarget,
        Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual mir::AssemblyBundle emit_raw_assembly(
        const mir::RawModule& raw_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, Diagnostics& diagnostics) const = 0;

    // Runs target legality and ABI planning before any output mode diverges.
    // It may specialize surviving calls, but must leave verified managed MIR
    // as the authoritative representation consumed by native and debug paths.
    [[nodiscard]] virtual bool prepare_managed(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;

    // This nonvirtual entry point runs the common native machine pipeline:
    // target selection, structural and target verification, standalone
    // auditing, then target assembly emission.
    [[nodiscard]] std::string emit_managed_assembly(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const;

    [[nodiscard]] virtual machine::Module lower_machine(
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual bool verify_machine(
        const machine::Module&, const Subtarget&, const CompilerOptions&,
        Diagnostics&) const {
        return true;
    }

    [[nodiscard]] virtual std::string emit_machine_assembly(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;
};

[[nodiscard]] const std::vector<const TargetBackend*>& all_target_backends();
[[nodiscard]] const TargetBackend* target_backend_for(
    const TargetInfo& target);

} // namespace cross
