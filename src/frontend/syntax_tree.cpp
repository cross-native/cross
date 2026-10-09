// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"
#include "frontend/token_tree.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

namespace cross {

namespace detail {
class SyntaxRelease {
public:
    template<class Value>
    static void release(Value& value) noexcept {
        OwnerRelease::run([&](OwnerRelease& release) { detach(value, release); });
    }
private:
    static void detach(TokenSequence& tokens, OwnerRelease& release) noexcept {
        for (auto& token : tokens) release.take(token.splice);
        // Drop provenance while the drain is active too: a retained context's
        // private type/AST metadata can itself own public node handles.
        tokens.clear();
    }
    static void detach(SyntaxNode& node, OwnerRelease& release) noexcept {
        for (auto& child : node.children) release.take(child);
        release.take(node.match);
        detach(node.tokens, release);
        node.context.reset();
    }
    static void detach(SyntaxMatchValue& match, OwnerRelease& release) noexcept {
        detach(match.input, release);
        for (auto& field : match.fields) {
            release.take(field.node);
            for (auto& record : field.records) release.take(record);
            detach(field.tokens, release);
        }
        match.context.reset();
    }
};
} // namespace detail

SyntaxNode::~SyntaxNode() { detail::SyntaxRelease::release(*this); }
SyntaxMatchValue::~SyntaxMatchValue() { detail::SyntaxRelease::release(*this); }

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
    size += 40 * context.import_declarations.size();
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
        "builtin_name", "literal", "embed_expression", "quote_expression",
        "global_label_declaration", "qualified_label_name", "qualified_function_name"};
    static_assert(std::size(names) == static_cast<std::size_t>(SyntaxProduction::Count));
    const auto index = static_cast<std::size_t>(production);
    return index < std::size(names) ? names[index] : std::string_view{};
}

bool syntax_expression_node(const SyntaxNode& node) {
    auto production = node.kind == SyntaxNode::Kind::Core
        ? node.production : node.slot_production;
    if (node.kind != SyntaxNode::Kind::Core &&
        node.kind != SyntaxNode::Kind::Extension &&
        node.kind != SyntaxNode::Kind::Macro &&
        node.kind != SyntaxNode::Kind::Deferred) return false;
    // Category compatibility follows complete expression productions, not
    // enum ordering: assignment_operator lives among expression productions
    // but is not itself an expression. Conversely, qualified_name is a valid
    // primary-expression alternative despite being shared with declarations.
    using P = SyntaxProduction;
    switch (production) {
    case P::Expression: case P::ConstantExpression: case P::AssignmentExpression:
    case P::ConditionalExpression: case P::LogicalOrExpression: case P::LogicalAndExpression:
    case P::InclusiveOrExpression: case P::ExclusiveOrExpression: case P::AndExpression:
    case P::EqualityExpression: case P::RelationalExpression: case P::ShiftExpression:
    case P::AdditiveExpression: case P::MultiplicativeExpression: case P::CastExpression:
    case P::UnaryExpression: case P::PostfixExpression: case P::PrimaryExpression:
    case P::QualifiedName: case P::BuiltinName: case P::Literal:
    case P::EmbedExpression: case P::QuoteExpression:
        return true;
    default:
        return false;
    }
}

bool syntax_statement_node(const SyntaxNode& node) {
    return syntax_compound_node(node) || (node.kind == SyntaxNode::Kind::Core &&
            (node.production == SyntaxProduction::Statement ||
             node.production == SyntaxProduction::UnattributedStatement)) ||
        ((node.kind == SyntaxNode::Kind::Deferred ||
          node.kind == SyntaxNode::Kind::Extension ||
          node.kind == SyntaxNode::Kind::Macro) &&
         (node.slot_production == SyntaxProduction::Statement ||
          node.slot_production == SyntaxProduction::UnattributedStatement));
}

bool syntax_compound_node(const SyntaxNode& node) {
    return (node.kind == SyntaxNode::Kind::Core &&
            node.production == SyntaxProduction::CompoundStatement) ||
        (node.kind == SyntaxNode::Kind::Deferred &&
         node.slot_production == SyntaxProduction::CompoundStatement &&
         node.deferred_category == SyntaxParseCategory::Statement);
}

