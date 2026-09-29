// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"
#include "frontend/embed.hpp"
#include "frontend/lexer.hpp"
#include "frontend/procedural.hpp"
#include "frontend/parser.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace cross {

namespace {

std::string join(std::string_view prefix, std::string_view name) {
    return prefix.empty() ? std::string(name) : std::string(prefix) + "::" + std::string(name);
}

std::vector<std::string> namespaces(std::string name) {
    std::vector<std::string> result;
    while (!name.empty()) {
        result.push_back(name);
        const auto last = name.rfind("::");
        if (last == std::string::npos) break;
        name.resize(last);
    }
    return result;
}

bool user_identifier(const Token& token) {
    return token.kind == TokenKind::Identifier && !is_reserved_identifier(token.text);
}

std::string name_at(const std::vector<Token>& tokens, std::size_t& index) {
    if (index >= tokens.size() || !user_identifier(tokens[index])) return {};
    std::string name(tokens[index++].text);
    while (index < tokens.size() && tokens[index].is("::")) {
        if (index + 1 >= tokens.size() || !user_identifier(tokens[index + 1])) return {};
        name += "::";
        name += tokens[index + 1].text;
        index += 2;
    }
    return name;
}

std::string_view closer(std::string_view token) {
    if (token == "(") return ")";
    if (token == "[") return "]";
    if (token == "[[") return "]]";
    if (token == "{") return "}";
    return {};
}

bool closing(std::string_view token) {
    return token == ")" || token == "]" || token == "]]" || token == "}";
}

std::optional<std::size_t> group_end(const std::vector<Token>& tokens, std::size_t index,
                                   SyntaxExecution& execution) {
    if (index >= tokens.size() || closer(tokens[index].text).empty()) return {};
    std::vector<std::string_view> stack;
    for (auto at = index; at < tokens.size() && tokens[at].kind != TokenKind::End; ++at) {
        if (!execution.work(tokens[at].location)) return {};
        if (const auto close = closer(tokens[at].text); !close.empty()) {
            if (stack.size() >= execution.limits().depth) return {};
            stack.push_back(close);
        }
        else if (closing(tokens[at].text)) {
            if (stack.empty() || stack.back() != tokens[at].text) return {};
            stack.pop_back();
            if (stack.empty()) return at;
        }
    }
    return {};
}

using Pattern = std::vector<SyntaxPatternElement>;
using RulePattern = std::function<const Pattern*(SyntaxEntityId)>;

struct FirstSet {
    struct Terminal {
        TokenKind kind;
        std::string text;
        bool operator==(const Terminal&) const = default;
    };
    std::vector<Terminal> exact;
    bool identifier{};
    bool literal{};
    bool builtin{};
    bool raw{};
};

bool merge_first(FirstSet& into, const FirstSet& from) {
    bool changed = false;
    for (const auto& token : from.exact)
        if (std::find(into.exact.begin(), into.exact.end(), token) == into.exact.end()) {
            into.exact.push_back(token);
            changed = true;
        }
    changed |= (!into.identifier && from.identifier) || (!into.literal && from.literal) ||
               (!into.builtin && from.builtin) || (!into.raw && from.raw);
    into.identifier |= from.identifier;
    into.literal |= from.literal;
    into.builtin |= from.builtin;
    into.raw |= from.raw;
    return changed;
}

bool first_accepts(const FirstSet& set, TokenKind kind, std::string_view text) {
    if (set.raw && kind != TokenKind::End && text != ";" && !closing(text)) return true;
    if (set.identifier && kind == TokenKind::Identifier && !is_reserved_identifier(text)) return true;
    if (set.literal && (kind == TokenKind::Integer || kind == TokenKind::Floating ||
                        kind == TokenKind::String || kind == TokenKind::Character)) return true;
    if (set.builtin && kind == TokenKind::BuiltinName) return true;
    return std::any_of(set.exact.begin(), set.exact.end(), [&](const auto& candidate) {
        return candidate.kind == kind && candidate.text == text;
    });
}

bool first_overlap(const FirstSet& left, const FirstSet& right) {
    const auto ordinary = [](const FirstSet& set) {
        return set.raw || set.identifier || set.literal || set.builtin;
    };
    if ((left.raw && ordinary(right)) || (right.raw && ordinary(left)) ||
        (left.identifier && right.identifier) || (left.literal && right.literal) ||
        (left.builtin && right.builtin)) return true;
    for (const auto& token : left.exact) if (first_accepts(right, token.kind, token.text)) return true;
    for (const auto& token : right.exact) if (first_accepts(left, token.kind, token.text)) return true;
    return false;
}

// Left-recursive cycles have been rejected before bound rules reach this
// analysis. Summarize only nullable prefixes, memoizing shared rule bodies:
// productive recursion stops at its first consuming element. Exact terminals
// are sets, not derivation lists, so a diamond-shaped rule graph stays bounded.
class PatternAnalysis {
public:
    struct Summary {
        FirstSet first;
        bool nullable{};
    };

    PatternAnalysis(SyntaxExecution& execution, Diagnostics& diagnostics, RulePattern rule)
        : execution_(execution), diagnostics_(diagnostics), rule_(std::move(rule)) {}

    bool good() const { return !failed_; }
    const Pattern* rule(SyntaxEntityId id) const { return rule_(id); }
    bool work(SourceLocation location, std::uint64_t amount = 1) {
        if (!failed_ && !execution_.work(location, amount)) failed_ = true;
        return !failed_;
    }
    bool merge(FirstSet& into, const FirstSet& from, SourceLocation location) {
        // Account for the comparisons used by the small, deduplicated set.
        if (!work(location, 1 + from.exact.size() *
            (1 + into.exact.size() + from.exact.size()))) return false;
        return merge_first(into, from);
    }

    const Summary& sequence(const Pattern& pattern) {
        if (failed_) return empty_;
        if (const auto found = sequences_.find(&pattern); found != sequences_.end()) return found->second;
        const auto location = pattern.empty() ? SourceLocation{} : pattern.front().location;
        if (!work(location)) return empty_;
        if (active_.size() >= execution_.limits().depth ||
            std::find(active_.begin(), active_.end(), &pattern) != active_.end()) {
            diagnostics_.error(location, "syntax pattern analysis depth exceeded");
            failed_ = true;
            return empty_;
        }
        active_.push_back(&pattern);
        Summary result{{}, true};
        for (const auto& part : pattern) {
            const auto& current = element(part);
            merge(result.first, current.first, part.location);
            if (failed_ || !current.nullable) { result.nullable = false; break; }
        }
        active_.pop_back();
        return sequences_.emplace(&pattern, std::move(result)).first->second;
    }

