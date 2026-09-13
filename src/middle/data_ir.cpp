// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/data_ir.hpp"

#include "common/diagnostic.hpp"
#include "common/floating_bits.hpp"
#include "common/integer_semantics.hpp"
#include "frontend/ast.hpp"
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

bool power_of_two(unsigned value) {
    return value != 0 && (value & (value - 1)) == 0;
}

std::optional<unsigned> object_alignment(const ObjectDecl& object,
                                         unsigned natural,
                                         Diagnostics& diagnostics) {
    const auto* item = attribute(object, "aligned");
    if (!item) return natural;
    if (item->arguments.size() != 1) {
        diagnostics.error(item->location,
                          "aligned requires one integer argument");
        return std::nullopt;
    }
    unsigned parsed{};
    const auto& text = item->arguments.front();
    const auto conversion =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != text.data() + text.size() ||
        !power_of_two(parsed)) {
        diagnostics.error(
            item->location,
            "aligned argument must be a nonzero power of two");
        return std::nullopt;
    }
    return std::max(natural, parsed);
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

const Expr* unwrapped_address(const Expr& expression) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return unwrapped_address(*expression.left);
    }
    if (expression.kind == Expr::Kind::Unary && expression.text == "&" &&
        expression.left) {
        return unwrapped_address(*expression.left);
    }
    return &expression;
}

std::optional<AddressConstant> address_constant(
    const hir::Module& module, const hir::Object& owner,
    const Expr& source) {
    const auto& expression = *unwrapped_address(source);
    if (expression.kind != Expr::Kind::Name) return std::nullopt;
    if (const auto* object =
            find_object(module, expression.text, owner.source_unit)) {
        AddressConstant result;
        result.kind = AddressKind::Object;
        result.object = object->id;
        return result;
    }
    if (const auto* function =
            find_function(module, expression.text, owner.source_unit)) {
        AddressConstant result;
        result.kind = AddressKind::Function;
        result.function = function->id;
        return result;
    }
    const auto split = expression.text.rfind("::");
    if (split == std::string::npos) return std::nullopt;
    const auto* function = find_function(
        module, expression.text.substr(0, split), owner.source_unit);
    if (!function) return std::nullopt;
    const auto* label =
        module.label(function->id, expression.text.substr(split + 2));
    if (!label) return std::nullopt;
    AddressConstant result;
    result.kind = AddressKind::Label;
    result.function = function->id;
    result.label = label->id;
    return result;
}

bool lower_initializer(Object& result, const hir::Module& module,
                       const hir::Object& entity,
                       const ObjectDecl& declaration,
                       const Subtarget& subtarget,
                       Diagnostics& diagnostics) {
    if (!declaration.initializer) return true;
    const auto& expression = *declaration.initializer;
    const auto& type = module.type(entity.type);
    if (type.kind == hir::Type::Kind::Array) {
        if (!type.element ||
            module.type(*type.element).kind != hir::Type::Kind::Builtin ||
            module.type(*type.element).builtin != BuiltinType::U8 ||
            expression.kind != Expr::Kind::String) {
            diagnostics.error(
                expression.location,
                "aggregate array initializers are not implemented yet");
            return false;
        }
        const auto required = expression.string_value.size() + 1;
        if (required > result.size) {
            diagnostics.error(
                expression.location,
                "string initializer does not fit in the u8 array");
            return false;
        }
        result.initializer = InitializerKind::Bytes;
        result.bytes.assign(result.size, 0);
        std::copy(expression.string_value.begin(),
                  expression.string_value.end(), result.bytes.begin());
        return true;
    }
    if (type.kind == hir::Type::Kind::Record) {
        diagnostics.error(
            expression.location,
            "aggregate record initializers are not implemented yet");
        return false;
    }
    if (type.kind == hir::Type::Kind::Pointer ||
        (type.kind == hir::Type::Kind::Builtin &&
         type.builtin == BuiltinType::Label)) {
        result.address = address_constant(module, entity, expression);
        if (!result.address) {
            diagnostics.error(
                expression.location,
                "global pointer/label initializer is not an address constant");
            return false;
        }
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

} // namespace

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
        if (const auto alignment =
                object_alignment(*declaration, natural, diagnostics)) {
            object.alignment = *alignment;
        }
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
            if (object.used) {
                diagnostics.error(declaration->location,
                                  "used requires an object definition");
            }
            if (object.retain) {
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
        if (object.address && object.address->kind == AddressKind::Function &&
            object.address->function) {
            (void)hir::stabilize_function_address(
                hir_module, *object.address->function, declaration->location,
                diagnostics);
        }
        result.objects.push_back(std::move(object));
    }
    return result;
}

} // namespace cross::data
