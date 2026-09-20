// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/data_ir.hpp"

#include "common/diagnostic.hpp"
#include "common/floating_bits.hpp"
#include "common/integer_semantics.hpp"
#include "frontend/ast.hpp"
#include "middle/initializer.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cross::data {
namespace {

const Attribute* attribute(const ObjectDecl& object, std::string_view name) {
    for (const auto& item : object.attributes) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

bool supported_attribute(std::string_view name) {
    return name == "link_name" || name == "section" || name == "aligned" ||
           name == "noinit" || name == "retain" || name == "used" ||
           name == "weak" || name == "visibility" ||
           name == "alias" || name == "weakref" ||
           name == "thread_local" || name == "tls_model";
}

bool marker_attribute(const ObjectDecl& object, std::string_view name,
                      Diagnostics& diagnostics) {
    const auto* item = attribute(object, name);
    if (!item) return false;
    if (!item->arguments.empty()) {
        diagnostics.error(item->location,
                          std::string(name) + " does not take arguments");
    }
    return true;
}

std::string string_attribute(const ObjectDecl& object,
                             std::string_view name,
                             Diagnostics& diagnostics) {
    const auto* item = attribute(object, name);
    if (!item) return {};
    if (item->arguments.size() != 1) {
        diagnostics.error(item->location,
                          std::string(name) + " requires one string argument");
        return {};
    }
    const auto value = decode_string_literal(item->arguments.front());
    if (!value) {
        diagnostics.error(item->location,
                          std::string(name) + " argument must be a string literal");
        return {};
    }
    return *value;
}

unsigned type_size(const hir::Module& module, hir::TypeId id,
                   const Subtarget& subtarget) {
    const auto& type = module.type(id);
    const auto address_bytes = subtarget.abi_info().address_bits / 8;
    if (type.kind == hir::Type::Kind::Pointer) return address_bytes;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto size = module.record(*type.record).size;
        return size <= std::numeric_limits<unsigned>::max()
                   ? static_cast<unsigned>(size)
                   : 0;
    }
    if (type.kind == hir::Type::Kind::Vector ||
        type.kind == hir::Type::Kind::Array) {
        if ((type.kind == hir::Type::Kind::Vector && type.scalable) ||
            !type.element || type.lanes == 0) {
            return 0;
        }
        const auto element = type_size(module, *type.element, subtarget);
        if (element == 0 ||
            type.lanes > std::numeric_limits<unsigned>::max() / element) {
            return 0;
        }
        return element * type.lanes;
    }
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8: return 1;
    case BuiltinType::I16:
    case BuiltinType::U16: return 2;
    case BuiltinType::I32:
    case BuiltinType::U32:
    case BuiltinType::F32: return 4;
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::F64: return 8;
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
    case BuiltinType::Fptr:
    case BuiltinType::Label: return address_bytes;
    case BuiltinType::I128:
    case BuiltinType::U128:
    case BuiltinType::F128: return 16;
    case BuiltinType::F80:
        return subtarget.target().data_layout.f80_storage_bytes;
    case BuiltinType::Void: return 0;
    }
    return 0;
}

unsigned natural_alignment(const hir::Module& module, hir::TypeId id,
                           unsigned size, const Subtarget& subtarget) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        return module.record(*type.record).alignment;
    }
    if (type.kind == hir::Type::Kind::Array && type.element) {
        const auto element_size = type_size(module, *type.element, subtarget);
        return natural_alignment(module, *type.element, element_size,
                                 subtarget);
    }
    if (type.kind == hir::Type::Kind::Builtin &&
        type.builtin == BuiltinType::F80) {
        return subtarget.target().data_layout.f80_alignment;
    }
    return std::max(
        1U, std::min(size,
                     subtarget.target().data_layout.natural_alignment_limit));
}

std::optional<UInt128> integer_value(const Expr& expression) {
    if (expression.evaluated_integer) return expression.evaluated_integer->value;
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return integer_value(*expression.left);
    }
    if (expression.kind == Expr::Kind::Unary && expression.left &&
        (expression.text == "+" || expression.text == "-")) {
        auto value = integer_value(*expression.left);
        if (value && expression.text == "-") *value = negate(*value);
        return value;
    }
    if (expression.kind != Expr::Kind::Integer) return std::nullopt;
    auto text = expression.text;
    static constexpr std::string_view suffixes[] = {
        "iptr", "uptr", "i128", "u128", "i64", "u64", "i32", "u32",
        "i16", "u16", "i8", "u8",
    };
    for (const auto suffix : suffixes) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.resize(text.size() - suffix.size());
            break;
        }
    }
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    unsigned base = 10;
    std::string_view digits = text;
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        base = 16;
        digits.remove_prefix(2);
    } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
        base = 2;
        digits.remove_prefix(2);
    } else if (digits.size() > 1 && digits.front() == '0') {
        base = 8;
        digits.remove_prefix(1);
    }
    return parse_uint128(digits, base);
}

std::optional<std::string> floating_text(const Expr& expression) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return floating_text(*expression.left);
    }
    if (expression.kind == Expr::Kind::Unary && expression.left &&
        (expression.text == "+" || expression.text == "-")) {
        auto text = floating_text(*expression.left);
        if (!text || expression.text == "+") return text;
        if (!text->empty() && text->front() == '-') text->erase(text->begin());
        else text->insert(text->begin(), '-');
        return text;
    }
    if (expression.kind != Expr::Kind::Floating) return std::nullopt;
    auto text = expression.text;
    static constexpr std::string_view suffixes[] = {
        "f128", "fptr", "f32", "f64", "f80",
    };
    for (const auto suffix : suffixes) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.resize(text.size() - suffix.size());
            break;
        }
    }
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    return text;
}

