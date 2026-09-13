// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/gcc/gimple_ir.hpp"

#include "common/uint128.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cross::debug {
namespace {

std::string c_string(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (const char byte : text) {
        const auto ch = static_cast<unsigned char>(byte);
        if (ch >= 0x20 && ch <= 0x7e && ch != '"' && ch != '\\') {
            out << static_cast<char>(ch);
        } else {
            out << '\\' << std::oct << std::setfill('0') << std::setw(3)
                << static_cast<unsigned>(ch) << std::dec;
        }
    }
    out << '"';
    return out.str();
}

std::string function_identifier(hir::FunctionId id) {
    return "cross_function_" + std::to_string(id.value);
}

std::string object_identifier(hir::ObjectId id) {
    return "cross_object_" + std::to_string(id.value);
}

bool parameter_cell(const hir::Parameter& parameter) {
    return parameter.mode != ParameterMode::In ||
           (parameter.physical_location &&
            *parameter.physical_location != "auto");
}

bool comparison(mir::BinaryOperation operation) {
    return operation >= mir::BinaryOperation::Equal;
}

enum class IntegerDomain { None, Signed, Unsigned };

IntegerDomain integer_domain(mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::SignedDivide:
    case BinaryOperation::SignedRemainder:
    case BinaryOperation::ShiftRightArithmetic:
    case BinaryOperation::SignedLess:
    case BinaryOperation::SignedLessEqual:
    case BinaryOperation::SignedGreater:
    case BinaryOperation::SignedGreaterEqual: return IntegerDomain::Signed;
    case BinaryOperation::UnsignedDivide:
    case BinaryOperation::UnsignedRemainder:
    case BinaryOperation::ShiftRightLogical:
    case BinaryOperation::UnsignedLess:
    case BinaryOperation::UnsignedLessEqual:
    case BinaryOperation::UnsignedGreater:
    case BinaryOperation::UnsignedGreaterEqual: return IntegerDomain::Unsigned;
    default: return IntegerDomain::None;
    }
}

bool domain_uses_right_operand(mir::BinaryOperation operation) {
    return operation != mir::BinaryOperation::ShiftRightArithmetic &&
           operation != mir::BinaryOperation::ShiftRightLogical;
}

std::string binary_operator(mir::BinaryOperation operation) {
    using mir::BinaryOperation;
    switch (operation) {
    case BinaryOperation::Add: return "+";
    case BinaryOperation::Subtract: return "-";
    case BinaryOperation::Multiply: return "*";
    case BinaryOperation::SignedDivide:
    case BinaryOperation::UnsignedDivide: return "/";
    case BinaryOperation::SignedRemainder:
    case BinaryOperation::UnsignedRemainder: return "%";
    case BinaryOperation::BitAnd: return "&";
    case BinaryOperation::BitOr: return "|";
    case BinaryOperation::BitXor: return "^";
    case BinaryOperation::ShiftLeft: return "<<";
    case BinaryOperation::ShiftRightArithmetic:
    case BinaryOperation::ShiftRightLogical: return ">>";
    case BinaryOperation::RotateLeft:
    case BinaryOperation::RotateRight: return "|";
    case BinaryOperation::Equal: return "==";
    case BinaryOperation::NotEqual: return "!=";
    case BinaryOperation::SignedLess:
    case BinaryOperation::UnsignedLess: return "<";
    case BinaryOperation::SignedLessEqual:
    case BinaryOperation::UnsignedLessEqual: return "<=";
    case BinaryOperation::SignedGreater:
    case BinaryOperation::UnsignedGreater: return ">";
    case BinaryOperation::SignedGreaterEqual:
    case BinaryOperation::UnsignedGreaterEqual: return ">=";
    }
    return "+";
}

unsigned memory_order(mir::MemoryOrder order) {
    // GCC's __ATOMIC_* constants are stable public values.  Spelling the
    // values keeps the generated file independent of headers.
    switch (order) {
    case mir::MemoryOrder::Relaxed: return 0;
    case mir::MemoryOrder::Acquire: return 2;
    case mir::MemoryOrder::Release: return 3;
    case mir::MemoryOrder::AcqRel: return 4;
    case mir::MemoryOrder::SeqCst: return 5;
    }
    return 5;
}

bool has_attribute(const hir::Function& function, std::string_view name) {
    for (const auto* declaration : function.declarations) {
        if (declaration && declaration->attribute(name)) return true;
    }
    return false;
}

class TypeEmitter {
public:
    TypeEmitter(const hir::Module& hir_module, Diagnostics& diagnostics)
        : hir_(hir_module), diagnostics_(diagnostics),
          emitted_(hir_module.types.size()), active_(hir_module.types.size()),
          required_(hir_module.types.size()) {}

    std::string name(hir::TypeId id) const {
        return "cross_t" + std::to_string(id.value);
    }

    std::string mask_name(hir::TypeId id) const {
        return "cross_mask_t" + std::to_string(id.value);
    }

    unsigned bits(hir::TypeId id) const {
        const auto& type = hir_.type(id);
        if (type.kind == hir::Type::Kind::Pointer) return hir_.address_bits;
        if ((type.kind == hir::Type::Kind::Vector ||
             type.kind == hir::Type::Kind::Array) &&
            type.element) {
            return bits(*type.element) * type.lanes;
        }
        if (type.kind == hir::Type::Kind::Record && type.record) {
            return static_cast<unsigned>(hir_.record(*type.record).size * 8U);
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
        case BuiltinType::Label: return hir_.address_bits;
        case BuiltinType::F80: return 80;
        case BuiltinType::I128:
        case BuiltinType::U128:
        case BuiltinType::F128: return 128;
        }
        return 0;
    }

    unsigned bytes(hir::TypeId id) const {
        return std::max(1U, (bits(id) + 7U) / 8U);
    }

    bool is_void(hir::TypeId id) const {
        const auto& type = hir_.type(id);
        return type.kind == hir::Type::Kind::Builtin &&
               type.builtin == BuiltinType::Void;
    }

    bool is_vector(hir::TypeId id) const {
        return hir_.type(id).kind == hir::Type::Kind::Vector;
    }

    bool is_floating(hir::TypeId id) const {
        const auto& type = hir_.type(id);
        if (type.kind == hir::Type::Kind::Vector && type.element) {
            return is_floating(*type.element);
        }
        return type.kind == hir::Type::Kind::Builtin &&
               (type.builtin == BuiltinType::F32 ||
                type.builtin == BuiltinType::F64 ||
                type.builtin == BuiltinType::Fptr ||
                type.builtin == BuiltinType::F80 ||
                type.builtin == BuiltinType::F128);
    }

    bool is_integer(hir::TypeId id) const {
        const auto& type = hir_.type(id);
        if (type.kind == hir::Type::Kind::Vector && type.element) {
            return is_integer(*type.element);
        }
        if (type.kind != hir::Type::Kind::Builtin) return false;
        switch (type.builtin) {
        case BuiltinType::Bool:
        case BuiltinType::I8:
        case BuiltinType::U8:
        case BuiltinType::I16:
        case BuiltinType::U16:
        case BuiltinType::I32:
        case BuiltinType::U32:
        case BuiltinType::I64:
        case BuiltinType::U64:
        case BuiltinType::Iptr:
        case BuiltinType::Uptr:
        case BuiltinType::I128:
        case BuiltinType::U128: return true;
        default: return false;
        }
    }

    std::string integer_domain_name(hir::TypeId id,
                                    IntegerDomain domain) const {
        if (is_vector(id)) {
            return "cross_gimple_" + integer_domain_spelling(domain) + "_v" +
                   std::to_string(id.value);
        }
        return integer_scalar(bits(id), domain);
    }

