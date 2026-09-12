// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/semantic.hpp"

#include "common/uint128.hpp"
#include "common/integer_semantics.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
using ValueSubstitutions = std::unordered_map<std::string, const Expr*>;

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
    }
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
    } else if (source->kind == Type::Kind::Vector) {
        result = vector_type(clone_type(source->element, substitutions),
                             source->lanes, source->scalable);
    } else if (source->kind == Type::Kind::Array) {
        result = array_type(clone_type(source->element, substitutions),
                            source->lanes);
    } else if (source->kind == Type::Kind::Record) {
        result = record_type(source->nominal_name, source->is_union);
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
    return result;
}

std::unique_ptr<Expr> clone_expr(const Expr& source,
                                 const TypeSubstitutions& types = {},
                                 const ValueSubstitutions& values = {});

std::unique_ptr<Expr> clone_expr(const Expr& source,
                                 const TypeSubstitutions& types,
                                 const ValueSubstitutions& values) {
    if (source.kind == Expr::Kind::Name) {
        const auto found = values.find(source.text);
        if (found != values.end()) return clone_expr(*found->second, types, {});
    }
    auto result = std::make_unique<Expr>();
    result->kind = source.kind;
    result->location = source.location;
    result->text = source.text;
    result->evaluated_integer = source.evaluated_integer;
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

template <typename Predicate>
FunctionDecl* resolve_function(Program& program, const FunctionDecl* caller,
                               std::string_view name, Predicate predicate) {
    const auto exact = std::find_if(program.functions.begin(), program.functions.end(),
                                    [&](const auto& candidate) {
                                        return candidate->name == name &&
                                               predicate(*candidate);
                                    });
    if (exact != program.functions.end()) return exact->get();
    if (caller && name.find("::") == std::string_view::npos) {
        const auto prefix = namespace_prefix(caller->name);
        if (!prefix.empty()) {
            const auto qualified = prefix + "::" + std::string(name);
            const auto local = std::find_if(
                program.functions.begin(), program.functions.end(),
                [&](const auto& candidate) {
                    return candidate->name == qualified && predicate(*candidate);
                });
            if (local != program.functions.end()) return local->get();
        }
        for (const auto& imported : caller->imports) {
            const auto qualified = imported + "::" + std::string(name);
            const auto found = std::find_if(
                program.functions.begin(), program.functions.end(),
                [&](const auto& candidate) {
                    return candidate->name == qualified && predicate(*candidate);
                });
            if (found != program.functions.end()) return found->get();
        }
    }
    return nullptr;
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

std::string generic_link_name(const FunctionDecl& function,
                              const std::vector<Expr::GenericArgument>& arguments,
                              std::string_view mangling) {
    std::vector<ManglingArgument> rendered;
    rendered.reserve(arguments.size());
    for (const auto& argument : arguments) {
        if (argument.type) {
            rendered.push_back(
                {ManglingArgument::Kind::Type,
                 canonical_type_name(argument.type)});
        } else {
            rendered.push_back(
                {ManglingArgument::Kind::Value,
                 argument.value ? argument.value->text : "<invalid>"});
        }
    }
    std::vector<ManglingParameter> parameters;
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        parameters.push_back(
            {.spelling = canonical_type_name(parameter.type),
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

struct GenericExpansionState {
    std::unordered_map<std::string, std::string> instance_names;
    std::unordered_set<const FunctionDecl*> rewritten_functions;
    unsigned depth{};
};

bool normalize_generic_arguments(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location);

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
    for (auto& child : statement.statements) {
        rewrite_generic_statement(*child, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.declaration) {
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
    if (statement.first) {
        rewrite_generic_statement(*statement.first, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.second) {
        rewrite_generic_statement(*statement.second, caller, program, diagnostics,
                                  state, mangling);
    }
}

void rewrite_generic_function(FunctionDecl& function, Program& program,
                              Diagnostics& diagnostics,
                              GenericExpansionState& state,
                              std::string_view mangling) {
    if (!function.body || !function.generic_parameters.empty() ||
        !state.rewritten_functions.insert(&function).second) return;
    rewrite_generic_statement(*function.body, &function, program, diagnostics,
                              state, mangling);
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
            if (!argument.value || argument.value->kind != Expr::Kind::Integer) {
                diagnostics.error(source.location,
                                  "generic value parameter '" + parameter.name +
                                      "' requires an integer constant argument");
                return {};
            }
            values.emplace(parameter.name, argument.value.get());
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
        if (attribute.name != "generic") result->attributes.push_back(attribute);
    }
    result->result_location = source.result_location;
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
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        return;
    }
    const auto name = expression->left->text;
    auto* generic = resolve_function(
        program, caller, name,
        [](const FunctionDecl& candidate) {
            return !candidate.generic_parameters.empty();
        });
    if (expression->generic_arguments.empty()) {
        if (generic) {
            diagnostics.error(expression->location,
                              "generic function '" + generic->name +
                                  "' requires explicit ::<...> arguments");
        } else if (auto* function = resolve_function(
                       program, caller, name,
                       [](const FunctionDecl& candidate) { return candidate.body != nullptr; })) {
            // An ordinary helper used by a generic constant may itself call
            // generics. Prepare its definition before any evaluator enters it,
            // regardless of declaration order. The visited set breaks cycles.
            rewrite_generic_function(*function, program, diagnostics, state, mangling);
        }
        return;
    }
    if (!generic) {
        diagnostics.error(expression->location,
                          "generic arguments applied to non-generic function '" +
                              name + "'");
        return;
    }
    if (!normalize_generic_arguments(*generic, expression->generic_arguments,
                                     caller, program, diagnostics, expression->location)) return;
    const auto link_name = generic_link_name(
        *generic, expression->generic_arguments, mangling);
    const auto found = state.instance_names.find(link_name);
    std::string internal_name;
    if (found != state.instance_names.end()) {
        internal_name = found->second;
    } else {
        if (state.depth >= 128 || state.instance_names.size() >= 4096) {
            diagnostics.error(expression->location, "generic instantiation budget exceeded");
            return;
        }
        internal_name = generic->name + "$G" +
                        std::to_string(stable_hash(link_name));
        auto instance = instantiate(*generic, expression->generic_arguments,
                                    internal_name, diagnostics);
        if (!instance) return;
        if (instance->linkage == Linkage::Global) {
            instance->attributes.push_back(
                {"link_name", {'"' + link_name + '"'}, instance->location});
        }
        state.instance_names.emplace(link_name, internal_name);
        auto* concrete = instance.get();
        program.functions.push_back(std::move(instance));
        // Publish before walking the body so recursive identical instances
        // resolve to the in-progress function. Nested constant generic calls
        // can then be evaluated through the same visible definition table.
        if (concrete->body) {
            ++state.depth;
            rewrite_generic_function(*concrete, program, diagnostics, state, mangling);
            --state.depth;
        }
    }
    expression->left->text = std::move(internal_name);
    expression->generic_arguments.clear();
}

bool expand_generics(Program& program, Diagnostics& diagnostics,
                     std::string_view mangling) {
    GenericExpansionState state;
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (!function->generic_parameters.empty()) continue;
        rewrite_generic_function(*function, program, diagnostics, state, mangling);
    }
    for (auto& object : program.objects) {
        if (object->initializer) {
            rewrite_generic_expr(object->initializer, nullptr, program,
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
                                 std::string_view name) {
    const auto exact = std::find_if(
        program.objects.begin(), program.objects.end(),
        [&](const auto& candidate) { return candidate->name == name; });
    if (exact != program.objects.end()) return exact->get();
    if (!caller || name.find("::") != std::string_view::npos) return nullptr;
    const auto prefix = namespace_prefix(caller->name);
    if (!prefix.empty()) {
        const auto qualified = prefix + "::" + std::string(name);
        const auto local = std::find_if(
            program.objects.begin(), program.objects.end(),
            [&](const auto& candidate) {
                return candidate->name == qualified;
            });
        if (local != program.objects.end()) return local->get();
    }
    for (const auto& imported : caller->imports) {
        const auto qualified = imported + "::" + std::string(name);
        const auto found = std::find_if(
            program.objects.begin(), program.objects.end(),
            [&](const auto& candidate) {
                return candidate->name == qualified;
            });
        if (found != program.objects.end()) return found->get();
    }
    return nullptr;
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
                scopes_.back().emplace(parameter.name, parameter.type);
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

    TypePtr find_name(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(std::string(name));
            if (found != scope->end()) return found->second;
        }
        if (const auto* object = resolve_object(program_, caller_, name)) {
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
            return find_name(expression.text);
        case Expr::Kind::Parenthesized:
            return expression.left ? infer(*expression.left) : TypePtr{};
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
        case Expr::Kind::Call:
            if (expression.left &&
                expression.left->kind == Expr::Kind::Name) {
                if (const auto* function = resolve_function(
                        program_, caller_, expression.left->text,
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
            scopes_.back()[statement.declaration->name] =
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
    std::vector<std::unordered_map<std::string, TypePtr>> scopes_;
    std::unordered_map<std::string, OperatorBinding> bindings_;
    std::unordered_map<std::string, std::string> function_bindings_;
};

bool bind_operators(Program& program, Diagnostics& diagnostics) {
    return OperatorBinder(program, diagnostics).run();
}

struct EvalValue {
    UInt128 integer{};
    TypePtr type;
    std::shared_ptr<std::string> string{};
    std::size_t offset{};

    EvalValue() = default;
    EvalValue(UInt128 integer_value, TypePtr value_type,
              std::shared_ptr<std::string> string_value = {},
              std::size_t string_offset = 0)
        : integer(integer_value), type(std::move(value_type)),
          string(std::move(string_value)), offset(string_offset) {}

    [[nodiscard]] bool pointer() const { return string != nullptr; }
};

std::optional<EvalValue> parse_integer_value(const Expr& expression) {
    if (expression.evaluated_integer)
        return EvalValue{expression.evaluated_integer->value,
                         builtin_type(expression.evaluated_integer->type)};
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
              const FunctionDecl* caller = nullptr)
        : program_(program), diagnostics_(diagnostics),
          current_function_(caller) {}

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
        if (++depth_ > 256) {
            fail(location,
                 "translation-time recursion depth exceeded 256");
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
            scopes_.back()[parameter.name] = {*value, true, true};
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
            auto decoded = decode_string_literal(expression.text);
            if (!decoded) return std::nullopt;
            decoded->push_back('\0');
            return EvalValue{{}, pointer_type(builtin_type(BuiltinType::U8, true)),
                             std::make_shared<std::string>(std::move(*decoded)), 0};
        }
        case Expr::Kind::Name:
            return lookup(expression.text, expression.location);
        case Expr::Kind::Parenthesized:
            return expression.left ? this->expression(*expression.left)
                                   : std::nullopt;
        case Expr::Kind::Unary:
            return unary(expression);
        case Expr::Kind::Binary:
            return binary(expression);
        case Expr::Kind::Conditional: {
            const auto type = expression_type(expression);
            auto condition = this->expression(*expression.left);
            if (!condition || condition->pointer() || !type) return std::nullopt;
            auto value = this->expression(*(condition->integer == UInt128{}
                                          ? expression.third
                                          : expression.right));
            return value ? convert(*value, type, expression.location) : std::nullopt;
        }
        case Expr::Kind::Assign:
            return assign(expression);
        case Expr::Kind::Call:
            return call_expression(expression);
        case Expr::Kind::Floating:
            fail(expression.location,
                 "floating translation-time evaluation is not implemented yet");
            return std::nullopt;
        }
        return std::nullopt;
    }

private:
    struct Cell {
        EvalValue value;
        bool initialized{};
        bool read_only{};
    };

    // Required folding must not discard malformed syntax in an untaken arm.
    // This validates the scalar expression boundary without executing calls
    // or arithmetic (division by zero in a short-circuited arm is permitted).
    bool validate_required_tree(const Expr& node) {
        if (node.kind == Expr::Kind::Call) {
            if (!node.left || node.left->kind != Expr::Kind::Name) {
                fail(node.location, "indirect calls in required constant expressions are not implemented yet");
                return false;
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
            const auto* function = resolve_function(program_, current_function_, node.left->text,
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
                    if ((from->pointee->is_const && !to->pointee->is_const) ||
                        (from->pointee->is_volatile && !to->pointee->is_volatile) ||
                        from->pointee->is_atomic != to->pointee->is_atomic)
                        return false;
                    auto source = clone_type(from->pointee);
                    auto destination = clone_type(to->pointee);
                    source->is_const = destination->is_const = false;
                    source->is_volatile = destination->is_volatile = false;
                    const auto void_type = [](const TypePtr& type) {
                        return type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void;
                    };
                    return same_type(source, destination) ||
                        ((void_type(source) || void_type(destination)) &&
                         source->kind != Type::Kind::Function && destination->kind != Type::Kind::Function);
                };
                const bool compatible = from && to &&
                    ((is_integer(from) && is_integer(to)) || same_type(from, to) ||
                     pointer_compatible());
                if (!compatible) {
                    fail(argument.location, "unsupported or incompatible argument type in required expression");
                    return false;
                }
            }
            return true;
        }
        if (node.left && !validate_required_tree(*node.left)) return false;
        if (node.right && !validate_required_tree(*node.right)) return false;
        if (node.third && !validate_required_tree(*node.third)) return false;
        if (node.kind == Expr::Kind::Integer) return expression(node).has_value();
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
        if (node.kind == Expr::Kind::Unary &&
            (!node.left || !is_integer(expression_type(*node.left)) ||
             (node.text != "+" && node.text != "-" && node.text != "!" && node.text != "~" &&
              node.text != "++" && node.text != "--" && node.text != "post++" && node.text != "post--"))) {
            fail(node.location, "unsupported unary operand in required integer expression");
            return false;
        }
        const bool modifying = node.kind == Expr::Kind::Assign ||
            (node.kind == Expr::Kind::Unary &&
             (node.text == "++" || node.text == "--" || node.text.starts_with("post")));
        if (modifying) {
            if (!node.left || node.left->kind != Expr::Kind::Name ||
                (node.right && !is_integer(expression_type(*node.right)))) {
                fail(node.location, "unsupported assignment in required integer expression");
                return false;
            }
            bool read_only = expression_type(*node.left)->is_const;
            if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (parameter.name == node.left->text)
                        read_only = read_only || parameter.mode == ParameterMode::In;
            }
            if (read_only) {
                fail(node.location, "cannot write an 'in' or const cell");
                return false;
            }
        }
        if (node.kind == Expr::Kind::Binary || node.kind == Expr::Kind::Conditional) {
            const auto left = node.left ? expression_type(*node.left) : nullptr;
            const auto right = node.right ? expression_type(*node.right) : nullptr;
            const bool indexing = node.kind == Expr::Kind::Binary && node.text == "index";
            if (!left || !right ||
                (indexing ? left->kind != Type::Kind::Pointer || !is_integer(right)
                          : !is_integer(left) || !is_integer(right))) {
                fail(node.location, "non-integer operation in required integer expression");
                return false;
            }
        }
        return true;
    }

    TypePtr expression_type(const Expr& expression) {
        switch (expression.kind) {
        case Expr::Kind::Integer: {
            const auto value = parse_integer_value(expression);
            return value ? value->type : nullptr;
        }
        case Expr::Kind::Character: return builtin_type(BuiltinType::U32);
        case Expr::Kind::Name:
            if (const auto* cell = lookup_mutable(expression.text)) return cell->value.type;
            if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (parameter.name == expression.text) return parameter.type;
            }
            if (const auto* object = resolve_object(program_, current_function_, expression.text)) return object->type;
            return {};
        case Expr::Kind::Parenthesized:
            return expression.left ? expression_type(*expression.left) : nullptr;
        case Expr::Kind::Assign:
            return expression_type(*expression.left);
        case Expr::Kind::Unary: {
            if (expression.text == "!") return builtin_type(BuiltinType::Bool);
            auto type = expression_type(*expression.left);
            if (!type || !is_integer(type)) return {};
            if (expression.text == "++" || expression.text == "--" || expression.text.starts_with("post")) return type;
            return builtin_integer(promote_integer(integer_type(type)));
        }
        case Expr::Kind::Binary:
        case Expr::Kind::Conditional: {
            const bool conditional = expression.kind == Expr::Kind::Conditional;
            if (!conditional && expression.text == "index") {
                const auto base = expression_type(*expression.left);
                return base && base->kind == Type::Kind::Pointer ? base->pointee : nullptr;
            }
            if (!conditional && (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == ">" || expression.text == "<=" ||
                expression.text == ">=" || expression.text == "&&" || expression.text == "||"))
                return builtin_type(BuiltinType::Bool);
            const auto left = expression_type(*(conditional ? expression.right : expression.left));
            const auto right = expression_type(*(conditional ? expression.third : expression.right));
            if (!left || !right || !is_integer(left) || !is_integer(right)) return {};
            if (!conditional && (expression.text == "<<" || expression.text == ">>"))
                return builtin_integer(promote_integer(integer_type(left)));
            return builtin_integer(common_integer_type(integer_type(left), integer_type(right)));
        }
        case Expr::Kind::Call:
            if (!expression.left || expression.left->kind != Expr::Kind::Name) return {};
            if ((expression.left->text == "$::eval" || expression.left->text == "$::runtime") &&
                expression.arguments.size() == 1) return expression_type(*expression.arguments.front());
            if (const auto* callee = resolve_function(program_, current_function_, expression.left->text,
                    [](const FunctionDecl&) { return true; })) return callee->return_type;
            return {};
        case Expr::Kind::String: return pointer_type(builtin_type(BuiltinType::U8, true));
        case Expr::Kind::Floating: return {};
        }
        return {};
    }

    IntegerType integer_type(const TypePtr& type) const {
        auto bits = type_bits(type);
        if (type && (type->kind == Type::Kind::Pointer ||
            (type->kind == Type::Kind::Builtin &&
             (type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr))))
            bits = program_.address_bits;
        return {bits, signed_value(EvalValue{{}, type}),
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

    std::optional<EvalValue> convert(EvalValue value, const TypePtr& type,
                                     SourceLocation location) {
        if (!type || type->is_volatile || type->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            return std::nullopt;
        }
        if (value.pointer()) {
            if (type->kind == Type::Kind::Pointer) {
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
        if (!is_integer(value.type) || !is_integer(type)) return std::nullopt;
        value.integer = convert_integer(value.integer, integer_type(value.type), integer_type(type));
        value.type = clone_type(type);
        return value;
    }

    std::optional<EvalValue> calculate(IntegerOperation operation, EvalValue left,
                                       EvalValue right, SourceLocation location) {
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
        return EvalValue{result.value, comparison ? builtin_type(BuiltinType::Bool) : builtin_integer(type)};
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
        if (++steps_ <= 1000000) return true;
        if (!budget_diagnosed_) {
            fail(location,
                 "translation-time instruction budget exceeded 1000000");
            budget_diagnosed_ = true;
        }
        return false;
    }

    Cell* lookup_mutable(std::string_view name) {
        for (auto index = scopes_.size(); index > frame_base_;) {
            auto& scope = scopes_[--index];
            const auto found = scope.find(std::string(name));
            if (found != scope.end()) return &found->second;
        }
        return nullptr;
    }

    std::optional<EvalValue> lookup(std::string_view name, SourceLocation location) {
        if (auto* cell = lookup_mutable(name)) {
            if (!cell->initialized) {
                fail(location, "read of uninitialized value during translation-time evaluation");
                return std::nullopt;
            }
            if (cell->value.type->is_volatile || cell->value.type->is_atomic) {
                fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
                return std::nullopt;
            }
            return cell->value;
        }
        return std::nullopt;
    }

    std::optional<EvalValue> unary(const Expr& expression) {
        if (!expression.left) return std::nullopt;
        if (expression.text == "++" || expression.text == "--" ||
            expression.text == "post++" || expression.text == "post--") {
            if (expression.left->kind != Expr::Kind::Name) return std::nullopt;
            auto* cell = lookup_mutable(expression.left->text);
            if (!cell) return std::nullopt;
            if (cell->read_only) {
                fail(expression.location, "cannot write an 'in' or const cell");
                return std::nullopt;
            }
            const auto previous = lookup(expression.left->text, expression.location);
            if (!previous || previous->pointer()) return std::nullopt;
            auto value = calculate(expression.text == "++" || expression.text == "post++"
                ? IntegerOperation::Add : IntegerOperation::Subtract,
                *previous, EvalValue{UInt128{1}, builtin_type(BuiltinType::I32)}, expression.location);
            if (!value) return std::nullopt;
            value = convert(*value, previous->type, expression.location);
            if (!value) return std::nullopt;
            lookup_mutable(expression.left->text)->value = *value;
            return expression.text.starts_with("post") ? previous : value;
        }
        auto value = this->expression(*expression.left);
        if (!value || value->pointer()) return std::nullopt;
        value = convert(*value, builtin_integer(promote_integer(integer_type(value->type))), expression.location);
        if (!value) return std::nullopt;
        if (expression.text == "+") return value;
        if (expression.text == "-") {
            return calculate(IntegerOperation::Subtract, EvalValue{{}, value->type}, *value, expression.location);
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
        const auto operation = expression.text;
        if (operation == "&&" || operation == "||") {
            if (left->pointer()) return std::nullopt;
            const bool lhs = !(left->integer == UInt128{});
            if ((operation == "&&" && !lhs) ||
                (operation == "||" && lhs)) {
                return EvalValue{{operation == "||", 0},
                                 builtin_type(BuiltinType::Bool)};
            }
            auto right = this->expression(*expression.right);
            if (!right || right->pointer()) return std::nullopt;
            return EvalValue{{!(right->integer == UInt128{}), 0},
                             builtin_type(BuiltinType::Bool)};
        }
        auto right = this->expression(*expression.right);
        if (!right) return std::nullopt;
        if (expression.text == "index") {
            if (!left->pointer() || right->pointer() || right->integer.high != 0) {
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
        if (left->pointer() || right->pointer()) return std::nullopt;
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
        if (!expression.left || expression.left->kind != Expr::Kind::Name ||
            !expression.right) {
            return std::nullopt;
        }
        auto* destination = lookup_mutable(expression.left->text);
        if (!destination) return std::nullopt;
        if (destination->read_only) {
            fail(expression.location, "cannot write an 'in' or const cell");
            return std::nullopt;
        }
        const auto destination_type = destination->value.type;
        if (expression.text == "=") {
            auto source = this->expression(*expression.right);
            if (!source) return std::nullopt;
            source = convert(*source, destination_type, expression.location);
            if (!source) return std::nullopt;
            *lookup_mutable(expression.left->text) = {*source, true, false};
            return source;
        }
        Expr binary_expression;
        binary_expression.kind = Expr::Kind::Binary;
        binary_expression.location = expression.location;
        binary_expression.text =
            expression.text.substr(0, expression.text.size() - 1);
        binary_expression.left = std::make_unique<Expr>();
        binary_expression.left->kind = Expr::Kind::Name;
        binary_expression.left->text = expression.left->text;
        binary_expression.right = clone_expr(*expression.right);
        auto result = binary(binary_expression);
        if (!result) return std::nullopt;
        result = convert(*result, destination_type, expression.location);
        if (!result) return std::nullopt;
        *lookup_mutable(expression.left->text) = {*result, true, false};
        return result;
    }

    std::optional<EvalValue> call_expression(const Expr& expression) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
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
                program_, current_function_, expression.left->text,
                [](const FunctionDecl& candidate) {
                    return candidate.attribute("runtime_only") != nullptr;
                })) {
            fail(expression.location,
                 "call to runtime-only function '" + blocked->name +
                     "' cannot be evaluated during translation");
            return std::nullopt;
        }
        auto* function = resolve_function(
            program_, current_function_, expression.left->text,
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
            if (statement.declaration->dynamic_array_bound) {
                return {Flow::Failed};
            }
            EvalValue value{{}, clone_type(statement.declaration->type)};
            scopes_.back()[statement.declaration->name] = {
                value, false, statement.declaration->type->is_const};
            if (statement.declaration->initializer) {
                auto initializer = expression(*statement.declaration->initializer);
                if (!initializer) return {Flow::Failed};
                initializer = convert(*initializer, statement.declaration->type, statement.location);
                if (!initializer) return {Flow::Failed};
                auto& cell = scopes_.back()[statement.declaration->name];
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
            if (!condition || condition->pointer()) return {Flow::Failed};
            if (!(condition->integer == UInt128{})) {
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
                    scopes_.back()[node->declaration->name] = {
                        EvalValue{{}, clone_type(node->declaration->type)}, false,
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
                if (!condition || condition->pointer()) return {Flow::Failed};
                if (condition->integer == UInt128{}) return {};
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
                if (!condition || condition->pointer()) return {Flow::Failed};
                if (condition->integer == UInt128{}) return {};
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
                    if (!condition || condition->pointer()) {
                        scopes_.pop_back();
                        return {Flow::Failed};
                    }
                    if (condition->integer == UInt128{}) break;
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
    std::vector<std::unordered_map<std::string, Cell>> scopes_;
    std::size_t frame_base_{};
    std::uint64_t steps_{};
    unsigned depth_{};
    bool budget_diagnosed_{};
    SourceLocation failure_location_;
    std::string failure_reason_;
    std::vector<CallFrame> call_stack_;
    std::vector<CallFrame> failure_trace_;
};

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
    return function.attribute("eval_only") != nullptr;
}

bool runtime_only(const FunctionDecl& function) {
    return function.attribute("runtime_only") != nullptr;
}

void replace_eval_value(std::unique_ptr<Expr>& expression,
                        const EvalValue& value) {
    auto replacement = std::make_unique<Expr>();
    replacement->kind = Expr::Kind::Integer;
    replacement->location = expression->location;
    replacement->text =
        to_decimal(value.integer) + literal_suffix(value.type);
    replacement->evaluated_integer = Expr::IntegerConstant{
        value.integer, value.type->builtin};
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

bool normalize_generic_arguments(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location) {
    if (arguments.size() != generic.generic_parameters.size()) {
        diagnostics.error(location, "generic argument count does not match '" + generic.name + "'");
        return false;
    }
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& parameter = generic.generic_parameters[index];
        auto& argument = arguments[index];
        if (!parameter.value_type) {
            if (argument.type) continue;
            diagnostics.error(location, "generic type parameter '" + parameter.name +
                                        "' requires a type argument");
            return false;
        }
        if (!is_integer(parameter.value_type)) {
            diagnostics.error(location, "non-integer generic value parameters are not implemented yet");
            return false;
        }
        if (!argument.value) {
            diagnostics.error(location, "generic value parameter '" + parameter.name +
                                        "' requires a value argument");
            return false;
        }
        if (!rewrite_required_integer(argument.value, caller, program, diagnostics,
                                      parameter.value_type)) return false;
    }
    return true;
}

void rewrite_eval_expr(std::unique_ptr<Expr>& expression,
                       FunctionDecl* caller, Program& program,
                       Diagnostics& diagnostics,
                       bool opportunistic,
                       bool required_context = false,
                       bool runtime_context = false) {
    if (!expression) return;

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
        replace_eval_value(expression, *value);
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
        program, caller, expression->left->text,
        [](const FunctionDecl& candidate) {
            return candidate.body != nullptr &&
                   candidate.attribute("macro") == nullptr;
        });
    auto* required_declaration = resolve_function(
        program, caller, expression->left->text,
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
    replace_eval_value(expression, *value);
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
    for (const auto& function : program.functions) {
        if (!function->body) continue;
        std::vector<std::unordered_map<std::string, bool>> scopes(1);
        for (const auto& parameter : function->parameters)
            scopes.back()[parameter.name] = parameter.mode == ParameterMode::In || parameter.type->is_const;
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
                    const auto found = scope->find(destination->text);
                    if (found != scope->end()) { read_only = found->second; break; }
                }
                if (!read_only) {
                    if (const auto* object = resolve_object(program, function.get(), destination->text))
                        read_only = object->type->is_const;
                }
                if (read_only.value_or(false))
                    diagnostics.error(expression->location, "cannot write an 'in' or const cell");
            }
            self(self, expression->left.get());
            self(self, expression->right.get());
            self(self, expression->third.get());
            for (const auto& argument : expression->arguments) self(self, argument.get());
        };
        const auto check_statement = [&](const auto& self, const Statement& statement) -> void {
            const bool scoped = statement.kind == Statement::Kind::Compound || statement.kind == Statement::Kind::For;
            if (scoped) scopes.emplace_back();
            if (statement.declaration) {
                scopes.back()[statement.declaration->name] = statement.declaration->type->is_const;
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
        if (evaluation_only(*function) && runtime_only(*function)) {
            diagnostics.error(
                function->location,
                "a function cannot be both eval_only and runtime_only");
        }
        if (!evaluation_only(*function)) continue;
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
        if (object->initializer) {
            if (is_integer(object->type)) {
                // Required initializers own the complete expression. Visiting
                // child calls first would evaluate untaken logical/conditional
                // arms and lose their short-circuit semantics.
                rewrite_required_integer(object->initializer, nullptr, program, diagnostics);
            } else {
                rewrite_eval_expr(object->initializer, nullptr, program,
                                  diagnostics, opportunistic, true);
            }
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

const Expr* raw_inline_result(const FunctionDecl& function) {
    if (!function.body ||
        function.body->kind != Statement::Kind::Compound ||
        function.body->statements.size() != 1) {
        return nullptr;
    }
    const auto& statement = *function.body->statements.front();
    return statement.kind == Statement::Kind::Return
               ? statement.expression.get()
               : nullptr;
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

void rewrite_raw_inline_expr(std::unique_ptr<Expr>& expression,
                             FunctionDecl& caller, Program& program,
                             Diagnostics& diagnostics, unsigned depth) {
    if (!expression) return;
    if (depth > 128) {
        diagnostics.error(expression->location,
                          "recursive raw_inline expansion exceeded 128 calls");
        return;
    }
    if (expression->left) {
        rewrite_raw_inline_expr(expression->left, caller, program, diagnostics,
                                depth);
    }
    if (expression->right) {
        rewrite_raw_inline_expr(expression->right, caller, program, diagnostics,
                                depth);
    }
    if (expression->third) {
        rewrite_raw_inline_expr(expression->third, caller, program, diagnostics,
                                depth);
    }
    for (auto& argument : expression->arguments) {
        rewrite_raw_inline_expr(argument, caller, program, diagnostics, depth);
    }
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        return;
    }
    auto* callee = resolve_function(
        program, &caller, expression->left->text,
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
    const auto* result = raw_inline_result(*callee);
    if (!result) {
        diagnostics.error(
            callee->location,
            "bootstrap raw legalization currently requires raw_inline body "
            "to contain one return expression");
        return;
    }
    ValueSubstitutions values;
    for (std::size_t index = 0; index < callee->parameters.size(); ++index) {
        values.emplace(callee->parameters[index].name,
                       expression->arguments[index].get());
    }
    expression = clone_expr(*result, {}, values);
    rewrite_raw_inline_expr(expression, caller, program, diagnostics, depth + 1);
}

void rewrite_raw_inline_statement(Statement& statement, FunctionDecl& caller,
                                  Program& program, Diagnostics& diagnostics) {
    for (auto& child : statement.statements) {
        rewrite_raw_inline_statement(*child, caller, program, diagnostics);
    }
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound) {
            rewrite_raw_inline_expr(statement.declaration->dynamic_array_bound,
                                    caller, program, diagnostics, 0);
        }
        if (statement.declaration->initializer) {
            rewrite_raw_inline_expr(statement.declaration->initializer,
                                    caller, program, diagnostics, 0);
        }
    }
    if (statement.expression) {
        rewrite_raw_inline_expr(statement.expression, caller, program,
                                diagnostics, 0);
    }
    if (statement.condition) {
        rewrite_raw_inline_expr(statement.condition, caller, program,
                                diagnostics, 0);
    }
    if (statement.increment) {
        rewrite_raw_inline_expr(statement.increment, caller, program,
                                diagnostics, 0);
    }
    if (statement.first) {
        rewrite_raw_inline_statement(*statement.first, caller, program,
                                     diagnostics);
    }
    if (statement.second) {
        rewrite_raw_inline_statement(*statement.second, caller, program,
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
        if (!function->attribute("naked") || !function->body) continue;
        rewrite_raw_inline_statement(*function->body, *function, program,
                                     diagnostics);
    }
    return diagnostics.errors() == 0;
}

} // namespace

bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls, std::string_view mangling) {
    if (!validate_attribute_names(program, diagnostics)) return false;
    if (!expand_generics(program, diagnostics, mangling)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("generic expansion failed without a diagnostic");
        }
        return false;
    }
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
    return true;
}

} // namespace cross