class BigUnsigned {
public:
    explicit BigUnsigned(std::uint32_t value = 0) {
        if (value) words_.push_back(value);
    }
    [[nodiscard]] bool zero() const { return words_.empty(); }
    [[nodiscard]] unsigned bit_length() const {
        return words_.empty()
                   ? 0U
                   : static_cast<unsigned>((words_.size() - 1) * 32) +
                         static_cast<unsigned>(32 -
                                               std::countl_zero(words_.back()));
    }
    void multiply(std::uint32_t value) {
        std::uint64_t carry{};
        for (auto& word : words_) {
            const auto product =
                static_cast<std::uint64_t>(word) * value + carry;
            word = static_cast<std::uint32_t>(product);
            carry = product >> 32;
        }
        if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
    }
    void add(std::uint32_t value) {
        std::uint64_t carry = value;
        for (auto& word : words_) {
            const auto sum = static_cast<std::uint64_t>(word) + carry;
            word = static_cast<std::uint32_t>(sum);
            carry = sum >> 32;
            if (!carry) return;
        }
        if (carry) words_.push_back(static_cast<std::uint32_t>(carry));
    }
    [[nodiscard]] BigUnsigned shifted(unsigned bits) const {
        if (zero()) return BigUnsigned{};
        BigUnsigned result;
        result.words_.assign(bits / 32, 0);
        std::uint64_t carry{};
        for (const auto word : words_) {
            const auto next =
                (static_cast<std::uint64_t>(word) << (bits % 32)) | carry;
            result.words_.push_back(static_cast<std::uint32_t>(next));
            carry = next >> 32;
        }
        if (carry) result.words_.push_back(static_cast<std::uint32_t>(carry));
        return result;
    }
    [[nodiscard]] int compare(const BigUnsigned& other) const {
        if (words_.size() != other.words_.size()) {
            return words_.size() < other.words_.size() ? -1 : 1;
        }
        for (std::size_t index = words_.size(); index != 0; --index) {
            if (words_[index - 1] == other.words_[index - 1]) continue;
            return words_[index - 1] < other.words_[index - 1] ? -1 : 1;
        }
        return 0;
    }
    void subtract(const BigUnsigned& other) {
        std::uint64_t borrow{};
        for (std::size_t index = 0; index < words_.size(); ++index) {
            const auto subtrahend =
                (index < other.words_.size() ? other.words_[index] : 0) +
                borrow;
            const auto current = static_cast<std::uint64_t>(words_[index]);
            words_[index] =
                static_cast<std::uint32_t>(current - subtrahend);
            borrow = current < subtrahend ? 1 : 0;
        }
        while (!words_.empty() && words_.back() == 0) words_.pop_back();
    }

private:
    std::vector<std::uint32_t> words_;
};

struct Rounded {
    UInt128 value;
    bool overflow{};
};

Rounded rounded_quotient(BigUnsigned numerator, BigUnsigned denominator) {
    Rounded result;
    if (denominator.zero()) return {{}, true};
    int shift = static_cast<int>(numerator.bit_length()) -
                static_cast<int>(denominator.bit_length());
    for (; shift >= 0; --shift) {
        const auto candidate = denominator.shifted(static_cast<unsigned>(shift));
        if (numerator.compare(candidate) < 0) continue;
        numerator.subtract(candidate);
        if (shift >= 128) result.overflow = true;
        else if (shift < 64) {
            result.value.low |= std::uint64_t{1} << static_cast<unsigned>(shift);
        } else {
            result.value.high |=
                std::uint64_t{1} << static_cast<unsigned>(shift - 64);
        }
    }
    const auto relation = numerator.shifted(1).compare(denominator);
    if (relation > 0 ||
        (relation == 0 && (result.value.low & 1U) != 0)) {
        bool overflow = false;
        result.value = add(result.value, {1}, &overflow);
        result.overflow |= overflow;
    }
    return result;
}

struct Rational {
    BigUnsigned numerator;
    BigUnsigned denominator{1};
    bool negative{};
};

