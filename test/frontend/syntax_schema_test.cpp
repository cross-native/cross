// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace cross;
using Node = std::shared_ptr<const SyntaxNode>;
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::abort(); }
}
Node production(Node node, SyntaxProduction expected, std::size_t children) {
    if (!node || node->kind != SyntaxNode::Kind::Core || node->production != expected) {
        std::cerr << "expected " << syntax_production_name(expected) << ", found "
                  << (node ? syntax_production_name(node->production) : "null") << '\n';
        std::abort();
    }
    if (node->children.size() != children) {
        std::cerr << syntax_production_name(expected) << ": expected " << children
                  << " children, found " << node->children.size() << '\n';
        std::abort();
    }
    return node;
}
void token(Node node, std::string_view spelling) {
    require(node && node->kind == SyntaxNode::Kind::Token &&
            node->tokens.size() == 1 && node->tokens.front().text == spelling,
            "incorrect public terminal");
}
Node child(Node node, std::size_t at, SyntaxProduction expected, std::size_t children) {
    require(at < node->children.size(), "missing public child");
    return production(node->children[at], expected, children);
}
Node descendant(Node node, SyntaxProduction expected) {
    std::vector<Node> pending{std::move(node)};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (current->kind == SyntaxNode::Kind::Core && current->production == expected)
            return current;
        for (auto at = current->children.rbegin(); at != current->children.rend(); ++at)
            pending.push_back(*at);
    }
    std::cerr << "missing descendant " << syntax_production_name(expected) << '\n';
    std::abort();
}
}

