// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/ast.hpp"
#include "frontend/vector_constraints.hpp"

#include "model/model.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace cross {

namespace detail {
class AstRelease {
public:
    template<class Node>
    static void release(Node& node) noexcept {
        OwnerRelease::run([&](OwnerRelease& release) { detach(node, release); });
    }
private:
    static void detach(Type& node, OwnerRelease& release) noexcept {
        release.take(node.pointee);
        release.take(node.element);
        release.take(node.array_bound);
        release.take(node.vector_bound);
        for (auto& request : node.alignment_requests) release.take(request);
        // Shared callable metadata is not edited speculatively. Its own
        // destructor forwards its edges only when the last owner releases it.
        node.function.reset();
    }
    static void detach(FunctionType& node, OwnerRelease& release) noexcept {
        release.take(node.result);
        for (auto& parameter : node.parameters) {
            release.take(parameter.type);
            release.take(parameter.declared_array_type);
        }
    }
    static void detach(Expr& node, OwnerRelease& release) noexcept {
        release.take(node.left);
        release.take(node.right);
        release.take(node.third);
        release.take(node.type);
        release.take(node.deferred_generic_signature);
        for (auto& argument : node.arguments) release.take(argument);
        for (auto& argument : node.generic_arguments) {
            release.take(argument.type);
            release.take(argument.value);
        }
        for (auto& relocation : node.object_relocations) release.take(relocation.type);
        for (auto& entry : node.initializer_entries) {
            for (auto& designator : entry.designators) release.take(designator.index);
            release.take(entry.value);
        }
    }
};
} // namespace detail

Type::~Type() { detail::AstRelease::release(*this); }
FunctionType::~FunctionType() { detail::AstRelease::release(*this); }
Expr::~Expr() { detail::AstRelease::release(*this); }

TagBinding::~TagBinding() {
    detail::OwnerRelease::run([&](detail::OwnerRelease& release) {
        release.take(declaration_source);
    });
}

Statement::~Statement() {
    Statement* pending{};
    const auto detach = [&](Statement& node) {
        const auto take = [&](std::unique_ptr<Statement>& owner) {
            if (auto* child = owner.release()) {
                child->teardown_next_ = pending;
                pending = child;
            }
        };
        for (auto& child : node.statements) take(child);
        take(node.first);
        take(node.second);
    };
    detach(*this);
    while (pending) {
        auto* node = pending;
        pending = node->teardown_next_;
        node->teardown_next_ = nullptr;
        detach(*node);
        // Statement edges are empty before delete, so nested teardown is a
        // leaf. Payload Expr and shared metadata keep their normal owners.
        delete node;
    }
}

MemberName member_name(const Expr& expression) {
    return {expression.text, token_origin(expression.location).fresh};
}

bool is_meta_type(const TypePtr& type) {
    if (!type) return false;
    switch (type->kind) {
    case Type::Kind::Tokens: case Type::Kind::SyntaxMatch: case Type::Kind::Syntax:
    case Type::Kind::Span: case Type::Kind::Context: case Type::Kind::Bytes:
    case Type::Kind::Buffer: case Type::Kind::TypeValue: case Type::Kind::CallSite: return true;
    default: return false;
    }
}

AliasDefinition::AliasDefinition(const TypePtr& type, std::uint64_t storage)
    : type_(copy_type(type)), storage_(storage) {}

TypePtr AliasDefinition::instantiate() const { return copy_type(type_); }

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
            for (auto& parameter : destination->function->parameters) {
                parameter.type = copy(parameter.type);
                parameter.declared_array_type = copy(parameter.declared_array_type);
            }
        }
    }
    return result;
}

bool has_pending_type_bound(const TypePtr& type) {
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> pending{type};
    while (!pending.empty()) {
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        if (((next->array_bound || next->vector_bound) && next->lanes == 0) ||
            (next->kind == Type::Kind::Array && !next->lanes &&
             next->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext) ||
            deferred_vector_extent(next) || !next->alignment_requests.empty() ||
            next->kind == Type::Kind::Generic) return true;
        // Written values of a generic record use compare only once evaluated.
        if (next->nominal_arguments && !next->nominal_arguments->normalized)
            for (const auto& item : next->nominal_arguments->items) {
                if (item.value) return true;
                pending.push_back(item.type);
            }
        pending.push_back(next->element);
        pending.push_back(next->pointee);
        if (next->function) {
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                pending.push_back(parameter.type);
                pending.push_back(parameter.declared_array_type);
            }
        }
    }
    return false;
}

