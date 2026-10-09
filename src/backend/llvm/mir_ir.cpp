// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/llvm/mir_ir.hpp"

#include "common/uint128.hpp"

#include "target/abi_lowering.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cross {
namespace {

std::string llvm_string(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (const char byte : text) {
        const auto ch = static_cast<unsigned char>(byte);
        if (ch >= 0x20 && ch <= 0x7e && ch != '"' && ch != '\\') {
            out << static_cast<char>(ch);
        } else {
            constexpr char digits[] = "0123456789ABCDEF";
            out << '\\' << digits[ch >> 4] << digits[ch & 15];
        }
    }
    out << '"';
    return out.str();
}

std::string symbol_name(std::string_view text) { return "@" + llvm_string(text); }

std::string llvm_visibility(hir::SymbolVisibility visibility) {
    switch (visibility) {
    case hir::SymbolVisibility::Default: return {};
    case hir::SymbolVisibility::Hidden: return "hidden ";
    case hir::SymbolVisibility::Protected: return "protected ";
    case hir::SymbolVisibility::Internal:
        // LLVM has no STV_INTERNAL spelling; hidden is its closest semantic
        // representation while native ELF preserves the exact visibility.
        return "hidden ";
    }
    return {};
}

std::string hexadecimal(std::uint64_t value, unsigned width) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0')
        << std::setw(static_cast<int>(width)) << value;
    return out.str();
}

std::string floating_flags(const CompilerOptions& options) {
    std::string result;
    if (options.fast_math) result += "reassoc arcp afn ";
    if (options.fp_contract == FpContractMode::Fast) result += "contract ";
    if (options.finite_math_only) result += "nnan ninf ";
    if (!options.signed_zeros) result += "nsz ";
    return result;
}

std::string target_features(const CompilerOptions& options) {
    std::string result;
    for (const auto& feature : options.target_features) {
        if (!result.empty()) result += ',';
        result += feature;
    }
    return result;
}

std::vector<std::string> enabled_abi_features(
    const CompilerOptions& options) {
    std::vector<std::string> result;
    for (const auto& feature : options.target_features) {
        if (feature.size() > 1 && feature.front() == '+') {
            result.push_back(feature.substr(1));
        }
    }
    return result;
}

std::string ir_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return "ptr";
    if (type.kind == hir::Type::Kind::Vector && type.element) {
        return type.scalable
                   ? "<vscale x " + std::to_string(type.lanes) + " x " +
                         ir_type(module, *type.element) + ">"
                   : "<" + std::to_string(type.lanes) + " x " +
                         ir_type(module, *type.element) + ">";
    }
    if (type.kind == hir::Type::Kind::Array && type.element) {
        return "[" + std::to_string(type.lanes) + " x " +
               ir_type(module, *type.element) + "]";
    }
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return "[" + std::to_string(module.record(*type.record).size) +
               " x i8]";
    }
    switch (type.builtin) {
    case BuiltinType::Void: return "void";
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return "i8";
    case BuiltinType::I16: case BuiltinType::U16: return "i16";
    case BuiltinType::I32: case BuiltinType::U32: return "i32";
    case BuiltinType::I64: case BuiltinType::U64: return "i64";
    case BuiltinType::Iptr: case BuiltinType::Uptr:
        return "i" + std::to_string(module.address_bits);
    case BuiltinType::I128: case BuiltinType::U128: return "i128";
    case BuiltinType::F32: return "float";
    case BuiltinType::F64: return "double";
    case BuiltinType::Fptr:
        return module.address_bits <= 32 ? "float" : "double";
    case BuiltinType::F80: return "x86_fp80";
    case BuiltinType::F128: return "fp128";
    case BuiltinType::Label: return "ptr";
    }
    return "void";
}

// An object, array element, or pointee. A typedef's alignment request pads
// the storage beyond its value, which keeps the base type's spelling.
std::string storage_ir_type(const hir::Module& module,
                            const TargetInfo* target, hir::TypeId id) {
    const auto& type = module.type(id);
    auto spelling = type.kind == hir::Type::Kind::Array && type.element
        ? "[" + std::to_string(type.lanes) + " x " +
              storage_ir_type(module, target, *type.element) + "]"
        : ir_type(module, id);
    if (type.alignment == 0 || !target) return spelling;
    const auto layout = hir::layout_size(module, id, *target);
    const auto natural = hir::natural_size(module, id, *target);
    if (!layout || !natural || *layout <= *natural) return spelling;
    return "<{ " + spelling + ", [" + std::to_string(*layout - *natural) +
           " x i8] }>";
}

bool comparison(mir::BinaryOperation operation) {
    return operation >= mir::BinaryOperation::Equal;
}

std::string binary_name(mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::Add: return "add";
    case BinaryOperation::Subtract: return "sub";
    case BinaryOperation::Multiply: return "mul";
    case BinaryOperation::SignedDivide: return "sdiv";
    case BinaryOperation::UnsignedDivide: return "udiv";
    case BinaryOperation::SignedRemainder: return "srem";
    case BinaryOperation::UnsignedRemainder: return "urem";
    case BinaryOperation::BitAnd: return "and";
    case BinaryOperation::BitOr: return "or";
    case BinaryOperation::BitXor: return "xor";
    case BinaryOperation::ShiftLeft: return "shl";
    case BinaryOperation::ShiftRightArithmetic: return "ashr";
    case BinaryOperation::ShiftRightLogical: return "lshr";
    case BinaryOperation::RotateLeft: return "fshl";
    case BinaryOperation::RotateRight: return "fshr";
    // The value emitter expands these through a double-width multiply.
    case BinaryOperation::UnsignedMultiplyHigh:
    case BinaryOperation::SignedMultiplyHigh: return "mul";
    case BinaryOperation::Equal: return "eq";
    case BinaryOperation::NotEqual: return "ne";
    case BinaryOperation::SignedLess: return "slt";
    case BinaryOperation::SignedLessEqual: return "sle";
    case BinaryOperation::SignedGreater: return "sgt";
    case BinaryOperation::SignedGreaterEqual: return "sge";
    case BinaryOperation::UnsignedLess: return "ult";
    case BinaryOperation::UnsignedLessEqual: return "ule";
    case BinaryOperation::UnsignedGreater: return "ugt";
    case BinaryOperation::UnsignedGreaterEqual: return "uge";
    }
    return "add";
}

std::string block_name(mir::BlockId id) {
    return id.value == 0 ? "entry" : "mir.bb" + std::to_string(id.value);
}

unsigned ir_alignment(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) {
        return std::max(1U, (module.address_bits + 7U) / 8U);
    }
    if (type.kind == hir::Type::Kind::Vector && type.element) {
        const auto component = ir_alignment(module, *type.element);
        const auto bytes = component * std::max(1U, type.lanes);
        unsigned alignment = 1;
        while (alignment < bytes && alignment < 64) alignment *= 2;
        return alignment;
    }
    if (type.kind == hir::Type::Kind::Array && type.element) {
        return ir_alignment(module, *type.element);
    }
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return module.record(*type.record).alignment;
    }
    switch (type.builtin) {
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 1;
    case BuiltinType::I16: case BuiltinType::U16: return 2;
    case BuiltinType::I32: case BuiltinType::U32: case BuiltinType::F32: return 4;
    case BuiltinType::I128: case BuiltinType::U128:
    case BuiltinType::F80: case BuiltinType::F128: return 16;
    case BuiltinType::Iptr: case BuiltinType::Uptr:
    case BuiltinType::Fptr:
        return std::max(1U, (module.address_bits + 7U) / 8U);
    case BuiltinType::Void: return 1;
    default: return 8;
    }
}

bool is_void(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Void;
}

