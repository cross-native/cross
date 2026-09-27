// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/semantic.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cross {

// Public match records are translation-only, not runtime records or typed HIR.
struct SyntaxMatchValue {
    struct Field {
        std::string name;
        TokenSequence tokens;
        std::vector<std::shared_ptr<const SyntaxMatchValue>> records;
    };
    TokenSequence input;
    std::vector<Field> fields;
};

using SyntaxEntityId = SyntaxContext::EntityId;
using SyntaxBinding = SyntaxContext::Binding;
struct SyntaxFunctionId { std::uint32_t value{}; };
enum class SyntaxKind { Item, Statement, Expression, Rule, Bundle };
struct SyntaxActivation {
    std::string name;
    std::optional<std::string> alias;
    SourceLocation location;
};
struct SyntaxPatternElement {
    enum class Kind { Terminal, Ident, Name, Literal, Paren, Bracket, Block, Group,
                      TokensUntil, Rule } kind;
    std::string field;
    MetaToken terminal;
    std::string rule_name;
    SourceLocation location;
    std::optional<SyntaxEntityId> resolved_rule{};
};
struct SyntaxDefinition {
    SyntaxEntityId id;
    SyntaxKind kind;
    std::string name;
    std::string prefix;
    std::string expander;
    std::vector<SyntaxPatternElement> pattern;
    std::vector<SyntaxActivation> uses;
    SourceLocation location;
    std::string name_space;
    std::vector<std::vector<std::string>> imports;
    bool rules_bound{};
    std::optional<SyntaxFunctionId> bound_expander{};
};

// Owns expansion functions and target evaluation facts for one primary input.
// Parsing replacements is deliberately left to Parser at bounded positions.
class SyntaxExecution {
public:
    using FunctionId = SyntaxFunctionId;
    SyntaxExecution(SourceManager& sources, Diagnostics& diagnostics,
                    unsigned address_bits, LayoutQuery size_of, LayoutQuery align_of,
                    EvaluationLimits limits, EvaluationLayout layout);
    std::vector<Token> prepare(const SourceFile& source);
    bool define_function(const std::vector<Token>& tokens, std::size_t& index, std::string_view name_space,
                          const std::vector<std::string>& imports,
                          const std::vector<SyntaxBinding>& bindings);
    std::optional<FunctionId> find_function(std::string_view name, std::string_view name_space,
                                          const std::vector<std::vector<std::string>>& imports,
                                          bool syntax_expander) const;
    struct Output {
        std::vector<Token> tokens;
        SourceLocation location;
    };
    std::optional<Output> expand(FunctionId function, TokenSequence input,
        std::shared_ptr<const SyntaxMatchValue> match, SourceLocation invocation,
        std::string_view name_space, const std::vector<std::string>& imports,
        const std::vector<SyntaxBinding>& bindings);
    bool begin_replacement(SourceLocation location);
    void end_replacement();
    bool work(SourceLocation location, std::uint64_t amount = 1);
    std::shared_ptr<const SyntaxContext> call_context(SourceLocation location,
        std::string_view name_space, const std::vector<std::string>& imports,
        const std::vector<SyntaxBinding>& bindings) const;
    const EvaluationLimits& limits() const { return limits_; }
    std::optional<MetaToken> terminal(std::string_view quoted, SourceLocation location);
private:
    struct Function {
        FunctionDecl declaration;
        bool syntax_expander{};
        std::vector<SyntaxBinding> bindings;
    };
    SourceManager& sources_;
    Diagnostics& diagnostics_;
    unsigned address_bits_;
    LayoutQuery size_of_, align_of_;
    EvaluationLimits limits_;
    EvaluationLayout layout_;
    std::vector<Function> functions_;
    std::uint64_t work_{};
    unsigned depth_{};
    unsigned expansions_{};
};

// Copyable lexical state; the stable declaration registry is shared by
// bounded replacement parsers, which cannot add registrations.
class SyntaxState {
public:
    explicit SyntaxState(std::shared_ptr<SyntaxExecution> execution);
    void push_scope();
    void pop_scope();
    void import(std::string name);
    std::vector<SyntaxBinding> bindings() const;
    const std::vector<std::vector<std::string>>& imports() const { return imports_; }
    bool declare(const std::vector<Token>& tokens, std::size_t& index,
                 std::string_view name_space, Diagnostics& diagnostics);
    bool activate(std::span<const SyntaxActivation> entries, std::string_view name_space,
                  Diagnostics& diagnostics);
    const SyntaxDefinition* selected(const Token& token, bool item) const;
    struct Match {
        std::shared_ptr<const SyntaxMatchValue> value;
        std::size_t end{};
        SyntaxExecution::FunctionId expander;
    };
    std::optional<Match> match(const SyntaxDefinition& definition,
        const std::vector<Token>& tokens, std::size_t begin, Diagnostics& diagnostics) const;
    std::shared_ptr<SyntaxExecution> execution() const { return execution_; }
private:
    std::optional<SyntaxEntityId> lookup(std::string_view name, std::string_view name_space,
        const std::vector<std::vector<std::string>>& imports, SourceLocation location,
        Diagnostics& diagnostics) const;
    std::shared_ptr<SyntaxExecution> execution_;
    std::shared_ptr<std::vector<SyntaxDefinition>> definitions_;
    std::vector<std::vector<std::string>> imports_{{}};
    std::vector<std::vector<SyntaxBinding>> scopes_{{}};
};

} // namespace cross