bool has_context_dependent_type_bound(const TypePtr& type) {
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> pending{type};
    while (!pending.empty()) {
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        if (deferred_vector_extent(next) || (next->kind == Type::Kind::Array && !next->lanes &&
            next->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext)) return true;
        pending.push_back(next->element);
        pending.push_back(next->pointee);
        if (next->function) {
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                pending.push_back(parameter.type);
                pending.push_back(parameter.declared_array_type);
            }
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

TypePtr type_value_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::TypeValue;
    return type;
}

TypePtr call_site_type() {
    auto type = std::make_shared<Type>();
    type->kind = Type::Kind::CallSite;
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
    type->nominal_arguments = declaration.nominal_arguments;
    return type;
}

namespace {

TypePtr generic_value_type(const Expr& value) {
    if (value.type) return value.type;
    if (value.evaluated_integer) return builtin_type(value.evaluated_integer->type);
    return {};
}

// Normalized generic values compare by their typed value, address, or label.
// Written ones, as in the generic declarations that a redeclaration compares,
// compare by their written form.
bool same_generic_value(const Expr* left, const Expr* right) {
    if (left == right) return true;
    if (!left || !right) return false;
    const auto label = [](const Expr& value) {
        return value.name_context ? value.name_context->label_address.get() : nullptr;
    };
    if (left->evaluated_address || right->evaluated_address)
        return left->evaluated_address && right->evaluated_address &&
            *left->evaluated_address == *right->evaluated_address &&
            same_type(generic_value_type(*left), generic_value_type(*right));
    if (left->evaluated_integer || right->evaluated_integer)
        return left->evaluated_integer && right->evaluated_integer &&
            left->evaluated_integer->value == right->evaluated_integer->value &&
            same_type(generic_value_type(*left), generic_value_type(*right));
    if (label(*left) || label(*right)) return label(*left) && label(*right) && *label(*left) == *label(*right);
    if (left->kind != right->kind || left->text != right->text || left->string_value != right->string_value ||
        static_cast<bool>(left->type) != static_cast<bool>(right->type) ||
        (left->type && !same_type(left->type, right->type)) ||
        left->arguments.size() != right->arguments.size() ||
        !same_generic_value(left->left.get(), right->left.get()) ||
        !same_generic_value(left->right.get(), right->right.get()) ||
        !same_generic_value(left->third.get(), right->third.get())) return false;
    for (std::size_t index = 0; index < left->arguments.size(); ++index)
        if (!same_generic_value(left->arguments[index].get(), right->arguments[index].get())) return false;
    return true;
}

// Consistent with same_type: equal types hash equally.
std::size_t type_identity_hash(const TypePtr& type, unsigned depth) {
    if (!type || depth > 16) return 0;
    std::size_t result = static_cast<std::size_t>(type->kind) * 131 + static_cast<std::size_t>(type->builtin);
    const auto mix = [&](std::size_t value) { result ^= value + 0x9e3779b9U + (result << 6) + (result >> 2); };
    mix((type->is_const ? 1U : 0U) | (type->is_volatile ? 2U : 0U) | (type->is_restrict ? 4U : 0U) |
        (type->is_atomic ? 8U : 0U));
    if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) mix(type->lanes);
    if (type->kind == Type::Kind::Record || type->kind == Type::Kind::Builtin) {
        mix(type->nominal_identity ? std::hash<const NominalTypeIdentity*>{}(type->nominal_identity.get())
                                   : std::hash<std::string>{}(type->nominal_name));
        if (type->nominal_arguments)
            for (const auto& item : type->nominal_arguments->items)
                mix(item.type ? type_identity_hash(item.type, depth + 1)
                    : item.value && item.value->evaluated_integer ? item.value->evaluated_integer->value.low : 0);
    }
    mix(type_identity_hash(type->pointee, depth + 1));
    mix(type_identity_hash(type->element, depth + 1));
    if (type->function) mix(type->function->parameters.size());
    return result;
}

} // namespace