    const Summary& element(const SyntaxPatternElement& part) {
        if (failed_) return empty_;
        if (const auto found = elements_.find(&part); found != elements_.end()) return found->second;
        if (!work(part.location)) return empty_;
        using K = SyntaxPatternElement::Kind;
        Summary result;
        auto& current = result.first;
        const auto expression_starts = [&] {
            current.identifier = current.literal = current.builtin = true;
            for (const auto spelling : {"(", "++", "--", "&", "*", "+", "-", "~", "!", "sizeof"})
                current.exact.push_back({spelling == std::string_view("sizeof")
                    ? TokenKind::Identifier : TokenKind::Punctuator, spelling});
        };
        const auto type_starts = [&] {
            current.identifier = current.builtin = true;
            for (const auto spelling : {"void", "bool", "i8", "i16", "i32", "i64", "i128", "iptr",
                                       "u8", "u16", "u32", "u64", "u128", "uptr", "f32", "f64",
                                       "f80", "f128", "fptr", "label", "const", "volatile", "restrict",
                                       "struct", "union", "enum"})
                current.exact.push_back({TokenKind::Identifier, spelling});
            current.exact.push_back({TokenKind::Punctuator, "[["});
        };
        const auto declaration_starts = [&] {
            type_starts();
            for (const auto spelling : {"typedef", "static", "global", "register", "stack", "inline"})
                current.exact.push_back({TokenKind::Identifier, spelling});
        };
        switch (part.kind) {
        case K::Terminal: current.exact.push_back({part.terminal.kind, part.terminal.text}); break;
        case K::Ident: case K::Name: current.identifier = true; break;
        case K::Literal: current.literal = true; break;
        case K::Expr: expression_starts(); break;
        case K::Type: type_starts(); break;
        case K::Declaration: case K::FunctionHeader:
        case K::FunctionDeclaration: case K::FunctionDefinition: case K::FunctionRaw:
            declaration_starts();
            break;
        case K::Statement:
            expression_starts();
            declaration_starts();
            for (const auto spelling : {"case", "default", "if", "switch", "while", "do", "for",
                                       "goto", "break", "continue", "return", "syntax"})
                current.exact.push_back({TokenKind::Identifier, spelling});
            current.exact.push_back({TokenKind::Punctuator, "{"});
            current.exact.push_back({TokenKind::Punctuator, ";"});
            break;
        case K::TokensUntil: current.raw = true; break;
        case K::Paren: case K::Bracket: case K::Block: case K::Group:
            for (const auto opener : {"(", "[", "[[", "{"})
                if (part.kind == K::Group ||
                    (part.kind == K::Paren && opener == std::string_view("(")) ||
                    (part.kind == K::Bracket && opener == std::string_view("[")) ||
                    (part.kind == K::Block && opener == std::string_view("{"))) {
                    FirstSet::Terminal token;
                    token.kind = TokenKind::Punctuator;
                    token.text = opener;
                    current.exact.push_back(std::move(token));
                }
            break;
        case K::Rule:
            if (part.resolved_rule)
                if (const auto* body = rule_(*part.resolved_rule)) result = sequence(*body);
            break;
        case K::Optional: case K::Repeat0: case K::Repeat1: case K::Separated0: case K::Separated1:
            result = sequence(part.pattern);
            if (part.kind == K::Optional || part.kind == K::Repeat0 || part.kind == K::Separated0)
                result.nullable = true;
            break;
        case K::Choice:
            for (const auto& alternative : part.alternatives) {
                const auto& branch = sequence(alternative.pattern);
                merge(current, branch.first, part.location);
                result.nullable |= branch.nullable;
            }
            break;
        }
        return elements_.emplace(&part, std::move(result)).first->second;
    }

private:
    SyntaxExecution& execution_;
    Diagnostics& diagnostics_;
    RulePattern rule_;
    bool failed_{};
    Summary empty_;
    std::vector<const Pattern*> active_;
    std::unordered_map<const Pattern*, Summary> sequences_;
    std::unordered_map<const SyntaxPatternElement*, Summary> elements_;
};

bool validate_pattern_progress(const Pattern& root, PatternAnalysis& analysis,
                               Diagnostics& diagnostics) {
    using K = SyntaxPatternElement::Kind;
    struct Pending {
        FirstSet follow;
        bool queued{};
    };
    std::unordered_map<const Pattern*, Pending> continuations;
    std::vector<const Pattern*> queue;
    const auto enqueue = [&](const Pattern& pattern, const FirstSet& follow) {
        if (pattern.empty()) return;
        auto [entry, inserted] = continuations.try_emplace(&pattern);
        const bool changed = analysis.merge(entry->second.follow, follow, pattern.front().location);
        if ((inserted || changed) && !entry->second.queued) {
            entry->second.queued = true;
            queue.push_back(&pattern);
        }
    };
    enqueue(root, {});
    // A rule can be reached through multiple callers or recursive tails.
    // Propagate unions until stable; never inline the recursive rule graph.
    for (std::size_t next = 0; next < queue.size() && analysis.good(); ++next) {
        const auto& pattern = *queue[next];
        auto continuation = continuations.at(&pattern).follow;
        continuations.at(&pattern).queued = false;
        for (std::size_t at = pattern.size(); at-- > 0;) {
            const auto& element = pattern[at];
            if (!analysis.work(element.location)) return false;
            if (element.kind == K::Expr || element.kind == K::Type) {
                if (!analysis.work(element.location, 1 + continuation.exact.size())) return false;
                // A fence can be supplied by a caller, an optional/choice, or
                // the separator of an enclosing list. Every possible next
                // token must fence the capture; a record boundary alone does
                // not bound an expression or inject new operator precedence.
                if (continuation.raw || continuation.identifier || continuation.literal || continuation.builtin ||
                    std::any_of(continuation.exact.begin(), continuation.exact.end(), [](const auto& fence) {
                        return fence.kind != TokenKind::Punctuator ||
                            (fence.text != ";" && fence.text != "," && !closing(fence.text));
                    })) {
                    diagnostics.error(element.location,
                        "parsed expression/type capture requires a semicolon, comma, or closing-delimiter fence");
                    return false;
                }
            }
            if ((element.kind == K::Optional || element.kind == K::Repeat0 || element.kind == K::Repeat1 ||
                 element.kind == K::Separated0 || element.kind == K::Separated1) &&
                analysis.sequence(element.pattern).nullable) {
                diagnostics.error(element.location, "syntax optional/repetition body may be nullable");
                return false;
            }
            const bool repeated = element.kind == K::Repeat0 || element.kind == K::Repeat1;
            const bool separated = element.kind == K::Separated0 || element.kind == K::Separated1;
            auto child_continuation = continuation;
            if (repeated || separated) {
                FirstSet loop;
                if (separated) loop.exact.push_back({element.terminal.kind, element.terminal.text});
                else loop = analysis.sequence(element.pattern).first;
                if (!analysis.work(element.location,
                    1 + loop.exact.size() + continuation.exact.size() +
                    2 * loop.exact.size() * continuation.exact.size())) return false;
                if (first_overlap(loop, continuation)) {
                    diagnostics.error(element.location, "syntax repetition start conflicts with continuation");
                    return false;
                }
                analysis.merge(child_continuation, loop, element.location);
            }
            enqueue(element.pattern, child_continuation);
            if (element.kind == K::Rule && element.resolved_rule)
                if (const auto* body = analysis.rule(*element.resolved_rule)) enqueue(*body, continuation);
            for (const auto& alternative : element.alternatives) {
                if (analysis.sequence(alternative.pattern).nullable) {
                    diagnostics.error(element.location, "syntax choice alternative must consume input");
                    return false;
                }
                enqueue(alternative.pattern, continuation);
            }
            const auto& start = analysis.element(element);
            if (!start.nullable) continuation = start.first;
            else analysis.merge(continuation, start.first, element.location);
            if (!analysis.good()) return false;
        }
    }
    return analysis.good();
}

} // namespace

SyntaxExecution::SyntaxExecution(SourceManager& sources, Diagnostics& diagnostics,
    unsigned address_bits, LayoutQuery size_of, LayoutQuery align_of,
    EvaluationLimits limits, EvaluationLayout layout)
    : sources_(sources), diagnostics_(diagnostics), address_bits_(address_bits),
      size_of_(std::move(size_of)), align_of_(std::move(align_of)), limits_(limits), layout_(layout) {}

std::vector<Token> SyntaxExecution::prepare(const SourceFile& source) {
    return Lexer(source, diagnostics_).lex();
}

void SyntaxExecution::publish_declarations(const Program& program,
    std::span<const RecordDecl> pending_records, std::span<const EnumDecl> pending_enumerations) {
    for (const auto& function : program.functions)
        if (published_functions_.insert(function.get()).second)
            declarations_.functions.push_back(copy_evaluation_declaration(*function));
    for (const auto& object : program.objects)
        if (published_objects_.insert(object.get()).second)
            declarations_.objects.push_back(copy_evaluation_declaration(*object));
    const auto publish_record = [&](const RecordDecl& record) {
        const auto prior = published_records_.find(record.nominal_key());
        if (prior != published_records_.end() && (prior->second || !record.complete)) return;
        published_records_[record.nominal_key()] = record.complete;
        declarations_.records.push_back(copy_evaluation_declaration(record));
    };
    for (const auto& record : program.records) publish_record(record);
    for (const auto& record : pending_records) publish_record(record);
    const auto publish_enumeration = [&](const EnumDecl& enumeration) {
        if (published_enumerations_.insert(enumeration.nominal_key()).second)
            declarations_.enumerations.push_back(copy_evaluation_declaration(enumeration));
    };
    for (const auto& enumeration : program.enumerations) publish_enumeration(enumeration);
    for (const auto& enumeration : pending_enumerations) publish_enumeration(enumeration);
}

bool SyntaxExecution::define_function(const std::vector<Token>& tokens, std::size_t& index, std::string_view name_space,
    const std::vector<std::string>& imports, const std::vector<SyntaxBinding>& bindings,
    std::shared_ptr<const SyntaxParseEnvironment> environment) {
    auto function = parse_expansion_function(tokens, index, diagnostics_,
                                             address_bits_);
    if (!function) return false;
    auto& declaration = function->function;
    declaration.name = join(name_space, declaration.name);
    declaration.source_namespace = name_space;
    declaration.imports = imports;
    for (const auto& previous : functions_) {
        if (previous.declaration.name != declaration.name) continue;
        diagnostics_.error(declaration.location, "expansion function is defined more than once: '" + declaration.name + "'");
        return false;
    }
    functions_.push_back({std::move(declaration), function->syntax_expander, bindings, std::move(environment)});
    return true;
}

std::optional<SyntaxExecution::FunctionId> SyntaxExecution::find_function(
    std::string_view name, std::string_view name_space,
    const std::vector<std::vector<std::string>>& imports, bool syntax_expander) const {
    const auto exact = [&](std::string_view candidate) -> std::optional<FunctionId> {
        for (std::size_t index = 0; index < functions_.size(); ++index)
            if (functions_[index].syntax_expander == syntax_expander &&
                functions_[index].declaration.name == candidate) return FunctionId{static_cast<std::uint32_t>(index)};
        return {};
    };
    for (const auto& prefix : namespaces(std::string(name_space)))
        if (auto result = exact(join(prefix, name))) return result;
    for (auto scope = imports.rbegin(); scope != imports.rend(); ++scope)
        for (const auto& imported : *scope)
            if (auto result = exact(join(imported, name))) return result;
    return exact(name);
}

bool SyntaxExecution::work(SourceLocation location, std::uint64_t amount) {
    if (amount > limits_.steps - std::min(work_, limits_.steps)) {
        diagnostics_.error(location, "syntax matching work budget exceeded");
        return false;
    }
    work_ += amount;
    return true;
}

bool SyntaxExecution::begin_replacement(SourceLocation location) {
    // Textual macro output is parsed by the caller's token cursor, so its
    // generating expansion may no longer occupy the C++ call stack. Retain
    // the same nesting limit through typed source ancestry, excluding public
    // fragment parsing/materialization wrappers.
    unsigned ancestry{};
    for (auto at = location; at.file;) {
        const auto* expansion = at.file->expansion_at(at.offset);
        if (!expansion) break;
        if (!work(location)) return false;
        if (expansion->kind == SourceExpansion::Kind::ProceduralMacro ||
            expansion->kind == SourceExpansion::Kind::SyntaxExtension) ++ancestry;
        if (ancestry >= limits_.depth) break;
        at = expansion->invocation;
    }
    if (std::max(depth_, ancestry) >= limits_.depth || expansions_ >= 128) {
        diagnostics_.error(location, "syntax/procedural expansion depth or invocation budget exceeded");
        return false;
    }
    ++depth_;
    ++expansions_;
    active_expansions_.emplace_back();
    return true;
}