bool is_floating(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Vector && type.element) {
        return is_floating(module, *type.element);
    }
    return type.kind == hir::Type::Kind::Builtin &&
           (type.builtin == BuiltinType::F32 ||
            type.builtin == BuiltinType::F64 ||
            type.builtin == BuiltinType::F80 ||
            type.builtin == BuiltinType::F128 ||
            type.builtin == BuiltinType::Fptr);
}

bool is_vector(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Vector;
}

unsigned abi_type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return module.address_bits;
    if ((type.kind == hir::Type::Kind::Vector ||
         type.kind == hir::Type::Kind::Array) && type.element) {
        return abi_type_bits(module, *type.element) * type.lanes;
    }
    switch (type.builtin) {
    case BuiltinType::Void: return 0;
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
    }
    return 0;
}

AbiValue llvm_abi_value(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) {
        return AbiValue{ScalarMode::pointer(
            static_cast<std::uint16_t>(module.address_bits))};
    }
    if (type.kind == hir::Type::Kind::Vector) {
        return AbiValue{ScalarMode::vector(
            static_cast<std::uint16_t>(abi_type_bits(module, id)))};
    }
    if (is_floating(module, id)) {
        return AbiValue{ScalarMode::floating(
            static_cast<std::uint16_t>(abi_type_bits(module, id)))};
    }
    return AbiValue{ScalarMode::integer(
        static_cast<std::uint16_t>(abi_type_bits(module, id)))};
}

std::string memory_order(mir::MemoryOrder order) {
    switch (order) {
    case mir::MemoryOrder::Relaxed: return "monotonic";
    case mir::MemoryOrder::Acquire: return "acquire";
    case mir::MemoryOrder::Release: return "release";
    case mir::MemoryOrder::AcqRel: return "acq_rel";
    case mir::MemoryOrder::SeqCst: return "seq_cst";
    }
    return "seq_cst";
}

std::string integer_ir_type(unsigned bits) {
    return "i" + std::to_string(bits);
}

std::string vector_constant(const hir::Module& module, hir::TypeId id,
                            std::string_view value) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Vector || !type.element ||
        type.scalable) {
        return "zeroinitializer";
    }
    std::string result = "<";
    for (std::uint32_t lane = 0; lane < type.lanes; ++lane) {
        if (lane != 0) result += ", ";
        result += ir_type(module, *type.element) + " " + std::string(value);
    }
    result += ">";
    return result;
}

bool parameter_cell(const hir::Parameter& parameter) {
    return parameter.mode != ParameterMode::In ||
           (parameter.physical_location &&
            *parameter.physical_location != "auto");
}

unsigned patch_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return 0;
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8: return 8;
    case BuiltinType::I16:
    case BuiltinType::U16: return 16;
    case BuiltinType::I32:
    case BuiltinType::U32: return 32;
    case BuiltinType::I64:
    case BuiltinType::U64: return 64;
    case BuiltinType::Iptr:
    case BuiltinType::Uptr: return module.address_bits;
    default: return 0;
    }
}

class Emitter {
public:
    Emitter(const hir::Module& hir_module, const mir::ManagedFunction& function,
            const CompilerOptions& options, Diagnostics& diagnostics)
        : hir_(hir_module), function_(function), options_(options),
          diagnostics_(diagnostics), entity_(hir_module.function(function.source)),
          target_(target_for_triple(options.target)),
          references_(function.values.size()) {
        for (const auto& value : function.values) {
            if (value.kind == mir::ValueKind::Parameter) {
                const auto& parameter =
                    entity_.parameters[value.parameter_index];
                references_[value.id.value] =
                    parameter_cell(parameter) && !transport_pointer(parameter)
                        ? "%mir.param." + std::to_string(value.parameter_index)
                        : "%arg" + std::to_string(value.parameter_index);
            } else if (value.kind == mir::ValueKind::ConstantInteger) {
                references_[value.id.value] =
                    to_decimal({value.integer, value.integer_high});
                const auto& type = hir_.type(value.type);
                if (type.kind == hir::Type::Kind::Pointer ||
                    (type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Label)) {
                    references_[value.id.value] = value.integer == 0 && value.integer_high == 0
                        ? "null" : "inttoptr (i" + std::to_string(hir_.address_bits) + " " +
                            references_[value.id.value] + " to ptr)";
                }
            } else if (value.kind == mir::ValueKind::ConstantFloating &&
                       hir_.type(value.type).kind == hir::Type::Kind::Builtin &&
                       hir_.type(value.type).builtin == BuiltinType::F80) {
                references_[value.id.value] =
                    "0xK" + hexadecimal(value.integer_high & 0xffffU, 4) +
                    hexadecimal(value.integer, 16);
            } else if (value.kind == mir::ValueKind::ConstantFloating &&
                       hir_.type(value.type).kind == hir::Type::Kind::Builtin &&
                       hir_.type(value.type).builtin == BuiltinType::F128) {
                // LLVM's fp128 literal spelling writes the low 64-bit word
                // before the high word.
                references_[value.id.value] =
                    "0xL" + hexadecimal(value.integer, 16) +
                    hexadecimal(value.integer_high, 16);
            } else if (value.kind == mir::ValueKind::LabelAddress) {
                const auto& label = hir_.labels.at(value.label->value);
                const auto& owner = hir_.function(label.owner);
                if (!label.definition || owner.naked) {
                    diagnostics_.error(value.location,
                        "LLVM debug serialization cannot represent an external or raw-assembly label address");
                    references_[value.id.value] = "null";
                } else {
                    references_[value.id.value] = "blockaddress(" +
                        symbol_name(owner.link_symbol) + ", %" + llvm_label_name(label.id) + ')';
                }
            } else if (value.kind == mir::ValueKind::SlotAddress &&
                       value.slot) {
                references_[value.id.value] =
                    "%mir.slot" + std::to_string(value.slot->value);
            } else if (value.kind == mir::ValueKind::FunctionAddress &&
                       value.callee) {
                references_[value.id.value] =
                    symbol_name(hir_.function(*value.callee).link_symbol);
            } else if (value.kind == mir::ValueKind::GlobalAddress &&
                       value.object) {
                references_[value.id.value] =
                    symbol_name(hir_.object(*value.object).link_symbol);
            } else {
                references_[value.id.value] =
                    "%mir.v" + std::to_string(value.id.value);
            }
        }
    }

    std::string run() {
        const bool contains_patch = std::any_of(
            function_.values.begin(), function_.values.end(),
            [](const mir::ManagedValue& value) {
                return value.kind == mir::ValueKind::PatchValue;
            });
        const auto linkage = entity_.weak
            ? "weak "
            : entity_.linkage == Linkage::Global ? "" : "internal ";
        out_ << "define " << linkage << llvm_visibility(entity_.visibility)
             << abi_name()
             << ir_type(hir_, entity_.result_type) << ' '
             << symbol_name(entity_.link_symbol) << '(';
        for (std::size_t index = 0; index < entity_.parameters.size(); ++index) {
            if (index != 0) out_ << ", ";
            const auto& parameter = entity_.parameters[index];
            out_ << (!parameter_cell(parameter)
                         ? ir_type(hir_, parameter.type)
                         : std::string("ptr"))
                 << " %arg" << index;
        }
        if (entity_.variadic) {
            if (!entity_.parameters.empty()) out_ << ", ";
            out_ << "...";
        }
        out_ << ')';
        out_ << " \"target-cpu\"=" << llvm_string(options_.cpu);
        const auto features = target_features(options_);
        if (!features.empty()) {
            out_ << " \"target-features\"=" << llvm_string(features);
        }
        if (entity_.minimum_alignment > 1) {
            out_ << " align " << entity_.minimum_alignment;
        }
        if (entity_.temperature == hir::FunctionTemperature::Hot) {
            out_ << " hot";
        } else if (entity_.temperature == hir::FunctionTemperature::Cold) {
            out_ << " cold";
        }
        if (contains_patch) out_ << " noinline";
        if (entity_.section) {
            out_ << " section " << llvm_string(*entity_.section);
        } else if (contains_patch) {
            out_ << " section "
                 << llvm_string(".text.cross.patch." +
                                std::to_string(function_.source.value));
        }
        out_ << " {\n";
        for (const auto& block : function_.blocks) emit_block(block);
        out_ << "}\n\n";
        return out_.str();
    }

private:
    std::string block_reference(mir::BlockId id) const {
        const auto binding = std::find_if(
            function_.labels.begin(), function_.labels.end(),
            [&](const mir::ManagedLabel& candidate) {
                return candidate.block == id;
            });
        if (binding == function_.labels.end()) return block_name(id);
        return llvm_label_name(binding->label);
    }

