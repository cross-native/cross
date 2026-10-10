// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/raw_mir.hpp"

#include <string>
#include <vector>

namespace cross {
struct CompilerOptions;
class Diagnostics;
class Subtarget;
namespace codegen { class ModuleView; }

namespace native {

// The native assembly of a group in emission order: each source unit, in
// command-line order, contributes its functions (managed and raw, with their
// jump tables and literals) and then its data objects; group-level symbol
// directives follow.
[[nodiscard]] std::string emit_module_assembly(
    const codegen::ModuleView& module,
    std::vector<mir::FunctionAssembly> functions,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics);

// Emits data objects owned by raw assembly because one or more exact
// subobjects receive patch-cell-address relocations, for LLVM/GIMPLE
// module-level assembly.
[[nodiscard]] std::string emit_patch_data_assembly(
    const codegen::ModuleView& module, const Subtarget& subtarget,
    const CompilerOptions& options, Diagnostics& diagnostics);

} // namespace native
} // namespace cross
