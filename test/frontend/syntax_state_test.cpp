// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
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
    const auto* source = sources.add("syntax-state.x", R"SOURCE(
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
[[syntax_expander]] static $::meta::tokens keep_group(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax KeepGroup : expression { prefix "keep_group"; match body:group; expand keep_group; }
syntax First : expression { prefix "same"; match body:paren; expand expand; }
syntax Second : statement { prefix "same"; match body:block; expand expand; }
syntax Item : item { prefix "same"; match body:block; expand expand; }
syntax Rule : rule { match value:literal; }
syntax Combined : expression { prefix "combined"; match rule(Rule); expand expand; }
syntax Optional : expression { prefix "optional"; match part:optional("x"); expand expand; }
syntax EmptyRule : rule { match part:optional("x"); }
syntax EmptyChain : rule { match child:rule(EmptyRule); }
syntax AtEnd : item { prefix "at_end"; match child:rule(EmptyChain); expand expand; }
syntax AtEndChoice : item { prefix "at_end_choice"; match child:choice(empty:(rule(EmptyChain)) | required:(rule(Rule))); expand expand; }
syntax AtEndOptional : item { prefix "at_end_optional"; match child:optional(branch:choice(required:(rule(Rule)) | other:("x"))); expand expand; }
syntax AtEndRepeat : item { prefix "at_end_repeat"; match children:repeat0(rule(Rule)); expand expand; }
syntax AtEndSeparated : item { prefix "at_end_separated"; match children:separated0(rule(Rule), ","); expand expand; }
syntax ParsedChoice : expression { prefix "parsed_choice"; match "(" branch:choice(parsed:(value:expr ";") | raw:(value:literal ";")) ")"; expand expand; }
syntax Ambiguous : expression { prefix "ambiguous"; match branch:choice(first:("x") | second:("x")); expand expand; }
syntax BadRule : rule { match rule(BadRule) "x"; }
syntax BadUse : expression { prefix "bad"; match rule(BadRule); expand expand; }
syntax RepeatTail : rule { match parts:repeat0("x"); }
syntax RepeatGood : expression { prefix "repeat_good"; match "(" rule(RepeatTail) ")"; expand expand; }
syntax RepeatBad : expression { prefix "repeat_bad"; match "(" rule(RepeatTail) "x" ")"; expand expand; }
syntax RepeatSquare : expression { prefix "repeat_square"; match "[" rule(RepeatTail) "]"; expand expand; }
syntax GroupItem : rule { match body:group "x"; }
syntax RepeatGroups : expression { prefix "repeat_groups"; match items:repeat0(rule(GroupItem)) ";"; expand expand; }
syntax SeparatedGroups : expression { prefix "separated_groups"; match items:separated0(rule(GroupItem), ",") ";"; expand expand; }
syntax OptionalGroup : expression { prefix "optional_group"; match body:optional(value:group) ";"; expand expand; }
syntax ChoiceGroup : expression { prefix "choice_group"; match body:choice(paren:(value:paren) | bracket:(value:bracket) | block:(value:block)) ";"; expand expand; }
syntax RawUntil : expression { prefix "raw_until"; match body:tokens_until(";") ";"; expand expand; }
syntax Pack : bundle { use First; use Item; }
)SOURCE");
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
    const bool reactivated = activate({{"First", {}, {}}});
    require(reactivated && state.bindings() == bindings, "repeated binding was not harmless");
    state.push_scope();
    require(!activate({{"Second", {}, {}}}), "inherited dispatch conflict succeeded");
    require(state.bindings() == bindings, "inherited failure mutated activation");
    require(activate({{"Second", std::string("other"), {}}}), "alias did not resolve inherited conflict");
    require(state.bindings().size() == 3, "nested alias was not activated");
    const auto* original = sources.add("context.x", "other {}");
    const auto original_tokens = Lexer(*original, diagnostics).lex();
    auto origin = token_origin(original_tokens.front().location);
    const std::vector<TokenIdentity> import_declarations{origin.identity};
    origin.context = execution->call_context(original_tokens.front().location, "", {"Imported"},
        state.bindings(), {}, import_declarations);
    auto without_import_provenance = *origin.context;
    without_import_provenance.import_declarations.clear();
    require(syntax_context_storage(*origin.context) == syntax_context_storage(without_import_provenance) + 40,
            "import declaration provenance is not charged as logical token identity storage");
    const auto* copied = sources.add("copied.x", "other {}", {}, {{0, 5, origin}});
    state.pop_scope();
    require(state.bindings() == bindings, "closing a scope failed to restore activation");
    const auto copied_tokens = Lexer(*copied, diagnostics).lex();
    require(token_origin(copied_tokens.front().location).context->import_declarations == import_declarations,
            "copied context lost already-applied import declaration identities");
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
    require(activate({{"RepeatGood", {}, {}}}), "valid repeated-rule caller failed");
    const auto before_continuation = state.bindings();
    require(!activate({{"RepeatSquare", {}, {}}, {"RepeatBad", {}, {}}}),
            "a previously bound rule ignored its new caller's continuation");
    require(state.bindings() == before_continuation,
            "failed continuation validation partially installed bindings");
    require(activate({{"RepeatSquare", {}, {}}}),
            "failed caller poisoned a later valid continuation of the shared rule");
    const auto* repeated_source = sources.add("repeat-after-failure.x", "repeat_good (x x)");
    const auto repeated_tokens = Lexer(*repeated_source, diagnostics).lex();
    const auto repeated_errors = diagnostics.errors();
    const auto repeated_match = state.match(*state.selected(repeated_tokens.front(), false),
                                           repeated_tokens, 0, diagnostics);
    require(repeated_match && repeated_match->end == 5 && diagnostics.errors() == repeated_errors,
            "failed caller mutated a previously valid rule or match context");

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
    {
        auto prefix_context = std::make_shared<SyntaxContext>();
        prefix_context->name_space = "PrefixContext";
        auto field_context = std::make_shared<SyntaxContext>();
        field_context->name_space = "FieldContext";
        auto prefix_origin = token_origin(invocation_tokens.front().location);
        prefix_origin.context = prefix_context;
        auto field_origin = token_origin(invocation_tokens[1].location);
        field_origin.context = field_context;
        const auto* mixed_source = sources.add("mixed-context.x", "same ()", {},
            {{0, 4, std::move(prefix_origin)}, {5, 7, std::move(field_origin)}});
        auto mixed_tokens = Lexer(*mixed_source, diagnostics).lex();
        const auto mixed = state.match(*selected, mixed_tokens, 0, diagnostics, {},
            [](SourceLocation at) { return token_origin(at).context; });
        require(mixed && mixed->value->context == prefix_context &&
            mixed->value->fields.front().node->context == field_context,
            "whole-match context came from a differently contextualized first field");
    }
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

    // Raw captures expose retained syntax objects directly, including opaque
    // nodes and spliced delimiter-token nodes. Display spelling is not grammar.
    const auto* splice_source = sources.add("raw-splice.x", "same (payload)");
    auto splice_tokens = Lexer(*splice_source, diagnostics).lex();
    const auto* literal_source = sources.add("raw-splice-literal.x", "37u32");
    const auto literal_tokens = Lexer(*literal_source, diagnostics).lex();
    auto literal_leaf = std::make_shared<SyntaxNode>();
    literal_leaf->tokens.emplace_back(literal_tokens.front());
    literal_leaf->span = {literal_tokens.front().location, literal_tokens.front().location};
    literal_leaf->context = origin.context;
    auto literal_node = std::make_shared<SyntaxNode>();
    literal_node->kind = SyntaxNode::Kind::Core;
    literal_node->production = SyntaxProduction::Literal;
    literal_node->children = {literal_leaf};
    literal_node->span = literal_leaf->span;
    literal_node->context = origin.context;
    for (const auto kind : {SyntaxNode::Kind::Token, SyntaxNode::Kind::Group, SyntaxNode::Kind::Core,
                           SyntaxNode::Kind::Macro, SyntaxNode::Kind::Extension, SyntaxNode::Kind::Deferred}) {
        std::shared_ptr<const SyntaxNode> retained;
        if (kind == SyntaxNode::Kind::Token) retained = literal_leaf;
        else if (kind == SyntaxNode::Kind::Group) retained = raw_field.node;
        else if (kind == SyntaxNode::Kind::Core) retained = literal_node;
        else {
            auto opaque = std::make_shared<SyntaxNode>();
            opaque->kind = kind;
            opaque->slot_production = SyntaxProduction::AssignmentExpression;
            opaque->deferred_category = SyntaxParseCategory::Expression;
            opaque->context = origin.context;
            opaque->span = literal_leaf->span;
            opaque->tokens = literal_leaf->tokens;
            retained = std::move(opaque);
        }
        splice_tokens[2].kind = TokenKind::StructuredSplice;
        splice_tokens[2].text = "{";
        splice_tokens[2].splice = retained;
        const auto captured = state.match(*selected, splice_tokens, 0, diagnostics);
        require(captured && diagnostics.errors() == errors, "raw group parsed a retained splice");
        const auto& field = captured->value->fields.front();
        require(field.node->children.size() == 3 && field.node->children[1] == retained &&
                field.node->splice_children == std::vector<std::size_t>{1} &&
                field.tokens[1].splice == retained,
                "raw capture lost original child identity or primitive splice metadata");
        require(syntax_validate_node(*field.node, shape_error), "raw splice tree failed public validation");
        const auto fragments = syntax_node_fragments(*field.node);
        require(fragments.size() == 3 && fragments[1].splice == retained,
                "raw tree reconstruction flattened a structured child");
        const auto flattened = syntax_node_tokens(*field.node);
        require(flattened.size() == syntax_node_tokens(*retained).size() + 2,
                "explicit raw tree projection lost a child token");
        for (const auto& item : flattened)
            require(!item.splice && item.kind != TokenKind::StructuredSplice,
                    "explicit projection retained a structured marker");
        const auto edited = syntax_replace_child(*field.node, 1, literal_node, shape_error);
        require(edited && edited->children[1] == literal_node &&
                syntax_node_fragments(*edited)[1].splice == literal_node && field.node->children[1] == retained,
                "raw group editing flattened or mutated a retained child");
        const auto token_edit = syntax_replace_child(*field.node, 1, literal_leaf, shape_error);
        require(token_edit && token_edit->splice_children == std::vector<std::size_t>{1},
                "raw splice replacement forgot a token-valued boundary");
        require(!syntax_replace_child(*field.node, 0, literal_node, shape_error),
                "raw group accepted a spliced opening delimiter");
        EvaluationLimits shallow;
        shallow.depth = 1;
        require(!syntax_validate_node(*field.node, shape_error, shallow),
                "retained raw child bypassed public-tree depth accounting");
    }
    const auto inserted = syntax_replace_child(*raw_field.node, 1, literal_node, shape_error);
    require(inserted && inserted->splice_children == std::vector<std::size_t>{1} &&
            syntax_node_fragments(*inserted)[1].splice == literal_node,
            "replacement of an ordinary raw token lost its new subtree boundary");
    auto unmarked = *inserted;
    unmarked.splice_children.clear();
    require(!syntax_validate_node(unmarked, shape_error), "raw core child without a splice edge was accepted");

    // Whole retained groups match their actual delimiter category and expose
    // primitive contents without stripping nested structured boundaries.
    using PK = SyntaxPatternElement::Kind;
    auto any_group = *selected;
    any_group.pattern.front().kind = PK::Group;
    require(activate({{"RepeatGroups", {}, {}}, {"SeparatedGroups", {}, {}},
                      {"OptionalGroup", {}, {}}, {"ChoiceGroup", {}, {}}, {"RawUntil", {}, {}}}),
            "whole-group combinators did not activate");
    for (const auto delimiters : {std::pair{"(", ")"}, {"[", "]"}, {"[[", "]]"}, {"{", "}"}}) {
        const auto written = Lexer(*sources.add("whole-group.x",
            std::string("same ") + delimiters.first + " 37u32 " + delimiters.second), diagnostics).lex();
        const auto original_group = state.match(any_group, written, 0, diagnostics);
        require(original_group.has_value(), "written group did not match");
        const auto retained = syntax_replace_child(*original_group->value->fields.front().node,
            1, literal_node, shape_error);
        require(retained != nullptr, "whole-group fixture replacement failed");
        auto input = Lexer(*sources.add("whole-splice.x", "same payload tail"), diagnostics).lex();
        input[1].kind = TokenKind::StructuredSplice;
        input[1].text = "]";
        input[1].splice = retained;
        const auto matched = state.match(any_group, input, 0, diagnostics);
        require(matched && matched->end == 2 && matched->value->input.size() == 2 &&
                matched->value->input[1].splice == retained,
                "whole-group match changed its input or consumed following source");
        const auto& field = matched->value->fields.front();
        require(field.node == retained && field.tokens.size() == 3 &&
                field.tokens.front().text == delimiters.first && field.tokens.back().text == delimiters.second &&
                field.tokens[1].splice == literal_node && field.node->children[1] == literal_node &&
                field.span.first.file == written[1].location.file &&
                field.span.last.offset == written[3].location.offset &&
                field.tokens.front().origin.identity == token_origin(written[1].location).identity,
                "whole-group capture changed its identity, nested boundary, span or token provenance");
        for (const auto category : {PK::Paren, PK::Bracket, PK::Block}) {
            auto specific = any_group;
            specific.pattern.front().kind = category;
            const auto result = state.match(specific, input, 0, diagnostics, {}, {}, SyntaxState::MatchMode::Probe);
            const auto opening = std::string_view(delimiters.first);
            require(result.has_value() == ((category == PK::Paren && opening == "(") ||
                    (category == PK::Bracket && opening == "[") || (category == PK::Block && opening == "{")),
                    "whole-group splice matched the wrong delimiter category");
        }
        for (const auto prefix : {"repeat_groups", "separated_groups", "optional_group", "choice_group", "raw_until"}) {
            if (std::string_view(prefix) == "choice_group" && std::string_view(delimiters.first) == "[[") continue;
            const bool repeated = std::string_view(prefix) == "repeat_groups" ||
                                  std::string_view(prefix) == "separated_groups";
            auto combined = Lexer(*sources.add("whole-combinator.x", std::string(prefix) +
                (repeated ? " payload x ;" : " payload ;")), diagnostics).lex();
            combined[1] = input[1];
            if (std::string_view(prefix) == "raw_until") combined[1].text = ";";
            const auto* owner = state.selected(combined.front(), false);
            const auto result = state.match(*owner, combined, 0, diagnostics);
            require(result && result->end == (repeated ? 4u : 3u), "whole-group combinator failed to compose");
            if (!repeated) continue;
            combined.erase(combined.begin() + 2);
            const auto before_errors = diagnostics.errors();
            const auto before_messages = messages.str().size();
            require(!state.match(*owner, combined, 0, diagnostics) && diagnostics.errors() == before_errors + 1 &&
                    messages.str().substr(before_messages).find("malformed syntax repetition after committed start") != std::string::npos,
                    "whole-group FIRST set allowed an empty repetition to hide a malformed item");
        }
        const auto empty_written = Lexer(*sources.add("empty-whole-group.x",
            std::string("same ") + delimiters.first + delimiters.second), diagnostics).lex();
        const auto empty_original = state.match(any_group, empty_written, 0, diagnostics);
        require(empty_original.has_value(), "empty written group did not match");
        input[1].splice = empty_original->value->fields.front().node;
        const auto empty_spliced = state.match(any_group, input, 0, diagnostics);
        require(empty_spliced && empty_spliced->value->fields.front().node == input[1].splice &&
                empty_spliced->value->fields.front().tokens.size() == 2,
                "empty group splice lost its delimiters or identity");
        input[1].splice = retained;
        for (const auto bound : {0, 1, 2}) {
            EvaluationLimits limits;
            if (bound == 0) limits.depth = 1;
            if (bound == 1) limits.steps = 3;
            if (bound == 2) limits.bytes = 512;
            std::ostringstream bounded_messages;
            Diagnostics bounded_diagnostics(bounded_messages);
            auto bounded_execution = std::make_shared<SyntaxExecution>(sources, bounded_diagnostics, 32,
                no_layout, no_layout, limits, EvaluationLayout{});
            SyntaxState bounded_state(state, bounded_execution);
            require(!bounded_state.match(any_group, input, 0, bounded_diagnostics, {}, {}, SyntaxState::MatchMode::Probe) &&
                    bounded_execution->resource_errors() == 1 && bounded_diagnostics.errors() == 1,
                    "whole group capture or quiet probe bypassed its resource limit");
        }
    }
    auto nested_group_input = Lexer(*sources.add("nested-whole-group.x", "same payload"), diagnostics).lex();
    nested_group_input[1].kind = TokenKind::StructuredSplice;
    nested_group_input[1].splice = raw_field.node;
    const auto nested_group_match = state.match(any_group, nested_group_input, 0, diagnostics);
    require(nested_group_match && nested_group_match->value->fields.front().node == raw_field.node &&
            nested_group_match->value->fields.front().tokens.size() == raw_field.tokens.size(),
            "whole-group capture parsed or flattened a raw nested sublanguage");
    require(activate({{"KeepGroup", {}, {}}}), "group identity expander did not activate");
    auto identity_input = Lexer(*sources.add("group-expansion-identity.x", "keep_group (37u32)"), diagnostics).lex();
    const auto* identity_owner = state.selected(identity_input.front(), false);
    const auto identity_capture = state.match(*identity_owner, identity_input, 0, diagnostics);
    require(identity_capture.has_value(), "initial group identity capture failed");
    const auto first_identity_output = execution->expand(identity_capture->expander, {}, identity_capture->value,
        identity_input.front().location, "", {}, state.bindings());
    require(first_identity_output && first_identity_output->tokens.front().splice,
            "initial group expansion did not retain a node");
    const auto published_group = first_identity_output->tokens.front().splice;
    for (const bool nested_child : {false, true}) {
        identity_input = Lexer(*sources.add("group-identity-relay.x",
            nested_child ? "keep_group (payload)" : "keep_group payload"), diagnostics).lex();
        auto& marker = identity_input[nested_child ? 2 : 1];
        marker.kind = TokenKind::StructuredSplice;
        marker.splice = published_group;
        const auto identity_match = state.match(*identity_owner, identity_input, 0, diagnostics);
        require(identity_match.has_value(), "retained identity relay did not match");
        const auto identity_output = execution->expand(identity_match->expander, {}, identity_match->value,
            identity_input.front().location, "", {}, state.bindings());
        require(identity_output && identity_output->tokens.front().splice,
                "retained identity relay did not expand");
        const auto returned = identity_output->tokens.front().splice;
        require((nested_child ? returned->children[1] : returned) == published_group,
                "expansion context attachment cloned a previously published node");
    }
    require(activate({{"Optional", {}, {}}}), "optional test syntax did not activate");
    const auto* empty_source = sources.add("empty-span.x", "optional ;");
    const auto empty_tokens = Lexer(*empty_source, diagnostics).lex();
    const auto empty_match = state.match(*state.selected(empty_tokens.front(), false), empty_tokens, 0, diagnostics);
    require(empty_match && empty_match->end == 1 &&
            empty_match->value->span.first.offset == 0 && empty_match->value->span.last.offset == 0 &&
            empty_match->value->fields.front().span.first.offset == empty_tokens[1].location.offset &&
            empty_match->value->fields.front().span.last.offset == empty_tokens[1].location.offset,
            "empty optional span was not anchored at its capture boundary");

    require(activate({{"AtEnd", {}, {}}}), "nullable-rule test did not activate");
    const auto* at_end_source = sources.add("rule-at-end.x", "at_end");
    const auto at_end_tokens = Lexer(*at_end_source, diagnostics).lex();
    const auto at_end_errors = diagnostics.errors();
    const auto at_end_match = state.match(*state.selected(at_end_tokens.front(), true),
        at_end_tokens, 0, diagnostics);
    require(at_end_match && at_end_match->end == 1 && diagnostics.errors() == at_end_errors,
        "nullable rule chain did not match end of input");
    const auto& chain = *at_end_match->value->fields.front().records.front();
    const auto& empty_rule = *chain.fields.front().records.front();
    require(chain.input.empty() && empty_rule.input.empty() &&
            empty_rule.fields.front().records.empty() &&
            empty_rule.span.first.offset == at_end_tokens.back().location.offset &&
            empty_rule.span.last.offset == at_end_tokens.back().location.offset,
        "nullable EOF rule lost its empty child record or boundary span");

    require(activate({{"AtEndChoice", {}, {}}, {"AtEndOptional", {}, {}}, {"AtEndRepeat", {}, {}},
                      {"AtEndSeparated", {}, {}}}), "nullable EOF combinators did not activate");
    for (const auto prefix : {"at_end_choice", "at_end_optional", "at_end_repeat", "at_end_separated"}) {
        const auto* input = sources.add("combinator-at-end.x", prefix);
        const auto input_tokens = Lexer(*input, diagnostics).lex();
        const auto combinator_errors = diagnostics.errors();
        const auto combinator_match = state.match(*state.selected(input_tokens.front(), true),
            input_tokens, 0, diagnostics);
        require(combinator_match && combinator_match->end == 1 && diagnostics.errors() == combinator_errors,
            "required EOF branch spoiled a valid empty combinator match");
        const auto& field = combinator_match->value->fields.front();
        if (std::string_view(prefix) == "at_end_choice") {
            require(field.records.size() == 1 && field.records.front()->variant == "empty" &&
                    field.records.front()->input.empty(),
                "nullable EOF choice lost its empty tagged child record");
        } else require(field.records.empty(), "empty EOF combinator produced a child record");
    }

    require(activate({{"ParsedChoice", {}, {}}}), "parsed choice did not activate");
    const auto* parsed_source = sources.add("parsed-failure.x", "parsed_choice (1u32;)");
    const auto parsed_tokens = Lexer(*parsed_source, diagnostics).lex();
    const auto* parsed_definition = state.selected(parsed_tokens.front(), false);
    const auto normal_errors = diagnostics.errors();
    const auto normal_miss = state.match(*parsed_definition, parsed_tokens, 0, diagnostics,
        [](SyntaxPatternElement::Kind, std::size_t) -> std::optional<SyntaxParsedFragment> { return {}; });
    require(normal_miss && diagnostics.errors() == normal_errors,
        "ordinary speculative miss invalidated an independent alternative");
    const auto probe_errors = diagnostics.errors();
    const auto* probe_source = sources.add("parsed-probe-miss.x", "parsed_choice unexpected;");
    const auto probe_tokens = Lexer(*probe_source, diagnostics).lex();
    const auto probe_miss = state.match(*parsed_definition, probe_tokens, 0, diagnostics,
        {}, {}, SyntaxState::MatchMode::Probe);
    require(!probe_miss && diagnostics.errors() == probe_errors,
        "boundary probe diagnosed an ordinary identifier/pattern mismatch");
    const auto hard_errors = diagnostics.errors();
    const auto hard_miss = state.match(*parsed_definition, parsed_tokens, 0, diagnostics,
        [&](SyntaxPatternElement::Kind, std::size_t at) -> std::optional<SyntaxParsedFragment> {
            execution->tree_limit_error(parsed_tokens[at].location);
            return {};
        });
    require(!hard_miss && diagnostics.errors() == hard_errors + 1,
        "shared parser resource error still returned a successful owner match");
    const auto hard_probe_errors = diagnostics.errors();
    const auto hard_probe = state.match(*parsed_definition, parsed_tokens, 0, diagnostics,
        [&](SyntaxPatternElement::Kind, std::size_t at) -> std::optional<SyntaxParsedFragment> {
            execution->tree_limit_error(parsed_tokens[at].location);
            return {};
        }, {}, SyntaxState::MatchMode::Probe);
    require(!hard_probe && diagnostics.errors() == hard_probe_errors + 1,
        "boundary probe suppressed a shared parser resource failure");
    for (const auto mode : {SyntaxState::MatchMode::Required, SyntaxState::MatchMode::Probe}) {
        std::ostringstream quiet_messages;
        Diagnostics quiet(quiet_messages);
        const auto shared_errors = diagnostics.errors();
        const auto resources = execution->resource_errors();
        const auto isolated = state.match(*parsed_definition, parsed_tokens, 0, quiet,
            [&](SyntaxPatternElement::Kind, std::size_t at) -> std::optional<SyntaxParsedFragment> {
                execution->tree_limit_error(parsed_tokens[at].location);
                return {};
            }, {}, mode);
        require(!isolated && quiet.errors() == 0 && diagnostics.errors() == shared_errors + 1 &&
                execution->resource_errors() == resources + 1,
            "a private matcher diagnostic sink swallowed a shared resource failure");
        const auto recovered = state.match(*parsed_definition, parsed_tokens, 0, quiet,
            [](SyntaxPatternElement::Kind, std::size_t) -> std::optional<SyntaxParsedFragment> { return {}; },
            {}, mode);
        require(recovered && quiet.errors() == 0 && execution->resource_errors() == resources + 1,
            "a previous invocation's resource failure poisoned an independent match");
    }
    {
        const auto* epoch_source = sources.add("resource-epoch-literal.x", "1u32");
        const auto epoch_tokens = Lexer(*epoch_source, diagnostics).lex();
        Parser reusable(epoch_tokens, diagnostics, execution, 32);
        const auto initial_recognition = reusable.parse_syntax_fragment(SyntaxPatternElement::Kind::Expr, 0);
        require(initial_recognition && initial_recognition->end == 1, "initial public expression recognition failed");
        execution->resource_error(epoch_tokens.front().location, "injected independent resource error");
        const auto resource_counter = execution->resource_errors();
        const auto error_counter = diagnostics.errors();
        const auto recovered_recognition = reusable.parse_syntax_fragment(SyntaxPatternElement::Kind::Expr, 0);
        const auto complete_recognition = reusable.parse_syntax_tokens(SyntaxParseCategory::Expression, epoch_tokens);
        require(recovered_recognition && recovered_recognition->end == 1 && complete_recognition &&
                execution->resource_errors() == resource_counter && diagnostics.errors() == error_counter,
            "an earlier resource error poisoned fresh recognition in a reused parser");
    }
    for (const auto mode : {SyntaxState::MatchMode::Required, SyntaxState::MatchMode::Probe}) {
        unsigned captures{};
        const auto resource_counter = execution->resource_errors();
        const auto error_counter = diagnostics.errors();
        const auto context_failure = state.match(*state.selected(invocation_tokens.front(), false),
            invocation_tokens, 0, diagnostics, {}, [&](SourceLocation at) {
                ++captures;
                execution->resource_error(at, "injected context-capture resource error");
                return std::make_shared<const SyntaxContext>();
            }, mode);
        require(!context_failure && captures == 1 && execution->resource_errors() == resource_counter + 1 &&
                diagnostics.errors() == error_counter + 1,
            "raw group construction continued after context capture exhausted resources");
        const auto independent = state.match(*state.selected(invocation_tokens.front(), false),
            invocation_tokens, 0, diagnostics, {}, {}, mode);
        require(independent && execution->resource_errors() == resource_counter + 1 &&
                diagnostics.errors() == error_counter + 1,
            "a context-capture resource error poisoned a fresh independent match");
    }
    require(activate({{"Ambiguous", {}, {}}}), "ambiguous-pattern owner did not activate");
    const auto* ambiguous_source = sources.add("ambiguous-probe.x", "ambiguous x");
    const auto ambiguous_tokens = Lexer(*ambiguous_source, diagnostics).lex();
    const auto ambiguous_errors = diagnostics.errors();
    const auto ambiguous_probe = state.match(*state.selected(ambiguous_tokens.front(), false),
        ambiguous_tokens, 0, diagnostics, {}, {}, SyntaxState::MatchMode::Probe);
    require(!ambiguous_probe && diagnostics.errors() == ambiguous_errors + 1,
        "boundary probe treated ambiguity as an ordinary identifier mismatch");

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

    EvaluationLimits tiny;
    tiny.steps = 3;
    auto limited = std::make_shared<SyntaxExecution>(sources, diagnostics, 32,
        no_layout, no_layout, tiny, EvaluationLayout{});
    for (unsigned index = 0; index < 3; ++index) {
        require(limited->begin_replacement({}), "independent invocation exhausted work too early");
        limited->end_replacement();
    }
    const auto work_errors = diagnostics.errors();
    require(!limited->begin_replacement({}) && diagnostics.errors() == work_errors + 1,
        "invocation entry was not charged to the configured work budget");
}