    static bool splits_llvm_block(const mir::ManagedValue& value) {
        return value.kind == mir::ValueKind::Atomic &&
               (value.atomic == mir::AtomicOperation::CompareExchange ||
                value.atomic == mir::AtomicOperation::FetchUpdate);
    }

    static std::string atomic_continuation(mir::ValueId id) {
        return "mir.atomic.cont." + std::to_string(id.value);
    }

    std::string predecessor_reference(mir::BlockId id) const {
        const auto& block = function_.blocks.at(id.value);
        for (auto value = block.values.rbegin(); value != block.values.rend();
             ++value) {
            if (splits_llvm_block(function_.values[value->value])) {
                return atomic_continuation(*value);
            }
        }
        return block_reference(id);
    }

    std::string abi_name(const hir::Function& function) {
        return abi_name(function.abi, function.location);
    }

    std::string abi_name(AbiId id, SourceLocation location) {
        const auto* target = target_for_triple(options_.target);
        const auto* abi = target ? find_abi(*target, id) : nullptr;
        if (!target || !abi) {
            diagnostics_.error(location, "managed MIR has unavailable ABI id " +
                                             std::to_string(id.value));
            return {};
        }
        return abi->canonical_name == target->default_abi(options_.target)
                   ? std::string{}
                   : std::string(abi->llvm_calling_convention) + ' ';
    }

    std::string abi_name() { return abi_name(entity_); }

    const std::string& reference(mir::ValueId id) const {
        return references_.at(id.value);
    }

    void emit_phi(const mir::ManagedValue& value) {
        out_ << "  " << reference(value.id) << " = phi " << ir_type(hir_, value.type) << ' ';
        for (std::size_t index = 0; index < value.incoming.size(); ++index) {
            if (index != 0) out_ << ", ";
            out_ << "[ " << reference(value.incoming[index].value) << ", %"
                 << predecessor_reference(value.incoming[index].predecessor)
                 << " ]";
        }
        out_ << '\n';
    }

    hir::TypeId atomic_object_type(const mir::ManagedValue& value) const {
        const auto& address = function_.values.at(value.operands.front().value);
        const auto& pointer = hir_.type(address.type);
        return *pointer.pointee;
    }

    void emit_compare_exchange(const mir::ManagedValue& value,
                               hir::TypeId object_type) {
        const auto type = ir_type(hir_, object_type);
        const auto alignment = ir_alignment(hir_, object_type);
        const auto stem = "%mir.atomic." + std::to_string(value.id.value);
        const auto expected_value = stem + ".expected";
        const auto pair = stem + ".pair";
        const auto observed_bits = stem + ".observed.bits";
        const auto observed = stem + ".observed";
        const auto success = stem + ".success";
        const auto failed = "mir.atomic.fail." +
                            std::to_string(value.id.value);
        const auto continuation = atomic_continuation(value.id);
        const auto expected_pointer = value.operands[1];
        const auto desired = value.operands[2];
        const auto& expected_pointer_type = hir_.type(
            function_.values[expected_pointer.value].type);
        const bool expected_volatile =
            expected_pointer_type.pointee &&
            hir_.type(*expected_pointer_type.pointee).is_volatile;

        out_ << "  " << expected_value << " = load "
             << (expected_volatile ? "volatile " : "") << type
             << ", ptr " << reference(expected_pointer) << ", align "
             << alignment << '\n';

        std::string compare_type = type;
        std::string compare_expected = expected_value;
        std::string compare_desired = reference(desired);
        if (is_floating(hir_, object_type)) {
            const auto bits = integer_ir_type(
                static_cast<unsigned>(alignment * 8U));
            const auto expected_bits = stem + ".expected.bits";
            const auto desired_bits = stem + ".desired.bits";
            out_ << "  " << expected_bits << " = bitcast " << type << ' '
                 << expected_value << " to " << bits << '\n';
            out_ << "  " << desired_bits << " = bitcast " << type << ' '
                 << reference(desired) << " to " << bits << '\n';
            compare_type = bits;
            compare_expected = expected_bits;
            compare_desired = desired_bits;
        }
        const auto pair_type = "{ " + compare_type + ", i1 }";
        out_ << "  " << pair << " = cmpxchg "
             << (value.is_volatile_access ? "volatile " : "") << "ptr "
             << reference(value.operands[0]) << ", " << compare_type << ' '
             << compare_expected << ", " << compare_type << ' '
             << compare_desired << ' ' << memory_order(value.memory_order)
             << ' ' << memory_order(value.failure_order) << ", align "
             << alignment << '\n';
        out_ << "  " << observed_bits << " = extractvalue " << pair_type
             << ' ' << pair << ", 0\n";
        if (is_floating(hir_, object_type)) {
            out_ << "  " << observed << " = bitcast " << compare_type << ' '
                 << observed_bits << " to " << type << '\n';
        }
        out_ << "  " << success << " = extractvalue " << pair_type << ' '
             << pair << ", 1\n";
        out_ << "  br i1 " << success << ", label %" << continuation
             << ", label %" << failed << '\n';
        out_ << failed << ":\n";
        out_ << "  store " << (expected_volatile ? "volatile " : "")
             << type << ' '
             << (is_floating(hir_, object_type) ? observed : observed_bits)
             << ", ptr " << reference(expected_pointer) << ", align "
             << alignment << '\n';
        out_ << "  br label %" << continuation << '\n';
        out_ << continuation << ":\n";
        out_ << "  " << reference(value.id) << " = zext i1 " << success
             << " to i8\n";
        active_label_ = continuation;
    }

