// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/semantic.hpp"

#include "common/uint128.hpp"
#include "common/floating_semantics.hpp"
#include "common/integer_semantics.hpp"
#include "frontend/lexer.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross {

std::span<const std::string_view> core_attribute_names() {
    static constexpr std::string_view names[] = {
        "abi", "address_space", "alias", "aligned", "always_inline",
        "atomic", "clobber", "cold", "eval_only", "ext_vector_type",
        "generic", "hot", "interrupt", "link_name", "macro", "may_alias",
        "musttail", "naked", "no_sanitize", "no_stack_protector", "noinit",
        "noinline", "noreturn", "operator", "packed", "raw_inline",
        "retain", "returns_twice", "runtime_only", "scalable_vector",
        "section", "stack_cleanup", "thread_local", "tls_model",
        "underlying", "used", "variadic", "vector_size", "visibility",
        "weak", "weakref",
    };
    return names;
}

bool is_known_attribute(std::string_view name) {
    const auto names = core_attribute_names();
    return std::find(names.begin(), names.end(), name) != names.end();
}

namespace {

using TypeSubstitutions = std::unordered_map<std::string, TypePtr>;
using ValueSubstitutions = NameMap<const Expr*>;

template <typename Visitor>
void visit_initializer_children(Expr& expression, Visitor&& visitor) {
    for (auto& entry : expression.initializer_entries) {
        for (auto& designator : entry.designators) {
            if (designator.index) visitor(designator.index);
        }
        if (entry.value) visitor(entry.value);
    }
}

template <typename Visitor>
void visit_initializer_children(const Expr& expression, Visitor&& visitor) {
    for (const auto& entry : expression.initializer_entries) {
        for (const auto& designator : entry.designators) {
            if (designator.index) visitor(*designator.index);
        }
        if (entry.value) visitor(*entry.value);
    }
}

bool validate_attribute_names(const Program& program,
                              Diagnostics& diagnostics) {
    const auto validate = [&](const std::vector<Attribute>& attributes) {
        for (const auto& attribute : attributes) {
            if (!is_known_attribute(attribute.name)) {
                diagnostics.error(attribute.location,
                                  "unknown attribute '" + attribute.name + "'");
            }
        }
    };
    for (const auto& enumeration : program.enumerations) {
        validate(enumeration.attributes);
    }
    for (const auto& record : program.records) {
        validate(record.attributes);
        for (const auto& member : record.members) {
            validate(member.attributes);
        }
    }
    for (const auto& function : program.functions) {
        validate(function->attributes);
        const auto visit_statement = [&](const auto& self,
                                         const Statement& statement) -> void {
            validate(statement.attributes);
            for (const auto& child : statement.statements) self(self, *child);
            if (statement.first) self(self, *statement.first);
            if (statement.second) self(self, *statement.second);
        };
        if (function->body) visit_statement(visit_statement, *function->body);
    }
    for (const auto& label : program.global_labels) validate(label.attributes);
    for (const auto& object : program.objects) validate(object->attributes);
    return diagnostics.errors() == 0;
}

TypePtr clone_type(const TypePtr& source,
                   const TypeSubstitutions& substitutions = {}) {
    if (!source) return {};
    TypePtr result;
    if (source->kind == Type::Kind::Builtin) {
        result = source->nominal_name.empty()
                     ? builtin_type(source->builtin)
                     : enum_type(source->nominal_name, source->builtin);
    } else if (source->kind == Type::Kind::Pointer) {
        result = pointer_type(clone_type(source->pointee, substitutions));
    } else if (source->kind == Type::Kind::Function && source->function) {
        auto parameters = source->function->parameters;
        for (auto& parameter : parameters)
            parameter.type = clone_type(parameter.type, substitutions);
        result =
            function_type(clone_type(source->function->result, substitutions),
                          std::move(parameters), source->function->variadic,
                          source->function->abi);
        result->function->result_location =
            source->function->result_location;
        result->function->clobbers = source->function->clobbers;
        result->function->stack_cleanup =
            source->function->stack_cleanup;
    } else if (source->kind == Type::Kind::Vector) {
        result = vector_type(clone_type(source->element, substitutions),
                             source->lanes, source->scalable);
    } else if (source->kind == Type::Kind::Array) {
        result = array_type(clone_type(source->element, substitutions),
                            source->lanes);
    } else if (source->kind == Type::Kind::Record) {
        result = record_type(source->nominal_name, source->is_union);
    } else if (source->kind == Type::Kind::Tokens) {
        result = tokens_type();
    } else if (source->kind == Type::Kind::Bytes) {
        result = bytes_type();
    } else if (source->kind == Type::Kind::Buffer) {
        result = buffer_type();
    } else {
        const auto found = substitutions.find(source->generic_name);
        if (found == substitutions.end()) {
            result = generic_type(source->generic_name);
        } else {
            result = clone_type(found->second);
        }
    }
    result->is_const = source->is_const;
    result->is_volatile = source->is_volatile;
    result->is_atomic = source->is_atomic;
    result->is_restrict = source->is_restrict;
    result->address_space = source->address_space;
    result->address_space_location = source->address_space_location;
    result->pending_address_space = source->pending_address_space;
    return result;
}

std::unique_ptr<Expr> clone_expr(const Expr& source,
                                 const TypeSubstitutions& types = {},
                                 const ValueSubstitutions& values = {});

std::unique_ptr<Expr> clone_expr(const Expr& source,
                                 const TypeSubstitutions& types,
                                 const ValueSubstitutions& values) {
    if (source.kind == Expr::Kind::Name) {
        const auto found = values.find(name_key(source));
        if (found != values.end()) return clone_expr(*found->second, types, {});
    }
    auto result = std::make_unique<Expr>();
    result->kind = source.kind;
    result->location = source.location;
    result->text = source.text;
    result->name_context = source.name_context;
    result->string_value = source.string_value;
    result->quote_fragments = source.quote_fragments;
    result->evaluated_integer = source.evaluated_integer;
    result->evaluated_floating = source.evaluated_floating;
    result->evaluated_address = source.evaluated_address;
    result->generic_visible_at_call = source.generic_visible_at_call;
    if (source.type) result->type = clone_type(source.type, types);
    if (source.left) result->left = clone_expr(*source.left, types, values);
    if (source.right) result->right = clone_expr(*source.right, types, values);
    if (source.third) result->third = clone_expr(*source.third, types, values);
    for (const auto& argument : source.arguments) {
        result->arguments.push_back(clone_expr(*argument, types, values));
    }
    for (const auto& argument : source.generic_arguments) {
        Expr::GenericArgument copy;
        if (argument.type) copy.type = clone_type(argument.type, types);
        if (argument.value) copy.value = clone_expr(*argument.value, types, values);
        result->generic_arguments.push_back(std::move(copy));
    }
    for (const auto& entry : source.initializer_entries) {
        Expr::InitializerEntry copy;
        copy.location = entry.location;
        for (const auto& designator : entry.designators) {
            Expr::InitializerDesignator designator_copy;
            designator_copy.kind = designator.kind;
            designator_copy.location = designator.location;
            designator_copy.member = designator.member;
            if (designator.index) {
                designator_copy.index =
                    clone_expr(*designator.index, types, values);
            }
            copy.designators.push_back(std::move(designator_copy));
        }
        if (entry.value) copy.value = clone_expr(*entry.value, types, values);
        result->initializer_entries.push_back(std::move(copy));
    }
    return result;
}

std::vector<Attribute> clone_attributes(
    const std::vector<Attribute>& source, const TypeSubstitutions& types,
    const ValueSubstitutions& values) {
    auto result = source;
    for (auto& attribute : result) {
        if (attribute.expression_argument) {
            attribute.expression_argument = std::shared_ptr<Expr>(
                clone_expr(*attribute.expression_argument, types, values));
        }
    }
    return result;
}

std::unique_ptr<VariableDecl> clone_variable(
    const VariableDecl& source, const TypeSubstitutions& types,
    const ValueSubstitutions& values) {
    auto result = std::make_unique<VariableDecl>();
    result->location = source.location;
    result->name = source.name;
    result->type = clone_type(source.type, types);
    if (source.dynamic_array_bound) {
        result->dynamic_array_bound =
            clone_expr(*source.dynamic_array_bound, types, values);
    }
    if (source.initializer) {
        result->initializer = clone_expr(*source.initializer, types, values);
    }
    result->storage_register = source.storage_register;
    result->storage_stack = source.storage_stack;
    result->storage_static = source.storage_static;
    result->attributes = clone_attributes(source.attributes, types, values);
    result->explicit_alignment = source.explicit_alignment;
    result->location_name = source.location_name;
    return result;
}

std::unique_ptr<Statement> clone_statement(
    const Statement& source, const TypeSubstitutions& types = {},
    const ValueSubstitutions& values = {}) {
    auto result = std::make_unique<Statement>();
    result->kind = source.kind;
    result->location = source.location;
    result->label_name = source.label_name;
    result->attributes = source.attributes;
    result->global_label = source.global_label;
    for (const auto& child : source.statements) {
        result->statements.push_back(clone_statement(*child, types, values));
    }
    if (source.declaration) {
        result->declaration = clone_variable(*source.declaration, types, values);
    }
    if (source.expression) {
        result->expression = clone_expr(*source.expression, types, values);
    }
    if (source.condition) {
        result->condition = clone_expr(*source.condition, types, values);
    }
    if (source.increment) {
        result->increment = clone_expr(*source.increment, types, values);
    }
    if (source.first) result->first = clone_statement(*source.first, types, values);
    if (source.second) result->second = clone_statement(*source.second, types, values);
    return result;
}

std::string namespace_prefix(std::string_view name) {
    const auto separator = name.rfind("::");
    return separator == std::string_view::npos
               ? std::string{}
               : std::string(name.substr(0, separator));
}

std::string lookup_source_unit(NameUse name, const FunctionDecl* caller) {
    if (name.location.file) return name.location.file->source_unit_at(name.location.line);
    return caller ? caller->source_unit : std::string{};
}

std::optional<std::string> value_namespace(
    const Program& program, const FunctionDecl* caller, NameUse name,
    std::string_view fallback_namespace = {}) {
    const auto unit = lookup_source_unit(name, caller);
    const auto visible = [&](const auto& declaration) {
        return declaration.linkage != Linkage::Static || unit.empty() ||
               declaration.source_unit == unit;
    };
    const auto candidates = namespace_candidates(
        name, fallback_namespace.empty() && caller ? caller->source_namespace : fallback_namespace,
        caller ? caller->imports : std::vector<std::string>{});
    for (const auto& candidate : candidates) {
        for (const auto& object : program.objects)
            if (object->name == candidate && visible(*object)) return candidate;
        for (const auto& function : program.functions)
            if (function->name == candidate && visible(*function)) return candidate;
        for (const auto& enumeration : program.enumerations)
            for (const auto& enumerator : enumeration.enumerators)
                if (enumerator.name == candidate) return candidate;
    }
    return std::nullopt;
}

const ObjectDecl* resolve_object(const Program& program,
                                const FunctionDecl* caller, NameUse name);

template <typename Predicate>
FunctionDecl* resolve_function(Program& program, const FunctionDecl* caller,
                               NameUse name, Predicate predicate) {
    const auto selected = value_namespace(program, caller, name);
    if (!selected) return nullptr;
    const auto unit = lookup_source_unit(name, caller);
    const auto exact = [&](std::string_view qualified) -> FunctionDecl* {
        FunctionDecl* shared = nullptr;
        for (const auto& candidate : program.functions) {
            if (candidate->name != qualified || !predicate(*candidate)) continue;
            if (candidate->linkage == Linkage::Static) {
                if (unit.empty() || candidate->source_unit == unit)
                    return candidate.get();
            } else if (!shared) {
                shared = candidate.get();
            }
        }
        return shared;
    };
    return exact(*selected);
}

std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const char character : text) {
        value ^= static_cast<unsigned char>(character);
        value *= 1099511628211ULL;
    }
    return value;
}

std::string_view parameter_mode_name(ParameterMode mode) {
    switch (mode) {
    case ParameterMode::In: return "in";
    case ParameterMode::Out: return "out";
    case ParameterMode::InOut: return "inout";
    }
    return {};
}

std::vector<ManglingArgument> generic_argument_descriptors(
    const std::vector<Expr::GenericArgument>& arguments) {
    std::vector<ManglingArgument> rendered;
    rendered.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument.type) {
            rendered.push_back(
                {ManglingArgument::Kind::Type,
                 canonical_type_name(argument.type)});
        } else {
            std::string spelling = argument.value ? argument.value->text : "<invalid>";
            if (argument.value && argument.value->evaluated_address) {
                const auto& address = *argument.value->evaluated_address;
                spelling = canonical_type_name(argument.value->type) + "=";
                if (address.kind == AddressConstant::Kind::Absolute) {
                    spelling += to_decimal(address.absolute);
                } else {
                    const auto& name = address.object ? address.object->name
                                                      : address.function->name;
                    const auto& unit = address.object ? address.object->source_unit
                                                      : address.function->source_unit;
                    const auto linkage = address.object ? address.object->linkage
                                                         : address.function->linkage;
                    spelling += '&';
                    if (linkage == Linkage::Static)
                        spelling += std::to_string(stable_hash(unit)) + ':';
                    spelling += name + (address.addend < 0 ? "" : "+") +
                                std::to_string(address.addend);
                }
            }
            if (argument.value && argument.value->type &&
                !argument.value->type->nominal_name.empty()) {
                spelling = canonical_type_name(argument.value->type) + "=" + spelling;
            }
            rendered.push_back(
                {ManglingArgument::Kind::Value, std::move(spelling)});
        }
    }
    return rendered;
}

std::string generic_link_name(const FunctionDecl& function,
                              const std::vector<Expr::GenericArgument>& arguments,
                              std::string_view mangling) {
    const auto rendered = generic_argument_descriptors(arguments);
    std::vector<ManglingParameter> parameters;
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        parameters.push_back(
            {.spelling = canonical_type_name(
                 callable_parameter_type(parameter.type, parameter.mode)),
             .mode = std::string(parameter_mode_name(parameter.mode))});
    }
    return encode_model_generic_link_name(
        {.qualified_name = function.name,
         .kind = "function",
         .result = canonical_type_name(function.return_type),
         .parameters = parameters,
         .variadic = function.variadic},
        rendered, mangling);
}

struct GenericArgumentKey {
    enum class Kind { Type, Integer, Address, Label } kind{Kind::Type};
    TypePtr type;
    UInt128 integer;
    AddressConstant address;
    std::string label;

    bool operator==(const GenericArgumentKey& other) const {
        return kind == other.kind &&
            ((!type && !other.type) || same_type(type, other.type)) &&
            integer == other.integer && address == other.address && label == other.label;
    }
};

std::vector<GenericArgumentKey> generic_argument_keys(
    const std::vector<Expr::GenericArgument>& arguments) {
    std::vector<GenericArgumentKey> result;
    for (const auto& argument : arguments) {
        GenericArgumentKey key;
        key.type = argument.type;
        if (argument.value) {
            key.type = argument.value->type;
            if (argument.value->evaluated_address) {
                key.kind = GenericArgumentKey::Kind::Address;
                key.address = *argument.value->evaluated_address;
            } else if (argument.value->evaluated_integer) {
                key.kind = GenericArgumentKey::Kind::Integer;
                key.integer = argument.value->evaluated_integer->value;
                if (!key.type) key.type = builtin_type(argument.value->evaluated_integer->type);
            } else {
                key.kind = GenericArgumentKey::Kind::Label;
                key.label = argument.value->text;
            }
        }
        result.push_back(std::move(key));
    }
    return result;
}

struct GenericExpansionState {
    struct Instance {
        const FunctionDecl* generic{};
        std::vector<GenericArgumentKey> arguments;
        std::string name;
    };
    std::vector<Instance> instances;
    std::unordered_set<const FunctionDecl*> rewritten_functions;
    unsigned depth{};
    GenericPointerResolver pointer_resolver;
    GenericAbiCanonicalizer canonical_abi;
    std::vector<NameKey> locals;
    std::vector<std::pair<NameKey, TypePtr>> local_types;
};

bool normalize_generic_callable_abis(TypePtr& type,
                                     const GenericExpansionState& state,
                                     Diagnostics& diagnostics,
                                     SourceLocation location) {
    if (!type) return true;
    if (type->kind == Type::Kind::Pointer)
        return normalize_generic_callable_abis(type->pointee, state,
                                               diagnostics, location);
    if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector)
        return normalize_generic_callable_abis(type->element, state,
                                               diagnostics, location);
    if (type->kind != Type::Kind::Function || !type->function) return true;
    if (state.canonical_abi) {
        auto resolved = state.canonical_abi(type->function->abi);
        if (!resolved) {
            diagnostics.error(location,
                              "unknown callable ABI '" +
                                  type->function->abi + "'");
            return false;
        }
        type->function->abi = std::move(*resolved);
    }
    if (!normalize_generic_callable_abis(type->function->result, state,
                                         diagnostics, location)) return false;
    for (auto& parameter : type->function->parameters)
        if (!normalize_generic_callable_abis(parameter.type, state,
                                             diagnostics, location)) return false;
    return true;
}

TypePtr generic_declaration_type(const FunctionDecl& function,
                                 const TypeSubstitutions& type_names) {
    auto parameters = function.parameters;
    for (auto& parameter : parameters)
        parameter.type = clone_type(parameter.type, type_names);
    auto type = function_type(clone_type(function.return_type, type_names),
                              std::move(parameters), function.variadic);
    type->function->result_location = function.result_location;
    if (const auto* abi = function.attribute("abi"); abi &&
        abi->arguments.size() == 1)
        type->function->abi =
            decode_string_literal(abi->arguments.front()).value_or("");
    if (const auto* cleanup = function.attribute("stack_cleanup"); cleanup &&
        cleanup->arguments.size() == 1)
        type->function->stack_cleanup =
            decode_string_literal(cleanup->arguments.front());
    for (const auto& attribute : function.attributes) {
        if (attribute.name != "clobber") continue;
        for (const auto& argument : attribute.arguments)
            if (const auto resource = decode_string_literal(argument))
                type->function->clobbers.push_back(*resource);
    }
    return type;
}

bool compatible_generic_declarations(
    const FunctionDecl& left, const FunctionDecl& right,
    const GenericExpansionState& state, Diagnostics& diagnostics) {
    if (left.linkage != right.linkage ||
        left.generic_parameters.size() != right.generic_parameters.size())
        return false;
    TypeSubstitutions left_names;
    TypeSubstitutions right_names;
    for (std::size_t index = 0; index < left.generic_parameters.size();
         ++index) {
        const auto& a = left.generic_parameters[index];
        const auto& b = right.generic_parameters[index];
        if (static_cast<bool>(a.value_type) !=
            static_cast<bool>(b.value_type)) return false;
        if (!a.value_type) {
            const auto placeholder = generic_type(
                "__generic_parameter_" + std::to_string(index));
            left_names.emplace(a.name, placeholder);
            right_names.emplace(b.name, placeholder);
        }
    }
    for (std::size_t index = 0; index < left.generic_parameters.size();
         ++index) {
        const auto& a = left.generic_parameters[index];
        const auto& b = right.generic_parameters[index];
        if (!a.value_type) continue;
        auto a_type = clone_type(a.value_type, left_names);
        auto b_type = clone_type(b.value_type, right_names);
        if (!normalize_generic_callable_abis(a_type, state, diagnostics,
                                             a.location) ||
            !normalize_generic_callable_abis(b_type, state, diagnostics,
                                             b.location) ||
            !same_type(a_type, b_type)) return false;
    }
    auto a_type = generic_declaration_type(left, left_names);
    auto b_type = generic_declaration_type(right, right_names);
    return normalize_generic_callable_abis(a_type, state, diagnostics,
                                           left.location) &&
           normalize_generic_callable_abis(b_type, state, diagnostics,
                                           right.location) &&
           same_type(a_type, b_type);
}

bool validate_generic_redeclarations(const Program& program,
                                     const GenericExpansionState& state,
                                     Diagnostics& diagnostics) {
    for (std::size_t left = 0; left < program.functions.size(); ++left) {
        const auto& a = *program.functions[left];
        if (a.generic_parameters.empty()) continue;
        for (std::size_t right = left + 1;
             right < program.functions.size(); ++right) {
            const auto& b = *program.functions[right];
            if (b.generic_parameters.empty() || a.name != b.name) continue;
            if ((a.linkage == Linkage::Static ||
                 b.linkage == Linkage::Static) &&
                a.source_unit != b.source_unit) continue;
            if (a.body && b.body) {
                diagnostics.error(b.location,
                                  "duplicate definition of generic function '" +
                                      b.name + "'");
            } else if (!compatible_generic_declarations(
                           a, b, state, diagnostics)) {
                diagnostics.error(b.location,
                                  "generic declarations of '" + b.name +
                                      "' have incompatible interfaces");
            }
        }
    }
    return diagnostics.errors() == 0;
}

TypePtr infer_generic_actual(const Expr& expression,
                             const FunctionDecl* caller,
                             Program& program,
                             const GenericExpansionState& state,
                             bool decay = true);

bool match_deduced_type(const TypePtr& formal, const TypePtr& actual,
                        TypeSubstitutions& bindings,
                        const std::unordered_set<std::string>& type_parameters,
                        std::string& conflict) {
    if (!formal || !actual) return false;
    if (formal->kind == Type::Kind::Generic &&
        type_parameters.contains(formal->generic_name)) {
        const auto found = bindings.find(formal->generic_name);
        if (found != bindings.end()) {
            if (!same_type(found->second, actual)) {
                conflict = formal->generic_name;
                return false;
            }
            return true;
        }
        bindings.emplace(formal->generic_name, clone_type(actual));
        return true;
    }
    const auto has_unbound = [&](const auto& self, const TypePtr& type)
                                 -> bool {
        if (!type) return false;
        if (type->kind == Type::Kind::Generic)
            return type_parameters.contains(type->generic_name);
        if (type->kind == Type::Kind::Pointer)
            return self(self, type->pointee);
        if (type->kind == Type::Kind::Array ||
            type->kind == Type::Kind::Vector)
            return self(self, type->element);
        if (type->kind == Type::Kind::Function && type->function) {
            if (self(self, type->function->result)) return true;
            for (const auto& parameter : type->function->parameters)
                if (self(self, parameter.type)) return true;
        }
        return false;
    };
    if (!has_unbound(has_unbound, formal)) return true;
    if (formal->kind != actual->kind ||
        formal->is_const != actual->is_const ||
        formal->is_volatile != actual->is_volatile ||
        formal->is_atomic != actual->is_atomic ||
        formal->is_restrict != actual->is_restrict) return false;
    if (formal->kind == Type::Kind::Pointer)
        return formal->address_space == actual->address_space &&
               match_deduced_type(formal->pointee, actual->pointee,
                                  bindings, type_parameters, conflict);
    if (formal->kind == Type::Kind::Array ||
        formal->kind == Type::Kind::Vector)
        return formal->lanes == actual->lanes &&
               formal->scalable == actual->scalable &&
               match_deduced_type(formal->element, actual->element,
                                  bindings, type_parameters, conflict);
    if (formal->kind == Type::Kind::Function && formal->function &&
        actual->function) {
        const auto& left = *formal->function;
        const auto& right = *actual->function;
        auto left_clobbers = left.clobbers;
        auto right_clobbers = right.clobbers;
        std::sort(left_clobbers.begin(), left_clobbers.end());
        std::sort(right_clobbers.begin(), right_clobbers.end());
        if (left.abi != right.abi || left.variadic != right.variadic ||
            left.result_location.value_or("auto") !=
                right.result_location.value_or("auto") ||
            left.stack_cleanup.value_or("caller") !=
                right.stack_cleanup.value_or("caller") ||
            left_clobbers != right_clobbers ||
            left.parameters.size() != right.parameters.size()) return false;
        if (!match_deduced_type(left.result, right.result, bindings,
                                type_parameters, conflict)) return false;
        for (std::size_t index = 0; index < left.parameters.size(); ++index) {
            if (left.parameters[index].mode != right.parameters[index].mode ||
                left.parameters[index].location_name.value_or("auto") !=
                    right.parameters[index].location_name.value_or("auto") ||
                !match_deduced_type(
                    callable_parameter_type(left.parameters[index].type,
                                            left.parameters[index].mode),
                    callable_parameter_type(right.parameters[index].type,
                                            right.parameters[index].mode),
                    bindings, type_parameters, conflict)) return false;
        }
        return true;
    }
    return same_type(formal, actual);
}

bool deduce_generic_arguments(const FunctionDecl& generic,
                              const Expr& call, const FunctionDecl* caller,
                              Program& program,
                              const GenericExpansionState& state,
                              std::vector<Expr::GenericArgument>& arguments,
                              Diagnostics& diagnostics) {
    if (arguments.size() > generic.generic_parameters.size()) {
        diagnostics.error(call.location, "generic argument count does not match '" +
                                             generic.name + "'");
        return false;
    }
    std::unordered_set<std::string> type_parameters;
    TypeSubstitutions bindings;
    for (const auto& parameter : generic.generic_parameters)
        if (!parameter.value_type) type_parameters.insert(parameter.name);
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& parameter = generic.generic_parameters[index];
        if (parameter.value_type) {
            if (!arguments[index].value) {
                diagnostics.error(call.location,
                                  "generic value parameter '" + parameter.name +
                                      "' requires a value argument");
                return false;
            }
        } else if (arguments[index].type) {
            arguments[index].type = clone_type(arguments[index].type);
            if (!normalize_generic_callable_abis(arguments[index].type,
                                                 state, diagnostics,
                                                 call.location)) return false;
            bindings.emplace(parameter.name, arguments[index].type);
            type_parameters.erase(parameter.name);
        } else {
            diagnostics.error(call.location,
                              "generic type parameter '" + parameter.name +
                                  "' requires a type argument");
            return false;
        }
    }
    if (call.arguments.size() < generic.parameters.size()) {
        diagnostics.error(call.location,
                          "generic call has too few fixed arguments for deduction");
        return false;
    }
    for (std::size_t index = 0; index < generic.parameters.size(); ++index) {
        const auto& parameter = generic.parameters[index];
        const auto& argument = *call.arguments[index];
        auto actual = infer_generic_actual(
            argument, caller, program, state,
            parameter.mode == ParameterMode::In);
        if (!actual) {
            diagnostics.error(argument.location,
                              "cannot determine generic call argument type");
            return false;
        }
        actual = clone_type(actual);
        auto formal = clone_type(
            callable_parameter_type(parameter.type, parameter.mode));
        if (parameter.mode == ParameterMode::In)
            actual = callable_parameter_type(actual, ParameterMode::In);
        if (!normalize_generic_callable_abis(formal, state, diagnostics,
                                             parameter.location) ||
            !normalize_generic_callable_abis(actual, state, diagnostics,
                                             argument.location)) return false;
        std::string conflict;
        if (!match_deduced_type(formal, actual, bindings,
                                type_parameters, conflict)) {
            diagnostics.error(argument.location,
                conflict.empty()
                    ? "generic type deduction requires an exact structural match"
                    : "conflicting deductions for generic type parameter '" +
                          conflict + "'");
            return false;
        }
    }
    std::vector<Expr::GenericArgument> complete(
        generic.generic_parameters.size());
    for (std::size_t index = 0; index < arguments.size(); ++index)
        complete[index] = std::move(arguments[index]);
    for (std::size_t index = arguments.size();
         index < generic.generic_parameters.size(); ++index) {
        const auto& parameter = generic.generic_parameters[index];
        if (parameter.value_type) {
            diagnostics.error(call.location,
                              "generic value parameter '" + parameter.name +
                                  "' requires an explicit argument");
            return false;
        }
        const auto found = bindings.find(parameter.name);
        if (found == bindings.end()) {
            diagnostics.error(call.location,
                              "could not deduce generic type parameter '" +
                                  parameter.name + "'");
            return false;
        }
        complete[index].type = found->second;
    }
    arguments = std::move(complete);
    return true;
}

void lift_static_locals(Program& program, FunctionDecl& function);
void lift_pointer_argument_strings(Program& program, std::unique_ptr<Expr>& expression,
                                   const FunctionDecl* caller);

bool normalize_generic_arguments(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location,
                                 const GenericExpansionState& state);

void rewrite_generic_expr(std::unique_ptr<Expr>& expression,
                          FunctionDecl* caller, Program& program,
                          Diagnostics& diagnostics,
                          GenericExpansionState& state,
                          std::string_view mangling);

