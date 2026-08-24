// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend/native/machine_pipeline.hpp"

#include "backend/native/standalone_audit.hpp"
#include "middle/machine_ir.hpp"
#include "target/backend.hpp"

namespace cross::native {

std::string run_machine_pipeline(
    const TargetBackend& backend, mir::ManagedModule& managed_module,
    hir::Module& hir_module, const Subtarget& subtarget,
    const CompilerOptions& options, Diagnostics& diagnostics) {
    auto module = backend.lower_machine(
        managed_module, hir_module, subtarget, options, diagnostics);
    if (diagnostics.errors() != 0 || !machine::verify(module, diagnostics)) {
        return {};
    }
    if (!backend.verify_machine(module, subtarget, options, diagnostics) ||
        !audit_standalone(module, hir_module, diagnostics)) {
        return {};
    }
    return backend.emit_machine_assembly(
        module, managed_module, hir_module, subtarget, options, diagnostics);
}

} // namespace cross::native
