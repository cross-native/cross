// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"

#include <string>
#include <string_view>

namespace cross {

inline std::string llvm_label_name(hir::LabelId label) {
    return "cross.label." + std::to_string(label.value);
}

// The LLVM intrinsic of a Cross square root, absolute value, or sign
// transfer on the LLVM floating type `type`, such as `llvm.sqrt.f64`.
inline std::string llvm_floating_intrinsic(mir::IntrinsicOperation operation,
                                           std::string_view type) {
    const std::string_view suffix = type == "float" ? "f32"
        : type == "double" ? "f64" : type == "x86_fp80" ? "f80" : "f128";
    const std::string_view name = operation == mir::IntrinsicOperation::Sqrt ? "sqrt"
        : operation == mir::IntrinsicOperation::Fabs ? "fabs" : "copysign";
    return "llvm." + std::string(name) + "." + std::string(suffix);
}

struct TargetInfo;

// The LLVM type of an object, array element, or pointee of `type`, including
// the tail padding of a typedef's alignment request.
std::string llvm_storage_type(const hir::Module& hir_module,
                              const TargetInfo* target, hir::TypeId type);

std::string emit_managed_mir_function(const hir::Module& hir_module,
                                      const mir::ManagedFunction& function,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics);

} // namespace cross