bool same_nominal_arguments(const NominalArguments& left, const NominalArguments& right) {
    if (&left == &right) return true;
    if (left.items.size() != right.items.size()) return false;
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        const auto& a = left.items[index];
        const auto& b = right.items[index];
        if (static_cast<bool>(a.type) != static_cast<bool>(b.type) ||
            static_cast<bool>(a.value) != static_cast<bool>(b.value) ||
            (a.type && !same_type(a.type, b.type)) ||
            (a.value && !same_generic_value(a.value.get(), b.value.get()))) return false;
    }
    return true;
}

std::size_t nominal_arguments_hash(const NominalArguments& arguments) {
    std::size_t result = arguments.items.size();
    for (const auto& item : arguments.items)
        result ^= (item.type ? type_identity_hash(item.type, 0)
                   : item.value && item.value->evaluated_integer ? item.value->evaluated_integer->value.low : 0) +
            0x9e3779b9U + (result << 6) + (result >> 2);
    return result;
}

std::string canonical_nominal_arguments(const NominalArguments& arguments) {
    std::string result = "<";
    for (std::size_t index = 0; index < arguments.items.size(); ++index) {
        const auto& item = arguments.items[index];
        if (index != 0) result += ',';
        result += item.type ? canonical_type_name(item.type)
            : !item.spelling.empty() ? item.spelling : item.value ? item.value->text : std::string("<invalid>");
    }
    return result + '>';
}

