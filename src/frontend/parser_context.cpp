// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace cross {
namespace {

// Display spans may be shared by all freshly constructed tokens. Only source
// identity and position in the same original token stream establish order.
bool precedes(const TokenIdentity& left, const TokenIdentity& right) {
    if (!left.source_unit || left.source_unit != right.source_unit || left.expansion != right.expansion)
        return false;
    return left.expansion.value ? left.output_position < right.output_position : left.offset < right.offset;
}

template<class Add, class Name>
void type_storage(const TypePtr& root, Add&& add, Name&& name, const bool& over_budget,
                  std::uint64_t maximum) {
    std::unordered_set<const Type*> seen;
    std::unordered_set<const Expr*> seen_expressions;
    std::unordered_set<const SyntaxContext*> seen_contexts;
    std::vector<TypePtr> pending{root};
    std::vector<const Expr*> expressions;
    while ((!pending.empty() || !expressions.empty()) && !over_budget) {
        if (!expressions.empty()) {
            const auto* expression = expressions.back();
            expressions.pop_back();
            if (!expression || !seen_expressions.insert(expression).second) continue;
            add(256); name(expression->text); name(expression->string_value);
            if (expression->translation_context &&
                seen_contexts.insert(expression->translation_context.get()).second)
                add(syntax_context_storage(*expression->translation_context));
            pending.push_back(expression->type);
            expressions.push_back(expression->left.get());
            expressions.push_back(expression->right.get());
            expressions.push_back(expression->third.get());
            if (expression->name_context) {
                add(232); name(expression->name_context->name_space);
                add(function_label_storage(expression->name_context->label_binding.scope));
                if (const auto& address = expression->name_context->label_address) {
                    add(64); name(address->global_name);
                }
                for (const auto& imported : expression->name_context->imports) name(imported);
                add(value_binding_storage(expression->name_context->value_binding, {}));
                add(fragment_lookup_storage(expression->name_context->fragment_lookup));
            }
            for (const auto& argument : expression->arguments) expressions.push_back(argument.get());
            for (const auto& argument : expression->generic_arguments) {
                add(32); pending.push_back(argument.type); expressions.push_back(argument.value.get());
            }
            for (const auto& entry : expression->initializer_entries) {
                add(48); expressions.push_back(entry.value.get());
                for (const auto& designator : entry.designators) {
                    add(64); name(designator.member); expressions.push_back(designator.index.get());
                    if (designator.member_fresh) {
                        add(64); name(designator.member_fresh->prefix); name(designator.member_fresh->source_unit);
                    }
                }
            }
            for (const auto& relocation : expression->object_relocations) {
                add(96); pending.push_back(relocation.type);
                if (const auto* label = std::get_if<LabelAddressConstant>(&relocation.address))
                    add(label->global_name.size());
            }
            for (const auto& fragment : expression->quote_fragments)
                for (const auto& token : fragment) {
                    add(meta_token_storage_bytes); name(token.text);
                    add(origin_binding_storage(token.origin));
                    if (token.splice) add(syntax_node_storage(*token.splice, maximum));
                }
            continue;
        }
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        add(264); name(next->generic_name); name(next->nominal_name);
        for (const auto& error : next->captured_errors) { add(64); name(error.message); }
        if (next->captured_tag_errors)
            for (const auto& error : *next->captured_tag_errors) { add(64); name(error.message); }
        if (next->nominal_identity) {
            add(128); name(next->nominal_identity->source_unit);
            name(next->nominal_identity->instance_key);
        }
        pending.push_back(next->pointee);
        pending.push_back(next->element);
        expressions.push_back(next->array_bound.get());
        expressions.push_back(next->vector_bound.get());
        if (next->function) {
            add(96); name(next->function->abi);
            if (next->function->result_location) name(*next->function->result_location);
            if (next->function->stack_cleanup) name(*next->function->stack_cleanup);
            for (const auto& clobber : next->function->clobbers) name(clobber);
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                add(160); name(parameter.name); add(value_binding_storage(parameter.binding, {}));
                if (parameter.location_name) name(*parameter.location_name);
                pending.push_back(parameter.type);
                pending.push_back(parameter.declared_array_type);
            }
        }
    }
}

} // namespace

void Parser::remember_fragment_name(std::string_view spelling, std::string_view destination,
    SourceLocation location, FragmentNameDomain domain, ValueBinding value) {
    if (preparing_header_ || fragment_namespaces_.empty()) return;
    const auto origin = token_origin(location);
    if (!origin.context || !origin.identity.source_unit) return;
    auto scope = std::find_if(fragment_namespaces_.rbegin(), fragment_namespaces_.rend(),
        [&](const auto& entry) { return entry.draft->name == active_namespace_; });
    if (scope == fragment_namespaces_.rend()) return;
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    if (execution && !execution->work(location, scope->draft.unique() ? 1 : scope->draft->entries + 1)) {
        public_tree_failed_ = true;
        return;
    }
    if (!scope->draft.unique()) scope->draft = std::make_shared<FragmentNamespaceTable>(*scope->draft);
    auto& table = domain == FragmentNameDomain::Tag ? scope->draft->tags
        : domain == FragmentNameDomain::Namespace ? scope->draft->namespaces : scope->draft->ordinary;
    auto& entries = table[NameKey(spelling, location)];
    const auto same_stream = [&](const FragmentNamespaceName& entry) {
        return entry.stream.source_unit == origin.identity.source_unit &&
            entry.stream.expansion == origin.identity.expansion &&
            entry.source_namespace == origin.context->name_space;
    };
    if (std::any_of(entries.begin(), entries.end(), same_stream)) return;
    entries.push_back({origin.identity, origin.context->name_space, std::string(destination), value});
    ++scope->draft->entries;
    scope->draft->storage += 192 + spelling.size() + origin.context->name_space.size() +
        destination.size() + value_binding_storage(value, {}) +
        (origin.fresh ? 64 + origin.fresh->prefix.size() + origin.fresh->source_unit.size() : 0);
}

void Parser::open_fragment_namespace(std::string_view spelling, const std::string& destination,
    SourceLocation location) {
    const auto origin = token_origin(location);
    const auto source_namespace = origin.context ? origin.context->name_space : active_namespace_;
    const NameKey key(spelling, location);
    std::shared_ptr<const FragmentNamespaceIdentity> identity;
    // A rejected public-capture alternative must not complete a live scope.
    if (!parsing_public_fragment_) {
        for (const auto& placement : fragment_namespace_placements_) {
            if (syntax_ && !syntax_->execution()->work(location)) { public_tree_failed_ = true; break; }
            if (placement.name == key && placement.destination == destination &&
                placement.source_namespace == source_namespace &&
                placement.stream.source_unit == origin.identity.source_unit &&
                placement.stream.expansion == origin.identity.expansion) {
                identity = placement.identity;
                break;
            }
        }
    }
    if (!identity) {
        identity = std::make_shared<const FragmentNamespaceIdentity>();
        fragment_namespace_placements_.push_back({key, origin.identity, source_namespace, destination, identity});
    }
    auto table = identity->completed() ? std::make_shared<FragmentNamespaceTable>(*identity->completed())
        : std::make_shared<FragmentNamespaceTable>();
    table->name = destination;
    fragment_namespaces_.push_back({std::move(table), std::move(identity)});
}

std::shared_ptr<const FragmentNamespaceLookup> Parser::fragment_lookup(
    std::string_view spelling, SourceLocation location) const {
    const auto origin = token_origin(location);
    if (origin.fragment_lookup && origin.fragment_lookup->key.spelling == spelling)
        return origin.fragment_lookup;
    // Parsed positive and negative lookup is settled. Reintroduction of its
    // own binder uses the separate declaration-copy remapping machinery.
    if (!origin.context || origin.value_context_captured) return {};
    const auto environment = origin.context->parse_environment;
    const auto inherited = environment ? environment->fragment_context : nullptr;
    if (fragment_namespaces_.empty() && !inherited) return {};
    auto result = std::make_shared<FragmentNamespaceLookup>();
    result->key = NameKey(spelling, location);
    for (auto scope = fragment_namespaces_.rbegin(); scope != fragment_namespaces_.rend(); ++scope) {
        if (syntax_ && !syntax_->execution()->work(location)) return {};
        result->scopes.push_back({scope->draft, scope->identity, origin.identity,
            result->key.context, origin.context->name_space});
    }
    if (inherited)
        for (const auto& scope : inherited->scopes) {
            if (syntax_ && !syntax_->execution()->work(location, result->scopes.size() + 1)) return {};
            const auto duplicate = std::any_of(result->scopes.begin(), result->scopes.end(),
                [&](const auto& candidate) {
                    return candidate.identity == scope.identity && candidate.mark == scope.mark &&
                        candidate.source_namespace == scope.source_namespace &&
                        candidate.stream.source_unit == scope.stream.source_unit &&
                        candidate.stream.expansion == scope.stream.expansion;
                });
            if (!duplicate) result->scopes.push_back(scope);
        }
    return result;
}