    void emit_atomic_update(const mir::ManagedValue& value,
                            hir::TypeId object_type) {
        const auto type = ir_type(hir_, object_type);
        const auto alignment = ir_alignment(hir_, object_type);
        const auto stem = "%mir.atomic." + std::to_string(value.id.value);
        const auto initial = stem + ".initial";
        const auto desired = stem + ".desired";
        const auto pair = stem + ".pair";
        const auto observed_bits = stem + ".observed.bits";
        const auto observed = stem + ".observed";
        const auto success = stem + ".success";
        const auto loop = "mir.atomic.loop." + std::to_string(value.id.value);
        const auto retry = "mir.atomic.retry." + std::to_string(value.id.value);
        const auto continuation = atomic_continuation(value.id);

        out_ << "  " << initial << " = load atomic "
             << (value.is_volatile_access ? "volatile " : "") << type
             << ", ptr " << reference(value.operands[0])
             << " monotonic, align " << alignment << '\n';
        out_ << "  br label %" << loop << '\n';
        out_ << loop << ":\n";
        out_ << "  " << reference(value.id) << " = phi " << type
             << " [ " << initial << ", %" << active_label_ << " ], [ "
             << observed << ", %" << retry << " ]\n";

        const bool floating = is_floating(hir_, object_type);
        const auto operation = floating
            ? (value.binary == mir::BinaryOperation::Add ? "fadd" :
               value.binary == mir::BinaryOperation::Subtract ? "fsub" :
               value.binary == mir::BinaryOperation::Multiply ? "fmul" :
                                                                "fdiv")
            : binary_name(value.binary);
        out_ << "  " << desired << " = " << operation << ' ';
        if (floating) out_ << floating_flags(options_);
        out_ << type << ' ' << reference(value.id) << ", "
             << reference(value.operands[1]) << '\n';

        std::string compare_type = type;
        std::string compare_expected = reference(value.id);
        std::string compare_desired = desired;
        if (floating) {
            const auto bits = integer_ir_type(
                static_cast<unsigned>(alignment * 8U));
            const auto expected_bits = stem + ".expected.bits";
            const auto desired_bits = stem + ".desired.bits";
            out_ << "  " << expected_bits << " = bitcast " << type << ' '
                 << reference(value.id) << " to " << bits << '\n';
            out_ << "  " << desired_bits << " = bitcast " << type << ' '
                 << desired << " to " << bits << '\n';
            compare_type = bits;
            compare_expected = expected_bits;
            compare_desired = desired_bits;
        }
        const auto pair_type = "{ " + compare_type + ", i1 }";
        out_ << "  " << pair << " = cmpxchg "
             << (value.is_volatile_access ? "volatile " : "") << "ptr "
             << reference(value.operands[0]) << ", " << compare_type << ' '
             << compare_expected << ", " << compare_type << ' '
             << compare_desired << ' ' << memory_order(value.memory_order)
             << ' ' << memory_order(value.failure_order) << ", align "
             << alignment << '\n';
        out_ << "  " << observed_bits << " = extractvalue " << pair_type
             << ' ' << pair << ", 0\n";
        if (floating) {
            out_ << "  " << observed << " = bitcast " << compare_type << ' '
                 << observed_bits << " to " << type << '\n';
        } else {
            out_ << "  " << observed << " = select i1 true, " << type << ' '
                 << observed_bits << ", " << type << " poison\n";
        }
        out_ << "  " << success << " = extractvalue " << pair_type << ' '
             << pair << ", 1\n";
        out_ << "  br i1 " << success << ", label %" << continuation
             << ", label %" << retry << '\n';
        out_ << retry << ":\n";
        out_ << "  br label %" << loop << '\n';
        out_ << continuation << ":\n";
        active_label_ = continuation;
    }

    void emit_atomic(const mir::ManagedValue& value) {
        if (value.atomic == mir::AtomicOperation::ThreadFence ||
            value.atomic == mir::AtomicOperation::SignalFence) {
            if (value.memory_order == mir::MemoryOrder::Relaxed) {
                out_ << "  ; relaxed atomic fence\n";
                return;
            }
            out_ << "  fence ";
            if (value.atomic == mir::AtomicOperation::SignalFence) {
                out_ << "syncscope(\"singlethread\") ";
            }
            out_ << memory_order(value.memory_order) << '\n';
            return;
        }

        const auto object_type = atomic_object_type(value);
        if (value.atomic == mir::AtomicOperation::CompareExchange) {
            emit_compare_exchange(value, object_type);
            return;
        }
        if (value.atomic == mir::AtomicOperation::FetchUpdate) {
            emit_atomic_update(value, object_type);
            return;
        }
        const auto type = ir_type(hir_, object_type);
        const auto alignment = ir_alignment(hir_, object_type);
        const auto address = reference(value.operands[0]);
        const auto volatile_text = value.is_volatile_access ? "volatile " : "";
        if (value.atomic == mir::AtomicOperation::Load) {
            out_ << "  " << reference(value.id) << " = load atomic "
                 << volatile_text << type << ", ptr " << address << ' '
                 << memory_order(value.memory_order) << ", align "
                 << alignment << '\n';
            return;
        }
        if (value.atomic == mir::AtomicOperation::Store) {
            out_ << "  store atomic " << volatile_text << type << ' '
                 << reference(value.operands[1]) << ", ptr " << address << ' '
                 << memory_order(value.memory_order) << ", align "
                 << alignment << '\n';
            return;
        }
        std::string operation;
        switch (value.atomic) {
        case mir::AtomicOperation::Exchange: operation = "xchg"; break;
        case mir::AtomicOperation::FetchAdd: operation = "add"; break;
        case mir::AtomicOperation::FetchSub: operation = "sub"; break;
        case mir::AtomicOperation::FetchAnd: operation = "and"; break;
        case mir::AtomicOperation::FetchXor: operation = "xor"; break;
        case mir::AtomicOperation::FetchOr: operation = "or"; break;
        default: break;
        }
        out_ << "  " << reference(value.id) << " = atomicrmw "
             << volatile_text << operation << " ptr " << address << ", "
             << type << ' ' << reference(value.operands[1]) << ' '
             << memory_order(value.memory_order) << ", align " << alignment
             << '\n';
    }

    void emit_variadic_state(const mir::ManagedValue& value) {
        const auto* target = target_for_triple(options_.target);
        const auto* abi = target ? find_abi(*target, entity_.abi) : nullptr;
        if (!abi) {
            diagnostics_.error(value.location,
                               "variadic state has no LLVM-debug ABI model");
            return;
        }
        const auto* state = value.variadic_state.valid() &&
                                    value.variadic_state.value <
                                        abi->variadic_states.size()
            ? &abi->variadic_states[value.variadic_state.value]
            : nullptr;
        std::vector<AbiValue> arguments;
        arguments.reserve(entity_.parameters.size());
        for (const auto& parameter : entity_.parameters) {
            arguments.push_back(llvm_abi_value(hir_, parameter.type));
        }
        std::vector<AbiValue> results;
        if (!is_void(hir_, entity_.result_type)) {
            results.push_back(llvm_abi_value(hir_, entity_.result_type));
        }
        const auto features = enabled_abi_features(options_);
        const auto classified = classify_variadic_signature(
            *abi, arguments, results, entity_.parameters.size(), features);
        if (!state || !classified) {
            diagnostics_.error(value.location,
                               "variadic state cannot classify its fixed prefix");
            return;
        }
        const auto cursor = abi_cursor_count(
            classified.layout.call.named_cursors, state->cursor);
        const auto displacement = static_cast<std::size_t>(state->base) +
                                  cursor * state->stride;
        if (state->kind == AbiVariadicStateKind::CursorOffset) {
            out_ << "  " << reference(value.id) << " = add "
                 << ir_type(hir_, value.type) << " 0, " << displacement
                 << '\n';
            return;
        }
        if (!state->llvm_va_list_offset ||
            abi->variadic_va_list_bytes == 0) {
            diagnostics_.error(
                value.location,
                "variadic state has no LLVM-debug va-list field");
            out_ << "  " << reference(value.id)
                 << " = getelementptr i8, ptr null, i64 0\n";
            return;
        }
        const auto suffix = std::to_string(value.id.value);
        out_ << "  %mir.va.field." << suffix
             << " = getelementptr i8, ptr %mir.va.list, i64 "
             << *state->llvm_va_list_offset << '\n';
        out_ << "  %mir.va.base." << suffix
             << " = load ptr, ptr %mir.va.field." << suffix
             << ", align 8\n";
        auto base = std::string("%mir.va.base.") + suffix;
        if (state->kind == AbiVariadicStateKind::StackAddress &&
            state->alignment > 1) {
            const auto pointer_integer =
                integer_ir_type(hir_.address_bits);
            out_ << "  %mir.va.raw." << suffix
                 << " = ptrtoint ptr " << base << " to "
                 << pointer_integer << "\n";
            out_ << "  %mir.va.biased." << suffix << " = add "
                 << pointer_integer << " %mir.va.raw."
                 << suffix << ", " << state->alignment - 1U << '\n';
            out_ << "  %mir.va.aligned.bits." << suffix
                 << " = and " << pointer_integer << " %mir.va.biased."
                 << suffix << ", "
                 << -static_cast<std::int64_t>(state->alignment) << '\n';
            out_ << "  %mir.va.aligned." << suffix
                 << " = inttoptr " << pointer_integer
                 << " %mir.va.aligned.bits." << suffix
                 << " to ptr\n";
            base = "%mir.va.aligned." + suffix;
        }
        const auto adjustment =
            state->kind == AbiVariadicStateKind::RegisterSaveAddress
                ? displacement : state->base;
        out_ << "  " << reference(value.id)
             << " = getelementptr i8, ptr " << base
             << ", i64 " << adjustment << '\n';
    }