void rewrite_generic_statement(
    Statement& statement, FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics,
    GenericExpansionState& state,
    std::string_view mangling) {
    const auto saved_locals = state.locals.size();
    const auto saved_local_types = state.local_types.size();
    const bool scoped = statement.kind == Statement::Kind::Compound ||
                        statement.kind == Statement::Kind::For;
    if (statement.kind == Statement::Kind::For && statement.first) {
        rewrite_generic_statement(*statement.first, caller, program, diagnostics,
                                  state, mangling);
    }
    for (auto& child : statement.statements) {
        rewrite_generic_statement(*child, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.declaration) {
        state.locals.push_back(name_key(*statement.declaration));
        state.local_types.emplace_back(name_key(*statement.declaration),
                                       statement.declaration->type);
        if (statement.declaration->dynamic_array_bound) {
            rewrite_generic_expr(statement.declaration->dynamic_array_bound,
                                 caller, program, diagnostics, state, mangling);
        }
        if (statement.declaration->initializer) {
            rewrite_generic_expr(statement.declaration->initializer, caller,
                                 program, diagnostics, state, mangling);
        }
    }
    if (statement.expression) {
        rewrite_generic_expr(statement.expression, caller, program, diagnostics,
                             state, mangling);
    }
    if (statement.condition) {
        rewrite_generic_expr(statement.condition, caller, program, diagnostics,
                             state, mangling);
    }
    if (statement.increment) {
        rewrite_generic_expr(statement.increment, caller, program, diagnostics,
                             state, mangling);
    }
    if (statement.first && statement.kind != Statement::Kind::For) {
        rewrite_generic_statement(*statement.first, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.second) {
        rewrite_generic_statement(*statement.second, caller, program, diagnostics,
                                  state, mangling);
    }
    if (scoped) {
        state.locals.resize(saved_locals);
        state.local_types.resize(saved_local_types);
    }
}

void rewrite_generic_function(FunctionDecl& function, Program& program,
                              Diagnostics& diagnostics,
                              GenericExpansionState& state,
                              std::string_view mangling) {
    if (!function.body || !function.generic_parameters.empty() ||
        !state.rewritten_functions.insert(&function).second) return;
    lift_static_locals(program, function);
    auto saved_locals = std::move(state.locals);
    auto saved_local_types = std::move(state.local_types);
    state.locals.clear();
    state.local_types.clear();
    for (const auto& parameter : function.parameters) {
        state.locals.push_back(name_key(parameter));
        state.local_types.emplace_back(name_key(parameter), parameter.type);
    }
    rewrite_generic_statement(*function.body, &function, program, diagnostics,
                              state, mangling);
    state.locals = std::move(saved_locals);
    state.local_types = std::move(saved_local_types);
}

std::unique_ptr<FunctionDecl> instantiate(
    const FunctionDecl& source, const std::vector<Expr::GenericArgument>& arguments,
    std::string internal_name, Diagnostics& diagnostics) {
    if (source.generic_parameters.size() != arguments.size()) {
        diagnostics.error(source.location,
                          "generic argument count does not match '" + source.name + "'");
        return {};
    }
    TypeSubstitutions types;
    ValueSubstitutions values;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& parameter = source.generic_parameters[index];
        const auto& argument = arguments[index];
        if (!parameter.value_type) {
            if (!argument.type) {
                diagnostics.error(source.location,
                                  "generic type parameter '" + parameter.name +
                                      "' requires a type argument");
                return {};
            }
            types.emplace(parameter.name, argument.type);
        } else {
            if (!argument.value ||
                (argument.value->kind != Expr::Kind::Integer &&
                 argument.value->kind != Expr::Kind::Address &&
                 !(argument.value->type &&
                   argument.value->type->kind == Type::Kind::Builtin &&
                   argument.value->type->builtin == BuiltinType::Label &&
                   argument.value->kind == Expr::Kind::Name))) {
                diagnostics.error(source.location,
                                  "generic value parameter '" + parameter.name +
                                      "' requires a normalized constant argument");
                return {};
            }
            values.emplace(name_key(parameter), argument.value.get());
        }
    }

    auto result = std::make_unique<FunctionDecl>();
    result->location = source.location;
    result->name = std::move(internal_name);
    result->source_namespace = source.source_namespace;
    result->source_unit = source.source_unit;
    result->imports = source.imports;
    result->return_type = clone_type(source.return_type, types);
    for (const auto& parameter : source.parameters) {
        result->parameters.push_back(
            {parameter.location, parameter.name, clone_type(parameter.type, types),
             parameter.mode, parameter.explicit_mode, parameter.location_name});
    }
    for (const auto& attribute : source.attributes) {
        if (attribute.name != "generic") {
            auto copy = attribute;
            if (attribute.expression_argument) {
                copy.expression_argument = std::shared_ptr<Expr>(
                    clone_expr(*attribute.expression_argument, types, values));
            }
            result->attributes.push_back(std::move(copy));
        }
    }
    result->result_location = source.result_location;
    for (const auto& assertion : source.deferred_static_assertions) {
        result->deferred_static_assertions.push_back(
            {assertion.location, assertion.source_namespace,
             assertion.condition
                 ? clone_expr(*assertion.condition, types, values)
                 : std::unique_ptr<Expr>{},
             assertion.message});
    }
    if (source.body) result->body = clone_statement(*source.body, types, values);
    result->linkage = source.linkage;
    result->variadic = source.variadic;
    result->inline_hint = source.inline_hint;
    return result;
}

void rewrite_generic_expr(std::unique_ptr<Expr>& expression,
                          FunctionDecl* caller, Program& program,
                          Diagnostics& diagnostics,
                          GenericExpansionState& state,
                          std::string_view mangling) {
    if (!expression) return;
    if (expression->left) {
        rewrite_generic_expr(expression->left, caller, program, diagnostics,
                             state, mangling);
    }
    if (expression->right) {
        rewrite_generic_expr(expression->right, caller, program, diagnostics,
                             state, mangling);
    }
    if (expression->third) {
        rewrite_generic_expr(expression->third, caller, program, diagnostics,
                             state, mangling);
    }
    for (auto& argument : expression->arguments) {
        rewrite_generic_expr(argument, caller, program, diagnostics,
                             state, mangling);
    }
    for (auto& argument : expression->generic_arguments) {
        if (argument.value) {
            rewrite_generic_expr(argument.value, caller, program, diagnostics,
                                 state, mangling);
        }
    }
    visit_initializer_children(*expression, [&](std::unique_ptr<Expr>& child) {
        rewrite_generic_expr(child, caller, program, diagnostics, state,
                             mangling);
    });
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        return;
    }
    if (std::find(state.locals.begin(), state.locals.end(),
                  name_key(*expression->left)) != state.locals.end())
        return;
    const auto name = expression->left->text;
    auto* declared_generic = resolve_function(
        program, caller, *expression->left,
        [](const FunctionDecl& candidate) {
            return !candidate.generic_parameters.empty();
        });
    auto* generic = resolve_function(
        program, caller, *expression->left,
        [](const FunctionDecl& candidate) {
            return !candidate.generic_parameters.empty() &&
                   candidate.body != nullptr;
        });
    if (!generic && !declared_generic &&
        expression->generic_arguments.empty()) {
        if (auto* function = resolve_function(
                       program, caller, *expression->left,
                       [](const FunctionDecl& candidate) { return candidate.body != nullptr; })) {
            // An ordinary helper used by a generic constant may itself call
            // generics. Prepare its definition before any evaluator enters it,
            // regardless of declaration order. The visited set breaks cycles.
            rewrite_generic_function(*function, program, diagnostics, state, mangling);
        }
        return;
    }
    if (!generic) {
        diagnostics.error(
            expression->location,
            declared_generic
                ? "definition of generic function '" + name +
                      "' is not visible in this compilation group"
                : "generic arguments applied to non-generic function '" +
                      name + "'");
        return;
    }
    if (!expression->generic_visible_at_call) {
        diagnostics.error(expression->location,
                          "generic function '" + name +
                              "' must be declared before use");
        return;
    }
    if (!deduce_generic_arguments(*generic, *expression, caller, program,
                                  state, expression->generic_arguments,
                                  diagnostics)) return;
    if (!normalize_generic_arguments(*generic, expression->generic_arguments,
                                     caller, program, diagnostics, expression->location,
                                     state)) return;
    const auto link_name = generic_link_name(
        *generic, expression->generic_arguments, mangling);
    // A user mangler controls external spelling, never semantic equivalence.
    std::string identity;
    const auto append_identity = [&](std::string_view text) {
        identity += std::to_string(text.size()) + ':' + std::string(text);
    };
    append_identity(generic->name);
    append_identity(generic->linkage == Linkage::Static ? generic->source_unit : "");
    for (const auto& argument : generic_argument_descriptors(expression->generic_arguments)) {
        identity += argument.kind == ManglingArgument::Kind::Type ? 'T' : 'V';
        append_identity(argument.spelling);
    }
    auto keys = generic_argument_keys(expression->generic_arguments);
    const auto found = std::find_if(state.instances.begin(), state.instances.end(),
        [&](const GenericExpansionState::Instance& instance) {
            return instance.generic == generic && instance.arguments == keys;
        });
    std::string internal_name;
    if (found != state.instances.end()) {
        internal_name = found->name;
    } else {
        if (state.depth >= 128 || state.instances.size() >= 4096) {
            diagnostics.error(expression->location, "generic instantiation budget exceeded");
            return;
        }
        internal_name = generic->name + "$G" +
                        std::to_string(stable_hash(identity));
        while (std::any_of(state.instances.begin(), state.instances.end(),
                          [&](const auto& instance) { return instance.name == internal_name; }))
            internal_name += '$';
        auto instance = instantiate(*generic, expression->generic_arguments,
                                    internal_name, diagnostics);
        if (!instance) return;
        if (instance->linkage == Linkage::Global && !instance->attribute("link_name")) {
            instance->attributes.push_back(
                {"link_name", {'"' + link_name + '"'}, instance->location});
        }
        state.instances.push_back({generic, std::move(keys), internal_name});
        auto* concrete = instance.get();
        program.functions.push_back(std::move(instance));
        for (auto& assertion : concrete->deferred_static_assertions)
            program.static_assertions.push_back(std::move(assertion));
        concrete->deferred_static_assertions.clear();
        // Publish before walking the body so recursive identical instances
        // resolve to the in-progress function. Nested constant generic calls
        // can then be evaluated through the same visible definition table.
        if (concrete->body) {
            ++state.depth;
            rewrite_generic_function(*concrete, program, diagnostics, state, mangling);
            --state.depth;
        }
    }
    bind_exact_name(*expression->left, std::move(internal_name));
    expression->generic_arguments.clear();
}

bool expand_generics(Program& program, Diagnostics& diagnostics,
                     std::string_view mangling,
                     const GenericPointerResolver& pointer_resolver,
                     const GenericAbiCanonicalizer& canonical_abi) {
    GenericExpansionState state;
    state.pointer_resolver = pointer_resolver;
    state.canonical_abi = canonical_abi;
    if (!validate_generic_redeclarations(program, state, diagnostics))
        return false;
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (!function->generic_parameters.empty()) continue;
        rewrite_generic_function(*function, program, diagnostics, state, mangling);
    }
    for (std::size_t index = 0; index < program.objects.size(); ++index) {
        auto* object = program.objects[index].get();
        if (object->initializer) {
            FunctionDecl context;
            context.name = object->name;
            context.source_namespace = namespace_prefix(object->name);
            context.source_unit = object->source_unit;
            rewrite_generic_expr(object->initializer, &context, program,
                                 diagnostics, state, mangling);
        }
    }
    program.functions.erase(
        std::remove_if(program.functions.begin(), program.functions.end(),
                       [](const auto& function) {
                           return !function->generic_parameters.empty();
                       }),
        program.functions.end());
    return diagnostics.errors() == 0;
}

class StaticLocalLifter {
public:
    explicit StaticLocalLifter(Program& program) : program_(program) {}

    void run(FunctionDecl& function) {
        if (!function.body) return;
        function_ = &function;
        scopes_.emplace_back();
        for (const auto& parameter : function.parameters) {
            scopes_.back()[name_key(parameter)] = {};
        }
        rewrite(*function.body);
    }

private:
    const std::string* replacement(std::string_view name, SourceLocation location) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(NameKey(name, location));
            if (found == scope->end()) continue;
            return found->second.empty() ? nullptr : &found->second;
        }
        return nullptr;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name) {
            if (const auto* name = replacement(expression->text, expression->location)) {
                bind_exact_name(*expression, *name);
            }
        }
        rewrite(expression->left);
        rewrite(expression->right);
        rewrite(expression->third);
        for (auto& argument : expression->arguments) rewrite(argument);
        for (auto& argument : expression->generic_arguments) {
            rewrite(argument.value);
        }
        visit_initializer_children(
            *expression,
            [&](std::unique_ptr<Expr>& child) { rewrite(child); });
    }

    void rewrite(Statement& statement) {
        const bool scoped = statement.kind == Statement::Kind::Compound ||
                            statement.kind == Statement::Kind::For;
        if (scoped) scopes_.emplace_back();
        if (statement.kind == Statement::Kind::For && statement.first)
            rewrite(*statement.first);
        if (statement.declaration) {
            auto& declaration = *statement.declaration;
            if (declaration.storage_static) {
                const auto local_name = declaration.name;
                const auto lifted_name =
                    function_->name + "::$static" +
                    std::to_string(ordinal_++) + "::" + local_name;
                scopes_.back()[name_key(declaration)] = lifted_name;
                rewrite(declaration.initializer);
                auto object = std::make_unique<ObjectDecl>();
                object->location = declaration.location;
                object->name = lifted_name;
                object->source_unit = function_->source_unit;
                object->type = std::move(declaration.type);
                object->initializer = std::move(declaration.initializer);
                object->linkage = Linkage::Static;
                object->attributes = std::move(declaration.attributes);
                program_.objects.push_back(std::move(object));
                statement.declaration.reset();
                statement.kind = Statement::Kind::Empty;
            } else {
                rewrite(declaration.dynamic_array_bound);
                rewrite(declaration.initializer);
                scopes_.back()[name_key(declaration)] = {};
            }
        }
        rewrite(statement.expression);
        rewrite(statement.condition);
        rewrite(statement.increment);
        if (statement.first && statement.kind != Statement::Kind::For)
            rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
        if (scoped) scopes_.pop_back();
    }

    Program& program_;
    FunctionDecl* function_{};
    unsigned ordinal_{};
    std::vector<NameMap<std::string>> scopes_;
};

void lift_static_locals(Program& program, FunctionDecl& function) {
    StaticLocalLifter(program).run(function);
}

class FunctionPointerAdapterLifter {
public:
    FunctionPointerAdapterLifter(Program& program,
                                 std::string_view default_abi)
        : program_(program), default_abi_(default_abi) {}

    void run() {
        const auto object_count = program_.objects.size();
        for (std::size_t index = 0; index < object_count; ++index) {
            caller_ = nullptr;
            source_unit_ = program_.objects[index]->source_unit;
            rewrite(program_.objects[index]->initializer,
                    program_.objects[index]->type);
        }
        const auto function_count = program_.functions.size();
        for (std::size_t index = 0; index < function_count; ++index) {
            caller_ = program_.functions[index].get();
            source_unit_ = caller_->source_unit;
            scopes_.clear();
            scopes_.emplace_back();
            for (const auto& parameter : caller_->parameters) {
                scopes_.back().emplace(name_key(parameter), parameter.type);
            }
            if (caller_->body) rewrite(*caller_->body);
        }
        caller_ = nullptr;
        scopes_.clear();
    }

private:
    static const FunctionType* pointed_function(const TypePtr& type) {
        return type && type->kind == Type::Kind::Pointer && type->pointee &&
                       type->pointee->kind == Type::Kind::Function
                   ? type->pointee->function.get()
                   : nullptr;
    }

    static bool same_shape(const FunctionDecl& source,
                           const FunctionType& destination) {
        if (source.variadic != destination.variadic ||
            source.parameters.size() != destination.parameters.size() ||
            source.result_location.value_or("auto") !=
                destination.result_location.value_or("auto") ||
            !same_type(source.return_type, destination.result)) {
            return false;
        }
        const auto* cleanup = source.attribute("stack_cleanup");
        const auto cleanup_name =
            cleanup && cleanup->arguments.size() == 1
                ? decode_string_literal(cleanup->arguments.front())
                : std::nullopt;
        if (cleanup_name.value_or("caller") !=
            destination.stack_cleanup.value_or("caller")) return false;
        std::vector<std::string> source_clobbers;
        for (const auto& attribute : source.attributes) {
            if (attribute.name != "clobber") continue;
            for (const auto& argument : attribute.arguments) {
                if (const auto value = decode_string_literal(argument))
                    source_clobbers.push_back(*value);
            }
        }
        auto destination_clobbers = destination.clobbers;
        std::sort(source_clobbers.begin(), source_clobbers.end());
        std::sort(destination_clobbers.begin(), destination_clobbers.end());
        if (source_clobbers != destination_clobbers) return false;
        for (std::size_t index = 0; index < source.parameters.size(); ++index) {
            if (source.parameters[index].mode !=
                    destination.parameters[index].mode ||
                source.parameters[index].location_name.value_or("auto") !=
                    destination.parameters[index].location_name.value_or("auto") ||
                !same_type(callable_parameter_type(source.parameters[index].type,
                                                   source.parameters[index].mode),
                           callable_parameter_type(destination.parameters[index].type,
                                                   destination.parameters[index].mode))) {
                return false;
            }
        }
        return true;
    }

    static std::string explicit_abi(const FunctionDecl& function) {
        const auto* attribute = function.attribute("abi");
        if (!attribute || attribute->arguments.size() != 1) return {};
        return decode_string_literal(attribute->arguments.front())
            .value_or(std::string{});
    }

    bool already_stable(const FunctionDecl& source,
                        std::string_view destination_abi) const {
        const auto source_abi = explicit_abi(source);
        const bool dynamic = source.definition() && !source.variadic &&
                             source.linkage != Linkage::Global;
        if (dynamic && source_abi.empty()) return false;
        const auto effective_source =
            source_abi.empty() ? std::string_view(default_abi_)
                               : std::string_view(source_abi);
        const auto effective_destination =
            destination_abi.empty() ? std::string_view(default_abi_)
                                    : destination_abi;
        return effective_source == effective_destination;
    }

    TypePtr lookup(const Expr& expression) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name_key(expression));
            if (found != scope->end()) return found->second;
        }
        if (const auto* object = resolve_object(program_, caller_, expression)) return object->type;
        return {};
    }

    TypePtr infer(const Expr& expression) const {
        switch (expression.kind) {
        case Expr::Kind::Quote: return {};
        case Expr::Kind::ByteSequence: return {};
        case Expr::Kind::Address:
            return expression.type;
        case Expr::Kind::Integer:
            return expression.evaluated_integer
                       ? builtin_type(expression.evaluated_integer->type)
                       : builtin_type(BuiltinType::I32);
        case Expr::Kind::Floating:
            return builtin_type(expression.text.ends_with("f32")
                                    ? BuiltinType::F32
                                    : BuiltinType::F64);
        case Expr::Kind::String:
            return pointer_type(builtin_type(BuiltinType::U8, true));
        case Expr::Kind::Character:
            return builtin_type(BuiltinType::U32);
        case Expr::Kind::Name:
            return lookup(expression);
        case Expr::Kind::Parenthesized:
            return expression.left ? infer(*expression.left) : TypePtr{};
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
            return builtin_type(BuiltinType::Uptr);
        case Expr::Kind::Unary: {
            const auto operand = expression.left ? infer(*expression.left)
                                                 : TypePtr{};
            if (expression.text == "&" && operand) {
                return pointer_type(operand);
            }
            if (expression.text == "*" && operand &&
                operand->kind == Type::Kind::Pointer) {
                return operand->pointee;
            }
            if (expression.text == "!") return builtin_type(BuiltinType::Bool);
            return operand;
        }
        case Expr::Kind::Binary: {
            if (expression.text == "index" && expression.left) {
                const auto aggregate = infer(*expression.left);
                if (aggregate && aggregate->kind == Type::Kind::Pointer) {
                    return aggregate->pointee;
                }
                if (aggregate && aggregate->kind == Type::Kind::Array) {
                    return aggregate->element;
                }
            }
            if (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == "<=" ||
                expression.text == ">" || expression.text == ">=" ||
                expression.text == "&&" || expression.text == "||") {
                return builtin_type(BuiltinType::Bool);
            }
            const auto left = expression.left ? infer(*expression.left)
                                              : TypePtr{};
            const auto right = expression.right ? infer(*expression.right)
                                                : TypePtr{};
            return same_type(left, right) ? left : (left ? left : right);
        }
        case Expr::Kind::Assign:
            return expression.left ? infer(*expression.left) : TypePtr{};
        case Expr::Kind::Conditional: {
            const auto left = expression.right ? infer(*expression.right)
                                               : TypePtr{};
            const auto right = expression.third ? infer(*expression.third)
                                                : TypePtr{};
            return same_type(left, right) ? left : TypePtr{};
        }
        case Expr::Kind::AggregateInitializer:
            return {};
        case Expr::Kind::Call:
            if (expression.left &&
                expression.left->kind == Expr::Kind::Name) {
                if (const auto* function = resolve_function(
                        program_, caller_, *expression.left,
                        [](const FunctionDecl&) { return true; })) {
                    return function->return_type;
                }
            } else if (expression.left) {
                if (const auto signature = pointed_function(
                        infer(*expression.left))) {
                    return signature->result;
                }
            }
            return {};
        }
        return {};
    }

    std::string make_adapter(const FunctionDecl& source,
                             const FunctionType& signature,
                             SourceLocation location) {
        const auto key = source.name + '#' +
                         (source.linkage == Linkage::Static ? source.source_unit : "") + '#' +
                         canonical_type_name(function_type(
                             clone_type(signature.result),
                             signature.parameters, signature.variadic,
                             signature.abi));
        if (const auto found = adapters_.find(key); found != adapters_.end()) {
            return found->second;
        }

        auto adapter = std::make_unique<FunctionDecl>();
        adapter->location = location;
        adapter->name = "$adapter." + std::to_string(ordinal_++);
        adapter->source_unit = source.source_unit;
        adapter->return_type = clone_type(signature.result);
        adapter->linkage = Linkage::Static;
        adapter->variadic = signature.variadic;
        const auto adapter_abi = signature.abi.empty()
                                     ? default_abi_
                                     : signature.abi;
        adapter->attributes.push_back(
            {"abi", {"\"" + adapter_abi + "\""}, location});

        auto call = std::make_unique<Expr>();
        call->kind = Expr::Kind::Call;
        call->location = location;
        call->left = std::make_unique<Expr>();
        call->left->kind = Expr::Kind::Name;
        call->left->location = location;
        call->left->text = source.name;
        for (std::size_t index = 0; index < signature.parameters.size();
             ++index) {
            auto parameter = signature.parameters[index];
            parameter.name = "$argument." + std::to_string(index);
            parameter.location = location;
            adapter->parameters.push_back(parameter);
            auto argument = std::make_unique<Expr>();
            argument->kind = Expr::Kind::Name;
            argument->location = location;
            argument->text = parameter.name;
            call->arguments.push_back(std::move(argument));
        }

        adapter->body = std::make_unique<Statement>();
        adapter->body->kind = Statement::Kind::Compound;
        adapter->body->location = location;
        const bool returns_void =
            signature.result &&
            signature.result->kind == Type::Kind::Builtin &&
            signature.result->builtin == BuiltinType::Void;
        if (returns_void) {
            auto invoke = std::make_unique<Statement>();
            invoke->kind = Statement::Kind::Expression;
            invoke->location = location;
            invoke->expression = std::move(call);
            adapter->body->statements.push_back(std::move(invoke));
            auto return_statement = std::make_unique<Statement>();
            return_statement->kind = Statement::Kind::Return;
            return_statement->location = location;
            adapter->body->statements.push_back(std::move(return_statement));
        } else {
            auto return_statement = std::make_unique<Statement>();
            return_statement->kind = Statement::Kind::Return;
            return_statement->location = location;
            return_statement->expression = std::move(call);
            adapter->body->statements.push_back(std::move(return_statement));
        }

        const auto name = adapter->name;
        adapters_.emplace(key, name);
        program_.functions.push_back(std::move(adapter));
        return name;
    }

    bool adapt(std::unique_ptr<Expr>& expression,
               const TypePtr& destination) {
        const auto* signature = pointed_function(destination);
        if (!expression || !signature) return false;
        if (expression->kind == Expr::Kind::Parenthesized &&
            expression->left) {
            return adapt(expression->left, destination);
        }
        if (expression->kind == Expr::Kind::Unary && expression->text == "&" &&
            expression->left) {
            return adapt(expression->left, destination);
        }
        const bool address = expression->kind == Expr::Kind::Address &&
                             expression->evaluated_address;
        if (expression->kind != Expr::Kind::Name && !address) return false;
        const auto* source = address ? expression->evaluated_address->function
            : resolve_function(program_, caller_, *expression,
                               [](const FunctionDecl&) { return true; });
        if (!source || !same_shape(*source, *signature) ||
            already_stable(*source, signature->abi) || source->variadic) {
            return false;
        }
        bind_exact_name(*expression, make_adapter(*source, *signature,
                                                  expression->location));
        if (address) {
            expression->evaluated_address->function = resolve_function(
                program_, nullptr, expression->text,
                [](const FunctionDecl&) { return true; });
        }
        return true;
    }

    void rewrite(std::unique_ptr<Expr>& expression,
                 const TypePtr& destination = {}) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Address)
            (void)adapt(expression, expression->type);
        (void)adapt(expression, destination);
        switch (expression->kind) {
        case Expr::Kind::Assign: {
            rewrite(expression->left);
            rewrite(expression->right,
                    expression->left ? infer(*expression->left) : TypePtr{});
            return;
        }
        case Expr::Kind::Conditional:
            rewrite(expression->left);
            rewrite(expression->right, destination);
            rewrite(expression->third, destination);
            return;
        case Expr::Kind::Cast:
            rewrite(expression->left, expression->type);
            return;
        case Expr::Kind::Call: {
            const FunctionType* signature{};
            if (expression->left &&
                expression->left->kind == Expr::Kind::Name) {
                if (const auto* callee = resolve_function(
                        program_, caller_, *expression->left,
                        [](const FunctionDecl&) { return true; })) {
                    for (std::size_t index = 0;
                         index < expression->arguments.size(); ++index) {
                        rewrite(expression->arguments[index],
                                index < callee->parameters.size()
                                    ? callee->parameters[index].type
                                    : TypePtr{});
                    }
                } else {
                    for (auto& argument : expression->arguments) {
                        rewrite(argument);
                    }
                }
            } else if (expression->left) {
                signature = pointed_function(infer(*expression->left));
                for (std::size_t index = 0;
                     index < expression->arguments.size(); ++index) {
                    rewrite(expression->arguments[index],
                            signature && index < signature->parameters.size()
                                ? signature->parameters[index].type
                                : TypePtr{});
                }
            }
            rewrite(expression->left);
            for (auto& argument : expression->generic_arguments) {
                rewrite(argument.value);
            }
            return;
        }
        default:
            break;
        }
        rewrite(expression->left);
        rewrite(expression->right);
        rewrite(expression->third);
        for (auto& argument : expression->arguments) rewrite(argument);
        for (auto& argument : expression->generic_arguments) {
            rewrite(argument.value);
        }
        visit_initializer_children(
            *expression,
            [&](std::unique_ptr<Expr>& child) { rewrite(child); });
    }

    void rewrite(Statement& statement) {
        const bool scoped = statement.kind == Statement::Kind::Compound ||
                            statement.kind == Statement::Kind::For;
        if (scoped) scopes_.emplace_back();
        if (statement.declaration) {
            rewrite(statement.declaration->dynamic_array_bound);
            rewrite(statement.declaration->initializer,
                    statement.declaration->type);
            scopes_.back()[name_key(*statement.declaration)] =
                statement.declaration->type;
        }
        rewrite(statement.expression,
                statement.kind == Statement::Kind::Return && caller_
                    ? caller_->return_type
                    : TypePtr{});
        rewrite(statement.condition);
        rewrite(statement.increment);
        if (statement.first) rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
        if (scoped) scopes_.pop_back();
    }

    Program& program_;
    std::string default_abi_;
    FunctionDecl* caller_{};
    std::string source_unit_;
    std::uint64_t ordinal_{};
    std::unordered_map<std::string, std::string> adapters_;
    std::vector<NameMap<TypePtr>> scopes_;
};

void lift_function_pointer_adapters(Program& program,
                                    std::string_view default_abi) {
    FunctionPointerAdapterLifter(program, default_abi).run();
}

struct OperatorBinding {
    std::string token;
    std::vector<TypePtr> parameters;
    TypePtr result;
    std::string function_name;
    SourceLocation location;
};

std::optional<unsigned> operator_arity(std::string_view token,
                                      unsigned parameter_count) {
    static constexpr std::string_view unary[] = {
        "+", "-", "~", "!",
    };
    static constexpr std::string_view binary[] = {
        "+", "-", "*", "/", "%", "<<", ">>", "&", "^", "|",
        "==", "!=", "<", "<=", ">", ">=",
    };
    if (parameter_count == 1 &&
        std::find(std::begin(unary), std::end(unary), token) !=
            std::end(unary)) {
        return 1;
    }
    if (parameter_count == 2 &&
        std::find(std::begin(binary), std::end(binary), token) !=
            std::end(binary)) {
        return 2;
    }
    return std::nullopt;
}

std::string operator_key(std::string_view token,
                         const std::vector<TypePtr>& parameters) {
    std::string result(token);
    result += '#';
    result += std::to_string(parameters.size());
    for (const auto& parameter : parameters) {
        result += ':';
        result += canonical_type_name(parameter);
    }
    return result;
}

const ObjectDecl* resolve_object(const Program& program,
                                 const FunctionDecl* caller,
                                 NameUse name) {
    const auto selected = value_namespace(program, caller, name);
    if (!selected) return nullptr;
    const auto unit = lookup_source_unit(name, caller);
    const auto exact = [&](std::string_view qualified) -> const ObjectDecl* {
        const ObjectDecl* shared{};
        for (const auto& candidate : program.objects) {
            if (candidate->name != qualified) continue;
            if (candidate->linkage == Linkage::Static) {
                if (unit.empty() || candidate->source_unit == unit) return candidate.get();
            } else if (!shared) shared = candidate.get();
        }
        return shared;
    };
    return exact(*selected);
}

struct ResolvedEnumerator {
    const EnumDecl* enumeration{};
    const EnumDecl::Enumerator* enumerator{};
};

std::optional<ResolvedEnumerator> exact_enumerator(
    const Program& program, std::string_view name) {
    std::optional<ResolvedEnumerator> result;
    for (const auto& enumeration : program.enumerations) {
        for (const auto& enumerator : enumeration.enumerators) {
            if (enumerator.name != name) continue;
            if (result) return std::nullopt;
            result = ResolvedEnumerator{&enumeration, &enumerator};
        }
    }
    return result;
}

std::optional<ResolvedEnumerator> resolve_enumerator(
    const Program& program, const FunctionDecl* caller,
    std::string_view current_namespace, NameUse name) {
    const auto selected = value_namespace(program, caller, name, current_namespace);
    return selected ? exact_enumerator(program, *selected) : std::nullopt;
}

class OperatorBinder {
public:
    OperatorBinder(Program& program, Diagnostics& diagnostics)
        : program_(program), diagnostics_(diagnostics) {}

    bool run() {
        collect();
        if (diagnostics_.errors() != 0) return false;
        for (auto& function : program_.functions) {
            if (!function->body) continue;
            caller_ = function.get();
            scopes_.clear();
            scopes_.emplace_back();
            for (const auto& parameter : function->parameters) {
                scopes_.back().emplace(name_key(parameter), parameter.type);
            }
            rewrite_statement(*function->body);
        }
        caller_ = nullptr;
        scopes_.clear();
        scopes_.emplace_back();
        for (auto& object : program_.objects) {
            rewrite_expression(object->initializer);
        }
        for (auto& function : program_.functions) {
            std::erase_if(function->attributes, [](const Attribute& attribute) {
                return attribute.name == "operator";
            });
        }
        return diagnostics_.errors() == 0;
    }

private:
    void collect() {
        for (const auto& function : program_.functions) {
            std::vector<const Attribute*> declarations;
            for (const auto& attribute : function->attributes) {
                if (attribute.name == "operator") {
                    declarations.push_back(&attribute);
                }
            }
            if (declarations.empty()) continue;
            if (declarations.size() != 1) {
                diagnostics_.error(
                    declarations[1]->location,
                    "a declaration has at most one 'operator' attribute");
                continue;
            }
            const auto* attribute = declarations.front();
            if (attribute->arguments.size() != 1) {
                diagnostics_.error(
                    attribute->location,
                    "'operator' requires exactly one quoted operator token");
                continue;
            }
            const auto token =
                decode_string_literal(attribute->arguments.front());
            if (!token) {
                diagnostics_.error(
                    attribute->location,
                    "'operator' argument must be a string literal");
                continue;
            }
            if (!operator_arity(*token,
                                static_cast<unsigned>(
                                    function->parameters.size()))) {
                diagnostics_.error(
                    attribute->location,
                    "operator '" + *token + "' is not overloadable with " +
                        std::to_string(function->parameters.size()) +
                        " operand(s)");
                continue;
            }
            if (function->variadic ||
                !function->generic_parameters.empty()) {
                diagnostics_.error(
                    attribute->location,
                    "operator bindings cannot be variadic or generic");
                continue;
            }
            bool nominal = false;
            bool valid_modes = true;
            std::vector<TypePtr> parameters;
            for (const auto& parameter : function->parameters) {
                parameters.push_back(parameter.type);
                nominal = nominal || is_nominal(parameter.type);
                valid_modes =
                    valid_modes && parameter.mode == ParameterMode::In &&
                    !parameter.location_name;
            }
            if (!nominal) {
                diagnostics_.error(
                    attribute->location,
                    "an operator binding requires at least one nominal "
                    "user-defined operand type");
                continue;
            }
            if (!valid_modes || function->result_location) {
                diagnostics_.error(
                    attribute->location,
                    "operator operands and result use ordinary automatic "
                    "'in' locations");
                continue;
            }
            OperatorBinding binding{
                *token, parameters, function->return_type, function->name,
                attribute->location,
            };
            const auto key = operator_key(*token, parameters);
            const auto [function_position, function_inserted] =
                function_bindings_.emplace(function->name, key);
            if (!function_inserted &&
                function_position->second != key) {
                diagnostics_.error(
                    attribute->location,
                    "operator binding redeclarations of '" +
                        function->name + "' disagree");
                continue;
            }
            const auto [position, inserted] =
                bindings_.emplace(key, std::move(binding));
            if (!inserted &&
                position->second.function_name != function->name) {
                diagnostics_.error(
                    attribute->location,
                    "duplicate exact operator binding for '" + *token + "'");
            }
        }
    }

