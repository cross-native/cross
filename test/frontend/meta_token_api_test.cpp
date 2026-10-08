// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/procedural.hpp"
#include "frontend/semantic.hpp"
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition, const char* message, const std::string& diagnostics) {
    if (condition) return;
    std::cerr << message << '\n' << diagnostics;
    std::exit(1);
}

void count_width_checks() {
    using namespace cross;
    // Exercise the model-parametric evaluator independently of the currently
    // shipped machine ABIs. A host size_t is not a target uptr value.
    for (const auto count : {0u, 1u, 255u, 256u}) {
        SourceManager sources;
        std::ostringstream parse_messages;
        Diagnostics parse_diagnostics(parse_messages);
        const auto expected = std::to_string(count) + "u32";
        const auto* source = sources.add("count-width.x",
            "[[macro]] static $::meta::tokens tokens_count(in $::meta::tokens input) { "
            "$::static_assert($::meta::len(input) == " + expected + ", \"token count\"); "
            "return $::quote { 1u32 }; } "
            "[[syntax_expander]] static $::meta::tokens children_count(in $::meta::syntax_match input) { "
            "$::static_assert($::meta::child_count($::syntax::node(input, \"tree\")) == " + expected +
            ", \"child count\"); return $::quote { 1u32 }; } "
            "[[syntax_expander]] static $::meta::tokens records_count(in $::meta::syntax_match input) { "
            "$::static_assert($::syntax::count(input, \"records\") == " + expected +
            ", \"record count\"); return $::quote { 1u32 }; }");
        Parser parser(Lexer(*source, parse_diagnostics).lex(), parse_diagnostics);
        auto program = parser.parse();
        require(parse_diagnostics.errors() == 0 && program.functions.size() == 3,
            "failed to parse count-width helpers", parse_messages.str());
        auto context = std::make_shared<SyntaxContext>();
        context->kind = SyntaxContext::Kind::DefinitionSite;
        context->definition = context->invocation = program.functions.front()->location;
        std::string words;
        for (unsigned at = 0; at < count; ++at) words += "word ";
        const auto* input_source = sources.add("count-input.x", words);
        TokenSequence tokens;
        for (const auto& token : Lexer(*input_source, parse_diagnostics).lex()) {
            if (token.kind == TokenKind::End) continue;
            tokens.emplace_back(token);
            tokens.back().origin.context = context;
        }
        auto tree = std::make_shared<SyntaxNode>();
        tree->kind = SyntaxNode::Kind::Core;
        tree->production = SyntaxProduction::BalancedTokens;
        tree->span = {context->invocation, context->invocation};
        tree->context = context;
        for (const auto& token : tokens) {
            auto child = std::make_shared<SyntaxNode>();
            child->kind = SyntaxNode::Kind::Token;
            child->tokens.push_back(token);
            child->span = {token.origin.span, token.origin.last_span()};
            child->context = context;
            tree->children.push_back(std::move(child));
        }
        std::string tree_error;
        require(syntax_validate_node(*tree, tree_error), "invalid count-width tree fixture", tree_error);
        auto match = std::make_shared<SyntaxMatchValue>();
        match->context = context;
        match->span = tree->span;
        SyntaxMatchValue::Field tree_field;
        tree_field.name = "tree";
        tree_field.kind = SyntaxMatchValue::Field::Kind::Parsed;
        tree_field.node = tree;
        tree_field.span = tree->span;
        match->fields.push_back(std::move(tree_field));
        SyntaxMatchValue::Field record_field;
        record_field.name = "records";
        record_field.kind = SyntaxMatchValue::Field::Kind::Nested;
        record_field.span = tree->span;
        for (unsigned at = 0; at < count; ++at) {
            auto record = std::make_shared<SyntaxMatchValue>();
            record->input.push_back(tokens[at]);
            record->span = {tokens[at].origin.span, tokens[at].origin.last_span()};
            record->context = context;
            record_field.records.push_back(std::move(record));
        }
        match->fields.push_back(std::move(record_field));
        const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
        for (const auto bits : {8u, 16u, 32u, 64u, 128u}) {
            for (unsigned operation = 0; operation < program.functions.size(); ++operation) {
                std::ostringstream messages;
                Diagnostics diagnostics(messages);
                const auto& function = *program.functions[operation];
                const auto result = operation == 0
                    ? evaluate_procedural_body(function, tokens, bits, no_layout, no_layout,
                        context, diagnostics)
                    : evaluate_syntax_body(function, match, bits, no_layout, no_layout,
                        context, diagnostics, EvaluationLimits{}, EvaluationLayout{});
                const bool overflow = bits == 8 && count == 256;
                if (overflow) {
                    require(!result && diagnostics.errors() != 0 &&
                        messages.str().find("count exceeds target uptr") != std::string::npos,
                        "meta count did not reject target-uptr overflow", messages.str());
                } else {
                    require(result && diagnostics.errors() == 0 && result->size() == 1 &&
                        result->front().text == "1u32",
                        "representable meta count changed with target width", messages.str());
                }
            }
            if (count == 1) {
                // Raw lexical storage can exceed the target count range while
                // its one top-level group still has a representable length.
                std::string grouped_words = "{";
                for (unsigned at = 0; at < 256; ++at) grouped_words += "word ";
                grouped_words += "}";
                const auto* grouped_source = sources.add("grouped-count.x", grouped_words);
                TokenSequence grouped;
                for (const auto& token : Lexer(*grouped_source, parse_diagnostics).lex()) {
                    if (token.kind == TokenKind::End) continue;
                    grouped.emplace_back(token);
                    grouped.back().origin.context = context;
                }
                std::ostringstream messages;
                Diagnostics diagnostics(messages);
                const auto result = evaluate_procedural_body(*program.functions[0], grouped, bits,
                    no_layout, no_layout, context, diagnostics);
                require(result && diagnostics.errors() == 0 && result->size() == 1,
                    "token count used raw lexical storage instead of top-level trees", messages.str());
            }
        }
    }
}
}