TypePtr enum_type(const EnumDecl& declaration) {
    auto type = enum_type(declaration.name, declaration.underlying);
    type->nominal_identity = declaration.nominal_identity;
    type->captured_tag_errors = declaration.captured_type_errors;
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
    if (type->kind == Type::Kind::TypeValue) return "$::meta::type";
    if (type->kind == Type::Kind::CallSite) return "$::meta::call";
    std::string prefix;
    if (type->is_const) prefix += "const ";
    if (type->is_volatile) prefix += "volatile ";
    if (type->is_restrict) prefix += "restrict ";
    if (type->is_atomic) prefix += "[[atomic]] ";
    if (type->alignment) prefix += "[[aligned(" + std::to_string(type->alignment) + ")]] ";
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
        auto result = prefix + (type->is_union ? "union " : "struct ") +
               (type->nominal_name.empty() ? "<anonymous>" : type->nominal_name);
        if (type->nominal_arguments) {
            const auto& items = type->nominal_arguments->items;
            for (std::size_t index = 0; index < items.size(); ++index) {
                result += index == 0 ? "<" : ", ";
                result += items[index].type ? type_name(items[index].type)
                    : !items[index].spelling.empty() ? items[index].spelling
                    : items[index].value ? items[index].value->text : std::string("<invalid>");
            }
            result += '>';
        }
        return result;
    }
    static constexpr const char* names[] = {
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64",
        "i128", "u128", "iptr", "uptr", "f32", "f64", "f80", "f128", "fptr", "label"
    };
    if (!type->nominal_key().empty()) {
        return prefix + "enum " + (type->nominal_name.empty() ? "<anonymous>" : type->nominal_name);
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
    if (type->kind == Type::Kind::TypeValue) return "$::meta::type";
    if (type->kind == Type::Kind::CallSite) return "$::meta::call";
    std::string result;
    if (type->is_const) result += 'K';
    if (type->is_volatile) result += 'V';
    if (type->is_restrict) result += 'R';
    if (type->is_atomic) result += 'A';
    if (type->alignment) result += 'L' + std::to_string(type->alignment) + '_';
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
        append(callable_result_type(signature.result));
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

enum class TypeComparisonMode { Exact, Source, Generic };

static bool same_type_impl(const TypePtr& left, const TypePtr& right,
                          TypeComparisonMode mode, bool& pending_extent) {
    const auto same = [&](const TypePtr& a, const TypePtr& b) {
        return same_type_impl(a, b, mode, pending_extent);
    };
    if (!left || !right || left->kind != right->kind || left->is_const != right->is_const ||
        left->is_volatile != right->is_volatile ||
        left->is_restrict != right->is_restrict ||
        left->is_atomic != right->is_atomic) return false;
    if (!left->alignment_requests.empty() || !right->alignment_requests.empty()) {
        if (mode == TypeComparisonMode::Exact) {
            if (left->alignment != right->alignment ||
                left->alignment_requests != right->alignment_requests) return false;
        } else {
            pending_extent = true;
        }
    } else if (left->alignment != right->alignment) {
        return false;
    }
    if (left->kind == Type::Kind::Pointer)
        return left->address_space == right->address_space &&
               same(left->pointee, right->pointee);
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
            !same(callable_result_type(a.result), callable_result_type(b.result)))
            return false;
        for (std::size_t index = 0; index < a.parameters.size(); ++index) {
            if (a.parameters[index].mode != b.parameters[index].mode ||
                a.parameters[index].location_name.value_or("auto") !=
                    b.parameters[index].location_name.value_or("auto") ||
                !same(callable_parameter_type(a.parameters[index].type,
                                                   a.parameters[index].mode),
                           callable_parameter_type(b.parameters[index].type,
                                                   b.parameters[index].mode)))
                return false;
        }
        return true;
    }
    if (left->kind == Type::Kind::Generic) return generic_type_key(*left) == generic_type_key(*right);
    if (left->kind == Type::Kind::Vector) {
        if (left->scalable != right->scalable) return false;
        const auto pending = [mode](const TypePtr& type) {
            return deferred_vector_extent(type) ||
                (mode == TypeComparisonMode::Generic && !type->lanes && type->vector_bound);
        };
        if (mode != TypeComparisonMode::Exact && (pending(left) || pending(right))) {
            if ((!pending(left) && !left->lanes) ||
                (!pending(right) && !right->lanes)) return false;
            pending_extent = true;
            return same(left->element, right->element);
        }
        return left->lanes == right->lanes &&
               (left->lanes != 0 || left->vector_bound == right->vector_bound) &&
               same(left->element, right->element);
    }
    if (left->kind == Type::Kind::Array) {
        const auto pending = [mode](const TypePtr& type) {
            return !type->lanes && (type->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext ||
                (mode == TypeComparisonMode::Generic && type->array_bound));
        };
        if (mode != TypeComparisonMode::Exact && (pending(left) || pending(right))) {
            if ((!pending(left) && !left->lanes) || (!pending(right) && !right->lanes)) return false;
            pending_extent = true;
            return same(left->element, right->element);
        }
        return left->lanes == right->lanes &&
               (left->lanes != 0 || left->array_bound == right->array_bound) &&
               same(left->element, right->element);
    }
    if (left->kind == Type::Kind::Record) {
        if (left->is_union != right->is_union) return false;
        const auto* a = left->nominal_arguments.get();
        const auto* b = right->nominal_arguments.get();
        if (mode != TypeComparisonMode::Generic || !a || !b || a->normalized || b->normalized)
            return left->nominal_key() == right->nominal_key();
        // Like an extent, a written value argument that depends on generic
        // parameters is compared for each used instance.
        if (!(left->nominal_key().generic() == right->nominal_key().generic()) ||
            a->items.size() != b->items.size()) return false;
        for (std::size_t index = 0; index < a->items.size(); ++index) {
            const auto& x = a->items[index];
            const auto& y = b->items[index];
            if (static_cast<bool>(x.type) != static_cast<bool>(y.type) ||
                static_cast<bool>(x.value) != static_cast<bool>(y.value) ||
                (x.type && !same(x.type, y.type))) return false;
            if (x.value && !same_generic_value(x.value.get(), y.value.get())) pending_extent = true;
        }
        return true;
    }
    if (left->kind == Type::Kind::Tokens || left->kind == Type::Kind::SyntaxMatch ||
        left->kind == Type::Kind::Syntax || left->kind == Type::Kind::Span ||
        left->kind == Type::Kind::Context || left->kind == Type::Kind::Bytes ||
        left->kind == Type::Kind::Buffer || left->kind == Type::Kind::TypeValue ||
        left->kind == Type::Kind::CallSite) return true;
    return left->builtin == right->builtin &&
           left->nominal_key() == right->nominal_key();
}

