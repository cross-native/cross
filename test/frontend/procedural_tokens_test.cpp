// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/procedural.hpp"
#include "frontend/syntax.hpp"
#include "frontend/token_tree.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::abort();
    }
}

void token_tree_checks(cross::SourceManager& sources, cross::Diagnostics& diagnostics) {
    using namespace cross;
    const auto lex = [&](std::string_view text) {
        const auto* source = sources.add("token-tree.x", std::string(text));
        TokenSequence result;
        for (const auto& token : Lexer(*source, diagnostics).lex())
            if (token.kind != TokenKind::End) result.emplace_back(token);
        return result;
    };
    auto tokens = lex("alpha (beta [3u32] {\"x\"}) [[attr(value)]] () [4u32] {beta}");
    std::size_t work{}, storage{};
    std::vector<TokenTreeRange> roots;
    const TokenTreeScanHooks hooks{
        [&](std::size_t index) { require(index == work++, "token traversal changed source order"); return true; },
        [&] { ++storage; return true; },
        [&](TokenTreeRange range) { roots.push_back(range); return true; }};
    require(scan_token_trees(tokens, 2, TokenTreeBalance::Required, hooks) == TokenTreeScanError::None,
            "balanced token-tree scan failed");
    require(work == tokens.size() && storage == 2 && roots.size() == 6,
            "token-tree work/high-water accounting changed");
    const TokenDelimiter expected[] = {TokenDelimiter::None, TokenDelimiter::Parenthesis,
        TokenDelimiter::Attribute, TokenDelimiter::Parenthesis, TokenDelimiter::Bracket, TokenDelimiter::Brace};
    std::size_t cursor{};
    for (std::size_t index = 0; index < roots.size(); ++index) {
        const auto& root = roots[index];
        require(root.begin == cursor && root.end > root.begin && root.delimiter == expected[index],
                "token-tree ranges lost delimiter identity or source coverage");
        cursor = root.end;
    }
    require(cursor == tokens.size() && roots[3].end == roots[3].begin + 2,
            "empty group or final token-tree range changed");
    const auto group = roots[1];
    const auto children = std::span<const MetaToken>(tokens).subspan(group.begin + 1, group.end - group.begin - 2);
    std::vector<TokenTreeRange> child_ranges;
    require(scan_token_trees(children, 1, TokenTreeBalance::Required,
        {{}, {}, [&](TokenTreeRange range) { child_ranges.push_back(range); return true; }}) == TokenTreeScanError::None &&
        child_ranges.size() == 3 && child_ranges[0].delimiter == TokenDelimiter::None &&
        child_ranges[1].delimiter == TokenDelimiter::Bracket && child_ranges[2].delimiter == TokenDelimiter::Brace,
        "direct group-child inspection flattened nested groups");
    require(&children.front() == &tokens[group.begin + 1] &&
            children.front().origin.identity == tokens[group.begin + 1].origin.identity,
            "group inspection copied or rebound input tokens");

    for (const auto source : {"(", "]", "([)]", "[[x]", "{[}"}) {
        auto invalid = lex(source);
        const auto failure = scan_token_trees(invalid, 16, TokenTreeBalance::Required, {});
        require(failure == (std::string_view(source) == "(" ? TokenTreeScanError::UnterminatedGroup
                                                            : TokenTreeScanError::UnmatchedDelimiter),
                "unbalanced token trees were accepted by complete inspection");
        require(scan_token_trees(invalid, 16, TokenTreeBalance::Fragments, {}) == TokenTreeScanError::None,
                "explicitly projected delimiter fragments became uncomposable");
    }
    require(scan_token_trees({}, 0, TokenTreeBalance::Required, {}) == TokenTreeScanError::None,
            "empty token sequence requires a delimiter budget");
    require(scan_token_trees(lex("word"), 0, TokenTreeBalance::Required, {}) == TokenTreeScanError::None,
            "lexical leaf consumed delimiter depth");
    require(scan_token_trees(lex("()"), 0, TokenTreeBalance::Required, {}) == TokenTreeScanError::DepthLimit,
            "zero delimiter depth admitted a group");

    // Splices remain indivisible even if their display text resembles syntax.
    // Their own tree validity/storage is checked by the syntax layer, not by
    // descending into the node during token-sequence inspection.
    auto splice = lex("marker");
    auto node = std::make_shared<SyntaxNode>();
    node->kind = SyntaxNode::Kind::Deferred;
    splice.front().kind = TokenKind::StructuredSplice;
    splice.front().text = "(";
    splice.front().splice = node;
    const auto identity = splice.front().origin.identity;
    std::size_t splice_roots{};
    require(scan_token_trees(splice, 0, TokenTreeBalance::Required,
        {{}, {}, [&](TokenTreeRange range) {
            ++splice_roots;
            return range == TokenTreeRange{0, 1, TokenDelimiter::None};
        }}) == TokenTreeScanError::None && splice_roots == 1 &&
        splice.front().splice == node && splice.front().origin.identity == identity,
        "opaque splice was reparsed, flattened, or rebound");

    work = storage = 0;
    require(scan_token_trees(tokens, 2, TokenTreeBalance::Required,
        {[&](std::size_t) { ++work; return false; }, [&] { ++storage; return true; }, {}}) ==
            TokenTreeScanError::Cancelled && work == 1 && storage == 0,
        "work cancellation continued inspecting tokens");
    work = storage = 0;
    require(scan_token_trees(tokens, 2, TokenTreeBalance::Required,
        {[&](std::size_t) { ++work; return true; }, [&] { ++storage; return false; }, {}}) ==
            TokenTreeScanError::Cancelled && work == 2 && storage == 1,
        "storage cancellation retained or visited later groups");
    work = 0;
    require(scan_token_trees(tokens, 2, TokenTreeBalance::Required,
        {[&](std::size_t) { ++work; return true; }, {}, [](TokenTreeRange) { return false; }}) ==
            TokenTreeScanError::Cancelled && work == 1,
        "element callback cancellation scanned another element");

    const auto deep = lex(std::string(4096, '(') + std::string(4096, ')'));
    require(scan_token_trees(deep, 4096, TokenTreeBalance::Required, {}) == TokenTreeScanError::None &&
            scan_token_trees(deep, 4095, TokenTreeBalance::Required, {}) == TokenTreeScanError::DepthLimit &&
            scan_token_trees(deep, 4095, TokenTreeBalance::Fragments, {}) == TokenTreeScanError::DepthLimit,
            "iterative token scanning lost its exact depth limit");
}

