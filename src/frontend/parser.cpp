// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"
#include "frontend/name.hpp"
#include "frontend/procedural.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <sstream>
#include <utility>

namespace cross {
namespace {

std::optional<BuiltinType> builtin_kind(std::string_view spelling) {
    static constexpr std::pair<std::string_view, BuiltinType> types[] = {
        {"void", BuiltinType::Void}, {"bool", BuiltinType::Bool},
        {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
        {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
        {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
        {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
        {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
        {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
        {"f32", BuiltinType::F32}, {"f64", BuiltinType::F64},
        {"f80", BuiltinType::F80}, {"f128", BuiltinType::F128},
        {"fptr", BuiltinType::Fptr},
        {"label", BuiltinType::Label},
    };
    for (const auto& [name, kind] : types) if (spelling == name) return kind;
    return std::nullopt;
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

std::optional<std::int64_t> constant_value(const Expr& expression) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return constant_value(*expression.left);
    }
    if (expression.kind == Expr::Kind::Integer) {
        auto text = expression.text;
        for (const auto suffix : {"iptr", "uptr", "i128", "u128", "i64", "u64", "i32", "u32",
                                  "i16", "u16", "i8", "u8"}) {
            const std::string_view suffix_view(suffix);
            if (text.size() > suffix_view.size() && text.ends_with(suffix_view)) {
                text.resize(text.size() - suffix_view.size());
                break;
            }
        }
        text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
        unsigned base = 10;
        std::string_view digits(text);
        if (digits.starts_with("0x") || digits.starts_with("0X")) { base = 16; digits.remove_prefix(2); }
        else if (digits.starts_with("0b") || digits.starts_with("0B")) { base = 2; digits.remove_prefix(2); }
        std::uint64_t value{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value,
                                            static_cast<int>(base));
        if (result.ec != std::errc{}) return std::nullopt;
        return static_cast<std::int64_t>(value);
    }
    if (expression.kind == Expr::Kind::Unary) {
        const auto operand = constant_value(*expression.left);
        if (!operand) return std::nullopt;
        if (expression.text == "+") return *operand;
        if (expression.text == "-") return -*operand;
        if (expression.text == "!") return *operand == 0;
        if (expression.text == "~") return ~*operand;
        return std::nullopt;
    }
    if (expression.kind == Expr::Kind::Conditional) {
        const auto condition = constant_value(*expression.left);
        if (!condition) return std::nullopt;
        return constant_value(*(*condition ? expression.right : expression.third));
    }
    if (expression.kind != Expr::Kind::Binary) return std::nullopt;
    const auto left = constant_value(*expression.left);
    const auto right = constant_value(*expression.right);
    if (!left || !right) return std::nullopt;
    if (expression.text == "+") return *left + *right;
    if (expression.text == "-") return *left - *right;
    if (expression.text == "*") return *left * *right;
    if (expression.text == "/") return *right == 0 ? std::nullopt : std::optional(*left / *right);
    if (expression.text == "%") return *right == 0 ? std::nullopt : std::optional(*left % *right);
    if (expression.text == "<<") return *left << *right;
    if (expression.text == ">>") return *left >> *right;
    if (expression.text == "<") return *left < *right;
    if (expression.text == "<=") return *left <= *right;
    if (expression.text == ">") return *left > *right;
    if (expression.text == ">=") return *left >= *right;
    if (expression.text == "==") return *left == *right;
    if (expression.text == "!=") return *left != *right;
    if (expression.text == "&") return *left & *right;
    if (expression.text == "^") return *left ^ *right;
    if (expression.text == "|") return *left | *right;
    if (expression.text == "&&") return *left != 0 && *right != 0;
    if (expression.text == "||") return *left != 0 || *right != 0;
    return std::nullopt;
}

} // namespace

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics,
               std::shared_ptr<SyntaxExecution> execution,
               unsigned address_bits)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics),
      address_bits_(address_bits) {
    if (execution) syntax_.emplace(std::move(execution));
}

const Token& Parser::current(std::size_t lookahead) const {
    if (public_tree_failed_) return tokens_.back();
    const auto position = index_ + lookahead;
    return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
}

const Token& Parser::current(std::size_t lookahead) {
    // Procedural macros replace tokens, not grammar subtrees. Only expose
    // expanded tokens when the core parser reaches them; raw captures and
    // speculative public-tree recognition must keep their input opaque.
    if (!raw_token_depth_ && probing_header_type_ && macro_start())
        throw HeaderProbeInvocation{public_input_indices_[index_], true};
    if (!raw_token_depth_ && !parsing_public_fragment_ && !public_tree_failed_)
        expand_inline_macro_fragments();
    return std::as_const(*this).current(lookahead);
}

bool Parser::consume(std::string_view spelling) {
    if (!current().is(spelling)) return false;
    ++index_;
    return true;
}

std::optional<Token> Parser::consume_kind(TokenKind kind) {
    if (current().kind != kind) return {};
    // Later token expansion may reallocate the cursor's vector. Consumers
    // retain a value, never a pointer into that mutable sequence.
    return tokens_[index_++];
}

bool Parser::expect(std::string_view spelling, std::string_view context) {
    if (consume(spelling)) return true;
    std::string message = "expected '" + std::string(spelling) + "'";
    if (!context.empty()) message += " " + std::string(context);
    error_here(std::move(message));
    return false;
}

void Parser::error_here(std::string message) {
    diagnostics_.error(current().location, message);
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

std::vector<SyntaxActivation> Parser::parse_syntax_entries(std::string_view end) {
    std::vector<SyntaxActivation> entries;
    do {
        const auto location = current().location;
        if (current().kind != TokenKind::Identifier || is_reserved_identifier(current().text)) {
            error_here("expected nonreserved syntax entity name");
            break;
        }
        auto name = parse_qualified_name();
        if (!name) break;
        SyntaxActivation entry{std::move(*name), {}, location};
        if (consume("as")) {
            if (current().kind != TokenKind::Identifier || is_reserved_identifier(current().text)) {
                error_here("expected nonreserved syntax prefix alias");
                break;
            }
            entry.alias = current().text;
            ++index_;
        }
        entries.push_back(std::move(entry));
    } while (consume(","));
    expect(end, "after syntax activation list");
    return entries;
}

bool Parser::parse_syntax_registration(Program* program) {
    if (!current().is("syntax")) return false;
    const auto location = current().location;
    if (replacement_ || token_origin(location).context) {
        error_here("expansion output cannot introduce syntax registration");
        ++index_;
        synchronize_external();
        return true;
    }
    if (!syntax_) return false;
    if (current(1).kind == TokenKind::Identifier && current(2).is(":")) {
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
            error_here("syntax definitions are allowed only at item position");
            if (declaration_end) index_ = *declaration_end;
            else { ++index_; synchronize_external(); }
        } else if (!syntax_->declare(tokens_, index_, active_namespace_, diagnostics_)) {
            if (declaration_end) index_ = *declaration_end;
            else synchronize_external();
        }
        return true;
    }
    ++index_;
    const bool region = consume("(");
    const auto errors = diagnostics_.errors();
    auto entries = parse_syntax_entries(region ? ")" : ";");
    if (region) {
        if (!program) {
            diagnostics_.error(location, "syntax regions are allowed only at item position");
            synchronize_external();
            return true;
        }
        syntax_->push_scope();
    }
    if (diagnostics_.errors() == errors)
        (void)syntax_->activate(entries, active_namespace_, diagnostics_);
    if (region) {
        const auto saved_imports = active_imports_;
        const auto saved_scope_imports = current_scope_imports_;
        current_scope_imports_ = 0;
        const auto region_namespace = active_namespace_;
        if (expect("{", "after syntax region")) {
            while (!current().is("}") && current().kind != TokenKind::End) {
                const auto before = index_;
                parse_external(*program, region_namespace);
                if (before == index_) ++index_;
            }
            expect("}", "after syntax region items");
        }
        active_imports_ = saved_imports;
        current_scope_imports_ = saved_scope_imports;
        syntax_->pop_scope();
    }
    return true;
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

std::optional<SyntaxExecution::Output> Parser::expand_at_position(bool item) {
    auto& expansion_diagnostics = expansion_diagnostics_ ? *expansion_diagnostics_ : diagnostics_;
    const auto location = current().location;
    const auto origin = token_origin(location);
    const auto& name_space = origin.context ? origin.context->name_space : active_namespace_;
    const auto& imports = origin.context ? origin.context->imports : active_imports_;
    const auto bindings = origin.context ? origin.context->syntax_bindings : syntax_->bindings();
    const auto context = syntax_context(location);
    if (!context) { ++index_; synchronize_external(); return {}; }
    auto execution = syntax_->execution();
    if (macro_start()) {
        auto name = parse_qualified_name();
        ++index_; // '!'
        const auto opening = current().text;
        std::vector<std::string_view> stack;
        TokenSequence input;
        ++index_;
        stack.push_back(opening == "(" ? ")" : opening == "[" ? "]" : "}");
        while (current().kind != TokenKind::End && !stack.empty()) {
            if (!execution->work(current().location)) return {};
            const auto spelling = current().text;
            if (spelling == "(" || spelling == "[" || spelling == "[[" || spelling == "{")
                stack.push_back(spelling == "(" ? ")" : spelling == "[" ? "]" : spelling == "[[" ? "]]" : "}");
            else if (spelling == ")" || spelling == "]" || spelling == "]]" || spelling == "}") {
                if (stack.back() != spelling) {
                    expansion_diagnostics.error(current().location,
                        "mismatched procedural macro token-tree delimiter");
                    ++index_;
                    return {};
                }
                stack.pop_back();
            }
            if (!stack.empty()) input.emplace_back(current());
            ++index_;
        }
        if (!stack.empty()) {
            expansion_diagnostics.error(location, "unterminated procedural macro token tree");
            return {};
        }
        const auto search_imports = origin.context
            ? std::vector<std::vector<std::string>>{imports} : syntax_->imports();
        const auto function = execution->find_function(*name, name_space, search_imports, false);
        if (!function) {
            expansion_diagnostics.error(location, "procedural macro is not visible: '" + *name + "'");
            return {};
        }
        return execution->expand(*function, std::move(input), {}, location, name_space, imports, bindings,
                                 context->parse_environment);
    }
    const auto* definition = active_syntax(item);
    if (!definition) return {};
    const auto matched = syntax_->match(*definition, tokens_, index_, expansion_diagnostics,
        [&](SyntaxPatternElement::Kind kind, std::size_t first) {
            return parse_syntax_fragment(kind, first);
        });
    if (!matched) { ++index_; synchronize_external(); return {}; }
    index_ = matched->end;
    return execution->expand(matched->expander, {}, matched->value, location, name_space, imports, bindings,
                             context->parse_environment, definition);
}

void Parser::expand_inline_macro_fragments() {
    if (raw_token_depth_) return;
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
        if (!execution->begin_replacement(current().location)) {
            discard_failed_invocation();
            return;
        }
        struct End {
            SyntaxExecution& execution;
            ~End() { execution.end_replacement(); }
        } end{*execution};
        auto output = expand_at_position(false);
        if (!output) {
            discard_failed_invocation();
            return;
        }
        if (output->tokens.empty() || output->tokens.back().kind != TokenKind::End) {
            diagnostics_.error(output->location,
                "procedural macro produced no token boundary");
            discard_failed_invocation();
            return;
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
    child->preparing_header_ = preparing_header_;
    child->expansion_diagnostics_ = expansion_diagnostics_;
    child->header_bindings_ = header_bindings_;
    child->public_header_uncertain_names_ = public_header_uncertain_names_;
    child->replacement_ = true;
    child->active_imports_ = active_imports_;
    child->current_scope_imports_ = current_scope_imports_;
    child->active_generic_types_ = active_generic_types_;
    child->known_generic_functions_ = known_generic_functions_;
    child->known_ordinary_values_ = known_ordinary_values_;
    child->local_scopes_ = local_scopes_;
    child->local_type_scopes_ = local_type_scopes_;
    child->scope_origins_ = scope_origins_;
    child->scope_events_ = scope_events_;
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

void Parser::prepare_header() {
    if (!syntax_ || preparing_header_ || parsing_public_fragment_ ||
        current().is("namespace") || current().is("using") ||
        current().is("$::static_assert")) return;
    // Only discovery-visible procedural fragments can change header-wide
    // bindings. An ordinary syntax expression alone must not make otherwise
    // settled captures opaque. This probe shares discovery's boundaries and
    // never executes a fragment or looks inside an expression owner's input.
    bool pending_fragments = false;
    (void)preview_generic_types(&pending_fragments);
    if (!pending_fragments) return;
    const auto first = index_;
    const auto location = tokens_[first].location;
    std::ostringstream ignored;
    Diagnostics provisional(ignored);
    auto child = replacement_parser({std::move(tokens_), location}, &provisional);
    child->index_ = first;
    child->preparing_header_ = true;
    child->parsing_public_function_header_ = true;
    child->expansion_diagnostics_ = &diagnostics_;
    auto bindings = std::make_shared<SyntaxHeaderBindings>();
    child->header_bindings_ = bindings;
    Program header;
    try {
        child->parse_external(header, active_namespace_);
    } catch (const HeaderPrepared&) {
        // Object initializers and function bodies are not header work.
    }
    bindings->finish(header.functions.empty() ? std::vector<GenericParameter>{}
        : std::move(header.functions.front()->generic_parameters), *syntax_->execution(), location);
    header_bindings_ = std::move(bindings);
    tokens_ = std::move(child->tokens_);
    index_ = first;
}

bool Parser::probe_header_type(bool generic_argument) {
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
    const auto probe = [&](bool type) -> Probe {
        if (!execution->begin_fragment(current().location, tokens_.size()))
            return {Outcome::Uncertain};
        struct End { SyntaxExecution& execution; ~End() { execution.end_fragment(); } } end{*execution};
        std::ostringstream ignored;
        Diagnostics provisional(ignored);
        auto child = replacement_parser({tokens_, current().location}, &provisional);
        child->index_ = index_;
        child->parsing_public_fragment_ = true;
        child->probing_header_type_ = true;
        child->parsing_generic_argument_ = generic_argument;
        for (std::size_t at = 0; at < tokens_.size(); ++at)
            child->public_input_indices_.push_back(at);
        try {
            if (type) {
                auto parsed = child->parse_type();
                std::optional<std::string> name;
                if (parsed) parsed = child->parse_declarator(
                    std::move(parsed), name, DeclaratorContext::TypeName);
                if (!parsed || name) return {};
            } else {
                (void)child->parse_assignment();
            }
            const bool at_end = generic_argument
                ? child->current().is(",") || child->current().is(">") || child->current().is(">>")
                : child->current().is(")");
            return {at_end && provisional.errors() == 0 ? Outcome::Complete : Outcome::Rejected};
        } catch (const HeaderProbeInvocation& invocation) {
            return {provisional.errors() == 0 ? Outcome::Invocation : Outcome::Rejected, invocation};
        } catch (const HeaderProbeRejected&) {
            return {};
        } catch (const DeferredNameRecognition&) {
            return {Outcome::Uncertain};
        }
    };
    for (;;) {
        const auto type = probe(true);
        if (type.outcome == Outcome::Complete) return true;
        if (type.outcome != Outcome::Invocation) return false;
        const auto expression = probe(false);
        // Only execute an invocation proved to be reached under either
        // interpretation, or when the expression grammar already failed.
        // In particular, never expose a macro hidden in a custom owner's input
        // merely because the tentative type parser could reach its tokens.
        if (expression.outcome != Outcome::Rejected &&
            (expression.outcome != Outcome::Invocation ||
             expression.invocation != type.invocation)) return false;
        const auto first = index_;
        index_ = type.invocation.position;
        if (type.invocation.macro) expand_inline_macro_fragments();
        else (void)parse_expression_replacement();
        index_ = first;
        // Expanded tokens may change the grammatical alternative. Re-probe;
        // neither speculative AST nor speculative name bindings are retained.
    }
}

void Parser::retain_prepared_fragment(std::size_t first, SyntaxExecution::Output output,
    SyntaxParseCategory category, std::shared_ptr<const SyntaxContext> context, bool deferred,
    SourceLocation original_position) {
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
        if (token.prepared->deferred)
            child->restore_deferred_environment(*token.prepared->context->parse_environment,
                *token.prepared->context, token.prepared->original_position);
        else child->restore_environment(*token.prepared->context->parse_environment,
                                        *token.prepared->context);
    }
    return child;
}

Parser::ProductionScope::ProductionScope(Parser& parser, SyntaxProduction production)
    : parser(parser), event(parser.begin_production(production)) {}

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
        (syntax_ && !syntax_->execution()->work(current().location))) {
        if (syntax_) syntax_->execution()->tree_limit_error(current().location);
        error_here("public syntax tree depth, work, or storage budget exceeded");
        public_tree_failed_ = true;
        return std::numeric_limits<std::size_t>::max();
    }
    const auto event = production_events_.size();
    // Reuse environments inside a core unit. Subsequent units capture the
    // lexical state after preceding declarations/imports have changed it.
    const bool boundary = production_stack_.empty() ||
        production == SyntaxProduction::Statement || production == SyntaxProduction::Declaration ||
        production == SyntaxProduction::FunctionHeader || production == SyntaxProduction::TypeName;
    auto context = boundary ? syntax_context(current().location)
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
                      : source.end - 1].location).span};
    node->context = source.context;
    const auto token_node = [&](std::size_t at) -> std::shared_ptr<const SyntaxNode> {
        auto leaf = std::make_shared<SyntaxNode>();
        leaf->kind = SyntaxNode::Kind::Token;
        leaf->tokens.emplace_back(tokens_[at]);
        if (!leaf->tokens.front().origin.context) leaf->tokens.front().origin.context = source.context;
        leaf->span = {leaf->tokens.front().origin.span, leaf->tokens.front().origin.span};
        leaf->context = leaf->tokens.front().origin.context;
        return leaf;
    };
    auto cursor = source.first;
    for (const auto child : source.children) {
        const auto& range = production_events_[child];
        if (range.first < cursor || range.first > range.end || range.end > source.end)
            return {};
        while (cursor < range.first) node->children.push_back(token_node(cursor++));
        auto built = public_node(child);
        if (!built) return {};
        node->children.push_back(std::move(built));
        cursor = range.end;
    }
    while (cursor < source.end) node->children.push_back(token_node(cursor++));
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
            if (syntax_ && !syntax_->execution()->work(current().location)) {
                public_tree_failed_ = true;
                return;
            }
            const auto text = current().text;
            if (!close.empty() && text == close) return;
            if (text == "(" || text == "[" || text == "{") {
                ProductionScope tree(*this, SyntaxProduction::BalancedTokenTree);
                ++index_;
                const auto expected = text == "(" ? ")" : text == "[" ? "]" : "}";
                sequence(SyntaxProduction::BalancedTokens, expected);
                if (!public_tree_failed_ && index_ < end && current().is(expected)) ++index_;
                else if (!public_tree_failed_) {
                    error_here("unterminated balanced attribute token tree");
                    return;
                }
            } else if (text == ")" || text == "]" || text == "}") {
                error_here("mismatched balanced attribute token-tree delimiter");
                return;
            } else ++index_;
        }
    };
    sequence(root_production, {});
    index_ = saved_index;
}

