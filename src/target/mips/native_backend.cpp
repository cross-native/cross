// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "target/mips/native_backend.hpp"

#include "backend/native/machine_pass.hpp"
#include "backend/native/machine_transform.hpp"
#include "common/control_flow.hpp"
#include "model/model.hpp"
#include "middle/mir_analysis.hpp"
#include "middle/shrink_wrap.hpp"
#include "target/abi_lowering.hpp"
#include "target/assembly_format.hpp"
#include "target/mips/features.hpp"
#include "target/mips/machine_description.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross::mips {
namespace {

AssemblySymbolVisibility assembly_visibility(
    hir::SymbolVisibility visibility) {
    switch (visibility) {
    case hir::SymbolVisibility::Default:
        return AssemblySymbolVisibility::Default;
    case hir::SymbolVisibility::Hidden:
        return AssemblySymbolVisibility::Hidden;
    case hir::SymbolVisibility::Protected:
        return AssemblySymbolVisibility::Protected;
    case hir::SymbolVisibility::Internal:
        return AssemblySymbolVisibility::Internal;
    }
    return AssemblySymbolVisibility::Default;
}

enum class LoweringPass : std::uint16_t {
    PropagateCopies,
    FoldBoundaryProjections,
    SelectIntegerImmediates,
    FoldPointerOffsets,
    EliminateRedundantExpressions,
    EliminateRedundantLoads,
    FuseDivisionResults,
    FuseCompareBranches,
    EliminateDeadValues,
    ScheduleBlockLayout,
    ScheduleInstructions,
    SplitEntryParameters,
    AllocateRegisters,
    ElideUnusedSpillSlots,
};

// Physical GPR identities use the architectural register numbers.  Machine
// IR keeps them numeric; this table is the single target-local conversion to
// assembly names and model ABI resources.
constexpr std::array<std::string_view, 32> gpr_names{
    "zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
constexpr std::array<std::string_view, 32> fpr_names{
    "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",
    "f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"};
constexpr std::uint32_t fpr_physical_base = 32;

std::optional<machine::PhysicalRegisterId> gpr_id(std::string_view name) {
    const auto found = std::find(gpr_names.begin(), gpr_names.end(), name);
    if (found == gpr_names.end()) return std::nullopt;
    return machine::PhysicalRegisterId{
        static_cast<std::uint32_t>(found - gpr_names.begin())};
}

std::string_view gpr_name(machine::PhysicalRegisterId id) {
    return id.value < gpr_names.size() ? gpr_names[id.value]
                                       : std::string_view{};
}

std::optional<machine::PhysicalRegisterId> fpr_id(std::string_view name) {
    const auto found = std::find(fpr_names.begin(), fpr_names.end(), name);
    if (found == fpr_names.end()) return std::nullopt;
    return machine::PhysicalRegisterId{
        fpr_physical_base +
        static_cast<std::uint32_t>(found - fpr_names.begin())};
}

std::string_view fpr_name(machine::PhysicalRegisterId id) {
    if (id.value < fpr_physical_base ||
        id.value >= fpr_physical_base + fpr_names.size()) {
        return {};
    }
    return fpr_names[id.value - fpr_physical_base];
}

std::optional<machine::PhysicalRegisterId> physical_register_id(
    std::string_view name) {
    if (const auto gpr = gpr_id(name)) return gpr;
    return fpr_id(name);
}

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

bool is_address_value(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Pointer ||
           (type.kind == hir::Type::Kind::Builtin &&
            type.builtin == BuiltinType::Label);
}

bool is_aggregate(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Record && type.record &&
           module.record(*type.record).complete;
}

bool is_vector(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Vector;
}

bool has_dynamic_stack(const machine::Function& function) {
    for (const auto& block : function.blocks) {
        for (const auto& instruction : block.instructions) {
            if (instruction.opcode == Opcode::StackSave ||
                instruction.opcode == Opcode::StackAllocate ||
                instruction.opcode == Opcode::StackRestore) return true;
        }
    }
    return false;
}

bool phi_instruction(const machine::Instruction& instruction) {
    return instruction.opcode == Opcode::Phi;
}

bool returns_twice_or_interrupt(const hir::Function& entity) {
    const auto special = [](const cross::FunctionDecl* declaration) {
        return declaration && (declaration->attribute("returns_twice") ||
                               declaration->attribute("interrupt"));
    };
    return special(entity.definition) ||
           std::any_of(entity.declarations.begin(), entity.declarations.end(),
                       special);
}

// Every argument and result piece travels in a register, so the interface
// uses no frame-relative incoming or result storage.
bool register_interface(const SignatureLayout& layout) {
    const auto in_register = [](const ValuePiece& piece) {
        return piece.location.kind == LocationKind::Register;
    };
    return std::all_of(layout.call.arguments.begin(),
                       layout.call.arguments.end(),
                       [&](const auto& assignment) {
                           return std::all_of(assignment.pieces.begin(),
                                              assignment.pieces.end(),
                                              in_register);
                       }) &&
           std::all_of(layout.results.begin(), layout.results.end(),
                       [&](const auto& result) {
                           return !result.indirect &&
                                  std::all_of(result.pieces.begin(),
                                              result.pieces.end(),
                                              in_register);
                       });
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
        std::optional<std::pair<std::uint64_t, unsigned>> bit_field_unit;
        for (const auto& member : record.members) {
            if (member.bit_width && *member.bit_width == 0) continue;
            if (member.bit_width && !record.is_union) {
                const auto unit = std::pair{
                    member.offset, type_bits(module, member.type)};
                if (bit_field_unit == unit) continue;
                bit_field_unit = unit;
            } else {
                bit_field_unit.reset();
            }
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

std::optional<SignatureLayout> classify_managed_interface(
    const hir::Module& module, const hir::Function& entity,
    const Subtarget& subtarget, const AbiEntry& abi) {
    std::vector<AbiValue> arguments;
    arguments.reserve(entity.parameters.size());
    for (const auto& parameter : entity.parameters) {
        arguments.push_back(abi_value_for(
            module, parameter.type, subtarget.target().data_layout, abi,
            parameter.mode == ParameterMode::In
                ? ValueTransport::Direct : ValueTransport::ByReference));
    }
    std::vector<AbiValue> results;
    if (!is_void(module, entity.result_type)) {
        results.push_back(abi_value_for(
            module, entity.result_type, subtarget.target().data_layout, abi));
    }
    auto classified = entity.variadic
        ? classify_variadic_signature(abi, arguments, results, entity.parameters.size(),
                                      subtarget.enabled_features())
        : classify_signature(abi, arguments, results, subtarget.enabled_features());
    if (!classified) return std::nullopt;
    return std::move(classified.layout);
}

const AbiEntry* managed_abi_model(const hir::Module& module,
                                  const hir::Function& function,
                                  const Subtarget& subtarget,
                                  const CompilerOptions& options) {
    if (options.private_abi &&
        function.abi_contract == hir::AbiContract::Dynamic) {
        // The model marks one candidate per address model and carrier width.
        // Its address width must match because the shared ABI interpreter
        // sizes pointer and by-reference channels from it; carriers follow
        // the native GPR width.
        const unsigned carrier_bits =
            subtarget.has_feature(Feature::Mips3) ? 64U : 32U;
        const auto& compilation = subtarget.abi_info();
        for (const auto& abi : model_registry().abis()) {
            if (abi.architecture != compilation.architecture ||
                abi.address_bits != compilation.address_bits ||
                abi.private_carrier_bits != carrier_bits) {
                continue;
            }
            // Private transport is an optimization, not permission to
            // reject a signature supported by its resolved source ABI.
            // Classify the complete interface (including hidden result
            // channels) before committing, using the same model and
            // resolved ISA facts at caller and callee.
            if (classify_managed_interface(module, function, subtarget, abi))
                return &abi;
            break;
        }
    }
    return abi_model(function.abi);
}

unsigned effective_piece_offset(const ValuePiece& piece,
                                unsigned value_bits, ByteOrder order) {
    if (order == ByteOrder::Little || piece.value_bits >= value_bits) {
        return piece.value_bit_offset;
    }
    const auto used = static_cast<unsigned>(piece.value_bit_offset) +
                      piece.value_bits;
    return used <= value_bits ? value_bits - used
                              : piece.value_bit_offset;
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
        std::vector<std::size_t> order;
        order.reserve(managed_.functions.size());
        if (options_.ipa_ra && options_.private_abi) {
            std::unordered_map<std::uint32_t, std::size_t> indices;
            for (std::size_t index = 0; index < managed_.functions.size();
                 ++index) {
                indices.emplace(
                    managed_.functions[index].source.value, index);
            }
            std::vector<unsigned char> state(managed_.functions.size());
            std::function<void(std::size_t)> visit =
                [&](std::size_t index) {
                    if (state[index] != 0) return;
                    state[index] = 1;
                    for (const auto& value :
                         managed_.functions[index].values) {
                        if (value.kind != mir::ValueKind::Call ||
                            !value.callee) {
                            continue;
                        }
                        const auto found = indices.find(value.callee->value);
                        if (found != indices.end() &&
                            state[found->second] == 0) {
                            visit(found->second);
                        }
                    }
                    state[index] = 2;
                    order.push_back(index);
                };
            for (std::size_t index = 0; index < managed_.functions.size();
                 ++index) {
                visit(index);
            }
        } else {
            for (std::size_t index = 0; index < managed_.functions.size();
                 ++index) {
                order.push_back(index);
            }
        }

        std::vector<std::optional<machine::Function>> lowered(
            managed_.functions.size());
        for (const auto index : order) {
            lowered[index] = lower_function(managed_.functions[index]);
        }
        for (auto& function : lowered) {
            if (function) {
                result_.functions.push_back(std::move(*function));
            }
        }
        return std::move(result_);
    }

private:
    bool lower_xor_zero_extend_return(
        mir::ValueId returned_id, machine::Instruction& result) const {
        if (!options_.machine_combine ||
            !subtarget_.has_feature(Feature::Mips3) || !source_ ||
            returned_id.value >= source_->values.size()) {
            return false;
        }
        const auto& returned = source_->values[returned_id.value];
        if (returned.kind != mir::ValueKind::Binary ||
            returned.binary != mir::BinaryOperation::BitXor ||
            returned.operands.size() != 2 ||
            type_bits(hir_, returned.type) != 64) {
            return false;
        }

        std::optional<mir::ValueId> wide;
        std::optional<mir::ValueId> narrow;
        for (unsigned index = 0; index < 2; ++index) {
            const auto candidate = returned.operands[index];
            if (candidate.value >= source_->values.size()) return false;
            const auto& extension = source_->values[candidate.value];
            if (extension.kind != mir::ValueKind::Cast ||
                extension.cast != mir::CastOperation::ZeroExtend ||
                extension.operands.size() != 1 ||
                type_bits(hir_, extension.type) != 64 ||
                extension.operands.front().value >=
                    source_->values.size() ||
                type_bits(hir_, source_->values[
                                    extension.operands.front().value]
                                    .type) != 32) {
                continue;
            }
            const auto other = returned.operands[1U - index];
            if (other.value >= source_->values.size() ||
                type_bits(hir_, source_->values[other.value].type) != 64) {
                return false;
            }
            wide = other;
            narrow = extension.operands.front();
            break;
        }
        if (!wide || !narrow) return false;

        const auto& entity = hir_.function(source_->source);
        const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
        const auto signature = abi
            ? classify_function_interface(entity, *abi)
            : std::optional<SignatureLayout>{};
        if (!signature || signature->results.size() != 1) return false;
        const auto& assignment = signature->results.front();
        if (assignment.indirect || assignment.pieces.size() != 2) {
            return false;
        }
        bool low = false;
        bool high = false;
        std::string_view first_register;
        for (const auto& piece : assignment.pieces) {
            if (piece.location.kind != LocationKind::Register ||
                piece.value_bits != 32 || piece.carrier_bits != 32 ||
                piece.location.reg.empty() ||
                piece.location.reg == first_register) {
                return false;
            }
            if (first_register.empty()) first_register = piece.location.reg;
            const auto offset = effective_piece_offset(
                piece, 64,
                subtarget_.target().data_layout.byte_order);
            if (offset == 0) low = true;
            else if (offset == 32) high = true;
            else return false;
        }
        if (!low || !high) return false;

        const auto wide_register = reg(*wide);
        const auto narrow_register = reg(*narrow);
        result.operands = {register_operand(wide_register),
                           register_operand(narrow_register)};
        result.uses = {wide_register, narrow_register};
        result.result_composition =
            machine::ResultComposition::XorZeroExtend;
        return true;
    }

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
            if (is_aggregate(hir_, value.type) &&
                type_bits(hir_, value.type) >
                    std::numeric_limits<std::uint16_t>::max()) {
                unsupported(value,
                            "aggregate SSA values larger than 8191 bytes");
            }
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
            if ((!is_aggregate(hir_, value.type) && mode.bits > 64) ||
                is_vector(hir_, value.type)) {
                unsupported(value, mode.bits > 64
                                       ? "scalar values wider than 64 bits"
                                       : "vector SSA values");
            }
            const machine::VirtualRegisterId id{
                static_cast<std::uint32_t>(current_.virtual_registers.size())};
            current_.virtual_registers.push_back(mode);
            current_.virtual_register_assignments.push_back(std::nullopt);
            current_.rematerialized_immediates.push_back(std::nullopt);
            current_.virtual_register_classes.push_back(
                is_aggregate(hir_, value.type)
                    ? machine::VirtualRegisterClass::Memory
                : is_floating(hir_, value.type)
                    ? machine::VirtualRegisterClass::Floating
                    : machine::VirtualRegisterClass::Integer);
            value_registers_[value.id.value] = id;
        }
    }

    bool unaligned_pointer_access(const mir::ManagedValue& value) const {
        if ((value.kind != mir::ValueKind::PointerLoad &&
             value.kind != mir::ValueKind::PointerStore) ||
            value.memory_alignment == 0)
            return false;
        const auto type =
            value.kind == mir::ValueKind::PointerLoad
                ? value.type
                : source_->values[value.operands.back().value].type;
        return !is_aggregate(hir_, type) &&
               value.memory_alignment <
                   storage_size(hir_, type, subtarget_.target().data_layout);
    }

    void create_stack_slots(const mir::ManagedFunction& source) {
        const auto& layout = subtarget_.target().data_layout;
        for (const auto& slot : source.slots) {
            machine::StackSlot target;
            target.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            target.kind = machine::StackSlotKind::Local;
            target.size = storage_size(hir_, slot.type, layout);
            target.alignment =
                std::max(storage_alignment(hir_, slot.type, layout),
                         slot.minimum_alignment);
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

        if (std::any_of(source.values.begin(), source.values.end(),
                        [](const mir::ManagedValue& value) {
                            return value.kind == mir::ValueKind::DynamicAlloca;
                        }) &&
            std::any_of(source.values.begin(), source.values.end(),
                        [](const mir::ManagedValue& value) {
                            return value.kind == mir::ValueKind::Call;
                        })) {
            machine::StackSlot mark;
            mark.id = {static_cast<std::uint32_t>(current_.stack_slots.size())};
            mark.kind = machine::StackSlotKind::Local;
            mark.size = (hir_.address_bits + 7U) / 8U;
            mark.alignment = mark.size;
            mark.location = source.location;
            mark.name = "$dynamic.call.sp";
            current_.stack_slots.push_back(std::move(mark));
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
            const bool aggregate = current_.virtual_register_classes[index] ==
                                   machine::VirtualRegisterClass::Memory;
            spill.size = aggregate        ? (mode.bits + 7U) / 8U
                         : floating       ? std::max(4U, (mode.bits + 7U) / 8U)
                         : mode.bits > 32 ? 8U
                                          : 4U;
            spill.alignment = spill.size >= 8 ? 8U : 4U;
            spill.location = source.location;
            spill.name = "$v" + std::to_string(index);
            spill.spill_for = machine::VirtualRegisterId{
                static_cast<std::uint32_t>(index)};
            current_.stack_slots.push_back(std::move(spill));
        }
        // Parallel aggregate PHI copies first snapshot their sources. Each
        // destination owns a separate cell, so cycles never overwrite a source.
        for (const auto& value : source.values) {
            if (value.kind != mir::ValueKind::Phi ||
                !is_aggregate(hir_, value.type))
                continue;
            machine::StackSlot temporary;
            temporary.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            temporary.kind = machine::StackSlotKind::Local;
            temporary.size = storage_size(hir_, value.type, layout);
            temporary.alignment = storage_alignment(hir_, value.type, layout);
            temporary.location = value.location;
            temporary.name =
                "$aggregate.phi." + std::to_string(reg(value.id).id);
            current_.stack_slots.push_back(std::move(temporary));
        }
        if (std::find(current_.virtual_register_classes.begin(),
                      current_.virtual_register_classes.end(),
                      machine::VirtualRegisterClass::Memory) !=
                current_.virtual_register_classes.end() ||
            std::any_of(source.values.begin(), source.values.end(),
                        [this](const mir::ManagedValue& value) {
                            return unaligned_pointer_access(value);
                        })) {
            machine::StackSlot carrier;
            carrier.id = {
                static_cast<std::uint32_t>(current_.stack_slots.size())};
            carrier.kind = machine::StackSlotKind::Local;
            carrier.size = 8;
            carrier.alignment = 8;
            carrier.location = source.location;
            carrier.name = "$aggregate.carrier";
            current_.stack_slots.push_back(std::move(carrier));
        }
    }

    bool elide_noreturn_saves(const hir::Function& entity) const {
        if (!options_.elide_noreturn_saves ||
            options_.unwind_model != UnwindModel::None ||
            options_.unwind_tables || options_.asynchronous_unwind_tables ||
            entity.naked || returns_twice_or_interrupt(entity)) {
            return false;
        }
        return source_ && !mir::has_reachable_return(*source_);
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
        const bool aggregate_load = is_aggregate(hir_, value.type) &&
                                    (value.kind == ValueKind::Load ||
                                     value.kind == ValueKind::PointerLoad ||
                                     value.kind == ValueKind::GlobalLoad ||
                                     value.kind == ValueKind::IndexedLoad ||
                                     (value.kind == ValueKind::Cast &&
                                      value.cast == mir::CastOperation::Reinterpret));
        const bool store = value.kind == ValueKind::Store ||
                           value.kind == ValueKind::PointerStore ||
                           value.kind == ValueKind::GlobalStore;
        const bool aggregate_store =
            store && !value.operands.empty() &&
            is_aggregate(hir_,
                         source_->values[value.operands.back().value].type);
        if (aggregate_load || aggregate_store) {
            auto result =
                target_instruction(aggregate_store ? Opcode::AggregateStore
                                   : value.kind == ValueKind::IndexedLoad
                                       ? Opcode::AggregateIndexedLoad
                                       : Opcode::AggregateLoad,
                                   value.location);
            if (value.slot) {
                result.operands.push_back(
                    stack_operand({value.slot->value}, machine::i8));
            } else if (value.object) {
                result.operands.push_back(machine::SymbolOperand{
                    hir_.object(*value.object).link_symbol, 0, false,
                    value.object});
            }
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            if (value.kind == ValueKind::IndexedLoad) {
                result.operands.push_back(immediate_operand(
                    storage_size(hir_, value.type,
                                 subtarget_.target().data_layout),
                    0, machine::i32));
            }
            if (aggregate_load) result.defs.push_back(reg(value.id));
            // Both directions read and write storage, including the SSA home.
            result.may_load = true;
            result.may_store = true;
            result.has_side_effects =
                aggregate_store || value.is_volatile_access;
            for (const auto id : {1U, 8U}) {
                result.clobbers.push_back(
                    machine::Register::physical_register({id}, machine::i64));
            }
            if (value.kind == ValueKind::IndexedLoad) {
                for (const auto id : {9U, 10U, 11U}) {
                    result.clobbers.push_back(
                        machine::Register::physical_register({id},
                                                             machine::i64));
                }
            }
            return result;
        }
        if (unaligned_pointer_access(value)) {
            const bool load = value.kind == ValueKind::PointerLoad;
            const auto type =
                load ? value.type
                     : source_->values[value.operands.back().value].type;
            auto result = target_instruction(
                !load                           ? Opcode::UnalignedStore
                : is_signed_integer(hir_, type) ? Opcode::UnalignedLoadSigned
                                                : Opcode::UnalignedLoadUnsigned,
                value.location);
            for (const auto operand : value.operands) {
                const auto source = reg(operand);
                result.operands.push_back(register_operand(source));
                result.uses.push_back(source);
            }
            if (load) result.defs.push_back(reg(value.id));
            result.may_load = true;
            result.may_store = true;
            result.has_side_effects = !load || value.is_volatile_access;
            for (const auto id :
                 {1U, 8U, 9U, 10U, fpr_physical_base, fpr_physical_base + 1U}) {
                result.clobbers.push_back(
                    machine::Register::physical_register({id}, machine::i64));
            }
            return result;
        }
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
            const auto* entity = value.label
                ? &hir_.labels.at(value.label->value)
                : nullptr;
            const bool symbolic = entity &&
                (entity->is_global || entity->owner != source_->source);
            auto result = target_instruction(
                symbolic ? Opcode::GlobalAddress : Opcode::LabelAddress,
                value.location);
            if (symbolic) {
                const auto symbol = entity->is_global ? entity->link_symbol
                    : ".Lcross.label." + std::to_string(entity->owner.value) +
                      '.' + std::to_string(entity->id.value);
                result.operands.push_back(machine::SymbolOperand{
                    symbol, 0, true, std::nullopt, entity->owner, entity->id});
                result.defs.push_back(reg(value.id));
                return result;
            }
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
        if (value.kind == ValueKind::FunctionAddress) {
            auto result =
                target_instruction(Opcode::GlobalAddress, value.location);
            result.operands.push_back(
                machine::SymbolOperand{hir_.function(*value.callee).link_symbol,
                                       0, true, std::nullopt, value.callee});
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
            if (value.kind == ValueKind::DynamicAlloca) {
                result.operands.push_back(immediate_operand(
                    value.integer, 0, machine::i64,
                    is_signed_integer(hir_, source_->values[value.operands.front().value].type)));
                result.operands.push_back(immediate_operand(value.integer_high, 0, machine::i64));
                result.may_store = true;
            }
            result.has_side_effects = true;
            for (const auto id : {1U, 8U, 9U, 10U}) {
                result.clobbers.push_back(machine::Register::physical_register({id}, machine::i64));
            }
            if (value.kind != ValueKind::DynamicStackSave) {
                result.clobbers.push_back(machine::Register::physical_register(
                    {29}, {static_cast<std::uint16_t>(hir_.address_bits)}));
            }
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
            const auto callee =
                *hir::call_signature(hir_, value.callee, value.call_signature);
            result.direct_callee = value.callee;
            result.call_signature = value.call_signature;
            result.must_tail = value.must_tail;
            if (value.callee) {
                result.operands.push_back(machine::SymbolOperand{
                    hir_.function(*value.callee).link_symbol, 0, true,
                    std::nullopt});
            } else {
                const auto target = reg(value.operands.front());
                result.operands.push_back(register_operand(target));
                result.uses.push_back(target);
            }
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
            const auto append_clobbers =
                [&](const std::vector<std::string>& names) {
                    for (const auto& name : names) {
                        const auto physical = physical_register_id(name);
                        if (!physical || std::any_of(
                                result.clobbers.begin(),
                                result.clobbers.end(),
                                [&](const machine::Register& present) {
                                    return present.kind ==
                                               machine::RegisterKind::Physical &&
                                           present.id == physical->value;
                                })) {
                            continue;
                        }
                        result.clobbers.push_back(
                            machine::Register::physical_register(
                                *physical, machine::i64));
                    }
                };
            const auto append_physical_clobbers =
                [&](const std::vector<machine::PhysicalRegisterId>& ids) {
                    for (const auto physical : ids) {
                        if (std::any_of(
                                result.clobbers.begin(),
                                result.clobbers.end(),
                                [&](const machine::Register& present) {
                                    return present.kind ==
                                               machine::RegisterKind::Physical &&
                                           present.id == physical.value;
                                })) {
                            continue;
                        }
                        result.clobbers.push_back(
                            machine::Register::physical_register(
                                physical, machine::i64));
                    }
                };
            const auto* entity =
                value.callee ? &hir_.function(*value.callee) : nullptr;
            if (const auto* abi =
                    entity ? managed_abi_model(hir_, *entity, subtarget_, options_)
                           : abi_model(callee.abi)) {
                const auto exact =
                    entity && options_.ipa_ra && options_.private_abi &&
                            entity->abi_contract == hir::AbiContract::Dynamic
                        ? private_clobbers_.find(entity->id.value)
                        : private_clobbers_.end();
                if (exact != private_clobbers_.end()) {
                    append_physical_clobbers(exact->second);
                } else {
                    append_clobbers(abi->call_clobbers);
                }
                std::vector<AbiValue> arguments;
                arguments.reserve(value.call_arguments.size());
                for (std::size_t index = 0; index < value.call_arguments.size();
                     ++index) {
                    const auto transport =
                        index < callee.parameters.size() &&
                                callee.parameters[index].mode !=
                                    ParameterMode::In
                            ? ValueTransport::ByReference
                            : ValueTransport::Direct;
                    arguments.push_back(abi_value_for(
                        hir_, value.call_arguments[index].type,
                        subtarget_.target().data_layout, *abi, transport));
                }
                std::vector<AbiValue> results;
                if (!is_void(hir_, callee.result_type)) {
                    results.push_back(
                        abi_value_for(hir_, callee.result_type,
                                      subtarget_.target().data_layout, *abi));
                }
                const auto classified =
                    callee.variadic
                        ? classify_variadic_signature(
                              *abi, arguments, results,
                              callee.parameters.size(),
                              subtarget_.enabled_features())
                        : classify_signature(*abi, arguments, results,
                                             subtarget_.enabled_features());
                if (classified) {
                    const auto append_pieces = [&](const auto& pieces) {
                        for (const auto& piece : pieces) {
                            if (piece.location.kind == LocationKind::Register) {
                                append_clobbers({piece.location.reg});
                            }
                        }
                    };
                    for (const auto& assignment :
                         classified.layout.call.arguments) {
                        append_pieces(assignment.pieces);
                        append_pieces(assignment.shadows);
                    }
                    for (const auto& assignment : classified.layout.results) {
                        append_pieces(assignment.pieces);
                    }
                }
            }
            if (entity) append_clobbers(entity->clobbers);
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
            if (value.intrinsic == mir::IntrinsicOperation::MachineNop) {
                diagnostics_.error(
                    value.location,
                    "MIPS selection received an unavailable $::_nop operation");
            }
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
                auto source_id = *terminator.value;
                if (lower_xor_zero_extend_return(source_id, result)) break;
                const auto& returned = source_->values[source_id.value];
                if (returned.kind == mir::ValueKind::Cast &&
                    returned.operands.size() == 1 &&
                    (returned.cast == mir::CastOperation::ZeroExtend ||
                     returned.cast == mir::CastOperation::SignExtend) &&
                    type_bits(hir_, source_->values[
                                         returned.operands.front().value]
                                         .type) <
                        type_bits(hir_, returned.type)) {
                    source_id = returned.operands.front();
                    result.result_extension =
                        returned.cast == mir::CastOperation::ZeroExtend
                            ? machine::ExtensionKind::Zero
                            : machine::ExtensionKind::Sign;
                }
                const auto source = reg(source_id);
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
                const auto& managed_value = source.values[value.value];
                if (managed_value.kind == mir::ValueKind::VoidValue) continue;
                if (!target.instructions.empty() &&
                    target.instructions.back().kind ==
                        machine::InstructionKind::Call &&
                    target.instructions.back().must_tail &&
                    managed_value.kind == mir::ValueKind::LifetimeEnd) {
                    continue;
                }
                target.instructions.push_back(
                    lower_value(managed_value));
            }
            target.instructions.push_back(lower_terminator(block.terminator));
            current_.blocks.push_back(std::move(target));
            current_.layout.push_back({block.id.value});
        }
    }

    std::optional<SignatureLayout> classify_function_interface(
        const hir::Function& entity, const AbiEntry& abi) const {
        return classify_managed_interface(hir_, entity, subtarget_, abi);
    }

    bool allocate_registers(machine::Function& function) {
        const auto& entity = hir_.function(function.source);
        if (!options_.register_allocation) {
            const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
            bool changed = false;
            std::unordered_set<std::uint32_t> saved;
            const auto add_save = [&](machine::PhysicalRegisterId physical) {
                if (physical.value > fpr_physical_base &&
                    physical.value < fpr_physical_base + fpr_names.size() &&
                    (physical.value - fpr_physical_base) % 2U != 0 &&
                    subtarget_.has_feature(Feature::Mips2) &&
                    !subtarget_.has_feature(Feature::Fp64) &&
                    !subtarget_.has_feature(Feature::SingleFloat)) {
                    --physical.value;
                }
                const auto integer_name = gpr_name(physical);
                const auto floating_name = fpr_name(physical);
                const auto name = floating_name.empty() ? integer_name
                                                        : floating_name;
                if (name.empty() || !saved.insert(physical.value).second ||
                    (abi && std::find(
                        abi->call_clobbers.begin(), abi->call_clobbers.end(),
                        name) != abi->call_clobbers.end())) {
                    return;
                }
                function.callee_saved_registers.push_back(physical);
                changed = true;
                machine::StackSlot save;
                save.id = {static_cast<std::uint32_t>(
                    function.stack_slots.size())};
                save.kind = machine::StackSlotKind::CalleeSave;
                save.size = !floating_name.empty()
                                ? subtarget_.has_feature(Feature::Fp64) ||
                                          (subtarget_.has_feature(Feature::Mips2) &&
                                           !subtarget_.has_feature(Feature::SingleFloat))
                                      ? 8U
                                      : 4U
                            : subtarget_.has_feature(Feature::Mips3) ? 8U
                                                                    : 4U;
                save.saved_register = machine::Register::physical_register(
                    physical,
                    machine::IntegerMode{static_cast<std::uint16_t>(
                        save.size * 8U)});
                save.alignment = save.size;
                save.location = function.location;
                save.name = "$callee.save." + std::string(name);
                function.stack_slots.push_back(std::move(save));
            };
            for (const auto& block : function.blocks) {
                for (const auto& instruction : block.instructions) {
                    for (const auto& clobber : instruction.clobbers) {
                        if (clobber.kind ==
                            machine::RegisterKind::Physical) {
                            add_save({clobber.id});
                        }
                    }
                }
            }
            return changed;
        }
        const auto count = function.virtual_registers.size();
        if (count == 0) return false;

        std::vector<bool> eligible(count);
        std::vector<unsigned> use_count(count);
        for (std::size_t id = 0; id < count; ++id) {
            const auto register_class =
                function.virtual_register_classes[id];
            const bool integer =
                register_class == machine::VirtualRegisterClass::Integer &&
                (function.virtual_registers[id].bits <= 32 ||
                 subtarget_.has_feature(Feature::Mips3));
            const bool floating =
                register_class == machine::VirtualRegisterClass::Floating &&
                function.virtual_registers[id].bits <= 64 &&
                subtarget_.has_feature(Feature::Mips3) &&
                subtarget_.has_feature(Feature::HardFloat);
            eligible[id] = integer || floating;
        }
        const auto virtual_id = [&](const machine::Register& value)
            -> std::optional<std::uint32_t> {
            return value.kind == machine::RegisterKind::Virtual &&
                           value.id < count
                ? std::optional<std::uint32_t>{value.id}
                : std::nullopt;
        };
        const auto withdraw = [&](const machine::Register& value) {
            if (const auto id = virtual_id(value)) eligible[*id] = false;
        };

        // Call arguments, results, and phi nodes remain ordinary allocatable
        // SSA values. Exact endpoint exclusions and caller-save splits are
        // derived after liveness; phi uses live on predecessor edges and are
        // resolved in parallel by the assembly emitter.
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& use : instruction.uses) {
                    if (const auto id = virtual_id(use)) ++use_count[*id];
                }
                if (instruction.kind == machine::InstructionKind::Return) {
                    for (const auto& definition : instruction.defs) {
                        withdraw(definition);
                    }
                }
            }
        }

        using LiveSet = std::unordered_set<std::uint32_t>;
        const auto block_count = function.blocks.size();
        std::vector<LiveSet> block_uses(block_count), block_defs(block_count);
        std::vector<std::unordered_map<std::uint32_t, LiveSet>>
            phi_edge_uses(block_count);
        std::vector<LiveSet> live_in(block_count), live_out(block_count);
        for (const auto& block : function.blocks) {
            if (block.id.value >= block_count) continue;
            auto& uses = block_uses[block.id.value];
            auto& defs = block_defs[block.id.value];
            for (const auto& instruction : block.instructions) {
                if (instruction.opcode == Opcode::Phi) {
                    for (std::size_t index = 0;
                         index + 1 < instruction.operands.size(); index += 2) {
                        const auto* predecessor =
                            std::get_if<machine::BlockOperand>(
                                &instruction.operands[index]);
                        const auto* incoming =
                            std::get_if<machine::RegisterOperand>(
                                &instruction.operands[index + 1]);
                        if (!predecessor || !incoming) continue;
                        if (const auto id = virtual_id(incoming->value)) {
                            phi_edge_uses[block.id.value]
                                         [predecessor->target.value]
                                             .insert(*id);
                        }
                    }
                } else {
                    for (const auto& use : instruction.uses) {
                        if (const auto id = virtual_id(use);
                            id && !defs.contains(*id)) {
                            uses.insert(*id);
                        }
                    }
                }
                for (const auto& definition : instruction.defs) {
                    if (const auto id = virtual_id(definition)) {
                        defs.insert(*id);
                    }
                }
            }
        }
        bool liveness_changed = true;
        while (liveness_changed) {
            liveness_changed = false;
            for (auto item = function.layout.rbegin();
                 item != function.layout.rend(); ++item) {
                if (item->value >= block_count) continue;
                const auto& block = function.blocks[item->value];
                LiveSet next_out;
                for (const auto successor : block.successors) {
                    if (successor.value >= block_count) continue;
                    next_out.insert(live_in[successor.value].begin(),
                                    live_in[successor.value].end());
                    const auto edge =
                        phi_edge_uses[successor.value].find(block.id.value);
                    if (edge != phi_edge_uses[successor.value].end()) {
                        next_out.insert(edge->second.begin(),
                                        edge->second.end());
                    }
                }
                LiveSet next_in = block_uses[item->value];
                for (const auto id : next_out) {
                    if (!block_defs[item->value].contains(id)) {
                        next_in.insert(id);
                    }
                }
                if (next_out != live_out[item->value] ||
                    next_in != live_in[item->value]) {
                    live_out[item->value] = std::move(next_out);
                    live_in[item->value] = std::move(next_in);
                    liveness_changed = true;
                }
            }
        }

        // Record the physical colors each live range may not occupy. Stable
        // ABI registers remain allocatable across a call; volatile colors are
        // still legal elsewhere and are split through their fallback homes
        // after coloring.
        std::vector<LiveSet> forbidden_colors(count);
        std::vector<LiveSet> hard_forbidden_colors(count);
        std::vector<std::vector<machine::PhysicalRegisterId>>
            preferred_physical_colors(count);
        const auto* function_abi = managed_abi_model(
            hir_, entity, subtarget_, options_);
        const auto function_layout = function_abi
            ? classify_function_interface(entity, *function_abi)
            : std::optional<SignatureLayout>{};
        LiveSet incoming_endpoint_colors;
        std::unordered_map<std::uint32_t, unsigned>
            incoming_endpoint_uses;
        if (function_layout) {
            const auto append_endpoints = [&](const auto& pieces) {
                for (const auto& piece : pieces) {
                    if (piece.location.kind != LocationKind::Register) {
                        continue;
                    }
                    if (const auto physical =
                            physical_register_id(piece.location.reg)) {
                        incoming_endpoint_colors.insert(physical->value);
                        ++incoming_endpoint_uses[physical->value];
                    }
                }
            };
            for (const auto& assignment :
                 function_layout->call.arguments) {
                append_endpoints(assignment.pieces);
                append_endpoints(assignment.shadows);
            }
            for (const auto& result : function_layout->results) {
                if (result.indirect) append_endpoints(result.pieces);
            }
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if ((instruction.opcode == Opcode::Parameter ||
                     instruction.opcode == Opcode::Fparameter) &&
                    function_abi) {
                    for (const auto& definition : instruction.defs) {
                        const auto id = virtual_id(definition);
                        if (!id) continue;
                        // Parameter captures are emitted in source order. Keep
                        // destinations outside the endpoints actually live at
                        // this entry, not the ABI's entire possible bank. This
                        // leaves unused Cross channels available to allocation
                        // while preventing an early capture from destroying a
                        // later input.
                        if (function_layout) {
                            hard_forbidden_colors[*id].insert(
                                incoming_endpoint_colors.begin(),
                                incoming_endpoint_colors.end());
                            const auto* parameter_index =
                                instruction.operands.empty()
                                ? nullptr
                                : std::get_if<machine::ImmediateOperand>(
                                      &instruction.operands.front());
                            if (parameter_index &&
                                parameter_index->value <
                                    function_layout->call.arguments.size()) {
                                const auto& assignment =
                                    function_layout->call.arguments[
                                        parameter_index->value];
                                const ValuePiece* preferred_piece = nullptr;
                                if (assignment.pieces.size() == 1) {
                                    preferred_piece =
                                        &assignment.pieces.front();
                                } else if (
                                    instruction.input_projection &&
                                    parameter_index->value <
                                        entity.parameters.size()) {
                                    const auto logical_bits = type_bits(
                                        hir_, entity.parameters[
                                                  parameter_index->value]
                                                  .type);
                                    const auto projection_begin =
                                        static_cast<unsigned>(
                                            instruction.input_projection
                                                ->bit_offset);
                                    const auto projection_end =
                                        projection_begin +
                                        static_cast<unsigned>(
                                            instruction.input_projection
                                                ->bit_width);
                                    const auto selected = std::find_if(
                                        assignment.pieces.begin(),
                                        assignment.pieces.end(),
                                        [&](const ValuePiece& piece) {
                                            const auto piece_begin =
                                                effective_piece_offset(
                                                    piece, logical_bits,
                                                    subtarget_.target()
                                                        .data_layout
                                                        .byte_order);
                                            return projection_begin >=
                                                       piece_begin &&
                                                projection_end <=
                                                    piece_begin +
                                                        piece.value_bits;
                                    });
                                    if (selected != assignment.pieces.end() &&
                                        definition.mode.bits <=
                                            selected->carrier_bits) {
                                        preferred_piece = &*selected;
                                    }
                                }
                                if (preferred_piece &&
                                    preferred_piece->location.kind ==
                                        LocationKind::Register) {
                                    if (const auto physical =
                                            physical_register_id(
                                                preferred_piece->location
                                                    .reg);
                                        physical &&
                                        incoming_endpoint_uses[
                                            physical->value] == 1) {
                                        // A scalar or projected parameter can
                                        // stay in the one ABI endpoint it
                                        // consumes. Endpoint uniqueness keeps
                                        // an early capture from destroying a
                                        // later argument or shadow.
                                        hard_forbidden_colors[*id].erase(
                                            physical->value);
                                        preferred_physical_colors[*id]
                                            .push_back(*physical);
                                    }
                                }
                            }
                        } else {
                            for (const auto& bank : function_abi->banks) {
                                for (const auto& name : bank.arguments) {
                                    if (const auto physical =
                                            physical_register_id(name)) {
                                        hard_forbidden_colors[*id].insert(
                                            physical->value);
                                    }
                                }
                            }
                        }
                    }
                    continue;
                }
                if (instruction.kind == machine::InstructionKind::Return) {
                    if (!function_abi) continue;
                    for (const auto& use : instruction.uses) {
                        const auto id = virtual_id(use);
                        if (!id) continue;
                        if (function_layout &&
                            function_layout->results.size() == 1 &&
                            function_layout->results.front().pieces.size() ==
                                1 &&
                            function_layout->results.front()
                                    .pieces.front().location.kind ==
                                LocationKind::Register) {
                            if (const auto physical = physical_register_id(
                                    function_layout->results.front()
                                        .pieces.front().location.reg)) {
                                preferred_physical_colors[*id].push_back(
                                    *physical);
                            }
                            continue;
                        }
                        for (const auto& bank : function_abi->banks) {
                            for (const auto& name : bank.results) {
                                if (const auto physical =
                                        physical_register_id(name)) {
                                    hard_forbidden_colors[*id].insert(
                                        physical->value);
                                }
                            }
                        }
                    }
                    continue;
                }
                if (instruction.kind != machine::InstructionKind::Call) {
                    continue;
                }
                if (instruction.direct_callee &&
                    instruction.defs.size() == 1) {
                    const auto& callee =
                        hir_.function(*instruction.direct_callee);
                    if (const auto* callee_abi = managed_abi_model(
                            hir_, callee, subtarget_, options_)) {
                        const auto layout = classify_function_interface(
                            callee, *callee_abi);
                        if (layout && layout->results.size() == 1 &&
                            layout->results.front().pieces.size() == 1 &&
                            layout->results.front()
                                    .pieces.front().location.kind ==
                                LocationKind::Register) {
                            if (const auto id =
                                    virtual_id(instruction.defs.front())) {
                                if (const auto physical = physical_register_id(
                                        layout->results.front()
                                            .pieces.front().location.reg)) {
                                    preferred_physical_colors[*id].push_back(
                                        *physical);
                                }
                            }
                        }
                    }
                }
                for (const auto& use : instruction.uses) {
                    const auto id = virtual_id(use);
                    if (!id) continue;
                    // Argument placement is emitted sequentially today. Keep
                    // an allocated source outside every register the call may
                    // overwrite, including exact stable-bank argument slots,
                    // so the sequence is a trivially safe parallel copy.
                    for (const auto& clobber : instruction.clobbers) {
                        if (clobber.kind ==
                            machine::RegisterKind::Physical) {
                            hard_forbidden_colors[*id].insert(clobber.id);
                        }
                    }
                }
            }
        }
        for (const auto& block : function.blocks) {
            if (block.id.value >= block_count) continue;
            auto live = live_out[block.id.value];
            for (auto item = block.instructions.rbegin();
                 item != block.instructions.rend(); ++item) {
                for (const auto& definition : item->defs) {
                    if (const auto id = virtual_id(definition)) {
                        live.erase(*id);
                    }
                }
                if (item->kind == machine::InstructionKind::Call) {
                    for (const auto id : live) {
                        if (id >= count) continue;
                        for (const auto& clobber : item->clobbers) {
                            if (clobber.kind ==
                                machine::RegisterKind::Physical) {
                                forbidden_colors[id].insert(clobber.id);
                            }
                        }
                    }
                }
                if (item->opcode != Opcode::Phi) {
                    for (const auto& use : item->uses) {
                        if (const auto id = virtual_id(use)) live.insert(*id);
                    }
                }
            }
        }

        std::vector<LiveSet> interference(count);
        std::vector<LiveSet> affinity(count);
        std::vector<LiveSet> backedge_affinity(count);
        const Dominance dominance(
            function.blocks.size(), function.entry.value,
            [&](std::uint32_t block) -> const std::vector<machine::BlockId>& {
                return function.blocks[block].successors;
            },
            [&](std::uint32_t block) -> const std::vector<machine::BlockId>& {
                return function.blocks[block].predecessors;
            });
        for (const auto& block : function.blocks) {
            if (block.id.value >= block_count) continue;
            auto live = live_out[block.id.value];
            for (auto item = block.instructions.rbegin();
                 item != block.instructions.rend(); ++item) {
                if (item->opcode != Opcode::Phi) {
                    // Simultaneously consumed values must not share a color,
                    // even when both die at this instruction. Reverse
                    // liveness alone adds them only after visiting the use.
                    for (std::size_t left = 0; left < item->uses.size();
                         ++left) {
                        const auto lhs = virtual_id(item->uses[left]);
                        if (!lhs) continue;
                        for (std::size_t right = left + 1;
                             right < item->uses.size(); ++right) {
                            const auto rhs = virtual_id(item->uses[right]);
                            if (!rhs || *lhs == *rhs) continue;
                            interference[*lhs].insert(*rhs);
                            interference[*rhs].insert(*lhs);
                        }
                    }
                }
                for (const auto& definition : item->defs) {
                    const auto id = virtual_id(definition);
                    if (!id) continue;
                    for (const auto other : live) {
                        if (other == *id) continue;
                        interference[*id].insert(other);
                        interference[other].insert(*id);
                    }
                    live.erase(*id);
                }
                if (item->opcode != Opcode::Phi) {
                    for (const auto& use : item->uses) {
                        if (const auto id = virtual_id(use)) live.insert(*id);
                    }
                }
            }
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.opcode != Opcode::Phi ||
                    instruction.defs.size() != 1) {
                    continue;
                }
                const auto target = virtual_id(instruction.defs.front());
                if (!target) continue;
                for (std::size_t index = 1;
                     index < instruction.operands.size(); index += 2) {
                    const auto* predecessor =
                        std::get_if<machine::BlockOperand>(
                            &instruction.operands[index - 1]);
                    const auto* incoming =
                        std::get_if<machine::RegisterOperand>(
                            &instruction.operands[index]);
                    if (!incoming) continue;
                    const auto source = virtual_id(incoming->value);
                    if (!source || *source == *target ||
                        function.virtual_register_classes[*source] !=
                            function.virtual_register_classes[*target]) {
                        continue;
                    }
                    // An existing edge is a real overlap: the PHI result may
                    // still be read after this incoming value is defined in
                    // the predecessor.  Single-use of the incoming value is
                    // not enough to prove coalescing safe.
                    if (interference[*source].contains(*target)) continue;
                    affinity[*target].insert(*source);
                    affinity[*source].insert(*target);
                    if (options_.cprop_registers && predecessor &&
                        predecessor->target.value < block_count &&
                        dominance.dominates(block.id.value,
                                            predecessor->target.value)) {
                        // Reserve a common color for a genuine loop-carried
                        // edge before unrelated ranges consume it. This is
                        // especially important for rotated loops, where a
                        // conditional critical edge cannot emit unconditional
                        // PHI moves in the latch itself.
                        backedge_affinity[*target].insert(*source);
                        backedge_affinity[*source].insert(*target);
                    }
                }
            }
        }
        std::vector<machine::PhysicalRegisterId> integer_colors;
        // Before MIPS III, 64-bit integers are legalized to word pairs whose
        // expansions use these registers as fixed scratches.
        const bool pair_scratch = !subtarget_.has_feature(Feature::Mips3) &&
            std::any_of(function.blocks.begin(), function.blocks.end(),
                [&](const machine::Block& block) {
                    return std::any_of(
                        block.instructions.begin(), block.instructions.end(),
                        [&](const machine::Instruction& instruction) {
                            const auto wide = [&](const machine::Register& value) {
                                return value.mode.bits > 32 && value.mode.bits <= 64 &&
                                    (value.kind != machine::RegisterKind::Virtual ||
                                     value.id >= count ||
                                     function.virtual_register_classes[value.id] ==
                                         machine::VirtualRegisterClass::Integer);
                            };
                            return std::any_of(instruction.uses.begin(),
                                               instruction.uses.end(), wide) ||
                                   std::any_of(instruction.defs.begin(),
                                               instruction.defs.end(), wide);
                        });
                });
        if (!pair_scratch) {
            // These registers are never implicit emitter scratches on the
            // native-width scalar path. They are preferred because they
            // require no prologue save under o32/EABI.
            for (const auto id : {2U, 3U, 4U, 5U, 6U, 7U,
                                  14U, 15U, 24U, 25U}) {
                integer_colors.push_back({id});
            }
            const bool needs_atomic_compare_scratch = std::any_of(
                function.blocks.begin(), function.blocks.end(),
                [](const machine::Block& block) {
                    return std::any_of(
                        block.instructions.begin(), block.instructions.end(),
                        [](const machine::Instruction& instruction) {
                            return decode_opcode(instruction.opcode) ==
                                Opcode::AtomicCompareExchange;
                        });
                });
            // The pair legalizer owns t4/t5 only on pre-MIPS-III targets.
            // Keep them available to native-width scalar code unless the one
            // LL/SC expansion that explicitly needs both appears.
            if (!needs_atomic_compare_scratch) {
                integer_colors.push_back({12});
                integer_colors.push_back({13});
            }
        }
        for (std::uint32_t id = 16; id <= 23; ++id) {
            integer_colors.push_back({id});
        }
        // F0/F2/F4 remain reserved for conversion, comparison, and ABI
        // staging. Even registers avoid an FR=0 odd-double restriction while
        // still covering the volatile o32/VR4300 bank without prologue saves.
        std::vector<machine::PhysicalRegisterId> floating_colors;
        for (const auto number : {6U, 8U, 10U, 12U, 14U, 16U, 18U,
                                  20U, 22U, 24U, 26U, 28U, 30U}) {
            floating_colors.push_back({fpr_physical_base + number});
        }

        std::vector<std::uint32_t> order(count);
        for (std::uint32_t id = 0; id < count; ++id) order[id] = id;
        const auto has_physical_preference = [&](std::uint32_t id) {
            if (!preferred_physical_colors[id].empty()) return true;
            return std::any_of(
                affinity[id].begin(), affinity[id].end(),
                [&](std::uint32_t neighbor) {
                    return neighbor < count &&
                        !preferred_physical_colors[neighbor].empty();
                });
        };
        std::sort(order.begin(), order.end(), [&](std::uint32_t left,
                                                  std::uint32_t right) -> bool {
            if (eligible[left] != eligible[right]) return eligible[left];
            const auto left_preferred = has_physical_preference(left);
            const auto right_preferred = has_physical_preference(right);
            if (left_preferred != right_preferred) return left_preferred;
            if (interference[left].size() != interference[right].size()) {
                return interference[left].size() >
                       interference[right].size();
            }
            if (use_count[left] != use_count[right]) {
                return use_count[left] > use_count[right];
            }
            return left < right;
        });

        bool changed = false;
        for (const auto id : order) {
            if (!eligible[id] ||
                function.virtual_register_assignments[id]) {
                continue;
            }
            const auto& colors =
                function.virtual_register_classes[id] ==
                        machine::VirtualRegisterClass::Floating
                    ? floating_colors
                    : integer_colors;
            std::vector<machine::PhysicalRegisterId> preferred;
            const auto append = [&](machine::PhysicalRegisterId color) {
                if (std::find(colors.begin(), colors.end(), color) ==
                    colors.end()) {
                    return;
                }
                if (std::find(preferred.begin(), preferred.end(), color) ==
                    preferred.end()) {
                    preferred.push_back(color);
                }
            };
            const auto append_affinity = [&](bool volatile_color) {
                for (const auto neighbor : affinity[id]) {
                    if (neighbor >= count) continue;
                    const auto color =
                        function.virtual_register_assignments[neighbor];
                    if (color && forbidden_colors[id].contains(color->value) ==
                                     volatile_color) {
                        append(*color);
                    }
                }
            };
            // A call-stable affinity/color avoids a split. Volatile colors
            // remain valid fallbacks when stable pressure is exhausted.
            for (const auto color : preferred_physical_colors[id]) {
                if (!forbidden_colors[id].contains(color.value)) append(color);
            }
            // A PHI may be colored before its incoming parameter or return
            // value. Carry explicit ABI endpoint preferences across affinity
            // edges so allocation order does not turn one eliminated entry
            // copy into a larger parallel-copy cycle at the loop boundary.
            for (const auto neighbor : affinity[id]) {
                if (neighbor >= count) continue;
                for (const auto color :
                     preferred_physical_colors[neighbor]) {
                    if (!forbidden_colors[id].contains(color.value)) {
                        append(color);
                    }
                }
            }
            append_affinity(false);
            for (const auto color : colors) {
                if (!forbidden_colors[id].contains(color.value)) append(color);
            }
            append_affinity(true);
            for (const auto color : colors) {
                if (forbidden_colors[id].contains(color.value)) append(color);
            }
            const auto selected = std::find_if(
                preferred.begin(), preferred.end(),
                [&](machine::PhysicalRegisterId color) {
                    if (hard_forbidden_colors[id].contains(color.value)) {
                        return false;
                    }
                    return std::none_of(
                        interference[id].begin(),
                        interference[id].end(),
                        [&](std::uint32_t neighbor) {
                            return neighbor < count &&
                                function.virtual_register_assignments[
                                    neighbor] == color;
                        });
                });
            if (selected == preferred.end()) continue;
            function.virtual_register_assignments[id] = *selected;
            changed = true;
            for (const auto neighbor : backedge_affinity[id]) {
                if (neighbor >= count || !eligible[neighbor] ||
                    function.virtual_register_assignments[neighbor] ||
                    hard_forbidden_colors[neighbor].contains(
                        selected->value) ||
                    interference[id].contains(neighbor) ||
                    std::any_of(
                        interference[neighbor].begin(),
                        interference[neighbor].end(),
                        [&](std::uint32_t other) {
                            return other < count &&
                                function.virtual_register_assignments[other] ==
                                    *selected;
                        })) {
                    continue;
                }
                function.virtual_register_assignments[neighbor] = *selected;
            }
        }

        // F0 is normally kept out of the palette because several complex
        // expansions use it as a fixed scratch. In a fully colored leaf that
        // contains only direct floating operations, no fallback can need that
        // scratch. Rename the return value's entire existing color class to
        // the ABI result endpoint. Values already sharing one color are
        // pairwise noninterfering, so this preserves PHI coalescing while
        // removing the final MOV.D/MOV.S.
        const auto uses_fixed_f0 = [](Opcode opcode) {
            switch (opcode) {
            case Opcode::Fiszero:
            case Opcode::FcmpEq:
            case Opcode::FcmpNe:
            case Opcode::FcmpLt:
            case Opcode::FcmpLe:
            case Opcode::FcmpGt:
            case Opcode::FcmpGe:
            case Opcode::Freinterpret:
            case Opcode::Fextend:
            case Opcode::Ftruncate:
            case Opcode::Sitofp:
            case Opcode::Uitofp:
            case Opcode::Fptosi:
            case Opcode::Fptoui:
            case Opcode::Select:
                return true;
            default:
                return false;
            }
        };
        bool can_rename_floating_result = std::none_of(
            function.blocks.begin(), function.blocks.end(),
            [&](const machine::Block& block) {
                return std::any_of(
                    block.instructions.begin(), block.instructions.end(),
                    [&](const machine::Instruction& instruction) {
                        return instruction.kind ==
                                   machine::InstructionKind::Call ||
                               uses_fixed_f0(decode_opcode(
                                   instruction.opcode));
                    });
            });
        if (can_rename_floating_result) {
            for (std::uint32_t id = 0; id < count; ++id) {
                if (eligible[id] &&
                    function.virtual_register_classes[id] ==
                        machine::VirtualRegisterClass::Floating &&
                    !function.virtual_register_assignments[id]) {
                    can_rename_floating_result = false;
                    break;
                }
            }
        }
        if (can_rename_floating_result) {
            const machine::PhysicalRegisterId f0{fpr_physical_base};
            for (std::uint32_t id = 0; id < count; ++id) {
                const auto& preferences = preferred_physical_colors[id];
                const auto old = function.virtual_register_assignments[id];
                if (!old || *old == f0 ||
                    std::find(preferences.begin(), preferences.end(), f0) ==
                        preferences.end()) {
                    continue;
                }
                std::vector<std::uint32_t> color_class;
                for (std::uint32_t member = 0; member < count; ++member) {
                    if (function.virtual_register_assignments[member] == *old) {
                        color_class.push_back(member);
                    }
                }
                const bool legal = std::all_of(
                    color_class.begin(), color_class.end(),
                    [&](std::uint32_t member) {
                        if (hard_forbidden_colors[member].contains(f0.value) ||
                            forbidden_colors[member].contains(f0.value)) {
                            return false;
                        }
                        return std::none_of(
                            interference[member].begin(),
                            interference[member].end(),
                            [&](std::uint32_t neighbor) {
                                return neighbor < count &&
                                    function.virtual_register_assignments[
                                        neighbor] == f0;
                            });
                    });
                if (!legal) continue;
                for (const auto member : color_class) {
                    function.virtual_register_assignments[member] = f0;
                }
            }
        }
        // Preserve only volatile assignments that actually cross a specific
        // call. Stable colors need no traffic; volatile colors retain their
        // existing spill home for one store/reload pair around that call.
        std::unordered_set<std::uint32_t> call_live_homes;
        for (auto& block : function.blocks) {
            if (block.id.value >= block_count) continue;
            auto live = live_out[block.id.value];
            for (auto item = block.instructions.rbegin();
                 item != block.instructions.rend(); ++item) {
                item->live_across_call.clear();
                for (const auto& definition : item->defs) {
                    if (const auto id = virtual_id(definition)) {
                        live.erase(*id);
                    }
                }
                if (item->kind == machine::InstructionKind::Call) {
                    std::vector<std::uint32_t> ids(live.begin(), live.end());
                    std::sort(ids.begin(), ids.end());
                    for (const auto id : ids) {
                        if (id >= count ||
                            !function.virtual_register_assignments[id]) {
                            continue;
                        }
                        const auto color =
                            *function.virtual_register_assignments[id];
                        const bool clobbered = std::any_of(
                            item->clobbers.begin(), item->clobbers.end(),
                            [&](const machine::Register& clobber) {
                                return clobber.kind ==
                                           machine::RegisterKind::Physical &&
                                       clobber.id == color.value;
                            });
                        if (!clobbered) continue;
                        item->live_across_call.push_back(
                            machine::Register::virtual_register(
                                {id}, function.virtual_registers[id]));
                        call_live_homes.insert(id);
                    }
                }
                if (item->opcode != Opcode::Phi) {
                    for (const auto& use : item->uses) {
                        if (const auto id = virtual_id(use)) live.insert(*id);
                    }
                }
            }
        }

        const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
        const auto function_clobbers = [&](std::string_view name) {
            const auto contains = [&](const std::vector<std::string>& names) {
                return std::find(names.begin(), names.end(), name) !=
                       names.end();
            };
            return (abi && contains(abi->call_clobbers)) ||
                   contains(entity.clobbers);
        };
        std::unordered_set<std::uint32_t> saved;
        const auto preserve_storage = [&](machine::PhysicalRegisterId physical) {
            if (physical.value > fpr_physical_base &&
                physical.value < fpr_physical_base + fpr_names.size() &&
                (physical.value - fpr_physical_base) % 2U != 0 &&
                subtarget_.has_feature(Feature::Mips2) &&
                !subtarget_.has_feature(Feature::Fp64) &&
                !subtarget_.has_feature(Feature::SingleFloat)) {
                --physical.value;
            }
            const auto integer_name = gpr_name(physical);
            const auto floating_name = fpr_name(physical);
            // Any GPR the function's ABI preserves, including gp when a
            // callee's contract clobbers it.
            const bool preserved_integer = !integer_name.empty() &&
                !function_clobbers(integer_name);
            const bool preserved_floating = !floating_name.empty() &&
                !function_clobbers(floating_name);
            if ((!preserved_integer && !preserved_floating) ||
                function.frame.elide_incoming_saves ||
                !saved.insert(physical.value).second) {
                return;
            }
            function.callee_saved_registers.push_back(physical);
            changed = true;
            machine::StackSlot save;
            save.id = {static_cast<std::uint32_t>(
                function.stack_slots.size())};
            save.kind = machine::StackSlotKind::CalleeSave;
            save.size = preserved_floating
                            ? subtarget_.has_feature(Feature::Fp64) ||
                                      (subtarget_.has_feature(Feature::Mips2) &&
                                       !subtarget_.has_feature(Feature::SingleFloat))
                                  ? 8U
                                  : 4U
                        : subtarget_.has_feature(Feature::Mips3) ? 8U : 4U;
            save.saved_register = machine::Register::physical_register(
                physical, machine::IntegerMode{static_cast<std::uint16_t>(
                              save.size * 8U)});
            save.alignment = save.size;
            save.location = function.location;
            save.name = "$callee.save." +
                std::string(preserved_floating ? floating_name
                                               : integer_name);
            function.stack_slots.push_back(std::move(save));
        };
        for (std::size_t id = 0; id < count; ++id) {
            const auto assignment =
                function.virtual_register_assignments[id];
            if (!assignment) continue;
            for (auto& slot : function.stack_slots) {
                if (slot.spill_for && slot.spill_for->value == id) {
                    if (!call_live_homes.contains(
                            static_cast<std::uint32_t>(id))) {
                        slot.elided = true;
                        slot.frame_offset.reset();
                    }
                    break;
                }
            }
            preserve_storage(*assignment);
        }
        // A registered entry may call a private dynamic interface that uses
        // registers its own ABI promises to preserve as argument or scratch
        // storage. Those bridge clobbers need homes even when none of the
        // caller's virtual values happens to allocate to the register.
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& clobber : instruction.clobbers) {
                    if (clobber.kind == machine::RegisterKind::Physical) {
                        preserve_storage({clobber.id});
                    }
                }
            }
        }
        return changed;
    }

    bool fold_pointer_offsets(machine::Function& function) {
        if (!options_.combine_addresses) return false;
        bool changed = false;
        std::vector<std::optional<machine::ImmediateOperand>> constants(
            function.virtual_registers.size());
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (instruction.opcode != Opcode::Constant ||
                    instruction.defs.size() != 1 ||
                    instruction.defs.front().kind !=
                        machine::RegisterKind::Virtual ||
                    instruction.operands.empty()) {
                    continue;
                }
                const auto* immediate =
                    std::get_if<machine::ImmediateOperand>(
                        &instruction.operands.front());
                if (immediate && instruction.defs.front().id <
                                     constants.size()) {
                    constants[instruction.defs.front().id] = *immediate;
                }
            }
        }

        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                const auto opcode = decode_opcode(instruction.opcode);
                const bool indexed_address =
                    opcode == Opcode::IndexedAddress;
                const bool indexed_load =
                    opcode == Opcode::IndexedLoadSigned ||
                    opcode == Opcode::IndexedLoadUnsigned ||
                    opcode == Opcode::FindexedLoad;
                if ((!indexed_address && !indexed_load) ||
                    instruction.uses.size() != 2 ||
                    instruction.operands.empty()) {
                    continue;
                }
                const auto index = instruction.uses[1];
                if (index.kind != machine::RegisterKind::Virtual ||
                    index.id >= constants.size() || !constants[index.id]) {
                    continue;
                }
                const auto* scale =
                    std::get_if<machine::ImmediateOperand>(
                        &instruction.operands.back());
                if (!scale || constants[index.id]->high != 0 ||
                    (scale->value != 0 &&
                     constants[index.id]->value >
                         static_cast<std::uint64_t>(
                             std::numeric_limits<std::int16_t>::max()) /
                             scale->value)) {
                    continue;
                }
                const auto bytes = constants[index.id]->value * scale->value;
                if (bytes > static_cast<std::uint64_t>(
                                std::numeric_limits<std::int16_t>::max())) {
                    continue;
                }
                const auto base = instruction.uses.front();
                if (indexed_address) {
                    instruction.opcode = Opcode::PointerOffset;
                } else if (opcode == Opcode::IndexedLoadSigned) {
                    instruction.opcode = Opcode::PointerLoadSigned;
                } else if (opcode == Opcode::IndexedLoadUnsigned) {
                    instruction.opcode = Opcode::PointerLoadUnsigned;
                } else {
                    instruction.opcode = Opcode::FpointerLoad;
                }
                instruction.uses = {base};
                instruction.operands = {
                    machine::RegisterOperand{base},
                    machine::ImmediateOperand{
                        bytes, 0,
                        {static_cast<std::uint16_t>(hir_.address_bits)},
                        true}};
                changed = true;
            }
        }

        changed = share_index_scales(function) || changed;

        std::vector<const machine::Instruction*> definitions(
            function.virtual_registers.size());
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto definition : instruction.defs) {
                    if (definition.kind ==
                            machine::RegisterKind::Virtual &&
                        definition.id < definitions.size()) {
                        definitions[definition.id] = &instruction;
                    }
                }
            }
        }
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                const auto opcode = decode_opcode(instruction.opcode);
                const bool pointer_access =
                    opcode == Opcode::PointerLoadSigned ||
                    opcode == Opcode::PointerLoadUnsigned ||
                    opcode == Opcode::FpointerLoad ||
                    opcode == Opcode::PointerStore ||
                    opcode == Opcode::FpointerStore;
                if (!pointer_access || instruction.uses.empty()) continue;
                const auto address = instruction.uses.front();
                if (address.kind != machine::RegisterKind::Virtual ||
                    address.id >= definitions.size() ||
                    !definitions[address.id] ||
                    definitions[address.id]->opcode != Opcode::PointerOffset ||
                    definitions[address.id]->uses.size() != 1 ||
                    definitions[address.id]->operands.size() < 2) {
                    continue;
                }
                const auto* offset =
                    std::get_if<machine::ImmediateOperand>(
                        &definitions[address.id]->operands.back());
                if (!offset) continue;
                const auto base = definitions[address.id]->uses.front();
                instruction.uses.front() = base;
                instruction.operands.front() =
                    machine::RegisterOperand{base};
                instruction.operands.push_back(*offset);
                changed = true;
            }
        }
        return changed;
    }

    machine::Register create_integer_temporary(
        machine::Function& function, machine::IntegerMode mode,
        SourceLocation location, std::string_view purpose) {
        const machine::VirtualRegisterId id{
            static_cast<std::uint32_t>(function.virtual_registers.size())};
        function.virtual_registers.push_back(mode);
        function.virtual_register_classes.push_back(
            machine::VirtualRegisterClass::Integer);
        function.virtual_register_assignments.push_back(std::nullopt);
        function.rematerialized_immediates.push_back(std::nullopt);

        machine::StackSlot spill;
        spill.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
        spill.kind = machine::StackSlotKind::Spill;
        spill.size = mode.bits > 32 ? 8U : 4U;
        spill.alignment = spill.size;
        spill.location = location;
        spill.name = "$" + std::string(purpose) + "." +
                     std::to_string(id.value);
        spill.spill_for = id;
        function.stack_slots.push_back(std::move(spill));
        return machine::Register::virtual_register(id, mode);
    }

    bool share_index_scales(machine::Function& function) {
        struct Group {
            machine::Register index;
            std::uint64_t scale{};
            std::size_t first{};
            unsigned uses{};
            std::optional<machine::Register> scaled;
        };
        const auto scale_of = [&](const machine::Instruction& instruction)
            -> std::optional<std::pair<machine::Register, std::uint64_t>> {
            const auto opcode = decode_opcode(instruction.opcode);
            if (opcode != Opcode::IndexedAddress &&
                opcode != Opcode::IndexedLoadSigned &&
                opcode != Opcode::IndexedLoadUnsigned &&
                opcode != Opcode::FindexedLoad) {
                return std::nullopt;
            }
            if (instruction.uses.size() != 2 ||
                instruction.operands.size() < 3) {
                return std::nullopt;
            }
            const auto index = instruction.uses[1];
            const auto* scale = std::get_if<machine::ImmediateOperand>(
                &instruction.operands.back());
            if (index.kind != machine::RegisterKind::Virtual || !scale ||
                scale->value <= 1 || !std::has_single_bit(scale->value) ||
                index.mode.bits != hir_.address_bits) {
                return std::nullopt;
            }
            return std::pair{index, scale->value};
        };

        bool changed = false;
        for (auto& block : function.blocks) {
            std::vector<Group> groups;
            for (std::size_t position = 0;
                 position < block.instructions.size(); ++position) {
                const auto candidate = scale_of(block.instructions[position]);
                if (!candidate) continue;
                const auto found = std::find_if(
                    groups.begin(), groups.end(), [&](const Group& group) {
                        return group.index == candidate->first &&
                               group.scale == candidate->second;
                    });
                if (found == groups.end()) {
                    groups.push_back(
                        {candidate->first, candidate->second, position, 1,
                         std::nullopt});
                } else {
                    ++found->uses;
                }
            }
            std::erase_if(groups, [](const Group& group) {
                return group.uses < 2;
            });
            if (groups.empty()) continue;

            for (auto& group : groups) {
                group.scaled = create_integer_temporary(
                    function, group.index.mode,
                    block.instructions[group.first].location,
                    "index.scale");
            }

            std::vector<machine::Instruction> rewritten;
            rewritten.reserve(block.instructions.size() + groups.size());
            for (std::size_t position = 0;
                 position < block.instructions.size(); ++position) {
                for (const auto& group : groups) {
                    if (group.first != position) continue;
                    machine::Instruction shift;
                    shift.kind = machine::InstructionKind::Target;
                    shift.opcode = Opcode::Shl;
                    shift.location = block.instructions[position].location;
                    shift.operands = {
                        machine::RegisterOperand{group.index},
                        machine::ImmediateOperand{
                            static_cast<std::uint64_t>(
                                std::countr_zero(group.scale)), 0,
                            group.index.mode, false}};
                    shift.uses = {group.index};
                    shift.defs = {*group.scaled};
                    rewritten.push_back(std::move(shift));
                }

                auto instruction = std::move(block.instructions[position]);
                const auto candidate = scale_of(instruction);
                if (candidate) {
                    const auto group = std::find_if(
                        groups.begin(), groups.end(), [&](const Group& value) {
                            return value.index == candidate->first &&
                                   value.scale == candidate->second;
                        });
                    if (group != groups.end()) {
                        instruction.uses[1] = *group->scaled;
                        instruction.operands[1] =
                            machine::RegisterOperand{*group->scaled};
                        std::get<machine::ImmediateOperand>(
                            instruction.operands.back()).value = 1;
                    }
                }
                rewritten.push_back(std::move(instruction));
            }
            block.instructions = std::move(rewritten);
            changed = true;
        }
        return changed;
    }

    bool fold_boundary_projections(machine::Function& function) {
        if (!options_.cprop_registers) return false;

        const auto& entity = hir_.function(function.source);
        std::unordered_map<std::uint32_t, machine::Register> replacements;
        std::unordered_set<std::uint32_t> removed_truncations;
        bool changed = false;

        for (auto& block : function.blocks) {
            for (auto& parameter : block.instructions) {
                if (decode_opcode(parameter.opcode) != Opcode::Parameter ||
                    parameter.defs.size() != 1 ||
                    parameter.defs.front().kind !=
                        machine::RegisterKind::Virtual ||
                    parameter.operands.empty()) {
                    continue;
                }
                const auto* parameter_index =
                    std::get_if<machine::ImmediateOperand>(
                        &parameter.operands.front());
                const auto source = parameter.defs.front();
                if (!parameter_index ||
                    parameter_index->value >= entity.parameters.size() ||
                    entity.parameters[parameter_index->value].mode !=
                        ParameterMode::In ||
                    source.id >= function.virtual_register_classes.size() ||
                    function.virtual_register_classes[source.id] !=
                        machine::VirtualRegisterClass::Integer) {
                    continue;
                }

                std::vector<machine::Register> truncations;
                bool eligible = true;
                for (const auto& use_block : function.blocks) {
                    for (const auto& consumer : use_block.instructions) {
                        const auto occurrences = static_cast<unsigned>(
                            std::count(consumer.uses.begin(),
                                       consumer.uses.end(), source));
                        if (occurrences == 0) continue;
                        if (occurrences != 1 ||
                            decode_opcode(consumer.opcode) != Opcode::Trunc ||
                            consumer.kind !=
                                machine::InstructionKind::Target ||
                            consumer.uses.size() != 1 ||
                            consumer.defs.size() != 1 ||
                            consumer.defs.front().kind !=
                                machine::RegisterKind::Virtual ||
                            consumer.defs.front().mode.bits > 32 ||
                            consumer.defs.front().mode.bits >=
                                source.mode.bits ||
                            consumer.defs.front().id >=
                                function.virtual_register_classes.size() ||
                            function.virtual_register_classes[
                                consumer.defs.front().id] !=
                                machine::VirtualRegisterClass::Integer ||
                            (!truncations.empty() &&
                             consumer.defs.front().mode !=
                                 truncations.front().mode)) {
                            eligible = false;
                            break;
                        }
                        truncations.push_back(consumer.defs.front());
                    }
                    if (!eligible) break;
                }
                if (!eligible || truncations.empty()) continue;

                const auto replacement = truncations.front();
                parameter.defs.front() = replacement;
                parameter.input_projection = machine::ValueProjection{
                    0, replacement.mode.bits};
                for (const auto truncation : truncations) {
                    removed_truncations.insert(truncation.id);
                    if (truncation != replacement) {
                        replacements[truncation.id] = replacement;
                    }
                }
                changed = true;
            }
        }
        if (!changed) return false;

        const auto replace = [&](machine::Register value) {
            if (value.kind != machine::RegisterKind::Virtual) return value;
            const auto found = replacements.find(value.id);
            return found == replacements.end() ? value : found->second;
        };
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                for (auto& use : instruction.uses) use = replace(use);
                for (auto& live : instruction.live_across_call) {
                    live = replace(live);
                }
                for (auto& operand : instruction.operands) {
                    if (auto* reg =
                            std::get_if<machine::RegisterOperand>(&operand)) {
                        reg->value = replace(reg->value);
                    }
                }
            }
            std::erase_if(
                block.instructions,
                [&](const machine::Instruction& instruction) {
                    return decode_opcode(instruction.opcode) == Opcode::Trunc &&
                        instruction.defs.size() == 1 &&
                        instruction.defs.front().kind ==
                            machine::RegisterKind::Virtual &&
                        removed_truncations.contains(
                            instruction.defs.front().id);
                });
        }
        return true;
    }

    bool select_integer_immediates(machine::Function& function) {
        if (!options_.machine_combine) return false;

        std::vector<std::optional<machine::ImmediateOperand>> constants(
            function.virtual_registers.size());
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if (decode_opcode(instruction.opcode) != Opcode::Constant ||
                    instruction.defs.size() != 1 ||
                    instruction.defs.front().kind !=
                        machine::RegisterKind::Virtual ||
                    instruction.operands.empty()) {
                    continue;
                }
                const auto* immediate =
                    std::get_if<machine::ImmediateOperand>(
                        &instruction.operands.front());
                const auto id = instruction.defs.front().id;
                if (immediate && id < constants.size()) {
                    constants[id] = *immediate;
                }
            }
        }

        const auto constant_for = [&](machine::Register value)
            -> const machine::ImmediateOperand* {
            if (value.kind != machine::RegisterKind::Virtual ||
                value.id >= constants.size() || !constants[value.id]) {
                return nullptr;
            }
            return &*constants[value.id];
        };
        const auto signed_value = [](const machine::ImmediateOperand& value,
                                     unsigned bits) {
            auto raw = value.value;
            if (bits < 64) {
                const auto mask = (std::uint64_t{1} << bits) - 1U;
                raw &= mask;
                if ((raw & (std::uint64_t{1} << (bits - 1U))) != 0) {
                    raw |= ~mask;
                }
            }
            return static_cast<std::int64_t>(raw);
        };
        const auto fits_signed_16 = [](std::int64_t value) {
            return value >= std::numeric_limits<std::int16_t>::min() &&
                   value <= std::numeric_limits<std::int16_t>::max();
        };

        bool changed = false;
        for (auto& block : function.blocks) {
            for (auto& instruction : block.instructions) {
                if (instruction.kind != machine::InstructionKind::Target ||
                    instruction.uses.size() != 2 ||
                    instruction.defs.size() != 1) {
                    continue;
                }
                auto opcode = decode_opcode(instruction.opcode);
                const bool commutative =
                    opcode == Opcode::Add || opcode == Opcode::And ||
                    opcode == Opcode::Or || opcode == Opcode::Xor;
                auto variable = instruction.uses[0];
                auto* immediate = constant_for(instruction.uses[1]);
                if (!immediate && commutative) {
                    immediate = constant_for(instruction.uses[0]);
                    variable = instruction.uses[1];
                }
                if (!immediate || immediate->high != 0 ||
                    variable.mode.bits < 32 || variable.mode.bits > 64 ||
                    (variable.mode.bits > 32 &&
                     !subtarget_.has_feature(Feature::Mips3))) {
                    continue;
                }

                auto selected = *immediate;
                selected.mode = variable.mode;
                bool encodable = false;
                if (opcode == Opcode::Add || opcode == Opcode::Sub) {
                    // ADDIU/DADDIU sign-extend their 16-bit field. Interpret
                    // the source constant modulo the value width, then use
                    // addition for both x+C and x-C when the resulting field
                    // is representable.
                    auto amount = signed_value(
                        selected, variable.mode.bits);
                    if (opcode == Opcode::Sub) {
                        if (amount == std::numeric_limits<std::int64_t>::min()) {
                            continue;
                        }
                        amount = -amount;
                    }
                    if (fits_signed_16(amount)) {
                        selected.value =
                            static_cast<std::uint64_t>(amount);
                        selected.high = 0;
                        selected.is_signed = true;
                        instruction.opcode = Opcode::Add;
                        encodable = true;
                    }
                } else if (opcode == Opcode::And || opcode == Opcode::Or ||
                           opcode == Opcode::Xor) {
                    // Logical immediates are zero-extended. Wider masks keep
                    // their register form so upper bits remain exact.
                    encodable = selected.value <= 0xffffU;
                    selected.is_signed = false;
                } else if (opcode == Opcode::Shl || opcode == Opcode::ShrS ||
                           opcode == Opcode::ShrU) {
                    selected.value %= variable.mode.bits;
                    selected.is_signed = false;
                    encodable = true;
                }
                if (!encodable) continue;

                instruction.uses = {variable};
                instruction.operands = {
                    machine::RegisterOperand{variable}, selected};
                changed = true;
            }
        }
        return changed;
    }

    bool scheduling_barrier(const machine::Instruction& instruction) const {
        if (instruction.kind != machine::InstructionKind::Target ||
            instruction.may_store || instruction.has_side_effects ||
            instruction.patch) {
            return true;
        }
        if (std::any_of(instruction.defs.begin(), instruction.defs.end(),
                        [](machine::Register value) {
                            return value.kind ==
                                   machine::RegisterKind::Physical;
                        }) ||
            std::any_of(instruction.uses.begin(), instruction.uses.end(),
                        [](machine::Register value) {
                            return value.kind ==
                                   machine::RegisterKind::Physical;
                        })) {
            return true;
        }
        switch (decode_opcode(instruction.opcode)) {
        case Opcode::UnalignedLoadSigned:
        case Opcode::UnalignedLoadUnsigned:
        case Opcode::UnalignedStore:
        case Opcode::AggregateLoad:
        case Opcode::AggregateStore:
        case Opcode::AggregateIndexedLoad:
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
        case Opcode::Invalid: return true;
        default: return false;
        }
    }

    unsigned estimated_latency(const machine::Instruction& instruction) const {
        if (instruction.may_load) {
            return subtarget_.has_feature(Feature::Mips3) ? 3U : 4U;
        }
        switch (decode_opcode(instruction.opcode)) {
        case Opcode::Sdiv:
        case Opcode::Udiv:
        case Opcode::Srem:
        case Opcode::Urem:
        case Opcode::Sdivrem:
        case Opcode::Udivrem: return 36U;
        case Opcode::Fdiv:
            return instruction.defs.empty() ||
                           instruction.defs.front().mode.bits <= 32
                       ? 20U
                       : 36U;
        case Opcode::Mul:
        case Opcode::MulStart: return 8U;
        case Opcode::Fmul:
            return instruction.defs.empty() ||
                           instruction.defs.front().mode.bits <= 32
                       ? 4U
                       : 7U;
        case Opcode::Fadd:
        case Opcode::Fsub: return 4U;
        case Opcode::Fextend:
        case Opcode::Ftruncate:
        case Opcode::Sitofp:
        case Opcode::Uitofp:
        case Opcode::Fptosi:
        case Opcode::Fptoui: return 5U;
        case Opcode::StackAddress:
        case Opcode::GlobalAddress:
        case Opcode::LabelAddress:
        case Opcode::IndexedAddress:
        case Opcode::PointerOffset: return 2U;
        default: return 1U;
        }
    }

    machine::Register create_hilo_dependency(machine::Function& function) {
        const machine::VirtualRegisterId id{
            static_cast<std::uint32_t>(function.virtual_registers.size())};
        function.virtual_registers.push_back(machine::i1);
        function.virtual_register_classes.push_back(
            machine::VirtualRegisterClass::Memory);
        function.virtual_register_assignments.push_back(std::nullopt);
        function.rematerialized_immediates.push_back(std::nullopt);
        return machine::Register::virtual_register(id, machine::i1);
    }

    bool indexed_operation_writes_hilo(
        const machine::Instruction& instruction) const {
        const auto opcode = decode_opcode(instruction.opcode);
        if (opcode != Opcode::IndexedAddress &&
            opcode != Opcode::AggregateIndexedLoad &&
            opcode != Opcode::IndexedLoadSigned &&
            opcode != Opcode::IndexedLoadUnsigned &&
            opcode != Opcode::FindexedLoad) {
            return false;
        }
        if (instruction.operands.empty()) return false;
        const auto* scale = std::get_if<machine::ImmediateOperand>(
            &instruction.operands.back());
        return scale && scale->value > 1 &&
            !std::has_single_bit(scale->value);
    }

    bool atomic_hilo_operation(const machine::Instruction& instruction) const {
        const auto opcode = decode_opcode(instruction.opcode);
        return opcode == Opcode::Mul || opcode == Opcode::Sdiv ||
            opcode == Opcode::Udiv || opcode == Opcode::Srem ||
            opcode == Opcode::Urem || opcode == Opcode::Sdivrem ||
            opcode == Opcode::Udivrem || opcode == Opcode::StackAllocate ||
            indexed_operation_writes_hilo(instruction);
    }

    bool split_hilo_operations(machine::Function& function) {
        bool changed = false;
        for (auto& block : function.blocks) {
            std::optional<machine::Register> completed_hilo;
            std::vector<machine::Instruction> expanded;
            expanded.reserve(block.instructions.size());
            for (auto& instruction : block.instructions) {
                const auto opcode = decode_opcode(instruction.opcode);
                // Original R4000 silicon can misexecute extended or variable
                // shifts while an integer multiply is in flight. Keep the
                // producer/read atomic until that erratum has its own target
                // hazard model; other MIPS-I--III CPUs interlock the result.
                const bool r4000_multiply_erratum =
                    subtarget_.cpu() == "r4000" ||
                    subtarget_.tune() == "r4000";
                const bool native_multiply =
                    opcode == Opcode::Mul && instruction.defs.size() == 1 &&
                    instruction.uses.size() >= 2 &&
                    !r4000_multiply_erratum &&
                    (instruction.defs.front().mode.bits <= 32 ||
                     subtarget_.has_feature(Feature::Mips3));
                if (native_multiply) {
                    const auto result = instruction.defs.front();
                    const auto active = create_hilo_dependency(function);
                    const auto completed = create_hilo_dependency(function);

                    instruction.opcode = Opcode::MulStart;
                    instruction.defs = {active};
                    if (completed_hilo) {
                        instruction.uses.push_back(*completed_hilo);
                    }
                    expanded.push_back(std::move(instruction));

                    auto read = target_instruction(Opcode::MoveFromLo,
                                                   expanded.back().location);
                    read.uses.push_back(active);
                    read.defs = {result, completed};
                    expanded.push_back(std::move(read));
                    completed_hilo = completed;
                    changed = true;
                    continue;
                }
                if (atomic_hilo_operation(instruction)) {
                    if (completed_hilo) {
                        instruction.uses.push_back(*completed_hilo);
                    }
                    const auto completed = create_hilo_dependency(function);
                    instruction.defs.push_back(completed);
                    completed_hilo = completed;
                    changed = true;
                }
                expanded.push_back(std::move(instruction));
            }
            block.instructions = std::move(expanded);
        }
        return changed;
    }

    bool schedule_region(std::vector<machine::Instruction>& instructions,
                         std::size_t begin, std::size_t end,
                         machine::BlockId owner) {
        const auto count = end - begin;
        if (count < 2) return false;

        // Phi updates on a loop backedge are distinct SSA names.  Record the
        // relationship so scheduling cannot move a destructive/coalesced
        // update ahead of a use of the current iteration's phi value.
        std::unordered_map<std::uint32_t, std::uint32_t>
            backedge_phi_sources;
        const auto reaches_owner = [&](machine::BlockId start) {
            std::vector<machine::BlockId> pending{start};
            std::unordered_set<std::uint32_t> visited;
            while (!pending.empty()) {
                const auto block = pending.back();
                pending.pop_back();
                if (block == owner) return true;
                if (!visited.insert(block.value).second ||
                    block.value >= current_.blocks.size()) {
                    continue;
                }
                for (const auto successor :
                     current_.blocks[block.value].successors) {
                    pending.push_back(successor);
                }
            }
            return false;
        };
        if (owner.value < current_.blocks.size()) {
            for (const auto successor :
                 current_.blocks[owner.value].successors) {
                if (!reaches_owner(successor) ||
                    successor.value >= current_.blocks.size()) {
                    continue;
                }
                for (const auto& phi :
                     current_.blocks[successor.value].instructions) {
                    if (decode_opcode(phi.opcode) != Opcode::Phi ||
                        phi.defs.size() != 1 ||
                        phi.defs.front().kind !=
                            machine::RegisterKind::Virtual) {
                        continue;
                    }
                    for (std::size_t operand = 0;
                         operand + 1 < phi.operands.size(); operand += 2) {
                        const auto* edge =
                            std::get_if<machine::BlockOperand>(
                                &phi.operands[operand]);
                        const auto* source =
                            std::get_if<machine::RegisterOperand>(
                                &phi.operands[operand + 1]);
                        if (edge && source && edge->target == owner &&
                            source->value.kind ==
                                machine::RegisterKind::Virtual) {
                            backedge_phi_sources.emplace(
                                source->value.id, phi.defs.front().id);
                        }
                    }
                }
            }
        }

        std::unordered_map<std::uint32_t, std::size_t> definition;
        for (std::size_t index = 0; index < count; ++index) {
            for (const auto& value : instructions[begin + index].defs) {
                if (value.kind == machine::RegisterKind::Virtual) {
                    definition.emplace(value.id, index);
                }
            }
        }
        std::vector<std::vector<std::size_t>> successors(count);
        std::vector<std::size_t> indegree(count);
        std::vector<std::size_t> remaining_uses(
            current_.virtual_registers.size());
        std::vector<bool> region_use(remaining_uses.size());
        for (std::size_t index = 0; index < count; ++index) {
            std::unordered_set<std::size_t> dependencies;
            for (const auto& value : instructions[begin + index].uses) {
                if (value.kind != machine::RegisterKind::Virtual) continue;
                if (value.id < remaining_uses.size()) {
                    ++remaining_uses[value.id];
                    region_use[value.id] = true;
                }
                const auto found = definition.find(value.id);
                if (found != definition.end() && found->second != index) {
                    dependencies.insert(found->second);
                }
            }
            indegree[index] = dependencies.size();
            for (const auto dependency : dependencies) {
                successors[dependency].push_back(index);
            }
        }
        // A definition consumed after this region remains live when the local
        // schedule ends.  Account for one sentinel use so pressure scoring
        // does not incorrectly treat it as a dead temporary.
        std::vector<bool> external_use(remaining_uses.size());
        const auto mark_external_uses = [&](const machine::Instruction& value) {
            for (const auto use : value.uses) {
                if (use.kind == machine::RegisterKind::Virtual &&
                    use.id < external_use.size()) {
                    external_use[use.id] = true;
                }
            }
        };
        for (std::size_t index = end; index < instructions.size(); ++index) {
            mark_external_uses(instructions[index]);
        }
        for (const auto& block : current_.blocks) {
            if (block.id == owner) continue;
            for (const auto& value : block.instructions) {
                mark_external_uses(value);
            }
        }
        for (std::size_t id = 0; id < external_use.size(); ++id) {
            if (external_use[id]) ++remaining_uses[id];
        }
        const auto initial_remaining_uses = remaining_uses;
        for (const auto& [update_value, phi_value] :
             backedge_phi_sources) {
            const auto update = definition.find(update_value);
            if (update == definition.end()) continue;
            for (std::size_t index = 0; index < update->second; ++index) {
                const bool uses_phi = std::any_of(
                    instructions[begin + index].uses.begin(),
                    instructions[begin + index].uses.end(),
                    [&](machine::Register use) {
                        return use.kind == machine::RegisterKind::Virtual &&
                               use.id == phi_value;
                    });
                if (!uses_phi ||
                    std::find(successors[index].begin(),
                              successors[index].end(), update->second) !=
                        successors[index].end()) {
                    continue;
                }
                successors[index].push_back(update->second);
                ++indegree[update->second];
            }
        }

        std::vector<unsigned> critical_height(count);
        for (std::size_t index = count; index-- > 0;) {
            unsigned successor_height{};
            for (const auto successor : successors[index]) {
                successor_height = std::max(
                    successor_height, critical_height[successor]);
            }
            critical_height[index] =
                estimated_latency(instructions[begin + index]) +
                successor_height;
        }

        std::vector<bool> prefer_early(count);
        for (std::size_t index = 0; index < count; ++index) {
            prefer_early[index] = std::any_of(
                instructions[begin + index].defs.begin(),
                instructions[begin + index].defs.end(),
                [&](machine::Register value) {
                    return value.kind == machine::RegisterKind::Virtual &&
                           backedge_phi_sources.contains(value.id);
                });
        }

        const bool compact =
            options_.optimize_for == OptimizationGoal::Size ||
            options_.optimize_for == OptimizationGoal::MinimumSize;
        const bool speed =
            options_.optimize_for == OptimizationGoal::Speed;
        const auto pressure_weight = [&](std::uint32_t id) {
            if (id < current_.virtual_register_classes.size()) {
                const auto register_class =
                    current_.virtual_register_classes[id];
                // Memory-class virtuals include effect and architectural-
                // resource tokens. They impose ordering through dependencies
                // but consume no allocatable register.
                if (register_class ==
                        machine::VirtualRegisterClass::Memory) {
                    return 0;
                }
                if (compact || !speed) return compact ? 40 : 24;
                if (register_class ==
                        machine::VirtualRegisterClass::Floating) {
                    // A latency-bearing floating operation commonly has
                    // several independent unrolled lanes available. Keeping
                    // every lane in one short-lived temporary serializes those
                    // chains.
                    return 4;
                }
                if (register_class ==
                        machine::VirtualRegisterClass::Integer) {
                    // Integer address and HI/LO chains have shorter latency,
                    // so retain more pressure bias than floating point while
                    // still allowing independent loads/shifts to start early.
                    return 8;
                }
            }
            // Hard per-bank budgets and the post-schedule profitability check
            // below remain authoritative; this weight only chooses among
            // otherwise-ready nodes.
            return compact ? 40 : 24;
        };
        std::vector<bool> emitted(count);
        std::vector<std::size_t> order;
        order.reserve(count);
        std::vector<bool> scheduled_live(remaining_uses.size());
        std::array<unsigned, 4> scheduled_live_counts{};
        for (std::size_t id = 0; id < scheduled_live.size(); ++id) {
            scheduled_live[id] = region_use[id] &&
                !definition.contains(static_cast<std::uint32_t>(id));
            if (!scheduled_live[id] ||
                id >= current_.virtual_register_classes.size()) {
                continue;
            }
            const auto bank = static_cast<std::size_t>(
                current_.virtual_register_classes[id]);
            if (bank < scheduled_live_counts.size()) {
                ++scheduled_live_counts[bank];
            }
        }
        // Leave two colors for loop-PHI interference and emitter staging that
        // are not visible in a single straight-line scheduling region.
        constexpr unsigned schedulable_gpr_peak = 17U;
        constexpr unsigned schedulable_fpr_peak = 11U;
        const auto projected_pressure = [&](std::size_t index) {
            auto projected = scheduled_live_counts;
            std::unordered_map<std::uint32_t, unsigned> uses;
            for (const auto use : instructions[begin + index].uses) {
                if (use.kind == machine::RegisterKind::Virtual &&
                    use.id < remaining_uses.size()) {
                    ++uses[use.id];
                }
            }
            for (const auto [id, consumed] : uses) {
                if (!scheduled_live[id] ||
                    remaining_uses[id] > consumed ||
                    id >= current_.virtual_register_classes.size()) {
                    continue;
                }
                const auto bank = static_cast<std::size_t>(
                    current_.virtual_register_classes[id]);
                if (bank < projected.size() && projected[bank] != 0) {
                    --projected[bank];
                }
            }
            for (const auto definition_value :
                 instructions[begin + index].defs) {
                if (definition_value.kind !=
                        machine::RegisterKind::Virtual ||
                    definition_value.id >= remaining_uses.size() ||
                    remaining_uses[definition_value.id] == 0 ||
                    scheduled_live[definition_value.id] ||
                    definition_value.id >=
                        current_.virtual_register_classes.size()) {
                    continue;
                }
                const auto bank = static_cast<std::size_t>(
                    current_.virtual_register_classes[
                        definition_value.id]);
                if (bank < projected.size()) ++projected[bank];
            }
            return projected;
        };
        unsigned hilo_write_gap{};
        unsigned hilo_result_gap{};
        const auto opcode_at = [&](std::size_t index) {
            return decode_opcode(instructions[begin + index].opcode);
        };
        const auto starts_hilo_operation = [&](std::size_t index) {
            const auto opcode = opcode_at(index);
            return opcode == Opcode::MulStart ||
                atomic_hilo_operation(instructions[begin + index]);
        };
        const auto reads_split_hilo = [&](std::size_t index) {
            return opcode_at(index) == Opcode::MoveFromLo;
        };
        const auto completes_atomic_hilo = [&](std::size_t index) {
            const auto opcode =
                decode_opcode(instructions[begin + index].opcode);
            return opcode != Opcode::MulStart &&
                atomic_hilo_operation(instructions[begin + index]);
        };
        while (order.size() != count) {
            std::optional<std::size_t> best;
            int best_score = std::numeric_limits<int>::min();
            const bool ready_non_hilo_writer = [&] {
                for (std::size_t index = 0; index < count; ++index) {
                    if (!emitted[index] && indegree[index] == 0 &&
                        !starts_hilo_operation(index)) {
                        return true;
                    }
                }
                return false;
            }();
            const bool ready_non_hilo_read = [&] {
                for (std::size_t index = 0; index < count; ++index) {
                    if (!emitted[index] && indegree[index] == 0 &&
                        !reads_split_hilo(index)) {
                        return true;
                    }
                }
                return false;
            }();
            const auto floating_bank = static_cast<std::size_t>(
                machine::VirtualRegisterClass::Floating);
            const auto integer_bank = static_cast<std::size_t>(
                machine::VirtualRegisterClass::Integer);
            const auto integer_limit = std::max(
                schedulable_gpr_peak,
                scheduled_live_counts[integer_bank]);
            const auto floating_limit = std::max(
                schedulable_fpr_peak,
                scheduled_live_counts[floating_bank]);
            const bool ready_within_pressure_budget = [&] {
                for (std::size_t index = 0; index < count; ++index) {
                    if (emitted[index] || indegree[index] != 0) continue;
                    const auto projected = projected_pressure(index);
                    if (projected[integer_bank] <= integer_limit &&
                        projected[floating_bank] <= floating_limit) {
                        return true;
                    }
                }
                return false;
            }();
            for (std::size_t index = 0; index < count; ++index) {
                if (emitted[index] || indegree[index] != 0) continue;
                const auto& candidate = instructions[begin + index];
                int pressure_delta{};
                for (const auto definition_value : candidate.defs) {
                    if (definition_value.kind ==
                            machine::RegisterKind::Virtual &&
                        definition_value.id < remaining_uses.size() &&
                        remaining_uses[definition_value.id] != 0) {
                        pressure_delta -= pressure_weight(definition_value.id);
                    }
                }
                std::unordered_map<std::uint32_t, unsigned> consumed_uses;
                for (const auto& use : candidate.uses) {
                    if (use.kind == machine::RegisterKind::Virtual &&
                        use.id < remaining_uses.size()) {
                        ++consumed_uses[use.id];
                    }
                }
                for (const auto [id, consumed] : consumed_uses) {
                    if (remaining_uses[id] <= consumed) {
                        pressure_delta += pressure_weight(id);
                    }
                }
                // VR4300 has a small allocatable bank and spilling costs more
                // instructions than a modest latency win can repay.  Prefer a
                // ready node that closes live ranges; use critical height to
                // choose among schedules with comparable pressure.
                int score = static_cast<int>(critical_height[index]) *
                                (compact ? 1 : 2) + pressure_delta;
                if (prefer_early[index]) score += compact ? 1 : 4;
                // MFHI/MFLO on pre-interlocked MIPS forbids a following
                // HI/LO writer for two architectural instructions. Prefer
                // ready scalar work during that window so the emitter does
                // not have to repair the schedule with NOPs.
                if (hilo_write_gap != 0 && ready_non_hilo_writer &&
                    starts_hilo_operation(index)) {
                    score -= 10000;
                }
                // A split MFLO is data-ready only after the multiply latency.
                // Prefer independent ready work during that interval.  The
                // dependency token still forces the read after its writer and
                // serializes every other use of the architectural HI/LO pair.
                if (hilo_result_gap != 0 && ready_non_hilo_read &&
                    reads_split_hilo(index)) {
                    score -= 10000;
                }
                const auto projected = projected_pressure(index);
                if (ready_within_pressure_budget &&
                    (projected[integer_bank] > integer_limit ||
                     projected[floating_bank] > floating_limit)) {
                    score -= 10000;
                }
                const auto opcode = decode_opcode(candidate.opcode);
                if (opcode == Opcode::Constant ||
                    opcode == Opcode::Fconstant ||
                    opcode == Opcode::StackAddress ||
                    opcode == Opcode::GlobalAddress ||
                    opcode == Opcode::LabelAddress) {
                    score -= 2;
                }
                if (!best || score > best_score ||
                    (score == best_score && index < *best)) {
                    best = index;
                    best_score = score;
                }
            }
            // A missing ready node means a malformed dependence graph; keep
            // the original order and let the verifier diagnose the function.
            if (!best) return false;
            emitted[*best] = true;
            order.push_back(*best);
            if (opcode_at(*best) == Opcode::MulStart) {
                // A remaining post-MFLO exclusion is paid as hardware/emitter
                // stalls when no independent node can fill it.  It no longer
                // applies after this writer has issued.
                hilo_write_gap = 0;
                hilo_result_gap = estimated_latency(
                    instructions[begin + *best]);
            } else if (reads_split_hilo(*best)) {
                hilo_result_gap = 0;
                hilo_write_gap =
                    subtarget_.has_feature(Feature::HiloInterlocks) ? 0U : 2U;
            } else {
                if (hilo_result_gap != 0) --hilo_result_gap;
                if (completes_atomic_hilo(*best)) {
                    hilo_write_gap =
                        subtarget_.has_feature(Feature::HiloInterlocks)
                        ? 0U : 2U;
                } else if (hilo_write_gap != 0) {
                    --hilo_write_gap;
                }
            }
            for (const auto& use : instructions[begin + *best].uses) {
                if (use.kind == machine::RegisterKind::Virtual &&
                    use.id < remaining_uses.size() &&
                    remaining_uses[use.id] != 0) {
                    --remaining_uses[use.id];
                    if (remaining_uses[use.id] == 0 &&
                        scheduled_live[use.id] &&
                        use.id < current_.virtual_register_classes.size()) {
                        scheduled_live[use.id] = false;
                        const auto bank = static_cast<std::size_t>(
                            current_.virtual_register_classes[use.id]);
                        if (bank < scheduled_live_counts.size() &&
                            scheduled_live_counts[bank] != 0) {
                            --scheduled_live_counts[bank];
                        }
                    }
                }
            }
            for (const auto definition_value :
                 instructions[begin + *best].defs) {
                if (definition_value.kind !=
                        machine::RegisterKind::Virtual ||
                    definition_value.id >= remaining_uses.size() ||
                    remaining_uses[definition_value.id] == 0 ||
                    scheduled_live[definition_value.id] ||
                    definition_value.id >=
                        current_.virtual_register_classes.size()) {
                    continue;
                }
                scheduled_live[definition_value.id] = true;
                const auto bank = static_cast<std::size_t>(
                    current_.virtual_register_classes[
                        definition_value.id]);
                if (bank < scheduled_live_counts.size()) {
                    ++scheduled_live_counts[bank];
                }
            }
            for (const auto successor : successors[*best]) {
                if (indegree[successor] != 0) --indegree[successor];
            }
        }

        bool changed = false;
        for (std::size_t index = 0; index < count; ++index) {
            changed = changed || order[index] != index;
        }
        if (!changed) return false;

        struct Pressure {
            std::array<unsigned, 4> peak{};
            std::array<std::uint64_t, 4> area{};
        };
        const auto measure_pressure = [&](const std::vector<std::size_t>& plan) {
            Pressure pressure;
            auto uses = initial_remaining_uses;
            std::vector<bool> live(uses.size());
            for (std::size_t id = 0; id < live.size(); ++id) {
                live[id] = region_use[id] && !definition.contains(
                                                   static_cast<std::uint32_t>(id));
            }
            const auto record = [&] {
                std::array<unsigned, 4> counts{};
                for (std::size_t id = 0; id < live.size(); ++id) {
                    if (!live[id] ||
                        id >= current_.virtual_register_classes.size()) {
                        continue;
                    }
                    const auto bank = static_cast<std::size_t>(
                        current_.virtual_register_classes[id]);
                    if (bank < counts.size()) ++counts[bank];
                }
                for (std::size_t bank = 0; bank < counts.size(); ++bank) {
                    pressure.peak[bank] = std::max(pressure.peak[bank],
                                                   counts[bank]);
                    pressure.area[bank] += counts[bank];
                }
            };
            record();
            for (const auto relative : plan) {
                const auto& value = instructions[begin + relative];
                for (const auto use : value.uses) {
                    if (use.kind != machine::RegisterKind::Virtual ||
                        use.id >= uses.size() || uses[use.id] == 0) {
                        continue;
                    }
                    --uses[use.id];
                    if (uses[use.id] == 0) live[use.id] = false;
                }
                for (const auto definition_value : value.defs) {
                    if (definition_value.kind ==
                            machine::RegisterKind::Virtual &&
                        definition_value.id < uses.size() &&
                        uses[definition_value.id] != 0) {
                        live[definition_value.id] = true;
                    }
                }
                record();
            }
            return pressure;
        };
        std::vector<std::size_t> original(count);
        for (std::size_t index = 0; index < count; ++index) {
            original[index] = index;
        }
        const auto original_pressure = measure_pressure(original);
        const auto scheduled_pressure = measure_pressure(order);
        const auto estimated_cycles = [&](const std::vector<std::size_t>& plan) {
            std::vector<unsigned> ready(count);
            unsigned next_issue{};
            unsigned completion{};
            for (const auto index : plan) {
                unsigned issue = next_issue;
                for (const auto use : instructions[begin + index].uses) {
                    if (use.kind != machine::RegisterKind::Virtual) continue;
                    const auto producer = definition.find(use.id);
                    if (producer != definition.end() &&
                        producer->second != index) {
                        issue = std::max(issue, ready[producer->second]);
                    }
                }
                ready[index] = issue +
                    estimated_latency(instructions[begin + index]);
                next_issue = issue + 1;
                completion = std::max(completion, ready[index]);
            }
            return completion;
        };
        const auto original_cycles = estimated_cycles(original);
        const auto scheduled_cycles = estimated_cycles(order);
        const auto hilo_stalls = [&](const std::vector<std::size_t>& plan) {
            unsigned write_gap{};
            unsigned result_gap{};
            unsigned stalls{};
            for (const auto index : plan) {
                if (starts_hilo_operation(index)) {
                    stalls += write_gap;
                    write_gap = 0;
                    if (opcode_at(index) == Opcode::MulStart) {
                        result_gap = estimated_latency(
                            instructions[begin + index]);
                    } else if (completes_atomic_hilo(index)) {
                        write_gap = subtarget_.has_feature(
                            Feature::HiloInterlocks) ? 0U : 2U;
                    }
                } else if (reads_split_hilo(index)) {
                    stalls += result_gap;
                    result_gap = 0;
                    write_gap = subtarget_.has_feature(
                        Feature::HiloInterlocks) ? 0U : 2U;
                } else {
                    if (result_gap != 0) --result_gap;
                    if (completes_atomic_hilo(index)) {
                        write_gap = subtarget_.has_feature(
                            Feature::HiloInterlocks) ? 0U : 2U;
                    } else if (write_gap != 0) {
                        --write_gap;
                    }
                }
            }
            return stalls;
        };
        const bool removes_hilo_stalls =
            speed &&
            hilo_stalls(order) < hilo_stalls(original);
        for (std::size_t bank = 0;
             bank < original_pressure.peak.size(); ++bank) {
            unsigned latency_allowance{};
            if (speed &&
                bank == static_cast<std::size_t>(
                            machine::VirtualRegisterClass::Integer) &&
                scheduled_cycles < original_cycles) {
                const auto available =
                    original_pressure.peak[bank] < schedulable_gpr_peak
                    ? schedulable_gpr_peak - original_pressure.peak[bank]
                    : 0U;
                auto repaid = original_cycles - scheduled_cycles;
                if (removes_hilo_stalls) repaid = std::max(1U, repaid);
                latency_allowance = std::min(available, repaid);
            }
            if (speed &&
                bank == static_cast<std::size_t>(
                            machine::VirtualRegisterClass::Floating) &&
                scheduled_cycles < original_cycles) {
                const auto available =
                    original_pressure.peak[bank] < schedulable_fpr_peak
                    ? schedulable_fpr_peak -
                          original_pressure.peak[bank]
                    : 0U;
                const auto repaid =
                    (original_cycles - scheduled_cycles) / 2U;
                latency_allowance = std::min(available, repaid);
            }
            if (scheduled_pressure.peak[bank] >
                original_pressure.peak[bank] + latency_allowance) {
                return false;
            }
        }
        std::vector<machine::Instruction> scheduled;
        scheduled.reserve(count);
        for (const auto index : order) {
            scheduled.push_back(std::move(instructions[begin + index]));
        }
        std::move(scheduled.begin(), scheduled.end(),
                  instructions.begin() +
                      static_cast<std::ptrdiff_t>(begin));
        return true;
    }

    bool schedule_instructions(machine::Function& function) {
        if (!options_.schedule_insns) return false;
        bool changed = split_hilo_operations(function);
        for (auto& block : function.blocks) {
            std::size_t begin{};
            while (begin < block.instructions.size()) {
                if (scheduling_barrier(block.instructions[begin])) {
                    ++begin;
                    continue;
                }
                auto end = begin + 1;
                while (end < block.instructions.size() &&
                       !scheduling_barrier(block.instructions[end])) {
                    ++end;
                }
                changed = schedule_region(block.instructions, begin, end,
                                          block.id) || changed;
                begin = end;
            }
        }
        return changed;
    }

    bool fuse_compare_branches(machine::Function& function) {
        if (!options_.compare_branch_fusion) return false;
        std::vector<unsigned> uses(function.virtual_registers.size());
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto use : instruction.uses) {
                    if (use.kind == machine::RegisterKind::Virtual &&
                        use.id < uses.size()) {
                        ++uses[use.id];
                    }
                }
            }
        }
        bool changed = false;
        for (auto& block : function.blocks) {
            if (block.instructions.empty()) continue;
            const auto terminator_index = block.instructions.size() - 1;
            auto& terminator = block.instructions[terminator_index];
            if (terminator.kind !=
                    machine::InstructionKind::ConditionalBranch ||
                !terminator.condition_predicate.empty() ||
                terminator.uses.size() != 1 ||
                terminator.operands.size() < 3) {
                continue;
            }
            const auto edge_has_phi = [&](const machine::Operand& operand) {
                const auto* edge = std::get_if<machine::BlockOperand>(&operand);
                if (!edge) return true;
                const auto successor = std::find_if(
                    function.blocks.begin(), function.blocks.end(),
                    [&](const machine::Block& candidate) {
                        return candidate.id == edge->target;
                    });
                return successor != function.blocks.end() &&
                    std::any_of(
                        successor->instructions.begin(),
                        successor->instructions.end(),
                        [](const machine::Instruction& instruction) {
                            return instruction.opcode == Opcode::Phi;
                        });
            };
            const bool yes_has_phi = edge_has_phi(terminator.operands[1]);
            const bool no_has_phi = edge_has_phi(terminator.operands[2]);
            const auto condition = terminator.uses.front();
            if (condition.kind != machine::RegisterKind::Virtual ||
                condition.id >= uses.size() || uses[condition.id] != 1) {
                continue;
            }
            const auto found = std::find_if(
                block.instructions.begin(),
                block.instructions.begin() +
                    static_cast<std::ptrdiff_t>(terminator_index),
                [&](const machine::Instruction& candidate) {
                    return candidate.defs.size() == 1 &&
                           candidate.defs.front() == condition;
                });
            if (found == block.instructions.begin() +
                             static_cast<std::ptrdiff_t>(terminator_index) ||
                found->kind != machine::InstructionKind::Target ||
                found->uses.empty() || found->patch || found->may_load ||
                found->may_store || found->has_side_effects) {
                continue;
            }
            const auto opcode = decode_opcode(found->opcode);
            const bool integer = opcode == Opcode::Iszero ||
                (opcode >= Opcode::CmpEq && opcode <= Opcode::CmpUge);
            const bool floating = opcode == Opcode::Fiszero ||
                (opcode >= Opcode::FcmpEq && opcode <= Opcode::FcmpGe);
            if ((!integer && !floating) ||
                ((yes_has_phi || no_has_phi) &&
                 opcode != Opcode::CmpEq && opcode != Opcode::CmpNe) ||
                (!subtarget_.has_feature(Feature::Mips3) &&
                 found->uses.front().mode.bits > 32) ||
                (floating && std::next(found) !=
                                 block.instructions.begin() +
                                     static_cast<std::ptrdiff_t>(
                                         terminator_index))) {
                continue;
            }
            terminator.condition_predicate = found->opcode;
            terminator.uses = found->uses;
            block.instructions.erase(found);
            changed = true;
        }
        return changed;
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
            {{LoweringPass::FoldBoundaryProjections},
             Stage::InstructionCombining, "fold-boundary-projections"},
            [this](machine::Function& function) {
                return fold_boundary_projections(function);
            });
        passes.add(
            {{LoweringPass::SelectIntegerImmediates},
             Stage::InstructionCombining, "select-integer-immediates"},
            [this](machine::Function& function) {
                return select_integer_immediates(function);
            });
        passes.add(
            {{LoweringPass::FoldPointerOffsets},
              Stage::InstructionCombining, "fold-pointer-offsets"},
            [this](machine::Function& function) {
                return fold_pointer_offsets(function);
            });
        passes.add(
            {{LoweringPass::EliminateRedundantExpressions},
             Stage::Canonicalization, "eliminate-redundant-expressions"},
            [this](machine::Function& function) {
                if (!options_.machine_cse) return false;
                const auto eligible = [](const machine::Instruction& value) {
                    switch (decode_opcode(value.opcode)) {
                    case Opcode::UnalignedLoadSigned:
                    case Opcode::UnalignedLoadUnsigned:
                    case Opcode::UnalignedStore:
                    case Opcode::AggregateLoad:
                    case Opcode::AggregateStore:
                    case Opcode::AggregateIndexedLoad:
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
            {{LoweringPass::FuseDivisionResults},
              Stage::InstructionCombining, "fuse-division-results"},
            [this](machine::Function& function) {
                if (!options_.machine_combine) return false;
                const unsigned native_bits =
                    subtarget_.has_feature(Feature::Mips3) ? 64U : 32U;
                return native::fuse_division_results(function,
                    [&](const machine::Instruction& instruction)
                        -> std::optional<native::DivisionKind> {
                        const auto opcode = decode_opcode(instruction.opcode);
                        if (instruction.uses.empty() ||
                            instruction.uses.front().mode.bits > native_bits ||
                            (opcode != Opcode::Sdiv && opcode != Opcode::Udiv &&
                             opcode != Opcode::Srem && opcode != Opcode::Urem))
                            return std::nullopt;
                        return native::DivisionKind{
                            opcode == Opcode::Sdiv || opcode == Opcode::Udiv,
                            opcode == Opcode::Sdiv || opcode == Opcode::Srem};
                    },
                    Opcode::Sdivrem, Opcode::Udivrem);
            });
        passes.add(
            {{LoweringPass::FuseCompareBranches},
              Stage::InstructionCombining, "fuse-compare-branches"},
            [this](machine::Function& function) {
                return fuse_compare_branches(function);
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
            {{LoweringPass::ScheduleBlockLayout}, Stage::Scheduling,
              "schedule-block-layout"},
            [this](machine::Function& function) {
                if (!options_.reorder_blocks) return false;
                return native::schedule_block_layout(
                    function, [&](machine::BlockId block) {
                        return std::any_of(
                            function.labels.begin(), function.labels.end(),
                            [&](const machine::Function::LocalLabel& label) {
                                return label.block == block;
                            });
                    });
            });
        passes.add(
            {{LoweringPass::ScheduleInstructions}, Stage::Scheduling,
              "schedule-instructions"},
            [this](machine::Function& function) {
                return schedule_instructions(function);
            });
        passes.add(
            {{LoweringPass::SplitEntryParameters}, Stage::RegisterAllocation,
              "split-entry-parameters"},
            [this](machine::Function& function) {
                return split_entry_parameters(function);
            });
        passes.add(
            {{LoweringPass::AllocateRegisters}, Stage::RegisterAllocation,
              "allocate-registers"},
            [this](machine::Function& function) {
                return allocate_registers(function);
            });
        passes.add(
            {{LoweringPass::ElideUnusedSpillSlots},
             Stage::FrameFinalization, "elide-unused-spill-slots"},
            [](machine::Function& function) {
                return native::elide_unused_virtual_spill_slots(function);
            });
        (void)passes.run(current_);
    }

    // A parameter that crosses calls only below one branch of the entry
    // block is copied there, so the entry block can leave it in a volatile
    // register and stay outside a shrink-wrapped frame. Only values the
    // allocator can color are split.
    bool split_entry_parameters(machine::Function& function) const {
        const auto& entity = hir_.function(function.source);
        if (!options_.shrink_wrap || !options_.register_allocation ||
            options_.unwind_model != UnwindModel::None ||
            options_.unwind_tables || options_.asynchronous_unwind_tables ||
            function.frame.elide_incoming_saves ||
            std::any_of(entity.parameters.begin(), entity.parameters.end(),
                        [](const hir::Parameter& parameter) {
                            return parameter.mode != ParameterMode::In;
                        })) {
            return false;
        }
        // Incoming stack pieces keep the prologue at entry anyway.
        const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
        const auto layout =
            abi ? classify_function_interface(entity, *abi) : std::nullopt;
        if (!layout || !register_interface(*layout)) return false;
        // Without a preserved register of its class, a parameter crossing a
        // call never makes the entry block need the frame; a copy only costs.
        const auto preserves =
            [&](std::initializer_list<std::string_view> names) {
                return std::any_of(
                    names.begin(), names.end(), [&](std::string_view name) {
                        const auto clobbers =
                            [&](const std::vector<std::string>& list) {
                                return std::find(list.begin(), list.end(),
                                                 name) != list.end();
                            };
                        return !(abi && clobbers(abi->call_clobbers)) &&
                               !clobbers(entity.clobbers);
                    });
            };
        const bool integer_preserved =
            preserves({"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"});
        const bool floating_preserved =
            preserves({"f20", "f22", "f24", "f26", "f28", "f30"});
        return machine::split_entry_parameters(
            function,
            [&](const machine::Instruction& instruction) {
                const auto opcode = decode_opcode(instruction.opcode);
                if ((opcode != Opcode::Parameter &&
                     opcode != Opcode::Fparameter) ||
                    instruction.defs.size() != 1 ||
                    instruction.defs.front().kind !=
                        machine::RegisterKind::Virtual) {
                    return false;
                }
                const auto value = instruction.defs.front();
                const auto kind = function.virtual_register_classes[value.id];
                return (integer_preserved &&
                        kind == machine::VirtualRegisterClass::Integer &&
                        (value.mode.bits <= 32 ||
                         subtarget_.has_feature(Feature::Mips3))) ||
                       (floating_preserved &&
                        kind == machine::VirtualRegisterClass::Floating &&
                        value.mode.bits <= 64 &&
                        subtarget_.has_feature(Feature::Mips3) &&
                        subtarget_.has_feature(Feature::HardFloat));
            },
            phi_instruction);
    }

    void record_private_clobbers(const machine::Function& function) {
        const auto& entity = hir_.function(function.source);
        if (!options_.ipa_ra || !options_.private_abi ||
            entity.abi_contract != hir::AbiContract::Dynamic ||
            !subtarget_.has_feature(Feature::Mips3)) {
            return;
        }

        std::unordered_set<std::uint32_t> preserved;
        for (const auto physical : function.callee_saved_registers) {
            preserved.insert(physical.value);
        }
        std::unordered_set<std::uint32_t> clobbered;
        const auto append = [&](machine::PhysicalRegisterId physical) {
            if (!preserved.contains(physical.value)) {
                clobbered.insert(physical.value);
            }
        };
        const auto append_name = [&](std::string_view name) {
            if (const auto physical = physical_register_id(name)) {
                append(*physical);
            }
        };

        // These registers are reserved from allocation because emitter
        // expansions may use them as scratch. Advertising them is harmless
        // to callers and keeps the summary sound without duplicating every
        // target expansion's internal sequence here.
        for (const auto name : {"at", "t0", "t1", "t2", "t3", "ra",
                                "f0", "f2", "f4"}) {
            append_name(name);
        }
        for (const auto assignment :
             function.virtual_register_assignments) {
            if (assignment) append(*assignment);
        }
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& definition : instruction.defs) {
                    if (definition.kind ==
                        machine::RegisterKind::Physical) {
                        append({definition.id});
                    }
                }
                for (const auto& clobber : instruction.clobbers) {
                    if (clobber.kind == machine::RegisterKind::Physical) {
                        append({clobber.id});
                    }
                }
                if (decode_opcode(instruction.opcode) ==
                    Opcode::AtomicCompareExchange) {
                    append_name("t4");
                    append_name("t5");
                }
            }
        }
        for (const auto& name : entity.clobbers) append_name(name);

        if (const auto* abi = managed_abi_model(
                hir_, entity, subtarget_, options_)) {
            if (const auto signature =
                    classify_function_interface(entity, *abi)) {
                for (const auto& result : signature->results) {
                    for (const auto& piece : result.pieces) {
                        if (piece.location.kind == LocationKind::Register) {
                            append_name(piece.location.reg);
                        }
                    }
                }
            }
        }

        std::vector<machine::PhysicalRegisterId> summary;
        summary.reserve(clobbered.size());
        for (const auto id : clobbered) summary.push_back({id});
        std::sort(summary.begin(), summary.end(),
                  [](machine::PhysicalRegisterId left,
                     machine::PhysicalRegisterId right) {
                      return left.value < right.value;
                  });
        private_clobbers_.insert_or_assign(entity.id.value,
                                            std::move(summary));
    }

    machine::Function lower_function(const mir::ManagedFunction& source) {
        current_ = {};
        source_ = &source;
        const auto& entity = hir_.function(source.source);
        current_.source = source.source;
        current_.source_entry = source.entry;
        current_.location = source.location;
        current_.symbol = entity.link_symbol;
        const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
        current_.abi = abi ? abi->id : entity.abi;
        current_.entry = {source.entry.value};
        current_.frame.stack_alignment =
            std::max(1U, subtarget_.abi_info().stack_alignment);
        current_.frame.outgoing_argument_alignment =
            current_.frame.stack_alignment;
        current_.frame.has_frame_pointer = true;
        current_.frame.elide_incoming_saves = elide_noreturn_saves(entity);
        create_registers(source);
        create_stack_slots(source);
        lower_blocks(source);
        for (const auto& label : source.labels) {
            current_.labels.push_back({label.label, {label.block.value}});
        }
        optimize_machine_function();
        record_private_clobbers(current_);
        return std::move(current_);
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
    std::unordered_map<std::uint32_t,
                       std::vector<machine::PhysicalRegisterId>>
        private_clobbers_;
};

} // namespace

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
          diagnostics_(diagnostics), format_(subtarget.object_format()),
          // A 64-bit address model produces ELF64 objects, which assemblers
          // read with the n64 register names: $t0-$t3 denote registers 12-15
          // and 8-11 are $a4-$a7.  Numeric GPR operands read identically
          // under every naming convention; FPR names are shared.
          numeric_gprs_(hir_module.address_bits > 32),
          wide_addresses_(hir_module.address_bits > 32) {}

    void prepare_frames() {
        if (options_.unwind_tables || options_.asynchronous_unwind_tables ||
            options_.unwind_model != UnwindModel::None) return;
        for (auto& function : module_.functions) {
            // Dynamic SP effects are not yet part of the pinned fixed-frame
            // program contract. Retain the verified MIR marks and late frame
            // emitter, with a stable FP for every fixed home.
            if (has_dynamic_stack(function)) continue;
            active_signature_ = classify_entity(hir_.function(function.source), function.location);
            if (!active_signature_) continue;
            prepare_parameter_homes(function);
            if (!finalize_frame(function)) continue;
            prepare_fixed_frame(function);
            if (can_shrink_wrap(function)) {
                function.frame.prologue_block = machine::place_prologue(
                    function, machine::frame_blocks(function, phi_instruction));
            }
        }
        active_signature_.reset();
    }

    std::string run() {
        if (format_ != ObjectFormat::Elf && !module_.functions.empty()) {
            diagnostics_.error(
                module_.functions.front().location,
                "native MIPS assembly currently requires an ELF target");
            return {};
        }
        if (elf_abi_tag(subtarget_.abi_info()) == ElfAbiTag::Eabi32) {
            // GNU MIPS linkers use this conventional empty marker in addition
            // to EF_MIPS_ABI_EABI32.  LLVM MC accepts the section even though
            // it cannot infer EABI32 from the PSP triple by itself.
            output_ << ".section .mdebug.eabi32\n.previous\n"
                       ".section .gcc_compiled_long32\n.previous\n";
        }
        for (auto& function : module_.functions) emit_function(function);
        emit_literal_pools();
        return output_.str();
    }

