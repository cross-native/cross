// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/hir.hpp"

#include "frontend/semantic.hpp"
#include "frontend/record_constraints.hpp"
#include "middle/initializer.hpp"
#include "model/model.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross::hir {
namespace {

std::string decode_attribute_string(const Attribute* attribute) {
    if (!attribute || attribute->arguments.size() != 1) return {};
    return decode_string_literal(attribute->arguments.front()).value_or(std::string{});
}

std::string sanitize(std::string_view text) {
    std::string result;
    for (const char ch : text) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9');
        result.push_back(safe ? ch : '_');
    }
    return result;
}

std::uint64_t stable_hash(std::string_view text) {
    std::uint64_t value = 1469598103934665603ULL;
    for (const char ch : text) {
        value ^= static_cast<unsigned char>(ch);
        value *= 1099511628211ULL;
    }
    return value;
}

std::string entity_key(Linkage linkage, std::string_view source_unit,
                       std::string_view name) {
    if (linkage != Linkage::Static) return std::string(name);
    return std::string(source_unit) + '\n' + std::string(name);
}

std::string_view parameter_mode_name(ParameterMode mode) {
    switch (mode) {
    case ParameterMode::In: return "in";
    case ParameterMode::Out: return "out";
    case ParameterMode::InOut: return "inout";
    }
    return {};
}

std::string model_entity_name(std::string_view qualified_name, const FreshIdentifier* fresh) {
    if (!fresh) return std::string(qualified_name);
    // A private identifier is reusable in distinct lexical namespaces. Its
    // opaque leaf identity must not erase the enclosing entity placement from
    // the name passed to the selected model (or private-symbol serializer).
    const auto separator = qualified_name.rfind("::");
    auto result = separator == std::string_view::npos ? std::string{}
        : std::string(qualified_name.substr(0, separator + 2));
    result += fresh_identifier_link_stem(*fresh);
    return result;
}

std::string resolved_link_name(const FunctionDecl& function,
                               const CompilerOptions& options) {
    if (const auto exact = decode_attribute_string(function.attribute("link_name"));
        !exact.empty()) return exact;
    const auto model_name = model_entity_name(function.name, function.fresh.get());
    if (function.linkage == Linkage::Global ||
        (function.linkage != Linkage::Static && !function.definition())) {
        std::vector<ManglingParameter> parameters;
        parameters.reserve(function.parameters.size());
        for (const auto& parameter : function.parameters) {
            parameters.push_back(
                {.spelling = canonical_type_name(
                     callable_parameter_type(parameter.type, parameter.mode)),
                 .mode = std::string(parameter_mode_name(parameter.mode))});
        }
        return encode_model_link_name(
            {.qualified_name = model_name,
             .kind = "function",
             .result = canonical_type_name(callable_result_type(function.return_type)),
             .parameters = parameters,
             .variadic = function.variadic},
            false, options.mangling);
    }
    if (function.linkage == Linkage::Static) {
        return "__cross_static_" + std::to_string(stable_hash(function.source_unit)) + '_' +
               sanitize(model_name);
    }
    return "__cross_group_" + sanitize(model_name);
}

const Attribute* object_attribute(const ObjectDecl& object, std::string_view name) {
    for (const auto& attribute : object.attributes) {
        if (attribute.name == name) return &attribute;
    }
    return nullptr;
}

std::string resolved_link_name(const ObjectDecl& object,
                               const CompilerOptions& options) {
    if (const auto exact = decode_attribute_string(object_attribute(object, "link_name"));
        !exact.empty()) return exact;
    const auto model_name = model_entity_name(object.name, object.fresh.get());
    if (object.linkage == Linkage::Global ||
        (object.linkage != Linkage::Static && !object.initializer)) {
        return encode_model_link_name(
            {.qualified_name = model_name,
             .kind = "object",
             .result = canonical_type_name(object.type),
             .parameters = {},
             .variadic = false},
            false, options.mangling);
    }
    if (object.linkage == Linkage::Static) {
        return "__cross_static_" + std::to_string(stable_hash(object.source_unit)) + '_' +
               sanitize(model_name);
    }
    return "__cross_group_" + sanitize(model_name);
}