int main() {
    using namespace cross;
    count_width_checks();
    const auto deferred_array = array_type(builtin_type(BuiltinType::Uptr), 0);
    deferred_array->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
    const auto deferred_copy = copy_type(pointer_type(deferred_array));
    require(deferred_copy->pointee != deferred_array &&
        deferred_copy->pointee->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext,
        "type graph copy lost the provisional inferred-extent dependency", {});
    const auto fixed_array = array_type(builtin_type(BuiltinType::Uptr), 3);
    require(compare_pointee(deferred_array, fixed_array) == PointeeCompatibility::DeferredExtent &&
        !compatible_pointee(deferred_array, fixed_array) && !compatible_pointee(deferred_array, deferred_array),
        "a provisional extent became a concrete compatibility proof", {});
    require(compare_pointee(deferred_array, array_type(builtin_type(BuiltinType::U32), 3)) ==
        PointeeCompatibility::Incompatible,
        "deferred extent hid a mismatching element type", {});
    const auto qualified_array = copy_type(deferred_array);
    qualified_array->is_const = true;
    require(compare_pointee(qualified_array, fixed_array) == PointeeCompatibility::Incompatible &&
        compare_pointee(fixed_array, qualified_array) == PointeeCompatibility::DeferredExtent,
        "deferred extent changed qualification preservation", {});
    auto first_space = pointer_type(deferred_array), second_space = pointer_type(fixed_array);
    first_space->address_space = 1;
    second_space->address_space = 2;
    require(compare_pointee(first_space, second_space) == PointeeCompatibility::Incompatible,
        "deferred extent hid a nested address-space mismatch", {});
    require(compare_pointee(deferred_array, builtin_type(BuiltinType::Void)) == PointeeCompatibility::Compatible,
        "an extent-independent void conversion was deferred", {});
    SourceManager sources;
    std::ostringstream parse_messages;
    Diagnostics parse_diagnostics(parse_messages);
    const auto* source = sources.add("inspect.x", R"SOURCE(
[[macro]] static $::meta::tokens inspect(in $::meta::tokens input) {
    $::meta::span(input);
    ($::meta::span(input));
    $::static_assert($::meta::is_kind(input, "group"), "input group");
    $::meta::tokens children = $::meta::children(input);
    $::static_assert($::meta::len(children) == 3uptr, "direct children only");
    $::static_assert($::meta::is_kind($::meta::at(children, 0uptr), "identifier"), "fresh lexical kind");
    $::meta::bytes spelling = $::meta::spelling($::meta::at(children, 0uptr));
    $::static_assert($::meta::len(spelling) == 5uptr && $::meta::at(spelling, 0uptr) == 'f',
                    "fresh identity does not leak into spelling");
    $::static_assert($::meta::is_kind($::meta::at(children, 2uptr), "splice"), "opaque splice");
    $::meta::note($::meta::span($::meta::at(children, 2uptr)), "retained node span");
    return children;
}
[[macro]] static $::meta::tokens construct(in $::meta::tokens input) {
    $::meta::tokens children = $::meta::children(input);
    $::meta::span source = $::meta::span(input);
    $::meta::tokens leaf = $::meta::token("identifier",
        $::meta::spelling($::meta::at(children, 0uptr)), source);
    $::meta::tokens group = $::meta::group("[[]]", children, source);
    return $::meta::concat(leaf, $::meta::concat(group, $::meta::token("integer", "7u32")));
}
[[macro]] static $::meta::tokens group_only(in $::meta::tokens input) {
    return $::meta::group("()", input);
}
[[macro]] static $::meta::tokens retarget(in $::meta::tokens input) {
    return $::meta::call_site($::meta::at($::meta::children(input), 0uptr));
}
[[macro]] static $::meta::tokens probe_identity(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("visible");
    if (0u32) {
        (u32 *)$::eval($::meta::len($::meta::gensym("discarded")) - 1uptr);
    }
    return $::meta::concat(first, $::meta::gensym("visible"));
}
[[macro]] static $::meta::tokens checked_case(in $::meta::tokens input) {
    switch (sizeof(uptr)) {
    case sizeof(uptr): return input;
    default: return $::quote {};
    }
}
[[macro]] static $::meta::tokens inferred_array(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("first");
    uptr values[] = { [$::eval(sizeof(uptr) + $::meta::len($::meta::gensym("index")) - 1uptr)] = 17uptr };
    $::static_assert(sizeof(values) == (sizeof(uptr) + 1uptr) * sizeof(uptr), "completed extent");
    $::static_assert(values[sizeof(uptr)] == 17uptr && values[0uptr] == 0uptr, "initialized array");
    return $::meta::concat(first, $::meta::gensym("last"));
}
)SOURCE");
    const auto function_tokens = Lexer(*source, parse_diagnostics).lex();
    Parser parser(function_tokens, parse_diagnostics);
    auto program = parser.parse();
    require(parse_diagnostics.errors() == 0 && program.functions.size() == 7,
            "failed to parse token-inspection helper", parse_messages.str());
    const auto* input_source = sources.add("input.x", "(first [[raw]] marker)");
    const auto lexed = Lexer(*input_source, parse_diagnostics).lex();
    TokenSequence input;
    for (const auto& token : lexed)
        if (token.kind != TokenKind::End) input.emplace_back(token);
    const auto* retained_source = sources.add("retained.x", "opaque!{}");
    auto node = std::make_shared<SyntaxNode>();
    node->kind = SyntaxNode::Kind::Deferred;
    node->span = {{retained_source, 0, 1, 1}, {retained_source, 8, 1, 9}};
    auto& marker = input[input.size() - 2];
    marker.kind = TokenKind::StructuredSplice;
    marker.splice = node;
    auto fresh = std::make_shared<FreshIdentifier>();
    fresh->prefix = "first";
    fresh->ordinal = 41;
    input[1].origin.fresh = fresh;
    auto fragment_lookup = std::make_shared<FragmentNamespaceLookup>();
    fragment_lookup->key = NameKey("first");
    fragment_lookup->key.fresh = fresh;
    input[1].origin.fragment_lookup = fragment_lookup;
    const auto context = std::make_shared<SyntaxContext>();
    context->kind = SyntaxContext::Kind::DefinitionSite;
    context->invocation = function_tokens.front().location;
    context->definition = function_tokens.front().location;
    const auto caller = std::make_shared<SyntaxContext>();
    caller->kind = SyntaxContext::Kind::CallSite;
    caller->name_space = "caller";
    for (auto& token : input) token.origin.context = caller;
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    for (const auto bits : {32u, 64u, 128u}) {
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big}) {
            std::ostringstream messages;
            Diagnostics diagnostics(messages);
            EvaluationLayout layout;
            layout.byte_order = order;
            const auto result = evaluate_procedural_body(*program.functions[0], input, bits,
                no_layout, no_layout, context, diagnostics, EvaluationLimits{}, layout);
            require(result && diagnostics.errors() == 0, "token inspection failed", messages.str());
            require(result->size() + 2 == input.size(), "children changed token count", messages.str());
            for (std::size_t at = 0; at < result->size(); ++at) {
                const auto& copied = (*result)[at];
                const auto& original = input[at + 1];
                require(copied.kind == original.kind && copied.text == original.text &&
                        copied.origin.identity == original.origin.identity &&
                        copied.origin.span.file == original.origin.span.file &&
                        copied.origin.span.offset == original.origin.span.offset &&
                        copied.origin.context == original.origin.context &&
                        copied.origin.fragment_lookup == original.origin.fragment_lookup &&
                        copied.origin.fresh == original.origin.fresh && copied.splice == original.splice,
                        "children changed retained token provenance or flattened a splice", messages.str());
            }
            require(messages.str().find("retained.x:1:1: note: retained node span") != std::string::npos,
                    "splice span used its wrapper instead of its retained node", messages.str());
            auto fragment = input;
            fragment.pop_back();
            std::ostringstream fragment_messages;
            Diagnostics fragment_diagnostics(fragment_messages);
            const auto rejected = evaluate_procedural_body(*program.functions[0], fragment, bits,
                no_layout, no_layout, context, fragment_diagnostics, EvaluationLimits{}, layout);
            require(!rejected && fragment_diagnostics.errors() != 0 &&
                    fragment_messages.str().find("requires balanced token groups") != std::string::npos,
                    "inspection accepted an incomplete projected delimiter fragment", fragment_messages.str());
            std::ostringstream group_messages;
            Diagnostics group_diagnostics(group_messages);
            const auto rejected_group = evaluate_procedural_body(*program.functions[2], fragment, bits,
                no_layout, no_layout, context, group_diagnostics, EvaluationLimits{}, layout);
            require(!rejected_group && group_diagnostics.errors() != 0 &&
                    group_messages.str().find("requires balanced token groups") != std::string::npos,
                    "group construction accepted an incomplete contents fragment", group_messages.str());
            const auto constructed = evaluate_procedural_body(*program.functions[1], input, bits,
                no_layout, no_layout, context, diagnostics, EvaluationLimits{}, layout);
            require(constructed && diagnostics.errors() == 0 && constructed->size() == input.size() + 2,
                    "token/group construction failed", messages.str());
            const auto& leaf = constructed->front();
            require(leaf.kind == TokenKind::Identifier && leaf.text == "first" && !leaf.origin.fresh &&
                    !leaf.origin.fragment_lookup &&
                    !leaf.origin.identity.source_unit &&
                    leaf.origin.context && leaf.origin.context->kind == SyntaxContext::Kind::DefinitionSite &&
                    leaf.origin.context->name_space != "caller" &&
                    leaf.origin.span.file == input_source && leaf.origin.span.offset == 0 &&
                    leaf.origin.last_span().file == input_source &&
                    leaf.origin.last_span().offset == input.back().origin.span.offset,
                    "explicit span changed construction context/identity or lost its final anchor", messages.str());
            require((*constructed)[1].text == "[[" && (*constructed)[constructed->size() - 2].text == "]]" &&
                    (*constructed)[1].origin.last_span().offset == input.back().origin.span.offset &&
                    (*constructed)[constructed->size() - 2].origin.last_span().offset == input.back().origin.span.offset,
                    "constructed group lost delimiter spelling or span anchors", messages.str());
            for (std::size_t at = 1; at + 1 < input.size(); ++at) {
                const auto& copied = (*constructed)[at + 1];
                require(copied.origin.identity == input[at].origin.identity &&
                        copied.origin.context == input[at].origin.context &&
                        copied.origin.fragment_lookup == input[at].origin.fragment_lookup &&
                        copied.origin.fresh == input[at].origin.fresh && copied.splice == input[at].splice,
                        "group construction changed its copied contents", messages.str());
            }
            require(constructed->back().origin.span.file == context->invocation.file &&
                    constructed->back().origin.span.offset == context->invocation.offset,
                    "omitted span did not use the enclosing invocation", messages.str());
            const auto retargeted = evaluate_procedural_body(*program.functions[3], input, bits,
                no_layout, no_layout, context, diagnostics, EvaluationLimits{}, layout, {}, caller);
            require(retargeted && diagnostics.errors() == 0 && retargeted->size() == 1 &&
                    !retargeted->front().origin.fragment_lookup &&
                    retargeted->front().origin.identity == input[1].origin.identity &&
                    retargeted->front().origin.fresh == fresh && retargeted->front().origin.context == caller,
                    "call_site retained the old namespace fragment or changed token identity", messages.str());
            const auto probed = evaluate_procedural_body(*program.functions[4], {}, bits,
                no_layout, no_layout, context, diagnostics, EvaluationLimits{}, layout, {}, caller);
            require(probed && diagnostics.errors() == 0 && probed->size() == 2 &&
                    probed->front().origin.fresh && probed->back().origin.fresh &&
                    probed->front().origin.fresh->ordinal > 1 &&
                    probed->back().origin.fresh->ordinal > probed->front().origin.fresh->ordinal,
                    "isolated source proof reset or lost fresh construction identity", messages.str());
            const LayoutQuery pointer_layout = [bits](const TypePtr& type) -> std::optional<std::uint64_t> {
                if (type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Uptr)
                    return bits / 8U;
                return {};
            };
            const auto checked = evaluate_procedural_body(*program.functions[5], input, bits,
                pointer_layout, pointer_layout, context, diagnostics, EvaluationLimits{}, layout, {}, caller);
            require(checked && diagnostics.errors() == 0 && checked->size() == input.size(),
                    "case validation cached a different evaluation context in the shared source AST", messages.str());
            // Supply the small primitive layout service this frontend client
            // needs. The driver tests exercise the real shared target planner.
            const LayoutQuery array_size = [bits](const TypePtr& type) -> std::optional<std::uint64_t> {
                if (type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Uptr)
                    return bits / 8U;
                if (type && type->kind == Type::Kind::Array && type->lanes && type->element &&
                    type->element->kind == Type::Kind::Builtin && type->element->builtin == BuiltinType::Uptr)
                    return type->lanes * (bits / 8U);
                return {};
            };
            const LayoutQuery array_alignment = [bits](const TypePtr&) -> std::optional<std::uint64_t> {
                return bits / 8U;
            };
            unsigned initializer_plans{};
            program.evaluation_initializer_plan = [&](const Expr& initializer, const TypePtr& destination) {
                ++initializer_plans;
                require(initializer.initializer_entries.size() == 1 && destination->kind == Type::Kind::Array &&
                    destination->lanes == 0, "initializer source type was cached in the shared AST", messages.str());
                const auto& entry = initializer.initializer_entries.front();
                require(entry.designators.size() == 1 && entry.designators.front().index &&
                    entry.designators.front().index->evaluated_integer.has_value(),
                    "initializer plan received an unresolved required designator", messages.str());
                const auto index = entry.designators.front().index->evaluated_integer->value.low;
                require(index == bits / 8U, "initializer retained a previous model's extent", messages.str());
                EvaluationInitializerPlan plan;
                plan.minimum_elements = index + 1;
                plan.items.push_back({entry.value.get(), destination->element,
                    {index * (bits / 8U), bits / 8U, {}, 0}});
                return plan;
            };
            const auto inferred = evaluate_procedural_body(*program.functions[6], {}, bits,
                array_size, array_alignment, context, diagnostics, EvaluationLimits{}, layout, {}, caller, &program);
            require(inferred && diagnostics.errors() == 0 && inferred->size() == 2 && initializer_plans == 1 &&
                inferred->front().origin.fresh && inferred->back().origin.fresh &&
                inferred->back().origin.fresh->ordinal == inferred->front().origin.fresh->ordinal + 1,
                "inferred allocation/initialization repeated a required designator or lost the plan", messages.str());
            program.evaluation_initializer_plan = {};
            auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
                no_layout, no_layout, EvaluationLimits{}, layout);
            const auto parsed = execution->parse_tokens(SyntaxParseCategory::Expression, {leaf},
                leaf.origin.context, context->invocation);
            require(parsed && parsed->span.first.file == input_source && parsed->span.first.offset == 0 &&
                    parsed->span.last.file == input_source &&
                    parsed->span.last.offset == input.back().origin.span.offset,
                    "serialization/public capture lost the supplied span's final anchor", messages.str());
            const auto reparsed = execution->parse_tokens(SyntaxParseCategory::Expression, {input[1]},
                leaf.origin.context, context->invocation);
            require(reparsed && diagnostics.errors() == 0, "explicit context reparse failed", messages.str());
            const auto projected = syntax_node_tokens(*reparsed);
            require(projected.size() == 1 && !projected.front().origin.fragment_lookup &&
                    projected.front().origin.identity == input[1].origin.identity &&
                    projected.front().origin.fresh == fresh,
                    "explicit context reparse retained namespace lookup or changed identity", messages.str());
        }
    }
    // Preserve the established placement rule: copied input has a fixed
    // identity, while unplaced constructed tokens acquire distinct output IDs.
    const auto* expanded = expand_procedural_macros(sources, "constructor-identities.x", R"SOURCE(
[[macro]] static $::meta::tokens construct(in $::meta::tokens input) {
    $::meta::tokens leaf = $::meta::token("builtin", $::meta::spelling(input), $::meta::span(input));
    return $::meta::concat($::meta::concat(input, input),
        $::meta::concat($::meta::concat(leaf, leaf), $::meta::group("()", leaf, $::meta::span(input))));
}
construct!{ $::patch }
)SOURCE", parse_diagnostics, 64, no_layout, no_layout);
    require(expanded && parse_diagnostics.errors() == 0, "constructor identity expansion failed", parse_messages.str());
    std::vector<TokenOrigin> patches;
    for (const auto& token : Lexer(*expanded, parse_diagnostics).lex())
        if (token.is("$::patch")) patches.push_back(token_origin(token.location));
    require(patches.size() == 5 && patches[0].identity == patches[1].identity &&
            patches[0].identity.expansion.value == 0,
            "constructor path changed copied input identity", parse_messages.str());
    for (std::size_t at = 2; at < patches.size(); ++at) {
        require(patches[at].identity.expansion.value != 0 && patches[at].identity.source_unit &&
                patches[at].span.file == patches[0].span.file && patches[at].span.offset == patches[0].span.offset,
                "supplied span became constructor identity or changed its diagnostic anchor", parse_messages.str());
        for (std::size_t prior = 0; prior < at; ++prior)
            require(patches[at].identity != patches[prior].identity,
                    "distinct constructed output positions shared identity", parse_messages.str());
    }
}