std::optional<Rational> parse_rational(std::string text) {
    Rational value;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        value.negative = text.front() == '-';
        text.erase(text.begin());
    }
    if (text.empty() || text.size() > 10000) return std::nullopt;
    const bool hexadecimal = text.starts_with("0x") || text.starts_with("0X");
    const auto marker = text.find_first_of(hexadecimal ? "pP" : "eE");
    std::int64_t exponent{};
    if (marker != std::string::npos) {
        auto part = std::string_view(text).substr(marker + 1);
        if (!part.empty() && part.front() == '+') part.remove_prefix(1);
        const auto parsed =
            std::from_chars(part.data(), part.data() + part.size(), exponent);
        if (part.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != part.data() + part.size()) {
            return std::nullopt;
        }
        text.resize(marker);
    }
    bool point{};
    bool digit{};
    std::int64_t fraction{};
    for (std::size_t index = hexadecimal ? 2U : 0U; index < text.size();
         ++index) {
        const auto ch = text[index];
        if (ch == '.') {
            if (point) return std::nullopt;
            point = true;
            continue;
        }
        unsigned current{};
        if (ch >= '0' && ch <= '9') current = static_cast<unsigned>(ch - '0');
        else if (hexadecimal && ch >= 'a' && ch <= 'f') {
            current = 10U + static_cast<unsigned>(ch - 'a');
        } else if (hexadecimal && ch >= 'A' && ch <= 'F') {
            current = 10U + static_cast<unsigned>(ch - 'A');
        } else {
            return std::nullopt;
        }
        if (current >= (hexadecimal ? 16U : 10U)) return std::nullopt;
        value.numerator.multiply(hexadecimal ? 16U : 10U);
        value.numerator.add(current);
        digit = true;
        if (point) ++fraction;
    }
    if (!digit) return std::nullopt;
    const auto adjustment = exponent - (hexadecimal ? 4 * fraction : fraction);
    if (adjustment > 100000 || adjustment < -100000) return std::nullopt;
    if (hexadecimal) {
        if (adjustment >= 0) {
            value.numerator =
                value.numerator.shifted(static_cast<unsigned>(adjustment));
        } else {
            value.denominator =
                value.denominator.shifted(static_cast<unsigned>(-adjustment));
        }
    } else if (adjustment >= 0) {
        for (std::int64_t index = 0; index < adjustment; ++index) {
            value.numerator.multiply(10);
        }
    } else {
        for (std::int64_t index = 0; index < -adjustment; ++index) {
            value.denominator.multiply(10);
        }
    }
    return value;
}

std::optional<UInt128> parse_binary128(std::string text) {
    const auto input = parse_rational(std::move(text));
    if (!input) return std::nullopt;
    const auto& rational = *input;
    if (rational.numerator.zero()) {
        return UInt128{0, rational.negative ? std::uint64_t{1} << 63 : 0};
    }
    int exponent = static_cast<int>(rational.numerator.bit_length()) -
                   static_cast<int>(rational.denominator.bit_length());
    if (exponent >= 0) {
        if (rational.numerator.compare(
                rational.denominator.shifted(static_cast<unsigned>(exponent))) <
            0) {
            --exponent;
        }
    } else if (rational.numerator
                       .shifted(static_cast<unsigned>(-exponent))
                       .compare(rational.denominator) < 0) {
        --exponent;
    }
    constexpr UInt128 hidden{0, std::uint64_t{1} << 48};
    std::uint16_t field{};
    UInt128 fraction{};
    if (exponent > 16383) {
        field = 0x7fff;
    } else if (exponent >= -16382) {
        const auto scale = 112 - exponent;
        auto numerator = rational.numerator;
        auto denominator = rational.denominator;
        if (scale >= 0) {
            numerator = numerator.shifted(static_cast<unsigned>(scale));
        } else {
            denominator = denominator.shifted(static_cast<unsigned>(-scale));
        }
        const auto rounded =
            rounded_quotient(std::move(numerator), std::move(denominator));
        if (rounded.overflow || bit(rounded.value, 113)) {
            if (++exponent > 16383) field = 0x7fff;
            else field = static_cast<std::uint16_t>(exponent + 16383);
        } else {
            field = static_cast<std::uint16_t>(exponent + 16383);
            fraction = subtract(rounded.value, hidden);
        }
    } else {
        const auto rounded = rounded_quotient(
            rational.numerator.shifted(16494), rational.denominator);
        if (rounded.overflow || !(rounded.value < hidden)) field = 1;
        else fraction = rounded.value;
    }
    auto high = (static_cast<std::uint64_t>(field) << 48) |
                (fraction.high & 0x0000ffffffffffffULL);
    if (rational.negative) high |= std::uint64_t{1} << 63;
    return UInt128{fraction.low, high};
}

const hir::Object* find_object(const hir::Module& module,
                               std::string_view name,
                               std::string_view source_unit) {
    for (const auto& object : module.objects) {
        if (object.source_name == name && object.linkage == Linkage::Static &&
            object.source_unit == source_unit) {
            return &object;
        }
    }
    for (const auto& object : module.objects) {
        if (object.source_name == name && object.linkage != Linkage::Static) {
            return &object;
        }
    }
    return nullptr;
}

const hir::Function* find_function(const hir::Module& module,
                                   std::string_view name,
                                   std::string_view source_unit) {
    for (const auto& function : module.functions) {
        if (function.source_name == name &&
            function.linkage == Linkage::Static &&
            function.source_unit == source_unit) {
            return &function;
        }
    }
    for (const auto& function : module.functions) {
        if (function.source_name == name &&
            function.linkage != Linkage::Static) {
            return &function;
        }
    }
    return nullptr;
}

std::optional<std::string> qualified_owner_name(std::string_view name,
                                                AddressScope scope) {
    if (name.find("::") != std::string_view::npos) return std::nullopt;
    const auto separator = scope.source_name.rfind("::");
    if (separator == std::string::npos) return std::nullopt;
    return std::string(scope.source_name.substr(0, separator + 2)) +
           std::string(name);
}

const hir::Object* find_object(const hir::Module& module,
                               std::string_view name,
                               AddressScope scope) {
    if (const auto qualified = qualified_owner_name(name, scope)) {
        if (const auto* object = find_object(module, *qualified,
                                             scope.source_unit)) return object;
    }
    return find_object(module, name, scope.source_unit);
}

const hir::Function* find_function(const hir::Module& module,
                                   std::string_view name,
                                   AddressScope scope) {
    if (const auto qualified = qualified_owner_name(name, scope)) {
        if (const auto* function = find_function(module, *qualified,
                                                 scope.source_unit))
            return function;
    }
    return find_function(module, name, scope.source_unit);
}