    TypePtr find_name(const Expr& expression) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name_key(expression));
            if (found != scope->end()) return found->second;
        }
        if (const auto* object = resolve_object(program_, caller_, expression)) {
            return object->type;
        }
        return {};
    }

    TypePtr member_type(const Expr& expression) const {
        if (!expression.left || !expression.right ||
            expression.right->kind != Expr::Kind::Name) {
            return {};
        }
        auto base = infer(*expression.left);
        if (!base) return {};
        if (expression.text == "pointer_member") {
            if (base->kind != Type::Kind::Pointer || !base->pointee) {
                return {};
            }
            base = base->pointee;
        }
        if (base->kind != Type::Kind::Record) return {};
        const auto definition = std::find_if(
            program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.name == base->nominal_name &&
                       candidate.complete;
            });
        if (definition == program_.records.end()) return {};
        const auto member = std::find_if(
            definition->members.begin(), definition->members.end(),
            [&](const RecordMemberDecl& candidate) {
                return candidate.name == expression.right->text;
            });
        if (member == definition->members.end()) return {};
        auto result = clone_type(member->type);
        result->is_const = result->is_const || base->is_const;
        result->is_volatile = result->is_volatile || base->is_volatile;
        return result;
    }

    TypePtr infer(const Expr& expression) const {
        switch (expression.kind) {
        case Expr::Kind::Quote: return {};
        case Expr::Kind::ByteSequence: return {};
        case Expr::Kind::Integer:
            return builtin_type(BuiltinType::I32);
        case Expr::Kind::Floating:
            return builtin_type(expression.text.ends_with("f32")
                                    ? BuiltinType::F32
                                    : BuiltinType::F64);
        case Expr::Kind::Character:
            return builtin_type(BuiltinType::U32);
        case Expr::Kind::String:
            return pointer_type(
                builtin_type(BuiltinType::U8, true));
        case Expr::Kind::Name:
            return find_name(expression);
        case Expr::Kind::Address:
            return expression.type;
        case Expr::Kind::Parenthesized:
            return expression.left ? infer(*expression.left) : TypePtr{};
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
            return builtin_type(BuiltinType::Uptr);
        case Expr::Kind::Unary: {
            const auto operand =
                expression.left ? infer(*expression.left) : TypePtr{};
            if (expression.text == "&" && operand) return pointer_type(operand);
            if (expression.text == "*" && operand &&
                operand->kind == Type::Kind::Pointer) {
                return operand->pointee;
            }
            if (expression.text == "!") {
                return builtin_type(BuiltinType::Bool);
            }
            return operand;
        }
        case Expr::Kind::Binary: {
            if (expression.text == "member" ||
                expression.text == "pointer_member") {
                return member_type(expression);
            }
            if (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == "<=" ||
                expression.text == ">" || expression.text == ">=" ||
                expression.text == "&&" || expression.text == "||") {
                return builtin_type(BuiltinType::Bool);
            }
            const auto left =
                expression.left ? infer(*expression.left) : TypePtr{};
            const auto right =
                expression.right ? infer(*expression.right) : TypePtr{};
            return same_type(left, right) ? left : (left ? left : right);
        }
        case Expr::Kind::Assign:
            return expression.left ? infer(*expression.left) : TypePtr{};
        case Expr::Kind::Conditional: {
            const auto left =
                expression.right ? infer(*expression.right) : TypePtr{};
            const auto right =
                expression.third ? infer(*expression.third) : TypePtr{};
            return same_type(left, right) ? left : TypePtr{};
        }
        case Expr::Kind::AggregateInitializer:
            return {};
        case Expr::Kind::Call:
            if (expression.left &&
                expression.left->kind == Expr::Kind::Name) {
                if (const auto* function = resolve_function(
                        program_, caller_, *expression.left,
                        [](const FunctionDecl&) { return true; })) {
                    return function->return_type;
                }
            }
            return {};
        }
        return {};
    }

    void rewrite_expression(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        rewrite_expression(expression->left);
        rewrite_expression(expression->right);
        rewrite_expression(expression->third);
        for (auto& argument : expression->arguments) {
            rewrite_expression(argument);
        }
        for (auto& argument : expression->generic_arguments) {
            rewrite_expression(argument.value);
        }
        visit_initializer_children(*expression,
            [&](std::unique_ptr<Expr>& child) {
                rewrite_expression(child);
            });

        if (expression->kind == Expr::Kind::Binary &&
            (expression->text == "member" ||
             expression->text == "pointer_member" ||
             expression->text == "index")) {
            return;
        }

        unsigned arity{};
        std::vector<TypePtr> parameters;
        if (expression->kind == Expr::Kind::Unary && expression->left) {
            arity = 1;
            parameters.push_back(infer(*expression->left));
        } else if (expression->kind == Expr::Kind::Binary &&
                   expression->left && expression->right) {
            arity = 2;
            parameters.push_back(infer(*expression->left));
            parameters.push_back(infer(*expression->right));
        } else {
            return;
        }
        if (std::any_of(parameters.begin(), parameters.end(),
                        [](const TypePtr& type) { return !type; })) {
            return;
        }
        if (!operator_arity(expression->text, arity)) return;
        const auto found =
            bindings_.find(operator_key(expression->text, parameters));
        if (found == bindings_.end()) return;

        auto call = std::make_unique<Expr>();
        call->kind = Expr::Kind::Call;
        call->location = expression->location;
        call->left = std::make_unique<Expr>();
        call->left->kind = Expr::Kind::Name;
        call->left->location = expression->location;
        call->left->text = found->second.function_name;
        call->arguments.push_back(std::move(expression->left));
        if (arity == 2) {
            call->arguments.push_back(std::move(expression->right));
        }
        expression = std::move(call);
    }

    void rewrite_statement(Statement& statement) {
        if (statement.kind == Statement::Kind::Compound) {
            scopes_.emplace_back();
            for (auto& child : statement.statements) {
                rewrite_statement(*child);
            }
            scopes_.pop_back();
            return;
        }
        if (statement.kind == Statement::Kind::For) {
            scopes_.emplace_back();
            if (statement.first) rewrite_statement(*statement.first);
            rewrite_expression(statement.condition);
            rewrite_expression(statement.increment);
            if (statement.second) rewrite_statement(*statement.second);
            scopes_.pop_back();
            return;
        }
        if (statement.declaration) {
            rewrite_expression(statement.declaration->dynamic_array_bound);
            rewrite_expression(statement.declaration->initializer);
            scopes_.back()[name_key(*statement.declaration)] =
                statement.declaration->type;
        }
        rewrite_expression(statement.expression);
        rewrite_expression(statement.condition);
        rewrite_expression(statement.increment);
        if (statement.first) rewrite_statement(*statement.first);
        if (statement.second) rewrite_statement(*statement.second);
    }

    Program& program_;
    Diagnostics& diagnostics_;
    FunctionDecl* caller_{};
    std::vector<NameMap<TypePtr>> scopes_;
    std::unordered_map<std::string, OperatorBinding> bindings_;
    std::unordered_map<std::string, std::string> function_bindings_;
};

bool bind_operators(Program& program, Diagnostics& diagnostics) {
    return OperatorBinder(program, diagnostics).run();
}

struct EvalBuffer {
    std::string data;
    std::vector<std::uint8_t> assigned;
    // Zero means raw byte storage; nonzero records the scalar lvalue that
    // established effective type for each byte of a typed write.
    std::vector<std::uint8_t> effective_type;
    bool frozen{};
};

struct EvalMetaPointer {
    std::shared_ptr<const std::string> immutable;
    std::shared_ptr<EvalBuffer> mutable_buffer;
    std::size_t view_offset{};
    std::size_t view_length{};
    std::size_t position{};
};

struct EvalValue {
    UInt128 integer{};
    TypePtr type;
    std::optional<floating::Value> floating;
    std::shared_ptr<std::string> string{};
    std::size_t offset{};
    std::optional<AddressConstant> address;
    // A named function has not yet acquired its contextual pointer ABI.
    // Binding it to a typed cell/parameter/result consumes this distinction.
    bool function_designator{};
    // Immutable token storage is distinct from both strings and integers.
    std::shared_ptr<const TokenSequence> tokens;
    std::shared_ptr<const std::string> bytes;
    std::size_t byte_offset{};
    std::size_t byte_length{};
    // Mutable buffers are shared handles. Copying an EvalValue preserves
    // aliasing; freeze invalidates all copies through this shared state.
    std::shared_ptr<EvalBuffer> buffer;
    std::optional<EvalMetaPointer> meta_pointer;

    EvalValue() = default;
    EvalValue(UInt128 integer_value, TypePtr value_type,
              std::shared_ptr<std::string> string_value = {},
              std::size_t string_offset = 0)
        : integer(integer_value), type(std::move(value_type)),
          string(std::move(string_value)), offset(string_offset) {}
    EvalValue(floating::Value floating_value, TypePtr value_type)
        : type(std::move(value_type)), floating(floating_value) {}

    [[nodiscard]] bool pointer() const {
        return string != nullptr || address.has_value() || meta_pointer.has_value();
    }
    [[nodiscard]] bool truthy() const {
        return floating ? floating::nonzero(*floating) : integer != UInt128{};
    }
};

BuiltinType floating_literal_type(std::string_view text) {
    if (text.ends_with("f32")) return BuiltinType::F32;
    if (text.ends_with("f80")) return BuiltinType::F80;
    if (text.ends_with("f128")) return BuiltinType::F128;
    if (text.ends_with("fptr")) return BuiltinType::Fptr;
    return BuiltinType::F64;
}

floating::Format floating_format(BuiltinType type, unsigned address_bits) {
    switch (type) {
    case BuiltinType::F32: return floating::Format::Binary32;
    case BuiltinType::F80: return floating::Format::Extended80;
    case BuiltinType::F128: return floating::Format::Binary128;
    case BuiltinType::Fptr:
        return address_bits == 32 ? floating::Format::Binary32
                                  : floating::Format::Binary64;
    default: return floating::Format::Binary64;
    }
}

std::optional<EvalValue> parse_floating_value(const Expr& expression,
                                             unsigned address_bits) {
    if (expression.evaluated_floating) {
        const auto& value = *expression.evaluated_floating;
        return EvalValue{floating::Value{value.bits,
                floating_format(value.type, address_bits)},
            builtin_type(value.type)};
    }
    auto text = expression.text;
    const auto type = floating_literal_type(text);
    const auto suffix = type == BuiltinType::Fptr || type == BuiltinType::F128
        ? 4U : 3U;
    if (text.ends_with("f32") || text.ends_with("f64") ||
        text.ends_with("f80") || text.ends_with("f128") ||
        text.ends_with("fptr")) text.resize(text.size() - suffix);
    const auto value = floating::parse(std::move(text),
                                      floating_format(type, address_bits));
    return value ? std::optional<EvalValue>{EvalValue{*value, builtin_type(type)}}
                 : std::nullopt;
}