std::optional<SyntaxParsedFragment> Parser::parse_syntax_fragment(
    SyntaxPatternElement::Kind kind, std::size_t first) const {
    using K = SyntaxPatternElement::Kind;
    if (first >= tokens_.size() || tokens_[first].kind == TokenKind::End) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    if (execution && !execution->begin_fragment(tokens_[first].location, tokens_.size())) return {};
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
        child->active_function_ = &function_context;
    }
    child->index_ = first;
    child->parsing_public_fragment_ = true;
    child->recording_public_tree_ = true;
    child->public_input_indices_.reserve(tokens_.size());
    for (std::size_t at = 0; at < tokens_.size(); ++at)
        child->public_input_indices_.push_back(at);
    const auto origin = token_origin(tokens_[first].location);
    if (origin.context) {
        child->active_namespace_ = origin.context->name_space;
        child->active_imports_ = origin.context->imports;
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
    try {
        const auto header_position = child->function_header_splice_position();
        const bool header_splice = header_position.has_value();
        if (kind == K::FunctionHeader && header_position == child->index_ &&
            !child->current(1).is("[[")) {
            ProductionScope header(*child, SyntaxProduction::FunctionHeader);
            const auto item = child->current();
            ++child->index_;
            if (header.event < child->production_events_.size())
                child->production_events_[header.event].opaque = item.splice;
        } else if ((kind == K::Declaration || kind == K::FunctionDeclaration ||
             kind == K::FunctionDefinition) &&
            child->current().kind == TokenKind::StructuredSplice && !header_splice) {
            ProductionScope declaration(*child, kind == K::FunctionDefinition
                ? SyntaxProduction::FunctionDefinition : SyntaxProduction::Declaration);
            const auto item = child->current();
            ++child->index_;
            if (!item.splice ||
                (kind == K::FunctionDefinition
                    ? !syntax_function_definition_node(*item.splice)
                    : !syntax_declaration_node(*item.splice))) return {};
            if (kind == K::FunctionDeclaration &&
                (item.splice->kind != SyntaxNode::Kind::Deferred ||
                 item.splice->deferred_category != SyntaxParseCategory::FunctionDeclaration)) {
                // The public declaration production also describes objects
                // and typedefs. Classify its direct-function shape without
                // executing nested macros or changing the retained node.
                if (!execution || !item.splice->context ||
                    !item.splice->context->parse_environment) return {};
                auto materialized = execution->materialize_node(*item.splice, item.location,
                    item.splice->kind == SyntaxNode::Kind::Deferred
                        ? item.splice->deferred_category : SyntaxParseCategory::None);
                if (!materialized) return {};
                auto validator = child->replacement_parser(std::move(*materialized), &local);
                validator->restore_environment(*item.splice->context->parse_environment,
                                                *item.splice->context);
                const auto classified = validator->parse_syntax_fragment(kind, 0);
                if (!classified || classified->end + 1 != validator->tokens_.size()) return {};
            }
            if (declaration.event < child->production_events_.size())
                child->production_events_[declaration.event].opaque = item.splice;
        } else if (kind == K::Expr) {
            (void)child->parse_assignment();
        } else if (kind == K::Type) {
            // A macro may introduce the type specifiers themselves. Its
            // output belongs after owner expansion, so retain the bounded
            // type fragment without executing or prematurely classifying it.
            const auto type_first = child->index_;
            while (child->current().is("const") || child->current().is("volatile") ||
                   child->current().is("restrict") || child->current().is("[[")) {
                if (child->current().is("[[")) {
                    const auto group_end = child->bounded_group_end(child->index_);
                    if (!group_end) return {};
                    child->index_ = *group_end;
                } else ++child->index_;
            }
            const bool opaque_type = child->macro_start();
            child->index_ = type_first;
            if (opaque_type) throw DeferredNameRecognition{};
            ProductionScope scope(*child, SyntaxProduction::TypeName);
            if (!child->type_start()) return {};
            auto type = child->parse_type();
            std::optional<std::string> name;
            (void)child->parse_declarator(std::move(type), name, DeclaratorContext::TypeName);
            if (name) return {};
        } else if (kind == K::Statement) {
            (void)child->parse_statement();
        } else if (kind == K::Declaration && !child->local_scopes_.empty()) {
            // The public declaration category has the same root at either
            // scope, but a block admits register/stack and local typedefs.
            if (!child->local_declaration_start()) return {};
            (void)child->parse_local_declaration({}, true,
                                                 SyntaxProduction::Declaration);
        } else if (kind == K::Declaration || kind == K::FunctionHeader ||
                   kind == K::FunctionDeclaration || kind == K::FunctionDefinition) {
            // These categories exclude namespaces, registration, and invocations
            // standing in place of the direct declaration/header itself.
            if (child->current().is("namespace") || child->current().is("using") ||
                child->current().is("syntax") || child->macro_start() ||
                child->active_syntax(true)) return {};
            child->parsing_public_function_header_ = kind == K::FunctionHeader;
            Program parsed;
            child->parse_external(parsed, child->active_namespace_);
            const bool direct_function = parsed.functions.size() == 1 &&
                parsed.objects.empty() && parsed.records.empty() &&
                parsed.enumerations.empty() && parsed.global_labels.empty();
            if (kind == K::FunctionHeader) {
                if (!direct_function || (!child->current().is(";") &&
                                         !child->current().is("{") &&
                                         child->current().kind != TokenKind::End)) return {};
            } else if (kind == K::FunctionDeclaration) {
                if (!direct_function || parsed.functions.front()->body) return {};
            } else if (kind == K::FunctionDefinition) {
                if (!direct_function || !parsed.functions.front()->body) return {};
            } else if (std::any_of(parsed.functions.begin(), parsed.functions.end(),
                       [](const auto& function) { return function->body != nullptr; })) return {};
        } else {
            return {};
        }
        if (child->public_deferred_header_)
            deferred_root = child->deferred_node(first, child->index_, deferred_slot,
                                                 deferred_category, initial_context);
    } catch (const DeferredNameRecognition&) {
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
        case K::Declaration:
            fragment_end = child->bounded_statement_end(first);
            break;
        default: return {};
        }
        if (!fragment_end) return {};
        deferred_root = child->deferred_node(first, *fragment_end, deferred_slot,
                                             deferred_category, initial_context);
        child->index_ = *fragment_end;
    }
    if (child->public_tree_failed_) {
        if (!execution) diagnostics_.error(tokens_[first].location,
            "public syntax tree depth, work, or storage budget exceeded");
        return {};
    }
    if (local.errors() != 0 || child->index_ <= first ||
        (!deferred_root && child->production_events_.empty())) return {};
    if (kind == K::Expr || kind == K::Type) {
        const auto& next = child->current();
        if (next.kind != TokenKind::End && !next.is(";") && !next.is(",") &&
            !next.is(")") && !next.is("]") && !next.is("]]") &&
            !next.is("}")) return {};
    }
    auto node = deferred_root ? std::move(deferred_root) : child->public_node(0);
    if (!node) return {};
    std::string shape_error;
    std::uint64_t validation_work{};
    SyntaxTreeValidationError failure{};
    const auto valid = syntax_validate_node(*node, shape_error,
        execution ? execution->limits() : EvaluationLimits{}, &validation_work, &failure);
    if (execution && !execution->work(tokens_[first].location, validation_work)) return {};
    if (!valid) {
        if (failure == SyntaxTreeValidationError::DepthLimit ||
            failure == SyntaxTreeValidationError::WorkLimit) {
            if (execution) execution->tree_limit_error(tokens_[first].location);
            else diagnostics_.error(tokens_[first].location, shape_error);
        }
        return {};
    }
    return SyntaxParsedFragment{child->public_input_indices_[child->index_], std::move(node)};
}

