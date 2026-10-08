// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"

#include <string>

namespace cross {

inline std::string llvm_label_name(hir::LabelId label) {
    return "cross.label." + std::to_string(label.value);
}

std::string emit_managed_mir_function(const hir::Module& hir_module,
                                      const mir::ManagedFunction& function,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics);

} // namespace cross
