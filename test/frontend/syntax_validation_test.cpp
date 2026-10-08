// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
using namespace cross;

struct Fixture {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics{messages};
    std::shared_ptr<SyntaxExecution> execution;
    SyntaxState state;
    unsigned source_number{};

    explicit Fixture(unsigned bits)
        : execution(std::make_shared<SyntaxExecution>(sources, diagnostics, bits,
            [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; },
            [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; },
            EvaluationLimits{}, EvaluationLayout{})), state(execution) {}

    void require(bool condition, const char* message) const {
        if (condition) return;
        std::cerr << message << '\n' << messages.str();
        std::exit(1);
    }
    std::vector<Token> tokens(std::string text) {
        return execution->prepare(*sources.add("validation-" + std::to_string(source_number++) + ".x",
                                               std::move(text)));
    }
    bool declare(std::string source, std::string_view context = {}) {
        const auto input = tokens(std::move(source));
        for (std::size_t at = 0; input[at].kind != TokenKind::End;)
            if (!state.declare(input, at, context, diagnostics)) return false;
        return true;
    }
    SyntaxFunctionId expander(std::string_view context = {}) {
        const auto input = tokens(R"(
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
})");
        std::size_t at{};
        require(execution->define_function(input, at, context, {}, state.bindings()) != nullptr,
                "expander definition failed");
        const auto id = execution->find_function("expand", context, {}, true);
        require(id.has_value(), "defined expander was not found");
        return *id;
    }
    bool activate(std::initializer_list<SyntaxActivation> entries, std::string_view context = {}) {
        return state.activate(std::span(entries.begin(), entries.size()), context, diagnostics);
    }
    SyntaxState::Match match(std::string source) {
        const auto input = tokens(std::move(source));
        const auto* definition = state.selected(input.front(), false);
        require(definition != nullptr, "activation did not install the expected prefix");
        const auto errors = diagnostics.errors();
        const auto result = state.match(*definition, input, 0, diagnostics);
        require(result && result->end + 1 == input.size() && diagnostics.errors() == errors,
                "bound grammar did not consume the expected invocation");
        return *result;
    }
};