    void emit_value(const mir::ManagedValue& value) {
        using mir::ValueKind;
        if (value.kind == ValueKind::Parameter) {
            const auto& parameter =
                entity_.parameters[value.parameter_index];
            if (!parameter_cell(parameter) || transport_pointer(parameter)) return;
            if (parameter.mode != ParameterMode::Out) {
                const auto& type = hir_.type(parameter.type);
                out_ << "  " << reference(value.id) << " = load "
                     << (type.is_atomic ? "atomic " : "")
                     << (type.is_volatile ? "volatile " : "")
                     << ir_type(hir_, parameter.type) << ", ptr %arg"
                     << value.parameter_index
                     << (type.is_atomic ? " seq_cst, align " : ", align ")
                     << ir_alignment(hir_, parameter.type) << '\n';
            } else {
                out_ << "  " << reference(value.id) << " = freeze "
                     << ir_type(hir_, parameter.type) << " poison\n";
            }
            return;
        }
        if (value.kind == ValueKind::VoidValue ||
            value.kind == ValueKind::ConstantInteger ||
            value.kind == ValueKind::LabelAddress ||
            value.kind == ValueKind::SlotAddress ||
            value.kind == ValueKind::GlobalAddress ||
            value.kind == ValueKind::FunctionAddress ||
            value.kind == ValueKind::Phi)
            return;
        if (value.kind == ValueKind::IndexedAddress) {
            const auto base = value.operands[0];
            const auto index = value.operands[1];
            const auto& pointer = hir_.type(value.type);
            out_ << "  " << reference(value.id) << " = getelementptr "
                 << storage_ir_type(hir_, target_, *pointer.pointee)
                 << ", ptr "
                 << reference(base) << ", "
                 << ir_type(hir_, function_.values[index.value].type) << ' '
                 << reference(index) << '\n';
            return;
        }
        if (value.kind == ValueKind::DynamicStackSave) {
            const auto pointer = "%mir.stack.mark." +
                                 std::to_string(value.id.value);
            out_ << "  " << pointer
                 << " = call ptr @llvm.stacksave.p0()\n";
            out_ << "  " << reference(value.id)
                 << " = ptrtoint ptr " << pointer << " to "
                 << ir_type(hir_, value.type) << "\n";
            return;
        }
        if (value.kind == ValueKind::DynamicAlloca) {
            const auto bound = value.operands.front();
            const auto& pointer = hir_.type(value.type);
            out_ << "  " << reference(value.id) << " = alloca "
                 << storage_ir_type(hir_, target_, *pointer.pointee) << ", "
                 << ir_type(hir_, function_.values[bound.value].type) << ' '
                 << reference(bound) << ", align " << value.integer_high
                 << '\n';
            return;
        }
        if (value.kind == ValueKind::DynamicStackRestore) {
            const auto mark = value.operands.front();
            const auto pointer = "%mir.stack.restore." +
                                 std::to_string(value.id.value);
            out_ << "  " << pointer << " = inttoptr "
                 << ir_type(hir_, function_.values[mark.value].type) << ' '
                 << reference(mark) << " to ptr\n";
            out_ << "  call void @llvm.stackrestore.p0(ptr " << pointer
                 << ")\n";
            return;
        }
        if (value.kind == ValueKind::ConstantFloating) {
            if (hir_.type(value.type).kind == hir::Type::Kind::Builtin &&
                (hir_.type(value.type).builtin == BuiltinType::F80 ||
                 hir_.type(value.type).builtin == BuiltinType::F128)) {
                return;
            }
            const auto bits = abi_type_bits(hir_, value.type) == 32
                                  ? "i32" : "i64";
            out_ << "  " << reference(value.id) << " = bitcast " << bits << ' '
                 << value.integer << " to " << ir_type(hir_, value.type) << '\n';
            return;
        }
        if (value.kind == ValueKind::VariadicState) {
            emit_variadic_state(value);
            return;
        }
        const auto result = reference(value.id);
        if (value.kind == ValueKind::Splat) {
            const auto source = value.operands.front();
            const auto source_type =
                ir_type(hir_, function_.values[source.value].type);
            const auto vector_type = ir_type(hir_, value.type);
            const auto& shape = hir_.type(value.type);
            const auto mask_type =
                "<" + std::to_string(shape.lanes) + " x i32>";
            const auto seed = "%mir.splat." + std::to_string(value.id.value);
            out_ << "  " << seed << " = insertelement " << vector_type
                 << " poison, " << source_type << ' ' << reference(source)
                 << ", i32 0\n";
            out_ << "  " << result << " = shufflevector " << vector_type
                 << ' ' << seed << ", " << vector_type << " poison, "
                 << mask_type << " zeroinitializer\n";
            return;
        }
        if (value.kind == ValueKind::ExtractElement) {
            const auto vector = value.operands[0];
            const auto index = value.operands[1];
            out_ << "  " << result << " = extractelement "
                 << ir_type(hir_, function_.values[vector.value].type) << ' '
                 << reference(vector) << ", "
                 << ir_type(hir_, function_.values[index.value].type) << ' '
                 << reference(index) << '\n';
            return;
        }
        if (value.kind == ValueKind::InsertElement) {
            const auto vector = value.operands[0];
            const auto index = value.operands[1];
            const auto element = value.operands[2];
            out_ << "  " << result << " = insertelement "
                 << ir_type(hir_, function_.values[vector.value].type) << ' '
                 << reference(vector) << ", "
                 << ir_type(hir_, function_.values[element.value].type) << ' '
                 << reference(element) << ", "
                 << ir_type(hir_, function_.values[index.value].type) << ' '
                 << reference(index) << '\n';
            return;
        }
        if (value.kind == ValueKind::LifetimeStart ||
            value.kind == ValueKind::LifetimeEnd) {
            out_ << "  ; "
                 << (value.kind == ValueKind::LifetimeStart ? "lifetime.start "
                                                            : "lifetime.end ")
                 << "%mir.slot" << value.slot->value << '\n';
            return;
        }
        if (value.kind == ValueKind::Load) {
            out_ << "  " << result << " = load " << ir_type(hir_, value.type)
                 << ", ptr %mir.slot" << value.slot->value << ", align "
                 << ir_alignment(hir_, value.type) << '\n';
            return;
        }
        if (value.kind == ValueKind::Store) {
            const auto source = value.operands.front();
            const auto type = function_.values[source.value].type;
            out_ << "  store " << ir_type(hir_, type) << ' ' << reference(source)
                 << ", ptr %mir.slot" << value.slot->value << ", align "
                 << ir_alignment(hir_, type) << '\n';
            return;
        }
        if (value.kind == ValueKind::PointerLoad) {
            const auto address = value.operands.front();
            out_ << "  " << result << " = load "
                 << (value.is_volatile_access ? "volatile " : "")
                 << ir_type(hir_, value.type) << ", ptr " << reference(address)
                 << ", align "
                 << (value.memory_alignment != 0
                         ? value.memory_alignment
                         : ir_alignment(hir_, value.type))
                 << '\n';
            return;
        }
        if (value.kind == ValueKind::PointerStore) {
            const auto address = value.operands[0];
            const auto source = value.operands[1];
            const auto type = function_.values[source.value].type;
            out_ << "  store " << (value.is_volatile_access ? "volatile " : "")
                 << ir_type(hir_, type) << ' ' << reference(source) << ", ptr "
                 << reference(address) << ", align "
                 << (value.memory_alignment != 0
                         ? value.memory_alignment
                         : ir_alignment(hir_, type))
                 << '\n';
            return;
        }
        if (value.kind == ValueKind::IndexedLoad) {
            const auto base = value.operands[0];
            const auto index = value.operands[1];
            const auto& pointer = hir_.type(
                function_.values[base.value].type);
            const auto address_element = pointer.pointee
                ? *pointer.pointee : value.type;
            const auto address =
                "%mir.addr." + std::to_string(value.id.value);
            out_ << "  " << address << " = getelementptr "
                 << storage_ir_type(hir_, target_, address_element)
                 << ", ptr "
                 << reference(base)
                 << ", " << ir_type(hir_, function_.values[index.value].type)
                 << ' ' << reference(index) << '\n';
            out_ << "  " << result << " = load "
                 << (value.is_volatile_access ? "volatile " : "")
                 << ir_type(hir_, value.type) << ", ptr " << address
                 << ", align "
                 << (value.memory_alignment != 0
                         ? value.memory_alignment
                         : ir_alignment(hir_, address_element))
                 << '\n';
            return;
        }
        if (value.kind == ValueKind::GlobalLoad) {
            const auto& object = hir_.object(*value.object);
            out_ << "  " << result << " = load "
                 << (value.is_volatile_access ? "volatile " : "")
                 << ir_type(hir_, value.type) << ", ptr "
                 << symbol_name(object.link_symbol) << ", align "
                 << ir_alignment(hir_, value.type) << '\n';
            return;
        }
        if (value.kind == ValueKind::GlobalStore) {
            const auto source = value.operands.front();
            const auto type = function_.values[source.value].type;
            const auto& object = hir_.object(*value.object);
            out_ << "  store "
                 << (value.is_volatile_access ? "volatile " : "")
                 << ir_type(hir_, type) << ' ' << reference(source)
                 << ", ptr " << symbol_name(object.link_symbol) << ", align "
                 << ir_alignment(hir_, type) << '\n';
            return;
        }
        if (value.kind == ValueKind::Atomic) {
            emit_atomic(value);
            return;
        }
        if (value.kind == ValueKind::Call) {
            const auto callee =
                *hir::call_signature(hir_, value.callee, value.call_signature);
            bool llvm_musttail = value.must_tail;
            if (llvm_musttail) {
                const bool parameters_match =
                    entity_.parameters.size() == callee.parameters.size() &&
                    std::equal(
                        entity_.parameters.begin(), entity_.parameters.end(),
                        callee.parameters.begin(),
                        [](const hir::Parameter& caller,
                           const hir::Parameter& target) {
                            return caller.type == target.type &&
                                   caller.mode == ParameterMode::In &&
                                   target.mode == ParameterMode::In &&
                                   caller.physical_location ==
                                       target.physical_location;
                        });
                const bool arguments_direct = std::all_of(
                    value.call_arguments.begin(),
                    value.call_arguments.end(),
                    [](const mir::CallArgument& argument) {
                        return argument.value.has_value();
                    });
                if (entity_.abi != callee.abi ||
                    entity_.result_type != callee.result_type ||
                    entity_.variadic != callee.variadic ||
                    !parameters_match || !arguments_direct) {
                    diagnostics_.error(
                        value.location,
                        "LLVM debug serialization cannot represent this Cross musttail ABI boundary");
                    llvm_musttail = false;
                }
            }
            out_ << "  ";
            if (!is_void(hir_, value.type)) out_ << result << " = ";
            if (llvm_musttail) out_ << "musttail ";
            out_ << "call " << abi_name(callee.abi, value.location)
                 << ir_type(hir_, value.type) << ' '
                 << (value.callee
                         ? symbol_name(hir_.function(*value.callee).link_symbol)
                         : reference(value.operands.front()))
                 << '(';
            for (std::size_t index = 0; index < value.call_arguments.size();
                 ++index) {
                if (index != 0) out_ << ", ";
                const auto& argument = value.call_arguments[index];
                if (argument.value) {
                    out_ << ir_type(
                                hir_,
                                function_.values[argument.value->value].type)
                         << ' ' << reference(*argument.value);
                } else {
                    out_ << "ptr %mir.slot" << argument.cell->value;
                }
            }
            out_ << ")\n";
            return;
        }
        if (value.kind == ValueKind::PatchValue) {
            if (value.patch_initial_address) {
                diagnostics_.error(value.location,
                    "LLVM debug serialization cannot preserve a relocatable $::patch immediate");
                return;
            }
            const auto bits = patch_bits(hir_, value.type);
            std::string mnemonic;
            std::string register_modifier;
            if (bits == 8) {
                mnemonic = "movb";
                register_modifier = "${0:b}";
            } else if (bits == 16) {
                mnemonic = "movw";
                register_modifier = "${0:w}";
            } else if (bits == 32) {
                mnemonic = "movl";
                register_modifier = "${0:k}";
            } else if (bits == 64) {
                mnemonic = "movabsq";
                register_modifier = "${0:q}";
            } else {
                diagnostics_.error(value.location,
                                   "x86-64 has no contiguous materializer for "
                                   "this $::patch value type");
                return;
            }
            const auto end = ".Lcross.patch.value." + std::to_string(value.patch_id) + ".end";
            const bool first_use = emitted_patch_cells_.insert(value.patch_id).second;
            auto assembly = first_use
                ? mnemonic + " $$" + to_decimal({value.integer, value.integer_high}) +
                    ", " + register_modifier + "\n" + end + ":"
                : (bits == 64 ? std::string("movq") : mnemonic) + " " + end + "-" +
                    std::to_string(bits / 8U) + "(%rip), " + register_modifier;
            out_ << "  " << result << " = call " << ir_type(hir_, value.type)
                 << " asm sideeffect " << llvm_string(assembly) << ", "
                 << llvm_string("=r") << "()\n";
            return;
        }
        if (value.kind == ValueKind::Intrinsic) {
            switch (value.intrinsic) {
            case mir::IntrinsicOperation::Expect: {
                const auto source = value.operands.front();
                const auto type = ir_type(hir_, value.type);
                out_ << "  " << result << " = call " << type
                     << " @llvm.expect." << type << '(' << type << ' '
                     << reference(source) << ", " << type << ' '
                     << to_decimal({value.integer, value.integer_high})
                     << ")\n";
                break;
            }
            case mir::IntrinsicOperation::Assume:
                // Cross assumptions are unevaluated. Keeping a typed marker
                // in MIR permits future fact propagation without introducing
                // runtime computation in the debug serializer.
                out_ << "  ; cross.assume (unevaluated)\n";
                break;
            case mir::IntrinsicOperation::Unreachable:
            case mir::IntrinsicOperation::Trap:
                break;
            case mir::IntrinsicOperation::MachineNop:
                out_ << "  call void asm sideeffect \"nop\", \"\"()\n";
                break;
            }
            return;
        }
        if (value.kind == ValueKind::Select) {
            const auto condition = value.operands[0];
            const auto truth = value.operands[1];
            const auto falsity = value.operands[2];
            const auto condition_type =
                ir_type(hir_, function_.values[condition.value].type);
            const auto result_type = ir_type(hir_, value.type);
            const auto& condition_shape =
                hir_.type(function_.values[condition.value].type);
            const bool vector_condition =
                condition_shape.kind == hir::Type::Kind::Vector;
            const auto predicate_type = vector_condition
                ? "<" + std::to_string(condition_shape.lanes) + " x i1>"
                : std::string("i1");
            const auto predicate =
                "%mir.select.p" + std::to_string(value.id.value);
            out_ << "  " << predicate << " = icmp ne "
                 << condition_type << ' ' << reference(condition)
                 << (vector_condition ? ", zeroinitializer\n" : ", 0\n");
            out_ << "  " << result << " = select " << predicate_type << ' '
                 << predicate
                 << ", " << result_type << ' ' << reference(truth)
                 << ", " << result_type << ' ' << reference(falsity)
                 << '\n';
            return;
        }
        if (value.kind == ValueKind::Unary) {
            const auto operand = value.operands.front();
            const auto operand_id = function_.values[operand.value].type;
            const auto type = ir_type(hir_, operand_id);
            const auto vector = is_vector(hir_, operand_id);
            if (value.unary == mir::UnaryOperation::IsZero) {
                const auto predicate = "%mir.p" + std::to_string(value.id.value);
                if (is_floating(hir_, operand_id)) {
                    out_ << "  " << predicate << " = fcmp "
                         << floating_flags(options_) << "oeq " << type << ' '
                         << reference(operand) << ", "
                         << (vector ? "zeroinitializer" : "0.0") << '\n';
                } else {
                    out_ << "  " << predicate << " = icmp eq " << type << ' '
                         << reference(operand) << ", "
                         << (vector ? "zeroinitializer" : "0") << '\n';
                }
                if (vector) {
                    const auto& shape = hir_.type(operand_id);
                    const auto predicate_type =
                        "<" + std::to_string(shape.lanes) + " x i1>";
                    out_ << "  " << result << " = sext " << predicate_type
                         << ' ' << predicate << " to "
                         << ir_type(hir_, value.type) << '\n';
                } else {
                    out_ << "  " << result << " = zext i1 " << predicate
                         << " to i8\n";
                }
            } else if (is_floating(hir_, operand_id)) {
                out_ << "  " << result << " = fneg "
                     << floating_flags(options_) << type << ' '
                     << reference(operand) << '\n';
            } else {
                out_ << "  " << result << " = "
                     << (value.unary == mir::UnaryOperation::Negate ? "sub " : "xor ")
                     << type << ' ';
                if (value.unary == mir::UnaryOperation::Negate) {
                    out_ << (vector ? "zeroinitializer" : "0") << ", "
                         << reference(operand);
                } else {
                    out_ << reference(operand) << ", "
                         << (vector ? vector_constant(hir_, operand_id, "-1")
                                    : "-1");
                }
                out_ << '\n';
            }
            return;
        }
        if (value.kind == ValueKind::Cast) {
            const auto operand = value.operands.front();
            const auto source_type = ir_type(hir_, function_.values[operand.value].type);
            const auto destination_type = ir_type(hir_, value.type);
            if (value.cast == mir::CastOperation::FloatExtend ||
                value.cast == mir::CastOperation::FloatTruncate) {
                out_ << "  " << result << " = "
                     << (value.cast == mir::CastOperation::FloatExtend ? "fpext" : "fptrunc")
                     << ' ' << source_type << ' ' << reference(operand)
                     << " to " << destination_type << '\n';
            } else if (
                value.cast == mir::CastOperation::SignedIntegerToFloat ||
                value.cast == mir::CastOperation::UnsignedIntegerToFloat ||
                value.cast == mir::CastOperation::FloatToSignedInteger ||
                value.cast == mir::CastOperation::FloatToUnsignedInteger) {
                const auto operation =
                    value.cast == mir::CastOperation::SignedIntegerToFloat
                        ? "sitofp"
                    : value.cast == mir::CastOperation::UnsignedIntegerToFloat
                        ? "uitofp"
                    : value.cast == mir::CastOperation::FloatToSignedInteger
                        ? "fptosi"
                        : "fptoui";
                out_ << "  " << result << " = " << operation << ' '
                     << source_type << ' ' << reference(operand) << " to "
                     << destination_type << '\n';
            } else if (value.cast == mir::CastOperation::Reinterpret) {
                if (source_type == destination_type) {
                    // MIR keeps source qualifiers in its type identity, while
                    // LLVM represents qualifiers on memory operations.  A
                    // select is an exact, optimizer-removable value copy for
                    // integers, pointers, and floating-point values alike.
                    out_ << "  " << result << " = select i1 true, "
                         << destination_type << ' ' << reference(operand)
                         << ", " << destination_type << " poison\n";
                } else if (source_type == "ptr") {
                    out_ << "  " << result << " = ptrtoint ptr "
                         << reference(operand) << " to "
                         << destination_type << '\n';
                } else if (destination_type == "ptr") {
                    out_ << "  " << result << " = inttoptr "
                         << source_type << ' ' << reference(operand)
                         << " to ptr\n";
                } else {
                    out_ << "  " << result << " = bitcast " << source_type
                         << ' ' << reference(operand) << " to "
                         << destination_type << '\n';
                }
            } else {
                const auto operation = value.cast == mir::CastOperation::SignExtend ? "sext" :
                                       value.cast == mir::CastOperation::ZeroExtend ? "zext" : "trunc";
                out_ << "  " << result << " = " << operation << ' ' << source_type << ' '
                     << reference(operand) << " to " << destination_type << '\n';
            }
            return;
        }
        const auto left = value.operands[0];
        const auto right = value.operands[1];
        const auto operand_type_id = function_.values[left.value].type;
        const auto operand_type = ir_type(hir_, operand_type_id);
        if (value.binary == mir::BinaryOperation::RotateLeft ||
            value.binary == mir::BinaryOperation::RotateRight) {
            const auto intrinsic =
                value.binary == mir::BinaryOperation::RotateLeft
                    ? "llvm.fshl." : "llvm.fshr.";
            out_ << "  " << result << " = call " << operand_type << " @"
                 << intrinsic << operand_type << '(' << operand_type << ' '
                 << reference(left) << ", " << operand_type << ' '
                 << reference(left) << ", " << operand_type << ' '
                 << reference(right) << ")\n";
            return;
        }
        if (value.binary == mir::BinaryOperation::UnsignedMultiplyHigh ||
            value.binary == mir::BinaryOperation::SignedMultiplyHigh) {
            const auto bits = abi_type_bits(hir_, operand_type_id);
            const auto wide = integer_ir_type(bits * 2);
            const auto extend =
                value.binary == mir::BinaryOperation::SignedMultiplyHigh
                    ? "sext" : "zext";
            const auto name = "%mir.h" + std::to_string(value.id.value);
            out_ << "  " << name << ".l = " << extend << ' ' << operand_type
                 << ' ' << reference(left) << " to " << wide << '\n';
            out_ << "  " << name << ".r = " << extend << ' ' << operand_type
                 << ' ' << reference(right) << " to " << wide << '\n';
            out_ << "  " << name << ".p = mul " << wide << ' ' << name
                 << ".l, " << name << ".r\n";
            out_ << "  " << name << ".h = lshr " << wide << ' ' << name
                 << ".p, " << bits << '\n';
            out_ << "  " << result << " = trunc " << wide << ' ' << name
                 << ".h to " << operand_type << '\n';
            return;
        }
        if (comparison(value.binary)) {
            const auto predicate = "%mir.p" + std::to_string(value.id.value);
            if (is_floating(hir_, operand_type_id)) {
                const auto predicate_name = value.binary == mir::BinaryOperation::Equal ? "oeq" :
                    value.binary == mir::BinaryOperation::NotEqual ? "une" :
                    value.binary == mir::BinaryOperation::SignedLess ? "olt" :
                    value.binary == mir::BinaryOperation::SignedLessEqual ? "ole" :
                    value.binary == mir::BinaryOperation::SignedGreater ? "ogt" : "oge";
                out_ << "  " << predicate << " = fcmp "
                     << floating_flags(options_) << predicate_name << ' '
                     << operand_type << ' ' << reference(left) << ", " << reference(right) << '\n';
            } else {
                out_ << "  " << predicate << " = icmp " << binary_name(value.binary) << ' '
                     << operand_type << ' ' << reference(left) << ", " << reference(right) << '\n';
            }
            if (is_vector(hir_, operand_type_id)) {
                const auto& shape = hir_.type(operand_type_id);
                const auto predicate_type =
                    "<" + std::to_string(shape.lanes) + " x i1>";
                out_ << "  " << result << " = sext " << predicate_type << ' '
                     << predicate << " to " << ir_type(hir_, value.type) << '\n';
            } else {
                out_ << "  " << result << " = zext i1 " << predicate
                     << " to i8\n";
            }
        } else {
            const auto operation = is_floating(hir_, operand_type_id)
                ? (value.binary == mir::BinaryOperation::Add ? "fadd" :
                   value.binary == mir::BinaryOperation::Subtract ? "fsub" :
                   value.binary == mir::BinaryOperation::Multiply ? "fmul" : "fdiv")
                : binary_name(value.binary);
            out_ << "  " << result << " = " << operation << ' ';
            if (is_floating(hir_, operand_type_id)) {
                out_ << floating_flags(options_);
            }
            out_ << operand_type << ' ' << reference(left) << ", "
                 << reference(right) << '\n';
        }
    }

