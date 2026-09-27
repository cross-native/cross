// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"
#include "frontend/name.hpp"

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
               std::shared_ptr<SyntaxExecution> execution)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {
    if (execution) syntax_.emplace(std::move(execution));
}

const Token& Parser::current(std::size_t lookahead) const {
    if (public_tree_failed_) return tokens_.back();
    const auto position = index_ + lookahead;
    return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
}

bool Parser::consume(std::string_view spelling) {
    if (!current().is(spelling)) return false;
    ++index_;
    return true;
}

const Token* Parser::consume_kind(TokenKind kind) {
    if (current().kind != kind) return nullptr;
    return &tokens_[index_++];
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
    while (current().kind != TokenKind::End) {
        if (consume(";")) return;
        if (current().is("}")) return;
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
    const auto location = current().location;
    const auto origin = token_origin(location);
    const auto& name_space = origin.context ? origin.context->name_space : active_namespace_;
    const auto& imports = origin.context ? origin.context->imports : active_imports_;
    const auto bindings = origin.context ? origin.context->syntax_bindings : syntax_->bindings();
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
                    error_here("mismatched procedural macro token-tree delimiter");
                    ++index_;
                    return {};
                }
                stack.pop_back();
            }
            if (!stack.empty()) input.emplace_back(current());
            ++index_;
        }
        if (!stack.empty()) {
            diagnostics_.error(location, "unterminated procedural macro token tree");
            return {};
        }
        const auto search_imports = origin.context
            ? std::vector<std::vector<std::string>>{imports} : syntax_->imports();
        const auto function = execution->find_function(*name, name_space, search_imports, false);
        if (!function) {
            diagnostics_.error(location, "procedural macro is not visible: '" + *name + "'");
            return {};
        }
        return execution->expand(*function, std::move(input), {}, location, name_space, imports, bindings);
    }
    const auto* definition = active_syntax(item);
    if (!definition) return {};
    const auto matched = syntax_->match(*definition, tokens_, index_, diagnostics_,
        [&](std::size_t first, std::size_t body_open) {
            return validate_syntax_function_header(first, body_open, name_space, imports);
        }, [&](SyntaxPatternElement::Kind kind, std::size_t first) {
            return parse_syntax_fragment(kind, first);
        });
    if (!matched) { ++index_; synchronize_external(); return {}; }
    index_ = matched->end;
    return execution->expand(matched->expander, {}, matched->value, location, name_space, imports, bindings);
}

bool Parser::validate_syntax_function_header(std::size_t first, std::size_t body_open,
    std::string_view name_space, const std::vector<std::string>& imports) const {
    // Parse the actual header in the caller's type/name environment, with a
    // synthetic empty body. A raw body belongs exclusively to the extension
    // and must not be core-parsed (or expanded) during recognition.
    std::vector<Token> header(tokens_.begin() + static_cast<std::ptrdiff_t>(first),
                              tokens_.begin() + static_cast<std::ptrdiff_t>(body_open + 1));
    header.push_back({TokenKind::Punctuator, "}", tokens_[body_open].location});
    header.push_back({TokenKind::End, {}, tokens_[body_open].location});
    auto child = replacement_parser({std::move(header), tokens_[first].location});
    child->syntax_.reset();
    child->replacement_ = false;
    child->active_namespace_ = name_space;
    child->active_imports_ = imports;
    const auto errors = diagnostics_.errors();
    Program parsed;
    child->parse_external(parsed, std::string(name_space));
    const bool direct = child->current().kind == TokenKind::End && parsed.functions.size() == 1 &&
        parsed.functions.front()->body && parsed.objects.empty() &&
        parsed.records.empty() && parsed.enumerations.empty() &&
        parsed.global_labels.empty();
    if (!direct) diagnostics_.error(tokens_[first].location,
        "syntax function capture requires a direct core function header");
    return direct && diagnostics_.errors() == errors;
}