std::optional<FragmentNamespaceName> Parser::fragment_name(std::string_view spelling,
    SourceLocation location, FragmentNameDomain domain) const {
    const auto lookup = fragment_lookup(spelling, location);
    if (!lookup) return {};
    if (syntax_ && !syntax_->execution()->work(location, lookup->scopes.size() + 1)) return {};
    return fragment_namespace_name(*lookup, domain);
}

bool Parser::retain_fragment_lookup(MetaToken& token, std::size_t at, std::size_t end) const {
    if (token.kind != TokenKind::Identifier || token.origin.value_context_captured ||
        token.origin.fragment_lookup || (at && tokens_[at - 1].is("::"))) return true;
    // A deferred/opaque unit has not classified its names yet. Retain only
    // its original namespace dependency, including a written qualified name;
    // reparsing must not acquire the receiving parser's namespace frames.
    auto spelling = identifier_binding_name(tokens_[at]);
    for (auto next = at + 1; next + 1 < end && tokens_[next].is("::") &&
            tokens_[next + 1].kind == TokenKind::Identifier; next += 2) {
        if (syntax_ && !syntax_->execution()->work(tokens_[at].location)) return false;
        spelling += "::";
        spelling += identifier_binding_name(tokens_[next + 1]);
    }
    token.origin.fragment_lookup = fragment_lookup(spelling, tokens_[at].location);
    return true;
}

void Parser::bind_generic_type(Type& type, const std::vector<GenericParameter>& parameters) const {
    if (type.kind != Type::Kind::Generic ||
        type.generic_binding.kind != ValueBinding::Kind::Unknown || !type.generic_location.valid()) return;
    const NameKey use(type.generic_name, type.generic_location);
    const auto bind = [&](const std::vector<GenericParameter>& candidates) {
        for (const auto& parameter : candidates) {
            if (parameter.value_type || NameKey(parameter.name, parameter.location) != use) continue;
            type.generic_binding = name_key(parameter).binding;
            type.generic_header_view = false;
            return true;
        }
        return false;
    };
    if (type.generic_header_view) {
        (void)bind(parameters);
        return;
    }
    // A copied use first consults its retained source declarations. Only an
    // unresolved header use may be completed from the current header's list.
    // Substitution subsequently compares declaration identity, not spelling.
    const auto origin = token_origin(type.generic_location);
    if (origin.context && origin.context->parse_environment) {
        const auto& environment = *origin.context->parse_environment;
        if (environment.header_bindings && environment.header_bindings->complete &&
            bind(environment.header_bindings->parameters)) return;
        if (bind(environment.generic_parameters)) return;
    }
    (void)bind(parameters);
}

Parser::ScopePlacementSelection Parser::select_scope_placements(
    const TokenIdentity& block, std::size_t event_base, SourceLocation location) const {
    const auto found = scope_placements_->find(block);
    if (found == scope_placements_->end()) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(location); };
    std::unordered_set<const ScopePlacement*> ancestry;
    for (auto current = scope_placement_; current; current = current->parent) {
        if (!work()) return {};
        ancestry.insert(current.get());
    }
    ScopePlacementSelection result;
    std::size_t best{};
    for (const auto& placement : found->second) {
        if (!work()) return {};
        if (placement->end_event <= event_base) continue;
        std::size_t proximity{};
        for (auto parent = placement; parent; parent = parent->parent) {
            if (!work()) return {};
            if (ancestry.contains(parent.get())) {
                proximity = parent->depth;
                break;
            }
        }
        if (result.closest.empty() || proximity > best) {
            result.closest.clear();
            best = proximity;
        }
        if (proximity == best) result.closest.push_back(placement);
    }
    return result;
}

void Parser::diagnose_lexical_association(const NameKey& key, SourceLocation location) const {
    diagnostics_.error(location, "ambiguous lexical association for '" + key.spelling +
        "': deferred lookup depends on several equally-close copies of its original block");
}

bool Parser::diagnose_retained_scope_ambiguity(std::string_view name, SourceLocation location,
    PublicNameDomain domain) const {
    const auto origin = token_origin(location);
    if (name.find("::") != std::string_view::npos || !origin.context ||
        !origin.context->parse_environment) return false;
    const auto& environment = *origin.context->parse_environment;
    const NameKey key(name, location);
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(location); };
    // Most retained scopes have never been copied. Avoid scanning the complete
    // declaration history for every ordinary type probe in that common case.
    // Only multiple eligible placements (or a saved ambiguity marker) can
    // require this diagnostic; source lookup itself stays with its own service.
    bool possible = !environment.scope_ambiguities.empty();
    for (const auto& block : environment.scope_origins) {
        if (possible) break;
        if (!work()) return true;
        const auto found = scope_placements_->find(block);
        if (found == scope_placements_->end() || found->second.size() < 2) continue;
        unsigned eligible{};
        for (const auto& placement : found->second) {
            if (!work()) return true;
            if (placement->end_event <= environment.scope_event_base) continue;
            if (++eligible == 2) { possible = true; break; }
        }
    }
    if (!possible) return false;
    for (auto depth = environment.scope_origins.size(); depth != 0; --depth) {
        if (!work()) return true;
        const auto captured = depth - 1;
        if (domain == PublicNameDomain::Tag) {
            if (captured < environment.local_tags.size() && environment.local_tags[captured].contains(key))
                return false;
        } else if ((captured < environment.values.size() && environment.values[captured].contains(key)) ||
                   (captured < environment.local_aliases.size() && environment.local_aliases[captured].contains(key)))
            return false;
        for (const auto& ambiguity : environment.scope_ambiguities) {
            if (!work()) return true;
            if (ambiguity.depth != captured || ambiguity.block != environment.scope_origins[captured]) continue;
            const auto& names = domain == PublicNameDomain::Tag ? ambiguity.tags : ambiguity.ordinary;
            if (names.contains(key)) {
                diagnose_lexical_association(key, location);
                return true;
            }
        }
        const auto placements = select_scope_placements(environment.scope_origins[captured],
            environment.scope_event_base, location);
        for (auto at = environment.scope_event_base; at < scope_events_->size(); ++at) {
            if (!work()) return true;
            const auto& event = (*scope_events_)[at];
            if (event.block != environment.scope_origins[captured]) continue;
            if (execution && !execution->work(location, placements.closest.size())) return true;
            if (!placements.contains(event.placement) ||
                !declaration_precedes(event.statement, event.statement_context, origin.identity)) continue;
            const bool supplied = domain == PublicNameDomain::Tag ? event.tags.contains(key)
                : event.values.contains(key) || event.aliases.contains(key);
            if (!supplied) continue;
            if (!placements.ambiguous()) return false;
            diagnose_lexical_association(key, location);
            return true;
        }
    }
    return false;
}

Parser::ForwardTagScope Parser::destination_forward_tag_scope() const {
    const auto& destination = declaration_destination_ && local_tag_scopes_.size() == tag_destination_depth_
        ? *declaration_destination_ : *this;
    ForwardTagScope result;
    if (!destination.scope_origins_.empty()) result.block = destination.scope_origins_.back();
    result.placement = destination.scope_placement_;
    result.generic_owner = destination.generic_tag_owner_;
    result.function_scope = destination.function_scope_;
    if (!result.block.source_unit) result.name_space = destination.active_namespace_;
    return result;
}

Parser::ForwardTagScope Parser::retained_forward_tag_scope(SourceLocation location) const {
    const auto origin = token_origin(location);
    ForwardTagScope result;
    result.name_space = origin.context ? origin.context->name_space : active_namespace_;
    auto event_base = std::size_t{};
    if (origin.context && origin.context->parse_environment) {
        const auto& environment = *origin.context->parse_environment;
        result.generic_owner = environment.generic_tag_owner;
        result.function_scope = environment.function_scope;
        event_base = environment.scope_event_base;
        if (!environment.scope_origins.empty()) result.block = environment.scope_origins.back();
    }
    if (!result.block.source_unit && !restored_scope_origins_.empty()) {
        result.block = restored_scope_origins_.back();
        event_base = restored_scope_event_base_;
    }
    // A previously unparsed quoted body can establish its source blocks here.
    // This is original token containment, never same-spelled destination lookup.
    if (!result.block.source_unit)
        for (auto depth = std::min(scope_origins_.size(), scope_ends_.size()); depth != 0; --depth)
            if (precedes(scope_origins_[depth - 1], origin.identity) &&
                precedes(origin.identity, scope_ends_[depth - 1])) {
                result.block = scope_origins_[depth - 1];
                break;
            }
    // Generated tokens can retain a helper's pre-fragment definition context.
    // Recover only their own original lexical containment, not the receiving
    // block. This also retains the inner block after projection drops its root.
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    for (const auto& [block, placements] : *scope_placements_) {
        if (execution && !execution->work(location)) break;
        if (placements.empty() || !precedes(block, origin.identity) ||
            !precedes(origin.identity, placements.front()->source_end)) continue;
        if (!result.block.source_unit || result.block.source_unit != origin.identity.source_unit ||
            result.block.expansion != origin.identity.expansion || precedes(result.block, block))
            result.block = block;
    }
    if (result.block.source_unit) {
        const auto selected = select_scope_placements(result.block, event_base, location);
        if (selected.closest.size() == 1) {
            result.placement = selected.closest.front();
            result.generic_owner = result.placement->generic_owner;
            result.function_scope = result.placement->function_scope;
        }
        result.name_space.clear();
    }
    return result;
}

