// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <unordered_set>
#include <utility>

namespace cross {

std::optional<SyntaxParseCategory> syntax_parse_category(std::string_view name) {
    using C = SyntaxParseCategory;
    static constexpr std::pair<std::string_view, C> categories[] = {
        {"expr", C::Expression}, {"stmt", C::Statement}, {"type", C::Type},
        {"declaration", C::Declaration}, {"function_header", C::FunctionHeader},
        {"function_decl", C::FunctionDeclaration}, {"function_def", C::FunctionDefinition},
    };
    for (const auto& [spelling, category] : categories)
        if (spelling == name) return category;
    return {};
}

std::uint64_t syntax_context_storage(const SyntaxContext& context) {
    std::uint64_t size = 128 + context.name_space.size();
    for (const auto& entry : context.imports) size += 32 + entry.size();
    for (const auto& binding : context.syntax_bindings) size += 48 + binding.prefix.size();
    if (context.parse_environment) size += syntax_environment_storage(*context.parse_environment);
    return size;
}

std::string_view syntax_production_name(SyntaxProduction production) {
    static constexpr std::string_view names[] = {
        "", "declaration", "function_header", "function_definition", "type_name",
        "declaration_specifiers", "declaration_specifier", "type_qualifier", "type_specifier",
        "scalar_type", "struct_or_union_specifier", "enum_specifier", "typedef_name", "target_scalar_builtin_name",
        "declarator", "abstract_declarator", "direct_declarator", "pointer_part",
        "function_suffix", "parameter_list", "parameter_declaration", "parameter_mode",
        "location", "object_location", "result_location", "generic_parameter_list", "generic_parameter",
        "init_declarator_list", "init_declarator", "member_declaration", "member_declarator", "enumerator",
        "attribute_specifier", "attribute", "attribute_name", "balanced_token_sequence",
        "balanced_token_tree", "balanced_tokens", "qualified_name", "namespace_name",
        "statement", "unattributed_statement", "compound_statement", "using_declaration",
        "labeled_statement", "selection_statement", "iteration_statement", "jump_statement",
        "expression_statement", "static_assert_declaration", "for_initializer",
        "declaration_without_final_semicolon", "array_suffix",
        "initializer", "initializer_entry", "designator",
        "expression", "constant_expression", "assignment_expression", "assignment_operator", "conditional_expression",
        "logical_or_expression", "logical_and_expression", "inclusive_or_expression",
        "exclusive_or_expression", "and_expression", "equality_expression",
        "relational_expression", "shift_expression", "additive_expression",
        "multiplicative_expression", "cast_expression", "unary_expression",
        "postfix_expression", "primary_expression", "argument_list", "generic_arguments", "generic_argument",
        "builtin_name", "literal", "embed_expression", "quote_expression"};
    static_assert(std::size(names) == static_cast<std::size_t>(SyntaxProduction::Count));
    const auto index = static_cast<std::size_t>(production);
    return index < std::size(names) ? names[index] : std::string_view{};
}

TokenSequence syntax_node_tokens(const SyntaxNode& node) {
    TokenSequence result;
    const auto append = [&](const MetaToken& token) {
        if (!result.empty() && token.split_source &&
            result.back().split_source == token.split_source &&
            result.back().origin.context == token.origin.context &&
            result.back().split_offset + result.back().text.size() == token.split_offset) {
            auto& prior = result.back();
            prior.text += token.text;
            if (prior.split_offset == 0 && prior.text.size() == prior.split_source->spelling.size()) {
                prior.kind = prior.split_source->kind;
                prior.text = prior.split_source->spelling;
                prior.split_source.reset();
            }
        } else result.push_back(token);
    };
    std::vector<const SyntaxNode*> pending{&node};
    while (!pending.empty()) {
        const auto* next = pending.back();
        pending.pop_back();
        if (next->kind == SyntaxNode::Kind::Token ||
            next->kind == SyntaxNode::Kind::Extension ||
            next->kind == SyntaxNode::Kind::Macro ||
            next->kind == SyntaxNode::Kind::Deferred) {
            for (const auto& token : next->tokens) append(token);
        } else {
            for (auto at = next->children.rbegin(); at != next->children.rend(); ++at)
                pending.push_back(at->get());
        }
    }
    return result;
}

std::size_t syntax_node_count(const SyntaxNode& node) {
    std::size_t count{};
    std::vector<const SyntaxNode*> pending{&node};
    while (!pending.empty()) {
        const auto* next = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : next->children) pending.push_back(child.get());
    }
    return count;
}

