// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/backend.hpp"

#include "common/options.hpp"
#include "target/aarch64/abi.hpp"
#include "target/aarch64/emission.hpp"
#include "target/aarch64/frame.hpp"
#include "target/aarch64/machine.hpp"
#include "target/aarch64/selection.hpp"
#include "target/aarch64/spill.hpp"
#include "target/backend.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <string>

namespace cross::aarch64 {
namespace {

// x9-x15 stage operands; an indirect call's target goes in x17, which no
// ABI passes a value in, and x16 is the frame scratch (frame.hpp).
const SpillPolicy spill_policy{{9, 10, 11, 12, 13, 14, 15}, 17};

bool manual_location(const std::optional<std::string>& location) {
    return location && *location != "auto";
}

class Backend final : public TargetBackend {
public:
    std::string_view architecture() const override { return "aarch64"; }

    bool validate_hir(const hir::Module& hir_module, const Subtarget& subtarget,
                      const CompilerOptions& options, Diagnostics& diagnostics) const override {
        if (subtarget.object_format() != ObjectFormat::Elf) {
            diagnostics.command_error("AArch64 code generation supports ELF objects only");
        }
        if (options.code_model != CodeModel::Small) {
            diagnostics.command_error("AArch64 code generation supports only the small code model");
        }
        for (const auto& function : hir_module.functions) {
            if (function.ownership == hir::BodyOwnership::Discarded) continue;
            const bool defined = function.definition != nullptr;
            if (function.naked) {
                diagnostics.error(function.location,
                                  "AArch64 code generation does not yet support naked functions");
                continue;
            }
            if (!defined) continue;
            if (function.variadic) {
                diagnostics.error(function.location,
                                  "AArch64 code generation does not yet support variadic functions");
            }
            if (manual_location(function.result_location) ||
                std::any_of(function.parameters.begin(), function.parameters.end(),
                            [](const hir::Parameter& parameter) {
                                return manual_location(parameter.physical_location);
                            })) {
                diagnostics.error(function.location,
                                  "AArch64 code generation does not yet support manual register "
                                  "endpoints; use a registered ABI");
            }
            const auto interrupt = [](const FunctionDecl* declaration) {
                return declaration && declaration->attribute("interrupt");
            };
            if (interrupt(function.definition) ||
                std::any_of(function.declarations.begin(), function.declarations.end(),
                            interrupt)) {
                diagnostics.error(function.location,
                                  "AArch64 has no registered interrupt ABI");
            }
        }
        return diagnostics.errors() == 0;
    }

    mir::RawModule lower_raw(const hir::Module&, const Subtarget&, Diagnostics&) const override {
        return {};
    }

    mir::AssemblyBundle emit_raw_assembly(const mir::RawModule&, const mir::ManagedModule&,
                                          const hir::Module&, const Subtarget&,
                                          const CompilerOptions&, Diagnostics&) const override {
        return {};
    }

    bool prepare_managed(mir::ManagedModule&, hir::Module&, const Subtarget&,
                         const CompilerOptions&, Diagnostics& diagnostics) const override {
        return diagnostics.errors() == 0;
    }

    machine::Module lower_machine(const mir::ManagedModule& managed_module,
                                  const hir::Module& hir_module, const Subtarget& subtarget,
                                  const CompilerOptions& options,
                                  Diagnostics& diagnostics) const override {
        machine::Module result;
        auto selected = select_module(managed_module, hir_module, subtarget, options, diagnostics);
        for (auto& function : selected) {
            if (!assign_frame_homes(function.function, spill_policy, diagnostics)) continue;
            const auto& entity = hir_module.function(function.function.source);
            const auto* abi = abi_model(entity.abi);
            if (!abi || !finalize_frame(function, entity, *abi, options, diagnostics)) continue;
            result.functions.push_back(std::move(function.function));
        }
        return result;
    }

    bool verify_machine(const machine::Module& module, const Subtarget&, const CompilerOptions&,
                        Diagnostics& diagnostics) const override {
        bool valid = true;
        for (const auto& function : module.functions) {
            const auto allocated = [&](const machine::Register& value) {
                return value.kind == machine::RegisterKind::Physical ||
                       (value.id < function.virtual_register_assignments.size() &&
                        function.virtual_register_assignments[value.id]);
            };
            if (!function.frame.program ||
                function.frame.program->stack_pointer != gpr(stack_pointer, 64)) {
                diagnostics.error(function.location,
                                  "AArch64 frame program must use sp in the address mode");
                valid = false;
            }
            machine::for_each_instruction(function, [&](const machine::Instruction& instruction) {
                const auto opcode = decode_opcode(instruction.opcode);
                if (instruction.kind == machine::InstructionKind::Target &&
                    (opcode == Opcode::None || opcode == Opcode::Phi)) {
                    diagnostics.error(instruction.location,
                                      "AArch64 Machine IR contains an unselected opcode '" +
                                          std::string(opcode_name(instruction.opcode)) + "'");
                    valid = false;
                }
                const bool frame = opcode == Opcode::FrameAdjust || opcode == Opcode::FrameCopy ||
                                   opcode == Opcode::FrameSave || opcode == Opcode::FrameRestore;
                if (frame != instruction.frame_effect.has_value()) {
                    diagnostics.error(instruction.location,
                                      "AArch64 frame opcode and effects disagree");
                    valid = false;
                }
                for (const auto* registers :
                     {&instruction.defs, &instruction.uses, &instruction.clobbers}) {
                    if (!std::all_of(registers->begin(), registers->end(), allocated)) {
                        diagnostics.error(instruction.location,
                                          "AArch64 instruction keeps an unallocated register");
                        valid = false;
                        return;
                    }
                }
            });
        }
        return valid;
    }

    mir::ManagedAssembly emit_machine_assembly(machine::Module& machine_module,
                                               const mir::ManagedModule&,
                                               const hir::Module& hir_module,
                                               const Subtarget& subtarget,
                                               const CompilerOptions& options,
                                               Diagnostics& diagnostics) const override {
        return emit_assembly(machine_module, hir_module, subtarget, options, diagnostics, nullptr);
    }

    mir::ManagedAssembly emit_machine_assembly_with_debug(
        machine::Module& machine_module, const mir::ManagedModule&,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, native::DebugInfo& debug,
        Diagnostics& diagnostics) const override {
        return emit_assembly(machine_module, hir_module, subtarget, options, diagnostics, &debug);
    }
};

} // namespace

const TargetBackend& backend() {
    static const Backend result;
    return result;
}

} // namespace cross::aarch64
