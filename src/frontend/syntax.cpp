// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"
#include "frontend/embed.hpp"
#include "frontend/lexer.hpp"
#include "frontend/procedural.hpp"

#include <algorithm>
#include <functional>
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

} // namespace

SyntaxExecution::SyntaxExecution(SourceManager& sources, Diagnostics& diagnostics,
    unsigned address_bits, LayoutQuery size_of, LayoutQuery align_of,
    EvaluationLimits limits, EvaluationLayout layout)
    : sources_(sources), diagnostics_(diagnostics), address_bits_(address_bits),
      size_of_(std::move(size_of)), align_of_(std::move(align_of)), limits_(limits), layout_(layout) {}

std::vector<Token> SyntaxExecution::prepare(const SourceFile& source) {
    return Lexer(source, diagnostics_).lex();
}

bool SyntaxExecution::define_function(const std::vector<Token>& tokens, std::size_t& index, std::string_view name_space,
    const std::vector<std::string>& imports, const std::vector<SyntaxBinding>& bindings) {
    auto function = parse_expansion_function(tokens, index, diagnostics_);
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
    functions_.push_back({std::move(declaration), function->syntax_expander, bindings});
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
    if (depth_ >= limits_.depth || expansions_ >= 128) {
        diagnostics_.error(location, "syntax/procedural expansion depth or invocation budget exceeded");
        return false;
    }
    ++depth_;
    ++expansions_;
    return true;
}

void SyntaxExecution::end_replacement() { --depth_; }

std::shared_ptr<const SyntaxContext> SyntaxExecution::call_context(SourceLocation location,
    std::string_view name_space, const std::vector<std::string>& imports,
    const std::vector<SyntaxBinding>& bindings) const {
    auto context = std::make_shared<SyntaxContext>();
    context->invocation = location;
    context->name_space = name_space;
    context->imports = imports;
    context->syntax_bindings = bindings;
    return context;
}