    // Outside a manual interface an `out`/`inout` parameter's MIR value is
    // its transport pointer, and MIR performs the copy-in and copy-out.
    bool transport_pointer(const hir::Parameter& parameter) const {
        return parameter.mode != ParameterMode::In &&
               !hir::manual_interface(entity_);
    }

    void emit_parameter_copyout(mir::BlockId block) {
        for (std::size_t index = 0; index < entity_.parameters.size(); ++index) {
            const auto& parameter = entity_.parameters[index];
            if (parameter.mode == ParameterMode::In ||
                transport_pointer(parameter)) {
                continue;
            }
            const auto name = "$param." + std::to_string(index);
            const auto slot = std::find_if(
                function_.slots.begin(), function_.slots.end(),
                [&](const mir::ManagedSlot& candidate) {
                    return candidate.name == name;
                });
            if (slot == function_.slots.end()) {
                diagnostics_.error(parameter.location,
                                   "managed output parameter has no cell");
                continue;
            }
            const auto temporary =
                "%mir.copyout." + std::to_string(block.value) + "." +
                std::to_string(index);
            const auto& type = hir_.type(parameter.type);
            out_ << "  " << temporary << " = load "
                 << (type.is_atomic ? "atomic " : "")
                 << (type.is_volatile ? "volatile " : "")
                 << ir_type(hir_, parameter.type) << ", ptr %mir.slot"
                 << slot->id.value
                 << (type.is_atomic ? " seq_cst, align " : ", align ")
                 << ir_alignment(hir_, parameter.type) << '\n';
            out_ << "  store " << (type.is_atomic ? "atomic " : "")
                 << (type.is_volatile ? "volatile " : "")
                 << ir_type(hir_, parameter.type) << ' ' << temporary
                 << ", ptr %arg" << index
                 << (type.is_atomic ? " seq_cst, align " : ", align ")
                 << ir_alignment(hir_, parameter.type) << '\n';
        }
    }

