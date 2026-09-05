// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/ast.hpp"

#include "model/model.hpp"

#include <charconv>
#include <sstream>

namespace cross {

TypePtr builtin_type(BuiltinType kind, bool is_const, bool is_volatile,
                     bool is_atomic) {
    auto type = std::make_shared<Type>();
    type->builtin = kind;
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    type->is_atomic = is_atomic;
    return type;
}

TypePtr pointer_type(TypePtr pointee, bool is_const, bool is_volatile,
                     bool is_atomic) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Pointer;
    type->pointee = std::move(pointee);
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    type->is_atomic = is_atomic;
    return type;
}

TypePtr generic_type(std::string name, bool is_const, bool is_volatile,
                     bool is_atomic) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Generic;
    type->generic_name = std::move(name);
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    type->is_atomic = is_atomic;
    return type;
}

TypePtr vector_type(TypePtr element, std::uint32_t lanes, bool scalable,
                    bool is_const, bool is_volatile, bool is_atomic) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Vector;
    type->element = std::move(element);
    type->lanes = lanes;
    type->scalable = scalable;
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    type->is_atomic = is_atomic;
    return type;
}

TypePtr array_type(TypePtr element, std::uint32_t elements,
                   bool is_const, bool is_volatile) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Array;
    type->element = std::move(element);
    type->lanes = elements;
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    return type;
}

TypePtr record_type(std::string name, bool is_union, bool is_const,
                    bool is_volatile) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Record;
    type->nominal_name = std::move(name);
    type->is_union = is_union;
    type->is_const = is_const;
    type->is_volatile = is_volatile;
    return type;
}

TypePtr enum_type(std::string name, BuiltinType underlying, bool is_const,
                  bool is_volatile, bool is_atomic) {
    auto type = builtin_type(underlying, is_const, is_volatile, is_atomic);
    type->nominal_name = std::move(name);
    return type;
}

std::string type_name(const TypePtr& type) {
    if (!type) return "<invalid>";
    std::string prefix;
    if (type->is_const) prefix += "const ";
    if (type->is_volatile) prefix += "volatile ";
    if (type->is_atomic) prefix += "[[atomic]] ";
    if (type->kind == Type::Kind::Pointer) return prefix + type_name(type->pointee) + " *";
    if (type->kind == Type::Kind::Generic) return prefix + type->generic_name;
    if (type->kind == Type::Kind::Vector) {
        return prefix + (type->scalable ? "scalable_vector<" : "vector<") +
               type_name(type->element) + "," +
               std::to_string(type->lanes) + ">";
    }
    if (type->kind == Type::Kind::Array) {
        return prefix + type_name(type->element) + "[" +
               (type->lanes == 0 ? std::string("*")
                                 : std::to_string(type->lanes)) +
               "]";
    }
    if (type->kind == Type::Kind::Record) {
        return prefix + (type->is_union ? "union " : "struct ") +
               type->nominal_name;
    }
    static constexpr const char* names[] = {
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64",
        "i128", "u128", "iptr", "uptr", "f32", "f64", "f80", "f128", "fptr", "label"
    };
    if (!type->nominal_name.empty()) {
        return prefix + "enum " + type->nominal_name;
    }
    return prefix + names[static_cast<unsigned>(type->builtin)];
}

std::string canonical_type_name(const TypePtr& type) {
    if (!type) return "<invalid>";
    std::string result;
    if (type->is_const) result += 'K';
    if (type->is_volatile) result += 'V';
    if (type->is_atomic) result += 'A';
    if (type->kind == Type::Kind::Pointer) {
        result += 'P';
        result += canonical_type_name(type->pointee);
        return result;
    }
    if (type->kind == Type::Kind::Generic) {
        result += type->generic_name;
        return result;
    }
    if (type->kind == Type::Kind::Vector) {
        result += type->scalable ? "Ds" : "Dv";
        result += std::to_string(type->lanes);
        result += '_';
        result += canonical_type_name(type->element);
        return result;
    }
    if (type->kind == Type::Kind::Array) {
        result += 'A';
        result += type->lanes == 0 ? "*" : std::to_string(type->lanes);
        result += '_';
        result += canonical_type_name(type->element);
        return result;
    }
    if (type->kind == Type::Kind::Record) {
        result += type->is_union ? "union " : "struct ";
        result += type->nominal_name;
        return result;
    }
    if (!type->nominal_name.empty()) {
        result += "enum ";
        result += type->nominal_name;
        return result;
    }
    static constexpr std::string_view names[] = {
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32",
        "i64", "u64", "i128", "u128", "iptr", "uptr", "f32",
        "f64", "f80", "f128", "fptr", "label",
    };
    result += names[static_cast<unsigned>(type->builtin)];
    return result;
}

