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
    EvaluationLimits limits;
    limits.steps = 100000000;
    limits.bytes = 256 * 1024 * 1024;
    limits.memory = 256 * 1024 * 1024;
    EvaluationLayout layout;
    layout.byte_order = order;
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; };
    auto execution = std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
        no_layout, no_layout, limits, layout);
    const auto* definitions = sources.add("recognition-definitions.x", R"cross(
typedef u16 Word;
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    $::meta::error($::syntax::span(input), "recognition executed an opaque expander");
    return $::quote { 0u32 };
}
syntax Nested : expression {
    prefix "nested"; match "(" value:expr ")"; expand expand;
}
syntax Nested;
)cross");
    auto declaration_tokens = Lexer(*definitions, diagnostics).lex();
    Parser owner(declaration_tokens, diagnostics, execution, bits);
    auto declarations = owner.parse();
    require(!diagnostics.errors(), "definition fixture did not parse");
    const auto context = owner.syntax_context(declaration_tokens.front().location);
    require(context && context->parse_environment, "fixture lost retained lookup");

    SyntaxState state(execution);
    const auto* grammar = sources.add("recognition-grammar.x", R"cross(
syntax Piece : rule { match value:expr ";"; }
syntax List : expression {
    prefix "list"; match "(" rows:separated1(rule(Piece), ",") ")"; expand expand;
}
)cross");
    const auto grammar_tokens = execution->prepare(*grammar);
    std::size_t position{};
    while (grammar_tokens[position].kind != TokenKind::End)
        require(state.declare(grammar_tokens, position, "", diagnostics), "grammar declaration failed");
    const SyntaxActivation activation{"List", {}, grammar_tokens.front().location};
    require(state.activate(std::span(&activation, 1), "", diagnostics), "grammar activation failed");
    const auto input = Lexer(*sources.add("recognition-input.x", "list (1u32;, 2u32;, 3u32;)"), diagnostics).lex();
    const auto* selected = state.selected(input.front(), false);
    require(selected, "list prefix was not selected");
    Parser input_parser(input, diagnostics, execution, bits);
    ContinuationSchedule* expected{};
    bool continuous = true;
    unsigned requests{};
    bool throw_first{}, fail_first{};
    SyntaxState::ParseFragment parse = [&](SyntaxPatternElement::Kind kind, std::size_t first)
        -> ContinuationTask<std::optional<SyntaxParsedFragment>> {
        co_await Witness{expected, continuous};
        ++requests;
        if (throw_first) { throw_first = false; throw std::runtime_error("recognition failure"); }
        if (fail_first) {
            fail_first = false;
            execution->resource_error(input[first].location, "recognition test resource failure");
            co_return std::nullopt;
        }
        co_return co_await input_parser.parse_syntax_fragment_async(kind, first);
    };
    const auto run = [&](bool legacy) -> ContinuationTask<std::optional<SyntaxState::Match>> {
        co_await Witness{expected, continuous};
        if (legacy) co_return state.match(*selected, input, 0, diagnostics, parse);
        co_return co_await state.match_async(*selected, input, 0, diagnostics, parse);
    };
    require(run(true).run().has_value() && !continuous && requests == 3,
        "legacy control did not detect a nested recognition pump");
    expected = nullptr; continuous = true; requests = 0;
    const auto matched = run(false).run();
    require(matched && continuous && requests == 3 && matched->end == input.size() - 1 &&
        matched->value->fields.front().records.size() == 3,
        "awaited matcher lost scheduler, order or repetition records");

    expected = nullptr; continuous = true; requests = 0; throw_first = true;
    try { (void)run(false).run(); require(false, "callback exception was swallowed"); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "recognition failure", "callback exception changed");
    }
    require(continuous && requests == 1, "exception did not stop later candidates");
    expected = nullptr; requests = 0;
    require(run(false).run().has_value() && continuous && requests == 3,
        "fresh matcher failed after callback exception");
    const auto errors = diagnostics.errors();
    const auto resources = execution->resource_errors();
    expected = nullptr; requests = 0; fail_first = true;
    require(!run(false).run() && continuous && requests == 1 &&
        diagnostics.errors() == errors + 1 && execution->resource_errors() == resources + 1,
        "resource failure was hidden or later captures ran");
    expected = nullptr; requests = 0;
    require(run(false).run().has_value() && requests == 3 && diagnostics.errors() == errors + 1,
        "fresh recognition did not rebase its resource epoch");

    // Real nested captures use the definition's active syntax and never execute
    // it. Keep production depth limits, with ample cumulative work/storage.
    std::string nested = "1u32";
    for (unsigned depth = 0; depth < 120; ++depth) nested = "nested(" + nested + ")";
    auto nested_tokens = Lexer(*sources.add("recognition-nested.x", nested), diagnostics).lex();
    const auto parsed = Parser::parse_syntax_tokens_async(SyntaxParseCategory::Expression,
        nested_tokens, context, diagnostics).run();
    require(parsed && diagnostics.errors() == errors + 1, "deep opaque recognition failed or executed code");
    const auto projected = syntax_node_tokens(*parsed);
    require(projected.size() == nested_tokens.size() - 1, "nested recognition lost input tokens");

    for (const auto category : {SyntaxParseCategory::Declaration, SyntaxParseCategory::FunctionHeader,
            SyntaxParseCategory::FunctionDeclaration, SyntaxParseCategory::FunctionDefinition}) {
        std::string text = "Word header<T>(in T input)";
        if (category == SyntaxParseCategory::Declaration || category == SyntaxParseCategory::FunctionDeclaration)
            text += ";";
        else if (category == SyntaxParseCategory::FunctionDefinition) text += " { return 1u16; }";
        auto tokens = Lexer(*sources.add("recognition-header.x", text), diagnostics).lex();
        require(Parser::parse_syntax_tokens_async(category, std::move(tokens), context, diagnostics).run() != nullptr,
            "public header/declaration recognition failed");
    }
    const auto type_input = Lexer(*sources.add("recognition-type.x", "Word"), diagnostics).lex();
    TokenSequence meta_input;
    meta_input.emplace_back(type_input.front());
    require(execution->parse_tokens_async(SyntaxParseCategory::Type, meta_input, context,
        type_input.front().location).run() != nullptr, "meta parse callback did not reach awaited public parsing");
    require(diagnostics.errors() == errors + 1, "inspection emitted an unrelated diagnostic");
}
} // namespace

int main() {
    check(64, cross::EvaluationByteOrder::Little);
    check(32, cross::EvaluationByteOrder::Big);
    check(32, cross::EvaluationByteOrder::Little);
    check(64, cross::EvaluationByteOrder::Big);
}