struct AddressValue {
    AddressConstant address;
    std::optional<hir::TypeId> pointee;
    TypePtr cast_pointee;
    bool integer{};
};

std::uint64_t source_storage_size(const hir::Module& module,
                                  const TypePtr& type,
                                  const Subtarget& subtarget) {
    if (!type) return 0;
    if (type->kind == Type::Kind::Pointer)
        return (module.address_bits + 7U) / 8U;
    if (type->kind == Type::Kind::Record) {
        const auto* record = module.record(type->nominal_name);
        return record && record->complete ? record->size : 0;
    }
    if (type->kind == Type::Kind::Array && type->element) {
        const auto element = source_storage_size(module, type->element,
                                                 subtarget);
        return element && type->lanes <=
                std::numeric_limits<std::uint64_t>::max() / element
            ? element * type->lanes : 0;
    }
    if (type->kind == Type::Kind::Vector)
        return type->scalable ? 0 : (type_bits(type) + 7U) / 8U;
    if (type->kind != Type::Kind::Builtin) return 0;
    const auto builtin = module.builtin(type->builtin);
    return builtin ? type_size(module, *builtin, subtarget) : 0;
}

bool add_address_addend(AddressConstant& address, std::uint64_t bytes) {
    if (bytes > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) return false;
    const auto amount = static_cast<std::int64_t>(bytes);
    if (address.addend > std::numeric_limits<std::int64_t>::max() - amount)
        return false;
    address.addend += amount;
    return true;
}

bool compatible_static_pointee(const hir::Module& module, hir::TypeId source,
                               const TypePtr& destination,
                               unsigned depth = 0) {
    if (!destination || depth >= 32) return false;
    const auto& from = module.type(source);
    if ((from.is_const && !destination->is_const) ||
        (from.is_volatile && !destination->is_volatile) ||
        from.is_atomic != destination->is_atomic) return false;
    if (depth == 0 && destination->kind == Type::Kind::Builtin &&
        destination->builtin == BuiltinType::Void)
        return from.kind != hir::Type::Kind::Function;
    switch (destination->kind) {
    case Type::Kind::Builtin:
        return from.kind == hir::Type::Kind::Builtin &&
               from.builtin == destination->builtin;
    case Type::Kind::Pointer:
        return from.kind == hir::Type::Kind::Pointer && from.pointee &&
               from.address_space == destination->address_space &&
               compatible_static_pointee(module, *from.pointee,
                                          destination->pointee, depth + 1);
    case Type::Kind::Array:
        return from.kind == hir::Type::Kind::Array && from.element &&
               from.lanes == destination->lanes &&
               compatible_static_pointee(module, *from.element,
                                          destination->element, depth + 1);
    case Type::Kind::Record:
        return from.kind == hir::Type::Kind::Record &&
               from.nominal_name == destination->nominal_name;
    case Type::Kind::Vector:
        return from.kind == hir::Type::Kind::Vector && from.element &&
               from.lanes == destination->lanes &&
               from.scalable == destination->scalable &&
               compatible_static_pointee(module, *from.element,
                                          destination->element, depth + 1);
    default:
        return false;
    }
}

std::optional<AddressValue> address_value(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget);

std::optional<AddressValue> address_designator(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left)
        return address_designator(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Name) {
        if (const auto* object =
                find_object(module, expression.text, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Object;
            result.address.object = object->id;
            result.pointee = object->type;
            return result;
        }
        if (const auto* function =
                find_function(module, expression.text, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Function;
            result.address.function = function->id;
            return result;
        }
        return std::nullopt;
    }
    if (expression.kind != Expr::Kind::Binary || !expression.left ||
        !expression.right) return std::nullopt;
    if (expression.text == "member" &&
        expression.right->kind == Expr::Kind::Name) {
        auto base = address_designator(module, scope, *expression.left,
                                       subtarget);
        if (!base) return std::nullopt;
        const hir::Record* record{};
        if (base->pointee) {
            const auto& type = module.type(*base->pointee);
            if (type.kind == hir::Type::Kind::Record && type.record)
                record = &module.record(*type.record);
        } else if (base->cast_pointee &&
                   base->cast_pointee->kind == Type::Kind::Record) {
            record = module.record(base->cast_pointee->nominal_name);
        }
        if (!record) return std::nullopt;
        const auto* member = module.member(record->id,
                                            expression.right->text);
        if (!member || member->bit_width ||
            !add_address_addend(base->address, member->offset))
            return std::nullopt;
        base->pointee = member->type;
        base->cast_pointee.reset();
        return base;
    }
    if (expression.text == "index") {
        auto base = address_designator(module, scope, *expression.left,
                                       subtarget);
        if (base && base->pointee) {
            const auto& selected = module.type(*base->pointee);
            if (selected.kind == hir::Type::Kind::Array && selected.element) {
                base->pointee = *selected.element;
            } else {
                base.reset();
            }
        }
        if (!base)
            base = address_value(module, scope, *expression.left, subtarget);
        if (!base || (!base->pointee && !base->cast_pointee) ||
            base->integer) return std::nullopt;
        const auto offset = integer_value(*expression.right);
        if (!offset || offset->high != 0 ||
            offset->low > std::numeric_limits<std::int64_t>::max())
            return std::nullopt;
        const auto size = base->cast_pointee
            ? source_storage_size(module, base->cast_pointee, subtarget)
            : type_size(module, *base->pointee, subtarget);
        if (size == 0 || offset->low >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) / size ||
            !add_address_addend(base->address, offset->low * size))
            return std::nullopt;
        return base;
    }
    return std::nullopt;
}