    std::string integer_domain_mask_name(hir::TypeId id,
                                         IntegerDomain domain) const {
        return "cross_gimple_" + integer_domain_spelling(domain) + "_mask_v" +
               std::to_string(id.value);
    }

    void require(hir::TypeId id) {
        required_.at(id.value) = true;
    }

    void emit(std::ostringstream& out) {
        out << "typedef __SIZE_TYPE__ cross_gimple_size;\n";
        for (const auto& record : hir_.records) {
            out << "struct cross_record_" << record.id.value << ";\n";
        }
        if (!hir_.records.empty()) out << '\n';
        for (std::uint32_t id = 0; id < hir_.types.size(); ++id) {
            if (!required_[id]) continue;
            emit_one({id}, out);
        }
        for (const auto& record : hir_.records) {
            if (!record.complete) continue;
            out << "struct __attribute__((packed, aligned("
                << std::max(1U, record.alignment) << "))) cross_record_"
                << record.id.value << " { unsigned char bytes["
                << std::max<std::uint64_t>(1, record.size) << "]; };\n";
        }
        if (!hir_.records.empty()) out << '\n';
    }

private:
    static std::string integer_domain_spelling(IntegerDomain domain) {
        return domain == IntegerDomain::Signed ? "signed" : "unsigned";
    }

    static std::string integer_scalar(unsigned width, IntegerDomain domain) {
        const bool is_signed = domain == IntegerDomain::Signed;
        if (width <= 8) return is_signed ? "signed char" : "unsigned char";
        if (width <= 16) return is_signed ? "short" : "unsigned short";
        if (width <= 32) return is_signed ? "int" : "unsigned int";
        if (width <= 64) return is_signed ? "long long" : "unsigned long long";
        return is_signed ? "__int128" : "unsigned __int128";
    }

    std::string qualifiers(const hir::Type& type) const {
        std::string result;
        if (type.is_const) result += "const ";
        if (type.is_volatile) result += "volatile ";
        return result;
    }

    std::string builtin(const hir::Type& type) const {
        switch (type.builtin) {
        case BuiltinType::Void: return "void";
        case BuiltinType::Bool: return "_Bool";
        case BuiltinType::I8: return "signed char";
        case BuiltinType::U8: return "unsigned char";
        case BuiltinType::I16: return "short";
        case BuiltinType::U16: return "unsigned short";
        case BuiltinType::I32: return "int";
        case BuiltinType::U32: return "unsigned int";
        case BuiltinType::I64: return "long long";
        case BuiltinType::U64: return "unsigned long long";
        case BuiltinType::Iptr:
            return hir_.address_bits == 32 ? "int" : "long long";
        case BuiltinType::Uptr:
            return hir_.address_bits == 32 ? "unsigned int"
                                           : "unsigned long long";
        case BuiltinType::I128: return "__int128";
        case BuiltinType::U128: return "unsigned __int128";
        case BuiltinType::F32: return "float";
        case BuiltinType::F64: return "double";
        case BuiltinType::Fptr:
            return hir_.address_bits == 32 ? "float" : "double";
        case BuiltinType::F80: return "long double";
        case BuiltinType::F128: return "__float128";
        case BuiltinType::Label: return "void *";
        }
        return "void";
    }

    void emit_one(hir::TypeId id, std::ostringstream& out) {
        if (emitted_.at(id.value)) return;
        if (active_.at(id.value)) {
            diagnostics_.command_error(
                "GIMPLE type serialization found a non-record type cycle");
            return;
        }
        active_[id.value] = true;
        const auto& type = hir_.type(id);
        switch (type.kind) {
        case hir::Type::Kind::Function:
            diagnostics_.command_error("GCC GIMPLE serialization of "
                                       "function-pointer interfaces is not "
                                       "implemented");
            break;
        case hir::Type::Kind::Builtin:
            out << "typedef " << qualifiers(type) << builtin(type) << ' '
                << name(id) << ";\n";
            break;
        case hir::Type::Kind::Pointer:
            if (!type.pointee) {
                diagnostics_.command_error("GIMPLE type serialization found a "
                                           "pointer without a pointee");
                break;
            }
            emit_one(*type.pointee, out);
            out << "typedef " << name(*type.pointee) << " *" << name(id)
                << ";\n";
            break;
        case hir::Type::Kind::Vector: {
            if (!type.element || type.scalable) {
                diagnostics_.command_error(
                    "GCC GIMPLE serialization requires fixed vector types");
                break;
            }
            emit_one(*type.element, out);
            const auto& element = hir_.type(*type.element);
            const auto element_name = element.kind == hir::Type::Kind::Builtin
                                          ? builtin(element)
                                          : name(*type.element);
            out << "typedef " << element_name << ' ' << name(id)
                << " __attribute__((vector_size(" << bytes(id) << ")));\n"
                << "typedef " << name(id) << ' ' << mask_name(id)
                << " __attribute__((vector_mask));\n";
            if (is_integer(*type.element)) {
                for (const auto domain :
                     {IntegerDomain::Signed, IntegerDomain::Unsigned}) {
                    out << "typedef "
                        << integer_domain_name(*type.element, domain) << ' '
                        << integer_domain_name(id, domain)
                        << " __attribute__((vector_size(" << bytes(id)
                        << ")));\n"
                        << "typedef " << integer_domain_name(id, domain) << ' '
                        << integer_domain_mask_name(id, domain)
                        << " __attribute__((vector_mask));\n";
                }
            }
            break;
        }
        case hir::Type::Kind::Array:
            if (!type.element) {
                diagnostics_.command_error("GIMPLE type serialization found an "
                                           "array without an element");
                break;
            }
            emit_one(*type.element, out);
            out << "typedef " << name(*type.element) << ' ' << name(id) << '['
                << type.lanes << "];\n";
            break;
        case hir::Type::Kind::Record:
            if (!type.record) {
                diagnostics_.command_error("GIMPLE type serialization found a "
                                           "record without an identity");
                break;
            }
            out << "typedef struct cross_record_" << type.record->value << ' '
                << name(id) << ";\n";
            break;
        }
        active_[id.value] = false;
        emitted_[id.value] = true;
    }

    const hir::Module& hir_;
    Diagnostics& diagnostics_;
    std::vector<bool> emitted_;
    std::vector<bool> active_;
    std::vector<bool> required_;
};

class FunctionEmitter {
public:
    struct CopyoutTemporary {
        mir::BlockId block;
        std::size_t parameter{};
        unsigned version{};
    };

    struct IntegerDomainTemporary {
        IntegerDomain domain{IntegerDomain::None};
        unsigned left_version{};
        std::optional<unsigned> right_version;
        std::optional<unsigned> result_version;
    };

