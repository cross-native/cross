// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::abort(); }
}
}

int main() {
    using namespace cross;
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, 32,
        no_layout, no_layout, EvaluationLimits{}, EvaluationLayout{});
    const auto* source = sources.add("syntax-state.x", R"(
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
syntax First : expression { prefix "same"; match body:paren; expand expand; }
syntax Second : statement { prefix "same"; match body:block; expand expand; }
syntax Item : item { prefix "same"; match body:block; expand expand; }
syntax Rule : rule { match value:literal; }
syntax Combined : expression { prefix "combined"; match rule(Rule); expand expand; }
syntax Optional : expression { prefix "optional"; match part:optional("x"); expand expand; }
syntax BadRule : rule { match rule(BadRule) "x"; }
syntax BadUse : expression { prefix "bad"; match rule(BadRule); expand expand; }
syntax Pack : bundle { use First; use Item; }
)");
    auto tokens = execution->prepare(*source);
    SyntaxState state(execution);
    for (std::size_t at = 0; tokens[at].kind != TokenKind::End;) {
        if (tokens[at].is("[[")) {
            require(execution->define_function(tokens, at, "", {}, state.bindings()), "expansion declaration failed");
        } else require(state.declare(tokens, at, "", diagnostics), "syntax declaration failed");
    }
    require(diagnostics.errors() == 0, "valid declarations produced diagnostics");
    const auto activate = [&](std::initializer_list<SyntaxActivation> entries) {
        return state.activate(std::span(entries.begin(), entries.size()), "", diagnostics);
    };
    require(!activate({{"First", {}, {}}, {"Second", {}, {}}}), "conflicting activation succeeded");
    require(state.bindings().empty(), "failed activation partially installed bindings");
    require(activate({{"Pack", {}, {}}}), "different dispatch families conflicted");
    const auto bindings = state.bindings();
    require(bindings.size() == 2, "bundle was not fully flattened");
    require(activate({{"First", {}, {}}}) && state.bindings() == bindings, "repeated binding was not harmless");
    state.push_scope();
    require(!activate({{"Second", {}, {}}}), "inherited dispatch conflict succeeded");
    require(state.bindings() == bindings, "inherited failure mutated activation");
    require(activate({{"Second", std::string("other"), {}}}), "alias did not resolve inherited conflict");
    require(state.bindings().size() == 3, "nested alias was not activated");
    const auto* original = sources.add("context.x", "other {}");
    const auto original_tokens = Lexer(*original, diagnostics).lex();
    auto origin = token_origin(original_tokens.front().location);
    origin.context = execution->call_context(original_tokens.front().location, "", {}, state.bindings());
    const auto* copied = sources.add("copied.x", "other {}", {}, {{0, 5, origin}});
    state.pop_scope();
    require(state.bindings() == bindings, "closing a scope failed to restore activation");
    const auto copied_tokens = Lexer(*copied, diagnostics).lex();
    const auto* copied_binding = state.selected(copied_tokens.front(), false);
    require(copied_binding && copied_binding->name == "Second", "copied tokens lost their activation context");
    require(!state.selected(original_tokens.front(), false), "inactive alias leaked out of its scope");
    const auto before = state.bindings();
    require(!activate({{"Combined", {}, {}}, {"Second", {}, {}}}), "later conflict succeeded");
    require(state.bindings() == before, "later conflict partially committed activation");
    require(activate({{"Combined", {}, {}}}), "failed activation poisoned later rule binding");
    const auto before_cycle = state.bindings();
    require(!activate({{"BadUse", {}, {}}}), "left-recursive rule activation succeeded");
    require(state.bindings() == before_cycle, "failed recursive activation installed a binding");

    // Matching preserves the actual written prefix and its bounded input.
    const auto* invocation = sources.add("invocation.x", "same ()");
    const auto invocation_tokens = Lexer(*invocation, diagnostics).lex();
    const auto* selected = state.selected(invocation_tokens.front(), false);
    require(selected && selected->name == "First", "wrong expression-family dispatch");
    const auto match = state.match(*selected, invocation_tokens, 0, diagnostics);
    require(match && match->end == 3 && match->value->input.front().text == "same",
            "raw input omitted its actual prefix or boundary");
    require(match->value->fields.front().kind == SyntaxMatchValue::Field::Kind::RawGroup &&
            match->value->fields.front().tokens.size() == 2 &&
            match->value->fields.front().node->kind == SyntaxNode::Kind::Group &&
            match->value->fields.front().node->children.size() == 2,
            "empty raw capture lost either its tokens or delimiter tree");
    const auto errors = diagnostics.errors();
    auto output = execution->expand(match->expander, {}, match->value, invocation_tokens.front().location,
        "", {}, state.bindings());
    require(output && diagnostics.errors() == errors && output->tokens.front().text == "1u32",
            "target-independent syntax evaluation failed");

    const auto* raw_source = sources.add("raw-group.x", "same (alien! { [[arbitrary]] [x] () } tail)");
    auto raw_tokens = Lexer(*raw_source, diagnostics).lex();
    const auto raw = state.match(*selected, raw_tokens, 0, diagnostics);
    require(raw && diagnostics.errors() == errors, "raw sublanguage was parsed or expanded");
    const auto& raw_field = raw->value->fields.front();
    std::string shape_error;
    require(syntax_validate_node(*raw_field.node, shape_error),
            "raw group has a structurally invalid shape");
    require(raw_field.kind == SyntaxMatchValue::Field::Kind::RawGroup && raw_field.node &&
            raw_field.node->production == SyntaxProduction::None && raw_field.node->children.size() == 6,
            "raw group has the wrong public schema");
    const auto& block = *raw_field.node->children[3];
    require(block.kind == SyntaxNode::Kind::Group && block.children.size() == 5 &&
            block.children[1]->kind == SyntaxNode::Kind::Group &&
            block.children[1]->children.size() == 3 &&
            block.children[2]->children.size() == 3 && block.children[3]->children.size() == 2,
            "nested raw delimiters were flattened");
    require(raw_field.node->span.first.file == raw_source &&
            raw_field.node->span.first.offset == raw_tokens[1].location.offset &&
            raw_field.node->span.last.offset == raw_tokens[raw->end - 1].location.offset,
            "raw group did not retain its original source span");
    require(raw->value->span.first.offset == 0 &&
            raw->value->span.last.offset == raw_field.node->span.last.offset &&
            raw_field.span.first.offset == raw_field.node->span.first.offset &&
            raw_field.span.last.offset == raw_field.node->span.last.offset,
            "match or capture span lost its original endpoints");
    const auto projection = syntax_node_tokens(*raw_field.node);
    require(projection.size() == raw_field.tokens.size() && projection.size() == raw->end - 1,
            "raw group projection changed its bounded token count");
    for (std::size_t at = 0; at < projection.size(); ++at) {
        require(projection[at].kind == raw_tokens[at + 1].kind &&
                projection[at].text == raw_tokens[at + 1].text &&
                projection[at].origin.span.file == raw_source &&
                projection[at].origin.span.offset == raw_tokens[at + 1].location.offset,
                "raw group projection changed a source token");
    }
    raw_tokens[2].text = "changed";
    require(syntax_node_tokens(*raw_field.node)[1].text == "alien" && raw_field.tokens[1].text == "alien",
            "raw group or capture retained mutable lexer storage");
    auto copied_origin = token_origin(raw_tokens[2].location);
    copied_origin.context = origin.context;
    const auto* contextual_source = sources.add("raw-group-context.x", raw_source->text, {},
        {{raw_tokens[2].location.offset, raw_tokens[2].location.offset + 5, copied_origin}});
    const auto contextual_tokens = Lexer(*contextual_source, diagnostics).lex();
    const auto contextual_match = state.match(*selected, contextual_tokens, 0, diagnostics);
    require(contextual_match && contextual_match->value->fields.front().node->children[1]->context == origin.context &&
            syntax_node_tokens(*contextual_match->value->fields.front().node)[1].origin.context == origin.context,
            "raw group lost a copied child's immutable lookup context");
    auto replacement = contextual_match->value->fields.front().node->children[1];
    const auto reconstructed = syntax_replace_child(*raw_field.node, 1, replacement, shape_error);
    require(reconstructed && reconstructed->children[1] == replacement &&
            reconstructed->children[1]->context == origin.context &&
            reconstructed->span.first.file == raw_field.node->span.first.file,
            "raw child replacement lost its original or copied context/span");
    const auto mismatched = syntax_replace_child(*raw_field.node, 0,
        raw_field.node->children.back(), shape_error);
    require(!mismatched, "raw child replacement broke delimiter balance");
    require(activate({{"Optional", {}, {}}}), "optional test syntax did not activate");
    const auto* empty_source = sources.add("empty-span.x", "optional ;");
    const auto empty_tokens = Lexer(*empty_source, diagnostics).lex();
    const auto empty_match = state.match(*state.selected(empty_tokens.front(), false), empty_tokens, 0, diagnostics);
    require(empty_match && empty_match->end == 1 &&
            empty_match->value->span.first.offset == 0 && empty_match->value->span.last.offset == 0 &&
            empty_match->value->fields.front().span.first.offset == empty_tokens[1].location.offset &&
            empty_match->value->fields.front().span.last.offset == empty_tokens[1].location.offset,
            "empty optional span was not anchored at its capture boundary");

    // Generic-close terminal fragments project back to the original lexical
    // token only when every adjacent piece has the same source identity.
    auto split = std::make_shared<const SplitTokenSource>(SplitTokenSource{TokenKind::Punctuator, ">>"});
    auto first_close = std::make_shared<SyntaxNode>();
    first_close->tokens.resize(1);
    first_close->tokens.front().kind = TokenKind::Punctuator;
    first_close->tokens.front().text = ">";
    first_close->tokens.front().split_source = split;
    auto second_close = std::make_shared<SyntaxNode>(*first_close);
    second_close->tokens.front().split_offset = 1;
    SyntaxNode generic;
    generic.kind = SyntaxNode::Kind::Core;
    generic.children = {first_close, second_close};
    const auto lexical = syntax_node_tokens(generic);
    require(lexical.size() == 1 && lexical.front().text == ">>" &&
            !lexical.front().split_source, "complete generic closes were not lossless");
    require(syntax_node_tokens(*second_close).front().text == ">",
            "one generic-close terminal exposed the entire source token");
    second_close->tokens.front().split_source =
        std::make_shared<const SplitTokenSource>(SplitTokenSource{TokenKind::Punctuator, ">>"});
    require(syntax_node_tokens(generic).size() == 2,
            "distinct generic-close source tokens were accidentally joined");

    // A copied parse environment must not acquire later syntax declarations.
    SyntaxState frozen(state, nullptr);
    const auto* later_source = sources.add("later-syntax.x",
        "syntax Later : rule { match value:literal; }");
    const auto later_tokens = Lexer(*later_source, diagnostics).lex();
    std::size_t later_at = 0;
    require(state.declare(later_tokens, later_at, "N", diagnostics),
            "later syntax declaration failed");
    const auto prior_errors = diagnostics.errors();
    require(state.resolve("N::Later", "", later_tokens.front().location, diagnostics).has_value(),
            "current syntax registry missed a later declaration");
    require(!frozen.resolve("N::Later", "", later_tokens.front().location, diagnostics) &&
            diagnostics.errors() == prior_errors + 1,
            "saved syntax context saw a later declaration");

    const auto* expression_source = sources.add("deferred-expression.x", "1u32 + 2u32");
    const auto expression_tokens = Lexer(*expression_source, diagnostics).lex();
    SyntaxNode deferred_expression;
    deferred_expression.kind = SyntaxNode::Kind::Deferred;
    deferred_expression.slot_production = SyntaxProduction::AssignmentExpression;
    deferred_expression.deferred_category = SyntaxParseCategory::Expression;
    deferred_expression.context = execution->call_context(
        expression_tokens.front().location, "", {}, state.bindings());
    deferred_expression.span = {expression_tokens.front().location,
                                expression_tokens[2].location};
    for (std::size_t at = 0; at + 1 < expression_tokens.size(); ++at)
        deferred_expression.tokens.emplace_back(expression_tokens[at]);
    std::string deferred_error;
    require(syntax_validate_node(deferred_expression, deferred_error),
            "category-tagged deferred expression failed public-tree validation");
    const auto materialized = execution->materialize_node(deferred_expression,
        expression_tokens.front().location, SyntaxParseCategory::Expression);
    require(materialized && materialized->tokens.size() == expression_tokens.size() &&
            materialized->tokens[0].is("1u32") && materialized->tokens[1].is("+"),
            "deferred expression could not be materialized in its expression slot");
    const auto category_errors = diagnostics.errors();
    require(!execution->materialize_node(deferred_expression,
                expression_tokens.front().location, SyntaxParseCategory::Statement) &&
            diagnostics.errors() == category_errors + 1,
            "deferred expression was accepted in a statement slot");
}