void SyntaxExecution::end_replacement() {
    active_expansions_.pop_back();
    --depth_;
}

bool SyntaxExecution::begin_fragment(SourceLocation location, std::size_t copied_tokens) {
    if (fragment_depth_ >= limits_.depth) {
        diagnostics_.error(location, "public syntax fragment nesting depth exceeded");
        return false;
    }
    if (!work(location, copied_tokens + 1)) return false;
    ++fragment_depth_;
    return true;
}

void SyntaxExecution::end_fragment() { --fragment_depth_; }

void SyntaxExecution::tree_limit_error(SourceLocation location) {
    diagnostics_.error(location, "public syntax tree depth, work, or storage budget exceeded");
}

std::shared_ptr<const SyntaxContext> SyntaxExecution::call_context(SourceLocation location,
    std::string_view name_space, const std::vector<std::string>& imports,
    const std::vector<SyntaxBinding>& bindings,
    std::shared_ptr<const SyntaxParseEnvironment> environment) const {
    auto context = std::make_shared<SyntaxContext>();
    context->invocation = location;
    context->name_space = name_space;
    context->imports = imports;
    context->syntax_bindings = bindings;
    context->parse_environment = std::move(environment);
    return context;
}

std::optional<SyntaxExecution::Output> SyntaxExecution::expand(FunctionId id,
    TokenSequence input, std::shared_ptr<const SyntaxMatchValue> match, SourceLocation invocation,
    std::string_view name_space, const std::vector<std::string>& imports,
    const std::vector<SyntaxBinding>& bindings,
    std::shared_ptr<const SyntaxParseEnvironment> environment,
    const SyntaxDefinition* owner) {
    if (id.value >= functions_.size()) return {};
    if (!active_expansions_.empty()) {
        ExpansionSignature signature;
        signature.function = id.value;
        signature.name_space = name_space;
        signature.imports = imports;
        signature.bindings = bindings;
        const auto& matched = match ? match->input : input;
        signature.input.reserve(matched.size());
        for (const auto& token : matched)
            signature.input.push_back({token.kind, token.text, token.splice.get()});
        unsigned repeats{};
        for (std::size_t at = 0; at + 1 < active_expansions_.size(); ++at)
            if (active_expansions_[at] && *active_expansions_[at] == signature) ++repeats;
        if (repeats >= 2) {
            diagnostics_.error(invocation,
                "syntax/procedural expansion depth or invocation budget exceeded (recursive cycle)");
            if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
            return {};
        }
        active_expansions_.back() = std::move(signature);
    }
    const auto& function = functions_[id.value];
    const auto call = call_context(invocation, name_space, imports, bindings, std::move(environment));
    const auto attach = [&](TokenSequence& tokens) {
        for (auto& token : tokens) if (!token.origin.context) token.origin.context = call;
    };
    attach(input);
    std::function<std::shared_ptr<const SyntaxNode>(const SyntaxNode&)> contextualize_node;
    std::function<std::shared_ptr<const SyntaxMatchValue>(const SyntaxMatchValue&)> contextualize_match;
    contextualize_node = [&](const SyntaxNode& source) -> std::shared_ptr<const SyntaxNode> {
        auto node = std::make_shared<SyntaxNode>(source);
        if (!node->context) node->context = call;
        attach(node->tokens);
        for (auto& child : node->children) child = contextualize_node(*child);
        if (node->match) node->match = contextualize_match(*node->match);
        return node;
    };
    contextualize_match = [&](const SyntaxMatchValue& source) -> std::shared_ptr<const SyntaxMatchValue> {
        auto value = std::make_shared<SyntaxMatchValue>(source);
        if (!value->context) value->context = call;
        attach(value->input);
        for (auto& field : value->fields) {
            attach(field.tokens);
            if (field.node) field.node = contextualize_node(*field.node);
            for (auto& record : field.records) record = contextualize_match(*record);
        }
        return value;
    };
    if (match) match = contextualize_match(*match);
    const auto expansion = sources_.next_expansion();
    auto definition = std::make_shared<SyntaxContext>();
    definition->kind = SyntaxContext::Kind::DefinitionSite;
    definition->expansion = expansion;
    definition->definition = function.declaration.location;
    definition->invocation = invocation;
    definition->name_space = function.declaration.source_namespace;
    definition->imports = function.declaration.imports;
    definition->syntax_bindings = function.bindings;
    definition->parse_environment = function.environment;
    const SyntaxParseCallback parse = [&](SyntaxParseCategory category, const TokenSequence& tokens,
        std::shared_ptr<const SyntaxContext> context, SourceLocation location) {
        return parse_tokens(category, tokens, std::move(context), location);
    };
    auto output = function.syntax_expander
        ? evaluate_syntax_body(function.declaration, std::move(match), address_bits_, size_of_, align_of_,
                               definition, diagnostics_, limits_, layout_, parse, call, &declarations_)
        : evaluate_procedural_body(function.declaration, input, address_bits_, size_of_, align_of_,
                                   definition, diagnostics_, limits_, layout_, parse, call, &declarations_);
    if (!output) {
        diagnostics_.note(function.declaration.location, "expansion function is defined here");
        if (owner) diagnostics_.note(owner->location, "syntax '" + owner->name + "' defined here");
        diagnostics_.note(invocation, "while expanding '" + function.declaration.name + "'");
        return {};
    }
    std::string text;
    std::vector<SourceTokenOrigin> origins;
    const auto original = token_origin(invocation).identity;
    for (std::size_t index = 0; index < output->size(); ++index) {
        auto& token = (*output)[index];
        if (!token.origin.identity.source_unit)
            token.origin.identity = {original.source_unit, 0, expansion, index};
        const auto begin = text.size();
        text += token.text;
        origins.push_back({begin, text.size(), token.origin, token.splice});
        text += ' ';
    }
    const auto end = text.size();
    const auto unit = invocation.file ? invocation.file->source_unit_at(invocation.line) : std::string{};
    std::vector<std::string> units(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1, unit);
    const auto* source = sources_.add(invocation.file ? invocation.file->path : std::filesystem::path("<syntax>"),
        std::move(text), {SourceExpansion{0, end,
            owner ? owner->name : function.declaration.name,
            invocation, owner ? owner->location : function.declaration.location,
            owner ? SourceExpansion::Kind::SyntaxExtension : SourceExpansion::Kind::ProceduralMacro,
            owner ? function.declaration.location : SourceLocation{}}},
        std::move(origins), {}, std::move(units));
    if (!validate_embeds(*source, diagnostics_)) return {};
    auto tokens = Lexer(*source, diagnostics_).lex();
    // Empty item replacements still need an ancestry-bearing source location.
    return Output{std::move(tokens), {source, 0, 1, 1}};
}

std::shared_ptr<const SyntaxNode> SyntaxExecution::parse_tokens(SyntaxParseCategory category,
    const TokenSequence& input, std::shared_ptr<const SyntaxContext> context,
    SourceLocation location) {
    if (!context || !context->parse_environment) return {};
    std::string text;
    std::vector<SourceTokenOrigin> origins;
    for (const auto& token : input) {
        if (!work(location)) return {};
        if (token.kind == TokenKind::End || token.kind == TokenKind::Invalid ||
            token.text.empty() || token.text.find('\0') != std::string::npos ||
            token.text.size() + 1 > limits_.bytes - std::min<std::uint64_t>(text.size(), limits_.bytes)) return {};
        const auto begin = text.size();
        text += token.text;
        auto origin = token.origin;
        // Explicit-context parsing changes lookup, not identity or source span.
        origin.context = context;
        origin.value_binding = {};
        origin.tag_binding.reset();
        origin.alias_binding.reset();
        origins.push_back({begin, text.size(), std::move(origin), token.splice});
        text += ' ';
    }
    const auto size = text.size();
    const auto* source = sources_.add("<meta::parse>", std::move(text),
        {{0, size, "$::meta::parse", location, context->definition,
          SourceExpansion::Kind::FragmentParse}}, std::move(origins));
    auto tokens = Lexer(*source, diagnostics_).lex();
    if (tokens.size() != input.size() + 1) return {};
    for (std::size_t at = 0; at < input.size(); ++at) {
        if (tokens[at].kind != input[at].kind || tokens[at].text != input[at].text) return {};
        if (tokens[at].splice != input[at].splice) return {};
        tokens[at].split_source = input[at].split_source;
        tokens[at].split_offset = input[at].split_offset;
    }
    return Parser::parse_syntax_tokens(category, std::move(tokens), std::move(context), diagnostics_);
}