    FunctionEmitter(const hir::Module& hir_module,
                    const mir::ManagedFunction& function,
                    const CompilerOptions&, Diagnostics& diagnostics,
                    TypeEmitter& types, GimpleStart start)
        : hir_(hir_module), function_(function), diagnostics_(diagnostics),
          types_(types), start_(start),
          entity_(hir_module.function(function.source)),
          auxiliary_(function.values.size()),
          integer_temporaries_(function.values.size()),
          next_version_(static_cast<unsigned>(function.values.size() + 1)) {
        for (const auto& value : function_.values) {
            if ((value.kind == mir::ValueKind::Binary &&
                 comparison(value.binary)) ||
                value.kind == mir::ValueKind::Select ||
                value.kind == mir::ValueKind::IndexedAddress) {
                auxiliary_[value.id.value] = next_version_++;
            }
            if (value.kind != mir::ValueKind::Binary ||
                value.operands.size() != 2) {
                continue;
            }
            const auto domain = integer_domain(value.binary);
            const auto operand_type =
                function_.values.at(value.operands.front().value).type;
            if (domain == IntegerDomain::None ||
                !types_.is_integer(operand_type)) {
                continue;
            }
            IntegerDomainTemporary temporary;
            temporary.domain = domain;
            temporary.left_version = next_version_++;
            if (domain_uses_right_operand(value.binary)) {
                temporary.right_version = next_version_++;
            }
            if (!comparison(value.binary)) {
                temporary.result_version = next_version_++;
            }
            integer_temporaries_[value.id.value] = temporary;
        }
        for (const auto& block : function_.blocks) {
            if (block.terminator.kind != mir::TerminatorKind::Return) continue;
            for (std::size_t index = 0; index < entity_.parameters.size();
                 ++index) {
                if (entity_.parameters[index].mode == ParameterMode::In)
                    continue;
                copyouts_.push_back({block.id, index, next_version_++});
            }
        }
    }

    std::string run() {
        out_ << (entity_.linkage == Linkage::Global ? "" : "static ")
             << types_.name(entity_.result_type) << " __GIMPLE (ssa";
        if (start_ == GimpleStart::Rtl) {
            out_ << ", startwith(\"optimized\")";
        }
        out_ << ") " << function_identifier(entity_.id) << '(';
        emit_parameters(out_, true);
        out_ << ")\n{\n";
        emit_declarations();
        for (const auto& block : function_.blocks)
            emit_block(block);
        out_ << "}\n\n";
        return out_.str();
    }

private:
    std::string main_base(mir::ValueId id) const {
        return "v" + std::to_string(id.value);
    }

    std::string main_reference(mir::ValueId id,
                               bool default_definition = false) const {
        return main_base(id) + '_' + std::to_string(id.value + 1) +
               (default_definition ? "(D)" : "");
    }

    std::string auxiliary_base(mir::ValueId id) const {
        return "p" + std::to_string(id.value);
    }

    std::string auxiliary_reference(mir::ValueId id) const {
        return auxiliary_base(id) + '_' +
               std::to_string(auxiliary_.at(id.value));
    }

    std::string integer_temporary_base(mir::ValueId id,
                                       std::string_view role) const {
        return "cross_domain_" + std::string(role) + '_' +
               std::to_string(id.value);
    }

    std::string integer_temporary_reference(mir::ValueId id,
                                            std::string_view role,
                                            unsigned version) const {
        return integer_temporary_base(id, role) + '_' + std::to_string(version);
    }

    std::string slot_name(mir::SlotId id) const {
        return "cross_slot_" + std::to_string(id.value);
    }

    std::string copyout_base(mir::BlockId block, std::size_t parameter) const {
        return "cross_copyout_" + std::to_string(block.value) + '_' +
               std::to_string(parameter);
    }

    std::string copyout_reference(const CopyoutTemporary& temporary) const {
        return copyout_base(temporary.block, temporary.parameter) + '_' +
               std::to_string(temporary.version);
    }

    unsigned block_number(mir::BlockId id) const { return id.value + 2U; }

    std::string block_name(mir::BlockId id) const {
        return "__BB" + std::to_string(block_number(id));
    }

    std::string block_definition(mir::BlockId id) const {
        return "__BB(" + std::to_string(block_number(id)) + ')';
    }

    std::string integer_literal(const mir::ManagedValue& value) {
        if (value.integer_high != 0) {
            diagnostics_.error(
                value.location,
                "GCC GIMPLE serialization does not yet materialize a "
                "nonzero high half of an i128/u128 constant");
            return "0";
        }
        auto text = std::to_string(value.integer);
        if (value.integer > 0x7fffffffffffffffULL) text += "ULL";
        return "_Literal (" + types_.name(value.type) + ") " + text;
    }

    std::string zero(hir::TypeId type) const {
        if (!types_.is_vector(type)) {
            return "_Literal (" + types_.name(type) + ") 0";
        }
        const auto& shape = hir_.type(type);
        std::string result = "_Literal (" + types_.name(type) + ") {";
        for (std::uint32_t lane = 0; lane < shape.lanes; ++lane) {
            if (lane != 0) result += ", ";
            result += "0";
        }
        return result + '}';
    }

    std::string reference(mir::ValueId id) {
        const auto& value = function_.values.at(id.value);
        if (value.kind == mir::ValueKind::Parameter) {
            const auto& parameter =
                entity_.parameters.at(value.parameter_index);
            if (!parameter_cell(parameter)) {
                return "arg" + std::to_string(value.parameter_index) + '_' +
                       std::to_string(value.id.value + 1) + "(D)";
            }
            return main_reference(id, parameter.mode == ParameterMode::Out);
        }
        if (value.kind == mir::ValueKind::ConstantInteger) {
            return integer_literal(value);
        }
        if (value.kind == mir::ValueKind::SlotAddress && value.slot) {
            return '&' + slot_name(*value.slot);
        }
        if (value.kind == mir::ValueKind::GlobalAddress && value.object) {
            return '&' + object_identifier(*value.object);
        }
        if (value.kind == mir::ValueKind::LabelAddress) {
            diagnostics_.error(value.location,
                               "GCC's __GIMPLE parser cannot represent a Cross "
                               "local-label address");
            return "_Literal (void *) 0";
        }
        return main_reference(id);
    }

    bool needs_result(const mir::ManagedValue& value) const {
        using mir::AtomicOperation;
        using mir::IntrinsicOperation;
        using mir::ValueKind;
        if (types_.is_void(value.type)) return false;
        switch (value.kind) {
        case ValueKind::ConstantInteger:
        case ValueKind::LabelAddress:
        case ValueKind::SlotAddress:
        case ValueKind::GlobalAddress: return false;
        case ValueKind::Parameter:
            return parameter_cell(entity_.parameters.at(value.parameter_index));
        case ValueKind::Atomic:
            return value.atomic != AtomicOperation::Store &&
                   value.atomic != AtomicOperation::ThreadFence &&
                   value.atomic != AtomicOperation::SignalFence;
        case ValueKind::Intrinsic:
            return value.intrinsic == IntrinsicOperation::Expect;
        default: return true;
        }
    }

    void emit_parameters(std::ostringstream& out, bool names) const {
        for (std::size_t index = 0; index < entity_.parameters.size();
             ++index) {
            if (index != 0) out << ", ";
            const auto& parameter = entity_.parameters[index];
            out << types_.name(parameter.type);
            if (parameter_cell(parameter)) out << " *";
            if (names) out << " arg" << index;
        }
        if (entity_.variadic) {
            if (!entity_.parameters.empty()) out << ", ";
            out << "...";
        } else if (entity_.parameters.empty()) {
            out << "void";
        }
    }