TypePtr Parser::declare_retained_implicit_tag(std::string_view name, SourceLocation location, bool is_union) {
    auto scope = retained_forward_tag_scope(location);
    if (name.find("::") != std::string_view::npos) {
        scope = {};
    }
    const NameKey key(name, location);
    if (const auto found = forward_tags_->scopes.find(scope); found != forward_tags_->scopes.end())
        if (const auto tag = found->second.find(key); tag != found->second.end())
            return copy_type(tag->second);
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto origin = token_origin(location);
    const auto source_unit = location.file ? location.file->source_unit_at(location.line) : std::string{};
    const auto storage = 256 + key.spelling.size() + name.size() + scope.name_space.size() + source_unit.size();
    if (execution) {
        if (!execution->work(location)) return {};
        const auto maximum = std::min(execution->limits().bytes, execution->limits().memory);
        if (storage > maximum - std::min(maximum, forward_tags_->storage)) {
            execution->tree_limit_error(location);
            public_tree_failed_ = true;
            return {};
        }
    }
    const auto canonical = !scope.block.source_unit && name.find("::") == std::string_view::npos &&
        !scope.name_space.empty() ? scope.name_space + "::" + std::string(name) : std::string(name);
    auto type = record_type(canonical, is_union);
    if (scope.block.source_unit) {
        type->nominal_identity = std::make_shared<const NominalTypeIdentity>(NominalTypeIdentity{
            origin.identity, source_unit, ++*nominal_occurrence_, scope.generic_owner, {}, scope.function_scope});
    } else record_types_.try_emplace(type->nominal_name, RecordTag{is_union, false});
    forward_tags_->storage += storage;
    forward_tags_->scopes[scope].emplace(key, copy_type(type));
    return type;
}

Parser::RetainedTypeName Parser::retained_type_name(std::string_view name, SourceLocation location,
    PublicNameDomain domain) const {
    const auto origin = token_origin(location);
    if (!origin.value_context_captured || name.find("::") != std::string_view::npos ||
        !origin.context || !origin.context->parse_environment) return {};
    const auto& environment = *origin.context->parse_environment;
    const NameKey key(name, location);
    using Kind = RetainedTypeName::Kind;
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(location); };
    const auto ambiguous = [&]() -> RetainedTypeName {
        diagnose_lexical_association(key, location);
        return {Kind::Ambiguous, {}, {}};
    };
    for (auto depth = environment.scope_origins.size(); depth != 0; --depth) {
        if (!work()) return {Kind::Ambiguous, {}, {}};
        const auto captured = depth - 1;
        if (domain == PublicNameDomain::Tag) {
            if (captured < environment.local_tags.size())
                if (const auto found = environment.local_tags[captured].find(key);
                    found != environment.local_tags[captured].end())
                    return {Kind::Tag, {}, found->second.type};
        } else {
            if (captured < environment.local_aliases.size())
                if (const auto found = environment.local_aliases[captured].find(key);
                    found != environment.local_aliases[captured].end())
                    return {Kind::Alias, found->second, {}};
            if (captured < environment.values.size() && environment.values[captured].contains(key))
                return {Kind::Value, {}, {}};
        }
        for (const auto& ambiguity : environment.scope_ambiguities) {
            if (!work()) return {Kind::Ambiguous, {}, {}};
            if (ambiguity.depth != captured || ambiguity.block != environment.scope_origins[captured]) continue;
            const auto& names = domain == PublicNameDomain::Tag ? ambiguity.tags : ambiguity.ordinary;
            if (names.contains(key)) return ambiguous();
        }
        const auto placements = select_scope_placements(environment.scope_origins[captured],
            environment.scope_event_base, location);
        TypePtr tag;
        for (const auto& placement : placements.closest) {
            if (!work()) return {Kind::Ambiguous, {}, {}};
            const auto first = std::max(environment.scope_event_base, placement->first_event);
            const auto end = std::min(scope_events_->size(), placement->end_event);
            for (auto at = first; at < end; ++at) {
                if (!work()) return {Kind::Ambiguous, {}, {}};
                const auto& event = (*scope_events_)[at];
                if (event.placement != placement || event.block != environment.scope_origins[captured] ||
                    !declaration_precedes(event.statement, event.statement_context, origin.identity)) continue;
                if (domain == PublicNameDomain::Tag) {
                    if (const auto found = event.tags.find(key); found != event.tags.end()) {
                        if (placements.ambiguous()) return ambiguous();
                        tag = found->second.type;
                    }
                } else {
                    if (const auto found = event.aliases.find(key); found != event.aliases.end()) {
                        if (placements.ambiguous()) return ambiguous();
                        return {Kind::Alias, found->second, {}};
                    }
                    if (event.values.contains(key)) {
                        if (placements.ambiguous()) return ambiguous();
                        return {Kind::Value, {}, {}};
                    }
                }
            }
        }
        if (tag) return {Kind::Tag, {}, std::move(tag)};
    }
    return {};
}

bool Parser::retained_type_binding_visible(std::size_t depth, const RetainedTypeName& binding,
    SourceLocation location) const {
    const auto origin = token_origin(location);
    if (!origin.value_context_captured || !origin.context || !origin.context->parse_environment) return true;
    if (depth >= scope_origins_.size()) return false;
    const auto& environment = *origin.context->parse_environment;
    const auto& block = scope_origins_[depth];
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(location); };
    // A wholly deferred generic header has no captured tag binding yet. Its
    // completed, header-local tag can classify the original body's use, just
    // like a parameter supplied by that header. Owner and original header
    // region are both required: a same-spelled destination header is not enough.
    if (binding.kind == RetainedTypeName::Kind::Tag && binding.tag && binding.tag->nominal_identity &&
        generic_header_depth_ && depth + 1 == *generic_header_depth_ &&
        binding.tag->nominal_identity->generic_owner == generic_tag_owner_ &&
        origin.deferred_parameter_region) {
        const auto& region = *origin.deferred_parameter_region;
        const auto& declaration = binding.tag->nominal_identity->declaration;
        if (precedes(region.first, declaration) && precedes(declaration, region.end)) return true;
    }
    bool captured = false;
    for (const auto& original : environment.scope_origins) {
        if (!work()) return false;
        if (original == block) { captured = true; break; }
    }
    bool restored = false;
    for (const auto& original : restored_scope_origins_) {
        if (!work()) return false;
        if (original == block) { restored = true; captured = true; break; }
    }
    if (!captured && (depth >= scope_ends_.size() || !precedes(block, origin.identity) ||
                     !precedes(origin.identity, scope_ends_[depth]))) return false;
    const auto placements = select_scope_placements(block,
        restored ? restored_scope_event_base_ : environment.scope_event_base, location);
    if (placements.ambiguous()) return false;
    for (const auto& placement : placements.closest) {
        const auto first = std::max(placement->first_event,
            restored ? restored_scope_event_base_ : environment.scope_event_base);
        const auto end = std::min(placement->end_event, scope_events_->size());
        for (auto at = first; at < end; ++at) {
            if (!work()) return false;
            const auto& event = (*scope_events_)[at];
            if (event.placement != placement || event.block != block) continue;
            bool supplies = false;
            if (binding.kind == RetainedTypeName::Kind::Alias) {
                for (const auto& [name, definition] : event.aliases) {
                    if (!work()) return false;
                    (void)name;
                    if (definition == binding.alias) { supplies = true; break; }
                }
            } else if (binding.kind == RetainedTypeName::Kind::Tag) {
                for (const auto& [name, tag] : event.tags) {
                    if (!work()) return false;
                    (void)name;
                    if (binding.tag && tag.type && tag.type->nominal_key() == binding.tag->nominal_key()) {
                        supplies = true; break;
                    }
                }
            }
            if (supplies && declaration_precedes(event.statement, event.statement_context, origin.identity, block))
                return true;
        }
    }
    // A whole deferred body may establish an inner source scope for the first
    // time. A textual macro can also supply an alias/tag inside an unfinished
    // declaration, before its statement event exists. In either case require
    // this placement's actual declaring token and original declaration order.
    for (auto at = std::min(index_, tokens_.size()); at != 0; --at) {
        if (!work()) return false;
        const auto& token = tokens_[at - 1];
        bool declares = false;
        if (binding.kind == RetainedTypeName::Kind::Alias)
            declares = token.alias_binding && token.alias_binding->role == AliasBinding::Role::Declaration &&
                token.alias_binding->definition == binding.alias;
        else if (binding.kind == RetainedTypeName::Kind::Tag)
            declares = token.tag_binding && token.tag_binding->role == TagBinding::Role::Declaration &&
                binding.tag && token.tag_binding->type == binding.tag->nominal_key();
        if (!declares) continue;
        const auto declaration = token_origin(token.location);
        return declaration_precedes(declaration.identity, declaration.context, origin.identity, block);
    }
    return false;
}