std::vector<std::string> decoded_clobbers(const FunctionDecl& function) {
    std::vector<std::string> result;
    for (const auto& attribute : function.attributes) {
        if (attribute.name != "clobber") continue;
        for (const auto& argument : attribute.arguments) {
            if (const auto value = decode_string_literal(argument)) result.push_back(*value);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

TypePtr variadic_state_type(std::string_view spelling) {
    unsigned pointers{};
    while (spelling.ends_with('*')) {
        ++pointers;
        spelling.remove_suffix(1);
    }
    const auto kind = builtin_kind(spelling);
    if (!kind) return {};
    auto result = builtin_type(*kind);
    while (pointers-- != 0) result = pointer_type(result);
    return result;
}

std::string source_namespace(std::string_view name) {
    const auto separator = name.rfind("::");
    return separator == std::string_view::npos
               ? std::string{}
               : std::string(name.substr(0, separator));
}

enum class LayoutViewCoverage { PendingAllowed, Complete, Closed };

// Resolves each by-value record of the graph once per call, so a changed
// provider, owner or completeness is always observed. Closed stops at a
// complete record: an extended view completed its members with it.
bool layout_view_matches_records(const Module& module, const Program& program,
                                 const TypePtr& source, LayoutViewCoverage coverage) {
    if (module.address_bits != program.address_bits) return false;
    // Holding each definition keeps the member types on the work list alive.
    std::vector<std::shared_ptr<const RecordDecl>> definitions;
    std::vector<const cross::Type*> work{source.get()};
    std::vector<bool> visited(module.records.size());
    std::unordered_set<NominalTypeKey, NominalTypeKeyHash> uncached;
    while (!work.empty()) {
        const auto* type = work.back();
        work.pop_back();
        if (!type) continue;
        if (type->kind == cross::Type::Kind::Array || type->kind == cross::Type::Kind::Vector) {
            work.push_back(type->element.get());
        } else if (type->kind == cross::Type::Kind::Record) {
            const auto key = type->nominal_key();
            if (key.identity && key.identity->function_scope &&
                (!module.evaluation_layout_scope ||
                 module.evaluation_layout_scope != program.evaluation_layout_scope)) return false;
            const auto* cached = module.record(key);
            if (cached ? visited[cached->id.value] : !uncached.insert(key).second) continue;
            if (cached) visited[cached->id.value] = true;
            auto definition = program.record_definition(key);
            if (!definition) return false;
            if (coverage != LayoutViewCoverage::PendingAllowed && (!cached || !cached->complete)) return false;
            if (cached && cached->complete && cached->definition != definition.get()) return false;
            if (coverage == LayoutViewCoverage::Closed) continue;
            for (const auto& member : definition->members) work.push_back(member.type.get());
            definitions.push_back(std::move(definition));
        }
        // The layout of a pointer does not depend on its pointee's shape.
    }
    return true;
}

class Builder {
public:
    Builder(Program& program, const CompilerOptions& options,
            const TargetInfo& target, Diagnostics& diagnostics,
            Module seed = {}, RecordSourceProofs* proofs = nullptr)
        : program_(program), options_(options), target_(target), diagnostics_(diagnostics),
          record_source_proofs_(proofs ? proofs : &own_record_source_proofs_) {
        if (seed.evaluation_layout_scope && seed.evaluation_layout_scope == program_.evaluation_layout_scope) {
            module_ = std::move(seed);
            seeded_records_ = module_.records.size();
            return;
        }
        module_.evaluation_layout_scope = program_.evaluation_layout_scope;
        const auto* abi = find_abi(target_, options_.abi, options_.target);
        if (abi) module_.default_abi = abi->id;
        // Resolve through the registry: a name shared by several address
        // models denotes the entry of this target triple.
        const auto add_abi_name = [&](const std::string& name) {
            if (const auto* entry = find_abi(target_, name, options_.target))
                module_.abi_names.emplace(name, entry->id);
        };
        for (const auto& entry : model_registry().abis()) {
            if (entry.architecture != target_.architecture) continue;
            add_abi_name(entry.canonical_name);
            for (const auto& alias : entry.aliases) add_abi_name(alias);
        }
        module_.abi_names["default"] = module_.default_abi;
        module_.address_bits = abi && abi->address_bits != 0
                                   ? abi->address_bits
                                   : 64U;
        for (unsigned kind = static_cast<unsigned>(BuiltinType::Void);
             kind <= static_cast<unsigned>(BuiltinType::Label); ++kind) {
            (void)intern_type(builtin_type(static_cast<BuiltinType>(kind)));
        }
    }

    ContinuationTask<Module> run_async() {
        validate_address_spaces();
        collect_record_shells();
        co_await finish_records_async();
        if (resource_failed()) co_return std::move(module_);
        collect_functions();
        if (resource_failed()) co_return std::move(module_);
        collect_objects();
        if (resource_failed()) co_return std::move(module_);
        co_await finish_functions_async();
        if (resource_failed()) co_return std::move(module_);
        finish_global_labels();
        co_await finish_objects_async();
        if (resource_failed()) co_return std::move(module_);
        finish_symbol_indirections();
        co_await finish_patch_sink_indices_async();
        if (resource_failed()) co_return std::move(module_);
        diagnose_symbol_collisions();
        co_return std::move(module_);
    }

    ContinuationTask<Module> constant_context_async() {
        collect_record_shells();
        co_await finish_records_async();
        if (resource_failed()) co_return std::move(module_);
        collect_functions(true);
        if (resource_failed()) co_return std::move(module_);
        collect_objects(true);
        if (resource_failed()) co_return std::move(module_);
        co_await finish_functions_async();
        if (resource_failed()) co_return std::move(module_);
        co_await finish_objects_async();
        co_return std::move(module_);
    }

    ContinuationTask<Module> record_layout_context_async() {
        collect_record_shells();
        co_await finish_records_async();
        co_return std::move(module_);
    }

    ContinuationTask<Module> required_layout_context_async(TypePtr type, EvaluationLayoutKind kind) {
        // Interning creates the shells of the records this query reaches;
        // layout resolves their definitions.
        required_layout_query_ = true;
        // Nested source probes replace the dynamically scoped callback. Keep
        // this callable alive until its continuation has fully returned.
        const auto prepare = program_.evaluation_prepare_layout;
        if (prepare && !(co_await prepare.async(type, kind)))
            co_return std::move(module_);
        if (resource_failed()) co_return std::move(module_);
        if (kind == EvaluationLayoutKind::Alignment)
            (void)co_await required_alignment_async(type, {});
        else if (co_await resolve_member_bounds_async(type, {}, true))
            (void)co_await storage_layout_async(intern_type(type), {});
        co_return std::move(module_);
    }

    void validate_address_spaces() {
        std::unordered_set<const cross::Type*> visited;
        std::unordered_set<std::string> reported;
        std::vector<const Expr*> retained_bounds;
        const auto type = [&](const auto& self, const TypePtr& source,
                              SourceLocation fallback) -> void {
            if (!source || !visited.insert(source.get()).second) return;
            if (source->array_bound) retained_bounds.push_back(source->array_bound.get());
            if (source->vector_bound) retained_bounds.push_back(source->vector_bound.get());
            for (const auto& request : source->alignment_requests) retained_bounds.push_back(request.get());
            if (source->pending_address_space) {
                diagnostics_.error(source->pending_address_space->second,
                                   "address_space requires a pointer declarator");
            }
            if (source->kind == cross::Type::Kind::Pointer) {
                if (const auto issue = address_space_type_error(target_, source->address_space)) {
                    const auto location =
                        source->address_space_location.valid()
                            ? source->address_space_location
                            : fallback;
                    const auto key =
                        std::to_string(reinterpret_cast<std::uintptr_t>(
                            location.file)) + ':' +
                        std::to_string(location.offset) + ':' +
                        std::to_string(source->address_space);
                    if (reported.insert(key).second) {
                        diagnostics_.error(location, *issue);
                    }
                }
                self(self, source->pointee, fallback);
            } else if (source->kind == cross::Type::Kind::Array ||
                       source->kind == cross::Type::Kind::Vector) {
                self(self, source->element, fallback);
            } else if (source->kind == cross::Type::Kind::Function &&
                       source->function) {
                self(self, source->function->result, fallback);
                for (const auto& parameter : source->function->parameters) {
                    self(self, parameter.type, parameter.location);
                    self(self, parameter.declared_array_type, parameter.location);
                }
            }
        };
        const auto expression = [&](const auto& self,
                                    const Expr* source) -> void {
            if (!source) return;
            type(type, source->type, source->location);
            self(self, source->left.get());
            self(self, source->right.get());
            self(self, source->third.get());
            for (const auto& argument : source->arguments)
                self(self, argument.get());
            for (const auto& argument : source->generic_arguments) {
                type(type, argument.type, source->location);
                self(self, argument.value.get());
            }
            for (const auto& entry : source->initializer_entries) {
                for (const auto& designator : entry.designators)
                    self(self, designator.index.get());
                self(self, entry.value.get());
            }
        };
        const auto attributes = [&](const std::vector<Attribute>& values) {
            for (const auto& attribute : values) {
                expression(expression,
                           attribute.expression_argument.get());
                for (const auto& binding : attribute.variadic_bindings)
                    type(type, binding.type, attribute.location);
            }
        };
        const auto statement = [&](const auto& self,
                                   const Statement* source) -> void {
            if (!source) return;
            attributes(source->attributes);
            if (source->declaration) {
                type(type, source->declaration->type,
                     source->declaration->location);
                attributes(source->declaration->attributes);
                expression(expression,
                           source->declaration->dynamic_array_bound.get());
                expression(expression,
                           source->declaration->initializer.get());
            }
            expression(expression, source->expression.get());
            expression(expression, source->condition.get());
            expression(expression, source->increment.get());
            self(self, source->first.get());
            self(self, source->second.get());
            for (const auto& child : source->statements)
                self(self, child.get());
        };
        for (const auto& record : program_.records) {
            attributes(record.attributes);
            for (const auto& member : record.members) {
                type(type, member.type, member.location);
                attributes(member.attributes);
                expression(expression, member.bit_width.get());
            }
        }
        for (const auto& enumeration : program_.enumerations) {
            attributes(enumeration.attributes);
            for (const auto& item : enumeration.enumerators)
                expression(expression, item.initializer.get());
        }
        for (const auto& assertion : program_.static_assertions)
            expression(expression, assertion.condition.get());
        for (const auto& label : program_.global_labels)
            attributes(label.attributes);
        for (const auto& function : program_.functions) {
            type(type, function->return_type, function->location);
            for (const auto& requirement : function->required_types) {
                type(type, requirement.type, requirement.location);
                type(type, requirement.compatible_with, requirement.location);
            }
            for (const auto& parameter : function->parameters) {
                type(type, parameter.type, parameter.location);
                type(type, parameter.declared_array_type, parameter.location);
            }
            for (const auto& parameter : function->generic_parameters)
                type(type, parameter.value_type, function->location);
            attributes(function->attributes);
            statement(statement, function->body.get());
        }
        for (const auto& object : program_.objects) {
            type(type, object->type, object->location);
            attributes(object->attributes);
            expression(expression, object->initializer.get());
        }
        for (const auto& requirement : program_.required_types) {
            type(type, requirement.type, requirement.location);
            type(type, requirement.compatible_with, requirement.location);
        }
        while (!retained_bounds.empty()) {
            const auto* bound = retained_bounds.back();
            retained_bounds.pop_back();
            expression(expression, bound);
        }
    }

private:
    // Object-producing required values must not rebuild the same record graph
    // through member/initializer services independently of their sizeof view.
    // A nominal key alone does not identify invocation-private prepared layout.
    bool share_layout_type(const TypePtr& source) const {
        return layout_view_matches_records(module_, program_, source, LayoutViewCoverage::PendingAllowed);
    }

    struct LayoutServiceScope {
        Program& program;
        decltype(Program::evaluation_member_layout) member_layout;
        decltype(Program::evaluation_initializer_plan) initializer_plan;
        decltype(Program::evaluation_initializer_types) initializer_types;

        LayoutServiceScope(Builder& builder, SourceLocation location)
            : program(builder.program_), member_layout(std::move(program.evaluation_member_layout)),
              initializer_plan(std::move(program.evaluation_initializer_plan)),
              initializer_types(std::move(program.evaluation_initializer_types)) {
            program.evaluation_member_layout = [&builder, previous = member_layout, location](
                const TypePtr& owner, const MemberName& name)
                -> ContinuationTask<std::optional<EvaluationMemberLayout>> {
                if (builder.resource_failed()) co_return {};
                if (!builder.share_layout_type(owner)) {
                    if (previous) co_return co_await previous.async(owner, name);
                    co_return {};
                }
                const auto id = builder.intern_type(owner);
                if (!(co_await builder.storage_layout_async(id, location)).first) co_return {};
                if (!builder.share_layout_type(owner)) {
                    if (previous) co_return co_await previous.async(owner, name);
                    co_return {};
                }
                const auto& type = builder.module_.type(id);
                if (!type.record) co_return {};
                const auto* selected = builder.module_.member(*type.record, name);
                if (!selected) co_return {};
                co_return EvaluationMemberLayout{selected->offset, selected->alignment,
                    selected->bit_width, selected->bit_offset};
            };
            program.evaluation_initializer_plan = [&builder, previous = initializer_plan, location](
                const Expr& expression, const TypePtr& destination)
                -> ContinuationTask<EvaluationInitializerPlan> {
                if (!builder.resource_failed()) {
                    if (!builder.share_layout_type(destination)) {
                        if (previous) co_return co_await previous.async(expression, destination);
                    } else {
                        const bool dynamic = destination && destination->kind == cross::Type::Kind::Array &&
                            !destination->lanes;
                        const auto& layout_type = dynamic ? destination->element : destination;
                        const auto id = builder.intern_type(layout_type);
                        const auto extent = co_await builder.storage_layout_async(id, location);
                        if (extent.first && builder.share_layout_type(destination))
                            co_return initializer::build_for_evaluation(expression, destination,
                                builder.program_, builder.module_, builder.target_);
                        if (!builder.resource_failed() && previous)
                            co_return co_await previous.async(expression, destination);
                    }
                }
                co_return EvaluationInitializerPlan{.items = {}, .valid = false,
                    .error_location = expression.location,
                    .error_message = "target layout is unavailable for this initializer"};
            };
            program.evaluation_initializer_types = [&builder](const Expr& expression,
                const TypePtr& destination, std::span<const Expr* const> deferred)
                -> ContinuationTask<EvaluationInitializerTypePlan> {
                // Type selection copies the registry and uses current source
                // shapes; it neither requires nor mutates physical layout.
                co_return initializer::types_for_evaluation(expression, destination,
                    builder.program_, builder.module_, builder.target_, deferred);
            };
        }
        ~LayoutServiceScope() {
            program.evaluation_member_layout = std::move(member_layout);
            program.evaluation_initializer_plan = std::move(initializer_plan);
            program.evaluation_initializer_types = std::move(initializer_types);
        }
        LayoutServiceScope(const LayoutServiceScope&) = delete;
        LayoutServiceScope& operator=(const LayoutServiceScope&) = delete;
    };

    TypeId intern_type(const TypePtr& source) {
        return module_.intern_type(source);
    }

    static bool power_of_two(unsigned value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    static std::optional<std::uint64_t> align_up(std::uint64_t value,
                                                  unsigned alignment) {
        if (!power_of_two(alignment)) return std::nullopt;
        const auto mask = static_cast<std::uint64_t>(alignment - 1);
        if (value > std::numeric_limits<std::uint64_t>::max() - mask) {
            return std::nullopt;
        }
        return (value + mask) & ~mask;
    }

    ContinuationTask<void> finish_patch_sink_designator_async(Expr& expression,
                                      std::string_view source_namespace) {
        std::vector<Expr*> indices;
        auto* selected = &expression;
        while (!resource_failed()) {
            if (selected->kind == Expr::Kind::Parenthesized && selected->left) {
                selected = selected->left.get();
                continue;
            }
            if (selected->kind != Expr::Kind::Binary || !selected->left) break;
            if (selected->text == "index" && selected->right &&
                !selected->right->evaluated_integer) indices.push_back(selected);
            selected = selected->left.get();
        }
        // The innermost selection is proved first. Do not start an outer proof
        // after a new resource failure in an inner index/layout dependency.
        for (auto index = indices.rbegin(); index != indices.rend(); ++index) {
            if (resource_failed()) co_return;
            auto& selection = **index;
            selection.right->evaluated_integer = co_await with_record_layout_async(
                selection.location, [&](const LayoutQuery& size_of, const LayoutQuery& align_of)
                    -> ContinuationTask<std::optional<Expr::IntegerConstant>> {
                    co_return co_await evaluate_target_integer_constant_async(
                        program_, *selection.right, diagnostics_, size_of, align_of,
                        source_namespace);
                });
        }
    }

    ContinuationTask<void> finish_patch_sink_expression_async(Expr& expression,
                                     std::string_view source_namespace) {
        if (resource_failed()) co_return;
        if (expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name &&
            expression.left->text == "$::patch" &&
            expression.arguments.size() == 2) {
            co_await finish_patch_sink_designator_async(*expression.arguments[1],
                                         source_namespace);
        }
        if (expression.left) {
            co_await finish_patch_sink_expression_async(*expression.left,
                                         source_namespace);
        }
        if (expression.right) {
            co_await finish_patch_sink_expression_async(*expression.right,
                                         source_namespace);
        }
        if (expression.third) {
            co_await finish_patch_sink_expression_async(*expression.third,
                                         source_namespace);
        }
        for (auto& argument : expression.arguments) {
            co_await finish_patch_sink_expression_async(*argument, source_namespace);
        }
        for (auto& argument : expression.generic_arguments) {
            if (argument.value) {
                co_await finish_patch_sink_expression_async(*argument.value,
                                             source_namespace);
            }
        }
        for (auto& entry : expression.initializer_entries) {
            for (auto& designator : entry.designators) {
                if (designator.index) {
                    co_await finish_patch_sink_expression_async(*designator.index,
                                                 source_namespace);
                }
            }
            if (entry.value) {
                co_await finish_patch_sink_expression_async(*entry.value,
                                             source_namespace);
            }
        }
    }

    ContinuationTask<void> finish_patch_sink_statement_async(Statement& statement,
                                    std::string_view source_namespace) {
        if (resource_failed()) co_return;
        for (auto& child : statement.statements) {
            co_await finish_patch_sink_statement_async(*child, source_namespace);
        }
        if (statement.declaration) {
            if (statement.declaration->dynamic_array_bound) {
                co_await finish_patch_sink_expression_async(
                    *statement.declaration->dynamic_array_bound,
                    source_namespace);
            }
            if (statement.declaration->initializer) {
                co_await finish_patch_sink_expression_async(
                    *statement.declaration->initializer, source_namespace);
            }
        }
        if (statement.expression) {
            co_await finish_patch_sink_expression_async(*statement.expression,
                                         source_namespace);
        }
        if (statement.condition) {
            co_await finish_patch_sink_expression_async(*statement.condition,
                                         source_namespace);
        }
        if (statement.increment) {
            co_await finish_patch_sink_expression_async(*statement.increment,
                                         source_namespace);
        }
        if (statement.first) {
            co_await finish_patch_sink_statement_async(*statement.first, source_namespace);
        }
        if (statement.second) {
            co_await finish_patch_sink_statement_async(*statement.second, source_namespace);
        }
    }

    ContinuationTask<void> finish_patch_sink_indices_async() {
        for (std::size_t index = 0; index < program_.functions.size(); ++index) {
            if (resource_failed()) co_return;
            const auto* function = program_.functions[index].get();
            const auto name_space = function->source_namespace;
            if (function->body) {
                co_await finish_patch_sink_statement_async(*function->body, name_space);
            }
        }
    }

    ContinuationTask<unsigned> parse_alignment_async(const std::vector<Attribute>& attributes,
                             std::string_view subject, std::string_view source_namespace) {
        unsigned result = 1;
        for (const auto& attribute : attributes) {
            if (resource_failed()) co_return result;
            if (attribute.name != "aligned") continue;
            const auto queries = record_layout_queries(attribute.location);
            LayoutServiceScope services(*this, attribute.location);
            const auto value = co_await evaluate_alignment_attribute_async(
                program_, attribute, diagnostics_, queries.first, queries.second, subject, source_namespace);
            if (value) result = std::max(result, *value);
        }
        co_return result;
    }

    void parse_symbol_attributes(
        const std::vector<const Attribute*>& attributes,
        bool has_definition, Linkage linkage, SourceLocation location,
        bool& weak, SymbolVisibility& visibility) {
        bool has_visibility{};
        for (const auto* attribute : attributes) {
            if (attribute->name == "weak") {
                weak = true;
                if (!attribute->arguments.empty()) {
                    diagnostics_.error(attribute->location,
                                       "weak does not take arguments");
                }
                continue;
            }
            if (attribute->name != "visibility") continue;
            if (attribute->arguments.size() != 1) {
                diagnostics_.error(
                    attribute->location,
                    "visibility requires one visibility-kind string");
                continue;
            }
            const auto spelling =
                decode_string_literal(attribute->arguments.front());
            if (!spelling) {
                diagnostics_.error(
                    attribute->location,
                    "visibility requires one visibility-kind string");
                continue;
            }
            const auto parsed = *spelling == "default"
                ? SymbolVisibility::Default
                : *spelling == "hidden"
                      ? SymbolVisibility::Hidden
                      : *spelling == "protected"
                            ? SymbolVisibility::Protected
                            : *spelling == "internal"
                                  ? SymbolVisibility::Internal
                                  : SymbolVisibility::Default;
            if (*spelling != "default" && *spelling != "hidden" &&
                *spelling != "protected" && *spelling != "internal") {
                diagnostics_.error(
                    attribute->location,
                    "visibility must be default, hidden, protected, or internal");
                continue;
            }
            if (has_visibility && visibility != parsed) {
                diagnostics_.error(attribute->location,
                                   "conflicting visibility attributes");
            } else {
                visibility = parsed;
                has_visibility = true;
            }
        }
        if (weak && !has_definition) {
            diagnostics_.error(location,
                               "weak requires an external definition");
        }
        if (weak && linkage != Linkage::Global) {
            diagnostics_.error(location,
                               "weak requires global linkage");
        }
        if (has_visibility && linkage != Linkage::Global) {
            diagnostics_.error(location,
                               "visibility requires global linkage");
        }
    }

    void parse_symbol_indirection_attributes(
        const std::vector<const Attribute*>& attributes,
        std::optional<std::string>& alias_target,
        std::optional<std::string>& weakref_target) {
        for (const auto* attribute : attributes) {
            auto* destination = attribute->name == "alias"
                ? &alias_target
                : attribute->name == "weakref" ? &weakref_target : nullptr;
            if (!destination) continue;
            if (attribute->arguments.size() != 1) {
                diagnostics_.error(
                    attribute->location,
                    attribute->name + " requires one link-name string");
                continue;
            }
            const auto spelling =
                decode_string_literal(attribute->arguments.front());
            if (!spelling || spelling->empty()) {
                diagnostics_.error(
                    attribute->location,
                    attribute->name + " requires one nonempty link-name string");
                continue;
            }
            if (*destination && **destination != *spelling) {
                diagnostics_.error(attribute->location,
                                   "conflicting " + attribute->name +
                                       " attributes");
            } else {
                *destination = *spelling;
            }
        }
    }

    bool parse_packed(const std::vector<Attribute>& attributes,
                      std::string_view subject, bool definition = false) {
        bool result = false;
        for (const auto& attribute : attributes) {
            if (attribute.name == "packed") {
                result = true;
                if (!attribute.arguments.empty()) {
                    diagnostics_.error(
                        attribute.location,
                        "packed on " + std::string(subject) +
                            " does not take arguments");
                }
            } else if (attribute.name != "aligned" &&
                       !(definition && attribute.name == "may_alias")) {
                diagnostics_.error(
                    attribute.location,
                    "attribute '" + attribute.name + "' is not valid on " +
                        std::string(subject));
            }
        }
        return result;
    }

    void collect_record_shells() {
        for (const auto& declaration : program_.records) {
            if (declaration.nominal_identity && declaration.nominal_identity->generic_owner) continue;
            auto found = module_.record_ids.find(declaration.nominal_key());
            RecordId id;
            if (found == module_.record_ids.end()) {
                id = {static_cast<std::uint32_t>(module_.records.size())};
                module_.record_ids.emplace(declaration.nominal_key(), id);
                Record record;
                record.id = id;
                record.source_key = declaration.nominal_key();
                record.location = declaration.location;
                record.source_name = declaration.name;
                record.is_union = declaration.is_union;
                module_.records.push_back(std::move(record));
            } else {
                id = found->second;
                if (module_.record(id).is_union != declaration.is_union) {
                    diagnostics_.error(
                        declaration.location,
                        "record '" + declaration.name +
                            "' was redeclared with the other record kind");
                }
            }
            auto& record = module_.record(id);
            record.declarations.push_back(&declaration);
            if (declaration.complete) {
                if (record.definition) {
                    diagnostics_.error(
                        declaration.location,
                        "duplicate definition of record '" +
                            declaration.name + "'");
                } else {
                    record.definition = &declaration;
                }
            } else if (!declaration.attributes.empty()) {
                for (const auto& attribute : declaration.attributes) {
                    diagnostics_.error(
                        attribute.location,
                        "layout attribute '" + attribute.name +
                            "' requires a complete record definition");
                }
            }
        }
    }

    ContinuationTask<std::pair<std::uint64_t, unsigned>> storage_layout_async(TypeId id,
                                                       SourceLocation location) {
        if (resource_failed()) co_return {0, 1};
        const auto type = module_.type(id);
        // Complete the nominal and extent dependencies first; the module's
        // layout functions then give the storage itself.
        if (type.kind == Type::Kind::Record) {
            if (!type.record || !(co_await layout_record_async(*type.record, location))) {
                co_return {0, 1};
            }
        } else if (type.kind == Type::Kind::Array || type.kind == Type::Kind::Vector) {
            const bool array = type.kind == Type::Kind::Array;
            if (!type.element || type.lanes == 0 || (!array && type.scalable)) {
                diagnostics_.error(location, array
                    ? "record member cannot have variable-length or incomplete array type"
                    : "record member cannot have scalable or incomplete vector type");
                co_return {0, 1};
            }
            const auto element = co_await storage_layout_async(*type.element, location);
            const auto fallback = array ? element.second : 1U;
            if (resource_failed()) co_return {0, fallback};
            if (element.first == 0 ||
                type.lanes > std::numeric_limits<std::uint64_t>::max() / element.first) {
                diagnostics_.error(location, array
                    ? "record member array size overflows target storage"
                    : "record member vector size overflows target storage");
                co_return {0, fallback};
            }
        } else if (type.kind != Type::Kind::Pointer &&
                   (type.kind != Type::Kind::Builtin || type.builtin == BuiltinType::Void)) {
            diagnostics_.error(location,
                               "record member has an incomplete or non-object type");
            co_return {0, 1};
        }
        co_return {layout_size(module_, id, target_).value_or(0),
                   static_cast<unsigned>(layout_alignment(module_, id, target_).value_or(1))};
    }

    ContinuationTask<std::optional<unsigned>> required_alignment_async(TypePtr source, SourceLocation location) {
        if (resource_failed()) co_return std::nullopt;
        if (!source) co_return {};
        const auto prepare_type = program_.evaluation_prepare_type;
        if (prepare_type &&
            !(co_await prepare_type.async(source, EvaluationLayoutKind::Alignment))) co_return {};
        if (!(co_await resolve_alignment_async(source, {}))) co_return {};
        // Array counts and pointee layouts do not determine alignment.
        if (source->kind == cross::Type::Kind::Array) {
            const auto element = co_await required_alignment_async(source->element, location);
            if (!element) co_return {};
            co_return std::max(*element, source->alignment);
        }
        const auto id = intern_type(source);
        if (source->kind != cross::Type::Kind::Record) {
            if (source->kind == cross::Type::Kind::Vector && !(co_await resolve_member_bounds_async(source, {}))) co_return {};
            const auto value = layout_alignment(module_, intern_type(source), target_);
            co_return value ? std::optional<unsigned>{static_cast<unsigned>(*value)} : std::nullopt;
        }
        const auto record_id = *module_.type(id).record;
        adopt_seed_record(record_id);
        if (module_.record(record_id).alignment_complete)
            co_return std::max(module_.record(record_id).alignment, source->alignment);
        const auto key = source->nominal_key();
        if (!alignment_active_.insert(key).second) {
            diagnostics_.error(location, "record alignment depends on itself");
            co_return {};
        }
        struct Pop {
            std::unordered_set<NominalTypeKey, NominalTypeKeyHash>& active;
            NominalTypeKey key;
            ~Pop() { active.erase(key); }
        } pop{alignment_active_, key};
        const auto definition = program_.record_definition(key);
        if (!definition) {
            diagnostics_.error(location, "incomplete record type cannot provide alignment");
            co_return {};
        }
        if (const auto error = record_source_error(*definition, program_, program_.record_index,
                record_source_proofs_)) {
            diagnostics_.error(error->location, error->message);
            co_return {};
        }
        const auto errors = diagnostics_.errors();
        const bool packed = parse_packed(definition->attributes, "a record definition", true);
        const auto name_space = source_namespace(definition->name);
        unsigned alignment = (co_await parse_alignment_async(definition->attributes, "a record definition", name_space));
        for (const auto& member : definition->members) {
            if (resource_failed()) co_return std::nullopt;
            const bool member_packed = parse_packed(member.attributes, "a record member");
            const auto requested = (co_await parse_alignment_async(member.attributes, "a record member", name_space));
            const auto natural = (co_await required_alignment_async(member.type, member.location));
            if (!natural) co_return {};
            // This is the same placement-alignment rule as full layout. Width,
            // including a zero-width field, does not change that rule.
            const auto kept = packed || member_packed
                ? std::max(1U, hir::requested_alignment(module_, intern_type(member.type))) : *natural;
            alignment = std::max(alignment, std::max(requested, kept));
        }
        if (resource_failed() || diagnostics_.errors() != errors) co_return {};
        auto& record = module_.record(record_id);
        record.definition = definition.get();
        record.retained_definition = definition;
        record.alignment = alignment;
        record.alignment_complete = true;
        co_return std::max(alignment, source->alignment);
    }

    // Typedef alignment requests use the same required-constant evaluator as
    // every other `aligned` placement.
    ContinuationTask<bool> resolve_alignment_async(TypePtr type, std::string_view name_space) {
        if (resource_failed()) co_return false;
        if (type->alignment_requests.empty()) co_return true;
        co_return co_await with_record_layout_async(type->alignment_requests.front()->location,
            [&](const LayoutQuery& size_of, const LayoutQuery& align_of) -> ContinuationTask<bool> {
                co_return co_await resolve_alignment_requests_async(program_, type, diagnostics_,
                    size_of, align_of, name_space);
            });
    }

    ContinuationTask<bool> set_bit_field_width_async(RecordMember& member,
                             const Expr::IntegerConstant& width,
                             SourceLocation location) {
        const auto storage_bits = (co_await storage_layout_async(member.type, location)).first * 8U;
        if (resource_failed()) co_return false;
        if (const auto* error = bit_field_width_error(width, module_.address_bits,
                                                     storage_bits, !member.name.empty())) {
            diagnostics_.error(location, error);
            co_return false;
        }
        member.bit_width = static_cast<unsigned>(width.value.low);
        co_return true;
    }

    std::pair<LayoutQuery, LayoutQuery> record_layout_queries(SourceLocation location) {
        LayoutQuery size_of = [this, location](const TypePtr& source)
            -> ContinuationTask<std::optional<std::uint64_t>> {
            const auto result = co_await storage_layout_async(intern_type(source), location);
            if (!result.first) co_return std::nullopt;
            co_return result.first;
        };
        LayoutQuery align_of = [this, location](const TypePtr& source)
            -> ContinuationTask<std::optional<std::uint64_t>> {
            const auto result = co_await required_alignment_async(source, location);
            if (!result) co_return std::nullopt;
            co_return *result;
        };
        return {std::move(size_of), std::move(align_of)};
    }

    template<class Evaluate>
    auto with_record_layout_async(SourceLocation location, Evaluate evaluate)
        -> std::invoke_result_t<Evaluate, const LayoutQuery&, const LayoutQuery&> {
        const auto queries = record_layout_queries(location);
        LayoutServiceScope services(*this, location);
        co_return co_await evaluate(queries.first, queries.second);
    }

    ContinuationTask<bool> resolve_bit_field_width_async(RecordMember& member,
                                 std::string_view source_namespace) {
        if (resource_failed()) co_return false;
        if (!member.pending_bit_width) co_return member.bit_width.has_value();
        const auto* expression = member.pending_bit_width;
        member.pending_bit_width = nullptr;
        const auto evaluated = co_await with_record_layout_async(expression->location,
            [&](const LayoutQuery& size_of, const LayoutQuery& align_of) -> ContinuationTask<std::optional<Expr::IntegerConstant>> {
                co_return co_await evaluate_target_integer_constant_async(program_, *expression, diagnostics_,
                    size_of, align_of, source_namespace);
            });
        if (!evaluated) co_return false;
        co_return co_await set_bit_field_width_async(member, *evaluated, member.location);
    }

    ContinuationTask<bool> resolve_member_bounds_async(TypePtr type, std::string_view name_space, bool layout_only = false) {
        if (resource_failed()) co_return false;
        if (!type) co_return true;
        if (layout_only && (type->kind == cross::Type::Kind::Pointer || type->kind == cross::Type::Kind::Function)) co_return true;
        bool valid = (co_await resolve_member_bounds_async(type->element, name_space, layout_only));
        valid = (co_await resolve_member_bounds_async(type->pointee, name_space, layout_only)) && valid;
        if (type->function) {
            valid = (co_await resolve_member_bounds_async(type->function->result, name_space)) && valid;
            for (const auto& parameter : type->function->parameters) {
                valid = (co_await resolve_member_bounds_async(parameter.type, name_space)) && valid;
                valid = (co_await resolve_member_bounds_async(parameter.declared_array_type, name_space)) && valid;
            }
        }
        if (type->array_bound && type->lanes == 0) {
            const auto expression = type->array_bound;
            const auto bound = co_await with_record_layout_async(expression->location,
                [&](const LayoutQuery& size_of, const LayoutQuery& align_of) -> ContinuationTask<std::optional<std::uint32_t>> {
                    co_return co_await evaluate_fixed_array_bound_async(program_, *expression, diagnostics_,
                        size_of, align_of, name_space);
                });
            if (!bound) co_return false;
            type->lanes = *bound;
        }
        if (type->vector_bound && type->lanes == 0) {
            const auto expression = type->vector_bound;
            valid = (co_await with_record_layout_async(expression->location,
                [&](const LayoutQuery& size_of, const LayoutQuery& align_of) -> ContinuationTask<bool> {
                    co_return co_await resolve_vector_bound_async(program_, type, diagnostics_, size_of, align_of, name_space);
                })) && valid;
        }
        valid = (co_await resolve_alignment_async(type, name_space)) && valid;
        co_return valid;
    }

    // The first use of a seed record keeps its facts while its definition owner
    // is unchanged, and makes it a shell again otherwise. Within one scope
    // identity and one record table a nominal key's definition is fixed once its
    // record is prepared (record views are never replaced), so the owners of a
    // kept record's members are not resolved again.
    void adopt_seed_record(RecordId id) {
        if (id.value >= seeded_records_) return;
        if (id.value >= layout_state_.size()) layout_state_.resize(module_.records.size());
        auto& record = module_.record(id);
        if (layout_state_[id.value] != 0 || (!record.complete && !record.alignment_complete)) return;
        if (program_.record_definition(record.source_key).get() == record.definition) {
            if (record.complete) layout_state_[id.value] = 2;
            return;
        }
        Record shell;
        shell.id = id;
        shell.source_key = std::move(record.source_key);
        shell.source_name = std::move(record.source_name);
        shell.is_union = record.is_union;
        record = std::move(shell);
    }

    ContinuationTask<bool> layout_record_async(RecordId id, SourceLocation use_location) {
        if (resource_failed()) co_return false;
        if (id.value >= layout_state_.size()) {
            layout_state_.resize(module_.records.size());
        }
        adopt_seed_record(id);
        if (layout_state_[id.value] == 2) co_return true;
        if (layout_state_[id.value] == 3) co_return false;
        if (layout_state_[id.value] == 1) {
            diagnostics_.error(
                use_location,
                "record contains itself by value through a member cycle");
            co_return false;
        }
        const auto key = module_.record(id).source_key;
        auto type = record_type(key.name, module_.record(id).is_union);
        type->nominal_identity = key.identity;
        const auto prepare_type = program_.evaluation_prepare_type;
        if (prepare_type &&
            !(co_await prepare_type.async(type, EvaluationLayoutKind::Complete))) co_return false;
        // Preparation can publish generic definitions and relocate the source
        // vector. Refresh this pointer from its typed nominal key, not spelling.
        auto definition = program_.record_definition(key);
        module_.record(id).definition = definition.get();
        module_.record(id).retained_definition = std::move(definition);
        // A required layout expression may intern an implicit pointer tag and
        // grow the record table. Work on a detached record, then publish by ID;
        // neither the record nor its member references may dangle across queries.
        auto record = module_.record(id);
        if (!record.definition) {
            diagnostics_.error(
                use_location,
                "incomplete record type '" + record.source_name +
                    "' cannot be used as an object or member");
            co_return false;
        }
        if (const auto error = record_source_error(*record.definition, program_, program_.record_index,
                record_source_proofs_)) {
            diagnostics_.error(error->location, error->message);
            layout_state_[id.value] = 3;
            co_return false;
        }
        layout_state_[id.value] = 1;
        const auto errors = diagnostics_.errors();
        co_await prepare_record_async(record);
        if (resource_failed()) { layout_state_[id.value] = 3; co_return false; }
        std::uint64_t extent{};
        unsigned record_alignment = 1;
        bool valid = diagnostics_.errors() == errors;
        struct ActiveBitFieldUnit {
            TypeId type;
            std::uint64_t offset{};
            unsigned bits{};
            unsigned used{};
            unsigned alignment{1};
        };
        std::optional<ActiveBitFieldUnit> active_bit_field;
        // GNU x86-64 layout tracks the first unallocated bit separately from
        // the containing integer unit used for access and ABI transport.
        const bool next_bit_placement =
            target_.data_layout.bit_field_placement ==
            BitFieldPlacement::NextAvailableBit;
        std::uint64_t next_bit{};
        const auto current_namespace = source_namespace(record.source_name);
        for (auto& member : record.members) {
            if (resource_failed()) { layout_state_[id.value] = 3; co_return false; }
            if (member.pending_source_type) {
                if (!(co_await resolve_member_bounds_async(member.pending_source_type, current_namespace, required_layout_query_))) {
                    valid = false;
                    member.pending_source_type.reset();
                    continue;
                }
                member.type = intern_type(member.pending_source_type);
                member.pending_source_type.reset();
                validate_atomic_type(member.type, member.location);
            }
            if (member.pending_bit_width &&
                !(co_await resolve_bit_field_width_async(member, current_namespace))) {
                valid = false;
            }
            const auto [size, natural_alignment] =
                (co_await storage_layout_async(member.type, member.location));
            if (resource_failed()) { layout_state_[id.value] = 3; co_return false; }
            if (size == 0) valid = false;
            const auto requested_alignment = member.alignment;
            const auto placement_alignment =
                std::max(member.alignment,
                         (record.packed || member.packed)
                             ? std::max(1U, hir::requested_alignment(module_, member.type))
                             : natural_alignment);
            member.alignment = placement_alignment;
            record_alignment = std::max(record_alignment,
                                        placement_alignment);
            if (member.bit_width) {
                const auto unit_bits = size <=
                                               std::numeric_limits<unsigned>::max() /
                                                   8U
                                           ? static_cast<unsigned>(size * 8U)
                                           : 0U;
                const auto width = *member.bit_width;
                if (width == 0) {
                    active_bit_field.reset();
                    if (record.is_union) {
                        member.offset = 0;
                    } else if (const auto offset =
                                   align_up(extent, placement_alignment)) {
                        member.offset = *offset;
                        extent = *offset;
                        if (next_bit_placement) {
                            if (extent >
                                std::numeric_limits<std::uint64_t>::max() / 8U) {
                                diagnostics_.error(
                                    member.location,
                                    "record layout overflows target storage");
                                valid = false;
                            } else {
                                next_bit = extent * 8U;
                            }
                        }
                    } else {
                        diagnostics_.error(
                            member.location,
                            "record layout overflows target storage");
                        valid = false;
                    }
                    continue;
                }
                if (unit_bits == 0 || width > unit_bits) {
                    valid = false;
                    continue;
                }
                if (record.is_union) {
                    member.offset = 0;
                    member.bit_offset =
                        target_.data_layout.bit_field_order ==
                                BitFieldOrder::LeastSignificantFirst
                            ? 0U
                            : unit_bits - width;
                    extent = std::max(extent, size);
                    continue;
                }
                if (next_bit_placement && !record.packed && !member.packed) {
                    active_bit_field.reset();
                    auto start = next_bit;
                    if (requested_alignment > 1) {
                        if (requested_alignment >
                            std::numeric_limits<unsigned>::max() / 8U) {
                            diagnostics_.error(
                                member.location,
                                "record layout overflows target storage");
                            valid = false;
                            continue;
                        }
                        const auto aligned = align_up(
                            start, requested_alignment * 8U);
                        if (!aligned) {
                            diagnostics_.error(
                                member.location,
                                "record layout overflows target storage");
                            valid = false;
                            continue;
                        }
                        start = *aligned;
                    }
                    const auto unit_start =
                        (start / unit_bits) * unit_bits;
                    if (width > unit_bits - (start - unit_start)) {
                        if (unit_start >
                            std::numeric_limits<std::uint64_t>::max() -
                                unit_bits) {
                            diagnostics_.error(
                                member.location,
                                "record layout overflows target storage");
                            valid = false;
                            continue;
                        }
                        start = unit_start + unit_bits;
                    }
                    if (start >
                        std::numeric_limits<std::uint64_t>::max() - width) {
                        diagnostics_.error(
                            member.location,
                            "record layout overflows target storage");
                        valid = false;
                        continue;
                    }
                    const auto storage_start =
                        (start / unit_bits) * unit_bits;
                    member.offset = storage_start / 8U;
                    const auto within_unit =
                        static_cast<unsigned>(start - storage_start);
                    member.bit_offset =
                        target_.data_layout.bit_field_order ==
                                BitFieldOrder::LeastSignificantFirst
                            ? within_unit
                            : unit_bits - within_unit - width;
                    next_bit = start + width;
                    extent = next_bit / 8U +
                             static_cast<std::uint64_t>(next_bit % 8U != 0);
                    continue;
                }
                const auto unqualified = module_.unqualified(member.type);
                const bool shares =
                    active_bit_field &&
                    active_bit_field->alignment == placement_alignment &&
                    (target_.data_layout.bit_field_unit_sharing ==
                             BitFieldUnitSharing::SameStorageSize
                         ? active_bit_field->bits == unit_bits
                         : active_bit_field->type == unqualified) &&
                    width <= active_bit_field->bits -
                                 active_bit_field->used;
                if (!shares) {
                    const auto offset = align_up(extent, placement_alignment);
                    if (!offset || *offset >
                                       std::numeric_limits<std::uint64_t>::max() -
                                           size) {
                        diagnostics_.error(
                            member.location,
                            "record layout overflows target storage");
                        valid = false;
                        active_bit_field.reset();
                        continue;
                    }
                    active_bit_field = ActiveBitFieldUnit{
                        unqualified, *offset, unit_bits, 0,
                        placement_alignment};
                    extent = *offset + size;
                    if (next_bit_placement) {
                        if (extent >
                            std::numeric_limits<std::uint64_t>::max() / 8U) {
                            diagnostics_.error(
                                member.location,
                                "record layout overflows target storage");
                            valid = false;
                        } else {
                            next_bit = extent * 8U;
                        }
                    }
                }
                member.offset = active_bit_field->offset;
                member.bit_offset =
                    target_.data_layout.bit_field_order ==
                            BitFieldOrder::LeastSignificantFirst
                        ? active_bit_field->used
                        : active_bit_field->bits -
                              active_bit_field->used - width;
                active_bit_field->used += width;
                if (active_bit_field->used == active_bit_field->bits) {
                    active_bit_field.reset();
                }
                continue;
            }
            active_bit_field.reset();
            if (record.is_union) {
                member.offset = 0;
                extent = std::max(extent, size);
                continue;
            }
            const auto offset = align_up(extent, placement_alignment);
            if (!offset || *offset >
                               std::numeric_limits<std::uint64_t>::max() -
                                   size) {
                diagnostics_.error(member.location,
                                   "record layout overflows target storage");
                valid = false;
                continue;
            }
            member.offset = *offset;
            extent = *offset + size;
            if (next_bit_placement) {
                if (extent >
                    std::numeric_limits<std::uint64_t>::max() / 8U) {
                    diagnostics_.error(
                        member.location,
                        "record layout overflows target storage");
                    valid = false;
                } else {
                    next_bit = extent * 8U;
                }
            }
        }
        record_alignment = std::max(record_alignment,
                                    record.explicit_alignment);
        const auto rounded = align_up(extent, record_alignment);
        if (!rounded) {
            diagnostics_.error(record.location,
                               "record size overflows target storage");
            valid = false;
        }
        record.alignment = record_alignment;
        record.size = rounded.value_or(0);
        module_.record(id) = std::move(record);
        layout_state_[id.value] = valid ? 2 : 3;
        co_return valid;
    }

    ContinuationTask<void> prepare_record_async(Record& record) {
        if (resource_failed()) co_return;
        const auto* definition = record.definition;
        const auto attributes = definition->attributes;
        // Member buffers and expression allocations survive RecordDecl vector
        // growth. Copy the descriptor handles before any nested required query.
        std::vector<const RecordMemberDecl*> members;
        for (const auto& member : definition->members) members.push_back(&member);
        record.location = definition->location;
        record.complete = true;
        record.packed = parse_packed(attributes,
                                     "a record definition", true);
        record.explicit_alignment =
            co_await parse_alignment_async(attributes,
                            "a record definition",
                            source_namespace(record.source_name));
        for (const auto* descriptor : members) {
            if (resource_failed()) co_return;
            const auto& source = *descriptor;
            RecordMember member;
            member.location = source.location;
            member.name = source.name;
            member.fresh = source.fresh;
            member.type = intern_type(source.type);
            member.pending_source_type = source.type;
            if (source.bit_width) {
                const auto* width = source.bit_width.get();
                while (width &&
                       width->kind == Expr::Kind::Parenthesized &&
                       width->left) {
                    width = width->left.get();
                }
                if (!width) {
                    diagnostics_.error(
                        source.location,
                        "bit-field width must be a nonnegative integer constant expression");
                } else if (width->evaluated_integer) {
                    (void)co_await set_bit_field_width_async(
                        member, *width->evaluated_integer,
                        source.location);
                } else {
                    member.pending_bit_width = width;
                }
            }
            member.packed = parse_packed(source.attributes,
                                         "a record member");
            member.alignment = co_await parse_alignment_async(source.attributes,
                                               "a record member",
                                               source_namespace(record.source_name));
            record.members.push_back(std::move(member));
        }
        co_return;
    }

    ContinuationTask<void> finish_records_async() {
        for (std::size_t index = 0; index < module_.records.size(); ++index) {
            if (resource_failed()) co_return;
            const auto& key = module_.records[index].source_key;
            if (key.identity && std::find(program_.translation_only_record_scopes.begin(),
                    program_.translation_only_record_scopes.end(), key.identity->function_scope) !=
                    program_.translation_only_record_scopes.end()) continue;
            if (module_.records[index].definition) {
                (void)co_await layout_record_async(
                    RecordId{static_cast<std::uint32_t>(index)},
                    module_.records[index].location);
            }
        }
    }

    void validate_atomic_type(TypeId id, SourceLocation location,
                              bool pending_outer_bound = false) {
        const auto& type = module_.type(id);
        if (type.kind == Type::Kind::Function) {
            if (!type.function || !type.function->abi.valid()) {
                diagnostics_.error(
                    location, "function-pointer type has no registered ABI");
                return;
            }
            const auto* abi = find_abi(target_, type.function->abi);
            if (!abi || abi->address_bits != module_.address_bits ||
                !abi->function_selectable) {
                diagnostics_.error(location,
                                   "function-pointer ABI is incompatible "
                                   "with the compilation data model");
            }
            if (type.is_const || type.is_volatile || type.is_atomic) {
                diagnostics_.error(
                    location, "function type cannot have object qualifiers");
            }
            if (type.function->stack_cleanup &&
                *type.function->stack_cleanup != "caller" &&
                *type.function->stack_cleanup != "callee") {
                diagnostics_.error(location,
                                   "stack_cleanup requires 'caller' or 'callee'");
            }
            for (const auto& resource : type.function->clobbers) {
                const auto instruction_resource = std::any_of(
                    target_.instructions.begin(), target_.instructions.end(),
                    [&](const InstructionEntry& instruction) {
                        const auto contains = [&](const auto& resources) {
                            return std::find(resources.begin(), resources.end(),
                                             resource) != resources.end();
                        };
                        return contains(instruction.implicit_reads) ||
                               contains(instruction.implicit_writes);
                    });
                if (resource != "memory" && resource != "flags" &&
                    !find_register(target_, resource) &&
                    !instruction_resource) {
                    diagnostics_.error(location,
                                       "unknown target resource in callable clobber: '" +
                                           resource + "'");
                }
            }
            for (const auto& parameter : type.function->parameters) {
                validate_atomic_type(parameter.type, parameter.location);
            }
            validate_atomic_type(type.function->result_type, location);
            return;
        }
        if (type.kind == Type::Kind::Array) {
            if (!type.element || (type.lanes == 0 && !pending_outer_bound && !required_layout_query_)) {
                diagnostics_.error(location,
                                   "array type requires a positive fixed bound");
            } else {
                const auto& element = module_.type(*type.element);
                if (element.kind == Type::Kind::Builtin &&
                    element.builtin == BuiltinType::Void) {
                    diagnostics_.error(location,
                                       "array element type cannot be void");
                }
                if (element.kind == Type::Kind::Vector &&
                    element.scalable) {
                    diagnostics_.error(
                        location,
                        "array element type cannot be a scalable vector");
                }
            }
        }
        if (type.is_atomic &&
            (type.kind == Type::Kind::Vector || type.kind == Type::Kind::Array ||
             type.kind == Type::Kind::Record ||
             (type.kind == Type::Kind::Builtin &&
              (type.builtin == BuiltinType::Void ||
               type.builtin == BuiltinType::Label)))) {
            diagnostics_.error(
                location,
                "atomic qualifier requires an integer, floating, or pointer object type");
        }
        if ((type.kind == Type::Kind::Vector ||
             type.kind == Type::Kind::Array) && type.element) {
            if (module_.type(*type.element).is_atomic) {
                diagnostics_.error(
                    location,
                    type.kind == Type::Kind::Vector
                        ? "a vector element type cannot be atomic-qualified"
                        : "an array element type cannot be atomic-qualified");
            }
            validate_atomic_type(*type.element, location);
        } else if (type.kind == Type::Kind::Pointer && type.pointee) {
            validate_atomic_type(*type.pointee, location);
        }
    }

    static bool automatic(const std::optional<std::string>& location) {
        return !location || *location == "auto";
    }

    bool complete_object_type(TypeId id, SourceLocation location) {
        const auto& type = module_.type(id);
        if (type.kind == Type::Kind::Record) {
            if (!type.record || !module_.record(*type.record).complete) {
                diagnostics_.error(
                    location,
                    "object has incomplete type '" + type_name(module_, id) +
                        "'");
                return false;
            }
            if (module_.record(*type.record).size == 0) {
                diagnostics_.error(
                    location,
                    "object type '" + type_name(module_, id) +
                        "' has no valid storage layout");
                return false;
            }
            return true;
        }
        if (type.kind == Type::Kind::Array && type.element) {
            return complete_object_type(*type.element, location);
        }
        return true;
    }

    bool compatible(const Function& canonical, const FunctionDecl& declaration) {
        if (module_.without_alignment(canonical.result_type) !=
                module_.without_alignment(intern_type(declaration.return_type)) ||
            canonical.parameters.size() != declaration.parameters.size() ||
            canonical.variadic != declaration.variadic) return false;
        const auto same_location = [](const std::optional<std::string>& left,
                                      const std::optional<std::string>& right) {
            const auto is_auto = [](const std::optional<std::string>& value) {
                return !value || *value == "auto";
            };
            return (is_auto(left) && is_auto(right)) || left == right;
        };
        if (!same_location(canonical.result_location, declaration.result_location)) return false;
        if (!canonical.declarations.empty()) {
            const auto& previous = *canonical.declarations.front();
            const auto previous_naked = previous.attribute("naked") != nullptr;
            const auto declaration_naked = declaration.attribute("naked") != nullptr;
            // An alias and its canonical name select the same registered entry.
            const auto previous_abi = decode_attribute_string(previous.attribute("abi"));
            const auto declaration_abi = decode_attribute_string(declaration.attribute("abi"));
            const auto previous_entry = module_.abi_names.find(previous_abi);
            const auto declaration_entry = module_.abi_names.find(declaration_abi);
            const bool same_abi = previous_abi == declaration_abi ||
                (previous_entry != module_.abi_names.end() &&
                 declaration_entry != module_.abi_names.end() &&
                 previous_entry->second == declaration_entry->second);
            if (previous_naked != declaration_naked || !same_abi ||
                decode_attribute_string(previous.attribute("stack_cleanup")) !=
                    decode_attribute_string(declaration.attribute("stack_cleanup")) ||
                decoded_clobbers(previous) != decoded_clobbers(declaration)) return false;
        }
        for (std::size_t index = 0; index < canonical.parameters.size(); ++index) {
            const auto& left = canonical.parameters[index];
            const auto& right = declaration.parameters[index];
            const auto* previous = canonical.declarations.front();
            const auto& previous_parameter = previous->parameters[index];
            if (left.mode != right.mode ||
                !same_type(callable_parameter_type(previous_parameter.type, left.mode),
                           callable_parameter_type(right.type, right.mode)) ||
                !same_location(left.physical_location, right.location_name)) return false;
        }
        return true;
    }

    Function make_function(const FunctionDecl& declaration, bool validate_layout = true) {
        Function function;
        function.id = {static_cast<std::uint32_t>(module_.functions.size())};
        function.location = declaration.location;
        function.source_name = declaration.name;
        function.source_unit = declaration.source_unit;
        function.linkage = declaration.linkage;
        function.result_type = intern_type(declaration.return_type);
        if (validate_layout) validate_atomic_type(function.result_type, declaration.location);
        if (module_.type(function.result_type).is_atomic) {
            diagnostics_.error(declaration.location,
                               "a function result cannot be atomic-qualified");
        }
        function.result_location = declaration.result_location;
        function.variadic = declaration.variadic;
        for (const auto& parameter : declaration.parameters) {
            const auto type = intern_type(parameter.type);
            if (validate_layout) validate_atomic_type(type, parameter.location);
            function.parameters.push_back({parameter.location, parameter.name,
                                           type, parameter.mode,
                                           parameter.location_name, parameter.binding});
        }
        return function;
    }

    void collect_functions(bool skip_templates = false) {
        for (const auto& source : program_.functions) {
            if (resource_failed()) return;
            if (skip_templates && !source->generic_parameters.empty()) continue;
            const auto key = entity_key(source->linkage, source->source_unit, source->name);
            const auto found = function_keys_.find(key);
            FunctionId id;
            if (found == function_keys_.end()) {
                id = {static_cast<std::uint32_t>(module_.functions.size())};
                function_keys_.emplace(key, id);
                // A constant-address context retains helper names/signatures
                // for source lookup, not runtime layout or transport. Its
                // translation-only extents may still require an invocation;
                // the source validator checks their constraints before erasure.
                const bool translation_only = source->attribute("eval_only") || source->has_meta_signature();
                module_.functions.push_back(make_function(*source, !skip_templates || !translation_only));
            } else {
                id = found->second;
                auto& canonical = module_.function(id);
                if (!compatible(canonical, *source)) {
                    diagnostics_.error(source->location,
                                       "incompatible redeclaration of function '" +
                                           source->name + "'");
                }
            }
            auto& canonical = module_.function(id);
            canonical.declarations.push_back(source.get());
            module_.function_ids.emplace(source.get(), id);
            if (source->definition()) {
                if (canonical.definition) {
                    diagnostics_.error(source->location,
                                       "duplicate definition of function '" + source->name + "'");
                } else {
                    canonical.definition = source.get();
                }
            }
        }
    }

    ContinuationTask<void> finish_functions_async() {
        for (auto& function : module_.functions) {
            if (resource_failed()) co_return;
            for (const auto* declaration : function.declarations) {
                if (declaration->definition()) continue;
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name == "aligned") {
                        diagnostics_.error(attribute.location,
                                           "aligned requires a function definition");
                    }
                }
            }
            const auto* representative = function.definition
                                             ? function.definition
                                             : function.declarations.back();
            function.location = representative->location;
            function.source_unit = representative->source_unit;
            function.linkage = representative->linkage;
            function.mergeable = function.linkage == Linkage::Global &&
                                 function.definition &&
                                 function.definition->generic_instance;
            function.link_symbol = resolved_link_name(*representative, options_);
            if (function.link_symbol.empty()) {
                diagnostics_.error(
                    representative->location,
                    "selected mangling model produced an empty function "
                    "link name");
            }
            function.result_type = intern_type(representative->return_type);
            function.result_location = representative->result_location;
            function.parameters.clear();
            for (const auto& parameter : representative->parameters) {
                function.parameters.push_back({parameter.location, parameter.name,
                                               intern_type(parameter.type), parameter.mode,
                                               parameter.location_name, parameter.binding});
            }

            const auto abi_text = decode_attribute_string(representative->attribute("abi"));
            const auto selected = abi_text.empty() ? options_.abi : abi_text;
            function.abi_explicit = !abi_text.empty();
            function.abi_contract =
                !function.variadic && !function.abi_explicit && function.definition &&
                        function.linkage != Linkage::Global
                    ? AbiContract::Dynamic
                    : AbiContract::Registered;
            const auto* selected_abi =
                find_abi(target_, selected, options_.target);
            if (selected_abi) {
                const auto* abi = selected_abi;
                function.abi = abi->id;
                if (!abi_text.empty() && !abi->function_selectable) {
                    diagnostics_.error(
                        representative->location,
                        "ABI model '" + abi->canonical_name +
                            "' is not function-selectable");
                }
            } else {
                diagnostics_.error(
                    representative->location,
                    "unknown ABI model '" + selected + "' for target '" +
                        options_.target + "'");
            }

            std::size_t variadic_attributes{};
            const Attribute* variadic_attribute{};
            for (const auto* declaration : function.declarations) {
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name != "variadic") continue;
                    ++variadic_attributes;
                    if (!variadic_attribute) variadic_attribute = &attribute;
                    if (!declaration->definition()) {
                        diagnostics_.error(
                            attribute.location,
                            "variadic state bindings are allowed only on a definition");
                    }
                }
            }
            if (function.variadic) {
                if (function.parameters.empty()) {
                    diagnostics_.error(
                        representative->location,
                        "a variadic function requires at least one named parameter");
                }
                for (const auto& parameter : function.parameters) {
                    if (parameter.mode != ParameterMode::In) {
                        diagnostics_.error(
                            parameter.location,
                            "every fixed parameter of a variadic function must use 'in'");
                    }
                    if (parameter.physical_location &&
                        *parameter.physical_location != "auto") {
                        diagnostics_.error(
                            parameter.location,
                            "bootstrap variadic lowering requires automatic fixed-parameter locations");
                    }
                }
                if (!selected_abi || !selected_abi->variadic_supported) {
                    diagnostics_.error(
                        representative->location,
                        "ABI model '" + selected +
                            "' does not define variadic lowering");
                }
                if (variadic_attributes > 1) {
                    diagnostics_.error(
                        representative->location,
                        "a variadic definition accepts at most one variadic attribute");
                }
                if (variadic_attributes != 0 && selected_abi &&
                    variadic_attribute) {
                    const auto* attribute = variadic_attribute;
                    NameSet names;
                    std::unordered_set<std::string> states;
                    for (const auto& declaration : attribute->variadic_bindings) {
                        const auto state = std::find_if(
                            selected_abi->variadic_states.begin(),
                            selected_abi->variadic_states.end(),
                            [&](const AbiVariadicState& candidate) {
                                return candidate.canonical_name == declaration.state;
                            });
                        if (state == selected_abi->variadic_states.end()) {
                            diagnostics_.error(
                                declaration.location,
                                "unknown variadic ABI state '" + declaration.state + "'");
                            continue;
                        }
                        const auto type = variadic_state_type(state->type);
                        if (!type || !same_type(type, declaration.type)) {
                            diagnostics_.error(
                                declaration.location,
                                "variadic state '" + declaration.state +
                                    "' requires binding type '" + state->type + "'");
                            continue;
                        }
                        VariadicBinding binding{declaration.location, declaration.name,
                            intern_type(declaration.type), state->id, declaration.binding};
                        if (!names.insert(name_key(binding)).second ||
                            !states.insert(declaration.state).second) {
                            diagnostics_.error(
                                declaration.location,
                                "duplicate variadic state binding");
                            continue;
                        }
                        function.variadic_bindings.push_back(std::move(binding));
                    }
                }
            } else if (variadic_attributes != 0) {
                diagnostics_.error(
                    representative->location,
                    "variadic attribute requires a variadic function definition");
            }
            if (const auto section = decode_attribute_string(
                    representative->attribute("section")); !section.empty()) {
                function.section = section;
            }
            if (function.definition) {
                function.minimum_alignment = co_await parse_alignment_async(
                    function.definition->attributes, "function definition",
                    function.definition->source_namespace);
            }
            std::vector<const Attribute*> symbol_attributes;
            std::vector<const Attribute*> indirection_attributes;
            for (const auto* declaration : function.declarations) {
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name == "weak" ||
                        attribute.name == "visibility") {
                        symbol_attributes.push_back(&attribute);
                    }
                    if (attribute.name == "alias" ||
                        attribute.name == "weakref") {
                        indirection_attributes.push_back(&attribute);
                    }
                }
            }
            parse_symbol_indirection_attributes(
                indirection_attributes, function.alias_target,
                function.weakref_target);
            parse_symbol_attributes(
                symbol_attributes,
                function.definition != nullptr || function.alias_target ||
                    function.weakref_target,
                function.linkage, representative->location, function.weak,
                function.visibility);
            bool hot{};
            bool cold{};
            for (const auto& attribute : representative->attributes) {
                const auto marker = [&](std::string_view name) {
                    if (attribute.name != name) return false;
                    if (!attribute.arguments.empty()) {
                        diagnostics_.error(
                            attribute.location,
                            std::string(name) + " does not take arguments");
                    }
                    return true;
                };
                hot = marker("hot") || hot;
                cold = marker("cold") || cold;
                function.used = marker("used") || function.used;
                function.retain = marker("retain") || function.retain;
                function.no_stack_protector =
                    marker("no_stack_protector") ||
                    function.no_stack_protector;
                if (attribute.name == "no_sanitize") {
                    if (attribute.arguments.size() != 1) {
                        diagnostics_.error(
                            attribute.location,
                            "no_sanitize requires one instrumentation-name string");
                    } else if (const auto name = decode_string_literal(
                                   attribute.arguments.front());
                               !name || name->empty()) {
                        diagnostics_.error(
                            attribute.location,
                            "no_sanitize requires one nonempty instrumentation-name string");
                    } else {
                        function.no_sanitize.push_back(*name);
                    }
                }
            }
            if (hot && cold) {
                diagnostics_.error(representative->location,
                                   "a function cannot be both hot and cold");
            } else if (hot) {
                function.temperature = FunctionTemperature::Hot;
            } else if (cold) {
                function.temperature = FunctionTemperature::Cold;
            }
            if ((function.used || function.retain) && !function.definition &&
                !function.alias_target) {
                diagnostics_.error(
                    representative->location,
                    std::string(function.retain ? "retain" : "used") +
                        " requires a function definition");
            }
            function.naked = representative->attribute("naked") != nullptr;
            for (const auto& attribute : representative->attributes) {
                if (attribute.name == "naked" && !attribute.arguments.empty()) {
                    diagnostics_.error(attribute.location, "naked does not take arguments");
                }
                if (attribute.name != "clobber") continue;
                for (const auto& argument : attribute.arguments) {
                    const auto value = decode_string_literal(argument);
                    if (!value) {
                        diagnostics_.error(attribute.location,
                                           "clobber arguments must be string literals");
                    } else {
                        function.clobbers.push_back(*value);
                        if (*value != "memory" && *value != "flags") {
                            const auto* entry = find_register(target_, *value);
                            const auto instruction_resource = std::any_of(
                                target_.instructions.begin(),
                                target_.instructions.end(),
                                [&](const InstructionEntry& instruction) {
                                    const auto contains = [&](const auto& resources) {
                                        return std::find(resources.begin(),
                                                         resources.end(), *value) !=
                                               resources.end();
                                    };
                                    return contains(instruction.implicit_reads) ||
                                           contains(instruction.implicit_writes);
                                });
                            if (!entry && !instruction_resource) {
                                diagnostics_.error(
                                    attribute.location,
                                    "unknown target resource in clobber: '" +
                                        *value + "'");
                            } else if (entry && entry->compiler_owned) {
                                diagnostics_.error(
                                    attribute.location,
                                    "stack/frame pointer cannot be a raw-inline "
                                    "scratch clobber");
                            }
                        }
                    }
                }
            }
            // Without an ABI base, nothing else states an external
            // interface's clobbers.
            if (!function.definition && !function.alias_target &&
                fully_custom(function)) {
                for (const auto* declaration : function.declarations) {
                    if (!declaration->attribute("clobber")) {
                        diagnostics_.error(
                            declaration->location,
                            "a fully custom unresolved declaration requires "
                            "a clobber attribute; clobber() declares no extra "
                            "clobbers");
                    }
                }
            }
            if (!function.definition) {
                function.ownership = BodyOwnership::None;
            } else if (function.naked) {
                function.ownership = BodyOwnership::RawMir;
                validate_naked_interface(function);
            } else {
                function.ownership = BodyOwnership::ManagedAst;
            }
            if (function.definition && function.definition->body) {
                collect_labels(function, *function.definition->body);
            }
        }
    }

    void collect_labels(Function& function, const Statement& statement) {
        if (statement.kind == Statement::Kind::Label) {
            const auto label_location = statement.label_location.valid()
                ? statement.label_location : statement.location;
            NameLookupContext context;
            context.label_binding = statement.label_binding;
            NameUse name(statement.label_name, label_location);
            name.context = &context;
            const bool public_collision = std::any_of(function.labels.begin(), function.labels.end(),
                [&](LabelId id) {
                    const auto& existing = module_.labels.at(id.value);
                    return (statement.global_label || existing.is_global) &&
                           existing.source_name == statement.label_name;
                });
            if (public_collision || module_.label(function.id, name)) {
                diagnostics_.error(statement.location,
                                   "duplicate label '" + statement.label_name + "'");
            } else {
                const LabelId id{static_cast<std::uint32_t>(module_.labels.size())};
                const auto qualified =
                    function.source_name + "::" + statement.label_name;
                std::string symbol;
                if (statement.global_label) {
                    const auto* owner = function.definition
                        ? function.definition : function.declarations.back();
                    symbol = resolved_label_link_name(
                        qualified, statement.attributes, statement.location,
                        owner->fresh.get(), statement.label_fresh.get());
                    if (function.linkage != Linkage::Global ||
                        function.abi_contract != AbiContract::Registered) {
                        diagnostics_.error(
                            statement.location,
                            "a global label definition requires a global stable-ABI function");
                    }
                    if (function.definition &&
                        function.definition->attribute("always_inline")) {
                        diagnostics_.error(
                            statement.location,
                            "a function containing a global label cannot be always_inline");
                    }
                } else if (!statement.attributes.empty()) {
                    diagnostics_.error(statement.location,
                                       "attributes are not valid on a local label");
                }
                module_.labels.push_back(
                    {id, function.id, statement.location,
                     statement.label_name, qualified, std::move(symbol), {},
                     &statement, statement.global_label,
                     NameKey{statement.label_name, label_location}, statement.label_binding});
                function.labels.push_back(id);
            }
        }
        for (const auto& child : statement.statements) collect_labels(function, *child);
        if (statement.first) collect_labels(function, *statement.first);
        if (statement.second) collect_labels(function, *statement.second);
    }

    std::string resolved_label_link_name(
        std::string_view qualified_name,
        const std::vector<Attribute>& attributes,
        SourceLocation location,
        const FreshIdentifier* owner_fresh = nullptr,
        const FreshIdentifier* label_fresh = nullptr) {
        const Attribute* link_name{};
        for (const auto& attribute : attributes) {
            if (attribute.name != "link_name") {
                diagnostics_.error(
                    attribute.location,
                    "attribute '" + attribute.name +
                        "' is not valid on a global label");
                continue;
            }
            if (link_name) {
                diagnostics_.error(attribute.location,
                                   "global label has more than one link_name attribute");
                continue;
            }
            link_name = &attribute;
        }
        std::string result;
        if (link_name) {
            result = decode_attribute_string(link_name);
            if (link_name->arguments.size() != 1 || result.empty()) {
                diagnostics_.error(
                    link_name->location,
                    "global-label link_name requires one nonempty string literal");
            }
        } else {
            std::string model_name(qualified_name);
            if (owner_fresh || label_fresh) {
                const auto split = qualified_name.rfind("::");
                if (split != std::string_view::npos) {
                    model_name = model_entity_name(qualified_name.substr(0, split), owner_fresh);
                    model_name += "::";
                    model_name += label_fresh
                        ? fresh_identifier_link_stem(*label_fresh)
                        : std::string(qualified_name.substr(split + 2));
                }
            }
            result = encode_model_link_name(model_name, true,
                                            options_.mangling);
        }
        if (result.empty()) {
            diagnostics_.error(
                location,
                "selected mangling model produced an empty global-label link name");
        }
        return result;
    }

    Function* find_global_label_function(std::string_view name,
                                         SourceLocation location) {
        Function* result{};
        for (auto& function : module_.functions) {
            if (function.source_name != name) continue;
            if (result) {
                diagnostics_.error(
                    location,
                    "global label owner '" + std::string(name) +
                        "' is ambiguous");
                return nullptr;
            }
            result = &function;
        }
        if (!result) {
            diagnostics_.error(
                location,
                "global label refers to unknown function '" +
                    std::string(name) + "'");
        }
        return result;
    }

    void finish_global_labels() {
        for (const auto& declaration : program_.global_labels) {
            const auto split = declaration.qualified_name.rfind("::");
            if (split == std::string::npos) {
                diagnostics_.error(
                    declaration.location,
                    "a global label declaration requires a qualified label name");
                continue;
            }
            const auto function_name =
                std::string_view(declaration.qualified_name).substr(0, split);
            auto* function =
                find_global_label_function(function_name, declaration.location);
            if (!function) continue;
            if (function->linkage != Linkage::Global ||
                function->abi_contract != AbiContract::Registered) {
                diagnostics_.error(
                    declaration.location,
                    "a global label declaration requires a global stable-ABI function");
            }
            const auto symbol = resolved_label_link_name(
                declaration.qualified_name, declaration.attributes,
                declaration.location, declaration.owner_fresh.get(),
                declaration.label_fresh.get());
            auto found = std::find_if(
                module_.labels.begin(), module_.labels.end(),
                [&](const Label& label) {
                    return label.owner == function->id &&
                           label.qualified_name == declaration.qualified_name;
                });
            if (found != module_.labels.end()) {
                if (!found->is_global) {
                    diagnostics_.error(
                        declaration.location,
                        "global label declaration disagrees with a local label definition");
                } else if (found->link_symbol != symbol) {
                    diagnostics_.error(
                        declaration.location,
                        "global label declarations use different link names for '" +
                            declaration.qualified_name + "'");
                }
                found->declarations.push_back(&declaration);
                continue;
            }
            if (function->definition) {
                diagnostics_.error(
                    declaration.location,
                    "global label declaration has no matching definition in function '" +
                        function->source_name + "'");
                continue;
            }
            const LabelId id{
                static_cast<std::uint32_t>(module_.labels.size())};
            module_.labels.push_back(
                {id, function->id, declaration.location,
                 declaration.qualified_name.substr(split + 2),
                 declaration.qualified_name, symbol, {&declaration}, nullptr,
                 true, NameKey{declaration.qualified_name.substr(split + 2)}, {}});
        }
    }

    // Every endpoint is manual and no abi attribute supplies a base.
    bool fully_custom(const Function& function) const {
        const auto& result = module_.type(function.result_type);
        const bool is_void = result.kind == Type::Kind::Builtin &&
                             result.builtin == BuiltinType::Void;
        const auto manual = [](const Parameter& parameter) {
            return !automatic(parameter.physical_location);
        };
        return !function.abi_explicit && !function.variadic &&
               (is_void || !automatic(function.result_location)) &&
               std::all_of(function.parameters.begin(), function.parameters.end(), manual) &&
               (!automatic(function.result_location) ||
                std::any_of(function.parameters.begin(), function.parameters.end(), manual));
    }

    void validate_naked_interface(const Function& function) {
        if (function.variadic) {
            diagnostics_.error(function.location,
                               "naked function cannot have a variadic interface");
        }
        for (const auto& parameter : function.parameters) {
            if (automatic(parameter.physical_location)) {
                diagnostics_.error(parameter.location,
                                   "naked requires a complete manual ABI; parameter '" +
                                       parameter.name + "' has an automatic endpoint");
            }
        }
        const auto& result = module_.type(function.result_type);
        const bool is_void = result.kind == Type::Kind::Builtin &&
                             result.builtin == BuiltinType::Void;
        if (!is_void && automatic(function.result_location)) {
            diagnostics_.error(function.location,
                               "naked function with a non-void result requires an explicit result location");
        }
    }

    Object make_object(const ObjectDecl& declaration, bool allow_inferred_bounds) {
        Object object;
        object.id = {static_cast<std::uint32_t>(module_.objects.size())};
        object.location = declaration.location;
        object.source_name = declaration.name;
        object.source_unit = declaration.source_unit;
        object.linkage = declaration.linkage;
        object.type = intern_type(declaration.type);
        // A symbolic-address query may precede byte/brace initializer
        // materialization elsewhere in the input. Keep that array's identity
        // and element type without inventing an extent. Layout queries still
        // return unavailable, and final HIR requires the completed bound.
        const bool pending_bound = allow_inferred_bounds && declaration.initializer &&
            declaration.type && declaration.type->kind == cross::Type::Kind::Array &&
            declaration.type->lanes == 0 && !declaration.type->array_bound;
        validate_atomic_type(object.type, declaration.location, pending_bound);
        (void)complete_object_type(object.type, declaration.location);
        return object;
    }

    void collect_objects(bool allow_inferred_bounds = false) {
        for (const auto& source : program_.objects) {
            if (resource_failed()) return;
            const auto key = entity_key(source->linkage, source->source_unit, source->name);
            const auto found = object_keys_.find(key);
            ObjectId id;
            if (found == object_keys_.end()) {
                id = {static_cast<std::uint32_t>(module_.objects.size())};
                object_keys_.emplace(key, id);
                module_.objects.push_back(make_object(*source, allow_inferred_bounds));
            } else {
                id = found->second;
                // A may_alias typedef denotes the same type.
                if (module_.without_may_alias(module_.objects[id.value].type) !=
                    module_.without_may_alias(intern_type(source->type))) {
                    diagnostics_.error(source->location,
                                       "incompatible redeclaration of object '" + source->name + "'");
                }
            }
            auto& canonical = module_.objects[id.value];
            canonical.declarations.push_back(source.get());
            module_.object_ids.emplace(source.get(), id);
            const bool indirection =
                object_attribute(*source, "alias") != nullptr ||
                object_attribute(*source, "weakref") != nullptr;
            if (source->initializer ||
                (source->linkage != Linkage::Group && !indirection)) {
                if (canonical.definition) {
                    diagnostics_.error(source->location,
                                       "duplicate definition of object '" + source->name + "'");
                } else {
                    canonical.definition = source.get();
                }
            }
        }
    }

    ContinuationTask<void> finish_objects_async() {
        for (auto& object : module_.objects) {
            if (resource_failed()) co_return;
            const auto* representative = object.definition
                                             ? object.definition
                                             : object.declarations.back();
            object.location = representative->location;
            object.source_unit = representative->source_unit;
            object.linkage = representative->linkage;
            object.link_symbol = resolved_link_name(*representative, options_);
            if (object.link_symbol.empty()) {
                diagnostics_.error(
                    representative->location,
                    "selected mangling model produced an empty object link "
                    "name");
            }
            if (const auto section = decode_attribute_string(
                    object_attribute(*representative, "section"));
                !section.empty()) {
                object.section = section;
            }
            object.minimum_alignment = co_await parse_alignment_async(
                representative->attributes, "an object definition",
                source_namespace(object.source_name));
            std::vector<const Attribute*> symbol_attributes;
            std::vector<const Attribute*> indirection_attributes;
            for (const auto* declaration : object.declarations) {
                for (const auto& attribute : declaration->attributes) {
                    if (attribute.name == "weak" ||
                        attribute.name == "visibility") {
                        symbol_attributes.push_back(&attribute);
                    }
                    if (attribute.name == "alias" ||
                        attribute.name == "weakref") {
                        indirection_attributes.push_back(&attribute);
                    }
                }
            }
            parse_symbol_indirection_attributes(
                indirection_attributes, object.alias_target,
                object.weakref_target);
            parse_symbol_attributes(
                symbol_attributes,
                object.definition != nullptr || object.alias_target ||
                    object.weakref_target,
                object.linkage, representative->location, object.weak,
                object.visibility);
            object.is_thread_local =
                object_attribute(*representative, "thread_local") != nullptr;
            object.tls_model = decode_attribute_string(
                object_attribute(*representative, "tls_model"));
        }
    }

    static bool alias_compatible(const Function& alias,
                                 const Function& target) {
        if (alias.result_type != target.result_type ||
            alias.parameters.size() != target.parameters.size() ||
            alias.result_location != target.result_location ||
            alias.abi != target.abi || alias.variadic != target.variadic ||
            alias.clobbers != target.clobbers) {
            return false;
        }
        for (std::size_t index = 0; index < alias.parameters.size(); ++index) {
            const auto& left = alias.parameters[index];
            const auto& right = target.parameters[index];
            if (left.type != right.type || left.mode != right.mode ||
                left.physical_location != right.physical_location) {
                return false;
            }
        }
        return true;
    }

    void finish_symbol_indirections() {
        for (auto& function : module_.functions) {
            if (function.alias_target && function.weakref_target) {
                diagnostics_.error(
                    function.location,
                    "alias and weakref cannot be combined on one function");
            }
            if (function.alias_target) {
                if (function.definition) {
                    diagnostics_.error(function.location,
                                       "alias function cannot have a body");
                }
                if (function.linkage != Linkage::Global) {
                    diagnostics_.error(function.location,
                                       "alias requires global linkage");
                }
                const auto target = std::find_if(
                    module_.functions.begin(), module_.functions.end(),
                    [&](const Function& candidate) {
                        return candidate.id != function.id &&
                               candidate.link_symbol == *function.alias_target;
                    });
                if (target == module_.functions.end() || !target->definition) {
                    diagnostics_.error(
                        function.location,
                        "function alias target '" + *function.alias_target +
                            "' is not a definition in this compilation group");
                } else if (!alias_compatible(function, *target)) {
                    diagnostics_.error(
                        function.location,
                        "function alias target '" + *function.alias_target +
                            "' has an incompatible type or ABI");
                } else {
                    target->used = target->used || function.used;
                    target->retain = target->retain || function.retain;
                }
            }
            if (function.weakref_target) {
                if (function.definition) {
                    diagnostics_.error(function.location,
                                       "weakref function cannot have a body");
                }
                if (function.weak) {
                    diagnostics_.error(
                        function.location,
                        "weakref already supplies weak binding and cannot be combined with weak");
                }
                if (function.visibility != SymbolVisibility::Default) {
                    diagnostics_.error(
                        function.location,
                        "weakref cannot have non-default visibility");
                }
                const auto existing_function = std::any_of(
                    module_.functions.begin(), module_.functions.end(),
                    [&](const Function& candidate) {
                        return candidate.id != function.id &&
                               !candidate.weakref_target &&
                               candidate.link_symbol ==
                                   *function.weakref_target;
                    });
                const auto existing_object = std::any_of(
                    module_.objects.begin(), module_.objects.end(),
                    [&](const Object& candidate) {
                        return !candidate.weakref_target &&
                               candidate.link_symbol ==
                                   *function.weakref_target;
                    });
                if (existing_function || existing_object) {
                    diagnostics_.error(
                        function.location,
                        "weakref target '" + *function.weakref_target +
                            "' is already declared in this compilation group");
                }
                function.link_symbol = *function.weakref_target;
            }
        }

        for (auto& object : module_.objects) {
            if (object.alias_target && object.weakref_target) {
                diagnostics_.error(
                    object.location,
                    "alias and weakref cannot be combined on one object");
            }
            if (object.alias_target) {
                if (object.definition) {
                    diagnostics_.error(object.location,
                                       "alias object cannot have an initializer");
                }
                if (object.linkage != Linkage::Global) {
                    diagnostics_.error(object.location,
                                       "alias requires global linkage");
                }
                const auto target = std::find_if(
                    module_.objects.begin(), module_.objects.end(),
                    [&](const Object& candidate) {
                        return candidate.id != object.id &&
                               candidate.link_symbol == *object.alias_target;
                    });
                if (target == module_.objects.end() || !target->definition) {
                    diagnostics_.error(
                        object.location,
                        "object alias target '" + *object.alias_target +
                            "' is not a definition in this compilation group");
                } else if (object.type != target->type ||
                           object.minimum_alignment !=
                               target->minimum_alignment ||
                           object.is_thread_local != target->is_thread_local ||
                           object.tls_model != target->tls_model) {
                    diagnostics_.error(
                        object.location,
                        "object alias target '" + *object.alias_target +
                            "' has an incompatible type, alignment, or storage contract");
                }
            }
            if (object.weakref_target) {
                if (object.definition) {
                    diagnostics_.error(object.location,
                                       "weakref object cannot have an initializer");
                }
                if (object.weak) {
                    diagnostics_.error(
                        object.location,
                        "weakref already supplies weak binding and cannot be combined with weak");
                }
                if (object.visibility != SymbolVisibility::Default) {
                    diagnostics_.error(
                        object.location,
                        "weakref cannot have non-default visibility");
                }
                const auto existing_function = std::any_of(
                    module_.functions.begin(), module_.functions.end(),
                    [&](const Function& candidate) {
                        return !candidate.weakref_target &&
                               candidate.link_symbol ==
                                   *object.weakref_target;
                    });
                const auto existing_object = std::any_of(
                    module_.objects.begin(), module_.objects.end(),
                    [&](const Object& candidate) {
                        return candidate.id != object.id &&
                               !candidate.weakref_target &&
                               candidate.link_symbol ==
                                   *object.weakref_target;
                    });
                if (existing_function || existing_object) {
                    diagnostics_.error(
                        object.location,
                        "weakref target '" + *object.weakref_target +
                            "' is already declared in this compilation group");
                }
                object.link_symbol = *object.weakref_target;
            }
        }
    }

    void diagnose_symbol_collisions() {
        std::unordered_map<std::string, SourceLocation> symbols;
        const auto add = [&](std::string_view symbol, SourceLocation location,
                             std::string_view source_name) {
            const auto [found, inserted] = symbols.emplace(std::string(symbol), location);
            if (!inserted) {
                diagnostics_.error(location, "link symbol '" + std::string(symbol) +
                                                 "' is shared by distinct entities including '" +
                                                 std::string(source_name) + "'");
            }
        };
        for (const auto& function : module_.functions) {
            if (!function.weakref_target) {
                add(function.link_symbol, function.location,
                    function.source_name);
            }
        }
        for (const auto& object : module_.objects) {
            if (!object.weakref_target) {
                add(object.link_symbol, object.location, object.source_name);
            }
        }
        for (const auto& label : module_.labels) {
            if (label.is_global) {
                add(label.link_symbol, label.location, label.qualified_name);
            }
        }
    }

    bool resource_failed() const {
        return program_.evaluation_resource_errors != resource_epoch_;
    }

    Program& program_;
    // A fresh builder ignores older independent failures; an affected traversal
    // stops after a new evaluator failure without parsing diagnostic wording.
    const std::uint64_t resource_epoch_{program_.evaluation_resource_errors};
    const CompilerOptions& options_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    Module module_;
    // Spares nested layout the closure its enclosing layout just validated.
    // An extended view shares the proofs of the views before it.
    RecordSourceProofs own_record_source_proofs_;
    RecordSourceProofs* record_source_proofs_;
    // Records below this ID come from the seed of an extended view.
    std::size_t seeded_records_{};
    std::vector<unsigned char> layout_state_;
    bool required_layout_query_{};
    std::unordered_set<NominalTypeKey, NominalTypeKeyHash> alignment_active_;
    std::unordered_map<std::string, FunctionId> function_keys_;
    std::unordered_map<std::string, ObjectId> object_keys_;
};

