// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/mips/native_backend.hpp"

#include "backend/native/machine_pass.hpp"
#include "backend/native/machine_transform.hpp"
#include "model/model.hpp"
#include "target/abi_lowering.hpp"
#include "target/assembly_format.hpp"
#include "target/mips/features.hpp"
#include "target/mips/machine_description.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cross::mips {
namespace {

enum class LoweringPass : std::uint16_t {
    PropagateCopies,
    EliminateRedundantExpressions,
    EliminateRedundantLoads,
    EliminateDeadValues,
    ElideUnusedSpillSlots,
};

bool is_void(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::Void;
}

bool is_floating(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return false;
    return type.builtin == BuiltinType::F32 ||
           type.builtin == BuiltinType::F64 ||
           type.builtin == BuiltinType::F80 ||
           type.builtin == BuiltinType::F128 ||
           type.builtin == BuiltinType::Fptr;
}

bool is_signed_integer(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return false;
    return type.builtin == BuiltinType::I8 ||
           type.builtin == BuiltinType::I16 ||
           type.builtin == BuiltinType::I32 ||
           type.builtin == BuiltinType::I64 ||
           type.builtin == BuiltinType::I128 ||
           type.builtin == BuiltinType::Iptr;
}

bool is_aggregate(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Record && type.record &&
           module.record(*type.record).complete;
}

bool is_vector(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Vector;
}

unsigned type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return module.address_bits;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto bytes = module.record(*type.record).size;
        return bytes <= std::numeric_limits<unsigned>::max() / 8U
                   ? static_cast<unsigned>(bytes * 8U)
                   : 0U;
    }
    if (type.kind == hir::Type::Kind::Array ||
        type.kind == hir::Type::Kind::Vector) {
        if (!type.element || type.lanes == 0 ||
            (type.kind == hir::Type::Kind::Vector && type.scalable)) {
            return 0;
        }
        const auto element = type_bits(module, *type.element);
        return element != 0 &&
                       type.lanes <= std::numeric_limits<unsigned>::max() /
                                         element
                   ? element * type.lanes
                   : 0U;
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

machine::IntegerMode mode_for(const hir::Module& module, hir::TypeId id) {
    return {static_cast<std::uint16_t>(type_bits(module, id))};
}

unsigned storage_size(const hir::Module& module, hir::TypeId id,
                      const TargetDataLayout& layout) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) {
        return (module.address_bits + 7U) / 8U;
    }
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto bytes = module.record(*type.record).size;
        return bytes <= std::numeric_limits<unsigned>::max()
                   ? static_cast<unsigned>(bytes)
                   : 0U;
    }
    if (type.kind == hir::Type::Kind::Array) {
        if (!type.element || type.lanes == 0) return 0;
        const auto element = storage_size(module, *type.element, layout);
        return element != 0 &&
                       type.lanes <= std::numeric_limits<unsigned>::max() /
                                         element
                   ? element * type.lanes
                   : 0U;
    }
    if (type.kind == hir::Type::Kind::Vector) {
        const auto bits = type_bits(module, id);
        return bits == 0 ? 0U : (bits + 7U) / 8U;
    }
    if (type.builtin == BuiltinType::F80) return layout.f80_storage_bytes;
    const auto bits = type_bits(module, id);
    return bits == 0 ? 0U : (bits + 7U) / 8U;
}

unsigned storage_alignment(const hir::Module& module, hir::TypeId id,
                           const TargetDataLayout& layout) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return module.record(*type.record).alignment;
    }
    if (type.kind == hir::Type::Kind::Array && type.element) {
        return storage_alignment(module, *type.element, layout);
    }
    if (type.kind == hir::Type::Kind::Pointer) {
        return std::min((module.address_bits + 7U) / 8U,
                        layout.natural_alignment_limit);
    }
    if (type.kind == hir::Type::Kind::Builtin &&
        type.builtin == BuiltinType::F80) {
        return layout.f80_alignment;
    }
    const auto bytes = storage_size(module, id, layout);
    const auto natural = bytes >= 16 ? 16U : bytes >= 8 ? 8U :
                         bytes >= 4 ? 4U : bytes >= 2 ? 2U : 1U;
    return std::min(natural, layout.natural_alignment_limit);
}

ScalarMode scalar_mode(const hir::Module& module, hir::TypeId id,
                       const AbiEntry& abi) {
    const auto& type = module.type(id);
    const auto bits = static_cast<std::uint16_t>(type_bits(module, id));
    if (type.kind == hir::Type::Kind::Pointer ||
        (type.kind == hir::Type::Kind::Builtin &&
         type.builtin == BuiltinType::Label)) {
        return ScalarMode::pointer(static_cast<std::uint16_t>(abi.address_bits));
    }
    if (type.kind == hir::Type::Kind::Record) {
        return ScalarMode::aggregate(bits);
    }
    if (type.kind == hir::Type::Kind::Array) return ScalarMode::array(bits);
    if (type.kind == hir::Type::Kind::Vector) return ScalarMode::vector(bits);
    if (is_floating(module, id)) return ScalarMode::floating(bits);
    return bits == 0 ? ScalarMode::zero() : ScalarMode::integer(bits);
}

AbiValue abi_value_for(const hir::Module& module, hir::TypeId id,
                       const TargetDataLayout& layout, const AbiEntry& abi,
                       ValueTransport transport = ValueTransport::Direct) {
    AbiValue result;
    result.mode = scalar_mode(module, id, abi);
    result.transport = transport;
    result.alignment_bits = static_cast<std::uint16_t>(std::min<unsigned>(
        storage_alignment(module, id, layout) * 8U,
        std::numeric_limits<std::uint16_t>::max()));
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto& record = module.record(*type.record);
        result.elements.reserve(record.members.size());
        result.element_offsets_bits.reserve(record.members.size());
        for (const auto& member : record.members) {
            result.elements.push_back(
                abi_value_for(module, member.type, layout, abi));
            result.element_offsets_bits.push_back(
                static_cast<std::uint32_t>(member.offset * 8U));
        }
    } else if (type.kind == hir::Type::Kind::Array && type.element) {
        result.element_count = type.lanes;
        result.elements.push_back(
            abi_value_for(module, *type.element, layout, abi));
    }
    return result;
}

const AbiEntry* abi_model(AbiId id) {
    const auto* abi = model_registry().find_abi(id);
    return abi && abi->architecture == "mips" ? abi : nullptr;
}

machine::Operand register_operand(machine::Register value) {
    return machine::RegisterOperand{value};
}

machine::Operand immediate_operand(std::uint64_t low, std::uint64_t high,
                                   machine::IntegerMode mode,
                                   bool is_signed = false) {
    return machine::ImmediateOperand{low, high, mode, is_signed};
}

machine::Operand stack_operand(machine::StackSlotId slot,
                               machine::IntegerMode mode) {
    return machine::StackSlotOperand{slot, 0, mode};
}

machine::Operand block_operand(machine::BlockId block) {
    return machine::BlockOperand{block};
}

machine::TargetOpcodeId integer_binary_opcode(
    mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::Add: return Opcode::Add;
    case BinaryOperation::Subtract: return Opcode::Sub;
    case BinaryOperation::Multiply: return Opcode::Mul;
    case BinaryOperation::SignedDivide: return Opcode::Sdiv;
    case BinaryOperation::UnsignedDivide: return Opcode::Udiv;
    case BinaryOperation::SignedRemainder: return Opcode::Srem;
    case BinaryOperation::UnsignedRemainder: return Opcode::Urem;
    case BinaryOperation::BitAnd: return Opcode::And;
    case BinaryOperation::BitOr: return Opcode::Or;
    case BinaryOperation::BitXor: return Opcode::Xor;
    case BinaryOperation::ShiftLeft: return Opcode::Shl;
    case BinaryOperation::ShiftRightArithmetic: return Opcode::ShrS;
    case BinaryOperation::ShiftRightLogical: return Opcode::ShrU;
    case BinaryOperation::RotateLeft: return Opcode::Rotl;
    case BinaryOperation::RotateRight: return Opcode::Rotr;
    case BinaryOperation::Equal: return Opcode::CmpEq;
    case BinaryOperation::NotEqual: return Opcode::CmpNe;
    case BinaryOperation::SignedLess: return Opcode::CmpSlt;
    case BinaryOperation::SignedLessEqual: return Opcode::CmpSle;
    case BinaryOperation::SignedGreater: return Opcode::CmpSgt;
    case BinaryOperation::SignedGreaterEqual: return Opcode::CmpSge;
    case BinaryOperation::UnsignedLess: return Opcode::CmpUlt;
    case BinaryOperation::UnsignedLessEqual: return Opcode::CmpUle;
    case BinaryOperation::UnsignedGreater: return Opcode::CmpUgt;
    case BinaryOperation::UnsignedGreaterEqual: return Opcode::CmpUge;
    }
    return Opcode::Invalid;
}

machine::TargetOpcodeId floating_binary_opcode(
    mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::Add: return Opcode::Fadd;
    case BinaryOperation::Subtract: return Opcode::Fsub;
    case BinaryOperation::Multiply: return Opcode::Fmul;
    case BinaryOperation::SignedDivide: return Opcode::Fdiv;
    case BinaryOperation::Equal: return Opcode::FcmpEq;
    case BinaryOperation::NotEqual: return Opcode::FcmpNe;
    case BinaryOperation::SignedLess: return Opcode::FcmpLt;
    case BinaryOperation::SignedLessEqual: return Opcode::FcmpLe;
    case BinaryOperation::SignedGreater: return Opcode::FcmpGt;
    case BinaryOperation::SignedGreaterEqual: return Opcode::FcmpGe;
    default: return Opcode::Invalid;
    }
}

machine::TargetOpcodeId cast_opcode(mir::CastOperation operation,
                                    bool representation_float) {
    using mir::CastOperation;
    switch (operation) {
    case CastOperation::SignExtend: return Opcode::Sext;
    case CastOperation::ZeroExtend: return Opcode::Zext;
    case CastOperation::Truncate: return Opcode::Trunc;
    case CastOperation::Reinterpret:
        return representation_float ? Opcode::Freinterpret
                                    : Opcode::Reinterpret;
    case CastOperation::FloatExtend: return Opcode::Fextend;
    case CastOperation::FloatTruncate: return Opcode::Ftruncate;
    case CastOperation::SignedIntegerToFloat: return Opcode::Sitofp;
    case CastOperation::UnsignedIntegerToFloat: return Opcode::Uitofp;
    case CastOperation::FloatToSignedInteger: return Opcode::Fptosi;
    case CastOperation::FloatToUnsignedInteger: return Opcode::Fptoui;
    }
    return Opcode::Invalid;
}

machine::TargetOpcodeId atomic_opcode(mir::AtomicOperation operation) {
    using mir::AtomicOperation;
    switch (operation) {
    case AtomicOperation::Load: return Opcode::AtomicLoad;
    case AtomicOperation::Store: return Opcode::AtomicStore;
    case AtomicOperation::Exchange: return Opcode::AtomicExchange;
    case AtomicOperation::CompareExchange: return Opcode::AtomicCompareExchange;
    case AtomicOperation::FetchAdd: return Opcode::AtomicFetchAdd;
    case AtomicOperation::FetchSub: return Opcode::AtomicFetchSub;
    case AtomicOperation::FetchAnd: return Opcode::AtomicFetchAnd;
    case AtomicOperation::FetchXor: return Opcode::AtomicFetchXor;
    case AtomicOperation::FetchOr: return Opcode::AtomicFetchOr;
    case AtomicOperation::FetchUpdate: return Opcode::AtomicFetchUpdate;
    case AtomicOperation::ThreadFence: return Opcode::AtomicThreadFence;
    case AtomicOperation::SignalFence: return Opcode::AtomicSignalFence;
    }
    return Opcode::Invalid;
}

class MachineLowerer {
public:
    MachineLowerer(const mir::ManagedModule& managed,
                   const hir::Module& hir_module,
                   const Subtarget& subtarget,
                   const CompilerOptions& options,
                   Diagnostics& diagnostics)
        : managed_(managed), hir_(hir_module), subtarget_(subtarget),
          options_(options), diagnostics_(diagnostics) {}

    machine::Module run() {
        for (const auto& function : managed_.functions) {
            lower_function(function);
        }
        return std::move(result_);
    }

private:
    bool has_result(const mir::ManagedValue& value) const {
        if (is_void(hir_, value.type)) return false;
        return value.kind != mir::ValueKind::LifetimeStart &&
               value.kind != mir::ValueKind::LifetimeEnd &&
               value.kind != mir::ValueKind::Store &&
               value.kind != mir::ValueKind::PointerStore &&
               value.kind != mir::ValueKind::GlobalStore;
    }

    machine::Register reg(mir::ValueId value) const {
        if (value.value >= value_registers_.size() ||
            !value_registers_[value.value]) {
            diagnostics_.error(
                source_ && value.value < source_->values.size()
                    ? source_->values[value.value].location
                    : current_.location,
                "MIPS selection encountered a value without a machine result");
            return machine::Register::physical_register({2}, machine::i32);
        }
        const auto id = *value_registers_[value.value];
        return machine::Register::virtual_register(
            id, current_.virtual_registers[id.value]);
    }

    machine::Instruction target_instruction(
        machine::TargetOpcodeId opcode, SourceLocation location) const {
        machine::Instruction result;
        result.kind = machine::InstructionKind::Target;
        result.opcode = opcode;
        result.location = location;
        return result;
    }

    void unsupported(const mir::ManagedValue& value,
                     std::string_view feature) {
        diagnostics_.error(value.location,
                           "MIPS native lowering does not yet support " +
                               std::string(feature));
    }

    void create_registers(const mir::ManagedFunction& source) {
        value_registers_.assign(source.values.size(), std::nullopt);
        for (const auto& value : source.values) {
            if (!has_result(value)) continue;
            const auto mode = mode_for(hir_, value.type);
            if (!mode.valid()) {
                diagnostics_.error(value.location,
                                   "managed value has no fixed MIPS machine mode");
                continue;
            }
            // MIPS I/II legalize 64-bit integers to a pair of 32-bit GPR
            // words in the assembly emitter.  Keeping the SSA value intact
            // here lets target-independent MIR optimizations reason about an
            // ordinary i64/u64 value without inventing a public pair type.
            if (is_floating(hir_, value.type) &&
                !subtarget_.has_feature(Feature::HardFloat)) {
                diagnostics_.error(
                    value.location,
                    "-msoft-float forbids FPU instructions; standalone Cross will not insert a software-float runtime call");
            }
            if (is_floating(hir_, value.type) && mode.bits > 32 &&
                subtarget_.has_feature(Feature::SingleFloat)) {
                diagnostics_.error(
                    value.location,
                    "the selected MIPS CPU has a single-precision-only FPU");
            }
            if (mode.bits > 64 || is_vector(hir_, value.type) ||
                is_aggregate(hir_, value.type)) {
                unsupported(value, mode.bits > 64
                                       ? "scalar values wider than 64 bits"
                                       : "aggregate or vector SSA values");
            }
            const machine::VirtualRegisterId id{
                static_cast<std::uint32_t>(current_.virtual_registers.size())};
            current_.virtual_registers.push_back(mode);
            current_.virtual_register_assignments.push_back(std::nullopt);
            current_.rematerialized_immediates.push_back(std::nullopt);
            current_.virtual_register_classes.push_back(
                is_floating(hir_, value.type)
                    ? machine::VirtualRegisterClass::Floating
                    : machine::VirtualRegisterClass::Integer);
            value_registers_[value.id.value] = id;
        }
    }