std::optional<EvalValue> parse_integer_value(const Expr& expression) {
    if (expression.evaluated_integer)
        return EvalValue{expression.evaluated_integer->value,
                         expression.type
                             ? clone_type(expression.type)
                             : builtin_type(
                                   expression.evaluated_integer->type)};
    auto text = expression.text;
    BuiltinType type = BuiltinType::I32;
    bool explicit_type = false;
    static constexpr std::pair<std::string_view, BuiltinType> suffixes[] = {
        {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
        {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
        {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
        {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
        {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
        {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
    };
    for (const auto& [suffix, candidate] : suffixes) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.resize(text.size() - suffix.size());
            type = candidate;
            explicit_type = true;
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
    if (!explicit_type) {
        if (fits_signed_positive(*parsed, 32)) type = BuiltinType::I32;
        else if (!decimal && fits_unsigned(*parsed, 32)) type = BuiltinType::U32;
        else if (fits_signed_positive(*parsed, 64)) type = BuiltinType::I64;
        else if (!decimal && fits_unsigned(*parsed, 64)) type = BuiltinType::U64;
        else if (fits_signed_positive(*parsed, 128)) type = BuiltinType::I128;
        else if (!decimal) type = BuiltinType::U128;
        else return std::nullopt;
    }
    return EvalValue{*parsed, builtin_type(type)};
}

TypePtr infer_generic_actual(const Expr& expression,
                             const FunctionDecl* caller,
                             Program& program,
                             const GenericExpansionState& state,
                             bool decay) {
    const auto adjusted = [&](TypePtr type) -> TypePtr {
        if (!type || !decay) return type;
        if (type->kind == Type::Kind::Array)
            return pointer_type(type->element);
        if (type->kind == Type::Kind::Function)
            return pointer_type(type);
        return type;
    };
    const auto integer_shape = [&](const TypePtr& type) {
        const auto kind = type->builtin;
        const bool signed_type = kind == BuiltinType::I8 || kind == BuiltinType::I16 ||
            kind == BuiltinType::I32 || kind == BuiltinType::I64 ||
            kind == BuiltinType::I128 || kind == BuiltinType::Iptr;
        const auto bits = kind == BuiltinType::Iptr || kind == BuiltinType::Uptr
            ? program.address_bits : type_bits(type);
        return IntegerType{bits, signed_type, kind == BuiltinType::Bool};
    };
    const auto integer_result = [&](IntegerType shape,
                                    const TypePtr& left,
                                    const TypePtr& right) -> TypePtr {
        for (const auto& candidate : {left, right}) {
            if (!is_integer(candidate)) continue;
            const auto candidate_shape = integer_shape(candidate);
            if (candidate_shape.bits >= 32 &&
                candidate_shape.bits == shape.bits &&
                candidate_shape.is_signed == shape.is_signed)
                return builtin_type(candidate->builtin);
        }
        const auto kind = shape.bits == 8
            ? (shape.is_signed ? BuiltinType::I8 : BuiltinType::U8)
            : shape.bits == 16
            ? (shape.is_signed ? BuiltinType::I16 : BuiltinType::U16)
            : shape.bits == 32
            ? (shape.is_signed ? BuiltinType::I32 : BuiltinType::U32)
            : shape.bits == 64
            ? (shape.is_signed ? BuiltinType::I64 : BuiltinType::U64)
            : (shape.is_signed ? BuiltinType::I128 : BuiltinType::U128);
        return builtin_type(kind);
    };
    const auto common_numeric = [&](const TypePtr& left,
                                    const TypePtr& right) -> TypePtr {
        if (!left || !right ||
            (!is_integer(left) && !is_floating(left)) ||
            (!is_integer(right) && !is_floating(right))) return {};
        if (is_floating(left) || is_floating(right)) {
            if (!is_floating(left)) return builtin_type(right->builtin);
            if (!is_floating(right)) return builtin_type(left->builtin);
            const auto rank = [&](const TypePtr& type) {
                return type->builtin == BuiltinType::Fptr
                    ? program.address_bits : type_bits(type);
            };
            return builtin_type(rank(left) >= rank(right)
                ? left->builtin : right->builtin);
        }
        return integer_result(common_integer_type(integer_shape(left),
                                                  integer_shape(right)),
                              left, right);
    };
    switch (expression.kind) {
    case Expr::Kind::Integer: {
        const auto value = parse_integer_value(expression);
        return value ? value->type : TypePtr{};
    }
    case Expr::Kind::Floating:
        return builtin_type(expression.evaluated_floating
            ? expression.evaluated_floating->type
            : floating_literal_type(expression.text));
    case Expr::Kind::Character:
        return builtin_type(BuiltinType::U32);
    case Expr::Kind::String:
        return pointer_type(builtin_type(BuiltinType::U8, true));
    case Expr::Kind::Address:
        return expression.type;
    case Expr::Kind::Name: {
        const auto key = name_key(expression);
        for (auto entry = state.local_types.rbegin();
             entry != state.local_types.rend(); ++entry)
            if (entry->first == key) return adjusted(entry->second);
        if (const auto* object = resolve_object(program, caller, expression))
            return adjusted(object->type);
        if (const auto* function = resolve_function(
                program, caller, expression,
                [](const FunctionDecl& candidate) {
                    return candidate.generic_parameters.empty();
                })) {
            auto type = function_type(function->return_type,
                                      function->parameters,
                                      function->variadic);
            type->function->result_location = function->result_location;
            if (const auto* abi = function->attribute("abi"); abi &&
                abi->arguments.size() == 1)
                type->function->abi = decode_string_literal(
                    abi->arguments.front()).value_or("");
            if (const auto* cleanup = function->attribute("stack_cleanup");
                cleanup && cleanup->arguments.size() == 1)
                type->function->stack_cleanup = decode_string_literal(
                    cleanup->arguments.front());
            for (const auto& attribute : function->attributes) {
                if (attribute.name != "clobber") continue;
                for (const auto& argument : attribute.arguments)
                    if (const auto resource = decode_string_literal(argument))
                        type->function->clobbers.push_back(*resource);
            }
            return adjusted(type);
        }
        return {};
    }
    case Expr::Kind::Parenthesized:
        return expression.left
                   ? infer_generic_actual(*expression.left, caller, program,
                                          state, decay)
                   : TypePtr{};
    case Expr::Kind::Cast:
        return adjusted(expression.type);
    case Expr::Kind::Sizeof:
    case Expr::Kind::Alignof:
        return builtin_type(BuiltinType::Uptr);
    case Expr::Kind::Assign:
        return expression.left
                   ? infer_generic_actual(*expression.left, caller, program,
                                          state, false)
                   : TypePtr{};
    case Expr::Kind::Unary: {
        if (expression.text == "!") return builtin_type(BuiltinType::Bool);
        if (!expression.left) return {};
        auto operand = infer_generic_actual(
            *expression.left, caller, program, state,
            expression.text != "&");
        if (!operand) return {};
        if (expression.text == "&") return pointer_type(operand);
        if (expression.text == "*")
            return operand->kind == Type::Kind::Pointer
                       ? adjusted(operand->pointee)
                       : TypePtr{};
        if ((expression.text == "+" || expression.text == "-" ||
             expression.text == "~") && is_integer(operand))
            return integer_result(promote_integer(integer_shape(operand)),
                                  operand, {});
        if (expression.text == "~") return {};
        if ((expression.text == "+" || expression.text == "-") &&
            is_floating(operand)) return builtin_type(operand->builtin);
        return operand;
    }
    case Expr::Kind::Binary: {
        if (!expression.left || !expression.right) return {};
        if (expression.text == "member" ||
            expression.text == "pointer_member") {
            if (expression.right->kind != Expr::Kind::Name) return {};
            auto base = infer_generic_actual(*expression.left, caller,
                                             program, state, false);
            if (expression.text == "pointer_member") {
                if (!base || base->kind != Type::Kind::Pointer) return {};
                base = base->pointee;
            }
            if (!base || base->kind != Type::Kind::Record) return {};
            const auto record = std::find_if(
                program.records.begin(), program.records.end(),
                [&](const RecordDecl& candidate) {
                    return candidate.name == base->nominal_name &&
                           candidate.complete;
                });
            if (record == program.records.end()) return {};
            const auto member = std::find_if(
                record->members.begin(), record->members.end(),
                [&](const RecordMemberDecl& candidate) {
                    return candidate.name == expression.right->text;
                });
            if (member == record->members.end()) return {};
            auto result = clone_type(member->type);
            result->is_const = result->is_const || base->is_const;
            result->is_volatile = result->is_volatile || base->is_volatile;
            return adjusted(result);
        }
        if (expression.text == "==" || expression.text == "!=" ||
            expression.text == "<" || expression.text == "<=" ||
            expression.text == ">" || expression.text == ">=" ||
            expression.text == "&&" || expression.text == "||")
            return builtin_type(BuiltinType::Bool);
        const auto left = infer_generic_actual(*expression.left, caller,
                                               program, state);
        if (expression.text == "index") {
            const auto index = infer_generic_actual(*expression.right, caller,
                                                    program, state);
            if (!left || !is_integer(index)) return {};
            return left->kind == Type::Kind::Pointer ||
                           left->kind == Type::Kind::Array
                       ? adjusted(left->kind == Type::Kind::Pointer
                                      ? left->pointee
                                      : left->element)
                       : TypePtr{};
        }
        const auto right = infer_generic_actual(*expression.right, caller,
                                                program, state);
        if (!left || !right) return {};
        if ((expression.text == "+" || expression.text == "-") &&
            left->kind == Type::Kind::Pointer && is_integer(right))
            return left;
        if (expression.text == "+" && is_integer(left) &&
            right->kind == Type::Kind::Pointer) return right;
        if (expression.text == "-" && left->kind == Type::Kind::Pointer &&
            right->kind == Type::Kind::Pointer)
            return builtin_type(BuiltinType::Iptr);
        if ((expression.text == "<<" || expression.text == ">>") &&
            is_integer(left) && is_integer(right))
            return integer_result(promote_integer(integer_shape(left)),
                                  left, {});
        if ((expression.text == "%" || expression.text == "&" ||
             expression.text == "|" || expression.text == "^") &&
            (!is_integer(left) || !is_integer(right))) return {};
        if (expression.text == "+" || expression.text == "-" ||
            expression.text == "*" || expression.text == "/" ||
            expression.text == "%" || expression.text == "&" ||
            expression.text == "|" || expression.text == "^")
            return common_numeric(left, right);
        return same_type(left, right) ? left : TypePtr{};
    }
    case Expr::Kind::Conditional: {
        if (!expression.right || !expression.third) return {};
        const auto yes = infer_generic_actual(*expression.right, caller,
                                              program, state);
        const auto no = infer_generic_actual(*expression.third, caller,
                                             program, state);
        if (same_type(yes, no)) return yes;
        return common_numeric(yes, no);
    }
    case Expr::Kind::Call: {
        if (!expression.left) return {};
        if (expression.left->kind == Expr::Kind::Name) {
            const auto* function = resolve_function(
                program, caller, *expression.left,
                [](const FunctionDecl& candidate) {
                    return candidate.generic_parameters.empty();
                });
            if (function) return adjusted(function->return_type);
        }
        auto callee = infer_generic_actual(*expression.left, caller,
                                           program, state);
        if (callee && callee->kind == Type::Kind::Pointer)
            callee = callee->pointee;
        return callee && callee->kind == Type::Kind::Function &&
                       callee->function
                   ? adjusted(callee->function->result)
                   : TypePtr{};
    }
    default:
        return {};
    }
}

bool signed_value(const EvalValue& value) {
    if (!value.type || value.type->kind != Type::Kind::Builtin) return false;
    return value.type->builtin == BuiltinType::I8 ||
           value.type->builtin == BuiltinType::I16 ||
           value.type->builtin == BuiltinType::I32 ||
           value.type->builtin == BuiltinType::I64 ||
           value.type->builtin == BuiltinType::I128 ||
           value.type->builtin == BuiltinType::Iptr;
}

class Evaluator {
public:
    Evaluator(Program& program, Diagnostics& diagnostics,
              const FunctionDecl* caller = nullptr,
              std::string current_namespace = {},
              const LayoutQuery* size_of = nullptr,
              const LayoutQuery* align_of = nullptr,
              const GenericPointerResolver* pointer_resolver = nullptr,
              std::shared_ptr<const SyntaxContext> macro_context = {})
        : program_(program), diagnostics_(diagnostics),
          current_function_(caller),
          current_namespace_(std::move(current_namespace)),
          size_of_(size_of), align_of_(align_of), pointer_resolver_(pointer_resolver),
          procedural_(macro_context != nullptr), macro_context_(std::move(macro_context)) {}

    bool charge_input_tokens(const TokenSequence& tokens, SourceLocation location) {
        constexpr std::size_t metadata_cost = 128;
        std::size_t size = 0;
        const auto byte_limit = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        for (const auto& token : tokens) {
            if (size > byte_limit || token.text.size() > byte_limit - size ||
                metadata_cost > byte_limit - size - token.text.size()) {
                fail(location, "translation-time token input budget exceeded " +
                    std::to_string(byte_limit) + " bytes");
                return false;
            }
            size += token.text.size() + metadata_cost;
        }
        return charge_meta_bytes(size, location);
    }

    std::unique_ptr<Expr> required_pointer(const Expr& source, const TypePtr& destination) {
        if (!validate_required_tree(source)) return {};
        auto value = expression(source);
        if (value) value = convert(*value, destination, source.location);
        if (!value || !value->address) {
            fail(source.location, "translation-time object pointer cannot escape into a generic argument");
            return {};
        }
        return value_expression(*value, source.location);
    }

    std::optional<EvalValue> required_integer(const Expr& source,
                                             const TypePtr& destination = {}) {
        if (!validate_required_tree(source)) return std::nullopt;
        auto value = expression(source);
        if (!value) return std::nullopt;
        if (value->pointer() || !is_integer(value->type)) {
            fail(source.location, "required expression is not an integer translation-time value");
            return std::nullopt;
        }
        if (!destination) return value;
        const auto from = integer_type(value->type);
        const auto to = integer_type(destination);
        const bool negative = integer_negative(value->integer, from);
        bool fits;
        if (to.is_bool) {
            fits = !negative && !(UInt128{1} < value->integer);
        } else if (negative) {
            const auto magnitude = mask_to(negate(value->integer), from.bits);
            fits = to.is_signed && !(shift_left(UInt128{1}, to.bits - 1) < magnitude);
        } else {
            fits = to.is_signed ? fits_signed_positive(value->integer, to.bits)
                                : fits_unsigned(value->integer, to.bits);
        }
        if (!fits) {
            fail(source.location, "generic value argument is not representable in parameter type '" +
                                  type_name(destination) + "'");
            return std::nullopt;
        }
        return convert(*value, destination, source.location);
    }

    std::optional<EvalValue> required_floating(const Expr& source,
                                               const TypePtr& destination) {
        if (!validate_required_tree(source)) return std::nullopt;
        auto value = expression(source);
        if (value && (value->bytes || value->buffer)) {
            fail(source.location, "meta byte values cannot be used as a runtime scalar");
            return std::nullopt;
        }
        if (!value || value->pointer() ||
            (!is_integer(value->type) && !is_floating(value->type))) {
            fail(source.location,
                 "required expression is not a scalar translation-time value");
            return std::nullopt;
        }
        value = convert(*value, destination, source.location);
        if (!value) fail(source.location,
                         "floating initializer cannot be converted to its type");
        return value;
    }

    std::optional<EvalValue> required_scalar(const Expr& source) {
        if (!validate_required_tree(source)) return std::nullopt;
        auto value = expression(source);
        if (value && (value->bytes || value->buffer)) {
            fail(source.location, "meta byte values cannot be used as a runtime scalar");
            return std::nullopt;
        }
        if (!value || value->pointer() ||
            (!is_integer(value->type) && !is_floating(value->type))) {
            fail(source.location,
                 "required expression is not a scalar translation-time value");
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::string> required_bytes(const Expr& source) {
        if (!validate_required_tree(source)) return std::nullopt;
        auto value = expression(source);
        if (!value) return std::nullopt;
        if (!value->bytes || !value->type ||
            value->type->kind != Type::Kind::Bytes) {
            fail(source.location, "static byte array requires a $::meta::bytes initializer; got '" +
                 type_name(value->type) + "'");
            return std::nullopt;
        }
        if (!charge_meta_bytes(value->byte_length, source.location))
            return std::nullopt;
        return value->bytes->substr(value->byte_offset, value->byte_length);
    }

    bool validate_procedural_body(const FunctionDecl& function) {
        scopes_.emplace_back();
        for (const auto& parameter : function.parameters)
            scopes_.back()[name_key(parameter)] = {
                EvalValue{UInt128{}, parameter.type}, false,
                parameter.type->is_const};
        const bool valid = function.body && validate_macro_statement(*function.body, 0, 0);
        scopes_.pop_back();
        return valid;
    }

    void diagnose(SourceLocation fallback) const {
        diagnostics_.error(
            failure_location_.valid() ? failure_location_ : fallback,
            failure_reason_.empty()
                ? "expression is not a translation-time value"
                : failure_reason_);
        for (auto frame = failure_trace_.rbegin();
             frame != failure_trace_.rend(); ++frame) {
            diagnostics_.note(
                frame->location,
                "while evaluating call to '" + frame->function + "'");
        }
    }

    std::optional<EvalValue> call(const FunctionDecl& function,
                                  const std::vector<EvalValue>& arguments,
                                  SourceLocation location) {
        if (++depth_ > program_.evaluation_limits.depth) {
            fail(location,
                 "translation-time recursion depth exceeded " +
                     std::to_string(program_.evaluation_limits.depth));
            --depth_;
            return std::nullopt;
        }
        if (!function.body || arguments.size() != function.parameters.size()) {
            fail(location,
                 "function '" + function.name +
                     "' has no visible implementation for translation-time "
                     "evaluation");
            --depth_;
            return std::nullopt;
        }
        for (const auto& parameter : function.parameters) {
            if (parameter.mode != ParameterMode::In) {
                fail(location,
                     "translation-time evaluation of '" + function.name +
                         "' requires only 'in' parameters");
                --depth_;
                return std::nullopt;
            }
        }
        call_stack_.push_back({function.name, location});
        const auto previous_base = frame_base_;
        frame_base_ = scopes_.size();
        scopes_.emplace_back();
        bool valid = true;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto& parameter = function.parameters[index];
            auto value = convert(arguments[index], parameter.type, location);
            if (!value) { valid = false; break; }
            scopes_.back()[name_key(parameter)] = {
                *value, true, parameter.type->is_const};
        }
        const auto previous = current_function_;
        current_function_ = &function;
        auto flow = valid ? statement(*function.body) : Flow{Flow::Failed};
        current_function_ = previous;
        scopes_.pop_back();
        frame_base_ = previous_base;
        if (flow.kind != Flow::Return || !flow.value) {
            fail(location,
                 "translation-time call to '" + function.name +
                     "' did not produce a value");
            call_stack_.pop_back();
            --depth_;
            return std::nullopt;
        }
        auto result = convert(*flow.value, function.return_type, location);
        call_stack_.pop_back();
        --depth_;
        return result;
    }

    std::optional<EvalValue> expression(const Expr& expression) {
        if (!step(expression.location)) return std::nullopt;
        switch (expression.kind) {
        case Expr::Kind::Quote: {
            if (!procedural_ || expression.quote_fragments.size() != expression.arguments.size() + 1) {
                fail(expression.location, "$::quote requires procedural macro execution");
                return std::nullopt;
            }
            TokenSequence result;
            for (std::size_t index = 0; index < expression.quote_fragments.size(); ++index) {
                auto literal = expression.quote_fragments[index];
                for (auto& token : literal) {
                    token.origin = {token_origin(macro_context_->invocation).span,
                                    {}, macro_context_, {}, 0};
                }
                if (!append_tokens(result, literal, expression.location))
                    return std::nullopt;
                if (index == expression.arguments.size()) break;
                auto value = this->expression(*expression.arguments[index]);
                if (!value || !value->tokens) {
                    fail(expression.arguments[index]->location, "$::unquote requires a token value");
                    return std::nullopt;
                }
                if (!append_tokens(result, *value->tokens, expression.arguments[index]->location))
                    return std::nullopt;
            }
            return token_value(std::move(result));
        }
        case Expr::Kind::ByteSequence:
            fail(expression.location, "materialized bytes are not an expression");
            return std::nullopt;
        case Expr::Kind::Integer: {
            auto value = parse_integer_value(expression);
            if (value && !expression.evaluated_integer) {
                const auto type = integer_type(value->type);
                if (type.is_signed ? !fits_signed_positive(value->integer, type.bits)
                                   : !fits_unsigned(value->integer, type.bits)) value.reset();
            }
            if (!value) fail(expression.location, "integer literal is not representable in its type");
            return value;
        }
        case Expr::Kind::Floating: {
            auto value = parse_floating_value(expression, program_.address_bits);
            if (!value) fail(expression.location,
                             "floating literal is invalid or not representable");
            return value;
        }
        case Expr::Kind::Character: {
            const auto decoded = decode_character_literal(expression.text);
            if (!decoded) {
                fail(expression.location, "invalid character literal");
                return std::nullopt;
            }
            return EvalValue{UInt128{*decoded},
                             builtin_type(BuiltinType::U32)};
        }
        case Expr::Kind::String: {
            auto decoded = expression.string_value.empty()
                               ? decode_string_literal(expression.text)
                               : std::optional<std::string>(
                                     expression.string_value);
            if (!decoded) return std::nullopt;
            decoded->push_back('\0');
            return EvalValue{{}, pointer_type(builtin_type(BuiltinType::U8, true)),
                             std::make_shared<std::string>(std::move(*decoded)), 0};
        }
        case Expr::Kind::Name:
            return lookup(expression);
        case Expr::Kind::Address: {
            if (!pointer_resolver_ || !expression.evaluated_address) {
                fail(expression.location,
                     "a runtime address cannot be inspected during translation-time execution");
                return std::nullopt;
            }
            EvalValue value{UInt128{}, expression.type};
            value.address = expression.evaluated_address;
            return value;
        }
        case Expr::Kind::Parenthesized:
            return expression.left ? this->expression(*expression.left)
                                   : std::nullopt;
        case Expr::Kind::Cast: {
            auto value = expression.left
                             ? this->expression(*expression.left)
                             : std::nullopt;
            return value && expression.type
                       ? convert(*value, expression.type, expression.location, true)
                       : std::nullopt;
        }
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof: {
            const auto type = expression.type
                                  ? expression.type
                                  : expression.left
                                        ? expression_type(*expression.left, false)
                                        : TypePtr{};
            const auto* query = expression.kind == Expr::Kind::Sizeof
                                    ? size_of_
                                    : align_of_;
            if (type && (type->kind == Type::Kind::Tokens ||
                         type->kind == Type::Kind::Bytes ||
                         type->kind == Type::Kind::Buffer)) {
                fail(expression.location, type_name(type) + " has no runtime size or alignment");
                return std::nullopt;
            }
            if (!type || !query) {
                fail(expression.location,
                     "target layout is unavailable for this translation-time query");
                return std::nullopt;
            }
            const auto value = (*query)(type);
            if (!value) {
                fail(expression.location,
                     expression.kind == Expr::Kind::Sizeof
                         ? "sizeof requires a complete object type with fixed size"
                         : "$::alignof requires a complete object type");
                return std::nullopt;
            }
            return EvalValue{UInt128{*value},
                             builtin_type(BuiltinType::Uptr)};
        }
        case Expr::Kind::Unary:
            return unary(expression);
        case Expr::Kind::Binary:
            return binary(expression);
        case Expr::Kind::Conditional: {
            const auto type = expression_type(expression);
            auto condition = this->expression(*expression.left);
            if (!condition || condition->pointer() || condition->tokens ||
                condition->bytes || condition->buffer || !type) return std::nullopt;
            auto value = this->expression(*(condition->truthy()
                                          ? expression.right
                                          : expression.third));
            return value ? convert(*value, type, expression.location) : std::nullopt;
        }
        case Expr::Kind::Assign:
            return assign(expression);
        case Expr::Kind::Call:
            return call_expression(expression);
        case Expr::Kind::AggregateInitializer:
            fail(expression.location,
                 "an aggregate initializer is not a scalar expression");
            return std::nullopt;
        }
        return std::nullopt;
    }

private:
    static bool contains_tokens(const TypePtr& type) {
        return type && (type->kind == Type::Kind::Tokens ||
            contains_tokens(type->pointee) || contains_tokens(type->element) ||
            (type->function && (contains_tokens(type->function->result) ||
                std::any_of(type->function->parameters.begin(), type->function->parameters.end(),
                    [](const ParameterDecl& parameter) { return contains_tokens(parameter.type); }))));
    }

    static EvalValue token_value(TokenSequence tokens) {
        EvalValue result{UInt128{}, tokens_type()};
        result.tokens = std::make_shared<const TokenSequence>(std::move(tokens));
        return result;
    }

    std::optional<std::vector<std::pair<std::size_t, std::size_t>>>
    token_trees(const TokenSequence& tokens, SourceLocation location) {
        std::vector<std::pair<std::size_t, std::size_t>> trees;
        std::vector<std::string_view> closers;
        std::size_t begin{};
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (closers.empty()) begin = index;
            const auto& spelling = tokens[index].text;
            if (spelling == "(") closers.push_back(")");
            else if (spelling == "[") closers.push_back("]");
            else if (spelling == "[[") closers.push_back("]]");
            else if (spelling == "{") closers.push_back("}");
            else if (spelling == ")" || spelling == "]" ||
                     spelling == "]]" || spelling == "}") {
                if (closers.empty() || closers.back() != spelling) {
                    fail(location, "token sequence requires balanced token groups");
                    return std::nullopt;
                }
                closers.pop_back();
            }
            if (closers.empty()) trees.emplace_back(begin, index + 1);
        }
        if (!closers.empty()) {
            fail(location, "token sequence requires balanced token groups");
            return std::nullopt;
        }
        return trees;
    }

    bool append_tokens(TokenSequence& result, const TokenSequence& part, SourceLocation location) {
        // Charge materialized output, including intermediate copies, so a
        // bounded loop cannot grow token storage exponentially without limit.
        const auto budget = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        // Logical metadata charge, not sizeof(MetaToken): resource decisions
        // must not depend on the host C++ library's string/pointer layout.
        constexpr std::size_t metadata_cost = 128;
        for (const auto& token : part) {
            if (token.text.size() > budget - token_bytes_ ||
                metadata_cost > budget - token_bytes_ - token.text.size()) {
                fail(location, "translation-time token construction budget exceeded " +
                    std::to_string(budget) + " bytes");
                return false;
            }
            const auto memory_budget = static_cast<std::size_t>(program_.evaluation_limits.memory);
            if (token.text.size() + metadata_cost >
                memory_budget - meta_bytes_ - token_bytes_) {
                fail(location, "translation-time meta memory budget exceeded " +
                    std::to_string(memory_budget) + " bytes");
                return false;
            }
            token_bytes_ += token.text.size() + metadata_cost;
        }
        result.insert(result.end(), part.begin(), part.end());
        return true;
    }

    bool charge_meta_bytes(std::size_t size, SourceLocation location) {
        const auto budget = static_cast<std::size_t>(program_.evaluation_limits.memory);
        if (meta_bytes_ > budget - token_bytes_ ||
            size > budget - token_bytes_ - meta_bytes_) {
            fail(location, "translation-time meta memory budget exceeded " +
                std::to_string(budget) + " bytes");
            return false;
        }
        meta_bytes_ += size;
        return true;
    }

    std::optional<TokenSequence> parse_tokens(std::string_view text, SourceLocation location) {
        // Lex at the string boundary without retaining pointers into this
        // temporary source. Diagnostics belong to the actual macro expression.
        if (text.find('\0') != std::string_view::npos) {
            fail(location, "$::meta::parse string contains a zero byte");
            return std::nullopt;
        }
        SourceFile source("<meta::parse>", std::string(text));
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto tokens = Lexer(source, diagnostics).lex();
        if (diagnostics.errors() != 0) {
            fail(location, "$::meta::parse could not tokenize its string");
            return std::nullopt;
        }
        std::vector<std::string_view> closers;
        for (const auto& token : tokens) {
            if (token.is("(")) closers.push_back(")");
            else if (token.is("[")) closers.push_back("]");
            else if (token.is("[[")) closers.push_back("]]");
            else if (token.is("{")) closers.push_back("}");
            else if (token.is(")") || token.is("]") || token.is("]]") || token.is("}")) {
                if (closers.empty() || closers.back() != token.text) {
                    fail(location, "$::meta::parse requires balanced token groups");
                    return std::nullopt;
                }
                closers.pop_back();
            }
        }
        if (!closers.empty()) {
            fail(location, "$::meta::parse requires balanced token groups");
            return std::nullopt;
        }
        TokenSequence result;
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::End) break;
            MetaToken value(token);
            value.origin = {token_origin(macro_context_->invocation).span,
                            {}, macro_context_, {}, 0};
            result.push_back(std::move(value));
        }
        return result;
    }

    const FunctionDecl* direct_function(const Expr& expression) {
        const Expr* node = &expression;
        while (node->kind == Expr::Kind::Parenthesized && node->left)
            node = node->left.get();
        if (node->kind == Expr::Kind::Unary && node->text == "&" && node->left) {
            node = node->left.get();
            while (node->kind == Expr::Kind::Parenthesized && node->left)
                node = node->left.get();
        }
        if (node->kind != Expr::Kind::Name || lookup_mutable(node->text, node->location) ||
            resolve_object(program_, current_function_, *node)) return nullptr;
        return resolve_function(program_, current_function_, *node,
                                [](const FunctionDecl&) { return true; });
    }

    std::unique_ptr<Expr> value_expression(const EvalValue& value, SourceLocation location) {
        auto result = std::make_unique<Expr>();
        result->location = location;
        result->type = clone_type(value.type);
        if (value.address) {
            result->kind = Expr::Kind::Address;
            result->evaluated_address = value.address;
        } else if (!value.pointer() && is_integer(value.type)) {
            result->kind = Expr::Kind::Integer;
            result->evaluated_integer = {value.integer, value.type->builtin};
            result->text = to_decimal(value.integer);
        } else {
            fail(location, "translation-time object pointer cannot escape into a runtime address");
            return {};
        }
        return result;
    }

    std::optional<EvalValue> resolve_pointer(std::unique_ptr<Expr> source,
                                            const TypePtr& destination) {
        if (!source || !pointer_resolver_) return std::nullopt;
        std::vector<NameKey> locals;
        for (auto index = frame_base_; index < scopes_.size(); ++index)
            for (const auto& [name, cell] : scopes_[index]) locals.push_back(name);
        if (!(*pointer_resolver_)(source, destination, current_function_, locals)) {
            fail(source->location, "address operation failed during translation-time evaluation");
            return std::nullopt;
        }
        EvalValue result{UInt128{}, destination};
        result.address = source->evaluated_address;
        return result;
    }

    // Evaluate only pointer bases and integer indices of an address designator.
    // Its designated cell is never read, nor can an automatic cell escape.
    std::unique_ptr<Expr> address_designator(const Expr& node) {
        if (node.kind == Expr::Kind::Name) {
            if (lookup_mutable(node.text, node.location)) {
                fail(node.location, "translation-time automatic object address cannot escape");
                return {};
            }
            return clone_expr(node);
        }
        auto result = clone_expr(node);
        if (node.kind == Expr::Kind::Parenthesized && node.left) {
            result->left = address_designator(*node.left);
            return result->left ? std::move(result) : nullptr;
        }
        if (node.kind == Expr::Kind::Unary && node.text == "*" && node.left) {
            const auto base = expression(*node.left);
            if (!base) return {};
            result->left = value_expression(*base, node.left->location);
            return result->left ? std::move(result) : nullptr;
        }
        if (node.kind == Expr::Kind::Binary && node.left && node.right) {
            if (node.text == "member") {
                result->left = address_designator(*node.left);
                return result->left ? std::move(result) : nullptr;
            }
            if (node.text == "index" || node.text == "pointer_member") {
                const auto type = expression_type(*node.left, false);
                if (node.text == "index" && type && type->kind == Type::Kind::Array) {
                    result->left = address_designator(*node.left);
                } else {
                    auto base = expression(*node.left);
                    if (!base) return {};
                    result->left = value_expression(*base, node.left->location);
                }
                if (!result->left) return {};
                if (node.text == "index") {
                    const auto index = expression(*node.right);
                    if (!index) return {};
                    result->right = value_expression(*index, node.right->location);
                    if (!result->right) return {};
                }
                return result;
            }
        }
        fail(node.location, "unsupported translation-time address designator");
        return {};
    }

    struct Cell {
        EvalValue value;
        bool initialized{};
        bool read_only{};
    };

    const RecordMemberDecl* selected_record_member(
        const Expr& expression, TypePtr* owner_type = nullptr) {
        if (expression.kind == Expr::Kind::Parenthesized &&
            expression.left) {
            return selected_record_member(*expression.left, owner_type);
        }
        if (expression.kind != Expr::Kind::Binary ||
            (expression.text != "member" &&
             expression.text != "pointer_member") ||
            !expression.left || !expression.right ||
            expression.right->kind != Expr::Kind::Name) {
            return nullptr;
        }
        auto owner = expression_type(*expression.left);
        if (expression.text == "pointer_member") {
            if (!owner || owner->kind != Type::Kind::Pointer ||
                !owner->pointee) {
                return nullptr;
            }
            owner = owner->pointee;
        }
        if (!owner || owner->kind != Type::Kind::Record) return nullptr;
        const auto record = std::find_if(
            program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.name == owner->nominal_name &&
                       candidate.complete;
            });
        if (record == program_.records.end()) return nullptr;
        const auto member = std::find_if(
            record->members.begin(), record->members.end(),
            [&](const RecordMemberDecl& candidate) {
                return candidate.name == expression.right->text;
            });
        if (member == record->members.end()) return nullptr;
        if (owner_type) *owner_type = std::move(owner);
        return &*member;
    }

    // Required folding must not discard malformed syntax in an untaken arm.
    // This validates the scalar expression boundary without executing calls
    // or arithmetic (division by zero in a short-circuited arm is permitted).
    bool validate_required_tree(const Expr& node) {
        if (procedural_ && node.kind == Expr::Kind::Quote) {
            for (const auto& argument : node.arguments) {
                if (!validate_required_tree(*argument)) return false;
                const auto type = expression_type(*argument);
                if (!type || type->kind != Type::Kind::Tokens) {
                    fail(argument->location, "$::unquote requires a token value");
                    return false;
                }
            }
            return true;
        }
        if (node.kind == Expr::Kind::Sizeof ||
            node.kind == Expr::Kind::Alignof) {
            const auto queried = node.type ? node.type
                : node.left ? expression_type(*node.left, false) : nullptr;
            if (queried && (queried->kind == Type::Kind::Bytes ||
                            queried->kind == Type::Kind::Buffer)) {
                fail(node.location, type_name(queried) + " has no runtime size or alignment");
                return false;
            }
            if (procedural_) {
                const auto type = node.type ? node.type
                    : node.left ? expression_type(*node.left, false) : nullptr;
                if (!type || contains_tokens(type)) {
                    fail(node.location, "layout query requires a runtime object type, not tokens");
                    return false;
                }
            }
            if (node.type) return true;
            if (node.left) {
                if (const auto* member =
                        selected_record_member(*node.left);
                    member && member->bit_width) {
                    fail(node.location,
                         node.kind == Expr::Kind::Sizeof
                             ? "sizeof cannot be applied to a bit-field"
                             : "$::alignof cannot be applied to a bit-field");
                    return false;
                }
            }
            if (!node.left || !expression_type(*node.left)) {
                fail(node.location,
                     "layout query has an unresolved expression type");
                return false;
            }
            return true;
        }
        if (node.kind == Expr::Kind::Call) {
            if (!node.left || node.left->kind != Expr::Kind::Name) {
                fail(node.location, "indirect calls in required constant expressions are not implemented yet");
                return false;
            }
            const auto& name = node.left->text;
            if (name == "$::embed") {
                if (node.arguments.size() != 1 ||
                    node.arguments.front()->kind != Expr::Kind::String ||
                    !token_origin(node.left->location).embed) {
                    fail(node.location, "$::embed requires one identified string-literal path");
                    return false;
                }
                return true;
            }
            if (name == "$::meta::len" || name == "$::meta::at" ||
                name == "$::meta::slice" || name == "$::meta::concat") {
                const auto count = name == "$::meta::len" ? 1U
                    : name == "$::meta::at" || name == "$::meta::concat" ? 2U : 3U;
                if (node.arguments.size() != count) {
                    fail(node.location, name + " requires " + std::to_string(count) + " arguments");
                    return false;
                }
                if (!validate_required_tree(*node.arguments.front())) return false;
                const auto sequence = expression_type(*node.arguments.front());
                if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                                  !(procedural_ && sequence->kind == Type::Kind::Tokens))) {
                    fail(node.arguments.front()->location,
                        name + " requires $::meta::bytes or $::meta::tokens");
                    return false;
                }
                for (std::size_t index = 1; index < count; ++index) {
                    const auto& argument = *node.arguments[index];
                    if (!validate_required_tree(argument)) return false;
                    const auto type = expression_type(argument);
                    const bool sequence_argument = name == "$::meta::concat";
                    if (!type || (sequence_argument
                            ? type->kind != sequence->kind : !is_integer(type))) {
                        fail(argument.location, name +
                            (sequence_argument ? (sequence->kind == Type::Kind::Tokens
                                    ? " requires token values"
                                    : " requires two values of the same meta-sequence type")
                                               : " requires an integer index"));
                        return false;
                    }
                }
                return true;
            }
            if (name == "$::meta::alloc" || name == "$::meta::cap" ||
                name == "$::meta::freeze") {
                const auto count = name == "$::meta::freeze" ? 2U : 1U;
                if (node.arguments.size() != count) {
                    fail(node.location, name + " requires " +
                        std::to_string(count) + " arguments");
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index) {
                    const auto& argument = *node.arguments[index];
                    if (!validate_required_tree(argument)) return false;
                    const auto type = expression_type(argument);
                    const bool integer = name == "$::meta::alloc" || index == 1;
                    if (!type || (integer ? !is_integer(type)
                                          : type->kind != Type::Kind::Buffer)) {
                        fail(argument.location, name +
                            (integer ? " requires an integer capacity or length"
                                     : " requires $::meta::buffer"));
                        return false;
                    }
                }
                return true;
            }
            if (name == "$::meta::data") {
                if (node.arguments.size() != 1U ||
                    !validate_required_tree(*node.arguments.front())) {
                    fail(node.location, "$::meta::data requires one meta-byte value");
                    return false;
                }
                const auto type = expression_type(*node.arguments.front());
                if (!type || (type->kind != Type::Kind::Bytes &&
                              type->kind != Type::Kind::Buffer)) {
                    fail(node.arguments.front()->location,
                        "$::meta::data requires $::meta::bytes or $::meta::buffer");
                    return false;
                }
                return true;
            }
            if (name.starts_with("$::meta::") &&
                !(procedural_ && (name == "$::meta::parse" ||
                                  name == "$::meta::concat"))) {
                fail(node.location, name + " is not implemented for byte evaluation");
                return false;
            }
            if (procedural_ && node.left->text == "$::meta::parse") {
                if (node.arguments.size() != 1U) {
                    fail(node.location, "$::meta::parse requires one string argument");
                    return false;
                }
                for (const auto& argument : node.arguments) {
                    if (!validate_required_tree(*argument)) return false;
                    const auto type = expression_type(*argument);
                    const bool string = type && type->kind == Type::Kind::Pointer &&
                        type->pointee && type->pointee->kind == Type::Kind::Builtin &&
                        type->pointee->builtin == BuiltinType::U8;
                    if (!string) {
                        fail(argument->location, "$::meta::parse requires a translation-time string");
                        return false;
                    }
                }
                return true;
            }
            if (node.left->text == "$::runtime") {
                fail(node.location, "$::runtime is invalid where a translation-time value is required");
                return false;
            }
            if (node.left->text == "$::eval") {
                if (node.arguments.size() != 1) {
                    fail(node.location, "$::eval requires exactly one expression");
                    return false;
                }
                return validate_required_tree(*node.arguments.front());
            }
            const auto* function = resolve_function(program_, current_function_, *node.left,
                                                     [](const FunctionDecl&) { return true; });
            if (!function || (!function->variadic && node.arguments.size() != function->parameters.size()) ||
                (function->variadic && node.arguments.size() < function->parameters.size())) {
                fail(node.location, "required expression has an unresolved call or invalid argument count");
                return false;
            }
            for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                const auto& argument = *node.arguments[index];
                if (!validate_required_tree(argument)) return false;
                if (index >= function->parameters.size()) continue;
                const auto from = expression_type(argument);
                const auto& to = function->parameters[index].type;
                const auto pointer_compatible = [&] {
                    if (!from || !to || from->kind != Type::Kind::Pointer ||
                        to->kind != Type::Kind::Pointer || !from->pointee || !to->pointee)
                        return false;
                    if (from->address_space != to->address_space) return false;
                    if (pointer_resolver_ && direct_function(argument))
                        return resolve_pointer(clone_expr(argument), to).has_value();
                    return compatible_pointee(from->pointee, to->pointee);
                };
                const bool compatible = from && to &&
                    (((is_integer(from) || is_floating(from)) &&
                      (is_integer(to) || is_floating(to))) ||
                     same_type(from, to) || pointer_compatible());
                if (!compatible) {
                    fail(argument.location, "unsupported or incompatible argument type in required expression");
                    return false;
                }
            }
            return true;
        }
        if (node.left && !validate_required_tree(*node.left)) return false;
        const bool member = node.kind == Expr::Kind::Binary &&
            (node.text == "member" || node.text == "pointer_member");
        if (node.right && !member && !validate_required_tree(*node.right)) return false;
        if (node.third && !validate_required_tree(*node.third)) return false;
        if (node.kind == Expr::Kind::Integer) return expression(node).has_value();
        if (node.kind == Expr::Kind::Floating) return expression(node).has_value();
        if (node.kind == Expr::Kind::Character) return expression(node).has_value();
        if (node.kind == Expr::Kind::String) {
            if (decode_string_literal(node.text)) return true;
            fail(node.location, "invalid string in required expression");
            return false;
        }
        if (!expression_type(node)) {
            fail(node.location, "unresolved name or unsupported type in required constant expression");
            return false;
        }
        if (procedural_ && node.kind == Expr::Kind::Cast && node.left &&
            !macro_convertible(expression_type(*node.left), node.type)) {
            fail(node.location, "unsupported conversion in procedural macro");
            return false;
        }
        const bool pointer_unary = pointer_resolver_ && node.kind == Expr::Kind::Unary &&
            (node.text == "&" || node.text == "*");
        if (node.kind == Expr::Kind::Unary && !pointer_unary &&
            (!node.left ||
             !(is_integer(expression_type(*node.left)) ||
               is_floating(expression_type(*node.left))) ||
             (is_floating(expression_type(*node.left))
                  ? node.text != "+" && node.text != "-" && node.text != "!" &&
                    node.text != "++" && node.text != "--" &&
                    node.text != "post++" && node.text != "post--"
                  : node.text != "+" && node.text != "-" && node.text != "!" &&
                    node.text != "~" && node.text != "++" && node.text != "--" &&
                    node.text != "post++" && node.text != "post--"))) {
            fail(node.location, "unsupported unary operand in required scalar expression");
            return false;
        }
        const bool modifying = node.kind == Expr::Kind::Assign ||
            (node.kind == Expr::Kind::Unary &&
             (node.text == "++" || node.text == "--" || node.text.starts_with("post")));
        if (modifying) {
            const auto destination = node.left ? expression_type(*node.left) : nullptr;
            const bool procedural_assignment = procedural_ && node.kind == Expr::Kind::Assign &&
                node.text == "=" && destination &&
                node.right && macro_convertible(expression_type(*node.right), destination);
            if (procedural_ && node.kind == Expr::Kind::Assign && node.right &&
                !macro_convertible(expression_type(*node.right), destination)) {
                fail(node.location, "incompatible assignment type in procedural macro");
                return false;
            }
            if (!node.left || node.left->kind != Expr::Kind::Name ||
                (node.right && !procedural_assignment && !(is_integer(expression_type(*node.right)) ||
                                 is_floating(expression_type(*node.right))))) {
                fail(node.location, "unsupported assignment in required scalar expression");
                return false;
            }
            bool read_only = expression_type(*node.left)->is_const;
            if (const auto* cell = lookup_mutable(node.left->text, node.left->location)) {
                read_only = read_only || cell->read_only;
            } else if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (name_key(parameter) == name_key(*node.left))
                        read_only = read_only || parameter.type->is_const;
            }
            if (read_only) {
                fail(node.location, "cannot write a const cell");
                return false;
            }
        }
        if (node.kind == Expr::Kind::Binary || node.kind == Expr::Kind::Conditional) {
            if (procedural_ && node.kind == Expr::Kind::Conditional) {
                const auto condition = expression_type(*node.left);
                const auto yes = expression_type(*node.right);
                const auto no = expression_type(*node.third);
                if (yes && no && yes->kind == Type::Kind::Tokens && no->kind == Type::Kind::Tokens) {
                    if (is_integer(condition) || is_floating(condition)) return true;
                    fail(node.location, "procedural macro condition must be scalar");
                    return false;
                }
            }
            if (pointer_resolver_ && member) return true;
            const auto left = node.left ? expression_type(*node.left) : nullptr;
            const auto right = node.right ? expression_type(*node.right) : nullptr;
            const bool indexing = node.kind == Expr::Kind::Binary && node.text == "index";
            const bool scalar = left && right &&
                (is_integer(left) || is_floating(left)) &&
                (is_integer(right) || is_floating(right));
            const bool floating_operands = scalar &&
                (is_floating(left) || is_floating(right));
            const bool floating_operator = node.kind == Expr::Kind::Conditional ||
                node.text == "+" || node.text == "-" || node.text == "*" ||
                node.text == "/" || node.text == "==" || node.text == "!=" ||
                node.text == "<" || node.text == "<=" || node.text == ">" ||
                node.text == ">=" || node.text == "&&" || node.text == "||";
            const auto result_type = expression_type(node);
            const bool scalar_pointer = result_type &&
                result_type->kind == Type::Kind::Pointer &&
                result_type->pointee &&
                meta_scalar_type(result_type->pointee);
            const bool pointer_operation = pointer_resolver_ && result_type &&
                result_type->kind == Type::Kind::Pointer &&
                (node.kind == Expr::Kind::Conditional || node.text == "+" || node.text == "-");
            const bool meta_pointer_operation = scalar_pointer &&
                node.kind == Expr::Kind::Binary &&
                (node.text == "+" || node.text == "-") &&
                ((node.left && meta_pointer_source(*node.left)) ||
                 (node.right && meta_pointer_source(*node.right)));
            const bool meta_pointer_pair = node.kind == Expr::Kind::Binary &&
                left && right && left->kind == Type::Kind::Pointer &&
                right->kind == Type::Kind::Pointer &&
                ((node.left && meta_pointer_source(*node.left)) ||
                 (node.right && meta_pointer_source(*node.right))) &&
                (node.text == "-" || node.text == "==" || node.text == "!=" ||
                 node.text == "<" || node.text == "<=" ||
                 node.text == ">" || node.text == ">=");
            if (indexing ? (!left || left->kind != Type::Kind::Pointer ||
                            !right || !is_integer(right))
                         : (!pointer_operation && !meta_pointer_operation &&
                            !meta_pointer_pair &&
                            (!scalar || (floating_operands && !floating_operator)))) {
                fail(node.location, "unsupported operation in required scalar expression");
                return false;
            }
            if (node.kind == Expr::Kind::Conditional &&
                (!node.third || !expression_type(*node.third))) {
                fail(node.location, "unresolved conditional operand in required expression");
                return false;
            }
        }
        return true;
    }

    static bool macro_convertible(const TypePtr& from, const TypePtr& to) {
        if (!from || !to) return false;
        if (from->kind == Type::Kind::Tokens || to->kind == Type::Kind::Tokens)
            return from->kind == to->kind;
        if ((is_integer(from) || is_floating(from)) && (is_integer(to) || is_floating(to)))
            return true;
        return from->kind == Type::Kind::Pointer && to->kind == Type::Kind::Pointer &&
            from->address_space == to->address_space &&
            compatible_pointee(from->pointee, to->pointee);
    }

    bool validate_macro_statement(const Statement& node, unsigned loops, unsigned breaks) {
        if (!node.attributes.empty()) {
            fail(node.location, "statement attributes are not supported during procedural macro execution");
            return false;
        }
        if (node.kind == Statement::Kind::Goto || node.kind == Statement::Kind::Label) {
            fail(node.location, "labels and goto are not permitted during translation-time evaluation");
            return false;
        }
        if ((node.kind == Statement::Kind::Break && breaks == 0) ||
            (node.kind == Statement::Kind::Continue && loops == 0)) {
            fail(node.location, "break or continue has no enclosing control statement");
            return false;
        }
        const bool loop = node.kind == Statement::Kind::While ||
            node.kind == Statement::Kind::DoWhile || node.kind == Statement::Kind::For;
        const bool scope = node.kind == Statement::Kind::Compound || node.kind == Statement::Kind::For;
        if (scope) scopes_.emplace_back();
        const auto validate = [&]() -> bool {
            if (node.declaration) {
                const auto& declaration = *node.declaration;
                const auto& type = declaration.type;
                if (!type || type->is_volatile || type->is_atomic || declaration.storage_static ||
                    declaration.storage_register || declaration.storage_stack || declaration.location_name ||
                    !declaration.attributes.empty() || declaration.dynamic_array_bound ||
                    !(is_integer(type) || is_floating(type) || type->kind == Type::Kind::Tokens ||
                      (type->kind == Type::Kind::Pointer && type->pointee &&
                       type->pointee->kind == Type::Kind::Builtin && type->pointee->builtin == BuiltinType::U8))) {
                    fail(node.location, "procedural macro locals require ordinary scalar, string-pointer, or token cells without runtime storage qualifiers");
                    return false;
                }
                if (scopes_.back().contains(name_key(declaration))) {
                    fail(node.location, "local cell is declared more than once in the same scope");
                    return false;
                }
                scopes_.back()[name_key(declaration)] = {EvalValue{UInt128{}, type}, false, type->is_const};
                if (declaration.initializer) {
                    if (!validate_required_tree(*declaration.initializer)) return false;
                    if (!macro_convertible(expression_type(*declaration.initializer), type)) {
                        fail(node.location, "incompatible initializer type in procedural macro");
                        return false;
                    }
                }
            }
            if (node.kind == Statement::Kind::For && node.first &&
                !validate_macro_statement(*node.first, loops, breaks)) return false;
            if (node.expression && !validate_required_tree(*node.expression)) return false;
            if (node.kind == Statement::Kind::Return &&
                (!node.expression || expression_type(*node.expression)->kind != Type::Kind::Tokens)) {
                fail(node.location, "procedural macro must return a token value");
                return false;
            }
            if (node.kind == Statement::Kind::Case &&
                (!node.expression || !is_integer(expression_type(*node.expression)))) {
                fail(node.location, "case requires a translation-time integer constant");
                return false;
            }
            if (node.condition) {
                if (!validate_required_tree(*node.condition)) return false;
                const auto type = expression_type(*node.condition);
                if (!is_integer(type) && (node.kind == Statement::Kind::Switch || !is_floating(type))) {
                    fail(node.condition->location, "procedural macro condition must be scalar (integer for switch)");
                    return false;
                }
            }
            if (node.increment && !validate_required_tree(*node.increment)) return false;
            const auto child_loops = loops + (loop ? 1U : 0U);
            const auto child_breaks = breaks + (loop || node.kind == Statement::Kind::Switch ? 1U : 0U);
            if (node.kind != Statement::Kind::For && node.first &&
                !validate_macro_statement(*node.first, child_loops, child_breaks)) return false;
            if (node.second && !validate_macro_statement(*node.second, child_loops, child_breaks)) return false;
            for (const auto& child : node.statements)
                if (!validate_macro_statement(*child, child_loops, child_breaks)) return false;
            return true;
        };
        const bool valid = validate();
        if (scope) scopes_.pop_back();
        return valid;
    }

    bool meta_pointer_source(const Expr& source) {
        if (source.kind == Expr::Kind::Call && source.left &&
            source.left->kind == Expr::Kind::Name &&
            source.left->text == "$::meta::data") return true;
        if (source.kind == Expr::Kind::Name) {
            const auto* cell = lookup_mutable(source.text, source.location);
            return cell && cell->value.meta_pointer.has_value();
        }
        if (source.kind == Expr::Kind::Parenthesized ||
            source.kind == Expr::Kind::Cast)
            return source.left && meta_pointer_source(*source.left);
        if (source.kind == Expr::Kind::Binary &&
            (source.text == "+" || source.text == "-"))
            return (source.left && meta_pointer_source(*source.left)) ||
                   (source.right && meta_pointer_source(*source.right));
        return false;
    }

    TypePtr expression_type(const Expr& expression, bool decay = true) {
        switch (expression.kind) {
        case Expr::Kind::Quote: return procedural_ ? tokens_type() : nullptr;
        case Expr::Kind::Integer: {
            const auto value = parse_integer_value(expression);
            return value ? value->type : nullptr;
        }
        case Expr::Kind::Character: return builtin_type(BuiltinType::U32);
        case Expr::Kind::Name:
            if (const auto* cell = lookup_mutable(expression.text, expression.location)) return cell->value.type;
            if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (name_key(parameter) == name_key(expression)) return parameter.type;
            }
            if (const auto* object = resolve_object(program_, current_function_, expression)) {
                if (pointer_resolver_ && decay && object->type->kind == Type::Kind::Array)
                    return pointer_type(object->type->element);
                return object->type;
            }
            if (pointer_resolver_) {
                if (const auto* function = resolve_function(program_, current_function_, expression,
                        [](const FunctionDecl&) { return true; })) {
                    auto type = function_type(function->return_type, function->parameters, function->variadic);
                    return decay ? pointer_type(type) : type;
                }
            }
            if (const auto found = resolve_enumerator(
                    program_, current_function_, current_namespace_,
                    expression);
                found && found->enumerator->value) {
                return enum_type(found->enumeration->name,
                                 found->enumeration->underlying);
            }
            return {};
        case Expr::Kind::Parenthesized:
            return expression.left ? expression_type(*expression.left, decay) : nullptr;
        case Expr::Kind::Address:
            return expression.type;
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
            return builtin_type(BuiltinType::Uptr);
        case Expr::Kind::Assign:
            return expression_type(*expression.left);
        case Expr::Kind::Unary: {
            if (expression.text == "!") return builtin_type(BuiltinType::Bool);
            auto type = expression_type(*expression.left, expression.text != "&");
            if (!type) return {};
            if (pointer_resolver_ && expression.text == "&") return pointer_type(type);
            if (pointer_resolver_ && expression.text == "*" && type->kind == Type::Kind::Pointer)
                return type->pointee;
            if (is_floating(type)) return type;
            if (!is_integer(type)) return {};
            if (expression.text == "++" || expression.text == "--" || expression.text.starts_with("post")) return type;
            return builtin_integer(promote_integer(integer_type(type)));
        }
        case Expr::Kind::Binary:
        case Expr::Kind::Conditional: {
            const bool conditional = expression.kind == Expr::Kind::Conditional;
            if (!conditional &&
                (expression.text == "member" ||
                 expression.text == "pointer_member")) {
                TypePtr owner;
                const auto* member = selected_record_member(expression,
                                                            &owner);
                if (!member || !owner) return {};
                auto result = clone_type(member->type);
                result->is_const = result->is_const || owner->is_const;
                result->is_volatile = result->is_volatile ||
                                      owner->is_volatile;
                if (pointer_resolver_ && decay && result->kind == Type::Kind::Array) {
                    auto element = clone_type(result->element);
                    element->is_const = element->is_const || result->is_const;
                    element->is_volatile = element->is_volatile || result->is_volatile;
                    return pointer_type(element);
                }
                return result;
            }
            if (!conditional && expression.text == "index") {
                const auto base = expression_type(*expression.left);
                return base && base->kind == Type::Kind::Pointer ? base->pointee
                    : base && base->kind == Type::Kind::Array ? base->element : nullptr;
            }
            if (!conditional && (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == ">" || expression.text == "<=" ||
                expression.text == ">=" || expression.text == "&&" || expression.text == "||"))
                return builtin_type(BuiltinType::Bool);
            const auto left = expression_type(*(conditional ? expression.right : expression.left));
            const auto right = expression_type(*(conditional ? expression.third : expression.right));
            if (!left || !right) return {};
            if (procedural_ && conditional && left->kind == Type::Kind::Tokens &&
                right->kind == Type::Kind::Tokens) return tokens_type();
            const auto scalar_pointer = [](const TypePtr& type) {
                return type->kind == Type::Kind::Pointer && type->pointee &&
                    meta_scalar_type(type->pointee);
            };
            if (pointer_resolver_ ||
                ((scalar_pointer(left) || scalar_pointer(right)) &&
                 ((expression.left && meta_pointer_source(*expression.left)) ||
                  (expression.right && meta_pointer_source(*expression.right))))) {
                if (conditional && left->kind == Type::Kind::Pointer && right->kind == Type::Kind::Pointer &&
                    left->address_space == right->address_space) {
                    if (compatible_pointee(left->pointee, right->pointee)) return right;
                    if (compatible_pointee(right->pointee, left->pointee)) return left;
                }
                if (!conditional && (expression.text == "+" || expression.text == "-")) {
                    if (left->kind == Type::Kind::Pointer && is_integer(right)) return left;
                    if (expression.text == "+" && is_integer(left) && right->kind == Type::Kind::Pointer) return right;
                    if (expression.text == "-" && left->kind == Type::Kind::Pointer &&
                        right->kind == Type::Kind::Pointer)
                        return builtin_type(BuiltinType::Iptr);
                }
            }
            if (is_floating(left) || is_floating(right)) {
                if ((!is_integer(left) && !is_floating(left)) ||
                    (!is_integer(right) && !is_floating(right))) return {};
                if (!is_floating(left)) return right;
                if (!is_floating(right)) return left;
                const auto left_bits = left->builtin == BuiltinType::Fptr
                    ? program_.address_bits : type_bits(left);
                const auto right_bits = right->builtin == BuiltinType::Fptr
                    ? program_.address_bits : type_bits(right);
                return left_bits >= right_bits ? left : right;
            }
            if (!is_integer(left) || !is_integer(right)) return {};
            if (!conditional && (expression.text == "<<" || expression.text == ">>"))
                return arithmetic_integer_result(
                    promote_integer(integer_type(left)), left, {});
            return arithmetic_integer_result(
                common_integer_type(integer_type(left), integer_type(right)),
                left, right);
        }
        case Expr::Kind::Call:
            if (!expression.left || expression.left->kind != Expr::Kind::Name) return {};
            if (expression.left->text == "$::embed") return bytes_type();
            if (expression.left->text == "$::meta::len")
                return builtin_type(BuiltinType::Uptr);
            if (expression.left->text == "$::meta::alloc")
                return buffer_type();
            if (expression.left->text == "$::meta::cap")
                return builtin_type(BuiltinType::Uptr);
            if (expression.left->text == "$::meta::freeze")
                return bytes_type();
            if (expression.left->text == "$::meta::data") {
                if (expression.arguments.size() != 1U) return {};
                const auto sequence = expression_type(*expression.arguments.front());
                if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                                  sequence->kind != Type::Kind::Buffer)) return {};
                return pointer_type(builtin_type(BuiltinType::U8,
                    sequence->kind == Type::Kind::Bytes));
            }
            if (expression.left->text == "$::meta::at" ||
                expression.left->text == "$::meta::slice" ||
                expression.left->text == "$::meta::concat") {
                if (expression.arguments.empty()) return {};
                const auto sequence = expression_type(*expression.arguments.front());
                if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                                  sequence->kind != Type::Kind::Tokens)) return {};
                return expression.left->text == "$::meta::at" &&
                       sequence->kind == Type::Kind::Bytes
                    ? builtin_type(BuiltinType::U8) : sequence;
            }
            if (procedural_ && expression.left->text == "$::meta::parse")
                return tokens_type();
            if ((expression.left->text == "$::eval" || expression.left->text == "$::runtime") &&
                expression.arguments.size() == 1) return expression_type(*expression.arguments.front());
            if (const auto* callee = resolve_function(program_, current_function_, *expression.left,
                    [](const FunctionDecl&) { return true; })) return callee->return_type;
            return {};
        case Expr::Kind::String: return pointer_type(builtin_type(BuiltinType::U8, true));
        case Expr::Kind::ByteSequence: return bytes_type();
        case Expr::Kind::Floating:
            return builtin_type(expression.evaluated_floating
                ? expression.evaluated_floating->type
                : floating_literal_type(expression.text));
        case Expr::Kind::AggregateInitializer: return {};
        }
        return {};
    }

    IntegerType integer_type(const TypePtr& type) const {
        auto bits = type_bits(type);
        if (type && (type->kind == Type::Kind::Pointer ||
            (type->kind == Type::Kind::Builtin &&
             (type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr))))
            bits = program_.address_bits;
        return {bits, signed_value(EvalValue{UInt128{}, type}),
                type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool};
    }

    TypePtr builtin_integer(IntegerType type) const {
        if (type.is_bool) return builtin_type(BuiltinType::Bool);
        const auto kind = type.bits == 8 ? (type.is_signed ? BuiltinType::I8 : BuiltinType::U8)
                        : type.bits == 16 ? (type.is_signed ? BuiltinType::I16 : BuiltinType::U16)
                        : type.bits == 32 ? (type.is_signed ? BuiltinType::I32 : BuiltinType::U32)
                        : type.bits == 64 ? (type.is_signed ? BuiltinType::I64 : BuiltinType::U64)
                                         : (type.is_signed ? BuiltinType::I128 : BuiltinType::U128);
        return builtin_type(kind);
    }

    TypePtr arithmetic_integer_result(IntegerType shape,
                                      const TypePtr& left,
                                      const TypePtr& right) const {
        for (const auto& candidate : {left, right}) {
            if (!is_integer(candidate)) continue;
            const auto current = integer_type(candidate);
            if (current.bits >= 32 && current.bits == shape.bits &&
                current.is_signed == shape.is_signed)
                return builtin_type(candidate->builtin);
        }
        return builtin_integer(shape);
    }

    TypePtr common_floating_type(const TypePtr& left,
                                  const TypePtr& right) const {
        if (!is_floating(left)) return right;
        if (!is_floating(right)) return left;
        const auto left_bits = left->builtin == BuiltinType::Fptr
            ? program_.address_bits : type_bits(left);
        const auto right_bits = right->builtin == BuiltinType::Fptr
            ? program_.address_bits : type_bits(right);
        return left_bits >= right_bits ? left : right;
    }

    std::optional<EvalValue> convert(EvalValue value, const TypePtr& type,
                                     SourceLocation location, bool explicit_cast = false) {
        if (!type || type->is_volatile || type->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            return std::nullopt;
        }
        if (value.tokens || type->kind == Type::Kind::Tokens) {
            if (!procedural_ || !value.tokens || type->kind != Type::Kind::Tokens) {
                fail(location, "token values cannot be converted to or from runtime types");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (value.bytes || value.buffer || type->kind == Type::Kind::Bytes ||
            type->kind == Type::Kind::Buffer) {
            if ((value.bytes && type->kind != Type::Kind::Bytes) ||
                (value.buffer && type->kind != Type::Kind::Buffer) ||
                (!value.bytes && !value.buffer)) {
                fail(location, "meta values cannot be converted to runtime types or other meta types");
                return std::nullopt;
            }
            if (value.buffer && value.buffer->frozen) {
                fail(location, "buffer handle was used after freeze");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (value.meta_pointer) {
            if (type->kind != Type::Kind::Pointer) {
                fail(location, "meta data pointers cannot convert to integer or other runtime values");
                return std::nullopt;
            }
            const auto void_pointee = [](const TypePtr& pointee) {
                return pointee && pointee->kind == Type::Kind::Builtin &&
                    pointee->builtin == BuiltinType::Void;
            };
            if (!type->pointee ||
                (!meta_scalar_type(type->pointee) &&
                 !void_pointee(type->pointee)) ||
                type->pointee->is_volatile || type->pointee->is_atomic ||
                !value.type || value.type->kind != Type::Kind::Pointer ||
                !value.type->pointee ||
                (value.type->pointee->is_const && !type->pointee->is_const) ||
                type->address_space != value.type->address_space ||
                (!explicit_cast &&
                 type->pointee->builtin != value.type->pointee->builtin &&
                 !void_pointee(type->pointee) &&
                 !void_pointee(value.type->pointee))) {
                fail(location, "meta data pointers require a compatible scalar or void pointer conversion without qualifier loss");
                return std::nullopt;
            }
            if (value.meta_pointer->mutable_buffer &&
                value.meta_pointer->mutable_buffer->frozen) {
                fail(location, "buffer data pointer was used after freeze");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (pointer_resolver_ && type->kind == Type::Kind::Pointer && !value.string) {
            auto source = value_expression(value, location);
            if (!source) return std::nullopt;
            // Check a direct declaration against the destination signature,
            // leaving the registered-ABI adapter to the normal lifting pass.
            // A previously typed pointer must instead retain its ABI identity.
            if (value.function_designator) source->type.reset();
            if (explicit_cast) {
                auto cast = std::make_unique<Expr>();
                cast->kind = Expr::Kind::Cast;
                cast->location = location;
                cast->type = type;
                cast->left = std::move(source);
                source = std::move(cast);
            }
            return resolve_pointer(std::move(source), type);
        }
        if (value.address) {
            fail(location, "emitted object addresses cannot be inspected during translation-time evaluation");
            return std::nullopt;
        }
        if (value.pointer()) {
            if (type->kind == Type::Kind::Pointer) {
                if (value.type->address_space != type->address_space) {
                    fail(location, "implicit pointer conversion changes address space");
                    return std::nullopt;
                }
                if (!type->pointee || !value.type->pointee ||
                    (value.type->pointee->is_const && !type->pointee->is_const) ||
                    (value.type->pointee->is_volatile && !type->pointee->is_volatile)) {
                    fail(location, "implicit pointer conversion discards qualifiers");
                    return std::nullopt;
                }
                value.type = clone_type(type);
                return value;
            }
            if (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool)
                return EvalValue{UInt128{1}, clone_type(type)};
            return std::nullopt;
        }
        if (is_floating(type)) {
            const auto format = floating_format(type->builtin,
                                                program_.address_bits);
            if (value.floating) {
                value.floating = floating::convert(*value.floating, format);
            } else if (is_integer(value.type)) {
                const auto source = integer_type(value.type);
                value.floating = floating::from_integer(value.integer,
                    source.bits, source.is_signed, format);
            } else return std::nullopt;
            value.type = clone_type(type);
            return value;
        }
        if (value.floating) {
            if (!is_integer(type)) return std::nullopt;
            const auto target = integer_type(type);
            if (target.is_bool) {
                return EvalValue{UInt128{floating::nonzero(*value.floating)},
                                 clone_type(type)};
            }
            const auto integer = floating::to_integer(*value.floating,
                                                      target.bits, target.is_signed);
            if (!integer) {
                fail(location, "floating-to-integer conversion is out of range during translation-time evaluation");
                return std::nullopt;
            }
            return EvalValue{*integer, clone_type(type)};
        }
        if (!is_integer(value.type) || !is_integer(type)) return std::nullopt;
        value.integer = convert_integer(value.integer, integer_type(value.type), integer_type(type));
        value.type = clone_type(type);
        return value;
    }

    std::optional<EvalValue> calculate(IntegerOperation operation, EvalValue left,
                                       EvalValue right, SourceLocation location) {
        const auto left_type = left.type;
        const auto right_type = right.type;
        const bool shift = operation == IntegerOperation::ShiftLeft ||
                           operation == IntegerOperation::ShiftRight;
        const auto type = shift ? promote_integer(integer_type(left.type))
                               : common_integer_type(integer_type(left.type), integer_type(right.type));
        left.integer = convert_integer(left.integer, integer_type(left.type), type);
        if (!shift) right.integer = convert_integer(right.integer, integer_type(right.type), type);
        const auto result = checked_integer_operation(operation, left.integer, right.integer, type);
        if (result.error != IntegerError::None) {
            fail(location, result.error == IntegerError::ShiftCount
                ? "invalid shift count during translation-time evaluation"
                : result.error == IntegerError::DivisionByZero
                ? "division by zero during translation-time evaluation"
                : "signed overflow during translation-time evaluation");
            return std::nullopt;
        }
        const bool comparison = operation >= IntegerOperation::Equal;
        return EvalValue{result.value, comparison ? builtin_type(BuiltinType::Bool)
            : arithmetic_integer_result(type, left_type,
                                        shift ? TypePtr{} : right_type)};
    }

    std::optional<EvalValue> calculate_floating(std::string_view operation,
                                                EvalValue left, EvalValue right,
                                                SourceLocation location) {
        const auto result_type = common_floating_type(left.type, right.type);
        auto converted_left = convert(std::move(left), result_type, location);
        auto converted_right = convert(std::move(right), result_type, location);
        if (!converted_left || !converted_right) return std::nullopt;
        left = std::move(*converted_left);
        right = std::move(*converted_right);
        if (operation == "==" || operation == "!=" || operation == "<" ||
            operation == "<=" || operation == ">" || operation == ">=") {
            const auto comparison = operation == "==" ? floating::Comparison::Equal
                : operation == "!=" ? floating::Comparison::NotEqual
                : operation == "<" ? floating::Comparison::Less
                : operation == "<=" ? floating::Comparison::LessEqual
                : operation == ">" ? floating::Comparison::Greater
                                   : floating::Comparison::GreaterEqual;
            return EvalValue{UInt128{floating::compare(comparison,
                *left.floating, *right.floating)}, builtin_type(BuiltinType::Bool)};
        }
        if (operation != "+" && operation != "-" && operation != "*" &&
            operation != "/") return std::nullopt;
        const auto opcode = operation == "+" ? floating::Operation::Add
            : operation == "-" ? floating::Operation::Subtract
            : operation == "*" ? floating::Operation::Multiply
                                : floating::Operation::Divide;
        return EvalValue{floating::binary(opcode, *left.floating,
            *right.floating, floating_format(result_type->builtin,
                                              program_.address_bits)), result_type};
    }

    struct Flow {
        enum Kind { Normal, Return, Break, Continue, Failed } kind{Normal};
        std::optional<EvalValue> value;

        Flow() = default;
        Flow(Kind flow_kind, std::optional<EvalValue> flow_value = {})
            : kind(flow_kind), value(std::move(flow_value)) {}
    };

    struct CallFrame {
        std::string function;
        SourceLocation location;
    };

    void fail(SourceLocation location, std::string reason) {
        if (!failure_reason_.empty()) return;
        failure_location_ = location;
        failure_reason_ = std::move(reason);
        failure_trace_ = call_stack_;
    }

    bool step(SourceLocation location) {
        if (++steps_ <= program_.evaluation_limits.steps) return true;
        if (!budget_diagnosed_) {
            fail(location,
                 "translation-time instruction budget exceeded " +
                     std::to_string(program_.evaluation_limits.steps));
            budget_diagnosed_ = true;
        }
        return false;
    }

    Cell* lookup_mutable(std::string_view name, SourceLocation location) {
        for (auto index = scopes_.size(); index > frame_base_;) {
            auto& scope = scopes_[--index];
            const auto found = scope.find(NameKey(name, location));
            if (found != scope.end()) return &found->second;
        }
        return nullptr;
    }

    std::optional<EvalValue> lookup(const Expr& expression) {
        const auto& name = expression.text;
        const auto location = expression.location;
        if (auto* cell = lookup_mutable(name, location)) {
            if (!cell->initialized) {
                fail(location, "read of uninitialized value during translation-time evaluation");
                return std::nullopt;
            }
            if (cell->value.type->is_volatile || cell->value.type->is_atomic) {
                fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
                return std::nullopt;
            }
            if (cell->value.buffer && cell->value.buffer->frozen) {
                fail(location, "buffer handle was used after freeze");
                return std::nullopt;
            }
            return cell->value;
        }
        if (const auto found = resolve_enumerator(
                program_, current_function_, current_namespace_, expression);
            found && found->enumerator->value) {
            return EvalValue{
                found->enumerator->value->value,
                enum_type(found->enumeration->name,
                          found->enumeration->underlying)};
        }
        if (pointer_resolver_) {
            auto source = clone_expr(expression);
            const auto type = expression_type(*source);
            const auto* object = resolve_object(program_, current_function_, expression);
            if (object && object->type->kind != Type::Kind::Array) {
                fail(location, "runtime/static storage cannot be read during translation-time evaluation");
                return std::nullopt;
            }
            if (type && type->kind == Type::Kind::Pointer) {
                const bool function = direct_function(*source) != nullptr;
                auto value = resolve_pointer(std::move(source), type);
                if (value) value->function_designator = function;
                return value;
            }
        }
        fail(location, "expression is not a translation-time value: '" +
                       std::string(name) + "' has no value in this syntax context");
        return std::nullopt;
    }

    static bool byte_meta_type(BuiltinType type) {
        return type == BuiltinType::U8 || type == BuiltinType::I8;
    }

    static bool meta_scalar_type(const TypePtr& type) {
        if (is_integer(type)) return true;
        return type && type->kind == Type::Kind::Builtin &&
            (type->builtin == BuiltinType::F32 ||
             type->builtin == BuiltinType::F64 ||
             type->builtin == BuiltinType::F80 ||
             type->builtin == BuiltinType::F128 ||
             type->builtin == BuiltinType::Fptr);
    }

    static bool compatible_meta_type(BuiltinType stored, BuiltinType access) {
        if (stored == access) return true;
        switch (stored) {
        case BuiltinType::I16: return access == BuiltinType::U16;
        case BuiltinType::U16: return access == BuiltinType::I16;
        case BuiltinType::I32: return access == BuiltinType::U32;
        case BuiltinType::U32: return access == BuiltinType::I32;
        case BuiltinType::I64: return access == BuiltinType::U64;
        case BuiltinType::U64: return access == BuiltinType::I64;
        case BuiltinType::I128: return access == BuiltinType::U128;
        case BuiltinType::U128: return access == BuiltinType::I128;
        case BuiltinType::Iptr: return access == BuiltinType::Uptr;
        case BuiltinType::Uptr: return access == BuiltinType::Iptr;
        default: return false;
        }
    }

    std::size_t meta_scalar_size(const EvalValue& value) const {
        const auto& pointee = value.type->pointee;
        if (pointee->builtin == BuiltinType::F80)
            return program_.evaluation_layout.f80_storage_bytes;
        const auto bits = pointee->builtin == BuiltinType::Fptr ||
                pointee->builtin == BuiltinType::Iptr ||
                pointee->builtin == BuiltinType::Uptr
            ? program_.address_bits : type_bits(pointee);
        return (bits + 7U) / 8U;
    }

    bool live_meta_pointer(const EvalValue& value, SourceLocation location) {
        if (!value.meta_pointer || !value.type ||
            value.type->kind != Type::Kind::Pointer || !value.type->pointee) {
            fail(location, "invalid meta pointer value");
            return false;
        }
        if (value.meta_pointer->mutable_buffer &&
            value.meta_pointer->mutable_buffer->frozen) {
            fail(location, "buffer data pointer was used after freeze");
            return false;
        }
        return true;
    }

    bool scalar_meta_pointer(const EvalValue& value, SourceLocation location) {
        if (!live_meta_pointer(value, location)) return false;
        if (!meta_scalar_type(value.type->pointee)) {
            fail(location, "meta pointer access requires a supported scalar type");
            return false;
        }
        return true;
    }

    std::optional<std::size_t> meta_access_index(const EvalValue& base,
                                                  SourceLocation location,
                                                  bool write = false) {
        if (!scalar_meta_pointer(base, location)) return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = meta_scalar_size(base);
        if (pointer.position > pointer.view_length ||
            size > pointer.view_length - pointer.position) {
            fail(location, write ? "meta pointer write is outside its view"
                                 : "meta pointer read is outside its view");
            return std::nullopt;
        }
        const auto index = pointer.view_offset + pointer.position;
        const auto alignment = base.type->pointee->builtin == BuiltinType::F80
            ? std::max<std::size_t>(1,
                program_.evaluation_layout.f80_alignment)
            : std::max<std::size_t>(1,
                std::min<std::size_t>(size,
                    program_.evaluation_layout.natural_alignment_limit));
        if (index % alignment != 0) {
            fail(location, "misaligned meta pointer access for target scalar type");
            return std::nullopt;
        }
        return index;
    }

    std::optional<EvalValue> meta_pointer_offset(EvalValue base,
                                                 const EvalValue& index,
                                                 bool subtract,
                                                 SourceLocation location) {
        if (!scalar_meta_pointer(base, location)) return std::nullopt;
        if (!is_integer(index.type)) {
            fail(location, "meta pointer offset requires an integer");
            return std::nullopt;
        }
        const auto type = integer_type(index.type);
        const bool negative = integer_negative(index.integer, type);
        const auto magnitude = negative
            ? mask_to(negate(index.integer), type.bits) : index.integer;
        if (magnitude.high != 0 ||
            magnitude.low > std::numeric_limits<std::size_t>::max()) {
            fail(location, "meta pointer offset is outside its view");
            return std::nullopt;
        }
        const auto amount = static_cast<std::size_t>(magnitude.low);
        auto& pointer = *base.meta_pointer;
        const bool backwards = subtract != negative;
        const auto available = backwards ? pointer.position
            : pointer.view_length - pointer.position;
        const auto stride = meta_scalar_size(base);
        if (amount > available / stride) {
            fail(location, "meta pointer offset is outside its view");
            return std::nullopt;
        }
        pointer.position = backwards ? pointer.position - amount * stride
                                     : pointer.position + amount * stride;
        return base;
    }

    std::optional<EvalValue> compare_meta_pointers(const EvalValue& left,
                                                   const EvalValue& right,
                                                   std::string_view operation,
                                                   SourceLocation location) {
        if (!live_meta_pointer(left, location) ||
            !live_meta_pointer(right, location)) return std::nullopt;
        const auto& a = *left.meta_pointer;
        const auto& b = *right.meta_pointer;
        const bool same_backing = a.immutable
            ? a.immutable == b.immutable
            : a.mutable_buffer && a.mutable_buffer == b.mutable_buffer;
        const auto a_position = a.view_offset + a.position;
        const auto b_position = b.view_offset + b.position;
        if (operation == "==" || operation == "!=") {
            const bool equal = same_backing && a_position == b_position;
            return EvalValue{UInt128{operation == "==" ? equal : !equal},
                             builtin_type(BuiltinType::Bool)};
        }
        if (!scalar_meta_pointer(left, location) ||
            !scalar_meta_pointer(right, location)) return std::nullopt;
        if (!same_backing || a.view_offset != b.view_offset ||
            a.view_length != b.view_length ||
            meta_scalar_size(left) != meta_scalar_size(right) ||
            left.type->pointee->builtin != right.type->pointee->builtin ||
            left.type->address_space != right.type->address_space) {
            fail(location, "meta pointer ordering or subtraction requires one compatible view");
            return std::nullopt;
        }
        if (operation == "-") {
            const auto stride = meta_scalar_size(left);
            const auto distance = a_position > b_position
                ? a_position - b_position : b_position - a_position;
            if (distance % stride != 0) {
                fail(location, "meta pointer difference is not a whole target element");
                return std::nullopt;
            }
            const auto magnitude = UInt128{distance / stride};
            const auto result = a_position < b_position
                ? mask_to(negate(magnitude), program_.address_bits) : magnitude;
            return EvalValue{result, builtin_type(BuiltinType::Iptr)};
        }
        const bool result = operation == "<" ? a_position < b_position
            : operation == "<=" ? a_position <= b_position
            : operation == ">" ? a_position > b_position
            : a_position >= b_position;
        return EvalValue{UInt128{result}, builtin_type(BuiltinType::Bool)};
    }

    std::optional<EvalValue> read_meta_pointer(const EvalValue& base,
                                                SourceLocation location) {
        const auto index = meta_access_index(base, location);
        if (!index) return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = meta_scalar_size(base);
        const auto access_type = base.type->pointee->builtin;
        UInt128 result;
        if (pointer.mutable_buffer) {
            for (std::size_t offset = 0; offset < size; ++offset) {
                const bool f80_padding = access_type == BuiltinType::F80 &&
                    (program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                         ? offset >= size - 10 : offset < size - 10);
                if (!f80_padding &&
                    !pointer.mutable_buffer->assigned[*index + offset]) {
                    fail(location, "read of unassigned buffer byte");
                    return std::nullopt;
                }
                const auto tag = pointer.mutable_buffer->effective_type[*index + offset];
                if (!byte_meta_type(access_type) && tag != 0 &&
                    !compatible_meta_type(static_cast<BuiltinType>(tag - 1),
                                          access_type)) {
                    fail(location, "meta pointer read violates effective type");
                    return std::nullopt;
                }
            }
        }
        for (std::size_t offset = 0; offset < size; ++offset) {
            const auto raw = pointer.mutable_buffer
                ? pointer.mutable_buffer->data[*index + offset]
                : (*pointer.immutable)[*index + offset];
            const auto lane = program_.evaluation_layout.byte_order ==
                EvaluationByteOrder::Little ? offset : size - 1 - offset;
            result = bit_or(result, shift_left(
                UInt128{static_cast<unsigned char>(raw)},
                static_cast<unsigned>(lane * 8)));
        }
        if (access_type == BuiltinType::Bool && result != UInt128{} &&
            result != UInt128{1}) {
            fail(location, "invalid bool representation in meta storage");
            return std::nullopt;
        }
        if (access_type == BuiltinType::F80) {
            result = mask_to(result, 80);
            const auto exponent = (result.high >> 0) & 0x7fffU;
            if (bit(result, 63) != (exponent != 0)) {
                fail(location, "invalid f80 representation in meta storage");
                return std::nullopt;
            }
        }
        if (is_floating(base.type->pointee))
            return EvalValue{floating::Value{
                result,
                floating_format(access_type, program_.address_bits)},
                builtin_type(access_type)};
        return EvalValue{result, builtin_type(access_type)};
    }

    std::optional<EvalValue> unary(const Expr& expression) {
        if (!expression.left) return std::nullopt;
        if (expression.text == "*") {
            auto pointer = this->expression(*expression.left);
            if (pointer && pointer->meta_pointer)
                return read_meta_pointer(*pointer, expression.location);
            if (pointer_resolver_)
                fail(expression.location,
                     "runtime/static storage cannot be read during translation-time evaluation");
            return std::nullopt;
        }
        if (pointer_resolver_ && expression.text == "&") {
            auto source = clone_expr(expression);
            source->left = address_designator(*expression.left);
            if (!source->left) return std::nullopt;
            auto value = resolve_pointer(std::move(source), expression_type(expression));
            if (value) value->function_designator = direct_function(expression) != nullptr;
            return value;
        }
        if (expression.text == "++" || expression.text == "--" ||
            expression.text == "post++" || expression.text == "post--") {
            if (expression.left->kind != Expr::Kind::Name) return std::nullopt;
            auto* cell = lookup_mutable(expression.left->text, expression.left->location);
            if (!cell) return std::nullopt;
            if (cell->read_only) {
                fail(expression.location, "cannot write a const cell");
                return std::nullopt;
            }
            const auto previous = lookup(*expression.left);
            if (!previous) return std::nullopt;
            if (previous->address && pointer_resolver_) {
                auto source = std::make_unique<Expr>();
                source->kind = Expr::Kind::Binary;
                source->location = expression.location;
                source->text = expression.text == "++" || expression.text == "post++" ? "+" : "-";
                source->left = value_expression(*previous, expression.location);
                source->right = value_expression(EvalValue{UInt128{1}, builtin_type(BuiltinType::I32)}, expression.location);
                auto value = resolve_pointer(std::move(source), previous->type);
                if (!value) return std::nullopt;
                lookup_mutable(expression.left->text, expression.left->location)->value = *value;
                return expression.text.starts_with("post") ? previous : value;
            }
            if (previous->pointer() || previous->tokens || previous->bytes ||
                previous->buffer) return std::nullopt;
            auto value = previous->floating
                ? calculate_floating(expression.text == "++" || expression.text == "post++"
                                         ? "+" : "-", *previous,
                                     EvalValue{UInt128{1}, builtin_type(BuiltinType::I32)},
                                     expression.location)
                : calculate(expression.text == "++" || expression.text == "post++"
                                ? IntegerOperation::Add : IntegerOperation::Subtract,
                            *previous, EvalValue{UInt128{1}, builtin_type(BuiltinType::I32)},
                            expression.location);
            if (!value) return std::nullopt;
            value = convert(*value, previous->type, expression.location);
            if (!value) return std::nullopt;
            lookup_mutable(expression.left->text, expression.left->location)->value = *value;
            return expression.text.starts_with("post") ? previous : value;
        }
        auto value = this->expression(*expression.left);
        if (!value || value->pointer() || value->tokens || value->bytes ||
            value->buffer) return std::nullopt;
        if (value->floating) {
            if (expression.text == "+") return value;
            if (expression.text == "-") {
                value->floating = floating::negate(*value->floating);
                return value;
            }
            if (expression.text == "!") {
                return EvalValue{UInt128{!value->truthy()},
                                 builtin_type(BuiltinType::Bool)};
            }
            return std::nullopt;
        }
        value = convert(*value, builtin_integer(promote_integer(integer_type(value->type))), expression.location);
        if (!value) return std::nullopt;
        if (expression.text == "+") return value;
        if (expression.text == "-") {
            return calculate(IntegerOperation::Subtract, EvalValue{UInt128{}, value->type}, *value, expression.location);
        }
        if (expression.text == "~") {
            value->integer = mask_to(bit_not(value->integer), integer_type(value->type).bits);
            return value;
        }
        if (expression.text == "!") {
            return EvalValue{value->integer == UInt128{} ? UInt128{1} : UInt128{},
                             builtin_type(BuiltinType::Bool)};
        }
        return std::nullopt;
    }

    std::optional<EvalValue> binary(const Expr& expression) {
        auto left = this->expression(*expression.left);
        if (!left) return std::nullopt;
        if (left->tokens || left->bytes || left->buffer) {
            fail(expression.location, "meta values do not support scalar operators");
            return std::nullopt;
        }
        const auto operation = expression.text;
        if (operation == "&&" || operation == "||") {
            if (left->pointer()) return std::nullopt;
            const bool lhs = left->truthy();
            if ((operation == "&&" && !lhs) ||
                (operation == "||" && lhs)) {
                return EvalValue{{operation == "||", 0},
                                 builtin_type(BuiltinType::Bool)};
            }
            auto right = this->expression(*expression.right);
            if (!right || right->pointer() || right->tokens || right->bytes ||
                right->buffer) return std::nullopt;
            return EvalValue{{right->truthy(), 0},
                             builtin_type(BuiltinType::Bool)};
        }
        auto right = this->expression(*expression.right);
        if (!right) return std::nullopt;
        if (left->meta_pointer && right->meta_pointer &&
            (expression.text == "-" || expression.text == "==" ||
             expression.text == "!=" || expression.text == "<" ||
             expression.text == "<=" || expression.text == ">" ||
             expression.text == ">="))
            return compare_meta_pointers(*left, *right, expression.text,
                                         expression.location);
        if (right->tokens || right->bytes || right->buffer) {
            fail(expression.location, "meta values do not support scalar operators");
            return std::nullopt;
        }
        if (expression.text == "index") {
            if (left->meta_pointer) {
                auto pointer = meta_pointer_offset(*left, *right, false,
                                                   expression.location);
                return pointer ? read_meta_pointer(*pointer, expression.location)
                               : std::nullopt;
            }
            if (!left->string || right->pointer() || right->integer.high != 0) {
                if (left->address) fail(expression.location,
                    "runtime/static storage cannot be read during translation-time evaluation");
                return std::nullopt;
            }
            const auto index = left->offset +
                               static_cast<std::size_t>(right->integer.low);
            if (index >= left->string->size()) {
                fail(expression.location,
                     "translation-time pointer index is out of bounds");
                return std::nullopt;
            }
            return EvalValue{{static_cast<unsigned char>((*left->string)[index]), 0},
                             builtin_type(BuiltinType::U8)};
        }
        if ((operation == "+" || operation == "-") && left->meta_pointer)
            return meta_pointer_offset(*left, *right, operation == "-",
                                       expression.location);
        if (operation == "+" && right->meta_pointer)
            return meta_pointer_offset(*right, *left, false,
                                       expression.location);
        if (pointer_resolver_ && (left->address || right->address) &&
            (operation == "+" || operation == "-")) {
            auto source = clone_expr(expression);
            source->left = value_expression(*left, expression.left->location);
            source->right = value_expression(*right, expression.right->location);
            if (!source->left || !source->right) return std::nullopt;
            const auto type = expression_type(expression);
            if (!type || type->kind != Type::Kind::Pointer) return std::nullopt;
            return resolve_pointer(std::move(source), type);
        }
        if (left->pointer() || right->pointer()) {
            fail(expression.location, "emitted object addresses cannot be inspected during translation-time evaluation");
            return std::nullopt;
        }
        if (left->floating || right->floating) {
            return calculate_floating(operation, *left, *right,
                                      expression.location);
        }
        static constexpr std::pair<std::string_view, IntegerOperation> operations[] = {
            {"+", IntegerOperation::Add}, {"-", IntegerOperation::Subtract},
            {"*", IntegerOperation::Multiply}, {"/", IntegerOperation::Divide},
            {"%", IntegerOperation::Remainder}, {"&", IntegerOperation::And},
            {"|", IntegerOperation::Or}, {"^", IntegerOperation::Xor},
            {"<<", IntegerOperation::ShiftLeft}, {">>", IntegerOperation::ShiftRight},
            {"==", IntegerOperation::Equal}, {"!=", IntegerOperation::NotEqual},
            {"<", IntegerOperation::Less}, {"<=", IntegerOperation::LessEqual},
            {">", IntegerOperation::Greater}, {">=", IntegerOperation::GreaterEqual},
        };
        for (const auto& [token, opcode] : operations)
            if (operation == token) return calculate(opcode, *left, *right, expression.location);
        return std::nullopt;
    }

    std::optional<EvalValue> assign(const Expr& expression) {
        if (!expression.left || !expression.right) {
            return std::nullopt;
        }
        const Expr* designator = expression.left.get();
        while (designator && designator->kind == Expr::Kind::Parenthesized)
            designator = designator->left.get();
        if (designator &&
            ((designator->kind == Expr::Kind::Binary && designator->text == "index") ||
             (designator->kind == Expr::Kind::Unary && designator->text == "*"))) {
            std::optional<EvalValue> pointer;
            if (designator->kind == Expr::Kind::Binary) {
                auto base = this->expression(*designator->left);
                auto index = this->expression(*designator->right);
                if (!base || !index) return std::nullopt;
                if (base->meta_pointer)
                    pointer = meta_pointer_offset(*base, *index, false,
                                                  designator->location);
            } else {
                pointer = this->expression(*designator->left);
            }
            if (!pointer || !pointer->meta_pointer) {
                fail(designator->location,
                    "translation-time assignment requires a meta data pointer");
                return std::nullopt;
            }
            if (expression.text != "=" ||
                !scalar_meta_pointer(*pointer, designator->location) ||
                pointer->type->pointee->is_const ||
                !pointer->meta_pointer->mutable_buffer) {
                fail(designator->location,
                    "meta pointer write requires mutable scalar storage and simple assignment");
                return std::nullopt;
            }
            const auto& target = *pointer->meta_pointer;
            const auto offset = meta_access_index(*pointer, designator->location, true);
            if (!offset) return std::nullopt;
            const auto size = meta_scalar_size(*pointer);
            const auto access_type = pointer->type->pointee->builtin;
            if (!byte_meta_type(access_type)) {
                for (std::size_t index = 0; index < size; ++index) {
                    const auto tag = target.mutable_buffer->effective_type[*offset + index];
                    if (tag != 0 &&
                        !compatible_meta_type(static_cast<BuiltinType>(tag - 1),
                                              access_type)) {
                        fail(designator->location,
                            "meta pointer write violates effective type");
                        return std::nullopt;
                    }
                }
            }
            auto source = this->expression(*expression.right);
            if (!source) return std::nullopt;
            source = convert(*source, builtin_type(pointer->type->pointee->builtin),
                             expression.right->location);
            if (!source) return std::nullopt;
            const auto bits = source->floating ? source->floating->bits
                                               : source->integer;
            for (std::size_t index = 0; index < size; ++index) {
                const auto lane = program_.evaluation_layout.byte_order ==
                    EvaluationByteOrder::Little ? index : size - 1 - index;
                target.mutable_buffer->data[*offset + index] = static_cast<char>(
                    shift_right(bits,
                        static_cast<unsigned>(lane * 8)).low & 0xffU);
                target.mutable_buffer->assigned[*offset + index] = 1;
                if (!byte_meta_type(access_type))
                    target.mutable_buffer->effective_type[*offset + index] =
                        static_cast<std::uint8_t>(access_type) + 1;
            }
            return source;
        }
        if (!designator || designator->kind != Expr::Kind::Name)
            return std::nullopt;
        auto* destination = lookup_mutable(designator->text, designator->location);
        if (!destination) return std::nullopt;
        if (destination->read_only) {
            fail(expression.location, "cannot write a const cell");
            return std::nullopt;
        }
        const auto destination_type = destination->value.type;
        if (expression.text == "=") {
            auto source = this->expression(*expression.right);
            if (!source) return std::nullopt;
            source = convert(*source, destination_type, expression.location);
            if (!source) return std::nullopt;
            *lookup_mutable(designator->text, designator->location) = {*source, true, false};
            return source;
        }
        Expr binary_expression;
        binary_expression.kind = Expr::Kind::Binary;
        binary_expression.location = expression.location;
        binary_expression.text =
            expression.text.substr(0, expression.text.size() - 1);
        binary_expression.left = clone_expr(*expression.left);
        binary_expression.right = clone_expr(*expression.right);
        auto result = binary(binary_expression);
        if (!result) return std::nullopt;
        result = convert(*result, destination_type, expression.location);
        if (!result) return std::nullopt;
        *lookup_mutable(designator->text, designator->location) = {*result, true, false};
        return result;
    }

    std::optional<EvalValue> call_expression(const Expr& expression) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            return std::nullopt;
        }
        if (lookup_mutable(expression.left->text, expression.left->location)) {
            fail(expression.location, "indirect calls are not permitted during translation-time evaluation");
            return std::nullopt;
        }
        if (procedural_ && expression.left->text == "$::meta::parse") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::parse requires one string argument");
                return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = this->expression(argument);
            if (!value || !value->string) {
                fail(argument.location, "$::meta::parse requires a translation-time string");
                return std::nullopt;
            }
            TokenSequence result;
            auto part = parse_tokens(std::string_view(*value->string).substr(value->offset,
                value->string->size() - value->offset - 1), argument.location);
            if (!part || !append_tokens(result, *part, argument.location)) return std::nullopt;
            return token_value(std::move(result));
        }
        const auto& name = expression.left->text;
        const auto byte_budget = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        if (name == "$::embed") {
            const auto identity = token_origin(expression.left->location).embed;
            if (expression.arguments.size() != 1 || !identity || !identity->snapshot ||
                expression.arguments.front()->kind != Expr::Kind::String) {
                fail(expression.location, "$::embed requires one identified string-literal path");
                return std::nullopt;
            }
            auto& snapshot = *identity->snapshot;
            if (!snapshot.bytes) {
                std::ifstream input(snapshot.path, std::ios::binary | std::ios::ate);
                if (!input) {
                    fail(expression.location, "cannot read embedded asset '" + identity->written_path + "'");
                    return std::nullopt;
                }
                const auto end = input.tellg();
                if (end < 0 || static_cast<std::uint64_t>(end) > byte_budget ||
                    !fits_unsigned(UInt128{static_cast<std::uint64_t>(end)}, program_.address_bits)) {
                    fail(expression.location, "embedded asset exceeds the target uptr or " +
                        std::to_string(byte_budget) + "-byte limit");
                    return std::nullopt;
                }
                std::string data(static_cast<std::size_t>(end), '\0');
                input.seekg(0);
                if (!input || (!data.empty() && !input.read(data.data(),
                        static_cast<std::streamsize>(data.size())))) {
                    fail(expression.location, "cannot read embedded asset '" + identity->written_path + "'");
                    return std::nullopt;
                }
                snapshot.bytes = std::make_shared<const std::string>(std::move(data));
            }
            if (counted_asset_backings_.insert(snapshot.bytes.get()).second &&
                !charge_meta_bytes(snapshot.bytes->size(), expression.location))
                return std::nullopt;
            EvalValue value{UInt128{}, bytes_type()};
            value.bytes = snapshot.bytes;
            value.byte_length = value.bytes->size();
            return value;
        }
        if (name == "$::meta::len" || name == "$::meta::at" ||
            name == "$::meta::slice" || name == "$::meta::concat") {
            const auto count = name == "$::meta::len" ? 1U
                : name == "$::meta::at" || name == "$::meta::concat" ? 2U : 3U;
            if (expression.arguments.size() != count) {
                fail(expression.location, name + " requires " + std::to_string(count) + " arguments");
                return std::nullopt;
            }
            auto source = this->expression(*expression.arguments[0]);
            if (!source || (!source->bytes && !source->tokens)) {
                fail(expression.arguments[0]->location,
                     name + " requires $::meta::bytes or $::meta::tokens");
                return std::nullopt;
            }
            if (source->tokens) {
                const auto trees = token_trees(*source->tokens,
                    expression.arguments[0]->location);
                if (!trees) return std::nullopt;
                if (name == "$::meta::len")
                    return EvalValue{UInt128{trees->size()},
                                     builtin_type(BuiltinType::Uptr)};
                if (name == "$::meta::concat") {
                    auto second = this->expression(*expression.arguments[1]);
                    if (!second || !second->tokens) {
                        fail(expression.arguments[1]->location,
                             "$::meta::concat requires token values");
                        return std::nullopt;
                    }
                    TokenSequence result;
                    if (!append_tokens(result, *source->tokens, expression.location) ||
                        !append_tokens(result, *second->tokens, expression.location))
                        return std::nullopt;
                    return token_value(std::move(result));
                }
                const auto index = this->expression(*expression.arguments[1]);
                if (!index || !is_integer(index->type) ||
                    integer_negative(index->integer, integer_type(index->type)) ||
                    index->integer.high != 0 || index->integer.low > trees->size()) {
                    fail(expression.arguments[1]->location,
                         name + " index is outside the token sequence");
                    return std::nullopt;
                }
                const auto offset = static_cast<std::size_t>(index->integer.low);
                std::size_t end = offset + 1;
                if (name == "$::meta::slice") {
                    const auto length = this->expression(*expression.arguments[2]);
                    if (!length || !is_integer(length->type) ||
                        integer_negative(length->integer, integer_type(length->type)) ||
                        length->integer.high != 0 ||
                        length->integer.low > trees->size() - offset) {
                        fail(expression.arguments[2]->location,
                             "$::meta::slice length is outside the token sequence");
                        return std::nullopt;
                    }
                    end = offset + static_cast<std::size_t>(length->integer.low);
                } else if (offset == trees->size()) {
                    fail(expression.arguments[1]->location,
                         "$::meta::at index is outside the token sequence");
                    return std::nullopt;
                }
                TokenSequence result;
                if (offset != end) {
                    const auto begin_token = (*trees)[offset].first;
                    const auto end_token = (*trees)[end - 1].second;
                    TokenSequence selected(source->tokens->begin() +
                        static_cast<std::ptrdiff_t>(begin_token),
                        source->tokens->begin() + static_cast<std::ptrdiff_t>(end_token));
                    if (!append_tokens(result, selected, expression.location))
                        return std::nullopt;
                }
                return token_value(std::move(result));
            }
            if (name == "$::meta::len")
                return EvalValue{UInt128{source->byte_length}, builtin_type(BuiltinType::Uptr)};
            if (name == "$::meta::concat") {
                auto second = this->expression(*expression.arguments[1]);
                if (!second || !second->bytes) {
                    fail(expression.arguments[1]->location, name + " requires $::meta::bytes");
                    return std::nullopt;
                }
                if (second->byte_length > byte_budget - source->byte_length ||
                    !fits_unsigned(UInt128{source->byte_length + second->byte_length},
                                   program_.address_bits)) {
                    fail(expression.location, "concatenated bytes exceed the target uptr or " +
                        std::to_string(byte_budget) + "-byte limit");
                    return std::nullopt;
                }
                if (!charge_meta_bytes(source->byte_length + second->byte_length,
                                       expression.location)) return std::nullopt;
                auto result = std::make_shared<std::string>();
                result->reserve(source->byte_length + second->byte_length);
                result->append(*source->bytes, source->byte_offset, source->byte_length);
                result->append(*second->bytes, second->byte_offset, second->byte_length);
                EvalValue value{UInt128{}, bytes_type()};
                value.bytes = std::move(result);
                value.byte_length = value.bytes->size();
                return value;
            }
            const auto index = this->expression(*expression.arguments[1]);
            if (!index || !is_integer(index->type) ||
                integer_negative(index->integer, integer_type(index->type)) ||
                index->integer.high != 0 || index->integer.low > source->byte_length) {
                fail(expression.arguments[1]->location, name + " index is outside the byte sequence");
                return std::nullopt;
            }
            const auto offset = static_cast<std::size_t>(index->integer.low);
            if (name == "$::meta::at") {
                if (offset == source->byte_length) {
                    fail(expression.arguments[1]->location, "$::meta::at index is outside the byte sequence");
                    return std::nullopt;
                }
                return EvalValue{UInt128{static_cast<unsigned char>(
                    (*source->bytes)[source->byte_offset + offset])}, builtin_type(BuiltinType::U8)};
            }
            const auto length = this->expression(*expression.arguments[2]);
            if (!length || !is_integer(length->type) ||
                integer_negative(length->integer, integer_type(length->type)) ||
                length->integer.high != 0 ||
                length->integer.low > source->byte_length - offset) {
                fail(expression.arguments[2]->location, "$::meta::slice length is outside the byte sequence");
                return std::nullopt;
            }
            source->byte_offset += offset;
            source->byte_length = static_cast<std::size_t>(length->integer.low);
            return source;
        }
        if (name == "$::meta::alloc" || name == "$::meta::cap" ||
            name == "$::meta::freeze") {
            const auto count = name == "$::meta::freeze" ? 2U : 1U;
            if (expression.arguments.size() != count) {
                fail(expression.location, name + " requires " +
                    std::to_string(count) + " arguments");
                return std::nullopt;
            }
            if (name == "$::meta::alloc") {
                auto capacity = this->expression(*expression.arguments[0]);
                if (!capacity || !is_integer(capacity->type) ||
                    integer_negative(capacity->integer,
                                     integer_type(capacity->type)) ||
                    !fits_unsigned(capacity->integer, program_.address_bits) ||
                    capacity->integer.high != 0 ||
                    capacity->integer.low > byte_budget) {
                    fail(expression.arguments[0]->location,
                        "$::meta::alloc capacity exceeds target uptr or " +
                        std::to_string(byte_budget) + " bytes");
                    return std::nullopt;
                }
                if (!charge_meta_bytes(
                        static_cast<std::size_t>(capacity->integer.low) * 3,
                        expression.location)) return std::nullopt;
                auto storage = std::make_shared<EvalBuffer>();
                storage->data.resize(static_cast<std::size_t>(capacity->integer.low));
                storage->assigned.resize(storage->data.size());
                storage->effective_type.resize(storage->data.size());
                EvalValue result{UInt128{}, buffer_type()};
                result.buffer = std::move(storage);
                return result;
            }
            auto handle = this->expression(*expression.arguments[0]);
            if (!handle || !handle->buffer || handle->buffer->frozen) {
                fail(expression.arguments[0]->location,
                    "$::meta::buffer handle is invalid or was used after freeze");
                return std::nullopt;
            }
            if (name == "$::meta::cap")
                return EvalValue{UInt128{handle->buffer->data.size()},
                                 builtin_type(BuiltinType::Uptr)};
            auto length = this->expression(*expression.arguments[1]);
            if (!length || !is_integer(length->type) ||
                integer_negative(length->integer, integer_type(length->type)) ||
                length->integer.high != 0 ||
                length->integer.low > handle->buffer->data.size()) {
                fail(expression.arguments[1]->location,
                    "$::meta::freeze length exceeds buffer capacity");
                return std::nullopt;
            }
            const auto count_bytes = static_cast<std::size_t>(length->integer.low);
            if (std::find(handle->buffer->assigned.begin(),
                          handle->buffer->assigned.begin() +
                              static_cast<std::ptrdiff_t>(count_bytes), 0) !=
                handle->buffer->assigned.begin() +
                    static_cast<std::ptrdiff_t>(count_bytes)) {
                fail(expression.arguments[1]->location,
                    "$::meta::freeze requires every prefix byte to be assigned");
                return std::nullopt;
            }
            if (!charge_meta_bytes(count_bytes, expression.location))
                return std::nullopt;
            auto storage = std::make_shared<const std::string>(
                handle->buffer->data.substr(0, count_bytes));
            handle->buffer->frozen = true;
            EvalValue result{UInt128{}, bytes_type()};
            result.bytes = std::move(storage);
            result.byte_length = count_bytes;
            return result;
        }
        if (name == "$::meta::data") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::data requires one meta-byte value");
                return std::nullopt;
            }
            auto source = this->expression(*expression.arguments.front());
            if (!source || (!source->bytes && !source->buffer) ||
                (source->buffer && source->buffer->frozen)) {
                fail(expression.arguments.front()->location,
                    "$::meta::data requires live bytes or buffer storage");
                return std::nullopt;
            }
            EvalValue result{UInt128{}, pointer_type(builtin_type(BuiltinType::U8,
                source->bytes != nullptr))};
            EvalMetaPointer pointer;
            if (source->bytes) {
                pointer.immutable = source->bytes;
                pointer.view_offset = source->byte_offset;
                pointer.view_length = source->byte_length;
            } else {
                pointer.mutable_buffer = source->buffer;
                pointer.view_length = source->buffer->data.size();
            }
            result.meta_pointer = std::move(pointer);
            return result;
        }
        if (name.starts_with("$::meta::")) {
            fail(expression.location, name + " is not implemented for byte evaluation");
            return std::nullopt;
        }
        if (expression.left->text == "$::eval") {
            if (expression.arguments.size() != 1) {
                fail(expression.location,
                     "$::eval requires exactly one expression");
                return std::nullopt;
            }
            return this->expression(*expression.arguments.front());
        }
        if (expression.left->text == "$::runtime") {
            fail(expression.location,
                 "$::runtime prevents translation-time evaluation");
            return std::nullopt;
        }
        if (auto* blocked = resolve_function(
                program_, current_function_, *expression.left,
                [](const FunctionDecl& candidate) {
                    return candidate.attribute("runtime_only") != nullptr;
                })) {
            fail(expression.location,
                 "call to runtime-only function '" + blocked->name +
                     "' cannot be evaluated during translation");
            return std::nullopt;
        }
        auto* function = resolve_function(
            program_, current_function_, *expression.left,
            [](const FunctionDecl& candidate) {
                return candidate.body != nullptr &&
                       candidate.attribute("runtime_only") == nullptr &&
                       candidate.attribute("macro") == nullptr;
            });
        if (!function) {
            fail(expression.location,
                 "call has no visible translation-time implementation");
            return std::nullopt;
        }
        std::vector<EvalValue> arguments;
        for (const auto& argument : expression.arguments) {
            auto value = this->expression(*argument);
            if (!value) return std::nullopt;
            arguments.push_back(std::move(*value));
        }
        return call(*function, arguments, expression.location);
    }

    Flow statement(const Statement& statement) {
        if (!step(statement.location)) return {Flow::Failed};
        switch (statement.kind) {
        case Statement::Kind::Compound: {
            scopes_.emplace_back();
            for (const auto& child : statement.statements) {
                auto flow = this->statement(*child);
                if (flow.kind != Flow::Normal) {
                    scopes_.pop_back();
                    return flow;
                }
            }
            scopes_.pop_back();
            return {};
        }
        case Statement::Kind::Declaration: {
            if (!statement.declaration) return {Flow::Failed};
            if (statement.declaration->storage_static) {
                fail(statement.location, "runtime/static storage cannot be used during translation-time evaluation");
                return {Flow::Failed};
            }
            if (scopes_.back().contains(name_key(*statement.declaration))) {
                fail(statement.location, "local cell is declared more than once in the same scope");
                return {Flow::Failed};
            }
            if (statement.declaration->dynamic_array_bound) {
                return {Flow::Failed};
            }
            EvalValue value{UInt128{}, clone_type(statement.declaration->type)};
            scopes_.back()[name_key(*statement.declaration)] = {
                value, false, statement.declaration->type->is_const};
            if (statement.declaration->initializer) {
                auto initializer = expression(*statement.declaration->initializer);
                if (!initializer) return {Flow::Failed};
                initializer = convert(*initializer, statement.declaration->type, statement.location);
                if (!initializer) return {Flow::Failed};
                auto& cell = scopes_.back()[name_key(*statement.declaration)];
                cell.value = *initializer;
                cell.initialized = true;
            }
            return {};
        }
        case Statement::Kind::Expression:
            if (statement.expression && !expression(*statement.expression)) {
                return {Flow::Failed};
            }
            return {};
        case Statement::Kind::Return:
            return {Flow::Return,
                    statement.expression ? expression(*statement.expression)
                                         : std::optional<EvalValue>{}};
        case Statement::Kind::If: {
            auto condition = expression(*statement.condition);
            if (!condition || condition->pointer() || condition->tokens ||
                condition->bytes || condition->buffer) return {Flow::Failed};
            if (condition->truthy()) {
                return this->statement(*statement.first);
            }
            return statement.second ? this->statement(*statement.second) : Flow{};
        }
        case Statement::Kind::Switch: {
            if (!statement.condition || !statement.first)
                return {Flow::Failed};
            auto selector = expression(*statement.condition);
            if (!selector || selector->pointer() || !is_integer(selector->type))
                return {Flow::Failed};
            const auto promoted = promote_integer(integer_type(selector->type));
            selector = convert(*selector, builtin_integer(promoted),
                               statement.condition->location);
            if (!selector) return {Flow::Failed};

            const Statement* body = statement.first.get();
            std::vector<const Statement*> labels;
            const auto collect = [&](const auto& self, const Statement* node, bool nested) -> bool {
                    if (!node || node->kind == Statement::Kind::Switch) return true;
                    if (node->kind == Statement::Kind::Case ||
                        node->kind == Statement::Kind::Default) {
                        if (nested) {
                            fail(node->location, "case/default inside another control statement is not lowerable yet");
                            return false;
                        }
                        labels.push_back(node);
                        return self(self, node->first.get(), false);
                    }
                    nested |= node->kind != Statement::Kind::Compound;
                    for (const auto& child : node->statements)
                        if (!self(self, child.get(), nested)) return false;
                    return self(self, node->first.get(), nested) &&
                           self(self, node->second.get(), nested);
                };
            if (!collect(collect, body, false)) return {Flow::Failed};
            if (labels.empty()) return {};
            std::optional<std::size_t> selected;
            std::optional<std::size_t> fallback;
            std::vector<UInt128> values;
            for (std::size_t index = 0; index < labels.size(); ++index) {
                const auto* label = labels[index];
                if (label->kind == Statement::Kind::Default) {
                    if (fallback) {
                        fail(label->location, "duplicate default label in switch");
                        return {Flow::Failed};
                    }
                    fallback = index;
                    continue;
                }
                if (!label->expression) return {Flow::Failed};
                auto value = expression(*label->expression);
                if (!value || value->pointer() || !is_integer(value->type)) {
                    fail(label->location, "case requires a translation-time integer constant");
                    return {Flow::Failed};
                }
                const auto case_type = integer_type(value->type);
                const bool negative = integer_negative(value->integer, case_type);
                const auto magnitude = negative
                    ? mask_to(negate(value->integer), case_type.bits)
                    : value->integer;
                const bool representable = promoted.is_signed
                    ? (negative ? !(shift_left(UInt128{1}, promoted.bits - 1) < magnitude)
                                : fits_signed_positive(magnitude, promoted.bits))
                    : (!negative && fits_unsigned(magnitude, promoted.bits));
                if (!representable) {
                    fail(label->location, "case value is not representable in the switch type");
                    return {Flow::Failed};
                }
                value = convert(*value, builtin_integer(promoted), label->location);
                if (!value) return {Flow::Failed};
                for (const auto& prior : values) {
                    if (prior == value->integer) {
                        fail(label->location, "duplicate case value in switch");
                        return {Flow::Failed};
                    }
                }
                values.push_back(value->integer);
                if (value->integer == selector->integer) selected = index;
            }
            if (!selected) selected = fallback;
            if (!selected) return {};
            const auto* selected_label = labels[*selected];
            bool active = false;
            const auto execute = [&](const auto& self, const Statement* node) -> Flow {
                if (!node) return {};
                if (node == selected_label) active = true;
                if (active) return this->statement(*node);
                if (node->kind == Statement::Kind::Compound) {
                    scopes_.emplace_back();
                    Flow flow;
                    for (const auto& child : node->statements) {
                        flow = self(self, child.get());
                        if (flow.kind != Flow::Normal) break;
                    }
                    scopes_.pop_back();
                    return flow;
                }
                if (node->kind == Statement::Kind::Case || node->kind == Statement::Kind::Default)
                    return self(self, node->first.get());
                if (node->kind == Statement::Kind::Declaration && node->declaration)
                    scopes_.back()[name_key(*node->declaration)] = {
                        EvalValue{UInt128{}, clone_type(node->declaration->type)}, false,
                        node->declaration->type->is_const};
                return {};
            };
            auto flow = execute(execute, body);
            return flow.kind == Flow::Break ? Flow{} : flow;
        }
        case Statement::Kind::Case:
        case Statement::Kind::Default:
            return statement.first ? this->statement(*statement.first) : Flow{};
        case Statement::Kind::While:
            while (true) {
                auto condition = expression(*statement.condition);
                if (!condition || condition->pointer() || condition->tokens ||
                    condition->bytes || condition->buffer) return {Flow::Failed};
                if (!condition->truthy()) return {};
                auto flow = this->statement(*statement.first);
                if (flow.kind == Flow::Return || flow.kind == Flow::Failed) return flow;
                if (flow.kind == Flow::Break) return {};
            }
        case Statement::Kind::DoWhile:
            do {
                auto flow = this->statement(*statement.first);
                if (flow.kind == Flow::Return || flow.kind == Flow::Failed) return flow;
                if (flow.kind == Flow::Break) return {};
                auto condition = expression(*statement.condition);
                if (!condition || condition->pointer() || condition->tokens ||
                    condition->bytes || condition->buffer) return {Flow::Failed};
                if (!condition->truthy()) return {};
            } while (true);
        case Statement::Kind::For: {
            scopes_.emplace_back();
            auto initial = this->statement(*statement.first);
            if (initial.kind != Flow::Normal) {
                scopes_.pop_back();
                return initial;
            }
            while (true) {
                if (statement.condition) {
                    auto condition = expression(*statement.condition);
                    if (!condition || condition->pointer() || condition->tokens ||
                        condition->bytes || condition->buffer) {
                        scopes_.pop_back();
                        return {Flow::Failed};
                    }
                    if (!condition->truthy()) break;
                }
                auto flow = this->statement(*statement.second);
                if (flow.kind == Flow::Return || flow.kind == Flow::Failed) {
                    scopes_.pop_back();
                    return flow;
                }
                if (flow.kind == Flow::Break) break;
                if (statement.increment && !expression(*statement.increment)) {
                    scopes_.pop_back();
                    return {Flow::Failed};
                }
            }
            scopes_.pop_back();
            return {};
        }
        case Statement::Kind::Break: return {Flow::Break};
        case Statement::Kind::Continue: return {Flow::Continue};
        case Statement::Kind::Empty: return {};
        case Statement::Kind::Label:
        case Statement::Kind::Goto:
            fail(statement.location,
                 "labels and goto are not permitted during translation-time "
                 "evaluation");
            return {Flow::Failed};
        }
        return {Flow::Failed};
    }

    Program& program_;
    Diagnostics& diagnostics_;
    const FunctionDecl* current_function_{};
    std::string current_namespace_;
    const LayoutQuery* size_of_{};
    const LayoutQuery* align_of_{};
    const GenericPointerResolver* pointer_resolver_{};
    bool procedural_{};
    std::shared_ptr<const SyntaxContext> macro_context_;
    std::size_t token_bytes_{};
    std::size_t meta_bytes_{};
    std::unordered_set<const std::string*> counted_asset_backings_;
    std::vector<NameMap<Cell>> scopes_;
    std::size_t frame_base_{};
    std::uint64_t steps_{};
    unsigned depth_{};
    bool budget_diagnosed_{};
    SourceLocation failure_location_;
    std::string failure_reason_;
    std::vector<CallFrame> call_stack_;
    std::vector<CallFrame> failure_trace_;
};