// The fields every interning helper below compares, so equal types share a
// bucket; names and qualifiers stay in each helper's own test.
std::size_t type_hash(const Type& type) {
    std::size_t hash = static_cast<std::size_t>(type.kind);
    const auto mix = [&](std::size_t value) { hash = hash * 31 + value; };
    if (type.kind == Type::Kind::Builtin) mix(static_cast<std::size_t>(type.builtin));
    mix(type.pointee ? type.pointee->value + 1 : 0);
    mix(type.element ? type.element->value + 1 : 0);
    mix(type.record ? type.record->value + 1 : 0);
    mix(type.lanes);
    if (type.function) {
        mix(type.function->result_type.value);
        for (const auto& parameter : type.function->parameters) mix(parameter.type.value);
    }
    return hash;
}

// The lowest ID whose type satisfies `same`, which must imply equal hashes.
template<class Same>
std::optional<TypeId> find_type(const Module& module, const Type& candidate, Same same) {
    if (module.indexed_types > module.types.size()) {
        module.type_index.clear();
        module.indexed_types = 0;
    }
    for (; module.indexed_types < module.types.size(); ++module.indexed_types)
        module.type_index[type_hash(module.types[module.indexed_types])].push_back(
            static_cast<std::uint32_t>(module.indexed_types));
    if (const auto found = module.type_index.find(type_hash(candidate)); found != module.type_index.end())
        for (const auto index : found->second)
            if (same(module.types[index])) return TypeId{index};
    return std::nullopt;
}

