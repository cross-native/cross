// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/semantic.hpp"

#include "common/uint128.hpp"
#include "common/floating_semantics.hpp"
#include "common/integer_semantics.hpp"
#include "common/relocation_addend.hpp"
#include "frontend/lexer.hpp"
#include "frontend/callable_abi.hpp"
#include "frontend/evaluation_task.hpp"
#include "frontend/intrinsic_constraints.hpp"
#include "frontend/record_constraints.hpp"
#include "frontend/source_type_query.hpp"
#include "frontend/vector_constraints.hpp"
#include "frontend/builtin_registry.hpp"
#include "frontend/syntax.hpp"
#include "frontend/token_tree.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross {

std::span<const std::string_view> core_attribute_names() {
    static constexpr std::string_view names[] = {
        "abi", "address", "address_space", "alias", "aligned", "always_inline",
        "atomic", "clobber", "cold", "eval_only", "exhaustive", "ext_vector_type",
        "hot", "interrupt", "link_name", "macro", "may_alias",
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
    std::shared_ptr<const FunctionScopeIdentity> function_scope{
        std::make_shared<const FunctionScopeIdentity>()};
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
        identity->function_scope = function_scope;
        identity->instance_key = serialization;
        identities.emplace(source.get(), identity);
        return identity;
    }
};

struct TypeSubstitutions : NameMap<TypePtr> {
    std::shared_ptr<NominalInstantiation> nominal;
    Diagnostics* diagnostics{};
};
using ValueSubstitutions = NameMap<const Expr*>;

using EnumInitializerPreparation = ContinuationQuery<void(std::unique_ptr<Expr>&)>;
EvaluationTask<bool> evaluate_enumerations_async(Program& program, Diagnostics& diagnostics,
    std::size_t first, const EnumInitializerPreparation& prepare,
    std::optional<Program::EnumerationPosition> through = {});
bool evaluate_enumerations(Program& program, Diagnostics& diagnostics, std::size_t first = 0,
                           const EnumInitializerPreparation& prepare = {},
                           std::optional<Program::EnumerationPosition> through = {});
void materialize_enumerators(Program& program, Diagnostics& diagnostics);
bool evaluation_only(const FunctionDecl& function);
std::optional<std::uint32_t> fixed_array_bound_value(Expr::IntegerConstant value, unsigned address_bits);

bool same_function_entity(const FunctionDecl& left, const FunctionDecl& right) {
    return left.name == right.name && left.fresh == right.fresh &&
        ((left.linkage != Linkage::Static && right.linkage != Linkage::Static) ||
         left.source_unit == right.source_unit);
}

const Attribute* function_entity_attribute(const Program& program,
    const FunctionDecl& function, std::string_view name) {
    if (const auto* attribute = function.attribute(name)) return attribute;
    for (const auto* declarations : {&program.functions, &program.evaluation_definitions})
        for (const auto& declaration : *declarations)
            if (same_function_entity(function, *declaration))
                if (const auto* attribute = declaration->attribute(name)) return attribute;
    return nullptr;
}

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

std::optional<std::string> attribute_argument_error(const Attribute& attribute) {
    if (attribute.arguments.empty()) return {};
    // These core contracts take no arguments regardless of declaration
    // context, target or ABI. Validate before translation-only erasure as
    // well as at ordinary source entry; HIR cannot check an erased helper.
    static constexpr std::string_view argument_free[] = {
        "always_inline", "atomic", "cold", "eval_only", "exhaustive", "hot", "macro", "may_alias", "musttail",
        "naked", "no_stack_protector", "noinit", "noinline", "noreturn", "packed",
        "raw_inline", "retain", "returns_twice", "runtime_only", "syntax_expander",
        "thread_local", "used", "weak",
    };
    if (std::find(std::begin(argument_free), std::end(argument_free), attribute.name) !=
        std::end(argument_free))
        return attribute.name + " does not take arguments";
    return {};
}

std::optional<std::string> function_attribute_error(const Attribute& attribute) {
    if (const auto error = attribute_argument_error(attribute)) return error;
    if (attribute.name == "no_sanitize") {
        if (attribute.arguments.size() != 1)
            return "no_sanitize requires one instrumentation-name string";
        const auto name = decode_string_literal(attribute.arguments.front());
        if (!name || name->empty())
            return "no_sanitize requires one nonempty instrumentation-name string";
    }
    if (attribute.name == "packed" || attribute.name == "underlying" ||
        attribute.name == "noinit" || attribute.name == "thread_local" ||
        attribute.name == "tls_model" || attribute.name == "musttail" ||
        attribute.name == "exhaustive")
        return "attribute '" + attribute.name + "' is not valid on a function";
    return {};
}

// These contracts need an emitted function, physical call boundary or machine
// entry/exit. Expansion functions and meta helpers have none, on any model.
bool runtime_contract_attribute(std::string_view name) {
    static constexpr std::string_view names[] = {
        "abi", "alias", "aligned", "clobber", "interrupt", "link_name",
        "naked", "raw_inline", "retain", "returns_twice", "section",
        "stack_cleanup", "used", "variadic", "visibility", "weak", "weakref",
    };
    return std::find(std::begin(names), std::end(names), name) != std::end(names);
}

std::string runtime_contract_error(const Attribute& attribute, std::string_view subject) {
    return "attribute '" + attribute.name + "' requires runtime symbol, ABI transport or "
        "entry/exit machinery; it is not valid on " + std::string(subject);
}

std::optional<std::string> function_attribute_conflict(const FunctionDecl& function) {
    // These are source contracts, not lowering constraints. Check them before
    // evaluation or generic-template erasure, including the implicit eval-only
    // contract of a direct meta signature. No physical ABI is needed here.
    if (function.attribute("always_inline") && function.attribute("noinline"))
        return "a function cannot be both always_inline and noinline";
    if (function.attribute("hot") && function.attribute("cold"))
        return "a function cannot be both hot and cold";
    if (evaluation_only(function) && function.attribute("runtime_only"))
        return "a function cannot be both eval_only and runtime_only";
    if (function.attribute("raw_inline") && function.attribute("naked"))
        return "raw_inline function is managed and cannot also be naked";
    return {};
}

std::optional<unsigned> alignment_value(const Expr::IntegerConstant& value) {
    if (value.value.high != 0 || value.value.low > std::numeric_limits<unsigned>::max() ||
        value.value.low == 0 || (value.value.low & (value.value.low - 1U)) != 0)
        return {};
    return static_cast<unsigned>(value.value.low);
}

const Attribute* malformed_tail_return(const Statement& statement) {
    if (statement.kind != Statement::Kind::Return || returned_call(statement)) return nullptr;
    for (const auto& attribute : statement.attributes)
        if (attribute.name == "musttail") return &attribute;
    return nullptr;
}

bool validate_attribute_names(const Program& program,
                              Diagnostics& diagnostics) {
    enum class Subject { General, Function, Object };
    const auto validate = [&](const std::vector<Attribute>& attributes, Subject subject = Subject::General) {
        for (const auto& attribute : attributes) {
            if (!is_known_attribute(attribute.name)) {
                diagnostics.error(attribute.location,
                                  "unknown attribute '" + attribute.name + "'");
            } else if (attribute.name == "syntax_expander") {
                diagnostics.error(attribute.location,
                    "syntax_expander requires a dedicated static expansion-function declaration");
            } else if (const auto error = subject == Subject::Function ? function_attribute_error(attribute)
                                                                      : attribute_argument_error(attribute)) {
                diagnostics.error(attribute.location, *error);
            } else if (subject == Subject::Object &&
                       (attribute.name == "packed" || attribute.name == "underlying" ||
                        attribute.name == "may_alias" || attribute.name == "exhaustive")) {
                diagnostics.error(attribute.location,
                    "attribute '" + attribute.name + "' is not valid on an object");
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
        validate(function->attributes, Subject::Function);
        if (const auto error = function_attribute_conflict(*function))
            diagnostics.error(function->location, *error);
        if (function->has_meta_signature())
            for (const auto& attribute : function->attributes)
                if (runtime_contract_attribute(attribute.name))
                    diagnostics.error(attribute.location,
                        runtime_contract_error(attribute, "a translation-only helper"));
        const auto visit_statement = [&](const auto& self,
                                         const Statement& statement) -> void {
            validate(statement.attributes);
            if (const auto* attribute = malformed_tail_return(statement))
                diagnostics.error(attribute->location,
                    "musttail requires returning a call expression directly");
            for (const auto& child : statement.statements) self(self, *child);
            if (statement.first) self(self, *statement.first);
            if (statement.second) self(self, *statement.second);
        };
        if (function->body) visit_statement(visit_statement, *function->body);
    }
    for (const auto& label : program.global_labels) validate(label.attributes);
    for (const auto& object : program.objects) validate(object->attributes, Subject::Object);
    return diagnostics.errors() == 0;
}

ContinuationTask<std::unique_ptr<Expr>> clone_expr_async(const Expr& source,
    const TypeSubstitutions& types = {}, const ValueSubstitutions& values = {});
ContinuationTask<TypePtr> clone_type_async(const TypePtr& source,
    const TypeSubstitutions& substitutions = {}, const ValueSubstitutions& values = {});

ContinuationTask<TypePtr> clone_type_async(const TypePtr& source,
                   const TypeSubstitutions& substitutions,
                   const ValueSubstitutions& values) {
    if (!source) co_return TypePtr{};
    TypePtr result;
    if (source->kind == Type::Kind::Builtin) {
        result = source->nominal_key().empty()
                     ? builtin_type(source->builtin)
                     : enum_type(source->nominal_name, source->builtin);
    } else if (source->kind == Type::Kind::Pointer) {
        result = pointer_type(co_await clone_type_async(source->pointee, substitutions, values));
    } else if (source->kind == Type::Kind::Function && source->function) {
        auto parameters = source->function->parameters;
        for (auto& parameter : parameters) {
            parameter.type = co_await clone_type_async(parameter.type, substitutions, values);
            parameter.declared_array_type = co_await clone_type_async(parameter.declared_array_type, substitutions, values);
        }
        result =
            function_type(co_await clone_type_async(source->function->result, substitutions, values),
                          std::move(parameters), source->function->variadic,
                          source->function->abi);
        result->function->result_location =
            source->function->result_location;
        result->function->clobbers = source->function->clobbers;
        result->function->stack_cleanup =
            source->function->stack_cleanup;
    } else if (source->kind == Type::Kind::Vector) {
        result = vector_type(co_await clone_type_async(source->element, substitutions, values),
                             source->lanes, source->scalable);
        result->vector_bound_unit = source->vector_bound_unit;
        result->vector_extent_dependency = source->vector_extent_dependency;
        if (source->vector_bound)
            result->vector_bound = co_await clone_expr_async(*source->vector_bound, substitutions, values);
    } else if (source->kind == Type::Kind::Array) {
        result = array_type(co_await clone_type_async(source->element, substitutions, values),
                            source->lanes);
        result->array_extent_dependency = source->array_extent_dependency;
        if (source->array_bound)
            result->array_bound = co_await clone_expr_async(*source->array_bound, substitutions, values);
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
        const auto found = substitutions.find(generic_type_key(*source));
        if (found == substitutions.end()) {
            result = generic_type(source->generic_name);
            result->generic_location = source->generic_location;
            result->generic_binding = source->generic_binding;
            if (substitutions.diagnostics)
                substitutions.diagnostics->error(source->generic_location,
                    "captured generic type parameter '" + source->generic_name +
                    "' is not visible in this instantiation");
        } else {
            result = co_await clone_type_async(found->second);
        }
    }
    const bool substituted = source->kind == Type::Kind::Generic && result->kind != Type::Kind::Generic;
    // A typedef's requested alignment raises any alignment of the substituted type.
    result->alignment = std::max(result->alignment, source->alignment);
    for (const auto& request : source->alignment_requests)
        result->alignment_requests.push_back(co_await clone_expr_async(*request, substitutions, values));
    result->is_const = source->is_const || (substituted && result->is_const);
    result->is_volatile = source->is_volatile || (substituted && result->is_volatile);
    result->is_atomic = source->is_atomic || (substituted && result->is_atomic);
    result->is_restrict = source->is_restrict || (substituted && result->is_restrict);
    result->may_alias = source->may_alias || (substituted && result->may_alias);
    if (!substituted || source->address_space_location.valid()) {
        result->address_space = source->address_space;
        result->address_space_location = source->address_space_location;
    }
    if (source->kind == Type::Kind::Vector && source->element &&
        source->element->kind == Type::Kind::Generic && result->element) {
        result->is_const = result->is_const || result->element->is_const;
        result->is_volatile = result->is_volatile || result->element->is_volatile;
        result->element->is_const = result->element->is_volatile = false;
        result->element->alignment = 0;
        result->element->alignment_requests.clear();
    }
    result->address_space_location = source->address_space_location;
    result->pending_address_space = source->pending_address_space;
    result->captured_errors.insert(result->captured_errors.end(),
        source->captured_errors.begin(), source->captured_errors.end());
    if (source->captured_tag_errors) result->captured_tag_errors = source->captured_tag_errors;
    // A substituted generic may already carry a nominal identity of its own.
    if (source->nominal_identity)
        result->nominal_identity = substitutions.nominal
            ? substitutions.nominal->substitute(source->nominal_identity) : source->nominal_identity;
    co_return result;
}

void instantiate_token_bindings(TokenSequence& tokens, const TypeSubstitutions& types,
                                const ValueSubstitutions& values);

ContinuationTask<std::unique_ptr<Expr>> clone_expr_async(const Expr& source,
                                 const TypeSubstitutions& types,
                                 const ValueSubstitutions& values) {
    enum class Phase { Left, Right, Third, Arguments, GenericArguments, Initializers };
    struct Frame {
        const Expr* source;
        Expr* destination;
        const ValueSubstitutions* values;
        Phase phase{Phase::Left};
        std::size_t index{};
        std::size_t designator{};
    };
    std::vector<Frame> pending;
    const ValueSubstitutions no_values;
    const auto enter = [&](const Expr& input, const ValueSubstitutions& bindings,
                           std::unique_ptr<Expr>& destination) -> ContinuationTask<void> {
        const Expr* node = &input;
        const auto* active_values = &bindings;
        if (input.kind == Expr::Kind::Name) {
            if (const auto found = bindings.find(name_key(input)); found != bindings.end()) {
                node = found->second;
                // Actual-value source is copied, not substituted recursively
                // through this declaration's value-parameter environment.
                active_values = &no_values;
            }
        }
        auto copy = std::make_unique<Expr>();
        copy->kind = node->kind;
        copy->location = node->location;
        copy->text = node->text;
        copy->name_context = node->name_context;
        copy->translation_context = node->translation_context;
        if (node->name_context && types.nominal && node->name_context->value_binding.enumeration) {
            auto context = std::make_shared<NameLookupContext>(*node->name_context);
            context->value_binding.enumeration = types.nominal->substitute(context->value_binding.enumeration);
            copy->name_context = std::move(context);
        }
        copy->string_value = node->string_value;
        copy->quote_fragments = node->quote_fragments;
        if (types.nominal)
            for (auto& fragment : copy->quote_fragments)
                instantiate_token_bindings(fragment, types, *active_values);
        copy->evaluated_integer = node->evaluated_integer;
        copy->evaluated_floating = node->evaluated_floating;
        copy->evaluated_address = node->evaluated_address;
        copy->object_relocations = node->object_relocations;
        copy->generic_visible_at_call = node->generic_visible_at_call;
        copy->instance_label = node->instance_label;
        if (node->type) copy->type = co_await clone_type_async(node->type, types, *active_values);
        if (node->deferred_generic_signature)
            copy->deferred_generic_signature = co_await clone_type_async(node->deferred_generic_signature, types, *active_values);
        destination = std::move(copy);
        pending.push_back({node, destination.get(), active_values});
        co_return;
    };
    std::unique_ptr<Expr> result;
    co_await enter(source, values, result);
    // Ordered frames preserve the recursive copier's exact field/substitution
    // order, including generic types before values and designators before leaves.
    while (!pending.empty()) {
        auto& frame = pending.back();
        const auto& node = *frame.source;
        auto& copy = *frame.destination;
        switch (frame.phase) {
        case Phase::Left:
            frame.phase = Phase::Right;
            if (node.left) co_await enter(*node.left, *frame.values, copy.left);
            break;
        case Phase::Right:
            frame.phase = Phase::Third;
            if (node.right) co_await enter(*node.right, *frame.values, copy.right);
            break;
        case Phase::Third:
            frame.phase = Phase::Arguments;
            if (node.third) co_await enter(*node.third, *frame.values, copy.third);
            break;
        case Phase::Arguments:
            if (frame.index == node.arguments.size()) {
                frame.phase = Phase::GenericArguments;
                frame.index = 0;
            } else {
                const auto index = frame.index++;
                copy.arguments.emplace_back();
                co_await enter(*node.arguments[index], *frame.values, copy.arguments.back());
            }
            break;
        case Phase::GenericArguments:
            if (frame.index == node.generic_arguments.size()) {
                frame.phase = Phase::Initializers;
                frame.index = 0;
            } else {
                const auto& argument = node.generic_arguments[frame.index++];
                copy.generic_arguments.emplace_back();
                auto& target = copy.generic_arguments.back();
                if (argument.type) target.type = co_await clone_type_async(argument.type, types, *frame.values);
                if (argument.value) co_await enter(*argument.value, *frame.values, target.value);
            }
            break;
        case Phase::Initializers:
            if (frame.index == node.initializer_entries.size()) {
                pending.pop_back();
                break;
            }
            const auto& entry = node.initializer_entries[frame.index];
            if (copy.initializer_entries.size() == frame.index) {
                copy.initializer_entries.emplace_back();
                copy.initializer_entries.back().location = entry.location;
            }
            auto& target = copy.initializer_entries.back();
            if (frame.designator == entry.designators.size()) {
                ++frame.index;
                frame.designator = 0;
                if (entry.value) co_await enter(*entry.value, *frame.values, target.value);
            } else {
                const auto& designator = entry.designators[frame.designator++];
                target.designators.emplace_back();
                auto& copied = target.designators.back();
                copied.kind = designator.kind;
                copied.location = designator.location;
                copied.member = designator.member;
                copied.member_fresh = designator.member_fresh;
                if (designator.index) co_await enter(*designator.index, *frame.values, copied.index);
            }
            break;
        }
    }
    co_return result;
}

// Pure source-owner copying has one host pump per synchronous root. Mixed
// type/bound-expression/annotation edges await the task variants above rather
// than nesting these entry wrappers. No evaluator/provider or ABI query runs.
TypePtr clone_type(const TypePtr& source,
    const TypeSubstitutions& substitutions = {}, const ValueSubstitutions& values = {}) {
    return clone_type_async(source, substitutions, values).run();
}

std::unique_ptr<Expr> clone_expr(const Expr& source,
    const TypeSubstitutions& types = {}, const ValueSubstitutions& values = {}) {
    return clone_expr_async(source, types, values).run();
}

std::vector<Attribute> clone_attributes(
    const std::vector<Attribute>& source, const TypeSubstitutions& types,
    const ValueSubstitutions& values) {
    auto result = source;
    for (auto& attribute : result) {
        for (auto& binding : attribute.variadic_bindings)
            binding.type = clone_type(binding.type, types, values);
        if (attribute.expression_argument) {
            attribute.expression_argument = std::shared_ptr<Expr>(
                clone_expr(*attribute.expression_argument, types, values));
        }
    }
    return result;
}

RecordDecl instantiate_record(const RecordDecl& source, const TypeSubstitutions& types,
                              const ValueSubstitutions& values) {
    RecordDecl copy;
    copy.location = source.location;
    copy.name = source.name;
    copy.is_union = source.is_union;
    copy.complete = source.complete;
    copy.nominal_identity = types.nominal->substitute(source.nominal_identity);
    copy.attributes = clone_attributes(source.attributes, types, values);
    for (const auto& member : source.members)
        copy.members.push_back({member.location, member.name, clone_type(member.type, types, values),
            member.bit_width ? clone_expr(*member.bit_width, types, values) : nullptr,
            clone_attributes(member.attributes, types, values), member.fresh});
    return copy;
}

EnumDecl instantiate_enumeration(const EnumDecl& source, const TypeSubstitutions& types,
                                 const ValueSubstitutions& values) {
    EnumDecl copy;
    copy.location = source.location;
    copy.name = source.name;
    copy.local = source.local;
    copy.underlying = source.underlying;
    copy.captured_type_errors = source.captured_type_errors;
    copy.nominal_identity = types.nominal->substitute(source.nominal_identity);
    copy.attributes = clone_attributes(source.attributes, types, values);
    for (const auto& enumerator : source.enumerators) {
        auto binding = enumerator.binding;
        binding.enumeration = types.nominal->substitute(binding.enumeration);
        copy.enumerators.push_back({enumerator.location, enumerator.name,
            enumerator.initializer ? clone_expr(*enumerator.initializer, types, values) : nullptr,
            {}, std::move(binding)});
    }
    return copy;
}

// A generic instance's constructed names denote the instance's block-scope
// types: substitute into the template's typedef and tag bindings and into the
// definitions they carry.
void instantiate_token_bindings(TokenSequence& tokens, const TypeSubstitutions& types,
                                const ValueSubstitutions& values) {
    std::unordered_map<const CarriedDefinitions*, std::shared_ptr<const CarriedDefinitions>> instances;
    const auto carried = [&](const std::shared_ptr<const CarriedDefinitions>& source)
        -> std::shared_ptr<const CarriedDefinitions> {
        if (!source) return {};
        auto& instance = instances[source.get()];
        if (!instance) {
            auto result = std::make_shared<CarriedDefinitions>();
            for (const auto& record : source->records)
                result->records.push_back(
                    std::make_shared<const RecordDecl>(instantiate_record(*record, types, values)));
            for (const auto& enumeration : source->enumerations)
                result->enumerations.push_back(
                    std::make_shared<const EnumDecl>(instantiate_enumeration(*enumeration, types, values)));
            instance = std::move(result);
        }
        return instance;
    };
    for (auto& token : tokens) {
        if (const auto alias = token.origin.alias_binding) {
            auto binding = std::make_shared<AliasBinding>(*alias);
            binding->definition = std::make_shared<const AliasDefinition>(
                clone_type(alias->definition->instantiate(), types, values), alias->definition->storage());
            binding->carried = carried(alias->carried);
            token.origin.alias_binding = std::move(binding);
        }
        if (const auto tag = token.origin.tag_binding) {
            auto binding = std::make_shared<TagBinding>(*tag);
            binding->type.identity = types.nominal->substitute(tag->type.identity);
            binding->carried = carried(tag->carried);
            token.origin.tag_binding = std::move(binding);
        }
    }
}

std::unique_ptr<VariableDecl> clone_variable(
    const VariableDecl& source, const TypeSubstitutions& types,
    const ValueSubstitutions& values) {
    auto result = std::make_unique<VariableDecl>();
    result->location = source.location;
    result->name = source.name;
    result->binding = source.binding;
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
    enum class Phase { Statements, Payload, First, Second, Done };
    struct Frame {
        const Statement* source;
        Statement* destination;
        Phase phase{Phase::Statements};
        std::size_t index{};
    };
    std::vector<Frame> pending;
    const auto enter = [&](const Statement& input, std::unique_ptr<Statement>& destination) {
        auto copy = std::make_unique<Statement>();
        copy->kind = input.kind;
        copy->location = input.location;
        copy->label_name = input.label_name;
        copy->label_location = input.label_location;
        copy->label_binding = input.label_binding;
        copy->label_fresh = input.label_fresh;
        copy->attributes = clone_attributes(input.attributes, types, values);
        copy->global_label = input.global_label;
        copy->assertion_message = input.assertion_message;
        destination = std::move(copy);
        pending.push_back({&input, destination.get()});
    };
    std::unique_ptr<Statement> result;
    enter(source, result);
    // This order is deliberately different from source validation: vector
    // children precede this node's declaration/expressions, then first/second.
    // Retain the old copier's nominal/value-substitution and diagnostic order.
    while (!pending.empty()) {
        auto& frame = pending.back();
        const auto& node = *frame.source;
        auto& copy = *frame.destination;
        switch (frame.phase) {
        case Phase::Statements:
            if (frame.index == node.statements.size()) frame.phase = Phase::Payload;
            else {
                const auto index = frame.index++;
                copy.statements.emplace_back();
                enter(*node.statements[index], copy.statements.back());
            }
            break;
        case Phase::Payload:
            if (node.declaration) copy.declaration = clone_variable(*node.declaration, types, values);
            if (node.expression) copy.expression = clone_expr(*node.expression, types, values);
            if (node.condition) copy.condition = clone_expr(*node.condition, types, values);
            for (const auto& increment : node.increments) copy.increments.push_back(clone_expr(*increment, types, values));
            frame.phase = Phase::First;
            break;
        case Phase::First:
            frame.phase = Phase::Second;
            if (node.first) enter(*node.first, copy.first);
            break;
        case Phase::Second:
            frame.phase = Phase::Done;
            if (node.second) enter(*node.second, copy.second);
            break;
        case Phase::Done:
            pending.pop_back();
            break;
        }
    }
    return result;
}

std::string namespace_prefix(std::string_view name) {
    const auto separator = name.rfind("::");
    return separator == std::string_view::npos
               ? std::string{}
               : std::string(name.substr(0, separator));
}

FunctionDecl* lexical_function(Program& program, const std::shared_ptr<const FunctionScopeIdentity>& scope) {
    if (!scope) return nullptr;
    for (const auto& function : program.functions)
        if (function->function_scope == scope) return function.get();
    for (const auto& function : program.evaluation_definitions)
        if (function->function_scope == scope) return function.get();
    for (const auto& function : program.expansion_definitions)
        if (function->function_scope == scope) return function.get();
    return nullptr;
}

FunctionDecl* object_lexical_function(Program& program, const ObjectDecl& object) {
    return lexical_function(program, object.lexical_function);
}

FunctionDecl* assertion_lexical_function(Program& program, const StaticAssertDecl& assertion) {
    return lexical_function(program, assertion.lexical_function);
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
        for (const auto* declarations : {&program.functions, &program.evaluation_definitions})
            for (const auto& function : *declarations)
                if (function->name == candidate && visible(*function)) return candidate;
        for (const auto& enumeration : program.enumerations)
            for (const auto& enumerator : enumeration.enumerators)
                if (!enumeration.local &&
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
        for (const auto* declarations : {&program.functions, &program.evaluation_definitions}) {
            for (const auto& candidate : *declarations) {
                if (candidate->name != qualified || !predicate(*candidate)) continue;
                if (candidate->linkage == Linkage::Static) {
                    if (unit.empty() || candidate->source_unit == unit)
                        return candidate.get();
                } else if (!shared) {
                    shared = candidate.get();
                }
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
            if (argument.value && argument.value->name_context &&
                argument.value->name_context->label_address) {
                const auto& address = *argument.value->name_context->label_address;
                if (!address.global_name.empty()) spelling = address.global_name;
                else {
                    const auto& owner = *address.owner;
                    const auto& unit = owner.linkage == Linkage::Static ? owner.source_unit : std::string{};
                    spelling = "label<" + std::to_string(unit.size()) + ':' + unit + ':' +
                        std::to_string(owner.name.size()) + ':' + owner.name + ':' +
                        std::to_string(address.ordinal) + '>';
                }
            }
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

struct GenericArgumentKey {
    enum class Kind { Type, Integer, Address, Label } kind{Kind::Type};
    TypePtr type;
    UInt128 integer;
    AddressConstant address;
    LabelAddressConstant label;

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
                if (argument.value->name_context && argument.value->name_context->label_address)
                    key.label = *argument.value->name_context->label_address;
            }
        }
        result.push_back(std::move(key));
    }
    return result;
}

struct GenericExpansionState {
    enum class RecordPreparation { Preparing, Prepared, Failed };
    struct Instance {
        const FunctionDecl* generic{};
        std::vector<GenericArgumentKey> arguments;
        std::string name;
    };
    std::vector<Instance> instances;
    struct PendingInterface {
        const FunctionDecl* left{};
        const FunctionDecl* right{};
    };
    std::vector<PendingInterface> pending_interfaces;
    std::size_t validated_declarations{};
    std::unordered_set<const FunctionDecl*> rewritten_functions;
    unsigned depth{};
    GenericPointerResolver pointer_resolver;
    GenericAbiCanonicalizer canonical_abi;
    std::vector<NameKey> locals;
    std::vector<std::pair<NameKey, TypePtr>> local_types;
    std::unordered_map<NominalTypeKey, RecordPreparation, NominalTypeKeyHash> required_records;
    std::unordered_map<NominalTypeKey, RecordPreparation, NominalTypeKeyHash> required_record_alignments;
    // Shared by every record preparation of this expansion.
    RecordSourceProofs record_proofs;
    // Resolve nominal operators before a required expression can execute or
    // supply a generic argument. The resulting direct call follows the same
    // demand-driven preparation path as a written helper call.
    std::function<void(std::unique_ptr<Expr>&, const FunctionDecl*)> rewrite_operator;
    bool expansion_evaluation{};
    const FunctionDecl* invocation_owner{};
    std::optional<std::uint64_t> resource_epoch;
    bool resource_failed(const Program& program) const {
        return resource_epoch && *resource_epoch != program.evaluation_resource_errors;
    }
};

struct GenericResourceFrame {
    GenericExpansionState& state;
    std::optional<std::uint64_t> previous;
    GenericResourceFrame(GenericExpansionState& expansion, const Program& program)
        : state(expansion), previous(expansion.resource_epoch) {
        if (!previous) state.resource_epoch = program.evaluation_resource_errors;
    }
    ~GenericResourceFrame() { state.resource_epoch = previous; }
};

bool install_operator_bindings(Program& program, Diagnostics& diagnostics,
                               GenericExpansionState& state);

bool normalize_generic_callable_abis(TypePtr& type,
                                     const GenericExpansionState& state,
                                     Diagnostics& diagnostics,
                                     SourceLocation location) {
    std::string unknown;
    if (canonicalize_callable_abis(type, state.canonical_abi, &unknown)) return true;
    diagnostics.error(location, "unknown callable ABI '" + unknown + "'");
    return false;
}

// A generic instance's mangling input is the instance's own signature:
// arguments replace the generic parameters and callable types name their
// canonical ABI, so the names of type parameters never reach link names.
std::optional<std::string> generic_link_name(
    std::string_view name, const FunctionDecl& instance,
    const std::vector<Expr::GenericArgument>& arguments,
    const GenericExpansionState& state, Diagnostics& diagnostics,
    std::string_view mangling) {
    const auto spelling = [&](const TypePtr& source) -> std::optional<std::string> {
        auto type = clone_type(source);
        if (!normalize_generic_callable_abis(type, state, diagnostics, instance.location))
            return std::nullopt;
        return canonical_type_name(type);
    };
    std::vector<ManglingParameter> parameters;
    parameters.reserve(instance.parameters.size());
    for (const auto& parameter : instance.parameters) {
        auto text = spelling(callable_parameter_type(parameter.type, parameter.mode));
        if (!text) return std::nullopt;
        parameters.push_back({.spelling = std::move(*text),
                              .mode = std::string(parameter_mode_name(parameter.mode))});
    }
    const auto result = spelling(callable_result_type(instance.return_type));
    if (!result) return std::nullopt;
    return encode_model_generic_link_name(
        {.qualified_name = name,
         .kind = "function",
         .result = *result,
         .parameters = parameters,
         .variadic = instance.variadic},
        generic_argument_descriptors(arguments), mangling);
}

TypePtr source_function_type(const FunctionDecl& function,
                             const TypeSubstitutions& type_names = {},
                             const ValueSubstitutions& values = {}) {
    auto parameters = function.parameters;
    for (auto& parameter : parameters) {
        parameter.type = clone_type(parameter.type, type_names, values);
        parameter.declared_array_type = clone_type(parameter.declared_array_type, type_names, values);
    }
    auto type = function_type(clone_type(function.return_type, type_names, values),
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

// A record or enumeration defined by a generic function header.
struct HeaderDefinition {
    NominalTypeKey key;
    const RecordDecl* record{};
    const EnumDecl* enumeration{};

    [[nodiscard]] SourceLocation location() const { return record ? record->location : enumeration->location; }
    [[nodiscard]] std::string description() const {
        const std::string keyword = enumeration ? "enum" : record->is_union ? "union" : "struct";
        const auto& name = record ? record->name : enumeration->name;
        return name.empty() ? "an anonymous " + keyword : "'" + keyword + " " + name + "'";
    }
};

std::vector<HeaderDefinition> header_definitions(const Program& program, const FunctionDecl& function) {
    std::vector<HeaderDefinition> result;
    for (const auto& key : function.header_types) {
        HeaderDefinition definition{key};
        for (const auto& record : program.records)
            if (record.complete && record.nominal_key() == key) definition.record = &record;
        for (const auto& enumeration : program.enumerations)
            if (enumeration.nominal_key() == key) definition.enumeration = &enumeration;
        if (definition.record || definition.enumeration) result.push_back(definition);
    }
    return result;
}

// Written-form equality of two cloned header expressions. Generic parameters
// were replaced by position, so the remaining names compare by spelling.
bool same_written_expression(const Expr* left, const Expr* right) {
    if (!left || !right) return left == right;
    if (left->kind != right->kind || left->text != right->text ||
        left->string_value != right->string_value ||
        static_cast<bool>(left->type) != static_cast<bool>(right->type) ||
        (left->type && compare_generic_types(left->type, right->type) == TypeComparison::Different) ||
        !same_written_expression(left->left.get(), right->left.get()) ||
        !same_written_expression(left->right.get(), right->right.get()) ||
        !same_written_expression(left->third.get(), right->third.get()) ||
        left->arguments.size() != right->arguments.size() ||
        left->generic_arguments.size() != right->generic_arguments.size() ||
        left->initializer_entries.size() != right->initializer_entries.size()) return false;
    for (std::size_t index = 0; index < left->arguments.size(); ++index)
        if (!same_written_expression(left->arguments[index].get(), right->arguments[index].get())) return false;
    for (std::size_t index = 0; index < left->generic_arguments.size(); ++index) {
        const auto& a = left->generic_arguments[index];
        const auto& b = right->generic_arguments[index];
        if (static_cast<bool>(a.type) != static_cast<bool>(b.type) ||
            (a.type && compare_generic_types(a.type, b.type) == TypeComparison::Different) ||
            !same_written_expression(a.value.get(), b.value.get())) return false;
    }
    for (std::size_t index = 0; index < left->initializer_entries.size(); ++index) {
        const auto& a = left->initializer_entries[index];
        const auto& b = right->initializer_entries[index];
        if (a.designators.size() != b.designators.size() ||
            !same_written_expression(a.value.get(), b.value.get())) return false;
        for (std::size_t at = 0; at < a.designators.size(); ++at)
            if (a.designators[at].kind != b.designators[at].kind ||
                !(a.designators[at].member_name() == b.designators[at].member_name()) ||
                !same_written_expression(a.designators[at].index.get(), b.designators[at].index.get()))
                return false;
    }
    return true;
}

// A redeclaration repeats each definition of the earlier declaration's generic
// header. Map the earlier definitions onto the later ones by position, so both
// declarations denote the same per-instance types, and report the first difference.
bool map_header_definitions(const Program& program, const FunctionDecl& left, const FunctionDecl& right,
    TypeSubstitutions& left_names, const ValueSubstitutions& left_values,
    const TypeSubstitutions& right_names, const ValueSubstitutions& right_values,
    Diagnostics& diagnostics, bool& pending) {
    const auto earlier = header_definitions(program, left);
    const auto later = header_definitions(program, right);
    if (earlier.empty() && later.empty()) return true;
    const auto function = " generic function '" + right.name + "'";
    const auto differ = [&](SourceLocation location, const std::string& message,
                            SourceLocation previous, const std::string& note) {
        diagnostics.error(location, message);
        diagnostics.note(previous, note);
        return false;
    };
    for (std::size_t index = 0; index < std::max(earlier.size(), later.size()); ++index) {
        if (index == earlier.size())
            return differ(later[index].location(), "redeclaration of" + function + " defines " +
                later[index].description() + ", which its earlier declaration does not define",
                left.location, "earlier declaration is here");
        if (index == later.size())
            return differ(right.location, "redeclaration of" + function + " does not repeat the definition of " +
                earlier[index].description(), earlier[index].location(), "earlier definition is here");
        const auto& a = earlier[index];
        const auto& b = later[index];
        if (a.description() != b.description())
            return differ(b.location(), "redeclaration of" + function + " defines " + b.description() +
                " where its earlier declaration defines " + a.description(), a.location(), "earlier definition is here");
    }
    left_names.nominal = std::make_shared<NominalInstantiation>();
    left_names.nominal->owner = left.generic_tag_owner;
    for (std::size_t index = 0; index < earlier.size(); ++index)
        left_names.nominal->identities.emplace(earlier[index].key.identity.get(), later[index].key.identity);
    const auto same_expression = [&](const Expr* a, const Expr* b) {
        if (!a || !b) return a == b;
        return same_written_expression(clone_expr(*a, left_names, left_values).get(),
                                       clone_expr(*b, right_names, right_values).get());
    };
    const auto same_attributes = [&](const std::vector<Attribute>& a, const std::vector<Attribute>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t index = 0; index < a.size(); ++index) {
            if (a[index].name != b[index].name || a[index].arguments.size() != b[index].arguments.size() ||
                !same_expression(a[index].expression_argument.get(), b[index].expression_argument.get()))
                return false;
            for (std::size_t at = a[index].expression_argument ? 1 : 0; at < a[index].arguments.size(); ++at)
                if (a[index].arguments[at] != b[index].arguments[at]) return false;
        }
        return true;
    };
    for (std::size_t index = 0; index < earlier.size(); ++index) {
        const auto& a = earlier[index];
        const auto& b = later[index];
        const auto subject = b.description();
        const auto previous = " than in the earlier declaration of" + function;
        if (!same_attributes(a.record ? a.record->attributes : a.enumeration->attributes,
                             b.record ? b.record->attributes : b.enumeration->attributes) ||
            (a.enumeration && a.enumeration->underlying != b.enumeration->underlying))
            return differ(b.location(), subject + " has different attributes" + previous,
                          a.location(), "earlier definition is here");
        const auto element = [&](std::size_t at) {
            return a.record ? std::pair{a.record->members[at].name, a.record->members[at].location}
                            : std::pair{a.enumeration->enumerators[at].name, a.enumeration->enumerators[at].location};
        };
        const auto later_element = [&](std::size_t at) {
            return b.record ? std::pair{b.record->members[at].name, b.record->members[at].location}
                            : std::pair{b.enumeration->enumerators[at].name, b.enumeration->enumerators[at].location};
        };
        const std::string kind = a.record ? "member '" : "enumerator '";
        const auto earlier_count = a.record ? a.record->members.size() : a.enumeration->enumerators.size();
        const auto later_count = b.record ? b.record->members.size() : b.enumeration->enumerators.size();
        for (std::size_t at = 0; at < std::max(earlier_count, later_count); ++at) {
            if (at == earlier_count)
                return differ(later_element(at).second, kind + later_element(at).first + "' of " + subject +
                    " is not in the earlier declaration of" + function, a.location(), "earlier definition is here");
            if (at == later_count)
                return differ(b.location(), subject + " lacks " + kind + element(at).first +
                    "' of the earlier declaration of" + function, element(at).second, "earlier " + kind +
                    element(at).first + "' is here");
            const auto [name, location] = later_element(at);
            const auto note = "earlier " + kind + element(at).first + "' is here";
            const bool same_name = a.record
                ? a.record->members[at].member_name() == b.record->members[at].member_name()
                : a.enumeration->enumerators[at].name == b.enumeration->enumerators[at].name;
            if (!same_name)
                return differ(location, kind + name + "' of " + subject + " does not match " + kind +
                    element(at).first + "' of the earlier declaration of" + function, element(at).second, note);
            if (a.enumeration) {
                if (!same_expression(a.enumeration->enumerators[at].initializer.get(),
                                     b.enumeration->enumerators[at].initializer.get()))
                    return differ(location, kind + name + "' of " + subject + " has a different value" + previous,
                                  element(at).second, note);
                continue;
            }
            const auto& earlier_member = a.record->members[at];
            const auto& later_member = b.record->members[at];
            const auto comparison = compare_generic_types(clone_type(earlier_member.type, left_names, left_values),
                                                          clone_type(later_member.type, right_names, right_values));
            if (comparison == TypeComparison::Different)
                return differ(location, kind + name + "' of " + subject + " has a different type" + previous,
                              element(at).second, note);
            pending = pending || comparison == TypeComparison::DeferredBound;
            if (!same_expression(earlier_member.bit_width.get(), later_member.bit_width.get()))
                return differ(location, kind + name + "' of " + subject + " has a different bit-field width" +
                              previous, element(at).second, note);
            if (!same_attributes(earlier_member.attributes, later_member.attributes))
                return differ(location, kind + name + "' of " + subject + " has different attributes" + previous,
                              element(at).second, note);
        }
    }
    return true;
}

// Reports its own error, so a caller only records a deferred comparison.
TypeComparison compatible_generic_declarations(const Program& program,
    const FunctionDecl& left, const FunctionDecl& right,
    const GenericExpansionState& state, Diagnostics& diagnostics) {
    const auto incompatible = [&] {
        diagnostics.error(right.location, "generic declarations of '" + right.name +
            "' have incompatible interfaces");
        return TypeComparison::Different;
    };
    if (left.linkage != right.linkage ||
        left.generic_parameters.size() != right.generic_parameters.size())
        return incompatible();
    bool pending{};
    TypeSubstitutions left_names;
    TypeSubstitutions right_names;
    ValueSubstitutions left_values;
    ValueSubstitutions right_values;
    std::vector<std::unique_ptr<Expr>> value_placeholders;
    for (std::size_t index = 0; index < left.generic_parameters.size();
         ++index) {
        const auto& a = left.generic_parameters[index];
        const auto& b = right.generic_parameters[index];
        if (static_cast<bool>(a.value_type) !=
            static_cast<bool>(b.value_type)) return incompatible();
        if (!a.value_type) {
            const auto placeholder = generic_type(
                "__generic_parameter_" + std::to_string(index));
            left_names.emplace(name_key(a), placeholder);
            right_names.emplace(name_key(b), placeholder);
        } else {
            auto placeholder = std::make_unique<Expr>();
            placeholder->kind = Expr::Kind::Name;
            placeholder->text = "__generic_value_" + std::to_string(index);
            left_values.emplace(name_key(a), placeholder.get());
            right_values.emplace(name_key(b), placeholder.get());
            value_placeholders.push_back(std::move(placeholder));
        }
    }
    if (!map_header_definitions(program, left, right, left_names, left_values,
                                right_names, right_values, diagnostics, pending))
        return TypeComparison::Different;
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
                                             b.location)) return incompatible();
        const auto comparison = compare_generic_types(a_type, b_type);
        if (comparison == TypeComparison::Different) return incompatible();
        pending = pending || comparison == TypeComparison::DeferredBound;
    }
    auto a_type = source_function_type(left, left_names);
    auto b_type = source_function_type(right, right_names);
    if (!normalize_generic_callable_abis(a_type, state, diagnostics, left.location) ||
        !normalize_generic_callable_abis(b_type, state, diagnostics, right.location)) return incompatible();
    const auto result = compare_generic_types(a_type, b_type);
    if (result == TypeComparison::Different) return incompatible();
    return pending || result == TypeComparison::DeferredBound ? TypeComparison::DeferredBound : TypeComparison::Same;
}

bool validate_generic_redeclarations(const Program& program,
                                     GenericExpansionState& state,
                                     Diagnostics& diagnostics) {
    const auto errors = diagnostics.errors();
    for (std::size_t left = 0; left < program.functions.size(); ++left) {
        const auto& a = *program.functions[left];
        if (a.generic_parameters.empty()) continue;
        for (std::size_t right = std::max(left + 1, state.validated_declarations);
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
            } else {
                if (compatible_generic_declarations(program, a, b, state, diagnostics) ==
                    TypeComparison::DeferredBound)
                    state.pending_interfaces.push_back({&a, &b});
            }
        }
    }
    state.validated_declarations = program.functions.size();
    return diagnostics.errors() == errors;
}

template <typename TypeOf>
TypePtr translation_intrinsic_type(const Expr& expression, bool procedural, TypeOf&& type_of) {
    if (expression.kind != Expr::Kind::Call || !expression.left ||
        expression.left->kind != Expr::Kind::Name) return {};
    switch (control_intrinsic(expression)) {
    case ControlIntrinsic::Expect:
        return expression.arguments.empty() ? TypePtr{} : type_of(*expression.arguments.front());
    case ControlIntrinsic::Assume: case ControlIntrinsic::Unreachable: case ControlIntrinsic::Trap:
        return builtin_type(BuiltinType::Void);
    case ControlIntrinsic::None: break;
    }
    switch (atomic_builtin(expression)) {
    case AtomicBuiltin::Store: case AtomicBuiltin::ThreadFence: case AtomicBuiltin::SignalFence:
        return builtin_type(BuiltinType::Void);
    case AtomicBuiltin::CompareExchange: case AtomicBuiltin::IsLockFree:
        return builtin_type(BuiltinType::Bool);
    case AtomicBuiltin::None: break;
    default: {
        const auto pointer = expression.arguments.empty() ? TypePtr{} : type_of(*expression.arguments.front());
        return pointer && pointer->kind == Type::Kind::Pointer ? atomic_value_type(pointer->pointee) : TypePtr{};
    }
    }
    if (procedural && (expression.left->text == "$::syntax::at" ||
        expression.left->text == "$::meta::extension_match")) return syntax_match_type();
    if (procedural && expression.left->text == "$::syntax::node") return syntax_type();
    if (procedural && expression.left->text == "$::syntax::context") return context_type();
    if (procedural && (expression.left->text == "$::syntax::span" ||
        expression.left->text == "$::syntax::capture_span" ||
        expression.left->text == "$::meta::span" ||
        expression.left->text == "$::meta::node_span")) return span_type();
    if (procedural && (expression.left->text == "$::syntax::error" ||
        expression.left->text == "$::syntax::warning" ||
        expression.left->text == "$::syntax::note" ||
        expression.left->text == "$::meta::error" || expression.left->text == "$::meta::warning" ||
        expression.left->text == "$::meta::note")) return builtin_type(BuiltinType::Void);
    if (procedural && expression.left->text == "$::meta::spelling") return bytes_type();
    if (procedural && expression.left->text == "$::meta::children") return tokens_type();
    if (procedural && (expression.left->text == "$::meta::token" ||
        expression.left->text == "$::meta::group")) return tokens_type();
    if (procedural && expression.left->text == "$::meta::delimiter")
        return pointer_type(builtin_type(BuiltinType::U8, true));
    if (procedural && expression.left->text == "$::syntax::count") return builtin_type(BuiltinType::Uptr);
    if (procedural && expression.left->text == "$::syntax::is_variant")
        return builtin_type(BuiltinType::Bool);
    if (procedural && (expression.left->text == "$::syntax::input" ||
        expression.left->text == "$::syntax::capture")) return tokens_type();
    if (procedural && expression.left->text == "$::meta::tokens") return tokens_type();
    if (procedural && expression.left->text == "$::meta::call_site") return tokens_type();
    if (procedural && expression.left->text == "$::meta::gensym") return tokens_type();
    if (procedural && (expression.left->text == "$::meta::child" ||
        expression.left->text == "$::meta::replace_child")) return syntax_type();
    if (procedural && expression.left->text == "$::meta::child_count")
        return builtin_type(BuiltinType::Uptr);
    if (procedural && (expression.left->text == "$::meta::is_kind" ||
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
        const auto sequence = type_of(*expression.arguments.front());
        if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                          sequence->kind != Type::Kind::Buffer)) return {};
        return pointer_type(builtin_type(BuiltinType::U8,
            sequence->kind == Type::Kind::Bytes));
    }
    if (expression.left->text == "$::meta::at" ||
        expression.left->text == "$::meta::slice" ||
        expression.left->text == "$::meta::concat") {
        if (expression.arguments.empty()) return {};
        const auto sequence = type_of(*expression.arguments.front());
        if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                          sequence->kind != Type::Kind::Tokens)) return {};
        return expression.left->text == "$::meta::at" &&
               sequence->kind == Type::Kind::Bytes
            ? builtin_type(BuiltinType::U8) : sequence;
    }
    if (procedural && expression.left->text == "$::meta::parse")
        return expression.arguments.size() == 3U ? syntax_type() : tokens_type();
    if ((expression.left->text == "$::eval" || expression.left->text == "$::runtime") &&
        expression.arguments.size() == 1) return type_of(*expression.arguments.front());
    if (patch_intrinsic(expression) && !expression.arguments.empty() && expression.arguments.size() <= 2)
        return type_of(*expression.arguments.front());
    return {};
}

std::optional<TokenKind> meta_lexical_kind(std::string_view name) {
    static constexpr std::pair<std::string_view, TokenKind> kinds[] = {
        {"identifier", TokenKind::Identifier}, {"builtin", TokenKind::BuiltinName},
        {"integer", TokenKind::Integer}, {"floating", TokenKind::Floating},
        {"character", TokenKind::Character}, {"string", TokenKind::String},
        {"punctuation", TokenKind::Punctuator}, {"splice", TokenKind::StructuredSplice}};
    const auto found = std::find_if(std::begin(kinds), std::end(kinds),
        [&](const auto& entry) { return entry.first == name; });
    return found == std::end(kinds) ? std::nullopt : std::optional(found->second);
}

TypePtr conditional_meta_type(const TypePtr& left, const TypePtr& right) {
    if (!is_meta_type(left) || !is_meta_type(right)) return {};
    // A conditional selects a value, not a cell. Top-level const therefore
    // does not distinguish its alternatives, but opaque meta kinds and every
    // other qualifier remain distinct. No runtime representation is invented.
    const auto a = callable_parameter_type(left, ParameterMode::In);
    const auto b = callable_parameter_type(right, ParameterMode::In);
    return same_type(a, b) ? a : TypePtr{};
}

// Type selection does not execute a zero proof. The shared source-operator
// checker validates a pointer/integer arm before evaluation or erasure.
TypePtr conditional_pointer_type(const TypePtr& left, const TypePtr& right) {
    if (!left || !right) return {};
    if (left->kind == Type::Kind::Pointer && right->kind == Type::Kind::Pointer)
        return common_pointer_type(left, right).type.value_or(TypePtr{});
    const auto pointer = left->kind == Type::Kind::Pointer && is_integer(right) ? left
        : right->kind == Type::Kind::Pointer && is_integer(left) ? right : TypePtr{};
    if (!pointer) return {};
    auto result = std::make_shared<Type>(*pointer);
    result->is_const = result->is_volatile = result->is_atomic = result->is_restrict = false;
    return result;
}

TypePtr infer_generic_actual(const Expr& expression,
                             const FunctionDecl* caller,
                             Program& program,
                             const GenericExpansionState& state,
                             bool decay = true,
                             SourceTypeMemo* memo = nullptr);

bool match_deduced_type(const TypePtr& formal, const TypePtr& actual,
                        TypeSubstitutions& bindings,
                        const NameSet& type_parameters,
                        std::string& conflict, bool allow_deferred = false) {
    if (!formal || !actual) return false;
    if (formal->kind == Type::Kind::Generic &&
        type_parameters.contains(generic_type_key(*formal))) {
        const auto found = bindings.find(generic_type_key(*formal));
        if (found != bindings.end()) {
            if (allow_deferred ? compare_source_types(found->second, actual) == TypeComparison::Different
                               : !same_type(found->second, actual)) {
                conflict = formal->generic_name;
                return false;
            }
            return true;
        }
        bindings.emplace(generic_type_key(*formal), clone_type(actual));
        return true;
    }
    const auto has_unbound = [&](const auto& self, const TypePtr& type)
                                 -> bool {
        if (!type) return false;
        if (type->kind == Type::Kind::Generic)
            return type_parameters.contains(generic_type_key(*type));
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
                                  bindings, type_parameters, conflict, allow_deferred);
    if (formal->kind == Type::Kind::Array ||
        formal->kind == Type::Kind::Vector) {
        const auto pending_extent = [](const TypePtr& type) {
            return deferred_vector_extent(type) || (type->kind == Type::Kind::Array && !type->lanes &&
                type->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext);
        };
        return (formal->lanes == actual->lanes || (allow_deferred &&
                (pending_extent(formal) || pending_extent(actual)))) &&
               formal->scalable == actual->scalable &&
               match_deduced_type(formal->element, actual->element,
                                  bindings, type_parameters, conflict, allow_deferred);
    }
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
        if (!match_deduced_type(callable_result_type(left.result), callable_result_type(right.result),
                                bindings, type_parameters, conflict, allow_deferred)) return false;
        for (std::size_t index = 0; index < left.parameters.size(); ++index) {
            if (left.parameters[index].mode != right.parameters[index].mode ||
                left.parameters[index].location_name.value_or("auto") !=
                    right.parameters[index].location_name.value_or("auto") ||
                !match_deduced_type(
                    callable_parameter_type(left.parameters[index].type,
                                            left.parameters[index].mode),
                    callable_parameter_type(right.parameters[index].type,
                                            right.parameters[index].mode),
                    bindings, type_parameters, conflict, allow_deferred)) return false;
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
                              Diagnostics& diagnostics, bool* context_dependent = nullptr) {
    if (arguments.size() > generic.generic_parameters.size()) {
        diagnostics.error(call.location, "generic argument count does not match '" +
                                             generic.name + "'");
        return false;
    }
    NameSet type_parameters;
    TypeSubstitutions bindings;
    for (const auto& parameter : generic.generic_parameters)
        if (!parameter.value_type) type_parameters.insert(name_key(parameter));
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
            bindings.emplace(name_key(parameter), arguments[index].type);
            if (context_dependent && has_context_dependent_type_bound(arguments[index].type))
                *context_dependent = true;
            type_parameters.erase(name_key(parameter));
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
        if (has_pending_type_bound(actual)) {
            if (context_dependent && has_context_dependent_type_bound(actual)) {
                *context_dependent = true;
            } else {
                diagnostics.error(argument.location,
                    "generic deduction requires resolved fixed type bounds");
                return false;
            }
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
                                type_parameters, conflict, context_dependent != nullptr)) {
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
        const auto found = bindings.find(name_key(parameter));
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
EvaluationTask<void> prepare_generic_inferred_array_async(VariableDecl& declaration, const FunctionDecl& caller,
    Program& program, Diagnostics& diagnostics, const GenericExpansionState& state);

EvaluationTask<bool> normalize_generic_arguments_async(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location,
                                 GenericExpansionState& state, std::string_view mangling,
                                 bool* context_dependent = nullptr);

void rewrite_generic_expr(std::unique_ptr<Expr>& expression,
                          const FunctionDecl* caller, Program& program,
                          Diagnostics& diagnostics,
                          GenericExpansionState& state,
                          std::string_view mangling);

EvaluationTask<void> rewrite_generic_expr_async(std::unique_ptr<Expr>& expression,
    const FunctionDecl* caller, Program& program, Diagnostics& diagnostics,
    GenericExpansionState& state, std::string_view mangling);

EvaluationTask<void> normalize_automatic_array_bound_async(VariableDecl& declaration, FunctionDecl* caller,
    Program& program, Diagnostics& diagnostics,
    std::span<const std::pair<NameKey, TypePtr>> local_types);

const char* source_array_element_error(const TypePtr& type) {
    if (!type || type->kind != Type::Kind::Array) return nullptr;
    const auto& element = type->element;
    if (!element || element->kind == Type::Kind::Function)
        return "array element requires an object type";
    if (element->kind == Type::Kind::Builtin && element->builtin == BuiltinType::Void)
        return "array element type cannot be void";
    if (element->kind == Type::Kind::Vector && element->scalable)
        return "array element type cannot be a scalable vector";
    if (element->is_atomic) return "an array element type cannot be atomic-qualified";
    if (is_meta_type(element)) return "opaque meta values cannot appear inside runtime pointer, array, or function types";
    return nullptr;
}

EvaluationTask<void> rewrite_generic_type_bounds_async(TypePtr type, const FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling,
    SourceLocation location = {}) {
    GenericResourceFrame resource_frame(state, program);
    if (!type || state.resource_failed(program)) co_return;
    if (!location.valid() && caller) location = caller->location;
    if (const auto* reason = source_array_element_error(type)) {
        diagnostics.error(location, reason);
        co_return;
    }
    if (type->kind == Type::Kind::Vector)
        if (const auto* reason = vector_element_error(type->element)) {
            diagnostics.error(type->vector_bound ? type->vector_bound->location : location, reason);
            co_return;
        }
    co_await rewrite_generic_type_bounds_async(type->pointee, caller, program, diagnostics, state, mangling, location);
    co_await rewrite_generic_type_bounds_async(type->element, caller, program, diagnostics, state, mangling, location);
    if (state.resource_failed(program)) co_return;
    if (type->function) {
        // Prototype cells have exact parser identities, including nested
        // callbacks. They provide types for unevaluated operands, never values
        // for fixed-bound proofs or names in the surrounding function body.
        struct RestorePrototypeTypes {
            GenericExpansionState& state;
            std::size_t names, types;
            ~RestorePrototypeTypes() {
                state.locals.resize(names);
                state.local_types.resize(types);
            }
        } restore{state, state.locals.size(), state.local_types.size()};
        for (const auto& parameter : type->function->parameters) {
            state.locals.push_back(name_key(parameter));
            state.local_types.emplace_back(name_key(parameter), parameter.type);
        }
        // Source compatibility must compare model identities, not written ABI
        // aliases. This applies to every prepared type use, not just deduction.
        if (state.canonical_abi) {
            const auto abi = state.canonical_abi(type->function->abi);
            if (!abi) {
                diagnostics.error(location, "unknown callable ABI '" + type->function->abi + "'");
                co_return;
            }
            type->function->abi = *abi;
        }
        co_await rewrite_generic_type_bounds_async(type->function->result, caller, program, diagnostics, state, mangling, location);
        for (const auto& parameter : type->function->parameters) {
            co_await rewrite_generic_type_bounds_async(parameter.type, caller, program, diagnostics, state, mangling, parameter.location);
            co_await rewrite_generic_type_bounds_async(parameter.declared_array_type, caller, program, diagnostics, state, mangling, parameter.location);
        }
    }
    if (type->vector_bound && (type->lanes == 0 || type->element->kind == Type::Kind::Generic)) {
        auto expression = clone_expr(*type->vector_bound);
        co_await rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling);
        type->vector_bound = std::move(expression);
        if (state.resource_failed(program)) co_return;
        if (type->element->kind != Type::Kind::Generic)
            (void)(co_await resolve_vector_bound_async(program, type, diagnostics,
                program.evaluation_size_of, program.evaluation_align_of,
                caller ? caller->source_namespace : std::string_view{}, caller, state.local_types,
                caller && evaluation_only(*caller) ? EvaluationIntegerContext::StagedDefinition
                                                   : EvaluationIntegerContext::Definition));
    }
    if (!type->alignment_requests.empty()) {
        for (auto& request : type->alignment_requests) {
            auto expression = clone_expr(*request);
            co_await rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling);
            request = std::move(expression);
            if (state.resource_failed(program)) co_return;
        }
        (void)(co_await resolve_alignment_requests_async(program, type, diagnostics,
            program.evaluation_size_of, program.evaluation_align_of,
            caller ? caller->source_namespace : std::string_view{}, caller, state.local_types,
            caller && evaluation_only(*caller) ? EvaluationIntegerContext::StagedDefinition
                                               : EvaluationIntegerContext::Definition));
    }
    if (!type->array_bound || type->lanes != 0 || state.resource_failed(program)) co_return;
    auto expression = clone_expr(*type->array_bound);
    co_await rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling);
    type->array_bound = std::move(expression);
    if (state.resource_failed(program)) co_return;
    // Fixed extents are required source semantics, including unused aliases
    // and adjusted array parameters. Resolve them before deduction or erasure.
    const auto bound_expression = type->array_bound;
    const auto result = co_await evaluate_target_integer_requirement_async(program, *bound_expression, diagnostics,
        program.evaluation_size_of, program.evaluation_align_of,
        caller ? caller->source_namespace : std::string_view{}, caller, state.local_types,
        caller && evaluation_only(*caller) ? EvaluationIntegerContext::StagedDefinition
                                           : EvaluationIntegerContext::Definition);
    if (result.status == EvaluationIntegerResult::Status::ContextUnavailable) {
        type->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
    } else if (result.value) {
        if (const auto bound = fixed_array_bound_value(*result.value, program.address_bits)) {
            type->lanes = *bound;
            type->array_extent_dependency = Type::ArrayExtentDependency::None;
        } else diagnostics.error(bound_expression->location,
            "fixed array bound must be a positive integer representable in 32 bits");
    }
}

void rewrite_generic_type_bounds(const TypePtr& type, const FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling,
    SourceLocation location = {}) {
    rewrite_generic_type_bounds_async(type, caller, program, diagnostics, state, mangling, location).run();
}

void collect_function_types(const FunctionDecl& function, GenericExpansionState& state) {
    for (const auto& parameter : function.parameters) {
        state.locals.push_back(name_key(parameter));
        state.local_types.emplace_back(name_key(parameter), parameter.type);
    }
    for (const auto& attribute : function.attributes)
        for (const auto& binding : attribute.variadic_bindings) {
            state.locals.push_back(name_key(binding));
            state.local_types.emplace_back(name_key(binding), binding.type);
        }
    std::vector<const Statement*> pending;
    if (function.body) pending.push_back(function.body.get());
    while (!pending.empty()) {
        const auto& statement = *pending.back();
        pending.pop_back();
        if (statement.declaration) {
            state.locals.push_back(name_key(*statement.declaration));
            state.local_types.emplace_back(name_key(*statement.declaration), statement.declaration->type);
        }
        if (statement.second) pending.push_back(statement.second.get());
        if (statement.first) pending.push_back(statement.first.get());
        for (auto child = statement.statements.rbegin(); child != statement.statements.rend(); ++child)
            pending.push_back(child->get());
    }
}

EvaluationTask<void> prepare_generic_local_types_async(FunctionDecl& function, Program& program, Diagnostics& diagnostics,
    GenericExpansionState& state, std::string_view mangling, const RecordDecl* consumer = nullptr) {
    GenericResourceFrame resource_frame(state, program);
    if (state.resource_failed(program)) co_return;
    std::vector<VariableDecl*> declarations;
    NameMap<VariableDecl*> locals;
    std::vector<const Statement*> pending;
    if (function.body) pending.push_back(function.body.get());
    while (!pending.empty()) {
        const auto& statement = *pending.back();
        pending.pop_back();
        if (statement.declaration) {
            declarations.push_back(statement.declaration.get());
            locals.emplace(name_key(*statement.declaration), statement.declaration.get());
        }
        if (statement.second) pending.push_back(statement.second.get());
        for (auto child = statement.statements.rbegin(); child != statement.statements.rend(); ++child)
            pending.push_back(child->get());
        if (statement.first) pending.push_back(statement.first.get());
    }
    std::unordered_set<const VariableDecl*> prepared;
    std::unordered_set<const Type*> visited_types;
    std::function<EvaluationTask<void>(VariableDecl&)> prepare;
    std::function<EvaluationTask<void>(const Expr*)> expression;
    std::function<EvaluationTask<void>(TypePtr)> type;
    type = [&](TypePtr value) -> EvaluationTask<void> {
        if (!value || state.resource_failed(program) || !visited_types.insert(value.get()).second) co_return;
        const auto array_bound = value->array_bound;
        const auto vector_bound = value->vector_bound;
        const auto alignment_requests = value->alignment_requests;
        co_await expression(array_bound.get());
        co_await expression(vector_bound.get());
        for (const auto& request : alignment_requests) co_await expression(request.get());
        co_await type(value->element);
        co_await type(value->pointee);
        if (value->function) {
            co_await type(value->function->result);
            for (const auto& parameter : value->function->parameters) co_await type(parameter.type);
        }
    };
    expression = [&](const Expr* value) -> EvaluationTask<void> {
        if (!value || state.resource_failed(program)) co_return;
        if (value->kind == Expr::Kind::Name) {
            if (const auto found = locals.find(name_key(*value)); found != locals.end()) co_await prepare(*found->second);
            else {
                TypePtr binding;
                for (const auto& [name, candidate] : state.local_types)
                    if (name == name_key(*value)) { binding = candidate; break; }
                if (binding) {
                    co_await type(binding);
                    co_await rewrite_generic_type_bounds_async(binding, &function, program, diagnostics, state, mangling, value->location);
                }
            }
        }
        co_await type(value->type);
        co_await expression(value->left.get());
        co_await expression(value->right.get());
        co_await expression(value->third.get());
        for (const auto& argument : value->arguments) co_await expression(argument.get());
        for (const auto& argument : value->generic_arguments) {
            co_await type(argument.type);
            co_await expression(argument.value.get());
        }
    };
    std::function<EvaluationTask<void>(Expr&, bool)> indices;
    indices = [&](Expr& source, bool rewrite) -> EvaluationTask<void> {
        if (state.resource_failed(program)) co_return;
        for (auto& entry : source.initializer_entries) {
            if (state.resource_failed(program)) co_return;
            for (auto& designator : entry.designators) {
                if (rewrite) co_await rewrite_generic_expr_async(designator.index, &function, program, diagnostics, state, mangling);
                else co_await expression(designator.index.get());
            }
            if (entry.value && entry.value->kind == Expr::Kind::AggregateInitializer)
                co_await indices(*entry.value, rewrite);
        }
    };
    prepare = [&](VariableDecl& declaration) -> EvaluationTask<void> {
        if (state.resource_failed(program) || !prepared.insert(&declaration).second) co_return;
        co_await type(declaration.type);
        const auto value = declaration.type;
        const bool static_bytes = declaration.storage_static && declaration.initializer &&
            declaration.initializer->kind != Expr::Kind::AggregateInitializer &&
            value && value->kind == Type::Kind::Array && value->element &&
            value->element->kind == Type::Kind::Builtin && value->element->builtin == BuiltinType::U8;
        if (static_bytes) co_await expression(declaration.initializer.get());
        if (declaration.initializer) co_await indices(*declaration.initializer, false);
        co_await rewrite_generic_type_bounds_async(declaration.type, &function, program, diagnostics, state, mangling,
            declaration.location);
        if (state.resource_failed(program)) co_return;
        if ((evaluation_only(function) || declaration.storage_static) && value && value->kind == Type::Kind::Array &&
            !value->lanes && !value->array_bound && !declaration.dynamic_array_bound && declaration.initializer) {
            co_await indices(*declaration.initializer, true);
            if (static_bytes)
                co_await rewrite_generic_expr_async(declaration.initializer, &function, program, diagnostics, state, mangling);
            co_await prepare_generic_inferred_array_async(declaration, function, program, diagnostics, state);
        }
    };
    if (!consumer) {
        for (auto* declaration : declarations) co_await prepare(*declaration);
    } else {
        // A record needs only its actual lexical dependencies, not a later
        // local whose initializer may itself query this record. Snapshot roots
        // before generic publication can grow the declaration tables.
        std::vector<TypePtr> types;
        std::vector<std::shared_ptr<const Expr>> expressions;
        for (const auto& attribute : consumer->attributes) expressions.push_back(attribute.expression_argument);
        for (const auto& member : consumer->members) {
            types.push_back(member.type);
            if (member.bit_width) expressions.push_back(std::shared_ptr<const Expr>(clone_expr(*member.bit_width)));
            for (const auto& attribute : member.attributes) expressions.push_back(attribute.expression_argument);
        }
        for (const auto& root : types) co_await type(root);
        for (const auto& root : expressions) co_await expression(root.get());
    }
}

EvaluationTask<bool> prepare_generic_record_async(NominalTypeKey key, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling,
    EvaluationLayoutKind kind = EvaluationLayoutKind::Complete) {
    GenericResourceFrame resource_frame(state, program);
    if (state.resource_failed(program)) co_return false;
    if (key.identity && key.identity->generic_owner) co_return true;
    auto& prepared_records = kind == EvaluationLayoutKind::Alignment
        ? state.required_record_alignments : state.required_records;
    if (const auto found = prepared_records.find(key); found != prepared_records.end())
        co_return found->second != GenericExpansionState::RecordPreparation::Failed;
    const auto* definition = program.record_index.definition(program, key);
    if (!definition) co_return true;
    const auto index = static_cast<std::size_t>(definition - program.records.data());
    if (const auto error = record_source_error(*definition, program, program.record_index, &state.record_proofs)) {
        diagnostics.error(error->location, error->message);
        prepared_records[key] = GenericExpansionState::RecordPreparation::Failed;
        co_return false;
    }
    prepared_records.emplace(key, GenericExpansionState::RecordPreparation::Preparing);
    const auto errors = diagnostics.errors();
    FunctionDecl context;
    context.source_namespace = namespace_prefix(program.records[index].name);
    const auto location = program.records[index].location;
    if (location.file) context.source_unit = location.file->source_unit_at(location.line);
    // A reached record's expressions carry their lexical bindings. Do not let
    // the function that happened to request its layout shadow those bindings.
    auto saved_locals = std::move(state.locals);
    auto saved_local_types = std::move(state.local_types);
    struct RestoreLocalTypes {
        GenericExpansionState& state;
        decltype(GenericExpansionState::locals) locals;
        decltype(GenericExpansionState::local_types) types;
        ~RestoreLocalTypes() {
            state.locals = std::move(locals);
            state.local_types = std::move(types);
        }
    } restore{state, std::move(saved_locals), std::move(saved_local_types)};
    state.locals.clear();
    state.local_types.clear();
    auto* owner = key.identity ? lexical_function(program, key.identity->function_scope) : nullptr;
    if (owner) collect_function_types(*owner, state);
    auto* caller = owner ? owner : &context;
    // Record requirements may query an alias-expanded local array type before
    // ordinary function rewriting reaches it. Prepare those exact lexical
    // types with their owner, retaining missing-context dependencies rather
    // than sending an unowned array to the target's layout service.
    if (owner) {
        co_await prepare_generic_local_types_async(*owner, program, diagnostics, state, mangling, &program.records[index]);
    }
    const auto rewrite_attribute = [&](std::optional<std::size_t> member, std::size_t attribute) -> EvaluationTask<void> {
        if (state.resource_failed(program)) co_return;
        const auto& attributes = member ? program.records[index].members[*member].attributes
                                       : program.records[index].attributes;
        if (!attributes[attribute].expression_argument) co_return;
        auto value = clone_expr(*attributes[attribute].expression_argument);
        co_await rewrite_generic_expr_async(value, caller, program, diagnostics, state, mangling);
        auto& updated = member ? program.records[index].members[*member].attributes
                               : program.records[index].attributes;
        updated[attribute].expression_argument = std::shared_ptr<Expr>(std::move(value));
    };
    for (std::size_t attribute = 0; attribute < program.records[index].attributes.size(); ++attribute)
        co_await rewrite_attribute({}, attribute);
    for (std::size_t member = 0; member < program.records[index].members.size(); ++member) {
        if (state.resource_failed(program)) break;
        auto type = program.records[index].members[member].type;
        if (kind == EvaluationLayoutKind::Alignment)
            while (type && type->kind == Type::Kind::Array) type = type->element;
        if (kind == EvaluationLayoutKind::Complete || (type && type->kind == Type::Kind::Vector))
            co_await rewrite_generic_type_bounds_async(type, caller, program, diagnostics, state, mangling,
                program.records[index].members[member].location);
        if (kind == EvaluationLayoutKind::Complete) {
            auto width = std::move(program.records[index].members[member].bit_width);
            co_await rewrite_generic_expr_async(width, caller, program, diagnostics, state, mangling);
            program.records[index].members[member].bit_width = std::move(width);
        }
        for (std::size_t attribute = 0; attribute < program.records[index].members[member].attributes.size(); ++attribute)
            co_await rewrite_attribute(member, attribute);
    }
    const bool valid = !state.resource_failed(program) && diagnostics.errors() == errors;
    prepared_records[key] = valid ? GenericExpansionState::RecordPreparation::Prepared
                                        : GenericExpansionState::RecordPreparation::Failed;
    co_return valid;
}

bool prepare_generic_record(const NominalTypeKey& key, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling,
    EvaluationLayoutKind kind = EvaluationLayoutKind::Complete) {
    return prepare_generic_record_async(key, program, diagnostics, state, mangling, kind).run();
}

EvaluationTask<bool> prepare_generic_layout_type_async(TypePtr type, EvaluationLayoutKind kind, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling) {
    if (kind == EvaluationLayoutKind::Alignment)
        while (type && type->kind == Type::Kind::Array) type = type->element;
    if (!type || type->kind == Type::Kind::Pointer || type->kind == Type::Kind::Function) co_return true;
    const auto errors = diagnostics.errors();
    if (kind == EvaluationLayoutKind::Complete || type->kind == Type::Kind::Vector)
        co_await rewrite_generic_type_bounds_async(type, nullptr, program, diagnostics, state, mangling);
    const bool ready = type->kind != Type::Kind::Record ||
        (co_await prepare_generic_record_async(type->nominal_key(), program, diagnostics, state, mangling, kind));
    co_return ready && diagnostics.errors() == errors;
}

std::string type_requirement_error(const TypeRequirement& requirement) {
    if (requirement.kind == TypeRequirement::Kind::GenericInterface)
        return "generic declarations of '" + requirement.name + "' have incompatible interfaces after substitution";
    return "typedef '" + requirement.name + "' redeclared with a different type";
}

EvaluationTask<void> rewrite_required_type_async(const TypeRequirement& requirement, FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling) {
    const auto errors = diagnostics.errors();
    GenericResourceFrame resource_frame(state, program);
    if (state.resource_failed(program)) co_return;
    co_await rewrite_generic_type_bounds_async(requirement.type, caller, program, diagnostics, state, mangling, requirement.location);
    if (state.resource_failed(program)) co_return;
    if (requirement.compatible_with) {
        co_await rewrite_generic_type_bounds_async(requirement.compatible_with, caller, program, diagnostics, state, mangling, requirement.location);
        const bool compatible = caller && evaluation_only(*caller)
            ? compare_source_types(requirement.type, requirement.compatible_with) != TypeComparison::Different
            : same_type(requirement.type, requirement.compatible_with);
        if (diagnostics.errors() == errors && !compatible)
            diagnostics.error(requirement.location, type_requirement_error(requirement));
    }
}

void rewrite_required_type(const TypeRequirement& requirement, FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling) {
    rewrite_required_type_async(requirement, caller, program, diagnostics, state, mangling).run();
}

EvaluationTask<void> rewrite_generic_statement_async(
    Statement& statement, FunctionDecl* caller, Program& program,
    Diagnostics& diagnostics,
    GenericExpansionState& state,
    std::string_view mangling) {
    GenericResourceFrame resource_frame(state, program);
    if (state.resource_failed(program)) co_return;
    const auto saved_locals = state.locals.size();
    const auto saved_local_types = state.local_types.size();
    const bool scoped = statement.kind == Statement::Kind::Compound ||
                        statement.kind == Statement::Kind::For;
    if (statement.kind == Statement::Kind::For && statement.first) {
        co_await rewrite_generic_statement_async(*statement.first, caller, program, diagnostics,
                                  state, mangling);
    }
    for (auto& child : statement.statements) {
        co_await rewrite_generic_statement_async(*child, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.declaration) {
        co_await rewrite_generic_type_bounds_async(statement.declaration->type, caller, program,
            diagnostics, state, mangling, statement.declaration->location);
        state.locals.push_back(name_key(*statement.declaration));
        state.local_types.emplace_back(name_key(*statement.declaration),
                                       statement.declaration->type);
        for (auto& attribute : statement.declaration->attributes) {
            if (!attribute.expression_argument) continue;
            auto expression = clone_expr(*attribute.expression_argument);
            co_await rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling);
            attribute.expression_argument = std::shared_ptr<Expr>(std::move(expression));
        }
        if (statement.declaration->dynamic_array_bound) {
            co_await rewrite_generic_expr_async(statement.declaration->dynamic_array_bound,
                                 caller, program, diagnostics, state, mangling);
            if (!state.resource_failed(program))
                co_await normalize_automatic_array_bound_async(*statement.declaration, caller, program,
                    diagnostics, state.local_types);
            state.local_types.back().second = statement.declaration->type;
        }
        if (statement.declaration->initializer) {
            co_await rewrite_generic_expr_async(statement.declaration->initializer, caller,
                                 program, diagnostics, state, mangling);
        }
    }
    if (statement.expression) {
        co_await rewrite_generic_expr_async(statement.expression, caller, program, diagnostics,
                             state, mangling);
    }
    if (statement.kind == Statement::Kind::StaticAssert &&
        (!caller || !evaluation_only(*caller))) {
        program.static_assertions.push_back({statement.location,
            caller ? caller->source_namespace : std::string{},
            std::move(statement.expression), std::move(statement.assertion_message),
            caller ? caller->function_scope : nullptr, state.local_types});
        statement.kind = Statement::Kind::Empty;
    }
    if (statement.condition) {
        co_await rewrite_generic_expr_async(statement.condition, caller, program, diagnostics,
                             state, mangling);
    }
    for (auto& increment : statement.increments)
        co_await rewrite_generic_expr_async(increment, caller, program, diagnostics,
                             state, mangling);
    if (statement.first && statement.kind != Statement::Kind::For) {
        co_await rewrite_generic_statement_async(*statement.first, caller, program, diagnostics,
                                  state, mangling);
    }
    if (statement.second) {
        co_await rewrite_generic_statement_async(*statement.second, caller, program, diagnostics,
                                  state, mangling);
    }
    if (scoped) {
        state.locals.resize(saved_locals);
        state.local_types.resize(saved_local_types);
    }
}

EvaluationTask<void> rewrite_generic_function_async(FunctionDecl& function, Program& program,
                              Diagnostics& diagnostics,
                              GenericExpansionState& state,
                              std::string_view mangling) {
    GenericResourceFrame resource_frame(state, program);
    if (state.resource_failed(program)) co_return;
    if (!function.generic_parameters.empty() ||
        !state.rewritten_functions.insert(&function).second) co_return;
    for (const auto& record : program.records) {
        if (!record.nominal_identity || record.nominal_identity->function_scope != function.function_scope) continue;
        if (const auto error = record_source_error(record, program, program.record_index, &state.record_proofs)) {
            diagnostics.error(error->location, error->message);
            co_return;
        }
    }
    const auto errors_before_bounds = diagnostics.errors();
    struct RestoreBindings {
        GenericExpansionState& state;
        decltype(GenericExpansionState::locals) locals;
        decltype(GenericExpansionState::local_types) types;
        ~RestoreBindings() {
            state.locals = std::move(locals);
            state.local_types = std::move(types);
        }
    } restore_bindings{state, std::move(state.locals), std::move(state.local_types)};
    state.locals.clear();
    state.local_types.clear();
    auto header_binding_count = function.parameters.size();
    for (const auto& attribute : function.attributes)
        header_binding_count += attribute.variadic_bindings.size();
    // Every parsed local use carries its declaring identity. Expose those
    // declarations' types (never values) to required layout operands, even in
    // unused typedef constraints. This does not make later or unrelated
    // same-spelled declarations visible to an earlier use.
    collect_function_types(function, state);
    // Typedef requirements can query earlier locals. Their alias-expanded
    // types are distinct graphs, so prepare those exact declaration types
    // before a required layout query can reach a context-free target service.
    co_await prepare_generic_local_types_async(function, program, diagnostics, state, mangling);
    // Record bounds may query an earlier initializer-inferred local. Prepare
    // those lexical extents first, then records before ordinary expression
    // deduction can project their members. Invocation-private record views
    // have already been prepared and must not be written back to source.
    if (state.invocation_owner != &function) {
        for (std::size_t index = 0; index < program.records.size(); ++index) {
            const auto key = program.records[index].nominal_key();
            if (key.identity && key.identity->function_scope == function.function_scope)
                (void)(co_await prepare_generic_record_async(key, program, diagnostics, state, mangling));
        }
    }
    co_await rewrite_generic_type_bounds_async(function.return_type, &function, program, diagnostics, state, mangling);
    for (const auto& requirement : function.required_types)
        co_await rewrite_required_type_async(requirement, &function, program, diagnostics, state, mangling);
    for (const auto& parameter : function.parameters) {
        co_await rewrite_generic_type_bounds_async(parameter.type, &function, program, diagnostics, state, mangling, parameter.location);
        co_await rewrite_generic_type_bounds_async(parameter.declared_array_type, &function, program, diagnostics, state, mangling, parameter.location);
    }
    for (const auto& attribute : function.attributes)
        for (const auto& binding : attribute.variadic_bindings)
            co_await rewrite_generic_type_bounds_async(binding.type, &function, program, diagnostics, state, mangling, binding.location);
    state.locals.resize(header_binding_count);
    state.local_types.resize(header_binding_count);
    if (diagnostics.errors() != errors_before_bounds) {
        co_return;
    }
    // Resolve source-local bound queries before static cells leave the body;
    // lift before ordinary expression rewriting so generic pointer arguments
    // can still refer to the resulting static object identities.
    // Translation-only bodies have no emitted storage. Keep their declarations
    // for source validation and reached sandbox checks; lifting them would force
    // invocation-dependent initializers outside the expansion that owns them.
    if (function.body && !state.expansion_evaluation && !evaluation_only(function))
        lift_static_locals(program, function);
    // Function attributes contain required expressions too. Rewrite after
    // generic substitution, before evaluating alignment, and detach the shared
    // expression so declarations/instances cannot mutate one another's tree.
    for (auto& attribute : function.attributes) {
        if (!attribute.expression_argument) continue;
        auto expression = clone_expr(*attribute.expression_argument);
        co_await rewrite_generic_expr_async(expression, &function, program, diagnostics, state, mangling);
        attribute.expression_argument = std::shared_ptr<Expr>(std::move(expression));
    }
    if (function.body)
        co_await rewrite_generic_statement_async(*function.body, &function, program, diagnostics,
                                  state, mangling);
}

void rewrite_generic_function(FunctionDecl& function, Program& program,
    Diagnostics& diagnostics, GenericExpansionState& state, std::string_view mangling) {
    rewrite_generic_function_async(function, program, diagnostics, state, mangling).run();
}

std::unique_ptr<FunctionDecl> instantiate(
    const FunctionDecl& source, const std::vector<Expr::GenericArgument>& arguments,
    std::string internal_name, std::string instance_key, Program& program,
    Diagnostics& diagnostics, const GenericExpansionState& state) {
    if (source.generic_parameters.size() != arguments.size()) {
        diagnostics.error(source.location,
                          "generic argument count does not match '" + source.name + "'");
        return {};
    }
    const auto initial_errors = diagnostics.errors();
    TypeSubstitutions types;
    types.diagnostics = &diagnostics;
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
            types.emplace(name_key(parameter), argument.type);
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
    if (types.nominal) result->function_scope = types.nominal->function_scope;
    result->location = source.location;
    result->name = std::move(internal_name);
    result->source_namespace = source.source_namespace;
    result->source_unit = source.source_unit;
    result->imports = source.imports;
    result->translation_context = source.translation_context;
    result->return_type = clone_type(source.return_type, types, values);
    for (const auto& requirement : source.required_types)
        result->required_types.push_back({clone_type(requirement.type, types, values),
            clone_type(requirement.compatible_with, types, values), requirement.location,
            requirement.source_namespace, requirement.name, requirement.kind});
    for (const auto& parameter : source.parameters) {
        result->parameters.push_back(
            {parameter.location, parameter.name, clone_type(parameter.type, types, values),
             parameter.mode, parameter.explicit_mode, parameter.location_name,
             clone_type(parameter.declared_array_type, types, values), parameter.binding});
    }
    for (const auto& attribute : source.attributes) {
        auto copy = attribute;
        for (auto& binding : copy.variadic_bindings)
            binding.type = clone_type(binding.type, types, values);
        if (attribute.expression_argument) {
            copy.expression_argument = std::shared_ptr<Expr>(
                clone_expr(*attribute.expression_argument, types, values));
        }
        result->attributes.push_back(std::move(copy));
    }
    // Concrete instances have a new entity name and templates disappear before
    // evaluation/lowering. Keep a no-return contract supplied by a redeclaration
    // as well as one written directly on the selected definition.
    if (!result->attribute("noreturn"))
        if (const auto* attribute = function_entity_attribute(program, source, "noreturn"))
            result->attributes.push_back(*attribute);
    result->result_location = source.result_location;
    // Preserve each deferred declaration promise on the concrete instance.
    // Required-type preparation resolves these private graphs using the target
    // services; translation-only calls recheck context-dependent extents at
    // invocation as they do other retained type constraints.
    for (const auto& pair : state.pending_interfaces) {
        const auto* declaration = pair.left == &source ? pair.right : pair.right == &source ? pair.left : nullptr;
        if (!declaration) continue;
        TypeSubstitutions declaration_types;
        ValueSubstitutions declaration_values;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto key = name_key(declaration->generic_parameters[index]);
            if (arguments[index].type) declaration_types.emplace(key, arguments[index].type);
            else declaration_values.emplace(key, arguments[index].value.get());
        }
        // Parameter names do not affect callable identity. A bound's sizeof
        // operand or invocation-time meta use must name the selected definition's
        // corresponding cell, while all unrelated captured uses retain identity.
        std::vector<std::unique_ptr<Expr>> parameter_uses;
        for (std::size_t index = 0; index < declaration->parameters.size(); ++index) {
            const auto& parameter = result->parameters[index];
            auto use = std::make_unique<Expr>();
            use->kind = Expr::Kind::Name;
            use->text = parameter.name;
            use->location = parameter.location;
            auto context = std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Local;
            context->value_binding = parameter.binding;
            use->name_context = std::move(context);
            declaration_values.emplace(name_key(declaration->parameters[index]), use.get());
            parameter_uses.push_back(std::move(use));
        }
        const auto require_interface = [&](TypePtr expected, TypePtr actual) {
            result->required_types.push_back({std::move(expected), std::move(actual), declaration->location,
                declaration->source_namespace, source.name, TypeRequirement::Kind::GenericInterface});
        };
        // The declaration's header definitions denote this instance's types.
        const auto declared = header_definitions(program, *declaration);
        const auto defined = header_definitions(program, source);
        if (types.nominal && !declared.empty()) {
            declaration_types.nominal = std::make_shared<NominalInstantiation>();
            declaration_types.nominal->owner = declaration->generic_tag_owner;
            for (std::size_t index = 0; index < std::min(declared.size(), defined.size()); ++index)
                declaration_types.nominal->identities.emplace(declared[index].key.identity.get(),
                    types.nominal->substitute(defined[index].key.identity));
        }
        require_interface(source_function_type(*declaration, declaration_types, declaration_values),
                          source_function_type(*result));
        for (std::size_t index = 0; index < std::min(declared.size(), defined.size()); ++index) {
            if (!declared[index].record || !defined[index].record) continue;
            const auto& members = declared[index].record->members;
            for (std::size_t at = 0; at < std::min(members.size(), defined[index].record->members.size()); ++at)
                require_interface(clone_type(members[at].type, declaration_types, declaration_values),
                                  clone_type(defined[index].record->members[at].type, types, values));
        }
        for (std::size_t index = 0; index < arguments.size(); ++index)
            if (source.generic_parameters[index].value_type)
                require_interface(clone_type(declaration->generic_parameters[index].value_type,
                                             declaration_types, declaration_values),
                                  clone_type(source.generic_parameters[index].value_type, types, values));
    }
    for (const auto& assertion : source.deferred_static_assertions) {
        result->deferred_static_assertions.push_back(
            {assertion.location, assertion.source_namespace,
             assertion.condition
                 ? clone_expr(*assertion.condition, types, values)
                 : std::unique_ptr<Expr>{},
             assertion.message});
    }
    if (source.body) result->body = clone_statement(*source.body, types, values);
    if (diagnostics.errors() != initial_errors) return {};
    result->linkage = source.linkage;
    result->variadic = source.variadic;
    result->inline_hint = source.inline_hint;
    result->generic_instance = true;
    if (types.nominal) {
        // Clone into temporary vectors: publication can reallocate the program
        // tables, and definitions may refer to each other in either direction.
        std::vector<RecordDecl> records;
        std::vector<EnumDecl> enumerations;
        for (const auto& record : program.records)
            if (record.nominal_identity &&
                record.nominal_identity->generic_owner == source.generic_tag_owner)
                records.push_back(instantiate_record(record, types, values));
        for (const auto& enumeration : program.enumerations)
            if (enumeration.nominal_identity &&
                enumeration.nominal_identity->generic_owner == source.generic_tag_owner)
                enumerations.push_back(instantiate_enumeration(enumeration, types, values));
        if (diagnostics.errors() != initial_errors) return {};
        program.records.insert(program.records.end(),
            std::make_move_iterator(records.begin()), std::make_move_iterator(records.end()));
        program.enumerations.insert(program.enumerations.end(),
            std::make_move_iterator(enumerations.begin()), std::make_move_iterator(enumerations.end()));
    }
    return result;
}

EvaluationTask<void> rewrite_generic_expr_async(std::unique_ptr<Expr>& expression,
                          const FunctionDecl* caller, Program& program,
                          Diagnostics& diagnostics,
                          GenericExpansionState& state,
                          std::string_view mangling) {
    GenericResourceFrame resource_frame(state, program);
    if (!expression || state.resource_failed(program)) co_return;
    co_await rewrite_generic_type_bounds_async(expression->type, caller, program, diagnostics, state, mangling, expression->location);
    for (auto& argument : expression->generic_arguments)
        co_await rewrite_generic_type_bounds_async(argument.type, caller, program, diagnostics, state, mangling, expression->location);
    if (expression->left) {
        co_await rewrite_generic_expr_async(expression->left, caller, program, diagnostics,
                             state, mangling);
    }
    if (expression->right) {
        co_await rewrite_generic_expr_async(expression->right, caller, program, diagnostics,
                             state, mangling);
    }
    if (expression->third) {
        co_await rewrite_generic_expr_async(expression->third, caller, program, diagnostics,
                             state, mangling);
    }
    for (auto& argument : expression->arguments) {
        co_await rewrite_generic_expr_async(argument, caller, program, diagnostics,
                             state, mangling);
    }
    for (auto& argument : expression->generic_arguments) {
        if (argument.value) {
            co_await rewrite_generic_expr_async(argument.value, caller, program, diagnostics,
                                 state, mangling);
        }
    }
    for (auto& entry : expression->initializer_entries) {
        for (auto& designator : entry.designators)
            if (designator.index)
                co_await rewrite_generic_expr_async(designator.index, caller, program, diagnostics, state, mangling);
        if (entry.value)
            co_await rewrite_generic_expr_async(entry.value, caller, program, diagnostics, state, mangling);
    }
    if (state.resource_failed(program)) co_return;
    if (expression->kind == Expr::Kind::Binary && expression->left &&
        (expression->text == "member" || expression->text == "pointer_member") &&
        caller != state.invocation_owner) {
        auto base = infer_generic_actual(*expression->left, caller, program, state, false);
        if (base && expression->text == "pointer_member" && base->kind == Type::Kind::Pointer) base = base->pointee;
        if (base && base->kind == Type::Kind::Record &&
            !(co_await prepare_generic_record_async(base->nominal_key(), program, diagnostics, state, mangling))) co_return;
    }
    if (state.rewrite_operator) state.rewrite_operator(expression, caller);
    const bool address_use = expression->kind == Expr::Kind::Name &&
        !expression->generic_arguments.empty();
    auto* designator = address_use ? expression.get()
        : expression->kind == Expr::Kind::Call ? expression->left.get() : nullptr;
    if (!designator || designator->kind != Expr::Kind::Name) co_return;
    if (std::find(state.locals.begin(), state.locals.end(),
                  name_key(*designator)) != state.locals.end()) {
        if (!expression->generic_arguments.empty())
            diagnostics.error(expression->location,
                "generic arguments applied to non-generic local value '" + designator->text + "'");
        co_return;
    }
    const auto name = designator->text;
    auto* declared_generic = resolve_function(
        program, caller, *designator,
        [](const FunctionDecl& candidate) {
            return !candidate.generic_parameters.empty();
        });
    auto* generic = resolve_function(
        program, caller, *designator,
        [](const FunctionDecl& candidate) {
            return !candidate.generic_parameters.empty() &&
                   candidate.body != nullptr;
        });
    if (!generic && !declared_generic &&
        expression->generic_arguments.empty()) {
        if (auto* function = resolve_function(
                       program, caller, *designator,
                       [](const FunctionDecl& candidate) { return candidate.body != nullptr; })) {
            // An ordinary helper used by a generic constant may itself call
            // generics. Prepare its definition before any evaluator enters it,
            // regardless of declaration order. The visited set breaks cycles.
            co_await rewrite_generic_function_async(*function, program, diagnostics, state, mangling);
        }
        co_return;
    }
    if (!generic) {
        diagnostics.error(
            expression->location,
            declared_generic
                ? "definition of generic function '" + name +
                      "' is not visible in this compilation group"
                : "generic arguments applied to non-generic function '" +
                      name + "'");
        co_return;
    }
    if (!expression->generic_visible_at_call) {
        diagnostics.error(expression->location,
                          "generic function '" + name +
                              "' must be declared before use");
        co_return;
    }
    std::vector<Expr::GenericArgument> arguments;
    for (const auto& source : expression->generic_arguments) {
        Expr::GenericArgument argument;
        if (source.type) argument.type = clone_type(source.type);
        if (source.value) argument.value = clone_expr(*source.value);
        arguments.push_back(std::move(argument));
    }
    bool context_dependent{};
    const bool allow_deferred = caller && evaluation_only(*caller) && caller != state.invocation_owner;
    // An address application supplies its complete argument list; there are
    // no call operands from which to deduce missing type parameters. It uses
    // the same canonical type/value normalization and instance cache as calls.
    if (address_use) {
        for (auto& argument : arguments) {
            if (!argument.type) continue;
            argument.type = clone_type(argument.type);
            if (!normalize_generic_callable_abis(argument.type, state,
                                                 diagnostics, expression->location)) co_return;
            if (has_context_dependent_type_bound(argument.type)) context_dependent = true;
        }
    } else if (!deduce_generic_arguments(*generic, *expression, caller, program,
                                  state, arguments,
                                  diagnostics, allow_deferred ? &context_dependent : nullptr)) co_return;
    if (!(co_await normalize_generic_arguments_async(*generic, arguments,
                                     caller, program, diagnostics, expression->location,
                                     state, mangling, allow_deferred ? &context_dependent : nullptr))) co_return;
    if (context_dependent) {
        if (!allow_deferred) {
            diagnostics.error(expression->location, "generic application requires resolved fixed type bounds");
            co_return;
        }
        TypeSubstitutions types;
        ValueSubstitutions values;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto key = name_key(generic->generic_parameters[index]);
            if (arguments[index].type) types.emplace(key, arguments[index].type);
            else if (arguments[index].value) values.emplace(key, arguments[index].value.get());
        }
        auto signature = source_function_type(*generic, types, values);
        co_await rewrite_generic_type_bounds_async(signature, caller, program, diagnostics, state, mangling, expression->location);
        if (!normalize_generic_callable_abis(signature, state, diagnostics, expression->location)) co_return;
        designator->deferred_generic_signature = std::move(signature);
        co_return;
    }
    designator->deferred_generic_signature.reset();
    expression->generic_arguments = std::move(arguments);
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
        const auto& limits = program.evaluation_limits;
        if (state.depth >= limits.generic_depth ||
            state.instances.size() >= limits.generic_instances) {
            ++program.evaluation_resource_errors;
            diagnostics.error(expression->location,
                state.depth >= limits.generic_depth
                    ? "generic instantiation budget exceeded: nesting depth limit " +
                          std::to_string(limits.generic_depth) + " (-fgeneric-depth-limit)"
                    : "generic instantiation budget exceeded: instance limit " +
                          std::to_string(limits.generic_instances) + " (-fgeneric-instance-limit)");
            co_return;
        }
        internal_name = generic->name + "$G" +
                        std::to_string(stable_hash(identity));
        while (std::any_of(state.instances.begin(), state.instances.end(),
                          [&](const auto& instance) { return instance.name == internal_name; }))
            internal_name += '$';
        const auto first_record = program.records.size();
        const auto first_enumeration = program.enumerations.size();
        auto instance = instantiate(*generic, expression->generic_arguments,
                                    internal_name, identity, program, diagnostics, state);
        if (!instance) co_return;
        instance->invocation_specialization = state.invocation_owner != nullptr ||
            (caller && evaluation_only(*caller));
        if (instance->linkage == Linkage::Global && !instance->attribute("link_name")) {
            const auto link_name = generic_link_name(generic->name, *instance,
                expression->generic_arguments, state, diagnostics, mangling);
            if (!link_name) co_return;
            instance->attributes.push_back(
                {"link_name", {'"' + *link_name + '"'}, instance->location});
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
        if (!(co_await evaluate_enumerations_async(program, diagnostics, first_enumeration,
                [&](std::unique_ptr<Expr>& initializer) -> EvaluationTask<void> {
                    co_await rewrite_generic_expr_async(initializer, concrete, program, diagnostics, state, mangling);
                }))) co_return;
        for (auto index = first_record; index < program.records.size(); ++index) {
            const auto key = program.records[index].nominal_key();
            if (!(co_await prepare_generic_record_async(key, program, diagnostics, state, mangling))) co_return;
        }
        // Publish before walking the body so recursive identical instances
        // resolve to the in-progress function. Nested constant generic calls
        // can then be evaluated through the same visible definition table.
        if (concrete->body) {
            co_await rewrite_generic_function_async(*concrete, program, diagnostics, state, mangling);
        }
    }
    if (!designator->instance_label.empty()) {
        bool found_label = false;
        std::vector<const Statement*> pending;
        if (generic->body) pending.push_back(generic->body.get());
        while (!pending.empty() && !found_label) {
            const auto* statement = pending.back();
            pending.pop_back();
            found_label = statement->kind == Statement::Kind::Label &&
                statement->label_name == designator->instance_label;
            if (statement->first) pending.push_back(statement->first.get());
            if (statement->second) pending.push_back(statement->second.get());
            for (const auto& child : statement->statements) pending.push_back(child.get());
        }
        if (!found_label) {
            diagnostics.error(expression->location, "generic function '" + name +
                "' has no label '" + designator->instance_label + "'");
            co_return;
        }
        internal_name += "::" + std::exchange(designator->instance_label, {});
    }
    bind_exact_name(*designator, std::move(internal_name));
    expression->generic_arguments.clear();
}

void rewrite_generic_expr(std::unique_ptr<Expr>& expression,
    const FunctionDecl* caller, Program& program, Diagnostics& diagnostics,
    GenericExpansionState& state, std::string_view mangling) {
    rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling).run();
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
    if (!install_operator_bindings(program, diagnostics, state)) return false;
    auto previous_prepare = std::move(program.evaluation_prepare_type);
    struct RestorePreparation {
        Program& program;
        decltype(Program::evaluation_prepare_type) previous;
        ~RestorePreparation() { program.evaluation_prepare_type = std::move(previous); }
    } restore_preparation{program, std::move(previous_prepare)};
    program.evaluation_prepare_type = [&](const TypePtr& type, EvaluationLayoutKind kind) -> EvaluationTask<bool> {
        co_return co_await prepare_generic_layout_type_async(type, kind, program, diagnostics, state, mangling);
    };
    if (!evaluate_enumerations(program, diagnostics, 0,
            [&](std::unique_ptr<Expr>& initializer) -> EvaluationTask<void> {
                co_await rewrite_generic_expr_async(initializer, nullptr, program, diagnostics, state, mangling);
            })) return false;
    materialize_enumerators(program, diagnostics);
    for (const auto& requirement : program.required_types) {
        FunctionDecl context;
        context.source_namespace = requirement.source_namespace;
        rewrite_required_type(requirement, &context, program, diagnostics, state, mangling);
    }
    for (std::size_t index = 0; index < program.records.size(); ++index) {
        const auto key = program.records[index].nominal_key();
        if (!prepare_generic_record(key, program, diagnostics, state, mangling)) return false;
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (!function->generic_parameters.empty()) continue;
        rewrite_generic_function(*function, program, diagnostics, state, mangling);
    }
    for (std::size_t index = 0; index < program.objects.size(); ++index) {
        auto* object = program.objects[index].get();
        auto* owner = object_lexical_function(program, *object);
        auto saved_locals = std::move(state.locals);
        auto saved_types = std::move(state.local_types);
        state.locals.clear();
        state.local_types.clear();
        if (owner) collect_function_types(*owner, state);
        FunctionDecl type_context;
        type_context.source_namespace = namespace_prefix(object->name);
        type_context.source_unit = object->source_unit;
        rewrite_generic_type_bounds(object->type, owner ? owner : &type_context,
            program, diagnostics, state, mangling, object->location);
        if (object->initializer) {
            FunctionDecl context;
            context.name = object->name;
            context.source_namespace = namespace_prefix(object->name);
            context.source_unit = object->source_unit;
            rewrite_generic_expr(object->initializer, owner ? owner : &context, program,
                                 diagnostics, state, mangling);
        }
        state.locals = std::move(saved_locals);
        state.local_types = std::move(saved_types);
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
        auto* owner = assertion_lexical_function(program, program.static_assertions[index]);
        auto saved_locals = std::move(state.locals);
        auto saved_types = std::move(state.local_types);
        state.locals.clear();
        state.local_types = program.static_assertions[index].local_types;
        for (const auto& [name, type] : state.local_types) state.locals.push_back(name);
        auto condition = std::move(program.static_assertions[index].condition);
        rewrite_generic_expr(condition, owner ? owner : &context, program, diagnostics, state, mangling);
        program.static_assertions[index].condition = std::move(condition);
        state.locals = std::move(saved_locals);
        state.local_types = std::move(saved_types);
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
    program.record_index = {};
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
        for (const auto& requirement : function.required_types) {
            rewrite_type(requirement.type);
            rewrite_type(requirement.compatible_with);
        }
        for (auto& record : program_.records) {
            if (!record.nominal_identity || record.nominal_identity->function_scope != function.function_scope) continue;
            rewrite_attributes(record.attributes);
            for (auto& member : record.members) {
                rewrite_type(member.type);
                rewrite(member.bit_width);
                rewrite_attributes(member.attributes);
            }
        }
    }

private:
    const std::string* replacement(const Expr& expression) const {
        const auto key = name_key(expression);
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(key);
            if (found == scope->end()) continue;
            return found->second.empty() ? nullptr : &found->second;
        }
        // Detached type requirements retain resolved local declaration keys,
        // even after traversal has left their original lexical block.
        if (const auto found = lifted_.find(key); found != lifted_.end())
            return &found->second;
        return nullptr;
    }

    template <typename Expression>
    void rewrite(std::shared_ptr<Expression>& expression) {
        if (!expression) return;
        auto copy = clone_expr(*expression);
        rewrite(copy);
        expression = std::move(copy);
    }

    void rewrite_type(const TypePtr& type) {
        if (!type || !rewritten_types_.insert(type.get()).second) return;
        rewrite(type->array_bound);
        rewrite(type->vector_bound);
        for (auto& request : type->alignment_requests) rewrite(request);
        rewrite_type(type->element);
        rewrite_type(type->pointee);
        if (type->function) {
            rewrite_type(type->function->result);
            for (const auto& parameter : type->function->parameters) {
                rewrite_type(parameter.type);
                rewrite_type(parameter.declared_array_type);
            }
        }
    }

    void rewrite_attributes(std::vector<Attribute>& attributes) {
        for (auto& attribute : attributes) {
            rewrite(attribute.expression_argument);
            for (const auto& binding : attribute.variadic_bindings) rewrite_type(binding.type);
        }
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        rewrite_type(expression->type);
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
            rewrite_type(argument.type);
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
                if (name_key(declaration).binding.kind == ValueBinding::Kind::Local)
                    lifted_[name_key(declaration)] = lifted_name;
                rewrite_type(declaration.type);
                rewrite_attributes(declaration.attributes);
                rewrite(declaration.initializer);
                auto object = std::make_unique<ObjectDecl>();
                object->location = declaration.location;
                object->name = lifted_name;
                object->source_unit = function_->source_unit;
                object->lexical_function = function_->function_scope;
                object->type = std::move(declaration.type);
                object->initializer = std::move(declaration.initializer);
                object->linkage = Linkage::Static;
                object->attributes = std::move(declaration.attributes);
                program_.objects.push_back(std::move(object));
                statement.declaration.reset();
                statement.kind = Statement::Kind::Empty;
            } else {
                rewrite_type(declaration.type);
                rewrite_attributes(declaration.attributes);
                rewrite(declaration.dynamic_array_bound);
                rewrite(declaration.initializer);
                scopes_.back()[name_key(declaration)] = {};
            }
        }
        rewrite(statement.expression);
        rewrite(statement.condition);
        for (auto& increment : statement.increments) rewrite(increment);
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
    NameMap<std::string> lifted_;
    std::unordered_set<const Type*> rewritten_types_;
};

void lift_static_locals(Program& program, FunctionDecl& function) {
    StaticLocalLifter(program).run(function);
}

enum class SourceMemberIssue { None, NotMember, UnknownType, NotPointer, NotRecord, IncompleteRecord, MissingMember };

template<class TypeOf>
const RecordMemberDecl* source_record_member(const Expr& expression, const Program& program,
                                             const TypeOf& type_of, TypePtr* owner_type = nullptr,
                                             SourceMemberIssue* issue = nullptr) {
    const auto reject = [&](SourceMemberIssue reason) -> const RecordMemberDecl* {
        if (issue) *issue = reason;
        return nullptr;
    };
    const auto* selected = &expression;
    while (selected->kind == Expr::Kind::Parenthesized && selected->left)
        selected = selected->left.get();
    if (selected->kind != Expr::Kind::Binary ||
        (selected->text != "member" && selected->text != "pointer_member") ||
        !selected->left || !selected->right || selected->right->kind != Expr::Kind::Name)
        return reject(SourceMemberIssue::NotMember);
    auto owner = type_of(*selected->left);
    if (!owner || owner->kind == Type::Kind::Generic) return reject(SourceMemberIssue::UnknownType);
    if (selected->text == "pointer_member") {
        if (owner->kind == Type::Kind::Array) owner = qualified_element_type(owner);
        else if (owner->kind == Type::Kind::Pointer) owner = owner->pointee;
        else return reject(SourceMemberIssue::NotPointer);
    }
    if (!owner || owner->kind == Type::Kind::Generic) return reject(SourceMemberIssue::UnknownType);
    if (owner->kind != Type::Kind::Record) return reject(SourceMemberIssue::NotRecord);
    const auto record = program.record_definition(owner->nominal_key());
    if (!record) return reject(SourceMemberIssue::IncompleteRecord);
    const auto member = std::find_if(record->members.begin(), record->members.end(),
        [&](const RecordMemberDecl& candidate) { return candidate.member_name() == member_name(*selected->right); });
    if (member == record->members.end()) return reject(SourceMemberIssue::MissingMember);
    if (issue) *issue = SourceMemberIssue::None;
    if (owner_type) *owner_type = std::move(owner);
    return &*member;
}

class FunctionPointerAdapterLifter {
public:
    FunctionPointerAdapterLifter(Program& program,
                                 std::string_view default_abi)
        : program_(program), default_abi_(default_abi) {}

    void run() {
        const auto object_count = program_.objects.size();
        for (std::size_t index = 0; index < object_count; ++index) {
            caller_ = object_lexical_function(program_, *program_.objects[index]);
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
            for (const auto& attribute : caller_->attributes)
                for (const auto& binding : attribute.variadic_bindings)
                    scopes_.back().emplace(name_key(binding), binding.type);
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

    // The adapter supplies the rest of the destination interface: ABI,
    // endpoints, result location, clobbers, and stack cleanup.
    static bool same_signature(const FunctionDecl& source,
                               const FunctionType& destination) {
        if (source.variadic != destination.variadic ||
            source.parameters.size() != destination.parameters.size() ||
            !same_type(callable_result_type(source.return_type),
                       callable_result_type(destination.result))) {
            return false;
        }
        for (std::size_t index = 0; index < source.parameters.size(); ++index) {
            if (source.parameters[index].mode !=
                    destination.parameters[index].mode ||
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

    // A non-dynamic function whose complete interface already matches the
    // destination keeps its own address.
    bool already_stable(const FunctionDecl& source,
                        const TypePtr& destination) const {
        const auto source_abi = explicit_abi(source);
        const bool dynamic = source.definition() && !source.variadic &&
                             source.linkage != Linkage::Global;
        if (dynamic && source_abi.empty()) return false;
        const auto canonical = [&](std::string_view abi) -> std::optional<std::string> {
            const auto effective = abi.empty() ? std::string_view(default_abi_) : abi;
            if (program_.canonical_callable_abi) return program_.canonical_callable_abi(effective);
            return std::string(effective);
        };
        auto own = source_function_type(source);
        auto wanted = clone_type(destination);
        const auto own_abi = canonical(source_abi);
        const auto wanted_abi = canonical(wanted->function->abi);
        if (!own_abi || !wanted_abi) return false;
        own->function->abi = *own_abi;
        wanted->function->abi = *wanted_abi;
        return compare_source_types(own, wanted) != TypeComparison::Different;
    }

    TypePtr lookup(const Expr& expression) const {
        const auto key = name_key(expression);
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(key);
            if (found != scope->end()) return found->second;
        }
        if (const auto* object = resolve_object(program_, caller_, expression)) return object->type;
        return {};
    }

    template<class TypeOf>
    TypePtr infer_step(const Expr& expression, const TypeOf& type_of) const {
        switch (expression.kind) {
        case Expr::Kind::VoidValue: return builtin_type(BuiltinType::Void);
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
            return expression.left ? type_of(*expression.left) : TypePtr{};
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
        case Expr::Kind::Offsetof:
            return builtin_type(BuiltinType::Uptr);
        case Expr::Kind::Unary: {
            const auto operand = expression.left ? type_of(*expression.left)
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
            if (expression.text == "member" || expression.text == "pointer_member") {
                TypePtr owner;
                const auto* member = source_record_member(expression, program_,
                    [&](const Expr& operand) { return type_of(operand); }, &owner);
                if (!member || !member->type) return {};
                auto result = std::make_shared<Type>(*member->type);
                result->is_const = result->is_const || owner->is_const;
                result->is_volatile = result->is_volatile || owner->is_volatile;
                return result;
            }
            if (expression.text == "index" && expression.left) {
                const auto aggregate = type_of(*expression.left);
                if (aggregate && aggregate->kind == Type::Kind::Pointer) {
                    return aggregate->pointee;
                }
                if (aggregate && (aggregate->kind == Type::Kind::Array || aggregate->kind == Type::Kind::Vector)) {
                    return qualified_element_type(aggregate);
                }
                // An unavailable element type stays unavailable. Falling back
                // through generic binary inference repeats the whole prefix.
                return {};
            }
            if (expression.text == "==" || expression.text == "!=" ||
                expression.text == "<" || expression.text == "<=" ||
                expression.text == ">" || expression.text == ">=" ||
                expression.text == "&&" || expression.text == "||") {
                return builtin_type(BuiltinType::Bool);
            }
            const auto left = expression.left ? type_of(*expression.left)
                                              : TypePtr{};
            const auto right = expression.right ? type_of(*expression.right)
                                                : TypePtr{};
            return same_type(left, right) ? left : (left ? left : right);
        }
        case Expr::Kind::Assign:
            return expression.left ? type_of(*expression.left) : TypePtr{};
        case Expr::Kind::Conditional: {
            const auto left = expression.right ? type_of(*expression.right)
                                               : TypePtr{};
            const auto right = expression.third ? type_of(*expression.third)
                                               : TypePtr{};
            if ((left && left->kind == Type::Kind::Pointer) || (right && right->kind == Type::Kind::Pointer))
                return conditional_pointer_type(left, right);
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
                        type_of(*expression.left))) {
                    return signature->result;
                }
            }
            return {};
        }
        return {};
    }

    TypePtr infer(const Expr& expression) const {
        return classify_source_type(expression, false,
            [&](const Expr& source, bool, const auto& type_of) { return infer_step(source, type_of); },
            [] { return false; });
    }

    std::string make_adapter(const FunctionDecl& source,
                             const TypePtr& destination,
                             SourceLocation location) {
        const auto& signature = *destination->function;
        const auto adapter_abi = signature.abi.empty()
                                     ? default_abi_
                                     : signature.abi;
        // Spellings of one interface share an adapter, preserving address
        // identity across conversions.
        auto identity = clone_type(destination);
        if (program_.canonical_callable_abi)
            identity->function->abi =
                program_.canonical_callable_abi(adapter_abi).value_or(adapter_abi);
        const auto key = source.name + '#' +
                         (source.linkage == Linkage::Static ? source.source_unit : "") + '#' +
                         canonical_type_name(identity);
        if (const auto found = adapters_.find(key); found != adapters_.end()) {
            return found->second;
        }

        auto adapter = std::make_unique<FunctionDecl>();
        adapter->location = location;
        adapter->name = "$adapter." + std::to_string(stable_hash(source.source_unit)) + '.' +
                        std::to_string(ordinals_[source.source_unit]++);
        adapter->source_unit = source.source_unit;
        adapter->return_type = clone_type(signature.result);
        adapter->linkage = Linkage::Static;
        adapter->variadic = signature.variadic;
        adapter->attributes.push_back(
            {"abi", {"\"" + adapter_abi + "\""}, location});
        adapter->result_location = signature.result_location;
        if (!signature.clobbers.empty()) {
            Attribute clobber{"clobber", {}, location};
            for (const auto& resource : signature.clobbers)
                clobber.arguments.push_back("\"" + resource + "\"");
            adapter->attributes.push_back(std::move(clobber));
        }
        if (signature.stack_cleanup)
            adapter->attributes.push_back(
                {"stack_cleanup", {"\"" + *signature.stack_cleanup + "\""}, location});

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

    const FunctionDecl* named_function(const Expr& expression) const {
        if (expression.kind == Expr::Kind::Address && expression.evaluated_address)
            return expression.evaluated_address->function;
        if (expression.kind != Expr::Kind::Name) return nullptr;
        return resolve_function(program_, caller_, expression,
                                [](const FunctionDecl&) { return true; });
    }

    // The pointer to a named function's own interface, else null.
    TypePtr own_pointer_type(const Expr& expression) const {
        const auto* selected = &expression;
        while (selected->left && (selected->kind == Expr::Kind::Parenthesized ||
               (selected->kind == Expr::Kind::Unary && selected->text == "&")))
            selected = selected->left.get();
        const auto* source = named_function(*selected);
        return source ? pointer_type(source_function_type(*source)) : TypePtr{};
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
        const auto* source = named_function(*expression);
        if (!source || evaluation_only(*source) || source->variadic ||
            !same_signature(*source, *signature) ||
            already_stable(*source, destination->pointee)) {
            return false;
        }
        bind_exact_name(*expression, make_adapter(*source, destination->pointee,
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
            // A conditional chooses no adapter: a named arm only decays to
            // its own stable pointer, and both arms must share one interface.
            rewrite(expression->left);
            rewrite(expression->right, own_pointer_type(*expression->right));
            rewrite(expression->third, own_pointer_type(*expression->third));
            return;
        case Expr::Kind::Cast:
            rewrite(expression->left, expression->type);
            return;
        case Expr::Kind::AggregateInitializer:
            rewrite_initializer(*expression, destination);
            return;
        case Expr::Kind::Binary:
            if (expression->text == "==" || expression->text == "!=") {
                const auto left = expression->left ? infer(*expression->left) : TypePtr{};
                const auto right = expression->right ? infer(*expression->right) : TypePtr{};
                // A function designator compared with a typed callback needs
                // the same stable interface conversion as an initializer.
                // Otherwise the comparison observes the private implementation
                // address on one side and its ABI adapter on the other.
                rewrite(expression->left, pointed_function(right) ? right : TypePtr{});
                rewrite(expression->right, pointed_function(left) ? left : TypePtr{});
                return;
            }
            break;
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
    }

    // The element or member type a designator chain selects, if known.
    TypePtr designated_type(TypePtr type,
                            const std::vector<Expr::InitializerDesignator>& designators) const {
        for (const auto& designator : designators) {
            if (!type) return {};
            if (designator.kind == Expr::InitializerDesignator::Kind::Index) {
                type = type->kind == Type::Kind::Array ? qualified_element_type(type) : TypePtr{};
                continue;
            }
            const auto record = type->kind == Type::Kind::Record
                ? program_.record_definition(type->nominal_key()) : nullptr;
            if (!record) return {};
            const auto member = std::find_if(record->members.begin(), record->members.end(),
                [&](const RecordMemberDecl& candidate) {
                    return candidate.member_name() == designator.member_name();
                });
            type = member == record->members.end() ? TypePtr{} : member->type;
        }
        return type;
    }

    // Each entry of a brace list receives its element or member type, so a
    // function name in a table is adapted like any other initializer. As in
    // the initializer planner, the first designator of an entry sets the next
    // positional member, also for a chain such as `.a.first`.
    void rewrite_initializer(Expr& initializer, const TypePtr& destination) {
        const auto record = destination && destination->kind == Type::Kind::Record
            ? program_.record_definition(destination->nominal_key()) : nullptr;
        const auto named_members = [&](auto&& visit) {
            std::size_t logical = 0;
            for (const auto& member : record->members)
                if (!member.name.empty() && visit(member, logical++)) return;
        };
        std::optional<std::size_t> position = 0;
        for (auto& entry : initializer.initializer_entries) {
            for (auto& designator : entry.designators) rewrite(designator.index);
            TypePtr target;
            if (!entry.designators.empty()) {
                target = designated_type(destination, entry.designators);
                position.reset();
                const auto& first = entry.designators.front();
                if (record && first.kind == Expr::InitializerDesignator::Kind::Member) {
                    named_members([&](const RecordMemberDecl& member, std::size_t logical) {
                        if (member.member_name() != first.member_name()) return false;
                        position = logical + 1;
                        return true;
                    });
                }
            } else if (destination && destination->kind == Type::Kind::Array) {
                target = qualified_element_type(destination);
            } else if (record && position) {
                named_members([&](const RecordMemberDecl& member, std::size_t logical) {
                    if (logical != *position) return false;
                    target = member.type;
                    return true;
                });
                ++*position;
            }
            rewrite(entry.value, target);
        }
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
        for (auto& increment : statement.increments) rewrite(increment);
        if (statement.first) rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
        if (scoped) scopes_.pop_back();
    }

    Program& program_;
    std::string default_abi_;
    FunctionDecl* caller_{};
    std::string source_unit_;
    // Per source unit, so that one unit's adapters do not rename another's.
    std::unordered_map<std::string, std::uint64_t> ordinals_;
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
    const FunctionDecl* function{};

    bool matches(std::string_view spelling, const std::vector<TypePtr>& operands) const {
        return token == spelling && parameters.size() == operands.size() &&
            std::equal(parameters.begin(), parameters.end(), operands.begin(),
                [](const TypePtr& left, const TypePtr& right) { return same_type(left, right); });
    }

    bool same_function(const FunctionDecl& candidate) const {
        return function->name == candidate.name && function->linkage == candidate.linkage &&
            (function->linkage != Linkage::Static || function->source_unit == candidate.source_unit);
    }
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

const ObjectDecl* resolve_object(const Program& program,
                                 const FunctionDecl* caller,
                                 NameUse name) {
    const auto selected = value_namespace(program, caller, name);
    if (!selected) return nullptr;
    const auto unit = lookup_source_unit(name, caller);
    // A definition in the group, which completes an incomplete array
    // declaration, takes precedence over forward declarations.
    const auto definition = [](const ObjectDecl& object) {
        return object.initializer || object.linkage != Linkage::Group;
    };
    const auto exact = [&](std::string_view qualified) -> const ObjectDecl* {
        const ObjectDecl* shared{};
        for (const auto& candidate : program.objects) {
            if (candidate->name != qualified) continue;
            if (candidate->linkage == Linkage::Static) {
                if (unit.empty() || candidate->source_unit == unit) return candidate.get();
            } else if (!shared || (!definition(*shared) && definition(*candidate))) shared = candidate.get();
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
            if (enumeration.local) continue;
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

    bool refresh(const GenericExpansionState& state) {
        const auto errors = diagnostics_.errors();
        for (; next_function_ < program_.functions.size(); ++next_function_) {
            const auto& function = program_.functions[next_function_];
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
                auto type = clone_type(callable_parameter_type(parameter.type, ParameterMode::In));
                if (!normalize_generic_callable_abis(type, state, diagnostics_, parameter.location))
                    return false;
                parameters.push_back(std::move(type));
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
            const auto previous = std::find_if(bindings_.begin(), bindings_.end(),
                [&](const OperatorBinding& binding) { return binding.same_function(*function); });
            if (previous != bindings_.end() && !previous->matches(*token, parameters)) {
                diagnostics_.error(
                    attribute->location,
                    "operator binding redeclarations of '" +
                        function->name + "' disagree");
                continue;
            }
            const auto duplicate = std::find_if(bindings_.begin(), bindings_.end(),
                [&](const OperatorBinding& binding) { return binding.matches(*token, parameters); });
            if (duplicate != bindings_.end() && !duplicate->same_function(*function)) {
                diagnostics_.error(
                    attribute->location,
                    "duplicate exact operator binding for '" + *token + "'");
            } else if (duplicate == bindings_.end()) {
                bindings_.push_back({*token, std::move(parameters), function.get()});
            }
        }
        return diagnostics_.errors() == errors;
    }

    void rewrite(std::unique_ptr<Expr>& expression, const FunctionDecl* caller,
                 const GenericExpansionState& state) {
        if (!expression || bindings_.empty()) return;
        const auto infer = [&](const Expr& operand) {
            return infer_generic_actual(operand, caller, program_, state);
        };
        const unsigned arity = expression->kind == Expr::Kind::Unary ? 1
            : expression->kind == Expr::Kind::Binary ? 2 : 0;
        if (!operator_arity(expression->text, arity)) return;
        std::vector<TypePtr> parameters;
        if (expression->kind == Expr::Kind::Unary && expression->left) {
            parameters.push_back(infer(*expression->left));
        } else if (expression->kind == Expr::Kind::Binary &&
                   expression->left && expression->right) {
            parameters.push_back(infer(*expression->left));
            parameters.push_back(infer(*expression->right));
        } else {
            return;
        }
        if (std::any_of(parameters.begin(), parameters.end(),
                        [](const TypePtr& type) { return !type; })) {
            return;
        }
        if (std::none_of(parameters.begin(), parameters.end(), is_nominal)) return;
        for (auto& parameter : parameters) {
            parameter = clone_type(callable_parameter_type(parameter, ParameterMode::In));
            if (!normalize_generic_callable_abis(parameter, state, diagnostics_, expression->location)) return;
        }
        const auto found = std::find_if(bindings_.begin(), bindings_.end(),
            [&](const OperatorBinding& binding) { return binding.matches(expression->text, parameters); });
        if (found == bindings_.end()) return;

        auto call = std::make_unique<Expr>();
        call->kind = Expr::Kind::Call;
        call->location = expression->location;
        call->left = std::make_unique<Expr>();
        call->left->kind = Expr::Kind::Name;
        // The operation keeps its use-site diagnostic location; the selected
        // name keeps the declaration's unit, including a private binding in a
        // different compilation-group input. Do not redo namespace lookup.
        call->left->location = found->function->location;
        bind_exact_name(*call->left, found->function->name);
        call->arguments.push_back(std::move(expression->left));
        if (arity == 2) {
            call->arguments.push_back(std::move(expression->right));
        }
        expression = std::move(call);
    }

private:
    Program& program_;
    Diagnostics& diagnostics_;
    std::size_t next_function_{};
    std::vector<OperatorBinding> bindings_;
};

bool install_operator_bindings(Program& program, Diagnostics& diagnostics,
                               GenericExpansionState& state) {
    auto bindings = std::make_shared<OperatorBinder>(program, diagnostics);
    if (!bindings->refresh(state)) return false;
    state.rewrite_operator = [bindings = std::move(bindings), &state](
        std::unique_ptr<Expr>& expression, const FunctionDecl* caller) {
        bindings->rewrite(expression, caller, state);
    };
    return true;
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
        // Pointer and code-label representations are retained as typed values.
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

bool is_label_type(const TypePtr& type) {
    return type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Label;
}

std::shared_ptr<const LabelAddressConstant> resolve_label_constant(
    const Expr& source, const FunctionDecl* caller, Program& program,
    std::string* error = nullptr, const std::function<bool(SourceLocation)>& work = {});

bool evaluation_only(const FunctionDecl& function) {
    return function.attribute("eval_only") || function.has_meta_signature();
}

const char* label_address_error(const LabelAddressConstant& address) {
    // Type lookup may recognize a label in a translation-only body, but no
    // address value may survive erasure of that body. Do not retain a source
    // owner/declaration handle that can never become an emitted HIR label.
    if (!address.owner) return nullptr;
    if (evaluation_only(*address.owner))
        return "cannot take a code label address of a translation-only function";
    if (address.owner->attribute("always_inline"))
        return "taking a label address conflicts with always_inline on its owner";
    return nullptr;
}

std::unique_ptr<Expr> label_constant_expression(
    std::shared_ptr<const LabelAddressConstant> address, SourceLocation location) {
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Name;
    result->location = location;
    result->type = builtin_type(BuiltinType::Label);
    result->text = !address->global_name.empty() ? address->global_name
        : address->owner->name + "::" + address->definition->label_name;
    auto context = std::make_shared<NameLookupContext>();
    context->kind = NameLookupContext::Kind::Exact;
    context->label_address = std::move(address);
    result->name_context = std::move(context);
    return result;
}

struct EvalValue {
    UInt128 integer{};
    TypePtr type;
    std::optional<floating::Value> floating;
    std::shared_ptr<std::string> string{};
    std::size_t offset{};
    std::optional<AddressConstant> address;
    // Target representation for this typed address's null value. Opaque meta
    // pointers use backing identity instead; zero is only the no-resolver fallback.
    UInt128 null_address{};
    // A code location is symbolic, never a fabricated target address.
    std::shared_ptr<const LabelAddressConstant> label_address;
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
        if (address) return address->kind != AddressConstant::Kind::Absolute || address->absolute != null_address;
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

TypePtr source_string_type(const Expr& expression, bool decay) {
    const auto element = builtin_type(BuiltinType::U8, true);
    if (decay) return pointer_type(element);
    const auto text = expression.string_value.empty() ? decode_string_literal(expression.text)
        : std::optional<std::string>(expression.string_value);
    if (!text || text->size() >= std::numeric_limits<std::uint32_t>::max()) return {};
    return array_type(element, static_cast<std::uint32_t>(text->size() + 1));
}

// Reject source-invalid queries without requesting target layout or evaluating
// operands. Array extents may still be pending (or captured VLAs); their eventual
// sizes remain the responsibility of the existing target/evaluator query path.
bool invalid_layout_query_type(const TypePtr& type, const Program& program,
                               bool size_query, bool array_element = false) {
    if (!type) return false;
    if (type->kind == Type::Kind::Function ||
        (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void)) return true;
    if (type->kind == Type::Kind::Vector && type->scalable) return size_query || array_element;
    if (type->kind == Type::Kind::Array)
        return invalid_layout_query_type(type->element, program, size_query, true);
    if (type->kind == Type::Kind::Record)
        return !program.record_definition(type->nominal_key());
    // In particular, pointers do not require a complete or fixed-size pointee.
    return false;
}

template<class TypeOf>
const char* source_layout_query_error(const Expr& node, const TypePtr& queried,
                                      const Program& program, const TypeOf& type_of) {
    if (contains_meta_type(queried)) return "meta values have no runtime size or alignment";
    const bool size_query = node.kind == Expr::Kind::Sizeof;
    // An explicitly written type has no object initializer or captured VLA
    // extent to complete an omitted bound. Retained bound expressions may still
    // need their ordinary required-expression preparation.
    if (size_query && node.type) {
        for (auto array = queried; array && array->kind == Type::Kind::Array; array = array->element)
            if (array->lanes == 0 && !array->array_bound)
                return "sizeof requires a complete object type with fixed size";
    }
    if (invalid_layout_query_type(queried, program, size_query))
        return size_query ? "sizeof requires a complete object type with fixed size"
                          : "$::alignof requires a complete object type";
    if (!node.type && node.left) {
        const auto* member = source_record_member(*node.left, program, type_of);
        if (member && member->bit_width)
            return size_query ? "sizeof cannot be applied to a bit-field"
                              : "$::alignof cannot be applied to a bit-field";
    }
    return nullptr;
}

template<class TypeOf>
const Expr* invalid_unquote_argument(const Expr& quote, const TypeOf& type_of) {
    for (const auto& argument : quote.arguments) {
        const auto type = type_of(*argument);
        if (!type || (type->kind != Type::Kind::Tokens && type->kind != Type::Kind::Syntax))
            return argument.get();
    }
    return nullptr;
}

template<class TypeOf>
TypePtr infer_generic_actual_step(const Expr& expression,
                             const FunctionDecl* caller,
                             Program& program,
                             const GenericExpansionState& state,
                             bool decay, const TypeOf& type_of) {
    const auto adjusted = [&](TypePtr type) -> TypePtr {
        if (!type || !decay) return type;
        if (type->kind == Type::Kind::Array)
            return pointer_type(qualified_element_type(type));
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
        if (left_vector && right_vector && !compatible_vector_shape(left, right)) return {};
        const auto& shape = deferred_vector_extent(right) ? right : left_vector ? left : right;
        const auto element = common_scalar_numeric(
            left_vector ? left->element : left,
            right_vector ? right->element : right);
        return element ? vector_result_type(element, shape)
                       : TypePtr{};
    };
    const auto vector_mask = [&](const TypePtr& type) -> TypePtr {
        if (!type || type->kind != Type::Kind::Vector || !type->element)
            return {};
        const auto bits = type->element->builtin == BuiltinType::Fptr ||
            type->element->builtin == BuiltinType::Iptr || type->element->builtin == BuiltinType::Uptr
            ? program.address_bits : type_bits(type->element);
        const auto mask = bits == 8 ? BuiltinType::I8
            : bits == 16 ? BuiltinType::I16
            : bits == 32 ? BuiltinType::I32
            : bits == 64 ? BuiltinType::I64
            : BuiltinType::Void;
        return mask == BuiltinType::Void ? TypePtr{}
            : vector_result_type(builtin_type(mask), type);
    };
    switch (expression.kind) {
    case Expr::Kind::VoidValue: return builtin_type(BuiltinType::Void);
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
        return source_string_type(expression, decay);
    case Expr::Kind::Quote:
        return tokens_type();
    case Expr::Kind::ByteSequence:
        return expression.type ? expression.type : bytes_type();
    case Expr::Kind::Address:
        return expression.type;
    case Expr::Kind::Name: {
        if (expression.deferred_generic_signature) return adjusted(expression.deferred_generic_signature);
        if (expression.name_context && expression.name_context->label_address)
            return builtin_type(BuiltinType::Label);
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
            auto type = source_function_type(*function);
            if (!canonicalize_callable_abis(type, program.canonical_callable_abi)) return {};
            return adjusted(type);
        }
        if (resolve_label_constant(expression, caller, program))
            return builtin_type(BuiltinType::Label);
        return {};
    }
    case Expr::Kind::Parenthesized:
        return expression.left
                   ? type_of(*expression.left, decay)
                   : TypePtr{};
    case Expr::Kind::Cast:
        return adjusted(expression.type);
    case Expr::Kind::Sizeof:
    case Expr::Kind::Alignof:
    case Expr::Kind::Offsetof:
        return builtin_type(BuiltinType::Uptr);
    case Expr::Kind::Assign:
        return expression.left
                   ? type_of(*expression.left, false)
                   : TypePtr{};
    case Expr::Kind::Unary: {
        if (!expression.left) return {};
        auto operand = type_of(*expression.left, expression.text != "&");
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
            TypePtr base;
            const auto* member = source_record_member(expression, program,
                [&](const Expr& operand) { return type_of(operand, false); },
                &base);
            if (!member) return {};
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
            const auto compared_left = type_of(*expression.left);
            const auto compared_right = type_of(*expression.right);
            if (!compared_left || !compared_right) return {};
            if (compared_left->kind != Type::Kind::Vector &&
                compared_right->kind != Type::Kind::Vector)
                return builtin_type(BuiltinType::Bool);
            const auto common = common_numeric(compared_left, compared_right);
            if (!common || common->kind != Type::Kind::Vector ||
                !common->element) return {};
            return vector_mask(common);
        }
        const auto left = type_of(*expression.left);
        if (expression.text == "index") {
            const auto index = type_of(*expression.right);
            if (!left || !is_integer(index)) return {};
            return left->kind == Type::Kind::Pointer ||
                           left->kind == Type::Kind::Array ||
                           left->kind == Type::Kind::Vector
                       ? adjusted(left->kind == Type::Kind::Pointer
                                      ? left->pointee
                                      : qualified_element_type(left))
                       : TypePtr{};
        }
        const auto right = type_of(*expression.right);
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
                : vector_result_type(clone_type(left->element), left);
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
        const auto yes = type_of(*expression.right);
        const auto no = type_of(*expression.third);
        if (is_meta_type(yes) || is_meta_type(no)) return conditional_meta_type(yes, no);
        if ((yes && yes->kind == Type::Kind::Pointer) || (no && no->kind == Type::Kind::Pointer))
            return conditional_pointer_type(yes, no);
        if (same_type(yes, no)) return yes;
        return common_numeric(yes, no);
    }
    case Expr::Kind::Call: {
        if (!expression.left) return {};
        if (auto type = translation_intrinsic_type(expression, true,
                [&](const Expr& argument) {
                    return type_of(argument);
                })) return adjusted(type);
        auto callee = type_of(*expression.left);
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

TypePtr infer_generic_actual(const Expr& expression, const FunctionDecl* caller,
                             Program& program, const GenericExpansionState& state, bool decay,
                             SourceTypeMemo* memo) {
    const auto resources = program.evaluation_resource_errors;
    return classify_source_type(expression, decay,
        [&](const Expr& source, bool child_decay, const auto& type_of) {
            return infer_generic_actual_step(source, caller, program, state, child_decay, type_of);
        }, [&] { return program.evaluation_resource_errors != resources; }, memo);
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

IntegerType evaluation_integer_type(const TypePtr& type, unsigned address_bits) {
    auto bits = type_bits(type);
    if (type && (type->kind == Type::Kind::Pointer ||
        (type->kind == Type::Kind::Builtin &&
         (type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr ||
          type->builtin == BuiltinType::Label))))
        bits = address_bits;
    return {bits, signed_value(EvalValue{UInt128{}, type}),
            type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool};
}

std::optional<std::uint32_t> fixed_array_bound_value(Expr::IntegerConstant value, unsigned address_bits) {
    const auto integer = evaluation_integer_type(builtin_type(value.type), address_bits);
    if (integer_negative(value.value, integer) || value.value.high || !value.value.low ||
        value.value.low > std::numeric_limits<std::uint32_t>::max()) return {};
    return static_cast<std::uint32_t>(value.value.low);
}

void replace_eval_value(std::unique_ptr<Expr>& expression, const EvalValue& value);
std::string literal_suffix(const TypePtr& type);

// Source object designators are independent of whether their effects may be
// evaluated during translation. Keep preflight and execution classification
// consistent, including members/lanes of temporary aggregate values.
template<class TypeOf, class ObjectName>
bool source_object_lvalue(const Expr& node, const TypeOf& type_of, const ObjectName& object_name) {
    if (node.kind == Expr::Kind::Parenthesized && node.left)
        return source_object_lvalue(*node.left, type_of, object_name);
    if (node.kind == Expr::Kind::Name) return object_name(node);
    if (node.kind == Expr::Kind::Unary && node.text == "*" && node.left) {
        const auto type = type_of(*node.left);
        return type && type->kind == Type::Kind::Pointer && type->pointee &&
            type->pointee->kind != Type::Kind::Function &&
            !(type->pointee->kind == Type::Kind::Builtin && type->pointee->builtin == BuiltinType::Void);
    }
    if (node.kind != Expr::Kind::Binary || !node.left) return false;
    if (node.text == "member") return source_object_lvalue(*node.left, type_of, object_name);
    const auto base = type_of(*node.left);
    if (!base) return false;
    if (node.text == "pointer_member")
        return base->kind == Type::Kind::Pointer && base->pointee &&
            base->pointee->kind == Type::Kind::Record;
    if (node.text == "index")
        return base->kind == Type::Kind::Pointer ||
            ((base->kind == Type::Kind::Array || base->kind == Type::Kind::Vector) &&
             source_object_lvalue(*node.left, type_of, object_name));
    return false;
}

template<class TypeOf, class ObjectLvalue>
const char* source_address_error(const Expr& node, const Program& program,
                                 const TypeOf& type_of, const ObjectLvalue& object_lvalue) {
    if (node.kind != Expr::Kind::Unary || node.text != "&" || !node.left) return nullptr;
    const auto operand = type_of(*node.left);
    if (!operand || operand->kind == Type::Kind::Generic || is_meta_type(operand)) return nullptr;
    const auto* member = source_record_member(*node.left, program, type_of);
    if (member && member->bit_width) return "cannot take the address of a bit-field";
    // Function designators and literal backing arrays are addressable without
    // being writable objects. This check does not read storage or ask for layout.
    if (operand->kind == Type::Kind::Function) return nullptr;
    const Expr* designator = node.left.get();
    while (designator->kind == Expr::Kind::Parenthesized && designator->left)
        designator = designator->left.get();
    if (designator->kind == Expr::Kind::String) return nullptr;
    if ((operand->kind == Type::Kind::Builtin && operand->builtin == BuiltinType::Void) ||
        !object_lvalue(*node.left))
        return designator->kind == Expr::Kind::Binary && designator->text == "member"
            ? "record value is not an object designator"
            : "address-of requires an object or function lvalue";
    return nullptr;
}

bool incompatible_records(const TypePtr& left, const TypePtr& right) {
    // Value copying ignores top-level value qualifiers, not nominal identity.
    if (!left || !right || left->kind == Type::Kind::Generic || right->kind == Type::Kind::Generic)
        return false;
    if (left->kind != Type::Kind::Record && right->kind != Type::Kind::Record) return false;
    return left->kind != right->kind || left->is_union != right->is_union ||
        left->nominal_key() != right->nominal_key();
}

struct SourceExpressionIssue {
    SourceLocation location;
    std::string message;
};

// One step of a $::offsetof designator over source member shapes. Callers
// resolve layout and index values; `record` retains the member's owner.
struct OffsetofStep {
    const Expr::InitializerDesignator* designator{};
    TypePtr owner;
    std::shared_ptr<const RecordDecl> record;
    const RecordMemberDecl* member{};
};

std::optional<SourceExpressionIssue> source_offsetof_error(const Expr& node, const Program& program,
                                                          std::vector<OffsetofStep>* steps = nullptr) {
    auto type = node.type;
    if (!type || type->kind == Type::Kind::Generic) return {};
    if (type->kind != Type::Kind::Record || !program.record_definition(type->nominal_key()))
        return SourceExpressionIssue{node.location, "$::offsetof requires a complete record type"};
    for (const auto& designator : node.initializer_entries.front().designators) {
        if (!type || type->kind == Type::Kind::Generic) return {};
        OffsetofStep step{&designator, type, {}, nullptr};
        if (designator.kind == Expr::InitializerDesignator::Kind::Index) {
            if (type->kind != Type::Kind::Array)
                return SourceExpressionIssue{designator.location, "$::offsetof index requires an array member"};
            type = type->element;
        } else {
            step.record = type->kind == Type::Kind::Record
                ? program.record_definition(type->nominal_key()) : nullptr;
            if (!step.record)
                return SourceExpressionIssue{designator.location, "$::offsetof member designator requires a record"};
            const auto member = std::find_if(step.record->members.begin(), step.record->members.end(),
                [&](const RecordMemberDecl& candidate) { return candidate.member_name() == designator.member_name(); });
            if (member == step.record->members.end())
                return SourceExpressionIssue{designator.location,
                    "record has no member named '" + designator.member + "'"};
            if (member->bit_width)
                return SourceExpressionIssue{designator.location, "$::offsetof cannot be applied to a bit-field"};
            step.member = &*member;
            type = member->type;
        }
        if (steps) steps->push_back(std::move(step));
    }
    return {};
}

// A designator need not read its object. In particular, &*p can preserve an
// incomplete record pointer, whereas an ordinary value use must know the
// complete object type. This role is independent of execution/unevaluation.
enum class SourceExpressionUse { Value, Designator };

SourceExpressionUse source_left_use(const Expr& node, SourceExpressionUse inherited) {
    if (node.kind == Expr::Kind::Parenthesized) return inherited;
    if (node.kind == Expr::Kind::Sizeof || node.kind == Expr::Kind::Alignof ||
        (node.kind == Expr::Kind::Unary && node.text == "&") ||
        (node.kind == Expr::Kind::Assign && node.text == "=") ||
        (node.kind == Expr::Kind::Binary && node.text == "member"))
        return SourceExpressionUse::Designator;
    return SourceExpressionUse::Value;
}

// Source completeness for operations that scale by an object's size. Retained
// bounds and invocation-dependent extents are promises, not invented strides;
// independent invalid element types still reject before that promise resolves.
bool source_scaled_object_type(const TypePtr& object, const Program& program) {
    if (!object || object->kind == Type::Kind::Generic) return true;
    if (invalid_layout_query_type(object, program, true)) return false;
    for (auto array = object; array && array->kind == Type::Kind::Array; array = array->element)
        if (array->lanes == 0 && !array->array_bound &&
            array->array_extent_dependency != Type::ArrayExtentDependency::ExpansionContext) return false;
    return true;
}

// Object/value legality survives unevaluated operands and enclosing scalar
// casts. This only queries source types and bindings; it never reads storage.
template<class TypeOf, class ObjectLvalue>
std::optional<SourceExpressionIssue> source_object_expression_error(const Expr& node,
    const Program& program, const TypeOf& type_of, const ObjectLvalue& object_lvalue,
    SourceExpressionUse use) {
    const auto error = [&](std::string message) -> std::optional<SourceExpressionIssue> {
        return SourceExpressionIssue{node.location, std::move(message)};
    };
    const bool member = node.kind == Expr::Kind::Binary &&
        (node.text == "member" || node.text == "pointer_member");
    if (member && node.left && !is_meta_type(type_of(*node.left, false))) {
        SourceMemberIssue issue{};
        (void)source_record_member(node, program,
            [&](const Expr& operand) { return type_of(operand, false); }, nullptr, &issue);
        switch (issue) {
        case SourceMemberIssue::NotPointer: return error("pointer member access requires a pointer to a record");
        case SourceMemberIssue::NotRecord: return error("member access requires a record object");
        case SourceMemberIssue::IncompleteRecord: return error("member access requires a complete record type");
        case SourceMemberIssue::MissingMember:
            return SourceExpressionIssue{node.right->location,
                "record has no member named '" + node.right->text + "'"};
        default: break;
        }
    } else if (node.kind == Expr::Kind::Binary && node.text == "index" && node.left && node.right) {
        const auto base = type_of(*node.left, true), index = type_of(*node.right, true);
        if (base && !is_meta_type(base) && base->kind != Type::Kind::Pointer &&
            base->kind != Type::Kind::Array && base->kind != Type::Kind::Vector &&
            base->kind != Type::Kind::Generic)
            return error("subscript requires an array, pointer, or vector");
        if (index && !is_meta_type(index) && !is_integer(index))
            return SourceExpressionIssue{node.right->location, "subscript index must have an integer type"};
        const auto element = base && base->kind == Type::Kind::Pointer ? base->pointee
            : base && (base->kind == Type::Kind::Array || base->kind == Type::Kind::Vector) ? base->element
            : TypePtr{};
        if (!source_scaled_object_type(element, program))
            return error("subscript requires a complete object element type");
    } else if (node.kind == Expr::Kind::Unary && node.text == "*" && node.left) {
        const auto operand = type_of(*node.left, true);
        if (operand && !is_meta_type(operand) && operand->kind != Type::Kind::Pointer)
            return error("dereference requires a pointer operand");
        if (operand && operand->kind == Type::Kind::Pointer && operand->pointee &&
            operand->pointee->kind == Type::Kind::Builtin && operand->pointee->builtin == BuiltinType::Void)
            return error("dereference requires a pointer to an object or function type");
    }
    if (const auto* reason = source_address_error(node, program,
            [&](const Expr& operand) { return type_of(operand, false); }, object_lvalue))
        return error(reason);
    const bool modifying = node.kind == Expr::Kind::Assign ||
        (node.kind == Expr::Kind::Unary &&
         (node.text == "++" || node.text == "--" || node.text == "post++" || node.text == "post--"));
    if (modifying && node.left) {
        const auto destination = type_of(*node.left, false);
        if (destination && !is_meta_type(destination)) {
            const auto* designator = node.left.get();
            while (designator->kind == Expr::Kind::Parenthesized && designator->left)
                designator = designator->left.get();
            if (!object_lvalue(*node.left) || destination->kind == Type::Kind::Function ||
                (destination->kind == Type::Kind::Builtin && destination->builtin == BuiltinType::Void))
                return error(designator->kind == Expr::Kind::Binary && designator->text == "member"
                    ? "record value is not an object designator" : "assignment or update requires an object lvalue");
            if (destination->is_const)
                return error(designator->kind == Expr::Kind::Name ? "cannot write a const cell"
                                                                : "cannot write a const subobject");
            if (destination->kind == Type::Kind::Record &&
                (node.kind == Expr::Kind::Unary || node.text != "="))
                return error("record values do not support update or compound assignment");
        }
    }
    const auto record_value = [&](const Expr& expression) {
        const auto type = type_of(expression, true);
        return type && type->kind == Type::Kind::Record;
    };
    if ((node.kind == Expr::Kind::Unary && node.left &&
         (node.text == "+" || node.text == "-" || node.text == "~" || node.text == "!") && record_value(*node.left)) ||
        (node.kind == Expr::Kind::Binary && !member &&
         ((node.left && record_value(*node.left)) || (node.right && record_value(*node.right)))))
        return error("built-in operator '" + node.text + "' cannot consume record values");
    if (node.kind == Expr::Kind::Conditional && node.right && node.third &&
        incompatible_records(type_of(*node.right, true), type_of(*node.third, true)))
        return error("conditional record operands must have the same nominal record type");
    if (node.kind == Expr::Kind::Cast && node.left &&
        incompatible_records(type_of(*node.left, true), node.type))
        return error("a record value requires the same nominal record type in cast");
    if (node.kind == Expr::Kind::Assign && node.text == "=" && node.left && node.right &&
        incompatible_records(type_of(*node.left, false), type_of(*node.right, true)))
        return error("a record value requires the same nominal record type in assignment");
    // Parentheses forward the use role to their child rather than duplicating
    // type lookup. Arrays decay without reading a whole array, and function
    // designators do not read an object representation.
    if (use == SourceExpressionUse::Value && (node.kind == Expr::Kind::Name ||
        (node.kind == Expr::Kind::Unary && node.text == "*") ||
        (node.kind == Expr::Kind::Binary && (member || node.text == "index")))) {
        const auto object = type_of(node, false);
        if (object && object->kind == Type::Kind::Record &&
            !program.record_definition(object->nominal_key()))
            return error("value access requires a complete object type");
    }
    return {};
}

// Parts of a callable interface that an adapter can bridge for a named function.
enum CallableInterfacePart : unsigned {
    CallableAbi = 1U << 0,
    CallableEndpoints = 1U << 1,
    CallableResultLocation = 1U << 2,
    CallableClobbers = 1U << 3,
    CallableCleanup = 1U << 4,
    CallableInterface = (1U << 5) - 1,
};

void adopt_callable_interface(FunctionType& target, const FunctionType& source, unsigned parts) {
    if (parts & CallableAbi) target.abi = source.abi;
    if (parts & CallableEndpoints)
        for (std::size_t index = 0; index < target.parameters.size(); ++index)
            target.parameters[index].location_name = source.parameters[index].location_name;
    if (parts & CallableResultLocation) target.result_location = source.result_location;
    if (parts & CallableClobbers) target.clobbers = source.clobbers;
    if (parts & CallableCleanup) target.stack_cleanup = source.stack_cleanup;
}

// The interface parts in which two function types differ, or zero when their
// results, parameter types, modes, or variadic forms differ as well.
unsigned callable_interface_differences(const TypePtr& source, const TypePtr& destination) {
    if (!source->function || !destination->function ||
        source->function->parameters.size() != destination->function->parameters.size())
        return 0;
    const auto agrees_with = [&](unsigned parts) {
        auto adapted = clone_type(source);
        adopt_callable_interface(*adapted->function, *destination->function, parts);
        return compare_source_types(adapted, destination) != TypeComparison::Different;
    };
    if (!agrees_with(CallableInterface)) return 0;
    unsigned differences = 0;
    for (unsigned part = 1; part < CallableInterface; part <<= 1)
        if (!agrees_with(CallableInterface & ~part)) differences |= part;
    return differences;
}

// One message per combination of differing parts: the prefix followed by a
// list such as "ABI and result location".
std::array<std::string, CallableInterface + 1> callable_interface_messages(std::string_view prefix) {
    constexpr std::array<std::string_view, 5> names{
        "ABI", "parameter endpoints", "result location", "clobbers", "stack cleanup"};
    std::array<std::string, CallableInterface + 1> result;
    for (unsigned mask = 1; mask <= CallableInterface; ++mask) {
        std::vector<std::string_view> parts;
        for (unsigned bit = 0; bit < names.size(); ++bit)
            if (mask & (1U << bit)) parts.push_back(names[bit]);
        auto& text = result[mask];
        text = prefix;
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (index != 0)
                text += index + 1 < parts.size() ? ", " : parts.size() > 2 ? ", and " : " and ";
            text += parts[index];
        }
    }
    return result;
}

// Only a named function can be adapted; a held value keeps its interface.
const char* callable_value_conversion_error(unsigned differences) {
    static const auto messages =
        callable_interface_messages("a function-pointer value cannot change its callable ");
    return messages[differences & CallableInterface].c_str();
}

// A conditional chooses no adapter, so its arms must share one interface.
const char* conditional_callable_error(unsigned differences) {
    static const auto messages =
        callable_interface_messages("conditional function-pointer operands differ in their callable ");
    return messages[differences & CallableInterface].c_str();
}

// Source operator constraints are independent of execution and target layout.
// Share these between erased-body validation and required/unevaluated trees.
template<class TypeOf, class NullInteger>
const char* source_operator_error(const Expr& node, const Program& program,
                                  const TypeOf& type_of, const NullInteger& null_integer) {
    if (node.kind == Expr::Kind::Cast && node.left && node.type &&
        node.type->kind == Type::Kind::Pointer && is_integer(type_of(*node.left))) {
        // Retain source-constant zero before optional folding and MIR's
        // integer/address width checks. Other explicit integer casts keep
        // their model-aware conversion path; this is not a runtime-zero test.
        (void)null_integer(*node.left);
        return nullptr;
    }
    const auto pending = [](const TypePtr& type) {
        return !type || type->kind == Type::Kind::Generic || is_meta_type(type) || type->kind == Type::Kind::Record;
    };
    const auto numeric = [](const TypePtr& type) { return is_integer(type) || is_floating(type); };
    const auto void_type = [](const TypePtr& type) {
        return type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void;
    };
    const auto mismatched_vectors = [](const TypePtr& a, const TypePtr& b) {
        return is_vector(a) && is_vector(b) &&
            (a->scalable != b->scalable || (a->lanes && b->lanes && a->lanes != b->lanes));
    };
    if (node.kind == Expr::Kind::Conditional && node.left && node.right && node.third) {
        const auto selector = type_of(*node.left), yes = type_of(*node.right), no = type_of(*node.third);
        // Opaque and nominal values retain their dedicated compatibility checks.
        if (selector && selector->kind != Type::Kind::Generic && !is_scalar(selector))
            return is_meta_type(selector) || is_meta_type(yes) || is_meta_type(no)
                ? "conditional meta selector must be scalar" : "conditional selector must be scalar";
        if (pending(yes) || pending(no)) return nullptr;
        if (is_label_type(yes) || is_label_type(no))
            return is_label_type(yes) && is_label_type(no) ? nullptr
                : "conditional label operands must both have label type and a scalar selector";
        if (void_type(yes) || void_type(no))
            return void_type(yes) && void_type(no) ? nullptr
                : "conditional operands must both be void or both produce values";
        if (mismatched_vectors(yes, no))
            return "conditional vector operands require matching lane counts and scalable shape";
        if (is_vector(yes) || is_vector(no)) {
            const auto a = is_vector(yes) ? yes->element : yes;
            const auto b = is_vector(no) ? no->element : no;
            if (pending(a) || pending(b)) return nullptr;
            return numeric(a) && numeric(b) ? nullptr
                : "conditional vector operands require numeric elements or scalar operands";
        }
        if ((yes->kind == Type::Kind::Pointer && is_floating(no)) ||
            (no->kind == Type::Kind::Pointer && is_floating(yes)))
            return "conditional operands cannot mix pointer and floating types";
        if (yes->kind == Type::Kind::Pointer || no->kind == Type::Kind::Pointer) {
            if (yes->kind == Type::Kind::Pointer && no->kind == Type::Kind::Pointer) {
                if (common_pointer_type(yes, no).type) return nullptr;
                if (yes->pointee && no->pointee && yes->pointee->kind == Type::Kind::Function &&
                    no->pointee->kind == Type::Kind::Function)
                    if (const auto differences = callable_interface_differences(yes->pointee, no->pointee))
                        return conditional_callable_error(differences);
                return "conditional pointer operands have no compatible common type";
            }
            auto& integer = yes->kind == Type::Kind::Pointer ? *node.third : *node.right;
            const auto integer_type = yes->kind == Type::Kind::Pointer ? no : yes;
            return is_integer(integer_type) && null_integer(integer) ? nullptr
                : "conditional pointer/integer operands require an integer constant zero";
        }
        return nullptr;
    }
    const bool unary = node.kind == Expr::Kind::Unary;
    const bool compound = node.kind == Expr::Kind::Assign && node.text != "=";
    if ((!unary && !compound && node.kind != Expr::Kind::Binary) || !node.left) return nullptr;
    std::string_view operation = node.text;
    if (compound) operation.remove_suffix(1);
    const bool update = unary && (operation == "++" || operation == "--" ||
                                  operation == "post++" || operation == "post--");
    const bool integral = operation == "~" || operation == "%" || operation == "&" ||
        operation == "|" || operation == "^" || operation == "<<" || operation == ">>";
    const bool arithmetic = operation == "+" || operation == "-" || operation == "*" || operation == "/";
    const bool comparison = !unary && (operation == "==" || operation == "!=" ||
        operation == "<" || operation == "<=" || operation == ">" || operation == ">=");
    const bool logical = unary ? operation == "!" : operation == "&&" || operation == "||";
    if ((!integral && !arithmetic && !update && !comparison && !logical) ||
        (unary && (operation == "&" || operation == "*"))) return nullptr;
    const auto left = type_of(*node.left);
    const auto right = !unary && node.right ? type_of(*node.right) : TypePtr{};
    if (pending(left) || (!unary && pending(right))) return nullptr;
    if (logical) {
        if (unary && is_vector(left))
            return pending(left->element) || numeric(left->element) ? nullptr
                : "vector logical negation requires numeric elements";
        return is_scalar(left) && (unary || is_scalar(right)) ? nullptr
            : "logical operator requires scalar operands";
    }
    if (comparison && (is_label_type(left) || is_label_type(right)))
        return (operation == "==" || operation == "!=") && is_label_type(left) && is_label_type(right)
            ? nullptr : "label comparisons require == or != between two label operands";
    const auto complete_pointee = [&](const TypePtr& pointer) {
        return source_scaled_object_type(pointer->pointee, program);
    };
    const bool left_pointer = left->kind == Type::Kind::Pointer;
    const bool right_pointer = right && right->kind == Type::Kind::Pointer;
    if (comparison && (left_pointer || right_pointer) && !is_vector(left) && !is_vector(right)) {
        if (is_floating(left) || is_floating(right))
            return "comparison cannot mix pointer and floating types";
        if (left_pointer != right_pointer && is_integer(left_pointer ? right : left)) {
            if (operation != "==" && operation != "!=")
                return "ordered pointer comparison requires two pointer operands";
            if (!null_integer(left_pointer ? *node.right : *node.left))
                return "pointer/integer equality requires an integer constant zero";
            return nullptr;
        }
        // Common pointer type and address representation remain model-owned.
        return (left_pointer || is_integer(left)) && (right_pointer || is_integer(right))
            ? nullptr : "comparison requires numeric or pointer operands";
    }
    if (update && left_pointer)
        return complete_pointee(left) ? nullptr : "pointer arithmetic requires a complete pointed-to object type";
    if (compound && left_pointer && operation != "+" && operation != "-")
        return "pointer compound assignment requires += or -=";
    if (!unary && (operation == "+" || operation == "-") && (left_pointer || right_pointer)) {
        if (compound && !left_pointer) return "scalar operator requires numeric operands";
        if (left_pointer && right_pointer) {
            if (operation != "-" || compound || left->address_space != right->address_space ||
                (compare_pointee(left->pointee, right->pointee) == PointeeCompatibility::Incompatible &&
                 compare_pointee(right->pointee, left->pointee) == PointeeCompatibility::Incompatible))
                return "pointer subtraction requires matching pointer types";
            return complete_pointee(left) && complete_pointee(right) ? nullptr
                : "pointer subtraction requires a complete pointed-to object type";
        }
        if ((!left_pointer && (operation != "+" || compound)) ||
            !is_integer(left_pointer ? right : left))
            return "pointer arithmetic requires one pointer and one integer operand";
        return complete_pointee(left_pointer ? left : right) ? nullptr
            : "pointer arithmetic requires a complete pointed-to object type";
    }
    const auto a = is_vector(left) ? left->element : left;
    const auto b = is_vector(right) ? right->element : right;
    if (pending(a) || (!unary && pending(b))) return nullptr;
    if (mismatched_vectors(left, right))
        return "vector operator requires matching lane counts and scalable shape";
    if (comparison)
        return numeric(a) && numeric(b) ? nullptr
            : is_vector(left) || is_vector(right) ? "vector comparison requires numeric operands"
                                                 : "comparison requires numeric or pointer operands";
    if (integral)
        return is_integer(a) && (unary || is_integer(b)) ? nullptr
            : is_vector(left) || is_vector(right) ? "vector operator requires integer operands"
                                                 : "operator requires integer operands";
    return numeric(a) && (unary || numeric(b)) ? nullptr
        : is_vector(left) || is_vector(right) ? "vector operator requires numeric operands"
                                             : "scalar operator requires numeric operands";
}

// The pure rule checker requests at most one integer-zero proof. Suspend for
// that proof outside its synchronous type callbacks, then apply the same rule
// with the retained result. Type queries themselves execute no source effects.
template<class TypeOf, class NullInteger>
EvaluationTask<const char*> source_operator_error_async(const Expr& node, const Program& program,
    const TypeOf& type_of, const NullInteger& null_integer) {
    Expr* requested{};
    const auto* reason = source_operator_error(node, program, type_of,
        [&](Expr& operand) { requested = &operand; return false; });
    if (!requested) co_return reason;
    const bool zero = co_await null_integer(*requested);
    co_return source_operator_error(node, program, type_of, [&](Expr&) { return zero; });
}

// Eval-only bodies and opaque values disappear before runtime HIR. Check their
// source type boundaries independently of execution, including untaken branches.
// This deliberately does not decide whether an ordinary operation's effects
// can execute during translation; only the reached evaluator path does that.
bool source_constant_candidate(const Expr& expression);

enum class SourceConversion { Implicit, Explicit };

bool is_void_type(const TypePtr& type) {
    return type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void;
}

const char* source_value_error(const TypePtr& type) {
    return is_void_type(type) ? "void expression cannot supply a value" : nullptr;
}



// Target-independent constraints only. Integer/address representation and
// cross-address-space conversions remain owned by the resolved target. This
// check must not execute a source value, including in an untaken helper branch.
const char* source_conversion_error(const TypePtr& source, const TypePtr& destination,
                                    SourceConversion conversion = SourceConversion::Implicit,
                                    bool direct_function = false) {
    if (!source || !destination || source->kind == Type::Kind::Generic ||
        destination->kind == Type::Kind::Generic) return nullptr;
    // A void expression may forward no result, but never becomes a scalar,
    // pointer or aggregate value merely because execution is skipped.
    if (!is_void_type(destination))
        if (const auto* reason = source_value_error(source)) return reason;
    if (is_meta_type(source) ||
        is_meta_type(destination) || source->kind == Type::Kind::Record ||
        destination->kind == Type::Kind::Record || is_label_type(source) ||
        is_label_type(destination)) return nullptr;
    const bool source_pointer = source->kind == Type::Kind::Pointer;
    const bool destination_pointer = destination->kind == Type::Kind::Pointer;
    if ((source_pointer && is_floating(destination)) ||
        (destination_pointer && is_floating(source)))
        return conversion == SourceConversion::Explicit
            ? "explicit cast cannot convert between a pointer and a non-integer type"
            : "conversion cannot convert between a pointer and a floating type";
    // ABI spellings have been canonicalized by the resolved model. An existing
    // pointer value must keep its complete stable interface. A known
    // non-variadic function designator can instead use an adapter when only
    // its interface differs; no adapter is inferred for an indirect value.
    // An explicit cast reinterprets any function pointer as another.
    if (source_pointer && destination_pointer && source->pointee && destination->pointee &&
        source->pointee->kind == Type::Kind::Function &&
        destination->pointee->kind == Type::Kind::Function) {
        if (conversion == SourceConversion::Explicit ||
            compare_pointee(source->pointee, destination->pointee) != PointeeCompatibility::Incompatible) return nullptr;
        const bool variadic = source->pointee->function && source->pointee->function->variadic;
        if (const auto differences = callable_interface_differences(source->pointee, destination->pointee);
            differences && !(direct_function && variadic))
            return direct_function ? nullptr : callable_value_conversion_error(differences);
        return conversion == SourceConversion::Explicit
            ? "explicit pointer conversion discards qualifiers or uses incompatible pointee types"
            : "implicit pointer conversion discards qualifiers or uses incompatible pointee types";
    }
    // Function and object pointers remain distinct even for explicit casts.
    // Do not apply all runtime object-pointee restrictions here: translation-
    // time byte backing permits typed views, checked by the evaluator. No
    // registered ABI currently defines a function/object pointer bridge.
    const bool function_object_cast = source_pointer && destination_pointer &&
        source->pointee && destination->pointee &&
        ((source->pointee->kind == Type::Kind::Function) !=
         (destination->pointee->kind == Type::Kind::Function));
    if (source_pointer && destination_pointer &&
        (conversion == SourceConversion::Implicit || function_object_cast) &&
        compare_pointee(source->pointee, destination->pointee) == PointeeCompatibility::Incompatible)
        return conversion == SourceConversion::Explicit
            ? "explicit pointer conversion discards qualifiers or uses incompatible pointee types"
            : "implicit pointer conversion discards qualifiers or uses incompatible pointee types";
    if (is_vector(destination)) {
        if (is_vector(source) && (source->scalable != destination->scalable ||
            (source->lanes && destination->lanes && source->lanes != destination->lanes)))
            return "vector conversion requires matching lane counts and scalability";
        const auto element = is_vector(source) ? source->element : source;
        if (element && element->kind != Type::Kind::Generic &&
            !is_integer(element) && !is_floating(element))
            return "vector conversion requires numeric scalar or vector operands";
    } else if (is_vector(source)) {
        return "vector value cannot convert to a non-vector type";
    }
    return nullptr;
}

template<class TypeOf>
bool source_function_designator(const Expr& source, const TypeOf& type_of) {
    const auto* value = &source;
    while (value->left && (value->kind == Expr::Kind::Parenthesized ||
        (value->kind == Expr::Kind::Unary && value->text == "&"))) value = value->left.get();
    if (value->kind == Expr::Kind::Address && value->evaluated_address)
        return value->evaluated_address->function != nullptr;
    if (value->kind != Expr::Kind::Name) return false;
    const auto type = type_of(*value, false);
    return type && type->kind == Type::Kind::Function;
}

template<class TypeOf>
const char* source_meta_expression_error(const Expr& node, const TypeOf& type_of) {
    if (node.kind == Expr::Kind::Cast && node.left) {
        const auto source = type_of(*node.left, true);
        if (contains_meta_type(node.type) && !is_meta_type(node.type))
            return "opaque meta values cannot appear inside runtime pointer, array, or function types";
        if (is_meta_type(source) || is_meta_type(node.type)) {
            if (node.left->kind == Expr::Kind::Quote && !is_meta_type(node.type))
                return "quote cannot enter runtime expressions";
            if (!conditional_meta_type(source, node.type)) return "incompatible meta value in cast";
        }
    } else if (node.kind == Expr::Kind::Assign && node.left && node.right) {
        const auto destination = type_of(*node.left, false), source = type_of(*node.right, true);
        if (is_meta_type(source) || is_meta_type(destination)) {
            if (node.right->kind == Expr::Kind::Quote && !is_meta_type(destination))
                return "quote cannot enter runtime expressions";
            if (!conditional_meta_type(source, destination)) return "incompatible meta value in assignment";
            if (node.text != "=") return "opaque meta values do not support compound assignment";
            if (destination->is_const) return "cannot write a const cell";
            const auto* designator = node.left.get();
            while (designator->kind == Expr::Kind::Parenthesized && designator->left)
                designator = designator->left.get();
            if (designator->kind != Expr::Kind::Name) return "meta assignment requires a local cell";
        }
    } else if (node.kind == Expr::Kind::Unary && node.left && is_meta_type(type_of(*node.left, true))) {
        return "opaque meta values do not support unary operators or addresses";
    } else if (node.kind == Expr::Kind::Binary) {
        const bool member = node.text == "member" || node.text == "pointer_member";
        if ((node.left && is_meta_type(type_of(*node.left, true))) ||
            (!member && node.right && is_meta_type(type_of(*node.right, true))))
            return "opaque meta values do not support binary operators";
    } else if (node.kind == Expr::Kind::Conditional && node.left && node.right && node.third) {
        const auto selector = type_of(*node.left, true);
        const auto yes = type_of(*node.right, true), no = type_of(*node.third, true);
        if (is_meta_type(selector) || is_meta_type(yes) || is_meta_type(no)) {
            if (selector && !is_scalar(selector)) return "conditional meta selector must be scalar";
            if ((is_meta_type(yes) || is_meta_type(no)) && !conditional_meta_type(yes, no))
                return "conditional meta operands must have the same type";
        }
    } else if (node.kind == Expr::Kind::Call && node.left && is_meta_type(type_of(*node.left, true))) {
        return "opaque meta values are not callable";
    }
    return nullptr;
}

template<class TypeOf>
const char* source_expression_conversion_error(const Expr& node, const TypeOf& type_of) {
    if (node.kind == Expr::Kind::Cast && node.left)
        return source_conversion_error(type_of(*node.left, true), node.type, SourceConversion::Explicit,
            source_function_designator(*node.left, type_of));
    if (node.kind == Expr::Kind::Assign && node.text == "=" && node.left && node.right)
        return source_conversion_error(type_of(*node.right, true), type_of(*node.left, false),
            SourceConversion::Implicit, source_function_designator(*node.right, type_of));
    return nullptr;
}

template<class TypeOf>
std::optional<std::string> source_name_error(const Expr& node, Program& program,
                                            const FunctionDecl* caller, const TypeOf& type_of,
                                            bool direct_callee = false) {
    // Required-call preflight can dispatch before visiting its callee leaf.
    // Keep the same compiler-name diagnostic on that path as in body/layout
    // validation, without changing ordinary callable-signature diagnostics.
    if (node.kind == Expr::Kind::Call && node.left && node.left->kind == Expr::Kind::Name &&
        node.left->text.starts_with("$::"))
        return source_name_error(*node.left, program, caller, type_of);
    if (node.kind != Expr::Kind::Name) return {};
    if (node.text.starts_with("$::")) {
        // Instruction names and operand/resource interpretation stay with the
        // target. Compiler-owned names do not bypass source validation merely
        // because an enclosing expression already has a known scalar type.
        if (is_machine_builtin_name(node.text) || find_core_expression_builtin(node.text)) return {};
        return "unknown compiler builtin '" + node.text + "'";
    }
    const auto type = type_of(node);
    // Translation-only functions have no address, even in unselected or
    // unevaluated source. A local pointer with the same spelling is a different
    // entity; query the non-decayed bound type before resolving a function.
    if (!direct_callee && (!type || type->kind == Type::Kind::Function))
        if (const auto* function = resolve_function(program, caller, node,
                [](const FunctionDecl& candidate) { return evaluation_only(candidate); }))
            return "eval-only function '" + function->name + "' has no runtime address";
    if (type) return {};
    // Source declarations name variadic state cells; the selected ABI owns
    // their type/availability validation. Compare binders, never state spellings.
    if (caller)
        for (const auto& attribute : caller->attributes)
            if (attribute.name == "variadic")
                for (const auto& binding : attribute.variadic_bindings)
                    if (name_key(binding) == name_key(node)) return {};
    // A visible generic or otherwise not-yet-prepared function is still a
    // declared name. Its type/instantiation constraints have separate checks.
    if (resolve_function(program, caller, node, [](const FunctionDecl&) { return true; })) return {};
    // A copied label token must retain its source binder even when used after
    // a differently quoted owner. Preserve the dedicated provenance diagnostic.
    if (resolved_label_binding(NameUse(node)).kind == LabelBinding::Kind::Reference)
        return "label address does not name a visible label in its retained source binding";
    return "unresolved name '" + node.text + "'";
}

bool source_instruction_call(const Expr& call) {
    return call.kind == Expr::Kind::Call && call.left &&
        call.left->kind == Expr::Kind::Name && call.left->text.starts_with("$::_");
}

bool source_argument_names(const Expr& call, const Expr&) {
    // Instruction operands can name machine resources and raw labels, not just
    // ordinary values. Their interpretation and diagnostics are target-owned.
    // Type-query operands already carry a parsed TypePtr. An expression
    // expander cannot smuggle a type spelling through ordinary value lookup.
    return !source_instruction_call(call);
}

template<class TypeOf>
SourceExpressionUse source_argument_use(const Expr& call, std::size_t index, const TypeOf& type_of) {
    if (atomic_builtin(call) == AtomicBuiltin::IsLockFree) return SourceExpressionUse::Designator;
    if (call.kind != Expr::Kind::Call || !call.left ||
        (call.left->kind == Expr::Kind::Name && call.left->text.starts_with("$::")))
        return SourceExpressionUse::Value;
    auto signature = type_of(*call.left);
    if (signature && signature->kind == Type::Kind::Pointer) signature = signature->pointee;
    if (signature && signature->kind == Type::Kind::Function && signature->function &&
        index < signature->function->parameters.size() &&
        signature->function->parameters[index].mode == ParameterMode::Out)
        return SourceExpressionUse::Designator;
    return SourceExpressionUse::Value;
}

struct SourceConstantProbe {
    std::optional<Expr::IntegerConstant> value;
    bool needs_invocation_context{};
};

using SourceConstantQuery = std::function<EvaluationTask<SourceConstantProbe>(const Expr&,
    std::span<const std::pair<NameKey, TypePtr>>, const TypePtr&)>;
using SourceReport = std::function<void(SourceLocation, std::string)>;

struct NormalizedInitializer {
    std::unique_ptr<Expr> source;
    std::vector<const Expr*> deferred_indices;
};

EvaluationTask<bool> normalize_initializer_indices_async(Expr& source,
    std::vector<const Expr*>& deferred, std::span<const std::pair<NameKey, TypePtr>> types,
    const SourceConstantQuery& constant, const SourceReport& report) {
    for (auto& entry : source.initializer_entries) {
        for (auto& designator : entry.designators) {
            if (!designator.index) continue;
            const auto probe = co_await constant(*designator.index, types, builtin_type(BuiltinType::Uptr));
            if (probe.needs_invocation_context) {
                deferred.push_back(designator.index.get());
                continue;
            }
            if (!probe.value) {
                report(designator.location, "array initializer designator requires a nonnegative integer constant");
                co_return false;
            }
            auto value = std::make_unique<Expr>();
            value->kind = Expr::Kind::Integer;
            value->location = designator.index->location;
            value->type = builtin_type(probe.value->type);
            value->text = to_decimal(probe.value->value) + literal_suffix(value->type);
            value->evaluated_integer = probe.value;
            designator.index = std::move(value);
        }
        if (entry.value && entry.value->kind == Expr::Kind::AggregateInitializer &&
            !(co_await normalize_initializer_indices_async(*entry.value, deferred, types, constant, report)))
            co_return false;
    }
    co_return true;
}

bool complete_source_array_extent(const TypePtr& type, std::uint64_t count,
    unsigned address_bits, SourceLocation location, const SourceReport& report) {
    if (!count) report(location, "an omitted array bound requires a nonempty initializer");
    else if (count > std::numeric_limits<std::uint32_t>::max())
        report(location, "inferred array bound exceeds the language limit");
    else if (!fits_unsigned(UInt128{count}, address_bits))
        report(location, "inferred array bound exceeds the target uptr limit");
    else {
        type->lanes = static_cast<std::uint32_t>(count);
        type->array_extent_dependency = Type::ArrayExtentDependency::None;
        return true;
    }
    return false;
}

EvaluationTask<bool> prepare_inferred_array_async(VariableDecl& declaration, Program& program,
    std::span<const std::pair<NameKey, TypePtr>> types, const SourceConstantQuery& constant,
    const SourceReport& report, bool invocation) {
    const auto& type = declaration.type;
    if (!type || type->kind != Type::Kind::Array || type->lanes || type->array_bound ||
        declaration.dynamic_array_bound || !declaration.initializer) co_return true;
    const auto& source = *declaration.initializer;
    if (source.kind == Expr::Kind::String && type->element && type->element->kind == Type::Kind::Builtin &&
        type->element->builtin == BuiltinType::U8) {
        const auto text = source.string_value.empty() ? decode_string_literal(source.text)
            : std::optional<std::string>(source.string_value);
        if (!text) { report(source.location, "invalid string initializer"); co_return false; }
        co_return complete_source_array_extent(type, text->size() + 1, program.address_bits, source.location, report);
    }
    if (declaration.storage_static && source.kind != Expr::Kind::AggregateInitializer &&
        type->element && type->element->kind == Type::Kind::Builtin &&
        type->element->builtin == BuiltinType::U8) {
        // An inferred static byte extent is a dependency of later local types,
        // just like a brace-list extent. Query its length in the same isolated
        // source-proof context; never execute the static declaration or expose
        // a runtime storage cell. The ordinary initializer check still enforces
        // the exact bytes-to-u8-array conversion.
        Expr length;
        length.kind = Expr::Kind::Call;
        length.location = source.location;
        length.left = std::make_unique<Expr>();
        length.left->kind = Expr::Kind::Name;
        length.left->location = source.location;
        length.left->text = "$::meta::len";
        length.arguments.push_back(clone_expr(source));
        const auto probe = co_await constant(length, types, builtin_type(BuiltinType::Uptr));
        if (probe.needs_invocation_context) {
            type->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
            co_return true;
        }
        if (!probe.value || probe.value->value.high) {
            report(source.location, "static byte array requires a target-sized translation-time byte count");
            co_return false;
        }
        co_return complete_source_array_extent(type, probe.value->value.low,
            program.address_bits, source.location, report);
    }
    // Other forms retain their dedicated source checks; they are not brace-list
    // extent inference.
    if (source.kind != Expr::Kind::AggregateInitializer) co_return true;
    NormalizedInitializer normalized{clone_expr(source), {}};
    if (!(co_await normalize_initializer_indices_async(*normalized.source, normalized.deferred_indices,
            types, constant, report))) co_return false;
    if (!program.evaluation_initializer_types) {
        report(source.location, "target initializer type selection is unavailable during source validation");
        co_return false;
    }
    const auto plan = co_await program.evaluation_initializer_types.async(*normalized.source, type, normalized.deferred_indices);
    if (!plan.valid) {
        report(plan.error_location.file ? plan.error_location : source.location,
            plan.error_message.empty() ? "invalid aggregate initializer" : plan.error_message);
        co_return false;
    }
    if (plan.outer_extent_deferred) type->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
    else if (!complete_source_array_extent(type, plan.minimum_elements, program.address_bits, source.location, report))
        co_return false;
    // An invocation owns this declaration graph. Keep normalized designators
    // for the later source/layout pass and execution instead of running their
    // context-sensitive name construction a second time.
    if (invocation) declaration.initializer = std::move(normalized.source);
    co_return true;
}

struct SourceInitializerProbe {
    bool valid{};
    bool needs_invocation_context{};
    bool symbolic{};
    std::optional<Expr::IntegerConstant> integer;
};

struct SourcePatchOperand {
    const Expr* instruction{};
    std::size_t index{};
    EvaluationInstructionOperand* facts{};
};

enum class SourceObjectStorage { NotObject, Automatic, Static, ThreadLocal };

struct SourceObjectDeclaration {
    SourceObjectStorage storage{SourceObjectStorage::NotObject};
    TypePtr type;
    const Expr* initializer{};
    std::string_view location_name;
};

SourceObjectDeclaration source_object_declaration(const Expr& name, const Program& program,
                                                 const FunctionDecl* caller) {
    const auto key = name_key(name);
    const auto duration = [](bool is_static, const std::vector<Attribute>& attributes) {
        if (!is_static) return SourceObjectStorage::Automatic;
        return std::any_of(attributes.begin(), attributes.end(),
            [](const Attribute& attribute) { return attribute.name == "thread_local"; })
            ? SourceObjectStorage::ThreadLocal : SourceObjectStorage::Static;
    };
    // Retained declaration identities, not spellings, distinguish a local or
    // copied binder from a same-spelled global. Static locals may not have been
    // lifted yet while expansion-time declarations are being validated.
    if (caller) {
        for (const auto& parameter : caller->parameters)
            if (name_key(parameter) == key) return {SourceObjectStorage::Automatic, parameter.type, nullptr,
                parameter.location_name ? std::string_view(*parameter.location_name) : std::string_view{}};
        for (const auto& attribute : caller->attributes)
            for (const auto& binding : attribute.variadic_bindings)
                if (name_key(binding) == key) return {SourceObjectStorage::Automatic, binding.type, nullptr, {}};
        std::vector<const Statement*> pending;
        if (caller->body) pending.push_back(caller->body.get());
        while (!pending.empty()) {
            const auto* statement = pending.back();
            pending.pop_back();
            if (statement->declaration && name_key(*statement->declaration) == key) {
                const auto& declaration = *statement->declaration;
                return {duration(declaration.storage_static, declaration.attributes),
                    declaration.type, declaration.initializer.get(),
                    declaration.location_name ? std::string_view(*declaration.location_name) : std::string_view{}};
            }
            for (const auto& child : statement->statements) pending.push_back(child.get());
            if (statement->first) pending.push_back(statement->first.get());
            if (statement->second) pending.push_back(statement->second.get());
        }
    }
    if (const auto* object = resolve_object(program, caller, name)) {
        SourceObjectDeclaration result{duration(true, object->attributes), object->type, object->initializer.get(), {}};
        // Lookup can select an earlier forward declaration. Inspect the same
        // source entity's definition without confusing fresh private binders
        // or separate translation-unit statics with matching printed names.
        for (const auto& candidate : program.objects) {
            if (object->fresh != candidate->fresh) continue;
            const bool same = object->binding.placement && candidate->binding.placement
                ? object->binding.placement == candidate->binding.placement
                : object->name == candidate->name &&
                    ((object->linkage != Linkage::Static && candidate->linkage != Linkage::Static) ||
                     (object->linkage == Linkage::Static && candidate->linkage == Linkage::Static &&
                      object->source_unit == candidate->source_unit));
            if (!same) continue;
            if (duration(true, candidate->attributes) == SourceObjectStorage::ThreadLocal)
                result.storage = SourceObjectStorage::ThreadLocal;
            if (candidate->initializer) {
                result.type = candidate->type;
                result.initializer = candidate->initializer.get();
            }
        }
        return result;
    }
    return {};
}

// This checks portable source shape, not materializer availability or physical
// sink representation. In particular, a target's sink type need not be uptr.
template<class TypeOf>
std::optional<SourceExpressionIssue> source_patch_intrinsic_error(
    const Expr& call, const Program& program, const FunctionDecl* caller, const TypeOf& type_of) {
    if (!patch_intrinsic(call)) return {};
    if (call.arguments.empty() || call.arguments.size() > 2)
        return SourceExpressionIssue{call.location, "$::patch requires an initial value and optional address sink"};
    const auto& initial = *call.arguments.front();
    if (const auto type = type_of(initial, true); type && type->kind != Type::Kind::Generic && !is_scalar(type))
        return SourceExpressionIssue{initial.location, "$::patch initial value must have scalar type"};
    if (call.arguments.size() == 1) return {};
    const auto& sink = *call.arguments[1];
    if (const auto type = type_of(sink, false); type && type->kind != Type::Kind::Generic && !is_scalar(type))
        return SourceExpressionIssue{sink.location, "$::patch address sink must designate a scalar subobject"};
    const Expr* selected = &sink;
    while (selected) {
        if (const auto type = type_of(*selected, false); type &&
            (type->is_const || type->is_volatile || type->is_atomic || type->is_restrict))
            return SourceExpressionIssue{selected->location, "$::patch address sink must be unqualified and non-atomic"};
        if (selected->kind == Expr::Kind::Parenthesized && selected->left) {
            selected = selected->left.get();
            continue;
        }
        if (selected->kind == Expr::Kind::Name) {
            // Leave unresolved names to the ordinary source-name validator.
            if (!type_of(*selected, false)) return {};
            switch (source_object_declaration(*selected, program, caller).storage) {
            case SourceObjectStorage::Static: return {};
            case SourceObjectStorage::ThreadLocal:
                return SourceExpressionIssue{selected->location, "$::patch address sink cannot designate thread-local storage"};
            default:
                return SourceExpressionIssue{selected->location, "$::patch address sink must designate a static-duration object"};
            }
        }
        if (selected->kind != Expr::Kind::Binary || !selected->left || !selected->right ||
            (selected->text != "member" && selected->text != "index"))
            return SourceExpressionIssue{selected->location,
                "$::patch address sink must be a static object followed only by direct member or constant array selections"};
        if (selected->text == "member") {
            const auto* member = source_record_member(*selected, program,
                [&](const Expr& node) { return type_of(node, false); });
            if (member && member->bit_width)
                return SourceExpressionIssue{selected->location, "$::patch address sink cannot designate a bit-field"};
        } else {
            const auto base = type_of(*selected->left, false);
            if (base && base->kind != Type::Kind::Generic && base->kind != Type::Kind::Array)
                return SourceExpressionIssue{selected->location, "$::patch array sink requires a direct array object"};
        }
        selected = selected->left.get();
    }
    return {};
}

template<class TypeOf>
std::optional<SourceExpressionIssue> source_staging_intrinsic_error(const Expr& call, const TypeOf& type_of) {
    if (const auto* reason = staging_intrinsic_arity_error(call))
        return SourceExpressionIssue{call.location, reason};
    if (staging_intrinsic(call) == StagingIntrinsic::Runtime &&
        is_meta_type(type_of(*call.arguments.front())))
        return SourceExpressionIssue{call.location, "meta values cannot enter runtime expressions"};
    return {};
}

template<class TypeOf, class Constant>
EvaluationTask<std::optional<SourceExpressionIssue>> source_patch_sink_error_async(
    const Expr& call, const Program& program, const FunctionDecl* caller,
    const TypeOf& type_of, const Constant& constant) {
    if (!patch_intrinsic(call) || call.arguments.size() != 2) co_return std::nullopt;
    const Expr* root = call.arguments[1].get();
    while (root->left && (root->kind == Expr::Kind::Parenthesized ||
            (root->kind == Expr::Kind::Binary && (root->text == "member" || root->text == "index"))))
        root = root->left.get();
    const auto declaration = root->kind == Expr::Kind::Name
        ? source_object_declaration(*root, program, caller) : SourceObjectDeclaration{};
    const bool initialized = declaration.initializer != nullptr;
    if (initialized && (!declaration.type ||
            (declaration.type->kind != Type::Kind::Array && declaration.type->kind != Type::Kind::Record) ||
            declaration.initializer->kind != Expr::Kind::AggregateInitializer))
        co_return SourceExpressionIssue{call.arguments[1]->location,
            "$::patch address sink must designate an uninitialized static-duration subobject"};
    std::uint64_t offset{};
    bool deferred{};
    for (const Expr* selected = call.arguments[1].get(); selected; selected = selected->left.get()) {
        if (selected->kind == Expr::Kind::Parenthesized) continue;
        if (selected->kind != Expr::Kind::Binary || !selected->left || !selected->right ||
            (selected->text != "member" && selected->text != "index")) break;
        const auto base = type_of(*selected->left, false);
        if (!base || base->kind == Type::Kind::Generic) continue;
        std::uint64_t displacement{};
        if (selected->text == "index") {
            const auto probe = co_await constant(*selected->right);
            if (probe.needs_invocation_context) { deferred = true; continue; }
            if (!probe.value || integer_negative(probe.value->value,
                    evaluation_integer_type(builtin_type(probe.value->type), program.address_bits)))
                co_return SourceExpressionIssue{selected->right->location,
                    "$::patch array sink requires a nonnegative integer constant index"};
            if (base->kind == Type::Kind::Array && base->lanes &&
                (probe.value->value.high || probe.value->value.low >= base->lanes))
                co_return SourceExpressionIssue{selected->right->location, "$::patch array sink index is out of range"};
            if (!initialized) continue;
            const auto size = program.evaluation_size_of ? program.evaluation_size_of(base->element) : std::nullopt;
            if (!size || !*size || probe.value->value.high ||
                probe.value->value.low > std::numeric_limits<std::uint64_t>::max() / *size)
                co_return SourceExpressionIssue{selected->location, "$::patch array sink has unavailable or overflowing target layout"};
            displacement = probe.value->value.low * *size;
        } else {
            if (!initialized) continue;
            const auto member = program.evaluation_member_layout
                ? program.evaluation_member_layout(base, member_name(*selected->right)) : std::nullopt;
            if (!member)
                co_return SourceExpressionIssue{selected->location, "$::patch member sink has unavailable target layout"};
            displacement = member->offset;
        }
        if (displacement > std::numeric_limits<std::uint64_t>::max() - offset)
            co_return SourceExpressionIssue{selected->location, "$::patch address sink offset overflows target storage"};
        offset += displacement;
    }
    if (!initialized || deferred) co_return std::nullopt;
    // Initializer plans list explicit writes only. Omitted aggregate elements
    // remain available for a sink, including partial arrays and nested records.
    // Normalize designators on a private copy; no invocation value is cached.
    auto initializer = clone_expr(*declaration.initializer);
    std::vector<Expr*> pending{initializer.get()};
    while (!pending.empty()) {
        auto* list = pending.back();
        pending.pop_back();
        for (auto& entry : list->initializer_entries) {
            for (auto& designator : entry.designators) {
                if (!designator.index) continue;
                const auto proof = co_await constant(*designator.index);
                if (proof.needs_invocation_context) { deferred = true; continue; }
                if (!proof.value || integer_negative(proof.value->value,
                        evaluation_integer_type(builtin_type(proof.value->type), program.address_bits)))
                    co_return SourceExpressionIssue{designator.location,
                        "array initializer designator requires a nonnegative integer constant"};
                designator.index->evaluated_integer = proof.value;
            }
            if (entry.value && entry.value->kind == Expr::Kind::AggregateInitializer)
                pending.push_back(entry.value.get());
        }
    }
    if (deferred) co_return std::nullopt;
    if (!program.evaluation_initializer_plan || !program.evaluation_size_of)
        co_return SourceExpressionIssue{call.location, "$::patch sink initializer requires target layout"};
    const auto plan = program.evaluation_initializer_plan(*initializer, declaration.type);
    if (!plan.valid) co_return SourceExpressionIssue{plan.error_location, plan.error_message};
    const auto size = program.evaluation_size_of(type_of(*call.arguments[1], false));
    if (!size || *size > std::numeric_limits<std::uint64_t>::max() - offset)
        co_return SourceExpressionIssue{call.location, "$::patch address sink has unavailable or overflowing target layout"};
    for (const auto& item : plan.items) {
        const auto item_size = program.evaluation_size_of(item.type);
        if (!item_size || *item_size > std::numeric_limits<std::uint64_t>::max() - item.layout.offset)
            co_return SourceExpressionIssue{call.location, "$::patch sink initializer has unavailable or overflowing target layout"};
        if (offset < item.layout.offset + *item_size && item.layout.offset < offset + *size)
            co_return SourceExpressionIssue{call.arguments[1]->location,
                "$::patch address sink must designate an uninitialized static-duration subobject"};
    }
    co_return std::nullopt;
}

template<class TypeOf, class Initializer>
EvaluationTask<std::optional<SourceExpressionIssue>> source_patch_initial_error_async(
    const Expr& call, const Program& program, SourcePatchOperand operand,
    const TypeOf& type_of, const Initializer& initializer, bool retain_constant = false) {
    if (!patch_intrinsic(call) || call.arguments.empty() || call.arguments.size() > 2)
        co_return std::nullopt;
    const auto& source = *call.arguments.front();
    const auto type = type_of(source, true);
    if (!type || type->kind == Type::Kind::Generic) co_return std::nullopt;
    std::vector<EvaluationPatchValueCapabilities> capabilities;
    if (operand.instruction && program.evaluation_patch_operand_capabilities) {
        capabilities = program.evaluation_patch_operand_capabilities(operand.instruction->left->text, operand.index,
            operand.instruction->arguments.size(), type);
        if (capabilities.empty())
            co_return SourceExpressionIssue{source.location,
                "selected target instruction has no patch field accepting exactly " + type_name(type)};
    } else if (!operand.instruction && program.evaluation_patch_value_capabilities) {
        const auto materializer = program.evaluation_patch_value_capabilities(type);
        if (!materializer)
            co_return SourceExpressionIssue{source.location,
                "selected target has no contiguous $::patch materializer for " + type_name(type)};
        capabilities.push_back(*materializer);
    }
    const bool checked_capabilities = !capabilities.empty();
    if (checked_capabilities && call.arguments.size() == 2) {
        // Keep candidates separate: one form's sink support cannot be combined
        // with another form's relocation support to invent a selectable form.
        std::erase_if(capabilities, [](const auto& form) { return !form.address_sink_type; });
        if (capabilities.empty())
            co_return SourceExpressionIssue{call.arguments[1]->location,
                operand.instruction ? "selected target instruction patch field does not support an address sink"
                    : "selected target does not support a $::patch address sink for this materializer"};
        const auto sink_type = type_of(*call.arguments[1], false);
        if (sink_type && sink_type->kind != Type::Kind::Generic) {
            const auto required = type_name(capabilities.front().address_sink_type);
            const bool one_type = std::all_of(capabilities.begin(), capabilities.end(), [&](const auto& form) {
                return same_type(form.address_sink_type, capabilities.front().address_sink_type);
            });
            std::erase_if(capabilities, [&](const auto& form) {
                return !same_type(sink_type, form.address_sink_type);
            });
            if (capabilities.empty())
                co_return SourceExpressionIssue{call.arguments[1]->location,
                    "$::patch address sink must designate a complete unqualified non-atomic " +
                        (one_type ? required : "target-registered patch-address") + " subobject"};
        }
    }
    const auto proof = co_await initializer(source, type);
    if (!proof.valid && !proof.needs_invocation_context)
        co_return SourceExpressionIssue{source.location,
            "$::patch initial requires a compile-time or link-time scalar value"};
    if ((proof.valid || proof.needs_invocation_context) && proof.symbolic && checked_capabilities) {
        std::erase_if(capabilities, [](const auto& form) { return !form.symbol_relocation; });
        if (capabilities.empty())
            co_return SourceExpressionIssue{source.location,
                operand.instruction ? "selected target instruction patch field cannot encode a symbol relocation"
                    : "selected target patch materializer cannot encode a symbol relocation"};
    }
    if (operand.facts) {
        operand.facts->kind = EvaluationInstructionOperand::Kind::Patch;
        operand.facts->type = type;
        operand.facts->integer = proof.integer;
        operand.facts->deferred = proof.needs_invocation_context;
        for (const auto& capability : capabilities)
            if (capability.instruction_form) operand.facts->patch_forms.push_back(*capability.instruction_form);
    }
    if (proof.valid && retain_constant)
        call.arguments.front()->evaluated_integer = proof.integer;
    co_return std::nullopt;
}

template<class TypeOf, class Lvalue, class Constant>
EvaluationTask<std::optional<SourceExpressionIssue>> source_instruction_error_async(
    const Expr& call, Program& program, const FunctionDecl* caller,
    const TypeOf& type_of, const Lvalue& lvalue, const Constant& constant,
    std::span<EvaluationInstructionOperand> operands, bool retain_constants = false, bool check_forms = true) {
    if (!source_instruction_call(call) || !program.evaluation_instruction_source) co_return std::nullopt;
    using Kind = EvaluationInstructionOperand::Kind;
    const auto value_bits = [&](const auto& self, const TypePtr& type) -> unsigned {
        if (!type) return 0;
        if (type->kind == Type::Kind::Pointer || is_label_type(type)) {
            const auto bytes = program.evaluation_size_of ? program.evaluation_size_of(type) : std::nullopt;
            return bytes && *bytes <= UINT_MAX / 8 ? static_cast<unsigned>(*bytes * 8) : 0;
        }
        if (type->kind == Type::Kind::Vector) {
            const auto element = self(self, type->element);
            return element && type->lanes <= UINT_MAX / element ? element * type->lanes : 0;
        }
        if (type->kind == Type::Kind::Builtin &&
            (type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr ||
             type->builtin == BuiltinType::Fptr)) return program.address_bits;
        return type_bits(type);
    };
    const auto register_fact = [&](const Expr& original) {
        EvaluationInstructionOperand::Memory::Register result;
        const Expr* expression = &original;
        while (expression->kind == Expr::Kind::Parenthesized && expression->left) expression = expression->left.get();
        result.type = type_of(*expression, false);
        result.bits = value_bits(value_bits, result.type);
        result.integer = is_integer(result.type);
        if (expression->kind == Expr::Kind::Name) {
            const auto object = source_object_declaration(*expression, program, caller);
            result.object = object.storage != SourceObjectStorage::NotObject;
            if (!object.location_name.empty() && object.location_name != "auto") result.fixed = object.location_name;
        }
        return result;
    };
    for (std::size_t i = 0; i < call.arguments.size(); ++i) {
        auto& facts = operands[i];
        if (facts.kind == Kind::Patch) continue; // Reuse the isolated initial proof.
        Expr* source = call.arguments[i].get();
        while (source->kind == Expr::Kind::Parenthesized && source->left) source = source->left.get();
        facts.type = type_of(*source, false);
        facts.floating = is_floating(facts.type);
        facts.deferred = has_pending_type_bound(facts.type);
        facts.writable = lvalue(*source) && facts.type && !facts.type->is_const;
        facts.bits = value_bits(value_bits, facts.type);
        if (source->kind == Expr::Kind::Name) {
            if (source->text.starts_with("$::reg::")) {
                facts.kind = Kind::Register;
                facts.fixed_register = source->text.substr(8);
                facts.writable = true;
                continue;
            }
            const auto object = source_object_declaration(*source, program, caller);
            if (object.storage != SourceObjectStorage::NotObject) {
                facts.kind = Kind::Register;
                if (!object.location_name.empty() && object.location_name != "auto")
                    facts.fixed_register = object.location_name;
                continue;
            }
            if (const auto label = resolve_label_constant(*source, caller, program)) {
                facts.kind = Kind::Label;
                facts.label_same_function = caller && label->owner &&
                    (label->owner == caller || (caller->function_scope &&
                        label->owner->function_scope == caller->function_scope));
                continue;
            }
        }
        if (lvalue(*source)) {
            facts.kind = Kind::Memory;
            auto& memory = facts.memory;
            using Shape = EvaluationInstructionOperand::Memory::Shape;
            if (source->kind == Expr::Kind::Unary && source->text == "*" && source->left) {
                memory.shape = Shape::Dereference;
                memory.base = register_fact(*source->left);
            } else if (source->kind == Expr::Kind::Binary && source->text == "index" && source->left && source->right) {
                memory.shape = Shape::Index;
                memory.base = register_fact(*source->left);
                memory.index = register_fact(*source->right);
                if (!memory.index.object && is_integer(type_of(*source->right, true))) {
                    const auto proof = co_await constant(*source->right);
                    memory.constant_index = proof.value;
                    memory.index_deferred = proof.needs_invocation_context;
                    if (proof.value) {
                        const auto integer = evaluation_integer_type(builtin_type(proof.value->type), program.address_bits);
                        memory.constant_bits = integer.bits;
                        memory.constant_signed = integer.is_signed;
                        if (retain_constants) source->right->evaluated_integer = proof.value;
                    }
                }
            }
            if (memory.base.type && memory.base.type->kind == Type::Kind::Pointer && memory.base.type->pointee) {
                Expr size;
                size.kind = Expr::Kind::Sizeof;
                size.type = memory.base.type->pointee;
                size.location = source->location;
                const auto proof = co_await constant(size);
                memory.layout_deferred = proof.needs_invocation_context;
                if (proof.value && !proof.value->value.high) memory.element_bytes = proof.value->value.low;
            }
            facts.deferred = facts.deferred || memory.layout_deferred || memory.index_deferred;
            continue;
        }
        if (facts.type && facts.type->kind == Type::Kind::Generic) {
            facts.kind = Kind::Deferred;
            facts.deferred = true;
            continue;
        }
        if (is_integer(facts.type)) {
            const auto proof = co_await constant(*source);
            facts.kind = Kind::Immediate;
            facts.integer = proof.value;
            facts.deferred = proof.needs_invocation_context;
            if (proof.value) {
                const auto integer = evaluation_integer_type(builtin_type(proof.value->type), program.address_bits);
                facts.integer_bits = integer.bits;
                facts.integer_signed = integer.is_signed;
                // Managed lowering encodes the proven immediate.
                if (retain_constants) source->evaluated_integer = proof.value;
            }
        }
    }
    if (check_forms)
        if (const auto reason = program.evaluation_instruction_source(call.left->text, operands))
            co_return SourceExpressionIssue{call.location, *reason};
    co_return std::nullopt;
}

template<class TypeOf>
TypePtr atomic_query_type(const Expr& call, const TypeOf& type_of) {
    if (call.type) return call.type;
    if (call.arguments.size() != 1) return {};
    // No lvalue read or array/function decay belongs to this type query.
    return type_of(*call.arguments.front(), false);
}
template<class TypeOf, class Constant>
EvaluationTask<std::optional<SourceExpressionIssue>> source_control_intrinsic_error_async(
    const Expr& call, const TypeOf& type_of, const Constant& constant, bool retain_constant = false) {
    if (const auto* reason = control_intrinsic_arity_error(call))
        co_return SourceExpressionIssue{call.location, reason};
    switch (control_intrinsic(call)) {
    case ControlIntrinsic::Expect: {
        const auto& value = *call.arguments[0];
        const auto type = type_of(value, true);
        if (type && type->kind != Type::Kind::Generic && !is_integer(type))
            co_return SourceExpressionIssue{value.location,
                "$::expect requires a boolean, integer, or enumeration value"};
        const auto& expected = *call.arguments[1];
        const auto expected_type = type_of(expected, true);
        if (expected_type && expected_type->kind != Type::Kind::Generic && !is_integer(expected_type))
            co_return SourceExpressionIssue{expected.location, "$::expect requires an integer constant expectation"};
        const auto probe = co_await constant(expected);
        if (!probe.value && !probe.needs_invocation_context)
            co_return SourceExpressionIssue{expected.location, "$::expect requires an integer constant expectation"};
        if (retain_constant && probe.value) call.arguments[1]->evaluated_integer = probe.value;
        break;
    }
    case ControlIntrinsic::Assume: {
        const auto& condition = *call.arguments[0];
        const auto type = type_of(condition, true);
        if (type && type->kind != Type::Kind::Generic && !is_scalar(type))
            co_return SourceExpressionIssue{condition.location, "$::assume requires a scalar condition"};
        if (const auto violation = assumption_violation(condition, [&](const Expr& node) {
                const auto queried = type_of(node, false);
                return queried && queried->kind != Type::Kind::Array && queried->kind != Type::Kind::Function &&
                    (queried->is_volatile || queried->is_atomic);
            }))
            co_return SourceExpressionIssue{violation->expression->location, violation->message()};
        break;
    }
    default: break;
    }
    co_return std::nullopt;
}

// Portable source constraints do not execute an atomic access or ask a target
// to select its lock-free sequence. Pointer representation/address-space
// lowering stays with the ordinary resolved-model conversion path.
template<class TypeOf, class Constant>
EvaluationTask<std::optional<SourceExpressionIssue>> source_atomic_intrinsic_error_async(
    const Expr& call, const TypeOf& type_of, const Constant& constant, bool retain_constant = false) {
    const auto operation = atomic_builtin(call);
    if (operation == AtomicBuiltin::None) co_return std::nullopt;
    const auto& name = call.left->text;
    const bool fence = operation == AtomicBuiltin::ThreadFence || operation == AtomicBuiltin::SignalFence;
    const bool query = operation == AtomicBuiltin::IsLockFree;
    const auto count = fence || query ? 1U : operation == AtomicBuiltin::Load ? 2U
        : operation == AtomicBuiltin::CompareExchange ? 5U : 3U;
    if (call.arguments.size() + (query && call.type ? 1U : 0U) != count)
        co_return SourceExpressionIssue{call.location, name + (query ? " requires one type or expression"
            : fence ? " requires one memory-order argument" : " requires " + std::to_string(count) + " arguments")};
    if (query) {
        const auto type = atomic_query_type(call, type_of);
        if (type && type->kind != Type::Kind::Generic && !is_scalar(type))
            co_return SourceExpressionIssue{call.location,
                "$::atomic_is_lock_free requires a scalar type or expression"};
        co_return std::nullopt;
    }
    const auto order_index = fence ? 0U : operation == AtomicBuiltin::Load ? 1U
        : operation == AtomicBuiltin::CompareExchange ? 3U : 2U;
    const auto& order_source = *call.arguments[order_index];
    const auto order = parse_source_memory_order(order_source);
    const auto order_error = [](const Expr& source) {
        return SourceExpressionIssue{source.location,
            "expected one of $::memory::relaxed, acquire, release, acq_rel, or seq_cst"};
    };
    if (!order) co_return order_error(order_source);
    if (fence) co_return std::nullopt;
    if (operation == AtomicBuiltin::Load && (*order == MemoryOrder::Release || *order == MemoryOrder::AcqRel))
        co_return SourceExpressionIssue{order_source.location,
            "atomic load order must be relaxed, acquire, or seq_cst"};
    if (operation == AtomicBuiltin::Store && (*order == MemoryOrder::Acquire || *order == MemoryOrder::AcqRel))
        co_return SourceExpressionIssue{order_source.location,
            "atomic store order must be relaxed, release, or seq_cst"};
    if (operation == AtomicBuiltin::CompareExchange) {
        const auto& failure_source = *call.arguments[4];
        const auto failure = parse_source_memory_order(failure_source);
        if (!failure) co_return order_error(failure_source);
        if (!valid_atomic_failure_order(*order, *failure))
            co_return SourceExpressionIssue{failure_source.location,
                "compare-exchange failure order is invalid or stronger than success"};
    }
    const auto pointer = type_of(*call.arguments[0], true);
    if (!pointer || pointer->kind == Type::Kind::Generic) co_return std::nullopt;
    const auto object = pointer->kind == Type::Kind::Pointer ? pointer->pointee : TypePtr{};
    if (!object || !object->is_atomic ||
        (object->kind != Type::Kind::Generic &&
         object->kind != Type::Kind::Pointer && !is_integer(object) && !is_floating(object)))
        co_return SourceExpressionIssue{call.arguments[0]->location,
            name + " requires a pointer to an atomic-qualified scalar"};
    if (operation != AtomicBuiltin::Load && object->is_const)
        co_return SourceExpressionIssue{call.arguments[0]->location,
            name + " cannot modify a const atomic object"};
    if (operation == AtomicBuiltin::Load) co_return std::nullopt;
    const auto value = atomic_value_type(object);
    if (operation != AtomicBuiltin::Store && operation != AtomicBuiltin::Exchange &&
        operation != AtomicBuiltin::CompareExchange && value->kind != Type::Kind::Generic && !is_integer(value))
        co_return SourceExpressionIssue{call.arguments[0]->location,
            "atomic fetch arithmetic and bitwise operations require an integer object"};
    if (operation == AtomicBuiltin::CompareExchange) {
        const auto expected = type_of(*call.arguments[1], true);
        if (expected && expected->kind != Type::Kind::Generic) {
            if (expected->kind != Type::Kind::Pointer || !expected->pointee)
                co_return SourceExpressionIssue{call.arguments[1]->location,
                    "$::atomic_compare_exchange expected argument must be a pointer"};
            const auto& target = expected->pointee;
            if (target->is_const || target->is_atomic ||
                (value->kind != Type::Kind::Generic && target->kind != Type::Kind::Generic &&
                 !same_type(atomic_value_type(target), value)))
                co_return SourceExpressionIssue{call.arguments[1]->location,
                    "$::atomic_compare_exchange expected pointer has an incompatible type"};
        }
    }
    auto& input = *call.arguments[operation == AtomicBuiltin::CompareExchange ? 2 : 1];
    const auto incoming = type_of(input, true);
    if (!incoming || incoming->kind == Type::Kind::Generic || value->kind == Type::Kind::Generic)
        co_return std::nullopt;
    if (const auto* reason = source_value_error(incoming))
        co_return SourceExpressionIssue{input.location, std::string(reason) + " in atomic value argument"};
    if (!is_scalar(incoming) || is_label_type(incoming))
        co_return SourceExpressionIssue{input.location, "atomic value argument requires a convertible scalar value"};
    if (const auto* reason = source_conversion_error(incoming, value, SourceConversion::Implicit,
            source_function_designator(input, type_of)))
        co_return SourceExpressionIssue{input.location, std::string(reason) + " in atomic value argument"};
    if (incoming->kind == Type::Kind::Pointer && value->kind != Type::Kind::Pointer &&
        !(value->kind == Type::Kind::Builtin && value->builtin == BuiltinType::Bool))
        co_return SourceExpressionIssue{input.location,
            "implicit pointer-to-integer conversion requires an explicit cast in atomic value argument"};
    if (value->kind == Type::Kind::Pointer && is_integer(incoming)) {
        const auto probe = co_await constant(input);
        if ((!probe.value || probe.value->value != UInt128{}) && !probe.needs_invocation_context)
            co_return SourceExpressionIssue{input.location,
                "implicit integer-to-pointer conversion requires a constant zero in atomic value argument"};
        if (retain_constant && probe.value) input.evaluated_integer = probe.value;
    }
    co_return std::nullopt;
}

enum class SourceValidationPhase { Definition, Invocation };

struct PreparedObjectInitializer {
    std::unique_ptr<Expr> source;
    TypePtr type;
    EvaluationInitializerPlan plan;
};

// Source signatures are independent of execution: layout operands must reject
// malformed calls without constructing tokens, traversing trees, or evaluating
// an invocation context. Keep them identical to translation-only body checks.
class MetaIntrinsicArgumentValidator {
public:
    using TypeQuery = std::function<TypePtr(const Expr&)>;
    using Report = std::function<void(SourceLocation, std::string)>;
    MetaIntrinsicArgumentValidator(TypeQuery type, Report report)
        : type_(std::move(type)), report_(std::move(report)) {}
    bool valid() const { return valid_; }

    bool intrinsic(const Expr& call) {
        if (!call.left || call.left->kind != Expr::Kind::Name) return false;
        const auto& name = call.left->text;
        if (!name.starts_with("$::meta::") && !name.starts_with("$::syntax::")) return false;
        using A = Argument;
        if (name == "$::syntax::input" || name == "$::syntax::span") arguments(call, {A::Match});
        else if (name == "$::syntax::context") arguments(call, {A::MatchOrNode});
        else if (name == "$::syntax::error" || name == "$::syntax::warning" || name == "$::syntax::note" ||
                 name == "$::meta::error" || name == "$::meta::warning" || name == "$::meta::note")
            arguments(call, {A::Span, A::String});
        else if (name == "$::syntax::capture" || name == "$::syntax::node" || name == "$::syntax::count" ||
                 name == "$::syntax::is_variant" || name == "$::syntax::capture_span")
            arguments(call, {A::Match, A::String});
        else if (name == "$::syntax::at") arguments(call, {A::Match, A::String, A::Integer});
        else if (name == "$::meta::tokens" || name == "$::meta::node_span" ||
                 name == "$::meta::child_count" || name == "$::meta::extension_match") arguments(call, {A::Node});
        else if (name == "$::meta::child") arguments(call, {A::Node, A::Integer});
        else if (name == "$::meta::replace_child") arguments(call, {A::Node, A::Integer, A::Node});
        else if (name == "$::meta::is_kind") arguments(call, {A::TokensOrNode, A::String});
        else if (name == "$::meta::spelling" || name == "$::meta::children" ||
                 name == "$::meta::delimiter" || name == "$::meta::span") arguments(call, {A::Tokens});
        else if (name == "$::meta::is_production" || name == "$::meta::is_extension")
            arguments(call, {A::Node, A::String});
        else if (name == "$::meta::token" || name == "$::meta::group") {
            const auto contents = name == "$::meta::token" ? A::StringOrBytes : A::Tokens;
            if (call.arguments.size() == 3) arguments(call, {A::String, contents, A::Span});
            else arguments(call, {A::String, contents});
        }
        else if (name == "$::meta::call_site") arguments(call, {A::Tokens});
        else if (name == "$::meta::gensym") arguments(call, {A::String});
        else if (name == "$::meta::parse") {
            if (call.arguments.size() == 1) arguments(call, {A::StringOrBytes});
            else arguments(call, {A::String, A::Tokens, A::Context});
        } else if (name == "$::meta::len") arguments(call, {A::Sequence});
        else if (name == "$::meta::at") arguments(call, {A::Sequence, A::Integer});
        else if (name == "$::meta::slice") arguments(call, {A::Sequence, A::Integer, A::Integer});
        else if (name == "$::meta::concat") {
            arguments(call, {A::Sequence, A::Sequence});
            if (valid_ && type(*call.arguments[0])->kind != type(*call.arguments[1])->kind)
                error(call.location, "$::meta::concat requires values of the same sequence type");
        } else if (name == "$::meta::data") arguments(call, {A::BytesOrBuffer});
        else if (name == "$::meta::alloc") arguments(call, {A::Integer});
        else if (name == "$::meta::cap") arguments(call, {A::Buffer});
        else if (name == "$::meta::freeze") arguments(call, {A::Buffer, A::Integer});
        else error(call.location, "unknown translation-only operation '" + name + "'");
        return true;
    }

private:
    enum class Argument {
        Tokens, Node, Match, Span, Context, Buffer, Integer, String,
        Sequence, BytesOrBuffer, MatchOrNode, TokensOrNode, StringOrBytes,
    };

    TypePtr type(const Expr& expression) { return type_(expression); }
    void error(SourceLocation location, std::string message) {
        if (valid_) report_(location, std::move(message));
        valid_ = false;
    }

    bool accepts(Argument expected, const TypePtr& actual) const {
        if (!actual) return false;
        switch (expected) {
        case Argument::Tokens: return actual->kind == Type::Kind::Tokens;
        case Argument::Node: return actual->kind == Type::Kind::Syntax;
        case Argument::Match: return actual->kind == Type::Kind::SyntaxMatch;
        case Argument::Span: return actual->kind == Type::Kind::Span;
        case Argument::Context: return actual->kind == Type::Kind::Context;
        case Argument::Buffer: return actual->kind == Type::Kind::Buffer;
        case Argument::Integer: return is_integer(actual);
        case Argument::String: return actual->kind == Type::Kind::Pointer && actual->pointee &&
            actual->pointee->kind == Type::Kind::Builtin && actual->pointee->builtin == BuiltinType::U8;
        case Argument::StringOrBytes: return actual->kind == Type::Kind::Bytes || accepts(Argument::String, actual);
        case Argument::Sequence: return actual->kind == Type::Kind::Tokens || actual->kind == Type::Kind::Bytes;
        case Argument::BytesOrBuffer: return actual->kind == Type::Kind::Bytes || actual->kind == Type::Kind::Buffer;
        case Argument::MatchOrNode: return actual->kind == Type::Kind::SyntaxMatch || actual->kind == Type::Kind::Syntax;
        case Argument::TokensOrNode: return actual->kind == Type::Kind::Tokens || actual->kind == Type::Kind::Syntax;
        }
        return false;
    }

    void arguments(const Expr& call, std::initializer_list<Argument> expected) {
        if (call.arguments.size() != expected.size()) {
            error(call.location, "invalid argument count for translation-only operation '" + call.left->text + "'");
            return;
        }
        std::size_t index{};
        for (const auto argument : expected) {
            const auto& value = *call.arguments[index++];
            if (!accepts(argument, type(value))) {
                error(value.location, "incompatible argument type for translation-only operation '" + call.left->text + "'");
                return;
            }
        }
    }

    TypeQuery type_;
    Report report_;
    bool valid_{true};
};

class SourceTypeValidator {
public:
    using Report = std::function<void(SourceLocation, std::string)>;
    using Constant = SourceConstantQuery;
    using Probe = std::function<EvaluationTask<SourceConstantProbe>(const Expr&,
        std::span<const std::pair<NameKey, TypePtr>>)>;
    using CaseValue = std::function<void(const Statement&, Expr::IntegerConstant)>;
    using InitializerValue = std::function<void(const VariableDecl&, PreparedObjectInitializer)>;
    using StaticInitializer = std::function<EvaluationTask<SourceInitializerProbe>(const Expr&,
        std::span<const std::pair<NameKey, TypePtr>>, const TypePtr&)>;
    SourceTypeValidator(Program& program, const FunctionDecl& function, Report report, Constant constant,
                        Probe probe, CaseValue case_value, InitializerValue initializer_value,
                        StaticInitializer static_initializer, SourceValidationPhase phase)
        : program_(program), function_(function), report_(std::move(report)),
          constant_(std::move(constant)), probe_(std::move(probe)), case_value_(std::move(case_value)),
          initializer_value_(std::move(initializer_value)), static_initializer_(std::move(static_initializer)), phase_(phase) {}

    EvaluationTask<bool> run_async() {
        for (const auto& attribute : function_.attributes)
            if (const auto message = function_attribute_error(attribute)) {
                error(attribute.location, *message);
                co_return false;
            }
        if (const auto message = function_attribute_conflict(function_)) {
            error(function_.location, *message);
            co_return false;
        }
        if (!function_.body) co_return true;
        // Direct goto has a function-wide namespace, including forward labels.
        // Retain actual bindings: a relocated use must not select an unrelated
        // destination label merely because its spelling is the same.
        std::vector<const Statement*> pending{function_.body.get()};
        while (!pending.empty()) {
            const auto* node = pending.back();
            pending.pop_back();
            // This source-shape contract is independent of execution and
            // generic substitution. A helper or template must not erase it.
            if (const auto* attribute = malformed_tail_return(*node)) {
                error(attribute->location, "musttail requires returning a call expression directly");
                co_return false;
            }
            if (function_.generic_parameters.empty() && node->kind == Statement::Kind::Label) {
                const auto location = node->label_location.valid() ? node->label_location : node->location;
                const NameKey key(node->label_name, location);
                auto& declarations = labels_[node->label_name];
                if (std::any_of(declarations.begin(), declarations.end(), [&](const Statement* previous) {
                        return node->global_label || previous->global_label ||
                            label_matches(*previous, key, node->label_binding);
                    })) {
                    error(location, "duplicate label '" + node->label_name + "'");
                    co_return false;
                }
                if (node->global_label && evaluation_only(function_)) {
                    error(location, "cannot export a code label from a translation-only function");
                    co_return false;
                }
                if (node->global_label && function_.linkage != Linkage::Global) {
                    error(location, "a global label definition requires a global stable-ABI function");
                    co_return false;
                }
                declarations.push_back(node);
            }
            // Visit source order so duplicate diagnostics point at the later
            // definition, independently of block/loop nesting.
            if (node->second) pending.push_back(node->second.get());
            if (node->first) pending.push_back(node->first.get());
            for (auto child = node->statements.rbegin(); child != node->statements.rend(); ++child)
                pending.push_back(child->get());
        }
        if (!function_.generic_parameters.empty()) co_return true;
        for (const auto& parameter : function_.parameters)
            types_.local_types.emplace_back(name_key(parameter), parameter.type);
        for (const auto& attribute : function_.attributes)
            for (const auto& binding : attribute.variadic_bindings)
                types_.local_types.emplace_back(name_key(binding), binding.type);
        co_await statement(*function_.body);
        co_return valid_;
    }

private:
    void error(SourceLocation location, std::string message) {
        if (valid_) report_(location, std::move(message));
        valid_ = false;
    }

    // Types are memoized for the current lexical state; every change of the
    // local bindings, and every outermost expression, discards them.
    TypePtr type(const Expr& expression, bool decay = true) {
        return infer_generic_actual(expression, &function_, program_, types_, decay, &types_memo_);
    }

    void forget_types() {
        if (!types_memo_.types.empty()) types_memo_ = {};
    }

    // A register object, including a lane of a register vector, has no address.
    bool register_designator(const Expr& expression) {
        const auto* root = &expression;
        while (root->kind == Expr::Kind::Parenthesized && root->left) root = root->left.get();
        if (root->kind == Expr::Kind::Binary && root->text == "index" && root->left) {
            const auto* base = root->left.get();
            while (base->kind == Expr::Kind::Parenthesized && base->left) base = base->left.get();
            if (const auto vector = type(*base); vector && vector->kind == Type::Kind::Vector)
                root = base;
        }
        return root->kind == Expr::Kind::Name && register_locals_.contains(name_key(*root));
    }

    bool object_lvalue(const Expr& expression) {
        return source_object_lvalue(expression,
            [&](const Expr& operand) { return type(operand); },
            [&](const Expr& name) {
                return std::any_of(types_.local_types.begin(), types_.local_types.end(),
                    [&](const auto& local) { return local.first == name_key(name); }) ||
                    resolve_object(program_, &function_, name);
            });
    }

    bool intrinsic(const Expr& call) {
        MetaIntrinsicArgumentValidator validator(
            [&](const Expr& operand) { return type(operand); },
            [&](SourceLocation location, std::string message) { error(location, std::move(message)); });
        return validator.intrinsic(call);
    }

    void conversion(const Expr& value, const TypePtr& destination, std::string_view context,
                    SourceConversion kind = SourceConversion::Implicit,
                    bool compound = false) {
        const auto source = type(value);
        if (is_meta_type(source) || is_meta_type(destination)) {
            if (value.kind == Expr::Kind::Quote && !is_meta_type(destination))
                error(value.location, "quote cannot enter runtime expressions");
            else if (!conditional_meta_type(source, destination))
                error(value.location, "incompatible meta value in " + std::string(context));
            return;
        }
        record_conversion(source, destination, value.location, context);
        // A compound assignment first computes its underlying operator; its
        // RHS is not independently assignment-converted to the destination.
        if (valid_ && !compound)
            if (const auto* reason = source_conversion_error(source, destination, kind,
                    source_function_designator(value,
                        [&](const Expr& operand, bool decay) { return type(operand, decay); })))
                error(value.location, std::string(reason) + " in " + std::string(context));
    }

    void record_conversion(const TypePtr& source, const TypePtr& destination,
                           SourceLocation location, std::string_view context) {
        if (incompatible_records(source, destination))
            error(location, "a record value requires the same nominal record type in " + std::string(context));
    }

    void complete_initializer_array(const TypePtr& destination, std::uint64_t count,
                                    SourceLocation location) {
        complete_source_array_extent(destination, count, program_.address_bits, location,
            [&](SourceLocation at, std::string message) { error(at, std::move(message)); });
    }

    EvaluationTask<void> initializer(const Expr& source, const TypePtr& destination,
                                     const VariableDecl* declaration = nullptr, bool static_storage = false) {
        if (!valid_ || !destination) co_return;
        const bool inferred = declaration && !declaration->dynamic_array_bound &&
            destination->kind == Type::Kind::Array && !destination->lanes && !destination->array_bound;
        if (source.kind == Expr::Kind::String && destination->kind == Type::Kind::Array &&
            destination->element && destination->element->kind == Type::Kind::Builtin &&
            destination->element->builtin == BuiltinType::U8) {
            const auto text = source.string_value.empty() ? decode_string_literal(source.text)
                : std::optional<std::string>(source.string_value);
            if (!text || (destination->lanes && text->size() >= destination->lanes))
                error(source.location, "string initializer does not fit in the u8 array");
            if (valid_ && inferred) complete_initializer_array(destination, text->size() + 1, source.location);
            if (valid_ && declaration)
                initializer_value_(*declaration, {clone_expr(source), destination, {}});
            co_return;
        }
        if (source.kind != Expr::Kind::AggregateInitializer) {
            if (destination->kind == Type::Kind::Array) {
                error(source.location, "aggregate initializer requires a brace list");
                co_return;
            }
            conversion(source, destination, "local initializer");
            if (valid_ && static_storage) {
                const auto proof = co_await static_initializer_(source, types_.local_types, destination);
                if (!proof.valid && !proof.needs_invocation_context)
                    error(source.location, "static initializer requires a compile-time or link-time value");
            }
            co_return;
        }
        auto normalized = clone_expr(source);
        std::vector<const Expr*> deferred_indices;
        if (!(co_await normalize_initializer_indices_async(*normalized, deferred_indices, types_.local_types,
                constant_, [&](SourceLocation at, std::string message) { error(at, std::move(message)); }))) co_return;
        if (!deferred_indices.empty() ||
            (phase_ == SourceValidationPhase::Definition && evaluation_only(function_))) {
            if (!program_.evaluation_initializer_types) {
                error(source.location, "target initializer type selection is unavailable during source validation");
                co_return;
            }
            const auto plan = co_await program_.evaluation_initializer_types.async(*normalized, destination, deferred_indices);
            if (!plan.valid) {
                error(plan.error_location.file ? plan.error_location : source.location,
                    plan.error_message.empty() ? "invalid aggregate initializer" : plan.error_message);
                co_return;
            }
            for (const auto& [value, type] : plan.items) co_await initializer(*value, type, nullptr, static_storage);
            if (valid_ && inferred) {
                if (plan.outer_extent_deferred)
                    destination->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
                else complete_initializer_array(destination, plan.minimum_elements, source.location);
            }
            co_return;
        }
        if (!program_.evaluation_initializer_plan) {
            error(source.location, "target initializer layout is unavailable during source validation");
            co_return;
        }
        auto plan = co_await program_.evaluation_initializer_plan.async(*normalized, destination);
        if (!plan.valid) {
            error(plan.error_location.file ? plan.error_location : source.location,
                plan.error_message.empty() ? "invalid aggregate initializer" : plan.error_message);
            co_return;
        }
        for (const auto& item : plan.items) co_await initializer(*item.expression, item.type, nullptr, static_storage);
        if (valid_ && inferred) complete_initializer_array(destination, plan.minimum_elements, source.location);
        if (valid_ && declaration)
            initializer_value_(*declaration, {std::move(normalized), destination, std::move(plan)});
    }

    void object_type(const TypePtr& type, SourceLocation location, bool array_element = false) {
        if (!type) return;
        if ((type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void) ||
            type->kind == Type::Kind::Function) {
            error(location, array_element ? "array element requires an object type"
                                          : "local declaration requires an object type");
        } else if (type->kind == Type::Kind::Array) {
            object_type(type->element, location, true);
        } else if (array_element && type->kind == Type::Kind::Vector && type->scalable) {
            error(location, "an array element cannot have scalable-vector type");
        } else if (type->kind == Type::Kind::Record && !program_.record_definition(type->nominal_key())) {
            error(location, "local object requires a complete record type");
        }
        // Pointers may refer to incomplete/void/function types. This is source
        // object legality, not a layout query or an attempt to allocate storage.
    }

    EvaluationTask<void> expression_async(const Expr& node, bool potentially_evaluated = true,
                    bool ordinary_name = true, SourcePatchOperand patch_operand = {},
                    bool direct_callee = false, SourceExpressionUse use = SourceExpressionUse::Value) {
        if (!valid_) co_return;
        struct Depth {
            unsigned& value;
            ~Depth() { --value; }
        } depth{++expression_depth_};
        if (expression_depth_ == 1) forget_types();
        if (const auto issue = source_staging_intrinsic_error(node,
                [&](const Expr& operand) { return type(operand); })) {
            error(issue->location, issue->message);
            co_return;
        }
        if (const auto issue = source_patch_intrinsic_error(node, program_, &function_,
                [&](const Expr& operand, bool decay) { return type(operand, decay); })) {
            error(issue->location, issue->message);
            co_return;
        }
        if (ordinary_name) {
            if (const auto reason = source_name_error(node, program_, &function_,
                    [&](const Expr& operand) { return type(operand, false); }, direct_callee)) {
                error(node.location, *reason);
                co_return;
            }
        }
        // A label type remains usable in an unevaluated layout query, but a
        // value cannot name code that will disappear before runtime lowering.
        // Check every branch before execution/erasure, not just selected ones.
        // A direct raw label operand is a target-owned control-flow resource,
        // not an address value. Only transparent parentheses keep that role;
        // casts and other value operations lose the instruction operand slot.
        if (potentially_evaluated && !patch_operand.instruction && node.kind == Expr::Kind::Name &&
            is_label_type(type(node)) &&
            ((node.name_context && node.name_context->label_address) || !object_lvalue(node))) {
            if (const auto address = resolve_label_constant(node, &function_, program_)) {
                if (const auto* reason = label_address_error(*address)) {
                    error(node.location, reason);
                    co_return;
                }
            }
        }
        const bool evaluated_children = potentially_evaluated &&
            node.kind != Expr::Kind::Sizeof && node.kind != Expr::Kind::Alignof &&
            atomic_builtin(node) != AtomicBuiltin::IsLockFree;
        if (node.left) (co_await expression_async(*node.left, evaluated_children, ordinary_name,
            node.kind == Expr::Kind::Parenthesized ? patch_operand : SourcePatchOperand{},
            node.kind == Expr::Kind::Call || (direct_callee && node.kind == Expr::Kind::Parenthesized),
            source_left_use(node, use)));
        const bool member = node.kind == Expr::Kind::Binary &&
            (node.text == "member" || node.text == "pointer_member");
        if (node.right && !member) (co_await expression_async(*node.right, evaluated_children, ordinary_name));
        if (node.third) (co_await expression_async(*node.third, evaluated_children, ordinary_name));
        std::vector<EvaluationInstructionOperand> instruction_operands(source_instruction_call(node) ? node.arguments.size() : 0);
        for (std::size_t index = 0; index < node.arguments.size(); ++index) {
            const auto& argument = node.arguments[index];
            (co_await expression_async(*argument, evaluated_children,
                ordinary_name && source_argument_names(node, *argument),
                {source_instruction_call(node) ? &node : nullptr, index,
                    source_instruction_call(node) ? &instruction_operands[index] : nullptr}, false,
                source_argument_use(node, index, [&](const Expr& operand) { return type(operand); })));
        }
        std::vector<const Expr*> initializer_children;
        visit_initializer_children(node, [&](const Expr& child) { initializer_children.push_back(&child); });
        for (const auto* child : initializer_children) {
            (co_await expression_async(*child, evaluated_children, ordinary_name));
            if (is_meta_type(type(*child)))
                error(child->location, "meta values cannot initialize runtime aggregate members");
        }
        if (!valid_) co_return;
        if (source_instruction_call(node)) {
            if (const auto issue = co_await source_instruction_error_async(node, program_, &function_,
                    [&](const Expr& operand, bool decay) { return type(operand, decay); },
                    [&](const Expr& operand) { return object_lvalue(operand); },
                    [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                        co_return co_await constant_(operand, types_.local_types, {});
                    }, instruction_operands, !evaluation_only(function_) && phase_ == SourceValidationPhase::Definition,
                    evaluation_only(function_) || phase_ == SourceValidationPhase::Invocation)) error(issue->location, issue->message);
            co_return;
        }
        if (patch_intrinsic(node)) {
            if (const auto issue = co_await source_patch_sink_error_async(node, program_, &function_,
                    [&](const Expr& operand, bool decay) { return type(operand, decay); },
                    [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                        co_return co_await constant_(operand, types_.local_types, {});
                    }))
                error(issue->location, issue->message);
            if (valid_)
                if (const auto issue = co_await source_patch_initial_error_async(node, program_, patch_operand,
                        [&](const Expr& operand, bool decay) { return type(operand, decay); },
                        [&](const Expr& operand, const TypePtr& destination) -> EvaluationTask<SourceInitializerProbe> {
                            co_return co_await static_initializer_(operand, types_.local_types, destination);
                        }, !evaluation_only(function_)))
                    error(issue->location, issue->message);
            co_return;
        }
        if (control_intrinsic(node) != ControlIntrinsic::None) {
            if (const auto issue = co_await source_control_intrinsic_error_async(node,
                    [&](const Expr& operand, bool decay) { return type(operand, decay); },
                    [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                        // Await here so the temporary destination type remains
                        // alive while the nested required probe is suspended.
                        co_return co_await constant_(operand, types_.local_types, {});
                    },
                    !evaluation_only(function_)))
                error(issue->location, issue->message);
            co_return;
        }
        if (atomic_builtin(node) != AtomicBuiltin::None) {
            if (const auto issue = co_await source_atomic_intrinsic_error_async(node,
                    [&](const Expr& operand, bool decay) { return type(operand, decay); },
                    [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                        co_return co_await probe_(operand, types_.local_types);
                    },
                    !evaluation_only(function_)))
                error(issue->location, issue->message);
            co_return;
        }
        if (const auto* reason = source_meta_expression_error(node,
                [&](const Expr& operand, bool decay) { return type(operand, decay); })) {
            error(node.location, reason);
            co_return;
        }
        if (const auto issue = source_object_expression_error(node, program_,
                [&](const Expr& operand, bool decay) { return type(operand, decay); },
                [&](const Expr& operand) { return object_lvalue(operand); }, use)) {
            error(issue->location, issue->message);
            co_return;
        }
        if (node.kind == Expr::Kind::Unary && node.text == "&" && node.left &&
            register_designator(*node.left)) {
            error(node.location, "a register object has no address");
            co_return;
        }
        if (node.kind == Expr::Kind::Binary && node.text == "index" && node.left && node.right) {
            const auto base = type(*node.left);
            if (base && base->kind == Type::Kind::Vector && !base->scalable) {
                const auto probe = co_await probe_(*node.right, types_.local_types);
                const auto& value = probe.value;
                if (value && (integer_negative(value->value,
                        evaluation_integer_type(builtin_type(value->type), program_.address_bits)) ||
                        value->value.high != 0 || value->value.low >=
                            (base->lanes ? base->lanes : std::numeric_limits<std::uint32_t>::max())))
                    error(node.right->location, "fixed-vector lane index is out of range");
            }
        }
        if (!valid_) co_return;
        if (node.kind == Expr::Kind::Quote) {
            if (const auto* argument = invalid_unquote_argument(node,
                    [&](const Expr& operand) { return type(operand); }))
                error(argument->location, "$::unquote requires a token value or syntax node");
        } else if (node.kind == Expr::Kind::Sizeof || node.kind == Expr::Kind::Alignof) {
            const auto queried = node.type ? node.type : node.left ? type(*node.left, false) : nullptr;
            if (const auto* reason = source_layout_query_error(node, queried, program_,
                         [&](const Expr& operand) { return type(operand, false); }))
                error(node.location, reason);
        } else if (node.kind == Expr::Kind::Offsetof) {
            if (const auto issue = source_offsetof_error(node, program_)) error(issue->location, issue->message);
        } else if (node.kind == Expr::Kind::Cast && node.left) {
            conversion(*node.left, node.type, "cast", SourceConversion::Explicit);
        } else if (node.kind == Expr::Kind::Assign && node.left && node.right) {
            const auto destination = type(*node.left, false);
            conversion(*node.right, destination, "assignment", SourceConversion::Implicit, node.text != "=");
        } else if (node.kind == Expr::Kind::Conditional && node.left && node.right && node.third) {
            const auto selector = type(*node.left), yes = type(*node.right), no = type(*node.third);
            if (selector && !is_scalar(selector))
                error(node.location, (is_meta_type(selector) || is_meta_type(yes) || is_meta_type(no))
                    ? "conditional meta selector must be scalar" : "conditional selector must be scalar");
        } else if (node.kind == Expr::Kind::Call && !intrinsic(node)) {
            if (staging_intrinsic(node) != StagingIntrinsic::None || patch_intrinsic(node)) co_return;
            auto signature = node.left ? type(*node.left) : nullptr;
            if (signature && signature->kind == Type::Kind::Pointer) signature = signature->pointee;
            if (signature && signature->kind != Type::Kind::Function) {
                error(node.location, "called expression does not have a function type");
                co_return;
            }
            const auto* callable = signature && signature->kind == Type::Kind::Function
                ? signature->function.get() : nullptr;
            if (callable) {
                const bool meta_interface = is_meta_type(callable->result) ||
                    std::any_of(callable->parameters.begin(), callable->parameters.end(),
                        [](const ParameterDecl& parameter) { return is_meta_type(parameter.type); });
                if (node.arguments.size() < callable->parameters.size() ||
                    (!callable->variadic && node.arguments.size() != callable->parameters.size()))
                    error(node.location, meta_interface ? "meta helper call has an invalid argument count"
                        : "function call requires " + std::to_string(callable->parameters.size()) +
                            (callable->variadic ? " or more" : "") + " arguments");
                for (std::size_t index = 0; index < node.arguments.size(); ++index) {
                    if (index < callable->parameters.size()) {
                        const auto& argument = *node.arguments[index];
                        const auto& parameter = callable->parameters[index];
                        const auto actual = type(argument, false);
                        // Out arguments supply no input value. Nonmodifiable
                        // actuals receive discarded copy-out, so even their
                        // record type need not match the parameter cell.
                        if (parameter.mode != ParameterMode::Out || is_meta_type(actual) || is_meta_type(parameter.type))
                            conversion(argument, parameter.type, "call argument");
                        if (parameter.mode != ParameterMode::In && actual && !actual->is_const &&
                            actual->kind != Type::Kind::Array && actual->kind != Type::Kind::Function &&
                            object_lvalue(argument)) {
                            record_conversion(parameter.type, actual, argument.location, "parameter copy-out");
                            if (valid_)
                                if (const auto* reason = source_conversion_error(parameter.type, actual))
                                    error(argument.location, std::string(reason) + " in parameter copy-out");
                        }
                    } else {
                        const auto actual = type(*node.arguments[index]);
                        if (is_meta_type(actual))
                            error(node.arguments[index]->location, "meta values cannot enter variadic arguments");
                        else if (const auto* reason = source_value_error(actual))
                            error(node.arguments[index]->location, std::string(reason) + " in variadic argument");
                    }
                }
            } else {
                for (const auto& argument : node.arguments)
                    if (is_meta_type(type(*argument)))
                        error(argument->location, "meta argument requires a visible typed helper interface");
            }
        }
        if (valid_) {
            bool deferred{};
            if (const auto* reason = co_await source_operator_error_async(node, program_,
                    [&](const Expr& operand) { return type(operand); },
                    [&](Expr& operand) -> EvaluationTask<bool> {
                        auto probe = co_await probe_(operand, types_.local_types);
                        deferred = probe.needs_invocation_context;
                        auto& value = probe.value;
                        if (value && value->value != UInt128{}) value.reset();
                        operand.evaluated_integer = value;
                        co_return value.has_value();
                    }))
                if (!deferred) error(node.location, reason);
        }
    }

    bool invocation_dependent_case(const Expr& node) {
        if (!evaluation_only(function_)) return false;
        std::vector<const Expr*> pending{&node};
        while (!pending.empty()) {
            const auto& current = *pending.back();
            pending.pop_back();
            if (current.kind == Expr::Kind::Name &&
                std::any_of(types_.local_types.begin(), types_.local_types.end(),
                    [&](const auto& local) { return local.first == name_key(current); })) return true;
            if (current.kind == Expr::Kind::Sizeof || current.kind == Expr::Kind::Alignof ||
                atomic_builtin(current) == AtomicBuiltin::IsLockFree) {
                // Layout operands do not read their cells. A VLA's sizeof is
                // the exception: its captured extent belongs to this invocation.
                if (current.kind == Expr::Kind::Sizeof) {
                    auto queried = current.type ? current.type
                        : current.left ? type(*current.left, false) : TypePtr{};
                    for (; queried && queried->kind == Type::Kind::Array; queried = queried->element)
                        if (queried->lanes == 0 &&
                            queried->array_extent_dependency != Type::ArrayExtentDependency::ExpansionContext)
                            return true;
                }
                continue;
            }
            // Preserve the original preorder and opaque layout edges. This is
            // dependency classification, not execution or a cached source proof.
            for (auto argument = current.generic_arguments.rbegin(); argument != current.generic_arguments.rend(); ++argument)
                if (argument->value) pending.push_back(argument->value.get());
            for (auto argument = current.arguments.rbegin(); argument != current.arguments.rend(); ++argument)
                pending.push_back(argument->get());
            if (current.third) pending.push_back(current.third.get());
            if (current.right) pending.push_back(current.right.get());
            if (current.left) pending.push_back(current.left.get());
        }
        return false;
    }

    EvaluationTask<void> case_label(Statement& node) {
        if (!valid_ || !node.expression || switches_.empty()) co_return;
        const auto label = type(*node.expression);
        if (label && !is_integer(label)) {
            error(node.location, "case requires an integer constant expression");
            co_return;
        }
        // The definition/invocation timing of translation-only dependent cases
        // is still a language-design question. Preserve the existing reached
        // evaluator path; do not impose a new definition-time restriction here.
        if (invocation_dependent_case(*node.expression)) co_return;
        const auto probe = co_await constant_(*node.expression, types_.local_types, {});
        if (probe.needs_invocation_context) co_return;
        const auto& value = probe.value;
        if (!value) {
            error(node.location, "case requires a translation-time integer constant");
            co_return;
        }
        auto& owner = switches_.back();
        if (owner.type.bits != 0) {
            const auto from = evaluation_integer_type(builtin_type(value->type), program_.address_bits);
            const auto converted = convert_integer(value->value, from, owner.type);
            if (convert_integer(converted, owner.type, from) != value->value ||
                integer_negative(converted, owner.type) != integer_negative(value->value, from)) {
                error(node.location, "case value is not representable in the promoted selector type");
                co_return;
            }
            if (std::find(owner.values.begin(), owner.values.end(), converted) != owner.values.end()) {
                error(node.location, "duplicate case value in switch");
                co_return;
            }
            owner.values.push_back(converted);
        }
        // Selection precedes block storage, so retain type-only lexical results
        // in the evaluator's invocation context. Only runtime cases are folded
        // in the AST; translation-only source may run in another expansion.
        case_value_(node, *value);
        if (!evaluation_only(function_))
            replace_eval_value(node.expression, EvalValue{value->value, builtin_type(value->type)});
    }

    static bool label_matches(const Statement& candidate, const NameKey& key, const LabelBinding& binding) {
        if (binding.scope && binding.scope != candidate.label_binding.scope) return false;
        if (binding.kind == LabelBinding::Kind::Reference &&
            binding.declaration != candidate.label_binding.declaration) return false;
        const auto location = candidate.label_location.valid() ? candidate.label_location : candidate.location;
        return candidate.global_label || NameKey(candidate.label_name, location) == key;
    }

    bool direct_goto(const Expr& target) {
        if (target.kind != Expr::Kind::Name || target.text.find("::") != std::string::npos ||
            (target.name_context && target.name_context->label_address)) return false;
        const auto binding = resolved_label_binding(NameUse(target));
        const NameKey key(target.text, target.location);
        const auto found = labels_.find(target.text);
        if (found != labels_.end()) {
            for (const auto* candidate : found->second)
                if (label_matches(*candidate, key, binding)) return true;
        }
        if (binding.kind == LabelBinding::Kind::Reference)
            error(target.location, "goto target does not name a visible label in its retained source binding");
        return false;
    }

    EvaluationTask<void> statement(Statement& node) {
        if (!valid_) co_return;
        // Control ownership is source legality, not an execution effect.
        // Check it even in unused helpers and untaken branches that disappear
        // before runtime lowering. Case/default bind to the nearest switch;
        // a nested loop does not create a new switch-label namespace.
        if (node.kind == Statement::Kind::Break && loop_depth_ == 0 && switches_.empty())
            error(node.location, "break has no enclosing loop or switch");
        if (node.kind == Statement::Kind::Continue && loop_depth_ == 0)
            error(node.location, "continue has no enclosing loop");
        if (node.kind == Statement::Kind::Case || node.kind == Statement::Kind::Default) {
            if (switches_.empty()) {
                error(node.location, "case/default has no enclosing switch");
            } else if (node.kind == Statement::Kind::Default) {
                if (switches_.back().has_default) error(node.location, "duplicate default label in switch");
                switches_.back().has_default = true;
            }
        }
        if (!valid_) co_return;
        const auto saved = types_.local_types.size();
        const bool scoped = node.kind == Statement::Kind::Compound || node.kind == Statement::Kind::For;
        if (node.kind == Statement::Kind::For && node.first) co_await statement(*node.first);
        if (node.declaration) {
            auto& declaration = *node.declaration;
            if (contains_meta_type(declaration.type) && !is_meta_type(declaration.type))
                error(declaration.location, "meta values cannot have runtime local storage");
            object_type(declaration.type, declaration.location);
            // Complete inferred extents in this validation's lexical types, not
            // in a shared AST that may be reused by another expansion/model.
            const auto destination = clone_type(declaration.type);
            types_.local_types.emplace_back(name_key(declaration), destination);
            forget_types();
            if (declaration.storage_register) {
                if (destination && (destination->kind == Type::Kind::Array ||
                                    destination->kind == Type::Kind::Record))
                    error(declaration.location,
                          "an aggregate object cannot be bound to a machine register");
                register_locals_.insert(name_key(declaration));
            }
            if (is_meta_type(declaration.type) &&
                (declaration.type->is_volatile || declaration.type->is_atomic ||
                 declaration.storage_static || declaration.storage_register || declaration.storage_stack ||
                 declaration.location_name || !declaration.attributes.empty()))
                error(declaration.location,
                    "meta cells require automatic translation-only storage without runtime qualifiers");
            // Local alignment is a required source constraint, not an effect
            // of entering this block or retaining a runtime function. Resolve
            // it with this lexical environment before meta-helper erasure.
            for (const auto& attribute : declaration.attributes) {
                if (attribute.name != "aligned" || !valid_) continue;
                if (attribute.arguments.size() != 1 || !attribute.expression_argument) {
                    error(attribute.location, "aligned on a local object requires one integer argument");
                    break;
                }
                co_await expression_async(*attribute.expression_argument);
                if (!valid_) break;
                const auto probe = co_await constant_(*attribute.expression_argument, types_.local_types, {});
                if (probe.needs_invocation_context) continue;
                const auto& value = probe.value;
                if (!value) {
                    error(attribute.location, "aligned argument must be an integer constant");
                    break;
                }
                const auto alignment = alignment_value(*value);
                if (!alignment) {
                    error(attribute.location,
                        "aligned argument must be a positive power-of-two integer constant");
                    break;
                }
                if (!evaluation_only(function_))
                    declaration.explicit_alignment = std::max(declaration.explicit_alignment, *alignment);
            }
            if (declaration.dynamic_array_bound) {
                co_await expression_async(*declaration.dynamic_array_bound);
                const auto bound = type(*declaration.dynamic_array_bound);
                if (bound && !is_integer(bound))
                    error(declaration.location, "array bound must have an integer type");
            }
            if (declaration.initializer) {
                co_await expression_async(*declaration.initializer);
                const auto source = type(*declaration.initializer);
                const bool static_bytes = declaration.storage_static && source && destination &&
                    source->kind == Type::Kind::Bytes && destination->kind == Type::Kind::Array &&
                    destination->element && destination->element->kind == Type::Kind::Builtin &&
                    destination->element->builtin == BuiltinType::U8;
                if (!static_bytes) co_await initializer(*declaration.initializer, destination,
                    &declaration, declaration.storage_static);
                else {
                    const auto proof = co_await static_initializer_(*declaration.initializer,
                        types_.local_types, destination);
                    if (!proof.valid && !proof.needs_invocation_context)
                        error(declaration.initializer->location, "static byte array requires a translation-time byte value");
                    else if (proof.needs_invocation_context && !destination->lanes)
                        destination->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
                }
            }
        }
        if (node.expression) {
            // Only a resolved bare local label bypasses ordinary expression
            // lookup. Otherwise goto requires a label-valued object/expression,
            // even in code erased before runtime lowering.
            const bool direct = node.kind == Statement::Kind::Goto && direct_goto(*node.expression);
            if (!direct) {
                co_await expression_async(*node.expression);
                if (valid_ && node.kind == Statement::Kind::Goto && !is_label_type(type(*node.expression)))
                    error(node.expression->location,
                        "goto target does not name a visible label or label-valued expression");
            }
        }
        if (node.kind == Statement::Kind::Case) co_await case_label(node);
        if (node.kind == Statement::Kind::StaticAssert && node.expression) {
            const auto condition = type(*node.expression);
            if (condition && !is_integer(condition) && !is_floating(condition))
                error(node.expression->location,
                    "$::static_assert condition must have a scalar arithmetic type");
        }
        if (node.kind == Statement::Kind::Return) {
            if (node.expression) {
                const auto source = type(*node.expression);
                // Void forwarding has no value. All other source/target void
                // mismatches are invalid even when this return never executes.
                if (source && function_.return_type && source->kind != Type::Kind::Generic &&
                    is_void_type(source) != is_void_type(function_.return_type))
                    error(node.location, is_void_type(function_.return_type)
                        ? "void function cannot return a value"
                        : "non-void function cannot return a void expression");
                if (valid_) conversion(*node.expression, function_.return_type, "return");
            }
            else if (is_meta_type(function_.return_type)) error(node.location, "meta helper must return a value");
            else if (function_.return_type && !(function_.return_type->kind == Type::Kind::Builtin &&
                     function_.return_type->builtin == BuiltinType::Void))
                error(node.location, "non-void function must return a value");
        }
        if (node.condition) {
            co_await expression_async(*node.condition);
            const auto condition = type(*node.condition);
            if (condition && (node.kind == Statement::Kind::Switch ? !is_integer(condition) : !is_scalar(condition)))
                error(node.condition->location, node.kind == Statement::Kind::Switch
                    ? "switch condition must have an integer type" : "condition must be scalar");
        }
        for (const auto& increment : node.increments) co_await expression_async(*increment);
        const bool loop = node.kind == Statement::Kind::While ||
            node.kind == Statement::Kind::DoWhile || node.kind == Statement::Kind::For;
        if (loop) ++loop_depth_;
        if (node.kind == Statement::Kind::Switch) {
            const auto selector = node.condition ? type(*node.condition) : TypePtr{};
            switches_.push_back({selector ? promote_integer(evaluation_integer_type(selector, program_.address_bits))
                                         : IntegerType{}, false, {}});
        }
        for (const auto& child : node.statements) co_await statement(*child);
        if (node.first && node.kind != Statement::Kind::For) co_await statement(*node.first);
        if (node.second) co_await statement(*node.second);
        if (node.kind == Statement::Kind::Switch) switches_.pop_back();
        if (loop) --loop_depth_;
        if (scoped) types_.local_types.resize(saved);
        forget_types();
    }

    Program& program_;
    const FunctionDecl& function_;
    Report report_;
    Constant constant_;
    Probe probe_;
    CaseValue case_value_;
    InitializerValue initializer_value_;
    StaticInitializer static_initializer_;
    GenericExpansionState types_;
    SourceTypeMemo types_memo_;
    unsigned expression_depth_{};
    std::unordered_set<NameKey, NameKeyHash> register_locals_;
    std::unordered_map<std::string, std::vector<const Statement*>> labels_;
    unsigned loop_depth_{};
    struct Switch {
        IntegerType type;
        bool has_default{};
        std::vector<UInt128> values;
    };
    std::vector<Switch> switches_;
    bool valid_{true};
    SourceValidationPhase phase_;
};

class Evaluator {
    enum class Preparation { Unprepared, Preparing, Ready, Deferred, Invalid };
    struct PreparedRecord {
        std::shared_ptr<RecordDecl> definition;
        Preparation alignment{Preparation::Unprepared};
        Preparation complete{Preparation::Unprepared};
    };
    using RecordViews = std::unordered_map<NominalTypeKey, PreparedRecord, NominalTypeKeyHash>;
public:
    enum class ResourceReporting { Required, Speculative };
    void resource_reporting(ResourceReporting reporting) { resource_reporting_ = reporting; }

    Evaluator(Program& program, Diagnostics& reporter,
              const FunctionDecl* current_function = nullptr,
              std::string current_namespace = {},
              const LayoutQuery* size_of_query = nullptr,
              const LayoutQuery* align_of_query = nullptr,
              const GenericPointerResolver* pointer_resolver = nullptr,
              std::shared_ptr<const SyntaxContext> macro_context = {},
              SyntaxParseCallback syntax_parse = {},
              std::shared_ptr<const SyntaxContext> call_context = {})
        : program_(program), diagnostics_(reporter),
          current_function_(current_function),
          current_namespace_(std::move(current_namespace)),
          size_of_(size_of_query ? size_of_query :
              (program.evaluation_size_of ? &program.evaluation_size_of : nullptr)),
          align_of_(align_of_query ? align_of_query :
              (program.evaluation_align_of ? &program.evaluation_align_of : nullptr)),
          pointer_resolver_(pointer_resolver ? pointer_resolver :
              (program.evaluation_pointer_resolver ? &program.evaluation_pointer_resolver : nullptr)),
          procedural_(macro_context != nullptr), macro_context_(std::move(macro_context)),
          syntax_parse_(std::move(syntax_parse)), call_context_(std::move(call_context)) {
        previous_record_query_ = std::move(program_.evaluation_record_definition);
        inherited_record_query_ = previous_record_query_;
        previous_layout_preparation_ = std::move(program_.evaluation_prepare_layout);
        previous_layout_scope_ = std::move(program_.evaluation_layout_scope);
        program_.evaluation_layout_scope = layout_scope_;
        program_.evaluation_record_definition = [this](const NominalTypeKey& key) -> std::shared_ptr<const RecordDecl> {
            if (const auto found = record_views_->find(key); found != record_views_->end()) return found->second.definition;
            return inherited_record_query_ ? inherited_record_query_(key) : nullptr;
        };
        program_.evaluation_prepare_layout = [this](const TypePtr& type, EvaluationLayoutKind kind) -> EvaluationTask<bool> {
            const auto result = co_await prepare_layout_async(type, kind);
            if (result == Preparation::Deferred) fail_context(current_function_ ? current_function_->location : SourceLocation{});
            co_return result == Preparation::Ready;
        };
        previous_integer_query_ = std::move(program_.evaluation_required_integer);
        previous_generic_query_ = std::move(program_.evaluation_generic_value);
        program_.evaluation_generic_value = [this](const Expr& source, const TypePtr& destination,
            Diagnostics& diagnostics, const FunctionDecl* caller,
            std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context)
                -> EvaluationTask<EvaluationGenericValueResult> {
            co_return co_await generic_value_requirement_async(source, destination, diagnostics, caller, local_types, context);
        };
        program_.evaluation_required_integer = [this](const Expr& source, Diagnostics& diagnostics,
            const LayoutQuery& size_of, const LayoutQuery& align_of, std::string_view source_namespace,
            const FunctionDecl* caller, std::span<const std::pair<NameKey, TypePtr>> local_types,
            EvaluationIntegerContext context) -> EvaluationTask<EvaluationIntegerResult> {
            EvaluationIntegerResult result;
            const auto errors = diagnostics.errors();
            bool needs_context{};
            // Namespace-only synthetic callers are not lexical expansion
            // owners. The requester must supply the actual source definition.
            const bool invocation = context == EvaluationIntegerContext::CallerInvocation &&
                caller && caller->definition();
            const bool optional = context == EvaluationIntegerContext::ProbeDefinition;
            const auto value = co_await isolated_source_probe_async<EvalValue>(nullptr, !optional,
                [&](Evaluator& probe) -> EvaluationTask<std::optional<EvalValue>> {
                    probe.current_function_ = caller;
                    probe.current_namespace_ = source_namespace;
                    if (size_of) probe.size_of_ = &size_of;
                    if (align_of) probe.align_of_ = &align_of;
                    // Persistent declaration preparation must not borrow an
                    // invocation, nor can a file-scope requirement acquire one
                    // just because a macro requests that global layout.
                    if (!invocation) {
                        probe.procedural_ = false;
                        probe.macro_context_.reset();
                        probe.syntax_parse_ = {};
                        probe.call_context_.reset();
                        probe.reset_record_views();
                    }
                    if (context == EvaluationIntegerContext::StagedDefinition && caller &&
                        caller->definition() && evaluation_only(*caller)) {
                        const auto probed = co_await probe.probe_integer_async(source, local_types, &needs_context, true);
                        if (!probed) co_return std::nullopt;
                        co_return EvalValue{probed->value, builtin_type(probed->type)};
                    }
                    co_return co_await probe.required_integer_with_types_async(source, local_types);
                }, optional);
            if (value) {
                result.status = EvaluationIntegerResult::Status::Value;
                result.value = Expr::IntegerConstant{value->integer, value->type->builtin};
            } else if (needs_context) {
                result.status = EvaluationIntegerResult::Status::ContextUnavailable;
            } else if (context_unavailable_ && !resource_exhausted_ && invocation &&
                       !procedural_ && evaluation_only(*caller) && diagnostics.errors() == errors) {
                result.status = EvaluationIntegerResult::Status::ContextUnavailable;
            } else if (diagnostics.errors() == errors && (!optional || resource_exhausted_)) {
                diagnose(diagnostics, source.location);
            }
            co_return result;
        };
    }

    ~Evaluator() {
        program_.evaluation_required_integer = std::move(previous_integer_query_);
        program_.evaluation_generic_value = std::move(previous_generic_query_);
        program_.evaluation_record_definition = std::move(previous_record_query_);
        program_.evaluation_prepare_layout = std::move(previous_layout_preparation_);
        program_.evaluation_layout_scope = std::move(previous_layout_scope_);
    }

    void reset_record_views() {
        record_views_ = std::make_shared<RecordViews>();
        layout_scope_ = std::make_shared<EvaluationLayoutScopeIdentity>();
        program_.evaluation_layout_scope = layout_scope_;
        inherited_record_query_ = {};
    }

    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    bool charge_input_tokens(const TokenSequence& tokens, SourceLocation location) {
        std::size_t size = 0;
        const auto byte_limit = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        for (const auto& token : tokens) {
            const auto metadata_cost = meta_token_storage_bytes + origin_binding_storage(token.origin);
            if (!charge_input_context(token.origin.context, location)) return false;
            if (token.splice && !charge_input_node(*token.splice, location)) return false;
            if (size > byte_limit || token.text.size() > byte_limit - size ||
                metadata_cost > byte_limit - size - token.text.size()) {
                fail_resource(location, "translation-time token input budget exceeded " +
                    std::to_string(byte_limit) + " bytes");
                return false;
            }
            size += token.text.size() + metadata_cost;
        }
        return charge_meta_bytes(size, location);
    }

    bool charge_input_node(const SyntaxNode& node, SourceLocation location) {
        const auto size = syntax_node_storage(node,
            std::min(program_.evaluation_limits.bytes, program_.evaluation_limits.memory));
        if (size > program_.evaluation_limits.bytes ||
            size > std::numeric_limits<std::size_t>::max()) {
            fail_resource(location, "public syntax tree exceeds translation-time storage capacity");
            return false;
        }
        return charge_meta_bytes(static_cast<std::size_t>(size), location);
    }

    bool charge_input_match(const SyntaxMatchValue& match, SourceLocation location) {
        struct Frame {
            const SyntaxMatchValue* match;
            std::size_t field{};
            std::size_t record{};
            bool entered{};
            bool field_entered{};
        };
        std::vector<Frame> pending{{&match}};
        while (!pending.empty()) {
            auto& frame = pending.back();
            const auto& current = *frame.match;
            if (!frame.entered) {
                // Preserve the original preorder and per-occurrence logical
                // charge, including repeated records that share one identity.
                if (!charge_input_context(current.context, location) ||
                    !charge_meta_bytes(syntax_match_storage_bytes, location) ||
                    !charge_input_tokens(current.input, location)) return false;
                if (current.variant && !charge_meta_bytes(32 + current.variant->size(), location)) return false;
                for (const auto& label : current.variant_labels)
                    if (!charge_meta_bytes(32 + label.size(), location)) return false;
                frame.entered = true;
            }
            if (frame.field == current.fields.size()) {
                pending.pop_back();
                continue;
            }
            const auto& field = current.fields[frame.field];
            if (!frame.field_entered) {
                if (!charge_meta_bytes(syntax_field_storage_bytes + field.name.size(), location) ||
                    !charge_input_tokens(field.tokens, location)) return false;
                if (field.node && !charge_input_node(*field.node, location)) return false;
                frame.field_entered = true;
            }
            if (frame.record < field.records.size()) {
                const auto* child = field.records[frame.record++].get();
                pending.push_back({child});
                continue;
            }
            ++frame.field;
            frame.record = 0;
            frame.field_entered = false;
        }
        return true;
    }

    // Synchronous entry points for parser/layout/source-validation clients.
    // Execution-to-execution edges below await continuations on one pump.
    std::optional<EvalValue> expression(const Expr& source) {
        return expression_async(source).run();
    }
    std::optional<EvalValue> call(const FunctionDecl& function,
        const std::vector<EvalValue>& arguments, SourceLocation location) {
        return call_values_async(function, arguments, location).run();
    }
    EvaluationTask<std::optional<EvalValue>> call_values_async(const FunctionDecl& function,
        std::vector<EvalValue> arguments, SourceLocation location) {
        // Value-only entry clients have no source actuals to capture. Do not
        // silently discard outputs when such a client attempts an ordinary call.
        std::vector<EvaluatedCallArgument> captured;
        for (const auto& parameter : function.parameters) {
            if (parameter.mode != ParameterMode::In) {
                fail(location, "translation-time output call requires captured source arguments");
                co_return std::nullopt;
            }
        }
        for (const auto& argument : arguments)
            captured.push_back({argument, {}, location});
        co_return co_await call_async(function, captured, location);
    }
    std::optional<EvalValue> required_integer_with_types(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> types, const TypePtr& destination = {}) {
        return required_integer_with_types_async(source, types, destination).run();
    }
    std::optional<EvalValue> required_scalar_with_types(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> types) {
        return required_scalar_with_types_async(source, types).run();
    }
    std::optional<EvalValue> required_integer(const Expr& source, const TypePtr& destination = {}) {
        return required_integer_async(source, destination).run();
    }
    std::optional<EvalValue> required_scalar(const Expr& source) {
        return required_scalar_async(source).run();
    }
    bool validate_required_tree(const Expr& source) {
        return validate_required_tree_async(source).run();
    }

    EvaluationTask<std::optional<EvalValue>> required_integer_with_types_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> local_types, const TypePtr& destination = {}) {
        co_return (co_await required_value_with_types_async(source, local_types, false, destination));
    }

    EvaluationTask<std::optional<EvalValue>> required_scalar_with_types_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> local_types) {
        co_return (co_await required_value_with_types_async(source, local_types, true));
    }

    EvaluationTask<std::optional<EvalValue>> required_value_with_types_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> local_types, bool scalar, const TypePtr& destination = {}) {
        std::unique_ptr<Expr> prepared;
        if (validation_phase_ == SourceValidationPhase::Invocation && program_.evaluation_prepare_expression) {
            std::vector<const Expr*> pending{&source};
            bool deferred{};
            while (!pending.empty() && !deferred) {
                const auto* node = pending.back(); pending.pop_back();
                if (!node) continue;
                deferred = node->deferred_generic_signature != nullptr;
                pending.push_back(node->left.get());
                pending.push_back(node->right.get());
                pending.push_back(node->third.get());
                for (const auto& argument : node->arguments) pending.push_back(argument.get());
                for (const auto& argument : node->generic_arguments) pending.push_back(argument.value.get());
                for (const auto& entry : node->initializer_entries) {
                    pending.push_back(entry.value.get());
                    for (const auto& designator : entry.designators) pending.push_back(designator.index.get());
                }
            }
            if (deferred) {
                prepared = clone_expr(source);
                const auto prepare = program_.evaluation_prepare_expression;
                if (!(co_await prepare.async(prepared, current_function_, local_types))) co_return std::nullopt;
            }
        }
        const auto& expression = prepared ? *prepared : source;
        push_source_types(local_types);
        std::optional<EvalValue> value;
        if (scalar) value = co_await required_scalar_async(expression);
        else value = co_await required_integer_async(expression, destination);
        pop_scope();
        co_return value;
    }

    void push_source_types(std::span<const std::pair<NameKey, TypePtr>> local_types) {
        // Types are available to unevaluated layout queries; runtime cell
        // values are deliberately unavailable even during an evaluated call.
        scopes_.emplace_back();
        if (current_function_)
            for (const auto& parameter : current_function_->parameters)
                scopes_.back()[name_key(parameter)] = {
                    EvalValue{UInt128{}, parameter.type}, false, parameter.type->is_const, true};
        if (current_function_)
            for (const auto& attribute : current_function_->attributes)
                for (const auto& binding : attribute.variadic_bindings)
                    scopes_.back()[name_key(binding)] = {
                        EvalValue{UInt128{}, binding.type}, false, binding.type->is_const, true};
        for (const auto& [name, type] : local_types)
            scopes_.back()[name] = {EvalValue{UInt128{}, type}, false, type && type->is_const, true};
    }

    EvaluationTask<std::optional<EvalValue>> generic_value_with_types_async(const Expr& source,
        const TypePtr& destination, std::span<const std::pair<NameKey, TypePtr>> local_types) {
        push_source_types(local_types);
        struct Pop { Evaluator& evaluator; ~Pop() { evaluator.pop_scope(); } } pop{*this};
        if (is_integer(destination)) co_return co_await required_integer_async(source, destination);
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        const auto from = expression_type(source);
        if (is_label_type(destination)) {
            if (!is_label_type(from)) {
                fail(source.location, "generic label argument requires a visible label address constant");
                co_return std::nullopt;
            }
        } else if (destination && destination->kind == Type::Kind::Pointer) {
            if (!from || (from->kind != Type::Kind::Pointer && !is_integer(from))) {
                fail(source.location, "generic pointer argument requires a compatible address constant");
                co_return std::nullopt;
            }
            if (const auto* reason = source_conversion_error(from, destination, SourceConversion::Implicit,
                    source_function_designator(source, [&](const Expr& node, bool decay) {
                        return expression_type(node, decay);
                    }))) {
                fail(source.location, reason);
                co_return std::nullopt;
            }
        } else {
            fail(source.location, "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
            co_return std::nullopt;
        }
        if (has_context_dependent_type_bound(destination)) {
            fail_context(source.location);
            co_return std::nullopt;
        }
        auto value = co_await expression_async(source);
        if (!value) co_return std::nullopt;
        value = (co_await convert_async(*value, destination, source.location));
        if (value && destination->kind == Type::Kind::Pointer && !value->address) {
            fail(source.location, "translation-time object pointer cannot escape into a generic argument");
            co_return std::nullopt;
        }
        co_return value;
    }

    EvaluationTask<EvaluationGenericValueResult> generic_value_requirement_async(const Expr& source,
        const TypePtr& destination, Diagnostics& diagnostics, const FunctionDecl* caller,
        std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context) {
        const auto errors = diagnostics.errors();
        bool needs_context{};
        const bool invocation = context == EvaluationIntegerContext::CallerInvocation && caller && caller->definition();
        const auto value = co_await isolated_source_probe_async<EvalValue>(nullptr, true,
            [&](Evaluator& probe) -> EvaluationTask<std::optional<EvalValue>> {
                probe.current_function_ = caller;
                probe.current_namespace_ = caller ? caller->source_namespace : std::string{};
                probe.validation_phase_ = invocation ? SourceValidationPhase::Invocation : SourceValidationPhase::Definition;
                if (!invocation) {
                    probe.procedural_ = false;
                    probe.macro_context_.reset();
                    probe.call_context_.reset();
                    probe.syntax_parse_ = {};
                    probe.reset_record_views();
                }
                if (context == EvaluationIntegerContext::StagedDefinition && caller && evaluation_only(*caller))
                    co_return co_await probe.isolated_source_probe_async<EvalValue>(&needs_context, true,
                        [&](Evaluator& check) -> EvaluationTask<std::optional<EvalValue>> {
                            co_return co_await check.generic_value_with_types_async(source, destination, local_types);
                        });
                co_return co_await probe.generic_value_with_types_async(source, destination, local_types);
            });
        if (value) {
            auto expression = value_expression(*value, source.location);
            if (expression && is_integer(value->type)) {
                const auto shape = integer_type(value->type);
                expression->text = integer_negative(value->integer, shape)
                    ? "-" + to_decimal(mask_to(negate(value->integer), shape.bits)) + literal_suffix(value->type)
                    : to_decimal(value->integer) + literal_suffix(value->type);
            }
            if (expression) co_return EvaluationGenericValueResult{
                EvaluationGenericValueResult::Status::Value, std::move(expression)};
        }
        if (needs_context) co_return EvaluationGenericValueResult{
            EvaluationGenericValueResult::Status::ContextUnavailable, {}};
        if (diagnostics.errors() == errors) diagnose(diagnostics, source.location);
        co_return EvaluationGenericValueResult{};
    }

    EvaluationTask<std::optional<Expr::IntegerConstant>> probe_integer_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> local_types, bool* needs_context = nullptr,
        bool required = false, const TypePtr& destination = {}) {
        if (resource_exhausted_ || (!required && !source_constant_candidate(source))) co_return std::nullopt;
        const auto value = co_await isolated_source_probe_async<EvalValue>(needs_context, required,
            [&](Evaluator& probe) -> EvaluationTask<std::optional<EvalValue>> {
                co_return co_await probe.required_integer_with_types_async(source, local_types, destination);
            });
        if (!value) co_return std::nullopt;
        co_return Expr::IntegerConstant{value->integer, value->type->builtin};
    }

    template<class Value, class Operation>
    EvaluationTask<std::optional<Value>> isolated_source_probe_async(bool* needs_context,
        bool required, const Operation& operation, bool optional_definition = false) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        // Values stay isolated, but a nested source proof is part of the same
        // logical evaluation. Carry capabilities, construction identity, budgets
        // and ancestry across the suspension, but never the caller's value cells.
        std::ostringstream output;
        Diagnostics quiet(output);
        Evaluator probe(program_, quiet, current_function_, current_namespace_, size_of_, align_of_,
            pointer_resolver_, macro_context_, syntax_parse_, call_context_);
        probe.record_views_ = record_views_;
        probe.layout_scope_ = layout_scope_;
        program_.evaluation_layout_scope = layout_scope_;
        probe.inherited_record_query_ = inherited_record_query_;
        probe.validation_phase_ = validation_phase_;
        probe.generic_definitions_validated_ = generic_definitions_validated_;
        probe.depth_ = depth_;
        probe.steps_ = steps_;
        probe.meta_bytes_ = meta_bytes_;
        probe.token_bytes_ = token_bytes_;
        probe.counted_asset_backings_ = counted_asset_backings_;
        probe.construction_contexts_ = construction_contexts_;
        probe.charged_contexts_ = charged_contexts_;
        probe.charged_environments_ = charged_environments_;
        probe.fresh_ordinal_ = fresh_ordinal_;
        probe.call_stack_ = call_stack_;
        probe.resource_reporting_ = resource_reporting_;
        const auto value = co_await operation(probe);
        // Successful source probes may validate newly instantiated generic
        // bodies. Retain those definition proofs, never execution values, so
        // nested validation does not revisit every suffix of the same set.
        if (value)
            generic_definitions_validated_.insert(probe.generic_definitions_validated_.begin(),
                probe.generic_definitions_validated_.end());
        steps_ = probe.steps_;
        meta_bytes_ = probe.meta_bytes_;
        token_bytes_ = probe.token_bytes_;
        counted_asset_backings_ = std::move(probe.counted_asset_backings_);
        construction_contexts_ = std::move(probe.construction_contexts_);
        charged_contexts_ = std::move(probe.charged_contexts_);
        charged_environments_ = std::move(probe.charged_environments_);
        fresh_ordinal_ = probe.fresh_ordinal_;
        if (!value && probe.context_unavailable_ && !probe.resource_exhausted_) {
            if (optional_definition) co_return std::nullopt;
            if (needs_context && !procedural_ && current_function_ && evaluation_only(*current_function_)) {
                *needs_context = true;
                co_return std::nullopt;
            } else context_unavailable_ = true;
        }
        if (probe.resource_exhausted_ || (!value && (required || probe.context_unavailable_))) {
            resource_exhausted_ = resource_exhausted_ || probe.resource_exhausted_;
            if (!failure_reason_) {
                failure_location_ = probe.failure_location_;
                failure_reason_ = std::move(probe.failure_reason_);
                failure_trace_ = std::move(probe.failure_trace_);
            }
            co_return std::nullopt;
        }
        co_return value;
    }

    EvaluationTask<SourceInitializerProbe> probe_initializer_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> types, const TypePtr& destination, bool defer_context) {
        SourceInitializerProbe result;
        const auto proof = co_await isolated_source_probe_async<bool>(
            defer_context ? &result.needs_invocation_context : nullptr, true,
            [&](Evaluator& probe) -> EvaluationTask<std::optional<bool>> {
                probe.push_source_types(types);
                const bool valid = co_await probe.static_initializer_async(source, destination,
                    &result.symbolic, &result.integer);
                probe.pop_scope();
                co_return valid ? std::optional<bool>{true} : std::nullopt;
            });
        result.valid = proof.has_value();
        co_return result;
    }

    bool integer_expression(const Expr& source) {
        return is_integer(expression_type(source));
    }

    // A source proof can retain a link-time value without inventing its bits.
    // Keep this separate from EvalValue: ordinary evaluation still cannot inspect
    // emitted addresses, and source checking creates neither storage nor relocations.
    struct StaticValue {
        std::optional<EvalValue> value;
        TypePtr type;
        TypePtr address_type;
        RelocationAddend addend{};
    };

    bool static_addend_offset(RelocationAddend& base, RelocationAddend offset,
                              std::uint64_t scale, bool addition, SourceLocation location) {
        const auto result = offset_relocation_addend(base, offset, scale, addition);
        if (!result || (program_.evaluation_relocation_addend &&
                        !program_.evaluation_relocation_addend(*result))) {
            fail(location, "static address addend exceeds the supported relocation range");
            return false;
        }
        base = *result;
        return true;
    }

    EvaluationTask<bool> static_address_offset_async(StaticValue& base, RelocationAddend offset,
                               std::uint64_t scale, bool addition, SourceLocation location) {
        if (base.address_type)
            co_return static_addend_offset(base.addend, offset, scale, addition, location);
        if (!pointer_resolver_ || !base.value || !base.value->address ||
            base.value->address->kind != AddressConstant::Kind::Absolute) co_return false;
        // Absolute addresses have known target bits, not relocation addends.
        // In particular, the relocation-record range is not their address range.
        const auto delta = offset_relocation_addend({}, offset, scale, addition);
        std::optional<std::uint64_t> bytes;
        if (size_of_) bytes = co_await size_of_->async(base.type);
        if (!delta || !bytes || !*bytes || *bytes > 16) {
            fail(location, "static absolute address requires a representable target pointer offset");
            co_return false;
        }
        const auto before = base.value->address->absolute;
        const auto after = delta->negative ? subtract(before, delta->magnitude) : add(before, delta->magnitude);
        if ((delta->negative && before < delta->magnitude) ||
            (!delta->negative && after < before) || !fits_unsigned(after, static_cast<unsigned>(*bytes * 8))) {
            fail(location, "static absolute address arithmetic overflows the selected target width");
            co_return false;
        }
        base.value->address->absolute = after;
        co_return true;
    }

    EvaluationTask<std::optional<StaticValue>> static_designator_async(
        const Expr& source, bool* known_symbolic = nullptr) {
        if (!step(source.location)) co_return std::nullopt;
        if (source.kind == Expr::Kind::Parenthesized && source.left)
            co_return co_await static_designator_async(*source.left, known_symbolic);
        if (source.kind == Expr::Kind::Name) {
            const auto object = source_object_declaration(source, program_, current_function_);
            if (object.storage == SourceObjectStorage::ThreadLocal) {
                // The ordinary static relocation contract does not describe a
                // per-thread address. Check this before a contextual addend can
                // defer the proof or an erased helper can bypass Data IR.
                fail(source.location, "a thread-local address is not an ordinary static relocation");
                co_return std::nullopt;
            }
            if (object.storage == SourceObjectStorage::Static ||
                (object.storage == SourceObjectStorage::NotObject && direct_function(source))) {
                if (known_symbolic) *known_symbolic = true;
                const auto type = pointer_type(expression_type(source, false));
                co_return StaticValue{{}, type, type};
            }
        } else if (source.kind == Expr::Kind::String) {
            if (known_symbolic) *known_symbolic = true;
            const auto type = pointer_type(expression_type(source, false));
            co_return StaticValue{{}, type, type};
        } else if (source.kind == Expr::Kind::Unary && source.text == "*" && source.left) {
            auto pointer = co_await static_value_async(*source.left, known_symbolic);
            if (pointer && pointer->type && pointer->type->kind == Type::Kind::Pointer &&
                (!pointer->value || !pointer->value->meta_pointer)) co_return pointer;
            co_return std::nullopt;
        } else if (source.kind == Expr::Kind::Binary && source.left && source.right) {
            if (source.text == "member" || source.text == "index" || source.text == "pointer_member") {
                const auto type = expression_type(*source.left, false);
                std::optional<StaticValue> base;
                if (source.text == "member" || (type &&
                    (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector)))
                    base = co_await static_designator_async(*source.left, known_symbolic);
                else base = co_await static_value_async(*source.left, known_symbolic);
                if (!base || !base->type || base->type->kind != Type::Kind::Pointer ||
                    (base->value && base->value->meta_pointer)) co_return std::nullopt;
                if (source.text == "member" || source.text == "pointer_member") {
                    const auto owner = source.text == "pointer_member" && type
                        ? (type->kind == Type::Kind::Array ? qualified_element_type(type) : type->pointee) : type;
                    std::optional<EvaluationMemberLayout> member;
                    if (program_.evaluation_member_layout)
                        member = co_await program_.evaluation_member_layout.async(owner, member_name(*source.right));
                    if (!member || member->bit_width) {
                        fail(source.location, "static address member requires a complete addressable target subobject");
                        co_return std::nullopt;
                    }
                    if (!(co_await static_address_offset_async(*base, {UInt128{member->offset}, false},
                                               1, true, source.location))) co_return std::nullopt;
                } else {
                    const auto index = co_await required_integer_async(*source.right);
                    if (!index) co_return std::nullopt;
                    const auto element = type &&
                        (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) ? type->element
                        : type && type->kind == Type::Kind::Pointer ? type->pointee : TypePtr{};
                    std::optional<std::uint64_t> size;
                    if (element && size_of_) size = co_await size_of_->async(element);
                    if (!size || !*size) {
                        fail(source.location, "static address index requires a complete target object type");
                        co_return std::nullopt;
                    }
                    if (!(co_await static_address_offset_async(*base,
                        relocation_addend(index->integer, integer_type(index->type)),
                        *size, true, source.location))) co_return std::nullopt;
                }
                const auto selected = pointer_type(expression_type(source, false));
                selected->address_space = base->type->address_space;
                base->type = selected;
                if (base->value) base->value->type = selected;
                co_return base;
            }
        }
        fail(source.location, "static initializer address cannot depend on an automatic local or parameter");
        co_return std::nullopt;
    }

    EvaluationTask<std::optional<StaticValue>> static_conversion_async(StaticValue value, TypePtr destination,
                                                SourceLocation location, bool explicit_cast = false) {
        if (!destination) co_return std::nullopt;
        if (!value.address_type) {
            if (!value.value) co_return std::nullopt;
            auto converted = (co_await convert_async(*value.value, destination, location, explicit_cast));
            if (!converted) co_return std::nullopt;
            co_return StaticValue{std::move(converted), destination, {}};
        }
        const bool same_label = is_label_type(value.type) && is_label_type(destination);
        const bool pointer = value.type && value.type->kind == Type::Kind::Pointer &&
            destination->kind == Type::Kind::Pointer;
        std::optional<std::uint64_t> address_size, destination_size;
        if (size_of_) {
            address_size = co_await size_of_->async(value.address_type);
            destination_size = co_await size_of_->async(destination);
        }
        const bool integer = is_integer(destination) && (explicit_cast || is_integer(value.type)) &&
            address_size && destination_size && *address_size == *destination_size;
        if (!same_label && !pointer && !integer) {
            fail(location, "static address initializer requires a compatible pointer, label, or address-width integer type");
            co_return std::nullopt;
        }
        value.type = destination;
        co_return value;
    }

    EvaluationTask<std::optional<StaticValue>> static_value_async(const Expr& source, bool* known_symbolic = nullptr) {
        if (!step(source.location)) co_return std::nullopt;
        if (source.kind == Expr::Kind::Parenthesized && source.left)
            co_return co_await static_value_async(*source.left, known_symbolic);
        if (source.kind == Expr::Kind::Conditional && source.left && source.right && source.third) {
            const auto condition = co_await expression_async(*source.left);
            if (!condition || !known_truth(*condition, source.location)) co_return std::nullopt;
            auto value = co_await static_value_async(condition->truthy() ? *source.right : *source.third, known_symbolic);
            if (!value) co_return std::nullopt;
            co_return co_await static_conversion_async(std::move(*value), expression_type(source), source.location);
        }
        if (source.kind == Expr::Kind::Cast && source.left) {
            auto value = co_await static_value_async(*source.left, known_symbolic);
            if (!value) co_return std::nullopt;
            co_return co_await static_conversion_async(std::move(*value), source.type, source.location, true);
        }
        if (source.kind == Expr::Kind::Unary && source.text == "&" && source.left) {
            co_return co_await static_designator_async(*source.left, known_symbolic);
        }
        const auto object_type = expression_type(source, false);
        if (source.kind != Expr::Kind::String && object_type &&
            (object_type->kind == Type::Kind::Array || object_type->kind == Type::Kind::Function)) {
            auto value = co_await static_designator_async(source, known_symbolic);
            if (!value) co_return std::nullopt;
            const auto type = pointer_type(object_type->kind == Type::Kind::Array
                ? qualified_element_type(object_type) : object_type);
            type->address_space = value->type->address_space;
            value->type = type;
            if (value->value) value->value->type = type;
            co_return value;
        }
        if (source.kind == Expr::Kind::Binary && source.left && source.right &&
            (source.text == "+" || source.text == "-")) {
            bool left_symbolic{}, right_symbolic{};
            auto left = co_await static_value_async(*source.left, &left_symbolic);
            // A known symbolic base plus an integer still requires a relocation,
            // even if proving the addend needs an unavailable invocation context.
            // Retain only this category fact, never an unproven value or offset.
            if (known_symbolic && left_symbolic && is_integer(expression_type(*source.right)))
                *known_symbolic = true;
            if (!left) co_return std::nullopt;
            auto right = co_await static_value_async(*source.right, &right_symbolic);
            if (known_symbolic && source.text == "+" && right_symbolic && is_integer(left->type))
                *known_symbolic = true;
            if (!right) co_return std::nullopt;
            const auto address = [](const StaticValue& value) {
                return value.address_type || (value.value && value.value->address);
            };
            if (address(*left) || address(*right)) {
                auto base = address(*left) ? *left : *right;
                const auto& offset = address(*left) ? *right : *left;
                if (offset.address_type || !offset.value || !is_integer(offset.type) ||
                    (address(*right) && source.text == "-")) {
                    fail(source.location, "static address initializer requires one address and an integer constant addend");
                    co_return std::nullopt;
                }
                std::optional<std::uint64_t> size{1};
                if (base.type && base.type->kind == Type::Kind::Pointer) {
                    size.reset();
                    if (size_of_) size = co_await size_of_->async(base.type->pointee);
                }
                if (!size || !*size) {
                    fail(source.location, "static address arithmetic requires a complete target object type");
                    co_return std::nullopt;
                }
                if (!(co_await static_address_offset_async(base,
                        relocation_addend(offset.value->integer, integer_type(offset.type)),
                        *size, source.text == "+", source.location))) co_return std::nullopt;
                if (const auto type = expression_type(source)) {
                    base.type = type;
                    if (base.value) base.value->type = type;
                }
                co_return base;
            }
            if (!left->value || !right->value) co_return std::nullopt;
            std::optional<EvalValue> value;
            if (is_vector(left->type) || is_vector(right->type))
                value = (co_await vector_binary_values_async(source.text, *left->value, *right->value, source.location));
            else if (left->value->meta_pointer && right->value->meta_pointer && source.text == "-")
                value = (co_await compare_meta_pointers_async(*left->value, *right->value, source.text, source.location));
            else if (left->value->meta_pointer)
                value = (co_await meta_pointer_offset_async(*left->value, *right->value, source.text == "-", source.location));
            else if (right->value->meta_pointer && source.text == "+")
                value = (co_await meta_pointer_offset_async(*right->value, *left->value, false, source.location));
            else value = (co_await scalar_binary_values_async(source.text, *left->value, *right->value, source.location));
            if (!value) co_return std::nullopt;
            co_return StaticValue{value, value->type, {}};
        }
        auto value = co_await expression_async(source);
        if (!value) co_return std::nullopt;
        if ((value->address && value->address->kind != AddressConstant::Kind::Absolute) || value->label_address) {
            if (known_symbolic) *known_symbolic = true;
            co_return StaticValue{{}, value->type, value->type,
                value->address ? relocation_addend(value->address->addend) : RelocationAddend{}};
        }
        co_return StaticValue{value, value->type, {}};
    }

    EvaluationTask<bool> static_initializer_async(const Expr& source, const TypePtr& destination,
                                                   bool* symbolic = nullptr,
                                                   std::optional<Expr::IntegerConstant>* integer = nullptr) {
        // A link-time value is not an instruction to evaluate address bytes.
        // Check source legality here; numeric leaves still use the sandbox below.
        if (!(co_await validate_unevaluated_constraints_async(source))) co_return false;
        auto value = co_await static_value_async(source, symbolic);
        if (!value) co_return false;
        if (value->value && (value->value->meta_pointer || (value->value->object &&
                std::any_of(value->value->object->pointers.begin(), value->value->object->pointers.end(),
                    [](const auto& slot) { return slot.value && slot.value->meta_pointer; })))) {
            fail(source.location, "translation-time object pointer cannot escape into static initialization");
            co_return false;
        }
        if (value->value && value->value->bytes && destination && destination->kind == Type::Kind::Array) {
            const auto size = value->value->byte_length;
            if (!size || size > std::numeric_limits<std::uint32_t>::max() ||
                !fits_unsigned(UInt128{size}, program_.address_bits) ||
                (destination->lanes && destination->lanes != size)) {
                fail(source.location, "materialized byte array has an invalid target-sized bound");
                co_return false;
            }
            if (!destination->lanes) destination->lanes = static_cast<std::uint32_t>(size);
            co_return true;
        }
        const auto converted = co_await static_conversion_async(std::move(*value), destination, source.location);
        if (converted && converted->address_type && program_.evaluation_relocation_addend &&
            !program_.evaluation_relocation_addend(converted->addend)) {
            fail(source.location, "static address addend exceeds the supported relocation range");
            co_return false;
        }
        if (symbolic && converted) *symbolic = static_cast<bool>(converted->address_type);
        if (integer && converted && converted->value && is_integer(converted->type))
            *integer = Expr::IntegerConstant{converted->value->integer, converted->type->builtin};
        co_return converted.has_value();
    }

    std::unique_ptr<Expr> required_pointer(const Expr& source, const TypePtr& destination) {
        return required_pointer_async(source, destination).run();
    }

    EvaluationTask<std::unique_ptr<Expr>> required_pointer_async(const Expr& source, const TypePtr& destination) {
        if (!(co_await validate_required_tree_async(source))) co_return nullptr;
        auto value = co_await expression_async(source);
        if (value) value = (co_await convert_async(*value, destination, source.location));
        if (!value || !value->address) {
            fail(source.location, "translation-time object pointer cannot escape into a generic argument");
            co_return nullptr;
        }
        co_return value_expression(*value, source.location);
    }

    std::unique_ptr<Expr> required_label(const Expr& source) {
        return required_label_async(source).run();
    }

    EvaluationTask<std::unique_ptr<Expr>> required_label_async(const Expr& source) {
        const Expr* root = &source;
        while (root->kind == Expr::Kind::Parenthesized && root->left) root = root->left.get();
        if (root->kind == Expr::Kind::Name && resolved_label_binding(NameUse(*root)).scope &&
            !expression_type(*root)) {
            fail(root->location, "label address does not name a visible label in its retained source binding");
            co_return nullptr;
        }
        if (!(co_await validate_required_tree_async(source))) co_return nullptr;
        auto value = co_await expression_async(source);
        if (!value) co_return nullptr;
        if (!is_label_type(value->type)) {
            fail(source.location, "generic label argument requires a visible label address constant");
            co_return nullptr;
        }
        co_return value_expression(*value, source.location);
    }

    EvaluationTask<std::optional<EvalValue>> required_integer_async(const Expr& source,
                                             const TypePtr& destination = {}) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        if (const auto type = expression_type(source); type && !is_integer(type)) {
            fail(source.location, "required expression is not an integer translation-time value");
            co_return std::nullopt;
        }
        auto value = (co_await expression_async(source));
        if (!value) co_return std::nullopt;
        if (value->pointer() || !is_integer(value->type)) {
            fail(source.location, "required expression is not an integer translation-time value");
            co_return std::nullopt;
        }
        if (!destination) co_return value;
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
            co_return std::nullopt;
        }
        co_return (co_await convert_async(*value, destination, source.location));
    }

    // The value of a pointer derived from an integer, an absolute address.
    EvaluationTask<std::optional<EvalValue>> required_absolute_pointer_async(const Expr& source) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        auto value = co_await expression_async(source);
        if (!value || !value->address || value->address->kind != AddressConstant::Kind::Absolute ||
            !value->type || value->type->kind != Type::Kind::Pointer) co_return std::nullopt;
        co_return value;
    }

    std::optional<EvalValue> required_floating(const Expr& source,
                                               const TypePtr& destination) {
        return required_floating_async(source, destination).run();
    }

    EvaluationTask<std::optional<EvalValue>> required_floating_async(const Expr& source,
                                               const TypePtr& destination) {
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        auto value = co_await expression_async(source);
        if (value && (value->bytes || value->buffer)) {
            fail(source.location, "meta byte values cannot be used as a runtime scalar");
            co_return std::nullopt;
        }
        if (!value || value->pointer() ||
            (!is_integer(value->type) && !is_floating(value->type))) {
            fail(source.location,
                 "required expression is not a scalar translation-time value");
            co_return std::nullopt;
        }
        value = (co_await convert_async(*value, destination, source.location));
        if (!value) fail(source.location,
                         "floating initializer cannot be converted to its type");
        co_return value;
    }

    EvaluationTask<std::optional<EvalValue>> required_scalar_async(const Expr& source) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        auto value = (co_await expression_async(source));
        if (value && (value->bytes || value->buffer)) {
            fail(source.location, "meta byte values cannot be used as a runtime scalar");
            co_return std::nullopt;
        }
        if (!value || value->pointer() ||
            (!is_integer(value->type) && !is_floating(value->type))) {
            fail(source.location,
                 "required expression is not a scalar translation-time value");
            co_return std::nullopt;
        }
        co_return value;
    }

    EvaluationTask<std::optional<std::string>> required_bytes_async(const Expr& source,
        std::span<const std::pair<NameKey, TypePtr>> local_types = {}) {
        push_source_types(local_types);
        struct Pop { Evaluator& evaluator; ~Pop() { evaluator.pop_scope(); } } pop{*this};
        if (!(co_await validate_required_tree_async(source))) co_return std::nullopt;
        auto value = co_await expression_async(source);
        if (!value) co_return std::nullopt;
        if (!value->bytes || !value->type ||
            value->type->kind != Type::Kind::Bytes) {
            fail(source.location, "static byte array requires a $::meta::bytes initializer; got '" +
                 type_name(value->type) + "'");
            co_return std::nullopt;
        }
        if (!charge_meta_bytes(value->byte_length, source.location))
            co_return std::nullopt;
        co_return value->bytes->substr(value->byte_offset, value->byte_length);
    }


    bool validate_source_body(const FunctionDecl& function) {
        return validate_source_body_async(function).run();
    }

    const FunctionDecl* record_owner(const NominalTypeKey& key) {
        if (!key.identity || !key.identity->function_scope) return nullptr;
        const auto& scope = key.identity->function_scope;
        if (current_function_ && current_function_->function_scope == scope) return current_function_;
        for (const auto& [source, view] : invocation_functions_)
            if (view->function_scope == scope) return view.get();
        const auto* source = lexical_function(program_, scope);
        if (!source || !evaluation_only(*source)) return nullptr;
        auto& view = invocation_functions_[source];
        if (!view) view = copy_evaluation_declaration(*source);
        return view.get();
    }

    EvaluationTask<Preparation> prepare_record_attributes_async(std::vector<Attribute>& attributes,
        std::string_view subject, std::span<const std::pair<NameKey, TypePtr>> local_types) {
        bool deferred{};
        for (auto& attribute : attributes) {
            if (attribute.name == "packed") {
                if (attribute.arguments.empty()) continue;
                fail(attribute.location, "packed on " + std::string(subject) + " does not take arguments");
                co_return Preparation::Invalid;
            }
            if (attribute.name != "aligned") {
                fail(attribute.location, "attribute '" + attribute.name + "' is not valid on " + std::string(subject));
                co_return Preparation::Invalid;
            }
            if (attribute.arguments.size() != 1 || !attribute.expression_argument) {
                fail(attribute.location, "aligned on " + std::string(subject) + " requires one integer argument");
                co_return Preparation::Invalid;
            }
            bool needs_context{};
            const auto value = co_await probe_integer_async(*attribute.expression_argument, local_types,
                validation_phase_ == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
            if (needs_context) { deferred = true; continue; }
            if (!value) co_return context_unavailable_ && !resource_exhausted_
                ? Preparation::Deferred : Preparation::Invalid;
            if (!alignment_value(*value)) {
                fail(attribute.location, "aligned argument must be a positive power-of-two integer constant");
                co_return Preparation::Invalid;
            }
            auto expression = clone_expr(*attribute.expression_argument);
            expression->evaluated_integer = value;
            attribute.expression_argument = std::move(expression);
        }
        co_return deferred ? Preparation::Deferred : Preparation::Ready;
    }

    // A size query can precede the processing of a static array whose bound
    // comes from its initializer. Complete that bound from the initializer,
    // evaluated in the object's own context; failures are left to the
    // initializer's ordinary checks.
    EvaluationTask<void> complete_initializer_extent_async(const TypePtr& type) {
        if (!type || type->kind != Type::Kind::Array || type->lanes || type->array_bound) co_return;
        const auto found = std::find_if(program_.objects.begin(), program_.objects.end(),
            [&](const auto& object) { return object->type == type && object->initializer; });
        if (found == program_.objects.end()) co_return;
        auto* object = found->get();
        const auto* owner = object_lexical_function(program_, *object);
        const auto source_namespace = owner ? owner->source_namespace : namespace_prefix(object->name);
        const LayoutQuery size_of = size_of_ ? *size_of_ : LayoutQuery{};
        const LayoutQuery align_of = align_of_ ? *align_of_ : LayoutQuery{};
        std::uint64_t count{};
        const auto& source = *object->initializer;
        if (source.kind == Expr::Kind::AggregateInitializer) {
            std::uint64_t cursor{};
            for (const auto& entry : source.initializer_entries) {
                auto selected = cursor;
                if (!entry.designators.empty() && entry.designators.front().index &&
                    entry.designators.front().kind == Expr::InitializerDesignator::Kind::Index) {
                    const auto index = (co_await evaluate_target_integer_requirement_async(program_,
                        *entry.designators.front().index, diagnostics_, size_of, align_of, source_namespace,
                        owner, {}, EvaluationIntegerContext::ProbeDefinition)).value;
                    if (!index || index->value.high || integer_negative(index->value,
                            evaluation_integer_type(builtin_type(index->type), program_.address_bits)))
                        co_return;
                    selected = index->value.low;
                }
                if (selected >= std::numeric_limits<std::uint32_t>::max()) co_return;
                cursor = selected + 1;
                count = std::max(count, cursor);
            }
        } else if (source.kind != Expr::Kind::String && type->element &&
                   type->element->kind == Type::Kind::Builtin && type->element->builtin == BuiltinType::U8) {
            Expr length;
            length.kind = Expr::Kind::Call;
            length.location = source.location;
            length.left = std::make_unique<Expr>();
            length.left->kind = Expr::Kind::Name;
            length.left->location = source.location;
            length.left->text = "$::meta::len";
            length.arguments.push_back(clone_expr(source));
            const auto bytes = (co_await evaluate_target_integer_requirement_async(program_, length,
                diagnostics_, size_of, align_of, source_namespace, owner, {},
                EvaluationIntegerContext::ProbeDefinition)).value;
            if (!bytes || bytes->value.high) co_return;
            count = bytes->value.low;
        }
        if (count && count <= std::numeric_limits<std::uint32_t>::max() &&
            fits_unsigned(UInt128{count}, program_.address_bits))
            type->lanes = static_cast<std::uint32_t>(count);
    }

    EvaluationTask<Preparation> prepare_layout_async(const TypePtr& type, EvaluationLayoutKind kind) {
        if (!type) co_return Preparation::Ready;
        if (!type->alignment_requests.empty()) {
            GenericExpansionState bindings;
            if (current_function_) collect_function_types(*current_function_, bindings);
            const auto prepared = co_await prepare_alignment_requests_async(type, bindings.local_types);
            if (prepared != Preparation::Ready) co_return prepared;
        }
        if (type->kind == Type::Kind::Vector) {
            GenericExpansionState bindings;
            if (current_function_) collect_function_types(*current_function_, bindings);
            co_return co_await prepare_vector_extent_async(type, bindings.local_types);
        }
        if (type->kind == Type::Kind::Array) {
            const auto element = co_await prepare_layout_async(type->element, kind);
            if (element == Preparation::Invalid) co_return element;
            if (kind == EvaluationLayoutKind::Alignment || type->lanes || !type->array_bound) co_return element;
            if (!preparing_type_bounds_.insert(type.get()).second) {
                fail(type->array_bound->location, "fixed array bound depends on its own layout");
                co_return Preparation::Invalid;
            }
            GenericExpansionState bindings;
            if (current_function_) collect_function_types(*current_function_, bindings);
            bool needs_context{};
            const auto value = co_await probe_integer_async(*type->array_bound, bindings.local_types,
                validation_phase_ == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
            preparing_type_bounds_.erase(type.get());
            if (needs_context) {
                type->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
                co_return Preparation::Deferred;
            }
            if (!value) co_return context_unavailable_ && !resource_exhausted_
                ? Preparation::Deferred : Preparation::Invalid;
            const auto bound = fixed_array_bound_value(*value, program_.address_bits);
            if (!bound) {
                fail(type->array_bound->location, "fixed array bound must be a positive integer representable in 32 bits");
                co_return Preparation::Invalid;
            }
            type->lanes = *bound;
            type->array_extent_dependency = Type::ArrayExtentDependency::None;
            co_return element;
        }
        if (type->kind != Type::Kind::Record) co_return Preparation::Ready;
        const auto key = type->nominal_key();
        const auto* owner = record_owner(key);
        if (!owner || !evaluation_only(*owner) || !owner->generic_parameters.empty()) co_return Preparation::Ready;
        auto found = record_views_->find(key);
        if (found == record_views_->end()) {
            // Instantiate required generic calls on the definition graph before
            // copying it. That preparation has definition-only capabilities;
            // invocation values are resolved exclusively in the private view.
            const auto prepare_type = program_.evaluation_prepare_type;
            if (prepare_type &&
                !(co_await prepare_type.async(type, EvaluationLayoutKind::Complete)))
                co_return Preparation::Invalid;
            const auto* definition = program_.record_index.definition(program_, key);
            if (!definition) co_return Preparation::Ready;
            if (const auto error = record_source_error(*definition, program_, program_.record_index,
                    &record_source_proofs_)) {
                fail(error->location, error->message);
                co_return Preparation::Invalid;
            }
            found = record_views_->emplace(key, PreparedRecord{
                std::make_shared<RecordDecl>(copy_evaluation_declaration(*definition))}).first;
        }
        auto& record = found->second;
        auto& state = kind == EvaluationLayoutKind::Alignment ? record.alignment : record.complete;
        if (state == Preparation::Preparing) {
            fail(record.definition->location, "record layout depends on its own preparation");
            co_return Preparation::Invalid;
        }
        if (state != Preparation::Unprepared) co_return state;
        state = Preparation::Preparing;
        const auto* previous_function = current_function_;
        auto previous_namespace = std::move(current_namespace_);
        current_function_ = owner;
        current_namespace_ = owner->source_namespace;
        struct Restore {
            Evaluator& evaluator;
            const FunctionDecl* function;
            std::string name_space;
            ~Restore() { evaluator.current_function_ = function; evaluator.current_namespace_ = std::move(name_space); }
        } restore{*this, previous_function, std::move(previous_namespace)};
        GenericExpansionState bindings;
        collect_function_types(*owner, bindings);
        bool deferred{};
        const auto accept = [&](Preparation result) {
            if (result == Preparation::Deferred) deferred = true;
            if (result == Preparation::Invalid) { state = result; return false; }
            return true;
        };
        if (kind == EvaluationLayoutKind::Complete) {
            if (!accept(co_await prepare_layout_async(type, EvaluationLayoutKind::Alignment))) co_return state;
        } else {
            if (!accept(co_await prepare_record_attributes_async(record.definition->attributes,
                    "a record definition", bindings.local_types))) co_return state;
            for (auto& member : record.definition->members)
                if (!accept(co_await prepare_record_attributes_async(member.attributes,
                        "a record member", bindings.local_types))) co_return state;
        }
        for (auto& member : record.definition->members) {
            const auto prepared = co_await prepare_layout_async(member.type, kind);
            if (!accept(prepared)) co_return state;
            if (kind == EvaluationLayoutKind::Complete && prepared == Preparation::Ready && size_of_) {
                if (!(co_await size_of_->async(member.type))) {
                    fail(member.location, "record member requires a complete target layout");
                    state = Preparation::Invalid;
                    co_return state;
                }
            }
            if (kind == EvaluationLayoutKind::Alignment || !member.bit_width) continue;
            bool needs_context{};
            const auto value = co_await probe_integer_async(*member.bit_width, bindings.local_types,
                validation_phase_ == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
            if (needs_context) { deferred = true; continue; }
            if (!value) {
                state = context_unavailable_ && !resource_exhausted_ ? Preparation::Deferred : Preparation::Invalid;
                co_return state;
            }
            std::optional<std::uint64_t> size;
            if (size_of_) size = co_await size_of_->async(member.type);
            if (!size) { state = Preparation::Invalid; co_return state; }
            const auto* error = bit_field_width_error(*value, program_.address_bits, *size * 8, !member.name.empty());
            if (error) { fail(member.location, error); state = Preparation::Invalid; co_return state; }
            member.bit_width->evaluated_integer = value;
        }
        state = deferred ? Preparation::Deferred : Preparation::Ready;
        co_return state;
    }

    EvaluationTask<Preparation> prepare_vector_extent_async(const TypePtr& type,
        std::span<const std::pair<NameKey, TypePtr>> local_types) {
        if (const auto* error = vector_element_error(type->element)) {
            fail(type->vector_bound ? type->vector_bound->location :
                current_function_ ? current_function_->location : SourceLocation{}, error);
            co_return Preparation::Invalid;
        }
        if (type->lanes || !type->vector_bound || type->element->kind == Type::Kind::Generic)
            co_return Preparation::Ready;
        if (!preparing_type_bounds_.insert(type.get()).second) {
            fail(type->vector_bound->location, "vector bound depends on its own layout");
            co_return Preparation::Invalid;
        }
        bool needs_context{};
        const auto value = co_await probe_integer_async(*type->vector_bound, local_types,
            validation_phase_ == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
        preparing_type_bounds_.erase(type.get());
        if (needs_context) {
            type->vector_extent_dependency = Type::VectorExtentDependency::ExpansionContext;
            co_return Preparation::Deferred;
        }
        if (!value) co_return context_unavailable_ && !resource_exhausted_
            ? Preparation::Deferred : Preparation::Invalid;
        const auto extent = vector_bound_value(type, *value, program_.address_bits);
        if (!extent.lanes) {
            fail(type->vector_bound->location, extent.error);
            co_return Preparation::Invalid;
        }
        type->lanes = *extent.lanes;
        type->vector_extent_dependency = Type::VectorExtentDependency::None;
        co_return Preparation::Ready;
    }

    EvaluationTask<Preparation> prepare_alignment_requests_async(const TypePtr& type,
        std::span<const std::pair<NameKey, TypePtr>> local_types) {
        if (!preparing_type_bounds_.insert(type.get()).second) {
            fail(type->alignment_requests.front()->location, "type alignment depends on its own layout");
            co_return Preparation::Invalid;
        }
        const auto requests = std::move(type->alignment_requests);
        type->alignment_requests.clear();
        auto result = Preparation::Ready;
        for (const auto& request : requests) {
            if (result == Preparation::Invalid) {
                type->alignment_requests.push_back(request);
                continue;
            }
            bool needs_context{};
            const auto value = co_await probe_integer_async(*request, local_types,
                validation_phase_ == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
            if (needs_context || (!value && context_unavailable_ && !resource_exhausted_)) {
                type->alignment_requests.push_back(request);
                result = Preparation::Deferred;
                continue;
            }
            const auto alignment = value ? alignment_value(*value) : std::nullopt;
            if (!alignment) {
                if (value) fail(request->location, "aligned argument must be a positive power-of-two integer constant");
                type->alignment_requests.push_back(request);
                result = Preparation::Invalid;
                continue;
            }
            type->alignment = std::max(type->alignment, *alignment);
        }
        preparing_type_bounds_.erase(type.get());
        co_return result;
    }

    EvaluationTask<bool> prepare_bound_types_async(const FunctionDecl& function, SourceValidationPhase phase) {
        if (!evaluation_only(function) || !function.generic_parameters.empty()) co_return true;
        GenericExpansionState bindings;
        collect_function_types(function, bindings);
        std::unordered_map<const Type*, VariableDecl*> inferred_arrays;
        std::vector<const Statement*> declarations;
        if (function.body) declarations.push_back(function.body.get());
        while (!declarations.empty()) {
            const auto* statement = declarations.back();
            declarations.pop_back();
            if (statement->declaration) {
                auto& declaration = *statement->declaration;
                const auto& type = declaration.type;
                // Minimal evaluator clients can provide only a concrete
                // initializer plan. Without the structural service, leave
                // their ordinary source-validation/planning path intact.
                if (program_.evaluation_initializer_types && type && type->kind == Type::Kind::Array && !type->lanes && !type->array_bound &&
                    declaration.initializer && !declaration.dynamic_array_bound)
                    inferred_arrays.emplace(type.get(), &declaration);
            }
            for (const auto& child : statement->statements) declarations.push_back(child.get());
            if (statement->first) declarations.push_back(statement->first.get());
            if (statement->second) declarations.push_back(statement->second.get());
        }
        struct Work {
            TypePtr type;
            const Expr* expression{};
            const Statement* statement{};
            bool finish{};
        };
        std::vector<Work> work;
        const auto type = [&](TypePtr value) { if (value) work.push_back({std::move(value)}); };
        const auto expression = [&](const Expr* value) { if (value) work.push_back({{}, value}); };
        const auto attributes = [&](const std::vector<Attribute>& list) {
            for (const auto& attribute : list) {
                expression(attribute.expression_argument.get());
                for (const auto& binding : attribute.variadic_bindings) type(binding.type);
            }
        };
        type(function.return_type);
        for (const auto& parameter : function.parameters) {
            type(parameter.type);
            type(parameter.declared_array_type);
        }
        for (const auto& requirement : function.required_types) {
            type(requirement.type);
            type(requirement.compatible_with);
        }
        attributes(function.attributes);
        for (const auto& record : program_.records)
            if (record.complete && record.nominal_identity &&
                record.nominal_identity->function_scope == function.function_scope)
                type(record_type(record));
        if (function.body) work.push_back({{}, {}, function.body.get()});
        std::unordered_set<const Type*> seen_types;
        std::unordered_set<const Expr*> seen_expressions;
        while (!work.empty()) {
            auto next = std::move(work.back());
            work.pop_back();
            if (next.statement) {
                const auto& statement = *next.statement;
                attributes(statement.attributes);
                if (statement.declaration) {
                    type(statement.declaration->type);
                    expression(statement.declaration->initializer.get());
                    expression(statement.declaration->dynamic_array_bound.get());
                    attributes(statement.declaration->attributes);
                }
                expression(statement.expression.get());
                expression(statement.condition.get());
                for (const auto& increment : statement.increments) expression(increment.get());
                for (const auto& child : statement.statements) work.push_back({{}, {}, child.get()});
                if (statement.first) work.push_back({{}, {}, statement.first.get()});
                if (statement.second) work.push_back({{}, {}, statement.second.get()});
            } else if (next.expression) {
                const auto& source = *next.expression;
                if (!seen_expressions.insert(&source).second) continue;
                type(source.type);
                if (source.kind == Expr::Kind::Name)
                    for (const auto& [name, local_type] : bindings.local_types)
                        if (name == name_key(source)) { type(local_type); break; }
                expression(source.left.get());
                expression(source.right.get());
                expression(source.third.get());
                for (const auto& argument : source.arguments) expression(argument.get());
                for (const auto& argument : source.generic_arguments) {
                    type(argument.type);
                    expression(argument.value.get());
                }
                for (const auto& entry : source.initializer_entries) {
                    expression(entry.value.get());
                    for (const auto& designator : entry.designators) expression(designator.index.get());
                }
            } else if (next.type) {
                const auto& source = next.type;
                if (!next.finish) {
                    if (!seen_types.insert(source.get()).second) continue;
                    if (source->pending_address_space) {
                        fail(source->pending_address_space->second, "address_space requires a pointer declarator");
                        co_return false;
                    }
                    if (source->kind == Type::Kind::Pointer && program_.evaluation_address_space_type_error)
                        if (const auto issue = program_.evaluation_address_space_type_error(source->address_space)) {
                            fail(source->address_space_location.valid() ? source->address_space_location : function.location,
                                 *issue);
                            co_return false;
                        }
                    if (const auto* reason = source_array_element_error(source)) {
                        fail(source->array_bound ? source->array_bound->location : function.location, reason);
                        co_return false;
                    }
                    work.push_back({source, {}, {}, true});
                    type(source->element);
                    type(source->pointee);
                    if (const auto inferred = inferred_arrays.find(source.get()); inferred != inferred_arrays.end())
                        expression(inferred->second->initializer.get());
                    expression(source->array_bound.get());
                    expression(source->vector_bound.get());
                    for (const auto& request : source->alignment_requests) expression(request.get());
                    if (source->function) {
                        // This dependency walk uses exact NameKeys, so retaining
                        // all visited prototype cells cannot expose them by
                        // spelling in another scope. No value cell is created.
                        for (const auto& parameter : source->function->parameters)
                            bindings.local_types.emplace_back(name_key(parameter), parameter.type);
                        type(source->function->result);
                        for (const auto& parameter : source->function->parameters) {
                            type(parameter.type);
                            type(parameter.declared_array_type);
                        }
                    }
                    if (source->kind == Type::Kind::Record && source->nominal_identity &&
                        source->nominal_identity->function_scope == function.function_scope) {
                        const auto key = source->nominal_key();
                        auto found = record_views_->find(key);
                        if (found == record_views_->end()) {
                            const auto prepare_type = program_.evaluation_prepare_type;
                            if (prepare_type &&
                                !(co_await prepare_type.async(source, EvaluationLayoutKind::Complete))) co_return false;
                            if (const auto definition = program_.record_definition(key)) {
                                if (const auto issue = record_source_error(*definition, program_,
                                        program_.record_index, &record_source_proofs_)) {
                                    fail(issue->location, issue->message);
                                    co_return false;
                                }
                                found = record_views_->emplace(key, PreparedRecord{
                                    std::make_shared<RecordDecl>(copy_evaluation_declaration(*definition))}).first;
                            }
                        }
                        if (found != record_views_->end()) {
                            // Extend the same dependency graph into private
                            // nominal members. Their sizeof(local) operands
                            // must prepare that exact local type before the
                            // containing record's value requirements run.
                            attributes(found->second.definition->attributes);
                            for (const auto& member : found->second.definition->members) {
                                type(member.type);
                                expression(member.bit_width.get());
                                attributes(member.attributes);
                            }
                        }
                    }
                    continue;
                }
                if (!source->alignment_requests.empty()) {
                    const auto location = source->alignment_requests.front()->location;
                    const auto result = co_await prepare_alignment_requests_async(source, bindings.local_types);
                    if (result == Preparation::Invalid) co_return false;
                    if (result == Preparation::Deferred && phase == SourceValidationPhase::Invocation) {
                        fail_context(location);
                        co_return false;
                    }
                }
                if (source->kind == Type::Kind::Record) {
                    const auto result = co_await prepare_layout_async(source, EvaluationLayoutKind::Complete);
                    if (result == Preparation::Invalid) co_return false;
                    if (result == Preparation::Deferred && phase == SourceValidationPhase::Invocation) {
                        fail_context(function.location);
                        co_return false;
                    }
                    if (result == Preparation::Ready && program_.record_definition(source->nominal_key()) && size_of_) {
                        if (!(co_await size_of_->async(source))) {
                            fail(function.location, "record requires a complete target layout");
                            co_return false;
                        }
                    }
                    if (const auto found = record_views_->find(source->nominal_key()); found != record_views_->end())
                        for (const auto& member : found->second.definition->members) type(member.type);
                    continue;
                }
                if (source->kind == Type::Kind::Vector) {
                    const auto result = co_await prepare_vector_extent_async(source, bindings.local_types);
                    if (result == Preparation::Invalid) co_return false;
                    if (result == Preparation::Deferred && phase == SourceValidationPhase::Invocation) {
                        fail_context(source->vector_bound->location);
                        co_return false;
                    }
                    continue;
                }
                if (const auto inferred = inferred_arrays.find(source.get()); inferred != inferred_arrays.end()) {
                    const bool ready = co_await prepare_inferred_array_async(*inferred->second, program_, bindings.local_types,
                        [&](const Expr& operand, std::span<const std::pair<NameKey, TypePtr>> types,
                            const TypePtr& destination) -> EvaluationTask<SourceConstantProbe> {
                            SourceConstantProbe result;
                            result.value = co_await probe_integer_async(operand, types,
                                phase == SourceValidationPhase::Definition ? &result.needs_invocation_context : nullptr,
                                true, destination);
                            co_return result;
                        }, [&](SourceLocation at, std::string message) { fail(at, std::move(message)); },
                        phase == SourceValidationPhase::Invocation);
                    if (!ready) co_return false;
                }
                if (source->kind != Type::Kind::Array || source->lanes || !source->array_bound) continue;
                bool needs_context{};
                const auto value = co_await probe_integer_async(*source->array_bound, bindings.local_types,
                    phase == SourceValidationPhase::Definition ? &needs_context : nullptr, true);
                if (needs_context) {
                    source->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
                    continue;
                }
                if (!value) co_return false;
                const auto extent = fixed_array_bound_value(*value, program_.address_bits);
                if (!extent) {
                    fail(source->array_bound->location,
                        "fixed array bound must be a positive integer representable in 32 bits");
                    co_return false;
                }
                source->lanes = *extent;
                source->array_extent_dependency = Type::ArrayExtentDependency::None;
            }
        }
        for (const auto& requirement : function.required_types) {
            if (!requirement.compatible_with) continue;
            const auto comparison = compare_source_types(requirement.type, requirement.compatible_with);
            if (comparison == TypeComparison::Different ||
                (comparison == TypeComparison::DeferredBound && phase == SourceValidationPhase::Invocation)) {
                fail(requirement.location, type_requirement_error(requirement));
                co_return false;
            }
        }
        co_return true;
    }

    EvaluationTask<bool> validate_generic_definitions_async() {
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            const auto* function = program_.functions[index].get();
            if (!function->invocation_specialization || !function->body ||
                !generic_definitions_validated_.insert(function).second) continue;
            const auto result = co_await isolated_source_probe_async<bool>(nullptr, true,
                [&](Evaluator& probe) -> EvaluationTask<std::optional<bool>> {
                    const bool meta = evaluation_only(*function);
                    if (meta) {
                        probe.procedural_ = false;
                        probe.macro_context_.reset();
                        probe.call_context_.reset();
                    }
                    if (!(co_await probe.validate_source_body_async(*function, meta
                            ? SourceValidationPhase::Definition : SourceValidationPhase::Invocation)))
                        co_return std::nullopt;
                    co_return true;
                });
            if (!result) co_return false;
        }
        co_return true;
    }

    EvaluationTask<bool> validate_source_body_async(const FunctionDecl& original,
        SourceValidationPhase phase = SourceValidationPhase::Definition) {
        if (!(co_await validate_generic_definitions_async())) co_return false;
        auto definition = phase == SourceValidationPhase::Definition && evaluation_only(original)
            ? copy_evaluation_declaration(original) : nullptr;
        const auto& function = definition ? *definition : original;
        const auto previous_phase = validation_phase_;
        validation_phase_ = phase;
        if (phase == SourceValidationPhase::Definition) reset_record_views();
        const auto* previous_function = current_function_;
        auto previous_namespace = std::move(current_namespace_);
        current_function_ = &function;
        current_namespace_ = function.source_namespace;
        if (!(co_await prepare_bound_types_async(function, phase))) {
            validation_phase_ = previous_phase;
            current_function_ = previous_function;
            current_namespace_ = std::move(previous_namespace);
            co_return false;
        }
        // Required constraints share capabilities/resources through isolated
        // probes. A missing invocation context is not an invented constant.
        SourceTypeValidator validator(program_, function,
            [&](SourceLocation at, std::string message) { fail(at, std::move(message)); },
            [&](const Expr& source, std::span<const std::pair<NameKey, TypePtr>> types,
                const TypePtr& destination)
                -> EvaluationTask<SourceConstantProbe> {
                SourceConstantProbe result;
                result.value = co_await probe_integer_async(source, types,
                    phase == SourceValidationPhase::Definition ? &result.needs_invocation_context : nullptr,
                    true, destination);
                co_return result;
            },
            [&](const Expr& source, std::span<const std::pair<NameKey, TypePtr>> types)
                -> EvaluationTask<SourceConstantProbe> {
                SourceConstantProbe result;
                result.value = co_await probe_integer_async(source, types,
                    phase == SourceValidationPhase::Definition ? &result.needs_invocation_context : nullptr);
                co_return result;
            },
            [&](const Statement& source, Expr::IntegerConstant value) {
                if (phase == SourceValidationPhase::Invocation) validated_cases_[&source] = value;
            },
            [&](const VariableDecl& declaration, PreparedObjectInitializer value) {
                if (phase == SourceValidationPhase::Invocation)
                    validated_initializers_[&declaration] = std::move(value);
            },
            [&](const Expr& source, std::span<const std::pair<NameKey, TypePtr>> types,
                const TypePtr& destination) -> EvaluationTask<SourceInitializerProbe> {
                co_return co_await probe_initializer_async(source, types, destination,
                    phase == SourceValidationPhase::Definition);
            }, phase);
        const bool valid = co_await validator.run_async();
        if (valid && phase == SourceValidationPhase::Definition && !resource_exhausted_ && !context_unavailable_) {
            // Only context-free definition proofs may survive helper erasure.
            // Deferred expressions stay intact; invocation preparation never
            // publishes its record copies into the source declaration table.
            for (const auto& [key, view] : *record_views_) {
                if (!key.identity || key.identity->function_scope != function.function_scope) continue;
                for (auto& record : program_.records)
                    if (record.complete && record.nominal_key() == key) {
                        record = copy_evaluation_declaration(*view.definition);
                        break;
                    }
            }
        }
        validation_phase_ = previous_phase;
        current_function_ = previous_function;
        current_namespace_ = std::move(previous_namespace);
        co_return valid && !resource_exhausted_ && !context_unavailable_;
    }

    bool resource_exhausted() const { return resource_exhausted_; }

    void diagnose(SourceLocation fallback) const {
        // A synchronous layout client may already have reported this failure
        // to the same sink before propagating it back through the evaluator.
        if (diagnostics_.errors() == diagnostics_at_entry_)
            diagnose(diagnostics_, fallback);
    }

    void diagnose(Diagnostics& diagnostics, SourceLocation fallback) const {
        diagnostics.error(
            failure_location_.valid() ? failure_location_ : fallback,
            failure_reason_.value_or("expression is not a translation-time value"));
        for (auto frame = failure_trace_.rbegin();
             frame != failure_trace_.rend(); ++frame) {
            diagnostics.note(
                frame->location,
                "while evaluating call to '" + frame->function + "'");
        }
    }

    struct EvaluatedCallArgument {
        std::optional<EvalValue> input;
        std::optional<EvalValue> destination;
        SourceLocation location;
    };

    struct PreparedCall {
        const FunctionDecl* function{};
        std::vector<EvaluatedCallArgument> arguments;
        SourceLocation location;
    };

    // A [[musttail]] return releases its frame before the callee runs, so a
    // chain of tail calls runs at the depth of its first call.
    EvaluationTask<std::optional<EvalValue>> call_async(const FunctionDecl& source,
                                  const std::vector<EvaluatedCallArgument>& arguments,
                                  SourceLocation location) {
        std::shared_ptr<PreparedCall> tail;
        auto result = co_await call_frame_async(source, arguments, location, tail);
        if (!tail) co_return result;
        while (tail) {
            const auto next = std::move(tail);
            result = co_await call_frame_async(*next->function, next->arguments, next->location, tail);
        }
        if (result && source.return_type && !(source.return_type->kind == Type::Kind::Builtin &&
                                                source.return_type->builtin == BuiltinType::Void))
            result = co_await convert_async(*result, source.return_type, location);
        co_return result;
    }

    EvaluationTask<std::optional<EvalValue>> call_frame_async(const FunctionDecl& source,
                                  const std::vector<EvaluatedCallArgument>& arguments,
                                  SourceLocation location, std::shared_ptr<PreparedCall>& tail) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        const FunctionDecl* selected = &source;
        if (evaluation_only(source)) {
            auto& prepared = invocation_functions_[&source];
            if (!prepared) prepared = copy_evaluation_declaration(source);
            selected = prepared.get();
        }
        const auto& function = *selected;
        if (++depth_ > program_.evaluation_limits.depth) {
            fail_resource(location,
                 "translation-time recursion depth exceeded " +
                     std::to_string(program_.evaluation_limits.depth));
            --depth_;
            co_return std::nullopt;
        }
        if (!function.body || arguments.size() != function.parameters.size()) {
            fail(location,
                 "function '" + function.name +
                     "' has no visible implementation for translation-time "
                     "evaluation");
            --depth_;
            co_return std::nullopt;
        }
        for (const auto& parameter : function.parameters) {
            if (evaluation_only(source) && parameter.mode != ParameterMode::In) {
                fail(location,
                     "translation-time evaluation of '" + function.name +
                         "' requires only 'in' parameters");
                --depth_;
                co_return std::nullopt;
            }
        }
        call_stack_.push_back({function.name, location});
        if (!source_validated_.contains(&function)) {
            if (evaluation_only(source) && program_.evaluation_prepare_function) {
                auto& prepared = *invocation_functions_.at(&source);
                const auto* previous = current_function_;
                const auto previous_phase = validation_phase_;
                current_function_ = &prepared;
                validation_phase_ = SourceValidationPhase::Invocation;
                const bool bounds_ready = co_await prepare_bound_types_async(prepared, SourceValidationPhase::Invocation);
                const auto prepare = program_.evaluation_prepare_function;
                const bool ready = bounds_ready && (co_await prepare.async(prepared));
                current_function_ = previous;
                validation_phase_ = previous_phase;
                if (!ready) {
                    call_stack_.pop_back();
                    --depth_;
                    co_return std::nullopt;
                }
            }
            if (!(co_await validate_source_body_async(function, SourceValidationPhase::Invocation))) {
                call_stack_.pop_back();
                --depth_;
                co_return std::nullopt;
            }
            source_validated_.insert(&function);
        }
        const auto previous_base = frame_base_;
        frame_base_ = scopes_.size();
        scopes_.emplace_back();
        const auto previous = current_function_;
        current_function_ = &function;
        bool valid = true;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto& parameter = function.parameters[index];
            std::optional<EvalValue> value;
            if (parameter.mode == ParameterMode::Out) {
                value = EvalValue{UInt128{}, clone_type(parameter.type)};
                if (parameter.type->kind == Type::Kind::Record ||
                    parameter.type->kind == Type::Kind::Vector)
                    value = co_await new_object_async(parameter.type, arguments[index].location);
            } else if (arguments[index].input) {
                value = (co_await convert_async(*arguments[index].input, parameter.type, arguments[index].location));
            }
            if (!value) { valid = false; break; }
            scopes_.back()[name_key(parameter)] = {
                *value, parameter.mode != ParameterMode::Out || value->object != nullptr,
                parameter.type->is_const};
        }
        Flow flow{Flow::Failed};
        if (valid) flow = co_await statement_async(*function.body);
        if (flow.kind == Flow::Return && flow.tail) {
            tail = std::move(flow.tail);
            current_function_ = previous;
            pop_scope();
            frame_base_ = previous_base;
            call_stack_.pop_back();
            --depth_;
            co_return std::nullopt;
        }
        // A successful evaluator call is a normal return, including implicit
        // fallthrough. Preserve failures from the body (assertions, sandbox or
        // resource limits), and enforce the contract before returning any value.
        const bool no_return = function_entity_attribute(program_, function, "noreturn");
        if (no_return && (flow.kind == Flow::Normal || flow.kind == Flow::Return)) {
            fail(flow.return_location.valid() ? flow.return_location : function.body->location,
                "noreturn function '" + function.name + "' returned normally during translation-time evaluation");
            flow.kind = Flow::Failed;
        }
        std::optional<EvalValue> result;
        if (function.return_type && function.return_type->kind == Type::Kind::Builtin &&
            function.return_type->builtin == BuiltinType::Void &&
            (flow.kind == Flow::Normal || (flow.kind == Flow::Return &&
             (!flow.value || (flow.value->type && flow.value->type->kind == Type::Kind::Builtin &&
                              flow.value->type->builtin == BuiltinType::Void))))) {
            result = EvalValue{UInt128{}, builtin_type(BuiltinType::Void)};
        } else if (flow.kind == Flow::Return && flow.value) {
            // Save the ordinary result before any destination can be changed.
            result = (co_await convert_async(*flow.value, function.return_type, location));
        } else if (flow.kind != Flow::Failed) {
            fail(location,
                 "translation-time call to '" + function.name +
                     "' did not produce a value");
        }
        // Snapshot all output cells before delivery: parameter cells are
        // distinct even when their actuals alias, and a failed body/return or
        // uninitialized output must not start copy-out. Keep callee storage
        // alive until the snapshots and stores have finished.
        std::vector<std::optional<EvalValue>> outputs(arguments.size());
        if (result) {
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                const auto& parameter = function.parameters[index];
                if (parameter.mode == ParameterMode::In) continue;
                const auto& cell = scopes_[frame_base_].at(name_key(parameter));
                if (parameter.mode == ParameterMode::Out && cell.value.object &&
                    !(co_await initialized_meta_output_async(cell.value, location))) {
                    fail(location, "output parameter '" + parameter.name +
                        "' was not assigned on normal return during translation-time evaluation");
                    result.reset();
                    break;
                }
                outputs[index] = (co_await read_cell_async(cell, location));
                if (!outputs[index]) { result.reset(); break; }
            }
        }
        struct OutputUndo {
            std::shared_ptr<EvalBuffer> storage;
            std::size_t offset{};
            std::string data;
            std::vector<std::uint8_t> assigned, effective_type;
        };
        struct OutputMetadataUndo {
            std::shared_ptr<EvalBuffer> storage;
            std::vector<EvalBuffer::TypedObject> typed_objects;
            std::vector<EvalBuffer::PointerObject> pointers;
        };
        std::vector<OutputUndo> undo;
        std::vector<OutputMetadataUndo> metadata_undo;
        std::unordered_set<EvalBuffer*> saved_metadata;
        if (result) {
            // Journal only affected ranges (including partially overwritten
            // opaque slots), not entire allocations behind sliced views. All
            // snapshots and their budget charges precede the first copy-out.
            for (const auto& argument : arguments) {
                if (!argument.destination) continue;
                const auto& destination = *argument.destination;
                const auto index = (co_await meta_access_index_async(destination, argument.location, true));
                const auto extent = (co_await meta_object_size_async(destination.type->pointee));
                if (!index || !extent || !destination.meta_pointer->mutable_buffer) {
                    result.reset();
                    break;
                }
                const auto storage = destination.meta_pointer->mutable_buffer;
                auto begin = *index, end = *index + *extent;
                for (const auto& slot : storage->pointers) {
                    if (*index >= slot.offset + slot.length || slot.offset >= *index + *extent) continue;
                    begin = std::min(begin, slot.offset);
                    end = std::max(end, slot.offset + slot.length);
                }
                if (end - begin > std::numeric_limits<std::size_t>::max() / 3 ||
                    !charge_meta_bytes((end - begin) * 3, argument.location)) {
                    result.reset();
                    break;
                }
                if (saved_metadata.insert(storage.get()).second) {
                    if (!charge_meta_bytes(storage->typed_objects.size() * 24 +
                                           storage->pointers.size() * 64, argument.location)) {
                        result.reset();
                        break;
                    }
                    metadata_undo.push_back({storage, storage->typed_objects, storage->pointers});
                }
                undo.push_back({storage, begin, storage->data.substr(begin, end - begin),
                    {storage->assigned.begin() + static_cast<std::ptrdiff_t>(begin),
                     storage->assigned.begin() + static_cast<std::ptrdiff_t>(end)},
                    {storage->effective_type.begin() + static_cast<std::ptrdiff_t>(begin),
                     storage->effective_type.begin() + static_cast<std::ptrdiff_t>(end)}});
            }
        }
        if (result) {
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (!arguments[index].destination) continue;
                if (!(co_await store_meta_pointer_async(arguments[index].destination, outputs[index],
                                        arguments[index].location))) {
                    result.reset();
                    for (const auto& saved : undo) {
                        std::copy(saved.data.begin(), saved.data.end(), saved.storage->data.begin() +
                            static_cast<std::ptrdiff_t>(saved.offset));
                        std::copy(saved.assigned.begin(), saved.assigned.end(), saved.storage->assigned.begin() +
                            static_cast<std::ptrdiff_t>(saved.offset));
                        std::copy(saved.effective_type.begin(), saved.effective_type.end(), saved.storage->effective_type.begin() +
                            static_cast<std::ptrdiff_t>(saved.offset));
                    }
                    for (auto& saved : metadata_undo) {
                        saved.storage->typed_objects = std::move(saved.typed_objects);
                        saved.storage->pointers = std::move(saved.pointers);
                    }
                    break;
                }
            }
        }
        current_function_ = previous;
        pop_scope();
        frame_base_ = previous_base;
        call_stack_.pop_back();
        --depth_;
        co_return result;
    }

    EvaluationTask<std::optional<EvalValue>> expression_async(const Expr& expression) {
        if (resource_exhausted_ || context_unavailable_) co_return std::nullopt;
        if (!step(expression.location)) co_return std::nullopt;
        switch (expression.kind) {
        case Expr::Kind::VoidValue:
            co_return EvalValue{0, builtin_type(BuiltinType::Void)};
        case Expr::Kind::Quote: {
            if (!procedural_) {
                fail_context(expression.location);
                co_return std::nullopt;
            }
            if (expression.quote_fragments.size() != expression.arguments.size() + 1) {
                fail(expression.location, "invalid quotation fragment structure");
                co_return std::nullopt;
            }
            TokenSequence result;
            const auto context = literal_context(expression);
            if (!context) co_return std::nullopt;
            for (std::size_t index = 0; index < expression.quote_fragments.size(); ++index) {
                auto literal = expression.quote_fragments[index];
                for (auto& token : literal) {
                    // Names bound to the helper's block-scope types keep them.
                    auto alias = std::move(token.origin.alias_binding);
                    auto tag = std::move(token.origin.tag_binding);
                    token.origin = {token_origin(macro_context_->invocation).span,
                                    {}, context, {}, 0, {}};
                    token.origin.span_end = token_origin(macro_context_->invocation).last_span();
                    token.origin.alias_binding = std::move(alias);
                    token.origin.tag_binding = std::move(tag);
                }
                if (!append_tokens(result, literal, expression.location))
                    co_return std::nullopt;
                if (index == expression.arguments.size()) break;
                auto value = (co_await this->expression_async(*expression.arguments[index]));
                if (!value) co_return std::nullopt;
                if (value->syntax_node) {
                    MetaToken splice;
                    splice.kind = TokenKind::StructuredSplice;
                    splice.text = "__cross_syntax_splice";
                    splice.origin = token_origin(value->syntax_node->span.first);
                    splice.origin.context = value->syntax_node->context;
                    splice.splice = value->syntax_node;
                    if (!append_tokens(result, {splice}, expression.arguments[index]->location))
                        co_return std::nullopt;
                    continue;
                }
                if (!value->tokens) {
                    fail(expression.arguments[index]->location,
                         "$::unquote requires a token value or syntax node");
                    co_return std::nullopt;
                }
                if (!append_tokens(result, *value->tokens, expression.arguments[index]->location))
                    co_return std::nullopt;
            }
            co_return token_value(std::move(result), expression.location);
        }
        case Expr::Kind::ByteSequence:
            if (expression.type) {
                auto value = co_await new_object_async(expression.type, expression.location, true);
                if (!value || expression.string_value.size() != value->object->data.size()) co_return std::nullopt;
                value->object->data = expression.string_value;
                for (const auto& relocation : expression.object_relocations) {
                    auto pointer = std::make_shared<EvalValue>(UInt128{}, relocation.type);
                    if (const auto* address = std::get_if<AddressConstant>(&relocation.address)) {
                        pointer->address = *address;
                    } else {
                        const auto& label = std::get<LabelAddressConstant>(relocation.address);
                        if (!charge_meta_bytes(64 + label.global_name.size(), expression.location)) co_return std::nullopt;
                        pointer->label_address = std::make_shared<const LabelAddressConstant>(label);
                    }
                    const auto length = co_await meta_object_size_async(relocation.type);
                    if (!length || *length != relocation.length || relocation.offset > value->object->data.size() ||
                        *length > value->object->data.size() - relocation.offset ||
                        !charge_meta_bytes(64 + type_name(relocation.type).size(), expression.location)) co_return std::nullopt;
                    value->object->pointers.push_back({static_cast<std::size_t>(relocation.offset),
                        *length, std::move(pointer), {}, false});
                }
                co_return value;
            }
            fail(expression.location, "materialized bytes are not an expression");
            co_return std::nullopt;
        case Expr::Kind::Integer: {
            auto value = parse_integer_value(expression);
            if (value && !expression.evaluated_integer) {
                const auto type = integer_type(value->type);
                if (type.is_signed ? !fits_signed_positive(value->integer, type.bits)
                                   : !fits_unsigned(value->integer, type.bits)) value.reset();
            }
            if (!value) fail(expression.location, "integer literal is not representable in its type");
            co_return value;
        }
        case Expr::Kind::Floating: {
            auto value = parse_floating_value(expression, program_.address_bits);
            if (!value) fail(expression.location,
                             "floating literal is invalid or not representable");
            co_return value;
        }
        case Expr::Kind::Character: {
            const auto decoded = decode_character_literal(expression.text);
            if (!decoded) {
                fail(expression.location, "invalid character literal");
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{*decoded},
                             builtin_type(BuiltinType::U32)};
        }
        case Expr::Kind::String: {
            auto decoded = expression.string_value.empty()
                               ? decode_string_literal(expression.text)
                               : std::optional<std::string>(
                                     expression.string_value);
            if (!decoded) co_return std::nullopt;
            decoded->push_back('\0');
            co_return EvalValue{{}, pointer_type(builtin_type(BuiltinType::U8, true)),
                             std::make_shared<std::string>(std::move(*decoded)), 0};
        }
        case Expr::Kind::Name:
            co_return (co_await lookup_async(expression));
        case Expr::Kind::Address: {
            if (!pointer_resolver_ || !expression.evaluated_address) {
                fail(expression.location,
                     "a runtime address cannot be inspected during translation-time execution");
                co_return std::nullopt;
            }
            co_return co_await resolve_pointer_async(clone_expr(expression), expression.type);
        }
        case Expr::Kind::Parenthesized:
            if (expression.left) co_return co_await this->expression_async(*expression.left);
            co_return std::nullopt;
        case Expr::Kind::Cast: {
            std::optional<EvalValue> value;
            if (expression.left) value = co_await this->expression_async(*expression.left);
            if (!value || !expression.type) co_return std::nullopt;
            co_return co_await convert_async(*value, expression.type, expression.location, true);
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
                co_return std::nullopt;
            }
            if (!type) {
                fail(expression.location,
                     "target layout is unavailable for this translation-time query");
                co_return std::nullopt;
            }
            if (expression.kind == Expr::Kind::Sizeof && !expression.type)
                co_await complete_initializer_extent_async(type);
            // Array alignment is independent of its count; vector alignment
            // can depend on its lanes. Pointer layout never needs either
            // pointee extent. Genuine incomplete arrays/VLAs are not deferred.
            for (auto array = type; array; array = array->element) {
                if (deferred_vector_extent(array)) {
                    fail_context(expression.location);
                    co_return std::nullopt;
                }
                if (array->kind != Type::Kind::Array) break;
                if (expression.kind == Expr::Kind::Sizeof) {
                    if (!array->lanes &&
                        array->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext) {
                        fail_context(expression.location);
                        co_return std::nullopt;
                    }
                }
            }
            if (!query && meta_object_type(type)) {
                if (const auto size = (co_await meta_object_size_async(type))) {
                    auto value = size;
                    if (expression.kind == Expr::Kind::Alignof)
                        value = co_await meta_object_alignment_async(type);
                    if (value)
                        co_return EvalValue{UInt128{*value},
                                         builtin_type(BuiltinType::Uptr)};
                }
            }
            if (!query) {
                fail(expression.location,
                     "target layout is unavailable for this translation-time query");
                co_return std::nullopt;
            }
            const auto value = co_await query->async(type);
            if (!value) {
                fail(expression.location,
                     expression.kind == Expr::Kind::Sizeof
                         ? "sizeof requires a complete object type with fixed size"
                         : "$::alignof requires a complete object type");
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{*value},
                             builtin_type(BuiltinType::Uptr)};
        }
        case Expr::Kind::Offsetof: {
            std::vector<OffsetofStep> steps;
            if (const auto issue = source_offsetof_error(expression, program_, &steps)) {
                fail(issue->location, issue->message);
                co_return std::nullopt;
            }
            std::uint64_t offset{};
            for (const auto& step : steps) {
                std::optional<std::uint64_t> displacement;
                if (step.member) {
                    if (program_.evaluation_member_layout)
                        if (const auto layout = co_await program_.evaluation_member_layout.async(
                                step.owner, step.designator->member_name()))
                            displacement = layout->offset;
                } else {
                    const auto index = co_await this->expression_async(*step.designator->index);
                    if (!index) co_return std::nullopt;
                    if (!is_integer(index->type) || integer_negative(index->integer,
                            evaluation_integer_type(index->type, program_.address_bits)) ||
                        index->integer.high != 0 || index->integer.low >= step.owner->lanes) {
                        fail(step.designator->location, "$::offsetof index is out of range");
                        co_return std::nullopt;
                    }
                    const auto size = size_of_ ? co_await size_of_->async(step.owner->element)
                                               : std::nullopt;
                    if (size && (*size == 0 ||
                                 index->integer.low <= std::numeric_limits<std::uint64_t>::max() / *size))
                        displacement = index->integer.low * *size;
                }
                if (!displacement || *displacement > std::numeric_limits<std::uint64_t>::max() - offset) {
                    fail(expression.location, "target layout is unavailable for this translation-time query");
                    co_return std::nullopt;
                }
                offset += *displacement;
            }
            if (!fits_unsigned(UInt128{offset}, program_.address_bits)) {
                fail(expression.location, "$::offsetof result is not representable as uptr");
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{offset}, builtin_type(BuiltinType::Uptr)};
        }
        case Expr::Kind::Unary:
            co_return (co_await unary_async(expression));
        case Expr::Kind::Binary:
            co_return (co_await binary_async(expression));
        case Expr::Kind::Conditional: {
            const auto type = expression_type(expression);
            auto condition = (co_await this->expression_async(*expression.left));
            if (!condition || !known_truth(*condition, expression.location) || !type)
                co_return std::nullopt;
            auto value = (co_await this->expression_async(*(condition->truthy()
                                          ? expression.right
                                          : expression.third)));
            if (!value) co_return std::nullopt;
            co_return co_await convert_async(*value, type, expression.location);
        }
        case Expr::Kind::Assign:
            co_return (co_await assign_async(expression));
        case Expr::Kind::Call:
            co_return (co_await call_expression_async(expression));
        case Expr::Kind::AggregateInitializer:
            fail(expression.location,
                 "an aggregate initializer is not a scalar expression");
            co_return std::nullopt;
        }
        co_return std::nullopt;
    }

private:
    std::shared_ptr<const SyntaxContext> literal_context(const Expr& expression) {
        const auto source = expression.translation_context ? expression.translation_context
            : current_function_ ? current_function_->translation_context : nullptr;
        if (!source) return macro_context_;
        auto& cached = construction_contexts_[source.get()];
        if (!cached) {
            auto context = std::make_shared<SyntaxContext>(*source);
            context->kind = SyntaxContext::Kind::DefinitionSite;
            if (current_function_) context->definition = current_function_->location;
            context->expansion = macro_context_->expansion;
            context->invocation = macro_context_->invocation;
            cached = std::move(context);
        }
        if (!charge_input_context(cached, expression.location)) return {};
        return cached;
    }

    bool charge_input_context(const std::shared_ptr<const SyntaxContext>& context, SourceLocation location) {
        if (!context || !charged_contexts_.insert(context).second) return true;
        auto size = syntax_context_storage(*context);
        if (context->parse_environment &&
            !charged_environments_.insert(context->parse_environment).second)
            size -= syntax_environment_storage(*context->parse_environment);
        if (size > program_.evaluation_limits.bytes) {
            fail_resource(location, "translation-time syntax context byte budget exceeded");
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

    std::optional<EvalValue> token_value(TokenSequence tokens, SourceLocation location,
                                        bool require_balanced = false) {
        // Explicit public-token projection may expose a lone delimiter for
        // later composition. Charge traversal/depth without making that value
        // a complete token tree; string parsing and tree indexing require one.
        if (!visit_token_trees(tokens, location, [](TokenTreeRange) { return true; },
                              require_balanced))
            return std::nullopt;
        EvalValue result{UInt128{}, tokens_type()};
        result.tokens = std::make_shared<const TokenSequence>(std::move(tokens));
        return result;
    }

    std::optional<EvalValue> meta_count_value(std::size_t count,
        SourceLocation location, std::string_view operation) {
        const UInt128 value{count};
        if (!fits_unsigned(value, program_.address_bits)) {
            fail(location, std::string(operation) + " count exceeds target uptr capacity");
            return std::nullopt;
        }
        return EvalValue{value, builtin_type(BuiltinType::Uptr)};
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

    template<class Complete>
    bool visit_token_trees(const TokenSequence& tokens, SourceLocation location, Complete&& complete,
                           bool require_balanced = true) {
        const auto error = scan_token_trees(tokens, program_.evaluation_limits.depth,
            require_balanced ? TokenTreeBalance::Required : TokenTreeBalance::Fragments,
            {[&](std::size_t) { return step(location); },
             // Preserve logical high-water accounting, independently of the
             // host vector's capacity or the typed stack element's sizeof.
             [&] { return charge_meta_bytes(16, location); },
             [&](TokenTreeRange range) { return complete(range); }});
        if (error == TokenTreeScanError::DepthLimit)
            fail_resource(location, "translation-time token-tree nesting depth exceeded");
        else if (error == TokenTreeScanError::UnmatchedDelimiter ||
                 error == TokenTreeScanError::UnterminatedGroup)
            fail(location, "token sequence requires balanced token groups");
        return error == TokenTreeScanError::None;
    }

    std::optional<std::vector<std::pair<std::size_t, std::size_t>>>
    token_trees(const TokenSequence& tokens, SourceLocation location) {
        std::vector<std::pair<std::size_t, std::size_t>> trees;
        if (!visit_token_trees(tokens, location, [&](TokenTreeRange range) {
            if (!charge_meta_bytes(16, location)) return false;
            trees.emplace_back(range.begin, range.end);
            return true;
        })) return std::nullopt;
        return trees;
    }

    EvaluationTask<std::optional<EvalValue>> inspect_token_async(const Expr& expression) {
        const auto& name = expression.left->text;
        const auto count = name == "$::meta::is_kind" ? 2U : 1U;
        if (!procedural_ || expression.arguments.size() != count) {
            fail(expression.location, name + " has an invalid argument count");
            co_return std::nullopt;
        }
        auto value = (co_await this->expression_async(*expression.arguments[0]));
        if (!value || !value->tokens) {
            fail(expression.location, name + " requires $::meta::tokens");
            co_return std::nullopt;
        }
        std::optional<TokenTreeRange> one;
        bool multiple{};
        if (!visit_token_trees(*value->tokens, expression.location, [&](TokenTreeRange range) {
            if (one) multiple = true;
            else one = range;
            return true;
        })) co_return std::nullopt;
        if (!one || multiple) {
            fail(expression.location, name + " requires exactly one token-tree element");
            co_return std::nullopt;
        }
        const auto& first = (*value->tokens)[one->begin];
        const bool group = one->delimiter != TokenDelimiter::None;
        if (name == "$::meta::is_kind") {
            auto kind = (co_await this->expression_async(*expression.arguments[1]));
            if (!kind || !kind->string || kind->offset >= kind->string->size()) {
                fail(expression.location, name + " requires a translation-time kind string");
                co_return std::nullopt;
            }
            const auto written = std::string_view(*kind->string).substr(kind->offset,
                kind->string->size() - kind->offset - 1);
            bool matches{};
            if (written == "group") matches = group;
            else {
                const auto found = meta_lexical_kind(written);
                if (!found) {
                    fail(expression.location, "unknown token-tree kind '" + std::string(written) + "'");
                    co_return std::nullopt;
                }
                matches = !group && first.kind == *found;
            }
            co_return EvalValue{UInt128{matches}, builtin_type(BuiltinType::Bool)};
        }
        if (name == "$::meta::span") {
            if (first.kind == TokenKind::StructuredSplice && first.splice)
                co_return span_value(first.splice->span, expression.location);
            co_return span_value({first.origin.span, (*value->tokens)[one->end - 1].origin.last_span()},
                              expression.location);
        }
        if (name == "$::meta::spelling") {
            if (group || first.kind == TokenKind::StructuredSplice) {
                fail(expression.location, "$::meta::spelling requires a lexical leaf, not a group or splice");
                co_return std::nullopt;
            }
            if (first.text.size() > program_.evaluation_limits.bytes ||
                !fits_unsigned(UInt128{first.text.size()}, program_.address_bits)) {
                fail_resource(expression.location, "token spelling exceeds target uptr or byte capacity");
                co_return std::nullopt;
            }
            if (!step(expression.location, first.text.size()) ||
                !charge_meta_bytes(first.text.size(), expression.location)) co_return std::nullopt;
            EvalValue result{UInt128{}, bytes_type()};
            result.bytes = std::make_shared<const std::string>(first.text);
            result.byte_length = first.text.size();
            co_return result;
        }
        if (!group) {
            fail(expression.location, name + " requires a balanced group");
            co_return std::nullopt;
        }
        if (name == "$::meta::children") {
            TokenSequence result;
            const TokenSequence children(value->tokens->begin() + static_cast<std::ptrdiff_t>(one->begin + 1),
                                         value->tokens->begin() + static_cast<std::ptrdiff_t>(one->end - 1));
            if (!append_tokens(result, children, expression.location)) co_return std::nullopt;
            co_return token_value(std::move(result), expression.location, true);
        }
        std::string_view delimiter;
        switch (one->delimiter) {
        case TokenDelimiter::Parenthesis: delimiter = "()"; break;
        case TokenDelimiter::Bracket: delimiter = "[]"; break;
        case TokenDelimiter::Brace: delimiter = "{}"; break;
        case TokenDelimiter::Attribute: delimiter = "[[]]"; break;
        case TokenDelimiter::None: break;
        }
        if (!charge_meta_bytes(delimiter.size() + 1, expression.location)) co_return std::nullopt;
        auto text = std::make_shared<std::string>(delimiter);
        text->push_back('\0');
        co_return EvalValue{UInt128{}, pointer_type(builtin_type(BuiltinType::U8, true)), std::move(text)};
    }

    EvaluationTask<std::optional<EvalValue>> construct_tokens_async(const Expr& expression) {
        const auto& name = expression.left->text;
        const bool group = name == "$::meta::group";
        if (!procedural_ || (expression.arguments.size() != 2 && expression.arguments.size() != 3)) {
            fail(expression.location, name + " requires two arguments and an optional source span");
            co_return std::nullopt;
        }
        auto selector = (co_await this->expression_async(*expression.arguments[0]));
        auto contents = (co_await this->expression_async(*expression.arguments[1]));
        if (!selector || !selector->string || selector->offset >= selector->string->size() || !contents) {
            fail(expression.location, name + " requires a translation-time selector string and contents");
            co_return std::nullopt;
        }
        const auto written = std::string_view(*selector->string).substr(selector->offset,
            selector->string->size() - selector->offset - 1);
        const auto invocation = token_origin(macro_context_->invocation);
        SyntaxSpan span{invocation.span, invocation.last_span()};
        if (expression.arguments.size() == 3) {
            auto supplied = (co_await this->expression_async(*expression.arguments[2]));
            if (!supplied || !supplied->syntax_span) {
                fail(expression.arguments[2]->location, name + " requires a source span as its third argument");
                co_return std::nullopt;
            }
            span = *supplied->syntax_span;
        }
        if (!group) {
            const auto kind = meta_lexical_kind(written);
            if (!kind || *kind == TokenKind::StructuredSplice) {
                fail(expression.arguments[0]->location, "$::meta::token requires a lexical leaf kind");
                co_return std::nullopt;
            }
            if ((!contents->string && !contents->bytes) ||
                (contents->string && contents->offset >= contents->string->size())) {
                fail(expression.arguments[1]->location, "$::meta::token text must be a translation-time string or bytes");
                co_return std::nullopt;
            }
            const auto text = contents->bytes
                ? std::string_view(*contents->bytes).substr(contents->byte_offset, contents->byte_length)
                : std::string_view(*contents->string).substr(contents->offset,
                    contents->string->size() - contents->offset - 1);
            auto result = parse_tokens(text, expression.arguments[1]->location, expression, name);
            if (!result) co_return std::nullopt;
            if (result->size() != 1 || result->front().kind != *kind || result->front().text != text) {
                fail(expression.arguments[1]->location,
                     "$::meta::token text must spell exactly one leaf of the requested kind, without trivia");
                co_return std::nullopt;
            }
            if (*kind == TokenKind::Punctuator &&
                (text == "(" || text == ")" || text == "[" || text == "]" ||
                 text == "{" || text == "}" || text == "[[" || text == "]]")) {
                fail(expression.arguments[1]->location, "$::meta::token cannot construct group delimiters");
                co_return std::nullopt;
            }
            result->front().origin.span = span.first;
            result->front().origin.span_end = span.last;
            co_return token_value(std::move(*result), expression.location, true);
        }
        if (written != "()" && written != "[]" && written != "{}" && written != "[[]]") {
            fail(expression.arguments[0]->location, "$::meta::group requires one of (), [], {}, or [[]]");
            co_return std::nullopt;
        }
        if (!contents->tokens) {
            fail(expression.arguments[1]->location, "$::meta::group contents must be tokens");
            co_return std::nullopt;
        }
        if (!visit_token_trees(*contents->tokens, expression.location, [](TokenTreeRange) { return true; }))
            co_return std::nullopt;
        const auto context = literal_context(expression);
        if (!context) co_return std::nullopt;
        MetaToken open, close;
        open.kind = close.kind = TokenKind::Punctuator;
        open.text = written.substr(0, written.size() / 2);
        close.text = written.substr(written.size() / 2);
        open.origin = close.origin = {span.first, {}, context, {}, 0, {}};
        open.origin.span_end = close.origin.span_end = span.last;
        TokenSequence result;
        if (!append_tokens(result, {open}, expression.location) ||
            !append_tokens(result, *contents->tokens, expression.location) ||
            !append_tokens(result, {close}, expression.location)) co_return std::nullopt;
        co_return token_value(std::move(result), expression.location, true);
    }

    bool append_tokens(TokenSequence& result, const TokenSequence& part, SourceLocation location) {
        // Charge materialized output, including intermediate copies, so a
        // bounded loop cannot grow token storage exponentially without limit.
        const auto budget = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        // Logical metadata charge, not sizeof(MetaToken): resource decisions
        // must not depend on the host C++ library's string/pointer layout.
        for (const auto& token : part) {
            const auto metadata_cost = meta_token_storage_bytes + origin_binding_storage(token.origin);
            if (token.text.size() > budget - token_bytes_ ||
                metadata_cost > budget - token_bytes_ - token.text.size()) {
                fail_resource(location, "translation-time token construction budget exceeded " +
                    std::to_string(budget) + " bytes");
                return false;
            }
            const auto memory_budget = static_cast<std::size_t>(program_.evaluation_limits.memory);
            if (token.text.size() + metadata_cost >
                memory_budget - meta_bytes_ - token_bytes_) {
                fail_resource(location, "translation-time meta memory budget exceeded " +
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
            fail_resource(location, "translation-time meta memory budget exceeded " +
                std::to_string(budget) + " bytes");
            return false;
        }
        meta_bytes_ += size;
        return true;
    }

    std::optional<TokenSequence> parse_tokens(std::string_view text, SourceLocation location,
                                            const Expr& construction,
                                            std::string_view operation = "$::meta::parse") {
        // Lex at the string boundary without retaining pointers into this
        // temporary source. Diagnostics belong to the actual macro expression.
        // Even whitespace/comments require scanning, and the temporary source
        // owns a copy. Charge both before invoking the lexer.
        if (text.size() > program_.evaluation_limits.bytes ||
            !fits_unsigned(UInt128{text.size()}, program_.address_bits)) {
            fail_resource(location, std::string(operation) + " input exceeds target uptr or byte capacity");
            return std::nullopt;
        }
        if (!step(location, text.size()) || !charge_meta_bytes(text.size(), location))
            return std::nullopt;
        if (text.find('\0') != std::string_view::npos) {
            fail(location, std::string(operation) + " string contains a zero byte");
            return std::nullopt;
        }
        // The byte overload is exact-length UTF-8, not a C string or a stream.
        // Apply the same validity rule to both overloads, including trivia and
        // literal contents that the lexical scanner otherwise treats opaquely.
        for (std::size_t at = 0; at < text.size();) {
            const auto lead = static_cast<unsigned char>(text[at++]);
            if (lead < 0x80) continue;
            const unsigned remaining = lead >= 0xc2 && lead <= 0xdf ? 1
                : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
            bool valid = remaining != 0 && remaining <= text.size() - at;
            for (unsigned byte = 0; valid && byte < remaining; ++byte) {
                const auto next = static_cast<unsigned char>(text[at++]);
                valid = next >= 0x80 && next <= 0xbf;
                if (byte == 0)
                    valid = valid && !(lead == 0xe0 && next < 0xa0) &&
                        !(lead == 0xed && next >= 0xa0) && !(lead == 0xf0 && next < 0x90) &&
                        !(lead == 0xf4 && next >= 0x90);
            }
            if (!valid) {
                fail(location, std::string(operation) + " input is not valid UTF-8");
                return std::nullopt;
            }
        }
        SourceFile source("<meta::parse>", std::string(text));
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto tokens = Lexer(source, diagnostics).lex();
        if (diagnostics.errors() != 0) {
            fail(location, std::string(operation) + " could not tokenize its string");
            return std::nullopt;
        }
        TokenSequence result;
        const auto context = literal_context(construction);
        if (!context) return std::nullopt;
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::End) break;
            MetaToken value(token);
            value.origin = {token_origin(macro_context_->invocation).span,
                            {}, context, {}, 0, {}};
            value.origin.span_end = token_origin(macro_context_->invocation).last_span();
            // The construction site's block-scope typedefs and tags bind names
            // as they do in a quote there.
            if (value.kind == TokenKind::Identifier && !construction.quote_fragments.empty()) {
                const bool tag = !result.empty() && (result.back().text == "struct" ||
                    result.back().text == "union" || result.back().text == "enum");
                for (const auto& bound : construction.quote_fragments.front()) {
                    if (bound.text != value.text) continue;
                    if (tag && bound.origin.tag_binding) value.origin.tag_binding = bound.origin.tag_binding;
                    else if (!tag && bound.origin.alias_binding)
                        value.origin.alias_binding = bound.origin.alias_binding;
                }
            }
            if (!append_tokens(result, {value}, location)) return std::nullopt;
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
        if (value.label_address) return label_constant_expression(value.label_address, location);
        auto result = std::make_unique<Expr>();
        result->location = location;
        result->type = clone_type(value.type);
        if (value.address) {
            result->kind = Expr::Kind::Address;
            result->evaluated_address = value.address;
        } else if (!value.pointer() && (is_integer(value.type) || is_label_type(value.type))) {
            result->kind = Expr::Kind::Integer;
            result->evaluated_integer = {value.integer, value.type->builtin};
            result->text = to_decimal(value.integer);
        } else {
            fail(location, "translation-time object pointer cannot escape into a runtime address");
            return {};
        }
        return result;
    }

    EvaluationTask<std::optional<EvalValue>> resolve_pointer_async(std::unique_ptr<Expr> source,
                                            const TypePtr& destination) {
        if (!source || !destination || destination->kind != Type::Kind::Pointer ||
            !pointer_resolver_) co_return std::nullopt;
        std::vector<NameKey> locals;
        for (auto index = frame_base_; index < scopes_.size(); ++index)
            for (const auto& [name, cell] : scopes_[index]) locals.push_back(name);
        const auto resolver = *pointer_resolver_;
        if (!(co_await resolver.async(source, destination, current_function_, locals))) {
            fail(source->location, "address operation failed during translation-time evaluation");
            co_return std::nullopt;
        }
        EvalValue result{UInt128{}, destination};
        result.address = source->evaluated_address;
        if (result.address && result.address->kind == AddressConstant::Kind::Absolute) {
            auto null = std::make_unique<Expr>();
            null->kind = Expr::Kind::Integer;
            null->location = source->location;
            null->text = "0uptr";
            if (!(co_await resolver.async(null, destination, current_function_, locals)) ||
                !null->evaluated_address || null->evaluated_address->kind != AddressConstant::Kind::Absolute) {
                fail(source->location, "target null pointer representation is unavailable during translation-time evaluation");
                co_return std::nullopt;
            }
            result.null_address = null->evaluated_address->absolute;
        }
        co_return result;
    }

    // Evaluate only pointer bases and integer indices of an address designator.
    // Its designated cell is never read, nor can an automatic cell escape.
    EvaluationTask<std::unique_ptr<Expr>> address_designator_async(const Expr& node) {
        if (node.kind == Expr::Kind::Name) {
            if (lookup_mutable(node)) {
                fail(node.location, "translation-time automatic object address cannot escape");
                co_return {};
            }
            co_return clone_expr(node);
        }
        auto result = clone_expr(node);
        if (node.kind == Expr::Kind::Parenthesized && node.left) {
            result->left = (co_await address_designator_async(*node.left));
            co_return result->left ? std::move(result) : nullptr;
        }
        if (node.kind == Expr::Kind::Unary && node.text == "*" && node.left) {
            const auto base = (co_await expression_async(*node.left));
            if (!base) co_return {};
            result->left = value_expression(*base, node.left->location);
            co_return result->left ? std::move(result) : nullptr;
        }
        if (node.kind == Expr::Kind::Binary && node.left && node.right) {
            if (node.text == "member") {
                result->left = (co_await address_designator_async(*node.left));
                co_return result->left ? std::move(result) : nullptr;
            }
            if (node.text == "index" || node.text == "pointer_member") {
                const auto type = expression_type(*node.left, false);
                if (node.text == "index" && type &&
                    (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector)) {
                    result->left = (co_await address_designator_async(*node.left));
                } else {
                    auto base = (co_await expression_async(*node.left));
                    if (!base) co_return {};
                    result->left = value_expression(*base, node.left->location);
                }
                if (!result->left) co_return {};
                if (node.text == "index") {
                    const auto index = (co_await expression_async(*node.right));
                    if (!index) co_return {};
                    result->right = value_expression(*index, node.right->location);
                    if (!result->right) co_return {};
                }
                co_return result;
            }
        }
        fail(node.location, "unsupported translation-time address designator");
        co_return {};
    }

    struct Cell {
        EvalValue value;
        bool initialized{};
        bool read_only{};
        bool type_only{};
        std::shared_ptr<EvalBuffer> storage;
        Cell() = default;
        Cell(EvalValue initial, bool assigned, bool immutable, bool unavailable = false)
            : value(std::move(initial)), initialized(assigned), read_only(immutable), type_only(unavailable) {}
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
        return source_record_member(expression, program_,
            [&](const Expr& operand) { return expression_type(operand, false); }, owner_type);
    }

    EvaluationTask<bool> validate_control_intrinsic_async(const Expr& node) {
        if (control_intrinsic(node) == ControlIntrinsic::None) co_return true;
        std::vector<std::pair<NameKey, TypePtr>> locals;
        for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                locals.emplace_back(name, cell.value.type);
        if (const auto issue = co_await source_control_intrinsic_error_async(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                    SourceConstantProbe result;
                    result.value = co_await probe_integer_async(operand, locals, nullptr, true);
                    co_return result;
                })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        co_return !resource_exhausted_ && !context_unavailable_;
    }

    EvaluationTask<bool> validate_patch_constraints_async(const Expr& node, SourcePatchOperand patch_operand) {
        if (!patch_intrinsic(node) || node.arguments.empty() || node.arguments.size() > 2) co_return true;
        std::vector<std::pair<NameKey, TypePtr>> locals;
        for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                locals.emplace_back(name, cell.value.type);
        if (const auto issue = co_await source_patch_sink_error_async(node, program_, current_function_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                    SourceConstantProbe result;
                    result.value = co_await probe_integer_async(operand, locals, nullptr, true);
                    co_return result;
                })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (const auto issue = co_await source_patch_initial_error_async(node, program_, patch_operand,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand, const TypePtr& destination) -> EvaluationTask<SourceInitializerProbe> {
                    co_return co_await probe_initializer_async(operand, locals, destination, false);
                })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        co_return !resource_exhausted_ && !context_unavailable_;
    }

    EvaluationTask<bool> validate_atomic_intrinsic_async(const Expr& node) {
        if (atomic_builtin(node) == AtomicBuiltin::None) co_return true;
        std::vector<std::pair<NameKey, TypePtr>> locals;
        for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                locals.emplace_back(name, cell.value.type);
        if (const auto issue = co_await source_atomic_intrinsic_error_async(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                    SourceConstantProbe result;
                    result.value = co_await probe_integer_async(operand, locals);
                    co_return result;
                })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        co_return !resource_exhausted_ && !context_unavailable_;
    }

    // Required folding must not discard malformed syntax in an untaken arm.
    // This validates the scalar expression boundary without executing calls
    // or arithmetic (division by zero in a short-circuited arm is permitted).
    EvaluationTask<bool> validate_constant_lane_index_async(const Expr& node) {
        if (node.kind != Expr::Kind::Binary || node.text != "index" ||
            !node.left || !node.right) co_return true;
        const auto base = expression_type(*node.left);
        if (!base || base->kind != Type::Kind::Vector || base->scalable) co_return true;
        std::vector<std::pair<NameKey, TypePtr>> locals;
        for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                locals.emplace_back(name, cell.value.type);
        const auto value = co_await probe_integer_async(*node.right, locals);
        if (resource_exhausted_ || context_unavailable_) co_return false;
        if (!value || (!integer_negative(value->value,
                evaluation_integer_type(builtin_type(value->type), program_.address_bits)) &&
                value->value.high == 0 && value->value.low <
                    (base->lanes ? base->lanes : std::numeric_limits<std::uint32_t>::max()))) co_return true;
        fail(node.right->location, "fixed-vector lane index is out of range");
        co_return false;
    }

    EvaluationTask<std::optional<Expr::IntegerConstant>> null_integer_async(const Expr& source) {
        std::vector<std::pair<NameKey, TypePtr>> locals;
        for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                locals.emplace_back(name, cell.value.type);
        const auto value = co_await probe_integer_async(source, locals);
        if (value && value->value == UInt128{}) co_return value;
        co_return std::nullopt;
    }

    EvaluationTask<bool> retain_null_integer_async(Expr& source) {
        source.evaluated_integer = (co_await null_integer_async(source));
        co_return source.evaluated_integer.has_value();
    }

    TypePtr call_signature(const Expr& node) {
        auto type = node.left ? expression_type(*node.left) : TypePtr{};
        if (type && type->kind == Type::Kind::Pointer) type = type->pointee;
        return type && type->kind == Type::Kind::Function && type->function ? type : TypePtr{};
    }

    EvaluationTask<bool> validate_call_arguments_async(const Expr& node, const FunctionType& signature) {
        if (node.arguments.size() < signature.parameters.size() ||
            (!signature.variadic && node.arguments.size() != signature.parameters.size())) {
            fail(node.location, "function call has an invalid argument count");
            co_return false;
        }
        for (std::size_t index = 0; index < node.arguments.size(); ++index) {
            const auto& argument = *node.arguments[index];
            const auto from = expression_type(argument);
            if (index >= signature.parameters.size()) {
                if (is_meta_type(from)) {
                    fail(argument.location, "meta values cannot enter variadic arguments");
                    co_return false;
                }
                if (const auto* reason = source_value_error(from)) {
                    fail(argument.location, std::string(reason) + " in variadic argument");
                    co_return false;
                }
                continue;
            }
            const auto& parameter = signature.parameters[index];
            const auto& to = parameter.type;
            if (parameter.mode != ParameterMode::Out)
                if (const auto* reason = source_value_error(from)) {
                    fail(argument.location, std::string(reason) + " in call argument");
                    co_return false;
                }
            const auto pointer_compatible = [&]() -> EvaluationTask<bool> {
                if (!from || !to || from->kind != Type::Kind::Pointer ||
                    to->kind != Type::Kind::Pointer || !from->pointee || !to->pointee)
                    co_return false;
                if (from->address_space != to->address_space) co_return false;
                if (pointer_resolver_ && direct_function(argument))
                    co_return (co_await resolve_pointer_async(clone_expr(argument), to)).has_value();
                co_return compare_pointee(from->pointee, to->pointee) != PointeeCompatibility::Incompatible;
            };
            bool compatible = from && to &&
                (((is_integer(from) || is_floating(from)) &&
                  (is_integer(to) || is_floating(to))) ||
                 (is_meta_type(from) && from->kind == to->kind) ||
                 (from->kind == Type::Kind::Pointer && to->kind == Type::Kind::Builtin &&
                  to->builtin == BuiltinType::Bool) ||
                 compare_source_types(callable_parameter_type(from, parameter.mode),
                           callable_parameter_type(to, parameter.mode)) != TypeComparison::Different);
            if (!compatible && from && to) compatible = co_await pointer_compatible();
            if (!compatible && from && to && is_integer(from) && to->kind == Type::Kind::Pointer)
                compatible = (co_await null_integer_async(argument)).has_value();
            if (!compatible && from && to)
                compatible = is_vector(to) && !to->scalable &&
                    ((is_vector(from) && !from->scalable && compatible_vector_shape(from, to) &&
                      (is_integer(from->element) || is_floating(from->element))) ||
                     is_integer(from) || is_floating(from));
            if (resource_exhausted_ || context_unavailable_) co_return false;
            // An out actual supplies no input value. Its source expression
            // still has to be valid; a modifiable lvalue also receives copy-out.
            if (!compatible && (parameter.mode != ParameterMode::Out ||
                                is_meta_type(from) || is_meta_type(to))) {
                fail(argument.location, "unsupported or incompatible argument type in required expression");
                co_return false;
            }
            const auto object = expression_type(argument, false);
            if (parameter.mode != ParameterMode::In && object && !object->is_const &&
                object->kind != Type::Kind::Array && object->kind != Type::Kind::Function &&
                translation_lvalue(argument)) {
                if ((to->kind == Type::Kind::Record || object->kind == Type::Kind::Record) &&
                    (to->kind != object->kind || to->is_union != object->is_union ||
                     to->nominal_key() != object->nominal_key())) {
                    fail(argument.location, "a record value requires the same nominal record type in parameter copy-out");
                    co_return false;
                }
                if (const auto* reason = source_conversion_error(to, object)) {
                    fail(argument.location, std::string(reason) + " in parameter copy-out");
                    co_return false;
                }
            }
        }
        co_return true;
    }

    EvaluationTask<bool> validate_unevaluated_constraints_async(const Expr& node, bool ordinary_names = true,
                                                               SourcePatchOperand patch_operand = {},
                                                               bool direct_callee = false,
                                                               SourceExpressionUse use = SourceExpressionUse::Value) {
        if (const auto issue = source_patch_intrinsic_error(node, program_, current_function_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (const auto issue = source_staging_intrinsic_error(node,
                [&](const Expr& operand) { return expression_type(operand); })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (ordinary_names) {
            if (const auto reason = source_name_error(node, program_, current_function_,
                    [&](const Expr& operand) { return expression_type(operand, false); }, direct_callee)) {
                fail(node.location, *reason);
                co_return false;
            }
        }
        if (const auto* reason = source_meta_expression_error(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(node.location, reason);
            co_return false;
        }
        if (const auto issue = source_object_expression_error(node, program_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand) { return translation_lvalue(operand); }, use)) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (!(co_await validate_constant_lane_index_async(node))) co_return false;
        if (const auto* reason = source_expression_conversion_error(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(node.location, reason);
            co_return false;
        }
        if (const auto* reason = co_await source_operator_error_async(node, program_,
                [&](const Expr& operand) { return expression_type(operand); },
                [&](Expr& operand) -> EvaluationTask<bool> {
                    co_return co_await retain_null_integer_async(operand);
                })) {
            fail(node.location, reason);
            co_return false;
        }
        if (resource_exhausted_ || context_unavailable_) co_return false;
        if (node.left && !(co_await validate_unevaluated_constraints_async(*node.left, ordinary_names,
                node.kind == Expr::Kind::Parenthesized ? patch_operand : SourcePatchOperand{},
                node.kind == Expr::Kind::Call || (direct_callee && node.kind == Expr::Kind::Parenthesized),
                source_left_use(node, use)))) co_return false;
        const bool member = node.kind == Expr::Kind::Binary &&
            (node.text == "member" || node.text == "pointer_member");
        if (node.right && !member && !(co_await validate_unevaluated_constraints_async(*node.right, ordinary_names))) co_return false;
        if (node.third && !(co_await validate_unevaluated_constraints_async(*node.third, ordinary_names))) co_return false;
        std::vector<EvaluationInstructionOperand> instruction_operands(source_instruction_call(node) ? node.arguments.size() : 0);
        for (std::size_t index = 0; index < node.arguments.size(); ++index) {
            const auto& argument = node.arguments[index];
            if (!(co_await validate_unevaluated_constraints_async(*argument,
                    ordinary_names && source_argument_names(node, *argument),
                    {source_instruction_call(node) ? &node : nullptr, index,
                        source_instruction_call(node) ? &instruction_operands[index] : nullptr}, false,
                    source_argument_use(node, index, [&](const Expr& operand) { return expression_type(operand); })))) co_return false;
        }
        for (const auto& entry : node.initializer_entries)
            if (entry.value && !(co_await validate_unevaluated_constraints_async(*entry.value, ordinary_names))) co_return false;
        if (node.kind == Expr::Kind::Quote) {
            if (const auto* argument = invalid_unquote_argument(node,
                    [&](const Expr& operand) { return expression_type(operand); })) {
                fail(argument->location, "$::unquote requires a token value or syntax node");
                co_return false;
            }
        } else if (node.kind == Expr::Kind::Sizeof || node.kind == Expr::Kind::Alignof) {
            const auto queried = node.type ? node.type
                : node.left ? expression_type(*node.left, false) : nullptr;
            if (const auto* reason = source_layout_query_error(node, queried, program_,
                    [&](const Expr& operand) { return expression_type(operand, false); })) {
                fail(node.location, reason);
                co_return false;
            }
            if (!queried) {
                fail(node.location, "layout query has an unresolved expression type");
                co_return false;
            }
        } else if (node.kind == Expr::Kind::Offsetof) {
            if (const auto issue = source_offsetof_error(node, program_)) {
                fail(issue->location, issue->message);
                co_return false;
            }
        }
        if (!(co_await validate_control_intrinsic_async(node))) co_return false;
        if (!(co_await validate_atomic_intrinsic_async(node))) co_return false;
        if (!(co_await validate_patch_constraints_async(node, patch_operand))) co_return false;
        if (source_instruction_call(node)) {
            std::vector<std::pair<NameKey, TypePtr>> locals;
            for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
                for (const auto& [name, cell] : scopes_[scope]) locals.emplace_back(name, cell.value.type);
            if (const auto issue = co_await source_instruction_error_async(node, program_, current_function_,
                    [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                    [&](const Expr& operand) { return translation_lvalue(operand); },
                    [&](const Expr& operand) -> EvaluationTask<SourceConstantProbe> {
                        SourceConstantProbe result;
                        result.value = co_await probe_integer_async(operand, locals, nullptr, true);
                        co_return result;
                    }, instruction_operands)) {
                fail(issue->location, issue->message);
                co_return false;
            }
            co_return !resource_exhausted_ && !context_unavailable_;
        }
        if (node.kind == Expr::Kind::Call) {
            MetaIntrinsicArgumentValidator validator(
                [&](const Expr& operand) { return expression_type(operand); },
                [&](SourceLocation location, std::string message) { fail(location, std::move(message)); });
            if (validator.intrinsic(node) && !validator.valid()) co_return false;
        }
        if (node.kind == Expr::Kind::Call && node.left &&
            !(node.left->kind == Expr::Kind::Name && node.left->text.starts_with("$::"))) {
            const auto signature = call_signature(node);
            if (!signature) {
                fail(node.location, "called expression does not have a function type");
                co_return false;
            }
            if (!(co_await validate_call_arguments_async(node, *signature->function))) co_return false;
        }
        co_return true;
    }

    EvaluationTask<bool> validate_required_tree_async(const Expr& node, bool direct_callee = false,
                                                     SourceExpressionUse use = SourceExpressionUse::Value) {
        if (const auto issue = source_patch_intrinsic_error(node, program_, current_function_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (const auto issue = source_staging_intrinsic_error(node,
                [&](const Expr& operand) { return expression_type(operand); })) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (const auto reason = source_name_error(node, program_, current_function_,
                [&](const Expr& operand) { return expression_type(operand, false); }, direct_callee)) {
            fail(node.location, *reason);
            co_return false;
        }
        if (const auto* reason = source_meta_expression_error(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(node.location, reason);
            co_return false;
        }
        if (const auto issue = source_object_expression_error(node, program_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); },
                [&](const Expr& operand) { return translation_lvalue(operand); }, use)) {
            fail(issue->location, issue->message);
            co_return false;
        }
        if (!(co_await validate_constant_lane_index_async(node))) co_return false;
        if (const auto* reason = source_expression_conversion_error(node,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(node.location, reason);
            co_return false;
        }
        if (const auto* reason = co_await source_operator_error_async(node, program_,
                [&](const Expr& operand) { return expression_type(operand); },
                [&](Expr& operand) -> EvaluationTask<bool> {
                    co_return co_await retain_null_integer_async(operand);
                })) {
            fail(node.location, reason);
            co_return false;
        }
        if (resource_exhausted_ || context_unavailable_) co_return false;
        if (node.kind == Expr::Kind::Quote) {
            for (const auto& argument : node.arguments)
                if (!(co_await validate_required_tree_async(*argument))) co_return false;
            if (const auto* argument = invalid_unquote_argument(node,
                    [&](const Expr& operand) { return expression_type(operand); })) {
                fail(argument->location, "$::unquote requires a token value or syntax node");
                co_return false;
            }
            co_return true;
        }
        if (node.kind == Expr::Kind::Sizeof ||
            node.kind == Expr::Kind::Alignof) {
            if (node.left && !(co_await validate_unevaluated_constraints_async(*node.left, true, {}, false,
                    SourceExpressionUse::Designator))) co_return false;
            const auto queried = node.type ? node.type
                : node.left ? expression_type(*node.left, false) : nullptr;
            if (queried && (queried->kind == Type::Kind::Bytes ||
                            queried->kind == Type::Kind::Buffer)) {
                fail(node.location, type_name(queried) + " has no runtime size or alignment");
                co_return false;
            }
            if (procedural_) {
                const auto type = node.type ? node.type
                    : node.left ? expression_type(*node.left, false) : nullptr;
                if (!type || contains_tokens(type)) {
                    fail(node.location, "layout query requires a runtime object type, not translation-only meta values");
                    co_return false;
                }
            }
            if (const auto* reason = source_layout_query_error(node, queried, program_,
                    [&](const Expr& operand) { return expression_type(operand, false); })) {
                fail(node.location, reason);
                co_return false;
            }
            if (node.type) co_return true;
            if (!node.left || !expression_type(*node.left)) {
                fail(node.location,
                     "layout query has an unresolved expression type");
                co_return false;
            }
            co_return true;
        }
        if (node.kind == Expr::Kind::Offsetof) {
            if (const auto issue = source_offsetof_error(node, program_)) {
                fail(issue->location, issue->message);
                co_return false;
            }
            for (const auto& designator : node.initializer_entries.front().designators)
                if (designator.index && !(co_await validate_required_tree_async(*designator.index)))
                    co_return false;
            co_return true;
        }
        if (node.kind == Expr::Kind::Name && is_label_type(expression_type(node)) &&
            ((node.name_context && node.name_context->label_address) || !translation_lvalue(node))) {
            if (const auto address = resolve_label_constant(node, current_function_, program_, nullptr,
                    [&](SourceLocation at) { return step(at); })) {
                if (const auto* reason = label_address_error(*address)) {
                    fail(node.location, reason);
                    co_return false;
                }
            }
        }
        if (control_intrinsic(node) != ControlIntrinsic::None || atomic_builtin(node) != AtomicBuiltin::None ||
            patch_intrinsic(node) || source_instruction_call(node))
            co_return co_await validate_unevaluated_constraints_async(node, true, {}, false, use);
        if (node.kind == Expr::Kind::Call) {
            if (!node.left || node.left->kind != Expr::Kind::Name ||
                (!node.left->text.starts_with("$::") && !direct_function(*node.left))) {
                // Type-check even an unselected call without executing its
                // callee. The evaluator's call entry enforces the stage rule.
                const auto signature = call_signature(node);
                if (!signature) {
                    fail(node.location, node.left && expression_type(*node.left)
                        ? "called expression does not have a function type"
                        : "required expression has an unresolved call or invalid argument count");
                    co_return false;
                }
                if (!(co_await validate_required_tree_async(*node.left, true))) co_return false;
                for (std::size_t index = 0; index < node.arguments.size(); ++index)
                    if (!(co_await validate_required_tree_async(*node.arguments[index], false,
                            source_argument_use(node, index,
                                [&](const Expr& operand) { return expression_type(operand); })))) co_return false;
                co_return (co_await validate_call_arguments_async(node, *signature->function));
            }
            const auto& name = node.left->text;
            if (name.starts_with("$::syntax::") || name == "$::meta::error" ||
                name == "$::meta::warning" || name == "$::meta::note") {
                const bool diagnostic = name == "$::syntax::error" ||
                    name == "$::syntax::warning" || name == "$::syntax::note" ||
                    name == "$::meta::error" || name == "$::meta::warning" || name == "$::meta::note";
                const bool context = name == "$::syntax::context";
                const auto count = name == "$::syntax::input" || name == "$::syntax::span" || context ? 1U
                    : name == "$::syntax::capture" || name == "$::syntax::node" ||
                      name == "$::syntax::count" ||
                      name == "$::syntax::is_variant" || name == "$::syntax::capture_span" || diagnostic ? 2U
                    : name == "$::syntax::at" ? 3U : 0U;
                if (count == 0 || node.arguments.size() != count) {
                    fail(node.location, "unsupported syntax operation or invalid argument count: " + name);
                    co_return false;
                }
                for (const auto& argument : node.arguments)
                    if (!(co_await validate_required_tree_async(*argument))) co_return false;
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
                    co_return false;
                }
                co_return true;
            }
            if (name == "$::meta::tokens" || name == "$::meta::node_span" || name == "$::meta::child_count" ||
                name == "$::meta::child" || name == "$::meta::is_kind" ||
                name == "$::meta::is_production" || name == "$::meta::is_extension" ||
                name == "$::meta::replace_child" ||
                name == "$::meta::extension_match") {
                const auto count = name == "$::meta::tokens" || name == "$::meta::node_span" ||
                    name == "$::meta::child_count" || name == "$::meta::extension_match" ? 1U :
                    name == "$::meta::replace_child" ? 3U : 2U;
                if (node.arguments.size() != count) {
                    fail(node.location, "unsupported syntax-tree operation or invalid argument count: " + name);
                    co_return false;
                }
                for (const auto& argument : node.arguments)
                    if (!(co_await validate_required_tree_async(*argument))) co_return false;
                const auto first = expression_type(*node.arguments[0]);
                if (!first || (first->kind != Type::Kind::Syntax &&
                    !(name == "$::meta::is_kind" && first->kind == Type::Kind::Tokens))) {
                    fail(node.arguments[0]->location, name + (name == "$::meta::is_kind"
                        ? " requires $::meta::syntax or $::meta::tokens" : " requires $::meta::syntax"));
                    co_return false;
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
                        co_return false;
                    }
                }
                if (count == 3) {
                    const auto third = expression_type(*node.arguments[2]);
                    if (!third || third->kind != Type::Kind::Syntax) {
                        fail(node.arguments[2]->location,
                            "$::meta::replace_child requires a syntax replacement node");
                        co_return false;
                    }
                }
                co_return true;
            }
            if (name == "$::meta::token" || name == "$::meta::group") {
                if (node.arguments.size() != 2 && node.arguments.size() != 3) {
                    fail(node.location, name + " requires two arguments and an optional source span");
                    co_return false;
                }
                for (std::size_t at = 0; at < node.arguments.size(); ++at) {
                    const auto& argument = *node.arguments[at];
                    if (!(co_await validate_required_tree_async(argument))) co_return false;
                    const auto type = expression_type(argument);
                    const bool string = type && type->kind == Type::Kind::Pointer && type->pointee &&
                        type->pointee->kind == Type::Kind::Builtin && type->pointee->builtin == BuiltinType::U8;
                    const bool valid = at == 0 ? string : at == 2 ? type && type->kind == Type::Kind::Span
                        : name == "$::meta::group" ? type && type->kind == Type::Kind::Tokens
                        : string || (type && type->kind == Type::Kind::Bytes);
                    if (!valid) {
                        fail(argument.location, "incompatible argument type for " + name);
                        co_return false;
                    }
                }
                co_return true;
            }
            if (name == "$::meta::spelling" || name == "$::meta::children" ||
                name == "$::meta::delimiter" || name == "$::meta::span") {
                if (node.arguments.size() != 1) {
                    fail(node.location, name + " requires one token value");
                    co_return false;
                }
                const auto& argument = *node.arguments.front();
                if (!(co_await validate_required_tree_async(argument))) co_return false;
                const auto type = expression_type(argument);
                if (!type || type->kind != Type::Kind::Tokens) {
                    fail(argument.location, name + " requires $::meta::tokens");
                    co_return false;
                }
                co_return true;
            }
            if (name == "$::embed") {
                if (node.arguments.size() != 1 ||
                    node.arguments.front()->kind != Expr::Kind::String ||
                    !token_origin(node.left->location).embed) {
                    fail(node.location, "$::embed requires one identified string-literal path");
                    co_return false;
                }
                co_return true;
            }
            if (name == "$::meta::call_site") {
                if (node.arguments.size() != 1U) {
                    fail(node.location, "$::meta::call_site requires one token value");
                    co_return false;
                }
                const auto& argument = *node.arguments.front();
                if (!(co_await validate_required_tree_async(argument))) co_return false;
                const auto type = expression_type(argument);
                if (!type || type->kind != Type::Kind::Tokens) {
                    fail(argument.location, "$::meta::call_site requires one identifier token value");
                    co_return false;
                }
                co_return true;
            }
            if (name == "$::meta::gensym") {
                if (node.arguments.size() != 1U) {
                    fail(node.location, "$::meta::gensym requires one string prefix");
                    co_return false;
                }
                const auto& argument = *node.arguments.front();
                if (!(co_await validate_required_tree_async(argument))) co_return false;
                const auto type = expression_type(argument);
                if (!type || type->kind != Type::Kind::Pointer ||
                    !type->pointee || type->pointee->kind != Type::Kind::Builtin ||
                    type->pointee->builtin != BuiltinType::U8) {
                    fail(argument.location, "$::meta::gensym requires a translation-time string prefix");
                    co_return false;
                }
                co_return true;
            }
            if (name == "$::meta::len" || name == "$::meta::at" ||
                name == "$::meta::slice" || name == "$::meta::concat") {
                const auto count = name == "$::meta::len" ? 1U
                    : name == "$::meta::at" || name == "$::meta::concat" ? 2U : 3U;
                if (node.arguments.size() != count) {
                    fail(node.location, name + " requires " + std::to_string(count) + " arguments");
                    co_return false;
                }
                if (!(co_await validate_required_tree_async(*node.arguments.front()))) co_return false;
                const auto sequence = expression_type(*node.arguments.front());
                if (!sequence || (sequence->kind != Type::Kind::Bytes &&
                                  sequence->kind != Type::Kind::Tokens)) {
                    fail(node.arguments.front()->location,
                        name + " requires $::meta::bytes or $::meta::tokens");
                    co_return false;
                }
                for (std::size_t index = 1; index < count; ++index) {
                    const auto& argument = *node.arguments[index];
                    if (!(co_await validate_required_tree_async(argument))) co_return false;
                    const auto type = expression_type(argument);
                    const bool sequence_argument = name == "$::meta::concat";
                    if (!type || (sequence_argument
                            ? type->kind != sequence->kind : !is_integer(type))) {
                        fail(argument.location, name +
                            (sequence_argument ? (sequence->kind == Type::Kind::Tokens
                                    ? " requires token values"
                                    : " requires two values of the same meta-sequence type")
                                               : " requires an integer index"));
                        co_return false;
                    }
                }
                co_return true;
            }
            if (name == "$::meta::alloc" || name == "$::meta::cap" ||
                name == "$::meta::freeze") {
                const auto count = name == "$::meta::freeze" ? 2U : 1U;
                if (node.arguments.size() != count) {
                    fail(node.location, name + " requires " +
                        std::to_string(count) + " arguments");
                    co_return false;
                }
                for (std::size_t index = 0; index < count; ++index) {
                    const auto& argument = *node.arguments[index];
                    if (!(co_await validate_required_tree_async(argument))) co_return false;
                    const auto type = expression_type(argument);
                    const bool integer = name == "$::meta::alloc" || index == 1;
                    if (!type || (integer ? !is_integer(type)
                                          : type->kind != Type::Kind::Buffer)) {
                        fail(argument.location, name +
                            (integer ? " requires an integer capacity or length"
                                     : " requires $::meta::buffer"));
                        co_return false;
                    }
                }
                co_return true;
            }
            if (name == "$::meta::data") {
                if (node.arguments.size() != 1U ||
                    !(co_await validate_required_tree_async(*node.arguments.front()))) {
                    fail(node.location, "$::meta::data requires one meta-byte value");
                    co_return false;
                }
                const auto type = expression_type(*node.arguments.front());
                if (!type || (type->kind != Type::Kind::Bytes &&
                              type->kind != Type::Kind::Buffer)) {
                    fail(node.arguments.front()->location,
                        "$::meta::data requires $::meta::bytes or $::meta::buffer");
                    co_return false;
                }
                co_return true;
            }
            if (name.starts_with("$::meta::") &&
                name != "$::meta::parse") {
                fail(node.location, name + " is not implemented for byte evaluation");
                co_return false;
            }
            if (node.left->text == "$::meta::parse") {
                if (node.arguments.size() != 1U && node.arguments.size() != 3U) {
                    fail(node.location, "$::meta::parse requires a string or category, tokens, and context");
                    co_return false;
                }
                for (std::size_t at = 0; at < node.arguments.size(); ++at) {
                    const auto& argument = node.arguments[at];
                    if (!(co_await validate_required_tree_async(*argument))) co_return false;
                    const auto type = expression_type(*argument);
                    const bool string = type && type->kind == Type::Kind::Pointer &&
                        type->pointee && type->pointee->kind == Type::Kind::Builtin &&
                        type->pointee->builtin == BuiltinType::U8;
                    const bool bytes = node.arguments.size() == 1U && type && type->kind == Type::Kind::Bytes;
                    if (at == 0 ? !string && !bytes : !type || type->kind !=
                        (at == 1 ? Type::Kind::Tokens : Type::Kind::Context)) {
                        fail(argument->location, node.arguments.size() == 1U
                            ? "$::meta::parse requires a translation-time string or bytes"
                            : "$::meta::parse requires a string or category string, tokens, and context");
                        co_return false;
                    }
                }
                co_return true;
            }
            if (node.left->text == "$::runtime") {
                // Only evaluating it is invalid, which the evaluator reports;
                // an unselected occurrence just has to be well formed.
                co_return (co_await validate_unevaluated_constraints_async(*node.arguments.front()));
            }
            if (node.left->text == "$::eval") {
                if (node.arguments.size() != 1) {
                    fail(node.location, "$::eval requires exactly one expression");
                    co_return false;
                }
                co_return (co_await validate_required_tree_async(*node.arguments.front()));
            }
            const auto* function = resolve_function(program_, current_function_, *node.left,
                                                     [](const FunctionDecl&) { return true; });
            if (!function) {
                fail(node.location, "required expression has an unresolved call or invalid argument count");
                co_return false;
            }
            for (std::size_t index = 0; index < node.arguments.size(); ++index)
                if (!(co_await validate_required_tree_async(*node.arguments[index], false,
                        source_argument_use(node, index,
                            [&](const Expr& operand) { return expression_type(operand); })))) co_return false;
            const auto signature = source_function_type(*function);
            co_return (co_await validate_call_arguments_async(node, *signature->function));
        }
        if (node.left && !(co_await validate_required_tree_async(*node.left,
                direct_callee && node.kind == Expr::Kind::Parenthesized, source_left_use(node, use)))) co_return false;
        const bool member = node.kind == Expr::Kind::Binary &&
            (node.text == "member" || node.text == "pointer_member");
        if (node.right && !member && !(co_await validate_required_tree_async(*node.right))) co_return false;
        if (node.third && !(co_await validate_required_tree_async(*node.third))) co_return false;
        // A well-formed grouping has its already-validated child's type/use.
        // Reclassifying every parent would scan all suffixes of a deep chain.
        if (node.kind == Expr::Kind::Parenthesized && node.left) co_return true;
        if (node.kind == Expr::Kind::Integer) co_return (co_await expression_async(node)).has_value();
        if (node.kind == Expr::Kind::Floating) co_return (co_await expression_async(node)).has_value();
        if (node.kind == Expr::Kind::Character) co_return (co_await expression_async(node)).has_value();
        if (node.kind == Expr::Kind::String) {
            if (decode_string_literal(node.text)) co_return true;
            fail(node.location, "invalid string in required expression");
            co_return false;
        }
        if (node.kind == Expr::Kind::Conditional) {
            const auto condition = expression_type(*node.left);
            const auto yes = expression_type(*node.right);
            const auto no = expression_type(*node.third);
            if (is_label_type(yes) || is_label_type(no)) {
                if (is_label_type(yes) && is_label_type(no) && is_scalar(condition)) co_return true;
                fail(node.location, "conditional label operands must both have label type and a scalar selector");
                co_return false;
            }
            if (is_meta_type(yes) || is_meta_type(no)) {
                if (!conditional_meta_type(yes, no)) {
                    fail(node.location, "conditional meta operands must have the same type");
                    co_return false;
                }
                if (is_scalar(condition)) co_return true;
                fail(node.location, "conditional meta selector must be scalar");
                co_return false;
            }
        }
        if (!expression_type(node)) {
            fail(node.location, "unresolved name or unsupported type in required constant expression");
            co_return false;
        }
        if (procedural_ && node.kind == Expr::Kind::Cast && node.left &&
            !(co_await macro_convertible_expression_async(*node.left, node.type, true))) {
            fail(node.location, "unsupported conversion in procedural macro");
            co_return false;
        }
        const bool pointer_unary = pointer_resolver_ && node.kind == Expr::Kind::Unary &&
            node.left && !is_meta_type(expression_type(*node.left)) &&
            (node.text == "&" || (node.text == "*" &&
                expression_type(*node.left)->kind == Type::Kind::Pointer) ||
             ((node.text == "++" || node.text == "--" || node.text == "post++" || node.text == "post--") &&
              expression_type(*node.left)->kind == Type::Kind::Pointer && meta_pointer_source(*node.left)));
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
            co_return false;
        }
        const bool modifying = node.kind == Expr::Kind::Assign ||
            (node.kind == Expr::Kind::Unary &&
             (node.text == "++" || node.text == "--" || node.text.starts_with("post")));
        if (modifying) {
            const auto destination = node.left ? expression_type(*node.left, false) : nullptr;
            const bool procedural_assignment = procedural_ && node.kind == Expr::Kind::Assign &&
                node.text == "=" && destination &&
                node.right && (co_await macro_convertible_expression_async(*node.right, destination));
            const bool label_assignment = node.kind == Expr::Kind::Assign && node.text == "=" &&
                is_label_type(destination) && node.right && is_label_type(expression_type(*node.right));
            if (procedural_ && node.kind == Expr::Kind::Assign && node.right &&
                !(co_await macro_convertible_expression_async(*node.right, destination))) {
                fail(node.location, "incompatible assignment type in procedural macro");
                co_return false;
            }
            if (!node.left || !translation_lvalue(*node.left) ||
                (node.right && !procedural_assignment && !label_assignment && !(is_integer(expression_type(*node.right)) ||
                                 is_floating(expression_type(*node.right))))) {
                fail(node.location, "unsupported assignment in required scalar expression");
                co_return false;
            }
            bool read_only = destination && destination->is_const;
            if (const auto* cell = lookup_mutable(*node.left)) {
                read_only = read_only || cell->read_only;
            } else if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (name_key(parameter) == name_key(*node.left))
                        read_only = read_only || parameter.type->is_const;
            }
            if (read_only) {
                fail(node.location, "cannot write a const cell");
                co_return false;
            }
        }
        if (node.kind == Expr::Kind::Binary || node.kind == Expr::Kind::Conditional) {
            if (member && (pointer_resolver_ ||
                           (selected_record_member(node) && node.left &&
                            meta_pointer_source(*node.left))))
                co_return true;
            const auto left = node.left ? expression_type(*node.left) : nullptr;
            const auto right = node.right ? expression_type(*node.right) : nullptr;
            const bool indexing = node.kind == Expr::Kind::Binary && node.text == "index";
            const bool label_comparison = node.kind == Expr::Kind::Binary &&
                (node.text == "==" || node.text == "!=") && is_label_type(left) && is_label_type(right);
            const bool null_comparison = node.kind == Expr::Kind::Binary &&
                (node.text == "==" || node.text == "!=") && left && right &&
                ((left->kind == Type::Kind::Pointer && is_integer(right)) ||
                 (right->kind == Type::Kind::Pointer && is_integer(left)));
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
            const bool value_conditional = node.kind == Expr::Kind::Conditional && result_type &&
                (is_scalar(result_type) || result_type->kind == Type::Kind::Record || is_vector(result_type)) && left &&
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
                            !meta_pointer_pair && !vector_operation && !value_conditional && !label_comparison && !null_comparison &&
                            (!scalar || (floating_operands && !floating_operator)))) {
                fail(node.location, "unsupported operation in required scalar expression");
                co_return false;
            }
            if (node.kind == Expr::Kind::Conditional &&
                (!node.third || !expression_type(*node.third))) {
                fail(node.location, "unresolved conditional operand in required expression");
                co_return false;
            }
        }
        co_return true;
    }

    static bool macro_convertible(const TypePtr& from, const TypePtr& to, bool explicit_cast = false,
                                   bool direct_function = false) {
        if (!from || !to) return false;
        if (is_label_type(from) || is_label_type(to)) {
            if (is_label_type(from) && is_label_type(to)) return true;
            const auto& other = is_label_type(from) ? to : from;
            return explicit_cast && other->kind == Type::Kind::Builtin &&
                other->builtin == BuiltinType::Uptr;
        }
        if (is_meta_type(from) || is_meta_type(to))
            return from->kind == to->kind;
        if ((is_integer(from) || is_floating(from)) && (is_integer(to) || is_floating(to)))
            return true;
        if (from->kind == Type::Kind::Pointer && to->kind == Type::Kind::Builtin &&
            to->builtin == BuiltinType::Bool) return true;
        if (explicit_cast && is_integer(from) && to->kind == Type::Kind::Pointer) return true;
        if (to->kind == Type::Kind::Record && from->kind == Type::Kind::Record) {
            auto a = without_alignment(from), b = without_alignment(to);
            a->is_const = b->is_const = false;
            return same_type(a, b);
        }
        if (is_vector(to) && !to->scalable) {
            const auto element = is_vector(from) ? from->element : from;
            return (!is_vector(from) || (!from->scalable && from->lanes == to->lanes)) &&
                (is_integer(element) || is_floating(element)) &&
                (is_integer(to->element) || is_floating(to->element));
        }
        if (from->kind == Type::Kind::Pointer && to->kind == Type::Kind::Pointer &&
            from->pointee && to->pointee && from->pointee->kind == Type::Kind::Function &&
            to->pointee->kind == Type::Kind::Function)
            return from->address_space == to->address_space &&
                !source_conversion_error(from, to,
                    explicit_cast ? SourceConversion::Explicit : SourceConversion::Implicit,
                    direct_function);
        return from->kind == Type::Kind::Pointer && to->kind == Type::Kind::Pointer &&
            from->address_space == to->address_space &&
            (compatible_pointee(from->pointee, to->pointee) ||
             (explicit_cast && from->pointee && to->pointee &&
              (!meta_leaf_const(from->pointee) || meta_leaf_const(to->pointee))));
    }

    EvaluationTask<bool> macro_convertible_expression_async(const Expr& source, const TypePtr& destination,
                                      bool explicit_cast = false) {
        const auto from = expression_type(source);
        if (!explicit_cast && destination && destination->kind == Type::Kind::Pointer && is_integer(from))
            co_return (co_await null_integer_async(source)).has_value();
        co_return macro_convertible(from, destination, explicit_cast,
            source_function_designator(source,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); }));
    }

    bool translation_lvalue(const Expr& node) {
        return source_object_lvalue(node,
            [&](const Expr& expression) { return expression_type(expression); },
            [&](const Expr& name) {
                if (lookup_mutable(name) || resolve_object(program_, current_function_, name)) return true;
                return current_function_ && std::any_of(current_function_->parameters.begin(),
                    current_function_->parameters.end(),
                    [&](const ParameterDecl& parameter) { return name_key(parameter) == name_key(name); });
            });
    }


    bool meta_pointer_source(const Expr& source) {
        std::vector<const Expr*> work{&source};
        while (!work.empty()) {
            const auto* selected = work.back();
            work.pop_back();
            if (selected->kind == Expr::Kind::Call && selected->left &&
                selected->left->kind == Expr::Kind::Name &&
                selected->left->text == "$::meta::data") return true;
            if (selected->kind == Expr::Kind::Name) {
                const auto* cell = lookup_mutable(*selected);
                if (cell && (cell->value.meta_pointer.has_value() ||
                             cell->value.object != nullptr || cell->storage != nullptr)) return true;
                continue;
            }
            if (selected->kind == Expr::Kind::Unary && selected->text == "&" && selected->left) {
                const auto* object = selected->left.get();
                while (object->kind == Expr::Kind::Parenthesized && object->left) object = object->left.get();
                // Address formation can create local scalar backing. This is
                // classification only; execution still rejects type-only cells.
                if (object->kind == Expr::Kind::Name) {
                    const auto* cell = lookup_mutable(*object);
                    if (cell && meta_object_type(cell->value.type)) return true;
                } else work.push_back(object);
                continue;
            }
            if (selected->kind == Expr::Kind::Parenthesized || selected->kind == Expr::Kind::Cast ||
                (selected->kind == Expr::Kind::Unary && selected->text == "*") ||
                (selected->kind == Expr::Kind::Binary && (selected->text == "index" ||
                    selected->text == "member" || selected->text == "pointer_member"))) {
                if (selected->left) work.push_back(selected->left.get());
            } else if (selected->kind == Expr::Kind::Binary &&
                       (selected->text == "+" || selected->text == "-")) {
                if (selected->right) work.push_back(selected->right.get());
                if (selected->left) work.push_back(selected->left.get());
            }
        }
        return false;
    }

    template<class TypeOf>
    TypePtr expression_type_step(const Expr& expression, bool decay, const TypeOf& type_of) {
        switch (expression.kind) {
        case Expr::Kind::VoidValue: return builtin_type(BuiltinType::Void);
        case Expr::Kind::Quote: return tokens_type();
        case Expr::Kind::Integer: {
            const auto value = parse_integer_value(expression);
            return value ? value->type : nullptr;
        }
        case Expr::Kind::Character: return builtin_type(BuiltinType::U32);
        case Expr::Kind::Name:
            if (expression.deferred_generic_signature)
                return decay ? pointer_type(expression.deferred_generic_signature) : expression.deferred_generic_signature;
            if (expression.name_context && expression.name_context->label_address)
                return builtin_type(BuiltinType::Label);
            if (const auto* cell = lookup_mutable(expression)) {
                if (decay && cell->value.type->kind == Type::Kind::Array)
                    return pointer_type(qualified_element_type(cell->value.type));
                return cell->value.type;
            }
            if (current_function_) {
                for (const auto& parameter : current_function_->parameters)
                    if (name_key(parameter) == name_key(expression)) return parameter.type;
                for (const auto& attribute : current_function_->attributes)
                    for (const auto& binding : attribute.variadic_bindings)
                        if (name_key(binding) == name_key(expression)) return binding.type;
            }
            if (const auto* object = resolve_object(program_, current_function_, expression)) {
                if (pointer_resolver_ && decay && object->type->kind == Type::Kind::Array)
                    return pointer_type(qualified_element_type(object->type));
                return object->type;
            }
            {
                if (const auto* function = resolve_function(program_, current_function_, expression,
                        [](const FunctionDecl&) { return true; })) {
                    auto type = source_function_type(*function);
                    if (!canonicalize_callable_abis(type, program_.canonical_callable_abi)) return {};
                    return decay ? pointer_type(type) : type;
                }
            }
            if (const auto found = resolve_enumerator(
                    program_, current_function_, current_namespace_,
                    expression);
                found && (found->enumerator->value || program_.evaluation_prepare_enumerator)) {
                return enum_type(*found->enumeration);
            }
            if (resolve_label_constant(expression, current_function_, program_, nullptr,
                    [&](SourceLocation at) { return step(at); }))
                return builtin_type(BuiltinType::Label);
            return {};
        case Expr::Kind::Parenthesized:
            return expression.left ? type_of(*expression.left, decay) : nullptr;
        case Expr::Kind::Address:
            return expression.type;
        case Expr::Kind::Cast:
            return expression.type;
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
        case Expr::Kind::Offsetof:
            return builtin_type(BuiltinType::Uptr);
        case Expr::Kind::Assign:
            return type_of(*expression.left);
        case Expr::Kind::Unary: {
            auto type = type_of(*expression.left, expression.text != "&");
            if (!type) return {};
            if (expression.text == "&") {
                const auto* operand = expression.left.get();
                while (operand->kind == Expr::Kind::Parenthesized && operand->left) operand = operand->left.get();
                if (operand->kind == Expr::Kind::Unary && operand->text == "*" && operand->left)
                    return type_of(*operand->left);
                return pointer_type(type);
            }
            if (is_vector(type)) return expression.text == "!" ? vector_mask_type(type)
                : vector_operation_type(type, type->element, "+");
            if (expression.text == "!") return builtin_type(BuiltinType::Bool);
            if (expression.text == "*" && type->kind == Type::Kind::Pointer) {
                if (decay && type->pointee && (type->pointee->kind == Type::Kind::Array ||
                                               type->pointee->kind == Type::Kind::Function)) {
                    auto result = pointer_type(type->pointee->kind == Type::Kind::Array
                        ? qualified_element_type(type->pointee) : type->pointee);
                    result->address_space = type->address_space;
                    return result;
                }
                return type->pointee;
            }
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
                const auto* member = source_record_member(expression, program_,
                    [&](const Expr& operand) { return type_of(operand, false); }, &owner);
                if (!member || !owner) return {};
                auto result = clone_type(member->type);
                result->is_const = result->is_const || owner->is_const;
                result->is_volatile = result->is_volatile ||
                                      owner->is_volatile;
                if (pointer_resolver_ && decay && result->kind == Type::Kind::Array) {
                    return pointer_type(qualified_element_type(result));
                }
                return result;
            }
            if (!conditional && expression.text == "index") {
                const auto base = type_of(*expression.left);
                auto result = base && base->kind == Type::Kind::Pointer ? base->pointee
                    : base && (base->kind == Type::Kind::Array ||
                               base->kind == Type::Kind::Vector)
                        ? qualified_element_type(base) : nullptr;
                if (decay && result && result->kind == Type::Kind::Array) {
                    result = pointer_type(qualified_element_type(result));
                    if (base->kind == Type::Kind::Pointer) result->address_space = base->address_space;
                }
                return result;
            }
            const auto left = type_of(*(conditional ? expression.right : expression.left));
            const auto right = type_of(*(conditional ? expression.third : expression.right));
            if (!left || !right) return {};
            if (is_label_type(left) || is_label_type(right)) {
                if (!is_label_type(left) || !is_label_type(right)) return {};
                if (conditional) return builtin_type(BuiltinType::Label);
                if (expression.text == "==" || expression.text == "!=")
                    return builtin_type(BuiltinType::Bool);
                return {};
            }
            if (conditional && left->kind == Type::Kind::Record && right->kind == Type::Kind::Record) {
                auto a = without_alignment(left);
                auto b = without_alignment(right);
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
            if (conditional && (is_meta_type(left) || is_meta_type(right)))
                return conditional_meta_type(left, right);
            if (conditional && (left->kind == Type::Kind::Pointer || right->kind == Type::Kind::Pointer))
                return conditional_pointer_type(left, right);
            const auto object_pointer = [](const TypePtr& type) {
                return type->kind == Type::Kind::Pointer && type->pointee &&
                    meta_object_type(type->pointee);
            };
            if (pointer_resolver_ ||
                ((object_pointer(left) || object_pointer(right)) &&
                 ((expression.left && meta_pointer_source(*expression.left)) ||
                  (expression.right && meta_pointer_source(*expression.right))))) {
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
        case Expr::Kind::Call: {
            if (!expression.left) return {};
            // Types are context-independent; only executing a contextual meta
            // operation requires an active expansion.
            if (auto type = translation_intrinsic_type(expression, true,
                    [&](const Expr& argument) { return type_of(argument); })) return type;
            auto callee = type_of(*expression.left);
            if (callee && callee->kind == Type::Kind::Pointer) callee = callee->pointee;
            return callee && callee->kind == Type::Kind::Function && callee->function
                ? callee->function->result : TypePtr{};
        }
        case Expr::Kind::String: return source_string_type(expression, decay);
        case Expr::Kind::ByteSequence: return expression.type ? expression.type : bytes_type();
        case Expr::Kind::Floating:
            return builtin_type(expression.evaluated_floating
                ? expression.evaluated_floating->type
                : floating_literal_type(expression.text));
        case Expr::Kind::AggregateInitializer: return {};
        }
        return {};
    }

    TypePtr expression_type(const Expr& expression, bool decay = true) {
        return classify_source_type(expression, decay,
            [&](const Expr& source, bool child_decay, const auto& type_of) {
                return expression_type_step(source, child_decay, type_of);
            }, [&] { return resource_exhausted_; });
    }

    IntegerType integer_type(const TypePtr& type) const {
        return evaluation_integer_type(type, program_.address_bits);
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

    bool flat_code_addresses(SourceLocation location) {
        if (program_.evaluation_layout.code_addresses == CodeAddressRepresentation::Flat &&
            program_.address_bits != 0 && program_.address_bits <= 128) return true;
        fail(location, "target does not provide numeric code-address representation for translation-time evaluation");
        return false;
    }

    bool known_truth(const EvalValue& value, SourceLocation location) {
        if (value.label_address) {
            fail(location, "emitted code addresses cannot be inspected during translation-time evaluation");
            return false;
        }
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

    EvaluationTask<std::optional<EvalValue>> new_object_async(TypePtr type, SourceLocation location,
                                       bool initialized = false) {
        const auto size = co_await meta_object_size_async(type);
        if (!size || *size > program_.evaluation_limits.bytes ||
            *size > std::numeric_limits<std::size_t>::max() / 3 ||
            !charge_meta_bytes(*size * 3, location)) {
            if (size && (*size > program_.evaluation_limits.bytes ||
                         *size > std::numeric_limits<std::size_t>::max() / 3)) mark_resource_exhausted();
            fail(location, "translation-time object exceeds target layout or byte budget");
            co_return std::nullopt;
        }
        EvalValue value{UInt128{}, clone_type(type)};
        value.object = std::make_shared<EvalBuffer>();
        value.object->data.resize(*size, '\0');
        value.object->assigned.resize(*size, initialized ? 0xffU : 0);
        value.object->effective_type.resize(*size, 0);
        if (!(co_await stamp_meta_object_types_async(*value.object, type, 0, location))) co_return std::nullopt;
        if (!(co_await register_meta_record_async(*value.object, 0, type, location))) co_return std::nullopt;
        co_return value;
    }

    EvaluationTask<EvalValue> vector_lane_pointer_async(const EvalValue& value, std::size_t lane) const {
        auto pointer = object_pointer(value);
        const auto stride = *(co_await meta_object_size_async(value.type->element));
        pointer.type = pointer_type(clone_type(value.type->element));
        pointer.meta_pointer->view_offset = lane * stride;
        pointer.meta_pointer->view_length = stride;
        co_return pointer;
    }

    TypePtr vector_operation_type(const TypePtr& left, const TypePtr& right,
                                   std::string_view operation) const {
        if (operation != "+" && operation != "-" && operation != "*" && operation != "/" &&
            operation != "%" && operation != "&" && operation != "|" && operation != "^" &&
            operation != "<<" && operation != ">>" && operation != "==" && operation != "!=" &&
            operation != "<" && operation != "<=" && operation != ">" && operation != ">=") return {};
        const auto shape = deferred_vector_extent(right) ? right : is_vector(left) ? left : right;
        if (!shape || !is_vector(shape) || shape->scalable ||
            (is_vector(left) && is_vector(right) &&
             !compatible_vector_shape(left, right))) return {};
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
        return vector_result_type(element, shape);
    }

    TypePtr vector_mask_type(const TypePtr& type) const {
        auto bits = type_bits(type->element);
        if (type->element->builtin == BuiltinType::Fptr ||
            type->element->builtin == BuiltinType::Iptr ||
            type->element->builtin == BuiltinType::Uptr) bits = program_.address_bits;
        return vector_result_type(builtin_integer({bits, true}), type);
    }

    EvaluationTask<std::optional<EvalValue>> vector_binary_values_async(std::string_view operation,
        const EvalValue& left, const EvalValue& right, SourceLocation location) {
        const auto common = vector_operation_type(left.type, right.type, operation);
        if (!common || operation == "&&" || operation == "||") {
            fail(location, "translation-time vector operator requires compatible lane shapes and numeric elements");
            co_return std::nullopt;
        }
        const bool comparison = operation == "==" || operation == "!=" || operation == "<" ||
            operation == "<=" || operation == ">" || operation == ">=";
        auto result = (co_await new_object_async(comparison ? vector_mask_type(common) : common, location));
        if (!result) co_return std::nullopt;
        for (std::size_t lane = 0; lane < common->lanes; ++lane) {
            if (!step(location)) co_return std::nullopt;
            std::optional<EvalValue> a = left, b = right;
            if (is_vector(left.type))
                a = co_await read_meta_pointer_async(co_await vector_lane_pointer_async(left, lane), location);
            if (is_vector(right.type))
                b = co_await read_meta_pointer_async(co_await vector_lane_pointer_async(right, lane), location);
            if (!a || !b) co_return std::nullopt;
            a = (co_await convert_async(*a, common->element, location));
            if (operation != "<<" && operation != ">>") b = (co_await convert_async(*b, common->element, location));
            if (!a || !b) co_return std::nullopt;
            auto value = (co_await scalar_binary_values_async(operation, *a, *b, location));
            if (!value) co_return std::nullopt;
            if (comparison) *value = EvalValue{value->truthy()
                ? mask_to(bit_not(UInt128{}), integer_type(result->type->element).bits) : UInt128{},
                result->type->element};
            if (!(co_await store_meta_pointer_async((co_await vector_lane_pointer_async(*result, lane)), value, location))) co_return std::nullopt;
        }
        co_return result;
    }

    EvaluationTask<std::unique_ptr<Expr>> normalized_object_initializer_async(const Expr& source) {
        // Designators are required constants, independent of current local values.
        std::vector<std::pair<NameKey, TypePtr>> local_types;
        for (auto scope = frame_base_; scope < scopes_.size(); ++scope)
            for (const auto& [name, cell] : scopes_[scope])
                local_types.emplace_back(name, cell.value.type);
        auto normalized = clone_expr(source);
        const auto normalize = [&](const auto& self, Expr& list) -> EvaluationTask<bool> {
            for (auto& entry : list.initializer_entries) {
                for (auto& designator : entry.designators) {
                    if (!designator.index) continue;
                    auto index = (co_await required_integer_with_types_async(*designator.index, local_types,
                        builtin_type(BuiltinType::Uptr)));
                    if (!index) {
                        fail(designator.location, "array initializer designator requires a nonnegative integer constant");
                        co_return false;
                    }
                    designator.index->kind = Expr::Kind::Integer;
                    designator.index->evaluated_integer = Expr::IntegerConstant{index->integer, BuiltinType::Uptr};
                }
                if (entry.value && entry.value->kind == Expr::Kind::AggregateInitializer &&
                    !(co_await self(self, *entry.value))) co_return false;
            }
            co_return true;
        };
        co_return (co_await normalize(normalize, *normalized)) ? std::move(normalized) : nullptr;
    }

    EvaluationTask<std::optional<EvalValue>> initialize_object_async(const Expr& source,
                                               const TypePtr& type,
                                               const PreparedObjectInitializer* prepared = nullptr) {
        auto value = co_await new_object_async(type, source.location, true);
        if (!value) co_return std::nullopt;
        if (source.kind == Expr::Kind::String && type->kind == Type::Kind::Array &&
            type->element->kind == Type::Kind::Builtin && type->element->builtin == BuiltinType::U8) {
            auto text = source.string_value.empty() ? decode_string_literal(source.text)
                                                   : std::optional<std::string>(source.string_value);
            if (!text || text->size() >= value->object->data.size()) {
                fail(source.location, "string initializer does not fit in the u8 array");
                co_return std::nullopt;
            }
            std::copy(text->begin(), text->end(), value->object->data.begin());
            co_return value;
        }
        if (!program_.evaluation_initializer_plan) {
            fail(source.location, "target initializer layout is unavailable during translation-time execution");
            co_return std::nullopt;
        }
        std::unique_ptr<Expr> normalized;
        EvaluationInitializerPlan generated_plan;
        const EvaluationInitializerPlan* plan{};
        if (prepared) plan = &prepared->plan;
        else {
            normalized = co_await normalized_object_initializer_async(source);
            if (!normalized) co_return std::nullopt;
            generated_plan = co_await program_.evaluation_initializer_plan.async(*normalized, type);
            plan = &generated_plan;
        }
        if (!plan->valid) {
            fail(plan->error_location.file ? plan->error_location : source.location,
                 plan->error_message.empty() ? "invalid aggregate initializer" : plan->error_message);
            co_return std::nullopt;
        }
        if (type->kind == Type::Kind::Array && plan->minimum_elements > type->lanes) {
            fail(source.location, "array initializer designator is out of range");
            co_return std::nullopt;
        }
        for (const auto& item : plan->items) {
            if (!step(item.expression->location)) co_return std::nullopt;
            auto pointer = object_pointer(*value);
            auto destination = clone_type(item.type);
            destination->is_const = false; // Initialization, not a modifying const access.
            pointer.type = pointer_type(destination);
            pointer.meta_pointer->view_offset = static_cast<std::size_t>(item.layout.offset);
            pointer.meta_pointer->view_length = (co_await meta_object_size_async(destination)).value_or(0);
            pointer.meta_pointer->access_alignment = item.layout.alignment;
            pointer.meta_pointer->union_member_view = true; // Destination types already stamped.
            if (item.layout.bit_width) pointer.meta_pointer->bit_field = EvalMetaPointer::BitField{
                *item.layout.bit_width, item.layout.bit_offset, {}, 0};
            if (item.type->kind == Type::Kind::Array && item.expression->kind == Expr::Kind::String) {
                auto nested = (co_await initialize_object_async(*item.expression, item.type));
                if (!nested) co_return std::nullopt;
                std::copy(nested->object->data.begin(), nested->object->data.end(),
                          value->object->data.begin() + static_cast<std::ptrdiff_t>(item.layout.offset));
            } else if (!(co_await store_meta_pointer_async(pointer, (co_await this->expression_async(*item.expression)),
                                           item.expression->location))) co_return std::nullopt;
        }
        co_return value;
    }

    EvaluationTask<std::optional<EvalValue>> convert_async(EvalValue value, const TypePtr& type,
                                     SourceLocation location, bool explicit_cast = false) {
        if (!type || type->is_volatile || type->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        if (is_label_type(value.type) || is_label_type(type)) {
            if (is_label_type(value.type) && is_label_type(type)) {
                value.type = clone_type(type);
                co_return value;
            }
            const auto& other = is_label_type(value.type) ? type : value.type;
            if (!explicit_cast || !other || other->kind != Type::Kind::Builtin ||
                other->builtin != BuiltinType::Uptr) {
                fail(location, "code labels permit only same-type or explicit uptr conversions");
                co_return std::nullopt;
            }
            if (value.label_address) {
                fail(location, "emitted code addresses cannot be inspected during translation-time evaluation");
                co_return std::nullopt;
            }
            if (!flat_code_addresses(location)) {
                co_return std::nullopt;
            }
            value.integer = mask_to(value.integer, program_.address_bits);
            value.type = clone_type(type);
            co_return value;
        }
        if (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool && value.pointer()) {
            if (value.address && value.address->kind != AddressConstant::Kind::Absolute) {
                fail(location, "emitted object addresses cannot be inspected during translation-time evaluation");
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{value.truthy()}, clone_type(type)};
        }
        if (value.syntax_match || type->kind == Type::Kind::SyntaxMatch) {
            if (!procedural_ || !value.syntax_match || type->kind != Type::Kind::SyntaxMatch) {
                fail(location, "syntax matches cannot convert to runtime or other meta values");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (value.syntax_node || type->kind == Type::Kind::Syntax) {
            if (!procedural_ || !value.syntax_node || type->kind != Type::Kind::Syntax) {
                fail(location, "syntax nodes cannot convert to runtime or other meta values");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (value.syntax_span || type->kind == Type::Kind::Span) {
            if (!procedural_ || !value.syntax_span || type->kind != Type::Kind::Span) {
                fail(location, "syntax spans cannot convert to runtime or other meta values");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (value.syntax_context || type->kind == Type::Kind::Context ||
            (value.type && value.type->kind == Type::Kind::Context)) {
            if (!procedural_ || !value.syntax_context || type->kind != Type::Kind::Context) {
                fail(location, "syntax contexts cannot convert to runtime or other meta values");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (value.tokens || type->kind == Type::Kind::Tokens) {
            if (!procedural_ || !value.tokens || type->kind != Type::Kind::Tokens) {
                fail(location, "token values cannot be converted to or from runtime types");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (value.bytes || value.buffer || type->kind == Type::Kind::Bytes ||
            type->kind == Type::Kind::Buffer) {
            if ((value.bytes && type->kind != Type::Kind::Bytes) ||
                (value.buffer && type->kind != Type::Kind::Buffer) ||
                (!value.bytes && !value.buffer)) {
                fail(location, "meta values cannot be converted to runtime types or other meta types");
                co_return std::nullopt;
            }
            if (value.buffer && value.buffer->frozen) {
                fail(location, "buffer handle was used after freeze");
                co_return std::nullopt;
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (type->kind == Type::Kind::Vector && !type->scalable &&
            (!value.object || is_vector(value.type))) {
            if (is_vector(value.type) &&
                (value.type->scalable || value.type->lanes != type->lanes)) {
                fail(location, "translation-time vector conversion requires matching lane shapes");
                co_return std::nullopt;
            }
            auto result = (co_await new_object_async(type, location));
            if (!result) co_return std::nullopt;
            for (std::size_t lane = 0; lane < type->lanes; ++lane) {
                if (!step(location)) co_return std::nullopt;
                std::optional<EvalValue> scalar = value;
                if (value.object)
                    scalar = co_await read_meta_pointer_async(co_await vector_lane_pointer_async(value, lane), location);
                if (!scalar) co_return std::nullopt;
                scalar = (co_await convert_async(*scalar, type->element, location, explicit_cast));
                if (!scalar || !(co_await store_meta_pointer_async((co_await vector_lane_pointer_async(*result, lane)), scalar, location)))
                    co_return std::nullopt;
            }
            co_return result;
        }
        if (value.object || type->kind == Type::Kind::Vector ||
            type->kind == Type::Kind::Record) {
            if (!value.object || !value.type ||
                (value.type->kind != Type::Kind::Vector &&
                 value.type->kind != Type::Kind::Record) ||
                value.type->kind != type->kind) {
                fail(location, "translation-time aggregate conversion requires a matching value");
                co_return std::nullopt;
            }
            auto from = without_alignment(value.type);
            auto to = without_alignment(type);
            from->is_const = to->is_const = false;
            if (!same_type(from, to) || from->scalable || to->scalable) {
                fail(location, "translation-time aggregate conversion requires the same complete type");
                co_return std::nullopt;
            }
            const auto size = value.object->data.size();
            EvalValue object_pointer{UInt128{}, pointer_type(clone_type(value.type))};
            EvalMetaPointer pointer;
            pointer.mutable_buffer = value.object;
            pointer.view_length = size;
            object_pointer.meta_pointer = std::move(pointer);
            if (!(co_await validate_meta_object_value_async(object_pointer, location)))
                co_return std::nullopt;
            if (size > std::numeric_limits<std::size_t>::max() / 3)
                co_return std::nullopt;
            auto cost = size * 3;
            for (const auto& slot : value.object->pointers)
                cost += 64 + type_name(slot.value->type).size();
            for (const auto& object : value.object->typed_objects) {
                const auto metadata = 24 + type_name(object.type).size();
                if (metadata > std::numeric_limits<std::size_t>::max() - cost)
                    co_return std::nullopt;
                cost += metadata;
            }
            if (!charge_meta_bytes(cost, location)) co_return std::nullopt;
            value.object = std::make_shared<EvalBuffer>(*value.object);
            value.type = clone_type(type);
            if (!(co_await stamp_meta_object_types_async(*value.object, value.type, 0, location)))
                co_return std::nullopt;
            co_return value;
        }
        if (value.meta_pointer) {
            if (type->kind != Type::Kind::Pointer) {
                fail(location, "meta data pointers cannot convert to integer or other runtime values");
                co_return std::nullopt;
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
                co_return std::nullopt;
            }
            if (value.meta_pointer->mutable_buffer &&
                value.meta_pointer->mutable_buffer->frozen) {
                fail(location, "buffer data pointer was used after freeze");
                co_return std::nullopt;
            }
            if (!same_type(value.type->pointee, type->pointee)) {
                value.meta_pointer->access_alignment.reset();
                value.meta_pointer->union_member_view = false;
                value.meta_pointer->bit_field.reset();
            }
            value.type = clone_type(type);
            co_return value;
        }
        if (!pointer_resolver_ && type->kind == Type::Kind::Pointer && is_integer(value.type) && value.integer == UInt128{}) {
            EvalValue result{UInt128{}, clone_type(type)};
            result.address = AddressConstant{};
            co_return result;
        }
        if (pointer_resolver_ && type->kind == Type::Kind::Pointer && !value.string) {
            auto source = value_expression(value, location);
            if (!source) co_return std::nullopt;
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
            co_return co_await resolve_pointer_async(std::move(source), type);
        }
        if (value.address) {
            // Normalization has already established these target address bits.
            // This does not reveal the bits of emitted symbols or meta storage.
            std::optional<std::uint64_t> bytes;
            if (size_of_ && value.type) bytes = co_await size_of_->async(value.type);
            if (explicit_cast && pointer_resolver_ && value.address->kind == AddressConstant::Kind::Absolute &&
                value.type && value.type->kind == Type::Kind::Pointer && value.type->pointee &&
                value.type->pointee->kind != Type::Kind::Function && is_integer(type) && bytes &&
                *bytes * 8 == integer_type(type).bits) {
                co_return EvalValue{mask_to(value.address->absolute, integer_type(type).bits), clone_type(type)};
            }
            fail(location, "emitted object addresses cannot be inspected during translation-time evaluation");
            co_return std::nullopt;
        }
        if (value.pointer()) {
            if (type->kind == Type::Kind::Pointer) {
                if (value.type->address_space != type->address_space) {
                    fail(location, "implicit pointer conversion changes address space");
                    co_return std::nullopt;
                }
                if (!type->pointee || !value.type->pointee ||
                    (value.type->pointee->is_const && !type->pointee->is_const) ||
                    (value.type->pointee->is_volatile && !type->pointee->is_volatile)) {
                    fail(location, "implicit pointer conversion discards qualifiers");
                    co_return std::nullopt;
                }
                value.type = clone_type(type);
                co_return value;
            }
            if (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Bool)
                co_return EvalValue{UInt128{1}, clone_type(type)};
            co_return std::nullopt;
        }
        if (is_floating(type)) {
            const auto format = floating_format(type->builtin,
                                                program_.address_bits);
            floating::ExceptionSet raised{};
            if (value.floating) {
                if (value.floating->format != format) {
                    value.floating = floating::convert(*value.floating, format, &raised);
                    if (!floating_environment_permits(raised, &*value.floating, location))
                        co_return std::nullopt;
                }
            } else if (is_integer(value.type)) {
                const auto source = integer_type(value.type);
                value.floating = floating::from_integer(value.integer,
                    source.bits, source.is_signed, format, &raised);
                if (!floating_environment_permits(raised, &*value.floating, location))
                    co_return std::nullopt;
            } else co_return std::nullopt;
            value.type = clone_type(type);
            co_return value;
        }
        if (value.floating) {
            if (!is_integer(type)) co_return std::nullopt;
            const auto target = integer_type(type);
            if (target.is_bool) {
                co_return EvalValue{UInt128{floating::nonzero(*value.floating)},
                                 clone_type(type)};
            }
            floating::ExceptionSet raised{};
            const auto integer = floating::to_integer(*value.floating,
                                                      target.bits, target.is_signed, &raised);
            if (!integer) {
                fail(location, "floating-to-integer conversion is out of range during translation-time evaluation");
                co_return std::nullopt;
            }
            if (!floating_environment_permits(raised, nullptr, location)) co_return std::nullopt;
            co_return EvalValue{*integer, clone_type(type)};
        }
        if (!is_integer(value.type) || !is_integer(type)) co_return std::nullopt;
        value.integer = convert_integer(value.integer, integer_type(value.type), integer_type(type));
        value.type = clone_type(type);
        co_return value;
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
        const auto result = checked_integer_operation(operation, left.integer, right.integer, type,
            program_.evaluation_layout.wrap_signed);
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

    EvaluationTask<std::optional<EvalValue>> calculate_floating_async(std::string_view operation,
                                                EvalValue left, EvalValue right,
                                                SourceLocation location) {
        const auto result_type = common_floating_type(left.type, right.type);
        auto converted_left = (co_await convert_async(std::move(left), result_type, location));
        auto converted_right = (co_await convert_async(std::move(right), result_type, location));
        if (!converted_left || !converted_right) co_return std::nullopt;
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
            floating::ExceptionSet raised{};
            const bool result = floating::compare(comparison,
                *left.floating, *right.floating, &raised);
            if (!floating_environment_permits(raised, nullptr, location)) co_return std::nullopt;
            co_return EvalValue{UInt128{result}, builtin_type(BuiltinType::Bool)};
        }
        if (operation != "+" && operation != "-" && operation != "*" &&
            operation != "/") co_return std::nullopt;
        const auto opcode = operation == "+" ? floating::Operation::Add
            : operation == "-" ? floating::Operation::Subtract
            : operation == "*" ? floating::Operation::Multiply
                                : floating::Operation::Divide;
        floating::ExceptionSet raised{};
        auto result = floating::binary(opcode, *left.floating,
            *right.floating, floating_format(result_type->builtin,
                                              program_.address_bits), &raised);
        if (!floating_environment_permits(raised, &result, location)) co_return std::nullopt;
        co_return EvalValue{result, result_type};
    }

    // Applies the profile's floating environment to an operation that raised
    // `raised`: an operation that would trap is not evaluated.
    bool floating_environment_permits(floating::ExceptionSet raised,
                                      floating::Value* result, SourceLocation location) {
        const auto trapped = floating::trap(
            program_.evaluation_layout.floating_environment, raised, result);
        if (!trapped) return true;
        fail(location, *trapped == floating::Exception::DenormalOperand
            ? std::string("floating-point operation traps on a denormal operand during translation-time evaluation")
            : "floating-point operation raises the enabled '" +
                  std::string(floating::exception_name(*trapped)) +
                  "' exception during translation-time evaluation");
        return false;
    }

    struct Flow {
        enum Kind { Normal, Return, Break, Continue, Failed } kind{Normal};
        std::optional<EvalValue> value;
        SourceLocation return_location;
        // A Return that transfers to this call in place of the frame.
        std::shared_ptr<PreparedCall> tail;

        Flow() = default;
        Flow(Kind flow_kind, std::optional<EvalValue> flow_value = {}, SourceLocation at = {})
            : kind(flow_kind), value(std::move(flow_value)), return_location(at) {}
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

    void mark_resource_exhausted() {
        if (resource_exhausted_) return;
        resource_exhausted_ = true;
        if (resource_reporting_ == ResourceReporting::Required)
            ++program_.evaluation_resource_errors;
    }

    void fail_resource(SourceLocation location, std::string reason) {
        mark_resource_exhausted();
        fail(location, std::move(reason));
    }

    void fail_context(SourceLocation location) {
        if (failure_reason_) return;
        context_unavailable_ = true;
        fail(location, "meta operation requires an active expansion context");
    }

    bool step(SourceLocation location, std::uint64_t amount = 1) {
        if (resource_exhausted_) return false;
        const auto limit = program_.evaluation_limits.steps;
        if (amount <= limit - std::min(steps_, limit)) {
            steps_ += amount;
            return true;
        }
        steps_ = limit;
        if (!budget_diagnosed_) {
            fail_resource(location,
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

    bool translation_cell(const Cell& cell, SourceLocation location) {
        if (!cell.type_only) return true;
        fail(location, "runtime local or parameter is not a translation-time value");
        return false;
    }

    EvaluationTask<std::optional<EvalValue>> read_cell_async(const Cell& cell, SourceLocation location) {
        if (!translation_cell(cell, location)) co_return std::nullopt;
        if (cell.storage) {
            EvalValue storage{UInt128{}, cell.value.type};
            storage.object = cell.storage;
            co_return (co_await read_meta_pointer_async(object_pointer(storage), location));
        }
        if (!cell.initialized) {
            fail(location, "read of uninitialized value during translation-time evaluation");
            co_return std::nullopt;
        }
        if (cell.value.type->is_volatile || cell.value.type->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        if (cell.value.buffer && cell.value.buffer->frozen) {
            fail(location, "buffer handle was used after freeze");
            co_return std::nullopt;
        }
        if (cell.value.object) co_return (co_await read_meta_pointer_async(object_pointer(cell.value), location));
        co_return cell.value;
    }

    EvaluationTask<std::optional<EvalValue>> lookup_async(const Expr& expression) {
        const auto& name = expression.text;
        const auto location = expression.location;
        if (auto* cell = lookup_mutable(expression)) co_return (co_await read_cell_async(*cell, location));
        if (expression.name_context && expression.name_context->label_address) {
            if (const auto* error = label_address_error(*expression.name_context->label_address)) {
                fail(location, error);
                co_return std::nullopt;
            }
            EvalValue value{UInt128{}, builtin_type(BuiltinType::Label)};
            value.label_address = expression.name_context->label_address;
            co_return value;
        }
        auto found = resolve_enumerator(program_, current_function_, current_namespace_, expression);
        if (found && !found->enumerator->value && program_.evaluation_prepare_enumerator) {
            const Program::EnumerationPosition position{
                static_cast<std::size_t>(found->enumeration - program_.enumerations.data()),
                static_cast<std::size_t>(found->enumerator - found->enumeration->enumerators.data())};
            const bool prepared = co_await program_.evaluation_prepare_enumerator.async(position);
            found = prepared ? resolve_enumerator(program_, current_function_, current_namespace_, expression)
                             : std::nullopt;
        }
        if (found && found->enumerator->value) {
            co_return EvalValue{
                found->enumerator->value->value,
                enum_type(*found->enumeration)};
        }
        if (pointer_resolver_) {
            auto source = clone_expr(expression);
            const auto type = expression_type(*source);
            const auto* object = resolve_object(program_, current_function_, expression);
            if (object && object->type->kind != Type::Kind::Array) {
                fail(location, "runtime/static storage cannot be read during translation-time evaluation");
                co_return std::nullopt;
            }
            if (type && type->kind == Type::Kind::Pointer) {
                const bool function = direct_function(*source) != nullptr;
                auto value = co_await resolve_pointer_async(std::move(source), type);
                if (value) value->function_designator = function;
                co_return value;
            }
        }
        if (auto label = resolve_label_constant(expression, current_function_, program_, nullptr,
                [&](SourceLocation at) { return step(at); })) {
            if (const auto* error = label_address_error(*label)) {
                fail(location, error);
                co_return std::nullopt;
            }
            if (!charge_meta_bytes(64 + label->global_name.size(), location)) co_return std::nullopt;
            EvalValue value{UInt128{}, builtin_type(BuiltinType::Label)};
            value.label_address = std::move(label);
            co_return value;
        }
        fail(location, "expression is not a translation-time value: '" +
                       std::string(name) + "' has no value in this syntax context");
        co_return std::nullopt;
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
        if (meta_scalar_type(type) || is_label_type(type)) return true;
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
        return static_cast<std::size_t>(builtin_storage_size(value.type->pointee->builtin,
            program_.address_bits, program_.evaluation_layout.f80_storage_bytes).value_or(0));
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

    EvaluationTask<std::optional<std::size_t>> meta_object_size_async(TypePtr type) const {
        // A typedef's requested alignment rounds the natural size up.
        if (type && type->alignment) {
            const auto size = co_await meta_object_size_async(without_alignment(type));
            const auto alignment = co_await meta_object_alignment_async(type);
            if (!size || !alignment) co_return std::nullopt;
            const auto storage = requested_storage({*size, *alignment}, type->alignment);
            co_return storage && storage->size <= std::numeric_limits<std::size_t>::max()
                ? std::optional<std::size_t>(static_cast<std::size_t>(storage->size)) : std::nullopt;
        }
        // Multiply out nested array layers in one pass rather than revisiting
        // the rest of the chain at every level; a layer with its own requested
        // alignment is sized separately.
        if (type && type->kind == Type::Kind::Array) {
            std::size_t count = 1;
            do {
                if (!type->lanes || !type->element ||
                    count > std::numeric_limits<std::size_t>::max() / type->lanes) co_return std::nullopt;
                count *= type->lanes;
                type = type->element;
            } while (type->kind == Type::Kind::Array && !type->alignment);
            const auto element = co_await meta_object_size_async(type);
            if (!element || *element == 0 || count > std::numeric_limits<std::size_t>::max() / *element)
                co_return std::nullopt;
            co_return *element * count;
        }
        if (is_label_type(type)) {
            std::optional<std::uint64_t> size;
            if (size_of_) size = co_await size_of_->async(type);
            co_return size && *size != 0 && *size <= std::numeric_limits<std::size_t>::max()
                ? std::optional<std::size_t>(static_cast<std::size_t>(*size)) : std::nullopt;
        }
        if (type && type->kind == Type::Kind::Pointer) {
            std::optional<std::uint64_t> size;
            if (size_of_) size = co_await size_of_->async(type);
            else size = (program_.address_bits + 7U) / 8U;
            co_return size && *size <= std::numeric_limits<std::size_t>::max()
                ? std::optional<std::size_t>(static_cast<std::size_t>(*size)) : std::nullopt;
        }
        if (!meta_object_type(type)) co_return std::nullopt;
        if (type->kind == Type::Kind::Record ||
            type->kind == Type::Kind::Vector) {
            if (!size_of_) co_return std::nullopt;
            const auto size = co_await size_of_->async(type);
            if (!size || *size == 0 ||
                *size > std::numeric_limits<std::size_t>::max())
                co_return std::nullopt;
            co_return static_cast<std::size_t>(*size);
        }
        EvalValue value;
        value.type = pointer_type(type);
        co_return meta_scalar_size(value);
    }

    EvaluationTask<std::optional<std::size_t>> meta_object_alignment_async(TypePtr type) const {
        // Array counts do not affect alignment; every layer's request does.
        std::size_t requested = 1;
        for (; type; type = type->element) {
            requested = std::max<std::size_t>(requested, type->alignment);
            if (type->kind != Type::Kind::Array) break;
        }
        if (!type) co_return std::nullopt;
        const auto natural = type->alignment ? without_alignment(type) : type;
        std::optional<std::uint64_t> alignment;
        if (is_label_type(type) || type->kind == Type::Kind::Pointer ||
            type->kind == Type::Kind::Record || type->kind == Type::Kind::Vector) {
            if (align_of_) alignment = co_await align_of_->async(type);
            else if (type->kind == Type::Kind::Pointer) {
                const auto size = co_await meta_object_size_async(natural);
                if (size) alignment = natural_storage_alignment(*size, false,
                    program_.evaluation_layout.natural_alignment_limit, program_.evaluation_layout.f80_alignment);
            }
        } else if (const auto size = co_await meta_object_size_async(natural)) {
            alignment = natural_storage_alignment(*size,
                type->builtin == BuiltinType::F80, program_.evaluation_layout.natural_alignment_limit,
                program_.evaluation_layout.f80_alignment);
        }
        if (!alignment || *alignment == 0 || *alignment > std::numeric_limits<std::size_t>::max())
            co_return std::nullopt;
        co_return std::max(static_cast<std::size_t>(*alignment), requested);
    }

    static bool same_meta_object_type(const TypePtr& left, const TypePtr& right) {
        auto a = without_alignment(left);
        auto b = without_alignment(right);
        a->is_const = b->is_const = false;
        return same_type(a, b);
    }

    struct MetaProjectionQuery {
        enum class Kind { Subobject, EffectiveTag } kind{Kind::Subobject};
        TypePtr requested;
        std::size_t length{};
        bool qualified_pointer_read{};
        std::uint8_t tag{};
    };

    // Metadata queries may themselves run required layout/value continuations.
    // Retain source-ordered frames rather than one native frame per ancestor;
    // do not query later members before the selected child's search finishes.
    EvaluationTask<bool> meta_projection_matches_async(TypePtr owner, std::size_t offset,
                                  const MetaProjectionQuery& query) const {
        struct Frame {
            TypePtr type;
            std::size_t offset{};
            std::shared_ptr<const RecordDecl> record;
            std::size_t next{};
        };
        std::vector<Frame> frames{{std::move(owner), offset, {}, 0}};
        const auto resource_epoch = program_.evaluation_resource_errors;
        while (!frames.empty()) {
            if (program_.evaluation_resource_errors != resource_epoch) co_return false;
            auto& frame = frames.back();
            if (!frame.record) {
                const auto size = (co_await meta_object_size_async(frame.type));
                if (program_.evaluation_resource_errors != resource_epoch) co_return false;
                if (!size || frame.offset > *size || query.length > *size - frame.offset) {
                    frames.pop_back();
                    continue;
                }
                if (query.kind == MetaProjectionQuery::Kind::Subobject) {
                    if (frame.offset == 0 && query.length == *size &&
                        same_meta_object_type(frame.type, query.requested)) co_return true;
                    // Read-only nested qualification conversion preserves an
                    // opaque pointer cell, not its representation bytes.
                    if (query.qualified_pointer_read && frame.offset == 0 && query.length == *size &&
                        frame.type->kind == Type::Kind::Pointer && query.requested->kind == Type::Kind::Pointer &&
                        query.requested->is_const && compatible_pointee(frame.type, query.requested)) co_return true;
                    if (frame.offset == 0 && query.length == *size && frame.type->nominal_key().empty() &&
                        query.requested->nominal_key().empty() && meta_scalar_type(frame.type) &&
                        meta_scalar_type(query.requested) &&
                        compatible_meta_type(frame.type->builtin, query.requested->builtin)) co_return true;
                } else {
                    if (meta_scalar_type(frame.type)) {
                        if (query.tag != 0xffU && compatible_meta_type(
                            static_cast<BuiltinType>(query.tag - 1), frame.type->builtin)) co_return true;
                        frames.pop_back();
                        continue;
                    }
                    if (frame.type->kind == Type::Kind::Pointer || is_label_type(frame.type)) {
                        if (query.tag == 0xffU) co_return true;
                        frames.pop_back();
                        continue;
                    }
                }
                if (frame.type->kind == Type::Kind::Array || frame.type->kind == Type::Kind::Vector) {
                    const auto stride = (co_await meta_object_size_async(frame.type->element));
                    if (program_.evaluation_resource_errors != resource_epoch) co_return false;
                    if (!stride || !*stride) { frames.pop_back(); continue; }
                    // An element is the only candidate: replace this frame.
                    const auto element = frame.type->element;
                    const auto element_offset = frame.offset % *stride;
                    frame = Frame{element, element_offset, {}, 0};
                    continue;
                }
                if (frame.type->kind != Type::Kind::Record || frame.type->is_union) {
                    if (frame.type->kind == Type::Kind::Record && frame.type->is_union &&
                        query.kind == MetaProjectionQuery::Kind::EffectiveTag) co_return true;
                    frames.pop_back();
                    continue;
                }
                frame.record = program_.record_definition(frame.type->nominal_key());
                if (!frame.record) { frames.pop_back(); continue; }
            }
            if (frame.next == frame.record->members.size()) {
                // An aggregate's padding retains its aggregate tag. This is
                // not a scalar compatibility or union exception for casts.
                if (query.kind == MetaProjectionQuery::Kind::EffectiveTag && query.tag == 0xffU) co_return true;
                frames.pop_back();
                continue;
            }
            const auto& member = frame.record->members[frame.next++];
            if (member.name.empty()) continue;
            const auto member_type = member.type;
            const auto member_name = member.member_name();
            std::optional<EvaluationMemberLayout> layout;
            if (program_.evaluation_member_layout)
                layout = co_await program_.evaluation_member_layout.async(frame.type, member_name);
            if (program_.evaluation_resource_errors != resource_epoch) co_return false;
            const auto member_size = (co_await meta_object_size_async(member_type));
            if (program_.evaluation_resource_errors != resource_epoch) co_return false;
            if (layout && member_size && layout->offset <= frame.offset &&
                frame.offset - layout->offset <= *member_size &&
                query.length <= *member_size - (frame.offset - layout->offset)) {
                const auto child_offset = frame.offset - static_cast<std::size_t>(layout->offset);
                frames.push_back({member_type, child_offset, {}, 0});
            }
        }
        co_return false;
    }

    EvaluationTask<bool> meta_record_subobject_async(const TypePtr& owner, std::size_t offset,
                               const TypePtr& requested, std::size_t length,
                               bool qualified_pointer_read = false) const {
        co_return (co_await meta_projection_matches_async(owner, offset,
            {MetaProjectionQuery::Kind::Subobject, requested, length, qualified_pointer_read, 0}));
    }

    // A record defined [[may_alias]] qualifies each use like a may_alias typedef.
    bool may_alias(const TypePtr& type) const {
        if (type->may_alias) return true;
        if (type->kind != Type::Kind::Record) return false;
        const auto definition = program_.record_definition(type->nominal_key());
        return definition && definition->attribute("may_alias");
    }

    EvaluationTask<bool> meta_record_effective_access_async(const EvalValue& base,
                                      SourceLocation location, bool write = false) {
        if (!base.meta_pointer || !base.meta_pointer->mutable_buffer ||
            base.meta_pointer->union_member_view || may_alias(base.type->pointee)) co_return true;
        const auto index = (co_await meta_access_index_async(base, location, write));
        const auto size = (co_await meta_object_size_async(base.type->pointee));
        if (!index || !size) co_return false;
        for (const auto& object : base.meta_pointer->mutable_buffer->typed_objects) {
            if (*index >= object.offset + object.length ||
                object.offset >= *index + *size) continue;
            const bool contained = *index >= object.offset &&
                *index - object.offset <= object.length &&
                *size <= object.length - (*index - object.offset) &&
                (co_await meta_record_subobject_async(object.type, *index - object.offset,
                                      base.type->pointee, *size, !write));
            const bool containing = object.offset >= *index &&
                object.offset - *index <= *size &&
                object.length <= *size - (object.offset - *index) &&
                (co_await meta_record_subobject_async(base.type->pointee, object.offset - *index,
                                      object.type, object.length));
            if (!contained && !containing) {
                fail(location, write ? "meta pointer write violates aggregate effective type"
                                     : "meta pointer read violates aggregate effective type");
                co_return false;
            }
        }
        co_return true;
    }

    EvaluationTask<bool> register_meta_record_async(EvalBuffer& storage, std::size_t offset,
                              TypePtr type, SourceLocation location) {
        if (type->kind != Type::Kind::Record && type->kind != Type::Kind::Pointer && !is_label_type(type) &&
            type->nominal_key().empty()) co_return true;
        const auto size = co_await meta_object_size_async(type);
        if (!size) co_return false;
        for (const auto& object : storage.typed_objects)
            if (offset >= object.offset && offset - object.offset <= object.length &&
                *size <= object.length - (offset - object.offset)) co_return true;
        // Account for logical range and type identity, not host shared_ptr size.
        if (!charge_meta_bytes(24 + type_name(type).size(), location)) co_return false;
        std::erase_if(storage.typed_objects, [&](const EvalBuffer::TypedObject& object) {
            return object.offset >= offset && object.offset - offset <= *size &&
                object.length <= *size - (object.offset - offset);
        });
        storage.typed_objects.push_back({offset, *size, clone_type(type)});
        co_return true;
    }

    EvaluationTask<bool> meta_bit_field_record_view_async(const EvalMetaPointer& pointer) const {
        if (!pointer.bit_field || !pointer.bit_field->owner ||
            !pointer.mutable_buffer) co_return false;
        const auto& field = *pointer.bit_field;
        const auto size = (co_await meta_object_size_async(field.owner));
        if (!size) co_return false;
        for (const auto& object : pointer.mutable_buffer->typed_objects)
            if (field.owner_offset >= object.offset &&
                field.owner_offset - object.offset <= object.length &&
                *size <= object.length - (field.owner_offset - object.offset) &&
                (co_await meta_record_subobject_async(object.type, field.owner_offset - object.offset,
                                      field.owner, *size))) co_return true;
        co_return false;
    }

    EvaluationTask<bool> meta_object_accepts_tag_at_async(const TypePtr& type, std::size_t offset,
                                    std::uint8_t tag) const {
        co_return (co_await meta_projection_matches_async(type, offset,
            {MetaProjectionQuery::Kind::EffectiveTag, {}, 1, false, tag}));
    }

    // A whole aggregate write establishes the declared leaf types. Padding
    // and unions retain an aggregate tag rather than becoming raw storage;
    // projected union members still have their explicit reinterpretation rule.
    EvaluationTask<bool> stamp_meta_object_types_async(EvalBuffer& storage, TypePtr type,
                                 std::size_t offset, SourceLocation location) {
        const auto size = co_await meta_object_size_async(type);
        if (!size || offset > storage.data.size() ||
            *size > storage.data.size() - offset ||
            meta_volatile_or_atomic(type)) {
            fail(location, "meta aggregate requires supported non-volatile object members");
            co_return false;
        }
        if (meta_scalar_type(type)) {
            std::fill_n(storage.effective_type.begin() +
                            static_cast<std::ptrdiff_t>(offset), *size,
                        static_cast<std::uint8_t>(
                            static_cast<std::uint8_t>(type->builtin) + 1));
            co_return co_await register_meta_record_async(storage, offset, type, location);
        }
        if (type->kind == Type::Kind::Pointer || is_label_type(type)) {
            std::fill_n(storage.effective_type.begin() + static_cast<std::ptrdiff_t>(offset),
                        *size, static_cast<std::uint8_t>(0xffU));
            co_return co_await register_meta_record_async(storage, offset, type, location);
        }
        if (type->kind == Type::Kind::Array ||
            type->kind == Type::Kind::Vector) {
            const auto stride = co_await meta_object_size_async(type->element);
            if (!stride) co_return false;
            for (std::size_t lane = 0; lane < type->lanes; ++lane)
                if (!(co_await stamp_meta_object_types_async(storage, type->element,
                        offset + lane * *stride, location))) co_return false;
            co_return true;
        }
        if (type->kind != Type::Kind::Record) co_return false;
        std::fill_n(storage.effective_type.begin() +
                        static_cast<std::ptrdiff_t>(offset), *size,
                    static_cast<std::uint8_t>(0xffU));
        if (type->is_union) co_return true;
        const auto record = program_.record_definition(type->nominal_key());
        if (!record) co_return false;
        for (const auto& member : record->members) {
            if (member.name.empty()) continue;
            std::optional<EvaluationMemberLayout> layout;
            if (program_.evaluation_member_layout)
                layout = co_await program_.evaluation_member_layout.async(type, member.member_name());
            if (layout && layout->bit_width) {
                const auto unit = co_await meta_object_size_async(member.type);
                if (!unit || layout->offset > *size || *unit > *size - layout->offset ||
                    *layout->bit_width == 0 || *layout->bit_width > *unit * 8 ||
                    layout->bit_offset > *unit * 8 - *layout->bit_width) {
                    fail(location, "meta bit-field has invalid target layout");
                    co_return false;
                }
                const auto mask = meta_bit_field_mask({*layout->bit_width, layout->bit_offset, {}, 0});
                // A target allocation unit can overlap a preceding ordinary
                // member. Stamp only bytes touched by the actual field bits.
                for (std::size_t byte = 0; byte < *unit; ++byte) {
                    const auto lane = program_.evaluation_layout.byte_order ==
                        EvaluationByteOrder::Little ? byte : *unit - 1 - byte;
                    if ((shift_right(mask, static_cast<unsigned>(lane * 8)).low & 0xffU) != 0)
                        storage.effective_type[offset + layout->offset + byte] =
                            static_cast<std::uint8_t>(member.type->builtin) + 1;
                }
                continue;
            }
            if (!layout || layout->offset > *size) co_return false;
            if (!(co_await stamp_meta_object_types_async(storage, member.type,
                    offset + static_cast<std::size_t>(layout->offset), location))) co_return false;
        }
        co_return true;
    }

    enum class MetaObjectAccess { CheckAggregate, RetainedSubobject };

    EvaluationTask<bool> initialized_meta_output_async(const EvalValue& value,
                                                       SourceLocation location) {
        // Value copying may retain a union's partly indeterminate representation.
        // An out cell additionally needs one complete alternative on normal
        // return. Check semantic fields/bits, never padding, with heap frames.
        struct Frame {
            TypePtr type;
            std::size_t offset{}, next{};
            std::optional<std::size_t> size, stride;
            std::optional<EvalMetaPointer::BitField> field;
            std::shared_ptr<const RecordDecl> record;
            bool any{};
        };
        std::vector<Frame> work;
        Frame root;
        root.type = value.type;
        work.push_back(std::move(root));
        std::optional<bool> child;
        const auto finish = [&](bool complete) {
            work.pop_back();
            child = complete;
        };
        while (!work.empty()) {
            if (!step(location)) co_return false;
            auto& frame = work.back();
            if (child) {
                const bool complete = *child;
                child.reset();
                if ((frame.any && complete) || (!frame.any && !complete)) {
                    finish(complete);
                    continue;
                }
            }
            if (!frame.size) {
                frame.size = co_await meta_object_size_async(frame.type);
                if (!frame.size || frame.offset > value.object->assigned.size() ||
                    *frame.size > value.object->assigned.size() - frame.offset) {
                    fail(location, "meta output cell has invalid target layout");
                    co_return false;
                }
                if (meta_scalar_type(frame.type) || frame.type->kind == Type::Kind::Pointer ||
                    is_label_type(frame.type)) {
                    const auto mask = frame.field ? meta_bit_field_mask(*frame.field) : UInt128{};
                    bool complete = true;
                    for (std::size_t byte = 0; byte < *frame.size; ++byte) {
                        const auto lane = program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                            ? byte : *frame.size - 1 - byte;
                        if (frame.type->kind == Type::Kind::Builtin &&
                            frame.type->builtin == BuiltinType::F80 && lane >= 10) continue;
                        const auto required = frame.field
                            ? static_cast<std::uint8_t>(shift_right(mask, static_cast<unsigned>(lane * 8)).low)
                            : static_cast<std::uint8_t>(0xffU);
                        if ((value.object->assigned[frame.offset + byte] & required) != required) {
                            complete = false;
                            break;
                        }
                    }
                    finish(complete);
                    continue;
                }
                if (frame.type->kind == Type::Kind::Record) {
                    frame.record = program_.record_definition(frame.type->nominal_key());
                    frame.any = frame.type->is_union;
                    if (!frame.record) co_return false;
                } else if (frame.type->kind == Type::Kind::Array || frame.type->kind == Type::Kind::Vector) {
                    frame.stride = co_await meta_object_size_async(frame.type->element);
                    if (!frame.stride) co_return false;
                } else co_return false;
            }
            Frame next;
            if (frame.record) {
                while (frame.next < frame.record->members.size() &&
                       frame.record->members[frame.next].name.empty()) ++frame.next;
                if (frame.next == frame.record->members.size()) {
                    finish(!frame.any);
                    continue;
                }
                const auto& member = frame.record->members[frame.next++];
                next.type = member.type;
                const auto member_name = member.member_name();
                std::optional<EvaluationMemberLayout> layout;
                if (program_.evaluation_member_layout)
                    layout = co_await program_.evaluation_member_layout.async(frame.type, member_name);
                if (!layout || layout->offset > *frame.size) co_return false;
                next.offset = frame.offset + static_cast<std::size_t>(layout->offset);
                if (layout->bit_width)
                    next.field = EvalMetaPointer::BitField{*layout->bit_width, layout->bit_offset, {}, 0};
            } else {
                if (frame.next == frame.type->lanes) {
                    finish(true);
                    continue;
                }
                next.type = frame.type->element;
                next.offset = frame.offset + frame.next++ * *frame.stride;
            }
            work.push_back(std::move(next));
        }
        co_return child.value_or(false);
    }

    EvaluationTask<bool> validate_meta_object_value_async(const EvalValue& base, SourceLocation location) {
        // One whole-object proof establishes its selected subobjects. Keep the
        // actual target/member projections, not a repeated search from the root
        // typed object for every descendant. Leaf representation checks remain.
        struct Frame {
            EvalValue base;
            MetaObjectAccess access;
            std::optional<std::size_t> index, size, stride, alignment, parent_alignment;
            std::shared_ptr<const RecordDecl> record;
            std::size_t next{};
            Frame(EvalValue value, MetaObjectAccess proof)
                : base(std::move(value)), access(proof) {}
        };
        std::vector<Frame> work;
        work.emplace_back(base, MetaObjectAccess::CheckAggregate);
        while (!work.empty()) {
            auto& frame = work.back();
            const auto type = frame.base.type->pointee;
            if (!frame.index) {
                frame.index = (co_await meta_access_index_async(frame.base, location));
                if (!frame.index) co_return false;
                if (meta_volatile_or_atomic(type)) {
                    fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
                    co_return false;
                }
                if (meta_scalar_type(type) || type->kind == Type::Kind::Pointer || is_label_type(type)) {
                    if (!(co_await read_meta_pointer_async(frame.base, location, frame.access))) co_return false;
                    work.pop_back();
                    continue;
                }
                if (frame.access == MetaObjectAccess::CheckAggregate &&
                    !(co_await meta_record_effective_access_async(frame.base, location))) co_return false;
                // Copy a union's representation; its selected-member read is
                // responsible for checking inactive/indeterminate storage.
                if (type->kind == Type::Kind::Record && type->is_union) {
                    work.pop_back();
                    continue;
                }
                frame.size = (co_await meta_object_size_async(type));
                if (!frame.size) co_return false;
                if (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) {
                    frame.stride = (co_await meta_object_size_async(type->element));
                    frame.alignment = (co_await meta_object_alignment_async(type->element));
                    if (!frame.stride || !frame.alignment) co_return false;
                    if (type->kind == Type::Kind::Vector && frame.base.meta_pointer->mutable_buffer &&
                        !frame.base.meta_pointer->union_member_view && !may_alias(type)) {
                        const auto& tags = frame.base.meta_pointer->mutable_buffer->effective_type;
                        for (std::size_t byte = 0; byte < *frame.size; ++byte) {
                            const auto tag = tags[*frame.index + byte];
                            if (tag != 0 && !compatible_meta_type(
                                    static_cast<BuiltinType>(tag - 1), type->element->builtin)) {
                                fail(location, "meta pointer read violates effective type");
                                co_return false;
                            }
                        }
                    }
                } else if (type->kind == Type::Kind::Record) {
                    frame.record = program_.record_definition(type->nominal_key());
                    if (!frame.record) co_return false;
                } else co_return false;
            }
            TypePtr child;
            std::size_t offset{}, size{}, alignment{};
            std::optional<EvalMetaPointer::BitField> field;
            if (frame.record) {
                while (frame.next < frame.record->members.size() &&
                       frame.record->members[frame.next].name.empty()) ++frame.next;
                if (frame.next == frame.record->members.size()) {
                    work.pop_back();
                    continue;
                }
                const auto& member = frame.record->members[frame.next++];
                std::optional<EvaluationMemberLayout> layout;
                if (program_.evaluation_member_layout)
                    layout = co_await program_.evaluation_member_layout.async(type, member.member_name());
                std::optional<std::size_t> member_alignment;
                if (meta_object_type(member.type))
                    member_alignment = co_await meta_object_alignment_async(member.type);
                const auto member_size = (co_await meta_object_size_async(member.type));
                if (!layout || !member_alignment || !member_size || layout->offset > *frame.size ||
                    *member_size > *frame.size - layout->offset) {
                    fail(location, "meta aggregate requires supported complete object members");
                    co_return false;
                }
                if (layout->bit_width) {
                    if (*layout->bit_width == 0 || *layout->bit_width > *member_size * 8U ||
                        layout->bit_offset > *member_size * 8U - *layout->bit_width) {
                        fail(location, "meta bit-field has invalid target layout");
                        co_return false;
                    }
                    field = EvalMetaPointer::BitField{*layout->bit_width,
                        layout->bit_offset, clone_type(type), *frame.index};
                }
                child = member.type;
                offset = static_cast<std::size_t>(layout->offset);
                size = *member_size;
                alignment = std::min<std::size_t>(layout->alignment, *member_alignment);
            } else {
                if (frame.next == type->lanes) {
                    work.pop_back();
                    continue;
                }
                child = type->element;
                offset = frame.next++ * *frame.stride;
                size = *frame.stride;
                alignment = *frame.alignment;
            }
            if (!frame.parent_alignment) {
                frame.parent_alignment = frame.base.meta_pointer->access_alignment;
                if (!frame.parent_alignment) frame.parent_alignment = (co_await meta_object_alignment_async(type));
                if (!frame.parent_alignment) co_return false;
            }
            EvalValue value = frame.base;
            value.type = pointer_type(clone_type(child));
            value.type->pointee->may_alias = value.type->pointee->may_alias || may_alias(type);
            auto& pointer = *value.meta_pointer;
            pointer.view_offset = *frame.index + offset;
            pointer.view_length = size;
            pointer.position = 0;
            pointer.access_alignment = std::min(*frame.parent_alignment, alignment);
            pointer.bit_field = std::move(field);
            work.emplace_back(std::move(value), MetaObjectAccess::RetainedSubobject);
        }
        co_return true;
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

    EvaluationTask<bool> sized_meta_pointer_async(const EvalValue& value, SourceLocation location) {
        if (!live_meta_pointer(value, location)) co_return false;
        if (!meta_object_type(value.type->pointee) ||
            !(co_await meta_object_size_async(value.type->pointee))) {
            fail(location, "meta pointer arithmetic requires a supported complete object type");
            co_return false;
        }
        co_return true;
    }

    EvaluationTask<std::optional<std::size_t>> meta_access_index_async(const EvalValue& base,
                                                  SourceLocation location,
                                                  bool write = false) {
        if (!(co_await sized_meta_pointer_async(base, location))) co_return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = *(co_await meta_object_size_async(base.type->pointee));
        if (pointer.position > pointer.view_length ||
            size > pointer.view_length - pointer.position) {
            fail(location, write ? "meta pointer write is outside its view"
                                 : "meta pointer read is outside its view");
            co_return std::nullopt;
        }
        const auto index = pointer.view_offset + pointer.position;
        auto alignment = pointer.access_alignment;
        if (!alignment) alignment = co_await meta_object_alignment_async(base.type->pointee);
        if (!alignment || index % *alignment != 0) {
            fail(location, base.type->pointee->kind == Type::Kind::Vector
                ? "misaligned meta pointer access for target vector type"
                : base.type->pointee->kind == Type::Kind::Record
                    ? "misaligned meta pointer access for target record type"
                : "misaligned meta pointer access for target scalar type");
            co_return std::nullopt;
        }
        co_return index;
    }

    EvaluationTask<std::optional<EvalValue>> decay_meta_array_async(EvalValue base,
                                              SourceLocation location) {
        if (!(co_await sized_meta_pointer_async(base, location)) ||
            base.type->pointee->kind != Type::Kind::Array) co_return std::nullopt;
        auto& pointer = *base.meta_pointer;
        const auto size = *(co_await meta_object_size_async(base.type->pointee));
        if (pointer.position > pointer.view_length ||
            size > pointer.view_length - pointer.position) {
            fail(location, "meta pointer read is outside its view");
            co_return std::nullopt;
        }
        const auto offset = pointer.view_offset + pointer.position;
        auto alignment = pointer.access_alignment;
        if (!alignment) alignment = co_await meta_object_alignment_async(base.type->pointee);
        if (!alignment || offset % *alignment != 0) {
            fail(location, "misaligned meta pointer access for target scalar type");
            co_return std::nullopt;
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
        co_return base;
    }

    EvaluationTask<std::optional<EvalValue>> meta_member_pointer_async(
        const Expr& expression) {
        if (!expression.left || !expression.right ||
            expression.right->kind != Expr::Kind::Name)
            co_return std::nullopt;
        std::optional<EvalValue> base;
        if (expression.text == "member") base = co_await meta_designator_pointer_async(*expression.left);
        else base = co_await this->expression_async(*expression.left);
        bool sized = base && base->meta_pointer;
        if (sized) sized = co_await sized_meta_pointer_async(*base, expression.location);
        if (!sized || base->type->pointee->kind != Type::Kind::Record) {
            fail(expression.location,
                 "meta record member access requires a supported structure pointer");
            co_return std::nullopt;
        }
        if (!(co_await meta_record_effective_access_async(*base, expression.location)))
            co_return std::nullopt;
        const auto* member = selected_record_member(expression);
        std::optional<EvaluationMemberLayout> layout;
        if (program_.evaluation_member_layout)
            layout = co_await program_.evaluation_member_layout.async(
                base->type->pointee, member_name(*expression.right));
        if (!member || !layout ||
            !meta_object_type(member->type) ||
            (layout->bit_width && !is_integer(member->type)) ||
            meta_volatile_or_atomic(member->type)) {
            fail(expression.location,
                 "meta record member requires a supported non-volatile object type");
            co_return std::nullopt;
        }
        const auto member_size = (co_await meta_object_size_async(member->type));
        const auto record_size = (co_await meta_object_size_async(base->type->pointee));
        if (!member_size || !record_size ||
            layout->offset > *record_size ||
            *member_size > *record_size - layout->offset) {
            fail(expression.location, "meta record member is outside its target layout");
            co_return std::nullopt;
        }
        const auto member_alignment = (co_await meta_object_alignment_async(member->type));
        const auto record_alignment =
            (co_await meta_object_alignment_async(base->type->pointee));
        if (!member_alignment || !record_alignment) {
            fail(expression.location, "meta record alignment is unavailable");
            co_return std::nullopt;
        }
        auto& pointer = *base->meta_pointer;
        if (pointer.position > pointer.view_length ||
            *record_size > pointer.view_length - pointer.position) {
            fail(expression.location, "meta pointer read is outside its view");
            co_return std::nullopt;
        }
        const auto record_offset = pointer.view_offset + pointer.position;
        const auto effective_record_alignment = pointer.access_alignment
            .value_or(*record_alignment);
        if (record_offset % effective_record_alignment != 0) {
            fail(expression.location, "misaligned meta pointer access for target record type");
            co_return std::nullopt;
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
                co_return std::nullopt;
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
        type->may_alias = type->may_alias || may_alias(base->type->pointee);
        auto pointer_type_value = pointer_type(type);
        pointer_type_value->address_space = base->type->address_space;
        base->type = std::move(pointer_type_value);
        co_return base;
    }

    EvaluationTask<std::optional<EvalValue>> meta_designator_pointer_async(const Expr& source) {
        const Expr* designator = &source;
        while (designator->kind == Expr::Kind::Parenthesized &&
               designator->left)
            designator = designator->left.get();
        if (designator->kind == Expr::Kind::Name) {
            auto* cell = lookup_mutable(*designator);
            if (cell && !translation_cell(*cell, designator->location)) co_return std::nullopt;
            if (cell && !cell->value.object && meta_object_type(cell->value.type)) {
                if (!cell->storage) {
                    auto storage = (co_await new_object_async(cell->value.type, designator->location));
                    if (!storage) co_return std::nullopt;
                    cell->storage = storage->object;
                    if (cell->initialized) {
                        auto pointer = object_pointer(*storage);
                        pointer.type->pointee->is_const = false;
                        if (!(co_await store_meta_pointer_async(pointer, cell->value, designator->location))) co_return std::nullopt;
                    }
                }
                EvalValue value{UInt128{}, cell->value.type};
                value.object = cell->storage;
                co_return object_pointer(value);
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
                co_return value;
            }
        }
        if (designator->kind == Expr::Kind::Unary &&
            designator->text == "*" && designator->left)
            co_return (co_await this->expression_async(*designator->left));
        if (designator->kind == Expr::Kind::Binary &&
            (designator->text == "member" ||
             designator->text == "pointer_member"))
            co_return (co_await meta_member_pointer_async(*designator));
        if (designator->kind == Expr::Kind::Binary &&
            designator->text == "index" && designator->left &&
            designator->right) {
            const auto owner = expression_type(*designator->left);
            if (owner && owner->kind == Type::Kind::Vector)
                co_return (co_await meta_vector_lane_pointer_async(*designator));
            auto base = (co_await this->expression_async(*designator->left));
            auto index = (co_await this->expression_async(*designator->right));
            if (!base || !index || !base->meta_pointer) co_return std::nullopt;
            co_return (co_await meta_pointer_offset_async(*base, *index, false,
                                       designator->location));
        }
        auto value = (co_await this->expression_async(*designator));
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
            co_return pointer_value;
        }
        co_return std::nullopt;
    }

    EvaluationTask<std::optional<EvalValue>> meta_vector_lane_pointer_async(const Expr& source) {
        if (!source.left || !source.right) co_return std::nullopt;
        auto base = (co_await meta_designator_pointer_async(*source.left));
        if (!base) {
            auto value = (co_await this->expression_async(*source.left));
            if (!value || !value->object || !value->type ||
                value->type->kind != Type::Kind::Vector) co_return std::nullopt;
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
            base->type->pointee->kind != Type::Kind::Vector) co_return std::nullopt;
        const auto vector_type = base->type->pointee;
        const auto offset = (co_await meta_access_index_async(*base, source.location));
        if (!offset) co_return std::nullopt;
        auto lane = (co_await this->expression_async(*source.right));
        if (!lane || !is_integer(lane->type) ||
            integer_negative(lane->integer, integer_type(lane->type)) ||
            lane->integer.high != 0 || lane->integer.low >= vector_type->lanes) {
            fail(source.right->location, "meta vector lane index is outside its view");
            co_return std::nullopt;
        }
        const auto lane_size = (co_await meta_object_size_async(vector_type->element));
        const auto lane_alignment = (co_await meta_object_alignment_async(vector_type->element));
        if (!lane_size || !lane_alignment) co_return std::nullopt;
        auto& pointer = *base->meta_pointer;
        pointer.view_offset = *offset +
            static_cast<std::size_t>(lane->integer.low) * *lane_size;
        pointer.view_length = *lane_size;
        pointer.position = 0;
        pointer.access_alignment = std::min<std::size_t>(
            pointer.access_alignment.value_or(*(co_await meta_object_alignment_async(vector_type))),
            *lane_alignment);
        pointer.bit_field.reset();
        auto element = clone_type(vector_type->element);
        element->is_const = element->is_const || vector_type->is_const;
        base->type = pointer_type(element);
        co_return base;
    }

    EvaluationTask<std::optional<EvalValue>> meta_pointer_offset_async(EvalValue base,
                                                 const EvalValue& index,
                                                 bool subtract,
                                                 SourceLocation location) {
        if (!(co_await sized_meta_pointer_async(base, location))) co_return std::nullopt;
        if (!is_integer(index.type)) {
            fail(location, "meta pointer offset requires an integer");
            co_return std::nullopt;
        }
        const auto type = integer_type(index.type);
        const bool negative = integer_negative(index.integer, type);
        const auto magnitude = negative
            ? mask_to(negate(index.integer), type.bits) : index.integer;
        if (magnitude.high != 0 ||
            magnitude.low > std::numeric_limits<std::size_t>::max()) {
            fail(location, "meta pointer offset is outside its view");
            co_return std::nullopt;
        }
        const auto amount = static_cast<std::size_t>(magnitude.low);
        auto& pointer = *base.meta_pointer;
        const bool backwards = subtract != negative;
        const auto available = backwards ? pointer.position
            : pointer.view_length - pointer.position;
        const auto stride = *(co_await meta_object_size_async(base.type->pointee));
        if (amount > available / stride) {
            fail(location, "meta pointer offset is outside its view");
            co_return std::nullopt;
        }
        pointer.position = backwards ? pointer.position - amount * stride
                                     : pointer.position + amount * stride;
        co_return base;
    }

    EvaluationTask<std::optional<EvalValue>> compare_meta_pointers_async(const EvalValue& left,
                                                   const EvalValue& right,
                                                   std::string_view operation,
                                                   SourceLocation location) {
        if (!live_meta_pointer(left, location) ||
            !live_meta_pointer(right, location)) co_return std::nullopt;
        const auto& a = *left.meta_pointer;
        const auto& b = *right.meta_pointer;
        const bool same_backing = a.immutable
            ? a.immutable == b.immutable
            : a.mutable_buffer && a.mutable_buffer == b.mutable_buffer;
        const auto a_position = a.view_offset + a.position;
        const auto b_position = b.view_offset + b.position;
        if (operation == "==" || operation == "!=") {
            const bool equal = same_backing && a_position == b_position;
            co_return EvalValue{UInt128{operation == "==" ? equal : !equal},
                             builtin_type(BuiltinType::Bool)};
        }
        if (!(co_await sized_meta_pointer_async(left, location)) ||
            !(co_await sized_meta_pointer_async(right, location))) co_return std::nullopt;
        bool one_view = same_backing && a.view_offset == b.view_offset &&
            a.view_length == b.view_length;
        if (one_view)
            one_view = (co_await meta_object_size_async(left.type->pointee)) ==
                (co_await meta_object_size_async(right.type->pointee));
        if (!one_view ||
            (!compatible_pointee(left.type->pointee, right.type->pointee) &&
             !compatible_pointee(right.type->pointee, left.type->pointee)) ||
            left.type->address_space != right.type->address_space) {
            fail(location, "meta pointer ordering or subtraction requires one compatible view");
            co_return std::nullopt;
        }
        if (operation == "-") {
            const auto stride = *(co_await meta_object_size_async(left.type->pointee));
            const auto distance = a_position > b_position
                ? a_position - b_position : b_position - a_position;
            if (distance % stride != 0) {
                fail(location, "meta pointer difference is not a whole target element");
                co_return std::nullopt;
            }
            const auto magnitude = UInt128{distance / stride};
            const auto result = a_position < b_position
                ? mask_to(negate(magnitude), program_.address_bits) : magnitude;
            co_return EvalValue{result, builtin_type(BuiltinType::Iptr)};
        }
        const bool result = operation == "<" ? a_position < b_position
            : operation == "<=" ? a_position <= b_position
            : operation == ">" ? a_position > b_position
            : a_position >= b_position;
        co_return EvalValue{UInt128{result}, builtin_type(BuiltinType::Bool)};
    }

    EvaluationTask<std::optional<EvalValue>> read_meta_pointer_async(const EvalValue& base,
        SourceLocation location, MetaObjectAccess access = MetaObjectAccess::CheckAggregate) {
        if (base.type && base.type->pointee &&
            (base.type->pointee->kind == Type::Kind::Pointer || is_label_type(base.type->pointee))) {
            const auto index = (co_await meta_access_index_async(base, location));
            if (!index || (access == MetaObjectAccess::CheckAggregate &&
                !(co_await meta_record_effective_access_async(base, location)))) co_return std::nullopt;
            const auto size = *(co_await meta_object_size_async(base.type->pointee));
            const auto& pointer = *base.meta_pointer;
            UInt128 bits;
            for (std::size_t byte = 0; byte < size; ++byte) {
                if (pointer.mutable_buffer) {
                    if (pointer.mutable_buffer->assigned[*index + byte] != 0xffU) {
                        fail(location, "read of unassigned buffer byte");
                        co_return std::nullopt;
                    }
                    const auto tag = pointer.mutable_buffer->effective_type[*index + byte];
                    if (!pointer.union_member_view && !may_alias(base.type->pointee) &&
                        tag != 0 && tag != 0xffU) {
                        fail(location, "meta pointer read violates effective type");
                        co_return std::nullopt;
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
                        co_return std::nullopt;
                    }
                    auto value = *slot.value;
                    if (slot.has_backing) {
                        value.meta_pointer->mutable_buffer = slot.backing.lock();
                        if (!value.meta_pointer->mutable_buffer) {
                            fail(location, "translation-time pointer refers to an object outside its lifetime");
                            co_return std::nullopt;
                        }
                    }
                    co_return (co_await convert_async(value, base.type->pointee, location, pointer.union_member_view));
                }
            }
            if (is_label_type(base.type->pointee)) {
                if (!flat_code_addresses(location)) co_return std::nullopt;
                co_return EvalValue{mask_to(bits, program_.address_bits), clone_type(base.type->pointee)};
            }
            EvalValue value{UInt128{}, clone_type(base.type->pointee)};
            value.address = AddressConstant{AddressConstant::Kind::Absolute, bits};
            if (pointer_resolver_)
                co_return co_await resolve_pointer_async(value_expression(value, location), value.type);
            co_return value;
        }
        if (base.type && base.type->kind == Type::Kind::Pointer &&
            base.type->pointee &&
            base.type->pointee->kind == Type::Kind::Array)
            co_return (co_await decay_meta_array_async(base, location));
        if (base.type && base.type->kind == Type::Kind::Pointer &&
            base.type->pointee &&
            (base.type->pointee->kind == Type::Kind::Vector ||
             base.type->pointee->kind == Type::Kind::Record)) {
            const auto index = (co_await meta_access_index_async(base, location));
            if (!index) co_return std::nullopt;
            if (!(co_await validate_meta_object_value_async(base, location))) co_return std::nullopt;
            const auto size = *(co_await meta_object_size_async(base.type->pointee));
            const auto& pointer = *base.meta_pointer;
            if (size > std::numeric_limits<std::size_t>::max() / 3 ||
                !charge_meta_bytes(size * 3, location)) co_return std::nullopt;
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
                        co_return std::nullopt;
                    }
                    if (!charge_meta_bytes(64 + type_name(slot.value->type).size(), location)) co_return std::nullopt;
                    auto copy = slot;
                    copy.offset -= *index;
                    value.object->pointers.push_back(std::move(copy));
                }
            } else {
                value.object->assigned.assign(size, 0xffU);
                value.object->effective_type.assign(size, 0);
            }
            if (!(co_await stamp_meta_object_types_async(*value.object, value.type, 0, location)))
                co_return std::nullopt;
            if (!(co_await register_meta_record_async(*value.object, 0, value.type, location)))
                co_return std::nullopt;
            co_return value;
        }
        if (!scalar_meta_pointer(base, location)) co_return std::nullopt;
        const auto index = (co_await meta_access_index_async(base, location));
        if (!index) co_return std::nullopt;
        const auto& pointer = *base.meta_pointer;
        const auto size = meta_scalar_size(base);
        const auto access_type = base.type->pointee->builtin;
        if (pointer.mutable_buffer) {
            for (const auto& slot : pointer.mutable_buffer->pointers) {
                if (*index < slot.offset + slot.length && slot.offset < *index + size) {
                    fail(location, slot.value->label_address
                        ? "opaque code label representation cannot be inspected as bytes or scalars"
                        : "opaque translation-time pointer representation cannot be inspected as bytes or scalars");
                    co_return std::nullopt;
                }
            }
        }
        if (access == MetaObjectAccess::CheckAggregate &&
            !byte_meta_type(access_type) && !pointer.bit_field &&
            !(co_await meta_record_effective_access_async(base, location))) co_return std::nullopt;
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
                    co_return std::nullopt;
                }
                const auto tag = pointer.mutable_buffer->effective_type[*index + offset];
                bool typed = required != 0 && !pointer.union_member_view &&
                    !may_alias(base.type->pointee);
                if (typed && pointer.bit_field) typed = !(co_await meta_bit_field_record_view_async(pointer));
                if (typed && !byte_meta_type(access_type) && tag != 0 &&
                    !compatible_meta_type(static_cast<BuiltinType>(tag - 1),
                                          access_type)) {
                    fail(location, "meta pointer read violates effective type");
                    co_return std::nullopt;
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
            co_return std::nullopt;
        }
        if (access_type == BuiltinType::F80) {
            result = mask_to(result, 80);
            const auto exponent = (result.high >> 0) & 0x7fffU;
            if (bit(result, 63) != (exponent != 0)) {
                fail(location, "invalid f80 representation in meta storage");
                co_return std::nullopt;
            }
        }
        if (is_floating(base.type->pointee))
            co_return EvalValue{floating::Value{
                result,
                floating_format(access_type, program_.address_bits)},
                builtin_type(access_type)};
        co_return EvalValue{result, clone_type(base.type->pointee)};
    }

    std::optional<EvalValue> read_string_byte(const EvalValue& pointer, UInt128 index,
                                             SourceLocation location) {
        const auto pointee = pointer.type && pointer.type->kind == Type::Kind::Pointer
            ? pointer.type->pointee : TypePtr{};
        if (!pointer.string || !pointee) return std::nullopt;
        if (pointee->is_volatile || pointee->is_atomic) {
            fail(location, "volatile or atomic access is not permitted during translation-time evaluation");
            return std::nullopt;
        }
        // A literal is declared byte storage, not untyped allocated backing.
        // A wider pointer cast must not silently turn its first byte into the
        // claimed wider value. Signed/unsigned byte views preserve their type.
        if (pointee->kind != Type::Kind::Builtin || !byte_meta_type(pointee->builtin)) {
            fail(location, "typed access is incompatible with translation-time string byte backing");
            return std::nullopt;
        }
        if (pointer.offset >= pointer.string->size() || index.high ||
            index.low >= pointer.string->size() - pointer.offset) {
            fail(location, "translation-time pointer index is out of bounds");
            return std::nullopt;
        }
        const auto offset = pointer.offset + static_cast<std::size_t>(index.low);
        return EvalValue{UInt128{static_cast<unsigned char>((*pointer.string)[offset])},
                         builtin_type(pointee->builtin)};
    }

    EvaluationTask<std::optional<EvalValue>> unary_async(const Expr& expression) {
        if (!expression.left) co_return std::nullopt;
        if (expression.text == "*") {
            auto pointer = (co_await this->expression_async(*expression.left));
            if (pointer && pointer->meta_pointer)
                co_return (co_await read_meta_pointer_async(*pointer, expression.location));
            if (pointer && pointer->string)
                co_return read_string_byte(*pointer, UInt128{}, expression.location);
            if (pointer_resolver_)
                fail(expression.location,
                     "runtime/static storage cannot be read during translation-time evaluation");
            co_return std::nullopt;
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
                auto pointer = (co_await meta_designator_pointer_async(*designator));
                if (pointer && pointer->meta_pointer && pointer->meta_pointer->bit_field) {
                    fail(expression.location, "cannot take the address of a bit-field");
                    co_return std::nullopt;
                }
                co_return pointer;
            }
            if (designator->kind == Expr::Kind::Unary && designator->text == "*" && designator->left)
                co_return co_await this->expression_async(*designator->left);
            if (!pointer_resolver_) co_return std::nullopt;
            auto source = clone_expr(expression);
            source->left = (co_await address_designator_async(*expression.left));
            if (!source->left) co_return std::nullopt;
            auto value = co_await resolve_pointer_async(std::move(source), expression_type(expression));
            if (value) value->function_designator = direct_function(expression) != nullptr;
            co_return value;
        }
        if (expression.text == "!") {
            const auto type = expression_type(*expression.left);
            if (type && type->kind == Type::Kind::Pointer) {
                auto value = (co_await this->expression_async(*expression.left));
                if (!value) co_return std::nullopt;
                value = (co_await convert_async(*value, builtin_type(BuiltinType::Bool), expression.location));
                co_return value ? std::optional<EvalValue>(EvalValue{UInt128{!value->truthy()}, value->type}) : std::nullopt;
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
            auto value = (co_await assign_async(assignment, &previous));
            co_return value && expression.text.starts_with("post") ? previous : value;
        }
        auto value = (co_await this->expression_async(*expression.left));
        if (value && value->object && is_vector(value->type)) {
            const auto result_type = expression_type(expression);
            std::optional<EvalValue> result;
            if (result_type) result = co_await new_object_async(result_type, expression.location);
            if (!result) co_return std::nullopt;
            for (std::size_t lane = 0; lane < value->type->lanes; ++lane) {
                if (!step(expression.location)) co_return std::nullopt;
                auto scalar = (co_await read_meta_pointer_async((co_await vector_lane_pointer_async(*value, lane)), expression.location));
                if (!scalar) co_return std::nullopt;
                std::optional<EvalValue> updated;
                if (expression.text == "!") updated = EvalValue{scalar->truthy() ? UInt128{}
                    : mask_to(bit_not(UInt128{}), integer_type(result_type->element).bits), result_type->element};
                else if (expression.text == "-") {
                    if (scalar->floating)
                        updated = EvalValue{floating::negate(*scalar->floating), result_type->element};
                    else updated = co_await scalar_binary_values_async("-",
                        EvalValue{UInt128{}, builtin_type(BuiltinType::I32)}, *scalar, expression.location);
                }
                else if (expression.text == "+") updated = (co_await convert_async(*scalar, result_type->element, expression.location));
                else if (expression.text == "~" && is_integer(scalar->type)) {
                    scalar = (co_await convert_async(*scalar, result_type->element, expression.location));
                    if (scalar) updated = EvalValue{mask_to(bit_not(scalar->integer),
                        integer_type(result_type->element).bits), result_type->element};
                }
                if (!updated || !(co_await store_meta_pointer_async((co_await vector_lane_pointer_async(*result, lane)), updated,
                                                     expression.location))) co_return std::nullopt;
            }
            co_return result;
        }
        if (!value || value->pointer() || value->tokens || value->bytes ||
            value->buffer || value->object) co_return std::nullopt;
        if (value->floating) {
            if (expression.text == "+") co_return value;
            if (expression.text == "-") {
                value->floating = floating::negate(*value->floating);
                co_return value;
            }
            if (expression.text == "!") {
                co_return EvalValue{UInt128{!value->truthy()},
                                 builtin_type(BuiltinType::Bool)};
            }
            co_return std::nullopt;
        }
        value = (co_await convert_async(*value, builtin_integer(promote_integer(integer_type(value->type))), expression.location));
        if (!value) co_return std::nullopt;
        if (expression.text == "+") co_return value;
        if (expression.text == "-") {
            co_return calculate(IntegerOperation::Subtract, EvalValue{UInt128{}, value->type}, *value, expression.location);
        }
        if (expression.text == "~") {
            value->integer = mask_to(bit_not(value->integer), integer_type(value->type).bits);
            co_return value;
        }
        if (expression.text == "!") {
            co_return EvalValue{value->integer == UInt128{} ? UInt128{1} : UInt128{},
                             builtin_type(BuiltinType::Bool)};
        }
        co_return std::nullopt;
    }

    EvaluationTask<std::optional<EvalValue>> binary_async(const Expr& expression) {
        if (expression.text == "pointer_member" ||
            expression.text == "member") {
            auto pointer = (co_await meta_member_pointer_async(expression));
            if (!pointer) co_return std::nullopt;
            co_return co_await read_meta_pointer_async(*pointer, expression.location);
        }
        if (expression.text == "index") {
            const auto owner = expression_type(*expression.left);
            if (owner && owner->kind == Type::Kind::Vector) {
                auto pointer = (co_await meta_vector_lane_pointer_async(expression));
                if (!pointer) co_return std::nullopt;
                co_return co_await read_meta_pointer_async(*pointer, expression.location);
            }
        }
        auto left = (co_await this->expression_async(*expression.left));
        if (!left) co_return std::nullopt;
        if (is_vector(left->type) || is_vector(expression_type(*expression.right))) {
            auto right = (co_await this->expression_async(*expression.right));
            if (!right) co_return std::nullopt;
            co_return co_await vector_binary_values_async(expression.text, *left, *right, expression.location);
        }
        if (left->tokens || left->bytes || left->buffer || left->object) {
            fail(expression.location, "meta values do not support scalar operators");
            co_return std::nullopt;
        }
        const auto operation = expression.text;
        if (operation == "&&" || operation == "||") {
            if (!known_truth(*left, expression.location)) co_return std::nullopt;
            const bool lhs = left->truthy();
            if ((operation == "&&" && !lhs) ||
                (operation == "||" && lhs)) {
                co_return EvalValue{{operation == "||", 0},
                                 builtin_type(BuiltinType::Bool)};
            }
            auto right = (co_await this->expression_async(*expression.right));
            if (!right || !known_truth(*right, expression.location)) co_return std::nullopt;
            co_return EvalValue{{right->truthy(), 0},
                             builtin_type(BuiltinType::Bool)};
        }
        auto right = (co_await this->expression_async(*expression.right));
        if (!right) co_return std::nullopt;
        if (is_label_type(left->type) || is_label_type(right->type)) {
            if ((operation == "==" || operation == "!=") &&
                is_label_type(left->type) && is_label_type(right->type)) {
                if (left->label_address && right->label_address &&
                    *left->label_address == *right->label_address)
                    co_return EvalValue{UInt128{operation == "=="}, builtin_type(BuiltinType::Bool)};
                if (!left->label_address && !right->label_address) {
                    if (!flat_code_addresses(expression.location)) co_return std::nullopt;
                    const bool equal = left->integer == right->integer;
                    co_return EvalValue{UInt128{operation == "==" ? equal : !equal}, builtin_type(BuiltinType::Bool)};
                }
                fail(expression.location, "comparison of distinct code labels depends on emitted addresses");
            } else fail(expression.location, "unsupported operation on a code label");
            co_return std::nullopt;
        }
        if (left->meta_pointer && right->meta_pointer &&
            (expression.text == "-" || expression.text == "==" ||
             expression.text == "!=" || expression.text == "<" ||
             expression.text == "<=" || expression.text == ">" ||
             expression.text == ">="))
            co_return (co_await compare_meta_pointers_async(*left, *right, expression.text,
                                         expression.location));
        if ((operation == "==" || operation == "!=") && (left->pointer() || right->pointer())) {
            const auto null = [](const EvalValue& value) {
                return value.address ? value.address->kind == AddressConstant::Kind::Absolute &&
                    value.address->absolute == value.null_address : !value.pointer() && is_integer(value.type) &&
                    value.integer == UInt128{};
            };
            if (null(*left) || null(*right)) {
                const auto& pointer = null(*left) ? *right : *left;
                if (pointer.meta_pointer && !live_meta_pointer(pointer, expression.location))
                    co_return std::nullopt;
                if (!pointer.address || pointer.address->kind == AddressConstant::Kind::Absolute) {
                    const bool equal = null(*left) && null(*right);
                    co_return EvalValue{UInt128{operation == "==" ? equal : !equal}, builtin_type(BuiltinType::Bool)};
                }
            }
            if (left->address && right->address && left->address->kind == AddressConstant::Kind::Absolute &&
                right->address->kind == AddressConstant::Kind::Absolute) {
                const bool equal = left->address->absolute == right->address->absolute;
                co_return EvalValue{UInt128{operation == "==" ? equal : !equal}, builtin_type(BuiltinType::Bool)};
            }
        }
        if (right->tokens || right->bytes || right->buffer || right->object) {
            fail(expression.location, "meta values do not support scalar operators");
            co_return std::nullopt;
        }
        if (expression.text == "index") {
            if (left->meta_pointer) {
                auto pointer = (co_await meta_pointer_offset_async(*left, *right, false,
                                                   expression.location));
                if (!pointer) co_return std::nullopt;
                co_return co_await read_meta_pointer_async(*pointer, expression.location);
            }
            if (!left->string || right->pointer()) {
                if (left->address) fail(expression.location,
                    "runtime/static storage cannot be read during translation-time evaluation");
                co_return std::nullopt;
            }
            co_return read_string_byte(*left, right->integer, expression.location);
        }
        if ((operation == "+" || operation == "-") && left->meta_pointer)
            co_return (co_await meta_pointer_offset_async(*left, *right, operation == "-",
                                       expression.location));
        if (operation == "+" && right->meta_pointer)
            co_return (co_await meta_pointer_offset_async(*right, *left, false,
                                       expression.location));
        if (pointer_resolver_ && (left->address || right->address) &&
            (operation == "+" || operation == "-")) {
            auto source = clone_expr(expression);
            source->left = value_expression(*left, expression.left->location);
            source->right = value_expression(*right, expression.right->location);
            if (!source->left || !source->right) co_return std::nullopt;
            const auto type = expression_type(expression);
            if (!type || type->kind != Type::Kind::Pointer) co_return std::nullopt;
            co_return co_await resolve_pointer_async(std::move(source), type);
        }
        if (left->pointer() || right->pointer()) {
            fail(expression.location, "emitted object addresses cannot be inspected during translation-time evaluation");
            co_return std::nullopt;
        }
        co_return (co_await scalar_binary_values_async(operation, *left, *right, expression.location));
    }

    EvaluationTask<std::optional<EvalValue>> scalar_binary_values_async(std::string_view operation,
        const EvalValue& left, const EvalValue& right, SourceLocation location) {
        if (left.pointer() || right.pointer() ||
            (!is_integer(left.type) && !is_floating(left.type)) ||
            (!is_integer(right.type) && !is_floating(right.type))) {
            fail(location, "translation-time scalar operator requires numeric operands");
            co_return std::nullopt;
        }
        if (left.floating || right.floating)
            co_return (co_await calculate_floating_async(operation, left, right, location));
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
            if (operation == token) co_return calculate(opcode, left, right, location);
        co_return std::nullopt;
    }

    EvaluationTask<std::optional<EvalValue>> store_meta_pointer_async(std::optional<EvalValue> pointer,
                                                std::optional<EvalValue> source,
                                                SourceLocation location) {
        bool sized = pointer && source;
        if (sized) sized = co_await sized_meta_pointer_async(*pointer, location);
        if (!sized || !pointer->meta_pointer->mutable_buffer || pointer->type->pointee->is_const) {
            fail(location, "meta pointer write requires mutable supported storage");
            co_return std::nullopt;
        }
        const auto& target = *pointer->meta_pointer;
        const auto offset = (co_await meta_access_index_async(*pointer, location, true));
        if (!offset) co_return std::nullopt;
        const auto extent = *(co_await meta_object_size_async(pointer->type->pointee));
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
        if (pointer->type->pointee->kind == Type::Kind::Pointer || is_label_type(pointer->type->pointee)) {
            if (!(co_await meta_record_effective_access_async(*pointer, location, true))) co_return std::nullopt;
            source = (co_await convert_async(*source, pointer->type->pointee, location));
            if (!source) co_return std::nullopt;
            const bool untyped = may_alias(pointer->type->pointee);
            for (std::size_t byte = 0; byte < extent; ++byte) {
                const auto tag = target.mutable_buffer->effective_type[*offset + byte];
                if (!target.union_member_view && !untyped && tag != 0 && tag != 0xffU) {
                    fail(location, "meta pointer write violates effective type");
                    co_return std::nullopt;
                }
            }
            if (!untyped && !(co_await register_meta_record_async(*target.mutable_buffer, *offset,
                    pointer->type->pointee, location)))
                co_return std::nullopt;
            erase_pointer_slots();
            const bool opaque = source->meta_pointer || source->string || source->label_address ||
                (source->address && source->address->kind != AddressConstant::Kind::Absolute);
            if (is_label_type(source->type) && !opaque && !flat_code_addresses(location))
                co_return std::nullopt;
            const auto bits = opaque ? UInt128{} : source->address
                ? source->address->absolute : source->integer;
            for (std::size_t byte = 0; byte < extent; ++byte) {
                const auto lane = program_.evaluation_layout.byte_order == EvaluationByteOrder::Little
                    ? byte : extent - 1 - byte;
                target.mutable_buffer->data[*offset + byte] = static_cast<char>(
                    shift_right(bits, static_cast<unsigned>(lane * 8)).low & 0xffU);
                target.mutable_buffer->assigned[*offset + byte] = 0xffU;
                if (!untyped) target.mutable_buffer->effective_type[*offset + byte] = 0xffU;
            }
            if (opaque) {
                if (!charge_meta_bytes(64 + type_name(source->type).size(), location)) co_return std::nullopt;
                auto saved = std::make_shared<EvalValue>(*source);
                EvalBuffer::PointerObject slot{*offset, extent, saved, {}, false};
                if (saved->meta_pointer && saved->meta_pointer->mutable_buffer) {
                    slot.backing = saved->meta_pointer->mutable_buffer;
                    slot.has_backing = true;
                    saved->meta_pointer->mutable_buffer.reset();
                }
                target.mutable_buffer->pointers.push_back(std::move(slot));
            }
            co_return source;
        }
        if (pointer->type->pointee->kind == Type::Kind::Record ||
            pointer->type->pointee->kind == Type::Kind::Vector) {
            if (pointer->type->pointee->kind == Type::Kind::Record &&
                !(co_await meta_record_effective_access_async(*pointer, location, true)))
                co_return std::nullopt;
            source = (co_await convert_async(*source, pointer->type->pointee,
                             location));
            if (!source || !source->object) co_return std::nullopt;
            const auto size = *(co_await meta_object_size_async(pointer->type->pointee));
            const bool untyped = may_alias(pointer->type->pointee);
            for (std::size_t byte = 0; byte < size; ++byte) {
                const auto incoming = source->object->effective_type[byte];
                const auto previous = target.mutable_buffer->effective_type[*offset + byte];
                if (!target.union_member_view && !untyped && incoming != 0 && previous != 0 &&
                    incoming != previous &&
                    (incoming == 0xffU || previous == 0xffU ||
                     !compatible_meta_type(
                         static_cast<BuiltinType>(previous - 1),
                         static_cast<BuiltinType>(incoming - 1)))) {
                    fail(location,
                         "meta pointer write violates effective type");
                    co_return std::nullopt;
                }
            }
            erase_pointer_slots();
            for (const auto& slot : source->object->pointers) {
                if (!charge_meta_bytes(64 + type_name(slot.value->type).size(), location)) co_return std::nullopt;
                auto copy = slot;
                copy.offset += *offset;
                target.mutable_buffer->pointers.push_back(std::move(copy));
            }
            for (std::size_t byte = 0; byte < size; ++byte) {
                target.mutable_buffer->data[*offset + byte] = source->object->data[byte];
                target.mutable_buffer->assigned[*offset + byte] = source->object->assigned[byte];
                if (!untyped)
                    target.mutable_buffer->effective_type[*offset + byte] =
                        source->object->effective_type[byte];
            }
            if (!untyped && !(co_await register_meta_record_async(*target.mutable_buffer, *offset,
                    pointer->type->pointee, location)))
                co_return std::nullopt;
            co_return source;
        }
        if (!scalar_meta_pointer(*pointer, location)) co_return std::nullopt;
        const auto size = meta_scalar_size(*pointer);
        const auto access_type = pointer->type->pointee->builtin;
        // Like a byte access, a may_alias access neither checks nor
        // establishes an effective type.
        const bool untyped = byte_meta_type(access_type) || may_alias(pointer->type->pointee);
        if (!untyped && !target.bit_field &&
            !(co_await meta_record_effective_access_async(*pointer, location, true))) co_return std::nullopt;
        if (!untyped && !target.union_member_view &&
            (!target.bit_field || !(co_await meta_bit_field_record_view_async(target)))) {
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
                    co_return std::nullopt;
                }
            }
        }
        source = (co_await convert_async(*source, pointer->type->pointee,
                         location));
        if (!source) co_return std::nullopt;
        if (!(co_await register_meta_record_async(*target.mutable_buffer, *offset, pointer->type->pointee, location))) co_return std::nullopt;
        erase_pointer_slots();
        const auto bits = source->floating ? source->floating->bits
                                           : source->integer;
        if (target.bit_field) {
            if (target.bit_field->owner && !target.union_member_view &&
                !(co_await meta_bit_field_record_view_async(target))) {
                const auto owner_size = (co_await meta_object_size_async(target.bit_field->owner));
                if (!owner_size) co_return std::nullopt;
                for (std::size_t byte = 0; byte < *owner_size; ++byte) {
                    const auto tag = target.mutable_buffer->effective_type[
                        target.bit_field->owner_offset + byte];
                    if (tag != 0 && !(co_await meta_object_accepts_tag_at_async(
                            target.bit_field->owner, byte, tag))) {
                        fail(location,
                             "meta pointer write violates aggregate effective type");
                        co_return std::nullopt;
                    }
                }
            }
            if (target.bit_field->owner &&
                !(co_await register_meta_record_async(*target.mutable_buffer,
                    target.bit_field->owner_offset, target.bit_field->owner,
                    location))) co_return std::nullopt;
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
            co_return EvalValue{extract_meta_bit_field(inserted, *pointer),
                             clone_type(pointer->type->pointee)};
        }
        for (std::size_t index = 0; index < size; ++index) {
            const auto lane = program_.evaluation_layout.byte_order ==
                EvaluationByteOrder::Little ? index : size - 1 - index;
            target.mutable_buffer->data[*offset + index] = static_cast<char>(
                shift_right(bits,
                    static_cast<unsigned>(lane * 8)).low & 0xffU);
            target.mutable_buffer->assigned[*offset + index] = 0xffU;
            if (!untyped)
                target.mutable_buffer->effective_type[*offset + index] =
                    static_cast<std::uint8_t>(access_type) + 1;
        }
        co_return source;

    }

    EvaluationTask<std::optional<EvalValue>> assign_async(const Expr& expression,
                                  std::optional<EvalValue>* previous_value = nullptr) {
        if (!expression.left || !expression.right) {
            co_return std::nullopt;
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
            auto pointer = (co_await meta_designator_pointer_async(*designator));
            if (!pointer || !pointer->meta_pointer) {
                fail(designator->location,
                    "translation-time assignment requires a meta data pointer");
                co_return std::nullopt;
            }
            if (!(co_await sized_meta_pointer_async(*pointer, designator->location)) ||
                pointer->type->pointee->is_const ||
                !pointer->meta_pointer->mutable_buffer) {
                fail(designator->location,
                    "meta pointer write requires mutable supported storage");
                co_return std::nullopt;
            }
            const auto offset = (co_await meta_access_index_async(*pointer, designator->location, true));
            if (!offset) co_return std::nullopt;
            std::optional<EvalValue> prior_value;
            if (expression.text != "=") {
                if (pointer->type->pointee->kind != Type::Kind::Pointer &&
                    !is_vector(pointer->type->pointee) &&
                    !scalar_meta_pointer(*pointer, designator->location)) {
                    fail(designator->location,
                         "meta aggregate compound assignment requires an implemented value operator");
                    co_return std::nullopt;
                }
                prior_value = (co_await read_meta_pointer_async(*pointer, designator->location));
                if (!prior_value) co_return std::nullopt;
                if (previous_value) *previous_value = prior_value;
            }
            auto source = (co_await this->expression_async(*expression.right));
            if (!source) co_return std::nullopt;
            if (prior_value) {
                if (prior_value->meta_pointer && expression.text != "+=" && expression.text != "-=") {
                    fail(expression.location, "pointer compound assignment requires += or -=");
                    co_return std::nullopt;
                }
                if (prior_value->meta_pointer)
                    source = co_await meta_pointer_offset_async(*prior_value, *source,
                        expression.text == "-=", expression.location);
                else if (is_vector(prior_value->type))
                    source = co_await vector_binary_values_async(
                        expression.text.substr(0, expression.text.size() - 1), *prior_value, *source, expression.location);
                else source = co_await scalar_binary_values_async(
                    expression.text.substr(0, expression.text.size() - 1), *prior_value, *source, expression.location);
                if (!source) co_return std::nullopt;
            }
            co_return (co_await store_meta_pointer_async(pointer, source, designator->location));
        }
        if (!designator || designator->kind != Expr::Kind::Name)
            co_return std::nullopt;
        auto* destination = lookup_mutable(*designator);
        if (!destination) co_return std::nullopt;
        if (!translation_cell(*destination, designator->location)) co_return std::nullopt;
        if (destination->read_only) {
            fail(expression.location, "cannot write a const cell");
            co_return std::nullopt;
        }
        const auto destination_type = destination->value.type;
        if (expression.text == "=") {
            auto source = (co_await this->expression_async(*expression.right));
            if (!source) co_return std::nullopt;
            source = (co_await convert_async(*source, destination_type, expression.location));
            if (!source) co_return std::nullopt;
            *lookup_mutable(*designator) = {*source, true, false};
            co_return source;
        }
        if (previous_value) {
            *previous_value = (co_await lookup_async(*designator));
            if (!*previous_value) co_return std::nullopt;
        }
        Expr binary_expression;
        binary_expression.kind = Expr::Kind::Binary;
        binary_expression.location = expression.location;
        binary_expression.text =
            expression.text.substr(0, expression.text.size() - 1);
        binary_expression.left = clone_expr(*expression.left);
        binary_expression.right = clone_expr(*expression.right);
        auto result = (co_await binary_async(binary_expression));
        if (!result) co_return std::nullopt;
        result = (co_await convert_async(*result, destination_type, expression.location));
        if (!result) co_return std::nullopt;
        *lookup_mutable(*designator) = {*result, true, false};
        co_return result;
    }

    EvaluationTask<std::optional<EvalValue>> call_expression_async(const Expr& expression) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            fail(expression.location, "indirect calls are not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        if (expression.left->deferred_generic_signature) {
            if (validation_phase_ == SourceValidationPhase::Definition) {
                fail_context(expression.location);
                co_return std::nullopt;
            }
            if (!program_.evaluation_prepare_expression) {
                fail(expression.location, "generic invocation preparation is unavailable");
                co_return std::nullopt;
            }
            auto prepared = clone_expr(expression);
            std::vector<std::pair<NameKey, TypePtr>> types;
            for (std::size_t scope = frame_base_; scope < scopes_.size(); ++scope)
                for (const auto& [name, cell] : scopes_[scope]) types.emplace_back(name, cell.value.type);
            const auto prepare = program_.evaluation_prepare_expression;
            if (!(co_await prepare.async(prepared, current_function_, types))) co_return std::nullopt;
            if (prepared->left->deferred_generic_signature) {
                fail_context(expression.location);
                co_return std::nullopt;
            }
            co_return co_await call_expression_async(*prepared);
        }
        if (!expression.left->text.starts_with("$::") && !direct_function(*expression.left)) {
            fail(expression.location, "indirect calls are not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        // Do not retain the large intrinsic-dispatch frame across every
        // recursive user/helper call. Tree walkers otherwise exhaust the host
        // stack on modest public trees, well before their evaluation limits.
        if (expression.left->text.starts_with("$::")) co_return (co_await intrinsic_call_expression_async(expression));
        auto prepared = co_await prepare_call_async(expression);
        if (!prepared) co_return std::nullopt;
        co_return (co_await call_async(*prepared->function, prepared->arguments, expression.location));
    }

    // Resolves a direct call to a visible function and evaluates its arguments.
    EvaluationTask<std::optional<PreparedCall>> prepare_call_async(const Expr& expression) {
        if (auto* blocked = resolve_function(
                program_, current_function_, *expression.left,
                [](const FunctionDecl& candidate) {
                    return candidate.attribute("runtime_only") != nullptr;
                })) {
            if (const auto message = function_attribute_conflict(*blocked))
                fail(blocked->location, *message);
            else
                fail(expression.location,
                     "call to runtime-only function '" + blocked->name +
                         "' cannot be evaluated during translation");
            co_return std::nullopt;
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
            co_return std::nullopt;
        }
        if (expression.arguments.size() != function->parameters.size()) {
            fail(expression.location, "function call has an invalid argument count");
            co_return std::nullopt;
        }
        std::vector<EvaluatedCallArgument> arguments;
        for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
            const auto& actual = *expression.arguments[index];
            const auto& parameter = function->parameters[index];
            EvaluatedCallArgument argument{{}, {}, actual.location};
            const auto object = expression_type(actual, false);
            if (parameter.mode != ParameterMode::In && translation_lvalue(actual)) {
                // Form the designator once even for a nonmodifiable lvalue.
                // In particular, out does not read an uninitialized actual.
                auto pointer = co_await meta_designator_pointer_async(actual);
                if (!pointer || !pointer->meta_pointer) {
                    fail(actual.location, "runtime/static storage cannot be used as a translation-time output destination");
                    co_return std::nullopt;
                }
                if (object && !object->is_const && object->kind != Type::Kind::Array &&
                    object->kind != Type::Kind::Function)
                    argument.destination = *pointer;
                if (parameter.mode == ParameterMode::InOut)
                    argument.input = (co_await read_meta_pointer_async(*pointer, actual.location));
            } else {
                auto value = co_await expression_async(actual);
                if (!value) co_return std::nullopt;
                if (parameter.mode != ParameterMode::Out) argument.input = std::move(*value);
            }
            if (parameter.mode != ParameterMode::Out && !argument.input) co_return std::nullopt;
            arguments.push_back(std::move(argument));
        }
        co_return PreparedCall{function, std::move(arguments), expression.location};
    }

    EvaluationTask<std::optional<EvalValue>> intrinsic_call_expression_async(const Expr& expression) {
        if (const auto issue = source_patch_intrinsic_error(expression, program_, current_function_,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); })) {
            fail(issue->location, issue->message);
            co_return std::nullopt;
        }
        if (patch_intrinsic(expression)) {
            fail(expression.location, "$::patch is not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        if (const auto issue = source_staging_intrinsic_error(expression,
                [&](const Expr& operand) { return expression_type(operand); })) {
            fail(issue->location, issue->message);
            co_return std::nullopt;
        }
        // Keep recursive tree transforms off the much larger scalar/byte
        // intrinsic frame. The token overload of is_kind has its own handler.
        const auto& name = expression.left->text;
        if (atomic_builtin(name) == AtomicBuiltin::IsLockFree) {
            if (!(co_await validate_unevaluated_constraints_async(expression))) co_return std::nullopt;
            const auto type = atomic_query_type(expression,
                [&](const Expr& operand, bool decay) { return expression_type(operand, decay); });
            std::optional<bool> value;
            if (type && program_.evaluation_atomic_is_lock_free)
                value = co_await program_.evaluation_atomic_is_lock_free.async(type);
            if (!value) {
                fail(expression.location, "target capability is unavailable for this atomic query");
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{*value ? 1U : 0U}, builtin_type(BuiltinType::Bool)};
        }
        if (atomic_builtin(name) != AtomicBuiltin::None && atomic_builtin(name) != AtomicBuiltin::IsLockFree) {
            fail(expression.location, "atomic operations are not permitted during translation-time evaluation");
            co_return std::nullopt;
        }
        if (!procedural_ && (name.starts_with("$::syntax::") ||
            name == "$::meta::parse" || name == "$::meta::call_site" || name == "$::meta::gensym" ||
            name == "$::meta::token" || name == "$::meta::group" || name == "$::meta::span" ||
            name == "$::meta::spelling" || name == "$::meta::children" || name == "$::meta::delimiter" ||
            name == "$::meta::tokens" || name == "$::meta::node_span" || name == "$::meta::child_count" ||
            name == "$::meta::child" || name == "$::meta::is_kind" || name == "$::meta::is_production" ||
            name == "$::meta::is_extension" || name == "$::meta::replace_child" ||
            name == "$::meta::extension_match" || name == "$::meta::error" ||
            name == "$::meta::warning" || name == "$::meta::note")) {
            fail_context(expression.location);
            co_return std::nullopt;
        }
        const bool token_kind = name == "$::meta::is_kind" &&
            !expression.arguments.empty() && expression_type(*expression.arguments[0]) &&
            expression_type(*expression.arguments[0])->kind == Type::Kind::Tokens;
        if (procedural_ && !token_kind && (name == "$::meta::tokens" ||
            name == "$::meta::node_span" || name == "$::meta::child_count" ||
            name == "$::meta::child" || name == "$::meta::is_kind" ||
            name == "$::meta::is_production" || name == "$::meta::is_extension" ||
            name == "$::meta::replace_child" || name == "$::meta::extension_match"))
            co_return (co_await syntax_node_intrinsic_call_async(expression));
        co_return (co_await other_intrinsic_call_async(expression));
    }

    EvaluationTask<std::optional<EvalValue>> syntax_node_intrinsic_call_async(const Expr& expression) {
        const auto& name = expression.left->text;
        const auto count = name == "$::meta::tokens" || name == "$::meta::node_span" ||
            name == "$::meta::child_count" || name == "$::meta::extension_match" ? 1U :
            name == "$::meta::replace_child" ? 3U : 2U;
        if (expression.arguments.size() != count) {
            fail(expression.location, name + " requires " + std::to_string(count) + " arguments");
            co_return std::nullopt;
        }
        auto source = (co_await this->expression_async(*expression.arguments[0]));
        if (!source || !source->syntax_node) {
            fail(expression.location, name + " requires a syntax node");
            co_return std::nullopt;
        }
        const auto& node = *source->syntax_node;
        if (name == "$::meta::extension_match") {
            if (node.kind != SyntaxNode::Kind::Extension || !node.definition || !node.match) {
                fail(expression.location, "$::meta::extension_match requires an extension node");
                co_return std::nullopt;
            }
            EvalValue value{UInt128{}, syntax_match_type()};
            value.syntax_match = node.match;
            co_return value;
        }
        if (name == "$::meta::node_span") co_return span_value(node.span, expression.location);
        if (name == "$::meta::tokens") {
            TokenSequence result;
            const auto flattened = syntax_node_tokens(node);
            if (!append_tokens(result, flattened, expression.location)) co_return std::nullopt;
            co_return token_value(std::move(result), expression.location);
        }
        if (name == "$::meta::child_count")
            co_return meta_count_value(node.children.size(), expression.location, name);
        auto argument = (co_await this->expression_async(*expression.arguments[1]));
        if (!argument) co_return std::nullopt;
        if (name == "$::meta::replace_child") {
            if (is_integer(argument->type) &&
                integer_negative(argument->integer, integer_type(argument->type))) {
                fail(expression.location, "syntax child index is out of range");
                co_return std::nullopt;
            }
            if (!is_integer(argument->type) || argument->integer.high != 0 ||
                argument->integer.low > std::numeric_limits<std::size_t>::max()) {
                fail(expression.arguments[1]->location,
                     "$::meta::replace_child requires an integer child index");
                co_return std::nullopt;
            }
            auto replacement = (co_await this->expression_async(*expression.arguments[2]));
            if (!replacement || !replacement->syntax_node) {
                fail(expression.arguments[2]->location,
                     "$::meta::replace_child requires a syntax replacement node");
                co_return std::nullopt;
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
                co_return std::nullopt;
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
                    fail_resource(expression.location,
                         "public syntax replacement exceeds translation-time storage budget");
                co_return std::nullopt;
            }
            EvalValue value{UInt128{}, syntax_type()};
            value.syntax_node = std::move(replaced);
            co_return value;
        }
        if (name == "$::meta::child") {
            if (!is_integer(argument->type) ||
                integer_negative(argument->integer, integer_type(argument->type)) ||
                argument->integer.high != 0 ||
                argument->integer.low >= node.children.size()) {
                fail(expression.location, "syntax child index is out of range");
                co_return std::nullopt;
            }
            EvalValue value{UInt128{}, syntax_type()};
            value.syntax_node = node.children[static_cast<std::size_t>(argument->integer.low)];
            co_return value;
        }
        if (!argument->string || argument->offset >= argument->string->size()) {
            fail(expression.location, name + " requires a translation-time string");
            co_return std::nullopt;
        }
        const auto written = std::string_view(*argument->string).substr(argument->offset,
            argument->string->size() - argument->offset - 1);
        if (name == "$::meta::is_extension") {
            const auto resolved = syntax_resolve_entity(node, written, diagnostics_,
                expression.arguments[1]->location);
            if (!resolved) co_return std::nullopt;
            co_return EvalValue{UInt128{node.kind == SyntaxNode::Kind::Extension &&
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
                co_return std::nullopt;
            }
            co_return EvalValue{UInt128{node.kind == SyntaxNode::Kind::Core &&
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
            co_return std::nullopt;
        }
        co_return EvalValue{UInt128{node.kind == found->second}, builtin_type(BuiltinType::Bool)};
    }

    EvaluationTask<std::optional<EvalValue>> other_intrinsic_call_async(const Expr& expression) {
        if (expression.left->text == "$::syntax::error" ||
            expression.left->text == "$::syntax::warning" ||
            expression.left->text == "$::syntax::note" ||
            expression.left->text == "$::meta::error" || expression.left->text == "$::meta::warning" ||
            expression.left->text == "$::meta::note") {
            const auto& name = expression.left->text;
            if (!procedural_ || expression.arguments.size() != 2) {
                fail(expression.location, "syntax diagnostic requires a span and string message");
                co_return std::nullopt;
            }
            auto span = (co_await this->expression_async(*expression.arguments[0]));
            if (!span) co_return std::nullopt;
            auto message = (co_await this->expression_async(*expression.arguments[1]));
            if (!span->syntax_span || !message || !message->string ||
                message->offset >= message->string->size()) {
                fail(expression.location, "syntax diagnostic requires a span and string message");
                co_return std::nullopt;
            }
            const auto text = std::string_view(*message->string).substr(message->offset,
                message->string->size() - message->offset - 1);
            if (!charge_meta_bytes(32 + text.size(), expression.location)) co_return std::nullopt;
            if (name == "$::syntax::error" || name == "$::meta::error") {
                fail(span->syntax_span->first, std::string(text));
                co_return std::nullopt;
            }
            diagnostics_.report(name == "$::syntax::warning" || name == "$::meta::warning" ? DiagnosticLevel::Warning
                : DiagnosticLevel::Note, span->syntax_span->first, text);
            for (auto frame = call_stack_.rbegin(); frame != call_stack_.rend(); ++frame)
                diagnostics_.note(frame->location, "while evaluating call to '" + frame->function + "'");
            diagnostics_.note(macro_context_->definition, "expansion function is defined here");
            diagnostics_.note(macro_context_->invocation, "while executing this expansion");
            co_return EvalValue{UInt128{}, builtin_type(BuiltinType::Void)};
        }
        if (expression.left->text == "$::meta::token" || expression.left->text == "$::meta::group")
            co_return (co_await construct_tokens_async(expression));
        const bool token_kind = expression.left->text == "$::meta::is_kind" &&
            !expression.arguments.empty() && expression_type(*expression.arguments[0]) &&
            expression_type(*expression.arguments[0])->kind == Type::Kind::Tokens;
        if (token_kind || expression.left->text == "$::meta::spelling" ||
            expression.left->text == "$::meta::children" || expression.left->text == "$::meta::delimiter" ||
            expression.left->text == "$::meta::span") co_return (co_await inspect_token_async(expression));
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
                co_return std::nullopt;
            }
            auto match = (co_await this->expression_async(*expression.arguments[0]));
            if (context) {
                if (match && match->syntax_match)
                    co_return context_value(match->syntax_match->context, expression.location);
                if (match && match->syntax_node)
                    co_return context_value(match->syntax_node->context, expression.location);
                fail(expression.location, "$::syntax::context requires a syntax match or node");
                co_return std::nullopt;
            }
            if (!match || !match->syntax_match) {
                fail(expression.location, "syntax operation requires a syntax match");
                co_return std::nullopt;
            }
            if (count == 1) {
                if (name == "$::syntax::span") co_return span_value(match->syntax_match->span, expression.location);
                TokenSequence output;
                if (!append_tokens(output, match->syntax_match->input, expression.location)) co_return std::nullopt;
                co_return token_value(std::move(output), expression.location);
            }
            auto field = (co_await this->expression_async(*expression.arguments[1]));
            if (!field || !field->string) {
                fail(expression.location, "syntax field must be a translation-time string");
                co_return std::nullopt;
            }
            if (field->offset >= field->string->size()) {
                fail(expression.location, "syntax field string pointer is out of bounds");
                co_return std::nullopt;
            }
            const auto written = std::string_view(*field->string).substr(field->offset,
                field->string->size() - field->offset - 1);
            if (name == "$::syntax::is_variant") {
                if (!match->syntax_match->variant ||
                    std::find(match->syntax_match->variant_labels.begin(),
                              match->syntax_match->variant_labels.end(), written) ==
                        match->syntax_match->variant_labels.end()) {
                    fail(expression.location, "syntax choice has no variant named '" + std::string(written) + "'");
                    co_return std::nullopt;
                }
                co_return EvalValue{UInt128{*match->syntax_match->variant == written ? 1U : 0U},
                                 builtin_type(BuiltinType::Bool)};
            }
            const auto found = std::find_if(match->syntax_match->fields.begin(), match->syntax_match->fields.end(),
                [&](const SyntaxMatchValue::Field& candidate) { return candidate.name == written; });
            if (found == match->syntax_match->fields.end()) {
                fail(expression.location, "syntax match has no field named '" + std::string(written) + "'");
                co_return std::nullopt;
            }
            if (name == "$::syntax::capture_span") co_return span_value(found->span, expression.location);
            if (name == "$::syntax::capture") {
                if (found->kind != SyntaxMatchValue::Field::Kind::Primitive &&
                    found->kind != SyntaxMatchValue::Field::Kind::RawGroup) {
                    fail(expression.location, "syntax capture requires a primitive token field");
                    co_return std::nullopt;
                }
                TokenSequence output;
                if (!append_tokens(output, found->tokens, expression.location)) co_return std::nullopt;
                co_return token_value(std::move(output), expression.location);
            }
            if (name == "$::syntax::node") {
                if (!found->node) {
                    fail(expression.location, "syntax node requires a parsed or raw-group capture field");
                    co_return std::nullopt;
                }
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = found->node;
                co_return value;
            }
            if (found->kind != SyntaxMatchValue::Field::Kind::Nested) {
                fail(expression.location, "syntax count/at requires a nested record field");
                co_return std::nullopt;
            }
            if (name == "$::syntax::count")
                co_return meta_count_value(found->records.size(), expression.location, name);
            auto index = (co_await this->expression_async(*expression.arguments[2]));
            if (!index || !is_integer(index->type) ||
                integer_negative(index->integer, integer_type(index->type)) ||
                index->integer.high != 0 ||
                index->integer.low >= found->records.size()) {
                fail(expression.location, "syntax record index is out of range");
                co_return std::nullopt;
            }
            EvalValue value{UInt128{}, syntax_match_type()};
            value.syntax_match = found->records[static_cast<std::size_t>(index->integer.low)];
            co_return value;
        }
        if (procedural_ && expression.left->text == "$::meta::call_site") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::call_site requires one token value");
                co_return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = (co_await this->expression_async(argument));
            if (!value || !value->tokens || value->tokens->size() != 1U ||
                value->tokens->front().kind != TokenKind::Identifier) {
                fail(argument.location, "$::meta::call_site requires exactly one identifier token");
                co_return std::nullopt;
            }
            if (!call_context_) {
                fail(expression.location, "$::meta::call_site has no invocation context");
                co_return std::nullopt;
            }
            if (!charge_input_context(call_context_, expression.location)) co_return std::nullopt;
            auto token = value->tokens->front();
            token.origin.context = call_context_;
            token.origin.lookup_mode = TokenOrigin::LookupMode::Invocation;
            token.origin.value_binding = {};
            token.origin.declaration_source.reset();
            token.origin.value_spelling.reset();
            token.origin.fragment_lookup.reset();
            token.origin.value_context_captured = false;
            token.origin.deferred_parameter_region.reset();
            token.origin.label_binding = {};
            token.origin.tag_binding.reset();
            token.origin.alias_binding.reset();
            TokenSequence result;
            if (!append_tokens(result, {token}, expression.location)) co_return std::nullopt;
            co_return token_value(std::move(result), expression.location);
        }
        if (procedural_ && expression.left->text == "$::meta::gensym") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::gensym requires one string prefix");
                co_return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = (co_await this->expression_async(argument));
            if (!value || !value->string || value->offset >= value->string->size()) {
                fail(argument.location, "$::meta::gensym requires a translation-time string prefix");
                co_return std::nullopt;
            }
            const auto written = std::string_view(*value->string).substr(value->offset,
                value->string->size() - value->offset - 1);
            // Any string is accepted: each character that cannot appear in an
            // identifier, including a whole UTF-8 sequence, becomes `_`.
            std::string prefix;
            for (std::size_t index = 0; index < written.size(); ++index) {
                const auto ch = static_cast<unsigned char>(written[index]);
                if (std::isalnum(ch) != 0 || ch == '_') {
                    prefix.push_back(static_cast<char>(ch));
                    continue;
                }
                prefix.push_back('_');
                if (ch >= 0xC0)
                    while (index + 1 < written.size() &&
                           (static_cast<unsigned char>(written[index + 1]) & 0xC0) == 0x80)
                        ++index;
            }
            if (prefix.empty() || std::isdigit(static_cast<unsigned char>(prefix.front())) != 0 ||
                is_reserved_identifier(prefix))
                prefix.insert(prefix.begin(), '_');
            const auto source = macro_context_->invocation;
            auto fresh = std::make_shared<FreshIdentifier>();
            fresh->expansion = macro_context_->expansion;
            fresh->ordinal = ++fresh_ordinal_;
            fresh->prefix = std::string(prefix);
            fresh->source_unit = source.file ? source.file->source_unit_at(source.line) : std::string{};
            if (!charge_meta_bytes(64 + fresh->prefix.size() + fresh->source_unit.size(),
                                   expression.location)) co_return std::nullopt;
            MetaToken token;
            token.kind = TokenKind::Identifier;
            token.text = std::string(prefix);
            token.origin = {token_origin(source).span, {}, macro_context_, {}, 0, {}, fresh};
            token.origin.span_end = token_origin(source).last_span();
            TokenSequence result;
            if (!append_tokens(result, {token}, expression.location)) co_return std::nullopt;
            co_return token_value(std::move(result), expression.location);
        }
        if (procedural_ && expression.left->text == "$::meta::parse") {
            if (expression.arguments.size() == 3U) {
                auto category = (co_await this->expression_async(*expression.arguments[0]));
                auto input = (co_await this->expression_async(*expression.arguments[1]));
                auto context = (co_await this->expression_async(*expression.arguments[2]));
                if (!category || !category->string || category->offset >= category->string->size() ||
                    !input || !input->tokens || !context || !context->syntax_context) {
                    fail(expression.location, "$::meta::parse requires a category string, tokens, and context");
                    co_return std::nullopt;
                }
                const auto written = std::string_view(*category->string).substr(category->offset,
                    category->string->size() - category->offset - 1);
                const auto selected = syntax_parse_category(written);
                if (!selected) {
                    fail(expression.arguments[0]->location, "invalid public syntax parse category: '" + std::string(written) + "'");
                    co_return std::nullopt;
                }
                if (!syntax_parse_) {
                    fail(expression.location, "public syntax parsing is unavailable in this translation context");
                    co_return std::nullopt;
                }
                auto node = co_await syntax_parse_.async(*selected, *input->tokens,
                    context->syntax_context, expression.location);
                if (!node) {
                    fail(expression.location, "$::meta::parse could not recognize complete bounded input for '" + std::string(written) + "'");
                    co_return std::nullopt;
                }
                const auto storage = syntax_node_storage(*node, program_.evaluation_limits.memory);
                if (storage > program_.evaluation_limits.bytes) {
                    fail_resource(expression.location, "public syntax parse output byte budget exceeded");
                    co_return std::nullopt;
                }
                if (!charge_meta_bytes(static_cast<std::size_t>(storage), expression.location)) co_return std::nullopt;
                EvalValue value{UInt128{}, syntax_type()};
                value.syntax_node = std::move(node);
                co_return value;
            }
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::parse requires one string or bytes argument");
                co_return std::nullopt;
            }
            const auto& argument = *expression.arguments.front();
            auto value = (co_await this->expression_async(argument));
            if (!value || (!value->string && !value->bytes)) {
                fail(argument.location, "$::meta::parse requires a translation-time string or bytes");
                co_return std::nullopt;
            }
            if (value->string && value->offset >= value->string->size()) {
                fail(argument.location, "$::meta::parse string pointer is out of bounds");
                co_return std::nullopt;
            }
            const auto text = value->bytes
                ? std::string_view(*value->bytes).substr(value->byte_offset, value->byte_length)
                : std::string_view(*value->string).substr(value->offset, value->string->size() - value->offset - 1);
            auto part = parse_tokens(text, argument.location, expression);
            if (!part) co_return std::nullopt;
            co_return token_value(std::move(*part), expression.location, true);
        }
        const auto& name = expression.left->text;
        const auto byte_budget = static_cast<std::size_t>(program_.evaluation_limits.bytes);
        if (name == "$::embed") {
            const auto identity = token_origin(expression.left->location).embed;
            if (expression.arguments.size() != 1 || !identity || !identity->snapshot ||
                expression.arguments.front()->kind != Expr::Kind::String) {
                fail(expression.location, "$::embed requires one identified string-literal path");
                co_return std::nullopt;
            }
            auto& snapshot = *identity->snapshot;
            if (!snapshot.bytes) {
                std::ifstream input(snapshot.path, std::ios::binary | std::ios::ate);
                if (!input) {
                    fail(expression.location, "cannot read embedded asset '" + identity->written_path + "'");
                    co_return std::nullopt;
                }
                const auto end = input.tellg();
                if (end < 0 || static_cast<std::uint64_t>(end) > byte_budget ||
                    !fits_unsigned(UInt128{static_cast<std::uint64_t>(end)}, program_.address_bits)) {
                    if (end >= 0) mark_resource_exhausted();
                    fail(expression.location, "embedded asset exceeds the target uptr or " +
                        std::to_string(byte_budget) + "-byte limit");
                    co_return std::nullopt;
                }
                std::string data(static_cast<std::size_t>(end), '\0');
                input.seekg(0);
                if (!input || (!data.empty() && !input.read(data.data(),
                        static_cast<std::streamsize>(data.size())))) {
                    fail(expression.location, "cannot read embedded asset '" + identity->written_path + "'");
                    co_return std::nullopt;
                }
                snapshot.bytes = std::make_shared<const std::string>(std::move(data));
            }
            if (counted_asset_backings_.insert(snapshot.bytes.get()).second &&
                !charge_meta_bytes(snapshot.bytes->size(), expression.location))
                co_return std::nullopt;
            EvalValue value{UInt128{}, bytes_type()};
            value.bytes = snapshot.bytes;
            value.byte_length = value.bytes->size();
            co_return value;
        }
        if (name == "$::meta::len" || name == "$::meta::at" ||
            name == "$::meta::slice" || name == "$::meta::concat") {
            const auto count = name == "$::meta::len" ? 1U
                : name == "$::meta::at" || name == "$::meta::concat" ? 2U : 3U;
            if (expression.arguments.size() != count) {
                fail(expression.location, name + " requires " + std::to_string(count) + " arguments");
                co_return std::nullopt;
            }
            auto source = (co_await this->expression_async(*expression.arguments[0]));
            if (!source || (!source->bytes && !source->tokens)) {
                fail(expression.arguments[0]->location,
                     name + " requires $::meta::bytes or $::meta::tokens");
                co_return std::nullopt;
            }
            if (source->tokens) {
                const auto trees = token_trees(*source->tokens,
                    expression.arguments[0]->location);
                if (!trees) co_return std::nullopt;
                if (name == "$::meta::len")
                    co_return meta_count_value(trees->size(), expression.location, name);
                if (name == "$::meta::concat") {
                    auto second = (co_await this->expression_async(*expression.arguments[1]));
                    if (!second || !second->tokens) {
                        fail(expression.arguments[1]->location,
                             "$::meta::concat requires token values");
                        co_return std::nullopt;
                    }
                    TokenSequence result;
                    if (!append_tokens(result, *source->tokens, expression.location) ||
                        !append_tokens(result, *second->tokens, expression.location))
                        co_return std::nullopt;
                    co_return token_value(std::move(result), expression.location);
                }
                const auto index = (co_await this->expression_async(*expression.arguments[1]));
                if (!index || !is_integer(index->type) ||
                    integer_negative(index->integer, integer_type(index->type)) ||
                    index->integer.high != 0 || index->integer.low > trees->size()) {
                    fail(expression.arguments[1]->location,
                         name + " index is outside the token sequence");
                    co_return std::nullopt;
                }
                const auto offset = static_cast<std::size_t>(index->integer.low);
                std::size_t end = offset + 1;
                if (name == "$::meta::slice") {
                    const auto length = (co_await this->expression_async(*expression.arguments[2]));
                    if (!length || !is_integer(length->type) ||
                        integer_negative(length->integer, integer_type(length->type)) ||
                        length->integer.high != 0 ||
                        length->integer.low > trees->size() - offset) {
                        fail(expression.arguments[2]->location,
                             "$::meta::slice length is outside the token sequence");
                        co_return std::nullopt;
                    }
                    end = offset + static_cast<std::size_t>(length->integer.low);
                } else if (offset == trees->size()) {
                    fail(expression.arguments[1]->location,
                         "$::meta::at index is outside the token sequence");
                    co_return std::nullopt;
                }
                TokenSequence result;
                if (offset != end) {
                    const auto begin_token = (*trees)[offset].first;
                    const auto end_token = (*trees)[end - 1].second;
                    TokenSequence selected(source->tokens->begin() +
                        static_cast<std::ptrdiff_t>(begin_token),
                        source->tokens->begin() + static_cast<std::ptrdiff_t>(end_token));
                    if (!append_tokens(result, selected, expression.location))
                        co_return std::nullopt;
                }
                co_return token_value(std::move(result), expression.location);
            }
            if (name == "$::meta::len")
                co_return meta_count_value(source->byte_length, expression.location, name);
            if (name == "$::meta::concat") {
                auto second = (co_await this->expression_async(*expression.arguments[1]));
                if (!second || !second->bytes) {
                    fail(expression.arguments[1]->location, name + " requires $::meta::bytes");
                    co_return std::nullopt;
                }
                if (second->byte_length > byte_budget - source->byte_length ||
                    !fits_unsigned(UInt128{source->byte_length + second->byte_length},
                                   program_.address_bits)) {
                    fail_resource(expression.location, "concatenated bytes exceed the target uptr or " +
                        std::to_string(byte_budget) + "-byte limit");
                    co_return std::nullopt;
                }
                if (!charge_meta_bytes(source->byte_length + second->byte_length,
                                       expression.location)) co_return std::nullopt;
                auto result = std::make_shared<std::string>();
                result->reserve(source->byte_length + second->byte_length);
                result->append(*source->bytes, source->byte_offset, source->byte_length);
                result->append(*second->bytes, second->byte_offset, second->byte_length);
                EvalValue value{UInt128{}, bytes_type()};
                value.bytes = std::move(result);
                value.byte_length = value.bytes->size();
                co_return value;
            }
            const auto index = (co_await this->expression_async(*expression.arguments[1]));
            if (!index || !is_integer(index->type) ||
                integer_negative(index->integer, integer_type(index->type)) ||
                index->integer.high != 0 || index->integer.low > source->byte_length) {
                fail(expression.arguments[1]->location, name + " index is outside the byte sequence");
                co_return std::nullopt;
            }
            const auto offset = static_cast<std::size_t>(index->integer.low);
            if (name == "$::meta::at") {
                if (offset == source->byte_length) {
                    fail(expression.arguments[1]->location, "$::meta::at index is outside the byte sequence");
                    co_return std::nullopt;
                }
                co_return EvalValue{UInt128{static_cast<unsigned char>(
                    (*source->bytes)[source->byte_offset + offset])}, builtin_type(BuiltinType::U8)};
            }
            const auto length = (co_await this->expression_async(*expression.arguments[2]));
            if (!length || !is_integer(length->type) ||
                integer_negative(length->integer, integer_type(length->type)) ||
                length->integer.high != 0 ||
                length->integer.low > source->byte_length - offset) {
                fail(expression.arguments[2]->location, "$::meta::slice length is outside the byte sequence");
                co_return std::nullopt;
            }
            source->byte_offset += offset;
            source->byte_length = static_cast<std::size_t>(length->integer.low);
            co_return source;
        }
        if (name == "$::meta::alloc" || name == "$::meta::cap" ||
            name == "$::meta::freeze") {
            const auto count = name == "$::meta::freeze" ? 2U : 1U;
            if (expression.arguments.size() != count) {
                fail(expression.location, name + " requires " +
                    std::to_string(count) + " arguments");
                co_return std::nullopt;
            }
            if (name == "$::meta::alloc") {
                auto capacity = (co_await this->expression_async(*expression.arguments[0]));
                if (!capacity || !is_integer(capacity->type) ||
                    integer_negative(capacity->integer,
                                     integer_type(capacity->type)) ||
                    !fits_unsigned(capacity->integer, program_.address_bits) ||
                    capacity->integer.high != 0 ||
                    capacity->integer.low > byte_budget) {
                    if (capacity && is_integer(capacity->type) &&
                        !integer_negative(capacity->integer, integer_type(capacity->type)) &&
                        (capacity->integer.high != 0 || capacity->integer.low > byte_budget))
                        mark_resource_exhausted();
                    fail(expression.arguments[0]->location,
                        "$::meta::alloc capacity exceeds target uptr or " +
                        std::to_string(byte_budget) + " bytes");
                    co_return std::nullopt;
                }
                if (!charge_meta_bytes(
                        static_cast<std::size_t>(capacity->integer.low) * 3,
                        expression.location)) co_return std::nullopt;
                auto storage = std::make_shared<EvalBuffer>();
                storage->data.resize(static_cast<std::size_t>(capacity->integer.low));
                storage->assigned.resize(storage->data.size());
                storage->effective_type.resize(storage->data.size());
                EvalValue result{UInt128{}, buffer_type()};
                result.buffer = std::move(storage);
                co_return result;
            }
            auto handle = (co_await this->expression_async(*expression.arguments[0]));
            if (!handle || !handle->buffer || handle->buffer->frozen) {
                fail(expression.arguments[0]->location,
                    "$::meta::buffer handle is invalid or was used after freeze");
                co_return std::nullopt;
            }
            if (name == "$::meta::cap")
                co_return meta_count_value(handle->buffer->data.size(), expression.location, name);
            auto length = (co_await this->expression_async(*expression.arguments[1]));
            if (!length || !is_integer(length->type) ||
                integer_negative(length->integer, integer_type(length->type)) ||
                length->integer.high != 0 ||
                length->integer.low > handle->buffer->data.size()) {
                fail(expression.arguments[1]->location,
                    "$::meta::freeze length exceeds buffer capacity");
                co_return std::nullopt;
            }
            const auto count_bytes = static_cast<std::size_t>(length->integer.low);
            for (const auto& slot : handle->buffer->pointers) {
                if (slot.offset < count_bytes) {
                    fail(expression.location, slot.value->label_address
                        ? "opaque code label representation cannot be frozen as bytes"
                        : "opaque translation-time pointer representation cannot be frozen as bytes");
                    co_return std::nullopt;
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
                co_return std::nullopt;
            }
            if (!charge_meta_bytes(count_bytes, expression.location))
                co_return std::nullopt;
            auto storage = std::make_shared<const std::string>(
                handle->buffer->data.substr(0, count_bytes));
            handle->buffer->frozen = true;
            EvalValue result{UInt128{}, bytes_type()};
            result.bytes = std::move(storage);
            result.byte_length = count_bytes;
            co_return result;
        }
        if (name == "$::meta::data") {
            if (expression.arguments.size() != 1U) {
                fail(expression.location, "$::meta::data requires one meta-byte value");
                co_return std::nullopt;
            }
            auto source = (co_await this->expression_async(*expression.arguments.front()));
            if (!source || (!source->bytes && !source->buffer) ||
                (source->buffer && source->buffer->frozen)) {
                fail(expression.arguments.front()->location,
                    "$::meta::data requires live bytes or buffer storage");
                co_return std::nullopt;
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
            co_return result;
        }
        if (name.starts_with("$::meta::")) {
            fail(expression.location, name + " is not implemented for byte evaluation");
            co_return std::nullopt;
        }
        if (expression.left->text == "$::eval") {
            if (expression.arguments.size() != 1) {
                fail(expression.location,
                     "$::eval requires exactly one expression");
                co_return std::nullopt;
            }
            co_return (co_await this->expression_async(*expression.arguments.front()));
        }
        if (expression.left->text == "$::runtime") {
            fail(expression.location,
                 "$::runtime is invalid where a translation-time value is required");
            co_return std::nullopt;
        }
        fail(expression.location, "call has no visible translation-time implementation");
        co_return std::nullopt;
    }

    // Execute either a loop's ordinary entry or the continuation following a
    // partially entered body. Switch dispatch can bypass a loop's initial test
    // and initializer, but subsequent tests/increments retain ordinary order.
    EvaluationTask<Flow> loop_iterations_async(const Statement& loop, bool after_body = false) {
        const bool is_for = loop.kind == Statement::Kind::For;
        const Statement* body = is_for ? loop.second.get() : loop.first.get();
        if (!body) co_return {Flow::Failed};
        for (;;) {
            if (after_body && is_for)
                for (const auto& increment : loop.increments)
                    if (!(co_await expression_async(*increment))) co_return {Flow::Failed};
            if ((after_body || loop.kind != Statement::Kind::DoWhile) && loop.condition) {
                const auto condition = (co_await expression_async(*loop.condition));
                if (!condition || !known_truth(*condition, loop.location)) co_return {Flow::Failed};
                if (!condition->truthy()) co_return {};
            }
            const auto flow = (co_await statement_async(*body));
            if (flow.kind == Flow::Return || flow.kind == Flow::Failed) co_return flow;
            if (flow.kind == Flow::Break) co_return {};
            after_body = true; // Both normal completion and continue reach the tail.
        }
    }

    void skipped_declaration(const Statement& node) {
        if (node.kind == Statement::Kind::DeclarationList) {
            for (const auto& child : node.statements) skipped_declaration(*child);
        } else if (node.kind == Statement::Kind::Declaration && node.declaration) {
            scopes_.back()[name_key(*node.declaration)] = {
                EvalValue{UInt128{}, clone_type(node.declaration->type)}, false,
                node.declaration->type->is_const};
        }
    }

    EvaluationTask<Flow> switch_statement_async(const Statement& statement) {
        if (!statement.condition || !statement.first)
            co_return {Flow::Failed};
        auto selector = (co_await expression_async(*statement.condition));
        if (!selector || selector->pointer() || !is_integer(selector->type))
            co_return {Flow::Failed};
        const auto promoted = promote_integer(integer_type(selector->type));
        selector = (co_await convert_async(*selector, builtin_integer(promoted),
                           statement.condition->location));
        if (!selector) co_return {Flow::Failed};

        const Statement* body = statement.first.get();
        std::vector<const Statement*> labels;
        const auto collect = [&](const auto& self, const Statement* node) -> bool {
                if (!node || node->kind == Statement::Kind::Switch) return true;
                if (!step(node->location)) return false;
                if (node->kind == Statement::Kind::Case ||
                    node->kind == Statement::Kind::Default) {
                    labels.push_back(node);
                    return self(self, node->first.get());
                }
                for (const auto& child : node->statements)
                    if (!self(self, child.get())) return false;
                return self(self, node->first.get()) && self(self, node->second.get());
            };
        if (!collect(collect, body)) co_return {Flow::Failed};
        if (labels.empty()) co_return {};
        std::optional<std::size_t> selected;
        std::optional<std::size_t> fallback;
        std::vector<UInt128> values;
        for (std::size_t index = 0; index < labels.size(); ++index) {
            const auto* label = labels[index];
            if (label->kind == Statement::Kind::Default) {
                if (fallback) {
                    fail(label->location, "duplicate default label in switch");
                    co_return {Flow::Failed};
                }
                fallback = index;
                continue;
            }
            if (!label->expression) co_return {Flow::Failed};
            std::optional<EvalValue> value;
            if (const auto found = validated_cases_.find(label); found != validated_cases_.end())
                value = EvalValue{found->second.value, builtin_type(found->second.type)};
            else value = co_await expression_async(*label->expression);
            if (!value || value->pointer() || !is_integer(value->type)) {
                fail(label->location, "case requires a translation-time integer constant");
                co_return {Flow::Failed};
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
                co_return {Flow::Failed};
            }
            value = (co_await convert_async(*value, builtin_integer(promoted), label->location));
            if (!value) co_return {Flow::Failed};
            for (const auto& prior : values) {
                if (prior == value->integer) {
                    fail(label->location, "duplicate case value in switch");
                    co_return {Flow::Failed};
                }
            }
            values.push_back(value->integer);
            if (value->integer == selector->integer) selected = index;
        }
        if (!selected) selected = fallback;
        if (!selected) co_return {};
        const auto* selected_label = labels[*selected];
        std::unordered_set<const Statement*> entry_path;
        const auto locate = [&](const auto& self, const Statement* node) -> bool {
            if (!node || node->kind == Statement::Kind::Switch) return false;
            bool found = node == selected_label;
            for (const auto& child : node->statements)
                if (!found) found = self(self, child.get());
            if (!found) found = self(self, node->first.get());
            if (!found) found = self(self, node->second.get());
            if (found) entry_path.insert(node);
            return found;
        };
        if (!locate(locate, body)) co_return {Flow::Failed};
        bool active = false;
        const auto execute = [&](const auto& self, const Statement* node) -> EvaluationTask<Flow> {
            if (!node) co_return {};
            if (node == selected_label) active = true;
            if (active) co_return (co_await this->statement_async(*node));
            if (!entry_path.contains(node)) {
                skipped_declaration(*node);
                co_return {};
            }
            if (node->kind == Statement::Kind::DeclarationList) {
                for (const auto& child : node->statements) {
                    auto flow = (co_await self(self, child.get()));
                    if (flow.kind != Flow::Normal) co_return flow;
                }
                co_return {};
            }
            if (node->kind == Statement::Kind::Compound) {
                scopes_.emplace_back();
                Flow flow;
                for (const auto& child : node->statements) {
                    flow = (co_await self(self, child.get()));
                    if (flow.kind != Flow::Normal) break;
                }
                pop_scope();
                co_return flow;
            }
            if (node->kind == Statement::Kind::Case || node->kind == Statement::Kind::Default)
                co_return (co_await self(self, node->first.get()));
            if (node->kind == Statement::Kind::If)
                co_return (co_await self(self, node->first && entry_path.contains(node->first.get())
                    ? node->first.get() : node->second.get()));
            if (node->kind == Statement::Kind::While || node->kind == Statement::Kind::DoWhile ||
                node->kind == Statement::Kind::For) {
                const bool is_for = node->kind == Statement::Kind::For;
                if (is_for) {
                    scopes_.emplace_back();
                    if (node->first) skipped_declaration(*node->first);
                }
                auto flow = (co_await self(self, is_for ? node->second.get() : node->first.get()));
                if (flow.kind == Flow::Break) flow = {};
                else if (flow.kind == Flow::Normal || flow.kind == Flow::Continue)
                    flow = (co_await loop_iterations_async(*node, true));
                if (is_for) pop_scope();
                co_return flow;
            }
            fail(node->location, "cannot enter this statement through a case/default label during translation");
            co_return {Flow::Failed};
        };
        auto flow = (co_await execute(execute, body));
        co_return flow.kind == Flow::Break ? Flow{} : flow;
    }

    EvaluationTask<Flow> statement_async(const Statement& statement) {
        if (!step(statement.location)) co_return {Flow::Failed};
        switch (statement.kind) {
        case Statement::Kind::DeclarationList:
            for (const auto& child : statement.statements) {
                auto flow = (co_await this->statement_async(*child));
                if (flow.kind != Flow::Normal) co_return flow;
            }
            co_return {};
        case Statement::Kind::Compound: {
            scopes_.emplace_back();
            for (const auto& child : statement.statements) {
                auto flow = (co_await this->statement_async(*child));
                if (flow.kind != Flow::Normal) {
                    pop_scope();
                    co_return flow;
                }
            }
            pop_scope();
            co_return {};
        }
        case Statement::Kind::Declaration: {
            if (!statement.declaration) co_return {Flow::Failed};
            if (statement.declaration->storage_static) {
                fail(statement.location, "runtime/static storage cannot be used during translation-time evaluation");
                co_return {Flow::Failed};
            }
            if (statement.declaration->location_name) {
                fail(statement.location, "physical local storage cannot be used during translation-time evaluation");
                co_return {Flow::Failed};
            }
            if (scopes_.back().contains(name_key(*statement.declaration))) {
                fail(statement.location, "local cell is declared more than once in the same scope");
                co_return {Flow::Failed};
            }
            const auto prepared = validated_initializers_.find(statement.declaration.get());
            const auto* initializer_plan = prepared == validated_initializers_.end() ? nullptr : &prepared->second;
            EvalValue value{UInt128{}, clone_type(initializer_plan ? initializer_plan->type : statement.declaration->type)};
            if (statement.declaration->dynamic_array_bound) {
                auto count = (co_await expression_async(*statement.declaration->dynamic_array_bound));
                if (!count || !is_integer(count->type) ||
                    integer_negative(count->integer, integer_type(count->type)) ||
                    count->integer == UInt128{} || count->integer.high != 0 ||
                    count->integer.low > std::numeric_limits<std::uint32_t>::max() ||
                    !fits_unsigned(count->integer, program_.address_bits)) {
                    fail(statement.location, "translation-time array bound must be a positive target-sized integer");
                    co_return {Flow::Failed};
                }
                value.type->lanes = static_cast<std::uint32_t>(count->integer.low);
            }
            if ((value.type->kind == Type::Kind::Vector && !value.type->scalable) ||
                value.type->kind == Type::Kind::Record || value.type->kind == Type::Kind::Array) {
                auto object = co_await new_object_async(value.type, statement.location);
                if (!object) co_return {Flow::Failed};
                value = std::move(*object);
            }
            scopes_.back()[name_key(*statement.declaration)] = {
                value, value.object != nullptr, statement.declaration->type->is_const};
            if (statement.declaration->initializer) {
                const auto& source = *statement.declaration->initializer;
                const bool aggregate = source.kind == Expr::Kind::AggregateInitializer ||
                    (source.kind == Expr::Kind::String && value.type->kind == Type::Kind::Array);
                std::optional<EvalValue> initializer;
                if (aggregate) initializer = co_await initialize_object_async(source, value.type, initializer_plan);
                else initializer = co_await expression_async(source);
                if (!initializer) co_return {Flow::Failed};
                if (!aggregate) initializer = (co_await convert_async(*initializer, statement.declaration->type, statement.location));
                if (!initializer) co_return {Flow::Failed};
                auto& cell = scopes_.back()[name_key(*statement.declaration)];
                if (cell.value.object) {
                    // Initialization preserves an address already formed for this cell.
                    *cell.value.object = *initializer->object;
                } else if (cell.storage) {
                    EvalValue storage{UInt128{}, cell.value.type};
                    storage.object = cell.storage;
                    auto pointer = object_pointer(storage);
                    pointer.type->pointee->is_const = false;
                    if (!(co_await store_meta_pointer_async(pointer, initializer, statement.location))) co_return {Flow::Failed};
                } else cell.value = *initializer;
                cell.initialized = true;
            }
            co_return {};
        }
        case Statement::Kind::Expression:
            if (statement.expression && !(co_await expression_async(*statement.expression))) {
                co_return {Flow::Failed};
            }
            co_return {};
        case Statement::Kind::Return: {
            if (!statement.expression) co_return {Flow::Return, {}, statement.location};
            // A tail call transfers in place of this frame unless the frame or
            // the call has outputs to copy out.
            const auto* call = returned_call(statement);
            if (call && call->left &&
                std::any_of(statement.attributes.begin(), statement.attributes.end(),
                            [](const Attribute& attribute) { return attribute.name == "musttail"; }) &&
                call->left->kind == Expr::Kind::Name && !call->left->text.starts_with("$::") &&
                !call->left->deferred_generic_signature && current_function_ &&
                std::all_of(current_function_->parameters.begin(), current_function_->parameters.end(),
                            [](const ParameterDecl& parameter) {
                                return parameter.mode == ParameterMode::In;
                            })) {
                auto prepared = co_await prepare_call_async(*call);
                if (!prepared) co_return Flow{Flow::Failed};
                if (std::none_of(prepared->arguments.begin(), prepared->arguments.end(),
                                 [](const EvaluatedCallArgument& argument) {
                                     return argument.destination.has_value();
                                 })) {
                    Flow flow{Flow::Return, {}, statement.location};
                    flow.tail = std::make_shared<PreparedCall>(std::move(*prepared));
                    co_return flow;
                }
                auto value = co_await call_async(*prepared->function, prepared->arguments, call->location);
                co_return value ? Flow{Flow::Return, std::move(value), statement.location} : Flow{Flow::Failed};
            }
            auto value = (co_await expression_async(*statement.expression));
            co_return value ? Flow{Flow::Return, std::move(value), statement.location} : Flow{Flow::Failed};
        }
        case Statement::Kind::If: {
            auto condition = (co_await expression_async(*statement.condition));
            if (!condition || !known_truth(*condition, statement.location)) co_return {Flow::Failed};
            if (condition->truthy()) {
                co_return (co_await this->statement_async(*statement.first));
            }
            if (statement.second) co_return co_await this->statement_async(*statement.second);
            co_return Flow{};
        }
        case Statement::Kind::Switch:
            co_return (co_await switch_statement_async(statement));
        case Statement::Kind::Case:
        case Statement::Kind::Default:
            if (statement.first) co_return co_await this->statement_async(*statement.first);
            co_return Flow{};
        case Statement::Kind::While:
        case Statement::Kind::DoWhile:
            co_return (co_await loop_iterations_async(statement));
        case Statement::Kind::For: {
            scopes_.emplace_back();
            auto initial = (co_await this->statement_async(*statement.first));
            if (initial.kind != Flow::Normal) {
                pop_scope();
                co_return initial;
            }
            const auto flow = (co_await loop_iterations_async(statement));
            pop_scope();
            co_return flow;
        }
        case Statement::Kind::Break: co_return {Flow::Break};
        case Statement::Kind::Continue: co_return {Flow::Continue};
        case Statement::Kind::Empty: co_return {};
        case Statement::Kind::StaticAssert: {
            if (!current_function_ || !evaluation_only(*current_function_)) {
                fail(statement.location, "block assertion was not prepared before evaluation");
                co_return {Flow::Failed};
            }
            // Do not create a type-only environment or cache this condition's
            // value. Every reached assertion observes the current call/iteration,
            // sharing its ordinary evaluation budgets and retained name bindings.
            const auto value = (co_await required_scalar_async(*statement.expression));
            if (!value) co_return {Flow::Failed};
            if (!value->truthy()) {
                fail(statement.location, "$::static_assert failed: " + statement.assertion_message);
                co_return {Flow::Failed};
            }
            co_return {};
        }
        case Statement::Kind::Label:
        case Statement::Kind::Goto:
            fail(statement.location,
                 "labels and goto are not permitted during translation-time "
                 "evaluation");
            co_return {Flow::Failed};
        }
        co_return {Flow::Failed};
    }

    Program& program_;
    Diagnostics& diagnostics_;
    const unsigned diagnostics_at_entry_{diagnostics_.errors()};
    EvaluationIntegerQuery previous_integer_query_;
    EvaluationGenericValueQuery previous_generic_query_;
    std::function<std::shared_ptr<const RecordDecl>(const NominalTypeKey&)>
        previous_record_query_, inherited_record_query_;
    ContinuationQuery<bool(const TypePtr&, EvaluationLayoutKind)> previous_layout_preparation_;
    std::shared_ptr<RecordViews> record_views_{std::make_shared<RecordViews>()};
    RecordSourceProofs record_source_proofs_;
    std::shared_ptr<const EvaluationLayoutScopeIdentity> layout_scope_{
        std::make_shared<EvaluationLayoutScopeIdentity>()}, previous_layout_scope_;
    std::unordered_set<const Type*> preparing_type_bounds_;
    SourceValidationPhase validation_phase_{SourceValidationPhase::Definition};
    const FunctionDecl* current_function_{};
    std::string current_namespace_;
    const LayoutQuery* size_of_{};
    const LayoutQuery* align_of_{};
    const GenericPointerResolver* pointer_resolver_{};
    bool procedural_{};
    std::unordered_set<const FunctionDecl*> source_validated_;
    // Prepared aliases, expression type operands and retained proofs belong to
    // this evaluator's enclosing expansion, never the published declaration.
    std::unordered_map<const FunctionDecl*, std::unique_ptr<FunctionDecl>> invocation_functions_;
    std::unordered_set<const FunctionDecl*> generic_definitions_validated_;
    std::unordered_map<const Statement*, Expr::IntegerConstant> validated_cases_;
    std::unordered_map<const VariableDecl*, PreparedObjectInitializer> validated_initializers_;
    std::shared_ptr<const SyntaxContext> macro_context_;
    std::unordered_map<const SyntaxContext*, std::shared_ptr<const SyntaxContext>> construction_contexts_;
    SyntaxParseCallback syntax_parse_;
    std::shared_ptr<const SyntaxContext> call_context_;
    // Retain charged identities: a discarded probe result must not let pointer
    // reuse make a later, distinct context appear already accounted for.
    std::unordered_set<std::shared_ptr<const SyntaxContext>> charged_contexts_;
    std::unordered_set<std::shared_ptr<const SyntaxParseEnvironment>> charged_environments_;
    std::size_t token_bytes_{};
    std::size_t meta_bytes_{};
    std::uint64_t fresh_ordinal_{};
    std::unordered_set<const std::string*> counted_asset_backings_;
    std::vector<NameMap<Cell>> scopes_;
    std::size_t frame_base_{};
    std::uint64_t steps_{};
    unsigned depth_{};
    bool budget_diagnosed_{};
    bool resource_exhausted_{};
    ResourceReporting resource_reporting_{ResourceReporting::Required};
    bool context_unavailable_{};
    SourceLocation failure_location_;
    std::optional<std::string> failure_reason_;
    std::vector<CallFrame> call_stack_;
    std::vector<CallFrame> failure_trace_;
};

bool source_constant_candidate(const Expr& expression) {
    // Eligibility is independent of optional folding. This is not a proof:
    // the isolated sandbox must resolve a direct visible helper and evaluate
    // constant inputs without borrowing caller runtime cells. Its own local
    // storage/effects remain permitted. Keep this syntax walk off the native
    // stack, including nested helper arguments and literal aggregate inputs.
    std::vector<const Expr*> work{&expression};
    while (!work.empty()) {
        const auto* node = work.back();
        work.pop_back();
        if (!node) return false;
        if (node->evaluated_integer) continue;
        switch (node->kind) {
        case Expr::Kind::Integer:
        case Expr::Kind::Floating:
        case Expr::Kind::Character:
        case Expr::Kind::String:
        case Expr::Kind::Quote:
        case Expr::Kind::ByteSequence:
        case Expr::Kind::Address:
        case Expr::Kind::Name:
        case Expr::Kind::Sizeof:
        case Expr::Kind::Alignof:
        case Expr::Kind::Offsetof:
            break;
        case Expr::Kind::Parenthesized:
        case Expr::Kind::Cast:
            work.push_back(node->left.get());
            break;
        case Expr::Kind::Unary:
            if (node->text != "+" && node->text != "-" && node->text != "~" && node->text != "!" &&
                node->text != "&" && node->text != "*") return false;
            work.push_back(node->left.get());
            break;
        case Expr::Kind::Binary:
            work.push_back(node->right.get());
            work.push_back(node->left.get());
            break;
        case Expr::Kind::Conditional:
            work.push_back(node->third.get());
            work.push_back(node->right.get());
            work.push_back(node->left.get());
            break;
        case Expr::Kind::Call:
            // These existing required/type-only forms own their operand rules.
            if (atomic_builtin(*node) == AtomicBuiltin::IsLockFree ||
                (node->left && node->left->kind == Expr::Kind::Name &&
                 node->left->text == "$::eval" && node->arguments.size() == 1)) break;
            if (!node->left) return false;
            for (auto argument = node->arguments.rbegin(); argument != node->arguments.rend(); ++argument)
                work.push_back(argument->get());
            break;
        case Expr::Kind::AggregateInitializer:
            for (auto entry = node->initializer_entries.rbegin(); entry != node->initializer_entries.rend(); ++entry) {
                work.push_back(entry->value.get());
                for (auto designator = entry->designators.rbegin(); designator != entry->designators.rend(); ++designator)
                    if (designator->kind == Expr::InitializerDesignator::Kind::Index)
                        work.push_back(designator->index.get());
            }
            break;
        default: return false;
        }
    }
    return true;
}


EvaluationTask<void> normalize_automatic_array_bound_async(VariableDecl& declaration, FunctionDecl* caller,
    Program& program, Diagnostics& diagnostics,
    std::span<const std::pair<NameKey, TypePtr>> local_types) {
    if (!declaration.dynamic_array_bound || !declaration.type ||
        declaration.type->kind != Type::Kind::Array ||
        !source_constant_candidate(*declaration.dynamic_array_bound)) co_return;
    // Parsing cannot classify generic values, enums or target layout queries.
    // Resolve a now-constant extent before following assertions snapshot its
    // type. Visible helpers may prove a constant independently of optional
    // call folding; runtime caller cells remain type-only. ProbeDefinition
    // suppresses nonconstant diagnostics but reports resource exhaustion.
    const auto source_namespace = caller ? caller->source_namespace : std::string{};
    const auto value = co_await evaluate_target_integer_constant_async(program,
        *declaration.dynamic_array_bound, diagnostics, program.evaluation_size_of,
        program.evaluation_align_of, source_namespace, caller, local_types,
        EvaluationIntegerContext::ProbeDefinition);
    if (!value) co_return;
    // The successful isolated proof is already typed. Check that one result,
    // rather than executing the same bound again under a second evaluator.
    const auto bound = fixed_array_bound_value(*value, program.address_bits);
    if (!bound) {
        diagnostics.error(declaration.dynamic_array_bound->location,
            "fixed array bound must be a positive integer representable in 32 bits");
        co_return;
    }
    declaration.type = clone_type(declaration.type);
    declaration.type->lanes = *bound;
    declaration.dynamic_array_bound.reset();
}


bool signed_builtin(BuiltinType type) {
    return type == BuiltinType::I8 || type == BuiltinType::I16 ||
           type == BuiltinType::I32 || type == BuiltinType::I64 ||
           type == BuiltinType::I128 || type == BuiltinType::Iptr;
}

bool evaluate_enumerations(Program& program, Diagnostics& diagnostics, std::size_t first,
                           const EnumInitializerPreparation& prepare,
                           std::optional<Program::EnumerationPosition> through) {
    return evaluate_enumerations_async(program, diagnostics, first, prepare, through).run();
}

EvaluationTask<bool> evaluate_enumerations_async(Program& program, Diagnostics& diagnostics,
    std::size_t first, const EnumInitializerPreparation& prepare,
    std::optional<Program::EnumerationPosition> through) {
    struct InitializerOwner {
        Program& program;
        Program::EnumerationPosition position;
        std::unique_ptr<Expr> expression;
        ~InitializerOwner() {
            program.enumerations[position.declaration].enumerators[position.enumerator].initializer =
                std::move(expression);
        }
    };
    const auto errors = diagnostics.errors();
    const auto resources = program.evaluation_resource_errors;
    std::unordered_set<std::string> names;
    // Nested instantiation prepares its own enums. Visit only this publication
    // batch, in declaration order, so a later generic actual may use an earlier
    // enumerator. Neither preparation nor evaluation may retain vector-element
    // references: a layout query can instantiate further records and enums.
    const auto last = through ? std::min(program.enumerations.size(), through->declaration + 1)
                              : program.enumerations.size();
    for (auto index = first; index < last; ++index) {
        const auto owner = program.enumerations[index].nominal_identity;
        if (owner && owner->generic_owner) continue;
        const auto type = enum_type(program.enumerations[index]);
        std::optional<EvalValue> previous;
        for (std::size_t item = 0; item < program.enumerations[index].enumerators.size(); ++item) {
            if (through && index == through->declaration && item > through->enumerator) break;
            struct RestorePosition {
                Program& program;
                std::optional<Program::EnumerationPosition> previous;
                ~RestorePosition() { program.evaluation_enumerator_position = previous; }
            } restore_position{program, program.evaluation_enumerator_position};
            program.evaluation_enumerator_position = Program::EnumerationPosition{index, item};
            if (prepare && !program.enumerations[index].enumerators[item].value) {
                InitializerOwner initializer{program, {index, item},
                    std::move(program.enumerations[index].enumerators[item].initializer)};
                co_await prepare.async(initializer.expression);
                if (diagnostics.errors() != errors || program.evaluation_resource_errors != resources) co_return false;
            }
            const auto location = program.enumerations[index].enumerators[item].location;
            const auto underlying = program.enumerations[index].underlying;
            if (!program.enumerations[index].local &&
                !names.insert(program.enumerations[index].enumerators[item].name).second) {
                diagnostics.error(location,
                                  "enumerator '" + program.enumerations[index].enumerators[item].name +
                                      "' is declared more than once");
                continue;
            }
            std::optional<EvalValue> value;
            if (const auto known = program.enumerations[index].enumerators[item].value) {
                value = EvalValue{known->value, clone_type(type)};
            } else if (program.enumerations[index].enumerators[item].initializer) {
                // Own the consumer outside the growable declaration vector.
                // An active required-value service carries its logical work,
                // call ancestry and capabilities, never caller value cells.
                FunctionDecl context;
                context.source_namespace = namespace_prefix(program.enumerations[index].name);
                if (location.file) context.source_unit = location.file->source_unit_at(location.line);
                InitializerOwner initializer{program, {index, item},
                    std::move(program.enumerations[index].enumerators[item].initializer)};
                if (program.evaluation_generic_value) {
                    const auto query = program.evaluation_generic_value;
                    const auto result = co_await query.async(*initializer.expression, type, diagnostics,
                        &context, {}, EvaluationIntegerContext::Definition);
                    if (result.status == EvaluationGenericValueResult::Status::Value &&
                        result.value && result.value->evaluated_integer)
                        value = EvalValue{result.value->evaluated_integer->value, clone_type(type)};
                } else {
                    Evaluator evaluator(program, diagnostics, nullptr, context.source_namespace);
                    value = co_await evaluator.required_integer_async(*initializer.expression, type);
                    if (!value) evaluator.diagnose(location);
                }
                if (program.evaluation_resource_errors != resources) co_return false;
            } else if (!previous) {
                value = EvalValue{UInt128{}, clone_type(type)};
            } else {
                auto bits = type_bits(type);
                if (underlying == BuiltinType::Iptr ||
                    underlying == BuiltinType::Uptr) {
                    bits = program.address_bits;
                }
                const bool is_signed =
                    signed_builtin(underlying);
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
                        location,
                        "implicit enumerator value is not representable in '" +
                            type_name(type) + "'");
                } else {
                    value = EvalValue{incremented.value, clone_type(type)};
                }
            }
            if (!value) continue;
            program.enumerations[index].enumerators[item].value = Expr::IntegerConstant{
                value->integer, underlying};
            previous = *value;
        }
    }
    co_return diagnostics.errors() == errors && program.evaluation_resource_errors == resources;
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
            caller_ = assertion_lexical_function(program_, assertion);
            current_namespace_ = assertion.source_namespace;
            scopes_.emplace_back();
            for (const auto& [name, type] : assertion.local_types) scopes_.back().insert(name);
            rewrite(assertion.condition);
            scopes_.pop_back();
        }
        for (auto& object : program_.objects) {
            caller_ = object_lexical_function(program_, *object);
            current_namespace_ = caller_ ? caller_->source_namespace : namespace_prefix(object->name);
            // Lifted initializers retain their lexical owner's types. Exact
            // binder keys preserve hygiene; later required evaluation still
            // supplies no runtime values for these names.
            GenericExpansionState query_scope;
            if (caller_) collect_function_types(*caller_, query_scope);
            scopes_.emplace_back();
            for (const auto& name : query_scope.locals) scopes_.back().insert(name);
            rewrite(object->initializer);
            scopes_.pop_back();
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
                    for (const auto& name : attribute.variadic_bindings)
                        scopes_.back().insert(name_key(name));
            if (function->body) rewrite(*function->body);
        }
        caller_ = nullptr;
        scopes_.clear();
    }

private:
    bool local(const NameKey& key) const {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            if (scope->contains(key)) return true;
        }
        return false;
    }

    void rewrite(std::unique_ptr<Expr>& expression) {
        if (!expression) return;
        bool visible = false;
        if (expression->kind == Expr::Kind::Name) {
            const auto key = name_key(*expression);
            visible = local(key);
            if (key.binding.kind == ValueBinding::Kind::Local && !visible) {
                diagnostics_.error(expression->location,
                    "captured local value '" + expression->text +
                    "' is not visible at its replacement site");
                return;
            }
        }
        if (visible) {
            auto context = expression->name_context
                ? std::make_shared<NameLookupContext>(*expression->name_context)
                : std::make_shared<NameLookupContext>();
            context->kind = NameLookupContext::Kind::Local;
            expression->name_context = std::move(context);
        }
        if (expression->kind == Expr::Kind::Name && !visible &&
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
        for (auto& increment : statement.increments) rewrite(increment);
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
    std::vector<const Expr*> pending{&expression};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (!node) continue;
        if (node->kind == Expr::Kind::Sizeof || node->kind == Expr::Kind::Alignof ||
            node->kind == Expr::Kind::Offsetof ||
            atomic_builtin(*node) == AtomicBuiltin::IsLockFree) return true;
        // LIFO insertion preserves the original source-order edge set, including
        // designator indices and generic actuals. This is a pure classification.
        for (auto entry = node->initializer_entries.rbegin(); entry != node->initializer_entries.rend(); ++entry) {
            pending.push_back(entry->value.get());
            for (auto designator = entry->designators.rbegin(); designator != entry->designators.rend(); ++designator)
                pending.push_back(designator->index.get());
        }
        for (auto argument = node->generic_arguments.rbegin(); argument != node->generic_arguments.rend(); ++argument)
            pending.push_back(argument->value.get());
        for (auto argument = node->arguments.rbegin(); argument != node->arguments.rend(); ++argument)
            pending.push_back(argument->get());
        pending.push_back(node->third.get());
        pending.push_back(node->right.get());
        pending.push_back(node->left.get());
    }
    return false;
}

bool contains_relocation_candidate(const Expr& expression,
                                   const Program& program,
                                   std::string_view source_namespace) {
    std::vector<const Expr*> pending{&expression};
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        if (!node) continue;
        // Unevaluated layout operands and already classified addresses are opaque.
        if (node->kind == Expr::Kind::Sizeof || node->kind == Expr::Kind::Alignof ||
            node->kind == Expr::Kind::Offsetof ||
            atomic_builtin(*node) == AtomicBuiltin::IsLockFree) continue;
        if (node->kind == Expr::Kind::Address && node->evaluated_address) {
            if (node->evaluated_address->kind != AddressConstant::Kind::Absolute) return true;
            continue;
        }
        if (node->kind == Expr::Kind::Unary && node->text == "&") return true;
        if (node->kind == Expr::Kind::Name) {
            if (node->name_context && node->name_context->label_address) return true;
            const auto selected = value_namespace(program, nullptr, *node, source_namespace);
            if (selected && std::any_of(program.objects.begin(), program.objects.end(),
                    [&](const auto& object) {
                        return object->name == *selected && object->type &&
                               object->type->kind == Type::Kind::Array;
                    })) return true;
        }
        // A cast function designator is its code address, like `&function`.
        if (node->kind == Expr::Kind::Cast && node->left) {
            const auto* operand = node->left.get();
            while (operand->kind == Expr::Kind::Parenthesized && operand->left) operand = operand->left.get();
            const auto selected = operand->kind == Expr::Kind::Name
                ? value_namespace(program, nullptr, *operand, source_namespace) : std::nullopt;
            if (selected && std::any_of(program.functions.begin(), program.functions.end(),
                    [&](const auto& function) { return function->name == *selected; })) return true;
        }
        for (auto argument = node->arguments.rbegin(); argument != node->arguments.rend(); ++argument)
            pending.push_back(argument->get());
        pending.push_back(node->third.get());
        pending.push_back(node->right.get());
        pending.push_back(node->left.get());
    }
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

bool runtime_only(const FunctionDecl& function) {
    return function.attribute("runtime_only") != nullptr;
}

void replace_eval_value(std::unique_ptr<Expr>& expression,
                        const EvalValue& value) {
    if (value.label_address) {
        expression = label_constant_expression(value.label_address, expression->location);
        return;
    }
    auto replacement = std::make_unique<Expr>();
    replacement->location = expression->location;
    if (value.type && value.type->kind == Type::Kind::Builtin &&
        value.type->builtin == BuiltinType::Void) {
        replacement->kind = Expr::Kind::VoidValue;
        expression = std::move(replacement);
        return;
    }
    if (value.object) {
        replacement->kind = Expr::Kind::ByteSequence;
        replacement->type = clone_type(value.type);
        replacement->string_value = value.object->data;
        for (const auto& slot : value.object->pointers) {
            if (slot.value->address) replacement->object_relocations.push_back(
                {slot.offset, slot.length, clone_type(slot.value->type), *slot.value->address});
            else if (slot.value->label_address) replacement->object_relocations.push_back(
                {slot.offset, slot.length, clone_type(slot.value->type), *slot.value->label_address});
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
    if (!value.type->nominal_key().empty() || is_label_type(value.type)) {
        replacement->type = clone_type(value.type);
    }
    expression = std::move(replacement);
}

bool materializable_eval_value(const EvalValue& value, SourceLocation location,
                               Diagnostics& diagnostics, bool required) {
    if (!value.object) return true;
    for (const auto& slot : value.object->pointers) {
        if (slot.value->meta_pointer || slot.value->string ||
            (!slot.value->address && !slot.value->label_address)) {
            if (required) diagnostics.error(location,
                "a translation-time pointer cannot escape inside an evaluated object");
            return false;
        }
    }
    return true;
}

void replace_required_integer(std::unique_ptr<Expr>& expression,
    const EvalValue& value, unsigned address_bits) {
    replace_eval_value(expression, value);
    // The model receives a canonical mathematical spelling, not the source
    // spelling or the unsigned encoding of a negative signed value.
    auto width = type_bits(value.type);
    if (value.type->builtin == BuiltinType::Iptr ||
        value.type->builtin == BuiltinType::Uptr) width = address_bits;
    if (signed_value(value) && bit(value.integer, width - 1)) {
        expression->text = "-" + to_decimal(mask_to(negate(value.integer), width)) +
                           literal_suffix(value.type);
    }
}

EvaluationTask<bool> rewrite_required_integer_async(std::unique_ptr<Expr>& expression,
                              const FunctionDecl* caller, Program& program,
                              Diagnostics& diagnostics,
                              const TypePtr& destination = {}) {
    Evaluator evaluator(program, diagnostics, caller);
    const auto value = co_await evaluator.required_integer_async(*expression, destination);
    if (!value) {
        evaluator.diagnose(expression->location);
        co_return false;
    }
    replace_required_integer(expression, *value, program.address_bits);
    co_return true;
}

EvaluationTask<bool> rewrite_required_floating_async(std::unique_ptr<Expr>& expression,
                               const FunctionDecl* caller, Program& program,
                               Diagnostics& diagnostics,
                               TypePtr destination,
                               const LayoutQuery* size_of = nullptr,
                               const LayoutQuery* align_of = nullptr,
                               std::string_view source_namespace = {}) {
    Evaluator evaluator(program, diagnostics, caller,
                        std::string(source_namespace), size_of, align_of);
    const auto value = co_await evaluator.required_floating_async(*expression, destination);
    if (!value) {
        evaluator.diagnose(expression->location);
        co_return false;
    }
    replace_eval_value(expression, *value);
    co_return true;
}

// Integer expressions in pointer initializers are required constants too.
// Preserve their integer type/value here: Data IR applies the destination
// address space's null encoding, and rejects nonzero implicit conversions.
EvaluationTask<bool> fold_pointer_integer_initializer_async(std::unique_ptr<Expr>& expression,
                                      Program& program, Diagnostics& diagnostics,
                                      const FunctionDecl* caller,
                                      std::string_view source_namespace,
                                      const LayoutQuery* size_of = nullptr,
                                      const LayoutQuery* align_of = nullptr) {
    Evaluator evaluator(program, diagnostics, caller, std::string(source_namespace),
                        size_of, align_of);
    const bool pending_layout = contains_layout_query(*expression) &&
        ((!size_of && !program.evaluation_size_of) ||
         (!align_of && !program.evaluation_align_of));
    if (!evaluator.integer_expression(*expression)) {
        // A pointer derived from an integer is a translation-time value whose
        // address bits initialize the object. Relocatable addresses and values
        // that do not fold are left to static data lowering.
        if (pending_layout || contains_relocation_candidate(*expression, program, source_namespace))
            co_return false;
        const auto pointer = co_await evaluator.required_absolute_pointer_async(*expression);
        if (!pointer) {
            if (evaluator.resource_exhausted()) evaluator.diagnose(expression->location);
            co_return false;
        }
        auto replacement = std::make_unique<Expr>();
        replacement->kind = Expr::Kind::Address;
        replacement->location = expression->location;
        replacement->type = clone_type(pointer->type);
        replacement->evaluated_address = pointer->address;
        expression = std::move(replacement);
        co_return true;
    }
    if (pending_layout) co_return true;
    const auto value = co_await evaluator.required_integer_async(*expression);
    if (value) replace_eval_value(expression, *value);
    else evaluator.diagnose(expression->location);
    co_return true;
}

EvaluationTask<void> fold_relocation_offsets_async(std::unique_ptr<Expr>& expression,
                             Program& program, Diagnostics& diagnostics,
                             const LayoutQuery& size_of, const LayoutQuery& align_of,
                             std::string_view source_namespace,
                             const FunctionDecl* caller = nullptr) {
    if (!expression) co_return;
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    if (expression->kind == Expr::Kind::AggregateInitializer) {
        for (auto& entry : expression->initializer_entries) {
            co_await fold_relocation_offsets_async(entry.value, program, diagnostics,
                size_of, align_of, source_namespace, caller);
            if (stopped()) co_return;
        }
        co_return;
    }
    co_await fold_relocation_offsets_async(expression->left, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    co_await fold_relocation_offsets_async(expression->right, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    co_await fold_relocation_offsets_async(expression->third, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    for (auto& argument : expression->arguments) {
        co_await fold_relocation_offsets_async(argument, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    if (expression->kind != Expr::Kind::Binary) co_return;
    const auto fold_integer = [&](std::unique_ptr<Expr>& operand) -> EvaluationTask<void> {
        Evaluator evaluator(program, diagnostics, caller, std::string(source_namespace),
            &size_of, &align_of);
        const auto value = co_await evaluator.required_integer_async(*operand);
        if (value) replace_eval_value(operand, *value);
        else evaluator.diagnose(operand->location);
    };
    if (expression->text == "index" && expression->left && expression->right &&
        contains_relocation_candidate(*expression->left, program, source_namespace)) {
        co_await fold_integer(expression->right);
    } else if ((expression->text == "+" || expression->text == "-") &&
               expression->left && expression->right) {
        if (contains_relocation_candidate(*expression->left, program, source_namespace) &&
            !contains_relocation_candidate(*expression->right, program, source_namespace)) {
            co_await fold_integer(expression->right);
        } else if (expression->text == "+" &&
                   contains_relocation_candidate(*expression->right, program, source_namespace) &&
                   !contains_relocation_candidate(*expression->left, program, source_namespace)) {
            co_await fold_integer(expression->left);
        }
    }
}

EvaluationTask<void> fold_patch_initial_offsets_async(std::unique_ptr<Expr>& expression,
                                Program& program, Diagnostics& diagnostics,
                                const LayoutQuery& size_of, const LayoutQuery& align_of,
                                std::string_view source_namespace,
                                const FunctionDecl* caller = nullptr) {
    if (!expression) co_return;
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    co_await fold_patch_initial_offsets_async(expression->left, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    co_await fold_patch_initial_offsets_async(expression->right, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    co_await fold_patch_initial_offsets_async(expression->third, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    for (auto& argument : expression->arguments) {
        co_await fold_patch_initial_offsets_async(argument, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    for (auto& entry : expression->initializer_entries) {
        co_await fold_patch_initial_offsets_async(entry.value, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    if (expression->kind == Expr::Kind::Call && expression->left &&
        expression->left->kind == Expr::Kind::Name && expression->left->text == "$::patch" &&
        !expression->arguments.empty() && expression->arguments.front() &&
        contains_relocation_candidate(*expression->arguments.front(), program, source_namespace)) {
        co_await fold_relocation_offsets_async(expression->arguments.front(), program, diagnostics,
            size_of, align_of, source_namespace, caller);
    }
}

EvaluationTask<void> fold_patch_initial_offsets_async(Statement& statement, Program& program,
                                Diagnostics& diagnostics,
                                const LayoutQuery& size_of, const LayoutQuery& align_of,
                                std::string_view source_namespace,
                                const FunctionDecl* caller = nullptr) {
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    for (auto& child : statement.statements) {
        co_await fold_patch_initial_offsets_async(*child, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    if (statement.declaration) {
        co_await fold_patch_initial_offsets_async(statement.declaration->dynamic_array_bound,
            program, diagnostics, size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
        co_await fold_patch_initial_offsets_async(statement.declaration->initializer,
            program, diagnostics, size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    co_await fold_patch_initial_offsets_async(statement.expression, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    co_await fold_patch_initial_offsets_async(statement.condition, program, diagnostics,
        size_of, align_of, source_namespace, caller);
    if (stopped()) co_return;
    for (auto& increment : statement.increments) {
        co_await fold_patch_initial_offsets_async(increment, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    if (statement.first) {
        co_await fold_patch_initial_offsets_async(*statement.first, program, diagnostics,
            size_of, align_of, source_namespace, caller);
        if (stopped()) co_return;
    }
    if (statement.second)
        co_await fold_patch_initial_offsets_async(*statement.second, program, diagnostics,
            size_of, align_of, source_namespace, caller);
}

std::shared_ptr<const LabelAddressConstant> resolve_label_constant(
    const Expr& source, const FunctionDecl* caller, Program& program,
    std::string* error, const std::function<bool(SourceLocation)>& work) {
    const Expr* value = &source;
    while (value->kind == Expr::Kind::Parenthesized && value->left) value = value->left.get();
    const auto reject = [&](std::string message) -> std::shared_ptr<const LabelAddressConstant> {
        if (error) *error = std::move(message);
        return {};
    };
    if (value->kind != Expr::Kind::Name)
        return reject("generic label argument requires a visible label address constant");
    if (value->name_context && value->name_context->label_address) return value->name_context->label_address;
    const auto separator = value->text.rfind("::");
    if (separator == std::string::npos &&
        ((value->name_context && value->name_context->kind == NameLookupContext::Kind::Local) ||
         value_namespace(program, caller, NameUse(*value)))) {
        // A value argument is an expression, not the direct-goto name slot.
        // An ordinary value must not silently turn into a same-spelled label.
        // Generic label parameters have already been substituted with their
        // normalized qualified address before this point.
        return reject(
            "generic label argument requires a visible label address constant; "
            "an ordinary value name cannot select a same-spelled label");
    }
    const auto owner_name = separator == std::string::npos
        ? caller ? caller->name : std::string{}
        : value->text.substr(0, separator);
    const auto label_name = separator == std::string::npos
        ? value->text : value->text.substr(separator + 2);
    NameUse owner_use(*value);
    owner_use.spelling = owner_name;
    NameLookupContext current_owner;
    if (separator == std::string::npos) {
        // The implicit owner is already a selected function, not a relative
        // name to look up again under the argument's lexical namespace.
        current_owner.kind = NameLookupContext::Kind::Exact;
        owner_use.context = &current_owner;
    }
    const auto* owner = resolve_function(
        program, caller, owner_use,
        [](const FunctionDecl& candidate) { return candidate.body != nullptr; });
    if (!owner) {
        owner = resolve_function(
            program, caller, owner_use,
            [](const FunctionDecl&) { return true; });
    }
    if (separator == std::string::npos && caller && evaluation_only(*caller)) {
        // Macro/expander definitions are not runtime callable entries. Their
        // own labels still have a source type in unevaluated queries. Retain
        // this lexical owner; label_address_error forbids exposing its value.
        owner = caller;
    }
    if (!owner || label_name.empty()) {
        return reject("generic label argument does not name a visible function label");
    }
    const auto qualified = owner->name + "::" + label_name;
    const bool declared = std::any_of(
        program.global_labels.begin(), program.global_labels.end(),
        [&](const GlobalLabelDecl& label) {
            return label.qualified_name == qualified;
        });
    auto address = std::make_shared<LabelAddressConstant>();
    address->owner = owner;
    const auto location = value->name_context && value->name_context->last_component_location.valid()
        ? value->name_context->last_component_location : value->location;
    const NameKey key(label_name, location);
    const auto binding = resolved_label_binding(NameUse(*value));
    std::vector<const Statement*> pending;
    if (owner->body) pending.push_back(owner->body.get());
    std::uint64_t ordinal = 0;
    while (!pending.empty()) {
        const auto* statement = pending.back();
        pending.pop_back();
        if (work && !work(statement->location)) return {};
        if (statement->kind == Statement::Kind::Label) {
            ++ordinal;
            const NameKey candidate(statement->label_name, statement->label_location);
            const bool matches = statement->global_label || candidate == key ||
                (separator != std::string::npos && !candidate.context.value && candidate.fresh == key.fresh);
            const bool bound =
                (!binding.scope || binding.scope == statement->label_binding.scope) &&
                (binding.kind != LabelBinding::Kind::Reference ||
                 binding.declaration == statement->label_binding.declaration);
            if (statement->label_name == label_name && matches && bound) {
                address->definition = statement;
                address->ordinal = ordinal;
                if (statement->global_label) address->global_name = qualified;
                break;
            }
        }
        if (statement->second) pending.push_back(statement->second.get());
        if (statement->first) pending.push_back(statement->first.get());
        for (auto at = statement->statements.rbegin(); at != statement->statements.rend(); ++at)
            pending.push_back(at->get());
    }
    if (!address->definition && declared && !binding.scope &&
        binding.kind == LabelBinding::Kind::Unknown) address->global_name = qualified;
    if ((!address->definition && address->global_name.empty()) || !owner->generic_parameters.empty()) {
        return reject("generic label argument requires a visible label in a concrete function");
    }
    return address;
}

EvaluationTask<void> prepare_generic_inferred_array_async(VariableDecl& declaration, const FunctionDecl& caller,
    Program& program, Diagnostics& diagnostics, const GenericExpansionState& state) {
    // Definition preparation is context-free even when requested by an active
    // expansion. Invocation-owned declarations were prepared by the evaluator.
    if (state.invocation_owner == &caller) co_return;
    const auto errors = diagnostics.errors();
    (void)(co_await prepare_inferred_array_async(declaration, program, state.local_types,
        [&](const Expr& source, std::span<const std::pair<NameKey, TypePtr>> types,
            const TypePtr&) -> EvaluationTask<SourceConstantProbe> {
            const auto proof = co_await evaluate_target_integer_requirement_async(program, source, diagnostics,
                program.evaluation_size_of, program.evaluation_align_of, caller.source_namespace,
                &caller, types, EvaluationIntegerContext::StagedDefinition);
            SourceConstantProbe result;
            result.needs_invocation_context = proof.status == EvaluationIntegerResult::Status::ContextUnavailable;
            if (proof.value && !integer_negative(proof.value->value,
                    evaluation_integer_type(builtin_type(proof.value->type), program.address_bits)) &&
                fits_unsigned(proof.value->value, program.address_bits)) result.value = proof.value;
            co_return result;
        }, [&](SourceLocation at, std::string message) {
            if (diagnostics.errors() == errors) diagnostics.error(at, message);
        }, false));
}

EvaluationTask<bool> normalize_generic_label_async(std::unique_ptr<Expr>& value,
                             const FunctionDecl* caller, Program& program,
                             Diagnostics& diagnostics) {
    const Expr* root = value.get();
    while (root->kind == Expr::Kind::Parenthesized && root->left) root = root->left.get();
    if (root->kind == Expr::Kind::Name) {
        // Keep the direct-name visibility diagnostics; compound expressions
        // use the same resolver through ordinary typed evaluation below.
        std::string error;
        auto address = resolve_label_constant(*root, caller, program, &error);
        if (!address) {
            diagnostics.error(root->location, error);
            co_return false;
        }
        if (const auto* invalid = label_address_error(*address)) {
            diagnostics.error(root->location, invalid);
            co_return false;
        }
        value = label_constant_expression(std::move(address), value->location);
        co_return true;
    }
    Evaluator evaluator(program, diagnostics, caller);
    auto result = co_await evaluator.required_label_async(*value);
    if (!result) {
        evaluator.diagnose(value->location);
        co_return false;
    }
    value = std::move(result);
    co_return true;
}

EvaluationTask<bool> normalize_generic_arguments_async(const FunctionDecl& generic,
                                 std::vector<Expr::GenericArgument>& arguments,
                                 const FunctionDecl* caller, Program& program,
                                 Diagnostics& diagnostics, SourceLocation location,
                                 GenericExpansionState& state, std::string_view mangling,
                                 bool* context_dependent) {
    if (arguments.size() != generic.generic_parameters.size()) {
        diagnostics.error(location, "generic argument count does not match '" + generic.name + "'");
        co_return false;
    }
    TypeSubstitutions types;
    ValueSubstitutions values;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& parameter = generic.generic_parameters[index];
        auto& argument = arguments[index];
        if (!parameter.value_type) {
            if (argument.type) {
                types.emplace(name_key(parameter), argument.type);
                continue;
            }
            diagnostics.error(location, "generic type parameter '" + parameter.name +
                                        "' requires a type argument");
            co_return false;
        }
        if (!argument.value) {
            diagnostics.error(location, "generic value parameter '" + parameter.name +
                                        "' requires a value argument");
            co_return false;
        }
        const auto value_type = clone_type(parameter.value_type, types, values);
        const auto errors = diagnostics.errors();
        co_await rewrite_generic_type_bounds_async(value_type, caller, program, diagnostics, state, mangling, location);
        if (diagnostics.errors() != errors || state.resource_failed(program)) co_return false;
        // Preparation reached from a layout/enum dependency is still charged
        // to its active evaluator. Definition ownership controls capabilities,
        // not whether the nested proof can reset work or start another pump.
        if (context_dependent || state.invocation_owner || program.evaluation_generic_value) {
            if (value_type->kind == Type::Kind::Pointer) {
                if (!state.pointer_resolver) {
                    diagnostics.error(location, "pointer-valued generic argument normalization is not implemented yet");
                    co_return false;
                }
                lift_pointer_argument_strings(program, argument.value, caller);
            }
            const auto context = context_dependent ? EvaluationIntegerContext::StagedDefinition
                : state.invocation_owner ? EvaluationIntegerContext::CallerInvocation
                                         : EvaluationIntegerContext::Definition;
            EvaluationGenericValueResult result;
            if (program.evaluation_generic_value) {
                const auto query = program.evaluation_generic_value;
                result = co_await query.async(*argument.value, value_type, diagnostics, caller, state.local_types, context);
            } else {
                Evaluator evaluator(program, diagnostics, caller);
                result = co_await evaluator.generic_value_requirement_async(*argument.value, value_type,
                    diagnostics, caller, state.local_types, context);
            }
            if (result.status == EvaluationGenericValueResult::Status::ContextUnavailable && context_dependent) {
                *context_dependent = true;
                // This private substitution supplies the parameter's type to
                // provisional signatures (e.g. sizeof a narrow value parameter).
                // The original argument stays untouched and is range-checked
                // again before a concrete invocation can be instantiated.
                auto converted = std::make_unique<Expr>();
                converted->kind = Expr::Kind::Cast;
                converted->location = argument.value->location;
                converted->type = value_type;
                converted->left = std::move(argument.value);
                argument.value = std::move(converted);
            } else if (result.status == EvaluationGenericValueResult::Status::Value) {
                argument.value = std::move(result.value);
            } else co_return false;
            values.emplace(name_key(parameter), argument.value.get());
            continue;
        }
        if (value_type->kind == Type::Kind::Pointer && state.pointer_resolver) {
            lift_pointer_argument_strings(program, argument.value, caller);
            if (!(co_await state.pointer_resolver.async(argument.value, value_type, caller,
                                         state.locals))) co_return false;
            values.emplace(name_key(parameter), argument.value.get());
            continue;
        }
        if (value_type->kind == Type::Kind::Builtin &&
            value_type->builtin == BuiltinType::Label) {
            if (!(co_await normalize_generic_label_async(argument.value, caller, program,
                                         diagnostics))) co_return false;
            values.emplace(name_key(parameter), argument.value.get());
            continue;
        }
        if (!is_integer(value_type)) {
            diagnostics.error(location,
                value_type->kind == Type::Kind::Pointer
                    ? "pointer-valued generic argument normalization is not implemented yet"
                    : "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
            co_return false;
        }
        if (!(co_await rewrite_required_integer_async(argument.value, caller, program, diagnostics,
                                      value_type))) co_return false;
        values.emplace(name_key(parameter), argument.value.get());
    }
    co_return true;
}

EvaluationTask<void> rewrite_patch_sink_indices_async(std::unique_ptr<Expr>& expression,
                                const FunctionDecl* caller,
                                Program& program,
                                Diagnostics& diagnostics) {
    if (!expression) co_return;
    const auto epoch = program.evaluation_resource_errors;
    if (expression->kind == Expr::Kind::Parenthesized) {
        co_await rewrite_patch_sink_indices_async(expression->left, caller, program,
                                   diagnostics);
        co_return;
    }
    if (expression->kind != Expr::Kind::Binary) co_return;
    if (expression->text == "member") {
        co_await rewrite_patch_sink_indices_async(expression->left, caller, program,
                                   diagnostics);
        co_return;
    }
    if (expression->text != "index") co_return;
    co_await rewrite_patch_sink_indices_async(expression->left, caller, program,
                               diagnostics);
    if (program.evaluation_resource_errors != epoch) co_return;
    if (expression->right) {
        if (contains_layout_query(*expression->right)) co_return;
        (void)(co_await rewrite_required_integer_async(
            expression->right, caller, program, diagnostics,
            builtin_type(BuiltinType::Uptr)));
    }
}

enum class EvalCallRewrite { Fold, PreserveRoot };

EvaluationTask<void> rewrite_eval_expr_async(std::unique_ptr<Expr>& expression,
                       FunctionDecl* caller, Program& program,
                       Diagnostics& diagnostics,
                       bool opportunistic,
                       bool required_context = false,
                       bool runtime_context = false,
                       EvalCallRewrite call_rewrite = EvalCallRewrite::Fold) {
    if (!expression) co_return;
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };

    if (expression->kind == Expr::Kind::Sizeof || expression->kind == Expr::Kind::Alignof ||
        expression->kind == Expr::Kind::Offsetof ||
        atomic_builtin(*expression) == AtomicBuiltin::IsLockFree) {
        // Resolve known queries while translation-only declarations still
        // exist. Never rewrite/execute the unevaluated operand's calls. Local
        // or dynamic object queries not yet known here remain for typed HIR.
        Evaluator evaluator(program, diagnostics, caller);
        // Resolved local binder identities distinguish shadowed declarations.
        // Supply their types (never cell values) while eval-only callees still
        // exist, so a mixed local/call operand can be typed before erasure.
        GenericExpansionState query_scope;
        if (caller) collect_function_types(*caller, query_scope);
        if (const auto value = co_await evaluator.required_integer_with_types_async(*expression, query_scope.local_types))
            replace_eval_value(expression, *value);
        else if (stopped() || expression->kind == Expr::Kind::Offsetof)
            evaluator.diagnose(expression->location);
        co_return;
    }

    if (expression->kind == Expr::Kind::Quote) {
        diagnostics.error(expression->location, "$::quote cannot enter runtime expressions");
        co_return;
    }

    if (expression->kind == Expr::Kind::Call && expression->left &&
        expression->left->kind == Expr::Kind::Name &&
        expression->left->text == "$::patch" &&
        !expression->arguments.empty() && expression->arguments.size() <= 2) {
        if (expression->arguments.size() == 2)
            co_await rewrite_patch_sink_indices_async(expression->arguments[1], caller, program,
                                       diagnostics);
        if (stopped()) co_return;
        auto& initial = expression->arguments.front();
        // The initial is a required constant, not an opportunistic runtime
        // fold. Evaluate it as a whole so casts/arithmetic, generic layout
        // queries and untaken arms retain their ordinary constant semantics.
        // Symbolic address initials retain their relocation representation.
        if (!contains_relocation_candidate(*initial, program,
                caller ? caller->source_namespace : std::string_view{}) &&
            (!contains_layout_query(*initial) || program.evaluation_size_of)) {
            Evaluator evaluator(program, diagnostics, caller);
            if (const auto value = co_await evaluator.required_integer_async(*initial)) {
                replace_eval_value(initial, *value);
                co_return;
            }
            if (stopped()) {
                evaluator.diagnose(initial->location);
                co_return;
            }
        }
    }

    if (expression->kind == Expr::Kind::AggregateInitializer) {
        for (auto& entry : expression->initializer_entries) {
            for (auto& designator : entry.designators) {
                if (designator.index) {
                    (void)(co_await rewrite_required_integer_async(
                        designator.index, caller, program, diagnostics,
                        builtin_type(BuiltinType::Uptr)));
                    if (stopped()) co_return;
                }
            }
            co_await rewrite_eval_expr_async(entry.value, caller, program, diagnostics,
                              opportunistic, required_context,
                              runtime_context);
            if (stopped()) co_return;
        }
        co_return;
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
            co_return;
        }
        // A required expression keeps the marker: evaluation rejects it if
        // the selected path reaches it.
        if (required_context) co_return;
        expression = std::move(expression->arguments.front());
        co_await rewrite_eval_expr_async(expression, caller, program, diagnostics, false,
                          false, true, call_rewrite);
        co_return;
    }

    // Forced evaluation owns the complete operand. The evaluator recursively
    // interprets visible ordinary functions, so nested calls need no separate
    // source rewrite first.
    if (direct_builtin_call("$::eval")) {
        if (runtime_context) {
            diagnostics.error(
                expression->location,
                "$::eval cannot appear inside a $::runtime expression");
            co_return;
        }
        if (expression->arguments.size() != 1) {
            diagnostics.error(expression->location,
                              "$::eval requires exactly one expression");
            co_return;
        }
        Evaluator evaluator(program, diagnostics, caller);
        auto value = co_await evaluator.expression_async(*expression->arguments.front());
        if (!value) {
            evaluator.diagnose(expression->location);
            co_return;
        }
        if (value->pointer()) {
            diagnostics.error(
                expression->location,
                "a translation-time pointer cannot escape through $::eval");
            co_return;
        }
        if (value->bytes || value->buffer) {
            diagnostics.error(expression->location,
                "meta byte values cannot enter runtime expressions");
            co_return;
        }
        if (materializable_eval_value(*value, expression->location, diagnostics, true))
            replace_eval_value(expression, *value);
        co_return;
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
        const auto value = co_await evaluator.required_scalar_async(*expression);
        if (!value) evaluator.diagnose(expression->location);
        else replace_eval_value(expression, *value);
        co_return;
    }

    if (expression->left) {
        co_await rewrite_eval_expr_async(expression->left, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context,
                          expression->kind == Expr::Kind::Parenthesized
                              ? call_rewrite : EvalCallRewrite::Fold);
        if (stopped()) co_return;
    }
    if (expression->right) {
        co_await rewrite_eval_expr_async(expression->right, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
        if (stopped()) co_return;
    }
    if (expression->third) {
        co_await rewrite_eval_expr_async(expression->third, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
        if (stopped()) co_return;
    }
    for (auto& argument : expression->arguments) {
        co_await rewrite_eval_expr_async(argument, caller, program, diagnostics,
                          opportunistic, required_context, runtime_context);
        if (stopped()) co_return;
    }
    if (expression->kind != Expr::Kind::Call || !expression->left ||
        expression->left->kind != Expr::Kind::Name) {
        co_return;
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
        co_return;
    }
    const bool required =
        (function && evaluation_only(*function)) ||
        required_declaration != nullptr || required_context;
    if ((function && runtime_only(*function) && !required) ||
        (!function && !required) ||
        ((!opportunistic || call_rewrite == EvalCallRewrite::PreserveRoot) && !required)) {
        co_return;
    }

    // Optional folding is a fresh speculative sandbox. Its exhaustion cannot
    // publish a mandatory failure event or abort the surviving runtime call.
    // Required evaluations retain the original diagnostic/resource channel.
    std::ostringstream speculative_output;
    Diagnostics speculative_diagnostics(speculative_output);
    Evaluator evaluator(program, required ? diagnostics : speculative_diagnostics, caller);
    if (!required) evaluator.resource_reporting(Evaluator::ResourceReporting::Speculative);
    auto value = co_await evaluator.expression_async(*expression);
    if (!value) {
        if (required) evaluator.diagnose(expression->location);
        co_return;
    }
    if (value->pointer()) {
        if (required) {
            diagnostics.error(
                expression->location,
                "an evaluation-only call cannot return a translation-time "
                "pointer");
        }
        co_return;
    }
    if (value->bytes || value->buffer) {
        diagnostics.error(expression->location,
            "meta byte values cannot enter runtime expressions");
        co_return;
    }
    if (materializable_eval_value(*value, expression->location, diagnostics, required))
        replace_eval_value(expression, *value);
}

void infer_initializer_array_bound(TypePtr& type, const Expr* initializer,
                                   Diagnostics& diagnostics) {
    if (!type || type->kind != Type::Kind::Array || type->lanes != 0 || type->array_bound ||
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
    const auto record = program.record_definition(parent->nominal_key());
    if (!record) return {};
    if (designator) {
        if (designator->kind !=
            Expr::InitializerDesignator::Kind::Member) {
            return {};
        }
        const auto member = std::find_if(
            record->members.begin(), record->members.end(),
            [&](const RecordMemberDecl& candidate) {
                return candidate.member_name() == designator->member_name();
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

// An explicit uptr conversion of a function label is a code-address
// relocation, like `(uptr)&function`. Resolve the label while its lexical
// owner is known; every other label use, and an unavailable label address,
// keeps its evaluated meaning and diagnostics.
void bind_label_conversions(std::unique_ptr<Expr>& root, const FunctionDecl* owner,
                            Program& program) {
    std::vector<Expr*> pending{root.get()};
    while (!pending.empty()) {
        auto* node = pending.back();
        pending.pop_back();
        if (!node || node->kind == Expr::Kind::Sizeof || node->kind == Expr::Kind::Alignof ||
            node->kind == Expr::Kind::Offsetof) continue;
        if (node->kind == Expr::Kind::Cast && node->left && node->type &&
            node->type->kind == Type::Kind::Builtin && node->type->builtin == BuiltinType::Uptr) {
            auto* operand = &node->left;
            while ((*operand)->kind == Expr::Kind::Parenthesized && (*operand)->left)
                operand = &(*operand)->left;
            if ((*operand)->kind == Expr::Kind::Name)
                if (auto address = resolve_label_constant(**operand, owner, program);
                    address && !label_address_error(*address)) {
                    *operand = label_constant_expression(std::move(address), (*operand)->location);
                    continue;
                }
        }
        for (auto& argument : node->arguments) pending.push_back(argument.get());
        pending.push_back(node->third.get());
        pending.push_back(node->right.get());
        pending.push_back(node->left.get());
    }
}

EvaluationTask<void> fold_static_initializer_async(Expr& initializer, TypePtr type,
                             Program& program, Diagnostics& diagnostics,
                             const LayoutQuery* size_of = nullptr,
                             const LayoutQuery* align_of = nullptr,
                             std::string_view source_namespace = {},
                             const FunctionDecl* caller = nullptr) {
    if (!type ||
        initializer.kind != Expr::Kind::AggregateInitializer) {
        co_return;
    }
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    std::size_t cursor{};
    for (auto& entry : initializer.initializer_entries) {
        if (stopped()) co_return;
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
            const auto record = program.record_definition(type->nominal_key());
            if (record) {
                const auto member = std::find_if(
                    record->members.begin(), record->members.end(),
                    [&](const RecordMemberDecl& candidate) {
                        return candidate.member_name() == first->member_name();
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
            co_await fold_static_initializer_async(*entry.value, destination, program,
                                    diagnostics, size_of, align_of,
                                    source_namespace, caller);
        } else if (is_label_type(destination)) {
            Evaluator evaluator(program, diagnostics, caller,
                                std::string(source_namespace), size_of, align_of);
            auto label = co_await evaluator.required_label_async(*entry.value);
            if (label) entry.value = std::move(label);
            else evaluator.diagnose(entry.value->location);
        } else if (destination->kind == Type::Kind::Pointer) {
            (void)(co_await fold_pointer_integer_initializer_async(entry.value, program, diagnostics,
                caller, source_namespace, size_of, align_of));
        } else if (is_integer(destination)) {
            bind_label_conversions(entry.value, caller, program);
            if (contains_relocation_candidate(*entry.value, program,
                                              source_namespace)) continue;
            const bool target_dependent =
                contains_layout_query(*entry.value);
            if (target_dependent && ((!size_of && !program.evaluation_size_of) ||
                                     (!align_of && !program.evaluation_align_of))) continue;
            if (!target_dependent) {
                (void)(co_await rewrite_required_integer_async(entry.value, caller, program,
                                               diagnostics));
                continue;
            }
            Evaluator evaluator(program, diagnostics, caller,
                                std::string(source_namespace), size_of,
                                align_of);
            const auto value = co_await evaluator.required_integer_async(*entry.value);
            if (!value) {
                evaluator.diagnose(entry.value->location);
                continue;
            }
            replace_eval_value(entry.value, *value);
        } else if (is_floating(destination)) {
            if (!contains_layout_query(*entry.value) ||
                ((size_of || program.evaluation_size_of) &&
                 (align_of || program.evaluation_align_of))) {
                (void)(co_await rewrite_required_floating_async(entry.value, caller,
                    program, diagnostics, destination, size_of, align_of,
                    source_namespace));
            }
        }
    }
}

EvaluationTask<void> rewrite_eval_statement_async(Statement& statement, FunctionDecl* caller,
                            Program& program, Diagnostics& diagnostics,
                            bool opportunistic) {
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    for (auto& child : statement.statements) {
        co_await rewrite_eval_statement_async(*child, caller, program, diagnostics,
                               opportunistic);
        if (stopped()) co_return;
    }
    if (statement.declaration) {
        if (statement.declaration->dynamic_array_bound) {
            co_await rewrite_eval_expr_async(statement.declaration->dynamic_array_bound,
                              caller, program, diagnostics, opportunistic);
            if (stopped()) co_return;
        }
        if (statement.declaration->initializer) {
            co_await rewrite_eval_expr_async(statement.declaration->initializer, caller,
                              program, diagnostics, opportunistic);
            if (stopped()) co_return;
            if (!statement.declaration->dynamic_array_bound) {
                infer_initializer_array_bound(
                    statement.declaration->type,
                    statement.declaration->initializer.get(), diagnostics);
            }
        }
    }
    if (statement.expression) {
        // A mandatory tail transfer must keep its returned call until ABI
        // lowering. Its operands may still fold, and explicit/implicit
        // required evaluation keeps its normal checks and diagnostics.
        const bool musttail = statement.kind == Statement::Kind::Return &&
            std::any_of(statement.attributes.begin(), statement.attributes.end(),
                [](const Attribute& attribute) { return attribute.name == "musttail"; });
        co_await rewrite_eval_expr_async(statement.expression, caller, program, diagnostics,
                          opportunistic, false, false,
                          musttail ? EvalCallRewrite::PreserveRoot : EvalCallRewrite::Fold);
        if (stopped()) co_return;
        if (statement.kind == Statement::Kind::Case) {
            Evaluator evaluator(program, diagnostics, caller);
            const auto value = co_await evaluator.expression_async(*statement.expression);
            if (stopped()) co_return;
            if (!value || value->pointer() || !is_integer(value->type)) {
                diagnostics.error(statement.location,
                    "case requires a translation-time integer constant");
            } else {
                replace_eval_value(statement.expression, *value);
            }
        }
    }
    if (statement.condition) {
        co_await rewrite_eval_expr_async(statement.condition, caller, program, diagnostics,
                          opportunistic);
        if (stopped()) co_return;
    }
    for (auto& increment : statement.increments) {
        co_await rewrite_eval_expr_async(increment, caller, program, diagnostics,
                          opportunistic);
        if (stopped()) co_return;
    }
    if (statement.first) {
        co_await rewrite_eval_statement_async(*statement.first, caller, program, diagnostics,
                               opportunistic);
        if (stopped()) co_return;
    }
    if (statement.second) {
        co_await rewrite_eval_statement_async(*statement.second, caller, program, diagnostics,
                               opportunistic);
        if (stopped()) co_return;
    }
}

EvaluationTask<bool> expand_evaluation_impl_async(Program& program, Diagnostics& diagnostics,
                       bool opportunistic) {
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    // These functions disappear before runtime HIR declaration merging. Check
    // their source interfaces here, without assigning a physical call ABI.
    for (std::size_t i = 0; i < program.functions.size(); ++i) {
        const auto& left = *program.functions[i];
        for (std::size_t j = i + 1; j < program.functions.size(); ++j) {
            const auto& right = *program.functions[j];
            if ((!left.has_meta_signature() && !right.has_meta_signature()) ||
                !same_function_entity(left, right)) continue;
            if (left.body && right.body)
                diagnostics.error(right.location, "duplicate definition of meta helper '" + right.name + "'");
            else if (left.linkage != right.linkage ||
                !same_type(function_type(left.return_type, left.parameters, left.variadic),
                           function_type(right.return_type, right.parameters, right.variadic)))
                diagnostics.error(right.location, "declarations of meta helper '" + right.name +
                    "' have incompatible interfaces");
        }
    }
    for (std::size_t record_index = 0; record_index < program.records.size(); ++record_index) {
        const auto identity = program.records[record_index].nominal_identity;
        const auto* owner = identity ? lexical_function(program, identity->function_scope) : nullptr;
        if (owner && evaluation_only(*owner)) continue;
        for (std::size_t member_index = 0;
             member_index < program.records[record_index].members.size(); ++member_index) {
            auto& member = program.records[record_index].members[member_index];
            if (contains_meta_type(member.type))
                diagnostics.error(member.location, "meta values cannot be record members");
            if (!member.bit_width || contains_layout_query(*member.bit_width)) continue;
            // Keep the allocated requirement visible to nested source/layout
            // queries. Preparation may grow either vector; reacquire its slot
            // after suspension rather than borrowing that slot across the proof.
            const auto* width = member.bit_width.get();
            Evaluator evaluator(program, diagnostics, nullptr);
            const auto value = co_await evaluator.required_integer_async(*width);
            if (value) replace_required_integer(
                program.records[record_index].members[member_index].bit_width,
                *value, program.address_bits);
            else evaluator.diagnose(width->location);
            if (stopped()) co_return false;
        }
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (!function->body) continue;
        std::vector<NameMap<bool>> scopes(1);
        for (const auto& parameter : function->parameters)
            scopes.back()[name_key(parameter)] = parameter.type->is_const;
        const auto check_expression = [&](const Expr* root) {
            struct Frame { const Expr* expression; bool direct_callee; };
            std::vector<Frame> pending{{root, false}};
            while (!pending.empty()) {
                const auto [expression, direct_callee] = pending.back();
                pending.pop_back();
                if (!expression) continue;
                if (expression->kind == Expr::Kind::Name && !direct_callee &&
                    std::none_of(scopes.rbegin(), scopes.rend(), [&](const auto& scope) {
                        return scope.contains(name_key(*expression));
                    }) && !resolve_object(program, function, *expression)) {
                    if (const auto* target = resolve_function(program, function, *expression,
                            [](const FunctionDecl& candidate) { return evaluation_only(candidate); }))
                        diagnostics.error(expression->location,
                            "eval-only function '" + target->name + "' has no runtime address");
                }
                const bool write = expression->kind == Expr::Kind::Assign ||
                    (expression->kind == Expr::Kind::Unary &&
                     (expression->text == "++" || expression->text == "--" ||
                      expression->text == "post++" || expression->text == "post--"));
                const Expr* destination = expression->left.get();
                while (write && destination && destination->kind == Expr::Kind::Parenthesized)
                    destination = destination->left.get();
                if (write && destination && destination->kind == Expr::Kind::Name) {
                    std::optional<bool> read_only;
                    const auto key = name_key(*destination);
                    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                        const auto found = scope->find(key);
                        if (found != scope->end()) { read_only = found->second; break; }
                    }
                    if (!read_only) {
                        if (const auto* object = resolve_object(program, function, *destination))
                            read_only = object->type->is_const;
                    }
                    if (read_only.value_or(false))
                        diagnostics.error(expression->location, "cannot write a const cell");
                }

                for (auto entry = expression->initializer_entries.rbegin();
                     entry != expression->initializer_entries.rend(); ++entry) {
                    pending.push_back({entry->value.get(), false});
                    for (auto designator = entry->designators.rbegin();
                         designator != entry->designators.rend(); ++designator)
                        pending.push_back({designator->index.get(), false});
                }
                for (auto argument = expression->arguments.rbegin();
                     argument != expression->arguments.rend(); ++argument)
                    pending.push_back({argument->get(), false});
                pending.push_back({expression->third.get(), false});
                pending.push_back({expression->right.get(), false});
                pending.push_back({expression->left.get(), expression->kind == Expr::Kind::Call ||
                    (direct_callee && expression->kind == Expr::Kind::Parenthesized)});
            }
        };
        enum class Visit { Enter, Expressions, Exit };
        struct StatementFrame { const Statement* statement; Visit visit; };
        std::vector<StatementFrame> pending{{function->body.get(), Visit::Enter}};
        while (!pending.empty()) {
            const auto [node, visit] = pending.back();
            pending.pop_back();
            const auto& statement = *node;
            if (visit == Visit::Exit) { scopes.pop_back(); continue; }
            if (visit == Visit::Expressions) {
                check_expression(statement.expression.get());
                check_expression(statement.condition.get());
                for (const auto& increment : statement.increments) check_expression(increment.get());
                continue;
            }
            const bool scoped = statement.kind == Statement::Kind::Compound ||
                statement.kind == Statement::Kind::For;
            if (scoped) {
                scopes.emplace_back();
                pending.push_back({node, Visit::Exit});
            }
            if (statement.declaration) {
                if (contains_meta_type(statement.declaration->type) &&
                    (!evaluation_only(*function) || !is_meta_type(statement.declaration->type))) {
                    diagnostics.error(statement.declaration->location,
                        "meta values cannot have runtime local storage");
                }
                const auto& declaration = *statement.declaration;
                if (is_meta_type(declaration.type) &&
                    (declaration.type->is_volatile || declaration.type->is_atomic ||
                     declaration.storage_static || declaration.storage_register || declaration.storage_stack ||
                     declaration.location_name || !declaration.attributes.empty()))
                    diagnostics.error(declaration.location,
                        "meta cells require automatic translation-only storage without runtime qualifiers");
                scopes.back()[name_key(*statement.declaration)] = statement.declaration->type->is_const;
                check_expression(statement.declaration->initializer.get());
                check_expression(statement.declaration->dynamic_array_bound.get());
            }

            if (statement.second) pending.push_back({statement.second.get(), Visit::Enter});
            for (auto child = statement.statements.rbegin(); child != statement.statements.rend(); ++child)
                pending.push_back({child->get(), Visit::Enter});
            pending.push_back({node, Visit::Expressions});
            if (statement.first) pending.push_back({statement.first.get(), Visit::Enter});
        }
        Evaluator source_validator(program, diagnostics, function, function->source_namespace);
        if (!(co_await source_validator.validate_source_body_async(*function))) source_validator.diagnose(function->location);
        if (stopped()) co_return false;
    }
    if (diagnostics.errors() != 0) co_return false;
    for (const auto& function : program.functions) {
        if (contains_meta_type(function->return_type) &&
            !is_meta_type(function->return_type))
            diagnostics.error(function->location, "meta types cannot be nested in runtime function types");
        for (const auto& parameter : function->parameters) {
            if (contains_meta_type(parameter.type) &&
                !is_meta_type(parameter.type))
                diagnostics.error(parameter.location, "meta types cannot be nested in runtime function types");
        }
        if (!evaluation_only(*function)) continue;
        if (function->has_meta_signature()) {
            if (function->linkage != Linkage::Static)
                diagnostics.error(function->location,
                    "function with a meta type in its signature must be static");
        }
        if (!function->body && !(function->has_meta_signature() &&
            std::any_of(program.functions.begin(), program.functions.end(), [&](const auto& candidate) {
                return candidate->body && same_function_entity(*function, *candidate);
            }))) {
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
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (evaluation_only(*function)) continue;
        if (function->body) {
            co_await rewrite_eval_statement_async(*function->body, function, program,
                                   diagnostics, opportunistic);
            if (stopped()) co_return false;
        }
    }
    for (std::size_t index = 0; index < program.objects.size(); ++index) {
        if (stopped()) co_return false;
        auto* object = program.objects[index].get();
        if (contains_meta_type(object->type)) {
            diagnostics.error(object->location,
                "meta values cannot have runtime object storage");
            continue;
        }
        if (object->initializer) {
            auto* owner = object_lexical_function(program, *object);
            const auto source_namespace = owner ? owner->source_namespace : namespace_prefix(object->name);
            const bool byte_array = object->type &&
                object->type->kind == Type::Kind::Array && object->type->element &&
                object->type->element->kind == Type::Kind::Builtin &&
                object->type->element->builtin == BuiltinType::U8;
            if (byte_array && object->initializer->kind != Expr::Kind::String &&
                object->initializer->kind != Expr::Kind::AggregateInitializer) {
                Evaluator evaluator(program, diagnostics, owner, source_namespace);
                GenericExpansionState query_scope;
                if (owner) collect_function_types(*owner, query_scope);
                auto bytes = co_await evaluator.required_bytes_async(*object->initializer, query_scope.local_types);
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
            bool folded_pointer{};
            if (is_integer(object->type))
                bind_label_conversions(object->initializer, owner, program);
            if (object->type && object->type->kind == Type::Kind::Pointer)
                folded_pointer = co_await fold_pointer_integer_initializer_async(object->initializer,
                    program, diagnostics, owner, source_namespace);
            if (stopped()) co_return false;
            if (folded_pointer) {
                // The destination conversion is target-owned, not an optional fold.
            } else if (is_label_type(object->type)) {
                Evaluator evaluator(program, diagnostics, owner, source_namespace);
                auto label = co_await evaluator.required_label_async(*object->initializer);
                if (label) object->initializer = std::move(label);
                else evaluator.diagnose(object->initializer->location);
            } else if ((is_integer(object->type) || is_floating(object->type)) &&
                object->initializer->kind !=
                    Expr::Kind::AggregateInitializer &&
                !contains_relocation_candidate(
                    *object->initializer, program,
                    source_namespace)) {
                // Required initializers own the complete expression. Visiting
                // child calls first would evaluate untaken logical/conditional
                // arms and lose their short-circuit semantics.
                if (!contains_layout_query(*object->initializer) ||
                    (program.evaluation_size_of && program.evaluation_align_of)) {
                    if (is_floating(object->type)) {
                        (void)(co_await rewrite_required_floating_async(object->initializer, owner,
                                                  program, diagnostics,
                                                  object->type));
                    } else {
                        (void)(co_await rewrite_required_integer_async(object->initializer, owner,
                                                 program, diagnostics));
                    }
                }
            } else {
                co_await rewrite_eval_expr_async(object->initializer, owner, program,
                                  diagnostics, opportunistic, true);
            }
            if (stopped()) co_return false;
            infer_initializer_array_bound(object->type,
                                          object->initializer.get(),
                                          diagnostics);
            if (object->type && object->type->kind == Type::Kind::Array &&
                object->type->lanes == 0)
                diagnostics.error(object->initializer->location,
                    "an omitted array bound requires a string, brace, or meta-byte initializer");
            co_await fold_static_initializer_async(*object->initializer, object->type,
                                    program, diagnostics, nullptr, nullptr, source_namespace, owner);
            if (stopped()) co_return false;
        }
    }
    if (stopped()) co_return false;
    // Required assertions must retain access to translation-only helpers.
    // Fold their scalar value before removing those helpers; the target
    // finalization stage still owns assertion success/failure diagnostics.
    for (std::size_t index = 0; index < program.static_assertions.size(); ++index) {
        auto assertion = std::move(program.static_assertions[index]);
        struct RestoreAssertion {
            Program& program;
            std::size_t index;
            StaticAssertDecl& assertion;
            ~RestoreAssertion() { program.static_assertions[index] = std::move(assertion); }
        } restore{program, index, assertion};
        if (contains_layout_query(*assertion.condition) && !program.evaluation_size_of)
            continue;
        Evaluator evaluator(program, diagnostics, assertion_lexical_function(program, assertion),
                            assertion.source_namespace);
        const auto value = co_await evaluator.required_scalar_with_types_async(*assertion.condition,
                            assertion.local_types);
        if (value) replace_eval_value(assertion.condition, *value);
        else evaluator.diagnose(assertion.location);
        if (stopped()) co_return false;
    }
    program.functions.erase(
        std::remove_if(program.functions.begin(), program.functions.end(),
                       [&](auto& function) {
                           if (!evaluation_only(*function)) return false;
                           // Later target layout and patch/value proofs can
                           // still call these source helpers. Retain ownership,
                           // not a runtime declaration or a dangling name entry.
                           program.evaluation_definitions.push_back(std::move(function));
                           return true;
                       }),
        program.functions.end());
    for (auto& function : program.functions) {
        std::erase_if(function->attributes, [](const Attribute& attribute) {
            return attribute.name == "runtime_only";
        });
    }
    co_return diagnostics.errors() == 0;
}

bool expand_evaluation(Program& program, Diagnostics& diagnostics, bool opportunistic) {
    return expand_evaluation_impl_async(program, diagnostics, opportunistic).run();
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
    for (const auto& increment : statement.increments)
        collect_patch_expressions(*increment, patches);
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
    for (auto& increment : statement.increments)
        resolve_raw_inline_expr(increment, caller, program, diagnostics);
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
    explicit StringPoolLifter(Program& program) : program_(program) {}

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
    // Unique in the group, and independent of other units' literals.
    std::string unit_ordinal(const std::string& unit) {
        return std::to_string(stable_hash(unit)) + '.' +
               std::to_string(program_.literal_ordinals[unit]++);
    }

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
            auto object = std::make_unique<ObjectDecl>();
            object->location = expression->location;
            object->source_unit = source_unit_.empty() && expression->location.file
                ? expression->location.file->source_unit_at(expression->location.line) : source_unit_;
            const auto name = "$value." + unit_ordinal(object->source_unit);
            object->name = name;
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
                    const auto record = program_.record_definition(destination->nominal_key());
                    if (record) {
                        const auto member = std::find_if(
                            record->members.begin(), record->members.end(),
                            [&](const RecordMemberDecl& candidate) {
                                return candidate.member_name() == first->member_name();
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
            auto object = std::make_unique<ObjectDecl>();
            object->location = expression->location;
            object->source_unit = source_unit_.empty() && expression->location.file
                ? expression->location.file->source_unit_at(expression->location.line)
                : source_unit_;
            const auto name = "$string." + unit_ordinal(object->source_unit);
            object->name = name;
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
        for (auto& increment : statement.increments) rewrite(increment);
        if (statement.first) rewrite(*statement.first);
        for (auto& child : statement.statements) rewrite(*child);
        if (statement.second) rewrite(*statement.second);
    }

    Program& program_;
    std::string source_unit_;
    bool static_initializer_{};
};

void lift_string_literals(Program& program) {
    StringPoolLifter(program).run();
}

void lift_pointer_argument_strings(Program& program, std::unique_ptr<Expr>& expression,
                                   const FunctionDecl* caller) {
    StringPoolLifter(program).pointer_argument(expression, caller);
}

} // namespace

ContinuationTask<bool> expand_evaluation_async(Program& program,
    Diagnostics& diagnostics, bool opportunistic) {
    co_return co_await expand_evaluation_impl_async(program, diagnostics, opportunistic);
}

std::unique_ptr<Expr> copy_expression(const Expr& source) { return clone_expr(source); }

std::unique_ptr<FunctionDecl> copy_evaluation_declaration(const FunctionDecl& source) {
    auto result = std::make_unique<FunctionDecl>();
    result->location = source.location;
    result->name = source.name;
    result->binding = source.binding;
    result->fresh = source.fresh;
    result->source_namespace = source.source_namespace;
    result->source_unit = source.source_unit;
    result->imports = source.imports;
    result->translation_context = source.translation_context;
    result->return_type = clone_type(source.return_type);
    for (const auto& requirement : source.required_types)
        result->required_types.push_back({clone_type(requirement.type), clone_type(requirement.compatible_with),
            requirement.location, requirement.source_namespace, requirement.name, requirement.kind});
    result->parameters = source.parameters;
    for (auto& parameter : result->parameters) {
        parameter.type = clone_type(parameter.type);
        parameter.declared_array_type = clone_type(parameter.declared_array_type);
    }
    result->attributes = clone_attributes(source.attributes, {}, {});
    result->generic_parameters = source.generic_parameters;
    for (auto& parameter : result->generic_parameters) parameter.value_type = clone_type(parameter.value_type);
    result->generic_tag_owner = source.generic_tag_owner;
    result->header_types = source.header_types;
    result->function_scope = source.function_scope;
    for (const auto& assertion : source.deferred_static_assertions)
        result->deferred_static_assertions.push_back({assertion.location, assertion.source_namespace,
            assertion.condition ? clone_expr(*assertion.condition) : nullptr, assertion.message});
    result->result_location = source.result_location;
    if (source.body) result->body = clone_statement(*source.body, {}, {});
    result->linkage = source.linkage;
    result->variadic = source.variadic;
    result->inline_hint = source.inline_hint;
    result->invocation_specialization = source.invocation_specialization;
    result->generic_instance = source.generic_instance;
    return result;
}

std::unique_ptr<ObjectDecl> copy_evaluation_declaration(const ObjectDecl& source) {
    auto result = std::make_unique<ObjectDecl>();
    result->location = source.location;
    result->name = source.name;
    result->binding = source.binding;
    result->fresh = source.fresh;
    result->source_unit = source.source_unit;
    result->lexical_function = source.lexical_function;
    result->type = clone_type(source.type);
    result->attributes = clone_attributes(source.attributes, {}, {});
    if (source.initializer) result->initializer = clone_expr(*source.initializer);
    result->linkage = source.linkage;
    return result;
}

RecordDecl copy_evaluation_declaration(const RecordDecl& source) {
    RecordDecl result;
    result.location = source.location;
    result.name = source.name;
    result.is_union = source.is_union;
    result.complete = source.complete;
    result.nominal_identity = source.nominal_identity;
    result.attributes = clone_attributes(source.attributes, {}, {});
    for (const auto& member : source.members)
        result.members.push_back({member.location, member.name, clone_type(member.type),
            member.bit_width ? clone_expr(*member.bit_width) : nullptr,
            clone_attributes(member.attributes, {}, {}), member.fresh});
    return result;
}

EnumDecl copy_evaluation_declaration(const EnumDecl& source) {
    EnumDecl result;
    result.location = source.location;
    result.name = source.name;
    result.local = source.local;
    result.underlying = source.underlying;
    result.captured_type_errors = source.captured_type_errors;
    result.nominal_identity = source.nominal_identity;
    result.attributes = clone_attributes(source.attributes, {}, {});
    for (const auto& item : source.enumerators)
        result.enumerators.push_back({item.location, item.name,
            item.initializer ? clone_expr(*item.initializer) : nullptr, item.value, item.binding});
    return result;
}

namespace {
EvaluationTask<void> validate_static_assertion_async(Program& program, Diagnostics& diagnostics,
    const StaticAssertDecl& assertion, const FunctionDecl* caller = nullptr,
    const LayoutQuery* size_of = nullptr, const LayoutQuery* align_of = nullptr) {
    if (const auto* owner = assertion_lexical_function(program, assertion)) caller = owner;
    Evaluator evaluator(program, diagnostics, caller, assertion.source_namespace, size_of, align_of);
    const auto value = co_await evaluator.required_scalar_with_types_async(*assertion.condition, assertion.local_types);
    if (!value) {
        if (!evaluator.resource_exhausted())
            diagnostics.error(assertion.location,
                "$::static_assert condition is not a scalar constant expression");
        evaluator.diagnose(assertion.location);
    } else if (!value->truthy()) {
        diagnostics.error(assertion.location, "$::static_assert failed: " + assertion.message);
    }
}


} // namespace

struct ExpansionSemantics::Impl {
    Program& program;
    Diagnostics& diagnostics;
    std::string mangling;
    GenericExpansionState state;
    OperatorBinder operators;
    std::unordered_set<std::size_t> active_enumerations;
    std::size_t next_assertion{};
    decltype(Program::evaluation_prepare_type) previous_type;
    decltype(Program::evaluation_prepare_function) previous_function;
    decltype(Program::evaluation_prepare_expression) previous_expression;
    decltype(Program::evaluation_prepare_enumerator) previous_enumerator;
    GenericAbiCanonicalizer previous_abi;

    Impl(Program& declarations, Diagnostics& reporter, std::string model,
         GenericAbiCanonicalizer canonical_abi)
        : program(declarations), diagnostics(reporter), mangling(std::move(model)),
          operators(program, diagnostics),
          previous_type(std::move(program.evaluation_prepare_type)),
          previous_function(std::move(program.evaluation_prepare_function)),
          previous_expression(std::move(program.evaluation_prepare_expression)),
          previous_enumerator(std::move(program.evaluation_prepare_enumerator)),
          previous_abi(std::move(program.canonical_callable_abi)) {
        state.expansion_evaluation = true;
        state.canonical_abi = canonical_abi ? std::move(canonical_abi) : previous_abi;
        program.canonical_callable_abi = state.canonical_abi;
        state.pointer_resolver = program.evaluation_pointer_resolver;
        state.rewrite_operator = [this](std::unique_ptr<Expr>& expression, const FunctionDecl* caller) {
            operators.rewrite(expression, caller, state);
        };
        program.evaluation_prepare_type = [this](const TypePtr& type, EvaluationLayoutKind kind) -> EvaluationTask<bool> {
            co_return co_await prepare_generic_layout_type_async(type, kind, program, diagnostics, state, mangling);
        };
        program.evaluation_prepare_function = [this](FunctionDecl& function) -> EvaluationTask<bool> {
            const auto errors = diagnostics.errors();
            struct Restore {
                GenericExpansionState& state;
                const FunctionDecl* previous;
                const FunctionDecl* prepared;
                ~Restore() {
                    state.rewritten_functions.erase(prepared);
                    state.invocation_owner = previous;
                }
            } restore{state, state.invocation_owner, &function};
            state.invocation_owner = &function;
            state.rewritten_functions.erase(&function);
            co_await rewrite_generic_function_async(function, program, diagnostics, state, mangling);
            co_return diagnostics.errors() == errors;
        };
        program.evaluation_prepare_expression = [this](std::unique_ptr<Expr>& expression,
            const FunctionDecl* caller, std::span<const std::pair<NameKey, TypePtr>> types) -> EvaluationTask<bool> {
            const auto errors = diagnostics.errors();
            struct Restore {
                GenericExpansionState& state;
                const FunctionDecl* previous;
                decltype(GenericExpansionState::locals) locals;
                decltype(GenericExpansionState::local_types) local_types;
                ~Restore() {
                    state.locals = std::move(locals);
                    state.local_types = std::move(local_types);
                    state.invocation_owner = previous;
                }
            } restore{state, state.invocation_owner, std::move(state.locals), std::move(state.local_types)};
            state.invocation_owner = caller;
            state.local_types.assign(types.begin(), types.end());
            state.locals.clear();
            for (const auto& [name, type] : types) state.locals.push_back(name);
            co_await rewrite_generic_expr_async(expression, caller, program, diagnostics, state, mangling);
            co_return diagnostics.errors() == errors;
        };
        program.evaluation_prepare_enumerator = [this](Program::EnumerationPosition position) -> EvaluationTask<bool> {
            if (position.declaration >= program.enumerations.size() ||
                position.enumerator >= program.enumerations[position.declaration].enumerators.size()) co_return false;
            if (const auto& owner = program.enumerations[position.declaration].nominal_identity;
                owner && owner->generic_owner) co_return false;
            if (const auto active = program.evaluation_enumerator_position; active &&
                (position.declaration > active->declaration ||
                 (position.declaration == active->declaration && position.enumerator >= active->enumerator)))
                co_return false;
            if (!active_enumerations.insert(position.declaration).second) co_return false;
            struct Pop {
                std::unordered_set<std::size_t>& active;
                std::size_t index;
                ~Pop() { active.erase(index); }
            } pop{active_enumerations, position.declaration};
            // A nested enum dependency must not inherit an outer function's
            // automatic-name classification while preparing generic arguments.
            struct RestoreLocals {
                GenericExpansionState& state;
                decltype(GenericExpansionState::locals) locals;
                decltype(GenericExpansionState::local_types) types;
                ~RestoreLocals() {
                    state.locals = std::move(locals);
                    state.local_types = std::move(types);
                }
            } restore{state, std::move(state.locals), std::move(state.local_types)};
            state.locals.clear();
            state.local_types.clear();
            co_return co_await evaluate_enumerations_async(program, diagnostics, position.declaration,
                [this](std::unique_ptr<Expr>& expression) -> EvaluationTask<void> {
                    co_await rewrite_generic_expr_async(expression, nullptr, program, diagnostics, state, mangling);
                }, position);
        };
    }

    ~Impl() {
        program.evaluation_prepare_type = std::move(previous_type);
        program.evaluation_prepare_function = std::move(previous_function);
        program.evaluation_prepare_expression = std::move(previous_expression);
        program.evaluation_prepare_enumerator = std::move(previous_enumerator);
        program.canonical_callable_abi = std::move(previous_abi);
    }
};

ExpansionSemantics::ExpansionSemantics(Program& program, Diagnostics& diagnostics,
    std::string mangling, GenericAbiCanonicalizer canonical_abi)
    : impl_(std::make_unique<Impl>(program, diagnostics, std::move(mangling), std::move(canonical_abi))) {}

ExpansionSemantics::~ExpansionSemantics() = default;

std::unique_ptr<FunctionDecl> ExpansionSemantics::prepare(const FunctionDecl& function) {
    return prepare_async(function).run();
}

ContinuationTask<std::unique_ptr<FunctionDecl>> ExpansionSemantics::prepare_async(const FunctionDecl& function) {
    GenericResourceFrame resource_frame(impl_->state, impl_->program);
    if (impl_->state.resource_failed(impl_->program)) co_return nullptr;
    auto result = copy_evaluation_declaration(function);
    // An invocation clone has no persistent identity. Do not leave its address
    // in the preparation cache if an awaited source/layout callback throws.
    struct RemoveEntryClone {
        GenericExpansionState& state;
        const FunctionDecl* function;
        ~RemoveEntryClone() { state.rewritten_functions.erase(function); }
    } remove_entry{impl_->state, result.get()};
    const auto errors = impl_->diagnostics.errors();
    // Expansion declarations grow monotonically. Validate newly published
    // generic interfaces before instantiation, just as in ordinary expansion;
    // otherwise their deferred promises disappear with helper erasure.
    if (!validate_generic_redeclarations(impl_->program, impl_->state, impl_->diagnostics)) co_return nullptr;
    if (!impl_->operators.refresh(impl_->state)) co_return nullptr;
    for (auto& assertion : result->deferred_static_assertions)
        impl_->program.static_assertions.push_back(std::move(assertion));
    result->deferred_static_assertions.clear();
    co_await rewrite_generic_function_async(*result, impl_->program, impl_->diagnostics, impl_->state, impl_->mangling);
    // Entry clones are invocation-local. Only declaration-view helper bodies
    // and concrete instances have stable identities in the persistent cache.
    if (impl_->diagnostics.errors() == errors) co_await validate_assertions_async();
    co_return impl_->diagnostics.errors() == errors ? std::move(result) : nullptr;
}

bool ExpansionSemantics::validate_assertions() {
    return validate_assertions_async().run();
}

ContinuationTask<bool> ExpansionSemantics::validate_assertions_async() {
    GenericResourceFrame resource_frame(impl_->state, impl_->program);
    if (impl_->state.resource_failed(impl_->program)) co_return false;
    const auto errors = impl_->diagnostics.errors();
    auto& assertions = impl_->program.static_assertions;
    // Publication can grow the vector during either suspended proof. Keep the
    // active declaration owned and restore it even if a callback throws.
    while (impl_->next_assertion < assertions.size() && impl_->diagnostics.errors() == errors) {
        const auto index = impl_->next_assertion++;
        auto assertion = std::move(assertions[index]);
        struct RestoreAssertion {
            std::vector<StaticAssertDecl>& declarations;
            std::size_t index;
            StaticAssertDecl& assertion;
            ~RestoreAssertion() { declarations[index] = std::move(assertion); }
        } restore_assertion{assertions, index, assertion};
        FunctionDecl context;
        context.source_namespace = assertion.source_namespace;
        if (assertion.location.file)
            context.source_unit = assertion.location.file->source_unit_at(assertion.location.line);
        auto* owner = assertion_lexical_function(impl_->program, assertion);
        {
            struct RestoreLocals {
                GenericExpansionState& state;
                decltype(GenericExpansionState::locals) locals;
                decltype(GenericExpansionState::local_types) types;
                ~RestoreLocals() {
                    state.locals = std::move(locals);
                    state.local_types = std::move(types);
                }
            } restore{impl_->state, std::move(impl_->state.locals), std::move(impl_->state.local_types)};
            impl_->state.locals.clear();
            impl_->state.local_types = assertion.local_types;
            for (const auto& [name, type] : assertion.local_types) impl_->state.locals.push_back(name);
            co_await rewrite_generic_expr_async(assertion.condition, owner ? owner : &context, impl_->program,
                impl_->diagnostics, impl_->state, impl_->mangling);
        }
        if (impl_->diagnostics.errors() == errors)
            co_await validate_static_assertion_async(impl_->program, impl_->diagnostics, assertion, &context);
    }
    co_return impl_->diagnostics.errors() == errors;
}
bool ExpansionSemantics::validate_source(const FunctionDecl& function) {
    return validate_source_async(function).run();
}

ContinuationTask<bool> ExpansionSemantics::validate_source_async(const FunctionDecl& function) {
    auto prepared = co_await prepare_async(function);
    if (!prepared) co_return false;
    Evaluator validator(impl_->program, impl_->diagnostics, prepared.get(),
        prepared->source_namespace, &impl_->program.evaluation_size_of,
        &impl_->program.evaluation_align_of);
    if (co_await validator.validate_source_body_async(*prepared)) co_return true;
    validator.diagnose(prepared->location);
    co_return false;
}

void discard_unreferenced_invocation_specializations(Program& program);

bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls, std::string_view mangling,
                      std::string_view default_abi,
                      const GenericPointerResolver& pointer_resolver,
                      const GenericAbiCanonicalizer& canonical_abi,
                      const EvaluationLayoutInstaller& install_layout) {
    struct RestoreAbiResolver {
        Program& program;
        GenericAbiCanonicalizer previous;
        ~RestoreAbiResolver() { program.canonical_callable_abi = std::move(previous); }
    } restore_abi{program, std::move(program.canonical_callable_abi)};
    program.canonical_callable_abi = canonical_abi ? canonical_abi : restore_abi.previous;
    // Generated code brings copies of the helper-local definitions that it names.
    // Keep one definition of each; their owner's records are laid out for runtime.
    std::vector<std::shared_ptr<const FunctionScopeIdentity>> carried_owners;
    std::unordered_set<NominalTypeKey, NominalTypeKeyHash> defined;
    for (const auto& record : program.records)
        if (record.complete && !record.carried) defined.insert(record.nominal_key());
    for (const auto& enumeration : program.enumerations)
        if (!enumeration.carried) defined.insert(enumeration.nominal_key());
    std::erase_if(program.records, [&](const RecordDecl& record) {
        if (!record.carried) return false;
        carried_owners.push_back(record.nominal_identity->function_scope);
        return !defined.insert(record.nominal_key()).second;
    });
    program.record_index = {};
    std::erase_if(program.enumerations, [&](const EnumDecl& enumeration) {
        return enumeration.carried && !defined.insert(enumeration.nominal_key()).second;
    });
    if (!validate_attribute_names(program, diagnostics)) return false;
    if (!expand_generics(program, diagnostics, mangling, pointer_resolver,
                         program.canonical_callable_abi)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("generic expansion failed without a diagnostic");
        }
        return false;
    }
    materialize_enumerators(program, diagnostics);
    lift_function_pointer_adapters(program, default_abi);
    const auto remember_record_owner = [&](const FunctionDecl& function) {
        if (evaluation_only(function) && function.function_scope &&
            std::find(carried_owners.begin(), carried_owners.end(), function.function_scope) == carried_owners.end() &&
            std::find(program.translation_only_record_scopes.begin(), program.translation_only_record_scopes.end(),
                function.function_scope) == program.translation_only_record_scopes.end())
            program.translation_only_record_scopes.push_back(function.function_scope);
    };
    for (const auto& function : program.functions) remember_record_owner(*function);
    for (const auto& function : program.expansion_definitions) remember_record_owner(*function);
    // Operator attributes have been validated and all bound expressions became
    // ordinary calls during generic/required-expression preparation.
    for (auto& function : program.functions)
        std::erase_if(function->attributes, [](const Attribute& attribute) {
            return attribute.name == "operator";
        });
    if (install_layout) install_layout(program);
    if (diagnostics.errors() != 0) return false;
    if (!expand_evaluation(program, diagnostics, evaluate_calls)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("compile-time evaluation failed without a diagnostic");
        }
        return false;
    }
    discard_unreferenced_invocation_specializations(program);
    if (!expand_raw_inline(program, diagnostics)) {
        if (diagnostics.errors() == 0) {
            diagnostics.command_error("raw-compatible inlining failed without a diagnostic");
        }
        return false;
    }
    lift_string_literals(program);
    return true;
}

// Translation-only uses do not create a runtime-code obligation for a private
// specialization. Preserve any surviving direct, indirect or symbolic use,
// including transitive helpers, before discarding evaluation-only instances.
void discard_unreferenced_invocation_specializations(Program& program) {
    std::unordered_set<const FunctionDecl*> live;
    std::vector<const FunctionDecl*> work;
    const auto keep = [&](const FunctionDecl* function) {
        if (function && live.insert(function).second) work.push_back(function);
    };
    const auto expressions = [&](const Expr* root, const FunctionDecl* caller) {
        std::vector<const Expr*> pending{root};
        while (!pending.empty()) {
            const auto* expression = pending.back(); pending.pop_back();
            if (!expression) continue;
            if (expression->kind == Expr::Kind::Name)
                keep(resolve_function(program, caller, *expression, [](const FunctionDecl&) { return true; }));
            if (expression->evaluated_address) keep(expression->evaluated_address->function);
            if (expression->name_context && expression->name_context->label_address)
                keep(expression->name_context->label_address->owner);
            for (const auto& relocation : expression->object_relocations)
                std::visit([&](const auto& address) {
                    using Address = std::decay_t<decltype(address)>;
                    if constexpr (std::is_same_v<Address, AddressConstant>) keep(address.function);
                    else keep(address.owner);
                }, relocation.address);
            pending.push_back(expression->left.get());
            pending.push_back(expression->right.get());
            pending.push_back(expression->third.get());
            for (const auto& argument : expression->arguments) pending.push_back(argument.get());
            for (const auto& entry : expression->initializer_entries) {
                pending.push_back(entry.value.get());
                for (const auto& designator : entry.designators) pending.push_back(designator.index.get());
            }
        }
    };
    for (const auto& function : program.functions)
        if (!function->invocation_specialization || function->linkage == Linkage::Global ||
            function->attribute("used") || function->attribute("retain")) keep(function.get());
    for (const auto& object : program.objects) {
        FunctionDecl context;
        context.source_namespace = namespace_prefix(object->name);
        context.source_unit = object->source_unit;
        expressions(object->initializer.get(), &context);
    }
    while (!work.empty()) {
        const auto* function = work.back(); work.pop_back();
        std::vector<const Statement*> statements{function->body.get()};
        while (!statements.empty()) {
            const auto* statement = statements.back(); statements.pop_back();
            if (!statement) continue;
            expressions(statement->expression.get(), function);
            expressions(statement->condition.get(), function);
            for (const auto& increment : statement->increments) expressions(increment.get(), function);
            if (statement->declaration) {
                expressions(statement->declaration->initializer.get(), function);
                expressions(statement->declaration->dynamic_array_bound.get(), function);
            }
            statements.push_back(statement->first.get());
            statements.push_back(statement->second.get());
            for (const auto& child : statement->statements) statements.push_back(child.get());
        }
    }
    std::erase_if(program.functions, [&](const auto& function) {
        return function->invocation_specialization && !live.contains(function.get());
    });
}

ContinuationTask<bool> finalize_target_constants_async(Program& program, Diagnostics& diagnostics,
                               LayoutQuery size_of, LayoutQuery align_of) {
    const auto epoch = program.evaluation_resource_errors;
    const auto stopped = [&] { return program.evaluation_resource_errors != epoch; };
    // Required preparation may publish declarations. Retain allocated owners,
    // not references/iterators into their growable unique_ptr vectors.
    for (std::size_t index = 0; index < program.objects.size(); ++index) {
        if (stopped()) co_return false;
        auto* object = program.objects[index].get();
        if (!object->initializer) continue;
        auto* owner = object_lexical_function(program, *object);
        const auto source_namespace = owner ? owner->source_namespace : namespace_prefix(object->name);
        const bool relocation = contains_relocation_candidate(*object->initializer, program, source_namespace);
        if (relocation) {
            co_await fold_relocation_offsets_async(object->initializer, program, diagnostics,
                size_of, align_of, source_namespace, owner);
            if (stopped()) co_return false;
        }
        if (!contains_layout_query(*object->initializer)) continue;
        if (object->initializer->kind == Expr::Kind::AggregateInitializer) {
            co_await fold_static_initializer_async(*object->initializer, object->type, program,
                diagnostics, &size_of, &align_of, source_namespace, owner);
            continue;
        }
        if (object->type && object->type->kind == Type::Kind::Pointer) {
            if (co_await fold_pointer_integer_initializer_async(object->initializer, program, diagnostics,
                    owner, source_namespace, &size_of, &align_of)) continue;
        }
        if (is_floating(object->type) && !relocation) {
            (void)(co_await rewrite_required_floating_async(object->initializer, owner, program,
                diagnostics, object->type, &size_of, &align_of, source_namespace));
            continue;
        }
        if (!is_integer(object->type) || relocation) continue;
        Evaluator evaluator(program, diagnostics, owner, source_namespace, &size_of, &align_of);
        const auto value = co_await evaluator.required_integer_async(*object->initializer);
        if (!value) evaluator.diagnose(object->initializer->location);
        else replace_eval_value(object->initializer, *value);
    }
    if (stopped()) co_return false;
    for (std::size_t index = 0; index < program.static_assertions.size(); ++index) {
        auto assertion = std::move(program.static_assertions[index]);
        struct RestoreAssertion {
            Program& program;
            std::size_t index;
            StaticAssertDecl& assertion;
            ~RestoreAssertion() { program.static_assertions[index] = std::move(assertion); }
        } restore{program, index, assertion};
        co_await validate_static_assertion_async(program, diagnostics, assertion, nullptr, &size_of, &align_of);
        if (stopped()) co_return false;
    }
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        auto* function = program.functions[index].get();
        if (function->body)
            co_await fold_patch_initial_offsets_async(*function->body, program, diagnostics,
                size_of, align_of, function->source_namespace, function);
        if (stopped()) co_return false;
    }
    co_return diagnostics.errors() == 0;
}

bool finalize_target_constants(Program& program, Diagnostics& diagnostics,
                               const LayoutQuery& size_of, const LayoutQuery& align_of) {
    return finalize_target_constants_async(program, diagnostics, size_of, align_of).run();
}

bool validate_expansion_function_declaration(const FunctionDecl& function,
    bool syntax_expander, Diagnostics& diagnostics) {
    const auto errors = diagnostics.errors();
    const auto role = syntax_expander ? "syntax_expander" : "macro";
    const auto input = syntax_expander ? Type::Kind::SyntaxMatch : Type::Kind::Tokens;
    if (function.linkage != Linkage::Static)
        diagnostics.error(function.location, "'" + std::string(role) + "' functions must be static and cannot be global");
    if (!function.return_type || function.return_type->kind != Type::Kind::Tokens)
        diagnostics.error(function.location, "'" + std::string(role) + "' functions must return $::meta::tokens");
    if (!function.body || function.variadic || function.parameters.size() != 1 ||
        function.parameters.front().mode != ParameterMode::In ||
        !function.parameters.front().type || function.parameters.front().type->kind != input ||
        function.parameters.front().name.empty())
        diagnostics.error(function.location, "expansion function requires exactly one 'in [const] " +
            std::string(syntax_expander ? "$::meta::syntax_match" : "$::meta::tokens") + " name' parameter and a body");
    if (function.result_location)
        diagnostics.error(function.location, "expansion functions cannot specify a result location");
    for (const auto& parameter : function.parameters)
        if (parameter.location_name)
            diagnostics.error(parameter.location, "expansion functions cannot specify a parameter location");
    if (!function.generic_parameters.empty())
        diagnostics.error(function.location, "generic expansion functions are not supported");
    for (const auto& attribute : function.attributes) {
        if (!is_known_attribute(attribute.name))
            diagnostics.error(attribute.location, "unknown attribute '" + attribute.name + "'");
        else if (const auto error = function_attribute_error(attribute))
            diagnostics.error(attribute.location, *error);
    }
    if (const auto error = function_attribute_conflict(function))
        diagnostics.error(function.location, *error);
    if (diagnostics.errors() != errors) return false;
    for (const auto& attribute : function.attributes) {
        if (attribute.name == role || attribute.name == "eval_only" ||
            attribute.name == "always_inline" || attribute.name == "noinline" ||
            attribute.name == "hot" || attribute.name == "cold" ||
            attribute.name == "no_stack_protector" || attribute.name == "no_sanitize" ||
            attribute.name == "noreturn") continue;
        if (runtime_contract_attribute(attribute.name))
            diagnostics.error(attribute.location,
                runtime_contract_error(attribute, "an expansion function"));
        else diagnostics.error(attribute.location,
            "attribute '" + attribute.name + "' is not valid on an expansion function");
    }
    return diagnostics.errors() == errors;
}

std::optional<TokenSequence> evaluate_procedural_body(
    const FunctionDecl& macro, const TokenSequence& input, unsigned address_bits,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse,
    std::shared_ptr<const SyntaxContext> call_context, Program* declarations) {
    return evaluate_procedural_body_async(macro, input, address_bits,
        size_of, align_of, std::move(macro_context), diagnostics, limits, layout, parse,
        std::move(call_context), declarations).run();
}

ContinuationTask<std::optional<TokenSequence>> evaluate_procedural_body_async(
    const FunctionDecl& macro, TokenSequence input, unsigned address_bits,
    LayoutQuery size_of, LayoutQuery align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, SyntaxParseCallback parse,
    std::shared_ptr<const SyntaxContext> call_context, Program* declarations) {
    Program empty;
    Program& context = declarations ? *declarations : empty;
    context.address_bits = address_bits;
    context.evaluation_limits = limits;
    context.evaluation_layout = layout;
    const auto invocation = macro_context->invocation;
    Evaluator evaluator(context, diagnostics, &macro, macro.source_namespace,
                        &size_of, &align_of, nullptr, std::move(macro_context), parse,
                        std::move(call_context));
    if (!evaluator.charge_input_tokens(input, invocation)) {
        evaluator.diagnose(invocation);
        co_return std::nullopt;
    }
    EvalValue argument{UInt128{}, tokens_type()};
    argument.tokens = std::make_shared<const TokenSequence>(std::move(input));
    const auto result = co_await evaluator.call_values_async(macro, {argument}, invocation);
    if (!result || !result->tokens) {
        evaluator.diagnose(invocation);
        co_return std::nullopt;
    }
    co_return *result->tokens;
}

std::optional<TokenSequence> evaluate_syntax_body(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse,
    std::shared_ptr<const SyntaxContext> call_context, Program* declarations) {
    return evaluate_syntax_body_async(function, std::move(input), address_bits,
        size_of, align_of, std::move(macro_context), diagnostics, limits, layout, parse,
        std::move(call_context), declarations).run();
}

ContinuationTask<std::optional<TokenSequence>> evaluate_syntax_body_async(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, LayoutQuery size_of, LayoutQuery align_of,
    std::shared_ptr<const SyntaxContext> macro_context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, SyntaxParseCallback parse,
    std::shared_ptr<const SyntaxContext> call_context, Program* declarations) {
    Program empty;
    Program& context = declarations ? *declarations : empty;
    context.address_bits = address_bits;
    context.evaluation_limits = limits;
    context.evaluation_layout = layout;
    const auto invocation = macro_context->invocation;
    Evaluator evaluator(context, diagnostics, &function, function.source_namespace,
                        &size_of, &align_of, nullptr, std::move(macro_context), parse,
                        std::move(call_context));
    if (!input || !evaluator.charge_input_match(*input, invocation)) {
        evaluator.diagnose(invocation);
        co_return std::nullopt;
    }
    EvalValue argument{UInt128{}, syntax_match_type()};
    argument.syntax_match = std::move(input);
    const auto result = co_await evaluator.call_values_async(function, {argument}, invocation);
    if (!result || !result->tokens) {
        evaluator.diagnose(invocation);
        co_return std::nullopt;
    }
    co_return *result->tokens;
}

EvaluationIntegerResult evaluate_target_integer_requirement(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context) {
    return evaluate_target_integer_requirement_async(program, expression, diagnostics, size_of, align_of,
        source_namespace, caller, local_types, context).run();
}

std::optional<Expr::IntegerConstant> evaluate_target_integer_constant(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context) {
    return evaluate_target_integer_constant_async(program, expression, diagnostics, size_of, align_of,
        source_namespace, caller, local_types, context).run();
}

std::optional<std::uint32_t> evaluate_fixed_array_bound(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types) {
    return evaluate_fixed_array_bound_async(program, expression, diagnostics, size_of, align_of,
        source_namespace, caller, local_types).run();
}

bool resolve_vector_bound(Program& program, const TypePtr& type, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of, std::string_view source_namespace,
    const FunctionDecl* caller, std::span<const std::pair<NameKey, TypePtr>> local_types,
    EvaluationIntegerContext context) {
    return resolve_vector_bound_async(program, type, diagnostics, size_of, align_of,
        source_namespace, caller, local_types, context).run();
}

std::optional<unsigned> evaluate_alignment_attribute(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace) {
    return evaluate_alignment_attribute_async(program, attribute, diagnostics, size_of, align_of,
        subject, source_namespace).run();
}

ContinuationTask<EvaluationIntegerResult> evaluate_target_integer_requirement_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types,
    EvaluationIntegerContext context) {
    if (program.evaluation_required_integer) {
        // Nested evaluators temporarily replace the installed callback; retain
        // this callable while it creates and runs its isolated source probe.
        const auto query = program.evaluation_required_integer;
        co_return co_await query.async(expression, diagnostics, size_of, align_of,
                     source_namespace, caller, local_types, context);
    }
    Evaluator evaluator(program, diagnostics, caller,
                        std::string(source_namespace), size_of ? &size_of : nullptr,
                        align_of ? &align_of : nullptr);
    if (context == EvaluationIntegerContext::StagedDefinition && caller &&
        caller->definition() && evaluation_only(*caller)) {
        bool needs_context{};
        const auto value = co_await evaluator.probe_integer_async(expression, local_types, &needs_context, true);
        if (needs_context) co_return {EvaluationIntegerResult::Status::ContextUnavailable, {}};
        if (value) co_return {EvaluationIntegerResult::Status::Value, value};
        evaluator.diagnose(expression.location);
        co_return {};
    }
    const auto value = co_await evaluator.required_integer_with_types_async(expression, local_types);
    if (!value) {
        if (context != EvaluationIntegerContext::ProbeDefinition || evaluator.resource_exhausted())
            evaluator.diagnose(expression.location);
        co_return {};
    }
    co_return {EvaluationIntegerResult::Status::Value, Expr::IntegerConstant{value->integer, value->type->builtin}};
}

ContinuationTask<std::optional<Expr::IntegerConstant>> evaluate_target_integer_constant_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context) {
    co_return (co_await evaluate_target_integer_requirement_async(program, expression, diagnostics, size_of, align_of,
        source_namespace, caller, local_types, context)).value;
}

ContinuationTask<std::optional<std::uint32_t>> evaluate_fixed_array_bound_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types) {
    const auto value = co_await evaluate_target_integer_constant_async(program, expression, diagnostics,
        size_of, align_of, source_namespace, caller, local_types);
    if (!value) co_return {};
    const auto bound = fixed_array_bound_value(*value, program.address_bits);
    if (!bound) {
        diagnostics.error(expression.location,
            "fixed array bound must be a positive integer representable in 32 bits");
        co_return {};
    }
    co_return bound;
}

ContinuationTask<bool> resolve_vector_bound_async(Program& program, const TypePtr& type, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of, std::string_view source_namespace,
    const FunctionDecl* caller, std::span<const std::pair<NameKey, TypePtr>> local_types,
    EvaluationIntegerContext context) {
    if (!type || type->kind != Type::Kind::Vector || !type->vector_bound || type->lanes != 0)
        co_return true;
    const auto location = type->vector_bound->location;
    if (const auto* error = vector_element_error(type->element)) {
        diagnostics.error(location, error);
        co_return false;
    }
    const auto result = co_await evaluate_target_integer_requirement_async(program, *type->vector_bound,
        diagnostics, size_of, align_of, source_namespace, caller, local_types, context);
    if (result.status == EvaluationIntegerResult::Status::ContextUnavailable) {
        type->vector_extent_dependency = Type::VectorExtentDependency::ExpansionContext;
        co_return true;
    }
    if (!result.value) co_return false;
    const auto extent = vector_bound_value(type, *result.value, program.address_bits);
    if (!extent.lanes) {
        diagnostics.error(location, extent.error);
        co_return false;
    }
    type->lanes = *extent.lanes;
    type->vector_extent_dependency = Type::VectorExtentDependency::None;
    co_return true;
}

ContinuationTask<bool> resolve_alignment_requests_async(Program& program, const TypePtr& type,
    Diagnostics& diagnostics, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context) {
    if (!type || type->alignment_requests.empty()) co_return true;
    const auto requests = std::move(type->alignment_requests);
    type->alignment_requests.clear();
    bool valid = true;
    for (const auto& request : requests) {
        const auto result = co_await evaluate_target_integer_requirement_async(program, *request,
            diagnostics, size_of, align_of, source_namespace, caller, local_types, context);
        if (result.status == EvaluationIntegerResult::Status::ContextUnavailable) {
            type->alignment_requests.push_back(request);
            continue;
        }
        if (!result.value) {
            valid = false;
            continue;
        }
        const auto alignment = alignment_value(*result.value);
        if (!alignment) {
            diagnostics.error(request->location,
                "aligned argument must be a positive power-of-two integer constant");
            valid = false;
            continue;
        }
        type->alignment = std::max(type->alignment, *alignment);
    }
    co_return valid;
}

std::unique_ptr<Expr> evaluate_target_pointer_constant(
    Program& program, const Expr& expression, const TypePtr& destination,
    const FunctionDecl* caller, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    const GenericPointerResolver& resolver) {
    return evaluate_target_pointer_constant_async(program, expression, destination, caller,
        diagnostics, size_of, align_of, resolver).run();
}

ContinuationTask<std::unique_ptr<Expr>> evaluate_target_pointer_constant_async(
    Program& program, const Expr& expression, const TypePtr& destination,
    const FunctionDecl* caller, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    const GenericPointerResolver& resolver) {
    // A pointer-producing helper reached by target normalization is part of
    // the active proof. Share its resources, but keep declaration-only lookup
    // and capabilities instead of borrowing invocation locals or meta context.
    if (program.evaluation_generic_value) {
        const auto query = program.evaluation_generic_value;
        auto result = co_await query.async(expression, destination, diagnostics, caller, {},
            EvaluationIntegerContext::Definition);
        co_return std::move(result.value);
    }
    const auto errors = diagnostics.errors();
    Evaluator evaluator(program, diagnostics, caller,
                        caller ? caller->source_namespace : std::string{},
                        &size_of, &align_of, &resolver);
    auto value = co_await evaluator.required_pointer_async(expression, destination);
    if (!value && diagnostics.errors() == errors) evaluator.diagnose(expression.location);
    co_return value;
}

ContinuationTask<std::optional<unsigned>> evaluate_alignment_attribute_async(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace) {
    if (attribute.arguments.size() != 1 ||
        !attribute.expression_argument) {
        diagnostics.error(attribute.location,
                          "aligned on " + std::string(subject) +
                              " requires one integer argument");
        co_return std::nullopt;
    }
    auto value = attribute.expression_argument->evaluated_integer;
    if (!value)
        value = co_await evaluate_target_integer_constant_async(program, *attribute.expression_argument,
            diagnostics, size_of, align_of, source_namespace);
    if (!value) co_return std::nullopt;
    const auto alignment = alignment_value(*value);
    if (!alignment) {
        diagnostics.error(
            attribute.location,
            "aligned argument must be a positive power-of-two integer constant");
        co_return std::nullopt;
    }
    co_return alignment;
}

} // namespace cross