void local_and_forward(unsigned bits) {
    Fixture f(bits);
    f.require(f.declare(R"(
syntax Unused : expression { prefix "unused"; match rule(Missing); expand missing; }
syntax Cycle : rule { match rule(Cycle); }
syntax A : rule { match branch:choice(stop:("end") | next:("a" rule(B))); }
syntax Owner : expression { prefix "forward"; match rule(A); expand expand; }
)"), "declaration prematurely resolved an unused or forward rule graph");
    f.require(f.diagnostics.errors() == 0 && f.state.bindings().empty(),
              "declaration diagnosed a graph or installed a prefix");
    f.require(f.declare(R"(
syntax B : rule { match branch:choice(stop:("end") | next:("b" rule(A))); }
)"), "productive mutual rule declaration failed");
    const auto expander = f.expander();
    f.require(f.activate({{"Owner", {}, {}}}), "forward/mutually recursive activation failed");
    f.require(f.match("forward a b a end").expander.value == expander.value,
              "forward activation selected the wrong expander");

    for (const auto* source : {
        "syntax Bad : rule { match value:ident value:literal; }",
        "syntax Bad : rule { match value:choice(same:(\"x\") | same:(\"y\")); }",
        "syntax Bad : rule { match value:repeat0(child:optional(\"x\")); }",
        "syntax Bad : rule { match value:repeat0(\"x\") \"x\"; }",
        "syntax Bad : rule { match value:separated0(\"x\", \"(\"); }",
        "syntax Bad : rule { match value:separated1(\"x\", \")\"); }",
        "syntax Bad : rule { match value:expr \"+\"; }",
        "syntax Bad : rule { match \"(\"; }"}) {
        const auto errors = f.diagnostics.errors();
        f.require(!f.declare(source) && f.diagnostics.errors() > errors,
                  "unused locally invalid syntax was accepted");
    }
    // Failed local declarations must not reserve their name in the registry.
    f.require(f.declare("syntax Bad : rule { match \"ok\"; }"),
              "invalid declaration poisoned a later valid declaration");
}

void graph_failure_and_stability(unsigned bits) {
    Fixture f(bits);
    f.expander();
    f.require(f.declare("syntax Leaf : rule { match part:optional(\"outer\"); }"),
              "nullable rule declaration failed");
    f.require(f.declare(R"SOURCE(
syntax Owner : expression { prefix "candidate"; match "(" parts:repeat0(rule(Leaf)) ")"; expand expand; }
)SOURCE", "Near::Deep"), "declaration bound an already visible nullable rule too early");
    const bool activated = f.activate({{"Near::Deep::Owner", {}, {}}});
    f.require(!activated && f.state.bindings().empty(),
              "nullable-body activation succeeded or leaked its prefix");
    f.require(f.messages.str().find("body may be nullable") != std::string::npos,
              "activation failed for a reason other than graph-dependent progress");

    f.require(f.declare("syntax Leaf : rule { match \"inner\"; }", "Near"),
              "closer rule declaration failed");
    const auto nearer_expander = f.expander("Near");
    f.require(f.activate({{"Near::Deep::Owner", {}, {}}}),
              "failed graph activation froze an earlier rule identity");
    f.require(f.match("candidate (inner inner)").expander.value == nearer_expander.value,
              "failed graph activation froze an earlier expander identity");

    // Successful activation, unlike declaration or failed activation, fixes IDs.
    f.require(f.declare("syntax Leaf : rule { match \"deep\"; }", "Near::Deep"),
              "later closest rule declaration failed");
    const auto closest_expander = f.expander("Near::Deep");
    f.require(closest_expander.value != nearer_expander.value, "expander identities are not distinct");
    f.require(f.activate({{"Near::Deep::Owner", std::string("again"), {}}}),
              "reactivation with an alias failed");
    f.require(f.match("again (inner)").expander.value == nearer_expander.value,
              "successful activation was retargeted by later closer declarations");
}

void multi_entry_rollback(unsigned bits) {
    Fixture f(bits);
    f.expander();
    f.require(f.declare(R"(
syntax Leaf : rule { match "outer"; }
syntax Conflict : expression { prefix "candidate"; match "conflict"; expand expand; }
)"), "outer declarations failed");
    f.require(f.declare(R"(
syntax Owner : expression { prefix "candidate"; match rule(Leaf); expand expand; }
syntax Pack : bundle { use Owner; use Conflict; }
)", "Near"), "bundle declarations failed");
    const bool activated = f.activate({{"Near::Pack", {}, {}}});
    f.require(!activated && f.state.bindings().empty(),
              "conflicting bundle partially committed");
    f.require(f.declare("syntax Leaf : rule { match \"inner\"; }", "Near"),
              "closer rule declaration after conflict failed");
    const auto expander = f.expander("Near");
    f.require(f.activate({{"Near::Owner", {}, {}}, {"Conflict", std::string("other"), {}}}),
              "failed bundle froze staged identities");
    f.require(f.match("candidate inner").expander.value == expander.value,
              "failed bundle froze a staged rule or expander identity");
    f.match("other conflict");
}

void retained_imports(unsigned bits) {
    Fixture f(bits);
    f.state.push_scope();
    f.state.import("DefinitionImports");
    f.require(f.declare(R"(
syntax Owner : expression { prefix "imported"; match rule(Leaf); expand expand; }
)", "Library"), "declaration required a not-yet-visible imported rule or expander");
    f.state.pop_scope();
    f.require(f.declare("syntax Leaf : rule { match \"definition\"; }", "DefinitionImports"),
              "definition-site imported rule failed");
    const auto definition_expander = f.expander("DefinitionImports");
    f.require(f.declare("syntax Leaf : rule { match \"destination\"; }", "DestinationImports"),
              "destination-site imported rule failed");
    f.expander("DestinationImports");
    f.state.import("DestinationImports");
    f.require(f.activate({{"Library::Owner", {}, {}}}), "retained-import activation failed");
    f.require(f.match("imported definition").expander.value == definition_expander.value,
              "activation used its destination imports instead of the declaration's retained imports");
}

void multi_entry_graph_rollback(unsigned bits) {
    Fixture f(bits);
    f.expander();
    f.require(f.declare(R"(
syntax Leaf : rule { match "outer"; }
syntax Stable : expression { prefix "stable"; match "ok"; expand expand; }
)"), "initial graph-rollback declarations failed");
    f.require(f.activate({{"Stable", {}, {}}}), "initial stable activation failed");
    const auto original_bindings = f.state.bindings();
    f.require(f.declare(R"(
syntax Good : expression { prefix "good"; match rule(Leaf); expand expand; }
syntax Recursive : rule { match rule(Recursive); }
syntax Bad : expression { prefix "bad"; match rule(Recursive); expand expand; }
)", "Near"), "late graph failure was diagnosed during declaration");
    const bool activated = f.activate({{"Near::Good", {}, {}}, {"Near::Bad", {}, {}}});
    f.require(!activated && f.state.bindings() == original_bindings,
              "late graph failure partially committed or damaged previous bindings");
    for (const auto* expected : {"requested by syntax activation 'Near::Good'",
                                "requested by syntax activation 'Near::Bad'",
                                "syntax 'Near::Good' defined here", "syntax 'Near::Bad' defined here"})
        f.require(f.messages.str().find(expected) != std::string::npos,
                  "graph failure lost its atomic activation request trail");
    f.require(f.declare("syntax Leaf : rule { match \"inner\"; }", "Near"),
              "closer rule declaration after graph failure failed");
    const auto nearer_expander = f.expander("Near");
    f.messages.str({});
    f.messages.clear();
    f.require(f.activate({{"Near::Good", {}, {}}}), "retry after graph failure failed");
    f.require(f.messages.str().find("requested by syntax activation") == std::string::npos,
              "successful activation emitted failure ancestry");
    f.require(f.match("good inner").expander.value == nearer_expander.value,
              "late graph failure froze a staged rule or expander identity");
    f.match("stable ok");
}
} // namespace

int main() {
    for (const unsigned bits : {32u, 64u}) {
        local_and_forward(bits);
        graph_failure_and_stability(bits);
        multi_entry_rollback(bits);
        multi_entry_graph_rollback(bits);
        retained_imports(bits);
    }
}