bool identical(const Type& type, const Type& candidate) {
    return type.kind == candidate.kind && type.builtin == candidate.builtin &&
        type.pointee == candidate.pointee &&
        type.record == candidate.record &&
        type.function == candidate.function &&
        type.element == candidate.element &&
        type.lanes == candidate.lanes &&
        type.scalable == candidate.scalable &&
        type.nominal_key() == candidate.nominal_key() &&
        type.is_const == candidate.is_const &&
        type.is_volatile == candidate.is_volatile &&
        type.is_restrict == candidate.is_restrict &&
        type.is_atomic == candidate.is_atomic &&
        type.may_alias == candidate.may_alias &&
        type.address_space == candidate.address_space &&
        type.alignment == candidate.alignment;
}

} // namespace

const Function* Module::function(const FunctionDecl& declaration) const {
    const auto found = function_ids.find(&declaration);
    return found == function_ids.end() ? nullptr : &functions[found->second.value];
}

std::optional<TypeId> Module::builtin(BuiltinType kind) const {
    Type type;
    type.builtin = kind;
    return find_type(*this, type, [&](const Type& candidate) {
        return candidate.kind == Type::Kind::Builtin && candidate.builtin == kind &&
            !candidate.is_const && !candidate.is_volatile &&
            !candidate.is_atomic && !candidate.is_restrict && !candidate.may_alias &&
            !candidate.alignment;
    });
}