    void emit_declarations() {
        for (const auto& slot : function_.slots) {
            out_ << "  ";
            if (slot.is_volatile) out_ << "volatile ";
            out_ << types_.name(slot.type) << ' ' << slot_name(slot.id)
                 << (slot.minimum_alignment > 1
                         ? " __attribute__((aligned(" +
                               std::to_string(slot.minimum_alignment) + ")))"
                         : "")
                 << ";\n";
        }
        for (const auto& value : function_.values) {
            if (needs_result(value)) {
                out_ << "  " << types_.name(value.type) << ' '
                     << main_base(value.id) << ";\n";
            }
            if (!auxiliary_.at(value.id.value)) continue;
            out_ << "  ";
            if (value.kind == mir::ValueKind::IndexedAddress) {
                out_ << "cross_gimple_size ";
            } else {
                const auto condition_type =
                    value.kind == mir::ValueKind::Select
                        ? function_.values.at(value.operands.front().value).type
                        : function_.values.at(value.operands.front().value)
                              .type;
                const auto& integer_temporary =
                    integer_temporaries_.at(value.id.value);
                out_ << (types_.is_vector(condition_type)
                             ? integer_temporary
                                   ? types_.integer_domain_mask_name(
                                         condition_type,
                                         integer_temporary->domain)
                                   : types_.mask_name(condition_type)
                             : std::string("_Bool"))
                     << ' ';
            }
            out_ << auxiliary_base(value.id) << ";\n";
        }
        for (const auto& value : function_.values) {
            const auto& temporary = integer_temporaries_.at(value.id.value);
            if (!temporary) continue;
            const auto left = value.operands.at(0);
            const auto right = value.operands.at(1);
            out_ << "  "
                 << types_.integer_domain_name(
                        function_.values.at(left.value).type, temporary->domain)
                 << ' ' << integer_temporary_base(value.id, "left") << ";\n";
            if (temporary->right_version) {
                out_ << "  "
                     << types_.integer_domain_name(
                            function_.values.at(right.value).type,
                            temporary->domain)
                     << ' ' << integer_temporary_base(value.id, "right")
                     << ";\n";
            }
            if (temporary->result_version) {
                out_ << "  "
                     << types_.integer_domain_name(value.type,
                                                   temporary->domain)
                     << ' ' << integer_temporary_base(value.id, "result")
                     << ";\n";
            }
        }
        for (const auto& temporary : copyouts_) {
            out_ << "  "
                 << types_.name(entity_.parameters[temporary.parameter].type)
                 << ' ' << copyout_base(temporary.block, temporary.parameter)
                 << ";\n";
        }
        out_ << '\n';
    }

    std::string memory_reference(hir::TypeId type, std::string address,
                                 unsigned alignment, bool is_volatile) const {
        std::string result = "__MEM <";
        if (is_volatile) result += "volatile ";
        result += types_.name(type);
        if (alignment != 0) {
            result += ", " + std::to_string(alignment * 8U);
        }
        result += "> (" + std::move(address) + ')';
        return result;
    }

    std::string indexed_address(const mir::ManagedValue& value) {
        const auto base = value.operands.at(0);
        const auto index = value.operands.at(1);
        const auto& pointer = hir_.type(function_.values.at(base.value).type);
        const auto element = pointer.pointee.value_or(value.type);
        return reference(base) + " + " + reference(index) +
               " * _Literal (cross_gimple_size) " +
               std::to_string(types_.bytes(element));
    }

    void emit_phi(const mir::ManagedValue& value) {
        out_ << "  " << reference(value.id) << " = __PHI (";
        for (std::size_t index = 0; index < value.incoming.size(); ++index) {
            if (index != 0) out_ << ", ";
            out_ << block_name(value.incoming[index].predecessor) << ": "
                 << reference(value.incoming[index].value);
        }
        out_ << ");\n";
    }

    void emit_floating_constant(const mir::ManagedValue& value) {
        const auto bits = types_.bits(value.type);
        if (bits != 32 && bits != 64) {
            diagnostics_.error(
                value.location,
                "GCC GIMPLE serialization currently supports exact "
                "f32/f64 constants only");
            return;
        }
        out_ << "  " << reference(value.id) << " = __VIEW_CONVERT <"
             << types_.name(value.type) << ">(_Literal ("
             << (bits == 32 ? "unsigned int" : "unsigned long long") << ") "
             << value.integer << (bits == 64 ? "ULL" : "U") << ");\n";
    }

    std::optional<std::uint64_t> constant_index(mir::ValueId id) const {
        const auto& value = function_.values.at(id.value);
        if (value.kind != mir::ValueKind::ConstantInteger ||
            value.integer_high != 0) {
            return std::nullopt;
        }
        return value.integer;
    }

    void emit_atomic(const mir::ManagedValue& value) {
        using mir::AtomicOperation;
        if (value.atomic == AtomicOperation::ThreadFence ||
            value.atomic == AtomicOperation::SignalFence) {
            out_ << "  "
                 << (value.atomic == AtomicOperation::ThreadFence
                         ? "__atomic_thread_fence"
                         : "__atomic_signal_fence")
                 << " (" << memory_order(value.memory_order) << ");\n";
            return;
        }
        const auto address = reference(value.operands.at(0));
        if (value.atomic == AtomicOperation::Load) {
            out_ << "  " << reference(value.id) << " = __atomic_load_n ("
                 << address << ", " << memory_order(value.memory_order)
                 << ");\n";
            return;
        }
        if (value.atomic == AtomicOperation::Store) {
            out_ << "  __atomic_store_n (" << address << ", "
                 << reference(value.operands.at(1)) << ", "
                 << memory_order(value.memory_order) << ");\n";
            return;
        }
        if (value.atomic == AtomicOperation::CompareExchange) {
            out_ << "  " << reference(value.id)
                 << " = __atomic_compare_exchange_n (" << address << ", "
                 << reference(value.operands.at(1)) << ", "
                 << reference(value.operands.at(2)) << ", 0, "
                 << memory_order(value.memory_order) << ", "
                 << memory_order(value.failure_order) << ");\n";
            return;
        }
        std::string builtin;
        switch (value.atomic) {
        case AtomicOperation::Exchange: builtin = "__atomic_exchange_n"; break;
        case AtomicOperation::FetchAdd: builtin = "__atomic_fetch_add"; break;
        case AtomicOperation::FetchSub: builtin = "__atomic_fetch_sub"; break;
        case AtomicOperation::FetchAnd: builtin = "__atomic_fetch_and"; break;
        case AtomicOperation::FetchXor: builtin = "__atomic_fetch_xor"; break;
        case AtomicOperation::FetchOr: builtin = "__atomic_fetch_or"; break;
        case AtomicOperation::FetchUpdate:
            switch (value.binary) {
            case mir::BinaryOperation::Add:
                builtin = "__atomic_fetch_add";
                break;
            case mir::BinaryOperation::Subtract:
                builtin = "__atomic_fetch_sub";
                break;
            case mir::BinaryOperation::BitAnd:
                builtin = "__atomic_fetch_and";
                break;
            case mir::BinaryOperation::BitXor:
                builtin = "__atomic_fetch_xor";
                break;
            case mir::BinaryOperation::BitOr:
                builtin = "__atomic_fetch_or";
                break;
            default:
                diagnostics_.error(value.location,
                                   "GCC GIMPLE serialization cannot express "
                                   "this general atomic "
                                   "fetch-update without changing the MIR CFG");
                return;
            }
            break;
        default: break;
        }
        out_ << "  " << reference(value.id) << " = " << builtin << " ("
             << address << ", " << reference(value.operands.at(1)) << ", "
             << memory_order(value.memory_order) << ");\n";
    }