    void create_stack_slots(const mir::ManagedFunction& source) {
        const auto& layout = subtarget_.target().data_layout;
        for (const auto& slot : source.slots) {
            machine::StackSlot target;
            target.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            target.kind = machine::StackSlotKind::Local;
            target.size = storage_size(hir_, slot.type, layout);
            target.alignment = storage_alignment(hir_, slot.type, layout);
            target.location = slot.location;
            target.name = slot.name;
            if (target.size == 0) {
                diagnostics_.error(
                    slot.location,
                    "managed stack object has no fixed MIPS storage size");
                target.size = 1;
            }
            if (slot.physical_location && *slot.physical_location != "auto") {
                diagnostics_.error(
                    slot.location,
                    "MIPS managed hard-register locals are not implemented yet");
            }
            current_.stack_slots.push_back(std::move(target));
        }

        const auto& entity = hir_.function(source.source);
        for (std::size_t index = 0; index < entity.parameters.size(); ++index) {
            if (entity.parameters[index].mode == ParameterMode::In) continue;
            machine::StackSlot pointer;
            pointer.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            pointer.kind = machine::StackSlotKind::Local;
            pointer.size = (hir_.address_bits + 7U) / 8U;
            pointer.alignment = pointer.size;
            pointer.location = entity.parameters[index].location;
            pointer.name = "$paramptr." + std::to_string(index);
            current_.stack_slots.push_back(std::move(pointer));
        }

        for (std::size_t index = 0;
             index < current_.virtual_registers.size(); ++index) {
            const auto mode = current_.virtual_registers[index];
            const bool floating =
                current_.virtual_register_classes[index] ==
                machine::VirtualRegisterClass::Floating;
            machine::StackSlot spill;
            spill.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            spill.kind = machine::StackSlotKind::Spill;
            spill.size = floating ? std::max(4U, (mode.bits + 7U) / 8U)
                                  : mode.bits > 32 ? 8U : 4U;
            spill.alignment = spill.size >= 8 ? 8U : 4U;
            spill.location = source.location;
            spill.name = "$v" + std::to_string(index);
            spill.spill_for = machine::VirtualRegisterId{
                static_cast<std::uint32_t>(index)};
            current_.stack_slots.push_back(std::move(spill));
        }
    }

    Opcode load_opcode(hir::TypeId type, Opcode signed_opcode,
                       Opcode unsigned_opcode, Opcode floating_opcode) const {
        return is_floating(hir_, type)
                   ? floating_opcode
                   : is_signed_integer(hir_, type) ? signed_opcode
                                                   : unsigned_opcode;
    }

