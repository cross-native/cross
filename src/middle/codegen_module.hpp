// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/data_ir.hpp"
#include "middle/hir.hpp"
#include "middle/mir.hpp"
#include "middle/raw_mir.hpp"

namespace cross {
class Diagnostics;

namespace codegen {

// Typed, non-owning boundary between target-independent lowering and output
// components. Backends consume entity IDs and IR nodes through this view;
// source text and serialized LLVM syntax are never compiler-internal transport.
enum class FunctionBodyKind { Declaration, ManagedMir, RawMir, Unlowered };

class ModuleView {
public:
    ModuleView(const hir::Module& hir_module,
               const data::Module& data_module,
               const mir::ManagedModule& managed_module,
               const mir::RawModule& raw_module,
               const mir::AssemblyBundle& raw_assembly)
        : hir_(hir_module), data_(data_module), managed_(managed_module),
          raw_(raw_module), raw_assembly_(raw_assembly) {}

    [[nodiscard]] const hir::Module& hir() const { return hir_; }
    [[nodiscard]] const data::Module& data() const { return data_; }
    [[nodiscard]] const mir::ManagedModule& managed() const { return managed_; }
    [[nodiscard]] const mir::RawModule& raw() const { return raw_; }
    [[nodiscard]] const mir::AssemblyBundle& raw_assembly() const {
        return raw_assembly_;
    }

    [[nodiscard]] FunctionBodyKind body_kind(hir::FunctionId id) const;
    [[nodiscard]] const mir::ManagedFunction* managed_body(
        hir::FunctionId id) const;
    [[nodiscard]] const mir::RawFunction* raw_body(hir::FunctionId id) const;
    [[nodiscard]] bool fully_lowered() const;

private:
    const hir::Module& hir_;
    const data::Module& data_;
    const mir::ManagedModule& managed_;
    const mir::RawModule& raw_;
    const mir::AssemblyBundle& raw_assembly_;
};

// Checks cross-representation IDs and the single-owner body invariant at the
// boundary. Unlowered definitions are structurally valid only long enough for
// the driver to issue a middle-end ownership diagnostic; every output path
// requires ModuleView::fully_lowered().
[[nodiscard]] bool verify(const ModuleView& module, Diagnostics& diagnostics);

// The first object, stack cell, or value whose type, pointee, or array element
// requests alignment: a layout the debugging serializers do not encode.
[[nodiscard]] std::optional<SourceLocation>
requested_alignment_location(const ModuleView& module);

} // namespace codegen
} // namespace cross