bool same_type(const TypePtr& left, const TypePtr& right) {
    bool unused{};
    return same_type_impl(left, right, TypeComparisonMode::Exact, unused);
}

const RecordDecl* RecordSourceIndex::definition(const Program& program, const NominalTypeKey& key) {
    if (program_ != &program || records_ != program.records.data() || count_ != program.records.size()) {
        definitions_.clear();
        for (const auto& record : program.records)
            definitions_[record.nominal_key()].push_back(&record);
        program_ = &program;
        records_ = program.records.data();
        count_ = program.records.size();
    }
    const auto found = definitions_.find(key);
    if (found != definitions_.end())
        for (const auto* record : found->second)
            if (record->complete) return record;
    return nullptr;
}

std::shared_ptr<const RecordDecl> Program::record_definition(const NominalTypeKey& key) const {
    if (evaluation_record_definition) {
        // A nested layout query may replace the scoped provider while running.
        const auto query = evaluation_record_definition;
        if (auto view = query(key)) return view;
    }
    if (const auto* record = record_index.definition(*this, key))
        return std::shared_ptr<const RecordDecl>{std::shared_ptr<const RecordDecl>{}, record};
    return {};
}

namespace {

template<class Matches>
MemberLookup search_embedded(const Program& program, const RecordDecl& record, const Matches& matches) {
    struct Candidate {
        std::vector<const RecordMemberDecl*> members;
        std::vector<std::shared_ptr<const RecordDecl>> owners;
    };
    std::vector<Candidate> level{{{}, {{std::shared_ptr<const RecordDecl>{}, &record}}}};
    MemberLookup result;
    while (!level.empty() && result.members.empty()) {
        std::vector<Candidate> next;
        for (const auto& candidate : level) {
            for (const auto& member : candidate.owners.back()->members) {
                if (matches(member)) {
                    if (!result.members.empty()) {
                        result.ambiguous = true;
                        return result;
                    }
                    result.members = candidate.members;
                    result.members.push_back(&member);
                    result.owners = candidate.owners;
                }
                if (!result.members.empty() || !member.embedded || !member.type ||
                    member.type->kind != Type::Kind::Record) continue;
                auto definition = program.record_definition(member.type->nominal_key());
                // An invalid record embedding itself is diagnosed elsewhere.
                if (!definition || std::any_of(candidate.owners.begin(), candidate.owners.end(),
                        [&](const auto& owner) { return owner->nominal_key() == definition->nominal_key(); }))
                    continue;
                auto deeper = candidate;
                deeper.members.push_back(&member);
                deeper.owners.push_back(std::move(definition));
                next.push_back(std::move(deeper));
            }
        }
        level = std::move(next);
    }
    return result;
}

} // namespace

MemberLookup find_record_member(const Program& program, const RecordDecl& record,
                                const MemberName& name) {
    return search_embedded(program, record,
        [&](const RecordMemberDecl& member) { return member.member_name() == name; });
}

MemberLookup find_embedded_type(const Program& program, const TypePtr& outer,
                                const TypePtr& inner) {
    if (!outer || !inner || outer->kind != Type::Kind::Record || inner->kind != Type::Kind::Record)
        return {};
    const auto record = program.record_definition(outer->nominal_key());
    if (!record) return {};
    auto result = search_embedded(program, *record, [&](const RecordMemberDecl& member) {
        return member.embedded && member.type && member.type->kind == Type::Kind::Record &&
            member.type->is_union == inner->is_union && member.type->nominal_key() == inner->nominal_key();
    });
    if (!result.owners.empty()) result.owners.front() = record;
    return result;
}

TypePtr member_lookup_owner(const TypePtr& owner, const MemberLookup& lookup) {
    if (!owner || lookup.members.size() < 2) return owner;
    auto result = owner;
    for (std::size_t at = 0; at + 1 < lookup.members.size(); ++at) {
        auto next = std::make_shared<Type>(*lookup.members[at]->type);
        next->is_const = next->is_const || result->is_const;
        next->is_volatile = next->is_volatile || result->is_volatile;
        next->may_alias = next->may_alias || result->may_alias;
        result = std::move(next);
    }
    return result;
}