    machine::Instruction lower_value(const mir::ManagedValue& value) {
        using mir::ValueKind;
        if (value.kind == ValueKind::Parameter) {
            auto result = target_instruction(
                is_floating(hir_, value.type) ? Opcode::Fparameter
                                              : Opcode::Parameter,
                value.location);
            result.operands.push_back(immediate_operand(
                value.parameter_index, 0, machine::i32));
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::ConstantInteger ||
            value.kind == ValueKind::ConstantFloating) {
            auto result = target_instruction(
                value.kind == ValueKind::ConstantFloating
                    ? Opcode::Fconstant
                    : Opcode::Constant,
                value.location);
            result.operands.push_back(immediate_operand(
                value.integer, value.integer_high, reg(value.id).mode,
                is_signed_integer(hir_, value.type)));
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::LabelAddress) {
            auto result = target_instruction(Opcode::LabelAddress,
                                             value.location);
            const auto found = std::find_if(
                source_->labels.begin(), source_->labels.end(),
                [&](const mir::ManagedLabel& label) {
                    return value.label && label.label == *value.label;
                });
            if (found == source_->labels.end()) {
                diagnostics_.error(value.location,
                                   "MIPS label address has no target block");
            } else {
                result.operands.push_back(
                    block_operand({found->block.value}));
            }
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::SlotAddress) {
            auto result = target_instruction(Opcode::StackAddress,
                                             value.location);
            result.operands.push_back(
                stack_operand({value.slot->value},
                              {static_cast<std::uint16_t>(hir_.address_bits)}));
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::GlobalAddress) {
            auto result = target_instruction(Opcode::GlobalAddress,
                                             value.location);
            result.operands.push_back(machine::SymbolOperand{
                hir_.object(*value.object).link_symbol, 0, false,
                value.object});
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::IndexedAddress) {
            auto result = target_instruction(Opcode::IndexedAddress,
                                             value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            const auto& pointer = hir_.type(value.type);
            const auto element = pointer.pointee
                ? storage_size(hir_, *pointer.pointee,
                               subtarget_.target().data_layout)
                : 0U;
            result.operands.push_back(
                immediate_operand(element, 0, machine::i32));
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::VariadicState) {
            auto result = target_instruction(Opcode::VariadicState,
                                             value.location);
            result.variadic_state = value.variadic_state;
            result.defs.push_back(reg(value.id));
            unsupported(value, "variadic state materialization");
            return result;
        }
        if (value.kind == ValueKind::LifetimeStart ||
            value.kind == ValueKind::LifetimeEnd) {
            auto result = target_instruction(
                value.kind == ValueKind::LifetimeStart
                    ? Opcode::LifetimeStart
                    : Opcode::LifetimeEnd,
                value.location);
            if (value.slot) {
                result.operands.push_back(
                    stack_operand({value.slot->value}, machine::i32));
            }
            return result;
        }
        if (value.kind == ValueKind::DynamicStackSave ||
            value.kind == ValueKind::DynamicAlloca ||
            value.kind == ValueKind::DynamicStackRestore) {
            const auto opcode = value.kind == ValueKind::DynamicStackSave
                ? Opcode::StackSave
                : value.kind == ValueKind::DynamicAlloca
                      ? Opcode::StackAllocate
                      : Opcode::StackRestore;
            auto result = target_instruction(opcode, value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            if (has_result(value)) result.defs.push_back(reg(value.id));
            unsupported(value, "dynamic stack allocation");
            return result;
        }
        if (value.kind == ValueKind::Load) {
            const auto opcode = load_opcode(
                value.type, Opcode::LoadSigned, Opcode::LoadUnsigned,
                Opcode::Fload);
            auto result = target_instruction(opcode, value.location);
            result.operands.push_back(
                stack_operand({value.slot->value}, reg(value.id).mode));
            result.defs.push_back(reg(value.id));
            result.may_load = true;
            result.has_side_effects = value.is_volatile_access;
            return result;
        }
        if (value.kind == ValueKind::Store) {
            const auto source = reg(value.operands.front());
            auto result = target_instruction(
                is_floating(hir_, source_->values[value.operands.front().value].type)
                    ? Opcode::Fstore
                    : Opcode::Store,
                value.location);
            result.operands.push_back(
                stack_operand({value.slot->value}, source.mode));
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
            result.may_store = true;
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::PointerLoad) {
            const auto opcode = load_opcode(
                value.type, Opcode::PointerLoadSigned,
                Opcode::PointerLoadUnsigned, Opcode::FpointerLoad);
            auto result = target_instruction(opcode, value.location);
            const auto address = reg(value.operands.front());
            result.operands.push_back(register_operand(address));
            result.uses.push_back(address);
            result.defs.push_back(reg(value.id));
            result.may_load = true;
            result.has_side_effects = value.is_volatile_access;
            return result;
        }
        if (value.kind == ValueKind::PointerStore) {
            const auto address = reg(value.operands[0]);
            const auto source = reg(value.operands[1]);
            const auto source_type = source_->values[value.operands[1].value].type;
            auto result = target_instruction(
                is_floating(hir_, source_type) ? Opcode::FpointerStore
                                               : Opcode::PointerStore,
                value.location);
            result.operands.push_back(register_operand(address));
            result.operands.push_back(register_operand(source));
            result.uses = {address, source};
            result.may_store = true;
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::IndexedLoad) {
            const auto opcode = load_opcode(
                value.type, Opcode::IndexedLoadSigned,
                Opcode::IndexedLoadUnsigned, Opcode::FindexedLoad);
            auto result = target_instruction(opcode, value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            result.operands.push_back(immediate_operand(
                storage_size(hir_, value.type,
                             subtarget_.target().data_layout),
                0, machine::i32));
            result.defs.push_back(reg(value.id));
            result.may_load = true;
            result.has_side_effects = value.is_volatile_access;
            return result;
        }
        if (value.kind == ValueKind::GlobalLoad) {
            const auto opcode = load_opcode(
                value.type, Opcode::GlobalLoadSigned,
                Opcode::GlobalLoadUnsigned, Opcode::FglobalLoad);
            auto result = target_instruction(opcode, value.location);
            result.operands.push_back(machine::SymbolOperand{
                hir_.object(*value.object).link_symbol, 0, false,
                value.object});
            result.defs.push_back(reg(value.id));
            result.may_load = true;
            result.has_side_effects = value.is_volatile_access;
            return result;
        }
        if (value.kind == ValueKind::GlobalStore) {
            const auto source = reg(value.operands.front());
            const auto source_type = source_->values[value.operands.front().value].type;
            auto result = target_instruction(
                is_floating(hir_, source_type) ? Opcode::FglobalStore
                                               : Opcode::GlobalStore,
                value.location);
            result.operands.push_back(machine::SymbolOperand{
                hir_.object(*value.object).link_symbol, 0, false,
                value.object});
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
            result.may_store = true;
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::Atomic) {
            auto result = target_instruction(atomic_opcode(value.atomic),
                                             value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            const auto flags =
                static_cast<std::uint64_t>(value.memory_order) |
                (static_cast<std::uint64_t>(value.failure_order) << 8U) |
                (static_cast<std::uint64_t>(value.binary) << 16U);
            unsigned bits{};
            if (!value.operands.empty()) {
                const auto& pointer = hir_.type(
                    source_->values[value.operands.front().value].type);
                if (pointer.kind == hir::Type::Kind::Pointer && pointer.pointee) {
                    bits = type_bits(hir_, *pointer.pointee);
                }
            }
            result.operands.push_back(
                immediate_operand(bits, flags, machine::i128));
            if (has_result(value)) result.defs.push_back(reg(value.id));
            result.may_load = value.atomic != mir::AtomicOperation::Store &&
                              value.atomic != mir::AtomicOperation::SignalFence;
            result.may_store = value.atomic != mir::AtomicOperation::Load &&
                               value.atomic != mir::AtomicOperation::SignalFence;
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::Call) {
            machine::Instruction result;
            result.kind = machine::InstructionKind::Call;
            result.location = value.location;
            const auto& callee = hir_.function(*value.callee);
            result.direct_callee = callee.id;
            result.operands.push_back(machine::SymbolOperand{
                callee.link_symbol, 0, true, std::nullopt});
            for (const auto& argument : value.call_arguments) {
                result.call_argument_types.push_back(argument.type);
                if (argument.value) {
                    const auto source = reg(*argument.value);
                    result.operands.push_back(register_operand(source));
                    result.uses.push_back(source);
                } else {
                    result.operands.push_back(stack_operand(
                        {argument.cell->value},
                        {static_cast<std::uint16_t>(hir_.address_bits)}));
                }
            }
            if (has_result(value)) result.defs.push_back(reg(value.id));
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::PatchValue) {
            auto result = target_instruction(Opcode::Patch, value.location);
            const auto target = reg(value.id);
            result.operands.push_back(immediate_operand(
                value.integer, value.integer_high, target.mode));
            result.defs.push_back(target);
            result.patch = machine::PatchSite{
                value.patch_id, target.mode.bits, value.patch_sink};
            result.has_side_effects = true;
            return result;
        }
        if (value.kind == ValueKind::Intrinsic) {
            auto result = target_instruction(
                value.intrinsic == mir::IntrinsicOperation::Expect
                    ? Opcode::Expect
                    : Opcode::IntrinsicNoop,
                value.location);
            if (value.intrinsic == mir::IntrinsicOperation::Expect) {
                const auto source = reg(value.operands.front());
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
                result.defs.push_back(reg(value.id));
            } else {
                result.has_side_effects = true;
            }
            return result;
        }
        if (value.kind == ValueKind::Unary) {
            const auto source = reg(value.operands.front());
            const auto floating = is_floating(
                hir_, source_->values[value.operands.front().value].type);
            const auto opcode = floating
                ? value.unary == mir::UnaryOperation::Negate
                      ? Opcode::Fneg
                      : Opcode::Fiszero
                : value.unary == mir::UnaryOperation::Negate
                      ? Opcode::Neg
                      : value.unary == mir::UnaryOperation::BitNot
                            ? Opcode::Not
                            : Opcode::Iszero;
            auto result = target_instruction(opcode, value.location);
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::Cast) {
            const auto source = reg(value.operands.front());
            const auto source_type = source_->values[value.operands.front().value].type;
            const bool representation_float =
                value.cast == mir::CastOperation::Reinterpret &&
                (is_floating(hir_, source_type) ||
                 is_floating(hir_, value.type));
            auto result = target_instruction(
                cast_opcode(value.cast, representation_float), value.location);
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::Select) {
            auto result = target_instruction(Opcode::Select, value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::Phi) {
            auto result = target_instruction(Opcode::Phi, value.location);
            for (const auto& incoming : value.incoming) {
                const auto source = reg(incoming.value);
                result.operands.push_back(
                    block_operand({incoming.predecessor.value}));
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            result.defs.push_back(reg(value.id));
            return result;
        }
        if (value.kind == ValueKind::Splat ||
            value.kind == ValueKind::ExtractElement ||
            value.kind == ValueKind::InsertElement) {
            unsupported(value, "vector operations");
            auto result = target_instruction(Opcode::Invalid, value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            if (has_result(value)) result.defs.push_back(reg(value.id));
            return result;
        }

        // The only remaining result-producing MIR form is a scalar binary.
        const auto source_type = source_->values[value.operands.front().value].type;
        auto result = target_instruction(
            is_floating(hir_, source_type)
                ? floating_binary_opcode(value.binary)
                : integer_binary_opcode(value.binary),
            value.location);
        for (const auto operand : value.operands) {
            const auto source = reg(operand);
            result.operands.push_back(register_operand(source));
            result.uses.push_back(source);
        }
        result.defs.push_back(reg(value.id));
        if (result.opcode == Opcode::Invalid) {
            unsupported(value, "the selected scalar operation");
        }
        return result;
    }

    machine::Instruction lower_terminator(
        const mir::ManagedTerminator& terminator) {
        machine::Instruction result;
        result.location = terminator.location;
        switch (terminator.kind) {
        case mir::TerminatorKind::Return:
            result.kind = machine::InstructionKind::Return;
            if (terminator.value) {
                const auto source = reg(*terminator.value);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            break;
        case mir::TerminatorKind::Branch:
            result.kind = machine::InstructionKind::Branch;
            result.operands.push_back(
                block_operand({terminator.successors.front().value}));
            break;
        case mir::TerminatorKind::ConditionalBranch: {
            result.kind = machine::InstructionKind::ConditionalBranch;
            const auto condition = reg(*terminator.value);
            result.operands.push_back(register_operand(condition));
            result.uses.push_back(condition);
            for (const auto successor : terminator.successors) {
                result.operands.push_back(
                    block_operand({successor.value}));
            }
            break;
        }
        case mir::TerminatorKind::IndirectBranch: {
            result.kind = machine::InstructionKind::IndirectBranch;
            const auto destination = reg(*terminator.value);
            result.operands.push_back(register_operand(destination));
            result.uses.push_back(destination);
            for (const auto successor : terminator.successors) {
                result.operands.push_back(
                    block_operand({successor.value}));
            }
            break;
        }
        case mir::TerminatorKind::Unreachable:
            result.kind = machine::InstructionKind::Unreachable;
            result.has_side_effects = true;
            break;
        case mir::TerminatorKind::Trap:
            result.kind = machine::InstructionKind::Trap;
            result.has_side_effects = true;
            break;
        case mir::TerminatorKind::None:
            result.kind = machine::InstructionKind::Unreachable;
            diagnostics_.error(terminator.location,
                               "managed MIR block has no terminator during MIPS selection");
            break;
        }
        return result;
    }

    void lower_blocks(const mir::ManagedFunction& source) {
        for (const auto& block : source.blocks) {
            machine::Block target;
            target.id = {block.id.value};
            target.location = block.location;
            target.label = "mir.bb." + std::to_string(block.id.value);
            target.reachable = true;
            for (const auto predecessor : block.predecessors) {
                target.predecessors.push_back({predecessor.value});
            }
            for (const auto successor : block.terminator.successors) {
                target.successors.push_back({successor.value});
            }
            for (const auto value : block.values) {
                target.instructions.push_back(
                    lower_value(source.values[value.value]));
            }
            target.instructions.push_back(lower_terminator(block.terminator));
            current_.blocks.push_back(std::move(target));
            current_.layout.push_back({block.id.value});
        }
    }

    void optimize_machine_function() {
        native::MachineFunctionPassManager passes;
        using Stage = native::MachineStage;
        passes.add(
            {{LoweringPass::PropagateCopies}, Stage::Canonicalization,
             "propagate-copies"},
            [this](machine::Function& function) {
                if (!options_.cprop_registers) return false;
                const auto is_copy = [](const machine::Instruction& value) {
                    const auto opcode = decode_opcode(value.opcode);
                    return opcode == Opcode::Expect ||
                           opcode == Opcode::Reinterpret ||
                           opcode == Opcode::Freinterpret;
                };
                return native::propagate_virtual_register_copies(
                    function, is_copy);
            });
        passes.add(
            {{LoweringPass::EliminateRedundantExpressions},
             Stage::Canonicalization, "eliminate-redundant-expressions"},
            [this](machine::Function& function) {
                if (!options_.machine_cse) return false;
                const auto eligible = [](const machine::Instruction& value) {
                    switch (decode_opcode(value.opcode)) {
                    case Opcode::None:
                    case Opcode::Parameter:
                    case Opcode::Fparameter:
                    case Opcode::Phi:
                    case Opcode::Patch:
                    case Opcode::IntrinsicNoop:
                    case Opcode::LifetimeStart:
                    case Opcode::LifetimeEnd:
                    case Opcode::AtomicLoad:
                    case Opcode::AtomicStore:
                    case Opcode::AtomicExchange:
                    case Opcode::AtomicCompareExchange:
                    case Opcode::AtomicFetchAdd:
                    case Opcode::AtomicFetchSub:
                    case Opcode::AtomicFetchAnd:
                    case Opcode::AtomicFetchXor:
                    case Opcode::AtomicFetchOr:
                    case Opcode::AtomicFetchUpdate:
                    case Opcode::AtomicThreadFence:
                    case Opcode::AtomicSignalFence:
                    case Opcode::VariadicState:
                    case Opcode::StackSave:
                    case Opcode::StackAllocate:
                    case Opcode::StackRestore:
                    case Opcode::Invalid: return false;
                    default: return true;
                    }
                };
                return native::eliminate_redundant_expressions(
                    function, eligible);
            });
        passes.add(
            {{LoweringPass::EliminateRedundantLoads},
             Stage::Canonicalization, "eliminate-redundant-loads"},
            [this](machine::Function& function) {
                if (!options_.machine_load_cse) return false;
                return native::eliminate_redundant_loads(
                    function, [](const machine::Instruction&) {
                        return true;
                    });
            });
        passes.add(
            {{LoweringPass::EliminateDeadValues}, Stage::Canonicalization,
             "eliminate-dead-machine-values"},
            [this](machine::Function& function) {
                if (!options_.machine_dce) return false;
                std::vector<bool> implicit_parameter_storage(
                    function.virtual_registers.size());
                for (const auto& block : function.blocks) {
                    for (const auto& value : block.instructions) {
                        const auto opcode = decode_opcode(value.opcode);
                        if (opcode != Opcode::Parameter &&
                            opcode != Opcode::Fparameter) {
                            continue;
                        }
                        for (const auto& definition : value.defs) {
                            if (definition.kind ==
                                    machine::RegisterKind::Virtual &&
                                definition.id <
                                    implicit_parameter_storage.size()) {
                                implicit_parameter_storage[definition.id] =
                                    true;
                            }
                        }
                    }
                }
                return native::eliminate_dead_definitions(
                    function,
                    [&](const machine::Register& definition) {
                        return definition.kind ==
                                   machine::RegisterKind::Virtual &&
                               definition.id <
                                   implicit_parameter_storage.size() &&
                               implicit_parameter_storage[definition.id];
                    });
            });
        passes.add(
            {{LoweringPass::ElideUnusedSpillSlots},
             Stage::FrameFinalization, "elide-unused-spill-slots"},
            [](machine::Function& function) {
                return native::elide_unused_virtual_spill_slots(function);
            });
        (void)passes.run(current_);
    }

    void lower_function(const mir::ManagedFunction& source) {
        current_ = {};
        source_ = &source;
        const auto& entity = hir_.function(source.source);
        current_.source = source.source;
        current_.source_entry = source.entry;
        current_.location = source.location;
        current_.symbol = entity.link_symbol;
        current_.abi = entity.abi;
        current_.entry = {source.entry.value};
        current_.frame.stack_alignment =
            std::max(1U, subtarget_.abi_info().stack_alignment);
        current_.frame.outgoing_argument_alignment =
            current_.frame.stack_alignment;
        current_.frame.has_frame_pointer = true;
        create_registers(source);
        create_stack_slots(source);
        lower_blocks(source);
        for (const auto& label : source.labels) {
            current_.labels.push_back({label.label, {label.block.value}});
        }
        optimize_machine_function();
        result_.functions.push_back(std::move(current_));
    }

    const mir::ManagedModule& managed_;
    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    machine::Module result_;
    machine::Function current_;
    const mir::ManagedFunction* source_{};
    std::vector<std::optional<machine::VirtualRegisterId>> value_registers_;
};

} // namespace

machine::Module lower_managed_machine(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics) {
    return MachineLowerer(
        managed, hir_module, subtarget, options, diagnostics).run();
}

namespace {

bool safe_assembly_text(std::string_view text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char ch) {
               return ch >= 0x20 && ch != 0x7f && ch != '"' && ch != '\\';
           });
}

std::string quoted(std::string_view text) {
    return '"' + std::string(text) + '"';
}

std::string assembly_symbol(std::string_view symbol) {
    const bool simple = !symbol.empty() &&
        std::all_of(symbol.begin(), symbol.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                   ch == '$';
        });
    return simple ? std::string(symbol) : quoted(symbol);
}

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

class AssemblyEmitter {
public:
    AssemblyEmitter(machine::Module& module,
                    const hir::Module& hir_module,
                    const Subtarget& subtarget,
                    const CompilerOptions& options,
                    Diagnostics& diagnostics)
        : module_(module), hir_(hir_module), subtarget_(subtarget), options_(options),
          diagnostics_(diagnostics), format_(subtarget.object_format()) {}

    std::string run() {
        if (format_ != ObjectFormat::Elf && !module_.functions.empty()) {
            diagnostics_.error(
                module_.functions.front().location,
                "native MIPS assembly currently requires an ELF target");
            return {};
        }
        if (subtarget_.abi() == "eabi32") {
            // GNU MIPS linkers use this conventional empty marker in addition
            // to EF_MIPS_ABI_EABI32.  LLVM MC accepts the section even though
            // it cannot infer EABI32 from the PSP triple by itself.
            output_ << ".section .mdebug.eabi32\n.previous\n"
                       ".section .gcc_compiled_long32\n.previous\n";
        }
        for (auto& function : module_.functions) emit_function(function);
        return output_.str();
    }

private:
    struct ActiveSignature {
        const AbiEntry* abi{};
        SignatureLayout layout;
    };

    void raw_instruction(std::string_view opcode,
                         std::string_view operands = {}) {
        output_ << '\t' << opcode;
        if (!operands.empty()) output_ << '\t' << operands;
        output_ << '\n';
    }

    static bool writes_hilo(std::string_view opcode) {
        return opcode == "mult" || opcode == "multu" ||
               opcode == "dmult" || opcode == "dmultu" ||
               opcode == "div" || opcode == "divu" ||
               opcode == "ddiv" || opcode == "ddivu" ||
               opcode == "mthi" || opcode == "mtlo";
    }

    static bool memory_load(std::string_view opcode) {
        return opcode == "lb" || opcode == "lbu" || opcode == "lh" ||
               opcode == "lhu" || opcode == "lw" || opcode == "lwu" ||
               opcode == "ld" || opcode == "lwc1" || opcode == "ldc1" ||
               opcode == "ll" || opcode == "lld";
    }

    void instruction(std::string_view opcode,
                     std::string_view operands = {}) {
        // MIPS I-III HI/LO is unusual: MFHI/MFLO may be followed by ordinary
        // work, but not by a HI/LO writer until two instructions have passed.
        // Preserve useful instructions and materialize NOPs only if a writer
        // arrives while the architectural exclusion window is still open.
        if (hilo_write_barrier_ != 0) {
            if (writes_hilo(opcode)) {
                while (hilo_write_barrier_ != 0) {
                    raw_instruction("nop");
                    --hilo_write_barrier_;
                }
            } else {
                --hilo_write_barrier_;
            }
        }

        raw_instruction(opcode, operands);
        if (!subtarget_.has_feature(Feature::LoadInterlocks) &&
            memory_load(opcode)) {
            // This intentionally favors correctness over trying to infer a
            // pseudo-instruction's eventual register dependencies.  A later
            // scheduler can fill these MIPS-I load slots from Machine IR.
            raw_instruction("nop");
        }
        if (!subtarget_.has_feature(Feature::FpuTransferInterlocks) &&
            (opcode == "mfc1" || opcode == "mtc1" ||
             opcode == "dmfc1" || opcode == "dmtc1")) {
            raw_instruction("nop");
        }
        if (!subtarget_.has_feature(Feature::FpuCompareInterlocks) &&
            opcode.starts_with("c.")) {
            raw_instruction("nop");
        }
        if (!subtarget_.has_feature(Feature::HiloInterlocks) &&
            (opcode == "mfhi" || opcode == "mflo")) {
            hilo_write_barrier_ = 2;
        }
    }

    void encoded(std::uint32_t word, std::string_view comment) {
        output_ << "\t.word\t0x" << std::hex << std::setw(8)
                << std::setfill('0') << word << std::dec << std::setfill(' ');
        if (!comment.empty()) output_ << "\t# " << comment;
        output_ << '\n';
        if (hilo_write_barrier_ != 0) --hilo_write_barrier_;
    }

    static std::string reg_name(std::string_view name) {
        return "$" + std::string(name);
    }

    static bool fpr(std::string_view name) {
        return name.size() >= 2 && name.front() == 'f' &&
               std::all_of(name.begin() + 1, name.end(), [](unsigned char ch) {
                   return ch >= '0' && ch <= '9';
               });
    }

    static std::string memory(std::int64_t offset,
                              std::string_view base = "fp") {
        return std::to_string(offset) + "(" + reg_name(base) + ")";
    }

    std::string block_label(const machine::Function& function,
                            machine::BlockId block) const {
        return ".Lcross.mips." + std::to_string(function.source.value) +
               ".bb." + std::to_string(block.value);
    }

    std::string local_label(const machine::Function& function) {
        return ".Lcross.mips." + std::to_string(function.source.value) +
               ".tmp." + std::to_string(next_label_++);
    }

    const machine::StackSlot* spill_slot(
        const machine::Function& function,
        machine::Register value) const {
        if (value.kind != machine::RegisterKind::Virtual) return nullptr;
        const auto found = std::find_if(
            function.stack_slots.begin(), function.stack_slots.end(),
            [&](const machine::StackSlot& slot) {
                return slot.spill_for && slot.spill_for->value == value.id;
            });
        return found == function.stack_slots.end() ? nullptr : &*found;
    }

    const machine::StackSlot* named_slot(
        const machine::Function& function,
        std::string_view name) const {
        const auto found = std::find_if(
            function.stack_slots.begin(), function.stack_slots.end(),
            [&](const machine::StackSlot& slot) { return slot.name == name; });
        return found == function.stack_slots.end() ? nullptr : &*found;
    }

    std::int32_t slot_offset(const machine::Function& function,
                             machine::StackSlotId slot,
                             SourceLocation location) {
        if (slot.value >= function.stack_slots.size() ||
            !function.stack_slots[slot.value].frame_offset) {
            diagnostics_.error(location,
                               "MIPS frame references an unassigned stack slot");
            return 0;
        }
        return *function.stack_slots[slot.value].frame_offset;
    }

    std::int32_t vreg_offset(const machine::Function& function,
                             machine::Register value,
                             SourceLocation location) {
        const auto* slot = spill_slot(function, value);
        if (!slot || !slot->frame_offset) {
            diagnostics_.error(location,
                               "MIPS virtual register has no spill home");
            return 0;
        }
        return *slot->frame_offset;
    }

    std::optional<ActiveSignature> classify_entity(
        const hir::Function& entity, SourceLocation location,
        std::span<const hir::TypeId> actual_types = {}) {
        const auto* abi = abi_model(entity.abi);
        if (!abi) {
            diagnostics_.error(location,
                               "MIPS function has no registered ABI model");
            return std::nullopt;
        }
        std::vector<AbiValue> arguments;
        const auto count = actual_types.empty()
            ? entity.parameters.size() : actual_types.size();
        arguments.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto type = actual_types.empty()
                ? entity.parameters[index].type : actual_types[index];
            const bool fixed = index < entity.parameters.size();
            const auto transport = fixed &&
                                           entity.parameters[index].mode !=
                                               ParameterMode::In
                                       ? ValueTransport::ByReference
                                       : ValueTransport::Direct;
            arguments.push_back(abi_value_for(
                hir_, type, subtarget_.target().data_layout, *abi,
                transport));
        }
        std::vector<AbiValue> results;
        if (!is_void(hir_, entity.result_type)) {
            results.push_back(abi_value_for(
                hir_, entity.result_type, subtarget_.target().data_layout,
                *abi));
        }
        const auto classified = entity.variadic
            ? classify_variadic_signature(
                  *abi, arguments, results, entity.parameters.size(),
                  subtarget_.enabled_features())
            : classify_signature(*abi, arguments, results,
                                 subtarget_.enabled_features());
        if (!classified) {
            diagnostics_.error(
                location,
                "MIPS ABI model could not classify function interface '" +
                    entity.source_name + "'");
            return std::nullopt;
        }
        return ActiveSignature{abi, classified.layout};
    }

    static std::string parameter_home_name(std::string_view reg) {
        return "$abi.in." + std::string(reg);
    }

    void prepare_parameter_homes(machine::Function& function) {
        if (!active_signature_) return;
        for (const auto& assignment :
             active_signature_->layout.call.arguments) {
            for (const auto& piece : assignment.pieces) {
                if (piece.location.kind != LocationKind::Register) continue;
                const auto name = parameter_home_name(piece.location.reg);
                if (named_slot(function, name)) continue;
                machine::StackSlot home;
                home.id = {static_cast<std::uint32_t>(
                    function.stack_slots.size())};
                home.kind = machine::StackSlotKind::IncomingArgument;
                home.size = std::max(
                    4U, (static_cast<unsigned>(piece.carrier_bits) + 7U) /
                            8U);
                home.alignment = std::min(home.size, 8U);
                home.location = function.location;
                home.name = name;
                function.stack_slots.push_back(std::move(home));
            }
        }
    }

    void emit_parameter_homes(const machine::Function& function) {
        if (!active_signature_) return;
        std::vector<std::string> saved;
        for (const auto& assignment :
             active_signature_->layout.call.arguments) {
            for (const auto& piece : assignment.pieces) {
                if (piece.location.kind != LocationKind::Register ||
                    std::find(saved.begin(), saved.end(),
                              piece.location.reg) != saved.end()) {
                    continue;
                }
                const auto* home = named_slot(
                    function, parameter_home_name(piece.location.reg));
                if (!home || !home->frame_offset) {
                    diagnostics_.error(
                        function.location,
                        "MIPS incoming argument register has no frame home");
                    continue;
                }
                if (fpr(piece.location.reg)) {
                    instruction(piece.carrier_bits <= 32 ? "swc1" : "sdc1",
                                reg_name(piece.location.reg) + "," +
                                    memory(*home->frame_offset));
                } else {
                    store_integer_memory(piece.location.reg,
                                         memory(*home->frame_offset),
                                         piece.carrier_bits);
                }
                saved.push_back(piece.location.reg);
            }
        }
    }

    static unsigned effective_piece_offset(
        const ValuePiece& piece, unsigned value_bits, ByteOrder order) {
        if (order == ByteOrder::Little || piece.value_bits >= value_bits) {
            return piece.value_bit_offset;
        }
        const auto used = static_cast<unsigned>(piece.value_bit_offset) +
                          piece.value_bits;
        return used <= value_bits ? value_bits - used
                                  : piece.value_bit_offset;
    }

    void normalize_integer(std::string_view reg, unsigned bits,
                           bool sign) {
        if (bits >= 64) return;
        if (bits == 32) {
            if (subtarget_.has_feature(Feature::Mips3)) {
                if (sign) {
                    instruction("sll", reg_name(reg) + "," + reg_name(reg) +
                                           ",0");
                } else {
                    instruction("dsll32", reg_name(reg) + "," + reg_name(reg) +
                                              ",0");
                    instruction("dsrl32", reg_name(reg) + "," + reg_name(reg) +
                                              ",0");
                }
            }
            return;
        }
        if (bits == 16) {
            if (sign) {
                instruction("sll", reg_name(reg) + "," + reg_name(reg) +
                                       ",16");
                instruction("sra", reg_name(reg) + "," + reg_name(reg) +
                                       ",16");
            } else {
                instruction("andi", reg_name(reg) + "," + reg_name(reg) +
                                        ",65535");
            }
            return;
        }
        if (bits <= 8) {
            if (sign) {
                instruction("sll", reg_name(reg) + "," + reg_name(reg) +
                                       ",24");
                instruction("sra", reg_name(reg) + "," + reg_name(reg) +
                                       ",24");
            } else {
                instruction("andi", reg_name(reg) + "," + reg_name(reg) +
                                        ",255");
            }
        }
    }

    void load_integer_memory(std::string_view target,
                             std::string memory_operand, unsigned bits,
                             bool sign) {
        const auto opcode = bits <= 8 ? (sign ? "lb" : "lbu") :
                            bits <= 16 ? (sign ? "lh" : "lhu") :
                            bits <= 32 && !sign &&
                                    subtarget_.has_feature(Feature::Mips3)
                                ? "lwu"
                            : bits <= 32 ? "lw" : "ld";
        instruction(opcode, reg_name(target) + "," + memory_operand);
    }

    void store_integer_memory(std::string_view source,
                              std::string memory_operand, unsigned bits) {
        const auto opcode = bits <= 8 ? "sb" : bits <= 16 ? "sh" :
                            bits <= 32 ? "sw" : "sd";
        instruction(opcode, reg_name(source) + "," + memory_operand);
    }

    bool legalizes_to_pair(machine::Register value) const {
        return value.mode.bits > 32 && value.mode.bits <= 64 &&
               !subtarget_.has_feature(Feature::Mips3);
    }

    std::int64_t word_offset(std::int64_t base, bool high) const {
        const bool little = subtarget_.target().data_layout.byte_order ==
                            ByteOrder::Little;
        return base + ((little ? high : !high) ? 4 : 0);
    }

    void load_pair_memory(std::string_view low, std::string_view high,
                          std::int64_t offset,
                          std::string_view base = "fp") {
        instruction("lw", reg_name(low) + "," +
                              memory(word_offset(offset, false), base));
        instruction("lw", reg_name(high) + "," +
                              memory(word_offset(offset, true), base));
    }

    void store_pair_memory(std::string_view low, std::string_view high,
                           std::int64_t offset,
                           std::string_view base = "fp") {
        instruction("sw", reg_name(low) + "," +
                              memory(word_offset(offset, false), base));
        instruction("sw", reg_name(high) + "," +
                              memory(word_offset(offset, true), base));
    }

    void load_vreg_pair(const machine::Function& function,
                        machine::Register value, std::string_view low,
                        std::string_view high, SourceLocation location) {
        load_pair_memory(low, high, vreg_offset(function, value, location));
    }

    void store_vreg_pair(const machine::Function& function,
                         machine::Register value, std::string_view low,
                         std::string_view high, SourceLocation location) {
        store_pair_memory(low, high, vreg_offset(function, value, location));
    }

    void load_vreg(const machine::Function& function,
                   machine::Register value, std::string_view target,
                   SourceLocation location) {
        const auto offset = vreg_offset(function, value, location);
        load_integer_memory(target, memory(offset),
                            value.mode.bits > 32 ? 64U : 32U, true);
    }

    void store_vreg(const machine::Function& function,
                    machine::Register value, std::string_view source,
                    SourceLocation location) {
        const auto offset = vreg_offset(function, value, location);
        store_integer_memory(source, memory(offset),
                             value.mode.bits > 32 ? 64U : 32U);
    }

    void load_fvreg(const machine::Function& function,
                    machine::Register value, std::string_view target,
                    SourceLocation location) {
        const auto offset = vreg_offset(function, value, location);
        instruction(value.mode.bits == 32 ? "lwc1" : "ldc1",
                    reg_name(target) + "," + memory(offset));
    }

    void store_fvreg(const machine::Function& function,
                     machine::Register value, std::string_view source,
                     SourceLocation location) {
        const auto offset = vreg_offset(function, value, location);
        instruction(value.mode.bits == 32 ? "swc1" : "sdc1",
                    reg_name(source) + "," + memory(offset));
    }

    void copy_vreg(const machine::Function& function,
                   machine::Register target, machine::Register source,
                   SourceLocation location) {
        const auto floating = target.kind == machine::RegisterKind::Virtual &&
            target.id < function.virtual_register_classes.size() &&
            function.virtual_register_classes[target.id] ==
                machine::VirtualRegisterClass::Floating;
        if (floating) {
            load_fvreg(function, source, "f0", location);
            store_fvreg(function, target, "f0", location);
        } else if (legalizes_to_pair(target)) {
            load_vreg_pair(function, source, "t0", "t1", location);
            store_vreg_pair(function, target, "t0", "t1", location);
        } else {
            load_vreg(function, source, "t0", location);
            store_vreg(function, target, "t0", location);
        }
    }

    std::uint32_t outgoing_size(const machine::Function& function) {
        std::uint32_t size{};
        for (const auto& block : function.blocks) {
            for (const auto& value : block.instructions) {
                if (value.kind != machine::InstructionKind::Call ||
                    !value.direct_callee) {
                    continue;
                }
                const auto& callee = hir_.function(*value.direct_callee);
                auto classified = classify_entity(
                    callee, value.location, value.call_argument_types);
                if (!classified) continue;
                if (classified->layout.call.outgoing_area_size >
                    std::numeric_limits<std::uint32_t>::max()) {
                    diagnostics_.error(value.location,
                                       "MIPS outgoing call frame is too large");
                    continue;
                }
                size = std::max(
                    size, static_cast<std::uint32_t>(
                              classified->layout.call.outgoing_area_size));
            }
        }
        return size;
    }

    bool finalize_frame(machine::Function& function) {
        auto offset = outgoing_size(function);
        function.frame.outgoing_argument_size = offset;
        for (auto& slot : function.stack_slots) {
            if (slot.elided) continue;
            offset = align_up(offset, slot.alignment);
            slot.frame_offset = static_cast<std::int32_t>(offset);
            offset += slot.size;
        }
        offset = align_up(offset, 4);
        saved_fp_offset_ = offset;
        offset += 4;
        saved_ra_offset_ = offset;
        offset += 4;
        frame_size_ = align_up(
            offset, std::max(8U, function.frame.stack_alignment));
        if (frame_size_ > 32760U) {
            diagnostics_.error(
                function.location,
                "MIPS fixed frame exceeds the first backend slice's 16-bit "
                "frame-offset range");
            return false;
        }
        function.frame.local_size = frame_size_;
        function.frame.finalized = true;
        return true;
    }

    std::string incoming_memory(const ValuePiece& piece,
                                const AbiEntry& abi) const {
        const auto offset = static_cast<std::uint64_t>(frame_size_) +
                            callee_stack_offset(piece, abi);
        return memory(static_cast<std::int64_t>(offset));
    }

    void load_abi_piece(const machine::Function& function,
                        const ValuePiece& piece, const AbiEntry& abi,
                        std::string_view gpr, bool parameter_entry) {
        if (piece.location.kind == LocationKind::Register) {
            if (parameter_entry) {
                const auto* home = named_slot(
                    function, parameter_home_name(piece.location.reg));
                if (!home || !home->frame_offset) {
                    diagnostics_.error(
                        function.location,
                        "MIPS incoming argument register has no frame home");
                    return;
                }
                load_integer_memory(gpr, memory(*home->frame_offset),
                                    piece.carrier_bits, false);
            } else {
                instruction("move", reg_name(gpr) + "," +
                                        reg_name(piece.location.reg));
            }
        } else {
            load_integer_memory(
                gpr, incoming_memory(piece, abi), piece.carrier_bits, false);
        }
    }

    void assemble_incoming_integer(const machine::Function& function,
                                   const std::vector<ValuePiece>& pieces,
                                   const AbiEntry& abi, unsigned value_bits,
                                   bool sign, bool parameter_entry) {
        instruction("move", "$t0,$zero");
        for (const auto& piece : pieces) {
            load_abi_piece(function, piece, abi, "t1", parameter_entry);
            if (piece.value_bits < 32) {
                const auto mask = (std::uint64_t{1} << piece.value_bits) - 1U;
                instruction("andi", "$t1,$t1," + std::to_string(mask));
            } else if (piece.value_bits == 32 &&
                       subtarget_.has_feature(Feature::Mips3)) {
                instruction("dsll32", "$t1,$t1,0");
                instruction("dsrl32", "$t1,$t1,0");
            }
            const auto shift = effective_piece_offset(
                piece, value_bits, subtarget_.target().data_layout.byte_order);
            if (shift != 0) {
                instruction(value_bits > 32 ? "dsll" : "sll",
                            "$t1,$t1," + std::to_string(shift));
            }
            instruction(value_bits > 32 ? "or" : "or", "$t0,$t0,$t1");
        }
        normalize_integer("t0", value_bits, sign);
    }

    void assemble_incoming_pair(const machine::Function& function,
                                const std::vector<ValuePiece>& pieces,
                                const AbiEntry& abi, unsigned value_bits,
                                bool parameter_entry) {
        instruction("move", "$t0,$zero");
        instruction("move", "$t1,$zero");
        for (const auto& piece : pieces) {
            load_abi_piece(function, piece, abi, "t2", parameter_entry);
            if (piece.value_bits < 32) {
                const auto mask = (std::uint64_t{1} << piece.value_bits) - 1U;
                instruction("andi", "$t2,$t2," + std::to_string(mask));
            }
            const auto shift = effective_piece_offset(
                piece, value_bits,
                subtarget_.target().data_layout.byte_order);
            auto destination = std::string_view{"t0"};
            auto word_shift = shift;
            if (shift >= 32) {
                destination = "t1";
                word_shift -= 32;
            }
            if (word_shift != 0) {
                instruction("sll", "$t2,$t2," +
                                       std::to_string(word_shift));
            }
            instruction("or", reg_name(destination) + "," +
                                  reg_name(destination) + ",$t2");
        }
    }

    void capture_parameter(const machine::Function& function,
                           const machine::Instruction& value) {
        if (!active_signature_ || value.defs.empty() ||
            value.operands.empty()) {
            return;
        }
        const auto index = static_cast<std::size_t>(
            std::get<machine::ImmediateOperand>(value.operands.front()).value);
        const auto& entity = hir_.function(function.source);
        if (index >= entity.parameters.size() ||
            index >= active_signature_->layout.call.arguments.size()) {
            diagnostics_.error(value.location,
                               "MIPS parameter index exceeds ABI signature");
            return;
        }
        const auto& parameter = entity.parameters[index];
        const auto& assignment =
            active_signature_->layout.call.arguments[index];
        const auto target = value.defs.front();
        if (assignment.pieces.empty()) {
            diagnostics_.error(value.location,
                               "MIPS ABI parameter has no transport piece");
            return;
        }
        if (parameter.mode != ParameterMode::In) {
            const auto* pointer = named_slot(
                function, "$paramptr." + std::to_string(index));
            if (!pointer || !pointer->frame_offset) {
                diagnostics_.error(value.location,
                                   "MIPS output parameter has no pointer home");
                return;
            }
            load_abi_piece(function, assignment.pieces.front(),
                           *active_signature_->abi, "t0", true);
            instruction("sw", "$t0," + memory(*pointer->frame_offset));
            if (parameter.mode == ParameterMode::Out) {
                if (legalizes_to_pair(target)) {
                    store_vreg_pair(function, target, "zero", "zero",
                                    value.location);
                } else {
                    instruction("move", "$t1,$zero");
                    store_vreg(function, target, "t1", value.location);
                }
                return;
            }
            if (is_floating(hir_, parameter.type)) {
                instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                            "$f0,0($t0)");
                store_fvreg(function, target, "f0", value.location);
            } else {
                if (legalizes_to_pair(target)) {
                    load_pair_memory("t1", "t2", 0, "t0");
                    store_vreg_pair(function, target, "t1", "t2",
                                    value.location);
                } else {
                    load_integer_memory(
                        "t1", "0($t0)", target.mode.bits,
                        is_signed_integer(hir_, parameter.type));
                    store_vreg(function, target, "t1", value.location);
                }
            }
            return;
        }
        if (is_floating(hir_, parameter.type) &&
            assignment.pieces.size() == 1 &&
            assignment.pieces.front().location.kind ==
                LocationKind::Register &&
            fpr(assignment.pieces.front().location.reg)) {
            const auto& piece = assignment.pieces.front();
            const auto* home = named_slot(
                function, parameter_home_name(piece.location.reg));
            if (!home || !home->frame_offset) {
                diagnostics_.error(
                    value.location,
                    "MIPS floating argument register has no frame home");
                return;
            }
            instruction(piece.carrier_bits <= 32 ? "lwc1" : "ldc1",
                        "$f0," + memory(*home->frame_offset));
            store_fvreg(function, target, "f0", value.location);
            return;
        }
        const auto bits = type_bits(hir_, parameter.type);
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            assemble_incoming_pair(function, assignment.pieces,
                                   *active_signature_->abi, bits, true);
            store_vreg_pair(function, target, "t0", "t1", value.location);
        } else {
            assemble_incoming_integer(
                function, assignment.pieces, *active_signature_->abi, bits,
                is_signed_integer(hir_, parameter.type), true);
        }
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            // The pair was written above, including floating bit transport.
        } else if (is_floating(hir_, parameter.type)) {
            const auto offset = vreg_offset(function, target, value.location);
            store_integer_memory("t0", memory(offset), target.mode.bits);
        } else {
            store_vreg(function, target, "t0", value.location);
        }
    }

    void emit_copyouts(const machine::Function& function,
                       SourceLocation location) {
        const auto& entity = hir_.function(function.source);
        for (std::size_t index = 0; index < entity.parameters.size(); ++index) {
            const auto& parameter = entity.parameters[index];
            if (parameter.mode == ParameterMode::In) continue;
            const auto* pointer = named_slot(
                function, "$paramptr." + std::to_string(index));
            const auto* local = named_slot(
                function, "$param." + std::to_string(index));
            if (!pointer || !pointer->frame_offset || !local ||
                !local->frame_offset) {
                diagnostics_.error(
                    location,
                    "MIPS output parameter lacks its managed copy-out cells");
                continue;
            }
            instruction("lw", "$t0," + memory(*pointer->frame_offset));
            const auto bits = type_bits(hir_, parameter.type);
            if (is_floating(hir_, parameter.type)) {
                instruction(bits == 32 ? "lwc1" : "ldc1",
                            "$f0," + memory(*local->frame_offset));
                instruction(bits == 32 ? "swc1" : "sdc1",
                            "$f0,0($t0)");
            } else {
                if (bits > 32 &&
                    !subtarget_.has_feature(Feature::Mips3)) {
                    load_pair_memory("t1", "t2", *local->frame_offset);
                    store_pair_memory("t1", "t2", 0, "t0");
                } else {
                    load_integer_memory(
                        "t1", memory(*local->frame_offset), bits,
                        is_signed_integer(hir_, parameter.type));
                    store_integer_memory("t1", "0($t0)", bits);
                }
            }
        }
    }

    void place_integer_piece(const machine::Function& function,
                             machine::Register source,
                             const ValuePiece& piece, unsigned value_bits,
                             SourceLocation location) {
        const auto shift = effective_piece_offset(
            piece, value_bits, subtarget_.target().data_layout.byte_order);
        if (legalizes_to_pair(source)) {
            // ABI endpoints may include every ordinary caller-saved GPR.
            // Use the compiler-owned assembler temporary so placing one
            // piece cannot destroy an argument/result placed earlier.
            const auto offset = vreg_offset(function, source, location);
            const bool high = shift >= 32;
            instruction("lw", "$at," +
                                  memory(word_offset(offset, high)));
            const auto word_shift = shift - (high ? 32U : 0U);
            if (word_shift != 0) {
                instruction("srl", "$at,$at," +
                                       std::to_string(word_shift));
            }
        } else {
            load_vreg(function, source, "at", location);
        }
        if (!legalizes_to_pair(source) && shift != 0) {
            instruction(value_bits > 32 ? "dsrl" : "srl",
                        "$at,$at," + std::to_string(shift));
        }
        if (subtarget_.has_feature(Feature::Mips3) &&
            value_bits > piece.value_bits && piece.carrier_bits == 32) {
            // o32 was specified for 32-bit GPRs.  On a MIPS III processor,
            // GCC represents each 32-bit carrier as a sign-extended physical
            // GPR value.  Callers may rely on that invariant when consuming
            // split u64/f64 results, even though only the low word is payload.
            normalize_integer("at", 32, true);
        }
        if (piece.location.kind == LocationKind::Register) {
            instruction("move", reg_name(piece.location.reg) + ",$at");
        } else {
            store_integer_memory(
                "at", memory(static_cast<std::int64_t>(
                                  piece.location.stack_offset),
                              "sp"),
                piece.carrier_bits);
        }
    }

    void place_pointer_piece(const machine::Function& function,
                             const machine::StackSlotOperand& source,
                             const ValuePiece& piece,
                             SourceLocation location) {
        const auto offset = slot_offset(function, source.slot, location) +
                            source.offset;
        instruction("addiu", "$at,$fp," + std::to_string(offset));
        if (piece.location.kind == LocationKind::Register) {
            instruction("move", reg_name(piece.location.reg) + ",$at");
        } else {
            instruction("sw", "$at," +
                                  memory(static_cast<std::int64_t>(
                                             piece.location.stack_offset),
                                         "sp"));
        }
    }

    void place_floating_piece(const machine::Function& function,
                              machine::Register source,
                              const ValuePiece& piece,
                              SourceLocation location) {
        if (piece.location.kind == LocationKind::Register &&
            fpr(piece.location.reg)) {
            load_fvreg(function, source, piece.location.reg, location);
            return;
        }
        // Soft-float and the o32 "integer seen" rule transport floating bits
        // through ordinary GPR pieces.
        const auto offset = vreg_offset(function, source, location);
        const auto shift = effective_piece_offset(
            piece, source.mode.bits,
            subtarget_.target().data_layout.byte_order);
        if (legalizes_to_pair(source)) {
            const bool high = shift >= 32;
            instruction("lw", "$at," +
                                  memory(word_offset(offset, high)));
            const auto word_shift = shift - (high ? 32U : 0U);
            if (word_shift != 0) {
                instruction("srl", "$at,$at," +
                                       std::to_string(word_shift));
            }
        } else {
            load_integer_memory("at", memory(offset), source.mode.bits,
                                false);
        }
        if (!legalizes_to_pair(source) && shift != 0) {
            instruction("dsrl", "$at,$at," + std::to_string(shift));
        }
        if (subtarget_.has_feature(Feature::Mips3) &&
            source.mode.bits > piece.value_bits && piece.carrier_bits == 32) {
            normalize_integer("at", 32, true);
        }
        if (piece.location.kind == LocationKind::Register) {
            instruction("move", reg_name(piece.location.reg) + ",$at");
        } else {
            store_integer_memory(
                "at", memory(static_cast<std::int64_t>(
                                  piece.location.stack_offset),
                              "sp"),
                piece.carrier_bits);
        }
    }

    void capture_call_result(const machine::Function& function,
                             const machine::Instruction& call,
                             const ActiveSignature& signature) {
        if (call.defs.empty()) return;
        if (signature.layout.results.empty() ||
            signature.layout.results.front().pieces.empty() ||
            signature.layout.results.front().indirect) {
            diagnostics_.error(call.location,
                               "MIPS indirect call results are not implemented yet");
            return;
        }
        const auto target = call.defs.front();
        const auto& assignment = signature.layout.results.front();
        const auto& callee = hir_.function(*call.direct_callee);
        if (is_floating(hir_, callee.result_type) &&
            assignment.pieces.size() == 1 &&
            assignment.pieces.front().location.kind == LocationKind::Register &&
            fpr(assignment.pieces.front().location.reg)) {
            store_fvreg(function, target,
                        assignment.pieces.front().location.reg,
                        call.location);
            return;
        }
        const auto bits = type_bits(hir_, callee.result_type);
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            assemble_incoming_pair(function, assignment.pieces,
                                   *signature.abi, bits, false);
            store_vreg_pair(function, target, "t0", "t1", call.location);
        } else {
            assemble_incoming_integer(
                function, assignment.pieces, *signature.abi, bits,
                is_signed_integer(hir_, callee.result_type), false);
        }
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            // Already stored as two ABI words.
        } else if (is_floating(hir_, callee.result_type)) {
            store_integer_memory(
                "t0", memory(vreg_offset(function, target, call.location)),
                target.mode.bits);
        } else {
            store_vreg(function, target, "t0", call.location);
        }
    }

    void emit_call(const machine::Function& function,
                   const machine::Instruction& call) {
        if (!call.direct_callee) {
            diagnostics_.error(call.location,
                               "indirect MIPS calls are not implemented yet");
            return;
        }
        const auto& callee = hir_.function(*call.direct_callee);
        auto signature = classify_entity(
            callee, call.location, call.call_argument_types);
        if (!signature) return;
        if (signature->layout.call.arguments.size() + 1 !=
            call.operands.size()) {
            diagnostics_.error(call.location,
                               "MIPS call operands disagree with ABI layout");
            return;
        }
        for (std::size_t index = 0;
             index < signature->layout.call.arguments.size(); ++index) {
            const auto& assignment =
                signature->layout.call.arguments[index];
            const auto& operand = call.operands[index + 1];
            if (const auto* cell =
                    std::get_if<machine::StackSlotOperand>(&operand)) {
                for (const auto& piece : assignment.pieces) {
                    place_pointer_piece(function, *cell, piece,
                                        call.location);
                }
                continue;
            }
            const auto* source_operand =
                std::get_if<machine::RegisterOperand>(&operand);
            if (!source_operand) continue;
            const auto source = source_operand->value;
            const bool floating =
                index < call.call_argument_types.size() &&
                is_floating(hir_, call.call_argument_types[index]);
            for (const auto& piece : assignment.pieces) {
                if (floating) {
                    place_floating_piece(function, source, piece,
                                         call.location);
                } else {
                    place_integer_piece(
                        function, source, piece,
                        type_bits(hir_, call.call_argument_types[index]),
                        call.location);
                }
            }
            for (const auto& piece : assignment.shadows) {
                if (floating) {
                    place_floating_piece(function, source, piece,
                                         call.location);
                } else {
                    place_integer_piece(
                        function, source, piece,
                        type_bits(hir_, call.call_argument_types[index]),
                        call.location);
                }
            }
        }
        const auto* callee_symbol =
            std::get_if<machine::SymbolOperand>(&call.operands.front());
        if (!callee_symbol) return;
        instruction("jal", assembly_symbol(callee_symbol->name));
        instruction("nop");
        capture_call_result(function, call, *signature);
    }

    void place_return(const machine::Function& function,
                      const machine::Instruction& value) {
        emit_copyouts(function, value.location);
        const auto& entity = hir_.function(function.source);
        if (!value.uses.empty()) {
            if (!active_signature_ ||
                active_signature_->layout.results.empty()) {
                diagnostics_.error(value.location,
                                   "MIPS return has no ABI result layout");
            } else {
                const auto& result =
                    active_signature_->layout.results.front();
                if (result.indirect || result.pieces.empty()) {
                    diagnostics_.error(
                        value.location,
                        "MIPS indirect function results are not implemented yet");
                } else {
                    const auto source = value.uses.front();
                    if (is_floating(hir_, entity.result_type) &&
                        result.pieces.size() == 1 &&
                        result.pieces.front().location.kind ==
                            LocationKind::Register &&
                        fpr(result.pieces.front().location.reg)) {
                        load_fvreg(function, source,
                                   result.pieces.front().location.reg,
                                   value.location);
                    } else {
                        for (const auto& piece : result.pieces) {
                            if (is_floating(hir_, entity.result_type)) {
                                place_floating_piece(function, source, piece,
                                                     value.location);
                            } else {
                                place_integer_piece(
                                    function, source, piece,
                                    type_bits(hir_, entity.result_type),
                                    value.location);
                            }
                        }
                    }
                }
            }
        }
        instruction("b", epilogue_label_);
        instruction("nop");
    }

    // Instruction and control-flow emission are defined in the following
    // section; ABI transport is intentionally complete before target opcode
    // selection so o32 never depends on LLVM's calling-convention lowering.
    void emit_integer_pair_binary(const machine::Function& function,
                                  const machine::Instruction& value);
    void emit_integer_binary(const machine::Function& function,
                             const machine::Instruction& value);
    void emit_floating_binary(const machine::Function& function,
                              const machine::Instruction& value);
    void emit_cast(const machine::Function& function,
                   const machine::Instruction& value);
    void emit_atomic(const machine::Function& function,
                     const machine::Instruction& value);
    void emit_phi_edge_copies(const machine::Function& function,
                              machine::BlockId predecessor,
                              machine::BlockId successor);
    void emit_target(const machine::Function& function,
                     const machine::Instruction& value);
    void emit_terminator(const machine::Function& function,
                         const machine::Instruction& value,
                         machine::BlockId predecessor);
    void emit_function(machine::Function& function);

    machine::Module& module_;
    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    ObjectFormat format_;
    std::ostringstream output_;
    std::optional<ActiveSignature> active_signature_;
    std::uint32_t frame_size_{};
    std::uint32_t saved_fp_offset_{};
    std::uint32_t saved_ra_offset_{};
    std::uint32_t next_label_{};
    unsigned hilo_write_barrier_{};
    std::string epilogue_label_;
};