bool Parser::diagnose_scope_ambiguity(std::size_t depth, const NameKey& key,
    PublicNameDomain domain, SourceLocation location) const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    for (const auto& ambiguity : scope_ambiguities_) {
        if (execution && !execution->work(location)) return true;
        if (ambiguity.depth != depth || depth >= scope_origins_.size() ||
            ambiguity.block != scope_origins_[depth]) continue;
        const auto& names = domain == PublicNameDomain::Tag ? ambiguity.tags : ambiguity.ordinary;
        if (!names.contains(key)) continue;
        diagnose_lexical_association(key, location);
        return true;
    }
    return false;
}

ValueBinding Parser::retained_value_binding(std::string_view name, SourceLocation location) const {
    const auto origin = token_origin(location);
    if (!origin.context || !origin.context->parse_environment || name.find("::") != std::string_view::npos)
        return {};
    const auto& environment = *origin.context->parse_environment;
    const NameKey key(name, location);
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(location); };
    // A captured parameter use keeps its original prototype lookup. A wholly
    // deferred header may acquire an earlier binder only inside that original
    // parameter region, never from the destination's same-spelled parameter.
    for (auto scope = prototype_scopes_.rbegin(); scope != prototype_scopes_.rend(); ++scope) {
        if (!work()) return {};
        const auto found = scope->values.find(key);
        if (found == scope->values.end()) continue;
        auto region = scope->region;
        if (origin.deferred_parameter_region &&
            precedes(origin.deferred_parameter_region->first, region.first) &&
            precedes(region.first, origin.deferred_parameter_region->end))
            region.end = origin.deferred_parameter_region->end;
        auto use = origin;
        std::unordered_set<const SyntaxContext*> seen;
        while (!(precedes(region.first, use.identity) && precedes(use.identity, region.end))) {
            if (!work()) return {};
            if (!use.identity.expansion.value || !use.context ||
                !seen.insert(use.context.get()).second) break;
            use = token_origin(use.context->invocation);
        }
        if (!precedes(region.first, use.identity) || !precedes(use.identity, region.end)) continue;
        const auto declaration = token_origin(found->second.location);
        if (declaration_precedes(declaration.identity, declaration.context, origin.identity,
                scope->region.first)) return found->second.binding;
    }
    for (auto scope = environment.prototypes.rbegin(); scope != environment.prototypes.rend(); ++scope) {
        if (!work()) return {};
        if (const auto found = scope->values.find(key); found != scope->values.end())
            return found->second.binding;
    }
    // A deferred whole body has not classified its inner scopes yet. Reparse
    // may establish a closer declaration in that original token region. Such
    // a binder must follow its own original opening token and precede this use;
    // a destination parameter or a surrounding quoted block does not qualify.
    for (auto depth = std::min(local_scopes_.size(), scope_origins_.size()); depth != 0; --depth) {
        if (!work()) return {};
        const auto& block = scope_origins_[depth - 1];
        bool captured_scope = false;
        for (std::size_t captured = 0; captured < environment.scope_origins.size(); ++captured) {
            if (!work()) return {};
            if (environment.scope_origins[captured] != block || captured >= environment.values.size()) continue;
            captured_scope = true;
            if (const auto found = environment.values[captured].find(key);
                found != environment.values[captured].end()) return found->second;
        }
        const auto found = local_scopes_[depth - 1].find(key);
        // Explicitly captured scope identity already establishes ownership.
        // A newly discovered inner scope additionally needs a closing boundary
        // to exclude same-stream declarations from an unrelated sibling block.
        if (found == local_scopes_[depth - 1].end() || depth > scope_ends_.size() ||
            !precedes(block, origin.identity) ||
            (!captured_scope && !precedes(origin.identity, scope_ends_[depth - 1]))) continue;
        if (precedes(block, found->second.declaration) &&
            precedes(found->second.declaration, origin.identity))
            return found->second;
        // A nested macro may construct the declaration in another token
        // stream. Its original-block event can establish order via invocation
        // ancestry, but only for this placement's actual local binding. Events
        // from an earlier copy of the same block must not substitute a binder.
        for (auto at = environment.scope_event_base; at < scope_events_->size(); ++at) {
            if (!work()) return {};
            const auto& event = (*scope_events_)[at];
            if (event.block != block) continue;
            const auto supplied = event.values.find(key);
            if (supplied != event.values.end() && supplied->second == found->second &&
                declaration_precedes(event.statement, event.statement_context,
                                     origin.identity, block)) return found->second;
        }
        // Textual output may supply just a declarator, with its initializer or
        // later declarators supplied by the original stream. No statement event
        // exists yet. Consult the actual declaring token of this placement,
        // never a same-spelled token or a reference to a sibling placement.
        for (auto at = std::min(index_, tokens_.size()); at != 0; --at) {
            if (!work()) return {};
            const auto& token = tokens_[at - 1];
            if (token.value_binding != found->second) continue;
            const auto declaration = token_origin(token.location);
            if (declaration.identity != found->second.declaration) continue;
            if (declaration_precedes(declaration.identity, declaration.context,
                                     origin.identity, block)) return found->second;
            break;
        }
    }
    for (auto depth = environment.values.size(); depth != 0; --depth) {
        if (!work()) return {};
        const auto& scope = environment.values[depth - 1];
        if (const auto found = scope.find(key); found != scope.end()) return found->second;
        // Earlier surviving declarations can complete an opaque source block;
        // events from a different destination block cannot supply its names.
        if (depth > environment.scope_origins.size()) continue;
        for (const auto& ambiguity : environment.scope_ambiguities) {
            if (!work()) return {};
            if (ambiguity.depth == depth - 1 && ambiguity.block == environment.scope_origins[depth - 1] &&
                ambiguity.ordinary.contains(key)) {
                diagnose_lexical_association(key, location);
                return {};
            }
        }
        const auto placements = select_scope_placements(environment.scope_origins[depth - 1],
            environment.scope_event_base, location);
        auto receiving = scope_origins_.size();
        for (auto live = scope_origins_.size(); live != 0; --live) {
            if (!work()) return {};
            if (scope_origins_[live - 1] == environment.scope_origins[depth - 1]) {
                receiving = live - 1;
                break;
            }
        }
        for (auto at = environment.scope_event_base; at < scope_events_->size(); ++at) {
            if (!work()) return {};
            const auto& event = (*scope_events_)[at];
            if (execution && !execution->work(location, placements.closest.size())) return {};
            if (!placements.contains(event.placement)) continue;
            if (event.block != environment.scope_origins[depth - 1] ||
                !declaration_precedes(event.statement, event.statement_context, origin.identity)) continue;
            if (placements.ambiguous() && (event.values.contains(key) || event.aliases.contains(key))) {
                diagnose_lexical_association(key, location);
                return {};
            }
            if (const auto found = event.values.find(key); found != event.values.end()) {
                // An active copy may have discarded the original declaration.
                // Do not recover a sibling copy's value (including enumerators,
                // which have no runtime visibility check to catch the mistake).
                if (receiving != scope_origins_.size()) {
                    if (receiving >= local_scopes_.size()) continue;
                    const auto current = local_scopes_[receiving].find(key);
                    if (current == local_scopes_[receiving].end() || current->second != found->second) continue;
                }
                return found->second;
            }
        }
    }
    for (const auto& parameter : environment.parameters) {
        if (!work()) return {};
        if (NameKey(parameter.name, parameter.location) == key) return name_key(parameter).binding;
    }
    for (const auto& binding : environment.variadic_bindings) {
        if (!work()) return {};
        if (NameKey(binding.name, binding.location) == key) return name_key(binding).binding;
    }
    const auto& generics = environment.header_bindings && environment.header_bindings->complete
        ? environment.header_bindings->parameters : environment.generic_parameters;
    for (const auto& parameter : generics) {
        if (!work()) return {};
        if (parameter.value_type && NameKey(parameter.name, parameter.location) == key)
            return name_key(parameter).binding;
    }
    if (origin.deferred_parameter_region && active_function_) {
        const auto& region = *origin.deferred_parameter_region;
        const auto in_original_header = [&](SourceLocation declaration) {
            std::unordered_set<const SyntaxContext*> seen;
            while (declaration.valid()) {
                if (!work()) return false;
                const auto supplied = token_origin(declaration);
                if (precedes(region.first, supplied.identity) && precedes(supplied.identity, region.end))
                    return true;
                // A constructed binder belongs to the header containing its
                // invocation, including nested macro/helper construction. A
                // copied source binder keeps its own identity instead; spans
                // alone cannot distinguish those cases after relocation.
                if (!supplied.identity.expansion.value || !supplied.context ||
                    !seen.insert(supplied.context.get()).second) break;
                declaration = supplied.context->invocation;
            }
            return false;
        };
        for (const auto& parameter : active_function_->parameters) {
            if (!work()) return {};
            if (NameKey(parameter.name, parameter.location) == key &&
                in_original_header(parameter.location)) return name_key(parameter).binding;
        }
        for (const auto& attribute : active_function_->attributes)
            for (const auto& binding : attribute.variadic_bindings) {
                if (!work()) return {};
                if (NameKey(binding.name, binding.location) == key &&
                    in_original_header(binding.location)) return name_key(binding).binding;
            }
        for (const auto& parameter : active_function_->generic_parameters) {
            if (!work()) return {};
            if (parameter.value_type && NameKey(parameter.name, parameter.location) == key &&
                in_original_header(parameter.location)) return name_key(parameter).binding;
        }
    }
    // A constructor can deliberately supply one identifier to both a binder
    // and its uses, or construct a complete declaration/use pair in one fresh
    // expansion. This is not an unrelated destination binding. Existing source
    // bindings above still take precedence over such a role change.
    for (auto scope = local_scopes_.rbegin(); scope != local_scopes_.rend(); ++scope) {
        if (!work()) return {};
        const auto found = scope->find(key);
        if (found == scope->end()) continue;
        const auto& declaration = found->second.declaration;
        if (declaration == origin.identity ||
            (origin.identity.expansion.value && declaration.expansion == origin.identity.expansion &&
             precedes(declaration, origin.identity))) return found->second;
        break;
    }
    ValueBinding result;
    if (origin.value_context_captured) result.kind = ValueBinding::Kind::Nonlocal;
    return result;
}

