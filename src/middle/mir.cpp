// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/mir.hpp"

#include "common/continuation_task.hpp"
#include "common/floating_bits.hpp"
#include "common/floating_semantics.hpp"
#include "common/integer_semantics.hpp"
#include "common/uint128.hpp"
#include "frontend/intrinsic_constraints.hpp"
#include "middle/data_ir.hpp"
#include "middle/initializer.hpp"
#include "middle/initialization.hpp"
#include "middle/mir_analysis.hpp"
#include "middle/mir_pass.hpp"
#include "middle/patch_sink.hpp"
#include "middle/mir_transform.hpp"
#include "target/instruction_constraints.hpp"
#include "target/subtarget.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross::mir {
namespace {

unsigned type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return module.address_bits;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto size = module.record(*type.record).size;
        return size <= std::numeric_limits<unsigned>::max() / 8U
                   ? static_cast<unsigned>(size * 8U)
                   : 0;
    }
    if (type.kind == hir::Type::Kind::Vector ||
        type.kind == hir::Type::Kind::Array) {
        return (type.kind != hir::Type::Kind::Vector || !type.scalable) &&
                       type.element
                   ? type_bits(module, *type.element) * type.lanes
                   : 0;
    }
    switch (type.builtin) {
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 8;
    case BuiltinType::I16: case BuiltinType::U16: return 16;
    case BuiltinType::I32: case BuiltinType::U32: case BuiltinType::F32: return 32;
    case BuiltinType::I64: case BuiltinType::U64: case BuiltinType::F64:
        return 64;
    case BuiltinType::Iptr: case BuiltinType::Uptr: case BuiltinType::Fptr:
    case BuiltinType::Label: return module.address_bits;
    case BuiltinType::F80: return 80;
    case BuiltinType::I128: case BuiltinType::U128: case BuiltinType::F128: return 128;
    case BuiltinType::Void: return 0;
    }
    return 0;
}

std::uint64_t storage_size(const hir::Module& module, hir::TypeId id,
                           const TargetInfo& target) {
    return hir::layout_size(module, id, target).value_or(0);
}

unsigned storage_alignment(const hir::Module& module, hir::TypeId id,
                           const TargetInfo& target) {
    return static_cast<unsigned>(hir::layout_alignment(module, id, target).value_or(1));
}

// What a dereference may assume and an access requires: the alignment of the
// type without its own (typedef) request.
unsigned access_alignment(const hir::Module& module, hir::TypeId id,
                          const TargetInfo& target) {
    return static_cast<unsigned>(hir::natural_alignment(module, id, target).value_or(1));
}

bool integer_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin >= BuiltinType::Bool && type.builtin <= BuiltinType::Uptr;
}

bool unsigned_integer_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return false;
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::U8:
    case BuiltinType::U16:
    case BuiltinType::U32:
    case BuiltinType::U64:
    case BuiltinType::U128:
    case BuiltinType::Uptr: return true;
    default: return false;
    }
}

bool floating_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           (type.builtin == BuiltinType::F32 ||
            type.builtin == BuiltinType::F64 ||
            type.builtin == BuiltinType::F80 ||
            type.builtin == BuiltinType::F128 ||
            type.builtin == BuiltinType::Fptr);
}

bool pointer_type(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Pointer;
}

bool vector_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Vector && !type.scalable &&
           type.element && type.lanes != 0;
}

bool array_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Array && type.element;
}

bool vector_integer_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return vector_type(module, id) && integer_type(module, *type.element);
}

bool vector_floating_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return vector_type(module, id) && floating_type(module, *type.element);
}

bool label_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           type.builtin == BuiltinType::Label;
}

bool scalar_type(const hir::Module& module, hir::TypeId id) {
    return integer_type(module, id) || floating_type(module, id) ||
           pointer_type(module, id) || label_type(module, id);
}

bool record_value_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Record || !type.record ||
        type.is_atomic) {
        return false;
    }
    const auto& record = module.record(*type.record);
    return record.complete && record.size != 0 &&
           record.size <= std::numeric_limits<std::uint16_t>::max() / 8U;
}

bool managed_value_type(const hir::Module& module, hir::TypeId id) {
    return scalar_type(module, id) || vector_type(module, id) ||
           record_value_type(module, id);
}

bool managed_object_type(const hir::Module& module, hir::TypeId id) {
    if (managed_value_type(module, id)) return true;
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto& record = module.record(*type.record);
        return !type.is_atomic && record.complete && record.size != 0;
    }
    return array_type(module, id) && !type.is_atomic &&
           managed_object_type(module, *type.element);
}

bool atomic_object_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.is_atomic && scalar_type(module, id) &&
           !(type.kind == hir::Type::Kind::Builtin &&
             (type.builtin == BuiltinType::Void ||
              type.builtin == BuiltinType::Label));
}

// Scalar promotion's cell rule, apart from `live_on_return`.
bool promotable_scalar_slot(const ManagedSlot& slot,
                            const hir::Module& module) {
    const auto& type = module.type(slot.type);
    return !slot.is_volatile && !slot.physical_location &&
           !slot.address_taken && !type.is_atomic &&
           (type.kind == hir::Type::Kind::Builtin ||
            type.kind == hir::Type::Kind::Pointer);
}

bool atomic_intrinsic(std::string_view name) {
    return atomic_builtin(name) != AtomicBuiltin::None;
}

bool representation_compatible(const hir::Module& module, hir::TypeId left,
                               hir::TypeId right) {
    if (left == right) return true;
    const auto& lhs = module.type(left);
    const auto& rhs = module.type(right);
    return lhs.kind == rhs.kind && lhs.builtin == rhs.builtin &&
           lhs.pointee == rhs.pointee && lhs.record == rhs.record &&
           lhs.address_space == rhs.address_space &&
           lhs.function == rhs.function && lhs.element == rhs.element &&
           lhs.lanes == rhs.lanes && lhs.scalable == rhs.scalable &&
           lhs.is_atomic == rhs.is_atomic &&
           lhs.nominal_key() == rhs.nominal_key();
}

bool unqualified_representation_compatible(const hir::Module& module,
                                           hir::TypeId left,
                                           hir::TypeId right) {
    const auto& lhs = module.type(left);
    const auto& rhs = module.type(right);
    return lhs.kind == rhs.kind && lhs.builtin == rhs.builtin &&
           lhs.pointee == rhs.pointee && lhs.element == rhs.element &&
           lhs.address_space == rhs.address_space &&
           lhs.function == rhs.function && lhs.lanes == rhs.lanes &&
           lhs.scalable == rhs.scalable && lhs.nominal_key() == rhs.nominal_key();
}

bool vector_element_compatible(const hir::Module& module,
                               hir::TypeId left, hir::TypeId right) {
    if (unqualified_representation_compatible(module, left, right)) {
        return true;
    }
    // Integer arithmetic carries signedness and pointer-width intent in its
    // operation, not in the physical lane representation.  Same-width
    // integer elements can therefore share a packed expression; explicit
    // MIR casts remain in the cloned expression where the source required
    // one.  This is important for ordinary u64 expressions containing an
    // uptr induction on 64-bit targets.
    return integer_type(module, left) && integer_type(module, right) &&
           type_bits(module, left) == type_bits(module, right);
}

bool signed_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Vector && type.element) {
        return signed_type(module, *type.element);
    }
    if (type.kind != hir::Type::Kind::Builtin) return false;
    return type.builtin == BuiltinType::I8 || type.builtin == BuiltinType::I16 ||
           type.builtin == BuiltinType::I32 || type.builtin == BuiltinType::I64 ||
           type.builtin == BuiltinType::I128 || type.builtin == BuiltinType::Iptr;
}

bool void_type(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Void;
}

struct ParsedInteger {
    UInt128 value{};
    BuiltinType type{BuiltinType::I32};
};

struct ParsedFloating {
    std::uint64_t bits{};
    std::uint64_t bits_high{};
    BuiltinType type{BuiltinType::F64};
};

std::optional<ParsedFloating> parse_floating(std::string text,
                                             unsigned address_bits) {
    BuiltinType type = BuiltinType::F64;
    if (text.ends_with("f32")) {
        type = BuiltinType::F32;
        text.resize(text.size() - 3);
    } else if (text.ends_with("f64")) {
        text.resize(text.size() - 3);
    } else if (text.ends_with("fptr")) {
        type = BuiltinType::Fptr;
        text.resize(text.size() - 4);
    } else if (text.ends_with("f80")) {
        type = BuiltinType::F80;
        text.resize(text.size() - 3);
    } else if (text.ends_with("f128")) {
        type = BuiltinType::F128;
        text.resize(text.size() - 4);
    }
    const auto format = type == BuiltinType::F32 ||
        (type == BuiltinType::Fptr && address_bits <= 32)
        ? floating::Format::Binary32
        : type == BuiltinType::F80 ? floating::Format::Extended80
        : type == BuiltinType::F128 ? floating::Format::Binary128
                                    : floating::Format::Binary64;
    const auto parsed = floating::parse(std::move(text), format);
    return parsed ? std::optional<ParsedFloating>{
                        ParsedFloating{parsed->bits.low, parsed->bits.high, type}}
                  : std::nullopt;
}

std::optional<ParsedFloating> parse_floating(const Expr& expression,
                                             unsigned address_bits) {
    if (expression.evaluated_floating) {
        const auto& value = *expression.evaluated_floating;
        return ParsedFloating{value.bits.low, value.bits.high, value.type};
    }
    return parse_floating(expression.text, address_bits);
}

unsigned builtin_bits(BuiltinType type, unsigned address_bits) {
    switch (type) {
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 8;
    case BuiltinType::I16: case BuiltinType::U16: return 16;
    case BuiltinType::I32: case BuiltinType::U32:
    case BuiltinType::F32: return 32;
    case BuiltinType::I64: case BuiltinType::U64:
    case BuiltinType::F64: return 64;
    case BuiltinType::Iptr: case BuiltinType::Uptr:
    case BuiltinType::Fptr: return address_bits;
    case BuiltinType::F80: return 80;
    case BuiltinType::I128: case BuiltinType::U128: return 128;
    case BuiltinType::F128: return 128;
    default: return 0;
    }
}

bool builtin_signed(BuiltinType type) {
    return type == BuiltinType::I8 || type == BuiltinType::I16 ||
           type == BuiltinType::I32 || type == BuiltinType::I64 ||
           type == BuiltinType::I128 || type == BuiltinType::Iptr;
}

std::optional<ParsedInteger> parse_integer(std::string text,
                                           unsigned address_bits) {
    BuiltinType explicit_type = BuiltinType::I32;
    bool has_explicit = false;
    static constexpr std::pair<std::string_view, BuiltinType> suffixes[]{
        {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
        {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
        {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
        {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
        {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
        {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
    };
    for (const auto& [suffix, type] : suffixes) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.resize(text.size() - suffix.size());
            explicit_type = type;
            has_explicit = true;
            break;
        }
    }
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    unsigned base = 10;
    bool decimal = true;
    std::string_view digits(text);
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        base = 16;
        decimal = false;
        digits.remove_prefix(2);
    } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
        base = 2;
        decimal = false;
        digits.remove_prefix(2);
    } else if (digits.size() > 1 && digits.front() == '0') {
        base = 8;
        decimal = false;
        digits.remove_prefix(1);
    }
    const auto parsed = parse_uint128(digits, base);
    if (!parsed) return std::nullopt;
    const auto value = *parsed;
    if (has_explicit) {
        const auto bits = builtin_bits(explicit_type, address_bits);
        if (bits == 0 ||
            (builtin_signed(explicit_type) ? !fits_signed_positive(value, bits)
                                           : !fits_unsigned(value, bits))) {
            return std::nullopt;
        }
    } else if (decimal) {
        if (fits_signed_positive(value, 32)) {
            explicit_type = BuiltinType::I32;
        } else if (fits_signed_positive(value, 64)) {
            explicit_type = BuiltinType::I64;
        } else if (fits_signed_positive(value, 128)) {
            explicit_type = BuiltinType::I128;
        } else {
            return std::nullopt;
        }
    } else if (fits_signed_positive(value, 32)) {
        explicit_type = BuiltinType::I32;
    } else if (fits_unsigned(value, 32)) {
        explicit_type = BuiltinType::U32;
    } else if (fits_signed_positive(value, 64)) {
        explicit_type = BuiltinType::I64;
    } else if (fits_unsigned(value, 64)) {
        explicit_type = BuiltinType::U64;
    } else if (fits_signed_positive(value, 128)) {
        explicit_type = BuiltinType::I128;
    } else {
        explicit_type = BuiltinType::U128;
    }
    return ParsedInteger{value, explicit_type};
}

std::optional<ParsedInteger> patch_initial(const Expr& expression,
                                           unsigned address_bits) {
    if (expression.evaluated_integer)
        return ParsedInteger{expression.evaluated_integer->value,
                             expression.evaluated_integer->type};
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return patch_initial(*expression.left, address_bits);
    }
    if (expression.kind == Expr::Kind::Integer) {
        return parse_integer(expression.text, address_bits);
    }
    if (expression.kind != Expr::Kind::Unary || !expression.left ||
        (expression.text != "+" && expression.text != "-" &&
         expression.text != "~")) {
        return std::nullopt;
    }
    auto value = patch_initial(*expression.left, address_bits);
    if (!value) return std::nullopt;
    const auto bits = builtin_bits(value->type, address_bits);
    if (expression.text == "-") {
        value->value = mask_to(negate(value->value), bits);
    } else if (expression.text == "~") {
        value->value = mask_to(bit_not(value->value), bits);
    }
    return value;
}

bool eligible_expression(const Expr& expression) {
    switch (expression.kind) {
    case Expr::Kind::VoidValue: return true;
    case Expr::Kind::Quote: return false;
    case Expr::Kind::ByteSequence: return false;
    // Semantic expansion folds every offsetof query.
    case Expr::Kind::Offsetof: return false;
    case Expr::Kind::Address:
        return expression.type && expression.evaluated_address;
    case Expr::Kind::Integer:
    case Expr::Kind::Floating:
    case Expr::Kind::Name:
        return true;
    case Expr::Kind::Parenthesized:
        return expression.left && eligible_expression(*expression.left);
    case Expr::Kind::Cast:
        return expression.type && expression.left &&
               eligible_expression(*expression.left);
    case Expr::Kind::Sizeof:
    case Expr::Kind::Alignof:
        return expression.type ||
               (expression.left && eligible_expression(*expression.left));
    case Expr::Kind::Unary:
        return expression.left &&
               (expression.text == "+" || expression.text == "-" ||
                expression.text == "!" || expression.text == "~" ||
                expression.text == "*" || expression.text == "&" ||
                expression.text == "++" || expression.text == "--" ||
                expression.text == "post++" || expression.text == "post--") &&
               eligible_expression(*expression.left);
    case Expr::Kind::Binary:
        return expression.left && expression.right &&
               eligible_expression(*expression.left) && eligible_expression(*expression.right);
    case Expr::Kind::Conditional:
        return expression.left && expression.right && expression.third &&
               eligible_expression(*expression.left) &&
               eligible_expression(*expression.right) &&
               eligible_expression(*expression.third);
    case Expr::Kind::Assign:
        return expression.left && expression.right &&
               (expression.text == "=" || expression.text == "+=" ||
                expression.text == "-=" || expression.text == "*=" ||
                expression.text == "/=" || expression.text == "%=" ||
                expression.text == "<<=" || expression.text == ">>=" ||
                expression.text == "&=" || expression.text == "^=" ||
                expression.text == "|=") &&
               eligible_expression(*expression.left) &&
               eligible_expression(*expression.right);
    case Expr::Kind::Call:
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            return expression.left && eligible_expression(*expression.left) &&
                   std::all_of(expression.arguments.begin(),
                               expression.arguments.end(),
                               [](const auto& argument) {
                                   return eligible_expression(*argument);
                               });
        }
        if (expression.left->text == "$::patch") {
            return !expression.arguments.empty() &&
                   expression.arguments.size() <= 2 &&
                   std::all_of(expression.arguments.begin(),
                               expression.arguments.end(),
                               [](const auto& argument) {
                                   return eligible_expression(*argument);
                               });
        }
        if (expression.left->text == "$::_movabs" ||
            expression.left->text == "$::_add" ||
            expression.left->text == "$::_cmp") {
            return expression.arguments.size() == 2 &&
                   expression.arguments.front()->kind == Expr::Kind::Name &&
                   eligible_expression(*expression.arguments[1]);
        }
        if (expression.left->text == "$::alignof") {
            return expression.arguments.size() == 1 &&
                   expression.arguments.front()->kind == Expr::Kind::Name;
        }
        if (expression.left->text == "$::expect") {
            return true;
        }
        if (expression.left->text == "$::assume") {
            return true;
        }
        if (expression.left->text == "$::unreachable" ||
            expression.left->text == "$::trap") {
            return true;
        }
        if (atomic_intrinsic(expression.left->text)) {
            return std::all_of(expression.arguments.begin(),
                               expression.arguments.end(),
                               [](const auto& argument) {
                                   return eligible_expression(*argument);
                               });
        }
        // Target instructions are admitted here so the managed lowerer can
        // issue a precise diagnostic when a form has raw-only semantics.
        if (expression.left->text.starts_with("$::_")) {
            return std::all_of(expression.arguments.begin(),
                               expression.arguments.end(),
                               [](const auto& argument) {
                                   return eligible_expression(*argument);
                               });
        }
        return !expression.left->text.starts_with("$::") &&
               std::all_of(expression.arguments.begin(), expression.arguments.end(),
                           [](const auto& argument) {
                               return eligible_expression(*argument);
                           });
    case Expr::Kind::Character:
        return decode_character_literal(expression.text).has_value();
    case Expr::Kind::String:
        return false;
    case Expr::Kind::AggregateInitializer:
        return std::all_of(
            expression.initializer_entries.begin(),
            expression.initializer_entries.end(),
            [](const Expr::InitializerEntry& entry) {
                const auto valid_value =
                    entry.value &&
                    (entry.value->kind == Expr::Kind::String ||
                     eligible_expression(*entry.value));
                return valid_value &&
                       std::all_of(
                           entry.designators.begin(),
                           entry.designators.end(),
                           [](const Expr::InitializerDesignator& designator) {
                               return !designator.index ||
                                      eligible_expression(*designator.index);
                           });
            });
    }
    return false;
}

bool eligible_array_element_type(const TypePtr& type) {
    if (!type || type->is_atomic) return false;
    if (type->kind == cross::Type::Kind::Array) {
        return type->element &&
               eligible_array_element_type(type->element);
    }
    if (type->kind == cross::Type::Kind::Pointer) return true;
    if (type->kind == cross::Type::Kind::Record) return true;
    if (type->kind == cross::Type::Kind::Vector) {
        return !type->scalable && type->element && type->lanes != 0;
    }
    return type->kind == cross::Type::Kind::Builtin &&
           type->builtin >= BuiltinType::Bool &&
           (type->builtin <= BuiltinType::Fptr ||
            type->builtin == BuiltinType::Label);
}

bool eligible_statement(const Statement& statement) {
    switch (statement.kind) {
    case Statement::Kind::StaticAssert:
        return false; // Must be prepared and removed before runtime lowering.
    case Statement::Kind::Compound:
    case Statement::Kind::DeclarationList:
        return std::all_of(statement.statements.begin(), statement.statements.end(),
                           [](const auto& child) { return eligible_statement(*child); });
    case Statement::Kind::Empty:
        return true;
    case Statement::Kind::Declaration:
        if (!statement.declaration || !statement.declaration->type) return false;
        if (statement.declaration->type->kind ==
            cross::Type::Kind::Array) {
            const auto dynamic =
                statement.declaration->type->lanes == 0;
            return eligible_array_element_type(statement.declaration->type) &&
                   (dynamic ==
                    static_cast<bool>(
                        statement.declaration->dynamic_array_bound)) &&
                   (!dynamic || eligible_expression(
                                    *statement.declaration
                                         ->dynamic_array_bound)) &&
                   (!statement.declaration->initializer ||
                    statement.declaration->initializer->kind ==
                        Expr::Kind::String ||
                    eligible_expression(
                        *statement.declaration->initializer)) &&
                   !statement.declaration->storage_register &&
                   !statement.declaration->location_name;
        }
        return
               ((statement.declaration->type->kind == cross::Type::Kind::Builtin &&
                 statement.declaration->type->builtin >= BuiltinType::Bool &&
                 (statement.declaration->type->builtin <= BuiltinType::Fptr ||
                  statement.declaration->type->builtin == BuiltinType::Label)) ||
                statement.declaration->type->kind == cross::Type::Kind::Pointer ||
                statement.declaration->type->kind == cross::Type::Kind::Record ||
                (statement.declaration->type->kind == cross::Type::Kind::Vector &&
                 !statement.declaration->type->scalable)) &&
               (!statement.declaration->initializer ||
                eligible_expression(*statement.declaration->initializer));
    case Statement::Kind::Expression:
        return !statement.expression || eligible_expression(*statement.expression);
    case Statement::Kind::Return:
        return !statement.expression || eligible_expression(*statement.expression);
    case Statement::Kind::If:
    case Statement::Kind::Switch:
        return statement.condition && eligible_expression(*statement.condition) &&
               statement.first && eligible_statement(*statement.first) &&
               (!statement.second || eligible_statement(*statement.second));
    case Statement::Kind::While:
        return statement.condition && eligible_expression(*statement.condition) &&
               statement.first && eligible_statement(*statement.first);
    case Statement::Kind::DoWhile:
        return statement.first && eligible_statement(*statement.first) &&
               statement.condition && eligible_expression(*statement.condition);
    case Statement::Kind::For:
        return statement.first && eligible_statement(*statement.first) &&
               (!statement.condition || eligible_expression(*statement.condition)) &&
               std::all_of(statement.increments.begin(), statement.increments.end(),
                           [](const auto& increment) { return eligible_expression(*increment); }) &&
               statement.second && eligible_statement(*statement.second);
    case Statement::Kind::Break:
    case Statement::Kind::Continue:
        return true;
    case Statement::Kind::Label:
        return !statement.first || eligible_statement(*statement.first);
    case Statement::Kind::Case:
    case Statement::Kind::Default:
        return !statement.first || eligible_statement(*statement.first);
    case Statement::Kind::Goto:
        return statement.expression && eligible_expression(*statement.expression);
    }
    return false;
}

void collect_address_taken_names(const Expr& expression,
                                 NameSet& names) {
    if (expression.kind == Expr::Kind::Unary && expression.text == "&" &&
        expression.left) {
        const Expr* operand = expression.left.get();
        while (operand->kind == Expr::Kind::Parenthesized && operand->left) {
            operand = operand->left.get();
        }
        if (operand->kind == Expr::Kind::Name) names.insert(name_key(*operand));
    }
    if (expression.left) collect_address_taken_names(*expression.left, names);
    if (expression.right) collect_address_taken_names(*expression.right, names);
    if (expression.third) collect_address_taken_names(*expression.third, names);
    for (const auto& argument : expression.arguments) {
        collect_address_taken_names(*argument, names);
    }
    for (const auto& entry : expression.initializer_entries) {
        if (entry.value) collect_address_taken_names(*entry.value, names);
        for (const auto& designator : entry.designators)
            if (designator.index)
                collect_address_taken_names(*designator.index, names);
    }
}

void collect_address_taken_names(const Statement& statement,
                                 NameSet& names) {
    if (statement.declaration &&
        statement.declaration->dynamic_array_bound) {
        collect_address_taken_names(
            *statement.declaration->dynamic_array_bound, names);
    }
    if (statement.declaration && statement.declaration->initializer) {
        collect_address_taken_names(*statement.declaration->initializer,
                                    names);
    }
    if (statement.expression) {
        collect_address_taken_names(*statement.expression, names);
    }
    if (statement.condition) {
        collect_address_taken_names(*statement.condition, names);
    }
    for (const auto& increment : statement.increments)
        collect_address_taken_names(*increment, names);
    for (const auto& child : statement.statements) {
        collect_address_taken_names(*child, names);
    }
    if (statement.first) collect_address_taken_names(*statement.first, names);
    if (statement.second) collect_address_taken_names(*statement.second, names);
}

void collect_modified_names(const Expr& expression, NameSet& names) {
    const bool modifying = expression.kind == Expr::Kind::Assign ||
        (expression.kind == Expr::Kind::Unary &&
         (expression.text == "++" || expression.text == "--" ||
          expression.text == "post++" || expression.text == "post--"));
    if (modifying && expression.left) {
        const Expr* target = expression.left.get();
        while (target->kind == Expr::Kind::Parenthesized && target->left)
            target = target->left.get();
        if (target->kind == Expr::Kind::Name)
            names.insert(name_key(*target));
    }
    if (expression.left) collect_modified_names(*expression.left, names);
    if (expression.right) collect_modified_names(*expression.right, names);
    if (expression.third) collect_modified_names(*expression.third, names);
    for (const auto& argument : expression.arguments)
        collect_modified_names(*argument, names);
    for (const auto& entry : expression.initializer_entries) {
        if (entry.value) collect_modified_names(*entry.value, names);
        for (const auto& designator : entry.designators)
            if (designator.index)
                collect_modified_names(*designator.index, names);
    }
}

void collect_modified_names(const Statement& statement, NameSet& names) {
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound)
            collect_modified_names(
                *statement.declaration->dynamic_array_bound, names);
        if (statement.declaration->initializer)
            collect_modified_names(*statement.declaration->initializer, names);
    }
    if (statement.expression) collect_modified_names(*statement.expression, names);
    if (statement.condition) collect_modified_names(*statement.condition, names);
    for (const auto& increment : statement.increments) collect_modified_names(*increment, names);
    for (const auto& child : statement.statements)
        collect_modified_names(*child, names);
    if (statement.first) collect_modified_names(*statement.first, names);
    if (statement.second) collect_modified_names(*statement.second, names);
}

void collect_local_names(const Statement& statement, NameSet& names) {
    if (statement.declaration)
        names.insert(name_key(*statement.declaration));
    for (const auto& child : statement.statements)
        collect_local_names(*child, names);
    if (statement.first) collect_local_names(*statement.first, names);
    if (statement.second) collect_local_names(*statement.second, names);
}

bool contains_dynamic_array(const Statement& statement) {
    if (statement.declaration &&
        statement.declaration->dynamic_array_bound) {
        return true;
    }
    for (const auto& child : statement.statements) {
        if (contains_dynamic_array(*child)) return true;
    }
    return (statement.first && contains_dynamic_array(*statement.first)) ||
           (statement.second && contains_dynamic_array(*statement.second));
}

// Whether control can enter the statement other than at its start: through a
// label, or through a case/default label of an enclosing switch. A nested
// switch's own case labels are entered only through that switch.
bool contains_entry_label(const Statement& statement,
                          bool nested_switch = false) {
    if (statement.kind == Statement::Kind::Label) return true;
    if (!nested_switch && (statement.kind == Statement::Kind::Case ||
                           statement.kind == Statement::Kind::Default)) {
        return true;
    }
    nested_switch |= statement.kind == Statement::Kind::Switch;
    for (const auto& child : statement.statements) {
        if (contains_entry_label(*child, nested_switch)) return true;
    }
    return (statement.first &&
            contains_entry_label(*statement.first, nested_switch)) ||
           (statement.second &&
            contains_entry_label(*statement.second, nested_switch));
}

enum class CallTypeUse { Result, Input, Output, Variadic };

bool supported_call_type(const hir::Module& module, hir::TypeId type,
                         CallTypeUse use) {
    const auto& value = module.type(type);
    // A result is a transported value, not a callee parameter cell. Its
    // top-level cv qualifiers need neither mutable nor volatile storage.
    // Keep the original qualified type and all nested/pointee qualifiers.
    const bool allow_const = use == CallTypeUse::Result || use == CallTypeUse::Input;
    const bool allow_volatile = use == CallTypeUse::Result;
    const bool cv_supported = (!value.is_const || allow_const) &&
                              (!value.is_volatile || allow_volatile);
    if (value.kind == hir::Type::Kind::Pointer)
        return value.is_atomic || !value.is_volatile || allow_volatile;
    if (value.kind == hir::Type::Kind::Vector)
        return cv_supported && !value.is_atomic && !value.scalable &&
               value.element && type_bits(module, type) != 0;
    if (value.kind == hir::Type::Kind::Record)
        return cv_supported && record_value_type(module, type);
    return value.kind == hir::Type::Kind::Builtin &&
           (value.is_atomic || cv_supported) &&
           ((use == CallTypeUse::Result && value.builtin == BuiltinType::Void) ||
            integer_type(module, type) || floating_type(module, type) ||
            label_type(module, type));
}

bool eligible_function(const hir::Module& module, const hir::Function& function,
                       const TargetInfo& target) {
    if (!function.definition) return false;
    const auto* abi = find_abi(target, function.abi);
    if (!abi || !abi->function_selectable ||
        (function.variadic && !abi->variadic_supported)) return false;
    if (!supported_call_type(module, function.result_type, CallTypeUse::Result)) return false;
    for (const auto& parameter : function.parameters) {
        if (!supported_call_type(module, parameter.type,
                parameter.mode == ParameterMode::In ? CallTypeUse::Input : CallTypeUse::Output))
            return false;
    }
    for (const auto& attribute : function.definition->attributes) {
        if (attribute.name != "abi" && attribute.name != "aligned" &&
            attribute.name != "link_name" &&
            attribute.name != "section" &&
            attribute.name != "raw_inline" &&
            attribute.name != "always_inline" &&
            attribute.name != "cold" &&
            attribute.name != "hot" &&
            attribute.name != "used" &&
            attribute.name != "retain" &&
            attribute.name != "weak" &&
            attribute.name != "visibility" &&
            attribute.name != "alias" &&
            attribute.name != "weakref" &&
            attribute.name != "no_stack_protector" &&
            attribute.name != "no_sanitize" &&
            attribute.name != "noinline" &&
            attribute.name != "noreturn" &&
            attribute.name != "variadic" &&
            attribute.name != "stack_cleanup" &&
            attribute.name != "clobber" &&
            attribute.name != "naked") {
            return false;
        }
    }
    return function.definition->body &&
           eligible_statement(*function.definition->body);
}

class ManagedLowerer {
public:
    ManagedLowerer(hir::Module& module, const Subtarget& subtarget,
                   const CompilerOptions& options, Diagnostics& diagnostics)
        : hir_(module), subtarget_(subtarget), target_(subtarget.target()),
          diagnostics_(diagnostics), bounds_trap_(options.bounds_trap) {}

    ManagedModule run() {
        for (auto& function : hir_.functions) {
            if (function.ownership != hir::BodyOwnership::ManagedAst ||
                !eligible_function(hir_, function, target_)) continue;
            auto lowered = lower_function(function);
            if (!lowered) continue;
            result_.definitions.insert(function.id.value);
            result_.functions.push_back(std::move(*lowered));
            function.ownership = hir::BodyOwnership::ManagedMir;
        }
        return std::move(result_);
    }

private:
    struct LocalBinding {
        SlotId slot;
        hir::TypeId type;
        std::optional<ValueId> dynamic_address;
        std::optional<ValueId> dynamic_size;
        bool register_storage{};
    };

    struct Scope {
        NameMap<LocalBinding> bindings;
        std::vector<SlotId> slots;
        std::optional<ValueId> dynamic_stack_mark;
    };

    struct LoopContext {
        BlockId break_target;
        BlockId continue_target;
        std::size_t retained_scopes{};
        bool is_switch{};
    };

    struct AtomicLvalue {
        ValueId address;
        hir::TypeId object_type;
        bool is_volatile{};
    };

    struct BitFieldAccess {
        unsigned width{};
        unsigned offset{};
    };

    struct DesignatorAddress {
        ValueId address;
        hir::TypeId type;
        unsigned alignment{1};
        std::optional<BitFieldAccess> bit_field{};
    };

    struct VectorLane {
        std::optional<LocalBinding> local;
        std::optional<DesignatorAddress> memory;
        ValueId index;
        hir::TypeId element_type;
    };

    enum class ObjectAccess { Designator, Value };

    struct ActiveDynamicArray {
        const Statement* declaration{};
        std::size_t scope_depth{};

        friend bool operator==(const ActiveDynamicArray&,
                               const ActiveDynamicArray&) = default;
    };

    struct ControlPoint {
        std::size_t scope_depth{};
        std::vector<ActiveDynamicArray> dynamic_arrays;
    };

    void collect_control_points(
        const Statement& statement, hir::FunctionId function,
        std::size_t scope_depth,
        std::vector<ActiveDynamicArray>& dynamic_arrays) {
        if (statement.kind == Statement::Kind::DeclarationList) {
            for (const auto& child : statement.statements)
                collect_control_points(*child, function, scope_depth, dynamic_arrays);
            return;
        }
        if (statement.kind == Statement::Kind::Compound) {
            const auto retained = dynamic_arrays.size();
            for (const auto& child : statement.statements) {
                collect_control_points(*child, function, scope_depth + 1,
                                       dynamic_arrays);
            }
            dynamic_arrays.resize(retained);
            return;
        }
        if (statement.kind == Statement::Kind::For) {
            const auto retained = dynamic_arrays.size();
            if (statement.first) {
                collect_control_points(*statement.first, function,
                                       scope_depth + 1, dynamic_arrays);
            }
            if (statement.second) {
                collect_control_points(*statement.second, function,
                                       scope_depth + 1, dynamic_arrays);
            }
            dynamic_arrays.resize(retained);
            return;
        }
        if (statement.kind == Statement::Kind::Label) {
            if (const auto* label =
                    hir_.label(function, statement)) {
                label_control_points_.emplace(
                    label->id.value,
                    ControlPoint{scope_depth, dynamic_arrays});
            }
        } else if (statement.kind == Statement::Kind::Goto ||
                   statement.kind == Statement::Kind::Switch ||
                   statement.kind == Statement::Kind::Case ||
                   statement.kind == Statement::Kind::Default) {
            statement_control_points_.emplace(
                &statement, ControlPoint{scope_depth, dynamic_arrays});
        }
        if (statement.declaration &&
            statement.declaration->dynamic_array_bound) {
            dynamic_arrays.push_back({&statement, scope_depth});
        }
        if (statement.first) {
            collect_control_points(*statement.first, function, scope_depth,
                                   dynamic_arrays);
        }
        if (statement.second) {
            collect_control_points(*statement.second, function, scope_depth,
                                   dynamic_arrays);
        }
    }

    bool supports_direct_vla_transition(const Statement& statement,
                                        hir::LabelId target) const {
        const auto source = statement_control_points_.find(&statement);
        const auto destination = label_control_points_.find(target.value);
        if (source == statement_control_points_.end() ||
            destination == label_control_points_.end() ||
            destination->second.scope_depth > source->second.scope_depth) {
            return false;
        }
        std::vector<ActiveDynamicArray> retained;
        for (const auto& array : source->second.dynamic_arrays) {
            if (array.scope_depth <= destination->second.scope_depth) {
                retained.push_back(array);
            }
        }
        return retained == destination->second.dynamic_arrays;
    }

    // Binds a naked parameter to the register its location names; the
    // parameter is that register for the whole body.
    void bind_hard_register(const std::string& name, SourceLocation location,
                            hir::TypeId type, std::string_view register_name,
                            const NameKey& key, std::uint32_t index) {
        const auto* view = find_register(target_, register_name);
        const auto bits = type_bits(hir_, type);
        const auto floating = floating_type(hir_, type);
        if (!view ||
            std::none_of(view->instruction_scalar_modes.begin(),
                         view->instruction_scalar_modes.end(),
                         [&](const RegisterEntry::ScalarMode& mode) {
                             return mode.bits == bits &&
                                    mode.floating == floating;
                         })) {
            diagnostics_.error(location,
                               "naked parameter '" + name +
                                   "' must name one target register that "
                                   "carries its type");
            failed_ = true;
            return;
        }
        const SlotId slot{static_cast<std::uint32_t>(current_.slots.size())};
        current_.slots.push_back({slot, location, type,
                                  "$param." + std::to_string(index),
                                  std::string(register_name), false, false,
                                  false, 1, index});
        const LocalBinding binding{slot, type, std::nullopt, std::nullopt};
        scopes_.back().bindings.emplace(key, binding);
        scopes_.back().slots.push_back(slot);
    }

    std::optional<ManagedFunction> lower_function(const hir::Function& function) {
        failed_ = false;
        // A naked body is the programmer's exact code: no instrumentation.
        bounds_checks_ = bounds_trap_ && !function.naked &&
            std::find(function.no_sanitize.begin(), function.no_sanitize.end(),
                      "bounds") == function.no_sanitize.end();
        dynamic_counts_.clear();
        current_patch_sinks_.clear();
        current_patch_origins_.clear();
        parameter_values_.clear();
        label_blocks_.clear();
        label_control_points_.clear();
        statement_control_points_.clear();
        case_blocks_.clear();
        scopes_.clear();
        loops_.clear();
        copy_outs_.clear();
        current_ = {};
        current_.source = function.id;
        address_taken_names_.clear();
        collect_address_taken_names(*function.definition->body,
                                    address_taken_names_);
        modified_names_.clear();
        collect_modified_names(*function.definition->body, modified_names_);
        local_names_.clear();
        for (const auto& parameter : function.parameters)
            local_names_.insert(name_key(parameter));
        collect_local_names(*function.definition->body, local_names_);
        collect_copyout_names(*function.definition->body);
        current_.location = function.location;
        current_.result_type = function.result_type;
        naked_ = function.naked;
        has_dynamic_arrays_ =
            contains_dynamic_array(*function.definition->body);
        std::vector<ActiveDynamicArray> dynamic_arrays;
        collect_control_points(*function.definition->body, function.id, 1,
                               dynamic_arrays);
        current_.entry = new_block(function.location, true);
        for (const auto label_id : function.labels) {
            const auto& label = hir_.labels.at(label_id.value);
            const auto target = new_block(label.location);
            label_blocks_.emplace(label_id.value, target);
            current_.labels.push_back({label_id, target});
        }
        enter(current_.entry);
        scopes_.emplace_back();
        // Without a manual interface an `out`/`inout` parameter is its
        // transport pointer, and its cell copies in and out through it.
        const bool pointer_transport = !hir::manual_interface(function);
        for (std::uint32_t index = 0; index < function.parameters.size(); ++index) {
            const auto& parameter = function.parameters[index];
            if (naked_) {
                // A naked parameter is the register its location names.
                bind_hard_register(parameter.name, parameter.location,
                                   parameter.type,
                                   parameter.physical_location.value_or(""),
                                   name_key(parameter), index);
                continue;
            }
            const bool transport =
                pointer_transport && parameter.mode != ParameterMode::In;
            const auto value = add_value(
                ValueKind::Parameter,
                transport ? hir_.pointer_to(parameter.type) : parameter.type,
                parameter.location);
            current_.values[value.value].parameter_index = index;
            current_.parameters.push_back(value);
            // An atomic-qualified parameter is still transported by value,
            // but source semantics require a distinct atomic callee cell.
            // An unmodified scalar `in` parameter may stay in SSA. Written
            // parameters and record designators need a distinct callee cell.
            if (parameter.mode == ParameterMode::In &&
                (!parameter.physical_location ||
                 *parameter.physical_location == "auto") &&
                !address_taken_names_.contains(name_key(parameter)) &&
                !modified_names_.contains(name_key(parameter)) &&
                !hir_.type(parameter.type).is_atomic &&
                !record_value_type(hir_, parameter.type)) {
                if (!parameter_values_.emplace(
                        name_key(parameter), value).second) {
                    failed_ = true;
                }
                continue;
            }
            const SlotId slot{
                static_cast<std::uint32_t>(current_.slots.size())};
            const auto cell_type = parameter.type;
            current_.slots.push_back(
                {slot, parameter.location, cell_type,
                 "$param." + std::to_string(index), std::nullopt, false,
                 address_taken_names_.contains(name_key(parameter)),
                 parameter.mode != ParameterMode::In && !transport, 1,
                 index});
            const LocalBinding binding{slot, cell_type, std::nullopt,
                                       std::nullopt};
            if (!scopes_.back().bindings.emplace(name_key(parameter), binding).second) {
                failed_ = true;
            }
            scopes_.back().slots.push_back(slot);
            (void)lifetime(ValueKind::LifetimeStart, slot,
                           parameter.location);
            if (transport) {
                copy_outs_.push_back({binding, value});
                if (parameter.mode == ParameterMode::InOut) {
                    const auto incoming =
                        load_pointer(value, parameter.location);
                    if (!incoming) failed_ = true;
                    else (void)store_slot(binding, *incoming, parameter.location);
                }
            } else if (parameter.mode != ParameterMode::Out) {
                (void)store_slot(binding, value, parameter.location);
            }
        }
        for (std::uint32_t index = 0;
             index < function.variadic_bindings.size(); ++index) {
            const auto& state = function.variadic_bindings[index];
            const auto* abi = find_abi(target_, function.abi);
            const auto* state_model =
                abi && state.state.valid() &&
                        state.state.value < abi->variadic_states.size()
                    ? &abi->variadic_states[state.state.value]
                    : nullptr;
            const auto value = add_value(
                ValueKind::VariadicState, state.type, state.location);
            current_.values[value.value].variadic_state = state.state;
            const SlotId slot{
                static_cast<std::uint32_t>(current_.slots.size())};
            current_.slots.push_back(
                {slot, state.location, state.type,
                 "$variadic." +
                     (state_model ? state_model->canonical_name
                                  : std::to_string(state.state.value)),
                 std::nullopt, false,
                 address_taken_names_.contains(name_key(state)), false, 1,
                 std::nullopt});
            const LocalBinding binding{slot, state.type, std::nullopt,
                                       std::nullopt};
            if (!scopes_.back().bindings.emplace(
                    name_key(state), binding).second) {
                diagnostics_.error(
                    state.location,
                    "variadic state binding '" + state.name +
                        "' conflicts with another local name");
                failed_ = true;
                continue;
            }
            scopes_.back().slots.push_back(slot);
            (void)lifetime(ValueKind::LifetimeStart, slot,
                           state.location);
            (void)store_slot(binding, value, state.location);
        }
        lower_statement(*function.definition->body);
        if (current_block_ && naked_ && !failed_) {
            diagnostics_.error(function.location,
                               "reachable end of naked function requires an "
                               "explicit target control transfer");
            failed_ = true;
        }
        if (current_block_) {
            if (void_type(hir_, function.result_type) &&
                !function.definition->attribute("noreturn")) {
                copy_out_parameters(function.location);
                terminate(TerminatorKind::Return, function.location, std::nullopt, {});
            } else {
                terminate(TerminatorKind::Unreachable, function.location, std::nullopt, {});
            }
        }
        if (failed_) {
            current_patch_sinks_.clear();
            return std::nullopt;
        }
        prune_unreachable_blocks(current_);
        for (const auto& sink : current_patch_sinks_) {
            patch_sinks_.insert(sink);
            result_.object_definitions.insert(sink.first);
        }
        current_patch_sinks_.clear();
        return std::move(current_);
    }

    BlockId new_block(SourceLocation location, bool entry = false) {
        const BlockId id{static_cast<std::uint32_t>(current_.blocks.size())};
        const EffectId effect{static_cast<std::uint32_t>(current_.effects.size())};
        ManagedEffect effect_value;
        effect_value.id = effect;
        effect_value.location = location;
        effect_value.kind = entry ? EffectKind::Entry : EffectKind::Phi;
        current_.effects.push_back(std::move(effect_value));
        ManagedBlock block;
        block.id = id;
        block.location = location;
        block.effect = effect;
        current_.blocks.push_back(std::move(block));
        return id;
    }

    ManagedBlock& block(BlockId id) { return current_.blocks.at(id.value); }

    void enter(BlockId id) {
        current_block_ = id;
        current_effect_ = block(id).effect;
    }

    ValueId add_value(ValueKind kind, hir::TypeId type, SourceLocation location) {
        if (!current_block_) {
            failed_ = true;
            return {};
        }
        const ValueId id{static_cast<std::uint32_t>(current_.values.size())};
        ManagedValue value;
        value.id = id;
        value.location = location;
        value.type = type;
        value.kind = kind;
        current_.values.push_back(std::move(value));
        block(*current_block_).values.push_back(id);
        return id;
    }

    ValueId add_effectful(ValueKind kind, hir::TypeId type,
                          SourceLocation location) {
        if (!current_effect_) {
            failed_ = true;
            return {};
        }
        const auto value = add_value(kind, type, location);
        const auto output = EffectId{static_cast<std::uint32_t>(current_.effects.size())};
        ManagedEffect effect;
        effect.id = output;
        effect.location = location;
        effect.kind = EffectKind::Operation;
        effect.input = *current_effect_;
        effect.operation = value;
        current_.effects.push_back(std::move(effect));
        auto& operation = current_.values[value.value];
        operation.effect_input = current_effect_;
        operation.effect_output = output;
        current_effect_ = output;
        return value;
    }

    void terminate(TerminatorKind kind, SourceLocation location,
                   std::optional<ValueId> value, std::vector<BlockId> successors) {
        if (!current_block_) {
            failed_ = true;
            return;
        }
        if (!current_effect_) {
            failed_ = true;
            return;
        }
        auto& current = block(*current_block_);
        current.terminator = {kind, location, value, successors, *current_effect_};
        for (const auto successor : successors) {
            auto& predecessors = block(successor).predecessors;
            if (std::find(predecessors.begin(), predecessors.end(), current.id) ==
                predecessors.end()) predecessors.push_back(current.id);
            current_.effects[block(successor).effect.value].incoming.push_back(
                {current.id, *current_effect_});
        }
        current_block_.reset();
        current_effect_.reset();
    }

    std::optional<NameKey> local_name(const Expr& expression) const {
        if (expression.kind == Expr::Kind::Name) return name_key(expression);
        if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
            return local_name(*expression.left);
        }
        return std::nullopt;
    }

    const LocalBinding* find_local(const NameKey& name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->bindings.find(name);
            if (found != scope->bindings.end()) return &found->second;
        }
        return nullptr;
    }

    bool global_scalar(const hir::Object& object) const {
        return managed_value_type(hir_, object.type);
    }

    bool global_object(const hir::Object& object) const {
        return managed_object_type(hir_, object.type);
    }

    const hir::Function* exact_function(std::string_view name,
                                        bool local_static) const {
        const auto& caller = hir_.function(current_.source);
        const hir::Function* result = nullptr;
        for (const auto& candidate : hir_.functions) {
            if (candidate.source_name != name) continue;
            const bool is_local_static = candidate.linkage == Linkage::Static &&
                                         candidate.source_unit == caller.source_unit;
            if (local_static != is_local_static) continue;
            if (!local_static && candidate.linkage == Linkage::Static) continue;
            if (result) return nullptr;
            result = &candidate;
        }
        return result;
    }

    std::pair<const hir::Function*, const hir::Object*> resolve_value_name(NameUse name) const {
        const auto* source = hir_.function(current_.source).definition;
        const auto candidates = namespace_candidates(name,
            source ? source->source_namespace : std::string{},
            source ? source->imports : std::vector<std::string>{});
        for (const auto& candidate : candidates) {
            const auto* function = exact_function(candidate, true);
            if (!function) function = exact_function(candidate, false);
            const auto* object = exact_object(candidate, true);
            if (!object) object = exact_object(candidate, false);
            if (function || object) return {function, object};
        }
        return {};
    }

    const hir::Function* resolve_function(NameUse name) const {
        return resolve_value_name(name).first;
    }

    void collect_copyout_names(const Expr& expression) {
        if (expression.kind == Expr::Kind::Call && expression.left) {
            const auto* callee = expression.left->kind == Expr::Kind::Name &&
                                         !local_names_.contains(name_key(*expression.left))
                                     ? resolve_function(*expression.left)
                                     : nullptr;
            // A machine instruction may write any object operand.
            const bool builtin = expression.left->kind == Expr::Kind::Name &&
                expression.left->text.starts_with("$::") &&
                !expression.left->text.starts_with("$::_");
            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                if (builtin ||
                    (callee && (index >= callee->parameters.size() ||
                                callee->parameters[index].mode == ParameterMode::In)))
                    continue;
                const Expr* actual = expression.arguments[index].get();
                while (actual && actual->kind == Expr::Kind::Parenthesized &&
                       actual->left) actual = actual->left.get();
                // A lane/member output may modify an in parameter's local
                // cell too. Do not leave that root as a value-only SSA binding.
                while (actual && actual->kind == Expr::Kind::Binary && actual->left &&
                       (actual->text == "index" || actual->text == "member")) {
                    actual = actual->left.get();
                    while (actual->kind == Expr::Kind::Parenthesized && actual->left)
                        actual = actual->left.get();
                }
                if (actual && actual->kind == Expr::Kind::Name)
                    modified_names_.insert(name_key(*actual));
            }
        }
        if (expression.left) collect_copyout_names(*expression.left);
        if (expression.right) collect_copyout_names(*expression.right);
        if (expression.third) collect_copyout_names(*expression.third);
        for (const auto& argument : expression.arguments)
            collect_copyout_names(*argument);
        for (const auto& entry : expression.initializer_entries) {
            if (entry.value) collect_copyout_names(*entry.value);
            for (const auto& designator : entry.designators)
                if (designator.index)
                    collect_copyout_names(*designator.index);
        }
    }

    void collect_copyout_names(const Statement& statement) {
        if (statement.declaration) {
            if (statement.declaration->dynamic_array_bound)
                collect_copyout_names(*statement.declaration->dynamic_array_bound);
            if (statement.declaration->initializer)
                collect_copyout_names(*statement.declaration->initializer);
        }
        if (statement.expression) collect_copyout_names(*statement.expression);
        if (statement.condition) collect_copyout_names(*statement.condition);
        for (const auto& increment : statement.increments) collect_copyout_names(*increment);
        for (const auto& child : statement.statements)
            collect_copyout_names(*child);
        if (statement.first) collect_copyout_names(*statement.first);
        if (statement.second) collect_copyout_names(*statement.second);
    }

    const hir::Object* exact_object(std::string_view name,
                                    bool local_static) const {
        const auto& caller = hir_.function(current_.source);
        const hir::Object* result = nullptr;
        for (const auto& candidate : hir_.objects) {
            if (candidate.source_name != name) continue;
            const bool is_local_static = candidate.linkage == Linkage::Static &&
                                         candidate.source_unit == caller.source_unit;
            if (local_static != is_local_static) continue;
            if (!local_static && candidate.linkage == Linkage::Static) continue;
            if (result) return nullptr;
            result = &candidate;
        }
        return result;
    }

    const hir::Object* resolve_object(NameUse name) const {
        return resolve_value_name(name).second;
    }

    const hir::Label* resolve_label(NameUse name) const {
        if (const auto* local = hir_.label(current_.source, name)) {
            return local;
        }
        const auto split = name.spelling.rfind("::");
        if (split == std::string_view::npos) return nullptr;
        NameUse owner_name = name;
        owner_name.spelling = name.spelling.substr(0, split);
        const auto* owner = resolve_function(owner_name);
        if (!owner) return nullptr;
        const auto qualified = owner->source_name + std::string(name.spelling.substr(split));
        name.spelling = qualified;
        return hir_.label(owner->id, name);
    }

    ValueId label_address(const hir::Label& label,
                          SourceLocation location) {
        (void)hir::stabilize_label_address(hir_, label.id, location, diagnostics_);
        const auto value = add_value(
            ValueKind::LabelAddress, *hir_.builtin(BuiltinType::Label),
            location);
        current_.values[value.value].label = label.id;
        return value;
    }

    std::optional<PatchSink> resolve_patch_sink(
        const Expr& expression, PatchAddressRepresentation representation) {
        auto result = resolve_patch_sink_designator(
            expression, hir_, target_, representation,
            [&](NameUse name) { return resolve_object(name); },
            diagnostics_);
        if (!result) {
            failed_ = true;
            return std::nullopt;
        }
        return result;
    }

    bool callable(const hir::Function& function) const {
        if (!supported_call_type(hir_, function.result_type, CallTypeUse::Result)) {
            return false;
        }
        const auto* abi = find_abi(target_, function.abi);
        if (!abi || !abi->function_selectable ||
            (function.variadic && !abi->variadic_supported)) return false;
        for (const auto& parameter : function.parameters) {
            if (!supported_call_type(hir_, parameter.type,
                    parameter.mode == ParameterMode::In ? CallTypeUse::Input : CallTypeUse::Output))
                return false;
        }
        return true;
    }

    hir::TypeId function_pointer_type(const hir::Function& function) {
        auto signature = *hir::call_signature(hir_, function.id, {});
        return hir_.pointer_to(hir_.function_type(std::move(signature)));
    }

    std::optional<ValueId> function_address(const hir::Function& function,
                                            SourceLocation location) {
        if (function.fixed_address)
            return constant(UInt128{*function.fixed_address}, function_pointer_type(function), location);
        hir::stabilize_function_address(hir_, function.id);
        const auto id = function.id;
        const auto type = function_pointer_type(function);
        const auto value =
            add_value(ValueKind::FunctionAddress, type, location);
        current_.values[value.value].callee = id;
        return value;
    }

    ValueId lifetime(ValueKind kind, SlotId slot, SourceLocation location) {
        const auto value = add_effectful(kind, *hir_.builtin(BuiltinType::Void),
                                         location);
        current_.values[value.value].slot = slot;
        return value;
    }

    ValueId dynamic_stack_save(SourceLocation location) {
        return add_effectful(ValueKind::DynamicStackSave,
                             *hir_.builtin(BuiltinType::Uptr), location);
    }

    ValueId dynamic_alloca(ValueId bound, hir::TypeId element,
                           std::uint64_t element_size, unsigned alignment,
                           SourceLocation location) {
        const auto value = add_effectful(
            ValueKind::DynamicAlloca, hir_.pointer_to(element), location);
        auto& allocation = current_.values[value.value];
        allocation.operands.push_back(bound);
        allocation.integer = element_size;
        allocation.integer_high = alignment;
        return value;
    }

    ValueId dynamic_stack_restore(ValueId mark,
                                  SourceLocation location) {
        const auto value = add_effectful(
            ValueKind::DynamicStackRestore,
            *hir_.builtin(BuiltinType::Void), location);
        current_.values[value.value].operands.push_back(mark);
        return value;
    }

    ValueId slot_address(const LocalBinding& binding,
                         SourceLocation location) {
        auto& slot = current_.slots[binding.slot.value];
        slot.address_taken = true;
        const auto value = add_value(ValueKind::SlotAddress,
                                     hir_.pointer_to(binding.type), location);
        current_.values[value.value].slot = binding.slot;
        return value;
    }

    ValueId global_address(const hir::Object& object,
                           SourceLocation location) {
        if (object.fixed_address)
            return constant(UInt128{*object.fixed_address}, hir_.pointer_to(object.type), location);
        const auto value = add_value(ValueKind::GlobalAddress,
                                     hir_.pointer_to(object.type), location);
        current_.values[value.value].object = object.id;
        return value;
    }

    ValueId indexed_address(ValueId base, ValueId index,
                            hir::TypeId pointee, SourceLocation location) {
        const auto value = add_value(ValueKind::IndexedAddress,
                                     hir_.pointer_to(pointee), location);
        current_.values[value.value].operands = {base, index};
        return value;
    }

    hir::TypeId qualified_array_element(hir::TypeId array) {
        const auto& type = hir_.type(array);
        return hir_.add_qualifiers(*type.element, type.is_const, type.is_volatile,
                                   type.may_alias);
    }

    ValueId decay_array_address(ValueId address, hir::TypeId array,
                                SourceLocation location) {
        return cast(address, hir_.pointer_to(qualified_array_element(array)), location);
    }

    const hir::RecordMember* resolve_member(hir::TypeId record_type,
                                            const MemberName& name,
                                            SourceLocation location,
                                            bool diagnose = true) {
        const auto& type = hir_.type(record_type);
        if (type.kind != hir::Type::Kind::Record || !type.record) {
            if (diagnose) {
                diagnostics_.error(location,
                                   "member access requires a record object");
                failed_ = true;
            }
            return nullptr;
        }
        const auto& record = hir_.record(*type.record);
        if (!record.complete) {
            if (diagnose) {
                diagnostics_.error(location,
                                   "member access requires a complete record type");
                failed_ = true;
            }
            return nullptr;
        }
        const auto* member = hir_.member(*type.record, name);
        if (!member && diagnose) {
            diagnostics_.error(location,
                               "record '" + record.source_name +
                                   "' has no member named '" +
                                   name.spelling + "'");
            failed_ = true;
        }
        return member;
    }

    hir::TypeId qualified_member_type(hir::TypeId record_type,
                                      const hir::RecordMember& member) {
        const auto& owner = hir_.type(record_type);
        return hir_.add_qualifiers(member.type, owner.is_const,
                                   owner.is_volatile, owner.may_alias);
    }

    hir::TypeId bit_field_storage_type(hir::TypeId member_type) {
        const auto& source = hir_.type(member_type);
        BuiltinType storage = BuiltinType::U8;
        switch (type_bits(hir_, member_type)) {
        case 8: storage = BuiltinType::U8; break;
        case 16: storage = BuiltinType::U16; break;
        case 32: storage = BuiltinType::U32; break;
        case 64:
            storage = source.builtin == BuiltinType::Iptr ||
                              source.builtin == BuiltinType::Uptr
                          ? BuiltinType::Uptr
                          : BuiltinType::U64;
            break;
        case 128: storage = BuiltinType::U128; break;
        default: break;
        }
        return hir_.add_qualifiers(*hir_.builtin(storage), false,
                                   source.is_volatile);
    }

    ContinuationTask<std::optional<DesignatorAddress>> lower_designator_address_async(
        const Expr& expression, ObjectAccess access = ObjectAccess::Designator) {
        if (expression.kind == Expr::Kind::Parenthesized &&
            expression.left) {
            co_return co_await lower_designator_address_async(*expression.left, access);
        }
        if (expression.kind == Expr::Kind::Name) {
            if (const auto* local = find_local(name_key(expression))) {
                if (local->dynamic_address) {
                    co_return DesignatorAddress{
                        cast(*local->dynamic_address,
                             hir_.pointer_to(local->type),
                             expression.location),
                        local->type,
                        storage_alignment(hir_, local->type, target_)};
                }
                if (current_.slots[local->slot.value].physical_location) {
                    co_return std::nullopt;
                }
                co_return DesignatorAddress{
                    slot_address(*local, expression.location), local->type,
                    storage_alignment(hir_, local->type, target_)};
            }
            if (const auto* object = resolve_object(expression);
                object && global_object(*object)) {
                co_return DesignatorAddress{
                    global_address(*object, expression.location), object->type,
                    storage_alignment(hir_, object->type, target_)};
            }
            co_return std::nullopt;
        }
        if (expression.kind == Expr::Kind::Unary &&
            expression.text == "*" && expression.left) {
            auto address = co_await lower_expression_async(*expression.left);
            if (!address) co_return std::nullopt;
            const auto& pointer =
                hir_.type(current_.values[address->value].type);
            if (pointer.kind != hir::Type::Kind::Pointer ||
                !pointer.pointee) {
                co_return std::nullopt;
            }
            co_return DesignatorAddress{
                *address, *pointer.pointee,
                access_alignment(hir_, *pointer.pointee, target_)};
        }
        if (expression.kind == Expr::Kind::Binary &&
            expression.text == "index" && expression.left &&
            expression.right) {
            std::optional<ValueId> base;
            hir::TypeId element{};
            unsigned base_alignment{1};
            // Classify without evaluating first. A pointer-valued member is
            // not an array designator, and probing its address would execute
            // a side-effecting base twice before the fallback value load.
            const auto aggregate_type = designator_type(*expression.left, nullptr, access);
            if (aggregate_type && vector_type(hir_, *aggregate_type)) {
                const auto lane = co_await lower_vector_lane_async(expression, true);
                co_return lane ? lane->memory : std::nullopt;
            }
            std::optional<ValueId> dynamic_count;
            std::uint64_t fixed_count{};
            if (aggregate_type && array_type(hir_, *aggregate_type)) {
                const auto aggregate = co_await lower_designator_address_async(*expression.left, access);
                if (!aggregate) co_return std::nullopt;
                fixed_count = hir_.type(aggregate->type).lanes;
                if (fixed_count == 0) {
                    const auto name = local_name(*expression.left);
                    const auto* local = name ? find_local(*name) : nullptr;
                    if (local && local->dynamic_address)
                        dynamic_count = dynamic_counts_.at(local->dynamic_address->value);
                }
                element = qualified_array_element(aggregate->type);
                base = decay_array_address(aggregate->address,
                                           aggregate->type,
                                           expression.location);
                base_alignment = std::min(
                    aggregate->alignment,
                    storage_alignment(hir_, element, target_));
            } else {
                base = co_await lower_expression_async(*expression.left);
                if (!base) co_return std::nullopt;
                const auto& pointer =
                    hir_.type(current_.values[base->value].type);
                if (pointer.kind != hir::Type::Kind::Pointer ||
                    !pointer.pointee) {
                    co_return std::nullopt;
                }
                element = *pointer.pointee;
                base_alignment =
                    access_alignment(hir_, element, target_);
            }
            auto index = co_await lower_expression_async(*expression.right);
            if (!index ||
                !integer_type(hir_, current_.values[index->value].type)) {
                co_return std::nullopt;
            }
            if (bounds_checks_ && (fixed_count != 0 || dynamic_count))
                require_subscript_in_bounds(*index, fixed_count, dynamic_count,
                                            &expression == one_past_subscript_,
                                            expression.location);
            co_return DesignatorAddress{
                indexed_address(*base, *index, element,
                                expression.location),
                element, base_alignment};
        }
        if (expression.kind == Expr::Kind::Binary &&
            (expression.text == "member" ||
             expression.text == "pointer_member") &&
            expression.left && expression.right &&
            expression.right->kind == Expr::Kind::Name) {
            std::optional<DesignatorAddress> base;
            if (expression.text == "member") {
                base = co_await lower_designator_address_async(*expression.left, access);
            } else {
                auto address = co_await lower_expression_async(*expression.left);
                if (address) {
                    const auto& pointer =
                        hir_.type(current_.values[address->value].type);
                    if (pointer.kind == hir::Type::Kind::Pointer &&
                        pointer.pointee) {
                        base = DesignatorAddress{
                            *address, *pointer.pointee,
                            access_alignment(hir_, *pointer.pointee,
                                             target_)};
                    }
                }
            }
            if (!base) {
                if (access == ObjectAccess::Designator && expression.text == "member" &&
                    designator_type(*expression.left, nullptr, ObjectAccess::Value)) {
                    diagnostics_.error(expression.location,
                        "a member of a record value is not an object designator");
                    failed_ = true;
                }
                co_return std::nullopt;
            }
            const auto* member = resolve_member(
                base->type, member_name(*expression.right),
                expression.right->location);
            if (!member) co_return std::nullopt;
            const auto member_type =
                qualified_member_type(base->type, *member);
            auto address = cast(
                base->address,
                hir_.pointer_to(*hir_.builtin(BuiltinType::U8)),
                expression.location);
            if (member->offset != 0) {
                const auto offset = constant(
                    member->offset, *hir_.builtin(BuiltinType::Uptr),
                    expression.location);
                address = indexed_address(
                    address, offset, *hir_.builtin(BuiltinType::U8),
                    expression.location);
            }
            const auto address_type = member->bit_width
                                          ? bit_field_storage_type(member_type)
                                          : member_type;
            address = cast(address, hir_.pointer_to(address_type),
                           expression.location);
            co_return DesignatorAddress{
                address, member_type,
                std::max(1U, std::min(base->alignment,
                                     member->alignment)),
                member->bit_width
                    ? std::optional<BitFieldAccess>(BitFieldAccess{
                          *member->bit_width, member->bit_offset})
                    : std::nullopt};
        }
        if (access == ObjectAccess::Value) {
            const auto type = infer_type(expression);
            if (type && record_value_type(hir_, *type)) {
                const auto value = co_await lower_expression_async(expression);
                if (!value) co_return std::nullopt;
                // A record SSA result has no source designator. Give this
                // occurrence a typed storage home for common member/bit-field
                // projection; target ABI lowering still owns its transport.
                const SlotId slot{static_cast<std::uint32_t>(current_.slots.size())};
                current_.slots.push_back({slot, expression.location, *type,
                    "$record.value." + std::to_string(slot.value), std::nullopt,
                    false, true, false, storage_alignment(hir_, *type, target_), std::nullopt});
                scopes_.back().slots.push_back(slot);
                (void)lifetime(ValueKind::LifetimeStart, slot, expression.location);
                const LocalBinding temporary{slot, *type, std::nullopt, std::nullopt};
                (void)store_slot(temporary, *value, expression.location);
                co_return DesignatorAddress{slot_address(temporary, expression.location),
                    *type, storage_alignment(hir_, *type, target_)};
            }
        }
        co_return std::nullopt;
    }

    std::optional<DesignatorAddress> lower_designator_address(
        const Expr& expression, ObjectAccess access = ObjectAccess::Designator) {
        return lower_designator_address_async(expression, access).run();
    }

    std::optional<hir::TypeId> designator_type(
        const Expr& expression, bool* is_bit_field = nullptr,
        ObjectAccess access = ObjectAccess::Designator) {
        if (is_bit_field) *is_bit_field = false;
        if (expression.kind == Expr::Kind::Parenthesized &&
            expression.left) {
            return designator_type(*expression.left, is_bit_field, access);
        }
        if (expression.kind == Expr::Kind::Name) {
            if (const auto* local = find_local(name_key(expression))) {
                return local->type;
            }
            if (const auto* object = resolve_object(expression);
                object && global_object(*object)) {
                return object->type;
            }
            return std::nullopt;
        }
        if (expression.kind == Expr::Kind::Unary &&
            expression.text == "*" && expression.left) {
            const auto pointer_id = infer_type(*expression.left);
            if (!pointer_id) return std::nullopt;
            const auto& pointer = hir_.type(*pointer_id);
            return pointer.kind == hir::Type::Kind::Pointer &&
                           pointer.pointee
                       ? pointer.pointee
                       : std::nullopt;
        }
        if (expression.kind == Expr::Kind::Binary &&
            expression.text == "index" && expression.left) {
            const auto pointer_id = infer_type(*expression.left);
            if (!pointer_id) return std::nullopt;
            if (vector_type(hir_, *pointer_id) && designator_type(*expression.left)) {
                return qualified_array_element(*pointer_id);
            }
            const auto& pointer = hir_.type(*pointer_id);
            return pointer.kind == hir::Type::Kind::Pointer &&
                           pointer.pointee
                       ? pointer.pointee
                       : std::nullopt;
        }
        if (expression.kind == Expr::Kind::Binary &&
            (expression.text == "member" ||
             expression.text == "pointer_member") &&
            expression.left && expression.right &&
            expression.right->kind == Expr::Kind::Name) {
            auto owner = expression.text == "pointer_member"
                             ? infer_type(*expression.left)
                             : designator_type(*expression.left, nullptr, access);
            if (!owner) return std::nullopt;
            if (expression.text == "pointer_member") {
                const auto& pointer = hir_.type(*owner);
                if (pointer.kind != hir::Type::Kind::Pointer ||
                    !pointer.pointee) {
                    return std::nullopt;
                }
                owner = pointer.pointee;
            }
            const auto* member = resolve_member(
                *owner, member_name(*expression.right),
                expression.right->location, false);
            if (member && is_bit_field) {
                *is_bit_field = member->bit_width.has_value();
            }
            return member ? std::optional<hir::TypeId>(
                                qualified_member_type(*owner, *member))
                          : std::nullopt;
        }
        if (access == ObjectAccess::Value) {
            const auto type = infer_type(expression);
            if (type && record_value_type(hir_, *type)) return type;
        }
        return std::nullopt;
    }

    const Expr* vector_lane_expression(const Expr& expression) {
        const auto* node = &expression;
        while (node->kind == Expr::Kind::Parenthesized && node->left) {
            node = node->left.get();
        }
        if (node->kind != Expr::Kind::Binary || node->text != "index" ||
            !node->left || !node->right) return nullptr;
        const auto type = infer_type(*node->left);
        return type && vector_type(hir_, *type) ? node : nullptr;
    }

    ContinuationTask<std::optional<VectorLane>> lower_vector_lane_async(const Expr& expression,
                                                bool require_address = false) {
        const auto type = designator_type(*expression.left);
        if (!type || !vector_type(hir_, *type)) co_return std::nullopt;
        const auto lanes = hir_.type(*type).lanes;
        const auto element = qualified_array_element(*type);
        std::optional<LocalBinding> local;
        std::optional<DesignatorAddress> memory;
        const auto name = local_name(*expression.left);
        const auto* binding = name ? find_local(*name) : nullptr;
        // Keep ordinary and hard-bound vector cells in SSA. Volatile memory
        // lanes instead use scalar addresses, without widening an access to
        // the whole vector. A physical register has no memory address.
        if (!require_address && binding && !binding->dynamic_address &&
            (!hir_.type(*type).is_volatile ||
             current_.slots[binding->slot.value].physical_location)) {
            local = *binding;
        } else {
            memory = co_await lower_designator_address_async(*expression.left);
            if (!memory) co_return std::nullopt;
        }
        const auto index = co_await lower_expression_async(*expression.right);
        if (!index || !integer_type(hir_, current_.values[index->value].type)) {
            co_return std::nullopt;
        }
        const auto& value = current_.values[index->value];
        const auto literal = patch_initial(*expression.right, hir_.address_bits);
        if ((literal && (literal->value.high != 0 || literal->value.low >= lanes)) ||
            (value.kind == ValueKind::ConstantInteger &&
             (value.integer_high != 0 || value.integer >= lanes))) {
            diagnostics_.error(expression.right->location,
                               "fixed-vector lane index is out of range");
            failed_ = true;
            co_return std::nullopt;
        }
        if (memory) {
            const auto base = cast(memory->address, hir_.pointer_to(element),
                                   expression.location);
            memory->address = indexed_address(base, *index, element,
                                               expression.location);
            memory->type = element;
            memory->alignment = std::min(memory->alignment,
                                         storage_alignment(hir_, element, target_));
        }
        co_return VectorLane{local, memory, *index, element};
    }

    std::optional<VectorLane> lower_vector_lane(const Expr& expression,
                                                bool require_address = false) {
        return lower_vector_lane_async(expression, require_address).run();
    }

    std::optional<ValueId> load_vector_lane(const VectorLane& lane,
                                             SourceLocation location) {
        if (lane.memory) {
            return load_pointer(lane.memory->address, location,
                                lane.memory->alignment);
        }
        const auto vector = load_slot(*lane.local, location);
        const auto value = add_value(ValueKind::ExtractElement,
                                     lane.element_type, location);
        current_.values[value.value].operands = {vector, lane.index};
        return value;
    }

    std::optional<ValueId> store_vector_lane(const VectorLane& lane,
                                              ValueId value,
                                              SourceLocation location) {
        if (hir_.type(lane.element_type).is_const) return std::nullopt;
        value = assignment_cast(value, lane.element_type, location);
        if (lane.memory) {
            if (!store_pointer(lane.memory->address, value, location,
                               lane.memory->alignment)) return std::nullopt;
        } else {
            // The RHS may have changed other lanes. Reload the current cell
            // before inserting only the remembered destination lane.
            const auto vector = load_slot(*lane.local, location);
            const auto inserted = add_value(ValueKind::InsertElement,
                                             lane.local->type, location);
            current_.values[inserted.value].operands = {vector, lane.index, value};
            (void)store_slot(*lane.local, inserted, location);
        }
        return value;
    }

    ContinuationTask<std::optional<AtomicLvalue>> lower_atomic_lvalue_async(
        const Expr& expression) {
        const auto object_type = designator_type(expression);
        if (!object_type || !atomic_object_type(hir_, *object_type)) {
            co_return std::nullopt;
        }
        const auto& object = hir_.type(*object_type);
        if (object.is_const) {
            diagnostics_.error(expression.location,
                               "cannot modify a const atomic object");
            failed_ = true;
            co_return std::nullopt;
        }
        if (!lock_free_atomic_type(hir_.unqualified(*object_type))) {
            diagnostics_.error(
                expression.location,
                "selected target '" + std::string(target_.architecture) +
                    "' has no runtime-free atomic operation for " +
                    hir::type_name(hir_, hir_.unqualified(*object_type)));
            failed_ = true;
            co_return std::nullopt;
        }

        if (auto designator = co_await lower_designator_address_async(expression)) {
            const auto natural = access_alignment(
                hir_, *object_type, target_);
            if (designator->alignment < natural) {
                diagnostics_.error(
                    expression.location,
                    "an atomic object cannot be accessed through an under-aligned packed member");
                failed_ = true;
                co_return std::nullopt;
            }
            co_return AtomicLvalue{designator->address, *object_type,
                                object.is_volatile};
        }

        const Expr* source = &expression;
        while (source->kind == Expr::Kind::Parenthesized && source->left) {
            source = source->left.get();
        }
        std::optional<ValueId> address;
        if (source->kind == Expr::Kind::Name) {
            if (const auto* local = find_local(name_key(*source))) {
                if (current_.slots[local->slot.value].physical_location) {
                    diagnostics_.error(
                        source->location,
                        "an atomic object cannot be bound to a machine register");
                    failed_ = true;
                    co_return std::nullopt;
                }
                address = slot_address(*local, source->location);
            } else if (const auto* global = resolve_object(*source);
                       global && global_scalar(*global)) {
                address = global_address(*global, source->location);
            }
        } else if (source->kind == Expr::Kind::Unary &&
                   source->text == "*" && source->left) {
            address = co_await lower_expression_async(*source->left);
        } else if (source->kind == Expr::Kind::Binary &&
                   source->text == "index" && source->left &&
                   source->right) {
            auto base = co_await lower_expression_async(*source->left);
            auto index = co_await lower_expression_async(*source->right);
            if (base && index &&
                integer_type(hir_, current_.values[index->value].type)) {
                address = indexed_address(*base, *index, *object_type,
                                          source->location);
            }
        }
        if (!address) co_return std::nullopt;
        co_return AtomicLvalue{*address, *object_type, object.is_volatile};
    }

    std::optional<AtomicLvalue> lower_atomic_lvalue(
        const Expr& expression) {
        return lower_atomic_lvalue_async(expression).run();
    }

    ValueId atomic_operation(AtomicOperation operation, hir::TypeId type,
                             std::vector<ValueId> operands,
                             MemoryOrder order, SourceLocation location,
                             MemoryOrder failure = MemoryOrder::SeqCst,
                             bool is_volatile = false) {
        const bool fence = operation == AtomicOperation::ThreadFence ||
                           operation == AtomicOperation::SignalFence;
        if (!fence && !operands.empty() &&
            operands.front().value < current_.values.size()) {
            const auto& pointer = hir_.type(
                current_.values[operands.front().value].type);
            if (pointer.kind == hir::Type::Kind::Pointer && pointer.pointee &&
                atomic_object_type(hir_, *pointer.pointee)) {
                const auto value_type = hir_.unqualified(*pointer.pointee);
                if (!lock_free_atomic_type(value_type)) {
                    diagnostics_.error(
                        location,
                        "selected target '" + std::string(target_.architecture) +
                            "' has no runtime-free atomic operation for " +
                            hir::type_name(hir_, value_type));
                    failed_ = true;
                }
            }
        }
        const auto value = add_effectful(ValueKind::Atomic, type, location);
        auto& atomic = current_.values[value.value];
        atomic.atomic = operation;
        atomic.memory_order = order;
        atomic.failure_order = failure;
        atomic.operands = std::move(operands);
        atomic.is_volatile_access = is_volatile;
        return value;
    }

    ValueId load_slot(const LocalBinding& binding, SourceLocation location) {
        if (atomic_object_type(hir_, binding.type)) {
            return atomic_operation(
                AtomicOperation::Load, hir_.unqualified(binding.type),
                {slot_address(binding, location)}, MemoryOrder::SeqCst,
                location, MemoryOrder::SeqCst,
                hir_.type(binding.type).is_volatile);
        }
        const auto value = add_effectful(ValueKind::Load, binding.type, location);
        current_.values[value.value].slot = binding.slot;
        current_.values[value.value].is_volatile_access =
            current_.slots[binding.slot.value].is_volatile;
        return value;
    }

    ValueId store_slot(const LocalBinding& binding, ValueId source,
                       SourceLocation location) {
        if (atomic_object_type(hir_, binding.type)) {
            const auto value_type = hir_.unqualified(binding.type);
            source = cast(source, value_type, location);
            return atomic_operation(
                AtomicOperation::Store, *hir_.builtin(BuiltinType::Void),
                {slot_address(binding, location), source},
                MemoryOrder::SeqCst, location, MemoryOrder::SeqCst,
                hir_.type(binding.type).is_volatile);
        }
        source = assignment_cast(source, binding.type, location);
        const auto value = add_effectful(ValueKind::Store,
                                         *hir_.builtin(BuiltinType::Void), location);
        auto& store = current_.values[value.value];
        store.slot = binding.slot;
        store.is_volatile_access =
            current_.slots[binding.slot.value].is_volatile;
        store.operands.push_back(source);
        return value;
    }

    ValueId load_global(const hir::Object& object, SourceLocation location) {
        if (object.fixed_address)
            return *load_pointer(global_address(object, location), location);
        if (atomic_object_type(hir_, object.type)) {
            return atomic_operation(
                AtomicOperation::Load, hir_.unqualified(object.type),
                {global_address(object, location)}, MemoryOrder::SeqCst,
                location, MemoryOrder::SeqCst,
                hir_.type(object.type).is_volatile);
        }
        const auto value = add_effectful(ValueKind::GlobalLoad, object.type, location);
        auto& load = current_.values[value.value];
        load.object = object.id;
        load.is_volatile_access = hir_.type(object.type).is_volatile;
        return value;
    }

    ValueId store_global(const hir::Object& object, ValueId source,
                         SourceLocation location) {
        if (object.fixed_address)
            return *store_pointer(global_address(object, location), source, location);
        if (atomic_object_type(hir_, object.type)) {
            const auto value_type = hir_.unqualified(object.type);
            source = cast(source, value_type, location);
            return atomic_operation(
                AtomicOperation::Store, *hir_.builtin(BuiltinType::Void),
                {global_address(object, location), source},
                MemoryOrder::SeqCst, location, MemoryOrder::SeqCst,
                hir_.type(object.type).is_volatile);
        }
        source = assignment_cast(source, object.type, location);
        const auto value = add_effectful(ValueKind::GlobalStore,
                                         *hir_.builtin(BuiltinType::Void), location);
        auto& store = current_.values[value.value];
        store.object = object.id;
        store.is_volatile_access = hir_.type(object.type).is_volatile;
        store.operands.push_back(source);
        return value;
    }

    std::optional<ValueId> load_indexed(ValueId base, ValueId index,
                                        SourceLocation location) {
        const auto& pointer = hir_.type(current_.values[base.value].type);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
            !integer_type(hir_, current_.values[index.value].type)) {
            return std::nullopt;
        }
        if (array_type(hir_, *pointer.pointee)) {
            const auto address = indexed_address(base, index, *pointer.pointee,
                                                 location);
            return decay_array_address(address, *pointer.pointee, location);
        }
        if (!managed_value_type(hir_, *pointer.pointee)) {
            return std::nullopt;
        }
        if (atomic_object_type(hir_, *pointer.pointee)) {
            const auto pointee = *pointer.pointee;
            const auto is_volatile = hir_.type(pointee).is_volatile;
            const auto address = indexed_address(base, index,
                                                 pointee, location);
            return atomic_operation(
                AtomicOperation::Load,
                hir_.unqualified(pointee), {address},
                MemoryOrder::SeqCst, location, MemoryOrder::SeqCst,
                is_volatile);
        }
        const auto value = add_effectful(ValueKind::IndexedLoad, *pointer.pointee,
                                         location);
        auto& load = current_.values[value.value];
        load.operands = {base, index};
        load.is_volatile_access = hir_.type(*pointer.pointee).is_volatile;
        return value;
    }

    std::optional<ValueId> load_pointer(ValueId address, SourceLocation location,
                                        unsigned alignment = 0) {
        const auto& pointer = hir_.type(current_.values[address.value].type);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee) {
            return std::nullopt;
        }
        if (array_type(hir_, *pointer.pointee)) {
            return decay_array_address(address, *pointer.pointee, location);
        }
        if (!managed_value_type(hir_, *pointer.pointee)) {
            return std::nullopt;
        }
        if (atomic_object_type(hir_, *pointer.pointee)) {
            const auto pointee = *pointer.pointee;
            const auto is_volatile = hir_.type(pointee).is_volatile;
            return atomic_operation(
                AtomicOperation::Load,
                hir_.unqualified(pointee), {address},
                MemoryOrder::SeqCst, location, MemoryOrder::SeqCst,
                is_volatile);
        }
        const auto value = add_effectful(ValueKind::PointerLoad, *pointer.pointee,
                                         location);
        auto& load = current_.values[value.value];
        load.operands = {address};
        load.is_volatile_access = hir_.type(*pointer.pointee).is_volatile;
        load.memory_alignment = alignment;
        return value;
    }

    std::optional<ValueId> store_pointer(ValueId address, ValueId source,
                                         SourceLocation location,
                                         unsigned alignment = 0) {
        const auto& pointer = hir_.type(current_.values[address.value].type);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
            !managed_value_type(hir_, *pointer.pointee) ||
            hir_.type(*pointer.pointee).is_const) {
            return std::nullopt;
        }
        if (atomic_object_type(hir_, *pointer.pointee)) {
            const auto pointee = *pointer.pointee;
            const auto is_volatile = hir_.type(pointee).is_volatile;
            const auto value_type = hir_.unqualified(pointee);
            source = cast(source, value_type, location);
            return atomic_operation(
                AtomicOperation::Store, *hir_.builtin(BuiltinType::Void),
                {address, source}, MemoryOrder::SeqCst, location,
                MemoryOrder::SeqCst, is_volatile);
        }
        source = assignment_cast(source, *pointer.pointee, location);
        const auto value = add_effectful(ValueKind::PointerStore,
                                         *hir_.builtin(BuiltinType::Void), location);
        auto& store = current_.values[value.value];
        store.operands = {address, source};
        store.is_volatile_access = hir_.type(*pointer.pointee).is_volatile;
        store.memory_alignment = alignment;
        return value;
    }

    ValueId integer_binary(BinaryOperation operation, hir::TypeId type,
                           ValueId left, ValueId right,
                           SourceLocation location) {
        const auto result = add_value(ValueKind::Binary, type, location);
        auto& binary = current_.values[result.value];
        binary.binary = operation;
        binary.operands = {left, right};
        return result;
    }

    ValueId extract_bit_field(ValueId storage, hir::TypeId logical_type,
                              BitFieldAccess field,
                              SourceLocation location) {
        const auto storage_type =
            hir_.unqualified(current_.values[storage.value].type);
        storage = cast(storage, storage_type, location);
        const auto bits = type_bits(hir_, storage_type);
        if (field.offset != 0) {
            const auto amount = constant(field.offset, storage_type,
                                         location);
            storage = integer_binary(BinaryOperation::ShiftRightLogical,
                                     storage_type, storage, amount,
                                     location);
        }
        if (field.width < bits) {
            const auto mask = constant(
                mask_to(bit_not(UInt128{}), field.width), storage_type,
                location);
            storage = integer_binary(BinaryOperation::BitAnd, storage_type,
                                     storage, mask, location);
        }
        if (!signed_type(hir_, logical_type) || field.width == bits) {
            return cast(storage, logical_type, location);
        }
        const auto shift = bits - field.width;
        const auto amount = constant(shift, storage_type, location);
        storage = integer_binary(BinaryOperation::ShiftLeft, storage_type,
                                 storage, amount, location);
        auto signed_value = cast(storage, logical_type, location);
        // Narrow integers can live in wider physical registers. Widening the
        // signed, shifted value first makes its sign bit observable before the
        // arithmetic shift on targets such as MIPS32.
        const auto arithmetic_type = bits < 32
                                         ? *hir_.builtin(BuiltinType::I32)
                                         : logical_type;
        signed_value = cast(signed_value, arithmetic_type, location);
        const auto signed_amount = constant(shift, arithmetic_type, location);
        return cast(integer_binary(BinaryOperation::ShiftRightArithmetic,
                                   arithmetic_type, signed_value,
                                   signed_amount, location),
                    logical_type, location);
    }

    struct LoadedBitField {
        ValueId value;
        ValueId storage;
    };

    std::optional<LoadedBitField> load_bit_field(
        const DesignatorAddress& designator, SourceLocation location) {
        if (!designator.bit_field) return std::nullopt;
        auto storage = load_pointer(designator.address, location,
                                    designator.alignment);
        if (!storage) return std::nullopt;
        current_.values[storage->value].bit_field_region =
            mir::ManagedValue::BitFieldRegion{
                designator.bit_field->width,
                designator.bit_field->offset};
        return LoadedBitField{
            extract_bit_field(*storage, designator.type,
                              *designator.bit_field, location),
            *storage};
    }

    std::optional<ValueId> store_bit_field(
        const DesignatorAddress& designator, ValueId source,
        SourceLocation location,
        std::optional<ValueId> loaded_storage = std::nullopt) {
        if (!designator.bit_field) return std::nullopt;
        source = assignment_cast(source, designator.type, location);
        const auto& pointer =
            hir_.type(current_.values[designator.address.value].type);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee) {
            return std::nullopt;
        }
        const auto storage_type = hir_.unqualified(*pointer.pointee);
        auto source_bits = cast(source, storage_type, location);
        const auto storage_bits = type_bits(hir_, storage_type);
        const auto field = *designator.bit_field;
        const auto value_mask_bits =
            mask_to(bit_not(UInt128{}), field.width);
        if (field.width < storage_bits) {
            const auto value_mask = constant(value_mask_bits, storage_type,
                                             location);
            source_bits = integer_binary(BinaryOperation::BitAnd,
                                         storage_type, source_bits,
                                         value_mask, location);
        }
        auto inserted = source_bits;
        if (field.offset != 0) {
            const auto amount = constant(field.offset, storage_type,
                                         location);
            inserted = integer_binary(BinaryOperation::ShiftLeft,
                                      storage_type, inserted, amount,
                                      location);
        }
        if (field.width != storage_bits || field.offset != 0) {
            auto previous = loaded_storage;
            if (!previous) {
                previous = load_pointer(designator.address, location,
                                        designator.alignment);
                if (previous) {
                    current_.values[previous->value].bit_field_update_read = true;
                }
            }
            if (!previous) return std::nullopt;
            *previous = cast(*previous, storage_type, location);
            const auto field_mask = shift_left(value_mask_bits,
                                               field.offset);
            const auto clear_mask = constant(
                mask_to(bit_not(field_mask), storage_bits), storage_type,
                location);
            const auto cleared = integer_binary(
                BinaryOperation::BitAnd, storage_type, *previous,
                clear_mask, location);
            inserted = integer_binary(BinaryOperation::BitOr, storage_type,
                                      cleared, inserted, location);
        }
        const auto stored = store_pointer(designator.address, inserted,
                                          location, designator.alignment);
        if (!stored) {
            return std::nullopt;
        }
        current_.values[stored->value].bit_field_region =
            mir::ManagedValue::BitFieldRegion{field.width, field.offset};
        return extract_bit_field(source_bits, designator.type,
                                 BitFieldAccess{field.width, 0}, location);
    }

    bool initialize_string_array(const LocalBinding& binding,
                                 const Expr& initializer) {
        const auto& array = hir_.type(binding.type);
        if (array.kind != hir::Type::Kind::Array || !array.element ||
            hir_.type(*array.element).kind != hir::Type::Kind::Builtin ||
            hir_.type(*array.element).builtin != BuiltinType::U8 ||
            initializer.kind != Expr::Kind::String || array.lanes == 0) {
            return false;
        }
        const auto required = initializer.string_value.size() + 1;
        if (required > array.lanes) {
            diagnostics_.error(
                initializer.location,
                "string initializer does not fit in the u8 array");
            failed_ = true;
            return true;
        }

        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const auto source_element = hir_.type(*array.element);
        const auto initialization_element = hir_.add_qualifiers(
            hir_.unqualified(*array.element), false,
            source_element.is_volatile);
        const auto qualified_base = decay_array_address(
            slot_address(binding, initializer.location), binding.type,
            initializer.location);
        const auto base = cast(
            qualified_base, hir_.pointer_to(initialization_element),
            initializer.location);
        const auto store_byte = [&](std::uint64_t index,
                                    std::uint8_t byte) {
            const auto offset = constant(index, uptr, initializer.location);
            const auto address = indexed_address(
                base, offset, initialization_element,
                initializer.location);
            const auto value = constant(byte, initialization_element,
                                        initializer.location);
            if (!store_pointer(address, value, initializer.location, 1)) {
                failed_ = true;
            }
        };
        for (std::size_t index = 0;
             index < initializer.string_value.size(); ++index) {
            store_byte(index, static_cast<std::uint8_t>(
                                  initializer.string_value[index]));
        }
        store_byte(initializer.string_value.size(), 0);

        if (required == array.lanes) return true;
        if (array.lanes - required <= 16) {
            for (std::uint64_t index = required; index < array.lanes;
                 ++index) {
                store_byte(index, 0);
            }
            return true;
        }

        const auto initial = constant(required, uptr, initializer.location);
        const auto preheader = *current_block_;
        const auto test = new_block(initializer.location);
        const auto body = new_block(initializer.location);
        const auto end = new_block(initializer.location);
        terminate(TerminatorKind::Branch, initializer.location,
                  std::nullopt, {test});

        enter(test);
        const auto index = add_value(ValueKind::Phi, uptr,
                                     initializer.location);
        const auto limit = constant(array.lanes, uptr,
                                    initializer.location);
        const auto condition = add_value(ValueKind::Binary,
                                         *hir_.builtin(BuiltinType::Bool),
                                         initializer.location);
        current_.values[condition.value].binary =
            BinaryOperation::UnsignedLess;
        current_.values[condition.value].operands = {index, limit};
        terminate(TerminatorKind::ConditionalBranch,
                  initializer.location, condition, {body, end});

        enter(body);
        const auto address = indexed_address(
            base, index, initialization_element, initializer.location);
        const auto zero = constant(0, initialization_element,
                                   initializer.location);
        if (!store_pointer(address, zero, initializer.location, 1)) {
            failed_ = true;
        }
        const auto one = constant(1, uptr, initializer.location);
        const auto next = add_value(ValueKind::Binary, uptr,
                                    initializer.location);
        current_.values[next.value].binary = BinaryOperation::Add;
        current_.values[next.value].operands = {index, one};
        const auto backedge = *current_block_;
        terminate(TerminatorKind::Branch, initializer.location,
                  std::nullopt, {test});
        current_.values[index.value].incoming = {
            {preheader, initial}, {backedge, next}};
        enter(end);
        return true;
    }

    bool initialize_empty_dynamic_array(const LocalBinding& binding,
                                        SourceLocation location) {
        if (!binding.dynamic_address || !binding.dynamic_size) return false;
        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const auto& array = hir_.type(binding.type);
        const bool is_volatile = array.is_volatile ||
                                 (array.element &&
                                  hir_.type(*array.element).is_volatile);
        const auto byte = hir_.add_qualifiers(
            *hir_.builtin(BuiltinType::U8), false, is_volatile);
        const auto base = cast(*binding.dynamic_address,
                               hir_.pointer_to(byte), location);
        const auto initial = constant(0, uptr, location);
        const auto preheader = *current_block_;
        const auto test = new_block(location);
        const auto body = new_block(location);
        const auto end = new_block(location);
        terminate(TerminatorKind::Branch, location, std::nullopt, {test});

        enter(test);
        const auto index = add_value(ValueKind::Phi, uptr, location);
        const auto condition = add_value(
            ValueKind::Binary, *hir_.builtin(BuiltinType::Bool), location);
        current_.values[condition.value].binary =
            BinaryOperation::UnsignedLess;
        current_.values[condition.value].operands = {
            index, *binding.dynamic_size};
        terminate(TerminatorKind::ConditionalBranch, location,
                  condition, {body, end});

        enter(body);
        const auto address = indexed_address(base, index, byte, location);
        const auto zero = constant(0, byte, location);
        if (!store_pointer(address, zero, location, 1)) return false;
        const auto one = constant(1, uptr, location);
        const auto next = integer_binary(BinaryOperation::Add, uptr,
                                         index, one, location);
        const auto backedge = *current_block_;
        terminate(TerminatorKind::Branch, location, std::nullopt, {test});
        current_.values[index.value].incoming = {
            {preheader, initial}, {backedge, next}};
        enter(end);
        return true;
    }

    bool require_dynamic_array_extent(ValueId count,
                                      std::uint64_t minimum,
                                      SourceLocation location) {
        if (minimum == 0) return true;
        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const auto required = constant(minimum, uptr, location);
        const auto too_small = add_value(
            ValueKind::Binary, *hir_.builtin(BuiltinType::Bool), location);
        current_.values[too_small.value].binary =
            BinaryOperation::UnsignedLess;
        current_.values[too_small.value].operands = {count, required};
        const auto trap = new_block(location);
        const auto ready = new_block(location);
        terminate(TerminatorKind::ConditionalBranch, location, too_small,
                  {trap, ready});

        enter(trap);
        const auto operation = add_effectful(
            ValueKind::Intrinsic, *hir_.builtin(BuiltinType::Void), location);
        current_.values[operation.value].intrinsic =
            IntrinsicOperation::Trap;
        terminate(TerminatorKind::Trap, location, std::nullopt, {});
        enter(ready);
        return true;
    }

    // -fbounds-trap: trap unless `index` selects one of the array's elements
    // or forms its one-past address. The index compares as an unsigned value
    // as wide as an address or the index, so a negative index is out of range.
    void require_subscript_in_bounds(ValueId index, std::uint64_t fixed_count,
                                     std::optional<ValueId> dynamic_count,
                                     bool one_past, SourceLocation location) {
        const auto index_type = current_.values[index.value].type;
        const auto bits = type_bits(hir_, index_type);
        if (const auto& value = current_.values[index.value];
            !dynamic_count && value.kind == ValueKind::ConstantInteger) {
            const UInt128 constant_index{value.integer, value.integer_high};
            const bool negative =
                signed_type(hir_, index_type) && bit(constant_index, bits - 1U);
            if (!negative && (one_past ? !(UInt128{fixed_count} < constant_index)
                                       : constant_index < UInt128{fixed_count}))
                return;
        }
        const auto compare_type = *hir_.builtin(
            bits <= hir_.address_bits ? BuiltinType::Uptr
            : bits <= 64 ? BuiltinType::U64 : BuiltinType::U128);
        const auto wide = cast(index, compare_type, location);
        const auto limit = dynamic_count
            ? cast(*dynamic_count, compare_type, location)
            : constant(UInt128{fixed_count}, compare_type, location);
        const auto in_bounds = add_value(
            ValueKind::Binary, *hir_.builtin(BuiltinType::Bool), location);
        current_.values[in_bounds.value].binary = one_past
            ? BinaryOperation::UnsignedLessEqual : BinaryOperation::UnsignedLess;
        current_.values[in_bounds.value].operands = {wide, limit};
        const auto ready = new_block(location);
        const auto trap = new_block(location);
        terminate(TerminatorKind::ConditionalBranch, location, in_bounds,
                  {ready, trap});
        enter(trap);
        const auto operation = add_effectful(
            ValueKind::Intrinsic, *hir_.builtin(BuiltinType::Void), location);
        current_.values[operation.value].intrinsic = IntrinsicOperation::Trap;
        terminate(TerminatorKind::Trap, location, std::nullopt, {});
        enter(ready);
    }

    bool initialize_aggregate_items(
        ValueId base, hir::TypeId byte, bool object_volatile,
        const initializer::Plan& plan) {
        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const auto store_byte = [&](ValueId index, std::uint8_t value,
                                    SourceLocation location) {
            const auto address = indexed_address(base, index, byte, location);
            const auto stored = constant(value, byte, location);
            if (!store_pointer(address, stored, location, 1)) failed_ = true;
        };

        for (const auto& item : plan.items) {
            if (!item.expression) {
                failed_ = true;
                continue;
            }
            const auto& type = hir_.type(item.type);
            if (type.kind == hir::Type::Kind::Array && type.element &&
                hir_.type(*type.element).kind == hir::Type::Kind::Builtin &&
                hir_.type(*type.element).builtin == BuiltinType::U8 &&
                item.expression->kind == Expr::Kind::String) {
                const auto item_size =
                    hir::layout_size(hir_, item.type, target_).value_or(0);
                const auto required = item.expression->string_value.size() + 1;
                if (required > item_size) {
                    diagnostics_.error(
                        item.expression->location,
                        "string initializer does not fit in the u8 array");
                    failed_ = true;
                    continue;
                }
                for (std::size_t index = 0;
                     index < item.expression->string_value.size(); ++index) {
                    const auto offset = constant(
                        item.offset + index, uptr,
                        item.expression->location);
                    store_byte(offset, static_cast<std::uint8_t>(
                                           item.expression->string_value[index]),
                               item.expression->location);
                }
                store_byte(constant(item.offset + required - 1, uptr,
                                    item.expression->location),
                           0, item.expression->location);
                continue;
            }
            if (type.kind == hir::Type::Kind::Array ||
                type.kind == hir::Type::Kind::Record) {
                diagnostics_.error(
                    item.expression->location,
                    "nested aggregate initialization requires a brace list");
                failed_ = true;
                continue;
            }
            const auto initialization_type = type.is_atomic
                ? hir_.add_qualifiers(item.type, false, object_volatile)
                : hir_.add_qualifiers(hir_.unqualified(item.type), false,
                                      object_volatile || type.is_volatile);
            const auto address_type = item.bit_width
                                          ? bit_field_storage_type(
                                                initialization_type)
                                          : initialization_type;
            const auto offset = constant(item.offset, uptr,
                                         item.expression->location);
            const auto byte_address = indexed_address(
                base, offset, byte, item.expression->location);
            const auto address = cast(
                byte_address, hir_.pointer_to(address_type),
                item.expression->location);
            // Materialize the destination before evaluating the initializer.
            // A call result can occupy the ABI return register, so creating
            // the address afterward would extend that fixed-register value
            // across address arithmetic and can make both store operands
            // alias in native lowering.
            auto value = lower_expression(*item.expression,
                                          initialization_type);
            if (!value) {
                failed_ = true;
                continue;
            }
            *value = assignment_cast(*value, initialization_type,
                                     item.expression->location);
            if (item.bit_width) {
                const DesignatorAddress designator{
                    address, initialization_type, item.alignment,
                    BitFieldAccess{*item.bit_width, item.bit_offset}};
                if (!store_bit_field(designator, *value,
                                     item.expression->location)) {
                    failed_ = true;
                }
                continue;
            }
            if (atomic_object_type(hir_, item.type)) {
                (void)atomic_operation(
                    AtomicOperation::Store,
                    *hir_.builtin(BuiltinType::Void), {address, *value},
                    MemoryOrder::SeqCst, item.expression->location,
                    MemoryOrder::SeqCst,
                    hir_.type(initialization_type).is_volatile);
                continue;
            }
            const auto operation = add_effectful(
                ValueKind::PointerStore,
                *hir_.builtin(BuiltinType::Void),
                item.expression->location);
            auto& store = current_.values[operation.value];
            store.operands = {address, *value};
            store.is_volatile_access =
                hir_.type(initialization_type).is_volatile;
            store.memory_alignment = item.alignment;
        }
        return !failed_;
    }

    bool initialize_dynamic_array(const LocalBinding& binding,
                                  const Expr& source, ValueId count) {
        if (!binding.dynamic_address || !binding.dynamic_size) return false;
        const auto& array = hir_.type(binding.type);
        if (array.kind != hir::Type::Kind::Array || !array.element) {
            return false;
        }
        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const bool object_volatile =
            array.is_volatile || hir_.type(*array.element).is_volatile;
        const auto byte = hir_.add_qualifiers(
            *hir_.builtin(BuiltinType::U8), false, object_volatile);
        const auto base = cast(*binding.dynamic_address,
                               hir_.pointer_to(byte), source.location);

        if (source.kind == Expr::Kind::String &&
            hir_.type(*array.element).kind == hir::Type::Kind::Builtin &&
            hir_.type(*array.element).builtin == BuiltinType::U8) {
            const auto required = source.string_value.size() + 1U;
            if (!require_dynamic_array_extent(count, required,
                                              source.location) ||
                !initialize_empty_dynamic_array(binding, source.location)) {
                return false;
            }
            for (std::size_t index = 0;
                 index < source.string_value.size(); ++index) {
                const auto address = indexed_address(
                    base, constant(index, uptr, source.location), byte,
                    source.location);
                const auto value = constant(
                    static_cast<std::uint8_t>(source.string_value[index]),
                    byte, source.location);
                if (!store_pointer(address, value, source.location, 1)) {
                    return false;
                }
            }
            const auto terminator = indexed_address(
                base, constant(source.string_value.size(), uptr,
                               source.location),
                byte, source.location);
            if (!store_pointer(
                    terminator, constant(0, byte, source.location),
                    source.location, 1)) {
                return false;
            }
            return true;
        }

        const auto plan = initializer::build_dynamic_array(
            source, binding.type, hir_, target_, diagnostics_);
        if (!plan.valid) {
            failed_ = true;
            return true;
        }
        if (!require_dynamic_array_extent(count, plan.minimum_elements,
                                          source.location) ||
            !initialize_empty_dynamic_array(binding, source.location)) {
            return false;
        }
        return initialize_aggregate_items(base, byte, object_volatile, plan);
    }

    bool initialize_aggregate(const LocalBinding& binding,
                              const Expr& source) {
        const auto plan = initializer::build(
            source, binding.type, hir_, target_, diagnostics_);
        if (!plan.valid) {
            failed_ = true;
            return true;
        }
        const auto size = hir::layout_size(hir_, binding.type, target_);
        if (!size || *size == 0) {
            diagnostics_.error(source.location,
                               "aggregate initializer has no fixed storage size");
            failed_ = true;
            return true;
        }
        const auto uptr = *hir_.builtin(BuiltinType::Uptr);
        const bool object_volatile =
            hir_.type(binding.type).is_volatile;
        const auto byte = hir_.add_qualifiers(
            *hir_.builtin(BuiltinType::U8), false, object_volatile);
        const auto base = cast(slot_address(binding, source.location),
                               hir_.pointer_to(byte), source.location);
        const auto store_byte = [&](ValueId index, std::uint8_t value,
                                    SourceLocation location) {
            const auto address = indexed_address(base, index, byte, location);
            const auto stored = constant(value, byte, location);
            if (!store_pointer(address, stored, location, 1)) failed_ = true;
        };

        if (*size <= 16) {
            for (std::uint64_t index = 0; index < *size; ++index) {
                store_byte(constant(index, uptr, source.location), 0,
                           source.location);
            }
        } else {
            const auto initial = constant(0, uptr, source.location);
            const auto preheader = *current_block_;
            const auto test = new_block(source.location);
            const auto body = new_block(source.location);
            const auto end = new_block(source.location);
            terminate(TerminatorKind::Branch, source.location,
                      std::nullopt, {test});

            enter(test);
            const auto index = add_value(ValueKind::Phi, uptr,
                                         source.location);
            const auto limit = constant(*size, uptr, source.location);
            const auto condition = add_value(
                ValueKind::Binary, *hir_.builtin(BuiltinType::Bool),
                source.location);
            current_.values[condition.value].binary =
                BinaryOperation::UnsignedLess;
            current_.values[condition.value].operands = {index, limit};
            terminate(TerminatorKind::ConditionalBranch, source.location,
                      condition, {body, end});

            enter(body);
            store_byte(index, 0, source.location);
            const auto one = constant(1, uptr, source.location);
            const auto next = add_value(ValueKind::Binary, uptr,
                                        source.location);
            current_.values[next.value].binary = BinaryOperation::Add;
            current_.values[next.value].operands = {index, one};
            const auto backedge = *current_block_;
            terminate(TerminatorKind::Branch, source.location,
                      std::nullopt, {test});
            current_.values[index.value].incoming = {
                {preheader, initial}, {backedge, next}};
            enter(end);
        }

        return initialize_aggregate_items(base, byte, object_volatile, plan);
    }

    void end_lifetimes_from(std::size_t retained_scopes,
                            SourceLocation location) {
        if (!current_block_) return;
        for (std::size_t scope_index = scopes_.size();
             scope_index > retained_scopes; --scope_index) {
            const auto& slots = scopes_[scope_index - 1].slots;
            for (auto slot = slots.rbegin(); slot != slots.rend(); ++slot) {
                (void)lifetime(ValueKind::LifetimeEnd, *slot, location);
            }
            if (const auto mark =
                    scopes_[scope_index - 1].dynamic_stack_mark) {
                (void)dynamic_stack_restore(*mark, location);
            }
        }
    }

    // Runs at a normal return, after the return value is saved.
    void copy_out_parameters(SourceLocation location) {
        for (const auto& [cell, pointer] : copy_outs_) {
            const auto value = load_slot(cell, location);
            current_.values[value.value].copy_out_read = true;
            if (!store_pointer(pointer, value, location)) failed_ = true;
        }
    }

    std::optional<hir::TypeId> atomic_pointee(const Expr& expression) {
        const auto pointer_id = infer_type(expression);
        if (!pointer_id) return std::nullopt;
        const auto& pointer = hir_.type(*pointer_id);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
            !atomic_object_type(hir_, *pointer.pointee)) {
            return std::nullopt;
        }
        return pointer.pointee;
    }

    // Memoized for the current bindings: lower_statement and every new
    // binding discard the results.
    std::optional<hir::TypeId> infer_type(const Expr& expression) {
        if (const auto found = inferred_types_.find(&expression);
            found != inferred_types_.end())
            return found->second;
        const auto type = compute_type(expression);
        inferred_types_.emplace(&expression, type);
        return type;
    }

    void forget_types() {
        if (!inferred_types_.empty()) inferred_types_ = {};
    }

    std::optional<hir::TypeId> compute_type(const Expr& expression) {
        switch (expression.kind) {
        case Expr::Kind::VoidValue: return hir_.builtin(BuiltinType::Void);
        case Expr::Kind::Quote: return std::nullopt;
        case Expr::Kind::ByteSequence: return std::nullopt;
        case Expr::Kind::Address:
            return expression.type ? std::optional<hir::TypeId>(hir_.intern_type(expression.type))
                                   : std::nullopt;
        case Expr::Kind::Integer: {
            const auto parsed = patch_initial(expression, hir_.address_bits);
            return parsed
                       ? std::optional<hir::TypeId>(
                             expression.type
                                 ? hir_.intern_type(expression.type)
                                 : *hir_.builtin(parsed->type))
                       : std::nullopt;
        }
        case Expr::Kind::Floating: {
            const auto parsed = parse_floating(expression,
                                               hir_.address_bits);
            return parsed ? hir_.builtin(parsed->type) : std::nullopt;
        }
        case Expr::Kind::Name: {
            if (const auto* local = find_local(name_key(expression))) {
                if (array_type(hir_, local->type)) {
                    if (local->dynamic_address) {
                        return hir_.pointer_to(
                            *hir_.type(local->type).element);
                    }
                    return hir_.pointer_to(qualified_array_element(local->type));
                }
                return atomic_object_type(hir_, local->type)
                           ? std::optional<hir::TypeId>(
                                 hir_.unqualified(local->type))
                           : std::optional<hir::TypeId>(local->type);
            }
            const auto found = parameter_values_.find(name_key(expression));
            if (found != parameter_values_.end()) {
                return current_.values[found->second.value].type;
            }
            const auto* object = resolve_object(expression);
            if (object && global_object(*object)) {
                if (array_type(hir_, object->type)) {
                    return hir_.pointer_to(qualified_array_element(object->type));
                }
                return atomic_object_type(hir_, object->type)
                           ? std::optional<hir::TypeId>(
                                 hir_.unqualified(object->type))
                           : std::optional<hir::TypeId>(object->type);
            }
            if (const auto* function = resolve_function(expression)) {
                return function_pointer_type(*function);
            }
            return resolve_label(expression)
                       ? hir_.builtin(BuiltinType::Label) : std::nullopt;
        }
        case Expr::Kind::Parenthesized:
            return expression.left ? infer_type(*expression.left) : std::nullopt;
        case Expr::Kind::Cast:
            return expression.type
                       ? std::optional<hir::TypeId>(
                             hir_.intern_type(expression.type))
                       : std::nullopt;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
        case Expr::Kind::Offsetof:
            return hir_.builtin(BuiltinType::Uptr);
        case Expr::Kind::Unary:
            if (expression.text == "++" || expression.text == "--" ||
                expression.text == "post++" || expression.text == "post--") {
                const auto name = expression.left ? local_name(*expression.left)
                                                  : std::nullopt;
                const auto* local = name ? find_local(*name) : nullptr;
                if (local) {
                    if (array_type(hir_, local->type)) {
                        return std::nullopt;
                    }
                    return atomic_object_type(hir_, local->type)
                               ? std::optional<hir::TypeId>(
                                     hir_.unqualified(local->type))
                               : std::optional<hir::TypeId>(local->type);
                }
                const auto designator = expression.left
                                            ? designator_type(*expression.left)
                                            : std::nullopt;
                return designator && atomic_object_type(hir_, *designator)
                           ? std::optional<hir::TypeId>(
                                 hir_.unqualified(*designator))
                           : designator;
            }
            if (!expression.left) return std::nullopt;
            if (expression.text == "&") {
                if (expression.left->kind == Expr::Kind::Unary &&
                    expression.left->text == "*" && expression.left->left) {
                    const auto pointer = infer_type(*expression.left->left);
                    return pointer && pointer_type(hir_, *pointer)
                               ? pointer : std::nullopt;
                }
                if (const auto designator =
                        designator_type(*expression.left)) {
                    return hir_.pointer_to(*designator);
                }
                const auto name = local_name(*expression.left);
                if (!name) return std::nullopt;
                if (const auto* local = find_local(*name)) {
                    return hir_.pointer_to(local->type);
                }
                const auto parameter = parameter_values_.find(*name);
                if (parameter != parameter_values_.end()) {
                    return hir_.pointer_to(
                        current_.values[parameter->second.value].type);
                }
                if (const auto* object = resolve_object(*expression.left);
                    object && global_object(*object)) {
                    return hir_.pointer_to(object->type);
                }
                if (const auto* function = resolve_function(*expression.left))
                    return function_pointer_type(*function);
                return std::nullopt;
            }
            if (const auto operand = infer_type(*expression.left)) {
                if (expression.text == "!") {
                    return vector_type(hir_, *operand)
                               ? vector_mask_type(*operand)
                               : hir_.builtin(BuiltinType::Bool);
                }
                if (expression.text == "*") {
                    const auto& pointer = hir_.type(*operand);
                    if (pointer.kind != hir::Type::Kind::Pointer ||
                        !pointer.pointee) {
                        return std::nullopt;
                    }
                    if (hir_.type(*pointer.pointee).kind ==
                        hir::Type::Kind::Function)
                        return operand;
                    if (array_type(hir_, *pointer.pointee)) {
                        return hir_.pointer_to(
                            qualified_array_element(*pointer.pointee));
                    }
                    if (!managed_value_type(hir_, *pointer.pointee)) {
                        return std::nullopt;
                    }
                    return atomic_object_type(hir_, *pointer.pointee)
                               ? std::optional<hir::TypeId>(
                                     hir_.unqualified(*pointer.pointee))
                               : pointer.pointee;
                }
                if (floating_type(hir_, *operand)) {
                    return expression.text == "~" ? std::nullopt : operand;
                }
                return promote(*operand);
            }
            return std::nullopt;
        case Expr::Kind::Binary: {
            if (expression.text == "member" ||
                expression.text == "pointer_member") {
                const auto type = designator_type(expression, nullptr, ObjectAccess::Value);
                if (!type) return std::nullopt;
                const auto& member = hir_.type(*type);
                if (member.kind == hir::Type::Kind::Array &&
                    member.element) {
                    return hir_.pointer_to(qualified_array_element(*type));
                }
                return atomic_object_type(hir_, *type)
                           ? std::optional<hir::TypeId>(
                                 hir_.unqualified(*type))
                           : type;
            }
            if (expression.text == "index") {
                if (!expression.left || !expression.right) return std::nullopt;
                const auto base = infer_type(*expression.left);
                const auto index = infer_type(*expression.right);
                if (!base || !index || !integer_type(hir_, *index)) {
                    return std::nullopt;
                }
                const auto& aggregate = hir_.type(*base);
                if (aggregate.kind == hir::Type::Kind::Vector) {
                    return aggregate.element;
                }
                if (aggregate.kind != hir::Type::Kind::Pointer ||
                    !aggregate.pointee) {
                    return std::nullopt;
                }
                if (array_type(hir_, *aggregate.pointee)) {
                    return hir_.pointer_to(
                        qualified_array_element(*aggregate.pointee));
                }
                return managed_value_type(hir_, *aggregate.pointee)
                           ? (atomic_object_type(hir_, *aggregate.pointee)
                                  ? std::optional<hir::TypeId>(
                                        hir_.unqualified(*aggregate.pointee))
                                  : aggregate.pointee)
                           : std::nullopt;
            }
            if (expression.text == "&&" || expression.text == "||") {
                return hir_.builtin(BuiltinType::Bool);
            }
            const auto left = infer_type(*expression.left);
            const auto right = infer_type(*expression.right);
            if (!left || !right) return std::nullopt;
            const bool left_pointer = pointer_type(hir_, *left);
            const bool right_pointer = pointer_type(hir_, *right);
            if (expression.text == "+" && left_pointer &&
                integer_type(hir_, *right)) {
                return left;
            }
            if (expression.text == "+" && right_pointer &&
                integer_type(hir_, *left)) {
                return right;
            }
            if (expression.text == "-" && left_pointer &&
                integer_type(hir_, *right)) {
                return left;
            }
            if (expression.text == "-" && left_pointer && right_pointer &&
                representation_compatible(hir_, *left, *right)) {
                return hir_.builtin(BuiltinType::Iptr);
            }
            if (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == "<=" ||
                expression.text == ">" || expression.text == ">=") {
                const auto common = common_type(*left, *right);
                return common && vector_type(hir_, *common)
                           ? vector_mask_type(*common)
                           : hir_.builtin(BuiltinType::Bool);
            }
            if (expression.text == "<<" || expression.text == ">>") {
                return promote(*left);
            }
            return common_type(*left, *right);
        }
        case Expr::Kind::Conditional: {
            const auto then_type = infer_type(*expression.right);
            const auto else_type = infer_type(*expression.third);
            return then_type && else_type ? conditional_type(*then_type, *else_type)
                                          : std::nullopt;
        }
        case Expr::Kind::Assign: {
            if (!expression.left) return std::nullopt;
            const Expr* target = expression.left.get();
            while (target->kind == Expr::Kind::Parenthesized && target->left)
                target = target->left.get();
            if (target->kind == Expr::Kind::Binary &&
                target->text == "index") return infer_type(*target);
            const auto type = designator_type(*target);
            if (!type || !managed_value_type(hir_, *type) ||
                hir_.type(*type).is_const) return std::nullopt;
            return atomic_object_type(hir_, *type)
                ? std::optional<hir::TypeId>(hir_.unqualified(*type))
                : type;
        }
        case Expr::Kind::Call: {
            if (expression.left) {
                const auto target = infer_type(*expression.left);
                if (target) {
                    const auto& pointer = hir_.type(*target);
                    if (pointer.kind == hir::Type::Kind::Pointer &&
                        pointer.pointee &&
                        hir_.type(*pointer.pointee).function) {
                        return hir_.type(*pointer.pointee)
                            .function->result_type;
                    }
                }
            }
            if (!expression.left || expression.left->kind != Expr::Kind::Name) {
                return std::nullopt;
            }
            if (expression.left->text == "$::patch") {
                if (expression.arguments.empty()) return std::nullopt;
                const auto initial = patch_initial(
                    *expression.arguments.front(), hir_.address_bits);
                return initial ? hir_.builtin(initial->type)
                               : infer_type(*expression.arguments.front());
            }
            if (expression.left->text == "$::alignof") {
                return hir_.builtin(BuiltinType::Uptr);
            }
            if (expression.left->text == "$::expect") {
                return expression.arguments.size() == 2
                           ? infer_type(*expression.arguments.front())
                           : std::nullopt;
            }
            if (expression.left->text == "$::assume" ||
                expression.left->text == "$::unreachable" ||
                expression.left->text == "$::trap") {
                return hir_.builtin(BuiltinType::Void);
            }
            if (expression.left->text == "$::atomic_thread_fence" ||
                expression.left->text == "$::atomic_signal_fence" ||
                expression.left->text == "$::atomic_store") {
                return hir_.builtin(BuiltinType::Void);
            }
            if (expression.left->text == "$::atomic_compare_exchange" ||
                expression.left->text == "$::atomic_is_lock_free") {
                return hir_.builtin(BuiltinType::Bool);
            }
            if (atomic_intrinsic(expression.left->text)) {
                if (expression.arguments.empty()) return std::nullopt;
                const auto pointee = atomic_pointee(
                    *expression.arguments.front());
                return pointee ? std::optional<hir::TypeId>(
                                     hir_.unqualified(*pointee))
                               : std::nullopt;
            }
            const auto* function = resolve_function(*expression.left);
            return function && callable(*function)
                       ? std::optional<hir::TypeId>(function->result_type)
                       : std::nullopt;
        }
        case Expr::Kind::Character: {
            return hir_.builtin(BuiltinType::U32);
        }
        case Expr::Kind::String:
            return std::nullopt;
        case Expr::Kind::AggregateInitializer:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<hir::TypeId> promote(hir::TypeId type) {
        if (vector_integer_type(hir_, type)) return type;
        if (!integer_type(hir_, type)) return std::nullopt;
        return type_bits(hir_, type) < 32 ? hir_.builtin(BuiltinType::I32)
                                          : std::optional<hir::TypeId>(type);
    }

    std::optional<hir::TypeId> conditional_type(hir::TypeId left, hir::TypeId right) {
        const bool a = pointer_type(hir_, left), b = pointer_type(hir_, right);
        if (a && b) return hir_.common_pointer_type(left, right);
        // Source validation owns the integer-zero proof, retained on the source arm.
        // Destination-typed lowering applies the selected null representation.
        if (a && integer_type(hir_, right)) return hir_.unqualified(left);
        if (b && integer_type(hir_, left)) return hir_.unqualified(right);
        return common_type(left, right);
    }

    std::optional<hir::TypeId> common_type(hir::TypeId left, hir::TypeId right) {
        if (representation_compatible(hir_, left, right) &&
            hir_.type(left).kind == hir::Type::Kind::Record) {
            return left;
        }
        const bool left_vector = vector_type(hir_, left);
        const bool right_vector = vector_type(hir_, right);
        if (left_vector || right_vector) {
            const auto vector = left_vector ? left : right;
            const auto scalar = left_vector ? right : left;
            const auto& shape = hir_.type(vector);
            if (left_vector && right_vector) {
                const auto& other = hir_.type(scalar);
                if (shape.lanes != other.lanes ||
                    shape.scalable != other.scalable || !shape.element ||
                    !other.element) {
                    return std::nullopt;
                }
                const auto element = common_type(*shape.element,
                                                 *other.element);
                return element
                           ? std::optional<hir::TypeId>(
                                 hir_.vector_of(*element, shape.lanes,
                                                shape.scalable))
                           : std::nullopt;
            }
            if (!shape.element ||
                (!integer_type(hir_, scalar) &&
                 !floating_type(hir_, scalar))) {
                return std::nullopt;
            }
            const auto element = common_type(*shape.element, scalar);
            return element
                       ? std::optional<hir::TypeId>(
                             hir_.vector_of(*element, shape.lanes,
                                            shape.scalable))
                       : std::nullopt;
        }
        if (pointer_type(hir_, left) && pointer_type(hir_, right))
            return hir_.common_pointer_type(left, right);
        if (representation_compatible(hir_, left, right) && label_type(hir_, left)) return left;
        if (floating_type(hir_, left) && floating_type(hir_, right)) {
            return type_bits(hir_, left) >= type_bits(hir_, right) ? left : right;
        }
        if (floating_type(hir_, left) && integer_type(hir_, right)) {
            return left;
        }
        if (integer_type(hir_, left) && floating_type(hir_, right)) {
            return right;
        }
        if (!integer_type(hir_, left) || !integer_type(hir_, right)) return std::nullopt;
        left = *promote(left);
        right = *promote(right);
        if (left == right) return left;
        const IntegerType lhs{type_bits(hir_, left), signed_type(hir_, left)};
        const IntegerType rhs{type_bits(hir_, right), signed_type(hir_, right)};
        const auto common = common_integer_type(lhs, rhs);
        return common.bits == lhs.bits && common.is_signed == lhs.is_signed ? left : right;
    }

    std::optional<hir::TypeId> vector_mask_type(hir::TypeId vector) {
        const auto& type = hir_.type(vector);
        if (type.kind != hir::Type::Kind::Vector || !type.element) {
            return std::nullopt;
        }
        BuiltinType mask = BuiltinType::I8;
        switch (type_bits(hir_, *type.element)) {
        case 8: mask = BuiltinType::I8; break;
        case 16: mask = BuiltinType::I16; break;
        case 32: mask = BuiltinType::I32; break;
        case 64: mask = BuiltinType::I64; break;
        default: return std::nullopt;
        }
        return hir_.vector_of(*hir_.builtin(mask), type.lanes,
                              type.scalable);
    }

    ValueId cast(ValueId source, hir::TypeId destination, SourceLocation location) {
        const auto source_type = current_.values[source.value].type;
        if (source_type == destination) {
            return source;
        }
        if (hir_.type(destination).kind == hir::Type::Kind::Builtin &&
            hir_.type(destination).builtin == BuiltinType::Bool) {
            source = booleanize(source, location);
            if (current_.values[source.value].type == destination) return source;
        }
        if (representation_compatible(hir_, source_type, destination)) {
            const auto value = add_value(ValueKind::Cast, destination, location);
            auto& cast_value = current_.values[value.value];
            cast_value.operands.push_back(source);
            cast_value.cast = CastOperation::Reinterpret;
            return value;
        }
        if (hir_.type(source_type).kind == hir::Type::Kind::Record ||
            hir_.type(destination).kind == hir::Type::Kind::Record) {
            diagnostics_.error(
                location,
                "a record value requires the same nominal record type");
            failed_ = true;
            return source;
        }
        if (vector_type(hir_, destination) &&
            !vector_type(hir_, source_type)) {
            const auto& vector = hir_.type(destination);
            source = cast(source, *vector.element, location);
            const auto value = add_value(ValueKind::Splat, destination,
                                         location);
            current_.values[value.value].operands.push_back(source);
            return value;
        }
        if (vector_type(hir_, source_type) &&
            vector_type(hir_, destination)) {
            const auto& source_vector = hir_.type(source_type);
            const auto& target_vector = hir_.type(destination);
            if (source_vector.lanes != target_vector.lanes ||
                source_vector.scalable != target_vector.scalable) {
                diagnostics_.error(
                    location,
                    "vector conversion requires matching lane counts and "
                    "scalability");
                failed_ = true;
                return source;
            }
            const auto source_element = *source_vector.element;
            const auto target_element = *target_vector.element;
            const auto value = add_value(ValueKind::Cast, destination,
                                         location);
            auto& cast_value = current_.values[value.value];
            cast_value.operands.push_back(source);
            const auto source_bits = type_bits(hir_, source_element);
            const auto target_bits = type_bits(hir_, target_element);
            if (integer_type(hir_, source_element) &&
                floating_type(hir_, target_element)) {
                cast_value.cast = signed_type(hir_, source_element)
                                      ? CastOperation::SignedIntegerToFloat
                                      : CastOperation::UnsignedIntegerToFloat;
            } else if (floating_type(hir_, source_element) &&
                       integer_type(hir_, target_element)) {
                cast_value.cast = signed_type(hir_, target_element)
                                      ? CastOperation::FloatToSignedInteger
                                      : CastOperation::FloatToUnsignedInteger;
            } else if (floating_type(hir_, source_element) &&
                       floating_type(hir_, target_element)) {
                cast_value.cast = source_bits < target_bits
                                      ? CastOperation::FloatExtend
                                      : CastOperation::FloatTruncate;
            } else {
                cast_value.cast = source_bits < target_bits
                                      ? (signed_type(hir_, source_element)
                                             ? CastOperation::SignExtend
                                             : CastOperation::ZeroExtend)
                                  : source_bits > target_bits
                                      ? CastOperation::Truncate
                                      : CastOperation::Reinterpret;
            }
            return value;
        }
        if (floating_type(hir_, source_type) && floating_type(hir_, destination)) {
            const auto value = add_value(ValueKind::Cast, destination, location);
            auto& cast_value = current_.values[value.value];
            cast_value.operands.push_back(source);
            const auto from_bits = type_bits(hir_, source_type);
            const auto to_bits = type_bits(hir_, destination);
            cast_value.cast = from_bits < to_bits ? CastOperation::FloatExtend
                            : from_bits > to_bits ? CastOperation::FloatTruncate
                                                  : CastOperation::Reinterpret;
            return value;
        }
        if (integer_type(hir_, source_type) &&
            floating_type(hir_, destination)) {
            const auto value = add_value(ValueKind::Cast, destination, location);
            auto& cast_value = current_.values[value.value];
            cast_value.operands.push_back(source);
            cast_value.cast = signed_type(hir_, source_type)
                                  ? CastOperation::SignedIntegerToFloat
                                  : CastOperation::UnsignedIntegerToFloat;
            return value;
        }
        if (floating_type(hir_, source_type) &&
            integer_type(hir_, destination)) {
            const auto value = add_value(ValueKind::Cast, destination, location);
            auto& cast_value = current_.values[value.value];
            cast_value.operands.push_back(source);
            cast_value.cast = signed_type(hir_, destination)
                                  ? CastOperation::FloatToSignedInteger
                                  : CastOperation::FloatToUnsignedInteger;
            return value;
        }
        const auto source_bits = type_bits(hir_, source_type);
        const auto destination_bits = type_bits(hir_, destination);
        const auto value = add_value(ValueKind::Cast, destination, location);
        auto& cast_value = current_.values[value.value];
        cast_value.operands.push_back(source);
        cast_value.cast = source_bits < destination_bits
                              ? (signed_type(hir_, source_type)
                                     ? CastOperation::SignExtend
                                     : CastOperation::ZeroExtend)
                              : source_bits > destination_bits
                                    ? CastOperation::Truncate
                                    : CastOperation::Reinterpret;
        return value;
    }

    bool compatible_pointer_conversion(hir::TypeId source,
                                       hir::TypeId destination,
                                       unsigned depth = 0,
                                       bool intermediate_const = true) const {
        bool immediate = depth == 0;
        for (;;) {
            const auto& from = hir_.type(source);
            const auto& to = hir_.type(destination);
            if ((from.is_const && !to.is_const) ||
                (from.is_volatile && !to.is_volatile) ||
                from.is_atomic != to.is_atomic) return false;
            if (!immediate && !intermediate_const &&
                ((!from.is_const && to.is_const) ||
                 (!from.is_volatile && to.is_volatile))) return false;
            if (immediate &&
                (void_type(hir_, source) || void_type(hir_, destination))) {
                return from.kind != hir::Type::Kind::Function &&
                       to.kind != hir::Type::Kind::Function;
            }
            immediate = false;
            if (from.kind == hir::Type::Kind::Pointer &&
                to.kind == hir::Type::Kind::Pointer && from.pointee && to.pointee) {
                if (from.address_space != to.address_space) return false;
                intermediate_const = intermediate_const && to.is_const;
                source = *from.pointee;
                destination = *to.pointee;
                continue;
            }
            if ((from.kind == hir::Type::Kind::Array || from.kind == hir::Type::Kind::Vector) &&
                from.kind == to.kind && from.element && to.element) {
                if (from.lanes != to.lanes || from.scalable != to.scalable) return false;
                source = *from.element;
                destination = *to.element;
                continue;
            }
            return representation_compatible(hir_, source, destination);
        }
    }

    ValueId assignment_cast(ValueId source, hir::TypeId destination,
                            SourceLocation location) {
        const auto& from = hir_.type(current_.values[source.value].type);
        const auto& to = hir_.type(destination);
        if (from.kind == hir::Type::Kind::Pointer &&
            to.kind == hir::Type::Kind::Pointer && from.pointee && to.pointee &&
            (from.address_space != to.address_space ||
             !compatible_pointer_conversion(*from.pointee, *to.pointee))) {
            diagnostics_.error(location,
                "implicit pointer conversion changes address space, discards qualifiers, or uses incompatible pointee types");
            failed_ = true;
            return source;
        }
        return cast(source, destination, location);
    }

    ValueId constant(UInt128 integer, hir::TypeId type, SourceLocation location) {
        const auto value = add_value(ValueKind::ConstantInteger, type, location);
        current_.values[value.value].integer = integer.low;
        current_.values[value.value].integer_high = integer.high;
        return value;
    }

    ValueId floating_constant(std::uint64_t bits, hir::TypeId type,
                              std::uint64_t bits_high,
                              SourceLocation location) {
        const auto value = add_value(ValueKind::ConstantFloating, type, location);
        current_.values[value.value].integer = bits;
        current_.values[value.value].integer_high = bits_high;
        return value;
    }

    std::optional<ValueId> null_pointer_constant(hir::TypeId type,
                                                 SourceLocation location) {
        const auto* space = find_address_space(target_, hir_.type(type).address_space);
        if (!space || !space->native_lowering) {
            diagnostics_.error(location,
                "pointer conversion has no native address-space representation");
            return std::nullopt;
        }
        const auto bits = space->pointer_bits ? space->pointer_bits : hir_.address_bits;
        const UInt128 null{space->null_low, space->null_high};
        if (bits != type_bits(hir_, type) || !fits_unsigned(null, bits)) {
            diagnostics_.error(location,
                "null pointer representation does not fit the pointer type");
            return std::nullopt;
        }
        return constant(null, type, location);
    }

    ValueId booleanize(ValueId source, SourceLocation location) {
        const auto type = current_.values[source.value].type;
        if (!scalar_type(hir_, type)) {
            diagnostics_.error(location,
                               "a non-scalar value cannot be used as a condition");
            failed_ = true;
            return constant(0, *hir_.builtin(BuiltinType::Bool), location);
        }
        std::optional<ValueId> zero;
        if (pointer_type(hir_, type)) zero = null_pointer_constant(type, location);
        else zero = floating_type(hir_, type)
            ? floating_constant(0, type, 0, location) : constant(0, type, location);
        if (!zero) {
            failed_ = true;
            return constant(0, *hir_.builtin(BuiltinType::Bool), location);
        }
        const auto result = add_value(ValueKind::Binary,
                                      *hir_.builtin(BuiltinType::Bool), location);
        auto& value = current_.values[result.value];
        value.binary = BinaryOperation::NotEqual;
        value.operands = {source, *zero};
        return result;
    }

    ContinuationTask<std::optional<ValueId>> lower_expression_async(const Expr& expression,
                                            std::optional<hir::TypeId> destination = {}) {
        if (!current_block_) co_return std::nullopt;
        if (destination && pointer_type(hir_, *destination) && expression.evaluated_integer &&
            expression.evaluated_integer->value == UInt128{}) {
            const auto source = infer_type(expression);
            if (source && integer_type(hir_, *source))
                co_return null_pointer_constant(*destination, expression.location);
        }
        std::optional<ValueId> result;
        switch (expression.kind) {
        case Expr::Kind::VoidValue:
            result = add_value(ValueKind::VoidValue, *hir_.builtin(BuiltinType::Void),
                               expression.location);
            break;
        case Expr::Kind::Quote:
            diagnostics_.error(expression.location,
                "translation-time token quotation cannot enter runtime lowering");
            co_return std::nullopt;
        case Expr::Kind::ByteSequence:
            diagnostics_.error(expression.location,
                "materialized bytes cannot enter runtime lowering");
            co_return std::nullopt;
        case Expr::Kind::Offsetof:
            diagnostics_.error(expression.location,
                "$::offsetof was not resolved before runtime lowering");
            co_return std::nullopt;
        case Expr::Kind::Address: {
            if (!expression.type || !expression.evaluated_address) break;
            const auto type = hir_.intern_type(expression.type);
            const auto& address = *expression.evaluated_address;
            if (address.kind == AddressConstant::Kind::Absolute) {
                result = constant(address.absolute, type, expression.location);
                break;
            }
            if (address.kind == AddressConstant::Kind::Object && address.object) {
                if (const auto* object = hir_.object(*address.object))
                    result = global_address(*object, expression.location);
            } else if (address.kind == AddressConstant::Kind::Function && address.function) {
                if (const auto* function = hir_.function(*address.function))
                    result = function_address(*function, expression.location);
            }
            if (!result) break;
            if (address.addend != 0) {
                const auto byte = *hir_.builtin(BuiltinType::U8);
                const auto base = cast(*result, hir_.pointer_to(byte), expression.location);
                const auto offset = constant(static_cast<std::uint64_t>(address.addend),
                                             *hir_.builtin(BuiltinType::Iptr), expression.location);
                result = indexed_address(base, offset, byte, expression.location);
            }
            result = cast(*result, type, expression.location);
            break;
        }
        case Expr::Kind::Integer: {
            const auto parsed = patch_initial(expression, hir_.address_bits);
            if (!parsed) break;
            const auto type = expression.type
                                  ? std::optional<hir::TypeId>(
                                        hir_.intern_type(expression.type))
                                  : hir_.builtin(parsed->type);
            if (!type) break;
            result = constant(parsed->value, *type, expression.location);
            break;
        }
        case Expr::Kind::Floating: {
            const auto parsed = parse_floating(expression,
                                               hir_.address_bits);
            if (!parsed) break;
            const auto type = hir_.builtin(parsed->type);
            if (!type) break;
            result = floating_constant(parsed->bits, *type,
                                       parsed->bits_high,
                                       expression.location);
            break;
        }
        case Expr::Kind::Name: {
            if (const auto* local = find_local(name_key(expression))) {
                if (array_type(hir_, local->type)) {
                    result = local->dynamic_address
                                 ? *local->dynamic_address
                                 : decay_array_address(
                                       slot_address(*local,
                                                    expression.location),
                                       local->type, expression.location);
                } else if (managed_value_type(hir_, local->type)) {
                    result = load_slot(*local, expression.location);
                }
            } else {
                const auto found = parameter_values_.find(name_key(expression));
                if (found != parameter_values_.end()) {
                    result = found->second;
                } else if (const auto* object = resolve_object(expression);
                           object && global_object(*object)) {
                    if (array_type(hir_, object->type)) {
                        result = decay_array_address(
                            global_address(*object, expression.location),
                            object->type, expression.location);
                    } else if (managed_value_type(hir_, object->type)) {
                        result = load_global(*object, expression.location);
                    }
                } else if (const auto* label =
                               resolve_label(expression)) {
                    result = label_address(*label, expression.location);
                } else if (const auto* function =
                               resolve_function(expression)) {
                    result = function_address(*function, expression.location);
                } else if (resolved_label_binding(expression).kind == LabelBinding::Kind::Reference) {
                    const auto location = expression.name_context &&
                        expression.name_context->last_component_location.valid()
                            ? expression.name_context->last_component_location : expression.location;
                    diagnostics_.error(location,
                        "label address does not name a visible label in its retained source binding");
                }
            }
            break;
        }
        case Expr::Kind::Parenthesized:
            if (expression.left) result = co_await lower_expression_async(*expression.left);
            break;
        case Expr::Kind::Cast: {
            if (!expression.left || !expression.type) break;
            const auto destination_type = hir_.intern_type(expression.type);
            if (pointer_type(hir_, destination_type) && expression.left->evaluated_integer &&
                expression.left->evaluated_integer->value == UInt128{}) {
                const auto source_type = infer_type(*expression.left);
                if (source_type && integer_type(hir_, *source_type)) {
                    result = null_pointer_constant(destination_type, expression.location);
                    break;
                }
            }
            auto source = co_await lower_expression_async(*expression.left);
            if (!source) break;
            const auto source_type = current_.values[source->value].type;
            // An explicit scalar-to-vector cast splats, like the implicit conversion.
            const bool splat = vector_type(hir_, destination_type) &&
                (integer_type(hir_, source_type) || floating_type(hir_, source_type));
            if (!splat && (!scalar_type(hir_, source_type) ||
                           !scalar_type(hir_, destination_type)) &&
                !representation_compatible(hir_, source_type,
                                           destination_type)) {
                diagnostics_.error(
                    expression.location,
                    "explicit cast requires arithmetic, pointer, or compatible aggregate types");
                break;
            }
            const auto source_pointer = pointer_type(hir_, source_type);
            const auto destination_pointer =
                pointer_type(hir_, destination_type);
            const bool destination_bool =
                hir_.type(destination_type).kind == hir::Type::Kind::Builtin &&
                hir_.type(destination_type).builtin == BuiltinType::Bool;
            if (source_pointer && destination_pointer) {
                const auto& source_pointer_type = hir_.type(source_type);
                const auto& destination_pointer_type =
                    hir_.type(destination_type);
                // An explicit cast reinterprets one function pointer as another.
                const bool functions = source_pointer_type.pointee && destination_pointer_type.pointee &&
                    hir_.type(*source_pointer_type.pointee).kind == hir::Type::Kind::Function &&
                    hir_.type(*destination_pointer_type.pointee).kind == hir::Type::Kind::Function;
                if (!source_pointer_type.pointee ||
                    !destination_pointer_type.pointee ||
                    source_pointer_type.address_space !=
                        destination_pointer_type.address_space ||
                    (!functions && !compatible_pointer_conversion(
                        *source_pointer_type.pointee,
                        *destination_pointer_type.pointee))) {
                    diagnostics_.error(
                        expression.location,
                        "explicit pointer conversion discards qualifiers or "
                        "uses incompatible pointee types");
                    break;
                }
            } else if ((source_pointer || destination_pointer) &&
                       !destination_bool &&
                       !integer_type(hir_, source_pointer ? destination_type
                                                         : source_type)) {
                diagnostics_.error(
                    expression.location,
                    "explicit cast cannot convert between a pointer and a non-integer type");
                break;
            } else if (!destination_bool &&
                       ((source_pointer &&
                         type_bits(hir_, destination_type) <
                             hir_.address_bits) ||
                        (destination_pointer &&
                         type_bits(hir_, source_type) < hir_.address_bits))) {
                diagnostics_.error(
                    expression.location,
                    "pointer casts require an integer at least as wide as the target address");
                break;
            }
            result = cast(*source, destination_type, expression.location);
            break;
        }
        case Expr::Kind::Sizeof: {
            if (!expression.type && expression.left) {
                const auto name = local_name(*expression.left);
                const auto* local = name ? find_local(*name) : nullptr;
                if (local && local->dynamic_size) {
                    result = *local->dynamic_size;
                    break;
                }
            }
            std::optional<hir::TypeId> queried;
            bool bit_field{};
            if (expression.type) {
                queried = hir_.intern_type(expression.type);
            } else if (expression.left) {
                queried = designator_type(*expression.left, &bit_field, ObjectAccess::Value);
                if (!queried) queried = infer_type(*expression.left);
            }
            if (!queried) break;
            if (bit_field) {
                diagnostics_.error(expression.location,
                                   "sizeof cannot be applied to a bit-field");
                break;
            }
            const auto& type = hir_.type(*queried);
            const auto invalid =
                type.kind == hir::Type::Kind::Function ||
                (type.kind == hir::Type::Kind::Builtin &&
                 type.builtin == BuiltinType::Void) ||
                (type.kind == hir::Type::Kind::Vector && type.scalable) ||
                (type.kind == hir::Type::Kind::Record &&
                 (!type.record || !hir_.record(*type.record).complete));
            const auto size = storage_size(hir_, *queried, target_);
            if (invalid || size == 0) {
                diagnostics_.error(expression.location,
                                   "sizeof requires a complete object type with fixed size");
                break;
            }
            result = constant(UInt128{size},
                              *hir_.builtin(BuiltinType::Uptr),
                              expression.location);
            break;
        }
        case Expr::Kind::Alignof: {
            std::optional<hir::TypeId> queried;
            bool bit_field{};
            if (expression.type) {
                queried = hir_.intern_type(expression.type);
            } else if (expression.left) {
                queried = designator_type(*expression.left, &bit_field, ObjectAccess::Value);
                if (!queried) queried = infer_type(*expression.left);
            }
            if (!queried) break;
            if (bit_field) {
                diagnostics_.error(
                    expression.location,
                    "$::alignof cannot be applied to a bit-field");
                break;
            }
            const auto& type = hir_.type(*queried);
            const auto invalid =
                type.kind == hir::Type::Kind::Function ||
                (type.kind == hir::Type::Kind::Builtin &&
                 type.builtin == BuiltinType::Void) ||
                (type.kind == hir::Type::Kind::Record &&
                 (!type.record || !hir_.record(*type.record).complete));
            if (invalid) {
                diagnostics_.error(
                    expression.location,
                    "$::alignof requires a complete object type");
                break;
            }
            result = constant(
                UInt128{storage_alignment(hir_, *queried, target_)},
                *hir_.builtin(BuiltinType::Uptr), expression.location);
            break;
        }
        case Expr::Kind::Unary: {
            if (expression.text == "&") {
                const Expr* operand = expression.left.get();
                while (operand &&
                       operand->kind == Expr::Kind::Parenthesized &&
                       operand->left) {
                    operand = operand->left.get();
                }
                if (operand && operand->kind == Expr::Kind::Unary &&
                    operand->text == "*" && operand->left) {
                    result = co_await lower_expression_async(*operand->left);
                    break;
                }
                if (operand) {
                    // A register object, including a lane of a register
                    // vector, has no address.
                    const Expr* root = operand;
                    if (root->kind == Expr::Kind::Binary && root->text == "index" &&
                        root->left) {
                        const Expr* base = root->left.get();
                        while (base->kind == Expr::Kind::Parenthesized && base->left)
                            base = base->left.get();
                        if (const auto type = infer_type(*base); type && vector_type(hir_, *type))
                            root = base;
                    }
                    const auto name = local_name(*root);
                    const auto* local = name ? find_local(*name) : nullptr;
                    if (local && !local->dynamic_address &&
                        (local->register_storage ||
                         current_.slots[local->slot.value].physical_location)) {
                        diagnostics_.error(expression.location,
                                           "a register object has no address");
                        failed_ = true;
                        break;
                    }
                }
                if (operand) {
                    const auto* enclosing = std::exchange(one_past_subscript_, operand);
                    auto designator = co_await lower_designator_address_async(*operand);
                    one_past_subscript_ = enclosing;
                    if (designator) {
                        if (designator->bit_field) {
                            diagnostics_.error(
                                expression.location,
                                "cannot take the address of a bit-field");
                            failed_ = true;
                            break;
                        }
                        const auto natural = access_alignment(
                            hir_, designator->type, target_);
                        if (designator->alignment < natural) {
                            diagnostics_.warning(
                                expression.location,
                                "forming a pointer to a potentially under-aligned packed member");
                        }
                        result = designator->address;
                        break;
                    }
                }
                if (operand && operand->kind == Expr::Kind::Binary &&
                    operand->text == "index" && operand->left &&
                    operand->right) {
                    const auto pointee = designator_type(*operand);
                    auto base = co_await lower_expression_async(*operand->left);
                    auto index = co_await lower_expression_async(*operand->right);
                    if (pointee && base && index &&
                        integer_type(hir_,
                                     current_.values[index->value].type)) {
                        result = indexed_address(*base, *index, *pointee,
                                                 expression.location);
                    }
                    break;
                }
                const auto name = operand ? local_name(*operand)
                                          : std::nullopt;
                const auto* found = name ? find_local(*name) : nullptr;
                if (found) {
                    if (found->dynamic_address) {
                        result = cast(*found->dynamic_address,
                                      hir_.pointer_to(found->type),
                                      expression.location);
                    } else {
                        auto& slot = current_.slots[found->slot.value];
                        slot.address_taken = true;
                        result = add_value(ValueKind::SlotAddress,
                                           hir_.pointer_to(found->type),
                                           expression.location);
                        current_.values[result->value].slot = found->slot;
                    }
                    break;
                }
                if (name) {
                    if (const auto* object = resolve_object(*expression.left);
                        object && global_object(*object)) {
                        result = global_address(*object, expression.location);
                    } else if (const auto* function = resolve_function(*expression.left)) {
                        result =
                            function_address(*function, expression.location);
                    }
                }
                break;
            }
            if (expression.text == "++" || expression.text == "--" ||
                expression.text == "post++" || expression.text == "post--") {
                if (const auto* node = vector_lane_expression(*expression.left)) {
                    const auto lane = co_await lower_vector_lane_async(*node);
                    if (!lane) break;
                    const auto old = load_vector_lane(*lane, expression.location);
                    if (!old) break;
                    const auto one = unit_value(lane->element_type,
                                                 expression.location);
                    const auto updated = compound_value(
                        expression.text.ends_with("++") ? "+=" : "-=",
                        *old, one, lane->element_type, expression.location);
                    if (!updated || !store_vector_lane(*lane, *updated,
                                                       expression.location)) break;
                    result = expression.text.starts_with("post") ? *old : *updated;
                    break;
                }
                const auto object_type = expression.left
                                             ? designator_type(*expression.left)
                                             : std::nullopt;
                if (object_type && atomic_object_type(hir_, *object_type)) {
                    const auto value_type = hir_.unqualified(*object_type);
                    if (!integer_type(hir_, value_type) &&
                        !floating_type(hir_, value_type)) {
                        diagnostics_.error(
                            expression.location,
                            "atomic update requires an integer or floating object");
                        break;
                    }
                    auto lvalue = co_await lower_atomic_lvalue_async(*expression.left);
                    if (!lvalue) break;
                    const auto one = unit_value(value_type,
                                                expression.location);
                    result = atomic_update(
                        *lvalue, one,
                        expression.text == "++" ||
                                expression.text == "post++"
                            ? "+=" : "-=",
                        expression.location,
                        expression.text.starts_with("post"));
                    break;
                }
                const auto name = local_name(*expression.left);
                const auto* found = name ? find_local(*name) : nullptr;
                if (!found) {
                    auto designator =
                        co_await lower_designator_address_async(*expression.left);
                    if (designator && hir_.type(designator->type).is_const) {
                        diagnostics_.error(expression.location, "cannot write a const subobject");
                        failed_ = true;
                        break;
                    }
                    if (!designator ||
                        !managed_value_type(hir_, designator->type) ||
                        hir_.type(designator->type).is_const ||
                        (!integer_type(hir_, designator->type) &&
                         !floating_type(hir_, designator->type) &&
                         !pointer_type(hir_, designator->type))) {
                        break;
                    }
                    std::optional<ValueId> old;
                    std::optional<ValueId> loaded_storage;
                    if (designator->bit_field) {
                        auto loaded = load_bit_field(*designator,
                                                     expression.location);
                        if (!loaded) break;
                        old = loaded->value;
                        loaded_storage = loaded->storage;
                    } else {
                        old = load_pointer(
                            designator->address, expression.location,
                            designator->alignment);
                    }
                    if (!old) break;
                    std::optional<ValueId> updated;
                    if (pointer_type(hir_, designator->type)) {
                        const auto one = constant(
                            UInt128{1}, *hir_.builtin(BuiltinType::Iptr),
                            expression.location);
                        updated = pointer_offset(
                            *old, designator->type, one,
                            expression.text == "++" ||
                                expression.text == "post++",
                            expression.location);
                    } else {
                        const auto one = unit_value(
                            designator->type, expression.location);
                        updated = compound_value(
                            expression.text.ends_with("++") ? "+=" : "-=",
                            *old, one, designator->type, expression.location);
                    }
                    if (!updated) break;
                    const auto stored = designator->bit_field
                                            ? store_bit_field(
                                                  *designator, *updated,
                                                  expression.location,
                                                  loaded_storage)
                                            : store_pointer(
                                                  designator->address,
                                                  *updated,
                                                  expression.location,
                                                  designator->alignment);
                    if (!stored) {
                        break;
                    }
                    result = expression.text.starts_with("post")
                                 ? *old
                                 : (designator->bit_field ? *stored
                                                          : *updated);
                    break;
                }
                if (found->dynamic_address ||
                    array_type(hir_, found->type)) break;
                const auto binding = *found;
                const auto old = load_slot(binding, expression.location);
                if (pointer_type(hir_, binding.type)) {
                    const auto one = constant(
                        UInt128{1}, *hir_.builtin(BuiltinType::Iptr),
                        expression.location);
                    const auto updated = pointer_offset(
                        old, binding.type, one,
                        expression.text == "++" ||
                            expression.text == "post++",
                        expression.location);
                    if (!updated) break;
                    (void)store_slot(binding, *updated,
                                     expression.location);
                    result = expression.text.starts_with("post")
                                 ? old
                                 : *updated;
                    break;
                }
                auto one_type = binding.type;
                if (vector_type(hir_, binding.type)) {
                    one_type = *hir_.type(binding.type).element;
                }
                auto one = unit_value(one_type, expression.location);
                if (one_type != binding.type) {
                    one = cast(one, binding.type, expression.location);
                }
                const auto updated = compound_value(
                    expression.text.ends_with("++") ? "+=" : "-=",
                    old, one, binding.type, expression.location);
                if (!updated) break;
                (void)store_slot(binding, *updated, expression.location);
                result = expression.text.starts_with("post") ? old : *updated;
                break;
            }
            auto operand = co_await lower_expression_async(*expression.left);
            if (!operand) break;
            if (expression.text == "*") {
                const auto& pointer =
                    hir_.type(current_.values[operand->value].type);
                result = pointer.kind == hir::Type::Kind::Pointer &&
                                 pointer.pointee &&
                                 hir_.type(*pointer.pointee).kind ==
                                     hir::Type::Kind::Function
                             ? operand
                             : load_pointer(*operand, expression.location);
                break;
            }
            const auto operand_type = current_.values[operand->value].type;
            const bool operand_vector = vector_type(hir_, operand_type);
            if (expression.text == "!" &&
                pointer_type(hir_, operand_type)) {
                // Test against the selected null encoding before negating;
                // neither an uptr cast nor a raw zero test defines pointer truth.
                operand = booleanize(*operand, expression.location);
            } else if (!floating_type(hir_, operand_type) &&
                       !operand_vector) {
                const auto promoted = promote(operand_type);
                if (!promoted) break;
                operand = cast(*operand, *promoted, expression.location);
            }
            if (operand_vector && expression.text == "~" &&
                !vector_integer_type(hir_, operand_type)) {
                break;
            }
            if (expression.text == "+") {
                result = *operand;
                break;
            }
            const auto type = expression.text == "!"
                                  ? (operand_vector
                                         ? *vector_mask_type(operand_type)
                                         : *hir_.builtin(BuiltinType::Bool))
                                  : current_.values[operand->value].type;
            const auto value = add_value(ValueKind::Unary, type, expression.location);
            auto& unary = current_.values[value.value];
            unary.operands.push_back(*operand);
            unary.unary = expression.text == "-" ? UnaryOperation::Negate
                          : expression.text == "~" ? UnaryOperation::BitNot
                                                   : UnaryOperation::IsZero;
            result = value;
            break;
        }
        case Expr::Kind::Binary:
            if (expression.text == "member" ||
                expression.text == "pointer_member") {
                auto designator = co_await lower_designator_address_async(expression, ObjectAccess::Value);
                if (designator) {
                    const auto& type = hir_.type(designator->type);
                    if (type.kind == hir::Type::Kind::Array &&
                        type.element) {
                        result = decay_array_address(
                            designator->address, designator->type,
                            expression.location);
                    } else if (managed_value_type(hir_,
                                                  designator->type)) {
                        if (designator->bit_field) {
                            const auto loaded = load_bit_field(
                                *designator, expression.location);
                            if (loaded) result = loaded->value;
                        } else {
                            result = load_pointer(
                                designator->address, expression.location,
                                designator->alignment);
                        }
                    }
                }
            } else if (expression.text == "index") {
                const auto aggregate_type =
                    infer_type(*expression.left);
                if (aggregate_type && vector_type(hir_, *aggregate_type) &&
                    designator_type(*expression.left)) {
                    const auto lane = co_await lower_vector_lane_async(expression);
                    if (lane) result = load_vector_lane(*lane, expression.location);
                    break;
                }
                if (!aggregate_type ||
                    !vector_type(hir_, *aggregate_type)) {
                    if (auto designator =
                            co_await lower_designator_address_async(expression, ObjectAccess::Value);
                        designator &&
                        (managed_value_type(hir_, designator->type) ||
                         array_type(hir_, designator->type))) {
                        result = load_pointer(
                            designator->address, expression.location,
                            designator->alignment);
                        break;
                    }
                }
                auto base = co_await lower_expression_async(*expression.left);
                auto index = co_await lower_expression_async(*expression.right);
                if (base && index &&
                    vector_type(hir_, current_.values[base->value].type)) {
                    const auto& vector =
                        hir_.type(current_.values[base->value].type);
                    const auto& index_value = current_.values[index->value];
                    if (index_value.kind == ValueKind::ConstantInteger &&
                        (index_value.integer_high != 0 ||
                         index_value.integer >= vector.lanes)) {
                        diagnostics_.error(expression.right->location,
                                           "fixed-vector lane index is out of range");
                        failed_ = true;
                        break;
                    }
                    result = add_value(ValueKind::ExtractElement,
                                       *vector.element,
                                       expression.location);
                    current_.values[result->value].operands = {*base, *index};
                } else if (base && index) {
                    result = load_indexed(*base, *index,
                                          expression.location);
                }
            } else {
                if (expression.text == "&&" || expression.text == "||")
                    result = co_await lower_logical_async(expression);
                else
                    result = co_await lower_binary_async(expression);
            }
            break;
        case Expr::Kind::Conditional:
            result = co_await lower_conditional_async(expression);
            break;
        case Expr::Kind::Assign:
            result = co_await lower_assignment_async(expression);
            break;
        case Expr::Kind::Call:
            result = co_await lower_call_async(expression);
            break;
        case Expr::Kind::Character: {
            const auto value = decode_character_literal(expression.text);
            if (value) result = constant(UInt128{*value}, *hir_.builtin(BuiltinType::U32), expression.location);
            break;
        }
        case Expr::Kind::String:
            break;
        case Expr::Kind::AggregateInitializer:
            diagnostics_.error(
                expression.location,
                "an aggregate initializer is not a scalar expression");
            break;
        }
        if (!result) {
            failed_ = true;
            co_return std::nullopt;
        }
        co_return destination ? std::optional<ValueId>(assignment_cast(*result, *destination,
                                                         expression.location))
                           : result;
    }

    std::optional<ValueId> lower_expression(const Expr& expression,
                                            std::optional<hir::TypeId> destination = {}) {
        return lower_expression_async(expression, destination).run();
    }

    ContinuationTask<std::optional<ValueId>> lower_binary_async(const Expr& expression) {
        const auto left_type = infer_type(*expression.left);
        const auto right_type = infer_type(*expression.right);
        if (!left_type || !right_type) co_return std::nullopt;
        const bool left_pointer = pointer_type(hir_, *left_type);
        const bool right_pointer = pointer_type(hir_, *right_type);
        if ((expression.text == "==" || expression.text == "!=") && left_pointer != right_pointer) {
            const auto& integer = left_pointer ? *expression.right : *expression.left;
            const auto integer_type_id = left_pointer ? *right_type : *left_type;
            if (!integer_type(hir_, integer_type_id) || !integer.evaluated_integer ||
                integer.evaluated_integer->value != UInt128{}) {
                diagnostics_.error(integer.location,
                    "pointer/integer equality requires an integer constant zero");
                co_return std::nullopt;
            }
            const auto pointer_type_id = left_pointer ? *left_type : *right_type;
            const auto pointer = co_await lower_expression_async(left_pointer ? *expression.left : *expression.right,
                                                   pointer_type_id);
            if (!pointer) co_return std::nullopt;
            const auto zero = null_pointer_constant(pointer_type_id, integer.location);
            if (!zero) co_return std::nullopt;
            const auto result = add_value(ValueKind::Binary, *hir_.builtin(BuiltinType::Bool), expression.location);
            auto& value = current_.values[result.value];
            value.binary = expression.text == "==" ? BinaryOperation::Equal : BinaryOperation::NotEqual;
            value.operands = left_pointer ? std::vector<ValueId>{*pointer, *zero}
                                          : std::vector<ValueId>{*zero, *pointer};
            co_return result;
        }
        const bool addition = expression.text == "+";
        const bool subtraction = expression.text == "-";
        if ((addition || subtraction) &&
            (left_pointer || right_pointer)) {
            if (left_pointer && right_pointer) {
                if (!subtraction || !representation_compatible(hir_, *left_type, *right_type)) {
                    diagnostics_.error(
                        expression.location,
                        "pointer subtraction requires matching pointer types");
                    co_return std::nullopt;
                }
                const auto& pointer = hir_.type(*left_type);
                if (!pointer.pointee ||
                    storage_size(hir_, *pointer.pointee, target_) == 0) {
                    diagnostics_.error(
                        expression.location,
                        "pointer subtraction requires a complete pointed-to object type");
                    co_return std::nullopt;
                }
                auto left = co_await lower_expression_async(*expression.left);
                auto right = co_await lower_expression_async(*expression.right);
                if (!left || !right) co_return std::nullopt;
                const auto iptr = *hir_.builtin(BuiltinType::Iptr);
                left = cast(*left, iptr, expression.left->location);
                right = cast(*right, iptr, expression.right->location);
                const auto bytes = add_value(ValueKind::Binary, iptr,
                                             expression.location);
                auto& difference = current_.values[bytes.value];
                difference.binary = BinaryOperation::Subtract;
                difference.operands = {*left, *right};
                const auto element_size =
                    storage_size(hir_, *pointer.pointee, target_);
                if (element_size == 1) co_return bytes;
                const auto divisor = constant(UInt128{element_size}, iptr,
                                              expression.location);
                const auto result = add_value(ValueKind::Binary, iptr,
                                              expression.location);
                auto& quotient = current_.values[result.value];
                quotient.binary = BinaryOperation::SignedDivide;
                quotient.operands = {bytes, divisor};
                co_return result;
            }

            const bool pointer_on_left = left_pointer;
            if ((!pointer_on_left && !addition) ||
                !integer_type(hir_, pointer_on_left ? *right_type
                                                   : *left_type)) {
                diagnostics_.error(
                    expression.location,
                    "pointer arithmetic requires one pointer and one integer operand");
                co_return std::nullopt;
            }
            const auto pointer_type_id =
                pointer_on_left ? *left_type : *right_type;
            const auto& pointer = hir_.type(pointer_type_id);
            if (!pointer.pointee ||
                storage_size(hir_, *pointer.pointee, target_) == 0) {
                diagnostics_.error(
                    expression.location,
                    "pointer arithmetic requires a complete pointed-to object type");
                co_return std::nullopt;
            }
            auto base = co_await lower_expression_async(
                *(pointer_on_left ? expression.left : expression.right));
            auto index = co_await lower_expression_async(
                *(pointer_on_left ? expression.right : expression.left));
            if (!base || !index) co_return std::nullopt;
            const auto iptr = *hir_.builtin(BuiltinType::Iptr);
            index = cast(*index, iptr,
                         pointer_on_left ? expression.right->location
                                         : expression.left->location);
            if (subtraction) {
                const auto negated = add_value(ValueKind::Unary, iptr,
                                               expression.location);
                auto& unary = current_.values[negated.value];
                unary.unary = UnaryOperation::Negate;
                unary.operands.push_back(*index);
                index = negated;
            }
            co_return indexed_address(*base, *index, *pointer.pointee,
                                   expression.location);
        }
        const bool shift = expression.text == "<<" || expression.text == ">>";
        const bool floating = floating_type(hir_, *left_type) ||
                              floating_type(hir_, *right_type) ||
                              vector_floating_type(hir_, *left_type) ||
                              vector_floating_type(hir_, *right_type);
        if (floating && shift) {
            co_return std::nullopt;
        }
        const auto common = shift ? promote(*left_type)
                                  : common_type(*left_type, *right_type);
        if (!common) co_return std::nullopt;
        auto left = co_await lower_expression_async(*expression.left, *common);
        std::optional<ValueId> right;
        if (shift) right = co_await lower_expression_async(*expression.right);
        else right = co_await lower_expression_async(*expression.right, *common);
        if (!left || !right) co_return std::nullopt;
        if (shift) {
            const auto right_promoted = promote(current_.values[right->value].type);
            if (!right_promoted) co_return std::nullopt;
            right = cast(*right, *right_promoted, expression.right->location);
            right = cast(*right, *common, expression.right->location);
        }
        const bool comparison = expression.text == "==" || expression.text == "!=" ||
                                expression.text == "<" || expression.text == "<=" ||
                                expression.text == ">" || expression.text == ">=";
        const bool pointer = pointer_type(hir_, *left_type) ||
                             pointer_type(hir_, *right_type);
        if (pointer &&
            (!pointer_type(hir_, *left_type) ||
             !pointer_type(hir_, *right_type) ||
             !comparison)) {
            co_return std::nullopt;
        }
        if (floating && expression.text == "%") {
            diagnostics_.error(
                expression.location,
                "floating remainder has no runtime-free lowering on this target");
            co_return std::nullopt;
        }
        if (floating && expression.text != "+" && expression.text != "-" &&
            expression.text != "*" && expression.text != "/" && !comparison) {
            co_return std::nullopt;
        }
        const auto result_type = comparison && vector_type(hir_, *common)
                                     ? *vector_mask_type(*common)
                                 : comparison
                                     ? *hir_.builtin(BuiltinType::Bool)
                                     : *common;
        const auto result = add_value(ValueKind::Binary, result_type, expression.location);
        auto& value = current_.values[result.value];
        value.operands = {*left, *right};
        const bool sign = signed_type(hir_, *common);
        if (expression.text == "+") value.binary = BinaryOperation::Add;
        else if (expression.text == "-") value.binary = BinaryOperation::Subtract;
        else if (expression.text == "*") value.binary = BinaryOperation::Multiply;
        else if (expression.text == "/") value.binary = floating ? BinaryOperation::SignedDivide
                                                                    : sign ? BinaryOperation::SignedDivide
                                                                           : BinaryOperation::UnsignedDivide;
        else if (expression.text == "%") value.binary = sign ? BinaryOperation::SignedRemainder
                                                               : BinaryOperation::UnsignedRemainder;
        else if (expression.text == "&") value.binary = BinaryOperation::BitAnd;
        else if (expression.text == "|") value.binary = BinaryOperation::BitOr;
        else if (expression.text == "^") value.binary = BinaryOperation::BitXor;
        else if (expression.text == "<<") value.binary = BinaryOperation::ShiftLeft;
        else if (expression.text == ">>") value.binary = sign
            ? BinaryOperation::ShiftRightArithmetic : BinaryOperation::ShiftRightLogical;
        else if (expression.text == "==") value.binary = BinaryOperation::Equal;
        else if (expression.text == "!=") value.binary = BinaryOperation::NotEqual;
        else if (expression.text == "<") value.binary = sign || floating
            ? BinaryOperation::SignedLess : BinaryOperation::UnsignedLess;
        else if (expression.text == "<=") value.binary = sign || floating
            ? BinaryOperation::SignedLessEqual : BinaryOperation::UnsignedLessEqual;
        else if (expression.text == ">") value.binary = sign || floating
            ? BinaryOperation::SignedGreater : BinaryOperation::UnsignedGreater;
        else if (expression.text == ">=") value.binary = sign || floating
            ? BinaryOperation::SignedGreaterEqual : BinaryOperation::UnsignedGreaterEqual;
        else co_return std::nullopt;
        co_return result;
    }

    std::optional<ValueId> lower_binary(const Expr& expression) {
        return lower_binary_async(expression).run();
    }

    std::optional<BinaryOperation> compound_operation(
        std::string_view spelling, hir::TypeId type,
        SourceLocation location) {
        const bool floating = floating_type(hir_, type) ||
                              vector_floating_type(hir_, type);
        const bool sign = signed_type(hir_, type);
        if (spelling == "+=") return BinaryOperation::Add;
        if (spelling == "-=") return BinaryOperation::Subtract;
        if (spelling == "*=") return BinaryOperation::Multiply;
        if (spelling == "/=") {
            return floating || sign ? BinaryOperation::SignedDivide
                                    : BinaryOperation::UnsignedDivide;
        }
        if (spelling == "%=") {
            if (floating) {
                diagnostics_.error(
                    location,
                    "floating remainder has no runtime-free lowering on this target");
                return std::nullopt;
            }
            return sign ? BinaryOperation::SignedRemainder
                        : BinaryOperation::UnsignedRemainder;
        }
        if (spelling == "&=") {
            return floating ? std::nullopt
                            : std::optional<BinaryOperation>(
                                  BinaryOperation::BitAnd);
        }
        if (spelling == "^=") {
            return floating ? std::nullopt
                            : std::optional<BinaryOperation>(
                                  BinaryOperation::BitXor);
        }
        if (spelling == "|=") {
            return floating ? std::nullopt
                            : std::optional<BinaryOperation>(
                                  BinaryOperation::BitOr);
        }
        if (spelling == "<<=") {
            return floating ? std::nullopt
                            : std::optional<BinaryOperation>(
                                  BinaryOperation::ShiftLeft);
        }
        if (spelling == ">>=") {
            return floating
                       ? std::nullopt
                       : std::optional<BinaryOperation>(
                             sign ? BinaryOperation::ShiftRightArithmetic
                                  : BinaryOperation::ShiftRightLogical);
        }
        return std::nullopt;
    }

    std::optional<ValueId> compound_value(std::string_view spelling,
                                           ValueId left, ValueId right,
                                           hir::TypeId destination,
                                           SourceLocation location) {
        const auto left_type = current_.values[left.value].type;
        const auto right_type = current_.values[right.value].type;
        const bool shift = spelling == "<<=" || spelling == ">>=";
        if (shift && (!integer_type(hir_, right_type) &&
                      !vector_integer_type(hir_, right_type))) return std::nullopt;
        const auto common = shift ? promote(left_type)
                                  : common_type(left_type, right_type);
        if (!common) return std::nullopt;
        const auto operation = compound_operation(spelling, *common, location);
        if (!operation) return std::nullopt;
        left = cast(left, *common, location);
        if (shift) {
            const auto promoted = promote(right_type);
            if (!promoted) return std::nullopt;
            right = cast(right, *promoted, location);
        }
        right = cast(right, *common, location);
        const auto result = add_value(ValueKind::Binary, *common, location);
        current_.values[result.value].operands = {left, right};
        current_.values[result.value].binary = *operation;
        return assignment_cast(result, destination, location);
    }

    ValueId unit_value(hir::TypeId type, SourceLocation location) {
        const auto& value = hir_.type(type);
        if (value.kind == hir::Type::Kind::Builtin &&
            value.builtin == BuiltinType::F32) {
            return floating_constant(0x3f800000U, type, 0, location);
        }
        if (value.kind == hir::Type::Kind::Builtin &&
            (value.builtin == BuiltinType::F64 ||
             value.builtin == BuiltinType::Fptr)) {
            return floating_constant(0x3ff0000000000000ULL, type, 0,
                                     location);
        }
        if (value.kind == hir::Type::Kind::Builtin &&
            value.builtin == BuiltinType::F80) {
            return floating_constant(0x8000000000000000ULL, type,
                                     0x3fffU, location);
        }
        if (value.kind == hir::Type::Kind::Builtin &&
            value.builtin == BuiltinType::F128) {
            return floating_constant(0, type,
                                     0x3fff000000000000ULL, location);
        }
        return constant(1, type, location);
    }

    std::optional<ValueId> pointer_offset(ValueId base,
                                          hir::TypeId pointer_type_id,
                                          ValueId index, bool addition,
                                          SourceLocation location) {
        const auto& pointer = hir_.type(pointer_type_id);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
            storage_size(hir_, *pointer.pointee, target_) == 0 ||
            !integer_type(hir_, current_.values[index.value].type)) {
            diagnostics_.error(
                location,
                "pointer arithmetic requires an integer offset and a complete pointed-to object type");
            return std::nullopt;
        }
        const auto iptr = *hir_.builtin(BuiltinType::Iptr);
        index = cast(index, iptr, location);
        if (!addition) {
            const auto negated = add_value(ValueKind::Unary, iptr, location);
            auto& unary = current_.values[negated.value];
            unary.unary = UnaryOperation::Negate;
            unary.operands.push_back(index);
            index = negated;
        }
        return indexed_address(base, index, *pointer.pointee, location);
    }

    std::optional<ValueId> atomic_update(
        const AtomicLvalue& lvalue, ValueId right, std::string_view spelling,
        SourceLocation location, bool return_old = false) {
        const auto type = hir_.unqualified(lvalue.object_type);
        if (!integer_type(hir_, type) && !floating_type(hir_, type)) {
            diagnostics_.error(
                location,
                "atomic compound operations require an integer or floating object");
            return std::nullopt;
        }

        const auto right_type = current_.values[right.value].type;
        if ((spelling == "<<=" || spelling == ">>=") &&
            !integer_type(hir_, right_type)) return std::nullopt;
        const auto common = spelling == "<<=" || spelling == ">>="
                                ? promote(type) : common_type(type, right_type);
        if (!common) return std::nullopt;
        const auto operation = compound_operation(spelling, *common, location);
        if (!operation) return std::nullopt;
        if (floating_type(hir_, type) ||
            !representation_compatible(hir_, type, *common)) {
            // A target FetchUpdate operates in the object's representation.
            // Wider/mixed arithmetic instead runs in a shared CAS loop. The
            // captured address and RHS dominate the loop and execute once.
            const SlotId slot{static_cast<std::uint32_t>(current_.slots.size())};
            current_.slots.push_back({slot, location, type,
                "$atomic.expected." + std::to_string(slot.value), std::nullopt,
                false, true, false, storage_alignment(hir_, type, target_), std::nullopt});
            const LocalBinding expected{slot, type, std::nullopt, std::nullopt};
            (void)lifetime(ValueKind::LifetimeStart, slot, location);
            const auto initial = atomic_operation(
                AtomicOperation::Load, type, {lvalue.address}, MemoryOrder::SeqCst,
                location, MemoryOrder::SeqCst, lvalue.is_volatile);
            (void)store_slot(expected, initial, location);
            const auto expected_address = slot_address(expected, location);
            const auto loop = new_block(location);
            const auto end = new_block(location);
            terminate(TerminatorKind::Branch, location, std::nullopt, {loop});
            enter(loop);
            const auto old = load_slot(expected, location);
            const auto updated = compound_value(spelling, old, right, type, location);
            if (!updated) return std::nullopt;
            const auto success = atomic_operation(
                AtomicOperation::CompareExchange, *hir_.builtin(BuiltinType::Bool),
                {lvalue.address, expected_address, *updated}, MemoryOrder::SeqCst,
                location, MemoryOrder::SeqCst, lvalue.is_volatile);
            terminate(TerminatorKind::ConditionalBranch, location, success, {end, loop});
            enter(end);
            (void)lifetime(ValueKind::LifetimeEnd, slot, location);
            return return_old ? old : *updated;
        }

        right = assignment_cast(right, type, location);

        AtomicOperation atomic = AtomicOperation::FetchUpdate;
        if (integer_type(hir_, type)) {
            if (*operation == BinaryOperation::Add) {
                atomic = AtomicOperation::FetchAdd;
            } else if (*operation == BinaryOperation::Subtract) {
                atomic = AtomicOperation::FetchSub;
            } else if (*operation == BinaryOperation::BitAnd) {
                atomic = AtomicOperation::FetchAnd;
            } else if (*operation == BinaryOperation::BitXor) {
                atomic = AtomicOperation::FetchXor;
            } else if (*operation == BinaryOperation::BitOr) {
                atomic = AtomicOperation::FetchOr;
            }
        }
        const auto old = atomic_operation(
            atomic, type, {lvalue.address, right}, MemoryOrder::SeqCst,
            location, MemoryOrder::SeqCst, lvalue.is_volatile);
        current_.values[old.value].binary = *operation;
        if (return_old) return old;

        const auto updated = add_value(ValueKind::Binary, type, location);
        auto& binary = current_.values[updated.value];
        binary.binary = *operation;
        binary.operands = {old, right};
        return updated;
    }

    ContinuationTask<std::optional<ValueId>> lower_atomic_assignment_async(
        const Expr& expression, hir::TypeId object_type) {
        const auto value_type = hir_.unqualified(object_type);
        if (expression.text == "=") {
            auto lvalue = co_await lower_atomic_lvalue_async(*expression.left);
            auto source = co_await lower_expression_async(*expression.right, value_type);
            if (!lvalue || !source) co_return std::nullopt;
            (void)atomic_operation(
                AtomicOperation::Store, *hir_.builtin(BuiltinType::Void),
                {lvalue->address, *source}, MemoryOrder::SeqCst,
                expression.location, MemoryOrder::SeqCst,
                lvalue->is_volatile);
            co_return source;
        }
        if (!integer_type(hir_, value_type) &&
            !floating_type(hir_, value_type)) {
            diagnostics_.error(
                expression.location,
                "atomic compound operations require an integer or floating object");
            co_return std::nullopt;
        }
        auto lvalue = co_await lower_atomic_lvalue_async(*expression.left);
        if (!lvalue) co_return std::nullopt;
        auto right = co_await lower_expression_async(*expression.right);
        if (!right) co_return std::nullopt;
        co_return atomic_update(*lvalue, *right, expression.text,
                             expression.location);
    }

    std::optional<ValueId> lower_atomic_assignment(
        const Expr& expression, hir::TypeId object_type) {
        return lower_atomic_assignment_async(expression, object_type).run();
    }

    ContinuationTask<std::optional<ValueId>> lower_assignment_async(const Expr& expression) {
        const Expr* target = expression.left.get();
        while (target && target->kind == Expr::Kind::Parenthesized &&
               target->left) target = target->left.get();
        if (!target) co_return std::nullopt;
        if (const auto* node = vector_lane_expression(*target)) {
            const auto lane = co_await lower_vector_lane_async(*node);
            if (!lane || hir_.type(lane->element_type).is_const) co_return std::nullopt;
            std::optional<ValueId> old;
            if (expression.text != "=") {
                old = load_vector_lane(*lane, expression.location);
                if (!old) co_return std::nullopt;
            }
            auto source = co_await lower_expression_async(*expression.right);
            if (!source) co_return std::nullopt;
            if (old) source = compound_value(expression.text, *old, *source,
                                             lane->element_type, expression.location);
            co_return source ? store_vector_lane(*lane, *source, expression.location)
                          : std::nullopt;
        }
        const auto object_type = designator_type(*target);
        if (object_type && atomic_object_type(hir_, *object_type)) {
            co_return co_await lower_atomic_assignment_async(expression, *object_type);
        }
        if (target->kind == Expr::Kind::Binary &&
            (target->text == "member" ||
             target->text == "pointer_member" ||
             target->text == "index")) {
            std::optional<DesignatorAddress> designator;
            const auto aggregate_type = target->text == "index" &&
                                                target->left
                                            ? infer_type(
                                                  *target->left)
                                            : std::nullopt;
            if (!aggregate_type ||
                !vector_type(hir_, *aggregate_type)) {
                designator =
                    co_await lower_designator_address_async(*target);
            }
            if (designator) {
                if (hir_.type(designator->type).is_const) {
                    diagnostics_.error(expression.location, "cannot write a const subobject");
                    failed_ = true;
                    co_return std::nullopt;
                }
                if (!managed_value_type(hir_, designator->type)) {
                    co_return std::nullopt;
                }
            if (expression.text == "=") {
                auto source = co_await lower_expression_async(*expression.right,
                                               designator->type);
                if (!source) {
                    co_return std::nullopt;
                }
                if (designator->bit_field) {
                    co_return store_bit_field(*designator, *source,
                                           expression.location);
                }
                if (!store_pointer(designator->address, *source,
                                   expression.location,
                                   designator->alignment)) {
                    co_return std::nullopt;
                }
                co_return source;
            }
            std::optional<ValueId> left;
            if (designator->bit_field) {
                const auto loaded = load_bit_field(*designator,
                                                   expression.location);
                if (loaded) {
                    left = loaded->value;
                }
            } else {
                left = load_pointer(designator->address,
                                    expression.location,
                                    designator->alignment);
            }
            if (left && pointer_type(hir_, designator->type)) {
                if (expression.text != "+=" && expression.text != "-=") {
                    co_return std::nullopt;
                }
                auto right = co_await lower_expression_async(*expression.right);
                if (!right) co_return std::nullopt;
                const auto result = pointer_offset(
                    *left, designator->type, *right,
                    expression.text == "+=", expression.location);
                if (!result ||
                    !store_pointer(designator->address, *result,
                                   expression.location,
                                   designator->alignment)) {
                    co_return std::nullopt;
                }
                co_return result;
            }
            const auto right = co_await lower_expression_async(*expression.right);
            if (!left || !right) co_return std::nullopt;
            const auto result = compound_value(expression.text, *left, *right,
                                                designator->type, expression.location);
            if (!result) co_return std::nullopt;
            const auto stored = designator->bit_field
                                    ? store_bit_field(
                                          *designator, *result,
                                          expression.location)
                                    : store_pointer(
                                          designator->address, *result,
                                          expression.location,
                                          designator->alignment);
            if (!stored) {
                co_return std::nullopt;
            }
            co_return designator->bit_field ? stored : result;
            }
        }
        if (target->kind == Expr::Kind::Binary &&
            target->text == "index" &&
            target->left && target->right) {
            const auto element_type = designator_type(*target);
            if (element_type && managed_value_type(hir_, *element_type) &&
                !hir_.type(*element_type).is_const) {
                auto base = co_await lower_expression_async(*target->left);
                auto index = co_await lower_expression_async(*target->right);
                if (!base || !index ||
                    !integer_type(hir_, current_.values[index->value].type)) {
                    co_return std::nullopt;
                }
                const auto address = indexed_address(
                    *base, *index, *element_type, expression.location);
                if (expression.text == "=") {
                    auto source = co_await lower_expression_async(*expression.right,
                                                   *element_type);
                    if (!source ||
                        !store_pointer(address, *source,
                                       expression.location)) {
                        co_return std::nullopt;
                    }
                    co_return source;
                }
                auto left = load_pointer(address, expression.location);
                if (left && pointer_type(hir_, *element_type)) {
                    if (expression.text != "+=" &&
                        expression.text != "-=") {
                        co_return std::nullopt;
                    }
                    auto right = co_await lower_expression_async(*expression.right);
                    if (!right) co_return std::nullopt;
                    const auto pointer_result = pointer_offset(
                        *left, *element_type, *right,
                        expression.text == "+=", expression.location);
                    if (!pointer_result ||
                        !store_pointer(address, *pointer_result,
                                       expression.location)) {
                        co_return std::nullopt;
                    }
                    co_return pointer_result;
                }
                const auto right = co_await lower_expression_async(*expression.right);
                if (!left || !right) co_return std::nullopt;
                const auto result = compound_value(expression.text, *left, *right,
                                                    *element_type, expression.location);
                if (!result || !store_pointer(address, *result, expression.location)) {
                    co_return std::nullopt;
                }
                co_return result;
            }
        }
        if (target->kind == Expr::Kind::Unary &&
            target->text == "*" && target->left) {
            auto address = co_await lower_expression_async(*target->left);
            const auto type = infer_type(*target);
            if (!address || !type) co_return std::nullopt;
            if (expression.text == "=") {
                auto source = co_await lower_expression_async(*expression.right, *type);
                if (!source ||
                    !store_pointer(*address, *source, expression.location)) {
                    co_return std::nullopt;
                }
                co_return source;
            }
            auto left = load_pointer(*address, expression.location);
            if (left && pointer_type(hir_, *type)) {
                if (expression.text != "+=" && expression.text != "-=") {
                    co_return std::nullopt;
                }
                auto right = co_await lower_expression_async(*expression.right);
                if (!right) co_return std::nullopt;
                const auto result = pointer_offset(
                    *left, *type, *right, expression.text == "+=",
                    expression.location);
                if (!result ||
                    !store_pointer(*address, *result,
                                   expression.location)) {
                    co_return std::nullopt;
                }
                co_return result;
            }
            const auto right = co_await lower_expression_async(*expression.right);
            if (!left || !right) co_return std::nullopt;
            const auto result = compound_value(expression.text, *left, *right,
                                                *type, expression.location);
            if (!result || !store_pointer(*address, *result, expression.location)) {
                co_return std::nullopt;
            }
            co_return result;
        }
        const auto name = local_name(*target);
        const auto* found = name ? find_local(*name) : nullptr;
        const auto* object = found || !name ? nullptr : resolve_object(*target);
        if (found && (found->dynamic_address ||
                      array_type(hir_, found->type))) {
            co_return std::nullopt;
        }
        if (!found && (!object || !global_scalar(*object) ||
                       hir_.type(object->type).is_const)) {
            co_return std::nullopt;
        }
        const auto type = found ? found->type : object->type;
        ValueId result;
        if (expression.text == "=") {
            auto source = co_await lower_expression_async(*expression.right, type);
            if (!source) co_return std::nullopt;
            result = *source;
        } else {
            const auto left = found ? load_slot(*found, expression.location)
                                    : load_global(*object, expression.location);
            if (pointer_type(hir_, type)) {
                if (expression.text != "+=" && expression.text != "-=") {
                    co_return std::nullopt;
                }
                auto right = co_await lower_expression_async(*expression.right);
                if (!right) co_return std::nullopt;
                const auto updated = pointer_offset(
                    left, type, *right, expression.text == "+=",
                    expression.location);
                if (!updated) co_return std::nullopt;
                result = *updated;
                if (found) {
                    (void)store_slot(*found, result, expression.location);
                } else {
                    (void)store_global(*object, result,
                                       expression.location);
                }
                co_return result;
            }
            auto right = co_await lower_expression_async(*expression.right);
            if (!right) co_return std::nullopt;
            const auto updated = compound_value(expression.text, left, *right,
                                                  type, expression.location);
            if (!updated) co_return std::nullopt;
            result = *updated;
        }
        if (found) {
            (void)store_slot(*found, result, expression.location);
        } else {
            (void)store_global(*object, result, expression.location);
        }
        co_return result;
    }

    std::optional<ValueId> lower_assignment(const Expr& expression) {
        return lower_assignment_async(expression).run();
    }

    std::optional<MemoryOrder> parse_memory_order(const Expr& expression) {
        return parse_source_memory_order(expression);
    }

    bool lock_free_atomic_type(hir::TypeId type) const {
        return hir::lock_free_atomic_type(hir_, type, target_, subtarget_);
    }

    bool valid_failure_order(MemoryOrder success,
                             MemoryOrder failure) const {
        return valid_atomic_failure_order(success, failure);
    }

    ContinuationTask<std::optional<ValueId>> lower_atomic_call_async(const Expr& expression) {
        const auto& name = expression.left->text;
        const auto diagnose_order = [&](const Expr& order_expression) {
            diagnostics_.error(
                order_expression.location,
                "expected one of $::memory::relaxed, acquire, release, "
                "acq_rel, or seq_cst");
        };
        if (name == "$::atomic_is_lock_free") {
            if (expression.arguments.size() + (expression.type ? 1U : 0U) != 1) {
                diagnostics_.error(expression.location,
                                   "$::atomic_is_lock_free requires one type or expression");
                co_return std::nullopt;
            }
            auto type = expression.type ? std::optional<hir::TypeId>(hir_.intern_type(expression.type))
                : infer_type(*expression.arguments.front());
            if (!type || !scalar_type(hir_, *type)) {
                diagnostics_.error(expression.location,
                                   "$::atomic_is_lock_free requires a scalar type or expression");
                co_return std::nullopt;
            }
            co_return constant(lock_free_atomic_type(*type) ? 1U : 0U,
                            *hir_.builtin(BuiltinType::Bool),
                            expression.location);
        }
        if (name == "$::atomic_thread_fence" ||
            name == "$::atomic_signal_fence") {
            if (expression.arguments.size() != 1) {
                diagnostics_.error(
                    expression.location,
                    name + " requires one memory-order argument");
                co_return std::nullopt;
            }
            const auto order = parse_memory_order(
                *expression.arguments.front());
            if (!order) {
                diagnose_order(*expression.arguments.front());
                co_return std::nullopt;
            }
            co_return atomic_operation(
                name == "$::atomic_thread_fence"
                    ? AtomicOperation::ThreadFence
                    : AtomicOperation::SignalFence,
                *hir_.builtin(BuiltinType::Void), {}, *order,
                expression.location);
        }

        const std::size_t required =
            name == "$::atomic_load" ? 2U :
            name == "$::atomic_compare_exchange" ? 5U : 3U;
        if (expression.arguments.size() != required) {
            diagnostics_.error(
                expression.location,
                name + " requires " + std::to_string(required) +
                    " arguments");
            co_return std::nullopt;
        }
        const auto pointee = atomic_pointee(*expression.arguments[0]);
        if (!pointee) {
            diagnostics_.error(
                expression.arguments[0]->location,
                name + " requires a pointer to an atomic-qualified scalar");
            co_return std::nullopt;
        }
        const auto atomic_type = *pointee;
        const auto value_type = hir_.unqualified(atomic_type);
        if (name != "$::atomic_load" &&
            hir_.type(atomic_type).is_const) {
            diagnostics_.error(
                expression.arguments[0]->location,
                name + " cannot modify a const atomic object");
            co_return std::nullopt;
        }
        if (!lock_free_atomic_type(value_type)) {
            diagnostics_.error(
                expression.arguments[0]->location,
                "selected target '" + std::string(target_.architecture) +
                    "' has no runtime-free atomic operation for " +
                    hir::type_name(hir_, value_type));
            co_return std::nullopt;
        }
        const auto is_volatile = hir_.type(atomic_type).is_volatile;
        auto address = co_await lower_expression_async(*expression.arguments[0]);
        if (!address) co_return std::nullopt;

        if (name == "$::atomic_load") {
            const auto order = parse_memory_order(*expression.arguments[1]);
            if (!order) {
                diagnose_order(*expression.arguments[1]);
                co_return std::nullopt;
            }
            if (*order == MemoryOrder::Release ||
                *order == MemoryOrder::AcqRel) {
                diagnostics_.error(expression.arguments[1]->location,
                                   "atomic load order must be relaxed, acquire, or seq_cst");
                co_return std::nullopt;
            }
            co_return atomic_operation(AtomicOperation::Load, value_type,
                                    {*address}, *order,
                                    expression.location,
                                    MemoryOrder::SeqCst, is_volatile);
        }

        if (name == "$::atomic_compare_exchange") {
            const auto expected_pointer =
                infer_type(*expression.arguments[1]);
            if (!expected_pointer) co_return std::nullopt;
            const auto expected_pointer_value = hir_.type(*expected_pointer);
            if (expected_pointer_value.kind != hir::Type::Kind::Pointer ||
                !expected_pointer_value.pointee) {
                diagnostics_.error(
                    expression.arguments[1]->location,
                    "$::atomic_compare_exchange expected argument must be a pointer");
                co_return std::nullopt;
            }
            const auto expected_type = *expected_pointer_value.pointee;
            if (hir_.type(expected_type).is_const ||
                hir_.type(expected_type).is_atomic ||
                hir_.unqualified(expected_type) != value_type) {
                diagnostics_.error(
                    expression.arguments[1]->location,
                    "$::atomic_compare_exchange expected pointer has an incompatible type");
                co_return std::nullopt;
            }
            auto expected = co_await lower_expression_async(*expression.arguments[1]);
            auto desired = co_await lower_expression_async(*expression.arguments[2],
                                            value_type);
            const auto success = parse_memory_order(*expression.arguments[3]);
            const auto failure = parse_memory_order(*expression.arguments[4]);
            if (!success) diagnose_order(*expression.arguments[3]);
            if (!failure) diagnose_order(*expression.arguments[4]);
            if (!expected || !desired || !success || !failure) {
                co_return std::nullopt;
            }
            if (!valid_failure_order(*success, *failure)) {
                diagnostics_.error(
                    expression.arguments[4]->location,
                    "compare-exchange failure order is invalid or stronger than success");
                co_return std::nullopt;
            }
            co_return atomic_operation(
                AtomicOperation::CompareExchange,
                *hir_.builtin(BuiltinType::Bool),
                {*address, *expected, *desired}, *success,
                expression.location, *failure, is_volatile);
        }

        auto source = co_await lower_expression_async(*expression.arguments[1], value_type);
        const auto order = parse_memory_order(*expression.arguments[2]);
        if (!order) diagnose_order(*expression.arguments[2]);
        if (!source || !order) co_return std::nullopt;
        if (name == "$::atomic_store") {
            if (*order == MemoryOrder::Acquire ||
                *order == MemoryOrder::AcqRel) {
                diagnostics_.error(expression.arguments[2]->location,
                                   "atomic store order must be relaxed, release, or seq_cst");
                co_return std::nullopt;
            }
            co_return atomic_operation(
                AtomicOperation::Store,
                *hir_.builtin(BuiltinType::Void), {*address, *source},
                *order, expression.location, MemoryOrder::SeqCst,
                is_volatile);
        }
        AtomicOperation operation = AtomicOperation::Exchange;
        if (name == "$::atomic_fetch_add") operation = AtomicOperation::FetchAdd;
        else if (name == "$::atomic_fetch_sub") operation = AtomicOperation::FetchSub;
        else if (name == "$::atomic_fetch_and") operation = AtomicOperation::FetchAnd;
        else if (name == "$::atomic_fetch_xor") operation = AtomicOperation::FetchXor;
        else if (name == "$::atomic_fetch_or") operation = AtomicOperation::FetchOr;
        if (operation != AtomicOperation::Exchange &&
            !integer_type(hir_, value_type)) {
            diagnostics_.error(
                expression.arguments[0]->location,
                "atomic fetch arithmetic and bitwise operations require an integer object");
            co_return std::nullopt;
        }
        co_return atomic_operation(operation, value_type,
                                {*address, *source}, *order,
                                expression.location, MemoryOrder::SeqCst,
                                is_volatile);
    }

    std::optional<ValueId> lower_atomic_call(const Expr& expression) {
        return lower_atomic_call_async(expression).run();
    }

    static const Expr& without_parentheses(const Expr& expression) {
        const Expr* result = &expression;
        while (result->kind == Expr::Kind::Parenthesized && result->left)
            result = result->left.get();
        return *result;
    }

    static bool memory_designator(const Expr& expression) {
        return (expression.kind == Expr::Kind::Unary &&
                expression.text == "*") ||
               (expression.kind == Expr::Kind::Binary &&
                (expression.text == "index" || expression.text == "member" ||
                 expression.text == "pointer_member"));
    }

    bool machine_operand_matches(const InstructionOperandEntry& field,
                                 const Expr& argument) {
        const auto type = infer_type(argument);
        const bool name = argument.kind == Expr::Kind::Name;
        if (field.allow_memory && memory_designator(argument)) {
            const auto pointee = designator_type(argument);
            return !field.memory_bits ||
                   (pointee && type_bits(hir_, *pointee) == field.memory_bits);
        }
        if (field.allow_register && name) {
            return type && !hir_.type(*type).is_atomic &&
                   (!field.value_bits ||
                    type_bits(hir_, *type) == field.value_bits) &&
                   floating_type(hir_, *type) ==
                       (field.register_class == "floating") &&
                   (field.role == InstructionOperandRole::Input ||
                    !hir_.type(*type).is_const);
        }
        if (!field.allow_immediate || !argument.evaluated_integer) {
            return false;
        }
        const auto value = convert_integer(
            argument.evaluated_integer->value,
            {builtin_bits(argument.evaluated_integer->type, hir_.address_bits),
             builtin_signed(argument.evaluated_integer->type), false},
            {128, true, false});
        return (!value.high || (value.high == UINT64_MAX &&
                                value.low >= (std::uint64_t{1} << 63))) &&
               instruction_immediate_fits(value.low, field);
    }

    // The available registry form a machine-instruction call names, or a
    // diagnostic explaining why none or several apply.
    const InstructionEntry* select_machine_form(const Expr& call) {
        const auto& name = call.left->text;
        const InstructionEntry* selected = nullptr;
        bool arity = false;
        bool ambiguous = false;
        std::optional<InstructionFeatureConflict> conflict;
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (form->operands.size() != call.arguments.size()) continue;
            arity = true;
            bool matches = true;
            for (std::size_t index = 0; index < form->operands.size();
                 ++index) {
                if (!machine_operand_matches(
                        form->operands[index],
                        without_parentheses(*call.arguments[index]))) {
                    matches = false;
                    break;
                }
            }
            if (!matches) continue;
            if (const auto missing = instruction_feature_conflict(
                    *form, [&](std::string_view feature) {
                        return subtarget_.supports_registry_feature(feature);
                    })) {
                if (!conflict) conflict = missing;
                continue;
            }
            ambiguous = ambiguous || selected;
            selected = form;
        }
        const auto prefix = "target instruction '" + name + "' ";
        std::string message;
        if (ambiguous) {
            message = prefix + "has ambiguous typed forms for these operands";
        } else if (selected) {
            return selected;
        } else if (conflict) {
            message = prefix +
                      (conflict->forbidden ? "is unavailable with feature '"
                                           : "requires feature '") +
                      std::string(conflict->feature) + "'";
        } else if (!arity) {
            message = prefix + "has no form accepting " +
                      std::to_string(call.arguments.size()) + " operands";
        } else {
            message = "no typed form of " + prefix + "matches these operands";
        }
        diagnostics_.error(call.location, message);
        failed_ = true;
        return nullptr;
    }

    ContinuationTask<std::optional<ValueId>> lower_machine_instruction_async(
        const Expr& expression) {
        const auto& name = expression.left->text;
        const auto fail = [&](std::string message) -> std::optional<ValueId> {
            diagnostics_.error(expression.location,
                               "target instruction '" + name + "' " +
                                   std::move(message));
            failed_ = true;
            return std::nullopt;
        };
        const auto* form = select_machine_form(expression);
        if (!form) co_return std::nullopt;
        // A naked body leaves through a raw return; raw branches stay in the
        // raw lowerer, structured control and goto cover branching here.
        const bool exit = form->control == InstructionControlEffect::RawReturn ||
                          form->control == InstructionControlEffect::Trap;
        if (form->control != InstructionControlEffect::None &&
            (!naked_ || !exit)) {
            co_return fail(naked_ ? "is a raw branch; use goto or structured "
                                    "control flow in this naked function"
                                  : "transfers control and is only available "
                                    "in a naked function");
        }
        if (form->stack_delta != 0 || form->ordered_stack_delta != 0 ||
            form->ordered_stack_reset) {
            co_return fail("changes machine stack state and is only "
                           "available in a naked function");
        }
        std::vector<ValueId> operands;
        const Expr* output = nullptr;
        for (std::size_t index = 0; index < form->operands.size(); ++index) {
            const auto& field = form->operands[index];
            const auto& argument =
                without_parentheses(*expression.arguments[index]);
            if (field.allow_memory && memory_designator(argument)) {
                const auto designator =
                    co_await lower_designator_address_async(argument);
                if (!designator) co_return std::nullopt;
                operands.push_back(designator->address);
                continue;
            }
            if (field.allow_register && argument.kind == Expr::Kind::Name) {
                if (field.role != InstructionOperandRole::Input) {
                    if (output) co_return fail("has more than one output");
                    output = &argument;
                }
                if (field.role == InstructionOperandRole::Output) continue;
                const auto value = co_await lower_expression_async(argument);
                if (!value) co_return std::nullopt;
                operands.push_back(*value);
                continue;
            }
            const auto type = infer_type(argument);
            if (!argument.evaluated_integer || !type) {
                co_return fail("requires a translation-time constant for "
                               "operand " + std::to_string(index + 1));
            }
            const auto constant =
                add_value(ValueKind::ConstantInteger, *type, argument.location);
            current_.values[constant.value].integer =
                argument.evaluated_integer->value.low;
            current_.values[constant.value].integer_high =
                argument.evaluated_integer->value.high;
            operands.push_back(constant);
        }
        const LocalBinding* local = nullptr;
        const hir::Object* object = nullptr;
        auto result_type = *hir_.builtin(BuiltinType::Void);
        if (output) {
            local = find_local(name_key(*output));
            object = local ? nullptr : resolve_object(*output);
            if ((!local && (!object || !global_scalar(*object))) ||
                (local && local->dynamic_address)) {
                co_return fail("output must name a scalar object");
            }
            result_type = hir_.unqualified(local ? local->type : object->type);
        }
        const auto result =
            pure_instruction_form(*form)
                ? add_value(ValueKind::MachineInstruction, result_type,
                            expression.location)
                : add_effectful(ValueKind::MachineInstruction, result_type,
                                expression.location);
        current_.values[result.value].instruction_form =
            instruction_form_id(target_, *form);
        current_.values[result.value].operands = std::move(operands);
        if (local) {
            (void)store_slot(*local, result, expression.location);
        } else if (object) {
            (void)store_global(*object, result, expression.location);
        }
        if (exit) {
            terminate(TerminatorKind::Unreachable, expression.location,
                      std::nullopt, {});
        }
        co_return result;
    }

    ContinuationTask<std::optional<ValueId>> lower_call_async(const Expr& expression) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            co_return co_await lower_indirect_call_async(expression);
        }
        if (expression.left->text == "$::patch") {
            co_return lower_patch(expression);
        }
        if (atomic_intrinsic(expression.left->text)) {
            co_return co_await lower_atomic_call_async(expression);
        }
        if (expression.left->text == "$::alignof") {
            if (expression.arguments.size() != 1) {
                co_return std::nullopt;
            }
            static constexpr std::pair<std::string_view, BuiltinType> types[]{
                {"bool", BuiltinType::Bool},
                {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
                {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
                {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
                {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
                {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
                {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
                {"f32", BuiltinType::F32}, {"f64", BuiltinType::F64},
                {"f80", BuiltinType::F80}, {"f128", BuiltinType::F128},
                {"fptr", BuiltinType::Fptr},
            };
            std::optional<hir::TypeId> queried;
            if (expression.arguments.front()->kind == Expr::Kind::Name) {
                const auto found = std::find_if(
                    std::begin(types), std::end(types),
                    [&](const auto& candidate) {
                        return candidate.first ==
                               expression.arguments.front()->text;
                    });
                if (found != std::end(types)) {
                    queried = hir_.builtin(found->second);
                }
            }
            if (!queried) {
                queried = designator_type(*expression.arguments.front());
            }
            if (!queried) {
                queried = infer_type(*expression.arguments.front());
            }
            if (!queried) co_return std::nullopt;
            const auto alignment = storage_alignment(
                hir_, *queried, target_);
            co_return constant(alignment, *hir_.builtin(BuiltinType::Uptr),
                            expression.location);
        }
        if (expression.left->text == "$::expect") {
            if (expression.arguments.size() != 2) {
                diagnostics_.error(expression.location,
                                   "$::expect requires two arguments");
                co_return std::nullopt;
            }
            const auto type = infer_type(*expression.arguments.front());
            const auto expected = patch_initial(
                *expression.arguments[1], hir_.address_bits);
            if (!type || !integer_type(hir_, *type)) {
                diagnostics_.error(
                    expression.arguments.front()->location,
                    "$::expect requires a boolean, integer, or enumeration value");
                co_return std::nullopt;
            }
            if (!expected) {
                diagnostics_.error(
                    expression.arguments[1]->location,
                    "$::expect requires an integer constant expectation");
                co_return std::nullopt;
            }
            auto source = co_await lower_expression_async(*expression.arguments.front());
            if (!source) co_return std::nullopt;
            source = cast(*source, *type, expression.location);
            const auto result = add_value(ValueKind::Intrinsic, *type,
                                          expression.location);
            auto& intrinsic = current_.values[result.value];
            intrinsic.intrinsic = IntrinsicOperation::Expect;
            intrinsic.operands.push_back(*source);
            const auto bits = type_bits(hir_, *type);
            const auto converted = mask_to(expected->value, bits);
            intrinsic.integer = converted.low;
            intrinsic.integer_high = converted.high;
            co_return result;
        }
        if (expression.left->text == "$::assume") {
            if (expression.arguments.size() != 1) {
                diagnostics_.error(expression.location,
                                   "$::assume requires one argument");
                co_return std::nullopt;
            }
            const auto& condition = *expression.arguments.front();
            const auto type = infer_type(condition);
            if (!type || !scalar_type(hir_, *type)) {
                diagnostics_.error(condition.location,
                                   "$::assume requires a scalar condition");
                co_return std::nullopt;
            }
            if (const auto violation = assumption_violation(condition, [&](const Expr& node) {
                    auto queried = designator_type(node, nullptr, ObjectAccess::Value);
                    if (!queried) queried = infer_type(node);
                    if (!queried) return false;
                    const auto& source = hir_.type(*queried);
                    return source.kind != hir::Type::Kind::Array && source.kind != hir::Type::Kind::Function &&
                        (source.is_volatile || source.is_atomic);
                })) {
                diagnostics_.error(violation->expression->location, violation->message());
                co_return std::nullopt;
            }
            const auto result = add_effectful(
                ValueKind::Intrinsic, *hir_.builtin(BuiltinType::Void),
                expression.location);
            current_.values[result.value].intrinsic =
                IntrinsicOperation::Assume;
            const Expr* predicate = &condition;
            while (predicate->kind == Expr::Kind::Parenthesized && predicate->left)
                predicate = predicate->left.get();
            if (predicate->kind == Expr::Kind::Binary && predicate->text == "<" &&
                predicate->left && predicate->right && predicate->left->kind == Expr::Kind::Name &&
                !find_local(name_key(*predicate->left))) {
                const auto parameter = parameter_values_.find(name_key(*predicate->left));
                const auto limit = patch_initial(*predicate->right, hir_.address_bits);
                if (parameter != parameter_values_.end() && limit) {
                    const auto source_type = current_.values[parameter->second.value].type;
                    const auto common = common_type(source_type, *hir_.builtin(limit->type));
                    if (common && !signed_type(hir_, *common) &&
                        type_bits(hir_, *common) == type_bits(hir_, source_type)) {
                        auto& fact = current_.values[result.value];
                        fact.binary = BinaryOperation::UnsignedLess;
                        fact.operands = {parameter->second};
                        const auto bound = convert_integer(limit->value,
                            {builtin_bits(limit->type, hir_.address_bits), builtin_signed(limit->type)},
                            {type_bits(hir_, *common), false});
                        fact.integer = bound.low;
                        fact.integer_high = bound.high;
                    }
                }
            }
            co_return result;
        }
        if (expression.left->text == "$::unreachable" ||
            expression.left->text == "$::trap") {
            const bool trap = expression.left->text == "$::trap";
            if (!expression.arguments.empty()) {
                diagnostics_.error(
                    expression.location,
                    trap ? "$::trap takes no arguments"
                         : "$::unreachable takes no arguments");
                co_return std::nullopt;
            }
            const auto result = add_effectful(
                ValueKind::Intrinsic, *hir_.builtin(BuiltinType::Void),
                expression.location);
            current_.values[result.value].intrinsic =
                trap ? IntrinsicOperation::Trap
                     : IntrinsicOperation::Unreachable;
            terminate(trap ? TerminatorKind::Trap
                           : TerminatorKind::Unreachable,
                      expression.location, std::nullopt, {});
            co_return result;
        }
        if (expression.left->text == "$::_nop") {
            if (!expression.arguments.empty()) {
                diagnostics_.error(expression.location,
                                   "$::_nop takes no arguments");
                co_return std::nullopt;
            }
            if (!target_has_instruction(target_, expression.left->text)) {
                diagnostics_.error(
                    expression.location,
                    "target instruction '$::_nop' is not available for '" +
                        std::string(target_.architecture) + "'");
                co_return std::nullopt;
            }
            const auto result = add_effectful(
                ValueKind::Intrinsic, *hir_.builtin(BuiltinType::Void),
                expression.location);
            current_.values[result.value].intrinsic =
                IntrinsicOperation::MachineNop;
            co_return result;
        }
        if (expression.left->text == "$::_movabs" ||
            expression.left->text == "$::_add" ||
            expression.left->text == "$::_cmp") {
            if (expression.arguments.size() != 2) co_return std::nullopt;
            const auto name = local_name(*expression.arguments.front());
            const auto* found = name ? find_local(*name) : nullptr;
            if (!found || found->dynamic_address) co_return std::nullopt;
            const auto binding = *found;
            auto right = co_await lower_expression_async(*expression.arguments[1],
                                          binding.type);
            if (!right) co_return std::nullopt;
            if (expression.left->text == "$::_movabs") {
                (void)store_slot(binding, *right, expression.location);
                co_return *right;
            }
            const auto left = load_slot(binding, expression.location);
            const auto comparison =
                expression.left->text == "$::_cmp";
            const auto result = add_value(
                ValueKind::Binary,
                comparison ? *hir_.builtin(BuiltinType::Bool) : binding.type,
                expression.location);
            auto& operation = current_.values[result.value];
            operation.operands = {left, *right};
            operation.binary = comparison ? BinaryOperation::Equal
                                          : BinaryOperation::Add;
            if (!comparison) {
                (void)store_slot(binding, result, expression.location);
            }
            co_return result;
        }
        if (expression.left->text.starts_with("$::_")) {
            co_return co_await lower_machine_instruction_async(expression);
        }
        if (find_local(name_key(*expression.left)) ||
            parameter_values_.contains(name_key(*expression.left)) ||
            resolve_object(*expression.left))
            co_return co_await lower_indirect_call_async(expression);
        const auto* callee = resolve_function(*expression.left);
        if (!callee || !callable(*callee)) co_return std::nullopt;
        if (callee->fixed_address) co_return co_await lower_indirect_call_async(expression);
        co_return co_await lower_resolved_call_async(
            expression, *hir::call_signature(hir_, callee->id, {}), callee);
    }

    std::optional<ValueId> lower_call(const Expr& expression) {
        return lower_call_async(expression).run();
    }

    ContinuationTask<std::optional<ValueId>> lower_indirect_call_async(const Expr& expression) {
        if (!expression.left) co_return std::nullopt;
        const auto target = co_await lower_expression_async(*expression.left);
        if (!target) co_return std::nullopt;
        const auto& pointer = hir_.type(current_.values[target->value].type);
        if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
            hir_.type(*pointer.pointee).kind != hir::Type::Kind::Function ||
            !hir_.type(*pointer.pointee).function) {
            diagnostics_.error(
                expression.location,
                "indirect call requires a typed function pointer");
            failed_ = true;
            co_return std::nullopt;
        }
        const auto type = *pointer.pointee;
        const auto signature = *hir_.type(type).function;
        const auto* abi = find_abi(target_, signature.abi);
        if (!abi || !abi->function_selectable ||
            abi->address_bits != hir_.address_bits ||
            (signature.variadic && !abi->variadic_supported)) {
            diagnostics_.error(
                expression.location,
                "indirect call has no compatible registered ABI");
            failed_ = true;
            co_return std::nullopt;
        }
        co_return co_await lower_resolved_call_async(expression, signature, nullptr, *target,
                                   type);
    }

    std::optional<ValueId> lower_indirect_call(const Expr& expression) {
        return lower_indirect_call_async(expression).run();
    }

    ContinuationTask<std::optional<ValueId>> lower_resolved_call_async(
        const Expr& expression, hir::FunctionSignature signature,
        const hir::Function* callee, std::optional<ValueId> target = {},
        std::optional<hir::TypeId> indirect_signature = {}) {
        if (expression.arguments.size() < signature.parameters.size() ||
            (!signature.variadic &&
             expression.arguments.size() != signature.parameters.size())) {
            diagnostics_.error(
                expression.location,
                "call to '" +
                    (callee ? callee->source_name
                            : std::string("function pointer")) +
                    "' requires " +
                    std::to_string(signature.parameters.size()) +
                    (signature.variadic ? " or more arguments" : " arguments"));
            failed_ = true;
            co_return std::nullopt;
        }
        struct PendingCopyOut {
            SlotId cell;
            std::optional<LocalBinding> local;
            std::optional<hir::ObjectId> object;
            std::optional<DesignatorAddress> designator;
            std::optional<VectorLane> lane;
            SourceLocation location;
            bool copy_out = true;
        };
        std::vector<CallArgument> arguments;
        std::vector<PendingCopyOut> copyouts;
        // A `[[musttail]]` call passes the transport pointer of each caller
        // output given as an output argument on to the callee, which then
        // delivers it; the caller delivers its other outputs before the call.
        const bool forward_outputs =
            &expression == musttail_call_ &&
            !hir::manual_interface(signature) &&
            !hir::manual_interface(hir_.function(current_.source));
        std::vector<bool> forwarded(copy_outs_.size());
        for (std::size_t index = 0; index < signature.parameters.size();
             ++index) {
            const auto& parameter = signature.parameters[index];
            const auto& actual = *expression.arguments[index];
            const bool manual_cell = parameter.mode == ParameterMode::In &&
                                     parameter.physical_location &&
                                     *parameter.physical_location != "auto";
            if (parameter.mode == ParameterMode::In && !manual_cell) {
                auto argument = co_await lower_expression_async(actual, parameter.type);
                if (!argument) co_return std::nullopt;
                arguments.push_back(
                    {*argument, std::nullopt, parameter.type, false});
                continue;
            }
            if (forward_outputs && parameter.mode != ParameterMode::In) {
                const auto name = local_name(actual);
                const auto* local = name ? find_local(*name) : nullptr;
                const auto found = std::find_if(
                    copy_outs_.begin(), copy_outs_.end(),
                    [&](const std::pair<LocalBinding, ValueId>& output) {
                        return local && output.first.slot == local->slot;
                    });
                const auto position =
                    static_cast<std::size_t>(found - copy_outs_.begin());
                if (found == copy_outs_.end() ||
                    !hir::same_callable_type(hir_, found->first.type,
                                             parameter.type) ||
                    forwarded[position]) {
                    diagnostics_.error(
                        actual.location,
                        "musttail requires each output argument to be a "
                        "distinct caller output parameter of the same type");
                    failed_ = true;
                    co_return std::nullopt;
                }
                forwarded[position] = true;
                // The callee reads an `inout` value through the pointer.
                if (parameter.mode == ParameterMode::InOut &&
                    !store_pointer(found->second,
                                   load_slot(found->first, actual.location),
                                   actual.location)) {
                    failed_ = true;
                    co_return std::nullopt;
                }
                arguments.push_back(
                    {found->second, std::nullopt, parameter.type, false});
                continue;
            }
            std::optional<LocalBinding> actual_local;
            const hir::Object* actual_object{};
            std::optional<DesignatorAddress> actual_designator;
            std::optional<VectorLane> actual_lane;
            if (!manual_cell) {
                if (const auto name = local_name(actual)) {
                    if (const auto* found = find_local(*name)) {
                        actual_local = *found;
                    } else {
                        actual_object = resolve_object(actual);
                    }
                } else if (const auto* lane = vector_lane_expression(actual);
                           lane && designator_type(actual)) {
                    actual_lane = co_await lower_vector_lane_async(*lane);
                    if (!actual_lane) co_return std::nullopt;
                } else if (designator_type(actual)) {
                    actual_designator = co_await lower_designator_address_async(actual);
                    if (!actual_designator) co_return std::nullopt;
                }
            }
            const SlotId cell{
                static_cast<std::uint32_t>(current_.slots.size())};
            current_.slots.push_back(
                {cell, actual.location, parameter.type,
                 "$call." + std::to_string(current_.values.size()) + "." +
                     std::to_string(index),
                 std::nullopt, false, false, false, 1, std::nullopt});
            (void)lifetime(ValueKind::LifetimeStart, cell, actual.location);
            const LocalBinding temporary{cell, parameter.type, std::nullopt,
                                         std::nullopt};
            if (parameter.mode == ParameterMode::InOut || manual_cell) {
                std::optional<ValueId> initial;
                if (manual_cell) {
                    initial = co_await lower_expression_async(actual, parameter.type);
                } else if (actual_local) {
                    initial = assignment_cast(
                        load_slot(*actual_local, actual.location),
                        parameter.type, actual.location);
                } else if (actual_object && global_scalar(*actual_object)) {
                    initial = assignment_cast(
                        load_global(*actual_object, actual.location),
                        parameter.type, actual.location);
                } else if (actual_lane) {
                    initial = load_vector_lane(*actual_lane, actual.location);
                    if (initial) initial = assignment_cast(*initial, parameter.type, actual.location);
                } else if (actual_designator) {
                    if (actual_designator->bit_field) {
                        const auto loaded = load_bit_field(*actual_designator, actual.location);
                        if (loaded) initial = loaded->value;
                    } else {
                        initial = load_pointer(actual_designator->address, actual.location,
                                               actual_designator->alignment);
                    }
                    if (initial) {
                        initial = assignment_cast(*initial, parameter.type,
                                                  actual.location);
                    }
                } else {
                    initial = co_await lower_expression_async(actual, parameter.type);
                }
                if (!initial) co_return std::nullopt;
                (void)store_slot(temporary, *initial, actual.location);
            } else if (!actual_local && !actual_object && !actual_designator && !actual_lane) {
                // A non-lvalue `out` actual is still evaluated once for its
                // source-visible effects; only its eventual copy-out is
                // discarded.
                if (!co_await lower_expression_async(actual)) co_return std::nullopt;
            }
            arguments.push_back({std::nullopt, cell, parameter.type, false});
            PendingCopyOut copyout;
            copyout.cell = cell;
            copyout.location = actual.location;
            copyout.copy_out = !manual_cell;
            const auto modifiable = [&](hir::TypeId type) {
                const auto& object = hir_.type(type);
                return !object.is_const && object.kind != hir::Type::Kind::Array &&
                    object.kind != hir::Type::Kind::Function;
            };
            if (actual_local && modifiable(actual_local->type)) {
                copyout.local = actual_local;
            }
            if (actual_object && modifiable(actual_object->type)) {
                copyout.object = actual_object->id;
            }
            if (actual_designator && modifiable(actual_designator->type))
                copyout.designator = actual_designator;
            if (actual_lane && modifiable(actual_lane->element_type)) copyout.lane = actual_lane;
            copyouts.push_back(std::move(copyout));
        }
        for (std::size_t index = signature.parameters.size();
             index < expression.arguments.size(); ++index) {
            const auto& actual = *expression.arguments[index];
            auto type = infer_type(actual);
            if (!type || !supported_call_type(hir_, *type, CallTypeUse::Variadic)) {
                diagnostics_.error(actual.location,
                                   "unsupported variadic argument type");
                failed_ = true;
                co_return std::nullopt;
            }
            auto promoted = *type;
            if (integer_type(hir_, promoted) &&
                type_bits(hir_, promoted) < 32) {
                promoted = *hir_.builtin(BuiltinType::I32);
            } else if (hir_.type(promoted).kind == hir::Type::Kind::Builtin &&
                       hir_.type(promoted).builtin == BuiltinType::F32) {
                promoted = *hir_.builtin(BuiltinType::F64);
            }
            auto value = co_await lower_expression_async(actual, promoted);
            if (!value) co_return std::nullopt;
            value = cast(*value, promoted, actual.location);
            arguments.push_back({*value, std::nullopt, promoted, true});
        }
        if (forward_outputs) {
            for (std::size_t index = 0; index < copy_outs_.size(); ++index) {
                if (forwarded[index]) continue;
                const auto& [cell, pointer] = copy_outs_[index];
                const auto value = load_slot(cell, expression.location);
                current_.values[value.value].copy_out_read = true;
                if (!store_pointer(pointer, value, expression.location)) {
                    failed_ = true;
                    co_return std::nullopt;
                }
            }
        }
        const auto result = add_effectful(
            ValueKind::Call, signature.result_type, expression.location);
        auto& call = current_.values[result.value];
        if (callee) call.callee = callee->id;
        call.call_signature = indirect_signature;
        if (target) call.operands.push_back(*target);
        call.call_arguments = std::move(arguments);
        for (const auto& argument : call.call_arguments) {
            if (argument.value) call.operands.push_back(*argument.value);
        }
        const bool noreturn =
            callee &&
            ((callee->definition &&
              callee->definition->attribute("noreturn")) ||
             std::any_of(
                 callee->declarations.begin(), callee->declarations.end(),
                 [](const FunctionDecl* declaration) {
                     return declaration->attribute("noreturn") != nullptr;
                 }));
        if (noreturn) {
            terminate(TerminatorKind::Unreachable, expression.location,
                      std::nullopt, {});
            co_return result;
        }
        for (const auto& copyout : copyouts) {
            const LocalBinding temporary{
                copyout.cell, current_.slots[copyout.cell.value].type,
                std::nullopt, std::nullopt};
            if (copyout.copy_out) {
                const auto value = load_slot(temporary, copyout.location);
                if (copyout.local) {
                    (void)store_slot(*copyout.local, value, copyout.location);
                } else if (copyout.object) {
                    (void)store_global(hir_.object(*copyout.object), value,
                                       copyout.location);
                } else if (copyout.lane) {
                    if (!store_vector_lane(*copyout.lane, value, copyout.location))
                        co_return std::nullopt;
                } else if (copyout.designator) {
                    const auto& destination = *copyout.designator;
                    const auto stored = destination.bit_field
                        ? store_bit_field(destination, value, copyout.location)
                        : store_pointer(destination.address, value, copyout.location,
                                        destination.alignment);
                    if (!stored) co_return std::nullopt;
                }
            }
            (void)lifetime(ValueKind::LifetimeEnd, copyout.cell,
                           copyout.location);
        }
        co_return result;
    }

    std::optional<ValueId> lower_resolved_call(
        const Expr& expression, hir::FunctionSignature signature,
        const hir::Function* callee, std::optional<ValueId> target = {},
        std::optional<hir::TypeId> indirect_signature = {}) {
        return lower_resolved_call_async(expression, std::move(signature), callee, target, indirect_signature).run();
    }

    std::optional<ValueId> lower_patch(const Expr& expression) {
        if (expression.arguments.empty() || expression.arguments.size() > 2) {
            diagnostics_.error(
                expression.location,
                "$::patch requires an initial value and optional address sink");
            failed_ = true;
            return std::nullopt;
        }
        const auto& initial_expression = *expression.arguments.front();
        const auto initial = patch_initial(initial_expression,
                                           hir_.address_bits);
        std::optional<data::AddressConstant> initial_address;
        BuiltinType initial_type{};
        if (initial) {
            initial_type = initial->type;
        } else {
            const auto& caller = hir_.function(current_.source);
            const std::function<bool(const Expr&)> names_runtime_cell =
                [&](const Expr& node) {
                    if (node.kind == Expr::Kind::Name &&
                        node.text.find("::") == std::string::npos &&
                        (find_local(name_key(node)) ||
                         parameter_values_.contains(name_key(node)))) return true;
                    if (node.left && names_runtime_cell(*node.left)) return true;
                    if (node.right && names_runtime_cell(*node.right)) return true;
                    if (node.third && names_runtime_cell(*node.third)) return true;
                    return std::any_of(
                        node.arguments.begin(), node.arguments.end(),
                        [&](const auto& argument) {
                            return names_runtime_cell(*argument);
                        });
                };
            if (!names_runtime_cell(initial_expression)) {
                initial_address = data::relocatable_address(
                    hir_, {caller.source_name, caller.source_unit},
                    initial_expression, subtarget_, true);
            }
            if (initial_address) {
                const auto inferred = infer_type(initial_expression);
                if (!inferred ||
                    hir_.type(*inferred).kind != hir::Type::Kind::Builtin ||
                    type_bits(hir_, *inferred) != hir_.address_bits) {
                    diagnostics_.error(initial_expression.location,
                        "$::patch relocatable initial requires an address-width integer type");
                    failed_ = true;
                    return std::nullopt;
                }
                initial_type = hir_.type(*inferred).builtin;
                if (initial_address->kind == data::AddressKind::Object &&
                    initial_address->object &&
                    hir_.object(*initial_address->object).is_thread_local) {
                    diagnostics_.error(initial_expression.location,
                        "$::patch initial cannot use an ordinary relocation to thread-local storage");
                    failed_ = true;
                    return std::nullopt;
                }
                if (initial_address->kind == data::AddressKind::Label && initial_address->label) {
                    if (!hir::stabilize_label_address(hir_, *initial_address->label,
                                                      initial_expression.location, diagnostics_)) {
                        failed_ = true;
                        return std::nullopt;
                    }
                } else if (initial_address->kind == data::AddressKind::Function &&
                           initial_address->function) {
                    hir::stabilize_function_address(hir_, *initial_address->function);
                }
            }
        }
        if (!initial && !initial_address) {
            diagnostics_.error(
                expression.arguments.front()->location,
                "$::patch initial value must be a translation-time integer or relocatable address");
            failed_ = true;
            return std::nullopt;
        }
        const auto source_type = builtin_type(initial_type);
        const auto materializer = find_patch_value_materializer(
            target_, type_name(source_type));
        if (!materializer || !subtarget_.supports_registry_feature(materializer->feature)) {
            diagnostics_.error(
                expression.arguments.front()->location,
                "selected target has no contiguous $::patch materializer for " +
                    std::string(type_name(source_type)));
            failed_ = true;
            return std::nullopt;
        }
        if (initial_address && !materializer->supports_symbol_relocation) {
            diagnostics_.error(initial_expression.location,
                "selected target patch materializer cannot encode a symbol relocation");
            failed_ = true;
            return std::nullopt;
        }
        const auto type = hir_.builtin(initial_type);
        if (!type) {
            failed_ = true;
            return std::nullopt;
        }
        std::optional<PatchSink> sink;
        if (expression.arguments.size() == 2) {
            if (materializer->patch_address == PatchAddressRepresentation::Unavailable) {
                diagnostics_.error(
                    expression.arguments[1]->location,
                    "selected target does not support a $::patch address sink for this materializer");
                failed_ = true;
                return std::nullopt;
            }
            sink = resolve_patch_sink(*expression.arguments[1], materializer->patch_address);
            if (!sink) return std::nullopt;
        }
        const auto origin = token_origin(expression.left->location).identity;
        const auto previous = current_patch_origins_.find(origin);
        const bool copied = origin.source_unit && previous != current_patch_origins_.end();
        std::uint32_t identity{};
        if (copied) {
            const auto& original = current_.values[previous->second.value];
            if (original.type != *type || original.patch_sink != sink ||
                original.patch_initial_address != initial_address ||
                original.integer != (initial ? initial->value.low : 0) ||
                original.integer_high != (initial ? initial->value.high : 0)) {
                diagnostics_.error(expression.location,
                    "copies of one $::patch expression disagree on type, initial value, or address sink");
                diagnostics_.note(original.location, "first use of this patch expression is here");
                failed_ = true;
                return std::nullopt;
            }
            if (!materializer->supports_shared_cell) {
                diagnostics_.error(expression.location,
                    "selected target cannot share one $::patch cell across copied uses");
                failed_ = true;
                return std::nullopt;
            }
            identity = original.patch_id;
        } else {
            if (sink) {
                const auto key = std::pair{sink->object.value, sink->offset};
                if (patch_sinks_.contains(key) || !current_patch_sinks_.insert(key).second) {
                    diagnostics_.error(expression.arguments[1]->location,
                        "$::patch address sink is used by more than one site");
                    failed_ = true;
                    return std::nullopt;
                }
            }
            identity = next_patch_id_++;
        }
        const auto result = add_value(ValueKind::PatchValue, *type,
                                      expression.location);
        auto& patch = current_.values[result.value];
        if (initial) {
            patch.integer = initial->value.low;
            patch.integer_high = initial->value.high;
        } else {
            patch.patch_initial_address = *initial_address;
        }
        patch.patch_id = identity;
        if (sink) patch.patch_sink = *sink;
        if (origin.source_unit && !copied) current_patch_origins_.emplace(origin, result);
        return result;
    }

    ContinuationTask<std::optional<ValueId>> lower_logical_async(const Expr& expression) {
        auto left = co_await lower_expression_async(*expression.left);
        if (!left || !current_block_) co_return std::nullopt;
        left = booleanize(*left, expression.left->location);
        const auto short_predecessor = *current_block_;
        const auto short_value = constant(expression.text == "||" ? 1 : 0,
                                          *hir_.builtin(BuiltinType::Bool),
                                          expression.location);
        const auto right_block = new_block(expression.right->location);
        const auto merge_block = new_block(expression.location);
        const auto first = expression.text == "&&" ? right_block : merge_block;
        const auto second = expression.text == "&&" ? merge_block : right_block;
        terminate(TerminatorKind::ConditionalBranch, expression.location, *left,
                  {first, second});

        enter(right_block);
        auto right = co_await lower_expression_async(*expression.right);
        if (!right) co_return std::nullopt;
        right = booleanize(*right, expression.right->location);
        const auto right_predecessor = *current_block_;
        terminate(TerminatorKind::Branch, expression.location, std::nullopt,
                  {merge_block});

        enter(merge_block);
        const auto result = add_value(ValueKind::Phi,
                                      *hir_.builtin(BuiltinType::Bool),
                                      expression.location);
        current_.values[result.value].incoming = {
            {short_predecessor, short_value}, {right_predecessor, *right},
        };
        co_return result;
    }

    std::optional<ValueId> lower_logical(const Expr& expression) {
        return lower_logical_async(expression).run();
    }

    ContinuationTask<std::optional<ValueId>> lower_conditional_async(const Expr& expression) {
        const auto then_type = infer_type(*expression.right);
        const auto else_type = infer_type(*expression.third);
        if (!then_type || !else_type) co_return std::nullopt;
        const auto common = void_type(hir_, *then_type) && void_type(hir_, *else_type)
            ? then_type : conditional_type(*then_type, *else_type);
        if (!common) {
            diagnostics_.error(expression.location, "conditional operands have no compatible common type");
            co_return std::nullopt;
        }
        auto condition = co_await lower_expression_async(*expression.left);
        if (!condition || !current_block_) co_return std::nullopt;
        condition = booleanize(*condition, expression.left->location);
        const auto then_block = new_block(expression.right->location);
        const auto else_block = new_block(expression.third->location);
        const auto merge_block = new_block(expression.location);
        terminate(TerminatorKind::ConditionalBranch, expression.location, *condition,
                  {then_block, else_block});

        enter(then_block);
        auto then_value = co_await lower_expression_async(*expression.right, *common);
        if (!then_value || !current_block_) co_return std::nullopt;
        const auto then_predecessor = *current_block_;
        terminate(TerminatorKind::Branch, expression.location, std::nullopt,
                  {merge_block});

        enter(else_block);
        auto else_value = co_await lower_expression_async(*expression.third, *common);
        if (!else_value || !current_block_) co_return std::nullopt;
        const auto else_predecessor = *current_block_;
        terminate(TerminatorKind::Branch, expression.location, std::nullopt,
                  {merge_block});

        enter(merge_block);
        if (void_type(hir_, *common))
            co_return add_value(ValueKind::VoidValue, *common, expression.location);
        const auto result = add_value(ValueKind::Phi, *common, expression.location);
        current_.values[result.value].incoming = {
            {then_predecessor, *then_value}, {else_predecessor, *else_value},
        };
        co_return result;
    }

    std::optional<ValueId> lower_conditional(const Expr& expression) {
        return lower_conditional_async(expression).run();
    }

    void lower_statement(const Statement& statement) {
        if (failed_) return;
        forget_types();
        switch (statement.kind) {
        case Statement::Kind::DeclarationList:
            for (const auto& child : statement.statements) {
                if (current_block_) lower_statement(*child);
            }
            return;
        case Statement::Kind::Compound: {
            scopes_.emplace_back();
            for (const auto& child : statement.statements) {
                if (!current_block_ &&
                    child->kind != Statement::Kind::Label &&
                    child->kind != Statement::Kind::Case &&
                    child->kind != Statement::Kind::Default &&
                    child->kind != Statement::Kind::Compound) {
                    if (!contains_entry_label(*child)) continue;
                    // Lower an unreachable head so a nested label or case
                    // keeps its body; pruning removes the head again.
                    enter(new_block(child->location));
                }
                lower_statement(*child);
            }
            if (current_block_) {
                end_lifetimes_from(scopes_.size() - 1, statement.location);
            }
            scopes_.pop_back();
            return;
        }
        case Statement::Kind::Empty:
            return;
        case Statement::Kind::StaticAssert:
            diagnostics_.error(statement.location, "block assertion reached runtime lowering unprepared");
            failed_ = true;
            return;
        case Statement::Kind::Case:
        case Statement::Kind::Default: {
            const auto found = case_blocks_.find(&statement);
            if (found == case_blocks_.end()) { failed_ = true; return; }
            if (current_block_)
                terminate(TerminatorKind::Branch, statement.location, std::nullopt, {found->second});
            enter(found->second);
            if (statement.first) lower_statement(*statement.first);
            return;
        }
        case Statement::Kind::Switch:
            lower_switch(statement);
            return;
        case Statement::Kind::Declaration: {
            if (!current_block_ || scopes_.empty()) { failed_ = true; return; }
            const auto& declaration = *statement.declaration;
            std::optional<std::string> physical_location;
            if (declaration.location_name) {
                if (!declaration.storage_register) {
                    diagnostics_.error(
                        declaration.location,
                        "an object location requires the register storage "
                        "specifier");
                    failed_ = true;
                    return;
                }
                const auto* view = find_register(
                    target_, *declaration.location_name);
                // A naked body may bind any register an instruction operand
                // can carry, including compiler-owned ones.
                const auto* modes = !view ? nullptr
                                    : naked_ ? &view->instruction_scalar_modes
                                             : &view->hard_scalar_modes;
                if (!modes || modes->empty()) {
                    diagnostics_.error(
                        declaration.location,
                        "target register '" + *declaration.location_name +
                            "' cannot carry a managed hard-bound scalar");
                    failed_ = true;
                    return;
                }
                if (view->compiler_owned && !naked_) {
                    diagnostics_.error(
                        declaration.location,
                        "hard register object cannot use compiler-owned "
                        "register '" +
                            std::string(view->storage) + "'");
                    failed_ = true;
                    return;
                }
                const auto bits = type_bits(declaration.type);
                const auto floating = is_floating(declaration.type);
                const bool valid_type = std::any_of(
                    modes->begin(), modes->end(),
                    [&](const RegisterEntry::ScalarMode& mode) {
                        return mode.bits == bits &&
                               mode.floating == floating;
                    });
                if (!valid_type) {
                    diagnostics_.error(
                        declaration.location,
                        "hard register '" +
                            *declaration.location_name +
                            "' does not match object type '" +
                            type_name(declaration.type) + "'");
                    failed_ = true;
                    return;
                }
                for (const auto& scope : scopes_) {
                    for (const auto& [name, binding] :
                         scope.bindings) {
                        (void)name;
                        if (binding.dynamic_address) continue;
                        const auto& active =
                            current_.slots[binding.slot.value];
                        if (!active.physical_location) continue;
                        const auto* active_view = find_register(
                            target_, *active.physical_location);
                        if (active_view &&
                            view->storage == active_view->storage) {
                            diagnostics_.error(
                                declaration.location,
                                "overlapping hard register object '" +
                                    std::string(view->storage) +
                                    "'");
                            failed_ = true;
                            return;
                        }
                    }
                }
                physical_location = *declaration.location_name;
            } else if (naked_ && !declaration.storage_static) {
                diagnostics_.error(
                    declaration.location,
                    "ordinary automatic and stack objects are not permitted "
                    "in a naked function; bind the object to a register");
                failed_ = true;
                return;
            } else if (declaration.storage_register) {
                // `register` without a fixed location remains an optimizer
                // preference and uses an ordinary managed slot.
            }
            const auto type = hir_.intern_type(declaration.type);
            const bool array = array_type(hir_, type);
            const bool record = hir_.type(type).kind ==
                                hir::Type::Kind::Record;
            const bool aggregate = array || record;
            if (aggregate && (declaration.storage_register ||
                              declaration.location_name)) {
                diagnostics_.error(
                    declaration.location,
                    "an aggregate object cannot be bound to a machine register");
                failed_ = true;
                return;
            }
            const bool string_array_initializer =
                array && declaration.initializer &&
                declaration.initializer->kind == Expr::Kind::String &&
                hir_.type(type).lanes != 0 &&
                hir_.type(type).element &&
                hir_.type(*hir_.type(type).element).kind ==
                    hir::Type::Kind::Builtin &&
                hir_.type(*hir_.type(type).element).builtin ==
                    BuiltinType::U8;
            if (hir_.type(type).is_atomic &&
                !atomic_object_type(hir_, type)) {
                diagnostics_.error(
                    declaration.location,
                    "atomic qualifier requires an integer, floating, or pointer object type");
                failed_ = true;
                return;
            }
            if (hir_.type(type).is_atomic && physical_location) {
                diagnostics_.error(
                    declaration.location,
                    "an atomic object cannot be bound to a machine register");
                failed_ = true;
                return;
            }
            if (!managed_object_type(hir_, type) ||
                scopes_.back().bindings.contains(name_key(declaration))) {
                failed_ = true;
                return;
            }
            const bool dynamic_array =
                array && hir_.type(type).lanes == 0;
            if (dynamic_array) {
                if (!declaration.dynamic_array_bound) {
                    diagnostics_.error(
                        declaration.location,
                        "variable-length array has no runtime bound");
                    failed_ = true;
                    return;
                }
                auto bound = lower_expression(
                    *declaration.dynamic_array_bound);
                if (!bound ||
                    !integer_type(hir_, current_.values[bound->value].type)) {
                    diagnostics_.error(
                        declaration.dynamic_array_bound->location,
                        "variable-length array bound must have integer type");
                    failed_ = true;
                    return;
                }
                if (type_bits(
                        hir_, current_.values[bound->value].type) > 64) {
                    diagnostics_.error(
                        declaration.dynamic_array_bound->location,
                        "variable-length array bound cannot exceed pointer width");
                    failed_ = true;
                    return;
                }
                const auto element = *hir_.type(type).element;
                const auto element_size =
                    storage_size(hir_, element, target_);
                const auto alignment = std::max(
                    {16U, storage_alignment(hir_, element, target_),
                     declaration.explicit_alignment});
                if (element_size == 0) {
                    diagnostics_.error(
                        declaration.location,
                        "variable-length array element has no fixed storage size");
                    failed_ = true;
                    return;
                }
                if (!scopes_.back().dynamic_stack_mark) {
                    scopes_.back().dynamic_stack_mark =
                        dynamic_stack_save(declaration.location);
                }
                const auto dynamic_count = cast(
                    *bound, *hir_.builtin(BuiltinType::Uptr),
                    declaration.location);
                auto dynamic_size = dynamic_count;
                if (element_size != 1) {
                    const auto scale = constant(
                        element_size, *hir_.builtin(BuiltinType::Uptr),
                        declaration.location);
                    const auto bytes = add_value(
                        ValueKind::Binary,
                        *hir_.builtin(BuiltinType::Uptr),
                        declaration.location);
                    current_.values[bytes.value].binary =
                        BinaryOperation::Multiply;
                    current_.values[bytes.value].operands = {
                        dynamic_size, scale};
                    dynamic_size = bytes;
                }
                // Capture the extent before allocation's address result can
                // overwrite the register carrying the narrow bound.
                const auto address = dynamic_alloca(
                    *bound, element, element_size, alignment,
                    declaration.location);
                const LocalBinding binding{{}, type, address, dynamic_size};
                dynamic_counts_.emplace(address.value, dynamic_count);
                if (declaration.initializer &&
                    !initialize_dynamic_array(
                        binding, *declaration.initializer, dynamic_count)) {
                    failed_ = true;
                    return;
                }
                scopes_.back().bindings.emplace(name_key(declaration), binding);
                forget_types();
                return;
            }
            const SlotId slot{static_cast<std::uint32_t>(current_.slots.size())};
            current_.slots.push_back({slot, declaration.location, type,
                                      declaration.name,
                                      std::move(physical_location),
                                      declaration.type->is_volatile,
                                      address_taken_names_.contains(
                                          name_key(declaration)),
                                      false, 1, std::nullopt});
            current_.slots.back().minimum_alignment =
                declaration.explicit_alignment;
            const LocalBinding binding{slot, type, std::nullopt,
                                       std::nullopt,
                                       declaration.storage_register};
            scopes_.back().bindings.emplace(name_key(declaration), binding);
            forget_types();
            scopes_.back().slots.push_back(slot);
            (void)lifetime(ValueKind::LifetimeStart, slot, declaration.location);
            if (declaration.initializer) {
                if (string_array_initializer) {
                    if (!initialize_string_array(
                            binding, *declaration.initializer)) {
                        diagnostics_.error(
                            declaration.initializer->location,
                            "aggregate array initializers are not implemented yet");
                        failed_ = true;
                    }
                    return;
                }
                if (declaration.initializer->kind ==
                    Expr::Kind::AggregateInitializer) {
                    (void)initialize_aggregate(binding,
                                               *declaration.initializer);
                    return;
                }
                auto initializer = lower_expression(*declaration.initializer, type);
                if (!initializer) { failed_ = true; return; }
                (void)store_slot(binding, *initializer, declaration.location);
            }
            return;
        }
        case Statement::Kind::Expression:
            if (statement.expression && !lower_expression(*statement.expression)) {
                failed_ = true;
            }
            return;
        case Statement::Kind::Return: {
            if (!current_block_) { failed_ = true; return; }
            if (naked_) {
                diagnostics_.error(
                    statement.location,
                    "ordinary return is not permitted in a naked function; "
                    "use an explicit target control-transfer built-in");
                failed_ = true;
                return;
            }
            const Attribute* musttail{};
            for (const auto& attribute : statement.attributes) {
                if (attribute.name == "musttail") musttail = &attribute;
            }
            if (musttail) {
                if (!returned_call(statement)) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail requires returning a call expression directly");
                    failed_ = true;
                    return;
                }
                if (has_dynamic_arrays_) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail cannot restore variable-length array storage before the tail transfer");
                    failed_ = true;
                    return;
                }
            }
            std::optional<ValueId> result;
            if (statement.expression) {
                musttail_call_ = musttail ? returned_call(statement) : nullptr;
                result = lower_expression(*statement.expression, current_.result_type);
                musttail_call_ = nullptr;
            }
            if (musttail && result) {
                auto& call = current_.values[result->value];
                if (call.kind != ValueKind::Call) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail requires the returned value to be the direct result of the call");
                    failed_ = true;
                    return;
                }
                if (!current_block_) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail cannot target a call lowered as noreturn");
                    failed_ = true;
                    return;
                }
                const auto signature = hir::call_signature(
                    hir_, call.callee, call.call_signature);
                const auto& caller = hir_.function(current_.source);
                const bool caller_outputs = std::any_of(
                    caller.parameters.begin(), caller.parameters.end(),
                    [](const hir::Parameter& parameter) {
                        return parameter.mode != ParameterMode::In;
                    });
                const bool callee_outputs =
                    !signature || std::any_of(
                        signature->parameters.begin(),
                        signature->parameters.end(),
                        [](const hir::Parameter& parameter) {
                            return parameter.mode != ParameterMode::In;
                        });
                if ((caller_outputs || callee_outputs) &&
                    (!signature || hir::manual_interface(caller) ||
                     hir::manual_interface(*signature))) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail output-parameter forwarding through manual "
                        "endpoints is not implemented");
                    failed_ = true;
                    return;
                }
                // The call may end the lifetimes of its own argument cells.
                const auto& values =
                    current_.blocks[current_block_->value].values;
                const auto last = std::find(values.rbegin(), values.rend(),
                                            *result);
                if (last == values.rend() ||
                    !std::all_of(values.rbegin(), last, [&](ValueId value) {
                        return current_.values[value.value].kind ==
                               ValueKind::LifetimeEnd;
                    })) {
                    diagnostics_.error(
                        musttail->location,
                        "musttail call requires work after the call and cannot be a tail transfer");
                    failed_ = true;
                    return;
                }
                call.must_tail = true;
            }
            const bool void_result = void_type(hir_, current_.result_type);
            if ((!result && !void_result) ||
                (result && void_result &&
                 !void_type(hir_, current_.values[result->value].type))) {
                failed_ = true;
                return;
            }
            // A tail call has already delivered or forwarded every output.
            if (!musttail) copy_out_parameters(statement.location);
            end_lifetimes_from(0, statement.location);
            terminate(TerminatorKind::Return, statement.location,
                      void_result ? std::nullopt : result, {});
            return;
        }
        case Statement::Kind::If:
            lower_if(statement);
            return;
        case Statement::Kind::While:
            lower_while(statement);
            return;
        case Statement::Kind::DoWhile:
            lower_do_while(statement);
            return;
        case Statement::Kind::For:
            lower_for(statement);
            return;
        case Statement::Kind::Label: {
            const auto* label =
                hir_.label(current_.source, statement);
            if (!label) { failed_ = true; return; }
            const auto found = label_blocks_.find(label->id.value);
            if (found == label_blocks_.end()) { failed_ = true; return; }
            if (current_block_) {
                terminate(TerminatorKind::Branch, statement.location,
                          std::nullopt, {found->second});
            }
            enter(found->second);
            if (statement.first) lower_statement(*statement.first);
            return;
        }
        case Statement::Kind::Goto: {
            if (!current_block_ || !statement.expression) {
                failed_ = true;
                return;
            }
            if (statement.expression->kind == Expr::Kind::Name &&
                statement.expression->text.find("::") == std::string::npos) {
                if (const auto* label = hir_.label(
                        current_.source, *statement.expression)) {
                    const auto found = label_blocks_.find(label->id.value);
                    if (found == label_blocks_.end()) {
                        failed_ = true;
                        return;
                    }
                    if (has_dynamic_arrays_ &&
                        !supports_direct_vla_transition(statement,
                                                        label->id)) {
                        diagnostics_.error(
                            statement.location,
                            "direct goto would enter or change variable-length array storage state");
                        failed_ = true;
                        return;
                    }
                    const auto point =
                        label_control_points_.find(label->id.value);
                    if (point != label_control_points_.end() &&
                        point->second.scope_depth < scopes_.size()) {
                        end_lifetimes_from(point->second.scope_depth,
                                           statement.location);
                    }
                    terminate(TerminatorKind::Branch, statement.location,
                              std::nullopt, {found->second});
                    return;
                }
                if (resolved_label_binding(*statement.expression).kind == LabelBinding::Kind::Reference) {
                    diagnostics_.error(statement.expression->location,
                        "goto target does not name a visible label in its retained source binding");
                    failed_ = true;
                    return;
                }
            }
            auto destination = lower_expression(*statement.expression);
            if (!destination ||
                !label_type(hir_, current_.values[destination->value].type)) {
                diagnostics_.error(statement.expression->location,
                    "goto target does not name a visible label or label-valued expression");
                failed_ = true;
                return;
            }
            const auto& destination_value = current_.values[destination->value];
            if (destination_value.kind == ValueKind::LabelAddress && destination_value.label &&
                hir_.labels.at(destination_value.label->value).owner != current_.source) {
                diagnostics_.error(statement.expression->location,
                    "managed goto target must belong to the current function");
                failed_ = true;
                return;
            }
            std::vector<BlockId> targets;
            targets.reserve(current_.labels.size());
            for (const auto& label : current_.labels) {
                targets.push_back(label.block);
            }
            if (targets.empty()) { failed_ = true; return; }
            terminate(TerminatorKind::IndirectBranch, statement.location,
                      *destination, std::move(targets));
            return;
        }
        case Statement::Kind::Break:
            if (loops_.empty() || !current_block_) { failed_ = true; return; }
            end_lifetimes_from(loops_.back().retained_scopes, statement.location);
            terminate(TerminatorKind::Branch, statement.location, std::nullopt,
                      {loops_.back().break_target});
            return;
        case Statement::Kind::Continue: {
            if (loops_.empty() || !current_block_) { failed_ = true; return; }
            const auto loop = std::find_if(loops_.rbegin(), loops_.rend(),
                                          [](const LoopContext& context) { return !context.is_switch; });
            if (loop == loops_.rend()) { failed_ = true; return; }
            end_lifetimes_from(loop->retained_scopes, statement.location);
            terminate(TerminatorKind::Branch, statement.location, std::nullopt,
                      {loop->continue_target});
            return;
        }
        default:
            failed_ = true;
            return;
        }
    }

    // The variable-length array whose scope a jump from the switch to `label`
    // would enter, if any.
    const Statement* entered_dynamic_array(const Statement& switch_statement,
                                           const Statement& label) const {
        const auto source = statement_control_points_.find(&switch_statement);
        const auto destination = statement_control_points_.find(&label);
        if (source == statement_control_points_.end() ||
            destination == statement_control_points_.end()) {
            return nullptr;
        }
        const auto& active = source->second.dynamic_arrays;
        for (const auto& array : destination->second.dynamic_arrays) {
            if (std::find(active.begin(), active.end(), array) == active.end()) {
                return array.declaration;
            }
        }
        return nullptr;
    }

    void lower_switch(const Statement& statement) {
        auto selector = lower_expression(*statement.condition);
        if (!selector || !integer_type(hir_, current_.values[selector->value].type)) {
            diagnostics_.error(statement.location, "switch requires an integer or enumeration selector");
            failed_ = true;
            return;
        }
        const auto type = *promote(current_.values[selector->value].type);
        selector = cast(*selector, type, statement.location);
        const auto end = new_block(statement.location);
        auto fallback = end;
        std::vector<std::pair<UInt128, BlockId>> cases;
        const auto collect = [&](const auto& self, const Statement& node) -> void {
            if (node.kind == Statement::Kind::Switch) return;
            if (node.kind == Statement::Kind::Case || node.kind == Statement::Kind::Default) {
                if (const auto* array = entered_dynamic_array(statement, node)) {
                    diagnostics_.error(
                        node.location,
                        std::string(node.kind == Statement::Kind::Case ? "case" : "default") +
                            " label would enter the scope of variable-length array '" +
                            array->declaration->name + "'");
                    failed_ = true;
                }
                const auto block = new_block(node.location);
                case_blocks_.emplace(&node, block);
                if (node.kind == Statement::Kind::Default) {
                    fallback = block;
                    if (node.first) self(self, *node.first);
                    return;
                }
                const auto parsed = patch_initial(*node.expression, hir_.address_bits);
                if (!parsed) {
                    diagnostics_.error(node.location, "case requires a translation-time integer constant");
                    failed_ = true;
                    return;
                }
                const IntegerType source_type{builtin_bits(parsed->type, hir_.address_bits), builtin_signed(parsed->type)};
                const IntegerType destination_type{type_bits(hir_, type), signed_type(hir_, type)};
                const auto value = convert_integer(parsed->value, source_type, destination_type);
                if (convert_integer(value, destination_type, source_type) != parsed->value ||
                    integer_negative(value, destination_type) != integer_negative(parsed->value, source_type)) {
                    diagnostics_.error(node.location, "case value is not representable in the promoted selector type");
                    failed_ = true;
                }
                if (std::any_of(cases.begin(), cases.end(), [&](const auto& entry) { return entry.first == value; })) {
                    diagnostics_.error(node.location, "duplicate case value");
                    failed_ = true;
                }
                cases.emplace_back(value, block);
                if (node.first) self(self, *node.first);
                return;
            }
            for (const auto& child : node.statements) self(self, *child);
            if (node.first) self(self, *node.first);
            if (node.second) self(self, *node.second);
        };
        collect(collect, *statement.first);
        if (failed_) return;
        const auto bound = unsigned_upper_bound_at_exit(current_, *selector, *current_block_);
        if (bound && bound->high == 0 && bound->low <= cases.size()) {
            bool covered = true;
            for (std::uint64_t value = 0; value < bound->low; ++value)
                covered &= std::any_of(cases.begin(), cases.end(),
                    [&](const auto& entry) { return entry.first == UInt128{value}; });
            if (covered) {
                fallback = new_block(statement.location);
                current_.blocks[fallback.value].terminator.kind = TerminatorKind::Unreachable;
                current_.blocks[fallback.value].terminator.effect = current_.blocks[fallback.value].effect;
                current_.blocks[fallback.value].terminator.location = statement.location;
            }
        }
        for (const auto& [value, block] : cases) {
            const auto literal = constant(value, type, statement.location);
            const auto condition = add_value(ValueKind::Binary, *hir_.builtin(BuiltinType::Bool), statement.location);
            current_.values[condition.value].binary = BinaryOperation::Equal;
            current_.values[condition.value].operands = {*selector, literal};
            const auto next = new_block(statement.location);
            terminate(TerminatorKind::ConditionalBranch, statement.location, condition, {block, next});
            enter(next);
        }
        terminate(TerminatorKind::Branch, statement.location, std::nullopt, {fallback});
        enter(new_block(statement.location));
        loops_.push_back({end, {}, scopes_.size(), true});
        lower_statement(*statement.first);
        loops_.pop_back();
        if (current_block_) terminate(TerminatorKind::Branch, statement.location, std::nullopt, {end});
        enter(end);
    }

    void lower_while(const Statement& statement) {
        const auto test = new_block(statement.location);
        const auto body = new_block(statement.first->location);
        const auto end = new_block(statement.location);
        terminate(TerminatorKind::Branch, statement.location, std::nullopt, {test});

        enter(test);
        auto condition = lower_expression(*statement.condition);
        if (!condition) { failed_ = true; return; }
        condition = booleanize(*condition, statement.condition->location);
        terminate(TerminatorKind::ConditionalBranch, statement.location, *condition,
                  {body, end});

        enter(body);
        loops_.push_back({end, test, scopes_.size()});
        lower_statement(*statement.first);
        loops_.pop_back();
        if (current_block_) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt, {test});
        }
        enter(end);
    }

    void lower_do_while(const Statement& statement) {
        const auto body = new_block(statement.first->location);
        const auto test = new_block(statement.condition->location);
        const auto end = new_block(statement.location);
        terminate(TerminatorKind::Branch, statement.location, std::nullopt, {body});

        enter(body);
        loops_.push_back({end, test, scopes_.size()});
        lower_statement(*statement.first);
        loops_.pop_back();
        if (current_block_) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt, {test});
        }

        enter(test);
        auto condition = lower_expression(*statement.condition);
        if (!condition) { failed_ = true; return; }
        condition = booleanize(*condition, statement.condition->location);
        terminate(TerminatorKind::ConditionalBranch, statement.location, *condition,
                  {body, end});
        enter(end);
    }

    void lower_for(const Statement& statement) {
        scopes_.emplace_back();
        lower_statement(*statement.first);
        if (!current_block_) { failed_ = true; scopes_.pop_back(); return; }
        const auto test = new_block(statement.location);
        const auto body = new_block(statement.second->location);
        const auto increment = new_block(statement.location);
        const auto end = new_block(statement.location);
        terminate(TerminatorKind::Branch, statement.location, std::nullopt, {test});

        enter(test);
        if (statement.condition) {
            auto condition = lower_expression(*statement.condition);
            if (!condition) { failed_ = true; scopes_.pop_back(); return; }
            condition = booleanize(*condition, statement.condition->location);
            terminate(TerminatorKind::ConditionalBranch, statement.location,
                      *condition, {body, end});
        } else {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt, {body});
        }

        enter(body);
        loops_.push_back({end, increment, scopes_.size()});
        lower_statement(*statement.second);
        loops_.pop_back();
        if (current_block_) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt,
                      {increment});
        }

        enter(increment);
        for (const auto& expression : statement.increments) {
            if (current_block_ && !lower_expression(*expression)) {
                failed_ = true;
                scopes_.pop_back();
                return;
            }
        }
        if (current_block_) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt, {test});
        }
        enter(end);
        end_lifetimes_from(scopes_.size() - 1, statement.location);
        scopes_.pop_back();
    }

    void lower_if(const Statement& statement) {
        auto condition = lower_expression(*statement.condition);
        if (!condition || !current_block_) { failed_ = true; return; }
        condition = booleanize(*condition, statement.condition->location);
        const auto then_block = new_block(statement.first->location);
        const auto else_block = new_block(statement.second ? statement.second->location
                                                           : statement.location);
        const auto merge_block = new_block(statement.location);
        terminate(TerminatorKind::ConditionalBranch, statement.location, *condition,
                  {then_block, else_block});

        enter(then_block);
        lower_statement(*statement.first);
        bool then_reaches = current_block_.has_value();
        if (then_reaches) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt,
                      {merge_block});
        }

        enter(else_block);
        if (statement.second) lower_statement(*statement.second);
        bool else_reaches = current_block_.has_value();
        if (else_reaches) {
            terminate(TerminatorKind::Branch, statement.location, std::nullopt,
                      {merge_block});
        }

        if (then_reaches || else_reaches) {
            enter(merge_block);
        } else {
            enter(merge_block);
            terminate(TerminatorKind::Unreachable, statement.location,
                      std::nullopt, {});
        }
    }

    hir::Module& hir_;
    const Subtarget& subtarget_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    // -fbounds-trap, and whether it instruments the current function.
    bool bounds_trap_{};
    bool bounds_checks_{};
    // The subscript operand of `&`, which may form the one-past address.
    const Expr* one_past_subscript_{};
    // Element counts of variable-length arrays, by allocation value.
    std::unordered_map<std::uint32_t, ValueId> dynamic_counts_;
    ManagedModule result_;
    ManagedFunction current_;
    std::optional<BlockId> current_block_;
    std::optional<EffectId> current_effect_;
    NameMap<ValueId> parameter_values_;
    // Parameter cells and the transport pointers their normal returns copy
    // them through.
    std::vector<std::pair<LocalBinding, ValueId>> copy_outs_;
    // The call of the `[[musttail]]` return being lowered.
    const Expr* musttail_call_{};
    NameSet address_taken_names_;
    NameSet modified_names_;
    NameSet local_names_;
    std::unordered_map<std::uint32_t, BlockId> label_blocks_;
    std::unordered_map<std::uint32_t, ControlPoint> label_control_points_;
    std::unordered_map<const Statement*, ControlPoint> statement_control_points_;
    std::unordered_map<const Statement*, BlockId> case_blocks_;
    std::vector<Scope> scopes_;
    std::unordered_map<const Expr*, std::optional<hir::TypeId>> inferred_types_;
    std::vector<LoopContext> loops_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> patch_sinks_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> current_patch_sinks_;
    std::unordered_map<TokenIdentity, ValueId, TokenIdentityHash> current_patch_origins_;
    std::uint32_t next_patch_id_{};
    bool has_dynamic_arrays_{};
    // A naked body: hard-bound registers only, raw exits, no frame.
    bool naked_{};
    bool failed_{};
};

bool comparison(BinaryOperation operation) {
    return operation >= BinaryOperation::Equal;
}

using OutBitInterval = InitializedBitRange;
using OutBitIntervals = std::vector<OutBitInterval>;

void add_out_interval(OutBitIntervals& intervals, OutBitInterval added) {
    if (added.begin >= added.end) return;
    intervals.push_back(added);
    std::sort(intervals.begin(), intervals.end(),
              [](const auto& left, const auto& right) {
                  return left.begin < right.begin;
              });
    std::size_t kept = 0;
    for (const auto& interval : intervals) {
        if (kept && interval.begin <= intervals[kept - 1].end) {
            intervals[kept - 1].end =
                std::max(intervals[kept - 1].end, interval.end);
        } else {
            intervals[kept++] = interval;
        }
    }
    intervals.resize(kept);
}

OutBitIntervals intersect_out_intervals(const OutBitIntervals& left,
                                        const OutBitIntervals& right) {
    OutBitIntervals result;
    std::size_t a = 0;
    std::size_t b = 0;
    while (a < left.size() && b < right.size()) {
        const auto begin = std::max(left[a].begin, right[b].begin);
        const auto end = std::min(left[a].end, right[b].end);
        if (begin < end) result.push_back({begin, end});
        if (left[a].end < right[b].end) ++a;
        else ++b;
    }
    return result;
}

bool out_intervals_cover(const OutBitIntervals& assigned,
                         const OutBitIntervals& needed) {
    return std::all_of(needed.begin(), needed.end(),
        [&](const OutBitInterval& part) {
            return std::any_of(assigned.begin(), assigned.end(),
                [&](const OutBitInterval& available) {
                    return available.begin <= part.begin &&
                           available.end >= part.end;
                });
        });
}

bool out_type_complete(const hir::Module& module, const TargetInfo& target,
                       hir::TypeId id, std::uint64_t base_bits,
                       const OutBitIntervals& assigned) {
    return initialized_type(module, id, base_bits, assigned,
        [&](hir::TypeId type) { return storage_size(module, type, target); });
}

// Run this on source MIR, before inlining or slot promotion can erase the
// parameter-cell writes. A normal return consumes every `out` cell even when
// the body contains no explicit load of it.
void check_out_definite_assignment(const ManagedFunction& function,
                                   const hir::Module& hir_module,
                                   const TargetInfo& target,
                                   Diagnostics& diagnostics) {
    const auto& source = hir_module.function(function.source);
    if (std::none_of(source.parameters.begin(), source.parameters.end(),
                    [](const auto& parameter) { return parameter.mode == ParameterMode::Out; })) return;
    const LocalPointerAnalysis pointers(function, hir_module, target);
    std::vector<bool> reachable(function.blocks.size());
    std::vector<BlockId> pending{function.entry};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id.value >= function.blocks.size() || reachable[id.value]) continue;
        reachable[id.value] = true;
        for (const auto successor :
             function.blocks[id.value].terminator.successors) {
            pending.push_back(successor);
        }
    }

    for (const auto& slot : function.slots) {
        if (!slot.source_parameter ||
            *slot.source_parameter >= source.parameters.size() ||
            source.parameters[*slot.source_parameter].mode != ParameterMode::Out) {
            continue;
        }
        const auto& parameter = source.parameters[*slot.source_parameter];
        const auto slot_size = storage_size(hir_module, slot.type, target);
        const OutBitIntervals top{{0, slot_size * 8}};
        std::vector<OutBitIntervals> assigned_in(function.blocks.size(), top);
        std::vector<OutBitIntervals> assigned_out(function.blocks.size(), top);
        assigned_in[function.entry.value].clear();
        const auto pointer_region = [&](const ManagedValue& value,
                                        const LocalAddress& address, bool write)
            -> std::optional<OutBitInterval> {
            if (address.slot != slot.id || !address.bytes ||
                *address.bytes > slot_size) return std::nullopt;
            const auto start = *address.bytes * 8;
            std::uint64_t begin = start;
            std::uint64_t end{};
            if (value.bit_field_region) {
                begin += value.bit_field_region->offset;
                end = begin + value.bit_field_region->width;
            } else {
                const auto type = write && value.operands.size() >= 2
                    ? function.values[value.operands[1].value].type
                    : value.type;
                end = begin + storage_size(hir_module, type, target) * 8;
            }
            if (end > slot_size * 8 || begin >= end) return std::nullopt;
            return OutBitInterval{begin, end};
        };
        const auto transfer_value = [&](const ManagedValue& value,
                                        OutBitIntervals assigned) {
            if (value.kind == ValueKind::LifetimeStart &&
                value.slot == slot.id) {
                assigned.clear();
            } else if (value.kind == ValueKind::Store &&
                       value.slot == slot.id) {
                add_out_interval(assigned, {0, slot_size * 8});
            } else if ((value.kind == ValueKind::PointerStore ||
                        (value.kind == ValueKind::Atomic && value.atomic == AtomicOperation::Store)) &&
                       !value.operands.empty()) {
                if (const auto address = pointers.targets(value.operands.front()).definite())
                    if (const auto region = pointer_region(value, *address, true))
                        add_out_interval(assigned, *region);
            }
            return assigned;
        };
        const auto transfer = [&](const ManagedBlock& block,
                                  OutBitIntervals assigned) {
            for (const auto id : block.values)
                assigned = transfer_value(function.values[id.value],
                                          std::move(assigned));
            return assigned;
        };
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto& block : function.blocks) {
                if (!reachable[block.id.value]) continue;
                OutBitIntervals incoming;
                if (block.id != function.entry &&
                    !block.predecessors.empty()) {
                    bool first = true;
                    for (const auto predecessor : block.predecessors) {
                        if (predecessor.value >= reachable.size() ||
                            !reachable[predecessor.value]) continue;
                        if (first) {
                            incoming = assigned_out[predecessor.value];
                            first = false;
                        } else {
                            incoming = intersect_out_intervals(
                                incoming, assigned_out[predecessor.value]);
                        }
                    }
                }
                const auto outgoing = transfer(block, incoming);
                if (assigned_in[block.id.value] != incoming ||
                    assigned_out[block.id.value] != outgoing) {
                    assigned_in[block.id.value] = incoming;
                    assigned_out[block.id.value] = outgoing;
                    changed = true;
                }
            }
        }
        // A tail call that forwards the parameter's transport pointer leaves
        // the assignment to its callee.
        const auto forwarded = [&](const ManagedBlock& block) {
            const auto pointer = *slot.source_parameter <
                                         function.parameters.size()
                ? std::optional<ValueId>(
                      function.parameters[*slot.source_parameter])
                : std::nullopt;
            return pointer && std::any_of(
                block.values.begin(), block.values.end(), [&](ValueId id) {
                    const auto& value = function.values[id.value];
                    return value.kind == ValueKind::Call && value.must_tail &&
                           std::any_of(
                               value.call_arguments.begin(),
                               value.call_arguments.end(),
                               [&](const CallArgument& argument) {
                                   return argument.value == pointer;
                               });
                });
        };
        bool read_reported = false;
        bool return_reported = false;
        for (const auto& block : function.blocks) {
            if (!reachable[block.id.value]) continue;
            auto assigned = assigned_in[block.id.value];
            for (const auto id : block.values) {
                const auto& value = function.values[id.value];
                const bool direct_read = value.kind == ValueKind::Load &&
                                         value.slot == slot.id &&
                                         !value.copy_out_read;
                const bool pointer_read = !value.bit_field_update_read &&
                    !value.copy_out_read &&
                    (value.kind == ValueKind::PointerLoad ||
                     (value.kind == ValueKind::Atomic &&
                      value.atomic != AtomicOperation::Store));
                bool pointer_read_ready = true;
                if (pointer_read && !value.operands.empty()) {
                    for (const auto& address : pointers.targets(value.operands.front()).addresses) {
                        if (address.slot != slot.id) continue;
                        const auto region = pointer_region(value, address, false);
                        if (!address.bytes) {
                            pointer_read_ready &= out_type_complete(hir_module, target, slot.type, 0, assigned);
                        } else if (region) {
                            const auto kind = hir_module.type(value.type).kind;
                            pointer_read_ready &= !value.bit_field_region &&
                                (kind == hir::Type::Kind::Record || kind == hir::Type::Kind::Array)
                                ? out_type_complete(hir_module, target, value.type, region->begin, assigned)
                                : out_intervals_cover(assigned, {*region});
                        }
                    }
                }
                if (!read_reported &&
                    ((direct_read &&
                      !out_type_complete(hir_module, target, slot.type, 0,
                                         assigned)) ||
                     !pointer_read_ready)) {
                    diagnostics.error(value.location,
                        "read of 'out' parameter '" + parameter.name +
                        "' before assignment");
                    read_reported = true;
                }
                assigned = transfer_value(value, std::move(assigned));
            }
            if (!out_type_complete(hir_module, target, slot.type, 0,
                                   assigned) && !return_reported &&
                block.terminator.kind == TerminatorKind::Return &&
                !forwarded(block)) {
                diagnostics.error(block.terminator.location,
                    "normal return leaves 'out' parameter '" +
                    parameter.name + "' unassigned");
                return_reported = true;
            }
        }
    }
}

bool verify_function(const ManagedFunction& function, const hir::Module& hir_module,
                     Diagnostics& diagnostics) {
    bool valid = true;
    const auto fail = [&](SourceLocation location, std::string message) {
        diagnostics.error(location, "invalid managed MIR: " + message);
        valid = false;
    };
    if (function.result_type.value >= hir_module.types.size()) {
        fail(function.location, "result type is out of range");
    }
    if (function.blocks.empty() || function.entry.value >= function.blocks.size()) {
        fail(function.location, "entry block is out of range");
        return false;
    }
    std::vector<std::optional<BlockId>> definition_block(function.values.size());
    std::vector<std::size_t> definition_order(function.values.size());
    std::vector<unsigned> definition_count(function.values.size());
    for (std::size_t block_index = 0; block_index < function.blocks.size(); ++block_index) {
        const auto& block = function.blocks[block_index];
        if (block.id.value != block_index) fail(block.location, "non-canonical block id");
        if (block.terminator.kind == TerminatorKind::None) {
            fail(block.location, "block has no terminator");
        }
        for (const auto successor : block.terminator.successors) {
            if (successor.value >= function.blocks.size()) {
                fail(block.terminator.location, "successor is out of range");
            }
        }
        std::unordered_set<std::uint32_t> predecessor_ids;
        for (const auto predecessor : block.predecessors) {
            if (predecessor.value >= function.blocks.size()) {
                fail(block.location, "predecessor is out of range");
            } else if (!predecessor_ids.insert(predecessor.value).second) {
                fail(block.location, "duplicate predecessor");
            }
        }
        for (std::size_t order = 0; order < block.values.size(); ++order) {
            const auto id = block.values[order];
            if (id.value >= function.values.size()) {
                fail(block.location, "value id is out of range");
                continue;
            }
            if (function.values[id.value].id != id) fail(block.location, "non-canonical value id");
            if (++definition_count[id.value] != 1) {
                fail(function.values[id.value].location, "value has multiple definitions");
            }
            definition_block[id.value] = block.id;
            definition_order[id.value] = order;
        }
    }
    for (std::size_t index = 0; index < function.values.size(); ++index) {
        if (function.values[index].id.value != index) {
            fail(function.values[index].location, "non-canonical value id");
        }
        if (function.values[index].type.value >= hir_module.types.size()) {
            fail(function.values[index].location, "value type is out of range");
        }
        if (definition_count[index] == 0) {
            fail(function.values[index].location, "value has no definition block");
        }
    }
    for (std::size_t index = 0; index < function.slots.size(); ++index) {
        const auto& slot = function.slots[index];
        if (slot.id.value != index) fail(slot.location, "non-canonical slot id");
        if (slot.type.value >= hir_module.types.size() ||
            !managed_object_type(hir_module, slot.type)) {
            fail(slot.location, "invalid stack slot type");
        }
        if (slot.minimum_alignment == 0 ||
            (slot.minimum_alignment & (slot.minimum_alignment - 1)) != 0) {
            fail(slot.location, "stack slot alignment is not a power of two");
        }
    }
    std::unordered_set<std::uint32_t> label_ids;
    std::unordered_set<std::uint32_t> label_blocks;
    const auto& source_function = hir_module.function(function.source);
    if (function.labels.size() != source_function.labels.size()) {
        fail(function.location, "local-label map does not cover HIR labels");
    }
    for (const auto& binding : function.labels) {
        if (binding.label.value >= hir_module.labels.size() ||
            hir_module.labels[binding.label.value].owner != function.source ||
            binding.block.value >= function.blocks.size() ||
            !label_ids.insert(binding.label.value).second ||
            !label_blocks.insert(binding.block.value).second) {
            fail(function.location, "invalid local-label map");
        }
    }
    std::vector<unsigned> effect_definition_count(function.effects.size());
    for (std::size_t index = 0; index < function.effects.size(); ++index) {
        if (function.effects[index].id.value != index) {
            fail(function.effects[index].location, "non-canonical effect id");
        }
    }
    for (const auto& block : function.blocks) {
        if (block.effect.value >= function.effects.size()) {
            fail(block.location, "block effect is out of range");
        } else {
            ++effect_definition_count[block.effect.value];
            const auto& effect = function.effects[block.effect.value];
            const auto expected = block.id == function.entry ? EffectKind::Entry
                                                              : EffectKind::Phi;
            if (effect.kind != expected) fail(effect.location, "invalid block effect kind");
        }
        if (block.terminator.effect.value >= function.effects.size()) {
            fail(block.terminator.location, "terminator effect is out of range");
        }
    }
    for (const auto& value : function.values) {
        if (!value.effect_output) continue;
        if (value.effect_output->value >= function.effects.size()) {
            fail(value.location, "operation effect is out of range");
            continue;
        }
        ++effect_definition_count[value.effect_output->value];
    }
    for (std::size_t index = 0; index < effect_definition_count.size(); ++index) {
        if (effect_definition_count[index] != 1) {
            fail(function.effects[index].location,
                 effect_definition_count[index] == 0
                     ? "effect has no definition" : "effect has multiple definitions");
        }
    }
    if (!valid) return false;

    for (const auto& block : function.blocks) {
        std::unordered_set<std::uint32_t> successor_ids;
        for (const auto successor : block.terminator.successors) {
            if (!successor_ids.insert(successor.value).second) {
                fail(block.terminator.location, "duplicate successor");
            }
            const auto& predecessors = function.blocks[successor.value].predecessors;
            if (std::find(predecessors.begin(), predecessors.end(), block.id) ==
                predecessors.end()) {
                fail(block.terminator.location, "CFG successor lacks matching predecessor");
            }
        }
        for (const auto predecessor : block.predecessors) {
            const auto& successors = function.blocks[predecessor.value].terminator.successors;
            if (std::find(successors.begin(), successors.end(), block.id) ==
                successors.end()) {
                fail(block.location, "CFG predecessor lacks matching successor");
            }
        }
    }

    for (const auto& block : function.blocks) {
        const auto& block_effect = function.effects[block.effect.value];
        if (block.id == function.entry) {
            if (block_effect.input || block_effect.operation ||
                !block_effect.incoming.empty()) {
                fail(block_effect.location, "entry effect has inputs");
            }
        } else {
            if (block_effect.input || block_effect.operation ||
                block_effect.incoming.size() != block.predecessors.size()) {
                fail(block_effect.location, "effect phi predecessor mismatch");
            }
            std::unordered_set<std::uint32_t> effect_predecessors;
            for (const auto& incoming : block_effect.incoming) {
                if (incoming.predecessor.value >= function.blocks.size() ||
                    std::find(block.predecessors.begin(), block.predecessors.end(),
                              incoming.predecessor) == block.predecessors.end()) {
                    fail(block_effect.location, "effect phi names a non-predecessor");
                    continue;
                }
                if (!effect_predecessors.insert(incoming.predecessor.value).second) {
                    fail(block_effect.location, "effect phi repeats a predecessor");
                }
                if (incoming.effect.value >= function.effects.size() ||
                    function.blocks[incoming.predecessor.value].terminator.effect !=
                        incoming.effect) {
                    fail(block_effect.location, "effect phi input does not match edge");
                }
            }
        }

        auto current_effect = block.effect;
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            const bool effectful = value.kind == ValueKind::LifetimeStart ||
                                   value.kind == ValueKind::LifetimeEnd ||
                                   value.kind == ValueKind::DynamicStackSave ||
                                   value.kind == ValueKind::DynamicAlloca ||
                                   value.kind == ValueKind::DynamicStackRestore ||
                                   value.kind == ValueKind::Load ||
                                   value.kind == ValueKind::Store ||
                                   value.kind == ValueKind::PointerLoad ||
                                   value.kind == ValueKind::PointerStore ||
                                   value.kind == ValueKind::IndexedLoad ||
                                   value.kind == ValueKind::GlobalLoad ||
                                   value.kind == ValueKind::GlobalStore ||
                                   value.kind == ValueKind::Atomic ||
                                   value.kind == ValueKind::Call ||
                                   (value.kind == ValueKind::Intrinsic &&
                                    value.intrinsic !=
                                        IntrinsicOperation::Expect) ||
                                   // Only an impure form joins the chain.
                                   (value.kind == ValueKind::MachineInstruction &&
                                    value.effect_input.has_value());
            if (!effectful) {
                if (value.effect_input || value.effect_output) {
                    fail(value.location, "pure value carries effects");
                }
                continue;
            }
            if (!value.effect_input || !value.effect_output ||
                *value.effect_input != current_effect ||
                value.effect_output->value >= function.effects.size()) {
                fail(value.location, "broken operation effect chain");
                continue;
            }
            const auto& output = function.effects[value.effect_output->value];
            if (output.kind != EffectKind::Operation ||
                output.input != value.effect_input || output.operation != value.id ||
                !output.incoming.empty()) {
                fail(value.location, "operation effect metadata disagrees");
            }
            current_effect = *value.effect_output;
        }
        if (block.terminator.effect != current_effect) {
            fail(block.terminator.location, "terminator does not consume final block effect");
        }
    }
    if (!valid) return false;

    std::vector<bool> listed_parameter(function.values.size());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const auto id = function.parameters[index];
        if (id.value >= function.values.size()) {
            fail(function.location, "parameter value is out of range");
            continue;
        }
        const auto& value = function.values[id.value];
        if (listed_parameter[id.value]) fail(value.location, "parameter is listed twice");
        listed_parameter[id.value] = true;
        if (value.kind != ValueKind::Parameter || value.parameter_index != index ||
            *definition_block[id.value] != function.entry) {
            fail(value.location, "invalid parameter definition");
        }
    }
    for (const auto& value : function.values) {
        if (value.kind == ValueKind::Parameter && !listed_parameter[value.id.value]) {
            fail(value.location, "parameter definition is not in the parameter list");
        }
    }
    if (!valid) return false;

    // Code in an unreachable block never runs, so its uses are only ordered
    // within that block.
    const DominatorTree dominance(function);
    const auto dominates = [&](BlockId block, BlockId definition) {
        return !dominance.reachable(block) || dominance.dominates(definition, block);
    };

    std::unordered_map<std::uint32_t, const ManagedValue*> patch_ids;
    for (const auto& block : function.blocks) {
        for (std::size_t order = 0; order < block.values.size(); ++order) {
            const auto& value = function.values[block.values[order].value];
            if (value.patch_initial_address &&
                value.kind != ValueKind::PatchValue) {
                fail(value.location,
                     "relocatable patch initial belongs only to a patch value");
            }
            if (value.must_tail) {
                if (value.kind != ValueKind::Call) {
                    fail(value.location,
                         "must-tail metadata requires a managed call");
                }
                const bool returned =
                    block.terminator.kind == TerminatorKind::Return &&
                    ((void_type(hir_module, value.type) &&
                      !block.terminator.value) ||
                     (!void_type(hir_module, value.type) &&
                      block.terminator.value == value.id));
                const bool trailing_lifetimes = std::all_of(
                    block.values.begin() +
                        static_cast<std::ptrdiff_t>(order + 1),
                    block.values.end(), [&](ValueId trailing) {
                        return function.values[trailing.value].kind ==
                               ValueKind::LifetimeEnd;
                    });
                if (!returned || !trailing_lifetimes) {
                    fail(value.location,
                         "must-tail call is not the final returned operation");
                }
            }
            if (value.kind == ValueKind::Phi) {
                if (void_type(hir_module, value.type))
                    fail(value.location, "phi must carry a value");
                if (value.incoming.size() != block.predecessors.size()) {
                    fail(value.location, "phi predecessor count mismatch");
                }
                std::unordered_set<std::uint32_t> incoming_predecessors;
                for (const auto& incoming : value.incoming) {
                    if (std::find(block.predecessors.begin(), block.predecessors.end(),
                                  incoming.predecessor) == block.predecessors.end()) {
                        fail(value.location, "phi names a non-predecessor block");
                        continue;
                    }
                    if (!incoming_predecessors.insert(incoming.predecessor.value).second) {
                        fail(value.location, "phi names a predecessor more than once");
                    }
                    if (incoming.value.value >= function.values.size() ||
                        function.values[incoming.value.value].type != value.type) {
                        fail(value.location, "phi incoming type mismatch");
                        continue;
                    }
                    const auto defining = *definition_block[incoming.value.value];
                    if (defining != incoming.predecessor &&
                        !dominates(incoming.predecessor, defining)) {
                        fail(value.location, "phi incoming value does not dominate its edge");
                    }
                }
                if (!value.operands.empty()) fail(value.location, "phi has ordinary operands");
                continue;
            }
            for (const auto operand : value.operands) {
                if (operand.value >= function.values.size()) {
                    fail(value.location, "operand value is out of range");
                    continue;
                }
                const auto defining = *definition_block[operand.value];
                if (defining == block.id) {
                    if (definition_order[operand.value] >= order) {
                        fail(value.location, "value is used before its definition");
                    }
                } else if (!dominates(block.id, defining)) {
                    fail(value.location, "value definition does not dominate its use");
                }
            }
            if (value.kind == ValueKind::Unary && value.operands.size() != 1) {
                fail(value.location, "unary operation has wrong arity");
            }
            if (value.kind == ValueKind::Binary && value.operands.size() != 2) {
                fail(value.location, "binary operation has wrong arity");
            }
            if (value.kind == ValueKind::Cast && value.operands.size() != 1) {
                fail(value.location, "cast has wrong arity");
            }
            if (value.kind == ValueKind::Select) {
                if (value.operands.size() != 3 ||
                    value.operands[0].value >= function.values.size() ||
                    value.operands[1].value >= function.values.size() ||
                    value.operands[2].value >= function.values.size()) {
                    fail(value.location, "select has wrong arity");
                } else {
                    const auto condition_type =
                        function.values[value.operands[0].value].type;
                    const auto truth_type =
                        function.values[value.operands[1].value].type;
                    const auto falsity_type =
                        function.values[value.operands[2].value].type;
                    const auto& condition_shape =
                        hir_module.type(condition_type);
                    const auto& result_shape =
                        hir_module.type(value.type);
                    const bool scalar_select =
                        integer_type(hir_module, condition_type) &&
                        (integer_type(hir_module, value.type) ||
                         pointer_type(hir_module, value.type));
                    const bool vector_select =
                        vector_integer_type(hir_module, condition_type) &&
                        vector_type(hir_module, value.type) &&
                        condition_shape.lanes == result_shape.lanes &&
                        condition_shape.element && result_shape.element &&
                        type_bits(hir_module, *condition_shape.element) ==
                            type_bits(hir_module, *result_shape.element);
                    if ((!scalar_select && !vector_select) ||
                        truth_type != value.type ||
                        falsity_type != value.type) {
                        fail(value.location, "select operand type mismatch");
                    }
                }
            }
            if (value.kind == ValueKind::Splat) {
                if (value.operands.size() != 1 ||
                    !vector_type(hir_module, value.type) ||
                    value.operands.front().value >= function.values.size() ||
                    hir_module.type(value.type).element !=
                        function.values[value.operands.front().value].type) {
                    fail(value.location, "invalid vector splat");
                }
            }
            if (value.kind == ValueKind::ExtractElement) {
                if (value.operands.size() != 2 ||
                    value.operands[0].value >= function.values.size() ||
                    value.operands[1].value >= function.values.size()) {
                    fail(value.location, "invalid vector extraction");
                } else {
                    const auto source_type =
                        function.values[value.operands[0].value].type;
                    const auto& source = hir_module.type(source_type);
                    if (!vector_type(hir_module, source_type) ||
                        source.element != value.type ||
                        !integer_type(
                            hir_module,
                            function.values[value.operands[1].value].type)) {
                        fail(value.location, "vector extraction type mismatch");
                    }
                }
            }
            if (value.kind == ValueKind::InsertElement) {
                if (value.operands.size() != 3 ||
                    value.operands[0].value >= function.values.size() ||
                    value.operands[1].value >= function.values.size() ||
                    value.operands[2].value >= function.values.size()) {
                    fail(value.location, "invalid vector insertion");
                } else {
                    const auto source_type =
                        function.values[value.operands[0].value].type;
                    const auto& source = hir_module.type(source_type);
                    if (source_type != value.type ||
                        !vector_type(hir_module, source_type) ||
                        !integer_type(
                            hir_module,
                            function.values[value.operands[1].value].type) ||
                        source.element !=
                            function.values[value.operands[2].value].type) {
                        fail(value.location, "vector insertion type mismatch");
                    }
                }
            }
            if (value.kind == ValueKind::IndexedAddress) {
                if (value.operands.size() != 2 ||
                    value.operands[0].value >= function.values.size() ||
                    value.operands[1].value >= function.values.size()) {
                    fail(value.location, "invalid indexed address");
                } else {
                    const auto base_id =
                        function.values[value.operands[0].value].type;
                    const auto& base = hir_module.type(base_id);
                    const auto& result = hir_module.type(value.type);
                    if (base.kind != hir::Type::Kind::Pointer ||
                        result.kind != hir::Type::Kind::Pointer ||
                        !base.pointee || !result.pointee ||
                        base.pointee != result.pointee ||
                        !integer_type(
                            hir_module,
                            function.values[value.operands[1].value].type)) {
                        fail(value.location, "indexed address type mismatch");
                    }
                }
            }
            if (value.kind == ValueKind::DynamicStackSave ||
                value.kind == ValueKind::DynamicAlloca ||
                value.kind == ValueKind::DynamicStackRestore) {
                const bool save =
                    value.kind == ValueKind::DynamicStackSave;
                const bool allocate =
                    value.kind == ValueKind::DynamicAlloca;
                const auto& type = hir_module.type(value.type);
                const bool metadata_clear =
                    !value.slot && !value.callee && !value.object &&
                    !value.patch_sink && !value.label &&
                    value.call_arguments.empty() &&
                    !value.is_volatile_access;
                if (!metadata_clear) {
                    fail(value.location,
                         "dynamic stack operation carries unrelated metadata");
                } else if (save) {
                    if (value.type !=
                            *hir_module.builtin(BuiltinType::Uptr) ||
                        !value.operands.empty() || value.integer != 0 ||
                        value.integer_high != 0) {
                        fail(value.location,
                             "invalid dynamic stack-save operation");
                    }
                } else if (allocate) {
                    const auto alignment = value.integer_high;
                    if (type.kind != hir::Type::Kind::Pointer ||
                        !type.pointee || value.operands.size() != 1 ||
                        value.operands.front().value >=
                            function.values.size() ||
                        !integer_type(
                            hir_module,
                            function.values[value.operands.front().value].type) ||
                        value.integer == 0 || alignment == 0 ||
                        (alignment & (alignment - 1U)) != 0) {
                        fail(value.location,
                             "invalid dynamic stack-allocation operation");
                    }
                } else if (value.type !=
                               *hir_module.builtin(BuiltinType::Void) ||
                           value.operands.size() != 1 ||
                           value.operands.front().value >=
                               function.values.size() ||
                           function.values[value.operands.front().value].type !=
                               *hir_module.builtin(BuiltinType::Uptr) ||
                           value.integer != 0 || value.integer_high != 0) {
                    fail(value.location,
                         "invalid dynamic stack-restore operation");
                }
            }
            if (value.kind == ValueKind::Intrinsic) {
                const bool expect =
                    value.intrinsic == IntrinsicOperation::Expect;
                const bool machine_nop =
                    value.intrinsic == IntrinsicOperation::MachineNop;
                const bool range = value.intrinsic == IntrinsicOperation::Assume &&
                    value.operands.size() == 1 && value.binary == BinaryOperation::UnsignedLess &&
                    value.operands.front().value < function.values.size() &&
                    integer_type(hir_module, function.values[value.operands.front().value].type);
                if (expect &&
                    (value.operands.size() != 1 ||
                     !integer_type(hir_module, value.type) ||
                     value.operands.front().value >= function.values.size() ||
                     function.values[value.operands.front().value].type !=
                         value.type ||
                     !fits_unsigned(
                         {value.integer, value.integer_high},
                         type_bits(hir_module, value.type)))) {
                    fail(value.location, "invalid $::expect MIR operation");
                } else if (machine_nop &&
                           (!value.operands.empty() ||
                            !void_type(hir_module, value.type) ||
                            value.integer != 0 || value.integer_high != 0)) {
                    fail(value.location,
                         "invalid managed machine-nop MIR operation");
                } else if (!expect && !machine_nop &&
                           ((!value.operands.empty() && !range) ||
                            !void_type(hir_module, value.type))) {
                    fail(value.location,
                         "invalid control-intrinsic MIR operation");
                }
            }
            if (value.kind == ValueKind::Atomic) {
                const auto fence =
                    value.atomic == AtomicOperation::ThreadFence ||
                    value.atomic == AtomicOperation::SignalFence;
                const auto expected_arity =
                    fence ? 0U :
                    value.atomic == AtomicOperation::Load ? 1U :
                    value.atomic == AtomicOperation::CompareExchange ? 3U
                                                                     : 2U;
                if (value.operands.size() != expected_arity || value.slot ||
                    value.callee || value.object || value.patch_sink ||
                    !value.call_arguments.empty()) {
                    fail(value.location, "invalid atomic operation metadata");
                } else if (fence) {
                    if (!void_type(hir_module, value.type) ||
                        value.is_volatile_access) {
                        fail(value.location, "invalid atomic fence");
                    }
                } else if (value.operands.empty() ||
                           value.operands.front().value >=
                               function.values.size()) {
                    fail(value.location, "atomic operation has no address");
                } else {
                    const auto& pointer = hir_module.type(
                        function.values[value.operands.front().value].type);
                    if (pointer.kind != hir::Type::Kind::Pointer ||
                        !pointer.pointee ||
                        !atomic_object_type(hir_module, *pointer.pointee)) {
                        fail(value.location,
                             "atomic operation address type mismatch");
                    } else {
                        const auto object_type = *pointer.pointee;
                        if (value.is_volatile_access !=
                            hir_module.type(object_type).is_volatile) {
                            fail(value.location,
                                 "atomic volatile metadata mismatch");
                        }
                        const bool result_value =
                            value.atomic == AtomicOperation::Load ||
                            value.atomic == AtomicOperation::Exchange ||
                            value.atomic == AtomicOperation::FetchAdd ||
                            value.atomic == AtomicOperation::FetchSub ||
                            value.atomic == AtomicOperation::FetchAnd ||
                            value.atomic == AtomicOperation::FetchXor ||
                            value.atomic == AtomicOperation::FetchOr ||
                            value.atomic == AtomicOperation::FetchUpdate;
                        if (result_value &&
                            !unqualified_representation_compatible(
                                hir_module, value.type, object_type)) {
                            fail(value.location,
                                 "atomic result type mismatch");
                        }
                        if (value.atomic == AtomicOperation::Store &&
                            !void_type(hir_module, value.type)) {
                            fail(value.location,
                                 "atomic store must return void");
                        }
                        if (value.atomic ==
                                AtomicOperation::CompareExchange &&
                            value.type !=
                                hir_module.builtin(BuiltinType::Bool)) {
                            fail(value.location,
                                 "atomic compare-exchange must return bool");
                        }
                        if (expected_arity >= 2 &&
                            value.operands[1].value < function.values.size()) {
                            const auto second = function.values[
                                value.operands[1].value].type;
                            if (value.atomic ==
                                AtomicOperation::CompareExchange) {
                                const auto& expected_pointer =
                                    hir_module.type(second);
                                if (expected_pointer.kind !=
                                        hir::Type::Kind::Pointer ||
                                    !expected_pointer.pointee ||
                                    hir_module.type(*expected_pointer.pointee)
                                        .is_const ||
                                    hir_module.type(*expected_pointer.pointee)
                                        .is_atomic ||
                                    !unqualified_representation_compatible(
                                        hir_module,
                                        *expected_pointer.pointee,
                                        object_type)) {
                                    fail(value.location,
                                         "atomic expected-pointer type mismatch");
                                }
                            } else if (!unqualified_representation_compatible(
                                           hir_module, second,
                                           object_type)) {
                                fail(value.location,
                                     "atomic source type mismatch");
                            }
                        }
                        if (value.atomic ==
                                AtomicOperation::CompareExchange &&
                            value.operands[2].value < function.values.size() &&
                            !unqualified_representation_compatible(
                                hir_module,
                                function.values[value.operands[2].value].type,
                                object_type)) {
                            fail(value.location,
                                 "atomic desired-value type mismatch");
                        }
                        const bool fetch =
                            value.atomic == AtomicOperation::FetchAdd ||
                            value.atomic == AtomicOperation::FetchSub ||
                            value.atomic == AtomicOperation::FetchAnd ||
                            value.atomic == AtomicOperation::FetchXor ||
                            value.atomic == AtomicOperation::FetchOr;
                        if (fetch && !integer_type(hir_module, object_type)) {
                            fail(value.location,
                                 "atomic fetch operation requires integer storage");
                        }
                        if (value.atomic == AtomicOperation::FetchUpdate) {
                            const bool arithmetic =
                                value.binary == BinaryOperation::Add ||
                                value.binary == BinaryOperation::Subtract ||
                                value.binary == BinaryOperation::Multiply ||
                                value.binary == BinaryOperation::SignedDivide ||
                                value.binary == BinaryOperation::UnsignedDivide ||
                                value.binary == BinaryOperation::SignedRemainder ||
                                value.binary == BinaryOperation::UnsignedRemainder ||
                                value.binary == BinaryOperation::BitAnd ||
                                value.binary == BinaryOperation::BitOr ||
                                value.binary == BinaryOperation::BitXor ||
                                value.binary == BinaryOperation::ShiftLeft ||
                                value.binary == BinaryOperation::ShiftRightArithmetic ||
                                value.binary == BinaryOperation::ShiftRightLogical;
                            const bool floating = floating_type(
                                hir_module, object_type);
                            const bool floating_operation =
                                value.binary == BinaryOperation::Add ||
                                value.binary == BinaryOperation::Subtract ||
                                value.binary == BinaryOperation::Multiply ||
                                value.binary == BinaryOperation::SignedDivide;
                            if (!arithmetic ||
                                (!integer_type(hir_module, object_type) &&
                                 !floating) ||
                                (floating && !floating_operation)) {
                                fail(value.location,
                                     "invalid atomic compound-update operation");
                            }
                        }
                    }
                }
                if (value.atomic == AtomicOperation::Load &&
                    (value.memory_order == MemoryOrder::Release ||
                     value.memory_order == MemoryOrder::AcqRel)) {
                    fail(value.location, "invalid atomic load order");
                }
                if (value.atomic == AtomicOperation::Store &&
                    (value.memory_order == MemoryOrder::Acquire ||
                     value.memory_order == MemoryOrder::AcqRel)) {
                    fail(value.location, "invalid atomic store order");
                }
                if (value.atomic == AtomicOperation::CompareExchange) {
                    const auto failure_valid =
                        value.failure_order != MemoryOrder::Release &&
                        value.failure_order != MemoryOrder::AcqRel &&
                        (value.memory_order == MemoryOrder::SeqCst ||
                         (value.memory_order == MemoryOrder::Acquire &&
                          (value.failure_order == MemoryOrder::Acquire ||
                           value.failure_order == MemoryOrder::Relaxed)) ||
                         (value.memory_order == MemoryOrder::AcqRel &&
                          (value.failure_order == MemoryOrder::Acquire ||
                           value.failure_order == MemoryOrder::Relaxed)) ||
                         ((value.memory_order == MemoryOrder::Relaxed ||
                           value.memory_order == MemoryOrder::Release) &&
                          value.failure_order == MemoryOrder::Relaxed));
                    if (!failure_valid) {
                        fail(value.location,
                             "invalid compare-exchange failure order");
                    }
                }
            }
            if ((value.kind == ValueKind::VoidValue ||
                 value.kind == ValueKind::Parameter ||
                 value.kind == ValueKind::ConstantInteger ||
                 value.kind == ValueKind::ConstantFloating ||
                 value.kind == ValueKind::LabelAddress ||
                 value.kind == ValueKind::SlotAddress ||
                 value.kind == ValueKind::GlobalAddress ||
                 value.kind == ValueKind::DynamicStackSave ||
                 value.kind == ValueKind::VariadicState ||
                 value.kind == ValueKind::PatchValue) &&
                !value.operands.empty()) {
                fail(value.location, "leaf value has operands");
            }
            if (value.kind != ValueKind::Phi && !value.incoming.empty()) {
                fail(value.location, "non-phi value has phi inputs");
            }
            if (value.kind == ValueKind::ConstantFloating &&
                !floating_type(hir_module, value.type)) {
                fail(value.location, "floating constant has a non-floating type");
            }
            if (value.kind == ValueKind::ConstantInteger &&
                !integer_type(hir_module, value.type) &&
                !pointer_type(hir_module, value.type) &&
                !label_type(hir_module, value.type)) {
                fail(value.location, "integer constant has a non-integer/pointer/label type");
            }
            if (value.kind == ValueKind::VoidValue &&
                !void_type(hir_module, value.type)) {
                fail(value.location, "void value has a non-void type");
            }
            if (value.kind == ValueKind::LabelAddress) {
                if (!value.label ||
                    value.label->value >= hir_module.labels.size() ||
                    (!hir_module.labels[value.label->value].definition &&
                     !hir_module.labels[value.label->value].is_global) ||
                    !label_type(hir_module, value.type)) {
                    fail(value.location, "invalid label address");
                }
            } else if (value.label) {
                fail(value.location, "non-label value carries label metadata");
            }
            if (value.kind == ValueKind::VariadicState) {
                const auto& entity = hir_module.function(function.source);
                const auto binding = std::find_if(
                    entity.variadic_bindings.begin(),
                    entity.variadic_bindings.end(),
                    [&](const hir::VariadicBinding& candidate) {
                        return candidate.state == value.variadic_state &&
                               candidate.type == value.type;
                    });
                if (!entity.variadic || value.variadic_state.empty() ||
                    binding == entity.variadic_bindings.end() || value.slot ||
                    value.callee || value.object || value.patch_sink ||
                    value.is_volatile_access ||
                    !value.call_arguments.empty()) {
                    fail(value.location, "invalid variadic-state value");
                }
            } else if (!value.variadic_state.empty()) {
                fail(value.location,
                     "non-variadic-state value carries state metadata");
            }
            const bool slot_operation = value.kind == ValueKind::LifetimeStart ||
                                        value.kind == ValueKind::LifetimeEnd ||
                                        value.kind == ValueKind::Load ||
                                        value.kind == ValueKind::Store;
            if (value.kind == ValueKind::SlotAddress) {
                if (!value.slot || value.slot->value >= function.slots.size()) {
                    fail(value.location, "slot address has an invalid slot");
                } else {
                    const auto& pointer = hir_module.type(value.type);
                    if (pointer.kind != hir::Type::Kind::Pointer ||
                        !pointer.pointee ||
                        *pointer.pointee !=
                            function.slots[value.slot->value].type ||
                        !function.slots[value.slot->value].address_taken) {
                        fail(value.location, "slot address type mismatch");
                    }
                }
                if (value.callee || value.object || value.patch_sink ||
                    value.is_volatile_access ||
                    !value.call_arguments.empty()) {
                    fail(value.location,
                         "slot address carries unrelated metadata");
                }
            } else if (value.kind == ValueKind::GlobalAddress) {
                if (value.slot || value.callee || value.patch_sink ||
                    value.is_volatile_access ||
                    !value.call_arguments.empty() || !value.object ||
                    value.object->value >= hir_module.objects.size()) {
                    fail(value.location,
                         "global address has an invalid object");
                } else {
                    const auto& pointer = hir_module.type(value.type);
                    if (pointer.kind != hir::Type::Kind::Pointer ||
                        !pointer.pointee ||
                        *pointer.pointee !=
                            hir_module.object(*value.object).type) {
                        fail(value.location, "global address type mismatch");
                    }
                }
            } else if (slot_operation) {
                if (!value.slot || value.slot->value >= function.slots.size()) {
                    fail(value.location, "slot operation has an invalid slot");
                } else {
                    const auto slot_type = function.slots[value.slot->value].type;
                    if (value.kind == ValueKind::Load &&
                        (value.type != slot_type || !value.operands.empty())) {
                        fail(value.location, "invalid load operation");
                    } else if (value.kind == ValueKind::Store &&
                               (value.type != *hir_module.builtin(BuiltinType::Void) ||
                                value.operands.size() != 1 ||
                                value.operands.front().value >= function.values.size() ||
                                !representation_compatible(
                                    hir_module,
                                    function.values[value.operands.front().value].type,
                                    slot_type))) {
                        fail(value.location, "invalid store operation");
                    } else if ((value.kind == ValueKind::LifetimeStart ||
                                value.kind == ValueKind::LifetimeEnd) &&
                               (value.type != *hir_module.builtin(BuiltinType::Void) ||
                                !value.operands.empty())) {
                        fail(value.location, "invalid lifetime operation");
                    }
                }
                if ((value.kind == ValueKind::Load ||
                     value.kind == ValueKind::Store) &&
                    value.slot &&
                    value.slot->value < function.slots.size() &&
                    value.is_volatile_access !=
                        function.slots[value.slot->value].is_volatile) {
                    fail(value.location,
                         "slot operation volatile metadata mismatch");
                }
                if (value.callee || value.object || value.patch_sink ||
                    !value.call_arguments.empty()) {
                    fail(value.location, "slot operation carries non-slot metadata");
                }
            } else if (value.kind == ValueKind::IndexedLoad) {
                if (value.slot || value.callee || value.object || value.patch_sink ||
                    !value.call_arguments.empty() ||
                    value.operands.size() != 2 ||
                    value.operands[0].value >= function.values.size() ||
                    value.operands[1].value >= function.values.size()) {
                    fail(value.location, "invalid indexed-load operation");
                } else {
                    const auto& base_type =
                        hir_module.type(function.values[value.operands[0].value].type);
                    const auto index_type =
                        function.values[value.operands[1].value].type;
                    const auto& result_type = hir_module.type(value.type);
                    const bool direct_element = base_type.pointee &&
                        unqualified_representation_compatible(
                            hir_module, *base_type.pointee, value.type);
                    const bool vector_element = base_type.pointee &&
                        result_type.kind == hir::Type::Kind::Vector &&
                        result_type.element &&
                        unqualified_representation_compatible(
                            hir_module, *base_type.pointee,
                            *result_type.element);
                    if (base_type.kind != hir::Type::Kind::Pointer ||
                        !base_type.pointee ||
                        (!direct_element && !vector_element) ||
                        !managed_value_type(hir_module, value.type) ||
                        !integer_type(hir_module, index_type) ||
                        value.is_volatile_access !=
                            hir_module.type(*base_type.pointee).is_volatile) {
                        fail(value.location, "indexed-load type or volatile metadata mismatch");
                    }
                }
            } else if (value.kind == ValueKind::PointerLoad ||
                       value.kind == ValueKind::PointerStore) {
                const bool store = value.kind == ValueKind::PointerStore;
                if (value.slot || value.callee || value.object || value.patch_sink ||
                    !value.call_arguments.empty() ||
                    value.operands.size() != (store ? 2U : 1U) ||
                    value.operands.front().value >= function.values.size()) {
                    fail(value.location, "invalid pointer memory operation");
                } else {
                    const auto& pointer = hir_module.type(
                        function.values[value.operands.front().value].type);
                    const auto stored_type = store &&
                            value.operands[1].value < function.values.size()
                        ? std::optional<hir::TypeId>{
                              function.values[value.operands[1].value].type}
                        : std::nullopt;
                    const auto packed_store = stored_type
                        ? [&] {
                              const auto& stored =
                                  hir_module.type(*stored_type);
                              return pointer.pointee &&
                                  stored.kind == hir::Type::Kind::Vector &&
                                  stored.element &&
                                  unqualified_representation_compatible(
                                      hir_module, *stored.element,
                                      *pointer.pointee);
                          }()
                        : false;
                    if (pointer.kind != hir::Type::Kind::Pointer || !pointer.pointee ||
                        !managed_value_type(hir_module, *pointer.pointee) ||
                        (value.memory_alignment != 0 &&
                         (value.memory_alignment &
                          (value.memory_alignment - 1U)) != 0) ||
                        value.is_volatile_access !=
                            hir_module.type(*pointer.pointee).is_volatile ||
                        (!store && value.type != *pointer.pointee) ||
                        (store && (hir_module.type(*pointer.pointee).is_const ||
                                   value.type != *hir_module.builtin(BuiltinType::Void) ||
                                   value.operands[1].value >= function.values.size() ||
                                   (!representation_compatible(
                                        hir_module,
                                        function.values[value.operands[1].value].type,
                                        *pointer.pointee) &&
                                    !packed_store)))) {
                        fail(value.location, "pointer memory operation type or volatile metadata mismatch");
                    }
                }
            } else if (value.kind == ValueKind::GlobalLoad ||
                       value.kind == ValueKind::GlobalStore) {
                if (value.slot || value.callee || value.patch_sink ||
                    !value.call_arguments.empty() || !value.object ||
                    value.object->value >= hir_module.objects.size()) {
                    fail(value.location, "global operation has an invalid object");
                } else {
                    const auto& object = hir_module.object(*value.object);
                    const auto& object_type = hir_module.type(object.type);
                    if (!managed_value_type(hir_module, object.type) ||
                        value.is_volatile_access != object_type.is_volatile) {
                        fail(value.location, "global operation type or volatile metadata mismatch");
                    } else if (value.kind == ValueKind::GlobalLoad &&
                               (value.type != object.type || !value.operands.empty())) {
                        fail(value.location, "invalid global-load operation");
                    } else if (value.kind == ValueKind::GlobalStore &&
                               (object_type.is_const ||
                                value.type != *hir_module.builtin(BuiltinType::Void) ||
                                value.operands.size() != 1 ||
                                value.operands.front().value >= function.values.size() ||
                                !representation_compatible(
                                    hir_module,
                                    function.values[value.operands.front().value].type,
                                    object.type))) {
                        fail(value.location, "invalid global-store operation");
                    }
                }
            } else if (value.kind == ValueKind::FunctionAddress) {
                const auto signature =
                    hir::call_signature(hir_module, value.callee, {});
                const auto& pointer = hir_module.type(value.type);
                if (!signature || value.slot || value.object ||
                    value.patch_sink || value.call_signature ||
                    !value.operands.empty() || !value.call_arguments.empty() ||
                    pointer.kind != hir::Type::Kind::Pointer ||
                    !pointer.pointee ||
                    hir_module.type(*pointer.pointee).function != signature) {
                    fail(value.location, "invalid typed function address");
                }
            } else if (value.kind == ValueKind::Atomic) {
                // Validated above; this branch keeps atomic volatile metadata
                // out of the pure-value catch-all below.
            } else if (value.kind == ValueKind::Call) {
                const auto signature = hir::call_signature(
                    hir_module, value.callee, value.call_signature);
                if (value.slot || !signature) {
                    fail(value.location, "call has an invalid callee");
                } else {
                    const auto& callee = *signature;
                    if (value.call_signature) {
                        if (value.operands.empty() ||
                            value.operands.front().value >=
                                function.values.size()) {
                            fail(value.location,
                                 "indirect call has no target operand");
                        } else {
                            const auto& pointer = hir_module.type(
                                function.values[value.operands.front().value]
                                    .type);
                            if (pointer.kind != hir::Type::Kind::Pointer ||
                                pointer.pointee != value.call_signature)
                                fail(value.location, "indirect call target and "
                                                     "signature disagree");
                        }
                    }
                    const auto count_valid =
                        callee.variadic ? value.call_arguments.size() >=
                                              callee.parameters.size()
                                        : value.call_arguments.size() ==
                                              callee.parameters.size();
                    if (!count_valid || callee.result_type != value.type) {
                        fail(value.location, "call signature mismatch");
                    } else {
                        std::size_t ordinary_index =
                            value.call_signature ? 1 : 0;
                        for (std::size_t index = 0;
                             index < value.call_arguments.size(); ++index) {
                            const auto& argument = value.call_arguments[index];
                            const bool unnamed =
                                index >= callee.parameters.size();
                            if (argument.unnamed != unnamed ||
                                (unnamed && !callee.variadic)) {
                                fail(value.location,
                                     "call variadic-prefix metadata mismatch");
                            }
                            if (argument.value.has_value() ==
                                argument.cell.has_value()) {
                                fail(value.location,
                                     "call argument must name exactly one "
                                     "value or parameter cell");
                                continue;
                            }
                            if (argument.value) {
                                const auto expected =
                                    unnamed ? argument.type
                                            : callee.parameters[index].type;
                                // A tail call forwards a caller output as
                                // its transport pointer.
                                const bool forwarded =
                                    !unnamed &&
                                    callee.parameters[index].mode !=
                                        ParameterMode::In;
                                const auto* actual =
                                    argument.value->value <
                                            function.values.size()
                                        ? &hir_module.type(
                                              function
                                                  .values[argument.value
                                                              ->value]
                                                  .type)
                                        : nullptr;
                                if (!actual || argument.type != expected ||
                                    (forwarded
                                         ? !value.must_tail ||
                                               actual->kind !=
                                                   hir::Type::Kind::Pointer ||
                                               !actual->pointee ||
                                               !hir::same_callable_type(
                                                   hir_module, *actual->pointee,
                                                   expected)
                                         : function
                                                   .values[argument.value
                                                               ->value]
                                                   .type != expected)) {
                                    fail(value.location,
                                         "call value argument type or mode "
                                         "mismatch");
                                }
                                if (ordinary_index >= value.operands.size() ||
                                    value.operands[ordinary_index] !=
                                        *argument.value) {
                                    fail(value.location,
                                         "call ordinary operand list disagrees "
                                         "with argument cells");
                                }
                                ++ordinary_index;
                            } else if (unnamed ||
                                       (callee.parameters[index].mode ==
                                            ParameterMode::In &&
                                        (!callee.parameters[index]
                                              .physical_location ||
                                         *callee.parameters[index]
                                                 .physical_location ==
                                             "auto")) ||
                                       argument.cell->value >=
                                           function.slots.size() ||
                                       function.slots[argument.cell->value]
                                               .type !=
                                           callee.parameters[index].type ||
                                       argument.type !=
                                           callee.parameters[index].type) {
                                fail(value.location,
                                     "call argument type mismatch");
                            }
                        }
                        if (ordinary_index != value.operands.size()) {
                            fail(value.location,
                                 "call has unassociated ordinary operands");
                        }
                    }
                }
                if (value.patch_sink || value.object ||
                    value.is_volatile_access) {
                    fail(value.location,
                         "call carries unrelated operation metadata");
                }
            } else if (value.kind == ValueKind::PatchValue) {
                const bool valid_type =
                    value.type.value < hir_module.types.size();
                if (value.slot || value.callee || value.object ||
                    value.is_volatile_access || !valid_type ||
                    (valid_type && (!integer_type(hir_module, value.type) ||
                                    type_bits(hir_module, value.type) > 64))) {
                    fail(value.location, "invalid patch-value operation");
                }
                const auto bits =
                    valid_type ? type_bits(hir_module, value.type) : 0;
                if (bits == 0 ||
                    (!value.patch_initial_address &&
                     !fits_unsigned({value.integer, value.integer_high},
                                    bits))) {
                    fail(value.location,
                         "patch initial bits do not fit its type");
                }
                if (value.patch_initial_address) {
                    const auto& address = *value.patch_initial_address;
                    const bool identity_valid =
                        (address.kind == data::AddressKind::Object &&
                         address.object &&
                         address.object->value < hir_module.objects.size()) ||
                        (address.kind == data::AddressKind::Function &&
                         address.function &&
                         address.function->value < hir_module.functions.size()) ||
                        (address.kind == data::AddressKind::Label &&
                         address.label &&
                         address.label->value < hir_module.labels.size());
                    if (!identity_valid || bits != hir_module.address_bits ||
                        value.integer != 0 || value.integer_high != 0) {
                        fail(value.location,
                             "invalid relocatable patch initial");
                    }
                }
                const auto [patch, first_use] = patch_ids.emplace(value.patch_id, &value);
                if (!first_use && (patch->second->type != value.type ||
                    patch->second->integer != value.integer ||
                    patch->second->integer_high != value.integer_high ||
                    patch->second->patch_initial_address != value.patch_initial_address ||
                    patch->second->patch_sink != value.patch_sink)) {
                    fail(value.location, "inconsistent uses of a shared patch-value site");
                }
                if (value.patch_sink &&
                    value.patch_sink->object.value >=
                        hir_module.objects.size()) {
                    fail(value.location, "patch-value sink is out of range");
                } else if (value.patch_sink) {
                    const auto storage_bytes = patch_address_storage_bytes(
                        value.patch_sink->representation, hir_module.address_bits);
                    if (!storage_bytes || value.patch_sink->storage_bytes != storage_bytes) {
                        fail(value.location, "invalid patch-value sink representation");
                    }
                    const auto& sink = hir_module.object(
                        value.patch_sink->object);
                    if (!sink.definition) {
                        fail(value.location,
                             "patch-value sink object is not a definition");
                    }
                }
            } else if (value.slot || value.callee || value.object ||
                       value.patch_sink || value.call_signature ||
                       value.is_volatile_access ||
                       !value.call_arguments.empty()) {
                fail(value.location, "pure value carries operation metadata");
            }
        }
        const auto& terminator = block.terminator;
        if (terminator.value) {
            if (terminator.value->value >= function.values.size()) {
                fail(terminator.location, "terminator value is out of range");
            } else {
                const auto defining = *definition_block[terminator.value->value];
                if (defining != block.id &&
                    !dominates(block.id, defining)) {
                    fail(terminator.location,
                         "terminator value definition does not dominate its use");
                }
            }
        }
        if (terminator.kind == TerminatorKind::Return) {
            if (!terminator.successors.empty()) {
                fail(terminator.location, "return has successors");
            } else if (void_type(hir_module, function.result_type) != !terminator.value) {
                fail(terminator.location, "return payload does not match function result");
            } else if (terminator.value && terminator.value->value < function.values.size() &&
                       function.values[terminator.value->value].type != function.result_type) {
                fail(terminator.location, "return value type mismatch");
            }
        } else if (terminator.kind == TerminatorKind::ConditionalBranch) {
            if (!terminator.value || terminator.successors.size() != 2 ||
                terminator.value->value >= function.values.size() ||
                function.values[terminator.value->value].type !=
                    hir_module.builtin(BuiltinType::Bool)) {
                fail(terminator.location, "invalid conditional branch");
            }
        } else if (terminator.kind == TerminatorKind::Branch) {
            if (terminator.value || terminator.successors.size() != 1) {
                fail(terminator.location, "invalid branch");
            }
        } else if (terminator.kind == TerminatorKind::IndirectBranch) {
            std::vector<BlockId> label_targets;
            label_targets.reserve(function.labels.size());
            for (const auto& label : function.labels) {
                label_targets.push_back(label.block);
            }
            if (!terminator.value ||
                terminator.value->value >= function.values.size() ||
                !label_type(
                    hir_module,
                    function.values[terminator.value->value].type) ||
                terminator.successors != label_targets) {
                fail(terminator.location, "invalid indirect branch");
            }
        } else if ((terminator.kind == TerminatorKind::Unreachable ||
                    terminator.kind == TerminatorKind::Trap) &&
                   (terminator.value || !terminator.successors.empty())) {
            fail(terminator.location,
                 "invalid unreachable or trap terminator");
        }
    }
    return valid;
}

} // namespace

const ManagedFunction* ManagedModule::find(hir::FunctionId id) const {
    for (const auto& function : functions) if (function.source == id) return &function;
    return nullptr;
}

ManagedModule lower_managed(hir::Module& hir_module,
                            const Subtarget& subtarget,
                            const CompilerOptions& options,
                            Diagnostics& diagnostics) {
    auto module = ManagedLowerer(
        hir_module, subtarget, options, diagnostics).run();
    (void)verify(module, hir_module, diagnostics);
    for (const auto& function : module.functions)
        check_out_definite_assignment(function, hir_module,
                                      subtarget.target(), diagnostics);
    return module;
}

bool verify(const ManagedModule& module, const hir::Module& hir_module,
            Diagnostics& diagnostics) {
    bool valid = true;
    std::unordered_set<std::uint32_t> seen;
    std::map<std::pair<std::uint32_t, std::uint64_t>, std::uint32_t> seen_patch_sinks;
    std::unordered_map<std::uint32_t, hir::FunctionId> patch_owners;
    std::unordered_set<std::uint32_t> seen_patch_sink_objects;
    for (const auto& function : module.functions) {
        if (function.source.value >= hir_module.functions.size()) {
            diagnostics.error(function.location,
                              "invalid managed MIR: source function is out of range");
            valid = false;
            continue;
        }
        if (!seen.insert(function.source.value).second) {
            diagnostics.error(function.location,
                              "invalid managed MIR: duplicate function definition");
            valid = false;
        }
        if (!module.definitions.contains(function.source.value)) {
            diagnostics.error(function.location,
                              "invalid managed MIR: definition index omits function");
            valid = false;
        }
        if (hir_module.function(function.source).ownership !=
            hir::BodyOwnership::ManagedMir) {
            diagnostics.error(function.location,
                              "invalid managed MIR: HIR body owner disagrees");
            valid = false;
        }
        valid = verify_function(function, hir_module, diagnostics) && valid;
        for (const auto& value : function.values) {
            if (value.kind != ValueKind::PatchValue) continue;
            const auto [owner, first_use] = patch_owners.emplace(value.patch_id, function.source);
            if (!first_use && owner->second != function.source) {
                diagnostics.error(value.location,
                    "invalid managed MIR: patch identity has multiple function owners");
                valid = false;
            }
            if (!value.patch_sink) continue;
            const auto sink = std::pair{
                value.patch_sink->object.value, value.patch_sink->offset};
            seen_patch_sink_objects.insert(sink.first);
            const auto [site, first_sink] = seen_patch_sinks.emplace(sink, value.patch_id);
            if (!first_sink && site->second != value.patch_id) {
                diagnostics.error(
                    value.location,
                    "invalid managed MIR: patch sink is used by multiple sites");
                valid = false;
            }
            if (!module.object_definitions.contains(sink.first)) {
                diagnostics.error(
                    value.location,
                    "invalid managed MIR: patch sink definition is not owned");
                valid = false;
            }
        }
    }
    for (const auto source : module.definitions) {
        if (source >= hir_module.functions.size()) {
            diagnostics.error({}, "invalid managed MIR: definition index is out of range");
            valid = false;
        } else if (!seen.contains(source)) {
            diagnostics.error(hir_module.functions[source].location,
                              "invalid managed MIR: indexed definition is missing");
            valid = false;
        }
    }
    for (const auto& function : hir_module.functions) {
        if (function.ownership == hir::BodyOwnership::ManagedMir &&
            !module.definitions.contains(function.id.value)) {
            diagnostics.error(function.location,
                              "invalid managed MIR: owned HIR function is missing");
            valid = false;
        }
    }
    for (const auto source : module.object_definitions) {
        if (source >= hir_module.objects.size()) {
            diagnostics.error(
                {}, "invalid managed MIR: patch sink definition is out of range");
            valid = false;
        } else if (!seen_patch_sink_objects.contains(source)) {
            diagnostics.error(
                hir_module.objects[source].location,
                "invalid managed MIR: owned patch sink has no patch-value site");
            valid = false;
        }
    }
    return valid;
}

namespace {

bool is_effectful_value(const ManagedValue& value) {
    return value.effect_input.has_value() || value.effect_output.has_value();
}

bool has_function_attribute(const hir::Function& function,
                            std::string_view name) {
    return function.definition && function.definition->attribute(name);
}

std::size_t inline_cost(const ManagedFunction& function) {
    return static_cast<std::size_t>(std::count_if(
        function.values.begin(), function.values.end(),
        [](const ManagedValue& value) {
            return value.kind != ValueKind::Parameter &&
                   value.kind != ValueKind::VoidValue &&
                   value.kind != ValueKind::LifetimeStart &&
                   value.kind != ValueKind::LifetimeEnd;
        }));
}

bool structurally_inlineable(const ManagedFunction& function,
                             const hir::Function& entity) {
    if (entity.variadic || function.blocks.size() != 1 ||
        function.entry.value != 0 ||
        !function.labels.empty() ||
        function.blocks.front().terminator.kind != TerminatorKind::Return) {
        return false;
    }
    for (const auto& parameter : entity.parameters) {
        if (parameter.mode != ParameterMode::In ||
            (parameter.physical_location &&
             *parameter.physical_location != "auto")) {
            return false;
        }
    }
    if (std::any_of(function.values.begin(), function.values.end(),
                    [](const ManagedValue& value) {
                        return value.kind == ValueKind::DynamicStackSave ||
                               value.kind == ValueKind::DynamicAlloca ||
                               value.kind == ValueKind::DynamicStackRestore;
                    })) {
        return false;
    }
    for (const auto& slot : function.slots) {
        if (slot.physical_location) return false;
    }
    for (const auto& value : function.values) {
        if (value.kind == ValueKind::Phi ||
            value.kind == ValueKind::LabelAddress ||
            value.kind == ValueKind::VariadicState ||
            value.kind == ValueKind::PatchValue) {
            return false;
        }
    }
    return true;
}

void replace_effect_inputs(ManagedFunction& function, EffectId from,
                           EffectId to) {
    for (auto& value : function.values) {
        if (value.effect_input == from) value.effect_input = to;
    }
    for (auto& effect : function.effects) {
        if (effect.input == from) effect.input = to;
        for (auto& incoming : effect.incoming) {
            if (incoming.effect == from) incoming.effect = to;
        }
    }
    for (auto& block : function.blocks) {
        if (block.terminator.effect == from) block.terminator.effect = to;
    }
}

// Rewrites every reference to a recorded effect to the end of its chain.
void forward_effects(ManagedFunction& function,
                     std::unordered_map<std::uint32_t, EffectId>& forwarded) {
    const auto resolve = [&](EffectId effect) {
        auto target = effect;
        for (auto found = forwarded.find(target.value); found != forwarded.end();
             found = forwarded.find(target.value))
            target = found->second;
        for (auto found = forwarded.find(effect.value); found != forwarded.end();
             found = forwarded.find(effect.value)) {
            effect = found->second;
            found->second = target;
        }
        return target;
    };
    for (auto& value : function.values)
        if (value.effect_input) value.effect_input = resolve(*value.effect_input);
    for (auto& effect : function.effects) {
        if (effect.input) effect.input = resolve(*effect.input);
        for (auto& incoming : effect.incoming) incoming.effect = resolve(incoming.effect);
    }
    for (auto& block : function.blocks)
        block.terminator.effect = resolve(block.terminator.effect);
}

void replace_value_uses(ManagedFunction& function, ValueId from, ValueId to) {
    for (auto& value : function.values) {
        for (auto& operand : value.operands) {
            if (operand == from) operand = to;
        }
        for (auto& argument : value.call_arguments) {
            if (argument.value == from) argument.value = to;
        }
        for (auto& incoming : value.incoming) {
            if (incoming.value == from) incoming.value = to;
        }
    }
    for (auto& block : function.blocks) {
        if (block.terminator.value == from) block.terminator.value = to;
    }
}

bool inline_call(ManagedFunction& caller, std::size_t block_index,
                 std::size_t position, const ManagedFunction& callee) {
    const auto call_id = caller.blocks[block_index].values[position];
    const auto call = caller.values[call_id.value];
    if (!call.effect_input || !call.effect_output ||
        call.call_arguments.size() != callee.parameters.size()) {
        return false;
    }
    for (const auto& argument : call.call_arguments) {
        if (!argument.value || argument.cell) return false;
    }

    std::vector<std::optional<ValueId>> value_map(callee.values.size());
    for (const auto parameter_id : callee.parameters) {
        if (parameter_id.value >= callee.values.size()) return false;
        const auto parameter_index =
            callee.values[parameter_id.value].parameter_index;
        if (parameter_index >= call.call_arguments.size() ||
            !call.call_arguments[parameter_index].value) {
            return false;
        }
        value_map[parameter_id.value] =
            *call.call_arguments[parameter_index].value;
    }

    std::vector<SlotId> slot_map;
    slot_map.reserve(callee.slots.size());
    for (std::size_t index = 0; index < callee.slots.size(); ++index) {
        slot_map.push_back(
            {static_cast<std::uint32_t>(caller.slots.size() + index)});
    }

    std::vector<ManagedValue> clones;
    std::vector<bool> clone_effectful;
    std::vector<ValueId> inserted;
    clones.reserve(callee.values.size());
    clone_effectful.reserve(callee.values.size());
    inserted.reserve(callee.values.size());
    const auto value_base = caller.values.size();
    for (const auto source_id : callee.blocks.front().values) {
        const auto& source = callee.values[source_id.value];
        if (source.kind == ValueKind::Parameter) continue;

        ManagedValue clone = source;
        clone.id = {
            static_cast<std::uint32_t>(value_base + clones.size())};
        clone.effect_input.reset();
        clone.effect_output.reset();
        const auto remap_value = [&](ValueId& value) {
            if (value.value >= value_map.size() || !value_map[value.value]) {
                return false;
            }
            value = *value_map[value.value];
            return true;
        };
        for (auto& operand : clone.operands) {
            if (!remap_value(operand)) return false;
        }
        for (auto& argument : clone.call_arguments) {
            if (argument.value && !remap_value(*argument.value)) return false;
            if (argument.cell) {
                if (argument.cell->value >= slot_map.size()) return false;
                argument.cell = slot_map[argument.cell->value];
            }
        }
        for (auto& incoming : clone.incoming) {
            if (!remap_value(incoming.value)) return false;
        }
        if (clone.slot) {
            if (clone.slot->value >= slot_map.size()) return false;
            clone.slot = slot_map[clone.slot->value];
        }
        value_map[source_id.value] = clone.id;
        inserted.push_back(clone.id);
        clone_effectful.push_back(is_effectful_value(source));
        clones.push_back(std::move(clone));
    }

    std::optional<ValueId> returned;
    if (callee.blocks.front().terminator.value) {
        const auto source = *callee.blocks.front().terminator.value;
        if (source.value >= value_map.size() || !value_map[source.value]) {
            return false;
        }
        returned = *value_map[source.value];
    }

    for (std::size_t index = 0; index < callee.slots.size(); ++index) {
        auto slot = callee.slots[index];
        slot.id = slot_map[index];
        slot.name = "$inline." + std::to_string(callee.source.value) + "." +
                    std::to_string(call_id.value) + "." + slot.name;
        caller.slots.push_back(std::move(slot));
    }
    for (auto& clone : clones) caller.values.push_back(std::move(clone));

    auto current_effect = *call.effect_input;
    for (std::size_t index = 0; index < inserted.size(); ++index) {
        if (!clone_effectful[index]) continue;
        const EffectId output{
            static_cast<std::uint32_t>(caller.effects.size())};
        ManagedEffect effect;
        effect.id = output;
        effect.location = caller.values[inserted[index].value].location;
        effect.kind = EffectKind::Operation;
        effect.input = current_effect;
        effect.operation = inserted[index];
        caller.effects.push_back(std::move(effect));
        auto& operation = caller.values[inserted[index].value];
        operation.effect_input = current_effect;
        operation.effect_output = output;
        current_effect = output;
    }
    replace_effect_inputs(
        caller, *call.effect_output, current_effect);

    auto& block_values = caller.blocks[block_index].values;
    block_values.erase(block_values.begin() +
                       static_cast<std::ptrdiff_t>(position));
    block_values.insert(
        block_values.begin() + static_cast<std::ptrdiff_t>(position),
        inserted.begin(), inserted.end());
    if (returned) replace_value_uses(caller, call_id, *returned);
    return true;
}

void compact_effects(ManagedFunction& function) {
    std::vector<bool> live_values(function.values.size());
    for (const auto& block : function.blocks) {
        for (const auto value : block.values) {
            if (value.value < live_values.size()) live_values[value.value] = true;
        }
    }

    std::vector<bool> used(function.effects.size());
    std::vector<EffectId> pending;
    const auto mark = [&](EffectId effect) {
        if (effect.value < used.size() && !used[effect.value]) {
            used[effect.value] = true;
            pending.push_back(effect);
        }
    };
    for (const auto& block : function.blocks) {
        mark(block.effect);
        mark(block.terminator.effect);
    }
    for (std::size_t index = 0; index < function.values.size(); ++index) {
        if (!live_values[index]) continue;
        const auto& value = function.values[index];
        if (value.effect_input) mark(*value.effect_input);
        if (value.effect_output) mark(*value.effect_output);
    }
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& effect = function.effects[id.value];
        if (effect.input) mark(*effect.input);
        for (const auto& incoming : effect.incoming) mark(incoming.effect);
    }

    std::vector<std::optional<EffectId>> remap(function.effects.size());
    std::vector<ManagedEffect> effects;
    effects.reserve(function.effects.size());
    for (std::size_t index = 0; index < function.effects.size(); ++index) {
        if (!used[index]) continue;
        const EffectId id{static_cast<std::uint32_t>(effects.size())};
        remap[index] = id;
        auto effect = function.effects[index];
        effect.id = id;
        effects.push_back(std::move(effect));
    }
    const auto map = [&](EffectId id) {
        return *remap[id.value];
    };
    for (auto& effect : effects) {
        if (effect.input) effect.input = map(*effect.input);
        for (auto& incoming : effect.incoming) {
            incoming.effect = map(incoming.effect);
        }
    }
    for (auto& block : function.blocks) {
        block.effect = map(block.effect);
        block.terminator.effect = map(block.terminator.effect);
    }
    for (std::size_t index = 0; index < function.values.size(); ++index) {
        if (!live_values[index]) continue;
        auto& value = function.values[index];
        if (value.effect_input) value.effect_input = map(*value.effect_input);
        if (value.effect_output) value.effect_output = map(*value.effect_output);
    }
    function.effects = std::move(effects);
}

void inline_managed_calls(ManagedModule& module,
                          const hir::Module& hir_module,
                          const CompilerOptions& options,
                          Diagnostics& diagnostics) {
    for (auto& caller : module.functions) {
        bool changed = false;
        std::size_t expanded_units = 0;
        const auto ordinary_limit =
            static_cast<std::size_t>(options.inline_unit_limit);
        const std::size_t expansion_multiplier =
            options.optimize_for == OptimizationGoal::Speed
                ? 8U
                : options.optimize_for == OptimizationGoal::Size
                      ? 4U
                      : 2U;
        const auto expansion_limit =
            std::max<std::size_t>(32,
                                  ordinary_limit * expansion_multiplier);

        for (std::size_t block_index = 0;
             block_index < caller.blocks.size(); ++block_index) {
            auto& values = caller.blocks[block_index].values;
            for (std::size_t position = 0; position < values.size();) {
                const auto call_id = values[position];
                const auto& call = caller.values[call_id.value];
                if (call.kind != ValueKind::Call || !call.callee) {
                    ++position;
                    continue;
                }
                if (call.must_tail) {
                    ++position;
                    continue;
                }
                const auto& entity = hir_module.function(*call.callee);
                const auto* callee = module.find(*call.callee);
                // A naked function admits only raw-compatible inlining.
                const bool mandatory =
                    has_function_attribute(entity, "always_inline") ||
                    (hir_module.function(caller.source).naked &&
                     has_function_attribute(entity, "raw_inline"));
                const bool forbidden =
                    has_function_attribute(entity, "noinline");
                const bool requested =
                    options.inline_functions || mandatory;
                if (!callee || callee == &caller || forbidden || !requested ||
                    !structurally_inlineable(*callee, entity)) {
                    ++position;
                    continue;
                }
                const auto cost = inline_cost(*callee);
                const auto hinted_limit =
                    entity.definition && entity.definition->inline_hint
                        ? ordinary_limit * 2
                        : ordinary_limit;
                if (!mandatory &&
                    (cost > hinted_limit ||
                     expanded_units + cost > expansion_limit)) {
                    ++position;
                    continue;
                }
                if (!inline_call(caller, block_index, position, *callee)) {
                    ++position;
                    continue;
                }
                changed = true;
                expanded_units += cost;
                // The inserted body can itself contain profitable calls.
            }
        }
        if (changed) {
            compact_effects(caller);
            compact_managed_values(caller);
        }
    }

    for (const auto& caller : module.functions) {
        const bool naked = hir_module.function(caller.source).naked;
        for (const auto& block : caller.blocks) {
            for (const auto value_id : block.values) {
                const auto& value = caller.values[value_id.value];
                if (naked && value.kind == ValueKind::Call) {
                    const auto* callee = value.callee
                        ? &hir_module.function(*value.callee)
                        : nullptr;
                    diagnostics.error(
                        value.location,
                        callee && has_function_attribute(*callee, "raw_inline")
                            ? "raw_inline call to '" + callee->source_name +
                                  "' could not be inlined into the naked "
                                  "function"
                            : std::string("ordinary calls are not permitted "
                                          "in a naked function"));
                    continue;
                }
                if (value.kind != ValueKind::Call || !value.callee ||
                    !module.owns(*value.callee)) {
                    continue;
                }
                const auto& callee = hir_module.function(*value.callee);
                if (has_function_attribute(callee, "always_inline") &&
                    !has_function_attribute(callee, "noinline")) {
                    diagnostics.error(
                        value.location,
                        "always_inline call to '" + callee.source_name +
                            "' could not be inlined");
                }
            }
        }
    }
}

void eliminate_dead_values(ManagedFunction& function) {
    std::vector<bool> live(function.values.size());
    std::vector<ValueId> pending;
    const auto mark = [&](ValueId value) {
        if (value.value < live.size() && !live[value.value]) {
            live[value.value] = true;
            pending.push_back(value);
        }
    };
    for (const auto parameter : function.parameters) mark(parameter);
    for (const auto& block : function.blocks) {
        if (block.terminator.value) mark(*block.terminator.value);
        for (const auto value_id : block.values) {
            if (is_effectful_value(function.values[value_id.value])) {
                mark(value_id);
            }
        }
    }
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& value = function.values[id.value];
        for (const auto operand : value.operands) mark(operand);
        for (const auto& argument : value.call_arguments) {
            if (argument.value) mark(*argument.value);
        }
        for (const auto& incoming : value.incoming) mark(incoming.value);
    }
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId value) {
            return !live[value.value];
        });
    }
    compact_effects(function);
    compact_managed_values(function);
}

void remove_effectful_values(
    ManagedFunction& function,
    const std::unordered_set<std::uint32_t>& removed) {
    if (removed.empty()) return;
    std::unordered_map<std::uint32_t, EffectId> forwarded;
    for (const auto id : removed) {
        const auto& value = function.values[id];
        if (value.effect_input && value.effect_output && *value.effect_input != *value.effect_output)
            forwarded[value.effect_output->value] = *value.effect_input;
    }
    forward_effects(function, forwarded);
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId value) {
            return removed.contains(value.value);
        });
    }
    compact_effects(function);
    compact_managed_values(function);
}

bool optimizable_slot(const ManagedFunction& function, SlotId slot) {
    const auto& value = function.slots[slot.value];
    return !value.is_volatile && !value.physical_location &&
           !value.address_taken;
}

std::vector<bool> mir_reachable_blocks(const ManagedFunction& function) {
    std::vector<bool> reachable(function.blocks.size());
    std::vector<BlockId> pending{function.entry};
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        if (block.value >= reachable.size() || reachable[block.value]) {
            continue;
        }
        reachable[block.value] = true;
        for (const auto successor :
             function.blocks[block.value].terminator.successors) {
            pending.push_back(successor);
        }
    }
    return reachable;
}

// Scalar slot promotion visits each slot's own loads, stores, and lifetime
// markers rather than the whole function, and defers phi insertion, use
// replacement, and value removal to one pass after every slot is decided.
class ScalarSlotPromotion {
public:
    ScalarSlotPromotion(ManagedFunction& function, const hir::Module& hir_module)
        : function_(function), reachable_(mir_reachable_blocks(function)),
          occurrences_(function.slots.size()), candidates_(function.slots.size()),
          inserted_phis_(function.blocks.size()) {
        for (std::uint32_t index = 0; index < function.slots.size(); ++index) {
            candidates_[index] =
                promotable_scalar_slot(function.slots[index], hir_module) &&
                !function.slots[index].live_on_return;
        }
        for (const auto& value : function.values) {
            if (value.slot && value.kind != ValueKind::Load &&
                value.kind != ValueKind::Store &&
                value.kind != ValueKind::LifetimeStart &&
                value.kind != ValueKind::LifetimeEnd) {
                candidates_[value.slot->value] = false;
            }
            if (value.kind == ValueKind::Call)
                for (const auto& argument : value.call_arguments)
                    if (argument.cell) candidates_[argument.cell->value] = false;
        }
        for (const auto& block : function.blocks) {
            for (const auto id : block.values) {
                const auto& value = function.values[id.value];
                if (!value.slot || !candidates_[value.slot->value]) continue;
                auto& groups = occurrences_[value.slot->value];
                if (groups.empty() || groups.back().first != block.id.value)
                    groups.push_back({block.id.value, {}});
                groups.back().second.push_back(id);
            }
        }
    }

    // Returns the promoted slots.
    std::vector<bool> run() {
        std::vector<bool> promoted(function_.slots.size());
        if (!function_.labels.empty()) return promoted;
        for (std::uint32_t index = 0; index < function_.slots.size(); ++index)
            promoted[index] = candidates_[index] && promote(SlotId{index});
        // Each promotion put its phi first in the block, so the last slot's
        // phi leads.
        for (auto& block : function_.blocks) {
            auto& phis = inserted_phis_[block.id.value];
            if (phis.empty()) continue;
            block.values.insert(block.values.begin(), phis.rbegin(), phis.rend());
        }
        replacements_.apply(function_);
        remove_effectful_values(function_, removed_);
        return promoted;
    }

private:
    using Group = std::pair<std::uint32_t, std::vector<ValueId>>;

    // Definite assignment is a forward intersection problem. Reachable
    // non-entry blocks start at the lattice top so initialized loop-carried
    // cells converge to true instead of being rejected on the first round.
    void definitely_assigned(const std::vector<const Group*>& by_block,
                             std::vector<bool>& assigned_in,
                             std::vector<bool>& assigned_out) const {
        const auto count = function_.blocks.size();
        assigned_in.assign(count, true);
        assigned_out.assign(count, true);
        for (std::size_t index = 0; index < count; ++index) {
            if (!reachable_[index]) {
                assigned_in[index] = false;
                assigned_out[index] = false;
            }
        }
        assigned_in[function_.entry.value] = false;
        const auto transfer = [&](std::uint32_t block, bool assigned) {
            if (!by_block[block]) return assigned;
            for (const auto id : by_block[block]->second) {
                const auto& value = function_.values[id.value];
                if (value.kind == ValueKind::Store) assigned = true;
                else if (value.kind == ValueKind::LifetimeStart ||
                         value.kind == ValueKind::LifetimeEnd) {
                    assigned = false;
                }
            }
            return assigned;
        };
        for (bool changed = true; changed;) {
            changed = false;
            for (const auto& block : function_.blocks) {
                if (!reachable_[block.id.value]) continue;
                bool next_in = false;
                if (block.id != function_.entry && !block.predecessors.empty()) {
                    next_in = std::all_of(
                        block.predecessors.begin(), block.predecessors.end(),
                        [&](BlockId predecessor) {
                            return predecessor.value < reachable_.size() &&
                                   reachable_[predecessor.value] &&
                                   assigned_out[predecessor.value];
                        });
                }
                const auto next_out = transfer(block.id.value, next_in);
                if (assigned_in[block.id.value] != next_in ||
                    assigned_out[block.id.value] != next_out) {
                    assigned_in[block.id.value] = next_in;
                    assigned_out[block.id.value] = next_out;
                    changed = true;
                }
            }
        }
    }

    bool promote(SlotId slot) {
        const auto& groups = occurrences_[slot.value];
        const auto count = function_.blocks.size();
        std::vector<const Group*> by_block(count);
        for (const auto& group : groups) by_block[group.first] = &group;

        std::vector<bool> assigned_in;
        std::vector<bool> assigned_out;
        definitely_assigned(by_block, assigned_in, assigned_out);
        for (const auto& [block, values] : groups) {
            if (!reachable_[block]) return false;
            bool assigned = assigned_in[block];
            for (const auto id : values) {
                const auto& value = function_.values[id.value];
                if (value.kind == ValueKind::Load && !assigned) return false;
                if (value.kind == ValueKind::Store) {
                    if (value.operands.size() != 1) return false;
                    assigned = true;
                } else if (value.kind == ValueKind::LifetimeStart ||
                           value.kind == ValueKind::LifetimeEnd) {
                    assigned = false;
                }
            }
        }

        struct ReachingValue {
            bool resolved{};
            std::optional<ValueId> value;
        };
        std::vector<ReachingValue> entry(count);
        std::vector<ReachingValue> outgoing(count);
        std::vector<std::optional<ValueId>> phis(count);
        std::vector<BlockId> phi_blocks;
        const auto first_phi = function_.values.size();
        for (const auto& block : function_.blocks) {
            if (!reachable_[block.id.value]) continue;
            if (!assigned_in[block.id.value]) {
                entry[block.id.value].resolved = true;
                continue;
            }
            if (block.predecessors.size() <= 1) continue;
            ManagedValue phi;
            phi.id = {static_cast<std::uint32_t>(function_.values.size())};
            phi.location = block.location;
            phi.type = function_.slots[slot.value].type;
            phi.kind = ValueKind::Phi;
            function_.values.push_back(std::move(phi));
            phis[block.id.value] = function_.values.back().id;
            entry[block.id.value] = {true, function_.values.back().id};
            phi_blocks.push_back(block.id);
        }
        const auto discard_phis = [&] { function_.values.resize(first_phi); };

        const auto simulate = [&](std::uint32_t block, std::optional<ValueId> current) {
            if (!by_block[block]) return current;
            for (const auto id : by_block[block]->second) {
                const auto& value = function_.values[id.value];
                if (value.kind == ValueKind::Store) {
                    current = value.operands.front();
                } else if (value.kind == ValueKind::LifetimeStart ||
                           value.kind == ValueKind::LifetimeEnd) {
                    current.reset();
                }
            }
            return current;
        };
        for (bool progress = true; progress;) {
            progress = false;
            for (const auto& block : function_.blocks) {
                if (!reachable_[block.id.value] || outgoing[block.id.value].resolved) {
                    continue;
                }
                auto& block_entry = entry[block.id.value];
                if (!block_entry.resolved) {
                    if (block.predecessors.size() != 1) continue;
                    const auto predecessor = block.predecessors.front();
                    if (!outgoing[predecessor.value].resolved) continue;
                    block_entry = outgoing[predecessor.value];
                }
                outgoing[block.id.value] = {true, simulate(block.id.value, block_entry.value)};
                progress = true;
            }
        }
        for (std::size_t index = 0; index < count; ++index) {
            if (reachable_[index] && !outgoing[index].resolved) {
                discard_phis();
                return false;
            }
        }
        for (const auto block : phi_blocks) {
            auto& phi = function_.values[phis[block.value]->value];
            for (const auto predecessor : function_.blocks[block.value].predecessors) {
                if (!outgoing[predecessor.value].value) {
                    // Definite assignment guaranteed an incoming value here.
                    discard_phis();
                    return false;
                }
                phi.incoming.push_back({predecessor, *outgoing[predecessor.value].value});
            }
        }
        for (const auto block : phi_blocks)
            inserted_phis_[block.value].push_back(*phis[block.value]);

        std::vector<std::uint32_t> removed;
        for (const auto& [block, values] : groups) {
            auto current = entry[block].value;
            for (const auto id : values) {
                const auto& value = function_.values[id.value];
                if (value.kind == ValueKind::Load) {
                    if (!current) return false;
                    replacements_.add(id, *current);
                } else if (value.kind == ValueKind::Store) {
                    current = replacements_.resolve(value.operands.front());
                } else {
                    current.reset();
                }
                removed.push_back(id.value);
            }
        }
        removed_.insert(removed.begin(), removed.end());
        return true;
    }

    ManagedFunction& function_;
    std::vector<bool> reachable_;
    std::vector<std::vector<Group>> occurrences_;
    std::vector<bool> candidates_;
    std::vector<std::vector<ValueId>> inserted_phis_;
    ValueReplacements replacements_;
    std::unordered_set<std::uint32_t> removed_;
};

void compact_promoted_slots(ManagedFunction& function,
                            const std::vector<bool>& promoted) {
    std::vector<bool> referenced(function.slots.size());
    for (const auto& value : function.values) {
        if (value.slot) referenced[value.slot->value] = true;
        for (const auto& argument : value.call_arguments) {
            if (argument.cell) referenced[argument.cell->value] = true;
        }
    }
    std::vector<std::optional<SlotId>> remap(function.slots.size());
    std::vector<ManagedSlot> slots;
    slots.reserve(function.slots.size());
    for (std::size_t index = 0; index < function.slots.size(); ++index) {
        if (index < promoted.size() && promoted[index] &&
            !referenced[index]) {
            continue;
        }
        const SlotId replacement{
            static_cast<std::uint32_t>(slots.size())};
        remap[index] = replacement;
        auto value = std::move(function.slots[index]);
        value.id = replacement;
        slots.push_back(std::move(value));
    }
    for (auto& value : function.values) {
        if (value.slot) value.slot = *remap[value.slot->value];
        for (auto& argument : value.call_arguments) {
            if (argument.cell) {
                argument.cell = *remap[argument.cell->value];
            }
        }
    }
    function.slots = std::move(slots);
}

void promote_scalar_slots(ManagedFunction& function,
                          const hir::Module& hir_module) {
    const auto promoted = ScalarSlotPromotion(function, hir_module).run();
    if (std::any_of(promoted.begin(), promoted.end(),
                    [](bool value) { return value; })) {
        compact_promoted_slots(function, promoted);
    }
}

std::vector<bool> transfer_slot_liveness(
    const ManagedFunction& function, const ManagedBlock& block,
    std::vector<bool> needed,
    std::unordered_set<std::uint32_t>* removed = nullptr) {
    for (auto item = block.values.rbegin();
         item != block.values.rend(); ++item) {
        const auto& value = function.values[item->value];
        if ((value.kind == ValueKind::Load ||
             value.kind == ValueKind::Store ||
             value.kind == ValueKind::LifetimeStart ||
             value.kind == ValueKind::LifetimeEnd) &&
            value.slot && optimizable_slot(function, *value.slot)) {
            const auto slot = value.slot->value;
            if (value.kind == ValueKind::Load) {
                needed[slot] = true;
            } else if (value.kind == ValueKind::Store) {
                if (removed && !needed[slot]) {
                    removed->insert(value.id.value);
                }
                // This definition supplies all later reads, so the value held
                // before it is not live even when the store itself is live.
                needed[slot] = false;
            } else if (value.kind == ValueKind::LifetimeEnd &&
                       function.slots[slot].live_on_return) {
                // ABI copy-out consumes the final parameter-cell value at the
                // return boundary, after source lifetime markers.
                needed[slot] = true;
            } else {
                // Neither the previous nor the next source lifetime may use
                // the storage value across a lifetime boundary.
                needed[slot] = false;
            }
            continue;
        }
        if (value.kind != ValueKind::Call) continue;
        // Cell arguments are the only way an ordinary managed call can access
        // a caller slot. Conservatively require the incoming value until
        // parameter-specific mod/ref summaries are available.
        for (const auto& argument : value.call_arguments) {
            if (argument.cell &&
                optimizable_slot(function, *argument.cell)) {
                needed[argument.cell->value] = true;
            }
        }
    }
    return needed;
}

void eliminate_dead_stores(ManagedFunction& function) {
    const auto slots = function.slots.size();
    std::vector<std::vector<bool>> live_in(
        function.blocks.size(), std::vector<bool>(slots));
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto block = function.blocks.rbegin();
             block != function.blocks.rend(); ++block) {
            std::vector<bool> live_out(slots);
            if (block->terminator.kind == TerminatorKind::Return) {
                for (std::size_t slot = 0; slot < slots; ++slot) {
                    live_out[slot] = function.slots[slot].live_on_return;
                }
            }
            for (const auto successor : block->terminator.successors) {
                for (std::size_t slot = 0; slot < slots; ++slot) {
                    live_out[slot] = live_out[slot] ||
                                     live_in[successor.value][slot];
                }
            }
            auto next = transfer_slot_liveness(
                function, *block, std::move(live_out));
            if (next != live_in[block->id.value]) {
                live_in[block->id.value] = std::move(next);
                changed = true;
            }
        }
    }

    std::unordered_set<std::uint32_t> removed;
    for (const auto& block : function.blocks) {
        std::vector<bool> live_out(slots);
        if (block.terminator.kind == TerminatorKind::Return) {
            for (std::size_t slot = 0; slot < slots; ++slot) {
                live_out[slot] = function.slots[slot].live_on_return;
            }
        }
        for (const auto successor : block.terminator.successors) {
            for (std::size_t slot = 0; slot < slots; ++slot) {
                live_out[slot] = live_out[slot] ||
                                 live_in[successor.value][slot];
            }
        }
        (void)transfer_slot_liveness(
            function, block, std::move(live_out), &removed);
    }
    remove_effectful_values(function, removed);
}

bool acyclic_control_flow(const ManagedFunction& function) {
    std::vector<unsigned char> state(function.blocks.size());
    const auto visit = [&](const auto& self, BlockId id) -> bool {
        if (state[id.value] == 1) return false;
        if (state[id.value] == 2) return true;
        state[id.value] = 1;
        for (const auto successor :
             function.blocks[id.value].terminator.successors) {
            if (!self(self, successor)) return false;
        }
        state[id.value] = 2;
        return true;
    };
    return visit(visit, function.entry);
}

bool locally_removable_function(const ManagedFunction& function,
                                const hir::Module& hir_module) {
    const auto& entity = hir_module.function(function.source);
    if (!function.labels.empty() || !acyclic_control_flow(function) ||
        std::any_of(entity.parameters.begin(), entity.parameters.end(),
                    [](const hir::Parameter& parameter) {
                        return parameter.mode != ParameterMode::In ||
                               parameter.physical_location.has_value();
                    })) {
        return false;
    }
    for (const auto& block : function.blocks) {
        if (block.terminator.kind == TerminatorKind::IndirectBranch ||
            block.terminator.kind == TerminatorKind::Unreachable ||
            block.terminator.kind == TerminatorKind::Trap) {
            return false;
        }
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            if (value.is_volatile_access ||
                value.kind == ValueKind::PointerStore ||
                value.kind == ValueKind::GlobalStore ||
                value.kind == ValueKind::Atomic ||
                value.kind == ValueKind::PatchValue) {
                return false;
            }
            if ((value.kind == ValueKind::Load ||
                 value.kind == ValueKind::Store) &&
                value.slot &&
                !optimizable_slot(function, *value.slot)) {
                return false;
            }
            if (value.kind == ValueKind::Call &&
                std::any_of(value.call_arguments.begin(),
                            value.call_arguments.end(),
                            [](const CallArgument& argument) {
                                return argument.cell.has_value();
                            })) {
                return false;
            }
        }
    }
    return true;
}

std::unordered_set<std::uint32_t> infer_removable_functions(
    const ManagedModule& module, const hir::Module& hir_module) {
    std::unordered_set<std::uint32_t> removable;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& function : module.functions) {
            if (removable.contains(function.source.value) ||
                !locally_removable_function(function, hir_module)) {
                continue;
            }
            bool calls_are_removable = true;
            for (const auto& value : function.values) {
                if (value.kind != ValueKind::Call) continue;
                if (!value.callee ||
                    !removable.contains(value.callee->value)) {
                    calls_are_removable = false;
                    break;
                }
            }
            if (calls_are_removable) {
                removable.insert(function.source.value);
                changed = true;
            }
        }
    }
    return removable;
}

void eliminate_dead_removable_calls(
    ManagedFunction& function,
    const std::unordered_set<std::uint32_t>& removable) {
    std::vector<bool> live(function.values.size());
    std::vector<ValueId> pending;
    const auto mark = [&](ValueId value) {
        if (value.value < live.size() && !live[value.value]) {
            live[value.value] = true;
            pending.push_back(value);
        }
    };
    for (const auto parameter : function.parameters) mark(parameter);
    for (const auto& block : function.blocks) {
        if (block.terminator.value) mark(*block.terminator.value);
        for (const auto id : block.values) {
            const auto& value = function.values[id.value];
            const bool removable_call =
                value.kind == ValueKind::Call && value.callee &&
                removable.contains(value.callee->value);
            if (is_effectful_value(value) && !removable_call) mark(id);
        }
    }
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto& value = function.values[id.value];
        for (const auto operand : value.operands) mark(operand);
        for (const auto& argument : value.call_arguments) {
            if (argument.value) mark(*argument.value);
        }
        for (const auto& incoming : value.incoming) mark(incoming.value);
    }

    std::unordered_set<std::uint32_t> removed;
    for (const auto& value : function.values) {
        if (!live[value.id.value] && value.kind == ValueKind::Call &&
            value.callee && removable.contains(value.callee->value)) {
            removed.insert(value.id.value);
        }
    }
    remove_effectful_values(function, removed);
}

void remove_replaced_values(
    ManagedFunction& function,
    const std::unordered_set<std::uint32_t>& removed) {
    if (removed.empty()) return;
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId value) {
            return removed.contains(value.value);
        });
    }
    compact_managed_values(function);
}

void propagate_phi_scc_copies(ManagedFunction& function) {
    const auto count = function.values.size();
    std::vector<int> index(count, -1);
    std::vector<int> low_link(count, -1);
    std::vector<bool> on_stack(count);
    std::vector<ValueId> stack;
    std::vector<std::vector<ValueId>> components;
    int next_index{};
    std::function<void(ValueId)> visit = [&](ValueId id) {
        index[id.value] = next_index;
        low_link[id.value] = next_index++;
        stack.push_back(id);
        on_stack[id.value] = true;
        for (const auto& incoming : function.values[id.value].incoming) {
            const auto source = incoming.value;
            if (source.value >= count ||
                function.values[source.value].kind != ValueKind::Phi) {
                continue;
            }
            if (index[source.value] == -1) {
                visit(source);
                low_link[id.value] = std::min(low_link[id.value],
                                              low_link[source.value]);
            } else if (on_stack[source.value]) {
                low_link[id.value] = std::min(low_link[id.value],
                                              index[source.value]);
            }
        }
        if (low_link[id.value] != index[id.value]) return;
        std::vector<ValueId> component;
        while (!stack.empty()) {
            const auto member = stack.back();
            stack.pop_back();
            on_stack[member.value] = false;
            component.push_back(member);
            if (member == id) break;
        }
        components.push_back(std::move(component));
    };
    for (const auto& value : function.values) {
        if (value.kind == ValueKind::Phi && index[value.id.value] == -1) {
            visit(value.id);
        }
    }

    std::unordered_set<std::uint32_t> removed;
    for (const auto& component : components) {
        std::unordered_set<std::uint32_t> members;
        for (const auto id : component) members.insert(id.value);
        std::optional<ValueId> replacement;
        bool compatible = true;
        for (const auto id : component) {
            const auto& phi = function.values[id.value];
            for (const auto& incoming : phi.incoming) {
                if (members.contains(incoming.value.value)) continue;
                if (!replacement) replacement = incoming.value;
                else if (*replacement != incoming.value) {
                    compatible = false;
                    break;
                }
            }
            if (!compatible) break;
        }
        if (!compatible || !replacement ||
            function.values[replacement->value].type !=
                function.values[component.front().value].type) {
            continue;
        }
        for (const auto id : component) {
            replace_value_uses(function, id, *replacement);
            removed.insert(id.value);
        }
    }
    remove_replaced_values(function, removed);
}

void propagate_trivial_copies(ManagedFunction& function) {
    // Nested control flow often creates a cycle of pass-through phis around
    // one invariant value. No member is locally trivial, but the SCC as a
    // whole is when it has exactly one incoming value from outside. Collapse
    // those cycles before the ordinary one-phi copy rules.
    propagate_phi_scc_copies(function);
    std::unordered_set<std::uint32_t> removed;
    for (auto& value : function.values) {
        std::optional<ValueId> replacement;
        if (value.kind == ValueKind::Phi && !value.incoming.empty()) {
            std::optional<ValueId> candidate;
            bool compatible = true;
            for (const auto& incoming : value.incoming) {
                // A recurrence with no update (`x = phi(initial, x)`) is a
                // forwarding phi. Ignoring the self edge exposes `initial`
                // to nested-loop and induction optimizations.
                if (incoming.value == value.id) continue;
                if (!candidate) candidate = incoming.value;
                else if (*candidate != incoming.value) {
                    compatible = false;
                    break;
                }
            }
            if (compatible && candidate) {
                replacement = *candidate;
            }
        } else if (value.kind == ValueKind::Cast &&
                   value.cast == CastOperation::Reinterpret &&
                   value.operands.size() == 1 &&
                   function.values[value.operands.front().value].type ==
                       value.type) {
            replacement = value.operands.front();
        } else if (value.kind == ValueKind::Select &&
                   value.operands.size() == 3 &&
                   value.operands[1] == value.operands[2]) {
            replacement = value.operands[1];
        }
        if (!replacement) continue;
        replace_value_uses(function, value.id, *replacement);
        removed.insert(value.id.value);
    }
    remove_replaced_values(function, removed);
}

void factor_common_select_addends(ManagedFunction& function) {
    // Canonicalize
    //   select(c, common + a, common + b)
    // into
    //   common + select(c, a, b).
    // Besides removing duplicated work, this exposes the ordinary additive
    // reduction shape to the loop vectorizer. Integer addition is modular;
    // floating selection still performs exactly one addition on the chosen
    // arm, so the rewrite does not reassociate arithmetic.
    for (auto& block : function.blocks) {
        for (std::size_t position = 0; position < block.values.size();
             ++position) {
            const auto selected_id = block.values[position];
            const auto selected = function.values[selected_id.value];
            if (selected.kind != ValueKind::Select ||
                selected.operands.size() != 3) {
                continue;
            }
            const auto truth_id = selected.operands[1];
            const auto falsity_id = selected.operands[2];
            const auto truth = function.values[truth_id.value];
            const auto falsity = function.values[falsity_id.value];
            if (truth.kind != ValueKind::Binary ||
                falsity.kind != ValueKind::Binary ||
                truth.binary != BinaryOperation::Add ||
                falsity.binary != BinaryOperation::Add ||
                truth.operands.size() != 2 ||
                falsity.operands.size() != 2 ||
                truth.type != selected.type ||
                falsity.type != selected.type) {
                continue;
            }
            std::optional<ValueId> common;
            ValueId truth_term{};
            ValueId falsity_term{};
            for (unsigned truth_index = 0; truth_index < 2 && !common;
                 ++truth_index) {
                for (unsigned falsity_index = 0;
                     falsity_index < 2 && !common; ++falsity_index) {
                    if (truth.operands[truth_index] !=
                        falsity.operands[falsity_index]) {
                        continue;
                    }
                    common = truth.operands[truth_index];
                    truth_term = truth.operands[1U - truth_index];
                    falsity_term = falsity.operands[1U - falsity_index];
                }
            }
            if (!common) continue;

            ManagedValue terms;
            terms.id = {static_cast<std::uint32_t>(function.values.size())};
            terms.location = selected.location;
            terms.type = selected.type;
            terms.kind = ValueKind::Select;
            terms.operands = {
                selected.operands[0], truth_term, falsity_term};
            const auto terms_id = terms.id;
            function.values.push_back(std::move(terms));

            auto& replacement = function.values[selected_id.value];
            replacement.kind = ValueKind::Binary;
            replacement.binary = BinaryOperation::Add;
            replacement.operands = {*common, terms_id};
            replacement.incoming.clear();
            block.values.insert(
                block.values.begin() +
                    static_cast<std::ptrdiff_t>(position),
                terms_id);
            ++position;
        }
    }
}

std::optional<std::string> redundant_expression_key(
    const ManagedValue& value) {
    std::string result = std::to_string(static_cast<unsigned>(value.kind)) +
                         ':' + std::to_string(value.type.value);
    switch (value.kind) {
    case ValueKind::ConstantInteger:
    case ValueKind::ConstantFloating:
        result += ':' + std::to_string(value.integer) + ':' +
                  std::to_string(value.integer_high);
        return result;
    case ValueKind::LabelAddress:
        if (!value.label) return std::nullopt;
        result += ':' + std::to_string(value.label->value);
        return result;
    case ValueKind::SlotAddress:
        if (!value.slot) return std::nullopt;
        result += ':' + std::to_string(value.slot->value);
        return result;
    case ValueKind::GlobalAddress:
        if (!value.object) return std::nullopt;
        result += ':' + std::to_string(value.object->value);
        return result;
    case ValueKind::Unary:
        result += ':' + std::to_string(static_cast<unsigned>(value.unary));
        break;
    case ValueKind::Binary:
        result += ':' + std::to_string(static_cast<unsigned>(value.binary));
        break;
    case ValueKind::Cast:
        result += ':' + std::to_string(static_cast<unsigned>(value.cast));
        break;
    case ValueKind::Select:
        break;
    case ValueKind::MachineInstruction:
        if (is_effectful_value(value)) return std::nullopt;
        result += ':' + std::to_string(value.instruction_form.value);
        break;
    default: return std::nullopt;
    }
    for (const auto operand : value.operands) {
        result += ':' + std::to_string(operand.value);
    }
    return result;
}

void eliminate_fully_redundant_expressions(ManagedFunction& function) {
    std::unordered_set<std::uint32_t> removed;
    ValueReplacements replacements;
    for (const auto& block : function.blocks) {
        std::unordered_map<std::string, ValueId> available;
        for (const auto id : block.values) {
            replacements.rewrite(function.values[id.value]);
            const auto key = redundant_expression_key(
                function.values[id.value]);
            if (!key) continue;
            const auto [found, inserted] = available.emplace(*key, id);
            if (inserted) continue;
            replacements.add(id, found->second);
            removed.insert(id.value);
        }
    }
    replacements.apply(function);
    remove_replaced_values(function, removed);
}

bool constant_integer(const ManagedValue& value, UInt128 expected) {
    return value.kind == ValueKind::ConstantInteger &&
           UInt128{value.integer, value.integer_high} == expected;
}

void make_integer_constant(ManagedValue& value, UInt128 integer) {
    value.kind = ValueKind::ConstantInteger;
    value.integer = integer.low;
    value.integer_high = integer.high;
    value.operands.clear();
    value.incoming.clear();
}

void simplify_integer_operations(ManagedFunction& function,
                                 const hir::Module& hir_module) {
    std::unordered_set<std::uint32_t> removed;
    ValueReplacements replacements;
    for (auto& value : function.values) {
        replacements.rewrite(value);
        if (value.kind != ValueKind::Binary || value.operands.size() != 2) {
            continue;
        }
        const auto left_id = value.operands[0];
        const auto right_id = value.operands[1];
        const auto& left = function.values[left_id.value];
        const auto& right = function.values[right_id.value];
        if (floating_type(hir_module, left.type) ||
            floating_type(hir_module, right.type)) {
            continue;
        }
        if (left_id == right_id) {
            std::optional<bool> folded;
            switch (value.binary) {
            case BinaryOperation::Equal:
            case BinaryOperation::SignedLessEqual:
            case BinaryOperation::SignedGreaterEqual:
            case BinaryOperation::UnsignedLessEqual:
            case BinaryOperation::UnsignedGreaterEqual:
                folded = true;
                break;
            case BinaryOperation::NotEqual:
            case BinaryOperation::SignedLess:
            case BinaryOperation::SignedGreater:
            case BinaryOperation::UnsignedLess:
            case BinaryOperation::UnsignedGreater:
                folded = false;
                break;
            default: break;
            }
            if (folded) {
                make_integer_constant(value, *folded ? UInt128{1}
                                                     : UInt128{});
                continue;
            }
        }

        const auto bits = type_bits(hir_module, left.type);
        const auto all_ones = mask_to(bit_not(UInt128{}), bits);
        std::optional<ValueId> replacement;
        switch (value.binary) {
        case BinaryOperation::Add:
            if (constant_integer(left, {})) replacement = right_id;
            else if (constant_integer(right, {})) replacement = left_id;
            break;
        case BinaryOperation::Subtract:
            if (constant_integer(right, {})) replacement = left_id;
            break;
        case BinaryOperation::Multiply:
            if (constant_integer(left, {1})) replacement = right_id;
            else if (constant_integer(right, {1})) replacement = left_id;
            break;
        case BinaryOperation::BitAnd:
            if (constant_integer(left, all_ones)) replacement = right_id;
            else if (constant_integer(right, all_ones)) replacement = left_id;
            break;
        case BinaryOperation::BitOr:
        case BinaryOperation::BitXor:
            if (constant_integer(left, {})) replacement = right_id;
            else if (constant_integer(right, {})) replacement = left_id;
            break;
        case BinaryOperation::ShiftLeft:
        case BinaryOperation::ShiftRightArithmetic:
        case BinaryOperation::ShiftRightLogical:
        case BinaryOperation::RotateLeft:
        case BinaryOperation::RotateRight:
            if (constant_integer(right, {})) replacement = left_id;
            break;
        case BinaryOperation::NotEqual:
            if (hir_module.type(left.type).kind ==
                    hir::Type::Kind::Builtin &&
                hir_module.type(left.type).builtin == BuiltinType::Bool &&
                constant_integer(right, {})) {
                replacement = left_id;
            }
            break;
        case BinaryOperation::Equal:
            if (hir_module.type(left.type).kind ==
                    hir::Type::Kind::Builtin &&
                hir_module.type(left.type).builtin == BuiltinType::Bool &&
                constant_integer(right, {1})) {
                replacement = left_id;
            }
            break;
        default: break;
        }
        if (!replacement || *replacement == value.id) continue;
        replacements.add(value.id, *replacement);
        removed.insert(value.id.value);
    }
    replacements.apply(function);
    remove_replaced_values(function, removed);
}

std::optional<UInt128> folded_integer_constant(
    const ManagedFunction& function, const hir::Module& hir_module,
    ValueId id) {
    if (id.value >= function.values.size()) return std::nullopt;
    const auto& value = function.values[id.value];
    const auto bits = type_bits(hir_module, value.type);
    if (bits == 0) return std::nullopt;
    if (value.kind == ValueKind::ConstantInteger) {
        return mask_to(UInt128{value.integer, value.integer_high}, bits);
    }
    if (value.kind != ValueKind::Cast || value.operands.size() != 1) {
        return std::nullopt;
    }
    const auto source_id = value.operands.front();
    if (source_id.value >= function.values.size()) return std::nullopt;
    const auto source_bits = type_bits(
        hir_module, function.values[source_id.value].type);
    if (source_bits == 0) return std::nullopt;
    auto folded = folded_integer_constant(function, hir_module, source_id);
    if (!folded) return std::nullopt;
    *folded = mask_to(*folded, source_bits);
    if (value.cast == CastOperation::SignExtend &&
        bit(*folded, source_bits - 1U)) {
        *folded = bit_or(
            *folded,
            bit_not(mask_to(bit_not(UInt128{}), source_bits)));
    } else if (value.cast != CastOperation::ZeroExtend &&
               value.cast != CastOperation::Truncate &&
               value.cast != CastOperation::Reinterpret &&
               value.cast != CastOperation::SignExtend) {
        return std::nullopt;
    }
    return mask_to(*folded, bits);
}

bool reduce_constant_multiplications(ManagedFunction& function,
                                     const hir::Module& hir_module,
                                     const CompilerOptions& options) {
    const auto balance = std::min(options.risc_cisc_balance, 100U);
    const auto risc_weight = 100U - balance;
    const unsigned risc_budget =
        options.optimize_for == OptimizationGoal::Speed ? 5U : 2U;
    const auto operation_budget = risc_budget * risc_weight / 100U;
    if (operation_budget == 0) return false;

    struct Digit {
        unsigned shift{};
        bool subtract{};
    };

    bool changed = false;
    std::unordered_set<std::uint32_t> removed;
    for (auto& block : function.blocks) {
        std::vector<ValueId> rewritten;
        rewritten.reserve(block.values.size());
        const auto append = [&](ManagedValue value) {
            value.id = {
                static_cast<std::uint32_t>(function.values.size())};
            const auto id = value.id;
            function.values.push_back(std::move(value));
            rewritten.push_back(id);
            return id;
        };

        for (const auto id : block.values) {
            const auto source = function.values[id.value];
            if (source.kind != ValueKind::Binary ||
                source.binary != BinaryOperation::Multiply ||
                source.operands.size() != 2 ||
                !unsigned_integer_type(hir_module, source.type)) {
                rewritten.push_back(id);
                continue;
            }
            const auto bits = type_bits(hir_module, source.type);
            if (bits == 0 || bits > 64) {
                rewritten.push_back(id);
                continue;
            }
            const auto left = source.operands[0];
            const auto right = source.operands[1];
            const auto left_constant = folded_integer_constant(
                function, hir_module, left);
            const auto right_constant = folded_integer_constant(
                function, hir_module, right);
            if (left_constant.has_value() == right_constant.has_value()) {
                rewritten.push_back(id);
                continue;
            }
            const auto literal = left_constant ? left : right;
            const auto variable = left_constant ? right : left;
            const auto constant = left_constant ? *left_constant
                                                : *right_constant;
            if (constant.high != 0) {
                rewritten.push_back(id);
                continue;
            }
            const auto mask = bits == 64
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t{1} << bits) - 1U;
            const auto multiplier = constant.low & mask;
            if (multiplier == 0) {
                replace_value_uses(function, id, literal);
                removed.insert(id.value);
                changed = true;
                continue;
            }
            if (multiplier == 1) {
                replace_value_uses(function, id, variable);
                removed.insert(id.value);
                changed = true;
                continue;
            }
            if (multiplier == mask) {
                auto& replacement = function.values[id.value];
                replacement.kind = ValueKind::Unary;
                replacement.unary = UnaryOperation::Negate;
                replacement.operands = {variable};
                replacement.incoming.clear();
                rewritten.push_back(id);
                changed = true;
                continue;
            }
            // Keep the signed-digit builder away from the one uint64 carry
            // case. Large modular constants are better left to the target's
            // multiply instruction in any event.
            if (multiplier > std::numeric_limits<std::int64_t>::max()) {
                rewritten.push_back(id);
                continue;
            }

            // Non-adjacent signed digits minimize the number of shifted
            // addends. For example, 3*x becomes (x<<2)-x and 17*x becomes
            // (x<<4)+x. This is target-independent modular arithmetic; the
            // RISC/CISC factor controls only its profitability.
            std::vector<Digit> digits;
            auto remaining = multiplier;
            unsigned shift{};
            while (remaining != 0) {
                if ((remaining & 1U) != 0) {
                    const bool subtract =
                        (remaining & 3U) == 3U;
                    digits.push_back({shift, subtract});
                    if (subtract) ++remaining;
                    else --remaining;
                }
                remaining >>= 1U;
                ++shift;
            }
            std::reverse(digits.begin(), digits.end());
            unsigned cost =
                static_cast<unsigned>(digits.size() - 1U);
            for (const auto digit : digits) {
                if (digit.shift != 0) ++cost;
            }
            if (cost == 0 || cost > operation_budget) {
                rewritten.push_back(id);
                continue;
            }

            if (digits.size() == 1) {
                ManagedValue amount;
                amount.location = source.location;
                amount.type = source.type;
                amount.kind = ValueKind::ConstantInteger;
                amount.integer = digits.front().shift;
                const auto amount_id = append(std::move(amount));
                auto& replacement = function.values[id.value];
                replacement.kind = ValueKind::Binary;
                replacement.binary = BinaryOperation::ShiftLeft;
                replacement.operands = {variable, amount_id};
                replacement.incoming.clear();
                rewritten.push_back(id);
                changed = true;
                continue;
            }

            const auto shifted_term = [&](unsigned amount) {
                if (amount == 0) return variable;
                ManagedValue amount_value;
                amount_value.location = source.location;
                amount_value.type = source.type;
                amount_value.kind = ValueKind::ConstantInteger;
                amount_value.integer = amount;
                const auto amount_id = append(std::move(amount_value));

                ManagedValue shifted;
                shifted.location = source.location;
                shifted.type = source.type;
                shifted.kind = ValueKind::Binary;
                shifted.binary = BinaryOperation::ShiftLeft;
                shifted.operands = {variable, amount_id};
                return append(std::move(shifted));
            };

            std::vector<ValueId> terms;
            terms.reserve(digits.size());
            for (const auto digit : digits) {
                terms.push_back(shifted_term(digit.shift));
            }
            auto combined = terms.front();
            for (std::size_t index = 1; index < terms.size(); ++index) {
                const auto operation = digits[index].subtract
                    ? BinaryOperation::Subtract
                    : BinaryOperation::Add;
                if (index + 1U == terms.size()) {
                    auto& replacement = function.values[id.value];
                    replacement.kind = ValueKind::Binary;
                    replacement.binary = operation;
                    replacement.operands = {combined, terms[index]};
                    replacement.incoming.clear();
                    combined = id;
                } else {
                    ManagedValue next;
                    next.location = source.location;
                    next.type = source.type;
                    next.kind = ValueKind::Binary;
                    next.binary = operation;
                    next.operands = {combined, terms[index]};
                    combined = append(std::move(next));
                }
            }
            rewritten.push_back(id);
            changed = true;
        }
        block.values = std::move(rewritten);
    }
    remove_replaced_values(function, removed);
    return changed;
}

// Division and remainder by an integer constant through a fixed-point
// reciprocal: Granlund and Montgomery, "Division by Invariant Integers using
// Multiplication" (PLDI 1994), with GCC's choice of the smallest multiplier.
unsigned bit_width(UInt128 value) {
    return value.high != 0
        ? 64U + static_cast<unsigned>(std::bit_width(value.high))
        : static_cast<unsigned>(std::bit_width(value.low));
}

unsigned trailing_zeros(UInt128 value) {
    return value.low != 0
        ? static_cast<unsigned>(std::countr_zero(value.low))
        : 64U + static_cast<unsigned>(std::countr_zero(value.high));
}

bool power_of_two(UInt128 value) {
    return bit_width(value) == trailing_zeros(value) + 1U;
}

struct Reciprocal {
    UInt128 multiplier;
    unsigned shift{};
};

// The multiplier m < 2^(bits + 1) and the shift s with
// floor(x / divisor) == floor(m * x / 2^(bits + s)) for 0 <= x < 2^precision.
// Requires 1 < divisor < 2^bits, 0 < precision <= bits, and
// bits + ceil(log2(divisor)) < 128.
Reciprocal choose_reciprocal(UInt128 divisor, unsigned bits,
                             unsigned precision) {
    const auto log = bit_width(subtract(divisor, UInt128{1}));
    const auto scale = shift_left(UInt128{1}, bits + log);
    auto low = divide(scale, divisor).first;
    auto high = divide(add(scale, shift_left(UInt128{1},
                                              bits + log - precision)),
                       divisor).first;
    auto shift = log;
    for (; shift > 0; --shift) {
        const auto next_low = shift_right(low, 1);
        const auto next_high = shift_right(high, 1);
        if (!(next_low < next_high)) break;
        low = next_low;
        high = next_high;
    }
    return {high, shift};
}

struct ConstantDivision {
    ValueId dividend;
    // The divisor's bit pattern at the operation width.
    UInt128 divisor;
    unsigned bits{};
    // The dividend is below 2^precision; less than `bits` proves it
    // nonnegative.
    unsigned precision{};
    bool is_signed{};
    // The target selects these multiply-high operations at this width.
    bool unsigned_multiply_high{};
    bool signed_multiply_high{};
};

// Builds a replacement sequence before a division, or only counts its
// operations and materialized constants when it has no function.
class DivisionSequence {
public:
    DivisionSequence(ManagedFunction* function, std::vector<ValueId>* values,
                     const ManagedValue& division, unsigned bits,
                     hir::TypeId boolean, const Subtarget& subtarget,
                     bool is_signed)
        : function_(function), values_(values),
          location_(division.location), type_(division.type), bits_(bits),
          boolean_(boolean), subtarget_(subtarget), is_signed_(is_signed) {}

    unsigned multiplies{};
    unsigned operations{};
    unsigned constant_cost{};

    // Shift amounts and other small literals are instruction immediates on
    // every target and are not priced.
    ValueId constant(UInt128 value, bool materialized = true) {
        value = mask_to(value, bits_);
        if (materialized) {
            constant_cost += subtarget_.integer_constant_materialization_cost(
                {bits_, value.low, value.high, is_signed_});
        }
        ManagedValue result;
        result.kind = ValueKind::ConstantInteger;
        result.integer = value.low;
        result.integer_high = value.high;
        return append(std::move(result), type_);
    }

    ValueId binary(BinaryOperation operation, ValueId left, ValueId right) {
        if (operation == BinaryOperation::Multiply ||
            operation == BinaryOperation::UnsignedMultiplyHigh ||
            operation == BinaryOperation::SignedMultiplyHigh) {
            ++multiplies;
        } else {
            ++operations;
        }
        ManagedValue result;
        result.kind = ValueKind::Binary;
        result.binary = operation;
        result.operands = {left, right};
        return append(std::move(result), operation >= BinaryOperation::Equal
                                             ? boolean_
                                             : type_);
    }

    ValueId shift(BinaryOperation operation, ValueId value, unsigned amount) {
        if (amount == 0) return value;
        return binary(operation, value, constant(amount, false));
    }

    ValueId negate(ValueId value) {
        ++operations;
        ManagedValue result;
        result.kind = ValueKind::Unary;
        result.unary = UnaryOperation::Negate;
        result.operands = {value};
        return append(std::move(result), type_);
    }

    ValueId widen(ValueId truth) {
        ++operations;
        ManagedValue result;
        result.kind = ValueKind::Cast;
        result.cast = CastOperation::ZeroExtend;
        result.operands = {truth};
        return append(std::move(result), type_);
    }

private:
    ValueId append(ManagedValue value, hir::TypeId type) {
        if (!function_) return {};
        const ValueId id{static_cast<std::uint32_t>(function_->values.size())};
        value.id = id;
        value.location = location_;
        value.type = type;
        function_->values.push_back(std::move(value));
        values_->push_back(id);
        return id;
    }

    ManagedFunction* function_;
    std::vector<ValueId>* values_;
    SourceLocation location_;
    hir::TypeId type_;
    unsigned bits_;
    hir::TypeId boolean_;
    const Subtarget& subtarget_;
    bool is_signed_;
};

// floor(dividend / magnitude) for a nonnegative dividend.
std::optional<ValueId> unsigned_quotient(DivisionSequence& sequence,
                                         const ConstantDivision& division,
                                         UInt128 magnitude) {
    const auto x = division.dividend;
    const auto bits = division.bits;
    const auto precision = division.precision;
    if (magnitude == UInt128{1}) return x;
    if (bit_width(magnitude) > precision) {
        return sequence.constant({}, false);
    }
    if (power_of_two(magnitude)) {
        return sequence.shift(BinaryOperation::ShiftRightLogical, x,
                              trailing_zeros(magnitude));
    }
    if (bit_width(magnitude) == bits) {
        // Above 2^(bits-1) the quotient is zero or one.
        return sequence.widen(sequence.binary(
            BinaryOperation::UnsignedGreaterEqual, x,
            sequence.constant(magnitude)));
    }
    const auto log = bit_width(subtract(magnitude, UInt128{1}));
    if (precision < bits && precision + log < 128) {
        // A narrow dividend admits an ordinary multiply whose full product
        // still fits the operation width.
        const auto reciprocal = choose_reciprocal(magnitude, precision,
                                                  precision);
        if (bit_width(reciprocal.multiplier) + precision <= bits &&
            precision + reciprocal.shift < bits) {
            return sequence.shift(
                BinaryOperation::ShiftRightLogical,
                sequence.binary(BinaryOperation::Multiply, x,
                                sequence.constant(reciprocal.multiplier)),
                precision + reciprocal.shift);
        }
    }
    if (!division.unsigned_multiply_high || bits + log >= 128) {
        return std::nullopt;
    }
    auto reciprocal = choose_reciprocal(magnitude, bits, precision);
    if (bit_width(reciprocal.multiplier) <= bits) {
        return sequence.shift(
            BinaryOperation::ShiftRightLogical,
            sequence.binary(BinaryOperation::UnsignedMultiplyHigh, x,
                            sequence.constant(reciprocal.multiplier)),
            reciprocal.shift);
    }
    if (const auto zeros = trailing_zeros(magnitude); zeros != 0) {
        // Dividing out the even factor first leaves a narrower dividend
        // whose multiplier fits the operation width.
        reciprocal = choose_reciprocal(shift_right(magnitude, zeros), bits,
                                       precision - zeros);
        if (bit_width(reciprocal.multiplier) > bits) return std::nullopt;
        const auto shifted = sequence.shift(
            BinaryOperation::ShiftRightLogical, x, zeros);
        return sequence.shift(
            BinaryOperation::ShiftRightLogical,
            sequence.binary(BinaryOperation::UnsignedMultiplyHigh, shifted,
                            sequence.constant(reciprocal.multiplier)),
            reciprocal.shift);
    }
    if (reciprocal.shift == 0) return std::nullopt;
    // The multiplier needs bits + 1 bits: add its implicit top bit back as
    // the dividend through an overflow-free average.
    const auto high = sequence.binary(
        BinaryOperation::UnsignedMultiplyHigh, x,
        sequence.constant(reciprocal.multiplier));
    const auto half = sequence.shift(
        BinaryOperation::ShiftRightLogical,
        sequence.binary(BinaryOperation::Subtract, x, high), 1);
    return sequence.shift(BinaryOperation::ShiftRightLogical,
                          sequence.binary(BinaryOperation::Add, half, high),
                          reciprocal.shift - 1);
}

std::optional<ValueId> unsigned_remainder(DivisionSequence& sequence,
                                          const ConstantDivision& division,
                                          UInt128 magnitude) {
    const auto x = division.dividend;
    const auto bits = division.bits;
    if (magnitude == UInt128{1}) return sequence.constant({}, false);
    if (bit_width(magnitude) > division.precision) return x;
    if (power_of_two(magnitude)) {
        return sequence.binary(
            BinaryOperation::BitAnd, x,
            sequence.constant(subtract(magnitude, UInt128{1})));
    }
    if (bit_width(magnitude) == bits) {
        const auto divisor = sequence.constant(magnitude);
        const auto quotient = sequence.widen(sequence.binary(
            BinaryOperation::UnsignedGreaterEqual, x, divisor));
        return sequence.binary(
            BinaryOperation::Subtract, x,
            sequence.binary(BinaryOperation::BitAnd,
                            sequence.negate(quotient), divisor));
    }
    const auto quotient = unsigned_quotient(sequence, division, magnitude);
    if (!quotient) return std::nullopt;
    return sequence.binary(
        BinaryOperation::Subtract, x,
        sequence.binary(BinaryOperation::Multiply, *quotient,
                        sequence.constant(magnitude)));
}

// 2^power - 1 for a negative dividend and zero otherwise, so that the
// arithmetic shift by `power` rounds toward zero.
ValueId rounding_bias(DivisionSequence& sequence, ValueId x, unsigned bits,
                      unsigned power) {
    if (power == 1) {
        return sequence.shift(BinaryOperation::ShiftRightLogical, x,
                              bits - 1);
    }
    return sequence.shift(
        BinaryOperation::ShiftRightLogical,
        sequence.shift(BinaryOperation::ShiftRightArithmetic, x, bits - 1),
        bits - power);
}

UInt128 signed_magnitude(const ConstantDivision& division) {
    return bit(division.divisor, division.bits - 1)
        ? mask_to(negate(division.divisor), division.bits)
        : division.divisor;
}

std::optional<ValueId> signed_quotient(DivisionSequence& sequence,
                                       const ConstantDivision& division) {
    const auto x = division.dividend;
    const auto bits = division.bits;
    const bool negative = bit(division.divisor, bits - 1);
    const auto magnitude = signed_magnitude(division);
    if (division.precision < bits) {
        const auto quotient = unsigned_quotient(sequence, division, magnitude);
        if (!quotient || !negative) return quotient;
        return sequence.negate(*quotient);
    }
    if (magnitude == UInt128{1}) return negative ? sequence.negate(x) : x;
    if (power_of_two(magnitude)) {
        const auto power = trailing_zeros(magnitude);
        if (power + 1 == bits) {
            // Only the most negative value itself has a nonzero quotient.
            return sequence.widen(sequence.binary(
                BinaryOperation::Equal, x,
                sequence.constant(division.divisor)));
        }
        const auto quotient = sequence.shift(
            BinaryOperation::ShiftRightArithmetic,
            sequence.binary(BinaryOperation::Add, x,
                            rounding_bias(sequence, x, bits, power)),
            power);
        return negative ? sequence.negate(quotient) : quotient;
    }
    if (!division.signed_multiply_high || bits > 64) return std::nullopt;
    const auto reciprocal = choose_reciprocal(magnitude, bits, bits - 1);
    if (bit_width(reciprocal.multiplier) > bits) return std::nullopt;
    auto product = sequence.binary(
        BinaryOperation::SignedMultiplyHigh, x,
        sequence.constant(reciprocal.multiplier));
    // A multiplier with its top bit set reads as m - 2^bits.
    if (bit(reciprocal.multiplier, bits - 1)) {
        product = sequence.binary(BinaryOperation::Add, product, x);
    }
    const auto shifted = sequence.shift(BinaryOperation::ShiftRightArithmetic,
                                        product, reciprocal.shift);
    const auto sign = sequence.shift(BinaryOperation::ShiftRightArithmetic,
                                     x, bits - 1);
    return negative
        ? sequence.binary(BinaryOperation::Subtract, sign, shifted)
        : sequence.binary(BinaryOperation::Subtract, shifted, sign);
}

std::optional<ValueId> signed_remainder(DivisionSequence& sequence,
                                        const ConstantDivision& division) {
    const auto x = division.dividend;
    const auto bits = division.bits;
    const auto magnitude = signed_magnitude(division);
    // The remainder takes the dividend's sign, so only |divisor| matters.
    if (division.precision < bits) {
        return unsigned_remainder(sequence, division, magnitude);
    }
    if (magnitude == UInt128{1}) return sequence.constant({}, false);
    if (power_of_two(magnitude)) {
        const auto power = trailing_zeros(magnitude);
        if (power + 1 == bits) {
            const auto quotient = sequence.widen(sequence.binary(
                BinaryOperation::Equal, x,
                sequence.constant(division.divisor)));
            return sequence.binary(
                BinaryOperation::BitAnd, x,
                sequence.binary(BinaryOperation::Subtract, quotient,
                                sequence.constant(UInt128{1}, false)));
        }
        const auto rounded = sequence.binary(
            BinaryOperation::Add, x, rounding_bias(sequence, x, bits, power));
        return sequence.binary(
            BinaryOperation::Subtract, x,
            sequence.binary(BinaryOperation::BitAnd, rounded,
                            sequence.constant(negate(magnitude))));
    }
    const auto quotient = signed_quotient(sequence, division);
    if (!quotient) return std::nullopt;
    return sequence.binary(
        BinaryOperation::Subtract, x,
        sequence.binary(BinaryOperation::Multiply, *quotient,
                        sequence.constant(division.divisor)));
}

// An upper bound on the significant bits of an integer value: a zero
// extension, a constant mask, or a constant logical right shift clears the
// high bits.
unsigned significant_bits(const ManagedFunction& function,
                          const hir::Module& hir_module, ValueId id,
                          unsigned bits) {
    const auto& value = function.values[id.value];
    if (value.kind == ValueKind::Cast &&
        value.cast == CastOperation::ZeroExtend &&
        value.operands.size() == 1) {
        const auto source = type_bits(
            hir_module, function.values[value.operands.front().value].type);
        if (source != 0) return std::min(source, bits);
    }
    if (value.kind != ValueKind::Binary || value.operands.size() != 2) {
        return bits;
    }
    if (value.binary == BinaryOperation::BitAnd) {
        for (const auto operand : value.operands) {
            if (const auto mask =
                    folded_integer_constant(function, hir_module, operand)) {
                return std::min(bits, bit_width(mask_to(*mask, bits)));
            }
        }
    }
    if (value.binary == BinaryOperation::ShiftRightLogical) {
        const auto amount = folded_integer_constant(function, hir_module,
                                                    value.operands[1]);
        if (amount && amount->high == 0 && amount->low < bits) {
            return bits - static_cast<unsigned>(amount->low);
        }
    }
    return bits;
}

// A size objective admits at most one simple operation, which no divide
// sequence undercuts. A speed objective compares the target's latencies,
// charging materialized constants by the RISC weight of the cost blend.
bool profitable_division_sequence(const DivisionSequence& sequence,
                                  const ConstantDivision& division,
                                  const Subtarget& subtarget,
                                  const CompilerOptions& options) {
    if (options.optimize_for == OptimizationGoal::Size ||
        options.optimize_for == OptimizationGoal::MinimumSize) {
        return sequence.multiplies == 0 && sequence.operations <= 1;
    }
    const IntegerOperationCostQuery query{division.bits, division.is_signed};
    // A width without a divide instruction is a software loop.
    const auto divide = subtarget.integer_division_cost(query);
    if (!divide) return true;
    const auto multiply = subtarget.integer_multiply_high_cost(query);
    if (sequence.multiplies != 0 && !multiply) return false;
    const auto risc_weight = 100U - std::min(options.risc_cisc_balance, 100U);
    const auto replacement =
        (sequence.multiplies * multiply.value_or(0) + sequence.operations) *
            100U +
        sequence.constant_cost * risc_weight;
    const auto original =
        *divide * 100U +
        subtarget.integer_constant_materialization_cost(
            {division.bits, division.divisor.low, division.divisor.high,
             division.is_signed}) *
            risc_weight;
    return replacement < original;
}

bool reduce_constant_divisions(ManagedFunction& function,
                               const hir::Module& hir_module,
                               const Subtarget& subtarget,
                               const CompilerOptions& options) {
    const auto boolean = hir_module.builtin(BuiltinType::Bool);
    if (!boolean) return false;
    bool changed = false;
    std::unordered_set<std::uint32_t> removed;
    ValueReplacements replacements;
    for (auto& block : function.blocks) {
        std::vector<ValueId> rewritten;
        rewritten.reserve(block.values.size());
        for (const auto id : block.values) {
            const auto& candidate = function.values[id.value];
            const bool quotient =
                candidate.binary == BinaryOperation::SignedDivide ||
                candidate.binary == BinaryOperation::UnsignedDivide;
            const bool is_signed =
                candidate.binary == BinaryOperation::SignedDivide ||
                candidate.binary == BinaryOperation::SignedRemainder;
            const auto bits = type_bits(hir_module, candidate.type);
            if (candidate.kind != ValueKind::Binary ||
                candidate.operands.size() != 2 ||
                (!quotient &&
                 candidate.binary != BinaryOperation::SignedRemainder &&
                 candidate.binary != BinaryOperation::UnsignedRemainder) ||
                !integer_type(hir_module, candidate.type) || bits < 32 ||
                type_bits(hir_module,
                          function.values[candidate.operands[0].value]
                              .type) != bits) {
                rewritten.push_back(id);
                continue;
            }
            const auto divisor = folded_integer_constant(
                function, hir_module, candidate.operands[1]);
            if (!divisor || mask_to(*divisor, bits) == UInt128{}) {
                rewritten.push_back(id);
                continue;
            }
            // Appending the sequence below invalidates `candidate`.
            const auto source = candidate;
            const ConstantDivision division{
                source.operands[0], mask_to(*divisor, bits), bits,
                significant_bits(function, hir_module, source.operands[0],
                                 bits),
                is_signed,
                subtarget.integer_multiply_high_cost({bits, false}).has_value(),
                subtarget.integer_multiply_high_cost({bits, true}).has_value()};
            const auto build = [&](DivisionSequence& sequence) {
                if (is_signed) {
                    return quotient ? signed_quotient(sequence, division)
                                    : signed_remainder(sequence, division);
                }
                return quotient
                    ? unsigned_quotient(sequence, division, division.divisor)
                    : unsigned_remainder(sequence, division, division.divisor);
            };
            DivisionSequence estimate(nullptr, nullptr, source, bits,
                                      *boolean, subtarget, is_signed);
            if (!build(estimate) ||
                !profitable_division_sequence(estimate, division, subtarget,
                                              options)) {
                rewritten.push_back(id);
                continue;
            }
            const auto first = function.values.size();
            DivisionSequence sequence(&function, &rewritten, source, bits,
                                      *boolean, subtarget, is_signed);
            const auto result = *build(sequence);
            if (result.value >= first &&
                result.value + 1 == function.values.size()) {
                // The last new operation takes over the division's identity.
                auto final_value = std::move(function.values.back());
                function.values.pop_back();
                rewritten.pop_back();
                final_value.id = id;
                function.values[id.value] = std::move(final_value);
                rewritten.push_back(id);
            } else {
                replacements.add(id, result);
                removed.insert(id.value);
            }
            changed = true;
        }
        block.values = std::move(rewritten);
    }
    replacements.apply(function);
    remove_replaced_values(function, removed);
    return changed;
}

bool floating_zero(const ManagedValue& value, unsigned bits) {
    if (value.kind != ValueKind::ConstantFloating) return false;
    if (bits == 32) return (value.integer & 0x7fffffffU) == 0;
    if (bits == 64) {
        return (value.integer & 0x7fffffffffffffffULL) == 0;
    }
    if (bits == 80) {
        return value.integer == 0 &&
               (value.integer_high & 0x7fffU) == 0;
    }
    if (bits == 128) {
        return value.integer == 0 &&
               (value.integer_high & 0x7fffffffffffffffULL) == 0;
    }
    return false;
}

bool floating_one(const ManagedValue& value, unsigned bits) {
    if (value.kind != ValueKind::ConstantFloating) return false;
    if (bits == 32) {
        return value.integer_high == 0 &&
               value.integer == 0x3f800000U;
    }
    if (bits == 64) {
        return value.integer_high == 0 &&
               value.integer == 0x3ff0000000000000ULL;
    }
    if (bits == 80) {
        return value.integer == 0x8000000000000000ULL &&
               value.integer_high == 0x3fffU;
    }
    if (bits == 128) {
        return value.integer == 0 &&
               value.integer_high == 0x3fff000000000000ULL;
    }
    return false;
}

void simplify_floating_math(ManagedFunction& function,
                            const hir::Module& hir_module,
                            const CompilerOptions& options) {
    std::unordered_set<std::uint32_t> removed;
    ValueReplacements replacements;
    for (auto& value : function.values) {
        replacements.rewrite(value);
        if (value.kind != ValueKind::Binary || value.operands.size() != 2) {
            continue;
        }
        const auto left_id = value.operands[0];
        const auto right_id = value.operands[1];
        const auto& left = function.values[left_id.value];
        const auto& right = function.values[right_id.value];
        if (!floating_type(hir_module, left.type) ||
            left.type != right.type) {
            continue;
        }
        if (options.finite_math_only && left_id == right_id) {
            std::optional<bool> folded;
            switch (value.binary) {
            case BinaryOperation::Equal:
            case BinaryOperation::SignedLessEqual:
            case BinaryOperation::SignedGreaterEqual:
                folded = true;
                break;
            case BinaryOperation::NotEqual:
            case BinaryOperation::SignedLess:
            case BinaryOperation::SignedGreater:
                folded = false;
                break;
            default: break;
            }
            if (folded) {
                value.kind = ValueKind::ConstantInteger;
                value.integer = *folded ? 1U : 0U;
                value.integer_high = 0;
                value.operands.clear();
                continue;
            }
        }
        if (!floating_type(hir_module, value.type)) continue;
        const auto bits = type_bits(hir_module, value.type);
        std::optional<ValueId> replacement;
        switch (value.binary) {
        case BinaryOperation::Add:
            if (!options.signed_zeros) {
                if (floating_zero(left, bits)) replacement = right_id;
                else if (floating_zero(right, bits)) replacement = left_id;
            }
            break;
        case BinaryOperation::Subtract:
            if (!options.signed_zeros && floating_zero(right, bits)) {
                replacement = left_id;
            }
            break;
        case BinaryOperation::Multiply:
            if (options.fast_math && floating_one(left, bits)) {
                replacement = right_id;
            } else if (options.fast_math && floating_one(right, bits)) {
                replacement = left_id;
            } else if (options.finite_math_only && !options.signed_zeros &&
                       floating_zero(left, bits)) {
                replacement = left_id;
            } else if (options.finite_math_only && !options.signed_zeros &&
                       floating_zero(right, bits)) {
                replacement = right_id;
            }
            break;
        case BinaryOperation::SignedDivide:
            if (options.fast_math && floating_one(right, bits)) {
                replacement = left_id;
            }
            break;
        default: break;
        }
        if (!replacement || *replacement == value.id) continue;
        replacements.add(value.id, *replacement);
        removed.insert(value.id.value);
    }
    replacements.apply(function);
    remove_replaced_values(function, removed);
}

std::vector<bool> reachable_blocks(
    const ManagedFunction& function,
    std::optional<BlockId> edge_owner = std::nullopt,
    std::optional<BlockId> ignored_successor = std::nullopt,
    bool preserve_labels = false) {
    std::vector<bool> reachable(function.blocks.size());
    std::vector<BlockId> pending{function.entry};
    if (preserve_labels)
        for (const auto& label : function.labels) pending.push_back(label.block);
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id.value >= reachable.size() || reachable[id.value]) continue;
        reachable[id.value] = true;
        for (const auto successor :
             function.blocks[id.value].terminator.successors) {
            if (edge_owner == id && ignored_successor == successor) continue;
            pending.push_back(successor);
        }
    }
    return reachable;
}

void eliminate_unreachable_blocks(ManagedFunction& function) {
    // Labels are observable addresses even outside ordinary entry flow.
    // Keep their forward closure, but still remove detached non-label blocks
    // after edge rewrites; skipping compaction would leave stale CFG edges.
    const auto reachable = reachable_blocks(function, {}, {}, true);
    if (std::all_of(reachable.begin(), reachable.end(),
                    [](bool value) { return value; })) {
        return;
    }
    for (auto& block : function.blocks) {
        if (!reachable[block.id.value]) continue;
        std::erase_if(block.predecessors, [&](BlockId predecessor) {
            return !reachable[predecessor.value];
        });
        auto& entry_effect = function.effects[block.effect.value];
        std::erase_if(entry_effect.incoming,
                      [&](const EffectIncoming& incoming) {
                          return !reachable[incoming.predecessor.value];
                      });
        for (const auto value_id : block.values) {
            auto& value = function.values[value_id.value];
            if (value.kind != ValueKind::Phi) continue;
            std::erase_if(value.incoming, [&](const PhiIncoming& incoming) {
                return !reachable[incoming.predecessor.value];
            });
        }
    }

    std::vector<std::optional<BlockId>> remap(function.blocks.size());
    std::vector<ManagedBlock> blocks;
    blocks.reserve(function.blocks.size());
    for (std::size_t index = 0; index < function.blocks.size(); ++index) {
        if (!reachable[index]) continue;
        const BlockId replacement{
            static_cast<std::uint32_t>(blocks.size())};
        remap[index] = replacement;
        auto block = std::move(function.blocks[index]);
        block.id = replacement;
        blocks.push_back(std::move(block));
    }
    const auto map = [&](BlockId id) { return *remap[id.value]; };
    function.entry = map(function.entry);
    for (auto& block : blocks) {
        for (auto& predecessor : block.predecessors) {
            predecessor = map(predecessor);
        }
        for (auto& successor : block.terminator.successors) {
            successor = map(successor);
        }
        auto& entry_effect = function.effects[block.effect.value];
        for (auto& incoming : entry_effect.incoming) {
            incoming.predecessor = map(incoming.predecessor);
        }
        for (const auto value_id : block.values) {
            auto& value = function.values[value_id.value];
            if (value.kind != ValueKind::Phi) continue;
            for (auto& incoming : value.incoming) {
                incoming.predecessor = map(incoming.predecessor);
            }
        }
    }
    for (auto& label : function.labels) label.block = map(label.block);
    function.blocks = std::move(blocks);
    compact_effects(function);
    compact_managed_values(function);
}

bool fold_constant_branches(ManagedFunction& function) {
    bool changed = false;
    for (auto& block : function.blocks) {
        auto& terminator = block.terminator;
        if (terminator.kind != TerminatorKind::ConditionalBranch ||
            !terminator.value || terminator.successors.size() != 2) {
            continue;
        }
        const auto& condition = function.values[terminator.value->value];
        if (condition.kind != ValueKind::ConstantInteger) continue;

        const bool truth = condition.integer != 0 ||
                           condition.integer_high != 0;
        const auto selected = terminator.successors[truth ? 0U : 1U];
        const auto discarded = terminator.successors[truth ? 1U : 0U];
        const auto prospective =
            reachable_blocks(function, block.id, discarded);
        const bool would_discard_label = std::any_of(
            function.labels.begin(), function.labels.end(),
            [&](const ManagedLabel& label) {
                return !prospective[label.block.value];
            });
        if (would_discard_label) continue;
        terminator.kind = TerminatorKind::Branch;
        terminator.value.reset();
        terminator.successors = {selected};

        auto& discarded_block = function.blocks[discarded.value];
        std::erase(discarded_block.predecessors, block.id);
        auto& entry_effect = function.effects[discarded_block.effect.value];
        std::erase_if(entry_effect.incoming,
                      [&](const EffectIncoming& incoming) {
                          return incoming.predecessor == block.id;
                      });
        for (const auto value_id : discarded_block.values) {
            auto& value = function.values[value_id.value];
            if (value.kind != ValueKind::Phi) continue;
            std::erase_if(value.incoming, [&](const PhiIncoming& incoming) {
                return incoming.predecessor == block.id;
            });
        }
        changed = true;
    }
    if (changed) eliminate_unreachable_blocks(function);
    return changed;
}

bool thread_boolean_phi_branch_once(ManagedFunction& function) {
    for (const auto& merge : function.blocks) {
        if (merge.terminator.kind != TerminatorKind::ConditionalBranch ||
            !merge.terminator.value ||
            merge.terminator.successors.size() != 2 ||
            merge.terminator.successors[0] ==
                merge.terminator.successors[1] ||
            std::find(merge.values.begin(), merge.values.end(),
                      *merge.terminator.value) == merge.values.end() ||
            std::any_of(
                merge.values.begin(), merge.values.end(),
                [&](ValueId id) {
                    const auto& value = function.values[id.value];
                    return id != *merge.terminator.value &&
                           value.kind != ValueKind::ConstantInteger &&
                           value.kind != ValueKind::ConstantFloating;
                }) ||
            std::any_of(function.labels.begin(), function.labels.end(),
                        [&](const ManagedLabel& label) {
                            return label.block == merge.id;
                        })) {
            continue;
        }
        const auto condition_id = *merge.terminator.value;
        const auto condition = function.values[condition_id.value];
        if (condition.kind != ValueKind::Phi ||
            condition.incoming.size() != merge.predecessors.size() ||
            merge.predecessors.size() < 2) {
            continue;
        }
        struct Plan {
            BlockId predecessor;
            ValueId condition;
            std::optional<bool> constant;
        };
        std::vector<Plan> plans;
        bool valid = true;
        for (const auto predecessor_id : merge.predecessors) {
            const auto incoming = std::find_if(
                condition.incoming.begin(), condition.incoming.end(),
                [&](const PhiIncoming& value) {
                    return value.predecessor == predecessor_id;
                });
            if (incoming == condition.incoming.end()) {
                valid = false;
                break;
            }
            const auto& value = function.values[incoming->value.value];
            std::optional<bool> constant;
            if (value.kind == ValueKind::ConstantInteger) {
                constant = value.integer != 0 || value.integer_high != 0;
            }
            const auto& predecessor =
                function.blocks[predecessor_id.value];
            const auto edge_count = static_cast<unsigned>(std::count(
                predecessor.terminator.successors.begin(),
                predecessor.terminator.successors.end(), merge.id));
            if (edge_count != 1 ||
                (!constant &&
                 (predecessor.terminator.kind != TerminatorKind::Branch ||
                  predecessor.terminator.successors.size() != 1))) {
                valid = false;
                break;
            }
            plans.push_back(
                {predecessor_id, incoming->value, constant});
        }
        if (!valid) continue;

        // Constants materialized in the merge may feed phis or operations in
        // either successor.  Redirecting every incoming edge makes the merge
        // unreachable, so keep those definitions in the entry block where
        // they dominate every replacement edge.  The branch condition phi
        // itself remains in the merge and dies with it.
        auto& mutable_merge = function.blocks[merge.id.value];
        std::vector<ValueId> hoisted_constants;
        for (const auto id : mutable_merge.values) {
            if (id == condition_id) continue;
            const auto kind = function.values[id.value].kind;
            if (kind == ValueKind::ConstantInteger ||
                kind == ValueKind::ConstantFloating) {
                hoisted_constants.push_back(id);
            }
        }
        if (!hoisted_constants.empty()) {
            auto& entry_values =
                function.blocks[function.entry.value].values;
            entry_values.insert(entry_values.end(),
                                hoisted_constants.begin(),
                                hoisted_constants.end());
            std::erase_if(mutable_merge.values, [&](ValueId id) {
                return std::find(hoisted_constants.begin(),
                                 hoisted_constants.end(), id) !=
                       hoisted_constants.end();
            });
        }

        const auto truth = merge.terminator.successors[0];
        const auto falsity = merge.terminator.successors[1];
        std::unordered_map<std::uint32_t, std::vector<BlockId>> routed;
        for (const auto& plan : plans) {
            auto& predecessor = function.blocks[plan.predecessor.value];
            if (plan.constant) {
                const auto target = *plan.constant ? truth : falsity;
                std::replace(predecessor.terminator.successors.begin(),
                             predecessor.terminator.successors.end(),
                             merge.id, target);
                if (predecessor.terminator.kind ==
                        TerminatorKind::ConditionalBranch &&
                    predecessor.terminator.successors.size() == 2 &&
                    predecessor.terminator.successors[0] ==
                        predecessor.terminator.successors[1]) {
                    predecessor.terminator.kind = TerminatorKind::Branch;
                    predecessor.terminator.value.reset();
                    predecessor.terminator.successors.resize(1);
                }
                routed[target.value].push_back(plan.predecessor);
            } else {
                predecessor.terminator.kind =
                    TerminatorKind::ConditionalBranch;
                predecessor.terminator.value = plan.condition;
                predecessor.terminator.successors = {truth, falsity};
                routed[truth.value].push_back(plan.predecessor);
                routed[falsity.value].push_back(plan.predecessor);
            }
        }

        for (const auto target_id : {truth, falsity}) {
            auto& target = function.blocks[target_id.value];
            auto predecessors = routed[target_id.value];
            std::sort(predecessors.begin(), predecessors.end(),
                      [](BlockId left, BlockId right) {
                          return left.value < right.value;
                      });
            predecessors.erase(
                std::unique(predecessors.begin(), predecessors.end()),
                predecessors.end());
            std::erase(target.predecessors, merge.id);
            for (const auto predecessor : predecessors) {
                if (std::find(target.predecessors.begin(),
                              target.predecessors.end(), predecessor) ==
                    target.predecessors.end()) {
                    target.predecessors.push_back(predecessor);
                }
            }
            auto& effect = function.effects[target.effect.value];
            std::erase_if(effect.incoming,
                          [&](const EffectIncoming& incoming) {
                              return incoming.predecessor == merge.id;
                          });
            for (const auto predecessor : predecessors) {
                effect.incoming.push_back(
                    {predecessor,
                     function.blocks[predecessor.value].terminator.effect});
            }
            for (const auto id : target.values) {
                auto& value = function.values[id.value];
                if (value.kind != ValueKind::Phi) continue;
                const auto incoming = std::find_if(
                    value.incoming.begin(), value.incoming.end(),
                    [&](const PhiIncoming& edge) {
                        return edge.predecessor == merge.id;
                    });
                if (incoming == value.incoming.end()) continue;
                const auto merged_value = incoming->value;
                value.incoming.erase(incoming);
                for (const auto predecessor : predecessors) {
                    auto edge_value = merged_value;
                    const auto& merged =
                        function.values[merged_value.value];
                    if (merged.kind == ValueKind::Phi) {
                        const auto source = std::find_if(
                            merged.incoming.begin(), merged.incoming.end(),
                            [&](const PhiIncoming& edge) {
                                return edge.predecessor == predecessor;
                            });
                        if (source != merged.incoming.end()) {
                            edge_value = source->value;
                        }
                    }
                    value.incoming.push_back(
                        {predecessor, edge_value});
                }
            }
        }
        eliminate_unreachable_blocks(function);
        return true;
    }
    return false;
}

bool thread_boolean_phi_branches(ManagedFunction& function) {
    bool changed = false;
    while (thread_boolean_phi_branch_once(function)) changed = true;
    return changed;
}

bool if_conversion_scalar_type(const hir::Module& hir_module,
                               hir::TypeId type) {
    const auto bits = type_bits(hir_module, type);
    return bits != 0 && bits <= 64 &&
           (integer_type(hir_module, type) ||
            pointer_type(hir_module, type));
}

bool safely_speculatable_for_if_conversion(
    const ManagedValue& value, const ManagedFunction& function,
    const hir::Module& hir_module) {
    if (is_effectful_value(value) ||
        !if_conversion_scalar_type(hir_module, value.type)) {
        return false;
    }
    for (const auto operand : value.operands) {
        if (operand.value >= function.values.size() ||
            !if_conversion_scalar_type(
                hir_module, function.values[operand.value].type)) {
            return false;
        }
    }
    switch (value.kind) {
    case ValueKind::ConstantInteger:
    case ValueKind::SlotAddress:
    case ValueKind::GlobalAddress:
        return true;
    case ValueKind::Unary:
        return value.operands.size() == 1;
    case ValueKind::Binary:
        if (value.operands.size() != 2) return false;
        switch (value.binary) {
        case BinaryOperation::SignedDivide:
        case BinaryOperation::UnsignedDivide:
        case BinaryOperation::SignedRemainder:
        case BinaryOperation::UnsignedRemainder:
            return false;
        default: return true;
        }
    case ValueKind::Cast:
        return value.operands.size() == 1;
    case ValueKind::Intrinsic:
        return value.intrinsic == IntrinsicOperation::Expect &&
               value.operands.size() == 1;
    default:
        return false;
    }
}

unsigned if_conversion_cost(const ManagedValue& value,
                            const hir::Module& hir_module,
                            const CompilerOptions& options) {
    constexpr unsigned cost_scale = 100;
    const auto blended = [&](unsigned risc, unsigned cisc) {
        const auto balance = std::min(options.risc_cisc_balance, 100U);
        return risc * (100U - balance) + cisc * balance;
    };
    switch (value.kind) {
    case ValueKind::Intrinsic:
        // These normally disappear into an immediate/addressing mode or are
        // pure metadata. Counting every MIR node equally used to make the
        // decision depend on front-end spelling instead of generated work.
        return 0;
    case ValueKind::ConstantInteger:
        // A CISC target commonly absorbs the literal into its consumer;
        // load/store RISCs more often need an explicit materialization.
        return blended(1U, 0U);
    case ValueKind::SlotAddress:
    case ValueKind::GlobalAddress:
        return blended(2U, 1U);
    case ValueKind::Cast:
        return value.cast == CastOperation::Reinterpret ? 0U : cost_scale;
    case ValueKind::Binary:
        if (value.binary == BinaryOperation::Multiply) {
            return blended(4U, 3U);
        }
        if (value.binary == BinaryOperation::ShiftLeft ||
            value.binary == BinaryOperation::ShiftRightArithmetic ||
            value.binary == BinaryOperation::ShiftRightLogical ||
            value.binary == BinaryOperation::RotateLeft ||
            value.binary == BinaryOperation::RotateRight) {
            return blended(1U, 2U);
        }
        return cost_scale;
    case ValueKind::Unary:
        return cost_scale;
    default:
        return std::max(1U, type_bits(hir_module, value.type) / 64U) *
               cost_scale;
    }
}

bool value_depends_on_ordinary_memory(
    const ManagedFunction& function, ValueId id,
    std::unordered_set<std::uint32_t>& visited) {
    if (id.value >= function.values.size() ||
        !visited.insert(id.value).second) {
        return false;
    }
    const auto& value = function.values[id.value];
    if (!value.is_volatile_access &&
        (value.kind == ValueKind::Load ||
         value.kind == ValueKind::PointerLoad ||
         value.kind == ValueKind::IndexedLoad ||
         value.kind == ValueKind::GlobalLoad)) {
        return true;
    }
    if (is_effectful_value(value) || value.kind == ValueKind::Call ||
        value.kind == ValueKind::PatchValue) {
        return false;
    }
    return std::any_of(
        value.operands.begin(), value.operands.end(),
        [&](ValueId operand) {
            return value_depends_on_ordinary_memory(
                function, operand, visited);
        });
}

bool unpredictable_memory_condition(const ManagedFunction& function,
                                     const hir::Module& hir_module,
                                     ValueId condition) {
    if (condition.value >= function.values.size()) return false;
    const auto& comparison = function.values[condition.value];
    if (comparison.kind != ValueKind::Binary ||
        comparison.operands.size() != 2) {
        return false;
    }
    // Unsigned comparison with the top-bit boundary is exactly a test of one
    // data bit.  Treat either polarity like a masked memory predicate: for
    // unprofiled data it has the same high-entropy character, while arbitrary
    // range comparisons (including binary-search bounds) remain ordinary.
    const auto top_bit_boundary = [&](ValueId literal, ValueId data) {
        if (literal.value >= function.values.size() ||
            data.value >= function.values.size()) {
            return false;
        }
        const auto& constant = function.values[literal.value];
        const auto bits = type_bits(
            hir_module, function.values[data.value].type);
        if (constant.kind != ValueKind::ConstantInteger ||
            constant.integer_high != 0 || bits == 0 || bits > 64 ||
            constant.integer != (std::uint64_t{1} << (bits - 1U))) {
            return false;
        }
        std::unordered_set<std::uint32_t> visited;
        return value_depends_on_ordinary_memory(
            function, data, visited);
    };
    switch (comparison.binary) {
    case BinaryOperation::UnsignedLess:
    case BinaryOperation::UnsignedGreaterEqual:
        if (top_bit_boundary(
                comparison.operands[1], comparison.operands[0])) {
            return true;
        }
        break;
    case BinaryOperation::UnsignedGreater:
    case BinaryOperation::UnsignedLessEqual:
        if (top_bit_boundary(
                comparison.operands[0], comparison.operands[1])) {
            return true;
        }
        break;
    default: break;
    }
    if (comparison.binary != BinaryOperation::Equal &&
        comparison.binary != BinaryOperation::NotEqual) {
        return false;
    }
    const auto is_zero = [&](ValueId id) {
        const auto& value = function.values[id.value];
        return value.kind == ValueKind::ConstantInteger &&
               value.integer == 0 && value.integer_high == 0;
    };
    std::optional<ValueId> masked;
    if (is_zero(comparison.operands[0])) masked = comparison.operands[1];
    else if (is_zero(comparison.operands[1])) masked = comparison.operands[0];
    if (!masked || masked->value >= function.values.size()) return false;
    const auto& bit_and = function.values[masked->value];
    if (bit_and.kind != ValueKind::Binary ||
        bit_and.binary != BinaryOperation::BitAnd ||
        bit_and.operands.size() != 2) {
        return false;
    }
    const auto is_nonzero_constant = [&](ValueId id) {
        const auto& value = function.values[id.value];
        return value.kind == ValueKind::ConstantInteger &&
               (value.integer != 0 || value.integer_high != 0);
    };
    std::optional<ValueId> data;
    if (is_nonzero_constant(bit_and.operands[0])) data = bit_and.operands[1];
    else if (is_nonzero_constant(bit_and.operands[1])) data = bit_and.operands[0];
    if (!data) return false;
    std::unordered_set<std::uint32_t> visited;
    return value_depends_on_ordinary_memory(function, *data, visited);
}

// Converts the diamond owned by OWNER_INDEX. Disconnected arms stay in place
// as unreachable blocks, and effects to be replaced are recorded in FORWARDED
// (from -> to); if_convert_diamonds applies both once at the end.
bool if_convert_one_diamond(ManagedFunction& function,
                            const hir::Module& hir_module,
                            std::size_t owner_index,
                            const CompilerOptions& options,
                            std::unordered_map<std::uint32_t, EffectId>& forwarded) {
    auto& owner = function.blocks[owner_index];
    if (owner.terminator.kind != TerminatorKind::ConditionalBranch ||
        !owner.terminator.value ||
        owner.terminator.successors.size() != 2 ||
        owner.terminator.successors[0] == owner.terminator.successors[1]) {
        return false;
    }
    const auto condition = *owner.terminator.value;
    if (condition.value >= function.values.size() ||
        !integer_type(hir_module, function.values[condition.value].type)) {
        return false;
    }

    // Keep the final diamond of a dense `selector == 0`, `== 1`, ... ladder
    // as control flow.  Target lowering can then form one complete jump table
    // instead of seeing a shorter table followed by a select that computes
    // both remaining arms.  Ordinary isolated equality diamonds retain the
    // normal if-conversion profitability decision.
    const auto equality_test = [&](ValueId id)
        -> std::optional<std::pair<ValueId, std::uint64_t>> {
        if (id.value >= function.values.size()) return std::nullopt;
        const auto& comparison = function.values[id.value];
        if (comparison.kind != ValueKind::Binary ||
            comparison.binary != BinaryOperation::Equal ||
            comparison.operands.size() != 2) {
            return std::nullopt;
        }
        const auto match = [&](ValueId selector, ValueId literal)
            -> std::optional<std::pair<ValueId, std::uint64_t>> {
            if (literal.value >= function.values.size()) return std::nullopt;
            const auto& constant = function.values[literal.value];
            if (constant.kind != ValueKind::ConstantInteger ||
                constant.integer_high != 0) {
                return std::nullopt;
            }
            return std::pair{selector, constant.integer};
        };
        if (const auto result =
                match(comparison.operands[0], comparison.operands[1])) {
            return result;
        }
        return match(comparison.operands[1], comparison.operands[0]);
    };
    const auto dense_equality_tail = [&] {
        const auto final = equality_test(condition);
        if (!final) return false;
        auto selector = final->first;
        auto expected = final->second;
        auto current = owner.id;
        std::size_t length = 1;
        while (expected != 0) {
            const auto& block = function.blocks[current.value];
            if (block.predecessors.size() != 1) break;
            const auto predecessor_id = block.predecessors.front();
            const auto& predecessor =
                function.blocks[predecessor_id.value];
            if (predecessor.terminator.kind !=
                    TerminatorKind::ConditionalBranch ||
                !predecessor.terminator.value ||
                predecessor.terminator.successors.size() != 2 ||
                predecessor.terminator.successors[1] != current) {
                break;
            }
            const auto prior = equality_test(
                *predecessor.terminator.value);
            if (!prior || prior->first != selector ||
                expected == 0 || prior->second != expected - 1) {
                break;
            }
            expected = prior->second;
            current = predecessor_id;
            ++length;
        }
        return expected == 0 && length >= 4;
    };
    if (options.jump_tables && dense_equality_tail()) {
        return false;
    }

    const auto truth_id = owner.terminator.successors[0];
    const auto falsity_id = owner.terminator.successors[1];
    auto& truth = function.blocks[truth_id.value];
    auto& falsity = function.blocks[falsity_id.value];
    const auto pure_arm = [&](const ManagedBlock& arm) {
        return arm.predecessors.size() == 1 &&
               arm.predecessors.front() == owner.id &&
               std::none_of(
                   function.labels.begin(), function.labels.end(),
                   [&](const ManagedLabel& label) {
                       return label.block == arm.id;
                   }) &&
               std::all_of(
                   arm.values.begin(), arm.values.end(),
                   [&](ValueId id) {
                       return safely_speculatable_for_if_conversion(
                           function.values[id.value], function, hir_module);
                   });
    };
    if (!pure_arm(truth) || !pure_arm(falsity)) return false;

    constexpr unsigned cost_scale = 100;
    const auto profitable = [&](unsigned cost) {
        unsigned limit = options.if_conversion_limit * cost_scale;
        // A low-bit test of loaded data commonly has little branch
        // predictability. Once allocation and if-conversion are enabled,
        // tolerate a few more cheap, nontrapping integer operations to replace
        // its misprediction with a select. Other diamonds retain the ordinary
        // profile-sensitive threshold because simultaneously live arm values
        // can cost more save/restore code than CMOV removes.
        if (unpredictable_memory_condition(
                function, hir_module, condition)) {
            limit = std::max(
                limit,
                options.if_conversion_memory_limit * cost_scale);
        }
        return cost <= limit;
    };
    const auto arm_cost = [&](const ManagedBlock& arm) {
        unsigned result{};
        for (const auto id : arm.values) {
            result += if_conversion_cost(
                function.values[id.value], hir_module, options);
        }
        return result;
    };
    const auto disconnect = [&](ManagedBlock& arm) {
        arm.predecessors.clear();
        function.effects[arm.effect.value].incoming.clear();
        arm.terminator.kind = TerminatorKind::Unreachable;
        arm.terminator.value.reset();
        arm.terminator.successors.clear();
        arm.terminator.effect = arm.effect;
    };

    if (truth.terminator.kind == TerminatorKind::Return &&
        falsity.terminator.kind == TerminatorKind::Return &&
        truth.terminator.successors.empty() &&
        falsity.terminator.successors.empty()) {
        if (truth.terminator.value.has_value() !=
            falsity.terminator.value.has_value()) {
            return false;
        }
        std::optional<ValueId> selected;
        if (truth.terminator.value) {
            const auto truth_value = *truth.terminator.value;
            const auto falsity_value = *falsity.terminator.value;
            const auto type = function.values[truth_value.value].type;
            if (function.values[falsity_value.value].type != type ||
                !if_conversion_scalar_type(hir_module, type) ||
                !profitable(arm_cost(truth) + arm_cost(falsity) +
                            cost_scale)) {
                return false;
            }
            ManagedValue value;
            value.id = {static_cast<std::uint32_t>(
                function.values.size())};
            value.location = owner.terminator.location;
            value.type = type;
            value.kind = ValueKind::Select;
            value.operands = {condition, truth_value, falsity_value};
            selected = value.id;
            function.values.push_back(std::move(value));
        }

        if (selected) {
            owner.values.insert(owner.values.end(), truth.values.begin(),
                                truth.values.end());
            owner.values.insert(owner.values.end(), falsity.values.begin(),
                                falsity.values.end());
            owner.values.push_back(*selected);
        }
        truth.values.clear();
        falsity.values.clear();
        owner.terminator.kind = TerminatorKind::Return;
        owner.terminator.value = selected;
        owner.terminator.successors.clear();
        disconnect(truth);
        disconnect(falsity);
        return true;
    }

    if (truth.terminator.kind != TerminatorKind::Branch ||
        falsity.terminator.kind != TerminatorKind::Branch ||
        truth.terminator.successors.size() != 1 ||
        falsity.terminator.successors.size() != 1 ||
        truth.terminator.successors.front() !=
            falsity.terminator.successors.front()) {
        return false;
    }

    const auto merge_id = truth.terminator.successors.front();
    if (merge_id == owner.id || merge_id == truth_id ||
        merge_id == falsity_id) {
        return false;
    }
    auto& merge = function.blocks[merge_id.value];
    if (merge.predecessors.size() != 2 ||
        std::find(merge.predecessors.begin(), merge.predecessors.end(),
                  truth_id) == merge.predecessors.end() ||
        std::find(merge.predecessors.begin(), merge.predecessors.end(),
                  falsity_id) == merge.predecessors.end()) {
        return false;
    }

    struct Selection {
        ValueId result;
        ValueId truth;
        ValueId falsity;
    };
    std::vector<Selection> selections;
    for (const auto id : merge.values) {
        const auto& value = function.values[id.value];
        if (value.kind != ValueKind::Phi) continue;
        if (!if_conversion_scalar_type(hir_module, value.type) ||
            value.incoming.size() != 2) {
            return false;
        }
        const auto truth_value = std::find_if(
            value.incoming.begin(), value.incoming.end(),
            [&](const PhiIncoming& incoming) {
                return incoming.predecessor == truth_id;
            });
        const auto falsity_value = std::find_if(
            value.incoming.begin(), value.incoming.end(),
            [&](const PhiIncoming& incoming) {
                return incoming.predecessor == falsity_id;
            });
        if (truth_value == value.incoming.end() ||
            falsity_value == value.incoming.end()) {
            return false;
        }
        selections.push_back(
            {id, truth_value->value, falsity_value->value});
    }
    if (selections.empty()) return false;

    const auto speculative_cost =
        arm_cost(truth) + arm_cost(falsity) +
        static_cast<unsigned>(selections.size()) * cost_scale;
    if (!profitable(speculative_cost)) return false;

    const auto truth_values = truth.values;
    const auto falsity_values = falsity.values;
    owner.values.insert(owner.values.end(), truth_values.begin(),
                        truth_values.end());
    owner.values.insert(owner.values.end(), falsity_values.begin(),
                        falsity_values.end());
    std::unordered_set<std::uint32_t> selected_ids;
    for (const auto& selection : selections) {
        auto& value = function.values[selection.result.value];
        value.kind = ValueKind::Select;
        value.operands = {condition, selection.truth, selection.falsity};
        value.incoming.clear();
        owner.values.push_back(selection.result);
        selected_ids.insert(selection.result.value);
    }
    std::erase_if(merge.values, [&](ValueId id) {
        return selected_ids.contains(id.value);
    });
    truth.values.clear();
    falsity.values.clear();

    owner.terminator.kind = TerminatorKind::Branch;
    owner.terminator.value.reset();
    owner.terminator.successors = {merge_id};
    merge.predecessors = {owner.id};
    auto& merge_effect = function.effects[merge.effect.value];
    merge_effect.incoming = {
        {owner.id, owner.terminator.effect}};

    disconnect(truth);
    disconnect(falsity);

    // The converted diamond leaves OWNER branching to a single-predecessor
    // merge. Fold that linear edge immediately. Besides removing a redundant
    // jump, this keeps chains of source-level `if` statements in one MIR loop
    // body so the loop vectorizer can see nested selects as a single packed
    // expression.
    const bool merge_has_label = std::any_of(
        function.labels.begin(), function.labels.end(),
        [&](const ManagedLabel& label) {
            return label.block == merge_id;
        });
    const bool merge_has_phi = std::any_of(
        merge.values.begin(), merge.values.end(),
        [&](ValueId id) {
            return function.values[id.value].kind == ValueKind::Phi;
        });
    if (!merge_has_label && !merge_has_phi &&
        merge.predecessors.size() == 1 &&
        merge.predecessors.front() == owner.id) {
        forwarded[merge.effect.value] = owner.terminator.effect;
        owner.values.insert(owner.values.end(), merge.values.begin(),
                            merge.values.end());
        merge.values.clear();
        owner.terminator = merge.terminator;
        for (const auto successor_id : owner.terminator.successors) {
            auto& successor = function.blocks[successor_id.value];
            std::replace(successor.predecessors.begin(),
                         successor.predecessors.end(), merge_id, owner.id);
            auto& successor_effect =
                function.effects[successor.effect.value];
            for (auto& incoming : successor_effect.incoming) {
                if (incoming.predecessor == merge_id) {
                    incoming.predecessor = owner.id;
                }
            }
            for (const auto id : successor.values) {
                auto& value = function.values[id.value];
                if (value.kind != ValueKind::Phi) continue;
                for (auto& incoming : value.incoming) {
                    if (incoming.predecessor == merge_id) {
                        incoming.predecessor = owner.id;
                    }
                }
            }
        }
        disconnect(merge);
    }
    return true;
}

bool if_convert_diamonds(ManagedFunction& function,
                         const hir::Module& hir_module,
                         const CompilerOptions& options) {
    // Converting a diamond can expose another one at the same owner (its
    // absorbed merge ends in a branch) or at an earlier block (an enclosing
    // diamond whose arm was this one), so sweep until nothing changes.
    std::unordered_map<std::uint32_t, EffectId> forwarded;
    bool changed = false;
    for (bool converted = true; converted;) {
        converted = false;
        for (std::size_t index = 0; index < function.blocks.size(); ++index) {
            while (if_convert_one_diamond(function, hir_module, index, options,
                                          forwarded))
                converted = changed = true;
        }
    }
    if (changed) {
        forward_effects(function, forwarded);
        eliminate_unreachable_blocks(function);
    }
    return changed;
}

bool safely_loop_invariant(const ManagedValue& value,
                           const ManagedFunction& function,
                           const hir::Module& hir_module,
                           const Subtarget& subtarget) {
    if (value.effect_input || value.effect_output ||
        value.is_volatile_access || value.patch_sink) {
        return false;
    }
    switch (value.kind) {
    // Keep immediate-like values close to their uses, but hoist constants
    // that the resolved target says are expensive to rematerialize.  The
    // target owns encoding and ISA-feature details; MIR owns LICM policy.
    case ValueKind::ConstantInteger: {
        const auto bits = type_bits(hir_module, value.type);
        return subtarget.integer_constant_materialization_cost(
                   {bits, value.integer, value.integer_high,
                    signed_type(hir_module, value.type)}) > 1;
    }
    // A nonzero f32/f64 literal otherwise costs a GPR materialization plus a
    // transfer to SIMD on every trip. Hoist it once; zero remains a cheap
    // xor-zero idiom and wide formats retain their addressable loop home.
    case ValueKind::ConstantFloating: {
        const auto bits = type_bits(hir_module, value.type);
        return (bits == 32 || bits == 64) &&
               !floating_zero(value, bits);
    }
    case ValueKind::LabelAddress:
    case ValueKind::SlotAddress:
    case ValueKind::GlobalAddress:
        return true;
    case ValueKind::IndexedAddress:
        return true;
    case ValueKind::Unary:
        return !floating_type(hir_module, value.type);
    case ValueKind::Binary:
        if (value.binary == BinaryOperation::SignedDivide ||
            value.binary == BinaryOperation::UnsignedDivide ||
            value.binary == BinaryOperation::SignedRemainder ||
            value.binary == BinaryOperation::UnsignedRemainder) {
            return false;
        }
        return value.operands.empty() ||
               !floating_type(
                   hir_module,
                   function.values[value.operands.front().value].type);
    case ValueKind::Cast:
        return value.cast == CastOperation::SignExtend ||
               value.cast == CastOperation::ZeroExtend ||
               value.cast == CastOperation::Truncate ||
               value.cast == CastOperation::Reinterpret;
    case ValueKind::Splat:
    case ValueKind::InsertElement:
        return true;
    case ValueKind::Intrinsic:
        return value.intrinsic == IntrinsicOperation::Expect;
    default:
        return false;
    }
}

bool supports_loop_invariant(const ManagedValue& value,
                             const ManagedFunction& function,
                             const hir::Module& hir_module,
                             const Subtarget& subtarget) {
    if (safely_loop_invariant(
            value, function, hir_module, subtarget)) return true;
    // Small integer literals are deliberately rematerialized in ordinary
    // loop bodies, but they must still participate in the invariant closure.
    // Otherwise a cheap literal such as the mask in `bound & -16` pins the
    // entire derived expression inside an enclosing loop. Only move such a
    // literal when a genuinely hoistable expression depends on it.
    return value.kind == ValueKind::ConstantInteger &&
           !value.effect_input && !value.effect_output &&
           !value.is_volatile_access && !value.patch_sink;
}

void move_loop_invariants(ManagedFunction& function,
                          const hir::Module& hir_module,
                          const Subtarget& subtarget,
                          std::span<const CanonicalLoop> loops) {
    if (function.blocks.size() < 2) return;
    std::vector<std::optional<BlockId>> definition_block(
        function.values.size());
    for (const auto& block : function.blocks) {
        for (const auto value : block.values) {
            definition_block[value.value] = block.id;
        }
    }
    for (const auto& loop : loops) {
        std::unordered_set<std::uint32_t> invariant;
        std::vector<ValueId> moved;
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto block_id : loop.blocks) {
                const auto& block = function.blocks[block_id];
                for (const auto id : block.values) {
                    if (invariant.contains(id.value)) continue;
                    const auto& value = function.values[id.value];
                    if (!supports_loop_invariant(
                            value, function, hir_module, subtarget)) {
                        continue;
                    }
                    const bool operands_invariant = std::all_of(
                        value.operands.begin(), value.operands.end(),
                        [&](ValueId operand) {
                            if (!definition_block[operand.value]) {
                                return false;
                            }
                            return !loop.blocks.contains(
                                       definition_block[operand.value]
                                           ->value) ||
                                   invariant.contains(operand.value);
                        });
                    if (!operands_invariant) continue;
                    invariant.insert(id.value);
                    moved.push_back(id);
                    changed = true;
                }
            }
        }
        // Support-only literals are not roots: retain ordinary loop
        // rematerialization unless a profitable invariant expression needs
        // them. Walk operands backwards from the real roots to select the
        // minimal dependency subset to move.
        std::unordered_set<std::uint32_t> required;
        std::vector<ValueId> pending;
        for (const auto id : moved) {
            if (safely_loop_invariant(
                    function.values[id.value], function, hir_module,
                    subtarget)) {
                required.insert(id.value);
                pending.push_back(id);
            }
        }
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            for (const auto operand : function.values[id.value].operands) {
                if (invariant.contains(operand.value) &&
                    required.insert(operand.value).second) {
                    pending.push_back(operand);
                }
            }
        }
        std::erase_if(moved, [&](ValueId id) {
            return !required.contains(id.value);
        });
        if (moved.empty()) continue;
        for (const auto block_id : loop.blocks) {
            auto& values = function.blocks[block_id].values;
            std::erase_if(values, [&](ValueId id) {
                return required.contains(id.value);
            });
        }
        auto& preheader = function.blocks[loop.preheader.value].values;
        preheader.insert(preheader.end(), moved.begin(), moved.end());
        for (const auto id : moved) {
            definition_block[id.value] = loop.preheader;
        }
    }
}

struct AffineLoopValue {
    ValueId phi;
    ValueId initial;
    ValueId next;
    ValueId step;
};

struct VectorLoopStore {
    ValueId store;
    ValueId address;
    ValueId base;
    ValueId value;
};

struct ReductionLoopPattern {
    CanonicalLoop loop;
    BlockId body;
    BlockId exit;
    ValueId index;
    ValueId initial_index;
    ValueId bound;
    ValueId reduction;
    ValueId initial_reduction;
    ValueId term;
    ValueId index_next;
    ValueId reduction_next;
    BinaryOperation reduction_operation{BinaryOperation::Add};
    std::vector<AffineLoopValue> affine_values;
    std::vector<VectorLoopStore> stores;
    std::vector<ValueId> load_bases;
};

std::optional<ValueId> phi_value_from(const ManagedValue& phi,
                                      BlockId predecessor) {
    const auto found = std::find_if(
        phi.incoming.begin(), phi.incoming.end(),
        [&](const PhiIncoming& incoming) {
            return incoming.predecessor == predecessor;
        });
    if (found == phi.incoming.end()) return std::nullopt;
    return found->value;
}

std::vector<std::optional<BlockId>> value_definition_blocks(
    const ManagedFunction& function) {
    std::vector<std::optional<BlockId>> result(function.values.size());
    for (const auto& block : function.blocks) {
        for (const auto value : block.values) {
            result[value.value] = block.id;
        }
    }
    return result;
}

bool select_unit_induction_exits(
    ManagedFunction& function, const hir::Module& hir_module,
    std::span<const CanonicalLoop> loops) {
    const auto definition_blocks = value_definition_blocks(function);
    bool changed = false;
    for (const auto& loop : loops) {
        if (loop.header.value >= function.blocks.size()) continue;
        auto& header = function.blocks[loop.header.value];
        if (!header.terminator.value ||
            header.terminator.value->value >= function.values.size()) {
            continue;
        }
        const auto condition_id = *header.terminator.value;
        const auto& condition = function.values[condition_id.value];
        if (condition.kind != ValueKind::Binary ||
            condition.binary != BinaryOperation::UnsignedLess ||
            condition.operands.size() != 2) {
            continue;
        }
        const auto induction_id = condition.operands[0];
        const auto bound_id = condition.operands[1];
        if (induction_id.value >= function.values.size() ||
            bound_id.value >= definition_blocks.size() ||
            (definition_blocks[bound_id.value] &&
             loop.blocks.contains(
                 definition_blocks[bound_id.value]->value))) {
            continue;
        }
        const auto& induction = function.values[induction_id.value];
        if (induction.kind != ValueKind::Phi ||
            !unsigned_integer_type(hir_module, induction.type) ||
            induction.incoming.size() != 2) {
            continue;
        }
        const auto initial = phi_value_from(induction, loop.preheader);
        const auto carried = std::find_if(
            induction.incoming.begin(), induction.incoming.end(),
            [&](const PhiIncoming& incoming) {
                return incoming.predecessor != loop.preheader &&
                    loop.blocks.contains(incoming.predecessor.value);
            });
        if (!initial || carried == induction.incoming.end() ||
            initial->value >= function.values.size() ||
            carried->value.value >= function.values.size()) {
            continue;
        }
        const auto& zero = function.values[initial->value];
        const auto& update = function.values[carried->value.value];
        if (zero.kind != ValueKind::ConstantInteger || zero.integer != 0 ||
            zero.integer_high != 0 || update.kind != ValueKind::Binary ||
            update.binary != BinaryOperation::Add ||
            update.operands.size() != 2) {
            continue;
        }
        std::optional<ValueId> step_id;
        if (update.operands[0] == induction_id) {
            step_id = update.operands[1];
        } else if (update.operands[1] == induction_id) {
            step_id = update.operands[0];
        }
        if (!step_id || step_id->value >= function.values.size()) continue;
        const auto& step = function.values[step_id->value];
        if (step.kind != ValueKind::ConstantInteger || step.integer != 1 ||
            step.integer_high != 0) {
            continue;
        }

        ManagedValue selected = condition;
        selected.id = ValueId{static_cast<std::uint32_t>(
            function.values.size())};
        selected.binary = BinaryOperation::NotEqual;
        const auto selected_id = selected.id;
        function.values.push_back(std::move(selected));
        header.values.push_back(selected_id);
        header.terminator.value = selected_id;
        changed = true;
    }
    return changed;
}

void coalesce_equivalent_inductions(ManagedFunction& function,
                                    const hir::Module& hir_module,
                                    std::span<const CanonicalLoop> loops,
                                    bool compact = true,
                                    bool derive_scaled = false) {
    struct Recurrence {
        ValueId phi;
        ValueId update;
        UInt128 initial;
        UInt128 step;
        hir::TypeId type;
    };
    std::unordered_set<std::uint32_t> removed;
    const auto definition_blocks = value_definition_blocks(function);
    for (const auto& loop : loops) {
        auto& header = function.blocks[loop.header.value];
        if (header.predecessors.size() != 2) continue;
        const auto backedge = header.predecessors[0] == loop.preheader
            ? header.predecessors[1] : header.predecessors[0];
        std::unordered_set<std::uint32_t> condition_operands;
        if (header.terminator.value) {
            for (const auto operand :
                 function.values[header.terminator.value->value].operands) {
                condition_operands.insert(operand.value);
            }
        }
        auto ordered_values = header.values;
        std::stable_sort(
            ordered_values.begin(), ordered_values.end(),
            [&](ValueId left, ValueId right) {
                return condition_operands.contains(left.value) &&
                       !condition_operands.contains(right.value);
            });
        std::vector<Recurrence> recurrences;
        bool converted = false;
        for (const auto id : ordered_values) {
            auto& phi = function.values[id.value];
            if (phi.kind != ValueKind::Phi ||
                !integer_type(hir_module, phi.type)) {
                continue;
            }
            const auto initial_id = phi_value_from(phi, loop.preheader);
            const auto update_id = phi_value_from(phi, backedge);
            if (!initial_id || !update_id) continue;
            const auto& initial = function.values[initial_id->value];
            const auto& update = function.values[update_id->value];
            if (initial.kind != ValueKind::ConstantInteger ||
                update.kind != ValueKind::Binary ||
                update.binary != BinaryOperation::Add ||
                update.operands.size() != 2) {
                continue;
            }
            std::optional<ValueId> step_id;
            if (update.operands[0] == id) step_id = update.operands[1];
            else if (update.operands[1] == id) {
                step_id = update.operands[0];
            }
            if (!step_id) continue;
            const auto& step = function.values[step_id->value];
            if (step.kind != ValueKind::ConstantInteger) continue;

            unsigned update_uses{};
            for (const auto& value : function.values) {
                update_uses += static_cast<unsigned>(std::count(
                    value.operands.begin(), value.operands.end(),
                    *update_id));
                update_uses += static_cast<unsigned>(std::count_if(
                    value.incoming.begin(), value.incoming.end(),
                    [&](const PhiIncoming& incoming) {
                        return incoming.value == *update_id;
                    }));
            }
            for (const auto& block : function.blocks) {
                if (block.terminator.value == *update_id) ++update_uses;
            }
            if (update_uses != 1) continue;

            const Recurrence candidate{
                id, *update_id,
                {initial.integer, initial.integer_high},
                {step.integer, step.integer_high}, phi.type};
            const auto equivalent = std::find_if(
                recurrences.begin(), recurrences.end(),
                [&](const Recurrence& prior) {
                    return prior.initial == candidate.initial &&
                           prior.step == candidate.step &&
                           type_bits(hir_module, prior.type) ==
                               type_bits(hir_module, candidate.type);
                });
            if (equivalent == recurrences.end()) {
                const auto offset = std::find_if(
                    recurrences.begin(), recurrences.end(),
                    [&](const Recurrence& prior) {
                        const auto bits =
                            type_bits(hir_module, candidate.type);
                        return derive_scaled &&
                            prior.initial == UInt128{} &&
                            candidate.initial == candidate.step &&
                            prior.step == candidate.step &&
                            candidate.initial == mask_to(
                                add(prior.initial, prior.step), bits) &&
                            type_bits(hir_module, prior.type) == bits;
                    });
                if (offset != recurrences.end()) {
                    const auto update_block =
                        definition_blocks[offset->update.value];
                    bool local_uses = update_block.has_value();
                    std::optional<std::size_t> first_use;
                    if (local_uses) {
                        for (const auto& use_block : function.blocks) {
                            for (std::size_t position = 0;
                                 position < use_block.values.size();
                                 ++position) {
                                const auto user_id =
                                    use_block.values[position];
                                if (user_id == candidate.phi ||
                                    user_id == candidate.update) {
                                    continue;
                                }
                                const auto& user =
                                    function.values[user_id.value];
                                const bool uses_candidate =
                                    std::find(user.operands.begin(),
                                              user.operands.end(),
                                              candidate.phi) !=
                                        user.operands.end() ||
                                    std::any_of(
                                        user.call_arguments.begin(),
                                        user.call_arguments.end(),
                                        [&](const CallArgument& argument) {
                                            return argument.value ==
                                                candidate.phi;
                                        }) ||
                                    std::any_of(
                                        user.incoming.begin(),
                                        user.incoming.end(),
                                        [&](const PhiIncoming& incoming) {
                                            return incoming.value ==
                                                candidate.phi;
                                        });
                                if (!uses_candidate) continue;
                                if (use_block.id != *update_block) {
                                    local_uses = false;
                                    break;
                                }
                                first_use = first_use
                                    ? std::min(*first_use, position)
                                    : position;
                            }
                            if (!local_uses) break;
                            if (use_block.terminator.value ==
                                candidate.phi) {
                                if (use_block.id != *update_block) {
                                    local_uses = false;
                                    break;
                                }
                                first_use = first_use
                                    ? std::min(*first_use,
                                               use_block.values.size())
                                    : use_block.values.size();
                            }
                        }
                    }
                    if (local_uses) {
                        // The one-step-ahead value is exactly the primary
                        // backedge update. Move that pure update before its
                        // local consumers and use it for both jobs. This
                        // removes a phi, an increment, and one loop-carried
                        // register without adding reconstruction work.
                        auto& primary_update =
                            function.values[offset->update.value];
                        for (auto& operand : primary_update.operands) {
                            if (operand != offset->phi) {
                                operand = *initial_id;
                            }
                        }
                        auto& values =
                            function.blocks[update_block->value].values;
                        std::erase(values, offset->update);
                        auto insertion = values.end();
                        if (first_use) {
                            insertion = std::find_if(
                                values.begin(), values.end(),
                                [&](ValueId user_id) {
                                    const auto& user =
                                        function.values[user_id.value];
                                    return std::find(
                                               user.operands.begin(),
                                               user.operands.end(),
                                               candidate.phi) !=
                                               user.operands.end() ||
                                        std::any_of(
                                            user.call_arguments.begin(),
                                            user.call_arguments.end(),
                                            [&](const CallArgument& argument) {
                                                return argument.value ==
                                                    candidate.phi;
                                            });
                                });
                        }
                        values.insert(insertion, offset->update);
                        replace_value_uses(function, candidate.phi,
                                           offset->update);
                        removed.insert(candidate.phi.value);
                        removed.insert(candidate.update.value);
                        converted = true;
                        continue;
                    }
                }
                const auto unit = std::find_if(
                    recurrences.begin(), recurrences.end(),
                    [&](const Recurrence& prior) {
                        return derive_scaled &&
                            candidate.initial == UInt128{} &&
                            prior.initial == UInt128{} &&
                            prior.step == UInt128{1} &&
                            candidate.step != UInt128{1} &&
                            type_bits(hir_module, prior.type) ==
                                type_bits(hir_module, candidate.type);
                    });
                if (unit != recurrences.end()) {
                    // In size mode, materialize a scaled secondary induction
                    // from the loop's unit-step counter. One multiply per
                    // outer trip is normally smaller than another phi,
                    // update, edge copy, and long-lived register or spill.
                    phi.kind = ValueKind::Binary;
                    phi.binary = BinaryOperation::Multiply;
                    phi.operands = {unit->phi, *step_id};
                    phi.incoming.clear();
                    removed.insert(candidate.update.value);
                    converted = true;
                    continue;
                }
                recurrences.push_back(candidate);
                continue;
            }

            // Preserve the duplicate's source type as a zero-cost SSA cast.
            // Its uses keep their original signedness/type, while the target
            // allocator can coalesce both representations into one register.
            phi.kind = ValueKind::Cast;
            phi.cast = CastOperation::Reinterpret;
            phi.operands = {equivalent->phi};
            phi.incoming.clear();
            removed.insert(candidate.update.value);
            converted = true;
        }
        if (converted) {
            // Phi definitions are edge values and may be freely ordered at
            // the start of their block. Keep every new cast after all phis so
            // coalescing a condition induction never creates a forward use.
            std::stable_partition(
                header.values.begin(), header.values.end(),
                [&](ValueId id) {
                    return function.values[id.value].kind ==
                           ValueKind::Phi;
                });
        }
    }
    if (compact) {
        remove_replaced_values(function, removed);
    } else if (!removed.empty()) {
        // Loop transforms can leave unreachable values in the sparse SSA
        // table. Compacting here would require every such stale reference to
        // have a remap entry. The final DCE pass already computes reachability
        // and compacts safely, so only detach the now-dead updates here.
        for (auto& block : function.blocks) {
            std::erase_if(block.values, [&](ValueId value) {
                return removed.contains(value.value);
            });
        }
    }
}

bool reduce_affine_address_inductions(
    ManagedFunction& function, const hir::Module& hir_module,
    const TargetInfo& target, const CompilerOptions& options,
    std::span<const CanonicalLoop> input_loops) {
    struct Access {
        ValueId load;
        BlockId block;
        std::uint64_t element_offset{};
    };
    struct Group {
        ValueId base;
        hir::TypeId pointer_type;
        hir::TypeId index_type;
        std::uint64_t element_size{};
        std::vector<Access> accesses;
    };

    bool changed = false;
    auto loops = std::vector<CanonicalLoop>(input_loops.begin(),
                                             input_loops.end());
    std::stable_sort(loops.begin(), loops.end(),
                     [](const CanonicalLoop& left,
                        const CanonicalLoop& right) {
                         return left.blocks.size() < right.blocks.size();
                     });
    const auto definition_blocks = value_definition_blocks(function);
    const auto append = [&](ManagedValue value) {
        value.id = ValueId{static_cast<std::uint32_t>(
            function.values.size())};
        const auto id = value.id;
        function.values.push_back(std::move(value));
        return id;
    };

    for (const auto& loop : loops) {
        if (loop.header.value >= function.blocks.size() ||
            loop.preheader.value >= function.blocks.size()) {
            continue;
        }
        const auto header_values =
            function.blocks[loop.header.value].values;
        for (const auto index_phi_id : header_values) {
            if (index_phi_id.value >= function.values.size()) continue;
            const auto& index_phi = function.values[index_phi_id.value];
            if (index_phi.kind != ValueKind::Phi ||
                !integer_type(hir_module, index_phi.type) ||
                index_phi.incoming.size() != 2) {
                continue;
            }
            const auto initial = std::find_if(
                index_phi.incoming.begin(), index_phi.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return incoming.predecessor == loop.preheader;
                });
            const auto carried = std::find_if(
                index_phi.incoming.begin(), index_phi.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return incoming.predecessor != loop.preheader;
                });
            if (initial == index_phi.incoming.end() ||
                carried == index_phi.incoming.end() ||
                !loop.blocks.contains(carried->predecessor.value) ||
                initial->value.value >= function.values.size() ||
                carried->value.value >= function.values.size()) {
                continue;
            }
            const auto& initial_value =
                function.values[initial->value.value];
            const auto& update = function.values[carried->value.value];
            if (initial_value.kind != ValueKind::ConstantInteger ||
                initial_value.integer != 0 ||
                initial_value.integer_high != 0 ||
                update.kind != ValueKind::Binary ||
                update.binary != BinaryOperation::Add ||
                update.operands.size() != 2) {
                continue;
            }
            std::optional<ValueId> step_id;
            if (update.operands[0] == index_phi_id) {
                step_id = update.operands[1];
            } else if (update.operands[1] == index_phi_id) {
                step_id = update.operands[0];
            }
            if (!step_id || step_id->value >= function.values.size()) {
                continue;
            }
            const auto& step = function.values[step_id->value];
            if (step.kind != ValueKind::ConstantInteger ||
                step.integer == 0 || step.integer_high != 0) {
                continue;
            }
            // The pointer recurrence equals the indexed addresses only while
            // the index does not wrap: an index as wide as an address wraps
            // with it, and signed overflow is undefined without -fwrapv.
            if (type_bits(hir_module, index_phi.type) < hir_module.address_bits &&
                (options.wrapv || !signed_type(hir_module, index_phi.type))) {
                continue;
            }
            const auto latch = carried->predecessor;
            const auto index_type = index_phi.type;
            const auto index_location = index_phi.location;
            const auto update_location = update.location;
            const auto step_elements = step.integer;

            const auto affine_offset = [&](ValueId candidate) {
                std::optional<std::uint64_t> result;
                std::unordered_set<std::uint32_t> visited;
                std::function<bool(ValueId, std::uint64_t&)> derive =
                    [&](ValueId id, std::uint64_t& offset) {
                        if (id == index_phi_id) {
                            offset = 0;
                            return true;
                        }
                        if (id.value >= function.values.size() ||
                            !visited.insert(id.value).second) {
                            return false;
                        }
                        const auto& value = function.values[id.value];
                        if (value.kind == ValueKind::Cast &&
                            value.cast == CastOperation::Reinterpret &&
                            value.operands.size() == 1 &&
                            type_bits(hir_module, value.type) ==
                        type_bits(hir_module, index_type)) {
                            return derive(value.operands.front(), offset);
                        }
                        if (value.kind != ValueKind::Binary ||
                            value.binary != BinaryOperation::Add ||
                            value.operands.size() != 2) {
                            return false;
                        }
                        for (unsigned side = 0; side < 2; ++side) {
                            const auto constant_id = value.operands[1U - side];
                            if (constant_id.value >= function.values.size()) {
                                continue;
                            }
                            const auto& constant =
                                function.values[constant_id.value];
                            if (constant.kind != ValueKind::ConstantInteger ||
                                constant.integer_high != 0) {
                                continue;
                            }
                            std::uint64_t base_offset{};
                            if (!derive(value.operands[side], base_offset) ||
                                constant.integer >
                                    std::numeric_limits<std::uint64_t>::max() -
                                        base_offset) {
                                continue;
                            }
                            offset = base_offset + constant.integer;
                            return true;
                        }
                        return false;
                    };
                std::uint64_t offset{};
                if (derive(candidate, offset)) result = offset;
                return result;
            };

            std::vector<Group> groups;
            for (const auto raw_block : loop.blocks) {
                if (raw_block >= function.blocks.size()) continue;
                const auto block_id = BlockId{raw_block};
                const auto values = function.blocks[raw_block].values;
                for (const auto id : values) {
                    if (id.value >= function.values.size()) continue;
                    const auto& load = function.values[id.value];
                    if (load.is_volatile_access) {
                        continue;
                    }
                    std::optional<ValueId> base;
                    std::optional<ValueId> index;
                    if (load.kind == ValueKind::IndexedLoad &&
                        load.operands.size() == 2) {
                        base = load.operands[0];
                        index = load.operands[1];
                    } else if (load.kind == ValueKind::PointerLoad &&
                               load.operands.size() == 1 &&
                               load.operands.front().value <
                                   function.values.size()) {
                        const auto& address =
                            function.values[load.operands.front().value];
                        if (address.kind == ValueKind::IndexedAddress &&
                            address.operands.size() == 2) {
                            base = address.operands[0];
                            index = address.operands[1];
                        }
                    }
                    if (!base || !index) continue;
                    const auto offset = affine_offset(*index);
                    if (!offset) continue;
                    if (base->value >= definition_blocks.size() ||
                        !definition_blocks[base->value] ||
                        loop.blocks.contains(
                            definition_blocks[base->value]->value)) {
                        continue;
                    }
                    const auto& base_value = function.values[base->value];
                    const auto& pointer = hir_module.type(base_value.type);
                    if (pointer.kind != hir::Type::Kind::Pointer ||
                        !pointer.pointee) {
                        continue;
                    }
                    const auto element_size =
                        storage_size(hir_module, load.type, target);
                    const auto pointee_size =
                        storage_size(hir_module, *pointer.pointee, target);
                    if (element_size == 0 || element_size != pointee_size ||
                        *offset > static_cast<std::uint64_t>(
                                      std::numeric_limits<std::int16_t>::max()) /
                                      element_size ||
                        step_elements > static_cast<std::uint64_t>(
                                           std::numeric_limits<std::int16_t>::max()) /
                                           element_size) {
                        continue;
                    }
                    auto group = std::find_if(
                        groups.begin(), groups.end(),
                        [&](const Group& candidate) {
                            return candidate.base == *base &&
                                   candidate.element_size == element_size;
                        });
                    if (group == groups.end()) {
                        groups.push_back({*base, base_value.type,
                                          index_type, element_size, {}});
                        group = std::prev(groups.end());
                    }
                    group->accesses.push_back({id, block_id, *offset});
                }
            }

            for (auto& group : groups) {
                if ((options.optimize_for == OptimizationGoal::Size ||
                     options.optimize_for ==
                         OptimizationGoal::MinimumSize) &&
                    group.accesses.size() < 2) {
                    continue;
                }
                const auto address_cost = group.element_size == 1 ? 1U : 2U;
                const auto risc_weight =
                    100U - std::min(options.risc_cisc_balance, 100U);
                const auto benefit = static_cast<std::uint64_t>(
                    group.accesses.size()) * address_cost * risc_weight;
                if (benefit <= 100U) continue;

                ManagedValue pointer_phi;
                pointer_phi.location = index_location;
                pointer_phi.type = group.pointer_type;
                pointer_phi.kind = ValueKind::Phi;
                const auto pointer_phi_id = append(std::move(pointer_phi));

                ManagedValue pointer_next;
                pointer_next.location = update_location;
                pointer_next.type = group.pointer_type;
                pointer_next.kind = ValueKind::IndexedAddress;
                pointer_next.operands = {pointer_phi_id, *step_id};
                const auto pointer_next_id = append(std::move(pointer_next));
                function.blocks[latch.value].values.push_back(
                    pointer_next_id);

                auto& completed_phi = function.values[pointer_phi_id.value];
                completed_phi.incoming = {
                    {loop.preheader, group.base},
                    {latch, pointer_next_id}};
                auto& header = function.blocks[loop.header.value].values;
                const auto first_non_phi = std::find_if(
                    header.begin(), header.end(), [&](ValueId id) {
                        return function.values[id.value].kind != ValueKind::Phi;
                    });
                header.insert(first_non_phi, pointer_phi_id);

                std::unordered_map<std::uint64_t, ValueId> offsets;
                for (const auto& access : group.accesses) {
                    auto& block = function.blocks[access.block.value];
                    const auto load_position = std::find(
                        block.values.begin(), block.values.end(), access.load);
                    if (load_position == block.values.end()) continue;
                    ValueId address = pointer_phi_id;
                    if (access.element_offset != 0) {
                        auto offset = offsets.find(access.element_offset);
                        if (offset == offsets.end()) {
                            ManagedValue constant;
                            constant.location = index_location;
                            constant.type = group.index_type;
                            constant.kind = ValueKind::ConstantInteger;
                            constant.integer = access.element_offset;
                            const auto constant_id = append(std::move(constant));
                            function.blocks[loop.preheader.value].values.push_back(
                                constant_id);
                            offset = offsets.emplace(access.element_offset,
                                                     constant_id).first;
                        }
                        ManagedValue lane_address;
                        lane_address.location =
                            function.values[access.load.value].location;
                        lane_address.type = group.pointer_type;
                        lane_address.kind = ValueKind::IndexedAddress;
                        lane_address.operands = {pointer_phi_id,
                                                 offset->second};
                        address = append(std::move(lane_address));
                        block.values.insert(load_position, address);
                    }
                    auto& load = function.values[access.load.value];
                    load.kind = ValueKind::PointerLoad;
                    load.operands = {address};
                }
                changed = true;
            }
        }
    }
    return changed;
}

struct UnrollLoopPattern {
    CanonicalLoop loop;
    BlockId body;
    BlockId backedge;
    BlockId exit;
    std::vector<BlockId> body_blocks;
    std::vector<ValueId> body_values;
    ValueId index;
    ValueId bound;
    std::vector<ValueId> phis;
    std::vector<ValueId> header_values;
};

struct SharedMaskedInduction {
    ValueId value;
    ValueId source;
    UInt128 reduced_mask;
    hir::TypeId type;
    SourceLocation location;
};

bool is_loop_index_view(const ManagedFunction& function,
                        const hir::Module& hir_module,
                        ValueId candidate, ValueId index) {
    std::unordered_set<std::uint32_t> visited;
    while (candidate != index) {
        if (candidate.value >= function.values.size() ||
            !visited.insert(candidate.value).second) {
            return false;
        }
        const auto& value = function.values[candidate.value];
        if (value.kind != ValueKind::Cast ||
            value.cast != CastOperation::Reinterpret ||
            value.operands.size() != 1 ||
            type_bits(hir_module, value.type) !=
                type_bits(hir_module,
                          function.values[value.operands.front().value].type)) {
            return false;
        }
        candidate = value.operands.front();
    }
    return true;
}

std::vector<SharedMaskedInduction> find_shared_masked_inductions(
    const ManagedFunction& function, const hir::Module& hir_module,
    const UnrollLoopPattern& pattern, unsigned factor) {
    std::vector<SharedMaskedInduction> result;
    if (factor < 2 || (factor & (factor - 1U)) != 0) return result;
    const UInt128 low_bits{factor - 1U};
    for (const auto id : pattern.body_values) {
        const auto& value = function.values[id.value];
        if (value.kind != ValueKind::Binary ||
            value.binary != BinaryOperation::BitAnd ||
            value.operands.size() != 2 ||
            !integer_type(hir_module, value.type)) {
            continue;
        }
        std::optional<ValueId> source;
        const ManagedValue* mask{};
        for (unsigned index = 0; index < 2; ++index) {
            const auto source_id = value.operands[index];
            const auto mask_id = value.operands[1U - index];
            const auto& candidate_mask = function.values[mask_id.value];
            if (is_loop_index_view(function, hir_module, source_id,
                                   pattern.index) &&
                candidate_mask.kind == ValueKind::ConstantInteger) {
                source = source_id;
                mask = &candidate_mask;
                break;
            }
        }
        if (!source || !mask) continue;
        const auto bits = type_bits(hir_module, value.type);
        const auto original_mask = mask_to(
            UInt128{mask->integer, mask->integer_high}, bits);
        if (bit_and(original_mask, low_bits) != low_bits) continue;
        const auto reduced_mask = mask_to(
            bit_and(original_mask, bit_not(low_bits)), bits);
        result.push_back(
            {id, *source, reduced_mask, value.type, value.location});
    }
    return result;
}

std::optional<UnrollLoopPattern> find_unrollable_loop(
    const ManagedFunction& function, const hir::Module& hir_module,
    const CanonicalLoop& loop) {
    if (loop.blocks.size() < 2 ||
        std::any_of(function.labels.begin(), function.labels.end(),
                    [&](const ManagedLabel& label) {
                        return loop.blocks.contains(label.block.value);
                    })) {
        return std::nullopt;
    }
    const auto& header = function.blocks[loop.header.value];
    const auto& terminator = header.terminator;
    if (terminator.kind != TerminatorKind::ConditionalBranch ||
        !terminator.value || terminator.successors.size() != 2 ||
        terminator.effect != header.effect) {
        return std::nullopt;
    }
    const auto& condition = function.values[terminator.value->value];
    if (condition.kind != ValueKind::Binary ||
        condition.binary != BinaryOperation::UnsignedLess ||
        condition.operands.size() != 2) {
        return std::nullopt;
    }
    const auto index = condition.operands[0];
    const auto bound = condition.operands[1];
    const auto& induction = function.values[index.value];
    if (induction.kind != ValueKind::Phi ||
        !integer_type(hir_module, induction.type)) {
        return std::nullopt;
    }
    const auto body_id = terminator.successors[0];
    const auto exit_id = terminator.successors[1];
    if (!loop.blocks.contains(body_id.value) ||
        loop.blocks.contains(exit_id.value) || body_id == loop.header) {
        return std::nullopt;
    }
    // Accept a single straight-line chain between the condition header and
    // its backedge. If-conversion commonly leaves a separate latch block for
    // the induction update, even though the machine loop has one body.
    std::vector<BlockId> body_blocks;
    std::vector<ValueId> body_values;
    std::unordered_set<std::uint32_t> visited;
    auto current = body_id;
    auto predecessor = loop.header;
    std::optional<BlockId> backedge;
    while (true) {
        if (current == loop.header ||
            !loop.blocks.contains(current.value) ||
            !visited.insert(current.value).second) {
            return std::nullopt;
        }
        const auto& block = function.blocks[current.value];
        if (block.predecessors.size() != 1 ||
            block.predecessors.front() != predecessor ||
            block.terminator.kind != TerminatorKind::Branch ||
            block.terminator.successors.size() != 1) {
            return std::nullopt;
        }
        body_blocks.push_back(current);
        body_values.insert(body_values.end(), block.values.begin(),
                           block.values.end());
        const auto successor = block.terminator.successors.front();
        if (successor == loop.header) {
            backedge = current;
            break;
        }
        predecessor = current;
        current = successor;
    }
    if (!backedge || body_blocks.size() + 1U != loop.blocks.size()) {
        return std::nullopt;
    }
    const auto definitions = value_definition_blocks(function);
    if (!definitions[bound.value] ||
        loop.blocks.contains(definitions[bound.value]->value)) {
        return std::nullopt;
    }

    std::vector<ValueId> phis;
    std::vector<ValueId> header_values;
    for (const auto id : header.values) {
        const auto& value = function.values[id.value];
        if (value.kind == ValueKind::Phi) {
            if (value.incoming.size() != 2 ||
                !phi_value_from(value, loop.preheader) ||
                !phi_value_from(value, *backedge)) {
                return std::nullopt;
            }
            phis.push_back(id);
        } else if (id != *terminator.value) {
            if (!safely_speculatable_for_if_conversion(
                    value, function, hir_module)) {
                return std::nullopt;
            }
            header_values.push_back(id);
        }
    }
    const auto initial = phi_value_from(induction, loop.preheader);
    const auto next = phi_value_from(induction, *backedge);
    if (!initial || !next) return std::nullopt;
    const auto& initial_value = function.values[initial->value];
    const auto& next_value = function.values[next->value];
    if (initial_value.kind != ValueKind::ConstantInteger ||
        initial_value.integer != 0 || initial_value.integer_high != 0 ||
        next_value.kind != ValueKind::Binary ||
        next_value.binary != BinaryOperation::Add ||
        next_value.operands.size() != 2) {
        return std::nullopt;
    }
    std::optional<ValueId> step;
    if (next_value.operands[0] == index) step = next_value.operands[1];
    else if (next_value.operands[1] == index) step = next_value.operands[0];
    if (!step) return std::nullopt;
    const auto& step_value = function.values[step->value];
    if (step_value.kind != ValueKind::ConstantInteger ||
        step_value.integer != 1 || step_value.integer_high != 0) {
        return std::nullopt;
    }

    const std::unordered_set<std::uint32_t> phi_ids = [&] {
        std::unordered_set<std::uint32_t> result;
        for (const auto phi : phis) result.insert(phi.value);
        return result;
    }();
    const std::unordered_set<std::uint32_t> header_value_ids = [&] {
        std::unordered_set<std::uint32_t> result;
        for (const auto value : header_values) {
            for (const auto operand :
                 function.values[value.value].operands) {
                if (!definitions[operand.value]) return
                    std::unordered_set<std::uint32_t>{};
                const auto definition = *definitions[operand.value];
                if (definition == loop.header &&
                    !phi_ids.contains(operand.value) &&
                    !result.contains(operand.value)) {
                    return std::unordered_set<std::uint32_t>{};
                }
            }
            result.insert(value.value);
        }
        return result;
    }();
    if (header_value_ids.size() != header_values.size()) {
        return std::nullopt;
    }
    std::unordered_set<std::uint32_t> available = phi_ids;
    available.insert(header_value_ids.begin(), header_value_ids.end());
    for (const auto id : body_values) {
        const auto& value = function.values[id.value];
        const bool ordinary_read =
            !value.is_volatile_access &&
            (value.kind == ValueKind::Load ||
             value.kind == ValueKind::PointerLoad ||
             value.kind == ValueKind::IndexedLoad ||
             value.kind == ValueKind::GlobalLoad);
        // A straight-line, non-volatile pointer store can be cloned along
        // with the reads around it.  unroll_loop rebuilds one effect chain in
        // source iteration order, so this exposes memory-level parallelism
        // without reordering potentially aliasing accesses.  Keep atomics,
        // calls, stack management, and direct object/cell stores conservative
        // until their extra lifetime/ownership rules are represented here.
        const bool ordinary_store =
            !value.is_volatile_access &&
            value.kind == ValueKind::PointerStore;
        if ((is_effectful_value(value) &&
             !ordinary_read && !ordinary_store) ||
            value.kind == ValueKind::Phi ||
            value.kind == ValueKind::Parameter ||
            value.kind == ValueKind::VariadicState ||
            value.kind == ValueKind::PatchValue) {
            return std::nullopt;
        }
        for (const auto operand : value.operands) {
            if (!definitions[operand.value]) return std::nullopt;
            const auto definition = *definitions[operand.value];
            if (definition == loop.header &&
                !phi_ids.contains(operand.value) &&
                !header_value_ids.contains(operand.value)) {
                return std::nullopt;
            }
            if (loop.blocks.contains(definition.value) &&
                definition != loop.header &&
                !available.contains(operand.value)) {
                return std::nullopt;
            }
        }
        available.insert(id.value);
    }
    return UnrollLoopPattern{loop, body_id, *backedge, exit_id,
                             std::move(body_blocks), std::move(body_values),
                             index, bound,
                             std::move(phis),
                             std::move(header_values)};
}

bool unroll_loop(ManagedFunction& function,
                 const UnrollLoopPattern& pattern,
                 const hir::Module& hir_module,
                 unsigned factor,
                 bool select_affine_exit) {
    if (factor < 2) return false;
    const auto& old_header = function.blocks[pattern.loop.header.value];
    const auto& old_body = function.blocks[pattern.body.value];
    const auto condition_id = *old_header.terminator.value;
    const auto condition_type = function.values[condition_id.value].type;
    const auto index_type = function.values[pattern.index.value].type;
    const auto definitions = value_definition_blocks(function);
    const auto induction_update = *phi_value_from(
        function.values[pattern.index.value], pattern.backedge);
    const auto shared_masked_patterns = find_shared_masked_inductions(
        function, hir_module, pattern, factor);

    struct AffineExitInduction {
        ValueId phi;
        ValueId initial;
        std::uint64_t step{};
        hir::TypeId type;
        SourceLocation location;
    };
    const auto affine_exit = [&]() -> std::optional<AffineExitInduction> {
        if (!select_affine_exit ||
            !unsigned_integer_type(
                hir_module, function.values[pattern.index.value].type)) {
            return std::nullopt;
        }
        // The ordinary counter must exist solely to control this loop. Its
        // value after a normal exit is then exactly the bound, so both the
        // main unrolled loop and scalar cleanup can discard the recurrence.
        for (const auto& value : function.values) {
            const bool uses_index =
                std::find(value.operands.begin(), value.operands.end(),
                          pattern.index) != value.operands.end() ||
                std::any_of(
                    value.call_arguments.begin(), value.call_arguments.end(),
                    [&](const CallArgument& argument) {
                        return argument.value == pattern.index;
                    }) ||
                std::any_of(value.incoming.begin(), value.incoming.end(),
                            [&](const PhiIncoming& incoming) {
                                return incoming.value == pattern.index;
                            });
            if (uses_index && value.id != induction_update &&
                value.id != condition_id) {
                return std::nullopt;
            }
        }
        for (const auto& block : function.blocks) {
            if (block.terminator.value == pattern.index) {
                return std::nullopt;
            }
        }

        const auto index_bits = type_bits(
            hir_module, function.values[pattern.index.value].type);
        for (const auto candidate_id : pattern.phis) {
            if (candidate_id == pattern.index) continue;
            const auto& candidate = function.values[candidate_id.value];
            if (!unsigned_integer_type(hir_module, candidate.type) ||
                type_bits(hir_module, candidate.type) != index_bits) {
                continue;
            }
            const auto initial = phi_value_from(
                candidate, pattern.loop.preheader);
            const auto update_id = phi_value_from(
                candidate, pattern.backedge);
            if (!initial || !update_id) continue;
            const auto& update = function.values[update_id->value];
            if (update.kind != ValueKind::Binary ||
                update.binary != BinaryOperation::Add ||
                update.operands.size() != 2) {
                continue;
            }
            std::optional<ValueId> step_id;
            if (update.operands[0] == candidate_id) {
                step_id = update.operands[1];
            } else if (update.operands[1] == candidate_id) {
                step_id = update.operands[0];
            }
            if (!step_id) continue;
            const auto& step = function.values[step_id->value];
            if (step.kind != ValueKind::ConstantInteger ||
                step.integer_high != 0 || step.integer == 0 ||
                (step.integer & 1U) == 0) {
                continue;
            }
            return AffineExitInduction{
                candidate_id, *initial, step.integer, candidate.type,
                candidate.location};
        }
        return std::nullopt;
    }();

    const BlockId header_id{
        static_cast<std::uint32_t>(function.blocks.size())};
    const BlockId body_id{header_id.value + 1U};
    const BlockId exit_id{header_id.value + 2U};
    ManagedBlock header;
    header.id = header_id;
    header.location = old_header.location;
    header.predecessors = {pattern.loop.preheader, body_id};
    ManagedBlock body;
    body.id = body_id;
    body.location = old_body.location;
    body.predecessors = {header_id};
    ManagedBlock exit;
    exit.id = exit_id;
    exit.location = old_header.location;
    exit.predecessors = {header_id};

    const auto add_phi_effect = [&](SourceLocation location,
                                    std::vector<EffectIncoming> incoming) {
        const EffectId id{
            static_cast<std::uint32_t>(function.effects.size())};
        ManagedEffect effect;
        effect.id = id;
        effect.location = location;
        effect.kind = EffectKind::Phi;
        effect.incoming = std::move(incoming);
        function.effects.push_back(std::move(effect));
        return id;
    };
    const auto preheader_effect =
        function.blocks[pattern.loop.preheader.value].terminator.effect;
    const auto header_effect = add_phi_effect(
        header.location, {{pattern.loop.preheader, preheader_effect}});
    const auto body_effect = add_phi_effect(
        body.location, {{header_id, header_effect}});
    const auto exit_effect = add_phi_effect(
        exit.location, {{header_id, header_effect}});
    header.effect = header_effect;
    body.effect = body_effect;
    exit.effect = exit_effect;

    const auto append_value = [&](std::vector<ValueId>& destination,
                                  ManagedValue value) {
        const ValueId id{
            static_cast<std::uint32_t>(function.values.size())};
        value.id = id;
        function.values.push_back(std::move(value));
        destination.push_back(id);
        return id;
    };
    auto& preheader_values =
        function.blocks[pattern.loop.preheader.value].values;
    ManagedValue factor_value;
    factor_value.location = header.location;
    factor_value.type = index_type;
    factor_value.kind = ValueKind::ConstantInteger;
    factor_value.integer = factor;
    const auto factor_id =
        append_value(preheader_values, std::move(factor_value));
    std::vector<ValueId> induction_offsets(factor + 1U);
    induction_offsets[factor] = factor_id;
    for (unsigned offset = 1; offset < factor; ++offset) {
        ManagedValue offset_value;
        offset_value.location = header.location;
        offset_value.type = index_type;
        offset_value.kind = ValueKind::ConstantInteger;
        offset_value.integer = offset;
        induction_offsets[offset] =
            append_value(preheader_values, std::move(offset_value));
    }
    ManagedValue mask_value;
    mask_value.location = header.location;
    mask_value.type = index_type;
    mask_value.kind = ValueKind::ConstantInteger;
    const auto index_bits = type_bits(hir_module, index_type);
    mask_value.integer = index_bits >= 64
        ? ~(static_cast<std::uint64_t>(factor) - 1U)
        : ((std::uint64_t{1} << index_bits) - 1U) &
              ~(static_cast<std::uint64_t>(factor) - 1U);
    mask_value.integer_high = index_bits > 64
        ? (index_bits >= 128
               ? std::numeric_limits<std::uint64_t>::max()
               : (std::uint64_t{1} << (index_bits - 64U)) - 1U)
        : 0;
    const auto mask_id =
        append_value(preheader_values, std::move(mask_value));

    struct SharedMaskedState {
        ValueId source;
        hir::TypeId type;
        SourceLocation location;
        ValueId mask;
        std::vector<ValueId> offsets;
        std::optional<ValueId> base;
    };
    std::unordered_map<std::uint32_t, SharedMaskedState>
        shared_masked_inductions;
    for (const auto& pattern_value : shared_masked_patterns) {
        ManagedValue shared_mask;
        shared_mask.location = pattern_value.location;
        shared_mask.type = pattern_value.type;
        shared_mask.kind = ValueKind::ConstantInteger;
        shared_mask.integer = pattern_value.reduced_mask.low;
        shared_mask.integer_high = pattern_value.reduced_mask.high;
        const auto shared_mask_id =
            append_value(preheader_values, std::move(shared_mask));
        std::vector<ValueId> offsets(factor);
        for (unsigned offset = 1; offset < factor; ++offset) {
            ManagedValue offset_value;
            offset_value.location = pattern_value.location;
            offset_value.type = pattern_value.type;
            offset_value.kind = ValueKind::ConstantInteger;
            offset_value.integer = offset;
            offsets[offset] =
                append_value(preheader_values, std::move(offset_value));
        }
        shared_masked_inductions.emplace(
            pattern_value.value.value,
            SharedMaskedState{pattern_value.source, pattern_value.type,
                              pattern_value.location, shared_mask_id,
                              std::move(offsets), std::nullopt});
    }

    ManagedValue limit_value;
    limit_value.location = header.location;
    limit_value.type = index_type;
    limit_value.kind = ValueKind::Binary;
    limit_value.binary = BinaryOperation::BitAnd;
    limit_value.operands = {pattern.bound, mask_id};
    const auto unrolled_limit =
        append_value(preheader_values, std::move(limit_value));

    std::optional<ValueId> affine_main_limit;
    std::optional<ValueId> affine_full_limit;
    if (affine_exit) {
        ManagedValue step;
        step.location = affine_exit->location;
        step.type = affine_exit->type;
        step.kind = ValueKind::ConstantInteger;
        step.integer = affine_exit->step;
        const auto step_id =
            append_value(preheader_values, std::move(step));

        const auto as_affine_type = [&](ValueId source) {
            if (function.values[source.value].type == affine_exit->type) {
                return source;
            }
            ManagedValue cast;
            cast.location = affine_exit->location;
            cast.type = affine_exit->type;
            cast.kind = ValueKind::Cast;
            cast.cast = CastOperation::Reinterpret;
            cast.operands = {source};
            return append_value(preheader_values, std::move(cast));
        };
        const auto form_limit = [&](ValueId source) {
            ManagedValue scale;
            scale.location = affine_exit->location;
            scale.type = affine_exit->type;
            scale.kind = ValueKind::Binary;
            scale.binary = BinaryOperation::Multiply;
            scale.operands = {as_affine_type(source), step_id};
            const auto scaled =
                append_value(preheader_values, std::move(scale));

            ManagedValue limit;
            limit.location = affine_exit->location;
            limit.type = affine_exit->type;
            limit.kind = ValueKind::Binary;
            limit.binary = BinaryOperation::Add;
            limit.operands = {affine_exit->initial, scaled};
            return append_value(preheader_values, std::move(limit));
        };
        affine_main_limit = form_limit(unrolled_limit);
        affine_full_limit = form_limit(pattern.bound);
    }

    // Materialize loop literals once for the four cloned iterations. Small
    // integers can still disappear into target immediates; large constants
    // avoid a repeated movabs/load in the unrolled body.
    std::unordered_map<std::uint32_t, ValueId> invariant_values;
    for (const auto original : pattern.body_values) {
        const auto& source = function.values[original.value];
        if (source.kind != ValueKind::ConstantInteger &&
            source.kind != ValueKind::ConstantFloating) {
            continue;
        }
        auto clone = source;
        clone.id = {};
        invariant_values.emplace(
            original.value,
            append_value(preheader_values, std::move(clone)));
    }
    for (const auto [original, replacement] : invariant_values) {
        replace_value_uses(function, ValueId{original}, replacement);
    }
    for (const auto block : pattern.body_blocks) {
        std::erase_if(
            function.blocks[block.value].values,
            [&](ValueId id) {
                return invariant_values.contains(id.value);
            });
    }

    std::unordered_map<std::uint32_t, ValueId> state;
    std::unordered_map<std::uint32_t, ValueId> new_phis;
    for (const auto original : pattern.phis) {
        auto phi = function.values[original.value];
        const auto initial = *phi_value_from(phi, pattern.loop.preheader);
        phi.id = {};
        phi.incoming = {
            {pattern.loop.preheader, initial},
            {body_id, ValueId{}}};
        const auto id = append_value(header.values, std::move(phi));
        state.emplace(original.value, id);
        new_phis.emplace(original.value, id);
    }

    struct AffineUnrollRecurrence {
        ValueId phi;
        std::vector<ValueId> offsets;
    };
    std::unordered_map<std::uint32_t, AffineUnrollRecurrence>
        affine_updates;
    if (select_affine_exit) {
        for (const auto original : pattern.phis) {
            if (original == pattern.index) continue;
            const auto& phi = function.values[original.value];
            if (!unsigned_integer_type(hir_module, phi.type)) continue;
            const auto update_id = phi_value_from(phi, pattern.backedge);
            if (!update_id) continue;
            const auto& update = function.values[update_id->value];
            if (update.kind != ValueKind::Binary ||
                update.binary != BinaryOperation::Add ||
                update.operands.size() != 2) {
                continue;
            }
            std::optional<ValueId> step_id;
            if (update.operands[0] == original) {
                step_id = update.operands[1];
            } else if (update.operands[1] == original) {
                step_id = update.operands[0];
            }
            if (!step_id) continue;
            const auto& step = function.values[step_id->value];
            const auto bits = type_bits(hir_module, phi.type);
            if (step.kind != ValueKind::ConstantInteger || bits == 0 ||
                bits > 64 || step.integer_high != 0) {
                continue;
            }
            // Appending offsets grows function.values, which invalidates
            // the phi/update/step references above; copy what the loop
            // needs first.
            const auto offset_location = update.location;
            const auto offset_type = phi.type;
            const auto step_integer = step.integer;
            std::vector<ValueId> offsets(factor + 1U);
            for (unsigned iteration = 1; iteration <= factor; ++iteration) {
                ManagedValue offset;
                offset.location = offset_location;
                offset.type = offset_type;
                offset.kind = ValueKind::ConstantInteger;
                offset.integer = mask_to(
                    multiply(UInt128{step_integer}, UInt128{iteration}),
                    bits).low;
                offsets[iteration] =
                    append_value(preheader_values, std::move(offset));
            }
            affine_updates.emplace(
                update_id->value,
                AffineUnrollRecurrence{original, std::move(offsets)});
        }
    }
    const auto index = state.at(pattern.index.value);
    ManagedValue guard;
    guard.location = header.location;
    guard.type = condition_type;
    guard.kind = ValueKind::Binary;
    if (affine_exit) {
        guard.binary = BinaryOperation::NotEqual;
        guard.operands = {
            state.at(affine_exit->phi.value), *affine_main_limit};
    } else {
        guard.binary = BinaryOperation::UnsignedLess;
        guard.operands = {index, unrolled_limit};
    }
    const auto guard_id = append_value(header.values, std::move(guard));

    const auto resolve = [&](ValueId operand,
                             const std::unordered_map<std::uint32_t, ValueId>&
                                 iteration) -> std::optional<ValueId> {
        if (const auto found = iteration.find(operand.value);
            found != iteration.end()) {
            return found->second;
        }
        if (const auto found = state.find(operand.value);
            found != state.end()) {
            return found->second;
        }
        if (const auto found = invariant_values.find(operand.value);
            found != invariant_values.end()) {
            return found->second;
        }
        if (operand.value >= definitions.size()) return operand;
        if (!definitions[operand.value] ||
            pattern.loop.blocks.contains(
                definitions[operand.value]->value)) {
            return std::nullopt;
        }
        return operand;
    };
    const auto append_header_values =
        [&](std::vector<ValueId>& destination) {
            for (const auto original : pattern.header_values) {
                auto clone = function.values[original.value];
                clone.id = {};
                for (auto& operand : clone.operands) {
                    const auto replacement = resolve(operand, {});
                    if (!replacement) return false;
                    operand = *replacement;
                }
                state[original.value] =
                    append_value(destination, std::move(clone));
            }
            return true;
        };
    if (!append_header_values(header.values)) return false;
    auto current_effect = body_effect;
    const auto append_body_value = [&](ManagedValue value) {
        const bool effectful = is_effectful_value(value);
        if (effectful) {
            value.effect_input = current_effect;
            value.effect_output = EffectId{
                static_cast<std::uint32_t>(function.effects.size())};
        }
        const auto id = append_value(body.values, std::move(value));
        if (effectful) {
            ManagedEffect effect;
            effect.id = *function.values[id.value].effect_output;
            effect.location = function.values[id.value].location;
            effect.kind = EffectKind::Operation;
            effect.input = current_effect;
            effect.operation = id;
            function.effects.push_back(std::move(effect));
            current_effect = *function.values[id.value].effect_output;
        }
        return id;
    };
    for (unsigned iteration_number = 0;
         iteration_number < factor; ++iteration_number) {
        std::unordered_map<std::uint32_t, ValueId> iteration;
        std::optional<ValueId> next_index;
        for (const auto original : pattern.body_values) {
            if (invariant_values.contains(original.value)) continue;
            if (const auto found =
                    shared_masked_inductions.find(original.value);
                found != shared_masked_inductions.end()) {
                auto& shared = found->second;
                ManagedValue derived;
                derived.location = shared.location;
                derived.type = shared.type;
                derived.kind = ValueKind::Binary;
                if (iteration_number == 0) {
                    const auto source = resolve(shared.source, iteration);
                    if (!source) return false;
                    derived.binary = BinaryOperation::BitAnd;
                    derived.operands = {*source, shared.mask};
                } else {
                    if (!shared.base) return false;
                    derived.binary = BinaryOperation::Add;
                    derived.operands = {
                        *shared.base, shared.offsets[iteration_number]};
                }
                const auto id = append_body_value(std::move(derived));
                if (iteration_number == 0) shared.base = id;
                iteration.emplace(original.value, id);
                continue;
            }
            if (const auto found = affine_updates.find(original.value);
                found != affine_updates.end()) {
                ManagedValue next;
                next.location = function.values[original.value].location;
                next.type = function.values[found->second.phi.value].type;
                next.kind = ValueKind::Binary;
                next.binary = BinaryOperation::Add;
                next.operands = {
                    new_phis.at(found->second.phi.value),
                    found->second.offsets[iteration_number + 1U]};
                const auto id = iteration_number == 0
                    ? append_value(header.values, std::move(next))
                    : append_body_value(std::move(next));
                iteration.emplace(original.value, id);
                continue;
            }
            if (original == induction_update) {
                // Express every cloned induction value relative to the
                // loop-header phi rather than as a chain of +1 updates.
                // Keep the definition at the original update point so the
                // old induction dies first; target address folding can then
                // turn i+1/i+2/i+3 into displacements and coalesce i+N with
                // the backedge phi.
                ManagedValue next_index_value;
                next_index_value.location = body.location;
                next_index_value.type = index_type;
                next_index_value.kind = ValueKind::Binary;
                next_index_value.binary = BinaryOperation::Add;
                next_index_value.operands = {
                    index, induction_offsets[iteration_number + 1U]};
                next_index =
                    append_body_value(std::move(next_index_value));
                iteration.emplace(induction_update.value, *next_index);
                continue;
            }
            auto clone = function.values[original.value];
            clone.id = {};
            for (auto& operand : clone.operands) {
                const auto replacement = resolve(operand, iteration);
                if (!replacement) return false;
                operand = *replacement;
            }
            const auto id = append_body_value(std::move(clone));
            iteration.emplace(original.value, id);
        }
        if (!next_index) return false;
        auto next_state = state;
        for (const auto original : pattern.phis) {
            const auto incoming = *phi_value_from(
                function.values[original.value], pattern.backedge);
            const auto replacement = resolve(incoming, iteration);
            if (!replacement) return false;
            next_state[original.value] = *replacement;
        }
        state = std::move(next_state);
        if (iteration_number + 1U < factor) {
            for (const auto original : pattern.header_values) {
                state.erase(original.value);
            }
            if (!append_header_values(body.values)) return false;
        }
    }
    for (const auto original : pattern.phis) {
        function.values[new_phis.at(original.value).value]
            .incoming[1].value = state.at(original.value);
    }

    header.terminator.kind = TerminatorKind::ConditionalBranch;
    header.terminator.location = header.location;
    header.terminator.value = guard_id;
    header.terminator.successors = {body_id, exit_id};
    header.terminator.effect = header_effect;
    body.terminator.kind = TerminatorKind::Branch;
    body.terminator.location = body.location;
    body.terminator.successors = {header_id};
    body.terminator.effect = current_effect;
    exit.terminator.kind = TerminatorKind::Branch;
    exit.terminator.location = exit.location;
    exit.terminator.successors = {pattern.loop.header};
    exit.terminator.effect = exit_effect;
    function.effects[header_effect.value].incoming.push_back(
        {body_id, current_effect});

    auto& preheader = function.blocks[pattern.loop.preheader.value];
    std::replace(preheader.terminator.successors.begin(),
                 preheader.terminator.successors.end(),
                 pattern.loop.header, header_id);
    auto& original_header = function.blocks[pattern.loop.header.value];
    std::replace(original_header.predecessors.begin(),
                 original_header.predecessors.end(),
                 pattern.loop.preheader, exit_id);
    for (const auto original : pattern.phis) {
        auto& phi = function.values[original.value];
        for (auto& incoming : phi.incoming) {
            if (incoming.predecessor != pattern.loop.preheader) continue;
            incoming.predecessor = exit_id;
            incoming.value = affine_exit && original == pattern.index
                ? unrolled_limit
                : new_phis.at(original.value);
        }
    }
    if (affine_exit) {
        auto& original_condition = function.values[condition_id.value];
        original_condition.binary = BinaryOperation::NotEqual;
        original_condition.operands = {
            affine_exit->phi, *affine_full_limit};
    }
    auto& original_effect =
        function.effects[original_header.effect.value];
    for (auto& incoming : original_effect.incoming) {
        if (incoming.predecessor == pattern.loop.preheader) {
            incoming.predecessor = exit_id;
            incoming.effect = exit_effect;
        }
    }
    function.blocks.push_back(std::move(header));
    function.blocks.push_back(std::move(body));
    function.blocks.push_back(std::move(exit));
    return true;
}

void unroll_loops(ManagedFunction& function,
                  const hir::Module& hir_module,
                  const CompilerOptions& options,
                  std::span<const CanonicalLoop> loops) {
    if (options.unroll_factor < 2) return;
    for (const auto& loop : loops) {
        const auto pattern =
            find_unrollable_loop(function, hir_module, loop);
        if (!pattern) continue;
        const auto body_cost = pattern->body_values.size();
        if (body_cost == 0 || body_cost > 48) continue;
        // Four copies are profitable for compact recurrence and memory loops.
        // Larger memory bodies get two copies to expose independent misses
        // without excessive register pressure or instruction-cache growth.
        const bool reads_memory = std::any_of(
            pattern->body_values.begin(), pattern->body_values.end(),
            [&](ValueId id) {
                const auto kind = function.values[id.value].kind;
                return kind == ValueKind::Load ||
                       kind == ValueKind::PointerLoad ||
                       kind == ValueKind::IndexedLoad ||
                       kind == ValueKind::GlobalLoad;
            });
        const bool contains_select = std::any_of(
            pattern->body_values.begin(), pattern->body_values.end(),
            [&](ValueId id) {
                return function.values[id.value].kind == ValueKind::Select;
            });
        const bool contains_pointer_store = std::any_of(
            pattern->body_values.begin(), pattern->body_values.end(),
            [&](ValueId id) {
                return function.values[id.value].kind ==
                       ValueKind::PointerStore;
            });
        const bool contains_floating_recurrence = std::any_of(
            pattern->phis.begin(), pattern->phis.end(),
            [&](ValueId id) {
                return floating_type(hir_module,
                                     function.values[id.value].type);
            });
        // A four-way store loop keeps the address, stored value, and each
        // loop-carried result live together.  That is cheap on a wide CISC
        // register file but can turn a compact load/store loop into spills on
        // a smaller RISC bank.  Blend the largest body admitted at factor four
        // instead of selecting a different MIR dialect for each architecture.
        // Factor two still exposes independent memory operations and retains
        // the user's explicit upper bound.
        const auto balance = std::min(options.risc_cisc_balance, 100U);
        const auto four_way_store_budget =
            (12U * (100U - balance) + 24U * balance) / 100U;
        const bool store_pressure_limited =
            contains_pointer_store && body_cost > four_way_store_budget;
        const unsigned preferred =
            body_cost <= 24 && !store_pressure_limited &&
                (contains_select || contains_pointer_store || !reads_memory ||
                 (options.unroll_factor >= 4 &&
                  contains_floating_recurrence))
                ? 4U
                : 2U;
        const unsigned factor = std::min(preferred, options.unroll_factor);
        (void)unroll_loop(function, *pattern, hir_module, factor,
                          options.ivopts);
    }
}

bool reassociate_unit_recurrence_adds_impl(
    ManagedFunction& function, const hir::Module& hir_module,
    std::span<const CanonicalLoop> loops, const UseLists& use_lists) {
    struct Candidate {
        ValueId root;
        ValueId inner;
        ValueId base;
        std::size_t position{};
    };

    const auto add_other_operand = [&](ValueId id, ValueId operand)
        -> std::optional<ValueId> {
        if (id.value >= function.values.size()) return std::nullopt;
        const auto& value = function.values[id.value];
        if (value.kind != ValueKind::Binary ||
            value.binary != BinaryOperation::Add ||
            value.operands.size() != 2 || is_effectful_value(value)) {
            return std::nullopt;
        }
        if (value.operands[0] == operand) return value.operands[1];
        if (value.operands[1] == operand) return value.operands[0];
        return std::nullopt;
    };
    const auto unit_constant = [&](ValueId id, hir::TypeId type) {
        return id.value < function.values.size() &&
               function.values[id.value].type == type &&
               constant_integer(function.values[id.value], UInt128{1});
    };

    bool changed = false;
    std::unordered_set<std::uint32_t> rewritten_roots;
    for (const auto& loop : loops) {
        if (loop.header.value >= function.blocks.size()) continue;
        const auto& header = function.blocks[loop.header.value];
        std::vector<BlockId> backedges;
        for (const auto predecessor : header.predecessors) {
            if (loop.blocks.contains(predecessor.value)) {
                backedges.push_back(predecessor);
            }
        }
        if (backedges.size() != 1) continue;
        const auto backedge = backedges.front();
        auto& block = function.blocks[backedge.value];

        for (const auto phi_id : header.values) {
            const auto& phi = function.values[phi_id.value];
            if (phi.kind != ValueKind::Phi ||
                !unsigned_integer_type(hir_module, phi.type)) {
                continue;
            }
            const auto incoming = std::find_if(
                phi.incoming.begin(), phi.incoming.end(),
                [&](const PhiIncoming& edge) {
                    return edge.predecessor == backedge;
                });
            if (incoming == phi.incoming.end()) continue;
            const auto next_id = incoming->value;
            if (use_lists.definition_block(next_id) != backedge ||
                function.values[next_id.value].type != phi.type) {
                continue;
            }
            const auto recurrence_unit =
                add_other_operand(next_id, phi_id);
            if (!recurrence_unit ||
                !unit_constant(*recurrence_unit, phi.type)) {
                continue;
            }

            std::vector<Candidate> candidates;
            for (std::size_t position = 0; position < block.values.size();
                 ++position) {
                const auto root_id = block.values[position];
                if (rewritten_roots.contains(root_id.value)) continue;
                const auto& root = function.values[root_id.value];
                if (root.kind != ValueKind::Binary ||
                    root.binary != BinaryOperation::Add ||
                    root.type != phi.type || root.operands.size() != 2 ||
                    is_effectful_value(root)) {
                    continue;
                }

                std::optional<ValueId> inner_id;
                if (unit_constant(root.operands[0], phi.type)) {
                    inner_id = root.operands[1];
                } else if (unit_constant(root.operands[1], phi.type)) {
                    inner_id = root.operands[0];
                }
                if (!inner_id ||
                    use_lists.definition_block(*inner_id) != backedge) {
                    continue;
                }
                const auto& inner = function.values[inner_id->value];
                if (inner.type != phi.type) continue;
                const auto base = add_other_operand(*inner_id, phi_id);
                if (!base || function.values[base->value].type != phi.type) {
                    continue;
                }
                const auto& uses = use_lists.uses(*inner_id);
                if (uses.size() != 1 || !uses.front().user ||
                    *uses.front().user != root_id ||
                    uses.front().kind != UseKind::Operand) {
                    continue;
                }
                candidates.push_back(
                    {root_id, *inner_id, *base, position});
            }
            if (candidates.empty()) continue;

            const auto next_position = std::find(
                block.values.begin(), block.values.end(), next_id);
            if (next_position == block.values.end()) continue;
            const auto earliest = std::min_element(
                candidates.begin(), candidates.end(),
                [](const Candidate& left, const Candidate& right) {
                    return left.position < right.position;
                })->position;
            const auto current_next = static_cast<std::size_t>(
                std::distance(block.values.begin(), next_position));
            if (current_next > earliest) {
                bool operands_available = true;
                for (const auto operand :
                     function.values[next_id.value].operands) {
                    const auto definition =
                        use_lists.definition_block(operand);
                    if (definition != backedge) continue;
                    const auto operand_position = std::find(
                        block.values.begin(), block.values.end(), operand);
                    if (operand_position == block.values.end() ||
                        static_cast<std::size_t>(std::distance(
                            block.values.begin(), operand_position)) >=
                            earliest) {
                        operands_available = false;
                        break;
                    }
                }
                if (!operands_available) continue;
                block.values.erase(std::next(
                    block.values.begin(),
                    static_cast<std::ptrdiff_t>(current_next)));
                block.values.insert(
                    std::next(block.values.begin(),
                              static_cast<std::ptrdiff_t>(earliest)),
                    next_id);
            }

            for (const auto& candidate : candidates) {
                function.values[candidate.root.value].operands = {
                    candidate.base, next_id};
                rewritten_roots.insert(candidate.root.value);
                changed = true;
            }
        }
    }
    return changed;
}

// An additive recurrence, often exposed by unrolling, such as
//
//     sum = (((sum + a) + b) + c) + d
//
// retains one recurrence dependency for every cloned iteration.  For modulo
// unsigned addition, form the independent contribution first and update the
// loop-carried phi once.  Restrict flattening to single-use nodes in the
// backedge block: intermediate values observed by another expression or side
// effect keep their original evaluation graph.  Signed addition is excluded
// because reassociation could introduce overflow into a source execution that
// did not previously overflow.
bool rebalance_unsigned_add_recurrences(
    ManagedFunction& function, const hir::Module& hir_module,
    const CompilerOptions& options, const UseLists& use_lists) {
    struct Candidate {
        ValueId phi;
        std::size_t incoming_index{};
        BlockId backedge;
        hir::TypeId type;
        SourceLocation location;
        std::vector<ValueId> terms;
    };

    std::vector<Candidate> candidates;
    for (const auto& block : function.blocks) {
        for (const auto phi_id : block.values) {
            const auto& phi = function.values[phi_id.value];
            if (phi.kind != ValueKind::Phi ||
                !unsigned_integer_type(hir_module, phi.type)) {
                continue;
            }
            for (std::size_t incoming_index = 0;
                 incoming_index < phi.incoming.size(); ++incoming_index) {
                const auto& incoming = phi.incoming[incoming_index];
                const auto incoming_definition =
                    use_lists.definition_block(incoming.value);
                if (!incoming_definition ||
                    *incoming_definition != incoming.predecessor) {
                    continue;
                }
                // Store-heavy unrolled loops already keep the stored value,
                // address, and recurrence live together.  Balancing a second
                // reduction there lengthens all contribution live ranges and
                // commonly trades one dependency chain for spills.  Read-only
                // gather/reduction loops have the memory-level parallelism
                // this transform is intended to expose.
                const auto& backedge_block =
                    function.blocks[incoming.predecessor.value];
                const bool writes_memory = std::any_of(
                    backedge_block.values.begin(), backedge_block.values.end(),
                    [&](ValueId id) {
                        const auto kind = function.values[id.value].kind;
                        return kind == ValueKind::Store ||
                               kind == ValueKind::PointerStore ||
                               kind == ValueKind::GlobalStore ||
                               kind == ValueKind::Atomic;
                    });
                if (writes_memory) continue;
                std::vector<ValueId> leaves;
                bool valid = true;
                const auto flatten = [&](const auto& self, ValueId id,
                                         unsigned depth) -> void {
                    if (!valid) return;
                    if (depth >= 64 || id.value >= function.values.size()) {
                        valid = false;
                        return;
                    }
                    if (id == phi_id) {
                        leaves.push_back(id);
                        return;
                    }
                    const auto& value = function.values[id.value];
                    const auto definition = use_lists.definition_block(id);
                    const bool local_single_use =
                        definition && *definition == incoming.predecessor &&
                        use_lists.uses(id).size() == 1;
                    if (local_single_use && value.kind == ValueKind::Binary &&
                        value.binary == BinaryOperation::Add &&
                        value.type == phi.type && value.operands.size() == 2 &&
                        !is_effectful_value(value)) {
                        self(self, value.operands[0], depth + 1U);
                        self(self, value.operands[1], depth + 1U);
                        return;
                    }
                    leaves.push_back(id);
                };
                flatten(flatten, incoming.value, 0);
                if (!valid ||
                    std::count(leaves.begin(), leaves.end(), phi_id) != 1) {
                    continue;
                }
                std::erase(leaves, phi_id);
                if (leaves.size() < 2) continue;

                // Reassociation shortens the loop-carried dependency by one
                // add per independent term, but every term then remains live
                // until the contribution tree is complete. Blend the two
                // costs continuously: a load/store RISC endpoint prefers the
                // original short live ranges, while a CISC/OoO endpoint can
                // profit from the shallower recurrence. Wider-than-register
                // values pay proportionally more pressure.
                const auto balance =
                    std::min(options.risc_cisc_balance, 100U);
                const auto blended = [&](unsigned risc, unsigned cisc) {
                    return static_cast<std::uint64_t>(risc) *
                               (100U - balance) +
                           static_cast<std::uint64_t>(cisc) * balance;
                };
                const auto independent_terms = leaves.size() - 1U;
                const auto pressure_units = std::max(
                    1U, (type_bits(hir_module, phi.type) + 63U) / 64U);
                const auto dependency_benefit =
                    independent_terms * blended(1U, 3U);
                const auto live_range_cost = independent_terms *
                    pressure_units * blended(3U, 1U);
                if (dependency_benefit <= live_range_cost) continue;

                std::sort(leaves.begin(), leaves.end(),
                          [](ValueId left, ValueId right) {
                              return left.value < right.value;
                          });
                candidates.push_back(
                    {phi_id, incoming_index, incoming.predecessor, phi.type,
                     function.values[incoming.value.value].location,
                     std::move(leaves)});
            }
        }
    }

    for (auto& candidate : candidates) {
        const auto append_add = [&](ValueId left, ValueId right) {
            ManagedValue addition;
            addition.location = candidate.location;
            addition.type = candidate.type;
            addition.kind = ValueKind::Binary;
            addition.binary = BinaryOperation::Add;
            addition.operands = {left, right};
            const ValueId id{
                static_cast<std::uint32_t>(function.values.size())};
            addition.id = id;
            function.values.push_back(std::move(addition));
            function.blocks[candidate.backedge.value].values.push_back(id);
            return id;
        };

        auto layer = std::move(candidate.terms);
        while (layer.size() > 1) {
            std::vector<ValueId> next;
            next.reserve((layer.size() + 1U) / 2U);
            for (std::size_t index = 0; index < layer.size(); index += 2U) {
                if (index + 1U == layer.size()) {
                    next.push_back(layer[index]);
                } else {
                    next.push_back(append_add(layer[index], layer[index + 1U]));
                }
            }
            layer = std::move(next);
        }
        const auto root = append_add(candidate.phi, layer.front());
        function.values[candidate.phi.value]
            .incoming[candidate.incoming_index]
            .value = root;
    }
    return !candidates.empty();
}

bool scalar_vector_element(const hir::Module& hir_module,
                           hir::TypeId type) {
    const auto& value = hir_module.type(type);
    if (value.kind != hir::Type::Kind::Builtin) return false;
    return integer_type(hir_module, type) ||
           value.builtin == BuiltinType::F32 ||
           value.builtin == BuiltinType::F64;
}

std::optional<ReductionLoopPattern> find_reduction_loop(
    const ManagedFunction& function, const hir::Module& hir_module,
    const CanonicalLoop& loop) {
    if (loop.blocks.size() < 2 || loop.blocks.size() > 3 ||
        std::any_of(function.labels.begin(), function.labels.end(),
                    [&](const ManagedLabel& label) {
                        return loop.blocks.contains(label.block.value);
                    })) {
        return std::nullopt;
    }
    const auto& header = function.blocks[loop.header.value];
    const auto& terminator = header.terminator;
    if (terminator.kind != TerminatorKind::ConditionalBranch ||
        !terminator.value || terminator.successors.size() != 2) {
        return std::nullopt;
    }
    const auto& condition = function.values[terminator.value->value];
    if (condition.kind != ValueKind::Binary ||
        condition.binary != BinaryOperation::UnsignedLess ||
        condition.operands.size() != 2) {
        return std::nullopt;
    }
    const auto index_id = condition.operands.front();
    const auto& index = function.values[index_id.value];
    if (index.kind != ValueKind::Phi ||
        !scalar_vector_element(hir_module, index.type) ||
        floating_type(hir_module, index.type)) {
        return std::nullopt;
    }
    const auto body_id = terminator.successors.front();
    const auto exit_id = terminator.successors.back();
    if (!loop.blocks.contains(body_id.value) || body_id == loop.header ||
        loop.blocks.contains(exit_id.value)) {
        return std::nullopt;
    }
    const auto& body = function.blocks[body_id.value];
    if (body.predecessors.size() != 1 ||
        body.predecessors.front() != loop.header ||
        body.terminator.kind != TerminatorKind::Branch ||
        body.terminator.successors.size() != 1) {
        return std::nullopt;
    }
    auto backedge_id = body_id;
    if (body.terminator.successors.front() != loop.header) {
        backedge_id = body.terminator.successors.front();
        if (loop.blocks.size() != 3 ||
            !loop.blocks.contains(backedge_id.value) ||
            backedge_id == loop.header || backedge_id == body_id) {
            return std::nullopt;
        }
        const auto& latch = function.blocks[backedge_id.value];
        if (latch.predecessors.size() != 1 ||
            latch.predecessors.front() != body_id ||
            latch.terminator.kind != TerminatorKind::Branch ||
            latch.terminator.successors.size() != 1 ||
            latch.terminator.successors.front() != loop.header) {
            return std::nullopt;
        }
    } else if (loop.blocks.size() != 2) {
        return std::nullopt;
    }
    const auto initial_index = phi_value_from(index, loop.preheader);
    const auto index_next = phi_value_from(index, backedge_id);
    if (!initial_index || !index_next) return std::nullopt;
    const auto& initial_index_value =
        function.values[initial_index->value];
    if (initial_index_value.kind != ValueKind::ConstantInteger ||
        initial_index_value.integer != 0 ||
        initial_index_value.integer_high != 0) {
        return std::nullopt;
    }
    const auto& next = function.values[index_next->value];
    if (next.kind != ValueKind::Binary ||
        next.binary != BinaryOperation::Add ||
        next.operands.size() != 2) {
        return std::nullopt;
    }
    std::optional<ValueId> step;
    if (next.operands[0] == index_id) step = next.operands[1];
    else if (next.operands[1] == index_id) step = next.operands[0];
    if (!step) return std::nullopt;
    const auto& step_value = function.values[step->value];
    if (step_value.kind != ValueKind::ConstantInteger ||
        step_value.integer != 1 || step_value.integer_high != 0) {
        return std::nullopt;
    }

    std::unordered_set<std::uint32_t> loop_values;
    for (const auto block_id : loop.blocks) {
        for (const auto id : function.blocks[block_id].values) {
            loop_values.insert(id.value);
        }
    }
    const auto loop_invariant = [&](ValueId id) {
        const auto& value = function.values[id.value];
        return !loop_values.contains(id.value) ||
               value.kind == ValueKind::ConstantInteger ||
               value.kind == ValueKind::ConstantFloating;
    };
    // The bound must be loop invariant. Folding can leave an invariant
    // expression of constants inside the loop; the header computes it on
    // every entry, so the transform may recompute it in the preheader.
    const std::function<bool(ValueId)> invariant_expression =
        [&](ValueId id) {
            if (!loop_values.contains(id.value)) return true;
            const auto& value = function.values[id.value];
            if (value.effect_input || value.effect_output ||
                (value.kind != ValueKind::ConstantInteger &&
                 value.kind != ValueKind::Unary &&
                 value.kind != ValueKind::Binary &&
                 value.kind != ValueKind::Cast)) {
                return false;
            }
            return std::all_of(value.operands.begin(), value.operands.end(),
                               invariant_expression);
        };
    if (!invariant_expression(condition.operands[1])) return std::nullopt;

    struct ReductionCandidate {
        ValueId phi;
        ValueId initial;
        ValueId term;
        ValueId next;
        BinaryOperation operation;
    };
    std::optional<ReductionCandidate> reduction;
    std::vector<AffineLoopValue> affine_values;
    for (const auto id : header.values) {
        if (id == index_id) continue;
        const auto& phi = function.values[id.value];
        if (phi.kind != ValueKind::Phi ||
            !scalar_vector_element(hir_module, phi.type) ||
            type_bits(hir_module, phi.type) < 32) {
            continue;
        }
        const auto initial = phi_value_from(phi, loop.preheader);
        const auto carried = phi_value_from(phi, backedge_id);
        if (!initial || !carried || *carried == id) continue;
        const auto& update = function.values[carried->value];
        const bool associative_integer =
            integer_type(hir_module, phi.type) &&
            (update.binary == BinaryOperation::Add ||
             update.binary == BinaryOperation::BitAnd ||
             update.binary == BinaryOperation::BitOr ||
             update.binary == BinaryOperation::BitXor);
        if (update.kind != ValueKind::Binary ||
            (!associative_integer &&
             update.binary != BinaryOperation::Add) ||
            update.operands.size() != 2) {
            continue;
        }
        std::optional<ValueId> term;
        if (update.operands[0] == id) term = update.operands[1];
        else if (update.operands[1] == id) term = update.operands[0];
        if (!term) continue;

        // An additive recurrence with a loop-invariant step is an affine
        // induction, not a second reduction. Keeping that distinction lets
        // map/reduce loops form packed lane values without changing the
        // source iteration order or dropping recurrence updates.
        if (update.binary == BinaryOperation::Add &&
            integer_type(hir_module, phi.type) && loop_invariant(*term)) {
            affine_values.push_back(
                {id, *initial, *carried, *term});
            continue;
        }
        if (reduction) return std::nullopt;
        reduction = ReductionCandidate{
            id, *initial, *term, *carried, update.binary};
    }
    if (!reduction) return std::nullopt;

    ReductionLoopPattern result{
        loop, body_id, exit_id, index_id, *initial_index,
        condition.operands[1], reduction->phi, reduction->initial,
        reduction->term, *index_next, reduction->next,
        reduction->operation,
        std::move(affine_values), {}, {}};

    for (const auto& affine : result.affine_values) {
        if (!vector_element_compatible(
                hir_module, function.values[affine.phi.value].type,
                function.values[result.reduction.value].type)) {
            return std::nullopt;
        }
    }

    // Skipping several scalar iterations is legal only when every other phi
    // is invariant or is one of the affine recurrences advanced below.
    for (const auto id : header.values) {
        if (id == result.index || id == result.reduction ||
            std::any_of(result.affine_values.begin(),
                        result.affine_values.end(),
                        [&](const AffineLoopValue& affine) {
                            return affine.phi == id;
                        })) {
            continue;
        }
        const auto& phi = function.values[id.value];
        if (phi.kind != ValueKind::Phi) continue;
        const auto carried = phi_value_from(phi, backedge_id);
        if (!carried || *carried != id) return std::nullopt;
    }

    // Vector stores are initially limited to one contiguous, non-volatile
    // destination. Distinct source/destination ranges are guarded at runtime
    // by the transform, so callers retain ordinary C-style alias semantics.
    for (const auto id : body.values) {
        const auto& store = function.values[id.value];
        if (store.kind != ValueKind::PointerStore) continue;
        if (store.is_volatile_access || store.operands.size() != 2 ||
            !result.stores.empty()) {
            return std::nullopt;
        }
        const auto address_id = store.operands[0];
        const auto& address = function.values[address_id.value];
        if (address.kind != ValueKind::IndexedAddress ||
            address.operands.size() != 2 ||
            address.operands[1] != result.index ||
            loop_values.contains(address.operands[0].value) ||
            !vector_element_compatible(
                hir_module,
                function.values[store.operands[1].value].type,
                function.values[result.reduction.value].type)) {
            return std::nullopt;
        }
        result.stores.push_back(
            {id, address_id, address.operands[0], store.operands[1]});
    }
    return result;
}

unsigned preferred_vector_bits(const Subtarget& subtarget,
                               const CompilerOptions& options,
                               bool floating, unsigned element_bits) {
    unsigned cap = std::numeric_limits<unsigned>::max();
    const auto preference =
        resolved_text(options, "m.prefer-vector-width", "none");
    if (preference != "none") {
        unsigned parsed{};
        const auto converted = std::from_chars(
            preference.data(), preference.data() + preference.size(), parsed);
        if (converted.ec == std::errc{}) cap = parsed;
    }
    unsigned result{};
    for (const auto& width :
         subtarget.target().native_vector_widths) {
        const auto feature = floating ? width.floating_feature
                                      : width.integer_feature;
        if (width.bits > cap || width.bits % element_bits != 0 ||
            width.bits / element_bits < 2 ||
            (!feature.empty() && !subtarget.has_feature(feature))) {
            continue;
        }
        result = std::max(result, width.bits);
    }
    return result;
}

bool vectorizable_reduction_term(
    const ManagedFunction& function, const hir::Module& hir_module,
    const ReductionLoopPattern& pattern,
    const std::vector<std::optional<BlockId>>& definitions,
    ValueId id, std::unordered_set<std::uint32_t>& required,
    unsigned& arithmetic_operations) {
    if (!required.insert(id.value).second) return true;
    if (id == pattern.reduction) return false;
    if (id == pattern.index ||
        std::any_of(pattern.affine_values.begin(),
                    pattern.affine_values.end(),
                    [&](const AffineLoopValue& affine) {
                        return affine.phi == id;
                    })) {
        return vector_element_compatible(
            hir_module, function.values[id.value].type,
            function.values[pattern.reduction.value].type);
    }
    if (!definitions[id.value] ||
        !pattern.loop.blocks.contains(definitions[id.value]->value)) {
        return vector_element_compatible(
            hir_module, function.values[id.value].type,
            function.values[pattern.reduction.value].type);
    }
    const auto& value = function.values[id.value];
    if (!vector_element_compatible(
            hir_module, value.type,
            function.values[pattern.reduction.value].type)) {
        return false;
    }
    if (value.kind == ValueKind::ConstantInteger ||
        value.kind == ValueKind::ConstantFloating) {
        return true;
    }
    if (value.kind == ValueKind::Intrinsic &&
        value.intrinsic == IntrinsicOperation::Expect &&
        value.operands.size() == 1) {
        return vectorizable_reduction_term(
            function, hir_module, pattern, definitions,
            value.operands.front(), required, arithmetic_operations);
    }
    if (value.kind == ValueKind::PointerLoad &&
        !value.is_volatile_access && value.operands.size() == 1) {
        const auto address_id = value.operands.front();
        const auto& address = function.values[address_id.value];
        required.insert(address_id.value);
        if (address.kind != ValueKind::IndexedAddress ||
            address.operands.size() != 2 ||
            address.operands[1] != pattern.index) {
            return false;
        }
        const auto base = address.operands.front();
        return definitions[base.value] &&
               !pattern.loop.blocks.contains(definitions[base.value]->value);
    }
    if (value.kind == ValueKind::IndexedLoad &&
        !value.is_volatile_access && value.operands.size() == 2 &&
        value.operands[1] == pattern.index) {
        const auto base = value.operands.front();
        return definitions[base.value] &&
               !pattern.loop.blocks.contains(definitions[base.value]->value);
    }
    if (value.kind == ValueKind::Unary && value.operands.size() == 1) {
        return vectorizable_reduction_term(
            function, hir_module, pattern, definitions,
            value.operands.front(), required, arithmetic_operations);
    }
    if (value.kind == ValueKind::Cast && value.operands.size() == 1 &&
        (value.cast == CastOperation::Reinterpret ||
         value.cast == CastOperation::Truncate ||
         value.cast == CastOperation::ZeroExtend ||
         value.cast == CastOperation::SignExtend)) {
        return vectorizable_reduction_term(
            function, hir_module, pattern, definitions,
            value.operands.front(), required, arithmetic_operations);
    }
    if (value.kind == ValueKind::Select && value.operands.size() == 3) {
        const auto condition_id = value.operands[0];
        const auto& condition = function.values[condition_id.value];
        if (condition.kind != ValueKind::Binary ||
            !comparison(condition.binary) ||
            condition.operands.size() != 2) {
            return false;
        }
        required.insert(condition_id.value);
        ++arithmetic_operations;
        return vectorizable_reduction_term(
                   function, hir_module, pattern, definitions,
                   condition.operands[0], required,
                   arithmetic_operations) &&
               vectorizable_reduction_term(
                   function, hir_module, pattern, definitions,
                   condition.operands[1], required,
                   arithmetic_operations) &&
               vectorizable_reduction_term(
                   function, hir_module, pattern, definitions,
                   value.operands[1], required,
                   arithmetic_operations) &&
               vectorizable_reduction_term(
                   function, hir_module, pattern, definitions,
                   value.operands[2], required,
                   arithmetic_operations);
    }
    if (value.kind != ValueKind::Binary || value.operands.size() != 2) {
        return false;
    }
    const bool floating = floating_type(hir_module, value.type);
    const bool supported = value.binary == BinaryOperation::Add ||
        value.binary == BinaryOperation::Subtract ||
        value.binary == BinaryOperation::Multiply ||
        value.binary == BinaryOperation::BitAnd ||
        value.binary == BinaryOperation::BitOr ||
        value.binary == BinaryOperation::BitXor ||
        value.binary == BinaryOperation::ShiftLeft ||
        value.binary == BinaryOperation::ShiftRightArithmetic ||
        value.binary == BinaryOperation::ShiftRightLogical ||
        (floating && value.binary == BinaryOperation::SignedDivide);
    if (!supported) return false;
    ++arithmetic_operations;
    return vectorizable_reduction_term(
               function, hir_module, pattern, definitions,
               value.operands[0], required, arithmetic_operations) &&
           vectorizable_reduction_term(
               function, hir_module, pattern, definitions,
               value.operands[1], required, arithmetic_operations);
}

bool vectorize_reduction_loop(
    ManagedFunction& function, hir::Module& hir_module,
    const ReductionLoopPattern& pattern,
    const std::vector<std::optional<BlockId>>& definitions,
    const std::unordered_set<std::uint32_t>& required,
    unsigned vector_bits,
    const Subtarget& subtarget,
    const CompilerOptions& options) {
    const auto scalar_type =
        function.values[pattern.reduction.value].type;
    const auto element_bits = type_bits(hir_module, scalar_type);
    if (element_bits == 0 || vector_bits % element_bits != 0) return false;
    const auto lanes = vector_bits / element_bits;
    if (lanes < 2) return false;
    const bool floating = floating_type(hir_module, scalar_type);
    const bool reassociate = !floating || options.fast_math;
    std::unordered_map<std::uint32_t, unsigned> select_depths;
    std::function<unsigned(ValueId)> select_depth = [&](ValueId id) {
        if (id.value >= function.values.size()) return 0U;
        if (const auto found = select_depths.find(id.value);
            found != select_depths.end()) {
            return found->second;
        }
        const auto& value = function.values[id.value];
        if (!definitions[id.value] ||
            !pattern.loop.blocks.contains(definitions[id.value]->value)) {
            select_depths.emplace(id.value, 0U);
            return 0U;
        }
        unsigned result{};
        for (const auto operand : value.operands) {
            result = std::max(result, select_depth(operand));
        }
        if (value.kind == ValueKind::Select) ++result;
        select_depths.emplace(id.value, result);
        return result;
    };
    const auto conditional_depth = select_depth(pattern.term);
    // Independent accumulators hide packed-add latency and let targets issue
    // several loads per cycle. The model-defined vector-interleave budget can
    // permit four streams for store loops too. AVX2 i64 multiplication is the
    // exception:
    // without AVX-512DQ every multiply expands into a long instruction chain,
    // and four simultaneous chains exceed the sixteen-register AVX2 file.
    // Keep that form at two streams to avoid hot-loop spills. A profile may
    // likewise select a smaller budget to limit code size and
    // preserved-register pressure. A vector select has substantially higher
    // live pressure and remains capped at two streams.
    // Strict floating reductions retain a single carrier because changing
    // their addition order is observable.
    const bool expanded_avx2_qword_multiply =
        subtarget.target().architecture == "x86-64" &&
        subtarget.has_feature("avx2") &&
        !subtarget.has_feature("avx512dq") && element_bits == 64 &&
        std::any_of(required.begin(), required.end(),
                    [&](std::uint32_t raw_id) {
                        const auto& value = function.values[raw_id];
                        return value.kind == ValueKind::Binary &&
                               value.binary == BinaryOperation::Multiply;
                    });
    const unsigned memory_interleave = expanded_avx2_qword_multiply
        ? std::min(options.vector_interleave, 2U)
        : options.vector_interleave;
    const unsigned interleave = reassociate
        ? (conditional_depth >= 2U
               ? 1U
               : conditional_depth != 0U
                   ? 2U
                   : !pattern.stores.empty() ? memory_interleave : 4U)
        : 1U;
    if (lanes > std::numeric_limits<unsigned>::max() / interleave) {
        return false;
    }
    const auto vector_step = lanes * interleave;
    if (!std::has_single_bit(vector_step)) return false;
    const auto vector_type = hir_module.vector_of(scalar_type, lanes);
    const auto vector_type_for = [&](hir::TypeId element) {
        return hir_module.vector_of(element, lanes);
    };
    const auto bool_type =
        function.values[function.blocks[pattern.loop.header.value]
                            .terminator.value->value]
            .type;
    const auto index_type = function.values[pattern.index.value].type;

    const bool needs_alias_guard = !pattern.stores.empty() &&
        std::any_of(pattern.load_bases.begin(), pattern.load_bases.end(),
                    [&](ValueId base) {
                        return base != pattern.stores.front().base;
                    });
    const BlockId alias_guard_id{
        static_cast<std::uint32_t>(function.blocks.size())};
    const BlockId vector_header_id{
        alias_guard_id.value + (needs_alias_guard ? 1U : 0U)};
    const BlockId vector_body_id{vector_header_id.value + 1U};
    const BlockId vector_exit_id{vector_header_id.value + 2U};
    const auto vector_entry_id = needs_alias_guard
        ? alias_guard_id : pattern.loop.preheader;
    ManagedBlock alias_guard;
    if (needs_alias_guard) {
        alias_guard.id = alias_guard_id;
        alias_guard.location =
            function.blocks[pattern.loop.header.value].location;
        alias_guard.predecessors = {pattern.loop.preheader};
    }
    ManagedBlock vector_header;
    vector_header.id = vector_header_id;
    vector_header.location =
        function.blocks[pattern.loop.header.value].location;
    vector_header.predecessors = {vector_entry_id, vector_body_id};
    ManagedBlock vector_body;
    vector_body.id = vector_body_id;
    vector_body.location = function.blocks[pattern.body.value].location;
    vector_body.predecessors = {vector_header_id};
    ManagedBlock vector_exit;
    vector_exit.id = vector_exit_id;
    vector_exit.location =
        function.blocks[pattern.loop.header.value].location;
    vector_exit.predecessors = {vector_header_id};

    const auto add_phi_effect = [&](SourceLocation location,
                                    std::vector<EffectIncoming> incoming) {
        const EffectId id{
            static_cast<std::uint32_t>(function.effects.size())};
        ManagedEffect effect;
        effect.id = id;
        effect.location = location;
        effect.kind = EffectKind::Phi;
        effect.incoming = std::move(incoming);
        function.effects.push_back(std::move(effect));
        return id;
    };
    const auto preheader_effect =
        function.blocks[pattern.loop.preheader.value].terminator.effect;
    std::optional<EffectId> alias_guard_effect;
    if (needs_alias_guard) {
        alias_guard_effect = add_phi_effect(
            alias_guard.location,
            {{pattern.loop.preheader, preheader_effect}});
        alias_guard.effect = *alias_guard_effect;
    }
    const auto vector_header_effect = add_phi_effect(
        vector_header.location,
        {{vector_entry_id,
          alias_guard_effect.value_or(preheader_effect)}});
    const auto vector_body_effect = add_phi_effect(
        vector_body.location,
        {{vector_header_id, vector_header_effect}});
    const auto vector_exit_effect = add_phi_effect(
        vector_exit.location,
        {{vector_header_id, vector_header_effect}});
    vector_header.effect = vector_header_effect;
    vector_body.effect = vector_body_effect;
    vector_exit.effect = vector_exit_effect;

    const auto append_value = [&](std::vector<ValueId>& destination,
                                  ManagedValue value) {
        const ValueId id{
            static_cast<std::uint32_t>(function.values.size())};
        value.id = id;
        function.values.push_back(std::move(value));
        destination.push_back(id);
        return id;
    };
    auto& preheader_values =
        function.blocks[pattern.loop.preheader.value].values;
    const auto add_integer_constant = [&](hir::TypeId type,
                                          std::uint64_t integer) {
        ManagedValue value;
        value.location = vector_header.location;
        value.type = type;
        value.kind = ValueKind::ConstantInteger;
        value.integer = integer;
        return append_value(preheader_values, std::move(value));
    };
    const auto lanes_constant = add_integer_constant(index_type, lanes);
    const auto vector_step_constant =
        interleave == 1
            ? lanes_constant
            : add_integer_constant(index_type, vector_step);

    // The bound is loop invariant, but folding may have left it as an
    // expression of constants inside the loop, where it does not dominate
    // the preheader or the alias guard. Recompute it in the preheader.
    const std::function<ValueId(ValueId)> preheader_copy = [&](ValueId id) {
        if (!definitions[id.value] ||
            !pattern.loop.blocks.contains(definitions[id.value]->value)) {
            return id;
        }
        auto clone = function.values[id.value];
        for (auto& operand : clone.operands) operand = preheader_copy(operand);
        return append_value(preheader_values, std::move(clone));
    };
    const auto bound = preheader_copy(pattern.bound);
    // The recognized induction starts at zero, so rounding the bound down to
    // a whole vector group gives a loop-invariant end index. Comparing the
    // induction with this limit avoids a subtract in every vector iteration.
    // It is also naturally safe for short loops: their rounded limit is zero.
    const auto index_bits = type_bits(hir_module, index_type);
    const auto vector_limit_mask = mask_to(
        bit_not(UInt128{vector_step - 1U}), index_bits);
    ManagedValue mask_value;
    mask_value.location = vector_header.location;
    mask_value.type = index_type;
    mask_value.kind = ValueKind::ConstantInteger;
    mask_value.integer = vector_limit_mask.low;
    mask_value.integer_high = vector_limit_mask.high;
    const auto mask =
        append_value(preheader_values, std::move(mask_value));
    ManagedValue limit_value;
    limit_value.location = vector_header.location;
    limit_value.type = index_type;
    limit_value.kind = ValueKind::Binary;
    limit_value.binary = BinaryOperation::BitAnd;
    limit_value.operands = {bound, mask};
    const auto vector_limit =
        append_value(preheader_values, std::move(limit_value));

    std::optional<ValueId> alias_condition;
    if (needs_alias_guard) {
        const auto& store = pattern.stores.front();
        for (const auto load_base : pattern.load_bases) {
            if (load_base == store.base) continue;
            const auto append_address = [&](ValueId base) {
                ManagedValue end;
                end.location = alias_guard.location;
                end.type = function.values[base.value].type;
                end.kind = ValueKind::IndexedAddress;
                end.operands = {base, bound};
                return append_value(alias_guard.values, std::move(end));
            };
            const auto store_end = append_address(store.base);
            const auto load_end = append_address(load_base);
            const auto append_compare = [&](ValueId left, ValueId right) {
                ManagedValue compare;
                compare.location = alias_guard.location;
                compare.type = bool_type;
                compare.kind = ValueKind::Binary;
                compare.binary = BinaryOperation::UnsignedLessEqual;
                compare.operands = {left, right};
                return append_value(alias_guard.values,
                                    std::move(compare));
            };
            const auto store_before_load =
                append_compare(store_end, load_base);
            const auto load_before_store =
                append_compare(load_end, store.base);
            ManagedValue disjoint;
            disjoint.location = alias_guard.location;
            disjoint.type = bool_type;
            disjoint.kind = ValueKind::Binary;
            disjoint.binary = BinaryOperation::BitOr;
            disjoint.operands = {store_before_load, load_before_store};
            const auto disjoint_id =
                append_value(alias_guard.values, std::move(disjoint));
            if (!alias_condition) {
                alias_condition = disjoint_id;
                continue;
            }
            ManagedValue all_disjoint;
            all_disjoint.location = alias_guard.location;
            all_disjoint.type = bool_type;
            all_disjoint.kind = ValueKind::Binary;
            all_disjoint.binary = BinaryOperation::BitAnd;
            all_disjoint.operands = {*alias_condition, disjoint_id};
            alias_condition = append_value(
                alias_guard.values, std::move(all_disjoint));
        }
        if (!alias_condition) return false;
    }

    std::vector<ValueId> lane_constants;
    lane_constants.reserve(lanes);
    for (unsigned lane = 0; lane < lanes; ++lane) {
        lane_constants.push_back(add_integer_constant(index_type, lane));
    }
    ManagedValue index_offsets_splat;
    index_offsets_splat.location = vector_header.location;
    index_offsets_splat.type = vector_type_for(index_type);
    index_offsets_splat.kind = ValueKind::Splat;
    index_offsets_splat.operands = {lane_constants.front()};
    auto index_lane_offsets =
        append_value(preheader_values, std::move(index_offsets_splat));
    for (unsigned lane = 1; lane < lanes; ++lane) {
        ManagedValue insert;
        insert.location = vector_header.location;
        insert.type = vector_type_for(index_type);
        insert.kind = ValueKind::InsertElement;
        insert.operands = {
            index_lane_offsets, lane_constants[lane],
            lane_constants[lane]};
        index_lane_offsets =
            append_value(preheader_values, std::move(insert));
    }

    // Carry the packed induction through the vector loop instead of
    // rebuilding it from the scalar induction with a splat on every trip.
    // Besides removing the scalar-to-SIMD transfer, this also keeps the lane
    // offsets in a register across iterations.  The scalar induction remains
    // available for addressing, the loop test, and scalar cleanup.
    auto initial_lane_index = index_lane_offsets;
    const auto& initial_index_value =
        function.values[pattern.initial_index.value];
    const bool zero_initial_index =
        initial_index_value.kind == ValueKind::ConstantInteger &&
        initial_index_value.integer == 0 &&
        initial_index_value.integer_high == 0;
    if (!zero_initial_index) {
        ManagedValue initial_index_splat;
        initial_index_splat.location = vector_header.location;
        initial_index_splat.type = vector_type_for(index_type);
        initial_index_splat.kind = ValueKind::Splat;
        initial_index_splat.operands = {pattern.initial_index};
        const auto initial_index_vector = append_value(
            preheader_values, std::move(initial_index_splat));
        ManagedValue initial_indices;
        initial_indices.location = vector_header.location;
        initial_indices.type = vector_type_for(index_type);
        initial_indices.kind = ValueKind::Binary;
        initial_indices.binary = BinaryOperation::Add;
        initial_indices.operands = {
            initial_index_vector, index_lane_offsets};
        initial_lane_index = append_value(
            preheader_values, std::move(initial_indices));
    }
    // One lane-group step serves both interleaved groups and the latch. After
    // the final group, adding it once more reaches the next iteration. This
    // avoids keeping a second `lanes * interleave` vector live in the loop.
    ManagedValue index_lane_step_splat;
    index_lane_step_splat.location = vector_header.location;
    index_lane_step_splat.type = vector_type_for(index_type);
    index_lane_step_splat.kind = ValueKind::Splat;
    index_lane_step_splat.operands = {lanes_constant};
    const auto index_lane_step = append_value(
        preheader_values, std::move(index_lane_step_splat));

    ValueId vector_identity{};
    if (reassociate) {
        ManagedValue identity;
        identity.location = vector_header.location;
        identity.type = scalar_type;
        identity.kind = floating ? ValueKind::ConstantFloating
                                 : ValueKind::ConstantInteger;
        if (pattern.reduction_operation == BinaryOperation::BitAnd) {
            const auto all_bits = mask_to(
                bit_not(UInt128{}), element_bits);
            identity.integer = all_bits.low;
            identity.integer_high = all_bits.high;
        }
        const auto scalar_zero =
            append_value(preheader_values, std::move(identity));
        ManagedValue splat;
        splat.location = vector_header.location;
        splat.type = vector_type;
        splat.kind = ValueKind::Splat;
        splat.operands = {scalar_zero};
        vector_identity = append_value(
            preheader_values, std::move(splat));
    }

    struct VectorAffineState {
        AffineLoopValue source;
        ValueId initial_vector;
        ValueId advance_vector;
        ValueId carrier;
        ValueId next;
    };
    // Treat an invariantly-scaled primary induction as an affine vector
    // recurrence as well.  Recomputing `index * constant` in every packed
    // group is especially costly on AVX2, which has no packed qword multiply;
    // advancing the modular product by `vector_step * constant` is exact for
    // every integer width and exposes the same strength reduction to all
    // targets.
    auto vector_affines = pattern.affine_values;
    for (const auto raw_id : required) {
        const ValueId id{raw_id};
        const auto& value = function.values[id.value];
        if (value.kind != ValueKind::Binary ||
            value.binary != BinaryOperation::Multiply ||
            value.operands.size() != 2 ||
            !integer_type(hir_module, value.type)) {
            continue;
        }
        std::optional<ValueId> scale;
        if (value.operands[0] == pattern.index) {
            scale = value.operands[1];
        } else if (value.operands[1] == pattern.index) {
            scale = value.operands[0];
        }
        if (!scale) continue;
        const bool invariant =
            function.values[scale->value].kind ==
                ValueKind::ConstantInteger ||
            !definitions[scale->value] ||
            !pattern.loop.blocks.contains(
                definitions[scale->value]->value);
        if (!invariant ||
            function.values[scale->value].type != value.type ||
            !vector_element_compatible(
                hir_module,
                function.values[pattern.index.value].type,
                value.type)) {
            continue;
        }
        const auto initial = add_integer_constant(value.type, 0);
        vector_affines.push_back({id, initial, id, *scale});
    }
    std::vector<VectorAffineState> affine_states;
    affine_states.reserve(vector_affines.size());
    for (const auto& affine : vector_affines) {
        const auto affine_type =
            function.values[affine.phi.value].type;
        const auto affine_vector_type = vector_type_for(affine_type);
        auto scalar_step = affine.step;
        if (definitions[scalar_step.value] &&
            pattern.loop.blocks.contains(
                definitions[scalar_step.value]->value)) {
            auto clone = function.values[scalar_step.value];
            clone.id = {};
            clone.effect_input.reset();
            clone.effect_output.reset();
            scalar_step = append_value(preheader_values, std::move(clone));
        }
        const auto scaled_step = [&](unsigned multiplier) {
            if (multiplier == 1) return scalar_step;
            const auto factor =
                add_integer_constant(affine_type, multiplier);
            ManagedValue scaled;
            scaled.location = vector_header.location;
            scaled.type = affine_type;
            scaled.kind = ValueKind::Binary;
            scaled.binary = BinaryOperation::Multiply;
            scaled.operands = {scalar_step, factor};
            return append_value(preheader_values, std::move(scaled));
        };

        ManagedValue initial_splat;
        initial_splat.location = vector_header.location;
        initial_splat.type = affine_vector_type;
        initial_splat.kind = ValueKind::Splat;
        initial_splat.operands = {affine.initial};
        auto initial_vector =
            append_value(preheader_values, std::move(initial_splat));
        for (unsigned lane = 1; lane < lanes; ++lane) {
            ManagedValue lane_value;
            lane_value.location = vector_header.location;
            lane_value.type = affine_type;
            lane_value.kind = ValueKind::Binary;
            lane_value.binary = BinaryOperation::Add;
            lane_value.operands = {affine.initial, scaled_step(lane)};
            const auto lane_value_id =
                append_value(preheader_values, std::move(lane_value));
            ManagedValue insert;
            insert.location = vector_header.location;
            insert.type = affine_vector_type;
            insert.kind = ValueKind::InsertElement;
            insert.operands = {
                initial_vector, lane_constants[lane], lane_value_id};
            initial_vector =
                append_value(preheader_values, std::move(insert));
        }

        ManagedValue advance_splat;
        advance_splat.location = vector_header.location;
        advance_splat.type = affine_vector_type;
        advance_splat.kind = ValueKind::Splat;
        advance_splat.operands = {scaled_step(lanes)};
        const auto advance_vector =
            append_value(preheader_values, std::move(advance_splat));
        affine_states.push_back(
            {affine, initial_vector, advance_vector, {}, {}});
    }

    ManagedValue vector_index_phi;
    vector_index_phi.location = vector_header.location;
    vector_index_phi.type = index_type;
    vector_index_phi.kind = ValueKind::Phi;
    vector_index_phi.incoming = {
        {vector_entry_id, pattern.initial_index},
        {vector_body_id, ValueId{}}};
    const auto vector_index =
        append_value(vector_header.values, std::move(vector_index_phi));
    ManagedValue vector_lane_index_phi;
    vector_lane_index_phi.location = vector_header.location;
    vector_lane_index_phi.type = vector_type_for(index_type);
    vector_lane_index_phi.kind = ValueKind::Phi;
    vector_lane_index_phi.incoming = {
        {vector_entry_id, initial_lane_index},
        {vector_body_id, ValueId{}}};
    const auto vector_lane_index = append_value(
        vector_header.values, std::move(vector_lane_index_phi));
    std::vector<ValueId> carriers;
    carriers.reserve(interleave);
    for (unsigned group = 0; group < interleave; ++group) {
        ManagedValue carrier_phi;
        carrier_phi.location = vector_header.location;
        carrier_phi.type = reassociate ? vector_type : scalar_type;
        carrier_phi.kind = ValueKind::Phi;
        carrier_phi.incoming = {
            {vector_entry_id,
             reassociate ? vector_identity : pattern.initial_reduction},
            {vector_body_id, ValueId{}}};
        carriers.push_back(
            append_value(vector_header.values, std::move(carrier_phi)));
    }
    for (auto& affine : affine_states) {
        ManagedValue carrier_phi;
        carrier_phi.location = vector_header.location;
        carrier_phi.type =
            function.values[affine.initial_vector.value].type;
        carrier_phi.kind = ValueKind::Phi;
        carrier_phi.incoming = {
            {vector_entry_id, affine.initial_vector},
            {vector_body_id, ValueId{}}};
        affine.carrier =
            append_value(vector_header.values, std::move(carrier_phi));
    }
    ManagedValue vector_condition;
    vector_condition.location = vector_header.location;
    vector_condition.type = bool_type;
    vector_condition.kind = ValueKind::Binary;
    vector_condition.binary = BinaryOperation::UnsignedLess;
    vector_condition.operands = {vector_index, vector_limit};
    const auto vector_condition_id =
        append_value(vector_header.values, std::move(vector_condition));

    EffectId current_effect = vector_body_effect;
    const auto append_effectful = [&](ManagedValue value) {
        value.effect_input = current_effect;
        const EffectId output{
            static_cast<std::uint32_t>(function.effects.size())};
        value.effect_output = output;
        const auto id = append_value(vector_body.values, std::move(value));
        ManagedEffect effect;
        effect.id = output;
        effect.location = function.values[id.value].location;
        effect.kind = EffectKind::Operation;
        effect.input = current_effect;
        effect.operation = id;
        function.effects.push_back(std::move(effect));
        current_effect = output;
        return id;
    };
    std::unordered_map<std::uint32_t, ValueId> vector_values;
    std::unordered_map<std::uint32_t, ValueId> splats;
    std::unordered_map<std::uint32_t, ValueId> scalar_clones;
    std::unordered_map<std::uint32_t, ValueId> comparison_biases;
    std::unordered_map<std::uint32_t, ValueId> comparison_zeroes;
    ValueId active_vector_index = vector_index;
    ValueId active_lane_index = vector_lane_index;
    std::unordered_map<std::uint32_t, ValueId> active_affine_values;
    std::function<ValueId(ValueId)> vectorize_value;
    const auto splat_scalar = [&](ValueId original) {
        if (const auto found = splats.find(original.value);
            found != splats.end()) {
            return found->second;
        }
        auto scalar = original;
        if (definitions[original.value] &&
            pattern.loop.blocks.contains(
                definitions[original.value]->value)) {
            // A preheader splat cannot refer directly to a definition in the
            // scalar loop body, including a literal: constants are ordinary
            // SSA definitions in MIR. Clone every admitted pure leaf into the
            // preheader. Later local value numbering merges duplicate clones,
            // while the scalar cleanup retains its original definition.
            if (const auto found = scalar_clones.find(original.value);
                found != scalar_clones.end()) {
                scalar = found->second;
            } else {
                auto clone = function.values[original.value];
                clone.id = {};
                clone.effect_input.reset();
                clone.effect_output.reset();
                scalar = append_value(preheader_values, std::move(clone));
                scalar_clones.emplace(original.value, scalar);
            }
        }
        ManagedValue splat;
        splat.location = function.values[original.value].location;
        splat.type = vector_type_for(
            function.values[original.value].type);
        splat.kind = ValueKind::Splat;
        splat.operands = {scalar};
        const auto result =
            append_value(preheader_values, std::move(splat));
        splats.emplace(original.value, result);
        return result;
    };
    const auto comparison_bias = [&](hir::TypeId element_type) {
        if (const auto found = comparison_biases.find(element_type.value);
            found != comparison_biases.end()) {
            return found->second;
        }
        const auto bits = type_bits(hir_module, element_type);
        ManagedValue sign_bit;
        sign_bit.location = vector_header.location;
        sign_bit.type = element_type;
        sign_bit.kind = ValueKind::ConstantInteger;
        sign_bit.integer = std::uint64_t{1} << (bits - 1U);
        const auto scalar = append_value(
            preheader_values, std::move(sign_bit));
        ManagedValue splat;
        splat.location = vector_header.location;
        splat.type = vector_type_for(element_type);
        splat.kind = ValueKind::Splat;
        splat.operands = {scalar};
        const auto result = append_value(
            preheader_values, std::move(splat));
        comparison_biases.emplace(element_type.value, result);
        return result;
    };
    const auto comparison_zero = [&](hir::TypeId element_type) {
        if (const auto found = comparison_zeroes.find(element_type.value);
            found != comparison_zeroes.end()) {
            return found->second;
        }
        ManagedValue zero;
        zero.location = vector_header.location;
        zero.type = element_type;
        zero.kind = ValueKind::ConstantInteger;
        const auto scalar = append_value(
            preheader_values, std::move(zero));
        ManagedValue splat;
        splat.location = vector_header.location;
        splat.type = vector_type_for(element_type);
        splat.kind = ValueKind::Splat;
        splat.operands = {scalar};
        const auto result = append_value(
            preheader_values, std::move(splat));
        comparison_zeroes.emplace(element_type.value, result);
        return result;
    };
    vectorize_value = [&](ValueId id) -> ValueId {
        if (const auto found = vector_values.find(id.value);
            found != vector_values.end()) {
            return found->second;
        }
        if (id == pattern.index) {
            vector_values.emplace(id.value, active_lane_index);
            return active_lane_index;
        }
        if (const auto found = active_affine_values.find(id.value);
            found != active_affine_values.end()) {
            vector_values.emplace(id.value, found->second);
            return found->second;
        }
        const auto source = function.values[id.value];
        if (!definitions[id.value] ||
            !pattern.loop.blocks.contains(definitions[id.value]->value) ||
            source.kind == ValueKind::ConstantInteger ||
            source.kind == ValueKind::ConstantFloating) {
            const auto result = splat_scalar(id);
            vector_values.emplace(id.value, result);
            return result;
        }
        if (source.kind == ValueKind::Intrinsic) {
            const auto result = vectorize_value(source.operands.front());
            vector_values.emplace(id.value, result);
            return result;
        }
        if (source.kind == ValueKind::PointerLoad ||
            source.kind == ValueKind::IndexedLoad) {
            ValueId base;
            if (source.kind == ValueKind::PointerLoad) {
                const auto address =
                    function.values[source.operands.front().value];
                base = address.operands.front();
            } else {
                base = source.operands.front();
            }
            ManagedValue load;
            load.location = source.location;
            load.type = vector_type_for(source.type);
            load.kind = ValueKind::IndexedLoad;
            load.is_volatile_access = false;
            load.memory_alignment = source.memory_alignment;
            load.operands = {base, active_vector_index};
            const auto result = append_effectful(std::move(load));
            vector_values.emplace(id.value, result);
            return result;
        }
        // AVX2 has signed packed integer ordering but no unsigned packed
        // ordering. Biasing both operands by their sign bit maps unsigned
        // order to signed order. Express that mapping in MIR so LICM and CSE
        // can hoist the shared bias and every biased loop-invariant bound;
        // leaving it hidden inside the x86 compare expansion would rebuild
        // the same mask for every compare on every trip.
        const auto signed_ordering = [](BinaryOperation operation)
            -> std::optional<BinaryOperation> {
            switch (operation) {
            case BinaryOperation::UnsignedLess:
                return BinaryOperation::SignedLess;
            case BinaryOperation::UnsignedLessEqual:
                return BinaryOperation::SignedLessEqual;
            case BinaryOperation::UnsignedGreater:
                return BinaryOperation::SignedGreater;
            case BinaryOperation::UnsignedGreaterEqual:
                return BinaryOperation::SignedGreaterEqual;
            default:
                return std::nullopt;
            }
        };
        if (source.kind == ValueKind::Binary &&
            source.operands.size() == 2) {
            const auto signed_operation = signed_ordering(source.binary);
            const auto element_type =
                function.values[source.operands.front().value].type;
            const auto bits = type_bits(hir_module, element_type);
            const auto mask_element = bits == 8
                ? hir_module.builtin(BuiltinType::I8)
                : bits == 16
                ? hir_module.builtin(BuiltinType::I16)
                : bits == 32
                ? hir_module.builtin(BuiltinType::I32)
                : hir_module.builtin(BuiltinType::I64);
            // Unsigned ordering at the signed boundary is just a sign test:
            //   x <  2^(n-1)  == signed(x) >= 0
            //   x >= 2^(n-1)  == signed(x) <  0
            // and the adjacent <=/> forms have the same split. Preserve
            // that canonical form so targets can consume the sign bit
            // directly in a select without materializing a compare mask.
            std::optional<BinaryOperation> boundary_operation;
            if (integer_type(hir_module, element_type) &&
                bits >= 8 && bits <= 64) {
                const auto& right =
                    function.values[source.operands[1].value];
                if (right.kind == ValueKind::ConstantInteger &&
                    right.integer_high == 0) {
                    const auto sign_bit =
                        std::uint64_t{1} << (bits - 1U);
                    if ((source.binary == BinaryOperation::UnsignedLess &&
                         right.integer == sign_bit) ||
                        (source.binary ==
                             BinaryOperation::UnsignedLessEqual &&
                         right.integer == sign_bit - 1U)) {
                        boundary_operation =
                            BinaryOperation::SignedGreaterEqual;
                    } else if ((source.binary ==
                                    BinaryOperation::UnsignedGreaterEqual &&
                                right.integer == sign_bit) ||
                               (source.binary ==
                                    BinaryOperation::UnsignedGreater &&
                                right.integer == sign_bit - 1U)) {
                        boundary_operation =
                            BinaryOperation::SignedLess;
                    }
                }
            }
            if (boundary_operation) {
                ManagedValue compare;
                compare.location = source.location;
                compare.type = hir_module.vector_of(*mask_element, lanes);
                compare.kind = ValueKind::Binary;
                compare.binary = *boundary_operation;
                compare.operands = {
                    vectorize_value(source.operands[0]),
                    comparison_zero(element_type)};
                const auto result = append_value(
                    vector_body.values, std::move(compare));
                vector_values.emplace(id.value, result);
                return result;
            }
            if (signed_operation && integer_type(hir_module, element_type) &&
                bits >= 8 && bits <= 64) {
                const auto bias = comparison_bias(element_type);
                const auto append_biased = [&](ValueId operand) {
                    ManagedValue biased;
                    biased.location = source.location;
                    biased.type = vector_type_for(element_type);
                    biased.kind = ValueKind::Binary;
                    biased.binary = BinaryOperation::BitXor;
                    biased.operands = {vectorize_value(operand), bias};
                    return append_value(
                        vector_body.values, std::move(biased));
                };
                const auto left = append_biased(source.operands[0]);
                const auto right = append_biased(source.operands[1]);
                ManagedValue compare;
                compare.location = source.location;
                compare.type = hir_module.vector_of(*mask_element, lanes);
                compare.kind = ValueKind::Binary;
                compare.binary = *signed_operation;
                compare.operands = {left, right};
                const auto result = append_value(
                    vector_body.values, std::move(compare));
                vector_values.emplace(id.value, result);
                return result;
            }
        }
        ManagedValue transformed;
        transformed.location = source.location;
        transformed.type = vector_type_for(source.type);
        if (source.kind == ValueKind::Binary &&
            comparison(source.binary) && !source.operands.empty()) {
            const auto bits = type_bits(
                hir_module,
                function.values[source.operands.front().value].type);
            const auto mask_element = bits == 8
                ? hir_module.builtin(BuiltinType::I8)
                : bits == 16
                ? hir_module.builtin(BuiltinType::I16)
                : bits == 32
                ? hir_module.builtin(BuiltinType::I32)
                : hir_module.builtin(BuiltinType::I64);
            transformed.type = hir_module.vector_of(*mask_element, lanes);
        }
        transformed.kind = source.kind;
        transformed.unary = source.unary;
        transformed.binary = source.binary;
        transformed.cast = source.cast;
        for (const auto operand : source.operands) {
            transformed.operands.push_back(vectorize_value(operand));
        }
        const auto result =
            append_value(vector_body.values, std::move(transformed));
        vector_values.emplace(id.value, result);
        return result;
    };

    std::vector<ValueId> vector_terms;
    vector_terms.reserve(interleave);
    std::vector<ValueId> carrier_next;
    carrier_next.reserve(interleave);
    std::vector<ValueId> active_affine_carriers;
    active_affine_carriers.reserve(affine_states.size());
    for (const auto& affine : affine_states) {
        active_affine_carriers.push_back(affine.carrier);
    }
    for (unsigned group = 0; group < interleave; ++group) {
        if (group == 0) {
            active_vector_index = vector_index;
            active_lane_index = vector_lane_index;
        } else {
            ManagedValue group_index;
            group_index.location =
                function.values[pattern.index_next.value].location;
            group_index.type = index_type;
            group_index.kind = ValueKind::Binary;
            group_index.binary = BinaryOperation::Add;
            group_index.operands = {
                vector_index,
                add_integer_constant(index_type, group * lanes)};
            active_vector_index = append_value(
                vector_body.values, std::move(group_index));
            ManagedValue group_lane_index;
            group_lane_index.location =
                function.values[pattern.index_next.value].location;
            group_lane_index.type = vector_type_for(index_type);
            group_lane_index.kind = ValueKind::Binary;
            group_lane_index.binary = BinaryOperation::Add;
            group_lane_index.operands = {
                active_lane_index, index_lane_step};
            active_lane_index = append_value(
                vector_body.values, std::move(group_lane_index));
        }
        vector_values.clear();

        active_affine_values.clear();
        for (std::size_t affine_index = 0;
             affine_index < affine_states.size(); ++affine_index) {
            const auto& affine = affine_states[affine_index];
            auto active = active_affine_carriers[affine_index];
            if (group != 0) {
                ManagedValue offset;
                offset.location =
                    function.values[affine.source.phi.value].location;
                offset.type = function.values[affine.carrier.value].type;
                offset.kind = ValueKind::Binary;
                offset.binary = BinaryOperation::Add;
                offset.operands = {
                    active, affine.advance_vector};
                active = append_value(
                    vector_body.values, std::move(offset));
                active_affine_carriers[affine_index] = active;
            }
            active_affine_values.emplace(
                affine.source.phi.value, active);
        }
        const auto vector_term = vectorize_value(pattern.term);
        if (reassociate) {
            ManagedValue update;
            update.location =
                function.values[pattern.reduction_next.value].location;
            update.type = vector_type;
            update.kind = ValueKind::Binary;
            update.binary = pattern.reduction_operation;
            update.operands = {carriers[group], vector_term};
            carrier_next.push_back(
                append_value(vector_body.values, std::move(update)));
        } else {
            vector_terms.push_back(vector_term);
        }
        for (const auto& store : pattern.stores) {
            const auto stored = vectorize_value(store.value);
            auto address = function.values[store.address.value];
            address.id = {};
            address.effect_input.reset();
            address.effect_output.reset();
            address.operands = {store.base, active_vector_index};
            const auto address_id = append_value(
                vector_body.values, std::move(address));
            auto vector_store = function.values[store.store.value];
            vector_store.id = {};
            vector_store.effect_input.reset();
            vector_store.effect_output.reset();
            vector_store.operands = {address_id, stored};
            (void)append_effectful(std::move(vector_store));
        }
    }
    if (!reassociate) {
        auto next = carriers.front();
        for (unsigned lane = 0; lane < lanes; ++lane) {
            ManagedValue extract;
            extract.location = function.values[pattern.term.value].location;
            extract.type = scalar_type;
            extract.kind = ValueKind::ExtractElement;
            extract.operands = {
                vector_terms.front(), lane_constants[lane]};
            const auto element =
                append_value(vector_body.values, std::move(extract));
            ManagedValue update;
            update.location =
                function.values[pattern.reduction_next.value].location;
            update.type = scalar_type;
            update.kind = ValueKind::Binary;
            update.binary = BinaryOperation::Add;
            update.operands = {next, element};
            next =
                append_value(vector_body.values, std::move(update));
        }
        carrier_next.push_back(next);
    }
    for (std::size_t affine_index = 0;
         affine_index < affine_states.size(); ++affine_index) {
        auto& affine = affine_states[affine_index];
        ManagedValue update;
        update.location =
            function.values[affine.source.next.value].location;
        update.type = function.values[affine.carrier.value].type;
        update.kind = ValueKind::Binary;
        update.binary = BinaryOperation::Add;
        update.operands = {
            active_affine_carriers[affine_index],
            affine.advance_vector};
        affine.next =
            append_value(vector_body.values, std::move(update));
        function.values[affine.carrier.value].incoming[1].value =
            affine.next;
    }
    ManagedValue vector_index_next;
    vector_index_next.location =
        function.values[pattern.index_next.value].location;
    vector_index_next.type = index_type;
    vector_index_next.kind = ValueKind::Binary;
    vector_index_next.binary = BinaryOperation::Add;
    vector_index_next.operands = {vector_index, vector_step_constant};
    const auto vector_index_next_id =
        append_value(vector_body.values, std::move(vector_index_next));
    function.values[vector_index.value].incoming[1].value =
        vector_index_next_id;
    ManagedValue vector_lane_index_next;
    vector_lane_index_next.location =
        function.values[pattern.index_next.value].location;
    vector_lane_index_next.type = vector_type_for(index_type);
    vector_lane_index_next.kind = ValueKind::Binary;
    vector_lane_index_next.binary = BinaryOperation::Add;
    vector_lane_index_next.operands = {
        active_lane_index, index_lane_step};
    const auto vector_lane_index_next_id = append_value(
        vector_body.values, std::move(vector_lane_index_next));
    function.values[vector_lane_index.value].incoming[1].value =
        vector_lane_index_next_id;
    for (unsigned group = 0; group < interleave; ++group) {
        function.values[carriers[group].value].incoming[1].value =
            carrier_next[group];
    }

    ValueId scalar_reduction = carriers.front();
    if (reassociate) {
        auto combined_carrier = carriers.front();
        for (unsigned group = 1; group < interleave; ++group) {
            ManagedValue combine_vectors;
            combine_vectors.location = vector_exit.location;
            combine_vectors.type = vector_type;
            combine_vectors.kind = ValueKind::Binary;
            combine_vectors.binary = pattern.reduction_operation;
            combine_vectors.operands = {
                combined_carrier, carriers[group]};
            combined_carrier = append_value(
                vector_exit.values, std::move(combine_vectors));
        }
        scalar_reduction = pattern.initial_reduction;
        for (unsigned lane = 0; lane < lanes; ++lane) {
            ManagedValue extract;
            extract.location = vector_exit.location;
            extract.type = scalar_type;
            extract.kind = ValueKind::ExtractElement;
            extract.operands = {combined_carrier, lane_constants[lane]};
            const auto element =
                append_value(vector_exit.values, std::move(extract));
            ManagedValue combine;
            combine.location = vector_exit.location;
            combine.type = scalar_type;
            combine.kind = ValueKind::Binary;
            combine.binary = pattern.reduction_operation;
            combine.operands = {scalar_reduction, element};
            scalar_reduction =
                append_value(vector_exit.values, std::move(combine));
        }
    }
    std::unordered_map<std::uint32_t, ValueId> scalar_affine_values;
    for (const auto& affine : affine_states) {
        ManagedValue extract;
        extract.location = vector_exit.location;
        extract.type = function.values[affine.source.phi.value].type;
        extract.kind = ValueKind::ExtractElement;
        extract.operands = {affine.carrier, lane_constants.front()};
        const auto scalar =
            append_value(vector_exit.values, std::move(extract));
        scalar_affine_values.emplace(affine.source.phi.value, scalar);
    }

    vector_header.terminator.kind =
        TerminatorKind::ConditionalBranch;
    vector_header.terminator.location = vector_header.location;
    vector_header.terminator.value = vector_condition_id;
    vector_header.terminator.successors = {
        vector_body_id, vector_exit_id};
    vector_header.terminator.effect = vector_header_effect;
    vector_body.terminator.kind = TerminatorKind::Branch;
    vector_body.terminator.location = vector_body.location;
    vector_body.terminator.successors = {vector_header_id};
    vector_body.terminator.effect = current_effect;
    vector_exit.terminator.kind = TerminatorKind::Branch;
    vector_exit.terminator.location = vector_exit.location;
    vector_exit.terminator.successors = {pattern.loop.header};
    vector_exit.terminator.effect = vector_exit_effect;
    if (needs_alias_guard) {
        alias_guard.terminator.kind =
            TerminatorKind::ConditionalBranch;
        alias_guard.terminator.location = alias_guard.location;
        alias_guard.terminator.value = alias_condition;
        alias_guard.terminator.successors = {
            vector_header_id, pattern.loop.header};
        alias_guard.terminator.effect = *alias_guard_effect;
    }
    function.effects[vector_header_effect.value].incoming.push_back(
        {vector_body_id, current_effect});

    auto& preheader = function.blocks[pattern.loop.preheader.value];
    std::replace(preheader.terminator.successors.begin(),
                 preheader.terminator.successors.end(),
                 pattern.loop.header,
                 needs_alias_guard ? alias_guard_id : vector_header_id);
    auto& header = function.blocks[pattern.loop.header.value];
    if (needs_alias_guard) {
        std::replace(header.predecessors.begin(),
                     header.predecessors.end(),
                     pattern.loop.preheader, alias_guard_id);
        header.predecessors.push_back(vector_exit_id);
    } else {
        std::replace(header.predecessors.begin(),
                     header.predecessors.end(),
                     pattern.loop.preheader, vector_exit_id);
    }
    for (const auto id : header.values) {
        auto& value = function.values[id.value];
        if (value.kind != ValueKind::Phi) continue;
        const auto incoming = std::find_if(
            value.incoming.begin(), value.incoming.end(),
            [&](const PhiIncoming& edge) {
                return edge.predecessor == pattern.loop.preheader;
            });
        if (incoming == value.incoming.end()) continue;
        const auto original_initial = incoming->value;
        ValueId vector_initial = original_initial;
        if (id == pattern.index) vector_initial = vector_index;
        else if (id == pattern.reduction) {
            vector_initial = scalar_reduction;
        } else if (const auto found =
                       scalar_affine_values.find(id.value);
                   found != scalar_affine_values.end()) {
            vector_initial = found->second;
        }
        if (needs_alias_guard) {
            incoming->predecessor = alias_guard_id;
            value.incoming.push_back(
                {vector_exit_id, vector_initial});
        } else {
            incoming->predecessor = vector_exit_id;
            incoming->value = vector_initial;
        }
    }
    auto& header_effect = function.effects[header.effect.value];
    const auto incoming_effect = std::find_if(
        header_effect.incoming.begin(), header_effect.incoming.end(),
        [&](const EffectIncoming& edge) {
            return edge.predecessor == pattern.loop.preheader;
        });
    if (incoming_effect != header_effect.incoming.end()) {
        if (needs_alias_guard) {
            incoming_effect->predecessor = alias_guard_id;
            incoming_effect->effect = *alias_guard_effect;
            header_effect.incoming.push_back(
                {vector_exit_id, vector_exit_effect});
        } else {
            incoming_effect->predecessor = vector_exit_id;
            incoming_effect->effect = vector_exit_effect;
        }
    }
    if (needs_alias_guard) {
        function.blocks.push_back(std::move(alias_guard));
    }
    function.blocks.push_back(std::move(vector_header));
    function.blocks.push_back(std::move(vector_body));
    function.blocks.push_back(std::move(vector_exit));
    return true;
}

struct SlpTree {
    std::vector<ValueId> lanes;
    bool operation{};
    std::vector<SlpTree> operands;
};

struct SlpCost {
    unsigned scalar{};
    unsigned vector{};
    unsigned operations{};
    std::unordered_set<std::string> accounted_operations;
    std::unordered_set<std::string> accounted_leaves;
};

std::string slp_lane_key(const std::vector<ValueId>& lanes) {
    std::string result;
    result.reserve(lanes.size() * 8U);
    for (const auto lane : lanes) {
        result += std::to_string(lane.value);
        result.push_back(',');
    }
    return result;
}

bool slp_scalar_type(const hir::Module& hir_module, hir::TypeId type) {
    return floating_type(hir_module, type) &&
           (type_bits(hir_module, type) == 32 ||
            type_bits(hir_module, type) == 64);
}

bool slp_operation(const ManagedValue& value,
                   const hir::Module& hir_module) {
    if (is_effectful_value(value) || !slp_scalar_type(hir_module, value.type)) {
        return false;
    }
    if (value.kind == ValueKind::Unary) {
        return value.operands.size() == 1 &&
               value.unary == UnaryOperation::Negate;
    }
    if (value.kind != ValueKind::Binary || value.operands.size() != 2) {
        return false;
    }
    return value.binary == BinaryOperation::Add ||
           value.binary == BinaryOperation::Subtract ||
           value.binary == BinaryOperation::Multiply ||
           value.binary == BinaryOperation::SignedDivide;
}

bool same_slp_operation(const ManagedValue& left,
                        const ManagedValue& right) {
    if (left.kind != right.kind || left.type != right.type ||
        left.operands.size() != right.operands.size()) {
        return false;
    }
    if (left.kind == ValueKind::Unary) return left.unary == right.unary;
    if (left.kind == ValueKind::Binary) return left.binary == right.binary;
    return false;
}

unsigned slp_operation_cost(const ManagedValue& value) {
    if (value.kind == ValueKind::Binary) {
        if (value.binary == BinaryOperation::SignedDivide) return 8U;
        if (value.binary == BinaryOperation::Multiply) return 2U;
    }
    return 1U;
}

bool equivalent_slp_leaf(const ManagedValue& left,
                         const ManagedValue& right) {
    if (left.id == right.id) return true;
    if (left.kind != right.kind || left.type != right.type) return false;
    return (left.kind == ValueKind::ConstantInteger ||
            left.kind == ValueKind::ConstantFloating) &&
           left.integer == right.integer &&
           left.integer_high == right.integer_high;
}

std::string slp_shape(
    const ManagedFunction& function, const hir::Module& hir_module,
    const std::vector<std::optional<BlockId>>& definitions,
    BlockId block, ValueId id,
    const std::unordered_set<std::uint32_t>& claimed,
    unsigned depth = 0) {
    if (depth >= 32 || id.value >= function.values.size() ||
        claimed.contains(id.value) || !definitions[id.value] ||
        *definitions[id.value] != block ||
        !slp_operation(function.values[id.value], hir_module)) {
        return "L";
    }
    const auto& value = function.values[id.value];
    std::string result = value.kind == ValueKind::Unary ? "U" : "B";
    result += std::to_string(
        value.kind == ValueKind::Unary
            ? static_cast<unsigned>(value.unary)
            : static_cast<unsigned>(value.binary));
    result.push_back('(');
    for (const auto operand : value.operands) {
        result += slp_shape(function, hir_module, definitions, block,
                            operand, claimed, depth + 1U);
        result.push_back(';');
    }
    result.push_back(')');
    return result;
}

bool slp_depends_on(const ManagedFunction& function,
                    const std::vector<std::optional<BlockId>>& definitions,
                    BlockId block, ValueId root, ValueId possible_dependency,
                    unsigned depth = 0) {
    if (root == possible_dependency) return true;
    if (depth >= 64 || root.value >= function.values.size() ||
        !definitions[root.value] || *definitions[root.value] != block) {
        return false;
    }
    return std::any_of(
        function.values[root.value].operands.begin(),
        function.values[root.value].operands.end(),
        [&](ValueId operand) {
            return slp_depends_on(function, definitions, block, operand,
                                  possible_dependency, depth + 1U);
        });
}

SlpTree build_slp_tree(
    const ManagedFunction& function, const hir::Module& hir_module,
    const std::vector<std::optional<BlockId>>& definitions,
    BlockId block, std::vector<ValueId> lanes,
    const std::unordered_set<std::uint32_t>& claimed,
    SlpCost& cost,
    std::unordered_set<std::uint32_t>& operation_nodes) {
    SlpTree result;
    result.lanes = std::move(lanes);
    const auto key = slp_lane_key(result.lanes);
    const auto& first = function.values[result.lanes.front().value];
    const bool all_equivalent = std::all_of(
        result.lanes.begin() + 1, result.lanes.end(),
        [&](ValueId id) {
            return equivalent_slp_leaf(
                first, function.values[id.value]);
        });
    const bool all_distinct = [&] {
        std::unordered_set<std::uint32_t> unique;
        for (const auto id : result.lanes) unique.insert(id.value);
        return unique.size() == result.lanes.size();
    }();
    const bool operations = !all_equivalent && all_distinct &&
        std::all_of(result.lanes.begin(), result.lanes.end(),
                    [&](ValueId id) {
                        return !claimed.contains(id.value) &&
                               definitions[id.value] &&
                               *definitions[id.value] == block &&
                               slp_operation(function.values[id.value],
                                             hir_module) &&
                               same_slp_operation(
                                   first, function.values[id.value]);
                    });
    if (!operations) {
        if (cost.accounted_leaves.insert(key).second) {
            cost.vector += all_equivalent
                               ? 1U
                               : static_cast<unsigned>(result.lanes.size());
        }
        return result;
    }

    result.operation = true;
    if (cost.accounted_operations.insert(key).second) {
        const auto weight = slp_operation_cost(first);
        cost.scalar += weight * static_cast<unsigned>(result.lanes.size());
        cost.vector += weight;
        ++cost.operations;
    }
    for (const auto id : result.lanes) operation_nodes.insert(id.value);
    for (std::size_t operand = 0; operand < first.operands.size(); ++operand) {
        std::vector<ValueId> child_lanes;
        child_lanes.reserve(result.lanes.size());
        for (const auto id : result.lanes) {
            child_lanes.push_back(
                function.values[id.value].operands[operand]);
        }
        result.operands.push_back(build_slp_tree(
            function, hir_module, definitions, block,
            std::move(child_lanes), claimed, cost, operation_nodes));
    }
    return result;
}

bool vectorize_slp_group(
    ManagedFunction& function, hir::Module& hir_module,
    BlockId block_id, const SlpTree& tree, unsigned lanes,
    std::unordered_set<std::uint32_t>& claimed) {
    auto& block = function.blocks[block_id.value];
    const auto scalar_type = function.values[tree.lanes.front().value].type;
    const auto vector_type = hir_module.vector_of(scalar_type, lanes);
    const auto index_type = *hir_module.builtin(BuiltinType::Uptr);
    std::vector<ValueId> inserted;
    const auto append = [&](ManagedValue value) {
        value.id = {static_cast<std::uint32_t>(function.values.size())};
        const auto id = value.id;
        function.values.push_back(std::move(value));
        inserted.push_back(id);
        return id;
    };

    std::vector<ValueId> indices;
    indices.reserve(lanes);
    for (unsigned lane = 0; lane < lanes; ++lane) {
        ManagedValue index;
        index.location = function.values[tree.lanes.front().value].location;
        index.type = index_type;
        index.kind = ValueKind::ConstantInteger;
        index.integer = lane;
        indices.push_back(append(std::move(index)));
    }

    std::unordered_map<std::string, ValueId> emitted;
    std::function<ValueId(const SlpTree&)> emit =
        [&](const SlpTree& node) -> ValueId {
        const auto key = slp_lane_key(node.lanes);
        if (const auto found = emitted.find(key); found != emitted.end()) {
            return found->second;
        }
        ValueId result;
        if (!node.operation) {
            ManagedValue splat;
            splat.location =
                function.values[node.lanes.front().value].location;
            splat.type = vector_type;
            splat.kind = ValueKind::Splat;
            splat.operands = {node.lanes.front()};
            result = append(std::move(splat));
            const auto& first =
                function.values[node.lanes.front().value];
            for (unsigned lane = 1; lane < lanes; ++lane) {
                if (equivalent_slp_leaf(
                        first, function.values[node.lanes[lane].value])) {
                    continue;
                }
                ManagedValue insert;
                insert.location =
                    function.values[node.lanes[lane].value].location;
                insert.type = vector_type;
                insert.kind = ValueKind::InsertElement;
                insert.operands = {result, indices[lane], node.lanes[lane]};
                result = append(std::move(insert));
            }
        } else {
            const auto& source =
                function.values[node.lanes.front().value];
            ManagedValue operation;
            operation.location = source.location;
            operation.type = vector_type;
            operation.kind = source.kind;
            operation.unary = source.unary;
            operation.binary = source.binary;
            for (const auto& operand : node.operands) {
                operation.operands.push_back(emit(operand));
            }
            result = append(std::move(operation));
        }
        emitted.emplace(key, result);
        return result;
    };

    const auto packed = emit(tree);
    std::vector<ValueId> extracts;
    extracts.reserve(lanes);
    for (unsigned lane = 0; lane < lanes; ++lane) {
        ManagedValue extract;
        extract.location = function.values[tree.lanes[lane].value].location;
        extract.type = scalar_type;
        extract.kind = ValueKind::ExtractElement;
        extract.operands = {packed, indices[lane]};
        extracts.push_back(append(std::move(extract)));
    }

    std::size_t insertion = 0;
    for (const auto root : tree.lanes) {
        const auto found = std::find(block.values.begin(),
                                     block.values.end(), root);
        if (found == block.values.end()) return false;
        insertion = std::max(
            insertion,
            static_cast<std::size_t>(found - block.values.begin()) + 1U);
    }
    block.values.insert(
        block.values.begin() + static_cast<std::ptrdiff_t>(insertion),
        inserted.begin(), inserted.end());
    for (unsigned lane = 0; lane < lanes; ++lane) {
        replace_value_uses(function, tree.lanes[lane], extracts[lane]);
    }
    for (const auto id : tree.lanes) claimed.insert(id.value);
    return true;
}

void vectorize_slp(ManagedFunction& function, hir::Module& hir_module,
                   const Subtarget& subtarget,
                   const CompilerOptions& options) {
    if (options.optimize_for != OptimizationGoal::Speed) return;
    // Prefer the smallest native packed width for isolated trees: wider
    // groups increase packing latency and register pressure without the
    // amortization available to a counted loop.
    unsigned vector_bits{};
    for (const auto& width : subtarget.target().native_vector_widths) {
        const auto feature = width.floating_feature;
        if (!feature.empty() && !subtarget.has_feature(feature)) continue;
        if (width.bits < 128) continue;
        if (vector_bits == 0 || width.bits < vector_bits) {
            vector_bits = width.bits;
        }
    }
    if (vector_bits == 0) return;

    const auto definitions = value_definition_blocks(function);
    std::unordered_set<std::uint32_t> claimed;
    for (auto& block : function.blocks) {
        const auto original_values = block.values;
        for (auto cursor = original_values.rbegin();
             cursor != original_values.rend(); ++cursor) {
            const auto root = *cursor;
            if (claimed.contains(root.value) ||
                !slp_operation(function.values[root.value], hir_module)) {
                continue;
            }
            const auto element_bits =
                type_bits(hir_module, function.values[root.value].type);
            if (element_bits == 0 || vector_bits % element_bits != 0) {
                continue;
            }
            const auto lanes = vector_bits / element_bits;
            if (lanes < 2 || lanes > 8) continue;
            const auto shape = slp_shape(
                function, hir_module, definitions, block.id, root, claimed);
            if (shape == "L") continue;

            std::vector<ValueId> roots{root};
            for (auto candidate = cursor + 1;
                 candidate != original_values.rend() &&
                 roots.size() < lanes; ++candidate) {
                if (claimed.contains(candidate->value) ||
                    function.values[candidate->value].type !=
                        function.values[root.value].type ||
                    slp_shape(function, hir_module, definitions, block.id,
                              *candidate, claimed) != shape) {
                    continue;
                }
                const bool dependent = std::any_of(
                    roots.begin(), roots.end(), [&](ValueId prior) {
                        return slp_depends_on(function, definitions, block.id,
                                              prior, *candidate) ||
                               slp_depends_on(function, definitions, block.id,
                                              *candidate, prior);
                    });
                if (!dependent) roots.push_back(*candidate);
            }
            if (roots.size() != lanes) continue;
            std::reverse(roots.begin(), roots.end());

            SlpCost cost;
            std::unordered_set<std::uint32_t> nodes;
            const auto tree = build_slp_tree(
                function, hir_module, definitions, block.id,
                roots, claimed, cost, nodes);
            cost.vector += lanes; // materialize scalar users from packed root
            if (!tree.operation || cost.operations < 2 ||
                cost.scalar <= cost.vector + 1U) {
                continue;
            }
            if (!vectorize_slp_group(function, hir_module, block.id, tree,
                                     lanes, claimed)) {
                continue;
            }
            claimed.insert(nodes.begin(), nodes.end());
        }
    }
}

void vectorize_reduction_loops(ManagedFunction& function,
                               hir::Module& hir_module,
                               const Subtarget& subtarget,
                               const CompilerOptions& options,
                               std::span<const CanonicalLoop> loops) {
    if (options.optimize_for != OptimizationGoal::Speed) return;
    const auto definitions = value_definition_blocks(function);
    for (const auto& loop : loops) {
        auto pattern =
            find_reduction_loop(function, hir_module, loop);
        if (!pattern) continue;
        const auto scalar_type =
            function.values[pattern->reduction.value].type;
        const bool floating = floating_type(hir_module, scalar_type);
        unsigned arithmetic_operations{};
        std::unordered_set<std::uint32_t> required;
        bool term_vectorizable = vectorizable_reduction_term(
                function, hir_module, *pattern, definitions,
                pattern->term, required, arithmetic_operations);
        for (const auto& store : pattern->stores) {
            required.insert(store.store.value);
            required.insert(store.address.value);
            term_vectorizable = term_vectorizable &&
                vectorizable_reduction_term(
                    function, hir_module, *pattern, definitions,
                    store.value, required, arithmetic_operations);
        }
        if (!term_vectorizable) continue;
        pattern->load_bases.clear();
        for (const auto id :
             function.blocks[pattern->body.value].values) {
            if (!required.contains(id.value)) continue;
            const auto& value = function.values[id.value];
            std::optional<ValueId> base;
            if (value.kind == ValueKind::IndexedLoad &&
                value.operands.size() == 2) {
                base = value.operands[0];
            } else if (value.kind == ValueKind::PointerLoad &&
                       value.operands.size() == 1) {
                const auto& address =
                    function.values[value.operands[0].value];
                if (address.kind == ValueKind::IndexedAddress &&
                    address.operands.size() == 2) {
                    base = address.operands[0];
                }
            }
            if (base &&
                std::find(pattern->load_bases.begin(),
                          pattern->load_bases.end(), *base) ==
                    pattern->load_bases.end()) {
                pattern->load_bases.push_back(*base);
            }
        }
        if (!pattern->stores.empty()) {
            const auto& body_values =
                function.blocks[pattern->body.value].values;
            const auto store_position = std::find(
                body_values.begin(), body_values.end(),
                pattern->stores.front().store);
            const bool load_after_store =
                store_position != body_values.end() &&
                std::any_of(store_position + 1, body_values.end(),
                            [&](ValueId id) {
                                const auto& value =
                                    function.values[id.value];
                                return required.contains(id.value) &&
                                    (value.kind == ValueKind::PointerLoad ||
                                     value.kind == ValueKind::IndexedLoad);
                            });
            if (load_after_store) continue;
        }
        bool unrelated_effect = false;
        for (const auto id : function.blocks[pattern->body.value].values) {
            const auto& value = function.values[id.value];
            if ((value.effect_input || value.effect_output) &&
                !required.contains(id.value)) {
                unrelated_effect = true;
                break;
            }
        }
        // Strict floating reductions preserve scalar addition order. The
        // vectorized expression must contain enough arithmetic to amortize
        // lane extraction and those ordered additions.
        if (unrelated_effect ||
            (floating && !options.fast_math &&
             arithmetic_operations < 2)) {
            continue;
        }
        const auto width = preferred_vector_bits(
            subtarget, options, floating,
            type_bits(hir_module, scalar_type));
        if (width != 0) {
            (void)vectorize_reduction_loop(
                function, hir_module, *pattern, definitions,
                required, width, subtarget, options);
        }
    }
}

struct EarlyExitLoopPattern {
    CanonicalLoop loop;
    BlockId test;
    BlockId latch;
    BlockId exit;
    ValueId index;
    ValueId initial_index;
    ValueId bound;
    ValueId index_next;
    ValueId base;
    ValueId load;
    ValueId compared_load;
    ValueId invariant;
    BinaryOperation continuation{BinaryOperation::UnsignedLess};
    bool load_on_left{};
};

std::optional<BinaryOperation> negated_integer_comparison(
    BinaryOperation operation) {
    switch (operation) {
    case BinaryOperation::Equal: return BinaryOperation::NotEqual;
    case BinaryOperation::NotEqual: return BinaryOperation::Equal;
    case BinaryOperation::SignedLess:
        return BinaryOperation::SignedGreaterEqual;
    case BinaryOperation::SignedLessEqual:
        return BinaryOperation::SignedGreater;
    case BinaryOperation::SignedGreater:
        return BinaryOperation::SignedLessEqual;
    case BinaryOperation::SignedGreaterEqual:
        return BinaryOperation::SignedLess;
    case BinaryOperation::UnsignedLess:
        return BinaryOperation::UnsignedGreaterEqual;
    case BinaryOperation::UnsignedLessEqual:
        return BinaryOperation::UnsignedGreater;
    case BinaryOperation::UnsignedGreater:
        return BinaryOperation::UnsignedLessEqual;
    case BinaryOperation::UnsignedGreaterEqual:
        return BinaryOperation::UnsignedLess;
    default: return std::nullopt;
    }
}

std::optional<EarlyExitLoopPattern> find_early_exit_loop(
    const ManagedFunction& function, const hir::Module& hir_module,
    const CanonicalLoop& loop,
    const std::vector<std::optional<BlockId>>& definitions,
    const UseLists& uses) {
    if (loop.blocks.size() != 3 ||
        std::any_of(function.labels.begin(), function.labels.end(),
                    [&](const ManagedLabel& label) {
                        return loop.blocks.contains(label.block.value);
                    })) {
        return std::nullopt;
    }
    const auto& header = function.blocks[loop.header.value];
    if (header.predecessors.size() != 2 ||
        header.terminator.kind != TerminatorKind::ConditionalBranch ||
        !header.terminator.value ||
        header.terminator.successors.size() != 2) {
        return std::nullopt;
    }
    const auto& range = function.values[header.terminator.value->value];
    if (range.kind != ValueKind::Binary ||
        range.binary != BinaryOperation::UnsignedLess ||
        range.operands.size() != 2) {
        return std::nullopt;
    }
    const auto bound_definition = definitions[range.operands[1].value];
    if (bound_definition &&
        loop.blocks.contains(bound_definition->value)) {
        return std::nullopt;
    }
    const auto index_id = range.operands.front();
    const auto& index = function.values[index_id.value];
    if (index.kind != ValueKind::Phi || index.incoming.size() != 2 ||
        !integer_type(hir_module, index.type)) {
        return std::nullopt;
    }
    for (const auto id : header.values) {
        if (id != index_id &&
            function.values[id.value].kind == ValueKind::Phi) {
            return std::nullopt;
        }
    }
    const auto initial_index = phi_value_from(index, loop.preheader);
    const auto backedge = std::find_if(
        index.incoming.begin(), index.incoming.end(),
        [&](const PhiIncoming& incoming) {
            return incoming.predecessor != loop.preheader;
        });
    if (!initial_index || backedge == index.incoming.end() ||
        !loop.blocks.contains(backedge->predecessor.value)) {
        return std::nullopt;
    }
    const auto& initial = function.values[initial_index->value];
    if (initial.kind != ValueKind::ConstantInteger || initial.integer != 0 ||
        initial.integer_high != 0) {
        return std::nullopt;
    }

    const auto test_id = header.terminator.successors.front();
    const auto exit_id = header.terminator.successors.back();
    const auto latch_id = backedge->predecessor;
    if (!loop.blocks.contains(test_id.value) || test_id == loop.header ||
        !loop.blocks.contains(latch_id.value) || latch_id == loop.header ||
        latch_id == test_id || loop.blocks.contains(exit_id.value)) {
        return std::nullopt;
    }
    const auto& test = function.blocks[test_id.value];
    const auto& latch = function.blocks[latch_id.value];
    if (test.predecessors.size() != 1 ||
        test.predecessors.front() != loop.header ||
        test.terminator.kind != TerminatorKind::ConditionalBranch ||
        !test.terminator.value || test.terminator.successors.size() != 2 ||
        test.terminator.successors.front() != latch_id ||
        test.terminator.successors.back() != exit_id ||
        latch.predecessors.size() != 1 ||
        latch.predecessors.front() != test_id ||
        latch.terminator.kind != TerminatorKind::Branch ||
        latch.terminator.successors.size() != 1 ||
        latch.terminator.successors.front() != loop.header) {
        return std::nullopt;
    }
    const auto index_next = backedge->value;
    const auto& next = function.values[index_next.value];
    if (next.kind != ValueKind::Binary ||
        next.binary != BinaryOperation::Add || next.operands.size() != 2) {
        return std::nullopt;
    }
    std::optional<ValueId> step;
    if (next.operands[0] == index_id) step = next.operands[1];
    else if (next.operands[1] == index_id) step = next.operands[0];
    if (!step) return std::nullopt;
    const auto& step_value = function.values[step->value];
    if (step_value.kind != ValueKind::ConstantInteger ||
        step_value.integer != 1 || step_value.integer_high != 0) {
        return std::nullopt;
    }

    const auto comparison_id = *test.terminator.value;
    const auto& condition = function.values[comparison_id.value];
    if (condition.kind != ValueKind::Binary ||
        !negated_integer_comparison(condition.binary) ||
        condition.operands.size() != 2) {
        return std::nullopt;
    }
    std::optional<ValueId> load_id;
    std::optional<ValueId> compared_load_id;
    bool load_on_left = false;
    for (unsigned operand = 0; operand < 2; ++operand) {
        auto candidate = condition.operands[operand];
        const auto compared = candidate;
        const auto* candidate_value = &function.values[candidate.value];
        while (candidate_value->kind == ValueKind::Cast &&
               candidate_value->cast == CastOperation::Reinterpret &&
               candidate_value->operands.size() == 1 &&
               integer_type(hir_module, candidate_value->type) &&
               integer_type(
                   hir_module,
                   function.values[candidate_value->operands.front().value]
                       .type) &&
               type_bits(hir_module, candidate_value->type) ==
                   type_bits(
                       hir_module,
                       function.values[candidate_value->operands.front().value]
                           .type)) {
            candidate = candidate_value->operands.front();
            candidate_value = &function.values[candidate.value];
        }
        const auto kind = candidate_value->kind;
        if (kind != ValueKind::PointerLoad &&
            kind != ValueKind::IndexedLoad) {
            continue;
        }
        if (load_id) return std::nullopt;
        load_id = candidate;
        compared_load_id = compared;
        load_on_left = operand == 0;
    }
    if (!load_id || !compared_load_id) return std::nullopt;
    const auto invariant = condition.operands[load_on_left ? 1U : 0U];
    const auto& load = function.values[load_id->value];
    const auto compared_type =
        function.values[compared_load_id->value].type;
    const auto element_bits = type_bits(hir_module, compared_type);
    if (!integer_type(hir_module, load.type) ||
        !integer_type(hir_module, compared_type) ||
        type_bits(hir_module, load.type) != element_bits ||
        element_bits < 8 ||
        element_bits > 64 || !std::has_single_bit(element_bits) ||
        load.is_volatile_access || !load.effect_input ||
        !load.effect_output ||
        function.values[invariant.value].type != compared_type) {
        return std::nullopt;
    }
    const auto invariant_definition = definitions[invariant.value];
    if (invariant_definition &&
        loop.blocks.contains(invariant_definition->value)) {
        return std::nullopt;
    }

    std::optional<ValueId> base;
    if (load.kind == ValueKind::IndexedLoad && load.operands.size() == 2 &&
        load.operands[1] == index_id) {
        base = load.operands[0];
    } else if (load.kind == ValueKind::PointerLoad &&
               load.operands.size() == 1) {
        const auto address_id = load.operands.front();
        const auto& address = function.values[address_id.value];
        if (address.kind == ValueKind::IndexedAddress &&
            address.operands.size() == 2 &&
            address.operands[1] == index_id) {
            base = address.operands[0];
        }
    }
    if (!base) return std::nullopt;
    const auto base_definition = definitions[base->value];
    if (base_definition && loop.blocks.contains(base_definition->value)) {
        return std::nullopt;
    }

    for (const auto id : test.values) {
        const auto& value = function.values[id.value];
        if (is_effectful_value(value) && id != *load_id) {
            return std::nullopt;
        }
        if (std::any_of(uses.uses(id).begin(), uses.uses(id).end(),
                        [&](const ValueUse& use) {
                            return use.block != test_id;
                        })) {
            return std::nullopt;
        }
    }
    for (const auto id : header.values) {
        if (is_effectful_value(function.values[id.value])) {
            return std::nullopt;
        }
    }
    for (const auto id : latch.values) {
        const auto& value = function.values[id.value];
        if (is_effectful_value(value) ||
            (id != index_next && value.kind != ValueKind::ConstantInteger)) {
            return std::nullopt;
        }
    }
    return EarlyExitLoopPattern{
        loop, test_id, latch_id, exit_id, index_id, *initial_index,
        range.operands[1], index_next, *base, *load_id,
        *compared_load_id, invariant,
        condition.binary, load_on_left};
}

bool vectorize_early_exit_loop(ManagedFunction& function,
                               hir::Module& hir_module,
                               const EarlyExitLoopPattern& pattern,
                               unsigned vector_bits) {
    if (pattern.loop.header.value >= function.blocks.size() ||
        pattern.loop.preheader.value >= function.blocks.size()) {
        return false;
    }
    const auto original_header_effect =
        function.blocks[pattern.loop.header.value].effect;
    if (original_header_effect.value >= function.effects.size() ||
        std::none_of(
            function.effects[original_header_effect.value].incoming.begin(),
            function.effects[original_header_effect.value].incoming.end(),
            [&](const EffectIncoming& incoming) {
                return incoming.predecessor == pattern.loop.preheader;
            })) {
        return false;
    }
    const auto scalar_type =
        function.values[pattern.compared_load.value].type;
    const auto element_bits = type_bits(hir_module, scalar_type);
    if (element_bits == 0 || vector_bits % element_bits != 0) return false;
    const auto lanes = vector_bits / element_bits;
    if (lanes < 2 || !std::has_single_bit(lanes)) return false;
    const auto failure_operation =
        negated_integer_comparison(pattern.continuation);
    if (!failure_operation) return false;

    const auto index_type = function.values[pattern.index.value].type;
    const auto bool_type =
        function.values[function.blocks[pattern.test.value]
                            .terminator.value->value]
            .type;
    const auto mask_element = element_bits == 8
        ? hir_module.builtin(BuiltinType::I8)
        : element_bits == 16
        ? hir_module.builtin(BuiltinType::I16)
        : element_bits == 32
        ? hir_module.builtin(BuiltinType::I32)
        : hir_module.builtin(BuiltinType::I64);
    if (!mask_element) return false;
    const auto vector_type = hir_module.vector_of(scalar_type, lanes);
    const auto mask_type = hir_module.vector_of(*mask_element, lanes);

    const BlockId vector_header_id{
        static_cast<std::uint32_t>(function.blocks.size())};
    const BlockId vector_body_id{vector_header_id.value + 1U};
    const BlockId vector_exit_id{vector_header_id.value + 2U};
    ManagedBlock vector_header;
    vector_header.id = vector_header_id;
    vector_header.location = function.blocks[pattern.loop.header.value].location;
    vector_header.predecessors = {pattern.loop.preheader, vector_body_id};
    ManagedBlock vector_body;
    vector_body.id = vector_body_id;
    vector_body.location = function.blocks[pattern.test.value].location;
    vector_body.predecessors = {vector_header_id};
    ManagedBlock vector_exit;
    vector_exit.id = vector_exit_id;
    vector_exit.location = vector_header.location;
    vector_exit.predecessors = {vector_header_id, vector_body_id};

    const auto add_phi_effect = [&](SourceLocation location,
                                    std::vector<EffectIncoming> incoming) {
        const EffectId id{
            static_cast<std::uint32_t>(function.effects.size())};
        ManagedEffect effect;
        effect.id = id;
        effect.location = location;
        effect.kind = EffectKind::Phi;
        effect.incoming = std::move(incoming);
        function.effects.push_back(std::move(effect));
        return id;
    };
    const auto preheader_effect =
        function.blocks[pattern.loop.preheader.value].terminator.effect;
    const auto vector_header_effect = add_phi_effect(
        vector_header.location,
        {{pattern.loop.preheader, preheader_effect}});
    const auto vector_body_effect = add_phi_effect(
        vector_body.location, {{vector_header_id, vector_header_effect}});
    const auto vector_exit_effect = add_phi_effect(
        vector_exit.location, {{vector_header_id, vector_header_effect}});
    vector_header.effect = vector_header_effect;
    vector_body.effect = vector_body_effect;
    vector_exit.effect = vector_exit_effect;

    const auto append_value = [&](std::vector<ValueId>& destination,
                                  ManagedValue value) {
        const ValueId id{
            static_cast<std::uint32_t>(function.values.size())};
        value.id = id;
        function.values.push_back(std::move(value));
        destination.push_back(id);
        return id;
    };
    auto& preheader_values =
        function.blocks[pattern.loop.preheader.value].values;
    const auto add_integer_constant = [&](hir::TypeId type,
                                          std::uint64_t integer) {
        ManagedValue value;
        value.location = vector_header.location;
        value.type = type;
        value.kind = ValueKind::ConstantInteger;
        value.integer = integer;
        return append_value(preheader_values, std::move(value));
    };
    const auto lanes_constant = add_integer_constant(index_type, lanes);
    const auto index_bits = type_bits(hir_module, index_type);
    const auto limit_mask = mask_to(
        bit_not(UInt128{lanes - 1U}), index_bits);
    ManagedValue mask;
    mask.location = vector_header.location;
    mask.type = index_type;
    mask.kind = ValueKind::ConstantInteger;
    mask.integer = limit_mask.low;
    mask.integer_high = limit_mask.high;
    const auto mask_id = append_value(preheader_values, std::move(mask));
    ManagedValue limit;
    limit.location = vector_header.location;
    limit.type = index_type;
    limit.kind = ValueKind::Binary;
    limit.binary = BinaryOperation::BitAnd;
    limit.operands = {pattern.bound, mask_id};
    const auto vector_limit =
        append_value(preheader_values, std::move(limit));
    ManagedValue invariant_splat;
    invariant_splat.location =
        function.values[pattern.invariant.value].location;
    invariant_splat.type = vector_type;
    invariant_splat.kind = ValueKind::Splat;
    invariant_splat.operands = {pattern.invariant};
    const auto vector_invariant =
        append_value(preheader_values, std::move(invariant_splat));
    std::vector<ValueId> lane_constants;
    lane_constants.reserve(lanes);
    for (unsigned lane = 0; lane < lanes; ++lane) {
        lane_constants.push_back(add_integer_constant(index_type, lane));
    }

    ManagedValue vector_index_phi;
    vector_index_phi.location = vector_header.location;
    vector_index_phi.type = index_type;
    vector_index_phi.kind = ValueKind::Phi;
    vector_index_phi.incoming = {
        {pattern.loop.preheader, pattern.initial_index},
        {vector_body_id, ValueId{}}};
    const auto vector_index =
        append_value(vector_header.values, std::move(vector_index_phi));
    ManagedValue vector_range;
    vector_range.location = vector_header.location;
    vector_range.type = bool_type;
    vector_range.kind = ValueKind::Binary;
    vector_range.binary = BinaryOperation::UnsignedLess;
    vector_range.operands = {vector_index, vector_limit};
    const auto vector_range_id =
        append_value(vector_header.values, std::move(vector_range));

    EffectId current_effect = vector_body_effect;
    ManagedValue vector_load;
    vector_load.location = function.values[pattern.load.value].location;
    vector_load.type = vector_type;
    vector_load.kind = ValueKind::IndexedLoad;
    vector_load.memory_alignment =
        function.values[pattern.load.value].memory_alignment;
    vector_load.operands = {pattern.base, vector_index};
    vector_load.effect_input = current_effect;
    const EffectId load_effect{
        static_cast<std::uint32_t>(function.effects.size())};
    vector_load.effect_output = load_effect;
    const auto vector_load_id =
        append_value(vector_body.values, std::move(vector_load));
    ManagedEffect load_operation;
    load_operation.id = load_effect;
    load_operation.location =
        function.values[vector_load_id.value].location;
    load_operation.kind = EffectKind::Operation;
    load_operation.input = current_effect;
    load_operation.operation = vector_load_id;
    function.effects.push_back(std::move(load_operation));
    current_effect = load_effect;

    ManagedValue failures;
    failures.location =
        function.values[function.blocks[pattern.test.value]
                            .terminator.value->value]
            .location;
    failures.type = mask_type;
    failures.kind = ValueKind::Binary;
    failures.binary = *failure_operation;
    failures.operands = pattern.load_on_left
        ? std::vector<ValueId>{vector_load_id, vector_invariant}
        : std::vector<ValueId>{vector_invariant, vector_load_id};
    const auto failure_mask =
        append_value(vector_body.values, std::move(failures));
    ManagedValue first_extract;
    first_extract.location = vector_body.location;
    first_extract.type = *mask_element;
    first_extract.kind = ValueKind::ExtractElement;
    first_extract.operands = {failure_mask, lane_constants.front()};
    auto reduced =
        append_value(vector_body.values, std::move(first_extract));
    for (unsigned lane = 1; lane < lanes; ++lane) {
        ManagedValue extract;
        extract.location = vector_body.location;
        extract.type = *mask_element;
        extract.kind = ValueKind::ExtractElement;
        extract.operands = {failure_mask, lane_constants[lane]};
        const auto element =
            append_value(vector_body.values, std::move(extract));
        ManagedValue combine;
        combine.location = vector_body.location;
        combine.type = *mask_element;
        combine.kind = ValueKind::Binary;
        combine.binary = BinaryOperation::BitOr;
        combine.operands = {reduced, element};
        reduced = append_value(vector_body.values, std::move(combine));
    }
    ManagedValue no_failure;
    no_failure.location = vector_body.location;
    no_failure.type = bool_type;
    no_failure.kind = ValueKind::Unary;
    no_failure.unary = UnaryOperation::IsZero;
    no_failure.operands = {reduced};
    const auto no_failure_id =
        append_value(vector_body.values, std::move(no_failure));
    ManagedValue vector_index_next;
    vector_index_next.location =
        function.values[pattern.index_next.value].location;
    vector_index_next.type = index_type;
    vector_index_next.kind = ValueKind::Binary;
    vector_index_next.binary = BinaryOperation::Add;
    vector_index_next.operands = {vector_index, lanes_constant};
    const auto vector_index_next_id =
        append_value(vector_body.values, std::move(vector_index_next));
    function.values[vector_index.value].incoming[1].value =
        vector_index_next_id;

    vector_header.terminator.kind = TerminatorKind::ConditionalBranch;
    vector_header.terminator.location = vector_header.location;
    vector_header.terminator.value = vector_range_id;
    vector_header.terminator.successors = {
        vector_body_id, vector_exit_id};
    vector_header.terminator.effect = vector_header_effect;
    vector_body.terminator.kind = TerminatorKind::ConditionalBranch;
    vector_body.terminator.location = vector_body.location;
    vector_body.terminator.value = no_failure_id;
    vector_body.terminator.successors = {
        vector_header_id, vector_exit_id};
    vector_body.terminator.effect = current_effect;
    vector_exit.terminator.kind = TerminatorKind::Branch;
    vector_exit.terminator.location = vector_exit.location;
    vector_exit.terminator.successors = {pattern.loop.header};
    vector_exit.terminator.effect = vector_exit_effect;
    function.effects[vector_header_effect.value].incoming.push_back(
        {vector_body_id, current_effect});
    function.effects[vector_exit_effect.value].incoming.push_back(
        {vector_body_id, current_effect});

    auto& preheader = function.blocks[pattern.loop.preheader.value];
    std::replace(preheader.terminator.successors.begin(),
                 preheader.terminator.successors.end(),
                 pattern.loop.header, vector_header_id);
    auto& header = function.blocks[pattern.loop.header.value];
    std::replace(header.predecessors.begin(), header.predecessors.end(),
                 pattern.loop.preheader, vector_exit_id);
    for (const auto id : header.values) {
        auto& value = function.values[id.value];
        if (value.kind != ValueKind::Phi) continue;
        const auto incoming = std::find_if(
            value.incoming.begin(), value.incoming.end(),
            [&](const PhiIncoming& edge) {
                return edge.predecessor == pattern.loop.preheader;
            });
        if (incoming == value.incoming.end()) continue;
        incoming->predecessor = vector_exit_id;
        if (id == pattern.index) incoming->value = vector_index;
    }
    auto& header_effect = function.effects[header.effect.value];
    const auto incoming_effect = std::find_if(
        header_effect.incoming.begin(), header_effect.incoming.end(),
        [&](const EffectIncoming& edge) {
            return edge.predecessor == pattern.loop.preheader;
        });
    incoming_effect->predecessor = vector_exit_id;
    incoming_effect->effect = vector_exit_effect;

    function.blocks.push_back(std::move(vector_header));
    function.blocks.push_back(std::move(vector_body));
    function.blocks.push_back(std::move(vector_exit));
    return true;
}

bool vectorize_early_exit_loops(ManagedFunction& function,
                                hir::Module& hir_module,
                                const Subtarget& subtarget,
                                const CompilerOptions& options,
                                std::span<const CanonicalLoop> loops,
                                const UseLists& uses) {
    bool changed = false;
    const auto definitions = value_definition_blocks(function);
    for (const auto& loop : loops) {
        const auto pattern = find_early_exit_loop(
            function, hir_module, loop, definitions, uses);
        if (!pattern) continue;
        const auto element_bits = type_bits(
            hir_module, function.values[pattern->load.value].type);
        const auto width = preferred_vector_bits(
            subtarget, options, false, element_bits);
        if (width != 0 && vectorize_early_exit_loop(
                              function, hir_module, *pattern, width)) {
            changed = true;
        }
    }
    return changed;
}

bool fold_constants(ManagedFunction& function,
                    const hir::Module& hir_module) {
    bool changed = false;
    for (auto& value : function.values) {
        if (value.kind == ValueKind::Unary && value.operands.size() == 1) {
            const auto& operand = function.values[value.operands[0].value];
            if (operand.kind == ValueKind::ConstantFloating &&
                value.unary == UnaryOperation::Negate) {
                const auto bits = type_bits(hir_module, value.type);
                if (bits != 32 && bits != 64 && bits != 80 && bits != 128) {
                    continue;
                }
                const auto folded = bit_xor(
                    UInt128{operand.integer, operand.integer_high},
                    shift_left(UInt128{1}, bits - 1U));
                value.integer = folded.low;
                value.integer_high = folded.high;
                value.kind = ValueKind::ConstantFloating;
                value.operands.clear();
                changed = true;
                continue;
            }
            if (operand.kind != ValueKind::ConstantInteger) continue;
            const auto bits = type_bits(hir_module, value.type);
            const UInt128 input{operand.integer, operand.integer_high};
            UInt128 folded;
            if (value.unary == UnaryOperation::Negate) {
                folded = mask_to(negate(input), bits);
            } else if (value.unary == UnaryOperation::BitNot) {
                folded = mask_to(bit_not(input), bits);
            } else {
                folded = input == UInt128{} ? UInt128{1} : UInt128{};
            }
            value.integer = folded.low;
            value.integer_high = folded.high;
            value.kind = ValueKind::ConstantInteger;
            value.operands.clear();
            changed = true;
        } else if (value.kind == ValueKind::Cast &&
                   value.operands.size() == 1) {
            const auto& operand = function.values[value.operands[0].value];
            if (operand.kind != ValueKind::ConstantInteger) continue;
            const auto source_bits = type_bits(hir_module, operand.type);
            const auto target_bits = type_bits(hir_module, value.type);
            if (source_bits == 0 || target_bits == 0) continue;
            auto folded = mask_to(
                UInt128{operand.integer, operand.integer_high}, source_bits);
            if (value.cast == CastOperation::SignExtend &&
                bit(folded, source_bits - 1U)) {
                folded = bit_or(
                    folded,
                    bit_not(mask_to(bit_not(UInt128{}), source_bits)));
            } else if (value.cast != CastOperation::ZeroExtend &&
                       value.cast != CastOperation::Truncate &&
                       value.cast != CastOperation::Reinterpret &&
                       value.cast != CastOperation::SignExtend) {
                continue;
            }
            folded = mask_to(folded, target_bits);
            value.integer = folded.low;
            value.integer_high = folded.high;
            value.kind = ValueKind::ConstantInteger;
            value.operands.clear();
            changed = true;
        } else if (value.kind == ValueKind::Binary &&
                   value.operands.size() == 2) {
            const auto& left = function.values[value.operands[0].value];
            const auto& right = function.values[value.operands[1].value];
            if (left.kind != ValueKind::ConstantInteger ||
                right.kind != ValueKind::ConstantInteger) {
                continue;
            }
            const auto bits = comparison(value.binary)
                ? type_bits(hir_module, left.type)
                : type_bits(hir_module, value.type);
            const UInt128 left_value{left.integer, left.integer_high};
            const UInt128 right_value{right.integer, right.integer_high};
            UInt128 folded;
            switch (value.binary) {
            case BinaryOperation::Add:
                folded = mask_to(add(left_value, right_value), bits);
                break;
            case BinaryOperation::Subtract:
                folded = mask_to(subtract(left_value, right_value), bits);
                break;
            case BinaryOperation::Multiply:
                folded = mask_to(multiply(left_value, right_value), bits);
                break;
            case BinaryOperation::BitAnd:
                folded = bit_and(left_value, right_value);
                break;
            case BinaryOperation::BitOr:
                folded = bit_or(left_value, right_value);
                break;
            case BinaryOperation::BitXor:
                folded = bit_xor(left_value, right_value);
                break;
            case BinaryOperation::ShiftLeft:
                folded = mask_to(
                    shift_left(left_value,
                               static_cast<unsigned>(right_value.low % bits)),
                    bits);
                break;
            case BinaryOperation::ShiftRightLogical:
                folded = shift_right(
                    left_value,
                    static_cast<unsigned>(right_value.low % bits));
                break;
            case BinaryOperation::RotateLeft: {
                const auto amount =
                    static_cast<unsigned>(right_value.low % bits);
                folded = amount == 0
                    ? mask_to(left_value, bits)
                    : mask_to(
                          bit_or(shift_left(left_value, amount),
                                 shift_right(left_value, bits - amount)),
                          bits);
                break;
            }
            case BinaryOperation::RotateRight: {
                const auto amount =
                    static_cast<unsigned>(right_value.low % bits);
                folded = amount == 0
                    ? mask_to(left_value, bits)
                    : mask_to(
                          bit_or(shift_right(left_value, amount),
                                 shift_left(left_value, bits - amount)),
                          bits);
                break;
            }
            case BinaryOperation::Equal:
                folded = left_value == right_value ? 1 : 0;
                break;
            case BinaryOperation::NotEqual:
                folded = left_value == right_value ? 0 : 1;
                break;
            case BinaryOperation::UnsignedLess:
                folded = left_value < right_value ? 1 : 0;
                break;
            case BinaryOperation::UnsignedLessEqual:
                folded = right_value < left_value ? 0 : 1;
                break;
            case BinaryOperation::UnsignedGreater:
                folded = right_value < left_value ? 1 : 0;
                break;
            case BinaryOperation::UnsignedGreaterEqual:
                folded = left_value < right_value ? 0 : 1;
                break;
            default: continue;
            }
            value.integer = folded.low;
            value.integer_high = folded.high;
            value.kind = ValueKind::ConstantInteger;
            value.operands.clear();
            changed = true;
        }
    }
    return changed;
}

std::optional<std::uint64_t> constant_u64(
    const ManagedFunction& function, ValueId id) {
    if (id.value >= function.values.size()) return std::nullopt;
    const auto& value = function.values[id.value];
    if (value.kind != ValueKind::ConstantInteger ||
        value.integer_high != 0) {
        return std::nullopt;
    }
    return value.integer;
}

std::optional<std::uint64_t> affine_phi_offset(
    const ManagedFunction& function, ValueId value_id, ValueId phi,
    std::uint64_t maximum_offset) {
    if (value_id == phi) return 0;
    if (value_id.value >= function.values.size()) return std::nullopt;
    const auto& value = function.values[value_id.value];
    if (value.kind != ValueKind::Binary ||
        value.binary != BinaryOperation::Add ||
        value.operands.size() != 2) {
        return std::nullopt;
    }
    std::optional<ValueId> constant;
    if (value.operands[0] == phi) constant = value.operands[1];
    else if (value.operands[1] == phi) constant = value.operands[0];
    if (!constant) return std::nullopt;
    const auto offset = constant_u64(function, *constant);
    if (!offset || *offset > maximum_offset) return std::nullopt;
    return offset;
}

bool value_used_outside(
    const ManagedFunction& function, ValueId sought,
    const std::unordered_set<std::uint32_t>& ignored_users) {
    for (const auto& value : function.values) {
        if (ignored_users.contains(value.id.value)) continue;
        if (std::find(value.operands.begin(), value.operands.end(), sought) !=
                value.operands.end() ||
            std::any_of(
                value.call_arguments.begin(), value.call_arguments.end(),
                [&](const CallArgument& argument) {
                    return argument.value == sought;
                }) ||
            std::any_of(
                value.incoming.begin(), value.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return incoming.value == sought;
                })) {
            return true;
        }
    }
    return std::any_of(
        function.blocks.begin(), function.blocks.end(),
        [&](const ManagedBlock& block) {
            return block.terminator.value == sought;
        });
}

bool promote_native_induction_views(
    ManagedFunction& function, const hir::Module& hir_module,
    unsigned native_integer_bits, std::span<const CanonicalLoop> loops) {
    const auto native_kind = [&]() -> std::optional<BuiltinType> {
        switch (native_integer_bits) {
        case 32: return BuiltinType::U32;
        case 64: return BuiltinType::U64;
        case 128: return BuiltinType::U128;
        default: return std::nullopt;
        }
    }();
    if (!native_kind) return false;
    const auto native_type = hir_module.builtin(*native_kind);
    if (!native_type) return false;

    const auto definitions = value_definition_blocks(function);
    std::unordered_set<std::uint32_t> removed;
    std::vector<ValueId> possibly_dead_affine_values;
    bool changed = false;

    const auto append = [&](BlockId block, ManagedValue value,
                            std::optional<ValueId> before = std::nullopt,
                            bool phi_position = false) {
        const ValueId id{
            static_cast<std::uint32_t>(function.values.size())};
        value.id = id;
        function.values.push_back(std::move(value));
        auto& values = function.blocks[block.value].values;
        auto position = values.end();
        if (before) position = std::find(values.begin(), values.end(), *before);
        else if (phi_position) {
            position = std::find_if(
                values.begin(), values.end(), [&](ValueId candidate) {
                    return function.values[candidate.value].kind !=
                           ValueKind::Phi;
                });
        }
        values.insert(position, id);
        return id;
    };

    struct View {
        ValueId cast;
        ValueId source;
        BlockId block;
        std::uint64_t offset{};
    };

    for (const auto& loop : loops) {
        if (loop.header.value >= function.blocks.size() ||
            loop.preheader.value >= function.blocks.size()) {
            continue;
        }
        const auto& header = function.blocks[loop.header.value];
        if (header.predecessors.size() != 2 ||
            header.terminator.kind != TerminatorKind::ConditionalBranch ||
            !header.terminator.value) {
            continue;
        }
        const auto condition_id = *header.terminator.value;
        if (condition_id.value >= function.values.size()) continue;
        const auto condition = function.values[condition_id.value];

        std::vector<ValueId> phis;
        for (const auto id : header.values) {
            if (function.values[id.value].kind == ValueKind::Phi) {
                phis.push_back(id);
            }
        }
        for (const auto phi_id : phis) {
            const auto& phi = function.values[phi_id.value];
            const auto source_bits = type_bits(hir_module, phi.type);
            if (!unsigned_integer_type(hir_module, phi.type) ||
                source_bits == 0 || source_bits >= native_integer_bits ||
                phi.incoming.size() != 2 ||
                condition.kind != ValueKind::Binary ||
                (condition.binary != BinaryOperation::UnsignedLess &&
                 condition.binary != BinaryOperation::NotEqual) ||
                condition.operands.size() != 2 ||
                condition.operands[0] != phi_id) {
                continue;
            }

            const auto outside = std::find_if(
                phi.incoming.begin(), phi.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return incoming.predecessor == loop.preheader;
                });
            const auto inside = std::find_if(
                phi.incoming.begin(), phi.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return loop.blocks.contains(incoming.predecessor.value) &&
                           incoming.predecessor != loop.preheader;
                });
            if (outside == phi.incoming.end() ||
                inside == phi.incoming.end() ||
                outside == inside || inside->value.value >= function.values.size() ||
                inside->value.value >= definitions.size() ||
                definitions[inside->value.value] != inside->predecessor) {
                continue;
            }
            const auto initial = constant_u64(function, outside->value);
            if (!initial || *initial != 0) continue;

            const auto& update = function.values[inside->value.value];
            if (update.kind != ValueKind::Binary ||
                update.binary != BinaryOperation::Add ||
                update.operands.size() != 2) {
                continue;
            }
            std::optional<ValueId> step_id;
            if (update.operands[0] == phi_id) step_id = update.operands[1];
            else if (update.operands[1] == phi_id) step_id = update.operands[0];
            if (!step_id) continue;
            const auto step = constant_u64(function, *step_id);
            if (!step || *step == 0 || *step >
                    (source_bits == 64
                         ? std::numeric_limits<std::uint64_t>::max()
                         : (std::uint64_t{1} << source_bits) - 1U)) {
                continue;
            }

            // A unit recurrence guarded by phi < bound cannot wrap before
            // its last useful view. For an unrolled power-of-two recurrence,
            // require the loop limit to have the corresponding low bits
            // cleared. This proves phi + [0, step) remains in source range.
            if (*step > 1) {
                if (!std::has_single_bit(*step)) continue;
                const auto bound_id = condition.operands[1];
                if (bound_id.value >= function.values.size()) continue;
                const auto& bound = function.values[bound_id.value];
                if (bound.kind != ValueKind::Binary ||
                    bound.binary != BinaryOperation::BitAnd ||
                    bound.operands.size() != 2) {
                    continue;
                }
                std::optional<std::uint64_t> mask;
                if (const auto left = constant_u64(
                        function, bound.operands[0])) mask = left;
                if (!mask) {
                    if (const auto right = constant_u64(
                            function, bound.operands[1])) mask = right;
                }
                if (!mask || (*mask & (*step - 1U)) != 0) continue;
            }

            std::vector<View> views;
            std::optional<hir::TypeId> view_type;
            for (const auto block_value : loop.blocks) {
                if (block_value >= function.blocks.size()) continue;
                for (const auto id : function.blocks[block_value].values) {
                    const auto& value = function.values[id.value];
                    if (value.kind != ValueKind::Cast ||
                        value.cast != CastOperation::ZeroExtend ||
                        type_bits(hir_module, value.type) !=
                            native_integer_bits ||
                        !unsigned_integer_type(hir_module, value.type) ||
                        value.operands.size() != 1) {
                        continue;
                    }
                    if (view_type && value.type != *view_type) continue;
                    const auto offset = affine_phi_offset(
                        function, value.operands[0], phi_id, *step - 1U);
                    if (!offset) continue;
                    if (!view_type) view_type = value.type;
                    views.push_back(
                        {id, value.operands[0], BlockId{block_value}, *offset});
                }
            }
            if (views.empty()) continue;

            const auto phi_location = phi.location;
            const auto update_location = update.location;
            const auto backedge = inside->predecessor;

            ManagedValue zero;
            zero.location = phi_location;
            zero.type = *view_type;
            zero.kind = ValueKind::ConstantInteger;
            const auto wide_initial = append(loop.preheader, std::move(zero));

            ManagedValue wide_step;
            wide_step.location = update_location;
            wide_step.type = *view_type;
            wide_step.kind = ValueKind::ConstantInteger;
            wide_step.integer = *step;
            const auto wide_step_id =
                append(loop.preheader, std::move(wide_step));

            ManagedValue wide_phi;
            wide_phi.location = phi_location;
            wide_phi.type = *view_type;
            wide_phi.kind = ValueKind::Phi;
            wide_phi.incoming = {
                {loop.preheader, wide_initial},
                {backedge, ValueId{}}};
            const auto wide_phi_id =
                append(loop.header, std::move(wide_phi), std::nullopt, true);

            ManagedValue wide_update;
            wide_update.location = update_location;
            wide_update.type = *view_type;
            wide_update.kind = ValueKind::Binary;
            wide_update.binary = BinaryOperation::Add;
            wide_update.operands = {wide_phi_id, wide_step_id};
            const auto wide_update_id =
                append(backedge, std::move(wide_update));
            function.values[wide_phi_id.value].incoming[1].value =
                wide_update_id;

            std::unordered_map<std::uint64_t, ValueId> offsets;
            for (const auto& view : views) {
                auto replacement = wide_phi_id;
                if (view.offset != 0) {
                    auto constant = offsets.find(view.offset);
                    if (constant == offsets.end()) {
                        ManagedValue offset;
                        offset.location =
                            function.values[view.cast.value].location;
                        offset.type = *view_type;
                        offset.kind = ValueKind::ConstantInteger;
                        offset.integer = view.offset;
                        constant = offsets.emplace(
                            view.offset,
                            append(loop.preheader, std::move(offset))).first;
                    }
                    ManagedValue derived;
                    derived.location =
                        function.values[view.cast.value].location;
                    derived.type = *view_type;
                    derived.kind = ValueKind::Binary;
                    derived.binary = BinaryOperation::Add;
                    derived.operands = {wide_phi_id, constant->second};
                    replacement = append(
                        view.block, std::move(derived), view.cast);
                }
                replace_value_uses(function, view.cast, replacement);
                removed.insert(view.cast.value);
                if (view.source != phi_id) {
                    possibly_dead_affine_values.push_back(view.source);
                }
            }
            changed = true;
        }
    }

    if (!changed) return false;
    for (const auto candidate : possibly_dead_affine_values) {
        if (!value_used_outside(function, candidate, removed)) {
            removed.insert(candidate.value);
        }
    }
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId id) {
            return removed.contains(id.value);
        });
    }
    compact_managed_values(function);
    return true;
}

} // namespace

bool reassociate_unit_recurrence_adds(
    ManagedFunction& function, const hir::Module& hir_module,
    std::span<const CanonicalLoop> loops, const UseLists& use_lists) {
    return reassociate_unit_recurrence_adds_impl(
        function, hir_module, loops, use_lists);
}

void prune_unreachable_blocks(ManagedFunction& function) {
    eliminate_unreachable_blocks(function);
}

bool promote_native_induction_views(ManagedModule& module,
                                    hir::Module& hir_module,
                                    unsigned native_integer_bits) {
    FunctionPassManager pipeline;
    pipeline.add(
        PassId::NativeInductionViewPromotion,
        [&](ManagedFunction& function, FunctionAnalysisManager& analyses) {
            const bool changed = promote_native_induction_views(
                function, hir_module, native_integer_bits,
                analyses.loops().canonical_loops());
            return changed ? PassResult::changed_values()
                           : PassResult::unchanged();
        });
    return pipeline.run(module);
}

void optimize(ManagedModule& module, hir::Module& hir_module,
              const Subtarget& subtarget, const CompilerOptions& options,
              Diagnostics& diagnostics) {
    inline_managed_calls(module, hir_module, options, diagnostics);

    // Scalar cells are a source-language ownership device, not a mandate for
    // machine stack traffic. Promote eligible cells before the remaining SSA
    // passes so loops, the allocator, and target selection see their values.
    if (options.tree_copy_prop) {
        FunctionPassManager promotion;
        promotion.add(
            PassId::PromoteScalarSlots,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                promote_scalar_slots(function, hir_module);
                return PassResult::changed_values();
            });
        (void)promotion.run(module);
    }
    // A naked function has no frame: the scalar cells of its inlined
    // helpers and its dead temporaries disappear at every level.
    for (auto& function : module.functions) {
        if (!hir_module.function(function.source).naked) continue;
        if (!options.tree_copy_prop) {
            promote_scalar_slots(function, hir_module);
            propagate_trivial_copies(function);
        }
        if (!options.tree_ccp) {
            (void)fold_constants(function, hir_module);
            simplify_integer_operations(function, hir_module);
        }
        if (!options.tree_dce) eliminate_dead_values(function);
    }

    // This is the one module pass in the early scalar pipeline: it reaches a
    // fixed point over the call graph and therefore intentionally brackets
    // the per-function pass managers.
    if (options.ipa_pure_const) {
        const auto removable =
            infer_removable_functions(module, hir_module);
        for (auto& function : module.functions) {
            eliminate_dead_removable_calls(function, removable);
        }
    }

    FunctionPassManager pipeline;
    if (options.tree_dse) {
        pipeline.add(
            PassId::DeadStoreElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                eliminate_dead_stores(function);
                return PassResult::changed_values();
            });
    }
    if (options.tree_copy_prop) {
        pipeline.add(
            PassId::CopyPropagation,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                propagate_trivial_copies(function);
                return PassResult::changed_values();
            });
    }
    if (options.tree_ccp) {
        const auto rounds = std::max(1U, options.ccp_rounds);
        for (unsigned round = 0; round < rounds; ++round) {
            pipeline.add(
                PassId::ConstantFolding,
                [&](ManagedFunction& function, FunctionAnalysisManager&) {
                    return fold_constants(function, hir_module)
                        ? PassResult::changed_values()
                        : PassResult::unchanged();
                });
        }
        pipeline.add(
            PassId::IntegerSimplification,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                simplify_integer_operations(function, hir_module);
                return PassResult::changed_values();
            });
        pipeline.add(
            PassId::BranchFolding,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                const bool changed = fold_constant_branches(function);
                if (changed && options.tree_copy_prop) {
                    propagate_trivial_copies(function);
                }
                return changed ? PassResult::changed_cfg()
                               : PassResult::unchanged();
            });
    }
    if (options.thread_jumps) {
        pipeline.add(
            PassId::BranchThreading,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                const bool changed = thread_boolean_phi_branches(function);
                if (changed && options.tree_copy_prop) {
                    propagate_trivial_copies(function);
                }
                return changed ? PassResult::changed_cfg()
                               : PassResult::unchanged();
            });
    }
    if (options.if_conversion) {
        pipeline.add(
            PassId::IfConversion,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                const bool changed =
                    if_convert_diamonds(function, hir_module, options);
                if (changed && options.tree_copy_prop) {
                    propagate_trivial_copies(function);
                }
                return changed ? PassResult::changed_cfg()
                               : PassResult::unchanged();
            });
        pipeline.add(
            PassId::SelectFactoring,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                factor_common_select_addends(function);
                return PassResult::changed_values();
            });
    }
    // Preserve complete diamonds until if-conversion has had the opportunity
    // to form semantic selects. Forwarding one empty arm first would turn a
    // diamond into a triangle and hide the target-independent conversion.
    if (options.tree_cfg_cleanup) {
        pipeline.add(
            PassId::ForwardingBlockElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                return eliminate_forwarding_blocks(function)
                    ? PassResult::changed_cfg()
                    : PassResult::unchanged();
            });
    }
    if (options.tree_fre) {
        pipeline.add(
            PassId::RedundantExpressionElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                eliminate_fully_redundant_expressions(function);
                return PassResult::changed_values();
            });
    }
    if (options.ivopts) {
        pipeline.add(
            PassId::InductionCoalescing,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                coalesce_equivalent_inductions(
                    function, hir_module,
                    analyses.loops().canonical_loops(), true,
                    options.optimize_for == OptimizationGoal::Size ||
                        options.optimize_for ==
                            OptimizationGoal::MinimumSize);
                return PassResult::changed_values();
            });
    }
    if (options.move_loop_invariants) {
        pipeline.add(
            PassId::LoopInvariantMotion,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                move_loop_invariants(
                    function, hir_module, subtarget,
                    analyses.loops().canonical_loops());
                return PassResult::changed_values();
            });
    }
    if (options.tree_loop_vectorize) {
        pipeline.add(
            PassId::ReductionVectorization,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                vectorize_reduction_loops(
                    function, hir_module, subtarget, options,
                    analyses.loops().canonical_loops());
                return PassResult::changed_cfg();
            });
    }
    if (options.tree_early_exit_vectorize) {
        pipeline.add(
            PassId::EarlyExitVectorization,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                const bool changed = vectorize_early_exit_loops(
                    function, hir_module, subtarget, options,
                    analyses.loops().canonical_loops(), analyses.uses());
                return changed ? PassResult::changed_cfg()
                               : PassResult::unchanged();
            });
    }
    // Preserve vectorizable reductions before expanding scalar bodies. The
    // unroller then handles dependency-heavy scalar loops and vector cleanup
    // tails without hiding the canonical reduction shape from vectorization.
    if (options.unroll_loops) {
        pipeline.add(
            PassId::LoopUnrolling,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                unroll_loops(function, hir_module, options,
                             analyses.loops().canonical_loops());
                return PassResult::changed_cfg();
            });
    }
    if (options.tree_reassoc) {
        pipeline.add(
            PassId::UnitRecurrenceReassociation,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                return reassociate_unit_recurrence_adds(
                           function, hir_module,
                           analyses.loops().canonical_loops(),
                           analyses.uses())
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
        pipeline.add(
            PassId::RecurrenceRebalancing,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                return rebalance_unsigned_add_recurrences(
                           function, hir_module, options,
                           analyses.uses())
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
    }
    if (options.tree_slp_vectorize) {
        pipeline.add(
            PassId::SlpVectorization,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                vectorize_slp(function, hir_module, subtarget, options);
                return PassResult::changed_values();
            });
    }
    // After vectorization, so loop shapes keep their divisions; before LICM,
    // which hoists a reciprocal's materialization, value numbering, which
    // shares the quotient of a division and a remainder, and SLSR, which
    // rewrites the remainder's constant multiply.
    if (options.div_by_constant) {
        pipeline.add(
            PassId::DivisionByConstant,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                return reduce_constant_divisions(function, hir_module,
                                                 subtarget, options)
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
    }
    if (options.move_loop_invariants) {
        pipeline.add(
            PassId::LoopInvariantMotion,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                move_loop_invariants(
                    function, hir_module, subtarget,
                    analyses.loops().canonical_loops());
                return PassResult::changed_values();
            });
    }
    // LICM can place equivalent constants and derived expressions from
    // distinct branch arms in one preheader. Run local value numbering again
    // there so size-oriented code does not reserve and save separate
    // registers for identical loop-invariant bounds.
    if (options.tree_fre) {
        pipeline.add(
            PassId::RedundantExpressionElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                eliminate_fully_redundant_expressions(function);
                return PassResult::changed_values();
            });
    }
    if (options.tree_ccp) {
        pipeline.add(
            PassId::IntegerSimplification,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                simplify_integer_operations(function, hir_module);
                if (options.tree_copy_prop) {
                    propagate_trivial_copies(function);
                }
                return PassResult::changed_values();
            });
    }
    if (options.tree_slsr) {
        pipeline.add(
            PassId::StraightLineStrengthReduction,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                return reduce_constant_multiplications(
                           function, hir_module, options)
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
    }
    if (options.tree_bit_ccp) {
        pipeline.add(
            PassId::BitwiseValueNarrowing,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                return narrow_bitwise_values(
                           function, hir_module, subtarget.target())
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
    }
    // Keep shift/or forms visible to loop and SLP vectorization. Scalar
    // survivors become target-independent rotates only after those passes,
    // and -fpeephole2 remains independent from -ftree-ccp.
    if (options.peephole2) {
        pipeline.add(
            PassId::BitwiseCanonicalization,
            [&](ManagedFunction& function,
                FunctionAnalysisManager&) {
                return canonicalize_bitwise_operations(function, hir_module)
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
    }
    // Simplification and loop transforms can expose equivalent recurrences
    // after the first induction pass. Canonicalize them while SSA IDs are
    // still sparse; final DCE performs the safe global compaction.
    if (options.ivopts) {
        pipeline.add(
            PassId::UnitInductionExitSelection,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                return select_unit_induction_exits(
                           function, hir_module,
                           analyses.loops().canonical_loops())
                    ? PassResult::changed_values()
                    : PassResult::unchanged();
            });
        pipeline.add(
            PassId::InductionCoalescing,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                coalesce_equivalent_inductions(
                    function, hir_module,
                    analyses.loops().canonical_loops(), false,
                    options.optimize_for == OptimizationGoal::Size ||
                        options.optimize_for ==
                            OptimizationGoal::MinimumSize);
                return PassResult::changed_values();
            });
        pipeline.add(
            PassId::AddressInductionStrengthReduction,
            [&](ManagedFunction& function,
                FunctionAnalysisManager& analyses) {
                const bool changed = reduce_affine_address_inductions(
                    function, hir_module, subtarget.target(), options,
                    analyses.loops().canonical_loops());
                return changed ? PassResult::changed_values()
                               : PassResult::unchanged();
            });
    }
    // Keep canonical top-tested loops available to vectorization, unrolling,
    // LICM, and induction selection. Rotate only their surviving scalar CFGs
    // late, when a bottom test removes the repeated header transfer without
    // hiding those higher-level loop shapes.
    if (options.tree_loop_rotate) {
        pipeline.add(
            PassId::LoopRotation,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                const bool changed = rotate_guarded_loops(function);
                if (changed) {
                    // Rotation clones the continuation predicate after the
                    // ordinary scalar simplification rounds. Re-canonicalize
                    // boolean cast/compare chains so machine compare-branch
                    // fusion sees the same direct predicate at both tests.
                    if (options.tree_ccp) {
                        simplify_integer_operations(function, hir_module);
                    }
                    if (options.tree_copy_prop) {
                        propagate_trivial_copies(function);
                    }
                }
                return changed ? PassResult::changed_cfg()
                               : PassResult::unchanged();
            });
    }
    if (options.fast_math || options.finite_math_only ||
        !options.signed_zeros) {
        pipeline.add(
            PassId::FloatingSimplification,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                simplify_floating_math(function, hir_module, options);
                return PassResult::changed_values();
            });
    }
    if (options.tree_cfg_cleanup) {
        pipeline.add(
            PassId::ForwardingBlockElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                return eliminate_forwarding_blocks(function)
                    ? PassResult::changed_cfg()
                    : PassResult::unchanged();
            });
    }
    if (options.tree_tail_merge &&
        (options.optimize_for == OptimizationGoal::Size ||
         options.optimize_for == OptimizationGoal::MinimumSize)) {
        pipeline.add(
            PassId::TailMerging,
            [&](ManagedFunction& function, FunctionAnalysisManager&) {
                return factor_common_phi_tails(function, hir_module)
                    ? PassResult::changed_cfg()
                    : PassResult::unchanged();
            });
    }
    if (options.tree_dce) {
        pipeline.add(
            PassId::DeadCodeElimination,
            [](ManagedFunction& function, FunctionAnalysisManager&) {
                eliminate_dead_values(function);
                return PassResult::changed_values();
            });
    }

    (void)pipeline.run(module);
}

bool specialize_surviving_calls(ManagedModule& module,
                                hir::Module& hir_module,
                                const CompilerOptions& options) {
    if (!options.ipa_cp_clone) return false;

    struct Site {
        hir::FunctionId caller;
        ValueId call;
        hir::FunctionId callee;
        std::string key;
        std::vector<std::optional<ManagedValue>> constants;
    };
    std::vector<Site> sites;
    for (const auto& caller : module.functions) {
        for (const auto& block : caller.blocks) {
            for (const auto id : block.values) {
                const auto& call = caller.values[id.value];
                if (call.kind != ValueKind::Call || call.must_tail ||
                    !call.callee ||
                    !module.owns(*call.callee)) {
                    continue;
                }
                const auto& entity =
                    hir_module.function(*call.callee);
                const auto* body = module.find(*call.callee);
                if (!body ||
                    entity.abi_contract != hir::AbiContract::Dynamic ||
                    entity.linkage == Linkage::Global ||
                    !body->labels.empty() ||
                    std::any_of(
                        body->values.begin(), body->values.end(),
                        [](const ManagedValue& value) {
                            return value.kind == ValueKind::PatchValue;
                        })) {
                    continue;
                }
                bool any_constant = false;
                Site site;
                site.caller = caller.source;
                site.call = id;
                site.callee = *call.callee;
                site.key = std::to_string(call.callee->value);
                site.constants.resize(call.call_arguments.size());
                for (std::size_t index = 0;
                     index < call.call_arguments.size(); ++index) {
                    const auto& argument = call.call_arguments[index];
                    if (!argument.value) {
                        site.key += "|_";
                        continue;
                    }
                    const auto& value =
                        caller.values[argument.value->value];
                    if (value.kind != ValueKind::ConstantInteger &&
                        value.kind != ValueKind::ConstantFloating) {
                        site.key += "|_";
                        continue;
                    }
                    any_constant = true;
                    site.constants[index] = value;
                    site.key +=
                        "|" + std::to_string(index) + ":" +
                        std::to_string(
                            static_cast<unsigned>(value.kind)) +
                        ":" + std::to_string(value.type.value) + ":" +
                        std::to_string(value.integer) + ":" +
                        std::to_string(value.integer_high);
                }
                if (any_constant) sites.push_back(std::move(site));
            }
        }
    }

    std::unordered_map<std::string, hir::FunctionId> variants;
    std::unordered_map<std::uint32_t, unsigned> per_callee;
    bool changed = false;
    for (const auto& site : sites) {
        auto variant = variants.find(site.key);
        hir::FunctionId variant_id;
        if (variant == variants.end()) {
            if (variants.size() >= 32 ||
                per_callee[site.callee.value] >= 4) {
                continue;
            }
            const auto* original_body = module.find(site.callee);
            if (!original_body) continue;
            const auto original_entity =
                hir_module.function(site.callee);
            variant_id = {
                static_cast<std::uint32_t>(
                    hir_module.functions.size())};

            auto entity = original_entity;
            entity.id = variant_id;
            entity.source_name += "$const";
            entity.link_symbol =
                "__cross_clone_" +
                std::to_string(site.callee.value) + "_" +
                std::to_string(variant_id.value);
            entity.linkage = Linkage::Static;
            entity.abi_contract = hir::AbiContract::Dynamic;
            entity.abi_explicit = false;
            entity.labels.clear();
            hir_module.functions.push_back(std::move(entity));

            auto clone = *original_body;
            clone.source = variant_id;
            clone.labels.clear();
            auto& entry = clone.blocks[clone.entry.value].values;
            auto insertion = std::find_if(
                entry.begin(), entry.end(),
                [&](ValueId id) {
                    return clone.values[id.value].kind !=
                           ValueKind::Parameter;
                });
            for (std::size_t index = 0;
                 index < site.constants.size() &&
                 index < clone.parameters.size(); ++index) {
                if (!site.constants[index]) continue;
                auto constant = *site.constants[index];
                constant.id = {
                    static_cast<std::uint32_t>(
                        clone.values.size())};
                constant.location =
                    clone.values[clone.parameters[index].value]
                        .location;
                constant.parameter_index = 0;
                constant.slot.reset();
                constant.callee.reset();
                constant.label.reset();
                constant.object.reset();
                constant.patch_sink.reset();
                constant.patch_id = 0;
                constant.is_volatile_access = false;
                constant.effect_input.reset();
                constant.effect_output.reset();
                constant.operands.clear();
                constant.call_arguments.clear();
                constant.incoming.clear();
                const auto id = constant.id;
                clone.values.push_back(std::move(constant));
                insertion = entry.insert(insertion, id);
                ++insertion;
                replace_value_uses(
                    clone, clone.parameters[index], id);
            }
            module.definitions.insert(variant_id.value);
            module.functions.push_back(std::move(clone));
            variants.emplace(site.key, variant_id);
            ++per_callee[site.callee.value];
        } else {
            variant_id = variant->second;
        }

        const auto caller = std::find_if(
            module.functions.begin(), module.functions.end(),
            [&](const ManagedFunction& function) {
                return function.source == site.caller;
            });
        if (caller == module.functions.end() ||
            site.call.value >= caller->values.size()) {
            continue;
        }
        auto& call = caller->values[site.call.value];
        if (call.kind != ValueKind::Call ||
            call.callee != site.callee) {
            continue;
        }
        call.callee = variant_id;
        changed = true;
    }
    return changed;
}

} // namespace cross::mir