void AssemblyEmitter::emit_integer_pair_binary(
    const machine::Function& function,
    const machine::Instruction& value) {
    if (value.uses.size() < 2 || value.defs.empty()) return;
    const auto left = value.uses[0];
    const auto right = value.uses[1];
    const auto target = value.defs.front();
    const auto opcode = decode_opcode(value.opcode);
    load_vreg_pair(function, left, "t0", "t1", value.location);
    if (legalizes_to_pair(right)) {
        load_vreg_pair(function, right, "t2", "t3", value.location);
    } else {
        load_vreg(function, right, "t2", value.location);
        instruction("move", "$t3,$zero");
    }

    const auto store_pair = [&] {
        store_vreg_pair(function, target, "t4", "t5", value.location);
    };
    const auto store_boolean = [&] {
        store_vreg(function, target, "t4", value.location);
    };
    const auto emit_less = [&](bool is_signed, bool reverse) {
        const auto left_low = reverse ? "t2" : "t0";
        const auto left_high = reverse ? "t3" : "t1";
        const auto right_low = reverse ? "t0" : "t2";
        const auto right_high = reverse ? "t1" : "t3";
        instruction(is_signed ? "slt" : "sltu",
                    "$t4," + reg_name(left_high) + "," +
                        reg_name(right_high));
        instruction("xor", "$t6," + reg_name(left_high) + "," +
                               reg_name(right_high));
        instruction("sltiu", "$t6,$t6,1");
        instruction("sltu", "$t7," + reg_name(left_low) + "," +
                                reg_name(right_low));
        instruction("and", "$t7,$t7,$t6");
        instruction("or", "$t4,$t4,$t7");
    };

    switch (opcode) {
    case Opcode::Add:
        instruction("addu", "$t4,$t0,$t2");
        instruction("sltu", "$t6,$t4,$t0");
        instruction("addu", "$t5,$t1,$t3");
        instruction("addu", "$t5,$t5,$t6");
        store_pair();
        return;
    case Opcode::Sub:
        instruction("sltu", "$t6,$t0,$t2");
        instruction("subu", "$t4,$t0,$t2");
        instruction("subu", "$t5,$t1,$t3");
        instruction("subu", "$t5,$t5,$t6");
        store_pair();
        return;
    case Opcode::Mul:
        instruction("multu", "$t0,$t2");
        instruction("mflo", "$t4");
        instruction("mfhi", "$t5");
        instruction("multu", "$t0,$t3");
        instruction("mflo", "$t6");
        instruction("addu", "$t5,$t5,$t6");
        instruction("multu", "$t1,$t2");
        instruction("mflo", "$t6");
        instruction("addu", "$t5,$t5,$t6");
        store_pair();
        return;
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor: {
        const auto mnemonic = opcode == Opcode::And ? "and" :
                              opcode == Opcode::Or ? "or" : "xor";
        instruction(mnemonic, "$t4,$t0,$t2");
        instruction(mnemonic, "$t5,$t1,$t3");
        store_pair();
        return;
    }
    case Opcode::CmpEq:
    case Opcode::CmpNe:
        instruction("xor", "$t4,$t0,$t2");
        instruction("xor", "$t5,$t1,$t3");
        instruction("or", "$t4,$t4,$t5");
        instruction(opcode == Opcode::CmpEq ? "sltiu" : "sltu",
                    opcode == Opcode::CmpEq ? "$t4,$t4,1"
                                            : "$t4,$zero,$t4");
        store_boolean();
        return;
    case Opcode::CmpSlt: emit_less(true, false); store_boolean(); return;
    case Opcode::CmpUlt: emit_less(false, false); store_boolean(); return;
    case Opcode::CmpSgt: emit_less(true, true); store_boolean(); return;
    case Opcode::CmpUgt: emit_less(false, true); store_boolean(); return;
    case Opcode::CmpSle:
        emit_less(true, true);
        instruction("xori", "$t4,$t4,1");
        store_boolean();
        return;
    case Opcode::CmpUle:
        emit_less(false, true);
        instruction("xori", "$t4,$t4,1");
        store_boolean();
        return;
    case Opcode::CmpSge:
        emit_less(true, false);
        instruction("xori", "$t4,$t4,1");
        store_boolean();
        return;
    case Opcode::CmpUge:
        emit_less(false, false);
        instruction("xori", "$t4,$t4,1");
        store_boolean();
        return;
    case Opcode::Shl:
    case Opcode::ShrS:
    case Opcode::ShrU:
    case Opcode::Rotl:
    case Opcode::Rotr: {
        if (options_.machine_combine &&
            options_.optimize_for == OptimizationGoal::Speed) {
            const auto below_word = local_label(function);
            const auto rotate_word = local_label(function);
            const auto done = local_label(function);
            instruction("andi", "$t2,$t2,63");
            instruction("move", "$t4,$t0");
            instruction("move", "$t5,$t1");
            instruction("beq", "$t2,$zero," + done);
            instruction("nop");
            instruction("sltiu", "$t6,$t2,32");
            instruction("bne", "$t6,$zero," + below_word);
            instruction("nop");
            instruction("addiu", "$t2,$t2,-32");

            if (opcode == Opcode::Shl) {
                instruction("move", "$t4,$zero");
                instruction("sllv", "$t5,$t0,$t2");
            } else if (opcode == Opcode::ShrU ||
                       opcode == Opcode::ShrS) {
                instruction(opcode == Opcode::ShrS ? "srav" : "srlv",
                            "$t4,$t1,$t2");
                instruction(opcode == Opcode::ShrS ? "sra" : "move",
                            opcode == Opcode::ShrS ? "$t5,$t1,31"
                                                   : "$t5,$zero");
            } else {
                instruction("beq", "$t2,$zero," + rotate_word);
                instruction("nop");
                instruction("subu", "$t7,$zero,$t2");
                if (opcode == Opcode::Rotl) {
                    instruction("sllv", "$t4,$t1,$t2");
                    instruction("srlv", "$t6,$t0,$t7");
                    instruction("or", "$t4,$t4,$t6");
                    instruction("sllv", "$t5,$t0,$t2");
                    instruction("srlv", "$t6,$t1,$t7");
                    instruction("or", "$t5,$t5,$t6");
                } else {
                    instruction("srlv", "$t4,$t1,$t2");
                    instruction("sllv", "$t6,$t0,$t7");
                    instruction("or", "$t4,$t4,$t6");
                    instruction("srlv", "$t5,$t0,$t2");
                    instruction("sllv", "$t6,$t1,$t7");
                    instruction("or", "$t5,$t5,$t6");
                }
            }
            instruction("b", done);
            instruction("nop");

            if (opcode == Opcode::Rotl || opcode == Opcode::Rotr) {
                output_ << rotate_word << ":\n";
                instruction("move", "$t4,$t1");
                instruction("move", "$t5,$t0");
                instruction("b", done);
                instruction("nop");
            }

            output_ << below_word << ":\n";
            instruction("subu", "$t7,$zero,$t2");
            if (opcode == Opcode::Shl) {
                instruction("sllv", "$t4,$t0,$t2");
                instruction("sllv", "$t5,$t1,$t2");
                instruction("srlv", "$t6,$t0,$t7");
                instruction("or", "$t5,$t5,$t6");
            } else if (opcode == Opcode::ShrU ||
                       opcode == Opcode::ShrS) {
                instruction("srlv", "$t4,$t0,$t2");
                instruction("sllv", "$t6,$t1,$t7");
                instruction("or", "$t4,$t4,$t6");
                instruction(opcode == Opcode::ShrS ? "srav" : "srlv",
                            "$t5,$t1,$t2");
            } else if (opcode == Opcode::Rotl) {
                instruction("sllv", "$t4,$t0,$t2");
                instruction("srlv", "$t6,$t1,$t7");
                instruction("or", "$t4,$t4,$t6");
                instruction("sllv", "$t5,$t1,$t2");
                instruction("srlv", "$t6,$t0,$t7");
                instruction("or", "$t5,$t5,$t6");
            } else {
                instruction("srlv", "$t4,$t0,$t2");
                instruction("sllv", "$t6,$t1,$t7");
                instruction("or", "$t4,$t4,$t6");
                instruction("srlv", "$t5,$t1,$t2");
                instruction("sllv", "$t6,$t0,$t7");
                instruction("or", "$t5,$t5,$t6");
            }
            output_ << done << ":\n";
            store_pair();
            return;
        }

        const auto loop = local_label(function);
        const auto done = local_label(function);
        instruction("andi", "$t2,$t2,63");
        instruction("move", "$t4,$t0");
        instruction("move", "$t5,$t1");
        instruction("beq", "$t2,$zero," + done);
        instruction("nop");
        output_ << loop << ":\n";
        if (opcode == Opcode::Shl || opcode == Opcode::Rotl) {
            instruction("srl", "$t6,$t4,31");
            if (opcode == Opcode::Rotl) {
                instruction("srl", "$t7,$t5,31");
            }
            instruction("sll", "$t5,$t5,1");
            instruction("or", "$t5,$t5,$t6");
            instruction("sll", "$t4,$t4,1");
            if (opcode == Opcode::Rotl) {
                instruction("or", "$t4,$t4,$t7");
            }
        } else {
            instruction("sll", "$t6,$t5,31");
            if (opcode == Opcode::Rotr) {
                instruction("andi", "$t7,$t4,1");
            }
            instruction("srl", "$t4,$t4,1");
            instruction("or", "$t4,$t4,$t6");
            instruction(opcode == Opcode::ShrS ? "sra" : "srl",
                        "$t5,$t5,1");
            if (opcode == Opcode::Rotr) {
                instruction("sll", "$t7,$t7,31");
                instruction("or", "$t5,$t5,$t7");
            }
        }
        instruction("addiu", "$t2,$t2,-1");
        instruction("bne", "$t2,$zero," + loop);
        instruction("nop");
        output_ << done << ":\n";
        store_pair();
        return;
    }
    case Opcode::Sdiv:
    case Opcode::Srem:
    case Opcode::Udiv:
    case Opcode::Urem: {
        const bool is_signed = opcode == Opcode::Sdiv ||
                               opcode == Opcode::Srem;
        const bool remainder = opcode == Opcode::Srem ||
                               opcode == Opcode::Urem;
        const auto left_ready = local_label(function);
        const auto right_ready = local_label(function);
        const auto loop = local_label(function);
        const auto subtract = local_label(function);
        const auto next = local_label(function);
        const auto quotient_ready = local_label(function);
        const auto remainder_ready = local_label(function);
        if (is_signed) {
            instruction("srl", "$a0,$t1,31");
            instruction("srl", "$a1,$t3,31");
            instruction("xor", "$a0,$a0,$a1");
            instruction("srl", "$a1,$t1,31");
            instruction("bgez", "$t1," + left_ready);
            instruction("nop");
            instruction("subu", "$t0,$zero,$t0");
            instruction("sltu", "$t7,$zero,$t0");
            instruction("subu", "$t1,$zero,$t1");
            instruction("subu", "$t1,$t1,$t7");
            output_ << left_ready << ":\n";
            instruction("bgez", "$t3," + right_ready);
            instruction("nop");
            instruction("subu", "$t2,$zero,$t2");
            instruction("sltu", "$t7,$zero,$t2");
            instruction("subu", "$t3,$zero,$t3");
            instruction("subu", "$t3,$t3,$t7");
            output_ << right_ready << ":\n";
        }
        instruction("move", "$t4,$zero");
        instruction("move", "$t5,$zero");
        instruction("li", "$t6,64");
        output_ << loop << ":\n";
        instruction("srl", "$t7,$t1,31");
        instruction("sll", "$t8,$t5,1");
        instruction("srl", "$t9,$t4,31");
        instruction("or", "$t5,$t8,$t9");
        instruction("sll", "$t4,$t4,1");
        instruction("or", "$t4,$t4,$t7");
        // The dividend is kept as a low/high pair in t0/t1.  After its
        // current top bit has entered the remainder, shift the quotient
        // candidate left as one 64-bit value: t0's top bit crosses into t1.
        instruction("srl", "$t7,$t0,31");
        instruction("sll", "$t1,$t1,1");
        instruction("or", "$t1,$t1,$t7");
        instruction("sll", "$t0,$t0,1");
        instruction("sltu", "$t7,$t5,$t3");
        instruction("bne", "$t7,$zero," + next);
        instruction("nop");
        instruction("sltu", "$t7,$t3,$t5");
        instruction("bne", "$t7,$zero," + subtract);
        instruction("nop");
        instruction("sltu", "$t7,$t4,$t2");
        instruction("bne", "$t7,$zero," + next);
        instruction("nop");
        output_ << subtract << ":\n";
        instruction("sltu", "$t7,$t4,$t2");
        instruction("subu", "$t4,$t4,$t2");
        instruction("subu", "$t5,$t5,$t3");
        instruction("subu", "$t5,$t5,$t7");
        instruction("ori", "$t0,$t0,1");
        output_ << next << ":\n";
        instruction("addiu", "$t6,$t6,-1");
        instruction("bne", "$t6,$zero," + loop);
        instruction("nop");
        if (is_signed) {
            instruction("beq", "$a0,$zero," + quotient_ready);
            instruction("nop");
            instruction("subu", "$t0,$zero,$t0");
            instruction("sltu", "$t7,$zero,$t0");
            instruction("subu", "$t1,$zero,$t1");
            instruction("subu", "$t1,$t1,$t7");
            output_ << quotient_ready << ":\n";
            instruction("beq", "$a1,$zero," + remainder_ready);
            instruction("nop");
            instruction("subu", "$t4,$zero,$t4");
            instruction("sltu", "$t7,$zero,$t4");
            instruction("subu", "$t5,$zero,$t5");
            instruction("subu", "$t5,$t5,$t7");
            output_ << remainder_ready << ":\n";
        }
        if (!remainder) {
            instruction("move", "$t4,$t0");
            instruction("move", "$t5,$t1");
        }
        store_pair();
        return;
    }
    default:
        diagnostics_.error(value.location,
                           "unknown MIPS pair-legalized integer operation");
        return;
    }
}

