// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/mips/backend.hpp"

#include "common/options.hpp"
#include "middle/machine_ir.hpp"
#include "model/model.hpp"
#include "target/backend.hpp"
#include "target/mips/features.hpp"
#include "target/mips/machine_description.hpp"
#include "target/mips/native_backend.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

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

// Whether a manual location (`reg`, `*reg`, or `A=>B`) names the register
// `name` names or one sharing its storage.
bool names_register(const TargetInfo& target, std::string_view location,
                    std::string_view name) {
    const auto* reserved = find_register(target, name);
    for (std::size_t begin = 0; begin <= location.size();) {
        const auto end = std::min(location.find("=>", begin), location.size());
        auto part = location.substr(begin, end - begin);
        begin = end + 2;
        if (part.starts_with('*')) part.remove_prefix(1);
        const auto* entry = find_register(target, part);
        if (part == name ||
            (entry && reserved && entry->storage == reserved->storage)) {
            return true;
        }
    }
    return false;
}

// A selected data base reserves its register: an ABI that carries
// arguments or results in it and a manual location that names it are
// errors. Clobber lists cannot name it, because it is compiler-owned.
void check_data_base_registers(const hir::Module& module,
                               const Subtarget& subtarget,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics) {
    const auto& target = subtarget.target();
    for (const auto& base : options.data_bases) {
        const auto reserved = [&](SourceLocation location,
                                  std::string_view what) {
            diagnostics.error(location,
                              std::string(what) + " names '" + base.base +
                                  "', the base register of data_base '" +
                                  base.name + "'");
        };
        const auto check_abi = [&](SourceLocation location, AbiId id) {
            const auto* abi = find_abi(target, id);
            if (abi && abi_carries_register(target, *abi, base.base)) {
                diagnostics.error(
                    location, "ABI '" + abi->canonical_name +
                                  "' carries arguments or results in '" +
                                  base.base + "', the base register of "
                                  "data_base '" + base.name + "'");
            }
        };
        for (const auto& function : module.functions) {
            check_abi(function.location, function.abi);
            if (function.result_location &&
                names_register(target, *function.result_location, base.base)) {
                reserved(function.location, "the result location");
            }
            for (const auto& parameter : function.parameters) {
                if (parameter.physical_location &&
                    names_register(target, *parameter.physical_location,
                                   base.base)) {
                    reserved(parameter.location, "the parameter location");
                }
            }
        }
        // The private convention of the compilation, chosen as
        // managed_abi_model chooses it.
        if (!options.private_abi) continue;
        const unsigned carrier_bits =
            subtarget.has_feature(Feature::Mips3) ? 64U : 32U;
        for (const auto& abi : model_registry().abis()) {
            if (abi.architecture == target.architecture &&
                abi.address_bits == subtarget.abi_info().address_bits &&
                abi.private_carrier_bits == carrier_bits &&
                abi_carries_register(target, abi, base.base)) {
                diagnostics.command_error(
                    "the private ABI '" + abi.canonical_name +
                    "' carries arguments or results in '" + base.base +
                    "', the base register of data_base '" + base.name +
                    "'; use -fno-private-abi");
            }
        }
    }
}

bool unsupported_interface_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record) {
        return !type.record || !module.record(*type.record).complete ||
               module.record(*type.record).size > 8191U;
    }
    return type.kind == hir::Type::Kind::Array ||
           type.kind == hir::Type::Kind::Vector || type_bits(module, id) > 64;
}

class Backend final : public TargetBackend {
public:
    std::string_view architecture() const override { return "mips"; }

