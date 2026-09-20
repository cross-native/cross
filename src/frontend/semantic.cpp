// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/semantic.hpp"

#include "common/uint128.hpp"
#include "common/integer_semantics.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>
#include <optional>
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
using ValueSubstitutions = std::unordered_map<std::string, const Expr*>;

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
        const auto found = values.find(source.text);
        if (found != values.end()) return clone_expr(*found->second, types, {});
    }
    auto result = std::make_unique<Expr>();
    result->kind = source.kind;
    result->location = source.location;
    result->text = source.text;
    result->string_value = source.string_value;
    result->evaluated_integer = source.evaluated_integer;
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

class StaticLocalLifter {
public:
    explicit StaticLocalLifter(Program& program) : program_(program) {}

    void run() {
        const auto initial_count = program_.functions.size();
        for (std::size_t index = 0; index < initial_count; ++index) {
            auto& function = *program_.functions[index];
            if (!function.body) continue;
            function_ = &function;
            ordinal_ = 0;
            scopes_.clear();
            scopes_.emplace_back();
            for (const auto& parameter : function.parameters) {
                scopes_.back()[parameter.name] = {};
            }
            rewrite(*function.body);
        }
        function_ = nullptr;
        scopes_.clear();
    }

private:
    const std::string* replacement(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(std::string(name));
            if (found == scope->end()) continue;
            return found->second.empty() ? nullptr : &found->second;
        }
        return nullptr;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name) {
            if (const auto* name = replacement(expression->text)) {
                expression->text = *name;
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
        if (statement.declaration) {
            auto& declaration = *statement.declaration;
            if (declaration.storage_static) {
                const auto local_name = declaration.name;
                const auto lifted_name =
                    function_->name + "::$static" +
                    std::to_string(ordinal_++) + "::" + local_name;
                scopes_.back()[local_name] = lifted_name;
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
                scopes_.back()[declaration.name] = {};
            }
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
    FunctionDecl* function_{};
    unsigned ordinal_{};
    std::vector<std::unordered_map<std::string, std::string>> scopes_;
};

void lift_static_locals(Program& program) {
    StaticLocalLifter(program).run();
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
                scopes_.back().emplace(parameter.name, parameter.type);
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
            !same_type(source.return_type, destination.result)) {
            return false;
        }
        for (std::size_t index = 0; index < source.parameters.size(); ++index) {
            if (source.parameters[index].mode !=
                    destination.parameters[index].mode ||
                !same_type(source.parameters[index].type,
                           destination.parameters[index].type)) {
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

    TypePtr lookup(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(std::string(name));
            if (found != scope->end()) return found->second;
        }
        const auto find = [&](std::string_view candidate) -> TypePtr {
            const auto found = std::find_if(
                program_.objects.begin(), program_.objects.end(),
                [&](const auto& object) { return object->name == candidate; });
            return found == program_.objects.end() ? TypePtr{}
                                                    : (*found)->type;
        };
        if (const auto exact = find(name)) return exact;
        if (!caller_ || name.find("::") != std::string_view::npos) return {};
        const auto prefix = namespace_prefix(caller_->name);
        if (!prefix.empty()) {
            if (const auto local = find(prefix + "::" + std::string(name))) {
                return local;
            }
        }
        for (const auto& imported : caller_->imports) {
            if (const auto object =
                    find(imported + "::" + std::string(name))) {
                return object;
            }
        }
        return {};
    }

    TypePtr infer(const Expr& expression) const {
        switch (expression.kind) {
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
            return lookup(expression.text);
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
                        program_, caller_, expression.left->text,
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
        adapter->source_unit = source_unit_;
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
        if (expression->kind != Expr::Kind::Name) return false;
        const auto* source = resolve_function(
            program_, caller_, expression->text,
            [](const FunctionDecl&) { return true; });
        if (!source || !same_shape(*source, *signature) ||
            already_stable(*source, signature->abi) || source->variadic) {
            return false;
        }
        expression->text = make_adapter(*source, *signature,
                                        expression->location);
        return true;
    }

    void rewrite(std::unique_ptr<Expr>& expression,
                 const TypePtr& destination = {}) {
        if (!expression) return;
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
                        program_, caller_, expression->left->text,
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
            scopes_.back()[statement.declaration->name] =
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
    std::vector<std::unordered_map<std::string, TypePtr>> scopes_;
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
    std::string_view current_namespace, std::string_view name) {
    if (name.find("::") != std::string_view::npos) {
        return exact_enumerator(program, name);
    }
    auto name_space = std::string(current_namespace);
    if (name_space.empty() && caller) name_space = caller->source_namespace;
    while (!name_space.empty()) {
        if (auto found = exact_enumerator(
                program, name_space + "::" + std::string(name))) {
            return found;
        }
        const auto separator = name_space.rfind("::");
        if (separator == std::string::npos) break;
        name_space.resize(separator);
    }
    if (caller) {
        for (const auto& imported : caller->imports) {
            if (auto found = exact_enumerator(
                    program, imported + "::" + std::string(name))) {
                return found;
            }
        }
    }
    return exact_enumerator(program, name);
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
              const LayoutQuery* align_of = nullptr)
        : program_(program), diagnostics_(diagnostics),
          current_function_(caller),
          current_namespace_(std::move(current_namespace)),
          size_of_(size_of), align_of_(align_of) {}

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
            return lookup(expression.text, expression.location);
        case Expr::Kind::Parenthesized:
            return expression.left ? this->expression(*expression.left)
                                   : std::nullopt;
        case Expr::Kind::Cast: {
            auto value = expression.left
                             ? this->expression(*expression.left)
                             : std::nullopt;
            return value && expression.type
                       ? convert(*value, expression.type, expression.location)
                       : std::nullopt;
        }
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof: {
            const auto type = expression.type
                                  ? expression.type
                                  : expression.left
                                        ? expression_type(*expression.left)
                                        : TypePtr{};
            const auto* query = expression.kind == Expr::Kind::Sizeof
                                    ? size_of_
                                    : align_of_;
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
        case Expr::Kind::AggregateInitializer:
            fail(expression.location,
                 "an aggregate initializer is not a scalar expression");
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
        if (node.kind == Expr::Kind::Sizeof ||
            node.kind == Expr::Kind::Alignof) {
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
                    if (from->address_space != to->address_space) return false;
                    if ((from->pointee->is_const && !to->pointee->is_const) ||
                        (from->pointee->is_volatile && !to->pointee->is_volatile) ||
                        from->pointee->is_atomic != to->pointee->is_atomic)
                        return false;
                    auto source = clone_type(from->pointee);
                    auto destination = clone_type(to->pointee);
                    source->is_const = destination->is_const = false;
                    source->is_volatile = destination->is_volatile = false;
                    source->is_restrict = destination->is_restrict = false;
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
            if (const auto found = resolve_enumerator(
                    program_, current_function_, current_namespace_,
                    expression.text);
                found && found->enumerator->value) {
                return enum_type(found->enumeration->name,
                                 found->enumeration->underlying);
            }
            return {};
        case Expr::Kind::Parenthesized:
            return expression.left ? expression_type(*expression.left) : nullptr;
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
            return builtin_type(BuiltinType::Uptr);
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
                return result;
            }
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
        if (const auto found = resolve_enumerator(
                program_, current_function_, current_namespace_, name);
            found && found->enumerator->value) {
            return EvalValue{
                found->enumerator->value->value,
                enum_type(found->enumeration->name,
                          found->enumeration->underlying)};
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
    std::string current_namespace_;
    const LayoutQuery* size_of_{};
    const LayoutQuery* align_of_{};
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
                scopes_.back().insert(parameter.name);
            }
            if (function->body) rewrite(*function->body);
        }
        caller_ = nullptr;
        scopes_.clear();
    }

private:
    bool local(std::string_view name) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (scope->contains(std::string(name))) return true;
        }
        return false;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        if (expression->kind == Expr::Kind::Name &&
            !local(expression->text) &&
            !resolve_object(program_, caller_, expression->text) &&
            !resolve_function(program_, caller_, expression->text,
                              [](const FunctionDecl&) { return true; })) {
            if (const auto found = resolve_enumerator(
                    program_, caller_, current_namespace_, expression->text);
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
            rewrite(statement.declaration->initializer);
            scopes_.back().insert(statement.declaration->name);
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
    std::vector<std::unordered_set<std::string>> scopes_;
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
    if (expression.kind == Expr::Kind::Unary && expression.text == "&")
        return true;
    if (expression.kind == Expr::Kind::Name) {
        const auto is_array = [&](std::string_view name) {
            return std::any_of(
                program.objects.begin(), program.objects.end(),
                [&](const auto& candidate) {
                    return candidate->name == name && candidate->type &&
                           candidate->type->kind == Type::Kind::Array;
                });
        };
        if (is_array(expression.text)) return true;
        if (!source_namespace.empty() &&
            expression.text.find("::") == std::string::npos &&
            is_array(std::string(source_namespace) + "::" +
                     expression.text)) return true;
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
    for (auto& record : program.records) {
        for (auto& member : record.members) {
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
            visit_initializer_children(*expression,
                [&](const Expr& child) { self(self, &child); });
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
            if (is_integer(object->type) &&
                object->initializer->kind !=
                    Expr::Kind::AggregateInitializer &&
                !contains_relocation_candidate(
                    *object->initializer, program,
                    namespace_prefix(object->name))) {
                // Required initializers own the complete expression. Visiting
                // child calls first would evaluate untaken logical/conditional
                // arms and lose their short-circuit semantics.
                if (!contains_layout_query(*object->initializer)) {
                    rewrite_required_integer(object->initializer, nullptr,
                                             program, diagnostics);
                }
            } else {
                rewrite_eval_expr(object->initializer, nullptr, program,
                                  diagnostics, opportunistic, true);
            }
            infer_initializer_array_bound(object->type,
                                          object->initializer.get(),
                                          diagnostics);
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
    visit_initializer_children(*expression,
        [&](std::unique_ptr<Expr>& child) {
            rewrite_raw_inline_expr(child, caller, program, diagnostics,
                                    depth);
        });
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

class StringPoolLifter {
public:
    explicit StringPoolLifter(Program& program) : program_(program) {}

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
            object->source_unit = source_unit_;
            object->type = array_type(
                builtin_type(BuiltinType::U8, true),
                static_cast<std::uint32_t>(expression->string_value.size() + 1));
            object->initializer = std::move(expression);
            object->linkage = Linkage::Static;
            program_.objects.push_back(std::move(object));

            expression = std::make_unique<Expr>();
            expression->kind = Expr::Kind::Name;
            expression->location = program_.objects.back()->location;
            expression->text = name;
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

} // namespace

bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls, std::string_view mangling,
                      std::string_view default_abi) {
    if (!validate_attribute_names(program, diagnostics)) return false;
    if (!evaluate_enumerations(program, diagnostics)) return false;
    materialize_enumerators(program);
    if (!expand_generics(program, diagnostics, mangling)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("generic expansion failed without a diagnostic");
        }
        return false;
    }
    lift_static_locals(program);
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
        const auto value = evaluator.required_integer(*assertion.condition);
        if (!value) {
            diagnostics.error(
                assertion.location,
                "$::static_assert condition is not an integer constant expression");
            evaluator.diagnose(assertion.location);
            continue;
        }
        if (value->integer == UInt128{}) {
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