void AssemblyEmitter::emit_integer_binary(
    const machine::Function& function,
    const machine::Instruction& value) {
    if (value.uses.size() < 2 || value.defs.empty()) return;
    const auto left = value.uses[0];
    const auto right = value.uses[1];
    const auto target = value.defs.front();
    const bool wide = left.mode.bits > 32;
    if (wide && !subtarget_.has_feature(Feature::Mips3)) {
        emit_integer_pair_binary(function, value);
        return;
    }
    load_vreg(function, left, "t0", value.location);
    load_vreg(function, right, "t1", value.location);
    switch (decode_opcode(value.opcode)) {
    case Opcode::Add:
        instruction(wide ? "daddu" : "addu", "$t2,$t0,$t1");
        break;
    case Opcode::Sub:
        instruction(wide ? "dsubu" : "subu", "$t2,$t0,$t1");
        break;
    case Opcode::Mul:
        instruction(wide ? "dmult" : "mult", "$t0,$t1");
        instruction("mflo", "$t2");
        break;
    case Opcode::Sdiv:
    case Opcode::Srem:
        instruction(wide ? "ddiv" : "div", "$zero,$t0,$t1");
        instruction(value.opcode == Opcode::Srem ? "mfhi" : "mflo", "$t2");
        break;
    case Opcode::Udiv:
    case Opcode::Urem:
        instruction(wide ? "ddivu" : "divu", "$zero,$t0,$t1");
        instruction(value.opcode == Opcode::Urem ? "mfhi" : "mflo", "$t2");
        break;
    case Opcode::And: instruction("and", "$t2,$t0,$t1"); break;
    case Opcode::Or: instruction("or", "$t2,$t0,$t1"); break;
    case Opcode::Xor: instruction("xor", "$t2,$t0,$t1"); break;
    case Opcode::Shl:
        instruction(wide ? "dsllv" : "sllv", "$t2,$t0,$t1");
        break;
    case Opcode::ShrS:
        instruction(wide ? "dsrav" : "srav", "$t2,$t0,$t1");
        break;
    case Opcode::ShrU:
        instruction(wide ? "dsrlv" : "srlv", "$t2,$t0,$t1");
        break;
    case Opcode::Rotl:
        if (!wide && subtarget_.has_feature(Feature::Rotate)) {
            instruction("subu", "$t3,$zero,$t1");
            encoded(0x01685046U, "rorv $t2,$t0,$t3");
        } else {
            instruction(wide ? "dsubu" : "subu", "$t3,$zero,$t1");
            instruction(wide ? "dsllv" : "sllv", "$t2,$t0,$t1");
            instruction(wide ? "dsrlv" : "srlv", "$t3,$t0,$t3");
            instruction("or", "$t2,$t2,$t3");
        }
        break;
    case Opcode::Rotr:
        if (!wide && subtarget_.has_feature(Feature::Rotate)) {
            encoded(0x01285046U, "rorv $t2,$t0,$t1");
        } else {
            instruction(wide ? "dsubu" : "subu", "$t3,$zero,$t1");
            instruction(wide ? "dsrlv" : "srlv", "$t2,$t0,$t1");
            instruction(wide ? "dsllv" : "sllv", "$t3,$t0,$t3");
            instruction("or", "$t2,$t2,$t3");
        }
        break;
    case Opcode::CmpEq:
        instruction("xor", "$t2,$t0,$t1");
        instruction("sltiu", "$t2,$t2,1");
        break;
    case Opcode::CmpNe:
        instruction("xor", "$t2,$t0,$t1");
        instruction("sltu", "$t2,$zero,$t2");
        break;
    case Opcode::CmpSlt: instruction("slt", "$t2,$t0,$t1"); break;
    case Opcode::CmpUlt: instruction("sltu", "$t2,$t0,$t1"); break;
    case Opcode::CmpSgt: instruction("slt", "$t2,$t1,$t0"); break;
    case Opcode::CmpUgt: instruction("sltu", "$t2,$t1,$t0"); break;
    case Opcode::CmpSle:
        instruction("slt", "$t2,$t1,$t0");
        instruction("xori", "$t2,$t2,1");
        break;
    case Opcode::CmpUle:
        instruction("sltu", "$t2,$t1,$t0");
        instruction("xori", "$t2,$t2,1");
        break;
    case Opcode::CmpSge:
        instruction("slt", "$t2,$t0,$t1");
        instruction("xori", "$t2,$t2,1");
        break;
    case Opcode::CmpUge:
        instruction("sltu", "$t2,$t0,$t1");
        instruction("xori", "$t2,$t2,1");
        break;
    default:
        diagnostics_.error(value.location,
                           "unknown MIPS integer Machine IR operation");
        return;
    }
    store_vreg(function, target, "t2", value.location);
}