void Parser::remember_label_binding(Statement& statement, std::size_t token_index) {
    auto& token = tokens_.at(token_index);
    const auto origin = token_origin(token.location);
    auto binding = token.label_binding;
    if (binding.kind == LabelBinding::Kind::Unknown && !binding.scope)
        binding = origin.label_binding;
    if (!binding.scope && origin.context && origin.context->parse_environment)
        binding.scope = origin.context->parse_environment->function_scope;
    if (!binding.scope) binding.scope = function_scope_;
    if (binding.kind != LabelBinding::Kind::Definition) {
        binding.kind = LabelBinding::Kind::Definition;
        binding.declaration = origin.identity;
    }
    token.label_binding = binding;
    statement.label_binding = std::move(binding);
}

void Parser::bind_label_references(Statement& root, bool complete_function) {
    const auto execution = recording_public_tree_ && syntax_ ? syntax_->execution() : nullptr;
    const auto charge = [&] {
        if (!execution || execution->work(root.location)) return true;
        public_tree_failed_ = true;
        return false;
    };
    NameMap<std::vector<LabelBinding>> definitions;
    std::vector<Statement*> statements{&root};
    std::vector<Expr*> expressions;
    while (!statements.empty()) {
        auto* statement = statements.back();
        statements.pop_back();
        if (!statement) continue;
        if (!charge()) return;
        if (statement->kind == Statement::Kind::Label)
            definitions[NameKey(statement->label_name, statement->label_location)]
                .push_back(statement->label_binding);
        expressions.push_back(statement->expression.get());
        expressions.push_back(statement->condition.get());
        expressions.push_back(statement->increment.get());
        if (statement->declaration) {
            expressions.push_back(statement->declaration->initializer.get());
            expressions.push_back(statement->declaration->dynamic_array_bound.get());
        }
        statements.push_back(statement->first.get());
        statements.push_back(statement->second.get());
        for (auto& child : statement->statements) statements.push_back(child.get());
    }
    if (complete_function && function_scope_ && !function_scope_->labels_) {
        auto labels = std::make_shared<FunctionScopeIdentity::Labels>();
        // Ordinary runtime source is not an evaluator allocation. If retained
        // by a later meta operation, its complete table is charged there.
        const auto maximum = execution
            ? std::min(execution->limits().bytes, execution->limits().memory)
            : std::numeric_limits<std::uint64_t>::max();
        for (const auto& [name, candidates] : definitions) {
            for (const auto& binding : candidates) {
                if (binding.scope != function_scope_) continue;
                const auto storage = 128 + name.spelling.size();
                if (storage > maximum - std::min(labels->storage, maximum)) {
                    public_tree_failed_ = true;
                    if (execution) execution->tree_limit_error(root.location);
                    return;
                }
                if (!charge()) return;
                // Duplicate declarations are diagnosed by HIR, not speculative
                // capture recognition. They cannot create a second namespace.
                if (labels->declarations.emplace(name, binding.declaration).second)
                    labels->storage += storage;
            }
        }
        function_scope_->labels_ = std::move(labels);
    }
    if (definitions.empty()) return;
    const auto bind = [&](LabelBinding& binding, const NameKey& name) {
        binding = resolved_label_binding(binding, name);
        if (binding.kind != LabelBinding::Kind::Unknown) return;
        const auto found = definitions.find(name);
        if (found == definitions.end()) return;
        const auto declaration = std::find_if(found->second.begin(), found->second.end(),
            [&](const LabelBinding& candidate) { return candidate.scope == binding.scope; });
        if (declaration == found->second.end()) return;
        binding = *declaration;
        binding.kind = LabelBinding::Kind::Reference;
    };
    // Publish every interpreted token, including uses inside retained immutable
    // type bounds that are not direct statement expression children. Unparsed
    // quote/raw tokens have no function scope and must not be rebound here.
    if (recording_public_tree_) {
        for (auto& token : tokens_) {
            if (!charge()) return;
            if (token.kind == TokenKind::Identifier && token.label_binding.scope)
                bind(token.label_binding, NameKey(identifier_binding_name(token), token.location));
        }
    }
    while (!expressions.empty()) {
        auto* expression = expressions.back();
        expressions.pop_back();
        if (!expression) continue;
        if (!charge()) return;
        if (expression->kind == Expr::Kind::Name && expression->name_context &&
            expression->text.find("::") == std::string::npos &&
            expression->name_context->label_binding.kind == LabelBinding::Kind::Unknown) {
            auto binding = expression->name_context->label_binding;
            bind(binding, NameKey(expression->text, expression->location));
            if (binding.kind == LabelBinding::Kind::Reference) {
                auto context = std::make_shared<NameLookupContext>(*expression->name_context);
                context->label_binding = std::move(binding);
                expression->name_context = std::move(context);
            }
        }
        expressions.push_back(expression->left.get());
        expressions.push_back(expression->right.get());
        expressions.push_back(expression->third.get());
        for (auto& argument : expression->arguments) expressions.push_back(argument.get());
        for (auto& argument : expression->generic_arguments) expressions.push_back(argument.value.get());
        for (auto& entry : expression->initializer_entries) {
            expressions.push_back(entry.value.get());
            for (auto& designator : entry.designators) expressions.push_back(designator.index.get());
        }
    }
}

AliasDefinitionPtr Parser::make_alias_definition(const TypePtr& type) const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto maximum = execution ? std::min(execution->limits().bytes, execution->limits().memory)
                                   : std::numeric_limits<std::uint64_t>::max();
    std::uint64_t storage = 32;
    std::uint64_t work = 1;
    bool over_budget = storage > maximum;
    const auto add = [&](std::uint64_t bytes) {
        ++work;
        if (bytes > maximum - std::min(storage, maximum)) over_budget = true;
        else storage += bytes;
    };
    const auto name = [&](std::string_view text) { add(32); add(text.size()); };
    type_storage(type, add, name, over_budget, maximum);
    if (over_budget || (execution && !execution->work(current().location, work))) {
        if (over_budget) {
            if (execution) execution->tree_limit_error(current().location);
            else diagnostics_.error(current().location, "typedef binding storage exceeds the supported size");
        }
        return {};
    }
    return std::make_shared<const AliasDefinition>(type, storage);
}

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
        add(128); name(parameter.name); add(value_binding_storage(parameter.binding, {}));
        type_storage(parameter.value_type, add, name, over_budget, maximum);
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
    auto storage = environment.storage + (environment.header_bindings ? environment.header_bindings->storage : 0) +
        function_label_storage(environment.function_scope) + fragment_lookup_storage(environment.fragment_context);
    for (const auto& scope : environment.fragment_namespaces)
        if (scope.identity->completed() && scope.identity->completed() != scope.draft)
            storage += scope.identity->completed()->storage + scope.identity->completed()->name.size();
    return storage;
}

