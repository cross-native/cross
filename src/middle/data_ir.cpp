// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/data_ir.hpp"

#include "common/diagnostic.hpp"
#include "common/floating_bits.hpp"
#include "common/floating_semantics.hpp"
#include "common/integer_semantics.hpp"
#include "common/relocation_addend.hpp"
#include "frontend/ast.hpp"
#include "frontend/semantic.hpp"
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

std::pair<const hir::Object*, const hir::Function*> find_value_name(
    const hir::Module& module, NameUse name, AddressScope scope) {
    const auto separator = scope.source_name.rfind("::");
    const auto prefix = separator == std::string::npos ? std::string_view{}
        : scope.source_name.substr(0, separator);
    for (const auto& candidate : namespace_candidates(name, prefix)) {
        const auto* object = find_object(module, candidate, scope.source_unit);
        const auto* function = find_function(module, candidate, scope.source_unit);
        if (object || function) return {object, function};
    }
    return {};
}

const hir::Object* find_object(const hir::Module& module,
                               NameUse name,
                               AddressScope scope) {
    return find_value_name(module, name, scope).first;
}

const hir::Function* find_function(const hir::Module& module,
                                   NameUse name,
                                   AddressScope scope) {
    return find_value_name(module, name, scope).second;
}

struct AddressValue {
    AddressConstant address;
    std::optional<hir::TypeId> pointee;
    TypePtr cast_pointee;
    bool integer{};
    bool owner_const{};
    bool owner_volatile{};
};