void splice_storage_checks(cross::SourceManager& sources) {
    using namespace cross;
    std::ostringstream parsed_messages;
    Diagnostics parsed_diagnostics(parsed_messages);
    const auto* source = sources.add("splice-storage.x", R"(
        [[macro]] static $::meta::tokens forward(in $::meta::tokens input) { return input; }
        [[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
            return $::syntax::input(input);
        }
    )");
    const auto tokens = Lexer(*source, parsed_diagnostics).lex();
    Parser parser(tokens, parsed_diagnostics);
    const auto program = parser.parse();
    require(parsed_diagnostics.errors() == 0 && program.functions.size() == 2,
            "failed to parse retained-splice storage helpers");
    const auto location = tokens.front().location;
    auto context = std::make_shared<SyntaxContext>();
    context->kind = SyntaxContext::Kind::DefinitionSite;
    context->invocation = location;
    auto payload = std::make_shared<SyntaxNode>();
    payload->kind = SyntaxNode::Kind::Token;
    MetaToken value;
    value.kind = TokenKind::Identifier;
    value.text.assign(8192, 'x');
    payload->tokens.push_back(value);
    MetaToken marker;
    marker.kind = TokenKind::StructuredSplice;
    marker.text = "__cross_syntax_splice";
    marker.origin = token_origin(location);
    marker.splice = payload;
    auto wrapper = std::make_shared<SyntaxNode>();
    wrapper->kind = SyntaxNode::Kind::Deferred;
    wrapper->tokens = {marker};
    marker.splice = wrapper;
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    enum class Input { Macro, Match, FieldTokens, FieldNode, NestedMatch };
    for (const auto width : {32U, 64U, 128U}) {
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big}) {
            for (const auto input : {Input::Macro, Input::Match, Input::FieldTokens,
                                     Input::FieldNode, Input::NestedMatch}) {
                for (unsigned budget = 0; budget < 3; ++budget) {
                    EvaluationLimits limits;
                    limits.memory = budget == 1 ? 4096 : 1024 * 1024;
                    limits.bytes = budget == 2 ? 4096 : 1024 * 1024;
                    EvaluationLayout layout;
                    layout.byte_order = order;
                    std::ostringstream messages;
                    Diagnostics diagnostics(messages);
                    Program declarations;
                    std::optional<TokenSequence> result;
                    if (input == Input::Macro) {
                        result = evaluate_procedural_body(*program.functions[0], {marker}, width,
                            no_layout, no_layout, context, diagnostics, limits, layout, {}, {}, &declarations);
                    } else {
                        auto match = std::make_shared<SyntaxMatchValue>();
                        if (input == Input::Match) match->input = {marker};
                        else {
                            match->fields.emplace_back();
                            auto& field = match->fields.back();
                            field.name = "retained";
                            if (input == Input::FieldTokens) field.tokens = {marker};
                            else if (input == Input::FieldNode) field.node = wrapper;
                            else {
                                auto nested = std::make_shared<SyntaxMatchValue>();
                                nested->input = {marker};
                                field.records.push_back(std::move(nested));
                            }
                        }
                        result = evaluate_syntax_body(*program.functions[1], match, width,
                            no_layout, no_layout, context, diagnostics, limits, layout, {}, {}, &declarations);
                    }
                    if (budget == 0) {
                        require(result && diagnostics.errors() == 0 && declarations.evaluation_resource_errors == 0,
                                "retained-splice input failed with sufficient logical storage");
                        if (input == Input::Macro || input == Input::Match)
                            require(result->size() == 1 && result->front().splice == wrapper &&
                                    result->front().origin.identity == marker.origin.identity,
                                    "input storage accounting flattened or rebound a structured splice");
                    } else {
                        const auto expected = budget == 1 ? "translation-time meta memory budget exceeded"
                            : "public syntax tree exceeds translation-time storage capacity";
                        require(!result && diagnostics.errors() != 0 && declarations.evaluation_resource_errors == 1 &&
                                messages.str().find(expected) != std::string::npos &&
                                messages.str().find("splice-storage.x:") != std::string::npos,
                                "retained-splice input bypassed its byte/memory budget or lost its diagnostic");
                        const auto errors = diagnostics.errors();
                        const auto recovered = evaluate_procedural_body(*program.functions[0], {}, width,
                            no_layout, no_layout, context, diagnostics, limits, layout, {}, {}, &declarations);
                        require(recovered && recovered->empty() && diagnostics.errors() == errors &&
                                declarations.evaluation_resource_errors == 1,
                            "an earlier evaluator resource failure poisoned an independent invocation");
                    }
                }
            }
        }
    }
}
}