bool syntax_environment_same_lookup(const SyntaxParseEnvironment& left, const SyntaxParseEnvironment& right) {
    if (&left == &right) return true;
    if (left.identity != right.identity) return false;
    // Only namespace refinement copies an existing snapshot. It changes these
    // classifier tables and fragment provenance, never the lexical/header state.
    if (left.aliases != right.aliases || left.functions != right.functions ||
        left.records != right.records || left.enumerations != right.enumerations) return false;
    if (left.ordinary_values != right.ordinary_values &&
        (!left.ordinary_values || !right.ordinary_values ||
         left.ordinary_values->entries != right.ordinary_values->entries)) return false;
    if (left.fragment_context == right.fragment_context) return true;
    if (!left.fragment_context || !right.fragment_context ||
        left.fragment_context->key != right.fragment_context->key ||
        left.fragment_context->scopes.size() != right.fragment_context->scopes.size()) return false;
    return std::equal(left.fragment_context->scopes.begin(), left.fragment_context->scopes.end(),
        right.fragment_context->scopes.begin(), [](const auto& a, const auto& b) {
            return a.preceding == b.preceding && a.identity == b.identity && a.stream == b.stream &&
                a.mark == b.mark && a.source_namespace == b.source_namespace;
        });
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

std::shared_ptr<const SyntaxParseEnvironment> Parser::snapshot_environment(
    std::shared_ptr<const FragmentNamespaceLookup> fragment_context) const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    const auto maximum = std::min(limits.bytes, limits.memory);
    std::uint64_t storage = 296;
    std::uint64_t work = 1;
    bool over_budget = storage > maximum;
    const auto add = [&](std::uint64_t size) {
        ++work;
        if (size > maximum - std::min(storage, maximum)) over_budget = true;
        else storage += size;
    };
    const auto name = [&](std::string_view text) { add(32); add(text.size()); };
    const auto type = [&](const TypePtr& root) { type_storage(root, add, name, over_budget, maximum); };
    for (const auto& entry : active_generic_types_) name(entry);
    for (const auto& [entry, function] : known_functions_) {
        name(entry); add(128 + function.parameters.size() * 8);
        add(value_binding_storage(function.binding, {}));
    }
    add(known_ordinary_values_->storage);
    for (const auto& scope : local_scopes_) {
        add(32);
        for (const auto& [entry, binding] : scope) {
            name(entry.spelling); add(96);
            add(value_binding_storage(binding, {}));
        }
    }
    for (const auto& scope : prototype_scopes_) {
        add(112);
        for (const auto& [entry, value] : scope.values) {
            name(entry.spelling); add(128);
            add(value_binding_storage(value.binding, {}));
        }
    }
    add(scope_origins_.size() * 40);
    add(scope_ends_.size() * 40);
    add(import_regions_.size() * 80);
    for (const auto& scope : fragment_namespaces_) add(scope.draft->storage + scope.draft->name.size() + 32);
    add(active_import_declarations_.size() * 40);
    for (const auto& scope : local_type_scopes_) {
        add(32);
        for (const auto& [entry, value] : scope) { name(entry.spelling); add(value->storage()); }
    }
    for (const auto& scope : local_tag_scopes_) {
        add(32);
        for (const auto& [entry, tag] : scope) { name(entry.spelling); type(tag.type); add(8); }
    }
    for (const auto& ambiguity : scope_ambiguities_) {
        add(64);
        for (const auto& entry : ambiguity.ordinary) name(entry.spelling);
        for (const auto& entry : ambiguity.tags) name(entry.spelling);
    }
    for (const auto& [entry, value] : type_aliases_) { name(entry); add(value->storage()); }
    for (const auto& [entry, value] : enum_types_) {
        name(entry); add(32);
        if (value.errors)
            for (const auto& error : *value.errors) { add(64); name(error.message); }
    }
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
            add(160); name(parameter.name); type(parameter.type); type(parameter.declared_array_type);
            add(value_binding_storage(parameter.binding, {}));
            if (parameter.location_name) name(*parameter.location_name);
        }
        for (const auto& parameter : active_function_->generic_parameters) {
            add(128); name(parameter.name); type(parameter.value_type);
            add(value_binding_storage(parameter.binding, {}));
        }
        for (const auto& attribute : active_function_->attributes)
            for (const auto& binding : attribute.variadic_bindings) {
                add(128); name(binding.name); name(binding.state); type(binding.type);
                add(value_binding_storage(binding.binding, {}));
            }
    }
    add(8 * public_uncertain_binding_depths_.size() + switch_default_seen_.size());
    if (header_bindings_ && header_bindings_->storage > maximum - std::min(storage, maximum))
        over_budget = true;
    if (fragment_lookup_storage(fragment_context) > maximum - std::min(storage, maximum))
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
    result->import_declarations = active_import_declarations_;
    result->generic_types = active_generic_types_;
    result->functions = known_functions_;
    result->ordinary_values = known_ordinary_values_;
    result->values = local_scopes_;
    result->prototypes = prototype_scopes_;
    result->local_aliases = local_type_scopes_;
    result->local_tags = local_tag_scopes_;
    result->scope_ambiguities = scope_ambiguities_;
    result->nominal_occurrence = nominal_occurrence_;
    result->generic_tag_owner = generic_tag_owner_;
    result->generic_header_depth = generic_header_depth_;
    result->function_scope = function_scope_;
    for (auto& scope : result->local_tags)
        for (auto& [entry, tag] : scope) { (void)entry; tag.type = copy_type(tag.type); }
    result->scope_origins = scope_origins_;
    result->scope_ends = scope_ends_;
    result->import_regions = import_regions_;
    result->fragment_namespaces = fragment_namespaces_;
    result->fragment_context = std::move(fragment_context);
    result->scope_event_base = scope_events_->size();
    result->header_bindings = header_bindings_;
    result->aliases = type_aliases_;
    result->enumerations = enum_types_;
    result->records = record_types_;
    result->function_context = active_function_ != nullptr;
    if (active_function_) {
        result->parameters = active_function_->parameters;
        for (auto& parameter : result->parameters) {
            parameter.type = copy_type(parameter.type);
            parameter.declared_array_type = copy_type(parameter.declared_array_type);
        }
        result->generic_parameters = active_function_->generic_parameters;
        for (auto& parameter : result->generic_parameters)
            parameter.value_type = copy_type(parameter.value_type);
        for (const auto& attribute : active_function_->attributes)
            for (const auto& binding : attribute.variadic_bindings) {
                auto& copied = result->variadic_bindings.emplace_back(binding);
                copied.type = copy_type(binding.type);
            }
    }
    result->uncertain_depths = public_uncertain_binding_depths_;
    result->procedural_body = parsing_procedural_body_;
    result->switch_depth = switch_depth_;
    result->switch_defaults = switch_default_seen_;
    result->storage = storage;
    return result;
}