std::optional<EmbeddedConversion> embedded_pointer_conversion(const Program& program,
    const TypePtr& source, const TypePtr& destination, bool explicit_cast) {
    if (!source || !destination || source->kind != Type::Kind::Pointer ||
        destination->kind != Type::Kind::Pointer) return {};
    const auto& from = source->pointee;
    const auto& to = destination->pointee;
    if (!from || !to || from->kind != Type::Kind::Record || to->kind != Type::Kind::Record ||
        (from->is_union == to->is_union && from->nominal_key() == to->nominal_key())) return {};
    EmbeddedConversion result{find_embedded_type(program, from, to), false};
    if (result.path.members.empty() && !result.path.ambiguous && explicit_cast)
        result = {find_embedded_type(program, to, from), true};
    if (result.path.members.empty() && !result.path.ambiguous) return {};
    return result;
}

const char* embedded_conversion_qualifier_error(const EmbeddedConversion& conversion,
    const TypePtr& source, const TypePtr& destination) {
    bool is_const = source->pointee->is_const, is_volatile = source->pointee->is_volatile;
    if (!conversion.downward)
        for (const auto* member : conversion.path.members) {
            is_const = is_const || member->type->is_const;
            is_volatile = is_volatile || member->type->is_volatile;
        }
    if ((is_const && !destination->pointee->is_const) || (is_volatile && !destination->pointee->is_volatile))
        return "pointer conversion to or from an embedded member's type discards qualifiers";
    return nullptr;
}

TypeComparison compare_source_types(const TypePtr& left, const TypePtr& right) {
    bool pending{};
    if (!same_type_impl(left, right, TypeComparisonMode::Source, pending)) return TypeComparison::Different;
    return pending ? TypeComparison::DeferredBound : TypeComparison::Same;
}

TypeComparison compare_generic_types(const TypePtr& left, const TypePtr& right) {
    bool pending{};
    if (!same_type_impl(left, right, TypeComparisonMode::Generic, pending)) return TypeComparison::Different;
    return pending ? TypeComparison::DeferredBound : TypeComparison::Same;
}

TypePtr callable_parameter_type(const TypePtr& type, ParameterMode mode) {
    if (!type || ((mode != ParameterMode::In || !type->is_const) &&
                  !type->alignment && type->alignment_requests.empty())) return type;
    auto normalized = without_alignment(type);
    if (mode == ParameterMode::In) normalized->is_const = false;
    return normalized;
}

TypePtr without_alignment(const TypePtr& type) {
    if (!type) return type;
    auto result = std::make_shared<Type>(*type);
    result->alignment = 0;
    result->alignment_requests.clear();
    return result;
}

TypePtr callable_result_type(const TypePtr& type) {
    return type && (type->alignment || !type->alignment_requests.empty())
        ? without_alignment(type) : type;
}