std::uint64_t source_storage_size(const hir::Module& module,
                                  const TypePtr& type,
                                  const Subtarget& subtarget) {
    if (!type) return 0;
    if (type->kind == Type::Kind::Pointer)
        return (module.address_bits + 7U) / 8U;
    if (type->kind == Type::Kind::Record) {
        const auto* record = module.record(type->nominal_key());
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
    const auto sum = offset_relocation_addend(relocation_addend(address.addend),
                                              {UInt128{bytes}, false}, 1, true);
    const auto value = sum ? relocation_addend_i64(*sum) : std::nullopt;
    if (!value) return false;
    address.addend = *value;
    return true;
}

std::optional<RelocationAddend> address_offset(const Expr& expression, unsigned address_bits) {
    // Required proofs already include unary promotion/wrapping. Inspect those
    // typed bits before considering the legacy unprepared literal syntax.
    if (expression.evaluated_integer) {
        const auto& value = *expression.evaluated_integer;
        const auto type = value.type;
        const auto width = type == BuiltinType::Iptr || type == BuiltinType::Uptr
            ? address_bits : type_bits(builtin_type(type));
        const bool signed_type = type == BuiltinType::I8 || type == BuiltinType::I16 ||
            type == BuiltinType::I32 || type == BuiltinType::I64 ||
            type == BuiltinType::I128 || type == BuiltinType::Iptr;
        return relocation_addend(value.value, {width, signed_type});
    }
    if (expression.kind == Expr::Kind::Parenthesized && expression.left)
        return address_offset(*expression.left, address_bits);
    if (expression.kind == Expr::Kind::Unary && expression.left &&
        (expression.text == "+" || expression.text == "-")) {
        auto value = address_offset(*expression.left, address_bits);
        if (value && expression.text == "-" && value->magnitude != UInt128{})
            value->negative = !value->negative;
        return value;
    }
    const auto value = integer_value(expression);
    return value ? std::optional{RelocationAddend{*value, false}} : std::nullopt;
}

bool apply_address_offset(AddressConstant& address, const Expr& expression,
                          std::uint64_t scale, bool addition, unsigned address_bits) {
    const auto offset = address_offset(expression, address_bits);
    const auto sum = offset ? offset_relocation_addend(relocation_addend(address.addend), *offset,
                                                       scale, addition) : std::nullopt;
    const auto value = sum ? relocation_addend_i64(*sum) : std::nullopt;
    if (!value) return false;
    address.addend = *value;
    return true;
}

bool compatible_static_pointee(const hir::Module& module, hir::TypeId source,
                               const TypePtr& destination,
                               unsigned depth = 0, bool nested_qualification = true) {
    auto to = destination;
    bool immediate = depth == 0;
    while (to) {
        const auto& from = module.type(source);
        if ((from.is_const && !to->is_const) ||
            (from.is_volatile && !to->is_volatile) ||
            from.is_atomic != to->is_atomic) return false;
        if (!nested_qualification &&
            ((!from.is_const && to->is_const) ||
             (!from.is_volatile && to->is_volatile))) return false;
        if (immediate &&
            ((to->kind == Type::Kind::Builtin && to->builtin == BuiltinType::Void) ||
             (from.kind == hir::Type::Kind::Builtin && from.builtin == BuiltinType::Void)))
            return from.kind != hir::Type::Kind::Function && to->kind != Type::Kind::Function;
        immediate = false;
        switch (to->kind) {
        case Type::Kind::Builtin:
            return from.kind == hir::Type::Kind::Builtin && from.builtin == to->builtin &&
                   from.nominal_key() == to->nominal_key();
        case Type::Kind::Pointer:
            if (from.kind != hir::Type::Kind::Pointer || !from.pointee ||
                from.address_space != to->address_space) return false;
            nested_qualification = nested_qualification && to->is_const;
            source = *from.pointee;
            to = to->pointee;
            break;
        case Type::Kind::Array:
        case Type::Kind::Vector:
            if ((to->kind == Type::Kind::Array ? from.kind != hir::Type::Kind::Array
                                              : from.kind != hir::Type::Kind::Vector) ||
                !from.element || from.lanes != to->lanes || from.scalable != to->scalable) return false;
            source = *from.element;
            to = to->element;
            break;
        case Type::Kind::Record:
            return from.kind == hir::Type::Kind::Record && from.nominal_key() == to->nominal_key();
        default:
            return false;
        }
    }
    return false;
}

ContinuationTask<std::optional<AddressValue>> address_value_async(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget);

ContinuationTask<std::optional<AddressValue>> address_designator_async(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left)
        co_return co_await address_designator_async(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Unary && expression.text == "*" && expression.left)
        co_return co_await address_value_async(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Name) {
        if (const auto* object =
                find_object(module, expression, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Object;
            result.address.object = object->id;
            result.pointee = object->type;
            co_return result;
        }
        if (const auto* function =
                find_function(module, expression, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Function;
            result.address.function = function->id;
            co_return result;
        }
        co_return std::nullopt;
    }
    if (expression.kind != Expr::Kind::Binary || !expression.left ||
        !expression.right) co_return std::nullopt;
    if ((expression.text == "member" || expression.text == "pointer_member") &&
        expression.right->kind == Expr::Kind::Name) {
        std::optional<AddressValue> base;
        if (expression.text == "member")
            base = co_await address_designator_async(module, scope, *expression.left, subtarget);
        else base = co_await address_value_async(module, scope, *expression.left, subtarget);
        if (!base) co_return std::nullopt;
        const hir::Record* record{};
        if (base->pointee) {
            const auto& type = module.type(*base->pointee);
            base->owner_const = base->owner_const || type.is_const;
            base->owner_volatile = base->owner_volatile || type.is_volatile;
            if (type.kind == hir::Type::Kind::Record && type.record)
                record = &module.record(*type.record);
        } else if (base->cast_pointee &&
                   base->cast_pointee->kind == Type::Kind::Record) {
            base->owner_const = base->owner_const || base->cast_pointee->is_const;
            base->owner_volatile = base->owner_volatile || base->cast_pointee->is_volatile;
            record = module.record(base->cast_pointee->nominal_key());
        }
        if (!record) co_return std::nullopt;
        const auto* member = module.member(record->id,
                                            member_name(*expression.right));
        if (!member || member->bit_width ||
            !add_address_addend(base->address, member->offset))
            co_return std::nullopt;
        base->pointee = member->type;
        base->cast_pointee.reset();
        co_return base;
    }
    if (expression.text == "index") {
        auto base = co_await address_designator_async(module, scope, *expression.left,
                                       subtarget);
        if (base && base->pointee) {
            const auto& selected = module.type(*base->pointee);
            if ((selected.kind == hir::Type::Kind::Array ||
                 (selected.kind == hir::Type::Kind::Vector && !selected.scalable)) && selected.element) {
                base->owner_const = base->owner_const || selected.is_const;
                base->owner_volatile = base->owner_volatile || selected.is_volatile;
                base->pointee = *selected.element;
            } else {
                base.reset();
            }
        } else if (base && base->cast_pointee) {
            if (base->cast_pointee->kind == Type::Kind::Array ||
                (base->cast_pointee->kind == Type::Kind::Vector && !base->cast_pointee->scalable))
                base->cast_pointee = qualified_element_type(base->cast_pointee);
            else base.reset();
        }
        if (!base)
            base = co_await address_value_async(module, scope, *expression.left, subtarget);
        if (!base || (!base->pointee && !base->cast_pointee) ||
            base->integer) co_return std::nullopt;
        const auto size = base->cast_pointee
            ? source_storage_size(module, base->cast_pointee, subtarget)
            : type_size(module, *base->pointee, subtarget);
        if (!apply_address_offset(base->address, *expression.right, size,
                                   true, module.address_bits))
            co_return std::nullopt;
        co_return base;
    }
    co_return std::nullopt;
}

ContinuationTask<std::optional<AddressValue>> address_value_async(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget) {
    if (expression.name_context && expression.name_context->label_address) {
        const auto* label = module.label(*expression.name_context->label_address);
        if (!label) co_return std::nullopt;
        AddressValue result;
        result.address.kind = AddressKind::Label;
        result.address.function = label->owner;
        result.address.label = label->id;
        co_return result;
    }
    if (expression.kind == Expr::Kind::Address && expression.evaluated_address) {
        const auto& source = *expression.evaluated_address;
        AddressValue result;
        if (source.kind == cross::AddressConstant::Kind::Object && source.object) {
            const auto* object = module.object(*source.object);
            if (!object) co_return std::nullopt;
            result.address.kind = AddressKind::Object;
            result.address.object = object->id;
        } else if (source.kind == cross::AddressConstant::Kind::Function && source.function) {
            const auto* function = module.function(*source.function);
            if (!function) co_return std::nullopt;
            result.address.kind = AddressKind::Function;
            result.address.function = function->id;
        } else co_return std::nullopt;
        result.address.addend = source.addend;
        if (expression.type && expression.type->kind == Type::Kind::Pointer)
            result.cast_pointee = expression.type->pointee;
        co_return result;
    }
    if (expression.kind == Expr::Kind::Parenthesized && expression.left)
        co_return co_await address_value_async(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Unary && expression.text == "&" &&
        expression.left)
        co_return co_await address_designator_async(module, scope, *expression.left, subtarget);
    if (expression.kind == Expr::Kind::Name) {
        if (const auto* object =
                find_object(module, expression, scope)) {
            const auto& type = module.type(object->type);
            if (type.kind != hir::Type::Kind::Array || !type.element)
                co_return std::nullopt;
            AddressValue result;
            result.address.kind = AddressKind::Object;
            result.address.object = object->id;
            result.pointee = *type.element;
            result.owner_const = type.is_const;
            result.owner_volatile = type.is_volatile;
            co_return result;
        }
        if (const auto* function =
                find_function(module, expression, scope)) {
            AddressValue result;
            result.address.kind = AddressKind::Function;
            result.address.function = function->id;
            co_return result;
        }
        const auto split = expression.text.rfind("::");
        if (split == std::string::npos) co_return std::nullopt;
        NameUse owner_name(expression);
        owner_name.spelling = std::string_view(expression.text).substr(0, split);
        const auto* function = find_function(module, owner_name, scope);
        if (!function) co_return std::nullopt;
        const auto qualified = function->source_name + expression.text.substr(split);
        NameUse label_name(expression);
        label_name.spelling = qualified;
        const auto* label = module.label(function->id, label_name);
        if (!label) co_return std::nullopt;
        AddressValue result;
        result.address.kind = AddressKind::Label;
        result.address.function = function->id;
        result.address.label = label->id;
        co_return result;
    }
    if ((expression.kind == Expr::Kind::Binary &&
         (expression.text == "member" || expression.text == "pointer_member" || expression.text == "index")) ||
        (expression.kind == Expr::Kind::Unary && expression.text == "*")) {
        auto value = co_await address_designator_async(module, scope, expression,
                                         subtarget);
        if (!value) co_return std::nullopt;
        if (value->cast_pointee) {
            if (value->cast_pointee->kind != Type::Kind::Array) co_return std::nullopt;
            value->cast_pointee = qualified_element_type(value->cast_pointee);
            co_return value;
        }
        if (!value->pointee) co_return std::nullopt;
        const auto& selected = module.type(*value->pointee);
        if (selected.kind != hir::Type::Kind::Array || !selected.element)
            co_return std::nullopt;
        value->pointee = *selected.element;
        value->owner_const = value->owner_const || selected.is_const;
        value->owner_volatile = value->owner_volatile || selected.is_volatile;
        co_return value;
    }
    if (expression.kind == Expr::Kind::Cast && expression.left &&
        expression.type) {
        auto value = co_await address_value_async(module, scope, *expression.left,
                                   subtarget);
        if (!value) co_return std::nullopt;
        if (expression.type->kind == Type::Kind::Builtin) {
            const auto kind = expression.type->builtin;
            if ((kind != BuiltinType::Uptr && kind != BuiltinType::Iptr &&
                 kind != BuiltinType::U32 && kind != BuiltinType::I32 &&
                 kind != BuiltinType::U64 && kind != BuiltinType::I64) ||
                (kind == BuiltinType::Uptr || kind == BuiltinType::Iptr
                    ? module.address_bits
                    : type_bits(expression.type)) != module.address_bits) {
                co_return std::nullopt;
            }
            value->integer = true;
            value->pointee.reset();
            value->cast_pointee.reset();
            co_return value;
        }
        if (expression.type->kind == Type::Kind::Pointer) {
            if (!expression.type->pointee ||
                (value->owner_const && !expression.type->pointee->is_const) ||
                (value->owner_volatile && !expression.type->pointee->is_volatile) ||
                (value->pointee &&
                 !compatible_static_pointee(
                     module, *value->pointee,
                     expression.type->pointee)) ||
                (value->cast_pointee &&
                 !compatible_pointee(value->cast_pointee,
                                      expression.type->pointee))) {
                co_return std::nullopt;
            }
            value->integer = false;
            value->pointee.reset();
            value->cast_pointee = expression.type->pointee;
            value->owner_const = false;
            value->owner_volatile = false;
            co_return value;
        }
        co_return std::nullopt;
    }
    if (expression.kind == Expr::Kind::Binary && expression.left &&
        expression.right &&
        (expression.text == "+" || expression.text == "-")) {
        auto value = co_await address_value_async(module, scope, *expression.left,
                                   subtarget);
        const Expr* offset_expression = expression.right.get();
        if (!value && expression.text == "+") {
            value = co_await address_value_async(module, scope, *expression.right,
                                  subtarget);
            offset_expression = expression.left.get();
        }
        if (!value) co_return std::nullopt;
        const auto scale = value->integer ? std::uint64_t{1}
            : value->cast_pointee
                ? source_storage_size(module, value->cast_pointee, subtarget)
            : value->pointee
                ? type_size(module, *value->pointee, subtarget)
                : std::uint64_t{0};
        if (!apply_address_offset(value->address, *offset_expression, scale,
                                   expression.text == "+", module.address_bits))
            co_return std::nullopt;
        co_return value;
    }
    co_return std::nullopt;
}

std::optional<AddressValue> address_value(
    const hir::Module& module, AddressScope scope,
    const Expr& expression, const Subtarget& subtarget) {
    return address_value_async(module, scope, expression, subtarget).run();
}

bool lower_scalar_initializer(Object& result, const hir::Module& module,
                              const hir::Object& entity,
                              const Expr& expression,
                              const Subtarget& subtarget,
                              Diagnostics& diagnostics) {
    const auto& type = module.type(entity.type);
    if (expression.kind == Expr::Kind::Address && expression.evaluated_address &&
        expression.evaluated_address->kind == cross::AddressConstant::Kind::Absolute) {
        result.initializer = InitializerKind::Integer;
        result.bits = expression.evaluated_address->absolute;
        return true;
    }
    if (type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Label &&
        expression.evaluated_integer && expression.evaluated_integer->type == BuiltinType::Label) {
        if (subtarget.target().data_layout.code_addresses != CodeAddressRepresentation::Flat) {
            diagnostics.error(expression.location, "target does not provide numeric code-address representation");
            return false;
        }
        result.initializer = InitializerKind::Integer;
        result.bits = mask_to(expression.evaluated_integer->value, module.address_bits);
        return true;
    }
    if (type.kind == hir::Type::Kind::Pointer ||
        (type.kind == hir::Type::Kind::Builtin &&
         type.builtin == BuiltinType::Label)) {
        if (type.kind == hir::Type::Kind::Pointer) {
            const Expr* source = &expression;
            while (source->left && (source->kind == Expr::Kind::Parenthesized ||
                (source->kind == Expr::Kind::Unary && (source->text == "+" || source->text == "-"))))
                source = source->left.get();
            // Numeric code-label bits are not an integer null-pointer constant.
            const auto integer = source->evaluated_integer &&
                source->evaluated_integer->type == BuiltinType::Label
                ? std::nullopt : integer_value(expression);
            if (integer && *integer == UInt128{}) {
                const auto* space = find_address_space(subtarget.target(), type.address_space);
                if (!space || !space->native_lowering) {
                    diagnostics.error(expression.location,
                        "pointer initializer has no native address-space representation");
                    return false;
                }
                const auto bits = space->pointer_bits ? space->pointer_bits : module.address_bits;
                const UInt128 null{space->null_low, space->null_high};
                if (bits != result.size * 8U || !fits_unsigned(null, bits)) {
                    diagnostics.error(expression.location,
                        "null pointer representation does not fit the destination storage");
                    return false;
                }
                result.initializer = InitializerKind::Integer;
                result.bits = null;
                return true;
            }
        }
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
    const Expr* source = &expression;
    while (source->kind == Expr::Kind::Parenthesized && source->left)
        source = source->left.get();
    if ((!text && !source->evaluated_floating) ||
        type.kind != hir::Type::Kind::Builtin) {
        diagnostics.error(
            expression.location,
            "global initializer is not a supported scalar constant");
        return false;
    }
    result.initializer = InitializerKind::Floating;
    const auto format = type.builtin == BuiltinType::F32 ||
        (type.builtin == BuiltinType::Fptr &&
         subtarget.abi_info().address_bits == 32)
        ? floating::Format::Binary32
        : type.builtin == BuiltinType::F64 || type.builtin == BuiltinType::Fptr
        ? floating::Format::Binary64
        : type.builtin == BuiltinType::F80
        ? floating::Format::Extended80 : floating::Format::Binary128;
    if (type.builtin != BuiltinType::F32 && type.builtin != BuiltinType::F64 &&
        type.builtin != BuiltinType::F80 && type.builtin != BuiltinType::F128 &&
        type.builtin != BuiltinType::Fptr) {
        diagnostics.error(expression.location,
                          "global initializer has incompatible floating type");
        return false;
    }
    if (source->evaluated_floating) {
        result.bits = source->evaluated_floating->bits;
        return true;
    }
    const auto parsed = floating::parse(*text, format);
    if (!parsed) {
        diagnostics.error(expression.location, "invalid floating literal");
        return false;
    }
    result.bits = parsed->bits;
    return true;
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

bool append_image_relocations(Object& result, const Expr& expression,
                              unsigned offset, const hir::Module& module,
                              const hir::Object& entity, const Subtarget& subtarget,
                              Diagnostics& diagnostics) {
    for (const auto& relocation : expression.object_relocations) {
        if (relocation.offset > expression.string_value.size() ||
            relocation.length > expression.string_value.size() - relocation.offset ||
            relocation.offset + offset > result.size ||
            relocation.length > result.size - relocation.offset - offset) {
            diagnostics.error(expression.location, "evaluated object relocation exceeds target storage");
            return false;
        }
        Expr source;
        source.kind = Expr::Kind::Address;
        source.location = expression.location;
        source.type = relocation.type;
        if (const auto* address = std::get_if<cross::AddressConstant>(&relocation.address)) {
            source.evaluated_address = *address;
        } else {
            source.kind = Expr::Kind::Name;
            auto context = std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Exact;
            context->label_address = std::make_shared<const LabelAddressConstant>(
                std::get<LabelAddressConstant>(relocation.address));
            source.name_context = std::move(context);
        }
        const auto address = relocatable_address(module,
            {entity.source_name, entity.source_unit}, source, subtarget, false);
        if (!address || (address->kind == AddressKind::Object && address->object &&
                         module.object(*address->object).is_thread_local)) {
            diagnostics.error(expression.location, "evaluated object pointer is not a static relocation");
            return false;
        }
        result.relocations.push_back({offset + static_cast<unsigned>(relocation.offset),
                                      static_cast<unsigned>(relocation.length), *address});
    }
    return true;
}

bool lower_initializer(Object& result, const hir::Module& module,
                       const hir::Object& entity,
                       const ObjectDecl& declaration,
                       const Subtarget& subtarget,
                       Diagnostics& diagnostics) {
    if (!declaration.initializer) return true;
    const auto& expression = *declaration.initializer;
    const auto& type = module.type(entity.type);
    if (expression.kind == Expr::Kind::ByteSequence && expression.type) {
        if (expression.string_value.size() != result.size) {
            diagnostics.error(expression.location, "evaluated object representation does not match target storage");
            return false;
        }
        result.initializer = InitializerKind::Aggregate;
        result.bytes.assign(expression.string_value.begin(), expression.string_value.end());
        return append_image_relocations(result, expression, 0, module, entity, subtarget, diagnostics);
    }
    if (type.kind == hir::Type::Kind::Array && type.element &&
        module.type(*type.element).kind == hir::Type::Kind::Builtin &&
        module.type(*type.element).builtin == BuiltinType::U8 &&
        expression.kind == Expr::Kind::ByteSequence) {
        if (expression.string_value.size() != result.size) {
            diagnostics.error(expression.location,
                              "materialized byte count does not match the u8 array bound");
            return false;
        }
        result.initializer = InitializerKind::Bytes;
        result.bytes.assign(expression.string_value.begin(),
                            expression.string_value.end());
        return true;
    }
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
        if (item.expression->kind == Expr::Kind::ByteSequence && item.expression->type) {
            if (item.expression->string_value.size() != *item_size) {
                diagnostics.error(item.expression->location,
                    "evaluated object representation does not match target storage");
                valid = false;
                continue;
            }
            std::copy(item.expression->string_value.begin(), item.expression->string_value.end(),
                      result.bytes.begin() + static_cast<std::ptrdiff_t>(item.offset));
            valid &= append_image_relocations(result, *item.expression,
                static_cast<unsigned>(item.offset), module, entity, subtarget, diagnostics);
            continue;
        }
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

bool normalize_generic_pointer(Program& program, std::unique_ptr<Expr>& expression,
                               const TypePtr& destination,
                               const FunctionDecl* caller,
                               std::span<const NameKey> locals,
                               const CompilerOptions& options,
                               const Subtarget& subtarget,
                               Diagnostics& diagnostics) {
    return normalize_generic_pointer_async(program, expression, destination, caller, locals,
        options, subtarget, diagnostics).run();
}

ContinuationTask<bool> normalize_generic_pointer_async(Program& program, std::unique_ptr<Expr>& expression,
                               const TypePtr& destination,
                               const FunctionDecl* caller,
                               std::span<const NameKey> locals,
                               const CompilerOptions& options,
                               const Subtarget& subtarget,
                               Diagnostics& diagnostics) {
    const auto initial_errors = diagnostics.errors();
    const auto initial_resources = program.evaluation_resource_errors;
    if (!expression || !destination || destination->kind != Type::Kind::Pointer)
        co_return false;
    auto module = co_await hir::build_constant_context_async(program, options, subtarget.target(),
                                               diagnostics);
    if (diagnostics.errors() != initial_errors || program.evaluation_resource_errors != initial_resources) co_return false;
    const auto* space = find_address_space(subtarget.target(), destination->address_space);
    if (!space || !space->native_lowering) {
        diagnostics.error(expression->location, "generic pointer type has no native address-space representation");
        co_return false;
    }
    const auto address_bits = space->pointer_bits ? space->pointer_bits : module.address_bits;
    const AddressScope scope{caller ? caller->name : std::string_view{},
                             caller ? caller->source_unit : std::string_view{}};
    const auto separator = scope.source_name.rfind("::");
    const auto name_space = separator == std::string_view::npos
        ? std::string_view{} : scope.source_name.substr(0, separator);
    const LayoutQuery size_of = [&](const TypePtr& type) {
        return hir::layout_size(module, module.intern_type(type), subtarget.target());
    };
    const LayoutQuery align_of = [&](const TypePtr& type) {
        return hir::layout_alignment(module, module.intern_type(type), subtarget.target());
    };
    const auto reject = [&](SourceLocation location, std::string message) {
        diagnostics.error(location, std::move(message));
        return false;
    };
    // Bind in the caller's source context before substitution moves the value
    // to the generic definition. Runtime cells can never become relocations.
    const auto bind = [&](Expr& root) -> bool {
        std::vector<Expr*> pending{&root};
        while (!pending.empty()) {
            auto& node = *pending.back();
            pending.pop_back();
            if (node.kind == Expr::Kind::Name) {
                if (std::find(locals.begin(), locals.end(), name_key(node)) != locals.end())
                    return reject(node.location,
                        "generic pointer argument cannot depend on an automatic local or parameter");
                const auto candidates = namespace_candidates(node,
                    caller ? caller->source_namespace : std::string{},
                    caller ? caller->imports : std::vector<std::string>{});
                const hir::Object* object{};
                const hir::Function* function{};
                for (const auto& candidate : candidates) {
                    object = find_object(module, candidate, scope.source_unit);
                    function = find_function(module, candidate, scope.source_unit);
                    if (object || function) break;
                }
                if (object) bind_exact_name(node, object->source_name);
                else if (function) bind_exact_name(node, function->source_name);
                else if (node.text == "$::runtime")
                    return reject(node.location, "runtime expression is not permitted in a generic pointer argument");
                else if (node.text != "$::eval")
                    return reject(node.location, "unresolved name in generic pointer argument: '" + node.text + "'");
            }
            const bool member = node.kind == Expr::Kind::Binary &&
                (node.text == "member" || node.text == "pointer_member");
            for (auto argument = node.arguments.rbegin(); argument != node.arguments.rend(); ++argument)
                pending.push_back(argument->get());
            if (node.third) pending.push_back(node.third.get());
            if (node.right && !member) pending.push_back(node.right.get());
            if (node.left) pending.push_back(node.left.get());
        }
        return true;
    };
    if (!expression || !bind(*expression)) co_return false;
    const auto integer = [&](const Expr& node) {
        return evaluate_target_integer_constant_async(program, node, diagnostics,
                                                size_of, align_of, name_space);
    };
    const auto fold_integer = [&](std::unique_ptr<Expr>& node) -> ContinuationTask<bool> {
        const auto value = co_await integer(*node);
        if (!value) co_return false;
        auto folded = std::make_unique<Expr>();
        folded->location = node->location;
        folded->kind = Expr::Kind::Integer;
        folded->evaluated_integer = *value;
        folded->text = to_decimal(value->value);
        node = std::move(folded);
        co_return true;
    };
    const auto has_address = [&](const Expr& root) -> bool {
        std::vector<const Expr*> pending{&root};
        while (!pending.empty()) {
            const auto& node = *pending.back();
            pending.pop_back();
            if (node.kind == Expr::Kind::Address) {
                if (node.evaluated_address &&
                    node.evaluated_address->kind != cross::AddressConstant::Kind::Absolute) return true;
                continue;
            }
            if (node.kind == Expr::Kind::Unary && node.text == "&") return true;
            if (node.kind == Expr::Kind::Name) {
                const auto* object = find_object(module, node, scope);
                if ((object && module.type(object->type).kind == hir::Type::Kind::Array) ||
                       find_function(module, node, scope)) return true;
                continue;
            }
            if (node.kind == Expr::Kind::Call || node.kind == Expr::Kind::Sizeof ||
                node.kind == Expr::Kind::Alignof) continue;
            if (node.third) pending.push_back(node.third.get());
            if (node.right) pending.push_back(node.right.get());
            if (node.left) pending.push_back(node.left.get());
        }
        return false;
    };
    const GenericPointerResolver resolve_pointer =
        [&](std::unique_ptr<Expr>& node, const TypePtr& type,
            const FunctionDecl* context, std::span<const NameKey> local_names) {
            return normalize_generic_pointer_async(program, node, type, context, local_names,
                                              options, subtarget, diagnostics);
        };
    const auto pointer_type_of = [&](const Expr& root) -> TypePtr {
        std::vector<const Expr*> pending{&root};
        while (!pending.empty()) {
            const auto& node = *pending.back();
            pending.pop_back();
            if (node.type && node.type->kind == Type::Kind::Pointer) return node.type;
            if (node.kind == Expr::Kind::Parenthesized && node.left) pending.push_back(node.left.get());
            if (node.kind == Expr::Kind::Call && node.left && node.left->kind == Expr::Kind::Name) {
                if (node.left->text == "$::eval" && node.arguments.size() == 1)
                    pending.push_back(node.arguments.front().get());
                if (const auto* function = find_function(module, *node.left, scope)) {
                    const auto* source = function->definition ? function->definition : function->declarations.back();
                    if (source->return_type->kind == Type::Kind::Pointer) return source->return_type;
                }
            }
            if (node.kind == Expr::Kind::Binary && (node.text == "+" || node.text == "-")) {
                if (node.text == "+" && node.right) pending.push_back(node.right.get());
                if (node.left) pending.push_back(node.left.get());
            }
        }
        return {};
    };
    const auto fold = [&](const auto& self, std::unique_ptr<Expr>& node) -> ContinuationTask<bool> {
        if (node->kind == Expr::Kind::Unary && node->text == "&" && node->left) {
            auto* designator = node->left.get();
            while (designator->kind == Expr::Kind::Parenthesized && designator->left)
                designator = designator->left.get();
            if (designator->kind == Expr::Kind::Unary && designator->text == "*" && designator->left) {
                auto pointer = std::move(designator->left);
                node = std::move(pointer);
                co_return (co_await self(self, node));
            }
        }
        if (node->kind == Expr::Kind::Call && node->left &&
            node->left->kind == Expr::Kind::Name && node->left->text == "$::eval") {
            if (node->arguments.size() != 1)
                co_return reject(node->location, "$::eval requires exactly one expression");
            auto operand = std::move(node->arguments.front());
            node = std::move(operand);
            co_return (co_await self(self, node));
        }
        if (node->kind == Expr::Kind::Call && pointer_type_of(*node)) {
            auto value = co_await evaluate_target_pointer_constant_async(program, *node,
                pointer_type_of(*node), caller, diagnostics,
                size_of, align_of, resolve_pointer);
            if (!value) co_return false;
            node = std::move(value);
            co_return true;
        }
        if (node->kind == Expr::Kind::Conditional && node->left &&
            node->right && node->third) {
            const auto condition = co_await integer(*node->left);
            if (!condition) co_return false;
            auto selected = condition->value == UInt128{}
                ? std::move(node->third) : std::move(node->right);
            node = std::move(selected);
            co_return (co_await self(self, node));
        }
        if (node->kind == Expr::Kind::Binary && node->left && node->right) {
            if (node->text == "index") {
                if (!(co_await self(self, node->left))) co_return false;
                co_return co_await fold_integer(node->right);
            }
            if (node->text == "+" || node->text == "-") {
                // Fold pointer-producing calls before deciding which operand
                // supplies the address and which supplies the integer offset.
                if (!(co_await self(self, node->left)) || !(co_await self(self, node->right))) co_return false;
                const bool left_address = has_address(*node->left);
                const bool right_address = has_address(*node->right);
                if (left_address && !right_address)
                    co_return (co_await fold_integer(node->right));
                if (!left_address && right_address && node->text == "+")
                    co_return (co_await fold_integer(node->left));
                const auto left_type = pointer_type_of(*node->left);
                const auto right_type = pointer_type_of(*node->right);
                if ((left_type && !right_type) || (!left_type && right_type && node->text == "+")) {
                    auto& base = left_type ? node->left : node->right;
                    auto& offset = left_type ? node->right : node->left;
                    const auto type = left_type ? left_type : right_type;
                    if (!(co_await resolve_pointer.async(base, type, caller, locals)) || !(co_await fold_integer(offset))) co_return false;
                    const auto scale = co_await size_of.async(type->pointee);
                    AddressConstant delta;
                    if (!scale || !apply_address_offset(delta, *offset, *scale,
                                                        node->text == "+", address_bits))
                        co_return reject(node->location, "generic pointer arithmetic requires a complete object type and representable offset");
                    auto value = *base->evaluated_address;
                    const bool negative = delta.addend < 0;
                    const auto magnitude = negative ? std::uint64_t{0} - static_cast<std::uint64_t>(delta.addend)
                                                    : static_cast<std::uint64_t>(delta.addend);
                    const UInt128 amount{magnitude};
                    const auto sum = negative ? subtract(value.absolute, amount) : add(value.absolute, amount);
                    if ((negative && value.absolute < amount) ||
                        (!negative && sum < value.absolute) || !fits_unsigned(sum, address_bits))
                        co_return reject(node->location, "generic pointer arithmetic overflows the selected target width");
                    value.absolute = sum;
                    auto result = std::make_unique<Expr>();
                    result->kind = Expr::Kind::Address;
                    result->location = node->location;
                    result->type = type;
                    result->evaluated_address = value;
                    node = std::move(result);
                    co_return true;
                }
                co_return true;
            }
        }
        if (node->left && !(co_await self(self, node->left))) co_return false;
        co_return true;
    };
    if (!(co_await fold(fold, expression)) || program.evaluation_resource_errors != initial_resources) co_return false;

    const auto check_conversion = [&](const TypePtr& from, const TypePtr& to,
                                      SourceLocation location) {
        if (!from || from->kind != Type::Kind::Pointer || !to ||
            to->kind != Type::Kind::Pointer) return true;
        if (from->address_space != to->address_space)
            return reject(location, "generic pointer conversion changes address space");
        const bool from_function = from->pointee->kind == Type::Kind::Function;
        const bool to_function = to->pointee->kind == Type::Kind::Function;
        if (from_function || to_function) {
            if (!from_function || !to_function ||
                module.intern_type(from) != module.intern_type(to))
                return reject(location, "generic pointer argument has an incompatible function type");
        } else if (!compatible_static_pointee(module, module.intern_type(from->pointee),
                                               to->pointee)) {
            return reject(location,
                          "generic pointer argument has an incompatible pointed-to type or qualifiers");
        }
        return true;
    };
    const auto unparen = [](const Expr* node) {
        while (node->kind == Expr::Kind::Parenthesized && node->left) node = node->left.get();
        return node;
    };
    const auto check_casts = [&](const Expr& root) -> bool {
        std::vector<const Expr*> pending{&root};
        while (!pending.empty()) {
            const auto& node = *pending.back();
            pending.pop_back();
            if (node.kind == Expr::Kind::Cast && node.left &&
                !check_conversion(unparen(node.left.get())->type, node.type, node.location))
                return false;
            if (node.third) pending.push_back(node.third.get());
            if (node.right) pending.push_back(node.right.get());
            if (node.left) pending.push_back(node.left.get());
        }
        return true;
    };
    if (!check_casts(*expression) ||
        !check_conversion(unparen(expression.get())->type, destination, expression->location))
        co_return false;

    cross::AddressConstant normalized;
    if (has_address(*expression)) {
        const auto value = co_await address_value_async(module, scope, *expression, subtarget);
        if (!value || value->integer || value->address.kind == AddressKind::Label)
            co_return reject(expression->location, "generic pointer argument is not a supported address constant");
        const bool function_pointer = destination->pointee &&
            destination->pointee->kind == Type::Kind::Function;
        if (value->address.kind == AddressKind::Function) {
            const auto& function = module.function(*value->address.function);
            const auto expected = module.type(module.intern_type(destination));
            auto actual = hir::call_signature(module, function.id, {});
            if (!function_pointer || !actual || !expected.pointee ||
                !module.type(*expected.pointee).function)
                co_return reject(expression->location, "generic pointer argument has an incompatible function type");
            auto signature = *module.type(*expected.pointee).function;
            // Contextual adapters run after instantiation and supply the
            // destination's ABI, endpoints, result location, clobbers, and cleanup.
            actual->abi = signature.abi;
            actual->result_location = signature.result_location;
            actual->clobbers = signature.clobbers;
            actual->stack_cleanup = signature.stack_cleanup;
            for (std::size_t index = 0; index < actual->parameters.size() &&
                 index < signature.parameters.size(); ++index)
                actual->parameters[index].physical_location =
                    signature.parameters[index].physical_location;
            if (*actual != signature || value->address.addend != 0)
                co_return reject(expression->location, "generic pointer argument has an incompatible function signature");
            normalized.kind = cross::AddressConstant::Kind::Function;
            normalized.function = function.definition ? function.definition
                                                      : function.declarations.back();
            if (normalized.function->attribute("eval_only") ||
                normalized.function->has_meta_signature() ||
                normalized.function->attribute("always_inline"))
                co_return reject(expression->location,
                              "generic pointer argument requires a function with a runtime address");
        } else {
            const auto& object = module.object(*value->address.object);
            if (object.is_thread_local)
                co_return reject(expression->location, "a thread-local address is not a generic pointer constant");
            const auto pointee = value->cast_pointee
                ? std::optional<hir::TypeId>(module.intern_type(value->cast_pointee))
                : value->pointee;
            if (function_pointer || !pointee ||
                (value->owner_const && !destination->pointee->is_const) ||
                (value->owner_volatile && !destination->pointee->is_volatile) ||
                !compatible_static_pointee(module, *pointee, destination->pointee))
                co_return reject(expression->location, "generic pointer argument has an incompatible pointed-to type or qualifiers");
            normalized.kind = cross::AddressConstant::Kind::Object;
            normalized.object = object.definition ? object.definition
                                                  : object.declarations.back();
            const auto extent = hir::layout_size(module, object.type, subtarget.target());
            if (value->address.addend < 0 ||
                (extent && static_cast<std::uint64_t>(value->address.addend) > *extent))
                co_return reject(expression->location,
                              "generic pointer address is outside its object or one-past bound");
        }
        normalized.addend = value->address.addend;
    } else {
        const Expr* source = expression.get();
        bool explicitly_pointer = false;
        while (source->left &&
               (source->kind == Expr::Kind::Parenthesized ||
                (source->kind == Expr::Kind::Cast && source->type &&
                 source->type->kind == Type::Kind::Pointer))) {
            if (source->kind == Expr::Kind::Cast) {
                explicitly_pointer = true;
                if (source->type->address_space != destination->address_space)
                    co_return reject(source->location, "generic pointer conversion changes address space");
            }
            source = source->left.get();
        }
        const bool normalized_absolute = source->kind == Expr::Kind::Address &&
            source->evaluated_address &&
            source->evaluated_address->kind == cross::AddressConstant::Kind::Absolute;
        if (normalized_absolute) explicitly_pointer = true;
        std::optional<Expr::IntegerConstant> value;
        if (normalized_absolute)
            value = Expr::IntegerConstant{source->evaluated_address->absolute, BuiltinType::Uptr};
        else value = co_await integer(*source);
        if (!value) co_return false;
        if (!explicitly_pointer && value->value != UInt128{})
            co_return reject(source->location, "a nonzero integer generic pointer argument requires an explicit pointer cast");
        auto width = type_bits(builtin_type(value->type));
        if (value->type == BuiltinType::Uptr || value->type == BuiltinType::Iptr)
            width = program.address_bits;
        const bool is_signed = value->type == BuiltinType::I8 || value->type == BuiltinType::I16 ||
            value->type == BuiltinType::I32 || value->type == BuiltinType::I64 ||
            value->type == BuiltinType::I128 || value->type == BuiltinType::Iptr;
        const IntegerType from{width, is_signed};
        const IntegerType to{address_bits, false};
        normalized.absolute = convert_integer(value->value, from, to);
        if (width > address_bits &&
            convert_integer(normalized.absolute, to, from) != value->value)
            co_return reject(source->location, "generic pointer address is not representable in the selected target width");
        if (value->value == UInt128{} && !normalized_absolute)
            normalized.absolute = UInt128{space->null_low, space->null_high};
    }
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Address;
    result->location = expression->location;
    result->type = destination;
    result->evaluated_address = normalized;
    expression = std::move(result);
    co_return true;
}

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
        const auto stabilize = [&](const AddressConstant& address) {
            if (address.kind == AddressKind::Label && address.label)
                (void)hir::stabilize_label_address(hir_module, *address.label,
                                                  declaration->location, diagnostics);
            else if (address.kind == AddressKind::Function && address.function)
                hir::stabilize_function_address(hir_module, *address.function);
        };
        if (object.address) stabilize(*object.address);
        for (const auto& relocation : object.relocations) {
            stabilize(relocation.address);
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