std::unique_ptr<Parser> Parser::replacement_parser(SyntaxExecution::Output output,
    Diagnostics* diagnostics) const {
    auto child = std::make_unique<Parser>(std::move(output.tokens),
                                          diagnostics ? *diagnostics : diagnostics_);
    child->syntax_ = syntax_;
    child->replacement_ = true;
    child->active_imports_ = active_imports_;
    child->current_scope_imports_ = current_scope_imports_;
    child->active_generic_types_ = active_generic_types_;
    child->known_generic_functions_ = known_generic_functions_;
    child->known_ordinary_values_ = known_ordinary_values_;
    child->local_scopes_ = local_scopes_;
    child->active_namespace_ = active_namespace_;
    child->enum_types_ = enum_types_;
    child->record_types_ = record_types_;
    child->type_aliases_ = type_aliases_;
    child->active_function_ = active_function_;
    // Caller angle-token fences cannot constrain an independently bounded
    // replacement expression. Its own generic parses install their fences.
    child->parsing_generic_argument_ = false;
    child->parsing_procedural_body_ = parsing_procedural_body_;
    child->switch_depth_ = switch_depth_;
    child->switch_default_seen_ = switch_default_seen_;
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
    production_events_.push_back({production, index_, index_, {}, {}});
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
    node->context = token_origin(tokens_[source.first].location).context;
    const auto token_node = [&](std::size_t at) -> std::shared_ptr<const SyntaxNode> {
        auto leaf = std::make_shared<SyntaxNode>();
        leaf->kind = SyntaxNode::Kind::Token;
        leaf->tokens.emplace_back(tokens_[at]);
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
    if (kind == K::Expr) {
        (void)child->parse_assignment();
    } else if (kind == K::Type) {
        ProductionScope scope(*child, SyntaxProduction::TypeName);
        if (!child->type_start()) return {};
        auto type = child->parse_type();
        std::optional<std::string> name;
        (void)child->parse_declarator(std::move(type), name);
        if (name) return {};
    } else if (kind == K::Statement) {
        (void)child->parse_statement();
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
                                     !child->current().is("{"))) return {};
        } else if (kind == K::FunctionDeclaration) {
            if (!direct_function || parsed.functions.front()->body) return {};
        } else if (kind == K::FunctionDefinition) {
            if (!direct_function || !parsed.functions.front()->body) return {};
        } else if (std::any_of(parsed.functions.begin(), parsed.functions.end(),
                   [](const auto& function) { return function->body != nullptr; })) return {};
    } else {
        return {};
    }
    if (child->public_tree_failed_) {
        if (!execution) diagnostics_.error(tokens_[first].location,
            "public syntax tree depth, work, or storage budget exceeded");
        return {};
    }
    if (local.errors() != 0 || child->index_ <= first ||
        child->production_events_.empty()) return {};
    if (kind == K::Expr || kind == K::Type) {
        const auto& next = child->current();
        if (next.kind != TokenKind::End && !next.is(";") && !next.is(",") &&
            !next.is(")") && !next.is("]") && !next.is("]]") &&
            !next.is("}")) return {};
    }
    auto node = child->public_node(0);
    if (!node) return {};
    return SyntaxParsedFragment{child->public_input_indices_[child->index_], std::move(node)};
}