std::optional<SyntaxExecution::Output> SyntaxExecution::expand(FunctionId id,
    TokenSequence input, std::shared_ptr<const SyntaxMatchValue> match, SourceLocation invocation,
    std::string_view name_space, const std::vector<std::string>& imports,
    const std::vector<SyntaxBinding>& bindings) {
    if (id.value >= functions_.size()) return {};
    const auto& function = functions_[id.value];
    const auto call = call_context(invocation, name_space, imports, bindings);
    const auto attach = [&](TokenSequence& tokens) {
        for (auto& token : tokens) if (!token.origin.context) token.origin.context = call;
    };
    attach(input);
    const auto contextualize = [&](const auto& self, const SyntaxMatchValue& source)
        -> std::shared_ptr<const SyntaxMatchValue> {
        auto value = std::make_shared<SyntaxMatchValue>(source);
        attach(value->input);
        for (auto& field : value->fields) {
            attach(field.tokens);
            for (auto& record : field.records) record = self(self, *record);
        }
        return value;
    };
    if (match) match = contextualize(contextualize, *match);
    const auto expansion = sources_.next_expansion();
    auto definition = std::make_shared<SyntaxContext>();
    definition->kind = SyntaxContext::Kind::DefinitionSite;
    definition->expansion = expansion;
    definition->definition = function.declaration.location;
    definition->invocation = invocation;
    definition->name_space = function.declaration.source_namespace;
    definition->imports = function.declaration.imports;
    definition->syntax_bindings = function.bindings;
    auto output = function.syntax_expander
        ? evaluate_syntax_body(function.declaration, std::move(match), address_bits_, size_of_, align_of_,
                               definition, diagnostics_, limits_, layout_)
        : evaluate_procedural_body(function.declaration, input, address_bits_, size_of_, align_of_,
                                   definition, diagnostics_, limits_, layout_);
    if (!output) {
        diagnostics_.note(function.declaration.location, "expansion function is defined here");
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
        origins.push_back({begin, text.size(), token.origin});
        text += ' ';
    }
    const auto end = text.size();
    const auto unit = invocation.file ? invocation.file->source_unit_at(invocation.line) : std::string{};
    std::vector<std::string> units(static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1, unit);
    const auto* source = sources_.add(invocation.file ? invocation.file->path : std::filesystem::path("<syntax>"),
        std::move(text), {{0, end,
            function.declaration.name, invocation, function.declaration.location}}, std::move(origins), {}, std::move(units));
    if (!validate_embeds(*source, diagnostics_)) return {};
    auto tokens = Lexer(*source, diagnostics_).lex();
    // Empty item replacements still need an ancestry-bearing source location.
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

void SyntaxState::push_scope() { scopes_.emplace_back(); imports_.emplace_back(); }
void SyntaxState::pop_scope() { scopes_.pop_back(); imports_.pop_back(); }
void SyntaxState::import(std::string name) { imports_.back().push_back(std::move(name)); }

std::vector<SyntaxBinding> SyntaxState::bindings() const {
    std::vector<SyntaxBinding> result;
    for (const auto& scope : scopes_) result.insert(result.end(), scope.begin(), scope.end());
    return result;
}

std::optional<SyntaxEntityId> SyntaxState::lookup(std::string_view name, std::string_view name_space,
    const std::vector<std::vector<std::string>>& imports, SourceLocation location,
    Diagnostics& diagnostics) const {
    const auto exact = [&](std::string_view candidate) -> std::optional<SyntaxEntityId> {
        for (const auto& definition : *definitions_)
            if (definition.name == candidate) return definition.id;
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
    const auto error = [&](std::string message) {
        diagnostics.error(index < tokens.size() ? tokens[index].location : start, std::move(message));
        return false;
    };
    const auto take = [&](std::string_view text) {
        if (index < tokens.size() && tokens[index].is(text)) { ++index; return true; }
        return false;
    };
    if (index >= tokens.size() || !user_identifier(tokens[index])) return error("expected nonreserved syntax entity name");
    const auto name = join(name_space, tokens[index++].text);
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
        std::unordered_set<std::string> fields;
        std::vector<std::string_view> delimiters;
        while (index < tokens.size() && !tokens[index].is(";") && tokens[index].kind != TokenKind::End) {
            if (!execution_->work(tokens[index].location)) return false;
            SyntaxPatternElement element{SyntaxPatternElement::Kind::Terminal, {}, {}, {}, tokens[index].location};
            if (tokens[index].kind == TokenKind::String) {
                auto terminal = execution_->terminal(tokens[index].text, tokens[index].location);
                ++index;
                if (!terminal) return false;
                element.terminal = std::move(*terminal);
                // Stored string views must not refer to a temporary element.
                if (const auto close = closer(element.terminal.text); !close.empty()) delimiters.push_back(close);
                else if (closing(element.terminal.text)) {
                    if (delimiters.empty() || delimiters.back() != element.terminal.text)
                        return error("syntax pattern terminals must balance delimiters");
                    delimiters.pop_back();
                }
            } else {
                if (!(tokens[index].is("rule") && index + 1 < tokens.size() && tokens[index + 1].is("("))) {
                    if (!user_identifier(tokens[index])) return error("expected syntax capture or quoted terminal");
                    element.field = tokens[index++].text;
                    if (!fields.insert(element.field).second) return error("duplicate syntax capture field");
                    if (!take(":")) return error("expected ':' after syntax capture field");
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
                else if (capture == "function" || capture == "function_raw")
                    element.kind = K::FunctionRaw;
                else if (capture == "rule") {
                    element.kind = K::Rule;
                    if (!take("(")) return error("expected '(' after rule");
                    element.rule_name = name_at(tokens, index);
                    if (element.rule_name.empty() || !take(")")) return error("expected qualified rule name and ')'");
                } else if (capture == "tokens_until") {
                    element.kind = K::TokensUntil;
                    if (!take("(") || index >= tokens.size() || tokens[index].kind != TokenKind::String)
                        return error("expected tokens_until(\";\")");
                    const auto stop = decode_string_literal(tokens[index++].text);
                    if (!stop || *stop != ";" || !take(")")) return error("tokens_until requires a semicolon fence");
                } else return error("syntax capture '" + std::string(capture) + "' is not implemented yet");
            }
            definition.pattern.push_back(std::move(element));
        }
        if (definition.pattern.empty() || !delimiters.empty() || !take(";"))
            return error("syntax match must be nonempty and balanced");
        for (std::size_t at = 0; at < definition.pattern.size(); ++at) {
            if (definition.pattern[at].kind == SyntaxPatternElement::Kind::TokensUntil &&
                (at + 1 == definition.pattern.size() || definition.pattern[at + 1].kind != SyntaxPatternElement::Kind::Terminal ||
                 definition.pattern[at + 1].terminal.text != ";"))
                return error("tokens_until must be followed immediately by terminal ';'");
        }
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
    return true;
}

bool SyntaxState::activate(std::span<const SyntaxActivation> entries, std::string_view name_space,
    Diagnostics& diagnostics) {
    std::vector<SyntaxBinding> proposed;
    std::vector<SyntaxEntityId> path;
    struct PendingRules {
        SyntaxEntityId definition;
        std::vector<std::pair<std::size_t, SyntaxEntityId>> references;
    };
    std::vector<PendingRules> pending_rules;
    std::vector<std::pair<SyntaxEntityId, SyntaxFunctionId>> pending_expanders;
    const auto validate_rules = [&](const auto& self, const SyntaxDefinition& definition) -> bool {
        if (definition.rules_bound || std::any_of(pending_rules.begin(), pending_rules.end(),
            [&](const auto& pending) { return pending.definition == definition.id; })) return true;
        if (path.size() >= execution_->limits().depth ||
            std::find(path.begin(), path.end(), definition.id) != path.end()) {
            diagnostics.error(definition.location, "recursive syntax rules are not implemented yet");
            return false;
        }
        path.push_back(definition.id);
        PendingRules pending{definition.id, {}};
        for (std::size_t at = 0; at < definition.pattern.size(); ++at) {
            const auto& element = definition.pattern[at];
            if (!execution_->work(element.location)) return false;
            if (element.kind != SyntaxPatternElement::Kind::Rule) continue;
            const auto id = lookup(element.rule_name, definition.name_space, definition.imports, element.location, diagnostics);
            if (!id) return false;
            const auto& rule = (*definitions_)[id->value];
            if (rule.kind != SyntaxKind::Rule) {
                diagnostics.error(element.location, "rule reference must denote a syntax rule");
                return false;
            }
            if (!self(self, rule)) return false;
            pending.references.emplace_back(at, *id);
        }
        path.pop_back();
        pending_rules.push_back(std::move(pending));
        return true;
    };
    const auto flatten = [&](const auto& self, const SyntaxActivation& entry, std::string_view context,
                             const std::vector<std::vector<std::string>>& imports) -> bool {
        const auto id = lookup(entry.name, context, imports, entry.location, diagnostics);
        if (!id || !execution_->work(entry.location)) return false;
        const auto& definition = (*definitions_)[id->value];
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
        return true;
    };
    for (const auto& entry : entries) if (!flatten(flatten, entry, name_space, imports_)) return false;
    // Commit lookup identities together with the entire activation. Later
    // declarations must not retarget a grammar already bound at activation.
    for (const auto& pending : pending_rules) {
        auto& definition = (*definitions_)[pending.definition.value];
        for (const auto& [at, id] : pending.references) definition.pattern[at].resolved_rule = id;
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
    const std::function<bool(std::size_t, std::size_t)>& function_header) const {
    std::size_t position = begin + 1;
    const auto previous_errors = diagnostics.errors();
    std::uint64_t storage{};
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
            if (!charge(128 + tokens[at].text.size())) return false;
            destination.emplace_back(tokens[at]);
        }
        return true;
    };
    const auto run = [&](const auto& self, const SyntaxDefinition& rule, unsigned depth)
        -> std::shared_ptr<const SyntaxMatchValue> {
        if (depth >= execution_->limits().depth) {
            diagnostics.error(tokens[begin].location, "syntax rule matching depth exceeded");
            return {};
        }
        if (!charge(128)) return {};
        auto value = std::make_shared<SyntaxMatchValue>();
        const auto first = position;
        for (const auto& element : rule.pattern) {
            if (position >= tokens.size() || !execution_->work(tokens[position].location)) return {};
            const auto start = position;
            SyntaxMatchValue::Field field{element.field, {}, {}};
            using K = SyntaxPatternElement::Kind;
            if (element.kind == K::Rule) {
                if (!element.resolved_rule) return {};
                auto nested = self(self, (*definitions_)[element.resolved_rule->value], depth + 1);
                if (!nested) return {};
                field.records.push_back(std::move(nested));
            } else if (element.kind == K::Terminal) {
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
                    if (!execution_->work(tokens[position].location)) return {};
                    if (closing(tokens[position].text)) return {};
                    if (const auto close = group_end(tokens, position, *execution_)) position = *close + 1;
                    else if (!closer(tokens[position].text).empty()) return {};
                    else ++position;
                }
                if (position == start || position >= tokens.size() || !tokens[position].is(";")) return {};
            } else if (element.kind == K::FunctionRaw) {
                // Find the first top-level body opener. Nested declarator,
                // attribute, and parameter groups are part of the header;
                // the brace body itself is never parsed by the core parser.
                while (position < tokens.size() && tokens[position].kind != TokenKind::End &&
                       !tokens[position].is("{") && !tokens[position].is(";")) {
                    if (!execution_->work(tokens[position].location)) return {};
                    if (const auto close = group_end(tokens, position, *execution_)) position = *close + 1;
                    else if (!closer(tokens[position].text).empty() || closing(tokens[position].text)) return {};
                    else ++position;
                }
                if (position >= tokens.size() || !tokens[position].is("{") ||
                    !function_header || !function_header(start, position)) return {};
                const auto close = group_end(tokens, position, *execution_);
                if (!close) return {};
                position = *close + 1;
            } else {
                const auto opening = tokens[position].text;
                if ((element.kind == K::Paren && opening != "(") ||
                    (element.kind == K::Bracket && opening != "[") ||
                    (element.kind == K::Block && opening != "{") || closer(opening).empty()) return {};
                const auto close = group_end(tokens, position, *execution_);
                if (!close) return {};
                position = *close + 1;
            }
            if (!execution_->work(tokens[start].location, position - start)) return {};
            if (!field.name.empty()) {
                if (!charge(64 + field.name.size())) return {};
                if (field.records.empty() && !copy_tokens(field.tokens, start, position)) return {};
                value->fields.push_back(std::move(field));
            }
        }
        if (!copy_tokens(value->input, first, position)) return {};
        return value;
    };
    auto value = run(run, definition, 0);
    if (!value) {
        if (diagnostics.errors() == previous_errors) diagnostics.error(tokens[begin].location,
            "syntax-match error for active prefix '" + std::string(tokens[begin].text) + "'");
        diagnostics.note(definition.location, "syntax is defined here");
        return {};
    }
    auto root = std::make_shared<SyntaxMatchValue>(*value);
    if (!charge(128 + tokens[begin].text.size())) return {};
    root->input.insert(root->input.begin(), MetaToken(tokens[begin]));
    if (!definition.bound_expander) return {};
    return Match{std::move(root), position, *definition.bound_expander};
}

} // namespace cross