std::shared_ptr<const SyntaxParseEnvironment> Parser::refine_namespace_environment(
    const SyntaxParseEnvironment& retained,
    std::shared_ptr<const FragmentNamespaceLookup> lookup, SourceLocation location) const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    const auto maximum = std::min(limits.bytes, limits.memory);
    if (namespace_context_cache_depth_)
        for (const auto& [base, cached] : public_namespace_contexts_) {
            if (execution && !execution->work(location, lookup->scopes.size() + 1)) return {};
            if (base != &retained || cached->fragment_context->scopes.size() != lookup->scopes.size()) continue;
            const auto same = std::equal(lookup->scopes.begin(), lookup->scopes.end(),
                cached->fragment_context->scopes.begin(), [](const auto& left, const auto& right) {
                    return left.preceding == right.preceding && left.identity == right.identity &&
                        left.mark == right.mark && left.source_namespace == right.source_namespace &&
                        left.stream.source_unit == right.stream.source_unit &&
                        left.stream.expansion == right.stream.expansion;
                });
            if (same) return cached;
        }
    if (execution && !execution->work(location, retained.functions.size() + retained.aliases.size() +
            retained.records.size() + retained.enumerations.size() + retained.values.size() + 1)) return {};
    auto result = std::make_shared<SyntaxParseEnvironment>(retained);
    result->fragment_context = std::move(lookup);
    auto storage = syntax_environment_storage(*result);
    const auto charge = [&](std::uint64_t amount) {
        if (amount > maximum - std::min(storage, maximum)) {
            if (execution) execution->tree_limit_error(location);
            diagnostics_.error(location, "syntax context byte or memory budget exceeded");
            return false;
        }
        storage += amount;
        result->storage += amount;
        return true;
    };
    if (storage > maximum) { (void)charge(1); return {}; }
    const auto work = [&](std::uint64_t amount = 1) { return !execution || execution->work(location, amount); };
    // Preserve the original lexical locals, imports and free-name classifiers.
    // Only declarations reachable through this fragment's namespace view can
    // enrich its context. Copying all current maps would expose destination
    // declarations to a context that never contained them.
    const auto copy_name = [&](const std::string& name, FragmentNameDomain domain) {
        if (!work()) return false;
        if (domain != FragmentNameDomain::Tag) {
            if (const auto found = type_aliases_.find(name);
                found != type_aliases_.end() && !result->aliases.contains(name)) {
                if (!charge(32 + name.size() + found->second->storage())) return false;
                result->aliases.emplace(name, found->second);
            }
            if (const auto found = known_functions_.find(name);
                found != known_functions_.end() && !result->functions.contains(name)) {
                if (!charge(160 + name.size() + found->second.parameters.size() * 8 +
                        value_binding_storage(found->second.binding, {}))) return false;
                result->functions.emplace(name, found->second);
            }
            if (const auto found = known_ordinary_values_->entries.find(name);
                found != known_ordinary_values_->entries.end() &&
                (!result->ordinary_values || !result->ordinary_values->entries.contains(name))) {
                const auto amount = 128 + name.size();
                if (!charge(amount)) return false;
                if (!result->ordinary_values) result->ordinary_values = std::make_shared<OrdinaryNames>();
                if (!result->ordinary_values.unique()) {
                    if (!work(result->ordinary_values->entries.size() + 1)) return false;
                    result->ordinary_values = std::make_shared<OrdinaryNames>(*result->ordinary_values);
                }
                result->ordinary_values->entries.emplace(name, found->second);
                result->ordinary_values->storage += amount;
            }
        }
        if (domain != FragmentNameDomain::Ordinary) {
            if (const auto found = record_types_.find(name);
                found != record_types_.end() && !result->records.contains(name)) {
                if (!charge(48 + name.size())) return false;
                result->records.emplace(name, found->second);
            }
            if (const auto found = enum_types_.find(name);
                found != enum_types_.end() && !result->enumerations.contains(name)) {
                auto amount = 64 + name.size();
                if (found->second.errors)
                    for (const auto& error : *found->second.errors) amount += 96 + error.message.size();
                if (!charge(amount)) return false;
                result->enumerations.emplace(name, found->second);
            }
        }
        return true;
    };
    const auto copy_namespace = [&](const std::string& prefix) {
        const auto copy_map = [&](const auto& map) {
            for (const auto& [name, value] : map) {
                (void)value;
                if (!work()) return false;
                if (name.starts_with(prefix) && std::string_view(name).substr(prefix.size()).starts_with("::") &&
                    !copy_name(name, FragmentNameDomain::Namespace)) return false;
            }
            return true;
        };
        return copy_map(type_aliases_) && copy_map(known_functions_) && copy_map(known_ordinary_values_->entries) &&
            copy_map(record_types_) && copy_map(enum_types_);
    };
    for (const auto& scope : result->fragment_context->scopes) {
        const auto copy_domain = [&](const auto& names, FragmentNameDomain domain) {
            for (const auto& [key, entries] : names)
                for (const auto& entry : entries) {
                    if (!work()) return false;
                    if (key.context != scope.mark || entry.stream.source_unit != scope.stream.source_unit ||
                        entry.stream.expansion != scope.stream.expansion ||
                        entry.source_namespace != scope.source_namespace) continue;
                    if (!(domain == FragmentNameDomain::Namespace ? copy_namespace(entry.destination)
                            : copy_name(entry.destination, domain))) return false;
                }
            return true;
        };
        if (!copy_domain(scope.preceding->ordinary, FragmentNameDomain::Ordinary) ||
            !copy_domain(scope.preceding->tags, FragmentNameDomain::Tag) ||
            !copy_domain(scope.preceding->namespaces, FragmentNameDomain::Namespace)) return {};
    }
    if (namespace_context_cache_depth_) public_namespace_contexts_.emplace_back(&retained, result);
    return result;
}

std::shared_ptr<const SyntaxContext> Parser::syntax_context(SourceLocation location) const {
    const auto origin = token_origin(location);
    const auto retained = effective_context(location);
    if (origin.context && !retained) return {};
    if (retained && retained->parse_environment && origin.value_context_captured && !recording_public_tree_)
        return retained;
    const auto lookup = origin.fragment_lookup ? origin.fragment_lookup : fragment_lookup({}, location);
    if (retained && retained->parse_environment && !recording_public_tree_ && !lookup)
        return retained;
    const auto environment = retained && retained->parse_environment && !recording_public_tree_
        ? refine_namespace_environment(*retained->parse_environment, lookup, location)
        : snapshot_environment(lookup);
    if (!environment) return {};
    auto context = retained ? std::make_shared<SyntaxContext>(*retained)
                                  : std::make_shared<SyntaxContext>();
    if (!origin.context) {
        context->invocation = location;
        context->name_space = active_namespace_;
        context->imports = active_imports_;
        context->import_declarations = active_import_declarations_;
        if (syntax_) context->syntax_bindings = syntax_->bindings();
    }
    context->parse_environment = environment;
    return context;
}

std::shared_ptr<const SyntaxContext> Parser::effective_context(SourceLocation location,
    const SyntaxParseEnvironment* environment) const {
    const auto origin = token_origin(location);
    const auto& context = origin.context;
    if (!context || origin.lookup_mode == TokenOrigin::LookupMode::Invocation ||
        scope_import_events_->empty()) return context;
    // A retained outer context still permits imports written inside the
    // original generated/copied block. It must not acquire imports merely
    // because its tokens were moved beneath an unrelated destination block.
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    const auto maximum = std::min(limits.bytes, limits.memory);
    auto storage = syntax_context_storage(*context);
    std::vector<std::string> imports;
    std::vector<TokenIdentity> declarations;
    const auto append = [&](const ScopeImportEvent& event) {
        if (execution && !execution->work(location, context->import_declarations.size() + declarations.size() + 1))
            return false;
        if (!event.declaration.source_unit ||
            std::find(context->import_declarations.begin(), context->import_declarations.end(), event.declaration) !=
                context->import_declarations.end() ||
            std::find(declarations.begin(), declarations.end(), event.declaration) != declarations.end()) return true;
        if (40 > maximum - std::min(storage, maximum)) {
            if (execution) execution->tree_limit_error(location);
            diagnostics_.error(location, "syntax context byte or memory budget exceeded");
            return false;
        }
        storage += 40;
        declarations.push_back(event.declaration);
        for (const auto& imported : event.imports) {
            const auto amount = 32 + imported.size();
            if (amount > maximum - std::min(storage, maximum)) {
                if (execution) execution->tree_limit_error(location);
                diagnostics_.error(location, "syntax context byte or memory budget exceeded");
                return false;
            }
            storage += amount;
            imports.push_back(imported);
        }
        return true;
    };
    const auto region = [&](const TokenIdentity& block, const TokenIdentity& end) {
        if (execution && !execution->work(location)) return false;
        auto position = origin.identity;
        auto enclosing = origin.context;
        unsigned depth = 0;
        while (!precedes(block, position) || !precedes(position, end)) {
            if (!enclosing || !enclosing->invocation.valid()) return true;
            if (depth++ >= limits.depth) {
                if (execution) execution->tree_limit_error(location);
                diagnostics_.error(location, "syntax import provenance depth exceeded");
                return false;
            }
            if (execution && !execution->work(location)) return false;
            const auto invocation = token_origin(enclosing->invocation);
            if (invocation.identity == position) return true;
            position = invocation.identity;
            enclosing = invocation.context;
        }
        for (const auto& event : *scope_import_events_) {
            if (execution && !execution->work(location)) return false;
            if (event.scope != ScopeImportEvent::Scope::Braced || event.block != block ||
                !precedes(event.declaration, origin.identity)) continue;
            // The namespace may predate the expansion supplying both import
            // and use. Only containment follows invocation ancestry; their
            // order must still be in the same original token stream. Ambient
            // caller imports therefore cannot capture quoted identifiers.
            if (block.source_unit == event.declaration.source_unit && block.expansion == event.declaration.expansion &&
                !precedes(block, event.declaration)) continue;
            if (!append(event)) return false;
        }
        return true;
    };
    const auto& starts = environment ? environment->scope_origins : scope_origins_;
    const auto& ends = environment ? environment->scope_ends : scope_ends_;
    for (auto depth = std::min(starts.size(), ends.size()); depth != 0; --depth)
        if (!region(starts[depth - 1], ends[depth - 1])) return {};
    const auto& external = environment ? environment->import_regions : import_regions_;
    for (auto at = external.rbegin(); at != external.rend(); ++at)
        if (!region(at->first, at->end)) return {};
    for (const auto& event : *scope_import_events_) {
        if (execution && !execution->work(location)) return {};
        // A file import belongs to its original source unit/expansion, not
        // every token subsequently relocated into the destination file.
        if (event.scope == ScopeImportEvent::Scope::File && precedes(event.declaration, origin.identity))
            if (!append(event)) return {};
    }
    if (imports.empty()) return context;
    auto result = std::make_shared<SyntaxContext>(*context);
    for (const auto& imported : result->imports) {
        if (execution && !execution->work(location, imports.size())) return {};
        if (std::find(imports.begin(), imports.end(), imported) == imports.end())
            imports.push_back(imported);
    }
    result->imports = std::move(imports);
    result->import_declarations.insert(result->import_declarations.end(), declarations.begin(), declarations.end());
    return result;
}