std::optional<SyntaxExecution::Output> SyntaxExecution::materialize_node(
    const SyntaxNode& node, SourceLocation location, SyntaxParseCategory deferred_category) {
    std::vector<const SyntaxNode*> pending{&node};
    std::unordered_set<const SyntaxNode*> seen;
    while (!pending.empty()) {
        const auto* next = pending.back();
        pending.pop_back();
        if (!seen.insert(next).second) continue;
        if (!work(location)) return {};
        // Only the root is opened in this grammar slot. Deferred descendants
        // stay structured markers and are opened by their own category parser.
        if (next == &node && next->kind == SyntaxNode::Kind::Deferred &&
            next->deferred_category != deferred_category) {
            diagnostics_.error(location,
                "deferred syntax-node splice requires explicit $::meta::tokens projection");
            return {};
        }
        for (const auto& child : next->children) if (child) pending.push_back(child.get());
        for (const auto& token : next->tokens)
            if (token.splice) pending.push_back(token.splice.get());
    }
    const auto fragments = syntax_node_fragments(node);
    std::string text;
    std::vector<SourceTokenOrigin> origins;
    const auto expansion = sources_.next_expansion();
    const auto original = token_origin(location).identity;
    for (std::size_t index = 0; index < fragments.size(); ++index) {
        if (!work(location)) return {};
        auto token = fragments[index];
        if (token.kind == TokenKind::End || token.kind == TokenKind::Invalid ||
            token.text.empty() || token.text.find('\0') != std::string::npos ||
            token.text.size() + 1 > limits_.bytes - std::min<std::uint64_t>(text.size(), limits_.bytes)) {
            diagnostics_.error(location, "syntax node cannot be materialized without explicit token projection");
            return {};
        }
        if (!token.origin.identity.source_unit)
            token.origin.identity = {original.source_unit, 0, expansion, index};
        const auto begin = text.size();
        text += token.text;
        origins.push_back({begin, text.size(), std::move(token.origin), token.splice});
        text += ' ';
    }
    const auto end = text.size();
    const auto unit = location.file
        ? location.file->source_unit_at(location.line) : std::string{};
    std::vector<std::string> units(
        static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1, unit);
    const auto* source = sources_.add("<syntax-splice>", std::move(text),
        {{0, end, "$::unquote", location, node.span.first,
          SourceExpansion::Kind::StructuredSplice}}, std::move(origins),
        {}, std::move(units));
    auto tokens = Lexer(*source, diagnostics_).lex();
    if (tokens.size() != fragments.size() + 1) {
        diagnostics_.error(location, "syntax node cannot be materialized without explicit token projection");
        return {};
    }
    for (std::size_t at = 0; at < fragments.size(); ++at) {
        if (tokens[at].kind != fragments[at].kind || tokens[at].text != fragments[at].text ||
            tokens[at].splice != fragments[at].splice) {
            diagnostics_.error(location, "syntax node cannot be materialized without explicit token projection");
            return {};
        }
        tokens[at].split_source = fragments[at].split_source;
        tokens[at].split_offset = fragments[at].split_offset;
    }
    return Output{std::move(tokens), {source, 0, 1, 1}};
}

std::optional<MetaToken> SyntaxExecution::terminal(std::string_view quoted, SourceLocation location) {
    const auto decoded = decode_string_literal(quoted);
    if (!decoded || decoded->empty() || decoded->size() > limits_.bytes || decoded->find('\0') != std::string::npos) {
        diagnostics_.error(location, "syntax terminal must spell exactly one existing token");
        return {};
    }
    const auto* source = sources_.add("<syntax-terminal>", *decoded);
    auto tokens = Lexer(*source, diagnostics_).lex();
    if (tokens.size() != 2 || tokens[0].kind == TokenKind::Invalid || tokens[0].text != *decoded) {
        diagnostics_.error(location, "syntax terminal must spell exactly one existing token");
        return {};
    }
    MetaToken result(tokens[0]);
    result.origin = token_origin(location);
    return result;
}

SyntaxState::SyntaxState(std::shared_ptr<SyntaxExecution> execution)
    : execution_(std::move(execution)), definitions_(std::make_shared<std::vector<SyntaxDefinition>>()) {}

SyntaxState::SyntaxState(const SyntaxState& snapshot, std::shared_ptr<SyntaxExecution> execution)
    : execution_(std::move(execution)), definitions_(snapshot.definitions_),
      visible_definitions_(snapshot.visible_definitions_),
      imports_(snapshot.imports_), scopes_(snapshot.scopes_) {}

void SyntaxState::push_scope() { scopes_.emplace_back(); imports_.emplace_back(); }
void SyntaxState::pop_scope() { scopes_.pop_back(); imports_.pop_back(); }
void SyntaxState::import(std::string name) { imports_.back().push_back(std::move(name)); }

std::vector<SyntaxBinding> SyntaxState::bindings() const {
    std::vector<SyntaxBinding> result;
    for (const auto& scope : scopes_) result.insert(result.end(), scope.begin(), scope.end());
    return result;
}

std::optional<SyntaxEntityId> SyntaxState::resolve(
    std::string_view name, std::string_view name_space, SourceLocation location,
    Diagnostics& diagnostics) const {
    const auto start = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
    };
    const auto continuation = [&](char ch) {
        return start(ch) || (ch >= '0' && ch <= '9');
    };
    bool valid = !name.empty();
    for (std::size_t at = 0; valid && at < name.size();) {
        const auto begin = at;
        if (!start(name[at++])) { valid = false; break; }
        while (at < name.size() && continuation(name[at])) ++at;
        valid = !is_reserved_identifier(name.substr(begin, at - begin));
        if (at == name.size()) break;
        valid = valid && at + 2 < name.size() && name.substr(at, 2) == "::";
        at += 2;
    }
    if (!valid) {
        diagnostics.error(location, "expected a qualified syntax-name string");
        return {};
    }
    return lookup(name, name_space, imports_, location, diagnostics);
}

std::optional<SyntaxEntityId> SyntaxState::lookup(std::string_view name, std::string_view name_space,
    const std::vector<std::vector<std::string>>& imports, SourceLocation location,
    Diagnostics& diagnostics) const {
    const auto exact = [&](std::string_view candidate) -> std::optional<SyntaxEntityId> {
        for (std::size_t at = 0; at < visible_definitions_; ++at)
            if ((*definitions_)[at].name == candidate) return (*definitions_)[at].id;
        return {};
    };
    if (name.find("::") != std::string_view::npos) {
        if (auto id = exact(name)) return id;
    } else {
        for (const auto& prefix : namespaces(std::string(name_space)))
            if (auto id = exact(join(prefix, name))) return id;
        for (auto scope = imports.rbegin(); scope != imports.rend(); ++scope) {
            std::optional<SyntaxEntityId> selected;
            for (const auto& imported : *scope) {
                const auto id = exact(join(imported, name));
                if (!id) continue;
                if (selected && *selected != *id) {
                    diagnostics.error(location, "ambiguous syntax entity '" + std::string(name) + "'");
                    return {};
                }
                selected = id;
            }
            if (selected) return selected;
        }
        if (auto id = exact(name)) return id;
    }
    diagnostics.error(location, "syntax entity is not visible: '" + std::string(name) + "'");
    return {};
}

