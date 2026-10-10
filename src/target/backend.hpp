// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"
#include "middle/mir.hpp"
#include "middle/raw_mir.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

struct CompilerOptions;
class Diagnostics;
class Subtarget;
struct TargetInfo;

namespace native {
class DebugInfo;
}

// Production code-generation boundary implemented once per architecture.
// The driver and middle end never name architecture ABI-plan or instruction
// selector types; a RISC backend can use the same HIR/MIR while supplying its
// own legalization, Machine IR, scheduling, allocation, and printer pipeline.
class TargetBackend {
public:
    virtual ~TargetBackend() = default;

    [[nodiscard]] virtual std::string_view architecture() const = 0;

    // Exact CPU spelling for the selected external object writer. This is
    // target-owned because Cross CPU/model names need not match LLVM or
    // binutils vocabulary. An empty value asks the writer to use its triple
    // default; instruction selection has already happened before this hook.
    [[nodiscard]] virtual std::string object_writer_cpu(
        const Subtarget&) const {
        return {};
    }
    [[nodiscard]] virtual std::vector<std::string> object_writer_features(
        const Subtarget&) const {
        return {};
    }

    // Some object metadata expresses target-owned ABI facts that a generic
    // external assembler cannot infer from the triple or instruction stream.
    // The default is deliberately a no-op; architecture backends may perform
    // a checked, format-specific finalization after assembly.
    [[nodiscard]] virtual bool finalize_object(
        const std::filesystem::path&, const Subtarget&,
        Diagnostics&) const {
        return true;
    }

    // Validate architecture-specific HIR contracts early enough that later
    // source/body diagnostics can still be accumulated in the same run.
    [[nodiscard]] virtual bool validate_hir(
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual mir::RawModule lower_raw(
        const hir::Module& hir_module, const Subtarget& subtarget,
        Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual mir::AssemblyBundle emit_raw_assembly(
        const mir::RawModule& raw_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, Diagnostics& diagnostics) const = 0;

    // Runs target legality and ABI planning before any output mode diverges.
    // It may specialize surviving calls, but must leave verified managed MIR
    // as the authoritative representation consumed by native and debug paths.
    [[nodiscard]] virtual bool prepare_managed(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;

    // This nonvirtual entry point runs the common native machine pipeline:
    // target selection, structural and target verification, standalone
    // auditing, then target assembly emission.
    [[nodiscard]] mir::ManagedAssembly emit_managed_assembly(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        native::DebugInfo& debug, Diagnostics& diagnostics) const;

    [[nodiscard]] virtual machine::Module lower_machine(
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;

    [[nodiscard]] virtual bool verify_machine(
        const machine::Module&, const Subtarget&, const CompilerOptions&,
        Diagnostics&) const {
        return true;
    }

    [[nodiscard]] virtual mir::ManagedAssembly emit_machine_assembly(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const = 0;

    // Emitters that describe their functions in `debug` (line rows,
    // call-frame directives, and address ranges) override these; the
    // defaults emit the same code and describe nothing.
    [[nodiscard]] virtual mir::AssemblyBundle emit_raw_assembly_with_debug(
        const mir::RawModule& raw_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, native::DebugInfo&,
        Diagnostics& diagnostics) const {
        return emit_raw_assembly(raw_module, managed_module, hir_module,
                                 subtarget, options, diagnostics);
    }
    [[nodiscard]] virtual mir::ManagedAssembly emit_machine_assembly_with_debug(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        native::DebugInfo&, Diagnostics& diagnostics) const {
        return emit_machine_assembly(machine_module, managed_module,
                                     hir_module, subtarget, options,
                                     diagnostics);
    }
};

[[nodiscard]] const std::vector<const TargetBackend*>& all_target_backends();
[[nodiscard]] const TargetBackend* target_backend_for(
    const TargetInfo& target);

} // namespace cross