void Parser::restore_environment(const SyntaxParseEnvironment& environment,
                                 const SyntaxContext& context) {
    address_bits_ = environment.address_bits;
    if (environment.syntax)
        syntax_.emplace(*environment.syntax, environment.execution.lock());
    active_namespace_ = context.name_space;
    active_imports_ = context.imports;
    active_import_declarations_ = context.import_declarations;
    current_scope_imports_ = environment.scope_imports;
    active_generic_types_ = environment.generic_types;
    known_functions_ = environment.functions;
    known_ordinary_values_ = environment.ordinary_values
        ? environment.ordinary_values : std::make_shared<OrdinaryNames>();
    local_scopes_ = environment.values;
    prototype_scopes_ = environment.prototypes;
    local_type_scopes_ = environment.local_aliases;
    local_tag_scopes_ = environment.local_tags;
    scope_ambiguities_ = environment.scope_ambiguities;
    if (environment.nominal_occurrence) nominal_occurrence_ = environment.nominal_occurrence;
    generic_tag_owner_ = environment.generic_tag_owner;
    generic_header_depth_ = environment.generic_header_depth;
    function_scope_ = environment.function_scope;
    for (auto& scope : local_tag_scopes_)
        for (auto& [entry, tag] : scope) { (void)entry; tag.type = copy_type(tag.type); }
    scope_origins_ = environment.scope_origins;
    scope_ends_ = environment.scope_ends;
    restored_scope_origins_ = environment.scope_origins;
    restored_scope_event_base_ = environment.scope_event_base;
    import_regions_ = environment.import_regions;
    fragment_namespaces_ = environment.fragment_namespaces;
    type_aliases_ = environment.aliases;
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
        for (auto& parameter : restored_function_context_->parameters) {
            parameter.type = copy_type(parameter.type);
            parameter.declared_array_type = copy_type(parameter.declared_array_type);
        }
        restored_function_context_->generic_parameters = environment.generic_parameters;
        restored_function_context_->generic_tag_owner = environment.generic_tag_owner;
        restored_function_context_->function_scope = environment.function_scope;
        for (auto& parameter : restored_function_context_->generic_parameters)
            parameter.value_type = copy_type(parameter.value_type);
        if (!environment.variadic_bindings.empty()) {
            Attribute attribute{"variadic", {}, context.definition};
            attribute.variadic_bindings = environment.variadic_bindings;
            for (auto& binding : attribute.variadic_bindings)
                binding.type = copy_type(binding.type);
            restored_function_context_->attributes.push_back(std::move(attribute));
        }
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

bool Parser::declaration_precedes(TokenIdentity position, std::shared_ptr<const SyntaxContext> context,
                                  const TokenIdentity& use, TokenIdentity after) const {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    for (std::uint64_t depth = 0;; ++depth) {
        if (position.source_unit == use.source_unit && position.expansion == use.expansion)
            return precedes(position, use) && (!after.source_unit || precedes(after, position));
        // A declaration constructed by an earlier original-block invocation
        // belongs at that invocation's position. A copied token keeps its own
        // stream; diagnostics and destination placement never establish order.
        if (!position.expansion.value || !context || !context->invocation.valid()) return false;
        if (depth >= limits.depth) {
            diagnostics_.error(context->invocation, "syntax declaration provenance depth exceeded");
            return false;
        }
        if (execution && !execution->work(context->invocation)) return false;
        const auto invocation = token_origin(context->invocation);
        if (invocation.identity == position) return false;
        position = invocation.identity;
        context = invocation.context;
    }
}

void Parser::restore_deferred_environment(const SyntaxParseEnvironment& environment,
                                          const SyntaxContext& context,
                                          TokenIdentity original_position) {
    // Reparse against the saved environment plus declarations made after the
    // capture by earlier statements of its original lexical block. A moved
    // fragment never inherits declarations from its destination block.
    const auto receiving_origins = std::move(scope_origins_);
    const auto receiving_values = std::move(local_scopes_);
    const auto receiving_aliases = std::move(local_type_scopes_);
    const auto receiving_tags = std::move(local_tag_scopes_);
    restore_environment(environment, context);
    if (!original_position.source_unit) return;
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto work = [&] { return !execution || execution->work(context.invocation); };
    std::vector<ScopePlacementSelection> placements;
    placements.reserve(scope_origins_.size());
    for (const auto& block : scope_origins_)
        placements.push_back(select_scope_placements(block, environment.scope_event_base,
            context.invocation));
    std::vector<ScopeAmbiguity> ambiguous_names(scope_origins_.size());
    for (std::size_t captured = 0; captured < scope_origins_.size(); ++captured) {
        ambiguous_names[captured].block = scope_origins_[captured];
        ambiguous_names[captured].depth = captured;
    }
    for (std::size_t at = environment.scope_event_base; at < scope_events_->size(); ++at) {
        if (!work()) return;
        const auto& event = (*scope_events_)[at];
        if (!declaration_precedes(event.statement, event.statement_context, original_position)) continue;
        for (std::size_t captured = 0; captured < scope_origins_.size(); ++captured) {
            if (!work()) return;
            if (scope_origins_[captured] != event.block) continue;
            if (execution && !execution->work(context.invocation, placements[captured].closest.size())) return;
            if (!placements[captured].contains(event.placement)) continue;
            if (placements[captured].ambiguous()) {
                auto& names = ambiguous_names[captured];
                const auto ordinary = [&](const NameKey& key) {
                    if (!local_scopes_[captured].contains(key) && !local_type_scopes_[captured].contains(key))
                        names.ordinary.insert(key);
                };
                for (const auto& [key, binding] : event.values) {
                    if (!work()) return;
                    (void)binding; ordinary(key);
                }
                for (const auto& [key, type] : event.aliases) {
                    if (!work()) return;
                    (void)type; ordinary(key);
                }
                for (const auto& [key, tag] : event.tags) {
                    if (!work()) return;
                    (void)tag;
                    if (!local_tag_scopes_[captured].contains(key)) names.tags.insert(key);
                }
                continue;
            }
            // A live copy of the original block owns its own declaration
            // placements. Shared lexical identity does not permit borrowing
            // an earlier copy's aliases/values while selecting this copy's
            // tags. An absent declaration in this copy stays absent as well.
            auto receiving = receiving_origins.size();
            for (auto depth = receiving_origins.size(); depth != 0; --depth) {
                if (!work()) return;
                if (receiving_origins[depth - 1] == event.block) {
                    receiving = depth - 1;
                    break;
                }
            }
            const bool live_copy = receiving != receiving_origins.size();
            for (const auto& [key, binding] : event.values) {
                if (!work()) return;
                if (live_copy) {
                    if (receiving >= receiving_values.size()) continue;
                    const auto current = receiving_values[receiving].find(key);
                    if (current == receiving_values[receiving].end() || current->second != binding) continue;
                }
                local_scopes_[captured].emplace(key, binding);
            }
            for (const auto& [key, type] : event.aliases) {
                if (!work()) return;
                if (live_copy) {
                    if (receiving >= receiving_aliases.size()) continue;
                    const auto current = receiving_aliases[receiving].find(key);
                    if (current == receiving_aliases[receiving].end() || current->second != type) continue;
                }
                local_type_scopes_[captured].try_emplace(key, type);
            }
            for (const auto& [key, tag] : event.tags) {
                if (!work()) return;
                if (live_copy) {
                    if (receiving >= receiving_tags.size()) continue;
                    const auto current = receiving_tags[receiving].find(key);
                    if (current == receiving_tags[receiving].end() || !current->second.type || !tag.type ||
                        current->second.type->nominal_key() != tag.type->nominal_key()) continue;
                }
                local_tag_scopes_[captured][key] = Parser::LocalTag{copy_type(tag.type), tag.complete};
            }
        }
    }
    for (auto& ambiguity : ambiguous_names)
        if (!ambiguity.ordinary.empty() || !ambiguity.tags.empty())
            scope_ambiguities_.push_back(std::move(ambiguity));
}

std::shared_ptr<const SyntaxNode> Parser::parse_syntax_tokens(
    SyntaxParseCategory category, std::vector<Token> input,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics) {
    return parse_syntax_tokens_async(category, std::move(input), std::move(context), diagnostics).run();
}

EvaluationTask<std::shared_ptr<const SyntaxNode>> Parser::parse_syntax_tokens_async(
    SyntaxParseCategory category, std::vector<Token> input,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics) {
    if (!context || !context->parse_environment || input.empty()) co_return {};
    const auto& environment = *context->parse_environment;
    // A standalone schema parser may have no executor. An expired compilation
    // executor, however, must not be replaced with ambient caller state.
    if (environment.syntax && environment.execution.expired()) co_return {};
    Parser parser({}, diagnostics, nullptr, environment.address_bits);
    parser.restore_environment(environment, *context);
    parser.explicit_parse_context_ = context;
    co_return co_await parser.parse_syntax_tokens_async(category, std::move(input));
}

} // namespace cross