    void emit_value(const mir::ManagedValue& value) {
        using mir::ValueKind;
        if (value.kind == ValueKind::Parameter) {
            const auto& parameter =
                entity_.parameters.at(value.parameter_index);
            if (!parameter_cell(parameter) ||
                parameter.mode == ParameterMode::Out) {
                return;
            }
            const auto& type = hir_.type(parameter.type);
            out_ << "  " << reference(value.id) << " = "
                 << memory_reference(parameter.type,
                                     "arg" +
                                         std::to_string(value.parameter_index),
                                     0, type.is_volatile)
                 << ";\n";
            return;
        }
        if (value.kind == ValueKind::ConstantInteger ||
            value.kind == ValueKind::LabelAddress ||
            value.kind == ValueKind::SlotAddress ||
            value.kind == ValueKind::GlobalAddress ||
            value.kind == ValueKind::Phi) {
            return;
        }
        if (value.kind == ValueKind::ConstantFloating) {
            emit_floating_constant(value);
            return;
        }
        if (value.kind == ValueKind::IndexedAddress) {
            const auto base = value.operands.at(0);
            const auto index = value.operands.at(1);
            const auto& pointer =
                hir_.type(function_.values.at(base.value).type);
            const auto element = pointer.pointee.value_or(value.type);
            out_ << "  " << auxiliary_reference(value.id) << " = "
                 << reference(index) << " * _Literal (cross_gimple_size) "
                 << types_.bytes(element) << ";\n"
                 << "  " << reference(value.id) << " = " << reference(base)
                 << " + " << auxiliary_reference(value.id) << ";\n";
            return;
        }
        if (value.kind == ValueKind::VariadicState) {
            diagnostics_.error(
                value.location,
                "GCC GIMPLE serialization does not yet model a Cross "
                "variadic ABI cursor");
            return;
        }
        if (value.kind == ValueKind::DynamicStackSave ||
            value.kind == ValueKind::DynamicAlloca ||
            value.kind == ValueKind::DynamicStackRestore) {
            diagnostics_.error(
                value.location,
                "GCC GIMPLE serialization does not yet model dynamic "
                "Cross stack scopes");
            return;
        }
        if (value.kind == ValueKind::Splat) {
            const auto source = reference(value.operands.front());
            const auto& shape = hir_.type(value.type);
            out_ << "  " << reference(value.id) << " = _Literal ("
                 << types_.name(value.type) << ") {";
            for (std::uint32_t lane = 0; lane < shape.lanes; ++lane) {
                if (lane != 0) out_ << ", ";
                out_ << source;
            }
            out_ << "};\n";
            return;
        }
        if (value.kind == ValueKind::ExtractElement) {
            const auto index = constant_index(value.operands.at(1));
            const auto& vector =
                hir_.type(function_.values.at(value.operands.at(0).value).type);
            if (!index || !vector.element || *index >= vector.lanes) {
                diagnostics_.error(
                    value.location,
                    "GCC GIMPLE serialization requires a constant "
                    "in-range vector extraction index");
                return;
            }
            const auto element_bits = types_.bits(*vector.element);
            out_ << "  " << reference(value.id) << " = __BIT_FIELD_REF <"
                 << types_.name(value.type) << "> ("
                 << reference(value.operands.at(0)) << ", " << element_bits
                 << ", " << (*index * element_bits) << ");\n";
            return;
        }
        if (value.kind == ValueKind::InsertElement) {
            const auto index = constant_index(value.operands.at(1));
            const auto& vector = hir_.type(value.type);
            if (!index || !vector.element || *index >= vector.lanes) {
                diagnostics_.error(
                    value.location,
                    "GCC GIMPLE serialization requires a constant "
                    "in-range vector insertion index");
                return;
            }
            out_ << "  " << reference(value.id) << " = __BIT_INSERT ("
                 << reference(value.operands.at(0)) << ", "
                 << reference(value.operands.at(2)) << ", "
                 << (*index * types_.bits(*vector.element)) << ");\n";
            return;
        }
        if (value.kind == ValueKind::LifetimeStart ||
            value.kind == ValueKind::LifetimeEnd) {
            return;
        }
        if (value.kind == ValueKind::Load) {
            const auto& slot = function_.slots.at(value.slot->value);
            out_ << "  " << reference(value.id) << " = "
                 << memory_reference(value.type, '&' + slot_name(*value.slot),
                                     0, slot.is_volatile)
                 << ";\n";
            return;
        }
        if (value.kind == ValueKind::Store) {
            const auto& slot = function_.slots.at(value.slot->value);
            const auto source = value.operands.front();
            out_ << "  "
                 << memory_reference(function_.values.at(source.value).type,
                                     '&' + slot_name(*value.slot), 0,
                                     slot.is_volatile)
                 << " = " << reference(source) << ";\n";
            return;
        }
        if (value.kind == ValueKind::PointerLoad) {
            out_ << "  " << reference(value.id) << " = "
                 << memory_reference(
                        value.type, reference(value.operands.front()),
                        value.memory_alignment, value.is_volatile_access)
                 << ";\n";
            return;
        }
        if (value.kind == ValueKind::PointerStore) {
            const auto source = value.operands.at(1);
            out_ << "  "
                 << memory_reference(function_.values.at(source.value).type,
                                     reference(value.operands.at(0)),
                                     value.memory_alignment,
                                     value.is_volatile_access)
                 << " = " << reference(source) << ";\n";
            return;
        }
        if (value.kind == ValueKind::IndexedLoad) {
            out_ << "  " << reference(value.id) << " = "
                 << memory_reference(value.type, indexed_address(value),
                                     value.memory_alignment,
                                     value.is_volatile_access)
                 << ";\n";
            return;
        }
        if (value.kind == ValueKind::GlobalLoad) {
            out_ << "  " << reference(value.id) << " = "
                 << memory_reference(value.type,
                                     '&' + object_identifier(*value.object), 0,
                                     value.is_volatile_access)
                 << ";\n";
            return;
        }
        if (value.kind == ValueKind::GlobalStore) {
            const auto source = value.operands.front();
            out_ << "  "
                 << memory_reference(function_.values.at(source.value).type,
                                     '&' + object_identifier(*value.object), 0,
                                     value.is_volatile_access)
                 << " = " << reference(source) << ";\n";
            return;
        }
        if (value.kind == ValueKind::Atomic) {
            emit_atomic(value);
            return;
        }
        if (value.kind == ValueKind::Call) {
            if (!value.callee) {
                diagnostics_.error(
                    value.location,
                    "GCC GIMPLE serialization of indirect calls is "
                    "not implemented");
                return;
            }
            out_ << "  ";
            if (!types_.is_void(value.type)) {
                out_ << reference(value.id) << " = ";
            }
            out_ << function_identifier(*value.callee) << " (";
            for (std::size_t index = 0; index < value.call_arguments.size();
                 ++index) {
                if (index != 0) out_ << ", ";
                const auto& argument = value.call_arguments[index];
                if (argument.value) {
                    out_ << reference(*argument.value);
                } else {
                    out_ << '&' << slot_name(*argument.cell);
                }
            }
            out_ << ");\n";
            return;
        }
        if (value.kind == ValueKind::PatchValue) {
            diagnostics_.error(
                value.location,
                "GCC's __GIMPLE parser has no inline-assembly "
                "statement for preserving $::patch materialization");
            return;
        }
        if (value.kind == ValueKind::Intrinsic) {
            switch (value.intrinsic) {
            case mir::IntrinsicOperation::Expect:
                out_ << "  " << reference(value.id) << " = __builtin_expect ("
                     << reference(value.operands.front()) << ", "
                     << value.integer << ");\n";
                break;
            case mir::IntrinsicOperation::Assume:
                // Cross assumptions are intentionally unevaluated and carry
                // no runtime operand in MIR.  Its facts have already been
                // consumed by the Cross middle end.
                break;
            case mir::IntrinsicOperation::Unreachable:
            case mir::IntrinsicOperation::Trap: break;
            }
            return;
        }
        if (value.kind == ValueKind::Select) {
            const auto condition = value.operands.at(0);
            const auto condition_type =
                function_.values.at(condition.value).type;
            out_ << "  " << auxiliary_reference(value.id) << " = "
                 << reference(condition) << " != " << zero(condition_type)
                 << ";\n"
                 << "  " << reference(value.id) << " = "
                 << auxiliary_reference(value.id) << " ? "
                 << reference(value.operands.at(1)) << " : "
                 << reference(value.operands.at(2)) << ";\n";
            return;
        }
        if (value.kind == ValueKind::Unary) {
            const auto operand = value.operands.front();
            out_ << "  " << reference(value.id) << " = ";
            if (value.unary == mir::UnaryOperation::IsZero) {
                out_ << reference(operand)
                     << " == " << zero(function_.values.at(operand.value).type);
            } else {
                out_ << (value.unary == mir::UnaryOperation::Negate ? "-" : "~")
                     << reference(operand);
            }
            out_ << ";\n";
            return;
        }
        if (value.kind == ValueKind::Cast) {
            const auto operand = value.operands.front();
            out_ << "  " << reference(value.id) << " = ";
            if (value.cast == mir::CastOperation::Reinterpret) {
                out_ << "__VIEW_CONVERT <" << types_.name(value.type) << ">("
                     << reference(operand) << ')';
            } else {
                out_ << '(' << types_.name(value.type) << ") "
                     << reference(operand);
            }
            out_ << ";\n";
            return;
        }
        if (value.kind == ValueKind::Binary) {
            const auto left = value.operands.at(0);
            const auto right = value.operands.at(1);
            const auto operand_type = function_.values.at(left.value).type;
            if (value.binary == mir::BinaryOperation::RotateLeft ||
                value.binary == mir::BinaryOperation::RotateRight) {
                out_ << "  " << reference(value.id) << " = cross_gimple_"
                     << (value.binary == mir::BinaryOperation::RotateLeft
                             ? "rotl_" : "rotr_")
                     << operand_type.value << " (" << reference(left)
                     << ", " << reference(right) << ");\n";
                return;
            }
            const auto& integer_temporary =
                integer_temporaries_.at(value.id.value);
            if (integer_temporary) {
                const auto domain_type = types_.integer_domain_name(
                    operand_type, integer_temporary->domain);
                const auto left_reference = integer_temporary_reference(
                    value.id, "left", integer_temporary->left_version);
                out_ << "  " << left_reference << " = __VIEW_CONVERT <"
                     << domain_type << ">(" << reference(left) << ");\n";
                std::string right_reference = reference(right);
                if (integer_temporary->right_version) {
                    right_reference = integer_temporary_reference(
                        value.id, "right", *integer_temporary->right_version);
                    out_ << "  " << right_reference << " = __VIEW_CONVERT <"
                         << types_.integer_domain_name(
                                function_.values.at(right.value).type,
                                integer_temporary->domain)
                         << ">(" << reference(right) << ");\n";
                }
                if (comparison(value.binary)) {
                    out_ << "  " << auxiliary_reference(value.id) << " = "
                         << left_reference << ' '
                         << binary_operator(value.binary) << ' '
                         << right_reference << ";\n"
                         << "  " << reference(value.id) << " = ";
                    if (types_.is_vector(operand_type)) {
                        out_ << "__VIEW_CONVERT <" << types_.name(value.type)
                             << ">(" << auxiliary_reference(value.id) << ')';
                    } else {
                        out_ << '(' << types_.name(value.type) << ") "
                             << auxiliary_reference(value.id);
                    }
                    out_ << ";\n";
                    return;
                }
                const auto result_reference = integer_temporary_reference(
                    value.id, "result", *integer_temporary->result_version);
                out_ << "  " << result_reference << " = " << left_reference
                     << ' ' << binary_operator(value.binary) << ' '
                     << right_reference << ";\n"
                     << "  " << reference(value.id) << " = __VIEW_CONVERT <"
                     << types_.name(value.type) << ">(" << result_reference
                     << ");\n";
                return;
            }
            const bool floating_divide =
                types_.is_floating(operand_type) &&
                (value.binary == mir::BinaryOperation::SignedDivide ||
                 value.binary == mir::BinaryOperation::UnsignedDivide);
            if (floating_divide) {
                // GCC's experimental GIMPLE parser maps '/' to
                // TRUNC_DIV_EXPR even for floating modes.  A normal-C
                // always-inline shim enters GCC as RDIV_EXPR and is inlined
                // before both requested start points.
                out_ << "  " << reference(value.id) << " = "
                     << "cross_gimple_fdiv_" << operand_type.value << " ("
                     << reference(left) << ", " << reference(right) << ");\n";
                return;
            }
            if (!comparison(value.binary)) {
                out_ << "  " << reference(value.id) << " = " << reference(left)
                     << ' ' << binary_operator(value.binary) << ' '
                     << reference(right) << ";\n";
                return;
            }
            out_ << "  " << auxiliary_reference(value.id) << " = "
                 << reference(left) << ' ' << binary_operator(value.binary)
                 << ' ' << reference(right) << ";\n"
                 << "  " << reference(value.id) << " = ";
            if (types_.is_vector(operand_type)) {
                out_ << "__VIEW_CONVERT <" << types_.name(value.type) << ">("
                     << auxiliary_reference(value.id) << ')';
            } else {
                out_ << '(' << types_.name(value.type) << ") "
                     << auxiliary_reference(value.id);
            }
            out_ << ";\n";
            return;
        }
    }