std::shared_ptr<const SyntaxNode> Parser::parse_syntax_tokens(
    SyntaxParseCategory category, std::vector<Token> input) const {
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
    case C::None: return {};
    default: return {};
    }
    if (input.size() < 2 || input.back().kind != TokenKind::End ||
        std::any_of(input.begin(), input.end() - 1,
                    [](const Token& token) { return token.kind == TokenKind::End; })) return {};
    const auto end = input.size() - 1;
    const auto location = input.front().location;
    auto child = replacement_parser({std::move(input), location});
    const auto fragment = child->parse_syntax_fragment(kind, 0);
    if (!fragment || fragment->end != end) return {};
    return fragment->node;
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
            if (!local_scopes_[depth - 1].contains(key) &&
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

std::optional<std::size_t> Parser::bounded_statement_end(std::size_t first, unsigned depth) {
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    if (first >= tokens_.size() || tokens_[first].kind == TokenKind::End) return {};
    if (depth >= limits.depth) {
        public_tree_failed_ = true;
        if (execution) execution->tree_limit_error(tokens_[first].location);
        return {};
    }
    if (execution && !execution->work(tokens_[first].location)) {
        public_tree_failed_ = true;
        return {};
    }
    auto at = first;
    while (tokens_[at].is("[[")) {
        const auto end = bounded_group_end(at);
        if (!end || *end >= tokens_.size()) return {};
        at = *end;
    }
    if (tokens_[at].is("{")) return bounded_group_end(at);
    const auto head = tokens_[at].text;
    if (head == "if" || head == "switch" || head == "while" || head == "for") {
        if (at + 1 >= tokens_.size() || !tokens_[at + 1].is("(")) return {};
        const auto condition_end = bounded_group_end(at + 1);
        if (!condition_end) return {};
        auto end = bounded_statement_end(*condition_end, depth + 1);
        if (end && head == "if" && *end < tokens_.size() && tokens_[*end].is("else"))
            end = bounded_statement_end(*end + 1, depth + 1);
        return end;
    }
    if (head == "do") {
        const auto body_end = bounded_statement_end(at + 1, depth + 1);
        if (!body_end || *body_end + 1 >= tokens_.size() ||
            !tokens_[*body_end].is("while") || !tokens_[*body_end + 1].is("(")) return {};
        const auto condition_end = bounded_group_end(*body_end + 1);
        if (!condition_end || *condition_end >= tokens_.size() ||
            !tokens_[*condition_end].is(";")) return {};
        return *condition_end + 1;
    }
    if (head == "label" && at + 2 < tokens_.size() &&
        tokens_[at + 1].kind == TokenKind::Identifier && tokens_[at + 2].is(":"))
        return bounded_statement_end(at + 3, depth + 1);
    if (tokens_[at].kind == TokenKind::Identifier && at + 1 < tokens_.size() &&
        tokens_[at + 1].is(":")) return bounded_statement_end(at + 2, depth + 1);
    if (head == "case") {
        unsigned conditional_depth{};
        for (++at; at < tokens_.size(); ++at) {
            if (execution && !execution->work(tokens_[at].location)) {
                public_tree_failed_ = true;
                return {};
            }
            const auto text = tokens_[at].text;
            if (tokens_[at].kind == TokenKind::End || text == ";" || text == "}") return {};
            if (text == "(" || text == "[" || text == "[[" || text == "{") {
                const auto end = bounded_group_end(at);
                if (!end) return {};
                at = *end - 1;
            } else if (text == "?") ++conditional_depth;
            else if (text == ":") {
                if (conditional_depth) --conditional_depth;
                else return bounded_statement_end(at + 1, depth + 1);
            }
        }
        return {};
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
        if (!parse_opaque_invocation(SyntaxKind::Statement)) return {};
        return index_;
    }
    if (macro_start()) {
        if (!parse_opaque_invocation(SyntaxKind::Statement)) return {};
        if (!opaque_statement_has_expression_continuation()) return index_;
    }
    for (; at < tokens_.size(); ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        const auto text = tokens_[at].text;
        if (tokens_[at].kind == TokenKind::End || text == "}" || text == ")" ||
            text == "]" || text == "]]" || text == "else" ||
            (at != unit_first && (text == "if" || text == "for" || text == "while" ||
                             text == "do" || text == "switch" || text == "return"))) return {};
        if (text == ";") return at + 1;
        if (text == "(" || text == "[" || text == "[[" || text == "{") {
            const auto end = bounded_group_end(at);
            if (!end) return {};
            at = *end - 1;
        }
    }
    return {};
}

std::shared_ptr<const SyntaxContext> Parser::public_fragment_context(std::size_t first) const {
    return syntax_context(tokens_[first].location);
}

std::shared_ptr<const SyntaxNode> Parser::deferred_node(
    std::size_t first, std::size_t end, SyntaxProduction slot,
    SyntaxParseCategory category, std::shared_ptr<const SyntaxContext> context) {
    if (first >= end || end > tokens_.size()) return {};
    const auto execution = syntax_ ? syntax_->execution() : nullptr;
    const auto limits = execution ? execution->limits() : EvaluationLimits{};
    auto node = std::make_shared<SyntaxNode>();
    node->kind = SyntaxNode::Kind::Deferred;
    node->slot_production = slot;
    node->deferred_category = category;
    node->span = {token_origin(tokens_[first].location).span,
                  token_origin(tokens_[end - 1].location).span};
    node->context = std::move(context);
    std::uint64_t storage = 128;
    for (auto at = first; at < end; ++at) {
        if (execution && !execution->work(tokens_[at].location)) {
            public_tree_failed_ = true;
            return {};
        }
        storage += 128 + tokens_[at].text.size();
        if (storage > std::min(limits.bytes, limits.memory)) {
            public_tree_failed_ = true;
            if (execution) execution->tree_limit_error(tokens_[at].location);
            return {};
        }
        MetaToken token(tokens_[at]);
        if (!token.origin.context) token.origin.context = node->context;
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

std::shared_ptr<const SyntaxNode> Parser::parse_opaque_invocation(SyntaxKind category) {
    if (!syntax_) return {};
    const auto first = index_;
    auto node = std::make_shared<SyntaxNode>();
    node->slot_production = category == SyntaxKind::Expression
        ? SyntaxProduction::PrimaryExpression
        : category == SyntaxKind::Statement
            ? SyntaxProduction::UnattributedStatement
            : SyntaxProduction::Declaration;
    if (macro_start()) {
        node->kind = SyntaxNode::Kind::Macro;
        ++index_;
        while (current().is("::")) index_ += 2;
        expect("!");
        const auto opening = current().text;
        if (opening != "(" && opening != "[" && opening != "{") return {};
        std::vector<std::string_view> closers;
        do {
            if (!syntax_->execution()->work(current().location)) return {};
            const auto text = current().text;
            if (text == "(" || text == "[" || text == "[[" || text == "{") {
                if (closers.size() >= syntax_->execution()->limits().depth) {
                    syntax_->execution()->tree_limit_error(current().location);
                    public_tree_failed_ = true;
                    error_here("opaque macro token-tree depth exceeded");
                    return {};
                }
                closers.push_back(text == "(" ? ")" : text == "[" ? "]" : text == "[[" ? "]]" : "}");
            } else if (text == ")" || text == "]" || text == "]]" || text == "}") {
                if (closers.empty() || closers.back() != text) {
                    error_here("mismatched opaque macro token-tree delimiter");
                    return {};
                }
                closers.pop_back();
            }
            ++index_;
        } while (!closers.empty() && current().kind != TokenKind::End);
        if (!closers.empty()) {
            error_here("unterminated opaque macro token tree");
            return {};
        }
    } else {
        const auto* definition = active_syntax(category == SyntaxKind::Item);
        if (!definition) return {};
        if (definition->kind != category) {
            error_here("opaque syntax invocation has the wrong surrounding category");
            ++index_;
            return {};
        }
        const auto matched = syntax_->match(*definition, tokens_, index_, diagnostics_,
            [&](SyntaxPatternElement::Kind kind, std::size_t begin) {
                return parse_syntax_fragment(kind, begin);
            });
        if (!matched) return {};
        node->kind = SyntaxNode::Kind::Extension;
        node->definition = definition->id;
        node->match = matched->value;
        index_ = matched->end;
    }
    node->span = {token_origin(tokens_[first].location).span,
                  token_origin(tokens_[index_ - 1].location).span};
    node->context = public_fragment_context(first);
    if (!node->context) return {};
    for (auto at = first; at < index_; ++at) {
        MetaToken token(tokens_[at]);
        if (!token.origin.context) token.origin.context = node->context;
        node->tokens.push_back(std::move(token));
    }
    if (recording_public_tree_) {
        const auto event = production_events_.size();
        production_events_.push_back({SyntaxProduction::None, first, index_, {}, node, node->context});
        if (!production_stack_.empty())
            production_events_[production_stack_.back()].children.push_back(event);
    }
    return node;
}

void Parser::adopt_replacement(Parser& child) {
    syntax_ = std::move(child.syntax_);
    active_imports_ = std::move(child.active_imports_);
    current_scope_imports_ = child.current_scope_imports_;
    known_generic_functions_ = std::move(child.known_generic_functions_);
    known_ordinary_values_ = std::move(child.known_ordinary_values_);
    local_scopes_ = std::move(child.local_scopes_);
    local_type_scopes_ = std::move(child.local_type_scopes_);
    scope_origins_ = std::move(child.scope_origins_);
    enum_types_ = std::move(child.enum_types_);
    pending_enumerations_.insert(pending_enumerations_.end(),
        std::make_move_iterator(child.pending_enumerations_.begin()),
        std::make_move_iterator(child.pending_enumerations_.end()));
    pending_records_.insert(pending_records_.end(),
        std::make_move_iterator(child.pending_records_.begin()),
        std::make_move_iterator(child.pending_records_.end()));
    record_types_ = std::move(child.record_types_);
    type_aliases_ = std::move(child.type_aliases_);
    declared_aliases_.insert(declared_aliases_.end(),
        std::make_move_iterator(child.declared_aliases_.begin()),
        std::make_move_iterator(child.declared_aliases_.end()));
    switch_default_seen_ = std::move(child.switch_default_seen_);
    for (auto& assertion : child.static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

bool Parser::transfer_spliced_tags(
    Parser& child, const std::unordered_map<std::string, RecordTag>& prior_records,
    SourceLocation location) {
    const auto previous_errors = diagnostics_.errors();
    for (const auto& [name, tag] : child.record_types_) {
        const auto before = prior_records.find(name);
        if (before != prior_records.end() &&
            before->second.is_union == tag.is_union &&
            before->second.complete == tag.complete) continue;
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
        if (const auto destination = enum_types_.find(enumeration.name);
            destination != enum_types_.end() &&
            destination->second != enumeration.underlying)
            diagnostics_.error(location,
                "spliced enumeration '" + enumeration.name +
                "' conflicts with the destination underlying type");
    }
    if (diagnostics_.errors() != previous_errors) return false;
    for (const auto& [name, tag] : child.record_types_) {
        const auto before = prior_records.find(name);
        if (before != prior_records.end() &&
            before->second.is_union == tag.is_union &&
            before->second.complete == tag.complete) continue;
        auto [destination, inserted] = record_types_.emplace(name, tag);
        if (!inserted && tag.complete) destination->second.complete = true;
    }
    for (auto& record : child.pending_records_)
        pending_records_.push_back(std::move(record));
    for (auto& enumeration : child.pending_enumerations_) {
        enum_types_[enumeration.name] = enumeration.underlying;
        for (const auto& enumerator : enumeration.enumerators)
            known_ordinary_values_.insert(enumerator.name);
        pending_enumerations_.push_back(std::move(enumeration));
    }
    return true;
}

std::unique_ptr<Statement> Parser::parse_statement_replacement() {
    const auto location = current().location;
    const auto* owner = active_syntax(false);
    auto result = std::make_unique<Statement>();
    result->kind = Statement::Kind::Empty;
    result->location = location;
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) {
        if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
        ++index_;
        synchronize_external();
        return result;
    }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = expand_at_position(false);
    if (!output) return result;
    auto child = replacement_parser(std::move(*output));
    // Only custom statement owners use this bounded parser. Procedural
    // token macros are exposed directly in the caller's token stream.
    if (child->current().kind == TokenKind::End) {
        diagnostics_.error(location, "statement expansion must produce exactly one complete statement");
    } else result = child->parse_statement();
    if (child->current().kind != TokenKind::End)
        child->error_here("statement expansion must produce exactly one complete statement");
    adopt_replacement(*child);
    return result;
}

std::unique_ptr<Expr> Parser::parse_expression_replacement() {
    const auto first = index_;
    const auto location = current().location;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Integer;
    result->text = "0";
    result->location = location;
    const auto* owner = active_syntax(false);
    if (owner && owner->kind != SyntaxKind::Expression) {
        error_here("statement syntax is not valid at expression position");
        ++index_;
        return result;
    }
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) {
        if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
        ++index_;
        return result;
    }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = expand_at_position(false);
    if (!output) {
        if (preparing_header_)
            retain_prepared_fragment(first, {{{TokenKind::End, {}, location}}, location},
                                     SyntaxParseCategory::Expression);
        return result;
    }
    auto child = replacement_parser(std::move(*output));
    if (child->current().kind == TokenKind::End)
        diagnostics_.error(location, "expression expansion must produce one assignment expression");
    else result = child->parse_assignment();
    if (child->current().kind != TokenKind::End)
        child->error_here("expression expansion must produce one assignment expression without a semicolon");
    if (preparing_header_) {
        retain_prepared_fragment(first, {std::move(child->tokens_), location},
                                 SyntaxParseCategory::Expression);
    } else adopt_replacement(*child);
    // The parsed root is a subtree, so caller operators cannot reassociate
    // across the expansion boundary (even without textual parentheses).
    auto grouped = std::make_unique<Expr>();
    grouped->kind = Expr::Kind::Parenthesized;
    grouped->location = location;
    grouped->left = std::move(result);
    return grouped;
}

bool Parser::defer_public_header_group(std::optional<std::size_t> end,
                                      const std::function<void()>& parse) {
    if (!allow_public_header_deferral_ || !end) {
        parse();
        return false;
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
        parse();
        return false;
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
        return true;
    }
}

std::vector<Attribute> Parser::parse_attributes(bool one_specifier, AttributeParseMode mode) {
    if (!allow_public_header_deferral_) return parse_attributes_impl(one_specifier, mode);
    std::vector<Attribute> result;
    while (current().is("[[")) {
        std::vector<Attribute> attributes;
        if (defer_public_header_group(bounded_group_end(index_), [&] {
                attributes = parse_attributes_impl(true, mode);
            })) attributes.clear();
        result.insert(result.end(), std::make_move_iterator(attributes.begin()),
                      std::make_move_iterator(attributes.end()));
        if (one_specifier) break;
    }
    return result;
}

std::vector<Attribute> Parser::parse_attributes_impl(bool one_specifier, AttributeParseMode mode) {
    std::vector<Attribute> result;
    while (current().is("[[")) {
        ProductionScope attribute_specifier(*this, SyntaxProduction::AttributeSpecifier);
        consume("[[");
        do {
            ProductionScope attribute_production(*this, SyntaxProduction::Attribute);
            normalize_qualified_name();
            if (current().kind == TokenKind::BuiltinName) {
                diagnostics_.error(
                    current().location,
                    "attributes are contextual names; omit the '$::' prefix");
                ++index_;
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            const auto name_event = begin_production(SyntaxProduction::AttributeName);
            const auto first = consume_kind(TokenKind::Identifier);
            if (!first) {
                end_production(name_event);
                error_here("expected attribute name");
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            std::string name(first->text);
            while (consume("::")) {
                const auto component = consume_kind(TokenKind::Identifier);
                if (!component) {
                    error_here("expected attribute-name component after '::'");
                    break;
                }
                name += "::";
                name += component->text;
            }
            end_production(name_event);
            Attribute attribute{std::move(name), {}, first->location};
            if (consume("(")) {
                const auto argument_first = index_;
                const auto recording = recording_public_tree_;
                recording_public_tree_ = false;
                if (mode == AttributeParseMode::Semantic && attribute.name == "generic") {
                    const auto saved_generic_types = active_generic_types_;
                    while (!current().is(")") &&
                           current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto parameter_location = current().location;
                        std::optional<std::string> parameter_name;
                        TypePtr value_type;
                        if (current().kind == TokenKind::Identifier &&
                            (current(1).is(",") || current(1).is(")"))) {
                            parameter_name = identifier_binding_name(current());
                            ++index_;
                            active_generic_types_.push_back(*parameter_name);
                        } else {
                            value_type = parse_type();
                            if (value_type) {
                                value_type = parse_declarator(
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
                        if (index_ == start || !consume(",")) break;
                        if (current().is(")")) {
                            error_here("empty 'generic' parameter");
                            break;
                        }
                    }
                    if (attribute.generic_parameters.empty()) {
                        diagnostics_.error(attribute.location,
                                           "'generic' requires at least one parameter");
                    }
                    expect(")", "after generic parameters");
                    active_generic_types_ = saved_generic_types;
                } else if (mode == AttributeParseMode::Semantic && attribute.name == "variadic") {
                    while (!current().is(")") && current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto type = parse_type();
                        std::optional<std::string> binding_name;
                        auto name_location = current().location;
                        if (type) (void)parse_declarator(std::move(type), binding_name, DeclaratorContext::Named,
                                                          nullptr, &name_location);
                        if (!binding_name) error_here("expected variadic state binding name");
                        if (current().kind == TokenKind::String) ++index_;
                        else error_here("expected variadic state-name string");
                        std::string spelling;
                        for (auto at = start; at < index_; ++at) spelling += tokens_[at].text;
                        attribute.arguments.push_back(std::move(spelling));
                        if (binding_name)
                            attribute.variadic_names.push_back({name_location, *binding_name});
                        if (index_ == start || !consume(",")) break;
                    }
                    expect(")", "after variadic state bindings");
                } else if (mode == AttributeParseMode::Semantic && attribute.name == "aligned") {
                    while (!current().is(")") &&
                           current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto expression = parse_expression();
                        if (index_ == start) {
                            error_here("expected alignment expression");
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
                        if (!consume(",")) break;
                    }
                    expect(")", "after alignment expression");
                } else {
                    unsigned depth = 1;
                    std::string argument;
                    while (depth != 0 && current().kind != TokenKind::End) {
                        expand_inline_macro_fragments();
                        if (current().is("(") ) { ++depth; argument += current().text; ++index_; continue; }
                        if (current().is(")")) {
                            --depth;
                            if (depth == 0) {
                                if (!argument.empty()) attribute.arguments.push_back(argument);
                                ++index_;
                                break;
                            }
                            argument += current().text; ++index_; continue;
                        }
                        if (depth == 1 && current().is(",")) {
                            attribute.arguments.push_back(argument);
                            argument.clear();
                            ++index_;
                            continue;
                        }
                        argument += current().text;
                        ++index_;
                    }
                }
                recording_public_tree_ = recording;
                if (index_ > argument_first && tokens_[index_ - 1].is(")"))
                    record_balanced_sequence(argument_first, index_ - 1);
            }
            result.push_back(std::move(attribute));
        } while (consume(","));
        expect("]]", "to close attribute list");
        if (one_specifier) break;
    }
    return result;
}

void Parser::apply_type_attributes(
    TypePtr& type,
    std::optional<std::pair<std::uint32_t, SourceLocation>>*
        pending_address_space) {
    if (!current().is("[[")) return;
    for (const auto& attribute : parse_attributes(true))
        apply_type_attribute(type, attribute, pending_address_space);
}

void Parser::apply_type_attribute(
    TypePtr& type, const Attribute& attribute,
    std::optional<std::pair<std::uint32_t, SourceLocation>>*
        pending_address_space) {
    if (attribute.name == "atomic") {
        if (!attribute.arguments.empty())
            diagnostics_.error(attribute.location, "'atomic' takes no arguments");
        else if (type->is_atomic)
            diagnostics_.error(attribute.location, "duplicate 'atomic' type qualifier");
        else type->is_atomic = true;
        return;
    }
    if (attribute.name == "address_space") {
        if (attribute.arguments.size() != 1) {
            diagnostics_.error(attribute.location,
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
            diagnostics_.error(attribute.location,
                               "address_space requires a nonnegative target registry number");
            return;
        }
        if (pending_address_space) {
            if (*pending_address_space)
                diagnostics_.error(attribute.location,
                                   "duplicate address_space type qualifier");
            else *pending_address_space = std::pair{number, attribute.location};
        } else if (type->kind != Type::Kind::Pointer) {
            diagnostics_.error(attribute.location,
                               "address_space requires a pointer type");
        } else if (type->address_space_location.valid()) {
            diagnostics_.error(attribute.location,
                               "duplicate address_space type qualifier");
        } else {
            type->address_space = number;
            type->address_space_location = attribute.location;
        }
        return;
    }
    diagnostics_.error(attribute.location,
                       "attribute '" + attribute.name +
                           "' is not valid as a type qualifier here");
}

std::optional<std::string> Parser::parse_qualified_name(SyntaxProduction production_name) {
    normalize_qualified_name();
    if (current().kind != TokenKind::Identifier) return std::nullopt;
    ProductionScope production(*this, production_name);
    const auto first = consume_kind(TokenKind::Identifier);
    if (!first) return std::nullopt;
    std::string name = identifier_binding_name(*first);
    while (current().is("::") && current(1).kind == TokenKind::Identifier) {
        consume("::");
        const auto component = consume_kind(TokenKind::Identifier);
        if (!component) {
            error_here("expected identifier after '::'");
            break;
        }
        name += "::";
        name += identifier_binding_name(*component);
    }
    return name;
}

void Parser::normalize_qualified_name() {
    if (!syntax_ || raw_token_depth_) return;
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
        expand_inline_macro_fragments();
        if (diagnostics_.errors() != errors ||
            std::as_const(*this).current().kind != TokenKind::Identifier) return;
        ++index_;
        for (;;) {
            // A following fragment may contribute the separator, component,
            // or nothing. Never enter an argument group while probing a name.
            expand_inline_macro_fragments();
            if (diagnostics_.errors() != errors || assembled_invocation() ||
                !std::as_const(*this).current().is("::")) break;
            ++index_;
            expand_inline_macro_fragments();
            if (diagnostics_.errors() != errors || assembled_invocation() ||
                std::as_const(*this).current().kind != TokenKind::Identifier) break;
            ++index_;
        }
        index_ = restore.first;
        if (diagnostics_.errors() != errors || !macro_start()) return;
        // Fragments may have assembled a complete qualified invocation.
        // Expand it before callers classify a value, type, or declaration.
    }
}

std::string Parser::peek_qualified_name() {
    normalize_qualified_name();
    return std::as_const(*this).peek_qualified_name();
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

TypePtr Parser::resolve_type_alias(std::string_view name) const {
    require_public_name_context(name);
    if (name.find("::") == std::string_view::npos) {
        const NameKey key(name, current().location);
        for (auto scope = local_type_scopes_.size(); scope != 0; --scope) {
            const auto found = local_type_scopes_[scope - 1].find(key);
            if (found != local_type_scopes_[scope - 1].end()) return found->second;
            if (local_scopes_[scope - 1].contains(key)) return {};
        }
    }
    const auto find = [&](std::string_view candidate) -> TypePtr {
        const auto found = type_aliases_.find(std::string(candidate));
        return found == type_aliases_.end() ? TypePtr{} : found->second;
    };
    const auto origin = token_origin(current().location);
    for (const auto& candidate : namespace_candidates(NameUse(name),
            origin.context ? origin.context->name_space : active_namespace_,
            origin.context ? origin.context->imports : active_imports_)) {
        if (const auto type = find(candidate)) return type;
        if (known_ordinary_values_.contains(candidate)) return {};
    }
    return {};
}

bool Parser::type_start(TypeProbe probe) {
    // Cast/sizeof probes can be looking at an expression owner rather than
    // a type. Its input must stay opaque until the expression parser selects
    // it, even if the next token happens to begin a procedural invocation.
    const auto* owner = active_syntax(false);
    if (probe != TypeProbe::Required && owner) {
        // Activation commits before name-sensitive type/expression probing.
        // Required type slots remain unaffected by expression activation.
        if (owner->kind == SyntaxKind::Expression) return false;
    } else normalize_qualified_name();
    const auto token = current();
    if (token.kind == TokenKind::PreparedFragment)
        return token.prepared && token.prepared->category == SyntaxParseCategory::Type;
    if (token.kind == TokenKind::StructuredSplice)
        return token.splice && syntax_type_node(*token.splice);
    if (allow_public_header_deferral_ && public_header_uncertain_names_ && probe == TypeProbe::Required &&
        token.kind == TokenKind::Identifier && !is_reserved_identifier(token.text) &&
        std::as_const(*this).peek_qualified_name().find("::") == std::string::npos) {
        public_deferred_header_ = true;
        return true;
    }
    if (token.kind == TokenKind::Identifier) {
        const auto binding = token.value_binding.kind != ValueBinding::Kind::Unknown
            ? token.value_binding : token_origin(token.location).value_binding;
        if (binding.kind != ValueBinding::Kind::Unknown) return false;
        require_public_name_context(std::as_const(*this).peek_qualified_name());
    }
    const auto name = std::as_const(*this).peek_qualified_name();
    if (preparing_header_ && (!parsing_public_fragment_ || probing_header_type_) &&
        token.kind == TokenKind::Identifier && !is_reserved_identifier(token.text)) {
        if (probe == TypeProbe::Required || probe == TypeProbe::GenericTypeArgumentAlternative)
            return true;
        if (!parsing_public_fragment_ &&
            probe_header_type(probe == TypeProbe::GenericArgumentAlternative)) return true;
    }
    return token.is("[[") || token.is("const") || token.is("volatile") ||
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

TypePtr Parser::parse_type(bool record_specifiers,
                           std::function<bool()> storage_specifier,
                           std::vector<Attribute>* declaration_attributes) {
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
            if (attribute.name == "atomic" || attribute.name == "address_space")
                deferred_type_attributes.push_back(std::move(attribute));
            else declaration_attributes->push_back(std::move(attribute));
        }
    }
    const auto consume_specifier_attributes = [&](TypePtr* built_type) {
        for (auto& attribute : parse_attributes(true)) {
            if (attribute.name == "atomic" || attribute.name == "address_space") {
                if (built_type)
                    apply_type_attribute(*built_type, attribute, &pending_address_space);
                else deferred_type_attributes.push_back(std::move(attribute));
            } else if (declaration_attributes) {
                declaration_attributes->push_back(std::move(attribute));
            } else {
                diagnostics_.error(attribute.location,
                    "attribute '" + attribute.name +
                        "' is not valid as a type qualifier here");
            }
        }
    };
    const auto storage_start = [&] {
        return storage_specifier &&
            (current().is("typedef") || current().is("static") ||
             current().is("global") || current().is("register") ||
             current().is("stack") || current().is("inline"));
    };
    while (current().is("const") || current().is("volatile") ||
           current().is("restrict") || current().is("[[") || storage_start()) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if (storage_start()) {
            if (!storage_specifier()) break;
            continue;
        }
        if (current().is("[[")) {
            consume_specifier_attributes(nullptr);
            continue;
        }
        ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
        if (consume("const"))
            is_const = true;
        else if (consume("volatile"))
            is_volatile = true;
        else {
            restrict_location = current().location;
            consume("restrict");
            is_restrict = true;
        }
    }
    normalize_qualified_name();
    TypePtr type;
    {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
        ProductionScope target_type(*this, current().kind == TokenKind::BuiltinName
                                               ? SyntaxProduction::TargetScalarBuiltinName
                                               : SyntaxProduction::None);
        ProductionScope builtin(*this, current().kind == TokenKind::BuiltinName
                                           ? SyntaxProduction::BuiltinName
                                           : SyntaxProduction::None);
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        if (current().kind == TokenKind::PreparedFragment) {
            const auto token = current();
            ++index_;
            type = builtin_type(BuiltinType::I32);
            if (!token.prepared || token.prepared->category != SyntaxParseCategory::Type) {
                diagnostics_.error(token.location, "prepared expression is not a type");
            } else if (parsing_public_fragment_) {
                if (!probing_header_type_)
                    diagnostics_.error(token.location, "internal prepared type cannot enter a public capture");
            } else if (auto child = prepared_fragment_parser(token)) {
                const auto prior_records = child->record_types_;
                const auto previous_errors = diagnostics_.errors();
                auto parsed = child->parse_type();
                std::optional<std::string> name;
                if (parsed) parsed = child->parse_declarator(std::move(parsed), name, DeclaratorContext::TypeName);
                if (name || child->current().kind != TokenKind::End)
                    child->error_here("prepared type must contain one complete nameless type");
                if (diagnostics_.errors() == previous_errors && parsed &&
                    transfer_spliced_tags(*child, prior_records, token.location)) {
                    for (auto& assertion : child->static_assertions_)
                        static_assertions_.push_back(std::move(assertion));
                    type = std::move(parsed);
                }
            }
        } else if (current().kind == TokenKind::StructuredSplice) {
            const auto item = current();
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
                        if (item.splice->kind == SyntaxNode::Kind::Deferred)
                            child->restore_deferred_environment(
                                *item.splice->context->parse_environment,
                                *item.splice->context, item.splice->span.first);
                        else
                            child->restore_environment(
                                *item.splice->context->parse_environment,
                                *item.splice->context);
                        const auto prior_records = child->record_types_;
                        const auto previous_errors = diagnostics_.errors();
                        auto parsed = child->parse_type();
                        std::optional<std::string> declarator_name;
                        if (parsed) parsed = child->parse_declarator(std::move(parsed), declarator_name,
                                                                   DeclaratorContext::TypeName);
                        if (declarator_name)
                            child->error_here("structured type splice cannot declare a name");
                        if (child->current().kind != TokenKind::End)
                            child->error_here("structured type splice must contain one complete type");
                        if (diagnostics_.errors() == previous_errors && parsed &&
                            transfer_spliced_tags(*child, prior_records, item.location)) {
                            for (auto& assertion : child->static_assertions_)
                                static_assertions_.push_back(std::move(assertion));
                            type = std::move(parsed);
                        }
                        if (preparing_header_)
                            retain_prepared_fragment(item_index,
                                {std::move(child->tokens_), item.location}, SyntaxParseCategory::Type,
                                item.splice->context, item.splice->kind == SyntaxNode::Kind::Deferred,
                                item.splice->span.first);
                    }
                }
            }
        } else if (current().is("$::meta::context")) {
            if (!parsing_procedural_body_)
                error_here("$::meta::context is only available in expansion functions");
            type = context_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if (current().is("$::meta::span")) {
            if (!parsing_procedural_body_)
                error_here("$::meta::span is only available in expansion functions");
            type = span_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if (current().is("$::meta::syntax")) {
            if (!parsing_procedural_body_)
                error_here("$::meta::syntax is only available in expansion functions");
            type = syntax_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if (current().is("$::meta::syntax_match")) {
            if (!parsing_procedural_body_)
                error_here("$::meta::syntax_match is only available in expansion functions");
            type = syntax_match_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
            ++index_;
        } else if (current().is("$::meta::tokens")) {
            if (!parsing_procedural_body_)
                error_here("$::meta::tokens is only available in translation-time macro bodies");
            ++index_;
            type = tokens_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
        } else if (current().is("$::meta::bytes") || current().is("$::meta::buffer")) {
            const bool bytes = consume("$::meta::bytes");
            if (!bytes)
                consume("$::meta::buffer");
            type = bytes ? bytes_type() : buffer_type();
            type->is_const = is_const;
            type->is_volatile = is_volatile;
        } else if (current().is("struct") || current().is("union")) {
            ProductionScope record(*this, SyntaxProduction::StructOrUnionSpecifier);
            const auto location = current().location;
            const bool is_union = consume("union");
            if (!is_union)
                consume("struct");
            const auto name_location = current().location;
            const auto name = parse_qualified_name();
            if (!name) {
                error_here("expected record name after '" +
                           std::string(is_union ? "union" : "struct") + "'");
                return {};
            }
            auto record_attributes = parse_attributes();
            if (current().is("{")) {
                RecordDecl declaration;
                declaration.location = location;
                declaration.name = join_namespace(active_namespace_, *name);
                declaration.is_union = is_union;
                declaration.complete = true;
                if (declaration_attributes)
                    for (const auto& attribute : *declaration_attributes)
                        if (attribute.name == "packed" || attribute.name == "aligned")
                            declaration.attributes.push_back(attribute);
                declaration.attributes.insert(declaration.attributes.end(),
                    std::make_move_iterator(record_attributes.begin()),
                    std::make_move_iterator(record_attributes.end()));
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
                if (defer_public_header_group(allow_public_header_deferral_
                        ? bounded_group_end(index_) : std::nullopt, [&] {
                        consume("{");
                        parse_record_members(declaration);
                    })) declaration.members.clear();
                type = record_type(declaration.name, is_union, is_const, is_volatile);
                pending_records_.push_back(std::move(declaration));
            } else {
                require_public_name_context(*name, name_location, PublicNameDomain::Tag);
                for (const auto& attribute : record_attributes)
                    diagnostics_.error(attribute.location,
                        "record attributes on a type use are not yet supported");
                auto canonical = *name;
                auto found = record_types_.end();
                if (name->find("::") != std::string::npos) {
                    found = record_types_.find(canonical);
                } else {
                    auto current_namespace = active_namespace_;
                    while (!current_namespace.empty()) {
                        canonical = join_namespace(current_namespace, *name);
                        found = record_types_.find(canonical);
                        if (found != record_types_.end())
                            break;
                        const auto separator = current_namespace.rfind("::");
                        if (separator == std::string::npos)
                            break;
                        current_namespace.resize(separator);
                    }
                    for (const auto &imported : active_imports_) {
                        if (found != record_types_.end())
                            break;
                        canonical = join_namespace(imported, *name);
                        found = record_types_.find(canonical);
                    }
                    if (found == record_types_.end()) {
                        canonical = *name;
                        found = record_types_.find(canonical);
                    }
                }
                if (found == record_types_.end()) {
                    canonical = name->find("::") == std::string::npos && !active_namespace_.empty()
                                    ? join_namespace(active_namespace_, *name)
                                    : *name;
                    record_types_.emplace(canonical, RecordTag{is_union, false});
                } else if (found->second.is_union != is_union) {
                    diagnostics_.error(current().location,
                                       "record tag '" + *name +
                                           "' was previously declared with the other record kind");
                }
                type = record_type(canonical, is_union, is_const, is_volatile);
            }
        } else if (current().is("enum")) {
            ProductionScope enumeration(*this, SyntaxProduction::EnumSpecifier);
            const auto location = current().location;
            consume("enum");
            const auto name_location = current().location;
            const auto name = parse_qualified_name();
            if (!name) {
                error_here("expected enumeration name after 'enum'");
                return {};
            }
            auto enum_attributes = parse_attributes();
            if (current().is("{")) {
                EnumDecl declaration;
                declaration.location = location;
                declaration.name = join_namespace(active_namespace_, *name);
                if (declaration_attributes)
                    for (const auto& attribute : *declaration_attributes)
                        if (attribute.name == "underlying")
                            declaration.attributes.push_back(attribute);
                declaration.attributes.insert(declaration.attributes.end(),
                    std::make_move_iterator(enum_attributes.begin()),
                    std::make_move_iterator(enum_attributes.end()));
                declaration.underlying = enum_underlying(declaration.attributes);
                if (defer_public_header_group(allow_public_header_deferral_
                        ? bounded_group_end(index_) : std::nullopt, [&] {
                        parse_enumerators(declaration, active_namespace_);
                    })) declaration.enumerators.clear();
                const auto found = enum_types_.find(declaration.name);
                if (found != enum_types_.end() && found->second != declaration.underlying) {
                    diagnostics_.error(location, "enumeration '" + declaration.name +
                        "' redeclared with a different underlying type");
                } else {
                    enum_types_[declaration.name] = declaration.underlying;
                }
                type = enum_type(declaration.name, declaration.underlying,
                                 is_const, is_volatile);
                pending_enumerations_.push_back(std::move(declaration));
            } else {
                require_public_name_context(*name, name_location, PublicNameDomain::Tag);
                for (const auto& attribute : enum_attributes)
                    diagnostics_.error(attribute.location,
                        "enumeration attributes on a type use are not yet supported");
                auto canonical = *name;
                auto found = enum_types_.find(canonical);
                if (found == enum_types_.end() && canonical.find("::") == std::string::npos &&
                    !active_namespace_.empty()) {
                    canonical = join_namespace(active_namespace_, canonical);
                    found = enum_types_.find(canonical);
                }
                if (found == enum_types_.end() && name->find("::") == std::string::npos) {
                    for (const auto &imported : active_imports_) {
                        canonical = join_namespace(imported, *name);
                        found = enum_types_.find(canonical);
                        if (found != enum_types_.end())
                            break;
                    }
                }
                if (found == enum_types_.end()) {
                    diagnostics_.error(current().location,
                                       "unknown enumeration type '" + *name + "'");
                    type = enum_type(*name, BuiltinType::I32, is_const, is_volatile);
                } else {
                    type = enum_type(canonical, found->second, is_const, is_volatile);
                }
            }
        }
        const auto kind = type ? std::optional<BuiltinType>{} : builtin_kind(current().text);
        const auto alias_name = type ? std::string{} : peek_qualified_name();
        const auto generic = std::find(active_generic_types_.begin(),
                                       active_generic_types_.end(), alias_name);
        const bool uncertain_header_type = allow_public_header_deferral_ && public_header_uncertain_names_ &&
            !alias_name.empty() && alias_name.find("::") == std::string::npos &&
            !is_reserved_identifier(alias_name);
        if (uncertain_header_type) public_deferred_header_ = true;
        const auto alias = alias_name.empty() || generic != active_generic_types_.end() || uncertain_header_type
            ? TypePtr{} : resolve_type_alias(alias_name);
        const bool provisional_type = uncertain_header_type || (preparing_header_ &&
            (!parsing_public_fragment_ || probing_header_type_) &&
            current().kind == TokenKind::Identifier && !is_reserved_identifier(current().text));
        if (!type && !kind && generic == active_generic_types_.end() && !alias && !provisional_type) {
            if (const auto message = familiar_c_spelling(current().text)) {
                error_here(*message);
                ++index_;
                return builtin_type(BuiltinType::I32, is_const, is_volatile);
            }
            error_here("expected Cross type");
            return {};
        }
        if (!type) {
            if (alias) {
                ProductionScope alias_production(*this, SyntaxProduction::TypedefName);
                (void)parse_qualified_name();
                type = copy_type(alias);
                type->is_const = type->is_const || is_const;
                type->is_volatile = type->is_volatile || is_volatile;
            } else {
                ProductionScope leaf(*this, kind ? SyntaxProduction::ScalarType
                                                 : SyntaxProduction::TypedefName);
                const auto spelling = identifier_binding_name(current());
                if (kind)
                    ++index_;
                else
                    (void)parse_qualified_name();
                type = kind ? builtin_type(*kind, is_const, is_volatile)
                            : generic_type(spelling, is_const, is_volatile);
            }
        }
    }
    for (const auto& attribute : deferred_type_attributes)
        apply_type_attribute(type, attribute, &pending_address_space);
    while (current().is("const") || current().is("volatile") ||
           current().is("restrict") || current().is("[[") || storage_start()) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if (storage_start()) {
            if (!storage_specifier()) break;
        } else if (current().is("[[")) {
            // Each written attribute_specifier owns its own occurrence.
            consume_specifier_attributes(&type);
        } else {
            ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
            if (consume("const"))
                is_const = true;
            else if (consume("volatile"))
                is_volatile = true;
            else {
                restrict_location = current().location;
                consume("restrict");
                is_restrict = true;
            }
        }
    }
    type->is_const = type->is_const || is_const;
    type->is_volatile = type->is_volatile || is_volatile;
    type->is_restrict = type->is_restrict || is_restrict;
    if (type->is_restrict && type->kind != Type::Kind::Pointer) {
        diagnostics_.error(restrict_location.value_or(current().location),
                           "restrict qualifier requires a pointer type");
    }
    type->pending_address_space = pending_address_space;
    return type;
}

std::vector<std::string> Parser::preview_generic_types(bool* pending_fragments) {
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
    const auto expose = [&](std::size_t at) {
        if (pending_fragments) {
            if (at >= tokens_.size() || tokens_[at].kind != TokenKind::Identifier) return;
            while (at + 2 < tokens_.size() && tokens_[at + 1].is("::") &&
                   tokens_[at + 2].kind == TokenKind::Identifier) at += 2;
            if (at + 2 < tokens_.size() && tokens_[at + 1].is("!") &&
                (tokens_[at + 2].is("(") || tokens_[at + 2].is("[") ||
                 tokens_[at + 2].is("{"))) *pending_fragments = true;
            return;
        }
        if (parsing_public_fragment_ || preparing_header_ || !syntax_) return;
        const auto saved = index_;
        index_ = at;
        normalize_qualified_name();
        index_ = saved;
    };
    std::size_t declarator_depth = 0;
    const auto group_end = [&](std::size_t first, bool parameters = false) -> std::optional<std::size_t> {
        if (execution && declarator_depth >= execution->limits().depth) {
            execution->tree_limit_error(tokens_[first].location);
            if (recording_public_tree_) public_tree_failed_ = true;
            return {};
        }
        std::vector<std::string_view> closes{closer(tokens_[first].text)};
        for (auto at = first + 1; at < tokens_.size(); ++at) {
            if (parameters && closes.size() == 1) expose(at);
            if (!work(at)) return {};
            if (tokens_[at].kind == TokenKind::End) return {};
            if (const auto close = closer(tokens_[at].text); !close.empty()) {
                if (execution && closes.size() + declarator_depth >= execution->limits().depth) {
                    execution->tree_limit_error(tokens_[at].location);
                    if (recording_public_tree_) public_tree_failed_ = true;
                    return {};
                }
                closes.push_back(close);
            } else if (tokens_[at].is(")") || tokens_[at].is("]") ||
                       tokens_[at].is("}") || tokens_[at].is("]]")) {
                if (closes.back() != tokens_[at].text) return {};
                closes.pop_back();
                if (closes.empty()) return at;
            }
        }
        return {};
    };
    const auto append_types = [&](std::size_t first, std::size_t end) {
        auto begin = first;
        for (auto at = first; at <= end; ++at) {
            if (at != end) {
                if (!work(at)) return;
                if (!closer(tokens_[at].text).empty()) {
                    const auto close = group_end(at);
                    if (!close || *close >= end) return;
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
    bool have_type = false;
    bool have_name = false;
    const auto name_end = [&](std::size_t at) {
        while (at + 2 < tokens_.size() && tokens_[at + 1].is("::") &&
               tokens_[at + 2].kind == TokenKind::Identifier) at += 2;
        return at;
    };
    const auto macro_end = [&](std::size_t last_name) -> std::optional<std::size_t> {
        if (last_name + 2 < tokens_.size() && tokens_[last_name + 1].is("!") &&
            (tokens_[last_name + 2].is("(") || tokens_[last_name + 2].is("[") ||
             tokens_[last_name + 2].is("{")))
            // A malformed invocation is still opaque. Stop discovery at the
            // fence instead of descending into its unbalanced raw input.
            return group_end(last_name + 2).value_or(tokens_.size() - 1);
        return {};
    };
    for (auto cursor = index_; cursor < tokens_.size(); ++cursor) {
        // Header fragments can introduce generics that scope over the earlier
        // result type. Expose attribute names and generic-parameter sequences,
        // but never inspect expression arguments or a function's body.
        expose(cursor);
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
            expose(at);
            if (at < tokens_.size() && tokens_[at].kind == TokenKind::Identifier) {
                const auto last_name = name_end(at);
                for (; at <= last_name; ++at)
                    if (!work(at)) return result;
                if (const auto end = macro_end(last_name)) at = *end + 1;
                while (at < tokens_.size() && tokens_[at].is("[[")) {
                    const auto end = group_end(at);
                    if (!end) return result;
                    at = *end + 1;
                }
                if (at < tokens_.size() && tokens_[at].is("{")) {
                    const auto end = group_end(at);
                    if (!end) return result;
                    at = *end + 1;
                }
                have_type = true;
                cursor = at - 1;
                continue;
            }
        }
        if (tokens_[cursor].is("[[")) {
            auto at = cursor + 1;
            while (at < tokens_.size()) {
                expose(at);
                if (tokens_[at].kind != TokenKind::Identifier) break;
                const auto name = at++;
                bool qualified = false;
                expose(at);
                while (at + 1 < tokens_.size() && tokens_[at].is("::")) {
                    qualified = true;
                    expose(++at);
                    if (tokens_[at].kind != TokenKind::Identifier) break;
                    expose(++at);
                }
                if (at < tokens_.size() && tokens_[at].is("(")) {
                    const bool generic = !qualified && tokens_[name].is("generic");
                    const auto arguments_end = group_end(at, generic);
                    if (!arguments_end) break;
                    if (generic)
                        append_types(at + 1, *arguments_end);
                    at = *arguments_end + 1;
                }
                expose(at);
                if (at >= tokens_.size() || !tokens_[at].is(",")) break;
                ++at;
            }
            const auto close = group_end(cursor);
            if (!close) break;
            cursor = *close;
        } else if (tokens_[cursor].is("<") && have_name && cursor != 0 &&
                   tokens_[cursor - 1].kind == TokenKind::Identifier) {
            unsigned depth = 1;
            auto close = cursor + 1;
            for (; close < tokens_.size(); ++close) {
                if (depth == 1) expose(close);
                if (!work(close)) return result;
                if (!closer(tokens_[close].text).empty()) {
                    const auto end = group_end(close);
                    if (!end) return result;
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
            if (close + 1 < tokens_.size()) expose(close + 1);
            if (depth != 0 || close + 1 >= tokens_.size() ||
                (!tokens_[close + 1].is("(") &&
                 !(declarator_depth && tokens_[close + 1].is(")")))) break;
            append_types(cursor + 1, close);
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
            const auto close = group_end(cursor);
            if (!close) break;
            cursor = *close;
        } else if (tokens_[cursor].kind == TokenKind::Identifier &&
                   !is_reserved_identifier(tokens_[cursor].text)) {
            const auto last_name = name_end(cursor);
            for (auto at = cursor + 1; at <= last_name; ++at)
                if (!work(at)) return result;
            if (have_type) have_name = true;
            else have_type = true;
            cursor = macro_end(last_name).value_or(last_name);
        } else if (builtin_kind(tokens_[cursor].text) ||
                   tokens_[cursor].kind == TokenKind::BuiltinName ||
                   tokens_[cursor].kind == TokenKind::StructuredSplice ||
                   tokens_[cursor].kind == TokenKind::PreparedFragment) {
            have_type = true;
        }
    }
    return result;
}

std::vector<FunctionDecl::GenericParameter>
Parser::parse_angle_generic_parameters() {
    if (!allow_public_header_deferral_) return parse_angle_generic_parameters_impl();
    std::vector<FunctionDecl::GenericParameter> result;
    if (defer_public_header_group(generic_parameter_group_end(index_), [&] {
            result = parse_angle_generic_parameters_impl();
        })) result.clear();
    return result;
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

std::vector<FunctionDecl::GenericParameter>
Parser::parse_angle_generic_parameters_impl() {
    ProductionScope list(*this, SyntaxProduction::GenericParameterList);
    std::vector<FunctionDecl::GenericParameter> result;
    consume("<");
    if (consume(">")) {
        error_here("a generic parameter list cannot be empty");
        return result;
    }
    for (;;) {
        ProductionScope parameter(*this, SyntaxProduction::GenericParameter);
        const auto location = current().location;
        std::optional<std::string> name;
        TypePtr value_type;
        if (current().kind == TokenKind::Identifier &&
            (current(1).is(",") || current(1).is(">"))) {
            name = identifier_binding_name(current());
            ++index_;
        } else {
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                value_type = parse_type();
                if (value_type)
                    value_type = parse_declarator(std::move(value_type), name, DeclaratorContext::TypePrefix);
            }
            if (const auto token = consume_kind(TokenKind::Identifier))
                name = identifier_binding_name(*token);
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
                               [&](const auto& parameter) {
                                   return parameter.name == *name;
                               })) {
            diagnostics_.error(location,
                               "duplicate generic parameter '" + *name + "'");
        } else {
            if (!value_type) active_generic_types_.push_back(*name);
            result.push_back({std::move(*name), std::move(value_type), location});
        }
        parameter.finish();
        if (!consume(",")) break;
        if (current().is(">")) {
            error_here("a generic parameter list cannot have a trailing comma");
            break;
        }
    }
    expect(">", "after generic parameters");
    return result;
}

void Parser::remember_function(const FunctionDecl& function) {
    if (function.generic_parameters.empty()) {
        known_ordinary_values_.insert(function.name);
        return;
    }
    GenericSignature signature;
    for (const auto& parameter : function.generic_parameters)
        signature.push_back(parameter.value_type ? GenericParameterKind::Value : GenericParameterKind::Type);
    known_generic_functions_.try_emplace(function.name, std::move(signature));
}

const Parser::GenericSignature* Parser::known_generic_parameters(const Expr& name) const {
    const auto binding = name_key(name).binding.kind;
    if (binding == ValueBinding::Kind::Local) return nullptr;
    if (binding == ValueBinding::Kind::Unknown && current().is("<"))
        require_public_name_context(name.text, name.location);
    if (binding == ValueBinding::Kind::Unknown && name.text.find("::") == std::string::npos)
        for (auto scope = local_scopes_.rbegin();
             scope != local_scopes_.rend(); ++scope)
            if (scope->contains(NameKey(name.text, name.location))) return nullptr;
    for (const auto& candidate : namespace_candidates(
             NameUse{name}, active_namespace_, active_imports_)) {
        if (known_ordinary_values_.contains(candidate)) return nullptr;
        if (const auto found = known_generic_functions_.find(candidate);
            found != known_generic_functions_.end()) return &found->second;
    }
    return nullptr;
}

bool Parser::known_generic_name(const Expr& name) const {
    return known_generic_parameters(name) != nullptr;
}

bool Parser::consume_generic_close() {
    if (consume(">")) return true;
    if (!current().is(">>")) return false;
    const auto source = current().split_source ? current().split_source
        : std::make_shared<const SplitTokenSource>(SplitTokenSource{
            current().kind, std::string(current().text)});
    auto remainder = current();
    remainder.text = ">";
    remainder.split_source = source;
    remainder.split_offset = current().split_offset + 1;
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
    return true;
}

TypePtr Parser::parse_declarator(TypePtr base, std::optional<std::string>& name, DeclaratorContext context,
                                 std::unique_ptr<Expr>* dynamic_outer_bound,
                                 SourceLocation* name_location,
                                 std::vector<FunctionDecl::GenericParameter>* angle_parameters) {
    const bool parameter = context == DeclaratorContext::Parameter;
    const bool abstract_only = context == DeclaratorContext::TypePrefix;
    expand_inline_macro_fragments();
    const bool written = current().is("*") || current().is("(") || current().is("[") ||
                         (!abstract_only && current().kind == TokenKind::Identifier);
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
    while (current().is("*")) {
        ProductionScope pointer(*this, SyntaxProduction::PointerPart);
        consume("*");
        base = pointer_type(std::move(base));
        if (pending_address_space) {
            base->address_space = pending_address_space->first;
            base->address_space_location = pending_address_space->second;
            pending_address_space.reset();
        }
        for (;;) {
            expand_inline_macro_fragments();
            if (!current().is("const") && !current().is("volatile") &&
                !current().is("restrict") && !current().is("[[")) break;
            if (current().is("[[")) {
                apply_type_attributes(base);
                continue;
            }
            ProductionScope qualifier(*this, SyntaxProduction::TypeQualifier);
            if (consume("const"))
                base->is_const = true;
            else if (consume("volatile"))
                base->is_volatile = true;
            else {
                consume("restrict");
                base->is_restrict = true;
            }
        }
    }
    expand_inline_macro_fragments();
    const bool grouped = [&] {
        if (!current().is("(")) return false;
        struct Restore {
            std::size_t& index;
            std::size_t previous;
            ~Restore() { index = previous; }
        } restore{index_, index_};
        ++index_;
        // This is a reached declarator boundary, not a speculative scan of
        // an expression owner's input. Public recognition still throws before
        // expansion so a bounded enclosing capture may defer instead.
        expand_inline_macro_fragments();
        if (current().is("*") || current().is("(") || current().is("[")) return true;
        if (current().kind != TokenKind::Identifier || is_reserved_identifier(current().text))
            return false;
        // A required declared name can shadow a type. In optional/abstract
        // contexts an established type instead starts an unnamed parameter
        // list, preserving the existing callable-type interpretation.
        return context == DeclaratorContext::Named || !type_start();
    }();
    if (pending_address_space) {
        if (grouped)
            base->pending_address_space = pending_address_space;
        else if (base->kind != Type::Kind::Pointer)
            diagnostics_.error(pending_address_space->second,
                               "address_space requires a pointer type");
        else if (base->address_space_location.valid())
            diagnostics_.error(pending_address_space->second,
                               "duplicate address_space type qualifier");
        else {
            base->address_space = pending_address_space->first;
            base->address_space_location = pending_address_space->second;
        }
    }
    TypePtr nested;
    TypePtr hole;
    const auto direct_event =
        begin_production(written ? SyntaxProduction::DirectDeclarator : SyntaxProduction::None);
    if (grouped) {
        consume("(");
        hole = std::make_shared<Type>();
        nested = parse_declarator(hole, name, context, nullptr, name_location,
                                   angle_parameters);
        expect(")", "after parenthesized declarator");
    } else if (!abstract_only) {
        const auto location = current().location;
        name = parse_qualified_name();
        if (probing_header_type_ && context == DeclaratorContext::TypeName && name)
            throw HeaderProbeRejected{};
        if (name && name_location)
            *name_location = location;
        if (name && angle_parameters && current().is("<"))
            *angle_parameters = parse_angle_generic_parameters();
    }
    expand_inline_macro_fragments();
    if (current().is("[")) {
        base = parse_array_suffix(std::move(base), parameter, dynamic_outer_bound);
    }
    expand_inline_macro_fragments();
    if (current().is("(")) {
        ProductionScope suffix(*this, SyntaxProduction::FunctionSuffix);
        std::vector<ParameterDecl> parameters;
        bool variadic = false;
        if (defer_public_header_group(allow_public_header_deferral_
                ? bounded_group_end(index_) : std::nullopt, [&] {
                consume("(");
                parse_parameter_list(parameters, variadic);
                expect(")", "after function parameters");
            })) {
            parameters.clear();
            variadic = false;
        }
        base = function_type(std::move(base), std::move(parameters), variadic);
        expand_inline_macro_fragments();
        if (current().is("->")) {
            ProductionScope result_location(*this, SyntaxProduction::ResultLocation);
            consume("->");
            const auto location_token = consume_kind(TokenKind::String);
            if (!location_token) {
                error_here("expected result location string after '->'");
            } else {
                base->function->result_location = decode_string_literal(location_token->text);
                if (!base->function->result_location)
                    diagnostics_.error(location_token->location, "invalid result location string");
            }
        }
        // In a grouped declarator the attributes follow this function
        // suffix, even if another callable component surrounds it.
        if ((nested && nested != hole) || !name) {
            auto suffix_attributes = parse_attributes();
            apply_callable_attributes(base, suffix_attributes);
            for (const auto& attribute : suffix_attributes) {
                if (attribute.name != "abi" && attribute.name != "clobber" &&
                    attribute.name != "stack_cleanup")
                    diagnostics_.error(
                        attribute.location,
                        "function-only attribute cannot qualify a nested callable type");
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
        const auto fill = [&](auto&& self, TypePtr& type) -> void {
            if (type == hole) {
                type = base;
                return;
            }
            if (!type) return;
            if (type->kind == Type::Kind::Pointer) {
                if (type->pointee == hole && nested_address_space) {
                    if (type->address_space_location.valid()) {
                        diagnostics_.error(
                            nested_address_space->second,
                            "duplicate address_space type qualifier");
                    } else {
                        type->address_space = nested_address_space->first;
                        type->address_space_location =
                            nested_address_space->second;
                    }
                    nested_address_space.reset();
                }
                self(self, type->pointee);
            } else if (type->kind == Type::Kind::Array)
                self(self, type->element);
            else if (type->kind == Type::Kind::Function && type->function)
                self(self, type->function->result);
        };
        fill(fill, nested);
        if (nested_address_space) {
            diagnostics_.error(nested_address_space->second,
                               "address_space requires a pointer type");
        }
        base = std::move(nested);
    }
    if (parameter && base) {
        if (base->kind == Type::Kind::Array)
            base = pointer_type(base->element);
        else if (base->kind == Type::Kind::Function)
            base = pointer_type(base);
    }
    return base;
}

void Parser::apply_callable_attributes(
    TypePtr& type, const std::vector<Attribute>& attributes) {
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
                diagnostics_.error(
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
            diagnostics_.error(
                attribute.location,
                "callable attribute requires a function or function-pointer type");
            continue;
        }
        *node = std::make_shared<Type>(**node);
        (*node)->function = std::make_shared<FunctionType>(*(*node)->function);
        auto& signature = *(*node)->function;
        if (attribute.name == "clobber") {
            if (attribute.arguments.empty()) {
                diagnostics_.error(attribute.location,
                                   "'clobber' requires string arguments");
            }
            for (const auto& argument : attribute.arguments) {
                const auto resource = decode_string_literal(argument);
                if (!resource || resource->empty())
                    diagnostics_.error(attribute.location,
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
            diagnostics_.error(attribute.location,
                               "'" + attribute.name +
                                   "' requires one nonempty string");
            continue;
        }
        if (attribute.name == "abi") signature.abi = *spelling;
        else signature.stack_cleanup = *spelling;
    }
}

TypePtr Parser::parse_array_suffix(
    TypePtr element, bool parameter,
    std::unique_ptr<Expr>* dynamic_outer_bound) {
    std::vector<std::uint32_t> bounds;
    while (current().is("[")) {
        ProductionScope array_suffix(*this, SyntaxProduction::ArraySuffix);
        consume("[");
        if (consume("]")) {
            if (parameter) {
                diagnostics_.error(
                    current().location,
                    "an array parameter requires a positive fixed bound in the bootstrap compiler");
                bounds.push_back(1);
            } else {
                bounds.push_back(0);
            }
            continue;
        }
        const auto location = current().location;
        auto expression = parse_assignment();
        expect("]", "after array bound");
        const auto value = expression ? constant_value(*expression)
                                      : std::nullopt;
        if (!value && dynamic_outer_bound && bounds.empty() &&
            !*dynamic_outer_bound) {
            *dynamic_outer_bound = std::move(expression);
            bounds.push_back(0);
        } else if (!value || *value <= 0 ||
            static_cast<std::uint64_t>(*value) >
                std::numeric_limits<std::uint32_t>::max()) {
            diagnostics_.error(
                location,
                dynamic_outer_bound
                    ? "only the outermost array bound may be a runtime value"
                    : "fixed array bound must be a positive integer translation-time value");
            bounds.push_back(1);
        } else {
            bounds.push_back(static_cast<std::uint32_t>(*value));
        }
    }
    for (auto bound = bounds.rbegin(); bound != bounds.rend(); ++bound) {
        element = array_type(std::move(element), *bound);
    }
    if (parameter && element && element->kind == Type::Kind::Array) {
        element = pointer_type(element->element);
    }
    return element;
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
    Program program;
    while (current().kind != TokenKind::End) {
        const auto before = index_;
        parse_external(program, {});
        // A top-level stray closing brace is a synchronization boundary for a
        // nested namespace, but it must not stall the outermost parse loop.
        if (index_ == before && current().kind != TokenKind::End) ++index_;
    }
    drain_pending_tags(program);
    program.static_assertions = std::move(static_assertions_);
    return program;
}

void Parser::drain_pending_tags(Program& program) {
    for (auto& enumeration : pending_enumerations_)
        program.enumerations.push_back(std::move(enumeration));
    pending_enumerations_.clear();
    for (auto& record : pending_records_)
        program.records.push_back(std::move(record));
    pending_records_.clear();
}

void Parser::parse_typedef(const std::string& name_space,
                           std::vector<Attribute> attributes, TypePtr base_type,
                           SourceLocation location, bool consume_semicolon) {
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    do {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        std::optional<std::string> name;
        auto name_location = location;
        auto type = parse_declarator(copy_type(base_type), name, DeclaratorContext::Named,
                                     nullptr, &name_location);
        if (!type || !name) {
            if (!name) error_here("expected typedef name");
            synchronize_external();
            return;
        }
        if (local_type_scopes_.empty()) *name = join_namespace(name_space, *name);
        else if (name->find("::") != std::string::npos)
            diagnostics_.error(name_location, "a local typedef name must be unqualified");
        auto item_attributes = attributes;
        auto trailing = parse_attributes();
        item_attributes.insert(item_attributes.end(),
            std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
        register_typedef(name_location, std::move(*name), std::move(type), item_attributes);
    } while (consume(","));
    list.finish();
    if (consume_semicolon) expect(";", "after typedef declaration");
}

void Parser::register_typedef(SourceLocation location, std::string name, TypePtr type,
                              const std::vector<Attribute>& attributes) {
    apply_callable_attributes(type, attributes);

    const Attribute* vector_attribute = nullptr;
    for (const auto& attribute : attributes) {
        if (attribute.name != "aligned" && attribute.name != "abi" &&
            attribute.name != "clobber" && attribute.name != "stack_cleanup" &&
            attribute.name != "vector_size" &&
            attribute.name != "ext_vector_type" &&
            attribute.name != "scalable_vector")
            diagnostics_.error(attribute.location,
                "attribute '" + attribute.name + "' is not valid on a typedef");
        if (attribute.name != "vector_size" &&
            attribute.name != "ext_vector_type" &&
            attribute.name != "scalable_vector") {
            continue;
        }
        if (vector_attribute) {
            diagnostics_.error(
                attribute.location,
                "a typedef has at most one vector type attribute");
            continue;
        }
        vector_attribute = &attribute;
    }
    if (vector_attribute) {
        if (type->kind == Type::Kind::Vector ||
            (!is_integer(type) && !is_floating(type)) ||
            type->is_atomic ||
            type->builtin == BuiltinType::Bool ||
            type->builtin == BuiltinType::F80 ||
            type->builtin == BuiltinType::F128 ||
            type->builtin == BuiltinType::Fptr) {
            diagnostics_.error(
                vector_attribute->location,
                "vector element type must be a supported integer, f32, or f64 scalar");
        } else if (vector_attribute->arguments.size() != 1) {
            diagnostics_.error(
                vector_attribute->location,
                "'" + vector_attribute->name +
                    "' requires one positive integer argument");
        } else {
            const auto& text = vector_attribute->arguments.front();
            std::uint64_t amount{};
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), amount);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size() || amount == 0) {
                diagnostics_.error(
                    vector_attribute->location,
                    "'" + vector_attribute->name +
                        "' requires one positive integer argument");
            } else {
                std::uint64_t lanes = amount;
                if (vector_attribute->name == "vector_size") {
                    auto element_bits = type_bits(type);
                    if (type->kind == Type::Kind::Builtin &&
                        (type->builtin == BuiltinType::Iptr ||
                         type->builtin == BuiltinType::Uptr)) {
                        if (address_bits_ == 0) {
                            diagnostics_.error(vector_attribute->location,
                                "vector_size of a target-sized element requires a resolved target");
                            return;
                        }
                        element_bits = address_bits_;
                    }
                    if (amount > std::numeric_limits<std::uint64_t>::max() / 8U) {
                        diagnostics_.error(vector_attribute->location,
                                           "vector size is out of range");
                        return;
                    }
                    const auto total_bits = amount * 8U;
                    if (element_bits == 0 || total_bits % element_bits != 0) {
                        diagnostics_.error(
                            vector_attribute->location,
                            "vector size must be a multiple of the element size");
                        return;
                    } else {
                        lanes = total_bits / element_bits;
                    }
                }
                if (lanes == 0 ||
                    lanes > std::numeric_limits<std::uint32_t>::max()) {
                    diagnostics_.error(vector_attribute->location,
                                       "vector lane count is out of range");
                } else {
                    type = vector_type(
                        type, static_cast<std::uint32_t>(lanes),
                        vector_attribute->name == "scalable_vector");
                }
            }
        }
    }

    if (replacement_)
        declared_aliases_.push_back({name, copy_type(type),
                                     local_type_scopes_.size()});
    auto& aliases = type_aliases_;
    if (!local_type_scopes_.empty()) {
        const NameKey key(name, location);
        if (local_scopes_.back().contains(key)) {
            diagnostics_.error(location, "typedef '" + name + "' conflicts with a local value");
            return;
        }
        auto& local = local_type_scopes_.back();
        const auto found = local.find(key);
        if (found != local.end() && !same_type(found->second, type)) {
            diagnostics_.error(location, "typedef '" + name + "' redeclared with a different type");
            return;
        }
        local[key] = std::move(type);
        return;
    }
    const auto found = aliases.find(name);
    if (found != aliases.end() && !same_type(found->second, type)) {
        diagnostics_.error(location,
                           "typedef '" + name +
                               "' redeclared with a different type");
        return;
    }
    aliases[std::move(name)] = std::move(type);
}

void Parser::parse_external_node_splice(
    Program& program, const std::string& name_space) {
    const auto item = current();
    ++index_;
    const bool function_definition = item.splice &&
        syntax_function_definition_node(*item.splice);
    if (!item.splice || (!syntax_declaration_node(*item.splice) &&
                         !function_definition)) {
        diagnostics_.error(item.location,
            "structured syntax splice requires a declaration or function-definition node at external position");
        return;
    }
    if (!syntax_ || !item.splice->context ||
        !item.splice->context->parse_environment) {
        diagnostics_.error(item.location,
            "structured syntax splice has no retained parse environment");
        return;
    }
    const auto category = item.splice->kind == SyntaxNode::Kind::Deferred
        ? item.splice->deferred_category
        : function_definition ? SyntaxParseCategory::FunctionDefinition
                              : SyntaxParseCategory::Declaration;
    auto output = syntax_->execution()->materialize_node(
        *item.splice, item.location, category);
    if (!output) return;
    auto child = replacement_parser(std::move(*output));
    if (item.splice->kind == SyntaxNode::Kind::Deferred)
        child->restore_deferred_environment(
            *item.splice->context->parse_environment,
            *item.splice->context, item.splice->span.first);
    else
        child->restore_environment(*item.splice->context->parse_environment,
                                   *item.splice->context);
    const auto prior_records = child->record_types_;
    const auto previous_errors = diagnostics_.errors();
    Program parsed;
    child->parse_external(parsed, name_space);
    if (child->current().kind != TokenKind::End)
        child->error_here(function_definition
            ? "structured function-definition splice must contain one complete function definition"
            : "structured declaration splice must contain one complete declaration");
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
    if (!parsed.global_labels.empty() || !parsed.static_assertions.empty())
        diagnostics_.error(item.location,
            "structured declaration splice must contain a core declaration");
    for (const auto& alias : child->declared_aliases_) {
        if (alias.scope_depth != 0) continue;
        if (const auto found = type_aliases_.find(alias.name);
            found != type_aliases_.end() && !same_type(found->second, alias.type))
            diagnostics_.error(item.location,
                "spliced typedef '" + alias.name + "' has a different destination type");
    }
    for (const auto& enumeration : parsed.enumerations)
        if (const auto found = enum_types_.find(enumeration.name);
            found != enum_types_.end() && found->second != enumeration.underlying)
            diagnostics_.error(item.location,
                "spliced enumeration '" + enumeration.name +
                "' conflicts with the destination underlying type");
    if (diagnostics_.errors() != previous_errors ||
        !transfer_spliced_tags(*child, prior_records, item.location)) return;
    for (const auto& alias : child->declared_aliases_)
        if (alias.scope_depth == 0)
            type_aliases_.try_emplace(alias.name, copy_type(alias.type));
    for (const auto& function : parsed.functions) remember_function(*function);
    for (const auto& object : parsed.objects)
        known_ordinary_values_.insert(object->name);
    for (const auto& enumeration : parsed.enumerations) {
        enum_types_[enumeration.name] = enumeration.underlying;
        for (const auto& enumerator : enumeration.enumerators)
            known_ordinary_values_.insert(enumerator.name);
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

void Parser::parse_function_header_splice(
    Program& program, const std::string& name_space, std::size_t production_event,
    std::size_t header_index) {
    const auto first = index_;
    const auto item = tokens_[header_index];
    const auto previous_errors = diagnostics_.errors();
    // Record the published attribute trees now, but interpret all attributes
    // together with the retained header in the bounded parser below. This
    // keeps generic/ABI/variadic handling on the ordinary declaration path.
    const auto attributes = [&] {
        if (parsing_public_fragment_) {
            (void)parse_attributes(false, AttributeParseMode::SyntaxOnly);
            return true;
        }
        // Ordinary parsing merely transfers the written groups here. Running
        // their macros would both reorder header work and invalidate the
        // saved header index before the bounded parser receives the tokens.
        while (std::as_const(*this).current().is("[[")) {
            const auto end = bounded_group_end(index_);
            if (!end) {
                error_here("structured header requires balanced attributes");
                return false;
            }
            index_ = *end;
        }
        return true;
    };
    if (!attributes()) return;
    {
        ProductionScope header(*this, SyntaxProduction::FunctionHeader);
        ++index_;
        if (header.event < production_events_.size())
            production_events_[header.event].opaque = item.splice;
    }
    const auto trailing_first = index_;
    if (!attributes()) return;
    const auto header_end = index_;
    const bool header_only = parsing_public_function_header_;
    const auto compound_start = [](const Token& token) {
        return token.is("{") || (token.kind == TokenKind::StructuredSplice &&
            token.splice && syntax_compound_node(*token.splice));
    };
    bool body = compound_start(std::as_const(*this).current());
    if (parsing_public_fragment_ && !header_only && !body && !current().is(";")) {
        error_here("structured function header requires a body or ';'");
        return;
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
        return;
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
        auto end = header_only ? std::optional{header_end}
            : body && current().is("{") ? bounded_group_end(index_)
                                        : std::optional{index_ + 1};
        if (!end) {
            error_here("structured function header requires a balanced body");
            return;
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
        return;
    }
    const auto execution = syntax_->execution();
    auto output = execution->materialize_node(
        *item.splice, item.location, SyntaxParseCategory::FunctionHeader);
    if (!output) { discard_tail(); return; }
    const auto token_count = output->tokens.size() + (header_index - first) +
        (header_end - trailing_first);
    if (!execution->begin_fragment(item.location, token_count)) { discard_tail(); return; }
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
                                           *item.splice->context, item.splice->span.first);
    else child->restore_environment(*item.splice->context->parse_environment,
                                     *item.splice->context);
    const auto prior_records = child->record_types_;
    child->parsing_public_fragment_ = parsing_public_fragment_;
    child->parsing_public_function_header_ = true;
    Program parsed;
    child->parse_external(parsed, name_space);
    if (child->current().kind != TokenKind::End || parsed.functions.size() != 1 ||
        parsed.functions.front()->body || !parsed.objects.empty() ||
        !parsed.records.empty() || !parsed.enumerations.empty() ||
        !parsed.global_labels.empty()) {
        diagnostics_.error(item.location,
            "structured function header must contain one complete direct-function header");
        discard_tail();
        return;
    }
    if (diagnostics_.errors() != previous_errors ||
        !transfer_spliced_tags(*child, prior_records, item.location)) {
        discard_tail();
        return;
    }
    auto function = std::move(parsed.functions.front());
    remember_function(*function);
    if (!header_only) {
        struct Restore {
            Parser& parser;
            FunctionDecl* function;
            std::string name_space;
            std::vector<std::string> generic_types;
            ~Restore() {
                parser.active_function_ = function;
                parser.active_namespace_ = std::move(name_space);
                parser.active_generic_types_ = std::move(generic_types);
            }
        } restore{*this, active_function_, active_namespace_, active_generic_types_};
        active_function_ = function.get();
        active_namespace_ = name_space;
        active_generic_types_.clear();
        for (const auto& parameter : function->generic_parameters)
            if (!parameter.value_type) active_generic_types_.push_back(parameter.name);
        if (!parsing_public_fragment_) {
            // A textual token macro may supply the following body or semicolon,
            // but only after surviving header expansions have completed and
            // its generic context is available to the body invocation.
            body = compound_start(current());
            if (!body && !current().is(";")) {
                error_here("structured function header requires a body or ';'");
                return;
            }
        }
        if (body) function->body = parse_compound();
        else expect(";", "after structured function header");
    }
    program.functions.push_back(std::move(function));
    for (auto& assertion : child->static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

void Parser::parse_external(Program& program, const std::string& name_space) {
    struct NamespaceRestore {
        std::string& value;
        std::string previous;
        ~NamespaceRestore() { value = std::move(previous); }
    } restore{active_namespace_, active_namespace_};
    active_namespace_ = name_space;
    (void)current();
    drain_pending_tags(program);
    ProductionScope production(*this, parsing_public_fragment_
        ? SyntaxProduction::Declaration : SyntaxProduction::None);
    if (const auto header_position = function_header_splice_position()) {
        parse_function_header_splice(program, name_space, production.event, *header_position);
        return;
    }
    const auto expansion_head = expansion_function_head(tokens_, index_);
    if (parsing_public_fragment_ && (current().is("syntax") ||
        current().is("namespace") || current().is("using") ||
        current().is("$::static_assert") || macro_start() || active_syntax(true) ||
        expansion_head)) {
        error_here("parsed declaration requires a direct core declaration");
        ++index_;
        return;
    }
    if (current().kind == TokenKind::StructuredSplice) {
        if (parsing_public_fragment_) {
            const auto item = current();
            ++index_;
            if (!item.splice || (!syntax_declaration_node(*item.splice) &&
                                 !syntax_function_definition_node(*item.splice)))
                diagnostics_.error(item.location,
                    "structured syntax splice requires a declaration or function-definition node at external position");
            else if (recording_public_tree_ && production.event < production_events_.size())
                production_events_[production.event].opaque = item.splice;
        } else parse_external_node_splice(program, name_space);
        return;
    }
    if (syntax_ && expansion_head) {
        const bool generated = replacement_ || token_origin(current().location).context;
        if (generated) error_here("expansion output cannot introduce syntax registration");
        else {
            const auto context = syntax_context(current().location);
            if (context) (void)syntax_->execution()->define_function(tokens_, index_, active_namespace_,
                active_imports_, syntax_->bindings(), context->parse_environment);
            else { ++index_; synchronize_external(); }
        }
        if (generated) { ++index_; synchronize_external(); }
        return;
    }
    if (parse_syntax_registration(&program)) return;
    const auto* external_owner = active_syntax(true);
    if (external_owner) {
        const auto location = current().location;
        auto execution = syntax_->execution();
        if (!execution->begin_replacement(location)) {
            if (external_owner) diagnostics_.note(external_owner->location,
                "syntax '" + external_owner->name + "' defined here");
            ++index_;
            synchronize_external();
            return;
        }
        struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
        auto output = expand_at_position(true);
        if (!output) return;
        auto child = replacement_parser(std::move(*output));
        while (child->current().kind != TokenKind::End) {
            const auto before = child->index_;
            child->parse_external(program, name_space);
            if (before == child->index_) ++child->index_;
        }
        adopt_replacement(*child);
        return;
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
    prepare_header();
    if (allow_public_header_deferral_)
        (void)preview_generic_types(&public_header_uncertain_names_);
    active_generic_types_ = preview_generic_types();
    auto attributes = parse_attributes();
    if (current().is("$::static_assert")) {
        if (!attributes.empty()) error_here("attributes are not valid on $::static_assert");
        (void)parse_static_assertion();
        return;
    }
    if (consume("namespace")) {
        auto nested = parse_qualified_name();
        if (!nested || !expect("{")) { synchronize_external(); return; }
        const auto full = join_namespace(name_space, *nested);
        const auto saved_imports = active_imports_;
        const auto saved_scope_imports = current_scope_imports_;
        current_scope_imports_ = 0;
        if (syntax_) syntax_->push_scope();
        active_namespace_ = full;
        while (!current().is("}") && current().kind != TokenKind::End) {
            const auto before = index_;
            parse_external(program, full);
            if (before == index_) ++index_;
        }
        expect("}");
        if (syntax_) syntax_->pop_scope();
        active_imports_ = saved_imports;
        current_scope_imports_ = saved_scope_imports;
        return;
    }
    if (consume("using")) {
        if (auto imported = parse_qualified_name()) {
            if (syntax_) syntax_->import(*imported);
            active_imports_.insert(active_imports_.begin() + static_cast<std::ptrdiff_t>(current_scope_imports_), std::move(*imported));
            ++current_scope_imports_;
        }
        else error_here("expected namespace name after 'using'");
        expect(";");
        return;
    }
    const auto standalone_tag = [&](std::string_view tag) {
        if (!current().is(tag) || current(1).kind != TokenKind::Identifier)
            return false;
        std::size_t at = 2;
        while (current(at).is("::") && current(at + 1).kind == TokenKind::Identifier)
            at += 2;
        while (current(at).is("[[")) {
            unsigned depth = 1;
            ++at;
            while (depth && current(at).kind != TokenKind::End) {
                if (current(at).is("[[")) ++depth;
                else if (current(at).is("]]")) --depth;
                ++at;
            }
            if (depth) return false;
        }
        if (current(at).is("{")) {
            unsigned depth = 1;
            ++at;
            while (depth && current(at).kind != TokenKind::End) {
                if (current(at).is("{")) ++depth;
                else if (current(at).is("}")) --depth;
                ++at;
            }
            if (depth) return false;
        }
        return current(at).is(";");
    };
    if (standalone_tag("enum")) {
        parse_enum_declaration(program, name_space, std::move(attributes));
        return;
    }
    if (standalone_tag("struct") || standalone_tag("union")) {
        parse_record_declaration(program, name_space, std::move(attributes));
        return;
    }
    Linkage linkage = Linkage::Group;
    bool linkage_seen = false;
    bool inline_hint = false;
    bool typedef_seen = false;
    const auto consume_storage = [&]() -> bool {
        const auto location = current().location;
        if (consume("typedef")) {
            if (typedef_seen || linkage_seen || inline_hint)
                diagnostics_.error(location, "typedef cannot combine with another storage specifier");
            typedef_seen = true;
            return true;
        }
        if (consume("inline")) {
            if (typedef_seen)
                diagnostics_.error(location, "typedef cannot combine with another storage specifier");
            inline_hint = true;
            return true;
        }
        Linkage selected;
        if (consume("global")) selected = Linkage::Global;
        else if (consume("static")) selected = Linkage::Static;
        else return false;
        if (typedef_seen)
            diagnostics_.error(location, "typedef cannot combine with another storage specifier");
        if (linkage_seen && linkage != selected)
            diagnostics_.error(location, "declaration cannot be both 'global' and 'static'");
        linkage = selected;
        linkage_seen = true;
        return true;
    };
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    while (current().is("typedef") || current().is("global") ||
           current().is("static") || current().is("inline")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        (void)consume_storage();
    }

    if (!typedef_seen && linkage == Linkage::Global && current().is("label") &&
        current(1).kind == TokenKind::Identifier && current(2).is("::")) {
        if (inline_hint) {
            diagnostics_.error(current().location,
                               "'inline' is valid only on a function");
        }
        parse_global_label_declaration(program, name_space,
                                       std::move(attributes));
        return;
    }

    if (!type_start() && !current().is("[[")) {
        if (const auto message = familiar_c_spelling(current().text)) {
            error_here(*message);
        } else {
            error_here(typedef_seen ? "expected aliased type after 'typedef'"
                                    : "expected declaration");
        }
        synchronize_external();
        return;
    }
    const auto location = current().location;
    auto base_type = parse_type(false, consume_storage, &attributes);
    specifiers.finish();
    if (typedef_seen) {
        parse_typedef(name_space, std::move(attributes), std::move(base_type), location);
        return;
    }
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    unsigned ordinal = 0;
    bool last_function = false;
    for (;;) {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        std::optional<std::string> name;
        SourceLocation name_location;
        std::vector<FunctionDecl::GenericParameter> angle_parameters;
        auto type = parse_declarator(copy_type(base_type), name, DeclaratorContext::Named, nullptr, &name_location,
                                     &angle_parameters);
        if (!type || !name) {
            if (!name) error_here("expected declaration name");
            synchronize_external();
            return;
        }
        *name = join_namespace(name_space, *name);
        last_function = type->kind == Type::Kind::Function && type->function;
        if (last_function) {
            auto signature = type->function;
            auto result_type = signature->result;
            auto function = parse_function(
                location, std::move(*name), name_space, std::move(result_type),
                linkage, inline_hint, attributes, std::move(signature),
                std::move(angle_parameters));
            if (function) function->fresh = token_origin(name_location).fresh;
            if (function) {
                remember_function(*function);
            }
            const bool compound_start = current().is("{") ||
                (current().kind == TokenKind::StructuredSplice && current().splice &&
                 syntax_compound_node(*current().splice));
            if (function && (parsing_public_function_header_ || compound_start)) {
                if (ordinal != 0)
                    error_here("a function definition requires a single declarator");
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
                            error_here("deferred function requires a balanced body");
                            return;
                        }
                        function->body = std::make_unique<Statement>();
                        function->body->kind = Statement::Kind::Compound;
                        function->body->location = current().location;
                        index_ = *end;
                    } else {
                        auto* previous_function = active_function_;
                        active_function_ = function.get();
                        function->body = parse_compound();
                        active_function_ = previous_function;
                    }
                }
                program.functions.push_back(std::move(function));
                return;
            }
            if (function) program.functions.push_back(std::move(function));
        } else {
            known_ordinary_values_.insert(*name);
            if (!angle_parameters.empty())
                diagnostics_.error(location,
                                   "angle generic parameters require a direct function declaration");
            auto item_attributes = attributes;
            auto trailing = parse_attributes();
            item_attributes.insert(item_attributes.end(),
                std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
            apply_callable_attributes(type, item_attributes);
            if (inline_hint)
                diagnostics_.error(location, "'inline' is valid only on a function");
            auto object = parse_object(location, std::move(*name), std::move(type),
                                       linkage, std::move(item_attributes), false);
            if (object) object->fresh = token_origin(name_location).fresh;
            if (object) program.objects.push_back(std::move(object));
        }
        item.finish();
        ++ordinal;
        if (!consume(",")) break;
    }
    list.finish();
    if (last_function && !current().is(";")) {
        error_here("expected ';' or function body");
        synchronize_external();
    } else {
        expect(";", "after declaration");
    }
}

void Parser::parse_global_label_declaration(
    Program& program, const std::string& name_space,
    std::vector<Attribute> attributes) {
    const auto location = current().location;
    consume("label");
    const auto first_name = index_;
    auto name = parse_qualified_name();
    if (!name || name->find("::") == std::string::npos) {
        diagnostics_.error(location,
                           "a global label declaration requires a qualified label name");
        synchronize_external();
        return;
    }
    auto owner_fresh = index_ >= first_name + 3
        ? token_origin(tokens_[index_ - 3].location).fresh : nullptr;
    auto label_fresh = token_origin(tokens_[index_ - 1].location).fresh;
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    expect(";", "after global label declaration");
    program.global_labels.push_back(
        {location, join_namespace(name_space, *name), std::move(owner_fresh),
         std::move(label_fresh), std::move(attributes)});
}

void Parser::parse_enum_declaration(Program& program,
                                    const std::string& name_space,
                                    std::vector<Attribute> attributes) {
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
    ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
    ProductionScope enumeration(*this, SyntaxProduction::EnumSpecifier);
    const auto location = current().location;
    consume("enum");
    auto name = parse_qualified_name();
    if (!name) {
        error_here("expected enumeration name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    const auto underlying = enum_underlying(attributes);
    EnumDecl declaration;
    declaration.location = location;
    declaration.name = *name;
    declaration.underlying = underlying;
    declaration.attributes = std::move(attributes);
    parse_enumerators(declaration, name_space);
    enumeration.finish();
    type_specifier.finish();
    specifier.finish();
    specifiers.finish();
    expect(";", "after enumeration declaration");

    const auto found = enum_types_.find(*name);
    if (found != enum_types_.end() && found->second != underlying) {
        diagnostics_.error(location,
                           "enumeration '" + *name +
                               "' redeclared with a different underlying type");
    } else {
        enum_types_[*name] = underlying;
    }
    program.enumerations.push_back(std::move(declaration));
}

BuiltinType Parser::enum_underlying(const std::vector<Attribute>& attributes) {
    BuiltinType underlying = BuiltinType::I32;
    bool underlying_seen = false;
    for (const auto& attribute : attributes) {
        if (attribute.name != "underlying") continue;
        if (underlying_seen) {
            diagnostics_.error(attribute.location,
                               "enumeration has more than one 'underlying' attribute");
            continue;
        }
        underlying_seen = true;
        if (attribute.arguments.size() != 1) {
            diagnostics_.error(attribute.location,
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
            diagnostics_.error(attribute.location,
                               "'underlying' requires a non-bool integer type");
            continue;
        }
        underlying = *kind;
    }
    return underlying;
}

void Parser::parse_enumerators(EnumDecl& declaration, const std::string& name_space) {
    if (!consume("{")) return;
    while (!current().is("}") && current().kind != TokenKind::End) {
        ProductionScope entry(*this, SyntaxProduction::Enumerator);
        expand_inline_macro_fragments();
        const auto token = consume_kind(TokenKind::Identifier);
        if (!token) {
            error_here("expected enumerator name");
            while (!current().is(",") && !current().is("}") &&
                   current().kind != TokenKind::End) {
                ++index_;
            }
        } else {
            EnumDecl::Enumerator enumerator;
            enumerator.location = token->location;
            enumerator.name = join_namespace(name_space, identifier_binding_name(*token));
            known_ordinary_values_.insert(enumerator.name);
            if (consume("=")) enumerator.initializer = parse_constant_expression();
            declaration.enumerators.push_back(std::move(enumerator));
        }
        entry.finish();
        if (!consume(",")) break;
    }
    expect("}", "after enumeration definition");
}

void Parser::parse_record_declaration(
    Program& program, const std::string& name_space,
    std::vector<Attribute> attributes) {
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
    ProductionScope type_specifier(*this, SyntaxProduction::TypeSpecifier);
    ProductionScope record(*this, SyntaxProduction::StructOrUnionSpecifier);
    const auto location = current().location;
    const bool is_union = consume("union");
    if (!is_union) consume("struct");
    auto name = parse_qualified_name();
    if (!name) {
        error_here("expected record name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    auto [tag, inserted] = record_types_.emplace(
        *name, RecordTag{is_union, false});
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
    if (consume("{")) {
        declaration.complete = true;
        if (!inserted && tag->second.complete) {
            diagnostics_.error(location,
                               "duplicate definition of record '" + *name + "'");
        }
        tag->second.complete = true;
        parse_record_members(declaration);
    }
    record.finish();
    type_specifier.finish();
    specifier.finish();
    specifiers.finish();
    expect(";", "after record declaration");
    program.records.push_back(std::move(declaration));
}

void Parser::parse_record_members(RecordDecl& declaration) {
    while (!current().is("}") && current().kind != TokenKind::End) {
        ProductionScope member(*this, SyntaxProduction::MemberDeclaration);
        auto member_attributes = parse_attributes();
        if (!type_start()) {
            error_here("expected record member declaration");
            while (!current().is(";") && !current().is("}") &&
                   current().kind != TokenKind::End) {
                ++index_;
            }
            consume(";");
            continue;
        }
        const auto member_location = current().location;
        auto base_type = parse_type(true, {}, &member_attributes);
        bool parsed_member = false;
        do {
            ProductionScope member_declarator(*this, SyntaxProduction::MemberDeclarator);
            std::optional<std::string> member_name;
            auto member_type = parse_declarator(copy_type(base_type), member_name);
            auto item_attributes = parse_attributes();
            item_attributes.insert(
                item_attributes.begin(), member_attributes.begin(),
                member_attributes.end());
            std::unique_ptr<Expr> bit_width;
            if (consume(":")) {
                bit_width = parse_constant_expression();
                auto trailing_attributes = parse_attributes();
                item_attributes.insert(
                    item_attributes.end(),
                    std::make_move_iterator(trailing_attributes.begin()),
                    std::make_move_iterator(trailing_attributes.end()));
            } else if (!member_name) {
                error_here("expected record member name");
                break;
            }
            declaration.members.push_back(
                {member_location, member_name.value_or(std::string{}),
                 std::move(member_type), std::move(bit_width),
                 std::move(item_attributes)});
            parsed_member = true;
        } while (consume(","));
        if (!parsed_member) {
            while (!current().is(";") && !current().is("}") &&
                   current().kind != TokenKind::End) {
                ++index_;
            }
        }
        expect(";", "after record member declaration");
    }
    expect("}", "after record definition");
}

bool Parser::parse_static_assertion() {
    ProductionScope production(*this, SyntaxProduction::StaticAssertDeclaration);
    const auto location = current().location;
    consume("$::static_assert");
    expect("(");
    auto condition = parse_constant_expression();
    expect(",");
    const auto message_token = consume_kind(TokenKind::String);
    if (!message_token) error_here("expected diagnostic string in $::static_assert");
    expect(")");
    expect(";");
    const auto message = message_token
                             ? decode_string_literal(message_token->text)
                                   .value_or("static assertion failed")
                             : std::string("static assertion failed");
    if (!condition) return false;
    StaticAssertDecl assertion{location, active_namespace_,
                               std::move(condition), std::move(message)};
    if (active_function_ &&
        !active_function_->generic_parameters.empty())
        active_function_->deferred_static_assertions.push_back(
            std::move(assertion));
    else
        static_assertions_.push_back(std::move(assertion));
    return true;
}

void Parser::parse_parameter_list(std::vector<ParameterDecl>& parameters, bool& variadic) {
    ProductionScope list(*this, SyntaxProduction::ParameterList);
    // A fragment may supply no parameters, the lone void spelling, a list,
    // or its final ellipsis. Decide only after expansion; speculative capture
    // still throws before running any nested invocation.
    expand_inline_macro_fragments();
    if (current().is(")")) return;
    if (current().is("void")) {
        const auto first = index_++;
        expand_inline_macro_fragments();
        index_ = first;
        if (current(1).is(")")) { ++index_; return; }
    }
    unsigned ordinal = 0;
    for (;;) {
        expand_inline_macro_fragments();
        if (consume("...")) {
            variadic = true;
            expand_inline_macro_fragments();
            return;
        }
        const auto before = index_;
        parameters.push_back(parse_parameter(ordinal++));
        // Empty output after a comma is not an empty list: it leaves an
        // invalid trailing comma and must diagnose as an absent parameter.
        if (index_ == before || !consume(",")) return;
    }
}

ParameterDecl Parser::parse_parameter(unsigned ordinal) {
    expand_inline_macro_fragments();
    ProductionScope production(*this, SyntaxProduction::ParameterDeclaration);
    ParameterDecl parameter;
    parameter.location = current().location;
    auto attributes = parse_attributes();
    expand_inline_macro_fragments();
    if (current().is("in") || current().is("out") || current().is("inout")) {
        ProductionScope mode(*this, SyntaxProduction::ParameterMode);
        if (consume("in")) parameter.mode = ParameterMode::In;
        else if (consume("out")) parameter.mode = ParameterMode::Out;
        else { consume("inout"); parameter.mode = ParameterMode::InOut; }
        parameter.explicit_mode = true;
    }
    expand_inline_macro_fragments();
    parameter.type = parse_type(true, {}, &attributes);
    std::optional<std::string> name;
    parameter.type = parse_declarator(std::move(parameter.type), name, DeclaratorContext::Parameter,
                                      nullptr, &parameter.location);
    if (parameter.mode != ParameterMode::In && parameter.type &&
        parameter.type->is_const) {
        diagnostics_.error(parameter.location,
                           "'out' and 'inout' parameter cells cannot be const");
    }
    parameter.name = name.value_or("_parameter" + std::to_string(ordinal));
    apply_callable_attributes(parameter.type, attributes);
    if (current().kind == TokenKind::String) {
        ProductionScope location_production(*this, SyntaxProduction::Location);
        const auto location = consume_kind(TokenKind::String);
        parameter.location_name = decode_string_literal(location->text);
        if (!parameter.location_name) diagnostics_.error(location->location, "invalid location string");
    }
    auto trailing = parse_attributes();
    apply_callable_attributes(parameter.type, trailing);
    return parameter;
}

std::unique_ptr<FunctionDecl>
Parser::parse_function(SourceLocation location, std::string name,
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
        expect("(");
        parse_parameter_list(function->parameters, function->variadic);
        expect(")");
    }
    if (consume("->")) {
        const auto location_token = consume_kind(TokenKind::String);
        if (!location_token)
            error_here("expected result location string after '->'");
        else {
            function->result_location =
                decode_string_literal(location_token->text);
            if (!function->result_location) {
                diagnostics_.error(location_token->location,
                                   "invalid result location string");
            }
        }
    }
    auto trailing = parse_attributes();
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
    // Attributes preceding a function header are parsed before its generic
    // parameter list becomes part of the active function. Bind their scalar
    // expressions now, without rebinding a copied token that already has an
    // immutable source context of its own.
    const auto bind_attribute_name = [&](const auto& self, Expr& expression) -> void {
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
        for (auto& argument : expression.generic_arguments)
            if (argument.value) self(self, *argument.value);
    };
    for (auto& attribute : function->attributes)
        if (attribute.expression_argument)
            bind_attribute_name(bind_attribute_name, *attribute.expression_argument);
    // The containing declaration owns its separators or the function body.
    return function;
}

std::unique_ptr<ObjectDecl> Parser::parse_object(
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
    if (consume("=")) object->initializer = parse_initializer();
    if (object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 && object->initializer &&
        object->initializer->kind == Expr::Kind::String &&
        object->type->element &&
        object->type->element->kind == Type::Kind::Builtin &&
        object->type->element->builtin == BuiltinType::U8) {
        object->type->lanes = static_cast<std::uint32_t>(
            object->initializer->string_value.size() + 1);
    }
    if (object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 &&
        !object->initializer) {
        diagnostics_.error(location,
                           "an omitted array bound requires an initializer");
    }
    if (consume_semicolon) expect(";");
    return object;
}

bool Parser::local_declaration_start() {
    return current().is("register") || current().is("stack") ||
           current().is("static") || current().is("typedef") ||
           type_start(TypeProbe::ExpressionAlternative);
}

std::unique_ptr<Statement>
Parser::parse_local_declaration(std::vector<Attribute> attributes,
                                bool consume_semicolon,
                                SyntaxProduction production_name) {
    ProductionScope production(*this, production_name);
    const auto location = current().location;
    bool storage_register = false;
    bool storage_stack = false;
    bool storage_static = false;
    bool typedef_seen = false;
    const auto consume_storage = [&]() -> bool {
        if (current().is("typedef")) {
            const auto storage_location = current().location;
            ++index_;
            if (typedef_seen || storage_register || storage_stack || storage_static)
                diagnostics_.error(storage_location,
                    "typedef cannot combine with another storage specifier");
            typedef_seen = true;
            return true;
        }
        bool* selected = nullptr;
        if (current().is("register")) selected = &storage_register;
        else if (current().is("stack")) selected = &storage_stack;
        else if (current().is("static")) selected = &storage_static;
        if (!selected) return false;
        const auto location = current().location;
        ++index_;
        if (typedef_seen)
            diagnostics_.error(location, "typedef cannot combine with another storage specifier");
        else if (storage_register || storage_stack || storage_static)
            diagnostics_.error(location, "local declaration has more than one storage specifier");
        *selected = true;
        return true;
    };
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    if (current().is("typedef") || current().is("register") ||
        current().is("stack") || current().is("static")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        (void)consume_storage();
    }
    auto base_type = parse_type(false, consume_storage, &attributes);
    specifiers.finish();
    if (typedef_seen) {
        parse_typedef({}, std::move(attributes), std::move(base_type), location,
                      consume_semicolon);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = location;
        return statement;
    }
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    auto result = std::make_unique<Statement>();
    result->kind = Statement::Kind::DeclarationList;
    result->location = location;
    do {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Declaration;
        statement->location = current().location;
        statement->declaration = std::make_unique<VariableDecl>();
        auto& declaration = *statement->declaration;
        declaration.location = current().location;
        declaration.storage_register = storage_register;
        declaration.storage_stack = storage_stack;
        declaration.storage_static = storage_static;
        std::optional<std::string> name;
        declaration.type = parse_declarator(copy_type(base_type), name, DeclaratorContext::Named,
            &declaration.dynamic_array_bound, &declaration.location);
        if (!name) error_here("expected local variable name");
        else {
            declaration.name = *name;
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
        if (current().kind == TokenKind::String) {
            ProductionScope location_production(*this, SyntaxProduction::ObjectLocation);
            const auto location_token = consume_kind(TokenKind::String);
            declaration.location_name = decode_string_literal(location_token->text);
            if (!declaration.location_name)
                diagnostics_.error(location_token->location, "invalid location string");
        }
        declaration.attributes = attributes;
        auto trailing = parse_attributes();
        declaration.attributes.insert(declaration.attributes.end(),
            std::make_move_iterator(trailing.begin()), std::make_move_iterator(trailing.end()));
        for (const auto& attribute : declaration.attributes) {
            if (attribute.name != "aligned") {
                diagnostics_.error(attribute.location,
                    "attribute '" + attribute.name + "' is not valid on a local object");
            }
            // Target-dependent constants are resolved after nominal HIR layout.
        }
        if (consume("=")) declaration.initializer = parse_initializer();
        if (declaration.type && declaration.type->kind == Type::Kind::Array &&
            declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
            declaration.initializer && declaration.initializer->kind == Expr::Kind::String &&
            declaration.type->element && declaration.type->element->kind == Type::Kind::Builtin &&
            declaration.type->element->builtin == BuiltinType::U8) {
            declaration.type->lanes = static_cast<std::uint32_t>(
                declaration.initializer->string_value.size() + 1);
        }
        if (declaration.type && declaration.type->kind == Type::Kind::Array &&
            declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
            (!declaration.initializer ||
             declaration.initializer->kind != Expr::Kind::AggregateInitializer)) {
            diagnostics_.error(declaration.location,
                               "an omitted array bound requires a u8 string initializer");
        }
        result->statements.push_back(std::move(statement));
    } while (consume(","));
    list.finish();
    if (consume_semicolon) expect(";");
    if (result->statements.size() == 1) return std::move(result->statements.front());
    return result;
}

std::unique_ptr<Statement> Parser::parse_compound() {
    ProductionScope production(*this, SyntaxProduction::CompoundStatement);
    if (current().kind == TokenKind::StructuredSplice) {
        const auto item = current();
        if (!item.splice || !syntax_compound_node(*item.splice)) {
            error_here("structured function body requires a compound-statement node");
            ++index_;
            return {};
        }
        if (parsing_public_fragment_) {
            if (production.event < production_events_.size())
                production_events_[production.event].opaque = item.splice;
            ++index_;
            auto body = std::make_unique<Statement>();
            body->kind = Statement::Kind::Compound;
            body->location = item.location;
            return body;
        }
        auto body = parse_statement();
        if (body && body->kind != Statement::Kind::Compound)
            diagnostics_.error(item.location,
                "structured function body must contain one complete compound statement");
        return body;
    }
    const auto first = index_;
    const auto production_depth = production_stack_.size();
    std::optional<std::size_t> end_input;
    std::shared_ptr<const SyntaxContext> context;
    if (parsing_public_fragment_ && !public_tree_failed_) {
        if (const auto end = bounded_group_end(first)) end_input = public_input_indices_[*end];
        context = public_fragment_context(first);
    }
    if (parsing_public_fragment_ && public_tree_failed_) {
        // A failed ProductionScope has no event ID. Do not publish or index
        // a deferred event after resource exhaustion; the caller reports it.
        auto failed = std::make_unique<Statement>();
        failed->kind = Statement::Kind::Compound;
        failed->location = current().location;
        return failed;
    }
    const auto saved_imports = active_imports_;
    const auto saved_scope_imports = current_scope_imports_;
    const auto saved_values_depth = local_scopes_.size();
    const auto saved_types_depth = local_type_scopes_.size();
    const auto saved_uncertain_count = public_uncertain_binding_depths_.size();
    const auto saved_recording = recording_public_tree_;
    const auto saved_generic_argument = parsing_generic_argument_;
    const auto saved_generic_types = parsing_public_fragment_
        ? active_generic_types_ : std::vector<std::string>{};
    const auto saved_switch_depth = switch_depth_;
    const auto saved_switch_defaults = parsing_public_fragment_
        ? switch_default_seen_ : std::vector<bool>{};
    current_scope_imports_ = 0;
    local_scopes_.emplace_back();
    local_type_scopes_.emplace_back();
    scope_origins_.push_back(token_origin(current().location).identity);
    if (syntax_) syntax_->push_scope();
    if (local_scopes_.size() == 1 && active_function_)
        for (const auto& parameter : active_function_->parameters) {
            if (!local_scopes_.back().emplace(NameKey(parameter.name, parameter.location),
                    name_key(parameter).binding).second)
                diagnostics_.error(parameter.location,
                    "duplicate parameter name '" + parameter.name + "'");
        }
    if (local_scopes_.size() == 1 && active_function_)
        for (const auto& attribute : active_function_->attributes)
            if (attribute.name == "variadic")
                for (const auto& name : attribute.variadic_names) {
                    if (!local_scopes_.back().emplace(NameKey(name.name, name.location),
                            name_key(name).binding).second)
                        diagnostics_.error(name.location,
                            "variadic state binding '" + name.name +
                            "' conflicts with another local name");
                }
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Compound;
    statement->location = current().location;
    try {
        expect("{");
        while (!current().is("}") && current().kind != TokenKind::End) {
            const auto before = index_;
            const auto statement_origin = token_origin(current().location).span;
            const bool record_bindings = syntax_ && !parsing_public_fragment_ &&
                statement_origin.valid() && scope_origins_.back().source_unit;
            NameSet prior_values;
            NameSet prior_aliases;
            if (record_bindings) {
                for (const auto& [key, binding] : local_scopes_.back()) {
                    (void)binding;
                    prior_values.insert(key);
                }
                for (const auto& [key, type] : local_type_scopes_.back()) {
                    (void)type;
                    prior_aliases.insert(key);
                }
            }
            if (current().is("syntax") && (syntax_ || token_origin(current().location).context)) {
                auto declaration = std::make_unique<Statement>();
                declaration->kind = Statement::Kind::Empty;
                declaration->location = current().location;
                (void)parse_syntax_registration(nullptr);
                statement->statements.push_back(std::move(declaration));
            } else if (current().is("using")) {
                ProductionScope using_declaration(*this, SyntaxProduction::UsingDeclaration);
                consume("using");
                auto declaration = std::make_unique<Statement>();
                declaration->kind = Statement::Kind::Empty;
                declaration->location = tokens_[index_ - 1].location;
                if (auto imported = parse_qualified_name(SyntaxProduction::NamespaceName)) {
                    if (syntax_) syntax_->import(*imported);
                    active_imports_.insert(active_imports_.begin() +
                        static_cast<std::ptrdiff_t>(current_scope_imports_),
                        std::move(*imported));
                    ++current_scope_imports_;
                } else error_here("expected namespace name after 'using'");
                expect(";", "after using declaration");
                statement->statements.push_back(std::move(declaration));
            } else statement->statements.push_back(parse_statement());
            if (record_bindings) {
                ScopeEvent event;
                event.block = scope_origins_.back();
                event.statement = statement_origin;
                for (const auto& [key, binding] : local_scopes_.back())
                    if (!prior_values.contains(key)) event.values.emplace(key, binding);
                for (const auto& [key, type] : local_type_scopes_.back())
                    if (!prior_aliases.contains(key))
                        event.aliases.emplace(key, copy_type(type));
                if (!event.values.empty() || !event.aliases.empty())
                    scope_events_->push_back(std::move(event));
            }
            if (index_ == before && current().kind != TokenKind::End) ++index_;
        }
        expect("}");
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
    current_scope_imports_ = saved_scope_imports;
    local_scopes_.resize(saved_values_depth);
    local_type_scopes_.resize(saved_types_depth);
    scope_origins_.resize(saved_values_depth);
    public_uncertain_binding_depths_.resize(saved_uncertain_count);
    if (syntax_) syntax_->pop_scope();
    return statement;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    ProductionScope production(*this, SyntaxProduction::Statement);
    if (current().kind == TokenKind::StructuredSplice &&
        (!current().splice || !syntax_type_node(*current().splice))) {
        const auto item = current();
        ++index_;
        auto invalid = std::make_unique<Statement>();
        invalid->kind = Statement::Kind::Empty;
        invalid->location = item.location;
        const bool declaration_node = item.splice && syntax_declaration_node(*item.splice);
        if (!item.splice || (!syntax_statement_node(*item.splice) && !declaration_node)) {
            diagnostics_.error(item.location,
                "structured syntax splice requires a statement node at statement position");
            return invalid;
        }
        if (!syntax_ || !item.splice->context ||
            !item.splice->context->parse_environment) {
            diagnostics_.error(item.location,
                "structured syntax splice has no retained parse environment");
            return invalid;
        }
        if (recording_public_tree_ && production.event < production_events_.size()) {
            const bool whole_statement = item.splice->kind == SyntaxNode::Kind::Core
                ? item.splice->production == SyntaxProduction::Statement
                : item.splice->slot_production == SyntaxProduction::Statement;
            if (whole_statement)
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
                production_events_[production.event].opaque = std::move(wrapper);
            }
        }
        // Recognition keeps the original statement node without executing
        // opaque descendants. Surviving output is parsed in its saved lookup
        // environment, then only declarations made by this statement enter
        // the destination block.
        if (parsing_public_fragment_) return invalid;
        if ((local_scopes_.empty() || local_type_scopes_.empty()) &&
            !syntax_compound_node(*item.splice)) {
            diagnostics_.error(item.location,
                "structured statement splice requires a destination block");
            return invalid;
        }
        auto output = syntax_->execution()->materialize_node(
            *item.splice, item.location, declaration_node
                ? SyntaxParseCategory::Declaration : SyntaxParseCategory::Statement);
        if (!output) return invalid;
        auto child = replacement_parser(std::move(*output));
        if (item.splice->kind == SyntaxNode::Kind::Deferred)
            child->restore_deferred_environment(
                *item.splice->context->parse_environment,
                *item.splice->context, item.splice->span.first);
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
            child->scope_origins_.emplace_back();
        }
        const auto prior_values = child->local_scopes_.empty()
            ? NameMap<ValueBinding>{} : child->local_scopes_.back();
        const auto prior_aliases = child->local_type_scopes_.empty()
            ? NameMap<TypePtr>{} : child->local_type_scopes_.back();
        const auto prior_records = child->record_types_;
        const auto previous_errors = diagnostics_.errors();
        auto statement = child->parse_statement();
        if (child->current().kind != TokenKind::End)
            child->error_here("structured statement splice must contain one complete statement");
        if (diagnostics_.errors() != previous_errors || !statement) return invalid;
        // A compound owns an inner scope. Only a direct declaration statement
        // can add bindings to the surrounding destination block.
        if (!syntax_compound_node(*item.splice)) {
            for (const auto& [key, binding] : child->local_scopes_.back()) {
                if (prior_values.contains(key)) continue;
                if (local_type_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced local value '" + key.spelling + "' conflicts with a destination typedef");
                else if (local_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced local value '" + key.spelling + "' was declared more than once");
                else local_scopes_.back().emplace(key, binding);
            }
            for (const auto& [key, type] : child->local_type_scopes_.back()) {
                if (prior_aliases.contains(key)) continue;
                if (local_scopes_.back().contains(key))
                    diagnostics_.error(item.location,
                        "spliced typedef '" + key.spelling + "' conflicts with a destination value");
                else if (const auto found = local_type_scopes_.back().find(key);
                         found != local_type_scopes_.back().end()) {
                    if (!same_type(found->second, type))
                        diagnostics_.error(item.location,
                            "spliced typedef '" + key.spelling + "' has a different destination type");
                } else local_type_scopes_.back().emplace(key, copy_type(type));
            }
        }
        if (diagnostics_.errors() != previous_errors) return invalid;
        if (!transfer_spliced_tags(*child, prior_records, item.location)) return invalid;
        switch_default_seen_ = std::move(child->switch_default_seen_);
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
        }
        return statement;
    }
    auto attributes = parse_attributes();
    return parse_unattributed_statement(std::move(attributes));
}

std::unique_ptr<Statement> Parser::parse_unattributed_statement(
    std::vector<Attribute> attributes) {
    ProductionScope production(*this, SyntaxProduction::UnattributedStatement);
    if (!attributes.empty()) {
        if (current().is("global") && current(1).is("label") &&
            current(2).kind == TokenKind::Identifier && current(3).is(":"))
            return parse_global_label_statement(std::move(attributes));
        if (local_declaration_start())
            return parse_local_declaration(std::move(attributes));
        if (current().is("return")) {
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
    if (current().is("syntax") && (syntax_ || token_origin(current().location).context)) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = current().location;
        error_here(replacement_ || token_origin(current().location).context
            ? "expansion output cannot introduce syntax registration"
            : "syntax registration requires an external item or compound block item position");
        ++index_;
        synchronize_external();
        return statement;
    }
    if (parsing_public_fragment_ && macro_start()) {
        auto placeholder = std::make_unique<Statement>();
        placeholder->kind = Statement::Kind::Empty;
        placeholder->location = current().location;
        const auto first = index_;
        const auto events = production_events_.size();
        if (!parse_opaque_invocation(SyntaxKind::Statement)) {
            error_here("could not recognize a bounded opaque statement invocation");
            return placeholder;
        }
        // An explicit operator/suffix after a macro places it inside a core
        // expression statement. A standalone invocation stays opaque; its
        // output is classified only after the owner has expanded.
        if (opaque_statement_has_expression_continuation()) {
            index_ = first;
            production_events_.resize(events);
            if (!production_stack_.empty())
                production_events_[production_stack_.back()].children.pop_back();
            ProductionScope expression_statement(*this, SyntaxProduction::ExpressionStatement);
            placeholder->kind = Statement::Kind::Expression;
            placeholder->expression = parse_expression();
            expect(";", "after expression statement");
        } else mark_public_binding_uncertainty();
        return placeholder;
    }
    if (const auto* definition = active_syntax(false)) {
        if (definition->kind == SyntaxKind::Statement) {
            if (!parsing_public_fragment_) return parse_statement_replacement();
            auto placeholder = std::make_unique<Statement>();
            placeholder->kind = Statement::Kind::Empty;
            placeholder->location = current().location;
            if (!parse_opaque_invocation(SyntaxKind::Statement))
                error_here("could not recognize a bounded opaque statement invocation");
            mark_public_binding_uncertainty();
            return placeholder;
        }
        ProductionScope expression_statement(*this, SyntaxProduction::ExpressionStatement);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Expression;
        statement->location = current().location;
        statement->expression = parse_expression();
        expect(";", "after expression statement");
        return statement;
    }
    if (current().is("{")) return parse_compound();
    if (current().is("$::static_assert")) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = current().location;
        (void)parse_static_assertion();
        return statement;
    }
    if (current().is("global") && current(1).is("label") &&
        current(2).kind == TokenKind::Identifier && current(3).is(":")) {
        return parse_global_label_statement();
    }
    if ((current().kind == TokenKind::Identifier && !current().is("default") && current(1).is(":")) ||
        (current().is("label") && current(1).kind == TokenKind::Identifier && current(2).is(":"))) {
        ProductionScope labeled_statement(*this, SyntaxProduction::LabeledStatement);
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Label;
        statement->location = current().location;
        consume("label");
        statement->label_name = identifier_binding_name(current());
        statement->label_fresh = token_origin(current().location).fresh;
        ++index_;
        expect(":");
        if (parsing_public_fragment_ ||
            (current().kind != TokenKind::End && !current().is("}")))
            statement->first = parse_statement();
        return statement;
    }
    if (local_declaration_start()) return parse_local_declaration();
    const auto selected_production = current().is("if") || current().is("switch")
        ? SyntaxProduction::SelectionStatement
        : current().is("while") || current().is("do") || current().is("for")
            ? SyntaxProduction::IterationStatement
        : current().is("return") || current().is("goto") || current().is("break") ||
          current().is("continue") ? SyntaxProduction::JumpStatement
        : current().is("case") || current().is("default") ? SyntaxProduction::LabeledStatement
        : SyntaxProduction::ExpressionStatement;
    ProductionScope selected(*this, selected_production);
    const auto location = current().location;
    auto statement = std::make_unique<Statement>();
    statement->location = location;
    if (consume(";")) { statement->kind = Statement::Kind::Empty; return statement; }
    if (consume("return")) {
        statement->kind = Statement::Kind::Return;
        statement->attributes = std::move(attributes);
        if (!current().is(";")) statement->expression = parse_expression();
        expect(";");
        return statement;
    }
    if (consume("goto")) {
        statement->kind = Statement::Kind::Goto;
        statement->expression = parse_assignment();
        expect(";");
        return statement;
    }
    if (consume("if")) {
        statement->kind = Statement::Kind::If;
        expect("("); statement->condition = parse_expression(); expect(")");
        statement->first = parse_statement();
        if (consume("else")) statement->second = parse_statement();
        return statement;
    }
    if (consume("switch")) {
        statement->kind = Statement::Kind::Switch;
        expect("("); statement->condition = parse_expression(); expect(")");
        ++switch_depth_;
        switch_default_seen_.push_back(false);
        statement->first = parse_statement();
        switch_default_seen_.pop_back();
        --switch_depth_;
        return statement;
    }
    if (consume("while")) {
        statement->kind = Statement::Kind::While;
        expect("("); statement->condition = parse_expression(); expect(")");
        statement->first = parse_statement();
        return statement;
    }
    if (consume("do")) {
        statement->kind = Statement::Kind::DoWhile;
        statement->first = parse_statement();
        expect("while", "after 'do' body");
        expect("("); statement->condition = parse_expression(); expect(")");
        expect(";");
        return statement;
    }
    if (consume("for")) {
        statement->kind = Statement::Kind::For;
        const auto saved_uncertain_count = public_uncertain_binding_depths_.size();
        local_scopes_.emplace_back();
        local_type_scopes_.emplace_back();
        scope_origins_.push_back(token_origin(current().location).identity);
        expect("(");
        {
            ProductionScope initializer(*this, SyntaxProduction::ForInitializer);
            if (current().is(";")) {
                statement->first = std::make_unique<Statement>();
                statement->first->kind = Statement::Kind::Empty;
                statement->first->location = location;
            } else if (local_declaration_start()) {
                statement->first = parse_local_declaration({}, false,
                    SyntaxProduction::DeclarationWithoutFinalSemicolon);
            } else if (current().is("[[")) {
                ProductionScope declaration(*this, SyntaxProduction::DeclarationWithoutFinalSemicolon);
                auto initializer_attributes = parse_attributes();
                if (local_declaration_start()) {
                    statement->first = parse_local_declaration(
                        std::move(initializer_attributes), false, SyntaxProduction::None);
                } else {
                    for (const auto& attribute : initializer_attributes) {
                        diagnostics_.error(attribute.location,
                            "attribute '" + attribute.name + "' is not valid on a for initializer");
                    }
                    statement->first = std::make_unique<Statement>();
                    statement->first->kind = Statement::Kind::Expression;
                    statement->first->location = current().location;
                    statement->first->expression = parse_expression();
                }
            } else {
                statement->first = std::make_unique<Statement>();
                statement->first->kind = Statement::Kind::Expression;
                statement->first->location = current().location;
                statement->first->expression = parse_expression();
            }
        }
        expect(";");
        if (!current().is(";")) statement->condition = parse_expression();
        expect(";");
        if (!current().is(")")) statement->increment = parse_expression();
        expect(")");
        statement->second = parse_statement();
        local_scopes_.pop_back();
        local_type_scopes_.pop_back();
        scope_origins_.pop_back();
        public_uncertain_binding_depths_.resize(saved_uncertain_count);
        return statement;
    }
    if (consume("break")) {
        statement->kind = Statement::Kind::Break; expect(";"); return statement;
    }
    if (consume("continue")) {
        statement->kind = Statement::Kind::Continue; expect(";"); return statement;
    }
    if (consume("case")) {
        statement->kind = Statement::Kind::Case;
        if (switch_depth_ == 0) {
            error_here("case label is not inside a switch");
        }
        statement->expression = parse_constant_expression();
        expect(":");
        if (!current().is("}")) statement->first = parse_statement();
        return statement;
    }
    if (consume("default")) {
        statement->kind = Statement::Kind::Default;
        if (switch_depth_ == 0) {
            error_here("default label is not inside a switch");
        } else if (!switch_default_seen_.empty() &&
                   switch_default_seen_.back()) {
            error_here("duplicate default label in switch");
        } else if (!switch_default_seen_.empty()) {
            switch_default_seen_.back() = true;
        }
        expect(":");
        if (!current().is("}")) statement->first = parse_statement();
        return statement;
    }
    statement->kind = Statement::Kind::Expression;
    statement->expression = parse_expression();
    expect(";");
    return statement;
}

std::unique_ptr<Statement> Parser::parse_global_label_statement(
    std::vector<Attribute> attributes) {
    ProductionScope production(*this, SyntaxProduction::LabeledStatement);
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Label;
    statement->location = current().location;
    statement->global_label = true;
    statement->attributes = std::move(attributes);
    consume("global");
    consume("label");
    const auto name = consume_kind(TokenKind::Identifier);
    if (!name) {
        error_here("expected label name after 'global label'");
    } else {
        statement->label_name = identifier_binding_name(*name);
        statement->label_fresh = token_origin(name->location).fresh;
    }
    expect(":", "after global label name");
    if (parsing_public_fragment_ ||
        (current().kind != TokenKind::End && !current().is("}")))
        statement->first = parse_statement();
    return statement;
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
    ProductionScope production(*this, SyntaxProduction::Expression);
    return parse_assignment(std::move(seed));
}

std::unique_ptr<Expr> Parser::parse_constant_expression() {
    ProductionScope production(*this, SyntaxProduction::ConstantExpression);
    return parse_conditional();
}

std::unique_ptr<Expr> Parser::parse_initializer() {
    ProductionScope production(*this, SyntaxProduction::Initializer);
    if (!current().is("{")) return parse_assignment();
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::AggregateInitializer;
    result->location = current().location;
    consume("{");
    while (!current().is("}") && current().kind != TokenKind::End) {
        {
            ProductionScope initializer_entry(*this, SyntaxProduction::InitializerEntry);
            Expr::InitializerEntry entry;
            entry.location = current().location;
            while (current().is(".") || current().is("[")) {
                ProductionScope designator_production(*this, SyntaxProduction::Designator);
                Expr::InitializerDesignator designator;
                designator.location = current().location;
                if (consume(".")) {
                    designator.kind = Expr::InitializerDesignator::Kind::Member;
                    const auto member = consume_kind(TokenKind::Identifier);
                    if (!member) error_here("expected member name after '.' in initializer");
                    else designator.member = identifier_binding_name(*member);
                } else {
                    consume("[");
                    designator.kind = Expr::InitializerDesignator::Kind::Index;
                    designator.index = parse_constant_expression();
                    expect("]", "after initializer designator");
                }
                entry.designators.push_back(std::move(designator));
            }
            if (!entry.designators.empty()) consume("=");
            entry.value = parse_initializer();
            result->initializer_entries.push_back(std::move(entry));
        }
        if (!consume(",")) break;
        if (current().is("}")) break;
    }
    expect("}", "after initializer list");
    return result;
}

std::unique_ptr<Expr> Parser::parse_assignment(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::AssignmentExpression);
    auto left = parse_conditional(std::move(seed));
    if (current().is("=") || current().is("+=") || current().is("-=") ||
        current().is("*=") || current().is("/=") || current().is("%=") ||
        current().is("<<=") || current().is(">>=") ||
        current().is("&=") || current().is("^=") ||
        current().is("|=")) {
        const auto operation = current();
        {
            ProductionScope assignment_operator(*this, SyntaxProduction::AssignmentOperator);
            ++index_;
        }
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Assign;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = parse_assignment();
        return result;
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_conditional(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::ConditionalExpression);
    auto condition = parse_binary(1, std::move(seed));
    if (!consume("?")) return condition;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Conditional;
    result->location = condition->location;
    result->left = std::move(condition);
    result->right = parse_expression();
    expect(":");
    result->third = parse_conditional();
    return result;
}

std::unique_ptr<Expr> Parser::parse_binary(int minimum_precedence, std::unique_ptr<Expr> seed) {
    static constexpr SyntaxProduction productions[] = {
        SyntaxProduction::None, SyntaxProduction::LogicalOrExpression,
        SyntaxProduction::LogicalAndExpression, SyntaxProduction::InclusiveOrExpression,
        SyntaxProduction::ExclusiveOrExpression, SyntaxProduction::AndExpression,
        SyntaxProduction::EqualityExpression, SyntaxProduction::RelationalExpression,
        SyntaxProduction::ShiftExpression, SyntaxProduction::AdditiveExpression,
        SyntaxProduction::MultiplicativeExpression};
    ProductionScope production(*this, productions[minimum_precedence]);
    auto left = minimum_precedence == 10
        ? (seed ? parse_postfix(std::move(seed)) : parse_cast())
        : parse_binary(minimum_precedence + 1, std::move(seed));
    for (;;) {
        // A token macro between operands may supply any operator or suffix.
        // Recognition cannot choose its precedence before the owner runs.
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        if (parsing_generic_argument_ &&
            (current().is(">") || current().is(">>"))) break;
        const int current_precedence = precedence(current().text);
        if (current_precedence != minimum_precedence) break;
        const auto operation = current(); ++index_;
        auto right = minimum_precedence == 10 ? parse_cast()
            : parse_binary(minimum_precedence + 1);
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Binary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = std::move(right);
        left = std::move(result);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_cast() {
    ProductionScope production(*this, SyntaxProduction::CastExpression);
    if (current().is("(")) {
        const auto saved = index_;
        ++index_;
        (void)current();
        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
        const bool begins_type = type_start(TypeProbe::ExpressionAlternative);
        index_ = saved;
        if (begins_type) {
            const auto location = current().location;
            consume("(");
            TypePtr type;
            std::optional<std::string> name;
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                type = parse_type();
                type = parse_declarator(std::move(type), name, DeclaratorContext::TypeName);
            }
            if (name) {
                diagnostics_.error(location,
                                   "a cast type name cannot declare an object");
            }
            expect(")", "after cast type");
            auto result = std::make_unique<Expr>();
            result->kind = Expr::Kind::Cast;
            result->location = location;
            result->type = std::move(type);
            result->left = parse_cast();
            return result;
        }
    }
    return parse_unary();
}

std::unique_ptr<Expr> Parser::parse_unary() {
    ProductionScope production(*this, SyntaxProduction::UnaryExpression);
    if (current().is("sizeof")) {
        const auto location = current().location;
        consume("sizeof");
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Sizeof;
        result->location = location;
        if (current().is("(")) {
            const auto saved = index_;
            ++index_;
            (void)current();
            if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
            const bool begins_type = type_start(TypeProbe::ExpressionAlternative);
            index_ = saved;
            if (begins_type) {
                consume("(");
                std::optional<std::string> name;
                {
                    ProductionScope type_name(*this, SyntaxProduction::TypeName);
                    result->type = parse_type();
                    result->type = parse_declarator(std::move(result->type), name, DeclaratorContext::TypeName);
                }
                if (name) {
                    diagnostics_.error(
                        location,
                        "a sizeof type name cannot declare an object");
                }
                expect(")", "after sizeof type");
                return result;
            }
        }
        result->left = parse_unary();
        return result;
    }
    if (current().is("+") || current().is("-") || current().is("!") ||
        current().is("~") || current().is("&") || current().is("*") ||
        current().is("++") || current().is("--")) {
        const auto operation = current(); ++index_;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Unary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = parse_unary();
        return result;
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix(std::unique_ptr<Expr> seed) {
    ProductionScope production(*this, SyntaxProduction::PostfixExpression);
    auto expression = seed ? std::move(seed) : parse_primary();
    for (;;) {
        const bool explicit_generic = current().is("::") &&
                                      current(1).is("<");
        const bool inferred_generic = current().is("<") && expression &&
                                      expression->kind == Expr::Kind::Name &&
                                      known_generic_name(*expression);
        if (explicit_generic || inferred_generic) {
            ProductionScope generic_arguments(*this, SyntaxProduction::GenericArguments);
            index_ += explicit_generic ? 2 : 1;
            if (consume(">")) {
                error_here("a generic argument list cannot be empty");
            } else {
                for (;;) {
                    {
                        ProductionScope generic_argument(*this, SyntaxProduction::GenericArgument);
                        Expr::GenericArgument argument;
                        (void)current();
                        if (parsing_public_fragment_ && macro_start()) throw DeferredNameRecognition{};
                        auto probe = TypeProbe::GenericArgumentAlternative;
                        if (preparing_header_ && expression->kind == Expr::Kind::Name) {
                            const auto* parameters = known_generic_parameters(*expression);
                            const auto ordinal = expression->generic_arguments.size();
                            if (parameters && ordinal < parameters->size() &&
                                (*parameters)[ordinal] == GenericParameterKind::Type)
                                probe = TypeProbe::GenericTypeArgumentAlternative;
                        }
                        if (type_start(probe)) {
                            ProductionScope type_name(*this, SyntaxProduction::TypeName);
                            argument.type = parse_type();
                            std::optional<std::string> declared;
                            argument.type = parse_declarator(std::move(argument.type), declared, DeclaratorContext::TypeName);
                            if (declared) error_here("a generic type argument cannot declare an object");
                        } else {
                            const bool previous = parsing_generic_argument_;
                            parsing_generic_argument_ = true;
                            argument.value = parse_constant_expression();
                            parsing_generic_argument_ = previous;
                        }
                        expression->generic_arguments.push_back(std::move(argument));
                    }
                    if (!consume(",")) break;
                }
                if (!consume_generic_close())
                    error_here("expected '>' to close generic argument list");
            }
            continue;
        }
        if (consume("(")) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto call = std::make_unique<Expr>();
            call->kind = Expr::Kind::Call;
            call->location = expression->location;
            call->generic_visible_at_call =
                expression->kind == Expr::Kind::Name &&
                known_generic_name(*expression);
            call->generic_arguments = std::move(expression->generic_arguments);
            call->left = std::move(expression);
            {
                ProductionScope argument_list(*this, SyntaxProduction::ArgumentList);
                if (!current().is(")")) {
                    do { call->arguments.push_back(parse_assignment()); } while (consume(","));
                }
            }
            expect(")");
            parsing_generic_argument_ = previous;
            expression = std::move(call);
            continue;
        }
        if (consume("[")) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto index = std::make_unique<Expr>();
            index->kind = Expr::Kind::Binary;
            index->location = expression->location;
            index->text = "index";
            index->left = std::move(expression);
            index->right = parse_expression();
            expect("]");
            parsing_generic_argument_ = previous;
            expression = std::move(index);
            continue;
        }
        if (current().is(".") || current().is("->")) {
            const bool through_pointer = consume("->");
            if (!through_pointer) consume(".");
            const auto member_name = consume_kind(TokenKind::Identifier);
            if (!member_name) {
                error_here("expected member name after '" +
                           std::string(through_pointer ? "->" : ".") + "'");
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
            member->right->text = identifier_binding_name(*member_name);
            expression = std::move(member);
            continue;
        }
        if (current().is("++") || current().is("--")) {
            const auto operation = current();
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
    return expression;
}

std::unique_ptr<Statement> Parser::parse_procedural_body(FunctionDecl& function) {
    const auto previous_function = active_function_;
    const auto previous_namespace = active_namespace_;
    const auto previous_imports = active_imports_;
    active_function_ = &function;
    active_namespace_ = function.source_namespace;
    active_imports_ = function.imports;
    parsing_procedural_body_ = true;
    auto body = parse_compound();
    if (current().kind != TokenKind::End)
        error_here("unexpected tokens after procedural macro body");
    parsing_procedural_body_ = false;
    active_function_ = previous_function;
    active_namespace_ = previous_namespace;
    active_imports_ = previous_imports;
    return body;
}

std::unique_ptr<Expr> Parser::parse_quote() {
    struct RawTokens {
        unsigned& depth;
        explicit RawTokens(unsigned& value) : depth(value) { ++depth; }
        ~RawTokens() { --depth; }
    } raw{raw_token_depth_};
    ProductionScope quote(*this, SyntaxProduction::QuoteExpression);
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Quote;
    result->location = current().location;
    if (!parsing_procedural_body_ && !parsing_public_fragment_)
        error_here("$::quote is only available in translation-time macro bodies");
    ++index_;
    if (!expect("{", "after $::quote")) return result;
    const auto content_first = index_;
    TokenSequence literal;
    std::vector<std::string_view> closers{"}"};
    while (current().kind != TokenKind::End) {
        if (!parsing_public_fragment_ && current().is("$::unquote")) {
            result->quote_fragments.push_back(std::move(literal));
            literal.clear();
            ++index_;
            if (!expect("(", "after $::unquote")) return result;
            result->arguments.push_back(parse_expression());
            if (!expect(")", "after $::unquote expression")) return result;
            continue;
        }
        if (current().is("(")) closers.push_back(")");
        else if (current().is("[")) closers.push_back("]");
        else if (current().is("[[")) closers.push_back("]]");
        else if (current().is("{")) closers.push_back("}");
        else if (current().is(")") || current().is("]") ||
                 current().is("]]") || current().is("}")) {
            if (closers.empty() || closers.back() != current().text) {
                error_here("$::quote requires balanced token groups");
                return result;
            }
            closers.pop_back();
            if (closers.empty()) {
                result->quote_fragments.push_back(std::move(literal));
                record_balanced_sequence(content_first, index_, SyntaxProduction::BalancedTokens);
                ++index_;
                return result;
            }
        }
        literal.emplace_back(current());
        ++index_;
    }
    error_here("unterminated $::quote token tree");
    return result;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    ProductionScope production(*this, SyntaxProduction::PrimaryExpression);
    // A selected custom owner gets its still-opaque input first. Ordinary
    // names may instead be assembled textually before primary classification.
    if (!macro_start() && !active_syntax(false)) normalize_qualified_name();
    if (current().is("syntax") &&
        (replacement_ || token_origin(current().location).context)) {
        error_here("expansion output cannot introduce syntax registration");
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Integer;
        result->location = current().location;
        result->text = "0";
        ++index_;
        return result;
    }
    if (syntax_ && (macro_start() || active_syntax(false))) {
        if (probing_header_type_)
            throw HeaderProbeInvocation{public_input_indices_[index_], macro_start()};
        if (parsing_public_fragment_) {
            auto placeholder = std::make_unique<Expr>();
            placeholder->kind = Expr::Kind::Integer;
            placeholder->text = "0";
            placeholder->location = current().location;
            if (!parse_opaque_invocation(SyntaxKind::Expression)) {
                error_here("could not recognize a bounded opaque expression invocation");
                if (index_ < tokens_.size() && current().kind != TokenKind::End) ++index_;
            }
            return placeholder;
        }
        return parse_expression_replacement();
    }
    const auto item = current();
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
            grouped->left = child->parse_assignment();
            if (child->current().kind != TokenKind::End)
                child->error_here("prepared expression must contain one assignment expression");
            // A structured splice's context is for lookup, not for replacing
            // the destination's active syntax/import/type environment.
            if (!item.prepared->context) adopt_replacement(*child);
        }
        if (!grouped->left) {
            grouped->left = std::make_unique<Expr>();
            grouped->left->kind = Expr::Kind::Integer;
            grouped->left->text = "0";
            grouped->left->location = item.location;
        }
        return grouped;
    }
    if (item.kind == TokenKind::StructuredSplice) {
        ++index_;
        auto invalid = [&] {
            auto result = std::make_unique<Expr>();
            result->kind = Expr::Kind::Integer;
            result->location = item.location;
            result->text = "0";
            return result;
        };
        if (!item.splice || !syntax_expression_node(*item.splice)) {
            diagnostics_.error(item.location,
                "structured syntax splice requires an expression node at expression position");
            return invalid();
        }
        if (!syntax_ || !item.splice->context ||
            !item.splice->context->parse_environment) {
            diagnostics_.error(item.location,
                "structured syntax splice has no retained parse environment");
            return invalid();
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
        if (parsing_public_fragment_) return invalid();
        auto output = syntax_->execution()->materialize_node(
            *item.splice, item.location, SyntaxParseCategory::Expression);
        if (!output) return invalid();
        auto child = replacement_parser(std::move(*output));
        // A deferred expression needs declarations from preceding expansions
        // in its original captured block, not same-spelled destination locals.
        if (item.splice->kind == SyntaxNode::Kind::Deferred)
            child->restore_deferred_environment(
                *item.splice->context->parse_environment, *item.splice->context,
                item.splice->span.first);
        else
            child->restore_environment(*item.splice->context->parse_environment,
                                       *item.splice->context);
        child->parsing_public_fragment_ = parsing_public_fragment_;
        child->recording_public_tree_ = parsing_public_fragment_;
        auto parsed = child->parse_assignment();
        if (child->current().kind != TokenKind::End)
            child->error_here("structured expression splice must contain one complete expression");
        if (preparing_header_)
            retain_prepared_fragment(item_index, {std::move(child->tokens_), item.location},
                SyntaxParseCategory::Expression, item.splice->context,
                item.splice->kind == SyntaxNode::Kind::Deferred, item.splice->span.first);
        if (!parsed) return invalid();
        auto grouped = std::make_unique<Expr>();
        grouped->kind = Expr::Kind::Parenthesized;
        grouped->location = item.location;
        grouped->left = std::move(parsed);
        return grouped;
    }
    if (current().is("$::quote")) return parse_quote();
    if (current().is("$::embed")) {
        ProductionScope embed(*this, SyntaxProduction::EmbedExpression);
        ++index_;
        expect("(", "after $::embed");
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Call;
        result->location = item.location;
        result->left = std::make_unique<Expr>();
        result->left->kind = Expr::Kind::Name;
        result->left->location = item.location;
        result->left->text = std::string(item.text);
        const auto path = consume_kind(TokenKind::String);
        if (!path) {
            error_here("$::embed requires one string-literal path");
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
        expect(")", "after embedded asset path");
        return result;
    }
    if (current().is("$::unquote"))
        error_here("$::unquote is only valid inside $::quote");
    if (current().is("$::alignof") && current(1).is("(")) {
        index_ += 2;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Alignof;
        result->location = item.location;
        if (type_start(TypeProbe::ExpressionAlternative)) {
            ProductionScope type_name(*this, SyntaxProduction::TypeName);
            result->type = parse_type();
            std::optional<std::string> name;
            result->type = parse_declarator(std::move(result->type), name, DeclaratorContext::TypeName);
            if (name) {
                diagnostics_.error(
                    item.location,
                    "an alignof type name cannot declare an object");
            }
        } else {
            result->left = parse_expression();
        }
        expect(")", "after alignof operand");
        return result;
    }
    if (consume("(")) {
        const bool previous = parsing_generic_argument_;
        parsing_generic_argument_ = false;
        auto inner = parse_expression();
        expect(")");
        parsing_generic_argument_ = previous;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Parenthesized;
        result->location = item.location;
        result->left = std::move(inner);
        return result;
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
        while (current().kind == TokenKind::String) {
            const auto decoded = decode_string_literal(current().text);
            if (!decoded) {
                diagnostics_.error(current().location,
                                   "invalid UTF-8 string literal");
            } else {
                result->string_value += *decoded;
            }
            ++index_;
        }
        return result;
    }
    else if (item.kind == TokenKind::Character) result->kind = Expr::Kind::Character;
    else if (item.kind == TokenKind::Identifier || item.kind == TokenKind::BuiltinName) {
        result->kind = Expr::Kind::Name;
        if (item.kind == TokenKind::Identifier) {
            if (auto name = parse_qualified_name()) result->text = *name;
            const auto origin = token_origin(item.location);
            auto context = std::make_shared<NameLookupContext>();
            context->name_space = origin.context ? origin.context->name_space : active_namespace_;
            context->imports = origin.context ? origin.context->imports : active_imports_;
            context->value_binding_inherited =
                item.value_binding.kind != ValueBinding::Kind::Unknown ||
                origin.value_binding.kind != ValueBinding::Kind::Unknown;
            context->value_binding = item.value_binding.kind != ValueBinding::Kind::Unknown
                ? item.value_binding : origin.value_binding;
            if (context->value_binding.kind == ValueBinding::Kind::Unknown) {
                // Do not freeze a lookup that preceding opaque code may change.
                // The enclosing bounded unit must stay deferred instead.
                require_public_name_context(result->text, item.location);
                context->value_binding.kind = ValueBinding::Kind::Nonlocal;
                if (result->text.find("::") == std::string::npos) {
                    const NameKey key(result->text, item.location);
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
            if (context->value_binding.kind == ValueBinding::Kind::Local)
                context->kind = NameLookupContext::Kind::Local;
            // This belongs to the private token provenance. Public trees still
            // expose syntax only, while projection preserves an exact use.
            if (!preparing_header_) tokens_[item_index].value_binding = context->value_binding;
            result->name_context = std::move(context);
            return result;
        }
    } else {
        error_here("expected expression");
        result->text = "0";
        result->kind = Expr::Kind::Integer;
    }
    ++index_;
    return result;
}

} // namespace cross
