// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"
#include "middle/raw_mir.hpp"
#include "target/subtarget.hpp"

namespace cross::native {
class DebugInfo;
}

namespace cross::aarch64 {

// Prints allocated, frame-finalized AArch64 Machine IR as GNU assembly.
// Every register an instruction names is physical or assigned, and the
// printer writes no register an instruction does not define or clobber.
[[nodiscard]] mir::ManagedAssembly emit_assembly(const machine::Module& module,
                                                 const hir::Module& hir_module,
                                                 const Subtarget& subtarget,
                                                 const CompilerOptions& options,
                                                 Diagnostics& diagnostics,
                                                 native::DebugInfo* debug);

} // namespace cross::aarch64