    void emit_parameter_copyout(mir::BlockId block) {
        for (std::size_t index = 0; index < entity_.parameters.size();
             ++index) {
            const auto& parameter = entity_.parameters[index];
            if (parameter.mode == ParameterMode::In) continue;
            const auto expected_name = "$param." + std::to_string(index);
            const auto slot =
                std::find_if(function_.slots.begin(), function_.slots.end(),
                             [&](const mir::ManagedSlot& candidate) {
                                 return candidate.name == expected_name;
                             });
            if (slot == function_.slots.end()) {
                diagnostics_.error(parameter.location,
                                   "managed output parameter has no cell");
                continue;
            }
            const auto temporary =
                std::find_if(copyouts_.begin(), copyouts_.end(),
                             [&](const CopyoutTemporary& candidate) {
                                 return candidate.block == block &&
                                        candidate.parameter == index;
                             });
            if (temporary == copyouts_.end()) {
                diagnostics_.error(
                    parameter.location,
                    "managed output parameter has no GIMPLE copyout temporary");
                continue;
            }
            const auto& type = hir_.type(parameter.type);
            out_ << "  " << copyout_reference(*temporary) << " = "
                 << memory_reference(parameter.type, '&' + slot_name(slot->id),
                                     0, slot->is_volatile)
                 << ";\n  "
                 << memory_reference(parameter.type,
                                     "arg" + std::to_string(index), 0,
                                     type.is_volatile)
                 << " = " << copyout_reference(*temporary) << ";\n";
        }
    }

    void emit_block(const mir::ManagedBlock& block) {
        out_ << '\n' << block_definition(block.id) << ":\n";
        for (const auto id : block.values) {
            const auto& value = function_.values.at(id.value);
            if (value.kind == mir::ValueKind::Phi) emit_phi(value);
        }
        for (const auto id : block.values) {
            emit_value(function_.values.at(id.value));
        }
        const auto& terminator = block.terminator;
        switch (terminator.kind) {
        case mir::TerminatorKind::Return:
            emit_parameter_copyout(block.id);
            out_ << "  return";
            if (terminator.value) out_ << ' ' << reference(*terminator.value);
            out_ << ";\n";
            break;
        case mir::TerminatorKind::Branch:
            out_ << "  goto " << block_name(terminator.successors.front())
                 << ";\n";
            break;
        case mir::TerminatorKind::ConditionalBranch: {
            const auto condition = *terminator.value;
            out_ << "  if (" << reference(condition)
                 << " != " << zero(function_.values.at(condition.value).type)
                 << ")\n"
                 << "    goto " << block_name(terminator.successors.at(0))
                 << ";\n  else\n    goto "
                 << block_name(terminator.successors.at(1)) << ";\n";
            break;
        }
        case mir::TerminatorKind::IndirectBranch:
            diagnostics_.error(
                terminator.location,
                "GCC's __GIMPLE parser cannot represent an indirect goto");
            break;
        case mir::TerminatorKind::Unreachable:
            out_ << "  __builtin_unreachable ();\n";
            break;
        case mir::TerminatorKind::Trap: out_ << "  __builtin_trap ();\n"; break;
        case mir::TerminatorKind::None:
            diagnostics_.error(
                terminator.location,
                "GIMPLE serializer found an unterminated MIR block");
            break;
        }
    }