bool signed_builtin(BuiltinType type) {
    return type == BuiltinType::I8 || type == BuiltinType::I16 ||
           type == BuiltinType::I32 || type == BuiltinType::I64 ||
           type == BuiltinType::I128 || type == BuiltinType::Iptr;
}

bool evaluate_enumerations(Program& program, Diagnostics& diagnostics) {
    std::unordered_set<std::string> names;
    for (auto& enumeration : program.enumerations) {
        const auto type = enum_type(enumeration.name, enumeration.underlying);
        std::optional<EvalValue> previous;
        for (auto& enumerator : enumeration.enumerators) {
            if (!names.insert(enumerator.name).second) {
                diagnostics.error(enumerator.location,
                                  "enumerator '" + enumerator.name +
                                      "' is declared more than once");
                continue;
            }
            std::optional<EvalValue> value;
            if (enumerator.initializer) {
                Evaluator evaluator(program, diagnostics, nullptr,
                                    namespace_prefix(enumeration.name));
                value = evaluator.required_integer(*enumerator.initializer,
                                                   type);
                if (!value) evaluator.diagnose(enumerator.location);
            } else if (!previous) {
                value = EvalValue{UInt128{}, clone_type(type)};
            } else {
                auto bits = type_bits(type);
                if (enumeration.underlying == BuiltinType::Iptr ||
                    enumeration.underlying == BuiltinType::Uptr) {
                    bits = program.address_bits;
                }
                const bool is_signed =
                    signed_builtin(enumeration.underlying);
                const auto limit = is_signed
                                       ? subtract(shift_left(UInt128{1},
                                                             bits - 1),
                                                  UInt128{1})
                                       : mask_to(bit_not(UInt128{}), bits);
                const auto incremented = checked_integer_operation(
                    IntegerOperation::Add, previous->integer, UInt128{1},
                    {bits, is_signed});
                if (previous->integer == limit ||
                    incremented.error != IntegerError::None) {
                    diagnostics.error(
                        enumerator.location,
                        "implicit enumerator value is not representable in '" +
                            type_name(type) + "'");
                } else {
                    value = EvalValue{incremented.value, clone_type(type)};
                }
            }
            if (!value) continue;
            enumerator.value = Expr::IntegerConstant{
                value->integer, enumeration.underlying};
            previous = *value;
        }
    }
    return diagnostics.errors() == 0;
}