std::optional<AddressValue> address_value(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left)
        return address_value(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Unary && expression.text == "&" &&
        expression.left)
        return address_designator(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Name) {
        if (const auto* object =
                find_object(module, expression.text, scope)) {
            const auto& type = module.type(object->type);
            if (type.kind != hir::Type::Kind::Array || !type.element)
                return std::nullopt;
            AddressValue result;
            result.address.kind = AddressKind::Object;
            result.address.object = object->id;
            result.pointee = *type.element;
            return result;
        }
        if (const auto* function =
                find_function(module, expression.text, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Function;
            result.address.function = function->id;
            return result;
        }
        const auto split = expression.text.rfind("::");
        if (split == std::string::npos) return std::nullopt;
        const auto* function = find_function(
            module, expression.text.substr(0, split), scope);
        if (!function) return std::nullopt;
        const auto* label = module.label(
            function->id, expression.text.substr(split + 2));
        if (!label) return std::nullopt;
        AddressValue result;
        result.address.kind = AddressKind::Label;
        result.address.function = function->id;
        result.address.label = label->id;
        return result;
    }
    if (expression.kind == Expr::Kind::Binary &&
        expression.text == "member") {
        auto value = address_designator(module, scope, expression,
                                         subtarget);
        if (!value || !value->pointee) return std::nullopt;
        const auto& selected = module.type(*value->pointee);
        if (selected.kind != hir::Type::Kind::Array || !selected.element)
            return std::nullopt;
        value->pointee = *selected.element;
        return value;
    }
    if (expression.kind == Expr::Kind::Cast && expression.left &&
        expression.type) {
        auto value = address_value(module, scope, *expression.left,
                                   subtarget);
        if (!value) return std::nullopt;
        if (expression.type->kind == Type::Kind::Builtin) {
            const auto kind = expression.type->builtin;
            if ((kind != BuiltinType::Uptr && kind != BuiltinType::Iptr &&
                 kind != BuiltinType::U32 && kind != BuiltinType::I32 &&
                 kind != BuiltinType::U64 && kind != BuiltinType::I64) ||
                (kind == BuiltinType::Uptr || kind == BuiltinType::Iptr
                    ? module.address_bits
                    : type_bits(expression.type)) != module.address_bits) {
                return std::nullopt;
            }
            value->integer = true;
            value->pointee.reset();
            value->cast_pointee.reset();
            return value;
        }
        if (expression.type->kind == Type::Kind::Pointer) {
            if (!expression.type->pointee ||
                (value->pointee &&
                 !compatible_static_pointee(
                     module, *value->pointee,
                     expression.type->pointee)) ||
                (value->cast_pointee &&
                 !same_type(value->cast_pointee,
                            expression.type->pointee))) {
                return std::nullopt;
            }
            value->integer = false;
            value->pointee.reset();
            value->cast_pointee = expression.type->pointee;
            return value;
        }
        return std::nullopt;
    }
    if (expression.kind == Expr::Kind::Binary && expression.left &&
        expression.right &&
        (expression.text == "+" || expression.text == "-")) {
        auto value = address_value(module, scope, *expression.left,
                                   subtarget);
        const Expr* offset_expression = expression.right.get();
        if (!value && expression.text == "+") {
            value = address_value(module, scope, *expression.right,
                                  subtarget);
            offset_expression = expression.left.get();
        }
        if (!value) return std::nullopt;
        const auto offset = integer_value(*offset_expression);
        if (!offset || offset->high != 0 ||
            offset->low > std::numeric_limits<std::int64_t>::max())
            return std::nullopt;
        const auto scale = value->integer ? std::uint64_t{1}
            : value->cast_pointee
                ? source_storage_size(module, value->cast_pointee, subtarget)
            : value->pointee
                ? type_size(module, *value->pointee, subtarget)
                : std::uint64_t{0};
        if (scale == 0 || offset->low >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) / scale)
            return std::nullopt;
        const auto amount = static_cast<std::int64_t>(offset->low * scale);
        if (expression.text == "+") {
            if (!add_address_addend(value->address,
                                    static_cast<std::uint64_t>(amount)))
                return std::nullopt;
        } else {
            if (value->address.addend <
                std::numeric_limits<std::int64_t>::min() + amount)
                return std::nullopt;
            value->address.addend -= amount;
        }
        return value;
    }
    return std::nullopt;
}