    const hir::Module& hir_;
    const mir::ManagedFunction& function_;
    Diagnostics& diagnostics_;
    TypeEmitter& types_;
    GimpleStart start_;
    const hir::Function& entity_;
    std::vector<unsigned> auxiliary_;
    std::vector<std::optional<IntegerDomainTemporary>> integer_temporaries_;
    unsigned next_version_;
    std::vector<CopyoutTemporary> copyouts_;
    std::ostringstream out_;
};

class ModuleEmitter {
public:
    ModuleEmitter(const CompilerOptions& options, Diagnostics& diagnostics,
                  const codegen::ModuleView& module, GimpleStart start)
        : options_(options), diagnostics_(diagnostics), hir_(module.hir()),
          data_(module.data()), managed_(module.managed()),
          raw_assembly_(module.raw_assembly()), types_(hir_, diagnostics),
          start_(start) {}

    std::string run() {
        std::ostringstream out;
        out << "/* Cross language 0.8: experimental GCC __GIMPLE bridge.\n"
               "   Compile this file with GCC -fgimple. */\n\n";
        collect_required_types();
        types_.emit(out);
        emit_rotate_helpers(out);
        emit_floating_division_helpers(out);
        emit_function_declarations(out);
        emit_object_declarations(out);
        emit_object_definitions(out);
        if (!raw_assembly_.module_assembly.empty()) {
            out << "__asm__(" << c_string(raw_assembly_.module_assembly)
                << ");\n\n";
        }
        for (const auto& function : hir_.functions) {
            if (!function.definition || raw_assembly_.owns(function.id)) {
                continue;
            }
            const auto* body = managed_.find(function.id);
            if (!body) {
                diagnostics_.error(
                    function.location,
                    "GIMPLE serializer received a function without "
                    "managed MIR ownership");
                continue;
            }
            out << FunctionEmitter(hir_, *body, options_, diagnostics_, types_,
                                   start_)
                       .run();
        }
        return out.str();
    }

private:
    void collect_required_types() {
        for (const auto& function : hir_.functions) {
            types_.require(function.result_type);
            for (const auto& parameter : function.parameters) {
                types_.require(parameter.type);
            }
            for (const auto& binding : function.variadic_bindings) {
                types_.require(binding.type);
            }
        }
        for (const auto& object : data_.objects) {
            types_.require(object.type);
        }
        for (const auto& function : managed_.functions) {
            types_.require(function.result_type);
            for (const auto& slot : function.slots) {
                types_.require(slot.type);
            }
            for (const auto& value : function.values) {
                types_.require(value.type);
                for (const auto& argument : value.call_arguments) {
                    types_.require(argument.type);
                }
            }
        }
    }

    void emit_rotate_helpers(std::ostringstream& out) const {
        struct UsedType {
            hir::TypeId type;
            bool left{};
            bool right{};
        };
        std::vector<UsedType> used;
        for (const auto& function : managed_.functions) {
            for (const auto& value : function.values) {
                if (value.kind != mir::ValueKind::Binary ||
                    (value.binary != mir::BinaryOperation::RotateLeft &&
                     value.binary != mir::BinaryOperation::RotateRight) ||
                    value.operands.empty()) {
                    continue;
                }
                const auto type =
                    function.values.at(value.operands.front().value).type;
                auto found = std::find_if(
                    used.begin(), used.end(), [&](const UsedType& candidate) {
                        return candidate.type == type;
                    });
                if (found == used.end()) {
                    used.push_back({type});
                    found = used.end() - 1;
                }
                if (value.binary == mir::BinaryOperation::RotateLeft) {
                    found->left = true;
                } else {
                    found->right = true;
                }
            }
        }
        std::sort(used.begin(), used.end(),
                  [](const UsedType& left, const UsedType& right) {
                      return left.type.value < right.type.value;
                  });
        for (const auto& entry : used) {
            const auto type = types_.name(entry.type);
            const auto domain = types_.integer_domain_name(
                entry.type, IntegerDomain::Unsigned);
            const auto bits = types_.bits(entry.type);
            const auto emit = [&](std::string_view name, bool left) {
                out << "static __attribute__((always_inline, artificial)) "
                       "inline "
                    << type << " cross_gimple_" << name << '_'
                    << entry.type.value << '(' << type << " value, " << type
                    << " amount) { " << domain << " bits_value = ("
                    << domain << ") value; " << domain << " count = ("
                    << domain << ") amount & " << (bits - 1U) << "; return ("
                    << type << ") ((bits_value " << (left ? "<<" : ">>")
                    << " count) | (bits_value " << (left ? ">>" : "<<")
                    << " ((0 - count) & " << (bits - 1U) << "))); }\n";
            };
            if (entry.left) emit("rotl", true);
            if (entry.right) emit("rotr", false);
        }
        if (!used.empty()) out << '\n';
    }

    void emit_floating_division_helpers(std::ostringstream& out) const {
        std::vector<hir::TypeId> types;
        for (const auto& function : managed_.functions) {
            for (const auto& value : function.values) {
                if (value.kind != mir::ValueKind::Binary ||
                    (value.binary != mir::BinaryOperation::SignedDivide &&
                     value.binary != mir::BinaryOperation::UnsignedDivide) ||
                    value.operands.empty()) {
                    continue;
                }
                const auto type =
                    function.values.at(value.operands.front().value).type;
                if (!types_.is_floating(type) ||
                    std::find(types.begin(), types.end(), type) !=
                        types.end()) {
                    continue;
                }
                types.push_back(type);
            }
        }
        std::sort(types.begin(), types.end(),
                  [](hir::TypeId left, hir::TypeId right) {
                      return left.value < right.value;
                  });
        for (const auto type : types) {
            out << "static __attribute__((always_inline, artificial)) inline "
                << types_.name(type) << " cross_gimple_fdiv_" << type.value
                << '(' << types_.name(type) << " left, " << types_.name(type)
                << " right) { return left / right; }\n";
        }
        if (!types.empty()) out << '\n';
    }

    const AbiEntry* abi(const hir::Function& function) const {
        const auto* target = target_for_triple(options_.target);
        const auto* result = target ? find_abi(*target, function.abi) : nullptr;
        if (!result) {
            diagnostics_.error(function.location,
                               "GIMPLE serializer has no selected ABI model");
        }
        return result;
    }

    std::string function_attributes(const hir::Function& function) const {
        std::vector<std::string> attributes;
        if (const auto* entry = abi(function);
            entry && !entry->gcc_calling_attribute.empty()) {
            attributes.push_back(entry->gcc_calling_attribute);
        }
        if (function.section) {
            attributes.push_back("section(" + c_string(*function.section) +
                                 ')');
        }
        constexpr std::string_view markers[] = {
            "always_inline", "cold", "hot",    "noinline",           "noreturn",
            "returns_twice", "used", "retain", "no_stack_protector",
        };
        for (const auto marker : markers) {
            if (has_attribute(function, marker)) {
                attributes.emplace_back(marker);
            }
        }
        if (attributes.empty()) return {};
        std::string result = " __attribute__((";
        for (std::size_t index = 0; index < attributes.size(); ++index) {
            if (index != 0) result += ", ";
            result += attributes[index];
        }
        return result + "))";
    }