class EnumeratorMaterializer {
public:
    explicit EnumeratorMaterializer(Program& program) : program_(program) {}

    void run() {
        for (auto& record : program_.records) {
            caller_ = nullptr;
            current_namespace_ = namespace_prefix(record.name);
            for (auto& member : record.members) {
                rewrite(member.bit_width);
            }
        }
        for (auto& assertion : program_.static_assertions) {
            caller_ = nullptr;
            current_namespace_ = assertion.source_namespace;
            rewrite(assertion.condition);
        }
        for (auto& object : program_.objects) {
            caller_ = nullptr;
            current_namespace_ = namespace_prefix(object->name);
            rewrite(object->initializer);
        }
        for (auto& function : program_.functions) {
            caller_ = function.get();
            current_namespace_ = function->source_namespace;
            scopes_.clear();
            scopes_.emplace_back();
            for (const auto& parameter : function->parameters) {
                scopes_.back().insert(name_key(parameter));
            }
            for (const auto& parameter : function->generic_parameters) {
                scopes_.back().insert(name_key(parameter));
            }
            if (function->body) rewrite(*function->body);
        }
        caller_ = nullptr;
        scopes_.clear();
    }

private:
    bool local(std::string_view name, SourceLocation location) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (scope->contains(NameKey(name, location))) return true;
        }
        return false;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name &&
            local(expression->text, expression->location)) {
            auto context = std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Local;
            expression->name_context = std::move(context);
        }
        if (expression->kind == Expr::Kind::Name &&
            !local(expression->text, expression->location) &&
            !resolve_object(program_, caller_, *expression) &&
            !resolve_function(program_, caller_, *expression,
                              [](const FunctionDecl&) { return true; })) {
            if (const auto found = resolve_enumerator(
                    program_, caller_, current_namespace_, *expression);
                found && found->enumerator->value) {
                expression->kind = Expr::Kind::Integer;
                expression->evaluated_integer = found->enumerator->value;
                expression->type = enum_type(
                    found->enumeration->name,
                    found->enumeration->underlying);
                expression->text = to_decimal(
                    found->enumerator->value->value);
                return;
            }
        }
        rewrite(expression->left);
        const bool member = expression->kind == Expr::Kind::Binary &&
            (expression->text == "member" || expression->text == "pointer_member");
        if (!member) rewrite(expression->right);
        rewrite(expression->third);
        for (auto& argument : expression->arguments) rewrite(argument);
        for (auto& argument : expression->generic_arguments) {
            rewrite(argument.value);
        }
        visit_initializer_children(
            *expression,
            [&](std::unique_ptr<Expr>& child) { rewrite(child); });
    }

    void rewrite(Statement& statement) {
        const bool scoped = statement.kind == Statement::Kind::Compound ||
                            statement.kind == Statement::Kind::For;
        if (scoped) scopes_.emplace_back();
        if (statement.declaration) {
            rewrite(statement.declaration->dynamic_array_bound);
            rewrite(statement.declaration->initializer);
            scopes_.back().insert(name_key(*statement.declaration));
        }
        rewrite(statement.expression);
        rewrite(statement.condition);
        rewrite(statement.increment);
        if (statement.first) rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
        if (scoped) scopes_.pop_back();
    }

    Program& program_;
    FunctionDecl* caller_{};
    std::string current_namespace_;
    std::vector<NameSet> scopes_;
};

