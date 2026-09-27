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

    // Matching preserves the actual written prefix and its bounded input.
    const auto* invocation = sources.add("invocation.x", "same ()");
    const auto invocation_tokens = Lexer(*invocation, diagnostics).lex();
    const auto* selected = state.selected(invocation_tokens.front(), false);
    require(selected && selected->name == "First", "wrong expression-family dispatch");
    const auto match = state.match(*selected, invocation_tokens, 0, diagnostics);
    require(match && match->end == 3 && match->value->input.front().text == "same",
            "raw input omitted its actual prefix or boundary");
    const auto errors = diagnostics.errors();
    auto output = execution->expand(match->expander, {}, match->value, invocation_tokens.front().location,
        "", {}, state.bindings());
    require(output && diagnostics.errors() == errors && output->tokens.front().text == "1u32",
            "target-independent syntax evaluation failed");
}