    void emit_function_parameters(std::ostringstream& out,
                                  const hir::Function& function) const {
        for (std::size_t index = 0; index < function.parameters.size();
             ++index) {
            if (index != 0) out << ", ";
            const auto& parameter = function.parameters[index];
            out << types_.name(parameter.type);
            if (parameter_cell(parameter)) out << " *";
            out << " arg" << index;
        }
        if (function.variadic) {
            if (!function.parameters.empty()) out << ", ";
            out << "...";
        } else if (function.parameters.empty()) {
            out << "void";
        }
    }

    void emit_function_declarations(std::ostringstream& out) const {
        for (const auto& function : hir_.functions) {
            const bool internal_definition =
                function.linkage != Linkage::Global && function.definition &&
                !raw_assembly_.owns(function.id);
            out << (internal_definition ? "static " : "extern ")
                << types_.name(function.result_type) << ' '
                << function_identifier(function.id) << '(';
            emit_function_parameters(out, function);
            out << ") __asm__(" << c_string(function.link_symbol) << ')'
                << function_attributes(function) << ";\n";
        }
        if (!hir_.functions.empty()) out << '\n';
    }

    std::string object_prefix(const data::Object& object,
                              const hir::Object& entity,
                              bool declaration) const {
        std::string result;
        if (declaration) {
            result += entity.linkage == Linkage::Global ? "extern " : "static ";
        } else if (entity.linkage != Linkage::Global) {
            result += "static ";
        }
        if (object.is_thread_local) result += "__thread ";
        if (object.read_only) result += "const ";
        return result;
    }

    std::string object_attributes(const data::Object& object,
                                  const hir::Object& entity) const {
        std::vector<std::string> attributes;
        if (entity.section) {
            attributes.push_back("section(" + c_string(*entity.section) + ')');
        } else if (object.initializer == data::InitializerKind::Uninitialized) {
            attributes.emplace_back("section(\".noinit\")");
        }
        attributes.push_back("aligned(" + std::to_string(object.alignment) +
                             ')');
        if (object.used) attributes.emplace_back("used");
        if (object.retain) attributes.emplace_back("retain");
        if (object.is_thread_local) {
            const auto model =
                object.tls_model.empty()
                    ? (entity.linkage == Linkage::Global ? "initial-exec"
                                                         : "local-exec")
                    : object.tls_model;
            attributes.push_back("tls_model(" + c_string(model) + ')');
        }
        std::string result = " __attribute__((";
        for (std::size_t index = 0; index < attributes.size(); ++index) {
            if (index != 0) result += ", ";
            result += attributes[index];
        }
        return result + "))";
    }

    void emit_object_declarations(std::ostringstream& out) const {
        for (const auto& object : data_.objects) {
            if (raw_assembly_.owns(object.source)) continue;
            const auto& entity = hir_.object(object.source);
            out << object_prefix(object, entity, true)
                << types_.name(object.type) << ' '
                << object_identifier(object.source) << " __asm__("
                << c_string(entity.link_symbol) << ')'
                << object_attributes(object, entity) << ";\n";
        }
        if (!data_.objects.empty()) out << '\n';
    }

    std::string floating_initializer(const data::Object& object) {
        const auto& type = hir_.type(object.type);
        std::ostringstream out;
        out << std::hexfloat;
        if (type.kind == hir::Type::Kind::Builtin &&
            (type.builtin == BuiltinType::F32 ||
             (type.builtin == BuiltinType::Fptr && object.size == 4))) {
            out << std::bit_cast<float>(
                       static_cast<std::uint32_t>(object.bits.low))
                << 'f';
            return out.str();
        }
        if (type.kind == hir::Type::Kind::Builtin &&
            (type.builtin == BuiltinType::F64 ||
             (type.builtin == BuiltinType::Fptr && object.size == 8))) {
            out << std::bit_cast<double>(object.bits.low);
            return out.str();
        }
        diagnostics_.error(object.location,
                           "GCC GIMPLE serialization currently supports exact "
                           "f32/f64 static initializers only");
        return "0.0";
    }

    std::string address_initializer(const data::Object& object) {
        if (!object.address) return "0";
        const auto& address = *object.address;
        std::string target;
        if (address.kind == data::AddressKind::Object && address.object) {
            target = '&' + object_identifier(*address.object);
        } else if (address.kind == data::AddressKind::Function &&
                   address.function) {
            target = '&' + function_identifier(*address.function);
        } else if (address.kind == data::AddressKind::Label) {
            diagnostics_.error(
                object.location,
                "GCC GIMPLE serialization cannot place a local-label "
                "address in static data");
            return "0";
        } else {
            diagnostics_.error(object.location,
                               "data IR address constant has no target");
            return "0";
        }
        if (address.addend != 0) {
            target = "((unsigned char *)" + target +
                     (address.addend > 0 ? " + " : " - ") +
                     std::to_string(std::abs(address.addend)) + ')';
        }
        return '(' + types_.name(object.type) + ") " + target;
    }

    std::string initializer(const data::Object& object) {
        switch (object.initializer) {
        case data::InitializerKind::Declaration:
        case data::InitializerKind::Uninitialized: return {};
        case data::InitializerKind::Zero: return "{0}";
        case data::InitializerKind::Integer:
            if (object.bits.high != 0) {
                diagnostics_.error(object.location,
                                   "GCC GIMPLE serialization does not yet "
                                   "materialize a nonzero high "
                                   "half of an i128/u128 static initializer");
                return "0";
            }
            return '(' + types_.name(object.type) + ") " +
                   std::to_string(object.bits.low) +
                   (object.bits.low > 0x7fffffffffffffffULL ? "ULL" : "");
        case data::InitializerKind::Floating:
            return floating_initializer(object);
        case data::InitializerKind::Address: return address_initializer(object);
        case data::InitializerKind::Bytes: {
            std::string result = "{";
            for (std::size_t index = 0; index < object.bytes.size(); ++index) {
                if (index != 0) result += ", ";
                result += std::to_string(
                    static_cast<unsigned>(object.bytes[index]));
            }
            return result + '}';
        }
        }
        return "{0}";
    }

    void emit_object_definitions(std::ostringstream& out) {
        for (const auto& object : data_.objects) {
            if (raw_assembly_.owns(object.source) ||
                object.initializer == data::InitializerKind::Declaration) {
                continue;
            }
            const auto& entity = hir_.object(object.source);
            out << object_prefix(object, entity, false)
                << types_.name(object.type) << ' '
                << object_identifier(object.source)
                << object_attributes(object, entity);
            const auto value = initializer(object);
            if (!value.empty()) out << " = " << value;
            out << ";\n";
        }
        if (!data_.objects.empty()) out << '\n';
    }

    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    const hir::Module& hir_;
    const data::Module& data_;
    const mir::ManagedModule& managed_;
    const mir::AssemblyBundle& raw_assembly_;
    TypeEmitter types_;
    GimpleStart start_;
};

} // namespace

GimpleTextSerializer::GimpleTextSerializer(const CompilerOptions& options,
                                           Diagnostics& diagnostics,
                                           GimpleStart start)
    : options_(options), diagnostics_(diagnostics), start_(start) {
}

std::string GimpleTextSerializer::serialize(const codegen::ModuleView& module) {
    return ModuleEmitter(options_, diagnostics_, module, start_).run();
}

} // namespace cross::debug
