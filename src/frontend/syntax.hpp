// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/semantic.hpp"

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

using SyntaxEntityId = SyntaxContext::EntityId;
using SyntaxBinding = SyntaxContext::Binding;
struct SyntaxMatchValue;

struct SyntaxSpan {
    SourceLocation first;
    SourceLocation last;
};

// Logical evaluator accounting, independent of host/target runtime layout.
inline constexpr std::uint64_t syntax_span_storage_bytes = 64;
inline constexpr std::uint64_t syntax_match_storage_bytes = 192;
inline constexpr std::uint64_t syntax_field_storage_bytes = 128;

enum class SyntaxProduction {
    None, Declaration, FunctionHeader, FunctionDefinition, TypeName,
    DeclarationSpecifiers, DeclarationSpecifier, TypeQualifier, TypeSpecifier,
    ScalarType, StructOrUnionSpecifier, EnumSpecifier, TypedefName, TargetScalarBuiltinName,
    Declarator, AbstractDeclarator, DirectDeclarator, PointerPart,
    FunctionSuffix, ParameterList, ParameterDeclaration, ParameterMode,
    Location, ObjectLocation, ResultLocation, GenericParameterList, GenericParameter,
    InitDeclaratorList, InitDeclarator, MemberDeclaration, MemberDeclarator, Enumerator,
    AttributeSpecifier, Attribute, AttributeName, BalancedTokenSequence,
    BalancedTokenTree, BalancedTokens, QualifiedName, NamespaceName,
    Statement, UnattributedStatement, CompoundStatement, UsingDeclaration,
    LabeledStatement, SelectionStatement, IterationStatement, JumpStatement,
    ExpressionStatement, StaticAssertDeclaration, ForInitializer,
    DeclarationWithoutFinalSemicolon, ArraySuffix,
    Initializer, InitializerEntry, Designator,
    Expression, ConstantExpression, AssignmentExpression, AssignmentOperator, ConditionalExpression,
    LogicalOrExpression, LogicalAndExpression, InclusiveOrExpression,
    ExclusiveOrExpression, AndExpression, EqualityExpression,
    RelationalExpression, ShiftExpression, AdditiveExpression,
    MultiplicativeExpression, CastExpression, UnaryExpression,
    PostfixExpression, PrimaryExpression, ArgumentList, GenericArguments, GenericArgument,
    BuiltinName, Literal, EmbedExpression, QuoteExpression, Count,
};
std::string_view syntax_production_name(SyntaxProduction production);

// The public, versioned tree is independent of typed AST/HIR. Every node
// owns its lexical identity and remains immutable after construction.
struct SyntaxNode {
    enum class Kind { Token, Group, Core, Extension, Macro, Deferred } kind{Kind::Token};
    SyntaxProduction production{SyntaxProduction::None};
    // Expected grammar slot for an opaque extension/macro/deferred node.
    // It is not exposed as a core production by the public query API.
    SyntaxProduction slot_production{SyntaxProduction::None};
    TokenSequence tokens;
    std::vector<std::shared_ptr<const SyntaxNode>> children;
    SyntaxSpan span;
    std::shared_ptr<const SyntaxContext> context;
    std::optional<SyntaxEntityId> definition;
    std::shared_ptr<const SyntaxMatchValue> match;
};

TokenSequence syntax_node_tokens(const SyntaxNode& node);
std::size_t syntax_node_count(const SyntaxNode& node);
std::uint64_t syntax_node_storage(const SyntaxNode& node,
    std::uint64_t stop_after = std::numeric_limits<std::uint64_t>::max());
enum class SyntaxTreeValidationError { None, Shape, DepthLimit, WorkLimit };
bool syntax_validate_node(const SyntaxNode& node, std::string& error,
    EvaluationLimits limits = {}, std::uint64_t* work = nullptr,
    SyntaxTreeValidationError* failure = nullptr);
std::shared_ptr<const SyntaxNode> syntax_replace_child(
    const SyntaxNode& parent, std::size_t index,
    std::shared_ptr<const SyntaxNode> replacement, std::string& error,
    EvaluationLimits limits = {}, std::uint64_t* work = nullptr);

// Public match records are translation-only, not runtime records or typed HIR.
struct SyntaxMatchValue {
    struct Field {
        enum class Kind { Primitive, RawGroup, Parsed, Nested };
        std::string name;
        TokenSequence tokens;
        std::shared_ptr<const SyntaxNode> node;
        std::vector<std::shared_ptr<const SyntaxMatchValue>> records;
        Kind kind{Kind::Primitive};
        SyntaxSpan span;
    };
    TokenSequence input;
    std::vector<Field> fields;
    std::optional<std::string> variant;
    std::vector<std::string> variant_labels;
    SyntaxSpan span;
};

struct SyntaxFunctionId { std::uint32_t value{}; };
enum class SyntaxKind { Item, Statement, Expression, Rule, Bundle };
struct SyntaxActivation {
    std::string name;
    std::optional<std::string> alias;
    SourceLocation location;
};
struct SyntaxParsedFragment {
    std::size_t end{};
    std::shared_ptr<const SyntaxNode> node;
};
struct SyntaxPatternElement {
    enum class Kind { Terminal, Ident, Name, Literal, Paren, Bracket, Block, Group,
                      TokensUntil, FunctionRaw, Expr, Statement, Type, Declaration,
                      FunctionHeader, FunctionDeclaration, FunctionDefinition,
                      Rule, Optional, Repeat0, Repeat1, Separated0, Separated1,
                      Choice } kind;
    struct Alternative {
        std::string label;
        std::vector<SyntaxPatternElement> pattern;
    };
    std::string field;
    MetaToken terminal;
    std::string rule_name;
    std::vector<SyntaxPatternElement> pattern;
    std::vector<Alternative> alternatives;
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
    bool begin_fragment(SourceLocation location, std::size_t copied_tokens);
    void end_fragment();
    void tree_limit_error(SourceLocation location);
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
    unsigned fragment_depth_{};
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
        const std::vector<Token>& tokens, std::size_t begin, Diagnostics& diagnostics,
        const std::function<bool(std::size_t, std::size_t)>& function_header = {},
        const std::function<std::optional<SyntaxParsedFragment>(
            SyntaxPatternElement::Kind, std::size_t)>& parse_fragment = {}) const;
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