PointeeCompatibility compare_pointee(const TypePtr& source, const TypePtr& destination,
                                     unsigned depth, bool nested_qualification) {
    using Result = PointeeCompatibility;
    auto from = source, to = destination;
    bool immediate = depth == 0;
    bool pending{};
    const auto is_void = [](const TypePtr& type) {
        return type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void;
    };
    while (from && to) {
        if ((from->is_const && !to->is_const) ||
            (from->is_volatile && !to->is_volatile) ||
            from->is_atomic != to->is_atomic) return Result::Incompatible;
        if (!nested_qualification &&
            ((!from->is_const && to->is_const) ||
             (!from->is_volatile && to->is_volatile))) return Result::Incompatible;
        if (immediate && (is_void(from) || is_void(to)))
            return from->kind != Type::Kind::Function && to->kind != Type::Kind::Function
                ? Result::Compatible : Result::Incompatible;
        if (from->kind != to->kind) return Result::Incompatible;
        immediate = false;
        if (from->kind == Type::Kind::Pointer) {
            if (from->address_space != to->address_space) return Result::Incompatible;
            nested_qualification = nested_qualification && to->is_const;
            from = from->pointee;
            to = to->pointee;
            continue;
        }
        if (from->kind == Type::Kind::Array || from->kind == Type::Kind::Vector) {
            const auto deferred = [](const TypePtr& type) {
                return (type->kind == Type::Kind::Array && !type->lanes &&
                    type->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext) ||
                    deferred_vector_extent(type);
            };
            const bool pending_extent = deferred(from) || deferred(to);
            if (pending_extent && ((!deferred(from) && !from->lanes) ||
                (!deferred(to) && !to->lanes))) return Result::Incompatible;
            if ((!pending_extent && from->lanes != to->lanes) || from->scalable != to->scalable)
                return Result::Incompatible;
            pending = pending || pending_extent;
            from = from->element;
            to = to->element;
            continue;
        }
        auto unqualified_from = without_alignment(from);
        auto unqualified_to = without_alignment(to);
        unqualified_from->is_const = unqualified_to->is_const = false;
        unqualified_from->is_volatile = unqualified_to->is_volatile = false;
        unqualified_from->is_restrict = unqualified_to->is_restrict = false;
        switch (compare_source_types(unqualified_from, unqualified_to)) {
        case TypeComparison::Same: return pending ? Result::DeferredExtent : Result::Compatible;
        case TypeComparison::DeferredBound: return Result::DeferredExtent;
        case TypeComparison::Different: return Result::Incompatible;
        }
    }
    return Result::Incompatible;
}

bool compatible_pointee(const TypePtr& source, const TypePtr& destination,
                        unsigned depth, bool nested_qualification) {
    return compare_pointee(source, destination, depth, nested_qualification) == PointeeCompatibility::Compatible;
}

PointerJoinResult<TypePtr> common_pointer_type(const TypePtr& left, const TypePtr& right,
                                              const AddressSpaceJoin& spaces) {
    if (!left || !right) return {};
    struct Traits {
        using Type = TypePtr;
        PointerJoinNode<Type> describe(const Type& type) const {
            PointerJoinNode<Type> node;
            switch (type->kind) {
            case cross::Type::Kind::Pointer:
                node.kind = PointerJoinKind::Pointer;
                if (type->pointee) node.child = type->pointee;
                break;
            case cross::Type::Kind::Array:
                node.kind = PointerJoinKind::Array;
                if (type->element) node.child = type->element;
                break;
            case cross::Type::Kind::Vector:
                node.kind = PointerJoinKind::Vector;
                if (type->element) node.child = type->element;
                break;
            case cross::Type::Kind::Function: node.kind = PointerJoinKind::Function; break;
            case cross::Type::Kind::Builtin:
                if (type->builtin == BuiltinType::Void) node.kind = PointerJoinKind::Void;
                break;
            default: break;
            }
            node.is_const = type->is_const;
            node.is_volatile = type->is_volatile;
            node.is_atomic = type->is_atomic;
            node.is_restrict = type->is_restrict;
            node.alignment = type->alignment;
            node.address_space = type->address_space;
            node.extent = type->lanes;
            node.scalable = type->scalable;
            node.deferred_extent = !type->lanes &&
                ((type->kind == cross::Type::Kind::Array &&
                  type->array_extent_dependency == cross::Type::ArrayExtentDependency::ExpansionContext) ||
                 deferred_vector_extent(type));
            return node;
        }
        PointerJoinEquality equal_leaf(const Type& left, const Type& right) const {
            auto a = without_alignment(left), b = without_alignment(right);
            a->is_const = b->is_const = a->is_volatile = b->is_volatile = false;
            a->is_restrict = b->is_restrict = false;
            switch (compare_source_types(a, b)) {
            case TypeComparison::Same: return PointerJoinEquality::Same;
            case TypeComparison::DeferredBound: return PointerJoinEquality::Deferred;
            case TypeComparison::Different: return PointerJoinEquality::Different;
            }
            return PointerJoinEquality::Different;
        }
        Type rebuild(const Type& base, const PointerJoinNode<Type>& node) const {
            auto result = std::make_shared<cross::Type>(*base);
            result->is_const = node.is_const;
            result->is_volatile = node.is_volatile;
            result->is_atomic = node.is_atomic;
            result->is_restrict = node.is_restrict;
            if (result->alignment != node.alignment) {
                result->alignment = node.alignment;
                result->alignment_requests.clear();
            }
            result->address_space = node.address_space;
            if (node.kind == PointerJoinKind::Pointer) result->pointee = *node.child;
            else if (node.kind == PointerJoinKind::Array || node.kind == PointerJoinKind::Vector)
                result->element = *node.child;
            return result;
        }
    } traits;
    return join_pointer_types(traits, left, right, spaces);
}