std::uint64_t syntax_node_storage(const SyntaxNode& node, std::uint64_t stop_after) {
    std::uint64_t size{};
    const auto add = [&](std::uint64_t amount) {
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        size = amount > maximum - size ? maximum : size + amount;
    };
    std::unordered_set<const SyntaxContext*> contexts;
    std::unordered_set<const SyntaxParseEnvironment*> environments;
    const auto context = [&](const std::shared_ptr<const SyntaxContext>& value) {
        if (!value || !contexts.insert(value.get()).second) return;
        auto amount = syntax_context_storage(*value);
        if (value->parse_environment && !environments.insert(value->parse_environment.get()).second)
            amount -= syntax_environment_storage(*value->parse_environment);
        add(amount);
    };
    const auto tokens = [&](const TokenSequence& sequence) {
        for (const auto& token : sequence) {
            if (size > stop_after) break;
            add(meta_token_storage_bytes); add(token.text.size());
            context(token.origin.context);
            if (token.split_source) { add(32); add(token.split_source->spelling.size()); }
        }
    };
    std::vector<const SyntaxNode*> nodes{&node};
    std::vector<const SyntaxMatchValue*> matches;
    while ((!nodes.empty() || !matches.empty()) && size <= stop_after) {
        if (!nodes.empty()) {
            const auto* next = nodes.back();
            nodes.pop_back();
            add(128);
            context(next->context);
            tokens(next->tokens);
            for (const auto& child : next->children) {
                if (size > stop_after) break;
                add(16); nodes.push_back(child.get());
            }
            if (next->match) matches.push_back(next->match.get());
        } else {
            const auto* next = matches.back();
            matches.pop_back();
            add(syntax_match_storage_bytes);
            context(next->context);
            tokens(next->input);
            if (next->variant) { add(32); add(next->variant->size()); }
            for (const auto& label : next->variant_labels) {
                if (size > stop_after) break;
                add(32); add(label.size());
            }
            for (const auto& field : next->fields) {
                if (size > stop_after) break;
                add(syntax_field_storage_bytes); add(field.name.size()); tokens(field.tokens);
                if (field.node) nodes.push_back(field.node.get());
                for (const auto& child : field.records) {
                    if (size > stop_after) break;
                    add(16); matches.push_back(child.get());
                }
            }
        }
    }
    return size;
}

