// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/ast.hpp"

#include "model/model.hpp"

#include <algorithm>
#include <charconv>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cross {

TypePtr tokens_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Tokens;
    return type;
}

TypePtr syntax_match_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::SyntaxMatch;
    return type;
}

TypePtr syntax_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Syntax;
    return type;
}

TypePtr span_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Span;
    return type;
}

TypePtr context_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Context;
    return type;
}

TypePtr copy_type(const TypePtr& type) {
    if (!type) return {};
    std::unordered_map<const Type*, TypePtr> copies;
    std::vector<std::pair<TypePtr, TypePtr>> pending;
    const auto copy = [&](const TypePtr& source) -> TypePtr {
        if (!source) return {};
        const auto found = copies.find(source.get());
        if (found != copies.end()) return found->second;
        auto destination = std::make_shared<Type>(*source);
        copies.emplace(source.get(), destination);
        pending.emplace_back(source, destination);
        return destination;
    };
    auto result = copy(type);
    while (!pending.empty()) {
        const auto [source, destination] = std::move(pending.back());
        pending.pop_back();
        destination->pointee = copy(source->pointee);
        destination->element = copy(source->element);
        if (source->function) {
            destination->function = std::make_shared<FunctionType>(*source->function);
            destination->function->result = copy(source->function->result);
            for (auto& parameter : destination->function->parameters)
                parameter.type = copy(parameter.type);
        }
    }
    return result;
}

bool has_pending_array_bound(const TypePtr& type) {
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> pending{type};
    while (!pending.empty()) {
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        if (next->array_bound && next->lanes == 0) return true;
        pending.push_back(next->element);
        pending.push_back(next->pointee);
        if (next->function) {
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) pending.push_back(parameter.type);
        }
    }
    return false;
}

std::span<const std::string_view> core_keyword_names() {
    static constexpr std::string_view names[] = {
        "bool", "break", "case", "const", "continue", "default", "do", "else",
        "enum", "f32", "f64", "f80", "f128", "for", "fptr", "global", "goto",
        "i8", "i16", "i32", "i64", "i128", "if", "in", "inline", "inout", "iptr",
        "label", "namespace", "out", "register", "restrict", "return", "sizeof",
        "stack", "static", "struct", "switch", "syntax", "typedef", "u8", "u16",
        "u32", "u64", "union", "u128", "uptr", "using", "void", "volatile", "while"
    };
    return names;
}

bool is_reserved_identifier(std::string_view name) {
    const auto names = core_keyword_names();
    return std::find(names.begin(), names.end(), name) != names.end() || name.starts_with("$::");
}

TypePtr bytes_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Bytes;
    return type;
}

TypePtr buffer_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Buffer;
    return type;
}

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

