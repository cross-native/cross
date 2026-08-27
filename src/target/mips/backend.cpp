// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/mips/backend.hpp"

#include "common/options.hpp"
#include "middle/machine_ir.hpp"
#include "target/backend.hpp"
#include "target/mips/features.hpp"
#include "target/mips/machine_description.hpp"
#include "target/mips/native_backend.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace cross::mips {
namespace {

unsigned type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return module.address_bits;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return static_cast<unsigned>(std::min<std::uint64_t>(
            module.record(*type.record).size * 8U,
            std::numeric_limits<unsigned>::max()));
    }
    if ((type.kind == hir::Type::Kind::Array ||
         type.kind == hir::Type::Kind::Vector) && type.element) {
        return type_bits(module, *type.element) * type.lanes;
    }
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8: return 8;
    case BuiltinType::I16:
    case BuiltinType::U16: return 16;
    case BuiltinType::I32:
    case BuiltinType::U32:
    case BuiltinType::F32: return 32;
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::F64: return 64;
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
    case BuiltinType::Fptr:
    case BuiltinType::Label: return module.address_bits;
    case BuiltinType::F80: return 80;
    case BuiltinType::I128:
    case BuiltinType::U128:
    case BuiltinType::F128: return 128;
    case BuiltinType::Void: return 0;
    }
    return 0;
}

bool unsupported_interface_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Record ||
           type.kind == hir::Type::Kind::Array ||
           type.kind == hir::Type::Kind::Vector || type_bits(module, id) > 64;
}

class Backend final : public TargetBackend {
public:
    std::string_view architecture() const override { return "mips"; }

    std::string object_writer_cpu(
        const Subtarget& subtarget) const override {
        if (subtarget.has_feature(Feature::Mips64)) return "mips64";
        if (subtarget.has_feature(Feature::Mips32r2)) return "mips32r2";
        if (subtarget.has_feature(Feature::Mips32)) return "mips32";
        if (subtarget.has_feature(Feature::Mips5)) return "mips5";
        if (subtarget.has_feature(Feature::Mips4)) return "mips4";
        if (subtarget.has_feature(Feature::Mips3)) return "mips3";
        if (subtarget.has_feature(Feature::Mips2)) return "mips2";
        return "mips1";
    }

    std::vector<std::string> object_writer_features(
        const Subtarget& subtarget) const override {
        std::vector<std::string> result;
        if (!subtarget.has_feature(Feature::AbiCalls)) {
            result.emplace_back("+noabicalls");
        }
        if (subtarget.has_feature(Feature::SoftFloat)) {
            result.emplace_back("+soft-float");
        } else if (subtarget.has_feature(Feature::Fpxx) ||
                   subtarget.has_feature(Feature::Fp32)) {
            // The managed slice uses only the FPXX-compatible subset of the
            // paired-register o32 convention. Conservative metadata keeps
            // these objects link-compatible with either 32- or 64-bit FPU
            // hardware while retaining the model's fp32 transport policy.
            result.emplace_back("+fpxx");
            result.emplace_back("+nooddspreg");
        } else if (subtarget.has_feature(Feature::Fp64)) {
            result.emplace_back("+fp64");
        }
        return result;
    }

    bool validate_hir(const hir::Module& hir_module,
                      const Subtarget& subtarget,
                      const CompilerOptions& options,
                      Diagnostics& diagnostics) const override {
        if (subtarget.object_format() != ObjectFormat::Elf) {
            diagnostics.command_error(
                "the first MIPS backend slice supports ELF object targets only");
        }
        if (options.position_independent || options.pie ||
            subtarget.has_feature(Feature::AbiCalls)) {
            diagnostics.command_error(
                "MIPS PIC/PIE and -mabicalls need a GP/GOT call model and are not implemented yet");
        }
        if (options.code_model != CodeModel::Small) {
            diagnostics.command_error(
                "MIPS currently supports only the small absolute code model");
        }
        if (subtarget.has_feature(Feature::Mips16) ||
            subtarget.has_feature(Feature::MicroMips)) {
            diagnostics.command_error(
                "MIPS16 and microMIPS encodings are registered but not lowered yet");
        }
        for (const auto& object : hir_module.objects) {
            if (object.is_thread_local) {
                diagnostics.error(
                    object.location,
                    "MIPS TLS lowering is not implemented yet; Cross will not insert a hidden resolver call");
            }
        }
        for (const auto& function : hir_module.functions) {
            if (function.ownership == hir::BodyOwnership::RawMir ||
                function.naked) {
                diagnostics.error(
                    function.location,
                    "raw/naked MIPS functions await the architecture instruction registry");
            }
            if (function.variadic || !function.variadic_bindings.empty()) {
                diagnostics.error(
                    function.location,
                    "MIPS variadic state lowering is not implemented in the first o32 slice");
            }
            if (function.result_location) {
                diagnostics.error(
                    function.location,
                    "manual MIPS result endpoints are not implemented yet; use a registered ABI");
            }
            if (unsupported_interface_type(hir_module,
                                           function.result_type)) {
                diagnostics.error(
                    function.location,
                    "the first MIPS native slice supports scalar function results through 64 bits");
            }
            for (const auto& parameter : function.parameters) {
                if (unsupported_interface_type(hir_module, parameter.type)) {
                    diagnostics.error(
                        parameter.location,
                        "the first MIPS native slice supports scalar parameters through 64 bits");
                }
                if (parameter.physical_location &&
                    *parameter.physical_location != "auto") {
                    diagnostics.error(
                        parameter.location,
                        "manual MIPS parameter endpoints are not implemented yet; use [[abi(...)]]");
                }
            }
        }
        return diagnostics.errors() == 0;
    }

    mir::RawModule lower_raw(const hir::Module&, const Subtarget&,
                             Diagnostics&) const override {
        return {};
    }

    mir::AssemblyBundle emit_raw_assembly(
        const mir::RawModule&, const mir::ManagedModule&,
        const hir::Module&, const Subtarget&, const CompilerOptions&,
        Diagnostics&) const override {
        return {};
    }

    bool prepare_managed(mir::ManagedModule&, hir::Module&,
                         const Subtarget&, const CompilerOptions&,
                         Diagnostics& diagnostics) const override {
        // MIPS registered ABIs, including Cross's internal convention, are
        // interpreted directly during frame/call lowering. No ABI name is
        // hard-coded and no LLVM convention ID is involved.
        return diagnostics.errors() == 0;
    }

    machine::Module lower_machine(
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        return lower_managed_machine(managed_module, hir_module, subtarget,
                                     options, diagnostics);
    }

    bool verify_machine(const machine::Module& module, const Subtarget&,
                        const CompilerOptions&,
                        Diagnostics& diagnostics) const override {
        bool valid = true;
        for (const auto& function : module.functions) {
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    if (instruction.kind == machine::InstructionKind::Target &&
                        !describe_opcode(instruction.opcode)) {
                        diagnostics.error(
                            instruction.location,
                            "MIPS Machine IR contains unknown target opcode " +
                                std::to_string(instruction.opcode.value));
                        valid = false;
                    }
                }
            }
        }
        return valid;
    }

    std::string emit_machine_assembly(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options,
        Diagnostics& diagnostics) const override {
        return emit_managed_machine_assembly(
            machine_module, managed_module, hir_module, subtarget, options,
            diagnostics);
    }
};

} // namespace

const TargetBackend& backend() {
    static const Backend result;
    return result;
}

} // namespace cross::mips