void AssemblyEmitter::emit_floating_binary(
    const machine::Function& function,
    const machine::Instruction& value) {
    if (value.uses.size() < 2 || value.defs.empty()) return;
    const auto left = value.uses[0];
    const auto right = value.uses[1];
    const auto target = value.defs.front();
    const std::string suffix = left.mode.bits == 32 ? ".s" : ".d";
    load_fvreg(function, left, "f0", value.location);
    load_fvreg(function, right, "f2", value.location);
    switch (decode_opcode(value.opcode)) {
    case Opcode::Fadd:
        instruction("add" + suffix, "$f4,$f0,$f2");
        store_fvreg(function, target, "f4", value.location);
        return;
    case Opcode::Fsub:
        instruction("sub" + suffix, "$f4,$f0,$f2");
        store_fvreg(function, target, "f4", value.location);
        return;
    case Opcode::Fmul:
        instruction("mul" + suffix, "$f4,$f0,$f2");
        if (subtarget_.has_feature(Feature::Fix4300)) instruction("nop");
        store_fvreg(function, target, "f4", value.location);
        return;
    case Opcode::Fdiv:
        instruction("div" + suffix, "$f4,$f0,$f2");
        store_fvreg(function, target, "f4", value.location);
        return;
    default: break;
    }

    bool true_on_condition = true;
    switch (decode_opcode(value.opcode)) {
    case Opcode::FcmpEq: instruction("c.eq" + suffix, "$f0,$f2"); break;
    case Opcode::FcmpNe:
        instruction("c.eq" + suffix, "$f0,$f2");
        true_on_condition = false;
        break;
    case Opcode::FcmpLt: instruction("c.lt" + suffix, "$f0,$f2"); break;
    case Opcode::FcmpLe: instruction("c.le" + suffix, "$f0,$f2"); break;
    case Opcode::FcmpGt: instruction("c.lt" + suffix, "$f2,$f0"); break;
    case Opcode::FcmpGe: instruction("c.le" + suffix, "$f2,$f0"); break;
    default:
        diagnostics_.error(value.location,
                           "unknown MIPS floating Machine IR operation");
        return;
    }
    const auto yes = local_label(function);
    const auto done = local_label(function);
    instruction(true_on_condition ? "bc1t" : "bc1f", yes);
    instruction("move", "$t0,$zero");
    instruction("b", done);
    instruction("nop");
    output_ << yes << ":\n";
    instruction("li", "$t0,1");
    output_ << done << ":\n";
    store_vreg(function, target, "t0", value.location);
}