private:
    struct ActiveSignature {
        const AbiEntry* abi{};
        SignatureLayout layout;
    };

    struct SuccessorDelayEntry {
        machine::BlockId successor;
        std::size_t instruction{};
        std::string resume_label;
        bool fused_compare{};
        bool elide_original{};
    };

    struct FloatLiteral {
        std::uint64_t bits{};
        unsigned width{};
        std::string label;
    };

    struct IntegerLiteral {
        std::uint64_t bits{};
        std::string label;
    };

    bool prefer_float_literal(
        const machine::ImmediateOperand& immediate) const {
        if (!options_.machine_combine || immediate.mode.bits != 64 ||
            (options_.optimize_for != OptimizationGoal::Size &&
             options_.optimize_for != OptimizationGoal::MinimumSize)) {
            return false;
        }
        const auto signed_low = static_cast<std::int64_t>(immediate.value);
        if (signed_low >= std::numeric_limits<std::int16_t>::min() &&
            signed_low <= std::numeric_limits<std::int16_t>::max()) {
            return false;
        }
        unsigned nonzero_halfwords = 0;
        for (unsigned shift = 0; shift < 64; shift += 16) {
            if (((immediate.value >> shift) & 0xffffU) != 0) {
                ++nonzero_halfwords;
            }
        }
        // Two relocation-bearing instructions plus eight pool bytes cost 16
        // bytes. Three or more populated chunks require strictly more space
        // for DLI followed by a GPR-to-FPR transfer.
        return nonzero_halfwords >= 3;
    }

    const FloatLiteral& float_literal(
        const machine::ImmediateOperand& immediate) {
        const auto found = std::find_if(
            float_literals_.begin(), float_literals_.end(),
            [&](const FloatLiteral& candidate) {
                return candidate.bits == immediate.value &&
                       candidate.width == immediate.mode.bits;
            });
        if (found != float_literals_.end()) return *found;
        const auto index = float_literals_.size();
        float_literals_.push_back(
            {immediate.value, immediate.mode.bits,
             ".Lcross.mips.float." + std::to_string(index)});
        return float_literals_.back();
    }

    bool prefer_integer_literal(
        const machine::ImmediateOperand& immediate) const {
        if (!options_.machine_combine || immediate.mode.bits != 64 ||
            large_code_model() ||
            (options_.optimize_for != OptimizationGoal::Size &&
             options_.optimize_for != OptimizationGoal::MinimumSize)) {
            return false;
        }
        // A pool reference costs two instructions and eight data bytes. Keep
        // the transformation strictly profitable even after charging the
        // data by accepting only constants whose four populated chunks need
        // a full DLI construction. Leading 0xffff is excluded because GNU
        // as can sometimes exploit sign extension for that shape.
        for (unsigned shift = 0; shift < 64; shift += 16) {
            if (((immediate.value >> shift) & 0xffffU) == 0) return false;
        }
        return (immediate.value >> 48U) != 0xffffU;
    }

    const IntegerLiteral& integer_literal(
        const machine::ImmediateOperand& immediate) {
        const auto found = std::find_if(
            integer_literals_.begin(), integer_literals_.end(),
            [&](const IntegerLiteral& candidate) {
                return candidate.bits == immediate.value;
            });
        if (found != integer_literals_.end()) return *found;
        const auto index = integer_literals_.size();
        integer_literals_.push_back(
            {immediate.value,
             ".Lcross.mips.integer." + std::to_string(index)});
        return integer_literals_.back();
    }

    void emit_literal_pools() {
        if (float_literals_.empty() && integer_literals_.empty()) return;
        std::string error;
        const auto directive = assembly_section_directive(
            format_, {".rodata", AssemblySectionKind::ReadOnlyData,
                      false, false},
            error);
        if (!directive) {
            diagnostics_.error(module_.functions.front().location, error);
            return;
        }
        output_ << *directive << '\n';
        for (const auto& literal : float_literals_) {
            output_ << ".p2align " << (literal.width == 32 ? 2 : 3) << '\n'
                    << literal.label << ":\n\t"
                    << (literal.width == 32 ? ".word " : ".quad ")
                    << literal.bits << '\n';
        }
        for (const auto& literal : integer_literals_) {
            output_ << ".p2align 3\n"
                    << literal.label << ":\n\t.quad "
                    << literal.bits << '\n';
        }
    }

    static bool identifier_character(unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '.' || ch == '$';
    }

    // These opcodes take a symbol or label, never a register.  Cross symbols
    // may legally contain '$', so they are exempt from register respelling.
    static bool symbolic_operand_only(std::string_view opcode) {
        return opcode == "j" || opcode == "jal" || opcode == "b";
    }

    // Rewrite `$name` GPR operands to `$number`.  Applied once, at the single
    // point where operand text reaches the output stream, so literal operand
    // strings elsewhere in the emitter stay readable.  Idempotent: a numeric
    // spelling is not a GPR name and passes through untouched.
    std::string respell_registers(std::string_view operands) const {
        if (!numeric_gprs_ ||
            operands.find('$') == std::string_view::npos) {
            return std::string(operands);
        }
        std::string result;
        result.reserve(operands.size());
        for (std::size_t index = 0; index < operands.size(); ++index) {
            const auto ch = operands[index];
            if (ch != '$' ||
                (index != 0 &&
                 identifier_character(
                     static_cast<unsigned char>(operands[index - 1])))) {
                result.push_back(ch);
                continue;
            }
            auto end = index + 1;
            while (end < operands.size() &&
                   std::isalnum(static_cast<unsigned char>(operands[end]))) {
                ++end;
            }
            const auto id = gpr_id(operands.substr(index + 1,
                                                   end - index - 1));
            if (!id ||
                (end < operands.size() &&
                 identifier_character(
                     static_cast<unsigned char>(operands[end])))) {
                result.push_back(ch);
                continue;
            }
            result += '$';
            result += std::to_string(id->value);
            index = end - 1;
        }
        return result;
    }

    void raw_instruction(std::string_view opcode,
                         std::string_view operands = {}) {
        output_ << '\t' << opcode;
        if (!operands.empty()) {
            output_ << '\t';
            if (numeric_gprs_ && !symbolic_operand_only(opcode)) {
                output_ << respell_registers(operands);
            } else {
                output_ << operands;
            }
        }
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

    static std::optional<std::string> register_operand(
        std::string_view operands, std::size_t wanted) {
        std::size_t current = 0;
        for (std::size_t offset = 0; offset < operands.size(); ++offset) {
            if (operands[offset] != '$') continue;
            const auto begin = offset + 1;
            auto end = begin;
            while (end < operands.size()) {
                const auto ch = static_cast<unsigned char>(operands[end]);
                if (!std::isalnum(ch) && ch != '_') break;
                ++end;
            }
            if (current++ == wanted) {
                return std::string(operands.substr(begin, end - begin));
            }
            offset = end == 0 ? end : end - 1;
        }
        return std::nullopt;
    }

    static bool mentions_register(std::string_view operands,
                                  std::string_view name) {
        for (std::size_t index = 0;; ++index) {
            const auto operand = register_operand(operands, index);
            if (!operand) return false;
            if (*operand == name) return true;
        }
    }

    // Rewrite a memory operand so it addresses the word four bytes above.
    // The emitter spells addresses as `imm(base)` or `%lo(sym)(base)`; any
    // other form is left to the caller.
    static std::optional<std::string> next_word_address(
        std::string_view address) {
        if (address.starts_with("%lo(")) {
            const auto close = address.find(")(");
            if (close == std::string_view::npos) return std::nullopt;
            return "%lo(" + std::string(address.substr(4, close - 4)) +
                   "+4)" + std::string(address.substr(close + 1));
        }
        const auto open = address.find('(');
        if (open == std::string_view::npos) return std::nullopt;
        std::int64_t offset = 0;
        if (open != 0 &&
            std::from_chars(address.data(), address.data() + open, offset)
                    .ec != std::errc{}) {
            return std::nullopt;
        }
        return std::to_string(offset + 4) + std::string(address.substr(open));
    }

    // MIPS I has no doubleword coprocessor loads or stores. Access the FR=0
    // register pair by words instead: the even register holds the low word
    // and the odd register the high word, while big-endian memory keeps the
    // high word at the lower address. The first word occupies the MIPS I
    // load delay slot of the second, so only the pair's final load pays a
    // NOP.
    bool split_doubleword_coprocessor_access(std::string_view opcode,
                                             std::string_view operands) {
        if (subtarget_.has_feature(Feature::Mips2) ||
            (opcode != "ldc1" && opcode != "sdc1")) {
            return false;
        }
        const auto comma = operands.find(',');
        if (comma == std::string_view::npos || !operands.starts_with("$f")) {
            return false;
        }
        unsigned number = 0;
        if (std::from_chars(operands.data() + 2, operands.data() + comma,
                            number)
                    .ec != std::errc{} ||
            (number & 1U) != 0 || number + 1 >= fpr_names.size()) {
            return false;
        }
        const auto address = operands.substr(comma + 1);
        const auto upper = next_word_address(address);
        if (!upper) return false;
        const bool little = subtarget_.target().data_layout.byte_order ==
                            ByteOrder::Little;
        const std::string even = "$f" + std::to_string(number);
        const std::string odd = "$f" + std::to_string(number + 1);
        const std::string_view word = opcode == "ldc1" ? "lwc1" : "swc1";
        instruction(word, (little ? even : odd) + "," + std::string(address),
                    true);
        instruction(word, (little ? odd : even) + "," + *upper);
        return true;
    }

    void instruction(std::string_view opcode,
                     std::string_view operands = {},
                     bool defer_load_delay = false) {
        if (split_doubleword_coprocessor_access(opcode, operands)) return;
        const bool floating_multiply =
            opcode == "mul.s" || opcode == "mul.d" || opcode == "mul.ps";
        // MIPS I--III only require a transfer delay when the immediately
        // following instruction consumes the transferred register.  Keep the
        // hazard pending for one emitted instruction so independent integer,
        // address, or other-register work fills the slot instead of forcing a
        // NOP after every MTC1/MFC1.
        if (fpu_transfer_delay_register_) {
            if (mentions_register(operands, *fpu_transfer_delay_register_)) {
                raw_instruction("nop");
                if (hilo_write_barrier_ != 0) --hilo_write_barrier_;
            }
            fpu_transfer_delay_register_.reset();
        }
        // Early VR4300 silicon only needs separation between consecutive FP
        // multiplies. Let useful intervening work pay that gap and emit a NOP
        // only when another multiply arrives immediately.
        if (fpu_multiply_pending_) {
            if (floating_multiply) {
                raw_instruction("nop");
                if (hilo_write_barrier_ != 0) --hilo_write_barrier_;
            }
            fpu_multiply_pending_ = false;
        }
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
        if (!defer_load_delay &&
            !subtarget_.has_feature(Feature::LoadInterlocks) &&
            memory_load(opcode)) {
            // This intentionally favors correctness over trying to infer a
            // pseudo-instruction's eventual register dependencies.  A later
            // scheduler can fill these MIPS-I load slots from Machine IR.
            raw_instruction("nop");
        }
        if (!subtarget_.has_feature(Feature::FpuTransferInterlocks) &&
            (opcode == "mfc1" || opcode == "mtc1" ||
             opcode == "dmfc1" || opcode == "dmtc1")) {
            const auto destination =
                register_operand(operands,
                                 opcode == "mfc1" || opcode == "dmfc1"
                                     ? 0
                                     : 1);
            if (destination) fpu_transfer_delay_register_ = *destination;
        }
        if (!subtarget_.has_feature(Feature::FpuCompareInterlocks) &&
            opcode.starts_with("c.")) {
            raw_instruction("nop");
        }
        if (!subtarget_.has_feature(Feature::HiloInterlocks) &&
            (opcode == "mfhi" || opcode == "mflo")) {
            hilo_write_barrier_ = 2;
        }
        if (subtarget_.has_feature(Feature::Fix4300) && floating_multiply) {
            fpu_multiply_pending_ = true;
        }
    }

    void encoded(std::uint32_t word, std::string_view comment) {
        // An encoded target instruction has no typed operand list here.  Keep
        // the conservative separation if it follows a non-interlocked FPU
        // transfer; Allegrex normally has the architectural interlock and
        // therefore does not pay this fallback.
        if (fpu_transfer_delay_register_) {
            raw_instruction("nop");
            if (hilo_write_barrier_ != 0) --hilo_write_barrier_;
            fpu_transfer_delay_register_.reset();
        }
        output_ << "\t.word\t0x" << std::hex << std::setw(8)
                << std::setfill('0') << word << std::dec << std::setfill(' ');
        if (!comment.empty()) output_ << "\t# " << comment;
        output_ << '\n';
        fpu_multiply_pending_ = false;
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
                              std::string_view base) {
        return std::to_string(offset) + "(" + reg_name(base) + ")";
    }

    // Address arithmetic follows the selected ABI's address width.  A 32-bit
    // ABI on a 64-bit register file keeps the word forms: their sign-extended
    // results are exactly the canonical 32-bit addresses MIPS III expects.
    std::string_view address_add() const {
        return wide_addresses_ ? "daddu" : "addu";
    }

    // Symbol addresses default to the sym32 model: `lui`/`%lo` yields a
    // sign-extended 32-bit address, which is complete for 32-bit ABIs and
    // covers the low 2 GB plus KSEG0/KSEG1 under n64.  The large code model
    // materializes every 64-bit address from all four relocation halves.
    bool large_code_model() const {
        return wide_addresses_ && options_.code_model == CodeModel::Large;
    }

    void materialize_symbol_address(std::string_view destination,
                                    const std::string& symbol) {
        const auto reg = reg_name(destination);
        if (large_code_model()) {
            instruction("lui", reg + ",%highest(" + symbol + ")");
            instruction("daddiu", reg + "," + reg + ",%higher(" + symbol + ")");
            instruction("dsll", reg + "," + reg + ",16");
            instruction("daddiu", reg + "," + reg + ",%hi(" + symbol + ")");
            instruction("dsll", reg + "," + reg + ",16");
            instruction("daddiu", reg + "," + reg + ",%lo(" + symbol + ")");
            return;
        }
        instruction("lui", reg + ",%hi(" + symbol + ")");
        instruction(address_add_immediate(),
                    reg + "," + reg + ",%lo(" + symbol + ")");
    }

    std::string_view address_add_immediate() const {
        return wide_addresses_ ? "daddiu" : "addiu";
    }

    std::string_view address_shift_left() const {
        return wide_addresses_ ? "dsll" : "sll";
    }

    std::string_view address_multiply() const {
        return wide_addresses_ ? "dmultu" : "multu";
    }

    std::string_view address_load() const {
        return wide_addresses_ ? "ld" : "lw";
    }

    std::string_view address_store() const {
        return wide_addresses_ ? "sd" : "sw";
    }

    std::uint32_t address_bytes() const {
        return (hir_.address_bits + 7U) / 8U;
    }

    // Shrink-wrap placement keeps frame storage inside the framed region.
    void require_frame() const {
        if (!frame_active_) {
            diagnostics_.error(
                function_location_,
                "MIPS code outside a shrink-wrapped frame uses frame storage");
        }
    }

    // Returns before a shrink-wrapped prologue, or from a frameless
    // function, jump back directly instead of through the epilogue.
    bool direct_return() const { return frame_size_ == 0 || !frame_active_; }

    std::string memory(std::int64_t offset) const {
        require_frame();
        return memory(offset, frame_pointer_active_ ? "fp" : "sp");
    }

    std::string_view frame_base() const {
        require_frame();
        return frame_pointer_active_ ? std::string_view{"fp"}
                                     : std::string_view{"sp"};
    }

    std::string block_label(const machine::Function& function,
                            machine::BlockId block) const {
        return ".Lcross.mips." + std::to_string(function.source.value) +
               ".bb." + std::to_string(block.value);
    }

    std::optional<machine::BlockId> layout_successor(
        const machine::Function& function,
        machine::BlockId block) const {
        const auto found = std::find(function.layout.begin(),
                                     function.layout.end(), block);
        if (found == function.layout.end() ||
            std::next(found) == function.layout.end()) {
            return std::nullopt;
        }
        return *std::next(found);
    }

    bool same_physical_assignment(const machine::Function& function,
                                  machine::Register left,
                                  machine::Register right) const {
        if (left == right) return true;
        if (left.kind != machine::RegisterKind::Virtual ||
            right.kind != machine::RegisterKind::Virtual ||
            left.id >= function.virtual_register_assignments.size() ||
            right.id >= function.virtual_register_assignments.size()) {
            return false;
        }
        const auto lhs = function.virtual_register_assignments[left.id];
        const auto rhs = function.virtual_register_assignments[right.id];
        return lhs && rhs && lhs == rhs;
    }

    bool edge_has_phi_copies(const machine::Function& function,
                             machine::BlockId predecessor,
                             machine::BlockId successor) const {
        const auto found = std::find_if(
            function.blocks.begin(), function.blocks.end(),
            [&](const machine::Block& candidate) {
                return candidate.id == successor;
            });
        if (found == function.blocks.end()) return false;
        for (const auto& phi : found->instructions) {
            if (phi.kind != machine::InstructionKind::Target ||
                phi.opcode != Opcode::Phi || phi.defs.empty()) {
                continue;
            }
            for (std::size_t index = 0;
                 index + 1 < phi.operands.size(); index += 2) {
                const auto* incoming =
                    std::get_if<machine::BlockOperand>(
                        &phi.operands[index]);
                const auto* source =
                    std::get_if<machine::RegisterOperand>(
                        &phi.operands[index + 1]);
                if (!incoming || !source ||
                    incoming->target != predecessor) {
                    continue;
                }
                if (!same_physical_assignment(
                        function, source->value, phi.defs.front())) {
                    return true;
                }
                break;
            }
        }
        return false;
    }

    struct AssignedPhiCopy {
        std::string_view source;
        std::string_view target;
    };

    std::optional<AssignedPhiCopy> single_assigned_gpr_phi_copy(
        const machine::Function& function,
        machine::BlockId predecessor,
        machine::BlockId successor) const {
        const auto found = std::find_if(
            function.blocks.begin(), function.blocks.end(),
            [&](const machine::Block& candidate) {
                return candidate.id == successor;
            });
        if (found == function.blocks.end()) return std::nullopt;
        std::optional<AssignedPhiCopy> result;
        for (const auto& phi : found->instructions) {
            if (phi.kind != machine::InstructionKind::Target ||
                phi.opcode != Opcode::Phi || phi.defs.empty()) {
                continue;
            }
            for (std::size_t index = 0;
                 index + 1 < phi.operands.size(); index += 2) {
                const auto* incoming = std::get_if<machine::BlockOperand>(
                    &phi.operands[index]);
                const auto* source = std::get_if<machine::RegisterOperand>(
                    &phi.operands[index + 1]);
                if (!incoming || !source ||
                    incoming->target != predecessor) {
                    continue;
                }
                const auto target = phi.defs.front();
                if (same_physical_assignment(
                        function, source->value, target)) {
                    break;
                }
                const bool scalar_integer =
                    target.kind == machine::RegisterKind::Virtual &&
                    target.id < function.virtual_register_classes.size() &&
                    function.virtual_register_classes[target.id] ==
                        machine::VirtualRegisterClass::Integer &&
                    !legalizes_to_pair(target) && target.mode.bits <= 64;
                const auto source_gpr = assigned_gpr(
                    function, source->value);
                const auto target_gpr = assigned_gpr(function, target);
                if (!scalar_integer || !source_gpr || !target_gpr || result) {
                    return std::nullopt;
                }
                result = AssignedPhiCopy{*source_gpr, *target_gpr};
                break;
            }
        }
        return result;
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

    std::optional<std::string_view> assigned_gpr(
        const machine::Function& function,
        machine::Register value) const {
        if (value.kind != machine::RegisterKind::Virtual ||
            value.id >= function.virtual_register_assignments.size() ||
            !function.virtual_register_assignments[value.id]) {
            return std::nullopt;
        }
        const auto name = gpr_name(
            *function.virtual_register_assignments[value.id]);
        return name.empty() ? std::nullopt
                            : std::optional<std::string_view>{name};
    }

    std::optional<std::string_view> assigned_fpr(
        const machine::Function& function,
        machine::Register value) const {
        if (value.kind != machine::RegisterKind::Virtual ||
            value.id >= function.virtual_register_assignments.size() ||
            !function.virtual_register_assignments[value.id]) {
            return std::nullopt;
        }
        const auto name = fpr_name(
            *function.virtual_register_assignments[value.id]);
        return name.empty() ? std::nullopt
                            : std::optional<std::string_view>{name};
    }

    std::string_view input_gpr(const machine::Function& function,
                               machine::Register value,
                               std::string_view fallback,
                               SourceLocation location) {
        if (const auto assigned = assigned_gpr(function, value)) {
            return *assigned;
        }
        load_vreg(function, value, fallback, location);
        return fallback;
    }

    std::string_view output_gpr(const machine::Function& function,
                                machine::Register value,
                                std::string_view fallback) const {
        if (const auto assigned = assigned_gpr(function, value)) {
            return *assigned;
        }
        return fallback;
    }

    void commit_gpr(const machine::Function& function,
                    machine::Register value, std::string_view source,
                    SourceLocation location) {
        if (!assigned_gpr(function, value)) {
            store_vreg(function, value, source, location);
        }
    }

    void form_indexed_address(const machine::Function& function,
                              const machine::Instruction& value,
                              std::string_view destination) {
        const auto base = input_gpr(
            function, value.uses[0], "t0", value.location);
        const auto index = input_gpr(
            function, value.uses[1], "t1", value.location);
        const auto scale = static_cast<unsigned>(
            std::get<machine::ImmediateOperand>(
                value.operands.back()).value);
        if (scale == 1) {
            instruction(address_add(), reg_name(destination) + "," +
                                           reg_name(base) + "," +
                                           reg_name(index));
            return;
        }
        const auto scaled = destination != base && base != index
            ? destination : std::string_view{"t2"};
        if (scale == 0) {
            instruction("move", reg_name(destination) + "," +
                                    reg_name(base));
            return;
        }
        if (std::has_single_bit(scale)) {
            instruction(address_shift_left(),
                        reg_name(scaled) + "," + reg_name(index) + "," +
                            std::to_string(std::countr_zero(scale)));
        } else {
            instruction("li", "$t3," + std::to_string(scale));
            instruction(address_multiply(), reg_name(index) + ",$t3");
            instruction("mflo", reg_name(scaled));
        }
        instruction(address_add(), reg_name(destination) + "," +
                                       reg_name(base) + "," +
                                       reg_name(scaled));
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
        const auto* abi = managed_abi_model(hir_, entity, subtarget_, options_);
        return classify_interface(
            *hir::call_signature(hir_, entity.id, std::nullopt), abi, location,
            actual_types);
    }

    std::optional<ActiveSignature>
    classify_call(const machine::Instruction& call) {
        if (call.direct_callee) {
            return classify_entity(hir_.function(*call.direct_callee),
                                   call.location, call.call_argument_types);
        }
        const auto signature =
            hir::call_signature(hir_, std::nullopt, call.call_signature);
        if (!signature) {
            diagnostics_.error(call.location,
                               "MIPS call has no typed interface");
            return std::nullopt;
        }
        return classify_interface(*signature, abi_model(signature->abi),
                                  call.location, call.call_argument_types);
    }

    std::optional<ActiveSignature>
    classify_interface(const hir::FunctionSignature& entity,
                       const AbiEntry* abi, SourceLocation location,
                       std::span<const hir::TypeId> actual_types) {
        if (!abi) {
            diagnostics_.error(location,
                               "MIPS function has no registered ABI model");
            return std::nullopt;
        }
        std::vector<AbiValue> arguments;
        const auto count = actual_types.empty() ? entity.parameters.size()
                                                : actual_types.size();
        arguments.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto type = actual_types.empty()
                                  ? entity.parameters[index].type
                                  : actual_types[index];
            const bool fixed = index < entity.parameters.size();
            const auto transport =
                fixed && entity.parameters[index].mode != ParameterMode::In
                    ? ValueTransport::ByReference
                    : ValueTransport::Direct;
            arguments.push_back(abi_value_for(
                hir_, type, subtarget_.target().data_layout, *abi, transport));
        }
        std::vector<AbiValue> results;
        if (!is_void(hir_, entity.result_type)) {
            results.push_back(abi_value_for(hir_, entity.result_type,
                                            subtarget_.target().data_layout,
                                            *abi));
        }
        const auto classified =
            entity.variadic
                ? classify_variadic_signature(*abi, arguments, results,
                                              entity.parameters.size(),
                                              subtarget_.enabled_features())
                : classify_signature(*abi, arguments, results,
                                     subtarget_.enabled_features());
        if (!classified) {
            diagnostics_.error(
                location,
                "MIPS ABI model could not classify function interface");
            return std::nullopt;
        }
        return ActiveSignature{abi, classified.layout};
    }

    static std::string parameter_home_name(std::string_view reg) {
        return "$abi.in." + std::string(reg);
    }

    std::optional<machine::Register> parameter_target(
        const machine::Function& function, std::size_t parameter) const {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                if ((instruction.opcode != Opcode::Parameter &&
                     instruction.opcode != Opcode::Fparameter) ||
                    instruction.defs.empty() ||
                    instruction.operands.empty()) {
                    continue;
                }
                const auto* index =
                    std::get_if<machine::ImmediateOperand>(
                        &instruction.operands.front());
                if (index && index->value == parameter) {
                    return instruction.defs.front();
                }
            }
        }
        return std::nullopt;
    }

    bool individually_direct_parameter_capture(
        const machine::Function& function, std::size_t index) const {
        if (!active_signature_ ||
            index >= active_signature_->layout.call.arguments.size()) {
            return false;
        }
        const auto& entity = hir_.function(function.source);
        if (index >= entity.parameters.size()) return false;
        // An `out`/`inout` parameter arrives as its transport pointer.
        const bool pointer = entity.parameters[index].mode != ParameterMode::In;
        const auto& assignment =
            active_signature_->layout.call.arguments[index];
        const auto target = parameter_target(function, index);
        if (!target) return false;
        if (!pointer && is_floating(hir_, entity.parameters[index].type)) {
            return assignment.pieces.size() == 1 &&
                assignment.pieces.front().location.kind ==
                    LocationKind::Register &&
                assigned_fpr(function, *target).has_value();
        }
        if (!assigned_gpr(function, *target) || assignment.pieces.empty() ||
            std::any_of(assignment.pieces.begin(), assignment.pieces.end(),
                        [](const ValuePiece& piece) {
                            return piece.location.kind !=
                                   LocationKind::Register;
                        })) {
            return false;
        }
        const auto bits = pointer
            ? hir_.address_bits
            : type_bits(hir_, entity.parameters[index].type);
        return assignment.pieces.size() == 1 ||
            (subtarget_.has_feature(Feature::Mips3) && bits <= 64);
    }

    // Captures are emitted in parameter order. Direct captures never write
    // another parameter's incoming register, and fallback captures write
    // only the t0/t1 assembly scratches, so every incoming register survives
    // until its own capture unless an argument arrives in t0 or t1.
    bool incoming_registers_survive() const {
        return active_signature_ && std::none_of(
            active_signature_->layout.call.arguments.begin(),
            active_signature_->layout.call.arguments.end(),
            [](const auto& assignment) {
                return std::any_of(
                    assignment.pieces.begin(), assignment.pieces.end(),
                    [](const ValuePiece& piece) {
                        return piece.location.kind == LocationKind::Register &&
                            (piece.location.reg == "t0" || piece.location.reg == "t1");
                    });
            });
    }

    bool direct_parameter_capture(const machine::Function& function,
                                  std::size_t index) const {
        if (!individually_direct_parameter_capture(function, index) ||
            !active_signature_) {
            return false;
        }
        if (incoming_registers_survive()) return true;
        // Otherwise one fallback capture requires every incoming register to
        // be preserved in its home first.
        for (std::size_t other = 0;
             other < active_signature_->layout.call.arguments.size();
             ++other) {
            const auto& assignment =
                active_signature_->layout.call.arguments[other];
            const bool arrives_in_register = std::any_of(
                assignment.pieces.begin(), assignment.pieces.end(),
                [](const ValuePiece& piece) {
                    return piece.location.kind == LocationKind::Register;
                });
            if (arrives_in_register &&
                !individually_direct_parameter_capture(function, other)) {
                return false;
            }
        }
        return true;
    }

    // An output pointer arriving in one register is read from it directly
    // when incoming registers survive, so it needs no home.
    bool output_pointer_in_register(const machine::Function& function,
                                    std::size_t index) const {
        const auto& entity = hir_.function(function.source);
        if (!active_signature_ || !incoming_registers_survive() ||
            index >= entity.parameters.size() ||
            entity.parameters[index].mode == ParameterMode::In ||
            index >= active_signature_->layout.call.arguments.size()) {
            return false;
        }
        const auto& pieces = active_signature_->layout.call.arguments[index].pieces;
        return pieces.size() == 1 &&
            pieces.front().location.kind == LocationKind::Register;
    }

    void prepare_parameter_homes(machine::Function& function) {
        if (!active_signature_) return;
        if (!active_signature_->layout.results.empty() &&
            active_signature_->layout.results.front().indirect) {
            const auto& pieces =
                active_signature_->layout.results.front().pieces;
            for (std::size_t index = 0; index < pieces.size(); ++index) {
                machine::StackSlot home;
                home.id = {
                    static_cast<std::uint32_t>(function.stack_slots.size())};
                home.kind = machine::StackSlotKind::IncomingArgument;
                home.size = address_bytes();
                home.alignment = home.size;
                home.location = function.location;
                home.name = "$aggregate.sret." + std::to_string(index);
                function.stack_slots.push_back(std::move(home));
            }
        }
        for (std::size_t index = 0;
             index < active_signature_->layout.call.arguments.size();
             ++index) {
            if (direct_parameter_capture(function, index) ||
                output_pointer_in_register(function, index)) continue;
            const auto& assignment =
                active_signature_->layout.call.arguments[index];
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
        if (!active_signature_->layout.results.empty() &&
            active_signature_->layout.results.front().indirect) {
            const auto& pieces =
                active_signature_->layout.results.front().pieces;
            for (std::size_t index = 0; index < pieces.size(); ++index) {
                const auto* home = named_slot(
                    function, "$aggregate.sret." + std::to_string(index));
                if (!home || !home->frame_offset) {
                    diagnostics_.error(
                        function.location,
                        "MIPS indirect result pointer has no frame home");
                    return;
                }
                const auto& piece = pieces[index];
                if (piece.location.kind == LocationKind::Register) {
                    instruction(address_store(),
                                reg_name(piece.location.reg) + "," +
                                    memory(*home->frame_offset));
                } else {
                    instruction(address_load(),
                                "$at," + incoming_memory(
                                             piece, *active_signature_->abi));
                    instruction(address_store(),
                                "$at," + memory(*home->frame_offset));
                }
            }
        }
        std::vector<std::string> saved;
        for (std::size_t index = 0;
             index < active_signature_->layout.call.arguments.size();
             ++index) {
            if (direct_parameter_capture(function, index) ||
                output_pointer_in_register(function, index)) continue;
            const auto& assignment =
                active_signature_->layout.call.arguments[index];
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

    void emit_callee_saves(const machine::Function& function, bool cfi) {
        for (const auto physical : function.callee_saved_registers) {
            const auto integer_name = gpr_name(physical);
            const auto floating_name = fpr_name(physical);
            const auto name = floating_name.empty()
                ? integer_name : floating_name;
            const auto* slot = named_slot(
                function, "$callee.save." + std::string(name));
            if (name.empty() || !slot || !slot->frame_offset) {
                diagnostics_.error(
                    function.location,
                    "MIPS allocated callee-save register has no frame home");
                continue;
            }
            instruction(!floating_name.empty()
                            ? slot->size > 4 ? "sdc1" : "swc1"
                            : slot->size > 4 ? "sd" : "sw",
                        reg_name(name) + "," +
                            memory(*slot->frame_offset));
            if (cfi) {
                output_ << ".cfi_offset " << physical.value << ','
                        << static_cast<std::int64_t>(*slot->frame_offset) -
                               frame_size_
                        << '\n';
            }
        }
    }

    void emit_callee_restores(const machine::Function& function, bool cfi) {
        for (auto item = function.callee_saved_registers.rbegin();
             item != function.callee_saved_registers.rend(); ++item) {
            const auto integer_name = gpr_name(*item);
            const auto floating_name = fpr_name(*item);
            const auto name = floating_name.empty()
                ? integer_name : floating_name;
            const auto* slot = named_slot(
                function, "$callee.save." + std::string(name));
            if (name.empty() || !slot || !slot->frame_offset) continue;
            instruction(!floating_name.empty()
                            ? slot->size > 4 ? "ldc1" : "lwc1"
                            : slot->size > 4 ? "ld" : "lw",
                        reg_name(name) + "," +
                            memory(*slot->frame_offset));
            if (cfi) output_ << ".cfi_restore " << item->value << '\n';
        }
    }

    void prepare_fixed_frame(machine::Function& function) {
        using namespace machine;
        const IntegerMode address_mode{static_cast<std::uint16_t>(address_bytes() * 8U)};
        const auto sp = Register::physical_register({29}, address_mode);
        const auto fp = Register::physical_register({30}, address_mode);
        const auto ra = Register::physical_register({31}, address_mode);
        FrameProgram program;
        program.stack_pointer = sp;
        program.stack_size = frame_size_;
        program.entry_alignment = function.frame.stack_alignment;
        program.body_alignment = function.frame.stack_alignment;
        const auto add_slot = [&](Register reg, std::uint32_t offset, std::string name) {
            StackSlot slot;
            slot.id = {static_cast<std::uint32_t>(function.stack_slots.size())};
            slot.kind = StackSlotKind::CalleeSave;
            slot.size = reg.mode.bits / 8U;
            slot.alignment = slot.size;
            slot.frame_offset = static_cast<std::int32_t>(offset);
            slot.saved_register = reg;
            slot.location = function.location;
            slot.name = std::move(name);
            function.stack_slots.push_back(std::move(slot));
        };
        if (saves_fp_) add_slot(fp, saved_fp_offset_, "$frame.fp");
        if (saves_ra_) add_slot(ra, saved_ra_offset_, "$frame.ra");
        const auto adjustment = [&](std::int32_t delta) {
            FrameEffect effect;
            effect.operation = FrameOperation::AdjustStack;
            effect.base = sp;
            effect.stack_delta = delta;
            return frame_instruction(Opcode::FrameAdjust, std::move(effect), function.location);
        };
        const auto copy = [&](Register destination, Register source) {
            FrameEffect effect;
            effect.operation = FrameOperation::CopyBase;
            effect.base = source;
            effect.destination = destination;
            return frame_instruction(Opcode::FrameCopy, std::move(effect), function.location);
        };
        const auto transfer = [&](Register reg, Register base, bool load) {
            const auto slot = std::find_if(function.stack_slots.begin(), function.stack_slots.end(),
                [&](const StackSlot& item) { return item.saved_register == reg; });
            FrameEffect effect;
            effect.operation = load ? FrameOperation::Restore : FrameOperation::Save;
            effect.base = base;
            if (slot == function.stack_slots.end() || !slot->frame_offset) {
                diagnostics_.error(function.location, "MIPS frame register has no typed save slot");
            } else {
                effect.transfers.push_back({reg, *slot->frame_offset, slot->id});
            }
            return frame_instruction(load ? Opcode::FrameRestore : Opcode::FrameSave,
                                     std::move(effect), function.location);
        };
        if (frame_size_ != 0) {
            program.prologue.push_back(adjustment(-static_cast<std::int32_t>(frame_size_)));
        }
        if (saves_ra_) program.prologue.push_back(transfer(ra, sp, false));
        if (saves_fp_) program.prologue.push_back(transfer(fp, sp, false));
        if (function.frame.has_frame_pointer) program.prologue.push_back(copy(fp, sp));
        const auto base = function.frame.has_frame_pointer ? fp : sp;
        for (const auto reg : function.callee_saved_registers) {
            const auto slot = std::find_if(function.stack_slots.begin(), function.stack_slots.end(),
                [&](const StackSlot& item) {
                    return item.saved_register && item.saved_register->id == reg.value;
                });
            if (slot != function.stack_slots.end()) {
                program.prologue.push_back(transfer(*slot->saved_register, base, false));
            }
        }
        if (!function.frame.elide_incoming_saves) {
            for (auto reg = function.callee_saved_registers.rbegin();
                 reg != function.callee_saved_registers.rend(); ++reg) {
                const auto slot = std::find_if(function.stack_slots.begin(), function.stack_slots.end(),
                    [&](const StackSlot& item) {
                        return item.saved_register && item.saved_register->id == reg->value;
                    });
                if (slot != function.stack_slots.end()) {
                    program.epilogue.push_back(transfer(*slot->saved_register, base, true));
                }
            }
            if (function.frame.has_frame_pointer) program.epilogue.push_back(copy(sp, fp));
            if (saves_fp_) program.epilogue.push_back(transfer(fp, sp, true));
            if (saves_ra_) program.epilogue.push_back(transfer(ra, sp, true));
            if (frame_size_ != 0) {
                program.epilogue.push_back(adjustment(static_cast<std::int32_t>(frame_size_)));
            }
        }
        function.frame.program = std::move(program);
    }

    // Shrink-wrapping moves only the fixed frame program. Entry work that
    // uses the frame (incoming homes and stack pieces, output pointers,
    // indirect or stack results) keeps it at function entry, as do externally
    // visible labels.
    bool can_shrink_wrap(const machine::Function& function) const {
        const auto& entity = hir_.function(function.source);
        if (!options_.shrink_wrap || !active_signature_ ||
            function.frame.elide_incoming_saves ||
            function.frame.program->prologue.empty() ||
            returns_twice_or_interrupt(entity) ||
            std::any_of(entity.parameters.begin(), entity.parameters.end(),
                        [](const hir::Parameter& parameter) {
                            return parameter.mode != ParameterMode::In;
                        }) ||
            std::any_of(function.labels.begin(), function.labels.end(),
                        [&](const machine::Function::LocalLabel& label) {
                            return hir_.labels.at(label.label.value).is_global;
                        }) ||
            std::any_of(function.stack_slots.begin(),
                        function.stack_slots.end(),
                        [](const machine::StackSlot& slot) {
                            return !slot.elided &&
                                   slot.kind ==
                                       machine::StackSlotKind::IncomingArgument;
                        })) {
            return false;
        }
        return register_interface(active_signature_->layout);
    }

    void emit_frame_instruction(const machine::Instruction& value) {
        const auto& effect = *value.frame_effect;
        const auto base = reg_name(gpr_name({effect.base.id}));
        switch (decode_opcode(value.opcode)) {
        case Opcode::FrameAdjust:
            instruction(address_add_immediate(), base + "," + base + "," +
                        std::to_string(effect.stack_delta));
            break;
        case Opcode::FrameCopy:
            instruction("move", reg_name(gpr_name({effect.destination->id})) + "," + base);
            break;
        case Opcode::FrameSave:
        case Opcode::FrameRestore:
            for (const auto& transfer : effect.transfers) {
                const bool load = effect.operation == machine::FrameOperation::Restore;
                const auto floating = fpr_name({transfer.reg.id});
                const auto reg = floating.empty() ? gpr_name({transfer.reg.id}) : floating;
                const auto opcode = !floating.empty()
                    ? transfer.reg.mode.bits > 32
                        ? (load ? "ldc1" : "sdc1")
                        : (load ? "lwc1" : "swc1")
                    : transfer.reg.mode.bits > 32 ? (load ? "ld" : "sd") : (load ? "lw" : "sw");
                instruction(opcode, reg_name(reg) + "," + std::to_string(transfer.offset) +
                                    "(" + base + ")");
            }
            break;
        default:
            diagnostics_.error(value.location, "invalid selected MIPS frame instruction");
            break;
        }
    }

    const machine::Instruction* emit_prepared_epilogue(const machine::Function& function) {
        const auto& code = function.frame.program->epilogue;
        const bool delayed = !code.empty() && code.back().opcode == Opcode::FrameAdjust;
        for (std::size_t index = 0; index < code.size() - static_cast<unsigned>(delayed); ++index) {
            emit_frame_instruction(code[index]);
        }
        return delayed ? &code.back() : nullptr;
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

    void move_gpr_to_fpr(std::string_view source,
                         std::string_view destination,
                         unsigned bits) {
        instruction(bits <= 32 ? "mtc1" : "dmtc1",
                    reg_name(source) + "," + reg_name(destination));
    }

    void move_fpr_to_gpr(std::string_view source,
                         std::string_view destination,
                         unsigned bits) {
        instruction(bits <= 32 ? "mfc1" : "dmfc1",
                    reg_name(destination) + "," + reg_name(source));
    }

    // MIPS64 word operations define an SImode result by sign-extending bit
    // 31 regardless of the source-language signedness. Keep all internal
    // 32-bit values in that hardware-native form; explicit widening still
    // uses normalize_integer() to apply zero- versus sign-extension semantics.
    void canonicalize_integer_value(std::string_view reg, unsigned bits,
                                    bool sign) {
        if (bits == 32 && subtarget_.has_feature(Feature::Mips3)) {
            instruction("sll", reg_name(reg) + "," + reg_name(reg) + ",0");
            return;
        }
        normalize_integer(reg, bits, sign);
    }

    bool abi_supplies_canonical_word(const ValuePiece& piece,
                                     unsigned bits) const {
        return bits == 32 && piece.carrier_bits == 32 &&
            piece.extension == AbiExtensionKind::Sign &&
            subtarget_.has_feature(Feature::Mips3);
    }

    bool internal_value_matches_extension(const ValuePiece& piece,
                                          unsigned bits) const {
        if (!subtarget_.has_feature(Feature::Mips3) ||
            piece.extension == AbiExtensionKind::None) {
            return true;
        }
        return bits == 32 && piece.carrier_bits == 32 &&
            piece.extension == AbiExtensionKind::Sign;
    }

    void apply_abi_register_extension(std::string_view reg,
                                      const ValuePiece& piece) {
        if (!subtarget_.has_feature(Feature::Mips3) ||
            piece.extension == AbiExtensionKind::None ||
            piece.carrier_bits >= 64) {
            return;
        }
        normalize_integer(
            reg, piece.carrier_bits,
            piece.extension == AbiExtensionKind::Sign);
    }

    void load_integer_memory(std::string_view target,
                             std::string memory_operand, unsigned bits,
                             bool sign) {
        const auto opcode = bits <= 8 ? (sign ? "lb" : "lbu") :
                            bits <= 16 ? (sign ? "lh" : "lhu") :
                            bits <= 32 ? "lw" : "ld";
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
                          std::string_view base = {}) {
        if (base.empty()) base = frame_base();
        instruction("lw", reg_name(low) + "," +
                              memory(word_offset(offset, false), base));
        instruction("lw", reg_name(high) + "," +
                              memory(word_offset(offset, true), base));
    }

    void store_pair_memory(std::string_view low, std::string_view high,
                           std::int64_t offset,
                           std::string_view base = {}) {
        if (base.empty()) base = frame_base();
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
        if (const auto assigned = assigned_gpr(function, value)) {
            if (*assigned != target) {
                instruction("move", reg_name(target) + "," +
                                        reg_name(*assigned));
            }
            return;
        }
        const auto offset = vreg_offset(function, value, location);
        load_integer_memory(target, memory(offset),
                            value.mode.bits > 32 ? 64U : 32U, true);
    }

    void store_vreg(const machine::Function& function,
                    machine::Register value, std::string_view source,
                    SourceLocation location) {
        if (const auto assigned = assigned_gpr(function, value)) {
            if (*assigned != source) {
                instruction("move", reg_name(*assigned) + "," +
                                        reg_name(source));
            }
            return;
        }
        const auto offset = vreg_offset(function, value, location);
        store_integer_memory(source, memory(offset),
                             value.mode.bits > 32 ? 64U : 32U);
    }

    void load_fvreg(const machine::Function& function,
                    machine::Register value, std::string_view target,
                    SourceLocation location) {
        if (const auto assigned = assigned_fpr(function, value)) {
            if (*assigned != target) {
                instruction(value.mode.bits == 32 ? "mov.s" : "mov.d",
                            reg_name(target) + "," + reg_name(*assigned));
            }
            return;
        }
        const auto offset = vreg_offset(function, value, location);
        instruction(value.mode.bits == 32 ? "lwc1" : "ldc1",
                    reg_name(target) + "," + memory(offset));
    }

    void store_fvreg(const machine::Function& function,
                     machine::Register value, std::string_view source,
                     SourceLocation location) {
        if (const auto assigned = assigned_fpr(function, value)) {
            if (*assigned != source) {
                instruction(value.mode.bits == 32 ? "mov.s" : "mov.d",
                            reg_name(*assigned) + "," + reg_name(source));
            }
            return;
        }
        const auto offset = vreg_offset(function, value, location);
        instruction(value.mode.bits == 32 ? "swc1" : "sdc1",
                    reg_name(source) + "," + memory(offset));
    }

    std::string_view input_fpr(const machine::Function& function,
                               machine::Register value,
                               std::string_view fallback,
                               SourceLocation location) {
        if (const auto assigned = assigned_fpr(function, value)) {
            return *assigned;
        }
        load_fvreg(function, value, fallback, location);
        return fallback;
    }

    std::string_view output_fpr(const machine::Function& function,
                                machine::Register value,
                                std::string_view fallback) const {
        if (const auto assigned = assigned_fpr(function, value)) {
            return *assigned;
        }
        return fallback;
    }

    void commit_fpr(const machine::Function& function,
                    machine::Register value, std::string_view source,
                    SourceLocation location) {
        if (!assigned_fpr(function, value)) {
            store_fvreg(function, value, source, location);
        }
    }

    bool aggregate_vreg(const machine::Function& function,
                        machine::Register value) const {
        return value.kind == machine::RegisterKind::Virtual &&
               value.id < function.virtual_register_classes.size() &&
               function.virtual_register_classes[value.id] ==
                   machine::VirtualRegisterClass::Memory &&
               value.mode.bits >= 8;
    }

    void copy_bytes(std::string_view destination_base, std::int64_t destination,
                    std::string_view source_base, std::int64_t source,
                    unsigned bytes) {
        // Homes are disjoint from source-language objects. Byte accesses also
        // handle packed records and preserve object representation on either
        // endian. No implicit memcpy helper or alignment assumption is needed.
        for (unsigned index = 0; index < bytes; ++index) {
            instruction("lbu", "$at," + memory(source + index, source_base));
            instruction("sb",
                        "$at," + memory(destination + index, destination_base));
        }
    }

    void emit_copy(const machine::Function& function,
                   const machine::Instruction& value) {
        const auto target = value.defs.front();
        const auto source = value.uses.front();
        if (same_physical_assignment(function, target, source)) return;
        if (const auto assigned = assigned_gpr(function, target)) {
            load_vreg(function, source, *assigned, value.location);
        } else if (const auto floating = assigned_fpr(function, target)) {
            load_fvreg(function, source, *floating, value.location);
        } else {
            copy_vreg(function, target, source, value.location);
        }
    }

    void copy_vreg(const machine::Function& function,
                   machine::Register target, machine::Register source,
                   SourceLocation location) {
        const auto floating = target.kind == machine::RegisterKind::Virtual &&
            target.id < function.virtual_register_classes.size() &&
            function.virtual_register_classes[target.id] ==
                machine::VirtualRegisterClass::Floating;
        if (aggregate_vreg(function, target)) {
            copy_bytes(frame_base(), vreg_offset(function, target, location),
                       frame_base(), vreg_offset(function, source, location),
                       target.mode.bits / 8U);
        } else if (floating) {
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

    bool valid_aggregate_piece(machine::Register value, const ValuePiece& piece,
                               SourceLocation location) {
        const auto bits = piece.indirect_value_bits != 0
                              ? piece.indirect_value_bits
                              : piece.value_bits;
        if (piece.value_bit_offset % 8U != 0 || bits % 8U != 0 ||
            static_cast<unsigned>(piece.value_bit_offset) + bits >
                value.mode.bits ||
            (piece.location.kind == LocationKind::Register &&
             (piece.carrier_bits < piece.value_bits ||
              piece.carrier_bits > 64 ||
              (piece.carrier_bits > 32 && !fpr(piece.location.reg) &&
               !subtarget_.has_feature(Feature::Mips3))))) {
            diagnostics_.error(
                location,
                "MIPS aggregate ABI piece has an unsupported size or offset");
            return false;
        }
        if (piece.location.kind == LocationKind::Register &&
            fpr(piece.location.reg) &&
            (!subtarget_.has_feature(Feature::HardFloat) ||
             (piece.carrier_bits > 32 &&
              subtarget_.has_feature(Feature::SingleFloat)))) {
            diagnostics_.error(location,
                               "MIPS aggregate ABI requires an unavailable "
                               "floating register carrier");
            return false;
        }
        return true;
    }

    std::int64_t aggregate_carrier_offset(const machine::Function& function,
                                          SourceLocation location) {
        const auto* slot = named_slot(function, "$aggregate.carrier");
        if (!slot || !slot->frame_offset) {
            diagnostics_.error(
                location, "MIPS aggregate ABI transport has no carrier cell");
            return 0;
        }
        return *slot->frame_offset;
    }

    void place_aggregate_piece(const machine::Function& function,
                               machine::Register source,
                               const ValuePiece& piece,
                               SourceLocation location) {
        if (!valid_aggregate_piece(source, piece, location)) return;
        const auto offset =
            static_cast<std::int64_t>(vreg_offset(function, source, location)) +
            piece.value_bit_offset / 8U;
        if (piece.indirect_value_bits != 0) {
            instruction(address_add_immediate(),
                        "$at," + reg_name(frame_base()) + "," +
                            std::to_string(offset));
            apply_abi_register_extension("at", piece);
            if (piece.location.kind == LocationKind::Register) {
                instruction("move", reg_name(piece.location.reg) + ",$at");
            } else {
                instruction(address_store(),
                            "$at," + memory(static_cast<std::int64_t>(
                                                piece.location.stack_offset),
                                            "sp"));
            }
            return;
        }
        if (piece.location.kind == LocationKind::Stack) {
            copy_bytes("sp",
                       static_cast<std::int64_t>(piece.location.stack_offset),
                       frame_base(), offset, piece.value_bits / 8U);
            return;
        }
        const auto carrier = aggregate_carrier_offset(function, location);
        for (unsigned byte = 0; byte < (piece.carrier_bits + 7U) / 8U; ++byte) {
            instruction("sb", "$zero," + memory(carrier + byte));
        }
        // Aggregate offsets describe address-ordered object bytes, not scalar
        // significance. A short piece occupies the beginning of its carrier
        // on both endians (high bits on big endian, low bits on little endian).
        copy_bytes(frame_base(), carrier, frame_base(), offset,
                   piece.value_bits / 8U);
        if (fpr(piece.location.reg)) {
            if (piece.carrier_bits != 32 && piece.carrier_bits != 64) {
                diagnostics_.error(
                    location,
                    "MIPS floating aggregate carrier must be 32 or 64 bits");
                return;
            }
            instruction(piece.carrier_bits == 32 ? "lwc1" : "ldc1",
                        reg_name(piece.location.reg) + "," + memory(carrier));
        } else {
            load_integer_memory(piece.location.reg, memory(carrier),
                                piece.carrier_bits, false);
            apply_abi_register_extension(piece.location.reg, piece);
        }
    }

    void capture_aggregate_piece(const machine::Function& function,
                                 machine::Register target,
                                 const ValuePiece& piece, const AbiEntry& abi,
                                 bool parameter_entry,
                                 SourceLocation location) {
        if (!valid_aggregate_piece(target, piece, location)) return;
        const auto destination =
            static_cast<std::int64_t>(vreg_offset(function, target, location)) +
            piece.value_bit_offset / 8U;
        if (piece.indirect_value_bits != 0) {
            load_abi_piece(function, piece, abi, "t0", parameter_entry);
            normalize_integer("t0", hir_.address_bits, true);
            copy_bytes(frame_base(), destination, "t0", 0,
                       piece.indirect_value_bits / 8U);
            return;
        }
        std::string_view base = frame_base();
        std::int64_t offset{};
        if (piece.location.kind == LocationKind::Stack) {
            offset = static_cast<std::int64_t>(piece.location.stack_offset);
            if (parameter_entry)
                offset += frame_size_ + abi.return_address_bytes;
            else
                base = "sp";
        } else if (parameter_entry) {
            const auto* home =
                named_slot(function, parameter_home_name(piece.location.reg));
            if (!home || !home->frame_offset) {
                diagnostics_.error(
                    location,
                    "MIPS aggregate parameter register has no frame home");
                return;
            }
            offset = *home->frame_offset;
        } else {
            offset = aggregate_carrier_offset(function, location);
            if (fpr(piece.location.reg)) {
                if (piece.carrier_bits != 32 && piece.carrier_bits != 64) {
                    diagnostics_.error(location,
                                       "MIPS floating aggregate carrier must "
                                       "be 32 or 64 bits");
                    return;
                }
                instruction(piece.carrier_bits == 32 ? "swc1" : "sdc1",
                            reg_name(piece.location.reg) + "," +
                                memory(offset));
            } else {
                store_integer_memory(piece.location.reg, memory(offset),
                                     piece.carrier_bits);
            }
        }
        copy_bytes(frame_base(), destination, base, offset,
                   piece.value_bits / 8U);
    }

    std::uint32_t outgoing_size(const machine::Function& function) {
        std::uint32_t size{};
        for (const auto& block : function.blocks) {
            for (const auto& value : block.instructions) {
                if (value.kind != machine::InstructionKind::Call) {
                    continue;
                }
                auto classified = classify_call(value);
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
        if (has_dynamic_stack(function)) offset = 0;
        bool has_call = false;
        for (const auto& block : function.blocks) {
            for (std::size_t index = 0;
                 index < block.instructions.size(); ++index) {
                const auto& instruction = block.instructions[index];
                if (instruction.kind != machine::InstructionKind::Call) {
                    continue;
                }
                const bool tail = index + 1 < block.instructions.size() &&
                                  can_emit_tail_call(
                                      function, instruction,
                                      block.instructions[index + 1]);
                has_call = has_call || !tail;
            }
        }
        for (auto& slot : function.stack_slots) {
            if (slot.elided) continue;
            offset = align_up(offset, slot.alignment);
            slot.frame_offset = static_cast<std::int32_t>(offset);
            offset += slot.size;
        }
        bool incoming_stack = false;
        if (active_signature_) {
            for (const auto& assignment :
                 active_signature_->layout.call.arguments) {
                incoming_stack = incoming_stack || std::any_of(
                    assignment.pieces.begin(), assignment.pieces.end(),
                    [](const ValuePiece& piece) {
                        return piece.location.kind == LocationKind::Stack;
                    });
            }
        }
        has_call_ = has_call;
        if (!has_dynamic_stack(function) && offset == 0 && !has_call && !incoming_stack &&
            function.callee_saved_registers.empty()) {
            saved_fp_offset_ = 0;
            saved_ra_offset_ = 0;
            saves_fp_ = false;
            saves_ra_ = false;
            frame_size_ = 0;
            function.frame.has_frame_pointer = false;
            function.frame.local_size = 0;
            function.frame.finalized = true;
            return true;
        }
        function.frame.has_frame_pointer = has_dynamic_stack(function) ||
            !options_.omit_frame_pointer;
        // $fp and $ra carry addresses, so their homes are as wide as the
        // ABI's address model: one word under o32/EABI, a doubleword under
        // n64.
        const auto pointer_bytes = address_bytes();
        offset = align_up(offset, pointer_bytes);
        saves_fp_ = function.frame.has_frame_pointer &&
                    !function.frame.elide_incoming_saves;
        if (saves_fp_) {
            saved_fp_offset_ = offset;
            offset += pointer_bytes;
        } else {
            saved_fp_offset_ = 0;
        }
        saves_ra_ = has_call && !function.frame.elide_incoming_saves;
        if (saves_ra_) {
            saved_ra_offset_ = offset;
            offset += pointer_bytes;
        } else {
            saved_ra_offset_ = 0;
        }
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
                load_integer_memory(
                    gpr, memory(*home->frame_offset), piece.carrier_bits,
                    piece.extension == AbiExtensionKind::Sign);
            } else {
                instruction("move", reg_name(gpr) + "," +
                                        reg_name(piece.location.reg));
            }
        } else {
            load_integer_memory(
                gpr, incoming_memory(piece, abi), piece.carrier_bits,
                piece.extension == AbiExtensionKind::Sign);
        }
    }

    void assemble_incoming_integer(const machine::Function& function,
                                   const std::vector<ValuePiece>& pieces,
                                   const AbiEntry& abi, unsigned value_bits,
                                   bool sign, bool parameter_entry) {
        if (pieces.size() == 1 &&
            effective_piece_offset(
                pieces.front(), value_bits,
                subtarget_.target().data_layout.byte_order) == 0 &&
            pieces.front().value_bits >= value_bits) {
            load_abi_piece(function, pieces.front(), abi, "t0",
                           parameter_entry);
            if (!abi_supplies_canonical_word(
                    pieces.front(), value_bits)) {
                canonicalize_integer_value("t0", value_bits, sign);
            }
            return;
        }
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
        canonicalize_integer_value("t0", value_bits, sign);
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
            // The managed body copies in and out through this pointer. A
            // 32-bit ABI address carried by a MIPS-III 64-bit GPR must be
            // canonical before it is dereferenced.
            const auto& piece = assignment.pieces.front();
            if (direct_parameter_capture(function, index)) {
                const auto destination = assigned_gpr(function, target);
                if (!destination) return;
                if (*destination != piece.location.reg) {
                    instruction("move", reg_name(*destination) + "," +
                                            reg_name(piece.location.reg));
                }
                if (!abi_supplies_canonical_word(piece, hir_.address_bits)) {
                    normalize_integer(*destination, hir_.address_bits, true);
                }
                return;
            }
            load_abi_piece(function, piece, *active_signature_->abi, "t0",
                           !output_pointer_in_register(function, index));
            normalize_integer("t0", hir_.address_bits, true);
            store_vreg(function, target, "t0", value.location);
            return;
        }
        if (is_aggregate(hir_, parameter.type)) {
            for (const auto& piece : assignment.pieces) {
                capture_aggregate_piece(function, target, piece,
                                        *active_signature_->abi, true,
                                        value.location);
            }
            return;
        }
        if (value.input_projection) {
            const auto logical_bits = type_bits(hir_, parameter.type);
            const auto projection_begin =
                static_cast<unsigned>(value.input_projection->bit_offset);
            const auto projection_end = projection_begin +
                static_cast<unsigned>(value.input_projection->bit_width);
            const auto selected = std::find_if(
                assignment.pieces.begin(), assignment.pieces.end(),
                [&](const ValuePiece& piece) {
                    const auto piece_begin = effective_piece_offset(
                        piece, logical_bits,
                        subtarget_.target().data_layout.byte_order);
                    return projection_begin >= piece_begin &&
                        projection_end <= piece_begin + piece.value_bits;
                });
            if (selected == assignment.pieces.end()) {
                diagnostics_.error(
                    value.location,
                    "MIPS projected parameter is not contained in one ABI "
                    "transport piece");
                return;
            }
            const auto piece_begin = effective_piece_offset(
                *selected, logical_bits,
                subtarget_.target().data_layout.byte_order);
            const auto shift = projection_begin - piece_begin;
            if (direct_parameter_capture(function, index) &&
                selected->location.kind == LocationKind::Register) {
                const auto destination = assigned_gpr(function, target);
                if (!destination) return;
                if (*destination != selected->location.reg) {
                    instruction("move", reg_name(*destination) + "," +
                                            reg_name(selected->location.reg));
                }
                if (shift != 0) {
                    instruction(selected->carrier_bits > 32 ? "dsrl" : "srl",
                                reg_name(*destination) + "," +
                                    reg_name(*destination) + "," +
                                    std::to_string(shift));
                }
                if (!abi_supplies_canonical_word(
                        *selected, target.mode.bits)) {
                    canonicalize_integer_value(
                        *destination, target.mode.bits, false);
                }
                return;
            }
            load_abi_piece(function, *selected, *active_signature_->abi,
                           "t0", true);
            if (shift != 0) {
                instruction(selected->carrier_bits > 32 ? "dsrl" : "srl",
                            "$t0,$t0," + std::to_string(shift));
            }
            if (!abi_supplies_canonical_word(
                    *selected, target.mode.bits)) {
                canonicalize_integer_value(
                    "t0", target.mode.bits, false);
            }
            store_vreg(function, target, "t0", value.location);
            return;
        }
        if (direct_parameter_capture(function, index)) {
            const auto& piece = assignment.pieces.front();
            if (is_floating(hir_, parameter.type)) {
                const auto destination = assigned_fpr(function, target);
                if (!destination) return;
                if (fpr(piece.location.reg)) {
                    if (*destination != piece.location.reg) {
                        instruction(target.mode.bits == 32 ? "mov.s" : "mov.d",
                                    reg_name(*destination) + "," +
                                        reg_name(piece.location.reg));
                    }
                } else {
                    move_gpr_to_fpr(piece.location.reg, *destination,
                                    target.mode.bits);
                }
                return;
            }
            const auto destination = assigned_gpr(function, target);
            if (!destination) return;
            if (assignment.pieces.size() == 1) {
                if (*destination != piece.location.reg) {
                    instruction("move", reg_name(*destination) + "," +
                                            reg_name(piece.location.reg));
                }
                const auto bits = type_bits(hir_, parameter.type);
                if (!abi_supplies_canonical_word(piece, bits)) {
                    canonicalize_integer_value(
                        *destination, bits,
                        is_signed_integer(hir_, parameter.type) ||
                            is_address_value(hir_, parameter.type));
                }
                return;
            }

            // MIPS III can assemble an o32-style word sequence directly into
            // one allocated 64-bit destination. Parameter colors exclude all
            // live incoming endpoints, and $at is compiler-reserved, so this
            // is a safe parallel entry capture without a stack home.
            bool first = true;
            const auto bits = type_bits(hir_, parameter.type);
            if (bits == 64 && assignment.pieces.size() == 2) {
                const ValuePiece* low = nullptr;
                const ValuePiece* high = nullptr;
                for (const auto& input : assignment.pieces) {
                    const auto offset = effective_piece_offset(
                        input, bits,
                        subtarget_.target().data_layout.byte_order);
                    if (input.value_bits == 32 && offset == 0) low = &input;
                    if (input.value_bits == 32 && offset == 32) high = &input;
                }
                if (low && high) {
                    instruction("dsll32", reg_name(*destination) + "," +
                                              reg_name(high->location.reg) +
                                              ",0");
                    instruction("dsll32", "$at," +
                                              reg_name(low->location.reg) +
                                              ",0");
                    instruction("dsrl32", "$at,$at,0");
                    instruction("or", reg_name(*destination) + "," +
                                          reg_name(*destination) + ",$at");
                    return;
                }
            }
            for (const auto& input : assignment.pieces) {
                auto work = std::string_view{"at"};
                if (first) work = *destination;
                if (work != input.location.reg) {
                    instruction("move", reg_name(work) + "," +
                                            reg_name(input.location.reg));
                }
                if (input.value_bits < 32) {
                    const auto mask =
                        (std::uint64_t{1} << input.value_bits) - 1U;
                    instruction("andi", reg_name(work) + "," +
                                            reg_name(work) + "," +
                                            std::to_string(mask));
                } else if (input.value_bits == 32) {
                    instruction("dsll32", reg_name(work) + "," +
                                               reg_name(work) + ",0");
                    instruction("dsrl32", reg_name(work) + "," +
                                               reg_name(work) + ",0");
                }
                const auto shift = effective_piece_offset(
                    input, bits,
                    subtarget_.target().data_layout.byte_order);
                if (shift != 0) {
                    instruction("dsll", reg_name(work) + "," +
                                            reg_name(work) + "," +
                                            std::to_string(shift));
                }
                if (!first) {
                    instruction("or", reg_name(*destination) + "," +
                                          reg_name(*destination) + "," +
                                          reg_name(work));
                }
                first = false;
            }
            canonicalize_integer_value(
                *destination, bits,
                is_signed_integer(hir_, parameter.type) ||
                    is_address_value(hir_, parameter.type));
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
                is_signed_integer(hir_, parameter.type) ||
                    is_address_value(hir_, parameter.type),
                true);
        }
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            // The pair was written above, including floating bit transport.
        } else if (is_floating(hir_, parameter.type)) {
            if (const auto destination = assigned_fpr(function, target)) {
                move_gpr_to_fpr("t0", *destination, target.mode.bits);
            } else {
                const auto offset =
                    vreg_offset(function, target, value.location);
                store_integer_memory("t0", memory(offset), target.mode.bits);
            }
        } else {
            store_vreg(function, target, "t0", value.location);
        }
    }

    void place_integer_piece(const machine::Function& function,
                             machine::Register source,
                             const ValuePiece& piece, unsigned value_bits,
                             SourceLocation location,
                             machine::ExtensionKind extension = machine::ExtensionKind::None) {
        const auto shift = effective_piece_offset(
            piece, value_bits, subtarget_.target().data_layout.byte_order);
        if (extension == machine::ExtensionKind::None &&
            !legalizes_to_pair(source) && shift == 0 &&
            piece.value_bits >= value_bits &&
            piece.location.kind == LocationKind::Register &&
            internal_value_matches_extension(piece, value_bits)) {
            if (const auto assigned = assigned_gpr(function, source)) {
                if (*assigned != piece.location.reg) {
                    instruction("move", reg_name(piece.location.reg) + "," +
                                            reg_name(*assigned));
                }
                return;
            }
        }
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
            if (extension != machine::ExtensionKind::None) {
                // A return may absorb a MIR widening cast. Its source home
                // still contains the narrow bits, so source extension must
                // precede splitting and the independently specified ABI
                // carrier extension.
                normalize_integer("at", source.mode.bits,
                                  extension == machine::ExtensionKind::Sign);
            }
        }
        if (!legalizes_to_pair(source) && shift != 0) {
            if (extension != machine::ExtensionKind::None &&
                shift >= source.mode.bits) {
                if (extension == machine::ExtensionKind::Sign)
                    instruction("sra", "$at,$at,31");
                else
                    instruction("move", "$at,$zero");
            } else {
                instruction(value_bits > 32 ? "dsrl" : "srl",
                            "$at,$at," + std::to_string(shift));
            }
        }
        apply_abi_register_extension("at", piece);
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
        instruction(address_add_immediate(),
                    "$at," + reg_name(frame_base()) + "," +
                        std::to_string(offset));
        apply_abi_register_extension("at", piece);
        if (piece.location.kind == LocationKind::Register) {
            instruction("move", reg_name(piece.location.reg) + ",$at");
        } else {
            instruction(address_store(),
                        "$at," + memory(static_cast<std::int64_t>(
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
        const auto shift = effective_piece_offset(
            piece, source.mode.bits,
            subtarget_.target().data_layout.byte_order);
        if (const auto assigned = assigned_fpr(function, source)) {
            move_fpr_to_gpr(*assigned, "at", source.mode.bits);
        } else if (legalizes_to_pair(source)) {
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
            const auto offset = vreg_offset(function, source, location);
            load_integer_memory("at", memory(offset), source.mode.bits,
                                false);
        }
        if (!legalizes_to_pair(source) && shift != 0) {
            instruction("dsrl", "$at,$at," + std::to_string(shift));
        }
        apply_abi_register_extension("at", piece);
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

    bool place_split_integer_registers(
        const machine::Function& function, machine::Register source,
        const std::vector<ValuePiece>& pieces, unsigned value_bits,
        bool fill_return_delay = false) {
        if (!subtarget_.has_feature(Feature::Mips3) || value_bits != 64 ||
            source.mode.bits != 64 || pieces.size() != 2) {
            return false;
        }
        const ValuePiece* low = nullptr;
        const ValuePiece* high = nullptr;
        for (const auto& piece : pieces) {
            if (piece.location.kind != LocationKind::Register ||
                piece.value_bits != 32 || piece.carrier_bits != 32 ||
                piece.extension != AbiExtensionKind::Sign) {
                return false;
            }
            const auto offset = effective_piece_offset(
                piece, value_bits,
                subtarget_.target().data_layout.byte_order);
            if (offset == 0) low = &piece;
            if (offset == 32) high = &piece;
        }
        const auto assigned = assigned_gpr(function, source);
        if (!low || !high || !assigned ||
            low->location.reg == high->location.reg) {
            return false;
        }

        const auto place_low = [&] {
            instruction("sll", reg_name(low->location.reg) + "," +
                                   reg_name(*assigned) + ",0");
        };
        const auto place_high = [&] {
            instruction("dsrl32", reg_name(high->location.reg) + "," +
                                      reg_name(*assigned) + ",0");
            instruction("sll", reg_name(high->location.reg) + "," +
                                   reg_name(high->location.reg) + ",0");
        };
        if (fill_return_delay) {
            if (*assigned == low->location.reg ||
                *assigned == high->location.reg) {
                return false;
            }
            place_high();
            if (direct_return()) instruction("jr", "$ra");
            else instruction("b", epilogue_label_);
            place_low();
            return true;
        }
        if (*assigned == high->location.reg) {
            place_low();
            place_high();
        } else {
            place_high();
            place_low();
        }
        return true;
    }

    void capture_call_result(const machine::Function& function,
                             const machine::Instruction& call,
                             const ActiveSignature& signature) {
        if (call.defs.empty()) return;
        if (aggregate_vreg(function, call.defs.front()) &&
            !signature.layout.results.empty()) {
            const auto& result = signature.layout.results.front();
            if (!result.indirect) {
                for (const auto& piece : result.pieces) {
                    capture_aggregate_piece(function, call.defs.front(), piece,
                                            *signature.abi, false,
                                            call.location);
                }
            }
            return;
        }
        if (signature.layout.results.empty() ||
            signature.layout.results.front().pieces.empty() ||
            signature.layout.results.front().indirect) {
            diagnostics_.error(call.location,
                               "MIPS indirect call results are not implemented yet");
            return;
        }
        const auto target = call.defs.front();
        const auto& assignment = signature.layout.results.front();
        const auto callee =
            *hir::call_signature(hir_, call.direct_callee, call.call_signature);
        const auto bits = type_bits(hir_, callee.result_type);
        if (!is_floating(hir_, callee.result_type) &&
            assignment.pieces.size() == 1 &&
            assignment.pieces.front().location.kind ==
                LocationKind::Register &&
            effective_piece_offset(
                assignment.pieces.front(), bits,
                subtarget_.target().data_layout.byte_order) == 0 &&
            assignment.pieces.front().value_bits >= bits) {
            if (const auto destination = assigned_gpr(function, target)) {
                const auto source = assignment.pieces.front().location.reg;
                if (*destination != source) {
                    instruction("move", reg_name(*destination) + "," +
                                            reg_name(source));
                }
                if (!abi_supplies_canonical_word(
                        assignment.pieces.front(), bits)) {
                    canonicalize_integer_value(
                        *destination, bits,
                        is_signed_integer(hir_, callee.result_type) ||
                            is_address_value(hir_, callee.result_type));
                }
                return;
            }
        }
        if (is_floating(hir_, callee.result_type) &&
            assignment.pieces.size() == 1 &&
            assignment.pieces.front().location.kind == LocationKind::Register &&
            fpr(assignment.pieces.front().location.reg)) {
            store_fvreg(function, target,
                        assignment.pieces.front().location.reg,
                        call.location);
            return;
        }
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            assemble_incoming_pair(function, assignment.pieces,
                                   *signature.abi, bits, false);
            store_vreg_pair(function, target, "t0", "t1", call.location);
        } else {
            assemble_incoming_integer(
                function, assignment.pieces, *signature.abi, bits,
                is_signed_integer(hir_, callee.result_type) ||
                    is_address_value(hir_, callee.result_type),
                false);
        }
        if (bits > 32 && !subtarget_.has_feature(Feature::Mips3)) {
            // Already stored as two ABI words.
        } else if (is_floating(hir_, callee.result_type)) {
            if (const auto destination = assigned_fpr(function, target)) {
                move_gpr_to_fpr("t0", *destination, target.mode.bits);
            } else {
                store_integer_memory(
                    "t0",
                    memory(vreg_offset(function, target, call.location)),
                    target.mode.bits);
            }
        } else {
            store_vreg(function, target, "t0", call.location);
        }
    }

    void spill_call_live_registers(
        const machine::Function& function,
        const machine::Instruction& call) {
        for (const auto value : call.live_across_call) {
            if (const auto assigned = assigned_gpr(function, value)) {
                store_integer_memory(
                    *assigned,
                    memory(vreg_offset(function, value, call.location)),
                    value.mode.bits > 32 ? 64U : 32U);
                continue;
            }
            if (const auto assigned = assigned_fpr(function, value)) {
                instruction(value.mode.bits == 32 ? "swc1" : "sdc1",
                            reg_name(*assigned) + "," + memory(
                                vreg_offset(function, value, call.location)));
                continue;
            }
            diagnostics_.error(
                call.location,
                "MIPS call-live value lost its physical assignment");
        }
    }

    void reload_call_live_registers(
        const machine::Function& function,
        const machine::Instruction& call) {
        for (const auto value : call.live_across_call) {
            if (const auto assigned = assigned_gpr(function, value)) {
                load_integer_memory(
                    *assigned,
                    memory(vreg_offset(function, value, call.location)),
                    value.mode.bits > 32 ? 64U : 32U, true);
                continue;
            }
            if (const auto assigned = assigned_fpr(function, value)) {
                instruction(value.mode.bits == 32 ? "lwc1" : "ldc1",
                            reg_name(*assigned) + "," + memory(
                                vreg_offset(function, value, call.location)));
                continue;
            }
            diagnostics_.error(
                call.location,
                "MIPS call-live value lost its physical assignment");
        }
    }

    bool can_emit_tail_call(const machine::Function& function,
                            const machine::Instruction& call,
                            const machine::Instruction& result) {
        if (has_dynamic_stack(function) ||
            (!options_.optimize_sibling_calls && !call.must_tail) ||
            !active_signature_ ||
            !call.direct_callee || call.operands.empty() ||
            call.kind != machine::InstructionKind::Call ||
            result.kind != machine::InstructionKind::Return) {
            return false;
        }
        const auto& caller = hir_.function(function.source);
        const auto& callee = hir_.function(*call.direct_callee);
        if (callee.variadic ||
            std::any_of(caller.parameters.begin(), caller.parameters.end(),
                        [](const hir::Parameter& parameter) {
                            return parameter.mode != ParameterMode::In;
                        }) ||
            std::any_of(callee.parameters.begin(), callee.parameters.end(),
                        [](const hir::Parameter& parameter) {
                            return parameter.mode != ParameterMode::In;
                        })) {
            return false;
        }
        const auto permits_clobber = [&](const std::vector<std::string>& names,
                                         std::uint32_t physical) {
            return std::any_of(
                names.begin(), names.end(), [&](const std::string& name) {
                    const auto id = physical_register_id(name);
                    return id && id->value == physical;
                });
        };
        for (const auto& clobber : call.clobbers) {
            if (clobber.kind != machine::RegisterKind::Physical) continue;
            if (!permits_clobber(active_signature_->abi->call_clobbers,
                                 clobber.id) &&
                !permits_clobber(caller.clobbers, clobber.id)) {
                return false;
            }
        }
        auto signature = classify_entity(
            callee, call.location, call.call_argument_types);
        if (!signature) return false;
        // o32 reserves a register-argument home area even when every value is
        // transported in GPRs.  A sibling call can reuse the home area that
        // the caller of this function already provided after our frame is
        // removed.  Actual stack pieces remain disallowed below.
        for (const auto& assignment : signature->layout.call.arguments) {
            if (assignment.indirect || assignment.stack_size != 0 ||
                std::any_of(
                    assignment.pieces.begin(), assignment.pieces.end(),
                    [](const ValuePiece& piece) {
                        return piece.location.kind != LocationKind::Register;
                    })) {
                return false;
            }
            for (const auto& piece : assignment.pieces) {
                const auto physical = physical_register_id(piece.location.reg);
                if (physical && std::find(
                        function.callee_saved_registers.begin(),
                        function.callee_saved_registers.end(), *physical) !=
                                      function.callee_saved_registers.end()) {
                    return false;
                }
            }
        }

        const bool caller_void = is_void(hir_, caller.result_type);
        const bool callee_void = is_void(hir_, callee.result_type);
        if (caller_void != callee_void) return false;
        if (caller_void) {
            return call.defs.empty() && result.uses.empty();
        }
        if (caller.result_type != callee.result_type ||
            call.defs.size() != 1 || result.uses.size() != 1 ||
            call.defs.front() != result.uses.front() ||
            active_signature_->layout.results.size() != 1 ||
            signature->layout.results.size() != 1) {
            return false;
        }
        const auto& caller_result =
            active_signature_->layout.results.front();
        const auto& callee_result = signature->layout.results.front();
        if (caller_result.indirect || callee_result.indirect ||
            caller_result.stack_size != 0 || callee_result.stack_size != 0 ||
            caller_result.pieces.size() != callee_result.pieces.size()) {
            return false;
        }
        for (std::size_t index = 0;
             index < caller_result.pieces.size(); ++index) {
            const auto& left = caller_result.pieces[index];
            const auto& right = callee_result.pieces[index];
            if (left.location.kind != right.location.kind ||
                left.location.reg != right.location.reg ||
                left.location.stack_offset != right.location.stack_offset ||
                left.value_bit_offset != right.value_bit_offset ||
                left.value_bits != right.value_bits ||
                left.carrier_bits != right.carrier_bits ||
                left.extension != right.extension) {
                return false;
            }
        }
        return true;
    }

    bool needs_shared_epilogue(const machine::Function& function) {
        for (const auto& block : function.blocks) {
            if (!framed_[block.id.value]) continue;
            for (std::size_t index = 0;
                 index < block.instructions.size(); ++index) {
                const auto& instruction = block.instructions[index];
                if (instruction.kind != machine::InstructionKind::Return) {
                    continue;
                }
                if (index != 0 &&
                    can_emit_tail_call(function,
                                       block.instructions[index - 1],
                                       instruction)) {
                    continue;
                }
                return true;
            }
        }
        return false;
    }

    void emit_call(const machine::Function& function,
                   const machine::Instruction& call, bool tail = false,
                   bool cfi = false) {
        auto signature = classify_call(call);
        if (!signature) return;
        if (signature->layout.call.arguments.size() + 1 !=
            call.operands.size()) {
            diagnostics_.error(call.location,
                               "MIPS call operands disagree with ABI layout");
            return;
        }
        if (!tail) spill_call_live_registers(function, call);
        const machine::StackSlot* dynamic_mark = nullptr;
        if (has_dynamic_stack(function)) {
            for (const auto& slot : function.stack_slots) {
                if (slot.name == "$dynamic.call.sp") dynamic_mark = &slot;
            }
            if (!dynamic_mark || !dynamic_mark->frame_offset) {
                diagnostics_.error(call.location, "missing MIPS dynamic call stack home");
                return;
            }
            const auto size = signature->layout.call.outgoing_area_size;
            const auto alignment = signature->layout.call.outgoing_area_alignment;
            if (size > 32760 || alignment > 32768) {
                diagnostics_.error(call.location, "MIPS dynamic outgoing call frame is too large");
                return;
            }
            instruction(address_store(), "$sp," + memory(*dynamic_mark->frame_offset));
            instruction(address_add_immediate(), "$t0,$sp,-" + std::to_string(size));
            instruction(wide_addresses_ ? "dli" : "li", "$t1,-" + std::to_string(alignment));
            instruction("and", "$t0,$t0,$t1");
            emit_stack_probe(function);
        }
        if (!signature->layout.results.empty() &&
            signature->layout.results.front().indirect) {
            if (call.defs.empty() ||
                !aggregate_vreg(function, call.defs.front())) {
                diagnostics_.error(call.location,
                                   "MIPS indirect call result requires an "
                                   "aggregate destination");
                return;
            }
            for (const auto& piece : signature->layout.results.front().pieces) {
                place_aggregate_piece(function, call.defs.front(), piece,
                                      call.location);
            }
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
            if (aggregate_vreg(function, source)) {
                for (const auto& piece : assignment.pieces) {
                    place_aggregate_piece(function, source, piece,
                                          call.location);
                }
                for (const auto& piece : assignment.shadows) {
                    place_aggregate_piece(function, source, piece,
                                          call.location);
                }
                continue;
            }
            const bool floating =
                index < call.call_argument_types.size() &&
                is_floating(hir_, call.call_argument_types[index]);
            const auto argument_bits =
                type_bits(hir_, call.call_argument_types[index]);
            if (floating || !place_split_integer_registers(
                                function, source, assignment.pieces,
                                argument_bits)) {
                for (const auto& piece : assignment.pieces) {
                    if (floating) {
                        place_floating_piece(function, source, piece,
                                             call.location);
                    } else {
                        place_integer_piece(function, source, piece,
                                            argument_bits, call.location);
                    }
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
        const auto* callee_register =
            std::get_if<machine::RegisterOperand>(&call.operands.front());
        if (!callee_symbol && !callee_register) return;
        if (tail) {
            if (!callee_symbol) {
                diagnostics_.error(call.location,
                                   "indirect MIPS tail calls are not selected");
                return;
            }
            if (cfi) output_ << ".cfi_remember_state\n";
            const machine::Instruction* frame_delay = nullptr;
            if (function.frame.program) {
                frame_delay = emit_prepared_epilogue(function);
            } else {
                emit_callee_restores(function, cfi);
                if (function.frame.has_frame_pointer) {
                    instruction("move", "$sp,$fp");
                }
                if (saves_fp_) {
                    instruction(address_load(),
                                "$fp," + memory(saved_fp_offset_, "sp"));
                }
                if (saves_ra_) {
                    instruction(address_load(),
                                "$ra," + memory(saved_ra_offset_, "sp"));
                }
            }
            // Under the large model the callee may lie outside the 256 MB
            // region a direct jump can name.  $at is the emitter's own
            // scratch and never carries an argument or a live value here.
            if (large_code_model()) {
                materialize_symbol_address(
                    "at", assembly_symbol(callee_symbol->name));
                instruction("jr", "$at");
            } else {
                instruction("j", assembly_symbol(callee_symbol->name));
            }
            if (frame_delay) emit_frame_instruction(*frame_delay);
            else if (frame_size_ == 0) instruction("nop");
            else {
                instruction(address_add_immediate(),
                            "$sp,$sp," + std::to_string(frame_size_));
            }
            if (cfi) output_ << ".cfi_restore_state\n";
            return;
        }
        if (callee_register) {
            const auto target = input_gpr(function, callee_register->value,
                                          "at", call.location);
            instruction("jalr", reg_name(target));
        } else if (large_code_model()) {
            materialize_symbol_address("at",
                                       assembly_symbol(callee_symbol->name));
            instruction("jalr", "$at");
        } else {
            instruction("jal", assembly_symbol(callee_symbol->name));
        }
        instruction("nop");
        capture_call_result(function, call, *signature);
        if (dynamic_mark) {
            instruction(address_load(), "$sp," + memory(*dynamic_mark->frame_offset));
        }
        reload_call_live_registers(function, call);
    }

    // $t0 is the aligned lower stack address. Probe each newly reserved page
    // inline: Cross must never introduce a runtime stack-check helper.
    void emit_stack_probe(const machine::Function& function) {
        const auto loop = local_label(function);
        const auto finish = local_label(function);
        const auto done = local_label(function);
        instruction("beq", "$sp,$t0," + done);
        instruction("nop");
        output_ << loop << ":\n";
        instruction(wide_addresses_ ? "dsubu" : "subu", "$t1,$sp,$t0");
        instruction("sltiu", "$at,$t1,4096");
        instruction("bne", "$at,$zero," + finish);
        instruction("nop");
        instruction(address_add_immediate(), "$sp,$sp,-4096");
        instruction("sw", "$zero,0($sp)");
        instruction("b", loop);
        instruction("nop");
        output_ << finish << ":\n";
        instruction("move", "$sp,$t0");
        instruction("sw", "$zero,0($sp)");
        output_ << done << ":\n";
    }

    void emit_dynamic_stack(const machine::Function& function,
                            const machine::Instruction& value) {
        if (value.opcode == Opcode::StackSave) {
            store_vreg(function, value.defs.front(), "sp", value.location);
            return;
        }
        load_vreg(function, value.uses.front(), "t0", value.location);
        if (value.opcode == Opcode::StackRestore) {
            instruction("move", "$sp,$t0");
            return;
        }
        const auto& element = std::get<machine::ImmediateOperand>(
            value.operands[value.operands.size() - 2]);
        const auto& requested = std::get<machine::ImmediateOperand>(value.operands.back());
        const auto alignment = std::max<std::uint64_t>(
            requested.value, function.frame.stack_alignment);
        const auto invalid = local_label(function);
        const auto done = local_label(function);
        normalize_integer("t0", value.uses.front().mode.bits, element.is_signed);
        instruction("beq", "$t0,$zero," + invalid);
        instruction("nop");
        if (element.is_signed) {
            instruction("bltz", "$t0," + invalid);
            instruction("nop");
        }
        normalize_integer("t0", hir_.address_bits, false);
        instruction(wide_addresses_ ? "dli" : "li", "$t1," + std::to_string(element.value));
        instruction(wide_addresses_ ? "dmultu" : "multu", "$t0,$t1");
        instruction("mfhi", "$t1");
        instruction("bne", "$t1,$zero," + invalid);
        instruction("nop");
        instruction("mflo", "$t1");
        normalize_integer("t1", hir_.address_bits, false);
        instruction("move", "$t2,$sp");
        normalize_integer("t2", hir_.address_bits, false);
        instruction("sltu", "$at,$t2,$t1");
        instruction("bne", "$at,$zero," + invalid);
        instruction("nop");
        instruction(wide_addresses_ ? "dsubu" : "subu", "$t0,$sp,$t1");
        instruction(wide_addresses_ ? "dli" : "li", "$t1,-" + std::to_string(alignment));
        instruction("and", "$t0,$t0,$t1");
        emit_stack_probe(function);
        store_vreg(function, value.defs.front(), "sp", value.location);
        instruction("b", done);
        instruction("nop");
        output_ << invalid << ":\n";
        instruction("break");
        output_ << done << ":\n";
    }

    bool place_composed_return(
        const machine::Function& function,
        const machine::Instruction& value,
        const ReturnAssignment& result,
        bool fallthrough_epilogue) {
        if (value.result_composition !=
                machine::ResultComposition::XorZeroExtend) {
            return false;
        }
        if (!subtarget_.has_feature(Feature::Mips3) ||
            value.uses.size() != 2 || value.uses[0].mode.bits != 64 ||
            value.uses[1].mode.bits != 32 || result.indirect ||
            result.pieces.size() != 2) {
            diagnostics_.error(
                value.location,
                "invalid MIPS mixed-width result composition");
            return true;
        }

        const ValuePiece* low = nullptr;
        const ValuePiece* high = nullptr;
        for (const auto& piece : result.pieces) {
            if (piece.location.kind != LocationKind::Register ||
                piece.value_bits != 32 || piece.carrier_bits != 32) {
                diagnostics_.error(
                    value.location,
                    "MIPS mixed-width result requires two 32-bit register pieces");
                return true;
            }
            const auto offset = effective_piece_offset(
                piece, 64,
                subtarget_.target().data_layout.byte_order);
            if (offset == 0) low = &piece;
            else if (offset == 32) high = &piece;
        }
        if (!low || !high || low->location.reg == high->location.reg) {
            diagnostics_.error(
                value.location,
                "MIPS mixed-width result has an invalid ABI piece map");
            return true;
        }

        const auto wide_assignment = assigned_gpr(function, value.uses[0]);
        const auto narrow_assignment = assigned_gpr(function, value.uses[1]);
        const auto scratch = [&](std::initializer_list<std::string_view>
                                     unavailable)
            -> std::optional<std::string_view> {
            static constexpr std::array<std::string_view, 11> candidates{
                "t0", "t1", "t2", "t3", "t4", "t5",
                "t6", "t7", "t8", "t9", "at"};
            const auto found = std::find_if(
                candidates.begin(), candidates.end(),
                [&](std::string_view candidate) {
                    return std::find(unavailable.begin(), unavailable.end(),
                                     candidate) == unavailable.end();
                });
            return found == candidates.end()
                ? std::nullopt
                : std::optional<std::string_view>{*found};
        };

        std::string_view wide;
        std::string_view narrow;
        if (wide_assignment) {
            wide = *wide_assignment;
        } else {
            const auto selected = scratch(
                {low->location.reg, high->location.reg,
                 narrow_assignment.value_or(std::string_view{})});
            if (!selected) {
                diagnostics_.error(value.location,
                                   "MIPS return has no wide-value scratch register");
                return true;
            }
            wide = *selected;
            load_vreg(function, value.uses[0], wide, value.location);
        }
        if (narrow_assignment) {
            narrow = *narrow_assignment;
        } else {
            const auto selected = scratch(
                {low->location.reg, high->location.reg, wide});
            if (!selected) {
                diagnostics_.error(
                    value.location,
                    "MIPS return has no narrow-value scratch register");
                return true;
            }
            narrow = *selected;
            load_vreg(function, value.uses[1], narrow, value.location);
        }
        if (wide == narrow || wide == low->location.reg ||
            wide == high->location.reg || narrow == low->location.reg ||
            narrow == high->location.reg) {
            diagnostics_.error(
                value.location,
                "MIPS allocator overlapped a composed return source and result");
            return true;
        }

        instruction("xor", reg_name(low->location.reg) + "," +
                               reg_name(wide) + "," + reg_name(narrow));
        instruction("dsrl32", reg_name(high->location.reg) + "," +
                                  reg_name(wide) + ",0");
        if (high->extension == AbiExtensionKind::Sign) {
            instruction("sll", reg_name(high->location.reg) + "," +
                                   reg_name(high->location.reg) + ",0");
        }

        // Keep the final low-word normalization in the control-transfer
        // delay slot whenever this block owns that transfer.
        std::string delay_opcode = "nop";
        std::string delay_operands;
        if (low->extension == AbiExtensionKind::Sign) {
            delay_opcode = "sll";
            delay_operands = reg_name(low->location.reg) + "," +
                reg_name(low->location.reg) + ",0";
        } else if (low->extension == AbiExtensionKind::Zero) {
            instruction("dsll32", reg_name(low->location.reg) + "," +
                                      reg_name(low->location.reg) + ",0");
            delay_opcode = "dsrl32";
            delay_operands = reg_name(low->location.reg) + "," +
                reg_name(low->location.reg) + ",0";
        }
        const auto emit_delay = [&] {
            if (delay_opcode == "nop") instruction("nop");
            else instruction(delay_opcode, delay_operands);
        };
        if (direct_return()) {
            instruction("jr", "$ra");
            emit_delay();
        } else if (!fallthrough_epilogue) {
            instruction("b", epilogue_label_);
            emit_delay();
        } else if (delay_opcode != "nop") {
            instruction(delay_opcode, delay_operands);
        }
        return true;
    }

    void place_return(const machine::Function& function,
                      const machine::Instruction& value,
                      bool fallthrough_epilogue) {
        const auto& entity = hir_.function(function.source);
        if (!value.uses.empty()) {
            if (!active_signature_ ||
                active_signature_->layout.results.empty()) {
                diagnostics_.error(value.location,
                                   "MIPS return has no ABI result layout");
            } else {
                const auto& result =
                    active_signature_->layout.results.front();
                if (is_aggregate(hir_, entity.result_type)) {
                    const auto source = value.uses.front();
                    for (std::size_t index = 0; index < result.pieces.size();
                         ++index) {
                        const auto& piece = result.pieces[index];
                        if (!result.indirect) {
                            place_aggregate_piece(function, source, piece,
                                                  value.location);
                            continue;
                        }
                        if (!valid_aggregate_piece(source, piece,
                                                   value.location))
                            continue;
                        const auto* pointer =
                            named_slot(function, "$aggregate.sret." +
                                                     std::to_string(index));
                        if (!pointer || !pointer->frame_offset) {
                            diagnostics_.error(value.location,
                                               "MIPS aggregate result has no "
                                               "hidden pointer home");
                            continue;
                        }
                        instruction(address_load(),
                                    "$t0," + memory(*pointer->frame_offset));
                        normalize_integer("t0", hir_.address_bits, true);
                        copy_bytes("t0", 0, frame_base(),
                                   static_cast<std::int64_t>(vreg_offset(
                                       function, source, value.location)) +
                                       piece.value_bit_offset / 8U,
                                   piece.indirect_value_bits / 8U);
                        if (!result.indirect_result_reg.empty() && index == 0) {
                            instruction("move",
                                        reg_name(result.indirect_result_reg) +
                                            ",$t0");
                        }
                    }
                } else if (result.indirect || result.pieces.empty()) {
                    diagnostics_.error(
                        value.location,
                        "MIPS indirect function results are not implemented yet");
                } else if (place_composed_return(function, value, result,
                                                 fallthrough_epilogue)) {
                    return;
                } else {
                    const auto source = value.uses.front();
                    if (!is_floating(hir_, entity.result_type) &&
                        value.result_extension ==
                            machine::ExtensionKind::Zero &&
                        source.mode.bits <= 32 &&
                        result.pieces.size() == 2) {
                        const auto result_bits =
                            type_bits(hir_, entity.result_type);
                        const ValuePiece* low = nullptr;
                        const ValuePiece* high = nullptr;
                        for (const auto& piece : result.pieces) {
                            if (piece.location.kind !=
                                    LocationKind::Register ||
                                piece.carrier_bits != 32) {
                                low = nullptr;
                                high = nullptr;
                                break;
                            }
                            const auto offset = effective_piece_offset(
                                piece, result_bits,
                                subtarget_.target().data_layout.byte_order);
                            if (offset == 0 &&
                                piece.value_bits >= source.mode.bits) {
                                low = &piece;
                            }
                            if (offset >= source.mode.bits) high = &piece;
                        }
                        if (low && high) {
                            if (const auto assigned =
                                    assigned_gpr(function, source);
                                assigned &&
                                *assigned != low->location.reg &&
                                *assigned != high->location.reg) {
                                instruction(
                                    "move", reg_name(high->location.reg) +
                                                ",$zero");
                                const auto place_low = [&] {
                                    if (source.mode.bits == 32) {
                                        instruction(
                                            "sll",
                                            reg_name(low->location.reg) +
                                                "," + reg_name(*assigned) +
                                                ",0");
                                    } else {
                                        const auto mask =
                                            (std::uint64_t{1}
                                             << source.mode.bits) - 1U;
                                        instruction(
                                            "andi",
                                            reg_name(low->location.reg) +
                                                "," + reg_name(*assigned) +
                                                "," +
                                                std::to_string(mask));
                                    }
                                };
                                if (direct_return()) {
                                    instruction("jr", "$ra");
                                    place_low();
                                } else if (!fallthrough_epilogue) {
                                    instruction("b", epilogue_label_);
                                    place_low();
                                } else {
                                    place_low();
                                }
                                return;
                            }
                        }
                    }
                    if (is_floating(hir_, entity.result_type) &&
                        result.pieces.size() == 1 &&
                        result.pieces.front().location.kind ==
                            LocationKind::Register &&
                        fpr(result.pieces.front().location.reg)) {
                        load_fvreg(function, source,
                                   result.pieces.front().location.reg,
                                   value.location);
                    } else if (
                        !is_floating(hir_, entity.result_type) &&
                        value.result_extension ==
                            machine::ExtensionKind::None &&
                        place_split_integer_registers(
                            function, source, result.pieces,
                            type_bits(hir_, entity.result_type),
                            direct_return() || !fallthrough_epilogue)) {
                        return;
                    } else {
                        for (const auto& piece : result.pieces) {
                            if (is_floating(hir_, entity.result_type)) {
                                place_floating_piece(function, source, piece,
                                                     value.location);
                            } else if (
                                value.result_extension ==
                                    machine::ExtensionKind::Zero &&
                                effective_piece_offset(
                                    piece,
                                    type_bits(hir_, entity.result_type),
                                    subtarget_.target().data_layout.byte_order) >=
                                    source.mode.bits) {
                                if (piece.location.kind ==
                                    LocationKind::Register) {
                                    instruction(
                                        "move",
                                        reg_name(piece.location.reg) +
                                            ",$zero");
                                } else {
                                    store_integer_memory(
                                        "zero",
                                        memory(static_cast<std::int64_t>(
                                                   piece.location.stack_offset),
                                               "sp"),
                                        piece.carrier_bits);
                                }
                            } else {
                                place_integer_piece(
                                    function, source, piece,
                                    type_bits(hir_, entity.result_type),
                                    value.location, value.result_extension);
                            }
                        }
                    }
                }
            }
        }
        if (direct_return()) {
            instruction("jr", "$ra");
            instruction("nop");
        } else if (!fallthrough_epilogue) {
            instruction("b", epilogue_label_);
            instruction("nop");
        }
    }

    bool single_delay_slot_instruction(
        const machine::Function& function,
        const machine::Instruction& candidate) const {
        if (!options_.schedule_insns2 ||
            candidate.kind != machine::InstructionKind::Target ||
            candidate.may_load || candidate.may_store ||
            candidate.has_side_effects || candidate.patch ||
            candidate.defs.size() != 1) {
            return false;
        }

        const auto allocated = [&](machine::Register value) {
            if (value.kind != machine::RegisterKind::Virtual ||
                value.id >= function.virtual_register_classes.size()) {
                return false;
            }
            const auto kind = function.virtual_register_classes[value.id];
            if (kind == machine::VirtualRegisterClass::Floating) {
                return assigned_fpr(function, value).has_value();
            }
            if (kind != machine::VirtualRegisterClass::Integer) return false;
            return assigned_gpr(function, value).has_value();
        };
        const auto opcode = decode_opcode(candidate.opcode);
        if (opcode == Opcode::MulStart) {
            const auto memory_token = [&](machine::Register value) {
                return value.kind == machine::RegisterKind::Virtual &&
                    value.id < function.virtual_register_classes.size() &&
                    function.virtual_register_classes[value.id] ==
                        machine::VirtualRegisterClass::Memory;
            };
            if (candidate.uses.size() < 2 ||
                !assigned_gpr(function, candidate.uses[0]) ||
                !assigned_gpr(function, candidate.uses[1]) ||
                !memory_token(candidate.defs.front()) ||
                !std::all_of(candidate.uses.begin() + 2,
                             candidate.uses.end(), memory_token)) {
                return false;
            }
            return candidate.uses.front().mode.bits <= 32 ||
                subtarget_.has_feature(Feature::Mips3);
        }
        if (!std::all_of(candidate.defs.begin(), candidate.defs.end(),
                         allocated) ||
            !std::all_of(candidate.uses.begin(), candidate.uses.end(),
                         allocated)) {
            return false;
        }
        if (!subtarget_.has_feature(Feature::Mips3) &&
            std::any_of(candidate.defs.begin(), candidate.defs.end(),
                        [](machine::Register value) {
                            return value.mode.bits > 32;
                        })) {
            return false;
        }

        // Only admit Machine IR operations whose allocated form is exactly
        // one architectural instruction.  The delay-slot emitter can then
        // move it as a unit without parsing assembly text or splitting a
        // legalization sequence.
        if (opcode == Opcode::Constant && candidate.operands.size() == 1) {
            const auto* immediate =
                std::get_if<machine::ImmediateOperand>(
                    &candidate.operands.front());
            return immediate && immediate->value == 0 &&
                immediate->high == 0;
        }
        switch (opcode) {
        case Opcode::Neg:
        case Opcode::Not:
        case Opcode::Iszero:
        case Opcode::Fneg:
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Shl:
        case Opcode::ShrS:
        case Opcode::ShrU:
        case Opcode::CmpSlt:
        case Opcode::CmpSgt:
        case Opcode::CmpUlt:
        case Opcode::CmpUgt:
        case Opcode::Fadd:
        case Opcode::Fsub:
        case Opcode::Fdiv: return true;
        case Opcode::Fmul:
            // -mfix4300 emits a second architectural instruction after the
            // multiply. Moving only the multiply into a branch delay slot
            // could skip that separation on the taken edge.
            return !subtarget_.has_feature(Feature::Fix4300);
        default: return false;
        }
    }

    bool can_fill_delay_slot(const machine::Function& function,
                             const machine::Instruction& candidate,
                             const machine::Instruction& terminator,
                             machine::BlockId predecessor) const {
        if (!single_delay_slot_instruction(function, candidate) ||
            (terminator.kind != machine::InstructionKind::Branch &&
             terminator.kind !=
                 machine::InstructionKind::ConditionalBranch)) {
            return false;
        }

        if (terminator.kind == machine::InstructionKind::Branch) {
            if (terminator.operands.empty()) return false;
            const auto successor = std::get<machine::BlockOperand>(
                terminator.operands.front()).target;
            if (layout_successor(function, predecessor) == successor ||
                edge_has_phi_copies(function, predecessor, successor)) {
                return false;
            }
        } else {
            if (terminator.operands.size() < 3) return false;
            const auto yes = std::get<machine::BlockOperand>(
                terminator.operands[1]).target;
            const auto no = std::get<machine::BlockOperand>(
                terminator.operands[2]).target;
            const bool yes_copies =
                edge_has_phi_copies(function, predecessor, yes);
            const bool no_copies =
                edge_has_phi_copies(function, predecessor, no);
            const auto next = layout_successor(function, predecessor);
            // Conditional edge copies are emitted after the architectural
            // delay slot only on the directly encoded edge. Keep the
            // candidate before any stub that must perform copies for the
            // other edge.
            if ((yes_copies || no_copies) &&
                !((next == yes && !no_copies) ||
                  (next == no && !yes_copies))) {
                return false;
            }
            for (const auto definition : candidate.defs) {
                if (std::any_of(
                        terminator.uses.begin(), terminator.uses.end(),
                        [&](machine::Register use) {
                            return same_physical_assignment(
                                function, definition, use);
                        })) {
                    return false;
                }
            }
        }
        return true;
    }

    bool single_fused_compare_instruction(
        const machine::Function& function,
        const machine::Instruction& candidate) const {
        if (!options_.schedule_insns2 ||
            candidate.kind !=
                machine::InstructionKind::ConditionalBranch ||
            candidate.condition_predicate.empty() ||
            candidate.uses.size() < 2) {
            return false;
        }
        const auto predicate = decode_opcode(
            candidate.condition_predicate);
        if (predicate < Opcode::CmpSlt ||
            predicate > Opcode::CmpUge) {
            return false;
        }
        const unsigned register_bits =
            subtarget_.has_feature(Feature::Mips3) ? 64U : 32U;
        return candidate.uses[0].mode.bits <= register_bits &&
            candidate.uses[1].mode.bits <= register_bits &&
            assigned_gpr(function, candidate.uses[0]).has_value() &&
            assigned_gpr(function, candidate.uses[1]).has_value();
    }

    std::optional<std::pair<std::size_t, std::size_t>> delay_slot_plan(
        const machine::Function& function,
        const machine::Block& block) const {
        if (!options_.schedule_insns2) return std::nullopt;
        for (std::size_t terminator_index = block.instructions.size();
             terminator_index-- > 0;) {
            const auto& terminator = block.instructions[terminator_index];
            if (terminator.kind != machine::InstructionKind::Branch &&
                terminator.kind !=
                    machine::InstructionKind::ConditionalBranch) {
                continue;
            }
            for (std::size_t candidate_index = terminator_index;
                 candidate_index-- > 0;) {
                const auto& candidate = block.instructions[candidate_index];
                if (candidate.kind != machine::InstructionKind::Target ||
                    candidate.may_store || candidate.has_side_effects ||
                    candidate.patch) {
                    break;
                }
                if (!can_fill_delay_slot(function, candidate, terminator,
                                         block.id)) {
                    continue;
                }
                bool legal = true;
                for (std::size_t between = candidate_index + 1;
                     between < terminator_index && legal; ++between) {
                    const auto& crossed = block.instructions[between];
                    if (crossed.kind != machine::InstructionKind::Target ||
                        crossed.may_store || crossed.has_side_effects ||
                        crossed.patch) {
                        legal = false;
                        break;
                    }
                    for (const auto definition : candidate.defs) {
                        const bool read_or_overwritten =
                            std::any_of(
                                crossed.uses.begin(), crossed.uses.end(),
                                [&](machine::Register use) {
                                    return same_physical_assignment(
                                        function, definition, use);
                                }) ||
                            std::any_of(
                                crossed.defs.begin(), crossed.defs.end(),
                                [&](machine::Register crossed_definition) {
                                    return same_physical_assignment(
                                        function, definition,
                                        crossed_definition);
                                });
                        if (read_or_overwritten) {
                            legal = false;
                            break;
                        }
                    }
                    for (const auto use : candidate.uses) {
                        for (const auto definition : crossed.defs) {
                            if (same_physical_assignment(function, use,
                                                         definition)) {
                                legal = false;
                                break;
                            }
                        }
                        if (!legal) break;
                    }
                }
                if (legal) {
                    return std::pair{candidate_index, terminator_index};
                }
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    void plan_successor_delay_slots(const machine::Function& function) {
        edge_delay_entries_.clear();
        successor_delay_entries_.clear();
        if (!options_.schedule_insns2) return;

        const auto no_output_prefix = [](const machine::Instruction& value) {
            if (value.kind != machine::InstructionKind::Target) return false;
            const auto opcode = decode_opcode(value.opcode);
            return opcode == Opcode::Phi ||
                opcode == Opcode::LifetimeStart ||
                opcode == Opcode::LifetimeEnd ||
                opcode == Opcode::IntrinsicNoop;
        };
        // A non-interlocked MIPS I--III core forbids a HI/LO writer in the
        // two instructions following MFHI/MFLO.  The branch itself pays one
        // slot, so a moved MulStart is safe when at least one emitted
        // instruction follows the most recent MoveFromLo.  Only an
        // unconditional branch to the next layout block can cross a block
        // boundary without emitting a branch and delay slot; walk exactly
        // that fallthrough chain.  Every other incoming edge clears the
        // two-instruction exclusion window by construction.
        const auto hilo_delay_safe = [&](machine::BlockId source) {
            if (subtarget_.has_feature(Feature::HiloInterlocks)) return true;
            std::unordered_set<std::uint32_t> visited;
            auto current = source;
            while (visited.insert(current.value).second) {
                const auto block = std::find_if(
                    function.blocks.begin(), function.blocks.end(),
                    [&](const machine::Block& candidate) {
                        return candidate.id == current;
                    });
                if (block == function.blocks.end() ||
                    block->instructions.empty()) {
                    return true;
                }
                auto end = block->instructions.size();
                if (block->instructions.back().kind !=
                    machine::InstructionKind::Target) {
                    --end;
                }
                while (end != 0) {
                    const auto& value = block->instructions[--end];
                    if (no_output_prefix(value)) continue;
                    if (value.kind != machine::InstructionKind::Target) {
                        return true;
                    }
                    return decode_opcode(value.opcode) != Opcode::MoveFromLo;
                }

                const auto layout = std::find(
                    function.layout.begin(), function.layout.end(), current);
                if (layout == function.layout.begin() ||
                    layout == function.layout.end()) {
                    return true;
                }
                const auto previous_id = *std::prev(layout);
                const auto previous = std::find_if(
                    function.blocks.begin(), function.blocks.end(),
                    [&](const machine::Block& candidate) {
                        return candidate.id == previous_id;
                    });
                if (previous == function.blocks.end() ||
                    previous->instructions.empty()) {
                    return true;
                }
                const auto& terminator = previous->instructions.back();
                if (terminator.kind != machine::InstructionKind::Branch ||
                    terminator.operands.empty()) {
                    return true;
                }
                const auto* target = std::get_if<machine::BlockOperand>(
                    &terminator.operands.front());
                if (!target || target->target != current ||
                    layout_successor(function, previous_id) != current) {
                    return true;
                }
                current = previous_id;
            }
            return false;
        };
        const auto plan_edge = [&](const machine::Block& source,
                                   machine::BlockId target,
                                   bool allow_fused_compare,
                                   bool prior_branch_clears_hilo = false) {
            // A successor's first operation cannot run before its prologue.
            if (target == source.id ||
                function.frame.prologue_block == target ||
                layout_successor(function, source.id) == target ||
                edge_has_phi_copies(function, source.id, target)) {
                return;
            }
            const auto successor = std::find_if(
                function.blocks.begin(), function.blocks.end(),
                [&](const machine::Block& block) {
                    return block.id == target;
                });
            if (successor == function.blocks.end()) return;

            std::optional<std::size_t> candidate_index;
            bool fused_compare = false;
            for (std::size_t index = 0;
                 index < successor->instructions.size(); ++index) {
                const auto& candidate = successor->instructions[index];
                if (no_output_prefix(candidate)) continue;
                if (single_delay_slot_instruction(function, candidate)) {
                    if (decode_opcode(candidate.opcode) == Opcode::MulStart &&
                        !prior_branch_clears_hilo &&
                        !hilo_delay_safe(source.id)) {
                        break;
                    }
                    candidate_index = index;
                } else if (allow_fused_compare &&
                           single_fused_compare_instruction(
                               function, candidate)) {
                    candidate_index = index;
                    fused_compare = true;
                }
                break;
            }
            if (!candidate_index) return;
            if (const auto successor_plan =
                    delay_slot_plan(function, *successor);
                successor_plan &&
                successor_plan->first == *candidate_index) {
                return;
            }

            auto entry = successor_delay_entries_.find(target.value);
            if (entry == successor_delay_entries_.end()) {
                SuccessorDelayEntry planned{
                    target, *candidate_index, local_label(function),
                    fused_compare};
                entry = successor_delay_entries_
                            .emplace(target.value, std::move(planned))
                            .first;
            }
            edge_delay_entries_.emplace(source.id.value, entry->second);
        };
        for (const auto& source : function.blocks) {
            if (source.instructions.empty()) continue;
            const auto& branch = source.instructions.back();
            if (branch.kind == machine::InstructionKind::Branch) {
                if (branch.operands.size() != 1 ||
                    delay_slot_plan(function, source)) {
                    continue;
                }
                const auto* target = std::get_if<machine::BlockOperand>(
                    &branch.operands.front());
                if (target) plan_edge(source, target->target, true);
                continue;
            }
            if (branch.kind !=
                    machine::InstructionKind::ConditionalBranch ||
                branch.operands.size() < 3) {
                continue;
            }
            const auto* yes = std::get_if<machine::BlockOperand>(
                &branch.operands[1]);
            const auto* no = std::get_if<machine::BlockOperand>(
                &branch.operands[2]);
            if (!yes || !no || delay_slot_plan(function, source) ||
                edge_has_phi_copies(function, source.id, yes->target) ||
                edge_has_phi_copies(function, source.id, no->target)) {
                continue;
            }
            const auto next = layout_successor(function, source.id);
            if (next != yes->target && next != no->target) {
                // Both conditional encodings emit the false edge as a
                // secondary unconditional branch when neither destination
                // is the layout successor. Its otherwise-empty delay slot
                // can execute the false successor's first operation.
                plan_edge(source, no->target, true, true);
            } else if (subtarget_.has_feature(Feature::BranchLikely)) {
                // A branch-likely annuls its delay slot on the fallthrough
                // edge. This makes the first target operation edge-specific
                // without speculative side effects or physical-register
                // liveness assumptions.
                if (next == yes->target) {
                    plan_edge(source, no->target, false);
                } else if (next == no->target) {
                    plan_edge(source, yes->target, false);
                }
            }
        }

        // When every incoming edge executes the same target instruction in
        // its delay slot, no ordinary entry can reach the original copy.
        // Remove that copy rather than trading one NOP for duplicated code.
        for (auto& [target, entry] : successor_delay_entries_) {
            if (entry.fused_compare || entry.successor.value != target ||
                entry.successor == function.entry ||
                std::any_of(
                    function.labels.begin(), function.labels.end(),
                    [&](const machine::Function::LocalLabel& label) {
                        return label.block == entry.successor;
                    })) {
                continue;
            }
            bool has_predecessor = false;
            bool every_predecessor_planned = true;
            for (const auto& source : function.blocks) {
                if (std::find(source.successors.begin(),
                              source.successors.end(), entry.successor) ==
                    source.successors.end()) {
                    continue;
                }
                has_predecessor = true;
                const auto planned =
                    edge_delay_entries_.find(source.id.value);
                if (planned == edge_delay_entries_.end() ||
                    planned->second.successor != entry.successor ||
                    planned->second.instruction != entry.instruction ||
                    planned->second.fused_compare != entry.fused_compare) {
                    every_predecessor_planned = false;
                    break;
                }
            }
            entry.elide_original =
                has_predecessor && every_predecessor_planned;
        }
        for (auto& [source, entry] : edge_delay_entries_) {
            (void)source;
            const auto canonical =
                successor_delay_entries_.find(entry.successor.value);
            if (canonical != successor_delay_entries_.end()) {
                entry.elide_original = canonical->second.elide_original;
            }
        }
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
    void emit_select_group(
        const machine::Function& function,
        std::span<const machine::Instruction> values);
    bool emit_fused_order_compare(
        const machine::Function& function,
        const machine::Instruction& branch);
    void emit_successor_delay_branch(
        const machine::Function& function,
        machine::BlockId predecessor,
        machine::BlockId successor);
    void emit_terminator(const machine::Function& function,
                          const machine::Instruction& value,
                          machine::BlockId predecessor,
                          const machine::Instruction* delay = nullptr,
                          std::string_view resume_after_compare = {});
    void emit_function(machine::Function& function);

    machine::Module& module_;
    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    ObjectFormat format_;
    bool numeric_gprs_{};
    bool wide_addresses_{};
    std::ostringstream output_;
    std::unordered_set<std::uint32_t> emitted_patch_cells_;
    std::optional<ActiveSignature> active_signature_;
    std::uint32_t frame_size_{};
    std::uint32_t saved_fp_offset_{};
    std::uint32_t saved_ra_offset_{};
    // Blocks of the active function that run with its frame, and whether the
    // code being emitted does.
    std::vector<bool> framed_;
    bool frame_active_{true};
    SourceLocation function_location_;
    bool frame_pointer_active_{};
    bool saves_fp_{};
    bool saves_ra_{};
    bool has_call_{};
    std::uint32_t next_label_{};
    unsigned hilo_write_barrier_{};
    bool fpu_multiply_pending_{};
    std::optional<std::string> fpu_transfer_delay_register_;
    std::vector<FloatLiteral> float_literals_;
    std::vector<IntegerLiteral> integer_literals_;
    std::string epilogue_label_;
    std::unordered_map<std::uint32_t, SuccessorDelayEntry>
        edge_delay_entries_;
    std::unordered_map<std::uint32_t, SuccessorDelayEntry>
        successor_delay_entries_;
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
    if (value.uses.empty() || value.defs.empty() ||
        value.operands.size() < 2) {
        return;
    }
    const auto left = value.uses[0];
    const auto* immediate =
        std::get_if<machine::ImmediateOperand>(&value.operands[1]);
    if (!immediate && value.uses.size() < 2) return;
    const auto right = immediate ? machine::Register{} : value.uses[1];
    const auto target = value.defs.front();
    const bool wide = left.mode.bits > 32;
    if (wide && !subtarget_.has_feature(Feature::Mips3)) {
        emit_integer_pair_binary(function, value);
        return;
    }
    const auto opcode = decode_opcode(value.opcode);
    // Allegrex RORV is currently emitted as a fixed instruction word. Keep
    // that one form on its fixed scratch tuple until the encoder accepts
    // arbitrary register fields; all ordinary MIPS-III operations consume
    // and define their allocated registers directly.
    const bool fixed_rotate = !wide && subtarget_.has_feature(Feature::Rotate) &&
        (opcode == Opcode::Rotl || opcode == Opcode::Rotr);
    const auto left_gpr = fixed_rotate
        ? (load_vreg(function, left, "t0", value.location),
           std::string_view{"t0"})
        : input_gpr(function, left, "t0", value.location);
    const auto right_gpr = immediate ? std::string_view{} : fixed_rotate
        ? (load_vreg(function, right, "t1", value.location),
           std::string_view{"t1"})
        : input_gpr(function, right, "t1", value.location);
    const auto target_gpr = fixed_rotate
        ? std::string_view{"t2"}
        : output_gpr(function, target, "t2");
    const auto binary_operands = [&] {
        return reg_name(target_gpr) + "," + reg_name(left_gpr) + "," +
            reg_name(right_gpr);
    };
    const auto immediate_text = [&] {
        return immediate->is_signed
            ? std::to_string(static_cast<std::int64_t>(immediate->value))
            : std::to_string(immediate->value);
    };
    const auto immediate_operands = [&] {
        return reg_name(target_gpr) + "," + reg_name(left_gpr) + "," +
            immediate_text();
    };
    const auto shift_immediate = [&](std::string_view narrow,
                                     std::string_view low,
                                     std::string_view high) {
        auto amount = static_cast<unsigned>(immediate->value);
        auto mnemonic = narrow;
        if (wide) {
            mnemonic = amount < 32 ? low : high;
            amount %= 32;
        }
        instruction(mnemonic, reg_name(target_gpr) + "," +
                                  reg_name(left_gpr) + "," +
                                  std::to_string(amount));
    };
    switch (opcode) {
    case Opcode::Add:
        instruction(immediate ? (wide ? "daddiu" : "addiu")
                              : (wide ? "daddu" : "addu"),
                    immediate ? immediate_operands() : binary_operands());
        break;
    case Opcode::Sub:
        instruction(wide ? "dsubu" : "subu", binary_operands());
        break;
    case Opcode::Mul:
        instruction(wide ? "dmult" : "mult",
                    reg_name(left_gpr) + "," + reg_name(right_gpr));
        instruction("mflo", reg_name(target_gpr));
        break;
    case Opcode::Sdiv:
    case Opcode::Srem:
        instruction(wide ? "ddiv" : "div",
                    "$zero," + reg_name(left_gpr) + "," +
                        reg_name(right_gpr));
        instruction(value.opcode == Opcode::Srem ? "mfhi" : "mflo",
                    reg_name(target_gpr));
        break;
    case Opcode::Udiv:
    case Opcode::Urem:
        instruction(wide ? "ddivu" : "divu",
                    "$zero," + reg_name(left_gpr) + "," +
                        reg_name(right_gpr));
        instruction(value.opcode == Opcode::Urem ? "mfhi" : "mflo",
                    reg_name(target_gpr));
        break;
    case Opcode::Sdivrem:
    case Opcode::Udivrem: {
        instruction(opcode == Opcode::Sdivrem ? (wide ? "ddiv" : "div")
                                              : (wide ? "ddivu" : "divu"),
                    "$zero," + reg_name(left_gpr) + "," +
                        reg_name(right_gpr));
        const auto remainder = value.defs[1];
        const auto remainder_gpr = output_gpr(function, remainder, "t3");
        instruction("mflo", reg_name(target_gpr));
        instruction("mfhi", reg_name(remainder_gpr));
        commit_gpr(function, remainder, remainder_gpr, value.location);
        break;
    }
    case Opcode::And:
        instruction(immediate ? "andi" : "and",
                    immediate ? immediate_operands() : binary_operands());
        break;
    case Opcode::Or:
        instruction(immediate ? "ori" : "or",
                    immediate ? immediate_operands() : binary_operands());
        break;
    case Opcode::Xor:
        instruction(immediate ? "xori" : "xor",
                    immediate ? immediate_operands() : binary_operands());
        break;
    case Opcode::Shl:
        if (immediate) shift_immediate("sll", "dsll", "dsll32");
        else instruction(wide ? "dsllv" : "sllv", binary_operands());
        break;
    case Opcode::ShrS:
        if (immediate) shift_immediate("sra", "dsra", "dsra32");
        else instruction(wide ? "dsrav" : "srav", binary_operands());
        break;
    case Opcode::ShrU:
        if (immediate) shift_immediate("srl", "dsrl", "dsrl32");
        else instruction(wide ? "dsrlv" : "srlv", binary_operands());
        break;
    case Opcode::Rotl:
        if (!wide && subtarget_.has_feature(Feature::Rotate)) {
            instruction("subu", "$t3,$zero,$t1");
            encoded(0x01685046U, "rorv $t2,$t0,$t3");
        } else {
            instruction(wide ? "dsubu" : "subu",
                        "$t3,$zero," + reg_name(right_gpr));
            // The rotate expansion needs its original left operand twice;
            // avoid destroying it when allocation coalesces the result.
            const auto rotate_target = target_gpr == left_gpr
                ? std::string_view{"t2"} : target_gpr;
            instruction(wide ? "dsllv" : "sllv",
                        reg_name(rotate_target) + "," + reg_name(left_gpr) +
                            "," + reg_name(right_gpr));
            instruction(wide ? "dsrlv" : "srlv",
                        "$t3," + reg_name(left_gpr) + ",$t3");
            instruction("or", reg_name(target_gpr) + "," +
                                  reg_name(rotate_target) + ",$t3");
        }
        break;
    case Opcode::Rotr:
        if (!wide && subtarget_.has_feature(Feature::Rotate)) {
            encoded(0x01285046U, "rorv $t2,$t0,$t1");
        } else {
            instruction(wide ? "dsubu" : "subu",
                        "$t3,$zero," + reg_name(right_gpr));
            const auto rotate_target = target_gpr == left_gpr
                ? std::string_view{"t2"} : target_gpr;
            instruction(wide ? "dsrlv" : "srlv",
                        reg_name(rotate_target) + "," + reg_name(left_gpr) +
                            "," + reg_name(right_gpr));
            instruction(wide ? "dsllv" : "sllv",
                        "$t3," + reg_name(left_gpr) + ",$t3");
            instruction("or", reg_name(target_gpr) + "," +
                                  reg_name(rotate_target) + ",$t3");
        }
        break;
    case Opcode::CmpEq:
        instruction("xor", binary_operands());
        instruction("sltiu", reg_name(target_gpr) + "," +
                                  reg_name(target_gpr) + ",1");
        break;
    case Opcode::CmpNe:
        instruction("xor", binary_operands());
        instruction("sltu", reg_name(target_gpr) + ",$zero," +
                                 reg_name(target_gpr));
        break;
    case Opcode::CmpSlt: instruction("slt", binary_operands()); break;
    case Opcode::CmpUlt: instruction("sltu", binary_operands()); break;
    case Opcode::CmpSgt:
        instruction("slt", reg_name(target_gpr) + "," +
                               reg_name(right_gpr) + "," +
                               reg_name(left_gpr));
        break;
    case Opcode::CmpUgt:
        instruction("sltu", reg_name(target_gpr) + "," +
                                reg_name(right_gpr) + "," +
                                reg_name(left_gpr));
        break;
    case Opcode::CmpSle:
        instruction("slt", reg_name(target_gpr) + "," +
                               reg_name(right_gpr) + "," +
                               reg_name(left_gpr));
        instruction("xori", reg_name(target_gpr) + "," +
                                 reg_name(target_gpr) + ",1");
        break;
    case Opcode::CmpUle:
        instruction("sltu", reg_name(target_gpr) + "," +
                                reg_name(right_gpr) + "," +
                                reg_name(left_gpr));
        instruction("xori", reg_name(target_gpr) + "," +
                                 reg_name(target_gpr) + ",1");
        break;
    case Opcode::CmpSge:
        instruction("slt", binary_operands());
        instruction("xori", reg_name(target_gpr) + "," +
                                 reg_name(target_gpr) + ",1");
        break;
    case Opcode::CmpUge:
        instruction("sltu", binary_operands());
        instruction("xori", reg_name(target_gpr) + "," +
                                 reg_name(target_gpr) + ",1");
        break;
    default:
        diagnostics_.error(value.location,
                           "unknown MIPS integer Machine IR operation");
        return;
    }
    commit_gpr(function, target, target_gpr, value.location);
}

void AssemblyEmitter::emit_floating_binary(
    const machine::Function& function,
    const machine::Instruction& value) {
    if (value.uses.size() < 2 || value.defs.empty()) return;
    const auto left = value.uses[0];
    const auto right = value.uses[1];
    const auto target = value.defs.front();
    const std::string suffix = left.mode.bits == 32 ? ".s" : ".d";
    const auto left_fpr = input_fpr(
        function, left, "f0", value.location);
    const auto right_fpr = input_fpr(
        function, right, "f2", value.location);
    const auto opcode = decode_opcode(value.opcode);
    if (opcode >= Opcode::Fadd && opcode <= Opcode::Fdiv) {
        const auto target_fpr = output_fpr(function, target, "f4");
        const auto operands = reg_name(target_fpr) + "," +
            reg_name(left_fpr) + "," + reg_name(right_fpr);
        switch (opcode) {
        case Opcode::Fadd: instruction("add" + suffix, operands); break;
        case Opcode::Fsub: instruction("sub" + suffix, operands); break;
        case Opcode::Fmul: instruction("mul" + suffix, operands); break;
        case Opcode::Fdiv: instruction("div" + suffix, operands); break;
        default: break;
        }
        commit_fpr(function, target, target_fpr, value.location);
        return;
    }
    bool true_on_condition = true;
    const auto comparison_operands =
        reg_name(left_fpr) + "," + reg_name(right_fpr);
    switch (opcode) {
    case Opcode::FcmpEq:
        instruction("c.eq" + suffix, comparison_operands);
        break;
    case Opcode::FcmpNe:
        instruction("c.eq" + suffix, comparison_operands);
        true_on_condition = false;
        break;
    case Opcode::FcmpLt:
        instruction("c.lt" + suffix, comparison_operands);
        break;
    case Opcode::FcmpLe:
        instruction("c.le" + suffix, comparison_operands);
        break;
    case Opcode::FcmpGt:
        instruction("c.lt" + suffix,
                    reg_name(right_fpr) + "," + reg_name(left_fpr));
        break;
    case Opcode::FcmpGe:
        instruction("c.le" + suffix,
                    reg_name(right_fpr) + "," + reg_name(left_fpr));
        break;
    default:
        diagnostics_.error(value.location,
                           "unknown MIPS floating Machine IR operation");
        return;
    }
    const auto yes = local_label(function);
    const auto done = local_label(function);
    const auto result_gpr = output_gpr(function, target, "t0");
    instruction(true_on_condition ? "bc1t" : "bc1f", yes);
    instruction("move", reg_name(result_gpr) + ",$zero");
    instruction("b", done);
    instruction("nop");
    output_ << yes << ":\n";
    instruction("li", reg_name(result_gpr) + ",1");
    output_ << done << ":\n";
    commit_gpr(function, target, result_gpr, value.location);
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
                canonicalize_integer_value(
                    "t0", target.mode.bits, false);
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
            move_fpr_to_gpr("f0", "t0", source.mode.bits);
            store_vreg(function, target, "t0", value.location);
        } else if (target_float) {
            load_vreg(function, source, "t0", value.location);
            move_gpr_to_fpr("t0", "f0", target.mode.bits);
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
        const auto destination = target.mode.bits == 32
            ? std::string{"cvt.s."} : std::string{"cvt.d."};
        if (opcode == Opcode::Uitofp && source.mode.bits <= 32) {
            // MIPS has signed-word conversion only. For a high-bit u32, an
            // f32 uses the standard sticky-low-bit halving expansion so its
            // first rounding makes the same tie decision as a direct unsigned
            // conversion. An f64 can represent every u32 exactly, so convert
            // floor(value / 2), double it, and add the saved low bit.
            const auto ordinary = local_label(function);
            const auto done = local_label(function);
            instruction("bgez", "$t0," + ordinary);
            instruction("srl", "$t1,$t0,1");
            instruction("andi", "$t0,$t0,1");
            if (target.mode.bits <= 32) {
                instruction("or", "$t0,$t1,$t0");
            } else {
                move_gpr_to_fpr("t0", "f4", 32);
            }
            if (target.mode.bits > 32) {
                instruction("move", "$t0,$t1");
            }
            move_gpr_to_fpr("t0", "f0", 32);
            instruction(destination + "w", "$f2,$f0");
            instruction(target.mode.bits == 32 ? "add.s" : "add.d",
                        "$f2,$f2,$f2");
            if (target.mode.bits > 32) {
                instruction("cvt.d.w", "$f4,$f4");
                instruction("add.d", "$f2,$f2,$f4");
            }
            instruction("b", done);
            instruction("nop");
            output_ << ordinary << ":\n";
            move_gpr_to_fpr("t0", "f0", 32);
            instruction(destination + "w", "$f2,$f0");
            output_ << done << ":\n";
            store_fvreg(function, target, "f2", value.location);
            return;
        }
        const bool use_long = source.mode.bits > 32;
        if (use_long && !subtarget_.has_feature(Feature::Fp64)) {
            diagnostics_.error(
                value.location,
                "64-bit integer-to-float conversion requires 64-bit MIPS FPR mode; standalone Cross will not insert a runtime call");
            return;
        }
        if (opcode == Opcode::Uitofp && source.mode.bits == 64) {
            diagnostics_.error(
                value.location,
                "full-range u64-to-float conversion is not implemented in the MIPS slice yet");
            return;
        }
        move_gpr_to_fpr("t0", "f0", use_long ? 64U : 32U);
        instruction(destination + std::string(use_long ? "l" : "w"),
                    "$f2,$f0");
        store_fvreg(function, target, "f2", value.location);
        return;
    }
    if (opcode == Opcode::Fptosi || opcode == Opcode::Fptoui) {
        load_fvreg(function, source, "f0", value.location);
        if (opcode == Opcode::Fptoui && target.mode.bits == 64) {
            diagnostics_.error(
                value.location,
                "full-range float-to-u64 conversion is not implemented in the MIPS slice yet");
            return;
        }
        const auto source_suffix = source.mode.bits == 32 ? ".s" : ".d";
        if (target.mode.bits <= 32) {
            if (!subtarget_.has_feature(Feature::Mips2)) {
                diagnostics_.error(
                    value.location,
                    "float-to-integer conversion requires MIPS II trunc.w support; standalone Cross will not alter FCSR or insert a runtime call");
                return;
            }
            if (opcode == Opcode::Fptoui && target.mode.bits == 32) {
                // TRUNC.W is signed. Split the defined u32 range at 2^31,
                // subtract that exactly representable threshold on the high
                // path, then restore its integer bit. Construct the floating
                // threshold through a word FPR so FP32 targets never execute
                // the reserved TRUNC.L/DMFC1 forms.
                const auto ordinary = local_label(function);
                const auto done = local_label(function);
                instruction("lui", "$t0,32768");
                move_gpr_to_fpr("t0", "f4", 32);
                instruction(std::string("cvt") + source_suffix + ".w",
                            "$f4,$f4");
                instruction(std::string("abs") + source_suffix,
                            "$f4,$f4");
                instruction(std::string("c.lt") + source_suffix,
                            "$f0,$f4");
                instruction("bc1t", ordinary);
                instruction("nop");
                instruction(std::string("sub") + source_suffix,
                            "$f2,$f0,$f4");
                instruction(std::string("trunc.w") + source_suffix,
                            "$f2,$f2");
                move_fpr_to_gpr("f2", "t0", 32);
                instruction("lui", "$t1,32768");
                instruction("xor", "$t0,$t0,$t1");
                instruction("b", done);
                instruction("nop");
                output_ << ordinary << ":\n";
                instruction(std::string("trunc.w") + source_suffix,
                            "$f2,$f0");
                move_fpr_to_gpr("f2", "t0", 32);
                output_ << done << ":\n";
            } else {
                instruction(std::string("trunc.w") + source_suffix,
                            "$f2,$f0");
                move_fpr_to_gpr("f2", "t0", 32);
            }
        } else {
            if (!subtarget_.has_feature(Feature::Fp64)) {
                diagnostics_.error(
                    value.location,
                    "64-bit float-to-integer conversion requires 64-bit MIPS FPR mode; standalone Cross will not insert a runtime call");
                return;
            }
            instruction(std::string("trunc.l") + source_suffix,
                        "$f2,$f0");
            move_fpr_to_gpr("f2", "t0", 64);
        }
        canonicalize_integer_value(
            "t0", target.mode.bits, opcode == Opcode::Fptosi);
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
    const auto floating = [&](machine::Register reg) {
        return reg.kind == machine::RegisterKind::Virtual &&
               reg.id < function.virtual_register_classes.size() &&
               function.virtual_register_classes[reg.id] ==
                   machine::VirtualRegisterClass::Floating;
    };
    const auto load_bits = [&](machine::Register reg, std::string_view gpr) {
        if (floating(reg)) {
            load_fvreg(function, reg, "f0", value.location);
            move_fpr_to_gpr("f0", gpr, bits);
        } else {
            load_vreg(function, reg, gpr, value.location);
        }
    };
    const auto store_bits = [&](machine::Register reg, std::string_view gpr) {
        if (floating(reg)) {
            move_gpr_to_fpr(gpr, "f0", bits);
            store_fvreg(function, reg, "f0", value.location);
        } else {
            store_vreg(function, reg, gpr, value.location);
        }
    };
    load_vreg(function, value.uses.front(), "t0", value.location);
    if (opcode == Opcode::AtomicLoad) {
        instruction("sync");
        instruction("lw", "$t1,0($t0)");
        instruction("sync");
        if (!value.defs.empty()) {
            store_bits(value.defs.front(), "t1");
        }
        return;
    }
    if (opcode == Opcode::AtomicStore) {
        if (value.uses.size() < 2) return;
        load_bits(value.uses[1], "t1");
        instruction("sync");
        instruction("sw", "$t1,0($t0)");
        instruction("sync");
        return;
    }
    if (opcode == Opcode::AtomicCompareExchange) {
        if (value.uses.size() < 3 || value.defs.empty()) return;
        load_vreg(function, value.uses[1], "t3", value.location);
        instruction("lw", "$t4,0($t3)");
        load_bits(value.uses[2], "t5");
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
    load_bits(value.uses[1], "t3");
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
    store_bits(value.defs.front(), "t1");
}

void AssemblyEmitter::emit_phi_edge_copies(
    const machine::Function& function, machine::BlockId predecessor,
    machine::BlockId successor) {
    const auto found = std::find_if(
        function.blocks.begin(), function.blocks.end(),
        [&](const machine::Block& block) { return block.id == successor; });
    if (found == function.blocks.end()) return;
    struct Copy {
        machine::Register source;
        machine::Register target;
        SourceLocation location;
        bool source_temporary{};
    };
    std::vector<Copy> copies;
    std::vector<Copy> floating_copies;
    std::vector<Copy> fallback_copies;
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
            const auto target = phi.defs.front();
            const auto source_assignment =
                source->value.kind == machine::RegisterKind::Virtual &&
                        source->value.id <
                            function.virtual_register_assignments.size()
                    ? function.virtual_register_assignments[source->value.id]
                    : std::nullopt;
            const auto target_assignment =
                target.kind == machine::RegisterKind::Virtual &&
                        target.id <
                            function.virtual_register_assignments.size()
                    ? function.virtual_register_assignments[target.id]
                    : std::nullopt;
            if (source->value == target ||
                (source_assignment && target_assignment &&
                 *source_assignment == *target_assignment)) {
                break;
            }
            const bool scalar_integer =
                target.kind == machine::RegisterKind::Virtual &&
                target.id < function.virtual_register_classes.size() &&
                function.virtual_register_classes[target.id] ==
                    machine::VirtualRegisterClass::Integer &&
                !legalizes_to_pair(target) && target.mode.bits <= 64;
            const bool scalar_floating =
                target.kind == machine::RegisterKind::Virtual &&
                target.id < function.virtual_register_classes.size() &&
                function.virtual_register_classes[target.id] ==
                    machine::VirtualRegisterClass::Floating &&
                target.mode.bits <= 64;
            auto& destination = scalar_integer
                ? copies
                : scalar_floating ? floating_copies : fallback_copies;
            destination.push_back(
                {source->value, target, phi.location, false});
            break;
        }
    }
    const auto location_key = [&](machine::Register value,
                                  bool temporary) {
        if (temporary) return std::string("temporary");
        if (value.kind == machine::RegisterKind::Virtual &&
            value.id < function.virtual_register_assignments.size() &&
            function.virtual_register_assignments[value.id]) {
            return std::string("physical:") + std::to_string(
                function.virtual_register_assignments[value.id]->value);
        }
        return std::string("virtual:") + std::to_string(value.id);
    };
    const auto save_target = [&](const Copy& copy) {
        if (const auto target = assigned_gpr(function, copy.target)) {
            instruction("move", "$at," + reg_name(*target));
        } else {
            load_vreg(function, copy.target, "at", copy.location);
        }
    };
    const auto emit_copy = [&](const Copy& copy) {
        if (copy.source_temporary) {
            store_vreg(function, copy.target, "at", copy.location);
            return;
        }
        if (const auto source = assigned_gpr(function, copy.source)) {
            store_vreg(function, copy.target, *source, copy.location);
            return;
        }
        if (const auto target = assigned_gpr(function, copy.target)) {
            load_vreg(function, copy.source, *target, copy.location);
            return;
        }
        copy_vreg(function, copy.target, copy.source, copy.location);
    };

    const auto resolve = [&](std::vector<Copy>& pending,
                             const auto& save,
                             const auto& emit) {
        while (!pending.empty()) {
            std::optional<std::size_t> ready;
            for (std::size_t index = 0; index < pending.size(); ++index) {
                const auto target = location_key(pending[index].target, false);
                const bool target_is_source = std::any_of(
                    pending.begin(), pending.end(),
                    [&](const Copy& candidate) {
                        return location_key(candidate.source,
                                            candidate.source_temporary) ==
                               target;
                    });
                if (!target_is_source) {
                    ready = index;
                    break;
                }
            }
            if (ready) {
                emit(pending[*ready]);
                pending.erase(pending.begin() +
                              static_cast<std::ptrdiff_t>(*ready));
                continue;
            }

            const auto preserved =
                location_key(pending.front().target, false);
            save(pending.front());
            for (auto& copy : pending) {
                if (location_key(copy.source, copy.source_temporary) ==
                    preserved) {
                    copy.source_temporary = true;
                }
            }
        }
    };
    resolve(copies, save_target, emit_copy);

    const auto save_floating_target = [&](const Copy& copy) {
        if (const auto target = assigned_fpr(function, copy.target)) {
            instruction(copy.target.mode.bits == 32 ? "mov.s" : "mov.d",
                        "$f2," + reg_name(*target));
        } else {
            load_fvreg(function, copy.target, "f2", copy.location);
        }
    };
    const auto emit_floating_copy = [&](const Copy& copy) {
        if (copy.source_temporary) {
            store_fvreg(function, copy.target, "f2", copy.location);
            return;
        }
        if (const auto source = assigned_fpr(function, copy.source)) {
            store_fvreg(function, copy.target, *source, copy.location);
            return;
        }
        if (const auto target = assigned_fpr(function, copy.target)) {
            load_fvreg(function, copy.source, *target, copy.location);
            return;
        }
        copy_vreg(function, copy.target, copy.source, copy.location);
    };
    resolve(floating_copies, save_floating_target, emit_floating_copy);
    for (const auto& copy : fallback_copies) {
        if (!aggregate_vreg(function, copy.target)) continue;
        const auto* temporary = named_slot(
            function, "$aggregate.phi." + std::to_string(copy.target.id));
        if (!temporary || !temporary->frame_offset) {
            diagnostics_.error(copy.location,
                               "MIPS aggregate PHI has no copy cell");
            return;
        }
        copy_bytes(frame_base(), *temporary->frame_offset, frame_base(),
                   vreg_offset(function, copy.source, copy.location),
                   copy.target.mode.bits / 8U);
    }
    // Pre-MIPS-III register-pair phis retain their established lowering until
    // the paired-GPR class gains a dedicated allocator.
    for (const auto& copy : fallback_copies) {
        if (aggregate_vreg(function, copy.target)) {
            const auto* temporary = named_slot(
                function, "$aggregate.phi." + std::to_string(copy.target.id));
            copy_bytes(frame_base(),
                       vreg_offset(function, copy.target, copy.location),
                       frame_base(), *temporary->frame_offset,
                       copy.target.mode.bits / 8U);
        } else {
            copy_vreg(function, copy.target, copy.source, copy.location);
        }
    }
}

void AssemblyEmitter::emit_target(const machine::Function& function,
                                  const machine::Instruction& value) {
    const auto opcode = decode_opcode(value.opcode);
    if (opcode == Opcode::UnalignedLoadSigned ||
        opcode == Opcode::UnalignedLoadUnsigned ||
        opcode == Opcode::UnalignedStore) {
        const auto address =
            input_gpr(function, value.uses.front(), "t0", value.location);
        const bool load = opcode != Opcode::UnalignedStore;
        const auto scalar = load ? value.defs.front() : value.uses.back();
        const bool floating = scalar.kind == machine::RegisterKind::Virtual &&
                              function.virtual_register_classes[scalar.id] ==
                                  machine::VirtualRegisterClass::Floating;
        const auto carrier = aggregate_carrier_offset(function, value.location);
        if (load) {
            copy_bytes(frame_base(), carrier, address, 0,
                       (scalar.mode.bits + 7U) / 8U);
            if (floating) {
                instruction(scalar.mode.bits == 32 ? "lwc1" : "ldc1",
                            "$f0," + memory(carrier));
                store_fvreg(function, scalar, "f0", value.location);
            } else if (legalizes_to_pair(scalar)) {
                load_pair_memory("t1", "t2", carrier);
                store_vreg_pair(function, scalar, "t1", "t2", value.location);
            } else {
                load_integer_memory("t1", memory(carrier), scalar.mode.bits,
                                    opcode == Opcode::UnalignedLoadSigned);
                store_vreg(function, scalar, "t1", value.location);
            }
        } else {
            if (floating) {
                const auto source =
                    input_fpr(function, scalar, "f0", value.location);
                instruction(scalar.mode.bits == 32 ? "swc1" : "sdc1",
                            reg_name(source) + "," + memory(carrier));
            } else if (legalizes_to_pair(scalar)) {
                load_vreg_pair(function, scalar, "t1", "t2", value.location);
                store_pair_memory("t1", "t2", carrier);
            } else {
                const auto source =
                    input_gpr(function, scalar, "t1", value.location);
                store_integer_memory(source, memory(carrier), scalar.mode.bits);
            }
            copy_bytes(address, 0, frame_base(), carrier,
                       (scalar.mode.bits + 7U) / 8U);
        }
        return;
    }
    if (opcode == Opcode::AggregateLoad || opcode == Opcode::AggregateStore ||
        opcode == Opcode::AggregateIndexedLoad) {
        std::string_view base = frame_base();
        std::int64_t offset = 0;
        if (opcode == Opcode::AggregateIndexedLoad) {
            form_indexed_address(function, value, "t0");
            base = "t0";
        } else if (const auto* slot = std::get_if<machine::StackSlotOperand>(
                       &value.operands.front())) {
            offset = slot_offset(function, slot->slot, value.location) +
                     slot->offset;
        } else if (const auto* symbol = std::get_if<machine::SymbolOperand>(
                       &value.operands.front())) {
            materialize_symbol_address("t0", assembly_symbol(symbol->name));
            base = "t0";
        } else if (!value.uses.empty() &&
                   value.uses.front().kind == machine::RegisterKind::Virtual &&
                   function.virtual_register_classes[value.uses.front().id] ==
                       machine::VirtualRegisterClass::Memory) {
            // Qualifier-only record casts copy an SSA storage home, not a
            // scalar register or a pointer to foreign storage.
            offset = vreg_offset(function, value.uses.front(), value.location);
        } else {
            base =
                input_gpr(function, value.uses.front(), "t0", value.location);
        }
        const auto aggregate = opcode == Opcode::AggregateStore
                                   ? value.uses.back()
                                   : value.defs.front();
        const auto home = vreg_offset(function, aggregate, value.location);
        if (opcode == Opcode::AggregateStore) {
            copy_bytes(base, offset, frame_base(), home,
                       aggregate.mode.bits / 8U);
        } else {
            copy_bytes(frame_base(), home, base, offset,
                       aggregate.mode.bits / 8U);
        }
        return;
    }
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
            const auto end = ".Lcross.patch.value." +
                             std::to_string(value.patch->identity) + ".end";
            if (emitted_patch_cells_.insert(value.patch->identity).second) {
                const auto after = local_label(function);
                instruction("b", after);
                instruction("nop");
                output_ << ".p2align " << std::countr_zero(std::bit_ceil(bytes))
                        << '\n';
                if (bytes == 1) output_ << "\t.byte " << immediate.value << '\n';
                else if (bytes == 2) output_ << "\t.short " << immediate.value << '\n';
                else if (bytes == 4) output_ << "\t.word " << immediate.value << '\n';
                else output_ << "\t.dword " << immediate.value << '\n';
                // Keep end at the exact cell boundary, but branch only to an
                // instruction-aligned continuation after subword cells.
                output_ << end << ":\n.p2align 2\n" << after << ":\n";
            }
            const auto field = end + "-" + std::to_string(bytes);
            materialize_symbol_address("t0", field);
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
        if (opcode == Opcode::Fconstant) {
            const auto destination = assigned_fpr(function, target);
            if (destination && immediate.value == 0 && immediate.high == 0) {
                move_gpr_to_fpr("zero", *destination, target.mode.bits);
                return;
            }
            if (destination && prefer_float_literal(immediate)) {
                const auto& literal = float_literal(immediate);
                if (large_code_model()) {
                    materialize_symbol_address("t0", literal.label);
                    instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                                reg_name(*destination) + ",0($t0)");
                    return;
                }
                instruction("lui", "$t0,%hi(" + literal.label + ")");
                instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                            reg_name(*destination) + ",%lo(" + literal.label +
                                ")($t0)");
                return;
            }
            instruction(target.mode.bits > 32 ? "dli" : "li",
                        "$t0," + std::to_string(immediate.value));
            if (destination) {
                move_gpr_to_fpr("t0", *destination, target.mode.bits);
            } else {
                store_integer_memory(
                    "t0",
                    memory(vreg_offset(function, target, value.location)),
                    target.mode.bits);
            }
        } else {
            const auto destination = output_gpr(function, target, "t0");
            if (prefer_integer_literal(immediate)) {
                const auto& literal = integer_literal(immediate);
                instruction("lui", "$t0,%hi(" + literal.label + ")");
                instruction("ld", reg_name(destination) + ",%lo(" +
                                      literal.label + ")($t0)");
            } else {
                instruction(target.mode.bits > 32 ? "dli" : "li",
                            reg_name(destination) + "," +
                                std::to_string(immediate.value));
            }
            commit_gpr(function, target, destination, value.location);
        }
        return;
    }
    if (opcode == Opcode::StackAddress) {
        const auto& slot =
            std::get<machine::StackSlotOperand>(value.operands.front());
        const auto offset = slot_offset(function, slot.slot, value.location) +
                            slot.offset;
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t0");
        instruction(address_add_immediate(),
                    reg_name(destination) + "," + reg_name(frame_base()) +
                        "," + std::to_string(offset));
        commit_gpr(function, target, destination, value.location);
        return;
    }
    if (opcode == Opcode::GlobalAddress || opcode == Opcode::GlobalLoadSigned ||
        opcode == Opcode::GlobalLoadUnsigned || opcode == Opcode::FglobalLoad ||
        opcode == Opcode::GlobalStore || opcode == Opcode::FglobalStore) {
        const auto& symbol =
            std::get<machine::SymbolOperand>(value.operands.front());
        const auto name = assembly_symbol(symbol.name);
        const auto address = opcode == Opcode::GlobalAddress
            ? output_gpr(function, value.defs.front(), "t0")
            : std::string_view{"t0"};
        materialize_symbol_address(address, name);
        if (opcode == Opcode::GlobalAddress) {
            commit_gpr(function, value.defs.front(), address,
                       value.location);
        } else if (opcode == Opcode::GlobalLoadSigned ||
                   opcode == Opcode::GlobalLoadUnsigned) {
            const auto target = value.defs.front();
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, "t0");
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                const auto destination = output_gpr(function, target, "t1");
                load_integer_memory(destination, "0($t0)", target.mode.bits,
                                    opcode == Opcode::GlobalLoadSigned);
                commit_gpr(function, target, destination, value.location);
            }
        } else if (opcode == Opcode::FglobalLoad) {
            const auto target = value.defs.front();
            const auto destination = output_fpr(function, target, "f0");
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        reg_name(destination) + ",0($t0)");
            commit_fpr(function, target, destination, value.location);
        } else {
            const auto source = value.uses.front();
            if (opcode == Opcode::FglobalStore) {
                const auto source_fpr = input_fpr(
                    function, source, "f0", value.location);
                instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                            reg_name(source_fpr) + ",0($t0)");
            } else {
                if (legalizes_to_pair(source)) {
                    load_vreg_pair(function, source, "t1", "t2",
                                   value.location);
                    store_pair_memory("t1", "t2", 0, "t0");
                } else {
                    const auto source_gpr = input_gpr(
                        function, source, "t1", value.location);
                    store_integer_memory(source_gpr, "0($t0)",
                                         source.mode.bits);
                }
            }
        }
        return;
    }
    if (opcode == Opcode::LabelAddress) {
        const auto& block =
            std::get<machine::BlockOperand>(value.operands.front());
        const auto label = block_label(function, block.target);
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t0");
        materialize_symbol_address(destination, label);
        commit_gpr(function, target, destination, value.location);
        return;
    }
    if (opcode == Opcode::IndexedAddress) {
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t0");
        form_indexed_address(function, value, destination);
        commit_gpr(function, target, destination, value.location);
        return;
    }
    if (opcode == Opcode::PointerOffset) {
        if (value.uses.empty() || value.defs.empty() ||
            value.operands.size() < 2) {
            diagnostics_.error(value.location,
                               "malformed MIPS pointer-offset operation");
            return;
        }
        const auto* offset = std::get_if<machine::ImmediateOperand>(
            &value.operands.back());
        if (!offset || offset->high != 0 ||
            offset->value > static_cast<std::uint64_t>(
                                std::numeric_limits<std::int16_t>::max())) {
            diagnostics_.error(value.location,
                               "MIPS pointer offset exceeds signed 16-bit range");
            return;
        }
        const auto base = input_gpr(function, value.uses.front(), "t0",
                                    value.location);
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t1");
        if (offset->value == 0) {
            if (destination != base) {
                instruction("move", reg_name(destination) + "," +
                                        reg_name(base));
            }
        } else {
            instruction(hir_.address_bits > 32 ? "daddiu" : "addiu",
                        reg_name(destination) + "," + reg_name(base) + "," +
                            std::to_string(offset->value));
        }
        commit_gpr(function, target, destination, value.location);
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
                const auto destination = output_gpr(function, target, "t0");
                load_integer_memory(destination, memory(offset),
                                    target.mode.bits,
                                    opcode == Opcode::LoadSigned);
                commit_gpr(function, target, destination, value.location);
            }
        } else if (opcode == Opcode::Fload) {
            const auto target = value.defs.front();
            const auto destination = output_fpr(function, target, "f0");
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        reg_name(destination) + "," + memory(offset));
            commit_fpr(function, target, destination, value.location);
        } else if (opcode == Opcode::Fstore) {
            const auto source = value.uses.front();
            const auto source_fpr = input_fpr(
                function, source, "f0", value.location);
            instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                        reg_name(source_fpr) + "," + memory(offset));
        } else {
            const auto source = value.uses.front();
            if (legalizes_to_pair(source)) {
                load_vreg_pair(function, source, "t0", "t1",
                               value.location);
                store_pair_memory("t0", "t1", offset);
            } else {
                const auto source_gpr = input_gpr(
                    function, source, "t0", value.location);
                store_integer_memory(source_gpr, memory(offset),
                                     source.mode.bits);
            }
        }
        return;
    }
    if (opcode == Opcode::PointerLoadSigned ||
        opcode == Opcode::PointerLoadUnsigned ||
        opcode == Opcode::FpointerLoad || opcode == Opcode::PointerStore ||
        opcode == Opcode::FpointerStore) {
        std::uint64_t displacement{};
        if (value.operands.size() >= 2) {
            if (const auto* offset =
                    std::get_if<machine::ImmediateOperand>(
                        &value.operands.back())) {
                displacement = offset->value;
            }
        }
        const auto memory_operand = [&](std::string_view address) {
            return std::to_string(displacement) + "(" + reg_name(address) +
                   ")";
        };
        const auto address = input_gpr(
            function, value.uses.front(), "t0", value.location);
        if (opcode == Opcode::PointerLoadSigned ||
            opcode == Opcode::PointerLoadUnsigned) {
            const auto target = value.defs.front();
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2",
                                 static_cast<std::int64_t>(displacement),
                                 address);
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                const auto destination = output_gpr(function, target, "t1");
                load_integer_memory(destination,
                                    memory_operand(address),
                                    target.mode.bits,
                                    opcode == Opcode::PointerLoadSigned);
                commit_gpr(function, target, destination, value.location);
            }
        } else if (opcode == Opcode::FpointerLoad) {
            const auto target = value.defs.front();
            const auto destination = output_fpr(function, target, "f0");
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        reg_name(destination) + "," +
                            memory_operand(address));
            commit_fpr(function, target, destination, value.location);
        } else if (opcode == Opcode::FpointerStore) {
            const auto source = value.uses[1];
            const auto source_fpr = input_fpr(
                function, source, "f0", value.location);
            instruction(source.mode.bits == 32 ? "swc1" : "sdc1",
                        reg_name(source_fpr) + "," +
                            memory_operand(address));
        } else {
            const auto source = value.uses[1];
            if (legalizes_to_pair(source)) {
                load_vreg_pair(function, source, "t1", "t2",
                               value.location);
                store_pair_memory("t1", "t2",
                                  static_cast<std::int64_t>(displacement),
                                  address);
            } else {
                const auto source_gpr = input_gpr(
                    function, source, "t1", value.location);
                store_integer_memory(
                    source_gpr, memory_operand(address),
                    source.mode.bits);
            }
        }
        return;
    }
    if (opcode == Opcode::IndexedLoadSigned ||
        opcode == Opcode::IndexedLoadUnsigned ||
        opcode == Opcode::FindexedLoad) {
        const auto target = value.defs.front();
        const auto address = opcode != Opcode::FindexedLoad &&
                !legalizes_to_pair(target)
            ? output_gpr(function, target, "t0")
            : std::string_view{"t0"};
        form_indexed_address(function, value, address);
        if (opcode == Opcode::FindexedLoad) {
            const auto destination = output_fpr(function, target, "f0");
            instruction(target.mode.bits == 32 ? "lwc1" : "ldc1",
                        reg_name(destination) + ",0(" + reg_name(address) +
                            ")");
            commit_fpr(function, target, destination, value.location);
        } else {
            if (legalizes_to_pair(target)) {
                load_pair_memory("t1", "t2", 0, address);
                store_vreg_pair(function, target, "t1", "t2",
                                value.location);
            } else {
                load_integer_memory(address,
                                    "0(" + reg_name(address) + ")",
                                    target.mode.bits,
                                    opcode == Opcode::IndexedLoadSigned);
                commit_gpr(function, target, address, value.location);
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
        const auto source_gpr = input_gpr(
            function, source, "t0", value.location);
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t2");
        if (opcode == Opcode::Neg) {
            instruction(source.mode.bits > 32 ? "dsubu" : "subu",
                        reg_name(destination) + ",$zero," +
                            reg_name(source_gpr));
        } else if (opcode == Opcode::Not) {
            instruction("nor", reg_name(destination) + "," +
                                   reg_name(source_gpr) + ",$zero");
        } else {
            instruction("sltiu", reg_name(destination) + "," +
                                     reg_name(source_gpr) + ",1");
        }
        commit_gpr(function, target, destination, value.location);
        return;
    }
    if (opcode == Opcode::Fneg || opcode == Opcode::Fiszero) {
        const auto source = value.uses.front();
        const std::string suffix = source.mode.bits == 32 ? ".s" : ".d";
        const auto source_fpr = input_fpr(
            function, source, "f0", value.location);
        if (opcode == Opcode::Fneg) {
            const auto target = value.defs.front();
            const auto destination = output_fpr(function, target, "f2");
            instruction("neg" + suffix, reg_name(destination) + "," +
                                            reg_name(source_fpr));
            commit_fpr(function, target, destination, value.location);
        } else {
            if (source.mode.bits <= 32 ||
                subtarget_.has_feature(Feature::Mips3)) {
                move_gpr_to_fpr("zero", "f2", source.mode.bits);
            } else {
                instruction("mtc1", "$zero,$f2");
                instruction("mtc1", "$zero,$f3");
            }
            instruction("c.eq" + suffix,
                        reg_name(source_fpr) + ",$f2");
            const auto yes = local_label(function);
            const auto done = local_label(function);
            const auto target = value.defs.front();
            const auto destination = output_gpr(function, target, "t0");
            instruction("bc1t", yes);
            instruction("move", reg_name(destination) + ",$zero");
            instruction("b", done);
            instruction("nop");
            output_ << yes << ":\n";
            instruction("li", reg_name(destination) + ",1");
            output_ << done << ":\n";
            commit_gpr(function, target, destination, value.location);
        }
        return;
    }
    if (opcode == Opcode::MulStart) {
        if (value.uses.size() < 2) {
            diagnostics_.error(value.location,
                               "malformed MIPS split multiply producer");
            return;
        }
        const auto left = value.uses[0];
        const auto right = value.uses[1];
        const auto left_gpr = input_gpr(
            function, left, "t0", value.location);
        const auto right_gpr = input_gpr(
            function, right, "t1", value.location);
        instruction(left.mode.bits > 32 ? "dmult" : "mult",
                    reg_name(left_gpr) + "," + reg_name(right_gpr));
        return;
    }
    if (opcode == Opcode::MoveFromLo) {
        if (value.defs.empty()) {
            diagnostics_.error(value.location,
                               "malformed MIPS split multiply consumer");
            return;
        }
        const auto target = value.defs.front();
        const auto destination = output_gpr(function, target, "t2");
        instruction("mflo", reg_name(destination));
        commit_gpr(function, target, destination, value.location);
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
    if (opcode == Opcode::StackSave || opcode == Opcode::StackAllocate ||
        opcode == Opcode::StackRestore) {
        emit_dynamic_stack(function, value);
        return;
    }
    if (opcode == Opcode::VariadicState || opcode == Opcode::Invalid) {
        diagnostics_.error(value.location,
                           "unsupported operation reached the MIPS assembly emitter");
        return;
    }
    diagnostics_.error(value.location,
                       "unknown MIPS target opcode in assembly emission");
}

void AssemblyEmitter::emit_select_group(
    const machine::Function& function,
    std::span<const machine::Instruction> values) {
    if (values.size() < 2 || values.front().uses.size() < 3) return;
    const auto otherwise = local_label(function);
    const auto done = local_label(function);
    const auto condition = input_gpr(
        function, values.front().uses.front(), "t0",
        values.front().location);
    instruction("beq", reg_name(condition) + ",$zero," + otherwise);
    instruction("nop");
    for (const auto& value : values) {
        copy_vreg(function, value.defs.front(), value.uses[1],
                  value.location);
    }
    instruction("b", done);
    instruction("nop");
    output_ << otherwise << ":\n";
    for (const auto& value : values) {
        copy_vreg(function, value.defs.front(), value.uses[2],
                  value.location);
    }
    output_ << done << ":\n";
}

bool AssemblyEmitter::emit_fused_order_compare(
    const machine::Function& function,
    const machine::Instruction& branch) {
    if (branch.condition_predicate.empty() || branch.uses.size() < 2) {
        return false;
    }
    const auto predicate = decode_opcode(branch.condition_predicate);
    if (predicate < Opcode::CmpSlt || predicate > Opcode::CmpUge) {
        return false;
    }
    const auto left = input_gpr(
        function, branch.uses[0], "t0", branch.location);
    const auto right = input_gpr(
        function, branch.uses[1], "t1", branch.location);
    const bool unsigned_compare =
        predicate >= Opcode::CmpUlt && predicate <= Opcode::CmpUge;
    const bool swap = predicate == Opcode::CmpSle ||
        predicate == Opcode::CmpSgt ||
        predicate == Opcode::CmpUle ||
        predicate == Opcode::CmpUgt;
    instruction(unsigned_compare ? "sltu" : "slt",
                std::string{"$at,"} + reg_name(swap ? right : left) +
                    "," + reg_name(swap ? left : right));
    return true;
}

void AssemblyEmitter::emit_successor_delay_branch(
    const machine::Function& function,
    machine::BlockId predecessor,
    machine::BlockId successor) {
    const auto planned = edge_delay_entries_.find(predecessor.value);
    if (planned != edge_delay_entries_.end() &&
        planned->second.successor == successor) {
        const auto block = std::find_if(
            function.blocks.begin(), function.blocks.end(),
            [&](const machine::Block& candidate) {
                return candidate.id == successor;
            });
        if (block != function.blocks.end() &&
            planned->second.instruction < block->instructions.size()) {
            instruction("b", planned->second.resume_label);
            const auto& candidate =
                block->instructions[planned->second.instruction];
            if (planned->second.fused_compare) {
                if (!emit_fused_order_compare(function, candidate)) {
                    instruction("nop");
                }
            } else {
                emit_target(function, candidate);
            }
            return;
        }
    }
    instruction("b", block_label(function, successor));
    instruction("nop");
}

void AssemblyEmitter::emit_terminator(
    const machine::Function& function,
    const machine::Instruction& value,
    machine::BlockId predecessor,
    const machine::Instruction* delay,
    std::string_view resume_after_compare) {
    const auto emit_delay = [&] {
        if (delay) emit_target(function, *delay);
        else instruction("nop");
    };
    if (value.kind == machine::InstructionKind::Return) {
        const bool fallthrough_epilogue = frame_size_ != 0 && frame_active_ &&
            !function.layout.empty() &&
            function.layout.back() == predecessor;
        place_return(function, value, fallthrough_epilogue);
        return;
    }
    if (value.kind == machine::InstructionKind::Branch) {
        const auto successor =
            std::get<machine::BlockOperand>(value.operands.front()).target;
        emit_phi_edge_copies(function, predecessor, successor);
        if (layout_successor(function, predecessor) != successor) {
            if (delay) {
                instruction("b", block_label(function, successor));
                emit_target(function, *delay);
            } else {
                emit_successor_delay_branch(
                    function, predecessor, successor);
            }
        } else if (delay) {
            emit_target(function, *delay);
        }
        return;
    }
    if (value.kind == machine::InstructionKind::ConditionalBranch) {
        const auto condition = value.uses.front();
        const auto yes =
            std::get<machine::BlockOperand>(value.operands[1]).target;
        const auto no =
            std::get<machine::BlockOperand>(value.operands[2]).target;
        const bool yes_copies = edge_has_phi_copies(
            function, predecessor, yes);
        const bool no_copies = edge_has_phi_copies(
            function, predecessor, no);
        const auto next = layout_successor(function, predecessor);
        const SuccessorDelayEntry* taken_delay{};
        const machine::Instruction* taken_candidate{};
        const auto planned = edge_delay_entries_.find(predecessor.value);
        if (planned != edge_delay_entries_.end() &&
            (next == yes || next == no) &&
            planned->second.successor == (next == yes ? no : yes)) {
            const auto successor = std::find_if(
                function.blocks.begin(), function.blocks.end(),
                [&](const machine::Block& block) {
                    return block.id == planned->second.successor;
                });
            if (successor != function.blocks.end() &&
                planned->second.instruction <
                    successor->instructions.size()) {
                taken_delay = &planned->second;
                taken_candidate = &successor->instructions[
                    planned->second.instruction];
            }
        }
        if (!value.condition_predicate.empty()) {
            const auto predicate = decode_opcode(value.condition_predicate);
            const auto emit_fused_branch = [&, this](
                                               machine::BlockId target,
                                               bool branch_on_truth,
                                               const AssignedPhiCopy* copy =
                                                   nullptr,
                                               std::string_view target_label =
                                                   {},
                                               bool use_taken_delay = false) {
                const auto destination = target_label.empty()
                    ? block_label(function, target)
                    : std::string{target_label};
                const auto selected_delay = [&] {
                    if (copy) {
                        instruction("move", reg_name(copy->target) + "," +
                                                reg_name(copy->source));
                    } else if (use_taken_delay && taken_candidate) {
                        emit_target(function, *taken_candidate);
                    } else {
                        emit_delay();
                    }
                };
                if (predicate == Opcode::Iszero ||
                    (predicate >= Opcode::CmpEq &&
                     predicate <= Opcode::CmpUge)) {
                    if (value.uses.empty()) return false;
                    if (predicate == Opcode::Iszero ||
                        predicate == Opcode::CmpEq ||
                        predicate == Opcode::CmpNe) {
                        const auto left = input_gpr(
                            function, value.uses[0], "t0",
                            value.location);
                        const auto right = predicate == Opcode::Iszero
                            ? std::string_view{"zero"}
                            : value.uses.size() >= 2
                                  ? input_gpr(function, value.uses[1], "t1",
                                              value.location)
                                  : std::string_view{};
                        if (right.empty()) return false;
                        const bool truth_on_equal =
                            predicate != Opcode::CmpNe;
                        auto branch = branch_on_truth == truth_on_equal
                            ? std::string{"beq"} : std::string{"bne"};
                        if (copy || use_taken_delay) branch += 'l';
                        instruction(branch,
                                    reg_name(left) + "," + reg_name(right) +
                                        "," + destination);
                        selected_delay();
                        return true;
                    }
                    if (value.uses.size() < 2) return false;
                    const bool truth_on_nonzero =
                        predicate == Opcode::CmpSlt ||
                        predicate == Opcode::CmpSgt ||
                        predicate == Opcode::CmpUlt ||
                        predicate == Opcode::CmpUgt;
                    if (!emit_fused_order_compare(function, value)) {
                        return false;
                    }
                    if (!resume_after_compare.empty()) {
                        output_ << resume_after_compare << ":\n";
                    }
                    auto branch = branch_on_truth == truth_on_nonzero
                        ? std::string{"bne"} : std::string{"beq"};
                    if (copy || use_taken_delay) branch += 'l';
                    instruction(branch,
                                "$at,$zero," +
                                    destination);
                    selected_delay();
                    return true;
                }

                if (predicate == Opcode::Fiszero ||
                    (predicate >= Opcode::FcmpEq &&
                     predicate <= Opcode::FcmpGe)) {
                    if (value.uses.empty()) return false;
                    auto left = input_fpr(function, value.uses[0], "f0",
                                          value.location);
                    std::string_view right;
                    bool truth_on_condition = true;
                    std::string comparison;
                    const auto suffix = value.uses[0].mode.bits == 32
                        ? std::string{".s"} : std::string{".d"};
                    if (predicate == Opcode::Fiszero) {
                        right = "f2";
                        if (value.uses[0].mode.bits <= 32 ||
                            subtarget_.has_feature(Feature::Mips3)) {
                            move_gpr_to_fpr(
                                "zero", "f2", value.uses[0].mode.bits);
                        } else {
                            instruction("mtc1", "$zero,$f2");
                            instruction("mtc1", "$zero,$f3");
                        }
                        comparison = "c.eq" + suffix;
                    } else {
                        if (value.uses.size() < 2) return false;
                        right = input_fpr(function, value.uses[1], "f2",
                                          value.location);
                        switch (predicate) {
                        case Opcode::FcmpEq:
                            comparison = "c.eq" + suffix;
                            break;
                        case Opcode::FcmpNe:
                            comparison = "c.eq" + suffix;
                            truth_on_condition = false;
                            break;
                        case Opcode::FcmpLt:
                            comparison = "c.lt" + suffix;
                            break;
                        case Opcode::FcmpLe:
                            comparison = "c.le" + suffix;
                            break;
                        case Opcode::FcmpGt:
                            comparison = "c.lt" + suffix;
                            std::swap(left, right);
                            break;
                        case Opcode::FcmpGe:
                            comparison = "c.lt" + suffix;
                            truth_on_condition = false;
                            break;
                        default: return false;
                        }
                    }
                    instruction(comparison,
                                reg_name(left) + "," + reg_name(right));
                    auto branch = branch_on_truth == truth_on_condition
                        ? std::string{"bc1t"} : std::string{"bc1f"};
                    if (copy || use_taken_delay) branch += 'l';
                    instruction(branch, destination);
                    selected_delay();
                    return true;
                }
                return false;
            };

            if (!yes_copies && !no_copies && taken_delay &&
                taken_candidate) {
                if (emit_fused_branch(
                        taken_delay->successor,
                        taken_delay->successor == yes, nullptr,
                        taken_delay->resume_label, true)) {
                    return;
                }
            }

            if (!yes_copies && !no_copies) {
                if (next == yes) {
                    if (!emit_fused_branch(no, false)) {
                        diagnostics_.error(value.location,
                                           "malformed fused MIPS comparison branch");
                    }
                    return;
                }
                if (next == no) {
                    if (!emit_fused_branch(yes, true)) {
                        diagnostics_.error(value.location,
                                           "malformed fused MIPS comparison branch");
                    }
                    return;
                }
                if (!emit_fused_branch(yes, true)) {
                    diagnostics_.error(value.location,
                                       "malformed fused MIPS comparison branch");
                    return;
                }
                emit_successor_delay_branch(function, predecessor, no);
                return;
            }
            if (options_.schedule_insns2 &&
                subtarget_.has_feature(Feature::BranchLikely)) {
                if (next == yes && !yes_copies) {
                    if (const auto copy = single_assigned_gpr_phi_copy(
                            function, predecessor, no)) {
                        if (emit_fused_branch(no, false, &*copy)) return;
                    }
                } else if (next == no && !no_copies) {
                    if (const auto copy = single_assigned_gpr_phi_copy(
                            function, predecessor, yes)) {
                        if (emit_fused_branch(yes, true, &*copy)) return;
                    }
                }
            }
            if (next == no && !yes_copies) {
                if (!emit_fused_branch(yes, true)) {
                    diagnostics_.error(
                        value.location,
                        "malformed fused MIPS comparison branch");
                    return;
                }
                emit_phi_edge_copies(function, predecessor, no);
                return;
            }
            if (next == yes && !no_copies) {
                if (!emit_fused_branch(no, false)) {
                    diagnostics_.error(
                        value.location,
                        "malformed fused MIPS comparison branch");
                    return;
                }
                emit_phi_edge_copies(function, predecessor, yes);
                return;
            }
            if (!yes_copies) {
                if (!emit_fused_branch(yes, true)) {
                    diagnostics_.error(
                        value.location,
                        "malformed fused MIPS comparison branch");
                    return;
                }
                emit_phi_edge_copies(function, predecessor, no);
                instruction("b", block_label(function, no));
                instruction("nop");
                return;
            }
            if (!no_copies) {
                if (!emit_fused_branch(no, false)) {
                    diagnostics_.error(
                        value.location,
                        "malformed fused MIPS comparison branch");
                    return;
                }
                emit_phi_edge_copies(function, predecessor, yes);
                instruction("b", block_label(function, yes));
                instruction("nop");
                return;
            }
            const auto yes_edge = local_label(function);
            if (!emit_fused_branch(yes, true, nullptr, yes_edge)) {
                diagnostics_.error(
                    value.location,
                    "malformed fused MIPS comparison branch");
                return;
            }
            emit_phi_edge_copies(function, predecessor, no);
            instruction("b", block_label(function, no));
            instruction("nop");
            output_ << yes_edge << ":\n";
            emit_phi_edge_copies(function, predecessor, yes);
            instruction("b", block_label(function, yes));
            instruction("nop");
            return;
        }
        if (!yes_copies && !no_copies && taken_delay &&
            taken_candidate) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction(taken_delay->successor == yes ? "bnezl" : "beqzl",
                        reg_name(condition_gpr) + "," +
                            taken_delay->resume_label);
            emit_target(function, *taken_candidate);
            return;
        }
        if (!yes_copies && !no_copies) {
            if (next == yes) {
                instruction("beq", reg_name(input_gpr(
                    function, condition, "t0", value.location)) +
                    ",$zero," + block_label(function, no));
                emit_delay();
                return;
            }
            if (next == no) {
                instruction("bne", reg_name(input_gpr(
                    function, condition, "t0", value.location)) +
                    ",$zero," + block_label(function, yes));
                emit_delay();
                return;
            }
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction("bne", reg_name(condition_gpr) + ",$zero," +
                                   block_label(function, yes));
            emit_delay();
            emit_successor_delay_branch(function, predecessor, no);
            return;
        }
        // A branch-likely executes its delay slot only on the taken edge.
        // When the taken edge needs exactly one assigned-register PHI copy
        // and the no-copy edge is the layout successor, place that move in
        // the annulled slot.  This removes both edge stubs without executing
        // the copy on the fallthrough path.
        if (options_.schedule_insns2 &&
            subtarget_.has_feature(Feature::BranchLikely)) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            if (next == yes && !yes_copies) {
                if (const auto copy = single_assigned_gpr_phi_copy(
                        function, predecessor, no)) {
                    instruction("beqzl", reg_name(condition_gpr) + "," +
                                             block_label(function, no));
                    instruction("move", reg_name(copy->target) + "," +
                                            reg_name(copy->source));
                    return;
                }
            } else if (next == no && !no_copies) {
                if (const auto copy = single_assigned_gpr_phi_copy(
                        function, predecessor, yes)) {
                    instruction("bnezl", reg_name(condition_gpr) + "," +
                                             block_label(function, yes));
                    instruction("move", reg_name(copy->target) + "," +
                                            reg_name(copy->source));
                    return;
                }
            }
        }
        // When the laid-out edge owns the PHI copies and the taken edge does
        // not, branch directly to the copy-free destination and perform the
        // copies only after the ordinary delay slot. This is the complement
        // of the branch-likely form above and is especially useful after a
        // loop body has been rotated ahead of its test.
        if (next == no && !yes_copies) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction("bne", reg_name(condition_gpr) + ",$zero," +
                                   block_label(function, yes));
            emit_delay();
            emit_phi_edge_copies(function, predecessor, no);
            return;
        }
        if (next == yes && !no_copies) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction("beq", reg_name(condition_gpr) + ",$zero," +
                                   block_label(function, no));
            emit_delay();
            emit_phi_edge_copies(function, predecessor, yes);
            return;
        }
        if (!yes_copies) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction("bne", reg_name(condition_gpr) + ",$zero," +
                                   block_label(function, yes));
            emit_delay();
            emit_phi_edge_copies(function, predecessor, no);
            instruction("b", block_label(function, no));
            instruction("nop");
            return;
        }
        if (!no_copies) {
            const auto condition_gpr = input_gpr(
                function, condition, "t0", value.location);
            instruction("beq", reg_name(condition_gpr) + ",$zero," +
                                  block_label(function, no));
            emit_delay();
            emit_phi_edge_copies(function, predecessor, yes);
            instruction("b", block_label(function, yes));
            instruction("nop");
            return;
        }
        const auto yes_edge = local_label(function);
        const auto condition_gpr = input_gpr(
            function, condition, "t0", value.location);
        instruction("bne", reg_name(condition_gpr) + ",$zero," + yes_edge);
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
        const auto target = input_gpr(
            function, value.uses.front(), "t0", value.location);
        instruction("jr", reg_name(target));
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
    frame_active_ = true;
    active_signature_ = classify_entity(entity, function.location);
    if (!active_signature_) return;
    hilo_write_barrier_ = 0;
    fpu_multiply_pending_ = false;
    fpu_transfer_delay_register_.reset();
    // Fully allocated, non-overlapping parameter sets are captured directly.
    // Otherwise home the whole incoming register set before materialization:
    // EABI/Cross banks may overlap t0/t1 assembly scratches, so mixing direct
    // and fallback captures would let an early value destroy a later one.
    if (function.frame.program) {
        frame_size_ = function.frame.program->stack_size;
        saves_fp_ = false;
        saves_ra_ = false;
    } else {
        prepare_parameter_homes(function);
        if (!finalize_frame(function)) return;
    }
    framed_ = machine::framed_blocks(function);
    function_location_ = function.location;
    const bool shared_epilogue =
        frame_size_ != 0 && needs_shared_epilogue(function);
    frame_pointer_active_ = function.frame.has_frame_pointer;
    if (!safe_assembly_text(function.symbol) ||
        (entity.section && !safe_assembly_text(*entity.section))) {
        diagnostics_.error(function.location,
                           "MIPS symbol or section is not representable by the assembler");
        return;
    }
    const auto symbol = assembly_symbol(function.symbol);
    const bool patch_function = std::ranges::any_of(
        function.blocks, [](const machine::Block& block) {
            return std::ranges::any_of(
                block.instructions, [](const machine::Instruction& instruction) {
                    return instruction.patch.has_value();
                });
        });
    const auto split_function = options_.function_sections || entity.retain ||
                                entity.temperature !=
                                    hir::FunctionTemperature::Normal;
    const auto section = entity.section
        ? *entity.section
        : patch_function
              ? ".text.cross.patch." +
                    std::to_string(function.source.value)
        : split_function
              ? std::string(
                    entity.temperature == hir::FunctionTemperature::Hot
                        ? ".text.hot."
                        : entity.temperature == hir::FunctionTemperature::Cold
                              ? ".text.unlikely."
                              : ".text.") + function.symbol
              : ".text";
    std::string section_error;
    const auto directive = assembly_section_directive(
        format_, {section, AssemblySectionKind::Code,
                  entity.section.has_value(), entity.retain},
        section_error);
    if (!directive) {
        diagnostics_.error(function.location, section_error);
        return;
    }
    output_ << *directive << '\n';
    // ISA directives are scoped so llvm-mc's command-line ABI features (for
    // example +single-float) are restored before it finalizes .MIPS.abiflags.
    output_ << ".set push\n.set noreorder\n.set noat\n.option pic0\n";
    if (subtarget_.has_feature(Feature::Mips64)) output_ << ".set mips64\n";
    else if (subtarget_.has_feature(Feature::Mips5)) output_ << ".set mips5\n";
    else if (subtarget_.has_feature(Feature::Mips4)) output_ << ".set mips4\n";
    else if (subtarget_.has_feature(Feature::Mips3)) output_ << ".set mips3\n";
    else if (subtarget_.has_feature(Feature::Mips2)) output_ << ".set mips2\n";
    else output_ << ".set mips1\n";
    unsigned alignment_power = 2;
    if (options_.function_alignment != 0) {
        alignment_power = static_cast<unsigned>(
            std::countr_zero(options_.function_alignment));
    }
    alignment_power = std::max(
        alignment_power,
        static_cast<unsigned>(std::countr_zero(entity.minimum_alignment)));
    output_ << ".p2align " << alignment_power << '\n';
    std::string symbol_error;
    const auto symbol_directives = assembly_symbol_directives(
        format_, {symbol, entity.linkage == Linkage::Global, true,
                  entity.weak, assembly_visibility(entity.visibility)},
        symbol_error);
    if (!symbol_directives) {
        diagnostics_.error(function.location, symbol_error);
        return;
    }
    if (!symbol_directives->empty()) {
        output_ << *symbol_directives << '\n';
    }
    output_ << ".type " << symbol << ",@function\n"
            << ".ent " << symbol << '\n' << symbol << ":\n"
            << "\t.frame\t"
            << respell_registers(function.frame.has_frame_pointer ? "$fp"
                                                                  : "$sp")
            << ',' << frame_size_ << ','
            << respell_registers("$ra") << '\n';
    const bool cfi = (options_.unwind_tables ||
                      options_.asynchronous_unwind_tables) &&
                     assembly_uses_dwarf_cfi(format_);
    if (cfi) output_ << ".cfi_startproc\n";
    if (function.frame.program) {
        if (!function.frame.prologue_block) {
            for (const auto& instruction : function.frame.program->prologue) {
                emit_frame_instruction(instruction);
            }
        }
    } else if (frame_size_ != 0) {
        instruction(address_add_immediate(),
                    "$sp,$sp,-" + std::to_string(frame_size_));
        if (cfi) output_ << ".cfi_def_cfa_offset " << frame_size_ << '\n';
        if (saves_ra_) {
            instruction(address_store(),
                        "$ra," + memory(saved_ra_offset_, "sp"));
        }
        if (saves_fp_) {
            instruction(address_store(),
                        "$fp," + memory(saved_fp_offset_, "sp"));
        }
        if (cfi) {
            if (saves_ra_) {
                output_ << ".cfi_offset 31,"
                        << static_cast<std::int64_t>(saved_ra_offset_) -
                               frame_size_
                        << '\n';
            }
            if (saves_fp_) {
                output_ << ".cfi_offset 30,"
                        << static_cast<std::int64_t>(saved_fp_offset_) -
                               frame_size_
                        << '\n';
            }
        }
        if (function.frame.has_frame_pointer) {
            instruction("move", "$fp,$sp");
            if (cfi) output_ << ".cfi_def_cfa_register 30\n";
        }
        emit_callee_saves(function, cfi);
    }
    // Entry homes run before a shrink-wrapped prologue.
    frame_active_ = !function.frame.prologue_block;
    emit_parameter_homes(function);
    epilogue_label_ = ".Lcross.mips." +
                      std::to_string(function.source.value) + ".return";
    plan_successor_delay_slots(function);

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
                const auto& label_entity =
                    hir_.labels.at(label.label.value);
                if (!label_entity.is_global) continue;
                const auto label_symbol =
                    assembly_symbol(label_entity.link_symbol);
                output_ << ".globl " << label_symbol << '\n'
                        << ".type " << label_symbol << ",@function\n"
                        << label_symbol << ":\n";
            }
        }
        frame_active_ = framed_[found->id.value];
        if (function.frame.prologue_block == found->id) {
            for (const auto& instruction : function.frame.program->prologue) {
                emit_frame_instruction(instruction);
            }
        }
        const auto delay_plan = delay_slot_plan(function, *found);
        for (std::size_t index = 0;
             index < found->instructions.size(); ++index) {
            if (delay_plan && index == delay_plan->first) continue;
            const auto& value = found->instructions[index];
            if (value.kind == machine::InstructionKind::Target) {
                const auto successor_delay =
                    successor_delay_entries_.find(found->id.value);
                if (successor_delay != successor_delay_entries_.end() &&
                    successor_delay->second.instruction == index &&
                    !successor_delay->second.fused_compare &&
                    successor_delay->second.elide_original) {
                    output_ << successor_delay->second.resume_label << ":\n";
                    continue;
                }
                if (options_.compare_select_fusion &&
                    decode_opcode(value.opcode) == Opcode::Select &&
                    value.uses.size() >= 3 && !value.defs.empty() &&
                    !(value.defs.front().mode.bits <= 32 &&
                      subtarget_.has_feature(Feature::CondMove))) {
                    std::size_t end = index + 1;
                    while (end < found->instructions.size()) {
                        const auto& candidate = found->instructions[end];
                        if (candidate.kind !=
                                machine::InstructionKind::Target ||
                            decode_opcode(candidate.opcode) != Opcode::Select ||
                            candidate.uses.size() < 3 ||
                            candidate.defs.empty() ||
                            candidate.uses.front() != value.uses.front() ||
                            (candidate.defs.front().mode.bits <= 32 &&
                             subtarget_.has_feature(Feature::CondMove))) {
                            break;
                        }
                        ++end;
                    }
                    if (end - index >= 2) {
                        emit_select_group(
                            function,
                            std::span<const machine::Instruction>{
                                found->instructions.data() + index,
                                end - index});
                        index = end - 1;
                        continue;
                    }
                }
                emit_target(function, value);
                if (successor_delay != successor_delay_entries_.end() &&
                    successor_delay->second.instruction == index &&
                    !successor_delay->second.fused_compare) {
                    output_ << successor_delay->second.resume_label << ":\n";
                }
            } else if (value.kind == machine::InstructionKind::Call) {
                if (index + 1 < found->instructions.size() &&
                    can_emit_tail_call(function, value,
                                       found->instructions[index + 1])) {
                    emit_call(function, value, true, cfi);
                    ++index;
                } else if (value.must_tail) {
                    diagnostics_.error(
                        value.location,
                        "MIPS cannot satisfy musttail for this ABI, argument layout, or frame state");
                    return;
                } else {
                    emit_call(function, value);
                }
            } else if (value.kind == machine::InstructionKind::Copy) {
                emit_copy(function, value);
            } else {
                const machine::Instruction* delay = nullptr;
                if (delay_plan && index == delay_plan->second) {
                    delay = &found->instructions[delay_plan->first];
                }
                std::string_view resume_after_compare;
                const auto successor_delay =
                    successor_delay_entries_.find(found->id.value);
                if (successor_delay != successor_delay_entries_.end() &&
                    successor_delay->second.instruction == index &&
                    successor_delay->second.fused_compare) {
                    resume_after_compare =
                        successor_delay->second.resume_label;
                }
                emit_terminator(function, value, found->id, delay,
                                resume_after_compare);
            }
        }
    }

    frame_active_ = true;
    if (shared_epilogue) {
        output_ << epilogue_label_ << ":\n";
        if (function.frame.program) {
            const auto* delay = emit_prepared_epilogue(function);
            instruction("jr", "$ra");
            if (delay) emit_frame_instruction(*delay);
            else instruction("nop");
        } else {
            emit_callee_restores(function, cfi);
            if (function.frame.has_frame_pointer) {
                instruction("move", "$sp,$fp");
            }
            if (saves_fp_) {
                instruction(address_load(),
                            "$fp," + memory(saved_fp_offset_, "sp"));
            }
            if (saves_ra_) {
                instruction(address_load(),
                            "$ra," + memory(saved_ra_offset_, "sp"));
            }
            instruction("jr", "$ra");
            instruction(address_add_immediate(),
                        "$sp,$sp," + std::to_string(frame_size_));
        }
    }
    if (cfi) output_ << ".cfi_endproc\n";
    output_ << ".end " << symbol << "\n.size " << symbol << ",.-"
            << symbol << "\n.set reorder\n.set pop\n";
    active_signature_.reset();
}

} // namespace

machine::Module lower_managed_machine(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics) {
    auto result = MachineLowerer(managed, hir_module, subtarget, options, diagnostics).run();
    if (diagnostics.errors() == 0) {
        AssemblyEmitter(result, hir_module, subtarget, options, diagnostics).prepare_frames();
    }
    return result;
}

std::string emit_managed_machine_assembly(
    machine::Module& module, const mir::ManagedModule& managed,
    const hir::Module& hir_module, const Subtarget& subtarget,
    const CompilerOptions& options, Diagnostics& diagnostics) {
    (void)managed;
    return AssemblyEmitter(module, hir_module, subtarget, options,
                           diagnostics).run();
}

std::optional<ElfAbiTag> elf_abi_tag(const AbiEntry& abi) {
    if (abi.elf_abi_tag.empty()) return ElfAbiTag::Default;
    if (abi.elf_abi_tag == "eabi32") return ElfAbiTag::Eabi32;
    return std::nullopt;
}

} // namespace cross::mips