TypeId Module::intern_type(const TypePtr& source) {
    Type candidate;
    if (source) {
        candidate.kind =
            source->kind == cross::Type::Kind::Pointer    ? Type::Kind::Pointer
            : source->kind == cross::Type::Kind::Vector   ? Type::Kind::Vector
            : source->kind == cross::Type::Kind::Array    ? Type::Kind::Array
            : source->kind == cross::Type::Kind::Record   ? Type::Kind::Record
            : source->kind == cross::Type::Kind::Function ? Type::Kind::Function
                                                          : Type::Kind::Builtin;
        candidate.builtin = source->builtin;
        candidate.nominal_name = source->nominal_name;
        candidate.nominal_identity = source->nominal_identity;
        candidate.is_const = source->is_const;
        candidate.is_volatile = source->is_volatile;
        candidate.is_atomic = source->is_atomic;
        candidate.is_restrict = source->is_restrict;
        candidate.may_alias = source->may_alias;
        candidate.address_space = source->address_space;
        candidate.alignment = source->alignment;
        if (source->kind == cross::Type::Kind::Pointer) {
            candidate.pointee = intern_type(source->pointee);
        } else if (source->kind == cross::Type::Kind::Function &&
                   source->function) {
            FunctionSignature signature;
            signature.result_type = intern_type(callable_result_type(source->function->result));
            signature.variadic = source->function->variadic;
            signature.result_location = source->function->result_location;
            signature.clobbers = source->function->clobbers;
            std::sort(signature.clobbers.begin(), signature.clobbers.end());
            signature.stack_cleanup = source->function->stack_cleanup;
            const auto found = abi_names.find(source->function->abi);
            signature.abi = source->function->abi.empty() ? default_abi
                            : found == abi_names.end()    ? AbiId{}
                                                          : found->second;
            for (const auto& parameter : source->function->parameters) {
                signature.parameters.push_back(
                    {parameter.location, parameter.name,
                     intern_type(callable_parameter_type(parameter.type,
                                                         parameter.mode)), parameter.mode,
                     parameter.location_name, parameter.binding});
            }
            candidate.function = std::move(signature);
        } else if (source->kind == cross::Type::Kind::Vector ||
                   source->kind == cross::Type::Kind::Array) {
            candidate.element = intern_type(source->element);
            candidate.lanes = source->lanes;
            candidate.scalable = source->scalable;
        } else if (source->kind == cross::Type::Kind::Record) {
            auto found = record_ids.find(source->nominal_key());
            if (found == record_ids.end()) {
                const RecordId id{static_cast<std::uint32_t>(records.size())};
                record_ids.emplace(source->nominal_key(), id);
                Record record;
                record.id = id;
                record.source_key = source->nominal_key();
                record.source_name = source->nominal_name;
                record.is_union = source->is_union;
                records.push_back(std::move(record));
                candidate.record = id;
            } else {
                candidate.record = found->second;
            }
            // A record defined [[may_alias]] qualifies each use like a may_alias typedef.
            if (const auto* definition = record(*candidate.record).definition)
                candidate.may_alias = candidate.may_alias || definition->attribute("may_alias");
        }
    }
    if (const auto found = find_type(*this, candidate,
            [&](const Type& type) { return identical(type, candidate); }))
        return *found;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return id;
}