int main() {
    using namespace cross;
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    token_tree_checks(sources, diagnostics);
    splice_storage_checks(sources);
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> {
        return std::nullopt;
    };
    const std::string source = R"(
namespace definitions {
    [[macro]] static $::meta::tokens duplicate(in $::meta::tokens input) {
        return $::meta::concat(input, input);
    }
    [[macro]] static $::meta::tokens forward(in $::meta::tokens input) {
        return $::quote { duplicate! { $::unquote(input) } quoted };
    }
    [[macro]] static $::meta::tokens generated(in $::meta::tokens input) {
        $::meta::tokens tokens = $::quote { $::patch(3u32) };
        return $::meta::concat(tokens, tokens);
    }
    [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) {
        return $::meta::parse("parsed_identifier");
    }
    [[macro]] static $::meta::tokens generated_forward(in $::meta::tokens input) {
        return $::quote { duplicate! { freshly_forwarded $::patch(5u32) } };
    }
    [[macro]] static $::meta::tokens separated(in $::meta::tokens input) {
        return $::meta::concat($::meta::parse("+"), $::meta::parse("+"));
    }
    [[macro]] static $::meta::tokens retarget(in $::meta::tokens input) {
        return $::meta::call_site(input);
    }
    [[macro]] static $::meta::tokens fresh(in $::meta::tokens input) {
        $::meta::tokens first = $::meta::gensym("private");
        $::meta::tokens second = $::meta::gensym("private");
        $::meta::tokens retargeted = $::meta::call_site(first);
        return $::meta::concat($::meta::concat(first, retargeted), second);
    }
}
namespace caller {
    [[macro]] static $::meta::tokens duplicate(in $::meta::tokens input) {
        return $::quote { wrong };
    }
    definitions::forward! { copied $::patch(1u32) }
    definitions::generated! {}
    definitions::generated! {}
    definitions::parsed! {}
    definitions::generated_forward! {}
    definitions::separated! {}
    definitions::retarget! { caller_identifier }
    definitions::fresh! {}
}
)";
    std::vector<TokenIdentity> first_unit;
    for (const unsigned width : {32U, 64U}) {
        const auto* expanded = expand_procedural_macros(
            sources, "tokens.x", source, diagnostics, width, no_layout, no_layout);
        require(diagnostics.errors() == 0, output.str().c_str());
        const auto tokens = Lexer(*expanded, diagnostics).lex();
        std::vector<TokenOrigin> copied;
        std::vector<TokenOrigin> patches;
        std::vector<TokenOrigin> quoted;
        std::vector<TokenOrigin> parsed;
        std::vector<TokenOrigin> forwarded;
        std::vector<TokenOrigin> retargeted;
        std::vector<TokenOrigin> fresh;
        unsigned plus_count{};
        for (const auto& token : tokens) {
            require(!token.is("wrong"), "quoted macro name rebound at invocation site");
            require(!token.is("++"), "concatenation fused adjacent punctuators");
            if (token.is("+")) ++plus_count;
            if (token.is("copied")) copied.push_back(token_origin(token.location));
            if (token.is("$::patch")) patches.push_back(token_origin(token.location));
            if (token.is("quoted")) quoted.push_back(token_origin(token.location));
            if (token.is("parsed_identifier")) parsed.push_back(token_origin(token.location));
            if (token.is("freshly_forwarded")) forwarded.push_back(token_origin(token.location));
            if (token.is("caller_identifier")) retargeted.push_back(token_origin(token.location));
            if (token.is("private")) fresh.push_back(token_origin(token.location));
            if (token.kind != TokenKind::End) {
                const auto* origin = expanded->token_origin_at(token.location.offset);
                require(origin != nullptr, "serialized token lost its origin");
                require(origin->identity.source_unit != nullptr, "token identity has no source unit");
            }
        }
        require(copied.size() == 2 && patches.size() == 8 && quoted.size() == 1 && parsed.size() == 1 &&
                forwarded.size() == 2 && retargeted.size() == 1 && fresh.size() == 3 && plus_count == 2,
                "unexpected expansion token counts");
        require(copied[0].identity == copied[1].identity, "copied token lost lexical identity");
        require(copied[0].context == copied[1].context, "copied token lost lookup context");
        require(copied[0].context && copied[0].context->kind == SyntaxContext::Kind::CallSite &&
                copied[0].context->name_space == "caller", "input lost its call-site context");
        require(copied[0].identity.expansion.value == 0, "input token was treated as newly constructed");
        require(copied[0].lookup_mode == TokenOrigin::LookupMode::Lexical,
                "ordinary copied input acquired explicit invocation lookup");
        require(copied[0].span.file->text.substr(copied[0].span.offset, 6) == "copied",
                "copied token did not preserve its supplied span");
        require(retargeted[0].identity.expansion.value == 0 &&
                retargeted[0].span.file->text.substr(retargeted[0].span.offset, 17) == "caller_identifier" &&
                retargeted[0].context &&
                retargeted[0].context->kind == SyntaxContext::Kind::CallSite &&
                retargeted[0].context->name_space == "caller",
                "call_site changed copied identifier identity/span or lost invocation context");
        require(retargeted[0].lookup_mode == TokenOrigin::LookupMode::Invocation,
                "call_site did not preserve its explicit lookup policy");
        require(fresh[0].fresh && fresh[0].fresh == fresh[1].fresh &&
                fresh[0].fresh != fresh[2].fresh &&
                fresh[0].fresh->prefix == "private" &&
                fresh[0].context && fresh[0].context->kind == SyntaxContext::Kind::DefinitionSite &&
                fresh[1].context && fresh[1].context->kind == SyntaxContext::Kind::CallSite &&
                fresh[1].context->name_space == "caller" &&
                fresh[0].span.file == fresh[1].span.file &&
                fresh[0].span.offset == fresh[1].span.offset,
                "gensym did not preserve opaque fresh identity across token copies");
        require(fresh[0].lookup_mode == TokenOrigin::LookupMode::Lexical &&
                fresh[1].lookup_mode == TokenOrigin::LookupMode::Invocation &&
                fresh[2].lookup_mode == TokenOrigin::LookupMode::Lexical,
                "retargeting a fresh identifier changed another copy's lookup policy");
        require(patches[0].identity == patches[1].identity, "copied patch token lost lexical identity");
        require(patches[2].identity != patches[3].identity, "constructed output positions are not distinct");
        require(patches[2].identity.expansion == patches[3].identity.expansion,
                "one expansion received inconsistent identity");
        require(patches[2].identity.expansion != patches[4].identity.expansion,
                "separate invocations share expansion identity");
        require(patches[6].identity == patches[7].identity,
                "forwarding a generated patch token lost its outer expansion identity");
        require(forwarded[0].identity == forwarded[1].identity &&
                forwarded[0].identity.expansion == patches[6].identity.expansion &&
                forwarded[0].context == forwarded[1].context &&
                forwarded[0].context->kind == SyntaxContext::Kind::DefinitionSite,
                "nested input capture changed constructed token identity or context");
        for (const auto& value : {quoted[0], parsed[0]}) {
            require(value.context && value.context->kind == SyntaxContext::Kind::DefinitionSite &&
                    value.context->name_space == "definitions", "constructed token lost definition context");
            require(value.identity.expansion.value != 0, "constructed token has no expansion identity");
            require(value.span.file->text.substr(value.span.offset, 11) == "definitions",
                    "constructed token did not default to its invocation span");
        }
        if (width == 32) {
            for (const auto& patch : patches) first_unit.push_back(patch.identity);
        } else {
            for (std::size_t index = 0; index < patches.size(); ++index)
                require(patches[index].identity != first_unit[index], "primary source units share token identity");
        }
    }
    require(diagnostics.errors() == 0, output.str().c_str());

    // A default/unknown target must not silently inherit the host's flat code
    // addresses. Exercise the declared capability separately from any ABI name.
    for (const auto representation : {CodeAddressRepresentation::Opaque,
                                       CodeAddressRepresentation::Flat}) {
        for (const auto order : {EvaluationByteOrder::Little, EvaluationByteOrder::Big}) {
            for (const unsigned width : {32U, 64U, 128U}) {
                std::ostringstream messages;
                Diagnostics checked(messages);
                const LayoutQuery scalar_layout = [width](const TypePtr& type)
                    -> std::optional<std::uint64_t> {
                    if (type && type->kind == Type::Kind::Builtin &&
                        (type->builtin == BuiltinType::Label || type->builtin == BuiltinType::Uptr))
                        return width / 8;
                    return {};
                };
                const std::string numeric_source = R"(
[[macro]] static $::meta::tokens numeric(in $::meta::tokens input) {
    uptr bits = (uptr)0x112233445566778899aabbccddeeff00u128;
    label value = (label)bits;
    if ((uptr)value != bits || value != (label)bits) return $::quote { wrong };
    if (sizeof(label) != sizeof(uptr)) return $::quote { wrong };
    return $::quote { numeric_ok };
}
numeric!()
)";
                EvaluationLayout layout;
                layout.byte_order = order;
                layout.natural_alignment_limit = width / 8;
                layout.code_addresses = representation;
                const auto* expanded = expand_procedural_macros(sources, "numeric-label.x",
                    numeric_source, checked, width, scalar_layout, scalar_layout, {}, layout);
                if (representation == CodeAddressRepresentation::Opaque) {
                    require(checked.errors() != 0, "unknown target inherited numeric code-address semantics");
                    require(messages.str().find(
                        "target does not provide numeric code-address representation") != std::string::npos,
                        messages.str().c_str());
                } else {
                    require(checked.errors() == 0, messages.str().c_str());
                    bool found = false;
                    for (const auto& token : Lexer(*expanded, checked).lex()) {
                        require(!token.is("wrong"), "numeric label bits, width, or byte order changed");
                        found |= token.is("numeric_ok");
                    }
                    require(found, "numeric label macro did not expand");
                }
            }
        }
    }
}
