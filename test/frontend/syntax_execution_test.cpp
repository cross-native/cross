// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>
#include <stdexcept>

namespace {
using namespace cross;

struct Witness {
    ContinuationSchedule*& expected;
    bool& continuous;
    bool await_ready() const noexcept { return false; }
    template<class Promise>
    bool await_suspend(std::coroutine_handle<Promise> frame) const noexcept {
        if (!expected) expected = frame.promise().schedule;
        else if (expected != frame.promise().schedule) continuous = false;
        return false;
    }
    void await_resume() const noexcept {}
};

void check(unsigned bits, EvaluationByteOrder order) {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics(messages);
    const auto require = [&](bool condition, const char* detail,
        std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << bits << ':' << static_cast<unsigned>(order) << ':' << at.line()
                      << ": " << detail << '\n' << messages.str();
            std::abort();
        }
    };
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned parses{}, layouts{};
    bool throw_first{};
    LayoutQuery layout_query = [&](const TypePtr& type)
        -> ContinuationTask<std::optional<std::uint64_t>> {
        co_await Witness{expected, continuous};
        ++layouts;
        if (throw_first) { throw_first = false; throw std::runtime_error("execution callback failure"); }
        co_return type && type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Uptr
            ? bits / 8U : 2U;
    };
    EvaluationLimits limits;
    EvaluationLayout layout;
    layout.byte_order = order;
    auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
        layout_query, layout_query, limits, layout);
    const auto* fixture = sources.add("execution-definitions.x", R"cross(
typedef u16 Word;
namespace Qualified {
    typedef u16 Word;
    [[macro]] static $::meta::tokens execute_qualified(in $::meta::tokens input) {
        if (sizeof(uptr) == 0uptr) return $::quote { 0u32 };
        return input;
    }
}
static $::meta::tokens from_tokens(in $::meta::tokens input) {
    if (sizeof(uptr) == 0uptr) return $::quote { 0u32 };
    return input;
}
static $::meta::tokens from_match(in $::meta::syntax_match input) {
    return $::meta::tokens($::meta::parse("type", $::syntax::input(input), $::syntax::context(input)));
}
[[macro]] static $::meta::tokens execute(in $::meta::tokens input) {
    if (sizeof(uptr) == 0uptr) return $::quote { 0u32 };
    return input;
}
[[syntax_expander]] static $::meta::tokens execute_match(in $::meta::syntax_match input) {
    if (sizeof(uptr) == 0uptr) return $::quote { 0u32 };
    return $::syntax::input(input);
}
)cross");
    const auto definitions = Lexer(*fixture, diagnostics).lex();
    Parser owner(definitions, diagnostics, execution, bits);
    auto declarations = owner.parse();
    require(!diagnostics.errors() && declarations.functions.size() == 2,
        "execution fixture did not parse");
    const auto captured = owner.syntax_context(definitions.front().location);
    require(captured && captured->parse_environment, "fixture lost its retained environment");
    auto context = std::make_shared<SyntaxContext>(*captured);
    context->kind = SyntaxContext::Kind::DefinitionSite;
    context->definition = definitions.front().location;
    const auto input_tokens = Lexer(*sources.add("execution-input.x", "Word"), diagnostics).lex();
    context->invocation = input_tokens.front().location;
    TokenSequence input{MetaToken(input_tokens.front())};
    auto match = std::make_shared<SyntaxMatchValue>();
    match->input = input;
    match->context = context;
    const SyntaxParseCallback parse = [&](SyntaxParseCategory category, const TokenSequence& tokens,
        std::shared_ptr<const SyntaxContext> lookup, SourceLocation location)
        -> ContinuationTask<std::shared_ptr<const SyntaxNode>> {
        co_await Witness{expected, continuous};
        ++parses;
        if (throw_first) { throw_first = false; throw std::runtime_error("execution callback failure"); }
        co_return co_await execution->parse_tokens_async(category, tokens, std::move(lookup), location);
    };
    const auto run = [&](bool syntax, bool legacy, EvaluationLimits budget)
        -> ContinuationTask<std::optional<TokenSequence>> {
        co_await Witness{expected, continuous};
        const auto& function = *declarations.functions[syntax ? 1 : 0];
        if (legacy) {
            if (syntax) co_return evaluate_syntax_body(function, match, bits,
                layout_query, layout_query, context, diagnostics, budget, layout, parse, context, &declarations);
            co_return evaluate_procedural_body(function, input, bits,
                layout_query, layout_query, context, diagnostics, budget, layout, parse, context, &declarations);
        }
        // A temporary callback is destroyed before the lazy task is awaited.
        // The async entry must own it, not retain a caller's default/reference.
        if (syntax) {
            auto task = evaluate_syntax_body_async(function, match, bits,
                layout_query, layout_query, context, diagnostics, budget, layout,
                SyntaxParseCallback(parse), context, &declarations);
            co_return co_await task;
        }
        auto task = evaluate_procedural_body_async(function, input, bits,
            layout_query, layout_query, context, diagnostics, budget, layout,
            SyntaxParseCallback(parse), context, &declarations);
        co_return co_await task;
    };
    const auto reset = [&] { expected = nullptr; continuous = true; parses = layouts = 0; };
    for (bool syntax : {false, true}) {
        reset();
        require(run(syntax, true, limits).run().has_value() && !continuous &&
            (syntax ? parses == 1 : layouts > 0),
            "legacy body-entry control did not detect its nested pump");
        reset();
        const auto result = run(syntax, false, limits).run();
        require(result && continuous && (syntax ? parses == 1 : layouts > 0) &&
            result->size() == 1 && result->front().text == "Word",
            "awaited body entry lost its scheduler, callback or retained type");
        reset(); throw_first = true;
        try { (void)run(syntax, false, limits).run(); require(false, "callback exception was swallowed"); }
        catch (const std::runtime_error& error) {
            require(std::string_view(error.what()) == "execution callback failure", "callback exception changed");
        }
        require(continuous && (syntax ? parses == 1 : layouts == 1),
            "throwing callback escaped the caller scheduler");
        reset();
        require(run(syntax, false, limits).run().has_value() && continuous &&
            (syntax ? parses == 1 : layouts > 0),
            "fresh body execution failed after callback exception");
        const auto errors = diagnostics.errors();
        const auto resources = declarations.evaluation_resource_errors;
        auto exhausted = limits;
        exhausted.steps = 1;
        reset();
        require(!run(syntax, false, exhausted).run() && continuous && parses == 0 && layouts == 0 &&
            diagnostics.errors() == errors + 1 && declarations.evaluation_resource_errors == resources + 1,
            "resource failure was duplicated or executed a later callback");
        reset();
        require(run(syntax, false, limits).run().has_value() && continuous &&
            (syntax ? parses == 1 : layouts > 0) &&
            diagnostics.errors() == errors + 1,
            "fresh body execution did not rebase resource history");
    }
    for (bool syntax : {false, true}) {
        const auto id = execution->find_function(syntax ? "execute_match" : "execute", "", {}, syntax);
        require(id.has_value(), "registered expansion function was not found");
        const auto expand = [&](bool legacy) -> ContinuationTask<std::optional<SyntaxExecution::Output>> {
            co_await Witness{expected, continuous};
            if (legacy) co_return execution->expand(*id, input, syntax ? match : nullptr,
                context->invocation, "", {}, {}, captured->parse_environment);
            co_return co_await execution->expand_async(*id, input, syntax ? match : nullptr,
                context->invocation, "", {}, {}, captured->parse_environment);
        };
        reset();
        require(expand(true).run().has_value() && !continuous && layouts > 0,
            "legacy expansion control did not detect a nested layout pump");
        reset();
        const auto expanded = expand(false).run();
        require(expanded && continuous && layouts > 0 && expanded->tokens.front().text == "Word",
            "connected expansion entry lost scheduler, model layout or textual output");
    }

    // Adjacent token fragments are exposed by name/type/declarator parsing,
    // not by executing opaque capture input. Macro layout callbacks must stay
    // on the same pump as the ordinary declaration/body parser.
    const auto parse_composed = [&](std::string_view source, bool legacy)
        -> ContinuationTask<std::unique_ptr<FunctionDecl>> {
        co_await Witness{expected, continuous};
        const auto* file = sources.add("execution-composed.x", std::string(source));
        Parser parser(Lexer(*file, diagnostics).lex(), diagnostics, execution, bits);
        Program parsed;
        if (legacy) co_return parser.parse_expansion_declaration(captured, parsed);
        co_return co_await parser.parse_expansion_declaration_async(captured, parsed);
    };
    for (const auto source : {
        "static u32 probe() { return sizeof(Word execute!(*)); }",
        "static u32 probe() { return sizeof(Qualified execute!(:: Word) execute!(*)); }",
        "static u32 probe() { Word execute!(*) value; return 0u32; }",
        "static u32 probe(in Qualified execute!(:: Word) execute!(*) value) { return 0u32; }",
        "static u32 probe() { using Qualified execute!(); return sizeof(Word); }",
        "static u32 probe() { return sizeof(Qualified execute!(:: execute_qualified !(Word *))); }",
        "static u32 probe() { return execute!(13u32); }",
        "static u32 probe() { return 1u32 execute!(+ 2u32); }",
        "static u32 probe() { execute!(u32 value = 3u32; value += 2u32;) return value; }",
        "static u32 probe() { u32 values[2u32] execute!(= {3u32, 4u32}); return values[0u32]; }",
        "static u32 probe() { if execute!((1u32)) return 13u32; return 0u32; }",
        "static Word probe<execute!(Word)>(in Word value) { return value; }",
        "static u32 probe(in execute!(Word) value) { return 0u32; }",
        "static u32 probe() { execute!(return 13u32;) }",
        "execute!(static u32 probe(in Word value) { return 0u32; })"
    }) {
        const auto errors = diagnostics.errors();
        reset();
        require(parse_composed(source, true).run() && !continuous && layouts > 0,
            "legacy composed-parser control did not detect a nested pump");
        reset();
        auto function = parse_composed(source, false).run();
        require(function && function->body && continuous && layouts > 0 && diagnostics.errors() == errors,
            "composed name/type/declarator exposure lost its scheduler or source ownership");
    }
    const auto composed_source = "static u32 probe() { return sizeof(Qualified execute!(:: Word) execute!(*)); }";
    reset(); throw_first = true;
    try { (void)parse_composed(composed_source, false).run(); require(false, "name-lookahead exception was swallowed"); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "execution callback failure", "name-lookahead exception changed");
    }
    require(continuous && layouts == 1, "name-lookahead exception escaped the caller scheduler");
    reset();
    require(parse_composed(composed_source, false).run() && continuous && layouts > 0,
        "fresh name lookahead failed after a callback exception");

    // The ordinary root loop and post-parse expansion-function validation
    // also belong to one scheduler, not one pump per item or cursor query.
    const auto parse_program = [&](bool legacy) -> ContinuationTask<Program> {
        co_await Witness{expected, continuous};
        const auto* file = sources.add("execution-program.x", R"cross(
execute!(static u32 first() { return 13u32; })
static u32 second() { return execute!(first()); }
)cross");
        Parser parser(Lexer(*file, diagnostics).lex(), diagnostics, execution, bits);
        if (legacy) co_return parser.parse();
        co_return co_await parser.parse_async();
    };
    const auto program_errors = diagnostics.errors();
    reset();
    require(parse_program(true).run().functions.size() == 2 && !continuous && layouts > 0,
        "legacy root-parser control did not detect a nested pump");
    reset();
    require(parse_program(false).run().functions.size() == 2 && continuous && layouts > 0 &&
        diagnostics.errors() == program_errors,
        "awaited root parser lost its scheduler, textual items or source ordering");
    reset(); throw_first = true;
    try { (void)parse_program(false).run(); require(false, "root cursor exception was swallowed"); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "execution callback failure", "root cursor exception changed");
    }
    require(continuous && layouts == 1, "root cursor exception escaped the caller scheduler");
    reset();
    require(parse_program(false).run().functions.size() == 2 && continuous && layouts > 0,
        "fresh root parser failed after a callback exception");

    // Context attachment is invocation-local copy-on-change, not a spelling
    // rewrite. Repeated edges must share the same changed child; settled nodes
    // and their token lookup context must retain their original identity.
    auto settled = std::make_shared<SyntaxNode>();
    settled->context = captured;
    settled->tokens = input;
    settled->tokens.front().origin.context = captured;
    auto raw = std::make_shared<SyntaxNode>();
    raw->tokens = input;
    auto pair = std::make_shared<SyntaxNode>();
    pair->kind = SyntaxNode::Kind::Group;
    pair->children = {raw, raw, settled};
    auto leaf = std::make_shared<SyntaxMatchValue>();
    leaf->input = input;
    leaf->fields.push_back({});
    leaf->fields.front().node = pair;
    auto shared = std::make_shared<SyntaxMatchValue>();
    shared->fields.push_back({});
    shared->fields.front().records = {leaf, leaf};
    auto attached = syntax_attach_context(shared, context);
    const auto& copies = attached->fields.front().records;
    const auto& children = copies.front()->fields.front().node->children;
    require(attached != shared && copies[0] == copies[1] && copies[0] != leaf &&
        children[0] == children[1] && children[0] != raw && children[2] == settled &&
        children[0]->tokens.front().origin.context == context &&
        children[0]->tokens.front().origin.identity == input.front().origin.identity &&
        !shared->context && !raw->tokens.front().origin.context,
        "context attachment lost sharing, identity, retained lookup or source immutability");
    auto other = std::make_shared<SyntaxContext>(*context);
    require(syntax_attach_context(attached, other) == attached,
        "another invocation retargeted an already contextualized tree");
    const auto fresh = syntax_attach_context(shared, other);
    require(fresh != attached && fresh->fields.front().records.front()->context == other,
        "context attachment reused a previous invocation's memo table");

    // These are host-constructed graphs, not source accepted beyond the
    // language depth limit. Keep every original owner during construction and
    // tear down in parent-first order so shared_ptr destruction is not the test.
    std::vector<std::shared_ptr<SyntaxNode>> original_nodes;
    std::vector<std::shared_ptr<SyntaxMatchValue>> original_matches{leaf};
    for (unsigned depth = 0; depth < 8000; ++depth) {
        auto node = std::make_shared<SyntaxNode>();
        node->kind = SyntaxNode::Kind::Group;
        node->children.push_back(settled);
        node->match = original_matches.back();
        auto record = std::make_shared<SyntaxMatchValue>();
        record->fields.push_back({});
        record->fields.front().node = node;
        original_nodes.push_back(std::move(node));
        original_matches.push_back(std::move(record));
    }
    auto deep = syntax_attach_context(original_matches.back(), context);
    auto cursor = deep;
    for (unsigned depth = 0; depth < 8000; ++depth) {
        require(cursor->context == context && cursor->fields.front().node->context == context &&
            cursor->fields.front().node->children.front() == settled,
            "deep mixed graph lost context or settled node identity");
        cursor = cursor->fields.front().node->match;
    }
    require(cursor->fields.front().node->children[0] == cursor->fields.front().node->children[1],
        "deep mixed graph lost its shared leaf");
    cursor.reset();
    // The separate attachment has its own copies; stop at its original leaf
    // shape rather than relying on identity from another invocation.
    while (deep->fields.front().node->match) {
        auto next = deep->fields.front().node->match;
        deep.reset();
        deep = std::move(next);
    }
    deep.reset();
    for (std::size_t index = original_nodes.size(); index-- > 0;) {
        original_matches[index + 1].reset();
        original_nodes[index].reset();
    }
    original_matches.clear();

    std::vector<std::shared_ptr<SyntaxMatchValue>> records;
    auto base = std::make_shared<SyntaxMatchValue>();
    base->context = context;
    base->input = input;
    records.push_back(std::move(base));
    for (unsigned depth = 0; depth < 8000; ++depth) {
        auto record = std::make_shared<SyntaxMatchValue>();
        record->context = context;
        record->input = input;
        record->fields.push_back({});
        record->fields.front().name = "next";
        record->fields.front().records.push_back(records.back());
        records.push_back(std::move(record));
    }
    const auto charge = [&](EvaluationLimits budget) -> ContinuationTask<std::optional<TokenSequence>> {
        co_await Witness{expected, continuous};
        co_return co_await evaluate_syntax_body_async(*declarations.functions[1], records.back(), bits,
            layout_query, layout_query, context, diagnostics, budget, layout, parse, context, &declarations);
    };
    auto ample = limits;
    ample.steps = 100000000;
    ample.bytes = ample.memory = 268435456;
    reset();
    require(charge(ample).run().has_value() && continuous && parses == 1,
        "deep input charging failed or reached a different scheduler");
    const auto errors = diagnostics.errors();
    const auto resources = declarations.evaluation_resource_errors;
    auto limited = ample;
    limited.bytes = limited.memory = 1048576;
    reset();
    require(!charge(limited).run() && parses == 0 && diagnostics.errors() == errors + 1 &&
        declarations.evaluation_resource_errors == resources + 1,
        "deep input exhaustion was duplicated or executed the body");
    reset();
    require(charge(ample).run().has_value() && continuous && parses == 1 &&
        diagnostics.errors() == errors + 1,
        "fresh deep input charging did not recover its resource epoch");
    for (auto at = records.rbegin(); at != records.rend(); ++at) at->reset();
}
} // namespace

int main() {
    check(32, cross::EvaluationByteOrder::Little);
    check(64, cross::EvaluationByteOrder::Little);
    check(32, cross::EvaluationByteOrder::Big);
    check(64, cross::EvaluationByteOrder::Big);
}