void AssemblyEmitter::emit_cast(const machine::Function& function,
                                const machine::Instruction& value) {
    if (value.uses.empty() || value.defs.empty()) return;
    const auto source = value.uses.front();
    const auto target = value.defs.front();
    const bool source_float = source.kind == machine::RegisterKind::Virtual &&
        source.id < function.virtual_register_classes.size() &&
        function.virtual_register_classes[source.id] ==
            machine::VirtualRegisterClass::Floating;
    const bool target_float = target.kind == machine::RegisterKind::Virtual &&
        target.id < function.virtual_register_classes.size() &&
        function.virtual_register_classes[target.id] ==
            machine::VirtualRegisterClass::Floating;
    const auto opcode = decode_opcode(value.opcode);

    if (opcode == Opcode::Sext || opcode == Opcode::Zext ||
        opcode == Opcode::Trunc || opcode == Opcode::Reinterpret) {
        const bool sign = opcode == Opcode::Sext;
        if (legalizes_to_pair(source) && legalizes_to_pair(target)) {
            copy_vreg(function, target, source, value.location);
        } else if (legalizes_to_pair(source)) {
            load_vreg_pair(function, source, "t0", "t1", value.location);
            normalize_integer("t0", target.mode.bits, sign);
            store_vreg(function, target, "t0", value.location);
        } else if (legalizes_to_pair(target)) {
            load_vreg(function, source, "t0", value.location);
            normalize_integer("t0", source.mode.bits, sign);
            if (sign) instruction("sra", "$t1,$t0,31");
            else instruction("move", "$t1,$zero");
            store_vreg_pair(function, target, "t0", "t1", value.location);
        } else {
            load_vreg(function, source, "t0", value.location);
            // MIPS III `lw` sign-extends into the 64-bit physical GPR even
            // when the Machine IR value is unsigned.  Extension semantics
            // are determined by the source width; normalizing to the wider
            // destination is a no-op and used to leak those sign bits into
            // u32 -> u64 casts.  Truncation instead normalizes the retained
            // destination width.  Reinterpretation preserves all bits.
            if (opcode == Opcode::Sext || opcode == Opcode::Zext) {
                normalize_integer("t0", source.mode.bits, sign);
            } else if (opcode == Opcode::Trunc) {
                normalize_integer("t0", target.mode.bits, false);
            }
            store_vreg(function, target, "t0", value.location);
        }
        return;
    }
    if (opcode == Opcode::Freinterpret) {
        if (source.mode.bits > 32 && target.mode.bits > 32 &&
            !subtarget_.has_feature(Feature::Mips3)) {
            load_pair_memory(
                "t0", "t1", vreg_offset(function, source, value.location));
            store_pair_memory(
                "t0", "t1", vreg_offset(function, target, value.location));
        } else if (source_float && target_float) {
            copy_vreg(function, target, source, value.location);
        } else if (source_float) {
            load_fvreg(function, source, "f0", value.location);
            instruction(source.mode.bits == 32 ? "mfc1" : "dmfc1",
                        "$t0,$f0");
            store_vreg(function, target, "t0", value.location);
        } else if (target_float) {
            load_vreg(function, source, "t0", value.location);
            instruction(target.mode.bits == 32 ? "mtc1" : "dmtc1",
                        "$t0,$f0");
            store_fvreg(function, target, "f0", value.location);
        }
        return;
    }
    if (opcode == Opcode::Fextend || opcode == Opcode::Ftruncate) {
        load_fvreg(function, source, "f0", value.location);
        instruction(opcode == Opcode::Fextend ? "cvt.d.s" : "cvt.s.d",
                    "$f2,$f0");
        store_fvreg(function, target, "f2", value.location);
        return;
    }
    if (opcode == Opcode::Sitofp || opcode == Opcode::Uitofp) {
        load_vreg(function, source, "t0", value.location);
        const bool use_long = source.mode.bits > 32 || opcode == Opcode::Uitofp;
        if (use_long && !subtarget_.has_feature(Feature::Mips3)) {
            diagnostics_.error(value.location,
                               "64-bit integer-to-float conversion requires MIPS III");
            return;
        }
        if (opcode == Opcode::Uitofp && source.mode.bits == 64) {
            diagnostics_.error(
                value.location,
                "full-range u64-to-float conversion is not implemented in the MIPS slice yet");
            return;
        }
        instruction(use_long ? "dmtc1" : "mtc1", "$t0,$f0");
        const auto destination = target.mode.bits == 32 ? "cvt.s." : "cvt.d.";
        instruction(destination + std::string(use_long ? "l" : "w"),
                    "$f2,$f0");
        store_fvreg(function, target, "f2", value.location);
        return;
    }
    if (opcode == Opcode::Fptosi || opcode == Opcode::Fptoui) {
        load_fvreg(function, source, "f0", value.location);
        const bool use_long = target.mode.bits > 32 || opcode == Opcode::Fptoui;
        if (use_long && !subtarget_.has_feature(Feature::Mips3)) {
            diagnostics_.error(value.location,
                               "wide float-to-integer conversion requires MIPS III");
            return;
        }
        if (opcode == Opcode::Fptoui && target.mode.bits == 64) {
            diagnostics_.error(
                value.location,
                "full-range float-to-u64 conversion is not implemented in the MIPS slice yet");
            return;
        }
        const auto source_suffix = source.mode.bits == 32 ? ".s" : ".d";
        instruction(std::string(use_long ? "trunc.l" : "trunc.w") +
                        source_suffix,
                    "$f2,$f0");
        instruction(use_long ? "dmfc1" : "mfc1", "$t0,$f2");
        normalize_integer("t0", target.mode.bits,
                          opcode == Opcode::Fptosi);
        store_vreg(function, target, "t0", value.location);
        return;
    }
    diagnostics_.error(value.location, "unknown MIPS cast operation");
}

void AssemblyEmitter::emit_atomic(const machine::Function& function,
                                  const machine::Instruction& value) {
    const auto opcode = decode_opcode(value.opcode);
    if (opcode == Opcode::AtomicSignalFence) return;
    if (opcode == Opcode::AtomicThreadFence) {
        instruction("sync");
        return;
    }
    if (!subtarget_.has_feature(Feature::Llsc) || value.uses.empty()) {
        diagnostics_.error(value.location,
                           "selected MIPS CPU has no LL/SC atomic lowering");
        return;
    }
    const auto* metadata = value.operands.empty()
        ? nullptr
        : std::get_if<machine::ImmediateOperand>(&value.operands.back());
    const auto bits = metadata ? static_cast<unsigned>(metadata->value) : 0U;
    if (bits != 32) {
        diagnostics_.error(
            value.location,
            "the first MIPS LL/SC lowering supports 32-bit atomic objects only");
        return;
    }
    load_vreg(function, value.uses.front(), "t0", value.location);
    if (opcode == Opcode::AtomicLoad) {
        instruction("sync");
        instruction("lw", "$t1,0($t0)");
        instruction("sync");
        if (!value.defs.empty()) {
            store_vreg(function, value.defs.front(), "t1", value.location);
        }
        return;
    }
    if (opcode == Opcode::AtomicStore) {
        if (value.uses.size() < 2) return;
        load_vreg(function, value.uses[1], "t1", value.location);
        instruction("sync");
        instruction("sw", "$t1,0($t0)");
        instruction("sync");
        return;
    }
    if (opcode == Opcode::AtomicCompareExchange) {
        if (value.uses.size() < 3 || value.defs.empty()) return;
        load_vreg(function, value.uses[1], "t3", value.location);
        instruction("lw", "$t4,0($t3)");
        load_vreg(function, value.uses[2], "t5", value.location);
        const auto retry = local_label(function);
        const auto failed = local_label(function);
        const auto done = local_label(function);
        instruction("sync");
        output_ << retry << ":\n";
        instruction("ll", "$t1,0($t0)");
        instruction("bne", "$t1,$t4," + failed);
        instruction("move", "$t2,$t5");
        instruction("sc", "$t2,0($t0)");
        instruction("beq", "$t2,$zero," + retry);
        instruction("nop");
        instruction("li", "$t2,1");
        instruction("b", done);
        instruction("nop");
        output_ << failed << ":\n";
        instruction("sw", "$t1,0($t3)");
        instruction("move", "$t2,$zero");
        output_ << done << ":\n";
        instruction("sync");
        store_vreg(function, value.defs.front(), "t2", value.location);
        return;
    }
    if (value.uses.size() < 2 || value.defs.empty()) return;
    load_vreg(function, value.uses[1], "t3", value.location);
    const auto retry = local_label(function);
    instruction("sync");
    output_ << retry << ":\n";
    instruction("ll", "$t1,0($t0)");
    switch (opcode) {
    case Opcode::AtomicExchange: instruction("move", "$t2,$t3"); break;
    case Opcode::AtomicFetchAdd: instruction("addu", "$t2,$t1,$t3"); break;
    case Opcode::AtomicFetchSub: instruction("subu", "$t2,$t1,$t3"); break;
    case Opcode::AtomicFetchAnd: instruction("and", "$t2,$t1,$t3"); break;
    case Opcode::AtomicFetchXor: instruction("xor", "$t2,$t1,$t3"); break;
    case Opcode::AtomicFetchOr: instruction("or", "$t2,$t1,$t3"); break;
    case Opcode::AtomicFetchUpdate: {
        if (!metadata) return;
        const auto operation = static_cast<mir::BinaryOperation>(
            (metadata->high >> 16U) & 0xffU);
        switch (operation) {
        case mir::BinaryOperation::Multiply:
            instruction("mult", "$t1,$t3");
            instruction("mflo", "$t2");
            break;
        case mir::BinaryOperation::SignedDivide:
            instruction("div", "$zero,$t1,$t3");
            instruction("mflo", "$t2");
            break;
        case mir::BinaryOperation::UnsignedDivide:
            instruction("divu", "$zero,$t1,$t3");
            instruction("mflo", "$t2");
            break;
        case mir::BinaryOperation::SignedRemainder:
            instruction("div", "$zero,$t1,$t3");
            instruction("mfhi", "$t2");
            break;
        case mir::BinaryOperation::UnsignedRemainder:
            instruction("divu", "$zero,$t1,$t3");
            instruction("mfhi", "$t2");
            break;
        case mir::BinaryOperation::ShiftLeft:
            instruction("sllv", "$t2,$t1,$t3");
            break;
        case mir::BinaryOperation::ShiftRightArithmetic:
            instruction("srav", "$t2,$t1,$t3");
            break;
        case mir::BinaryOperation::ShiftRightLogical:
            instruction("srlv", "$t2,$t1,$t3");
            break;
        default:
            diagnostics_.error(value.location,
                               "MIPS LL/SC loop cannot lower this atomic update");
            return;
        }
        break;
    }
    default:
        diagnostics_.error(value.location, "unknown MIPS atomic operation");
        return;
    }
    instruction("sc", "$t2,0($t0)");
    instruction("beq", "$t2,$zero," + retry);
    instruction("nop");
    instruction("sync");
    store_vreg(function, value.defs.front(), "t1", value.location);
}

void AssemblyEmitter::emit_phi_edge_copies(
    const machine::Function& function, machine::BlockId predecessor,
    machine::BlockId successor) {
    const auto found = std::find_if(
        function.blocks.begin(), function.blocks.end(),
        [&](const machine::Block& block) { return block.id == successor; });
    if (found == function.blocks.end()) return;
    for (const auto& phi : found->instructions) {
        if (phi.kind != machine::InstructionKind::Target ||
            phi.opcode != Opcode::Phi || phi.defs.empty()) {
            continue;
        }
        for (std::size_t index = 0; index + 1 < phi.operands.size();
             index += 2) {
            const auto* incoming =
                std::get_if<machine::BlockOperand>(&phi.operands[index]);
            const auto* source = std::get_if<machine::RegisterOperand>(
                &phi.operands[index + 1]);
            if (!incoming || !source || incoming->target != predecessor) {
                continue;
            }
            copy_vreg(function, phi.defs.front(), source->value,
                      phi.location);
            break;
        }
    }
}