void materialize_enumerators(Program& program) {
    EnumeratorMaterializer(program).run();
}

bool contains_layout_query(const Expr& expression) {
    if (expression.kind == Expr::Kind::Sizeof ||
        expression.kind == Expr::Kind::Alignof) {
        return true;
    }
    if (expression.left && contains_layout_query(*expression.left)) return true;
    if (expression.right && contains_layout_query(*expression.right)) return true;
    if (expression.third && contains_layout_query(*expression.third)) return true;
    for (const auto& argument : expression.arguments) {
        if (contains_layout_query(*argument)) return true;
    }
    for (const auto& argument : expression.generic_arguments) {
        if (argument.value && contains_layout_query(*argument.value)) {
            return true;
        }
    }
    bool found = false;
    visit_initializer_children(expression, [&](const Expr& child) {
        found = found || contains_layout_query(child);
    });
    if (found) return true;
    return false;
}

bool contains_relocation_candidate(const Expr& expression,
                                   const Program& program,
                                   std::string_view source_namespace) {
    if (expression.kind == Expr::Kind::Sizeof ||
        expression.kind == Expr::Kind::Alignof) return false;
    if (expression.kind == Expr::Kind::Address && expression.evaluated_address)
        return expression.evaluated_address->kind != AddressConstant::Kind::Absolute;
    if (expression.kind == Expr::Kind::Unary && expression.text == "&")
        return true;
    if (expression.kind == Expr::Kind::Name) {
        const auto selected = value_namespace(program, nullptr, expression, source_namespace);
        if (selected && std::any_of(program.objects.begin(), program.objects.end(),
                [&](const auto& object) {
                    return object->name == *selected && object->type &&
                           object->type->kind == Type::Kind::Array;
                })) return true;
    }
    if (expression.left &&
        contains_relocation_candidate(*expression.left, program,
                                      source_namespace)) return true;
    if (expression.right &&
        contains_relocation_candidate(*expression.right, program,
                                      source_namespace)) return true;
    if (expression.third &&
        contains_relocation_candidate(*expression.third, program,
                                      source_namespace)) return true;
    for (const auto& argument : expression.arguments)
        if (contains_relocation_candidate(*argument, program,
                                          source_namespace)) return true;
    return false;
}

std::string literal_suffix(const TypePtr& type) {
    if (!type || type->kind != Type::Kind::Builtin) return "u128";
    switch (type->builtin) {
    case BuiltinType::I8: return "i8";
    case BuiltinType::U8: return "u8";
    case BuiltinType::I16: return "i16";
    case BuiltinType::U16: return "u16";
    case BuiltinType::I32: return "i32";
    case BuiltinType::U32: return "u32";
    case BuiltinType::I64: return "i64";
    case BuiltinType::U64: return "u64";
    case BuiltinType::I128: return "i128";
    case BuiltinType::U128: return "u128";
    case BuiltinType::Iptr: return "iptr";
    case BuiltinType::Uptr: return "uptr";
    case BuiltinType::Bool: return "u8";
    default: return "u128";
    }
}

bool evaluation_only(const FunctionDecl& function) {
    if (function.attribute("eval_only")) return true;
    if (function.return_type && (function.return_type->kind == Type::Kind::Bytes ||
                                  function.return_type->kind == Type::Kind::Buffer)) return true;
    return std::any_of(function.parameters.begin(), function.parameters.end(),
        [](const ParameterDecl& parameter) {
            return parameter.type &&
                (parameter.type->kind == Type::Kind::Bytes ||
                 parameter.type->kind == Type::Kind::Buffer);
        });
}

bool runtime_only(const FunctionDecl& function) {
    return function.attribute("runtime_only") != nullptr;
}

void replace_eval_value(std::unique_ptr<Expr>& expression,
                        const EvalValue& value) {
    auto replacement = std::make_unique<Expr>();
    replacement->location = expression->location;
    if (value.floating) {
        replacement->kind = Expr::Kind::Floating;
        replacement->text = "0.0" + literal_suffix(value.type);
        replacement->evaluated_floating = Expr::FloatingConstant{
            value.floating->bits, value.type->builtin};
        expression = std::move(replacement);
        return;
    }
    replacement->kind = Expr::Kind::Integer;
    replacement->text =
        to_decimal(value.integer) + literal_suffix(value.type);
    replacement->evaluated_integer = Expr::IntegerConstant{
        value.integer, value.type->builtin};
    if (!value.type->nominal_name.empty()) {
        replacement->type = clone_type(value.type);
    }
    expression = std::move(replacement);
}

bool rewrite_required_integer(std::unique_ptr<Expr>& expression,
                              const FunctionDecl* caller, Program& program,
                              Diagnostics& diagnostics,
                              const TypePtr& destination = {}) {
    Evaluator evaluator(program, diagnostics, caller);
    const auto value = evaluator.required_integer(*expression, destination);
    if (!value) {
        evaluator.diagnose(expression->location);
        return false;
    }
    replace_eval_value(expression, *value);
    // The model receives a canonical mathematical spelling, not the source
    // spelling or the unsigned encoding of a negative signed value.
    auto width = type_bits(value->type);
    if (value->type->builtin == BuiltinType::Iptr ||
        value->type->builtin == BuiltinType::Uptr) width = program.address_bits;
    if (signed_value(*value) && bit(value->integer, width - 1)) {
        expression->text = "-" + to_decimal(mask_to(negate(value->integer), width)) +
                           literal_suffix(value->type);
    }
    return true;
}

bool rewrite_required_floating(std::unique_ptr<Expr>& expression,
                               const FunctionDecl* caller, Program& program,
                               Diagnostics& diagnostics,
                               const TypePtr& destination,
                               const LayoutQuery* size_of = nullptr,
                               const LayoutQuery* align_of = nullptr,
                               std::string_view source_namespace = {}) {
    Evaluator evaluator(program, diagnostics, caller,
                        std::string(source_namespace), size_of, align_of);
    const auto value = evaluator.required_floating(*expression, destination);
    if (!value) {
        evaluator.diagnose(expression->location);
        return false;
    }
    replace_eval_value(expression, *value);
    return true;
}

void fold_relocation_offsets(std::unique_ptr<Expr>& expression,
                             Program& program, Diagnostics& diagnostics,
                             const LayoutQuery& size_of,
                             const LayoutQuery& align_of,
                             std::string_view source_namespace) {
    if (!expression) return;
    if (expression->kind == Expr::Kind::AggregateInitializer) {
        for (auto& entry : expression->initializer_entries)
            fold_relocation_offsets(entry.value, program, diagnostics,
                                    size_of, align_of, source_namespace);
        return;
    }
    fold_relocation_offsets(expression->left, program, diagnostics,
                            size_of, align_of, source_namespace);
    fold_relocation_offsets(expression->right, program, diagnostics,
                            size_of, align_of, source_namespace);
    fold_relocation_offsets(expression->third, program, diagnostics,
                            size_of, align_of, source_namespace);
    for (auto& argument : expression->arguments)
        fold_relocation_offsets(argument, program, diagnostics,
                                size_of, align_of, source_namespace);
    if (expression->kind != Expr::Kind::Binary) return;
    const auto fold_integer = [&](std::unique_ptr<Expr>& operand) {
        Evaluator evaluator(program, diagnostics, nullptr,
                            std::string(source_namespace), &size_of,
                            &align_of);
        const auto value = evaluator.required_integer(*operand);
        if (value) replace_eval_value(operand, *value);
        else evaluator.diagnose(operand->location);
    };
    if (expression->text == "index" && expression->left &&
        expression->right &&
        contains_relocation_candidate(*expression->left, program,
                                      source_namespace)) {
        fold_integer(expression->right);
    } else if ((expression->text == "+" || expression->text == "-") &&
               expression->left && expression->right) {
        if (contains_relocation_candidate(*expression->left, program,
                                          source_namespace) &&
            !contains_relocation_candidate(*expression->right, program,
                                           source_namespace)) {
            fold_integer(expression->right);
        } else if (expression->text == "+" &&
                   contains_relocation_candidate(*expression->right, program,
                                                 source_namespace) &&
                   !contains_relocation_candidate(*expression->left, program,
                                                  source_namespace)) {
            fold_integer(expression->left);
        }
    }
}

void fold_patch_initial_offsets(std::unique_ptr<Expr>& expression,
                                Program& program, Diagnostics& diagnostics,
                                const LayoutQuery& size_of,
                                const LayoutQuery& align_of,
                                std::string_view source_namespace) {
    if (!expression) return;
    fold_patch_initial_offsets(expression->left, program, diagnostics,
                               size_of, align_of, source_namespace);
    fold_patch_initial_offsets(expression->right, program, diagnostics,
                               size_of, align_of, source_namespace);
    fold_patch_initial_offsets(expression->third, program, diagnostics,
                               size_of, align_of, source_namespace);
    for (auto& argument : expression->arguments)
        fold_patch_initial_offsets(argument, program, diagnostics,
                                   size_of, align_of, source_namespace);
    for (auto& entry : expression->initializer_entries)
        fold_patch_initial_offsets(entry.value, program, diagnostics,
                                   size_of, align_of, source_namespace);
    if (expression->kind == Expr::Kind::Call && expression->left &&
        expression->left->kind == Expr::Kind::Name &&
        expression->left->text == "$::patch" &&
        !expression->arguments.empty() && expression->arguments.front() &&
        contains_relocation_candidate(*expression->arguments.front(),
                                      program, source_namespace)) {
        fold_relocation_offsets(expression->arguments.front(), program,
                                diagnostics, size_of, align_of,
                                source_namespace);
    }
}

void fold_patch_initial_offsets(Statement& statement, Program& program,
                                Diagnostics& diagnostics,
                                const LayoutQuery& size_of,
                                const LayoutQuery& align_of,
                                std::string_view source_namespace) {
    for (auto& child : statement.statements)
        fold_patch_initial_offsets(*child, program, diagnostics, size_of,
                                   align_of, source_namespace);
    if (statement.declaration) {
        fold_patch_initial_offsets(statement.declaration->dynamic_array_bound,
                                   program, diagnostics, size_of, align_of,
                                   source_namespace);
        fold_patch_initial_offsets(statement.declaration->initializer,
                                   program, diagnostics, size_of, align_of,
                                   source_namespace);
    }
    fold_patch_initial_offsets(statement.expression, program, diagnostics,
                               size_of, align_of, source_namespace);
    fold_patch_initial_offsets(statement.condition, program, diagnostics,
                               size_of, align_of, source_namespace);
    fold_patch_initial_offsets(statement.increment, program, diagnostics,
                               size_of, align_of, source_namespace);
    if (statement.first)
        fold_patch_initial_offsets(*statement.first, program, diagnostics,
                                   size_of, align_of, source_namespace);
    if (statement.second)
        fold_patch_initial_offsets(*statement.second, program, diagnostics,
                                   size_of, align_of, source_namespace);
}

bool contains_global_label(const Statement& statement,
                           std::string_view name) {
    if (statement.kind == Statement::Kind::Label &&
        statement.global_label && statement.label_name == name) {
        return true;
    }
    for (const auto& child : statement.statements) {
        if (contains_global_label(*child, name)) return true;
    }
    return (statement.first && contains_global_label(*statement.first, name)) ||
           (statement.second && contains_global_label(*statement.second, name));
}

bool normalize_generic_label(std::unique_ptr<Expr>& value,
                             const FunctionDecl* caller, Program& program,
                             Diagnostics& diagnostics) {
    while (value && value->kind == Expr::Kind::Parenthesized && value->left) {
        auto inner = std::move(value->left);
        value = std::move(inner);
    }
    if (!value || value->kind != Expr::Kind::Name) {
        diagnostics.error(value ? value->location : SourceLocation{},
                          "generic label argument requires a visible label address constant");
        return false;
    }
    const auto separator = value->text.rfind("::");
    const auto owner_name = separator == std::string::npos
        ? caller ? caller->name : std::string{}
        : value->text.substr(0, separator);
    const auto label_name = separator == std::string::npos
        ? value->text : value->text.substr(separator + 2);
    auto* owner = resolve_function(
        program, caller, owner_name,
        [](const FunctionDecl& candidate) { return candidate.body != nullptr; });
    if (!owner) {
        owner = resolve_function(
            program, caller, owner_name,
            [](const FunctionDecl&) { return true; });
    }
    if (!owner || label_name.empty()) {
        diagnostics.error(value->location,
                          "generic label argument does not name a visible function label");
        return false;
    }
    const auto qualified = owner->name + "::" + label_name;
    const bool declared = std::any_of(
        program.global_labels.begin(), program.global_labels.end(),
        [&](const GlobalLabelDecl& label) {
            return label.qualified_name == qualified;
        });
    const bool defined = owner->body &&
        contains_global_label(*owner->body, label_name);
    if (!declared && !defined) {
        diagnostics.error(value->location,
                          "generic label argument requires a visible global label; local labels cannot be transported into an instance");
        return false;
    }
    value->text = qualified;
    value->type = builtin_type(BuiltinType::Label);
    return true;
}

bool normalize_generic_arguments(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location,
                                 const GenericExpansionState& state) {
    if (arguments.size() != generic.generic_parameters.size()) {
        diagnostics.error(location, "generic argument count does not match '" + generic.name + "'");
        return false;
    }
    TypeSubstitutions types;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& parameter = generic.generic_parameters[index];
        auto& argument = arguments[index];
        if (!parameter.value_type) {
            if (argument.type) {
                types.emplace(parameter.name, argument.type);
                continue;
            }
            diagnostics.error(location, "generic type parameter '" + parameter.name +
                                        "' requires a type argument");
            return false;
        }
        if (!argument.value) {
            diagnostics.error(location, "generic value parameter '" + parameter.name +
                                        "' requires a value argument");
            return false;
        }
        const auto value_type = clone_type(parameter.value_type, types);
        if (value_type->kind == Type::Kind::Pointer && state.pointer_resolver) {
            lift_pointer_argument_strings(program, argument.value, caller);
            if (!state.pointer_resolver(argument.value, value_type, caller,
                                         state.locals)) return false;
            continue;
        }
        if (value_type->kind == Type::Kind::Builtin &&
            value_type->builtin == BuiltinType::Label) {
            if (!normalize_generic_label(argument.value, caller, program,
                                         diagnostics)) return false;
            continue;
        }
        if (!is_integer(value_type)) {
            diagnostics.error(location,
                value_type->kind == Type::Kind::Pointer
                    ? "pointer-valued generic argument normalization is not implemented yet"
                    : "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
            return false;
        }
        if (!rewrite_required_integer(argument.value, caller, program, diagnostics,
                                      value_type)) return false;
    }
    return true;
}

void rewrite_patch_sink_indices(std::unique_ptr<Expr>& expression,
                                const FunctionDecl* caller,
                                Program& program,
                                Diagnostics& diagnostics) {
    if (!expression) return;
    if (expression->kind == Expr::Kind::Parenthesized) {
        rewrite_patch_sink_indices(expression->left, caller, program,
                                   diagnostics);
        return;
    }
    if (expression->kind != Expr::Kind::Binary) return;
    if (expression->text == "member") {
        rewrite_patch_sink_indices(expression->left, caller, program,
                                   diagnostics);
        return;
    }
    if (expression->text != "index") return;
    rewrite_patch_sink_indices(expression->left, caller, program,
                               diagnostics);
    if (expression->right) {
        if (contains_layout_query(*expression->right)) return;
        (void)rewrite_required_integer(
            expression->right, caller, program, diagnostics,
            builtin_type(BuiltinType::Uptr));
    }
}