TypeId Module::function_type(FunctionSignature signature) {
    for (auto& parameter : signature.parameters) {
        if (parameter.mode == ParameterMode::In)
            parameter.type = without_top_level_const(parameter.type);
        parameter.type = without_alignment(parameter.type);
    }
    signature.result_type = without_alignment(signature.result_type);
    Type type;
    type.kind = Type::Kind::Function;
    type.function = std::move(signature);
    if (const auto found = find_type(*this, type, [&](const Type& candidate) {
            return candidate.kind == Type::Kind::Function && candidate.function == type.function;
        }))
        return *found;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

std::optional<TypeId> Module::common_pointer_type(TypeId left, TypeId right, const AddressSpaceJoin& spaces) {
    struct Traits {
        using Type = TypeId;
        Module& module;
        PointerJoinNode<Type> describe(Type id) const {
            const auto& type = module.type(id);
            PointerJoinNode<Type> node;
            switch (type.kind) {
            case hir::Type::Kind::Pointer: node.kind = PointerJoinKind::Pointer; node.child = type.pointee; break;
            case hir::Type::Kind::Array: node.kind = PointerJoinKind::Array; node.child = type.element; break;
            case hir::Type::Kind::Vector: node.kind = PointerJoinKind::Vector; node.child = type.element; break;
            case hir::Type::Kind::Function: node.kind = PointerJoinKind::Function; break;
            case hir::Type::Kind::Builtin:
                if (type.builtin == BuiltinType::Void) node.kind = PointerJoinKind::Void;
                break;
            default: break;
            }
            node.is_const = type.is_const;
            node.is_volatile = type.is_volatile;
            node.is_atomic = type.is_atomic;
            node.is_restrict = type.is_restrict;
            node.alignment = type.alignment;
            node.address_space = type.address_space;
            node.extent = type.lanes;
            node.scalable = type.scalable;
            return node;
        }
        PointerJoinEquality equal_leaf(Type left, Type right) const {
            auto a = module.type(left), b = module.type(right);
            a.is_const = b.is_const = a.is_volatile = b.is_volatile = false;
            a.is_restrict = b.is_restrict = a.may_alias = b.may_alias = false;
            a.alignment = b.alignment = 0;
            return identical(a, b) ? PointerJoinEquality::Same : PointerJoinEquality::Different;
        }
        Type rebuild(Type base, const PointerJoinNode<Type>& node) {
            auto result = module.type(base);
            result.is_const = node.is_const;
            result.is_volatile = node.is_volatile;
            result.is_atomic = node.is_atomic;
            result.is_restrict = node.is_restrict;
            result.alignment = node.alignment;
            result.address_space = node.address_space;
            if (node.kind == PointerJoinKind::Pointer) result.pointee = node.child;
            else if (node.kind == PointerJoinKind::Array || node.kind == PointerJoinKind::Vector)
                result.element = node.child;
            if (const auto found = find_type(module, result,
                    [&](const hir::Type& existing) { return identical(existing, result); }))
                return *found;
            const Type id{static_cast<std::uint32_t>(module.types.size())};
            module.types.push_back(std::move(result));
            return id;
        }
    } traits{*this};
    return join_pointer_types(traits, left, right, spaces).type;
}

TypeId Module::pointer_to(TypeId pointee) {
    Type type;
    type.kind = Type::Kind::Pointer;
    type.pointee = pointee;
    if (const auto found = find_type(*this, type, [&](const Type& candidate) {
            return candidate.kind == Type::Kind::Pointer &&
                candidate.pointee == pointee && !candidate.is_const &&
                !candidate.is_volatile && !candidate.is_atomic &&
                !candidate.is_restrict && !candidate.may_alias && candidate.address_space == 0 &&
                !candidate.alignment && candidate.nominal_key().empty();
        }))
        return *found;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

TypeId Module::without_top_level_const(TypeId id) {
    const auto& source = type(id);
    if (!source.is_const) return id;
    Type candidate = source;
    candidate.is_const = false;
    if (const auto found = find_type(*this, candidate,
            [&](const Type& existing) { return identical(existing, candidate); }))
        return *found;
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::without_alignment(TypeId id) {
    const auto& source = type(id);
    if (!source.alignment) return id;
    Type candidate = source;
    candidate.alignment = 0;
    if (const auto found = find_type(*this, candidate,
            [&](const Type& existing) { return identical(existing, candidate); }))
        return *found;
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::without_may_alias(TypeId id) {
    Type candidate = type(id);
    if (candidate.pointee) candidate.pointee = without_may_alias(*candidate.pointee);
    if (candidate.element) candidate.element = without_may_alias(*candidate.element);
    if (!candidate.may_alias && candidate.pointee == type(id).pointee &&
        candidate.element == type(id).element) return id;
    candidate.may_alias = false;
    if (const auto found = find_type(*this, candidate,
            [&](const Type& existing) { return identical(existing, candidate); }))
        return *found;
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::unqualified(TypeId id) {
    const auto& source = type(id);
    if (!source.is_const && !source.is_volatile && !source.is_atomic &&
        !source.is_restrict && !source.may_alias) {
        return id;
    }
    Type candidate = source;
    candidate.is_const = false;
    candidate.is_volatile = false;
    candidate.is_atomic = false;
    candidate.is_restrict = false;
    candidate.may_alias = false;
    if (const auto found = find_type(*this, candidate,
            [&](const Type& existing) { return identical(existing, candidate); }))
        return *found;
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::add_qualifiers(TypeId id, bool is_const,
                              bool is_volatile, bool may_alias) {
    const auto& source = type(id);
    if ((!is_const || source.is_const) &&
        (!is_volatile || source.is_volatile) &&
        (!may_alias || source.may_alias)) {
        return id;
    }
    Type candidate = source;
    candidate.is_const = candidate.is_const || is_const;
    candidate.is_volatile = candidate.is_volatile || is_volatile;
    candidate.may_alias = candidate.may_alias || may_alias;
    if (const auto found = find_type(*this, candidate,
            [&](const Type& existing) { return identical(existing, candidate); }))
        return *found;
    const TypeId result{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(candidate));
    return result;
}

TypeId Module::vector_of(TypeId element, std::uint32_t lanes,
                         bool scalable) {
    Type type;
    type.kind = Type::Kind::Vector;
    type.element = element;
    type.lanes = lanes;
    type.scalable = scalable;
    if (const auto found = find_type(*this, type, [&](const Type& candidate) {
            return candidate.kind == Type::Kind::Vector &&
                candidate.element == element && candidate.lanes == lanes &&
                candidate.scalable == scalable && !candidate.is_const &&
                !candidate.is_volatile && !candidate.is_atomic &&
                !candidate.is_restrict && !candidate.may_alias && !candidate.alignment;
        }))
        return *found;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

TypeId Module::array_of(TypeId element, std::uint32_t elements) {
    Type type;
    type.kind = Type::Kind::Array;
    type.element = element;
    type.lanes = elements;
    if (const auto found = find_type(*this, type, [&](const Type& candidate) {
            return candidate.kind == Type::Kind::Array &&
                candidate.element == element && candidate.lanes == elements &&
                !candidate.is_const && !candidate.is_volatile &&
                !candidate.is_atomic && !candidate.is_restrict && !candidate.may_alias &&
                !candidate.alignment;
        }))
        return *found;
    const TypeId id{static_cast<std::uint32_t>(types.size())};
    types.push_back(std::move(type));
    return id;
}

const Record* Module::record(const NominalTypeKey& key) const {
    const auto found = record_ids.find(key);
    return found == record_ids.end() ? nullptr
                                    : &records.at(found->second.value);
}

const RecordMember* Module::member(RecordId id,
                                   const MemberName& name) const {
    const auto& source = record(id);
    const auto found = std::find_if(
        source.members.begin(), source.members.end(),
        [&](const RecordMember& candidate) { return candidate.member_name() == name; });
    return found == source.members.end() ? nullptr : &*found;
}

const Object* Module::object(const ObjectDecl& declaration) const {
    const auto found = object_ids.find(&declaration);
    return found == object_ids.end() ? nullptr : &objects[found->second.value];
}

const Label* Module::label(FunctionId function, NameUse name) const {
    if (name.context && name.context->label_address) return label(*name.context->label_address);
    const auto split = name.spelling.rfind("::");
    const auto component = split == std::string_view::npos
        ? name.spelling : name.spelling.substr(split + 2);
    const auto location = name.context && name.context->last_component_location.valid()
        ? name.context->last_component_location : name.location;
    const NameKey key(component, location);
    const auto binding = resolved_label_binding(name);
    const auto binding_matches = [&](const Label& candidate) {
        if (binding.scope && binding.scope != candidate.binding.scope) return false;
        return binding.kind != LabelBinding::Kind::Reference ||
               binding.declaration == candidate.binding.declaration;
    };
    const Label* qualified_fallback = nullptr;
    for (const auto id : functions.at(function.value).labels) {
        const auto& candidate = labels.at(id.value);
        if (candidate.source_name != name.spelling && candidate.qualified_name != name.spelling) continue;
        if (!binding_matches(candidate)) continue;
        if (candidate.is_global || candidate.lookup_key == key) return &candidate;
        // Explicit qualification can name an ordinary label through its
        // visible function, but cannot select an unrelated private expansion.
        if (split != std::string_view::npos && !candidate.lookup_key.context.value &&
            candidate.lookup_key.fresh == key.fresh) qualified_fallback = &candidate;
    }
    if (qualified_fallback) return qualified_fallback;
    for (const auto& candidate : labels) {
        if (candidate.owner == function && candidate.is_global && binding_matches(candidate) &&
            (candidate.source_name == name.spelling ||
             candidate.qualified_name == name.spelling)) {
            return &candidate;
        }
    }
    return nullptr;
}

const Label* Module::label(FunctionId function, const Statement& definition) const {
    for (const auto id : functions.at(function.value).labels)
        if (labels.at(id.value).definition == &definition) return &labels.at(id.value);
    return nullptr;
}

const Label* Module::label(const LabelAddressConstant& address) const {
    if (!address.owner) return nullptr;
    const auto* owner = function(*address.owner);
    if (!owner) return nullptr;
    if (address.definition) return label(owner->id, *address.definition);
    const auto* selected = global_label(address.global_name);
    return selected && selected->owner == owner->id ? selected : nullptr;
}

const Label* Module::global_label(std::string_view qualified_name) const {
    for (const auto& candidate : labels) {
        if (candidate.is_global &&
            candidate.qualified_name == qualified_name) {
            return &candidate;
        }
    }
    return nullptr;
}

bool Module::raw_owned(const FunctionDecl& declaration) const {
    const auto* entity = function(declaration);
    return entity && entity->definition == &declaration &&
           entity->ownership == BodyOwnership::RawMir;
}

Module build(Program& program, const CompilerOptions& options,
             const TargetInfo& target, Diagnostics& diagnostics) {
    return build_async(program, options, target, diagnostics).run();
}

ContinuationTask<Module> build_async(Program& program, const CompilerOptions& options,
             const TargetInfo& target, Diagnostics& diagnostics) {
    Builder builder(program, options, target, diagnostics);
    co_return co_await builder.run_async();
}

Module build_constant_context(Program& program, const CompilerOptions& options,
                              const TargetInfo& target, Diagnostics& diagnostics) {
    return build_constant_context_async(program, options, target, diagnostics).run();
}

ContinuationTask<Module> build_constant_context_async(Program& program, const CompilerOptions& options,
                              const TargetInfo& target, Diagnostics& diagnostics) {
    Builder builder(program, options, target, diagnostics);
    co_return co_await builder.constant_context_async();
}

Module build_record_layout_context(Program& program, const CompilerOptions& options,
                                   const TargetInfo& target, Diagnostics& diagnostics) {
    return build_record_layout_context_async(program, options, target, diagnostics).run();
}

ContinuationTask<Module> build_record_layout_context_async(Program& program, const CompilerOptions& options,
                                   const TargetInfo& target, Diagnostics& diagnostics) {
    Builder builder(program, options, target, diagnostics);
    co_return co_await builder.record_layout_context_async();
}

bool layout_view_covers(const Module& module, const Program& program, const TypePtr& type) {
    return layout_view_matches_records(module, program, type, LayoutViewCoverage::Complete);
}

bool extended_layout_view_covers(const Module& module, const Program& program, const TypePtr& type) {
    return layout_view_matches_records(module, program, type, LayoutViewCoverage::Closed);
}

Module build_required_layout_context(Program& program, const CompilerOptions& options,
                                     const TargetInfo& target, Diagnostics& diagnostics,
                                     const TypePtr& type, EvaluationLayoutKind kind) {
    return build_required_layout_context_async(program, options, target, diagnostics, type, kind).run();
}

ContinuationTask<Module> build_required_layout_context_async(Program& program, const CompilerOptions& options,
                                     const TargetInfo& target, Diagnostics& diagnostics,
                                     TypePtr type, EvaluationLayoutKind kind) {
    Builder builder(program, options, target, diagnostics);
    co_return co_await builder.required_layout_context_async(std::move(type), kind);
}

ContinuationTask<Module> build_required_layout_context_async(Program& program, const CompilerOptions& options,
                                     const TargetInfo& target, Diagnostics& diagnostics,
                                     TypePtr type, EvaluationLayoutKind kind,
                                     Module seed, RecordSourceProofs& proofs) {
    Builder builder(program, options, target, diagnostics, std::move(seed), &proofs);
    co_return co_await builder.required_layout_context_async(std::move(type), kind);
}

bool validate_source_address_spaces(Program& program,
                                    const CompilerOptions& options,
                                    const TargetInfo& target,
                                    Diagnostics& diagnostics) {
    Builder(program, options, target, diagnostics).validate_address_spaces();
    return diagnostics.errors() == 0;
}

void stabilize_function_address(Module& module, FunctionId id) {
    module.function(id).abi_contract = AbiContract::Registered;
}

bool stabilize_label_address(Module& module, LabelId id,
                             SourceLocation location, Diagnostics& diagnostics) {
    const auto& label = module.labels.at(id.value);
    auto& function = module.function(label.owner);
    if (function.definition && function.definition->attribute("always_inline")) {
        diagnostics.error(location,
            "taking a label address conflicts with always_inline on its owner");
        return false;
    }
    // Freezing private transport does not erase explicit endpoints or clobbers.
    // Unlike a callable function pointer, no registered-interface adapter is
    // needed to represent an interior code address.
    function.abi_contract = AbiContract::Registered;
    return true;
}

namespace {

bool manual_endpoints(const std::optional<std::string>& result_location,
                      const std::vector<Parameter>& parameters) {
    const auto manual = [](const std::optional<std::string>& location) {
        return location && *location != "auto";
    };
    return manual(result_location) ||
           std::any_of(parameters.begin(), parameters.end(),
                       [&](const Parameter& parameter) {
                           return manual(parameter.physical_location);
                       });
}

} // namespace

bool manual_interface(const Function& function) {
    return manual_endpoints(function.result_location, function.parameters);
}

bool manual_interface(const FunctionSignature& signature) {
    return manual_endpoints(signature.result_location, signature.parameters);
}

std::optional<FunctionSignature>
call_signature(const Module& module, std::optional<FunctionId> direct,
               std::optional<TypeId> indirect) {
    if (direct.has_value() == indirect.has_value()) return std::nullopt;
    if (direct) {
        if (direct->value >= module.functions.size()) return std::nullopt;
        const auto& function = module.function(*direct);
        FunctionSignature signature;
        signature.result_type = function.result_type;
        signature.parameters = function.parameters;
        signature.abi = function.abi;
        signature.variadic = function.variadic;
        signature.result_location = function.result_location;
        signature.clobbers = function.clobbers;
        std::sort(signature.clobbers.begin(), signature.clobbers.end());
        if (!function.declarations.empty()) {
            const auto* declaration = function.definition
                                          ? function.definition
                                          : function.declarations.back();
            const auto cleanup = decode_attribute_string(
                declaration->attribute("stack_cleanup"));
            if (!cleanup.empty()) signature.stack_cleanup = cleanup;
        }
        return signature;
    }
    if (indirect->value >= module.types.size()) return std::nullopt;
    const auto& type = module.type(*indirect);
    return type.kind == Type::Kind::Function ? type.function : std::nullopt;
}

static bool same_callable_type(const Module& module, TypeId left, TypeId right,
                               bool top, bool ignore_const) {
    if (left == right) return true;
    auto a = module.type(left), b = module.type(right);
    if (a.kind != b.kind) return false;
    a.may_alias = b.may_alias = false;
    if (top) a.alignment = b.alignment = 0;
    if (ignore_const) a.is_const = b.is_const = false;
    if (a.kind == Type::Kind::Function) {
        if (!a.function || !b.function ||
            !same_interface(module, *a.function, *b.function))
            return false;
        a.function.reset();
        b.function.reset();
    }
    if (a.pointee.has_value() != b.pointee.has_value()) return false;
    if (a.pointee) {
        if (!same_callable_type(module, *a.pointee, *b.pointee, false, false))
            return false;
        a.pointee = b.pointee;
    }
    if (a.element.has_value() != b.element.has_value()) return false;
    if (a.element) {
        if (!same_callable_type(module, *a.element, *b.element, false, false))
            return false;
        a.element = b.element;
    }
    return identical(a, b);
}

bool same_callable_type(const Module& module, TypeId left, TypeId right) {
    return same_callable_type(module, left, right, true, false);
}

bool same_interface(const Module& module, const FunctionSignature& left,
                    const FunctionSignature& right) {
    if (left.parameters.size() != right.parameters.size()) return false;
    auto normalized = right;
    if (same_callable_type(module, left.result_type, right.result_type, true, false))
        normalized.result_type = left.result_type;
    for (std::size_t index = 0; index < left.parameters.size(); ++index) {
        const auto& parameter = left.parameters[index];
        auto& other = normalized.parameters[index];
        if (parameter.mode != other.mode) continue;
        if (same_callable_type(module, parameter.type, other.type, true,
                               parameter.mode == ParameterMode::In))
            other.type = parameter.type;
    }
    return left == normalized;
}

std::string type_name(const Module& module, TypeId id) {
    const auto& type = module.type(id);
    std::string prefix;
    if (type.is_const) prefix += "const ";
    if (type.is_volatile) prefix += "volatile ";
    if (type.is_restrict) prefix += "restrict ";
    if (type.is_atomic) prefix += "[[atomic]] ";
    if (type.may_alias) prefix += "[[may_alias]] ";
    if (type.alignment) prefix += "[[aligned(" + std::to_string(type.alignment) + ")]] ";
    if (type.kind == Type::Kind::Pointer) {
        return prefix + type_name(module, *type.pointee) +
               (type.address_space == 0
                    ? " *"
                    : " [[address_space(" +
                          std::to_string(type.address_space) + ")]] *");
    }
    if (type.kind == Type::Kind::Function && type.function) {
        std::string result =
            type_name(module, type.function->result_type) + " (";
        for (std::size_t index = 0; index < type.function->parameters.size();
             ++index) {
            if (index) result += ", ";
            const auto& parameter = type.function->parameters[index];
            result += std::string(parameter_mode_name(parameter.mode)) + " " +
                      type_name(module, parameter.type);
        }
        if (type.function->variadic)
            result += type.function->parameters.empty() ? "..." : ", ...";
        return prefix + result + ")";
    }
    if (type.kind == Type::Kind::Vector) {
        return prefix + (type.scalable ? "scalable_vector<" : "vector<") +
               type_name(module, *type.element) + "," +
               std::to_string(type.lanes) + ">";
    }
    if (type.kind == Type::Kind::Array) {
        return prefix + type_name(module, *type.element) + "[" +
               (type.lanes == 0 ? std::string("*")
                                : std::to_string(type.lanes)) +
               "]";
    }
    if (type.kind == Type::Kind::Record && type.record) {
        const auto& record = module.record(*type.record);
        return prefix + (record.is_union ? "union " : "struct ") +
               record.source_name;
    }
    if (!type.nominal_key().empty()) {
        return prefix + "enum " + type.nominal_name;
    }
    static constexpr std::string_view names[] = {
        "void", "bool", "i8", "u8", "i16", "u16", "i32", "u32",
        "i64", "u64", "i128", "u128", "iptr", "uptr", "f32",
        "f64", "f80", "f128", "fptr", "label",
    };
    return prefix + std::string(names[static_cast<unsigned>(type.builtin)]);
}

namespace {

std::optional<std::uint64_t> source_alignment(const Module& module, const TypePtr& type,
                                              const TargetInfo& target);

// Natural size of a prepared AST type; elements keep their requests.
std::optional<std::uint64_t> source_natural_size(const Module& module, const TypePtr& type,
                                                 const TargetInfo& target) {
    switch (type->kind) {
    case cross::Type::Kind::Builtin:
        return builtin_storage_size(type->builtin, module.address_bits,
                                    target.data_layout.f80_storage_bytes);
    case cross::Type::Kind::Pointer: return (module.address_bits + 7U) / 8U;
    case cross::Type::Kind::Record: {
        const auto* record = module.record(type->nominal_key());
        return record && record->complete && record->size ? std::optional{record->size} : std::nullopt;
    }
    case cross::Type::Kind::Array:
    case cross::Type::Kind::Vector: {
        if (!type->lanes || type->scalable) return {};
        const auto element = layout_size(module, type->element, target);
        if (!element || !*element || type->lanes > UINT64_MAX / *element) return {};
        return *element * type->lanes;
    }
    default: return {};
    }
}

std::optional<std::uint64_t> source_alignment(const Module& module, const TypePtr& type,
                                              const TargetInfo& target) {
    if (!type) return {};
    std::optional<std::uint64_t> natural;
    if (type->kind == cross::Type::Kind::Record) {
        const auto* record = module.record(type->nominal_key());
        if (record && (record->complete || record->alignment_complete)) natural = record->alignment;
    } else if (type->kind == cross::Type::Kind::Array) {
        natural = source_alignment(module, type->element, target);
    } else if (const auto size = source_natural_size(module, type, target)) {
        natural = natural_storage_alignment(*size,
            type->kind == cross::Type::Kind::Builtin && type->builtin == BuiltinType::F80,
            target.data_layout.natural_alignment_limit, target.data_layout.f80_alignment);
    }
    if (!natural) return {};
    return std::max<std::uint64_t>(*natural, type->alignment);
}

} // namespace

std::optional<std::uint64_t> layout_size(const Module& module, const TypePtr& type,
                                      const TargetInfo& target) {
    if (!type) return {};
    const auto size = source_natural_size(module, type, target);
    if (!size || !type->alignment) return size;
    const auto alignment = source_alignment(module, type, target);
    if (!alignment) return {};
    const auto storage = requested_storage({*size, *alignment}, type->alignment);
    return storage ? std::optional{storage->size} : std::nullopt;
}

std::optional<std::uint64_t> natural_size(const Module& module, TypeId id,
                                          const TargetInfo& target) {
    const auto& type = module.type(id);
    if (type.kind == Type::Kind::Pointer) {
        return (module.address_bits + 7U) / 8U;
    }
    if (type.kind == Type::Kind::Record) {
        if (!type.record || !module.record(*type.record).complete ||
            module.record(*type.record).size == 0) {
            return std::nullopt;
        }
        return module.record(*type.record).size;
    }
    if (type.kind == Type::Kind::Array) {
        if (!type.element || type.lanes == 0) return std::nullopt;
        const auto element = layout_size(module, *type.element, target);
        if (!element || *element == 0 ||
            type.lanes > std::numeric_limits<std::uint64_t>::max() /
                             *element) {
            return std::nullopt;
        }
        return *element * type.lanes;
    }
    if (type.kind == Type::Kind::Vector) {
        if (type.scalable || !type.element || type.lanes == 0) {
            return std::nullopt;
        }
        const auto element = layout_size(module, *type.element, target);
        if (!element ||
            type.lanes > std::numeric_limits<std::uint64_t>::max() /
                             *element) {
            return std::nullopt;
        }
        return *element * type.lanes;
    }
    if (type.kind != Type::Kind::Builtin) return std::nullopt;
    return builtin_storage_size(type.builtin, module.address_bits,
                                target.data_layout.f80_storage_bytes);
}

std::optional<std::uint64_t> natural_alignment(
    const Module& module, TypeId id, const TargetInfo& target) {
    const auto& type = module.type(id);
    if (type.kind == Type::Kind::Record) {
        if (!type.record || (!module.record(*type.record).complete &&
                             !module.record(*type.record).alignment_complete)) {
            return std::nullopt;
        }
        return module.record(*type.record).alignment;
    }
    if (type.kind == Type::Kind::Array && type.element) {
        return layout_alignment(module, *type.element, target);
    }
    const auto size = natural_size(module, id, target);
    if (!size) return std::nullopt;
    return natural_storage_alignment(*size,
        type.kind == Type::Kind::Builtin && type.builtin == BuiltinType::F80,
        target.data_layout.natural_alignment_limit, target.data_layout.f80_alignment);
}

std::optional<std::uint64_t> layout_size(const Module& module, TypeId id,
                                         const TargetInfo& target) {
    const auto size = natural_size(module, id, target);
    const auto requested = module.type(id).alignment;
    if (!size || !requested) return size;
    const auto alignment = natural_alignment(module, id, target);
    if (!alignment) return std::nullopt;
    const auto storage = requested_storage({*size, *alignment}, requested);
    return storage ? std::optional{storage->size} : std::nullopt;
}

std::optional<std::uint64_t> layout_alignment(
    const Module& module, TypeId id, const TargetInfo& target) {
    const auto alignment = natural_alignment(module, id, target);
    if (!alignment) return std::nullopt;
    return std::max<std::uint64_t>(*alignment, module.type(id).alignment);
}

unsigned requested_alignment(const Module& module, TypeId id) {
    unsigned result = 0;
    for (;;) {
        const auto& type = module.type(id);
        result = std::max(result, type.alignment);
        if (type.kind != Type::Kind::Array || !type.element) return result;
        id = *type.element;
    }
}

bool lock_free_atomic_type(const Module& module, TypeId id,
                           const TargetInfo& target, const Subtarget& subtarget) {
    const auto& type = module.type(id);
    if (type.kind != Type::Kind::Builtin && type.kind != Type::Kind::Pointer) return false;
    if (type.kind == Type::Kind::Builtin &&
        (type.builtin == BuiltinType::Void || type.builtin == BuiltinType::Label)) return false;
    const auto size = natural_size(module, id, target);
    if (!size) return false;
    // Extended floating storage may include target padding; capability entries
    // describe the scalar representation width, as they do in MIR selection.
    const auto bits = type.kind == Type::Kind::Builtin && type.builtin == BuiltinType::F80
        ? 80U : *size * 8U;
    return std::any_of(target.lock_free_atomic_widths.begin(), target.lock_free_atomic_widths.end(),
        [&](const AtomicWidthEntry& entry) {
            return entry.bits == bits && (entry.feature.empty() || entry.feature == "base" ||
                entry.feature == target.architecture || subtarget.has_feature(entry.feature));
        });
}

} // namespace cross::hir