void AssemblyEmitter::emit_target(const machine::Function& function,
                                  const machine::Instruction& value) {
    const auto opcode = decode_opcode(value.opcode);
    if (opcode == Opcode::Phi || opcode == Opcode::LifetimeStart ||
        opcode == Opcode::LifetimeEnd || opcode == Opcode::IntrinsicNoop) {
        return;
    }
    if (opcode == Opcode::Parameter || opcode == Opcode::Fparameter) {
        capture_parameter(function, value);
        return;
    }
    if (opcode == Opcode::Constant || opcode == Opcode::Fconstant ||
        opcode == Opcode::Patch) {
        if (value.defs.empty() || value.operands.empty()) return;
        const auto target = value.defs.front();
        const auto& immediate =
            std::get<machine::ImmediateOperand>(value.operands.front());
        if (opcode == Opcode::Patch) {
            if (!value.patch || target.mode.bits > 64) {
                diagnostics_.error(value.location,
                                   "MIPS patch field must be a scalar no wider than 64 bits");
                return;
            }
            const auto bytes = std::max(1U, (target.mode.bits + 7U) / 8U);
            const auto after = local_label(function);
            const auto end = ".Lcross.patch.value." +
                             std::to_string(value.patch->identity) + ".end";
            instruction("b", after);
            instruction("nop");
            output_ << ".p2align " << std::countr_zero(std::bit_ceil(bytes))
                    << '\n';
            if (bytes == 1) output_ << "\t.byte " << immediate.value << '\n';
            else if (bytes == 2) output_ << "\t.half " << immediate.value << '\n';
            else if (bytes == 4) output_ << "\t.word " << immediate.value << '\n';
            else output_ << "\t.dword " << immediate.value << '\n';
            output_ << end << ":\n" << after << ":\n";
            const auto field = end + "-" + std::to_string(bytes);
            instruction("lui", "$t0,%hi(" + field + ")");
            instruction("addiu", "$t0,$t0,%lo(" + field + ")");
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, "t0");
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                load_integer_memory("t1", "0($t0)", target.mode.bits,
                                    false);
                store_vreg(function, target, "t1", value.location);
            }
            return;
        }
        if (legalizes_to_pair(target)) {
            const auto low = static_cast<std::uint32_t>(immediate.value);
            const auto high = static_cast<std::uint32_t>(
                immediate.value >> 32U);
            instruction("li", "$t0," + std::to_string(low));
            instruction("li", "$t1," + std::to_string(high));
            store_vreg_pair(function, target, "t0", "t1", value.location);
            return;
        }
        instruction(target.mode.bits > 32 ? "dli" : "li",
                    "$t0," + std::to_string(immediate.value));
        if (opcode == Opcode::Fconstant) {
            store_integer_memory(
                "t0", memory(vreg_offset(function, target, value.location)),
                target.mode.bits);
        } else {
            store_vreg(function, target, "t0", value.location);
        }
        return;
    }
    if (opcode == Opcode::StackAddress) {
        const auto& slot =
            std::get<machine::StackSlotOperand>(value.operands.front());
        const auto offset = slot_offset(function, slot.slot, value.location) +
                            slot.offset;
        instruction("addiu", "$t0,$fp," + std::to_string(offset));
        store_vreg(function, value.defs.front(), "t0", value.location);
        return;
    }
    if (opcode == Opcode::GlobalAddress || opcode == Opcode::GlobalLoadSigned ||
        opcode == Opcode::GlobalLoadUnsigned || opcode == Opcode::FglobalLoad ||
        opcode == Opcode::GlobalStore || opcode == Opcode::FglobalStore) {
        const auto& symbol =
            std::get<machine::SymbolOperand>(value.operands.front());
        const auto name = assembly_symbol(symbol.name);
        instruction("lui", "$t0,%hi(" + name + ")");
        instruction("addiu", "$t0,$t0,%lo(" + name + ")");
        if (opcode == Opcode::GlobalAddress) {
            store_vreg(function, value.defs.front(), "t0", value.location);
        } else if (opcode == Opcode::GlobalLoadSigned ||
                   opcode == Opcode::GlobalLoadUnsigned) {
            const auto target = value.defs.front();
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, "t0");
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                load_integer_memory("t1", "0($t0)", target.mode.bits,
                                    opcode == Opcode::GlobalLoadSigned);
                store_vreg(function, target, "t1", value.location);
            }
        } else if (opcode == Opcode::FglobalLoad) {
            const auto target = value.defs.front();
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        "$f0,0($t0)");
            store_fvreg(function, target, "f0", value.location);
        } else {
            const auto source = value.uses.front();
            if (opcode == Opcode::FglobalStore) {
                load_fvreg(function, source, "f0", value.location);
                instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                            "$f0,0($t0)");
            } else {
                if (legalizes_to_pair(source)) {
                    load_vreg_pair(function, source, "t1", "t2",
                                   value.location);
                    store_pair_memory("t1", "t2", 0, "t0");
                } else {
                    load_vreg(function, source, "t1", value.location);
                    store_integer_memory("t1", "0($t0)", source.mode.bits);
                }
            }
        }
        return;
    }
    if (opcode == Opcode::LabelAddress) {
        const auto& block =
            std::get<machine::BlockOperand>(value.operands.front());
        const auto label = block_label(function, block.target);
        instruction("lui", "$t0,%hi(" + label + ")");
        instruction("addiu", "$t0,$t0,%lo(" + label + ")");
        store_vreg(function, value.defs.front(), "t0", value.location);
        return;
    }
    if (opcode == Opcode::IndexedAddress) {
        load_vreg(function, value.uses[0], "t0", value.location);
        load_vreg(function, value.uses[1], "t1", value.location);
        const auto scale = static_cast<unsigned>(
            std::get<machine::ImmediateOperand>(value.operands.back()).value);
        if (scale != 1) {
            if (std::has_single_bit(scale)) {
                instruction("sll", "$t1,$t1," +
                                       std::to_string(std::countr_zero(scale)));
            } else {
                instruction("li", "$t2," + std::to_string(scale));
                instruction("multu", "$t1,$t2");
                instruction("mflo", "$t1");
            }
        }
        instruction("addu", "$t0,$t0,$t1");
        store_vreg(function, value.defs.front(), "t0", value.location);
        return;
    }
    if (opcode == Opcode::LoadSigned || opcode == Opcode::LoadUnsigned ||
        opcode == Opcode::Fload || opcode == Opcode::Store ||
        opcode == Opcode::Fstore) {
        const auto& slot =
            std::get<machine::StackSlotOperand>(value.operands.front());
        const auto offset = slot_offset(function, slot.slot, value.location) +
                            slot.offset;
        if (opcode == Opcode::LoadSigned || opcode == Opcode::LoadUnsigned) {
            const auto target = value.defs.front();
            if (legalizes_to_pair(target)) {
                load_pair_memory("t0", "t1", offset);
                store_vreg_pair(function, target, "t0", "t1",
                                value.location);
            } else {
                load_integer_memory("t0", memory(offset), target.mode.bits,
                                    opcode == Opcode::LoadSigned);
                store_vreg(function, target, "t0", value.location);
            }
        } else if (opcode == Opcode::Fload) {
            const auto target = value.defs.front();
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        "$f0," + memory(offset));
            store_fvreg(function, target, "f0", value.location);
        } else if (opcode == Opcode::Fstore) {
            const auto source = value.uses.front();
            load_fvreg(function, source, "f0", value.location);
            instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                        "$f0," + memory(offset));
        } else {
            const auto source = value.uses.front();
            if (legalizes_to_pair(source)) {
                load_vreg_pair(function, source, "t0", "t1",
                               value.location);
                store_pair_memory("t0", "t1", offset);
            } else {
                load_vreg(function, source, "t0", value.location);
                store_integer_memory("t0", memory(offset), source.mode.bits);
            }
        }
        return;
    }
    if (opcode == Opcode::PointerLoadSigned ||
        opcode == Opcode::PointerLoadUnsigned ||
        opcode == Opcode::FpointerLoad || opcode == Opcode::PointerStore ||
        opcode == Opcode::FpointerStore) {
        load_vreg(function, value.uses.front(), "t0", value.location);
        if (opcode == Opcode::PointerLoadSigned ||
            opcode == Opcode::PointerLoadUnsigned) {
            const auto target = value.defs.front();
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, "t0");
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                load_integer_memory("t1", "0($t0)", target.mode.bits,
                                    opcode == Opcode::PointerLoadSigned);
                store_vreg(function, target, "t1", value.location);
            }
        } else if (opcode == Opcode::FpointerLoad) {
            const auto target = value.defs.front();
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        "$f0,0($t0)");
            store_fvreg(function, target, "f0", value.location);
        } else if (opcode == Opcode::FpointerStore) {
            const auto source = value.uses[1];
            load_fvreg(function, source, "f0", value.location);
            instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                        "$f0,0($t0)");
        } else {
            const auto source = value.uses[1];
            if (legalizes_to_pair(source)) {
                load_vreg_pair(function, source, "t1", "t2",
                               value.location);
                store_pair_memory("t1", "t2", 0, "t0");
            } else {
                load_vreg(function, source, "t1", value.location);
                store_integer_memory("t1", "0($t0)", source.mode.bits);
            }
        }
        return;
    }
    if (opcode == Opcode::IndexedLoadSigned ||
        opcode == Opcode::IndexedLoadUnsigned ||
        opcode == Opcode::FindexedLoad) {
        load_vreg(function, value.uses[0], "t0", value.location);
        load_vreg(function, value.uses[1], "t1", value.location);
        const auto scale = static_cast<unsigned>(
            std::get<machine::ImmediateOperand>(value.operands.back()).value);
        if (scale != 1) {
            if (std::has_single_bit(scale)) {
                instruction("sll", "$t1,$t1," +
                                       std::to_string(std::countr_zero(scale)));
            } else {
                instruction("li", "$t2," + std::to_string(scale));
                instruction("multu", "$t1,$t2");
                instruction("mflo", "$t1");
            }
        }
        instruction("addu", "$t0,$t0,$t1");
        const auto target = value.defs.front();
        if (opcode == Opcode::FindexedLoad) {
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        "$f0,0($t0)");
            store_fvreg(function, target, "f0", value.location);
        } else {
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, "t0");
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                load_integer_memory("t1", "0($t0)", target.mode.bits,
                                    opcode == Opcode::IndexedLoadSigned);
                store_vreg(function, target, "t1", value.location);
            }
        }
        return;
    }
    if (opcode == Opcode::Neg || opcode == Opcode::Not ||
        opcode == Opcode::Iszero) {
        const auto source = value.uses.front();
        if (legalizes_to_pair(source)) {
            load_vreg_pair(function, source, "t0", "t1", value.location);
            if (opcode == Opcode::Neg) {
                instruction("subu", "$t2,$zero,$t0");
                instruction("sltu", "$t4,$zero,$t2");
                instruction("subu", "$t3,$zero,$t1");
                instruction("subu", "$t3,$t3,$t4");
                store_vreg_pair(function, value.defs.front(), "t2", "t3",
                                value.location);
            } else if (opcode == Opcode::Not) {
                instruction("nor", "$t2,$t0,$zero");
                instruction("nor", "$t3,$t1,$zero");
                store_vreg_pair(function, value.defs.front(), "t2", "t3",
                                value.location);
            } else {
                instruction("or", "$t2,$t0,$t1");
                instruction("sltiu", "$t2,$t2,1");
                store_vreg(function, value.defs.front(), "t2",
                           value.location);
            }
            return;
        }
        load_vreg(function, source, "t0", value.location);
        if (opcode == Opcode::Neg) {
            instruction(source.mode.bits > 32 ? "dsubu" : "subu",
                        "$t0,$zero,$t0");
        } else if (opcode == Opcode::Not) {
            instruction("nor", "$t0,$t0,$zero");
        } else {
            instruction("sltiu", "$t0,$t0,1");
        }
        store_vreg(function, value.defs.front(), "t0", value.location);
        return;
    }
    if (opcode == Opcode::Fneg || opcode == Opcode::Fiszero) {
        const auto source = value.uses.front();
        const std::string suffix = source.mode.bits == 32 ? ".s" : ".d";
        load_fvreg(function, source, "f0", value.location);
        if (opcode == Opcode::Fneg) {
            instruction("neg" + suffix, "$f2,$f0");
            store_fvreg(function, value.defs.front(), "f2", value.location);
        } else {
            if (source.mode.bits == 32) instruction("mtc1", "$zero,$f2");
            else if (subtarget_.has_feature(Feature::Mips3)) {
                instruction("dmtc1", "$zero,$f2");
            } else {
                instruction("mtc1", "$zero,$f2");
                instruction("mtc1", "$zero,$f3");
            }
            instruction("c.eq" + suffix, "$f0,$f2");
            const auto yes = local_label(function);
            const auto done = local_label(function);
            instruction("bc1t", yes);
            instruction("move", "$t0,$zero");
            instruction("b", done);
            instruction("nop");
            output_ << yes << ":\n";
            instruction("li", "$t0,1");
            output_ << done << ":\n";
            store_vreg(function, value.defs.front(), "t0", value.location);
        }
        return;
    }
    if ((opcode >= Opcode::Add && opcode <= Opcode::CmpUge)) {
        emit_integer_binary(function, value);
        return;
    }
    if (opcode >= Opcode::Fadd && opcode <= Opcode::FcmpGe) {
        emit_floating_binary(function, value);
        return;
    }
    if (opcode >= Opcode::Sext && opcode <= Opcode::Fptoui) {
        emit_cast(function, value);
        return;
    }
    if (opcode == Opcode::Select) {
        if (value.uses.size() < 3 || value.defs.empty()) return;
        if (value.defs.front().mode.bits <= 32 &&
            subtarget_.has_feature(Feature::CondMove)) {
            load_vreg(function, value.uses[0], "t0", value.location);
            load_vreg(function, value.uses[1], "t1", value.location);
            load_vreg(function, value.uses[2], "t2", value.location);
            // Allegrex uses the MIPS conditional-move operand layout even
            // though generic MIPS-II assemblers do not recognize its mnemonic.
            encoded(0x0128500bU, "movn $t2,$t1,$t0");
            store_vreg(function, value.defs.front(), "t2", value.location);
            return;
        }
        const auto otherwise = local_label(function);
        const auto done = local_label(function);
        load_vreg(function, value.uses[0], "t0", value.location);
        instruction("beq", "$t0,$zero," + otherwise);
        instruction("nop");
        copy_vreg(function, value.defs.front(), value.uses[1], value.location);
        instruction("b", done);
        instruction("nop");
        output_ << otherwise << ":\n";
        copy_vreg(function, value.defs.front(), value.uses[2], value.location);
        output_ << done << ":\n";
        return;
    }
    if (opcode == Opcode::Expect) {
        if (!value.uses.empty() && !value.defs.empty()) {
            copy_vreg(function, value.defs.front(), value.uses.front(),
                      value.location);
        }
        return;
    }
    if (opcode >= Opcode::AtomicLoad &&
        opcode <= Opcode::AtomicSignalFence) {
        emit_atomic(function, value);
        return;
    }
    if (opcode == Opcode::VariadicState || opcode == Opcode::StackSave ||
        opcode == Opcode::StackAllocate || opcode == Opcode::StackRestore ||
        opcode == Opcode::Invalid) {
        diagnostics_.error(value.location,
                           "unsupported operation reached the MIPS assembly emitter");
        return;
    }
    diagnostics_.error(value.location,
                       "unknown MIPS target opcode in assembly emission");
}

void AssemblyEmitter::emit_terminator(
    const machine::Function& function,
    const machine::Instruction& value,
    machine::BlockId predecessor) {
    if (value.kind == machine::InstructionKind::Return) {
        place_return(function, value);
        return;
    }
    if (value.kind == machine::InstructionKind::Branch) {
        const auto successor =
            std::get<machine::BlockOperand>(value.operands.front()).target;
        emit_phi_edge_copies(function, predecessor, successor);
        instruction("b", block_label(function, successor));
        instruction("nop");
        return;
    }
    if (value.kind == machine::InstructionKind::ConditionalBranch) {
        const auto condition = value.uses.front();
        const auto yes =
            std::get<machine::BlockOperand>(value.operands[1]).target;
        const auto no =
            std::get<machine::BlockOperand>(value.operands[2]).target;
        const auto yes_edge = local_label(function);
        load_vreg(function, condition, "t0", value.location);
        instruction("bne", "$t0,$zero," + yes_edge);
        instruction("nop");
        emit_phi_edge_copies(function, predecessor, no);
        instruction("b", block_label(function, no));
        instruction("nop");
        output_ << yes_edge << ":\n";
        emit_phi_edge_copies(function, predecessor, yes);
        instruction("b", block_label(function, yes));
        instruction("nop");
        return;
    }
    if (value.kind == machine::InstructionKind::IndirectBranch) {
        for (std::size_t index = 1; index < value.operands.size(); ++index) {
            const auto* successor =
                std::get_if<machine::BlockOperand>(&value.operands[index]);
            if (!successor) continue;
            const auto found = std::find_if(
                function.blocks.begin(), function.blocks.end(),
                [&](const machine::Block& block) {
                    return block.id == successor->target;
                });
            if (found != function.blocks.end() &&
                std::any_of(found->instructions.begin(),
                            found->instructions.end(),
                            [](const machine::Instruction& instruction) {
                                return instruction.opcode == Opcode::Phi;
                            })) {
                diagnostics_.error(
                    value.location,
                    "MIPS indirect branch into a phi block is not implemented yet");
                return;
            }
        }
        load_vreg(function, value.uses.front(), "t0", value.location);
        instruction("jr", "$t0");
        instruction("nop");
        return;
    }
    if (value.kind == machine::InstructionKind::Trap) {
        instruction("break", "7");
        return;
    }
    // `unreachable` deliberately emits no hidden trap: reaching it is outside
    // the source contract, whereas the explicit trap intrinsic uses BREAK.
}

void AssemblyEmitter::emit_function(machine::Function& function) {
    const auto& entity = hir_.function(function.source);
    active_signature_ = classify_entity(entity, function.location);
    if (!active_signature_) return;
    // ABI register banks can overlap the emitter's scratch registers (EABI32
    // deliberately continues through t0-t3).  Home every incoming register
    // before parameter materialization so capturing an early argument cannot
    // destroy a later one.
    prepare_parameter_homes(function);
    if (!finalize_frame(function)) return;
    if (!safe_assembly_text(function.symbol) ||
        (entity.section && !safe_assembly_text(*entity.section))) {
        diagnostics_.error(function.location,
                           "MIPS symbol or section is not representable by the assembler");
        return;
    }
    const auto symbol = assembly_symbol(function.symbol);
    const auto section = entity.section
        ? *entity.section
        : options_.function_sections ? ".text." + function.symbol
                                     : ".text";
    std::string section_error;
    const auto directive = assembly_section_directive(
        format_, {section, AssemblySectionKind::Code,
                  entity.section.has_value(), false},
        section_error);
    if (!directive) {
        diagnostics_.error(function.location, section_error);
        return;
    }
    output_ << *directive << '\n';
    // ISA directives are scoped so llvm-mc's command-line ABI features (for
    // example +single-float) are restored before it finalizes .MIPS.abiflags.
    output_ << ".set push\n.set noreorder\n.set noat\n.option pic0\n";
    if (subtarget_.has_feature(Feature::Mips5)) output_ << ".set mips5\n";
    else if (subtarget_.has_feature(Feature::Mips4)) output_ << ".set mips4\n";
    else if (subtarget_.has_feature(Feature::Mips3)) output_ << ".set mips3\n";
    else if (subtarget_.has_feature(Feature::Mips2)) output_ << ".set mips2\n";
    else output_ << ".set mips1\n";
    unsigned alignment_power = 2;
    if (options_.function_alignment != 0) {
        alignment_power = static_cast<unsigned>(
            std::countr_zero(options_.function_alignment));
    }
    output_ << ".p2align " << alignment_power << '\n';
    if (entity.linkage == Linkage::Global) {
        output_ << ".globl " << symbol << '\n';
    } else {
        output_ << ".local " << symbol << '\n';
    }
    output_ << ".type " << symbol << ",@function\n"
            << ".ent " << symbol << '\n' << symbol << ":\n"
            << "\t.frame\t$fp," << frame_size_ << ",$ra\n";
    const bool cfi = (options_.unwind_tables ||
                      options_.asynchronous_unwind_tables) &&
                     assembly_uses_dwarf_cfi(format_);
    if (cfi) output_ << ".cfi_startproc\n";
    instruction("addiu", "$sp,$sp,-" + std::to_string(frame_size_));
    if (cfi) output_ << ".cfi_def_cfa_offset " << frame_size_ << '\n';
    instruction("sw", "$ra," + memory(saved_ra_offset_, "sp"));
    instruction("sw", "$fp," + memory(saved_fp_offset_, "sp"));
    if (cfi) {
        output_ << ".cfi_offset 31,"
                << static_cast<std::int64_t>(saved_ra_offset_) - frame_size_
                << "\n.cfi_offset 30,"
                << static_cast<std::int64_t>(saved_fp_offset_) - frame_size_
                << '\n';
    }
    instruction("move", "$fp,$sp");
    if (cfi) output_ << ".cfi_def_cfa_register 30\n";
    emit_parameter_homes(function);
    epilogue_label_ = ".Lcross.mips." +
                      std::to_string(function.source.value) + ".return";

    for (const auto block_id : function.layout) {
        const auto found = std::find_if(
            function.blocks.begin(), function.blocks.end(),
            [&](const machine::Block& block) { return block.id == block_id; });
        if (found == function.blocks.end()) continue;
        output_ << block_label(function, found->id) << ":\n";
        for (const auto& label : function.labels) {
            if (label.block == found->id) {
                output_ << ".Lcross.label." << function.source.value << '.'
                        << label.label.value << ":\n";
            }
        }
        for (const auto& value : found->instructions) {
            if (value.kind == machine::InstructionKind::Target) {
                emit_target(function, value);
            } else if (value.kind == machine::InstructionKind::Call) {
                emit_call(function, value);
            } else {
                emit_terminator(function, value, found->id);
            }
        }
    }

    output_ << epilogue_label_ << ":\n";
    instruction("move", "$sp,$fp");
    instruction("lw", "$fp," + memory(saved_fp_offset_, "sp"));
    instruction("lw", "$ra," + memory(saved_ra_offset_, "sp"));
    instruction("addiu", "$sp,$sp," + std::to_string(frame_size_));
    instruction("jr", "$ra");
    instruction("nop");
    if (cfi) output_ << ".cfi_endproc\n";
    output_ << ".end " << symbol << "\n.size " << symbol << ",.-"
            << symbol << "\n.set reorder\n.set pop\n";
    active_signature_.reset();
}

} // namespace

std::string emit_managed_machine_assembly(
    machine::Module& module, const mir::ManagedModule& managed,
    const hir::Module& hir_module, const Subtarget& subtarget,
    const CompilerOptions& options, Diagnostics& diagnostics) {
    (void)managed;
    return AssemblyEmitter(module, hir_module, subtarget, options,
                           diagnostics).run();
}

} // namespace cross::mips
