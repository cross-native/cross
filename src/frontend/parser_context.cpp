// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <algorithm>
#include <unordered_set>

namespace cross {
namespace {

template<class Add, class Name>
void type_storage(const TypePtr& root, Add&& add, Name&& name, const bool& over_budget) {
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> pending{root};
    while (!pending.empty() && !over_budget) {
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        add(128); name(next->generic_name); name(next->nominal_name);
        pending.push_back(next->pointee);
        pending.push_back(next->element);
        if (next->function) {
            add(96); name(next->function->abi);
            if (next->function->result_location) name(*next->function->result_location);
            if (next->function->stack_cleanup) name(*next->function->stack_cleanup);
            for (const auto& clobber : next->function->clobbers) name(clobber);
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                add(64); name(parameter.name);
                if (parameter.location_name) name(*parameter.location_name);
                pending.push_back(parameter.type);
            }
        }
    }
}

} // namespace

void SyntaxHeaderBindings::finish(std::vector<GenericParameter> values,
    SyntaxExecution& execution, SourceLocation location) {
    const auto maximum = std::min(execution.limits().bytes, execution.limits().memory);
    bool over_budget = storage > maximum;
    std::uint64_t work = 1;
    const auto add = [&](std::uint64_t bytes) {
        ++work;
        if (bytes > maximum - std::min(storage, maximum)) over_budget = true;
        else storage += bytes;
    };
    const auto name = [&](std::string_view text) { add(32); add(text.size()); };
    for (const auto& parameter : values) {
        add(32); name(parameter.name);
        type_storage(parameter.value_type, add, name, over_budget);
    }
    complete = true;
    if (over_budget || !execution.work(location, work)) {
        if (over_budget) execution.tree_limit_error(location);
        return;
    }
    parameters = std::move(values);
    for (auto& parameter : parameters) parameter.value_type = copy_type(parameter.value_type);
}

std::uint64_t syntax_environment_storage(const SyntaxParseEnvironment& environment) {
    return environment.storage + (environment.header_bindings ? environment.header_bindings->storage : 0);
}

std::optional<SyntaxEntityId> syntax_resolve_entity(
    const SyntaxNode& node, std::string_view name, Diagnostics& diagnostics,
    SourceLocation location) {
    if (!node.context || !node.context->parse_environment ||
        !node.context->parse_environment->syntax) {
        diagnostics.error(location, "$::meta::is_extension requires a node with retained syntax context");
        return {};
    }
    return node.context->parse_environment->syntax->resolve(
        name, node.context->name_space, location, diagnostics);
}

std::shared_ptr<const SyntaxParseEnvironment> Parser::snapshot_environment() const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    const auto maximum = std::min(limits.bytes, limits.memory);
    std::uint64_t storage = 192;
    std::uint64_t work = 1;
    bool over_budget = storage > maximum;
    const auto add = [&](std::uint64_t size) {
        ++work;
        if (size > maximum - std::min(storage, maximum)) over_budget = true;
        else storage += size;
    };
    const auto name = [&](std::string_view text) { add(32); add(text.size()); };
    const auto type = [&](const TypePtr& root) { type_storage(root, add, name, over_budget); };
    for (const auto& entry : active_generic_types_) name(entry);
    for (const auto& [entry, parameters] : known_generic_functions_) {
        name(entry); add(32 + parameters.size() * 8);
    }
    for (const auto& entry : known_ordinary_values_) name(entry);
    for (const auto& scope : local_scopes_) {
        add(32);
        for (const auto& [entry, binding] : scope) {
            (void)binding; name(entry.spelling); add(64);
        }
    }
    add(scope_origins_.size() * 40);
    for (const auto& scope : local_type_scopes_) {
        add(32);
        for (const auto& [entry, value] : scope) { name(entry.spelling); type(value); }
    }
    for (const auto& [entry, value] : type_aliases_) { name(entry); type(value); }
    for (const auto& [entry, value] : enum_types_) { (void)value; name(entry); add(16); }
    for (const auto& [entry, value] : record_types_) { (void)value; name(entry); add(16); }
    if (syntax_) {
        for (const auto& scope : syntax_->imports()) {
            add(32);
            for (const auto& entry : scope) name(entry);
        }
        for (const auto& binding : syntax_->bindings()) { add(16); name(binding.prefix); }
    }
    if (active_function_) {
        for (const auto& parameter : active_function_->parameters) {
            add(64); name(parameter.name); type(parameter.type);
            if (parameter.location_name) name(*parameter.location_name);
        }
        for (const auto& parameter : active_function_->generic_parameters) {
            add(32); name(parameter.name); type(parameter.value_type);
        }
    }
    add(8 * public_uncertain_binding_depths_.size() + switch_default_seen_.size());
    if (header_bindings_ && header_bindings_->storage > maximum - std::min(storage, maximum))
        over_budget = true;
    if (over_budget || (execution && !execution->work(current().location, work))) {
        if (over_budget) {
            if (execution) execution->tree_limit_error(current().location);
            diagnostics_.error(current().location, "syntax context byte or memory budget exceeded");
        }
        return {};
    }
    auto result = std::make_shared<SyntaxParseEnvironment>();
    result->address_bits = address_bits_;
    if (syntax_) {
        result->execution = execution;
        result->syntax.emplace(*syntax_, nullptr);
    }
    result->scope_imports = current_scope_imports_;
    result->generic_types = active_generic_types_;
    result->generic_functions = known_generic_functions_;
    result->ordinary_values = known_ordinary_values_;
    result->values = local_scopes_;
    result->local_aliases = local_type_scopes_;
    result->scope_origins = scope_origins_;
    result->scope_event_base = scope_events_->size();
    result->header_bindings = header_bindings_;
    for (auto& scope : result->local_aliases)
        for (auto& [entry, value] : scope) { (void)entry; value = copy_type(value); }
    for (const auto& [entry, value] : type_aliases_)
        result->aliases.emplace(entry, copy_type(value));
    result->enumerations = enum_types_;
    result->records = record_types_;
    result->function_context = active_function_ != nullptr;
    if (active_function_) {
        result->parameters = active_function_->parameters;
        for (auto& parameter : result->parameters) parameter.type = copy_type(parameter.type);
        result->generic_parameters = active_function_->generic_parameters;
        for (auto& parameter : result->generic_parameters)
            parameter.value_type = copy_type(parameter.value_type);
    }
    result->uncertain_depths = public_uncertain_binding_depths_;
    result->procedural_body = parsing_procedural_body_;
    result->switch_depth = switch_depth_;
    result->switch_defaults = switch_default_seen_;
    result->storage = storage;
    return result;
}

