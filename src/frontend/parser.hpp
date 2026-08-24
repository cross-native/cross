// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cross {

class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics);
    Program parse();

private:
    const Token& current(std::size_t lookahead = 0) const;
    bool consume(std::string_view spelling);
    const Token* consume_kind(TokenKind kind);
    bool expect(std::string_view spelling, std::string_view context = {});
    void error_here(std::string message);
    void synchronize_external();

    std::vector<Attribute> parse_attributes();
    std::optional<std::string> parse_qualified_name();
    std::string peek_qualified_name() const;
    TypePtr resolve_type_alias(std::string_view name) const;
    TypePtr parse_type();
    TypePtr parse_array_suffix(
        TypePtr element, bool parameter = false,
        std::unique_ptr<Expr>* dynamic_outer_bound = nullptr);
    bool type_start() const;
    void parse_external(Program& program, const std::string& name_space);
    void parse_typedef(const std::string& name_space,
                       std::vector<Attribute> attributes);
    void parse_enum_declaration(Program& program, const std::string& name_space,
                                std::vector<Attribute> attributes);
    void parse_record_declaration(Program& program,
                                  const std::string& name_space,
                                  std::vector<Attribute> attributes);
    bool parse_static_assertion();
    std::unique_ptr<FunctionDecl> parse_function(
        SourceLocation location, std::string name, std::string name_space,
        TypePtr return_type, Linkage linkage, bool inline_hint,
        std::vector<Attribute> attributes);
    std::unique_ptr<ObjectDecl> parse_object(
        SourceLocation location, std::string name, TypePtr type, Linkage linkage,
        std::vector<Attribute> attributes);
    ParameterDecl parse_parameter(unsigned ordinal);

    std::unique_ptr<Statement> parse_statement();
    std::unique_ptr<Statement> parse_compound();
    std::unique_ptr<Statement> parse_local_declaration();
    bool local_declaration_start() const;

    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_assignment();
    std::unique_ptr<Expr> parse_conditional();
    std::unique_ptr<Expr> parse_binary(int minimum_precedence);
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix();
    std::unique_ptr<Expr> parse_primary();
    std::vector<FunctionDecl::GenericParameter> generic_parameters(
        const std::vector<Attribute>& attributes);
    static int precedence(std::string_view operation);

    std::vector<Token> tokens_;
    Diagnostics& diagnostics_;
    std::size_t index_{};
    std::vector<std::string> active_imports_;
    std::vector<std::string> active_generic_types_;
    std::string active_namespace_;
    std::unordered_map<std::string, BuiltinType> enum_types_;
    struct RecordTag {
        bool is_union{};
        bool complete{};
    };
    std::unordered_map<std::string, RecordTag> record_types_;
    std::unordered_map<std::string, TypePtr> type_aliases_;
    bool parsing_generic_argument_{};
};

} // namespace cross