bool syntax_declaration_node(const SyntaxNode& node) {
    return (node.kind == SyntaxNode::Kind::Core &&
            (node.production == SyntaxProduction::Declaration ||
             node.production == SyntaxProduction::UsingDeclaration ||
             node.production == SyntaxProduction::GlobalLabelDeclaration ||
             node.production == SyntaxProduction::StaticAssertDeclaration)) ||
        (node.kind == SyntaxNode::Kind::Deferred &&
         node.slot_production == SyntaxProduction::Declaration &&
         (node.deferred_category == SyntaxParseCategory::Declaration ||
          node.deferred_category == SyntaxParseCategory::FunctionDeclaration));
}

bool syntax_function_definition_node(const SyntaxNode& node) {
    return (node.kind == SyntaxNode::Kind::Core &&
            node.production == SyntaxProduction::FunctionDefinition) ||
        (node.kind == SyntaxNode::Kind::Deferred &&
         node.slot_production == SyntaxProduction::FunctionDefinition &&
         node.deferred_category == SyntaxParseCategory::FunctionDefinition);
}

bool syntax_function_header_node(const SyntaxNode& node) {
    return (node.kind == SyntaxNode::Kind::Core &&
            node.production == SyntaxProduction::FunctionHeader) ||
        (node.kind == SyntaxNode::Kind::Deferred &&
         node.slot_production == SyntaxProduction::FunctionHeader &&
         node.deferred_category == SyntaxParseCategory::FunctionHeader);
}

bool syntax_type_node(const SyntaxNode& node) {
    const auto core_type = node.production == SyntaxProduction::TypeName ||
        node.production == SyntaxProduction::TypeSpecifier ||
        node.production == SyntaxProduction::ScalarType ||
        node.production == SyntaxProduction::StructOrUnionSpecifier ||
        node.production == SyntaxProduction::EnumSpecifier ||
        node.production == SyntaxProduction::TypedefName ||
        node.production == SyntaxProduction::TargetScalarBuiltinName;
    return (node.kind == SyntaxNode::Kind::Core && core_type) ||
        ((node.kind == SyntaxNode::Kind::Deferred ||
          node.kind == SyntaxNode::Kind::Extension ||
          node.kind == SyntaxNode::Kind::Macro) &&
         node.slot_production == SyntaxProduction::TypeName);
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
    struct ProjectionPart {
        const SyntaxNode* node{};
        const MetaToken* token{};
    };
    std::vector<ProjectionPart> pending{{&node, nullptr}};
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        if (next.token) {
            if (next.token->splice) pending.push_back({next.token->splice.get(), nullptr});
            else append(*next.token);
        } else if (next.node->kind == SyntaxNode::Kind::Token ||
                   next.node->kind == SyntaxNode::Kind::Extension ||
                   next.node->kind == SyntaxNode::Kind::Macro ||
                   next.node->kind == SyntaxNode::Kind::Deferred) {
            for (auto at = next.node->tokens.rbegin(); at != next.node->tokens.rend(); ++at)
                pending.push_back({nullptr, &*at});
        } else {
            for (auto at = next.node->children.rbegin(); at != next.node->children.rend(); ++at)
                pending.push_back({at->get(), nullptr});
        }
    }
    return result;
}