bool SyntaxState::declare(const std::vector<Token>& tokens, std::size_t& index,
    std::string_view name_space, Diagnostics& diagnostics) {
    const auto start = tokens[index++].location;
    std::string name;
    struct DeclarationNote {
        Diagnostics& diagnostics;
        SourceLocation start;
        const std::string& name;
        unsigned previous_errors;
        bool complete{};
        ~DeclarationNote() {
            if (complete || diagnostics.errors() == previous_errors) return;
            diagnostics.note(start, name.empty() ? "while declaring a syntax entity"
                : "while declaring syntax '" + name + "'");
        }
    } note{diagnostics, start, name, diagnostics.errors()};
    const auto error = [&](std::string message) {
        diagnostics.error(index < tokens.size() ? tokens[index].location : start, std::move(message));
        return false;
    };
    const auto take = [&](std::string_view text) {
        if (index < tokens.size() && tokens[index].is(text)) { ++index; return true; }
        return false;
    };
    if (index >= tokens.size() || !user_identifier(tokens[index])) return error("expected nonreserved syntax entity name");
    name = join(name_space, tokens[index++].text);
    if (!take(":")) return error("expected ':' after syntax entity name");
    const auto kind = index < tokens.size() ? tokens[index++].text : std::string_view{};
    const auto selected = kind == "item" ? std::optional<SyntaxKind>(SyntaxKind::Item)
        : kind == "statement" ? std::optional<SyntaxKind>(SyntaxKind::Statement)
        : kind == "expression" ? std::optional<SyntaxKind>(SyntaxKind::Expression)
        : kind == "rule" ? std::optional<SyntaxKind>(SyntaxKind::Rule)
        : kind == "bundle" ? std::optional<SyntaxKind>(SyntaxKind::Bundle) : std::nullopt;
    if (!selected || !take("{")) return error("expected syntax kind and '{'");
    SyntaxDefinition definition{{static_cast<std::uint32_t>(definitions_->size())}, *selected,
        name, {}, {}, {}, {}, start, std::string(name_space), imports_};
    if (*selected == SyntaxKind::Bundle) {
        while (take("use")) {
            SyntaxActivation entry{{}, {}, index < tokens.size() ? tokens[index].location : start};
            entry.name = name_at(tokens, index);
            if (entry.name.empty()) return error("expected syntax entity after bundle 'use'");
            if (take("as")) {
                if (index >= tokens.size() || !user_identifier(tokens[index])) return error("expected nonreserved syntax prefix alias");
                entry.alias = tokens[index++].text;
            }
            if (!take(";")) return error("expected ';' after bundle use");
            definition.uses.push_back(std::move(entry));
        }
        if (definition.uses.empty()) return error("syntax bundle requires a nonempty use list");
    } else {
        if (*selected != SyntaxKind::Rule) {
            if (!take("prefix") || index >= tokens.size() || tokens[index].kind != TokenKind::String)
                return error("syntax definition requires prefix, match, and expand in order");
            auto terminal = execution_->terminal(tokens[index].text, tokens[index].location);
            ++index;
            if (!terminal || terminal->kind != TokenKind::Identifier || is_reserved_identifier(terminal->text))
                return error("syntax prefix must be one ordinary nonreserved identifier");
            definition.prefix = terminal->text;
            if (!take(";")) return error("expected ';' after syntax prefix");
        }
        if (!take("match")) return error("syntax definition requires one match clause");
        const auto parse_pattern = [&](const auto& self, std::string_view stop, unsigned depth)
            -> std::optional<std::vector<SyntaxPatternElement>> {
            if (depth >= execution_->limits().depth) {
                error("syntax pattern nesting depth exceeded");
                return {};
            }
            std::vector<SyntaxPatternElement> pattern;
            std::unordered_set<std::string> fields;
            std::vector<std::string_view> delimiters;
            while (index < tokens.size() && !tokens[index].is(stop) &&
                   tokens[index].kind != TokenKind::End) {
                if (!execution_->work(tokens[index].location)) return {};
                SyntaxPatternElement element{};
                element.location = tokens[index].location;
                if (tokens[index].kind == TokenKind::String) {
                    auto terminal = execution_->terminal(tokens[index].text, tokens[index].location);
                    ++index;
                    if (!terminal) return {};
                    element.terminal = std::move(*terminal);
                    if (const auto close = closer(element.terminal.text); !close.empty()) delimiters.push_back(close);
                    else if (closing(element.terminal.text)) {
                        if (delimiters.empty() || delimiters.back() != element.terminal.text) {
                            error("syntax pattern terminals must balance delimiters");
                            return {};
                        }
                        delimiters.pop_back();
                    }
                } else {
                    if (!(tokens[index].is("rule") && index + 1 < tokens.size() && tokens[index + 1].is("("))) {
                        if (!user_identifier(tokens[index])) { error("expected syntax capture or quoted terminal"); return {}; }
                        element.field = tokens[index++].text;
                        if (!fields.insert(element.field).second) { error("duplicate syntax capture field"); return {}; }
                        if (!take(":")) { error("expected ':' after syntax capture field"); return {}; }
                    }
                    const auto capture = index < tokens.size() ? tokens[index++].text : std::string_view{};
                    using K = SyntaxPatternElement::Kind;
                    if (capture == "ident") element.kind = K::Ident;
                    else if (capture == "name") element.kind = K::Name;
                    else if (capture == "literal") element.kind = K::Literal;
                    else if (capture == "paren") element.kind = K::Paren;
                    else if (capture == "bracket") element.kind = K::Bracket;
                    else if (capture == "block") element.kind = K::Block;
                    else if (capture == "group") element.kind = K::Group;
                    else if (capture == "function" || capture == "function_raw") element.kind = K::FunctionRaw;
                    else if (capture == "expr") element.kind = K::Expr;
                    else if (capture == "stmt") element.kind = K::Statement;
                    else if (capture == "type") element.kind = K::Type;
                    else if (capture == "declaration") element.kind = K::Declaration;
                    else if (capture == "function_header") element.kind = K::FunctionHeader;
                    else if (capture == "function_decl") element.kind = K::FunctionDeclaration;
                    else if (capture == "function_def") element.kind = K::FunctionDefinition;
                    else if (capture == "rule") {
                        element.kind = K::Rule;
                        if (!take("(")) { error("expected '(' after rule"); return {}; }
                        element.rule_name = name_at(tokens, index);
                        if (element.rule_name.empty() || !take(")")) {
                            error("expected qualified rule name and ')'"); return {};
                        }
                    } else if (capture == "tokens_until") {
                        element.kind = K::TokensUntil;
                        if (!take("(") || index >= tokens.size() || tokens[index].kind != TokenKind::String) {
                            error("expected tokens_until(\";\")"); return {};
                        }
                        const auto fence = decode_string_literal(tokens[index++].text);
                        if (!fence || *fence != ";" || !take(")")) {
                            error("tokens_until requires a semicolon fence"); return {};
                        }
                    } else if (capture == "optional" || capture == "repeat0" || capture == "repeat1" ||
                               capture == "separated0" || capture == "separated1") {
                        element.kind = capture == "optional" ? K::Optional
                            : capture == "repeat0" ? K::Repeat0 : capture == "repeat1" ? K::Repeat1
                            : capture == "separated0" ? K::Separated0 : K::Separated1;
                        if (!take("(")) { error("expected '(' after syntax pattern combinator"); return {}; }
                        const bool separated = element.kind == K::Separated0 || element.kind == K::Separated1;
                        auto body = self(self, separated ? "," : ")", depth + 1);
                        if (!body) return {};
                        element.pattern = std::move(*body);
                        if (separated) {
                            if (!take(",") || index >= tokens.size() || tokens[index].kind != TokenKind::String) {
                                error("separated pattern requires a quoted one-token separator"); return {};
                            }
                            auto separator = execution_->terminal(tokens[index].text, tokens[index].location);
                            ++index;
                            if (!separator) return {};
                            element.terminal = std::move(*separator);
                        }
                        if (!take(")")) { error("expected ')' after syntax pattern combinator"); return {}; }
                    } else if (capture == "choice") {
                        element.kind = K::Choice;
                        if (!take("(")) { error("expected '(' after choice"); return {}; }
                        std::unordered_set<std::string> labels;
                        do {
                            if (index >= tokens.size() || !user_identifier(tokens[index])) {
                                error("expected choice alternative label"); return {};
                            }
                            SyntaxPatternElement::Alternative alternative;
                            alternative.label = tokens[index++].text;
                            if (!labels.insert(alternative.label).second) {
                                error("duplicate choice alternative label"); return {};
                            }
                            if (!take(":") || !take("(")) { error("expected ':(' after choice label"); return {}; }
                            auto body = self(self, ")", depth + 1);
                            if (!body || !take(")")) { error("expected ')' after choice alternative"); return {}; }
                            alternative.pattern = std::move(*body);
                            element.alternatives.push_back(std::move(alternative));
                        } while (take("|"));
                        if (!take(")")) { error("expected ')' after choice alternatives"); return {}; }
                    } else { error("syntax capture '" + std::string(capture) + "' is not implemented yet"); return {}; }
                }
                pattern.push_back(std::move(element));
            }
            if (pattern.empty() || !delimiters.empty()) {
                error("syntax match must be nonempty and balanced"); return {};
            }
            for (std::size_t at = 0; at < pattern.size(); ++at)
                if (pattern[at].kind == SyntaxPatternElement::Kind::TokensUntil &&
                    (at + 1 == pattern.size() || pattern[at + 1].kind != SyntaxPatternElement::Kind::Terminal ||
                     pattern[at + 1].terminal.text != ";")) {
                    error("tokens_until must be followed immediately by terminal ';'"); return {};
                }
            return pattern;
        };
        auto pattern = parse_pattern(parse_pattern, ";", 0);
        if (!pattern) return false;
        if (!take(";")) return error("expected ';' after syntax match");
        definition.pattern = std::move(*pattern);
        PatternAnalysis analysis(*execution_, diagnostics,
            [](SyntaxEntityId) -> const Pattern* { return nullptr; });
        if (!validate_pattern_progress(definition.pattern, analysis, diagnostics)) return false;
        if (*selected != SyntaxKind::Rule) {
            if (!take("expand")) return error("syntax definition requires an expand clause after match");
            definition.expander = name_at(tokens, index);
            if (definition.expander.empty() || !take(";")) return error("expected expander name and ';'");
        }
    }
    if (!take("}")) return error("duplicate, misplaced, or unsupported syntax clause");
    if (std::any_of(definitions_->begin(), definitions_->end(), [&](const auto& prior) { return prior.name == name; }))
        return error("syntax entity is defined more than once: '" + name + "'");
    definitions_->push_back(std::move(definition));
    visible_definitions_ = definitions_->size();
    note.complete = true;
    return true;
}

