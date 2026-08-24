// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/backend.hpp"

#include "middle/machine_ir.hpp"
#include "target/backend.hpp"
#include "target/subtarget.hpp"
#include "target/x86_64/dynamic_abi_plan.hpp"
#include "target/x86_64/machine_description.hpp"
#include "target/x86_64/manual_abi_plan.hpp"
#include "target/x86_64/manual_endpoint.hpp"
#include "target/x86_64/native_backend.hpp"
#include "target/x86_64/raw_backend.hpp"

namespace cross::x86_64 {
namespace {

class Backend final : public TargetBackend {
public:
    std::string_view architecture() const override { return "x86-64"; }

    bool validate_hir(
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        if (resolved_bool(options, "m.red-zone") &&
            subtarget.object_format() == ObjectFormat::Coff) {
            diagnostics.command_error(
                "option '-mred-zone' is unavailable for the x86-64 COFF "
                "platform contract");
            return false;
        }
        if (options.code_model == CodeModel::Kernel &&
            options.position_independent) {
            diagnostics.command_error(
                "x86-64 '-mcmodel=kernel' is incompatible with -fpic/-fpie");
            return false;
        }
        for (const auto& object : hir_module.objects) {
            if (!object.is_thread_local) continue;
            if (object.tls_model == "local-dynamic" ||
                object.tls_model == "global-dynamic") {
                diagnostics.error(
                    object.location,
                    "TLS model '" + object.tls_model +
                        "' requires a hidden resolver call; standalone Cross supports call-free static TLS models instead");
                continue;
            }
            if (subtarget.object_format() == ObjectFormat::MachO) {
                diagnostics.error(
                    object.location,
                    "Darwin x86-64 TLS uses the TLV resolver ABI; standalone Cross does not insert that hidden call");
            } else if (subtarget.object_format() != ObjectFormat::Elf &&
                       subtarget.object_format() != ObjectFormat::Coff) {
                diagnostics.error(
                    object.location,
                    "the selected x86-64 object format has no registered call-free TLS access model");
            }
        }
        (void)build_manual_abi_plans(
            hir_module, subtarget.target(), subtarget, options,
            diagnostics);
        return diagnostics.errors() == 0;
    }

    mir::RawModule lower_raw(
        const hir::Module& hir_module, const Subtarget& subtarget,
        Diagnostics& diagnostics) const override {
        return x86_64::lower_raw(hir_module, subtarget, diagnostics);
    }

    mir::AssemblyBundle emit_raw_assembly(
        const mir::RawModule& raw_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        return x86_64::emit_raw_assembly(
            raw_module, managed_module, hir_module, subtarget, options,
            diagnostics);
    }

    bool prepare_managed(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        const auto& target = subtarget.target();
        auto manual_plans = build_manual_abi_plans(
            hir_module, target, subtarget, options, diagnostics);
        if (diagnostics.errors() != 0) return false;

        auto dynamic_plans = build_dynamic_abi_plans(
            managed_module, hir_module, manual_plans, options, diagnostics);
        if (diagnostics.errors() != 0) return false;
        if (mir::specialize_surviving_calls(
                managed_module, hir_module, options)) {
            CompilerOptions cleanup_options = options;
            cleanup_options.inline_functions = false;
            cleanup_options.inline_unit_limit = 0;
            cleanup_options.ipa_cp_clone = false;
            mir::optimize(
                managed_module, hir_module, subtarget, cleanup_options,
                diagnostics);
            if (!mir::verify(
                    managed_module, hir_module, diagnostics)) {
                return false;
            }
            dynamic_plans = build_dynamic_abi_plans(
                managed_module, hir_module, manual_plans, options,
                diagnostics);
        }
        return diagnostics.errors() == 0;
    }

    machine::Module lower_machine(
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        const auto& target = subtarget.target();
        const auto manual_plans = build_manual_abi_plans(
            hir_module, target, subtarget, options, diagnostics);
        if (diagnostics.errors() != 0) return {};
        const auto dynamic_plans = build_dynamic_abi_plans(
            managed_module, hir_module, manual_plans, options, diagnostics);
        if (diagnostics.errors() != 0) return {};

        return x86_64::lower_managed_machine(
            managed_module, hir_module, manual_plans, dynamic_plans,
            subtarget, options, diagnostics);
    }

    bool verify_machine(
        const machine::Module& module, const Subtarget&,
        const CompilerOptions&, Diagnostics& diagnostics) const override {
        bool valid = true;
        for (const auto& function : module.functions) {
            for (const auto& slot : function.stack_slots) {
                if (!slot.hard_register ||
                    slot.hard_register->value < register_views().size()) {
                    continue;
                }
                diagnostics.error(
                    slot.location,
                    "x86-64 Machine IR contains an unknown hard-register "
                    "view " + std::to_string(slot.hard_register->value));
                valid = false;
            }
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.kind ==
                            machine::InstructionKind::Target &&
                        !describe_opcode(instruction.opcode)) {
                        diagnostics.error(
                            instruction.location,
                            "x86-64 Machine IR contains an unknown target "
                            "opcode " +
                                std::to_string(instruction.opcode.value));
                        valid = false;
                    }
                    if (instruction.condition_predicate.empty() ||
                        describe_opcode(instruction.condition_predicate)) {
                        continue;
                    }
                    diagnostics.error(
                        instruction.location,
                        "x86-64 Machine IR contains an unknown condition "
                        "opcode " +
                            std::to_string(
                                instruction.condition_predicate.value));
                    valid = false;
                }
            }
        }
        return valid;
    }

    std::string emit_machine_assembly(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        const auto manual_plans = build_manual_abi_plans(
            hir_module, subtarget.target(), subtarget, options, diagnostics);
        if (diagnostics.errors() != 0) return {};
        const auto dynamic_plans = build_dynamic_abi_plans(
            managed_module, hir_module, manual_plans, options,
            diagnostics);
        if (diagnostics.errors() != 0) return {};
        return emit_managed_machine_assembly(
            machine_module, hir_module, manual_plans, dynamic_plans,
            subtarget, options, diagnostics);
    }
};

} // namespace

const TargetBackend& backend() {
    static const Backend result;
    return result;
}

} // namespace cross::x86_64