bool same_type(const TypePtr& left, const TypePtr& right) {
    if (!left || !right || left->kind != right->kind || left->is_const != right->is_const ||
        left->is_volatile != right->is_volatile ||
        left->is_atomic != right->is_atomic) return false;
    if (left->kind == Type::Kind::Pointer) return same_type(left->pointee, right->pointee);
    if (left->kind == Type::Kind::Generic) return left->generic_name == right->generic_name;
    if (left->kind == Type::Kind::Vector) {
        return left->lanes == right->lanes &&
               left->scalable == right->scalable &&
               same_type(left->element, right->element);
    }
    if (left->kind == Type::Kind::Array) {
        return left->lanes == right->lanes &&
               same_type(left->element, right->element);
    }
    if (left->kind == Type::Kind::Record) {
        return left->is_union == right->is_union &&
               left->nominal_name == right->nominal_name;
    }
    return left->builtin == right->builtin &&
           left->nominal_name == right->nominal_name;
}

bool is_integer(const TypePtr& type) {
    return type && type->kind == Type::Kind::Builtin && type->builtin >= BuiltinType::Bool &&
           type->builtin <= BuiltinType::Uptr;
}

bool is_floating(const TypePtr& type) {
    return type && type->kind == Type::Kind::Builtin &&
            (type->builtin == BuiltinType::F32 || type->builtin == BuiltinType::F64 ||
            type->builtin == BuiltinType::F80 || type->builtin == BuiltinType::F128 ||
            type->builtin == BuiltinType::Fptr);
}

bool is_scalar(const TypePtr& type) {
    return type && (type->kind == Type::Kind::Pointer || is_integer(type) || is_floating(type) ||
                    (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Label));
}

bool is_vector(const TypePtr& type) {
    return type && type->kind == Type::Kind::Vector;
}

bool is_nominal(const TypePtr& type) {
    return type && ((type->kind == Type::Kind::Builtin &&
                     !type->nominal_name.empty()) ||
                    type->kind == Type::Kind::Record);
}

unsigned type_bits(const TypePtr& type) {
    if (!type) return 0;
    if (type->kind == Type::Kind::Pointer) return 64;
    if (type->kind == Type::Kind::Generic) return 0;
    if (type->kind == Type::Kind::Vector) {
        return type->scalable ? 0 : type_bits(type->element) * type->lanes;
    }
    if (type->kind == Type::Kind::Array) {
        return type_bits(type->element) * type->lanes;
    }
    if (type->kind == Type::Kind::Record) return 0;
    switch (type->builtin) {
    case BuiltinType::Bool: case BuiltinType::I8: case BuiltinType::U8: return 8;
    case BuiltinType::I16: case BuiltinType::U16: return 16;
    case BuiltinType::I32: case BuiltinType::U32: case BuiltinType::F32: return 32;
    case BuiltinType::I64: case BuiltinType::U64: case BuiltinType::Iptr:
    case BuiltinType::Uptr: case BuiltinType::F64: case BuiltinType::Fptr:
    case BuiltinType::Label: return 64;
    case BuiltinType::I128: case BuiltinType::U128: case BuiltinType::F128: return 128;
    case BuiltinType::F80: return 80;
    case BuiltinType::Void: return 0;
    }
    return 0;
}

const Attribute* FunctionDecl::attribute(std::string_view sought) const {
    for (const auto& item : attributes) if (item.name == sought) return &item;
    return nullptr;
}

std::string encode_link_name(std::string_view qualified_name, bool label,
                             std::string_view mangling_name) {
    return encode_model_link_name(qualified_name, label, mangling_name);
}

std::optional<std::string> decode_string_literal(std::string_view text) {
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return std::nullopt;
    std::string result;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        char ch = text[i];
        if (ch != '\\') {
            result.push_back(ch);
            continue;
        }
        if (++i + 1 > text.size()) return std::nullopt;
        switch (text[i]) {
        case '\'': result.push_back('\''); break;
        case '?': result.push_back('?'); break;
        case 'a': result.push_back('\a'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'v': result.push_back('\v'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case '\\': result.push_back('\\'); break;
        case '"': result.push_back('"'); break;
        default: {
            const bool hexadecimal = text[i] == 'x';
            if (!hexadecimal && (text[i] < '0' || text[i] > '7')) return std::nullopt;
            if (hexadecimal) ++i;
            unsigned value = 0;
            unsigned count = 0;
            while (i + 1 < text.size() && (hexadecimal || count < 3)) {
                const auto character = text[i];
                const auto digit = character >= '0' && character <= '9' ? static_cast<unsigned>(character - '0')
                                 : character >= 'a' && character <= 'f' ? static_cast<unsigned>(character - 'a' + 10)
                                 : character >= 'A' && character <= 'F' ? static_cast<unsigned>(character - 'A' + 10) : 16U;
                const auto base = hexadecimal ? 16U : 8U;
                if (digit >= base) break;
                value = value * base + digit;
                if (value > 255) return std::nullopt;
                ++count;
                ++i;
            }
            if (count == 0) return std::nullopt;
            --i;
            result.push_back(static_cast<char>(value));
            break;
        }
        }
    }
    return result;
}

std::optional<std::uint32_t> decode_character_literal(std::string_view text) {
    if (text.size() < 3 || text.front() != '\'' || text.back() != '\'') return std::nullopt;
    auto quoted = std::string(text);
    quoted.front() = quoted.back() = '"';
    const auto decoded = decode_string_literal(quoted);
    if (!decoded || decoded->size() != 1) return std::nullopt;
    return static_cast<unsigned char>(decoded->front());
}

} // namespace cross