bool SyntaxState::activate(std::span<const SyntaxActivation> entries, std::string_view name_space,
    Diagnostics& diagnostics) {
    std::vector<SyntaxBinding> proposed;
    std::vector<SyntaxEntityId> path;
    struct PendingRules {
        SyntaxEntityId definition;
        std::vector<SyntaxPatternElement> pattern;
    };
    std::vector<PendingRules> pending_rules;
    std::vector<std::pair<SyntaxEntityId, SyntaxFunctionId>> pending_expanders;
    std::vector<SyntaxEntityId> resolving_rules;
    struct BindingNote {
        Diagnostics& diagnostics;
        const SyntaxDefinition& definition;
        bool complete{};
        ~BindingNote() {
            if (!complete) diagnostics.note(definition.location,
                "while binding syntax '" + definition.name + "'");
        }
    };
    struct ActivationNote {
        Diagnostics& diagnostics;
        const SyntaxActivation& entry;
        const SyntaxDefinition& definition;
        bool complete{};
        ~ActivationNote() {
            if (complete) return;
            diagnostics.note(entry.location, "while activating syntax '" + entry.name + "'");
            diagnostics.note(definition.location, "syntax '" + definition.name + "' defined here");
        }
    };
    const auto validate_rules = [&](const auto& self, const SyntaxDefinition& definition) -> bool {
        if (definition.rules_bound || std::any_of(pending_rules.begin(), pending_rules.end(),
            [&](const auto& pending) { return pending.definition == definition.id; })) return true;
        // A cycle may be productive. Stage its stable IDs first, then reject
        // only paths that can recurse without consuming input.
        if (std::find(resolving_rules.begin(), resolving_rules.end(), definition.id) != resolving_rules.end())
            return true;
        BindingNote note{diagnostics, definition};
        if (resolving_rules.size() >= execution_->limits().depth) {
            diagnostics.error(definition.location, "syntax rule binding depth exceeded");
            return false;
        }
        resolving_rules.push_back(definition.id);
        PendingRules pending{definition.id, definition.pattern};
        const auto bind = [&](const auto& walk, std::vector<SyntaxPatternElement>& pattern) -> bool {
            for (auto& element : pattern) {
                if (!execution_->work(element.location)) return false;
                if (element.kind == SyntaxPatternElement::Kind::Rule) {
                    const auto id = lookup(element.rule_name, definition.name_space, definition.imports,
                                           element.location, diagnostics);
                    if (!id) return false;
                    const auto& rule = (*definitions_)[id->value];
                    if (rule.kind != SyntaxKind::Rule) {
                        diagnostics.error(element.location, "rule reference must denote a syntax rule");
                        return false;
                    }
                    if (!self(self, rule)) return false;
                    element.resolved_rule = *id;
                }
                if (!walk(walk, element.pattern)) return false;
                for (auto& alternative : element.alternatives)
                    if (!walk(walk, alternative.pattern)) return false;
            }
            return true;
        };
        if (!bind(bind, pending.pattern)) return false;
        resolving_rules.pop_back();
        pending_rules.push_back(std::move(pending));
        note.complete = true;
        return true;
    };
    const auto flatten = [&](const auto& self, const SyntaxActivation& entry, std::string_view context,
                             const std::vector<std::vector<std::string>>& imports) -> bool {
        const auto id = lookup(entry.name, context, imports, entry.location, diagnostics);
        if (!id) return false;
        const auto& definition = (*definitions_)[id->value];
        ActivationNote note{diagnostics, entry, definition};
        if (!execution_->work(entry.location)) return false;
        if (definition.kind == SyntaxKind::Rule) {
            diagnostics.error(entry.location, "a syntax rule cannot be activated");
            return false;
        }
        if (definition.kind == SyntaxKind::Bundle) {
            if (entry.alias) {
                diagnostics.error(entry.location, "a syntax bundle cannot be aliased");
                return false;
            }
            if (path.size() >= execution_->limits().depth || std::find(path.begin(), path.end(), *id) != path.end()) {
                diagnostics.error(entry.location, "cyclic syntax bundle graph or excessive depth");
                return false;
            }
            path.push_back(*id);
            for (const auto& use : definition.uses)
                if (!self(self, use, definition.name_space, definition.imports)) return false;
            path.pop_back();
            note.complete = true;
            return true;
        }
        const auto expander = definition.bound_expander ? definition.bound_expander
            : execution_->find_function(definition.expander, definition.name_space, definition.imports, true);
        if (!expander) {
            diagnostics.error(entry.location, "syntax expander is not visible: '" + definition.expander + "'");
            return false;
        }
        if (!definition.bound_expander) pending_expanders.emplace_back(*id, *expander);
        if (!validate_rules(validate_rules, definition)) return false;
        SyntaxBinding binding{definition.kind == SyntaxKind::Item ? SyntaxBinding::Family::Item
            : SyntaxBinding::Family::StatementExpression, entry.alias.value_or(definition.prefix), *id};
        for (const auto& current : bindings()) {
            if (current.family == binding.family && current.prefix == binding.prefix && current.entity != binding.entity) {
                diagnostics.error(entry.location, "syntax prefix conflicts with an inherited binding: '" + binding.prefix + "'");
                return false;
            }
        }
        for (const auto& current : proposed) {
            if (current.family == binding.family && current.prefix == binding.prefix && current.entity != binding.entity) {
                diagnostics.error(entry.location, "conflicting syntax prefixes in activation: '" + binding.prefix + "'");
                return false;
            }
        }
        if (std::find(proposed.begin(), proposed.end(), binding) == proposed.end()) proposed.push_back(std::move(binding));
        note.complete = true;
        return true;
    };
    for (const auto& entry : entries) if (!flatten(flatten, entry, name_space, imports_)) return false;
    const RulePattern bound_pattern = [&](SyntaxEntityId id) -> const Pattern* {
        for (const auto& pending : pending_rules)
            if (pending.definition == id) return &pending.pattern;
        return id.value < definitions_->size() ? &(*definitions_)[id.value].pattern : nullptr;
    };
    // Least fixed point of rule nullability. A recursive reference does not
    // become nullable merely because it points back to itself; it needs a
    // finite derivation that consumes no token.
    std::vector<bool> nullable(definitions_->size());
    bool analysis_failed{};
    std::function<bool(const Pattern&)> sequence_nullable;
    std::function<bool(const SyntaxPatternElement&)> element_nullable;
    element_nullable = [&](const SyntaxPatternElement& element) {
        if (!execution_->work(element.location)) {
            analysis_failed = true;
            return false;
        }
        using K = SyntaxPatternElement::Kind;
        if (element.kind == K::Optional || element.kind == K::Repeat0 || element.kind == K::Separated0)
            return true;
        if (element.kind == K::Repeat1 || element.kind == K::Separated1)
            return sequence_nullable(element.pattern);
        if (element.kind == K::Choice)
            return std::any_of(element.alternatives.begin(), element.alternatives.end(),
                [&](const auto& alternative) { return sequence_nullable(alternative.pattern); });
        if (element.kind == K::Rule && element.resolved_rule)
            return static_cast<bool>(nullable[element.resolved_rule->value]);
        return false;
    };
    sequence_nullable = [&](const Pattern& pattern) {
        return std::all_of(pattern.begin(), pattern.end(), element_nullable);
    };
    for (std::size_t round = 0; round < definitions_->size(); ++round) {
        bool changed = false;
        for (const auto& definition : *definitions_) {
            const bool available = definition.rules_bound || std::any_of(pending_rules.begin(), pending_rules.end(),
                [&](const auto& pending) { return pending.definition == definition.id; });
            if (!available || nullable[definition.id.value]) continue;
            if (!execution_->work(definition.location)) return false;
            if (sequence_nullable(*bound_pattern(definition.id))) {
                nullable[definition.id.value] = true;
                changed = true;
            }
            if (analysis_failed) return false;
        }
        if (!changed) break;
    }
    struct LeadingRule { SyntaxEntityId target; SourceLocation location; };
    std::vector<std::vector<LeadingRule>> leading(definitions_->size());
    std::function<void(const Pattern&, std::vector<LeadingRule>&)> collect_sequence;
    std::function<void(const SyntaxPatternElement&, std::vector<LeadingRule>&)> collect_element;
    collect_element = [&](const SyntaxPatternElement& element, std::vector<LeadingRule>& output) {
        if (!execution_->work(element.location)) {
            analysis_failed = true;
            return;
        }
        using K = SyntaxPatternElement::Kind;
        if (element.kind == K::Rule && element.resolved_rule)
            output.push_back({*element.resolved_rule, element.location});
        else if (element.kind == K::Optional || element.kind == K::Repeat0 ||
                 element.kind == K::Repeat1 || element.kind == K::Separated0 ||
                 element.kind == K::Separated1)
            collect_sequence(element.pattern, output);
        else if (element.kind == K::Choice)
            for (const auto& alternative : element.alternatives)
                collect_sequence(alternative.pattern, output);
    };
    collect_sequence = [&](const Pattern& pattern, std::vector<LeadingRule>& output) {
        for (const auto& element : pattern) {
            if (analysis_failed) return;
            collect_element(element, output);
            if (analysis_failed) return;
            if (!element_nullable(element)) break;
        }
    };
    for (const auto& pending : pending_rules) {
        if (!execution_->work((*definitions_)[pending.definition.value].location)) return false;
        collect_sequence(pending.pattern, leading[pending.definition.value]);
        if (analysis_failed) return false;
    }
    std::vector<std::uint8_t> visited(definitions_->size());
    std::vector<SyntaxEntityId> leading_path;
    const auto check_leading = [&](const auto& self, SyntaxEntityId id) -> bool {
        if (visited[id.value] == 2) return true;
        visited[id.value] = 1;
        leading_path.push_back(id);
        for (const auto& edge : leading[id.value]) {
            if (!execution_->work(edge.location)) return false;
            if (visited[edge.target.value] == 1) {
                diagnostics.error(edge.location, "left-recursive or nullable syntax rule cycle");
                const auto first = std::find(leading_path.begin(), leading_path.end(), edge.target);
                for (auto at = first; at != leading_path.end(); ++at) {
                    const auto& rule = (*definitions_)[at->value];
                    diagnostics.note(rule.location, "syntax rule '" + rule.name + "' participates in this cycle");
                }
                return false;
            }
            if (visited[edge.target.value] == 0 && !self(self, edge.target)) return false;
        }
        leading_path.pop_back();
        visited[id.value] = 2;
        return true;
    };
    for (const auto& pending : pending_rules)
        if (visited[pending.definition.value] == 0 && !check_leading(check_leading, pending.definition)) return false;
    PatternAnalysis progress(*execution_, diagnostics, bound_pattern);
    for (const auto& pending : pending_rules)
        if (!validate_pattern_progress(pending.pattern, progress, diagnostics)) {
            const auto& definition = (*definitions_)[pending.definition.value];
            diagnostics.note(definition.location, "while validating syntax '" + definition.name + "'");
            return false;
        }
    // Commit lookup identities together with the entire activation. Later
    // declarations must not retarget a grammar already bound at activation.
    for (const auto& pending : pending_rules) {
        auto& definition = (*definitions_)[pending.definition.value];
        definition.pattern = pending.pattern;
        definition.rules_bound = true;
    }
    for (const auto& [id, expander] : pending_expanders) (*definitions_)[id.value].bound_expander = expander;
    const auto inherited = bindings();
    for (auto& binding : proposed)
        if (std::find(inherited.begin(), inherited.end(), binding) == inherited.end())
            scopes_.back().push_back(std::move(binding));
    return true;
}

