// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/raw_mir.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

namespace cross::native {
class DebugInfo;
}

namespace cross::x86_64 {

mir::RawModule lower_raw(const hir::Module& hir_module, const Subtarget& subtarget,
                         Diagnostics& diagnostics);

mir::AssemblyBundle emit_raw_assembly(const mir::RawModule& mir_module,
                                      const mir::ManagedModule& managed_module,
                                      const hir::Module& hir_module,
                                      const Subtarget& subtarget,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics,
                                      native::DebugInfo* debug = nullptr);

} // namespace cross::x86_64