bool lower_scalar_initializer(Object& result, const hir::Module& module,
                              const hir::Object& entity,
                              const Expr& expression,
                              const Subtarget& subtarget,
                              Diagnostics& diagnostics) {
    const auto& type = module.type(entity.type);
    if (type.kind == hir::Type::Kind::Pointer ||
        (type.kind == hir::Type::Kind::Builtin &&
         type.builtin == BuiltinType::Label)) {
        const auto address = relocatable_address(
            module, {entity.source_name, entity.source_unit}, expression,
            subtarget, false);
        if (!address) {
            diagnostics.error(
                expression.location,
                "global pointer/label initializer is not an address constant");
            return false;
        }
        result.address = *address;
        const auto expected_function =
            type.kind == hir::Type::Kind::Pointer && type.pointee &&
            module.type(*type.pointee).kind == hir::Type::Kind::Function;
        if (expected_function ||
            result.address->kind == AddressKind::Function) {
            const auto actual =
                hir::call_signature(module, result.address->function, {});
            if (!expected_function ||
                result.address->kind != AddressKind::Function || !actual ||
                module.type(*type.pointee).function != actual) {
                diagnostics.error(expression.location,
                                  "global function-pointer initializer has an "
                                  "incompatible signature or ABI");
                return false;
            }
        }
        if (result.address->kind == AddressKind::Object &&
            result.address->object &&
            module.object(*result.address->object).is_thread_local) {
            diagnostics.error(
                expression.location,
                "a thread-local address is not an ordinary static relocation");
            return false;
        }
        result.initializer = InitializerKind::Address;
        return true;
    }
    if (type.kind == hir::Type::Kind::Builtin &&
        type.builtin <= BuiltinType::Uptr) {
        if (result.size == (module.address_bits + 7U) / 8U) {
            const auto address = relocatable_address(
                module, {entity.source_name, entity.source_unit}, expression,
                subtarget, true);
            if (address) {
                if (address->kind == AddressKind::Object &&
                    address->object &&
                    module.object(*address->object).is_thread_local) {
                    diagnostics.error(expression.location,
                        "a thread-local address is not an ordinary static relocation");
                    return false;
                }
                result.initializer = InitializerKind::Address;
                result.address = *address;
                return true;
            }
        }
        const auto value = integer_value(expression);
        if (!value) {
            diagnostics.error(expression.location,
                              "global integer initializer is not a constant");
            return false;
        }
        result.initializer = InitializerKind::Integer;
        result.bits = mask_to(*value, result.size * 8);
        const Expr* source = &expression;
        while (source->kind == Expr::Kind::Parenthesized && source->left)
            source = source->left.get();
        if (source->evaluated_integer) {
            const auto source_type = builtin_type(source->evaluated_integer->type);
            const auto source_bits = source_type->builtin == BuiltinType::Iptr ||
                source_type->builtin == BuiltinType::Uptr ? subtarget.abi_info().address_bits : type_bits(source_type);
            const bool source_signed = source_type->builtin == BuiltinType::I8 ||
                source_type->builtin == BuiltinType::I16 || source_type->builtin == BuiltinType::I32 ||
                source_type->builtin == BuiltinType::I64 || source_type->builtin == BuiltinType::I128 ||
                source_type->builtin == BuiltinType::Iptr;
            result.bits = convert_integer(*value, {source_bits, source_signed},
                {result.size * 8, false, type.builtin == BuiltinType::Bool});
        } else if (type.builtin == BuiltinType::Bool) {
            result.bits = UInt128{*value != UInt128{}};
        }
        return true;
    }
    const auto text = floating_text(expression);
    if (!text || type.kind != hir::Type::Kind::Builtin) {
        diagnostics.error(
            expression.location,
            "global initializer is not a supported scalar constant");
        return false;
    }
    result.initializer = InitializerKind::Floating;
    if (type.builtin == BuiltinType::F32 ||
        (type.builtin == BuiltinType::Fptr &&
         subtarget.abi_info().address_bits == 32)) {
        char* end{};
        const auto value = std::strtof(text->c_str(), &end);
        if (!end || *end != '\0') {
            diagnostics.error(expression.location, "invalid f32 literal");
            return false;
        }
        result.bits = {std::bit_cast<std::uint32_t>(value)};
        return true;
    }
    if (type.builtin == BuiltinType::F64 ||
        (type.builtin == BuiltinType::Fptr &&
         subtarget.abi_info().address_bits == 64)) {
        char* end{};
        const auto value = std::strtod(text->c_str(), &end);
        if (!end || *end != '\0') {
            diagnostics.error(expression.location, "invalid f64 literal");
            return false;
        }
        result.bits = {std::bit_cast<std::uint64_t>(value)};
        return true;
    }
    if (type.builtin == BuiltinType::F80) {
        const auto value = parse_extended80(*text);
        if (!value) {
            diagnostics.error(expression.location, "invalid f80 literal");
            return false;
        }
        result.bits = {value->significand, value->exponent_sign};
        return true;
    }
    if (type.builtin == BuiltinType::F128) {
        const auto value = parse_binary128_literal(*text);
        if (!value) {
            diagnostics.error(expression.location, "invalid f128 literal");
            return false;
        }
        result.bits = *value;
        return true;
    }
    diagnostics.error(expression.location,
                      "global initializer has incompatible floating type");
    return false;
}

void store_bits(std::vector<unsigned char>& bytes, unsigned offset,
                unsigned size, UInt128 value, ByteOrder order) {
    value = mask_to(value, size * 8);
    for (unsigned index = 0; index < size; ++index) {
        const auto source = order == ByteOrder::Little ? index
                                                       : size - index - 1;
        bytes[offset + index] = static_cast<unsigned char>(
            source < 8 ? value.low >> (source * 8)
                       : value.high >> ((source - 8) * 8));
    }
}

UInt128 load_bits(const std::vector<unsigned char>& bytes, unsigned offset,
                  unsigned size, ByteOrder order) {
    UInt128 value{};
    for (unsigned index = 0; index < size; ++index) {
        const auto destination = order == ByteOrder::Little
                                     ? index
                                     : size - index - 1;
        const auto byte = static_cast<std::uint64_t>(bytes[offset + index]);
        if (destination < 8) {
            value.low |= byte << (destination * 8);
        } else {
            value.high |= byte << ((destination - 8) * 8);
        }
    }
    return value;
}