namespace {

// Public grammar validation uses typed production references. References match
// a child's public root, not its spelling or any private AST representation.
struct TreeRule {
    enum class Kind { Reject, Terminal, TokenKind, Identifier, RawToken,
                      Reference, Group, Opaque, Sequence, Choice, Optional, Repeat };
    Kind kind{Kind::Reject};
    std::string_view terminal;
    TokenKind token_kind{TokenKind::Invalid};
    SyntaxProduction production{SyntaxProduction::None};
    std::vector<TreeRule> rules;
    explicit TreeRule(Kind kind = Kind::Reject) : kind(kind) {}
};

TreeRule terminal(std::string_view text) {
    TreeRule rule(TreeRule::Kind::Terminal); rule.terminal = text; return rule;
}
TreeRule token_kind(TokenKind kind) {
    TreeRule rule(TreeRule::Kind::TokenKind); rule.token_kind = kind; return rule;
}
TreeRule reference(SyntaxProduction production) {
    TreeRule rule(TreeRule::Kind::Reference); rule.production = production; return rule;
}
TreeRule sequence(std::initializer_list<TreeRule> rules) {
    TreeRule rule(TreeRule::Kind::Sequence); rule.rules = rules; return rule;
}
TreeRule choice(std::initializer_list<TreeRule> rules) {
    TreeRule rule(TreeRule::Kind::Choice); rule.rules = rules; return rule;
}
TreeRule optional(TreeRule nested) {
    TreeRule rule(TreeRule::Kind::Optional); rule.rules.push_back(std::move(nested)); return rule;
}
TreeRule repeated(TreeRule nested) {
    TreeRule rule(TreeRule::Kind::Repeat); rule.rules.push_back(std::move(nested)); return rule;
}
TreeRule terminals(std::initializer_list<std::string_view> texts) {
    TreeRule rule(TreeRule::Kind::Choice);
    for (const auto text : texts) rule.rules.push_back(terminal(text));
    return rule;
}
TreeRule opaque(SyntaxProduction production) {
    TreeRule rule(TreeRule::Kind::Opaque); rule.production = production; return rule;
}

const auto& public_tree_rules() {
    using P = SyntaxProduction;
    using K = TreeRule::Kind;
    static const auto rules = [] {
        std::array<TreeRule, static_cast<std::size_t>(P::Count)> result;
        const auto set = [&](P production, TreeRule rule) {
            result[static_cast<std::size_t>(production)] = std::move(rule);
        };
        const auto id = TreeRule(K::Identifier);
        const auto string = token_kind(TokenKind::String);
        const auto attrs = repeated(reference(P::AttributeSpecifier));
        const auto suffix = choice({reference(P::ArraySuffix), reference(P::FunctionSuffix)});
        const auto qualified = sequence({id, repeated(sequence({terminal("::"), id}))});
        const auto declaration = [&](bool semicolon) {
            auto rule = sequence({attrs, reference(P::DeclarationSpecifiers),
                optional(reference(P::InitDeclaratorList)), attrs});
            if (semicolon) rule.rules.push_back(terminal(";"));
            return rule;
        };
        set(P::Declaration, declaration(true));
        set(P::DeclarationWithoutFinalSemicolon, declaration(false));
        set(P::FunctionHeader, sequence({attrs, reference(P::DeclarationSpecifiers),
            reference(P::Declarator), attrs}));
        set(P::FunctionDefinition, sequence({attrs, reference(P::DeclarationSpecifiers),
            reference(P::Declarator), attrs, reference(P::CompoundStatement)}));
        set(P::TypeName, sequence({reference(P::DeclarationSpecifiers),
            optional(reference(P::AbstractDeclarator))}));
        set(P::DeclarationSpecifiers, sequence({reference(P::DeclarationSpecifier),
            repeated(reference(P::DeclarationSpecifier))}));
        set(P::DeclarationSpecifier, choice({terminals({"typedef", "static", "global", "register", "stack", "inline"}),
            reference(P::TypeQualifier), reference(P::TypeSpecifier), reference(P::AttributeSpecifier)}));
        set(P::TypeQualifier, terminals({"const", "volatile", "restrict"}));
        set(P::TypeSpecifier, choice({reference(P::ScalarType), reference(P::StructOrUnionSpecifier),
            reference(P::EnumSpecifier), reference(P::TypedefName), reference(P::TargetScalarBuiltinName)}));
        set(P::ScalarType, terminals({"void", "bool", "i8", "i16", "i32", "i64", "i128", "iptr",
            "u8", "u16", "u32", "u64", "u128", "uptr", "f32", "f64", "f80", "f128", "fptr", "label"}));
        set(P::StructOrUnionSpecifier, sequence({terminals({"struct", "union"}),
            optional(reference(P::QualifiedName)), optional(sequence({terminal("{"),
                repeated(reference(P::MemberDeclaration)), terminal("}")}))}));
        set(P::EnumSpecifier, sequence({terminal("enum"), optional(reference(P::QualifiedName)), attrs,
            optional(sequence({terminal("{"), reference(P::Enumerator),
                repeated(sequence({terminal(","), reference(P::Enumerator)})),
                optional(terminal(",")), terminal("}")}))}));
        set(P::TypedefName, reference(P::QualifiedName));
        set(P::TargetScalarBuiltinName, reference(P::BuiltinName));
        set(P::Declarator, sequence({repeated(reference(P::PointerPart)), reference(P::DirectDeclarator)}));
        set(P::AbstractDeclarator, sequence({repeated(reference(P::PointerPart)), optional(choice({
            sequence({terminal("("), reference(P::AbstractDeclarator), terminal(")"), repeated(suffix)}),
            sequence({suffix, repeated(suffix)})}))}));
        set(P::DirectDeclarator, sequence({choice({sequence({reference(P::QualifiedName),
                optional(reference(P::GenericParameterList))}),
            sequence({terminal("("), reference(P::Declarator), terminal(")")})}), repeated(suffix)}));
        set(P::PointerPart, sequence({terminal("*"), repeated(choice({reference(P::TypeQualifier),
            reference(P::AttributeSpecifier)}))}));
        set(P::ArraySuffix, sequence({terminal("["), optional(reference(P::AssignmentExpression)), terminal("]")}));
        set(P::FunctionSuffix, sequence({terminal("("), reference(P::ParameterList), terminal(")"),
            optional(reference(P::ResultLocation)), attrs}));
        set(P::ParameterList, choice({sequence({}), terminal("void"),
            sequence({reference(P::ParameterDeclaration),
                repeated(sequence({terminal(","), reference(P::ParameterDeclaration)})),
                optional(sequence({terminal(","), terminal("...")}))})}));
        set(P::ParameterDeclaration, sequence({attrs, optional(reference(P::ParameterMode)),
            reference(P::DeclarationSpecifiers), optional(choice({reference(P::Declarator),
                reference(P::AbstractDeclarator)})), optional(reference(P::Location)), attrs}));
        set(P::ParameterMode, terminals({"in", "out", "inout"}));
        set(P::Location, string); set(P::ObjectLocation, string);
        set(P::ResultLocation, sequence({terminal("->"), string}));
        set(P::GenericParameterList, sequence({terminal("<"), reference(P::GenericParameter),
            repeated(sequence({terminal(","), reference(P::GenericParameter)})), terminal(">")}));
        set(P::GenericParameter, choice({id, sequence({reference(P::TypeName), id})}));
        set(P::InitDeclaratorList, sequence({reference(P::InitDeclarator),
            repeated(sequence({terminal(","), reference(P::InitDeclarator)}))}));
        set(P::InitDeclarator, sequence({reference(P::Declarator), optional(reference(P::ObjectLocation)),
            attrs, optional(sequence({terminal("="), reference(P::Initializer)}))}));
        set(P::MemberDeclaration, sequence({attrs, reference(P::DeclarationSpecifiers),
            optional(sequence({reference(P::MemberDeclarator),
                repeated(sequence({terminal(","), reference(P::MemberDeclarator)}))})), terminal(";")}));
        set(P::MemberDeclarator, sequence({optional(reference(P::Declarator)),
            optional(sequence({terminal(":"), reference(P::ConstantExpression)})), attrs}));
        set(P::Enumerator, sequence({id, optional(sequence({terminal("="), reference(P::ConstantExpression)}))}));
        set(P::AttributeSpecifier, sequence({terminal("[["), reference(P::Attribute),
            repeated(sequence({terminal(","), reference(P::Attribute)})), terminal("]]")}));
        set(P::Attribute, sequence({reference(P::AttributeName), optional(sequence({terminal("("),
            optional(reference(P::BalancedTokenSequence)), terminal(")")}))}));
        set(P::AttributeName, qualified); set(P::QualifiedName, qualified); set(P::NamespaceName, qualified);
        const auto raw = TreeRule(K::RawToken);
        set(P::BalancedTokenSequence, repeated(choice({raw, reference(P::BalancedTokenTree)})));
        set(P::BalancedTokens, repeated(choice({raw, reference(P::BalancedTokenTree)})));
        set(P::BalancedTokenTree, choice({
            sequence({terminal("("), reference(P::BalancedTokens), terminal(")")}),
            sequence({terminal("["), reference(P::BalancedTokens), terminal("]")}),
            sequence({terminal("[["), reference(P::BalancedTokens), terminal("]]")}),
            sequence({terminal("{"), reference(P::BalancedTokens), terminal("}")})}));
        set(P::Statement, sequence({attrs, reference(P::UnattributedStatement)}));
        set(P::UnattributedStatement, choice({reference(P::LabeledStatement), reference(P::CompoundStatement),
            reference(P::ExpressionStatement), reference(P::SelectionStatement), reference(P::IterationStatement),
            reference(P::JumpStatement), reference(P::Declaration), reference(P::StaticAssertDeclaration),
            opaque(P::UnattributedStatement)}));
        set(P::CompoundStatement, sequence({terminal("{"), repeated(choice({reference(P::Statement),
            reference(P::UsingDeclaration)})), terminal("}")}));
        set(P::UsingDeclaration, sequence({terminal("using"), reference(P::NamespaceName), terminal(";")}));
        set(P::LabeledStatement, choice({sequence({id, terminal(":"), reference(P::Statement)}),
            sequence({terminal("label"), id, terminal(":"), reference(P::Statement)}),
            sequence({terminal("global"), terminal("label"), id, terminal(":"), reference(P::Statement)}),
            sequence({terminal("case"), reference(P::ConstantExpression), terminal(":"), reference(P::Statement)}),
            sequence({terminal("default"), terminal(":"), reference(P::Statement)})}));
        set(P::SelectionStatement, choice({sequence({terminal("if"), terminal("("), reference(P::Expression),
                terminal(")"), reference(P::Statement), optional(sequence({terminal("else"), reference(P::Statement)}))}),
            sequence({terminal("switch"), terminal("("), reference(P::Expression), terminal(")"), reference(P::Statement)})}));
        set(P::IterationStatement, choice({sequence({terminal("while"), terminal("("), reference(P::Expression),
                terminal(")"), reference(P::Statement)}),
            sequence({terminal("do"), reference(P::Statement), terminal("while"), terminal("("),
                reference(P::Expression), terminal(")"), terminal(";")}),
            sequence({terminal("for"), terminal("("), reference(P::ForInitializer), terminal(";"),
                optional(reference(P::Expression)), terminal(";"), optional(reference(P::Expression)),
                terminal(")"), reference(P::Statement)})}));
        set(P::JumpStatement, choice({sequence({terminals({"break", "continue"}), terminal(";")}),
            sequence({terminal("return"), optional(reference(P::Expression)), terminal(";")}),
            sequence({terminal("goto"), reference(P::AssignmentExpression), terminal(";")})}));
        set(P::ExpressionStatement, sequence({optional(reference(P::Expression)), terminal(";")}));
        set(P::StaticAssertDeclaration, sequence({terminal("$::static_assert"), terminal("("),
            reference(P::ConstantExpression), terminal(","), string, terminal(")"), terminal(";")}));
        set(P::ForInitializer, choice({sequence({}), reference(P::Expression), reference(P::DeclarationWithoutFinalSemicolon)}));
        set(P::Initializer, choice({reference(P::AssignmentExpression), sequence({terminal("{"),
            optional(sequence({reference(P::InitializerEntry),
                repeated(sequence({terminal(","), reference(P::InitializerEntry)})), optional(terminal(","))})), terminal("}")})}));
        set(P::InitializerEntry, sequence({repeated(reference(P::Designator)), optional(terminal("=")), reference(P::Initializer)}));
        set(P::Designator, choice({sequence({terminal("."), id}),
            sequence({terminal("["), reference(P::ConstantExpression), terminal("]")})}));
        set(P::Expression, reference(P::AssignmentExpression));
        set(P::ConstantExpression, reference(P::ConditionalExpression));
        set(P::AssignmentExpression, sequence({reference(P::ConditionalExpression),
            optional(sequence({reference(P::AssignmentOperator), reference(P::AssignmentExpression)}))}));
        set(P::AssignmentOperator, terminals({"=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "^=", "|="}));
        set(P::ConditionalExpression, sequence({reference(P::LogicalOrExpression),
            optional(sequence({terminal("?"), reference(P::Expression), terminal(":"), reference(P::ConditionalExpression)}))}));
        const auto binary = [&](P parent, P operand, std::initializer_list<std::string_view> operators) {
            set(parent, sequence({reference(operand), repeated(sequence({terminals(operators), reference(operand)}))}));
        };
        binary(P::LogicalOrExpression, P::LogicalAndExpression, {"||"});
        binary(P::LogicalAndExpression, P::InclusiveOrExpression, {"&&"});
        binary(P::InclusiveOrExpression, P::ExclusiveOrExpression, {"|"});
        binary(P::ExclusiveOrExpression, P::AndExpression, {"^"});
        binary(P::AndExpression, P::EqualityExpression, {"&"});
        binary(P::EqualityExpression, P::RelationalExpression, {"==", "!="});
        binary(P::RelationalExpression, P::ShiftExpression, {"<", "<=", ">", ">="});
        binary(P::ShiftExpression, P::AdditiveExpression, {"<<", ">>"});
        binary(P::AdditiveExpression, P::MultiplicativeExpression, {"+", "-"});
        binary(P::MultiplicativeExpression, P::CastExpression, {"*", "/", "%"});
        set(P::CastExpression, choice({reference(P::UnaryExpression), sequence({terminal("("),
            reference(P::TypeName), terminal(")"), reference(P::CastExpression)})}));
        set(P::UnaryExpression, choice({reference(P::PostfixExpression), sequence({
                terminals({"++", "--", "&", "*", "+", "-", "~", "!", "sizeof"}), reference(P::UnaryExpression)}),
            sequence({terminal("sizeof"), terminal("("), reference(P::TypeName), terminal(")")})}));
        set(P::PostfixExpression, sequence({reference(P::PrimaryExpression), repeated(choice({
            sequence({terminal("["), reference(P::Expression), terminal("]")}),
            sequence({terminal("("), reference(P::ArgumentList), terminal(")")}),
            sequence({terminals({".", "->"}), id}), terminals({"++", "--"}), reference(P::GenericArguments)}))}));
        set(P::PrimaryExpression, choice({reference(P::QualifiedName), reference(P::BuiltinName), reference(P::Literal),
            sequence({terminal("("), reference(P::Expression), terminal(")")}),
            reference(P::EmbedExpression), reference(P::QuoteExpression), opaque(P::PrimaryExpression)}));
        set(P::ArgumentList, optional(sequence({reference(P::AssignmentExpression),
            repeated(sequence({terminal(","), reference(P::AssignmentExpression)}))})));
        set(P::GenericArguments, sequence({optional(terminal("::")), terminal("<"), reference(P::GenericArgument),
            repeated(sequence({terminal(","), reference(P::GenericArgument)})), terminal(">")}));
        set(P::GenericArgument, choice({reference(P::TypeName), reference(P::ConstantExpression)}));
        set(P::BuiltinName, token_kind(TokenKind::BuiltinName));
        set(P::Literal, choice({token_kind(TokenKind::Integer), token_kind(TokenKind::Floating),
            token_kind(TokenKind::Character), sequence({string, repeated(string)})}));
        set(P::EmbedExpression, sequence({terminal("$::embed"), terminal("("), string, terminal(")")}));
        set(P::QuoteExpression, sequence({terminal("$::quote"), terminal("{"), reference(P::BalancedTokens), terminal("}")}));
        return result;
    }();
    return rules;
}

bool opaque_kind(SyntaxNode::Kind kind) {
    return kind == SyntaxNode::Kind::Extension || kind == SyntaxNode::Kind::Macro ||
           kind == SyntaxNode::Kind::Deferred;
}

bool delimiter(std::string_view text) {
    return text == "(" || text == ")" || text == "[" || text == "]" ||
           text == "[[" || text == "]]" || text == "{" || text == "}";
}

bool deferred_slot(SyntaxParseCategory category, SyntaxProduction slot) {
    using C = SyntaxParseCategory;
    using P = SyntaxProduction;
    switch (category) {
    case C::Expression: return slot == P::AssignmentExpression;
    case C::Statement: return slot == P::Statement || slot == P::CompoundStatement;
    case C::Type: return slot == P::TypeName;
    case C::Declaration: case C::FunctionDeclaration: return slot == P::Declaration;
    case C::FunctionHeader: return slot == P::FunctionHeader;
    case C::FunctionDefinition: return slot == P::FunctionDefinition;
    case C::None: return false;
    }
    return false;
}

struct TreeValidator {
    const EvaluationLimits& limits;
    std::uint64_t work{};
    bool exhausted{};
    SyntaxTreeValidationError failure{SyntaxTreeValidationError::Shape};
    using Positions = std::vector<std::size_t>;
    bool step() {
        if (work >= limits.steps) { exhausted = true; return false; }
        ++work;
        return true;
    }
    static void merge(Positions& destination, const Positions& source) {
        destination.insert(destination.end(), source.begin(), source.end());
        std::sort(destination.begin(), destination.end());
        destination.erase(std::unique(destination.begin(), destination.end()), destination.end());
    }
    Positions match(const TreeRule& rule, const std::vector<std::shared_ptr<const SyntaxNode>>& children,
                    std::size_t at) {
        if (!step()) return {};
        using K = TreeRule::Kind;
        if (rule.kind == K::Sequence) {
            Positions positions{at};
            for (const auto& nested : rule.rules) {
                Positions next;
                for (const auto position : positions) merge(next, match(nested, children, position));
                positions = std::move(next);
                if (positions.empty()) break;
            }
            return positions;
        }
        if (rule.kind == K::Choice || rule.kind == K::Optional) {
            Positions positions;
            if (rule.kind == K::Optional) positions.push_back(at);
            for (const auto& nested : rule.rules) merge(positions, match(nested, children, at));
            return positions;
        }
        if (rule.kind == K::Repeat) {
            Positions positions{at}, pending{at};
            std::vector<bool> visited(children.size() + 1);
            visited[at] = true;
            while (!pending.empty() && !exhausted) {
                const auto position = pending.back(); pending.pop_back();
                for (const auto next : match(rule.rules.front(), children, position)) {
                    if (!visited[next]) { visited[next] = true; positions.push_back(next); pending.push_back(next); }
                }
            }
            return positions;
        }
        if (at >= children.size() || !children[at]) return {};
        const auto& child = *children[at];
        const MetaToken* token = child.kind == SyntaxNode::Kind::Token && child.tokens.size() == 1
            ? &child.tokens.front() : nullptr;
        bool matched{};
        switch (rule.kind) {
        case K::Terminal: matched = token && token->text == rule.terminal; break;
        case K::TokenKind: matched = token && token->kind == rule.token_kind; break;
        case K::Identifier: matched = token && token->kind == TokenKind::Identifier &&
            !is_reserved_identifier(token->text); break;
        case K::RawToken: matched = token && !delimiter(token->text); break;
        case K::Reference: matched = child.kind == SyntaxNode::Kind::Core
            ? child.production == rule.production : opaque_kind(child.kind) && child.slot_production == rule.production;
            break;
        case K::Group: matched = child.kind == SyntaxNode::Kind::Group; break;
        case K::Opaque: matched = opaque_kind(child.kind) && child.slot_production == rule.production; break;
        default: break;
        }
        return matched ? Positions{at + 1} : Positions{};
    }
    bool node(const SyntaxNode& root, std::string& error) {
        std::vector<std::pair<const SyntaxNode*, unsigned>> pending{{&root, 1}};
        while (!pending.empty()) {
            const auto [node, depth] = pending.back(); pending.pop_back();
            if (!step()) break;
            if (depth > limits.depth) {
                failure = SyntaxTreeValidationError::DepthLimit;
                error = "public syntax tree exceeds validation depth budget";
                return false;
            }
            if (node->kind == SyntaxNode::Kind::Token || opaque_kind(node->kind)) {
                if (node->kind == SyntaxNode::Kind::Deferred) {
                    if (!deferred_slot(node->deferred_category, node->slot_production) ||
                        node->production != SyntaxProduction::None || !node->context ||
                        node->tokens.empty()) {
                        error = "deferred syntax node requires a compatible category/grammar slot, context, and bounded input";
                        return false;
                    }
                    std::vector<std::string_view> closers;
                    for (const auto& token : node->tokens) {
                        if (!step()) break;
                        const auto text = std::string_view(token.text);
                        if (token.kind == TokenKind::Invalid || token.kind == TokenKind::End) {
                            error = "deferred syntax input contains an invalid or boundary token";
                            return false;
                        }
                        if (text == "(" || text == "[" || text == "[[" || text == "{") {
                            if (closers.size() >= limits.depth) {
                                failure = SyntaxTreeValidationError::DepthLimit;
                                error = "deferred syntax input exceeds delimiter depth budget";
                                return false;
                            }
                            closers.push_back(text == "(" ? ")" : text == "[" ? "]" :
                                              text == "[[" ? "]]" : "}");
                        } else if (text == ")" || text == "]" || text == "]]" || text == "}") {
                            if (closers.empty() || closers.back() != text) {
                                error = "deferred syntax input has an unmatched delimiter";
                                return false;
                            }
                            closers.pop_back();
                        }
                    }
                    if (exhausted) break;
                    if (!closers.empty()) {
                        error = "deferred syntax input has an unterminated delimiter group";
                        return false;
                    }
                }
                if (!node->children.empty() || (node->kind == SyntaxNode::Kind::Token &&
                    (node->tokens.size() != 1 || node->tokens.front().kind == TokenKind::Invalid ||
                     node->tokens.front().kind == TokenKind::End))) {
                    error = "invalid opaque or token syntax node shape";
                    return false;
                }
                continue;
            }
            TreeRule group_rule;
            const TreeRule* rule{};
            if (node->kind == SyntaxNode::Kind::Core) {
                const auto index = static_cast<std::size_t>(node->production);
                if (index >= public_tree_rules().size()) {
                    error = "unknown public syntax production";
                    return false;
                }
                rule = &public_tree_rules()[index];
            } else if (node->kind == SyntaxNode::Kind::Group) {
                const auto raw = TreeRule(TreeRule::Kind::RawToken);
                const auto group = TreeRule(TreeRule::Kind::Group);
                group_rule = choice({
                    sequence({terminal("("), repeated(choice({raw, group})), terminal(")")}),
                    sequence({terminal("["), repeated(choice({raw, group})), terminal("]")}),
                    sequence({terminal("[["), repeated(choice({raw, group})), terminal("]]")}),
                    sequence({terminal("{"), repeated(choice({raw, group})), terminal("}")})});
                rule = &group_rule;
            }
            if (!rule || !node->tokens.empty()) {
                error = "invalid public syntax tree node representation";
                return false;
            }
            const auto positions = match(*rule, node->children, 0);
            if (exhausted) break;
            if (std::find(positions.begin(), positions.end(), node->children.size()) == positions.end()) {
                error = "syntax replacement changes the grammar production shape of '" +
                    std::string(node->kind == SyntaxNode::Kind::Group ? "group" : syntax_production_name(node->production)) + "'";
                return false;
            }
            for (const auto& child : node->children) {
                if (!child) { error = "syntax tree contains a null child"; return false; }
                pending.emplace_back(child.get(), depth + 1);
            }
        }
        if (exhausted) {
            failure = SyntaxTreeValidationError::WorkLimit;
            error = "public syntax tree exceeds validation work budget";
            return false;
        }
        error.clear();
        failure = SyntaxTreeValidationError::None;
        return true;
    }
};

} // namespace

bool syntax_validate_node(const SyntaxNode& node, std::string& error,
    EvaluationLimits limits, std::uint64_t* work, SyntaxTreeValidationError* failure) {
    TreeValidator validator{limits};
    const auto valid = validator.node(node, error);
    if (work) *work = validator.work;
    if (failure) *failure = validator.failure;
    return valid;
}

std::shared_ptr<const SyntaxNode> syntax_replace_child(
    const SyntaxNode& parent, std::size_t index,
    std::shared_ptr<const SyntaxNode> replacement, std::string& error,
    EvaluationLimits limits, std::uint64_t* work) {
    if (work) *work = 0;
    if (index >= parent.children.size()) {
        error = "syntax child index is out of range";
        return {};
    }
    if (!replacement) {
        error = "syntax replacement requires a node";
        return {};
    }
    auto result = std::make_shared<SyntaxNode>(parent);
    result->children[index] = std::move(replacement);
    if (!syntax_validate_node(*result, error, limits, work)) return {};
    return result;
}

} // namespace cross
