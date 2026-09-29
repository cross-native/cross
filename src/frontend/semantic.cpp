// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/semantic.hpp"

#include "common/uint128.hpp"
#include "common/floating_semantics.hpp"
#include "common/integer_semantics.hpp"
#include "frontend/lexer.hpp"
#include "frontend/syntax.hpp"
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
        "section", "stack_cleanup", "syntax_expander", "thread_local", "tls_model",
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

struct NominalInstantiation {
    std::shared_ptr<const GenericTagOwner> owner;
    std::string serialization;
    mutable std::unordered_map<const NominalTypeIdentity*,
        std::shared_ptr<const NominalTypeIdentity>> identities;

    std::shared_ptr<const NominalTypeIdentity> substitute(
        const std::shared_ptr<const NominalTypeIdentity>& source) const {
        if (!source || !owner || source->generic_owner != owner) return source;
        if (const auto found = identities.find(source.get()); found != identities.end())
            return found->second;
        auto identity = std::make_shared<NominalTypeIdentity>(*source);
        identity->generic_owner.reset();
        identity->instance_key = serialization;
        identities.emplace(source.get(), identity);
        return identity;
    }
};

struct TypeSubstitutions : std::unordered_map<std::string, TypePtr> {
    std::shared_ptr<NominalInstantiation> nominal;
};
using ValueSubstitutions = NameMap<const Expr*>;

using EnumInitializerPreparation = std::function<void(std::unique_ptr<Expr>&)>;
bool evaluate_enumerations(Program& program, Diagnostics& diagnostics, std::size_t first = 0,
                           const EnumInitializerPreparation& prepare = {});
