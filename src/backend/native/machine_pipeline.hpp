// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace cross {
class Diagnostics;
class Subtarget;
class TargetBackend;
struct CompilerOptions;

namespace hir {
class Module;
}
namespace mir {
struct ManagedModule;
}

namespace native {

class DebugInfo;

// Architecture-independent production path. A target supplies selection,
// legality, allocation policy, and assembly printing through TargetBackend;
// the common driver owns structural verification and the standalone audit.
[[nodiscard]] std::string run_machine_pipeline(
    const TargetBackend& backend, mir::ManagedModule& managed_module,
    hir::Module& hir_module, const Subtarget& subtarget,
    const CompilerOptions& options, DebugInfo& debug,
    Diagnostics& diagnostics);

} // namespace native
} // namespace cross
