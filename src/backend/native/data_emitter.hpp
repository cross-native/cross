// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace cross {
struct CompilerOptions;
class Diagnostics;
class Subtarget;
namespace codegen { class ModuleView; }

namespace native {

// Emits only object entities not already materialized by `owned`.  The caller
// may concatenate the returned text with owned.module_assembly.
[[nodiscard]] std::string emit_data_assembly(const codegen::ModuleView& module,
                                              const Subtarget& subtarget,
                                              const CompilerOptions& options,
                                              Diagnostics& diagnostics);

} // namespace native
} // namespace cross