void materialize_enumerators(Program& program, Diagnostics& diagnostics);

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
            } else if (attribute.name == "syntax_expander") {
                diagnostics.error(attribute.location,
                    "syntax_expander requires a dedicated static expansion-function declaration");
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

std::unique_ptr<Expr> clone_expr(const Expr& source,
                                 const TypeSubstitutions& types = {},
                                 const ValueSubstitutions& values = {});

TypePtr clone_type(const TypePtr& source,
                   const TypeSubstitutions& substitutions = {},
                   const ValueSubstitutions& values = {}) {
    if (!source) return {};
    TypePtr result;
    if (source->kind == Type::Kind::Builtin) {
        result = source->nominal_key().empty()
                     ? builtin_type(source->builtin)
                     : enum_type(source->nominal_name, source->builtin);
    } else if (source->kind == Type::Kind::Pointer) {
        result = pointer_type(clone_type(source->pointee, substitutions, values));
    } else if (source->kind == Type::Kind::Function && source->function) {
        auto parameters = source->function->parameters;
        for (auto& parameter : parameters)
            parameter.type = clone_type(parameter.type, substitutions, values);
        result =
            function_type(clone_type(source->function->result, substitutions, values),
                          std::move(parameters), source->function->variadic,
                          source->function->abi);
        result->function->result_location =
            source->function->result_location;
        result->function->clobbers = source->function->clobbers;
        result->function->stack_cleanup =
            source->function->stack_cleanup;
    } else if (source->kind == Type::Kind::Vector) {
        result = vector_type(clone_type(source->element, substitutions, values),
                             source->lanes, source->scalable);
    } else if (source->kind == Type::Kind::Array) {
        result = array_type(clone_type(source->element, substitutions, values),
                            source->lanes);
        if (source->array_bound)
            result->array_bound = clone_expr(*source->array_bound, substitutions, values);
    } else if (source->kind == Type::Kind::Record) {
        result = record_type(source->nominal_name, source->is_union);
    } else if (source->kind == Type::Kind::Tokens) {
        result = tokens_type();
    } else if (source->kind == Type::Kind::SyntaxMatch) {
        result = syntax_match_type();
    } else if (source->kind == Type::Kind::Syntax) {
        result = syntax_type();
    } else if (source->kind == Type::Kind::Span) {
        result = span_type();
    } else if (source->kind == Type::Kind::Context) {
        result = context_type();
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
    // A substituted generic may already carry a nominal identity of its own.
    if (source->nominal_identity)
        result->nominal_identity = substitutions.nominal
            ? substitutions.nominal->substitute(source->nominal_identity) : source->nominal_identity;
    return result;
}

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
    if (source.name_context && types.nominal &&
        source.name_context->value_binding.enumeration) {
        auto context = std::make_shared<NameLookupContext>(*source.name_context);
        context->value_binding.enumeration =
            types.nominal->substitute(context->value_binding.enumeration);
        result->name_context = std::move(context);
    }
    result->string_value = source.string_value;
    result->quote_fragments = source.quote_fragments;
    result->evaluated_integer = source.evaluated_integer;
    result->evaluated_floating = source.evaluated_floating;
    result->evaluated_address = source.evaluated_address;
    result->object_relocations = source.object_relocations;
    result->generic_visible_at_call = source.generic_visible_at_call;
    if (source.type) result->type = clone_type(source.type, types, values);
    if (source.left) result->left = clone_expr(*source.left, types, values);
    if (source.right) result->right = clone_expr(*source.right, types, values);
    if (source.third) result->third = clone_expr(*source.third, types, values);
    for (const auto& argument : source.arguments) {
        result->arguments.push_back(clone_expr(*argument, types, values));
    }
    for (const auto& argument : source.generic_arguments) {
        Expr::GenericArgument copy;
        if (argument.type) copy.type = clone_type(argument.type, types, values);
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
    result->type = clone_type(source.type, types, values);
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
    result->label_fresh = source.label_fresh;
    result->attributes = clone_attributes(source.attributes, types, values);
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
                if (enumerator.binding.kind != ValueBinding::Kind::Enumerator &&
                    enumerator.name == candidate) return candidate;
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
                !argument.value->type->nominal_key().empty()) {
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
        if (has_pending_array_bound(actual)) {
            diagnostics.error(argument.location,
                "generic deduction requires a resolved fixed array bound");
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

void rewrite_generic_type_bounds(const TypePtr& type, FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling) {
    if (!type) return;
    rewrite_generic_type_bounds(type->pointee, caller, program, diagnostics, state, mangling);
    rewrite_generic_type_bounds(type->element, caller, program, diagnostics, state, mangling);
    if (type->function) {
        rewrite_generic_type_bounds(type->function->result, caller, program, diagnostics, state, mangling);
        for (const auto& parameter : type->function->parameters)
            rewrite_generic_type_bounds(parameter.type, caller, program, diagnostics, state, mangling);
    }
    if (!type->array_bound || type->lanes != 0) return;
    auto expression = clone_expr(*type->array_bound);
    rewrite_generic_expr(expression, caller, program, diagnostics, state, mangling);
    type->array_bound = std::move(expression);
    // Resolve target-independent extents before a generic body can deduce an
    // array-pointer argument from a member. Layout-dependent queries remain
    // required expressions for the target's cycle-checked record layout pass.
    std::ostringstream output;
    Diagnostics quiet(output);
    if (const auto bound = evaluate_fixed_array_bound(program, *type->array_bound, quiet,
            program.evaluation_size_of, program.evaluation_align_of,
            caller ? caller->source_namespace : std::string_view{})) {
        type->lanes = *bound;
    }
}

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
    // Function attributes contain required expressions too. Rewrite after
    // generic substitution, before evaluating alignment, and detach the shared
    // expression so declarations/instances cannot mutate one another's tree.
    for (auto& attribute : function.attributes) {
        if (!attribute.expression_argument) continue;
        auto expression = clone_expr(*attribute.expression_argument);
        rewrite_generic_expr(expression, &function, program, diagnostics, state, mangling);
        attribute.expression_argument = std::shared_ptr<Expr>(std::move(expression));
    }
    rewrite_generic_statement(*function.body, &function, program, diagnostics,
                              state, mangling);
    state.locals = std::move(saved_locals);
    state.local_types = std::move(saved_local_types);
}

std::unique_ptr<FunctionDecl> instantiate(
    const FunctionDecl& source, const std::vector<Expr::GenericArgument>& arguments,
    std::string internal_name, std::string instance_key, Program& program,
    Diagnostics& diagnostics) {
    if (source.generic_parameters.size() != arguments.size()) {
        diagnostics.error(source.location,
                          "generic argument count does not match '" + source.name + "'");
        return {};
    }
    TypeSubstitutions types;
    if (source.generic_tag_owner) {
        types.nominal = std::make_shared<NominalInstantiation>();
        types.nominal->owner = source.generic_tag_owner;
        types.nominal->serialization = std::move(instance_key);
    }
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
    result->return_type = clone_type(source.return_type, types, values);
    for (const auto& parameter : source.parameters) {
        result->parameters.push_back(
            {parameter.location, parameter.name, clone_type(parameter.type, types, values),
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
    if (types.nominal) {
        // Clone into temporary vectors: publication can reallocate the program
        // tables, and definitions may refer to each other in either direction.
        std::vector<RecordDecl> records;
        std::vector<EnumDecl> enumerations;
        for (const auto& record : program.records) {
            if (!record.nominal_identity ||
                record.nominal_identity->generic_owner != source.generic_tag_owner) continue;
            RecordDecl copy;
            copy.location = record.location;
            copy.name = record.name;
            copy.is_union = record.is_union;
            copy.complete = record.complete;
            copy.nominal_identity = types.nominal->substitute(record.nominal_identity);
            copy.attributes = clone_attributes(record.attributes, types, values);
            for (const auto& member : record.members)
                copy.members.push_back({member.location, member.name, clone_type(member.type, types, values),
                    member.bit_width ? clone_expr(*member.bit_width, types, values) : nullptr,
                    clone_attributes(member.attributes, types, values)});
            records.push_back(std::move(copy));
        }
        for (const auto& enumeration : program.enumerations) {
            if (!enumeration.nominal_identity ||
                enumeration.nominal_identity->generic_owner != source.generic_tag_owner) continue;
            EnumDecl copy;
            copy.location = enumeration.location;
            copy.name = enumeration.name;
            copy.underlying = enumeration.underlying;
            copy.nominal_identity = types.nominal->substitute(enumeration.nominal_identity);
            copy.attributes = clone_attributes(enumeration.attributes, types, values);
            for (const auto& enumerator : enumeration.enumerators) {
                auto binding = enumerator.binding;
                binding.enumeration = types.nominal->substitute(binding.enumeration);
                copy.enumerators.push_back({enumerator.location, enumerator.name,
                    enumerator.initializer ? clone_expr(*enumerator.initializer, types, values) : nullptr,
                    {}, std::move(binding)});
            }
            enumerations.push_back(std::move(copy));
        }
        program.records.insert(program.records.end(),
            std::make_move_iterator(records.begin()), std::make_move_iterator(records.end()));
        program.enumerations.insert(program.enumerations.end(),
            std::make_move_iterator(enumerations.begin()), std::make_move_iterator(enumerations.end()));
    }
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
        const auto first_record = program.records.size();
        const auto first_enumeration = program.enumerations.size();
        auto instance = instantiate(*generic, expression->generic_arguments,
                                    internal_name, identity, program, diagnostics);
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
        ++state.depth;
        struct RestoreDepth {
            unsigned& depth;
            ~RestoreDepth() { --depth; }
        } restore_depth{state.depth};
        // Publish the concrete function first so enum initializers may call
        // generics (including recursive references) through the normal table.
        if (!evaluate_enumerations(program, diagnostics, first_enumeration,
                [&](std::unique_ptr<Expr>& initializer) {
                    rewrite_generic_expr(initializer, concrete, program, diagnostics, state, mangling);
                })) return;
        const auto rewrite_attribute = [&](std::size_t record, std::optional<std::size_t> member,
                                           std::size_t attribute) {
            const auto& attributes = member ? program.records[record].members[*member].attributes
                                            : program.records[record].attributes;
            if (!attributes[attribute].expression_argument) return;
            auto value = clone_expr(*attributes[attribute].expression_argument);
            rewrite_generic_expr(value, concrete, program, diagnostics, state, mangling);
            auto& updated = member ? program.records[record].members[*member].attributes
                                  : program.records[record].attributes;
            updated[attribute].expression_argument = std::shared_ptr<Expr>(std::move(value));
        };
        for (auto index = first_record; index < program.records.size(); ++index) {
            for (std::size_t attribute = 0; attribute < program.records[index].attributes.size(); ++attribute)
                rewrite_attribute(index, {}, attribute);
            for (std::size_t member = 0; member < program.records[index].members.size(); ++member) {
                // Copy the handle before calls that may append record definitions.
                const auto type = program.records[index].members[member].type;
                rewrite_generic_type_bounds(type, concrete, program, diagnostics, state, mangling);
                auto width = std::move(program.records[index].members[member].bit_width);
                rewrite_generic_expr(width, concrete, program, diagnostics, state, mangling);
                program.records[index].members[member].bit_width = std::move(width);
                for (std::size_t attribute = 0; attribute < program.records[index].members[member].attributes.size(); ++attribute)
                    rewrite_attribute(index, member, attribute);
            }
        }
        // Publish before walking the body so recursive identical instances
        // resolve to the in-progress function. Nested constant generic calls
        // can then be evaluated through the same visible definition table.
        if (concrete->body) {
            rewrite_generic_function(*concrete, program, diagnostics, state, mangling);
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
    for (std::size_t index = 0; index < program.records.size(); ++index) {
        if (program.records[index].nominal_identity &&
            program.records[index].nominal_identity->generic_owner) continue;
        FunctionDecl context;
        context.source_namespace = namespace_prefix(program.records[index].name);
        const auto location = program.records[index].location;
        if (location.file) context.source_unit = location.file->source_unit_at(location.line);
        for (std::size_t member = 0; member < program.records[index].members.size(); ++member) {
            const auto type = program.records[index].members[member].type;
            rewrite_generic_type_bounds(type, &context, program, diagnostics, state, mangling);
        }
    }
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
    // Assertions are required expressions too. Instantiating one can append
    // assertions from the substituted body, so drain the growing list before
    // removing generic definitions. Do not hold a vector element reference
    // across a rewrite that may reallocate that same list.
    for (std::size_t index = 0; index < program.static_assertions.size(); ++index) {
        FunctionDecl context;
        context.source_namespace = program.static_assertions[index].source_namespace;
        const auto location = program.static_assertions[index].location;
        if (location.file)
            context.source_unit = location.file->source_unit_at(location.line);
        auto condition = std::move(program.static_assertions[index].condition);
        rewrite_generic_expr(condition, &context, program, diagnostics, state, mangling);
        program.static_assertions[index].condition = std::move(condition);
    }
    program.functions.erase(
        std::remove_if(program.functions.begin(), program.functions.end(),
                       [](const auto& function) {
                           return !function->generic_parameters.empty();
                       }),
        program.functions.end());
    std::erase_if(program.records, [](const auto& record) {
        return record.nominal_identity && record.nominal_identity->generic_owner;
    });
    std::erase_if(program.enumerations, [](const auto& enumeration) {
        return enumeration.nominal_identity && enumeration.nominal_identity->generic_owner;
    });
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
    const std::string* replacement(const Expr& expression) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(name_key(expression));
            if (found == scope->end()) continue;
            return found->second.empty() ? nullptr : &found->second;
        }
        return nullptr;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name) {
            if (const auto* name = replacement(*expression)) {
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
            auto context = std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Local;
            context->value_binding = name_key(parameter).binding;
            argument->name_context = std::move(context);
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
            if (enumerator.binding.kind == ValueBinding::Kind::Enumerator) continue;
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
    if (name.context && name.context->value_binding.kind == ValueBinding::Kind::Enumerator) {
        for (const auto& enumeration : program.enumerations)
            for (const auto& enumerator : enumeration.enumerators)
                if (enumerator.binding == name.context->value_binding)
                    return ResolvedEnumerator{&enumeration, &enumerator};
        return {};
    }
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
                return candidate.nominal_key() == base->nominal_key() &&
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
        if (statement.kind == Statement::Kind::DeclarationList) {
            for (auto& child : statement.statements) rewrite_statement(*child);
            return;
        }
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

struct EvalValue;
struct EvalBuffer {
    std::string data;
    // Each bit records initialized object representation; full-byte stores
    // set 0xff, while bit-field writes mark only the addressed field bits.
    std::vector<std::uint8_t> assigned;
    // Zero means raw byte storage; scalar tags record acquired leaf types.
    // 0xff retains aggregate-only storage such as padding or copied unions.
    std::vector<std::uint8_t> effective_type;
    struct TypedObject {
        std::size_t offset{};
        std::size_t length{};
        TypePtr type;
    };
    // Whole-record stores additionally retain nominal aggregate identity;
    // leaf tags alone cannot distinguish two records with identical fields.
    std::vector<TypedObject> typed_objects;
    struct PointerObject {
        std::size_t offset{};
        std::size_t length{};
        std::shared_ptr<const EvalValue> value;
        // A self-referential object must not create an ownership cycle.
        std::weak_ptr<EvalBuffer> backing;
        bool has_backing{};
    };
    std::vector<PointerObject> pointers;
    bool alive{true};
    bool frozen{};
};

struct EvalMetaPointer {
    std::shared_ptr<const std::string> immutable;
    std::shared_ptr<EvalBuffer> mutable_buffer;
    std::size_t view_offset{};
    std::size_t view_length{};
    std::size_t position{};
    // A projected packed member can be accessed at its declared placement
    // alignment; changing pointee type drops this lvalue privilege.
    std::optional<std::size_t> access_alignment;
    // Access through a union member may reinterpret another active member's
    // representation; ordinary pointer casts do not inherit this permission.
    bool union_member_view{};
    struct BitField {
        unsigned width{};
        unsigned offset{};
        TypePtr owner;
        std::size_t owner_offset{};
    };
    std::optional<BitField> bit_field;
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
    std::shared_ptr<const SyntaxMatchValue> syntax_match;
    std::shared_ptr<const SyntaxNode> syntax_node;
    std::optional<SyntaxSpan> syntax_span;
    std::shared_ptr<const SyntaxContext> syntax_context;
    std::shared_ptr<const std::string> bytes;
    std::size_t byte_offset{};
    std::size_t byte_length{};
    // Mutable buffers are shared handles. Copying an EvalValue preserves
    // aliasing; freeze invalidates all copies through this shared state.
    std::shared_ptr<EvalBuffer> buffer;
    // Non-scalar values own a snapshot, rather than aliasing the lvalue
    // from which they were read. Local lvalues keep their own storage cell.
    std::shared_ptr<EvalBuffer> object;
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
        return string != nullptr || address.has_value() || meta_pointer.has_value() ||
            (type && type->kind == Type::Kind::Pointer);
    }
    [[nodiscard]] bool truthy() const {
        if (meta_pointer || string) return true;
        if (address) return address->kind != AddressConstant::Kind::Absolute || address->absolute != UInt128{};
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
    const auto common_scalar_numeric = [&](const TypePtr& left,
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
    const auto common_numeric = [&](const TypePtr& left,
                                    const TypePtr& right) -> TypePtr {
        if (!left || !right) return {};
        const bool left_vector = left->kind == Type::Kind::Vector;
        const bool right_vector = right->kind == Type::Kind::Vector;
        if (!left_vector && !right_vector)
            return common_scalar_numeric(left, right);
        if (left_vector && right_vector &&
            (left->lanes != right->lanes ||
             left->scalable != right->scalable)) return {};
        const auto& shape = left_vector ? left : right;
        const auto element = common_scalar_numeric(
            left_vector ? left->element : left,
            right_vector ? right->element : right);
        return element ? vector_type(element, shape->lanes, shape->scalable)
                       : TypePtr{};
    };
    const auto vector_mask = [&](const TypePtr& type) -> TypePtr {
        if (!type || type->kind != Type::Kind::Vector || !type->element)
            return {};
        const auto bits = type->element->builtin == BuiltinType::Fptr
            ? program.address_bits : type_bits(type->element);
        const auto mask = bits == 8 ? BuiltinType::I8
            : bits == 16 ? BuiltinType::I16
            : bits == 32 ? BuiltinType::I32
            : bits == 64 ? BuiltinType::I64
            : BuiltinType::Void;
        return mask == BuiltinType::Void ? TypePtr{}
            : vector_type(builtin_type(mask), type->lanes, type->scalable);
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
        if (const auto found = resolve_enumerator(program, caller,
                caller ? caller->source_namespace : std::string_view{}, expression))
            return enum_type(*found->enumeration);
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
        if (!expression.left) return {};
        auto operand = infer_generic_actual(
            *expression.left, caller, program, state,
            expression.text != "&");
        if (!operand) return {};
        if (expression.text == "!")
            return operand->kind == Type::Kind::Vector
                ? vector_mask(operand) : builtin_type(BuiltinType::Bool);
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
                    return candidate.nominal_key() == base->nominal_key() &&
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
        if (expression.text == "&&" || expression.text == "||")
            return builtin_type(BuiltinType::Bool);
        if (expression.text == "==" || expression.text == "!=" ||
            expression.text == "<" || expression.text == "<=" ||
            expression.text == ">" || expression.text == ">=") {
            const auto compared_left = infer_generic_actual(
                *expression.left, caller, program, state);
            const auto compared_right = infer_generic_actual(
                *expression.right, caller, program, state);
            if (!compared_left || !compared_right) return {};
            if (compared_left->kind != Type::Kind::Vector &&
                compared_right->kind != Type::Kind::Vector)
                return builtin_type(BuiltinType::Bool);
            const auto common = common_numeric(compared_left, compared_right);
            if (!common || common->kind != Type::Kind::Vector ||
                !common->element) return {};
            return vector_mask(common);
        }
        const auto left = infer_generic_actual(*expression.left, caller,
                                               program, state);
        if (expression.text == "index") {
            const auto index = infer_generic_actual(*expression.right, caller,
                                                    program, state);
            if (!left || !is_integer(index)) return {};
            return left->kind == Type::Kind::Pointer ||
                           left->kind == Type::Kind::Array ||
                           left->kind == Type::Kind::Vector
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
        const auto integer_or_integer_vector = [](const TypePtr& type) {
            return is_integer(type) ||
                   (type->kind == Type::Kind::Vector &&
                    is_integer(type->element));
        };
        if ((expression.text == "<<" || expression.text == ">>") &&
            integer_or_integer_vector(left) &&
            integer_or_integer_vector(right))
            return is_integer(left)
                ? integer_result(promote_integer(integer_shape(left)),
                                 left, {})
                : vector_type(clone_type(left->element), left->lanes,
                              left->scalable);
        if ((expression.text == "%" || expression.text == "&" ||
             expression.text == "|" || expression.text == "^") &&
            (!integer_or_integer_vector(left) ||
             !integer_or_integer_vector(right))) return {};
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
              std::shared_ptr<const SyntaxContext> macro_context = {},
              SyntaxParseCallback syntax_parse = {},
              std::shared_ptr<const SyntaxContext> call_context = {})
        : program_(program), diagnostics_(diagnostics),
          current_function_(caller),
          current_namespace_(std::move(current_namespace)),
          size_of_(size_of ? size_of :
              (program.evaluation_size_of ? &program.evaluation_size_of : nullptr)),
          align_of_(align_of ? align_of :
              (program.evaluation_align_of ? &program.evaluation_align_of : nullptr)),
          pointer_resolver_(pointer_resolver ? pointer_resolver :
              (program.evaluation_pointer_resolver ? &program.evaluation_pointer_resolver : nullptr)),
          procedural_(macro_context != nullptr), macro_context_(std::move(macro_context)),
          syntax_parse_(std::move(syntax_parse)), call_context_(std::move(call_context)) {}

    bool charge_input_tokens(const TokenSequence& tokens, SourceLocation location) {
        std::size_t size = 0;
        const auto byte_limit = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        for (const auto& token : tokens) {
            const auto metadata_cost = meta_token_storage_bytes + tag_binding_storage(token.origin.tag_binding);
            if (!charge_input_context(token.origin.context, location)) return false;
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

    bool charge_input_match(const SyntaxMatchValue& match, SourceLocation location) {
        if (!charge_input_context(match.context, location) ||
            !charge_meta_bytes(syntax_match_storage_bytes, location) || !charge_input_tokens(match.input, location)) return false;
        if (match.variant && !charge_meta_bytes(32 + match.variant->size(), location)) return false;
        for (const auto& label : match.variant_labels)
            if (!charge_meta_bytes(32 + label.size(), location)) return false;
        for (const auto& field : match.fields) {
            if (!charge_meta_bytes(syntax_field_storage_bytes + field.name.size(), location) ||
                !charge_input_tokens(field.tokens, location)) return false;
            if (field.node) {
                const auto size = syntax_node_storage(*field.node,
                    std::min(program_.evaluation_limits.bytes, program_.evaluation_limits.memory));
                if (size > program_.evaluation_limits.bytes ||
                    size > std::numeric_limits<std::size_t>::max()) {
                    fail(location, "public syntax tree exceeds translation-time storage capacity");
                    return false;
                }
                if (!charge_meta_bytes(static_cast<std::size_t>(size), location)) return false;
            }
            for (const auto& record : field.records)
                if (!charge_input_match(*record, location)) return false;
        }
        return true;
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
        pop_scope();
        return valid;
    }

    void diagnose(SourceLocation fallback) const {
        diagnostics_.error(
            failure_location_.valid() ? failure_location_ : fallback,
            failure_reason_.value_or("expression is not a translation-time value"));
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
        pop_scope();
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
                                    {}, macro_context_, {}, 0, {}};
                }
                if (!append_tokens(result, literal, expression.location))
                    return std::nullopt;
                if (index == expression.arguments.size()) break;
                auto value = this->expression(*expression.arguments[index]);
                if (!value) return std::nullopt;
                if (value->syntax_node) {
                    MetaToken splice;
                    splice.kind = TokenKind::StructuredSplice;
                    splice.text = "__cross_syntax_splice";
                    splice.origin = token_origin(value->syntax_node->span.first);
                    splice.origin.context = value->syntax_node->context;
                    splice.splice = value->syntax_node;
                    if (!append_tokens(result, {splice}, expression.arguments[index]->location))
                        return std::nullopt;
                    continue;
                }
                if (!value->tokens) {
                    fail(expression.arguments[index]->location,
                         "$::unquote requires a token value or syntax node");
                    return std::nullopt;
                }
                if (!append_tokens(result, *value->tokens, expression.arguments[index]->location))
                    return std::nullopt;
            }
            return token_value(std::move(result));
        }
        case Expr::Kind::ByteSequence:
            if (expression.type) {
                auto value = new_object(expression.type, expression.location, true);
                if (!value || expression.string_value.size() != value->object->data.size()) return std::nullopt;
                value->object->data = expression.string_value;
                for (const auto& relocation : expression.object_relocations) {
                    auto pointer = std::make_shared<EvalValue>(UInt128{}, relocation.type);
                    pointer->address = relocation.address;
                    const auto length = meta_object_size(relocation.type);
                    if (!length || *length != relocation.length || relocation.offset > value->object->data.size() ||
                        *length > value->object->data.size() - relocation.offset ||
                        !charge_meta_bytes(64 + type_name(relocation.type).size(), expression.location)) return std::nullopt;
                    value->object->pointers.push_back({static_cast<std::size_t>(relocation.offset),
                        *length, std::move(pointer), {}, false});
                }
                return value;
            }
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
                         type->kind == Type::Kind::SyntaxMatch ||
                         type->kind == Type::Kind::Syntax ||
                         type->kind == Type::Kind::Span ||
                         type->kind == Type::Kind::Context ||
                         type->kind == Type::Kind::Bytes ||
                         type->kind == Type::Kind::Buffer)) {
                fail(expression.location, type_name(type) + " has no runtime size or alignment");
                return std::nullopt;
            }
            if (!type) {
                fail(expression.location,
                     "target layout is unavailable for this translation-time query");
                return std::nullopt;
            }
            if (!query && meta_object_type(type)) {
                if (const auto size = meta_object_size(type)) {
                    const auto value = expression.kind == Expr::Kind::Sizeof
                        ? size : meta_object_alignment(type);
                    if (value)
                        return EvalValue{UInt128{*value},
                                         builtin_type(BuiltinType::Uptr)};
                }
            }
            if (!query) {
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
            if (!condition || !known_truth(*condition, expression.location) || !type)
                return std::nullopt;
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
    bool charge_input_context(const std::shared_ptr<const SyntaxContext>& context, SourceLocation location) {
        if (!context || !charged_contexts_.insert(context.get()).second) return true;
        auto size = syntax_context_storage(*context);
        if (context->parse_environment &&
            !charged_environments_.insert(context->parse_environment.get()).second)
            size -= syntax_environment_storage(*context->parse_environment);
        if (size > program_.evaluation_limits.bytes) {
            fail(location, "translation-time syntax context byte budget exceeded");
            return false;
        }
        return charge_meta_bytes(static_cast<std::size_t>(size), location);
    }

    static bool contains_tokens(const TypePtr& type) {
        return type && (type->kind == Type::Kind::Tokens ||
            type->kind == Type::Kind::SyntaxMatch ||
            type->kind == Type::Kind::Syntax ||
            type->kind == Type::Kind::Span ||
            type->kind == Type::Kind::Context ||
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

    std::optional<EvalValue> span_value(SyntaxSpan span, SourceLocation location) {
        if (!span.first.valid() || !span.last.valid()) {
            fail(location, "syntax span has no retained source location");
            return std::nullopt;
        }
        if (!charge_meta_bytes(syntax_span_storage_bytes, location)) return std::nullopt;
        EvalValue value{UInt128{}, span_type()};
        value.syntax_span = std::move(span);
        return value;
    }

    std::optional<EvalValue> context_value(std::shared_ptr<const SyntaxContext> context,
                                         SourceLocation location) {
        if (!context) {
            fail(location, "syntax value has no retained lookup context");
            return std::nullopt;
        }
        if (!charge_meta_bytes(syntax_context_handle_storage_bytes, location))
            return std::nullopt;
        EvalValue value{UInt128{}, context_type()};
        value.syntax_context = std::move(context);
        return value;
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
        for (const auto& token : part) {
            const auto metadata_cost = meta_token_storage_bytes + tag_binding_storage(token.origin.tag_binding);
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
                            {}, macro_context_, {}, 0, {}};
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
        if (node->kind != Expr::Kind::Name || lookup_mutable(*node) ||
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
        if (!source || !destination || destination->kind != Type::Kind::Pointer ||
            !pointer_resolver_) return std::nullopt;
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
            if (lookup_mutable(node)) {
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
        std::shared_ptr<EvalBuffer> storage;
        Cell() = default;
        Cell(EvalValue initial, bool assigned, bool immutable)
            : value(std::move(initial)), initialized(assigned), read_only(immutable) {}
    };

    void pop_scope() {
        for (auto& [name, cell] : scopes_.back()) {
            if (cell.storage) cell.storage->alive = false;
            if (cell.value.object) cell.value.object->alive = false;
        }
        scopes_.pop_back();
    }

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
                return candidate.nominal_key() == owner->nominal_key() &&
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
                if (!type || (type->kind != Type::Kind::Tokens &&
                              type->kind != Type::Kind::Syntax)) {
                    fail(argument->location,
                         "$::unquote requires a token value or syntax node");
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
                    fail(node.location, "layout query requires a runtime object type, not translation-only meta values");
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
            if (name.starts_with("$::syntax::")) {
                const bool diagnostic = name == "$::syntax::error" ||
                    name == "$::syntax::warning" || name == "$::syntax::note";
                const bool context = name == "$::syntax::context";
                const auto count = name == "$::syntax::input" || name == "$::syntax::span" || context ? 1U
                    : name == "$::syntax::capture" || name == "$::syntax::node" ||
                      name == "$::syntax::count" ||
                      name == "$::syntax::is_variant" || name == "$::syntax::capture_span" || diagnostic ? 2U
                    : name == "$::syntax::at" ? 3U : 0U;
                if (!procedural_ || count == 0 || node.arguments.size() != count) {
                    fail(node.location, "unsupported syntax operation or invalid argument count: " + name);
                    return false;
                }
                for (const auto& argument : node.arguments)
                    if (!validate_required_tree(*argument)) return false;
                const auto first = expression_type(*node.arguments[0]);
                const auto field = count > 1 ? expression_type(*node.arguments[1]) : nullptr;
                const bool valid_first = first && (context
                    ? first->kind == Type::Kind::SyntaxMatch || first->kind == Type::Kind::Syntax
                    : first->kind == (diagnostic ? Type::Kind::Span : Type::Kind::SyntaxMatch));
                if (!valid_first ||
                    (count > 1 && (!field || field->kind != Type::Kind::Pointer || !field->pointee ||
                                  field->pointee->builtin != BuiltinType::U8)) ||
                    (count > 2 && !is_integer(expression_type(*node.arguments[2])))) {
                    fail(node.location, context ? "$::syntax::context requires a syntax match or node"
                        : diagnostic ? "syntax diagnostic requires a span and string message"
                        : "syntax operation requires a match, string field, and integer index as applicable");
                    return false;
                }
                return true;
            }
            if (name == "$::meta::tokens" || name == "$::meta::node_span" || name == "$::meta::child_count" ||
                name == "$::meta::child" || name == "$::meta::is_kind" ||
                name == "$::meta::is_production" || name == "$::meta::is_extension" ||
                name == "$::meta::replace_child" ||
                name == "$::meta::extension_match") {
                const auto count = name == "$::meta::tokens" || name == "$::meta::node_span" ||
                    name == "$::meta::child_count" || name == "$::meta::extension_match" ? 1U :
                    name == "$::meta::replace_child" ? 3U : 2U;
                if (!procedural_ || node.arguments.size() != count) {
                    fail(node.location, "unsupported syntax-tree operation or invalid argument count: " + name);
                    return false;
                }
                for (const auto& argument : node.arguments)
                    if (!validate_required_tree(*argument)) return false;
                const auto first = expression_type(*node.arguments[0]);
                if (!first || first->kind != Type::Kind::Syntax) {
                    fail(node.arguments[0]->location, name + " requires $::meta::syntax");
                    return false;
                }
                if (count >= 2) {
                    const auto second = expression_type(*node.arguments[1]);
                    const bool integer = name == "$::meta::child" ||
                        name == "$::meta::replace_child";
                    const bool string = second && second->kind == Type::Kind::Pointer &&
                        second->pointee && second->pointee->kind == Type::Kind::Builtin &&
                        second->pointee->builtin == BuiltinType::U8;
                    if (!second || (integer ? !is_integer(second) : !string)) {
                        fail(node.arguments[1]->location, name +
                            (integer ? " requires an integer child index" : " requires a string name"));
                        return false;
                    }
                }
                if (count == 3) {
                    const auto third = expression_type(*node.arguments[2]);
                    if (!third || third->kind != Type::Kind::Syntax) {
                        fail(node.arguments[2]->location,
                            "$::meta::replace_child requires a syntax replacement node");
                        return false;
                    }
                }
                return true;
            }
            if (name == "$::embed") {
                if (node.arguments.size() != 1 ||
                    node.arguments.front()->kind != Expr::Kind::String ||
                    !token_origin(node.left->location).embed) {
                    fail(node.location, "$::embed requires one identified string-literal path");
                    return false;
                }
                return true;
            }
            if (procedural_ && name == "$::meta::call_site") {
                if (node.arguments.size() != 1U) {
                    fail(node.location, "$::meta::call_site requires one token value");
                    return false;
                }
                const auto& argument = *node.arguments.front();
                if (!validate_required_tree(argument)) return false;
                const auto type = expression_type(argument);
                if (!type || type->kind != Type::Kind::Tokens) {
                    fail(argument.location, "$::meta::call_site requires one identifier token value");
                    return false;
                }
                return true;
            }
            if (procedural_ && name == "$::meta::gensym") {
                if (node.arguments.size() != 1U) {
                    fail(node.location, "$::meta::gensym requires one string prefix");
                    return false;
                }
                const auto& argument = *node.arguments.front();
                if (!validate_required_tree(argument)) return false;
                const auto type = expression_type(argument);
                if (!type || type->kind != Type::Kind::Pointer ||
                    !type->pointee || type->pointee->kind != Type::Kind::Builtin ||
                    type->pointee->builtin != BuiltinType::U8) {
                    fail(argument.location, "$::meta::gensym requires a translation-time string prefix");
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
                if (node.arguments.size() != 1U && node.arguments.size() != 3U) {
                    fail(node.location, "$::meta::parse requires a string or category, tokens, and context");
                    return false;
                }
                for (std::size_t at = 0; at < node.arguments.size(); ++at) {
                    const auto& argument = node.arguments[at];
                    if (!validate_required_tree(*argument)) return false;
                    const auto type = expression_type(*argument);
                    const bool string = type && type->kind == Type::Kind::Pointer &&
                        type->pointee && type->pointee->kind == Type::Kind::Builtin &&
                        type->pointee->builtin == BuiltinType::U8;
                    if (at == 0 ? !string : !type || type->kind !=
                        (at == 1 ? Type::Kind::Tokens : Type::Kind::Context)) {
                        fail(argument->location, node.arguments.size() == 1U
                            ? "$::meta::parse requires a translation-time string"
                            : "$::meta::parse requires a string or category string, tokens, and context");
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
                     same_type(from, to) || pointer_compatible() ||
                     (is_vector(to) && !to->scalable &&
                      ((is_vector(from) && !from->scalable && from->lanes == to->lanes &&
                        (is_integer(from->element) || is_floating(from->element))) ||
                       is_integer(from) || is_floating(from))));
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
            node.left && (node.text == "&" || (node.text == "*" &&
                expression_type(*node.left)->kind == Type::Kind::Pointer));
        const bool vector_unary = node.kind == Expr::Kind::Unary && node.left &&
            is_vector(expression_type(*node.left)) &&
            (node.text == "+" || node.text == "-" || node.text == "!" ||
             (node.text == "~" && is_integer(expression_type(*node.left)->element)));
        const bool pointer_truth = node.kind == Expr::Kind::Unary && node.text == "!" &&
            node.left && expression_type(*node.left)->kind == Type::Kind::Pointer;
        if (node.kind == Expr::Kind::Unary && !pointer_unary && !vector_unary && !pointer_truth &&
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
            if (const auto* cell = lookup_mutable(*node.left)) {
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
                if (yes && no && yes->kind == no->kind &&
                    (yes->kind == Type::Kind::Tokens || yes->kind == Type::Kind::SyntaxMatch ||
                     yes->kind == Type::Kind::Syntax || yes->kind == Type::Kind::Span ||
                     yes->kind == Type::Kind::Context)) {
                    if (is_scalar(condition)) return true;
                    fail(node.location, "procedural macro condition must be scalar");
                    return false;
                }
            }
            if (member && (pointer_resolver_ ||
                           (selected_record_member(node) && node.left &&
                            meta_pointer_source(*node.left))))
                return true;
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
            const bool vector_operation = node.kind == Expr::Kind::Binary &&
                (is_vector(left) || is_vector(right)) &&
                vector_operation_type(left, right, node.text) != nullptr;
            const bool object_conditional = node.kind == Expr::Kind::Conditional && result_type &&
                (result_type->kind == Type::Kind::Record || is_vector(result_type)) && left &&
                (is_integer(left) || is_floating(left) || left->kind == Type::Kind::Pointer);
            const bool object_pointer = result_type &&
                result_type->kind == Type::Kind::Pointer &&
                result_type->pointee &&
                meta_object_type(result_type->pointee);
            const bool pointer_operation = pointer_resolver_ && result_type &&
                result_type->kind == Type::Kind::Pointer &&
                (node.kind == Expr::Kind::Conditional || node.text == "+" || node.text == "-");
            const bool meta_pointer_operation = object_pointer &&
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
            if (indexing ? (!left || (left->kind != Type::Kind::Pointer &&
                                     left->kind != Type::Kind::Array &&
                                     left->kind != Type::Kind::Vector) ||
                            !right || !is_integer(right))
                         : (!pointer_operation && !meta_pointer_operation &&
                            !meta_pointer_pair && !vector_operation && !object_conditional &&
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
        if (from->kind == Type::Kind::SyntaxMatch || to->kind == Type::Kind::SyntaxMatch)
            return from->kind == to->kind;
        if (from->kind == Type::Kind::Syntax || to->kind == Type::Kind::Syntax)
            return from->kind == to->kind;
        if (from->kind == Type::Kind::Span || to->kind == Type::Kind::Span)
            return from->kind == to->kind;
        if (from->kind == Type::Kind::Context || to->kind == Type::Kind::Context)
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
                      type->kind == Type::Kind::SyntaxMatch ||
                      type->kind == Type::Kind::Syntax ||
                      type->kind == Type::Kind::Span ||
                      type->kind == Type::Kind::Context ||
                      (type->kind == Type::Kind::Pointer && type->pointee &&
                       type->pointee->kind == Type::Kind::Builtin && type->pointee->builtin == BuiltinType::U8))) {
                    fail(node.location, "procedural macro locals require ordinary scalar, string-pointer, or translation-only meta cells without runtime storage qualifiers");
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
                if (node.kind == Statement::Kind::Switch ? !is_integer(type) : !is_scalar(type)) {
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
        if (scope) pop_scope();
        return valid;
    }

    bool meta_pointer_source(const Expr& source) {
        if (source.kind == Expr::Kind::Call && source.left &&
            source.left->kind == Expr::Kind::Name &&
            source.left->text == "$::meta::data") return true;
        if (source.kind == Expr::Kind::Name) {
            const auto* cell = lookup_mutable(source);
            return cell && (cell->value.meta_pointer.has_value() ||
                            cell->value.object != nullptr || cell->storage != nullptr);
        }
        if (source.kind == Expr::Kind::Parenthesized ||
            source.kind == Expr::Kind::Cast)
            return source.left && meta_pointer_source(*source.left);
        if (source.kind == Expr::Kind::Unary && source.text == "*")
            return source.left && meta_pointer_source(*source.left);
        if (source.kind == Expr::Kind::Binary &&
            (source.text == "index" || source.text == "member" ||
             source.text == "pointer_member"))
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
            if (const auto* cell = lookup_mutable(expression)) {
                if (decay && cell->value.type->kind == Type::Kind::Array)
                    return pointer_type(cell->value.type->element);
                return cell->value.type;
            }
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
                return enum_type(*found->enumeration);
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
            auto type = expression_type(*expression.left, expression.text != "&");
            if (!type) return {};
            if (expression.text == "&") return pointer_type(type);
            if (is_vector(type)) return expression.text == "!" ? vector_mask_type(type)
                : vector_operation_type(type, type->element, "+");
            if (expression.text == "!") return builtin_type(BuiltinType::Bool);
            if (expression.text == "*" && type->kind == Type::Kind::Pointer &&
                (pointer_resolver_ || meta_pointer_source(*expression.left)))
                return type->pointee;
            if (type->kind == Type::Kind::Pointer &&
                (expression.text == "++" || expression.text == "--" ||
                 expression.text == "post++" || expression.text == "post--") &&
                meta_pointer_source(*expression.left)) return type;
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
                    : base && (base->kind == Type::Kind::Array ||
                               base->kind == Type::Kind::Vector)
                        ? base->element : nullptr;
            }
            const auto left = expression_type(*(conditional ? expression.right : expression.left));
            const auto right = expression_type(*(conditional ? expression.third : expression.right));
            if (!left || !right) return {};
            if (conditional && left->kind == Type::Kind::Record && right->kind == Type::Kind::Record) {
                auto a = clone_type(left);
                auto b = clone_type(right);
                a->is_const = b->is_const = false;
                return same_type(a, b) ? a : TypePtr{};
            }
            if (is_vector(left) || is_vector(right)) {
                const auto common = vector_operation_type(left, right, conditional ? "+" : expression.text);
                if (!common) return {};
                return !conditional && (expression.text == "==" || expression.text == "!=" ||
                    expression.text == "<" || expression.text == ">" || expression.text == "<=" ||
                    expression.text == ">=") ? vector_mask_type(common) : common;
            }
            if (!conditional && (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == ">" || expression.text == "<=" ||
                expression.text == ">=" || expression.text == "&&" || expression.text == "||"))
                return builtin_type(BuiltinType::Bool);
            if (procedural_ && conditional && left->kind == right->kind &&
                (left->kind == Type::Kind::Tokens || left->kind == Type::Kind::SyntaxMatch ||
                 left->kind == Type::Kind::Syntax || left->kind == Type::Kind::Span ||
                 left->kind == Type::Kind::Context)) {
                auto result = clone_type(left);
                result->is_const = false;
                return result;
            }
            const auto object_pointer = [](const TypePtr& type) {
                return type->kind == Type::Kind::Pointer && type->pointee &&
                    meta_object_type(type->pointee);
            };
            if (pointer_resolver_ ||
                ((object_pointer(left) || object_pointer(right)) &&
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
            if (procedural_ && (expression.left->text == "$::syntax::at" ||
                expression.left->text == "$::meta::extension_match")) return syntax_match_type();
            if (procedural_ && expression.left->text == "$::syntax::node") return syntax_type();
            if (procedural_ && expression.left->text == "$::syntax::context") return context_type();
            if (procedural_ && (expression.left->text == "$::syntax::span" ||
                expression.left->text == "$::syntax::capture_span" ||
                expression.left->text == "$::meta::node_span")) return span_type();
            if (procedural_ && (expression.left->text == "$::syntax::error" ||
                expression.left->text == "$::syntax::warning" ||
                expression.left->text == "$::syntax::note")) return builtin_type(BuiltinType::Void);
            if (procedural_ && expression.left->text == "$::syntax::count") return builtin_type(BuiltinType::Uptr);
            if (procedural_ && expression.left->text == "$::syntax::is_variant")
                return builtin_type(BuiltinType::Bool);
            if (procedural_ && (expression.left->text == "$::syntax::input" ||
                expression.left->text == "$::syntax::capture")) return tokens_type();
            if (procedural_ && expression.left->text == "$::meta::tokens") return tokens_type();
            if (procedural_ && expression.left->text == "$::meta::call_site") return tokens_type();
            if (procedural_ && expression.left->text == "$::meta::gensym") return tokens_type();
            if (procedural_ && (expression.left->text == "$::meta::child" ||
                expression.left->text == "$::meta::replace_child")) return syntax_type();
            if (procedural_ && expression.left->text == "$::meta::child_count")
                return builtin_type(BuiltinType::Uptr);
            if (procedural_ && (expression.left->text == "$::meta::is_kind" ||
                expression.left->text == "$::meta::is_production" ||
                expression.left->text == "$::meta::is_extension"))
                return builtin_type(BuiltinType::Bool);
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
                return expression.arguments.size() == 3U ? syntax_type() : tokens_type();
            if ((expression.left->text == "$::eval" || expression.left->text == "$::runtime") &&
                expression.arguments.size() == 1) return expression_type(*expression.arguments.front());
            if (const auto* callee = resolve_function(program_, current_function_, *expression.left,
                    [](const FunctionDecl&) { return true; })) return callee->return_type;
            return {};
        case Expr::Kind::String: {
            const auto element = builtin_type(BuiltinType::U8, true);
            if (decay) return pointer_type(element);
            const auto text = expression.string_value.empty() ? decode_string_literal(expression.text)
                : std::optional<std::string>(expression.string_value);
            if (!text || text->size() >= std::numeric_limits<std::uint32_t>::max()) return {};
            return array_type(element, static_cast<std::uint32_t>(text->size() + 1));
        }
        case Expr::Kind::ByteSequence: return expression.type ? expression.type : bytes_type();
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

    static EvalValue object_pointer(const EvalValue& value) {
        EvalValue result{UInt128{}, pointer_type(clone_type(value.type))};
        EvalMetaPointer pointer;
        pointer.mutable_buffer = value.object;
        pointer.view_length = value.object->data.size();
        result.meta_pointer = std::move(pointer);
        return result;
    }

    bool known_truth(const EvalValue& value, SourceLocation location) {
        if (value.tokens || value.bytes || value.buffer || value.object ||
            (!is_integer(value.type) && !is_floating(value.type) && !value.pointer())) {
            fail(location, "translation-time condition requires a scalar value");
            return false;
        }
        if (value.address && value.address->kind != AddressConstant::Kind::Absolute) {
            fail(location, "emitted object addresses cannot be inspected during translation-time evaluation");
            return false;
        }
        return !value.meta_pointer || live_meta_pointer(value, location);
    }

    std::optional<EvalValue> new_object(const TypePtr& type, SourceLocation location,
                                       bool initialized = false) {
        const auto size = meta_object_size(type);
        if (!size || *size > program_.evaluation_limits.bytes ||
            *size > std::numeric_limits<std::size_t>::max() / 3 ||
            !charge_meta_bytes(*size * 3, location)) {
            fail(location, "translation-time object exceeds target layout or byte budget");
            return std::nullopt;
        }
        EvalValue value{UInt128{}, clone_type(type)};
        value.object = std::make_shared<EvalBuffer>();
        value.object->data.resize(*size, '\0');
        value.object->assigned.resize(*size, initialized ? 0xffU : 0);
        value.object->effective_type.resize(*size, 0);
        if (!stamp_meta_object_types(*value.object, type, 0, location) ||
            !register_meta_record(*value.object, 0, type, location)) return std::nullopt;
        return value;
    }

    EvalValue vector_lane_pointer(const EvalValue& value, std::size_t lane) const {
        auto pointer = object_pointer(value);
        const auto stride = *meta_object_size(value.type->element);
        pointer.type = pointer_type(clone_type(value.type->element));
        pointer.meta_pointer->view_offset = lane * stride;
        pointer.meta_pointer->view_length = stride;
        return pointer;
    }

    TypePtr vector_operation_type(const TypePtr& left, const TypePtr& right,
                                   std::string_view operation) const {
        if (operation != "+" && operation != "-" && operation != "*" && operation != "/" &&
            operation != "%" && operation != "&" && operation != "|" && operation != "^" &&
            operation != "<<" && operation != ">>" && operation != "==" && operation != "!=" &&
            operation != "<" && operation != "<=" && operation != ">" && operation != ">=") return {};
        const auto shape = is_vector(left) ? left : right;
        if (!shape || !is_vector(shape) || shape->scalable ||
            (is_vector(left) && is_vector(right) &&
             (left->lanes != right->lanes || left->scalable != right->scalable))) return {};
        const auto a = is_vector(left) ? left->element : left;
        const auto b = is_vector(right) ? right->element : right;
        if ((!is_integer(a) && !is_floating(a)) ||
            (!is_integer(b) && !is_floating(b))) return {};
        const bool shift = operation == "<<" || operation == ">>";
        if ((is_floating(a) || is_floating(b)) &&
            (shift || operation == "%" || operation == "&" || operation == "|" || operation == "^")) return {};
        if (shift && (!is_vector(left) || !is_integer(a) || !is_integer(b))) return {};
        const auto element = is_floating(a) || is_floating(b)
            ? common_floating_type(a, b)
            : arithmetic_integer_result(shift ? promote_integer(integer_type(a))
                : common_integer_type(integer_type(a), integer_type(b)), a, shift ? TypePtr{} : b);
        return vector_type(element, shape->lanes);
    }

    TypePtr vector_mask_type(const TypePtr& type) const {
        auto bits = type_bits(type->element);
        if (type->element->builtin == BuiltinType::Fptr ||
            type->element->builtin == BuiltinType::Iptr ||
            type->element->builtin == BuiltinType::Uptr) bits = program_.address_bits;
        return vector_type(builtin_integer({bits, true}), type->lanes);
    }

    std::optional<EvalValue> vector_binary_values(std::string_view operation,
        const EvalValue& left, const EvalValue& right, SourceLocation location) {
        const auto common = vector_operation_type(left.type, right.type, operation);
        if (!common || operation == "&&" || operation == "||") {
            fail(location, "translation-time vector operator requires compatible lane shapes and numeric elements");
            return std::nullopt;
        }
        const bool comparison = operation == "==" || operation == "!=" || operation == "<" ||
            operation == "<=" || operation == ">" || operation == ">=";
        auto result = new_object(comparison ? vector_mask_type(common) : common, location);
        if (!result) return std::nullopt;
        for (std::size_t lane = 0; lane < common->lanes; ++lane) {
            if (!step(location)) return std::nullopt;
            auto a = is_vector(left.type) ? read_meta_pointer(vector_lane_pointer(left, lane), location)
                                         : std::optional<EvalValue>(left);
            auto b = is_vector(right.type) ? read_meta_pointer(vector_lane_pointer(right, lane), location)
                                          : std::optional<EvalValue>(right);
            if (!a || !b) return std::nullopt;
            a = convert(*a, common->element, location);
            if (operation != "<<" && operation != ">>") b = convert(*b, common->element, location);
            if (!a || !b) return std::nullopt;
            auto value = scalar_binary_values(operation, *a, *b, location);
            if (!value) return std::nullopt;
            if (comparison) *value = EvalValue{value->truthy()
                ? mask_to(bit_not(UInt128{}), integer_type(result->type->element).bits) : UInt128{},
                result->type->element};
            if (!store_meta_pointer(vector_lane_pointer(*result, lane), value, location)) return std::nullopt;
        }
        return result;
    }

    std::optional<EvalValue> initialize_object(const Expr& source,
                                               const TypePtr& type) {
        auto value = new_object(type, source.location, true);
        if (!value) return std::nullopt;
        if (source.kind == Expr::Kind::String && type->kind == Type::Kind::Array &&
            type->element->kind == Type::Kind::Builtin && type->element->builtin == BuiltinType::U8) {
            auto text = source.string_value.empty() ? decode_string_literal(source.text)
                                                   : std::optional<std::string>(source.string_value);
            if (!text || text->size() >= value->object->data.size()) {
                fail(source.location, "string initializer does not fit in the u8 array");
                return std::nullopt;
            }
            std::copy(text->begin(), text->end(), value->object->data.begin());
            return value;
        }
        if (!program_.evaluation_initializer_plan) {
            fail(source.location, "target initializer layout is unavailable during translation-time execution");
            return std::nullopt;
        }
        // Designators are required constants, independent of current local values.
        auto normalized = clone_expr(source);
        const auto normalize = [&](const auto& self, Expr& list) -> bool {
            for (auto& entry : list.initializer_entries) {
                for (auto& designator : entry.designators) {
                    if (!designator.index) continue;
                    Evaluator constants(program_, diagnostics_, current_function_, current_namespace_);
                    auto index = constants.required_integer(*designator.index, builtin_type(BuiltinType::Uptr));
                    if (!index) {
                        fail(designator.location, "array initializer designator requires a nonnegative integer constant");
                        return false;
                    }
                    designator.index->kind = Expr::Kind::Integer;
                    designator.index->evaluated_integer = Expr::IntegerConstant{index->integer, BuiltinType::Uptr};
                }
                if (entry.value && entry.value->kind == Expr::Kind::AggregateInitializer &&
                    !self(self, *entry.value)) return false;
            }
            return true;
        };
        if (!normalize(normalize, *normalized)) return std::nullopt;
        const auto plan = program_.evaluation_initializer_plan(*normalized, type);
        if (!plan.valid) {
            fail(plan.error_location.file ? plan.error_location : source.location,
                 plan.error_message.empty() ? "invalid aggregate initializer" : plan.error_message);
            return std::nullopt;
        }
        for (const auto& item : plan.items) {
            if (!step(item.expression->location)) return std::nullopt;
            auto pointer = object_pointer(*value);
            auto destination = clone_type(item.type);
            destination->is_const = false; // Initialization, not a modifying const access.
            pointer.type = pointer_type(destination);
            pointer.meta_pointer->view_offset = static_cast<std::size_t>(item.layout.offset);
            pointer.meta_pointer->view_length = meta_object_size(destination).value_or(0);
            pointer.meta_pointer->access_alignment = item.layout.alignment;
            pointer.meta_pointer->union_member_view = true; // Destination types already stamped.
            if (item.layout.bit_width) pointer.meta_pointer->bit_field = EvalMetaPointer::BitField{
                *item.layout.bit_width, item.layout.bit_offset, {}, 0};
            if (item.type->kind == Type::Kind::Array && item.expression->kind == Expr::Kind::String) {
                auto nested = initialize_object(*item.expression, item.type);
                if (!nested) return std::nullopt;
                std::copy(nested->object->data.begin(), nested->object->data.end(),
                          value->object->data.begin() + static_cast<std::ptrdiff_t>(item.layout.offset));
            } else if (!store_meta_pointer(pointer, this->expression(*item.expression),
                                           item.expression->location)) return std::nullopt;
        }
        return value;
    }

    std::optional<EvalValue> convert(EvalValue value, const TypePtr& type,
                                     SourceLocation location, bool explicit_cast = false) {
        if (!type || type->is_volatile || type->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            return std::nullopt;
        }
        if (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool && value.pointer()) {
            if (value.address && value.address->kind != AddressConstant::Kind::Absolute) {
                fail(location, "emitted object addresses cannot be inspected during translation-time evaluation");
                return std::nullopt;
            }
            return EvalValue{UInt128{value.truthy()}, clone_type(type)};
        }
        if (value.syntax_match || type->kind == Type::Kind::SyntaxMatch) {
            if (!procedural_ || !value.syntax_match || type->kind != Type::Kind::SyntaxMatch) {
                fail(location, "syntax matches cannot convert to runtime or other meta values");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (value.syntax_node || type->kind == Type::Kind::Syntax) {
            if (!procedural_ || !value.syntax_node || type->kind != Type::Kind::Syntax) {
                fail(location, "syntax nodes cannot convert to runtime or other meta values");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (value.syntax_span || type->kind == Type::Kind::Span) {
            if (!procedural_ || !value.syntax_span || type->kind != Type::Kind::Span) {
                fail(location, "syntax spans cannot convert to runtime or other meta values");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
        }
        if (value.syntax_context || type->kind == Type::Kind::Context ||
            (value.type && value.type->kind == Type::Kind::Context)) {
            if (!procedural_ || !value.syntax_context || type->kind != Type::Kind::Context) {
                fail(location, "syntax contexts cannot convert to runtime or other meta values");
                return std::nullopt;
            }
            value.type = clone_type(type);
            return value;
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
        if (type->kind == Type::Kind::Vector && !type->scalable &&
            (!value.object || is_vector(value.type))) {
            if (is_vector(value.type) &&
                (value.type->scalable || value.type->lanes != type->lanes)) {
                fail(location, "translation-time vector conversion requires matching lane shapes");
                return std::nullopt;
            }
            auto result = new_object(type, location);
            if (!result) return std::nullopt;
            for (std::size_t lane = 0; lane < type->lanes; ++lane) {
                if (!step(location)) return std::nullopt;
                auto scalar = value.object ? read_meta_pointer(vector_lane_pointer(value, lane), location)
                                           : std::optional<EvalValue>(value);
                if (!scalar) return std::nullopt;
                scalar = convert(*scalar, type->element, location, explicit_cast);
                if (!scalar || !store_meta_pointer(vector_lane_pointer(*result, lane), scalar, location))
                    return std::nullopt;
            }
            return result;
        }
        if (value.object || type->kind == Type::Kind::Vector ||
            type->kind == Type::Kind::Record) {
            if (!value.object || !value.type ||
                (value.type->kind != Type::Kind::Vector &&
                 value.type->kind != Type::Kind::Record) ||
                value.type->kind != type->kind) {
                fail(location, "translation-time aggregate conversion requires a matching value");
                return std::nullopt;
            }
            auto from = clone_type(value.type);
            auto to = clone_type(type);
            from->is_const = to->is_const = false;
            if (!same_type(from, to) || from->scalable || to->scalable) {
                fail(location, "translation-time aggregate conversion requires the same complete type");
                return std::nullopt;
            }
            const auto size = value.object->data.size();
            EvalValue object_pointer{UInt128{}, pointer_type(clone_type(value.type))};
            EvalMetaPointer pointer;
            pointer.mutable_buffer = value.object;
            pointer.view_length = size;
            object_pointer.meta_pointer = std::move(pointer);
            if (!validate_meta_object_value(object_pointer, location))
                return std::nullopt;
            if (size > std::numeric_limits<std::size_t>::max() / 3)
                return std::nullopt;
            auto cost = size * 3;
            for (const auto& slot : value.object->pointers)
                cost += 64 + type_name(slot.value->type).size();
            for (const auto& object : value.object->typed_objects) {
                const auto metadata = 24 + type_name(object.type).size();
                if (metadata > std::numeric_limits<std::size_t>::max() - cost)
                    return std::nullopt;
                cost += metadata;
            }
            if (!charge_meta_bytes(cost, location)) return std::nullopt;
            value.object = std::make_shared<EvalBuffer>(*value.object);
            value.type = clone_type(type);
            if (!stamp_meta_object_types(*value.object, value.type, 0, location))
                return std::nullopt;
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
                (!meta_object_type(type->pointee) &&
                 !void_pointee(type->pointee)) ||
                meta_volatile_or_atomic(type->pointee) ||
                !value.type || value.type->kind != Type::Kind::Pointer ||
                !value.type->pointee ||
                (meta_leaf_const(value.type->pointee) &&
                 !meta_leaf_const(type->pointee)) ||
                type->address_space != value.type->address_space ||
                 (!explicit_cast &&
                 !compatible_pointee(value.type->pointee, type->pointee))) {
                fail(location, "meta data pointers require a compatible scalar, fixed-array, record, fixed-vector, or void pointer conversion without qualifier loss");
                return std::nullopt;
            }
            if (value.meta_pointer->mutable_buffer &&
                value.meta_pointer->mutable_buffer->frozen) {
                fail(location, "buffer data pointer was used after freeze");
                return std::nullopt;
            }
            if (!same_type(value.type->pointee, type->pointee)) {
                value.meta_pointer->access_alignment.reset();
                value.meta_pointer->union_member_view = false;
                value.meta_pointer->bit_field.reset();
            }
            value.type = clone_type(type);
            return value;
        }
        if (type->kind == Type::Kind::Pointer && is_integer(value.type) && value.integer == UInt128{}) {
            EvalValue result{UInt128{}, clone_type(type)};
            result.address = AddressConstant{};
            return result;
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
        if (failure_reason_) return;
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

    Cell* lookup_mutable(const Expr& expression) {
        for (auto index = scopes_.size(); index > frame_base_;) {
            auto& scope = scopes_[--index];
            const auto found = scope.find(name_key(expression));
            if (found != scope.end()) return &found->second;
        }
        return nullptr;
    }

    std::optional<EvalValue> lookup(const Expr& expression) {
        const auto& name = expression.text;
        const auto location = expression.location;
        if (auto* cell = lookup_mutable(expression)) {
            if (cell->storage) {
                EvalValue storage{UInt128{}, cell->value.type};
                storage.object = cell->storage;
                return read_meta_pointer(object_pointer(storage), location);
            }
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
            if (cell->value.object) return read_meta_pointer(object_pointer(cell->value), location);
            return cell->value;
        }
        if (const auto found = resolve_enumerator(
                program_, current_function_, current_namespace_, expression);
            found && found->enumerator->value) {
            return EvalValue{
                found->enumerator->value->value,
                enum_type(*found->enumeration)};
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

    static bool meta_object_type(const TypePtr& type) {
        if (meta_scalar_type(type)) return true;
        if (type && type->kind == Type::Kind::Pointer) return true;
        if (type && (type->kind == Type::Kind::Record ||
                     (type->kind == Type::Kind::Vector && !type->scalable)))
            return true;
        return type && type->kind == Type::Kind::Array && type->lanes != 0 &&
            meta_object_type(type->element);
    }

    static bool meta_leaf_const(const TypePtr& type) {
        return type && (type->is_const ||
            (type->kind == Type::Kind::Array &&
             meta_leaf_const(type->element)));
    }

    static bool meta_volatile_or_atomic(const TypePtr& type) {
        return type && (type->is_volatile || type->is_atomic ||
            (type->kind == Type::Kind::Array &&
             meta_volatile_or_atomic(type->element)));
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

    static UInt128 meta_bit_field_mask(
        const EvalMetaPointer::BitField& field) {
        return shift_left(mask_to(bit_not(UInt128{}), field.width),
                          field.offset);
    }

    UInt128 extract_meta_bit_field(UInt128 storage,
                                   const EvalValue& base) const {
        const auto& field = *base.meta_pointer->bit_field;
        auto value = mask_to(shift_right(storage, field.offset), field.width);
        const auto type = integer_type(base.type->pointee);
        if (type.is_signed && bit(value, field.width - 1))
            value = bit_or(value, bit_not(mask_to(bit_not(UInt128{}),
                                                 field.width)));
        return mask_to(value, type.bits);
    }

    std::optional<std::size_t> meta_object_size(const TypePtr& type) const {
        if (type && type->kind == Type::Kind::Pointer) {
            const auto size = size_of_ ? (*size_of_)(type)
                : std::optional<std::uint64_t>((program_.address_bits + 7U) / 8U);
            return size && *size <= std::numeric_limits<std::size_t>::max()
                ? std::optional<std::size_t>(static_cast<std::size_t>(*size)) : std::nullopt;
        }
        if (!meta_object_type(type)) return std::nullopt;
        if (type->kind == Type::Kind::Record ||
            type->kind == Type::Kind::Vector) {
            if (!size_of_) return std::nullopt;
            const auto size = (*size_of_)(type);
            if (!size || *size == 0 ||
                *size > std::numeric_limits<std::size_t>::max())
                return std::nullopt;
            return static_cast<std::size_t>(*size);
        }
        if (type->kind != Type::Kind::Array) {
            EvalValue value;
            value.type = pointer_type(type);
            return meta_scalar_size(value);
        }
        const auto element = meta_object_size(type->element);
        if (!element || *element == 0 ||
            type->lanes > std::numeric_limits<std::size_t>::max() / *element)
            return std::nullopt;
        return *element * type->lanes;
    }

    std::optional<std::size_t> meta_object_alignment(const TypePtr& type) const {
        if (type->kind == Type::Kind::Pointer && align_of_) {
            const auto alignment = (*align_of_)(type);
            return alignment && *alignment != 0 && *alignment <= std::numeric_limits<std::size_t>::max()
                ? std::optional<std::size_t>(static_cast<std::size_t>(*alignment)) : std::nullopt;
        }
        if (type->kind == Type::Kind::Array)
            return meta_object_alignment(type->element);
        if (type->kind == Type::Kind::Record ||
            type->kind == Type::Kind::Vector) {
            if (!align_of_) return std::nullopt;
            const auto alignment = (*align_of_)(type);
            if (!alignment || *alignment == 0 ||
                *alignment > std::numeric_limits<std::size_t>::max())
                return std::nullopt;
            return static_cast<std::size_t>(*alignment);
        }
        const auto size = *meta_object_size(type);
        return type->builtin == BuiltinType::F80
            ? std::max<std::size_t>(1, program_.evaluation_layout.f80_alignment)
            : std::max<std::size_t>(1, std::min<std::size_t>(size,
                program_.evaluation_layout.natural_alignment_limit));
    }

    static bool same_meta_object_type(const TypePtr& left, const TypePtr& right) {
        auto a = clone_type(left);
        auto b = clone_type(right);
        a->is_const = b->is_const = false;
        return same_type(a, b);
    }

    bool meta_record_subobject(const TypePtr& owner, std::size_t offset,
                               const TypePtr& requested, std::size_t length) const {
        const auto size = meta_object_size(owner);
        if (!size || offset > *size || length > *size - offset) return false;
        if (offset == 0 && length == *size && same_meta_object_type(owner, requested))
            return true;
        if (offset == 0 && length == *size && owner->nominal_key().empty() &&
            requested->nominal_key().empty() && meta_scalar_type(owner) &&
            meta_scalar_type(requested) && compatible_meta_type(owner->builtin, requested->builtin)) return true;
        if (owner->kind == Type::Kind::Array || owner->kind == Type::Kind::Vector) {
            const auto stride = meta_object_size(owner->element);
            return stride && *stride != 0 &&
                meta_record_subobject(owner->element, offset % *stride,
                                      requested, length);
        }
        if (owner->kind != Type::Kind::Record || owner->is_union) return false;
        const auto record = std::find_if(program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.nominal_key() == owner->nominal_key() && candidate.complete;
            });
        if (record == program_.records.end()) return false;
        for (const auto& member : record->members) {
            if (member.name.empty()) continue;
            const auto layout = program_.evaluation_member_layout
                ? program_.evaluation_member_layout(owner, member.name) : std::nullopt;
            const auto member_size = meta_object_size(member.type);
            if (layout && member_size && layout->offset <= offset &&
                offset - layout->offset <= *member_size &&
                length <= *member_size - (offset - layout->offset) &&
                meta_record_subobject(member.type,
                    offset - static_cast<std::size_t>(layout->offset),
                    requested, length)) return true;
        }
        return false;
    }

    bool meta_record_effective_access(const EvalValue& base,
                                      SourceLocation location, bool write = false) {
        if (!base.meta_pointer || !base.meta_pointer->mutable_buffer ||
            base.meta_pointer->union_member_view) return true;
        const auto index = meta_access_index(base, location, write);
        const auto size = meta_object_size(base.type->pointee);
        if (!index || !size) return false;
        for (const auto& object : base.meta_pointer->mutable_buffer->typed_objects) {
            if (*index >= object.offset + object.length ||
                object.offset >= *index + *size) continue;
            const bool contained = *index >= object.offset &&
                *index - object.offset <= object.length &&
                *size <= object.length - (*index - object.offset) &&
                meta_record_subobject(object.type, *index - object.offset,
                                      base.type->pointee, *size);
            const bool containing = object.offset >= *index &&
                object.offset - *index <= *size &&
                object.length <= *size - (object.offset - *index) &&
                meta_record_subobject(base.type->pointee, object.offset - *index,
                                      object.type, object.length);
            if (!contained && !containing) {
                fail(location, write ? "meta pointer write violates aggregate effective type"
                                     : "meta pointer read violates aggregate effective type");
                return false;
            }
        }
        return true;
    }

    bool register_meta_record(EvalBuffer& storage, std::size_t offset,
                              const TypePtr& type, SourceLocation location) {
        if (type->kind != Type::Kind::Record && type->kind != Type::Kind::Pointer &&
            type->nominal_key().empty()) return true;
        const auto size = meta_object_size(type);
        if (!size) return false;
        for (const auto& object : storage.typed_objects)
            if (offset >= object.offset && offset - object.offset <= object.length &&
                *size <= object.length - (offset - object.offset)) return true;
        // Account for logical range and type identity, not host shared_ptr size.
        if (!charge_meta_bytes(24 + type_name(type).size(), location)) return false;
        std::erase_if(storage.typed_objects, [&](const EvalBuffer::TypedObject& object) {
            return object.offset >= offset && object.offset - offset <= *size &&
                object.length <= *size - (object.offset - offset);
        });
        storage.typed_objects.push_back({offset, *size, clone_type(type)});
        return true;
    }

    bool meta_bit_field_record_view(const EvalMetaPointer& pointer) const {
        if (!pointer.bit_field || !pointer.bit_field->owner ||
            !pointer.mutable_buffer) return false;
        const auto& field = *pointer.bit_field;
        const auto size = meta_object_size(field.owner);
        if (!size) return false;
        for (const auto& object : pointer.mutable_buffer->typed_objects)
            if (field.owner_offset >= object.offset &&
                field.owner_offset - object.offset <= object.length &&
                *size <= object.length - (field.owner_offset - object.offset) &&
                meta_record_subobject(object.type, field.owner_offset - object.offset,
                                      field.owner, *size)) return true;
        return false;
    }

    bool meta_object_accepts_tag_at(const TypePtr& type, std::size_t offset,
                                    std::uint8_t tag) const {
        const auto size = meta_object_size(type);
        if (!size || offset >= *size) return false;
        if (meta_scalar_type(type))
            return tag != 0xffU && compatible_meta_type(
                static_cast<BuiltinType>(tag - 1), type->builtin);
        if (type->kind == Type::Kind::Pointer) return tag == 0xffU;
        if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) {
            const auto stride = meta_object_size(type->element);
            return stride && *stride != 0 &&
                meta_object_accepts_tag_at(type->element, offset % *stride, tag);
        }
        if (type->kind != Type::Kind::Record) return false;
        if (type->is_union) return true;
        const auto record = std::find_if(program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.nominal_key() == type->nominal_key() && candidate.complete;
            });
        if (record == program_.records.end()) return false;
        for (const auto& member : record->members) {
            if (member.name.empty()) continue;
            const auto layout = program_.evaluation_member_layout
                ? program_.evaluation_member_layout(type, member.name) : std::nullopt;
            const auto member_size = meta_object_size(member.type);
            if (layout && member_size && layout->offset <= offset &&
                offset - layout->offset < *member_size &&
                meta_object_accepts_tag_at(member.type,
                    offset - static_cast<std::size_t>(layout->offset), tag)) return true;
        }
        return tag == 0xffU; // Padding already belonging to an aggregate.
    }

    // A whole aggregate write establishes the declared leaf types. Padding
    // and unions retain an aggregate tag rather than becoming raw storage;
    // projected union members still have their explicit reinterpretation rule.
    bool stamp_meta_object_types(EvalBuffer& storage, const TypePtr& type,
                                 std::size_t offset, SourceLocation location) {
        const auto size = meta_object_size(type);
        if (!size || offset > storage.data.size() ||
            *size > storage.data.size() - offset ||
            meta_volatile_or_atomic(type)) {
            fail(location, "meta aggregate requires supported non-volatile object members");
            return false;
        }
        if (meta_scalar_type(type)) {
            std::fill_n(storage.effective_type.begin() +
                            static_cast<std::ptrdiff_t>(offset), *size,
                        static_cast<std::uint8_t>(
                            static_cast<std::uint8_t>(type->builtin) + 1));
            return register_meta_record(storage, offset, type, location);
        }
        if (type->kind == Type::Kind::Pointer) {
            std::fill_n(storage.effective_type.begin() + static_cast<std::ptrdiff_t>(offset),
                        *size, static_cast<std::uint8_t>(0xffU));
            return register_meta_record(storage, offset, type, location);
        }
        if (type->kind == Type::Kind::Array ||
            type->kind == Type::Kind::Vector) {
            const auto stride = meta_object_size(type->element);
            if (!stride) return false;
            for (std::size_t lane = 0; lane < type->lanes; ++lane)
                if (!stamp_meta_object_types(storage, type->element,
                        offset + lane * *stride, location)) return false;
            return true;
        }
        if (type->kind != Type::Kind::Record) return false;
        std::fill_n(storage.effective_type.begin() +
                        static_cast<std::ptrdiff_t>(offset), *size,
                    static_cast<std::uint8_t>(0xffU));
        if (type->is_union) return true;
        const auto record = std::find_if(program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.nominal_key() == type->nominal_key() && candidate.complete;
            });
        if (record == program_.records.end()) return false;
        for (const auto& member : record->members) {
            if (member.name.empty()) continue;
            const auto layout = program_.evaluation_member_layout
                ? program_.evaluation_member_layout(type, member.name) : std::nullopt;
            if (!layout || layout->offset > *size ||
                !stamp_meta_object_types(storage, member.type,
                    offset + static_cast<std::size_t>(layout->offset), location))
                return false;
        }
        return true;
    }

    bool validate_meta_object_value(const EvalValue& base,
                                    SourceLocation location) {
        const auto index = meta_access_index(base, location);
        if (!index) return false;
        const auto type = base.type->pointee;
        if (meta_volatile_or_atomic(type)) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            return false;
        }
        if (meta_scalar_type(type) || type->kind == Type::Kind::Pointer)
            return read_meta_pointer(base, location).has_value();
        if (type->kind == Type::Kind::Record &&
            !meta_record_effective_access(base, location)) return false;
        // Union assignment copies its representation, including indeterminate
        // inactive storage. A later selected-member read validates that value.
        if (type->kind == Type::Kind::Record && type->is_union) return true;
        const auto project = [&](const TypePtr& child, std::size_t offset,
                                 std::size_t alignment,
                                 std::optional<EvalMetaPointer::BitField> field = {}) {
            EvalValue value = base;
            value.type = pointer_type(clone_type(child));
            auto& pointer = *value.meta_pointer;
            pointer.view_offset = *index + offset;
            pointer.view_length = meta_object_size(child).value_or(0);
            pointer.position = 0;
            pointer.access_alignment = std::min<std::size_t>(
                base.meta_pointer->access_alignment.value_or(
                    *meta_object_alignment(type)), alignment);
            pointer.bit_field = field;
            return value;
        };
        if (type->kind == Type::Kind::Array ||
            type->kind == Type::Kind::Vector) {
            const auto stride = meta_object_size(type->element);
            const auto alignment = meta_object_alignment(type->element);
            if (!stride || !alignment) return false;
            if (type->kind == Type::Kind::Vector &&
                base.meta_pointer->mutable_buffer &&
                !base.meta_pointer->union_member_view) {
                const auto& tags = base.meta_pointer->mutable_buffer->effective_type;
                for (std::size_t byte = 0; byte < *meta_object_size(type); ++byte) {
                    const auto tag = tags[*index + byte];
                    if (tag != 0 && !compatible_meta_type(
                            static_cast<BuiltinType>(tag - 1), type->element->builtin)) {
                        fail(location, "meta pointer read violates effective type");
                        return false;
                    }
                }
            }
            for (std::size_t lane = 0; lane < type->lanes; ++lane)
                if (!validate_meta_object_value(
                        project(type->element, lane * *stride, *alignment), location))
                    return false;
            return true;
        }
        if (type->kind != Type::Kind::Record) return false;
        const auto record = std::find_if(program_.records.begin(), program_.records.end(),
            [&](const RecordDecl& candidate) {
                return candidate.nominal_key() == type->nominal_key() && candidate.complete;
            });
        if (record == program_.records.end()) return false;
        for (const auto& member : record->members) {
            if (member.name.empty()) continue;
            const auto layout = program_.evaluation_member_layout
                ? program_.evaluation_member_layout(type, member.name) : std::nullopt;
            const auto alignment = meta_object_type(member.type)
                ? meta_object_alignment(member.type) : std::nullopt;
            const auto size = meta_object_size(member.type);
            const auto record_size = meta_object_size(type);
            if (!layout || !alignment || !size || !record_size ||
                layout->offset > *record_size ||
                *size > *record_size - layout->offset) {
                fail(location, "meta aggregate requires supported complete object members");
                return false;
            }
            std::optional<EvalMetaPointer::BitField> field;
            if (layout->bit_width) {
                if (*layout->bit_width == 0 || *layout->bit_width > *size * 8U ||
                    layout->bit_offset > *size * 8U - *layout->bit_width) {
                    fail(location, "meta bit-field has invalid target layout");
                    return false;
                }
                field = EvalMetaPointer::BitField{*layout->bit_width,
                    layout->bit_offset, clone_type(type), *index};
            }
            if (!validate_meta_object_value(project(member.type,
                    static_cast<std::size_t>(layout->offset),
                    std::min<std::size_t>(layout->alignment, *alignment), field),
                    location)) return false;
        }
        return true;
    }

    bool live_meta_pointer(const EvalValue& value, SourceLocation location) {
        if (!value.meta_pointer || !value.type ||
            value.type->kind != Type::Kind::Pointer || !value.type->pointee) {
            fail(location, "invalid meta pointer value");
            return false;
        }
        if (value.meta_pointer->mutable_buffer &&
            !value.meta_pointer->mutable_buffer->alive) {
            fail(location, "translation-time pointer refers to an object outside its lifetime");
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

    bool sized_meta_pointer(const EvalValue& value, SourceLocation location) {
        if (!live_meta_pointer(value, location)) return false;
        if (!meta_object_type(value.type->pointee) ||
            !meta_object_size(value.type->pointee)) {
            fail(location, "meta pointer arithmetic requires a supported complete object type");
            return false;
        }
        return true;
    }

    std::optional<std::size_t> meta_access_index(const EvalValue& base,
                                                  SourceLocation location,
                                                  bool write = false) {
        if (!sized_meta_pointer(base, location)) return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = *meta_object_size(base.type->pointee);
        if (pointer.position > pointer.view_length ||
            size > pointer.view_length - pointer.position) {
            fail(location, write ? "meta pointer write is outside its view"
                                 : "meta pointer read is outside its view");
            return std::nullopt;
        }
        const auto index = pointer.view_offset + pointer.position;
        const auto alignment = pointer.access_alignment
            ? pointer.access_alignment
            : meta_object_alignment(base.type->pointee);
        if (!alignment || index % *alignment != 0) {
            fail(location, base.type->pointee->kind == Type::Kind::Vector
                ? "misaligned meta pointer access for target vector type"
                : base.type->pointee->kind == Type::Kind::Record
                    ? "misaligned meta pointer access for target record type"
                : "misaligned meta pointer access for target scalar type");
            return std::nullopt;
        }
        return index;
    }

    std::optional<EvalValue> decay_meta_array(EvalValue base,
                                              SourceLocation location) {
        if (!sized_meta_pointer(base, location) ||
            base.type->pointee->kind != Type::Kind::Array) return std::nullopt;
        auto& pointer = *base.meta_pointer;
        const auto size = *meta_object_size(base.type->pointee);
        if (pointer.position > pointer.view_length ||
            size > pointer.view_length - pointer.position) {
            fail(location, "meta pointer read is outside its view");
            return std::nullopt;
        }
        const auto offset = pointer.view_offset + pointer.position;
        const auto alignment = pointer.access_alignment
            ? pointer.access_alignment
            : meta_object_alignment(base.type->pointee);
        if (!alignment || offset % *alignment != 0) {
            fail(location, "misaligned meta pointer access for target scalar type");
            return std::nullopt;
        }
        pointer.view_offset = offset;
        pointer.view_length = size;
        pointer.position = 0;
        auto element = clone_type(base.type->pointee->element);
        element->is_const = element->is_const || base.type->pointee->is_const;
        element->is_volatile = element->is_volatile || base.type->pointee->is_volatile;
        auto type = pointer_type(element);
        type->address_space = base.type->address_space;
        base.type = std::move(type);
        return base;
    }

    std::optional<EvalValue> meta_member_pointer(
        const Expr& expression) {
        if (!expression.left || !expression.right ||
            expression.right->kind != Expr::Kind::Name)
            return std::nullopt;
        auto base = expression.text == "member"
            ? meta_designator_pointer(*expression.left)
            : this->expression(*expression.left);
        if (!base || !base->meta_pointer || !sized_meta_pointer(*base, expression.location) ||
            base->type->pointee->kind != Type::Kind::Record) {
            fail(expression.location,
                 "meta record member access requires a supported structure pointer");
            return std::nullopt;
        }
        if (!meta_record_effective_access(*base, expression.location))
            return std::nullopt;
        const auto* member = selected_record_member(expression);
        const auto layout = program_.evaluation_member_layout
            ? program_.evaluation_member_layout(base->type->pointee,
                                                 expression.right->text)
            : std::nullopt;
        if (!member || !layout ||
            !meta_object_type(member->type) ||
            (layout->bit_width && !is_integer(member->type)) ||
            meta_volatile_or_atomic(member->type)) {
            fail(expression.location,
                 "meta record member requires a supported non-volatile object type");
            return std::nullopt;
        }
        const auto member_size = meta_object_size(member->type);
        const auto record_size = meta_object_size(base->type->pointee);
        if (!member_size || !record_size ||
            layout->offset > *record_size ||
            *member_size > *record_size - layout->offset) {
            fail(expression.location, "meta record member is outside its target layout");
            return std::nullopt;
        }
        const auto member_alignment = meta_object_alignment(member->type);
        const auto record_alignment =
            meta_object_alignment(base->type->pointee);
        if (!member_alignment || !record_alignment) {
            fail(expression.location, "meta record alignment is unavailable");
            return std::nullopt;
        }
        auto& pointer = *base->meta_pointer;
        if (pointer.position > pointer.view_length ||
            *record_size > pointer.view_length - pointer.position) {
            fail(expression.location, "meta pointer read is outside its view");
            return std::nullopt;
        }
        const auto record_offset = pointer.view_offset + pointer.position;
        const auto effective_record_alignment = pointer.access_alignment
            .value_or(*record_alignment);
        if (record_offset % effective_record_alignment != 0) {
            fail(expression.location, "misaligned meta pointer access for target record type");
            return std::nullopt;
        }
        pointer.access_alignment = std::min<std::size_t>(
            effective_record_alignment,
            std::min<std::size_t>(layout->alignment, *member_alignment));
        pointer.union_member_view |= base->type->pointee->is_union;
        if (layout->bit_width) {
            const auto storage_bits = *member_size * 8U;
            if (*layout->bit_width == 0 ||
                *layout->bit_width > storage_bits ||
                layout->bit_offset > storage_bits - *layout->bit_width) {
                fail(expression.location, "meta bit-field has invalid target layout");
                return std::nullopt;
            }
            pointer.bit_field = EvalMetaPointer::BitField{
                *layout->bit_width, layout->bit_offset,
                clone_type(base->type->pointee), record_offset};
        } else {
            pointer.bit_field.reset();
        }
        pointer.view_offset = record_offset +
            static_cast<std::size_t>(layout->offset);
        pointer.view_length = *member_size;
        pointer.position = 0;
        auto type = clone_type(member->type);
        type->is_const = type->is_const || base->type->pointee->is_const;
        type->is_volatile = type->is_volatile ||
            base->type->pointee->is_volatile;
        auto pointer_type_value = pointer_type(type);
        pointer_type_value->address_space = base->type->address_space;
        base->type = std::move(pointer_type_value);
        return base;
    }

    std::optional<EvalValue> meta_designator_pointer(const Expr& source) {
        const Expr* designator = &source;
        while (designator->kind == Expr::Kind::Parenthesized &&
               designator->left)
            designator = designator->left.get();
        if (designator->kind == Expr::Kind::Name) {
            auto* cell = lookup_mutable(*designator);
            if (cell && !cell->value.object && meta_object_type(cell->value.type)) {
                if (!cell->storage) {
                    auto storage = new_object(cell->value.type, designator->location);
                    if (!storage) return std::nullopt;
                    cell->storage = storage->object;
                    if (cell->initialized) {
                        auto pointer = object_pointer(*storage);
                        pointer.type->pointee->is_const = false;
                        if (!store_meta_pointer(pointer, cell->value, designator->location)) return std::nullopt;
                    }
                }
                EvalValue value{UInt128{}, cell->value.type};
                value.object = cell->storage;
                return object_pointer(value);
            }
            if (cell && cell->value.object && cell->value.type &&
                (cell->value.type->kind == Type::Kind::Vector ||
                 cell->value.type->kind == Type::Kind::Record ||
                 cell->value.type->kind == Type::Kind::Array)) {
                EvalValue value{UInt128{}, pointer_type(clone_type(cell->value.type))};
                EvalMetaPointer pointer;
                pointer.mutable_buffer = cell->value.object;
                pointer.view_length = cell->value.object->data.size();
                value.meta_pointer = std::move(pointer);
                return value;
            }
        }
        if (designator->kind == Expr::Kind::Unary &&
            designator->text == "*" && designator->left)
            return this->expression(*designator->left);
        if (designator->kind == Expr::Kind::Binary &&
            (designator->text == "member" ||
             designator->text == "pointer_member"))
            return meta_member_pointer(*designator);
        if (designator->kind == Expr::Kind::Binary &&
            designator->text == "index" && designator->left &&
            designator->right) {
            const auto owner = expression_type(*designator->left);
            if (owner && owner->kind == Type::Kind::Vector)
                return meta_vector_lane_pointer(*designator);
            auto base = this->expression(*designator->left);
            auto index = this->expression(*designator->right);
            if (!base || !index || !base->meta_pointer) return std::nullopt;
            return meta_pointer_offset(*base, *index, false,
                                       designator->location);
        }
        auto value = this->expression(*designator);
        if (value && value->object && value->type &&
            (value->type->kind == Type::Kind::Vector ||
             value->type->kind == Type::Kind::Record)) {
            auto owner = clone_type(value->type);
            owner->is_const = true;
            EvalValue pointer_value{UInt128{}, pointer_type(owner)};
            EvalMetaPointer pointer;
            pointer.mutable_buffer = value->object;
            pointer.view_length = value->object->data.size();
            pointer_value.meta_pointer = std::move(pointer);
            return pointer_value;
        }
        return std::nullopt;
    }

    std::optional<EvalValue> meta_vector_lane_pointer(const Expr& source) {
        if (!source.left || !source.right) return std::nullopt;
        auto base = meta_designator_pointer(*source.left);
        if (!base) {
            auto value = this->expression(*source.left);
            if (!value || !value->object || !value->type ||
                value->type->kind != Type::Kind::Vector) return std::nullopt;
            auto owner = clone_type(value->type);
            owner->is_const = true;
            base = EvalValue{UInt128{}, pointer_type(owner)};
            EvalMetaPointer pointer;
            pointer.mutable_buffer = value->object;
            pointer.view_length = value->object->data.size();
            base->meta_pointer = std::move(pointer);
        }
        if (!base->meta_pointer || !base->type ||
            base->type->kind != Type::Kind::Pointer ||
            !base->type->pointee ||
            base->type->pointee->kind != Type::Kind::Vector) return std::nullopt;
        const auto vector_type = base->type->pointee;
        const auto offset = meta_access_index(*base, source.location);
        if (!offset) return std::nullopt;
        auto lane = this->expression(*source.right);
        if (!lane || !is_integer(lane->type) ||
            integer_negative(lane->integer, integer_type(lane->type)) ||
            lane->integer.high != 0 || lane->integer.low >= vector_type->lanes) {
            fail(source.right->location, "meta vector lane index is outside its view");
            return std::nullopt;
        }
        const auto lane_size = meta_object_size(vector_type->element);
        const auto lane_alignment = meta_object_alignment(vector_type->element);
        if (!lane_size || !lane_alignment) return std::nullopt;
        auto& pointer = *base->meta_pointer;
        pointer.view_offset = *offset +
            static_cast<std::size_t>(lane->integer.low) * *lane_size;
        pointer.view_length = *lane_size;
        pointer.position = 0;
        pointer.access_alignment = std::min<std::size_t>(
            pointer.access_alignment.value_or(*meta_object_alignment(vector_type)),
            *lane_alignment);
        pointer.bit_field.reset();
        auto element = clone_type(vector_type->element);
        element->is_const = element->is_const || vector_type->is_const;
        base->type = pointer_type(element);
        return base;
    }

    std::optional<EvalValue> meta_pointer_offset(EvalValue base,
                                                 const EvalValue& index,
                                                 bool subtract,
                                                 SourceLocation location) {
        if (!sized_meta_pointer(base, location)) return std::nullopt;
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
        const auto stride = *meta_object_size(base.type->pointee);
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
        if (!sized_meta_pointer(left, location) ||
            !sized_meta_pointer(right, location)) return std::nullopt;
        if (!same_backing || a.view_offset != b.view_offset ||
            a.view_length != b.view_length ||
            meta_object_size(left.type->pointee) !=
                meta_object_size(right.type->pointee) ||
            (!compatible_pointee(left.type->pointee, right.type->pointee) &&
             !compatible_pointee(right.type->pointee, left.type->pointee)) ||
            left.type->address_space != right.type->address_space) {
            fail(location, "meta pointer ordering or subtraction requires one compatible view");
            return std::nullopt;
        }
        if (operation == "-") {
            const auto stride = *meta_object_size(left.type->pointee);
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
        if (base.type && base.type->pointee && base.type->pointee->kind == Type::Kind::Pointer) {
            const auto index = meta_access_index(base, location);
            if (!index || !meta_record_effective_access(base, location)) return std::nullopt;
            const auto size = *meta_object_size(base.type->pointee);
            const auto& pointer = *base.meta_pointer;
            UInt128 bits;
            for (std::size_t byte = 0; byte < size; ++byte) {
                if (pointer.mutable_buffer) {
                    if (pointer.mutable_buffer->assigned[*index + byte] != 0xffU) {
                        fail(location, "read of unassigned buffer byte");
                        return std::nullopt;
                    }
                    const auto tag = pointer.mutable_buffer->effective_type[*index + byte];
                    if (!pointer.union_member_view && tag != 0 && tag != 0xffU) {
                        fail(location, "meta pointer read violates effective type");
                        return std::nullopt;
                    }
                }
                const auto raw = pointer.mutable_buffer ? pointer.mutable_buffer->data[*index + byte]
                                                        : (*pointer.immutable)[*index + byte];
                const auto lane = program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                    ? byte : size - 1 - byte;
                bits = bit_or(bits, shift_left(UInt128{static_cast<unsigned char>(raw)},
                                              static_cast<unsigned>(lane * 8)));
            }
            if (pointer.mutable_buffer) {
                for (const auto& slot : pointer.mutable_buffer->pointers) {
                    if (*index >= slot.offset + slot.length || slot.offset >= *index + size) continue;
                    if (*index != slot.offset || size != slot.length) {
                        fail(location, "meta pointer read overlaps an opaque pointer representation");
                        return std::nullopt;
                    }
                    auto value = *slot.value;
                    if (slot.has_backing) {
                        value.meta_pointer->mutable_buffer = slot.backing.lock();
                        if (!value.meta_pointer->mutable_buffer) {
                            fail(location, "translation-time pointer refers to an object outside its lifetime");
                            return std::nullopt;
                        }
                    }
                    return convert(value, base.type->pointee, location, pointer.union_member_view);
                }
            }
            EvalValue value{UInt128{}, clone_type(base.type->pointee)};
            value.address = AddressConstant{AddressConstant::Kind::Absolute, bits};
            return value;
        }
        if (base.type && base.type->kind == Type::Kind::Pointer &&
            base.type->pointee &&
            base.type->pointee->kind == Type::Kind::Array)
            return decay_meta_array(base, location);
        if (base.type && base.type->kind == Type::Kind::Pointer &&
            base.type->pointee &&
            (base.type->pointee->kind == Type::Kind::Vector ||
             base.type->pointee->kind == Type::Kind::Record)) {
            const auto index = meta_access_index(base, location);
            if (!index) return std::nullopt;
            if (!validate_meta_object_value(base, location)) return std::nullopt;
            const auto size = *meta_object_size(base.type->pointee);
            const auto& pointer = *base.meta_pointer;
            if (size > std::numeric_limits<std::size_t>::max() / 3 ||
                !charge_meta_bytes(size * 3, location)) return std::nullopt;
            EvalValue value{UInt128{}, clone_type(base.type->pointee)};
            value.object = std::make_shared<EvalBuffer>();
            value.object->data = pointer.mutable_buffer
                ? pointer.mutable_buffer->data.substr(*index, size)
                : pointer.immutable->substr(*index, size);
            if (pointer.mutable_buffer) {
                value.object->assigned.assign(
                    pointer.mutable_buffer->assigned.begin() +
                        static_cast<std::ptrdiff_t>(*index),
                    pointer.mutable_buffer->assigned.begin() +
                        static_cast<std::ptrdiff_t>(*index + size));
                value.object->effective_type.assign(
                    pointer.mutable_buffer->effective_type.begin() +
                        static_cast<std::ptrdiff_t>(*index),
                    pointer.mutable_buffer->effective_type.begin() +
                        static_cast<std::ptrdiff_t>(*index + size));
                for (const auto& slot : pointer.mutable_buffer->pointers) {
                    if (slot.offset < *index || slot.offset - *index >= size) continue;
                    if (slot.length > size - (slot.offset - *index)) {
                        fail(location, "aggregate value overlaps an opaque pointer representation");
                        return std::nullopt;
                    }
                    if (!charge_meta_bytes(64 + type_name(slot.value->type).size(), location)) return std::nullopt;
                    auto copy = slot;
                    copy.offset -= *index;
                    value.object->pointers.push_back(std::move(copy));
                }
            } else {
                value.object->assigned.assign(size, 0xffU);
                value.object->effective_type.assign(size, 0);
            }
            if (!stamp_meta_object_types(*value.object, value.type, 0, location))
                return std::nullopt;
            if (!register_meta_record(*value.object, 0, value.type, location))
                return std::nullopt;
            return value;
        }
        if (!scalar_meta_pointer(base, location)) return std::nullopt;
        const auto index = meta_access_index(base, location);
        if (!index) return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = meta_scalar_size(base);
        const auto access_type = base.type->pointee->builtin;
        if (pointer.mutable_buffer) {
            for (const auto& slot : pointer.mutable_buffer->pointers) {
                if (*index < slot.offset + slot.length && slot.offset < *index + size) {
                    fail(location, "opaque translation-time pointer representation cannot be inspected as bytes or scalars");
                    return std::nullopt;
                }
            }
        }
        if (!byte_meta_type(access_type) && !pointer.bit_field &&
            !meta_record_effective_access(base, location)) return std::nullopt;
        UInt128 result;
        const auto field_mask = pointer.bit_field
            ? meta_bit_field_mask(*pointer.bit_field) : UInt128{};
        if (pointer.mutable_buffer) {
            for (std::size_t offset = 0; offset < size; ++offset) {
                const bool f80_padding = access_type == BuiltinType::F80 &&
                    (program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                         ? offset >= size - 10 : offset < size - 10);
                const auto lane = program_.evaluation_layout.byte_order ==
                    EvaluationByteOrder::Little ? offset : size - 1 - offset;
                const auto required = pointer.bit_field
                    ? static_cast<std::uint8_t>(
                        shift_right(field_mask,
                            static_cast<unsigned>(lane * 8)).low & 0xffU)
                    : static_cast<std::uint8_t>(0xffU);
                if (!f80_padding &&
                    (pointer.mutable_buffer->assigned[*index + offset] & required) !=
                        required) {
                    fail(location, pointer.bit_field
                        ? "read of unassigned buffer bit"
                        : "read of unassigned buffer byte");
                    return std::nullopt;
                }
                const auto tag = pointer.mutable_buffer->effective_type[*index + offset];
                if (required != 0 && !pointer.union_member_view &&
                    (!pointer.bit_field || !meta_bit_field_record_view(pointer)) &&
                    !byte_meta_type(access_type) && tag != 0 &&
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
        if (pointer.bit_field)
            result = extract_meta_bit_field(result, base);
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
        return EvalValue{result, clone_type(base.type->pointee)};
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
        if (expression.text == "&") {
            const Expr* designator = expression.left.get();
            while (designator->kind == Expr::Kind::Parenthesized && designator->left)
                designator = designator->left.get();
            const bool local = (designator->kind == Expr::Kind::Name &&
                lookup_mutable(*designator)) ||
                (designator->kind == Expr::Kind::Binary && designator->left &&
                 meta_pointer_source(*designator->left)) ||
                (designator->kind == Expr::Kind::Unary && designator->text == "*" &&
                 designator->left && meta_pointer_source(*designator->left));
            if (local) {
                auto pointer = meta_designator_pointer(*designator);
                if (pointer && pointer->meta_pointer && pointer->meta_pointer->bit_field) {
                    fail(expression.location, "cannot take the address of a bit-field");
                    return std::nullopt;
                }
                return pointer;
            }
            if (!pointer_resolver_) return std::nullopt;
            auto source = clone_expr(expression);
            source->left = address_designator(*expression.left);
            if (!source->left) return std::nullopt;
            auto value = resolve_pointer(std::move(source), expression_type(expression));
            if (value) value->function_designator = direct_function(expression) != nullptr;
            return value;
        }
        if (expression.text == "!") {
            const auto type = expression_type(*expression.left);
            if (type && type->kind == Type::Kind::Pointer) {
                auto value = this->expression(*expression.left);
                if (!value) return std::nullopt;
                value = convert(*value, builtin_type(BuiltinType::Bool), expression.location);
                return value ? std::optional<EvalValue>(EvalValue{UInt128{!value->truthy()}, value->type}) : std::nullopt;
            }
        }
        if (expression.text == "++" || expression.text == "--" ||
            expression.text == "post++" || expression.text == "post--") {
            Expr assignment;
            assignment.kind = Expr::Kind::Assign;
            assignment.location = expression.location;
            assignment.text = expression.text.ends_with("++") ? "+=" : "-=";
            assignment.left = clone_expr(*expression.left);
            assignment.right = std::make_unique<Expr>();
            assignment.right->kind = Expr::Kind::Integer;
            assignment.right->location = expression.location;
            assignment.right->text = "1i32";
            std::optional<EvalValue> previous;
            auto value = assign(assignment, &previous);
            return value && expression.text.starts_with("post") ? previous : value;
        }
        auto value = this->expression(*expression.left);
        if (value && value->object && is_vector(value->type)) {
            const auto result_type = expression_type(expression);
            auto result = result_type ? new_object(result_type, expression.location) : std::nullopt;
            if (!result) return std::nullopt;
            for (std::size_t lane = 0; lane < value->type->lanes; ++lane) {
                if (!step(expression.location)) return std::nullopt;
                auto scalar = read_meta_pointer(vector_lane_pointer(*value, lane), expression.location);
                if (!scalar) return std::nullopt;
                std::optional<EvalValue> updated;
                if (expression.text == "!") updated = EvalValue{scalar->truthy() ? UInt128{}
                    : mask_to(bit_not(UInt128{}), integer_type(result_type->element).bits), result_type->element};
                else if (expression.text == "-") updated = scalar->floating
                    ? std::optional<EvalValue>(EvalValue{floating::negate(*scalar->floating), result_type->element})
                    : scalar_binary_values("-", EvalValue{UInt128{}, builtin_type(BuiltinType::I32)},
                                           *scalar, expression.location);
                else if (expression.text == "+") updated = convert(*scalar, result_type->element, expression.location);
                else if (expression.text == "~" && is_integer(scalar->type)) {
                    scalar = convert(*scalar, result_type->element, expression.location);
                    if (scalar) updated = EvalValue{mask_to(bit_not(scalar->integer),
                        integer_type(result_type->element).bits), result_type->element};
                }
                if (!updated || !store_meta_pointer(vector_lane_pointer(*result, lane), updated,
                                                     expression.location)) return std::nullopt;
            }
            return result;
        }
        if (!value || value->pointer() || value->tokens || value->bytes ||
            value->buffer || value->object) return std::nullopt;
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
        if (expression.text == "pointer_member" ||
            expression.text == "member") {
            auto pointer = meta_member_pointer(expression);
            return pointer ? read_meta_pointer(*pointer, expression.location)
                           : std::nullopt;
        }
        if (expression.text == "index") {
            const auto owner = expression_type(*expression.left);
            if (owner && owner->kind == Type::Kind::Vector) {
                auto pointer = meta_vector_lane_pointer(expression);
                return pointer ? read_meta_pointer(*pointer, expression.location)
                               : std::nullopt;
            }
        }
        auto left = this->expression(*expression.left);
        if (!left) return std::nullopt;
        if (is_vector(left->type) || is_vector(expression_type(*expression.right))) {
            auto right = this->expression(*expression.right);
            return right ? vector_binary_values(expression.text, *left, *right, expression.location)
                         : std::nullopt;
        }
        if (left->tokens || left->bytes || left->buffer || left->object) {
            fail(expression.location, "meta values do not support scalar operators");
            return std::nullopt;
        }
        const auto operation = expression.text;
        if (operation == "&&" || operation == "||") {
            if (!known_truth(*left, expression.location)) return std::nullopt;
            const bool lhs = left->truthy();
            if ((operation == "&&" && !lhs) ||
                (operation == "||" && lhs)) {
                return EvalValue{{operation == "||", 0},
                                 builtin_type(BuiltinType::Bool)};
            }
            auto right = this->expression(*expression.right);
            if (!right || !known_truth(*right, expression.location)) return std::nullopt;
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
        if ((operation == "==" || operation == "!=") && (left->pointer() || right->pointer())) {
            const auto null = [](const EvalValue& value) {
                return value.address ? value.address->kind == AddressConstant::Kind::Absolute &&
                    value.address->absolute == UInt128{} : !value.pointer() && is_integer(value.type) &&
                    value.integer == UInt128{};
            };
            if (null(*left) || null(*right)) {
                const auto& pointer = null(*left) ? *right : *left;
                if (pointer.meta_pointer && !live_meta_pointer(pointer, expression.location))
                    return std::nullopt;
                if (!pointer.address || pointer.address->kind == AddressConstant::Kind::Absolute) {
                    const bool equal = null(*left) && null(*right);
                    return EvalValue{UInt128{operation == "==" ? equal : !equal}, builtin_type(BuiltinType::Bool)};
                }
            }
            if (left->address && right->address && left->address->kind == AddressConstant::Kind::Absolute &&
                right->address->kind == AddressConstant::Kind::Absolute) {
                const bool equal = left->address->absolute == right->address->absolute;
                return EvalValue{UInt128{operation == "==" ? equal : !equal}, builtin_type(BuiltinType::Bool)};
            }
        }
        if (right->tokens || right->bytes || right->buffer || right->object) {
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
        return scalar_binary_values(operation, *left, *right, expression.location);
    }

    std::optional<EvalValue> scalar_binary_values(std::string_view operation,
        const EvalValue& left, const EvalValue& right, SourceLocation location) {
        if (left.pointer() || right.pointer() ||
            (!is_integer(left.type) && !is_floating(left.type)) ||
            (!is_integer(right.type) && !is_floating(right.type))) {
            fail(location, "translation-time scalar operator requires numeric operands");
            return std::nullopt;
        }
        if (left.floating || right.floating)
            return calculate_floating(operation, left, right, location);
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
            if (operation == token) return calculate(opcode, left, right, location);
        return std::nullopt;
    }

    std::optional<EvalValue> store_meta_pointer(std::optional<EvalValue> pointer,
                                                std::optional<EvalValue> source,
                                                SourceLocation location) {
        if (!pointer || !source || !sized_meta_pointer(*pointer, location) ||
            !pointer->meta_pointer->mutable_buffer || pointer->type->pointee->is_const) {
            fail(location, "meta pointer write requires mutable supported storage");
            return std::nullopt;
        }
        const auto& target = *pointer->meta_pointer;
        const auto offset = meta_access_index(*pointer, location, true);
        if (!offset) return std::nullopt;
        const auto extent = *meta_object_size(pointer->type->pointee);
        const auto erase_pointer_slots = [&] {
            std::erase_if(target.mutable_buffer->pointers, [&](const EvalBuffer::PointerObject& slot) {
                if (*offset >= slot.offset + slot.length || slot.offset >= *offset + extent) return false;
                // Partial writes cannot manufacture the unobservable remainder.
                if (*offset > slot.offset || extent < slot.length)
                    std::fill_n(target.mutable_buffer->assigned.begin() +
                        static_cast<std::ptrdiff_t>(slot.offset), slot.length, 0);
                return true;
            });
        };
        if (pointer->type->pointee->kind == Type::Kind::Pointer) {
            if (!meta_record_effective_access(*pointer, location, true)) return std::nullopt;
            source = convert(*source, pointer->type->pointee, location);
            if (!source) return std::nullopt;
            for (std::size_t byte = 0; byte < extent; ++byte) {
                const auto tag = target.mutable_buffer->effective_type[*offset + byte];
                if (!target.union_member_view && tag != 0 && tag != 0xffU) {
                    fail(location, "meta pointer write violates effective type");
                    return std::nullopt;
                }
            }
            if (!register_meta_record(*target.mutable_buffer, *offset, pointer->type->pointee, location))
                return std::nullopt;
            erase_pointer_slots();
            const bool opaque = source->meta_pointer || source->string ||
                (source->address && source->address->kind != AddressConstant::Kind::Absolute);
            const auto bits = source->address && !opaque ? source->address->absolute : UInt128{};
            for (std::size_t byte = 0; byte < extent; ++byte) {
                const auto lane = program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                    ? byte : extent - 1 - byte;
                target.mutable_buffer->data[*offset + byte] = static_cast<char>(
                    shift_right(bits, static_cast<unsigned>(lane * 8)).low & 0xffU);
                target.mutable_buffer->assigned[*offset + byte] = 0xffU;
                target.mutable_buffer->effective_type[*offset + byte] = 0xffU;
            }
            if (opaque) {
                if (!charge_meta_bytes(64 + type_name(source->type).size(), location)) return std::nullopt;
                auto saved = std::make_shared<EvalValue>(*source);
                EvalBuffer::PointerObject slot{*offset, extent, saved, {}, false};
                if (saved->meta_pointer && saved->meta_pointer->mutable_buffer) {
                    slot.backing = saved->meta_pointer->mutable_buffer;
                    slot.has_backing = true;
                    saved->meta_pointer->mutable_buffer.reset();
                }
                target.mutable_buffer->pointers.push_back(std::move(slot));
            }
            return source;
        }
        if (pointer->type->pointee->kind == Type::Kind::Record ||
            pointer->type->pointee->kind == Type::Kind::Vector) {
            if (pointer->type->pointee->kind == Type::Kind::Record &&
                !meta_record_effective_access(*pointer, location, true))
                return std::nullopt;
            source = convert(*source, pointer->type->pointee,
                             location);
            if (!source || !source->object) return std::nullopt;
            const auto size = *meta_object_size(pointer->type->pointee);
            for (std::size_t byte = 0; byte < size; ++byte) {
                const auto incoming = source->object->effective_type[byte];
                const auto previous = target.mutable_buffer->effective_type[*offset + byte];
                if (!target.union_member_view && incoming != 0 && previous != 0 &&
                    incoming != previous &&
                    (incoming == 0xffU || previous == 0xffU ||
                     !compatible_meta_type(
                         static_cast<BuiltinType>(previous - 1),
                         static_cast<BuiltinType>(incoming - 1)))) {
                    fail(location,
                         "meta pointer write violates effective type");
                    return std::nullopt;
                }
            }
            erase_pointer_slots();
            for (const auto& slot : source->object->pointers) {
                if (!charge_meta_bytes(64 + type_name(slot.value->type).size(), location)) return std::nullopt;
                auto copy = slot;
                copy.offset += *offset;
                target.mutable_buffer->pointers.push_back(std::move(copy));
            }
            for (std::size_t byte = 0; byte < size; ++byte) {
                target.mutable_buffer->data[*offset + byte] = source->object->data[byte];
                target.mutable_buffer->assigned[*offset + byte] = source->object->assigned[byte];
                target.mutable_buffer->effective_type[*offset + byte] =
                    source->object->effective_type[byte];
            }
            if (!register_meta_record(*target.mutable_buffer, *offset,
                    pointer->type->pointee, location))
                return std::nullopt;
            return source;
        }
        if (!scalar_meta_pointer(*pointer, location)) return std::nullopt;
        const auto size = meta_scalar_size(*pointer);
        const auto access_type = pointer->type->pointee->builtin;
        if (!byte_meta_type(access_type) && !target.bit_field &&
            !meta_record_effective_access(*pointer, location, true)) return std::nullopt;
        if (!byte_meta_type(access_type) && !target.union_member_view &&
            (!target.bit_field || !meta_bit_field_record_view(target))) {
            for (std::size_t index = 0; index < size; ++index) {
                if (target.bit_field) {
                    const auto lane = program_.evaluation_layout.byte_order ==
                        EvaluationByteOrder::Little ? index : size - 1 - index;
                    if ((shift_right(meta_bit_field_mask(*target.bit_field),
                            static_cast<unsigned>(lane * 8)).low & 0xffU) == 0)
                        continue;
                }
                const auto tag = target.mutable_buffer->effective_type[*offset + index];
                if (tag != 0 &&
                    !compatible_meta_type(static_cast<BuiltinType>(tag - 1),
                                          access_type)) {
                    fail(location,
                        "meta pointer write violates effective type");
                    return std::nullopt;
                }
            }
        }
        source = convert(*source, pointer->type->pointee,
                         location);
        if (!source) return std::nullopt;
        if (!register_meta_record(*target.mutable_buffer, *offset, pointer->type->pointee, location)) return std::nullopt;
        erase_pointer_slots();
        const auto bits = source->floating ? source->floating->bits
                                           : source->integer;
        if (target.bit_field) {
            if (target.bit_field->owner && !target.union_member_view &&
                !meta_bit_field_record_view(target)) {
                const auto owner_size = meta_object_size(target.bit_field->owner);
                if (!owner_size) return std::nullopt;
                for (std::size_t byte = 0; byte < *owner_size; ++byte) {
                    const auto tag = target.mutable_buffer->effective_type[
                        target.bit_field->owner_offset + byte];
                    if (tag != 0 && !meta_object_accepts_tag_at(
                            target.bit_field->owner, byte, tag)) {
                        fail(location,
                             "meta pointer write violates aggregate effective type");
                        return std::nullopt;
                    }
                }
            }
            if (target.bit_field->owner &&
                !register_meta_record(*target.mutable_buffer,
                    target.bit_field->owner_offset, target.bit_field->owner,
                    location)) return std::nullopt;
            const auto field_mask = meta_bit_field_mask(*target.bit_field);
            const auto inserted = shift_left(
                mask_to(bits, target.bit_field->width),
                target.bit_field->offset);
            UInt128 previous;
            for (std::size_t index = 0; index < size; ++index) {
                const auto lane = program_.evaluation_layout.byte_order ==
                    EvaluationByteOrder::Little ? index : size - 1 - index;
                previous = bit_or(previous, shift_left(
                    UInt128{static_cast<unsigned char>(
                        target.mutable_buffer->data[*offset + index])},
                    static_cast<unsigned>(lane * 8)));
            }
            const auto updated = bit_or(bit_and(previous,
                bit_not(field_mask)), inserted);
            for (std::size_t index = 0; index < size; ++index) {
                const auto lane = program_.evaluation_layout.byte_order ==
                    EvaluationByteOrder::Little ? index : size - 1 - index;
                const auto shift = static_cast<unsigned>(lane * 8);
                const auto mask_byte = static_cast<std::uint8_t>(
                    shift_right(field_mask, shift).low & 0xffU);
                if (mask_byte == 0) continue;
                target.mutable_buffer->data[*offset + index] =
                    static_cast<char>(shift_right(updated, shift).low & 0xffU);
                target.mutable_buffer->assigned[*offset + index] |= mask_byte;
                target.mutable_buffer->effective_type[*offset + index] =
                    static_cast<std::uint8_t>(access_type) + 1;
            }
            return EvalValue{extract_meta_bit_field(inserted, *pointer),
                             clone_type(pointer->type->pointee)};
        }
        for (std::size_t index = 0; index < size; ++index) {
            const auto lane = program_.evaluation_layout.byte_order ==
                EvaluationByteOrder::Little ? index : size - 1 - index;
            target.mutable_buffer->data[*offset + index] = static_cast<char>(
                shift_right(bits,
                    static_cast<unsigned>(lane * 8)).low & 0xffU);
            target.mutable_buffer->assigned[*offset + index] = 0xffU;
            if (!byte_meta_type(access_type))
                target.mutable_buffer->effective_type[*offset + index] =
                    static_cast<std::uint8_t>(access_type) + 1;
        }
        return source;

    }

    std::optional<EvalValue> assign(const Expr& expression,
                                  std::optional<EvalValue>* previous_value = nullptr) {
        if (!expression.left || !expression.right) {
            return std::nullopt;
        }
        const Expr* designator = expression.left.get();
        while (designator && designator->kind == Expr::Kind::Parenthesized)
            designator = designator->left.get();
        if (designator &&
            ((designator->kind == Expr::Kind::Name &&
              lookup_mutable(*designator) &&
              (lookup_mutable(*designator)->storage ||
               lookup_mutable(*designator)->value.object)) ||
             (designator->kind == Expr::Kind::Binary &&
              (designator->text == "index" ||
               designator->text == "member" ||
               designator->text == "pointer_member")) ||
             (designator->kind == Expr::Kind::Unary && designator->text == "*"))) {
            auto pointer = meta_designator_pointer(*designator);
            if (!pointer || !pointer->meta_pointer) {
                fail(designator->location,
                    "translation-time assignment requires a meta data pointer");
                return std::nullopt;
            }
            if (!sized_meta_pointer(*pointer, designator->location) ||
                pointer->type->pointee->is_const ||
                !pointer->meta_pointer->mutable_buffer) {
                fail(designator->location,
                    "meta pointer write requires mutable supported storage");
                return std::nullopt;
            }
            const auto offset = meta_access_index(*pointer, designator->location, true);
            if (!offset) return std::nullopt;
            std::optional<EvalValue> prior_value;
            if (expression.text != "=") {
                if (pointer->type->pointee->kind != Type::Kind::Pointer &&
                    !is_vector(pointer->type->pointee) &&
                    !scalar_meta_pointer(*pointer, designator->location)) {
                    fail(designator->location,
                         "meta aggregate compound assignment requires an implemented value operator");
                    return std::nullopt;
                }
                prior_value = read_meta_pointer(*pointer, designator->location);
                if (!prior_value) return std::nullopt;
                if (previous_value) *previous_value = prior_value;
            }
            auto source = this->expression(*expression.right);
            if (!source) return std::nullopt;
            if (prior_value) {
                if (prior_value->meta_pointer && expression.text != "+=" && expression.text != "-=") {
                    fail(expression.location, "pointer compound assignment requires += or -=");
                    return std::nullopt;
                }
                source = prior_value->meta_pointer ? meta_pointer_offset(*prior_value, *source,
                    expression.text == "-=", expression.location)
                    : is_vector(prior_value->type) ? vector_binary_values(
                    expression.text.substr(0, expression.text.size() - 1),
                    *prior_value, *source, expression.location) : scalar_binary_values(
                    expression.text.substr(0, expression.text.size() - 1),
                    *prior_value, *source, expression.location);
                if (!source) return std::nullopt;
            }
            return store_meta_pointer(pointer, source, designator->location);
        }
        if (!designator || designator->kind != Expr::Kind::Name)
            return std::nullopt;
        auto* destination = lookup_mutable(*designator);
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
            *lookup_mutable(*designator) = {*source, true, false};
            return source;
        }
        if (previous_value) {
            *previous_value = lookup(*designator);
            if (!*previous_value) return std::nullopt;
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
        *lookup_mutable(*designator) = {*result, true, false};
        return result;
    }

    std::optional<EvalValue> call_expression(const Expr& expression) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            return std::nullopt;
        }
        if (lookup_mutable(*expression.left)) {
            fail(expression.location, "indirect calls are not permitted during translation-time evaluation");
            return std::nullopt;
        }
        if (expression.left->text == "$::syntax::error" ||
            expression.left->text == "$::syntax::warning" ||
            expression.left->text == "$::syntax::note") {
            const auto& name = expression.left->text;
            if (!procedural_ || expression.arguments.size() != 2) {
                fail(expression.location, "syntax diagnostic requires a span and string message");
                return std::nullopt;
            }
            auto span = this->expression(*expression.arguments[0]);
            if (!span) return std::nullopt;
            auto message = this->expression(*expression.arguments[1]);
            if (!span->syntax_span || !message || !message->string ||
                message->offset >= message->string->size()) {
                fail(expression.location, "syntax diagnostic requires a span and string message");
                return std::nullopt;
            }
            const auto text = std::string_view(*message->string).substr(message->offset,
                message->string->size() - message->offset - 1);
            if (!charge_meta_bytes(32 + text.size(), expression.location)) return std::nullopt;
            if (name == "$::syntax::error") {
                fail(span->syntax_span->first, std::string(text));
                return std::nullopt;
            }
            diagnostics_.report(name == "$::syntax::warning" ? DiagnosticLevel::Warning
                : DiagnosticLevel::Note, span->syntax_span->first, text);
            for (auto frame = call_stack_.rbegin(); frame != call_stack_.rend(); ++frame)
                diagnostics_.note(frame->location, "while evaluating call to '" + frame->function + "'");
            diagnostics_.note(macro_context_->definition, "expansion function is defined here");
            diagnostics_.note(macro_context_->invocation, "while executing this expansion");
            return EvalValue{UInt128{}, builtin_type(BuiltinType::Void)};
        }
        if (expression.left->text.starts_with("$::syntax::")) {
            const auto& name = expression.left->text;
            const bool context = name == "$::syntax::context";
            const auto count = name == "$::syntax::input" || name == "$::syntax::span" || context ? 1U
                : name == "$::syntax::capture" || name == "$::syntax::node" ||
                  name == "$::syntax::count" ||
                  name == "$::syntax::is_variant" || name == "$::syntax::capture_span" ? 2U
                : name == "$::syntax::at" ? 3U : 0U;
            if (!procedural_ || count == 0 || expression.arguments.size() != count) {
                fail(expression.location, "unsupported syntax operation or invalid argument count: " + name);
                return std::nullopt;
            }
            auto match = this->expression(*expression.arguments[0]);
            if (context) {
                if (match && match->syntax_match)
                    return context_value(match->syntax_match->context, expression.location);
                if (match && match->syntax_node)
                    return context_value(match->syntax_node->context, expression.location);
                fail(expression.location, "$::syntax::context requires a syntax match or node");
                return std::nullopt;
            }
            if (!match || !match->syntax_match) {
                fail(expression.location, "syntax operation requires a syntax match");
                return std::nullopt;
            }
            if (count == 1) {
                if (name == "$::syntax::span") return span_value(match->syntax_match->span, expression.location);
                TokenSequence output;
                if (!append_tokens(output, match->syntax_match->input, expression.location)) return std::nullopt;
                return token_value(std::move(output));
            }
            auto field = this->expression(*expression.arguments[1]);
            if (!field || !field->string) {
                fail(expression.location, "syntax field must be a translation-time string");
                return std::nullopt;
            }
            if (field->offset >= field->string->size()) {
                fail(expression.location, "syntax field string pointer is out of bounds");
                return std::nullopt;
            }
            const auto written = std::string_view(*field->string).substr(field->offset,
                field->string->size() - field->offset - 1);
            if (name == "$::syntax::is_variant") {
                if (!match->syntax_match->variant ||
                    std::find(match->syntax_match->variant_labels.begin(),
                              match->syntax_match->variant_labels.end(), written) ==
                        match->syntax_match->variant_labels.end()) {
                    fail(expression.location, "syntax choice has no variant named '" + std::string(written) + "'");
                    return std::nullopt;
                }
                return EvalValue{UInt128{*match->syntax_match->variant == written ? 1U : 0U},
                                 builtin_type(BuiltinType::Bool)};
            }
            const auto found = std::find_if(match->syntax_match->fields.begin(), match->syntax_match->fields.end(),
                [&](const SyntaxMatchValue::Field& candidate) { return candidate.name == written; });
            if (found == match->syntax_match->fields.end()) {
                fail(expression.location, "syntax match has no field named '" + std::string(written) + "'");
                return std::nullopt;
            }
            if (name == "$::syntax::capture_span") return span_value(found->span, expression.location);
            if (name == "$::syntax::capture") {
                if (found->kind != SyntaxMatchValue::Field::Kind::Primitive &&
                    found->kind != SyntaxMatchValue::Field::Kind::RawGroup) {
                    fail(expression.location, "syntax capture requires a primitive token field");
                    return std::nullopt;
                }
                TokenSequence output;
                if (!append_tokens(output, found->tokens, expression.location)) return std::nullopt;
                return token_value(std::move(output));
            }
            if (name == "$::syntax::node") {
                if (!found->node) {
                    fail(expression.location, "syntax node requires a parsed or raw-group capture field");
                    return std::nullopt;
                }
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = found->node;
                return value;
            }
            if (found->kind != SyntaxMatchValue::Field::Kind::Nested) {
                fail(expression.location, "syntax count/at requires a nested record field");
                return std::nullopt;
            }
            if (name == "$::syntax::count") return EvalValue{UInt128{found->records.size()}, builtin_type(BuiltinType::Uptr)};
            auto index = this->expression(*expression.arguments[2]);
            if (!index || !is_integer(index->type) || index->integer.high != 0 ||
                index->integer.low >= found->records.size()) {
                fail(expression.location, "syntax record index is out of range");
                return std::nullopt;
            }
            EvalValue value{UInt128{}, syntax_match_type()};
            value.syntax_match = found->records[static_cast<std::size_t>(index->integer.low)];
            return value;
        }
        if (procedural_ && (expression.left->text == "$::meta::tokens" ||
            expression.left->text == "$::meta::node_span" ||
            expression.left->text == "$::meta::child_count" ||
            expression.left->text == "$::meta::child" ||
            expression.left->text == "$::meta::is_kind" ||
            expression.left->text == "$::meta::is_production" ||
            expression.left->text == "$::meta::is_extension" ||
            expression.left->text == "$::meta::replace_child" ||
            expression.left->text == "$::meta::extension_match")) {
            const auto& name = expression.left->text;
            const auto count = name == "$::meta::tokens" || name == "$::meta::node_span" ||
                name == "$::meta::child_count" || name == "$::meta::extension_match" ? 1U :
                name == "$::meta::replace_child" ? 3U : 2U;
            if (expression.arguments.size() != count) {
                fail(expression.location, name + " requires " + std::to_string(count) + " arguments");
                return std::nullopt;
            }
            auto source = this->expression(*expression.arguments[0]);
            if (!source || !source->syntax_node) {
                fail(expression.location, name + " requires a syntax node");
                return std::nullopt;
            }
            const auto& node = *source->syntax_node;
            if (name == "$::meta::extension_match") {
                if (node.kind != SyntaxNode::Kind::Extension || !node.definition || !node.match) {
                    fail(expression.location, "$::meta::extension_match requires an extension node");
                    return std::nullopt;
                }
                EvalValue value{UInt128{}, syntax_match_type()};
                value.syntax_match = node.match;
                return value;
            }
            if (name == "$::meta::node_span") return span_value(node.span, expression.location);
            if (name == "$::meta::tokens") {
                TokenSequence result;
                const auto flattened = syntax_node_tokens(node);
                if (!append_tokens(result, flattened, expression.location)) return std::nullopt;
                return token_value(std::move(result));
            }
            if (name == "$::meta::child_count")
                return EvalValue{UInt128{node.children.size()}, builtin_type(BuiltinType::Uptr)};
            auto argument = this->expression(*expression.arguments[1]);
            if (!argument) return std::nullopt;
            if (name == "$::meta::replace_child") {
                if (!is_integer(argument->type) || argument->integer.high != 0 ||
                    argument->integer.low > std::numeric_limits<std::size_t>::max()) {
                    fail(expression.arguments[1]->location,
                         "$::meta::replace_child requires an integer child index");
                    return std::nullopt;
                }
                auto replacement = this->expression(*expression.arguments[2]);
                if (!replacement || !replacement->syntax_node) {
                    fail(expression.arguments[2]->location,
                         "$::meta::replace_child requires a syntax replacement node");
                    return std::nullopt;
                }
                std::string error;
                auto limits = program_.evaluation_limits;
                limits.steps -= std::min(steps_, limits.steps);
                std::uint64_t validation_work{};
                auto replaced = syntax_replace_child(node,
                    static_cast<std::size_t>(argument->integer.low),
                    replacement->syntax_node, error, limits, &validation_work);
                steps_ += validation_work;
                if (!replaced) {
                    fail(expression.location, std::move(error));
                    return std::nullopt;
                }
                const auto memory_remaining = program_.evaluation_limits.memory -
                    std::min<std::uint64_t>(program_.evaluation_limits.memory, meta_bytes_ + token_bytes_);
                const auto storage = syntax_node_storage(*replaced,
                    std::min(program_.evaluation_limits.bytes, memory_remaining));
                if (storage > program_.evaluation_limits.bytes ||
                    storage > std::numeric_limits<std::size_t>::max() ||
                    !charge_meta_bytes(static_cast<std::size_t>(storage), expression.location)) {
                    if (storage > program_.evaluation_limits.bytes ||
                        storage > std::numeric_limits<std::size_t>::max())
                        fail(expression.location,
                             "public syntax replacement exceeds translation-time storage budget");
                    return std::nullopt;
                }
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = std::move(replaced);
                return value;
            }
            if (name == "$::meta::child") {
                if (!is_integer(argument->type) || argument->integer.high != 0 ||
                    argument->integer.low >= node.children.size()) {
                    fail(expression.location, "syntax child index is out of range");
                    return std::nullopt;
                }
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = node.children[static_cast<std::size_t>(argument->integer.low)];
                return value;
            }
            if (!argument->string || argument->offset >= argument->string->size()) {
                fail(expression.location, name + " requires a translation-time string");
                return std::nullopt;
            }
            const auto written = std::string_view(*argument->string).substr(argument->offset,
                argument->string->size() - argument->offset - 1);
            if (name == "$::meta::is_extension") {
                const auto resolved = syntax_resolve_entity(node, written, diagnostics_,
                    expression.arguments[1]->location);
                if (!resolved) return std::nullopt;
                return EvalValue{UInt128{node.kind == SyntaxNode::Kind::Extension &&
                    node.definition && *node.definition == *resolved},
                    builtin_type(BuiltinType::Bool)};
            }
            if (name == "$::meta::is_production") {
                bool known = false;
                for (std::size_t at = 1; at < static_cast<std::size_t>(SyntaxProduction::Count); ++at)
                    if (syntax_production_name(static_cast<SyntaxProduction>(at)) == written) {
                        known = true;
                        break;
                    }
                if (!known) {
                    fail(expression.location,
                        "unknown public syntax production '" + std::string(written) + "'");
                    return std::nullopt;
                }
                return EvalValue{UInt128{node.kind == SyntaxNode::Kind::Core &&
                    syntax_production_name(node.production) == written}, builtin_type(BuiltinType::Bool)};
            }
            static constexpr std::pair<std::string_view, SyntaxNode::Kind> kinds[] = {
                {"token", SyntaxNode::Kind::Token}, {"group", SyntaxNode::Kind::Group},
                {"core", SyntaxNode::Kind::Core}, {"extension", SyntaxNode::Kind::Extension},
                {"macro", SyntaxNode::Kind::Macro}, {"deferred", SyntaxNode::Kind::Deferred}};
            const auto found = std::find_if(std::begin(kinds), std::end(kinds),
                [&](const auto& kind) { return kind.first == written; });
            if (found == std::end(kinds)) {
                fail(expression.location, "unknown public syntax node kind '" + std::string(written) + "'");
                return std::nullopt;
            }
            return EvalValue{UInt128{node.kind == found->second}, builtin_type(BuiltinType::Bool)};
        }
        if (procedural_ && expression.left->text == "$::meta::call_site") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::call_site requires one token value");
                return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = this->expression(argument);
            if (!value || !value->tokens || value->tokens->size() != 1U ||
                value->tokens->front().kind != TokenKind::Identifier) {
                fail(argument.location, "$::meta::call_site requires exactly one identifier token");
                return std::nullopt;
            }
            if (!call_context_) {
                fail(expression.location, "$::meta::call_site has no invocation context");
                return std::nullopt;
            }
            if (!charge_input_context(call_context_, expression.location)) return std::nullopt;
            auto token = value->tokens->front();
            token.origin.context = call_context_;
            token.origin.value_binding = {};
            token.origin.tag_binding.reset();
            TokenSequence result;
            if (!append_tokens(result, {token}, expression.location)) return std::nullopt;
            return token_value(std::move(result));
        }
        if (procedural_ && expression.left->text == "$::meta::gensym") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::gensym requires one string prefix");
                return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = this->expression(argument);
            if (!value || !value->string || value->offset >= value->string->size()) {
                fail(argument.location, "$::meta::gensym requires a translation-time string prefix");
                return std::nullopt;
            }
            const auto prefix = std::string_view(*value->string).substr(value->offset,
                value->string->size() - value->offset - 1);
            const auto initial = [](char ch) {
                return (ch >= 'A' && ch <= 'Z') ||
                       (ch >= 'a' && ch <= 'z') || ch == '_';
            };
            const auto continuation = [&](char ch) {
                return initial(ch) || (ch >= '0' && ch <= '9');
            };
            if (prefix.empty() || !initial(prefix.front()) ||
                !std::all_of(prefix.begin() + 1, prefix.end(), continuation) ||
                is_reserved_identifier(prefix)) {
                fail(argument.location, "$::meta::gensym prefix must be a nonreserved identifier");
                return std::nullopt;
            }
            const auto source = macro_context_->invocation;
            auto fresh = std::make_shared<FreshIdentifier>();
            fresh->expansion = macro_context_->expansion;
            fresh->ordinal = ++fresh_ordinal_;
            fresh->prefix = std::string(prefix);
            fresh->source_unit = source.file ? source.file->source_unit_at(source.line) : std::string{};
            if (!charge_meta_bytes(64 + fresh->prefix.size() + fresh->source_unit.size(),
                                   expression.location)) return std::nullopt;
            MetaToken token;
            token.kind = TokenKind::Identifier;
            token.text = std::string(prefix);
            token.origin = {token_origin(source).span, {}, macro_context_, {}, 0, {}, fresh};
            TokenSequence result;
            if (!append_tokens(result, {token}, expression.location)) return std::nullopt;
            return token_value(std::move(result));
        }
        if (procedural_ && expression.left->text == "$::meta::parse") {
            if (expression.arguments.size() == 3U) {
                auto category = this->expression(*expression.arguments[0]);
                auto input = this->expression(*expression.arguments[1]);
                auto context = this->expression(*expression.arguments[2]);
                if (!category || !category->string || category->offset >= category->string->size() ||
                    !input || !input->tokens || !context || !context->syntax_context) {
                    fail(expression.location, "$::meta::parse requires a category string, tokens, and context");
                    return std::nullopt;
                }
                const auto written = std::string_view(*category->string).substr(category->offset,
                    category->string->size() - category->offset - 1);
                const auto selected = syntax_parse_category(written);
                if (!selected) {
                    fail(expression.arguments[0]->location, "invalid public syntax parse category: '" + std::string(written) + "'");
                    return std::nullopt;
                }
                if (!syntax_parse_) {
                    fail(expression.location, "public syntax parsing is unavailable in this translation context");
                    return std::nullopt;
                }
                auto node = syntax_parse_(*selected, *input->tokens, context->syntax_context, expression.location);
                if (!node) {
                    fail(expression.location, "$::meta::parse could not recognize complete bounded input for '" + std::string(written) + "'");
                    return std::nullopt;
                }
                const auto storage = syntax_node_storage(*node, program_.evaluation_limits.memory);
                if (storage > program_.evaluation_limits.bytes) {
                    fail(expression.location, "public syntax parse output byte budget exceeded");
                    return std::nullopt;
                }
                if (!charge_meta_bytes(static_cast<std::size_t>(storage), expression.location)) return std::nullopt;
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = std::move(node);
                return value;
            }
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
            for (const auto& slot : handle->buffer->pointers) {
                if (slot.offset < count_bytes) {
                    fail(expression.location, "opaque translation-time pointer representation cannot be frozen as bytes");
                    return std::nullopt;
                }
            }
            if (std::find_if(handle->buffer->assigned.begin(),
                             handle->buffer->assigned.begin() +
                                 static_cast<std::ptrdiff_t>(count_bytes),
                             [](std::uint8_t bits) { return bits != 0xffU; }) !=
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
        case Statement::Kind::DeclarationList:
            for (const auto& child : statement.statements) {
                auto flow = this->statement(*child);
                if (flow.kind != Flow::Normal) return flow;
            }
            return {};
        case Statement::Kind::Compound: {
            scopes_.emplace_back();
            for (const auto& child : statement.statements) {
                auto flow = this->statement(*child);
                if (flow.kind != Flow::Normal) {
                    pop_scope();
                    return flow;
                }
            }
            pop_scope();
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
            EvalValue value{UInt128{}, clone_type(statement.declaration->type)};
            if (statement.declaration->dynamic_array_bound) {
                auto count = expression(*statement.declaration->dynamic_array_bound);
                if (!count || !is_integer(count->type) ||
                    integer_negative(count->integer, integer_type(count->type)) ||
                    count->integer == UInt128{} || count->integer.high != 0 ||
                    count->integer.low > std::numeric_limits<std::uint32_t>::max() ||
                    !fits_unsigned(count->integer, program_.address_bits)) {
                    fail(statement.location, "translation-time array bound must be a positive target-sized integer");
                    return {Flow::Failed};
                }
                value.type->lanes = static_cast<std::uint32_t>(count->integer.low);
            }
            if ((value.type->kind == Type::Kind::Vector && !value.type->scalable) ||
                value.type->kind == Type::Kind::Record || value.type->kind == Type::Kind::Array) {
                auto object = new_object(value.type, statement.location);
                if (!object) return {Flow::Failed};
                value = std::move(*object);
            }
            scopes_.back()[name_key(*statement.declaration)] = {
                value, value.object != nullptr, statement.declaration->type->is_const};
            if (statement.declaration->initializer) {
                const auto& source = *statement.declaration->initializer;
                const bool aggregate = source.kind == Expr::Kind::AggregateInitializer ||
                    (source.kind == Expr::Kind::String && value.type->kind == Type::Kind::Array);
                auto initializer = aggregate ? initialize_object(source, value.type) : expression(source);
                if (!initializer) return {Flow::Failed};
                if (!aggregate) initializer = convert(*initializer, statement.declaration->type, statement.location);
                if (!initializer) return {Flow::Failed};
                auto& cell = scopes_.back()[name_key(*statement.declaration)];
                if (cell.value.object) {
                    // Initialization preserves an address already formed for this cell.
                    *cell.value.object = *initializer->object;
                } else if (cell.storage) {
                    EvalValue storage{UInt128{}, cell.value.type};
                    storage.object = cell.storage;
                    auto pointer = object_pointer(storage);
                    pointer.type->pointee->is_const = false;
                    if (!store_meta_pointer(pointer, initializer, statement.location)) return {Flow::Failed};
                } else cell.value = *initializer;
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
            if (!condition || !known_truth(*condition, statement.location)) return {Flow::Failed};
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
                    nested |= node->kind != Statement::Kind::Compound &&
                              node->kind != Statement::Kind::DeclarationList;
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
                if (node->kind == Statement::Kind::DeclarationList) {
                    for (const auto& child : node->statements) {
                        auto flow = self(self, child.get());
                        if (flow.kind != Flow::Normal) return flow;
                    }
                    return {};
                }
                if (node->kind == Statement::Kind::Compound) {
                    scopes_.emplace_back();
                    Flow flow;
                    for (const auto& child : node->statements) {
                        flow = self(self, child.get());
                        if (flow.kind != Flow::Normal) break;
                    }
                    pop_scope();
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
                if (!condition || !known_truth(*condition, statement.location)) return {Flow::Failed};
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
                if (!condition || !known_truth(*condition, statement.location)) return {Flow::Failed};
                if (!condition->truthy()) return {};
            } while (true);
        case Statement::Kind::For: {
            scopes_.emplace_back();
            auto initial = this->statement(*statement.first);
            if (initial.kind != Flow::Normal) {
                pop_scope();
                return initial;
            }
            while (true) {
                if (statement.condition) {
                    auto condition = expression(*statement.condition);
                    if (!condition || !known_truth(*condition, statement.location)) {
                        pop_scope();
                        return {Flow::Failed};
                    }
                    if (!condition->truthy()) break;
                }
                auto flow = this->statement(*statement.second);
                if (flow.kind == Flow::Return || flow.kind == Flow::Failed) {
                    pop_scope();
                    return flow;
                }
                if (flow.kind == Flow::Break) break;
                if (statement.increment && !expression(*statement.increment)) {
                    pop_scope();
                    return {Flow::Failed};
                }
            }
            pop_scope();
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
    SyntaxParseCallback syntax_parse_;
    std::shared_ptr<const SyntaxContext> call_context_;
    std::unordered_set<const SyntaxContext*> charged_contexts_;
    std::unordered_set<const SyntaxParseEnvironment*> charged_environments_;
    std::size_t token_bytes_{};
    std::size_t meta_bytes_{};
    std::uint64_t fresh_ordinal_{};
    std::unordered_set<const std::string*> counted_asset_backings_;
    std::vector<NameMap<Cell>> scopes_;
    std::size_t frame_base_{};
    std::uint64_t steps_{};
    unsigned depth_{};
    bool budget_diagnosed_{};
    SourceLocation failure_location_;
    std::optional<std::string> failure_reason_;
    std::vector<CallFrame> call_stack_;
    std::vector<CallFrame> failure_trace_;
};

bool signed_builtin(BuiltinType type) {
    return type == BuiltinType::I8 || type == BuiltinType::I16 ||
           type == BuiltinType::I32 || type == BuiltinType::I64 ||
           type == BuiltinType::I128 || type == BuiltinType::Iptr;
}

bool evaluate_enumerations(Program& program, Diagnostics& diagnostics, std::size_t first,
                           const EnumInitializerPreparation& prepare) {
    std::unordered_set<std::string> names;
    // Nested instantiation prepares its own enums. Visit only this publication
    // batch, in declaration order, so a later generic actual may use an earlier
    // enumerator. No vector-element references survive a preparation callback.
    const auto last = program.enumerations.size();
    for (auto index = first; index < last; ++index) {
        const auto owner = program.enumerations[index].nominal_identity;
        if (owner && owner->generic_owner) continue;
        const auto type = enum_type(program.enumerations[index]);
        std::optional<EvalValue> previous;
        for (std::size_t item = 0; item < program.enumerations[index].enumerators.size(); ++item) {
            if (prepare && !program.enumerations[index].enumerators[item].value) {
                auto initializer = std::move(program.enumerations[index].enumerators[item].initializer);
                prepare(initializer);
                program.enumerations[index].enumerators[item].initializer = std::move(initializer);
                if (diagnostics.errors() != 0) return false;
            }
            auto& enumeration = program.enumerations[index];
            auto& enumerator = enumeration.enumerators[item];
            if (enumerator.binding.kind != ValueBinding::Kind::Enumerator &&
                !names.insert(enumerator.name).second) {
                diagnostics.error(enumerator.location,
                                  "enumerator '" + enumerator.name +
                                      "' is declared more than once");
                continue;
            }
            std::optional<EvalValue> value;
            if (enumerator.value) {
                value = EvalValue{enumerator.value->value, clone_type(type)};
            } else if (enumerator.initializer) {
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
    EnumeratorMaterializer(Program& program, Diagnostics& diagnostics)
        : program_(program), diagnostics_(diagnostics) {}

    void run() {
        for (auto& record : program_.records) {
            if (record.nominal_identity && record.nominal_identity->generic_owner) continue;
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
            for (const auto& attribute : function->attributes)
                if (attribute.name == "variadic")
                    for (const auto& name : attribute.variadic_names)
                        scopes_.back().insert(name_key(name));
            if (function->body) rewrite(*function->body);
        }
        caller_ = nullptr;
        scopes_.clear();
    }

private:
    bool local(const Expr& expression) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (scope->contains(name_key(expression))) return true;
        }
        return false;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name &&
            name_key(*expression).binding.kind == ValueBinding::Kind::Local &&
            !local(*expression)) {
            diagnostics_.error(expression->location,
                "captured local value '" + expression->text +
                "' is not visible at its replacement site");
            return;
        }
        if (expression->kind == Expr::Kind::Name &&
            local(*expression)) {
            auto context = expression->name_context
                ? std::make_shared<NameLookupContext>(*expression->name_context)
                : std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Local;
            expression->name_context = std::move(context);
        }
        if (expression->kind == Expr::Kind::Name &&
            !local(*expression) &&
            !resolve_object(program_, caller_, *expression) &&
            !resolve_function(program_, caller_, *expression,
                              [](const FunctionDecl&) { return true; })) {
            if (const auto found = resolve_enumerator(
                    program_, caller_, current_namespace_, *expression);
                found && found->enumerator->value) {
                expression->kind = Expr::Kind::Integer;
                expression->evaluated_integer = found->enumerator->value;
                expression->type = enum_type(*found->enumeration);
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
        if (statement.kind == Statement::Kind::For && statement.first)
            rewrite(*statement.first);
        if (statement.declaration) {
            scopes_.back().insert(name_key(*statement.declaration));
            rewrite(statement.declaration->dynamic_array_bound);
            rewrite(statement.declaration->initializer);
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
    Diagnostics& diagnostics_;
    FunctionDecl* caller_{};
    std::string current_namespace_;
    std::vector<NameSet> scopes_;
};

void materialize_enumerators(Program& program, Diagnostics& diagnostics) {
    EnumeratorMaterializer(program, diagnostics).run();
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
    if (value.object) {
        replacement->kind = Expr::Kind::ByteSequence;
        replacement->type = clone_type(value.type);
        replacement->string_value = value.object->data;
        for (const auto& slot : value.object->pointers) {
            if (slot.value->address) replacement->object_relocations.push_back(
                {slot.offset, slot.length, clone_type(slot.value->type), *slot.value->address});
        }
        expression = std::move(replacement);
        return;
    }
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
    if (!value.type->nominal_key().empty()) {
        replacement->type = clone_type(value.type);
    }
    expression = std::move(replacement);
}

bool materializable_eval_value(const EvalValue& value, SourceLocation location,
                               Diagnostics& diagnostics, bool required) {
    if (!value.object) return true;
    for (const auto& slot : value.object->pointers) {
        if (slot.value->meta_pointer || slot.value->string || !slot.value->address) {
            if (required) diagnostics.error(location,
                "a translation-time pointer cannot escape inside an evaluated object");
            return false;
        }
    }
    return true;
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
        if (materializable_eval_value(*value, expression->location, diagnostics, true))
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
    if (materializable_eval_value(*value, expression->location, diagnostics, required))
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
            return candidate.nominal_key() == parent->nominal_key() &&
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
                    return candidate.nominal_key() == type->nominal_key() &&
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
    // Required assertions must retain access to translation-only helpers.
    // Fold their scalar value before removing those helpers; the target
    // finalization stage still owns assertion success/failure diagnostics.
    for (auto& assertion : program.static_assertions) {
        if (contains_layout_query(*assertion.condition) && !program.evaluation_size_of)
            continue;
        Evaluator evaluator(program, diagnostics, nullptr, assertion.source_namespace);
        const auto value = evaluator.required_scalar(*assertion.condition);
        if (value) replace_eval_value(assertion.condition, *value);
        else evaluator.diagnose(assertion.location);
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
            static_initializer_ = true;
            rewrite(object->initializer, object->type);
        }
        static_initializer_ = false;
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
        if (expression->kind == Expr::Kind::ByteSequence && expression->type) {
            if (static_initializer_) return;
            const auto name = "$value." + std::to_string(ordinal_++);
            auto object = std::make_unique<ObjectDecl>();
            object->location = expression->location;
            object->name = name;
            object->source_unit = source_unit_.empty() && expression->location.file
                ? expression->location.file->source_unit_at(expression->location.line) : source_unit_;
            object->type = clone_type(expression->type);
            object->type->is_const = true;
            object->initializer = std::move(expression);
            object->linkage = Linkage::Static;
            program_.objects.push_back(std::move(object));
            expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Name;
            expression->location = program_.objects.back()->location;
            bind_exact_name(*expression, name);
            return;
        }
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
                            return candidate.nominal_key() == destination->nominal_key() &&
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
    bool static_initializer_{};
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
                      const GenericAbiCanonicalizer& canonical_abi,
                      const EvaluationLayoutInstaller& install_layout) {
    if (!validate_attribute_names(program, diagnostics)) return false;
    if (!evaluate_enumerations(program, diagnostics)) return false;
    materialize_enumerators(program, diagnostics);
    if (!expand_generics(program, diagnostics, mangling, pointer_resolver,
                         canonical_abi)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("generic expansion failed without a diagnostic");
        }
        return false;
    }
    materialize_enumerators(program, diagnostics);
    lift_function_pointer_adapters(program, default_abi);
    if (!bind_operators(program, diagnostics)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error(
                "operator binding failed without a diagnostic");
        }
        return false;
    }
    if (install_layout) install_layout(program);
    if (diagnostics.errors() != 0) return false;
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
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse,
    std::shared_ptr<const SyntaxContext> call_context) {
    Program context;
    context.address_bits = address_bits;
    context.evaluation_limits = limits;
    context.evaluation_layout = layout;
    const auto invocation = macro_context->invocation;
    Evaluator evaluator(context, diagnostics, &macro, macro.source_namespace,
                        &size_of, &align_of, nullptr, std::move(macro_context), parse,
                        std::move(call_context));
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

std::optional<TokenSequence> evaluate_syntax_body(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse,
    std::shared_ptr<const SyntaxContext> call_context) {
    Program context;
    context.address_bits = address_bits;
    context.evaluation_limits = limits;
    context.evaluation_layout = layout;
    const auto invocation = macro_context->invocation;
    Evaluator evaluator(context, diagnostics, &function, function.source_namespace,
                        &size_of, &align_of, nullptr, std::move(macro_context), parse,
                        std::move(call_context));
    if (!input || !evaluator.charge_input_match(*input, invocation) ||
        !evaluator.validate_procedural_body(function)) {
        evaluator.diagnose(invocation);
        return std::nullopt;
    }
    EvalValue argument{UInt128{}, syntax_match_type()};
    argument.syntax_match = std::move(input);
    const auto result = evaluator.call(function, {argument}, invocation);
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
                        std::string(source_namespace), size_of ? &size_of : nullptr,
                        align_of ? &align_of : nullptr);
    const auto value = evaluator.required_integer(expression);
    if (!value) {
        evaluator.diagnose(expression.location);
        return std::nullopt;
    }
    return Expr::IntegerConstant{value->integer, value->type->builtin};
}

std::optional<std::uint32_t> evaluate_fixed_array_bound(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace) {
    const auto value = evaluate_target_integer_constant(program, expression, diagnostics,
        size_of, align_of, source_namespace);
    if (!value) return {};
    const auto type = builtin_type(value->type);
    const auto bits = value->type == BuiltinType::Iptr || value->type == BuiltinType::Uptr
        ? program.address_bits : type_bits(type);
    const bool negative = signed_builtin(value->type) && bits &&
        (shift_right(value->value, bits - 1).low & 1U);
    if (negative || value->value.high || value->value.low == 0 ||
        value->value.low > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics.error(expression.location,
            "fixed array bound must be a positive integer representable in 32 bits");
        return {};
    }
    return static_cast<std::uint32_t>(value->value.low);
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
