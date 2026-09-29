// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace cross {
struct SyntaxParseEnvironmentTestAccess {
    static void header(SyntaxParseEnvironment& environment,
                       std::shared_ptr<const SyntaxHeaderBindings> bindings) {
        environment.header_bindings = std::move(bindings);
    }
    static auto header(const SyntaxParseEnvironment& environment) {
        return environment.header_bindings;
    }
    static void alias(SyntaxParseEnvironment& environment, std::string name, TypePtr type) {
        environment.aliases.emplace(std::move(name),
            std::make_shared<const AliasDefinition>(type, 224));
    }
};
} // namespace cross

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
        std::string shape_error;
        if (!syntax_validate_node(*fragment->node, shape_error)) {
            std::cerr << "invalid public shape: " << text << '\n' << shape_error << '\n';
            std::abort();
        }
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

    {
        const auto node = parse("{ typedef u32 (*Alias)(in u16 *value); Alias object; }", K::Statement);
        const auto projected = syntax_node_tokens(*node);
        std::shared_ptr<const AliasBinding> declaration;
        std::shared_ptr<const AliasBinding> use;
        MetaToken use_token;
        for (const auto& token : projected) {
            if (token.text != "Alias") continue;
            require(token.origin.alias_binding != nullptr, "parsed typedef token lost its binding");
            if (token.origin.alias_binding->role == AliasBinding::Role::Declaration)
                declaration = token.origin.alias_binding;
            else { use = token.origin.alias_binding; use_token = token; }
        }
        require(declaration && use && declaration->definition == use->definition,
                "typedef declaration and use did not share their opaque definition");
        auto changed = use->definition->instantiate();
        auto signature = changed->pointee->function;
        signature->abi = "changed_by_consumer";
        signature->result->builtin = BuiltinType::U8;
        signature->parameters[0].type->pointee->builtin = BuiltinType::U64;
        const auto original = use->definition->instantiate()->pointee->function;
        require(original->abi.empty() && original->result->builtin == BuiltinType::U32 &&
                original->parameters[0].type->pointee->builtin == BuiltinType::U16,
                "typedef consumers mutated the retained callable type graph");
        SyntaxNode leaf;
        leaf.kind = SyntaxNode::Kind::Token;
        leaf.tokens.push_back(use_token);
        const auto retained_storage = syntax_node_storage(leaf);
        leaf.tokens.front().origin.alias_binding.reset();
        require(retained_storage - syntax_node_storage(leaf) == alias_binding_storage(use),
                "public-tree storage did not account for a retained typedef type graph");
        require(use->definition->storage() > 128,
                "retained typedef graph storage omitted its callable and pointer children");
    }

    {
        auto header = std::make_shared<SyntaxHeaderBindings>();
        auto environment = std::make_shared<SyntaxParseEnvironment>();
        SyntaxParseEnvironmentTestAccess::header(*environment, header);
        auto context = std::make_shared<SyntaxContext>();
        context->parse_environment = environment;
        const auto* source = sources.add("header-capture.x", "sizeof(T *)");
        const auto input = Lexer(*source, diagnostics).lex();
        auto captured = Parser::parse_syntax_tokens(SyntaxParseCategory::Expression,
                                                    input, context, diagnostics);
        require(captured && captured->kind == SyntaxNode::Kind::Deferred &&
                SyntaxParseEnvironmentTestAccess::header(*captured->context->parse_environment) == header,
                "pending capture lost its original header dependency");
        const auto before = syntax_node_storage(*captured);
        header->parameters.push_back({"T", {}, input[2].location});
        header->storage += 64;
        header->complete = true;
        require(syntax_node_storage(*captured) > before,
                "completed header bindings were omitted from context storage");
        auto destination = std::make_shared<SyntaxHeaderBindings>();
        destination->parameters.push_back({"T", builtin_type(BuiltinType::U32), input[2].location});
        destination->complete = true;
        SyntaxParseEnvironmentTestAccess::header(*environment, destination);
        SyntaxParseEnvironmentTestAccess::alias(*environment, "Later", builtin_type(BuiltinType::U32));
        auto reparsed = Parser::parse_syntax_tokens(SyntaxParseCategory::Expression,
            input, captured->context, diagnostics);
        require(reparsed && reparsed->kind == SyntaxNode::Kind::Core,
                "relocated capture used destination generic bindings");
        const auto* later_source = sources.add("later-type.x", "Later");
        require(!Parser::parse_syntax_tokens(SyntaxParseCategory::Type,
                    Lexer(*later_source, diagnostics).lex(), captured->context, diagnostics),
                "header completion exposed unrelated later aliases");
        require(diagnostics.errors() == 0, "header recognition leaked speculative diagnostics");
    }

    const auto bound_tokens = syntax_node_tokens(*parse(
        "{ u32 value = 7u32; value += 1u32; }", K::Statement));
    require(bound_tokens.size() == 11 &&
            bound_tokens[6].origin.value_binding.kind == ValueBinding::Kind::Local &&
            bound_tokens[6].origin.value_binding.declaration ==
                bound_tokens[2].origin.identity,
            "parsed source value did not retain its exact declaring token");
    const auto free_tokens = syntax_node_tokens(*parse("outside + 1u32", K::Expr));
    require(!free_tokens.empty() &&
            free_tokens.front().origin.value_binding.kind == ValueBinding::Kind::Nonlocal,
            "parsed free value can be captured by a relocated local");
    const auto expression_child = parse("outside + 1u32", K::Expr);
    auto splice_wrapper = std::make_shared<SyntaxNode>();
    splice_wrapper->kind = SyntaxNode::Kind::Core;
    splice_wrapper->production = P::PrimaryExpression;
    splice_wrapper->structured_splice = true;
    splice_wrapper->children.push_back(expression_child);
    splice_wrapper->span = expression_child->span;
    splice_wrapper->context = expression_child->context;
    std::string splice_error;
    require(syntax_validate_node(*splice_wrapper, splice_error),
            "expression splice public shape failed validation");
    const auto structured = syntax_node_fragments(*splice_wrapper);
    require(structured.size() == 1 && structured.front().kind == TokenKind::StructuredSplice &&
            structured.front().splice == expression_child,
            "expression splice lost its owned public subtree");
    require(syntax_node_tokens(*splice_wrapper).size() == 3,
            "explicit textual projection did not flatten expression splice");
    const auto ordinary_literal = syntax_node_fragments(*parse("3u32", K::Expr));
    require(ordinary_literal.size() == 1 && ordinary_literal.front().splice == nullptr &&
            ordinary_literal.front().kind == TokenKind::Integer,
            "ordinary literal primary was misclassified as a structured splice");
    auto raw_splice = std::make_shared<SyntaxNode>();
    raw_splice->kind = SyntaxNode::Kind::Token;
    raw_splice->tokens.push_back(structured.front());
    require(syntax_node_tokens(*raw_splice).size() == 3 &&
            syntax_node_fragments(*raw_splice).front().splice == expression_child,
            "raw-node projection or structured copying lost its splice boundary");
    splice_wrapper->children.front() = parse("return 1u32;", K::Statement);
    require(!syntax_validate_node(*splice_wrapper, splice_error),
            "statement subtree was accepted in an expression splice slot");
    {
        auto deferred = std::make_shared<SyntaxNode>();
        deferred->kind = SyntaxNode::Kind::Deferred;
        deferred->slot_production = P::AssignmentExpression;
        deferred->deferred_category = SyntaxParseCategory::Expression;
        deferred->span = expression_child->span;
        deferred->context = expression_child->context;
        deferred->tokens = free_tokens;
        auto parent = std::make_shared<SyntaxNode>();
        parent->kind = SyntaxNode::Kind::Core;
        parent->production = P::Expression;
        parent->children.push_back(deferred);
        const auto nested_fragments = syntax_node_fragments(*parent);
        require(nested_fragments.size() == 1 &&
                nested_fragments.front().kind == TokenKind::StructuredSplice &&
                nested_fragments.front().splice == deferred,
                "nested deferred syntax lost its category/context boundary");
        std::ostringstream output;
        Diagnostics local(output);
        SyntaxExecution execution(sources, local, 64, {}, {}, {}, {});
        require(!execution.materialize_node(*deferred, deferred->span.first) &&
                output.str().find("requires explicit $::meta::tokens projection") != std::string::npos,
                "unsettled deferred input was silently structured-spliced");
    }
    for (const bool inherited : {false, true}) {
        const auto* header_source = sources.add("generic-attribute.x",
            "[[generic(u32 N), aligned(N)]] static u32 f() { return 0u32; }");
        auto header = Lexer(*header_source, diagnostics).lex();
        unsigned names{};
        for (auto& item : header)
            if (item.text == "N" && ++names == 2 && inherited)
                item.value_binding.kind = ValueBinding::Kind::Nonlocal;
        Parser header_parser(std::move(header), diagnostics);
        auto parsed = header_parser.parse();
        require(parsed.functions.size() == 1 && parsed.functions.front()->attributes.size() == 2,
                "generic attribute binding fixture failed to parse");
        const auto& value = parsed.functions.front()->attributes[1].expression_argument;
        require(value && value->name_context &&
                value->name_context->value_binding.kind ==
                    (inherited ? ValueBinding::Kind::Nonlocal : ValueBinding::Kind::Local),
                "function header retargeted an inherited generic-value use");
    }

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

    // Type attributes are ordinary grammar occurrences at their written
    // positions, including registry numbers not interpreted by tree inspection.
    for (const auto text : {
             "[[atomic]] const u32 volatile", "const [[atomic]] u32 volatile",
             "const u32 [[atomic]] volatile", "const u32 volatile [[atomic]]",
             "[[address_space(17)]] const u32 * volatile",
             "const [[address_space(17)]] u32 * volatile",
             "const u32 [[address_space(17)]] (* volatile)",
             "const u32 * [[address_space(17)]] volatile",
             "u32 * [[atomic]] const * volatile [[address_space(23)]]",
             "u32 (* const [[address_space(17)]] [3u32])(in u16)",
             "[[address_space(17)]] u32 (* volatile)[3u32]",
             "$::meta::bytes", "const $::meta::buffer"})
        (void)parse(text, K::Type);
    auto leading_qualified = production(parse("[[atomic]] const u32 volatile", K::Type),
                                         P::TypeName, 1);
    auto trailing_qualified = production(parse("const u32 volatile [[atomic]]", K::Type),
                                          P::TypeName, 1);
    auto leading_specifiers = child(leading_qualified, 0, P::DeclarationSpecifiers, 4);
    auto trailing_specifiers = child(trailing_qualified, 0, P::DeclarationSpecifiers, 4);
    child(child(leading_specifiers, 0, P::DeclarationSpecifier, 1), 0, P::AttributeSpecifier, 3);
    child(child(trailing_specifiers, 3, P::DeclarationSpecifier, 1), 0, P::AttributeSpecifier, 3);
    auto builtin_specifier = descendant(parse("$::meta::bytes", K::Type), P::TypeSpecifier);
    child(child(builtin_specifier, 0, P::TargetScalarBuiltinName, 1), 0, P::BuiltinName, 1);
    {
        std::string error;
        auto replaced_specifier = syntax_replace_child(*leading_specifiers, 0,
            trailing_specifiers->children[3], error);
        require(replaced_specifier && replaced_specifier->children[0] == trailing_specifiers->children[3],
                "type-attribute replacement lost the exact source child");
        require(replaced_specifier->children[1] == leading_specifiers->children[1],
                "type-attribute replacement changed an untouched qualifier");
        require(!syntax_replace_child(*leading_specifiers, 0,
            trailing_specifiers->children[3]->children[0], error),
                "a bare attribute bypassed its declaration-specifier wrapper");
    }

    for (const auto operand : {"u32", "struct Tag *", "value", "(value + other)"}) {
        const auto tree = parse(std::string("$::alignof(") + operand + ")", K::Expr);
        const auto primary = production(descendant(tree, P::PrimaryExpression), P::PrimaryExpression, 4);
        token(primary->children[0], "$::alignof");
        token(primary->children[1], "(");
        token(primary->children[3], ")");
        const auto type_operand = std::string_view(operand) == "u32" ||
                                  std::string_view(operand) == "struct Tag *";
        require(primary->children[2]->production == (type_operand ? P::TypeName : P::Expression),
                "alignof operand lost its type/expression shape");
        std::string error;
        require(!syntax_replace_child(*primary, 2, primary->children[0], error),
                "alignof accepted an unstructured operand token");
    }

    for (const auto tag : {"struct", "union"}) {
        const auto attributed = std::string(tag) + " Attributed [[packed]] [[aligned(8)]] { u32 value; }";
        for (const auto kind : {K::Type, K::Declaration, K::Statement, K::FunctionHeader,
                                K::FunctionDefinition}) {
            const auto source = kind == K::Type ? attributed :
                kind == K::Declaration || kind == K::Statement ? attributed + " object;" :
                "static " + attributed + " function() { return 7u32; }";
            const auto tree = parse(source, kind);
            const auto record = production(descendant(tree, P::StructOrUnionSpecifier),
                                           P::StructOrUnionSpecifier, 7);
            token(record->children[0], tag);
            child(record, 1, P::QualifiedName, 1);
            const auto packed = child(record, 2, P::AttributeSpecifier, 3);
            const auto aligned = child(record, 3, P::AttributeSpecifier, 3);
            token(record->children[4], "{");
            child(record, 5, P::MemberDeclaration, 3);
            token(record->children[6], "}");
            std::string error;
            const auto replaced = syntax_replace_child(*record, 2, aligned, error);
            require(replaced != nullptr, "record attribute replacement was rejected");
            require(replaced->children[1] == record->children[1] &&
                    replaced->children[5] == record->children[5],
                    "record attribute replacement changed untouched children");
            require(!syntax_replace_child(*record, 4, packed, error),
                    "record attribute replaced a required opening brace");
        }
    }

    for (const auto tag : {"struct", "union", "enum"}) {
        const bool enumeration = std::string_view(tag) == "enum";
        const auto anonymous = std::string(tag) + (enumeration
            ? " [[underlying(u16)]] { A = 3u16, B }"
            : " [[aligned(8)]] { u32 value; }");
        for (const auto kind : {K::Type, K::Declaration, K::Statement, K::FunctionHeader,
                                K::FunctionDefinition}) {
            const auto source = kind == K::Type ? anonymous :
                kind == K::Declaration || kind == K::Statement ? anonymous + " object;" :
                "static " + anonymous + " function() { return 0u32; }";
            const auto tree = parse(source, kind);
            const auto definition = production(descendant(tree, enumeration ? P::EnumSpecifier : P::StructOrUnionSpecifier),
                enumeration ? P::EnumSpecifier : P::StructOrUnionSpecifier, enumeration ? 7 : 5);
            token(definition->children[0], tag);
            child(definition, 1, P::AttributeSpecifier, 3);
            token(definition->children[2], "{");
            token(definition->children.back(), "}");
            if (kind == K::Type || kind == K::Declaration || kind == K::Statement) {
                const auto tokens = syntax_node_tokens(*definition);
                require(tokens.front().origin.tag_binding && tokens.front().origin.tag_binding->type.identity &&
                        tokens.front().origin.tag_binding->type.name.empty() &&
                        tokens.front().origin.tag_binding->role == TagBinding::Role::Declaration,
                        "anonymous definition keyword lost its private nominal binding");
            }
        }
    }
    for (const auto kind : {K::FunctionHeader, K::FunctionDefinition}) {
        const auto tree = parse("static struct { T value; } *function<T>(in T input) { return 0u32; }", kind);
        const auto definition = production(descendant(tree, P::StructOrUnionSpecifier),
                                           P::StructOrUnionSpecifier, 4);
        child(definition, 2, P::MemberDeclaration, 3);
        descendant(tree, P::GenericParameterList);
    }

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
    for (const auto kind : {K::FunctionHeader, K::FunctionDefinition}) {
        auto inline_header = production(parse(
            "static struct InlineResult { u32 value; } fn(in T value) "
            "[[generic(T)]] { struct InlineResult result = { (u32)value }; return result; }", kind),
            kind == K::FunctionHeader ? P::FunctionHeader : P::FunctionDefinition,
            kind == K::FunctionHeader ? 3 : 4);
        auto record = production(descendant(inline_header, P::StructOrUnionSpecifier),
                                 P::StructOrUnionSpecifier, 5);
        token(record->children.front(), "struct");
        child(record, 3, P::MemberDeclaration, 3);
        token(record->children.back(), "}");
        child(inline_header, 2, P::AttributeSpecifier, 3);
        if (kind == K::FunctionDefinition)
            production(inline_header->children.back(), P::CompoundStatement, 4);
    }
    auto definition = production(parse(function_source, K::FunctionDefinition), P::FunctionDefinition, 4);
    auto composed = std::make_shared<SyntaxNode>(*definition);
    composed->children = {header, definition->children.back()};
    std::string header_shape_error;
    require(syntax_validate_node(*composed, header_shape_error),
            "composed header/body public shape is invalid");
    const auto composed_fragments = syntax_node_fragments(*composed);
    require(!composed_fragments.empty() && composed_fragments.front().splice == header,
            "composed function flattened its retained header identity");
    {
        auto header_execution = std::make_shared<SyntaxExecution>(sources, diagnostics, 64,
            LayoutQuery{}, LayoutQuery{}, EvaluationLimits{}, EvaluationLayout{});
        const auto parse_decorated = [&](Node input, std::string_view text, K kind) {
            const auto* source = sources.add("decorated-header.x", std::string(text));
            auto tokens = Lexer(*source, diagnostics).lex();
            for (auto& item : tokens) {
                if (!item.is("header_marker")) continue;
                item.kind = TokenKind::StructuredSplice;
                item.splice = input;
            }
            Parser parser(tokens, diagnostics, header_execution, 64);
            const auto result = parser.parse_syntax_fragment(kind, 0);
            require(result && result->end + 1 == tokens.size(),
                    "decorated header did not consume its bounded input");
            require(syntax_validate_node(*result->node, header_shape_error),
                    "decorated header public tree failed validation");
            return result->node;
        };
        Node nested = header;
        for (unsigned at = 0; at < 4; ++at) {
            const auto previous = nested;
            nested = production(parse_decorated(previous,
                "[[noinline]] header_marker [[aligned(16)]]", K::FunctionHeader),
                P::FunctionHeader, 3);
            child(nested, 0, P::AttributeSpecifier, 3);
            child(nested, 2, P::AttributeSpecifier, 3);
            require(nested->children[1] == previous,
                    "decorated header cloned or flattened its retained child");
            const auto fragments = syntax_node_fragments(*nested);
            require(fragments.size() == 10 && fragments[3].splice == previous,
                    "decorated header serialization lost attributes or child identity");
            require(parse_decorated(nested, "header_marker", K::FunctionHeader) == nested,
                    "bare header round trip introduced an unnecessary wrapper");
        }
        const auto prototype = production(parse_decorated(nested,
            "[[noinline]] header_marker [[aligned(16)]];", K::FunctionDeclaration),
            P::Declaration, 4);
        require(prototype->children[1] == nested,
                "decorated prototype lost header identity");
        const auto decorated_definition = production(parse_decorated(nested,
            "[[noinline]] header_marker [[aligned(16)]] { return 1u32; }", K::FunctionDefinition),
            P::FunctionDefinition, 4);
        require(decorated_definition->children[1] == nested,
                "decorated definition lost header identity");
        auto replacement = syntax_replace_child(*nested, 1, header, header_shape_error);
        require(replacement && replacement->children[1] == header,
                "compatible header-child replacement failed");
        require(!syntax_replace_child(*nested, 1, decorated_definition, header_shape_error),
                "function definition was accepted in a header-child slot");
        require(diagnostics.errors() == 0, "decorated header diagnostics escaped");
    }
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

    declaration = production(parse("enum Inline [[underlying(u16)]] { low = 2u16, high, } value = high;",
                                   K::Declaration), P::Declaration, 3);
    enumeration = child(child(child(child(declaration, 0, P::DeclarationSpecifiers, 1),
                                     0, P::DeclarationSpecifier, 1),
                               0, P::TypeSpecifier, 1), 0, P::EnumSpecifier, 9);
    child(enumeration, 4, P::Enumerator, 3);
    child(enumeration, 6, P::Enumerator, 1);
    item = child(child(declaration, 1, P::InitDeclaratorList, 1),
                 0, P::InitDeclarator, 3);
    child(item, 0, P::Declarator, 1);
    child(item, 2, P::Initializer, 1);

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

    declaration = production(parse("struct InlineRecord { u32 first, second; } item = { 1u32, 2u32 };",
                                   K::Declaration), P::Declaration, 3);
    record = child(child(child(child(declaration, 0, P::DeclarationSpecifiers, 1),
                                0, P::DeclarationSpecifier, 1),
                          0, P::TypeSpecifier, 1), 0, P::StructOrUnionSpecifier, 5);
    member = child(record, 3, P::MemberDeclaration, 5);
    child(member, 1, P::MemberDeclarator, 1);
    child(member, 3, P::MemberDeclarator, 1);
    item = child(child(declaration, 1, P::InitDeclaratorList, 1),
                 0, P::InitDeclarator, 3);
    child(item, 0, P::Declarator, 1);
    child(item, 2, P::Initializer, 5);

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

    definition = production(parse("static T [[generic(T), noinline]] "
                                  "identity(in T value) { return value; }",
                                  K::FunctionDefinition), P::FunctionDefinition, 3);
    specifiers = child(definition, 0, P::DeclarationSpecifiers, 3);
    auto interleaved = child(specifiers, 2, P::DeclarationSpecifier, 1);
    child(interleaved, 0, P::AttributeSpecifier, 5);
    definition = production(parse("static T identity(in T value) "
                                  "[[noinline, generic(T, T *pointer)]] { return value; }",
                                  K::FunctionDefinition), P::FunctionDefinition, 4);
    child(definition, 2, P::AttributeSpecifier, 5);
    header = production(parse("static T identity(in T value) [[generic(T)]]",
                              K::FunctionHeader), P::FunctionHeader, 3);
    child(header, 2, P::AttributeSpecifier, 3);
    declaration = production(parse("static T identity(in T value) [[generic(T)]];",
                                   K::FunctionDeclaration), P::Declaration, 3);
    item = child(child(declaration, 1, P::InitDeclaratorList, 1),
                 0, P::InitDeclarator, 2);
    child(item, 1, P::AttributeSpecifier, 3);

    header = production(parse("static T (identity<T>)(in T (value)) [[noinline]]",
                              K::FunctionHeader), P::FunctionHeader, 3);
    direct = child(child(header, 1, P::Declarator, 1), 0, P::DirectDeclarator, 4);
    token(direct->children[0], "(");
    token(direct->children[2], ")");
    auto grouped_direct = child(child(direct, 1, P::Declarator, 1), 0, P::DirectDeclarator, 2);
    child(grouped_direct, 1, P::GenericParameterList, 3);
    child(direct, 3, P::FunctionSuffix, 3);
    for (const auto text : {"static u32 ((fn))(in u16 (value)) [[noinline]]",
                           "static u32 (*callback(in u16 value))(in u32 other)",
                           "static u32 accepts(u32 (u16), u32 (named))"})
        (void)parse(text, K::FunctionHeader);
    for (const auto text : {"u32 (in u16)", "u32 ((*)[3u32])", "u32 ([3u32])"})
        (void)parse(text, K::Type);

    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, 32,
        no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
    for (const auto text : {
             "{ future!{}; NewType item = 3u32; }",
             "{ future!{}; return (NewType)3u32; }",
             "{ future!{}; return introduced<u32>(3u32); }",
             "{ future!{}; for (NewType item = 0u32; ; ) {} }",
             "{ future!{}; struct Introduced value; }",
             "{ future!{}; enum Introduced value; }",
             "{ old::<nested::<3u32>>; future!{}; NewType item; }"}) {
        const auto* source = sources.add("deferred-schema.x", text);
        auto input = Lexer(*source, diagnostics).lex();
        Parser deferred_parser(input, diagnostics, execution, 32);
        const auto fragment = deferred_parser.parse_syntax_fragment(K::Statement, 0);
        require(fragment.has_value(), "name-sensitive compound capture failed");
        auto ordinary = child(fragment->node, 0, P::UnattributedStatement, 1);
        auto deferred = ordinary->children[0];
        require(deferred->kind == SyntaxNode::Kind::Deferred && deferred->children.empty(),
                "name-sensitive block was prematurely classified");
        require(deferred->slot_production == P::CompoundStatement &&
                deferred->deferred_category == SyntaxParseCategory::Statement,
                "deferred block lost its category/grammar slot");
        require(deferred->context && deferred->span.first.file == source &&
                deferred->span.last.file == source, "deferred block lost source context");
        const auto projected = syntax_node_tokens(*fragment->node);
        require(projected.size() == input.size() - 1 && fragment->end == input.size() - 1,
                "deferred block boundary/projection changed");
        for (std::size_t at = 0; at < projected.size(); ++at)
            require(projected[at].kind == input[at].kind && projected[at].text == input[at].text,
                    "deferred block token projection changed");
        std::string error;
        require(syntax_validate_node(*fragment->node, error), "deferred block shape failed validation");
        auto replacement = std::make_shared<SyntaxNode>(*deferred);
        auto replaced = syntax_replace_child(*ordinary, 0, replacement, error);
        require(replaced && replaced->children[0] == replacement &&
                ordinary->children[0] == deferred, "deferred leaf replacement lost identity");
        replacement = std::make_shared<SyntaxNode>(*deferred);
        replacement->deferred_category = SyntaxParseCategory::Type;
        require(!syntax_validate_node(*replacement, error), "incompatible deferred category was accepted");
        replacement->deferred_category = SyntaxParseCategory::Statement;
        replacement->tokens.pop_back();
        require(!syntax_validate_node(*replacement, error), "unbounded deferred input was accepted");
        EvaluationLimits validation_limits;
        validation_limits.steps = 2;
        SyntaxTreeValidationError failure;
        require(!syntax_validate_node(*deferred, error, validation_limits, nullptr, &failure) &&
                failure == SyntaxTreeValidationError::WorkLimit, "deferred payload escaped work accounting");
        validation_limits.steps = EvaluationLimits{}.steps;
        validation_limits.depth = 1;
        require(!syntax_validate_node(*deferred, error, validation_limits, nullptr, &failure) &&
                failure == SyntaxTreeValidationError::DepthLimit, "deferred payload escaped depth accounting");
    }
    for (const auto text : {
             "{ future!{}; return 3u32; }",
             "{ typedef u32 Word; future!{}; Word item = 3u32; }",
             "{ u32 value = 3u32; future!{}; return value + 1u32; }",
             "{ { future!{}; } ordinary(3u32); }"}) {
        const auto* source = sources.add("known-schema.x", text);
        auto input = Lexer(*source, diagnostics).lex();
        Parser known_parser(input, diagnostics, execution, 32);
        const auto fragment = known_parser.parse_syntax_fragment(K::Statement, 0);
        require(fragment.has_value(), "known binding capture failed");
        auto ordinary = child(fragment->node, 0, P::UnattributedStatement, 1);
        require(ordinary->children[0]->kind == SyntaxNode::Kind::Core &&
                ordinary->children[0]->production == P::CompoundStatement,
                "opaque invocation invalidated an established binding or escaped its scope");
    }
    {
        const auto* source = sources.add("nested-deferred-schema.x",
            "{ typedef u32 Word; { future!{}; Word item = 3u32; } Word after; }");
        auto input = Lexer(*source, diagnostics).lex();
        Parser nested_parser(input, diagnostics, execution, 32);
        const auto fragment = nested_parser.parse_syntax_fragment(K::Statement, 0);
        require(fragment.has_value(), "nested uncertainty capture failed");
        auto outer = child(child(fragment->node, 0, P::UnattributedStatement, 1),
                           0, P::CompoundStatement, 5);
        auto nested = child(child(outer, 2, P::Statement, 1), 0, P::UnattributedStatement, 1);
        require(nested->children[0]->kind == SyntaxNode::Kind::Deferred,
                "closer unknown binding did not defer an outer alias use");
        child(child(child(outer, 3, P::Statement, 1), 0, P::UnattributedStatement, 1),
              0, P::Declaration, 3);
    }
    for (const auto text : {
             "{ u32 = ; future!{}; NewType item; }",
             "{ future!{}; NewType item;",
             "{ future!{}; NewType item; ) }"}) {
        const auto* source = sources.add("invalid-deferred-schema.x", text);
        auto input = Lexer(*source, diagnostics).lex();
        Parser invalid_parser(input, diagnostics, execution, 32);
        require(!invalid_parser.parse_syntax_fragment(K::Statement, 0),
                "deferral hid an already-known error or an invalid boundary");
    }
    {
        const auto* source = sources.add("nested-capture-definitions.x",
            "[[syntax_expander]] static $::meta::tokens unused(in $::meta::syntax_match input) "
            "{ return $::quote { ; }; } "
            "syntax Type : statement { prefix \"with_type\"; match value:type \";\"; expand unused; } "
            "syntax Expr : statement { prefix \"with_expr\"; match value:expr \";\"; expand unused; } "
            "syntax Stmt : statement { prefix \"with_stmt\"; match value:stmt; expand unused; } "
            "syntax Type, Expr, Stmt;");
        auto input = Lexer(*source, diagnostics).lex();
        std::vector<std::pair<std::size_t, SyntaxParseCategory>> cases;
        const auto add_case = [&](std::string_view text, SyntaxParseCategory category) {
            const auto* capture = sources.add("nested-capture-schema.x", std::string(text));
            auto tokens = Lexer(*capture, diagnostics).lex();
            cases.emplace_back(input.size(), category);
            input.insert(input.end(), tokens.begin(), tokens.end());
        };
        using C = SyntaxParseCategory;
        add_case("{ future!{}; with_type NewType *; }", C::Type);
        add_case("{ future!{}; with_expr (NewType)3u32; }", C::Expression);
        add_case("{ future!{}; with_expr introduced<u32>(3u32); }", C::Expression);
        add_case("{ future!{}; with_expr introduced::<NewType, u32>(3u32); }", C::Expression);
        add_case("{ future!{}; with_stmt NewType item = 3u32; }", C::Statement);
        add_case("{ future!{}; with_stmt if ((NewType)1u32) NewType first; else NewType second; }", C::Statement);
        add_case("{ future!{}; with_stmt [[musttail]] return (NewType)3u32; }", C::Statement);
        add_case("{ future!{}; with_stmt for (NewType i = 0u32; ; ) {} }", C::Statement);
        add_case("{ future!{}; with_stmt while ((NewType)1u32) NewType item; }", C::Statement);
        add_case("{ future!{}; with_stmt do NewType item; while ((NewType)1u32); }", C::Statement);
        add_case("{ future!{}; with_stmt label tagged: NewType item; }", C::Statement);
        add_case("{ future!{}; with_stmt switch ((NewType)1u32) {} }", C::Statement);
        add_case("{ future!{}; with_expr introduced<NewType, u32>(3u32); }", C::None);
        add_case("{ future!{}; with_stmt if (1u32) NewType item else second; }", C::None);
        Parser nested_parser(input, diagnostics, execution, 32);
        (void)nested_parser.parse(); // Stop at the definitions' End boundary.
        require(diagnostics.errors() == 0, "nested-capture declarations failed");
        for (const auto& [first, category] : cases) {
            const auto fragment = nested_parser.parse_syntax_fragment(K::Statement, first);
            if (category == C::None) {
                require(!fragment, "an ambiguous or incomplete nested capture boundary was guessed");
                continue;
            }
            require(fragment.has_value(), "independently bounded nested capture failed");
            std::vector<Node> pending{fragment->node};
            Node extension;
            while (!pending.empty()) {
                auto node = std::move(pending.back());
                pending.pop_back();
                if (node->kind == SyntaxNode::Kind::Extension) { extension = node; break; }
                pending.insert(pending.end(), node->children.begin(), node->children.end());
            }
            require(extension && extension->match && extension->match->fields.size() == 1,
                    "bounded extension match record was not preserved");
            const auto value = extension->match->fields[0].node;
            require(value && value->kind == SyntaxNode::Kind::Deferred && value->children.empty() &&
                    value->deferred_category == category && value->context,
                    "nested capture did not preserve its opaque deferred category/context");
            std::string error;
            require(syntax_validate_node(*value, error), "nested deferred node failed validation");
            const auto projected = syntax_node_tokens(*fragment->node);
            require(projected.size() == fragment->end - first, "nested deferred projection changed its boundary");
            for (std::size_t at = 0; at < projected.size(); ++at)
                require(projected[at].kind == input[first + at].kind &&
                        projected[at].text == input[first + at].text, "nested deferred projection changed a token");
        }
    }
    for (unsigned depth = 1; depth <= 4; ++depth) {
        SourceManager limit_sources;
        std::ostringstream limit_messages;
        Diagnostics limit_diagnostics(limit_messages);
        const auto* source = limit_sources.add("deferred-limits.x", "{ future!{}; NewType value; }");
        auto input = Lexer(*source, limit_diagnostics).lex();
        EvaluationLimits limits;
        limits.depth = depth;
        auto limited_execution = std::make_shared<SyntaxExecution>(limit_sources, limit_diagnostics, 32,
            no_layout, no_layout, limits, EvaluationLayout{});
        Parser limited_parser(input, limit_diagnostics, limited_execution, 32);
        require(!limited_parser.parse_syntax_fragment(K::Statement, 0) &&
                limit_diagnostics.errors() != 0, "exhausted deferred construction published a tree");
    }

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

    {
        const auto* source = sources.add("bounded-parse-context.x",
            "typedef u32 Word; static T identity<T>(in T value) { return value; } "
            "[[syntax_expander]] static $::meta::tokens never_run(in $::meta::syntax_match input) "
            "{ uptr invalid = 1uptr / 0uptr; return $::quote { 0u32 }; } "
            "syntax Trap : expression { prefix \"trap\"; match body:paren; expand never_run; } syntax Trap;");
        auto input = Lexer(*source, diagnostics).lex();
        Parser owner(input, diagnostics, execution, 32);
        const auto early_context = owner.syntax_context(input.front().location);
        (void)owner.parse();
        const auto saved_context = owner.syntax_context(input.front().location);
        require(diagnostics.errors() == 0, "bounded parse context fixture failed");
        const auto saved_parse = [&](std::string_view text, SyntaxParseCategory category,
                                     const std::shared_ptr<const SyntaxContext>& context) {
            const auto* saved_source = sources.add("saved-context-input.x", std::string(text));
            return Parser::parse_syntax_tokens(category,
                Lexer(*saved_source, diagnostics).lex(), context, diagnostics);
        };
        require(early_context && saved_context && early_context->parse_environment &&
                saved_context->parse_environment, "parser context has no immutable environment");
        require(!saved_parse("Word", SyntaxParseCategory::Type, early_context),
                "later typedef mutated a saved parser environment");
        require(saved_parse("Word", SyntaxParseCategory::Type, saved_context) != nullptr,
                "saved parser environment lost its alias");
        const auto complete = [&](std::string_view text, std::string_view category) -> Node {
            const auto parsed_category = syntax_parse_category(category);
            if (!parsed_category) return {};
            const auto* fragment_source = sources.add("bounded-parse.x", std::string(text));
            auto fragment_input = Lexer(*fragment_source, diagnostics).lex();
            const auto count = fragment_input.size() - 1;
            auto node = owner.parse_syntax_tokens(*parsed_category, fragment_input);
            if (node) {
                const auto projected = syntax_node_tokens(*node);
                require(projected.size() == count, "bounded parse projected only a prefix");
                for (std::size_t at = 0; at < count; ++at)
                    require(projected[at].kind == fragment_input[at].kind &&
                            projected[at].text == fragment_input[at].text,
                            "bounded parse changed lexical tokens");
            }
            require(diagnostics.errors() == 0, "bounded parse leaked speculative diagnostics");
            return node;
        };
        production(complete("identity<Word>(3u32)", "expr"), P::AssignmentExpression, 1);
        production(complete("{ Word value = 3u32; }", "stmt"), P::Statement, 1);
        production(complete("Word (*)(in Word value) -> \"arbitrary.result\" [[abi(\"custom\")]]", "type"),
                   P::TypeName, 2);
        production(complete("Word value = 3u32;", "declaration"), P::Declaration, 3);
        production(complete("static Word fn(in Word value) -> \"stack.result\"", "function_header"),
                   P::FunctionHeader, 2);
        production(complete("static Word fn(in Word value) -> \"memory.result\";", "function_decl"),
                   P::Declaration, 3);
        production(complete("static Word fn(in Word value) { return value; }", "function_def"),
                   P::FunctionDefinition, 3);
        require(complete("trap ()", "expr") != nullptr,
                "bounded parse tried to execute a nested invocation");
        require(complete("typedef u16 Temporary;", "declaration") != nullptr,
                "bounded typedef recognition failed");
        require(!saved_parse("Temporary", SyntaxParseCategory::Type, saved_context),
                "speculative declaration changed a saved context");
        auto scoped = production(complete("{ typedef u16 Local; { typedef u32 Other; Other nested; } Local value; }",
                                          "stmt"), P::Statement, 1);
        auto scoped_block = child(child(scoped, 0, P::UnattributedStatement, 1),
                                  0, P::CompoundStatement, 5);
        const auto outer_context = scoped_block->children[3]->context;
        auto inner_block = child(child(scoped_block->children[2], 0, P::UnattributedStatement, 1),
                                 0, P::CompoundStatement, 4);
        const auto inner_context = inner_block->children[2]->context;
        require(saved_parse("Local", SyntaxParseCategory::Type, outer_context) &&
                saved_parse("Other", SyntaxParseCategory::Type, inner_context) &&
                !saved_parse("Other", SyntaxParseCategory::Type, outer_context) &&
                !saved_parse("Local", SyntaxParseCategory::Type, scoped->context),
                "public nodes did not preserve their lexical alias scopes");
        require(syntax_node_storage(*scoped) > syntax_node_count(*scoped) * 128,
                "public tree storage omitted context snapshots");
        auto early_trap = saved_parse("trap ()", SyntaxParseCategory::Expression, early_context);
        auto saved_trap = saved_parse("trap ()", SyntaxParseCategory::Expression, saved_context);
        const auto has_extension = [](Node node) {
            std::vector<Node> pending{std::move(node)};
            while (!pending.empty()) {
                auto next = std::move(pending.back()); pending.pop_back();
                if (next->kind == SyntaxNode::Kind::Extension) return true;
                pending.insert(pending.end(), next->children.begin(), next->children.end());
            }
            return false;
        };
        require(early_trap && saved_trap && !has_extension(early_trap) && has_extension(saved_trap),
                "later activation changed a saved context or nested expander executed");
        for (const auto& [text, category] : std::vector<std::pair<std::string_view, std::string_view>>{
                 {"", "expr"}, {"1u32, 2u32", "expr"}, {"1u32;", "expr"},
                 {"Word;", "type"}, {"Word value", "type"}, {"Temporary", "type"},
                 {"; ;", "stmt"}, {"if (1u32) ; else ; ;", "stmt"},
                 {"syntax Trap;", "stmt"}, {"namespace ns {}", "declaration"},
                 {"Word first; Word second;", "declaration"}, {"Word fn();", "function_header"},
                 {"Word fn() {}", "function_header"}, {"Word fn() {}", "function_decl"},
                 {"Word fn();", "function_def"}, {"Word (*pointer)();", "function_decl"},
                 {"Word value;", "function_decl"}, {"1u32", "expression"}})
            require(!complete(text, category), "invalid bounded category/input was accepted");
        require(complete("trap ()", "expr") != nullptr,
                "rejected registration damaged existing syntax activation");
        require(!owner.parse_syntax_tokens(SyntaxParseCategory::None, input),
                "empty parse category was accepted");
        auto missing_end = Lexer(*source, diagnostics).lex();
        missing_end.pop_back();
        require(!owner.parse_syntax_tokens(SyntaxParseCategory::Statement, missing_end),
                "unbounded input without an End fence was accepted");
    }

    {
        std::weak_ptr<SyntaxExecution> executor_lifetime;
        std::shared_ptr<const SyntaxContext> retained_context;
        {
            auto isolated_execution = std::make_shared<SyntaxExecution>(sources, diagnostics, 32,
                no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
            executor_lifetime = isolated_execution;
            const auto* source = sources.add("context-lifetime.x",
                "typedef u16 Word; [[syntax_expander]] static $::meta::tokens expand "
                "(in $::meta::syntax_match input) { return $::quote { 1u32 }; }");
            auto input = Lexer(*source, diagnostics).lex();
            Parser owner(input, diagnostics, isolated_execution, 32);
            (void)owner.parse();
            retained_context = owner.syntax_context(input.front().location);
            require(diagnostics.errors() == 0, "context lifetime fixture failed");
        }
        require(retained_context && retained_context->parse_environment && executor_lifetime.expired(),
                "immutable parser contexts created an executor ownership cycle");
        const auto* fragment = sources.add("expired-context.x", "Word");
        require(!Parser::parse_syntax_tokens(SyntaxParseCategory::Type,
                    Lexer(*fragment, diagnostics).lex(), retained_context, diagnostics),
                "expired compiler context fell back to ambient parser state");
    }

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
    auto left_expression = production(parse("2u32 + 3u32", K::Expr),
                                      P::AssignmentExpression, 1);
    auto right_expression = production(parse("7u32 * 4u32", K::Expr),
                                       P::AssignmentExpression, 1);
    std::string replacement_error;
    auto replaced = syntax_replace_child(*left_expression, 0,
        right_expression->children[0], replacement_error);
    require(replaced && replacement_error.empty(), "compatible syntax child replacement failed");
    auto changed_tokens = syntax_node_tokens(*replaced);
    require(changed_tokens.size() == 3 && changed_tokens[0].text == "7u32" &&
            changed_tokens[1].text == "*" && changed_tokens[2].text == "4u32",
            "syntax child replacement did not preserve the new concrete subtree");
    require(syntax_node_tokens(*left_expression)[0].text == "2u32",
            "syntax child replacement mutated its input");
    require(syntax_node_storage(*replaced, 127) > 127 &&
            syntax_node_storage(*replaced, 127) < syntax_node_storage(*replaced),
            "syntax storage traversal did not stop at its exceeded limit");
    require(!syntax_replace_child(*left_expression, 0, right_expression,
                                  replacement_error) &&
            replacement_error.find("grammar production") != std::string::npos,
            "incompatible syntax production was accepted");
    require(!syntax_replace_child(*left_expression, 1, right_expression->children[0],
                                  replacement_error) &&
            replacement_error.find("index") != std::string::npos,
            "out-of-range syntax replacement was accepted");
    auto opaque = std::make_shared<SyntaxNode>();
    opaque->kind = SyntaxNode::Kind::Macro;
    opaque->slot_production = P::ConditionalExpression;
    require(syntax_replace_child(*left_expression, 0, opaque, replacement_error) != nullptr,
            "opaque syntax node with the matching grammar slot was rejected");
    opaque->slot_production = P::PrimaryExpression;
    require(!syntax_replace_child(*left_expression, 0, opaque, replacement_error),
            "opaque syntax node with the wrong grammar slot was accepted");
    auto addition = descendant(left_expression, P::AdditiveExpression);
    auto subtraction = descendant(parse("8u32 - 1u32", K::Expr), P::AdditiveExpression);
    require(syntax_replace_child(*addition, 1, subtraction->children[1], replacement_error) != nullptr,
            "another operator in the same grammar alternative was rejected");
    auto multiplication = descendant(right_expression, P::MultiplicativeExpression);
    require(!syntax_replace_child(*addition, 1, multiplication->children[1], replacement_error),
            "an operator from another precedence production was accepted");
    auto integer_literal = descendant(left_expression, P::Literal);
    auto string_literal = descendant(parse("\"text\"", K::Expr), P::Literal);
    require(syntax_replace_child(*integer_literal, 0, string_literal->children[0], replacement_error) != nullptr,
            "another literal token kind was rejected");
    auto source_type = descendant(parse("u32", K::Type), P::TypeSpecifier);
    auto record_type_node = descendant(parse("struct NewTag", K::Type), P::StructOrUnionSpecifier);
    require(syntax_replace_child(*source_type, 0, record_type_node, replacement_error) != nullptr,
            "another type-specifier production alternative was rejected");
    auto first_scalar = descendant(source_type, P::ScalarType);
    auto second_scalar = descendant(parse("u16", K::Type), P::ScalarType);
    require(syntax_replace_child(*first_scalar, 0, second_scalar->children[0], replacement_error) != nullptr,
            "another scalar keyword was rejected");
    auto limited = EvaluationLimits{};
    limited.depth = 1;
    require(!syntax_replace_child(*left_expression, 0, right_expression->children[0],
                                  replacement_error, limited) &&
            replacement_error.find("depth budget") != std::string::npos,
            "syntax replacement did not enforce its depth budget");
    limited = EvaluationLimits{};
    limited.steps = 4;
    std::uint64_t validation_work{};
    require(!syntax_replace_child(*left_expression, 0, right_expression->children[0],
                                  replacement_error, limited, &validation_work) &&
            replacement_error.find("work budget") != std::string::npos && validation_work == 4,
            "syntax replacement did not enforce its work budget");
    // Exercise production alternatives through the same structural validator,
    // without relying on private AST types or semantic constant folding.
    for (const auto text : {"a = b += c ? d : e", "a || b && c | d ^ e & f == g < h << i + j * k",
                            "++value + -other + sizeof(value)", "(u32)value", "fn(a, b)[2u32].field++",
                            "\"first\" \"second\"", "callee::<u32, 3u32>(4u32)"})
        (void)parse(text, K::Expr);
    for (const auto text : {"u32 (*)(in u16 value)", "u32 *[3u32]", "u32 (*)[3u32]"})
        (void)parse(text, K::Type);
    for (const auto text : {"if (a) return b; else return c;", "switch (a) { case 1u32: break; default: return a; }",
                            "while (a) { continue; }", "do { a -= 1u32; } while (a);", "for (;;) break;",
                            "for (u32 i = 0u32; i < 2u32; ++i) continue;", "label point: goto point;",
                            "{ using ns; ; }", "$::static_assert(1u32, \"ok\");"})
        (void)parse(text, K::Statement);
    (void)parse("struct Bits { u32 : 0u32; u32 value : 4u32; };", K::Declaration);
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