const SyntaxDefinition* SyntaxState::selected(const Token& token, bool item) const {
    const auto origin = token_origin(token.location);
    const auto active = origin.context ? origin.context->syntax_bindings : bindings();
    for (const auto& binding : active) {
        if ((binding.family == SyntaxBinding::Family::Item) == item && binding.prefix == token.text &&
            binding.entity.value < definitions_->size()) return &(*definitions_)[binding.entity.value];
    }
    return nullptr;
}

std::optional<SyntaxState::Match> SyntaxState::match(const SyntaxDefinition& definition,
    const std::vector<Token>& tokens, std::size_t begin, Diagnostics& diagnostics,
    const std::function<std::optional<SyntaxParsedFragment>(
        SyntaxPatternElement::Kind, std::size_t)>& parse_fragment) const {
    const auto previous_errors = diagnostics.errors();
    PatternAnalysis analysis(*execution_, diagnostics, [&](SyntaxEntityId id) -> const Pattern* {
        return id.value < definitions_->size() ? &(*definitions_)[id.value].pattern : nullptr;
    });
    std::uint64_t storage{};
    bool failed{};
    const auto charge = [&](std::uint64_t amount) {
        const auto limit = std::min(execution_->limits().bytes, execution_->limits().memory);
        if (amount > limit - std::min(storage, limit)) {
            diagnostics.error(tokens[begin].location, "syntax match record byte or memory budget exceeded");
            return false;
        }
        storage += amount;
        return true;
    };
    const auto copy_tokens = [&](TokenSequence& destination, std::size_t first, std::size_t last) {
        for (auto at = first; at < last; ++at) {
            if (!charge(meta_token_storage_bytes + tokens[at].text.size()) ||
                !charge(token_binding_storage(tokens[at])) ||
                (tokens[at].split_source &&
                 !charge(32 + tokens[at].split_source->spelling.size()))) return false;
            destination.emplace_back(tokens[at]);
        }
        return true;
    };
    const auto raw_group = [&](std::size_t first, std::size_t last)
        -> std::shared_ptr<const SyntaxNode> {
        std::shared_ptr<SyntaxNode> root;
        std::vector<std::shared_ptr<SyntaxNode>> groups;
        for (auto at = first; at < last; ++at) {
            if (!execution_->work(tokens[at].location)) { failed = true; return {}; }
            const auto origin = token_origin(tokens[at].location);
            if (!closer(tokens[at].text).empty()) {
                if (!execution_->work(tokens[at].location) || !charge(128) ||
                    (!groups.empty() && !charge(16))) { failed = true; return {}; }
                auto group = std::make_shared<SyntaxNode>();
                group->kind = SyntaxNode::Kind::Group;
                group->span = {origin.span, origin.span};
                group->context = origin.context;
                if (groups.empty()) root = group;
                else groups.back()->children.push_back(group);
                groups.push_back(std::move(group));
            }
            // group_end has already validated balance and depth. Build only
            // public token/group nodes, never parsing or expanding contents.
            if (groups.empty() || !charge(128 + 16)) { failed = true; return {}; }
            auto leaf = std::make_shared<SyntaxNode>();
            leaf->span = {origin.span, origin.span};
            leaf->context = origin.context;
            if (!copy_tokens(leaf->tokens, at, at + 1)) { failed = true; return {}; }
            groups.back()->children.push_back(std::move(leaf));
            if (closing(tokens[at].text)) {
                groups.back()->span.last = origin.span;
                groups.pop_back();
            }
        }
        return root;
    };
    using FieldKind = SyntaxMatchValue::Field::Kind;
    const auto span = [&](std::size_t first, std::size_t last) {
        return SyntaxSpan{token_origin(tokens[first].location).span,
            token_origin(tokens[last == first ? first : last - 1].location).span};
    };
    struct RuleFrame {
        SyntaxEntityId id;
        SourceLocation reference;
    };
    struct Candidate {
        std::size_t end;
        std::shared_ptr<const SyntaxMatchValue> value;
        std::vector<RuleFrame> rules;
    };
    struct Piece {
        std::size_t end;
        std::vector<std::shared_ptr<const SyntaxMatchValue>> records;
        FieldKind kind{FieldKind::Primitive};
        std::shared_ptr<const SyntaxNode> node;
        std::vector<RuleFrame> rules;
    };
    using K = SyntaxPatternElement::Kind;
    std::vector<RuleFrame> active_rules;
    std::vector<RuleFrame> failure_rules;
    std::optional<std::vector<RuleFrame>> hard_failure_rules;
    struct CommittedFailure {
        std::size_t at;
        bool separator;
        std::vector<RuleFrame> rules;
    };
    std::optional<CommittedFailure> committed_failure;
    const auto remember_commit = [&](std::size_t at, bool separator) {
        if (!committed_failure || at > committed_failure->at ||
            (at == committed_failure->at && active_rules.size() > committed_failure->rules.size())) {
            if (!charge(64 + 32 * active_rules.size())) { failed = true; return; }
            committed_failure = CommittedFailure{at, separator, active_rules};
        }
    };
    std::size_t farthest_failure{};
    const auto consider_failure = [&](std::size_t at) {
        if (at > farthest_failure || (at == farthest_failure && active_rules.size() > failure_rules.size())) {
            farthest_failure = at;
            failure_rules = active_rules;
        }
    };
    std::vector<bool> noted_rules(definitions_->size());
    const auto note_rules = [&](const std::vector<RuleFrame>& rules) {
        for (const auto& frame : rules) {
            const auto id = frame.id;
            if (id.value >= definitions_->size() || noted_rules[id.value]) continue;
            noted_rules[id.value] = true;
            const auto& rule = (*definitions_)[id.value];
            diagnostics.note(frame.reference, "while matching syntax rule '" + rule.name + "'");
            diagnostics.note(rule.location, "syntax rule '" + rule.name + "' defined here");
        }
    };
    std::function<std::vector<Candidate>(const Pattern&, std::size_t, unsigned)> run;
    std::function<std::vector<Piece>(const SyntaxPatternElement&, std::size_t, unsigned)> pieces;
    run = [&](const Pattern& pattern, std::size_t first, unsigned depth) -> std::vector<Candidate> {
        if (failed) return {};
        consider_failure(first);
        if (depth >= execution_->limits().depth) {
            diagnostics.error(tokens[begin].location, "syntax pattern matching depth exceeded");
            failed = true;
            return {};
        }
        if (!charge(syntax_match_storage_bytes)) { failed = true; return {}; }
        std::vector<Candidate> active{{first, std::make_shared<SyntaxMatchValue>(), {}}};
        for (const auto& element : pattern) {
            std::vector<Candidate> next;
            for (const auto& candidate : active) {
                if (candidate.end >= tokens.size() || failed) continue;
                consider_failure(candidate.end);
                if (!execution_->work(tokens[candidate.end].location)) { failed = true; break; }
                for (auto part : pieces(element, candidate.end, depth + 1)) {
                    if (failed || !charge(syntax_match_storage_bytes +
                        syntax_field_storage_bytes * candidate.value->fields.size() +
                        32 * (candidate.rules.size() + part.rules.size()))) { failed = true; break; }
                    auto value = std::make_shared<SyntaxMatchValue>(*candidate.value);
                    if (!element.field.empty()) {
                        SyntaxMatchValue::Field field{element.field, {}, std::move(part.node),
                            std::move(part.records), part.kind, span(candidate.end, part.end)};
                        if (!charge(syntax_field_storage_bytes + field.name.size())) { failed = true; break; }
                        if ((part.kind == FieldKind::Primitive || part.kind == FieldKind::RawGroup) &&
                            !copy_tokens(field.tokens, candidate.end, part.end)) {
                            failed = true; break;
                        }
                        value->fields.push_back(std::move(field));
                    }
                    auto rules = candidate.rules;
                    rules.insert(rules.end(), part.rules.begin(), part.rules.end());
                    next.push_back({part.end, std::move(value), std::move(rules)});
                }
            }
            if (failed) return {};
            active = std::move(next);
            if (active.empty()) return {};
        }
        for (auto& candidate : active) {
            auto value = std::make_shared<SyntaxMatchValue>(*candidate.value);
            value->span = span(first, candidate.end);
            value->context = token_origin(tokens[first].location).context;
            if (!copy_tokens(value->input, first, candidate.end)) { failed = true; return {}; }
            candidate.value = std::move(value);
        }
        return active;
    };
    pieces = [&](const SyntaxPatternElement& element, std::size_t start, unsigned depth) -> std::vector<Piece> {
        if (failed || start >= tokens.size()) return {};
        consider_failure(start);
        if (tokens[start].kind == TokenKind::End && element.kind != K::Optional &&
            element.kind != K::Repeat0 && element.kind != K::Separated0) return {};
        const auto nested = [&](const Pattern& pattern, std::size_t from) {
            return run(pattern, from, depth);
        };
        if (element.kind == K::Rule) {
            if (!element.resolved_rule) return {};
            std::vector<Piece> output;
            active_rules.push_back({*element.resolved_rule, element.location});
            auto candidates = nested((*definitions_)[element.resolved_rule->value].pattern, start);
            if (failed && !hard_failure_rules) hard_failure_rules = active_rules;
            active_rules.pop_back();
            for (auto& candidate : candidates) {
                auto rules = std::move(candidate.rules);
                rules.insert(rules.begin(), {*element.resolved_rule, element.location});
                output.push_back({candidate.end, {candidate.value}, FieldKind::Nested, {}, std::move(rules)});
            }
            return output;
        }
        if (element.kind == K::Choice) {
            std::vector<Piece> output;
            for (const auto& alternative : element.alternatives)
                for (auto& candidate : nested(alternative.pattern, start)) {
                    if (!charge(syntax_match_storage_bytes + alternative.label.size())) { failed = true; return {}; }
                    auto value = std::make_shared<SyntaxMatchValue>(*candidate.value);
                    value->variant = alternative.label;
                    for (const auto& possible : element.alternatives) {
                        if (!charge(32 + possible.label.size())) { failed = true; return {}; }
                        value->variant_labels.push_back(possible.label);
                    }
                    output.push_back({candidate.end, {std::move(value)}, FieldKind::Nested, {},
                                      std::move(candidate.rules)});
                }
            return output;
        }
        if (element.kind == K::Optional) {
            std::vector<Piece> output{{start, {}, FieldKind::Nested, {}, {}}};
            for (auto& candidate : nested(element.pattern, start))
                if (candidate.end > start) output.push_back({candidate.end, {candidate.value}, FieldKind::Nested,
                                                               {}, std::move(candidate.rules)});
            return output;
        }
        if (element.kind == K::Repeat0 || element.kind == K::Repeat1 ||
            element.kind == K::Separated0 || element.kind == K::Separated1) {
            std::vector<Piece> output;
            const bool separated = element.kind == K::Separated0 || element.kind == K::Separated1;
            const bool require_one = element.kind == K::Repeat1 || element.kind == K::Separated1;
            const auto extend = [&](const auto& self, std::size_t at,
                                    std::vector<std::shared_ptr<const SyntaxMatchValue>>& records,
                                    std::vector<RuleFrame>& rules) -> bool {
                if (failed) return false;
                if (records.size() >= execution_->limits().depth) {
                    diagnostics.error(tokens[begin].location, "syntax repetition depth exceeded");
                    failed = true;
                    return false;
                }
                const auto finish = [&] {
                    if (require_one && records.empty()) return false;
                    if (!charge(128 + 16 * records.size() + 32 * rules.size())) { failed = true; return false; }
                    output.push_back({at, records, FieldKind::Nested, {}, rules});
                    return true;
                };
                std::size_t body = at;
                if (separated && !records.empty()) {
                    if (at >= tokens.size() || tokens[at].kind != element.terminal.kind ||
                        tokens[at].text != element.terminal.text) return finish();
                    body = at + 1;
                }
                auto candidates = nested(element.pattern, body);
                if (failed) return false;
                if (candidates.empty() && body < tokens.size()) {
                    const auto& first = analysis.sequence(element.pattern).first;
                    if (!analysis.good()) { failed = true; return false; }
                    if ((separated && !records.empty()) ||
                        (tokens[body].kind != TokenKind::End &&
                         first_accepts(first, tokens[body].kind, tokens[body].text))) {
                        // This derivation cannot stop before malformed input,
                        // but a sibling choice/optional derivation can still
                        // match. Keep the diagnostic only if the entire owner
                        // has no successful derivation. Resource failures use
                        // the separate, global `failed` state.
                        remember_commit(body, separated && !records.empty());
                        return false;
                    }
                }
                if (candidates.empty()) return finish();
                bool viable = false;
                for (auto& candidate : candidates) {
                    if (candidate.end <= body) {
                        diagnostics.error(tokens[begin].location, "syntax repetition body must consume input");
                        failed = true;
                        return false;
                    }
                    records.push_back(candidate.value);
                    const auto previous = rules.size();
                    rules.insert(rules.end(), candidate.rules.begin(), candidate.rules.end());
                    viable |= self(self, candidate.end, records, rules);
                    rules.resize(previous);
                    records.pop_back();
                    if (failed) return false;
                }
                // Retain shorter complete lengths only along a viable chain.
                // Otherwise a later malformed item would be silently treated
                // as the end of this same repetition.
                if (viable) (void)finish();
                return viable;
            };
            std::vector<std::shared_ptr<const SyntaxMatchValue>> records;
            std::vector<RuleFrame> rules;
            extend(extend, start, records, rules);
            return output;
        }
        std::size_t position = start;
        if (element.kind == K::Terminal) {
            if (tokens[position].kind != element.terminal.kind || tokens[position].text != element.terminal.text) return {};
            ++position;
        } else if (element.kind == K::Ident) {
            if (!user_identifier(tokens[position])) return {};
            ++position;
        } else if (element.kind == K::Name) {
            if (name_at(tokens, position).empty()) return {};
        } else if (element.kind == K::Literal) {
            const auto kind = tokens[position].kind;
            if (kind != TokenKind::Integer && kind != TokenKind::Floating &&
                kind != TokenKind::String && kind != TokenKind::Character) return {};
            ++position;
        } else if (element.kind == K::TokensUntil) {
            while (position < tokens.size() && tokens[position].kind != TokenKind::End && !tokens[position].is(";")) {
                if (!execution_->work(tokens[position].location)) { failed = true; return {}; }
                if (closing(tokens[position].text)) return {};
                if (const auto close = group_end(tokens, position, *execution_)) position = *close + 1;
                else if (!closer(tokens[position].text).empty()) return {};
                else ++position;
            }
            if (position == start || position >= tokens.size() || !tokens[position].is(";")) return {};
        } else if (element.kind == K::FunctionRaw) {
            // Let the core declarator grammar locate the header boundary.
            // The first written brace may instead belong to an inline tag,
            // and parentheses may belong to a function-pointer object. The
            // shared recognizer never parses or expands the following body.
            if (!parse_fragment) return {};
            const auto header = parse_fragment(K::FunctionHeader, start);
            if (!header || !header->node || header->end <= start ||
                header->end >= tokens.size() || !tokens[header->end].is("{")) return {};
            position = header->end;
            const auto close = group_end(tokens, position, *execution_);
            if (!close) return {};
            position = *close + 1;
        } else if (element.kind == K::Expr || element.kind == K::Statement ||
                   element.kind == K::Type || element.kind == K::Declaration ||
                   element.kind == K::FunctionHeader ||
                   element.kind == K::FunctionDeclaration ||
                   element.kind == K::FunctionDefinition) {
            if (!parse_fragment) return {};
            auto parsed = parse_fragment(element.kind, start);
            if (!parsed || !parsed->node || parsed->end <= start ||
                parsed->end > tokens.size()) return {};
            if (!execution_->work(tokens[start].location, parsed->end - start)) {
                failed = true;
                return {};
            }
            if (!charge(syntax_node_storage(*parsed->node,
                std::min(execution_->limits().bytes, execution_->limits().memory)))) {
                failed = true;
                return {};
            }
            return {{parsed->end, {}, FieldKind::Parsed, std::move(parsed->node), {}}};
        } else {
            const auto opening = tokens[position].text;
            if ((element.kind == K::Paren && opening != "(") ||
                (element.kind == K::Bracket && opening != "[") ||
                (element.kind == K::Block && opening != "{") || closer(opening).empty()) return {};
            const auto close = group_end(tokens, position, *execution_);
            if (!close) return {};
            position = *close + 1;
            auto node = raw_group(start, position);
            if (!node) return {};
            return {{position, {}, FieldKind::RawGroup, std::move(node), {}}};
        }
        if (!execution_->work(tokens[start].location, position - start)) { failed = true; return {}; }
        return {{position, {}, FieldKind::Primitive, {}, {}}};
    };
    auto matches = run(definition.pattern, begin + 1, 0);
    if (matches.empty() || failed) {
        if (!failed && committed_failure && diagnostics.errors() == previous_errors)
            diagnostics.error(tokens[committed_failure->at].location, committed_failure->separator
                ? "malformed syntax item after committed separator"
                : "malformed syntax repetition after committed start");
        if (diagnostics.errors() == previous_errors) diagnostics.error(tokens[begin].location,
            "syntax-match error for active prefix '" + std::string(tokens[begin].text) + "'");
        diagnostics.note(definition.location, "syntax '" + definition.name + "' defined here");
        if (hard_failure_rules) note_rules(*hard_failure_rules);
        else if (!failed && committed_failure) note_rules(committed_failure->rules);
        else if (!failed) note_rules(failure_rules);
        return {};
    }
    if (matches.size() != 1) {
        diagnostics.error(tokens[begin].location, "ambiguous syntax invocation has multiple complete derivations");
        diagnostics.note(definition.location, "syntax '" + definition.name + "' defined here");
        for (const auto& match : matches) note_rules(match.rules);
        return {};
    }
    auto root = std::make_shared<SyntaxMatchValue>(*matches.front().value);
    if (!charge(meta_token_storage_bytes + tokens[begin].text.size()) ||
        !charge(token_binding_storage(tokens[begin]))) {
        diagnostics.note(definition.location, "syntax '" + definition.name + "' defined here");
        note_rules(matches.front().rules);
        return {};
    }
    root->input.insert(root->input.begin(), MetaToken(tokens[begin]));
    root->span = span(begin, matches.front().end);
    if (!definition.bound_expander) return {};
    return Match{std::move(root), matches.front().end, *definition.bound_expander};
}

} // namespace cross