int main() {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto parse = [&](std::string_view text, SyntaxPatternElement::Kind kind) -> Node {
        const auto* source = sources.add("schema.x", std::string(text));
        auto input = Lexer(*source, diagnostics).lex();
        Parser parser(input, diagnostics);
        const auto fragment = parser.parse_syntax_fragment(kind, 0);
        if (!fragment) {
            std::cerr << "failed schema recognition: " << text << '\n' << messages.str();
            std::abort();
        }
        require(diagnostics.errors() == 0, "speculative diagnostics escaped");
        auto projected = syntax_node_tokens(*fragment->node);
        require(projected.size() == fragment->end, "projection omitted source terminals");
        for (std::size_t at = 0; at < projected.size(); ++at) {
            require(projected[at].kind == input[at].kind && projected[at].text == input[at].text,
                    "projection changed source terminals");
            require(projected[at].origin.span.line == input[at].location.line &&
                    projected[at].origin.span.column == input[at].location.column,
                    "projection changed source spans");
        }
        return fragment->node;
    };
    using P = SyntaxProduction;
    using K = SyntaxPatternElement::Kind;

    auto type = production(parse("const u32 * [[address_space(0)]] volatile restrict", K::Type),
                           P::TypeName, 2);
    auto specifiers = child(type, 0, P::DeclarationSpecifiers, 2);
    auto qualifier = child(child(specifiers, 0, P::DeclarationSpecifier, 1),
                           0, P::TypeQualifier, 1);
    token(qualifier->children[0], "const");
    auto scalar = child(child(child(specifiers, 1, P::DeclarationSpecifier, 1),
                              0, P::TypeSpecifier, 1), 0, P::ScalarType, 1);
    token(scalar->children[0], "u32");
    auto abstract = child(type, 1, P::AbstractDeclarator, 1);
    auto pointer = child(abstract, 0, P::PointerPart, 4);
    token(pointer->children[0], "*");
    child(pointer, 1, P::AttributeSpecifier, 3);
    child(pointer, 2, P::TypeQualifier, 1);
    child(pointer, 3, P::TypeQualifier, 1);

    type = production(parse("u32 const [[atomic]] volatile * const [[address_space(0)]] restrict",
                            K::Type), P::TypeName, 2);
    specifiers = child(type, 0, P::DeclarationSpecifiers, 4);
    child(child(specifiers, 2, P::DeclarationSpecifier, 1), 0, P::AttributeSpecifier, 3);
    pointer = child(child(type, 1, P::AbstractDeclarator, 1), 0, P::PointerPart, 4);
    child(pointer, 1, P::TypeQualifier, 1);
    child(pointer, 2, P::AttributeSpecifier, 3);
    child(pointer, 3, P::TypeQualifier, 1);

    type = production(parse("u32 (* const)(in u16 p \"abi.input\", out u32 *, ...) "
                            "-> \"memory.result\" [[abi(\"custom\")]]", K::Type), P::TypeName, 2);
    abstract = child(type, 1, P::AbstractDeclarator, 4);
    token(abstract->children[0], "(");
    pointer = child(child(abstract, 1, P::AbstractDeclarator, 1), 0, P::PointerPart, 2);
    token(pointer->children[0], "*");
    token(abstract->children[2], ")");
    auto suffix = child(abstract, 3, P::FunctionSuffix, 5);
    auto parameters = child(suffix, 1, P::ParameterList, 5);
    auto parameter = child(parameters, 0, P::ParameterDeclaration, 4);
    child(parameter, 0, P::ParameterMode, 1);
    child(parameter, 1, P::DeclarationSpecifiers, 1);
    child(parameter, 2, P::Declarator, 1);
    child(parameter, 3, P::Location, 1);
    parameter = child(parameters, 2, P::ParameterDeclaration, 3);
    child(parameter, 2, P::AbstractDeclarator, 1);
    token(parameters->children[4], "...");
    auto result = child(suffix, 3, P::ResultLocation, 2);
    token(result->children[0], "->");
    token(result->children[1], "\"memory.result\"");
    child(suffix, 4, P::AttributeSpecifier, 3);

    auto declaration = production(parse("global const u32 *pointer, scalar = 7u32;", K::Declaration),
                                  P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 3);
    token(child(specifiers, 0, P::DeclarationSpecifier, 1)->children[0], "global");
    auto list = child(declaration, 1, P::InitDeclaratorList, 3);
    auto item = child(list, 0, P::InitDeclarator, 1);
    auto declarator = child(item, 0, P::Declarator, 2);
    child(declarator, 0, P::PointerPart, 1);
    child(declarator, 1, P::DirectDeclarator, 1);
    token(list->children[1], ",");
    item = child(list, 2, P::InitDeclarator, 3);
    child(item, 0, P::Declarator, 1);
    token(item->children[1], "=");
    child(item, 2, P::Initializer, 1);
    token(declaration->children[2], ";");

    declaration = production(parse("const global u32 ordered = 5u32;", K::Declaration),
                             P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 3);
    child(child(specifiers, 0, P::DeclarationSpecifier, 1), 0, P::TypeQualifier, 1);
    token(child(specifiers, 1, P::DeclarationSpecifier, 1)->children[0], "global");
    child(child(specifiers, 2, P::DeclarationSpecifier, 1), 0, P::TypeSpecifier, 1);

    declaration = production(parse("u32 static trailing_storage;", K::Declaration),
                             P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 2);
    child(child(specifiers, 0, P::DeclarationSpecifier, 1), 0, P::TypeSpecifier, 1);
    token(child(specifiers, 1, P::DeclarationSpecifier, 1)->children[0], "static");

    declaration = production(parse("global const [[aligned(8)]] u32 aligned;",
                                   K::Declaration), P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 4);
    token(child(specifiers, 0, P::DeclarationSpecifier, 1)->children[0], "global");
    child(child(specifiers, 1, P::DeclarationSpecifier, 1), 0, P::TypeQualifier, 1);
    child(child(specifiers, 2, P::DeclarationSpecifier, 1), 0, P::AttributeSpecifier, 3);
    child(child(specifiers, 3, P::DeclarationSpecifier, 1), 0, P::TypeSpecifier, 1);

    declaration = production(parse("[[atomic]] u32 counter;", K::Declaration),
                             P::Declaration, 4);
    child(declaration, 0, P::AttributeSpecifier, 3);
    specifiers = child(declaration, 1, P::DeclarationSpecifiers, 1);
    child(child(specifiers, 0, P::DeclarationSpecifier, 1), 0, P::TypeSpecifier, 1);

    declaration = production(parse("typedef u32 Word, *Pointer;", K::Declaration), P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 2);
    token(child(specifiers, 0, P::DeclarationSpecifier, 1)->children[0], "typedef");
    list = child(declaration, 1, P::InitDeclaratorList, 3);
    child(child(list, 0, P::InitDeclarator, 1), 0, P::Declarator, 1);
    child(child(list, 2, P::InitDeclarator, 1), 0, P::Declarator, 2);

    declaration = production(parse("u32 typedef Reordered, *ReorderedPointer;",
                                   K::Declaration), P::Declaration, 3);
    specifiers = child(declaration, 0, P::DeclarationSpecifiers, 2);
    child(child(specifiers, 0, P::DeclarationSpecifier, 1), 0, P::TypeSpecifier, 1);
    token(child(specifiers, 1, P::DeclarationSpecifier, 1)->children[0], "typedef");
    list = child(declaration, 1, P::InitDeclaratorList, 3);
    child(child(list, 0, P::InitDeclarator, 1), 0, P::Declarator, 1);
    child(child(list, 2, P::InitDeclarator, 1), 0, P::Declarator, 2);

    declaration = production(parse("[[aligned(4)]] static u32 table[2] = { [1] = 3u32 };",
                                   K::Declaration), P::Declaration, 4);
    child(declaration, 0, P::AttributeSpecifier, 3);
    list = child(declaration, 2, P::InitDeclaratorList, 1);
    item = child(list, 0, P::InitDeclarator, 3);
    declarator = child(item, 0, P::Declarator, 1);
    auto direct = child(declarator, 0, P::DirectDeclarator, 2);
    child(direct, 1, P::ArraySuffix, 3);
    child(item, 2, P::Initializer, 3);

    const auto function_source = "global u32 fn(in u16 value) -> \"custom.result\" "
                                 "[[clobber(\"memory\")]] { return 7u32; }";
    auto header = production(parse(function_source, K::FunctionHeader), P::FunctionHeader, 3);
    child(header, 0, P::DeclarationSpecifiers, 2);
    direct = child(child(header, 1, P::Declarator, 1), 0, P::DirectDeclarator, 2);
    suffix = child(direct, 1, P::FunctionSuffix, 4);
    child(suffix, 3, P::ResultLocation, 2);
    child(header, 2, P::AttributeSpecifier, 3);
    auto definition = production(parse(function_source, K::FunctionDefinition), P::FunctionDefinition, 4);
    child(definition, 1, P::Declarator, 1);
    child(definition, 3, P::CompoundStatement, 3);
    declaration = production(parse("global u32 fn(in u16 value) -> \"stack.result\" "
                                   "[[clobber(\"memory\")]];", K::FunctionDeclaration), P::Declaration, 3);
    item = child(child(declaration, 1, P::InitDeclaratorList, 1), 0, P::InitDeclarator, 2);
    child(item, 0, P::Declarator, 1);
    child(item, 1, P::AttributeSpecifier, 3);

    declaration = production(parse("enum E [[underlying(u16)]] { first = 4u16, second, };",
                                   K::Declaration), P::Declaration, 2);
    auto enumeration = child(child(child(child(declaration, 0, P::DeclarationSpecifiers, 1),
                                        0, P::DeclarationSpecifier, 1),
                                  0, P::TypeSpecifier, 1), 0, P::EnumSpecifier, 9);
    child(enumeration, 4, P::Enumerator, 3);
    child(enumeration->children[4], 2, P::ConstantExpression, 1);
    child(enumeration, 6, P::Enumerator, 1);

    declaration = production(parse("struct Mixed { u32 *pointer, scalar; u32 bits : 3u32; };",
                                   K::Declaration), P::Declaration, 2);
    auto record = child(child(child(child(declaration, 0, P::DeclarationSpecifiers, 1),
                                   0, P::DeclarationSpecifier, 1),
                             0, P::TypeSpecifier, 1), 0, P::StructOrUnionSpecifier, 6);
    auto member = child(record, 3, P::MemberDeclaration, 5);
    child(member, 0, P::DeclarationSpecifiers, 1);
    declarator = child(child(member, 1, P::MemberDeclarator, 1), 0, P::Declarator, 2);
    child(declarator, 0, P::PointerPart, 1);
    child(child(member, 3, P::MemberDeclarator, 1), 0, P::Declarator, 1);
    member = child(record, 4, P::MemberDeclaration, 3);
    child(child(member, 1, P::MemberDeclarator, 3), 2, P::ConstantExpression, 1);

    definition = production(parse("global T identity<T, u32 N, u32 *P>(in T value) "
                                  "{ return value; }", K::FunctionDefinition), P::FunctionDefinition, 3);
    direct = child(child(definition, 1, P::Declarator, 1), 0, P::DirectDeclarator, 3);
    auto generic = child(direct, 1, P::GenericParameterList, 7);
    token(child(generic, 1, P::GenericParameter, 1)->children[0], "T");
    auto generic_parameter = child(generic, 3, P::GenericParameter, 2);
    child(generic_parameter, 0, P::TypeName, 1);
    token(generic_parameter->children[1], "N");
    generic_parameter = child(generic, 5, P::GenericParameter, 2);
    type = child(generic_parameter, 0, P::TypeName, 2);
    child(type, 1, P::AbstractDeclarator, 1);
    token(generic_parameter->children[1], "P");

    const auto* alias_source = sources.add("alias-schema.x",
        "namespace ns { typedef u32 Word; } ns::Word value;");
    auto alias_tokens = Lexer(*alias_source, diagnostics).lex();
    Parser alias_parser(alias_tokens, diagnostics);
    auto alias_program = alias_parser.parse();
    require(diagnostics.errors() == 0 && alias_program.objects.size() == 1, "alias fixture failed");
    std::size_t first = 0;
    while (!alias_tokens[first].is("}")) ++first;
    const auto alias_fragment = alias_parser.parse_syntax_fragment(K::Declaration, first + 1);
    require(alias_fragment.has_value(), "qualified alias capture failed");
    auto alias = child(child(child(child(alias_fragment->node, 0, P::DeclarationSpecifiers, 1),
                                  0, P::DeclarationSpecifier, 1),
                            0, P::TypeSpecifier, 1), 0, P::TypedefName, 1);
    child(alias, 0, P::QualifiedName, 3);

    auto statement = production(parse("{ register u32 value \"chosen.register\" [[aligned(4)]] = 3u32; }",
                                     K::Statement), P::Statement, 1);
    auto block = child(child(statement, 0, P::UnattributedStatement, 1),
                       0, P::CompoundStatement, 3);
    declaration = child(child(child(block, 1, P::Statement, 1),
                               0, P::UnattributedStatement, 1), 0, P::Declaration, 3);
    item = child(child(declaration, 1, P::InitDeclaratorList, 1), 0, P::InitDeclarator, 5);
    child(item, 1, P::ObjectLocation, 1);
    child(item, 2, P::AttributeSpecifier, 3);

    statement = production(parse("{ typedef u32 Word, *Pointer; "
                                 "Word left = 3u32, right = left + 4u32; "
                                 "Pointer p = &left, q = &right; right += *p; }", K::Statement),
                           P::Statement, 1);
    block = child(child(statement, 0, P::UnattributedStatement, 1), 0, P::CompoundStatement, 6);
    for (std::size_t at = 1; at <= 3; ++at) {
        declaration = child(child(child(block, at, P::Statement, 1),
                                   0, P::UnattributedStatement, 1), 0, P::Declaration, 3);
        list = child(declaration, 1, P::InitDeclaratorList, 3);
        token(list->children[1], ",");
    }

    auto expression = parse("$::embed(\"unread-asset.bin\")", K::Expr);
    auto embed = production(descendant(expression, P::EmbedExpression), P::EmbedExpression, 4);
    token(embed->children[0], "$::embed");
    token(embed->children[1], "(");
    token(embed->children[2], "\"unread-asset.bin\"");
    token(embed->children[3], ")");
    expression = parse("$::quote { alien! ([x] { word }) $::unquote(no_such_macro! { foreign; }) }",
                       K::Expr);
    auto quote = production(descendant(expression, P::QuoteExpression), P::QuoteExpression, 4);
    token(quote->children[0], "$::quote");
    token(quote->children[1], "{");
    auto contents = child(quote, 2, P::BalancedTokens, 5);
    token(contents->children[0], "alien");
    token(contents->children[1], "!");
    child(contents, 2, P::BalancedTokenTree, 3);
    token(contents->children[3], "$::unquote");
    child(contents, 4, P::BalancedTokenTree, 3);
    token(quote->children[3], "}");
    expression = parse("$::quote {}", K::Expr);
    quote = production(descendant(expression, P::QuoteExpression), P::QuoteExpression, 4);
    contents = child(quote, 2, P::BalancedTokens, 0);
    require(contents->span.first.offset == quote->children[3]->span.first.offset &&
            contents->span.last.offset == contents->span.first.offset,
            "empty quote content has the wrong boundary span");
    require(diagnostics.errors() == 0, "schema diagnostics escaped");
}