    void emit_block(const mir::ManagedBlock& block) {
        active_label_ = block_reference(block.id);
        out_ << block_reference(block.id) << ":\n";
        for (const auto id : block.values) {
            const auto& value = function_.values[id.value];
            if (value.kind == mir::ValueKind::Phi) emit_phi(value);
        }
        if (block.id == function_.entry) {
            if (entity_.variadic && !entity_.variadic_bindings.empty()) {
                const auto* target = target_for_triple(options_.target);
                const auto* abi = target
                    ? find_abi(*target, entity_.abi)
                    : nullptr;
                if (!abi || abi->variadic_va_list_bytes == 0) {
                    diagnostics_.error(
                        entity_.location,
                        "LLVM debug serialization has no variadic state layout");
                } else {
                    out_ << "  %mir.va.list = alloca ["
                         << abi->variadic_va_list_bytes
                         << " x i8], align "
                         << abi->variadic_va_list_alignment << '\n';
                    out_ << "  call void @llvm.va_start(ptr %mir.va.list)\n";
                }
            }
            for (const auto& slot : function_.slots) {
                out_ << "  %mir.slot" << slot.id.value << " = alloca "
                     << storage_ir_type(hir_, target_, slot.type) << ", align "
                     << std::max({ir_alignment(hir_, slot.type),
                                  hir::requested_alignment(hir_, slot.type),
                                  slot.minimum_alignment})
                     << '\n';
            }
        }
        for (const auto id : block.values) emit_value(function_.values[id.value]);
        const auto& terminator = block.terminator;
        switch (terminator.kind) {
        case mir::TerminatorKind::Return:
            emit_parameter_copyout(block.id);
            if (terminator.value) {
                out_ << "  ret " << ir_type(hir_, entity_.result_type) << ' '
                     << reference(*terminator.value) << '\n';
            } else {
                out_ << "  ret void\n";
            }
            break;
        case mir::TerminatorKind::Branch:
            out_ << "  br label %"
                 << block_reference(terminator.successors.front()) << '\n';
            break;
        case mir::TerminatorKind::ConditionalBranch: {
            const auto condition = *terminator.value;
            const auto predicate = "%mir.cond." + std::to_string(block.id.value);
            out_ << "  " << predicate << " = icmp ne "
                 << ir_type(hir_, function_.values[condition.value].type) << ' '
                 << reference(condition) << ", 0\n";
            out_ << "  br i1 " << predicate << ", label %"
                 << block_reference(terminator.successors[0])
                 << ", label %"
                 << block_reference(terminator.successors[1]) << '\n';
            break;
        }
        case mir::TerminatorKind::IndirectBranch: {
            out_ << "  indirectbr ptr " << reference(*terminator.value)
                 << ", [";
            for (std::size_t index = 0;
                 index < terminator.successors.size(); ++index) {
                if (index != 0) out_ << ", ";
                out_ << "label %"
                     << block_reference(terminator.successors[index]);
            }
            out_ << "]\n";
            break;
        }
        case mir::TerminatorKind::Unreachable:
            out_ << "  unreachable\n";
            break;
        case mir::TerminatorKind::Trap:
            out_ << "  call void @llvm.trap()\n"
                    "  unreachable\n";
            break;
        case mir::TerminatorKind::None:
            break;
        }
    }

    const hir::Module& hir_;
    const mir::ManagedFunction& function_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    const hir::Function& entity_;
    const TargetInfo* target_;
    std::vector<std::string> references_;
    std::string active_label_;
    std::ostringstream out_;
    std::unordered_set<std::uint32_t> emitted_patch_cells_;
};

} // namespace

std::string llvm_storage_type(const hir::Module& hir_module,
                              const TargetInfo* target, hir::TypeId type) {
    return storage_ir_type(hir_module, target, type);
}

std::string emit_managed_mir_function(const hir::Module& hir_module,
                                      const mir::ManagedFunction& function,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics) {
    return Emitter(hir_module, function, options, diagnostics).run();
}

} // namespace cross