bool lower_initializer(Object& result, const hir::Module& module,
                       const hir::Object& entity,
                       const ObjectDecl& declaration,
                       const Subtarget& subtarget,
                       Diagnostics& diagnostics) {
    if (!declaration.initializer) return true;
    const auto& expression = *declaration.initializer;
    const auto& type = module.type(entity.type);
    if (type.kind == hir::Type::Kind::Array && type.element &&
        module.type(*type.element).kind == hir::Type::Kind::Builtin &&
        module.type(*type.element).builtin == BuiltinType::U8 &&
        expression.kind == Expr::Kind::String) {
        const auto required = expression.string_value.size() + 1;
        if (required > result.size) {
            diagnostics.error(expression.location,
                              "string initializer does not fit in the u8 array");
            return false;
        }
        result.initializer = InitializerKind::Bytes;
        result.bytes.assign(result.size, 0);
        std::copy(expression.string_value.begin(),
                  expression.string_value.end(), result.bytes.begin());
        return true;
    }
    if (type.kind != hir::Type::Kind::Array &&
        type.kind != hir::Type::Kind::Record) {
        if (expression.kind == Expr::Kind::AggregateInitializer) {
            diagnostics.error(expression.location,
                              "brace initializer requires an aggregate object");
            return false;
        }
        return lower_scalar_initializer(result, module, entity, expression,
                                        subtarget, diagnostics);
    }
    if (expression.kind != Expr::Kind::AggregateInitializer) {
        diagnostics.error(expression.location,
                          "aggregate initializer requires a brace list");
        return false;
    }

    const auto plan = initializer::build(expression, entity.type, module,
                                         subtarget.target(), diagnostics);
    result.initializer = InitializerKind::Aggregate;
    result.bytes.assign(result.size, 0);
    bool valid = plan.valid;
    for (const auto& item : plan.items) {
        if (!item.expression) {
            valid = false;
            continue;
        }
        const auto item_size = hir::layout_size(
            module, item.type, subtarget.target());
        if (!item_size || *item_size > std::numeric_limits<unsigned>::max() ||
            item.offset > result.size ||
            *item_size > result.size - item.offset) {
            diagnostics.error(item.expression->location,
                              "aggregate initializer item exceeds object storage");
            valid = false;
            continue;
        }
        const auto& item_type = module.type(item.type);
        if (item_type.kind == hir::Type::Kind::Array && item_type.element &&
            module.type(*item_type.element).kind == hir::Type::Kind::Builtin &&
            module.type(*item_type.element).builtin == BuiltinType::U8 &&
            item.expression->kind == Expr::Kind::String) {
            const auto required = item.expression->string_value.size() + 1;
            if (required > *item_size) {
                diagnostics.error(
                    item.expression->location,
                    "string initializer does not fit in the u8 array");
                valid = false;
                continue;
            }
            std::copy(item.expression->string_value.begin(),
                      item.expression->string_value.end(),
                      result.bytes.begin() +
                          static_cast<std::ptrdiff_t>(item.offset));
            continue;
        }
        if (item_type.kind == hir::Type::Kind::Array ||
            item_type.kind == hir::Type::Kind::Record) {
            diagnostics.error(
                item.expression->location,
                "nested aggregate initialization requires a brace list");
            valid = false;
            continue;
        }
        Object scalar;
        scalar.location = item.expression->location;
        scalar.type = item.type;
        scalar.size = static_cast<unsigned>(*item_size);
        hir::Object scalar_entity = entity;
        scalar_entity.type = item.type;
        if (!lower_scalar_initializer(scalar, module, scalar_entity,
                                      *item.expression, subtarget,
                                      diagnostics)) {
            valid = false;
            continue;
        }
        const auto offset = static_cast<unsigned>(item.offset);
        if (scalar.initializer == InitializerKind::Integer ||
            scalar.initializer == InitializerKind::Floating) {
            const auto order = subtarget.target().data_layout.byte_order;
            if (item.bit_width) {
                const auto value_mask = mask_to(
                    bit_not(UInt128{}), *item.bit_width);
                const auto field_mask = shift_left(
                    value_mask, item.bit_offset);
                auto storage = load_bits(result.bytes, offset, scalar.size,
                                         order);
                storage = bit_or(
                    bit_and(storage, bit_not(field_mask)),
                    shift_left(bit_and(scalar.bits, value_mask),
                               item.bit_offset));
                store_bits(result.bytes, offset, scalar.size, storage,
                           order);
            } else {
                store_bits(result.bytes, offset, scalar.size, scalar.bits,
                           order);
            }
        } else if (scalar.initializer == InitializerKind::Address &&
                   scalar.address) {
            result.relocations.push_back(
                {offset, scalar.size, std::move(*scalar.address)});
        } else {
            diagnostics.error(item.expression->location,
                              "unsupported scalar aggregate initializer item");
            valid = false;
        }
    }
    std::sort(result.relocations.begin(), result.relocations.end(),
              [](const Relocation& left, const Relocation& right) {
                  return left.offset < right.offset;
              });
    return valid;
}

} // namespace

std::optional<AddressConstant> relocatable_address(
    const hir::Module& module, AddressScope scope, const Expr& expression,
    const Subtarget& subtarget, bool integer) {
    const auto value = address_value(module, scope, expression, subtarget);
    if (!value || value->integer != integer) return std::nullopt;
    return value->address;
}

