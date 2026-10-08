// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"
#include "frontend/name.hpp"
#include "frontend/procedural.hpp"
#include "frontend/semantic.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace cross {
namespace {

TokenIdentity deferred_position(const SyntaxNode& node) {
    return node.kind == SyntaxNode::Kind::Deferred && !node.tokens.empty()
        ? node.tokens.front().origin.identity : TokenIdentity{};
}

TokenOrigin statement_origin(const Token& token, SyntaxExecution* execution) {
    if (!token.splice) return token_origin(token.location);
    // A splice's display span is not its lexical start, especially for
    // generated input. Find the first retained source token without projection.
    std::vector<const SyntaxNode*> pending{token.splice.get()};
    while (!pending.empty()) {
        if (execution && !execution->work(token.location)) return {};
        const auto* node = pending.back();
        pending.pop_back();
        if (!node->tokens.empty()) {
            const auto& first = node->tokens.front();
            if (!first.splice) return first.origin;
            pending.push_back(first.splice.get());
        } else for (auto at = node->children.rbegin(); at != node->children.rend(); ++at)
            pending.push_back(at->get());
    }
    return token_origin(token.location);
}

// Parameter remaps belong to this header/body, not later functions. A bounded
// header parser retains them until the composing parser adopts its binders.
struct LocalRebindingsRestore {
    std::unordered_map<ValueBinding, ValueBinding, ValueBindingHash>& values;
    std::unordered_map<ValueBinding, ValueBinding, ValueBindingHash> previous;
    bool retain{};
    ~LocalRebindingsRestore() {
        if (retain) return;
        for (auto at = values.begin(); at != values.end();) {
            if (at->second.kind != ValueBinding::Kind::Local) { ++at; continue; }
            if (const auto found = previous.find(at->first); found != previous.end()) {
                at->second = found->second;
                ++at;
            } else at = values.erase(at);
        }
    }
};

bool is_vector_type_attribute(std::string_view name) {
    return name == "vector_size" || name == "ext_vector_type" || name == "scalable_vector";
}

struct CapturedTypeErrorSet {
    std::map<const SourceFile*, std::unordered_map<std::size_t, std::unordered_set<std::string>>> entries;
    bool insert(const CapturedTypeError& error) {
        return entries[error.location.file][error.location.offset].insert(error.message).second;
    }
};

template<class Visit>
bool visit_captured_type_errors(const TypePtr& type, SyntaxExecution* execution,
                               SourceLocation location, Visit&& visit) {
    std::unordered_set<const Type*> seen;
    std::unordered_set<const Expr*> seen_expressions;
    CapturedTypeErrorSet seen_errors;
    std::vector<TypePtr> pending{type};
    std::vector<const Expr*> expressions;
    while (!pending.empty() || !expressions.empty()) {
        if (execution && !execution->work(location)) return false;
        if (!expressions.empty()) {
            const auto* expression = expressions.back();
            expressions.pop_back();
            if (!expression || !seen_expressions.insert(expression).second) continue;
            pending.push_back(expression->type);
            expressions.push_back(expression->left.get());
            expressions.push_back(expression->right.get());
            expressions.push_back(expression->third.get());
            for (const auto& argument : expression->arguments) expressions.push_back(argument.get());
            for (const auto& argument : expression->generic_arguments) {
                pending.push_back(argument.type);
                expressions.push_back(argument.value.get());
            }
            for (const auto& entry : expression->initializer_entries) {
                expressions.push_back(entry.value.get());
                for (const auto& designator : entry.designators) expressions.push_back(designator.index.get());
            }
            continue;
        }
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        for (const auto& error : next->captured_errors)
            if (seen_errors.insert(error)) visit(error);
        if (next->captured_tag_errors)
            for (const auto& error : *next->captured_tag_errors)
                if (seen_errors.insert(error)) visit(error);
        pending.push_back(next->pointee);
        pending.push_back(next->element);
        expressions.push_back(next->array_bound.get());
        expressions.push_back(next->vector_bound.get());
        if (next->function) {
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                pending.push_back(parameter.type);
                pending.push_back(parameter.declared_array_type);
            }
        }
    }
    return true;
}

std::optional<std::string> familiar_c_spelling(std::string_view spelling) {
    if (spelling == "int" || spelling == "long" || spelling == "short" ||
        spelling == "signed" || spelling == "unsigned") {
        return "no visible Cross type named '" + std::string(spelling) +
               "'; Cross integers use exact names such as i32, u32, i64, "
               "and u64";
    }
    if (spelling == "char") {
        return "no visible Cross type named 'char'; use i8 or u8 for an "
               "8-bit integer";
    }
    if (spelling == "float") {
        return "no visible Cross type named 'float'; use f32";
    }
    if (spelling == "double") {
        return "no visible Cross type named 'double'; use f64";
    }
    if (spelling == "_Bool") {
        return "no visible Cross type named '_Bool'; use bool";
    }
    if (spelling == "auto") {
        return "Cross has no 'auto' type inference; write the exact type";
    }
    if (spelling == "extern") {
        return "'extern' is not Cross syntax; a declaration without a body "
               "or initializer is already a forward declaration";
    }
    if (spelling == "__attribute__" || spelling == "__declspec") {
        return "vendor attribute syntax is not Cross syntax; use a "
               "[[attribute]] spelling";
    }
    return std::nullopt;
}

std::string join_namespace(std::string_view prefix, std::string_view name) {
    if (prefix.empty()) return std::string(name);
    return std::string(prefix) + "::" + std::string(name);
}

std::optional<Expr::IntegerConstant> constant_value(const Expr& expression, unsigned address_bits) {
    // Parser-only classification cannot enter calls, inspect names, query
    // layout, or execute captured source. Literal arithmetic shares the real
    // target evaluator instead of performing unchecked host int64 arithmetic.
    const auto literal_arithmetic = [&](const auto& self, const Expr& node) -> bool {
        switch (node.kind) {
        case Expr::Kind::Integer:
            return address_bits != 0 ||
                (!node.text.ends_with("iptr") && !node.text.ends_with("uptr"));
        case Expr::Kind::Parenthesized:
            return node.left && self(self, *node.left);
        case Expr::Kind::Unary:
            return (node.text == "+" || node.text == "-" || node.text == "!" || node.text == "~") &&
                node.left && self(self, *node.left);
        case Expr::Kind::Binary:
            return node.left && node.right && self(self, *node.left) && self(self, *node.right);
        case Expr::Kind::Conditional:
            return node.left && node.right && node.third && self(self, *node.left) &&
                self(self, *node.right) && self(self, *node.third);
        default: return false;
        }
    };
    if (!literal_arithmetic(literal_arithmetic, expression)) return {};
    Program program;
    program.address_bits = address_bits;
    std::ostringstream output;
    Diagnostics quiet(output);
    return evaluate_target_integer_constant(program, expression, quiet, {}, {});
}

} // namespace

class Parser::ScopePlacementFrame {
public:
    ScopePlacementFrame(Parser& parser, SourceLocation location, TokenIdentity source_end = {})
        : parser_(parser), previous_(parser.scope_placement_) {
        const auto origin = token_origin(location);
        if (!parser.syntax_ || parser.parsing_public_fragment_ || !origin.identity.source_unit) return;
        placement_ = std::make_shared<ScopePlacement>();
        placement_->block = origin.identity;
        placement_->source_end = source_end;
        placement_->generic_owner = parser.generic_tag_owner_;
        placement_->function_scope = parser.function_scope_;
        placement_->parent = previous_;
        placement_->depth = previous_ ? previous_->depth + 1 : 1;
        placement_->first_event = parser.scope_events_->size();
        parser.scope_placement_ = placement_;
        (*parser.scope_placements_)[origin.identity].push_back(placement_);
        // Empty copies matter too: they cannot borrow a sibling's declarations.
        ScopeEvent opened;
        opened.block = origin.identity;
        opened.statement = origin.identity;
        opened.statement_context = origin.context;
        opened.placement = placement_;
        parser.scope_events_->push_back(std::move(opened));
    }
    ~ScopePlacementFrame() {
        if (placement_) placement_->end_event = parser_.scope_events_->size();
        parser_.scope_placement_ = std::move(previous_);
    }
private:
    Parser& parser_;
    std::shared_ptr<const ScopePlacement> previous_;
    std::shared_ptr<ScopePlacement> placement_;
};

class Parser::HeaderNominalFrame {
public:
    HeaderNominalFrame(Parser& parser, bool enabled, SourceLocation location,
                       const HeaderNominalScope* names = nullptr)
        : parser_(parser), enabled_(enabled), previous_(parser.generic_header_depth_),
          depths_{parser.local_scopes_.size(), parser.local_type_scopes_.size(),
                  parser.local_tag_scopes_.size(), parser.scope_origins_.size(),
                  parser.scope_ends_.size()} {
        if (!enabled_) return;
        parser.local_scopes_.push_back(names ? names->values : NameMap<ValueBinding>{});
        parser.local_type_scopes_.emplace_back();
        parser.local_tag_scopes_.push_back(names ? names->tags : NameMap<LocalTag>{});
        parser.scope_origins_.push_back(token_origin(location).identity);
        parser.scope_ends_.emplace_back();
        parser.generic_header_depth_ = parser.local_scopes_.size();
    }
    ~HeaderNominalFrame() {
        if (!enabled_) return;
        parser_.local_scopes_.resize(depths_[0]);
        parser_.local_type_scopes_.resize(depths_[1]);
        parser_.local_tag_scopes_.resize(depths_[2]);
        parser_.scope_origins_.resize(depths_[3]);
        parser_.scope_ends_.resize(depths_[4]);
        parser_.generic_header_depth_ = previous_;
    }
private:
    Parser& parser_;
    bool enabled_;
    std::optional<std::size_t> previous_;
    std::array<std::size_t, 5> depths_;
};

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics,
               std::shared_ptr<SyntaxExecution> execution,
               unsigned address_bits)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics),
      address_bits_(address_bits) {
    if (execution) syntax_.emplace(std::move(execution));
    resource_epoch_ = syntax_ ? syntax_->execution()->resource_errors() : 0;
}

const Token& Parser::current(std::size_t lookahead) const {
    if (public_tree_failed_ || resource_failed()) return tokens_.back();
    const auto position = index_ + lookahead;
    return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
}

const Token& Parser::recognition_current(std::size_t lookahead) const {
    if (!resource_failed() && !raw_token_depth_ && probing_header_type_ && macro_start())
        throw HeaderProbeInvocation{public_input_indices_[index_], true};
    return current(lookahead);
}

bool Parser::CursorRead::await_ready() {
    if (parser_.resource_failed()) return true;
    if (!parser_.raw_token_depth_ && parser_.probing_header_type_ && parser_.macro_start())
        throw HeaderProbeInvocation{parser_.public_input_indices_[parser_.index_], true};
    if (!parser_.raw_token_depth_ && !parser_.parsing_public_fragment_ &&
        !parser_.public_tree_failed_ && parser_.macro_start()) {
        exposure_.emplace(parser_.expand_inline_macro_fragments_async());
        return false;
    }
    return true;
}

Token Parser::CursorRead::await_resume() {
    if (exposure_) exposure_->await_resume();
    return std::as_const(parser_).current(lookahead_);
}

EvaluationTask<bool> Parser::consume_async(std::string_view spelling) {
    if (!(co_await current_async()).is(spelling)) co_return false;
    ++index_;
    co_return true;
}

EvaluationTask<std::optional<Token>> Parser::consume_kind_async(TokenKind kind) {
    if ((co_await current_async()).kind != kind) co_return {};
    // Later token expansion may reallocate the cursor's vector. Consumers
    // retain a value, never a pointer into that mutable sequence.
    co_return tokens_[index_++];
}

EvaluationTask<bool> Parser::expect_async(std::string_view spelling, std::string_view context) {
    if ((co_await consume_async(spelling))) co_return true;
    std::string message = "expected '" + std::string(spelling) + "'";
    if (!context.empty()) message += " " + std::string(context);
    (co_await error_here_async(std::move(message)));
    co_return false;
}

void Parser::error_here(std::string message) {
    const auto location = current().location;
    if (!resource_failed()) diagnostics_.error(location, message);
}

EvaluationTask<void> Parser::error_here_async(std::string message) {
    const auto location = (co_await current_async()).location;
    if (!resource_failed()) diagnostics_.error(location, message);
}

void Parser::synchronize_external() {
    // Recovery skips source; it must not execute macros in rejected input.
    while (std::as_const(*this).current().kind != TokenKind::End) {
        if (std::as_const(*this).current().is(";")) { ++index_; return; }
        if (std::as_const(*this).current().is("}")) return;
        const auto& token = std::as_const(*this).current();
        if (token.is("(") || token.is("[") || token.is("[[") || token.is("{")) {
            // A semicolon inside an inline tag or rejected raw body is not
            // an external recovery boundary. Keep the complete written group
            // opaque so its nested invocations cannot become new source.
            const auto end = bounded_group_end(index_);
            index_ = end.value_or(tokens_.size() - 1);
            continue;
        }
        ++index_;
    }
}

EvaluationTask<std::vector<SyntaxActivation>> Parser::parse_syntax_entries_async(std::string_view end) {
    std::vector<SyntaxActivation> entries;
    do {
        const auto location = (co_await current_async()).location;
        if ((co_await current_async()).kind != TokenKind::Identifier || is_reserved_identifier((co_await current_async()).text)) {
            (co_await error_here_async("expected nonreserved syntax entity name"));
            break;
        }
        auto name = (co_await parse_qualified_name_async());
        if (!name) break;
        SyntaxActivation entry{std::move(*name), {}, location};
        if ((co_await consume_async("as"))) {
            if ((co_await current_async()).kind != TokenKind::Identifier || is_reserved_identifier((co_await current_async()).text)) {
                (co_await error_here_async("expected nonreserved syntax prefix alias"));
                break;
            }
            entry.alias = (co_await current_async()).text;
            ++index_;
        }
        entries.push_back(std::move(entry));
    } while ((co_await consume_async(",")));
    (co_await expect_async(end, "after syntax activation list"));
    co_return entries;
}

EvaluationTask<bool> Parser::parse_syntax_registration_async(Program* program) {
    if (!(co_await current_async()).is("syntax")) co_return false;
    const auto location = (co_await current_async()).location;
    if (replacement_ || token_origin(location).context) {
        (co_await error_here_async("expansion output cannot introduce syntax registration"));
        ++index_;
        synchronize_external();
        co_return true;
    }
    if (!syntax_) co_return false;
    if ((co_await current_async(1)).kind == TokenKind::Identifier && (co_await current_async(2)).is(":")) {
        const auto first = index_;
        std::optional<std::size_t> declaration_end;
        for (auto at = first; at < tokens_.size() && tokens_[at].kind != TokenKind::End; ++at) {
            if (!tokens_[at].is("{")) continue;
            unsigned depth = 1;
            for (++at; at < tokens_.size() && tokens_[at].kind != TokenKind::End; ++at) {
                if (tokens_[at].is("{")) ++depth;
                else if (tokens_[at].is("}") && --depth == 0) { declaration_end = at + 1; break; }
            }
            break;
        }
        if (!program) {
            (co_await error_here_async("syntax definitions are allowed only at item position"));
            if (declaration_end) index_ = *declaration_end;
            else { ++index_; synchronize_external(); }
        } else if (!syntax_->declare(tokens_, index_, active_namespace_, diagnostics_)) {
            if (declaration_end) index_ = *declaration_end;
            else synchronize_external();
        }
        co_return true;
    }
    ++index_;
    const bool region = (co_await consume_async("("));
    const auto errors = diagnostics_.errors();
    auto entries = (co_await parse_syntax_entries_async(region ? ")" : ";"));
    if (region) {
        if (!program) {
            diagnostics_.error(location, "syntax regions are allowed only at item position");
            synchronize_external();
            co_return true;
        }
        syntax_->push_scope();
    }
    if (region) {
        struct RegionRestore {
            Parser& parser;
            std::vector<std::string> imports;
            std::vector<TokenIdentity> import_declarations;
            std::size_t scope_imports;
            std::size_t region_depth;
            ~RegionRestore() {
                parser.import_regions_.resize(region_depth);
                parser.active_imports_ = std::move(imports);
                parser.active_import_declarations_ = std::move(import_declarations);
                parser.current_scope_imports_ = scope_imports;
                parser.syntax_->pop_scope();
            }
        } restore{*this, active_imports_, active_import_declarations_,
            current_scope_imports_, import_regions_.size()};
        if (diagnostics_.errors() == errors)
            (void)syntax_->activate(entries, active_namespace_, diagnostics_);
        current_scope_imports_ = 0;
        const auto region_namespace = active_namespace_;
        TokenRegion import_region{token_origin((co_await current_async()).location).identity, {}};
        if (const auto end = bounded_group_end(index_); end && *end > index_)
            import_region.end = token_origin(tokens_[*end - 1].location).identity;
        if ((co_await expect_async("{", "after syntax region"))) {
            import_regions_.push_back(import_region);
            while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
                const auto before = index_;
                co_await parse_external_async(*program, region_namespace);
                if (before == index_) ++index_;
            }
            (co_await expect_async("}", "after syntax region items"));
        }
    } else if (diagnostics_.errors() == errors)
        (void)syntax_->activate(entries, active_namespace_, diagnostics_);
    co_return true;
}

bool Parser::macro_start() const {
    if (!syntax_ || current().kind != TokenKind::Identifier) return false;
    auto at = index_ + 1;
    while (at + 1 < tokens_.size() && tokens_[at].is("::") &&
           tokens_[at + 1].kind == TokenKind::Identifier) at += 2;
    return at + 1 < tokens_.size() && tokens_[at].is("!") &&
        (tokens_[at + 1].is("(") || tokens_[at + 1].is("[") || tokens_[at + 1].is("{"));
}

const SyntaxDefinition* Parser::active_syntax(bool item) const {
    if (!syntax_ || current().kind != TokenKind::Identifier ||
        current(1).is("::") || current(1).is(":") || macro_start()) return nullptr;
    return syntax_->selected(current(), item);
}

EvaluationTask<std::optional<SyntaxExecution::Output>> Parser::expand_at_position_async(bool item) {
    NamespaceContextCacheScope context_cache(*this);
    auto& expansion_diagnostics = expansion_diagnostics_ ? *expansion_diagnostics_ : diagnostics_;
    const auto location = (co_await current_async()).location;
    const auto origin = token_origin(location);
    const auto context = syntax_context(location);
    if (!context) { ++index_; synchronize_external(); co_return {}; }
    const auto& name_space = context->name_space;
    const auto& imports = context->imports;
    const auto& bindings = context->syntax_bindings;
    auto execution = syntax_->execution();
    if (macro_start()) {
        auto name = (co_await parse_qualified_name_async());
        ++index_; // '!'
        const auto opening = (co_await current_async()).text;
        std::vector<std::string_view> stack;
        TokenSequence input;
        ++index_;
        stack.push_back(opening == "(" ? ")" : opening == "[" ? "]" : "}");
        while ((co_await current_async()).kind != TokenKind::End && !stack.empty()) {
            if (!execution->work((co_await current_async()).location)) co_return {};
            const auto spelling = (co_await current_async()).text;
            if (spelling == "(" || spelling == "[" || spelling == "[[" || spelling == "{")
                stack.push_back(spelling == "(" ? ")" : spelling == "[" ? "]" : spelling == "[[" ? "]]" : "}");
            else if (spelling == ")" || spelling == "]" || spelling == "]]" || spelling == "}") {
                if (stack.back() != spelling) {
                    expansion_diagnostics.error((co_await current_async()).location,
                        "mismatched procedural macro token-tree delimiter");
                    ++index_;
                    co_return {};
                }
                stack.pop_back();
            }
            if (!stack.empty()) input.emplace_back((co_await current_async()));
            ++index_;
        }
        if (!stack.empty()) {
            expansion_diagnostics.error(location, "unterminated procedural macro token tree");
            co_return {};
        }
        const auto search_imports = origin.context
            ? std::vector<std::vector<std::string>>{imports} : syntax_->imports();
        // A bound namespace prefix is authoritative for macro names too.
        // Macro definitions themselves cannot be generated, but a fragment
        // may introduce/reopen a namespace containing a visible source macro.
        const auto introduced = name->find("::") == std::string::npos
            ? std::optional<FragmentNamespaceName>{}
            : fragment_name(*name, location, FragmentNameDomain::Namespace);
        const auto function = introduced
            ? execution->find_function(introduced->destination, {}, {}, false)
            : execution->find_function(*name, name_space, search_imports, false);
        if (!function) {
            expansion_diagnostics.error(location, "procedural macro is not visible: '" + *name + "'");
            co_return {};
        }
        co_return co_await execution->expand_async(*function, std::move(input), {}, location, name_space, imports, bindings,
                                 context->parse_environment, nullptr, context->import_declarations);
    }
    const auto* definition = active_syntax(item);
    if (!definition) co_return {};
    const auto matched = co_await syntax_->match_async(*definition, tokens_, index_, expansion_diagnostics,
        [&](SyntaxPatternElement::Kind kind, std::size_t first) {
            return parse_syntax_fragment_async(kind, first);
        }, [&](SourceLocation at) { return token_origin(at).context ? syntax_context(at) : context; });
    if (!matched) { ++index_; synchronize_external(); co_return {}; }
    index_ = matched->end;
    co_return co_await execution->expand_async(matched->expander, {}, matched->value, location, name_space, imports, bindings,
                             context->parse_environment, definition, context->import_declarations);
}



EvaluationTask<void> Parser::expand_inline_macro_fragments_async() {
    if (raw_token_depth_) co_return;
    struct RawTokens {
        unsigned& depth;
        explicit RawTokens(unsigned& value) : depth(value) { ++depth; }
        ~RawTokens() { --depth; }
    } raw{raw_token_depth_};
    while (syntax_ && macro_start()) {
        // Captures only recognize syntax; they cannot execute a nested macro
        // to discover a declarator's shape or its bound names.
        if (probing_header_type_)
            throw HeaderProbeInvocation{public_input_indices_[index_], true};
        if (parsing_public_fragment_) throw DeferredNameRecognition{};
        const auto first = index_;
        const auto discard_failed_invocation = [&] {
            // Header/name lookahead restores its cursor after exposing tokens.
            // Consume a failed invocation persistently, including its raw
            // input, so it cannot execute again or leak nested input into
            // recovery when normal parsing revisits this position.
            auto after = first + 1;
            while (after + 1 < tokens_.size() && tokens_[after].is("::")) after += 2;
            ++after; // '!' (macro_start already recognized the opening group)
            unsigned depth = 0;
            for (; after + 1 < tokens_.size(); ++after) {
                const auto& token = tokens_[after];
                if (token.is("(") || token.is("[") || token.is("[[") || token.is("{")) ++depth;
                else if (token.is(")") || token.is("]") || token.is("]]") || token.is("}")) {
                    if (depth && --depth == 0) { ++after; break; }
                }
            }
            tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
                          tokens_.begin() + static_cast<std::ptrdiff_t>(after));
            index_ = first;
        };
        auto execution = syntax_->execution();
        if (!execution->begin_replacement((co_await current_async()).location)) {
            discard_failed_invocation();
            co_return;
        }
        struct End {
            SyntaxExecution& execution;
            ~End() { execution.end_replacement(); }
        } end{*execution};
        auto output = co_await expand_at_position_async(false);
        if (!output) {
            discard_failed_invocation();
            co_return;
        }
        if (output->tokens.empty() || output->tokens.back().kind != TokenKind::End) {
            diagnostics_.error(output->location,
                "procedural macro produced no token boundary");
            discard_failed_invocation();
            co_return;
        }
        const auto after = index_;
        // Keep end-of-fragment errors on the innermost generating macro,
        // while preserving the caller's actual End boundary.
        if (after < tokens_.size() && tokens_[after].kind == TokenKind::End)
            tokens_[after].location = output->tokens.back().location;
        output->tokens.pop_back();
        tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
                      tokens_.begin() + static_cast<std::ptrdiff_t>(after));
        tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
            std::make_move_iterator(output->tokens.begin()),
            std::make_move_iterator(output->tokens.end()));
        index_ = first;
    }
}

std::unique_ptr<Parser> Parser::replacement_parser(SyntaxExecution::Output output,
    Diagnostics* diagnostics) const {
    auto child = std::make_unique<Parser>(std::move(output.tokens),
        diagnostics ? *diagnostics : diagnostics_, nullptr, address_bits_);
    child->syntax_ = syntax_;
    child->resource_epoch_ = resource_epoch_;
    child->preparing_header_ = preparing_header_;
    child->retaining_shared_specifiers_ = retaining_shared_specifiers_;
    child->shared_specifier_header_ = shared_specifier_header_;
    child->shared_specifier_declarator_ = shared_specifier_declarator_;
    child->shared_specifier_owners_ = shared_specifier_owners_;
    child->expansion_diagnostics_ = expansion_diagnostics_;
    child->header_bindings_ = header_bindings_;
    child->explicit_parse_context_ = explicit_parse_context_;
    child->public_header_uncertain_names_ = public_header_uncertain_names_;
    child->replacement_ = true;
    child->active_imports_ = active_imports_;
    child->active_import_declarations_ = active_import_declarations_;
    child->current_scope_imports_ = current_scope_imports_;
    child->active_generic_types_ = active_generic_types_;
    child->known_functions_ = known_functions_;
    child->declaration_destination_ = declaration_destination_;
    child->known_ordinary_values_ = known_ordinary_values_;
    child->local_scopes_ = local_scopes_;
    child->prototype_scopes_ = prototype_scopes_;
    child->local_type_scopes_ = local_type_scopes_;
    child->local_tag_scopes_ = local_tag_scopes_;
    child->scope_ambiguities_ = scope_ambiguities_;
    child->nominal_occurrence_ = nominal_occurrence_;
    child->generic_tag_owner_ = generic_tag_owner_;
    child->generic_header_depth_ = generic_header_depth_;
    child->function_scope_ = function_scope_;
    child->tag_destination_ = tag_destination_;
    child->tag_destination_depth_ = tag_destination_depth_;
    child->external_binding_depth_ = external_binding_depth_;
    child->value_rebindings_ = value_rebindings_;
    child->tag_rebindings_ = tag_rebindings_;
    child->alias_rebindings_ = alias_rebindings_;
    child->scope_origins_ = scope_origins_;
    child->scope_ends_ = scope_ends_;
    child->restored_scope_origins_ = restored_scope_origins_;
    child->restored_scope_event_base_ = restored_scope_event_base_;
    child->scope_events_ = scope_events_;
    child->scope_placement_ = scope_placement_;
    child->scope_placements_ = scope_placements_;
    child->forward_tags_ = forward_tags_;
    child->scope_import_events_ = scope_import_events_;
    child->import_regions_ = import_regions_;
    child->fragment_namespaces_ = fragment_namespaces_;
    child->fragment_namespace_placements_ = fragment_namespace_placements_;
    child->active_namespace_ = active_namespace_;
    child->enum_types_ = enum_types_;
    child->record_types_ = record_types_;
    child->type_aliases_ = type_aliases_;
    child->active_function_ = active_function_;
    // Caller angle-token fences cannot constrain an independently bounded
    // replacement expression. Its own generic parses install their fences.
    child->parsing_generic_argument_ = false;
    child->parsing_procedural_body_ = parsing_procedural_body_;
    child->public_uncertain_binding_depths_ = public_uncertain_binding_depths_;
    child->switch_depth_ = switch_depth_;
    child->switch_default_seen_ = switch_default_seen_;
    return child;
}

EvaluationTask<void> Parser::remember_shared_declarator_async() {
    if (!shared_specifier_owners_ || !shared_specifier_declarator_.source_unit) co_return;
    auto& owners = shared_specifier_owners_->declarations;
    if (syntax_) {
        const auto execution = syntax_->execution();
        if (!execution->work((co_await current_async()).location, owners.size() + 1)) co_return;
        const auto& limits = execution->limits();
        if (owners.size() >= std::min(limits.bytes, limits.memory) / 40) {
            execution->tree_limit_error((co_await current_async()).location);
            public_tree_failed_ = true;
            co_return;
        }
    }
    if (std::find(owners.begin(), owners.end(), shared_specifier_declarator_) == owners.end())
        owners.push_back(shared_specifier_declarator_);
}

void Parser::remember_specifier_input(std::size_t index) {
    if (!retaining_shared_specifiers_) return;
    if (syntax_) {
        const auto execution = syntax_->execution();
        const auto& limits = execution->limits();
        if (shared_specifier_inputs_.size() >= std::min(limits.bytes, limits.memory) / meta_token_storage_bytes) {
            execution->tree_limit_error(tokens_[index].location);
            public_tree_failed_ = true;
            return;
        }
        if (!execution->work(tokens_[index].location)) { public_tree_failed_ = true; return; }
    }
    shared_specifier_inputs_.emplace_back(index, tokens_[index]);
}

std::vector<Token> Parser::specifier_input_tokens(std::size_t first, std::size_t end) const {
    std::vector<Token> result(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
        tokens_.begin() + static_cast<std::ptrdiff_t>(end));
    // Preserve the public/written view's bound tokens. Only the private replay
    // input removes bindings learned while interpreting this semantic view.
    for (auto entry = shared_specifier_inputs_.rbegin(); entry != shared_specifier_inputs_.rend(); ++entry) {
        const auto& [position, input] = *entry;
        if (position < first || position >= end) continue;
        auto& token = result[position - first];
        // A containing expansion may have replaced this entire token range.
        // Its PreparedFragment already owns its own cleaned input sequence.
        if (token.kind == input.kind && token.text == input.text &&
            token.location.file == input.location.file && token.location.offset == input.location.offset)
            token = input;
    }
    return result;
}

EvaluationTask<void> Parser::prepare_header_async(const std::vector<Token>* shared_specifiers) {
    if (!syntax_ || preparing_header_ || parsing_public_fragment_ ||
        (co_await current_async()).is("namespace") || (co_await current_async()).is("using") ||
        (co_await current_async()).is("$::static_assert")) co_return;
    // Only discovery-visible procedural fragments can change header-wide
    // bindings. An ordinary syntax expression alone must not make otherwise
    // settled captures opaque. This probe shares discovery's boundaries and
    // never executes a fragment or looks inside an expression owner's input.
    bool pending_fragments = false;
    (void)(co_await preview_generic_types_async(&pending_fragments, nullptr, shared_specifiers != nullptr));
    if (!pending_fragments) co_return;
    const auto first = index_;
    const auto location = tokens_[first].location;
    const auto execution = shared_specifiers ? syntax_->execution() : nullptr;
    if (execution && !execution->begin_fragment(location, shared_specifiers->size())) co_return;
    struct EndPreparationView {
        std::shared_ptr<SyntaxExecution> execution;
        ~EndPreparationView() { if (execution) execution->end_fragment(); }
    } end_view{execution};
    // A later declarator has no written specifiers of its own. Give the same
    // header recognizer the already-expanded shared input, then remove that
    // private prefix at the grammar-proved declarator boundary. No source
    // expansion is replayed and the caller retains its one written prefix.
    if (shared_specifiers && !shared_specifiers->empty())
        tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
            shared_specifiers->begin(), shared_specifiers->end() - 1);
    std::ostringstream ignored;
    Diagnostics provisional(ignored);
    auto child = replacement_parser({std::move(tokens_), location}, &provisional);
    // The speculative parser owns the live token stream through suspension.
    // Restore it even when an internal callback throws before header completion.
    struct InputRestore {
        Parser& parser;
        Parser& child;
        std::size_t first;
        const std::vector<Token>* specifiers;
        ~InputRestore() {
            if (specifiers) {
                const auto boundary = child.prepared_declarator_start_.value_or(
                    first + (specifiers->empty() ? 0 : specifiers->size() - 1));
                child.tokens_.erase(child.tokens_.begin() + static_cast<std::ptrdiff_t>(first),
                    child.tokens_.begin() + static_cast<std::ptrdiff_t>(std::min(boundary, child.tokens_.size())));
            }
            parser.tokens_ = std::move(child.tokens_);
            parser.index_ = first;
        }
    } restore_input{*this, *child, first, shared_specifiers};
    child->index_ = first;
    child->preparing_header_ = true;
    child->parsing_public_function_header_ = true;
    child->expansion_diagnostics_ = &diagnostics_;
    auto bindings = std::make_shared<SyntaxHeaderBindings>();
    child->header_bindings_ = bindings;
    Program header;
    try {
        co_await child->parse_external_async(header, active_namespace_);
    } catch (const HeaderPrepared&) {
        // Object initializers and function bodies are not header work.
    }
    bindings->finish(header.functions.empty() ? std::vector<GenericParameter>{}
        : std::move(header.functions.front()->generic_parameters), *syntax_->execution(), location);
    header_bindings_ = std::move(bindings);
    co_return;
}

EvaluationTask<bool> Parser::probe_header_type_async(bool generic_argument) {
    // During preparation an as-yet unknown generic name can begin a cast or
    // sizeof type. Prove only its grammatical shape, with nested invocations
    // opaque; the final header parse still decides whether this is a type.
    // Individual probes cannot execute code or mutate the live token cursor.
    // When they prove a common next invocation, expose it once and try again.
    enum class Outcome { Complete, Rejected, Uncertain, Invocation };
    struct Probe {
        Outcome outcome{Outcome::Rejected};
        HeaderProbeInvocation invocation{};
    };
    const auto execution = syntax_->execution();
    const auto probe = [&](bool type) -> EvaluationTask<Probe> {
        if (!execution->begin_fragment((co_await current_async()).location, tokens_.size()))
            co_return {Outcome::Uncertain};
        struct End { SyntaxExecution& execution; ~End() { execution.end_fragment(); } } end{*execution};
        std::ostringstream ignored;
        Diagnostics provisional(ignored);
        auto child = replacement_parser({tokens_, (co_await current_async()).location}, &provisional);
        child->index_ = index_;
        child->parsing_public_fragment_ = true;
        child->scope_import_events_ = std::make_shared<std::vector<ScopeImportEvent>>(*scope_import_events_);
        child->probing_header_type_ = true;
        child->parsing_generic_argument_ = generic_argument;
        for (std::size_t at = 0; at < tokens_.size(); ++at)
            child->public_input_indices_.push_back(at);
        try {
            if (type) {
                auto parsed = co_await child->parse_type_async();
                std::optional<std::string> name;
                if (parsed) parsed = co_await child->parse_declarator_async(
                    std::move(parsed), name, DeclaratorContext::TypeName);
                if (!parsed || name) co_return {};
            } else {
                (void)co_await child->parse_assignment_async();
            }
            const auto boundary = co_await child->current_async();
            const bool at_end = generic_argument
                ? boundary.is(",") || boundary.is(">") || boundary.is(">>")
                : boundary.is(")");
            co_return {at_end && provisional.errors() == 0 ? Outcome::Complete : Outcome::Rejected};
        } catch (const HeaderProbeInvocation& invocation) {
            co_return {provisional.errors() == 0 ? Outcome::Invocation : Outcome::Rejected, invocation};
        } catch (const HeaderProbeRejected&) {
            co_return {};
        } catch (const DeferredNameRecognition&) {
            co_return {Outcome::Uncertain};
        }
    };
    for (;;) {
        const auto type = co_await probe(true);
        if (type.outcome == Outcome::Complete) co_return true;
        if (type.outcome != Outcome::Invocation) co_return false;
        const auto expression = co_await probe(false);
        // Only execute an invocation proved to be reached under either
        // interpretation, or when the expression grammar already failed.
        // In particular, never expose a macro hidden in a custom owner's input
        // merely because the tentative type parser could reach its tokens.
        if (expression.outcome != Outcome::Rejected &&
            (expression.outcome != Outcome::Invocation ||
             expression.invocation != type.invocation)) co_return false;
        struct RestoreCursor {
            std::size_t& cursor;
            std::size_t first;
            ~RestoreCursor() { cursor = first; }
        } restore{index_, index_};
        index_ = type.invocation.position;
        if (type.invocation.macro) (co_await expand_inline_macro_fragments_async());
        else (void)co_await parse_expression_replacement_async();
        // Expanded tokens may change the grammatical alternative. Re-probe;
        // neither speculative AST nor speculative name bindings are retained.
    }
}

void Parser::retain_prepared_fragment(std::size_t first, SyntaxExecution::Output output,
    SyntaxParseCategory category, std::shared_ptr<const SyntaxContext> context, bool deferred,
    TokenIdentity original_position) {
    const auto location = tokens_[first].location;
    const auto maximum = std::min(syntax_->execution()->limits().bytes,
                                  syntax_->execution()->limits().memory);
    std::uint64_t storage = 128;
    bool over_budget = storage > maximum;
    const auto add = [&](std::uint64_t bytes) {
        if (bytes > maximum - std::min(storage, maximum)) over_budget = true;
        else storage += bytes;
    };
    for (const auto& token : output.tokens) {
        add(meta_token_storage_bytes); add(token.text.size());
        add(token_binding_storage(token));
        if (token.prepared) add(token.prepared->storage);
        if (token.splice) add(syntax_node_storage(*token.splice, maximum));
    }
    if (context) add(syntax_context_storage(*context));
    if (over_budget) {
        syntax_->execution()->tree_limit_error(location);
        output.tokens = {{TokenKind::End, {}, location}};
    }
    auto fragment = std::make_shared<PreparedSyntaxFragment>();
    fragment->output = std::move(output);
    fragment->category = category;
    fragment->context = std::move(context);
    fragment->deferred = deferred;
    fragment->original_position = original_position;
    fragment->storage = storage;
    Token token{TokenKind::PreparedFragment, {}, location};
    token.prepared = std::move(fragment);
    tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
                  tokens_.begin() + static_cast<std::ptrdiff_t>(index_));
    tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(first), std::move(token));
    index_ = first + 1;
}

std::unique_ptr<Parser> Parser::prepared_fragment_parser(const Token& token) {
    if (!token.prepared || !syntax_ ||
        !syntax_->execution()->begin_fragment(token.location, token.prepared->output.tokens.size())) return {};
    auto child = replacement_parser(token.prepared->output);
    child->prepared_fragment_frame_ = std::make_unique<PreparedFragmentFrame>();
    child->prepared_fragment_frame_->execution = syntax_->execution();
    if (token.prepared->context && token.prepared->context->parse_environment) {
        child->retaining_shared_specifiers_ = false;
        if (token.prepared->deferred)
            child->restore_deferred_environment(*token.prepared->context->parse_environment,
                *token.prepared->context, token.prepared->original_position);
        else child->restore_environment(*token.prepared->context->parse_environment,
                                        *token.prepared->context);
    }
    return child;
}

Parser::ProductionScope::ProductionScope(Parser& owner, SyntaxProduction production)
    : parser(owner), event(owner.begin_production(production)) {}

Parser::ProductionScope::~ProductionScope() { finish(); }

void Parser::ProductionScope::finish() {
    parser.end_production(event);
    event = std::numeric_limits<std::size_t>::max();
}

std::size_t Parser::begin_production(SyntaxProduction production) {
    if (!recording_public_tree_ || production == SyntaxProduction::None)
        return std::numeric_limits<std::size_t>::max();
    if (public_tree_failed_) return std::numeric_limits<std::size_t>::max();
    const auto limits = syntax_ ? syntax_->execution()->limits() : EvaluationLimits{};
    if (production_stack_.size() >= limits.depth ||
        production_events_.size() >= std::min(limits.bytes, limits.memory) / 128 ||
        (syntax_ && !syntax_->execution()->work(recognition_current().location))) {
        if (syntax_) syntax_->execution()->tree_limit_error(recognition_current().location);
        error_here("public syntax tree depth, work, or storage budget exceeded");
        public_tree_failed_ = true;
        return std::numeric_limits<std::size_t>::max();
    }
    const auto event = production_events_.size();
    // Reuse environments inside a core unit. Subsequent units capture the
    // lexical state after preceding declarations/imports have changed it.
    const bool boundary = production_stack_.empty() ||
        production == SyntaxProduction::Statement || production == SyntaxProduction::Declaration ||
        production == SyntaxProduction::CompoundStatement ||
        production == SyntaxProduction::FunctionHeader || production == SyntaxProduction::TypeName;
    auto context = boundary ? syntax_context(recognition_current().location)
        : production_events_[production_stack_.back()].context;
    if (!context) {
        public_tree_failed_ = true;
        return std::numeric_limits<std::size_t>::max();
    }
    production_events_.push_back({production, index_, index_, {}, {}, std::move(context)});
    if (!production_stack_.empty())
        production_events_[production_stack_.back()].children.push_back(event);
    production_stack_.push_back(event);
    return event;
}

void Parser::end_production(std::size_t event) {
    if (event == std::numeric_limits<std::size_t>::max()) return;
    production_events_[event].end = index_;
    production_stack_.pop_back();
}

void Parser::flatten_production(std::size_t event, std::size_t parent) {
    if (parent == std::numeric_limits<std::size_t>::max()) {
        if (production_stack_.empty()) return;
        parent = production_stack_.back();
    }
    if (event >= production_events_.size() || parent >= production_events_.size() ||
        event == parent) return;
    auto& children = production_events_[parent].children;
    const auto found = std::find(children.begin(), children.end(), event);
    if (found == children.end()) return;
    const auto offset = static_cast<std::size_t>(found - children.begin());
    children.erase(found);
    const auto& nested = production_events_[event].children;
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(offset), nested.begin(), nested.end());
}

std::shared_ptr<const SyntaxNode> Parser::public_node(std::size_t event) const {
    NamespaceContextCacheScope context_cache(*this);
    if (event >= production_events_.size()) return {};
    const auto& source = production_events_[event];
    if (source.opaque) return source.opaque;
    if (source.first > source.end || source.end > tokens_.size() ||
        source.first >= tokens_.size()) return {};
    auto node = std::make_shared<SyntaxNode>();
    node->kind = SyntaxNode::Kind::Core;
    node->production = source.production;
    node->span = {token_origin(tokens_[source.first].location).span,
                  token_origin(tokens_[source.end == source.first ? source.first
                      : source.end - 1].location).last_span()};
    node->context = source.context;
    const auto token_node = [&](std::size_t at) -> std::shared_ptr<const SyntaxNode> {
        auto leaf = std::make_shared<SyntaxNode>();
        leaf->kind = SyntaxNode::Kind::Token;
        leaf->tokens.emplace_back(tokens_[at]);
        leaf->tokens.front().origin.context = public_token_context(tokens_[at].location, source.context);
        if (!leaf->tokens.front().origin.context) return {};
        leaf->tokens.front().origin.value_context_captured = true;
        leaf->span = {leaf->tokens.front().origin.span, leaf->tokens.front().origin.last_span()};
        leaf->context = leaf->tokens.front().origin.context;
        return leaf;
    };
    auto cursor = source.first;
    for (const auto child : source.children) {
        const auto& range = production_events_[child];
        if (range.first < cursor || range.first > range.end || range.end > source.end)
            return {};
        while (cursor < range.first) {
            auto leaf = token_node(cursor++);
            if (!leaf) return {};
            node->children.push_back(std::move(leaf));
        }
        auto built = public_node(child);
        if (!built) return {};
        if (range.opaque && range.end == range.first + 1 &&
            tokens_[range.first].kind == TokenKind::StructuredSplice &&
            (syntax_statement_node(*built) || syntax_declaration_node(*built) ||
             syntax_function_definition_node(*built)))
            node->splice_children.push_back(node->children.size());
        node->children.push_back(std::move(built));
        cursor = range.end;
    }
    while (cursor < source.end) {
        auto leaf = token_node(cursor++);
        if (!leaf) return {};
        node->children.push_back(std::move(leaf));
    }
    return node;
}

void Parser::record_balanced_sequence(std::size_t first, std::size_t end,
                                     SyntaxProduction root_production) {
    if (!recording_public_tree_ || public_tree_failed_ || first > end ||
        (first == end && root_production == SyntaxProduction::BalancedTokenSequence) ||
        end >= tokens_.size()) return;
    const auto saved_index = index_;
    index_ = first;
    // Raw public token productions are independent of typed interpretation;
    // in particular, quote contents never expose evaluated unquote operands.
    std::function<void(SyntaxProduction, std::string_view)> sequence;
    sequence = [&](SyntaxProduction production, std::string_view close) {
        ProductionScope scope(*this, production);
        while (index_ < end && !public_tree_failed_) {
            if (syntax_ && !syntax_->execution()->work(recognition_current().location)) {
                public_tree_failed_ = true;
                return;
            }
            const auto text = recognition_current().text;
            if (!close.empty() && text == close) return;
            if (text == "(" || text == "[" || text == "[[" || text == "{") {
                ProductionScope tree(*this, SyntaxProduction::BalancedTokenTree);
                ++index_;
                const auto expected = text == "(" ? ")" : text == "[" ? "]" : text == "[[" ? "]]" : "}";
                sequence(SyntaxProduction::BalancedTokens, expected);
                if (!public_tree_failed_ && index_ < end && recognition_current().is(expected)) ++index_;
                else if (!public_tree_failed_) {
                    error_here("unterminated balanced attribute token tree");
                    return;
                }
            } else if (text == ")" || text == "]" || text == "]]" || text == "}") {
                error_here("mismatched balanced attribute token-tree delimiter");
                return;
            } else ++index_;
        }
    };
    sequence(root_production, {});
    index_ = saved_index;
}

EvaluationTask<bool> Parser::recognize_declaration_splice_async(const Token& item,
    PublicDeclarationContext context) const {
    if (!item.splice || !syntax_ || !item.splice->context ||
        !item.splice->context->parse_environment) co_return false;
    const bool direct_function = context == PublicDeclarationContext::DirectFunction;
    if (item.splice->kind == SyntaxNode::Kind::Deferred &&
        (!direct_function || item.splice->deferred_category == SyntaxParseCategory::FunctionDeclaration))
        co_return true;
    // A settled public root retains lookup and identity, but does not waive
    // the requested scope's grammar. Reuse core recognition without executing
    // opaque descendants or publishing any speculative declarations.
    const auto execution = syntax_->execution();
    auto materialized = execution->materialize_node(*item.splice, item.location,
        item.splice->kind == SyntaxNode::Kind::Deferred
            ? item.splice->deferred_category : SyntaxParseCategory::None);
    if (!materialized) co_return false;
    std::ostringstream output;
    Diagnostics local(output);
    auto validator = replacement_parser(std::move(*materialized), &local);
    validator->restore_environment(*item.splice->context->parse_environment,
                                   *item.splice->context);
    if (context == PublicDeclarationContext::Block) {
        if (validator->local_scopes_.empty()) {
            validator->local_scopes_.emplace_back();
            validator->local_type_scopes_.emplace_back();
            validator->local_tag_scopes_.emplace_back();
            validator->scope_origins_.emplace_back();
            validator->scope_ends_.emplace_back();
        }
        validator->external_binding_depth_.reset();
    } else {
        // Keep captured local aliases/tags available to uses in a declaration
        // moved to file scope; only its grammar/binder placement is external.
        validator->external_binding_depth_ = validator->local_scopes_.size();
    }
    const auto classified = co_await validator->parse_syntax_fragment_async(direct_function
        ? SyntaxPatternElement::Kind::FunctionDeclaration : SyntaxPatternElement::Kind::Declaration, 0);
    co_return classified && classified->end + 1 == validator->tokens_.size();
}

std::optional<SyntaxParsedFragment> Parser::parse_syntax_fragment(
    SyntaxPatternElement::Kind kind, std::size_t first) const {
    return parse_syntax_fragment_async(kind, first).run();
}

EvaluationTask<std::optional<SyntaxParsedFragment>> Parser::parse_syntax_fragment_async(
    SyntaxPatternElement::Kind kind, std::size_t first) const {
    ResourceEpoch resource_epoch(*this);
    using K = SyntaxPatternElement::Kind;
    if (first >= tokens_.size() || tokens_[first].kind == TokenKind::End) co_return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    if (execution && !execution->begin_fragment(tokens_[first].location, tokens_.size())) co_return {};
    const auto previous_resources = execution ? execution->resource_errors() : 0;
    struct FragmentEnd {
        SyntaxExecution* execution;
        ~FragmentEnd() { if (execution) execution->end_fragment(); }
    } end{execution.get()};
    // Speculative alternatives must not emit diagnostics or mutate the owner's
    // parser. A child receives the same caller-side type and syntax environment.
    std::ostringstream output;
    Diagnostics local(output);
    auto child = replacement_parser({tokens_, tokens_[first].location}, &local);
    // Capture recognition may record generic static assertions, but it must
    // never append them to the actual caller. Only these context fields are
    // read by the statement parser; all speculative assertions stay private.
    FunctionDecl function_context;
    if (active_function_) {
        function_context.parameters = active_function_->parameters;
        function_context.generic_parameters = active_function_->generic_parameters;
        function_context.generic_tag_owner = active_function_->generic_tag_owner;
        function_context.function_scope = active_function_->function_scope;
        for (const auto& attribute : active_function_->attributes)
            if (attribute.name == "variadic") {
                auto& copied = function_context.attributes.emplace_back(attribute);
                for (auto& binding : copied.variadic_bindings)
                    binding.type = copy_type(binding.type);
            }
        child->active_function_ = &function_context;
    }
    child->index_ = first;
    child->parsing_public_fragment_ = true;
    child->scope_import_events_ = std::make_shared<std::vector<ScopeImportEvent>>(*scope_import_events_);
    child->recording_public_tree_ = true;
    child->public_input_indices_.reserve(tokens_.size());
    for (std::size_t at = 0; at < tokens_.size(); ++at)
        child->public_input_indices_.push_back(at);
    const auto origin = token_origin(tokens_[first].location);
    if (origin.context) {
        child->active_namespace_ = origin.context->name_space;
        const auto context = effective_context(tokens_[first].location);
        if (!context) co_return {};
        child->active_imports_ = context->imports;
        child->active_import_declarations_ = context->import_declarations;
    }
    const auto initial_context = child->public_fragment_context(first);
    child->allow_public_header_deferral_ = kind == K::FunctionHeader ||
        kind == K::FunctionDeclaration || kind == K::FunctionDefinition;
    const auto deferred_category = [&]() {
        switch (kind) {
        case K::Expr: return SyntaxParseCategory::Expression;
        case K::Type: return SyntaxParseCategory::Type;
        case K::Statement: return SyntaxParseCategory::Statement;
        case K::Declaration: return SyntaxParseCategory::Declaration;
        case K::FunctionHeader: return SyntaxParseCategory::FunctionHeader;
        case K::FunctionDeclaration: return SyntaxParseCategory::FunctionDeclaration;
        case K::FunctionDefinition: return SyntaxParseCategory::FunctionDefinition;
        default: return SyntaxParseCategory::None;
        }
    }();
    const auto deferred_slot = [&]() {
        switch (kind) {
        case K::Expr: return SyntaxProduction::AssignmentExpression;
        case K::Type: return SyntaxProduction::TypeName;
        case K::Statement: return SyntaxProduction::Statement;
        case K::Declaration:
        case K::FunctionDeclaration: return SyntaxProduction::Declaration;
        case K::FunctionHeader: return SyntaxProduction::FunctionHeader;
        case K::FunctionDefinition: return SyntaxProduction::FunctionDefinition;
        default: return SyntaxProduction::None;
        }
    }();
    std::shared_ptr<const SyntaxNode> deferred_root;
    const auto defer_root = [&](std::size_t fragment_end) {
        // Recognition may have annotated an earlier part before discovering
        // that the whole fragment needs to defer. Those provisional bindings
        // are not parsed input. Preserve incoming provenance, including real
        // structured children, but do not freeze a partial shared owner set or
        // a lookup learned from this abandoned semantic interpretation.
        for (auto at = first; at < fragment_end; ++at) {
            if (execution && !execution->work(child->tokens_[at].location)) return;
            const auto& input = tokens_[child->public_input_indices_[at]];
            auto& token = child->tokens_[at];
            token.value_binding = input.value_binding;
            token.value_spelling = input.value_spelling;
            token.fragment_lookup = input.fragment_lookup;
            token.tag_binding = input.tag_binding;
            token.alias_binding = input.alias_binding;
            token.label_binding = input.label_binding;
        }
        deferred_root = child->deferred_node(first, fragment_end, deferred_slot,
                                             deferred_category, initial_context);
    };
    bool deferred_recognition = false;
    try {
        const auto header_position = child->function_header_splice_position();
        const bool header_splice = header_position.has_value();
        if (kind == K::FunctionHeader && header_position == child->index_ &&
            !(co_await child->current_async(1)).is("[[")) {
            ProductionScope header(*child, SyntaxProduction::FunctionHeader);
            const auto item = (co_await child->current_async());
            ++child->index_;
            if (header.event < child->production_events_.size())
                child->production_events_[header.event].opaque = item.splice;
        } else if ((kind == K::Declaration || kind == K::FunctionDeclaration ||
             kind == K::FunctionDefinition) &&
            (co_await child->current_async()).kind == TokenKind::StructuredSplice && !header_splice &&
            (!(co_await child->current_async()).splice || !syntax_type_node(*(co_await child->current_async()).splice))) {
            ProductionScope declaration(*child, kind == K::FunctionDefinition
                ? SyntaxProduction::FunctionDefinition : SyntaxProduction::Declaration);
            const auto item = (co_await child->current_async());
            ++child->index_;
            if (!item.splice ||
                (kind == K::FunctionDefinition
                    ? !syntax_function_definition_node(*item.splice)
                    : !syntax_declaration_node(*item.splice))) co_return {};
            // A retained root does not bypass the requested parse context's
            // grammar restrictions, even when the inspected result is dropped.
            if (kind == K::Declaration && !child->binds_at_file_scope() &&
                item.splice->kind == SyntaxNode::Kind::Core &&
                item.splice->production == SyntaxProduction::GlobalLabelDeclaration) co_return {};
            if (kind != K::FunctionDefinition && !co_await child->recognize_declaration_splice_async(item,
                kind == K::FunctionDeclaration ? PublicDeclarationContext::DirectFunction
                    : child->binds_at_file_scope() ? PublicDeclarationContext::File
                                                  : PublicDeclarationContext::Block)) co_return {};
            if (declaration.event < child->production_events_.size())
                child->production_events_[declaration.event].opaque = item.splice;
        } else if (kind == K::Expr) {
            (void)co_await child->parse_assignment_async();
        } else if (kind == K::Type) {
            // A macro may introduce the type specifiers themselves. Its
            // output belongs after owner expansion, so retain the bounded
            // type fragment without executing or prematurely classifying it.
            const auto type_first = child->index_;
            while ((co_await child->current_async()).is("const") || (co_await child->current_async()).is("volatile") ||
                   (co_await child->current_async()).is("restrict") || (co_await child->current_async()).is("[[")) {
                if ((co_await child->current_async()).is("[[")) {
                    const auto group_end = child->bounded_group_end(child->index_);
                    if (!group_end) co_return {};
                    child->index_ = *group_end;
                } else ++child->index_;
            }
            const bool opaque_type = child->macro_start();
            child->index_ = type_first;
            if (opaque_type) throw DeferredNameRecognition{};
            ProductionScope scope(*child, SyntaxProduction::TypeName);
            if (!(co_await child->type_start_async())) co_return {};
            auto type = co_await child->parse_type_async();
            std::optional<std::string> name;
            (void)co_await child->parse_declarator_async(std::move(type), name, DeclaratorContext::TypeName);
            if (name) co_return {};
        } else if (kind == K::Statement) {
            auto statement = co_await child->parse_statement_async();
            if (statement) child->bind_label_references(*statement);
        } else if (kind == K::Declaration && (co_await child->current_async()).is("using")) {
            (void)co_await child->parse_using_declaration_async(child->binds_at_file_scope());
        } else if (kind == K::Declaration && (co_await child->current_async()).is("$::static_assert")) {
            Statement statement;
            (void)co_await child->parse_static_assertion_async(child->binds_at_file_scope() ? nullptr : &statement);
        } else if (kind == K::Declaration && !child->binds_at_file_scope()) {
            // The public declaration category has the same root at either
            // scope, but a block admits register/stack and local typedefs.
            if (!(co_await child->local_declaration_start_async())) co_return {};
            (void)co_await child->parse_local_declaration_async({}, true,
                                                 SyntaxProduction::Declaration);
        } else if (kind == K::Declaration || kind == K::FunctionHeader ||
                   kind == K::FunctionDeclaration || kind == K::FunctionDefinition) {
            // These categories exclude namespaces, registration, and invocations
            // standing in place of the direct declaration/header itself.
            if ((co_await child->current_async()).is("namespace") ||
                (co_await child->current_async()).is("syntax") || child->macro_start() ||
                child->active_syntax(true)) co_return {};
            if (kind == K::Declaration) {
                bool pending_header_bindings = false;
                (void)co_await child->preview_generic_types_async(&pending_header_bindings);
                if (child->public_tree_failed_) co_return {};
                if (pending_header_bindings) {
                    // Prototypes are declarations too. Before ordinary recognition can
                    // bind an earlier type to an alias that a generated generic may
                    // shadow, try the classifier which proves a direct function while
                    // deferring bounded header fragments. Keep its stronger deferred
                    // category (with the same declaration root). Non-functions still
                    // use ordinary declaration lookup, never provisional header types.
                    // Both attempts are isolated, non-executing and resource-accounted.
                    const auto errors = diagnostics_.errors();
                    auto prototype = co_await parse_syntax_fragment_async(K::FunctionDeclaration, first);
                    if (diagnostics_.errors() != errors ||
                        (execution && execution->resource_errors() != previous_resources)) co_return {};
                    if (prototype) co_return prototype;
                }
            }
            child->parsing_public_function_header_ = kind == K::FunctionHeader;
            Program parsed;
            co_await child->parse_external_async(parsed, child->active_namespace_);
            const bool direct_function = parsed.functions.size() == 1 &&
                parsed.objects.empty() && parsed.records.empty() &&
                parsed.enumerations.empty() && parsed.global_labels.empty();
            if (kind == K::FunctionHeader) {
                if (!direct_function || (!(co_await child->current_async()).is(";") &&
                                         !(co_await child->current_async()).is("{") &&
                                         (co_await child->current_async()).kind != TokenKind::End)) co_return {};
            } else if (kind == K::FunctionDeclaration) {
                if (!direct_function || parsed.functions.front()->body) co_return {};
            } else if (kind == K::FunctionDefinition) {
                if (!direct_function || !parsed.functions.front()->body) co_return {};
            } else if (std::any_of(parsed.functions.begin(), parsed.functions.end(),
                       [](const auto& function) { return function->body != nullptr; })) co_return {};
        } else {
            co_return {};
        }
        if (child->public_deferred_header_) defer_root(child->index_);
    } catch (const DeferredNameRecognition&) {
        deferred_recognition = true;
    }
    if (deferred_recognition) {
        // A nested capture can inherit uncertainty from an earlier opaque
        // invocation in its enclosing block. Prove its boundary lexically,
        // independently of the unresolved type/generic lookup.
        std::optional<std::size_t> fragment_end;
        switch (kind) {
        case K::Expr:
            fragment_end = child->fenced_fragment_end(first, true);
            break;
        case K::Type:
            fragment_end = child->fenced_fragment_end(first, false);
            break;
        case K::Statement:
            fragment_end = co_await child->bounded_statement_end_async(first);
            break;
        case K::Declaration:
            fragment_end = co_await child->bounded_declaration_end_async(first);
            break;
        default: co_return {};
        }
        if (!fragment_end) co_return {};
        defer_root(*fragment_end);
        child->index_ = *fragment_end;
    }
    if (execution && execution->resource_errors() != previous_resources) co_return {};
    if (child->public_tree_failed_) {
        if (!execution) diagnostics_.error(tokens_[first].location,
            "public syntax tree depth, work, or storage budget exceeded");
        co_return {};
    }
    if (local.errors() != 0 || child->index_ <= first ||
        (!deferred_root && child->production_events_.empty())) co_return {};
    if (kind == K::Expr || kind == K::Type) {
        const auto& next = (co_await child->current_async());
        if (next.kind != TokenKind::End && !next.is(";") && !next.is(",") &&
            !next.is(")") && !next.is("]") && !next.is("]]") &&
            !next.is("}")) co_return {};
    }
    auto node = deferred_root ? std::move(deferred_root) : child->public_node(0);
    if (!node) co_return {};
    std::string shape_error;
    std::uint64_t validation_work{};
    SyntaxTreeValidationError failure{};
    const auto valid = syntax_validate_node(*node, shape_error,
        execution ? execution->limits() : EvaluationLimits{}, &validation_work, &failure);
    if (execution && !execution->work(tokens_[first].location, validation_work)) co_return {};
    if (!valid) {
        if (failure == SyntaxTreeValidationError::DepthLimit ||
            failure == SyntaxTreeValidationError::WorkLimit) {
            if (execution) execution->tree_limit_error(tokens_[first].location);
            else diagnostics_.error(tokens_[first].location, shape_error);
        }
        co_return {};
    }
    co_return SyntaxParsedFragment{child->public_input_indices_[child->index_], std::move(node)};
}

std::shared_ptr<const SyntaxNode> Parser::parse_syntax_tokens(
    SyntaxParseCategory category, std::vector<Token> input) const {
    return parse_syntax_tokens_async(category, std::move(input)).run();
}

EvaluationTask<std::shared_ptr<const SyntaxNode>> Parser::parse_syntax_tokens_async(
    SyntaxParseCategory category, std::vector<Token> input) const {
    ResourceEpoch resource_epoch(*this);
    using C = SyntaxParseCategory;
    using K = SyntaxPatternElement::Kind;
    K kind;
    switch (category) {
    case C::Expression: kind = K::Expr; break;
    case C::Statement: kind = K::Statement; break;
    case C::Type: kind = K::Type; break;
    case C::Declaration: kind = K::Declaration; break;
    case C::FunctionHeader: kind = K::FunctionHeader; break;
    case C::FunctionDeclaration: kind = K::FunctionDeclaration; break;
    case C::FunctionDefinition: kind = K::FunctionDefinition; break;
    case C::None: co_return {};
    default: co_return {};
    }
    if (input.size() < 2 || input.back().kind != TokenKind::End ||
        std::any_of(input.begin(), input.end() - 1,
                    [](const Token& token) { return token.kind == TokenKind::End; })) co_return {};
    const auto end = input.size() - 1;
    const auto location = input.front().location;
    auto child = replacement_parser({std::move(input), location});
    const auto fragment = co_await child->parse_syntax_fragment_async(kind, 0);
    if (!fragment || fragment->end != end) co_return {};
    co_return fragment->node;
}

void Parser::require_public_name_context(std::string_view name, SourceLocation location,
                                         PublicNameDomain domain) const {
    if (!parsing_public_fragment_ || name.empty() || is_reserved_identifier(name)) return;
    if (probing_header_type_) return;
    if (domain == PublicNameDomain::Ordinary && name.find("::") == std::string_view::npos &&
        (public_header_uncertain_names_ || (header_bindings_ && !header_bindings_->complete)))
        throw DeferredNameRecognition{};
    if (public_uncertain_binding_depths_.empty()) return;
    if (name.find("::") == std::string_view::npos) {
        const NameKey key(name, location.valid() ? location : current().location);
        for (auto depth = local_scopes_.size(); depth != 0; --depth) {
            if (domain == PublicNameDomain::Tag) {
                if (!local_tag_scopes_[depth - 1].contains(key)) continue;
            } else if (!local_scopes_[depth - 1].contains(key) &&
                       !local_type_scopes_[depth - 1].contains(key)) continue;
            // A closer or same-scope established binding cannot be legally
            // retargeted by an opaque declaration in an outer/same scope.
            if (std::none_of(public_uncertain_binding_depths_.begin(),
                            public_uncertain_binding_depths_.end(),
                            [&](std::size_t uncertain) { return uncertain > depth; })) return;
            break;
        }
    }
    throw DeferredNameRecognition{};
}

void Parser::mark_public_binding_uncertainty() {
    const auto depth = local_scopes_.size();
    if (std::find(public_uncertain_binding_depths_.begin(),
                  public_uncertain_binding_depths_.end(), depth) ==
        public_uncertain_binding_depths_.end())
        public_uncertain_binding_depths_.push_back(depth);
}

std::optional<std::size_t> Parser::bounded_group_end(std::size_t first) {
    const auto closer = [](std::string_view text) -> std::string_view {
        if (text == "(") return ")";
        if (text == "[") return "]";
        if (text == "[[") return "]]";
        if (text == "{") return "}";
        return {};
    };
    if (first >= tokens_.size() || closer(tokens_[first].text).empty()) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    std::vector<std::string_view> stack;
    for (auto at = first; at < tokens_.size() && tokens_[at].kind != TokenKind::End; ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        const auto text = tokens_[at].text;
        if (const auto close = closer(text); !close.empty()) {
            if (stack.size() >= limits.depth) {
                public_tree_failed_ = true;
                if (execution) execution->tree_limit_error(tokens_[at].location);
                return {};
            }
            stack.push_back(close);
        } else if (text == ")" || text == "]" || text == "]]" || text == "}") {
            if (stack.empty() || stack.back() != text) return {};
            stack.pop_back();
            if (stack.empty()) return at + 1;
        }
    }
    return {};
}

std::optional<std::size_t> Parser::fenced_fragment_end(std::size_t first, bool expression) {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    // Explicit ::< selects generic grammar without a name lookup. Bare <
    // does not: a comma before its possible closer may be either an argument
    // separator or this capture's fence, so that boundary cannot be guessed.
    std::vector<bool> explicit_angles;
    for (auto at = first; at < tokens_.size(); ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        const auto text = tokens_[at].text;
        const bool closing = text == ")" || text == "]" || text == "]]" || text == "}";
        if (tokens_[at].kind == TokenKind::End || text == ";" || closing) {
            if (at == first || (!explicit_angles.empty() && explicit_angles.front())) return {};
            return at;
        }
        if (text == ",") {
            if (explicit_angles.empty()) return at == first ? std::optional<std::size_t>{} : at;
            if (!explicit_angles.front()) return {};
        }
        if (text == "(" || text == "[" || text == "[[" || text == "{") {
            const auto end = bounded_group_end(at);
            if (!end) return {};
            at = *end - 1;
        } else if (expression && text == "<") {
            if (explicit_angles.size() >= limits.depth) {
                public_tree_failed_ = true;
                if (execution) execution->tree_limit_error(tokens_[at].location);
                return {};
            }
            explicit_angles.push_back(at != first && tokens_[at - 1].is("::"));
        } else if (expression && (text == ">" || text == ">>")) {
            unsigned count = text == ">>" ? 2 : 1;
            while (count-- && !explicit_angles.empty()) explicit_angles.pop_back();
        }
    }
    return {};
}

EvaluationTask<std::optional<std::size_t>> Parser::bounded_declaration_end_async(std::size_t first) {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    for (auto at = first; at < tokens_.size(); ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            co_return {};
        }
        const auto& token = tokens_[at];
        if (token.kind == TokenKind::End || token.is("}") || token.is(")") ||
            token.is("]") || token.is("]]") || token.is("else") || token.is("co_return") ||
            token.is("if") || token.is("for") || token.is("while") || token.is("do") ||
            token.is("switch")) co_return {};
        if (token.is(";")) co_return at + 1;
        // Written inline tag definitions are part of the type specifiers, not
        // function bodies. Only their qualified name and balanced attributes
        // may intervene before the defining brace; an opaque name is not proof.
        if (token.is("struct") || token.is("union") || token.is("enum")) {
            auto tag_end = at + 1;
            if (tag_end < tokens_.size() && tokens_[tag_end].kind == TokenKind::Identifier) {
                ++tag_end;
                while (tag_end + 1 < tokens_.size() && tokens_[tag_end].is("::") &&
                       tokens_[tag_end + 1].kind == TokenKind::Identifier) {
                    if (execution && !execution->work(tokens_[tag_end].location, 2)) {
                        public_tree_failed_ = true;
                        co_return {};
                    }
                    tag_end += 2;
                }
            }
            while (tag_end < tokens_.size() && tokens_[tag_end].is("[[")) {
                const auto end = bounded_group_end(tag_end);
                if (!end) co_return {};
                tag_end = *end;
            }
            if (tag_end < tokens_.size() && tokens_[tag_end].is("{")) {
                const auto end = bounded_group_end(tag_end);
                if (!end) co_return {};
                at = *end - 1;
                continue;
            }
        }
        if (token.is("=")) {
            const auto value = at + 1;
            if (value >= tokens_.size()) co_return {};
            if (tokens_[value].is("{")) {
                const auto end = bounded_group_end(value);
                if (!end) co_return {};
                at = *end - 1;
            } else {
                // Expression syntax can own commas, semicolons and brace
                // groups. Reuse its bounded non-executing recognition rather
                // than mistaking its input for another declarator or body.
                const auto parsed = co_await parse_syntax_fragment_async(SyntaxPatternElement::Kind::Expr, value);
                if (!parsed) co_return {};
                at = parsed->end - 1;
            }
            continue;
        }
        if (token.is("{")) {
            // A brace macro input is independently bounded. Otherwise a brace
            // needs a written initializer: it may be a function body, and a
            // later semicolon can belong to the *next* source declaration.
            if (!(at > first && tokens_[at - 1].is("!"))) co_return {};
        }
        if (token.is("(") || token.is("[") || token.is("[[") || token.is("{")) {
            const auto end = bounded_group_end(at);
            if (!end) co_return {};
            at = *end - 1;
        }
    }
    co_return {};
}

EvaluationTask<std::optional<std::size_t>> Parser::bounded_statement_end_async(std::size_t first, unsigned depth) {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    if (first >= tokens_.size() || tokens_[first].kind == TokenKind::End) co_return {};
    if (depth >= limits.depth) {
        public_tree_failed_ = true;
        if (execution) execution->tree_limit_error(tokens_[first].location);
        co_return {};
    }
    if (execution && !execution->work(tokens_[first].location)) {
        public_tree_failed_ = true;
        co_return {};
    }
    if (tokens_[first].kind == TokenKind::StructuredSplice) {
        const auto& item = tokens_[first];
        if (!item.splice) co_return {};
        if (syntax_statement_node(*item.splice) || syntax_declaration_node(*item.splice)) {
            // A retained statement/declaration is one complete unit even if
            // an earlier name-dependent condition forced this boundary probe.
            // Never scan its display token for a semicolon or attach an else
            // to an if hidden inside the retained subtree.
            if (!item.splice->context || !item.splice->context->parse_environment) co_return {};
            if (syntax_declaration_node(*item.splice)) {
                if (item.splice->kind == SyntaxNode::Kind::Core &&
                    (item.splice->production == SyntaxProduction::UsingDeclaration ||
                     item.splice->production == SyntaxProduction::GlobalLabelDeclaration)) co_return {};
                if (!(co_await recognize_declaration_splice_async(item, PublicDeclarationContext::Block))) co_return {};
            }
            co_return first + 1;
        }
        // Expressions and complete type aliases may compose with following
        // source; other categories cannot occupy a statement position.
        if (!syntax_expression_node(*item.splice) && !syntax_type_node(*item.splice)) co_return {};
    }
    auto at = first;
    while (tokens_[at].is("[[")) {
        const auto end = bounded_group_end(at);
        if (!end || *end >= tokens_.size()) co_return {};
        at = *end;
    }
    if (tokens_[at].is("{")) co_return bounded_group_end(at);
    const auto head = tokens_[at].text;
    if (head == "if" || head == "switch" || head == "while" || head == "for") {
        if (at + 1 >= tokens_.size() || !tokens_[at + 1].is("(")) co_return {};
        const auto condition_end = bounded_group_end(at + 1);
        if (!condition_end) co_return {};
        auto end = (co_await bounded_statement_end_async(*condition_end, depth + 1));
        if (end && head == "if" && *end < tokens_.size() && tokens_[*end].is("else"))
            end = (co_await bounded_statement_end_async(*end + 1, depth + 1));
        co_return end;
    }
    if (head == "do") {
        const auto body_end = (co_await bounded_statement_end_async(at + 1, depth + 1));
        if (!body_end || *body_end + 1 >= tokens_.size() ||
            !tokens_[*body_end].is("while") || !tokens_[*body_end + 1].is("(")) co_return {};
        const auto condition_end = bounded_group_end(*body_end + 1);
        if (!condition_end || *condition_end >= tokens_.size() ||
            !tokens_[*condition_end].is(";")) co_return {};
        co_return *condition_end + 1;
    }
    if (head == "global" && at + 3 < tokens_.size() && tokens_[at + 1].is("label") &&
        tokens_[at + 2].kind == TokenKind::Identifier && tokens_[at + 3].is(":"))
        co_return (co_await bounded_statement_end_async(at + 4, depth + 1));
    if (head == "label" && at + 2 < tokens_.size() &&
        tokens_[at + 1].kind == TokenKind::Identifier && tokens_[at + 2].is(":"))
        co_return (co_await bounded_statement_end_async(at + 3, depth + 1));
    if (tokens_[at].kind == TokenKind::Identifier && at + 1 < tokens_.size() &&
        tokens_[at + 1].is(":")) co_return (co_await bounded_statement_end_async(at + 2, depth + 1));
    if (head == "case") {
        unsigned conditional_depth{};
        for (++at; at < tokens_.size(); ++at) {
            if (execution && !execution->work(tokens_[at].location)) {
                public_tree_failed_ = true;
                co_return {};
            }
            const auto text = tokens_[at].text;
            if (tokens_[at].kind == TokenKind::End || text == ";" || text == "}") co_return {};
            if (text == "(" || text == "[" || text == "[[" || text == "{") {
                const auto end = bounded_group_end(at);
                if (!end) co_return {};
                at = *end - 1;
            } else if (text == "?") ++conditional_depth;
            else if (text == ":") {
                if (conditional_depth) --conditional_depth;
                else co_return (co_await bounded_statement_end_async(at + 1, depth + 1));
            }
        }
        co_return {};
    }
    // An already-active syntax prefix or explicit macro token-tree is bounded
    // by its own pattern/group. Recognition remains read-only; no expander is
    // run to discover a statement's endpoint.
    struct RestoreProbe {
        Parser& parser;
        std::size_t index;
        bool recording;
        ~RestoreProbe() { parser.index_ = index; parser.recording_public_tree_ = recording; }
    } restore{*this, index_, recording_public_tree_};
    index_ = at;
    recording_public_tree_ = false;
    const auto unit_first = at;
    const auto* definition = active_syntax(false);
    if (definition && definition->kind == SyntaxKind::Statement) {
        if (!(co_await parse_opaque_invocation_async(SyntaxKind::Statement))) co_return {};
        co_return index_;
    }
    if (macro_start()) {
        if (!(co_await parse_opaque_invocation_async(SyntaxKind::Statement))) co_return {};
        if (!opaque_statement_has_expression_continuation()) co_return index_;
    }
    index_ = unit_first;
    try {
        struct RawLookahead {
            unsigned& depth;
            explicit RawLookahead(unsigned& value) : depth(value) { ++depth; }
            ~RawLookahead() { --depth; }
        } raw{raw_token_depth_};
        if ((co_await local_declaration_start_async())) co_return (co_await bounded_declaration_end_async(unit_first));
    } catch (const DeferredNameRecognition&) {
        // An unknown leading name can still belong to an expression. Keep the
        // general statement boundary path when declaration lookup is unsettled.
    }
    enum class Tail { Unknown, Expression, Initializer };
    auto tail = head == "co_return" || head == "goto" ? Tail::Expression : Tail::Unknown;
    for (; at < tokens_.size(); ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            co_return {};
        }
        const auto text = tokens_[at].text;
        if (tokens_[at].kind == TokenKind::End || text == "}" || text == ")" ||
            text == "]" || text == "]]" || text == "else" ||
            (at != unit_first && (text == "if" || text == "for" || text == "while" ||
                             text == "do" || text == "switch" || text == "co_return"))) co_return {};
        if (text == ";") co_return at + 1;
        if (text == "=" && tail == Tail::Unknown) tail = Tail::Initializer;
        else if (text == "," && tail == Tail::Initializer) tail = Tail::Unknown;
        else if (tail == Tail::Unknown &&
            (text == "+" || text == "-" || text == "/" || text == "%" ||
             text == "^" || text == "|" || text == "&&" || text == "||" ||
             text == "==" || text == "!=" || text == "?" || text == "++" || text == "--" ||
             text == "+=" || text == "-=" || text == "*=" || text == "/=" || text == "%=" ||
             text == "<<=" || text == ">>=" || text == "&=" || text == "^=" || text == "|="))
            tail = Tail::Expression;
        index_ = at;
        if (const auto* expression = active_syntax(false);
            expression && expression->kind == SyntaxKind::Expression) {
            // An expression extension owns its complete matched input, which
            // may itself contain semicolons or brace groups. Its owner remains
            // unexecuted; those tokens cannot fence the containing statement.
            const auto context = public_fragment_context(at);
            if (!context) co_return {};
            const auto errors = diagnostics_.errors();
            const auto resources = execution ? execution->resource_errors() : 0;
            const auto matched = co_await syntax_->match_async(*expression, tokens_, at, diagnostics_,
                [&](SyntaxPatternElement::Kind kind, std::size_t begin) {
                    return parse_syntax_fragment_async(kind, begin);
                }, [&](SourceLocation location) {
                    return token_origin(location).context ? syntax_context(location) : context;
                }, tail == Tail::Unknown ? SyntaxState::MatchMode::Probe : SyntaxState::MatchMode::Required);
            if (diagnostics_.errors() != errors ||
                (execution && execution->resource_errors() != resources)) co_return {};
            if (!matched) {
                if (tail == Tail::Unknown) continue; // Ordinary declarator-name alternative.
                co_return {};
            }
            if (tail == Tail::Unknown) {
                // A matching prefix may also be a declarator name. Accept
                // only input whose groups have the same independent boundary
                // in either interpretation, never a possible function body or
                // an expression-owned semicolon that could end a declaration.
                for (auto part = at + 1; part < matched->end; ++part) {
                    if (execution && !execution->work(tokens_[part].location)) {
                        public_tree_failed_ = true;
                        co_return {};
                    }
                    const auto spelling = tokens_[part].text;
                    if (spelling == ";" || (spelling == "{" &&
                        !tokens_[part - 1].is("!") && !tokens_[part - 1].is("$::quote") &&
                        !tokens_[part - 1].is("="))) co_return {};
                    if (spelling == "(" || spelling == "[" || spelling == "[[" || spelling == "{") {
                        const auto end = bounded_group_end(part);
                        if (!end || *end > matched->end) co_return {};
                        part = *end - 1;
                    }
                }
            }
            at = matched->end - 1;
            continue;
        }
        if (text == "{" && !(at > unit_first &&
            (tokens_[at - 1].is("!") || tokens_[at - 1].is("$::quote") ||
             tokens_[at - 1].is("=")))) {
            // Unknown leading output may be an expression, declaration or
            // control prefix. Only an independently owned brace can be interior
            // input: skipping a possible body would absorb following source.
            co_return {};
        }
        if (text == "(" || text == "[" || text == "[[" || text == "{") {
            const auto end = bounded_group_end(at);
            if (!end) co_return {};
            at = *end - 1;
        }
    }
    co_return {};
}

std::shared_ptr<const SyntaxContext> Parser::public_fragment_context(std::size_t first) const {
    return syntax_context(tokens_[first].location);
}

std::shared_ptr<const SyntaxContext> Parser::public_token_context(SourceLocation location,
    const std::shared_ptr<const SyntaxContext>& parsed_context) const {
    const auto origin = token_origin(location);
    if (!origin.context) return parsed_context;
    // Explicit parsing resets raw-token lookup to its selected root context,
    // but declarations/scopes introduced by that very parse must also be
    // visible to delayed macro input. A settled spliced child's context is
    // never replaced by the receiving parser's environment.
    if (explicit_parse_context_ && origin.context == explicit_parse_context_ &&
        !origin.value_context_captured) return parsed_context;
    // Retain imports from the original block even if projection removes the
    // surrounding using declaration.
    const auto retained = effective_context(location, parsed_context ? parsed_context->parse_environment.get() : nullptr);
    if (!retained || !retained->parse_environment || origin.value_context_captured) return retained;
    const auto lookup = fragment_lookup({}, location);
    if (!lookup) return retained;
    // A public token's context is independently observable. Save its namespace
    // view as well as its binding, without exposing the receiver's locals.
    const auto environment = refine_namespace_environment(*retained->parse_environment, lookup, location);
    if (!environment) return {};
    auto context = std::make_shared<SyntaxContext>(*retained);
    context->parse_environment = environment;
    return context;
}

std::shared_ptr<const SyntaxNode> Parser::deferred_node(
    std::size_t first, std::size_t end, SyntaxProduction slot,
    SyntaxParseCategory category, std::shared_ptr<const SyntaxContext> context) {
    NamespaceContextCacheScope context_cache(*this);
    if (first >= end || end > tokens_.size()) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    auto node = std::make_shared<SyntaxNode>();
    node->kind = SyntaxNode::Kind::Deferred;
    node->slot_production = slot;
    node->deferred_category = category;
    node->span = {token_origin(tokens_[first].location).span,
                  token_origin(tokens_[end - 1].location).last_span()};
    node->context = std::move(context);
    std::shared_ptr<const TokenRegion> parameter_region;
    if ((category == SyntaxParseCategory::FunctionHeader ||
         category == SyntaxParseCategory::FunctionDeclaration ||
         category == SyntaxParseCategory::Declaration) && end < tokens_.size()) {
        parameter_region = std::make_shared<const TokenRegion>(TokenRegion{
            token_origin(tokens_[first].location).identity,
            token_origin(tokens_[end].location).identity});
    }
    if (category == SyntaxParseCategory::FunctionDefinition && tokens_[end - 1].is("}")) {
        unsigned braces = 0;
        for (auto at = end; at > first;) {
            --at;
            if (execution && !execution->work(tokens_[at].location)) {
                public_tree_failed_ = true;
                return {};
            }
            if (tokens_[at].is("}")) ++braces;
            else if (tokens_[at].is("{") && --braces == 0) {
                parameter_region = std::make_shared<const TokenRegion>(TokenRegion{
                    token_origin(tokens_[first].location).identity,
                    token_origin(tokens_[at].location).identity});
                break;
            }
        }
    }
    std::uint64_t storage = 128;
    const auto maximum = std::min(limits.bytes, limits.memory);
    const auto charge = [&](std::uint64_t bytes) {
        if (bytes > maximum - std::min(storage, maximum)) return false;
        storage += bytes;
        return true;
    };
    for (auto at = first; at < end; ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        MetaToken token(tokens_[at]);
        token.origin.context = public_token_context(tokens_[at].location, node->context);
        if (!token.origin.context) { public_tree_failed_ = true; return {}; }
        if (!retain_fragment_lookup(token, at, end)) { public_tree_failed_ = true; return {}; }
        if (!token.origin.value_context_captured)
            token.origin.deferred_parameter_region = parameter_region;
        token.origin.value_context_captured = true;
        if (!charge(meta_token_storage_bytes) || !charge(token.text.size()) ||
            !charge(origin_binding_storage(token.origin))) {
            public_tree_failed_ = true;
            if (execution) execution->tree_limit_error(tokens_[at].location);
            return {};
        }
        node->tokens.push_back(std::move(token));
    }
    return node;
}

bool Parser::opaque_statement_has_expression_continuation() const {
    const auto suffix = current().text;
    return precedence(suffix) > 0 || suffix == "=" || suffix == "+=" || suffix == "-=" ||
        suffix == "*=" || suffix == "/=" || suffix == "%=" || suffix == "<<=" ||
        suffix == ">>=" || suffix == "&=" || suffix == "^=" || suffix == "|=" ||
        suffix == "?" || suffix == "(" || suffix == "[" || suffix == "." ||
        suffix == "->" || suffix == "++" || suffix == "--" ||
        (suffix == "::" && current(1).is("<"));
}

EvaluationTask<std::shared_ptr<const SyntaxNode>> Parser::parse_opaque_invocation_async(SyntaxKind category) {
    NamespaceContextCacheScope context_cache(*this);
    if (!syntax_) co_return {};
    const auto first = index_;
    const auto context = public_fragment_context(first);
    if (!context) co_return {};
    auto node = std::make_shared<SyntaxNode>();
    node->slot_production = category == SyntaxKind::Expression
        ? SyntaxProduction::PrimaryExpression
        : category == SyntaxKind::Statement
            ? SyntaxProduction::UnattributedStatement
            : SyntaxProduction::Declaration;
    if (macro_start()) {
        node->kind = SyntaxNode::Kind::Macro;
        ++index_;
        while ((co_await current_async()).is("::")) index_ += 2;
        (co_await expect_async("!"));
        const auto opening = (co_await current_async()).text;
        if (opening != "(" && opening != "[" && opening != "{") co_return {};
        std::vector<std::string_view> closers;
        do {
            if (!syntax_->execution()->work((co_await current_async()).location)) co_return {};
            const auto text = (co_await current_async()).text;
            if (text == "(" || text == "[" || text == "[[" || text == "{") {
                if (closers.size() >= syntax_->execution()->limits().depth) {
                    syntax_->execution()->tree_limit_error((co_await current_async()).location);
                    public_tree_failed_ = true;
                    (co_await error_here_async("opaque macro token-tree depth exceeded"));
                    co_return {};
                }
                closers.push_back(text == "(" ? ")" : text == "[" ? "]" : text == "[[" ? "]]" : "}");
            } else if (text == ")" || text == "]" || text == "]]" || text == "}") {
                if (closers.empty() || closers.back() != text) {
                    (co_await error_here_async("mismatched opaque macro token-tree delimiter"));
                    co_return {};
                }
                closers.pop_back();
            }
            ++index_;
        } while (!closers.empty() && (co_await current_async()).kind != TokenKind::End);
        if (!closers.empty()) {
            (co_await error_here_async("unterminated opaque macro token tree"));
            co_return {};
        }
    } else {
        const auto* definition = active_syntax(category == SyntaxKind::Item);
        if (!definition) co_return {};
        if (definition->kind != category) {
            (co_await error_here_async("opaque syntax invocation has the wrong surrounding category"));
            ++index_;
            co_return {};
        }
        const auto matched = co_await syntax_->match_async(*definition, tokens_, index_, diagnostics_,
            [&](SyntaxPatternElement::Kind kind, std::size_t begin) {
                return parse_syntax_fragment_async(kind, begin);
            }, [&](SourceLocation at) { return token_origin(at).context ? syntax_context(at) : context; });
        if (!matched) co_return {};
        node->kind = SyntaxNode::Kind::Extension;
        node->definition = definition->id;
        node->match = matched->value;
        index_ = matched->end;
    }
    node->span = {token_origin(tokens_[first].location).span,
                  token_origin(tokens_[index_ - 1].location).last_span()};
    node->context = context;
    for (auto at = first; at < index_; ++at) {
        MetaToken token(tokens_[at]);
        token.origin.context = public_token_context(tokens_[at].location, node->context);
        if (!token.origin.context) { public_tree_failed_ = true; co_return {}; }
        if (!retain_fragment_lookup(token, at, index_)) { public_tree_failed_ = true; co_return {}; }
        token.origin.value_context_captured = true;
        node->tokens.push_back(std::move(token));
    }
    if (recording_public_tree_) {
        const auto event = production_events_.size();
        production_events_.push_back({SyntaxProduction::None, first, index_, {}, node, node->context});
        if (!production_stack_.empty())
            production_events_[production_stack_.back()].children.push_back(event);
    }
    co_return node;
}

void Parser::adopt_replacement(Parser& child) {
    // Bounds parsed in a semantic specifier view still need the owning
    // function's completed value-parameter bindings, just like local parses.
    header_type_bounds_.insert(child.header_type_bounds_.begin(), child.header_type_bounds_.end());
    syntax_ = std::move(child.syntax_);
    active_imports_ = std::move(child.active_imports_);
    active_import_declarations_ = std::move(child.active_import_declarations_);
    current_scope_imports_ = child.current_scope_imports_;
    known_functions_ = std::move(child.known_functions_);
    known_ordinary_values_ = std::move(child.known_ordinary_values_);
    local_scopes_ = std::move(child.local_scopes_);
    local_type_scopes_ = std::move(child.local_type_scopes_);
    local_tag_scopes_ = std::move(child.local_tag_scopes_);
    value_rebindings_ = std::move(child.value_rebindings_);
    tag_rebindings_ = std::move(child.tag_rebindings_);
    alias_rebindings_ = std::move(child.alias_rebindings_);
    scope_origins_ = std::move(child.scope_origins_);
    scope_ends_ = std::move(child.scope_ends_);
    scope_placement_ = std::move(child.scope_placement_);
    fragment_namespaces_ = std::move(child.fragment_namespaces_);
    fragment_namespace_placements_ = std::move(child.fragment_namespace_placements_);
    enum_types_ = std::move(child.enum_types_);
    pending_enumerations_.insert(pending_enumerations_.end(),
        std::make_move_iterator(child.pending_enumerations_.begin()),
        std::make_move_iterator(child.pending_enumerations_.end()));
    pending_records_.insert(pending_records_.end(),
        std::make_move_iterator(child.pending_records_.begin()),
        std::make_move_iterator(child.pending_records_.end()));
    required_types_.insert(required_types_.end(),
        std::make_move_iterator(child.required_types_.begin()),
        std::make_move_iterator(child.required_types_.end()));
    record_types_ = std::move(child.record_types_);
    type_aliases_ = std::move(child.type_aliases_);
    declared_aliases_.insert(declared_aliases_.end(),
        std::make_move_iterator(child.declared_aliases_.begin()),
        std::make_move_iterator(child.declared_aliases_.end()));
    switch_default_seen_ = std::move(child.switch_default_seen_);
    for (auto& assertion : child.static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

Parser::TagState Parser::tag_state() const {
    return {record_types_, local_tag_scopes_.empty() ? NameMap<LocalTag>{}
                                                    : local_tag_scopes_.back(),
            local_scopes_.empty() ? NameMap<ValueBinding>{} : local_scopes_.back(),
            local_tag_scopes_.size()};
}

void Parser::prepare_tag_destination(const Parser& destination) {
    declaration_destination_ = &destination;
    generic_tag_owner_ = destination.generic_tag_owner_;
    function_scope_ = destination.function_scope_;
    tag_destination_depth_ = local_tag_scopes_.size();
    external_binding_depth_ = destination.binds_at_file_scope()
        ? std::optional(local_scopes_.size()) : std::nullopt;
    if (!destination.local_tag_scopes_.empty())
        tag_destination_ = destination.local_tag_scopes_.back();
    else tag_destination_.reset();
}

bool Parser::transfer_spliced_tags(
    Parser& child, const TagState& before,
    SourceLocation location) {
    const auto& prior_records = before.records;
    const auto previous_errors = diagnostics_.errors();
    NameMap<LocalTag> transferred;
    NameMap<ValueBinding> transferred_enumerators;
    if (before.depth && child.local_tag_scopes_.size() == before.depth)
        for (const auto& [key, tag] : child.local_tag_scopes_.back()) {
            const auto prior = before.local.find(key);
            if (prior != before.local.end() && prior->second.complete == tag.complete &&
                same_type(prior->second.type, tag.type)) continue;
            if (local_tag_scopes_.empty()) {
                diagnostics_.error(location, "moving a local tag declaration to file scope is not yet supported");
                continue;
            }
            const auto destination = local_tag_scopes_.back().find(key);
            if (destination != local_tag_scopes_.back().end()) {
                const auto& existing = destination->second;
                const bool record = tag.type->kind == Type::Kind::Record;
                const bool other_record = existing.type->kind == Type::Kind::Record;
                const auto subject = record ? "spliced record tag '" : "spliced enumeration '";
                if (record != other_record)
                    diagnostics_.error(location, subject + key.spelling +
                        (record ? "' conflicts with a destination enumeration" :
                                  "' conflicts with a destination record"));
                else if (record && tag.type->is_union != existing.type->is_union)
                    diagnostics_.error(location, subject + key.spelling + "' conflicts with the destination record kind");
                else if (!record && tag.type->builtin != existing.type->builtin)
                    diagnostics_.error(location, subject + key.spelling + "' conflicts with the destination underlying type");
                else if (tag.complete && existing.complete &&
                    (std::any_of(child.pending_records_.begin(), child.pending_records_.end(),
                         [&](const auto& declaration) {
                             return declaration.nominal_identity == tag.type->nominal_identity;
                         }) ||
                     std::any_of(child.pending_enumerations_.begin(), child.pending_enumerations_.end(),
                         [&](const auto& declaration) {
                             return declaration.nominal_identity == tag.type->nominal_identity;
                         })))
                    diagnostics_.error(location, subject + key.spelling + "' duplicates a destination definition");
                else if (!same_type(tag.type, existing.type))
                    diagnostics_.error(location, subject + key.spelling + "' conflicts with the destination type identity");
            }
            transferred.emplace(key, tag);
        }
    for (const auto& enumeration : child.pending_enumerations_) {
        if (!enumeration.local || !before.depth ||
            child.local_scopes_.size() != before.depth) continue;
        for (const auto& enumerator : enumeration.enumerators) {
            const NameKey key(enumerator.name, enumerator.location);
            const auto source = child.local_scopes_.back().find(key);
            if (source == child.local_scopes_.back().end() || source->second != enumerator.binding) continue;
            const auto prior = before.values.find(key);
            if (prior != before.values.end() && prior->second == enumerator.binding) continue;
            if (local_scopes_.empty()) {
                diagnostics_.error(location, "moving a local enumerator declaration to file scope is not yet supported");
                continue;
            }
            const auto existing = local_scopes_.back().find(key);
            if (local_type_scopes_.back().contains(key) ||
                (existing != local_scopes_.back().end() && existing->second != enumerator.binding))
                diagnostics_.error(location, "spliced local enumerator '" + enumerator.name +
                    "' conflicts with a destination declaration");
            else transferred_enumerators.emplace(key, enumerator.binding);
        }
    }
    for (const auto& [name, tag] : child.record_types_) {
        const auto prior = prior_records.find(name);
        if (prior != prior_records.end() &&
            prior->second.is_union == tag.is_union &&
            prior->second.complete == tag.complete) continue;
        if (enum_types_.contains(name))
            diagnostics_.error(location,
                "spliced record tag '" + name + "' conflicts with a destination enumeration");
        const auto destination = record_types_.find(name);
        if (destination == record_types_.end()) continue;
        if (destination->second.is_union != tag.is_union)
            diagnostics_.error(location,
                "spliced record tag '" + name + "' conflicts with the destination record kind");
        else if (tag.complete && destination->second.complete)
            diagnostics_.error(location,
                "spliced record tag '" + name + "' duplicates a destination definition");
    }
    for (const auto& enumeration : child.pending_enumerations_) {
        if (enumeration.local || enumeration.name.empty()) continue;
        if (record_types_.contains(enumeration.name))
            diagnostics_.error(location,
                "spliced enumeration '" + enumeration.name + "' conflicts with a destination record");
        if (const auto destination = enum_types_.find(enumeration.name);
            destination != enum_types_.end() &&
            destination->second.underlying != enumeration.underlying)
            diagnostics_.error(location,
                "spliced enumeration '" + enumeration.name +
                "' conflicts with the destination underlying type");
    }
    if (diagnostics_.errors() != previous_errors) return false;
    for (const auto& [key, tag] : transferred) local_tag_scopes_.back()[key] = tag;
    for (const auto& [key, binding] : transferred_enumerators) local_scopes_.back()[key] = binding;
    for (const auto& [key, binding] : child.value_rebindings_)
        if (std::any_of(transferred_enumerators.begin(), transferred_enumerators.end(),
                [&](const auto& entry) { return entry.second == binding; }) ||
            std::any_of(child.pending_enumerations_.begin(), child.pending_enumerations_.end(),
                [&](const auto& enumeration) {
                    return !enumeration.local && std::any_of(enumeration.enumerators.begin(),
                        enumeration.enumerators.end(), [&](const auto& item) { return item.binding == binding; });
                }))
            value_rebindings_[key] = binding;
    for (const auto& [key, binding] : child.tag_rebindings_)
        if (std::any_of(transferred.begin(), transferred.end(), [&](const auto& entry) {
                return entry.second.type->nominal_key() == binding->type;
            }) || std::any_of(child.pending_records_.begin(), child.pending_records_.end(),
                [&](const auto& record) { return record.name.empty() && record.nominal_key() == binding->type; }) ||
            std::any_of(child.pending_enumerations_.begin(), child.pending_enumerations_.end(),
                [&](const auto& enumeration) { return enumeration.name.empty() && enumeration.nominal_key() == binding->type; }))
            tag_rebindings_[key] = binding;
    for (const auto& [name, tag] : child.record_types_) {
        const auto prior = prior_records.find(name);
        if (prior != prior_records.end() &&
            prior->second.is_union == tag.is_union &&
            prior->second.complete == tag.complete) continue;
        auto [destination, inserted] = record_types_.emplace(name, tag);
        if (!inserted && tag.complete) destination->second.complete = true;
    }
    for (auto& record : child.pending_records_)
        pending_records_.push_back(std::move(record));
    for (auto& enumeration : child.pending_enumerations_) {
        if (!enumeration.local) {
            if (!enumeration.name.empty())
                enum_types_[enumeration.name] = {enumeration.underlying, enumeration.captured_type_errors};
            for (const auto& enumerator : enumeration.enumerators)
                remember_ordinary_name(enumerator.name, OrdinaryNameKind::Object,
                    enumerator.location, enumerator.binding);
        }
        pending_enumerations_.push_back(std::move(enumeration));
    }
    return true;
}

Parser::StatementTask Parser::parse_statement_replacement_async(bool block_item) {
    const auto location = (co_await current_async()).location;
    const auto* owner = active_syntax(false);
    auto result = std::make_unique<Statement>();
    result->kind = Statement::Kind::Empty;
    result->location = location;
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) {
        if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
        ++index_;
        synchronize_external();
        co_return result;
    }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = co_await expand_at_position_async(false);
    if (!output) co_return result;
    auto child = replacement_parser(std::move(*output));
    // Only custom statement owners use this bounded parser. Procedural
    // token macros are exposed directly in the caller's token stream.
    if ((co_await child->current_async()).kind == TokenKind::End) {
        diagnostics_.error(location, "statement expansion must produce exactly one complete statement");
    } else result = co_await child->parse_statement_async(block_item);
    if ((co_await child->current_async()).kind != TokenKind::End)
        (co_await child->error_here_async("statement expansion must produce exactly one complete statement"));
    adopt_replacement(*child);
    co_return result;
}

std::unique_ptr<Statement> Parser::parse_statement_replacement(bool block_item) {
    return parse_statement_replacement_async(block_item).run();
}

Parser::ExpressionTask Parser::parse_expression_replacement_async() {
    const auto first = index_;
    const auto location = (co_await current_async()).location;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Integer;
    result->text = "0";
    result->location = location;
    const auto* owner = active_syntax(false);
    if (owner && owner->kind != SyntaxKind::Expression) {
        (co_await error_here_async("statement syntax is not valid at expression position"));
        ++index_;
        co_return result;
    }
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) {
        if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
        ++index_;
        co_return result;
    }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = co_await expand_at_position_async(false);
    if (!output) {
        if (preparing_header_ || retaining_shared_specifiers_)
            retain_prepared_fragment(first, {{{TokenKind::End, {}, location}}, location},
                                     SyntaxParseCategory::Expression);
        co_return result;
    }
    auto child = replacement_parser(std::move(*output));
    if ((co_await child->current_async()).kind == TokenKind::End)
        diagnostics_.error(location, "expression expansion must produce one assignment expression");
    else result = co_await child->parse_assignment_async();
    if ((co_await child->current_async()).kind != TokenKind::End)
        (co_await child->error_here_async("expression expansion must produce one assignment expression without a semicolon"));
    if (preparing_header_ || retaining_shared_specifiers_) {
        retain_prepared_fragment(first, {child->specifier_input_tokens(0, child->tokens_.size()), location},
                                 SyntaxParseCategory::Expression);
    }
    if (!preparing_header_) adopt_replacement(*child);
    // The parsed root is a subtree, so caller operators cannot reassociate
    // across the expansion boundary (even without textual parentheses).
    auto grouped = std::make_unique<Expr>();
    grouped->kind = Expr::Kind::Parenthesized;
    grouped->location = location;
    grouped->left = std::move(result);
    co_return grouped;
}

EvaluationTask<bool> Parser::defer_public_header_group_async(std::optional<std::size_t> end,
                                      std::function<EvaluationTask<void>()> parse) {
    if (!allow_public_header_deferral_ || !end) {
        co_await parse();
        co_return false;
    }
    const auto first = index_;
    const auto end_input = public_input_indices_[*end];
    const auto context = public_fragment_context(first);
    const auto production_depth = production_stack_.size();
    const auto saved_generic_argument = parsing_generic_argument_;
    const auto saved_recording = recording_public_tree_;
    const auto saved_records = pending_records_.size();
    const auto saved_enumerations = pending_enumerations_.size();
    try {
        co_await parse();
        co_return false;
    } catch (const DeferredNameRecognition&) {
        if (!context || !context->parse_environment || public_tree_failed_) throw;
        // This boundary was proved from written delimiters, before recognition
        // changed any classifier state or split a token. Only the group is
        // skipped; ordinary declarator binding must still prove a function.
        const auto after = std::find(public_input_indices_.begin() +
            static_cast<std::ptrdiff_t>(first), public_input_indices_.end(), end_input);
        if (after == public_input_indices_.end()) throw;
        restore_environment(*context->parse_environment, *context);
        pending_records_.resize(saved_records);
        pending_enumerations_.resize(saved_enumerations);
        parsing_generic_argument_ = saved_generic_argument;
        recording_public_tree_ = saved_recording;
        production_stack_.resize(production_depth);
        index_ = static_cast<std::size_t>(after - public_input_indices_.begin());
        public_deferred_header_ = true;
        co_return true;
    }
}

std::vector<Attribute> Parser::parse_attributes(bool one_specifier, AttributeParseMode mode) {
    return parse_attributes_async(one_specifier, mode).run();
}

EvaluationTask<std::vector<Attribute>> Parser::parse_attributes_async(
    bool one_specifier, AttributeParseMode mode) {
    if (!allow_public_header_deferral_) co_return co_await parse_attributes_impl_async(one_specifier, mode);
    std::vector<Attribute> result;
    while ((co_await current_async()).is("[[")) {
        std::vector<Attribute> attributes;
        if (co_await defer_public_header_group_async(bounded_group_end(index_), [&]() -> EvaluationTask<void> {
                attributes = co_await parse_attributes_impl_async(true, mode);
            })) attributes.clear();
        result.insert(result.end(), std::make_move_iterator(attributes.begin()),
                      std::make_move_iterator(attributes.end()));
        if (one_specifier) break;
    }
    co_return result;
}

EvaluationTask<std::vector<Attribute>> Parser::parse_attributes_impl_async(bool one_specifier, AttributeParseMode mode) {
    std::vector<Attribute> result;
    while ((co_await current_async()).is("[[")) {
        ProductionScope attribute_specifier(*this, SyntaxProduction::AttributeSpecifier);
        (co_await consume_async("[["));
        do {
            ProductionScope attribute_production(*this, SyntaxProduction::Attribute);
            (co_await normalize_qualified_name_async());
            if ((co_await current_async()).kind == TokenKind::BuiltinName) {
                diagnostics_.error(
                    (co_await current_async()).location,
                    "attributes are contextual names; omit the '$::' prefix");
                ++index_;
                while (!(co_await current_async()).is("]]" ) && (co_await current_async()).kind != TokenKind::End) ++index_;
                break;
            }
            const auto name_event = begin_production(SyntaxProduction::AttributeName);
            const auto first = (co_await consume_kind_async(TokenKind::Identifier));
            if (!first) {
                end_production(name_event);
                (co_await error_here_async("expected attribute name"));
                while (!(co_await current_async()).is("]]" ) && (co_await current_async()).kind != TokenKind::End) ++index_;
                break;
            }
            std::string name(first->text);
            while ((co_await consume_async("::"))) {
                const auto component = (co_await consume_kind_async(TokenKind::Identifier));
                if (!component) {
                    (co_await error_here_async("expected attribute-name component after '::'"));
                    break;
                }
                name += "::";
                name += component->text;
            }
            end_production(name_event);
            Attribute attribute{std::move(name), {}, first->location};
            if ((co_await consume_async("("))) {
                const auto argument_first = index_;
                const auto recording = recording_public_tree_;
                recording_public_tree_ = false;
                if (mode == AttributeParseMode::Semantic && attribute.name == "generic") {
                    const auto saved_generic_types = active_generic_types_;
                    while (!(co_await current_async()).is(")") &&
                           (co_await current_async()).kind != TokenKind::End) {
                        const auto start = index_;
                        auto parameter_location = (co_await current_async()).location;
                        std::optional<std::string> parameter_name;
                        TypePtr value_type;
                        if ((co_await current_async()).kind == TokenKind::Identifier &&
                            ((co_await current_async(1)).is(",") || (co_await current_async(1)).is(")"))) {
                            parameter_name = identifier_binding_name((co_await current_async()));
                            ++index_;
                            active_generic_types_.push_back(*parameter_name);
                        } else {
                            value_type = co_await parse_type_async();
                            if (value_type) {
                                value_type = co_await parse_declarator_async(
                                    std::move(value_type), parameter_name, DeclaratorContext::Named,
                                    nullptr, &parameter_location);
                            }
                            if (value_type && !is_integer(value_type) &&
                                value_type->kind != Type::Kind::Pointer &&
                                value_type->kind != Type::Kind::Generic &&
                                !(value_type->kind == Type::Kind::Builtin &&
                                  value_type->builtin == BuiltinType::Label)) {
                                diagnostics_.error(tokens_[start].location,
                                    "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
                            }
                        }
                        if (!parameter_name ||
                            parameter_name->find("::") != std::string::npos) {
                            diagnostics_.error(tokens_[start].location,
                                               "expected an unqualified generic parameter name");
                        } else if (std::any_of(
                                       attribute.generic_parameters.begin(),
                                       attribute.generic_parameters.end(),
                                       [&](const auto& parameter) {
                                           return parameter.name == *parameter_name;
                                       })) {
                            diagnostics_.error(tokens_[start].location,
                                               "duplicate 'generic' parameter '" +
                                                   *parameter_name + "'");
                        } else {
                            attribute.generic_parameters.push_back(
                                {std::move(*parameter_name), std::move(value_type), parameter_location});
                        }
                        std::string spelling;
                        for (auto token = start; token < index_; ++token) {
                            if (!spelling.empty()) spelling += ' ';
                            spelling += tokens_[token].text;
                        }
                        attribute.arguments.push_back(std::move(spelling));
                        if (index_ == start || !(co_await consume_async(","))) break;
                        if ((co_await current_async()).is(")")) {
                            (co_await error_here_async("empty 'generic' parameter"));
                            break;
                        }
                    }
                    if (attribute.generic_parameters.empty()) {
                        diagnostics_.error(attribute.location,
                                           "'generic' requires at least one parameter");
                    }
                    (co_await expect_async(")", "after generic parameters"));
                    active_generic_types_ = saved_generic_types;
                } else if (mode == AttributeParseMode::Semantic && attribute.name == "variadic") {
                    while (!(co_await current_async()).is(")") && (co_await current_async()).kind != TokenKind::End) {
                        const auto start = index_;
                        auto type = co_await parse_type_async();
                        std::optional<std::string> binding_name;
                        auto name_location = (co_await current_async()).location;
                        if (type) type = co_await parse_declarator_async(std::move(type), binding_name, DeclaratorContext::Named,
                                                         nullptr, &name_location);
                        if (!binding_name) (co_await error_here_async("expected variadic state binding name"));
                        std::optional<std::string> state;
                        if ((co_await current_async()).kind == TokenKind::String) {
                            state = decode_string_literal((co_await current_async()).text);
                            if (!state) (co_await error_here_async("invalid variadic state-name string"));
                            ++index_;
                        } else (co_await error_here_async("expected variadic state-name string"));
                        std::string spelling;
                        for (auto at = start; at < index_; ++at) spelling += tokens_[at].text;
                        attribute.arguments.push_back(std::move(spelling));
                        if (binding_name && type && state)
                            attribute.variadic_bindings.push_back(
                                {name_location, *binding_name, std::move(type), std::move(*state)});
                        if (index_ == start || !(co_await consume_async(","))) break;
                    }
                    (co_await expect_async(")", "after variadic state bindings"));
                } else if (mode == AttributeParseMode::Semantic && !parsing_public_fragment_ &&
                           (attribute.name == "aligned" || is_vector_type_attribute(attribute.name))) {
                    while (!(co_await current_async()).is(")") &&
                           (co_await current_async()).kind != TokenKind::End) {
                        const auto start = index_;
                        auto expression = co_await parse_expression_async();
                        if (index_ == start) {
                            (co_await error_here_async("expected attribute constant expression"));
                            break;
                        }
                        std::string argument;
                        for (auto index = start; index < index_; ++index) {
                            argument += tokens_[index].text;
                        }
                        attribute.arguments.push_back(std::move(argument));
                        if (attribute.arguments.size() == 1) {
                            attribute.expression_argument =
                                std::move(expression);
                        }
                        if (!(co_await consume_async(","))) break;
                    }
                    (co_await expect_async(")", "after attribute constant expression"));
                } else {
                    unsigned depth = 1;
                    std::string argument;
                    while (depth != 0 && (co_await current_async()).kind != TokenKind::End) {
                        // The public attribute argument is a balanced-token
                        // sequence. Its core shape does not depend on macro
                        // output; retain it without deferring the entire owner.
                        if (!parsing_public_fragment_) (co_await expand_inline_macro_fragments_async());
                        if ((co_await current_async()).is("(") ) { ++depth; argument += (co_await current_async()).text; ++index_; continue; }
                        if ((co_await current_async()).is(")")) {
                            --depth;
                            if (depth == 0) {
                                if (!argument.empty()) attribute.arguments.push_back(argument);
                                ++index_;
                                break;
                            }
                            argument += (co_await current_async()).text; ++index_; continue;
                        }
                        if (depth == 1 && (co_await current_async()).is(",")) {
                            attribute.arguments.push_back(argument);
                            argument.clear();
                            ++index_;
                            continue;
                        }
                        argument += (co_await current_async()).text;
                        ++index_;
                    }
                }
                recording_public_tree_ = recording;
                if (index_ > argument_first && tokens_[index_ - 1].is(")"))
                    record_balanced_sequence(argument_first, index_ - 1);
                if (parsing_public_fragment_ && mode == AttributeParseMode::Semantic &&
                    (attribute.name == "aligned" || is_vector_type_attribute(attribute.name)) &&
                    index_ > argument_first &&
                    tokens_[index_ - 1].is(")")) {
                    // The public production owns balanced tokens, not a
                    // required scalar expression. Retain a valid expression
                    // only for speculative alias/header typing; malformed
                    // attribute semantics are checked if this source survives.
                    const auto execution = syntax_ ? syntax_->execution() : nullptr;
                    if (execution && !execution->begin_fragment(attribute.location,
                            index_ - argument_first)) {
                        public_tree_failed_ = true;
                        co_return result;
                    }
                    struct End {
                        SyntaxExecution* execution;
                        ~End() { if (execution) execution->end_fragment(); }
                    } end{execution.get()};
                    std::ostringstream output;
                    Diagnostics quiet(output);
                    std::vector<Token> input(tokens_.begin() + static_cast<std::ptrdiff_t>(argument_first),
                        tokens_.begin() + static_cast<std::ptrdiff_t>(index_ - 1));
                    input.push_back({TokenKind::End, {}, tokens_[index_ - 1].location});
                    auto probe = replacement_parser({std::move(input), attribute.location}, &quiet);
                    probe->parsing_public_fragment_ = true;
                    probe->recording_public_tree_ = true;
                    for (std::size_t at = 0; at < probe->tokens_.size(); ++at)
                        probe->public_input_indices_.push_back(at);
                    auto expression = co_await probe->parse_expression_async();
                    public_deferred_header_ = public_deferred_header_ || probe->public_deferred_header_;
                    if (probe->public_tree_failed_) public_tree_failed_ = true;
                    if (quiet.errors() == 0 && (co_await probe->current_async()).kind == TokenKind::End) {
                        // Only expression grammar can distinguish an argument
                        // separator from a comma in generic actuals. The raw
                        // balanced scanner must not decide expression arity.
                        std::string spelling;
                        for (auto at = argument_first; at + 1 < index_; ++at)
                            spelling += tokens_[at].text;
                        attribute.arguments = {std::move(spelling)};
                        attribute.expression_argument = std::move(expression);
                    }
                }
            }
            result.push_back(std::move(attribute));
        } while ((co_await consume_async(",")));
        (co_await expect_async("]]", "to close attribute list"));
        if (one_specifier) break;
    }
    co_return result;
}

EvaluationTask<void> Parser::apply_type_attributes_async(
    TypePtr& type,
    std::optional<std::pair<std::uint32_t, SourceLocation>>*
        pending_address_space) {
    if (!(co_await current_async()).is("[[")) co_return;
    for (const auto& attribute : (co_await parse_attributes_async(true)))
        apply_type_attribute(type, attribute, pending_address_space);
}

void Parser::apply_type_attribute(
    TypePtr& type, const Attribute& attribute,
    std::optional<std::pair<std::uint32_t, SourceLocation>>*
        pending_address_space) {
    const auto reject = [&](SourceLocation location, std::string message) {
        type_error(type, location, std::move(message));
    };
    if (is_vector_type_attribute(attribute.name)) {
        if (type->kind == Type::Kind::Vector) {
            reject(attribute.location, "a type has at most one vector type attribute");
            return;
        }
        if (type->kind != Type::Kind::Generic &&
            ((!is_integer(type) && !is_floating(type)) || type->is_atomic ||
            type->builtin == BuiltinType::Bool || type->builtin == BuiltinType::F80 ||
            type->builtin == BuiltinType::F128 || type->builtin == BuiltinType::Fptr)) {
            reject(attribute.location,
                "vector element type must be a supported integer, f32, or f64 scalar");
            return;
        }
        if (attribute.arguments.size() != 1 || !attribute.expression_argument) {
            reject(attribute.location,
                "'" + attribute.name + "' requires one positive integer argument");
            return;
        }
        std::uint64_t amount{};
        bool literal = false;
        if (attribute.expression_argument->kind == Expr::Kind::Integer) {
            const auto& text = attribute.arguments.front();
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), amount);
            literal = parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
            if (!literal) amount = 0;
        }
        if (literal && amount == 0) {
            reject(attribute.location,
                "'" + attribute.name + "' requires one positive integer argument");
            return;
        }
        std::uint64_t lanes = amount;
        if (type->kind == Type::Kind::Generic) lanes = 0;
        else if (attribute.name == "vector_size" && literal) {
            auto element_bits = type_bits(type);
            if (type->kind == Type::Kind::Builtin &&
                (type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr)) {
                if (address_bits_ == 0) {
                    reject(attribute.location,
                        "vector_size of a target-sized element requires a resolved target");
                    return;
                }
                element_bits = address_bits_;
            }
            if (amount > std::numeric_limits<std::uint64_t>::max() / 8U) {
                reject(attribute.location, "vector size is out of range");
                return;
            }
            const auto total_bits = amount * 8U;
            if (element_bits == 0 || total_bits % element_bits != 0) {
                reject(attribute.location,
                    "vector size must be a multiple of the element size");
                return;
            }
            lanes = total_bits / element_bits;
        }
        if (lanes > std::numeric_limits<std::uint32_t>::max()) {
            reject(attribute.location, "vector lane count is out of range");
            return;
        }
        // Scalar qualifiers qualify the constructed vector object, not a
        // distinct qualified lane type. This also preserves whole-object const
        // checking when a typedef uses a trailing vector attribute.
        const bool is_const = type->is_const, is_volatile = type->is_volatile;
        auto element = copy_type(type);
        element->is_const = element->is_volatile = false;
        type = vector_type(std::move(element), static_cast<std::uint32_t>(lanes),
                           attribute.name == "scalable_vector", is_const, is_volatile);
        type->vector_bound = attribute.expression_argument;
        header_type_bounds_.insert(type->vector_bound.get());
        type->vector_bound_unit = attribute.name == "vector_size"
            ? Type::VectorBoundUnit::Bytes : Type::VectorBoundUnit::Lanes;
        return;
    }
    if (attribute.name == "atomic") {
        if (!attribute.arguments.empty())
            reject(attribute.location, "'atomic' takes no arguments");
        else if (type->is_atomic)
            reject(attribute.location, "duplicate 'atomic' type qualifier");
        else type->is_atomic = true;
        return;
    }
    if (attribute.name == "address_space") {
        if (attribute.arguments.size() != 1) {
            reject(attribute.location,
                               "address_space requires one target registry number");
            return;
        }
        auto digits = attribute.arguments.front();
        digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
        int base = 10;
        if (digits.starts_with("0x") || digits.starts_with("0X")) {
            digits.erase(0, 2);
            base = 16;
        } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
            digits.erase(0, 2);
            base = 2;
        }
        std::uint32_t number{};
        const auto parsed = std::from_chars(
            digits.data(), digits.data() + digits.size(), number, base);
        if (digits.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != digits.data() + digits.size()) {
            reject(attribute.location,
                               "address_space requires a nonnegative target registry number");
            return;
        }
        if (pending_address_space) {
            if (*pending_address_space)
                reject(attribute.location,
                                   "duplicate address_space type qualifier");
            else *pending_address_space = std::pair{number, attribute.location};
        } else if (type->kind != Type::Kind::Pointer) {
            reject(attribute.location,
                               "address_space requires a pointer type");
        } else if (type->address_space_location.valid()) {
            reject(attribute.location,
                               "duplicate address_space type qualifier");
        } else {
            type->address_space = number;
            type->address_space_location = attribute.location;
        }
        return;
    }
    reject(attribute.location,
                       "attribute '" + attribute.name +
                           "' is not valid as a type qualifier here");
}

void Parser::type_error(TypePtr& type, SourceLocation location, std::string message) {
    if (!parsing_public_fragment_) {
        diagnostics_.error(location, std::move(message));
        return;
    }
    if (!type) return;
    // Alias and tag graphs can be shared by a speculative declaration. Never
    // poison the original binding when only this attributed use is invalid.
    type = std::make_shared<Type>(*type);
    type->captured_errors.push_back({location, std::move(message)});
}

EvaluationTask<void> Parser::validate_captured_type_async(TypePtr type) {
    if (parsing_public_fragment_ || preparing_header_) co_return;
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    (void)visit_captured_type_errors(type, execution.get(), (co_await current_async()).location,
        [&](const CapturedTypeError& error) { diagnostics_.error(error.location, error.message); });
}

EvaluationTask<std::optional<std::string>> Parser::parse_qualified_name_async(SyntaxProduction production_name) {
    (co_await normalize_qualified_name_async());
    if ((co_await current_async()).kind != TokenKind::Identifier) co_return std::nullopt;
    ProductionScope production(*this, production_name);
    const auto first = (co_await consume_kind_async(TokenKind::Identifier));
    if (!first) co_return std::nullopt;
    std::string name = identifier_binding_name(*first);
    while ((co_await current_async()).is("::") && (co_await current_async(1)).kind == TokenKind::Identifier) {
        (co_await consume_async("::"));
        const auto component = (co_await consume_kind_async(TokenKind::Identifier));
        if (!component) {
            (co_await error_here_async("expected identifier after '::'"));
            break;
        }
        name += "::";
        name += identifier_binding_name(*component);
    }
    co_return name;
}

EvaluationTask<void> Parser::normalize_qualified_name_async() {
    if (!syntax_ || raw_token_depth_) co_return;
    struct Restore {
        std::size_t& cursor;
        std::size_t first;
        ~Restore() { cursor = first; }
    } restore{index_, index_};
    const auto errors = diagnostics_.errors();
    const auto assembled_invocation = [&] {
        const auto saved = index_;
        index_ = restore.first;
        const bool invocation = macro_start();
        index_ = saved;
        return invocation;
    };
    for (;;) {
        (co_await expand_inline_macro_fragments_async());
        if (diagnostics_.errors() != errors ||
            std::as_const(*this).current().kind != TokenKind::Identifier) co_return;
        ++index_;
        for (;;) {
            // A following fragment may contribute the separator, component,
            // or nothing. Never enter an argument group while probing a name.
            (co_await expand_inline_macro_fragments_async());
            if (diagnostics_.errors() != errors || assembled_invocation() ||
                !std::as_const(*this).current().is("::")) break;
            ++index_;
            (co_await expand_inline_macro_fragments_async());
            if (diagnostics_.errors() != errors || assembled_invocation() ||
                std::as_const(*this).current().kind != TokenKind::Identifier) break;
            ++index_;
        }
        index_ = restore.first;
        if (diagnostics_.errors() != errors || !macro_start()) co_return;
        // Fragments may have assembled a complete qualified invocation.
        // Expand it before callers classify a value, type, or declaration.
    }
}

EvaluationTask<std::string> Parser::peek_qualified_name_async() {
    (co_await normalize_qualified_name_async());
    co_return std::as_const(*this).peek_qualified_name();
}

std::string Parser::peek_qualified_name() const {
    if (current().kind != TokenKind::Identifier) return {};
    std::string result = identifier_binding_name(current());
    auto position = index_ + 1;
    while (position + 1 < tokens_.size() &&
           tokens_[position].is("::") &&
           tokens_[position + 1].kind == TokenKind::Identifier) {
        result += "::";
        result += identifier_binding_name(tokens_[position + 1]);
        position += 2;
    }
    return result;
}

AliasDefinitionPtr Parser::bound_type_alias(std::string_view name) const {
    const auto binding = current().alias_binding ? current().alias_binding
        : token_origin(current().location).alias_binding;
    if (!binding || binding->spelling != name) return {};
    if (retaining_shared_specifiers_ && binding->role == AliasBinding::Role::SharedSpecifierUse &&
        binding->shared_owners && shared_specifier_declarator_.source_unit &&
        std::find(active_generic_types_.begin(), active_generic_types_.end(), name) != active_generic_types_.end()) {
        const auto& owners = binding->shared_owners->declarations;
        if (syntax_ && !syntax_->execution()->work(current().location, owners.size() + 1)) return {};
        if (std::find(owners.begin(), owners.end(), shared_specifier_declarator_) != owners.end()) return {};
    }
    auto definition = binding->definition;
    for (std::size_t remaining = alias_rebindings_.size(); remaining; --remaining) {
        const auto replacement = alias_rebindings_.find(definition);
        if (replacement == alias_rebindings_.end()) break;
        definition = replacement->second;
    }
    return definition;
}

void Parser::remember_alias_binding(std::size_t token_index, std::string_view spelling,
                                    AliasDefinitionPtr definition, bool declaration) {
    if (!definition || preparing_header_) return;
    auto& token = tokens_[token_index];
    if (declaration && binds_at_file_scope())
        remember_fragment_name(spelling, join_namespace(active_namespace_, spelling),
            token.location, FragmentNameDomain::Ordinary);
    const auto incoming = token.alias_binding ? token.alias_binding
        : token_origin(token.location).alias_binding;
    if (declaration && incoming && incoming->role == AliasBinding::Role::Declaration &&
        incoming->spelling == spelling && incoming->definition != definition)
        alias_rebindings_[incoming->definition] = definition;
    remember_specifier_input(token_index);
    const bool shared_use = retaining_shared_specifiers_ && shared_specifier_owners_ &&
        (!incoming || incoming->role == AliasBinding::Role::SharedSpecifierUse);
    const auto carried = !declaration && incoming && incoming->spelling == spelling
        ? incoming->carried : nullptr;
    import_carried(carried);
    token.alias_binding = std::make_shared<const AliasBinding>(AliasBinding{
        declaration ? AliasBinding::Role::Declaration : shared_use
            ? AliasBinding::Role::SharedSpecifierUse : AliasBinding::Role::Use,
        std::string(spelling), std::move(definition), shared_use
            ? incoming ? incoming->shared_owners : shared_specifier_owners_ : nullptr,
        carried});
}

void Parser::transfer_alias_rebindings(const Parser& child, const AliasDefinitionPtr& definition) {
    for (const auto& [old, replacement] : child.alias_rebindings_)
        if (replacement == definition) alias_rebindings_[old] = replacement;
}

AliasDefinitionPtr Parser::resolve_type_alias(std::string_view name) const {
    if (const auto binding = bound_type_alias(name)) return binding;
    const auto origin = token_origin(current().location);
    const bool prototype_context = !prototype_scopes_.empty() ||
        (origin.context && origin.context->parse_environment &&
         !origin.context->parse_environment->prototypes.empty());
    if (prototype_context && name.find("::") == std::string_view::npos) {
        const auto retained = retained_value_binding(name, current().location);
        if (retained.kind == ValueBinding::Kind::Local) return {};
        if (retained.kind == ValueBinding::Kind::Unknown) {
            const NameKey key(name, current().location);
            for (auto scope = prototype_scopes_.rbegin(); scope != prototype_scopes_.rend(); ++scope)
                if (scope->values.contains(key)) return {};
        }
    }
    require_public_name_context(name);
    const auto retained = retained_type_name(name, current().location, PublicNameDomain::Ordinary);
    if (retained.kind == RetainedTypeName::Kind::Alias) return retained.alias;
    if (retained.kind != RetainedTypeName::Kind::Absent) return {};
    if (!origin.value_context_captured &&
        diagnose_retained_scope_ambiguity(name, current().location, PublicNameDomain::Ordinary)) return {};
    if (name.find("::") == std::string_view::npos) {
        const NameKey key(name, current().location);
        for (auto scope = local_type_scopes_.size(); scope != 0; --scope) {
            const auto found = local_type_scopes_[scope - 1].find(key);
            if (found != local_type_scopes_[scope - 1].end() &&
                retained_type_binding_visible(scope - 1,
                    {RetainedTypeName::Kind::Alias, found->second, {}}, current().location)) return found->second;
            if (const auto value = local_scopes_[scope - 1].find(key); value != local_scopes_[scope - 1].end())
                if (!origin.value_context_captured || retained_value_binding(name, current().location) == value->second)
                    return {};
            if (diagnose_scope_ambiguity(scope - 1, key, PublicNameDomain::Ordinary,
                    current().location)) return {};
        }
    }
    const auto find = [&](std::string_view candidate) -> AliasDefinitionPtr {
        const auto found = type_aliases_.find(std::string(candidate));
        return found == type_aliases_.end() ? AliasDefinitionPtr{} : found->second;
    };
    if (const auto introduced = fragment_name(name, current().location, FragmentNameDomain::Ordinary))
        return find(introduced->destination);
    const auto context = effective_context(current().location);
    NameLookupContext lookup;
    lookup.name_space = origin.context ? origin.context->name_space : active_namespace_;
    lookup.imports = context ? context->imports : active_imports_;
    NameUse use(name, current().location);
    use.context = &lookup;
    for (const auto& candidate : namespace_candidates(use)) {
        if (const auto type = find(candidate)) return type;
        if (known_ordinary_values_->entries.contains(candidate)) return {};
    }
    return {};
}

TypePtr Parser::rebind_tag_types(TypePtr type) const {
    if (!type || tag_rebindings_.empty()) return type;
    type = copy_type(type);
    std::unordered_set<Type*> seen;
    std::vector<TypePtr> pending{type};
    while (!pending.empty()) {
        auto next = std::move(pending.back());
        pending.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        // Labels also have nominal keys, but occupy a different name domain.
        if ((next->kind == Type::Kind::Record || is_integer(next)) &&
            !next->nominal_key().empty()) {
            for (std::size_t remaining = tag_rebindings_.size(); remaining; --remaining) {
                const auto replacement = tag_rebindings_.find(next->nominal_key());
                if (replacement == tag_rebindings_.end()) break;
                const auto& binding = *replacement->second;
                next->kind = binding.kind == TagBinding::Kind::Enumeration
                    ? Type::Kind::Builtin : Type::Kind::Record;
                next->is_union = binding.kind == TagBinding::Kind::Union;
                next->builtin = binding.underlying;
                next->nominal_name = binding.type.name;
                next->nominal_identity = binding.type.identity;
                next->captured_tag_errors = binding.errors;
            }
        }
        pending.push_back(next->pointee);
        pending.push_back(next->element);
        if (next->function) {
            pending.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) {
                pending.push_back(parameter.type);
                pending.push_back(parameter.declared_array_type);
            }
        }
    }
    return type;
}

void Parser::remember_tag_binding(std::size_t token_index, std::string_view spelling,
                                  const TypePtr& type, bool declaration) {
    if (!type || type->nominal_key().empty() || preparing_header_) return;
    auto& token = tokens_[token_index];
    if (declaration && binds_at_file_scope())
        remember_fragment_name(spelling, type->nominal_name, token.location, FragmentNameDomain::Tag);
    auto binding = std::make_shared<TagBinding>(TagBinding{
        type->kind != Type::Kind::Record ? TagBinding::Kind::Enumeration :
            type->is_union ? TagBinding::Kind::Union : TagBinding::Kind::Structure,
        declaration ? TagBinding::Role::Declaration : TagBinding::Role::Use,
        std::string(spelling), type->nominal_key(), type->builtin, type->captured_tag_errors});
    const auto origin = token_origin(token.location);
    auto incoming = token.tag_binding ? token.tag_binding : origin.tag_binding;
    if (!incoming && declaration) incoming = origin.tag_declaration_source;
    if (declaration && incoming && incoming->role == TagBinding::Role::Declaration &&
        incoming->spelling == spelling &&
        (incoming->type != binding->type || incoming->errors != binding->errors)) {
        binding->declaration_source = incoming;
        binding->source_storage = tag_binding_storage(incoming);
        for (auto ancestor = incoming; ancestor; ancestor = ancestor->declaration_source) {
            if (syntax_ && !syntax_->execution()->work(token.location)) {
                public_tree_failed_ = true;
                break;
            }
            if (ancestor->type != binding->type) tag_rebindings_[ancestor->type] = binding;
        }
    }
    if (!declaration && incoming && incoming->spelling == spelling && incoming->type == binding->type) {
        binding->carried = incoming->carried;
        import_carried(binding->carried);
    }
    remember_specifier_input(token_index);
    token.tag_binding = std::move(binding);
}

AliasDefinitionPtr Parser::block_type_alias(std::string_view name) const {
    const NameKey key(name, current().location);
    for (auto scope = local_type_scopes_.size(); scope != 0; --scope) {
        if (const auto found = local_type_scopes_[scope - 1].find(key);
            found != local_type_scopes_[scope - 1].end()) return found->second;
        if (local_scopes_[scope - 1].contains(key)) return {};
    }
    return {};
}

TypePtr Parser::block_tag_type(std::string_view name) const {
    const NameKey key(name, current().location);
    for (auto scope = local_tag_scopes_.size(); scope != 0; --scope)
        if (const auto found = local_tag_scopes_[scope - 1].find(key);
            found != local_tag_scopes_[scope - 1].end()) return found->second.type;
    return {};
}

std::shared_ptr<const CarriedDefinitions> Parser::block_definitions(const TypePtr& type) const {
    // Definitions of the current function are still pending; generic-owned ones
    // are instantiated per instance instead of being carried.
    auto result = std::make_shared<CarriedDefinitions>();
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> types{type};
    std::vector<const Expr*> expressions;
    const auto add_attributes = [&](const std::vector<Attribute>& list) {
        for (const auto& attribute : list) expressions.push_back(attribute.expression_argument.get());
    };
    while (!types.empty() || !expressions.empty()) {
        if (!expressions.empty()) {
            const auto* expression = expressions.back();
            expressions.pop_back();
            if (!expression) continue;
            const auto enumeration = expression->kind == Expr::Kind::Name
                ? name_key(*expression).binding.enumeration : nullptr;
            if (enumeration && !enumeration->generic_owner &&
                std::none_of(result->enumerations.begin(), result->enumerations.end(),
                    [&](const auto& carried) { return carried->nominal_identity == enumeration; })) {
                const auto found = std::find_if(pending_enumerations_.begin(), pending_enumerations_.end(),
                    [&](const EnumDecl& candidate) { return candidate.nominal_identity == enumeration; });
                if (found != pending_enumerations_.end()) {
                    result->enumerations.push_back(std::make_shared<const EnumDecl>(copy_evaluation_declaration(*found)));
                    for (const auto& enumerator : found->enumerators) expressions.push_back(enumerator.initializer.get());
                }
            }
            expressions.push_back(expression->left.get());
            expressions.push_back(expression->right.get());
            expressions.push_back(expression->third.get());
            for (const auto& argument : expression->arguments) expressions.push_back(argument.get());
            for (const auto& argument : expression->generic_arguments) {
                types.push_back(argument.type);
                expressions.push_back(argument.value.get());
            }
            types.push_back(expression->type);
            continue;
        }
        auto next = std::move(types.back());
        types.pop_back();
        if (!next || !seen.insert(next.get()).second) continue;
        types.push_back(next->pointee);
        types.push_back(next->element);
        expressions.push_back(next->array_bound.get());
        expressions.push_back(next->vector_bound.get());
        if (next->function) {
            types.push_back(next->function->result);
            for (const auto& parameter : next->function->parameters) types.push_back(parameter.type);
        }
        if (next->kind != Type::Kind::Record || !next->nominal_identity ||
            next->nominal_identity->generic_owner) continue;
        const auto key = next->nominal_key();
        if (std::any_of(result->records.begin(), result->records.end(),
                [&](const auto& record) { return record->nominal_key() == key; })) continue;
        const auto found = std::find_if(pending_records_.begin(), pending_records_.end(),
            [&](const RecordDecl& record) { return record.complete && record.nominal_key() == key; });
        if (found == pending_records_.end()) continue;
        result->records.push_back(std::make_shared<const RecordDecl>(copy_evaluation_declaration(*found)));
        add_attributes(found->attributes);
        for (const auto& member : found->members) {
            types.push_back(member.type);
            expressions.push_back(member.bit_width.get());
            add_attributes(member.attributes);
        }
    }
    if (result->records.empty() && result->enumerations.empty()) return {};
    return result;
}

void Parser::import_carried(const std::shared_ptr<const CarriedDefinitions>& carried) {
    if (!carried) return;
    for (const auto& record : carried->records) {
        const auto key = record->nominal_key();
        if (std::any_of(pending_records_.begin(), pending_records_.end(),
                [&](const RecordDecl& existing) { return existing.nominal_key() == key; })) continue;
        auto copy = copy_evaluation_declaration(*record);
        copy.carried = true;
        pending_records_.push_back(std::move(copy));
    }
    for (const auto& enumeration : carried->enumerations) {
        if (std::any_of(pending_enumerations_.begin(), pending_enumerations_.end(),
                [&](const EnumDecl& existing) { return existing.nominal_identity == enumeration->nominal_identity; }))
            continue;
        auto copy = copy_evaluation_declaration(*enumeration);
        copy.carried = true;
        pending_enumerations_.push_back(std::move(copy));
    }
}

TypePtr Parser::resolve_tag_type(std::string_view name, const Token& token) const {
    const auto location = token.location;
    const auto origin = token_origin(location);
    const auto binding = token.tag_binding ? token.tag_binding : origin.tag_binding;
    if (binding && binding->spelling == name) {
        auto type = binding->kind == TagBinding::Kind::Enumeration
            ? enum_type(binding->type.name, binding->underlying)
            : record_type(binding->type.name, binding->kind == TagBinding::Kind::Union);
        type->nominal_identity = binding->type.identity;
        type->captured_tag_errors = binding->errors;
        return rebind_tag_types(std::move(type));
    }
    require_public_name_context(name, location, PublicNameDomain::Tag);
    const auto retained = retained_type_name(name, location, PublicNameDomain::Tag);
    if (retained.kind == RetainedTypeName::Kind::Tag) return rebind_tag_types(copy_type(retained.tag));
    if (retained.kind != RetainedTypeName::Kind::Absent) return {};
    if (!origin.value_context_captured &&
        diagnose_retained_scope_ambiguity(name, location, PublicNameDomain::Tag)) return {};
    if (name.find("::") == std::string_view::npos) {
        const NameKey key(name, location);
        for (auto depth = local_tag_scopes_.size(); depth != 0; --depth) {
            const auto& scope = local_tag_scopes_[depth - 1];
            if (const auto found = scope.find(key); found != scope.end())
                if (retained_type_binding_visible(depth - 1,
                        {RetainedTypeName::Kind::Tag, {}, found->second.type}, location))
                    return rebind_tag_types(copy_type(found->second.type));
            if (diagnose_scope_ambiguity(depth - 1, key, PublicNameDomain::Tag, location)) return {};
        }
    }
    // All three tag kinds share one lookup space. Select the nearest name
    // before checking its requested kind, and use the name token's context:
    // a quoted keyword and a copied tag identifier can have different origins.
    const auto context = effective_context(location);
    NameLookupContext lookup;
    lookup.name_space = origin.context ? origin.context->name_space : active_namespace_;
    lookup.imports = context ? context->imports : active_imports_;
    NameUse use(name, location);
    use.context = &lookup;
    auto candidates = namespace_candidates(use);
    if (const auto introduced = fragment_name(name, location, FragmentNameDomain::Tag))
        candidates = {introduced->destination};
    for (const auto& candidate : candidates) {
        if (const auto found = record_types_.find(candidate); found != record_types_.end())
            return rebind_tag_types(record_type(candidate, found->second.is_union));
        if (const auto found = enum_types_.find(candidate); found != enum_types_.end()) {
            auto type = enum_type(candidate, found->second.underlying);
            type->captured_tag_errors = found->second.errors;
            return rebind_tag_types(std::move(type));
        }
    }
    return {};
}

std::shared_ptr<const NominalTypeIdentity> Parser::new_nominal_identity(SourceLocation location) {
    const auto origin = token_origin(location);
    return std::make_shared<const NominalTypeIdentity>(NominalTypeIdentity{
        origin.identity, location.file ? location.file->source_unit_at(location.line) : std::string{},
        ++*nominal_occurrence_, generic_tag_owner_, {},
        function_scope_});
}

TypePtr Parser::declare_local_tag(TypePtr type, SourceLocation location, bool complete) {
    const NameKey key(type->nominal_name, location);
    auto& scope = local_tag_scopes_.back();
    if (tag_destination_ && tag_destination_depth_ == local_tag_scopes_.size()) {
        const auto destination = tag_destination_->find(key);
        if (destination != tag_destination_->end() && (!destination->second.complete || !complete) &&
            destination->second.type->kind == type->kind &&
            destination->second.type->is_union == type->is_union &&
            destination->second.type->builtin == type->builtin)
            scope[key] = destination->second;
    }
    auto found = scope.find(key);
    if (found != scope.end()) {
        const auto& prior = found->second.type;
        if (prior->kind != type->kind)
            diagnostics_.error(location, "tag '" + key.spelling + "' was previously declared as " +
                (prior->kind == Type::Kind::Record ? "a record" : "an enumeration"));
        else if (prior->kind == Type::Kind::Record && prior->is_union != type->is_union)
            diagnostics_.error(location, "record tag '" + key.spelling + "' was previously declared with the other record kind");
        else if (prior->kind != Type::Kind::Record && prior->builtin != type->builtin)
            diagnostics_.error(location, "enumeration '" + key.spelling + "' redeclared with a different underlying type");
        else if (complete && found->second.complete)
            diagnostics_.error(location, "duplicate definition of " +
                std::string(prior->kind == Type::Kind::Record ? "record '" : "enumeration '") + key.spelling + "'");
        found->second.complete = found->second.complete || complete;
        if (type->captured_tag_errors) {
            auto combined = std::make_shared<CapturedTypeErrors>();
            if (prior->captured_tag_errors) *combined = *prior->captured_tag_errors;
            combined->insert(combined->end(), type->captured_tag_errors->begin(), type->captured_tag_errors->end());
            found->second.type = std::make_shared<Type>(*prior);
            found->second.type->captured_tag_errors = std::move(combined);
        }
        return copy_type(found->second.type);
    }
    const auto source_forwards = forward_tags_->scopes.find(destination_forward_tag_scope());
    if (source_forwards != forward_tags_->scopes.end()) {
        const auto forward = source_forwards->second.find(key);
        if (forward != source_forwards->second.end()) {
            if (forward->second->kind != type->kind || forward->second->is_union != type->is_union)
                diagnostics_.error(location, "tag '" + key.spelling +
                    "' conflicts with its source-scope implicit forward declaration");
            type->nominal_identity = forward->second->nominal_identity;
        }
    }
    if (!type->nominal_identity) type->nominal_identity = new_nominal_identity(location);
    scope.emplace(key, LocalTag{copy_type(type), complete});
    return type;
}

EvaluationTask<bool> Parser::type_start_async(TypeProbe probe) {
    // Cast/sizeof probes can be looking at an expression owner rather than
    // a type. Its input must stay opaque until the expression parser selects
    // it, even if the next token happens to begin a procedural invocation.
    const auto* owner = active_syntax(false);
    if (probe != TypeProbe::Required && owner) {
        // Activation commits before name-sensitive type/expression probing.
        // Required type slots remain unaffected by expression activation.
        if (owner->kind == SyntaxKind::Expression) co_return false;
    } else (co_await normalize_qualified_name_async());
    const auto token = (co_await current_async());
    // These builtin names denote both meta types and value operations. In a
    // type/expression ambiguity, an immediate argument list selects the call;
    // required type positions and ordinary named declarations stay types.
    if (probe != TypeProbe::Required && token.kind == TokenKind::BuiltinName &&
        (token.is("$::meta::tokens") || token.is("$::meta::span")) &&
        std::as_const(*this).current(1).is("(")) co_return false;
    if (token.kind == TokenKind::PreparedFragment)
        co_return token.prepared && token.prepared->category == SyntaxParseCategory::Type;
    if (token.kind == TokenKind::StructuredSplice)
        co_return token.splice && syntax_type_node(*token.splice);
    if (allow_public_header_deferral_ && public_header_uncertain_names_ && probe == TypeProbe::Required &&
        token.kind == TokenKind::Identifier && !is_reserved_identifier(token.text) &&
        std::as_const(*this).peek_qualified_name().find("::") == std::string::npos) {
        public_deferred_header_ = true;
        co_return true;
    }
    if (token.kind == TokenKind::Identifier) {
        const auto binding = token.value_binding.kind != ValueBinding::Kind::Unknown
            ? token.value_binding : token_origin(token.location).value_binding;
        if (binding.kind != ValueBinding::Kind::Unknown) co_return false;
        const auto name = std::as_const(*this).peek_qualified_name();
        const auto origin = token_origin(token.location);
        const bool prototype_context = !prototype_scopes_.empty() ||
            (origin.context && origin.context->parse_environment &&
             !origin.context->parse_environment->prototypes.empty());
        if (prototype_context && name.find("::") == std::string::npos && !bound_type_alias(name)) {
            const auto retained = retained_value_binding(name, token.location);
            if (retained.kind == ValueBinding::Kind::Local) co_return false;
            if (retained.kind == ValueBinding::Kind::Unknown) {
                const NameKey key(name, token.location);
                for (auto scope = prototype_scopes_.rbegin(); scope != prototype_scopes_.rend(); ++scope)
                    if (scope->values.contains(key)) co_return false;
            }
        }
        require_public_name_context(name);
    }
    const auto name = std::as_const(*this).peek_qualified_name();
    if (preparing_header_ && (!parsing_public_fragment_ || probing_header_type_) &&
        token.kind == TokenKind::Identifier && !is_reserved_identifier(token.text)) {
        if (probe == TypeProbe::Required || probe == TypeProbe::GenericTypeArgumentAlternative)
            co_return true;
        if (!parsing_public_fragment_)
            if (co_await probe_header_type_async(probe == TypeProbe::GenericArgumentAlternative)) co_return true;
    }
    co_return token.is("[[") || token.is("const") || token.is("volatile") ||
           token.is("$::meta::tokens") ||
           token.is("$::meta::syntax_match") ||
           token.is("$::meta::syntax") ||
           token.is("$::meta::span") ||
           token.is("$::meta::context") ||
           token.is("$::meta::bytes") || token.is("$::meta::buffer") ||
           token.is("restrict") || token.is("enum") ||
           token.is("struct") || token.is("union") ||
           builtin_kind(token.text).has_value() ||
           std::find(active_generic_types_.begin(), active_generic_types_.end(),
                     name) != active_generic_types_.end() ||
           resolve_type_alias(name) != nullptr;
}

void Parser::resolve_specifier_attributes(SpecifierAttributes& attributes, bool declares_entity) {
    for (auto& candidate : attributes.candidates) {
        if (!declares_entity && attributes.inline_record) {
            auto& record = pending_records_[*attributes.inline_record];
            record.attributes.insert(record.attributes.begin() +
                static_cast<std::ptrdiff_t>(candidate.position), std::move(candidate.attribute));
        } else if (!parsing_public_fragment_ && !preparing_header_) {
            diagnostics_.error(candidate.attribute.location,
                "ambiguous leading '" + candidate.attribute.name +
                "' attribute: place it beside the inline tag name for type layout "
                "or after the declarator for entity layout");
        }
    }
    attributes.candidates.clear();
}

TypePtr Parser::parse_type(bool record_specifiers,
                           StorageSpecifier storage_specifier,
                           SpecifierAttributes* attributes, bool tag_declaration) {
    return parse_type_async(record_specifiers, std::move(storage_specifier), attributes, tag_declaration).run();
}

EvaluationTask<TypePtr> Parser::parse_type_async(bool record_specifiers,
                           StorageSpecifier storage_specifier,
                           SpecifierAttributes* attributes, bool tag_declaration) {
    auto* declaration_attributes = attributes ? &attributes->declaration : nullptr;
    ProductionScope specifiers(*this, record_specifiers
        ? SyntaxProduction::DeclarationSpecifiers : SyntaxProduction::None);
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    std::optional<SourceLocation> restrict_location;
    std::optional<std::pair<std::uint32_t, SourceLocation>> pending_address_space;
    std::vector<Attribute> deferred_type_attributes;
    if (declaration_attributes) {
        std::vector<Attribute> leading_attributes;
        leading_attributes.swap(*declaration_attributes);
        for (auto& attribute : leading_attributes) {
            if (attribute.name == "atomic" || attribute.name == "address_space" ||
                is_vector_type_attribute(attribute.name))
                deferred_type_attributes.push_back(std::move(attribute));
            else declaration_attributes->push_back(std::move(attribute));
        }
    }
    const auto consume_specifier_attributes = [&](TypePtr* built_type) -> EvaluationTask<void> {
        for (auto& attribute : (co_await parse_attributes_async(true))) {
            if (attribute.name == "atomic" || attribute.name == "address_space" ||
                is_vector_type_attribute(attribute.name)) {
                if (built_type)
                    apply_type_attribute(*built_type, attribute, &pending_address_space);
                else deferred_type_attributes.push_back(std::move(attribute));
            } else if (declaration_attributes) {
                declaration_attributes->push_back(std::move(attribute));
            } else if (built_type) apply_type_attribute(*built_type, attribute, &pending_address_space);
            else deferred_type_attributes.push_back(std::move(attribute));
        }
    };
    const auto storage_start_async = [&]() -> EvaluationTask<bool> {
        co_return storage_specifier &&
            ((co_await current_async()).is("typedef") || (co_await current_async()).is("static") ||
             (co_await current_async()).is("global") || (co_await current_async()).is("register") ||
             (co_await current_async()).is("stack") || (co_await current_async()).is("inline"));
    };
    while ((co_await current_async()).is("const") || (co_await current_async()).is("volatile") ||
           (co_await current_async()).is("restrict") || (co_await current_async()).is("[[") || (co_await storage_start_async())) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if ((co_await storage_start_async())) {
            if (!(co_await storage_specifier())) break;
            continue;
        }
        if ((co_await current_async()).is("[[")) {
            co_await consume_specifier_attributes(nullptr);
            continue;
        }
        ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
        if ((co_await consume_async("const")))
            is_const = true;
        else if ((co_await consume_async("volatile")))
            is_volatile = true;
        else {
            restrict_location = (co_await current_async()).location;
            (co_await consume_async("restrict"));
            is_restrict = true;
        }
    }
    (co_await normalize_qualified_name_async());
    TypePtr type;
    {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
        ProductionScope target_type(*this, (co_await current_async()).kind == TokenKind::BuiltinName
                                               ? SyntaxProduction::TargetScalarBuiltinName
                                               : SyntaxProduction::None);
        ProductionScope builtin(*this, (co_await current_async()).kind == TokenKind::BuiltinName
                                           ? SyntaxProduction::BuiltinName
                                           : SyntaxProduction::None);
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        if ((co_await current_async()).kind == TokenKind::PreparedFragment) {
            const auto token = (co_await current_async());
            ++index_;
            type = builtin_type(BuiltinType::I32);
            if (!token.prepared || token.prepared->category != SyntaxParseCategory::Type) {
                diagnostics_.error(token.location, "prepared expression is not a type");
            } else if (parsing_public_fragment_) {
                if (!probing_header_type_)
                    diagnostics_.error(token.location, "internal prepared type cannot enter a public capture");
            } else if (auto child = prepared_fragment_parser(token)) {
                const auto prior_records = child->tag_state();
                child->prepare_tag_destination(*this);
                const auto previous_errors = diagnostics_.errors();
                auto parsed = co_await child->parse_type_async();
                std::optional<std::string> name;
                if (parsed) parsed = co_await child->parse_declarator_async(std::move(parsed), name, DeclaratorContext::TypeName);
                if (name || (co_await child->current_async()).kind != TokenKind::End)
                    (co_await child->error_here_async("prepared type must contain one complete nameless type"));
                if (diagnostics_.errors() == previous_errors && parsed &&
                    transfer_spliced_tags(*child, prior_records, token.location)) {
                    for (auto& assertion : child->static_assertions_)
                        static_assertions_.push_back(std::move(assertion));
                    type = std::move(parsed);
                }
            }
        } else if ((co_await current_async()).kind == TokenKind::StructuredSplice) {
            const auto item = (co_await current_async());
            const auto item_index = index_;
            ++index_;
            type = builtin_type(BuiltinType::I32, is_const, is_volatile);
            if (!item.splice || !syntax_type_node(*item.splice)) {
                diagnostics_.error(item.location,
                    "structured syntax splice requires a type node at type position");
            } else if (!syntax_ || !item.splice->context ||
                       !item.splice->context->parse_environment) {
                diagnostics_.error(item.location,
                    "structured syntax splice has no retained parse environment");
            } else {
                if (recording_public_tree_ && type_specifier.event < production_events_.size()) {
                    auto wrapper = std::make_shared<SyntaxNode>();
                    wrapper->kind = SyntaxNode::Kind::Core;
                    wrapper->production = SyntaxProduction::TypeSpecifier;
                    wrapper->structured_splice = true;
                    wrapper->children.push_back(item.splice);
                    wrapper->span = item.splice->span;
                    wrapper->context = item.splice->context;
                    production_events_[type_specifier.event].opaque = std::move(wrapper);
                }
                if (!parsing_public_fragment_) {
                    auto output = syntax_->execution()->materialize_node(
                        *item.splice, item.location, SyntaxParseCategory::Type);
                    if (output) {
                        auto child = replacement_parser(std::move(*output));
                        child->retaining_shared_specifiers_ = false;
                        if (item.splice->kind == SyntaxNode::Kind::Deferred)
                            child->restore_deferred_environment(
                                *item.splice->context->parse_environment,
                                *item.splice->context, deferred_position(*item.splice));
                        else
                            child->restore_environment(
                                *item.splice->context->parse_environment,
                                *item.splice->context);
                        const auto prior_records = child->tag_state();
                        child->prepare_tag_destination(*this);
                        const auto previous_errors = diagnostics_.errors();
                        auto parsed = co_await child->parse_type_async();
                        std::optional<std::string> declarator_name;
                        if (parsed) parsed = co_await child->parse_declarator_async(std::move(parsed), declarator_name,
                                                                   DeclaratorContext::TypeName);
                        if (declarator_name)
                            (co_await child->error_here_async("structured type splice cannot declare a name"));
                        if ((co_await child->current_async()).kind != TokenKind::End)
                            (co_await child->error_here_async("structured type splice must contain one complete type"));
                        if (diagnostics_.errors() == previous_errors && parsed &&
                            transfer_spliced_tags(*child, prior_records, item.location)) {
                            for (auto& assertion : child->static_assertions_)
                                static_assertions_.push_back(std::move(assertion));
                            type = std::move(parsed);
                        }
                        if (preparing_header_ || retaining_shared_specifiers_)
                            retain_prepared_fragment(item_index,
                                {child->specifier_input_tokens(0, child->tokens_.size()), item.location}, SyntaxParseCategory::Type,
                                item.splice->context, item.splice->kind == SyntaxNode::Kind::Deferred,
                                deferred_position(*item.splice));
                    }
                }
            }
        } else if ((co_await current_async()).is("$::meta::context")) {
            type = context_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if ((co_await current_async()).is("$::meta::span")) {
            type = span_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if ((co_await current_async()).is("$::meta::syntax")) {
            type = syntax_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if ((co_await current_async()).is("$::meta::syntax_match")) {
            type = syntax_match_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if ((co_await current_async()).is("$::meta::tokens")) {
            ++index_;
            type = tokens_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
        } else if ((co_await current_async()).is("$::meta::bytes") || (co_await current_async()).is("$::meta::buffer")) {
            const bool bytes = (co_await consume_async("$::meta::bytes"));
            if (!bytes)
                (co_await consume_async("$::meta::buffer"));
            type = bytes ? bytes_type() : buffer_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
        } else if ((co_await current_async()).is("struct") || (co_await current_async()).is("union")) {
            ProductionScope record(*this, SyntaxProduction::StructOrUnionSpecifier);
            const auto keyword_index = index_;
            const auto location = (co_await current_async()).location;
            const bool is_union = (co_await consume_async("union"));
            if (!is_union)
                (co_await consume_async("struct"));
            const auto name_index = index_;
            const auto name_location = (co_await current_async()).location;
            const auto name = (co_await parse_qualified_name_async());
            auto record_attributes = co_await parse_attributes_async();
            if (!name && !(co_await current_async()).is("{")) {
                (co_await error_here_async("an anonymous record requires a definition"));
                co_return {};
            }
            const bool local_tag = name && !binds_at_file_scope() && name->find("::") == std::string::npos;
            if ((co_await current_async()).is("{")) {
                RecordDecl declaration;
                declaration.location = location;
                declaration.name = !name ? std::string{} : local_tag ? *name : join_namespace(active_namespace_, *name);
                declaration.is_union = is_union;
                declaration.complete = true;
                if (name && !local_tag && enum_types_.contains(declaration.name))
                    diagnostics_.error(name_location, "tag '" + declaration.name +
                        "' was previously declared as an enumeration");
                if (declaration_attributes) {
                    std::vector<Attribute> remaining;
                    std::size_t position = 0;
                    for (auto& attribute : *declaration_attributes) {
                        if (attribute.name != "packed" && attribute.name != "aligned") {
                            remaining.push_back(std::move(attribute));
                            continue;
                        }
                        const bool entity_owner = attributes->context == SpecifierAttributes::Context::Member ||
                            (attribute.name == "aligned" &&
                             attributes->context != SpecifierAttributes::Context::Parameter);
                        if (entity_owner)
                            attributes->candidates.push_back({std::move(attribute), position});
                        else declaration.attributes.push_back(std::move(attribute));
                        ++position;
                    }
                    *declaration_attributes = std::move(remaining);
                }
                declaration.attributes.insert(declaration.attributes.end(),
                    std::make_move_iterator(record_attributes.begin()),
                    std::make_move_iterator(record_attributes.end()));
                if (!name) {
                    declaration.nominal_identity = new_nominal_identity(location);
                } else if (local_tag) {
                    type = declare_local_tag(record_type(*name, is_union), name_location, true);
                    declaration.nominal_identity = type->nominal_identity;
                } else {
                    auto [tag, inserted] = record_types_.emplace(
                        declaration.name, RecordTag{is_union, true});
                    if (!inserted && tag->second.is_union != is_union) {
                        diagnostics_.error(location, "record tag '" + declaration.name +
                            "' was previously declared with the other record kind");
                    } else if (!inserted && tag->second.complete) {
                        diagnostics_.error(location,
                            "duplicate definition of record '" + declaration.name + "'");
                    }
                    tag->second.complete = true;
                }
                remember_tag_binding(name ? name_index : keyword_index,
                    name ? *name : is_union ? "union" : "struct", record_type(declaration), true);
                if (co_await defer_public_header_group_async(allow_public_header_deferral_
                        ? bounded_group_end(index_) : std::nullopt, [&]() -> EvaluationTask<void> {
                        (co_await consume_async("{"));
                        co_await parse_record_members_async(declaration);
                    })) declaration.members.clear();
                type = record_type(declaration);
                if (attributes) attributes->inline_record = pending_records_.size();
                pending_records_.push_back(std::move(declaration));
            } else {
                bool declares_tag = local_tag && tag_declaration && (co_await current_async()).is(";");
                type = declares_tag
                    ? declare_local_tag(record_type(*name, is_union), name_location, false)
                    : resolve_tag_type(*name, tokens_[name_index]);
                if (!type && !declares_tag && token_origin(name_location).value_context_captured) {
                    type = declare_retained_implicit_tag(*name, name_location, is_union);
                    if (!type) co_return {};
                }
                if (!type && local_tag) {
                    type = declare_local_tag(record_type(*name, is_union), name_location, false);
                    declares_tag = true;
                }
                if (!type) {
                    const auto origin = token_origin(name_location);
                    const auto& name_space = origin.context
                        ? origin.context->name_space : active_namespace_;
                    const auto canonical = name->find("::") == std::string::npos
                        ? join_namespace(name_space, *name) : *name;
                    record_types_.emplace(canonical, RecordTag{is_union, false});
                    type = record_type(canonical, is_union);
                    declares_tag = true;
                } else if (type->kind != Type::Kind::Record) {
                    diagnostics_.error(name_location, "tag '" + *name +
                        "' was previously declared as an enumeration");
                } else if (type->is_union != is_union) {
                    diagnostics_.error(name_location,
                                       "record tag '" + *name +
                                           "' was previously declared with the other record kind");
                }
                remember_tag_binding(name_index, *name, type, declares_tag);
                for (const auto& attribute : record_attributes)
                    type_error(type, attribute.location,
                        "record attributes on a type use are not yet supported");
            }
        } else if ((co_await current_async()).is("enum")) {
            ProductionScope enumeration(*this, SyntaxProduction::EnumSpecifier);
            const auto keyword_index = index_;
            const auto location = (co_await current_async()).location;
            (co_await consume_async("enum"));
            const auto name_index = index_;
            const auto name_location = (co_await current_async()).location;
            const auto name = (co_await parse_qualified_name_async());
            auto enum_attributes = co_await parse_attributes_async();
            if (!name && !(co_await current_async()).is("{")) {
                (co_await error_here_async("an anonymous enumeration requires a definition"));
                co_return {};
            }
            const bool local_tag = name && !binds_at_file_scope() && name->find("::") == std::string::npos;
            if ((co_await current_async()).is("{")) {
                EnumDecl declaration;
                declaration.location = location;
                declaration.name = !name ? std::string{} : local_tag ? *name : join_namespace(active_namespace_, *name);
                declaration.local = local_tag || (!name && !binds_at_file_scope());
                if (name && !local_tag && record_types_.contains(declaration.name))
                    diagnostics_.error(name_location, "tag '" + declaration.name +
                        "' was previously declared as a record");
                if (declaration_attributes) {
                    std::vector<Attribute> remaining;
                    for (auto& attribute : *declaration_attributes) {
                        if (attribute.name == "underlying")
                            declaration.attributes.push_back(std::move(attribute));
                        else remaining.push_back(std::move(attribute));
                    }
                    *declaration_attributes = std::move(remaining);
                }
                declaration.attributes.insert(declaration.attributes.end(),
                    std::make_move_iterator(enum_attributes.begin()),
                    std::make_move_iterator(enum_attributes.end()));
                declaration.underlying = enum_underlying(declaration.attributes, declaration.captured_type_errors);
                if (!name) {
                    declaration.nominal_identity = new_nominal_identity(location);
                } else if (local_tag) {
                    type = declare_local_tag(enum_type(declaration), name_location, true);
                    declaration.nominal_identity = type->nominal_identity;
                    declaration.captured_type_errors = type->captured_tag_errors;
                }
                if (name && !local_tag) {
                    const auto found = enum_types_.find(declaration.name);
                    if (found != enum_types_.end() && found->second.underlying != declaration.underlying) {
                        diagnostics_.error(location, "enumeration '" + declaration.name +
                            "' redeclared with a different underlying type");
                    } else {
                        enum_types_[declaration.name] = {declaration.underlying, declaration.captured_type_errors};
                    }
                }
                type = enum_type(declaration);
                remember_tag_binding(name ? name_index : keyword_index, name ? *name : "enum", type, true);
                if (co_await defer_public_header_group_async(allow_public_header_deferral_
                        ? bounded_group_end(index_) : std::nullopt, [&]() -> EvaluationTask<void> {
                        co_await parse_enumerators_async(declaration, active_namespace_);
                    })) declaration.enumerators.clear();
                pending_enumerations_.push_back(std::move(declaration));
            } else if (local_tag && tag_declaration && (co_await current_async()).is(";")) {
                std::shared_ptr<const CapturedTypeErrors> errors;
                type = enum_type(*name, enum_underlying(enum_attributes, errors));
                type->captured_tag_errors = std::move(errors);
                type = declare_local_tag(std::move(type), name_location, false);
                remember_tag_binding(name_index, *name, type, true);
            } else {
                type = resolve_tag_type(*name, tokens_[name_index]);
                if (!type) {
                    diagnostics_.error(name_location,
                                       "unknown enumeration type '" + *name + "'");
                    type = enum_type(*name, BuiltinType::I32, is_const, is_volatile);
                } else if (type->kind == Type::Kind::Record) {
                    diagnostics_.error(name_location, "tag '" + *name +
                        "' was previously declared as a record");
                }
                remember_tag_binding(name_index, *name, type, false);
                for (const auto& attribute : enum_attributes)
                    type_error(type, attribute.location,
                        "enumeration attributes on a type use are not yet supported");
            }
        }
        std::optional<BuiltinType> kind;
        if (!type) kind = builtin_kind((co_await current_async()).text);
        std::string alias_name;
        if (!type) alias_name = co_await peek_qualified_name_async();
        const auto generic = std::find(active_generic_types_.begin(),
                                       active_generic_types_.end(), alias_name);
        const auto bound_alias = bound_type_alias(alias_name);
        const bool uncertain_header_type = allow_public_header_deferral_ && public_header_uncertain_names_ &&
            !bound_alias && !alias_name.empty() && alias_name.find("::") == std::string::npos &&
            !is_reserved_identifier(alias_name);
        if (uncertain_header_type) public_deferred_header_ = true;
        const auto alias = bound_alias ? bound_alias :
            (alias_name.empty() || generic != active_generic_types_.end() || uncertain_header_type
                ? AliasDefinitionPtr{} : resolve_type_alias(alias_name));
        const bool provisional_type = uncertain_header_type || (preparing_header_ &&
            (!parsing_public_fragment_ || probing_header_type_) &&
            (co_await current_async()).kind == TokenKind::Identifier && !is_reserved_identifier((co_await current_async()).text));
        if (!type && !kind && generic == active_generic_types_.end() && !alias && !provisional_type) {
            if (const auto message = familiar_c_spelling((co_await current_async()).text)) {
                (co_await error_here_async(*message));
                ++index_;
                co_return builtin_type(BuiltinType::I32, is_const, is_volatile);
            }
            (co_await error_here_async("expected Cross type"));
            co_return {};
        }
        if (!type) {
            if (alias) {
                ProductionScope alias_production(*this, SyntaxProduction::TypedefName);
                const auto name_index = index_;
                (void)(co_await parse_qualified_name_async());
                remember_alias_binding(name_index, alias_name, alias, false);
                type = alias->instantiate();
                type->is_const = type->is_const || is_const;
                type->is_volatile = type->is_volatile || is_volatile;
            } else {
                ProductionScope leaf(*this, kind ? SyntaxProduction::ScalarType
                                                 : SyntaxProduction::TypedefName);
                const auto spelling = identifier_binding_name((co_await current_async()));
                const auto generic_location = (co_await current_async()).location;
                if (kind)
                    ++index_;
                else
                    (void)(co_await parse_qualified_name_async());
                type = kind ? builtin_type(*kind, is_const, is_volatile)
                            : generic_type(spelling, is_const, is_volatile);
                if (!kind) {
                    type->generic_location = generic_location;
                    const auto origin = token_origin(generic_location);
                    type->generic_header_view = retaining_shared_specifiers_ &&
                        (!origin.context || !origin.context->parse_environment ||
                         origin.context->parse_environment->header_bindings == header_bindings_ ||
                         origin.context->parse_environment->header_bindings == shared_specifier_header_);
                    if (active_function_)
                        bind_generic_type(*type, active_function_->generic_parameters);
                    else
                        bind_generic_type(*type, {});
                }
            }
        }
    }
    type = rebind_tag_types(std::move(type));
    (co_await validate_captured_type_async(type));
    for (const auto& attribute : deferred_type_attributes)
        apply_type_attribute(type, attribute, &pending_address_space);
    while ((co_await current_async()).is("const") || (co_await current_async()).is("volatile") ||
           (co_await current_async()).is("restrict") || (co_await current_async()).is("[[") || (co_await storage_start_async())) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if ((co_await storage_start_async())) {
            if (!(co_await storage_specifier())) break;
        } else if ((co_await current_async()).is("[[")) {
            // Each written attribute_specifier owns its own occurrence.
            co_await consume_specifier_attributes(&type);
        } else {
            ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
            if ((co_await consume_async("const")))
                is_const = true;
            else if ((co_await consume_async("volatile")))
                is_volatile = true;
            else {
                restrict_location = (co_await current_async()).location;
                (co_await consume_async("restrict"));
                is_restrict = true;
            }
        }
    }
    type->is_const = type->is_const || is_const;
    type->is_volatile = type->is_volatile || is_volatile;
    type->is_restrict = type->is_restrict || is_restrict;
    if (type->is_restrict && type->kind != Type::Kind::Pointer) {
        type_error(type, restrict_location.value_or((co_await current_async()).location),
                           "restrict qualifier requires a pointer type");
    }
    type->pending_address_space = pending_address_space;
    co_return type;
}

EvaluationTask<std::vector<std::string>> Parser::preview_generic_types_async(bool* pending_fragments, bool* generic_header,
                                                     bool declarator_only, TokenIdentity* first_declarator) {
    // Generic bindings cover the complete function header, even when written
    // after the result type. Only discover bare type parameters here; normal
    // parsing still validates the declaration and every parameter. Do not
    // inspect a body, initializer, parameter attribute, or next declarator.
    std::vector<std::string> result;
    const auto execution = syntax_ && (pending_fragments || recording_public_tree_ || replacement_)
        ? syntax_->execution() : nullptr;
    const auto work = [&](std::size_t at) {
        if (!execution || execution->work(tokens_[at].location)) return true;
        if (recording_public_tree_) public_tree_failed_ = true;
        return false;
    };
    const auto closer = [](std::string_view text) -> std::string_view {
        if (text == "(") return ")";
        if (text == "[") return "]";
        if (text == "{") return "}";
        if (text == "[[") return "]]";
        return {};
    };
    const auto expose_async = [&](std::size_t at)  -> EvaluationTask<void> {
        if (pending_fragments) {
            if (at >= tokens_.size() || tokens_[at].kind != TokenKind::Identifier) co_return;
            while (at + 2 < tokens_.size() && tokens_[at + 1].is("::") &&
                   tokens_[at + 2].kind == TokenKind::Identifier) at += 2;
            if (at + 2 < tokens_.size() && tokens_[at + 1].is("!") &&
                (tokens_[at + 2].is("(") || tokens_[at + 2].is("[") ||
                 tokens_[at + 2].is("{"))) *pending_fragments = true;
            co_return;
        }
        if (parsing_public_fragment_ || preparing_header_ || !syntax_) co_return;
        struct Restore {
            std::size_t& cursor;
            std::size_t saved;
            ~Restore() { cursor = saved; }
        } restore{index_, index_};
        index_ = at;
        co_await normalize_qualified_name_async();
    };
    std::size_t declarator_depth = 0;
    const auto group_end_async = [&](std::size_t first, bool parameters = false) -> EvaluationTask<std::optional<std::size_t>> {
        if (execution && declarator_depth >= execution->limits().depth) {
            execution->tree_limit_error(tokens_[first].location);
            if (recording_public_tree_) public_tree_failed_ = true;
            co_return {};
        }
        std::vector<std::string_view> closes{closer(tokens_[first].text)};
        for (auto at = first + 1; at < tokens_.size(); ++at) {
            if (parameters && closes.size() == 1) (co_await expose_async(at));
            if (!work(at)) co_return {};
            if (tokens_[at].kind == TokenKind::End) co_return {};
            if (const auto close = closer(tokens_[at].text); !close.empty()) {
                if (execution && closes.size() + declarator_depth >= execution->limits().depth) {
                    execution->tree_limit_error(tokens_[at].location);
                    if (recording_public_tree_) public_tree_failed_ = true;
                    co_return {};
                }
                closes.push_back(close);
            } else if (tokens_[at].is(")") || tokens_[at].is("]") ||
                       tokens_[at].is("}") || tokens_[at].is("]]")) {
                if (closes.back() != tokens_[at].text) co_return {};
                closes.pop_back();
                if (closes.empty()) co_return at;
            }
        }
        co_return {};
    };
    const auto append_types_async = [&](std::size_t first, std::size_t end)  -> EvaluationTask<void> {
        auto begin = first;
        for (auto at = first; at <= end; ++at) {
            if (at != end) {
                if (!work(at)) co_return;
                if (!closer(tokens_[at].text).empty()) {
                    const auto close = (co_await group_end_async(at));
                    if (!close || *close >= end) co_return;
                    at = *close;
                    continue;
                }
                if (!tokens_[at].is(",")) continue;
            }
            if (at == begin + 1 && tokens_[begin].kind == TokenKind::Identifier) {
                const auto name = identifier_binding_name(tokens_[begin]);
                if (std::find(result.begin(), result.end(), name) == result.end())
                    result.push_back(name);
            }
            begin = at + 1;
        }
    };
    bool have_type = declarator_only;
    bool have_name = false;
    const auto name_end = [&](std::size_t at) {
        while (at + 2 < tokens_.size() && tokens_[at + 1].is("::") &&
               tokens_[at + 2].kind == TokenKind::Identifier) at += 2;
        return at;
    };
    const auto macro_end_async = [&](std::size_t last_name) -> EvaluationTask<std::optional<std::size_t>> {
        if (last_name + 2 < tokens_.size() && tokens_[last_name + 1].is("!") &&
            (tokens_[last_name + 2].is("(") || tokens_[last_name + 2].is("[") ||
             tokens_[last_name + 2].is("{")))
            // A malformed invocation is still opaque. Stop discovery at the
            // fence instead of descending into its unbalanced raw input.
            co_return (co_await group_end_async(last_name + 2)).value_or(tokens_.size() - 1);
        co_return {};
    };
    for (auto cursor = index_; cursor < tokens_.size(); ++cursor) {
        // Header fragments can introduce generics that scope over the earlier
        // result type. Expose attribute names and generic-parameter sequences,
        // but never inspect expression arguments or a function's body.
        (co_await expose_async(cursor));
        if (!work(cursor)) break;
        if (tokens_[cursor].kind == TokenKind::End || tokens_[cursor].is(";") ||
            tokens_[cursor].is("{") || tokens_[cursor].is("=") ||
            tokens_[cursor].is(",")) break;
        if (tokens_[cursor].is("struct") || tokens_[cursor].is("union") ||
            tokens_[cursor].is("enum")) {
            // A written tag definition is part of the result specifier, not
            // the function body. Its members/enumerators and tag attributes
            // cannot supply outer header bindings merely by containing names.
            // Skip only the independently proved written groups here; their
            // surviving expansions run through normal grammar preparation.
            auto at = cursor + 1;
            (co_await expose_async(at));
            if (at < tokens_.size() && tokens_[at].kind == TokenKind::Identifier) {
                const auto last_name = name_end(at);
                for (; at <= last_name; ++at)
                    if (!work(at)) co_return result;
                if (const auto end = (co_await macro_end_async(last_name))) at = *end + 1;
            }
            while (at < tokens_.size() && tokens_[at].is("[[")) {
                const auto end = (co_await group_end_async(at));
                if (!end) co_return result;
                at = *end + 1;
            }
            if (at < tokens_.size() && tokens_[at].is("{")) {
                const auto end = (co_await group_end_async(at));
                if (!end) co_return result;
                at = *end + 1;
            }
            have_type = true;
            cursor = at - 1;
            continue;
        }
        if (tokens_[cursor].is("[[")) {
            auto at = cursor + 1;
            while (at < tokens_.size()) {
                (co_await expose_async(at));
                if (tokens_[at].kind != TokenKind::Identifier) break;
                const auto name = at++;
                bool qualified = false;
                (co_await expose_async(at));
                while (at + 1 < tokens_.size() && tokens_[at].is("::")) {
                    qualified = true;
                    (co_await expose_async(++at));
                    if (tokens_[at].kind != TokenKind::Identifier) break;
                    (co_await expose_async(++at));
                }
                if (at < tokens_.size() && tokens_[at].is("(")) {
                    const bool generic = !qualified && tokens_[name].is("generic");
                    const auto arguments_end = (co_await group_end_async(at, generic));
                    if (!arguments_end) break;
                    if (generic) {
                        if (generic_header) *generic_header = true;
                        (co_await append_types_async(at + 1, *arguments_end));
                    }
                    at = *arguments_end + 1;
                }
                (co_await expose_async(at));
                if (at >= tokens_.size() || !tokens_[at].is(",")) break;
                ++at;
            }
            const auto close = (co_await group_end_async(cursor));
            if (!close) break;
            cursor = *close;
        } else if (tokens_[cursor].is("<") && have_name && cursor != 0 &&
                   tokens_[cursor - 1].kind == TokenKind::Identifier) {
            unsigned depth = 1;
            auto close = cursor + 1;
            for (; close < tokens_.size(); ++close) {
                if (depth == 1) (co_await expose_async(close));
                if (!work(close)) co_return result;
                if (!closer(tokens_[close].text).empty()) {
                    const auto end = (co_await group_end_async(close));
                    if (!end) co_return result;
                    close = *end;
                } else if (tokens_[close].is("<")) ++depth;
                else if (tokens_[close].is(">")) --depth;
                else if (tokens_[close].is(">>")) {
                    if (depth < 2) break;
                    depth -= 2;
                } else if (tokens_[close].kind == TokenKind::End ||
                           tokens_[close].is(";") || tokens_[close].is("=")) break;
                if (depth == 0) break;
            }
            if (close + 1 < tokens_.size()) (co_await expose_async(close + 1));
            if (depth != 0 || close + 1 >= tokens_.size() ||
                (!tokens_[close + 1].is("(") &&
                 !(declarator_depth && tokens_[close + 1].is(")")))) break;
            if (generic_header) *generic_header = true;
            (co_await append_types_async(cursor + 1, close));
            cursor = close;
        } else if (tokens_[cursor].is("(") && have_type && !have_name) {
            // A required named declarator may group its name (and its angle
            // parameters). Descend only until that name is found; parameter
            // lists and expression groups remain opaque. Macro inputs are
            // skipped below as units, never mistaken for declarator grouping.
            if (execution && declarator_depth >= execution->limits().depth) {
                execution->tree_limit_error(tokens_[cursor].location);
                if (recording_public_tree_) public_tree_failed_ = true;
                break;
            }
            ++declarator_depth;
        } else if (tokens_[cursor].is(")")) {
            if (!declarator_depth) break;
            --declarator_depth;
        } else if (!closer(tokens_[cursor].text).empty()) {
            const auto close = (co_await group_end_async(cursor));
            if (!close) break;
            cursor = *close;
        } else if (tokens_[cursor].kind == TokenKind::Identifier &&
                   !is_reserved_identifier(tokens_[cursor].text)) {
            const auto last_name = name_end(cursor);
            for (auto at = cursor + 1; at <= last_name; ++at)
                if (!work(at)) co_return result;
            if (have_type) {
                if (!have_name && first_declarator)
                    *first_declarator = token_origin(tokens_[cursor].location).identity;
                have_name = true;
            }
            else have_type = true;
            cursor = (co_await macro_end_async(last_name)).value_or(last_name);
        } else if (builtin_kind(tokens_[cursor].text) ||
                   tokens_[cursor].kind == TokenKind::BuiltinName ||
                   tokens_[cursor].kind == TokenKind::StructuredSplice ||
                   tokens_[cursor].kind == TokenKind::PreparedFragment) {
            have_type = true;
        }
    }
    co_return result;
}

EvaluationTask<std::vector<FunctionDecl::GenericParameter>>
Parser::parse_angle_generic_parameters_async() {
    if (!allow_public_header_deferral_) co_return co_await parse_angle_generic_parameters_impl_async();
    std::vector<FunctionDecl::GenericParameter> result;
    if (co_await defer_public_header_group_async(generic_parameter_group_end(index_), [&]() -> EvaluationTask<void> {
            result = co_await parse_angle_generic_parameters_impl_async();
        })) result.clear();
    co_return result;
}

std::optional<std::size_t> Parser::generic_parameter_group_end(std::size_t first) {
    if (first >= tokens_.size() || !tokens_[first].is("<")) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    unsigned depth = 1;
    for (auto at = first + 1; at < tokens_.size(); ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        if (tokens_[at].kind == TokenKind::End || tokens_[at].is(";")) return {};
        if (tokens_[at].is("(") || tokens_[at].is("[") || tokens_[at].is("[[") || tokens_[at].is("{")) {
            const auto end = bounded_group_end(at);
            if (!end) return {};
            at = *end - 1;
        } else if (tokens_[at].is("<")) {
            if (depth >= limits.depth) {
                public_tree_failed_ = true;
                if (execution) execution->tree_limit_error(tokens_[at].location);
                return {};
            }
            ++depth;
        } else if (tokens_[at].is(">") || tokens_[at].is(">>")) {
            const unsigned closed = tokens_[at].is(">>") ? 2 : 1;
            if (closed > depth) return {};
            depth -= closed;
            if (!depth) return at + 1;
        } else if (tokens_[at].is(")") || tokens_[at].is("]") ||
                   tokens_[at].is("]]") || tokens_[at].is("}")) return {};
    }
    return {};
}

EvaluationTask<std::vector<FunctionDecl::GenericParameter>>
Parser::parse_angle_generic_parameters_impl_async() {
    ProductionScope list(*this, SyntaxProduction::GenericParameterList);
    std::vector<FunctionDecl::GenericParameter> result;
    (co_await consume_async("<"));
    if ((co_await consume_async(">"))) {
        (co_await error_here_async("a generic parameter list cannot be empty"));
        co_return result;
    }
    for (;;) {
        ProductionScope parameter(*this, SyntaxProduction::GenericParameter);
        auto location = (co_await current_async()).location;
        std::optional<std::string> name;
        TypePtr value_type;
        if ((co_await current_async()).kind == TokenKind::Identifier &&
            ((co_await current_async(1)).is(",") || (co_await current_async(1)).is(">"))) {
            name = identifier_binding_name((co_await current_async()));
            ++index_;
        } else {
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                value_type = co_await parse_type_async();
                if (value_type)
                    value_type = co_await parse_declarator_async(std::move(value_type), name, DeclaratorContext::TypePrefix);
            }
            if (const auto token = (co_await consume_kind_async(TokenKind::Identifier))) {
                name = identifier_binding_name(*token);
                location = token->location;
            }
            if (value_type && !is_integer(value_type) &&
                value_type->kind != Type::Kind::Pointer &&
                !(public_header_uncertain_names_ && value_type->kind == Type::Kind::Generic) &&
                !(value_type->kind == Type::Kind::Builtin &&
                  value_type->builtin == BuiltinType::Label))
                diagnostics_.error(location,
                    "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
        }
        if (!name || name->find("::") != std::string::npos) {
            diagnostics_.error(location,
                               "expected an unqualified generic parameter name");
        } else if (std::any_of(result.begin(), result.end(),
                               [&](const auto& existing) {
                                   return existing.name == *name;
                               })) {
            diagnostics_.error(location,
                               "duplicate generic parameter '" + *name + "'");
        } else {
            if (!value_type) active_generic_types_.push_back(*name);
            result.push_back({std::move(*name), std::move(value_type), location});
        }
        parameter.finish();
        if (!(co_await consume_async(","))) break;
        if ((co_await current_async()).is(">")) {
            (co_await error_here_async("a generic parameter list cannot have a trailing comma"));
            break;
        }
    }
    (co_await expect_async(">", "after generic parameters"));
    co_return result;
}

void Parser::remember_ordinary_name(std::string_view name, OrdinaryNameKind kind,
                                    SourceLocation location, ValueBinding binding) {
    if (location.valid() && !active_namespace_.empty() && name.starts_with(active_namespace_ + "::"))
        remember_fragment_name(name.substr(active_namespace_.size() + 2), name, location,
            FragmentNameDomain::Ordinary, binding);
    if (known_ordinary_values_->entries.contains(std::string(name))) return;
    if (!location.valid()) location = std::as_const(*this).current().location;
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    if (execution && !execution->work(location,
            known_ordinary_values_.unique() ? 1 : known_ordinary_values_->entries.size() + 1)) {
        public_tree_failed_ = true;
        return;
    }
    if (!known_ordinary_values_.unique())
        known_ordinary_values_ = std::make_shared<OrdinaryNames>(*known_ordinary_values_);
    known_ordinary_values_->entries.emplace(name, OrdinaryName{kind, binding});
    known_ordinary_values_->storage += 128 + name.size();
    known_ordinary_values_->storage += value_binding_storage(binding, {});
}

const ValueBinding* Parser::destination_value_binding(const std::string& name) const {
    if (declaration_destination_)
        if (const auto* binding = declaration_destination_->destination_value_binding(name))
            return binding;
    if (const auto found = known_ordinary_values_->entries.find(name);
        found != known_ordinary_values_->entries.end()) return &found->second.binding;
    if (const auto found = known_functions_.find(name); found != known_functions_.end())
        return &found->second.binding;
    return nullptr;
}

ValueBinding Parser::bind_value(std::string_view name, SourceLocation location,
                               std::size_t first, ValueBinding::Kind kind,
                               GenericSignature parameters) {
    const auto origin = token_origin(location);
    const NameKey key(name, location);
    auto identity = std::make_shared<ValuePlacementIdentity>();
    identity->kind = kind == ValueBinding::Kind::Function
        ? ValuePlacementIdentity::Kind::Function : ValuePlacementIdentity::Kind::Object;
    identity->name = kind == ValueBinding::Kind::Local
        ? std::string(name) : join_namespace(active_namespace_, name);
    identity->generic_parameters = std::move(parameters);
    std::shared_ptr<const ValuePlacementIdentity> placement = identity;
    if (kind != ValueBinding::Kind::Local) {
        if (const auto* previous = destination_value_binding(identity->name);
            previous && previous->kind == kind && previous->placement)
            placement = previous->placement;
    }
    ValueBinding binding{kind, origin.identity, key.context, {}, std::move(placement)};
    for (auto at = first; at < index_; ++at) {
        auto& token = tokens_[at];
        if (syntax_ && !syntax_->execution()->work(token.location)) {
            public_tree_failed_ = true;
            return binding;
        }
        if (token.location.file != location.file || token.location.offset != location.offset) continue;
        auto incoming = token.value_binding.kind != ValueBinding::Kind::Unknown
            ? token.value_binding : origin.value_binding;
        if (incoming.kind == ValueBinding::Kind::Unknown && origin.declaration_source)
            incoming = *origin.declaration_source;
        if (incoming.kind == ValueBinding::Kind::Unknown)
            incoming = retained_value_binding(name, location);
        if ((incoming.kind == ValueBinding::Kind::Local || incoming.kind == ValueBinding::Kind::Object ||
             incoming.kind == ValueBinding::Kind::Function) &&
            incoming != binding && incoming.declaration == origin.identity) {
            if (binding.placement == identity) {
                identity->source_binding = std::make_shared<const ValueBinding>(incoming);
                identity->source_storage = 96 + value_binding_storage(incoming, {});
            }
            auto ancestor = incoming;
            for (;;) {
                if (ancestor == binding) break;
                if (syntax_ && !syntax_->execution()->work(token.location)) {
                    public_tree_failed_ = true;
                    return binding;
                }
                value_rebindings_[ancestor] = binding;
                if (!ancestor.placement || !ancestor.placement->source_binding) break;
                ancestor = *ancestor.placement->source_binding;
            }
        }
        if (!preparing_header_) {
            remember_specifier_input(at);
            token.value_binding = binding;
            token.value_spelling = std::make_shared<const std::string>(name);
        }
        break;
    }
    if (kind != ValueBinding::Kind::Local)
        remember_fragment_name(name, binding.placement->name, location, FragmentNameDomain::Ordinary, binding);
    return binding;
}

ValueBinding Parser::nonlocal_value_binding(std::string_view name, SourceLocation location,
                                            const NameLookupContext& context) const {
    const auto origin = token_origin(location);
    const auto* captured = origin.value_context_captured && origin.context
        ? origin.context->parse_environment.get() : nullptr;
    const auto& values = captured && captured->ordinary_values
        ? captured->ordinary_values->entries : known_ordinary_values_->entries;
    const auto& functions = captured ? captured->functions : known_functions_;
    NameUse use(name, location);
    use.context = &context;
    for (const auto& candidate : namespace_candidates(use)) {
        if (const auto found = values.find(candidate); found != values.end())
            return found->second.binding;
        if (const auto found = functions.find(candidate); found != functions.end())
            return found->second.binding;
    }
    return {};
}

void Parser::remember_function(const FunctionDecl& function) {
    if (function.generic_parameters.empty()) {
        remember_ordinary_name(function.name, OrdinaryNameKind::Function, function.location, function.binding);
        return;
    }
    GenericSignature signature;
    for (const auto& parameter : function.generic_parameters)
        signature.push_back(parameter.value_type ? GenericParameterKind::Value : GenericParameterKind::Type);
    known_functions_.try_emplace(function.name, GenericFunction{std::move(signature), function.binding});
}

const Parser::GenericSignature* Parser::known_generic_parameters(const Expr& expression) const {
    const auto* designator = &expression;
    while (designator->kind == Expr::Kind::Parenthesized && designator->left)
        designator = designator->left.get();
    if (designator->kind != Expr::Kind::Name) return nullptr;
    const auto& name = *designator;
    const auto key = name_key(name);
    const auto binding = key.binding.kind;
    if (binding == ValueBinding::Kind::Function && key.binding.placement) {
        const auto& parameters = key.binding.placement->generic_parameters;
        return parameters.empty() ? nullptr : &parameters;
    }
    if (binding == ValueBinding::Kind::Local || binding == ValueBinding::Kind::Enumerator ||
        binding == ValueBinding::Kind::Object) return nullptr;
    if (binding == ValueBinding::Kind::Unknown && current().is("<"))
        require_public_name_context(name.text, name.location);
    if (binding == ValueBinding::Kind::Unknown && name.text.find("::") == std::string::npos)
        for (auto scope = local_scopes_.rbegin();
             scope != local_scopes_.rend(); ++scope)
            if (scope->contains(NameKey(name.text, name.location))) return nullptr;
    for (const auto& candidate : namespace_candidates(
             NameUse{name}, active_namespace_, active_imports_)) {
        if (known_ordinary_values_->entries.contains(candidate)) return nullptr;
        if (const auto found = known_functions_.find(candidate);
            found != known_functions_.end() && !found->second.parameters.empty()) return &found->second.parameters;
    }
    return nullptr;
}

bool Parser::known_generic_name(const Expr& name) const {
    return known_generic_parameters(name) != nullptr;
}

EvaluationTask<bool> Parser::consume_generic_close_async() {
    if ((co_await consume_async(">"))) co_return true;
    const auto token = co_await current_async();
    if (!token.is(">>")) co_return false;
    const auto source = token.split_source ? token.split_source
        : std::make_shared<const SplitTokenSource>(SplitTokenSource{
            token.kind, std::string(token.text)});
    auto remainder = token;
    remainder.text = ">";
    remainder.split_source = source;
    remainder.split_offset = token.split_offset + 1;
    ++remainder.location.offset;
    ++remainder.location.column;
    tokens_[index_].text = ">";
    tokens_[index_].split_source = source;
    tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(index_) + 1,
                   remainder);
    if (recording_public_tree_ || probing_header_type_)
        public_input_indices_.insert(public_input_indices_.begin() +
            static_cast<std::ptrdiff_t>(index_) + 1, public_input_indices_[index_]);
    ++index_;
    co_return true;
}

TypePtr Parser::parse_declarator(TypePtr base, std::optional<std::string>& name, DeclaratorContext context,
                                 std::unique_ptr<Expr>* dynamic_outer_bound,
                                 SourceLocation* name_location,
                                 std::vector<FunctionDecl::GenericParameter>* angle_parameters,
                                 std::size_t* name_token_index, bool share_prototype_scope) {
    return parse_declarator_async(std::move(base), name, context, dynamic_outer_bound,
        name_location, angle_parameters, name_token_index, share_prototype_scope).run();
}

EvaluationTask<TypePtr> Parser::parse_declarator_async(TypePtr base, std::optional<std::string>& name, DeclaratorContext context,
                                 std::unique_ptr<Expr>* dynamic_outer_bound,
                                 SourceLocation* name_location,
                                 std::vector<FunctionDecl::GenericParameter>* angle_parameters,
                                 std::size_t* name_token_index, bool share_prototype_scope,
                                 std::vector<Attribute>* entity_suffix_attributes,
                                 bool parenthesized_component) {
    PrototypeScopeFrame prototype_frame(*this, share_prototype_scope);
    const bool parameter = context == DeclaratorContext::Parameter;
    const bool abstract_only = context == DeclaratorContext::TypePrefix;
    (co_await expand_inline_macro_fragments_async());
    const bool written = (co_await current_async()).is("*") || (co_await current_async()).is("(") || (co_await current_async()).is("[") ||
                         (!abstract_only && (co_await current_async()).kind == TokenKind::Identifier);
    ProductionScope declarator(*this,
                               written ? SyntaxProduction::Declarator : SyntaxProduction::None);
    auto pending_address_space = base ? base->pending_address_space
                                      : std::optional<std::pair<std::uint32_t, SourceLocation>>{};
    if (pending_address_space) {
        // A member list reuses its base specifiers for each declarator.
        // Consuming one declarator must not mutate that shared base.
        base = std::make_shared<Type>(*base);
        base->pending_address_space.reset();
    }
    while ((co_await current_async()).is("*")) {
        ProductionScope pointer(*this, SyntaxProduction::PointerPart);
        (co_await consume_async("*"));
        base = pointer_type(std::move(base));
        if (pending_address_space) {
            base->address_space = pending_address_space->first;
            base->address_space_location = pending_address_space->second;
            pending_address_space.reset();
        }
        for (;;) {
            (co_await expand_inline_macro_fragments_async());
            if (!(co_await current_async()).is("const") && !(co_await current_async()).is("volatile") &&
                !(co_await current_async()).is("restrict") && !(co_await current_async()).is("[[")) break;
            if ((co_await current_async()).is("[[")) {
                co_await apply_type_attributes_async(base);
                continue;
            }
            ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
            if ((co_await consume_async("const")))
                base->is_const = true;
            else if ((co_await consume_async("volatile")))
                base->is_volatile = true;
            else {
                (co_await consume_async("restrict"));
                base->is_restrict = true;
            }
        }
    }
    (co_await expand_inline_macro_fragments_async());
    const auto grouped_probe = [&]() -> EvaluationTask<bool> {
        if (!(co_await current_async()).is("(")) co_return false;
        struct Restore {
            std::size_t& index;
            std::size_t previous;
            ~Restore() { index = previous; }
        } restore{index_, index_};
        ++index_;
        // This is a reached declarator boundary, not a speculative scan of
        // an expression owner's input. Public recognition still throws before
        // expansion so a bounded enclosing capture may defer instead.
        (co_await expand_inline_macro_fragments_async());
        if ((co_await current_async()).is("*") || (co_await current_async()).is("(") || (co_await current_async()).is("[")) co_return true;
        if ((co_await current_async()).kind != TokenKind::Identifier || is_reserved_identifier((co_await current_async()).text))
            co_return false;
        // A required declared name can shadow a type. In optional/abstract
        // contexts an established type instead starts an unnamed parameter
        // list, preserving the existing callable-type interpretation.
        if (context == DeclaratorContext::Named) co_return true;
        co_return !(co_await type_start_async());
    };
    const bool grouped = co_await grouped_probe();
    if (pending_address_space) {
        if (grouped)
            base->pending_address_space = pending_address_space;
        else if (base->kind != Type::Kind::Pointer) {
            type_error(base, pending_address_space->second,
                               "address_space requires a pointer type");
        } else if (base->address_space_location.valid()) {
            type_error(base, pending_address_space->second,
                               "duplicate address_space type qualifier");
        } else {
            base->address_space = pending_address_space->first;
            base->address_space_location = pending_address_space->second;
        }
    }
    TypePtr nested;
    TypePtr hole;
    const auto direct_event =
        begin_production(written ? SyntaxProduction::DirectDeclarator : SyntaxProduction::None);
    if (grouped) {
        (co_await consume_async("("));
        hole = std::make_shared<Type>();
        nested = co_await parse_declarator_async(hole, name, context, nullptr, name_location,
                                   angle_parameters, name_token_index, true,
                                   entity_suffix_attributes, true);
        (co_await expect_async(")", "after parenthesized declarator"));
    } else if (!abstract_only) {
        const auto location = (co_await current_async()).location;
        const auto token_index = index_;
        name = (co_await parse_qualified_name_async());
        if (probing_header_type_ && context == DeclaratorContext::TypeName && name)
            throw HeaderProbeRejected{};
        if (name && name_location)
            *name_location = location;
        if (name && name_token_index) *name_token_index = token_index;
        if (name && angle_parameters && (co_await current_async()).is("<"))
            *angle_parameters = co_await parse_angle_generic_parameters_async();
    }
    (co_await expand_inline_macro_fragments_async());
    if ((co_await current_async()).is("[")) {
        base = co_await parse_array_suffix_async(std::move(base), parameter, dynamic_outer_bound);
    }
    (co_await expand_inline_macro_fragments_async());
    if ((co_await current_async()).is("(")) {
        ProductionScope suffix(*this, SyntaxProduction::FunctionSuffix);
        std::vector<ParameterDecl> parameters;
        bool variadic = false;
        if (co_await defer_public_header_group_async(allow_public_header_deferral_
                ? bounded_group_end(index_) : std::nullopt, [&]() -> EvaluationTask<void> {
                (co_await consume_async("("));
                co_await parse_parameter_list_async(parameters, variadic);
                (co_await expect_async(")", "after function parameters"));
            })) {
            parameters.clear();
            variadic = false;
        }
        base = function_type(std::move(base), std::move(parameters), variadic);
        (co_await expand_inline_macro_fragments_async());
        if ((co_await current_async()).is("->")) {
            ProductionScope result_location(*this, SyntaxProduction::ResultLocation);
            (co_await consume_async("->"));
            const auto location_token = (co_await consume_kind_async(TokenKind::String));
            if (!location_token) {
                (co_await error_here_async("expected result location string after '->'"));
            } else {
                base->function->result_location = decode_string_literal(location_token->text);
                if (!base->function->result_location)
                    diagnostics_.error(location_token->location, "invalid result location string");
            }
        }
        // In a grouped declarator the attributes follow this function
        // suffix, even if another callable component surrounds it.
        const bool nested_callable = (nested && nested != hole) || !name;
        if (nested_callable || parenthesized_component) {
            auto suffix_attributes = co_await parse_attributes_async();
            apply_callable_attributes(base, suffix_attributes);
            if (!nested_callable && entity_suffix_attributes) {
                // A named function inside grouping still denotes the declared
                // entity. Preserve its body/generic attributes for that entity,
                // not for another callable formed by the outer suffix.
                entity_suffix_attributes->insert(entity_suffix_attributes->end(),
                    std::make_move_iterator(suffix_attributes.begin()),
                    std::make_move_iterator(suffix_attributes.end()));
            } else {
                for (const auto& attribute : suffix_attributes) {
                    if (attribute.name != "abi" && attribute.name != "clobber" &&
                        attribute.name != "stack_cleanup")
                        type_error(base,
                            attribute.location,
                            "function-only attribute cannot qualify a nested callable type");
                }
            }
        }
    }
    end_production(direct_event);
    if (recording_public_tree_ && declarator.event < production_events_.size() && !name) {
        production_events_[declarator.event].production = SyntaxProduction::AbstractDeclarator;
        flatten_production(direct_event);
    }
    if (nested) {
        // The inner declarator binds first. Fill its unique placeholder only
        // after the outer suffix has formed the result/element type.
        TypePtr* attributed_base = &base;
        while (*attributed_base) {
            if ((*attributed_base)->kind == Type::Kind::Array) {
                attributed_base = &(*attributed_base)->element;
            } else if ((*attributed_base)->kind == Type::Kind::Function &&
                       (*attributed_base)->function) {
                attributed_base = &(*attributed_base)->function->result;
            } else {
                break;
            }
        }
        auto nested_address_space = *attributed_base
            ? (*attributed_base)->pending_address_space
            : std::optional<std::pair<std::uint32_t, SourceLocation>>{};
        if (*attributed_base) {
            (*attributed_base)->pending_address_space.reset();
        }
        auto* component = &nested;
        while (*component) {
            auto& type = *component;
            if (type == hole) {
                type = base;
                break;
            }
            if (type->kind == Type::Kind::Pointer) {
                if (type->pointee == hole && nested_address_space) {
                    if (type->address_space_location.valid()) {
                        type_error(type,
                            nested_address_space->second,
                            "duplicate address_space type qualifier");
                    } else {
                        type->address_space = nested_address_space->first;
                        type->address_space_location =
                            nested_address_space->second;
                    }
                    nested_address_space.reset();
                }
                component = &type->pointee;
            } else if (type->kind == Type::Kind::Array)
                component = &type->element;
            else if (type->kind == Type::Kind::Function && type->function)
                component = &type->function->result;
            else break;
        }
        if (nested_address_space) {
            type_error(nested, nested_address_space->second,
                               "address_space requires a pointer type");
        }
        base = std::move(nested);
    }
    co_return base;
}

void Parser::apply_callable_attributes(
    TypePtr& type, const std::vector<Attribute>& attributes) {
    const auto reject = [&](SourceLocation location, std::string message) {
        type_error(type, location, std::move(message));
    };
    for (const auto& attribute : attributes) {
        if (attribute.name != "abi" && attribute.name != "clobber" &&
            attribute.name != "stack_cleanup") continue;
        if (type && type->kind != Type::Kind::Function) {
            const auto count = [&](const auto& self, const TypePtr& item)
                                   -> unsigned {
                if (!item) return 0;
                if (item->kind == Type::Kind::Pointer)
                    return self(self, item->pointee);
                if (item->kind == Type::Kind::Array)
                    return self(self, item->element);
                if (item->kind == Type::Kind::Function && item->function)
                    return 1 + self(self, item->function->result);
                return 0;
            };
            if (count(count, type) > 1) {
                reject(
                    attribute.location,
                    "leading callable attribute is ambiguous between nested function types");
                continue;
            }
        }
        TypePtr* node = &type;
        while (*node && ((*node)->kind == Type::Kind::Pointer ||
                         (*node)->kind == Type::Kind::Array)) {
            *node = std::make_shared<Type>(**node);
            node = (*node)->kind == Type::Kind::Pointer ? &(*node)->pointee
                                                        : &(*node)->element;
        }
        if (!*node || (*node)->kind != Type::Kind::Function ||
            !(*node)->function) {
            reject(
                attribute.location,
                "callable attribute requires a function or function-pointer type");
            continue;
        }
        *node = std::make_shared<Type>(**node);
        (*node)->function = std::make_shared<FunctionType>(*(*node)->function);
        auto& signature = *(*node)->function;
        if (attribute.name == "clobber") {
            if (attribute.arguments.empty()) {
                reject(attribute.location,
                                   "'clobber' requires string arguments");
            }
            for (const auto& argument : attribute.arguments) {
                const auto resource = decode_string_literal(argument);
                if (!resource || resource->empty())
                    reject(attribute.location,
                                       "clobber arguments must be nonempty strings");
                else
                    signature.clobbers.push_back(*resource);
            }
            continue;
        }
        const auto spelling = attribute.arguments.size() == 1
                                  ? decode_string_literal(attribute.arguments.front())
                                  : std::nullopt;
        if (!spelling || spelling->empty()) {
            reject(attribute.location,
                               "'" + attribute.name +
                                   "' requires one nonempty string");
            continue;
        }
        if (attribute.name == "abi") signature.abi = *spelling;
        else signature.stack_cleanup = *spelling;
    }
}

EvaluationTask<TypePtr> Parser::parse_array_suffix_async(
    TypePtr element, bool parameter,
    std::unique_ptr<Expr>* dynamic_outer_bound) {
    struct Bound { std::uint32_t count; std::shared_ptr<const Expr> expression{}; };
    std::vector<Bound> bounds;
    while ((co_await current_async()).is("[")) {
        ProductionScope array_suffix(*this, SyntaxProduction::ArraySuffix);
        if (co_await defer_public_header_group_async(allow_public_header_deferral_
                ? bounded_group_end(index_) : std::nullopt, [&]() -> EvaluationTask<void> {
        (co_await consume_async("["));
        if ((co_await consume_async("]"))) {
            if (parameter) {
                diagnostics_.error(
                    (co_await current_async()).location,
                    "an array parameter requires a positive fixed bound in the bootstrap compiler");
                bounds.push_back({1});
            } else {
                bounds.push_back({0});
            }
            co_return;
        }
        const auto location = (co_await current_async()).location;
        auto expression = co_await parse_assignment_async();
        (co_await expect_async("]", "after array bound"));
        const auto value = expression ? constant_value(*expression, address_bits_)
                                      : std::nullopt;
        const auto valid_extent = [&] {
            if (!value || value->value == UInt128{} || value->value.high != 0 ||
                value->value.low > std::numeric_limits<std::uint32_t>::max()) return false;
            const auto signed_type = value->type == BuiltinType::I8 || value->type == BuiltinType::I16 ||
                value->type == BuiltinType::I32 || value->type == BuiltinType::I64 ||
                value->type == BuiltinType::I128 || value->type == BuiltinType::Iptr;
            const auto bits = value->type == BuiltinType::Iptr ? address_bits_ : type_bits(builtin_type(value->type));
            return !signed_type || (bits && !(shift_right(value->value, bits - 1).low & 1U));
        };
        if (!value && dynamic_outer_bound && bounds.empty() &&
            !*dynamic_outer_bound) {
            *dynamic_outer_bound = std::move(expression);
            bounds.push_back({0});
        } else if (!value && expression) {
            bounds.push_back({0, std::shared_ptr<const Expr>(std::move(expression))});
        } else if (!valid_extent()) {
            diagnostics_.error(
                location,
                dynamic_outer_bound
                    ? "only the outermost array bound may be a runtime value"
                    : "fixed array bound must be a positive integer translation-time value");
            bounds.push_back({1});
        } else {
            bounds.push_back({static_cast<std::uint32_t>(value->value.low)});
        }
        })) bounds.push_back({0});
    }
    for (auto bound = bounds.rbegin(); bound != bounds.rend(); ++bound) {
        element = array_type(std::move(element), bound->count);
        element->array_bound = std::move(bound->expression);
        if (element->array_bound) header_type_bounds_.insert(element->array_bound.get());
    }
    co_return element;
}

std::vector<FunctionDecl::GenericParameter> Parser::generic_parameters(
    const std::vector<Attribute>& attributes) {
    std::vector<FunctionDecl::GenericParameter> result;
    bool seen = false;
    for (const auto& attribute : attributes) {
        if (attribute.name != "generic") continue;
        if (seen) {
            diagnostics_.error(attribute.location,
                               "a function has at most one 'generic' attribute");
            continue;
        }
        seen = true;
        result = attribute.generic_parameters;
    }
    return result;
}

Program Parser::parse() {
    return parse_async().run();
}

EvaluationTask<Program> Parser::parse_async() {
    ResourceEpoch resource_epoch(*this);
    Program program;
    while ((co_await current_async()).kind != TokenKind::End) {
        const auto before = index_;
        co_await parse_external_async(program, {});
        // A top-level stray closing brace is a synchronization boundary for a
        // nested namespace, but it must not stall the outermost parse loop.
        if (index_ == before && (co_await current_async()).kind != TokenKind::End) ++index_;
    }
    drain_pending_tags(program);
    program.static_assertions = std::move(static_assertions_);
    program.required_types = std::move(required_types_);
    if (syntax_ && diagnostics_.errors() == 0)
        (void)co_await syntax_->execution()->validate_functions_async();
    co_return program;
}

void Parser::drain_pending_tags(Program& program) {
    for (auto& enumeration : pending_enumerations_)
        program.enumerations.push_back(std::move(enumeration));
    pending_enumerations_.clear();
    for (auto& record : pending_records_)
        program.records.push_back(std::move(record));
    pending_records_.clear();
}

EvaluationTask<void> Parser::parse_typedef_async(std::string name_space,
                           std::vector<Attribute> attributes, TypePtr base_type,
                           SourceLocation location, bool consume_semicolon) {
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    do {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        std::optional<std::string> name;
        auto name_location = location;
        auto name_index = index_;
        auto type = co_await parse_declarator_async(copy_type(base_type), name, DeclaratorContext::Named,
                                     nullptr, &name_location, nullptr, &name_index);
        if (!type || !name) {
            if (!name) (co_await error_here_async("expected typedef name"));
            synchronize_external();
            co_return;
        }
        const auto spelling = *name;
        if (binds_at_file_scope()) *name = join_namespace(name_space, *name);
        else if (name->find("::") != std::string::npos)
            diagnostics_.error(name_location, "a local typedef name must be unqualified");
        auto item_attributes = attributes;
        auto trailing = co_await parse_attributes_async();
        item_attributes.insert(item_attributes.end(),
            std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
        auto definition = register_typedef(name_location, std::move(*name), std::move(type), item_attributes);
        remember_alias_binding(name_index, spelling, std::move(definition), true);
    } while ((co_await consume_async(",")));
    list.finish();
    if (consume_semicolon) (co_await expect_async(";", "after typedef declaration"));
}

AliasDefinitionPtr Parser::register_typedef(SourceLocation location, std::string name, TypePtr type,
                              const std::vector<Attribute>& attributes) {
    apply_callable_attributes(type, attributes);

    for (const auto& attribute : attributes) {
        if (attribute.name != "aligned" && attribute.name != "abi" &&
            attribute.name != "clobber" && attribute.name != "stack_cleanup" &&
            !is_vector_type_attribute(attribute.name))
            type_error(type, attribute.location,
                "attribute '" + attribute.name + "' is not valid on a typedef");
        if (attribute.name == "aligned")
            type_error(type, attribute.location,
                "aligned on a typedef is not implemented; align the object, record, or member instead");
        if (is_vector_type_attribute(attribute.name))
            apply_type_attribute(type, attribute);
    }

    auto definition = make_alias_definition(type);
    if (!definition) return {};
    TypePtr compatible_with;
    const auto compatible = [&](const AliasDefinitionPtr& previous) {
        auto previous_type = previous->instantiate();
        if (has_pending_type_bound(type) || has_pending_type_bound(previous_type)) {
            compatible_with = previous_type;
        } else if (!same_type(previous_type, type)) {
            return false;
        }
        definition = previous;
        if (parsing_public_fragment_) {
            // Compatible redeclarations share a binding, but its later uses
            // must not forget newly invalid attributes on that binding.
            const auto execution = syntax_ ? syntax_->execution() : nullptr;
            CapturedTypeErrorSet existing;
            if (!visit_captured_type_errors(previous_type, execution.get(), location,
                    [&](const CapturedTypeError& error) { existing.insert(error); })) return false;
            CapturedTypeErrors incoming;
            if (!visit_captured_type_errors(type, execution.get(), location,
                    [&](const CapturedTypeError& error) {
                        if (existing.insert(error)) incoming.push_back(error);
                    })) return false;
            if (!incoming.empty()) {
                previous_type->captured_errors.insert(previous_type->captured_errors.end(),
                    incoming.begin(), incoming.end());
                definition = make_alias_definition(previous_type);
                if (!definition) return false;
            }
        }
        return true;
    };
    auto& aliases = type_aliases_;
    if (!binds_at_file_scope()) {
        const NameKey key(name, location);
        if (local_scopes_.back().contains(key)) {
            diagnostics_.error(location, "typedef '" + name + "' conflicts with a local value");
            return {};
        }
        auto& local = local_type_scopes_.back();
        const auto found = local.find(key);
        if (found != local.end() && !compatible(found->second)) {
            diagnostics_.error(location, "typedef '" + name + "' redeclared with a different type");
            return {};
        }
        local[key] = definition;
    } else {
        const auto found = aliases.find(name);
        if (found != aliases.end() && !compatible(found->second)) {
            diagnostics_.error(location,
                               "typedef '" + name + "' redeclared with a different type");
            return {};
        }
        aliases[name] = definition;
    }
    TypeRequirement requirement{type, compatible_with, location, active_namespace_, name};
    if (active_function_ && !binds_at_file_scope()) active_function_->required_types.push_back(std::move(requirement));
    else required_types_.push_back(std::move(requirement));
    if (replacement_)
        declared_aliases_.push_back({std::move(name), definition,
            binds_at_file_scope() ? 0 : local_type_scopes_.size()});
    return definition;
}

EvaluationTask<void> Parser::parse_external_node_splice_async(
    Program& program, std::string name_space) {
    const auto item = (co_await current_async());
    ++index_;
    const bool function_definition = item.splice &&
        syntax_function_definition_node(*item.splice);
    if (!item.splice || (!syntax_declaration_node(*item.splice) &&
                         !function_definition)) {
        diagnostics_.error(item.location,
            "structured syntax splice requires a declaration or function-definition node at external position");
        co_return;
    }
    if (!syntax_ || !item.splice->context ||
        !item.splice->context->parse_environment) {
        diagnostics_.error(item.location,
            "structured syntax splice has no retained parse environment");
        co_return;
    }
    const auto category = item.splice->kind == SyntaxNode::Kind::Deferred
        ? item.splice->deferred_category
        : function_definition ? SyntaxParseCategory::FunctionDefinition
                              : SyntaxParseCategory::Declaration;
    auto output = syntax_->execution()->materialize_node(
        *item.splice, item.location, category);
    if (!output) co_return;
    auto child = replacement_parser(std::move(*output));
    child->scope_import_events_ = std::make_shared<std::vector<ScopeImportEvent>>(*scope_import_events_);
    const auto prior_import_events = child->scope_import_events_->size();
    if (item.splice->kind == SyntaxNode::Kind::Deferred)
        child->restore_deferred_environment(
            *item.splice->context->parse_environment,
            *item.splice->context, deferred_position(*item.splice));
    else
        child->restore_environment(*item.splice->context->parse_environment,
                                   *item.splice->context);
    const auto prior_records = child->tag_state();
    const auto prior_imports = child->current_scope_imports_;
    child->prepare_tag_destination(*this);
    const auto previous_errors = diagnostics_.errors();
    Program parsed;
    const bool imported_declaration = (co_await child->current_async()).is("using");
    co_await child->parse_external_async(parsed, name_space);
    if ((co_await child->current_async()).kind != TokenKind::End)
        (co_await child->error_here_async(function_definition
            ? "structured function-definition splice must contain one complete function definition"
            : "structured declaration splice must contain one complete declaration"));
    if (function_definition) {
        if (parsed.functions.size() != 1 || !parsed.functions.front()->body ||
            !parsed.objects.empty() || !parsed.records.empty() ||
            !parsed.enumerations.empty())
            diagnostics_.error(item.location,
                "structured function-definition splice must contain one complete function definition");
    } else for (const auto& function : parsed.functions)
        if (function->body)
            diagnostics_.error(item.location,
                "structured declaration splice cannot contain a function definition");
    for (const auto& alias : child->declared_aliases_) {
        if (alias.scope_depth != 0) continue;
        if (const auto found = type_aliases_.find(alias.name);
            found != type_aliases_.end() &&
            !same_type(found->second->instantiate(), alias.definition->instantiate()))
            diagnostics_.error(item.location,
                "spliced typedef '" + alias.name + "' has a different destination type");
    }
    for (const auto& enumeration : parsed.enumerations) {
        if (record_types_.contains(enumeration.name))
            diagnostics_.error(item.location,
                "spliced enumeration '" + enumeration.name + "' conflicts with a destination record");
        if (const auto found = enum_types_.find(enumeration.name);
            found != enum_types_.end() && found->second.underlying != enumeration.underlying)
            diagnostics_.error(item.location,
                "spliced enumeration '" + enumeration.name +
                "' conflicts with the destination underlying type");
    }
    if (diagnostics_.errors() != previous_errors ||
        !transfer_spliced_tags(*child, prior_records, item.location)) co_return;
    for (const auto& [key, binding] : child->tag_rebindings_)
        if (std::any_of(parsed.records.begin(), parsed.records.end(),
                [&](const auto& record) { return record.nominal_key() == binding->type; }) ||
            std::any_of(parsed.enumerations.begin(), parsed.enumerations.end(),
                [&](const auto& enumeration) { return enumeration.nominal_key() == binding->type; }))
            tag_rebindings_[key] = binding;
    for (const auto& [key, binding] : child->value_rebindings_)
        if (std::any_of(parsed.objects.begin(), parsed.objects.end(),
                [&](const auto& object) { return object->binding == binding; }) ||
            std::any_of(parsed.functions.begin(), parsed.functions.end(),
                [&](const auto& function) { return function->binding == binding; }) ||
            std::any_of(parsed.enumerations.begin(), parsed.enumerations.end(),
                [&](const auto& enumeration) {
                    return std::any_of(enumeration.enumerators.begin(), enumeration.enumerators.end(),
                        [&](const auto& enumerator) { return enumerator.binding == binding; });
                }))
            value_rebindings_[key] = binding;
    for (const auto& alias : child->declared_aliases_)
        if (alias.scope_depth == 0) {
            type_aliases_.try_emplace(alias.name, alias.definition);
            transfer_alias_rebindings(*child, alias.definition);
        }
    for (const auto& function : parsed.functions) remember_function(*function);
    for (const auto& object : parsed.objects)
        remember_ordinary_name(object->name, OrdinaryNameKind::Object, object->location, object->binding);
    for (const auto& enumeration : parsed.enumerations) {
        enum_types_[enumeration.name] = {enumeration.underlying, enumeration.captured_type_errors};
        for (const auto& enumerator : enumeration.enumerators)
            remember_ordinary_name(enumerator.name, OrdinaryNameKind::Object,
                enumerator.location, enumerator.binding);
    }
    const auto append = [](auto& destination, auto& source) {
        destination.insert(destination.end(),
            std::make_move_iterator(source.begin()),
            std::make_move_iterator(source.end()));
    };
    append(program.functions, parsed.functions);
    append(program.objects, parsed.objects);
    append(program.records, parsed.records);
    append(program.enumerations, parsed.enumerations);
    append(program.global_labels, parsed.global_labels);
    append(program.static_assertions, parsed.static_assertions);
    append(required_types_, child->required_types_);
    // Nested source scopes survive with their original provenance. A direct
    // import instead belongs to the destination item scope, recorded below.
    if (!imported_declaration)
        scope_import_events_->insert(scope_import_events_->end(),
            child->scope_import_events_->begin() + static_cast<std::ptrdiff_t>(prior_import_events),
            child->scope_import_events_->end());
    for (auto at = prior_imports; at < child->current_scope_imports_; ++at)
        apply_import(child->active_imports_[at], token_origin(item.location).identity, true);
    for (auto& assertion : child->static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

std::optional<std::size_t> Parser::function_header_splice_position() {
    auto at = index_;
    while (at < tokens_.size() && tokens_[at].is("[[")) {
        const auto end = bounded_group_end(at);
        if (!end) return {};
        at = *end;
    }
    if (at < tokens_.size() && tokens_[at].kind == TokenKind::StructuredSplice &&
        tokens_[at].splice && syntax_function_header_node(*tokens_[at].splice)) return at;
    return {};
}

EvaluationTask<void> Parser::parse_function_header_splice_async(
    Program& program, std::string name_space, std::size_t production_event,
    std::size_t header_index) {
    const auto first = index_;
    const auto item = tokens_[header_index];
    const auto previous_errors = diagnostics_.errors();
    // Record the published attribute trees now, but interpret all attributes
    // together with the retained header in the bounded parser below. This
    // keeps generic/ABI/variadic handling on the ordinary declaration path.
    const auto attributes = [&]() -> EvaluationTask<bool> {
        if (parsing_public_fragment_) {
            (void)co_await parse_attributes_async(false, AttributeParseMode::SyntaxOnly);
            co_return true;
        }
        // Ordinary parsing merely transfers the written groups here. Running
        // their macros would both reorder header work and invalidate the
        // saved header index before the bounded parser receives the tokens.
        while (std::as_const(*this).current().is("[[")) {
            const auto end = bounded_group_end(index_);
            if (!end) {
                (co_await error_here_async("structured header requires balanced attributes"));
                co_return false;
            }
            index_ = *end;
        }
        co_return true;
    };
    if (!(co_await attributes())) co_return;
    {
        ProductionScope header(*this, SyntaxProduction::FunctionHeader);
        ++index_;
        if (header.event < production_events_.size())
            production_events_[header.event].opaque = item.splice;
    }
    const auto trailing_first = index_;
    if (!(co_await attributes())) co_return;
    const auto header_end = index_;
    const bool header_only = parsing_public_function_header_;
    const auto compound_start = [](const Token& token) {
        return token.is("{") || (token.kind == TokenKind::StructuredSplice &&
            token.splice && syntax_compound_node(*token.splice));
    };
    bool body = compound_start(std::as_const(*this).current());
    if (parsing_public_fragment_ && !header_only && !body && !(co_await current_async()).is(";")) {
        (co_await error_here_async("structured function header requires a body or ';'"));
        co_return;
    }
    const auto discard_tail = [&] {
        // A failed header still owns this already delimited tail. Do not
        // misdiagnose its body as a sequence of unrelated external items.
        if (header_only) return;
        if (body && std::as_const(*this).current().is("{")) {
            if (const auto end = bounded_group_end(index_)) index_ = *end;
        } else ++index_;
    };
    if (!syntax_ || !item.splice->context || !item.splice->context->parse_environment) {
        diagnostics_.error(item.location,
            "structured function header has no retained parse environment");
        discard_tail();
        co_return;
    }
    const auto slot = header_only ? SyntaxProduction::FunctionHeader
        : body ? SyntaxProduction::FunctionDefinition : SyntaxProduction::Declaration;
    if (production_event < production_events_.size())
        production_events_[production_event].production = slot;
    if (parsing_public_fragment_ &&
        (item.splice->kind == SyntaxNode::Kind::Deferred || public_deferred_header_)) {
        // The header has a proven category, but unknown parameters/types or
        // an opaque attribute attached around an otherwise settled header.
        // Keep the whole composed unit deferred, including the header marker;
        // no speculative signature is allowed to classify its body.
        std::optional<std::size_t> end;
        if (header_only) end = header_end;
        else if (body && (co_await current_async()).is("{")) end = bounded_group_end(index_);
        else end = index_ + 1;
        if (!end) {
            (co_await error_here_async("structured function header requires a balanced body"));
            co_return;
        }
        const auto category = header_only ? SyntaxParseCategory::FunctionHeader
            : body ? SyntaxParseCategory::FunctionDefinition : SyntaxParseCategory::FunctionDeclaration;
        if (production_event < production_events_.size())
            production_events_[production_event].opaque = deferred_node(
                first, *end, slot, category,
                production_events_[production_event].context);
        index_ = *end;
        auto function = std::make_unique<FunctionDecl>();
        if (body && !header_only) {
            function->body = std::make_unique<Statement>();
            function->body->kind = Statement::Kind::Compound;
        }
        program.functions.push_back(std::move(function));
        co_return;
    }
    const auto execution = syntax_->execution();
    auto output = execution->materialize_node(
        *item.splice, item.location, SyntaxParseCategory::FunctionHeader);
    if (!output) { discard_tail(); co_return; }
    const auto token_count = output->tokens.size() + (header_index - first) +
        (header_end - trailing_first);
    if (!execution->begin_fragment(item.location, token_count)) { discard_tail(); co_return; }
    struct FragmentEnd {
        SyntaxExecution& execution;
        ~FragmentEnd() { execution.end_fragment(); }
    } fragment_end{*execution};
    output->tokens.insert(output->tokens.begin(),
        tokens_.begin() + static_cast<std::ptrdiff_t>(first),
        tokens_.begin() + static_cast<std::ptrdiff_t>(header_index));
    output->tokens.insert(output->tokens.end() - 1,
        tokens_.begin() + static_cast<std::ptrdiff_t>(trailing_first),
        tokens_.begin() + static_cast<std::ptrdiff_t>(header_end));
    auto child = replacement_parser(std::move(*output));
    if (item.splice->kind == SyntaxNode::Kind::Deferred)
        child->restore_deferred_environment(*item.splice->context->parse_environment,
                                           *item.splice->context, deferred_position(*item.splice));
    else child->restore_environment(*item.splice->context->parse_environment,
                                     *item.splice->context);
    const auto prior_records = child->tag_state();
    child->prepare_tag_destination(*this);
    child->parsing_public_fragment_ = parsing_public_fragment_;
    child->parsing_public_function_header_ = true;
    Program parsed;
    co_await child->parse_external_async(parsed, name_space);
    if ((co_await child->current_async()).kind != TokenKind::End || parsed.functions.size() != 1 ||
        parsed.functions.front()->body || !parsed.objects.empty() ||
        !parsed.records.empty() || !parsed.enumerations.empty() ||
        !parsed.global_labels.empty()) {
        diagnostics_.error(item.location,
            "structured function header must contain one complete direct-function header");
        discard_tail();
        co_return;
    }
    if (diagnostics_.errors() != previous_errors ||
        !transfer_spliced_tags(*child, prior_records, item.location)) {
        discard_tail();
        co_return;
    }
    auto function = std::move(parsed.functions.front());
    HeaderNominalFrame header_names(*this, child->completed_header_nominals_.has_value(),
        item.location, child->completed_header_nominals_ ? &*child->completed_header_nominals_ : nullptr);
    if (child->completed_header_nominals_) {
        for (const auto& [old, binding] : child->value_rebindings_)
            for (const auto& [name, value] : child->completed_header_nominals_->values)
                if (binding == value) value_rebindings_[old] = binding;
        for (const auto& [old, binding] : child->tag_rebindings_)
            for (const auto& [name, tag] : child->completed_header_nominals_->tags)
                if (binding->type == tag.type->nominal_key()) tag_rebindings_[old] = binding;
        if (header_only) completed_header_nominals_ = child->completed_header_nominals_;
    }
    if (parsing_public_fragment_) {
        // Recognition keeps this header as the original indivisible child.
        // Its body must therefore refer to that child's binders, not to the
        // temporary placement used only to inspect its signature here. A real
        // surviving splice will allocate and remap the destination binders.
        const auto retain_header_binding = [&](auto& parameter) {
            // Attributes outside the marker are new written children, not
            // part of the retained header. Publish their new declaring token
            // metadata in the owning public tree before classifying its body.
            for (auto at = first; at < header_end; ++at) {
                if (at >= header_index && at < trailing_first) continue;
                if (!execution->work(tokens_[at].location)) { public_tree_failed_ = true; return; }
                auto& token = tokens_[at];
                if (token.location.file == parameter.location.file &&
                    token.location.offset == parameter.location.offset) {
                    token.value_binding = parameter.binding;
                    token.value_spelling = std::make_shared<const std::string>(parameter.name);
                    return;
                }
            }
            if (parameter.binding.placement && parameter.binding.placement->source_binding)
                parameter.binding = *parameter.binding.placement->source_binding;
        };
        for (auto& parameter : function->parameters) retain_header_binding(parameter);
        for (auto& parameter : function->generic_parameters)
            if (parameter.value_type) retain_header_binding(parameter);
        for (auto& attribute : function->attributes)
            for (auto& binding : attribute.variadic_bindings) retain_header_binding(binding);
    }
    for (const auto& [key, binding] : child->value_rebindings_)
        if (binding == function->binding ||
            std::any_of(function->parameters.begin(), function->parameters.end(),
                [&](const auto& parameter) { return binding == parameter.binding; }) ||
            std::any_of(function->generic_parameters.begin(), function->generic_parameters.end(),
                [&](const auto& parameter) { return parameter.value_type && binding == parameter.binding; }) ||
            std::any_of(function->attributes.begin(), function->attributes.end(),
                [&](const auto& attribute) {
                    return std::any_of(attribute.variadic_bindings.begin(), attribute.variadic_bindings.end(),
                        [&](const auto& state) { return binding == state.binding; });
                }))
            value_rebindings_[key] = binding;
    remember_function(*function);
    if (!header_only) {
        struct Restore {
            Parser& parser;
            FunctionDecl* function;
            std::string name_space;
            std::vector<std::string> generic_types;
            std::shared_ptr<const GenericTagOwner> tag_owner;
            std::shared_ptr<const FunctionScopeIdentity> function_scope;
            ~Restore() {
                parser.active_function_ = function;
                parser.active_namespace_ = std::move(name_space);
                parser.active_generic_types_ = std::move(generic_types);
                parser.generic_tag_owner_ = std::move(tag_owner);
                parser.function_scope_ = std::move(function_scope);
            }
        } restore{*this, active_function_, active_namespace_, active_generic_types_,
                  generic_tag_owner_, function_scope_};
        active_function_ = function.get();
        generic_tag_owner_ = function->generic_tag_owner;
        function_scope_ = function->function_scope;
        active_namespace_ = name_space;
        active_generic_types_.clear();
        for (const auto& parameter : function->generic_parameters)
            if (!parameter.value_type) active_generic_types_.push_back(parameter.name);
        if (!parsing_public_fragment_) {
            // A textual token macro may supply the following body or semicolon,
            // but only after surviving header expansions have completed and
            // its generic context is available to the body invocation.
            body = compound_start((co_await current_async()));
            if (!body && !(co_await current_async()).is(";")) {
                (co_await error_here_async("structured function header requires a body or ';'"));
                co_return;
            }
        }
        if (body) {
            function->body = co_await parse_compound_async();
            bind_label_references(*function->body, true);
        }
        else (co_await expect_async(";", "after structured function header"));
    }
    program.functions.push_back(std::move(function));
    for (auto& assertion : child->static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

EvaluationTask<void> Parser::parse_external_async(Program& program, std::string name_space) {
    const auto errors = diagnostics_.errors();
    const auto first_function = program.functions.size();
    co_await parse_external_impl_async(program, name_space);
    if (!syntax_ || parsing_public_fragment_ || parsing_public_function_header_ ||
        preparing_header_ || diagnostics_.errors() != errors) co_return;
    for (std::size_t i = first_function; i < program.functions.size(); ++i) {
        auto& function = *program.functions[i];
        if (!function.translation_context) {
            auto context = std::make_shared<SyntaxContext>();
            context->kind = SyntaxContext::Kind::DefinitionSite;
            context->definition = function.location;
            context->name_space = function.source_namespace;
            context->imports = function.imports;
            context->import_declarations = active_import_declarations_;
            context->syntax_bindings = syntax_->bindings();
            context->parse_environment = snapshot_environment();
            function.translation_context = std::move(context);
        }
    }
    syntax_->execution()->publish_declarations(program, pending_records_, pending_enumerations_);
}


void Parser::parse_external(Program& program, const std::string& name_space) {
    parse_external_async(program, name_space).run();
}
EvaluationTask<void> Parser::parse_external_impl_async(Program& program, std::string name_space) {
    LocalRebindingsRestore bindings_restore{value_rebindings_, value_rebindings_,
        parsing_public_function_header_};
    struct HeaderBoundsRestore {
        std::unordered_set<const Expr*>& value;
        std::unordered_set<const Expr*> previous;
        ~HeaderBoundsRestore() { value = std::move(previous); }
    } header_bounds_restore{header_type_bounds_, std::move(header_type_bounds_)};
    header_type_bounds_.clear();
    struct NamespaceRestore {
        std::string& value;
        std::string previous;
        ~NamespaceRestore() { value = std::move(previous); }
    } restore{active_namespace_, active_namespace_};
    active_namespace_ = name_space;
    (void)(co_await current_async());
    drain_pending_tags(program);
    ProductionScope production(*this, parsing_public_fragment_ &&
        !(co_await current_async()).is("using") && !(co_await current_async()).is("$::static_assert")
        ? SyntaxProduction::Declaration : SyntaxProduction::None);
    if (const auto header_position = function_header_splice_position()) {
        co_await parse_function_header_splice_async(program, name_space, production.event, *header_position);
        co_return;
    }
    const auto expansion_head = macro_start() || active_syntax(true)
        ? std::optional<ExpansionFunctionHead>{} : expansion_function_head(tokens_, index_);
    if (parsing_public_fragment_ && ((co_await current_async()).is("syntax") ||
        (co_await current_async()).is("namespace") || macro_start() || active_syntax(true) ||
        expansion_head)) {
        (co_await error_here_async("parsed declaration requires a direct core declaration"));
        ++index_;
        co_return;
    }
    // A leading type splice is a declaration specifier, not a complete item.
    // Let the ordinary type path retain its wrapper and compose the declarator.
    if ((co_await current_async()).kind == TokenKind::StructuredSplice &&
        (!(co_await current_async()).splice || !syntax_type_node(*(co_await current_async()).splice))) {
        if (parsing_public_fragment_) {
            const auto item = (co_await current_async());
            ++index_;
            if (!item.splice || (!syntax_declaration_node(*item.splice) &&
                                 !syntax_function_definition_node(*item.splice)))
                diagnostics_.error(item.location,
                    "structured syntax splice requires a declaration or function-definition node at external position");
            else if (recording_public_tree_ && production.event < production_events_.size())
                production_events_[production.event].opaque = item.splice;
        } else co_await parse_external_node_splice_async(program, name_space);
        co_return;
    }
    if (syntax_ && expansion_head && !parsing_expansion_declaration_) {
        const bool generated = replacement_ || token_origin((co_await current_async()).location).context;
        if (generated) (co_await error_here_async("expansion output cannot introduce syntax registration"));
        else {
            const auto context = syntax_context((co_await current_async()).location);
            if (context) {
                if (const auto* function = co_await syntax_->execution()->define_function_async(tokens_, index_, active_namespace_,
                        active_imports_, syntax_->bindings(), context->parse_environment))
                    remember_function(*function);
            }
            else { ++index_; synchronize_external(); }
        }
        if (generated) { ++index_; synchronize_external(); }
        co_return;
    }
    if ((co_await parse_syntax_registration_async(&program))) co_return;
    const auto* external_owner = active_syntax(true);
    if (external_owner) {
        const auto location = (co_await current_async()).location;
        auto execution = syntax_->execution();
        if (!execution->begin_replacement(location)) {
            if (external_owner) diagnostics_.note(external_owner->location,
                "syntax '" + external_owner->name + "' defined here");
            ++index_;
            synchronize_external();
            co_return;
        }
        struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
        auto output = co_await expand_at_position_async(true);
        if (!output) co_return;
        auto child = replacement_parser(std::move(*output));
        while ((co_await child->current_async()).kind != TokenKind::End) {
            const auto before = child->index_;
            co_await child->parse_external_async(program, name_space);
            if (before == child->index_) ++child->index_;
        }
        adopt_replacement(*child);
        co_return;
    }
    struct GenericTypesRestore {
        std::vector<std::string>& value;
        std::vector<std::string> previous;
        ~GenericTypesRestore() { value = std::move(previous); }
    } generic_restore{active_generic_types_, active_generic_types_};
    struct HeaderRestore {
        std::shared_ptr<const SyntaxHeaderBindings>& value;
        std::shared_ptr<const SyntaxHeaderBindings> previous;
        ~HeaderRestore() { value = std::move(previous); }
    } header_restore{header_bindings_, header_bindings_};
    co_await prepare_header_async();
    if (allow_public_header_deferral_)
        (void)(co_await preview_generic_types_async(&public_header_uncertain_names_));
    bool generic_header = false;
    struct SharedDeclaratorRestore {
        TokenIdentity& value;
        TokenIdentity previous;
        std::shared_ptr<SharedSpecifierOwners>& owners;
        std::shared_ptr<SharedSpecifierOwners> previous_owners;
        ~SharedDeclaratorRestore() { value = previous; owners = std::move(previous_owners); }
    } shared_declarator_restore{shared_specifier_declarator_, shared_specifier_declarator_,
        shared_specifier_owners_, std::move(shared_specifier_owners_)};
    shared_specifier_declarator_ = {};
    shared_specifier_owners_ = std::make_shared<SharedSpecifierOwners>();
    active_generic_types_ = (co_await preview_generic_types_async(nullptr, &generic_header, false, &shared_specifier_declarator_));
    (co_await remember_shared_declarator_async());
    struct HeaderNominalOwnerRestore {
        Parser& parser;
        std::shared_ptr<const GenericTagOwner> owner;
        std::shared_ptr<const FunctionScopeIdentity> scope;
        ~HeaderNominalOwnerRestore() {
            parser.generic_tag_owner_ = std::move(owner);
            parser.function_scope_ = std::move(scope);
        }
    } nominal_owner_restore{*this, generic_tag_owner_, function_scope_};
    // Ownership starts before result types and attributes, not only at the
    // body. Value-only generic lists also create per-instance nominal types.
    generic_tag_owner_ = generic_header ? std::make_shared<const GenericTagOwner>() : nullptr;
    if (generic_header) function_scope_ = std::make_shared<const FunctionScopeIdentity>();
    auto header_names = std::make_unique<HeaderNominalFrame>(
        *this, generic_header, (co_await current_async()).location);
    const auto header_first = index_;
    struct SharedSpecifiersRestore {
        bool& value;
        bool previous;
        std::vector<std::pair<std::size_t, Token>>& inputs;
        std::vector<std::pair<std::size_t, Token>> previous_inputs;
        std::shared_ptr<const SyntaxHeaderBindings>& header;
        std::shared_ptr<const SyntaxHeaderBindings> previous_header;
        ~SharedSpecifiersRestore() {
            value = previous; inputs = std::move(previous_inputs); header = std::move(previous_header);
        }
    } shared_specifiers_restore{retaining_shared_specifiers_, retaining_shared_specifiers_,
        shared_specifier_inputs_, std::move(shared_specifier_inputs_),
        shared_specifier_header_, shared_specifier_header_};
    shared_specifier_header_ = header_bindings_;
    shared_specifier_inputs_.clear();
    retaining_shared_specifiers_ = true;
    auto attributes = co_await parse_attributes_async();
    retaining_shared_specifiers_ = shared_specifiers_restore.previous;
    if ((co_await current_async()).is("$::static_assert")) {
        if (!attributes.empty()) (co_await error_here_async("attributes are not valid on $::static_assert"));
        (void)co_await parse_static_assertion_async();
        co_return;
    }
    if ((co_await consume_async("namespace"))) {
        const auto name_first = index_;
        auto nested = (co_await parse_qualified_name_async());
        TokenRegion import_region{token_origin((co_await current_async()).location).identity, {}};
        if (const auto end = bounded_group_end(index_); end && *end > index_)
            import_region.end = token_origin(tokens_[*end - 1].location).identity;
        if (!nested || !(co_await expect_async("{"))) { synchronize_external(); co_return; }
        import_regions_.push_back(import_region);
        const auto full = join_namespace(name_space, *nested);
        const auto fragment_depth = fragment_namespaces_.size();
        // A qualified header introduces every component, just as nested
        // namespace blocks do. Each binder uses its own identifier provenance.
        for (auto at = name_first; at < index_ && tokens_[at].kind == TokenKind::Identifier; at += 2) {
            const auto component = identifier_binding_name(tokens_[at]);
            const auto destination = join_namespace(active_namespace_, component);
            remember_fragment_name(component, destination, tokens_[at].location, FragmentNameDomain::Namespace);
            open_fragment_namespace(component, destination, tokens_[at].location);
            active_namespace_ = destination;
        }
        const auto saved_imports = active_imports_;
        const auto saved_import_declarations = active_import_declarations_;
        const auto saved_scope_imports = current_scope_imports_;
        const auto saved_value_rebindings = value_rebindings_;
        current_scope_imports_ = 0;
        if (syntax_) syntax_->push_scope();
        active_namespace_ = full;
        while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
            const auto before = index_;
            co_await parse_external_async(program, full);
            if (before == index_) ++index_;
        }
        (co_await expect_async("}"));
        import_regions_.pop_back();
        while (fragment_namespaces_.size() > fragment_depth) {
            const auto& scope = fragment_namespaces_.back();
            scope.identity->completed_ = scope.draft;
            fragment_namespaces_.pop_back();
        }
        if (syntax_) syntax_->pop_scope();
        active_imports_ = saved_imports;
        active_import_declarations_ = saved_import_declarations;
        current_scope_imports_ = saved_scope_imports;
        value_rebindings_ = saved_value_rebindings;
        co_return;
    }
    if ((co_await current_async()).is("using")) {
        if (!attributes.empty()) (co_await error_here_async("attributes are not valid on a using declaration"));
        (void)(co_await parse_using_declaration_async(true));
        co_return;
    }
    if ((co_await current_async()).is("global") && (co_await current_async(1)).is("label")) {
        if (const auto owner = (co_await global_label_owner_async(index_ + 2))) {
            if (production.event < production_events_.size())
                production_events_[production.event].production = SyntaxProduction::GlobalLabelDeclaration;
            (co_await consume_async("global"));
            co_await parse_global_label_declaration_async(program, *owner, std::move(attributes));
            co_return;
        }
    }
    const auto standalone_tag_async = [&](std::string_view tag) -> EvaluationTask<bool> {
        if (!(co_await current_async()).is(tag) || (co_await current_async(1)).kind != TokenKind::Identifier)
            co_return false;
        std::size_t at = 2;
        while ((co_await current_async(at)).is("::") && (co_await current_async(at + 1)).kind == TokenKind::Identifier)
            at += 2;
        while ((co_await current_async(at)).is("[[")) {
            unsigned depth = 1;
            ++at;
            while (depth && (co_await current_async(at)).kind != TokenKind::End) {
                if ((co_await current_async(at)).is("[[")) ++depth;
                else if ((co_await current_async(at)).is("]]")) --depth;
                ++at;
            }
            if (depth) co_return false;
        }
        if ((co_await current_async(at)).is("{")) {
            unsigned depth = 1;
            ++at;
            while (depth && (co_await current_async(at)).kind != TokenKind::End) {
                if ((co_await current_async(at)).is("{")) ++depth;
                else if ((co_await current_async(at)).is("}")) --depth;
                ++at;
            }
            if (depth) co_return false;
        }
        co_return (co_await current_async(at)).is(";");
    };
    if ((co_await standalone_tag_async("enum"))) {
        co_await parse_enum_declaration_async(program, name_space, std::move(attributes));
        co_return;
    }
    if ((co_await standalone_tag_async("struct")) || (co_await standalone_tag_async("union"))) {
        co_await parse_record_declaration_async(program, name_space, std::move(attributes));
        co_return;
    }
    Linkage linkage = Linkage::Group;
    bool linkage_seen = false;
    bool inline_hint = false;
    bool typedef_seen = false;
    const auto consume_storage_async = [&]() -> EvaluationTask<bool> {
        const auto location = (co_await current_async()).location;
        if ((co_await consume_async("typedef"))) {
            if (typedef_seen || linkage_seen || inline_hint)
                diagnostics_.error(location, "typedef cannot combine with another storage specifier");
            typedef_seen = true;
            co_return true;
        }
        if ((co_await consume_async("inline"))) {
            if (typedef_seen)
                diagnostics_.error(location, "typedef cannot combine with another storage specifier");
            inline_hint = true;
            co_return true;
        }
        Linkage selected;
        if ((co_await consume_async("global"))) selected = Linkage::Global;
        else if ((co_await consume_async("static"))) selected = Linkage::Static;
        else co_return false;
        if (typedef_seen)
            diagnostics_.error(location, "typedef cannot combine with another storage specifier");
        if (linkage_seen && linkage != selected)
            diagnostics_.error(location, "declaration cannot be both 'global' and 'static'");
        linkage = selected;
        linkage_seen = true;
        co_return true;
    };
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    while ((co_await current_async()).is("typedef") || (co_await current_async()).is("global") ||
           (co_await current_async()).is("static") || (co_await current_async()).is("inline")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        (void)(co_await consume_storage_async());
    }

    if (!typedef_seen && linkage == Linkage::Global && (co_await current_async()).is("label")) {
        if (const auto owner = (co_await global_label_owner_async(index_ + 1))) {
            if (inline_hint)
                diagnostics_.error((co_await current_async()).location,
                                   "'inline' is valid only on a function");
            co_await parse_global_label_declaration_async(program, *owner, std::move(attributes));
            co_return;
        }
    }

    if (!(co_await type_start_async()) && !(co_await current_async()).is("[[")) {
        if (const auto message = familiar_c_spelling((co_await current_async()).text)) {
            (co_await error_here_async(*message));
        } else {
            (co_await error_here_async(typedef_seen ? "expected aliased type after 'typedef'"
                                    : "expected declaration"));
        }
        synchronize_external();
        co_return;
    }
    const auto location = (co_await current_async()).location;
    SpecifierAttributes specifier_attributes{attributes};
    retaining_shared_specifiers_ = true;
    auto base_type = co_await parse_type_async(false, consume_storage_async, &specifier_attributes);
    if (preparing_header_) prepared_declarator_start_ = index_;
    retaining_shared_specifiers_ = shared_specifiers_restore.previous;
    specifiers.finish();
    if (typedef_seen) {
        resolve_specifier_attributes(specifier_attributes, true);
        co_await parse_typedef_async(name_space, std::move(attributes), std::move(base_type), location);
        co_return;
    }
    if ((co_await current_async()).is(";") && base_type && !base_type->nominal_key().empty() &&
        !linkage_seen && !inline_hint) {
        resolve_specifier_attributes(specifier_attributes, false);
        (co_await consume_async(";"));
        co_return;
    }
    // Only shared declaration attributes apply to every sibling. Angle lists
    // and trailing attributes belong to the declarator that writes them.
    std::vector<GenericParameter> shared_generic_parameters;
    for (const auto& attribute : attributes)
        if (attribute.name == "generic")
            shared_generic_parameters.insert(shared_generic_parameters.end(),
                attribute.generic_parameters.begin(), attribute.generic_parameters.end());
    // Replay grammar, not source expansion: procedural output is already in
    // this sequence and bounded expression/type output is a PreparedFragment.
    // Incoming parsed/spliced bindings are retained; newly inferred bindings
    // from this first semantic view were deliberately not stamped onto tokens.
    auto shared_specifiers = specifier_input_tokens(header_first, index_);
    shared_specifiers.emplace_back(TokenKind::End, std::string_view{}, (co_await current_async()).location);
    TypePtr nongeneric_base = generic_header ? TypePtr{} : base_type;
    auto nongeneric_attributes = attributes;
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    unsigned ordinal = 0;
    bool last_function = false;
    for (;;) {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        PrototypeScopeFrame prototype_frame(*this);
        std::optional<std::string> name;
        SourceLocation name_location;
        std::vector<FunctionDecl::GenericParameter> angle_parameters;
        std::vector<Attribute> entity_suffix_attributes;
        const auto declarator_first = index_;
        auto type = co_await parse_declarator_async(copy_type(base_type), name, DeclaratorContext::Named, nullptr, &name_location,
                                     &angle_parameters, nullptr, true, &entity_suffix_attributes);
        if (!type || !name) {
            if (!name) (co_await error_here_async("expected declaration name"));
            synchronize_external();
            co_return;
        }
        const auto written_name = *name;
        *name = join_namespace(name_space, *name);
        last_function = type->kind == Type::Kind::Function && type->function;
        if (last_function) {
            auto signature = type->function;
            auto result_type = signature->result;
            auto function_attributes = attributes;
            function_attributes.insert(function_attributes.end(),
                std::make_move_iterator(entity_suffix_attributes.begin()),
                std::make_move_iterator(entity_suffix_attributes.end()));
            auto function = co_await parse_function_async(
                header_first, location, std::move(*name), name_space, std::move(result_type),
                linkage, inline_hint, std::move(function_attributes), std::move(signature),
                std::move(angle_parameters));
            if (function) function->fresh = token_origin(name_location).fresh;
            if (function) {
                GenericSignature parameters;
                for (const auto& parameter : function->generic_parameters)
                    parameters.push_back(parameter.value_type
                        ? GenericParameterKind::Value : GenericParameterKind::Type);
                function->binding = bind_value(written_name, name_location, declarator_first,
                    ValueBinding::Kind::Function, std::move(parameters));
                remember_function(*function);
            }
            const bool compound_start = (co_await current_async()).is("{") ||
                ((co_await current_async()).kind == TokenKind::StructuredSplice && (co_await current_async()).splice &&
                 syntax_compound_node(*(co_await current_async()).splice));
            if (function && (parsing_public_function_header_ || compound_start)) {
                prototype_frame.finish();
                if (parsing_public_function_header_ && generic_header)
                    completed_header_nominals_ = HeaderNominalScope{
                        local_scopes_.back(), local_tag_scopes_.back()};
                resolve_specifier_attributes(specifier_attributes, true);
                if (ordinal != 0)
                    (co_await error_here_async("a function definition requires a single declarator"));
                const auto item_event = item.event;
                const auto list_event = list.event;
                item.finish();
                list.finish();
                flatten_production(item_event, list_event);
                flatten_production(list_event);
                if (recording_public_tree_ && !public_tree_failed_)
                    production_events_[production.event].production =
                        parsing_public_function_header_ ? SyntaxProduction::FunctionHeader
                                                        : SyntaxProduction::FunctionDefinition;
                if (!parsing_public_function_header_) {
                    if (public_deferred_header_) {
                        const auto end = bounded_group_end(index_);
                        if (!end) {
                            (co_await error_here_async("deferred function requires a balanced body"));
                            co_return;
                        }
                        function->body = std::make_unique<Statement>();
                        function->body->kind = Statement::Kind::Compound;
                        function->body->location = (co_await current_async()).location;
                        index_ = *end;
                    } else {
                        auto* previous_function = active_function_;
                        auto previous_tag_owner = generic_tag_owner_;
                        auto previous_scope = function_scope_;
                        active_function_ = function.get();
                        generic_tag_owner_ = function->generic_tag_owner;
                        function_scope_ = function->function_scope;
                        function->body = co_await parse_compound_async();
                        bind_label_references(*function->body, true);
                        active_function_ = previous_function;
                        generic_tag_owner_ = std::move(previous_tag_owner);
                        function_scope_ = std::move(previous_scope);
                    }
                }
                program.functions.push_back(std::move(function));
                co_return;
            }
            if (function) program.functions.push_back(std::move(function));
        } else {
            prototype_frame.finish();
            resolve_specifier_attributes(specifier_attributes, true);
            const auto binding = bind_value(written_name, name_location, declarator_first, ValueBinding::Kind::Object);
            remember_ordinary_name(*name, OrdinaryNameKind::Object, name_location, binding);
            if (!angle_parameters.empty())
                diagnostics_.error(location,
                                   "angle generic parameters require a direct function declaration");
            auto item_attributes = attributes;
            item_attributes.insert(item_attributes.end(),
                std::make_move_iterator(entity_suffix_attributes.begin()),
                std::make_move_iterator(entity_suffix_attributes.end()));
            auto trailing = co_await parse_attributes_async();
            item_attributes.insert(item_attributes.end(),
                std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
            apply_callable_attributes(type, item_attributes);
            if (inline_hint)
                diagnostics_.error(location, "'inline' is valid only on a function");
            auto object = co_await parse_object_async(location, std::move(*name), std::move(type),
                                       linkage, std::move(item_attributes), false);
            if (object) object->fresh = token_origin(name_location).fresh;
            if (object) object->binding = binding;
            if (object) program.objects.push_back(std::move(object));
        }
        item.finish();
        prototype_frame.finish();
        ++ordinal;
        if (!(co_await consume_async(","))) break;
        header_names.reset();
        active_generic_types_.clear();
        header_bindings_.reset();
        generic_tag_owner_.reset();
        function_scope_ = nominal_owner_restore.scope;
        co_await prepare_header_async(&shared_specifiers);
        if (parsing_public_fragment_ && !allow_public_header_deferral_) {
            bool pending_fragments{};
            (void)(co_await preview_generic_types_async(&pending_fragments, nullptr, true));
            // A declaration capture can reach a pending header only after a
            // settled first sibling. Its ordinary declaration root still owns
            // the entire list; defer the bounded later header without running
            // its fragments or freezing an earlier shared semantic view.
            if (pending_fragments) allow_public_header_deferral_ = true;
        }
        if (allow_public_header_deferral_) {
            public_header_uncertain_names_ = false;
            (void)(co_await preview_generic_types_async(&public_header_uncertain_names_, nullptr, true));
        }
        generic_header = !shared_generic_parameters.empty();
        shared_specifier_declarator_ = {};
        active_generic_types_ = (co_await preview_generic_types_async(nullptr, &generic_header, true, &shared_specifier_declarator_));
        (co_await remember_shared_declarator_async());
        for (const auto& parameter : shared_generic_parameters) {
            if (!parameter.value_type &&
                std::find(active_generic_types_.begin(), active_generic_types_.end(), parameter.name) ==
                    active_generic_types_.end())
                active_generic_types_.push_back(parameter.name);
        }
        generic_tag_owner_ = generic_header ? std::make_shared<const GenericTagOwner>() : nullptr;
        function_scope_ = generic_header ? std::make_shared<const FunctionScopeIdentity>()
                                        : nominal_owner_restore.scope;
        header_names = std::make_unique<HeaderNominalFrame>(
            *this, generic_header, (co_await current_async()).location);
        if (!generic_header && nongeneric_base) {
            base_type = nongeneric_base;
            attributes = nongeneric_attributes;
        } else {
            const auto execution = syntax_ ? syntax_->execution() : nullptr;
            if (execution && !execution->begin_fragment(location, shared_specifiers.size())) co_return;
            struct EndView {
                std::shared_ptr<SyntaxExecution> execution;
                ~EndView() { if (execution) execution->end_fragment(); }
            } end_view{execution};
            auto view = replacement_parser({shared_specifiers, location});
            view->parsing_public_fragment_ = parsing_public_fragment_;
            view->allow_public_header_deferral_ = allow_public_header_deferral_;
            if (allow_public_header_deferral_)
                for (std::size_t at = 0; at < view->tokens_.size(); ++at)
                    view->public_input_indices_.push_back(at);
            view->retaining_shared_specifiers_ = true;
            auto view_attributes = co_await view->parse_attributes_async();
            const auto storage_async = [&]() -> EvaluationTask<bool> {
                co_return (co_await view->consume_async("global")) || (co_await view->consume_async("static")) || (co_await view->consume_async("inline"));
            };
            while ((co_await storage_async())) {}
            SpecifierAttributes view_specifiers{view_attributes};
            base_type = co_await view->parse_type_async(false, storage_async, &view_specifiers);
            if ((co_await view->current_async()).kind != TokenKind::End)
                (co_await view->error_here_async("shared declaration specifiers require a complete type"));
            if (view_specifiers.inline_record)
                *view_specifiers.inline_record += pending_records_.size();
            specifier_attributes.inline_record = view_specifiers.inline_record;
            specifier_attributes.candidates = std::move(view_specifiers.candidates);
            attributes = std::move(view_attributes);
            adopt_replacement(*view);
            if (!generic_header) {
                nongeneric_base = base_type;
                nongeneric_attributes = attributes;
            }
        }
    }
    // A prototype has no function-entry layout target. Wait until the entire
    // list is known: a later object declarator would make the prefix ambiguous.
    resolve_specifier_attributes(specifier_attributes, false);
    list.finish();
    if (last_function && !(co_await current_async()).is(";")) {
        (co_await error_here_async("expected ';' or function body"));
        synchronize_external();
    } else {
        (co_await expect_async(";", "after declaration"));
    }
}


void Parser::parse_external_impl(Program& program, const std::string& name_space) {
    parse_external_impl_async(program, name_space).run();
}
EvaluationTask<std::optional<std::string>> Parser::global_label_owner_async(std::size_t first) {
    struct Restore {
        std::size_t& cursor;
        std::size_t saved;
        ~Restore() { cursor = saved; }
    } restore{index_, index_};
    index_ = first;
    (co_await normalize_qualified_name_async());
    const auto location = (co_await current_async()).location;
    const auto name = std::as_const(*this).peek_qualified_name();
    const auto separator = name.rfind("::");
    if (separator == std::string::npos) co_return {};
    ++index_;
    while ((co_await current_async()).is("::") && (co_await current_async(1)).kind == TokenKind::Identifier) index_ += 2;
    while ((co_await current_async()).is("[[")) {
        const auto end = bounded_group_end(index_);
        if (!end) co_return {};
        index_ = *end;
    }
    // Suffixes and initializers describe ordinary qualified declarations, not
    // an exported code-label declaration, even if their prefix names a function.
    if (!(co_await current_async()).is(";")) co_return {};
    const auto owner = name.substr(0, separator);
    NameLookupContext lookup;
    const auto context = effective_context(location);
    lookup.name_space = context ? context->name_space : active_namespace_;
    lookup.imports = context ? context->imports : active_imports_;
    NameUse use(owner, location);
    lookup.fragment_lookup = fragment_lookup(owner, location);
    use.context = &lookup;
    // Token projection preserves a captured unit's lookup dependencies. A
    // newly emitted same-spelled function must not turn an old object node
    // into a code-label declaration. Fresh construction still sees functions
    // introduced earlier in its generated source stream.
    const auto origin = token_origin(location);
    const auto* captured = origin.value_context_captured && origin.context
        ? origin.context->parse_environment.get() : nullptr;
    const auto& functions = captured ? captured->functions : known_functions_;
    const auto& values = captured && captured->ordinary_values
        ? captured->ordinary_values->entries : known_ordinary_values_->entries;
    const auto& aliases = captured ? captured->aliases : type_aliases_;
    for (const auto& candidate : namespace_candidates(use)) {
        if (functions.contains(candidate)) co_return candidate;
        if (const auto found = values.find(candidate); found != values.end())
            co_return found->second.kind == OrdinaryNameKind::Function ? std::optional(candidate) : std::nullopt;
        if (aliases.contains(candidate)) co_return {};
    }
    co_return {};
}

EvaluationTask<void> Parser::parse_global_label_declaration_async(
    Program& program, std::string owner,
    std::vector<Attribute> attributes) {
    const auto location = (co_await current_async()).location;
    (co_await consume_async("label"));
    const auto first_name = index_;
    std::optional<std::string> name;
    {
        ProductionScope qualified_label(*this, SyntaxProduction::QualifiedLabelName);
        (co_await normalize_qualified_name_async());
        {
            ProductionScope function(*this, SyntaxProduction::QualifiedFunctionName);
            ProductionScope qualified(*this, SyntaxProduction::QualifiedName);
            if (const auto first = (co_await consume_kind_async(TokenKind::Identifier))) {
                name = identifier_binding_name(*first);
                while ((co_await current_async()).is("::") && (co_await current_async(1)).kind == TokenKind::Identifier && (co_await current_async(2)).is("::")) {
                    (co_await consume_async("::"));
                    const auto component = (co_await consume_kind_async(TokenKind::Identifier));
                    *name += "::" + identifier_binding_name(*component);
                }
            }
        }
        if (name && (co_await consume_async("::"))) {
            if (const auto label = (co_await consume_kind_async(TokenKind::Identifier)))
                *name += "::" + identifier_binding_name(*label);
            else name.reset();
        } else name.reset();
    }
    if (!name || name->find("::") == std::string::npos) {
        diagnostics_.error(location,
                           "a global label declaration requires a qualified label name");
        synchronize_external();
        co_return;
    }
    auto owner_fresh = index_ >= first_name + 3
        ? token_origin(tokens_[index_ - 3].location).fresh : nullptr;
    auto label_fresh = token_origin(tokens_[index_ - 1].location).fresh;
    auto trailing = co_await parse_attributes_async();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    (co_await expect_async(";", "after global label declaration"));
    program.global_labels.push_back(
        {location, owner + name->substr(name->rfind("::")), std::move(owner_fresh),
         std::move(label_fresh), std::move(attributes)});
}

EvaluationTask<void> Parser::parse_enum_declaration_async(Program& program,
                                    std::string name_space,
                                    std::vector<Attribute> attributes) {
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
    ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
    ProductionScope enumeration(*this, SyntaxProduction::EnumSpecifier);
    const auto location = (co_await current_async()).location;
    (co_await consume_async("enum"));
    const auto name_index = index_;
    auto name = (co_await parse_qualified_name_async());
    if (!name) {
        (co_await error_here_async("expected enumeration name"));
        synchronize_external();
        co_return;
    }
    const auto written_name = *name;
    *name = join_namespace(name_space, *name);
    if (record_types_.contains(*name))
        diagnostics_.error(location, "tag '" + *name +
            "' was previously declared as a record");
    auto trailing = co_await parse_attributes_async();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    EnumDecl declaration;
    const auto underlying = enum_underlying(attributes, declaration.captured_type_errors);
    declaration.location = location;
    declaration.name = *name;
    declaration.underlying = underlying;
    declaration.attributes = std::move(attributes);
    const auto found = enum_types_.find(*name);
    if (found != enum_types_.end() && found->second.underlying != underlying) {
        diagnostics_.error(location,
                           "enumeration '" + *name +
                               "' redeclared with a different underlying type");
    } else {
        enum_types_[*name] = {underlying, declaration.captured_type_errors};
    }
    remember_tag_binding(name_index, written_name, enum_type(declaration), true);
    co_await parse_enumerators_async(declaration, name_space);
    enumeration.finish();
    type_specifier.finish();
    specifier.finish();
    specifiers.finish();
    (co_await expect_async(";", "after enumeration declaration"));
    program.enumerations.push_back(std::move(declaration));
}

BuiltinType Parser::enum_underlying(const std::vector<Attribute>& attributes,
    std::shared_ptr<const CapturedTypeErrors>& errors) {
    BuiltinType underlying = BuiltinType::I32;
    bool underlying_seen = false;
    auto captured = std::make_shared<CapturedTypeErrors>();
    const auto reject = [&](SourceLocation location, std::string message) {
        if (parsing_public_fragment_) captured->push_back({location, std::move(message)});
        else diagnostics_.error(location, std::move(message));
    };
    for (const auto& attribute : attributes) {
        if (attribute.name != "underlying") continue;
        if (underlying_seen) {
            reject(attribute.location,
                               "enumeration has more than one 'underlying' attribute");
            continue;
        }
        underlying_seen = true;
        if (attribute.arguments.size() != 1) {
            reject(attribute.location,
                               "'underlying' requires one integer type");
            continue;
        }
        auto spelling = attribute.arguments.front();
        spelling.erase(std::remove_if(
            spelling.begin(), spelling.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
            spelling.end());
        const auto kind = builtin_kind(spelling);
        if (!kind || *kind == BuiltinType::Bool ||
            !is_integer(builtin_type(*kind))) {
            reject(attribute.location,
                               "'underlying' requires a non-bool integer type");
            continue;
        }
        underlying = *kind;
    }
    errors = captured->empty() ? nullptr : std::move(captured);
    return underlying;
}

void Parser::parse_enumerators(EnumDecl& declaration, const std::string& name_space) {
    parse_enumerators_async(declaration, name_space).run();
}

EvaluationTask<void> Parser::parse_enumerators_async(EnumDecl& declaration, std::string name_space) {
    if (!(co_await consume_async("{"))) co_return;
    // Even a named file-scope enum needs a private value-placement identity:
    // copied local enumerators may be placed in several namespaces without
    // changing the namespace-based identity of the enum's public type.
    const auto owner = declaration.nominal_identity
        ? declaration.nominal_identity : new_nominal_identity(declaration.location);
    while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
        ProductionScope entry(*this, SyntaxProduction::Enumerator);
        (co_await expand_inline_macro_fragments_async());
        const auto enumerator_index = index_;
        const auto token = (co_await consume_kind_async(TokenKind::Identifier));
        if (!token) {
            (co_await error_here_async("expected enumerator name"));
            while (!(co_await current_async()).is(",") && !(co_await current_async()).is("}") &&
                   (co_await current_async()).kind != TokenKind::End) {
                ++index_;
            }
        } else {
            EnumDecl::Enumerator enumerator;
            enumerator.location = token->location;
            enumerator.name = identifier_binding_name(*token);
            const NameKey key(enumerator.name, token->location);
            enumerator.binding = {ValueBinding::Kind::Enumerator,
                token_origin(token->location).identity, key.context, owner};
            auto incoming = token->value_binding.kind != ValueBinding::Kind::Unknown
                ? token->value_binding : token_origin(token->location).value_binding;
            if (incoming.kind == ValueBinding::Kind::Unknown)
                if (const auto& source = token_origin(token->location).declaration_source) incoming = *source;
            if (incoming.kind == ValueBinding::Kind::Enumerator && incoming != enumerator.binding &&
                incoming.declaration == token_origin(token->location).identity) {
                enumerator.binding.declaration_source = std::make_shared<const ValueDeclarationSource>(
                    ValueDeclarationSource{incoming, 112 + value_binding_storage(incoming, {})});
                for (;;) {
                    if (syntax_ && !syntax_->execution()->work(token->location)) {
                        public_tree_failed_ = true;
                        break;
                    }
                    value_rebindings_[incoming] = enumerator.binding;
                    if (!incoming.declaration_source) break;
                    incoming = incoming.declaration_source->binding;
                }
            }
            if (!preparing_header_) {
                remember_specifier_input(enumerator_index);
                tokens_[enumerator_index].value_binding = enumerator.binding;
                tokens_[enumerator_index].value_spelling =
                    std::make_shared<const std::string>(enumerator.name);
            }
            if (declaration.local) {
                if (local_scopes_.back().contains(key) || local_type_scopes_.back().contains(key))
                    diagnostics_.error(token->location,
                        "local enumerator '" + enumerator.name + "' conflicts with an existing declaration");
                else local_scopes_.back().emplace(key, enumerator.binding);
            } else {
                enumerator.name = join_namespace(name_space, enumerator.name);
                remember_ordinary_name(enumerator.name, OrdinaryNameKind::Object,
                    enumerator.location, enumerator.binding);
            }
            if ((co_await consume_async("="))) enumerator.initializer = co_await parse_constant_expression_async();
            declaration.enumerators.push_back(std::move(enumerator));
        }
        entry.finish();
        if (!(co_await consume_async(","))) break;
    }
    (co_await expect_async("}", "after enumeration definition"));
}

EvaluationTask<void> Parser::parse_record_declaration_async(
    Program& program, std::string name_space,
    std::vector<Attribute> attributes) {
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
    ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
    ProductionScope record(*this, SyntaxProduction::StructOrUnionSpecifier);
    const auto location = (co_await current_async()).location;
    const bool is_union = (co_await consume_async("union"));
    if (!is_union) (co_await consume_async("struct"));
    const auto name_index = index_;
    auto name = (co_await parse_qualified_name_async());
    if (!name) {
        (co_await error_here_async("expected record name"));
        synchronize_external();
        co_return;
    }
    const auto written_name = *name;
    *name = join_namespace(name_space, *name);
    auto trailing = co_await parse_attributes_async();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    auto [tag, inserted] = record_types_.emplace(
        *name, RecordTag{is_union, false});
    if (enum_types_.contains(*name))
        diagnostics_.error(location, "tag '" + *name +
            "' was previously declared as an enumeration");
    if (!inserted && tag->second.is_union != is_union) {
        diagnostics_.error(
            location, "record tag '" + *name +
                          "' was previously declared with the other record kind");
    }

    RecordDecl declaration;
    declaration.location = location;
    declaration.name = *name;
    declaration.is_union = is_union;
    declaration.attributes = std::move(attributes);
    remember_tag_binding(name_index, written_name, record_type(declaration), true);
    if ((co_await consume_async("{"))) {
        declaration.complete = true;
        if (!inserted && tag->second.complete) {
            diagnostics_.error(location,
                               "duplicate definition of record '" + *name + "'");
        }
        tag->second.complete = true;
        co_await parse_record_members_async(declaration);
    }
    record.finish();
    type_specifier.finish();
    specifier.finish();
    specifiers.finish();
    (co_await expect_async(";", "after record declaration"));
    program.records.push_back(std::move(declaration));
}

void Parser::parse_record_members(RecordDecl& declaration) {
    parse_record_members_async(declaration).run();
}

EvaluationTask<void> Parser::parse_record_members_async(RecordDecl& declaration) {
    while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
        ProductionScope member(*this, SyntaxProduction::MemberDeclaration);
        auto member_attributes = co_await parse_attributes_async();
        if (!(co_await type_start_async())) {
            (co_await error_here_async("expected record member declaration"));
            while (!(co_await current_async()).is(";") && !(co_await current_async()).is("}") &&
                   (co_await current_async()).kind != TokenKind::End) {
                ++index_;
            }
            (co_await consume_async(";"));
            continue;
        }
        const auto member_location = (co_await current_async()).location;
        SpecifierAttributes specifier_attributes{member_attributes, SpecifierAttributes::Context::Member};
        auto base_type = co_await parse_type_async(true, {}, &specifier_attributes);
        bool parsed_member = false;
        do {
            ProductionScope member_declarator(*this, SyntaxProduction::MemberDeclarator);
            std::optional<std::string> member_name;
            SourceLocation member_name_location;
            std::size_t member_name_index{};
            auto member_type = co_await parse_declarator_async(copy_type(base_type), member_name,
                DeclaratorContext::Named, nullptr, &member_name_location, nullptr, &member_name_index);
            const auto member_fresh = token_origin(member_name_location).fresh;
            if (member_name && member_fresh) member_name = tokens_[member_name_index].text;
            resolve_specifier_attributes(specifier_attributes, true);
            auto item_attributes = co_await parse_attributes_async();
            item_attributes.insert(
                item_attributes.begin(), member_attributes.begin(),
                member_attributes.end());
            std::unique_ptr<Expr> bit_width;
            if ((co_await consume_async(":"))) {
                bit_width = co_await parse_constant_expression_async();
                auto trailing_attributes = co_await parse_attributes_async();
                item_attributes.insert(
                    item_attributes.end(),
                    std::make_move_iterator(trailing_attributes.begin()),
                    std::make_move_iterator(trailing_attributes.end()));
            } else if (!member_name) {
                (co_await error_here_async("expected record member name"));
                break;
            }
            declaration.members.push_back(
                {member_location, member_name.value_or(std::string{}),
                 std::move(member_type), std::move(bit_width),
                 std::move(item_attributes), member_fresh});
            parsed_member = true;
        } while ((co_await consume_async(",")));
        if (!parsed_member) {
            while (!(co_await current_async()).is(";") && !(co_await current_async()).is("}") &&
                   (co_await current_async()).kind != TokenKind::End) {
                ++index_;
            }
        }
        (co_await expect_async(";", "after record member declaration"));
    }
    (co_await expect_async("}", "after record definition"));
}

void Parser::apply_import(std::string name, TokenIdentity declaration, bool file_scope) {
    if (file_scope) {
        ScopeImportEvent event;
        event.declaration = declaration;
        event.imports.push_back(name);
        if (import_regions_.empty()) event.scope = ScopeImportEvent::Scope::File;
        else event.block = import_regions_.back().first;
        scope_import_events_->push_back(std::move(event));
        active_import_declarations_.push_back(declaration);
    }
    if (syntax_) syntax_->import(name);
    active_imports_.insert(active_imports_.begin() + static_cast<std::ptrdiff_t>(current_scope_imports_),
                          std::move(name));
    ++current_scope_imports_;
}

EvaluationTask<std::unique_ptr<Statement>> Parser::parse_using_declaration_async(bool file_scope) {
    ProductionScope production(*this, SyntaxProduction::UsingDeclaration);
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Empty;
    statement->location = (co_await current_async()).location;
    const auto declaration = token_origin((co_await current_async()).location).identity;
    const auto errors = diagnostics_.errors();
    (co_await consume_async("using"));
    auto imported = (co_await parse_qualified_name_async(SyntaxProduction::NamespaceName));
    if (!imported) (co_await error_here_async("expected namespace name after 'using'"));
    (co_await expect_async(";", "after using declaration"));
    if (imported && diagnostics_.errors() == errors)
        apply_import(std::move(*imported), declaration, file_scope);
    co_return statement;
}

bool Parser::parse_static_assertion(Statement* block_statement) {
    return parse_static_assertion_async(block_statement).run();
}

EvaluationTask<bool> Parser::parse_static_assertion_async(Statement* block_statement) {
    ProductionScope production(*this, SyntaxProduction::StaticAssertDeclaration);
    const auto location = (co_await current_async()).location;
    (co_await consume_async("$::static_assert"));
    (co_await expect_async("("));
    auto condition = co_await parse_constant_expression_async();
    (co_await expect_async(","));
    const auto message_token = (co_await consume_kind_async(TokenKind::String));
    if (!message_token) (co_await error_here_async("expected diagnostic string in $::static_assert"));
    (co_await expect_async(")"));
    (co_await expect_async(";"));
    const auto message = message_token
                             ? decode_string_literal(message_token->text)
                                   .value_or("static assertion failed")
                             : std::string("static assertion failed");
    if (!condition) co_return false;
    // Retain block ownership through parsing and generic substitution. Semantic
    // preparation lifts ordinary assertions into the type-only worklist, while
    // translation-only bodies execute their assertions in the invocation frame.
    if (block_statement) {
        block_statement->kind = Statement::Kind::StaticAssert;
        block_statement->expression = std::move(condition);
        block_statement->assertion_message = message;
        co_return true;
    }
    StaticAssertDecl assertion{location, active_namespace_,
                               std::move(condition), std::move(message)};
    if (active_function_ &&
        !active_function_->generic_parameters.empty())
        active_function_->deferred_static_assertions.push_back(
            std::move(assertion));
    else
        static_assertions_.push_back(std::move(assertion));
    co_return true;
}

void Parser::parse_parameter_list(std::vector<ParameterDecl>& parameters, bool& variadic) {
    parse_parameter_list_async(parameters, variadic).run();
}

EvaluationTask<void> Parser::parse_parameter_list_async(std::vector<ParameterDecl>& parameters, bool& variadic) {
    ProductionScope list(*this, SyntaxProduction::ParameterList);
    PrototypeScope scope;
    if (index_ && tokens_[index_ - 1].is("(")) {
        scope.region.first = token_origin(tokens_[index_ - 1].location).identity;
        if (const auto end = bounded_group_end(index_ - 1); end && *end > index_)
            scope.region.end = token_origin(tokens_[*end - 1].location).identity;
    }
    prototype_scopes_.push_back(std::move(scope));
    // A fragment may supply no parameters, the lone void spelling, a list,
    // or its final ellipsis. Decide only after expansion; speculative capture
    // still throws before running any nested invocation.
    (co_await expand_inline_macro_fragments_async());
    if ((co_await current_async()).is(")")) co_return;
    if ((co_await current_async()).is("void")) {
        const auto first = index_++;
        (co_await expand_inline_macro_fragments_async());
        index_ = first;
        if ((co_await current_async(1)).is(")")) { ++index_; co_return; }
    }
    unsigned ordinal = 0;
    for (;;) {
        (co_await expand_inline_macro_fragments_async());
        if ((co_await consume_async("..."))) {
            variadic = true;
            (co_await expand_inline_macro_fragments_async());
            co_return;
        }
        const auto before = index_;
        parameters.push_back(co_await parse_parameter_async(ordinal++));
        // Empty output after a comma is not an empty list: it leaves an
        // invalid trailing comma and must diagnose as an absent parameter.
        if (index_ == before || !(co_await consume_async(","))) co_return;
    }
}

EvaluationTask<ParameterDecl> Parser::parse_parameter_async(unsigned ordinal) {
    (co_await expand_inline_macro_fragments_async());
    const auto parameter_first = index_;
    ProductionScope production(*this, SyntaxProduction::ParameterDeclaration);
    ParameterDecl parameter;
    parameter.location = (co_await current_async()).location;
    auto attributes = co_await parse_attributes_async();
    (co_await expand_inline_macro_fragments_async());
    if ((co_await current_async()).is("in") || (co_await current_async()).is("out") || (co_await current_async()).is("inout")) {
        ProductionScope mode(*this, SyntaxProduction::ParameterMode);
        if ((co_await consume_async("in"))) parameter.mode = ParameterMode::In;
        else if ((co_await consume_async("out"))) parameter.mode = ParameterMode::Out;
        else { (co_await consume_async("inout")); parameter.mode = ParameterMode::InOut; }
        parameter.explicit_mode = true;
    }
    (co_await expand_inline_macro_fragments_async());
    SpecifierAttributes specifier_attributes{attributes, SpecifierAttributes::Context::Parameter};
    parameter.type = co_await parse_type_async(true, {}, &specifier_attributes);
    std::optional<std::string> name;
    parameter.type = co_await parse_declarator_async(std::move(parameter.type), name, DeclaratorContext::Parameter,
                                      nullptr, &parameter.location);
    resolve_specifier_attributes(specifier_attributes, true);
    if (parameter.type && parameter.type->kind == Type::Kind::Array) {
        parameter.declared_array_type = parameter.type;
        parameter.type = pointer_type(parameter.type->element);
    } else if (parameter.type && parameter.type->kind == Type::Kind::Function) {
        parameter.type = pointer_type(parameter.type);
    }
    if (parameter.mode != ParameterMode::In && parameter.type &&
        parameter.type->is_const) {
        diagnostics_.error(parameter.location,
                           "'out' and 'inout' parameter cells cannot be const");
    }
    parameter.name = name.value_or("_parameter" + std::to_string(ordinal));
    if (name && !prototype_scopes_.empty()) {
        const auto lexical = name_key(parameter).binding;
        if (!preparing_header_) {
            parameter.binding = bind_value(parameter.name, parameter.location, parameter_first,
                ValueBinding::Kind::Local);
            value_rebindings_[lexical] = parameter.binding;
        }
        prototype_scopes_.back().values[NameKey(parameter.name, parameter.location)] =
            {name_key(parameter).binding, parameter.location};
    }
    const auto validate_parameter_attributes = [&](const std::vector<Attribute>& entries) {
        if (parsing_public_fragment_ || preparing_header_) return;
        for (const auto& attribute : entries)
            if (attribute.name == "aligned" || attribute.name == "packed" || attribute.name == "underlying")
                diagnostics_.error(attribute.location,
                    "attribute '" + attribute.name + "' is not valid on a parameter");
    };
    validate_parameter_attributes(attributes);
    apply_callable_attributes(parameter.type, attributes);
    if ((co_await current_async()).kind == TokenKind::String) {
        ProductionScope location_production(*this, SyntaxProduction::Location);
        const auto location = (co_await consume_kind_async(TokenKind::String));
        parameter.location_name = decode_string_literal(location->text);
        if (!parameter.location_name) diagnostics_.error(location->location, "invalid location string");
    }
    auto trailing = co_await parse_attributes_async();
    validate_parameter_attributes(trailing);
    apply_callable_attributes(parameter.type, trailing);
    co_return parameter;
}

EvaluationTask<std::unique_ptr<FunctionDecl>>
Parser::parse_function_async(std::size_t header_first, SourceLocation location, std::string name,
                       std::string name_space, TypePtr return_type,
                       Linkage linkage, bool inline_hint,
                       std::vector<Attribute> attributes,
                       std::shared_ptr<FunctionType> signature,
                       std::vector<FunctionDecl::GenericParameter>
                           angle_parameters) {
    auto function = std::make_unique<FunctionDecl>();
    function->location = location;
    function->name = std::move(name);
    function->source_namespace = std::move(name_space);
    if (location.file)
        function->source_unit = location.file->source_unit_at(location.line);
    function->imports = active_imports_;
    function->return_type = std::move(return_type);
    function->linkage = linkage;
    function->inline_hint = inline_hint;
    function->attributes = std::move(attributes);
    if (signature) {
        function->parameters = signature->parameters;
        function->variadic = signature->variadic;
        function->result_location = signature->result_location;
        if (!signature->abi.empty() && !function->attribute("abi")) {
            function->attributes.push_back(
                {"abi", {"\"" + signature->abi + "\""}, location});
        }
        if (!function->attribute("clobber")) {
            for (const auto& resource : signature->clobbers)
                function->attributes.push_back(
                    {"clobber", {"\"" + resource + "\""}, location});
        }
        if (signature->stack_cleanup &&
            !function->attribute("stack_cleanup")) {
            function->attributes.push_back(
                {"stack_cleanup", {"\"" + *signature->stack_cleanup + "\""},
                 location});
        }
    } else {
        (co_await expect_async("("));
        co_await parse_parameter_list_async(function->parameters, function->variadic);
        (co_await expect_async(")"));
    }
    if ((co_await consume_async("->"))) {
        const auto location_token = (co_await consume_kind_async(TokenKind::String));
        if (!location_token)
            (co_await error_here_async("expected result location string after '->'"));
        else {
            function->result_location =
                decode_string_literal(location_token->text);
            if (!function->result_location) {
                diagnostics_.error(location_token->location,
                                   "invalid result location string");
            }
        }
    }
    auto trailing = co_await parse_attributes_async();
    function->attributes.insert(function->attributes.end(),
                                std::make_move_iterator(trailing.begin()),
                                std::make_move_iterator(trailing.end()));
    function->generic_parameters = generic_parameters(function->attributes);
    if (!angle_parameters.empty()) {
        if (!function->generic_parameters.empty())
            diagnostics_.error(location,
                               "angle generic parameters cannot be combined with [[generic]]");
        else
            function->generic_parameters = std::move(angle_parameters);
    }
    if (!function->generic_parameters.empty()) {
        function->generic_tag_owner = generic_tag_owner_ ? generic_tag_owner_
            : std::make_shared<const GenericTagOwner>();
        if (generic_tag_owner_ && function_scope_) function->function_scope = function_scope_;
    }
    if (!preparing_header_) {
        for (auto& parameter : function->parameters) {
            // A written parameter already owns its final placement. Allocating
            // another one here would create a transient ancestry layer that a
            // composed public header could mistake for its retained binder.
            const NameKey key(parameter.name, parameter.location);
            const bool parsed_here = std::any_of(prototype_scopes_.begin(), prototype_scopes_.end(),
                [&](const PrototypeScope& scope) {
                    const auto found = scope.values.find(key);
                    return found != scope.values.end() && found->second.binding == parameter.binding;
                });
            if (parsed_here) continue;
            const auto lexical = name_key(parameter).binding;
            parameter.binding = bind_value(parameter.name, parameter.location, header_first,
                ValueBinding::Kind::Local);
            value_rebindings_[lexical] = parameter.binding;
        }
        for (auto& parameter : function->generic_parameters) {
            if (!parameter.value_type) continue;
            // Header preparation can expose a generic binder before its final
            // parse. Its exact lexical key must follow this same declaration,
            // never a same-spelled parameter in another destination header.
            const auto lexical = name_key(parameter).binding;
            parameter.binding = bind_value(parameter.name, parameter.location, header_first,
                ValueBinding::Kind::Local);
            value_rebindings_[lexical] = parameter.binding;
        }
        for (auto& attribute : function->attributes)
            for (auto& binding : attribute.variadic_bindings)
                binding.binding = bind_value(binding.name, binding.location, header_first,
                    ValueBinding::Kind::Local);
    }
    // Attributes preceding a function header are parsed before its generic
    // parameter list becomes part of the active function. Bind their scalar
    // expressions now, without rebinding a copied token that already has an
    // immutable source context of its own.
    std::function<void(const TypePtr&)> bind_type_bounds;
    const auto bind_attribute_name = [&](const auto& self, Expr& expression) -> void {
        bind_type_bounds(expression.type);
        if (expression.kind == Expr::Kind::Name && expression.name_context) {
            auto binding = expression.name_context->value_binding;
            for (auto remaining = value_rebindings_.size(); remaining; --remaining) {
                const auto found = value_rebindings_.find(binding);
                if (found == value_rebindings_.end()) break;
                binding = found->second;
            }
            if (binding != expression.name_context->value_binding) {
                auto context = std::make_shared<NameLookupContext>(*expression.name_context);
                context->value_binding = binding;
                if (binding.kind == ValueBinding::Kind::Local)
                    context->kind = NameLookupContext::Kind::Local;
                expression.name_context = std::move(context);
            }
        }
        if (expression.kind == Expr::Kind::Name &&
            (!expression.name_context || !expression.name_context->value_binding_inherited)) {
            for (const auto& parameter : function->generic_parameters) {
                if (!parameter.value_type ||
                    NameKey(expression.text, expression.location) !=
                        NameKey(parameter.name, parameter.location)) continue;
                auto context = expression.name_context
                    ? std::make_shared<NameLookupContext>(*expression.name_context)
                    : std::make_shared<NameLookupContext>();
                context->kind = NameLookupContext::Kind::Local;
                context->value_binding = name_key(parameter).binding;
                expression.name_context = std::move(context);
                if (!preparing_header_)
                    for (auto& token : tokens_)
                        if (token.location.file == expression.location.file &&
                            token.location.offset == expression.location.offset &&
                            token.text == expression.text)
                            token.value_binding = expression.name_context->value_binding;
                break;
            }
        }
        if (expression.left) self(self, *expression.left);
        if (expression.right) self(self, *expression.right);
        if (expression.third) self(self, *expression.third);
        for (auto& argument : expression.arguments) self(self, *argument);
        for (auto& argument : expression.generic_arguments) {
            bind_type_bounds(argument.type);
            if (argument.value) self(self, *argument.value);
        }
    };
    std::unordered_set<const Type*> bound_types;
    bind_type_bounds = [&](const TypePtr& type) -> void {
        if (!type) return;
        if (!bound_types.insert(type.get()).second) return;
        bind_generic_type(*type, function->generic_parameters);
        if (type->vector_bound && header_type_bounds_.contains(type->vector_bound.get())) {
            auto expression = copy_expression(*type->vector_bound);
            bind_attribute_name(bind_attribute_name, *expression);
            type->vector_bound = std::move(expression);
        }
        if (type->array_bound && header_type_bounds_.contains(type->array_bound.get())) {
            auto expression = copy_expression(*type->array_bound);
            bind_attribute_name(bind_attribute_name, *expression);
            type->array_bound = std::move(expression);
        }
        bind_type_bounds(type->element);
        bind_type_bounds(type->pointee);
        if (type->function) {
            bind_type_bounds(type->function->result);
            for (const auto& parameter : type->function->parameters) {
                bind_type_bounds(parameter.type);
                bind_type_bounds(parameter.declared_array_type);
            }
        }
    };
    for (auto& attribute : function->attributes) {
        if (attribute.expression_argument)
            bind_attribute_name(bind_attribute_name, *attribute.expression_argument);
        for (const auto& binding : attribute.variadic_bindings)
            bind_type_bounds(binding.type);
    }
    bind_type_bounds(function->return_type);
    for (const auto& parameter : function->parameters) {
        bind_type_bounds(parameter.type);
        bind_type_bounds(parameter.declared_array_type);
    }
    for (const auto& parameter : function->generic_parameters)
        bind_type_bounds(parameter.value_type);
    // Definitions nested in a header live in the declaration tables, not in
    // its nominal Type leaves. Bind their generic uses after the complete
    // parameter list is known, just like header bounds and attributes above.
    const auto bind_attributes = [&](std::vector<Attribute>& nested) {
        for (auto& attribute : nested)
            if (attribute.expression_argument)
                bind_attribute_name(bind_attribute_name, *attribute.expression_argument);
    };
    if (function->generic_tag_owner) {
        for (auto& record : pending_records_) {
            if (!record.nominal_identity ||
                record.nominal_identity->generic_owner != function->generic_tag_owner) continue;
            function->header_types.push_back(record.nominal_key());
            bind_attributes(record.attributes);
            for (auto& member : record.members) {
                bind_type_bounds(member.type);
                if (member.bit_width) bind_attribute_name(bind_attribute_name, *member.bit_width);
                bind_attributes(member.attributes);
            }
        }
        for (auto& enumeration : pending_enumerations_) {
            if (!enumeration.nominal_identity ||
                enumeration.nominal_identity->generic_owner != function->generic_tag_owner) continue;
            function->header_types.push_back(enumeration.nominal_key());
            bind_attributes(enumeration.attributes);
            for (auto& enumerator : enumeration.enumerators)
                if (enumerator.initializer)
                    bind_attribute_name(bind_attribute_name, *enumerator.initializer);
        }
        // Identity ordinals follow the order in which the header introduces its tags.
        std::sort(function->header_types.begin(), function->header_types.end(),
            [](const NominalTypeKey& left, const NominalTypeKey& right) {
                return left.identity->ordinal < right.identity->ordinal;
            });
    }
    // The containing declaration owns its separators or the function body.
    co_return function;
}

EvaluationTask<std::unique_ptr<ObjectDecl>> Parser::parse_object_async(
    SourceLocation location, std::string name, TypePtr type, Linkage linkage,
    std::vector<Attribute> attributes, bool consume_semicolon) {
    auto object = std::make_unique<ObjectDecl>();
    object->location = location;
    object->name = std::move(name);
    if (location.file) object->source_unit = location.file->source_unit_at(location.line);
    object->type = std::move(type);
    object->linkage = linkage;
    object->attributes = std::move(attributes);
    if (preparing_header_) throw HeaderPrepared{};
    if ((co_await consume_async("="))) object->initializer = co_await parse_initializer_async();
    if (object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 && !object->type->array_bound && object->initializer &&
        object->initializer->kind == Expr::Kind::String &&
        object->type->element &&
        object->type->element->kind == Type::Kind::Builtin &&
        object->type->element->builtin == BuiltinType::U8) {
        object->type->lanes = static_cast<std::uint32_t>(
            object->initializer->string_value.size() + 1);
    }
    if (!parsing_public_fragment_ && object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 && !object->type->array_bound &&
        !object->initializer) {
        diagnostics_.error(location,
                           "an omitted array bound requires an initializer");
    }
    if (consume_semicolon) (co_await expect_async(";"));
    co_return object;
}

EvaluationTask<bool> Parser::local_declaration_start_async() {
    if ((co_await current_async()).is("register") || (co_await current_async()).is("stack") ||
        (co_await current_async()).is("static") || (co_await current_async()).is("typedef")) co_return true;
    co_return co_await type_start_async(TypeProbe::ExpressionAlternative);
}

std::unique_ptr<Statement> Parser::parse_local_declaration(std::vector<Attribute> attributes,
    bool consume_semicolon, SyntaxProduction production) {
    return parse_local_declaration_async(std::move(attributes), consume_semicolon, production).run();
}

Parser::StatementTask
Parser::parse_local_declaration_async(std::vector<Attribute> attributes,
                                bool consume_semicolon,
                                SyntaxProduction production_name) {
    ProductionScope production(*this, production_name);
    const auto location = (co_await current_async()).location;
    bool storage_register = false;
    bool storage_stack = false;
    bool storage_static = false;
    bool typedef_seen = false;
    const auto consume_storage_async = [&]() -> EvaluationTask<bool> {
        if ((co_await current_async()).is("typedef")) {
            const auto storage_location = (co_await current_async()).location;
            ++index_;
            if (typedef_seen || storage_register || storage_stack || storage_static)
                diagnostics_.error(storage_location,
                    "typedef cannot combine with another storage specifier");
            typedef_seen = true;
            co_return true;
        }
        bool* selected = nullptr;
        if ((co_await current_async()).is("register")) selected = &storage_register;
        else if ((co_await current_async()).is("stack")) selected = &storage_stack;
        else if ((co_await current_async()).is("static")) selected = &storage_static;
        if (!selected) co_return false;
        const auto storage_location = (co_await current_async()).location;
        ++index_;
        if (typedef_seen)
            diagnostics_.error(storage_location, "typedef cannot combine with another storage specifier");
        else if (storage_register || storage_stack || storage_static)
            diagnostics_.error(storage_location, "local declaration has more than one storage specifier");
        *selected = true;
        co_return true;
    };
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    if ((co_await current_async()).is("typedef") || (co_await current_async()).is("register") ||
        (co_await current_async()).is("stack") || (co_await current_async()).is("static")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        (void)(co_await consume_storage_async());
    }
    SpecifierAttributes specifier_attributes{attributes};
    auto base_type = co_await parse_type_async(false, consume_storage_async, &specifier_attributes, true);
    specifiers.finish();
    if (typedef_seen) {
        resolve_specifier_attributes(specifier_attributes, true);
        co_await parse_typedef_async({}, std::move(attributes), std::move(base_type), location,
                      consume_semicolon);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = location;
        co_return statement;
    }
    if ((co_await current_async()).is(";") && base_type && !base_type->nominal_key().empty() &&
        !storage_register && !storage_stack && !storage_static) {
        resolve_specifier_attributes(specifier_attributes, false);
        if (consume_semicolon) (co_await consume_async(";"));
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = location;
        co_return statement;
    }
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    auto result = std::make_unique<Statement>();
    result->kind = Statement::Kind::DeclarationList;
    result->location = location;
    do {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Declaration;
        statement->location = (co_await current_async()).location;
        statement->declaration = std::make_unique<VariableDecl>();
        auto& declaration = *statement->declaration;
        declaration.location = (co_await current_async()).location;
        declaration.storage_register = storage_register;
        declaration.storage_stack = storage_stack;
        declaration.storage_static = storage_static;
        std::optional<std::string> name;
        const auto declarator_first = index_;
        declaration.type = co_await parse_declarator_async(copy_type(base_type), name, DeclaratorContext::Named,
            storage_static ? nullptr : &declaration.dynamic_array_bound, &declaration.location);
        resolve_specifier_attributes(specifier_attributes, true);
        if (!name) (co_await error_here_async("expected local variable name"));
        else {
            declaration.name = *name;
            declaration.binding = bind_value(*name, declaration.location, declarator_first, ValueBinding::Kind::Local);
            if (!local_scopes_.empty()) {
                const NameKey key(declaration.name, declaration.location);
                if (local_type_scopes_.back().contains(key))
                    diagnostics_.error(declaration.location,
                        "local value '" + declaration.name + "' conflicts with a typedef");
                if (local_scopes_.back().contains(key))
                    diagnostics_.error(declaration.location,
                        "local value '" + declaration.name +
                        "' was declared more than once");
                local_scopes_.back()[key] = name_key(declaration).binding;
            }
        }
        if ((co_await current_async()).kind == TokenKind::String) {
            ProductionScope location_production(*this, SyntaxProduction::ObjectLocation);
            const auto location_token = (co_await consume_kind_async(TokenKind::String));
            declaration.location_name = decode_string_literal(location_token->text);
            if (!declaration.location_name)
                diagnostics_.error(location_token->location, "invalid location string");
        }
        declaration.attributes = attributes;
        auto trailing = co_await parse_attributes_async();
        declaration.attributes.insert(declaration.attributes.end(),
            std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
        for (const auto& attribute : declaration.attributes) {
            if (!parsing_public_fragment_ && attribute.name != "aligned") {
                diagnostics_.error(attribute.location,
                    "attribute '" + attribute.name + "' is not valid on a local object");
            }
            // Target-dependent constants are resolved after nominal HIR layout.
        }
        if ((co_await consume_async("="))) declaration.initializer = co_await parse_initializer_async();
        if (declaration.type && declaration.type->kind == Type::Kind::Array &&
            declaration.type->lanes == 0 && !declaration.type->array_bound && !declaration.dynamic_array_bound &&
            declaration.initializer && declaration.initializer->kind == Expr::Kind::String &&
            declaration.type->element && declaration.type->element->kind == Type::Kind::Builtin &&
            declaration.type->element->builtin == BuiltinType::U8) {
            declaration.type->lanes = static_cast<std::uint32_t>(
                declaration.initializer->string_value.size() + 1);
        }
        if (!parsing_public_fragment_ && declaration.type && declaration.type->kind == Type::Kind::Array &&
            declaration.type->lanes == 0 && !declaration.type->array_bound && !declaration.dynamic_array_bound &&
            (!declaration.initializer ||
             (!declaration.storage_static && declaration.initializer->kind != Expr::Kind::AggregateInitializer))) {
            diagnostics_.error(declaration.location,
                               "an omitted array bound requires an eligible initializer");
        }
        result->statements.push_back(std::move(statement));
    } while ((co_await consume_async(",")));
    list.finish();
    if (consume_semicolon) (co_await expect_async(";"));
    if (result->statements.size() == 1) co_return std::move(result->statements.front());
    co_return result;
}

Parser::StatementTask Parser::parse_compound_async() {
    ProductionScope production(*this, SyntaxProduction::CompoundStatement);
    if ((co_await current_async()).kind == TokenKind::StructuredSplice) {
        const auto item = (co_await current_async());
        if (!item.splice || !syntax_compound_node(*item.splice)) {
            (co_await error_here_async("structured function body requires a compound-statement node"));
            ++index_;
            co_return {};
        }
        if (parsing_public_fragment_) {
            if (production.event < production_events_.size())
                production_events_[production.event].opaque = item.splice;
            ++index_;
            auto body = std::make_unique<Statement>();
            body->kind = Statement::Kind::Compound;
            body->location = item.location;
            co_return body;
        }
        auto body = co_await parse_statement_async();
        if (body && body->kind != Statement::Kind::Compound)
            diagnostics_.error(item.location,
                "structured function body must contain one complete compound statement");
        co_return body;
    }
    const auto first = index_;
    const auto production_depth = production_stack_.size();
    std::optional<std::size_t> end_input;
    std::shared_ptr<const SyntaxContext> context;
    TokenIdentity source_end;
    if ((parsing_public_fragment_ || token_origin((co_await current_async()).location).context) && !public_tree_failed_)
        if (const auto end = bounded_group_end(first); end && *end > first) {
            source_end = token_origin(tokens_[*end - 1].location).identity;
            if (parsing_public_fragment_) end_input = public_input_indices_[*end];
        }
    if (parsing_public_fragment_ && !public_tree_failed_) {
        context = public_fragment_context(first);
    }
    if (parsing_public_fragment_ && public_tree_failed_) {
        // A failed ProductionScope has no event ID. Do not publish or index
        // a deferred event after resource exhaustion; the caller reports it.
        auto failed = std::make_unique<Statement>();
        failed->kind = Statement::Kind::Compound;
        failed->location = (co_await current_async()).location;
        co_return failed;
    }
    const auto saved_imports = active_imports_;
    const auto saved_import_declarations = active_import_declarations_;
    const auto saved_scope_imports = current_scope_imports_;
    const auto saved_values_depth = local_scopes_.size();
    const auto saved_types_depth = local_type_scopes_.size();
    const auto saved_value_rebindings = value_rebindings_;
    const auto saved_tag_rebindings = tag_rebindings_;
    const auto saved_alias_rebindings = alias_rebindings_;
    const auto saved_uncertain_count = public_uncertain_binding_depths_.size();
    const auto saved_recording = recording_public_tree_;
    const auto saved_generic_argument = parsing_generic_argument_;
    const auto saved_generic_types = parsing_public_fragment_
        ? active_generic_types_ : std::vector<std::string>{};
    const auto saved_switch_depth = switch_depth_;
    const auto saved_switch_defaults = parsing_public_fragment_
        ? switch_default_seen_ : std::vector<bool>{};
    current_scope_imports_ = 0;
    ScopePlacementFrame placement(*this, (co_await current_async()).location, source_end);
    local_scopes_.emplace_back();
    local_type_scopes_.emplace_back();
    local_tag_scopes_.emplace_back();
    scope_origins_.push_back(token_origin((co_await current_async()).location).identity);
    scope_ends_.push_back(source_end);
    if (syntax_) syntax_->push_scope();
    const bool function_body = active_function_ &&
        (local_scopes_.size() == 1 ||
         (generic_header_depth_ && local_scopes_.size() == *generic_header_depth_ + 1));
    if (function_body)
        for (const auto& parameter : active_function_->parameters) {
            if (!local_scopes_.back().emplace(NameKey(parameter.name, parameter.location),
                    name_key(parameter).binding).second)
                diagnostics_.error(parameter.location,
                    "duplicate parameter name '" + parameter.name + "'");
        }
    if (function_body)
        for (const auto& attribute : active_function_->attributes)
            if (attribute.name == "variadic")
                for (const auto& name : attribute.variadic_bindings) {
                    if (!local_scopes_.back().emplace(NameKey(name.name, name.location),
                            name_key(name).binding).second)
                        diagnostics_.error(name.location,
                            "variadic state binding '" + name.name +
                            "' conflicts with another local name");
                }
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Compound;
    statement->location = (co_await current_async()).location;
    try {
        (co_await expect_async("{"));
        while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
            const auto before = index_;
            const auto statement_identity = token_origin((co_await current_async()).location).identity;
            const auto prior_imports = current_scope_imports_;
            const bool record_bindings = syntax_ && !parsing_public_fragment_ &&
                statement_identity.source_unit && scope_origins_.back().source_unit;
            TokenOrigin source_statement;
            if (record_bindings)
                source_statement = statement_origin((co_await current_async()), syntax_->execution().get());
            NameSet prior_values;
            NameSet prior_aliases;
            NameMap<LocalTag> prior_tags;
            if (record_bindings) {
                prior_tags = local_tag_scopes_.back();
                for (const auto& [key, binding] : local_scopes_.back()) {
                    (void)binding;
                    prior_values.insert(key);
                }
                for (const auto& [key, type] : local_type_scopes_.back()) {
                    (void)type;
                    prior_aliases.insert(key);
                }
            }
            if ((co_await current_async()).is("syntax") && (syntax_ || token_origin((co_await current_async()).location).context)) {
                auto declaration = std::make_unique<Statement>();
                declaration->kind = Statement::Kind::Empty;
                declaration->location = (co_await current_async()).location;
                (void)co_await parse_syntax_registration_async(nullptr);
                statement->statements.push_back(std::move(declaration));
            } else if ((co_await current_async()).is("using")) {
                statement->statements.push_back((co_await parse_using_declaration_async(false)));
            } else statement->statements.push_back(co_await parse_statement_async(true));
            if (prior_imports < current_scope_imports_ && scope_origins_.back().source_unit) {
                ScopeImportEvent imported{scope_origins_.back(), statement_identity, {}};
                for (auto at = prior_imports; at < current_scope_imports_; ++at)
                    imported.imports.push_back(active_imports_[at]);
                scope_import_events_->push_back(std::move(imported));
                active_import_declarations_.push_back(statement_identity);
            }
            if (record_bindings) {
                ScopeEvent event;
                event.block = scope_origins_.back();
                event.statement = source_statement.identity;
                event.statement_context = source_statement.context;
                event.placement = scope_placement_;
                for (const auto& [key, binding] : local_scopes_.back())
                    if (!prior_values.contains(key)) event.values.emplace(key, binding);
                for (const auto& [key, type] : local_type_scopes_.back())
                    if (!prior_aliases.contains(key))
                        event.aliases.emplace(key, type);
                for (const auto& [key, tag] : local_tag_scopes_.back()) {
                    const auto prior = prior_tags.find(key);
                    if (prior == prior_tags.end() || prior->second.complete != tag.complete ||
                        !same_type(prior->second.type, tag.type))
                        event.tags.emplace(key, LocalTag{copy_type(tag.type), tag.complete});
                }
                if (!event.values.empty() || !event.aliases.empty() || !event.tags.empty())
                    scope_events_->push_back(std::move(event));
            }
            if (index_ == before && (co_await current_async()).kind != TokenKind::End) ++index_;
        }
        (co_await expect_async("}"));
    } catch (const DeferredNameRecognition&) {
        // The complete brace unit is independently bounded. Stop before
        // deciding any name-sensitive grammar; inspection must not execute
        // the invocations that may establish those bindings.
        recording_public_tree_ = saved_recording;
        parsing_generic_argument_ = saved_generic_argument;
        active_generic_types_ = saved_generic_types;
        switch_depth_ = saved_switch_depth;
        switch_default_seen_ = saved_switch_defaults;
        production_stack_.resize(production_depth);
        if (public_tree_failed_) {
            // Keep the budget failure; no partially recorded tree is usable.
        } else if (!end_input) {
            error_here("name-sensitive capture boundary requires a raw bounded group");
        } else {
            const auto end = std::find(public_input_indices_.begin() +
                static_cast<std::ptrdiff_t>(first), public_input_indices_.end(), *end_input);
            if (end == public_input_indices_.end()) {
                error_here("deferred syntax boundary lost its source index");
            } else {
                index_ = static_cast<std::size_t>(end - public_input_indices_.begin());
                auto opaque = deferred_node(first, index_, SyntaxProduction::CompoundStatement,
                                             SyntaxParseCategory::Statement, std::move(context));
                production_events_.resize(production.event + 1);
                auto& event = production_events_[production.event];
                event.children.clear();
                event.opaque = std::move(opaque);
                statement->statements.clear();
            }
        }
    }
    active_imports_ = saved_imports;
    active_import_declarations_ = saved_import_declarations;
    current_scope_imports_ = saved_scope_imports;
    local_scopes_.resize(saved_values_depth);
    local_type_scopes_.resize(saved_types_depth);
    local_tag_scopes_.resize(saved_types_depth);
    value_rebindings_ = saved_value_rebindings;
    tag_rebindings_ = saved_tag_rebindings;
    alias_rebindings_ = saved_alias_rebindings;
    scope_origins_.resize(saved_values_depth);
    scope_ends_.resize(saved_values_depth);
    public_uncertain_binding_depths_.resize(saved_uncertain_count);
    if (syntax_) syntax_->pop_scope();
    co_return statement;
}

std::unique_ptr<Statement> Parser::parse_compound() {
    return parse_compound_async().run();
}

Parser::StatementTask Parser::parse_statement_async(bool block_item) {
    ProductionScope production(*this, SyntaxProduction::Statement);
    if ((co_await current_async()).kind == TokenKind::StructuredSplice &&
        (!(co_await current_async()).splice || (!syntax_type_node(*(co_await current_async()).splice) &&
                              !syntax_expression_node(*(co_await current_async()).splice)))) {
        const auto item = (co_await current_async());
        ++index_;
        auto invalid = std::make_unique<Statement>();
        invalid->kind = Statement::Kind::Empty;
        invalid->location = item.location;
        const bool declaration_node = item.splice && syntax_declaration_node(*item.splice);
        const bool using_node = item.splice && item.splice->kind == SyntaxNode::Kind::Core &&
            item.splice->production == SyntaxProduction::UsingDeclaration;
        const bool global_label_node = item.splice && item.splice->kind == SyntaxNode::Kind::Core &&
            item.splice->production == SyntaxProduction::GlobalLabelDeclaration;
        if ((using_node && !block_item) || global_label_node) {
            diagnostics_.error(item.location, global_label_node
                ? "global-label declaration splice requires item scope"
                : "using declaration splice requires a compound-statement block item");
            co_return invalid;
        }
        if (!item.splice || (!syntax_statement_node(*item.splice) && !declaration_node)) {
            diagnostics_.error(item.location,
                "structured syntax splice requires a statement node at statement position");
            co_return invalid;
        }
        if (!syntax_ || !item.splice->context ||
            !item.splice->context->parse_environment) {
            diagnostics_.error(item.location,
                "structured syntax splice has no retained parse environment");
            co_return invalid;
        }
        if (parsing_public_fragment_ && declaration_node &&
            !(co_await recognize_declaration_splice_async(item, PublicDeclarationContext::Block))) {
            diagnostics_.error(item.location,
                "structured declaration does not match destination block grammar");
            co_return invalid;
        }
        if (recording_public_tree_ && production.event < production_events_.size()) {
            const bool whole_statement = item.splice->kind == SyntaxNode::Kind::Core
                ? item.splice->production == SyntaxProduction::Statement
                : item.splice->slot_production == SyntaxProduction::Statement;
            if (whole_statement || using_node)
                production_events_[production.event].opaque = item.splice;
            else {
                std::shared_ptr<const SyntaxNode> child = item.splice;
                if (declaration_node || syntax_compound_node(*item.splice)) {
                    auto unattributed = std::make_shared<SyntaxNode>();
                    unattributed->kind = SyntaxNode::Kind::Core;
                    unattributed->production = SyntaxProduction::UnattributedStatement;
                    unattributed->span = item.splice->span;
                    unattributed->context = item.splice->context;
                    unattributed->children.push_back(child);
                    child = std::move(unattributed);
                }
                auto wrapper = std::make_shared<SyntaxNode>();
                wrapper->kind = SyntaxNode::Kind::Core;
                wrapper->production = SyntaxProduction::Statement;
                wrapper->span = item.splice->span;
                wrapper->context = item.splice->context;
                wrapper->children.push_back(std::move(child));
                wrapper->splice_children.push_back(0);
                production_events_[production.event].opaque = std::move(wrapper);
            }
        }
        // Recognition keeps the original statement node without executing
        // opaque descendants. Surviving output is parsed in its saved lookup
        // environment, then only declarations made by this statement enter
        // the destination block.
        if (parsing_public_fragment_ && !using_node) co_return invalid;
        if ((local_scopes_.empty() || local_type_scopes_.empty()) &&
            !syntax_compound_node(*item.splice)) {
            diagnostics_.error(item.location,
                "structured statement splice requires a destination block");
            co_return invalid;
        }
        auto output = syntax_->execution()->materialize_node(
            *item.splice, item.location, declaration_node
                ? SyntaxParseCategory::Declaration : SyntaxParseCategory::Statement);
        if (!output) co_return invalid;
        auto child = replacement_parser(std::move(*output));
        child->scope_import_events_ = std::make_shared<std::vector<ScopeImportEvent>>(*scope_import_events_);
        const auto prior_import_events = child->scope_import_events_->size();
        if (item.splice->kind == SyntaxNode::Kind::Deferred)
            child->restore_deferred_environment(
                *item.splice->context->parse_environment,
                *item.splice->context, deferred_position(*item.splice));
        else
            child->restore_environment(*item.splice->context->parse_environment,
                                       *item.splice->context);
        // Name lookup belongs to the captured node; control-flow placement
        // (notably switch labels) belongs to the destination statement.
        child->switch_depth_ = switch_depth_;
        child->switch_default_seen_ = switch_default_seen_;
        if (child->local_scopes_.empty() && !syntax_compound_node(*item.splice)) {
            child->local_scopes_.emplace_back();
            child->local_type_scopes_.emplace_back();
            child->local_tag_scopes_.emplace_back();
            child->scope_origins_.emplace_back();
            child->scope_ends_.emplace_back();
        }
        const auto prior_values = child->local_scopes_.empty()
            ? NameMap<ValueBinding>{} : child->local_scopes_.back();
        const auto prior_aliases = child->local_type_scopes_.empty()
            ? NameMap<AliasDefinitionPtr>{} : child->local_type_scopes_.back();
        const auto prior_records = child->tag_state();
        const auto prior_imports = child->current_scope_imports_;
        child->prepare_tag_destination(*this);
        const auto previous_errors = diagnostics_.errors();
        const bool imported_declaration = declaration_node && (co_await child->current_async()).is("using");
        if (imported_declaration && !block_item) {
            diagnostics_.error(item.location,
                "using declaration splice requires a compound-statement block item");
            co_return invalid;
        }
        std::unique_ptr<Statement> statement;
        if (imported_declaration) statement = (co_await child->parse_using_declaration_async(false));
        else statement = co_await child->parse_statement_async();
        if ((co_await child->current_async()).kind != TokenKind::End)
            (co_await child->error_here_async("structured statement splice must contain one complete statement"));
        if (diagnostics_.errors() != previous_errors || !statement) co_return invalid;
        // A compound owns an inner scope. Only a direct declaration statement
        // can add bindings to the surrounding destination block.
        std::vector<ValueBinding> introduced_values;
        if (!syntax_compound_node(*item.splice)) {
            for (const auto& [key, binding] : child->local_scopes_.back()) {
                if (prior_values.contains(key)) continue;
                if (local_type_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced local value '" + key.spelling + "' conflicts with a destination typedef");
                else if (local_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced local value '" + key.spelling + "' was declared more than once");
                else {
                    local_scopes_.back().emplace(key, binding);
                    introduced_values.push_back(binding);
                }
            }
            for (const auto& [key, type] : child->local_type_scopes_.back()) {
                if (prior_aliases.contains(key)) continue;
                if (local_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced typedef '" + key.spelling + "' conflicts with a destination value");
                else if (const auto found = local_type_scopes_.back().find(key);
                         found != local_type_scopes_.back().end()) {
                    if (!same_type(found->second->instantiate(), type->instantiate()))
                        diagnostics_.error(item.location,
                            "spliced typedef '" + key.spelling + "' has a different destination type");
                } else local_type_scopes_.back().emplace(key, type);
                transfer_alias_rebindings(*child, type);
            }
        }
        if (diagnostics_.errors() != previous_errors) co_return invalid;
        if (!transfer_spliced_tags(*child, prior_records, item.location)) co_return invalid;
        for (const auto& [key, binding] : child->value_rebindings_)
            if (std::find(introduced_values.begin(), introduced_values.end(), binding) != introduced_values.end())
                value_rebindings_[key] = binding;
        switch_default_seen_ = std::move(child->switch_default_seen_);
        scope_import_events_->insert(scope_import_events_->end(),
            child->scope_import_events_->begin() + static_cast<std::ptrdiff_t>(prior_import_events),
            child->scope_import_events_->end());
        for (auto at = prior_imports; at < child->current_scope_imports_; ++at)
            apply_import(child->active_imports_[at], token_origin(item.location).identity, false);
        for (auto& assertion : child->static_assertions_)
            static_assertions_.push_back(std::move(assertion));
        // Restored function metadata is detached from the destination AST.
        // Keep surviving generic assertions instead of losing them when the
        // bounded parser and its temporary function context are destroyed.
        if (child->restored_function_context_) {
            auto& assertions = active_function_ && !active_function_->generic_parameters.empty()
                ? active_function_->deferred_static_assertions : static_assertions_;
            for (auto& assertion : child->restored_function_context_->deferred_static_assertions)
                assertions.push_back(std::move(assertion));
            for (auto& requirement : child->restored_function_context_->required_types) {
                if (active_function_) active_function_->required_types.push_back(std::move(requirement));
                else required_types_.push_back(std::move(requirement));
            }
        }
        co_return statement;
    }
    auto attributes = co_await parse_attributes_async();
    co_return co_await parse_unattributed_statement_async(std::move(attributes), block_item);
}

std::unique_ptr<Statement> Parser::parse_statement(bool block_item) {
    return parse_statement_async(block_item).run();
}

Parser::StatementTask Parser::parse_unattributed_statement_async(
    std::vector<Attribute> attributes, bool block_item) {
    ProductionScope production(*this, SyntaxProduction::UnattributedStatement);
    if (!attributes.empty()) {
        if ((co_await current_async()).is("global") && (co_await current_async(1)).is("label") &&
            (co_await current_async(2)).kind == TokenKind::Identifier && (co_await current_async(3)).is(":"))
            co_return co_await parse_global_label_statement_async(std::move(attributes));
        if ((co_await local_declaration_start_async()))
            co_return co_await parse_local_declaration_async(std::move(attributes));
        if (parsing_public_fragment_) {
            // Public captures preserve balanced attribute grammar for
            // inspection/discard/repair. Surviving source gets the ordinary
            // placement, argument and duplicate-contract checks below.
        } else if ((co_await current_async()).is("return")) {
            bool musttail_seen = false;
            std::vector<Attribute> valid;
            for (auto& attribute : attributes) {
                if (attribute.name != "musttail") {
                    diagnostics_.error(attribute.location, "attribute '" + attribute.name +
                        "' is not valid on a return statement");
                    continue;
                }
                if (!attribute.arguments.empty()) {
                    diagnostics_.error(attribute.location, "musttail does not take arguments");
                    continue;
                }
                if (musttail_seen) {
                    diagnostics_.error(attribute.location,
                        "a return statement has at most one musttail attribute");
                    continue;
                }
                musttail_seen = true;
                valid.push_back(std::move(attribute));
            }
            attributes = std::move(valid);
        } else {
            for (const auto& attribute : attributes)
                diagnostics_.error(attribute.location, "attribute '" + attribute.name +
                    "' is not valid on this statement");
            attributes.clear();
        }
    }
    if ((co_await current_async()).is("syntax") && (syntax_ || token_origin((co_await current_async()).location).context)) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = (co_await current_async()).location;
        (co_await error_here_async(replacement_ || token_origin((co_await current_async()).location).context
            ? "expansion output cannot introduce syntax registration"
            : "syntax registration requires an external item or compound block item position"));
        ++index_;
        synchronize_external();
        co_return statement;
    }
    if (parsing_public_fragment_ && macro_start()) {
        auto placeholder = std::make_unique<Statement>();
        placeholder->kind = Statement::Kind::Empty;
        placeholder->location = (co_await current_async()).location;
        if (!(co_await parse_opaque_invocation_async(SyntaxKind::Statement))) {
            (co_await error_here_async("could not recognize a bounded opaque statement invocation"));
            co_return placeholder;
        }
        // An explicit operator/suffix proves continuation, not expression
        // grammar: textual output may supply a declaration, return, or several
        // block items. Retain the independently bounded statement (or enclosing
        // brace unit) until its owner survives, without executing this macro.
        if (opaque_statement_has_expression_continuation()) throw DeferredNameRecognition{};
        mark_public_binding_uncertainty();
        co_return placeholder;
    }
    if (const auto* definition = active_syntax(false)) {
        if (definition->kind == SyntaxKind::Statement) {
            if (!parsing_public_fragment_) co_return co_await parse_statement_replacement_async(block_item);
            auto placeholder = std::make_unique<Statement>();
            placeholder->kind = Statement::Kind::Empty;
            placeholder->location = (co_await current_async()).location;
            if (!(co_await parse_opaque_invocation_async(SyntaxKind::Statement)))
                (co_await error_here_async("could not recognize a bounded opaque statement invocation"));
            mark_public_binding_uncertainty();
            co_return placeholder;
        }
        ProductionScope expression_statement(*this, SyntaxProduction::ExpressionStatement);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Expression;
        statement->location = (co_await current_async()).location;
        statement->expression = co_await parse_expression_async();
        (co_await expect_async(";", "after expression statement"));
        co_return statement;
    }
    if ((co_await current_async()).is("{")) co_return co_await parse_compound_async();
    if ((co_await current_async()).is("$::static_assert")) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = (co_await current_async()).location;
        (void)co_await parse_static_assertion_async(statement.get());
        co_return statement;
    }
    if ((co_await current_async()).is("global") && (co_await current_async(1)).is("label") &&
        (co_await current_async(2)).kind == TokenKind::Identifier && (co_await current_async(3)).is(":")) {
        co_return co_await parse_global_label_statement_async();
    }
    if (((co_await current_async()).kind == TokenKind::Identifier && !(co_await current_async()).is("default") && (co_await current_async(1)).is(":")) ||
        ((co_await current_async()).is("label") && (co_await current_async(1)).kind == TokenKind::Identifier && (co_await current_async(2)).is(":"))) {
        ProductionScope labeled_statement(*this, SyntaxProduction::LabeledStatement);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Label;
        statement->location = (co_await current_async()).location;
        (co_await consume_async("label"));
        statement->label_name = identifier_binding_name((co_await current_async()));
        statement->label_location = (co_await current_async()).location;
        remember_label_binding(*statement, index_);
        statement->label_fresh = token_origin((co_await current_async()).location).fresh;
        ++index_;
        (co_await expect_async(":"));
        if (parsing_public_fragment_ ||
            ((co_await current_async()).kind != TokenKind::End && !(co_await current_async()).is("}")))
            statement->first = co_await parse_statement_async();
        co_return statement;
    }
    if ((co_await local_declaration_start_async())) co_return co_await parse_local_declaration_async();
    const auto head = co_await current_async();
    const auto selected_production = head.is("if") || head.is("switch")
        ? SyntaxProduction::SelectionStatement
        : head.is("while") || head.is("do") || head.is("for")
            ? SyntaxProduction::IterationStatement
        : head.is("return") || head.is("goto") || head.is("break") ||
          head.is("continue") ? SyntaxProduction::JumpStatement
        : head.is("case") || head.is("default") ? SyntaxProduction::LabeledStatement
        : SyntaxProduction::ExpressionStatement;
    ProductionScope selected(*this, selected_production);
    const auto location = (co_await current_async()).location;
    auto statement = std::make_unique<Statement>();
    statement->location = location;
    if ((co_await consume_async(";"))) { statement->kind = Statement::Kind::Empty; co_return statement; }
    if ((co_await consume_async("return"))) {
        statement->kind = Statement::Kind::Return;
        statement->attributes = std::move(attributes);
        if (!(co_await current_async()).is(";")) statement->expression = co_await parse_expression_async();
        (co_await expect_async(";"));
        co_return statement;
    }
    if ((co_await consume_async("goto"))) {
        statement->kind = Statement::Kind::Goto;
        statement->expression = co_await parse_assignment_async();
        (co_await expect_async(";"));
        co_return statement;
    }
    if ((co_await consume_async("if"))) {
        statement->kind = Statement::Kind::If;
        (co_await expect_async("(")); statement->condition = co_await parse_expression_async(); (co_await expect_async(")"));
        statement->first = co_await parse_statement_async();
        if ((co_await consume_async("else"))) statement->second = co_await parse_statement_async();
        co_return statement;
    }
    if ((co_await consume_async("switch"))) {
        statement->kind = Statement::Kind::Switch;
        (co_await expect_async("(")); statement->condition = co_await parse_expression_async(); (co_await expect_async(")"));
        ++switch_depth_;
        switch_default_seen_.push_back(false);
        statement->first = co_await parse_statement_async();
        switch_default_seen_.pop_back();
        --switch_depth_;
        co_return statement;
    }
    if ((co_await consume_async("while"))) {
        statement->kind = Statement::Kind::While;
        (co_await expect_async("(")); statement->condition = co_await parse_expression_async(); (co_await expect_async(")"));
        statement->first = co_await parse_statement_async();
        co_return statement;
    }
    if ((co_await consume_async("do"))) {
        statement->kind = Statement::Kind::DoWhile;
        statement->first = co_await parse_statement_async();
        (co_await expect_async("while", "after 'do' body"));
        (co_await expect_async("(")); statement->condition = co_await parse_expression_async(); (co_await expect_async(")"));
        (co_await expect_async(";"));
        co_return statement;
    }
    if ((co_await consume_async("for"))) {
        statement->kind = Statement::Kind::For;
        TokenIdentity source_end;
        if (parsing_public_fragment_ || token_origin(location).context)
            if (const auto end = co_await bounded_statement_end_async(index_ - 1); end && *end > index_)
                source_end = token_origin(tokens_[*end - 1].location).identity;
        ScopePlacementFrame placement(*this, (co_await current_async()).location, source_end);
        const auto saved_value_rebindings = value_rebindings_;
        const auto saved_tag_rebindings = tag_rebindings_;
        const auto saved_alias_rebindings = alias_rebindings_;
        const auto saved_uncertain_count = public_uncertain_binding_depths_.size();
        local_scopes_.emplace_back();
        local_type_scopes_.emplace_back();
        local_tag_scopes_.emplace_back();
        scope_origins_.push_back(token_origin((co_await current_async()).location).identity);
        scope_ends_.push_back(source_end);
        (co_await expect_async("("));
        {
            ProductionScope initializer(*this, SyntaxProduction::ForInitializer);
            if ((co_await current_async()).is(";")) {
                statement->first = std::make_unique<Statement>();
                statement->first->kind = Statement::Kind::Empty;
                statement->first->location = location;
            } else if ((co_await local_declaration_start_async())) {
                statement->first = co_await parse_local_declaration_async({}, false,
                    SyntaxProduction::DeclarationWithoutFinalSemicolon);
            } else if ((co_await current_async()).is("[[")) {
                ProductionScope declaration(*this, SyntaxProduction::DeclarationWithoutFinalSemicolon);
                auto initializer_attributes = co_await parse_attributes_async();
                if ((co_await local_declaration_start_async())) {
                    statement->first = co_await parse_local_declaration_async(
                        std::move(initializer_attributes), false, SyntaxProduction::None);
                } else {
                    for (const auto& attribute : initializer_attributes) {
                        diagnostics_.error(attribute.location,
                            "attribute '" + attribute.name + "' is not valid on a for initializer");
                    }
                    statement->first = std::make_unique<Statement>();
                    statement->first->kind = Statement::Kind::Expression;
                    statement->first->location = (co_await current_async()).location;
                    statement->first->expression = co_await parse_expression_async();
                }
            } else {
                statement->first = std::make_unique<Statement>();
                statement->first->kind = Statement::Kind::Expression;
                statement->first->location = (co_await current_async()).location;
                statement->first->expression = co_await parse_expression_async();
            }
        }
        (co_await expect_async(";"));
        if (!(co_await current_async()).is(";")) statement->condition = co_await parse_expression_async();
        (co_await expect_async(";"));
        if (!(co_await current_async()).is(")")) statement->increment = co_await parse_expression_async();
        (co_await expect_async(")"));
        statement->second = co_await parse_statement_async();
        local_scopes_.pop_back();
        local_type_scopes_.pop_back();
        local_tag_scopes_.pop_back();
        value_rebindings_ = saved_value_rebindings;
        tag_rebindings_ = saved_tag_rebindings;
        alias_rebindings_ = saved_alias_rebindings;
        scope_origins_.pop_back();
        scope_ends_.pop_back();
        public_uncertain_binding_depths_.resize(saved_uncertain_count);
        co_return statement;
    }
    if ((co_await consume_async("break"))) {
        statement->kind = Statement::Kind::Break; (co_await expect_async(";")); co_return statement;
    }
    if ((co_await consume_async("continue"))) {
        statement->kind = Statement::Kind::Continue; (co_await expect_async(";")); co_return statement;
    }
    if ((co_await consume_async("case"))) {
        statement->kind = Statement::Kind::Case;
        if (!parsing_public_fragment_ && switch_depth_ == 0) {
            (co_await error_here_async("case label is not inside a switch"));
        }
        statement->expression = co_await parse_constant_expression_async();
        (co_await expect_async(":"));
        if (!(co_await current_async()).is("}")) statement->first = co_await parse_statement_async();
        co_return statement;
    }
    if ((co_await consume_async("default"))) {
        statement->kind = Statement::Kind::Default;
        // Public recognition checks the labeled-statement grammar, not its
        // eventual control-flow owner. A library can discard or wrap the node;
        // only surviving source contributes defaults to a receiving switch.
        if (!parsing_public_fragment_) {
            if (switch_depth_ == 0) {
                (co_await error_here_async("default label is not inside a switch"));
            } else if (!switch_default_seen_.empty() &&
                       switch_default_seen_.back()) {
                (co_await error_here_async("duplicate default label in switch"));
            } else if (!switch_default_seen_.empty()) {
                switch_default_seen_.back() = true;
            }
        }
        (co_await expect_async(":"));
        if (!(co_await current_async()).is("}")) statement->first = co_await parse_statement_async();
        co_return statement;
    }
    statement->kind = Statement::Kind::Expression;
    statement->expression = co_await parse_expression_async();
    (co_await expect_async(";"));
    co_return statement;
}

std::unique_ptr<Statement> Parser::parse_unattributed_statement(
    std::vector<Attribute> attributes, bool block_item) {
    return parse_unattributed_statement_async(std::move(attributes), block_item).run();
}

Parser::StatementTask Parser::parse_global_label_statement_async(
    std::vector<Attribute> attributes) {
    ProductionScope production(*this, SyntaxProduction::LabeledStatement);
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Label;
    statement->location = (co_await current_async()).location;
    statement->global_label = true;
    statement->attributes = std::move(attributes);
    (co_await consume_async("global"));
    (co_await consume_async("label"));
    const auto name = (co_await consume_kind_async(TokenKind::Identifier));
    if (!name) {
        (co_await error_here_async("expected label name after 'global label'"));
    } else {
        statement->label_name = identifier_binding_name(*name);
        statement->label_location = name->location;
        remember_label_binding(*statement, index_ - 1);
        statement->label_fresh = token_origin(name->location).fresh;
    }
    (co_await expect_async(":", "after global label name"));
    if (parsing_public_fragment_ ||
        ((co_await current_async()).kind != TokenKind::End && !(co_await current_async()).is("}")))
        statement->first = co_await parse_statement_async();
    co_return statement;
}

std::unique_ptr<Statement> Parser::parse_global_label_statement(
    std::vector<Attribute> attributes) {
    return parse_global_label_statement_async(std::move(attributes)).run();
}

int Parser::precedence(std::string_view operation) {
    if (operation == "||") return 1;
    if (operation == "&&") return 2;
    if (operation == "|") return 3;
    if (operation == "^") return 4;
    if (operation == "&") return 5;
    if (operation == "==" || operation == "!=") return 6;
    if (operation == "<" || operation == ">" || operation == "<=" || operation == ">=") return 7;
    if (operation == "<<" || operation == ">>") return 8;
    if (operation == "+" || operation == "-") return 9;
    if (operation == "*" || operation == "/" || operation == "%") return 10;
    return -1;
}

std::unique_ptr<Expr> Parser::parse_expression(std::unique_ptr<Expr> seed) {
    return parse_expression_async(std::move(seed)).run();
}

std::unique_ptr<Expr> Parser::parse_constant_expression() {
    return parse_constant_expression_async().run();
}

std::unique_ptr<Expr> Parser::parse_initializer() {
    return parse_initializer_async().run();
}

std::unique_ptr<Expr> Parser::parse_assignment(std::unique_ptr<Expr> seed) {
    return parse_assignment_async(std::move(seed)).run();
}

std::unique_ptr<Expr> Parser::parse_conditional(std::unique_ptr<Expr> seed) {
    return parse_conditional_async(std::move(seed)).run();
}

std::unique_ptr<Expr> Parser::parse_binary(int minimum_precedence, std::unique_ptr<Expr> seed) {
    return parse_binary_async(minimum_precedence, std::move(seed)).run();
}

std::unique_ptr<Expr> Parser::parse_cast() {
    return parse_cast_async().run();
}

std::unique_ptr<Expr> Parser::parse_unary() {
    return parse_unary_async().run();
}

std::unique_ptr<Expr> Parser::parse_postfix(std::unique_ptr<Expr> seed) {
    return parse_postfix_async(std::move(seed)).run();
}

std::unique_ptr<Expr> Parser::parse_quote() {
    return parse_quote_async().run();
}

std::unique_ptr<Expr> Parser::parse_primary() {
    return parse_primary_async().run();
}

std::unique_ptr<Expr> Parser::parse_expression_replacement() {
    return parse_expression_replacement_async().run();
}

Parser::ExpressionTask Parser::parse_expression_async(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::Expression);
    co_return co_await parse_assignment_async(std::move(seed));
}

Parser::ExpressionTask Parser::parse_constant_expression_async() {
    ProductionScope production(*this, SyntaxProduction::ConstantExpression);
    co_return co_await parse_conditional_async();
}

Parser::ExpressionTask Parser::parse_initializer_async() {
    ProductionScope production(*this, SyntaxProduction::Initializer);
    if (!(co_await current_async()).is("{")) co_return co_await parse_assignment_async();
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::AggregateInitializer;
    result->location = (co_await current_async()).location;
    (co_await consume_async("{"));
    while (!(co_await current_async()).is("}") && (co_await current_async()).kind != TokenKind::End) {
        {
            ProductionScope initializer_entry(*this, SyntaxProduction::InitializerEntry);
            Expr::InitializerEntry entry;
            entry.location = (co_await current_async()).location;
            while ((co_await current_async()).is(".") || (co_await current_async()).is("[")) {
                ProductionScope designator_production(*this, SyntaxProduction::Designator);
                Expr::InitializerDesignator designator;
                designator.location = (co_await current_async()).location;
                if ((co_await consume_async("."))) {
                    designator.kind = Expr::InitializerDesignator::Kind::Member;
                    const auto member = (co_await consume_kind_async(TokenKind::Identifier));
                    if (!member) (co_await error_here_async("expected member name after '.' in initializer"));
                    else {
                        designator.member = member->text;
                        designator.member_fresh = token_origin(member->location).fresh;
                    }
                } else {
                    (co_await consume_async("["));
                    designator.kind = Expr::InitializerDesignator::Kind::Index;
                    designator.index = co_await parse_constant_expression_async();
                    (co_await expect_async("]", "after initializer designator"));
                }
                entry.designators.push_back(std::move(designator));
            }
            if (!entry.designators.empty()) (co_await consume_async("="));
            entry.value = co_await parse_initializer_async();
            result->initializer_entries.push_back(std::move(entry));
        }
        if (!(co_await consume_async(","))) break;
        if ((co_await current_async()).is("}")) break;
    }
    (co_await expect_async("}", "after initializer list"));
    co_return result;
}

Parser::ExpressionTask Parser::parse_assignment_async(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::AssignmentExpression);
    auto left = co_await parse_conditional_async(std::move(seed));
    if ((co_await current_async()).is("=") || (co_await current_async()).is("+=") || (co_await current_async()).is("-=") ||
        (co_await current_async()).is("*=") || (co_await current_async()).is("/=") || (co_await current_async()).is("%=") ||
        (co_await current_async()).is("<<=") || (co_await current_async()).is(">>=") ||
        (co_await current_async()).is("&=") || (co_await current_async()).is("^=") ||
        (co_await current_async()).is("|=")) {
        const auto operation = (co_await current_async());
        {
            ProductionScope assignment_operator(*this, SyntaxProduction::AssignmentOperator);
            ++index_;
        }
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Assign;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = co_await parse_assignment_async();
        co_return result;
    }
    co_return left;
}

Parser::ExpressionTask Parser::parse_conditional_async(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::ConditionalExpression);
    auto condition = co_await parse_binary_async(1, std::move(seed));
    if (!(co_await consume_async("?"))) co_return condition;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Conditional;
    result->location = condition->location;
    result->left = std::move(condition);
    result->right = co_await parse_expression_async();
    (co_await expect_async(":"));
    result->third = co_await parse_conditional_async();
    co_return result;
}

Parser::ExpressionTask Parser::parse_binary_async(int minimum_precedence, std::unique_ptr<Expr> seed) {
    static constexpr SyntaxProduction productions[] = {
        SyntaxProduction::None, SyntaxProduction::LogicalOrExpression,
        SyntaxProduction::LogicalAndExpression, SyntaxProduction::InclusiveOrExpression,
        SyntaxProduction::ExclusiveOrExpression, SyntaxProduction::AndExpression,
        SyntaxProduction::EqualityExpression, SyntaxProduction::RelationalExpression,
        SyntaxProduction::ShiftExpression, SyntaxProduction::AdditiveExpression,
        SyntaxProduction::MultiplicativeExpression};
    ProductionScope production(*this, productions[minimum_precedence]);
    std::unique_ptr<Expr> left;
    if (minimum_precedence == 10) {
        if (seed) left = co_await parse_postfix_async(std::move(seed));
        else left = co_await parse_cast_async();
    } else left = co_await parse_binary_async(minimum_precedence + 1, std::move(seed));
    for (;;) {
        // A token macro between operands may supply any operator or suffix.
        // Recognition cannot choose its precedence before the owner runs.
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        if (parsing_generic_argument_ &&
            ((co_await current_async()).is(">") || (co_await current_async()).is(">>"))) break;
        const int current_precedence = precedence((co_await current_async()).text);
        if (current_precedence != minimum_precedence) break;
        const auto operation = (co_await current_async()); ++index_;
        std::unique_ptr<Expr> right;
        if (minimum_precedence == 10) right = co_await parse_cast_async();
        else right = co_await parse_binary_async(minimum_precedence + 1);
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Binary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = std::move(right);
        left = std::move(result);
    }
    co_return left;
}

Parser::ExpressionTask Parser::parse_cast_async() {
    ProductionScope production(*this, SyntaxProduction::CastExpression);
    if ((co_await current_async()).is("(")) {
        const auto saved = index_;
        ++index_;
        (void)(co_await current_async());
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        const bool begins_type = (co_await type_start_async(TypeProbe::ExpressionAlternative));
        index_ = saved;
        if (begins_type) {
            const auto location = (co_await current_async()).location;
            (co_await consume_async("("));
            TypePtr type;
            std::optional<std::string> name;
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                type = co_await parse_type_async();
                type = co_await parse_declarator_async(std::move(type), name, DeclaratorContext::TypeName);
            }
            if (name) {
                diagnostics_.error(location,
                                   "a cast type name cannot declare an object");
            }
            (co_await expect_async(")", "after cast type"));
            auto result = std::make_unique<Expr>();
            result->kind = Expr::Kind::Cast;
            result->location = location;
            result->type = std::move(type);
            result->left = co_await parse_cast_async();
            co_return result;
        }
    }
    co_return co_await parse_unary_async();
}

Parser::ExpressionTask Parser::parse_unary_async() {
    ProductionScope production(*this, SyntaxProduction::UnaryExpression);
    if ((co_await current_async()).is("sizeof")) {
        const auto location = (co_await current_async()).location;
        (co_await consume_async("sizeof"));
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Sizeof;
        result->location = location;
        if ((co_await current_async()).is("(")) {
            const auto saved = index_;
            ++index_;
            (void)(co_await current_async());
            if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
            const bool begins_type = (co_await type_start_async(TypeProbe::ExpressionAlternative));
            index_ = saved;
            if (begins_type) {
                (co_await consume_async("("));
                std::optional<std::string> name;
                {
                    ProductionScope type_name(*this, SyntaxProduction::TypeName);
                    result->type = co_await parse_type_async();
                    result->type = co_await parse_declarator_async(std::move(result->type), name, DeclaratorContext::TypeName);
                }
                if (name) {
                    diagnostics_.error(
                        location,
                        "a sizeof type name cannot declare an object");
                }
                (co_await expect_async(")", "after sizeof type"));
                co_return result;
            }
        }
        result->left = co_await parse_unary_async();
        co_return result;
    }
    if ((co_await current_async()).is("+") || (co_await current_async()).is("-") || (co_await current_async()).is("!") ||
        (co_await current_async()).is("~") || (co_await current_async()).is("&") || (co_await current_async()).is("*") ||
        (co_await current_async()).is("++") || (co_await current_async()).is("--")) {
        const auto operation = (co_await current_async()); ++index_;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Unary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        if (operation.is("++") || operation.is("--")) result->left = co_await parse_unary_async();
        else result->left = co_await parse_cast_async();
        co_return result;
    }
    co_return co_await parse_postfix_async();
}

Parser::ExpressionTask Parser::parse_postfix_async(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::PostfixExpression);
    std::unique_ptr<Expr> expression;
    if (seed) expression = std::move(seed);
    else expression = co_await parse_primary_async();
    for (;;) {
        const bool explicit_generic = (co_await current_async()).is("::") &&
                                      (co_await current_async(1)).is("<");
        const bool inferred_generic = (co_await current_async()).is("<") && expression &&
                                      known_generic_name(*expression);
        if (explicit_generic || inferred_generic) {
            ProductionScope generic_arguments(*this, SyntaxProduction::GenericArguments);
            // Generic arguments describe the selected designator, not a
            // transparent grouping node around it. This also preserves them
            // when the instance is used as an address rather than called here.
            auto* application = expression.get();
            while (application->kind == Expr::Kind::Parenthesized && application->left)
                application = application->left.get();
            application->generic_visible_at_call = known_generic_name(*application);
            if (application->kind != Expr::Kind::Name && !parsing_public_fragment_ && !preparing_header_)
                diagnostics_.error(application->location,
                    "generic arguments require a named function designator");
            index_ += explicit_generic ? 2 : 1;
            if ((co_await consume_async(">"))) {
                (co_await error_here_async("a generic argument list cannot be empty"));
            } else {
                for (;;) {
                    {
                        ProductionScope generic_argument(*this, SyntaxProduction::GenericArgument);
                        Expr::GenericArgument argument;
                        (void)(co_await current_async());
                        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
                        auto probe = TypeProbe::GenericArgumentAlternative;
                        if (preparing_header_) {
                            const auto* parameters = known_generic_parameters(*expression);
                            const auto ordinal = application->generic_arguments.size();
                            if (parameters && ordinal < parameters->size() &&
                                (*parameters)[ordinal] == GenericParameterKind::Type)
                                probe = TypeProbe::GenericTypeArgumentAlternative;
                        }
                        if ((co_await type_start_async(probe))) {
                            ProductionScope type_name(*this, SyntaxProduction::TypeName);
                            argument.type = co_await parse_type_async();
                            std::optional<std::string> declared;
                            argument.type = co_await parse_declarator_async(std::move(argument.type), declared, DeclaratorContext::TypeName);
                            if (declared) (co_await error_here_async("a generic type argument cannot declare an object"));
                        } else {
                            const bool previous = parsing_generic_argument_;
                            parsing_generic_argument_ = true;
                            argument.value = co_await parse_constant_expression_async();
                            parsing_generic_argument_ = previous;
                        }
                        application->generic_arguments.push_back(std::move(argument));
                    }
                    if (!(co_await consume_async(","))) break;
                }
                if (!(co_await consume_generic_close_async()))
                    (co_await error_here_async("expected '>' to close generic argument list"));
            }
            if (application->kind == Expr::Kind::Name && (co_await current_async()).is("::") &&
                (co_await current_async(1)).kind == TokenKind::Identifier) {
                index_ += 1;
                application->instance_label = std::string((co_await current_async()).text);
                ++index_;
            }
            continue;
        }
        if ((co_await consume_async("("))) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto call = std::make_unique<Expr>();
            call->kind = Expr::Kind::Call;
            call->location = expression->location;
            // The operand tree already preserves callee grouping. Redundant
            // typed-AST parentheses must not turn a direct function designator
            // into an indirect call or hide an explicit generic application.
            // Public production events/splice children remain untouched.
            while (expression->kind == Expr::Kind::Parenthesized && expression->left) {
                auto wrapper = std::move(expression);
                expression = std::move(wrapper->left);
                expression->generic_arguments.insert(expression->generic_arguments.end(),
                    std::make_move_iterator(wrapper->generic_arguments.begin()),
                    std::make_move_iterator(wrapper->generic_arguments.end()));
            }
            if (expression->kind == Expr::Kind::Name && (expression->text == "$::meta::parse" ||
                expression->text == "$::meta::token" || expression->text == "$::meta::group"))
                call->translation_context = syntax_context(expression->location);
            call->generic_visible_at_call =
                expression->kind == Expr::Kind::Name &&
                known_generic_name(*expression);
            if (expression->kind == Expr::Kind::Name)
                call->generic_arguments = std::move(expression->generic_arguments);
            call->left = std::move(expression);
            {
                ProductionScope argument_list(*this, SyntaxProduction::ArgumentList);
                if (!(co_await current_async()).is(")")) {
                    do { call->arguments.push_back(co_await parse_assignment_async()); } while ((co_await consume_async(",")));
                }
            }
            (co_await expect_async(")"));
            parsing_generic_argument_ = previous;
            expression = std::move(call);
            continue;
        }
        if ((co_await consume_async("["))) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto index = std::make_unique<Expr>();
            index->kind = Expr::Kind::Binary;
            index->location = expression->location;
            index->text = "index";
            index->left = std::move(expression);
            index->right = co_await parse_expression_async();
            (co_await expect_async("]"));
            parsing_generic_argument_ = previous;
            expression = std::move(index);
            continue;
        }
        if ((co_await current_async()).is(".") || (co_await current_async()).is("->")) {
            const bool through_pointer = (co_await consume_async("->"));
            if (!through_pointer) (co_await consume_async("."));
            const auto member_name = (co_await consume_kind_async(TokenKind::Identifier));
            if (!member_name) {
                (co_await error_here_async("expected member name after '" +
                           std::string(through_pointer ? "->" : ".") + "'"));
                break;
            }
            auto member = std::make_unique<Expr>();
            member->kind = Expr::Kind::Binary;
            member->location = member_name->location;
            member->text = through_pointer ? "pointer_member" : "member";
            member->left = std::move(expression);
            member->right = std::make_unique<Expr>();
            member->right->kind = Expr::Kind::Name;
            member->right->location = member_name->location;
            member->right->text = member_name->text;
            expression = std::move(member);
            continue;
        }
        if ((co_await current_async()).is("++") || (co_await current_async()).is("--")) {
            const auto operation = (co_await current_async());
            ++index_;
            auto update = std::make_unique<Expr>();
            update->kind = Expr::Kind::Unary;
            update->location = operation.location;
            update->text = operation.is("++") ? "post++" : "post--";
            update->left = std::move(expression);
            expression = std::move(update);
            continue;
        }
        break;
    }
    co_return expression;
}

std::unique_ptr<FunctionDecl> Parser::parse_expansion_declaration(
    std::shared_ptr<const SyntaxContext> definition_context, Program& declarations) {
    return parse_expansion_declaration_async(std::move(definition_context), declarations).run();
}

EvaluationTask<std::unique_ptr<FunctionDecl>> Parser::parse_expansion_declaration_async(
    std::shared_ptr<const SyntaxContext> definition_context, Program& declarations) {
    const auto errors = diagnostics_.errors();
    if (definition_context && definition_context->parse_environment)
        restore_environment(*definition_context->parse_environment, *definition_context);
    if (definition_context) {
        active_namespace_ = definition_context->name_space;
        active_imports_ = definition_context->imports;
        active_import_declarations_ = definition_context->import_declarations;
    }
    parsing_procedural_body_ = true;
    parsing_expansion_declaration_ = true;
    co_await parse_external_impl_async(declarations, active_namespace_);
    if ((co_await current_async()).kind != TokenKind::End)
        (co_await error_here_async("unexpected tokens after expansion-function declaration"));
    drain_pending_tags(declarations);
    if (diagnostics_.errors() != errors) co_return {};
    if (declarations.functions.size() != 1 || !declarations.objects.empty()) {
        diagnostics_.error(tokens_.front().location,
            "expansion role requires one function definition");
        co_return {};
    }
    auto function = std::move(declarations.functions.front());
    declarations.functions.clear();
    for (auto& assertion : static_assertions_)
        function->deferred_static_assertions.push_back(std::move(assertion));
    static_assertions_.clear();
    co_return function;
}

Parser::ExpressionTask Parser::parse_quote_async() {
    struct RawTokens {
        unsigned& depth;
        explicit RawTokens(unsigned& value) : depth(value) { ++depth; }
        ~RawTokens() { --depth; }
    } raw{raw_token_depth_};
    ProductionScope quote(*this, SyntaxProduction::QuoteExpression);
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Quote;
    result->location = (co_await current_async()).location;
    result->translation_context = syntax_context(result->location);
    ++index_;
    if (!(co_await expect_async("{", "after $::quote"))) co_return result;
    const auto content_first = index_;
    TokenSequence literal;
    std::vector<std::string_view> closers{"}"};
    while ((co_await current_async()).kind != TokenKind::End) {
        if (!parsing_public_fragment_ && (co_await current_async()).is("$::unquote")) {
            result->quote_fragments.push_back(std::move(literal));
            literal.clear();
            ++index_;
            if (!(co_await expect_async("(", "after $::unquote"))) co_return result;
            result->arguments.push_back(co_await parse_expression_async());
            if (!(co_await expect_async(")", "after $::unquote expression"))) co_return result;
            continue;
        }
        if ((co_await current_async()).is("(")) closers.push_back(")");
        else if ((co_await current_async()).is("[")) closers.push_back("]");
        else if ((co_await current_async()).is("[[")) closers.push_back("]]");
        else if ((co_await current_async()).is("{")) closers.push_back("}");
        else if ((co_await current_async()).is(")") || (co_await current_async()).is("]") ||
                 (co_await current_async()).is("]]") || (co_await current_async()).is("}")) {
            if (closers.empty() || closers.back() != (co_await current_async()).text) {
                (co_await error_here_async("$::quote requires balanced token groups"));
                co_return result;
            }
            closers.pop_back();
            if (closers.empty()) {
                result->quote_fragments.push_back(std::move(literal));
                record_balanced_sequence(content_first, index_, SyntaxProduction::BalancedTokens);
                ++index_;
                co_return result;
            }
        }
        // A name bound to a block-scope typedef or tag of this function keeps
        // denoting that type wherever the generated code is placed, together
        // with the block-scope definitions that the type needs.
        MetaToken token((co_await current_async()));
        if (token.kind == TokenKind::Identifier && !parsing_public_fragment_) {
            const bool tag = !literal.empty() && (literal.back().text == "struct" ||
                literal.back().text == "union" || literal.back().text == "enum");
            if (tag && !token.origin.tag_binding) {
                if (const auto type = block_tag_type(token.text)) {
                    auto binding = std::make_shared<TagBinding>(TagBinding{
                        type->kind != Type::Kind::Record ? TagBinding::Kind::Enumeration
                            : type->is_union ? TagBinding::Kind::Union : TagBinding::Kind::Structure,
                        TagBinding::Role::Use, token.text, type->nominal_key(), type->builtin,
                        type->captured_tag_errors});
                    binding->carried = block_definitions(type);
                    token.origin.tag_binding = std::move(binding);
                }
            } else if (!tag && !token.origin.alias_binding) {
                if (auto alias = block_type_alias(token.text)) {
                    auto carried = block_definitions(alias->instantiate());
                    token.origin.alias_binding = std::make_shared<const AliasBinding>(AliasBinding{
                        AliasBinding::Role::Use, token.text, std::move(alias), nullptr, std::move(carried)});
                }
            }
        }
        literal.push_back(std::move(token));
        ++index_;
    }
    (co_await error_here_async("unterminated $::quote token tree"));
    co_return result;
}

Parser::ExpressionTask Parser::parse_primary_async() {
    ProductionScope production(*this, SyntaxProduction::PrimaryExpression);
    // A selected custom owner gets its still-opaque input first. Ordinary
    // names may instead be assembled textually before primary classification.
    if (!macro_start() && !active_syntax(false)) (co_await normalize_qualified_name_async());
    if ((co_await current_async()).is("syntax") &&
        (replacement_ || token_origin((co_await current_async()).location).context)) {
        (co_await error_here_async("expansion output cannot introduce syntax registration"));
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Integer;
        result->location = (co_await current_async()).location;
        result->text = "0";
        ++index_;
        co_return result;
    }
    if (syntax_ && (macro_start() || active_syntax(false))) {
        if (probing_header_type_)
            throw HeaderProbeInvocation{public_input_indices_[index_], macro_start()};
        if (parsing_public_fragment_) {
            auto placeholder = std::make_unique<Expr>();
            // The public node owns the operand's grammar, not its value. An
            // empty semantic grouping is deliberately nonconstant: a dummy
            // integer would turn an opaque array bound into a literal zero.
            placeholder->kind = Expr::Kind::Parenthesized;
            placeholder->location = (co_await current_async()).location;
            if (!(co_await parse_opaque_invocation_async(SyntaxKind::Expression))) {
                (co_await error_here_async("could not recognize a bounded opaque expression invocation"));
                if (index_ < tokens_.size() && (co_await current_async()).kind != TokenKind::End) ++index_;
            }
            co_return placeholder;
        }
        co_return co_await parse_expression_replacement_async();
    }
    const auto item = (co_await current_async());
    const auto item_index = index_;
    if (item.kind == TokenKind::PreparedFragment) {
        ++index_;
        auto grouped = std::make_unique<Expr>();
        grouped->kind = Expr::Kind::Parenthesized;
        grouped->location = item.location;
        if (!item.prepared || item.prepared->category != SyntaxParseCategory::Expression) {
            diagnostics_.error(item.location, "prepared type is not an expression");
        } else if (parsing_public_fragment_) {
            if (!probing_header_type_)
                diagnostics_.error(item.location, "internal prepared expression cannot enter a public capture");
        } else if (auto child = prepared_fragment_parser(item)) {
            const auto prior_tags = child->tag_state();
            child->prepare_tag_destination(*this);
            grouped->left = co_await child->parse_assignment_async();
            if ((co_await child->current_async()).kind != TokenKind::End)
                (co_await child->error_here_async("prepared expression must contain one assignment expression"));
            // A structured splice's context is for lookup, not for replacing
            // the destination's active syntax/import/type environment.
            if (!item.prepared->context) adopt_replacement(*child);
            else if (!transfer_spliced_tags(*child, prior_tags, item.location)) grouped->left.reset();
        }
        if (!grouped->left) {
            grouped->left = std::make_unique<Expr>();
            grouped->left->kind = Expr::Kind::Integer;
            grouped->left->text = "0";
            grouped->left->location = item.location;
        }
        co_return grouped;
    }
    if (item.kind == TokenKind::StructuredSplice) {
        ++index_;
        auto invalid = [&] {
            auto result = std::make_unique<Expr>();
            result->kind = parsing_public_fragment_ ? Expr::Kind::Parenthesized : Expr::Kind::Integer;
            result->location = item.location;
            result->text = "0";
            return result;
        };
        if (!item.splice || !syntax_expression_node(*item.splice)) {
            diagnostics_.error(item.location,
                "structured syntax splice requires an expression node at expression position");
            co_return invalid();
        }
        if (!syntax_ || !item.splice->context ||
            !item.splice->context->parse_environment) {
            diagnostics_.error(item.location,
                "structured syntax splice has no retained parse environment");
            co_return invalid();
        }
        if (recording_public_tree_ && production.event < production_events_.size()) {
            auto wrapper = std::make_shared<SyntaxNode>();
            wrapper->kind = SyntaxNode::Kind::Core;
            wrapper->production = SyntaxProduction::PrimaryExpression;
            wrapper->structured_splice = true;
            wrapper->children.push_back(item.splice);
            wrapper->span = item.splice->span;
            wrapper->context = item.splice->context;
            production_events_[production.event].opaque = std::move(wrapper);
        }
        // Public recognition must not reparse or execute the fragment's
        // nested invocations. Its validated public root already proves this
        // operand's category; the owner will materialize surviving output.
        if (parsing_public_fragment_) co_return invalid();
        auto output = syntax_->execution()->materialize_node(
            *item.splice, item.location, SyntaxParseCategory::Expression);
        if (!output) co_return invalid();
        auto child = replacement_parser(std::move(*output));
        // A deferred expression needs declarations from preceding expansions
        // in its original captured block, not same-spelled destination locals.
        child->retaining_shared_specifiers_ = false;
        if (item.splice->kind == SyntaxNode::Kind::Deferred)
            child->restore_deferred_environment(
                *item.splice->context->parse_environment, *item.splice->context,
                deferred_position(*item.splice));
        else
            child->restore_environment(*item.splice->context->parse_environment,
                                       *item.splice->context);
        child->parsing_public_fragment_ = parsing_public_fragment_;
        child->recording_public_tree_ = parsing_public_fragment_;
        const auto prior_tags = child->tag_state();
        child->prepare_tag_destination(*this);
        auto parsed = co_await child->parse_assignment_async();
        if ((co_await child->current_async()).kind != TokenKind::End)
            (co_await child->error_here_async("structured expression splice must contain one complete expression"));
        if (preparing_header_ || retaining_shared_specifiers_)
            retain_prepared_fragment(item_index, {child->specifier_input_tokens(0, child->tokens_.size()), item.location},
                SyntaxParseCategory::Expression, item.splice->context,
                item.splice->kind == SyntaxNode::Kind::Deferred, deferred_position(*item.splice));
        if (!preparing_header_ && !transfer_spliced_tags(*child, prior_tags, item.location)) co_return invalid();
        if (!parsed) co_return invalid();
        auto grouped = std::make_unique<Expr>();
        grouped->kind = Expr::Kind::Parenthesized;
        grouped->location = item.location;
        grouped->left = std::move(parsed);
        co_return grouped;
    }
    if ((co_await current_async()).is("$::quote")) co_return co_await parse_quote_async();
    if ((co_await current_async()).is("$::embed")) {
        ProductionScope embed(*this, SyntaxProduction::EmbedExpression);
        ++index_;
        (co_await expect_async("(", "after $::embed"));
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Call;
        result->location = item.location;
        result->left = std::make_unique<Expr>();
        result->left->kind = Expr::Kind::Name;
        result->left->location = item.location;
        result->left->text = std::string(item.text);
        const auto path = (co_await consume_kind_async(TokenKind::String));
        if (!path) {
            (co_await error_here_async("$::embed requires one string-literal path"));
        } else {
            auto argument = std::make_unique<Expr>();
            argument->kind = Expr::Kind::String;
            argument->location = path->location;
            argument->text = std::string(path->text);
            if (const auto decoded = decode_string_literal(path->text))
                argument->string_value = *decoded;
            else
                diagnostics_.error(path->location, "invalid UTF-8 string literal");
            result->arguments.push_back(std::move(argument));
        }
        (co_await expect_async(")", "after embedded asset path"));
        co_return result;
    }
    if ((co_await current_async()).is("$::unquote"))
        (co_await error_here_async("$::unquote is only valid inside $::quote"));
    if ((co_await current_async()).is("$::atomic_is_lock_free") && (co_await current_async(1)).is("(")) {
        index_ += 2;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Call;
        result->location = item.location;
        result->left = std::make_unique<Expr>();
        result->left->kind = Expr::Kind::Name;
        result->left->location = item.location;
        result->left->text = std::string(item.text);
        const bool previous = parsing_generic_argument_;
        parsing_generic_argument_ = false;
        if ((co_await type_start_async(TypeProbe::ExpressionAlternative))) {
            ProductionScope type_name(*this, SyntaxProduction::TypeName);
            result->type = co_await parse_type_async();
            std::optional<std::string> name;
            result->type = co_await parse_declarator_async(std::move(result->type), name, DeclaratorContext::TypeName);
            if (name) diagnostics_.error(item.location, "an atomic query type name cannot declare an object");
        } else {
            ProductionScope arguments(*this, SyntaxProduction::ArgumentList);
            if (!(co_await current_async()).is(")")) {
                do { result->arguments.push_back(co_await parse_assignment_async()); } while ((co_await consume_async(",")));
            }
        }
        (co_await expect_async(")", "after atomic query operand"));
        parsing_generic_argument_ = previous;
        co_return result;
    }
    if ((co_await current_async()).is("$::alignof") && (co_await current_async(1)).is("(")) {
        index_ += 2;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Alignof;
        result->location = item.location;
        if ((co_await type_start_async(TypeProbe::ExpressionAlternative))) {
            ProductionScope type_name(*this, SyntaxProduction::TypeName);
            result->type = co_await parse_type_async();
            std::optional<std::string> name;
            result->type = co_await parse_declarator_async(std::move(result->type), name, DeclaratorContext::TypeName);
            if (name) {
                diagnostics_.error(
                    item.location,
                    "an alignof type name cannot declare an object");
            }
        } else {
            result->left = co_await parse_expression_async();
        }
        (co_await expect_async(")", "after alignof operand"));
        co_return result;
    }
    if ((co_await consume_async("("))) {
        const bool previous = parsing_generic_argument_;
        parsing_generic_argument_ = false;
        auto inner = co_await parse_expression_async();
        (co_await expect_async(")"));
        parsing_generic_argument_ = previous;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Parenthesized;
        result->location = item.location;
        result->left = std::move(inner);
        co_return result;
    }
    auto result = std::make_unique<Expr>();
    const auto atom = item.kind == TokenKind::Identifier ? SyntaxProduction::None
        : item.kind == TokenKind::BuiltinName ? SyntaxProduction::BuiltinName
        : item.kind == TokenKind::Integer || item.kind == TokenKind::Floating ||
          item.kind == TokenKind::String || item.kind == TokenKind::Character
            ? SyntaxProduction::Literal : SyntaxProduction::None;
    ProductionScope atom_production(*this, atom);
    result->location = item.location;
    result->text = std::string(item.text);
    if (item.kind == TokenKind::Integer) result->kind = Expr::Kind::Integer;
    else if (item.kind == TokenKind::Floating) result->kind = Expr::Kind::Floating;
    else if (item.kind == TokenKind::String) {
        result->kind = Expr::Kind::String;
        while ((co_await current_async()).kind == TokenKind::String) {
            const auto decoded = decode_string_literal((co_await current_async()).text);
            if (!decoded) {
                diagnostics_.error((co_await current_async()).location,
                                   "invalid UTF-8 string literal");
            } else {
                result->string_value += *decoded;
            }
            ++index_;
        }
        co_return result;
    }
    else if (item.kind == TokenKind::Character) result->kind = Expr::Kind::Character;
    else if (item.kind == TokenKind::Identifier || item.kind == TokenKind::BuiltinName) {
        result->kind = Expr::Kind::Name;
        if (item.kind == TokenKind::Identifier) {
            if (auto name = (co_await parse_qualified_name_async())) result->text = *name;
            const auto origin = token_origin(item.location);
            auto context = std::make_shared<NameLookupContext>();
            context->name_space = origin.context ? origin.context->name_space : active_namespace_;
            context->last_component_location = tokens_[index_ - 1].location;
            const auto retained = effective_context(item.location);
            context->imports = retained ? retained->imports : active_imports_;
            const auto& last = tokens_[index_ - 1];
            const auto last_origin = token_origin(last.location);
            context->label_binding = last.label_binding;
            if (context->label_binding.kind == LabelBinding::Kind::Unknown &&
                !context->label_binding.scope) context->label_binding = last_origin.label_binding;
            if (context->label_binding.kind == LabelBinding::Kind::Definition)
                context->label_binding.kind = LabelBinding::Kind::Reference;
            // Qualification changes the owner lookup, not a copied final
            // component's identity. Fresh qualified references have no local
            // label dependency: their owner may be a different function.
            if (result->text.find("::") == std::string::npos) {
                if (!context->label_binding.scope && origin.context && origin.context->parse_environment)
                    context->label_binding.scope = origin.context->parse_environment->function_scope;
                if (!context->label_binding.scope) context->label_binding.scope = function_scope_;
            }
            context->value_binding_inherited =
                item.value_binding.kind != ValueBinding::Kind::Unknown ||
                origin.value_binding.kind != ValueBinding::Kind::Unknown;
            context->value_binding = item.value_binding.kind != ValueBinding::Kind::Unknown
                ? item.value_binding : origin.value_binding;
            context->fragment_lookup = item.fragment_lookup ? item.fragment_lookup : origin.fragment_lookup;
            if (context->fragment_lookup && context->fragment_lookup->key.spelling != result->text)
                context->fragment_lookup.reset();
            const auto& spelling = item.value_spelling ? item.value_spelling : origin.value_spelling;
            if (spelling && *spelling != result->text) {
                context->value_binding = {};
                context->value_binding_inherited = false;
            }
            if (context->value_binding.kind == ValueBinding::Kind::Unknown) {
                // Do not freeze a lookup that preceding opaque code may change.
                // The enclosing bounded unit must stay deferred instead.
                require_public_name_context(result->text, item.location);
                context->value_binding = retained_value_binding(result->text, item.location);
                if (context->value_binding.kind == ValueBinding::Kind::Unknown) {
                    context->value_binding.kind = ValueBinding::Kind::Nonlocal;
                    if (result->text.find("::") == std::string::npos) {
                        const NameKey key(result->text, item.location);
                        for (auto scope = prototype_scopes_.rbegin(); scope != prototype_scopes_.rend(); ++scope) {
                            if (const auto found = scope->values.find(key); found != scope->values.end()) {
                                context->value_binding = found->second.binding;
                                break;
                            }
                        }
                        if (context->value_binding.kind == ValueBinding::Kind::Nonlocal)
                            for (auto scope = local_scopes_.rbegin(); scope != local_scopes_.rend(); ++scope) {
                                if (const auto found = scope->find(key); found != scope->end()) {
                                    context->value_binding = found->second;
                                    break;
                                }
                            }
                        if (context->value_binding.kind == ValueBinding::Kind::Nonlocal && active_function_)
                            for (const auto& parameter : active_function_->generic_parameters)
                                if (parameter.value_type && NameKey(parameter.name, parameter.location) == key) {
                                    context->value_binding = name_key(parameter).binding;
                                    break;
                                }
                    }
                }
            }
            if (context->value_binding.kind == ValueBinding::Kind::Unknown ||
                context->value_binding.kind == ValueBinding::Kind::Nonlocal) {
                if (!context->fragment_lookup && !context->value_binding_inherited)
                    context->fragment_lookup = fragment_lookup(result->text, item.location);
                if (context->fragment_lookup)
                    if (const auto introduced = fragment_namespace_name(*context->fragment_lookup,
                            FragmentNameDomain::Ordinary);
                        introduced && introduced->value.kind != ValueBinding::Kind::Unknown) {
                        context->value_binding = introduced->value;
                        context->value_binding.kind = ValueBinding::Kind::Nonlocal;
                    }
            }
            if (context->value_binding.kind == ValueBinding::Kind::Nonlocal &&
                !context->value_binding.enumeration && !context->value_binding.placement) {
                const auto binding = nonlocal_value_binding(result->text, item.location, *context);
                if (binding.kind == ValueBinding::Kind::Enumerator || binding.kind == ValueBinding::Kind::Object ||
                    binding.kind == ValueBinding::Kind::Function) {
                    context->value_binding = binding;
                    // Namespace lookup still considers the complete expanded
                    // input. This is only a candidate for a copied declaration's
                    // placement remap, not an early final lookup result.
                    context->value_binding.kind = ValueBinding::Kind::Nonlocal;
                }
            }
            if (context->value_binding.kind == ValueBinding::Kind::Local ||
                context->value_binding.enumeration || context->value_binding.placement) {
                // Nested copies can retain an earlier generation's binding,
                // directly or through a captured scope. Each edge was created
                // by a declaration in this placement, not a destination lookup.
                auto binding = context->value_binding;
                if (binding.kind == ValueBinding::Kind::Nonlocal)
                    binding.kind = binding.enumeration ? ValueBinding::Kind::Enumerator
                        : binding.placement->kind == ValuePlacementIdentity::Kind::Function
                            ? ValueBinding::Kind::Function : ValueBinding::Kind::Object;
                for (std::size_t remaining = value_rebindings_.size(); remaining; --remaining) {
                    const auto replacement = value_rebindings_.find(binding);
                    if (replacement == value_rebindings_.end()) break;
                    context->value_binding = binding = replacement->second;
                }
            }
            if (context->value_binding.kind == ValueBinding::Kind::Local ||
                context->value_binding.kind == ValueBinding::Kind::Enumerator)
                context->kind = NameLookupContext::Kind::Local;
            // This belongs to the private token provenance. Public trees still
            // expose syntax only, while projection preserves an exact use.
            if (!preparing_header_) {
                remember_specifier_input(item_index);
                tokens_[item_index].value_binding = context->value_binding;
                tokens_[item_index].fragment_lookup = context->fragment_lookup;
                tokens_[item_index].value_spelling = spelling && *spelling == result->text
                    ? spelling : std::make_shared<const std::string>(result->text);
                tokens_[index_ - 1].label_binding = context->label_binding;
            }
            result->name_context = std::move(context);
            co_return result;
        }
    } else {
        (co_await error_here_async("expected expression"));
        result->text = "0";
        result->kind = Expr::Kind::Integer;
    }
    ++index_;
    co_return result;
}

} // namespace cross