std::shared_ptr<const SyntaxContext> Parser::syntax_context(SourceLocation location) const {
    const auto origin = token_origin(location);
    if (origin.context && origin.context->parse_environment && !recording_public_tree_)
        return origin.context;
    const auto environment = snapshot_environment();
    if (!environment) return {};
    auto context = origin.context ? std::make_shared<SyntaxContext>(*origin.context)
                                  : std::make_shared<SyntaxContext>();
    if (!origin.context) {
        context->invocation = location;
        context->name_space = active_namespace_;
        context->imports = active_imports_;
        if (syntax_) context->syntax_bindings = syntax_->bindings();
    }
    context->parse_environment = environment;
    return context;
}

void Parser::restore_environment(const SyntaxParseEnvironment& environment,
                                 const SyntaxContext& context) {
    address_bits_ = environment.address_bits;
    if (environment.syntax)
        syntax_.emplace(*environment.syntax, environment.execution.lock());
    active_namespace_ = context.name_space;
    active_imports_ = context.imports;
    current_scope_imports_ = environment.scope_imports;
    active_generic_types_ = environment.generic_types;
    known_generic_functions_ = environment.generic_functions;
    known_ordinary_values_ = environment.ordinary_values;
    local_scopes_ = environment.values;
    local_type_scopes_ = environment.local_aliases;
    scope_origins_ = environment.scope_origins;
    for (auto& scope : local_type_scopes_)
        for (auto& [entry, value] : scope) { (void)entry; value = copy_type(value); }
    type_aliases_.clear();
    for (const auto& [entry, value] : environment.aliases)
        type_aliases_.emplace(entry, copy_type(value));
    enum_types_ = environment.enumerations;
    record_types_ = environment.records;
    public_uncertain_binding_depths_ = environment.uncertain_depths;
    header_bindings_ = environment.header_bindings;
    parsing_procedural_body_ = environment.procedural_body;
    switch_depth_ = environment.switch_depth;
    switch_default_seen_ = environment.switch_defaults;
    if (environment.function_context) {
        restored_function_context_ = std::make_unique<FunctionDecl>();
        restored_function_context_->parameters = environment.parameters;
        for (auto& parameter : restored_function_context_->parameters)
            parameter.type = copy_type(parameter.type);
        restored_function_context_->generic_parameters = environment.generic_parameters;
        for (auto& parameter : restored_function_context_->generic_parameters)
            parameter.value_type = copy_type(parameter.value_type);
        active_function_ = restored_function_context_.get();
    }
    if (header_bindings_ && header_bindings_->complete) {
        if (!environment.function_context) {
            restored_function_context_ = std::make_unique<FunctionDecl>();
            active_function_ = restored_function_context_.get();
        }
        active_function_->generic_parameters = header_bindings_->parameters;
        for (auto& parameter : active_function_->generic_parameters)
            parameter.value_type = copy_type(parameter.value_type);
        active_generic_types_.clear();
        for (const auto& parameter : header_bindings_->parameters)
            if (!parameter.value_type) active_generic_types_.push_back(parameter.name);
    }
}

void Parser::restore_deferred_environment(const SyntaxParseEnvironment& environment,
                                          const SyntaxContext& context,
                                          SourceLocation original_position) {
    // Reparse against the saved environment plus declarations made after the
    // capture by earlier statements of its original lexical block. A moved
    // fragment never inherits declarations from its destination block.
    restore_environment(environment, context);
    if (!original_position.valid()) return;
    for (std::size_t at = environment.scope_event_base; at < scope_events_->size(); ++at) {
        const auto& event = (*scope_events_)[at];
        if (event.statement.file != original_position.file ||
            event.statement.offset >= original_position.offset) continue;
        for (std::size_t captured = 0; captured < scope_origins_.size(); ++captured) {
            if (scope_origins_[captured] != event.block) continue;
            for (const auto& [key, binding] : event.values)
                local_scopes_[captured].emplace(key, binding);
            for (const auto& [key, type] : event.aliases)
                local_type_scopes_[captured].try_emplace(key, copy_type(type));
        }
    }
}

std::shared_ptr<const SyntaxNode> Parser::parse_syntax_tokens(
    SyntaxParseCategory category, std::vector<Token> input,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics) {
    if (!context || !context->parse_environment || input.empty()) return {};
    const auto& environment = *context->parse_environment;
    // A standalone schema parser may have no executor. An expired compilation
    // executor, however, must not be replaced with ambient caller state.
    if (environment.syntax && environment.execution.expired()) return {};
    Parser parser({}, diagnostics, nullptr, environment.address_bits);
    parser.restore_environment(environment, *context);
    return parser.parse_syntax_tokens(category, std::move(input));
}

} // namespace cross