std::optional<UInt128> parse_binary128_literal(std::string text) {
    return parse_binary128(std::move(text));
}

const Object* Module::find(hir::ObjectId id) const {
    const auto found = object_indices.find(id.value);
    return found == object_indices.end() ? nullptr : &objects[found->second];
}

Module lower(hir::Module& hir_module, const Subtarget& subtarget,
             Diagnostics& diagnostics) {
    Module result;
    result.address_bits = subtarget.abi_info().address_bits;
    result.byte_order = subtarget.target().data_layout.byte_order;
    result.objects.reserve(hir_module.objects.size());
    for (const auto& entity : hir_module.objects) {
        const auto* declaration =
            entity.definition ? entity.definition : entity.declarations.back();
        for (const auto* source : entity.declarations) {
            for (const auto& item : source->attributes) {
                if (!supported_attribute(item.name)) {
                    diagnostics.error(
                        item.location,
                        "attribute '" + item.name +
                            "' is not implemented yet for objects");
                }
            }
        }
        Object object;
        object.source = entity.id;
        object.location = declaration->location;
        object.type = entity.type;
        object.size = type_size(hir_module, entity.type, subtarget);
        if (object.size == 0) {
            diagnostics.error(declaration->location,
                              "object has incomplete storage type");
        }
        const auto natural =
            natural_alignment(hir_module, entity.type, object.size, subtarget);
        object.alignment = std::max(natural, entity.minimum_alignment);
        object.retain = marker_attribute(*declaration, "retain", diagnostics);
        object.used = marker_attribute(*declaration, "used", diagnostics);
        object.is_thread_local =
            marker_attribute(*declaration, "thread_local", diagnostics);
        object.tls_model =
            string_attribute(*declaration, "tls_model", diagnostics);
        if (!object.tls_model.empty() && !object.is_thread_local) {
            diagnostics.error(
                declaration->location,
                "tls_model requires thread_local on the same object");
        }
        if (!object.tls_model.empty() && object.tls_model != "local-exec" &&
            object.tls_model != "initial-exec" &&
            object.tls_model != "local-dynamic" &&
            object.tls_model != "global-dynamic") {
            diagnostics.error(declaration->location,
                              "tls_model must be local-exec, initial-exec, "
                              "local-dynamic, or global-dynamic");
        }
        const bool noinit =
            marker_attribute(*declaration, "noinit", diagnostics);
        const auto recursively_const = [&](auto&& self,
                                           hir::TypeId id) -> bool {
            const auto& type = hir_module.type(id);
            return type.is_const || (type.kind == hir::Type::Kind::Array &&
                                     type.element && self(self, *type.element));
        };
        object.read_only = !object.is_thread_local &&
                           recursively_const(recursively_const, entity.type) &&
                           !noinit;
        if (!entity.definition) {
            object.initializer = InitializerKind::Declaration;
            if (noinit) {
                diagnostics.error(declaration->location,
                                  "noinit requires an object definition");
            }
            if (object.used && !entity.alias_target) {
                diagnostics.error(declaration->location,
                                  "used requires an object definition");
            }
            if (object.retain && !entity.alias_target) {
                diagnostics.error(declaration->location,
                                  "retain requires an object definition");
            }
        } else if (noinit) {
            object.initializer = InitializerKind::Uninitialized;
            if (hir_module.type(entity.type).is_const) {
                diagnostics.error(declaration->location,
                                  "noinit object cannot be const");
            }
            if (declaration->initializer) {
                diagnostics.error(declaration->location,
                                  "noinit object cannot have an initializer");
            }
        } else if (!declaration->initializer) {
            object.initializer = InitializerKind::Zero;
        } else {
            (void)lower_initializer(object, hir_module, entity, *declaration,
                                    subtarget, diagnostics);
        }
        result.object_indices.emplace(entity.id.value, result.objects.size());
        if (object.address &&
            (object.address->kind == AddressKind::Function ||
             object.address->kind == AddressKind::Label) &&
            object.address->function) {
            (void)hir::stabilize_function_address(
                hir_module, *object.address->function, declaration->location,
                diagnostics);
        }
        for (const auto& relocation : object.relocations) {
            if ((relocation.address.kind == AddressKind::Function ||
                 relocation.address.kind == AddressKind::Label) &&
                relocation.address.function) {
                (void)hir::stabilize_function_address(
                    hir_module, *relocation.address.function,
                    declaration->location, diagnostics);
            }
        }
        result.objects.push_back(std::move(object));
    }
    for (const auto& entity : hir_module.objects) {
        if (!entity.alias_target) continue;
        const auto alias_index = result.object_indices.find(entity.id.value);
        const auto target = std::find_if(
            hir_module.objects.begin(), hir_module.objects.end(),
            [&](const hir::Object& candidate) {
                return candidate.link_symbol == *entity.alias_target;
            });
        if (alias_index == result.object_indices.end() ||
            target == hir_module.objects.end()) {
            continue;
        }
        const auto target_index = result.object_indices.find(target->id.value);
        if (target_index == result.object_indices.end()) continue;
        auto& alias = result.objects[alias_index->second];
        auto& storage = result.objects[target_index->second];
        storage.used = storage.used || alias.used;
        storage.retain = storage.retain || alias.retain;
    }
    return result;
}

} // namespace cross::data