    bool validate_hir(const hir::Module& hir_module,
                      const Subtarget& subtarget,
                      const CompilerOptions& options,
                      Diagnostics& diagnostics) const override {
        if (subtarget.object_format() != ObjectFormat::Elf) {
            diagnostics.command_error(
                "the first MIPS backend slice supports ELF object targets only");
        }
        // The ABI's address model and the triple's ELF class must agree: the
        // object class follows the address width, which a mips64 triple
        // names, so a mismatch would mislabel every object.
        const bool wide_triple = options.target.starts_with("mips64");
        const bool wide_abi = subtarget.abi_info().address_bits > 32;
        if (wide_abi && !wide_triple) {
            diagnostics.command_error(
                "the MIPS '" + std::string(subtarget.abi()) +
                "' ABI has 64-bit addresses and needs a mips64/mips64el "
                "target triple");
        }
        if (!wide_abi && wide_triple) {
            diagnostics.command_error(
                "the MIPS '" + std::string(subtarget.abi()) +
                "' ABI has 32-bit addresses and cannot be selected for a "
                "mips64 target triple; use -mabi=cross, -mabi=n64, or a "
                "mips/mipsel triple");
        }
        if (wide_abi && !subtarget.has_feature(Feature::Mips3)) {
            diagnostics.command_error(
                "the MIPS '" + std::string(subtarget.abi()) +
                "' ABI needs the 64-bit MIPS III register file; select a "
                "MIPS III or later CPU");
        }
        if (wide_abi && subtarget.has_feature(Feature::SingleFloat)) {
            diagnostics.command_error(
                "the MIPS '" + std::string(subtarget.abi()) +
                "' ABI is defined on a 64-bit FPU; -msingle-float is not a "
                "supported combination");
        }
        if (subtarget.cpu() == "allegrex" &&
            (!subtarget.has_feature(Feature::Allegrex) ||
             !subtarget.has_feature(Feature::Mips2) ||
             !subtarget.has_feature(Feature::HardFloat) ||
             !subtarget.has_feature(Feature::SingleFloat))) {
            diagnostics.command_error(
                "Allegrex requires MIPS II and its hard single-precision FPU; the CPU baseline cannot be disabled with -mno-* overrides");
        }
        if (options.position_independent || options.pie ||
            subtarget.has_feature(Feature::AbiCalls)) {
            diagnostics.command_error(
                "MIPS PIC/PIE and -mabicalls need a GP/GOT call model and are not implemented yet");
        }
        if (options.code_model == CodeModel::Large) {
            // The large model materializes complete 64-bit symbol addresses
            // and calls through a register; it has no meaning for a 32-bit
            // address ABI, whose sym32 addresses are already complete.
            if (subtarget.abi_info().address_bits <= 32) {
                diagnostics.command_error(
                    "the large code model applies only to 64-bit address MIPS ABIs such as n64");
            }
        } else if (options.code_model != CodeModel::Small) {
            diagnostics.command_error(
                "MIPS supports only the small and large absolute code models");
        }
        if (subtarget.has_feature(Feature::Mips16) ||
            subtarget.has_feature(Feature::MicroMips)) {
            diagnostics.command_error(
                "MIPS16 and microMIPS encodings are registered but not lowered yet");
        }
        if (subtarget.has_feature(Feature::Vfpu)) {
            diagnostics.command_error(
                "Allegrex VFPU is registered but awaits overlapping scalar/vector/matrix register lowering");
        }
        for (const auto& object : hir_module.objects) {
            if (object.is_thread_local) {
                diagnostics.error(
                    object.location,
                    "MIPS TLS lowering is not implemented yet; Cross will not insert a hidden resolver call");
            }
        }
        check_data_base_registers(hir_module, subtarget, options,
                                  diagnostics);
        for (const auto& function : hir_module.functions) {
            // A per-function [[abi(...)]] selects transport, never the data
            // model: a 32-bit-address ABI on a 64-bit-address module would
            // classify its pointer channels as words.
            if (const auto* entry = find_abi(subtarget.target(), function.abi);
                entry && entry->address_bits != hir_module.address_bits) {
                diagnostics.error(
                    function.location,
                    "the MIPS '" + entry->canonical_name +
                        "' ABI has a different address width than the "
                        "compilation ABI; mixed address models are not "
                        "supported");
            }
            // A naked body writes its result register itself; managed
            // functions have no manual MIPS endpoints yet.
            if (function.result_location && function.naked) {
                const auto* view = find_register(subtarget.target(),
                                                 *function.result_location);
                const auto bits = hir_module.type(function.result_type).kind ==
                                          hir::Type::Kind::Pointer
                                      ? hir_module.address_bits
                                      : type_bits(hir_module,
                                                  function.result_type);
                if (!view || std::none_of(
                                 view->instruction_scalar_modes.begin(),
                                 view->instruction_scalar_modes.end(),
                                 [&](const RegisterEntry::ScalarMode& mode) {
                                     return mode.bits == bits;
                                 })) {
                    diagnostics.error(
                        function.location,
                        "a naked MIPS result location must name one "
                        "register that carries the result type");
                }
            } else if (function.result_location) {
                diagnostics.error(
                    function.location,
                    "manual MIPS result endpoints are not implemented yet; use a registered ABI");
            }
            if (unsupported_interface_type(hir_module,
                                           function.result_type)) {
                diagnostics.error(
                    function.location,
                    "MIPS native function results require scalars through 64 "
                    "bits or complete records through 8191 bytes");
            }
            for (const auto& parameter : function.parameters) {
                if (unsupported_interface_type(hir_module, parameter.type)) {
                    diagnostics.error(
                        parameter.location,
                        "MIPS native parameters require scalars through 64 "
                        "bits or complete records through 8191 bytes");
                }
                if (parameter.physical_location &&
                    *parameter.physical_location != "auto" &&
                    !function.naked) {
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
        const mir::RawModule&, const mir::ManagedModule& managed,
        const hir::Module& hir_module, const Subtarget&,
        const CompilerOptions&, Diagnostics& diagnostics) const override {
        mir::AssemblyBundle result;
        // The shared data emitter owns sink objects through this bundle,
        // including when the patch value itself is managed Machine IR.
        std::unordered_set<std::uint32_t> collected;
        for (const auto& function : managed.functions) {
            for (const auto& value : function.values) {
                if (value.kind != mir::ValueKind::PatchValue ||
                    !value.patch_sink || !collected.insert(value.patch_id).second) continue;
                const auto bits = type_bits(hir_module, value.type);
                if (bits != 8 && bits != 16 && bits != 32 && bits != 64) {
                    diagnostics.error(value.location,
                                      "MIPS patch sink has no contiguous "
                                      "scalar field");
                    continue;
                }
                result.object_definitions.insert(
                    value.patch_sink->object.value);
                result.patch_relocations.push_back({
                    *value.patch_sink,
                    ".Lcross.patch.value." +
                        std::to_string(value.patch_id) + ".end",
                    bits / 8});
            }
        }
        return result;
    }

    bool prepare_managed(mir::ManagedModule& managed_module,
                         hir::Module& hir_module,
                         const Subtarget& subtarget,
                         const CompilerOptions& options,
                         Diagnostics& diagnostics) const override {
        for (const auto& function : managed_module.functions) {
            for (const auto& slot : function.slots) {
                for (const auto& base : options.data_bases) {
                    if (slot.physical_location &&
                        names_register(subtarget.target(),
                                       *slot.physical_location, base.base)) {
                        diagnostics.error(
                            slot.location,
                            "the register binding names '" + base.base +
                                "', the base register of data_base '" +
                                base.name + "'");
                    }
                }
            }
            for (const auto& value : function.values) {
                if (value.kind == mir::ValueKind::Call && value.callee) {
                    const auto& callee = hir_module.function(*value.callee);
                    if (callee.naked && (!callee.parameters.empty() ||
                                         callee.result_location)) {
                        diagnostics.error(
                            value.location,
                            "calls to a MIPS naked function with manual "
                            "parameters or a result are not implemented");
                    }
                }
                if (value.kind != mir::ValueKind::Call || value.callee ||
                    !value.call_signature) {
                    continue;
                }
                const auto& signature =
                    hir_module.type(*value.call_signature).function;
                for (const auto& base : options.data_bases) {
                    const auto* abi =
                        signature ? find_abi(subtarget.target(), signature->abi)
                                  : nullptr;
                    if (abi && abi_carries_register(subtarget.target(), *abi,
                                                    base.base)) {
                        diagnostics.error(
                            value.location,
                            "ABI '" + abi->canonical_name +
                                "' carries arguments or results in '" +
                                base.base + "', the base register of "
                                "data_base '" + base.name + "'");
                    }
                }
                const auto manual = [](const std::optional<std::string>& location) {
                    return location && *location != "auto";
                };
                if (signature &&
                    (manual(signature->result_location) ||
                     std::any_of(signature->parameters.begin(),
                                 signature->parameters.end(),
                                 [&](const hir::Parameter& parameter) {
                                     return manual(parameter.physical_location);
                                 }))) {
                    diagnostics.error(
                        value.location,
                        "manual MIPS function-pointer endpoints are not "
                        "implemented yet; use a registered ABI");
                }
            }
        }
        if (diagnostics.errors() != 0) return false;
        // MIPS registered ABIs, including Cross's internal convention, are
        // interpreted directly during frame/call lowering. No ABI name is
        // hard-coded and no LLVM convention ID is involved.
        //
        // MIPS III couples a 32-bit address model to 64-bit integer
        // registers. Keep source-width loop control, but carry reusable wide
        // induction views in MIR so each u64 consumer does not re-extend the
        // same value in the native backend.
        if (options.ivopts && hir_module.address_bits < 64 &&
            subtarget.has_feature(Feature::Mips3)) {
            (void)mir::promote_native_induction_views(
                managed_module, hir_module, 64);
            if (!mir::verify(managed_module, hir_module, diagnostics)) {
                return false;
            }
        }
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

    bool verify_machine(const machine::Module& module, const Subtarget& subtarget,
                        const CompilerOptions&,
                        Diagnostics& diagnostics) const override {
        bool valid = true;
        for (const auto& function : module.functions) {
            if (function.frame.program &&
                function.frame.program->stack_pointer !=
                    machine::Register::physical_register(
                        {29}, {static_cast<std::uint16_t>(subtarget.abi_info().address_bits)})) {
                diagnostics.error(function.location,
                                  "MIPS frame program must use SP in the address mode");
                valid = false;
            }
            machine::for_each_instruction(
                function, [&](const machine::Instruction& instruction) {
                    if (instruction.kind == machine::InstructionKind::Target &&
                        !describe_opcode(instruction.opcode)) {
                        diagnostics.error(
                            instruction.location,
                            "MIPS Machine IR contains unknown target opcode " +
                                std::to_string(instruction.opcode.value));
                        valid = false;
                    }
                    const auto opcode = decode_opcode(instruction.opcode);
                    const bool aggregate =
                        opcode == Opcode::AggregateLoad ||
                        opcode == Opcode::AggregateStore ||
                        opcode == Opcode::AggregateIndexedLoad;
                    const bool unaligned =
                        opcode == Opcode::UnalignedLoadSigned ||
                        opcode == Opcode::UnalignedLoadUnsigned ||
                        opcode == Opcode::UnalignedStore;
                    if (aggregate || unaligned) {
                        const bool store = opcode == Opcode::AggregateStore ||
                                           opcode == Opcode::UnalignedStore;
                        const bool has_value =
                            store ? !instruction.uses.empty()
                                  : instruction.defs.size() == 1;
                        const auto value = !has_value ? machine::Register{}
                                           : store ? instruction.uses.back()
                                                   : instruction.defs.front();
                        const bool memory_value =
                            value.kind == machine::RegisterKind::Virtual &&
                            value.id <
                                function.virtual_register_classes.size() &&
                            function.virtual_register_classes[value.id] ==
                                machine::VirtualRegisterClass::Memory;
                        if (!has_value || instruction.operands.empty() ||
                            !instruction.may_load || !instruction.may_store ||
                            (store && (!instruction.defs.empty() ||
                                       !instruction.has_side_effects)) ||
                            memory_value != aggregate || value.mode.bits == 0 ||
                            (aggregate && value.mode.bits % 8U != 0) ||
                            (unaligned &&
                             (value.mode.bits > 64 ||
                              instruction.uses.size() != (store ? 2U : 1U)))) {
                            diagnostics.error(instruction.location,
                                              "invalid MIPS memory-transfer "
                                              "instruction or value class");
                            valid = false;
                        }
                    }
                    const bool frame_opcode =
                        opcode == Opcode::FrameAdjust || opcode == Opcode::FrameCopy ||
                        opcode == Opcode::FrameSave || opcode == Opcode::FrameRestore;
                    if (frame_opcode != instruction.frame_effect.has_value()) {
                        diagnostics.error(instruction.location,
                                          "MIPS frame opcode and effects disagree");
                        valid = false;
                        return;
                    }
                    if (!frame_opcode)
                        return;
                    const auto& effect = *instruction.frame_effect;
                    const auto expected = opcode == Opcode::FrameAdjust
                                              ? machine::FrameOperation::AdjustStack
                                          : opcode == Opcode::FrameCopy
                                              ? machine::FrameOperation::CopyBase
                                          : opcode == Opcode::FrameSave
                                              ? machine::FrameOperation::Save
                                              : machine::FrameOperation::Restore;
                    const bool transfers =
                        opcode == Opcode::FrameSave || opcode == Opcode::FrameRestore;
                    if (effect.operation != expected || effect.base.id >= 32 ||
                        effect.base.mode.bits != subtarget.abi_info().address_bits ||
                        (effect.destination && effect.destination->id >= 32) ||
                        effect.update != machine::FrameUpdate::None ||
                        !instruction.clobbers.empty() || effect.stack_delta < -32768 ||
                        effect.stack_delta > 32767 ||
                        (transfers && effect.transfers.size() != 1) ||
                        std::any_of(
                            effect.transfers.begin(), effect.transfers.end(),
                            [&](const machine::FrameTransfer& transfer) {
                                return transfer.reg.id >= 64 ||
                                       transfer.offset < -32768 ||
                                       transfer.offset > 32767 ||
                                       (transfer.reg.mode.bits != 32 &&
                                        transfer.reg.mode.bits != 64) ||
                                       (transfer.reg.id < 32 &&
                                        transfer.reg.mode.bits == 64 &&
                                        !subtarget.has_feature(Feature::Mips3)) ||
                                       (transfer.reg.id >= 32 &&
                                        (!subtarget.has_feature(Feature::HardFloat) ||
                                         (transfer.reg.mode.bits == 64 &&
                                          (!subtarget.has_feature(Feature::Mips2) ||
                                           subtarget.has_feature(Feature::SingleFloat)))));
                            })) {
                        diagnostics.error(instruction.location,
                                          "illegal selected MIPS frame instruction");
                        valid = false;
                    }
                });
        }
        return valid;
    }

    mir::ManagedAssembly emit_machine_assembly(machine::Module& machine_module,
                                      const mir::ManagedModule& managed_module,
                                      const hir::Module& hir_module,
                                      const Subtarget& subtarget,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics) const override {
        return emit_managed_machine_assembly(machine_module, managed_module, hir_module,
                                             subtarget, options, diagnostics);
    }

    mir::ManagedAssembly emit_machine_assembly_with_debug(
        machine::Module& machine_module,
        const mir::ManagedModule& managed_module,
        const hir::Module& hir_module, const Subtarget& subtarget,
        const CompilerOptions& options, native::DebugInfo& debug,
        Diagnostics& diagnostics) const override {
        return emit_managed_machine_assembly(machine_module, managed_module, hir_module,
                                             subtarget, options, diagnostics, &debug);
    }
};

} // namespace

const TargetBackend& backend() {
    static const Backend result;
    return result;
}

} // namespace cross::mips
