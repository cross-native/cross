// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/backend.hpp"

#include "backend/native/machine_pipeline.hpp"
#include "target/target.hpp"
#include "target/mips/backend.hpp"
#include "target/x86_64/backend.hpp"

namespace cross {

mir::ManagedAssembly TargetBackend::emit_managed_assembly(
    mir::ManagedModule& managed_module, hir::Module& hir_module,
    const Subtarget& subtarget, const CompilerOptions& options,
    native::DebugInfo& debug, Diagnostics& diagnostics) const {
    return native::run_machine_pipeline(
        *this, managed_module, hir_module, subtarget, options, debug,
        diagnostics);
}

const std::vector<const TargetBackend*>& all_target_backends() {
    static const std::vector<const TargetBackend*> backends{
        &mips::backend(),
        &x86_64::backend(),
    };
    return backends;
}

const TargetBackend* target_backend_for(const TargetInfo& target) {
    for (const auto* backend : all_target_backends()) {
        if (backend->architecture() == target.architecture) return backend;
    }
    return nullptr;
}

} // namespace cross