TokenSequence syntax_node_fragments(const SyntaxNode& node) {
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
    struct Part {
        const SyntaxNode* node;
        std::shared_ptr<const SyntaxNode> owner;
        bool splice{};
    };
    std::vector<Part> pending{{&node, {}}};
    while (!pending.empty()) {
        auto [next, owner, splice] = std::move(pending.back());
        pending.pop_back();
        std::shared_ptr<const SyntaxNode> fragment;
        if (next != &node && (splice || next->kind == SyntaxNode::Kind::Deferred ||
                             syntax_function_header_node(*next)))
            fragment = std::move(owner);
        if (next->kind == SyntaxNode::Kind::Core && next->structured_splice &&
            next->children.size() == 1 && next->children.front() &&
            ((next->production == SyntaxProduction::PrimaryExpression &&
              syntax_expression_node(*next->children.front())) ||
             (next->production == SyntaxProduction::TypeSpecifier &&
              syntax_type_node(*next->children.front())))) {
            fragment = next->children.front();
        }
        if (fragment) {
            MetaToken marker;
            marker.kind = TokenKind::StructuredSplice;
            marker.text = "__cross_syntax_splice";
            marker.origin = token_origin(fragment->span.first);
            marker.origin.context = fragment->context;
            marker.splice = std::move(fragment);
            result.push_back(std::move(marker));
        } else if (next->kind == SyntaxNode::Kind::Token ||
                   next->kind == SyntaxNode::Kind::Extension ||
                   next->kind == SyntaxNode::Kind::Macro ||
                   next->kind == SyntaxNode::Kind::Deferred) {
            for (const auto& token : next->tokens) append(token);
        } else {
            for (std::size_t at = next->children.size(); at-- > 0;)
                pending.push_back({next->children[at].get(), next->children[at],
                    std::binary_search(next->splice_children.begin(), next->splice_children.end(), at)});
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
    std::vector<const SyntaxNode*> nodes{&node};
    std::vector<const SyntaxMatchValue*> matches;
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
            add(origin_binding_storage(token.origin));
            context(token.origin.context);
            if (token.split_source) { add(32); add(token.split_source->spelling.size()); }
            // Opaque to grammar inspection does not mean free storage. A
            // deferred/raw token can retain another complete public tree.
            // Follow that ownership edge on the same iterative worklist.
            if (token.splice) nodes.push_back(token.splice.get());
        }
    };
    while ((!nodes.empty() || !matches.empty()) && size <= stop_after) {
        if (!nodes.empty()) {
            const auto* next = nodes.back();
            nodes.pop_back();
            add(128);
            add(8 * next->splice_children.size());
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

std::shared_ptr<const SyntaxMatchValue> syntax_attach_context(
    std::shared_ptr<const SyntaxMatchValue> match, std::shared_ptr<const SyntaxContext> context) {
    if (!match) return {};
    struct NodeFrame {
        std::shared_ptr<const SyntaxNode> source;
        std::shared_ptr<SyntaxNode> copy{};
        std::size_t child{};
        bool entered{};
        bool match_done{};
        SyntaxNode& change() {
            if (!copy) copy = std::make_shared<SyntaxNode>(*source);
            return *copy;
        }
    };
    struct MatchFrame {
        std::shared_ptr<const SyntaxMatchValue> source;
        std::shared_ptr<SyntaxMatchValue> copy{};
        std::size_t field{};
        std::size_t record{};
        bool entered{};
        bool field_entered{};
        bool node_done{};
        SyntaxMatchValue& change() {
            if (!copy) copy = std::make_shared<SyntaxMatchValue>(*source);
            return *copy;
        }
    };
    const auto needs_context = [](const TokenSequence& tokens) {
        return std::any_of(tokens.begin(), tokens.end(), [](const MetaToken& token) {
            return !token.origin.context;
        });
    };
    const auto attach = [&](TokenSequence& tokens) {
        for (auto& token : tokens) if (!token.origin.context) token.origin.context = context;
    };
    std::unordered_map<const SyntaxNode*, std::shared_ptr<const SyntaxNode>> nodes;
    std::unordered_map<const SyntaxMatchValue*, std::shared_ptr<const SyntaxMatchValue>> matches;
    std::vector<std::variant<NodeFrame, MatchFrame>> pending{MatchFrame{match}};
    while (!pending.empty()) {
        if (auto* frame = std::get_if<NodeFrame>(&pending.back())) {
            const auto& source = *frame->source;
            if (!frame->entered) {
                if (!source.context) frame->change().context = context;
                if (needs_context(source.tokens)) attach(frame->change().tokens);
                frame->entered = true;
            }
            if (frame->child < source.children.size()) {
                const auto& child = source.children[frame->child];
                const auto found = nodes.find(child.get());
                if (found == nodes.end()) {
                    pending.emplace_back(NodeFrame{child});
                    continue;
                }
                if (found->second != child) frame->change().children[frame->child] = found->second;
                ++frame->child;
                continue;
            }
            if (source.match && !frame->match_done) {
                const auto found = matches.find(source.match.get());
                if (found == matches.end()) {
                    pending.emplace_back(MatchFrame{source.match});
                    continue;
                }
                if (found->second != source.match) frame->change().match = found->second;
                frame->match_done = true;
            }
            nodes.emplace(frame->source.get(), frame->copy ? std::move(frame->copy) : frame->source);
            pending.pop_back();
            continue;
        }
        auto& frame = std::get<MatchFrame>(pending.back());
        const auto& source = *frame.source;
        if (!frame.entered) {
            if (!source.context) frame.change().context = context;
            if (needs_context(source.input)) attach(frame.change().input);
            frame.entered = true;
        }
        if (frame.field == source.fields.size()) {
            matches.emplace(frame.source.get(), frame.copy ? std::move(frame.copy) : frame.source);
            pending.pop_back();
            continue;
        }
        const auto& field = source.fields[frame.field];
        if (!frame.field_entered) {
            if (needs_context(field.tokens)) attach(frame.change().fields[frame.field].tokens);
            frame.field_entered = true;
        }
        if (field.node && !frame.node_done) {
            const auto found = nodes.find(field.node.get());
            if (found == nodes.end()) {
                pending.emplace_back(NodeFrame{field.node});
                continue;
            }
            if (found->second != field.node) frame.change().fields[frame.field].node = found->second;
            frame.node_done = true;
        }
        if (frame.record < field.records.size()) {
            const auto& record = field.records[frame.record];
            const auto found = matches.find(record.get());
            if (found == matches.end()) {
                pending.emplace_back(MatchFrame{record});
                continue;
            }
            if (found->second != record) frame.change().fields[frame.field].records[frame.record] = found->second;
            ++frame.record;
            continue;
        }
        ++frame.field;
        frame.record = 0;
        frame.field_entered = frame.node_done = false;
    }
    return matches.at(match.get());
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
    explicit TreeRule(Kind rule_kind = Kind::Reject) : kind(rule_kind) {}
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
        const auto expressions = sequence({reference(P::Expression),
            repeated(sequence({terminal(","), reference(P::Expression)}))});
        const auto declaration = [&](bool semicolon) {
            auto rule = sequence({attrs, reference(P::DeclarationSpecifiers),
                optional(reference(P::InitDeclaratorList)), attrs});
            if (semicolon) rule.rules.push_back(terminal(";"));
            return rule;
        };
        set(P::Declaration, choice({declaration(true),
            sequence({attrs, reference(P::FunctionHeader), attrs, terminal(";")})}));
        set(P::DeclarationWithoutFinalSemicolon, declaration(false));
        set(P::FunctionHeader, choice({
            sequence({attrs, reference(P::DeclarationSpecifiers), reference(P::Declarator), attrs}),
            sequence({attrs, reference(P::FunctionHeader), attrs})}));
        set(P::FunctionDefinition, choice({
            sequence({attrs, reference(P::DeclarationSpecifiers),
                reference(P::Declarator), attrs, reference(P::CompoundStatement)}),
            sequence({attrs, reference(P::FunctionHeader), attrs, reference(P::CompoundStatement)})}));
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
            optional(reference(P::QualifiedName)), attrs, optional(sequence({terminal("{"),
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
        set(P::GlobalLabelDeclaration, sequence({attrs, terminal("global"), terminal("label"),
            reference(P::QualifiedLabelName), attrs, terminal(";")}));
        set(P::QualifiedLabelName, sequence({reference(P::QualifiedFunctionName), terminal("::"), id}));
        set(P::QualifiedFunctionName, reference(P::QualifiedName));
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
                optional(reference(P::Expression)), terminal(";"), optional(expressions),
                terminal(")"), reference(P::Statement)})}));
        set(P::JumpStatement, choice({sequence({terminals({"break", "continue"}), terminal(";")}),
            sequence({terminal("return"), optional(reference(P::Expression)), terminal(";")}),
            sequence({terminal("goto"), reference(P::AssignmentExpression), terminal(";")})}));
        set(P::ExpressionStatement, sequence({optional(reference(P::Expression)), terminal(";")}));
        set(P::StaticAssertDeclaration, sequence({terminal("$::static_assert"), terminal("("),
            reference(P::ConstantExpression), terminal(","), string, terminal(")"), terminal(";")}));
        set(P::ForInitializer, choice({sequence({}), expressions, reference(P::DeclarationWithoutFinalSemicolon)}));
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
                terminals({"++", "--", "sizeof"}), reference(P::UnaryExpression)}),
            sequence({terminals({"&", "*", "+", "-", "~", "!"}), reference(P::CastExpression)}),
            sequence({terminal("sizeof"), terminal("("), reference(P::TypeName), terminal(")")})}));
        set(P::PostfixExpression, sequence({reference(P::PrimaryExpression), repeated(choice({
            sequence({terminal("["), reference(P::Expression), terminal("]")}),
            sequence({terminal("("), reference(P::ArgumentList), terminal(")")}),
            sequence({terminals({".", "->"}), id}), terminals({"++", "--"}), reference(P::GenericArguments)}))}));
        set(P::PrimaryExpression, choice({reference(P::QualifiedName), reference(P::BuiltinName), reference(P::Literal),
            sequence({terminal("("), reference(P::Expression), terminal(")")}),
            sequence({terminal("$::alignof"), terminal("("),
                choice({reference(P::TypeName), reference(P::Expression)}), terminal(")")}),
            sequence({terminal("$::offsetof"), terminal("("), reference(P::TypeName), terminal(","),
                id, repeated(reference(P::Designator)), terminal(")")}),
            sequence({terminal("$::atomic_is_lock_free"), terminal("("),
                choice({reference(P::TypeName), reference(P::ArgumentList)}), terminal(")")}),
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
            std::optional<std::size_t> previous_splice;
            for (const auto index : node->splice_children) {
                if (!step()) break;
                if (index >= node->children.size() || !node->children[index] ||
                    (previous_splice && index <= *previous_splice)) {
                    error = "invalid structured child boundary";
                    return false;
                }
                const auto& child = *node->children[index];
                if (node->kind == SyntaxNode::Kind::Group) {
                    if (index == 0 || index + 1 == node->children.size()) {
                        error = "raw group delimiters cannot be structured splice boundaries";
                        return false;
                    }
                } else if (!syntax_statement_node(child) && !syntax_declaration_node(child) &&
                    !syntax_function_definition_node(child)) {
                    error = "structured child boundary requires a statement or declaration fragment";
                    return false;
                }
                previous_splice = index;
            }
            if (exhausted) break;
            if (node->kind == SyntaxNode::Kind::Token || opaque_kind(node->kind)) {
                if (node->structured_splice) {
                    error = "structured splice must be a core expression or type specifier";
                    return false;
                }
                if (node->kind == SyntaxNode::Kind::Deferred) {
                    if (!deferred_slot(node->deferred_category, node->slot_production) ||
                        node->production != SyntaxProduction::None || !node->context ||
                        node->tokens.empty()) {
                        error = "deferred syntax node requires a compatible category/grammar slot, context, and bounded input";
                        return false;
                    }
                    const auto scanned = scan_token_trees(node->tokens, limits.depth,
                        TokenTreeBalance::Required, {[&](std::size_t index) {
                            if (!step()) return false;
                            const auto kind = node->tokens[index].kind;
                            if (kind == TokenKind::Invalid || kind == TokenKind::End) {
                                error = "deferred syntax input contains an invalid or boundary token";
                                return false;
                            }
                            return true;
                        }, {}, {}});
                    if (exhausted) break;
                    if (scanned == TokenTreeScanError::Cancelled) return false;
                    if (scanned == TokenTreeScanError::DepthLimit) {
                        failure = SyntaxTreeValidationError::DepthLimit;
                        error = "deferred syntax input exceeds delimiter depth budget";
                        return false;
                    }
                    if (scanned == TokenTreeScanError::UnmatchedDelimiter) {
                        error = "deferred syntax input has an unmatched delimiter";
                        return false;
                    }
                    if (scanned == TokenTreeScanError::UnterminatedGroup) {
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
            const TreeRule* rule{};
            bool group_shape{};
            if (node->kind == SyntaxNode::Kind::Core) {
                const auto index = static_cast<std::size_t>(node->production);
                if (index >= public_tree_rules().size()) {
                    error = "unknown public syntax production";
                    return false;
                }
                rule = &public_tree_rules()[index];
            } else if (node->kind == SyntaxNode::Kind::Group) {
                const auto lexical = [](const std::shared_ptr<const SyntaxNode>& child) -> const MetaToken* {
                    return child && child->kind == SyntaxNode::Kind::Token && child->tokens.size() == 1 &&
                        !child->tokens.front().splice && child->tokens.front().kind != TokenKind::StructuredSplice
                        ? &child->tokens.front() : nullptr;
                };
                if (node->children.size() >= 2) {
                    const auto* first = lexical(node->children.front());
                    const auto* last = lexical(node->children.back());
                    group_shape = first && last && first->kind == TokenKind::Punctuator &&
                        last->kind == TokenKind::Punctuator &&
                        ((first->text == "(" && last->text == ")") ||
                         (first->text == "[" && last->text == "]") ||
                         (first->text == "[[" && last->text == "]]") ||
                         (first->text == "{" && last->text == "}"));
                    for (std::size_t index = 1; group_shape && index + 1 < node->children.size(); ++index) {
                        if (!step()) break;
                        const auto& child = node->children[index];
                        if (std::binary_search(node->splice_children.begin(), node->splice_children.end(), index)) continue;
                        const auto* token = lexical(child);
                        group_shape = child && (child->kind == SyntaxNode::Kind::Group ||
                            (token && !delimiter(token->text)));
                    }
                }
            }
            if ((!rule && node->kind != SyntaxNode::Kind::Group) || !node->tokens.empty()) {
                error = "invalid public syntax tree node representation";
                return false;
            }
            const bool expression_splice_shape = node->structured_splice &&
                node->kind == SyntaxNode::Kind::Core &&
                node->production == SyntaxProduction::PrimaryExpression &&
                node->children.size() == 1 && node->children.front() &&
                syntax_expression_node(*node->children.front());
            const bool type_splice_shape = node->structured_splice &&
                node->kind == SyntaxNode::Kind::Core &&
                node->production == SyntaxProduction::TypeSpecifier &&
                node->children.size() == 1 && node->children.front() &&
                syntax_type_node(*node->children.front());
            if (node->structured_splice && !expression_splice_shape && !type_splice_shape) {
                error = "structured splice requires one category-compatible child";
                return false;
            }
            const auto positions = node->kind == SyntaxNode::Kind::Group
                ? (group_shape ? Positions{node->children.size()} : Positions{})
                : expression_splice_shape || type_splice_shape
                ? Positions{node->children.size()}
                : match(*rule, node->children, 0);
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
    const auto edge = std::lower_bound(result->splice_children.begin(), result->splice_children.end(), index);
    const bool was_bounded = edge != result->splice_children.end() && *edge == index;
    const bool bounded = parent.kind == SyntaxNode::Kind::Group
        ? was_bounded || (replacement->kind != SyntaxNode::Kind::Token && replacement->kind != SyntaxNode::Kind::Group)
        : syntax_statement_node(*replacement) || syntax_declaration_node(*replacement) ||
            syntax_function_definition_node(*replacement);
    // Tree editing inserts a subtree, even when the old child was written
    // directly rather than supplied by an unquote. Otherwise a new inner if
    // can steal its parent's else when this edited tree is materialized.
    if (bounded) {
        if (edge == result->splice_children.end() || *edge != index)
            result->splice_children.insert(edge, index);
    } else if (edge != result->splice_children.end() && *edge == index) {
        // Grammar alternatives can change, e.g. an unattributed statement's
        // declaration becomes a jump production. Its containing statement
        // supplies the boundary; this child is no longer a splice category.
        result->splice_children.erase(edge);
    }
    result->children[index] = std::move(replacement);
    if (!syntax_validate_node(*result, error, limits, work)) return {};
    return result;
}

} // namespace cross