void rewrite_eval_expr(std::unique_ptr<Expr>& expression,
                       FunctionDecl* caller, Program& program,
                       Diagnostics& diagnostics,
                       bool opportunistic,
                       bool required_context = false,
                       bool runtime_context = false) {
    if (!expression) return;

    if (expression->kind == Expr::Kind::Call && expression->left &&
        expression->left->kind == Expr::Kind::Name &&
        expression->left->text == "$::patch" &&
        expression->arguments.size() == 2) {
        rewrite_patch_sink_indices(expression->arguments[1], caller, program,
                                   diagnostics);
    }

    if (expression->kind == Expr::Kind::AggregateInitializer) {
        for (auto& entry : expression->initializer_entries) {
            for (auto& designator : entry.designators) {
                if (designator.index) {
                    (void)rewrite_required_integer(
                        designator.index, caller, program, diagnostics,
                        builtin_type(BuiltinType::Uptr));
                }
            }
            rewrite_eval_expr(entry.value, caller, program, diagnostics,
                              opportunistic, required_context,
                              runtime_context);
        }
        return;
    }

    const auto direct_builtin_call = [&](std::string_view name) {
        return expression->kind == Expr::Kind::Call &&
               expression->left &&
               expression->left->kind == Expr::Kind::Name &&
               expression->left->text == name;
    };

    // A runtime wrapper is a staging barrier for its complete operand. Remove
    // the marker before HIR construction, but deliberately do not visit the
    // enclosed expression in this pass.
    if (direct_builtin_call("$::runtime")) {
        if (expression->arguments.size() != 1) {
            diagnostics.error(expression->location,
                              "$::runtime requires exactly one expression");
            return;
        }
        if (required_context) {
            diagnostics.error(
                expression->location,
                "$::runtime is invalid where a translation-time value is "
                "required");
            return;
        }
        expression = std::move(expression->arguments.front());
        rewrite_eval_expr(expression, caller, program, diagnostics, false,
                          false, true);
        return;
    }

    // Forced evaluation owns the complete operand. The evaluator recursively
    // interprets visible ordinary functions, so nested calls need no separate
    // source rewrite first.
    if (direct_builtin_call("$::eval")) {
        if (runtime_context) {
            diagnostics.error(
                expression->location,
                "$::eval cannot appear inside a $::runtime expression");
            return;
        }
        if (expression->arguments.size() != 1) {
            diagnostics.error(expression->location,
                              "$::eval requires exactly one expression");
            return;
        }
        Evaluator evaluator(program, diagnostics, caller);
        auto value = evaluator.expression(*expression->arguments.front());
        if (!value) {
            evaluator.diagnose(expression->location);
            return;
        }
        if (value->pointer()) {
            diagnostics.error(
                expression->location,
                "a translation-time pointer cannot escape through $::eval");
            return;
        }
        if (value->bytes || value->buffer) {
            diagnostics.error(expression->location,
                "meta byte values cannot enter runtime expressions");
            return;
        }
        replace_eval_value(expression, *value);
        return;
    }

    if (expression->kind == Expr::Kind::Call && expression->left &&
        expression->left->kind == Expr::Kind::Name &&
        (expression->left->text == "$::embed" ||
         expression->left->text == "$::meta::len" ||
         expression->left->text == "$::meta::at" ||
         expression->left->text == "$::meta::slice" ||
         expression->left->text == "$::meta::concat" ||
         expression->left->text == "$::meta::data" ||
         expression->left->text == "$::meta::alloc" ||
         expression->left->text == "$::meta::cap" ||
         expression->left->text == "$::meta::freeze")) {
        Evaluator evaluator(program, diagnostics, caller);
        const auto value = evaluator.required_scalar(*expression);
        if (!value) evaluator.diagnose(expression->location);
        else replace_eval_value(expression, *value);
        return;
    }

    if (expression->left) {
        rewrite_eval_expr(expression->left, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
    }
    if (expression->right) {
        rewrite_eval_expr(expression->right, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
    }
    if (expression->third) {
        rewrite_eval_expr(expression->third, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
    }
    for (auto& argument : expression->arguments) {
        rewrite_eval_expr(argument, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
    }
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        return;
    }

    auto* function = resolve_function(
        program, caller, *expression->left,
        [](const FunctionDecl& candidate) {
            return candidate.body != nullptr &&
                   candidate.attribute("macro") == nullptr;
        });
    auto* required_declaration = resolve_function(
        program, caller, *expression->left,
        [](const FunctionDecl& candidate) {
            return evaluation_only(candidate);
        });
    if (runtime_context) {
        if (required_declaration) {
            diagnostics.error(
                expression->location,
                "eval-only function '" + required_declaration->name +
                    "' cannot be called inside $::runtime");
        }
        return;
    }
    const bool required =
        (function && evaluation_only(*function)) ||
        required_declaration != nullptr || required_context;
    if ((function && runtime_only(*function) && !required) ||
        (!function && !required) ||
        (!opportunistic && !required)) {
        return;
    }

    Evaluator evaluator(program, diagnostics, caller);
    auto value = evaluator.expression(*expression);
    if (!value) {
        if (required) evaluator.diagnose(expression->location);
        return;
    }
    if (value->pointer()) {
        if (required) {
            diagnostics.error(
                expression->location,
                "an evaluation-only call cannot return a translation-time "
                "pointer");
        }
        return;
    }
    if (value->bytes || value->buffer) {
        diagnostics.error(expression->location,
            "meta byte values cannot enter runtime expressions");
        return;
    }
    replace_eval_value(expression, *value);
}

void infer_initializer_array_bound(TypePtr& type, const Expr* initializer,
                                   Diagnostics& diagnostics) {
    if (!type || type->kind != Type::Kind::Array || type->lanes != 0 ||
        !initializer ||
        initializer->kind != Expr::Kind::AggregateInitializer) {
        return;
    }
    std::uint64_t cursor{};
    std::uint64_t count{};
    for (const auto& entry : initializer->initializer_entries) {
        std::uint64_t selected = cursor;
        if (!entry.designators.empty() &&
            entry.designators.front().kind ==
                Expr::InitializerDesignator::Kind::Index &&
            entry.designators.front().index) {
            const Expr* index = entry.designators.front().index.get();
            while (index->kind == Expr::Kind::Parenthesized && index->left) {
                index = index->left.get();
            }
            if (!index->evaluated_integer ||
                index->evaluated_integer->value.high != 0) {
                diagnostics.error(
                    entry.location,
                    "array initializer designator requires a nonnegative integer constant");
                continue;
            }
            selected = index->evaluated_integer->value.low;
        }
        if (selected >= std::numeric_limits<std::uint32_t>::max()) {
            diagnostics.error(entry.location,
                              "inferred array bound exceeds the language limit");
            continue;
        }
        cursor = selected + 1;
        count = std::max(count, cursor);
    }
    if (count == 0) {
        diagnostics.error(initializer->location,
                          "an omitted array bound requires a nonempty initializer");
        return;
    }
    type->lanes = static_cast<std::uint32_t>(count);
}

TypePtr initializer_child_type(const Program& program, const TypePtr& parent,
                               const Expr::InitializerDesignator* designator,
                               std::size_t position) {
    if (!parent) return {};
    if (parent->kind == Type::Kind::Array) {
        if (designator && designator->kind !=
                              Expr::InitializerDesignator::Kind::Index) {
            return {};
        }
        return parent->element;
    }
    if (parent->kind != Type::Kind::Record) return {};
    const auto record = std::find_if(
        program.records.begin(), program.records.end(),
        [&](const RecordDecl& candidate) {
            return candidate.name == parent->nominal_name &&
                   candidate.complete;
        });
    if (record == program.records.end()) return {};
    if (designator) {
        if (designator->kind !=
            Expr::InitializerDesignator::Kind::Member) {
            return {};
        }
        const auto member = std::find_if(
            record->members.begin(), record->members.end(),
            [&](const RecordMemberDecl& candidate) {
                return candidate.name == designator->member;
            });
        return member == record->members.end() ? TypePtr{} : member->type;
    }
    std::size_t index{};
    for (const auto& member : record->members) {
        if (member.name.empty()) continue;
        if (index++ == position) return member.type;
    }
    return {};
}

void fold_static_initializer(Expr& initializer, TypePtr type,
                             Program& program, Diagnostics& diagnostics,
                             const LayoutQuery* size_of = nullptr,
                             const LayoutQuery* align_of = nullptr,
                             std::string_view source_namespace = {}) {
    if (!type ||
        initializer.kind != Expr::Kind::AggregateInitializer) {
        return;
    }
    std::size_t cursor{};
    for (auto& entry : initializer.initializer_entries) {
        const auto* first = entry.designators.empty()
                                ? nullptr
                                : &entry.designators.front();
        std::size_t selected = cursor;
        if (first && first->kind ==
                         Expr::InitializerDesignator::Kind::Index &&
            first->index && first->index->evaluated_integer &&
            first->index->evaluated_integer->value.high == 0) {
            selected = static_cast<std::size_t>(
                first->index->evaluated_integer->value.low);
        } else if (first && first->kind ==
                                Expr::InitializerDesignator::Kind::Member &&
                   type->kind == Type::Kind::Record) {
            const auto record = std::find_if(
                program.records.begin(), program.records.end(),
                [&](const RecordDecl& candidate) {
                    return candidate.name == type->nominal_name &&
                           candidate.complete;
                });
            if (record != program.records.end()) {
                const auto member = std::find_if(
                    record->members.begin(), record->members.end(),
                    [&](const RecordMemberDecl& candidate) {
                        return candidate.name == first->member;
                    });
                if (member != record->members.end()) {
                    selected = static_cast<std::size_t>(
                        std::count_if(
                            record->members.begin(), member,
                            [](const RecordMemberDecl& candidate) {
                                return !candidate.name.empty();
                            }));
                }
            }
        }
        auto destination = initializer_child_type(program, type, first,
                                                  selected);
        cursor = selected + 1;
        for (std::size_t index = first ? 1 : 0;
             destination && index < entry.designators.size(); ++index) {
            destination = initializer_child_type(
                program, destination, &entry.designators[index], 0);
        }
        if (!destination || !entry.value) continue;
        if (entry.value->kind == Expr::Kind::AggregateInitializer) {
            fold_static_initializer(*entry.value, destination, program,
                                    diagnostics, size_of, align_of,
                                    source_namespace);
        } else if (is_integer(destination)) {
            if (contains_relocation_candidate(*entry.value, program,
                                              source_namespace)) continue;
            const bool target_dependent =
                contains_layout_query(*entry.value);
            if (target_dependent && (!size_of || !align_of)) continue;
            if (!target_dependent) {
                (void)rewrite_required_integer(entry.value, nullptr, program,
                                               diagnostics);
                continue;
            }
            Evaluator evaluator(program, diagnostics, nullptr,
                                std::string(source_namespace), size_of,
                                align_of);
            const auto value = evaluator.required_integer(*entry.value);
            if (!value) {
                evaluator.diagnose(entry.value->location);
                continue;
            }
            replace_eval_value(entry.value, *value);
        } else if (is_floating(destination)) {
            if (!contains_layout_query(*entry.value) ||
                (size_of && align_of)) {
                (void)rewrite_required_floating(entry.value, nullptr,
                    program, diagnostics, destination, size_of, align_of,
                    source_namespace);
            }
        }
    }
}

void rewrite_eval_statement(Statement& statement, FunctionDecl* caller,
                            Program& program, Diagnostics& diagnostics,
                            bool opportunistic) {
    for (auto& child : statement.statements) {
        rewrite_eval_statement(*child, caller, program, diagnostics,
                               opportunistic);
    }
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound) {
            rewrite_eval_expr(statement.declaration->dynamic_array_bound,
                              caller, program, diagnostics, opportunistic);
        }
        if (statement.declaration->initializer) {
            rewrite_eval_expr(statement.declaration->initializer, caller,
                              program, diagnostics, opportunistic);
            if (!statement.declaration->dynamic_array_bound) {
                infer_initializer_array_bound(
                    statement.declaration->type,
                    statement.declaration->initializer.get(), diagnostics);
            }
        }
    }
    if (statement.expression) {
        rewrite_eval_expr(statement.expression, caller, program, diagnostics,
                          opportunistic);
        if (statement.kind == Statement::Kind::Case) {
            Evaluator evaluator(program, diagnostics, caller);
            const auto value = evaluator.expression(*statement.expression);
            if (!value || value->pointer() || !is_integer(value->type)) {
                diagnostics.error(statement.location,
                    "case requires a translation-time integer constant");
            } else {
                replace_eval_value(statement.expression, *value);
            }
        }
    }
    if (statement.condition) {
        rewrite_eval_expr(statement.condition, caller, program, diagnostics,
                          opportunistic);
    }
    if (statement.increment) {
        rewrite_eval_expr(statement.increment, caller, program, diagnostics,
                          opportunistic);
    }
    if (statement.first) {
        rewrite_eval_statement(*statement.first, caller, program, diagnostics,
                               opportunistic);
    }
    if (statement.second) {
        rewrite_eval_statement(*statement.second, caller, program, diagnostics,
                               opportunistic);
    }
}

bool expand_evaluation(Program& program, Diagnostics& diagnostics,
                       bool opportunistic) {
    const auto contains_meta = [&](const auto& self, const TypePtr& type) -> bool {
        if (!type) return false;
        if (type->kind == Type::Kind::Bytes || type->kind == Type::Kind::Buffer)
            return true;
        if (self(self, type->pointee) || self(self, type->element)) return true;
        if (type->function) {
            if (self(self, type->function->result)) return true;
            for (const auto& parameter : type->function->parameters)
                if (self(self, parameter.type)) return true;
        }
        return false;
    };
    for (auto& record : program.records) {
        for (auto& member : record.members) {
            if (contains_meta(contains_meta, member.type))
                diagnostics.error(member.location,
                    "meta values cannot be record members");
            if (!member.bit_width) continue;
            if (contains_layout_query(*member.bit_width)) {
                continue;
            }
            (void)rewrite_required_integer(member.bit_width, nullptr,
                                           program, diagnostics);
        }
    }
    for (const auto& function : program.functions) {
        if (!function->body) continue;
        std::vector<NameMap<bool>> scopes(1);
        for (const auto& parameter : function->parameters)
            scopes.back()[name_key(parameter)] = parameter.type->is_const;
        const auto check_expression = [&](const auto& self, const Expr* expression) -> void {
            if (!expression) return;
            const bool write = expression->kind == Expr::Kind::Assign ||
                (expression->kind == Expr::Kind::Unary &&
                 (expression->text == "++" || expression->text == "--" ||
                  expression->text == "post++" || expression->text == "post--"));
            const Expr* destination = expression->left.get();
            while (destination && destination->kind == Expr::Kind::Parenthesized)
                destination = destination->left.get();
            if (write && destination && destination->kind == Expr::Kind::Name) {
                std::optional<bool> read_only;
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                    const auto found = scope->find(name_key(*destination));
                    if (found != scope->end()) { read_only = found->second; break; }
                }
                if (!read_only) {
                    if (const auto* object = resolve_object(program, function.get(), *destination))
                        read_only = object->type->is_const;
                }
                if (read_only.value_or(false))
                    diagnostics.error(expression->location, "cannot write a const cell");
            }
            self(self, expression->left.get());
            self(self, expression->right.get());
            self(self, expression->third.get());
            for (const auto& argument : expression->arguments) self(self, argument.get());
            visit_initializer_children(*expression,
                [&](const Expr& child) { self(self, &child); });
        };
        const auto check_statement = [&](const auto& self, const Statement& statement) -> void {
            const bool scoped = statement.kind == Statement::Kind::Compound || statement.kind == Statement::Kind::For;
            if (scoped) scopes.emplace_back();
            if (statement.declaration) {
                if (contains_meta(contains_meta, statement.declaration->type) &&
                    (!evaluation_only(*function) ||
                     (statement.declaration->type->kind != Type::Kind::Bytes &&
                      statement.declaration->type->kind != Type::Kind::Buffer))) {
                    diagnostics.error(statement.declaration->location,
                        "meta values cannot have runtime local storage");
                }
                scopes.back()[name_key(*statement.declaration)] = statement.declaration->type->is_const;
                check_expression(check_expression, statement.declaration->initializer.get());
                check_expression(check_expression, statement.declaration->dynamic_array_bound.get());
            }
            if (statement.first) self(self, *statement.first);
            check_expression(check_expression, statement.expression.get());
            check_expression(check_expression, statement.condition.get());
            check_expression(check_expression, statement.increment.get());
            for (const auto& child : statement.statements) self(self, *child);
            if (statement.second) self(self, *statement.second);
            if (scoped) scopes.pop_back();
        };
        check_statement(check_statement, *function->body);
    }
    if (diagnostics.errors() != 0) return false;
    for (const auto& function : program.functions) {
        if (contains_meta(contains_meta, function->return_type) &&
            function->return_type->kind != Type::Kind::Bytes &&
            function->return_type->kind != Type::Kind::Buffer)
            diagnostics.error(function->location, "meta types cannot be nested in runtime function types");
        for (const auto& parameter : function->parameters) {
            if (contains_meta(contains_meta, parameter.type) &&
                parameter.type->kind != Type::Kind::Bytes &&
                parameter.type->kind != Type::Kind::Buffer)
                diagnostics.error(parameter.location, "meta types cannot be nested in runtime function types");
        }
        if (evaluation_only(*function) && runtime_only(*function)) {
            diagnostics.error(
                function->location,
                "a function cannot be both eval_only and runtime_only");
        }
        if (!evaluation_only(*function)) continue;
        if ((function->return_type &&
             (function->return_type->kind == Type::Kind::Bytes ||
              function->return_type->kind == Type::Kind::Buffer)) ||
            std::any_of(function->parameters.begin(), function->parameters.end(),
                [](const ParameterDecl& parameter) {
                    return parameter.type &&
                        (parameter.type->kind == Type::Kind::Bytes ||
                         parameter.type->kind == Type::Kind::Buffer);
                })) {
            if (function->linkage != Linkage::Static)
                diagnostics.error(function->location,
                    "function with a meta byte type in its signature must be static");
        }
        if (!function->body) {
            diagnostics.error(
                function->location,
                "eval_only requires a visible function definition");
        }
        if (function->linkage == Linkage::Global) {
            diagnostics.error(
                function->location,
                "eval_only function cannot have global linkage");
        }
        if (function->return_type &&
            function->return_type->kind == Type::Kind::Pointer) {
            diagnostics.error(function->location,
                              "eval_only function cannot return a pointer");
        }
        for (const auto& parameter : function->parameters) {
            if (parameter.mode != ParameterMode::In) {
                diagnostics.error(parameter.location,
                                  "eval_only parameters must use 'in'");
            }
        }
    }
    for (auto& function : program.functions) {
        if (evaluation_only(*function)) continue;
        if (function->body) {
            rewrite_eval_statement(*function->body, function.get(), program,
                                   diagnostics, opportunistic);
        }
    }
    for (auto& object : program.objects) {
        if (contains_meta(contains_meta, object->type)) {
            diagnostics.error(object->location,
                "meta values cannot have runtime object storage");
            continue;
        }
        if (object->initializer) {
            const bool byte_array = object->type &&
                object->type->kind == Type::Kind::Array && object->type->element &&
                object->type->element->kind == Type::Kind::Builtin &&
                object->type->element->builtin == BuiltinType::U8;
            if (byte_array && object->initializer->kind != Expr::Kind::String &&
                object->initializer->kind != Expr::Kind::AggregateInitializer) {
                Evaluator evaluator(program, diagnostics, nullptr,
                    namespace_prefix(object->name));
                auto bytes = evaluator.required_bytes(*object->initializer);
                if (!bytes) evaluator.diagnose(object->initializer->location);
                else if (bytes->empty() ||
                         bytes->size() > std::numeric_limits<std::uint32_t>::max() ||
                         !fits_unsigned(UInt128{bytes->size()}, program.address_bits)) {
                    diagnostics.error(object->initializer->location,
                        "materialized byte array has an invalid target-sized bound");
                } else if (object->type->lanes != 0 &&
                           object->type->lanes != bytes->size()) {
                    diagnostics.error(object->initializer->location,
                        "explicit byte-array bound must equal the embedded byte count");
                } else {
                    if (object->type->lanes == 0)
                        object->type->lanes = static_cast<std::uint32_t>(bytes->size());
                    auto replacement = std::make_unique<Expr>();
                    replacement->kind = Expr::Kind::ByteSequence;
                    replacement->location = object->initializer->location;
                    replacement->string_value = std::move(*bytes);
                    object->initializer = std::move(replacement);
                }
                continue;
            }
            if ((is_integer(object->type) || is_floating(object->type)) &&
                object->initializer->kind !=
                    Expr::Kind::AggregateInitializer &&
                !contains_relocation_candidate(
                    *object->initializer, program,
                    namespace_prefix(object->name))) {
                // Required initializers own the complete expression. Visiting
                // child calls first would evaluate untaken logical/conditional
                // arms and lose their short-circuit semantics.
                if (!contains_layout_query(*object->initializer)) {
                    if (is_floating(object->type)) {
                        rewrite_required_floating(object->initializer, nullptr,
                                                  program, diagnostics,
                                                  object->type);
                    } else {
                        rewrite_required_integer(object->initializer, nullptr,
                                                 program, diagnostics);
                    }
                }
            } else {
                rewrite_eval_expr(object->initializer, nullptr, program,
                                  diagnostics, opportunistic, true);
            }
            infer_initializer_array_bound(object->type,
                                          object->initializer.get(),
                                          diagnostics);
            if (object->type && object->type->kind == Type::Kind::Array &&
                object->type->lanes == 0)
                diagnostics.error(object->initializer->location,
                    "an omitted array bound requires a string, brace, or meta-byte initializer");
            fold_static_initializer(*object->initializer, object->type,
                                    program, diagnostics);
        }
    }
    program.functions.erase(
        std::remove_if(program.functions.begin(), program.functions.end(),
                       [](const auto& function) {
                           return evaluation_only(*function);
                       }),
        program.functions.end());
    for (auto& function : program.functions) {
        std::erase_if(function->attributes, [](const Attribute& attribute) {
            return attribute.name == "runtime_only";
        });
    }
    return diagnostics.errors() == 0;
}

void collect_patch_expressions(const Expr& expression,
                               std::vector<const Expr*>& patches) {
    if (expression.kind == Expr::Kind::Call && expression.left &&
        expression.left->kind == Expr::Kind::Name &&
        expression.left->text == "$::patch") {
        patches.push_back(&expression);
    }
    if (expression.left) collect_patch_expressions(*expression.left, patches);
    if (expression.right) collect_patch_expressions(*expression.right, patches);
    if (expression.third) collect_patch_expressions(*expression.third, patches);
    for (const auto& argument : expression.arguments) {
        collect_patch_expressions(*argument, patches);
    }
    visit_initializer_children(expression, [&](const Expr& child) {
        collect_patch_expressions(child, patches);
    });
}

void collect_patch_expressions(const Statement& statement,
                               std::vector<const Expr*>& patches) {
    for (const auto& child : statement.statements) {
        collect_patch_expressions(*child, patches);
    }
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound) {
            collect_patch_expressions(
                *statement.declaration->dynamic_array_bound, patches);
        }
        if (statement.declaration->initializer) {
            collect_patch_expressions(*statement.declaration->initializer,
                                      patches);
        }
    }
    if (statement.expression) {
        collect_patch_expressions(*statement.expression, patches);
    }
    if (statement.condition) {
        collect_patch_expressions(*statement.condition, patches);
    }
    if (statement.increment) {
        collect_patch_expressions(*statement.increment, patches);
    }
    if (statement.first) collect_patch_expressions(*statement.first, patches);
    if (statement.second) collect_patch_expressions(*statement.second, patches);
}

void resolve_raw_inline_expr(std::unique_ptr<Expr>& expression,
                             FunctionDecl& caller, Program& program,
                             Diagnostics& diagnostics) {
    if (!expression) return;
    if (expression->left) {
        resolve_raw_inline_expr(expression->left, caller, program, diagnostics);
    }
    if (expression->right) {
        resolve_raw_inline_expr(expression->right, caller, program, diagnostics);
    }
    if (expression->third) {
        resolve_raw_inline_expr(expression->third, caller, program, diagnostics);
    }
    for (auto& argument : expression->arguments) {
        resolve_raw_inline_expr(argument, caller, program, diagnostics);
    }
    visit_initializer_children(*expression,
        [&](std::unique_ptr<Expr>& child) {
            resolve_raw_inline_expr(child, caller, program, diagnostics);
        });
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        return;
    }
    auto* callee = resolve_function(
        program, &caller, *expression->left,
        [](const FunctionDecl& candidate) {
            return candidate.attribute("raw_inline") != nullptr;
        });
    if (!callee) return;
    if (callee->parameters.size() != expression->arguments.size()) {
        diagnostics.error(expression->location,
                          "raw_inline argument count does not match '" +
                              callee->name + "'");
        return;
    }
    // Raw-compatible calls deliberately survive as structured AST until
    // target legalization.  Canonicalizing the source name here retains the
    // frontend's namespace/import resolution while allowing the raw backend
    // to clone the complete managed body (locals and control flow included)
    // under the naked caller's resource contract.
    bind_exact_name(*expression->left, callee->name);
}

void resolve_raw_inline_statement(Statement& statement, FunctionDecl& caller,
                                  Program& program, Diagnostics& diagnostics) {
    for (auto& child : statement.statements) {
        resolve_raw_inline_statement(*child, caller, program, diagnostics);
    }
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound) {
            resolve_raw_inline_expr(statement.declaration->dynamic_array_bound,
                                    caller, program, diagnostics);
        }
        if (statement.declaration->initializer) {
            resolve_raw_inline_expr(statement.declaration->initializer,
                                    caller, program, diagnostics);
        }
    }
    if (statement.expression) {
        resolve_raw_inline_expr(statement.expression, caller, program,
                                diagnostics);
    }
    if (statement.condition) {
        resolve_raw_inline_expr(statement.condition, caller, program,
                                diagnostics);
    }
    if (statement.increment) {
        resolve_raw_inline_expr(statement.increment, caller, program,
                                diagnostics);
    }
    if (statement.first) {
        resolve_raw_inline_statement(*statement.first, caller, program,
                                     diagnostics);
    }
    if (statement.second) {
        resolve_raw_inline_statement(*statement.second, caller, program,
                                     diagnostics);
    }
}

bool expand_raw_inline(Program& program, Diagnostics& diagnostics) {
    for (const auto& function : program.functions) {
        if (function->attribute("always_inline") &&
            function->attribute("noinline")) {
            diagnostics.error(
                function->location,
                "a function cannot be both always_inline and noinline");
        }
        std::vector<const Expr*> patches;
        if (function->body) {
            collect_patch_expressions(*function->body, patches);
        }
        const auto sink_patch = std::find_if(
            patches.begin(), patches.end(), [](const Expr* expression) {
                return expression->arguments.size() == 2;
            });
        if (function->attribute("always_inline") &&
            sink_patch != patches.end()) {
            diagnostics.error(
                (*sink_patch)->location,
                "always_inline function cannot contain a sink-bearing "
                "$::patch");
        }
        if (!function->attribute("raw_inline")) continue;
        if (sink_patch != patches.end()) {
            diagnostics.error(
                (*sink_patch)->location,
                "raw_inline function cannot contain a sink-bearing "
                "$::patch");
        }
        if (function->attribute("naked")) {
            diagnostics.error(function->location,
                              "raw_inline function is managed and cannot "
                              "also be naked");
        }
        if (function->variadic) {
            diagnostics.error(function->location,
                              "raw_inline function cannot be variadic");
        }
        for (const auto& parameter : function->parameters) {
            if (parameter.mode != ParameterMode::In) {
                diagnostics.error(parameter.location,
                                  "raw_inline parameters must use 'in'");
            }
        }
    }
    for (auto& function : program.functions) {
        if ((!function->attribute("naked") &&
             !function->attribute("raw_inline")) ||
            !function->body) {
            continue;
        }
        resolve_raw_inline_statement(*function->body, *function, program,
                                     diagnostics);
    }
    return diagnostics.errors() == 0;
}

class StringPoolLifter {
public:
    explicit StringPoolLifter(Program& program)
        : program_(program), ordinal_(program.objects.size()) {}

    void pointer_argument(std::unique_ptr<Expr>& expression,
                          const FunctionDecl* caller) {
        source_unit_ = caller ? caller->source_unit : std::string{};
        rewrite(expression);
    }

    void run() {
        const auto object_count = program_.objects.size();
        for (std::size_t index = 0; index < object_count; ++index) {
            auto* object = program_.objects[index].get();
            source_unit_ = object->source_unit;
            rewrite(object->initializer, object->type);
        }
        for (auto& assertion : program_.static_assertions) {
            source_unit_.clear();
            rewrite(assertion.condition);
        }
        for (auto& function : program_.functions) {
            source_unit_ = function->source_unit;
            if (function->body) rewrite(*function->body);
        }
    }

private:
    static bool u8_array_string(const TypePtr& type,
                                const std::unique_ptr<Expr>& initializer) {
        return type && type->kind == Type::Kind::Array && type->element &&
               type->element->kind == Type::Kind::Builtin &&
               type->element->builtin == BuiltinType::U8 && initializer &&
               initializer->kind == Expr::Kind::String;
    }

    void rewrite(std::unique_ptr<Expr>& expression,
                 const TypePtr& destination = {}) {
        if (!expression) return;
        if (u8_array_string(destination, expression)) return;
        if (expression->kind == Expr::Kind::AggregateInitializer) {
            std::size_t cursor{};
            for (auto& entry : expression->initializer_entries) {
                for (auto& designator : entry.designators) {
                    rewrite(designator.index);
                }
                const auto* first = entry.designators.empty()
                                        ? nullptr
                                        : &entry.designators.front();
                std::size_t selected = cursor;
                if (first && first->kind ==
                                 Expr::InitializerDesignator::Kind::Index &&
                    first->index && first->index->evaluated_integer &&
                    first->index->evaluated_integer->value.high == 0) {
                    selected = static_cast<std::size_t>(
                        first->index->evaluated_integer->value.low);
                } else if (first && first->kind ==
                                        Expr::InitializerDesignator::Kind::Member &&
                           destination &&
                           destination->kind == Type::Kind::Record) {
                    const auto record = std::find_if(
                        program_.records.begin(), program_.records.end(),
                        [&](const RecordDecl& candidate) {
                            return candidate.name == destination->nominal_name &&
                                   candidate.complete;
                        });
                    if (record != program_.records.end()) {
                        const auto member = std::find_if(
                            record->members.begin(), record->members.end(),
                            [&](const RecordMemberDecl& candidate) {
                                return candidate.name == first->member;
                            });
                        if (member != record->members.end()) {
                            selected = static_cast<std::size_t>(
                                std::count_if(
                                    record->members.begin(), member,
                                    [](const RecordMemberDecl& candidate) {
                                        return !candidate.name.empty();
                                    }));
                        }
                    }
                }
                auto child = initializer_child_type(program_, destination,
                                                     first, selected);
                cursor = selected + 1;
                for (std::size_t index = first ? 1 : 0;
                     child && index < entry.designators.size(); ++index) {
                    child = initializer_child_type(
                        program_, child, &entry.designators[index], 0);
                }
                rewrite(entry.value, child);
            }
            return;
        }
        if (expression->kind == Expr::Kind::String) {
            const auto name = "$string." + std::to_string(ordinal_++);
            auto object = std::make_unique<ObjectDecl>();
            object->location = expression->location;
            object->name = name;
            object->source_unit = source_unit_.empty() && expression->location.file
                ? expression->location.file->source_unit_at(expression->location.line)
                : source_unit_;
            object->type = array_type(
                builtin_type(BuiltinType::U8, true),
                static_cast<std::uint32_t>(expression->string_value.size() + 1));
            object->initializer = std::move(expression);
            object->linkage = Linkage::Static;
            program_.objects.push_back(std::move(object));

            expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Name;
            expression->location = program_.objects.back()->location;
            bind_exact_name(*expression, name);
            return;
        }
        rewrite(expression->left);
        rewrite(expression->right);
        rewrite(expression->third);
        for (auto& argument : expression->arguments) rewrite(argument);
        for (auto& argument : expression->generic_arguments) {
            rewrite(argument.value);
        }
        visit_initializer_children(
            *expression,
            [&](std::unique_ptr<Expr>& child) { rewrite(child); });
    }

    void rewrite(Statement& statement) {
        if (statement.declaration) {
            rewrite(statement.declaration->dynamic_array_bound);
            rewrite(statement.declaration->initializer,
                    statement.declaration->type);
        }
        rewrite(statement.expression);
        rewrite(statement.condition);
        rewrite(statement.increment);
        if (statement.first) rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
    }

    Program& program_;
    std::string source_unit_;
    std::uint64_t ordinal_{};
};

void lift_string_literals(Program& program) {
    StringPoolLifter(program).run();
}

void lift_pointer_argument_strings(Program& program, std::unique_ptr<Expr>& expression,
                                   const FunctionDecl* caller) {
    StringPoolLifter(program).pointer_argument(expression, caller);
}

} // namespace

bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls, std::string_view mangling,
                      std::string_view default_abi,
                      const GenericPointerResolver& pointer_resolver,
                      const GenericAbiCanonicalizer& canonical_abi) {
    if (!validate_attribute_names(program, diagnostics)) return false;
    if (!evaluate_enumerations(program, diagnostics)) return false;
    materialize_enumerators(program);
    if (!expand_generics(program, diagnostics, mangling, pointer_resolver,
                         canonical_abi)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("generic expansion failed without a diagnostic");
        }
        return false;
    }
    lift_function_pointer_adapters(program, default_abi);
    if (!bind_operators(program, diagnostics)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error(
                "operator binding failed without a diagnostic");
        }
        return false;
    }
    if (!expand_evaluation(program, diagnostics, evaluate_calls)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("compile-time evaluation failed without a diagnostic");
        }
        return false;
    }
    if (!expand_raw_inline(program, diagnostics)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("raw-compatible inlining failed without a diagnostic");
        }
        return false;
    }
    lift_string_literals(program);
    return true;
}

bool finalize_target_constants(Program& program, Diagnostics& diagnostics,
                               const LayoutQuery& size_of,
                               const LayoutQuery& align_of) {
    for (auto& object : program.objects) {
        if (!object->initializer) {
            continue;
        }
        const bool relocation = contains_relocation_candidate(
            *object->initializer, program,
            namespace_prefix(object->name));
        if (relocation) {
            fold_relocation_offsets(object->initializer, program,
                                    diagnostics, size_of, align_of,
                                    namespace_prefix(object->name));
        }
        if (!contains_layout_query(*object->initializer)) continue;
        if (object->initializer->kind ==
            Expr::Kind::AggregateInitializer) {
            fold_static_initializer(
                *object->initializer, object->type, program, diagnostics,
                &size_of, &align_of, namespace_prefix(object->name));
            continue;
        }
        if (is_floating(object->type) && !relocation) {
            (void)rewrite_required_floating(object->initializer, nullptr,
                program, diagnostics, object->type, &size_of, &align_of,
                namespace_prefix(object->name));
            continue;
        }
        if (!is_integer(object->type) || relocation) continue;
        Evaluator evaluator(program, diagnostics, nullptr,
                            namespace_prefix(object->name), &size_of,
                            &align_of);
        const auto value = evaluator.required_integer(*object->initializer);
        if (!value) {
            evaluator.diagnose(object->initializer->location);
            continue;
        }
        replace_eval_value(object->initializer, *value);
    }
    for (const auto& assertion : program.static_assertions) {
        Evaluator evaluator(program, diagnostics, nullptr,
                            assertion.source_namespace, &size_of, &align_of);
        const auto value = evaluator.required_scalar(*assertion.condition);
        if (!value) {
            diagnostics.error(
                assertion.location,
                "$::static_assert condition is not a scalar constant expression");
            evaluator.diagnose(assertion.location);
            continue;
        }
        if (!value->truthy()) {
            diagnostics.error(assertion.location,
                              "$::static_assert failed: " +
                                  assertion.message);
        }
    }
    const auto align_locals = [&](auto&& self, Statement& statement,
                                  std::string_view source_namespace) -> void {
        if (statement.declaration) {
            for (const auto& attribute :
                 statement.declaration->attributes) {
                if (attribute.name != "aligned") continue;
                const auto alignment = evaluate_alignment_attribute(
                    program, attribute, diagnostics, size_of, align_of,
                    "a local object", source_namespace);
                if (alignment) {
                    statement.declaration->explicit_alignment = std::max(
                        statement.declaration->explicit_alignment,
                        *alignment);
                }
            }
        }
        if (statement.first) self(self, *statement.first, source_namespace);
        if (statement.second) self(self, *statement.second, source_namespace);
        for (auto& child : statement.statements) {
            self(self, *child, source_namespace);
        }
    };
    for (auto& function : program.functions) {
        if (function->body) {
            align_locals(align_locals, *function->body,
                         function->source_namespace);
        }
    }
    for (auto& function : program.functions) {
        if (function->body)
            fold_patch_initial_offsets(*function->body, program, diagnostics,
                                       size_of, align_of,
                                       function->source_namespace);
    }
    return diagnostics.errors() == 0;
}

std::optional<TokenSequence> evaluate_procedural_body(
    const FunctionDecl& macro, const TokenSequence& input, unsigned address_bits,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout) {
    Program context;
    context.address_bits = address_bits;
    context.evaluation_limits = limits;
    context.evaluation_layout = layout;
    const auto invocation = macro_context->invocation;
    Evaluator evaluator(context, diagnostics, &macro, macro.source_namespace,
                        &size_of, &align_of, nullptr, std::move(macro_context));
    if (!evaluator.charge_input_tokens(input, invocation)) {
        evaluator.diagnose(invocation);
        return std::nullopt;
    }
    if (!evaluator.validate_procedural_body(macro)) {
        evaluator.diagnose(macro.location);
        diagnostics.note(invocation, "while expanding procedural macro '" + macro.name + "'");
        return std::nullopt;
    }
    EvalValue argument{UInt128{}, tokens_type()};
    argument.tokens = std::make_shared<const TokenSequence>(input);
    const auto result = evaluator.call(macro, {argument}, invocation);
    if (!result || !result->tokens) {
        evaluator.diagnose(invocation);
        return std::nullopt;
    }
    return *result->tokens;
}

std::optional<Expr::IntegerConstant> evaluate_target_integer_constant(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace) {
    Evaluator evaluator(program, diagnostics, nullptr,
                        std::string(source_namespace), &size_of, &align_of);
    const auto value = evaluator.required_integer(expression);
    if (!value) {
        evaluator.diagnose(expression.location);
        return std::nullopt;
    }
    return Expr::IntegerConstant{value->integer, value->type->builtin};
}

std::unique_ptr<Expr> evaluate_target_pointer_constant(
    Program& program, const Expr& expression, const TypePtr& destination,
    const FunctionDecl* caller, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    const GenericPointerResolver& resolver) {
    Evaluator evaluator(program, diagnostics, caller,
                        caller ? caller->source_namespace : std::string{},
                        &size_of, &align_of, &resolver);
    auto value = evaluator.required_pointer(expression, destination);
    if (!value) evaluator.diagnose(expression.location);
    return value;
}

std::optional<unsigned> evaluate_alignment_attribute(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace) {
    if (attribute.arguments.size() != 1 ||
        !attribute.expression_argument) {
        diagnostics.error(attribute.location,
                          "aligned on " + std::string(subject) +
                              " requires one integer argument");
        return std::nullopt;
    }
    const auto value = evaluate_target_integer_constant(
        program, *attribute.expression_argument, diagnostics, size_of,
        align_of, source_namespace);
    if (!value) return std::nullopt;
    if (value->value.high != 0 ||
        value->value.low > std::numeric_limits<unsigned>::max() ||
        value->value.low == 0 ||
        (value->value.low & (value->value.low - 1U)) != 0) {
        diagnostics.error(
            attribute.location,
            "aligned argument must be a positive power-of-two integer constant");
        return std::nullopt;
    }
    return static_cast<unsigned>(value->value.low);
}

} // namespace cross
