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

    mir::AssemblyBundle emit_raw_assembly_with_debug(
        const mir::RawModule& raw_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, native::DebugInfo& debug,
        Diagnostics& diagnostics) const override {
        return x86_64::emit_raw_assembly(
            raw_module, managed_module, hir_module, subtarget, options,
            diagnostics, &debug);
    }

    bool prepare_managed(
        mir::ManagedModule& managed_module, hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        const auto& target = subtarget.target();
        auto manual_plans = build_manual_abi_plans(
            hir_module, target, subtarget, options, diagnostics,
            &managed_module);
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
            hir_module, target, subtarget, options, diagnostics,
            &managed_module);
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
            if (function.frame.program &&
                function.frame.program->stack_pointer !=
                    machine::Register::physical_register(
                        {find_register_view("rsp")->storage_id}, machine::i64)) {
                diagnostics.error(function.location,
                                  "x86-64 frame program must use RSP in 64-bit mode");
                valid = false;
            }
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
            machine::for_each_instruction(
                function, [&](const machine::Instruction& instruction) {
                    if (instruction.kind == machine::InstructionKind::Target &&
                        !describe_opcode(instruction.opcode)) {
                        diagnostics.error(
                            instruction.location,
                            "x86-64 Machine IR contains an unknown target "
                            "opcode " +
                                std::to_string(instruction.opcode.value));
                        valid = false;
                    }
                    if (!instruction.condition_predicate.empty() &&
                        !describe_opcode(instruction.condition_predicate)) {
                        diagnostics.error(
                            instruction.location,
                            "x86-64 Machine IR contains an unknown condition "
                            "opcode " +
                                std::to_string(instruction.condition_predicate.value));
                        valid = false;
                    }
                    const auto opcode = decode_opcode(instruction.opcode);
                    const bool frame_opcode =
                        opcode == Opcode::FrameAdjust || opcode == Opcode::FrameCopy ||
                        opcode == Opcode::FrameSave || opcode == Opcode::FrameRestore ||
                        opcode == Opcode::FramePush || opcode == Opcode::FramePop;
                    if (frame_opcode != instruction.frame_effect.has_value()) {
                        diagnostics.error(instruction.location,
                                          "x86-64 frame opcode and effects disagree");
                        valid = false;
                        return;
                    }
                    if (!frame_opcode)
                        return;
                    const auto& effect = *instruction.frame_effect;
                    const bool push = opcode == Opcode::FramePush;
                    const bool pop = opcode == Opcode::FramePop;
                    const auto expected = opcode == Opcode::FrameAdjust
                                              ? machine::FrameOperation::AdjustStack
                                          : opcode == Opcode::FrameCopy
                                              ? machine::FrameOperation::CopyBase
                                          : opcode == Opcode::FrameSave || push
                                              ? machine::FrameOperation::Save
                                              : machine::FrameOperation::Restore;
                    const auto base_valid = [&](machine::Register reg) {
                        return reg.id < 16 && reg.mode.bits == 64;
                    };
                    const bool transfer = expected == machine::FrameOperation::Save ||
                                          expected == machine::FrameOperation::Restore;
                    if (effect.operation != expected || !base_valid(effect.base) ||
                        (effect.destination && !base_valid(*effect.destination)) ||
                        (transfer && effect.transfers.size() != 1) ||
                        effect.update != (push  ? machine::FrameUpdate::BeforeMemory
                                          : pop ? machine::FrameUpdate::AfterMemory
                                                : machine::FrameUpdate::None) ||
                        ((push || pop) && effect.stack_delta != (push ? -8 : 8)) ||
                        std::any_of(effect.transfers.begin(), effect.transfers.end(),
                                    [&](const machine::FrameTransfer& item) {
                                        const bool gpr = item.reg.id < 16 &&
                                                         item.reg.mode.bits == 64;
                                        // This form emits legacy MOVDQU (XMM0..15).
                                        const bool simd = item.reg.id >= 16 &&
                                                          item.reg.id < 32 &&
                                                          item.reg.mode.bits == 128;
                                        return (!gpr && !simd) ||
                                               ((push || pop) &&
                                                (!gpr || item.offset != 0));
                                    }) ||
                        (opcode == Opcode::FrameAdjust
                             ? instruction.clobbers !=
                                   std::vector<machine::Register>{
                                       machine::Register::physical_register(
                                           flags_storage, machine::i64)}
                             : !instruction.clobbers.empty())) {
                        diagnostics.error(instruction.location,
                                          "illegal selected x86-64 frame instruction");
                        valid = false;
                    }
                });
        }
        return valid;
    }

    mir::ManagedAssembly emit_machine_assembly(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        return emit_machine(machine_module, managed_module, hir_module,
                            subtarget, options, nullptr, diagnostics);
    }

    mir::ManagedAssembly emit_machine_assembly_with_debug(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        native::DebugInfo& debug, Diagnostics& diagnostics) const override {
        return emit_machine(machine_module, managed_module, hir_module,
                            subtarget, options, &debug, diagnostics);
    }

private:
    mir::ManagedAssembly emit_machine(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module,
        const Subtarget& subtarget, const CompilerOptions& options,
        native::DebugInfo* debug, Diagnostics& diagnostics) const {
        const auto manual_plans = build_manual_abi_plans(
            hir_module, subtarget.target(), subtarget, options, diagnostics,
            &managed_module);
        if (diagnostics.errors() != 0) return {};
        const auto dynamic_plans = build_dynamic_abi_plans(
            managed_module, hir_module, manual_plans, options,
            diagnostics);
        if (diagnostics.errors() != 0) return {};
        return emit_managed_machine_assembly(
            machine_module, hir_module, manual_plans, dynamic_plans,
            subtarget, options, diagnostics, debug);
    }
};

} // namespace

const TargetBackend& backend() {
    static const Backend result;
    return result;
}

} // namespace cross::x86_64