TypePtr qualified_element_type(const TypePtr& container) {
    if (!container || !container->element ||
        (container->kind != Type::Kind::Array && container->kind != Type::Kind::Vector))
        return {};
    if ((!container->is_const || container->element->is_const) &&
        (!container->is_volatile || container->element->is_volatile))
        return container->element;
    auto result = std::make_shared<Type>(*container->element);
    result->is_const = result->is_const || container->is_const;
    result->is_volatile = result->is_volatile || container->is_volatile;
    return result;
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
        type->kind == Type::Kind::Buffer || type->kind == Type::Kind::TypeValue ||
        type->kind == Type::Kind::CallSite)
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

std::optional<StorageLayout> requested_storage(StorageLayout natural, std::uint64_t requested) {
    const auto alignment = std::max(natural.alignment, requested);
    const auto padding = (alignment - natural.size % alignment) % alignment;
    if (natural.size > std::numeric_limits<std::uint64_t>::max() - padding) return std::nullopt;
    return StorageLayout{natural.size + padding, alignment};
}

const Attribute* FunctionDecl::attribute(std::string_view sought) const {
    for (const auto& item : attributes) if (item.name == sought) return &item;
    return nullptr;
}

const Attribute* RecordDecl::attribute(std::string_view sought) const {
    for (const auto& item : attributes) if (item.name == sought) return &item;
    return nullptr;
}

bool FunctionDecl::has_meta_signature() const {
    if (is_meta_type(return_type)) return true;
    return std::any_of(parameters.begin(), parameters.end(),
        [](const ParameterDecl& parameter) { return is_meta_type(parameter.type); });
}

std::string encode_link_name(std::string_view qualified_name, bool label,
                             std::string_view mangling_name) {
    return encode_model_link_name(qualified_name, label, mangling_name);
}

UnbracedInitializer unbrace_initializer(const Expr& source, bool scalar, bool byte_array) {
    if (source.kind != Expr::Kind::AggregateInitializer || (!scalar && !byte_array))
        return {&source, {}, nullptr};
    const auto& entries = source.initializer_entries;
    const auto* first = entries.empty() ? nullptr : &entries.front();
    if (byte_array) {
        // A leading string initializes the whole array; nothing may follow it.
        if (!first || !first->designators.empty() || !first->value ||
            first->value->kind != Expr::Kind::String) return {&source, {}, nullptr};
        if (entries.size() > 1) return {nullptr, entries[1].location, "excess entry in u8 array initializer"};
        return {first->value.get(), {}, nullptr};
    }
    if (!first) return {nullptr, source.location, "empty scalar initializer"};
    if (!first->designators.empty()) {
        const auto& designator = first->designators.front();
        return {nullptr, designator.location, designator.kind == Expr::InitializerDesignator::Kind::Index
            ? "array designator requires an array destination"
            : "member designator requires a record destination"};
    }
    if (!first->value) return {};
    if (first->value->kind == Expr::Kind::AggregateInitializer)
        return {nullptr, first->value->location, "too many braces around scalar initializer"};
    if (entries.size() > 1) return {nullptr, entries[1].location, "excess entry in scalar initializer"};
    return {first->value.get(), {}, nullptr};
}

UnbracedInitializer unbrace_initializer(const Expr& source, const TypePtr& destination) {
    const bool byte_array = destination && destination->kind == Type::Kind::Array && destination->element &&
        destination->element->kind == Type::Kind::Builtin && destination->element->builtin == BuiltinType::U8;
    return unbrace_initializer(source, is_scalar(destination), byte_array);
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