TypePtr function_type(TypePtr result, std::vector<ParameterDecl> parameters,
                      bool variadic, std::string abi) {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::Function;
    type->function = std::make_shared<FunctionType>();
    type->function->result = std::move(result);
    type->function->parameters = std::move(parameters);
    type->function->variadic = variadic;
    type->function->abi = std::move(abi);
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

TypePtr record_type(const RecordDecl& declaration) {
    auto type = record_type(declaration.name, declaration.is_union);
    type->nominal_identity = declaration.nominal_identity;
    return type;
}

TypePtr enum_type(const EnumDecl& declaration) {
    auto type = enum_type(declaration.name, declaration.underlying);
    type->nominal_identity = declaration.nominal_identity;
    return type;
}

std::string type_name(const TypePtr& type) {
    if (!type) return "<invalid>";
    if (type->kind == Type::Kind::Tokens) return "$::meta::tokens";
    if (type->kind == Type::Kind::SyntaxMatch) return "$::meta::syntax_match";
    if (type->kind == Type::Kind::Syntax) return "$::meta::syntax";
    if (type->kind == Type::Kind::Span) return "$::meta::span";
    if (type->kind == Type::Kind::Context) return "$::meta::context";
    if (type->kind == Type::Kind::Bytes) return "$::meta::bytes";
    if (type->kind == Type::Kind::Buffer) return "$::meta::buffer";
    std::string prefix;
    if (type->is_const) prefix += "const ";
    if (type->is_volatile) prefix += "volatile ";
    if (type->is_restrict) prefix += "restrict ";
    if (type->is_atomic) prefix += "[[atomic]] ";
    if (type->kind == Type::Kind::Pointer) {
        return prefix + type_name(type->pointee) +
               (type->address_space == 0
                    ? " *"
                    : " [[address_space(" +
                          std::to_string(type->address_space) + ")]] *");
    }
    if (type->kind == Type::Kind::Function && type->function) {
        const auto& signature = *type->function;
        std::string result = type_name(signature.result) + " (";
        for (std::size_t index = 0; index < signature.parameters.size();
             ++index) {
            if (index != 0) result += ", ";
            const auto& parameter = signature.parameters[index];
            result += parameter.mode == ParameterMode::In    ? "in "
                      : parameter.mode == ParameterMode::Out ? "out "
                                                             : "inout ";
            result += type_name(parameter.type);
            if (parameter.location_name)
                result += " \"" + *parameter.location_name + "\"";
        }
        if (signature.variadic)
            result += signature.parameters.empty() ? "..." : ", ...";
        result += ")";
        if (signature.result_location)
            result += " -> \"" + *signature.result_location + "\"";
        if (!signature.abi.empty())
            result += " [[abi(\"" + signature.abi + "\")]]";
        for (const auto& clobber : signature.clobbers)
            result += " [[clobber(\"" + clobber + "\")]]";
        if (signature.stack_cleanup)
            result += " [[stack_cleanup(\"" + *signature.stack_cleanup + "\")]]";
        return prefix + result;
    }
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
    if (!type->nominal_key().empty()) {
        return prefix + "enum " + type->nominal_name;
    }
    return prefix + names[static_cast<unsigned>(type->builtin)];
}

std::string canonical_type_name(const TypePtr& type) {
    if (!type) return "<invalid>";
    if (type->kind == Type::Kind::Tokens) return "$::meta::tokens";
    if (type->kind == Type::Kind::SyntaxMatch) return "$::meta::syntax_match";
    if (type->kind == Type::Kind::Syntax) return "$::meta::syntax";
    if (type->kind == Type::Kind::Span) return "$::meta::span";
    if (type->kind == Type::Kind::Context) return "$::meta::context";
    if (type->kind == Type::Kind::Bytes) return "$::meta::bytes";
    if (type->kind == Type::Kind::Buffer) return "$::meta::buffer";
    std::string result;
    if (type->is_const) result += 'K';
    if (type->is_volatile) result += 'V';
    if (type->is_restrict) result += 'R';
    if (type->is_atomic) result += 'A';
    if (type->kind == Type::Kind::Pointer) {
        result += 'P';
        if (type->address_space != 0) {
            result += "U" + std::to_string(type->address_space) + '_';
        }
        result += canonical_type_name(type->pointee);
        return result;
    }
    if (type->kind == Type::Kind::Function && type->function) {
        const auto& signature = *type->function;
        result +=
            "F" + std::to_string(signature.abi.size()) + "_" + signature.abi;
        const auto append = [&](const TypePtr& item) {
            const auto spelling = canonical_type_name(item);
            result += std::to_string(spelling.size()) + "_" + spelling;
        };
        const auto append_text = [&](const std::string& item) {
            result += std::to_string(item.size()) + "_" + item;
        };
        append(signature.result);
        append_text(signature.result_location.value_or("auto"));
        append_text(signature.stack_cleanup.value_or("caller"));
        auto clobbers = signature.clobbers;
        std::sort(clobbers.begin(), clobbers.end());
        result += std::to_string(clobbers.size()) + "_";
        for (const auto& clobber : clobbers) append_text(clobber);
        result += "_" + std::to_string(signature.parameters.size()) + "_";
        for (const auto& parameter : signature.parameters) {
            result += parameter.mode == ParameterMode::In    ? 'i'
                      : parameter.mode == ParameterMode::Out ? 'o'
                                                             : 'b';
            append(callable_parameter_type(parameter.type, parameter.mode));
            append_text(parameter.location_name.value_or("auto"));
        }
        result += signature.variadic ? "zE" : "E";
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
        result += type->nominal_key().canonical_name();
        return result;
    }
    if (!type->nominal_key().empty()) {
        result += "enum ";
        result += type->nominal_key().canonical_name();
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
        left->is_restrict != right->is_restrict ||
        left->is_atomic != right->is_atomic) return false;
    if (left->kind == Type::Kind::Pointer)
        return left->address_space == right->address_space &&
               same_type(left->pointee, right->pointee);
    if (left->kind == Type::Kind::Function) {
        if (!left->function || !right->function) return false;
        const auto& a = *left->function;
        const auto& b = *right->function;
        auto a_clobbers = a.clobbers;
        auto b_clobbers = b.clobbers;
        std::sort(a_clobbers.begin(), a_clobbers.end());
        std::sort(b_clobbers.begin(), b_clobbers.end());
        if (a.abi != b.abi || a.variadic != b.variadic ||
            a.result_location.value_or("auto") !=
                b.result_location.value_or("auto") ||
            a.stack_cleanup.value_or("caller") !=
                b.stack_cleanup.value_or("caller") ||
            a_clobbers != b_clobbers ||
            a.parameters.size() != b.parameters.size() ||
            !same_type(a.result, b.result))
            return false;
        for (std::size_t index = 0; index < a.parameters.size(); ++index) {
            if (a.parameters[index].mode != b.parameters[index].mode ||
                a.parameters[index].location_name.value_or("auto") !=
                    b.parameters[index].location_name.value_or("auto") ||
                !same_type(callable_parameter_type(a.parameters[index].type,
                                                   a.parameters[index].mode),
                           callable_parameter_type(b.parameters[index].type,
                                                   b.parameters[index].mode)))
                return false;
        }
        return true;
    }
    if (left->kind == Type::Kind::Generic) return left->generic_name == right->generic_name;
    if (left->kind == Type::Kind::Vector) {
        return left->lanes == right->lanes &&
               left->scalable == right->scalable &&
               same_type(left->element, right->element);
    }
    if (left->kind == Type::Kind::Array) {
        return left->lanes == right->lanes &&
               (left->lanes != 0 || left->array_bound == right->array_bound) &&
               same_type(left->element, right->element);
    }
    if (left->kind == Type::Kind::Record) {
        return left->is_union == right->is_union &&
               left->nominal_key() == right->nominal_key();
    }
    if (left->kind == Type::Kind::Tokens || left->kind == Type::Kind::SyntaxMatch ||
        left->kind == Type::Kind::Syntax || left->kind == Type::Kind::Span ||
        left->kind == Type::Kind::Context || left->kind == Type::Kind::Bytes ||
        left->kind == Type::Kind::Buffer) return true;
    return left->builtin == right->builtin &&
           left->nominal_key() == right->nominal_key();
}

TypePtr callable_parameter_type(const TypePtr& type, ParameterMode mode) {
    if (!type || mode != ParameterMode::In || !type->is_const) return type;
    auto normalized = std::make_shared<Type>(*type);
    normalized->is_const = false;
    return normalized;
}

bool compatible_pointee(const TypePtr& source, const TypePtr& destination,
                        unsigned depth, bool nested_qualification) {
    if (!source || !destination || depth >= 32) return false;
    if ((source->is_const && !destination->is_const) ||
        (source->is_volatile && !destination->is_volatile) ||
        source->is_atomic != destination->is_atomic) return false;
    if (!nested_qualification &&
        ((!source->is_const && destination->is_const) ||
         (!source->is_volatile && destination->is_volatile))) return false;
    const auto is_void = [](const TypePtr& type) {
        return type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void;
    };
    if (depth == 0 && (is_void(source) || is_void(destination)))
        return source->kind != Type::Kind::Function && destination->kind != Type::Kind::Function;
    if (source->kind != destination->kind) return false;
    if (source->kind == Type::Kind::Pointer)
        return source->address_space == destination->address_space &&
            compatible_pointee(source->pointee, destination->pointee, depth + 1,
                               nested_qualification && destination->is_const);
    if (source->kind == Type::Kind::Array || source->kind == Type::Kind::Vector)
        return source->lanes == destination->lanes && source->scalable == destination->scalable &&
            compatible_pointee(source->element, destination->element, depth + 1,
                               nested_qualification);
    auto from = std::make_shared<Type>(*source);
    auto to = std::make_shared<Type>(*destination);
    from->is_const = to->is_const = false;
    from->is_volatile = to->is_volatile = false;
    from->is_restrict = to->is_restrict = false;
    return same_type(from, to);
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
                     !type->nominal_key().empty()) ||
                    type->kind == Type::Kind::Record);
}

unsigned type_bits(const TypePtr& type) {
    if (!type) return 0;
    if (type->kind == Type::Kind::Pointer) return 64;
    if (type->kind == Type::Kind::Generic || type->kind == Type::Kind::Function ||
        type->kind == Type::Kind::Tokens || type->kind == Type::Kind::SyntaxMatch ||
        type->kind == Type::Kind::Syntax || type->kind == Type::Kind::Span ||
        type->kind == Type::Kind::Context || type->kind == Type::Kind::Bytes ||
        type->kind == Type::Kind::Buffer)
        return 0;
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