std::shared_ptr<const SyntaxNode> Parser::parse_opaque_invocation(SyntaxKind category) {
    if (!syntax_) return {};
    const auto first = index_;
    auto node = std::make_shared<SyntaxNode>();
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
            [&](std::size_t begin, std::size_t body_open) {
                return validate_syntax_function_header(begin, body_open, active_namespace_, active_imports_);
            }, [&](SyntaxPatternElement::Kind kind, std::size_t begin) {
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
    node->context = token_origin(tokens_[first].location).context;
    if (!node->context) node->context = syntax_->execution()->call_context(
        tokens_[first].location, active_namespace_, active_imports_, syntax_->bindings());
    for (auto at = first; at < index_; ++at) {
        MetaToken token(tokens_[at]);
        if (!token.origin.context) token.origin.context = node->context;
        node->tokens.push_back(std::move(token));
    }
    if (recording_public_tree_) {
        const auto event = production_events_.size();
        production_events_.push_back({SyntaxProduction::None, first, index_, {}, node});
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
    enum_types_ = std::move(child.enum_types_);
    record_types_ = std::move(child.record_types_);
    type_aliases_ = std::move(child.type_aliases_);
    switch_default_seen_ = std::move(child.switch_default_seen_);
    for (auto& assertion : child.static_assertions_)
        static_assertions_.push_back(std::move(assertion));
}

std::unique_ptr<Statement> Parser::parse_statement_replacement() {
    const auto location = current().location;
    const bool macro = macro_start();
    auto result = std::make_unique<Statement>();
    result->kind = Statement::Kind::Empty;
    result->location = location;
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) { ++index_; synchronize_external(); return result; }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = expand_at_position(false);
    if (!output) return result;
    auto child = replacement_parser(std::move(*output));
    // A macro at statement start may produce an expression whose semicolon
    // belongs to the caller. Custom statement syntax always owns a complete
    // statement; neither path can borrow an outside else or adjacent tokens.
    bool expression_macro = macro && child->current().kind != TokenKind::End &&
        !child->current().is("{") && !child->local_declaration_start();
    for (const auto keyword : {"return", "goto", "if", "switch", "while", "do", "for",
                               "break", "continue", "case", "default", "label", ";", "[["})
        if (child->current().is(keyword)) expression_macro = false;
    if (child->macro_start()) expression_macro = false;
    if (const auto* selected = child->active_syntax(false); selected && selected->kind == SyntaxKind::Statement)
        expression_macro = false;
    unsigned group_depth = 0;
    for (const auto& token : child->tokens_) {
        if (token.is("(") || token.is("[") || token.is("[[") || token.is("{")) ++group_depth;
        else if ((token.is(")") || token.is("]") || token.is("]]") || token.is("}")) && group_depth) --group_depth;
        else if (token.is(";") && group_depth == 0) expression_macro = false;
    }
    if (expression_macro) {
        result->kind = Statement::Kind::Expression;
        auto grouped = std::make_unique<Expr>();
        grouped->kind = Expr::Kind::Parenthesized;
        grouped->location = location;
        grouped->left = child->parse_assignment();
        result->expression = std::move(grouped);
    } else if (child->current().kind == TokenKind::End) {
        diagnostics_.error(location, "statement expansion must produce exactly one complete statement");
    } else result = child->parse_statement();
    if (child->current().kind != TokenKind::End)
        child->error_here("statement expansion must produce exactly one complete statement");
    const bool needs_semicolon = expression_macro || child->deferred_statement_semicolon_;
    adopt_replacement(*child);
    if (needs_semicolon) {
        if (!macro) diagnostics_.error(location, "statement expansion must produce exactly one complete statement");
        else {
            result->expression = parse_expression(std::move(result->expression));
            if (current().kind == TokenKind::End && replacement_) deferred_statement_semicolon_ = true;
            else expect(";", "after procedural expression statement");
        }
    }
    return result;
}

std::unique_ptr<Expr> Parser::parse_expression_replacement() {
    const auto location = current().location;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Integer;
    result->text = "0";
    result->location = location;
    if (const auto* definition = active_syntax(false); definition && definition->kind != SyntaxKind::Expression) {
        error_here("statement syntax is not valid at expression position");
        ++index_;
        return result;
    }
    auto execution = syntax_->execution();
    if (!execution->begin_replacement(location)) { ++index_; return result; }
    struct End { SyntaxExecution& execution; ~End() { execution.end_replacement(); } } end{*execution};
    auto output = expand_at_position(false);
    if (!output) return result;
    auto child = replacement_parser(std::move(*output));
    if (child->current().kind == TokenKind::End)
        diagnostics_.error(location, "expression expansion must produce one assignment expression");
    else result = child->parse_assignment();
    if (child->current().kind != TokenKind::End)
        child->error_here("expression expansion must produce one assignment expression without a semicolon");
    adopt_replacement(*child);
    // The parsed root is a subtree, so caller operators cannot reassociate
    // across the expansion boundary (even without textual parentheses).
    auto grouped = std::make_unique<Expr>();
    grouped->kind = Expr::Kind::Parenthesized;
    grouped->location = location;
    grouped->left = std::move(result);
    return grouped;
}

std::vector<Attribute> Parser::parse_attributes(bool one_specifier) {
    std::vector<Attribute> result;
    while (current().is("[[")) {
        ProductionScope attribute_specifier(*this, SyntaxProduction::AttributeSpecifier);
        consume("[[");
        do {
            ProductionScope attribute_production(*this, SyntaxProduction::Attribute);
            if (current().kind == TokenKind::BuiltinName) {
                diagnostics_.error(
                    current().location,
                    "attributes are contextual names; omit the '$::' prefix");
                ++index_;
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            const auto name_event = begin_production(SyntaxProduction::AttributeName);
            const auto* first = consume_kind(TokenKind::Identifier);
            if (!first) {
                end_production(name_event);
                error_here("expected attribute name");
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            std::string name(first->text);
            while (consume("::")) {
                const auto* component = consume_kind(TokenKind::Identifier);
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
                if (attribute.name == "generic") {
                    const auto saved_generic_types = active_generic_types_;
                    while (!current().is(")") &&
                           current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto parameter_location = current().location;
                        std::optional<std::string> parameter_name;
                        TypePtr value_type;
                        if (current().kind == TokenKind::Identifier &&
                            (current(1).is(",") || current(1).is(")"))) {
                            parameter_name = std::string(current().text);
                            ++index_;
                            active_generic_types_.push_back(*parameter_name);
                        } else {
                            value_type = parse_type();
                            if (value_type) {
                                value_type = parse_declarator(
                                    std::move(value_type), parameter_name, false,
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
                } else if (attribute.name == "aligned") {
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
    for (const auto& attribute : parse_attributes(true)) {
        if (attribute.name == "atomic") {
            if (!attribute.arguments.empty()) {
                diagnostics_.error(attribute.location,
                                   "'atomic' takes no arguments");
            } else if (type->is_atomic) {
                diagnostics_.error(attribute.location,
                                   "duplicate 'atomic' type qualifier");
            } else {
                type->is_atomic = true;
            }
            continue;
        }
        if (attribute.name == "address_space") {
            if (attribute.arguments.size() != 1) {
                diagnostics_.error(attribute.location,
                                   "address_space requires one target registry number");
                continue;
            }
            auto digits = attribute.arguments.front();
            digits.erase(std::remove(digits.begin(), digits.end(), '_'),
                         digits.end());
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
                continue;
            }
            if (pending_address_space) {
                if (*pending_address_space) {
                    diagnostics_.error(attribute.location,
                                       "duplicate address_space type qualifier");
                } else {
                    *pending_address_space =
                        std::pair{number, attribute.location};
                }
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
            continue;
        }
        diagnostics_.error(attribute.location,
                           "attribute '" + attribute.name +
                               "' is not valid as a type qualifier here");
    }
}

std::optional<std::string> Parser::parse_qualified_name(SyntaxProduction production_name) {
    if (current().kind != TokenKind::Identifier) return std::nullopt;
    ProductionScope production(*this, production_name);
    const auto* first = consume_kind(TokenKind::Identifier);
    if (!first) return std::nullopt;
    std::string name(first->text);
    while (current().is("::") && current(1).kind == TokenKind::Identifier) {
        consume("::");
        const auto* component = consume_kind(TokenKind::Identifier);
        if (!component) {
            error_here("expected identifier after '::'");
            break;
        }
        name += "::";
        name += component->text;
    }
    return name;
}

std::string Parser::peek_qualified_name() const {
    if (current().kind != TokenKind::Identifier) return {};
    std::string result(current().text);
    auto position = index_ + 1;
    while (position + 1 < tokens_.size() &&
           tokens_[position].is("::") &&
           tokens_[position + 1].kind == TokenKind::Identifier) {
        result += "::";
        result += tokens_[position + 1].text;
        position += 2;
    }
    return result;
}

TypePtr Parser::resolve_type_alias(std::string_view name) const {
    const auto find = [&](std::string_view candidate) -> TypePtr {
        const auto found = type_aliases_.find(std::string(candidate));
        return found == type_aliases_.end() ? TypePtr{} : found->second;
    };
    if (const auto exact = find(name)) return exact;
    if (name.find("::") != std::string_view::npos) return {};
    auto current_namespace = active_namespace_;
    while (!current_namespace.empty()) {
        if (const auto local = find(join_namespace(current_namespace, name))) {
            return local;
        }
        const auto separator = current_namespace.rfind("::");
        if (separator == std::string::npos) break;
        current_namespace.resize(separator);
    }
    for (const auto& imported : active_imports_) {
        if (const auto type = find(join_namespace(imported, name))) return type;
    }
    return {};
}

bool Parser::type_start() const {
    const auto& token = current();
    return token.is("const") || token.is("volatile") ||
           token.is("$::meta::tokens") ||
           token.is("$::meta::syntax_match") ||
           token.is("$::meta::syntax") ||
           token.is("$::meta::span") ||
           token.is("$::meta::bytes") || token.is("$::meta::buffer") ||
           token.is("restrict") || token.is("enum") ||
           token.is("struct") || token.is("union") ||
           builtin_kind(token.text).has_value() ||
           std::find(active_generic_types_.begin(), active_generic_types_.end(),
                     token.text) != active_generic_types_.end() ||
           resolve_type_alias(peek_qualified_name()) != nullptr;
}

TypePtr Parser::parse_type(bool record_specifiers) {
    ProductionScope specifiers(*this, record_specifiers
        ? SyntaxProduction::DeclarationSpecifiers : SyntaxProduction::None);
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    std::optional<SourceLocation> restrict_location;
    while (current().is("const") || current().is("volatile") || current().is("restrict")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
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
        if (current().is("$::meta::span")) {
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
            const bool is_union = consume("union");
            if (!is_union)
                consume("struct");
            const auto name = parse_qualified_name();
            if (!name) {
                error_here("expected record name after '" +
                           std::string(is_union ? "union" : "struct") + "'");
                return {};
            }
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
        } else if (current().is("enum")) {
            ProductionScope enumeration(*this, SyntaxProduction::EnumSpecifier);
            consume("enum");
            const auto name = parse_qualified_name();
            if (!name) {
                error_here("expected enumeration name after 'enum'");
                return {};
            }
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
                diagnostics_.error(current().location, "unknown enumeration type '" + *name + "'");
                type = enum_type(*name, BuiltinType::I32, is_const, is_volatile);
            } else {
                type = enum_type(canonical, found->second, is_const, is_volatile);
            }
        }
        const auto kind = type ? std::optional<BuiltinType>{} : builtin_kind(current().text);
        const auto generic =
            std::find(active_generic_types_.begin(), active_generic_types_.end(), current().text);
        const auto alias_name = type ? std::string{} : peek_qualified_name();
        const auto alias = alias_name.empty() ? TypePtr{} : resolve_type_alias(alias_name);
        if (!type && !kind && generic == active_generic_types_.end() && !alias) {
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
                const auto spelling = std::string(current().text);
                if (kind)
                    ++index_;
                else
                    (void)parse_qualified_name();
                type = kind ? builtin_type(*kind, is_const, is_volatile)
                            : generic_type(spelling, is_const, is_volatile);
            }
        }
    }
    std::optional<std::pair<std::uint32_t, SourceLocation>> pending_address_space;
    while (current().is("const") || current().is("volatile") || current().is("restrict") ||
           current().is("[[")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if (current().is("[[")) {
            // Each written attribute_specifier owns its own occurrence.
            apply_type_attributes(type, &pending_address_space);
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

std::vector<std::string> Parser::preview_angle_generic_types() const {
    for (auto cursor = index_; cursor < tokens_.size(); ++cursor) {
        if (tokens_[cursor].is(";") || tokens_[cursor].is("{") ||
            tokens_[cursor].is("=")) break;
        if (!tokens_[cursor].is("<") || cursor == 0 ||
            tokens_[cursor - 1].kind != TokenKind::Identifier) continue;
        unsigned depth = 1;
        auto close = cursor + 1;
        for (; close < tokens_.size(); ++close) {
            if (tokens_[close].is("<")) ++depth;
            else if (tokens_[close].is(">")) --depth;
            else if (tokens_[close].is(">>")) {
                if (depth < 2) break;
                depth -= 2;
            }
            if (depth == 0) break;
        }
        if (depth != 0 || close + 1 >= tokens_.size() ||
            !tokens_[close + 1].is("(")) continue;
        std::vector<std::string> result;
        auto begin = cursor + 1;
        for (auto index = begin; index <= close; ++index) {
            if (index != close && !tokens_[index].is(",")) continue;
            if (index == begin + 1 &&
                tokens_[begin].kind == TokenKind::Identifier)
                result.emplace_back(tokens_[begin].text);
            begin = index + 1;
        }
        return result;
    }
    return {};
}

std::vector<FunctionDecl::GenericParameter>
Parser::parse_angle_generic_parameters() {
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
            name = std::string(current().text);
            ++index_;
        } else {
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                value_type = parse_type();
                if (value_type)
                    value_type = parse_declarator(std::move(value_type), name, false,
                                                  nullptr, nullptr, nullptr, true);
            }
            if (const auto* token = consume_kind(TokenKind::Identifier))
                name = std::string(token->text);
            if (value_type && !is_integer(value_type) &&
                value_type->kind != Type::Kind::Pointer &&
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

bool Parser::known_generic_name(const Expr& name) const {
    if (name.text.find("::") == std::string::npos)
        for (auto scope = local_scopes_.rbegin();
             scope != local_scopes_.rend(); ++scope)
            if (scope->contains(name_key(name))) return false;
    const auto lookup = [&](std::string_view candidate)
        -> std::optional<bool> {
        if (known_ordinary_values_.contains(std::string(candidate)))
            return false;
        if (known_generic_functions_.contains(std::string(candidate)))
            return true;
        return std::nullopt;
    };
    for (const auto& candidate : namespace_candidates(
             NameUse{name}, active_namespace_, active_imports_))
        if (const auto found = lookup(candidate))
            return *found;
    return false;
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
    if (recording_public_tree_)
        public_input_indices_.insert(public_input_indices_.begin() +
            static_cast<std::ptrdiff_t>(index_) + 1, public_input_indices_[index_]);
    ++index_;
    return true;
}

TypePtr Parser::parse_declarator(TypePtr base, std::optional<std::string>& name, bool parameter,
                                 std::unique_ptr<Expr>* dynamic_outer_bound,
                                 SourceLocation* name_location,
                                 std::vector<FunctionDecl::GenericParameter>* angle_parameters,
                                 bool abstract_only) {
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
        while (current().is("const") || current().is("volatile") || current().is("restrict") ||
               current().is("[[")) {
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
    const bool grouped = current().is("(") && (current(1).is("*") || current(1).is("("));
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
        nested = parse_declarator(hole, name, parameter, nullptr, name_location,
                                   angle_parameters, abstract_only);
        expect(")", "after parenthesized declarator");
    } else if (!abstract_only) {
        const auto location = current().location;
        name = parse_qualified_name();
        if (name && name_location)
            *name_location = location;
        if (name && angle_parameters && current().is("<"))
            *angle_parameters = parse_angle_generic_parameters();
    }
    if (current().is("[")) {
        base = parse_array_suffix(std::move(base), parameter, dynamic_outer_bound);
    }
    if (current().is("(")) {
        ProductionScope suffix(*this, SyntaxProduction::FunctionSuffix);
        consume("(");
        std::vector<ParameterDecl> parameters;
        bool variadic = false;
        {
            ProductionScope list(*this, SyntaxProduction::ParameterList);
            if (!current().is(")")) {
                if (current().is("void") && current(1).is(")")) {
                    ++index_;
                } else {
                    unsigned ordinal = 0;
                    for (;;) {
                        if (consume("...")) {
                            variadic = true;
                            break;
                        }
                        const auto before = index_;
                        parameters.push_back(parse_parameter(ordinal++));
                        if (index_ == before || !consume(","))
                            break;
                    }
                }
            }
        }
        expect(")", "after function parameters");
        base = function_type(std::move(base), std::move(parameters), variadic);
        if (current().is("->")) {
            ProductionScope result_location(*this, SyntaxProduction::ResultLocation);
            consume("->");
            const auto* location_token = consume_kind(TokenKind::String);
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
        if (nested || !name) {
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
    program.static_assertions = std::move(static_assertions_);
    return program;
}

void Parser::parse_typedef(const std::string& name_space,
                           std::vector<Attribute> attributes) {
    const auto location = current().location;
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        consume("typedef");
    }
    if (!type_start()) {
        error_here("expected aliased type after 'typedef'");
        synchronize_external();
        return;
    }
    auto type = parse_type(false);
    specifiers.finish();
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    ProductionScope item(*this, SyntaxProduction::InitDeclarator);
    std::optional<std::string> name;
    type = parse_declarator(std::move(type), name);
    if (!type || !name) {
        if (!name) error_here("expected typedef name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    item.finish();
    list.finish();
    expect(";", "after typedef declaration");
    apply_callable_attributes(type, attributes);

    const Attribute* vector_attribute = nullptr;
    for (const auto& attribute : attributes) {
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
                const auto element_bits = type_bits(type);
                std::uint64_t lanes = amount;
                if (vector_attribute->name == "vector_size") {
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

    const auto found = type_aliases_.find(*name);
    if (found != type_aliases_.end() && !same_type(found->second, type)) {
        diagnostics_.error(location,
                           "typedef '" + *name +
                               "' redeclared with a different type");
        return;
    }
    type_aliases_[*name] = std::move(type);
}

void Parser::parse_external(Program& program, const std::string& name_space) {
    ProductionScope production(*this, parsing_public_fragment_
        ? SyntaxProduction::Declaration : SyntaxProduction::None);
    if (parsing_public_fragment_ && (current().is("syntax") ||
        current().is("namespace") || current().is("using") ||
        current().is("$::static_assert") || macro_start() || active_syntax(true) ||
        (current().is("[[") && (current(1).is("macro") ||
         current(1).is("syntax_expander"))))) {
        error_here("parsed declaration requires a direct core declaration");
        ++index_;
        return;
    }
    struct NamespaceRestore {
        std::string& value;
        std::string previous;
        ~NamespaceRestore() { value = std::move(previous); }
    } restore{active_namespace_, active_namespace_};
    active_namespace_ = name_space;
    if (syntax_ && current().is("[[") &&
        (current(1).is("macro") || current(1).is("syntax_expander")) && current(2).is("]]")) {
        if (replacement_) error_here("expansion output cannot introduce syntax registration");
        else (void)syntax_->execution()->define_function(tokens_, index_, active_namespace_,
            active_imports_, syntax_->bindings());
        if (replacement_) { ++index_; synchronize_external(); }
        return;
    }
    if (parse_syntax_registration(&program)) return;
    if (syntax_ && (macro_start() || active_syntax(true))) {
        const auto location = current().location;
        auto execution = syntax_->execution();
        if (!execution->begin_replacement(location)) { ++index_; synchronize_external(); return; }
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
    if (current().is("enum") &&
        current(1).kind == TokenKind::Identifier &&
        (current(2).is("{") || current(2).is("[[") ||
         current(2).is(";"))) {
        parse_enum_declaration(program, name_space, std::move(attributes));
        return;
    }
    if ((current().is("struct") || current().is("union")) &&
        current(1).kind == TokenKind::Identifier &&
        (current(2).is("{") || current(2).is("[[") ||
         current(2).is(";"))) {
        parse_record_declaration(program, name_space, std::move(attributes));
        return;
    }
    if (current().is("typedef")) {
        parse_typedef(name_space, std::move(attributes));
        return;
    }

    Linkage linkage = Linkage::Group;
    bool linkage_seen = false;
    bool inline_hint = false;
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    while (current().is("global") || current().is("static") || current().is("inline")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if (consume("inline")) { inline_hint = true; continue; }
        Linkage selected;
        if (consume("global")) selected = Linkage::Global;
        else { consume("static"); selected = Linkage::Static; }
        if (linkage_seen && linkage != selected) error_here("declaration cannot be both 'global' and 'static'");
        linkage = selected;
        linkage_seen = true;
    }

    if (linkage == Linkage::Global && current().is("label") &&
        current(1).kind == TokenKind::Identifier && current(2).is("::")) {
        if (inline_hint) {
            diagnostics_.error(current().location,
                               "'inline' is valid only on a function");
        }
        parse_global_label_declaration(program, name_space,
                                       std::move(attributes));
        return;
    }

    const auto parameters = generic_parameters(attributes);
    const auto saved_generic_types = active_generic_types_;
    active_generic_types_.clear();
    for (const auto& parameter : parameters) {
        if (!parameter.value_type) active_generic_types_.push_back(parameter.name);
    }
    for (const auto& name : preview_angle_generic_types()) {
        if (std::find(active_generic_types_.begin(), active_generic_types_.end(),
                      name) == active_generic_types_.end())
            active_generic_types_.push_back(name);
    }
    if (!type_start()) {
        if (const auto message = familiar_c_spelling(current().text)) {
            error_here(*message);
        } else {
            error_here("expected declaration");
        }
        synchronize_external();
        active_generic_types_ = saved_generic_types;
        return;
    }
    const auto location = current().location;
    auto base_type = parse_type(false);
    specifiers.finish();
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    unsigned ordinal = 0;
    bool last_function = false;
    for (;;) {
        ProductionScope item(*this, SyntaxProduction::InitDeclarator);
        std::optional<std::string> name;
        std::vector<FunctionDecl::GenericParameter> angle_parameters;
        auto type = parse_declarator(copy_type(base_type), name, false, nullptr, nullptr,
                                     &angle_parameters);
        if (!type || !name) {
            if (!name) error_here("expected declaration name");
            synchronize_external();
            active_generic_types_ = saved_generic_types;
            return;
        }
        *name = join_namespace(name_space, *name);
        last_function = type->kind == Type::Kind::Function && type->function;
        if (last_function) {
            if (!angle_parameters.empty() || !parameters.empty())
                known_generic_functions_.insert(*name);
            else
                known_ordinary_values_.insert(*name);
            auto signature = type->function;
            auto result_type = signature->result;
            auto function = parse_function(
                location, std::move(*name), name_space, std::move(result_type),
                linkage, inline_hint, attributes, std::move(signature),
                std::move(angle_parameters));
            if (function && (parsing_public_function_header_ || current().is("{"))) {
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
                    auto* previous_function = active_function_;
                    active_function_ = function.get();
                    function->body = parse_compound();
                    active_function_ = previous_function;
                }
                program.functions.push_back(std::move(function));
                active_generic_types_ = saved_generic_types;
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
    active_generic_types_ = saved_generic_types;
}

void Parser::parse_global_label_declaration(
    Program& program, const std::string& name_space,
    std::vector<Attribute> attributes) {
    const auto location = current().location;
    consume("label");
    auto name = parse_qualified_name();
    if (!name || name->find("::") == std::string::npos) {
        diagnostics_.error(location,
                           "a global label declaration requires a qualified label name");
        synchronize_external();
        return;
    }
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    expect(";", "after global label declaration");
    program.global_labels.push_back(
        {location, join_namespace(name_space, *name), std::move(attributes)});
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

    EnumDecl declaration;
    declaration.location = location;
    declaration.name = *name;
    declaration.underlying = underlying;
    declaration.attributes = std::move(attributes);
    if (consume("{")) {
        while (!current().is("}") && current().kind != TokenKind::End) {
            ProductionScope entry(*this, SyntaxProduction::Enumerator);
            const auto* token = consume_kind(TokenKind::Identifier);
            if (!token) {
                error_here("expected enumerator name");
                while (!current().is(",") && !current().is("}") &&
                       current().kind != TokenKind::End) {
                    ++index_;
                }
            } else {
                EnumDecl::Enumerator enumerator;
                enumerator.location = token->location;
                enumerator.name = join_namespace(name_space, token->text);
                known_ordinary_values_.insert(enumerator.name);
                if (consume("=")) enumerator.initializer = parse_constant_expression();
                declaration.enumerators.push_back(std::move(enumerator));
            }
            entry.finish();
            if (!consume(",")) break;
        }
        expect("}", "after enumeration definition");
    }
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
            auto base_type = parse_type();
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
    record.finish();
    type_specifier.finish();
    specifier.finish();
    specifiers.finish();
    expect(";", "after record declaration");
    program.records.push_back(std::move(declaration));
}

bool Parser::parse_static_assertion() {
    ProductionScope production(*this, SyntaxProduction::StaticAssertDeclaration);
    const auto location = current().location;
    consume("$::static_assert");
    expect("(");
    auto condition = parse_constant_expression();
    expect(",");
    const auto* message_token = consume_kind(TokenKind::String);
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

ParameterDecl Parser::parse_parameter(unsigned ordinal) {
    ProductionScope production(*this, SyntaxProduction::ParameterDeclaration);
    ParameterDecl parameter;
    parameter.location = current().location;
    auto attributes = parse_attributes();
    if (current().is("in") || current().is("out") || current().is("inout")) {
        ProductionScope mode(*this, SyntaxProduction::ParameterMode);
        if (consume("in")) parameter.mode = ParameterMode::In;
        else if (consume("out")) parameter.mode = ParameterMode::Out;
        else { consume("inout"); parameter.mode = ParameterMode::InOut; }
        parameter.explicit_mode = true;
    }
    parameter.type = parse_type();
    std::optional<std::string> name;
    parameter.type = parse_declarator(std::move(parameter.type), name, true,
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
        const auto* location = consume_kind(TokenKind::String);
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
    function->generic_parameters = generic_parameters(function->attributes);
    if (!angle_parameters.empty()) {
        if (!function->generic_parameters.empty())
            diagnostics_.error(location,
                               "angle generic parameters cannot be combined with [[generic]]");
        else
            function->generic_parameters = std::move(angle_parameters);
    }
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
        if (!consume(")")) {
            if (current().is("void") && current(1).is(")")) {
                ++index_;
            } else {
                unsigned ordinal = 0;
                for (;;) {
                    if (consume("...")) {
                        function->variadic = true;
                        break;
                    }
                    function->parameters.push_back(parse_parameter(ordinal++));
                    if (!consume(",")) break;
                }
            }
            expect(")");
        }
    }
    if (consume("->")) {
        const auto* location_token = consume_kind(TokenKind::String);
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

bool Parser::local_declaration_start() const {
    return current().is("register") || current().is("stack") ||
           current().is("static") || type_start();
}

std::unique_ptr<Statement>
Parser::parse_local_declaration(std::vector<Attribute> attributes,
                                bool consume_semicolon,
                                SyntaxProduction production_name) {
    ProductionScope production(*this, production_name);
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Declaration;
    statement->location = current().location;
    statement->declaration = std::make_unique<VariableDecl>();
    auto& declaration = *statement->declaration;
    declaration.location = current().location;
    ProductionScope specifiers(*this, SyntaxProduction::DeclarationSpecifiers);
    if (current().is("register") || current().is("stack") || current().is("static")) {
        ProductionScope specifier(*this, SyntaxProduction::DeclarationSpecifier);
        if (consume("register")) declaration.storage_register = true;
        else if (consume("stack")) declaration.storage_stack = true;
        else if (consume("static")) declaration.storage_static = true;
    }
    declaration.type = parse_type(false);
    specifiers.finish();
    ProductionScope list(*this, SyntaxProduction::InitDeclaratorList);
    ProductionScope item(*this, SyntaxProduction::InitDeclarator);
    std::optional<std::string> name;
    declaration.type =
        parse_declarator(std::move(declaration.type), name, false,
                         &declaration.dynamic_array_bound, &declaration.location);
    if (!name) error_here("expected local variable name");
    else {
        declaration.name = *name;
        if (!local_scopes_.empty()) local_scopes_.back().insert(name_key(declaration));
    }
    if (current().kind == TokenKind::String) {
        ProductionScope location_production(*this, SyntaxProduction::ObjectLocation);
        const auto* location = consume_kind(TokenKind::String);
        declaration.location_name = decode_string_literal(location->text);
    }
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    declaration.attributes = std::move(attributes);
    for (const auto& attribute : declaration.attributes) {
        if (attribute.name != "aligned") {
            diagnostics_.error(
                attribute.location,
                "attribute '" + attribute.name +
                    "' is not valid on a local object");
            continue;
        }
        // Target-dependent constants are resolved after HIR has completed
        // nominal layouts and before this local is lowered to MIR.
    }
    if (consume("=")) declaration.initializer = parse_initializer();
    if (declaration.type && declaration.type->kind == Type::Kind::Array &&
        declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
        declaration.initializer &&
        declaration.initializer->kind == Expr::Kind::String &&
        declaration.type->element &&
        declaration.type->element->kind == Type::Kind::Builtin &&
        declaration.type->element->builtin == BuiltinType::U8) {
        declaration.type->lanes = static_cast<std::uint32_t>(
            declaration.initializer->string_value.size() + 1);
    }
    if (declaration.type && declaration.type->kind == Type::Kind::Array &&
        declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
        (!declaration.initializer ||
         declaration.initializer->kind !=
             Expr::Kind::AggregateInitializer)) {
        diagnostics_.error(declaration.location,
                           "an omitted array bound requires a u8 string initializer");
    }
    item.finish();
    list.finish();
    if (consume_semicolon) expect(";");
    return statement;
}

std::unique_ptr<Statement> Parser::parse_compound() {
    ProductionScope production(*this, SyntaxProduction::CompoundStatement);
    const auto saved_imports = active_imports_;
    const auto saved_scope_imports = current_scope_imports_;
    current_scope_imports_ = 0;
    local_scopes_.emplace_back();
    if (syntax_) syntax_->push_scope();
    if (local_scopes_.size() == 1 && active_function_)
        for (const auto& parameter : active_function_->parameters)
            local_scopes_.back().insert(name_key(parameter));
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Compound;
    statement->location = current().location;
    expect("{");
    while (!current().is("}") && current().kind != TokenKind::End) {
        const auto before = index_;
        if (current().is("syntax") && (syntax_ || token_origin(current().location).context)) {
            auto declaration = std::make_unique<Statement>();
            declaration->kind = Statement::Kind::Empty;
            declaration->location = current().location;
            (void)parse_syntax_registration(nullptr);
            statement->statements.push_back(std::move(declaration));
        }
        else if (current().is("using")) {
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
        if (index_ == before && current().kind != TokenKind::End) ++index_;
    }
    expect("}");
    active_imports_ = saved_imports;
    current_scope_imports_ = saved_scope_imports;
    local_scopes_.pop_back();
    if (syntax_) syntax_->pop_scope();
    return statement;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    ProductionScope production(*this, SyntaxProduction::Statement);
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
        const auto suffix = current().text;
        const bool expression_continues = precedence(suffix) > 0 || suffix == "=" ||
            suffix == "+=" || suffix == "-=" || suffix == "*=" || suffix == "/=" ||
            suffix == "%=" || suffix == "<<=" || suffix == ">>=" || suffix == "&=" ||
            suffix == "^=" || suffix == "|=" || suffix == "?" || suffix == "(" ||
            suffix == "[" || suffix == "." || suffix == "->" || suffix == "++" ||
            suffix == "--" || (suffix == "::" && current(1).is("<"));
        if (expression_continues) {
            index_ = first;
            production_events_.resize(events);
            if (!production_stack_.empty())
                production_events_[production_stack_.back()].children.pop_back();
            ProductionScope expression_statement(*this, SyntaxProduction::ExpressionStatement);
            placeholder->kind = Statement::Kind::Expression;
            placeholder->expression = parse_expression();
            expect(";", "after expression statement");
        }
        return placeholder;
    }
    if (syntax_ && macro_start()) return parse_statement_replacement();
    if (const auto* definition = active_syntax(false)) {
        if (definition->kind == SyntaxKind::Statement) {
            if (!parsing_public_fragment_) return parse_statement_replacement();
            auto placeholder = std::make_unique<Statement>();
            placeholder->kind = Statement::Kind::Empty;
            placeholder->location = current().location;
            if (!parse_opaque_invocation(SyntaxKind::Statement))
                error_here("could not recognize a bounded opaque statement invocation");
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
        if (consume("label")) statement->label_name = std::string(current().text);
        else statement->label_name = std::string(current().text);
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
        local_scopes_.emplace_back();
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
    const auto* name = consume_kind(TokenKind::Identifier);
    if (!name) {
        error_here("expected label name after 'global label'");
    } else {
        statement->label_name = std::string(name->text);
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
                    const auto* member = consume_kind(TokenKind::Identifier);
                    if (!member) error_here("expected member name after '.' in initializer");
                    else designator.member = std::string(member->text);
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
        const bool begins_type = type_start();
        index_ = saved;
        if (begins_type) {
            const auto location = current().location;
            consume("(");
            TypePtr type;
            std::optional<std::string> name;
            {
                ProductionScope type_name(*this, SyntaxProduction::TypeName);
                type = parse_type();
                type = parse_declarator(std::move(type), name);
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
            const bool begins_type = type_start();
            index_ = saved;
            if (begins_type) {
                consume("(");
                std::optional<std::string> name;
                {
                    ProductionScope type_name(*this, SyntaxProduction::TypeName);
                    result->type = parse_type();
                    result->type = parse_declarator(std::move(result->type), name);
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
                        if (type_start()) {
                            ProductionScope type_name(*this, SyntaxProduction::TypeName);
                            argument.type = parse_type();
                            std::optional<std::string> declared;
                            argument.type = parse_declarator(std::move(argument.type), declared);
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
            const auto* member_name = consume_kind(TokenKind::Identifier);
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
            member->right->text = std::string(member_name->text);
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

std::unique_ptr<Statement> Parser::parse_procedural_body() {
    parsing_procedural_body_ = true;
    auto body = parse_compound();
    if (current().kind != TokenKind::End)
        error_here("unexpected tokens after procedural macro body");
    parsing_procedural_body_ = false;
    return body;
}

std::unique_ptr<Expr> Parser::parse_quote() {
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
        const auto* path = consume_kind(TokenKind::String);
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
        if (type_start()) {
            ProductionScope type_name(*this, SyntaxProduction::TypeName);
            result->type = parse_type();
            std::optional<std::string> name;
            result->type = parse_declarator(std::move(result->type), name);
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
