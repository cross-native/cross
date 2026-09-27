// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/syntax.hpp"

#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross {

class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics,
           std::shared_ptr<SyntaxExecution> execution = {});
    Program parse();
    // Parse a macro's braced body with the ordinary statement/expression
    // grammar, enabling translation-only token types and quotation.
    std::unique_ptr<Statement> parse_procedural_body();
    // Read-only, bounded recognition using the same core grammar. The returned
    // tree preserves source tokens; speculative state never escapes this call.
    std::optional<SyntaxParsedFragment> parse_syntax_fragment(
        SyntaxPatternElement::Kind kind, std::size_t first) const;

private:
    const Token& current(std::size_t lookahead = 0) const;
    bool consume(std::string_view spelling);
    const Token* consume_kind(TokenKind kind);
    bool expect(std::string_view spelling, std::string_view context = {});
    void error_here(std::string message);
    void synchronize_external();
    bool parse_syntax_registration(Program* program);
    std::vector<SyntaxActivation> parse_syntax_entries(std::string_view end);
    const SyntaxDefinition* active_syntax(bool item) const;
    bool macro_start() const;
    std::optional<SyntaxExecution::Output> expand_at_position(bool item);
    bool validate_syntax_function_header(std::size_t first, std::size_t body_open,
                                         std::string_view name_space,
                                         const std::vector<std::string>& imports) const;
    std::shared_ptr<const SyntaxNode> parse_opaque_invocation(SyntaxKind category);
    std::unique_ptr<Parser> replacement_parser(SyntaxExecution::Output output,
                                                Diagnostics* diagnostics = nullptr) const;
    void adopt_replacement(Parser& child);
    std::unique_ptr<Statement> parse_statement_replacement();
    std::unique_ptr<Expr> parse_expression_replacement();

    std::vector<Attribute> parse_attributes(bool one_specifier = false);
    void apply_type_attributes(
        TypePtr& type,
        std::optional<std::pair<std::uint32_t, SourceLocation>>*
            pending_address_space = nullptr);
    std::optional<std::string> parse_qualified_name(
        SyntaxProduction production = SyntaxProduction::QualifiedName);
    std::string peek_qualified_name() const;
    TypePtr resolve_type_alias(std::string_view name) const;
    TypePtr parse_type(bool record_specifiers = true);
    TypePtr
    parse_declarator(TypePtr base, std::optional<std::string>& name,
                     bool parameter = false,
                     std::unique_ptr<Expr>* dynamic_outer_bound = nullptr,
                     SourceLocation* name_location = nullptr,
                     std::vector<FunctionDecl::GenericParameter>*
                         angle_parameters = nullptr, bool abstract_only = false);
    std::vector<FunctionDecl::GenericParameter>
    parse_angle_generic_parameters();
    std::vector<std::string> preview_angle_generic_types() const;
    bool consume_generic_close();
    bool known_generic_name(const Expr& name) const;
    void apply_callable_attributes(TypePtr& type,
                                   const std::vector<Attribute>& attributes);
    TypePtr parse_array_suffix(
        TypePtr element, bool parameter = false,
        std::unique_ptr<Expr>* dynamic_outer_bound = nullptr);
    bool type_start() const;
    void parse_external(Program& program, const std::string& name_space);
    void parse_typedef(const std::string& name_space,
                       std::vector<Attribute> attributes, bool consume_semicolon = true);
    void register_typedef(SourceLocation location, std::string name, TypePtr type,
                          const std::vector<Attribute>& attributes);
    void parse_enum_declaration(Program& program, const std::string& name_space,
                                std::vector<Attribute> attributes);
    void parse_record_declaration(Program& program,
                                  const std::string& name_space,
                                  std::vector<Attribute> attributes);
    void parse_global_label_declaration(
        Program& program, const std::string& name_space,
        std::vector<Attribute> attributes);
    bool parse_static_assertion();
    std::unique_ptr<FunctionDecl>
    parse_function(SourceLocation location, std::string name,
                   std::string name_space, TypePtr return_type, Linkage linkage,
                   bool inline_hint, std::vector<Attribute> attributes,
                   std::shared_ptr<FunctionType> signature = {},
                   std::vector<FunctionDecl::GenericParameter>
                       angle_parameters = {});
    std::unique_ptr<ObjectDecl> parse_object(
        SourceLocation location, std::string name, TypePtr type, Linkage linkage,
        std::vector<Attribute> attributes, bool consume_semicolon = true);
    ParameterDecl parse_parameter(unsigned ordinal);

    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Statement> parse_unattributed_statement(std::vector<Attribute> attributes);
    std::unique_ptr<Statement>
    parse_global_label_statement(std::vector<Attribute> attributes = {});
    std::unique_ptr<Statement> parse_compound();
    std::unique_ptr<Statement>
    parse_local_declaration(std::vector<Attribute> attributes = {},
                            bool consume_semicolon = true,
                            SyntaxProduction production = SyntaxProduction::Declaration);
    bool local_declaration_start() const;

    std::unique_ptr<Expr> parse_expression(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_constant_expression();
    std::unique_ptr<Expr> parse_initializer();
    std::unique_ptr<Expr> parse_assignment(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_conditional(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_binary(int minimum_precedence, std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_cast();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_quote();
    std::vector<FunctionDecl::GenericParameter> generic_parameters(
        const std::vector<Attribute>& attributes);
    static int precedence(std::string_view operation);

    struct ProductionEvent {
        SyntaxProduction production;
        std::size_t first{};
        std::size_t end{};
        std::vector<std::size_t> children;
        std::shared_ptr<const SyntaxNode> opaque;
    };
    struct ProductionScope {
        Parser& parser;
        std::size_t event;
        ProductionScope(Parser& parser, SyntaxProduction production);
        ~ProductionScope();
        void finish();
    };
    std::size_t begin_production(SyntaxProduction production);
    void end_production(std::size_t event);
    void flatten_production(std::size_t event,
        std::size_t parent = std::numeric_limits<std::size_t>::max());
    std::shared_ptr<const SyntaxNode> public_node(std::size_t event) const;
    void record_balanced_sequence(std::size_t first, std::size_t end,
        SyntaxProduction production = SyntaxProduction::BalancedTokenSequence);

    std::vector<Token> tokens_;
    Diagnostics& diagnostics_;
    std::size_t index_{};
    std::optional<SyntaxState> syntax_;
    bool replacement_{};
    bool deferred_statement_semicolon_{};
    std::vector<std::string> active_imports_;
    std::size_t current_scope_imports_{};
    std::vector<std::string> active_generic_types_;
    std::unordered_set<std::string> known_generic_functions_;
    std::unordered_set<std::string> known_ordinary_values_;
    std::vector<NameSet> local_scopes_;
    std::vector<NameMap<TypePtr>> local_type_scopes_;
    std::string active_namespace_;
    std::unordered_map<std::string, BuiltinType> enum_types_;
    struct RecordTag {
        bool is_union{};
        bool complete{};
    };
    std::unordered_map<std::string, RecordTag> record_types_;
    std::unordered_map<std::string, TypePtr> type_aliases_;
    std::vector<StaticAssertDecl> static_assertions_;
    FunctionDecl* active_function_{};
    bool parsing_generic_argument_{};
    bool parsing_procedural_body_{};
    bool parsing_public_fragment_{};
    bool parsing_public_function_header_{};
    bool recording_public_tree_{};
    bool public_tree_failed_{};
    std::vector<ProductionEvent> production_events_;
    std::vector<std::size_t> production_stack_;
    // Public-fragment token splitting must not move the owner's match boundary.
    std::vector<std::size_t> public_input_indices_;
    unsigned switch_depth_{};
    std::vector<bool> switch_default_seen_;
};

} // namespace cross
